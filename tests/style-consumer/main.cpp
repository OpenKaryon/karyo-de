// SPDX-License-Identifier: GPL-3.0-or-later
#include <karyo/ui/style.h>
#include <karyo/ui/window.h>
#include <karyo/ui/password.h>
#include <QApplication>
#include <QLineEdit>

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    Karyo::Ui::applyStyle();
    Karyo::Ui::KaryoWindow window;
    window.setCentralWidget(new Karyo::Ui::PasswordField);
    window.resize(480, 240);
    window.show();
    application.processEvents();
    return application.style()->objectName().compare(QStringLiteral("Karyo"), Qt::CaseInsensitive)
        || window.grab().isNull();
}
