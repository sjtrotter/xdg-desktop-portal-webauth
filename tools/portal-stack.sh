#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
#
# Usage: tools/portal-stack.sh [options] [-- e2e-options]
# Run WebAuthentication and Certificate portals together.
# Private mode uses a headless X server and the sibling certificate SoftHSM fixture.
#
#   --live                    use the session bus and real token; answer prompts manually
#   --pin-prompt=MODE         select gtk (default), system or auto
#   --no-drive                leave private-mode windows for manual interaction
#   --no-e2e                  start without the fixture server, client or integration report
#   --cancel-chooser          cancel the certificate chooser
#   --second-start            run a second request against the same backend
#   --keep                    wait before shutting down the stack
#   --uninstall-module        remove the module file installed by this script
#   --completion-uri=URI      set the completion URI (default: https://example.invalid/cb)
#   --start-path=PATH         set the fixture start path (default: /start)
#   --session-mode=MODE       request shared or ephemeral storage
#   -h, --help                show this help
#
# CERTIFICATE_REPO selects the built sibling certificate backend and module.
# CERT_SOFTHSM_DIR selects its fixture; XDP_BUILD selects the frontend build.
# BACKEND selects this backend; XDP_ENV selects an environment file to source.
# XVFB and XDOTOOL select the UI tools; PIN supplies the fixture PIN through the environment.
# The frontend opt-in list includes WebAuthentication1 and Certificate1.
# Live mode holds org.freedesktop.portal.Desktop and leaves a per-user p11-kit module file.
# The module is enabled only for this backend and WebKitNetworkProcess.
# See docs/TESTING.md for setup and hardware-token tests.

set -u

here() { cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd; }
REPO="$(here)"

XDP_BUILD="${XDP_BUILD:-$REPO/../xdg-desktop-portal/build}"
BACKEND="${BACKEND:-$REPO/build/src/xdg-desktop-portal-webauth}"
XDP_ENV="${XDP_ENV:-$REPO/.xdp-env}"
CERTIFICATE_REPO="${CERTIFICATE_REPO:-$REPO/../xdg-desktop-portal-certificate}"
CERT_BUILD="${CERT_BUILD:-$CERTIFICATE_REPO/build}"
CERT_BACKEND="${CERT_BACKEND:-$CERT_BUILD/src/xdg-desktop-portal-certificate}"
CERT_MODULE_SO="${CERT_MODULE_SO:-$CERT_BUILD/src/module/libpkcs11-portal-certificate.so}"
CERT_SOFTHSM_DIR="${CERT_SOFTHSM_DIR:-${TMPDIR:-/tmp}/xdp-certificate-softhsm}"
XVFB="${XVFB:-$(command -v Xvfb || true)}"
XDOTOOL="${XDOTOOL:-$(command -v xdotool || true)}"
PIN="${PIN:-123456}"
SCREEN="${SCREEN:-:93}"
KEY_ALGORITHM="${KEY_ALGORITHM:-RSA}"
CERT_FIXTURE="${CERT_FIXTURE:-portal-test-rsa}"

MODE=private
PIN_PROMPT=gtk
DRIVE=1
RUN_E2E=1
CANCEL_CHOOSER=0
SECOND_START=0
KEEP=0
UNINSTALL_MODULE=0
COMPLETION_URI="https://example.invalid/cb"
START_PATH="/start"
SESSION_MODE=
E2E_ARGS=()

die() {
	echo "${0##*/}: $*" >&2
	exit 40
}

# shellcheck source=tools/lib.sh
. "$REPO/tools/lib.sh"

usage() {
	sed -n '5,29p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
	exit 0
}

MODULE_MARKER="# Installed by tools/portal-stack.sh --live."
MODULE_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/pkcs11/modules"
MODULE_FILE="$MODULE_DIR/xdg-desktop-portal-certificate.module"

