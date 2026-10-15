#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
#
# Adapted from the sibling backend xdg-desktop-portal-certificate's
# tools/dev-stack.sh (same author, same licence).
#
# Usage: tools/dev-stack.sh [options] [-- e2e-options]
# Run a fixture sign-in on a private D-Bus session with an existing display.
#
#   --live                     use the session bus and replace the system portal
#   --keep                     wait before shutting down the stack
#   --no-e2e                   start the stack without running the client
#   --mtls                     require a fixture client certificate
#   --cookie                   set and report the fixture cookie
#   --cookie-value=VALUE       set the fixture cookie value (default: one)
#   --session-mode=MODE        request shared or ephemeral storage; accepts a separate value
#   --completion-uri=URI       set the completion URI (default: https://example.invalid/cb)
#   --as-app=ID                identify the client through a cgroup and desktop file
#   --start-path=PATH          set the fixture start path (default: /start)
#   --data-home=PATH           reuse this data directory across runs
#   --expect-backend-log=ERE   require an extended regular expression match in the backend log
#   -h, --help                 show this help
#
# XDP_BUILD selects the experimental/integration frontend build.
# BACKEND selects the backend binary; XDP_ENV selects an environment file to source.
# SOFTHSM_DIR selects the tools/softhsm-fixture.sh fixture.
# The frontend needs WebAuthentication1 in XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL.
# Live mode holds org.freedesktop.portal.Desktop until exit; D-Bus activation restores it on demand.
# See docs/TESTING.md for setup and hardware-token tests.

set -u

here() { cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd; }
REPO="$(here)"

XDP_BUILD="${XDP_BUILD:-$REPO/../xdg-desktop-portal/build}"
BACKEND="${BACKEND:-$REPO/build/src/xdg-desktop-portal-webauth}"
XDP_ENV="${XDP_ENV:-$REPO/.xdp-env}"
SOFTHSM_DIR="${SOFTHSM_DIR:-${TMPDIR:-/tmp}/xdp-webauth-softhsm}"

FRONTEND_BIN="$XDP_BUILD/desktop-portal/xdg-desktop-portal"
PERMSTORE_BIN="$XDP_BUILD/document-portal/xdg-permission-store"

MODE=private
KEEP=0
RUN_E2E=1
MTLS=0
COOKIE=0
COOKIE_VALUE=one
SESSION_MODE=
COMPLETION_URI="https://example.invalid/cb"
AS_APP=
START_PATH="/start"
EXPECT_BACKEND=
DATA_HOME=
E2E_ARGS=()

die() {
	echo "${0##*/}: $*" >&2
	exit 40
}

# shellcheck source=tools/lib.sh
. "$REPO/tools/lib.sh"

if [ -n "${DEVDIR:-}" ]; then
	fixture_make "$DEVDIR" dev-stack
else
	DEVDIR="$(fixture_mktemp xdp-webauth-dev dev-stack)"
fi

usage() {
	sed -n '8,30p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
	exit 0
}

parse_args() {
	while [ $# -gt 0 ]; do
		case "$1" in
		--keep) KEEP=1 ;;
		--live) MODE=live ;;
		--no-e2e) RUN_E2E=0 ;;
		--mtls) MTLS=1 ;;
		--cookie) COOKIE=1 ;;
		--cookie-value=*) COOKIE_VALUE="${1#--cookie-value=}" ;;
		--session-mode=*) SESSION_MODE="${1#--session-mode=}" ;;
		--session-mode)
			shift
			SESSION_MODE="${1:-}"
			;;
		--completion-uri=*) COMPLETION_URI="${1#--completion-uri=}" ;;
		--as-app=*) AS_APP="${1#--as-app=}" ;;
		--start-path=*) START_PATH="${1#--start-path=}" ;;
		--data-home=*) DATA_HOME="${1#--data-home=}" ;;
		--expect-backend-log=*) EXPECT_BACKEND="${1#--expect-backend-log=}" ;;
		-h | --help) usage ;;
		--)
			shift
			E2E_ARGS=("$@")
			return
			;;
		*) die "unknown option '$1'; try --help" ;;
		esac
		shift
	done
}

