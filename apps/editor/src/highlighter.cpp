// SPDX-License-Identifier: GPL-3.0-or-later
#include "highlighter.h"

#include <karyo/ui/theme.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>

namespace Ui = Karyo::Ui;

namespace {

class Text
{
    Q_DECLARE_TR_FUNCTIONS(Languages)
};

// Block states for text that goes on past the end of its line.
enum State { Normal = 0, TripleDouble = 1, TripleSingle = 2, BlockComment = 3, Fence = 4 };

const QStringList &pythonKeywords()
{
    static const QStringList words = {
        QStringLiteral("False"),  QStringLiteral("None"),     QStringLiteral("True"),   QStringLiteral("and"),
        QStringLiteral("as"),     QStringLiteral("assert"),   QStringLiteral("async"),  QStringLiteral("await"),
        QStringLiteral("break"),  QStringLiteral("class"),    QStringLiteral("continue"), QStringLiteral("def"),
        QStringLiteral("del"),    QStringLiteral("elif"),     QStringLiteral("else"),   QStringLiteral("except"),
        QStringLiteral("finally"), QStringLiteral("for"),     QStringLiteral("from"),   QStringLiteral("global"),
        QStringLiteral("if"),     QStringLiteral("import"),   QStringLiteral("in"),     QStringLiteral("is"),
        QStringLiteral("lambda"), QStringLiteral("nonlocal"), QStringLiteral("not"),    QStringLiteral("or"),
        QStringLiteral("pass"),   QStringLiteral("raise"),    QStringLiteral("return"), QStringLiteral("try"),
        QStringLiteral("while"),  QStringLiteral("with"),     QStringLiteral("yield"),
    };
    return words;
}

// Snakemake's rule directives: keywords where a line starts with them.
const QStringList &snakemakeDirectives()
{
    static const QStringList words = {
        QStringLiteral("rule"),      QStringLiteral("checkpoint"), QStringLiteral("input"),
        QStringLiteral("output"),    QStringLiteral("params"),     QStringLiteral("log"),
        QStringLiteral("threads"),   QStringLiteral("resources"),  QStringLiteral("conda"),
        QStringLiteral("container"), QStringLiteral("shell"),      QStringLiteral("run"),
        QStringLiteral("script"),    QStringLiteral("wrapper"),    QStringLiteral("benchmark"),
        QStringLiteral("message"),   QStringLiteral("priority"),   QStringLiteral("configfile"),
        QStringLiteral("include"),   QStringLiteral("localrules"), QStringLiteral("wildcard_constraints"),
        QStringLiteral("onstart"),   QStringLiteral("onsuccess"),  QStringLiteral("onerror"),
        QStringLiteral("envmodules"), QStringLiteral("notebook"),  QStringLiteral("module"),
        QStringLiteral("use"),       QStringLiteral("group"),      QStringLiteral("singularity"),
    };
    return words;
}

const QStringList &rKeywords()
{
    static const QStringList words = {
        QStringLiteral("if"),     QStringLiteral("else"),    QStringLiteral("repeat"), QStringLiteral("while"),
        QStringLiteral("function"), QStringLiteral("for"),   QStringLiteral("in"),     QStringLiteral("next"),
        QStringLiteral("break"),  QStringLiteral("TRUE"),    QStringLiteral("FALSE"),  QStringLiteral("NULL"),
        QStringLiteral("Inf"),    QStringLiteral("NaN"),     QStringLiteral("NA"),     QStringLiteral("library"),
        QStringLiteral("require"), QStringLiteral("return"), QStringLiteral("source"),
    };
    return words;
}

const QStringList &shellKeywords()
{
    static const QStringList words = {
        QStringLiteral("if"),    QStringLiteral("then"),   QStringLiteral("else"),   QStringLiteral("elif"),
        QStringLiteral("fi"),    QStringLiteral("case"),   QStringLiteral("esac"),   QStringLiteral("for"),
        QStringLiteral("while"), QStringLiteral("until"),  QStringLiteral("do"),     QStringLiteral("done"),
        QStringLiteral("in"),    QStringLiteral("function"), QStringLiteral("return"), QStringLiteral("export"),
        QStringLiteral("local"), QStringLiteral("set"),    QStringLiteral("source"), QStringLiteral("exit"),
        QStringLiteral("echo"),  QStringLiteral("cd"),     QStringLiteral("readonly"), QStringLiteral("shift"),
    };
    return words;
}

const QStringList &nextflowKeywords()
{
    static const QStringList words = {
        QStringLiteral("process"), QStringLiteral("workflow"), QStringLiteral("input"),   QStringLiteral("output"),
        QStringLiteral("script"),  QStringLiteral("shell"),    QStringLiteral("exec"),    QStringLiteral("when"),
        QStringLiteral("main"),    QStringLiteral("take"),     QStringLiteral("emit"),    QStringLiteral("include"),
        QStringLiteral("from"),    QStringLiteral("params"),   QStringLiteral("Channel"), QStringLiteral("channel"),
        QStringLiteral("publishDir"), QStringLiteral("container"), QStringLiteral("conda"), QStringLiteral("cpus"),
        QStringLiteral("memory"),  QStringLiteral("time"),     QStringLiteral("tag"),     QStringLiteral("label"),
        QStringLiteral("path"),    QStringLiteral("val"),      QStringLiteral("tuple"),   QStringLiteral("file"),
        QStringLiteral("def"),     QStringLiteral("if"),       QStringLiteral("else"),    QStringLiteral("return"),
        QStringLiteral("true"),    QStringLiteral("false"),    QStringLiteral("null"),
    };
    return words;
}

bool isWordChar(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); }