# p11-kit enable-in/disable-in match the program name from argv[0]; configure only one per module.
install_module_file() {
	if [ -e "$MODULE_FILE" ]; then
		if head -1 -- "$MODULE_FILE" | grep -qF "$MODULE_MARKER"; then
			echo "${0##*/}: $MODULE_FILE already installed"
			return 0
		fi
		die "$MODULE_FILE has no matching script marker; remove it manually or set XDG_CONFIG_HOME elsewhere"
	fi

	[ -f "$CERT_MODULE_SO" ] ||
		die "no client-side module at $CERT_MODULE_SO; build $CERTIFICATE_REPO"
	local so_path
	so_path="$(realpath -e -- "$CERT_MODULE_SO")" || die "could not resolve $CERT_MODULE_SO"

	(
		umask 077
		mkdir -p -- "$MODULE_DIR" &&
			cat >"$MODULE_FILE" <<EOF
$MODULE_MARKER
# Remove with: tools/portal-stack.sh --uninstall-module
module: $so_path
critical: no
enable-in: xdg-desktop-portal-webauth, WebKitNetworkProcess
EOF
	) || die "could not write $MODULE_FILE"

	echo "${0##*/}: installed $MODULE_FILE"
	echo "${0##*/}: remove it with: ${0##*/} --uninstall-module"
}

uninstall_module_file() {
	if [ ! -e "$MODULE_FILE" ]; then
		echo "${0##*/}: $MODULE_FILE not present"
		return 0
	fi
	head -1 -- "$MODULE_FILE" | grep -qF "$MODULE_MARKER" ||
		die "$MODULE_FILE has no matching script marker; removal refused"
	rm -f -- "$MODULE_FILE" || die "could not remove $MODULE_FILE"
	echo "${0##*/}: removed $MODULE_FILE"
}

while [ $# -gt 0 ]; do
	case "$1" in
	--live) MODE=live ;;
	--pin-prompt=*) PIN_PROMPT="${1#--pin-prompt=}" ;;
	--no-drive) DRIVE=0 ;;
	--no-e2e) RUN_E2E=0 ;;
	--cancel-chooser) CANCEL_CHOOSER=1 ;;
	--second-start) SECOND_START=1 ;;
	--keep) KEEP=1 ;;
	--uninstall-module) UNINSTALL_MODULE=1 ;;
	--completion-uri=*) COMPLETION_URI="${1#--completion-uri=}" ;;
	--start-path=*) START_PATH="${1#--start-path=}" ;;
	--session-mode=*) SESSION_MODE="${1#--session-mode=}" ;;
	-h | --help) usage ;;
	--)
		shift
		E2E_ARGS=("$@")
		break
		;;
	*) die "unknown option '$1'; try --help" ;;
	esac
	shift
done

case "$PIN_PROMPT" in
gtk | system | auto) ;;
*) die "--pin-prompt takes gtk, system or auto" ;;
esac

# Module removal does not require a running stack or fixture.
if [ "$UNINSTALL_MODULE" = 1 ]; then
	uninstall_module_file
	exit $?
fi

if [ -n "${LOGDIR:-}" ]; then
	fixture_make "$LOGDIR" portal-stack
else
	LOGDIR="$(fixture_mktemp xdp-webauth-portal-stack portal-stack)"
fi

# shellcheck disable=SC1090
[ -n "$XDP_ENV" ] && [ -f "$XDP_ENV" ] && . "$XDP_ENV"

