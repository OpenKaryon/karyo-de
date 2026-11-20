// SPDX-License-Identifier: GPL-3.0-or-later
// Syntax colors of KaryonEditor.dc.html: comments muted italic, keywords and
// rule directives in ink-1, strings in ink-2 with {placeholders} in ink-1,
// numbers in sage, CONSTANTS in the accent. A small lexer per line covers the
// languages bioinformaticians keep open next to each other.
#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

enum class Language {
    Plain,
    Snakemake,
    Python,
    R,
    Shell,
    Nextflow,
    Yaml,
    Json,
    Markdown,
    Table,
    Sequence,
};

namespace Languages {
QList<Language> all();
QString name(Language language);
// "snakemake" and back, for the session file.
QString key(Language language);
Language fromKey(const QString &key);
// By file name, then by the first line (#!/usr/bin/env python3).
Language detect(const QString &fileName, const QString &firstLine);
// Title-tab glyph.
QString glyph(Language language);
// Tab inserts a tab character (tables, Makefiles) instead of spaces.
bool usesTabs(Language language);
// Line comment prefix for Toggle comment; empty if none.
QString commentPrefix(Language language);
} // namespace Languages

class Highlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit Highlighter(QTextDocument *document);
    void setLanguage(Language language);
    Language language() const { return m_language; }
    // Rebuilds the formats from the current theme.
    void updateColors();

protected:
    void highlightBlock(const QString &text) override;

private:
    void highlightCode(const QString &text);
    void highlightString(const QString &text, int from, int to);
    void highlightYaml(const QString &text);
    void highlightJson(const QString &text);
    void highlightMarkdown(const QString &text);

    Language m_language = Language::Plain;
    QTextCharFormat m_comment;
    QTextCharFormat m_keyword;
    QTextCharFormat m_string;
    QTextCharFormat m_placeholder;
    QTextCharFormat m_number;
    QTextCharFormat m_constant;
    QTextCharFormat m_heading;
};