// SAMPLES, MAX_READS: upper-case names of three or more characters.
bool isConstant(const QString &word)
{
    if (word.size() < 3)
        return false;
    bool letter = false;
    for (const QChar c : word) {
        if (c.isLower())
            return false;
        letter = letter || c.isLetter();
    }
    return letter;
}

} // namespace

namespace Languages {

QList<Language> all()
{
    return {Language::Plain,    Language::Snakemake, Language::Python, Language::R,
            Language::Shell,    Language::Nextflow,  Language::Yaml,   Language::Json,
            Language::Markdown, Language::Table,     Language::Sequence};
}

QString name(Language language)
{
    switch (language) {
    case Language::Plain: return Text::tr("Plain text");
    case Language::Snakemake: return QStringLiteral("Snakemake");
    case Language::Python: return QStringLiteral("Python");
    case Language::R: return QStringLiteral("R");
    case Language::Shell: return QStringLiteral("Shell");
    case Language::Nextflow: return QStringLiteral("Nextflow");
    case Language::Yaml: return QStringLiteral("YAML");
    case Language::Json: return QStringLiteral("JSON");
    case Language::Markdown: return QStringLiteral("Markdown");
    case Language::Table: return Text::tr("Table (TSV, CSV)");
    case Language::Sequence: return QStringLiteral("FASTA / FASTQ");
    }
    return {};
}

QString key(Language language)
{
    static const QStringList keys = {
        QStringLiteral("plain"), QStringLiteral("snakemake"), QStringLiteral("python"), QStringLiteral("r"),
        QStringLiteral("shell"), QStringLiteral("nextflow"),  QStringLiteral("yaml"),   QStringLiteral("json"),
        QStringLiteral("markdown"), QStringLiteral("table"),  QStringLiteral("sequence"),
    };
    return keys.value(int(language));
}

Language fromKey(const QString &k)
{
    for (const Language language : all()) {
        if (key(language) == k)
            return language;
    }
    return Language::Plain;
}

Language detect(const QString &fileName, const QString &firstLine)
{
    const QString name = QFileInfo(fileName).fileName();
    const QString lower = name.toLower();
    const QString suffix = QFileInfo(lower).suffix();
    if (lower == QLatin1String("snakefile") || suffix == QLatin1String("smk"))
        return Language::Snakemake;
    if (lower == QLatin1String("nextflow.config") || suffix == QLatin1String("nf"))
        return Language::Nextflow;
    static const QHash<QString, Language> bySuffix = {
        {QStringLiteral("py"), Language::Python},      {QStringLiteral("pyw"), Language::Python},
        {QStringLiteral("r"), Language::R},            {QStringLiteral("sh"), Language::Shell},
        {QStringLiteral("bash"), Language::Shell},     {QStringLiteral("zsh"), Language::Shell},
        {QStringLiteral("yaml"), Language::Yaml},      {QStringLiteral("yml"), Language::Yaml},
        {QStringLiteral("cwl"), Language::Yaml},       {QStringLiteral("json"), Language::Json},
        {QStringLiteral("ipynb"), Language::Json},     {QStringLiteral("md"), Language::Markdown},
        {QStringLiteral("markdown"), Language::Markdown}, {QStringLiteral("rmd"), Language::Markdown},
        {QStringLiteral("qmd"), Language::Markdown},   {QStringLiteral("tsv"), Language::Table},
        {QStringLiteral("csv"), Language::Table},      {QStringLiteral("tab"), Language::Table},
        {QStringLiteral("bed"), Language::Table},      {QStringLiteral("gff"), Language::Table},
        {QStringLiteral("gff3"), Language::Table},     {QStringLiteral("gtf"), Language::Table},
        {QStringLiteral("vcf"), Language::Table},      {QStringLiteral("sam"), Language::Table},
        {QStringLiteral("fa"), Language::Sequence},    {QStringLiteral("fasta"), Language::Sequence},
        {QStringLiteral("fna"), Language::Sequence},   {QStringLiteral("faa"), Language::Sequence},
        {QStringLiteral("fas"), Language::Sequence},   {QStringLiteral("fq"), Language::Sequence},
        {QStringLiteral("fastq"), Language::Sequence},
    };
    if (bySuffix.contains(suffix))
        return bySuffix.value(suffix);
    if (lower == QLatin1String(".bashrc") || lower == QLatin1String(".profile")
        || lower == QLatin1String(".bash_profile") || lower == QLatin1String("makefile"))
        return Language::Shell;
    if (firstLine.startsWith(QLatin1String("#!"))) {
        if (firstLine.contains(QLatin1String("snakemake")))
            return Language::Snakemake;
        if (firstLine.contains(QLatin1String("python")))
            return Language::Python;
        if (firstLine.contains(QLatin1String("Rscript")))
            return Language::R;
        if (firstLine.contains(QLatin1String("nextflow")))
            return Language::Nextflow;
        if (firstLine.contains(QLatin1String("sh")))
            return Language::Shell;
    }
    return Language::Plain;
}

QString glyph(Language language)
{
    switch (language) {
    case Language::Plain:
    case Language::Markdown:
        return QStringLiteral("file-text");
    case Language::Table:
        return QStringLiteral("table");
    case Language::Sequence:
        return QStringLiteral("dna");
    default:
        return QStringLiteral("file-code");
    }
}

bool usesTabs(Language language) { return language == Language::Table; }

QString commentPrefix(Language language)
{
    switch (language) {
    case Language::Snakemake:
    case Language::Python:
    case Language::R:
    case Language::Shell:
    case Language::Yaml:
        return QStringLiteral("#");
    case Language::Nextflow:
        return QStringLiteral("//");
    default:
        return {};
    }
}

} // namespace Languages

