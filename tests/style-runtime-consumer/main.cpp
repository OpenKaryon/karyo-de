// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QPushButton>
#include <QStyle>

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    if (application.style()->objectName().compare(QStringLiteral("Karyo"), Qt::CaseInsensitive))
        return 1;
    QPushButton button(QStringLiteral("Connect"));
    button.resize(200, 40);
    button.show();
    application.processEvents();
    return button.grab().isNull();
}
