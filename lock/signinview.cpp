// SPDX-License-Identifier: GPL-3.0-or-later
#include "signinview.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/password.h>
#include <karyo/ui/theme.h>
#include <karyo/ui/window.h>

#include <QAbstractButton>
#include <QDateTime>
#include <QFile>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;

namespace {

void setInk(QLabel *label, const QColor &color)
{
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, color);
    label->setPalette(palette);
}

// Accent text link under the password.
class Link : public QAbstractButton
{
public:
    Link(const QString &text, QWidget *parent)
        : QAbstractButton(parent)
    {
        setText(text);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setAttribute(Qt::WA_Hover);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override
    {
        return {QFontMetrics(Ui::bodyFont(14, QFont::DemiBold)).horizontalAdvance(text()) + 26, 30};
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (underMouse() || hasFocus())
            Ui::fillPill(p, rect(), t.hover);
        p.setFont(Ui::bodyFont(14, QFont::DemiBold));
        p.setPen(t.ink1);
        p.drawText(rect(), Qt::AlignCenter, text());
    }
};

// The power glyph of the bottom pill, as a button.
class PowerButton : public QAbstractButton
{
public:
    explicit PowerButton(QWidget *parent)
        : QAbstractButton(parent)
    {
        setFixedSize(44, 44);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (underMouse() || isDown() || hasFocus())
            Ui::fillPill(p, rect(), isDown() ? t.press : t.hover);
        p.drawPixmap(12, 12, Ui::icon(QStringLiteral("power"), 20, t.text, devicePixelRatioF()));
    }
};

// The desktop is independent of the host distribution.
QString versionLine()
{
    return QStringLiteral("Karyo Desktop");
}

// The first keyboard layout of the session, or of the system for the
// greeter: "us" shows as EN like the taskbar.
QString layoutCode()
{
    QString layouts = qEnvironmentVariable("XKB_DEFAULT_LAYOUT");
    if (layouts.isEmpty()) {
        QFile file(QStringLiteral("/etc/default/keyboard"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            layouts = QRegularExpression(QStringLiteral("XKBLAYOUT=\"?([^\"\n]+)"))
                          .match(QString::fromUtf8(file.readAll()))
                          .captured(1);
    }
    const QString first = layouts.section(QLatin1Char(','), 0, 0).trimmed();
    if (first.isEmpty() || first == QLatin1String("us") || first == QLatin1String("gb"))
        return QStringLiteral("EN");
    return first.left(2).toUpper();
}

// The card behind showChoices().
class ChoiceCard : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Ui::fillRounded(p, QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, t.glassStrong, t.edge);
    }
};

} // namespace

SignInView::SignInView(bool decorationOnly, QWidget *parent)
    // A whole screen of its own: no Karyo client frame.
    : QWidget(parent, Qt::FramelessWindowHint)
    , m_decorationOnly(decorationOnly)
    , m_version(versionLine())
    , m_layout(layoutCode())
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &SignInView::tick);
    timer->start(1000);
    tick();
    if (decorationOnly)
        return;

    const Ui::Theme &t = Ui::Theme::current();
    m_userBlock = new QWidget(this);
    auto *v = new QVBoxLayout(m_userBlock);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(18);
    auto *who = new QHBoxLayout;
    who->setSpacing(18);
    who->addSpacing(84);  // the avatar is painted
    auto *names = new QVBoxLayout;
    names->setSpacing(2);
    names->addStretch(1);
    m_nameLabel = new QLabel(m_userBlock);
    m_nameLabel->setFont(Ui::headingFont(28));
    setInk(m_nameLabel, t.text);
    names->addWidget(m_nameLabel);
    m_detailLabel = new QLabel(m_userBlock);
    m_detailLabel->setFont(Ui::bodyFont(14));
    setInk(m_detailLabel, t.muted);
    names->addWidget(m_detailLabel);
    names->addStretch(1);
    who->addLayout(names, 1);
    auto *whoBox = new QWidget(m_userBlock);
    whoBox->setFixedHeight(84);
    whoBox->setLayout(who);
    v->addWidget(whoBox);
    m_password = new Ui::PasswordField(m_userBlock);
    m_password->setFixedWidth(378 + 8 + 54);
    v->addWidget(m_password);
    m_message = new QLabel(m_userBlock);
    m_message->setFont(Ui::bodyFont(13.5));
    m_message->setWordWrap(true);
    setInk(m_message, t.ink1);
    m_message->hide();
    v->addWidget(m_message);
    m_links = new QWidget(m_userBlock);
    auto *links = new QHBoxLayout(m_links);
    links->setContentsMargins(0, 0, 0, 0);
    links->setSpacing(4);
    links->addStretch(1);
    v->addWidget(m_links);
    m_userBlock->setFixedWidth(m_password->width());

    m_power = new PowerButton(this);
    m_power->setToolTip(tr("Power"));
    connect(m_power, &QAbstractButton::clicked, this, &SignInView::showPowerMenu);
}