command -v python3 >/dev/null || die "python3 not found"
python3 -c 'import gi' 2>/dev/null || die "python3-gobject not found (the e2e client needs it)"
command -v openssl >/dev/null || die "openssl not found"
command -v dbus-run-session >/dev/null || die "dbus-run-session not found"
[ -x "$BACKEND" ] || die "no backend at $BACKEND; set BACKEND"
[ -x "$CERT_BACKEND" ] || die "no certificate backend at $CERT_BACKEND; set CERTIFICATE_REPO"
[ -f "$CERT_MODULE_SO" ] || die "no client-side module at $CERT_MODULE_SO; build the sibling repository"
[ "$MODE" = live ] && install_module_file
[ -x "$XDP_BUILD/desktop-portal/xdg-desktop-portal" ] || die "no frontend; set XDP_BUILD"
[ -x "$XDP_BUILD/document-portal/xdg-permission-store" ] || die "no permission store; set XDP_BUILD"
# The certificate fixture uses the sibling repository's marker and validation rules.
(
	XDP_FIXTURE_MARKER="${CERT_FIXTURE_MARKER:-.xdg-desktop-portal-certificate-fixture}"
	fixture_check "$CERT_SOFTHSM_DIR" softhsm
) || exit 40
[ -f "$CERT_SOFTHSM_DIR/module-path" ] ||
	die "no SoftHSM fixture; run $CERTIFICATE_REPO/tools/softhsm-fixture.sh"
[ -f "$CERT_SOFTHSM_DIR/$CERT_FIXTURE.pem" ] ||
	die "the fixture has no $CERT_FIXTURE.pem; rerun its tools/softhsm-fixture.sh"

if [ "$MODE" = private ]; then
	[ -n "$XVFB" ] || die "Xvfb not found; set \$XVFB"
	[ "$DRIVE" = 0 ] || [ -n "$XDOTOOL" ] || die "xdotool not found; set \$XDOTOOL"
fi

if ldd "$XDP_BUILD/desktop-portal/xdg-desktop-portal" 2>/dev/null | grep -q 'not found'; then
	ldd "$XDP_BUILD/desktop-portal/xdg-desktop-portal" | grep 'not found' >&2
	die "the frontend cannot resolve its libraries; set XDP_ENV (docs/TESTING.md)"
fi

SOFTHSM_MODULE="$(cat "$CERT_SOFTHSM_DIR/module-path")"

# Preserve the machine's other portals when overriding XDG_DESKTOP_PORTAL_DIR.
DEVDIR="$LOGDIR/portals"
(umask 077 && mkdir -p "$DEVDIR")
xdp_write_portal_dir "$DEVDIR" "$REPO" "$MODE"

rm -f -- "$DEVDIR/certificate.portal"
sed -e '/^#/d' -e '/^$/d' "$CERTIFICATE_REPO/data/certificate.portal.in" \
	>"$DEVDIR/certificate.portal" || die "no certificate.portal.in in $CERTIFICATE_REPO"
xdp_conf_set "$DEVDIR/portals.conf" \
	org.freedesktop.impl.portal.experimental.Certificate1 'certificate;'

# Private mode supplies p11-kit configuration inherited by the UI and network processes; live mode uses the installed file.
if [ "$MODE" = private ]; then
	CONFDIR="$LOGDIR/config"
	(umask 077 && mkdir -p "$CONFDIR/pkcs11/modules")
	cat >"$CONFDIR/pkcs11/modules/xdg-desktop-portal-certificate.module" <<EOF
module: $CERT_MODULE_SO
critical: no
priority: -10
disable-in: xdg-desktop-portal, xdg-desktop-portal-certificate, p11-kit-server
EOF
fi

# Generate the disposable TLS server certificate for the fixture.
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=localhost \
	-addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
	-keyout "$LOGDIR/server.key" -out "$LOGDIR/server.pem" >"$LOGDIR/openssl.log" 2>&1 ||
	die "could not make a server certificate; see $LOGDIR/openssl.log"

cat >"$LOGDIR/driver.sh" <<'DRIVER_EOF'
#!/bin/bash
set -u
declare -A driven

