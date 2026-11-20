// SPDX-License-Identifier: GPL-3.0-or-later
#include "pamcheck.h"

#include <QCoreApplication>
#include <QPointer>
#include <QThread>

#include <security/pam_appl.h>

#include <cstdlib>
#include <cstring>

namespace {

// Never log credentials. PAM owns successful replies and clears them later.
int converse(int count, const pam_message **messages, pam_response **responses, void *data)
{
    if (count <= 0 || count > PAM_MAX_NUM_MSG || !messages || !responses || !data)
        return PAM_CONV_ERR;
    *responses = nullptr;
    auto *replies = static_cast<pam_response *>(std::calloc(size_t(count), sizeof(pam_response)));
    if (!replies)
        return PAM_BUF_ERR;
    for (int i = 0; i < count; ++i) {
        const int style = messages[i] ? messages[i]->msg_style : -1;
        if (style == PAM_PROMPT_ECHO_OFF || style == PAM_PROMPT_ECHO_ON) {
            replies[i].resp = strdup(static_cast<const char *>(data));
            if (replies[i].resp)
                continue;
        } else if (style == PAM_TEXT_INFO || style == PAM_ERROR_MSG) {
            continue;
        }
        for (int j = 0; j <= i; ++j) {
            if (replies[j].resp) {
                explicit_bzero(replies[j].resp, std::strlen(replies[j].resp));
                std::free(replies[j].resp);
            }
        }
        std::free(replies);
        return PAM_CONV_ERR;
    }
    *responses = replies;
    return PAM_SUCCESS;
}

bool authenticate(const QByteArray &service, const QByteArray &user, QByteArray &password)
{
    pam_conv conversation{converse, password.data()};
    pam_handle_t *handle = nullptr;
    int status = pam_start(service.constData(), user.constData(), &conversation, &handle);
    if (status == PAM_SUCCESS)
        status = pam_authenticate(handle, 0);
    if (status == PAM_SUCCESS)
        status = pam_acct_mgmt(handle, 0);
    if (status == PAM_SUCCESS)
        pam_setcred(handle, PAM_REFRESH_CRED);
    if (handle)
        pam_end(handle, status);
    explicit_bzero(password.data(), size_t(password.size()));
    return status == PAM_SUCCESS;
}

} // namespace

PamCheck::PamCheck(const QString &service, QObject *parent)
    : QObject(parent)
    , m_service(service.toUtf8())
{
}

void PamCheck::check(const QString &user, const QString &password)
{
    const QByteArray service = m_service;
    const QByteArray name = user.toLocal8Bit();
    QByteArray secret = password.toUtf8();
    QPointer<PamCheck> self(this);
    QThread *thread = QThread::create([self, service, name, secret = std::move(secret)]() mutable {
        const bool ok = authenticate(service, name, secret);
        QMetaObject::invokeMethod(
            qApp, [self, ok] {
                if (self)
                    Q_EMIT self->finished(ok);
            },
            Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
