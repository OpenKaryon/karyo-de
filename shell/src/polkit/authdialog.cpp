// SPDX-License-Identifier: GPL-3.0-or-later
#include "authdialog.h"

#include "flyout.h"
#include "layersurface.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/password.h>
#include <karyo/ui/theme.h>
#include <karyo/ui/window.h>

#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QScreen>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;

namespace {

constexpr int CardWidth = 500;

void setInk(QLabel *label, const QColor &color)
{
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, color);
    label->setPalette(palette);
}

// The shield in an accent circle at the top of the card.
class Badge : public QWidget
{
public:
    explicit Badge(QWidget *parent)
        : QWidget(parent)
    {
        setFixedSize(52, 52);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Ui::fillPill(p, rect(), t.tint1);
        p.drawPixmap(15, 15, Ui::icon(QStringLiteral("shield"), 22, t.accent, devicePixelRatioF()));
    }
};

} // namespace

AuthDialog::AuthDialog(const QString &message, const QString &detail, const QStringList &identities, QScreen *screen)
    : QWidget(nullptr, Qt::FramelessWindowHint)
    , m_card(new QWidget(this))
    , m_identities(new QComboBox(m_card))
    , m_password(new Ui::PasswordField(m_card))
    , m_status(new QLabel(m_card))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("karyo-authentication"));
    // Full screen above everything, holding the keyboard like Windows' UAC.
    LayerSurface::configure(this, screen, LayerSurface::LayerOverlay,
                            LayerSurface::AnchorTop | LayerSurface::AnchorBottom | LayerSurface::AnchorLeft
                                | LayerSurface::AnchorRight,
                            -1);
    LayerSurface::setKeyboard(this, LayerSurface::KeyboardExclusive);
    resize(screen->size());

    const Ui::Theme &t = Ui::Theme::current();
    auto *v = new QVBoxLayout(m_card);
    v->setContentsMargins(34, 32, 34, 30);
    v->setSpacing(14);
    v->addWidget(new Badge(m_card));
    auto *title = new QLabel(tr("Authentication required"), m_card);
    title->setFont(Ui::headingFont(24));
    setInk(title, t.text);
    v->addWidget(title);
    auto *text = new QLabel(message, m_card);
    text->setWordWrap(true);
    text->setFont(Ui::bodyFont(14.5));
    setInk(text, t.text);
    v->addWidget(text);
    if (!detail.isEmpty()) {
        auto *small = new QLabel(detail, m_card);
        small->setWordWrap(true);
        small->setFont(Ui::monoFont(12));
        setInk(small, t.muted);
        small->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->addWidget(small);
    }
    v->addSpacing(4);
    m_identities->addItems(identities);
    m_identities->setVisible(identities.size() > 1);
    m_identities->setFont(Ui::bodyFont(13.5));
    connect(m_identities, &QComboBox::currentIndexChanged, this, &AuthDialog::identityChanged);
    v->addWidget(m_identities);
    if (identities.size() == 1) {
        auto *who = new QLabel(tr("As %1").arg(identities.first()), m_card);
        who->setFont(Ui::bodyFont(13, QFont::DemiBold));
        setInk(who, t.muted);
        v->addWidget(who);
    }
    m_password->setPlaceholder(tr("Password"));
    connect(m_password, &Ui::PasswordField::submitted, this, &AuthDialog::submitted);
    v->addWidget(m_password);
    m_status->setFont(Ui::bodyFont(12.5));
    m_status->setWordWrap(true);
    setInk(m_status, t.ink1);
    m_status->hide();
    v->addWidget(m_status);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancel = new Ui::PillButton(QString(), tr("Cancel"), m_card);
    cancel->setKind(Ui::PillButton::Kind::Secondary);
    connect(cancel, &QAbstractButton::clicked, this, &AuthDialog::cancelled);
    buttons->addWidget(cancel);
    v->addLayout(buttons);

    m_card->setFixedWidth(CardWidth);
    m_card->adjustSize();
    m_card->move((width() - CardWidth) / 2, (height() - m_card->height()) / 2);
    m_password->setFocus();
}

int AuthDialog::identityIndex() const { return qMax(0, m_identities->currentIndex()); }

QString AuthDialog::password() const { return m_password->text(); }

void AuthDialog::askPassword(const QString &prompt)
{
    // PAM says "Password: " or "Пароль: "; the placeholder says it already.
    QString text = prompt.trimmed();
    if (text.endsWith(QLatin1Char(':')))
        text.chop(1);
    if (!text.isEmpty())
        m_password->setPlaceholder(text);
    m_password->setBusy(false);
    m_password->clear();
    m_password->setFocus();
}

void AuthDialog::showError(const QString &text)
{
    m_status->setText(text);
    m_status->show();
    m_password->showError();
    m_card->adjustSize();
}

void AuthDialog::showInfo(const QString &text)
{
    m_status->setText(text);
    m_status->show();
    m_card->adjustSize();
}

void AuthDialog::setBusy(bool busy) { m_password->setBusy(busy); }

void AuthDialog::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), QColor(0, 0, 0, 115));
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    m_card->move((width() - m_card->width()) / 2, (height() - m_card->height()) / 2);
    Flyout::paintCard(p, m_card->geometry(), 28);
}

void AuthDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        Q_EMIT cancelled();
        return;
    }
    QWidget::keyPressEvent(event);
}

void AuthDialog::mousePressEvent(QMouseEvent *event)
{
    // The dimmed screen does nothing; only Cancel or Escape dismiss.
    QWidget::mousePressEvent(event);
}
