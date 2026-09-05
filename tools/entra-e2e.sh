#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
#
# entra-e2e.sh -- the Entra client, end to end, with nothing real behind it.
#
# tools/ui-smoke.sh proves the BACKEND can drive a sign-in window headless.
# This proves the CLIENT: that entra-token-helper builds an authorization
# request, gets a code back through the PUBLIC portal interface, exchanges it,
# stores an account, serves the next request from its cache, mints a
# proof-of-possession token bound to a caller's key, and forgets all of it.
#
# WHAT IT STANDS UP, all on ONE private bus inside ONE headless X server:
#
#   xdg-permission-store          the frontend refuses to start without it
#   xdg-desktop-portal            the development frontend, ONE gate on:
#                                 XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication
#   xdg-desktop-portal-webauth    this repository's backend, NO certificate
#                                 adapter: the mock authority asks for no card,
#                                 so this run is about the client and not about
#                                 the card. tools/portal-stack.sh is the card.
#   tools/mock-token-endpoint.py  an Entra ID shaped authority
#   entra-token-helper            the program under test, as an ordinary
#                                 application calling the public interface
#
# THE FIVE RUNS, and what each one is the only evidence for:
#
#   1  login              a window, a code, an exchange, an account stored
#   2  token              THE MOCK IS NOT CALLED AT ALL: served from cache
#   3  token --req-cnf    the mock refuses the pop REFRESH with
#                         interaction_required -- which is what Entra does,
#                         because that scope carries the "make sure you trust
#                         this client" interstitial -- the client opens the
#                         window again, exchanges a fresh code with req_cnf,
#                         and the pop token comes back carrying the caller's
#                         confirmation blob
#   4  token --req-cnf    again, and the mock sees another grant: A POP TOKEN IS
#                         NEVER CACHED
#   5  logout             the account is gone, and a --prompt never request
#                         after it exits 30 rather than opening a window
#
# THE KEYRING IS libsecret's FILE BACKEND. SECRET_BACKEND=file with
# SECRET_FILE_TEST_PATH and SECRET_FILE_TEST_PASSWORD needs no daemon, no
# gnome-keyring, no D-Bus name and no unlock prompt, which on a private bus is
# three fewer things to go wrong than starting gnome-keyring-daemon --components
# =secrets would be. It is a TEST configuration and the client does not choose
# it: docs/SECURITY.md's "no persistent cache mode" is about the client never
# inventing a store of its own, not about which Secret Service is running.
#
# WHAT IT NEEDS, none of which has to be installed system wide:
#
#   Xvfb        xorg-x11-server-Xvfb           $XVFB
#   $XDP_BUILD  a built frontend, as tools/dev-stack.sh describes
#   $BACKEND    this repository's backend
#   $HELPER     this repository's client
#
#     tools/entra-e2e.sh
#     tools/entra-e2e.sh --keep      # leave the stack up
#     tools/entra-e2e.sh --verbose   # the client's own breadcrumbs

set -u

here() { cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd; }
REPO="$(here)"

XDP_BUILD="${XDP_BUILD:-$REPO/../xdg-desktop-portal/build}"
BACKEND="${BACKEND:-$REPO/build-backend/src/xdg-desktop-portal-webauth}"
HELPER="${HELPER:-$REPO/build-entra/entra-token-helper}"
XDP_ENV="${XDP_ENV:-$REPO/.xdp-env}"
XVFB="${XVFB:-$(command -v Xvfb || true)}"
SCREEN="${SCREEN:-:94}"

TENANT="${TENANT:-8331b18d-2d87-48ef-a35f-ac8818ebf9b4}"
ACCOUNT="${ACCOUNT:-fixture@mock.invalid}"
SCOPE="${SCOPE:-https://www.wvd.azure.us/.default}"
POP_SCOPE="${POP_SCOPE:-ms-device-service://termsrv.wvd.microsoft.com/name/avd-host-1/user_impersonation}"
# The base64url JSON confirmation object FreeRDP generates. Its content is
# opaque to this client: it is passed through and must come back inside the token.
REQ_CNF="${REQ_CNF:-eyJraWQiOiJPc3FYSmNKSUczRWxROUYxUEN4YXduYnJKYXk0NTVvSWNNQlNJVERnNVdBIn0}"

KEEP=0
VERBOSE=""

die() {
	echo "${0##*/}: $*" >&2
	exit 40
}

# shellcheck source=tools/lib.sh
. "$REPO/tools/lib.sh"

usage() {
	sed -n '3,60p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
	exit 0
}