Highlighter::Highlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    updateColors();
}

void Highlighter::setLanguage(Language language)
{
    if (language == m_language)
        return;
    m_language = language;
    rehighlight();
}

void Highlighter::updateColors()
{
    const Ui::Theme &t = Ui::Theme::current();
    const bool dark = t.scheme == Ui::ColorScheme::Dark;
    m_comment = {};
    m_comment.setForeground(t.muted);
    m_comment.setFontItalic(true);
    m_keyword = {};
    m_keyword.setForeground(t.ink1);
    m_keyword.setFontWeight(QFont::DemiBold);
    m_string = {};
    m_string.setForeground(t.ink2);
    m_placeholder = m_keyword;
    m_number = {};
    m_number.setForeground(Ui::sage(dark ? 400 : 600));
    m_number.setFontWeight(QFont::DemiBold);
    m_constant = {};
    m_constant.setForeground(Ui::accent(dark ? 400 : 600));
    m_constant.setFontWeight(QFont::DemiBold);
    m_heading = m_keyword;
    rehighlight();
}

void Highlighter::highlightBlock(const QString &text)
{
    setCurrentBlockState(Normal);
    switch (m_language) {
    case Language::Plain:
        return;
    case Language::Yaml:
        highlightYaml(text);
        return;
    case Language::Json:
        highlightJson(text);
        return;
    case Language::Markdown:
        highlightMarkdown(text);
        return;
    case Language::Table:
        // The header row and comment lines (## in VCF, # in BED).
        if (text.startsWith(QLatin1Char('#')))
            setFormat(0, int(text.size()), m_comment);
        else if (currentBlock().blockNumber() == 0)
            setFormat(0, int(text.size()), m_keyword);
        return;
    case Language::Sequence:
        if (text.startsWith(QLatin1Char('>')) || text.startsWith(QLatin1Char('@')))
            setFormat(0, int(text.size()), m_keyword);
        else if (text.startsWith(QLatin1Char('+')) || text.startsWith(QLatin1Char(';')))
            setFormat(0, int(text.size()), m_comment);
        return;
    default:
        highlightCode(text);
    }
}

