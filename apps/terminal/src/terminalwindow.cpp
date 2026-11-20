// SPDX-License-Identifier: GPL-3.0-or-later
#include "terminalwindow.h"

#include <karyo/ui/menu.h>
#include <karyo/ui/theme.h>

#include <qtermwidget.h>

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

namespace Ui = Karyo::Ui;

namespace {

// Selecting text with the left button copies it, as in PuTTY: the copy
// happens when the button is released, once the selection is final.
class CopyOnSelect : public QObject
{
public:
    explicit CopyOnSelect(QTermWidget *term)
        : QObject(term)
        , m_term(term)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonRelease
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            QTimer::singleShot(0, m_term, [term = m_term] {
                if (!term->selectedText().isEmpty())
                    term->copyClipboard();
            });
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QTermWidget *m_term;
};

QColor stripColor() { return Ui::mix(Ui::neutral(900), Qt::black, 55); }
QColor hoverColor() { return Ui::alpha(Ui::neutral(100), 10); }

QString rgb(const QColor &c)
{
    return QStringLiteral("%1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue());
}

} // namespace

TerminalWindow::TerminalWindow(QWidget *parent)
    : KaryoWindow(parent)
    , m_tabs(new Ui::TitleTabs)
    , m_stack(new QStackedWidget)
{
    const Ui::Theme &t = Ui::Theme::current();
    setWindowTitle(tr("Terminal"));
    // The terminal is dark in both themes (design 1f).
    setBodyColor(t.term);
    setTitleColor(stripColor());
    setTitleHeight(40);
    setControlColors(Ui::neutral(400), hoverColor());

    m_tabs->setColors(t.term, Ui::neutral(200), Ui::neutral(400), hoverColor());
    m_tabs->setGlyphColor(Ui::sage(300));
    m_tabs->setMaxTabWidth(300);
    m_tabs->setShowNewMenu(true);
    titleLayout()->setContentsMargins(14, 0, 8, 0);
    titleLayout()->addWidget(m_tabs, 1);

    auto *body = new QWidget;
    auto *bodyLayout = new QHBoxLayout(body);
    // 18px top/bottom and 26px sides, with the terminal's own 8px margin inside.
    bodyLayout->setContentsMargins(18, 10, 14, 18);
    bodyLayout->addWidget(m_stack);
    setCentralWidget(body);

    connect(m_tabs, &Ui::TitleTabs::currentChanged, this, [this](int index) {
        if (index >= 0 && index < m_stack->count()) {
            m_stack->setCurrentIndex(index);
            m_stack->currentWidget()->setFocus();
        }
    });
    connect(m_tabs, &Ui::TitleTabs::closeRequested, this, &TerminalWindow::closeTab);
    connect(m_tabs, &Ui::TitleTabs::newTabRequested, this, [this] {
        newTab(current() ? current()->workingDirectory() : QDir::homePath());
    });
    connect(m_tabs, &Ui::TitleTabs::newMenuRequested, this, &TerminalWindow::showNewMenu);

    auto shortcut = [this](const QKeySequence &keys, auto slot) {
        auto *s = new QShortcut(keys, this);
        s->setContext(Qt::WindowShortcut);
        connect(s, &QShortcut::activated, this, slot);
    };
    shortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")), [this] {
        newTab(current() ? current()->workingDirectory() : QDir::homePath());
    });
    shortcut(QKeySequence(QStringLiteral("Ctrl+Shift+W")), [this] { closeTab(m_tabs->currentIndex()); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")), [this] { if (current()) current()->copyClipboard(); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")), [this] { if (current()) current()->pasteClipboard(); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")), [this] { if (current()) current()->toggleShowSearchBar(); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+PgDown")), [this] {
        if (m_tabs->count())
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
    });
    shortcut(QKeySequence(QStringLiteral("Ctrl+PgUp")), [this] {
        if (m_tabs->count())
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + m_tabs->count() - 1) % m_tabs->count());
    });
    shortcut(QKeySequence(QStringLiteral("Ctrl+=")), [this] { zoom(1); });
    shortcut(QKeySequence(QStringLiteral("Ctrl++")), [this] { zoom(1); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+-")), [this] { zoom(-1); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+0")), [this] { zoom(14 - m_fontPixels); });

    resizeToFit({1180, 820});
}

QTermWidget *TerminalWindow::current() const
{
    return qobject_cast<QTermWidget *>(m_stack->currentWidget());
}

