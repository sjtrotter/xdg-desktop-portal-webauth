#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
#
# Adapted from the sibling backend xdg-desktop-portal-certificate's
# tools/ui-smoke.sh (same author, same licence).
#
# Run tools/dev-stack.sh under Xvfb; --cancel sends Escape to the sign-in window.
# Other options pass to dev-stack.sh; arguments after -- pass to webauth-e2e.py.
# XVFB and XDOTOOL select executables; SCREEN and WINDOW_TITLE select the display and window.
# Requires a built frontend and tools/softhsm-fixture.sh fixture; see docs/TESTING.md.

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

# Xvfb and xdotool require the X11 backend.
export DISPLAY="$SCREEN"
unset WAYLAND_DISPLAY
export GDK_BACKEND=x11
export GTK_A11Y=none
# Keep fixture window appearance independent of desktop settings.
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
		# Without a window manager, focus must be assigned directly before sending the key.
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