preflight() {
	# shellcheck disable=SC1090
	[ -n "$XDP_ENV" ] && [ -f "$XDP_ENV" ] && . "$XDP_ENV"

	command -v dbus-run-session >/dev/null || die "dbus-run-session not found (dbus-daemon package)"
	command -v python3 >/dev/null || die "python3 not found"
	python3 -c 'import gi' 2>/dev/null || die "python3-gobject not found (the e2e client needs it)"

	[ -x "$FRONTEND_BIN" ] || die "no frontend at $FRONTEND_BIN; set XDP_BUILD"
	[ -x "$BACKEND" ] || die "no backend at $BACKEND; run 'meson setup build && ninja -C build' or set BACKEND"

	fixture_check "$SOFTHSM_DIR" softhsm
	[ -f "$SOFTHSM_DIR/server.pem" ] ||
		die "no fixture at $SOFTHSM_DIR; run tools/softhsm-fixture.sh first"

	[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] ||
		die "Display unavailable; tools/ui-smoke.sh supplies Xvfb."

	if ldd "$FRONTEND_BIN" 2>/dev/null | grep -q 'not found'; then
		echo "${0##*/}: the frontend cannot resolve its libraries:" >&2
		ldd "$FRONTEND_BIN" | grep 'not found' >&2
		die "set XDP_ENV to a file exporting LD_LIBRARY_PATH for libdex (docs/TESTING.md)"
	fi
}

write_devdir() {
	xdp_write_portal_dir "$DEVDIR" "$REPO" "$MODE"

	echo "${0##*/}: dev portal dir $DEVDIR"
	echo "--- webauth.portal"
	sed 's/^/    /' "$DEVDIR/webauth.portal"
	echo "--- portals.conf"
	sed 's/^/    /' "$DEVDIR/portals.conf"
	echo
}

start_server() {
	local args=(
		--cert "$SOFTHSM_DIR/server.pem"
		--key "$SOFTHSM_DIR/server.key"
		--completion-uri "$COMPLETION_URI"
		--access-log "$DEVDIR/access.log"
		--port-file "$DEVDIR/port"
	)

	[ "$MTLS" = 1 ] && args+=(--require-client-cert --ca "$SOFTHSM_DIR/ca.pem")
	[ "$COOKIE" = 1 ] && args+=(--cookie --cookie-value "$COOKIE_VALUE")

	rm -f "$DEVDIR/port" "$DEVDIR/access.log"
	python3 "$REPO/tools/mtls-server.py" "${args[@]}" >"$DEVDIR/server.log" 2>&1 &
	SERVER_PID=$!

	for _ in $(seq 1 40); do
		[ -s "$DEVDIR/port" ] && break
		kill -0 "$SERVER_PID" 2>/dev/null || die "the fixture server exited: $(cat "$DEVDIR/server.log")"
		sleep 0.25
	done

	PORT="$(cat "$DEVDIR/port" 2>/dev/null)"
	[ -n "$PORT" ] || die "the fixture server never reported a port"
	echo "${0##*/}: fixture server on port $PORT (client certificates: $([ "$MTLS" = 1 ] && echo required || echo off))"
}

start_stack() {
	local experimental
	experimental="$(xdp_experimental_portals WebAuthentication1)"
	export XDG_DESKTOP_PORTAL_DIR="$DEVDIR"

	if [ "$MODE" = private ]; then
		export XDG_CURRENT_DESKTOP="${XDG_CURRENT_DESKTOP:-dev}"
		"$PERMSTORE_BIN" >"$DEVDIR/permission-store.log" 2>&1 &
		PERM_PID=$!
		sleep 1
	else
		PERM_PID=""
	fi

	# Start the frontend before GTK can activate the system portal through Settings.
	FRONTEND_LOG="$DEVDIR/frontend.log"
	if [ "$MODE" = live ]; then
		XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL="$experimental" \
			"$FRONTEND_BIN" -r -v >"$FRONTEND_LOG" 2>&1 &
	else
		XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL="$experimental" \
			"$FRONTEND_BIN" -v >"$FRONTEND_LOG" 2>&1 &
	fi
	FRONTEND_PID=$!

	xdp_wait_for_name org.freedesktop.portal.Desktop "$FRONTEND_PID" || {
		tail -20 "$FRONTEND_LOG" >&2
		die "the frontend never took org.freedesktop.portal.Desktop"
	}

	"$BACKEND" "${BACKEND_ARGS[@]}" >"$DEVDIR/backend.log" 2>&1 &
	BACKEND_PID=$!

	xdp_wait_for_name org.freedesktop.impl.portal.desktop.webauth "$BACKEND_PID" || {
		tail -20 "$DEVDIR/backend.log" >&2
		die "the backend never took org.freedesktop.impl.portal.desktop.webauth"
	}
}