void Highlighter::highlightString(const QString &text, int from, int to)
{
    setFormat(from, to - from, m_string);
    // {sample} in Snakemake and Python, ${var} and $var in shell and Nextflow.
    const bool braces = m_language == Language::Snakemake || m_language == Language::Python;
    const bool dollars = m_language == Language::Shell || m_language == Language::Nextflow;
    for (int i = from; i < to; ++i) {
        const QChar c = text.at(i);
        if (braces && c == QLatin1Char('{')) {
            const int end = int(text.indexOf(QLatin1Char('}'), i));
            if (end < 0 || end >= to)
                break;
            setFormat(i, end + 1 - i, m_placeholder);
            i = end;
        } else if (dollars && c == QLatin1Char('$') && i + 1 < to) {
            int j = i + 1;
            if (text.at(j) == QLatin1Char('{')) {
                const int end = int(text.indexOf(QLatin1Char('}'), j));
                j = end < 0 || end >= to ? to : end + 1;
            } else {
                while (j < to && isWordChar(text.at(j)))
                    ++j;
            }
            setFormat(i, j - i, m_placeholder);
            i = j - 1;
        }
    }
}

void Highlighter::highlightCode(const QString &text)
{
    const int n = int(text.size());
    const bool python = m_language == Language::Python || m_language == Language::Snakemake;
    const QString lineComment = m_language == Language::Nextflow ? QStringLiteral("//") : QStringLiteral("#");
    const QStringList &keywords = m_language == Language::R          ? rKeywords()
                                  : m_language == Language::Shell    ? shellKeywords()
                                  : m_language == Language::Nextflow ? nextflowKeywords()
                                                                     : pythonKeywords();
    int i = 0;
    // Text carried over from the previous line.
    const int state = previousBlockState();
    if (state == TripleDouble || state == TripleSingle) {
        const QString quotes(3, state == TripleDouble ? QLatin1Char('"') : QLatin1Char('\''));
        const int end = int(text.indexOf(quotes));
        if (end < 0) {
            highlightString(text, 0, n);
            setCurrentBlockState(state);
            return;
        }
        highlightString(text, 0, end + 3);
        i = end + 3;
    } else if (state == BlockComment) {
        const int end = int(text.indexOf(QLatin1String("*/")));
        if (end < 0) {
            setFormat(0, n, m_comment);
            setCurrentBlockState(BlockComment);
            return;
        }
        setFormat(0, end + 2, m_comment);
        i = end + 2;
    }

    // Snakemake's directives where the line starts.
    int lead = 0;
    while (lead < n && text.at(lead).isSpace())
        ++lead;
    if (m_language == Language::Snakemake && i <= lead) {
        int end = lead;
        while (end < n && isWordChar(text.at(end)))
            ++end;
        if (snakemakeDirectives().contains(text.mid(lead, end - lead))) {
            setFormat(lead, end - lead, m_keyword);
            i = end;
        }
    }

    while (i < n) {
        const QChar c = text.at(i);
        // In shell a # is a comment only at a word start ($# is not).
        if (text.mid(i, lineComment.size()) == lineComment
            && (m_language != Language::Shell || i == 0 || text.at(i - 1).isSpace())) {
            setFormat(i, n - i, m_comment);
            return;
        }
        if (m_language == Language::Nextflow && text.mid(i, 2) == QLatin1String("/*")) {
            const int end = int(text.indexOf(QLatin1String("*/"), i + 2));
            if (end < 0) {
                setFormat(i, n - i, m_comment);
                setCurrentBlockState(BlockComment);
                return;
            }
            setFormat(i, end + 2 - i, m_comment);
            i = end + 2;
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')
            || (c == QLatin1Char('`') && (m_language == Language::Shell || m_language == Language::R))) {
            const QString triple(3, c);
            if (python && text.mid(i, 3) == triple) {
                const int end = int(text.indexOf(triple, i + 3));
                if (end < 0) {
                    highlightString(text, i, n);
                    setCurrentBlockState(c == QLatin1Char('"') ? TripleDouble : TripleSingle);
                    return;
                }
                highlightString(text, i, end + 3);
                i = end + 3;
                continue;
            }
            int j = i + 1;
            while (j < n && text.at(j) != c)
                j += text.at(j) == QLatin1Char('\\') ? 2 : 1;
            const int end = qMin(j + 1, n);
            highlightString(text, i, end);
            i = end;
            continue;
        }
        if (c.isDigit() && (i == 0 || !isWordChar(text.at(i - 1)))) {
            int j = i;
            while (j < n && (text.at(j).isLetterOrNumber() || text.at(j) == QLatin1Char('.')))
                ++j;
            setFormat(i, j - i, m_number);
            i = j;
            continue;
        }
        if (c.isLetter() || c == QLatin1Char('_')) {
            int j = i;
            while (j < n && (isWordChar(text.at(j)) || (m_language == Language::R && text.at(j) == QLatin1Char('.'))))
                ++j;
            const QString word = text.mid(i, j - i);
            if (keywords.contains(word))
                setFormat(i, j - i, m_keyword);
            else if (isConstant(word))
                setFormat(i, j - i, m_constant);
            i = j;
            continue;
        }
        if (m_language == Language::Shell && c == QLatin1Char('$')) {
            int j = i + 1;
            if (j < n && text.at(j) == QLatin1Char('{')) {
                const int end = int(text.indexOf(QLatin1Char('}'), j));
                j = end < 0 ? n : end + 1;
            } else {
                while (j < n && isWordChar(text.at(j)))
                    ++j;
            }
            setFormat(i, j - i, m_placeholder);
            i = j;
            continue;
        }
        ++i;
    }
}

