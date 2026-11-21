// SPDX-License-Identifier: GPL-3.0-or-later
#include "settingswindow.h"

#include "config.h"
#include "pages.h"
#include "widgets.h"

#include <karyo/ui/navigation.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QScrollArea>
#include <QStackedWidget>
#include <QSysInfo>
#include <QTimer>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;

namespace {

// Avatar, name and "login@host · Administrator".
class UserCard : public QWidget
{
public:
    explicit UserCard(QWidget *parent)
        : QWidget(parent)
    {
        setFixedHeight(84);
        m_name = Config::userName();
        m_detail = Config::userLogin() + QLatin1Char('@') + QSysInfo::machineHostName();
        if (Config::isAdministrator())
            m_detail += QStringLiteral(" · ") + SettingsWindow::tr("Administrator");
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF avatar(8, (height() - 64) / 2.0, 64, 64);
        Ui::fillPill(p, avatar, Ui::sage(500));
        p.setFont(Ui::bodyFont(22, QFont::Bold));
        p.setPen(Ui::neutral(100));
        p.drawText(avatar, Qt::AlignCenter, Config::initials(m_name));
        const int x = 8 + 64 + 14;
        p.setFont(Ui::bodyFont(15, QFont::DemiBold));
        p.setPen(t.text);
        p.drawText(QRect(x, height() / 2 - 22, width() - x, 22), Qt::AlignVCenter | Qt::AlignLeft, m_name);
        p.setFont(Ui::bodyFont(12.5));
        p.setPen(t.muted);
        p.drawText(QRect(x, height() / 2, width() - x, 20), Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(p.font()).elidedText(m_detail, Qt::ElideRight, width() - x));
    }

private:
    QString m_name;
    QString m_detail;
};

} // namespace

SettingsWindow::SettingsWindow()
    : m_back(new Ui::IconButton(QStringLiteral("arrow-left"), nullptr, {36, 34}, 18))
    , m_title(new QLabel(tr("Settings")))
{
    setWindowTitle(tr("Settings"));
    m_sections = {
        {QStringLiteral("system"), QStringLiteral("monitor"), tr("System"), createSystemPage},
        {QStringLiteral("devices"), QStringLiteral("bluetooth"), tr("Bluetooth & devices"), createDevicesPage},
        {QStringLiteral("network"), QStringLiteral("wifi"), tr("Network & internet"), createNetworkPage},
        {QStringLiteral("personalization"), QStringLiteral("palette"), tr("Personalization"), createPersonalizationPage},
        {QStringLiteral("apps"), QStringLiteral("layout-grid"), tr("Apps"), createAppsPage},
        {QStringLiteral("accounts"), QStringLiteral("circle-user-round"), tr("Accounts"), createAccountsPage},
        {QStringLiteral("time"), QStringLiteral("languages"), tr("Time & language"), createTimeLanguagePage},
        {QStringLiteral("privacy"), QStringLiteral("shield"), tr("Privacy & security"), createPrivacyPage},
        {QStringLiteral("updates"), QStringLiteral("refresh-cw"), tr("Updates"), createUpdatesPage},
        {QStringLiteral("about"), QStringLiteral("info"), tr("About Karyo"), createAboutPage},
    };
    m_current = 3;

    m_back->setObjectName("settingsBack");
    m_back->setHint(tr("Back"));
    m_back->setFocusPolicy(Qt::NoFocus);
    connect(m_back, &QAbstractButton::clicked, this, &SettingsWindow::back);
    m_title->setFont(Ui::bodyFont(13.5, QFont::DemiBold));
    titleLayout()->setContentsMargins(18, 0, 8, 0);
    titleLayout()->addWidget(m_back);
    titleLayout()->addWidget(m_title);
    titleLayout()->addStretch(1);
    setTitleHeight(40);

    build();
    // The theme and accent change from this very window: everything is made
    // again with the new colors, after the click that changed them returns.
    connect(Ui::ThemeNotifier::instance(), &Ui::ThemeNotifier::changed, this,
            [this] { QTimer::singleShot(0, this, &SettingsWindow::build); });
    countUpdates(this, [this](int count) {
        m_updates = count;
        if (m_nav && count > 0)
            m_nav->setBadge(8, QString::number(count));
    });
}

