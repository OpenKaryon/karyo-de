// SPDX-License-Identifier: GPL-3.0-or-later
// karyo-calculator [--mode standard|scientific|molarity|dilution|nucleic]:
// Karyo Calculator. Standard and Scientific modes with
// a shared history, and lab modes: Molarity, Dilution and Nucleic acids.
#include "calcpage.h"
#include "labpages.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/navigation.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/style.h>
#include <karyo/ui/theme.h>
#include <karyo/ui/window.h>

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLibraryInfo>
#include <QLocale>
#include <QPainter>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QTranslator>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;

namespace {

// The app's 30px tile with the calculator glyph.
class AppTile : public QWidget
{
public:
    AppTile() { setFixedSize(30, 30); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        Ui::fillRounded(painter, rect(), 9, Ui::sage(700));
        painter.drawPixmap(6, 6, Ui::icon(QStringLiteral("calculator"), 18, Ui::neutral(100), devicePixelRatioF()));
    }
};

// Lab pages scroll when the window is short.
QWidget *scrolling(QWidget *page)
{
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    scroll->viewport()->setAutoFillBackground(false);
    page->setAutoFillBackground(false);
    return scroll;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("karyo-calculator"));
    QApplication::setOrganizationName(QStringLiteral("openkaryon"));
    // Public Wayland identity; Karyo supplies client-side decoration rules.
    QApplication::setDesktopFileName(QStringLiteral("karyo-calculator"));
    Ui::applyStyle();

    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("karyo-calculator"), QStringLiteral("_"),
                        QStringLiteral(":/i18n")))
        QApplication::installTranslator(&translator);

    const Ui::Theme &t = Ui::Theme::current();
    Ui::KaryoWindow window;
    window.setWindowTitle(QObject::tr("Calculator"));
    window.setBodyColor(t.pane);
    auto *title = new QLabel(QObject::tr("Calculator"));
    title->setFont(Ui::bodyFont(14, QFont::DemiBold));
    window.titleLayout()->setContentsMargins(20, 0, 8, 0);
    window.titleLayout()->setSpacing(10);
    window.titleLayout()->addWidget(new AppTile);
    window.titleLayout()->addWidget(title);
    window.titleLayout()->addStretch(1);

    auto *body = new QWidget;
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(12, 0, 12, 12);
    bodyLayout->setSpacing(12);

    auto *navColumn = new QVBoxLayout;
    navColumn->setContentsMargins(0, 4, 0, 10);
    auto *nav = new Ui::NavList;
    nav->setFixedWidth(200);
    const int standard = nav->addItem(QStringLiteral("calculator"), QObject::tr("Standard"));
    const int scientific = nav->addItem(QStringLiteral("sigma"), QObject::tr("Scientific"));
    nav->addHeading(QObject::tr("Lab"));
    const int molarity = nav->addItem(QStringLiteral("flask-conical"), QObject::tr("Molarity"));
    const int dilution = nav->addItem(QStringLiteral("pipette"), QObject::tr("Dilution"));
    const int nucleic = nav->addItem(QStringLiteral("dna"), QObject::tr("Nucleic acids"));
    navColumn->addWidget(nav);
    navColumn->addStretch();
    bodyLayout->addLayout(navColumn);

    auto *panel = new Ui::InsetPanel;
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    auto *stack = new QStackedWidget;
    panelLayout->addWidget(stack);
    bodyLayout->addWidget(panel, 1);

    auto *history = new History(&app);
    auto *standardPage = new CalcPage(false, history);
    auto *scientificPage = new CalcPage(true, history);
    const QHash<int, QWidget *> pages = {
        {standard, standardPage},
        {scientific, scientificPage},
        {molarity, scrolling(new MolarityPage)},
        {dilution, scrolling(new DilutionPage)},
        {nucleic, scrolling(new NucleicPage)},
    };
    for (QWidget *page : pages)
        stack->addWidget(page);
    QSettings settings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                       QStringLiteral("calculator"));
    QObject::connect(nav, &Ui::NavList::currentChanged, stack, [=](int index) {
        QWidget *page = pages.value(index);
        if (!page)
            return;
        stack->setCurrentWidget(page);
        if (auto *calc = qobject_cast<CalcPage *>(page))
            calc->focusDisplay();
        QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                  QStringLiteral("calculator"))
            .setValue(QStringLiteral("mode"), index);
    });
    // --mode opens a mode directly (the desktop file's actions use it).
    int mode = settings.value(QStringLiteral("mode"), standard).toInt();
    const QStringList args = QApplication::arguments();
    const int modeArg = int(args.indexOf(QStringLiteral("--mode")));
    if (modeArg > 0 && modeArg + 1 < args.size()) {
        const QHash<QString, int> names = {{QStringLiteral("standard"), standard},
                                           {QStringLiteral("scientific"), scientific},
                                           {QStringLiteral("molarity"), molarity},
                                           {QStringLiteral("dilution"), dilution},
                                           {QStringLiteral("nucleic"), nucleic}};
        mode = names.value(args.at(modeArg + 1), mode);
    }
    nav->setCurrentIndex(pages.contains(mode) ? mode : standard);

    window.setCentralWidget(body);
    window.resizeToFit({1040, 720});
    window.show();
    if (auto *calc = qobject_cast<CalcPage *>(stack->currentWidget()))
        calc->focusDisplay();
    return QApplication::exec();
}
