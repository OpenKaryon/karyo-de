#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Run as an unprivileged disposable test user with installed session packages.
# Set KARYO_TEST_PASSWORD to that user's fixture password; never use a real account.
set -eu
: "${XDG_RUNTIME_DIR:?disposable runtime directory required}"
: "${KARYO_TEST_PASSWORD:?fixture password required}"
: "${KARYO_TEST_ARTIFACTS:?writable artifact directory required}"
export WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1
export LIBSEAT_BACKEND=noop LANG=C.UTF-8
export XDG_SESSION_TYPE=wayland
karyo-session --check
karyo-session &
session_pid=$!
export LABWC_PID=$session_pid
trap 'kill "$session_pid" 2>/dev/null || true' EXIT
n=0
until [ -S "$XDG_RUNTIME_DIR/wayland-0" ]; do
    n=$((n+1)); [ "$n" -lt 100 ] || exit 1
    sleep .1
done
export WAYLAND_DISPLAY=wayland-0
sleep 3
karyo-shell --start
sleep 1
grim "$KARYO_TEST_ARTIFACTS"/start.png
karyo-shell --start
: > "$KARYO_TEST_ARTIFACTS"/lock.log
karyo-lock >"$KARYO_TEST_ARTIFACTS"/lock.log 2>&1 &
lock_pid=$!
n=0
until grep -q KARYO_LOCKED "$KARYO_TEST_ARTIFACTS"/lock.log; do
    kill -0 "$lock_pid"
    n=$((n+1)); [ "$n" -lt 100 ] || exit 1
    sleep .1
done
kill -0 "$lock_pid"
grim "$KARYO_TEST_ARTIFACTS"/locked.png || true
wtype 'wrong-password' -k Return
sleep 3
kill -0 "$lock_pid"
grim "$KARYO_TEST_ARTIFACTS"/wrong-password.png || true
wtype "$KARYO_TEST_PASSWORD" -k Return
n=0
while kill -0 "$lock_pid" 2>/dev/null; do
    n=$((n+1)); [ "$n" -lt 100 ] || exit 1
    sleep .1
done
wait "$lock_pid"
grim "$KARYO_TEST_ARTIFACTS"/unlocked.png
karyo-shell --start
karyo-shell --start
labwc --exit
wait "$session_pid"
echo 'PASS real labwc session, Start, PAM rejection, successful unlock and logout'