# UI delays count against the TLS handshake deadline.
press() {
	local wid="$1"
	shift
	"$XDOTOOL" windowfocus "$wid" 2>/dev/null
	sleep 0.5
	for key in "$@"; do
		case "$key" in
		# Pass the PIN on stdin to keep it out of /proc command lines.
		type:) printf '%s' "$PIN" | "$XDOTOOL" type --delay 30 --file - ;;
		*) "$XDOTOOL" key "$key" ;;
		esac
		sleep 0.4
	done
}

# Retry windows after ten seconds because focus may be requested before mapping.
handle() {
	local wid="$1" now
	shift
	now="$(date +%s)"

	if [ -z "${driven[$wid]:-}" ]; then
		driven[$wid]="$now"
		echo "driver: $wid $*"
		press "$wid" "$@"
	elif [ $((now - driven[$wid])) -ge 10 ]; then
		driven[$wid]="$now"
		echo "driver: $wid still up, pressing again"
		press "$wid" "${@: -1}"
	fi
}

while [ ! -f "$LOGDIR/driver-stop" ]; do
	for wid in $("$XDOTOOL" search --onlyvisible --name "Use a Certificate" 2>/dev/null); do
		if [ "$CANCEL_CHOOSER" = 1 ]; then
			handle "$wid" Escape
		else
			handle "$wid" Down Return
		fi
	done
	for wid in $("$XDOTOOL" search --onlyvisible --name "Unlock Security Token" 2>/dev/null); do
		handle "$wid" "type:" Return
	done
	sleep 0.3
done
DRIVER_EOF
chmod +x "$LOGDIR/driver.sh"

# Check logs from each integration step to confirm the portal provider handled the handshake.
expect_log() {
	local label="$1" file="$2" pattern="$3"

	if grep -qE -- "$pattern" "$file" 2>/dev/null; then
		printf '  ok    %-34s %s\n' "$label" "$(grep -hoE -- "$pattern" "$file" | head -1)"
		return 0
	fi

	printf '  FAIL  %-34s (no /%s/ in %s)\n' "$label" "$pattern" "${file##*/}"
	return 1
}

check_completion_was_not_fetched() {
	local path
	path="$(python3 -c 'import sys,urllib.parse;print(urllib.parse.urlsplit(sys.argv[1]).path)' \
		"$COMPLETION_URI")"

	if grep -q " $path\$" "$LOGDIR/access.log" 2>/dev/null; then
		printf '  FAIL  %-34s the server was asked for %s\n' "completion never fetched" "$path"
		return 1
	fi

	printf '  ok    %-34s %s absent from the access log\n' "completion never fetched" "$path"
	return 0
}

