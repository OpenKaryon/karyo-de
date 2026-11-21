// SPDX-License-Identifier: GPL-3.0-or-later
// karyo-monitor: System Monitor (designs 1k, 1l).
#include "performancepage.h"
#include "processespage.h"
#include "sampler.h"
#include "tablepages.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/navigation.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/style.h>
#include <karyo/ui/theme.h>
#include <karyo/ui/window.h>

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QStackedWidget>
#include <QTranslator>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;

namespace {

// The app's 30px sage tile with the activity glyph.
class AppTile : public QWidget
{
public:
    AppTile() { setFixedSize(30, 30); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        Ui::fillRounded(painter, rect(), 9, Ui::sage(600));
        painter.drawPixmap(6, 6, Ui::icon(QStringLiteral("activity"), 18, Ui::neutral(100), devicePixelRatioF()));
    }
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("karyo-monitor"));
    QApplication::setOrganizationName(QStringLiteral("openkaryon"));
    QApplication::setDesktopFileName(QStringLiteral("karyo-monitor"));
    Ui::applyStyle();
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("karyo-monitor"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QApplication::installTranslator(&translator);

    const Ui::Theme &t = Ui::Theme::current();
    Ui::KaryoWindow window;
    window.setWindowTitle(QObject::tr("System Monitor"));
    window.setBodyColor(t.pane);

    // Title strip: app tile and name, then the centered search field.
    auto *title = new QLabel(QObject::tr("System Monitor"));
    title->setFont(Ui::bodyFont(14, QFont::DemiBold));
    auto *search = new Ui::SearchField(QObject::tr("Type a name, PID or environment to search"));
    search->setFixedWidth(560);
    search->setFillColor(t.card);
    window.titleLayout()->setContentsMargins(20, 0, 8, 0);
    window.titleLayout()->addWidget(new AppTile);
    window.titleLayout()->addWidget(title);
    window.titleLayout()->addStretch(1);
    window.titleLayout()->addWidget(search);
    window.titleLayout()->addStretch(1);

    auto *sampler = new Sampler(&app);
    auto *body = new QWidget;
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(12, 0, 12, 12);
    bodyLayout->setSpacing(12);

    auto *navColumn = new QVBoxLayout;
    navColumn->setContentsMargins(0, 4, 0, 10);
    auto *nav = new Ui::NavList;
    nav->setFixedWidth(216);
    const int processes = nav->addItem(QStringLiteral("layout-list"), QObject::tr("Processes"));
    const int performance = nav->addItem(QStringLiteral("activity"), QObject::tr("Performance"));
    const int startup = nav->addItem(QStringLiteral("rocket"), QObject::tr("Startup apps"));
    const int users = nav->addItem(QStringLiteral("users"), QObject::tr("Users"));
    const int details = nav->addItem(QStringLiteral("list"), QObject::tr("Details"));
    const int services = nav->addItem(QStringLiteral("cog"), QObject::tr("Services"));
    navColumn->addWidget(nav);
    navColumn->addStretch();
    bodyLayout->addLayout(navColumn);

    auto *panel = new Ui::InsetPanel;
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    auto *stack = new QStackedWidget;
    panelLayout->addWidget(stack);
    bodyLayout->addWidget(panel, 1);

    auto *processesPage = new ProcessesPage(sampler);
    auto *detailsPage = new DetailsPage(sampler);
    auto *usersPage = new UsersPage(sampler);
    auto *servicesPage = new ServicesPage;
    auto *startupPage = new StartupPage;
    QHash<int, QWidget *> pages = {{processes, processesPage},   {performance, new PerformancePage(sampler)},
                                   {startup, startupPage},       {users, usersPage},
                                   {details, detailsPage},       {services, servicesPage}};
    for (int i = 0; i <= services; ++i)
        stack->addWidget(pages.value(i));
    QObject::connect(nav, &Ui::NavList::currentChanged, stack, [stack, pages](int index) {
        if (QWidget *page = pages.value(index))
            stack->setCurrentWidget(page);
    });
    nav->setCurrentIndex(processes);

    QObject::connect(search->lineEdit(), &QLineEdit::textChanged, &app, [=](const QString &text) {
        processesPage->setFilter(text);
        detailsPage->setFilter(text);
        servicesPage->setFilter(text);
        startupPage->setFilter(text);
        usersPage->setFilter(text);
    });

    window.setCentralWidget(body);
    window.resizeToFit({1520, 900});
    window.show();
    return QApplication::exec();
}