QString TerminalWindow::colorSchemePath()
{
    // Generated from the design tokens: --k-term ground, neutral-200 text.
    // ANSI green and blue carry Debian's prompt (user@host, path) in the
    // design's sage-400 and accent-400.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/Karyo.colorscheme");
    const Ui::Theme &t = Ui::Theme::current();
    struct Ansi { QColor normal; QColor intense; };
    const Ansi ansi[8] = {
        {Ui::neutral(800), Ui::neutral(600)},          // black
        {QColor(0xe0, 0x6c, 0x55), QColor(0xf0, 0x8a, 0x72)}, // red
        {Ui::sage(400), Ui::sage(300)},                 // green
        {QColor(0xe6, 0xc0, 0x7b), QColor(0xf2, 0xd3, 0x96)}, // yellow
        {Ui::accent(400), Ui::accent(300)},             // blue: directories, paths
        {QColor(0xd9, 0xa0, 0xb6), QColor(0xe8, 0xb8, 0xca)}, // magenta
        {QColor(0x9c, 0xc4, 0xb2), QColor(0xb8, 0xd8, 0xc9)}, // cyan
        {Ui::neutral(200), Ui::neutral(100)},           // white
    };
    QString text;
    QTextStream out(&text);
    auto section = [&out](const QString &name, const QColor &color) {
        out << '[' << name << "]\nColor=" << rgb(color) << "\n\n";
    };
    section(QStringLiteral("Background"), t.term);
    section(QStringLiteral("BackgroundIntense"), t.term);
    section(QStringLiteral("BackgroundFaint"), t.term);
    section(QStringLiteral("Foreground"), Ui::neutral(200));
    section(QStringLiteral("ForegroundIntense"), Ui::neutral(100));
    section(QStringLiteral("ForegroundFaint"), Ui::neutral(500));
    for (int i = 0; i < 8; ++i) {
        section(QStringLiteral("Color%1").arg(i), ansi[i].normal);
        section(QStringLiteral("Color%1Intense").arg(i), ansi[i].intense);
        section(QStringLiteral("Color%1Faint").arg(i), Ui::mix(ansi[i].normal, t.term, 60));
    }
    out << "[General]\nDescription=Karyo\nOpacity=1\n";
    out.flush();
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(text.toUtf8());
    return path;
}