inner() {
	local rc=0 e2e_rc=0

	if [ "$MODE" = private ]; then
		"$XDP_BUILD/document-portal/xdg-permission-store" >"$LOGDIR/permission-store.log" 2>&1 &
		PERM=$!
		sleep 1
	else
		PERM=""
	fi

	# Start the frontend before GTK can activate the system portal through Settings.
	local experimental
	experimental="$(xdp_experimental_portals WebAuthentication1 Certificate1)"
	if [ "$MODE" = live ]; then
		XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL="$experimental" \
			"$XDP_BUILD/desktop-portal/xdg-desktop-portal" -r -v >"$LOGDIR/frontend.log" 2>&1 &
	else
		XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL="$experimental" \
			"$XDP_BUILD/desktop-portal/xdg-desktop-portal" -v >"$LOGDIR/frontend.log" 2>&1 &
	fi
	FE=$!
	xdp_wait_for_name org.freedesktop.portal.Desktop "$FE" || {
		tail -20 "$LOGDIR/frontend.log" >&2
		echo "${0##*/}: the frontend never took org.freedesktop.portal.Desktop"
		return 40
	}

	PROMPTER=""
	if [ "$PIN_PROMPT" = system ] && [ "$MODE" = private ]; then
		PROMPTER_BIN="${PROMPTER_BIN:-$CERT_BUILD/tests/certificate-test-prompter}"
		[ -x "$PROMPTER_BIN" ] || {
			echo "${0##*/}: no test prompter at $PROMPTER_BIN (needs a build with gcr-4)"
			return 40
		}
		TEST_PROMPTER_PIN="$PIN" "$PROMPTER_BIN" >"$LOGDIR/prompter.log" 2>&1 &
		PROMPTER=$!
		sleep 1
	fi

	# Live mode uses the real token through p11-kit and excludes software-token options.
	if [ "$MODE" = live ]; then
		"$CERT_BACKEND" --verbose --pin-prompt "$PIN_PROMPT" \
			>"$LOGDIR/certificate.log" 2>&1 &
	else
		"$CERT_BACKEND" --verbose --module "$SOFTHSM_MODULE" --allow-software-tokens \
			--pin-prompt "$PIN_PROMPT" >"$LOGDIR/certificate.log" 2>&1 &
	fi
	CE=$!
	xdp_wait_for_name org.freedesktop.impl.portal.desktop.certificate "$CE" || {
		tail -20 "$LOGDIR/certificate.log" >&2
		echo "${0##*/}: the certificate backend never took its bus name"
		return 40
	}

	EXTERNAL_IDP=0
	[ "$RUN_E2E" = 0 ] && EXTERNAL_IDP=1
	for a in "${E2E_ARGS[@]}"; do
		case "$a" in --entra-authorize|--start-uri|--start-uri=*) EXTERNAL_IDP=1 ;; esac
	done

	rm -f "$LOGDIR/port" "$LOGDIR/access.log"
	# The self-signed fixture client certificate is the server's trust anchor.
	[ "$EXTERNAL_IDP" = 1 ] || python3 "$REPO/tools/mtls-server.py" \
		--cert "$LOGDIR/server.pem" --key "$LOGDIR/server.key" \
		--require-client-cert --ca "$CERT_SOFTHSM_DIR/$CERT_FIXTURE.pem" \
		--completion-uri "$COMPLETION_URI" \
		--access-log "$LOGDIR/access.log" --port-file "$LOGDIR/port" \
		>"$LOGDIR/server.log" 2>&1 &
	SERVER=$!
	[ "$EXTERNAL_IDP" = 1 ] && SERVER=""
	for _ in $(seq 1 40); do
		[ "$EXTERNAL_IDP" = 1 ] && break
		[ -s "$LOGDIR/port" ] && break
		kill -0 "$SERVER" 2>/dev/null || {
			cat "$LOGDIR/server.log" >&2
			echo "${0##*/}: the fixture server exited"
			return 40
		}
		sleep 0.25
	done
	PORT="$(cat "$LOGDIR/port" 2>/dev/null)"
	[ -n "$PORT" ] || [ "$EXTERNAL_IDP" = 1 ] || {
		echo "${0##*/}: the fixture server never reported a port"
		return 40
	}

	# An explicit portal provider makes unavailability fail the test.
	local backend_args=(--verbose --cert-adapter portal)
	# Pin the fixture server certificate only for localhost.
	[ "$EXTERNAL_IDP" = 1 ] || backend_args+=(--debug-trust-certificate "localhost=$LOGDIR/server.pem")
	[ "$MODE" = live ] && backend_args+=(--replace --allow-replacement)

	"$BACKEND" "${backend_args[@]}" >"$LOGDIR/backend.log" 2>&1 &
	BE=$!
	xdp_wait_for_name org.freedesktop.impl.portal.desktop.webauth "$BE" || {
		tail -20 "$LOGDIR/backend.log" >&2
		echo "${0##*/}: the webauth backend never took its bus name"
		return 40
	}

	rm -f "$LOGDIR/driver-stop"
	DRIVER=""
	if [ "$DRIVE" = 1 ] && [ "$MODE" = private ]; then
		"$LOGDIR/driver.sh" >"$LOGDIR/driver.log" 2>&1 &
		DRIVER=$!
	fi

	local e2e=(--wait 120000)
	if [ "$EXTERNAL_IDP" = 1 ]; then
		:
	else
		e2e+=(--start-uri "https://localhost:$PORT$START_PATH" --completion-uri "$COMPLETION_URI")
	fi
	[ -n "$SESSION_MODE" ] && e2e+=(--session-mode "$SESSION_MODE")

	if [ "$RUN_E2E" = 1 ]; then
		echo
		echo "=== Start #1 ==="
		python3 "$REPO/tools/webauth-e2e.py" "${e2e[@]}" "${E2E_ARGS[@]}" \
			2>&1 | tee "$LOGDIR/e2e.log"
		e2e_rc="${PIPESTATUS[0]}"
		[ "$e2e_rc" = 0 ] || rc=1
	else
		echo
		echo "${0##*/}: --no-e2e: stack ready for manual requests"
	fi

	# The module retains its grant until C_Finalize; count chooser reuse across requests.
	if [ "$SECOND_START" = 1 ]; then
		local before after
		before="$(grep -c 'grant-created' "$LOGDIR/certificate.log" 2>/dev/null | tail -1)"
		echo
		echo "=== Start #2, same backend process ==="
		python3 "$REPO/tools/webauth-e2e.py" "${e2e[@]}" "${E2E_ARGS[@]}" \
			2>&1 | tee "$LOGDIR/e2e-2.log"
		[ "${PIPESTATUS[0]}" = 0 ] || rc=1
		after="$(grep -c 'grant-created' "$LOGDIR/certificate.log" 2>/dev/null | tail -1)"
		echo
		echo "second Start: grants before=$before after=$after"
	fi

	if [ "$KEEP" = 1 ]; then
		echo
		echo "${0##*/}: --keep: stack up on $DBUS_SESSION_BUS_ADDRESS"
		if [ "$EXTERNAL_IDP" = 1 ]; then
			echo "  no fixture identity provider: the caller names its own"
			echo "  DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS"
		else
			echo "  start URI https://localhost:$PORT$START_PATH"
		fi
		wait "$FE"
	fi

	touch "$LOGDIR/driver-stop"
	kill ${DRIVER:+"$DRIVER"} "$BE" "$CE" "$FE" ${SERVER:+"$SERVER"} ${PERM:+"$PERM"} \
		${PROMPTER:+"$PROMPTER"} 2>/dev/null
	sleep 1

	if [ "$RUN_E2E" = 0 ]; then
		echo
		echo "${0##*/}: --no-e2e: integration checks skipped"
		echo "logs in $LOGDIR"
		return "$rc"
	fi

	echo
	echo "=== hop by hop ==="
	if [ "$CANCEL_CHOOSER" = 1 ]; then
		expect_log "WebKit certificate challenge" "$LOGDIR/backend.log" \
			'certificate-challenge host=localhost' || rc=1
		expect_log "Chooser cancellation" "$LOGDIR/certificate.log" \
			'chooser-cancelled' || rc=1
		expect_log "Certificate challenge declined" "$LOGDIR/backend.log" \
			'certificate-declined reason=credential_unavailable' || rc=1
		expect_log "Credential-unavailable response" "$LOGDIR/e2e.log" \
			'Response reason=credential_unavailable' || rc=1
		expect_log "Process hardening restored" "$LOGDIR/backend.log" \
			'process-hardening outcome=identifiable-end' || rc=1
		if grep -qE 'certificate-answered' "$LOGDIR/backend.log"; then
			echo "  FAIL  a certificate was answered after a cancelled chooser"
			rc=1
		fi
	elif [ "$EXTERNAL_IDP" = 1 ]; then
		expect_log "WebKit certificate challenge" "$LOGDIR/backend.log" \
			'certificate-challenge host=' || rc=1
		expect_log "Portal provider selected" "$LOGDIR/backend.log" \
			'certificate-challenge provider=portal' || rc=1
		expect_log "Network process grant acquired" "$LOGDIR/backend.log" \
			'^\(process:[0-9]+\): pkcs11-portal-certificate-DEBUG.*grant acquired' || rc=1
		expect_log "Certificate grant created" "$LOGDIR/certificate.log" \
			'grant-created' || rc=1
		expect_log "Token PIN accepted" "$LOGDIR/certificate.log" \
			'login-ok' || rc=1
		expect_log "Token signature completed" "$LOGDIR/certificate.log" \
			'operation-completed' || rc=1
		expect_log "Portal certificate supplied" "$LOGDIR/backend.log" \
			'certificate-answered provider=portal' || rc=1
		expect_log "Authentication completed" "$LOGDIR/e2e.log" '^PASS$' || rc=1
		echo "  info  certificate host(s): $(grep -oE 'certificate-challenge host=[^ ]+' "$LOGDIR/backend.log" 2>/dev/null | sort -u | sed 's/.*host=//' | tr '\n' ' ')"
	else
		expect_log "WebKit certificate challenge" "$LOGDIR/backend.log" \
			'certificate-challenge host=localhost' || rc=1
		expect_log "Portal provider selected" "$LOGDIR/backend.log" \
			'certificate-challenge provider=portal' || rc=1
		expect_log "Process identification enabled" "$LOGDIR/backend.log" \
			'process-hardening outcome=identifiable-begin' || rc=1
		expect_log "Backend module loaded" "$LOGDIR/backend.log" \
			'xdg-desktop-portal-webauth:[0-9]+\): pkcs11-portal-certificate-DEBUG' || rc=1
		expect_log "Backend grant acquired" "$LOGDIR/backend.log" \
			'xdg-desktop-portal-webauth:[0-9]+\): pkcs11-portal-certificate-DEBUG.*grant acquired' ||
			rc=1
		# The network process is a separate D-Bus peer and acquires its own grant.
		expect_log "Network process grant acquired" "$LOGDIR/backend.log" \
			'^\(process:[0-9]+\): pkcs11-portal-certificate-DEBUG.*grant acquired' || rc=1
		# Unsandboxed callers without desktop files have unidentified application identity.
		expect_log "Frontend caller identity" "$LOGDIR/certificate.log" \
			'chooser-shown app_id=\(none\) identity=unidentified' || rc=1
		expect_log "Certificate grant created" "$LOGDIR/certificate.log" \
			'grant-created' || rc=1
		expect_log "Token PIN accepted" "$LOGDIR/certificate.log" \
			'login-ok' || rc=1
		expect_log "Token signature completed" "$LOGDIR/certificate.log" \
			'operation-completed' || rc=1
		expect_log "Process hardening restored" "$LOGDIR/backend.log" \
			'process-hardening outcome=identifiable-end' || rc=1
		expect_log "Portal certificate supplied" "$LOGDIR/backend.log" \
			'certificate-answered provider=portal' || rc=1
		expect_log "Server received fixture certificate" "$LOGDIR/server.log" \
			'client-cn=Portal Test User' || rc=1
		expect_log "Authentication completed" "$LOGDIR/e2e.log" '^PASS$' || rc=1
		check_completion_was_not_fetched || rc=1
	fi

	echo
	echo "module instances: $(grep -c 'the Certificate portal offers' "$LOGDIR/backend.log" 2>/dev/null | tail -1)"
	# Each handshake needs two choosers; an extra chooser indicates acquisition during chain verification.
	echo "choosers granted: $(grep -c 'grant-created' "$LOGDIR/certificate.log" 2>/dev/null | tail -1)"
	# Only the network process signs, so the first Sign prompts for the PIN.
	echo "PIN prompts:      $(grep -c 'login-ok' "$LOGDIR/certificate.log" 2>/dev/null | tail -1)"
	echo "signatures:       $(grep -c 'operation-completed' "$LOGDIR/certificate.log" 2>/dev/null | tail -1)"

	return "$rc"
}

