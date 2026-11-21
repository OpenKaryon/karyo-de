// SPDX-License-Identifier: GPL-3.0-or-later
#include "editorwindow.h"

#include "buffer.h"
#include "codeeditor.h"
#include "highlighter.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/menu.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QAbstractButton>
#include <QCloseEvent>
#include <QDir>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <functional>

namespace Ui = Karyo::Ui;

// ---------------------------------------------------------------- pieces

namespace {

QSettings editorSettings()
{
    return QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                     QStringLiteral("editor"));
}

QString terminalProgram()
{
    const QString configured = qEnvironmentVariable("TERMINAL");
    if (!configured.isEmpty() && !QStandardPaths::findExecutable(configured).isEmpty())
        return configured;
    for (const auto *name : {"karyo-terminal", "x-terminal-emulator"}) {
        const QString program = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (!program.isEmpty())
            return program;
    }
    return {};
}

QString shellQuote(const QString &text)
{
    QString quoted = text;
    quoted.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

// Status row text that opens a menu: "Spaces: 4", "LF", "Snakemake ▾".
class StatusButton : public QAbstractButton
{
public:
    StatusButton(bool chevron, QWidget *parent)
        : QAbstractButton(parent)
        , m_chevron(chevron)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
    }

    QSize sizeHint() const override
    {
        return {QFontMetrics(Ui::bodyFont(12.5)).horizontalAdvance(text()) + 20 + (m_chevron ? 19 : 0), 28};
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (underMouse() || isDown())
            Ui::fillPill(p, rect(), isDown() ? t.press : t.hover);
        p.setFont(Ui::bodyFont(12.5));
        p.setPen(t.muted);
        const int textWidth = QFontMetrics(p.font()).horizontalAdvance(text());
        p.drawText(QRect(10, 0, textWidth + 2, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
        if (m_chevron)
            p.drawPixmap(10 + textWidth + 6, (height() - 13) / 2,
                         Ui::icon(QStringLiteral("chevron-down"), 13, t.muted, devicePixelRatioF()));
    }

private:
    bool m_chevron;
};

// The sage "env: rnaseq" tag of the status row.
class EnvironmentTag : public QLabel
{
public:
    using QLabel::QLabel;

    QSize sizeHint() const override
    {
        return {QFontMetrics(Ui::bodyFont(12, QFont::DemiBold)).horizontalAdvance(text()) + 20, 24};
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Ui::fillPill(p, rect(), t.tint2);
        p.setFont(Ui::bodyFont(12, QFont::DemiBold));
        p.setPen(t.ink2);
        p.drawText(rect(), Qt::AlignCenter, text());
    }
};

// A notice over the code: a restored session, a file changed on disk.
class Banner : public QWidget
{
public:
    explicit Banner(QWidget *parent)
        : QWidget(parent)
        , m_text(new QLabel(this))
        , m_primary(new Ui::PillButton(QString(), QString(), this))
        , m_secondary(new Ui::PillButton(QString(), QString(), this))
    {
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(18, 8, 8, 8);
        row->setSpacing(8);
        m_text->setFont(Ui::bodyFont(13.5));
        m_text->setWordWrap(true);
        QPalette palette = m_text->palette();
        palette.setColor(QPalette::WindowText, Ui::Theme::current().text);
        m_text->setPalette(palette);
        row->addWidget(m_text, 1);
        m_primary->setKind(Ui::PillButton::Kind::Primary);
        m_secondary->setKind(Ui::PillButton::Kind::Secondary);
        row->addWidget(m_secondary);
        row->addWidget(m_primary);
        connect(m_primary, &QAbstractButton::clicked, this, [this] {
            hide();
            if (m_onPrimary)
                m_onPrimary();
        });
        connect(m_secondary, &QAbstractButton::clicked, this, [this] {
            hide();
            if (m_onSecondary)
                m_onSecondary();
        });
        hide();
    }

    // An empty primary text shows the secondary button alone.
    void showNotice(const QString &text, const QString &primary, std::function<void()> onPrimary,
                    const QString &secondary, std::function<void()> onSecondary)
    {
        m_text->setText(text);
        m_primary->setText(primary);
        m_primary->setVisible(!primary.isEmpty());
        m_secondary->setText(secondary);
        m_onPrimary = std::move(onPrimary);
        m_onSecondary = std::move(onSecondary);
        show();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Ui::fillRounded(p, QRectF(rect()), 8, Ui::Theme::current().tint1);
    }

private:
    QLabel *m_text;
    Ui::PillButton *m_primary;
    Ui::PillButton *m_secondary;
    std::function<void()> m_onPrimary;
    std::function<void()> m_onSecondary;
};

// The code card: --k-pane with the window radius.
class Card : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Ui::fillRounded(p, QRectF(rect()), 8, Ui::Theme::current().pane);
    }
};

} // namespace

class EditorPage : public QWidget
{
public:
    explicit EditorPage(Buffer *buffer, QWidget *parent = nullptr)
        : QWidget(parent)
        , buffer(buffer)
        , editor(new CodeEditor(buffer))
        , banner(new Banner(this))
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(10);
        v->addWidget(banner);
        auto *card = new Card(this);
        auto *inside = new QVBoxLayout(card);
        // Room for the rounded corners around the scroll bars.
        inside->setContentsMargins(0, 6, 8, 6);
        inside->addWidget(editor);
        v->addWidget(card, 1);
    }

    // A new tab nobody typed in: opening a file replaces it.
    bool isPristine() const { return buffer->path().isEmpty() && buffer->text().isEmpty() && !buffer->canUndo(); }

    Buffer *buffer;
    CodeEditor *editor;
    Banner *banner;
    bool languageChosen = false;
};

