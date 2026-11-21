#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
: "${DBUS_SESSION_BUS_ADDRESS:?Run under dbus-run-session}"
export DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"
exec "$@"
