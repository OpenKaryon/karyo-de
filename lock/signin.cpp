// SPDX-License-Identifier: GPL-3.0-or-later
#include "signin.h"

#include <pwd.h>

namespace SignIn {

QString fullName(const passwd *pw)
{
    const QString name = QString::fromLocal8Bit(pw->pw_gecos).section(QLatin1Char(','), 0, 0).trimmed();
    return name == QString::fromLocal8Bit(pw->pw_name) ? QString() : name;
}

} // namespace SignIn