check_frontend_log() {
	local configured="no" provided="no" i

	for i in $(seq 1 20); do
		grep -q "in configuration for org.freedesktop.impl.portal.experimental.WebAuthentication1" \
			"$FRONTEND_LOG" 2>/dev/null && configured="yes"
		grep -q "Providing portal org.freedesktop.portal.experimental.WebAuthentication1" \
			"$FRONTEND_LOG" 2>/dev/null && provided="yes"
		[ "$configured" = yes ] && [ "$provided" = yes ] && break
		sleep 0.25
	done

	if [ "$configured" = yes ] && [ "$provided" = yes ]; then
		echo "${0##*/}: frontend: web authentication portal provided"
	else
		echo "${0##*/}: frontend: web authentication portal unavailable (configured=$configured provided=$provided; see $FRONTEND_LOG)"
	fi
}

# The completion URI carries the authorization code and must never appear in the server access log.
check_completion_was_not_fetched() {
	local path

	path="$(python3 -c 'import sys,urllib.parse;print(urllib.parse.urlsplit(sys.argv[1]).path)' \
		"$COMPLETION_URI")"

	if grep -q " $path\$" "$DEVDIR/access.log" 2>/dev/null; then
		echo "${0##*/}: FAIL the fixture server was asked for $path"
		return 1
	fi

	echo "${0##*/}: Completion URI absent from server access log: $path"
	return 0
}

APP_CGROUP=""

# The frontend derives host identity from an app-<id>-<random>.scope cgroup and a matching desktop file.
make_app_scope() {
	local root="/sys/fs/cgroup/user.slice/user-$(id -u).slice/user@$(id -u).service/app.slice"

	[ -n "$AS_APP" ] || return 0
	[ -d "$root" ] || {
		echo "${0##*/}: no delegated cgroup at $root; --as-app needs a systemd user session"
		AS_APP=""
		return 0
	}

	APP_CGROUP="$root/app-$AS_APP-$$.scope"
	mkdir -p "$APP_CGROUP" 2>/dev/null || {
		echo "${0##*/}: could not create $APP_CGROUP; --as-app disabled"
		APP_CGROUP=""
		AS_APP=""
		return 0
	}

	# The frontend requires a matching desktop file to accept the scope identity.
	mkdir -p "$DEVDIR/share/applications"
	cat >"$DEVDIR/share/applications/$AS_APP.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=$AS_APP
Exec=/bin/true
NoDisplay=true
EOF
	export XDG_DATA_DIRS="$DEVDIR/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
	echo "${0##*/}: client will run as $AS_APP in $APP_CGROUP"
}

remove_app_scope() {
	[ -n "$APP_CGROUP" ] && rmdir "$APP_CGROUP" 2>/dev/null
	APP_CGROUP=""
}

# Request.Close unexports the public request without Response; verify termination in the backend log.
check_backend_log() {
	[ -n "$EXPECT_BACKEND" ] || return 0

	if grep -qE "$EXPECT_BACKEND" "$DEVDIR/backend.log" 2>/dev/null; then
		echo "${0##*/}: backend log matched: $EXPECT_BACKEND"
		grep -E "$EXPECT_BACKEND" "$DEVDIR/backend.log" | sed 's/^/    /'
		return 0
	fi

	echo "${0##*/}: FAIL the backend log has no line matching: $EXPECT_BACKEND"
	return 1
}

stop_stack() {
	kill "$FRONTEND_PID" "$BACKEND_PID" ${SERVER_PID:+"$SERVER_PID"} ${PERM_PID:+"$PERM_PID"} \
		2>/dev/null
	wait 2>/dev/null
}

