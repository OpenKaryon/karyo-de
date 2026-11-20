// SPDX-License-Identifier: GPL-3.0-or-later
#include "codeeditor.h"

#include "buffer.h"
#include "highlighter.h"

#include <karyo/ui/menu.h>
#include <karyo/ui/theme.h>

#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QTextBlock>
#include <QTimer>

namespace Ui = Karyo::Ui;

namespace {

constexpr int GutterWidth = 64;
constexpr int GutterPadding = 20;  // between the numbers and the text
constexpr int VerticalPadding = 16;

class Gutter : public QWidget
{
public:
    explicit Gutter(CodeEditor *editor)
        : QWidget(editor)
        , m_editor(editor)
    {
    }

protected:
    void paintEvent(QPaintEvent *event) override { m_editor->paintGutter(event); }

private:
    CodeEditor *m_editor;
};

int leadingWidth(const QString &text)
{
    int i = 0;
    while (i < text.size() && (text.at(i) == QLatin1Char(' ') || text.at(i) == QLatin1Char('\t')))
        ++i;
    return i;
}

} // namespace

CodeEditor::CodeEditor(Buffer *buffer, QWidget *parent)
    : QPlainTextEdit(parent)
    , m_buffer(buffer)
    , m_highlighter(new Highlighter(buffer->document()))
    , m_gutter(new Gutter(this))
{
    setDocument(buffer->document());
    setFrameShape(QFrame::NoFrame);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setAutoFillBackground(false);
    viewport()->setAutoFillBackground(false);
    setViewportMargins(GutterWidth, VerticalPadding, 0, VerticalPadding);
    // The accent caret is drawn in paintEvent; Qt's would take the text color.
    setCursorWidth(0);
    setFontPixels(14.5);

    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, int dy) {
        if (dy)
            m_gutter->scroll(0, dy);
        else
            m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        highlightCurrentLine();
        m_gutter->update();
        m_caretOn = true;
        const QTextCursor cursor = textCursor();
        Q_EMIT positionChanged(cursor.blockNumber() + 1, cursor.positionInBlock() + 1);
    });
    m_blink = new QTimer(this);
    m_blink->setInterval(530);
    connect(m_blink, &QTimer::timeout, this, [this] {
        m_caretOn = !m_caretOn;
        viewport()->update(cursorRect().adjusted(-2, 0, 2, 0));
    });
    updateColors();
}

Language CodeEditor::language() const { return m_highlighter->language(); }

void CodeEditor::setLanguage(Language language)
{
    m_highlighter->setLanguage(language);
    if (Languages::usesTabs(language))
        setIndentation(true, m_indentWidth);
}

void CodeEditor::setIndentation(bool tabs, int width)
{
    m_tabs = tabs;
    m_indentWidth = qBound(1, width, 8);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * m_indentWidth);
}

void CodeEditor::detectIndentation()
{
    int tabLines = 0;
    int spaceLines = 0;
    bool twoWide = false;
    int checked = 0;
    for (QTextBlock block = document()->begin(); block.isValid() && checked < 500; block = block.next(), ++checked) {
        const QString text = block.text();
        if (text.startsWith(QLatin1Char('\t'))) {
            ++tabLines;
        } else if (text.startsWith(QLatin1String("  "))) {
            ++spaceLines;
            int spaces = 0;
            while (spaces < text.size() && text.at(spaces) == QLatin1Char(' '))
                ++spaces;
            if (spaces % 4 != 0 && spaces % 2 == 0)
                twoWide = true;
        }
    }
    setIndentation(tabLines > spaceLines || Languages::usesTabs(language()), twoWide ? 2 : 4);
}

QString CodeEditor::indentUnit() const
{
    return m_tabs ? QStringLiteral("\t") : QString(m_indentWidth, QLatin1Char(' '));
}

void CodeEditor::setFontPixels(qreal pixels)
{
    setFont(Ui::monoFont(pixels));
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * m_indentWidth);
    m_gutter->update();
}

void CodeEditor::updateColors()
{
    const Ui::Theme &t = Ui::Theme::current();
    QPalette p = palette();
    p.setColor(QPalette::Base, Qt::transparent);
    p.setColor(QPalette::Text, t.text);
    p.setColor(QPalette::Highlight, t.sel);
    p.setColor(QPalette::HighlightedText, t.text);
    setPalette(p);
    viewport()->setPalette(p);
    m_highlighter->updateColors();
    highlightCurrentLine();
    m_gutter->update();
}

void CodeEditor::highlightCurrentLine()
{
    QTextEdit::ExtraSelection line;
    line.format.setBackground(Ui::Theme::current().hover);
    line.format.setProperty(QTextFormat::FullWidthSelection, true);
    line.cursor = textCursor();
    line.cursor.clearSelection();
    setExtraSelections({line});
}

int CodeEditor::gutterWidth() const { return GutterWidth; }

