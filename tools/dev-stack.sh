#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
#
# Adapted from the sibling backend xdg-desktop-portal-certificate's
# tools/dev-stack.sh (same author, LGPL-2.1-or-later there).
#
# dev-stack.sh -- run the experimental WebAuthentication portal end to end: a
# development xdg-desktop-portal frontend from the branch
# experimental/certificate-webauthentication, this repository's backend, a
# fixture identity provider, and tools/webauth-e2e.py against the PUBLIC
# interface.
#
# THIS OPENS A WINDOW. The backend is a web view; there is nothing to run
# headless. On a private bus with no display it exits 40, which is correct and
# is not what you wanted. Either run it under tools/ui-smoke.sh, which brings its
# own Xvfb, or run it with DISPLAY set to a desktop you are happy to see a
# window on.
#
# Two modes.
#
#   PRIVATE BUS (the default). Everything runs inside `dbus-run-session`, so
#   nothing touches the user's real session bus.
#
#   --live. Uses the real session bus and takes org.freedesktop.portal.Desktop
#   away from the system portal for the duration. THE SYSTEM PORTAL COMES BACK
#   BY D-BUS ACTIVATION as soon as this script's frontend exits and something
#   asks for the name again.
#
#     tools/dev-stack.sh                       # plain https flow, no certificates
#     tools/dev-stack.sh --mtls                # the server demands a client certificate
#     tools/dev-stack.sh --cookie --session-mode ephemeral
#     tools/dev-stack.sh --keep                # leave it up for manual gdbus
#     tools/dev-stack.sh -- --cancel-after 2000 --expect-response 1
#     tools/dev-stack.sh --as-app=org.example.WebauthClient --cookie
#     tools/dev-stack.sh --start-path=/wait -- --cancel-after 3000 --expect-no-response
#
#   --as-app RUNS THE CLIENT AS AN IDENTIFIED APPLICATION, which is the only way
#   to reach the shared storage mode: an unidentified caller is narrowed to
#   ephemeral by the backend, on purpose (src/storage.h). It does what a desktop
#   environment does when it launches an application -- puts the process in a
#   systemd user scope named app-<id>-<random>.scope, and makes an
#   <id>.desktop file findable -- because that is what xdg-desktop-portal reads
#   to derive a host caller's identity (shared/xdp-app-info-host.c,
#   get_app_from_pid). The cgroup is created directly rather than through
#   `systemd-run --user`, which would need the real session bus.
#
# WHAT IT NEEDS
#
#   $XDP_BUILD   a built xdg-desktop-portal from the branch
#                experimental/certificate-webauthentication. Default:
#                ../xdg-desktop-portal/build relative to this repository. It
#                must contain desktop-portal/xdg-desktop-portal and
#                document-portal/xdg-permission-store.
#   $BACKEND     this repository's backend binary. Default:
#                ./build-backend/src/xdg-desktop-portal-webauth
#   $XDP_ENV     a file to source first, for a frontend built against a scratch
#                prefix -- LD_LIBRARY_PATH for libdex, PKG_CONFIG_PATH.
#                Default: .xdp-env in this repository if it exists.
#   $SOFTHSM_DIR the fixture from tools/softhsm-fixture.sh. It supplies the
#                server certificate and, with --mtls, the client one.
#
# WHAT IT DOES
#
#   1. writes a throwaway $XDG_DESKTOP_PORTAL_DIR holding A SYMLINK TO EVERY
#      .portal FILE ON THE MACHINE, this repository's webauth.portal, and a COPY
#      of the machine's effective portals.conf with one line added routing
#      org.freedesktop.impl.portal.experimental.WebAuthentication to this
#      backend. All of it, and not just ours, because XDG_DESKTOP_PORTAL_DIR
#      makes the frontend ignore every other portal directory AND every other
#      portals.conf on the machine; tools/lib.sh says it at length.
#   2. starts xdg-permission-store -- xdg-desktop-portal refuses to start
#      without it -- on the private bus.
#   3. starts tools/mtls-server.py on a port of its own choosing.
#   4. starts the frontend with
#      XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication, then this
#      backend, so that the backend's stderr is visible rather than being
#      swallowed by D-Bus activation.
#   5. runs tools/webauth-e2e.py with whatever came after `--`.
#   6. checks the fixture server's access log: THE COMPLETION URI MUST NEVER
#      HAVE BEEN FETCHED. That is the one thing this whole design exists to
#      guarantee, and it is checked from the server's side rather than the
#      client's.

set -u

here() { cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd; }
REPO="$(here)"

