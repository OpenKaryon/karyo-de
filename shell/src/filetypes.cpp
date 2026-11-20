// SPDX-License-Identifier: GPL-3.0-or-later
#include "filetypes.h"

#include <karyo/ui/theme.h>

#include <QCoreApplication>
#include <QHash>
#include <QMimeDatabase>

namespace Ui = Karyo::Ui;

namespace {

enum class Family { Folder, Sequence, Code, Table, Text, Image, Archive, Document, Other };

struct Known
{
    const char *label;
    Family family;
};

// Lower-case suffixes (without a trailing .gz) of formats the MIME database names poorly.
const QHash<QString, Known> &bioFormats()
{
    static const QHash<QString, Known> formats = {
        {QStringLiteral("fastq"), {QT_TRANSLATE_NOOP("FileType", "FASTQ"), Family::Sequence}},
        {QStringLiteral("fq"), {QT_TRANSLATE_NOOP("FileType", "FASTQ"), Family::Sequence}},
        {QStringLiteral("fasta"), {QT_TRANSLATE_NOOP("FileType", "FASTA"), Family::Sequence}},
        {QStringLiteral("fa"), {QT_TRANSLATE_NOOP("FileType", "FASTA"), Family::Sequence}},
        {QStringLiteral("fna"), {QT_TRANSLATE_NOOP("FileType", "FASTA"), Family::Sequence}},
        {QStringLiteral("faa"), {QT_TRANSLATE_NOOP("FileType", "FASTA (protein)"), Family::Sequence}},
        {QStringLiteral("bam"), {QT_TRANSLATE_NOOP("FileType", "BAM alignment"), Family::Sequence}},
        {QStringLiteral("sam"), {QT_TRANSLATE_NOOP("FileType", "SAM alignment"), Family::Sequence}},
        {QStringLiteral("cram"), {QT_TRANSLATE_NOOP("FileType", "CRAM alignment"), Family::Sequence}},
        {QStringLiteral("vcf"), {QT_TRANSLATE_NOOP("FileType", "VCF variants"), Family::Sequence}},
        {QStringLiteral("bcf"), {QT_TRANSLATE_NOOP("FileType", "BCF variants"), Family::Sequence}},
        {QStringLiteral("bed"), {QT_TRANSLATE_NOOP("FileType", "BED intervals"), Family::Table}},
        {QStringLiteral("gtf"), {QT_TRANSLATE_NOOP("FileType", "GTF annotation"), Family::Table}},
        {QStringLiteral("gff"), {QT_TRANSLATE_NOOP("FileType", "GFF annotation"), Family::Table}},
        {QStringLiteral("gff3"), {QT_TRANSLATE_NOOP("FileType", "GFF annotation"), Family::Table}},
        {QStringLiteral("pdb"), {QT_TRANSLATE_NOOP("FileType", "PDB structure"), Family::Sequence}},
        {QStringLiteral("cif"), {QT_TRANSLATE_NOOP("FileType", "mmCIF structure"), Family::Sequence}},
        {QStringLiteral("h5ad"), {QT_TRANSLATE_NOOP("FileType", "AnnData"), Family::Table}},
        {QStringLiteral("tsv"), {QT_TRANSLATE_NOOP("FileType", "Tab-separated values"), Family::Table}},
        {QStringLiteral("csv"), {QT_TRANSLATE_NOOP("FileType", "Comma-separated values"), Family::Table}},
        {QStringLiteral("yaml"), {QT_TRANSLATE_NOOP("FileType", "YAML"), Family::Code}},
        {QStringLiteral("yml"), {QT_TRANSLATE_NOOP("FileType", "YAML"), Family::Code}},
        {QStringLiteral("smk"), {QT_TRANSLATE_NOOP("FileType", "Snakemake workflow"), Family::Code}},
        {QStringLiteral("nf"), {QT_TRANSLATE_NOOP("FileType", "Nextflow workflow"), Family::Code}},
        {QStringLiteral("ipynb"), {QT_TRANSLATE_NOOP("FileType", "Jupyter notebook"), Family::Code}},
        {QStringLiteral("md"), {QT_TRANSLATE_NOOP("FileType", "Markdown"), Family::Text}},
        {QStringLiteral("log"), {QT_TRANSLATE_NOOP("FileType", "Log file"), Family::Text}},
    };
    return formats;
}

Family familyOf(const QMimeType &mime)
{
    const QString name = mime.name();
    if (name.startsWith(QLatin1String("image/")))
        return Family::Image;
    if (name.startsWith(QLatin1String("text/x-")) || name.contains(QLatin1String("script"))
        || name.contains(QLatin1String("json")) || name.contains(QLatin1String("xml"))
        || name.contains(QLatin1String("yaml")))
        return Family::Code;
    if (name.startsWith(QLatin1String("text/")))
        return Family::Text;
    if (name.contains(QLatin1String("zip")) || name.contains(QLatin1String("tar"))
        || name.contains(QLatin1String("compressed")) || name.contains(QLatin1String("archive"))
        || name.contains(QLatin1String("7z")) || name.contains(QLatin1String("rar")))
        return Family::Archive;
    if (name.contains(QLatin1String("pdf")) || name.contains(QLatin1String("document"))
        || name.contains(QLatin1String("opendocument")) || name.contains(QLatin1String("msword")))
        return Family::Document;
    if (name.contains(QLatin1String("spreadsheet")) || name.contains(QLatin1String("csv")))
        return Family::Table;
    return Family::Other;
}

void paintFamily(FileType &type, Family family)
{
    switch (family) {
    case Family::Folder:
        type.glyph = QStringLiteral("folder");
        type.tile = Ui::accent(300);
        type.ink = Ui::accent(800);
        break;
    case Family::Sequence:
        type.glyph = QStringLiteral("dna");
        type.tile = Ui::sage(200);
        type.ink = Ui::sage(800);
        break;
    case Family::Code:
        type.glyph = QStringLiteral("file-code");
        type.tile = Ui::sage(300);
        type.ink = Ui::sage(800);
        break;
    case Family::Table:
        type.glyph = QStringLiteral("table");
        type.tile = Ui::accent(200);
        type.ink = Ui::accent(700);
        break;
    case Family::Image:
        type.glyph = QStringLiteral("image");
        type.tile = Ui::accent(200);
        type.ink = Ui::accent(700);
        break;
    case Family::Archive:
        type.glyph = QStringLiteral("file-archive");
        type.tile = Ui::neutral(300);
        type.ink = Ui::neutral(800);
        break;
    case Family::Document:
    case Family::Text:
    case Family::Other:
        type.glyph = QStringLiteral("file-text");
        type.tile = Ui::neutral(200);
        type.ink = Ui::neutral(700);
        break;
    }
}

} // namespace