void SignInView::setUser(const QString &name, const QString &detail)
{
    m_name = name;
    if (m_nameLabel) {
        m_nameLabel->setText(name);
        m_detailLabel->setText(detail);
        m_detailLabel->setVisible(!detail.isEmpty());
    }
    update();
}

QAbstractButton *SignInView::addLink(const QString &text)
{
    auto *link = new Link(text, m_links);
    auto *layout = static_cast<QHBoxLayout *>(m_links->layout());
    layout->insertWidget(layout->count() - 1, link);
    return link;
}

void SignInView::showMessage(const QString &text)
{
    if (!m_message)
        return;
    m_message->setText(text);
    m_message->setVisible(!text.isEmpty());
    placeUserBlock();
}

void SignInView::showChoices(QWidget *anchor, const QList<QPair<QString, QString>> &choices,
                             const std::function<void(int)> &chosen)
{
    delete m_choices;
    auto *card = new ChoiceCard(this);
    m_choices = card;
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(6, 6, 6, 6);
    v->setSpacing(2);
    for (int i = 0; i < choices.size(); ++i) {
        auto *row = new Ui::PillButton(choices.at(i).first, choices.at(i).second, card);
        row->setKind(Ui::PillButton::Kind::Ghost);
        connect(row, &QAbstractButton::clicked, this, [this, chosen, i] {
            closeChoices();
            chosen(i);
        });
        v->addWidget(row);
    }
    card->adjustSize();
    card->setFixedWidth(qMax(card->width(), 200));
    // Above the anchor, right edges aligned, inside the screen.
    const QPoint corner = anchor->mapTo(this, QPoint(anchor->width(), 0));
    card->move(qBound(8, corner.x() - card->width(), width() - card->width() - 8),
               qMax(8, corner.y() - card->height() - 14));
    card->show();
    card->raise();
    if (QWidget *first = card->findChild<QAbstractButton *>())
        first->setFocus();
}

void SignInView::showPowerMenu()
{
    const QStringList commands{QStringLiteral("suspend"), QStringLiteral("reboot"), QStringLiteral("poweroff")};
    showChoices(m_power,
                {{QStringLiteral("moon"), tr("Sleep")},
                 {QStringLiteral("rotate-cw"), tr("Restart")},
                 {QStringLiteral("power"), tr("Shut down")}},
                [commands](int index) {
                    QProcess::startDetached(QStringLiteral("systemctl"), {commands.at(index)});
                });
}

void SignInView::mousePressEvent(QMouseEvent *event)
{
    // A click beside the choices closes them.
    if (m_choices && !m_choices->geometry().contains(event->position().toPoint()))
        closeChoices();
    QWidget::mousePressEvent(event);
}

void SignInView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_choices) {
        closeChoices();
        return;
    }
    QWidget::keyPressEvent(event);
}

void SignInView::closeChoices()
{
    if (!m_choices)
        return;
    m_choices->deleteLater();
    m_choices = nullptr;
    // Typing goes back to the password.
    if (m_password)
        m_password->setFocus();
}

void SignInView::tick()
{
    const QLocale locale;
    const QDateTime now = QDateTime::currentDateTime();
    const QString time = locale.toString(now.time(), QStringLiteral("HH:mm"));
    QString date = locale.toString(now.date(), locale.language() == QLocale::English ? QStringLiteral("dddd, MMMM d")
                                                                                    : QStringLiteral("dddd, d MMMM"));
    if (!date.isEmpty())
        date[0] = date.at(0).toUpper();
    if (time != m_time || date != m_date) {
        m_time = time;
        m_date = date;
        update();
    }
}