void CodeEditor::paintGutter(QPaintEvent *event)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(m_gutter);
    painter.setFont(font());
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    const int current = textCursor().blockNumber();
    const int lineHeight = fontMetrics().height();
    while (block.isValid() && top <= event->rect().bottom()) {
        const qreal height = blockBoundingRect(block).height();
        if (block.isVisible() && top + height >= event->rect().top()) {
            if (number == current)
                painter.fillRect(QRectF(0, top, m_gutter->width(), height), t.hover);
            painter.setPen(number == current ? t.text : t.muted);
            painter.drawText(QRectF(0, top, GutterWidth - GutterPadding, lineHeight),
                             Qt::AlignRight | Qt::AlignVCenter, QString::number(number + 1));
        }
        block = block.next();
        top += height;
        ++number;
    }
}

void CodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect r = contentsRect();
    m_gutter->setGeometry(r.left(), r.top() + VerticalPadding, GutterWidth, r.height() - 2 * VerticalPadding);
}

void CodeEditor::paintEvent(QPaintEvent *event)
{
    QPlainTextEdit::paintEvent(event);
    if (!hasFocus() || !m_caretOn)
        return;
    QPainter painter(viewport());
    const QRect r = cursorRect();
    painter.fillRect(QRect(r.left(), r.top(), 2, r.height()), Ui::Theme::current().accent);
}

void CodeEditor::focusInEvent(QFocusEvent *event)
{
    QPlainTextEdit::focusInEvent(event);
    m_caretOn = true;
    m_blink->start();
}

void CodeEditor::focusOutEvent(QFocusEvent *event)
{
    QPlainTextEdit::focusOutEvent(event);
    m_blink->stop();
    viewport()->update();
}

void CodeEditor::undo()
{
    const int position = m_buffer->undo();
    if (position < 0)
        return;
    QTextCursor cursor = textCursor();
    cursor.setPosition(qMin(position, document()->characterCount() - 1));
    setTextCursor(cursor);
    ensureCursorVisible();
}

void CodeEditor::redo()
{
    const int position = m_buffer->redo();
    if (position < 0)
        return;
    QTextCursor cursor = textCursor();
    cursor.setPosition(qMin(position, document()->characterCount() - 1));
    setTextCursor(cursor);
    ensureCursorVisible();
}

void CodeEditor::mousePressEvent(QMouseEvent *event)
{
    m_buffer->breakTyping();
    QPlainTextEdit::mousePressEvent(event);
}

void CodeEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Undo)) {
        undo();
        return;
    }
    if (event->matches(QKeySequence::Redo)
        || (event->key() == Qt::Key_Y && event->modifiers() == Qt::ControlModifier)) {
        redo();
        return;
    }
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        m_buffer->breakTyping();
        break;
    default:
        break;
    }
    m_caretOn = true;
    QTextCursor cursor = textCursor();
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;

    if (event->key() == Qt::Key_Tab && mods == Qt::NoModifier) {
        if (cursor.hasSelection()
            && document()->findBlock(cursor.selectionStart()) != document()->findBlock(cursor.selectionEnd())) {
            indentSelection(false);
        } else if (m_tabs) {
            cursor.insertText(QStringLiteral("\t"));
        } else {
            cursor.insertText(QString(m_indentWidth - cursor.positionInBlock() % m_indentWidth, QLatin1Char(' ')));
        }
        return;
    }
    if (event->key() == Qt::Key_Backtab) {
        indentSelection(true);
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !(mods & Qt::ControlModifier)) {
        // Keeps the line's indentation, one level deeper after "rule x:" or "{".
        const QString before = cursor.block().text().left(cursor.positionInBlock());
        QString indent = before.left(leadingWidth(before));
        const QString trimmed = before.trimmed();
        const Language lang = language();
        const bool colonBlocks = lang == Language::Snakemake || lang == Language::Python || lang == Language::Yaml;
        if ((colonBlocks && trimmed.endsWith(QLatin1Char(':'))) || trimmed.endsWith(QLatin1Char('{')))
            indent += indentUnit();
        // A newline, never Qt's line separator (Shift+Enter).
        cursor.insertText(QLatin1Char('\n') + indent);
        ensureCursorVisible();
        return;
    }
    if (event->key() == Qt::Key_Backspace && mods == Qt::NoModifier && !cursor.hasSelection() && !m_tabs) {
        // In leading spaces, Backspace goes back one indentation stop.
        const int column = cursor.positionInBlock();
        const QString before = cursor.block().text().left(column);
        if (column > 0 && before.trimmed().isEmpty() && !before.contains(QLatin1Char('\t'))) {
            const int remove = (column - 1) % m_indentWidth + 1;
            cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, remove);
            cursor.removeSelectedText();
            return;
        }
    }
    QPlainTextEdit::keyPressEvent(event);
}