void SettingsWindow::build()
{
    const Ui::Theme &t = Ui::Theme::current();
    setBodyColor(t.pane);
    setControlColors(t.muted, t.hover);
    m_back->setGlyphColor(t.text);
    QPalette palette = m_title->palette();
    palette.setColor(QPalette::WindowText, t.text);
    m_title->setPalette(palette);
    qDeleteAll(m_subPages);
    m_subPages.clear();
    m_pages.clear();
    for (int i = 0; i < m_sections.size(); ++i)
        m_pages.append(nullptr);

    auto *body = new QWidget;
    auto *row = new QHBoxLayout(body);
    row->setContentsMargins(20, 4, 12, 0);
    row->setSpacing(28);

    auto *side = new QVBoxLayout;
    side->setSpacing(14);
    auto *sideWidget = new QWidget(body);
    sideWidget->setFixedWidth(290);
    sideWidget->setLayout(side);
    side->setContentsMargins(0, 0, 0, 20);
    side->addWidget(new UserCard(sideWidget));
    m_find = new Ui::SearchField(tr("Find a setting"), sideWidget);
    m_find->setObjectName("settingsSearch");
    m_find->setFillColor(t.card);
    connect(m_find->lineEdit(), &QLineEdit::textChanged, this, &SettingsWindow::find);
    connect(m_find->lineEdit(), &QLineEdit::returnPressed, this, [this] {
        for (int i = 0; i < m_sections.size(); ++i) {
            if (m_nav->isItemVisible(i)) {
                showIndex(i);
                return;
            }
        }
    });
    side->addWidget(m_find);
    m_nav = new Ui::NavList(sideWidget);
    m_nav->setObjectName("settingsNavigation");
    for (const Section &section : std::as_const(m_sections))
        m_nav->addItem(section.glyph, section.name);
    if (m_updates > 0)
        m_nav->setBadge(8, QString::number(m_updates));
    connect(m_nav, &Ui::NavList::activated, this, &SettingsWindow::showIndex);
    side->addWidget(m_nav);
    side->addStretch(1);
    row->addWidget(sideWidget);

    m_stack = new QStackedWidget(body);
    m_stack->setObjectName("settingsPages");
    for (int i = 0; i < m_sections.size(); ++i)
        m_stack->addWidget(new QWidget);
    row->addWidget(m_stack, 1);
    setCentralWidget(body);
    showIndex(m_current);
}

QWidget *SettingsWindow::wrap(SettingsPage *page)
{
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    page->setContentsMargins(0, 18, 20, 0);
    scroll->setWidget(page);
    // setWidget() turns the fill on; the page sits on the window's ground.
    page->setAutoFillBackground(false);
    connect(page, &SettingsPage::subPageRequested, this, &SettingsWindow::openSubPage);
    return scroll;
}

SettingsPage *SettingsWindow::page(int index)
{
    if (!m_pages.at(index)) {
        SettingsPage *created = m_sections.at(index).create(nullptr);
        QWidget *placeholder = m_stack->widget(index);
        m_stack->insertWidget(index, wrap(created));
        m_stack->removeWidget(placeholder);
        delete placeholder;
        m_pages[index] = created;
    }
    return m_pages.at(index);
}

void SettingsWindow::showIndex(int index)
{
    if (index < 0 || index >= m_sections.size())
        return;
    qDeleteAll(m_subPages);
    m_subPages.clear();
    m_current = index;
    SettingsPage *shown = page(index);
    m_stack->setCurrentIndex(index);
    m_nav->setCurrentIndex(index);
    shown->refresh();
    m_back->setEnabled(false);
}

void SettingsWindow::showSection(const QString &key)
{
    // Storage is a page of System.
    if (key == QLatin1String("storage")) {
        showSection(QStringLiteral("system"));
        openSubPage(createStoragePage(nullptr));
        return;
    }
    for (int i = 0; i < m_sections.size(); ++i) {
        if (m_sections.at(i).key == key)
            showIndex(i);
    }
}

void SettingsWindow::openSubPage(SettingsPage *sub)
{
    QWidget *wrapped = wrap(sub);
    m_stack->addWidget(wrapped);
    m_stack->setCurrentWidget(wrapped);
    m_subPages.append(wrapped);
    m_back->setEnabled(true);
}

void SettingsWindow::back()
{
    if (m_subPages.isEmpty())
        return;
    delete m_subPages.takeLast();
    if (m_subPages.isEmpty()) {
        m_stack->setCurrentIndex(m_current);
        // What the sub-page changed shows on the section's rows.
        m_pages[m_current] = nullptr;
        QWidget *old = m_stack->widget(m_current);
        m_stack->insertWidget(m_current, new QWidget);
        m_stack->removeWidget(old);
        old->deleteLater();
        showIndex(m_current);
    } else {
        m_stack->setCurrentWidget(m_subPages.last());
    }
}

void SettingsWindow::find(const QString &text)
{
    const QString needle = text.trimmed();
    for (int i = 0; i < m_sections.size(); ++i) {
        bool match = needle.isEmpty() || m_sections.at(i).name.contains(needle, Qt::CaseInsensitive);
        if (!match) {
            for (const QString &word : page(i)->keywords()) {
                if (word.contains(needle, Qt::CaseInsensitive)) {
                    match = true;
                    break;
                }
            }
        }
        m_nav->setItemVisible(i, match);
    }
}