// ---------------------------------------------------------------- window

QPair<QString, QString> EditorWindow::runner(Language language) const
{
    switch (language) {
    case Language::Snakemake:
        return {tr("Run with Snakemake"), QStringLiteral("snakemake --cores all --snakefile %1")};
    case Language::Nextflow:
        return {tr("Run with Nextflow"), QStringLiteral("nextflow run %1")};
    case Language::Python:
        return {tr("Run with Python"), QStringLiteral("python3 %1")};
    case Language::R:
        return {tr("Run with R"), QStringLiteral("Rscript %1")};
    case Language::Shell:
        return {tr("Run in Terminal"), QStringLiteral("bash %1")};
    default:
        return {};
    }
}

EditorWindow::EditorWindow(QWidget *parent)
    : KaryoWindow(parent)
    , m_tabs(new Ui::TitleTabs)
    , m_stack(new QStackedWidget)
    , m_cacheTimer(new QTimer(this))
{
    const Ui::Theme &t = Ui::Theme::current();
    setWindowTitle(tr("Text Editor"));
    setBodyColor(t.win);
    setTitleColor(t.pane);
    setTitleHeight(40);
    m_tabs->setColors(t.win, t.text, t.muted, t.hover);
    m_tabs->setGlyphColor(t.ink2);
    m_tabs->setMaxTabWidth(200);
    titleLayout()->setContentsMargins(14, 0, 10, 0);
    titleLayout()->addWidget(m_tabs, 1);

    QSettings settings = editorSettings();
    m_wrap = settings.value(QStringLiteral("wrap"), false).toBool();
    m_fontPixels = settings.value(QStringLiteral("fontPixels"), 14.5).toDouble();

    auto *body = new QWidget;
    auto *v = new QVBoxLayout(body);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // Menu row: File Edit View Search Tools, then search and Run.
    auto *menuRow = new QHBoxLayout;
    menuRow->setContentsMargins(14, 8, 14, 8);
    menuRow->setSpacing(2);
    buildMenus();
    for (const auto &menu : std::as_const(m_menus))
        menuRow->addWidget(menu.first);
    menuRow->addStretch(1);
    auto *search = new Ui::IconButton(QStringLiteral("search"), body, {38, 38}, 17);
    search->setHint(tr("Find (Ctrl+F)"));
    connect(search, &QAbstractButton::clicked, this, [this] { showFind(false); });
    menuRow->addWidget(search);
    menuRow->addSpacing(6);
    m_run = new Ui::PillButton(QStringLiteral("play"), QString(), body);
    m_run->setKind(Ui::PillButton::Kind::Secondary);
    connect(m_run, &QAbstractButton::clicked, this, &EditorWindow::run);
    menuRow->addWidget(m_run);
    v->addLayout(menuRow);

    // Find and replace, under the menu row.
    m_findBar = new QWidget(body);
    auto *find = new QHBoxLayout(m_findBar);
    find->setContentsMargins(14, 0, 14, 10);
    find->setSpacing(6);
    m_find = new Ui::SearchField(tr("Find"), m_findBar);
    m_find->setFixedWidth(300);
    m_replace = new Ui::SearchField(tr("Replace with"), m_findBar);
    m_replace->setFixedWidth(260);
    find->addWidget(m_find);
    find->addWidget(m_replace);
    auto *previous = new Ui::IconButton(QStringLiteral("chevron-up"), m_findBar);
    previous->setHint(tr("Previous (Shift+F3)"));
    connect(previous, &QAbstractButton::clicked, this, [this] { findNext(true); });
    auto *next = new Ui::IconButton(QStringLiteral("chevron-down"), m_findBar);
    next->setHint(tr("Next (F3)"));
    connect(next, &QAbstractButton::clicked, this, [this] { findNext(false); });
    m_matchCase = new Ui::IconButton(QStringLiteral("type"), m_findBar);
    m_matchCase->setHint(tr("Match case"));
    m_matchCase->setCheckable(true);
    connect(m_matchCase, &QAbstractButton::toggled, this, [this](bool on) {
        m_matchCase->setActive(on);
        countMatches();
    });
    find->addWidget(previous);
    find->addWidget(next);
    find->addWidget(m_matchCase);
    m_replaceButtons = new QWidget(m_findBar);
    auto *replaceRow = new QHBoxLayout(m_replaceButtons);
    replaceRow->setContentsMargins(4, 0, 0, 0);
    replaceRow->setSpacing(6);
    auto *replaceOne = new Ui::PillButton(QString(), tr("Replace"), m_replaceButtons);
    replaceOne->setKind(Ui::PillButton::Kind::Secondary);
    connect(replaceOne, &QAbstractButton::clicked, this, &EditorWindow::replaceOne);
    auto *replaceEvery = new Ui::PillButton(QString(), tr("Replace all"), m_replaceButtons);
    replaceEvery->setKind(Ui::PillButton::Kind::Secondary);
    connect(replaceEvery, &QAbstractButton::clicked, this, &EditorWindow::replaceAll);
    replaceRow->addWidget(replaceOne);
    replaceRow->addWidget(replaceEvery);
    find->addWidget(m_replaceButtons);
    m_findStatus = new QLabel(m_findBar);
    m_findStatus->setFont(Ui::bodyFont(12.5));
    QPalette muted = m_findStatus->palette();
    muted.setColor(QPalette::WindowText, t.muted);
    m_findStatus->setPalette(muted);
    find->addSpacing(8);
    find->addWidget(m_findStatus);
    find->addStretch(1);
    auto *closeFind = new Ui::IconButton(QStringLiteral("x"), m_findBar);
    closeFind->setHint(tr("Close (Esc)"));
    find->addWidget(closeFind);
    auto hideFind = [this] {
        m_findBar->hide();
        if (current())
            current()->editor->setFocus();
    };
    connect(closeFind, &QAbstractButton::clicked, this, hideFind);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), m_findBar);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, hideFind);
    connect(m_find->lineEdit(), &QLineEdit::returnPressed, this, [this] {
        findNext(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier);
    });
    connect(m_find->lineEdit(), &QLineEdit::textChanged, this, &EditorWindow::countMatches);
    connect(m_replace->lineEdit(), &QLineEdit::returnPressed, this, &EditorWindow::replaceOne);
    m_findBar->hide();
    v->addWidget(m_findBar);

    auto *stackRow = new QHBoxLayout;
    stackRow->setContentsMargins(14, 0, 14, 0);
    stackRow->addWidget(m_stack);
    v->addLayout(stackRow, 1);

    // Status row: position, indentation, encoding, line ends; environment and language.
    auto *status = new QWidget(body);
    status->setFixedHeight(40);
    auto *s = new QHBoxLayout(status);
    s->setContentsMargins(16, 0, 18, 0);
    s->setSpacing(4);
    m_position = new QLabel(status);
    m_position->setFont(Ui::bodyFont(12.5));
    m_position->setPalette(muted);
    s->addSpacing(10);
    s->addWidget(m_position);
    s->addSpacing(8);
    m_indent = new StatusButton(false, status);
    connect(m_indent, &QAbstractButton::clicked, this, [this] {
        EditorPage *page = current();
        if (!page)
            return;
        auto *menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        Ui::styleMenu(menu);
        for (const int width : {2, 4, 8})
            menu->addAction(tr("Spaces: %1").arg(width), this, [this, page, width] {
                page->editor->setIndentation(false, width);
                updateStatus();
            });
        menu->addAction(tr("Tabs"), this, [this, page] {
            page->editor->setIndentation(true, page->editor->indentWidth());
            updateStatus();
        });
        menu->popup(m_indent->mapToGlobal(QPoint(0, -menu->sizeHint().height() - 6)));
    });
    s->addWidget(m_indent);
    m_encoding = new QLabel(status);
    m_encoding->setFont(Ui::bodyFont(12.5));
    m_encoding->setPalette(muted);
    s->addSpacing(8);
    s->addWidget(m_encoding);
    s->addSpacing(8);
    m_lineEnding = new StatusButton(false, status);
    connect(m_lineEnding, &QAbstractButton::clicked, this, [this] {
        EditorPage *page = current();
        if (!page)
            return;
        auto *menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        Ui::styleMenu(menu);
        menu->addAction(tr("LF (Linux, macOS)"), this, [this, page] {
            page->buffer->setCrlf(false);
            updateStatus();
        });
        menu->addAction(tr("CRLF (Windows)"), this, [this, page] {
            page->buffer->setCrlf(true);
            updateStatus();
        });
        menu->popup(m_lineEnding->mapToGlobal(QPoint(0, -menu->sizeHint().height() - 6)));
    });
    s->addWidget(m_lineEnding);
    s->addStretch(1);
    // The conda environment the editor was started in.
    const QString environment = qEnvironmentVariable("CONDA_DEFAULT_ENV");
    m_environment = new EnvironmentTag(tr("env: %1").arg(environment), status);
    m_environment->setVisible(!environment.isEmpty());
    s->addWidget(m_environment);
    s->addSpacing(8);
    m_language = new StatusButton(true, status);
    connect(m_language, &QAbstractButton::clicked, this, [this] {
        EditorPage *page = current();
        if (!page)
            return;
        auto *menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        Ui::styleMenu(menu);
        for (const Language language : Languages::all())
            menu->addAction(Languages::name(language), this, [this, page, language] {
                page->languageChosen = true;
                setLanguage(page, language);
            });
        const QSize size = menu->sizeHint();
        menu->popup(m_language->mapToGlobal(QPoint(m_language->width() - size.width(), -size.height() - 6)));
    });
    s->addWidget(m_language);
    v->addWidget(status);
    setCentralWidget(body);

    connect(m_tabs, &Ui::TitleTabs::currentChanged, this, &EditorWindow::showPage);
    connect(m_tabs, &Ui::TitleTabs::closeRequested, this, [this](int index) { closeTab(index); });
    connect(m_tabs, &Ui::TitleTabs::newTabRequested, this, &EditorWindow::newUntitled);

    m_cacheTimer->setSingleShot(true);
    m_cacheTimer->setInterval(1000);
    connect(m_cacheTimer, &QTimer::timeout, this, [this] {
        writeCaches();
        writeSession(false);
    });
    resizeToFit({1360, 860});
}

