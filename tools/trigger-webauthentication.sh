#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
#
# Call the public WebAuthentication interface through xdg-desktop-portal; see docs/TESTING.md.

set -u

BUS="${BUS:---session}"          # --session (default) or --system
DEST=org.freedesktop.portal.Desktop
PATH_=/org/freedesktop/portal/desktop
IFACE=org.freedesktop.portal.experimental.WebAuthentication1

# The frontend validates URI arguments before activating a backend.
START_URI="${START_URI:-https://example.invalid/authorize?client_id=placeholder&response_type=code}"
COMPLETION_URI="${COMPLETION_URI:-https://example.invalid/oauth2/nativeclient}"

usage() {
	cat <<EOF
Usage: ${0##*/} [command]

Commands:
  version        read the exported interface version
  introspect     list the interfaces on /org/freedesktop/portal/desktop
  start          Start(s parent_window, s start_uri, s completion_uri, a{sv})
                 returns a Request handle; Response carries completion_uri
  reject-scheme  a start_uri that is not https; expect InvalidArgument
  reject-mode    submit an unknown session_mode; expect InvalidArgument
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
	# Subscribe before Start because completion arrives through the Request Response signal.
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
		grep -i 'interface org.freedesktop.portal' ||
		echo "no portal interfaces exported on $PATH_"
}

cmd_start() {
	# The title is untrusted application text.
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
