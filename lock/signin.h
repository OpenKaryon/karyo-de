// SPDX-License-Identifier: GPL-3.0-or-later
// Helpers shared by the lock screen and the greeter.
#pragma once

#include <QCoreApplication>
#include <QString>

struct passwd;

// Translatable strings outside of widgets, in the "SignIn" context.
class SignInText
{
    Q_DECLARE_TR_FUNCTIONS(SignIn)
};

namespace SignIn {

// The first GECOS field: "Display Name,,," -> "Display Name"; empty
// when it only repeats the login.
QString fullName(const passwd *pw);

} // namespace SignIn

int runLock();