QMenu *EditorWindow::addMenu(const QString &title)
{
    auto *button = new Ui::PillButton(QString(), title);
    auto *menu = new QMenu(this);
    Ui::styleMenu(menu);
    connect(button, &QAbstractButton::clicked, this, [button, menu] {
        menu->popup(button->mapToGlobal(QPoint(0, button->height() + 4)));
    });
    m_menus.append({button, menu});
    return menu;
}

void EditorWindow::buildMenus()
{
    // Shortcuts work with the menus closed because the window holds the actions.
    auto add = [this](QMenu *menu, const QString &text, const QKeySequence &keys, auto slot) {
        QAction *action = menu->addAction(text);
        action->setShortcut(keys);
        action->setShortcutContext(Qt::WindowShortcut);
        connect(action, &QAction::triggered, this, slot);
        addAction(action);
        return action;
    };
    auto editor = [this] { return current() ? current()->editor : nullptr; };

    QMenu *file = addMenu(tr("File"));
    add(file, tr("New tab"), QKeySequence::New, [this] { newUntitled(); });
    add(file, tr("Open…"), QKeySequence::Open, [this] {
        const QString dir = current() && !current()->buffer->path().isEmpty()
                                ? QFileInfo(current()->buffer->path()).absolutePath()
                                : QDir::homePath();
        openFiles(QFileDialog::getOpenFileNames(this, tr("Open"), dir));
    });
    file->addSeparator();
    add(file, tr("Save"), QKeySequence::Save, [this] { save(current()); });
    add(file, tr("Save as…"), QKeySequence(QStringLiteral("Ctrl+Shift+S")), [this] { saveAs(current()); });
    add(file, tr("Save all"), QKeySequence(QStringLiteral("Ctrl+Alt+S")), [this] { saveAll(); });
    add(file, tr("Revert to saved"), QKeySequence(), [this] { revert(current()); });
    file->addSeparator();
    add(file, tr("Close tab"), QKeySequence(QStringLiteral("Ctrl+W")), [this] { closeTab(m_tabs->currentIndex()); });
    add(file, tr("Quit"), QKeySequence(QStringLiteral("Ctrl+Q")), [this] { close(); });

    QMenu *edit = addMenu(tr("Edit"));
    add(edit, tr("Undo"), QKeySequence::Undo, [editor] { if (auto *e = editor()) e->undo(); });
    add(edit, tr("Redo"), QKeySequence(QStringLiteral("Ctrl+Shift+Z")), [editor] { if (auto *e = editor()) e->redo(); });
    edit->addSeparator();
    add(edit, tr("Cut"), QKeySequence::Cut, [editor] { if (auto *e = editor()) e->cut(); });
    add(edit, tr("Copy"), QKeySequence::Copy, [editor] { if (auto *e = editor()) e->copy(); });
    add(edit, tr("Paste"), QKeySequence::Paste, [editor] { if (auto *e = editor()) e->paste(); });
    add(edit, tr("Select all"), QKeySequence::SelectAll, [editor] { if (auto *e = editor()) e->selectAll(); });
    edit->addSeparator();
    add(edit, tr("Duplicate line"), QKeySequence(QStringLiteral("Ctrl+D")), [editor] { if (auto *e = editor()) e->duplicateLine(); });
    add(edit, tr("Delete line"), QKeySequence(QStringLiteral("Ctrl+Shift+K")), [editor] { if (auto *e = editor()) e->deleteLine(); });
    add(edit, tr("Toggle comment"), QKeySequence(QStringLiteral("Ctrl+/")), [editor] { if (auto *e = editor()) e->toggleComment(); });

    QMenu *view = addMenu(tr("View"));
    QAction *wrap = add(view, tr("Word wrap"), QKeySequence(QStringLiteral("Alt+Z")), [this] {
        m_wrap = !m_wrap;
        editorSettings().setValue(QStringLiteral("wrap"), m_wrap);
        for (EditorPage *page : std::as_const(m_pages))
            page->editor->setLineWrapMode(m_wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    });
    wrap->setCheckable(true);
    wrap->setChecked(m_wrap);
    view->addSeparator();
    add(view, tr("Zoom in"), QKeySequence(QStringLiteral("Ctrl+=")), [this] { zoom(1); });
    add(view, tr("Zoom out"), QKeySequence(QStringLiteral("Ctrl+-")), [this] { zoom(-1); });
    add(view, tr("Actual size"), QKeySequence(QStringLiteral("Ctrl+0")), [this] { zoom(0); });

    QMenu *searchMenu = addMenu(tr("Search"));
    add(searchMenu, tr("Find…"), QKeySequence::Find, [this] { showFind(false); });
    add(searchMenu, tr("Replace…"), QKeySequence(QStringLiteral("Ctrl+H")), [this] { showFind(true); });
    add(searchMenu, tr("Find next"), QKeySequence(QStringLiteral("F3")), [this] { findNext(false); });
    add(searchMenu, tr("Find previous"), QKeySequence(QStringLiteral("Shift+F3")), [this] { findNext(true); });
    searchMenu->addSeparator();
    add(searchMenu, tr("Go to line…"), QKeySequence(QStringLiteral("Ctrl+G")), [this] { goToLine(); });

    QMenu *tools = addMenu(tr("Tools"));
    add(tools, tr("Run"), QKeySequence(QStringLiteral("F5")), [this] { run(); });
    add(tools, tr("Open Terminal here"), QKeySequence(), [this] {
        const QString dir = current() && !current()->buffer->path().isEmpty()
                                ? QFileInfo(current()->buffer->path()).absolutePath()
                                : QDir::homePath();
        if (!QProcess::startDetached(terminalProgram(), {}, dir))
            QMessageBox::warning(this, tr("Text Editor"), tr("The terminal could not be opened."));
    });
    add(tools, tr("Trim trailing spaces"), QKeySequence(), [this] {
        EditorPage *page = current();
        if (!page)
            return;
        page->buffer->beginGroup();
        QTextCursor cursor(page->editor->document());
        for (QTextBlock block = page->editor->document()->begin(); block.isValid(); block = block.next()) {
            const QString text = block.text();
            int end = int(text.size());
            while (end > 0 && text.at(end - 1).isSpace())
                --end;
            if (end < text.size()) {
                cursor.setPosition(block.position() + end);
                cursor.setPosition(block.position() + int(text.size()), QTextCursor::KeepAnchor);
                cursor.removeSelectedText();
            }
        }
        page->buffer->endGroup();
    });

    // Tabs: Ctrl+PgDown / Ctrl+PgUp like the terminal, Ctrl+Tab too.
    auto cycle = [this](int delta) {
        if (m_tabs->count())
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + delta + m_tabs->count()) % m_tabs->count());
    };
    for (const auto &[keys, delta] : {std::pair{QStringLiteral("Ctrl+PgDown"), 1}, std::pair{QStringLiteral("Ctrl+PgUp"), -1},
                                      std::pair{QStringLiteral("Ctrl+Tab"), 1}, std::pair{QStringLiteral("Ctrl+Shift+Tab"), -1}}) {
        auto *shortcut = new QShortcut(QKeySequence(keys), this);
        const int step = delta;
        connect(shortcut, &QShortcut::activated, this, [cycle, step] { cycle(step); });
    }
}

