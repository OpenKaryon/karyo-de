// SPDX-License-Identifier: GPL-3.0-or-later
#include "buffer.h"
#include "codeeditor.h"
#include "highlighter.h"
#include <QtTest>

class ControlsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void commentAndDuplicateRemainUndoable()
    {
        Buffer buffer("fixture");
        CodeEditor editor(&buffer);
        editor.setLanguage(Language::Python);
        editor.setPlainText("value = 1");
        editor.show();
        editor.toggleComment();
        QCOMPARE(buffer.text(), QStringLiteral("# value = 1"));
        editor.undo();
        QCOMPARE(buffer.text(), QStringLiteral("value = 1"));
        editor.redo();
        QCOMPARE(buffer.text(), QStringLiteral("# value = 1"));
        editor.undo();
        editor.duplicateLine();
        QCOMPARE(buffer.text(), QStringLiteral("value = 1\nvalue = 1"));
        editor.undo();
        QCOMPARE(buffer.text(), QStringLiteral("value = 1"));
    }
    void universalFormatsStayAvailable()
    {
        QCOMPARE(Languages::detect("notes.md", {}), Language::Markdown);
        QCOMPARE(Languages::detect("table.csv", {}), Language::Table);
        QCOMPARE(Languages::detect("variants.vcf", {}), Language::Table);
        QCOMPARE(Languages::detect("sequences.fasta", {}), Language::Sequence);
        QCOMPARE(Languages::detect("analysis.py", {}), Language::Python);
    }
};
QTEST_MAIN(ControlsTest)
#include "controlstest.moc"
