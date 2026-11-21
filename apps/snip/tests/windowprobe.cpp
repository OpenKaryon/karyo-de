// SPDX-License-Identifier: GPL-3.0-or-later
#include "windowsnapshot.h"
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QWidget>
#include <cstdio>

class Fixture : public QWidget {
public:
    using QWidget::QWidget;
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#edf1f5"));
        p.fillRect(QRect(30,30,260,55), QColor("#276bc7"));
        p.setPen(Qt::white);
        p.setFont(QFont(QStringLiteral("sans"),20));
        p.drawText(QRect(40,30,240,55),Qt::AlignVCenter,QStringLiteral("Karyo screenshot QA"));
        p.setPen(QColor("#1f2937"));
        p.drawText(QPoint(30,130),QStringLiteral("Русский текст · arrows · blur"));
        for (int x=0;x<150;++x)
            p.fillRect(30+x,165,1,50, x%2 ? Qt::black : Qt::white);
    }
};
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    app.setDesktopFileName(QStringLiteral("karyo-snip-fixture"));
    if (app.arguments().contains(QStringLiteral("--dump"))) {
        QList<CaptureWindow> windows;
        QString error;
        if (!windowSnapshot(&windows,&error)) {
            std::fprintf(stderr,"%s\n",qPrintable(error)); return 1;
        }
        QJsonArray array;
        for (const auto &window: windows)
            array.append(QJsonObject{{"title",window.title},{"x",window.geometry.x()},{"y",window.geometry.y()},
                                     {"width",window.geometry.width()},{"height",window.geometry.height()}});
        std::puts(QJsonDocument(array).toJson(QJsonDocument::Compact).constData());
        return 0;
    }
    Fixture window;
    window.setWindowTitle(app.arguments().value(1,QStringLiteral("Karyo screenshot QA")));
    window.resize(520,360);
    window.show();
    return app.exec();
}