static FileType classify(const QFileInfo &info, bool directory, bool inspectContents)
{
    FileType type;
    if (directory) {
        type.label = QCoreApplication::translate("FileType", "Folder");
        type.mimeType = QStringLiteral("inode/directory");
        paintFamily(type, Family::Folder);
        return type;
    }
    static QMimeDatabase db;
    const QString name = info.fileName();
    QString lower = name.toLower();
    bool gzip = false;
    for (const char *suffix : {".gz", ".bgz", ".bz2", ".zst"}) {
        if (lower.endsWith(QLatin1String(suffix))) {
            lower.chop(int(qstrlen(suffix)));
            gzip = true;
            break;
        }
    }
    const QString suffix = lower.section(QLatin1Char('.'), -1);
    QMimeType mime;
    if (inspectContents) {
        mime = db.mimeTypeForFile(info, QMimeDatabase::MatchExtension);
    } else {
        const auto matches = db.mimeTypesForFileName(name);
        mime = matches.isEmpty() ? db.mimeTypeForName(QStringLiteral("application/octet-stream")) : matches.first();
    }
    // A name that says nothing (Snakefile, README, run_all): the first bytes
    // tell, so the file still has an app to open it.
    if (inspectContents && mime.isDefault() && info.isFile() && info.isReadable())
        mime = db.mimeTypeForFile(info, QMimeDatabase::MatchContent);
    type.mimeType = mime.name();
    if (name == QLatin1String("Snakefile") || lower.endsWith(QLatin1String(".snakefile"))) {
        if (mime.isDefault())
            type.mimeType = QStringLiteral("text/plain");
        type.label = QCoreApplication::translate("FileType", "Snakemake workflow");
        paintFamily(type, Family::Code);
        return type;
    }
    if (lower.contains(QLatin1Char('.'))) {
        const auto known = bioFormats().constFind(suffix);
        if (known != bioFormats().constEnd()) {
            type.label = QCoreApplication::translate("FileType", known->label);
            if (gzip)
                type.label += QCoreApplication::translate("FileType", " (gzip)");
            paintFamily(type, known->family);
            return type;
        }
    }
    type.label = mime.comment().isEmpty() ? mime.name() : mime.comment();
    if (!type.label.isEmpty())
        type.label[0] = type.label.at(0).toUpper();
    paintFamily(type, familyOf(mime));
    return type;
}

FileType FileType::of(const QFileInfo &info)
{
    return classify(info, info.isDir(), true);
}

FileType FileType::forName(const QString &name, bool directory)
{
    return classify(QFileInfo(name), directory, false);
}

QString formatSize(qint64 bytes)
{
    if (bytes < 1024)
        return QCoreApplication::translate("FileType", "%1 B").arg(bytes);
    const char *units[] = {QT_TRANSLATE_NOOP("FileType", "KB"), QT_TRANSLATE_NOOP("FileType", "MB"),
                           QT_TRANSLATE_NOOP("FileType", "GB"), QT_TRANSLATE_NOOP("FileType", "TB")};
    double value = bytes / 1024.0;
    int unit = 0;
    while (value >= 1024 && unit < 3) {
        value /= 1024;
        ++unit;
    }
    const QString number = value >= 10 || unit == 0 ? QString::number(qRound(value)) : QString::number(value, 'f', 1);
    return number + QLatin1Char(' ') + QCoreApplication::translate("FileType", units[unit]);
}
