// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sampler.h"
namespace ProcessActions {
bool signal(const ProcessInfo &process, int signal, QString *error);
bool efficiency(const ProcessInfo &process, QString *error);
}