void CodeEditor::indentSelection(bool outdent)
{
    QTextCursor cursor = textCursor();
    const QTextBlock first = document()->findBlock(cursor.selectionStart());
    int endPosition = cursor.selectionEnd();
    // A selection ending at a line start does not take that line.
    if (cursor.hasSelection() && endPosition > cursor.selectionStart()
        && document()->findBlock(endPosition).position() == endPosition)
        --endPosition;
    const QTextBlock last = document()->findBlock(endPosition);
    m_buffer->beginGroup();
    QTextCursor edit(document());
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        if (outdent) {
            const QString text = block.text();
            int remove = 0;
            if (text.startsWith(QLatin1Char('\t'))) {
                remove = 1;
            } else {
                while (remove < m_indentWidth && remove < text.size() && text.at(remove) == QLatin1Char(' '))
                    ++remove;
            }
            if (remove > 0) {
                edit.setPosition(block.position());
                edit.setPosition(block.position() + remove, QTextCursor::KeepAnchor);
                edit.removeSelectedText();
            }
        } else if (!block.text().isEmpty()) {
            edit.setPosition(block.position());
            edit.insertText(indentUnit());
        }
        if (block == last)
            break;
    }
    m_buffer->endGroup();
    QTextCursor selection(document());
    selection.setPosition(first.position());
    selection.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
    setTextCursor(selection);
}

void CodeEditor::toggleComment()
{
    const QString prefix = Languages::commentPrefix(language());
    if (prefix.isEmpty())
        return;
    QTextCursor cursor = textCursor();
    QTextBlock first = document()->findBlock(cursor.selectionStart());
    const QTextBlock last = document()->findBlock(qMax(cursor.selectionStart(),
                                                        cursor.selectionEnd() - (cursor.hasSelection() ? 1 : 0)));
    // Comment out unless every non-empty line is a comment already.
    bool commented = true;
    int indent = INT_MAX;
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        const QString text = block.text();
        if (!text.trimmed().isEmpty()) {
            commented = commented && text.trimmed().startsWith(prefix);
            indent = qMin(indent, leadingWidth(text));
        }
        if (block == last)
            break;
    }
    if (indent == INT_MAX)
        return;
    m_buffer->beginGroup();
    QTextCursor edit(document());
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        const QString text = block.text();
        if (!text.trimmed().isEmpty()) {
            if (commented) {
                const int at = int(text.indexOf(prefix));
                const int length = int(prefix.size()) + (text.mid(at + prefix.size(), 1) == QLatin1String(" ") ? 1 : 0);
                edit.setPosition(block.position() + at);
                edit.setPosition(block.position() + at + length, QTextCursor::KeepAnchor);
                edit.removeSelectedText();
            } else {
                edit.setPosition(block.position() + indent);
                edit.insertText(prefix + QLatin1Char(' '));
            }
        }
        if (block == last)
            break;
    }
    m_buffer->endGroup();
}

void CodeEditor::duplicateLine()
{
    QTextCursor cursor = textCursor();
    const QTextBlock block = cursor.block();
    QTextCursor edit(block);
    edit.movePosition(QTextCursor::EndOfBlock);
    m_buffer->breakTyping();
    edit.insertText(QLatin1Char('\n') + block.text());
    m_buffer->breakTyping();
    cursor.setPosition(block.next().position() + cursor.positionInBlock());
    setTextCursor(cursor);
}

void CodeEditor::deleteLine()
{
    QTextCursor cursor = textCursor();
    QTextBlock block = cursor.block();
    QTextCursor edit(document());
    if (block.next().isValid()) {
        edit.setPosition(block.position());
        edit.setPosition(block.next().position(), QTextCursor::KeepAnchor);
    } else {
        // The last line takes the newline before it.
        edit.setPosition(qMax(0, block.position() - 1));
        edit.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
    }
    m_buffer->breakTyping();
    edit.removeSelectedText();
    m_buffer->breakTyping();
}

void CodeEditor::contextMenuEvent(QContextMenuEvent *event)
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    const bool selection = textCursor().hasSelection();
    menu->addAction(tr("Undo"), QKeySequence::Undo, this, &CodeEditor::undo)->setEnabled(m_buffer->canUndo());
    menu->addAction(tr("Redo"), QKeySequence::Redo, this, &CodeEditor::redo)->setEnabled(m_buffer->canRedo());
    menu->addSeparator();
    menu->addAction(tr("Cut"), QKeySequence::Cut, this, &QPlainTextEdit::cut)->setEnabled(selection);
    menu->addAction(tr("Copy"), QKeySequence::Copy, this, &QPlainTextEdit::copy)->setEnabled(selection);
    menu->addAction(tr("Paste"), QKeySequence::Paste, this, &QPlainTextEdit::paste)->setEnabled(canPaste());
    menu->addSeparator();
    menu->addAction(tr("Select all"), QKeySequence::SelectAll, this, &QPlainTextEdit::selectAll);
    if (!Languages::commentPrefix(language()).isEmpty())
        menu->addAction(tr("Toggle comment"), QKeySequence(QStringLiteral("Ctrl+/")), this, &CodeEditor::toggleComment);
    menu->popup(event->globalPos());
}