XDG_DESKTOP_PORTAL_DIR="$DEVDIR"
# Live mode keeps the operator's XDG_CONFIG_HOME.
[ "$MODE" = private ] && XDG_CONFIG_HOME="$CONFDIR"

# Private mode uses Xvfb and xdotool; live mode uses the current display.
if [ "$MODE" = private ]; then
	"$XVFB" "$SCREEN" -screen 0 1280x1024x24 -nolisten tcp >"$LOGDIR/xvfb.log" 2>&1 &
	XVFB_PID=$!
	sleep 2

	cleanup() {
		kill "$XVFB_PID" 2>/dev/null
		wait "$XVFB_PID" 2>/dev/null
	}
	trap cleanup EXIT

	DISPLAY="$SCREEN"
	unset WAYLAND_DISPLAY
	export GDK_BACKEND=x11
	export GTK_A11Y=none
	# Keep fixture window appearance independent of desktop settings.
	export ADW_DEBUG_COLOR_SCHEME="${ADW_DEBUG_COLOR_SCHEME:-prefer-dark}"
else
	[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] ||
		die "Display unavailable; --live requires a desktop session."
fi

export DISPLAY XDG_DESKTOP_PORTAL_DIR XDG_CONFIG_HOME
export XDG_CURRENT_DESKTOP="${XDG_CURRENT_DESKTOP:-dev}"
export SOFTHSM2_CONF="$CERT_SOFTHSM_DIR/softhsm2.conf"
export XDG_DATA_HOME="$LOGDIR/data-home"
mkdir -p "$XDG_DATA_HOME"
# Module environment variables supply the authentication purpose and request reason to PKCS#11 consumers.
export PKCS11_PORTAL_CERTIFICATE_PURPOSE=client_auth
export PKCS11_PORTAL_CERTIFICATE_REASON="Sign in to the fixture identity provider"
# G_MESSAGES_DEBUG uses spaces; include both backend and module domains because it overrides verbose defaults.
export G_MESSAGES_DEBUG="webauth pkcs11-portal-certificate"
# Restrict fixture key algorithms so the chooser and server trust anchor select the same certificate.
[ "$MODE" = private ] && export PKCS11_PORTAL_CERTIFICATE_KEY_ALGORITHMS="$KEY_ALGORITHM"