void Highlighter::highlightYaml(const QString &text)
{
    static const QRegularExpression key(QStringLiteral("^(\\s*-?\\s*)([^\\s#'\"][^:#]*?)\\s*:(\\s|$)"));
    const int n = int(text.size());
    int i = 0;
    const auto match = key.match(text);
    if (match.hasMatch()) {
        setFormat(int(match.capturedStart(2)), int(match.capturedLength(2)), m_keyword);
        i = int(match.capturedEnd(2));
    }
    while (i < n) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('#') && (i == 0 || text.at(i - 1).isSpace())) {
            setFormat(i, n - i, m_comment);
            return;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            int j = i + 1;
            while (j < n && text.at(j) != c)
                j += text.at(j) == QLatin1Char('\\') ? 2 : 1;
            setFormat(i, qMin(j + 1, n) - i, m_string);
            i = j + 1;
            continue;
        }
        if ((c.isDigit() || c.isLetter()) && (i == 0 || !isWordChar(text.at(i - 1)))) {
            int j = i;
            while (j < n && (isWordChar(text.at(j)) || text.at(j) == QLatin1Char('.')))
                ++j;
            const QString word = text.mid(i, j - i);
            static const QStringList literals = {QStringLiteral("true"), QStringLiteral("false"),
                                                 QStringLiteral("null"), QStringLiteral("yes"),
                                                 QStringLiteral("no"), QStringLiteral("~")};
            if (c.isDigit())
                setFormat(i, j - i, m_number);
            else if (literals.contains(word.toLower()))
                setFormat(i, j - i, m_keyword);
            i = j;
            continue;
        }
        ++i;
    }
}

