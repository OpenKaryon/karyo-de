// SPDX-License-Identifier: GPL-3.0-or-later
#include "pamcheck.h"
#include <QSignalSpy>
#include <QtTest>
#include <security/pam_appl.h>
#include <cstdlib>
#include <cstring>

namespace {
int startResult, authResult, accountResult, refreshes, ends;
const pam_conv *conversation;
QString receivedUser, receivedService;
int storage;
}
extern "C" {
int pam_start(const char *service, const char *user, const pam_conv *conv, pam_handle_t **handle)
{
    receivedService = QString::fromUtf8(service);
    receivedUser = QString::fromUtf8(user);
    conversation = conv;
    *handle = startResult == PAM_SUCCESS ? reinterpret_cast<pam_handle_t *>(&storage) : nullptr;
    return startResult;
}
int pam_authenticate(pam_handle_t *, int)
{
    const pam_message message{PAM_PROMPT_ECHO_OFF, "Password:"};
    const pam_message *messages = &message;
    pam_response *response = nullptr;
    const int result = conversation->conv(1, &messages, &response, conversation->appdata_ptr);
    const bool correct = result == PAM_SUCCESS && response && response->resp
                         && std::strcmp(response->resp, "fixture-secret") == 0;
    if (response) {
        std::free(response->resp);
        std::free(response);
    }
    return correct ? authResult : PAM_CONV_ERR;
}
int pam_acct_mgmt(pam_handle_t *, int) { return accountResult; }
int pam_setcred(pam_handle_t *, int) { ++refreshes; return PAM_SUCCESS; }
int pam_end(pam_handle_t *handle, int) { if (handle) ++ends; return PAM_SUCCESS; }
}

class PamTest : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        startResult = authResult = accountResult = PAM_SUCCESS;
        refreshes = ends = 0;
    }
    void policy_data()
    {
        QTest::addColumn<int>("start");
        QTest::addColumn<int>("auth");
        QTest::addColumn<int>("account");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("accepted") << PAM_SUCCESS << PAM_SUCCESS << PAM_SUCCESS << true;
        QTest::newRow("bad-password") << PAM_SUCCESS << PAM_AUTH_ERR << PAM_SUCCESS << false;
        QTest::newRow("expired-account") << PAM_SUCCESS << PAM_SUCCESS << PAM_ACCT_EXPIRED << false;
        QTest::newRow("missing-service") << PAM_SYSTEM_ERR << PAM_SUCCESS << PAM_SUCCESS << false;
    }
    void policy()
    {
        QFETCH(int, start); QFETCH(int, auth); QFETCH(int, account); QFETCH(bool, accepted);
        startResult = start; authResult = auth; accountResult = account;
        PamCheck checker(QStringLiteral("karyo-lock"));
        QSignalSpy done(&checker, &PamCheck::finished);
        checker.check(QStringLiteral("fixture-user"), QStringLiteral("fixture-secret"));
        QVERIFY(done.wait(5000));
        QCOMPARE(done.size(), 1);
        QCOMPARE(done.first().first().toBool(), accepted);
        QCOMPARE(receivedUser, QStringLiteral("fixture-user"));
        QCOMPARE(receivedService, QStringLiteral("karyo-lock"));
        QCOMPARE(ends, start == PAM_SUCCESS ? 1 : 0);
        QCOMPARE(refreshes, accepted ? 1 : 0);
    }
};
QTEST_GUILESS_MAIN(PamTest)
#include "pamtest.moc"