inner() {
	BACKEND_ARGS=(--verbose)

	# The fixture CA requires an explicit development trust pin.
	BACKEND_ARGS+=(--debug-trust-certificate "localhost=$SOFTHSM_DIR/server.pem")

	if [ "$MTLS" = 1 ]; then
		BACKEND_ARGS+=(
			--cert-adapter pkcs11
			--client-cert-uri "$(cat "$SOFTHSM_DIR/cert-uri")"
			--client-cert-pin-file "$SOFTHSM_DIR/pin"
		)
		# WebKit's network process inherits the fixture p11-kit configuration.
		export XDG_CONFIG_HOME="$SOFTHSM_DIR"
		export SOFTHSM2_CONF="$SOFTHSM_DIR/softhsm2.conf"
	fi

	if [ "$MODE" = live ]; then
		BACKEND_ARGS+=(--replace --allow-replacement)
	fi

	# Use a disposable data home unless a test explicitly shares storage across runs.
	export XDG_DATA_HOME="${DATA_HOME:-$DEVDIR/data-home}"
	mkdir -p "$XDG_DATA_HOME"

	make_app_scope
	start_server
	start_stack
	check_frontend_log

	rc=0
	if [ "$RUN_E2E" = 1 ]; then
		local args=(
			--start-uri "https://localhost:$PORT$START_PATH"
			--completion-uri "$COMPLETION_URI"
		)
		[ -n "$SESSION_MODE" ] && args+=(--session-mode "$SESSION_MODE")

		if [ -n "$APP_CGROUP" ]; then
			# The process inspected by the frontend must enter the scope before exec.
			bash -c 'echo $$ >"$1/cgroup.procs" || exit 40; shift; exec "$@"' -- \
				"$APP_CGROUP" python3 "$REPO/tools/webauth-e2e.py" "${args[@]}" "${E2E_ARGS[@]}"
		else
			python3 "$REPO/tools/webauth-e2e.py" "${args[@]}" "${E2E_ARGS[@]}"
		fi
		rc=$?
		echo
		echo "${0##*/}: webauth-e2e.py exited $rc"

		check_completion_was_not_fetched || rc=1
		check_backend_log || rc=1
	fi

	if [ "$KEEP" = 1 ]; then
		echo
		echo "${0##*/}: --keep: stack up"
		echo "  DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS"
		echo "  XDG_DESKTOP_PORTAL_DIR=$XDG_DESKTOP_PORTAL_DIR"
		echo "  start URI https://localhost:$PORT/start"
		echo "  Ctrl-C to tear it down"
		wait "$FRONTEND_PID"
	fi

	echo "${0##*/}: logs in $DEVDIR"
	remove_app_scope
	stop_stack
	return "$rc"
}

main() {
	parse_args "$@"

	if [ "${DEV_STACK_INNER:-0}" = "1" ]; then
		trap stop_stack EXIT
		inner
		return
	fi

	preflight
	write_devdir

	local forward=()
	[ "$KEEP" = 1 ] && forward+=(--keep)
	[ "$RUN_E2E" = 0 ] && forward+=(--no-e2e)
	[ "$MTLS" = 1 ] && forward+=(--mtls)
	[ "$COOKIE" = 1 ] && forward+=(--cookie "--cookie-value=$COOKIE_VALUE")
	[ -n "$SESSION_MODE" ] && forward+=("--session-mode=$SESSION_MODE")
	[ -n "$AS_APP" ] && forward+=("--as-app=$AS_APP")
	forward+=("--start-path=$START_PATH")
	[ -n "$DATA_HOME" ] && forward+=("--data-home=$DATA_HOME")
	[ -n "$EXPECT_BACKEND" ] && forward+=("--expect-backend-log=$EXPECT_BACKEND")
	forward+=("--completion-uri=$COMPLETION_URI")

	if [ "$MODE" = live ]; then
		echo "${0##*/}: --live: taking org.freedesktop.portal.Desktop on the real session bus"
		trap 'fixture_remove "$DEVDIR" dev-stack' EXIT
		DEV_STACK_INNER=1 DEVDIR="$DEVDIR" XDP_BUILD="$XDP_BUILD" BACKEND="$BACKEND" \
			XDP_ENV="$XDP_ENV" SOFTHSM_DIR="$SOFTHSM_DIR" \
			"${BASH_SOURCE[0]}" --live "${forward[@]}" -- "${E2E_ARGS[@]}"
		return $?
	fi

	DEV_STACK_INNER=1 DEVDIR="$DEVDIR" XDP_BUILD="$XDP_BUILD" BACKEND="$BACKEND" \
		XDP_ENV="$XDP_ENV" SOFTHSM_DIR="$SOFTHSM_DIR" \
		dbus-run-session -- "${BASH_SOURCE[0]}" "${forward[@]}" -- "${E2E_ARGS[@]}"
	rc=$?

	# Failed runs retain their logs.
	[ "$rc" = 0 ] && [ "${KEEP_LOGS:-0}" = 0 ] && fixture_remove "$DEVDIR" dev-stack
	return $rc
}

main "$@"