EditorPage *EditorWindow::current() const { return m_pages.value(m_tabs->currentIndex()); }

EditorPage *EditorWindow::pageFor(Buffer *buffer) const
{
    for (EditorPage *page : m_pages) {
        if (page->buffer == buffer)
            return page;
    }
    return nullptr;
}

int EditorWindow::addPage(Buffer *buffer, Language language, bool languageChosen)
{
    auto *page = new EditorPage(buffer);
    page->languageChosen = languageChosen;
    page->editor->setLanguage(language);
    page->editor->detectIndentation();
    page->editor->setFontPixels(m_fontPixels);
    page->editor->setLineWrapMode(m_wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    m_pages.append(page);
    m_stack->addWidget(page);
    const int index = m_tabs->addTab(Languages::glyph(language), buffer->title());
    m_tabs->setTabDirty(index, buffer->isModified());
    connect(buffer, &Buffer::modifiedChanged, this, [this, page] {
        updateTab(page);
        if (page == current())
            updateStatus();
    });
    connect(buffer, &Buffer::dirtied, this, [this, buffer] { scheduleCache(buffer); });
    connect(page->editor, &CodeEditor::positionChanged, this, [this, page] {
        if (page == current())
            updateStatus();
    });
    if (m_recoveryWarning) {
        m_recoveryWarning = false;
        page->banner->showNotice(
            tr("The previous session could not be restored. Recovery files were kept."),
            {}, {}, tr("Dismiss"), [] {});
    }
    return index;
}

void EditorWindow::showPage(int index)
{
    EditorPage *page = m_pages.value(index);
    if (!page)
        return;
    m_stack->setCurrentWidget(page);
    page->editor->setFocus();
    updateStatus();
    updateRun();
    checkDisk(page);
    m_cacheTimer->start();
}

void EditorWindow::newUntitled()
{
    int number = 1;
    for (bool taken = true; taken; ++number) {
        taken = false;
        for (EditorPage *page : std::as_const(m_pages))
            taken = taken || (page->buffer->path().isEmpty() && page->buffer->untitledNumber() == number);
        if (!taken)
            break;
    }
    auto *buffer = new Buffer(QUuid::createUuid().toString(QUuid::WithoutBraces), this);
    buffer->setUntitledNumber(number);
    const int index = addPage(buffer, Language::Plain, false);
    m_tabs->setCurrentIndex(index);
    showPage(index);
}

void EditorWindow::openFiles(const QStringList &paths)
{
    EditorPage *pristine = m_pages.size() == 1 && m_pages.first()->isPristine() ? m_pages.first() : nullptr;
    int last = -1;
    for (const QString &path : paths) {
        if (path.isEmpty())
            continue;
        const QString absolute = QFileInfo(path).absoluteFilePath();
        bool open = false;
        for (int i = 0; i < m_pages.size(); ++i) {
            if (m_pages.at(i)->buffer->path() == absolute) {
                last = i;
                open = true;
            }
        }
        if (open || QFileInfo(absolute).isDir())
            continue;
        auto *buffer = new Buffer(QUuid::createUuid().toString(QUuid::WithoutBraces), this);
        if (QFileInfo::exists(absolute)) {
            QString error;
            if (!buffer->load(absolute, &error)) {
                QMessageBox::warning(this, tr("Could not open"),
                                     tr("“%1” could not be opened: %2").arg(QFileInfo(absolute).fileName(), error));
                delete buffer;
                continue;
            }
        } else {
            buffer->setNewPath(absolute);
        }
        last = addPage(buffer, Languages::detect(absolute, buffer->document()->firstBlock().text()), false);
        scheduleCache(buffer);
    }
    if (last < 0)
        return;
    m_tabs->setCurrentIndex(last);
    showPage(last);
    if (pristine && m_pages.size() > 1)
        closeTab(int(m_pages.indexOf(pristine)));
}

bool EditorWindow::closeTab(int index)
{
    EditorPage *page = m_pages.value(index);
    if (!page)
        return false;
    Buffer *buffer = page->buffer;
    if (buffer->isModified() && !(buffer->path().isEmpty() && buffer->text().isEmpty())) {
        QMessageBox box(this);
        box.setWindowTitle(tr("Text Editor"));
        box.setText(tr("Save changes to “%1”?").arg(buffer->title()));
        box.setInformativeText(tr("Your changes will be lost if you close this tab without saving."));
        QPushButton *saveButton = box.addButton(tr("Save"), QMessageBox::AcceptRole);
        QPushButton *discard = box.addButton(tr("Don't save"), QMessageBox::DestructiveRole);
        box.addButton(tr("Cancel"), QMessageBox::RejectRole);
        box.setDefaultButton(saveButton);
        box.exec();
        if (box.clickedButton() == saveButton) {
            if (!save(page))
                return false;
        } else if (box.clickedButton() != discard) {
            return false;
        }
    }
    m_dirty.remove(buffer);
    QDir(cacheDir(buffer)).removeRecursively();
    // The list goes first: removeTab reports the new current tab at once.
    m_pages.removeAt(index);
    m_stack->removeWidget(page);
    m_tabs->removeTab(index);
    page->deleteLater();
    buffer->deleteLater();
    if (m_pages.isEmpty())
        newUntitled();
    writeSession(false);
    return true;
}

bool EditorWindow::prepareEncoding(EditorPage *page)
{
    if (!page->buffer->needsUtf8())
        return true;
    const auto choice = QMessageBox::question(this, tr("Save as UTF-8?"),
        tr("ISO-8859-1 cannot store all characters in this text. Save as UTF-8 to keep them?"),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
    if (choice != QMessageBox::Yes)
        return false;
    page->buffer->useUtf8();
    updateStatus();
    return true;
}

bool EditorWindow::save(EditorPage *page)
{
    if (!page)
        return false;
    if (page->buffer->path().isEmpty())
        return saveAs(page);
    if (!prepareEncoding(page))
        return false;
    QString error;
    if (!page->buffer->save(page->buffer->path(), &error)) {
        QMessageBox::warning(this, tr("Could not save"),
                             tr("“%1” could not be saved: %2").arg(page->buffer->title(), error));
        return false;
    }
    page->banner->hide();
    updateTab(page);
    updateStatus();
    return true;
}

bool EditorWindow::saveAs(EditorPage *page)
{
    if (!page)
        return false;
    const QString suggested = page->buffer->path().isEmpty()
                                  ? QDir::homePath() + QLatin1Char('/') + page->buffer->title()
                                  : page->buffer->path();
    const QString path = QFileDialog::getSaveFileName(this, tr("Save as"), suggested);
    if (path.isEmpty())
        return false;
    if (!prepareEncoding(page))
        return false;
    QString error;
    if (!page->buffer->save(path, &error)) {
        QMessageBox::warning(this, tr("Could not save"),
                             tr("“%1” could not be saved: %2").arg(QFileInfo(path).fileName(), error));
        return false;
    }
    if (!page->languageChosen)
        setLanguage(page, Languages::detect(path, page->editor->document()->firstBlock().text()));
    page->banner->hide();
    updateTab(page);
    updateStatus();
    updateRun();
    return true;
}

void EditorWindow::saveAll()
{
    for (EditorPage *page : std::as_const(m_pages)) {
        if (page->buffer->isModified())
            save(page);
    }
}

void EditorWindow::revert(EditorPage *page)
{
    if (!page || !page->buffer->existsOnDisk())
        return;
    QString error;
    // An undoable step: Ctrl+Z brings the unsaved text back.
    if (!page->buffer->load(page->buffer->path(), &error))
        QMessageBox::warning(this, tr("Could not open"), error);
    page->banner->hide();
    updateStatus();
}

void EditorWindow::checkDisk(EditorPage *page)
{
    if (!page)
        return;
    Buffer *buffer = page->buffer;
    if (buffer->deletedOnDisk() && !page->banner->isVisible()) {
        page->banner->showNotice(tr("“%1” was deleted on disk. Saving creates it again.").arg(buffer->title()),
                                 tr("Save"), [this, page] { save(page); }, tr("Keep"), [] {});
        return;
    }
    if (!buffer->changedOnDisk())
        return;
    if (!buffer->isModified()) {
        // Nothing of ours to lose: follow the file.
        QString error;
        buffer->load(buffer->path(), &error);
        return;
    }
    page->banner->showNotice(tr("“%1” changed on disk.").arg(buffer->title()), tr("Reload"),
                             [this, page] { revert(page); }, tr("Keep mine"),
                             [buffer] { buffer->acceptDiskVersion(); });
}

void EditorWindow::setLanguage(EditorPage *page, Language language)
{
    page->editor->setLanguage(language);
    updateTab(page);
    updateStatus();
    updateRun();
    m_cacheTimer->start();
}

void EditorWindow::showFind(bool replace)
{
    m_findBar->show();
    m_replace->setVisible(replace);
    m_replaceButtons->setVisible(replace);
    if (EditorPage *page = current()) {
        const QString selected = page->editor->textCursor().selectedText();
        if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator))
            m_find->lineEdit()->setText(selected);
    }
    m_find->lineEdit()->setFocus();
    m_find->lineEdit()->selectAll();
    countMatches();
}

