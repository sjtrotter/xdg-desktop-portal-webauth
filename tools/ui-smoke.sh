#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
#
# Adapted from the sibling backend xdg-desktop-portal-certificate's
# tools/ui-smoke.sh (same author, same licence).
#
# ui-smoke.sh -- the whole thing, WINDOW INCLUDED, with nobody at the keyboard.
#
# It puts a headless X server round tools/dev-stack.sh. That is all it needs to
# do for the ordinary flows: the fixture identity provider redirects by itself,
# so the sign-in window is driven by the server rather than by a user, and the
# transaction ends when the completion URI is reached. What still needs a
# keyboard is CANCELLING, and --cancel is that: it waits for the window and
# sends Escape.
#
# THIS IS THE ONLY AUTOMATED TEST THAT OPENS THE WINDOW. backend/tests covers the
# rules with no display; this covers everything between a D-Bus call and a
# rendered page, which is where the interesting mistakes live.
#
# It is NOT a substitute for docs/TESTING.md. A software token in a headless X
# server is a rehearsal: no reader, no PIN retry counter to spend, and nothing to
# pull out mid-handshake.
#
# WHAT IT NEEDS, none of which has to be installed system wide:
#
#   Xvfb        xorg-x11-server-Xvfb           $XVFB
#   xdotool     xdotool (only for --cancel)    $XDOTOOL
#   a fixture from tools/softhsm-fixture.sh
#   a built frontend, as tools/dev-stack.sh describes
#
#     tools/ui-smoke.sh                       # plain https, no certificates
#     tools/ui-smoke.sh --mtls                # the server demands a client certificate
#     tools/ui-smoke.sh --cancel              # Escape, expecting response 1
#     tools/ui-smoke.sh --cookie --session-mode=ephemeral -- --expect-cookie no
#
# Everything after `--` goes to tools/webauth-e2e.py; everything before it that
# this script does not recognise goes to tools/dev-stack.sh.

set -u

here() { cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd; }
REPO="$(here)"

XVFB="${XVFB:-$(command -v Xvfb || true)}"
XDOTOOL="${XDOTOOL:-$(command -v xdotool || true)}"
SCREEN="${SCREEN:-:92}"
WINDOW_TITLE="${WINDOW_TITLE:-Web sign-in}"

die() {
	echo "${0##*/}: $*" >&2
	exit 40
}

# shellcheck source=tools/lib.sh
. "$REPO/tools/lib.sh"

if [ -n "${LOGDIR:-}" ]; then
	fixture_make "$LOGDIR" ui-smoke
else
	LOGDIR="$(fixture_mktemp xdp-webauth-ui-smoke ui-smoke)"
fi

CANCEL=0
STACK_ARGS=()
E2E_ARGS=()

while [ $# -gt 0 ]; do
	case "$1" in
	--cancel)
		CANCEL=1
		shift
		;;
	--)
		shift
		E2E_ARGS=("$@")
		break
		;;
	*)
		STACK_ARGS+=("$1")
		shift
		;;
	esac
done

[ -n "$XVFB" ] || die "Xvfb not found; set \$XVFB"
[ "$CANCEL" = 1 ] && [ -z "$XDOTOOL" ] && die "xdotool not found; set \$XDOTOOL"

# --cancel drives the window instead of letting the flow finish, so the
# expectation changes with it.
if [ "$CANCEL" = 1 ] && [ "${#E2E_ARGS[@]}" -eq 0 ]; then
	E2E_ARGS=(--expect-response 1 --expect-reason user_cancelled)
fi

"$XVFB" "$SCREEN" -screen 0 1280x1024x24 -nolisten tcp >"$LOGDIR/xvfb.log" 2>&1 &
XVFB_PID=$!
sleep 2

cleanup() {
	kill "$XVFB_PID" 2>/dev/null
	wait "$XVFB_PID" 2>/dev/null
}
trap cleanup EXIT

# X11 rather than Wayland, because a headless X server is something a machine
# with no compositor can start and xdotool can drive.
export DISPLAY="$SCREEN"
unset WAYLAND_DISPLAY
export GDK_BACKEND=x11
export GTK_A11Y=none
# The window's theme must not depend on what the machine running the test has its
# desktop set to.
export ADW_DEBUG_COLOR_SCHEME="${ADW_DEBUG_COLOR_SCHEME:-prefer-dark}"
export KEEP_LOGS=1

"$REPO/tools/dev-stack.sh" "${STACK_ARGS[@]}" -- "${E2E_ARGS[@]}" >"$LOGDIR/stack.log" 2>&1 &
STACK=$!

if [ "$CANCEL" = 1 ]; then
	wid=""
	for _ in $(seq 1 60); do
		wid="$("$XDOTOOL" search --name "$WINDOW_TITLE" 2>/dev/null | tail -1)"
		[ -n "$wid" ] && break
		kill -0 "$STACK" 2>/dev/null || break
		sleep 0.5
	done

	if [ -n "$wid" ]; then
		echo "${0##*/}: driving '$WINDOW_TITLE' with Escape"
		# There is no window manager, so focus is set directly and the key goes
		# through XTEST to whatever has it.
		"$XDOTOOL" windowfocus "$wid" 2>/dev/null
		sleep 1
		"$XDOTOOL" key Escape
	else
		echo "${0##*/}: no window titled '$WINDOW_TITLE' appeared"
	fi
fi

wait "$STACK"
rc=$?

cat "$LOGDIR/stack.log"
echo
echo "${0##*/}: dev-stack.sh exited $rc; logs in $LOGDIR"
exit "$rc"
