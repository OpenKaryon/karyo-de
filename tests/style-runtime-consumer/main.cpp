// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QPushButton>
#include <QStyle>
#include <QTemporaryFile>
#include <QDir>
#include <QIcon>

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    if (application.style()->objectName().compare(QStringLiteral("Karyo"), Qt::CaseInsensitive))
        return 1;
    QTemporaryFile iconFile(QDir::tempPath() + QStringLiteral("/karyo-native-XXXXXX.svg"));
    if (!iconFile.open())
        return 2;
    iconFile.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\">"
                   "<rect width=\"16\" height=\"16\" fill=\"green\"/></svg>");
    iconFile.flush();
    if (QIcon(iconFile.fileName()).pixmap(16, 16).isNull())
        return 3;
    QPushButton button(QStringLiteral("Connect"));
    button.resize(200, 40);
    button.show();
    application.processEvents();
    return button.grab().isNull();
}