bool EditorWindow::findNext(bool backward)
{
    EditorPage *page = current();
    const QString text = m_find->lineEdit()->text();
    if (!page || text.isEmpty())
        return false;
    QTextDocument::FindFlags flags;
    if (backward)
        flags |= QTextDocument::FindBackward;
    if (m_matchCase->isChecked())
        flags |= QTextDocument::FindCaseSensitively;
    QTextDocument *document = page->editor->document();
    QTextCursor found = document->find(text, page->editor->textCursor(), flags);
    if (found.isNull()) {
        // Around the end, as Notepad++ does.
        QTextCursor from(document);
        if (backward)
            from.movePosition(QTextCursor::End);
        found = document->find(text, from, flags);
    }
    if (found.isNull()) {
        m_findStatus->setText(tr("No matches"));
        return false;
    }
    page->buffer->breakTyping();
    page->editor->setTextCursor(found);
    page->editor->ensureCursorVisible();
    return true;
}

void EditorWindow::countMatches()
{
    EditorPage *page = current();
    const QString text = m_find->lineEdit()->text();
    if (!page || text.isEmpty() || !m_findBar->isVisible()) {
        m_findStatus->clear();
        return;
    }
    QTextDocument::FindFlags flags;
    if (m_matchCase->isChecked())
        flags |= QTextDocument::FindCaseSensitively;
    int count = 0;
    QTextCursor cursor(page->editor->document());
    while (count < 9999) {
        cursor = page->editor->document()->find(text, cursor, flags);
        if (cursor.isNull())
            break;
        ++count;
    }
    m_findStatus->setText(count == 0   ? tr("No matches")
                          : count == 1 ? tr("1 match")
                                       : tr("%n matches", nullptr, count));
}

