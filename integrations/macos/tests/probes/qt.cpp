// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (app.arguments().size() != 2) return 2;
    const QString report = app.arguments().at(1);
    QWidget window;
    window.setWindowTitle(QStringLiteral("Karyo Qt — Привет"));
    auto *layout = new QVBoxLayout(&window);
    auto *label = new QLabel(QStringLiteral("Привет из настоящего Qt macOS"), &window);
    auto *button = new QPushButton(QStringLiteral("Проверить кнопку"), &window);
    layout->addWidget(label);
    layout->addWidget(button);
    window.resize(460, 220);
    bool clicked = false;
    QObject::connect(button, &QPushButton::clicked, [&] { clicked = true; });
    window.show();
    QTimer::singleShot(500, button, &QPushButton::click);
    QTimer::singleShot(4000, [&] {
        const bool visible = window.isVisible();
        const bool text = label->text() == QStringLiteral("Привет из настоящего Qt macOS");
        const QImage image = window.grab().toImage();
        const bool raster = !image.isNull() && image.save(report + QStringLiteral(".png"));
        const bool passed = clicked && visible && text && raster;
        QJsonObject record{{"schema", 1}, {"probe", "qt-widgets"}, {"target", "macos"},
            {"architecture", "x86_64"}, {"qtVersion", qVersion()}, {"passed", passed},
            {"windowVisible", visible}, {"buttonSignal", clicked}, {"textUtf8", text},
            {"widgetRasterRender", raster}};
        QFile output(report);
        const QByteArray bytes = QJsonDocument(record).toJson();
        const bool saved = output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size();
        output.close();
        app.exit(passed && saved ? 0 : 1);
    });
    return app.exec();
}
