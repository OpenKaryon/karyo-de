// SPDX-License-Identifier: GPL-3.0-or-later
#include <karyo/ui/window.h>
#include <karyo/ui/password.h>
#include <karyo/ui/navigation.h>
#include <QApplication>
#include <QLineEdit>
#include <QVBoxLayout>

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    Karyo::Ui::KaryoWindow window;
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    auto *navigation = new Karyo::Ui::NavList;
    navigation->addItem(QStringLiteral("home"), QStringLiteral("Home"));
    auto *password = new Karyo::Ui::PasswordField;
    layout->addWidget(navigation);
    layout->addWidget(password);
    window.setCentralWidget(content);
    window.resize(480, 320);
    window.show();
    application.processEvents();
    return window.grab().isNull() || password->lineEdit()->echoMode() != QLineEdit::Password;
}