void EditorWindow::replaceOne()
{
    EditorPage *page = current();
    const QString text = m_find->lineEdit()->text();
    if (!page || text.isEmpty())
        return;
    QTextCursor cursor = page->editor->textCursor();
    const Qt::CaseSensitivity cs = m_matchCase->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (cursor.hasSelection() && cursor.selectedText().compare(text, cs) == 0) {
        page->buffer->breakTyping();
        cursor.insertText(m_replace->lineEdit()->text());
        page->buffer->breakTyping();
        page->editor->setTextCursor(cursor);
    }
    findNext(false);
    countMatches();
}

void EditorWindow::replaceAll()
{
    EditorPage *page = current();
    const QString text = m_find->lineEdit()->text();
    if (!page || text.isEmpty())
        return;
    QTextDocument::FindFlags flags;
    if (m_matchCase->isChecked())
        flags |= QTextDocument::FindCaseSensitively;
    const QString replacement = m_replace->lineEdit()->text();
    int count = 0;
    // One step: a single Ctrl+Z takes every replacement back.
    page->buffer->beginGroup();
    QTextCursor cursor(page->editor->document());
    while (true) {
        cursor = page->editor->document()->find(text, cursor, flags);
        if (cursor.isNull())
            break;
        cursor.insertText(replacement);
        ++count;
    }
    page->buffer->endGroup();
    m_findStatus->setText(tr("Replaced %n", nullptr, count));
}