while [ $# -gt 0 ]; do
	case "$1" in
	--keep) KEEP=1 ;;
	--verbose) VERBOSE="--verbose" ;;
	-h | --help) usage ;;
	*) die "unknown option '$1'; try --help" ;;
	esac
	shift
done

if [ -n "${LOGDIR:-}" ]; then
	fixture_make "$LOGDIR" entra-e2e
else
	LOGDIR="$(fixture_mktemp xdp-webauth-entra-e2e entra-e2e)"
fi

# shellcheck disable=SC1090
[ -n "$XDP_ENV" ] && [ -f "$XDP_ENV" ] && . "$XDP_ENV"

command -v python3 >/dev/null || die "python3 not found"
command -v openssl >/dev/null || die "openssl not found"
command -v dbus-run-session >/dev/null || die "dbus-run-session not found"
[ -n "$XVFB" ] || die "Xvfb not found; set \$XVFB"
[ -x "$BACKEND" ] || die "no backend at $BACKEND; set BACKEND"
[ -x "$HELPER" ] || die "no client at $HELPER; run 'meson setup build-entra clients/entra && ninja -C build-entra' or set HELPER"
[ -x "$XDP_BUILD/desktop-portal/xdg-desktop-portal" ] || die "no frontend; set XDP_BUILD"
[ -x "$XDP_BUILD/document-portal/xdg-permission-store" ] || die "no permission store; set XDP_BUILD"

if ldd "$XDP_BUILD/desktop-portal/xdg-desktop-portal" 2>/dev/null | grep -q 'not found'; then
	ldd "$XDP_BUILD/desktop-portal/xdg-desktop-portal" | grep 'not found' >&2
	die "the frontend cannot resolve its libraries; set XDP_ENV (docs/TESTING.md)"
fi

DEVDIR="$LOGDIR/portals"
(umask 077 && mkdir -p "$DEVDIR")
xdp_write_portal_dir "$DEVDIR" "$REPO" private

# The mock authority's certificate. Throwaway, and nothing on the machine trusts
# it -- correctly. Two things are told to: the backend, for the web view, with
# its one --debug-trust-certificate option; and the client, through the
# [testing] group of a configuration file written below. Neither can be set by
# an environment variable, which is the point.
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=localhost \
	-addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
	-keyout "$LOGDIR/mock.key" -out "$LOGDIR/mock.pem" >"$LOGDIR/openssl.log" 2>&1 ||
	die "could not make a server certificate; see $LOGDIR/openssl.log"

# ------------------------------------------------------------ the checks
#
# EACH ONE IS A LINE SOMEBODY ELSE WROTE. "A pop grant reached the authority" is
# a line the MOCK logged, not an inference from the client's exit code, and
# "the cache was used" is the ABSENCE of one.
pass=0
fail=0

check() {
	local label="$1" ok="$2"

	if [ "$ok" = 0 ]; then
		printf '  ok    %s\n' "$label"
		pass=$((pass + 1))
	else
		printf '  FAIL  %s\n' "$label"
		fail=$((fail + 1))
	fi
}

check_grep() {
	local label="$1" file="$2" pattern="$3"

	if grep -qE -- "$pattern" "$file" 2>/dev/null; then
		printf '  ok    %-46s %s\n' "$label" "$(grep -hoE -- "$pattern" "$file" | head -1)"
		pass=$((pass + 1))
	else
		printf '  FAIL  %-46s (no /%s/ in %s)\n' "$label" "$pattern" "${file##*/}"
		fail=$((fail + 1))
	fi
}

check_count() {
	local label="$1" file="$2" pattern="$3" want="$4" got

	got="$(grep -cE -- "$pattern" "$file" 2>/dev/null)"
	if [ "$got" = "$want" ]; then
		printf '  ok    %-46s %s\n' "$label" "$got"
		pass=$((pass + 1))
	else
		printf '  FAIL  %-46s want %s, got %s\n' "$label" "$want" "$got"
		fail=$((fail + 1))
	fi
}

# The confirmation blob has to come back INSIDE the token, or the token is not
# bound to the caller's key and the whole PoP exercise proved nothing.
check_pop_binding() {
	local file="$1"

	if python3 - "$file" "$REQ_CNF" <<'PY'
import base64, json, sys

token = open(sys.argv[1], encoding="utf-8").read().strip()
segments = token.split(".")
if len(segments) != 3:
    sys.exit(1)
payload = segments[1]
payload += "=" * (-len(payload) % 4)
claims = json.loads(base64.urlsafe_b64decode(payload))
sys.exit(0 if claims.get("cnf", {}).get("req_cnf") == sys.argv[2] else 1)
PY
	then
		check "the pop token carries the caller's req_cnf" 0
	else
		check "the pop token carries the caller's req_cnf" 1
	fi
}