XDP_BUILD="${XDP_BUILD:-$REPO/../xdg-desktop-portal/build}"
BACKEND="${BACKEND:-$REPO/build-backend/src/xdg-desktop-portal-webauth}"
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
	sed -n '3,80p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
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
	[ -x "$BACKEND" ] || die "no backend at $BACKEND; run 'meson setup build-backend backend && ninja -C build-backend' or set BACKEND"

	fixture_check "$SOFTHSM_DIR" softhsm
	[ -f "$SOFTHSM_DIR/server.pem" ] ||
		die "no fixture at $SOFTHSM_DIR; run tools/softhsm-fixture.sh first"

	[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] ||
		die "no display. The backend is a web view; use tools/ui-smoke.sh, which brings an Xvfb."

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
	export XDG_DESKTOP_PORTAL_DIR="$DEVDIR"
	export XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication

	if [ "$MODE" = private ]; then
		export XDG_CURRENT_DESKTOP="${XDG_CURRENT_DESKTOP:-dev}"
		"$PERMSTORE_BIN" >"$DEVDIR/permission-store.log" 2>&1 &
		PERM_PID=$!
		sleep 1
	else
		PERM_PID=""
	fi

	# THE FRONTEND GOES UP FIRST, and this is not a preference. GTK asks the
	# settings portal for a theme as soon as the backend initialises it, which on
	# a private bus ACTIVATES org.freedesktop.portal.Desktop -- and activation
	# finds the SYSTEM portal at /usr/libexec, which then owns the name this
	# script's frontend was about to take.
	FRONTEND_LOG="$DEVDIR/frontend.log"
	if [ "$MODE" = live ]; then
		"$FRONTEND_BIN" -r -v >"$FRONTEND_LOG" 2>&1 &
	else
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
		grep -q "in configuration for org.freedesktop.impl.portal.experimental.WebAuthentication" \
			"$FRONTEND_LOG" 2>/dev/null && configured="yes"
		grep -q "Providing portal org.freedesktop.portal.experimental.WebAuthentication" \
			"$FRONTEND_LOG" 2>/dev/null && provided="yes"
		[ "$configured" = yes ] && [ "$provided" = yes ] && break
		sleep 0.25
	done

	if [ "$configured" = yes ] && [ "$provided" = yes ]; then
		echo "${0##*/}: frontend: web authentication portal provided"
	else
		echo "${0##*/}: frontend: web authentication portal MISSING (configured=$configured provided=$provided; see $FRONTEND_LOG)"
	fi
}

# THE ONE THING THIS DESIGN EXISTS TO GUARANTEE, checked from the server's side.
# The completion URI carries the authorization code; a backend that let the
# engine follow the redirect would have sent that code to a page with no part in
# the exchange. When the completion URI points at the fixture server, the proof
# is that its access log never names the path.
check_completion_was_not_fetched() {
	local path

	path="$(python3 -c 'import sys,urllib.parse;print(urllib.parse.urlsplit(sys.argv[1]).path)' \
		"$COMPLETION_URI")"

	if grep -q " $path\$" "$DEVDIR/access.log" 2>/dev/null; then
		echo "${0##*/}: FAIL the fixture server was asked for $path"
		return 1
	fi

	echo "${0##*/}: the completion URI was never fetched ($path absent from the access log)"
	return 0
}

# The cgroup a desktop environment would have put the application in, made by
# hand. sd_pid_get_user_unit() reads the leaf of /proc/<pid>/cgroup, so a
# directory named app-<id>-<random>.scope under the user manager's delegated
# tree is enough; systemd itself is not involved and the real session bus is not
# touched. Removed on the way out, which only succeeds once it is empty.
APP_CGROUP=""

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

	# The .desktop file the frontend insists on before it will believe the unit
	# name, in a directory of this run's own.
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

# Some outcomes are not visible to the application at all: Request.Close() is
# answered by the frontend unexporting the request, so the only account of what
# the backend did is the backend's own log line.
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

	# The fixture server's certificate is issued by the fixture CA, which nothing
	# on the machine trusts -- correctly. See the option's own documentation in
	# src/webkit-session.h for why this exists and why it is the only trust
	# override in the binary.
	BACKEND_ARGS+=(--debug-trust-certificate "localhost=$SOFTHSM_DIR/server.pem")

	if [ "$MTLS" = 1 ]; then
		BACKEND_ARGS+=(
			--cert-adapter pkcs11
			--client-cert-uri "$(cat "$SOFTHSM_DIR/cert-uri")"
			--client-cert-pin-file "$SOFTHSM_DIR/pin"
		)
		# The token is visible to p11-kit -- and therefore to WebKit's network
		# process, which is where the URI is resolved -- only because these two
		# point at the fixture. The machine's own configuration is untouched.
		export XDG_CONFIG_HOME="$SOFTHSM_DIR"
		export SOFTHSM2_CONF="$SOFTHSM_DIR/softhsm2.conf"
	fi

	if [ "$MODE" = live ]; then
		BACKEND_ARGS+=(--replace --allow-replacement)
	fi

	# THE SHARED STORE IS A DIRECTORY UNDER $XDG_DATA_HOME, so a test run points
	# that somewhere disposable rather than writing an identity provider's
	# cookies into the home directory of whoever ran the script. Two runs that
	# want to share a store pass the same --data-home; one that does not gets a
	# directory of this run's own.
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
			# The client puts ITSELF in the scope and then becomes the client, so
			# that the process the frontend inspects is the one in the cgroup.
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

	# KEPT, not removed, when the run fails: the logs in it are the only account
	# of what the frontend, the backend and the server each did.
	DEV_STACK_INNER=1 DEVDIR="$DEVDIR" XDP_BUILD="$XDP_BUILD" BACKEND="$BACKEND" \
		XDP_ENV="$XDP_ENV" SOFTHSM_DIR="$SOFTHSM_DIR" \
		dbus-run-session -- "${BASH_SOURCE[0]}" "${forward[@]}" -- "${E2E_ARGS[@]}"
	rc=$?

	[ "$rc" = 0 ] && [ "${KEEP_LOGS:-0}" = 0 ] && fixture_remove "$DEVDIR" dev-stack
	return $rc
}

main "$@"
