// SPDX-License-Identifier: GPL-3.0-or-later
#include "filetypes.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class FileTypeTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void knownFormatsAndVirtualEntries()
    {
        QCOMPARE(FileType::forName(QStringLiteral("reads.FASTQ.gz")).label, QStringLiteral("FASTQ (gzip)"));
        QCOMPARE(FileType::forName(QStringLiteral("Snakefile")).glyph, QStringLiteral("file-code"));
        QCOMPARE(FileType::forName(QStringLiteral("sample.bam")).glyph, QStringLiteral("dna"));
        QCOMPARE(FileType::forName(QStringLiteral("sample.vcf")).label, QStringLiteral("VCF variants"));
        QCOMPARE(FileType::forName(QStringLiteral("table.tsv")).glyph, QStringLiteral("table"));
        QCOMPARE(FileType::forName(QStringLiteral("unmounted/project"), true).mimeType, QStringLiteral("inode/directory"));
        QCOMPARE(FileType::forName(QStringLiteral("nested/work.tar.gz")).glyph, QStringLiteral("file-archive"));
    }
    void virtualClassificationDoesNotInspectContents()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("unknown"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("This extensionless file contains text for MIME sniffing.\n");
        file.close();
        QCOMPARE(FileType::forName(path).mimeType, QStringLiteral("application/octet-stream"));
        QCOMPARE(FileType::of(QFileInfo(path)).mimeType, QStringLiteral("text/plain"));
    }
};
QTEST_MAIN(FileTypeTest)
#include "filetypetest.moc"