inner() {
	"$XDP_BUILD/document-portal/xdg-permission-store" >"$LOGDIR/permission-store.log" 2>&1 &
	PERM=$!
	sleep 1

	# THE FRONTEND GOES UP FIRST. GTK asks the settings portal for a colour
	# scheme as soon as a backend initialises it, which on a private bus
	# ACTIVATES org.freedesktop.portal.Desktop -- and activation finds the
	# SYSTEM portal at /usr/libexec, which then owns the name.
	"$XDP_BUILD/desktop-portal/xdg-desktop-portal" -v >"$LOGDIR/frontend.log" 2>&1 &
	FE=$!
	xdp_wait_for_name org.freedesktop.portal.Desktop "$FE" || {
		tail -20 "$LOGDIR/frontend.log" >&2
		echo "${0##*/}: the frontend never took org.freedesktop.portal.Desktop"
		return 40
	}

	# THE MOCK AUTHORITY. --fail-pop-refresh 1 makes the first
	# proof-of-possession refresh answer interaction_required, which is the
	# interstitial Entra shows for the RDS-AAD scope.
	rm -f "$LOGDIR/port" "$LOGDIR/events.log"
	python3 "$REPO/tools/mock-token-endpoint.py" \
		--cert "$LOGDIR/mock.pem" --key "$LOGDIR/mock.key" \
		--account "$ACCOUNT" --fail-pop-refresh 1 \
		--event-log "$LOGDIR/events.log" --port-file "$LOGDIR/port" \
		>"$LOGDIR/mock.log" 2>&1 &
	MOCK=$!
	for _ in $(seq 1 40); do
		[ -s "$LOGDIR/port" ] && break
		kill -0 "$MOCK" 2>/dev/null || {
			cat "$LOGDIR/mock.log" >&2
			echo "${0##*/}: the mock authority exited"
			return 40
		}
		sleep 0.25
	done
	PORT="$(cat "$LOGDIR/port" 2>/dev/null)"
	[ -n "$PORT" ] || {
		echo "${0##*/}: the mock authority never reported a port"
		return 40
	}

	"$BACKEND" --verbose --debug-trust-certificate "localhost=$LOGDIR/mock.pem" \
		>"$LOGDIR/backend.log" 2>&1 &
	BE=$!
	xdp_wait_for_name org.freedesktop.impl.portal.desktop.webauth "$BE" || {
		tail -20 "$LOGDIR/backend.log" >&2
		echo "${0##*/}: the webauth backend never took its bus name"
		return 40
	}

	# THE CLIENT'S CONFIGURATION FILE. A caller may not widen an allowlist and
	# no environment variable can; a user's file can, one entry at a time. That
	# is the only reason this run can point the client at localhost at all.
	cat >"$LOGDIR/client.conf" <<EOF
[allow]
authorities = localhost:$PORT;

[testing]
trust_certificate = $LOGDIR/mock.pem
EOF

	local common=(--config "$LOGDIR/client.conf" --authority "localhost:$PORT"
		--tenant "$TENANT" --client-id a85cf173-4192-42f8-81fa-777a763e6e2c)
	[ -n "$VERBOSE" ] && common+=("$VERBOSE")

	echo
	echo "=== 1. login ==="
	"$HELPER" login "${common[@]}" \
		--scope "$SCOPE" --scope openid --scope profile --scope offline_access \
		>"$LOGDIR/login.out" 2>"$LOGDIR/login.err"
	local rc=$?
	sed 's/^/    /' "$LOGDIR/login.out" "$LOGDIR/login.err" 2>/dev/null | head -20
	check "login exited 0" "$([ "$rc" = 0 ] && echo 0 || echo 1)"
	check_grep "login named the account" "$LOGDIR/login.out" "^Signed in as $ACCOUNT\$"
	check_grep "the authorization request used PKCE S256" "$LOGDIR/events.log" \
		'GET authorize .*pkce=S256'
	check_grep "the authorization request asked to choose an account" "$LOGDIR/events.log" \
		'GET authorize .*prompt=select_account'
	check_grep "the code was exchanged" "$LOGDIR/events.log" 'POST token outcome=code_ok pop=no'
	check_grep "the window opened and completed" "$LOGDIR/backend.log" 'completed'

	echo
	echo "=== 2. accounts ==="
	"$HELPER" accounts --config "$LOGDIR/client.conf" >"$LOGDIR/accounts.out" 2>&1
	sed 's/^/    /' "$LOGDIR/accounts.out"
	check_grep "the account is listed" "$LOGDIR/accounts.out" "^$ACCOUNT	localhost:$PORT	$TENANT\$"

	echo
	echo "=== 3. token, from cache ==="
	cp "$LOGDIR/events.log" "$LOGDIR/events-before-cache.log"
	"$HELPER" token "${common[@]}" --scope "$SCOPE" --scope openid --scope profile \
		--scope offline_access --account "$ACCOUNT" --prompt never \
		>"$LOGDIR/token.out" 2>"$LOGDIR/token.err"
	rc=$?
	check "token --prompt never exited 0" "$([ "$rc" = 0 ] && echo 0 || echo 1)"
	check "the token is a JWT shaped string" \
		"$(grep -qE '^[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+$' "$LOGDIR/token.out" &&
			echo 0 || echo 1)"
	# THE CACHE IS THE ABSENCE OF A REQUEST. Nothing new in the authority's log.
	check "the authority was not called at all" \
		"$(cmp -s "$LOGDIR/events.log" "$LOGDIR/events-before-cache.log" && echo 0 || echo 1)"
	check "one line on stdout and nothing else" \
		"$([ "$(wc -l <"$LOGDIR/token.out")" = 1 ] && echo 0 || echo 1)"

	echo
	echo "=== 4. token --req-cnf: the interstitial, then a pop grant ==="
	"$HELPER" token "${common[@]}" --scope "$POP_SCOPE" --account "$ACCOUNT" \
		--req-cnf "$REQ_CNF" >"$LOGDIR/pop.out" 2>"$LOGDIR/pop.err"
	rc=$?
	sed 's/^/    /' "$LOGDIR/pop.err" | head -20
	check "token --req-cnf exited 0" "$([ "$rc" = 0 ] && echo 0 || echo 1)"
	check_grep "the pop refresh was refused, needing the human" "$LOGDIR/events.log" \
		'POST token outcome=pop_refresh_interaction_required'
	check_grep "the window opened again for that scope" "$LOGDIR/events.log" \
		'GET authorize .*prompt=- '
	check_grep "the code was exchanged with a confirmation blob" "$LOGDIR/events.log" \
		'POST token outcome=code_ok pop=yes'
	check_grep "the request declared token_type=pop" "$LOGDIR/events.log" \
		'POST token .*pop=yes token_type=pop'
	check_pop_binding "$LOGDIR/pop.out"

	echo
	echo "=== 5. token --req-cnf again: a pop token is never cached ==="
	cp "$LOGDIR/events.log" "$LOGDIR/events-before-second-pop.log"
	"$HELPER" token "${common[@]}" --scope "$POP_SCOPE" --account "$ACCOUNT" \
		--req-cnf "$REQ_CNF" --json >"$LOGDIR/pop2.out" 2>"$LOGDIR/pop2.err"
	rc=$?
	check "the second token --req-cnf exited 0" "$([ "$rc" = 0 ] && echo 0 || echo 1)"
	check "the authority was called again" \
		"$(cmp -s "$LOGDIR/events.log" "$LOGDIR/events-before-second-pop.log" && echo 1 || echo 0)"
	check_grep "the json form names the token type" "$LOGDIR/pop2.out" '"token_type" : "pop"'
	check "no refresh token is anywhere in the response" \
		"$(grep -q refresh_token "$LOGDIR/pop2.out" && echo 1 || echo 0)"
	# The second pop request is a refresh grant: the mock only refuses the first.
	check_grep "the second one came from a refresh grant" "$LOGDIR/events.log" \
		'POST token outcome=refresh_ok pop=yes'

	echo
	echo "=== 6. logout, and what a request after it does ==="
	"$HELPER" logout --account "$ACCOUNT" --config "$LOGDIR/client.conf" \
		>"$LOGDIR/logout.out" 2>&1
	rc=$?
	sed 's/^/    /' "$LOGDIR/logout.out"
	check "logout exited 0" "$([ "$rc" = 0 ] && echo 0 || echo 1)"
	check_grep "logout named what it removed" "$LOGDIR/logout.out" "^Removed $ACCOUNT\$"

	"$HELPER" accounts --config "$LOGDIR/client.conf" >"$LOGDIR/accounts-after.out" 2>&1
	check "nothing is listed any more" \
		"$([ ! -s "$LOGDIR/accounts-after.out" ] && echo 0 || echo 1)"

	"$HELPER" token "${common[@]}" --scope "$SCOPE" --prompt never \
		>"$LOGDIR/gone.out" 2>"$LOGDIR/gone.err"
	rc=$?
	check "a silent request with no account exits 30" "$([ "$rc" = 30 ] && echo 0 || echo 1)"
	check "and stdout is empty" "$([ ! -s "$LOGDIR/gone.out" ] && echo 0 || echo 1)"

	echo
	echo "=== 7. no portal at all ==="
	# The gate off is the DEFAULT STATE OF A MACHINE, and a dispatcher has to be
	# able to tell it from a refusal. Asked here with no session bus at all,
	# which the client is required to answer the same way.
	env -u DBUS_SESSION_BUS_ADDRESS "$HELPER" login "${common[@]}" --scope "$SCOPE" \
		>"$LOGDIR/nobus.out" 2>"$LOGDIR/nobus.err"
	rc=$?
	check "no bus exits 40" "$([ "$rc" = 40 ] && echo 0 || echo 1)"
	check_grep "and the message names the gate" "$LOGDIR/nobus.err" \
		'XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication'

	echo
	echo "=== what nobody was allowed to write down ==="
	# A support bundle containing these logs must not let its reader sign in.
	local leaked=0
	for f in "$LOGDIR/login.err" "$LOGDIR/token.err" "$LOGDIR/pop.err" "$LOGDIR/pop2.err"; do
		[ -s "$f" ] || continue
		if grep -qE 'code=|code_verifier|refresh_token|eyJ[A-Za-z0-9_-]{20}' "$f"; then
			echo "  FAIL  a credential reached $f"
			leaked=1
		fi
	done
	check "no code, verifier or token in the client's stderr" "$leaked"

	if [ "$KEEP" = 1 ]; then
		echo
		echo "${0##*/}: --keep: stack up on $DBUS_SESSION_BUS_ADDRESS"
		echo "  authority  localhost:$PORT"
		echo "  config     $LOGDIR/client.conf"
		wait "$FE"
	fi

	kill "$BE" "$FE" "$MOCK" "$PERM" 2>/dev/null
	sleep 1

	echo
	echo "runs: $((pass + fail))  ok: $pass  failed: $fail"
	[ "$fail" = 0 ] || return 1
	return 0
}