export PIN LOGDIR REPO XDP_BUILD BACKEND CERT_BACKEND CERT_BUILD CERT_MODULE_SO \
	CERT_SOFTHSM_DIR CERT_FIXTURE SOFTHSM_MODULE XDOTOOL DRIVE PIN_PROMPT MODE \
	COMPLETION_URI START_PATH SESSION_MODE SECOND_START CANCEL_CHOOSER KEEP RUN_E2E

# Export e2e arguments as newline-separated text because shell arrays cannot be exported.
E2E_JOINED=""
[ "${#E2E_ARGS[@]}" -gt 0 ] && E2E_JOINED="$(printf '%s\n' "${E2E_ARGS[@]}")"
export E2E_JOINED

body() {
	mapfile -t E2E_ARGS < <(printf '%s' "${E2E_JOINED:-}" | sed '/^$/d')
	inner
}

if [ "$MODE" = live ]; then
	echo "${0##*/}: --live: taking org.freedesktop.portal.Desktop on the real session bus"
	body
	rc=$?
else
	dbus-run-session -- bash -c "
		$(declare -f xdp_experimental_portals)
		$(declare -f xdp_wait_for_name)
		$(declare -f expect_log)
		$(declare -f check_completion_was_not_fetched)
		$(declare -f inner)
		$(declare -f body)
		body"
	rc=$?
fi

echo
if [ "$rc" = 0 ]; then
	echo "portal-stack: PASS; logs in $LOGDIR"
else
	echo "portal-stack: FAIL ($rc); logs in $LOGDIR"
fi
exit "$rc"