void Highlighter::highlightJson(const QString &text)
{
    const int n = int(text.size());
    int i = 0;
    while (i < n) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('"')) {
            int j = i + 1;
            while (j < n && text.at(j) != QLatin1Char('"'))
                j += text.at(j) == QLatin1Char('\\') ? 2 : 1;
            const int end = qMin(j + 1, n);
            int k = end;
            while (k < n && text.at(k).isSpace())
                ++k;
            // A key is a string followed by a colon.
            setFormat(i, end - i, k < n && text.at(k) == QLatin1Char(':') ? m_keyword : m_string);
            i = end;
            continue;
        }
        if (c.isDigit() || c == QLatin1Char('-')) {
            int j = i + 1;
            while (j < n && (text.at(j).isDigit() || QStringLiteral(".eE+-").contains(text.at(j))))
                ++j;
            setFormat(i, j - i, m_number);
            i = j;
            continue;
        }
        if (c.isLetter()) {
            int j = i;
            while (j < n && text.at(j).isLetter())
                ++j;
            setFormat(i, j - i, m_keyword);
            i = j;
            continue;
        }
        ++i;
    }
}

void Highlighter::highlightMarkdown(const QString &text)
{
    const int n = int(text.size());
    const bool fence = text.trimmed().startsWith(QLatin1String("```"));
    if (previousBlockState() == Fence) {
        setFormat(0, n, m_string);
        if (!fence)
            setCurrentBlockState(Fence);
        return;
    }
    if (fence) {
        setFormat(0, n, m_string);
        setCurrentBlockState(Fence);
        return;
    }
    if (text.startsWith(QLatin1Char('#'))) {
        setFormat(0, n, m_heading);
        return;
    }
    static const QRegularExpression code(QStringLiteral("`[^`]+`"));
    for (auto it = code.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        setFormat(int(m.capturedStart()), int(m.capturedLength()), m_string);
    }
    static const QRegularExpression bold(QStringLiteral("\\*\\*[^*]+\\*\\*"));
    for (auto it = bold.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        QTextCharFormat strong;
        strong.setFontWeight(QFont::DemiBold);
        setFormat(int(m.capturedStart()), int(m.capturedLength()), strong);
    }
    static const QRegularExpression link(QStringLiteral("\\[[^\\]]+\\]\\([^)]+\\)"));
    for (auto it = link.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        setFormat(int(m.capturedStart()), int(m.capturedLength()), m_string);
    }
}