void EditorWindow::goToLine()
{
    EditorPage *page = current();
    if (!page)
        return;
    bool ok = false;
    const int lines = page->editor->document()->blockCount();
    const int line = QInputDialog::getInt(this, tr("Go to line"), tr("Line (1–%1)").arg(lines),
                                          page->editor->textCursor().blockNumber() + 1, 1, lines, 1, &ok);
    if (!ok)
        return;
    QTextCursor cursor(page->editor->document()->findBlockByNumber(line - 1));
    page->buffer->breakTyping();
    page->editor->setTextCursor(cursor);
    page->editor->centerCursor();
    page->editor->setFocus();
}

void EditorWindow::run()
{
    EditorPage *page = current();
    if (!page)
        return;
    const QString command = runner(page->editor->language()).second;
    if (command.isEmpty())
        return;
    // The tools read the file, not the editor: save first.
    if ((page->buffer->isModified() || page->buffer->path().isEmpty()) && !save(page))
        return;
    const QFileInfo file(page->buffer->path());
    if (!QProcess::startDetached(terminalProgram(),
            {QStringLiteral("-e"), QStringLiteral("sh"), QStringLiteral("-c"),
             command.arg(shellQuote(file.fileName())) + QStringLiteral("; exec \"${SHELL:-/bin/bash}\"")},
            file.absolutePath()))
        QMessageBox::warning(this, tr("Text Editor"), tr("The terminal could not be opened."));
}

void EditorWindow::zoom(int steps)
{
    m_fontPixels = steps == 0 ? 14.5 : qBound(9.0, m_fontPixels + steps, 32.0);
    editorSettings().setValue(QStringLiteral("fontPixels"), m_fontPixels);
    for (EditorPage *page : std::as_const(m_pages))
        page->editor->setFontPixels(m_fontPixels);
}

void EditorWindow::updateTab(EditorPage *page)
{
    const int index = int(m_pages.indexOf(page));
    if (index < 0)
        return;
    m_tabs->setTabTitle(index, page->buffer->title());
    m_tabs->setTabGlyph(index, Languages::glyph(page->editor->language()));
    m_tabs->setTabDirty(index, page->buffer->isModified());
    if (page == current())
        setWindowTitle(page->buffer->title() + QStringLiteral(" — ") + tr("Text Editor"));
}

void EditorWindow::updateStatus()
{
    EditorPage *page = current();
    if (!page)
        return;
    const QTextCursor cursor = page->editor->textCursor();
    m_position->setText(tr("Ln %1, Col %2").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1));
    m_indent->setText(page->editor->indentsWithTabs() ? tr("Tab size: %1").arg(page->editor->indentWidth())
                                                      : tr("Spaces: %1").arg(page->editor->indentWidth()));
    m_indent->updateGeometry();
    m_encoding->setText(page->buffer->encoding());
    m_lineEnding->setText(page->buffer->crlf() ? QStringLiteral("CRLF") : QStringLiteral("LF"));
    m_lineEnding->updateGeometry();
    m_language->setText(Languages::name(page->editor->language()));
    m_language->updateGeometry();
    setWindowTitle(page->buffer->title() + QStringLiteral(" — ") + tr("Text Editor"));
}

void EditorWindow::updateRun()
{
    EditorPage *page = current();
    const auto run = page ? runner(page->editor->language()) : QPair<QString, QString>();
    m_run->setText(run.first);
    m_run->setVisible(!run.second.isEmpty());
    m_run->updateGeometry();
}

// ---------------------------------------------------------------- session

