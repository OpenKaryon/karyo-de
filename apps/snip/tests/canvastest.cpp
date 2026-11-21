// SPDX-License-Identifier: GPL-3.0-or-later
#include "canvas.h"
#include "capture.h"
#include <karyo/ui/style.h>
#include <QInputMethodEvent>
#include <QPlainTextEdit>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class CanvasTest : public QObject {
    Q_OBJECT
private:
    static void drag(Canvas &canvas, QPoint from, QPoint to) {
        QTest::mousePress(&canvas, Qt::LeftButton, {}, from);
        QTest::mouseMove(&canvas, to);
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, to);
    }
private Q_SLOTS:
    void initTestCase() { Karyo::Ui::applyStyle(); }
    void colorsTextAndHistory() {
        Canvas canvas;
        canvas.resize(432, 332); // 400x300 image at (16,16), exactly 1:1.
        QImage base(400, 300, QImage::Format_ARGB32_Premultiplied);
        base.fill(Qt::white);
        canvas.setImage(base);
        canvas.show();
        canvas.setTool(Canvas::Tool::Box);
        canvas.setColor(Qt::red);
        drag(canvas, {40,40}, {180,120});
        const QImage red = canvas.image();
        QCOMPARE(red.pixelColor(24,50), QColor(Qt::red));
        QCOMPARE(red.pixelColor(50,50), QColor(Qt::white)); // outline, no fill
        canvas.setTool(Canvas::Tool::Arrow);
        canvas.setColor(Qt::blue);
        drag(canvas, {50,200}, {250,200});
        const QImage arrow = canvas.image();
        QCOMPARE(arrow.pixelColor(24,50), QColor(Qt::red));
        QCOMPARE(arrow.pixelColor(80,184), QColor(Qt::blue));
        canvas.setTool(Canvas::Tool::Text);
        canvas.addText({184,34}, QStringLiteral("Русский\nText"));
        const QImage annotated = canvas.image();
        QVERIFY(annotated != arrow);
        canvas.undo();
        QCOMPARE(canvas.image(), arrow);
        canvas.redo();
        QCOMPARE(canvas.image(), annotated);
        canvas.undo();
        canvas.addText({200,50}, QStringLiteral("   "));
        QCOMPARE(canvas.image(), arrow);
        QVERIFY(canvas.canRedo()); // cancelled/empty text creates no action
        canvas.undo();
        QCOMPARE(canvas.image(), red);
        canvas.undo();
        QCOMPARE(canvas.image(), base);
    }
    void blurIsExportedAndUndoable() {
        Canvas canvas;
        canvas.resize(232, 232);
        QImage base(200, 200, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 200; ++x)
                base.setPixelColor(x,y, ((x+y)%2) ? Qt::white : Qt::black);
        canvas.setImage(base);
        canvas.show();
        canvas.setTool(Canvas::Tool::Blur);
        drag(canvas, {56,56}, {176,176});
        const QImage blurred = canvas.image();
        QVERIFY(blurred != base);
        for (int y=65; y<135; ++y)
            for (int x=65; x<135; ++x)
                QVERIFY(qAbs(qRed(blurred.pixel(x,y)) - 127) < 4);
        QCOMPARE(blurred.copy(0,0,200,35), base.copy(0,0,200,35));
        canvas.undo();
        QCOMPARE(canvas.image(), base);
        canvas.redo();
        QCOMPARE(canvas.image(), blurred);
        QTemporaryDir dir;
        QString error;
        const QString path=dir.filePath(QStringLiteral("blur.png"));
        QVERIFY2(Capture::save(blurred,path,&error), qPrintable(error));
        QCOMPARE(QImage(path).convertToFormat(blurred.format()), blurred);
        canvas.setTool(Canvas::Tool::Box);
        canvas.setColor(Qt::red);
        drag(canvas,{76,76},{156,156});
        QCOMPARE(canvas.image().pixelColor(60,90), QColor(Qt::red)); // later action stays sharp
        canvas.undo();
        QCOMPARE(canvas.image(), blurred);
    }
    void cropAndScaledCoordinates() {
        Canvas canvas;
        canvas.resize(232,182); // image displayed at half size
        QImage base(400,300,QImage::Format_ARGB32_Premultiplied);
        base.fill(Qt::white);
        canvas.setImage(base);
        canvas.show();
        canvas.setTool(Canvas::Tool::Crop);
        drag(canvas,{41,41},{191,141});
        QCOMPARE(canvas.image(), base); // The crop is still an adjustable draft.
        QTest::keyClick(&canvas, Qt::Key_Return);
        QCOMPARE(canvas.image().size(), QSize(300,200));
        canvas.setTool(Canvas::Tool::Box);
        canvas.setColor(Qt::green);
        drag(canvas,{26,34},{106,94});
        QVERIFY(canvas.image() != base.copy(50,50,300,200));
        canvas.undo();
        QCOMPARE(canvas.image(), base.copy(50,50,300,200));
        canvas.undo();
        QCOMPARE(canvas.image(), base);
        QVERIFY(canvas.canRedo());
        canvas.setImage(base);
        QVERIFY(!canvas.canUndo());
        QVERIFY(!canvas.canRedo());
    }
    void editableArrowsAndBoxes() {
        Canvas canvas;
        canvas.resize(432,332);
        QImage base(400,300,QImage::Format_ARGB32_Premultiplied); base.fill(Qt::white);
        canvas.setImage(base); canvas.show();
        canvas.setTool(Canvas::Tool::Arrow); canvas.setColor(Qt::red);
        drag(canvas,{56,76},{216,76});
        const QImage original=canvas.image();
        // Endpoint drag rotates and extends the arrow, as one undoable edit.
        drag(canvas,{216,76},{276,126});
        const QImage resized=canvas.image();
        QVERIFY(resized!=original);
        canvas.undo(); QCOMPARE(canvas.image(),original);
        canvas.redo(); QCOMPARE(canvas.image(),resized);
        canvas.setTool(Canvas::Tool::Select);
        drag(canvas,{120,90},{140,120});
        const QImage moved=canvas.image();
        QVERIFY(moved!=resized);
        canvas.undo(); QCOMPARE(canvas.image(),resized);
        canvas.setTool(Canvas::Tool::Box); canvas.setColor(Qt::blue);
        drag(canvas,{50,180},{150,250});
        const QImage box=canvas.image();
        drag(canvas,{150,250},{200,280});
        const QImage stretched=canvas.image();
        if (!qEnvironmentVariableIsEmpty("KARYO_SNIP_QA_DIR"))
            canvas.grab().save(QDir(qEnvironmentVariable("KARYO_SNIP_QA_DIR")).filePath(QStringLiteral("box-handles.png")));
        QVERIFY(stretched!=box);
        QCOMPARE(stretched.pixelColor(184,220),QColor(Qt::blue));
        canvas.undo(); QCOMPARE(canvas.image(),box);
        canvas.redo(); QCOMPARE(canvas.image(),stretched);
        // Side handle changes only width; dragging the interior moves it.
        drag(canvas,{200,230},{220,230});
        const QImage side=canvas.image(); QVERIFY(side!=stretched);
        drag(canvas,{100,220},{120,200});
        const QImage translated=canvas.image(); QVERIFY(translated!=side);
        canvas.setColor(Qt::green);
        const QImage recolored=canvas.image(); QVERIFY(recolored!=translated);
        QTest::keyClick(&canvas,Qt::Key_Delete);
        const QImage deleted=canvas.image(); QVERIFY(deleted!=recolored);
        canvas.undo(); QCOMPARE(canvas.image(),recolored);
        canvas.undo(); QCOMPARE(canvas.image(),translated);
    }
    void cropIsAdjustableUntilEnter() {
        Canvas canvas;
        canvas.resize(432,332);
        QImage base(400,300,QImage::Format_ARGB32_Premultiplied);
        for (int y=0;y<300;++y) for (int x=0;x<400;++x) base.setPixelColor(x,y,QColor(x%256,y%256,100));
        canvas.setImage(base); canvas.show(); canvas.setTool(Canvas::Tool::Crop);
        drag(canvas,{56,56},{216,156});
        QCOMPARE(canvas.image(),base); QVERIFY(!canvas.canUndo());
        drag(canvas,{216,156},{256,196}); // bottom-right handle
        drag(canvas,{120,100},{140,110}); // move the crop
        if (!qEnvironmentVariableIsEmpty("KARYO_SNIP_QA_DIR"))
            canvas.grab().save(QDir(qEnvironmentVariable("KARYO_SNIP_QA_DIR")).filePath(QStringLiteral("crop-handles.png")));
        QCOMPARE(canvas.image(),base);
        QTest::keyClick(&canvas,Qt::Key_Return);
        QCOMPARE(canvas.image(),base.copy(60,50,200,140));
        canvas.undo(); QCOMPARE(canvas.image(),base);
        canvas.redo(); QCOMPARE(canvas.image(),base.copy(60,50,200,140));
        canvas.undo();
        drag(canvas,{56,56},{216,156});
        QTest::keyClick(&canvas,Qt::Key_Escape);
        QTest::keyClick(&canvas,Qt::Key_Return);
        QCOMPARE(canvas.image(),base); QVERIFY(!canvas.canUndo());
    }
    void inlineTextAcceptCancelAndReedit() {
        Canvas canvas;
        canvas.resize(432,332);
        QImage base(400,300,QImage::Format_ARGB32_Premultiplied); base.fill(Qt::white);
        canvas.setImage(base); canvas.show(); canvas.setTool(Canvas::Tool::Text);
        QTest::mouseClick(&canvas,Qt::LeftButton,{}, {50,50});
        auto *editor=canvas.findChild<QPlainTextEdit *>(QStringLiteral("inlineTextEditor"));
        QVERIFY(editor && editor->isVisible());
        QTest::keyClicks(editor,"Cancelled"); QTest::keyClick(editor,Qt::Key_Escape);
        QCOMPARE(canvas.image(),base); QVERIFY(!canvas.canUndo());
        QTest::mouseClick(&canvas,Qt::LeftButton,{}, {50,50});
        QInputMethodEvent ime;
        ime.setCommitString(QStringLiteral("Привет")); QApplication::sendEvent(editor,&ime);
        QTest::keyClick(editor,Qt::Key_Return,Qt::ShiftModifier);
        QTest::keyClicks(editor,"Second line");
        QCOMPARE(editor->toPlainText(),QStringLiteral("Привет\nSecond line"));
        if (!qEnvironmentVariableIsEmpty("KARYO_SNIP_QA_DIR"))
            canvas.grab().save(QDir(qEnvironmentVariable("KARYO_SNIP_QA_DIR")).filePath(QStringLiteral("inline-text.png")));
        QTest::keyClick(editor,Qt::Key_Return);
        QVERIFY(!editor->isVisible());
        const QImage text=canvas.image(); QVERIFY(text!=base);
        QTest::mouseDClick(&canvas,Qt::LeftButton,{}, {60,60});
        QVERIFY(editor->isVisible());
        QCOMPARE(editor->toPlainText(),QStringLiteral("Привет\nSecond line"));
        QTest::keyClicks(editor,"Changed"); QTest::keyClick(editor,Qt::Key_Return);
        const QImage edited=canvas.image(); QVERIFY(edited!=text);
        canvas.undo(); QCOMPARE(canvas.image(),text);
        canvas.redo(); QCOMPARE(canvas.image(),edited);
        canvas.undo(); canvas.undo(); QCOMPARE(canvas.image(),base);
    }
    void mixedScaleAndNegativeScreenOrigins() {
        QImage left(100,100,QImage::Format_ARGB32_Premultiplied); left.fill(Qt::red);
        QImage right(200,200,QImage::Format_ARGB32_Premultiplied); right.fill(Qt::blue);
        const QList<Capture::Frame> frames={{{-100,0,100,100},left},{{0,0,100,100},right}};
        const QImage joined=Capture::slice(frames,{-50,25,100,50});
        QCOMPARE(joined.size(), QSize(200,100));
        QCOMPARE(joined.pixelColor(40,40), QColor(Qt::red));
        QCOMPARE(joined.pixelColor(140,40), QColor(Qt::blue));
        QCOMPARE(Capture::slice(frames,{-200,0,120,40}).size(), QSize(20,40));
        QVERIFY(Capture::slice(frames,{200,200,50,50}).isNull());
    }
};
QTEST_MAIN(CanvasTest)
#include "canvastest.moc"