void SignInView::placeUserBlock()
{
    if (!m_userBlock)
        return;
    m_userBlock->adjustSize();
    const qreal scale = height() / 1080.0;
    const int bottom = height() - qRound(90 * scale);
    const int top = qMin(qRound(700 * scale), bottom - m_userBlock->height());
    m_userBlock->move(qRound(160 * scale), qMax(0, top));
}

void SignInView::resizeEvent(QResizeEvent *)
{
    placeUserBlock();
    const qreal s = height() / 1080.0;
    if (m_power)
        m_power->move(width() - qRound(41 * s) - m_power->width(), height() - qRound(37 * s) - m_power->height() + 5);
}

void SignInView::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), t.desk);
    const qreal s = height() / 1080.0;
    const qreal w = width();
    auto circle = [&](qreal cx, qreal cy, qreal r, const QColor &color) {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(cx, cy), r, r);
    };
    // The mirrored composition: sage at the top left, the cell at the right.
    circle(100 * s, 60 * s, 260 * s, t.blobD);
    circle(w - 370 * s, 880 * s, 590 * s, t.blobA);
    circle(w - 460 * s, 700 * s, 220 * s, t.blobB);
    circle(w - 400 * s, 640 * s, 60 * s, t.blobE);
    circle(770 * s, 131 * s, 11 * s, t.blobE);
    circle(826 * s, 176 * s, 6 * s, t.blobB);

    // Mark and wordmark.
    Ui::drawKaryoMark(p, QRectF(160 * s, 82 * s, 40 * s, 40 * s));
    p.setFont(Ui::headingFont(28 * s));
    p.setPen(t.text);
    p.drawText(QRectF(214 * s, 78 * s, 400 * s, 48 * s), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Karyo"));
    if (m_decorationOnly)
        return;

    // Clock and date.
    p.setFont(Ui::headingFont(176 * s));
    p.drawText(QRectF(152 * s, 240 * s, w * 0.6, 200 * s), Qt::AlignVCenter | Qt::AlignLeft, m_time);
    p.setFont(Ui::bodyFont(28 * s, QFont::DemiBold));
    p.drawText(QRectF(160 * s, 430 * s, w * 0.6, 50 * s), Qt::AlignVCenter | Qt::AlignLeft, m_date);

    // Avatar with initials, painted beside the name labels.
    if (m_userBlock && !m_name.isEmpty()) {
        const QRectF avatar(m_userBlock->x(), m_userBlock->y(), 84, 84);
        Ui::fillPill(p, avatar, Ui::sage(500));
        QString initials;
        for (const QString &word : m_name.split(QLatin1Char(' '), Qt::SkipEmptyParts).mid(0, 2))
            initials += word.at(0).toUpper();
        p.setFont(Ui::bodyFont(30, QFont::DemiBold));
        p.setPen(Ui::neutral(100));
        p.drawText(avatar, Qt::AlignCenter, initials);
    }

    // Version line and the glass pill.
    p.setFont(Ui::bodyFont(12.5));
    p.setPen(t.muted);
    p.drawText(QRectF(160 * s, height() - 70 * s, w / 2, 40 * s), Qt::AlignVCenter | Qt::AlignLeft, m_version);
    const QRectF pill(w - 41 * s - 212, height() - 37 * s - 55 + 5, 212, 55);
    Ui::fillRounded(p, pill, pill.height() / 2, t.glassStrong, t.edge);
    p.setFont(Ui::bodyFont(13, QFont::DemiBold));
    p.setPen(t.text);
    p.drawText(QRectF(pill.left() + 14, pill.top(), 40, pill.height()), Qt::AlignCenter, m_layout);
    const qreal dpr = devicePixelRatioF();
    p.drawPixmap(QPointF(pill.left() + 74, pill.center().y() - 10), Ui::icon(QStringLiteral("wifi"), 20, t.text, dpr));
    p.drawPixmap(QPointF(pill.left() + 124, pill.center().y() - 10),
                 Ui::icon(QStringLiteral("accessibility"), 20, t.text, dpr));
}