QString EditorWindow::dataDir() const
{
    // Keep unsaved tabs and undo caches from the prototype despite the new app ID.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                        + QStringLiteral("/openkaryon/openkaryon-editor");
    QDir().mkpath(dir);
    return dir;
}

QString EditorWindow::cacheDir(Buffer *buffer) const
{
    return dataDir() + QStringLiteral("/buffers/") + buffer->id();
}

void EditorWindow::scheduleCache(Buffer *buffer)
{
    m_dirty.insert(buffer);
    m_cacheTimer->start();
}

void EditorWindow::writeCaches()
{
    for (Buffer *buffer : std::as_const(m_dirty))
        buffer->writeCache(cacheDir(buffer));
    m_dirty.clear();
}

void EditorWindow::writeSession(bool clean)
{
    QJsonArray tabs;
    for (EditorPage *page : std::as_const(m_pages)) {
        QJsonObject tab;
        tab.insert(QStringLiteral("id"), page->buffer->id());
        tab.insert(QStringLiteral("path"), page->buffer->path());
        tab.insert(QStringLiteral("untitled"), page->buffer->untitledNumber());
        if (page->languageChosen)
            tab.insert(QStringLiteral("language"), Languages::key(page->editor->language()));
        tab.insert(QStringLiteral("cursor"), page->editor->textCursor().position());
        tab.insert(QStringLiteral("line"), page->editor->verticalScrollBar()->value());
        tabs.append(tab);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    // false while the editor runs: a session left that way ended in a crash.
    root.insert(QStringLiteral("clean"), clean);
    root.insert(QStringLiteral("current"), m_tabs->currentIndex());
    root.insert(QStringLiteral("tabs"), tabs);
    QSaveFile file(dataDir() + QStringLiteral("/session.json"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.commit();
    }
}

bool EditorWindow::restoreSession()
{
    QFile file(dataDir() + QStringLiteral("/session.json"));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray bytes = file.readAll();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    const QJsonObject root = document.object();
    const auto preserveSession = [this, &file] {
        QFile::copy(file.fileName(), dataDir() + QStringLiteral("/session.invalid-%1.json")
                    .arg(QDateTime::currentMSecsSinceEpoch()));
        m_recoveryWarning = true;
    };
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || root.value(QStringLiteral("version")).toInt(-1) != 1
        || !root.value(QStringLiteral("tabs")).isArray()) {
        preserveSession();
        return false;
    }
    static const QRegularExpression validId(QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
    QSet<QString> ids;
    for (const auto &value : root.value(QStringLiteral("tabs")).toArray()) {
        const QString id = value.toObject().value(QStringLiteral("id")).toString();
        if (!validId.match(id).hasMatch() || ids.contains(id)) {
            preserveSession();
            return false;
        }
        ids.insert(id);
    }
    const bool crashed = !root.value(QStringLiteral("clean")).toBool(true);
    for (const QJsonValue &value : root.value(QStringLiteral("tabs")).toArray()) {
        const QJsonObject tab = value.toObject();
        const QString id = tab.value(QStringLiteral("id")).toString();
        const QString path = tab.value(QStringLiteral("path")).toString();
        if (id.isEmpty())
            continue;
        auto *buffer = new Buffer(id, this);
        buffer->setUntitledNumber(tab.value(QStringLiteral("untitled")).toInt());
        QString error;
        const QString dir = dataDir() + QStringLiteral("/buffers/") + id;
        if (!buffer->readCache(dir, path, &error)) {
            // A saved file that is gone, with nothing unsaved to show.
            delete buffer;
            continue;
        }
        const QString key = tab.value(QStringLiteral("language")).toString();
        const Language language = key.isEmpty()
                                      ? Languages::detect(path, buffer->document()->firstBlock().text())
                                      : Languages::fromKey(key);
        const int index = addPage(buffer, language, !key.isEmpty());
        EditorPage *page = m_pages.at(index);
        const int position = tab.value(QStringLiteral("cursor")).toInt();
        const int line = tab.value(QStringLiteral("line")).toInt();
        QTimer::singleShot(0, page, [page, position, line] {
            QTextCursor cursor = page->editor->textCursor();
            cursor.setPosition(qBound(0, position, page->editor->document()->characterCount() - 1));
            page->editor->setTextCursor(cursor);
            page->editor->verticalScrollBar()->setValue(line);
        });
        if (crashed && buffer->isModified()) {
            page->banner->showNotice(
                tr("The editor closed unexpectedly. Your unsaved changes to “%1” are back.").arg(buffer->title()),
                buffer->existsOnDisk() ? tr("Revert to saved") : QString(), [this, page] { revert(page); },
                tr("Keep"), [] {});
        }
    }
    // Unknown recovery caches are retained. Explicit tab close removes only its own cache.
    if (m_pages.isEmpty())
        return false;
    const int index = qBound(0, root.value(QStringLiteral("current")).toInt(), int(m_pages.size()) - 1);
    m_tabs->setCurrentIndex(index);
    showPage(index);
    return true;
}

void EditorWindow::closeEvent(QCloseEvent *event)
{
    // Nothing to ask: every tab, saved or not, waits for the next start.
    writeCaches();
    writeSession(true);
    event->accept();
}

void EditorWindow::changeEvent(QEvent *event)
{
    KaryoWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && isActiveWindow())
        checkDisk(current());
}

// ---------------------------------------------------------------- D-Bus

EditorService::EditorService(EditorWindow *window)
    : QObject(window)
    , m_window(window)
{
}

void EditorService::Open(const QStringList &paths)
{
    if (paths.isEmpty())
        m_window->newUntitled();
    else
        m_window->openFiles(paths);
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
}