void TerminalWindow::newTab(const QString &directory, const QString &program,
                            const QStringList &args, const QString &glyph, const QString &title)
{
    const Ui::Theme &t = Ui::Theme::current();
    auto *term = new QTermWidget(0, m_stack);
    term->setColorScheme(colorSchemePath());
    QFont font = Ui::monoFont(m_fontPixels);
    term->setTerminalFont(font);
    term->setMargin(8);
    term->setScrollBarPosition(QTermWidget::ScrollBarRight);
    term->setKeyboardCursorShape(QTermWidget::KeyboardCursorShape::BlockCursor);
    term->setBlinkingCursor(true);
    term->setBoldIntense(false);
    term->setHistorySize(5000);
    term->setTerminalSizeHint(false);
    term->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(term, &QTermWidget::urlActivated, term, [](const QUrl &url, bool) {
        if (!(QGuiApplication::keyboardModifiers() & Qt::ControlModifier))
            return;
        if (url.isValid() && (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http")))
            QDesktopServices::openUrl(url);
    });
    term->setWorkingDirectory(directory.isEmpty() ? QDir::homePath() : directory);
    // Debian's .bashrc colors the prompt only for *-256color terminals.
    QStringList env = QProcessEnvironment::systemEnvironment().toStringList();
    env.removeIf([](const QString &var) {
        return var.startsWith(QLatin1String("TERM=")) || var.startsWith(QLatin1String("COLORTERM="));
    });
    env.append(QStringLiteral("TERM=xterm-256color"));
    env.append(QStringLiteral("COLORTERM=truecolor"));
    term->setEnvironment(env);
    if (!program.isEmpty()) {
        term->setShellProgram(program);
        term->setArgs(args);
    }
    // Light scrollbar handle on the dark ground, with no filled groove.
    QPalette palette = term->palette();
    palette.setColor(QPalette::WindowText, Ui::neutral(200));
    palette.setColor(QPalette::Window, t.term);
    palette.setColor(QPalette::Base, t.term);
    term->setPalette(palette);
    for (QScrollBar *bar : term->findChildren<QScrollBar *>()) {
        bar->setAutoFillBackground(false);
        bar->setPalette(palette);
    }

    const int index = m_tabs->addTab(glyph, title.isEmpty() ? tr("Terminal") : title);
    m_stack->addWidget(term);
    m_tabs->setCurrentIndex(index);
    m_stack->setCurrentWidget(term);

    if (title.isEmpty()) {
        connect(term, &QTermWidget::titleChanged, this, [this, term] {
            const int i = m_stack->indexOf(term);
            if (i >= 0 && !term->title().isEmpty())
                m_tabs->setTabTitle(i, term->title());
            if (term == current())
                setWindowTitle(term->title());
        });
    }
    connect(term, &QTermWidget::finished, this, [this, term] {
        const int i = m_stack->indexOf(term);
        if (i >= 0)
            closeTab(i);
    });
    // The text area is qtermwidget's TerminalDisplay; its mouse events carry the selection.
    for (QWidget *child : term->findChildren<QWidget *>()) {
        if (QByteArray(child->metaObject()->className()).endsWith("TerminalDisplay"))
            child->installEventFilter(new CopyOnSelect(term));
    }
    // A right click pastes, as in PuTTY; Shift+right click opens the menu
    // (and is the right click programs that use the mouse leave to the terminal).
    connect(term, &QWidget::customContextMenuRequested, this, [this, term](const QPoint &pos) {
        if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)
            showContextMenu(term, pos);
        else
            term->pasteClipboard();
    });
    term->startShellProgram();
    term->setFocus();
}

void TerminalWindow::closeTab(int index)
{
    QWidget *widget = m_stack->widget(index);
    if (!widget)
        return;
    m_stack->removeWidget(widget);
    widget->deleteLater();
    m_tabs->removeTab(index);
    if (m_stack->count() == 0)
        close();
    else
        m_stack->setCurrentIndex(m_tabs->currentIndex());
}

QStringList TerminalWindow::sshHosts()
{
    QStringList hosts;
    QFile file(QDir::homePath() + QStringLiteral("/.ssh/config"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return hosts;
    static const QRegularExpression hostLine(QStringLiteral("^\\s*Host\\s+(.+)$"),
                                             QRegularExpression::CaseInsensitiveOption);
    QTextStream in(&file);
    while (!in.atEnd()) {
        const auto match = hostLine.match(in.readLine());
        if (!match.hasMatch())
            continue;
        const QStringList names = match.captured(1).split(QRegularExpression(QStringLiteral("\\s+")),
                                                          Qt::SkipEmptyParts);
        for (const QString &name : names) {
            if (!name.contains(QLatin1Char('*')) && !name.contains(QLatin1Char('?'))
                && !name.startsWith(QLatin1Char('!')) && !hosts.contains(name))
                hosts.append(name);
        }
    }
    return hosts;
}

void TerminalWindow::showNewMenu(const QPoint &globalPos)
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    menu->addAction(tr("New tab"), this, [this] {
        newTab(current() ? current()->workingDirectory() : QDir::homePath());
    });
    menu->addAction(tr("New window"), this, [] {
        auto *window = new TerminalWindow;
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->newTab(QDir::homePath());
        window->show();
    });
    const QStringList hosts = sshHosts();
    if (!hosts.isEmpty()) {
        menu->addSeparator();
        for (const QString &host : hosts) {
            menu->addAction(tr("SSH: %1").arg(host), this, [this, host] {
                newTab(QDir::homePath(), QStringLiteral("ssh"), {host}, QStringLiteral("server"), host);
            });
        }
    }
    menu->popup(globalPos);
}

void TerminalWindow::showContextMenu(QTermWidget *term, const QPoint &pos)
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    QAction *copy = menu->addAction(tr("Copy"), term, &QTermWidget::copyClipboard);
    copy->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    copy->setEnabled(!term->selectedText().isEmpty());
    menu->addAction(tr("Paste"), term, &QTermWidget::pasteClipboard)
        ->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    menu->addSeparator();
    menu->addAction(tr("Find…"), term, &QTermWidget::toggleShowSearchBar)
        ->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    menu->addAction(tr("Clear"), term, &QTermWidget::clear);
    menu->addSeparator();
    menu->addAction(tr("New tab"), this, [this, term] { newTab(term->workingDirectory()); })
        ->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")));
    menu->popup(term->mapToGlobal(pos));
}

void TerminalWindow::zoom(int steps)
{
    m_fontPixels = qBound(9, m_fontPixels + steps, 32);
    for (int i = 0; i < m_stack->count(); ++i) {
        if (auto *term = qobject_cast<QTermWidget *>(m_stack->widget(i)))
            term->setTerminalFont(Ui::monoFont(m_fontPixels));
    }
}