"$XVFB" "$SCREEN" -screen 0 1280x1024x24 -nolisten tcp >"$LOGDIR/xvfb.log" 2>&1 &
XVFB_PID=$!
sleep 2

cleanup() {
	kill "$XVFB_PID" 2>/dev/null
	wait "$XVFB_PID" 2>/dev/null
}
trap cleanup EXIT

# X11 rather than Wayland, because a headless X server is something a machine
# with no compositor can start.
export DISPLAY="$SCREEN"
unset WAYLAND_DISPLAY
export GDK_BACKEND=x11
export GTK_A11Y=none
export ADW_DEBUG_COLOR_SCHEME="${ADW_DEBUG_COLOR_SCHEME:-prefer-dark}"

export XDG_DESKTOP_PORTAL_DIR="$DEVDIR"
export XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication
export XDG_CURRENT_DESKTOP="${XDG_CURRENT_DESKTOP:-dev}"
export XDG_DATA_HOME="$LOGDIR/data-home"
mkdir -p "$XDG_DATA_HOME"
export G_MESSAGES_DEBUG="webauth entra"

# libsecret's file backend: no daemon, no bus name, no unlock prompt. See the
# header. The password is a fixture's, in a directory this run made.
export SECRET_BACKEND=file
export SECRET_FILE_TEST_PATH="$LOGDIR/keyring"
export SECRET_FILE_TEST_PASSWORD="entra-e2e-fixture"
rm -f "$SECRET_FILE_TEST_PATH"

export LOGDIR REPO XDP_BUILD BACKEND HELPER TENANT ACCOUNT SCOPE POP_SCOPE REQ_CNF KEEP VERBOSE
export pass fail

dbus-run-session -- bash -c "
	$(declare -f xdp_wait_for_name)
	$(declare -f check)
	$(declare -f check_grep)
	$(declare -f check_count)
	$(declare -f check_pop_binding)
	$(declare -f inner)
	inner"
rc=$?

echo
if [ "$rc" = 0 ]; then
	echo "entra-e2e: PASS; logs in $LOGDIR"
else
	echo "entra-e2e: FAIL ($rc); logs in $LOGDIR"
fi
exit "$rc"
