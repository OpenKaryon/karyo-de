// SPDX-License-Identifier: GPL-3.0-or-later
// How a file is labeled and drawn: the design's 26px type tiles, with
// bioinformatics formats (FASTQ, BAM, VCF, Snakefile...) named explicitly.
#pragma once

#include <QColor>
#include <QFileInfo>
#include <QString>

struct FileType
{
    QString label;   // "FASTQ (gzip)", "Folder", "Tab-separated values"
    QString glyph;   // Lucide icon
    QColor tile;
    QColor ink;
    QString mimeType;

    static FileType of(const QFileInfo &info);
    // Virtual entries, e.g. archives: identify by name without reading disk.
    static FileType forName(const QString &name, bool directory = false);
};

QString formatSize(qint64 bytes);
