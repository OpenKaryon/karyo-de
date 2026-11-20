// SPDX-License-Identifier: GPL-3.0-or-later
// The text area of KaryonEditor.dc.html: IBM Plex Mono 14.5, a 64px line
// number gutter, the current line on --k-hover. Undo and Redo go to the
// buffer's own history; Tab indents, Enter keeps the indentation.
#pragma once

#include <QPlainTextEdit>

class Buffer;
class Highlighter;
class QTimer;
enum class Language;

class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    CodeEditor(Buffer *buffer, QWidget *parent = nullptr);

    Buffer *buffer() const { return m_buffer; }
    Highlighter *highlighter() const { return m_highlighter; }
    Language language() const;
    void setLanguage(Language language);
    // Indentation: spaces (width) or tab characters.
    bool indentsWithTabs() const { return m_tabs; }
    int indentWidth() const { return m_indentWidth; }
    void setIndentation(bool tabs, int width);
    // Guesses the indentation from the text (a Makefile's tabs).
    void detectIndentation();

    void undo();
    void redo();
    void toggleComment();
    void duplicateLine();
    void deleteLine();
    void setFontPixels(qreal pixels);
    void updateColors();

    int gutterWidth() const;
    void paintGutter(QPaintEvent *event);

Q_SIGNALS:
    void positionChanged(int line, int column);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void highlightCurrentLine();
    void indentSelection(bool outdent);
    QString indentUnit() const;

    Buffer *m_buffer;
    Highlighter *m_highlighter;
    QWidget *m_gutter;
    QTimer *m_blink = nullptr;
    bool m_caretOn = true;
    bool m_tabs = false;
    int m_indentWidth = 4;
};
