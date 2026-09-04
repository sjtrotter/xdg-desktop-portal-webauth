#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# trigger-webauthentication.sh -- poke the PUBLIC WebAuthentication portal
# interface.
#
# This calls org.freedesktop.portal.experimental.WebAuthentication on
# org.freedesktop.portal.Desktop at /org/freedesktop/portal/desktop -- the
# frontend, never this repository's backend. An application (entra-token-helper,
# say) would do exactly this, and it is the only way to exercise the backend the
# way it is meant to be exercised.
#
# The interface only exists if the running xdg-desktop-portal was started with
#     XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication
# (or "all"). With the gate off the interface is not exported at all and every
# call below fails with "no such interface" -- which is the intended behaviour,
# and is exactly the case entra-token-helper reports as exit 40.
#
# It defaults to --session-bus, i.e. whatever DBUS_SESSION_BUS_ADDRESS points
# at. To keep the real desktop out of it, run the whole thing on a private bus:
#
#     dbus-run-session -- tools/trigger-webauthentication.sh
#
# ...but a private bus has no portal on it, so in practice use tools/dev-stack.sh,
# which starts a frontend and a backend on a private bus and then runs this.
#
# THE URIs BELOW ARE PLACEHOLDERS. They point at example.invalid, which by RFC
# 6761 can never resolve, so nothing here can reach a real identity provider by
# accident. Pass your own with START_URI and COMPLETION_URI.
#
# Method and argument shapes are taken from
# data/org.freedesktop.portal.experimental.WebAuthentication.xml on the
# xdg-desktop-portal branch experimental/certificate-webauthentication (commit
# 3a32e9b) and from that branch's tests/test_webauthentication.py.

set -u

BUS="${BUS:---session}"          # --session (default) or --system
DEST=org.freedesktop.portal.Desktop
PATH_=/org/freedesktop/portal/desktop
IFACE=org.freedesktop.portal.experimental.WebAuthentication

# start_uri must be absolute https with a host. completion_uri must be absolute
# with a host and no userinfo. Both are validated in the frontend before any
# backend is woken; a malformed one is a D-Bus error out of Start().
START_URI="${START_URI:-https://example.invalid/authorize?client_id=placeholder&response_type=code}"
COMPLETION_URI="${COMPLETION_URI:-https://example.invalid/oauth2/nativeclient}"

usage() {
	cat <<EOF
Usage: ${0##*/} [command]

Commands:
  version        read the interface's version property (works with no backend
                 running, as long as the interface is exported)
  introspect     list the experimental interfaces the portal exports
  start          Start(s parent_window, s start_uri, s completion_uri, a{sv})
                 -> o handle. A Request: the completion_uri comes back in the
                 Response signal on that handle, not as a return value.
  reject-scheme  a start_uri that is not https; expect InvalidArgument
  reject-mode    an unknown session_mode; expect InvalidArgument, never a
                 silent fallback to "shared"
  monitor        watch Request and WebAuthentication signals; exit on Ctrl-C
  all            monitor in the background, then version + start + both
                 rejection cases

Environment:
  BUS              --session (default) or --system
  START_URI        default: $START_URI
  COMPLETION_URI   default: $COMPLETION_URI
EOF
}

monitor_bg() {
	# The answer to Start() arrives as a Response signal on the Request object,
	# not as the method's return value, so something has to be watching before
	# the call is made.
	gdbus monitor "$BUS" --dest "$DEST" &
	MONITOR_PID=$!
	sleep 1
}

cmd_version() {
	gdbus call "$BUS" --dest "$DEST" --object-path "$PATH_" \
		--method org.freedesktop.DBus.Properties.Get \
		"$IFACE" version
}

cmd_introspect() {
	gdbus introspect "$BUS" --dest "$DEST" --object-path "$PATH_" |
		grep -i experimental || echo "no experimental interfaces exported"
}

cmd_start() {
	# session_mode is "shared" or "ephemeral" and nothing else; timeout is
	# clamped to 900 by the frontend; title is untrusted application text and is
	# rendered as such, never as the identity of the window.
	gdbus call "$BUS" --dest "$DEST" --object-path "$PATH_" \
		--method "$IFACE".Start \
		"" "$START_URI" "$COMPLETION_URI" \
		"{'handle_token': <'wa1'>, \
'session_mode': <'ephemeral'>, \
'title': <'Sign in (placeholder)'>, \
'timeout': <uint32 120>}"
}

cmd_reject_scheme() {
	gdbus call "$BUS" --dest "$DEST" --object-path "$PATH_" \
		--method "$IFACE".Start \
		"" "http://example.invalid/start" "$COMPLETION_URI" "{}"
}

cmd_reject_mode() {
	gdbus call "$BUS" --dest "$DEST" --object-path "$PATH_" \
		--method "$IFACE".Start \
		"" "$START_URI" "$COMPLETION_URI" "{'session_mode': <'private'>}"
}

main() {
	local cmd="${1:-all}"
	case "$cmd" in
	version) cmd_version ;;
	introspect) cmd_introspect ;;
	start) cmd_start ;;
	reject-scheme) cmd_reject_scheme ;;
	reject-mode) cmd_reject_mode ;;
	monitor) gdbus monitor "$BUS" --dest "$DEST" ;;
	all)
		monitor_bg
		trap 'kill "$MONITOR_PID" 2>/dev/null' EXIT
		echo "== version"; cmd_version
		echo "== Start (watch the monitor for the Response carrying completion_uri)"
		cmd_start
		sleep 2
		echo "== rejection: start_uri is not https (expect InvalidArgument)"
		cmd_reject_scheme
		echo "== rejection: unknown session_mode (expect InvalidArgument)"
		cmd_reject_mode
		;;
	-h | --help | help) usage ;;
	*)
		echo "${0##*/}: unknown command '$cmd'" >&2
		usage >&2
		exit 64
		;;
	esac
}

main "$@"
