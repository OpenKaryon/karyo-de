// SPDX-License-Identifier: GPL-3.0-or-later
#include "karyo/ui/password.h"

#include "karyo/ui/icons.h"
#include "karyo/ui/paint.h"
#include "karyo/ui/theme.h"

#include <QAbstractButton>
#include <QEvent>
#include <QLineEdit>
#include <QPainter>
#include <QVariantAnimation>
#include <QtMath>

namespace Karyo::Ui {

namespace {

constexpr int Height = 54;
constexpr int Gap = 8;

class GoButton : public QAbstractButton
{
public:
    explicit GoButton(QWidget *parent)
        : QAbstractButton(parent)
    {
        setFixedSize(Height, Height);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Theme &t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor fill = isDown() ? accent(700) : underMouse() ? accent(600) : t.accent;
        if (!isEnabled())
            fill = alpha(t.accent, 45);
        fillPill(p, rect(), fill);
        p.drawPixmap((width() - 20) / 2, (height() - 20) / 2,
                     Karyo::Ui::icon(QStringLiteral("arrow-right"), 20, neutral(100), devicePixelRatioF()));
    }
};

} // namespace

PasswordField::PasswordField(QWidget *parent)
    : QWidget(parent)
    , m_edit(new QLineEdit(this))
    , m_go(new GoButton(this))
    , m_shake(new QVariantAnimation(this))
{
    setFixedHeight(Height);
    m_edit->setEchoMode(QLineEdit::Password);
    m_edit->setFrame(false);
    m_edit->setFont(bodyFont(16));
    m_edit->setAttribute(Qt::WA_TranslucentBackground);
    QPalette palette = m_edit->palette();
    palette.setColor(QPalette::Base, Qt::transparent);
    m_edit->setPalette(palette);
    m_edit->installEventFilter(this);
    setFocusProxy(m_edit);
    connect(m_edit, &QLineEdit::returnPressed, this, [this] {
        if (!m_busy)
            Q_EMIT submitted();
    });
    connect(m_edit, &QLineEdit::textEdited, this, [this] {
        if (m_error) {
            m_error = false;
            update();
        }
    });
    connect(m_go, &QAbstractButton::clicked, this, [this] {
        if (!m_busy)
            Q_EMIT submitted();
    });
    // A damped side-to-side shake for a wrong password.
    m_shake->setDuration(420);
    m_shake->setStartValue(0.0);
    m_shake->setEndValue(1.0);
    connect(m_shake, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        const qreal x = v.toReal();
        m_offset = qRound(qSin(x * M_PI * 6) * 10 * (1 - x));
        resizeEvent(nullptr);
        update();
    });
}

QString PasswordField::text() const { return m_edit->text(); }

void PasswordField::clear() { m_edit->clear(); }

void PasswordField::setPlaceholder(const QString &text) { m_edit->setPlaceholderText(text); }

void PasswordField::setBusy(bool busy)
{
    m_busy = busy;
    m_edit->setReadOnly(busy);
    m_go->setEnabled(!busy);
    update();
}

void PasswordField::showError()
{
    m_error = true;
    m_edit->selectAll();
    m_shake->stop();
    m_shake->start();
    update();
}

QSize PasswordField::sizeHint() const { return {378 + Gap + Height, Height}; }

QRect PasswordField::pillRect() const { return QRect(m_offset, 0, width() - Gap - Height, Height); }

void PasswordField::resizeEvent(QResizeEvent *)
{
    const QRect pill = pillRect();
    m_edit->setGeometry(pill.adjusted(54, 6, -20, -6));
    m_go->move(width() - Height + m_offset, 0);
}

bool PasswordField::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit && (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut))
        update();
    return QWidget::eventFilter(watched, event);
}

void PasswordField::paintEvent(QPaintEvent *)
{
    const Theme &t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF pill = QRectF(pillRect()).adjusted(1, 1, -1, -1);
    const QColor border = m_error ? accent(700) : m_edit->hasFocus() ? t.accent : t.edge;
    fillRounded(p, pill, pill.height() / 2, t.win, Qt::transparent);
    p.setPen(QPen(border, m_error || m_edit->hasFocus() ? 1.6 : 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
    p.drawPixmap(int(pill.left()) + 22, (height() - 18) / 2,
                 icon(QStringLiteral("lock"), 18, t.muted, devicePixelRatioF()));
}

} // namespace Karyo::Ui
