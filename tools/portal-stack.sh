#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
#
# portal-stack.sh -- BOTH PORTALS AT ONCE, and a card behind the web view.
#
# tools/ui-smoke.sh proves this backend can answer a TLS client-certificate
# challenge from a token it was handed the URI of. This proves the thing the
# project is actually for: that the token can be the CERTIFICATE PORTAL'S, that
# the card, the chooser and the PIN stay in that service, and that nothing in
# this process ever sees a PIN.
#
# WHAT IT STANDS UP, all on ONE private bus inside ONE headless X server:
#
#   xdg-permission-store        the frontend refuses to start without it
#   xdg-desktop-portal          the development frontend, BOTH gates on:
#                               XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=certificate,web-authentication
#   xdg-desktop-portal-certificate
#                               the sibling backend, against its SoftHSM fixture
#   xdg-desktop-portal-webauth  this backend, --cert-adapter portal
#   tools/mtls-server.py        an identity provider that DEMANDS a client
#                               certificate, trusting the fixture card and
#                               nothing else
#   tools/webauth-e2e.py        an application calling the PUBLIC interface
#
# and a p11-kit module directory of this run's own, holding one .module file
# that names the certificate portal's client-side module. Nothing is installed
# system wide and the machine's own p11-kit configuration is untouched.
#
# THE HOPS IT PROVES, each from a log line rather than from the exit code:
#
#   1  webkit `authenticate`, CLIENT_CERTIFICATE_REQUESTED   backend.log
#   2  tls/client_cert_portal.c builds the GTlsCertificate    backend.log
#   3  p11-kit loads the module -- in the BACKEND's process,
#      to build the certificate, and again in WebKit's
#      NETWORK process, to use the key                        backend.log (module debug)
#   4  the module calls CreateSession/AcquireCredential on
#      the PUBLIC interface                                   frontend.log
#   5  the certificate backend shows its chooser, the driver
#      picks, a grant is created                              certificate.log
#   6  GnuTLS C_SignInit/C_Sign -> the module's Sign -> the
#      PIN prompt -> a signature                              certificate.log
#   7  the handshake completes and the server sees the CN     server.log
#   8  the flow redirects and the completion URI is captured  e2e.log
#   9  THE COMPLETION URI WAS NEVER FETCHED                   access.log
#
# WHAT IT NEEDS, none of which has to be installed system wide:
#
#   Xvfb        xorg-x11-server-Xvfb           $XVFB
#   xdotool     xdotool                        $XDOTOOL
#   $CERTIFICATE_REPO   a built xdg-desktop-portal-certificate, with its module.
#                       Default ../xdg-desktop-portal-certificate.
#   $CERT_SOFTHSM_DIR   that repository's fixture, from its
#                       tools/softhsm-fixture.sh. Default
#                       ${TMPDIR:-/tmp}/xdp-certificate-softhsm.
#   $XDP_BUILD          a built frontend, as tools/dev-stack.sh describes.
#   $BACKEND            this repository's backend.
#
#     tools/portal-stack.sh                     # the joint run
#     tools/portal-stack.sh --pin-prompt=system # no window for the PIN at all
#     tools/portal-stack.sh --cancel-chooser    # Escape at the chooser
#     tools/portal-stack.sh --second-start      # two Starts, one backend process
#     tools/portal-stack.sh --keep              # leave it up
#     tools/portal-stack.sh --uninstall-module  # remove --live's p11-kit module file
#
# Everything after `--` goes to tools/webauth-e2e.py.
#
# THE PIN GOES THROUGH THE ENVIRONMENT, NOT ARGV. /proc/*/cmdline is readable by
# every user on the machine and /proc/*/environ is not. It is the fixture PIN
# today; it is also the line anyone adapting this script for a card will copy.
#
# A TEST THAT HAS ONLY BEEN RUN AGAINST A SOFTWARE TOKEN HAS NOT BEEN RUN. This
# is the rehearsal; docs/TESTING.md's "Live run against Entra" is the command
# the author runs against the real card and the real identity provider.
#
#     tools/portal-stack.sh --live --pin-prompt=system -- --start-uri ...
#
# --live uses the REAL session bus and the REAL certificate backend, takes
# org.freedesktop.portal.Desktop for the duration, and drives nothing: the
# person at the keyboard answers the chooser and the PIN prompt.
#
# --live also installs, the first time it runs and only if nothing is already
# there, a p11-kit module file into the REAL per-user config
# ($XDG_CONFIG_HOME/pkcs11/modules, default ~/.config/pkcs11/modules) naming
# the certificate portal's client-side module -- restricted with `enable-in`
# to the two processes that need it, this backend and WebKit's network
# process, so it is never offered to ssh, curl, a browser, or anything else
# on the machine. It is left in place after the run so the next --live does
# not have to ask again; `--uninstall-module` removes exactly that file, and
# only if this script is the one that wrote it.

set -u

here() { cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd; }
REPO="$(here)"

XDP_BUILD="${XDP_BUILD:-$REPO/../xdg-desktop-portal/build}"
BACKEND="${BACKEND:-$REPO/build-backend/src/xdg-desktop-portal-webauth}"
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
	sed -n '3,90p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
	exit 0
}

# ------------------------------------------------------------ --live's real p11-kit module
#
# INSTALLED, NOT ENVIRONMENT-OVERRIDDEN, and only for --live. The private-mode
# CONFDIR below points $XDG_CONFIG_HOME at a throwaway directory, which is a
# rehearsal shortcut: it proves the module loads, not that a real deployment's
# p11-kit configuration is set up correctly. --live leaves $XDG_CONFIG_HOME as
# the real environment's value, so this writes the module file p11-kit would
# actually read: $XDG_CONFIG_HOME/pkcs11/modules (falling back to
# ~/.config/pkcs11/modules the same way p11-kit's own
# common/path.c:expand_homedir() does), created 0700, file 0600, and only if
# absent.
#
# enable-in, NOT disable-in, and that is a deliberate departure from the
# sibling's shipped module file: pkcs11.conf(5) says "Do not specify both
# enable-in and disable-in for the same module", and p11-kit's
# is_module_enabled_unlocked() (p11-kit/modules.c) takes the enable-in branch
# whenever both are configured and never consults disable-in at all -- with
# both set, disable-in is silently dead weight. An enable-in allowlist of
# exactly the two processes that need this module is strictly narrower than
# any disable-in list could be: it already excludes xdg-desktop-portal,
# xdg-desktop-portal-certificate, p11-kit-server, and every other p11-kit
# consumer on the machine -- ssh, curl, browsers included -- which is the
# whole point of not installing this globally.
#
# The two names are matched by p11-kit as the BASE NAME OF argv[0]
# (p11-kit/util.c:_p11_get_progname_unlocked -> common/compat.c's
# getprogname(), which on glibc/Linux reads program_invocation_short_name,
# with a fallback that resolves /proc/self/exe when argv[0] is an absolute
# path -- see docs/TESTING.md for how this was verified on this machine).
# "xdg-desktop-portal-webauth" is this backend's own built binary name;
# "WebKitNetworkProcess" is the literal executable WebKitGTK execs for its
# network process (/usr/libexec/webkit2gtk-*/WebKitNetworkProcess).
MODULE_MARKER="# Installed by tools/portal-stack.sh --live."
MODULE_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/pkcs11/modules"
MODULE_FILE="$MODULE_DIR/xdg-desktop-portal-certificate.module"

install_module_file() {
	if [ -e "$MODULE_FILE" ]; then
		if head -1 -- "$MODULE_FILE" | grep -qF "$MODULE_MARKER"; then
			echo "${0##*/}: $MODULE_FILE already installed"
			return 0
		fi
		die "$MODULE_FILE already exists and was not written by this script; refusing to
touch it. If it is meant to be this module, remove it yourself, or point
CERTIFICATE_REPO/XDG_CONFIG_HOME elsewhere:
    $MODULE_FILE"
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
# See docs/TESTING.md "Live run against Entra" and the sibling's
# docs/decisions/0011-client-side-pkcs11-module.md. Installed: $(date +%F)
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
		die "$MODULE_FILE was not written by this script (its first line does not match);
refusing to remove it:
    $MODULE_FILE"
	rm -f -- "$MODULE_FILE" || die "could not remove $MODULE_FILE"
	echo "${0##*/}: removed $MODULE_FILE"
}

while [ $# -gt 0 ]; do
	case "$1" in
	--live) MODE=live ;;
	--pin-prompt=*) PIN_PROMPT="${1#--pin-prompt=}" ;;
	--no-drive) DRIVE=0 ;;
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

# Independent of everything else this script sets up: no LOGDIR, no build
# checks, no display. It only touches $MODULE_FILE.
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
# THE FIXTURE BELONGS TO THE SIBLING REPOSITORY, so it carries the sibling's
# marker and is checked against that name -- ownership, containment under
# $TMPDIR, no symlinks on the path, 0700, a 0600 marker naming the fixture. The
# module path inside it is a file the certificate backend dlopen()s, which is
# why it is checked at all rather than merely tested for.
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

# ------------------------------------------------------------ the portal directory
#
# BOTH .portal FILES AND BOTH LINES IN portals.conf. XDG_DESKTOP_PORTAL_DIR
# makes the frontend ignore every other portal directory AND every other
# portals.conf on the machine, so the directory has to be a copy of the
# machine's with two lines added rather than a directory holding only ours.
DEVDIR="$LOGDIR/portals"
(umask 077 && mkdir -p "$DEVDIR")
xdp_write_portal_dir "$DEVDIR" "$REPO" "$MODE"

rm -f -- "$DEVDIR/certificate.portal"
sed -e '/^#/d' -e '/^$/d' "$CERTIFICATE_REPO/data/certificate.portal.in" \
	>"$DEVDIR/certificate.portal" || die "no certificate.portal.in in $CERTIFICATE_REPO"
xdp_conf_set "$DEVDIR/portals.conf" \
	org.freedesktop.impl.portal.experimental.Certificate 'certificate;'

# ------------------------------------------------------------ p11-kit, for this run only
#
# PRIVATE MODE ONLY. THE PROCESS THAT RESOLVES THE URI IS NOT THIS SCRIPT AND
# NOT THE FRONTEND. The certificate is built in the BACKEND's process and used
# in WebKit's NETWORK process, which is a child of it; both find the module
# only because p11-kit reads $XDG_CONFIG_HOME/pkcs11/modules and the backend is
# started with that variable pointed here. `module:` is absolute because a
# build tree is not p11-kit's module directory. `disable-in` is fine here,
# unlike in the real per-user file install_module_file() writes for --live:
# this directory holds nothing else that enable-in would need to coexist
# with, and it is only ever read by the processes this script itself starts.
#
# --live does NOT do this: it leaves $XDG_CONFIG_HOME as the real
# environment's value and relies on install_module_file(), above, having put
# the module where p11-kit's real configuration actually looks.
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

# ------------------------------------------------------------ the server's own certificate
#
# Throwaway, and nothing on the machine trusts it -- correctly. The backend is
# told to trust it for localhost and nothing else; see src/webkit-session.h for
# why that option exists and why it is the only trust override in the binary.
# What is under test is the CLIENT's certificate, which is the card's.
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=localhost \
	-addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
	-keyout "$LOGDIR/server.key" -out "$LOGDIR/server.pem" >"$LOGDIR/openssl.log" 2>&1 ||
	die "could not make a server certificate; see $LOGDIR/openssl.log"

# ------------------------------------------------------------ the person at the keyboard
#
# THE SAME DRIVER LOOP AS THE SIBLING'S tools/ui-smoke.sh, in its module-smoke
# form: each window is answered once and retried if it is still up ten seconds
# later, because with no window manager a focus set before the window was mapped
# goes nowhere. A loop rather than a fixed script, because how MANY choosers
# appear is one of the things this run is here to find out.
cat >"$LOGDIR/driver.sh" <<'DRIVER_EOF'
#!/bin/bash
set -u
declare -A driven

press() {
	local wid="$1"
	shift
	"$XDOTOOL" windowfocus "$wid" 2>/dev/null
	sleep 1
	for key in "$@"; do
		case "$key" in
		# THE PIN IS NOT AN ARGUMENT. xdotool --file - reads what to type from
		# stdin, so it never appears in /proc/*/cmdline.
		type:) printf '%s' "$PIN" | "$XDOTOOL" type --delay 60 --file - ;;
		*) "$XDOTOOL" key "$key" ;;
		esac
		sleep 0.8
	done
}

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
	sleep 1
done
DRIVER_EOF
chmod +x "$LOGDIR/driver.sh"

# ------------------------------------------------------------ the hop-by-hop proof
#
# EACH HOP IS A LOG LINE FROM THE PROCESS THAT MADE IT, not an inference from an
# exit code. A run that "passed" because nothing checked which half did the work
# is the exact mistake this file exists to avoid: the pkcs11 provider would
# complete the same handshake with no portal in the picture at all.
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

	# THE FRONTEND GOES UP FIRST. GTK asks the settings portal for a colour
	# scheme as soon as a backend initialises it, which on a private bus
	# ACTIVATES org.freedesktop.portal.Desktop -- and activation finds the SYSTEM
	# portal at /usr/libexec, which then owns the name this script's frontend was
	# about to take.
	if [ "$MODE" = live ]; then
		"$XDP_BUILD/desktop-portal/xdg-desktop-portal" -r -v >"$LOGDIR/frontend.log" 2>&1 &
	else
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

	# THE CERTIFICATE BACKEND, on the fixture. --live drops --module and
	# --allow-software-tokens: there the card is real and the backend finds it
	# through p11-kit the way it does on any machine.
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

	# THE FIXTURE IDENTITY PROVIDER. Its CA is the fixture card's own
	# certificate: it is a self-signed leaf, so it is its own anchor, and a
	# handshake completes only if the certificate the module handed over is that
	# one. Nothing else on the machine would satisfy it.
	rm -f "$LOGDIR/port" "$LOGDIR/access.log"
	python3 "$REPO/tools/mtls-server.py" \
		--cert "$LOGDIR/server.pem" --key "$LOGDIR/server.key" \
		--require-client-cert --ca "$CERT_SOFTHSM_DIR/$CERT_FIXTURE.pem" \
		--completion-uri "$COMPLETION_URI" \
		--access-log "$LOGDIR/access.log" --port-file "$LOGDIR/port" \
		>"$LOGDIR/server.log" 2>&1 &
	SERVER=$!
	for _ in $(seq 1 40); do
		[ -s "$LOGDIR/port" ] && break
		kill -0 "$SERVER" 2>/dev/null || {
			cat "$LOGDIR/server.log" >&2
			echo "${0##*/}: the fixture server exited"
			return 40
		}
		sleep 0.25
	done
	PORT="$(cat "$LOGDIR/port" 2>/dev/null)"
	[ -n "$PORT" ] || {
		echo "${0##*/}: the fixture server never reported a port"
		return 40
	}

	# THIS BACKEND, WITH THE PORTAL PROVIDER NAMED. --cert-adapter portal rather
	# than auto, so that a run in which the portal provider is unavailable FAILS
	# instead of quietly falling through to the pkcs11 one and proving nothing.
	local backend_args=(
		--verbose
		--cert-adapter portal
		--debug-trust-certificate "localhost=$LOGDIR/server.pem"
	)
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

	local e2e=(
		--start-uri "https://localhost:$PORT$START_PATH"
		--completion-uri "$COMPLETION_URI"
		--wait 120000
	)
	[ -n "$SESSION_MODE" ] && e2e+=(--session-mode "$SESSION_MODE")

	echo
	echo "=== Start #1 ==="
	python3 "$REPO/tools/webauth-e2e.py" "${e2e[@]}" "${E2E_ARGS[@]}" \
		2>&1 | tee "$LOGDIR/e2e.log"
	e2e_rc="${PIPESTATUS[0]}"
	[ "$e2e_rc" = 0 ] || rc=1

	# A SECOND Start AGAINST THE SAME BACKEND PROCESS. The module keeps its grant
	# until C_Finalize, so the question is whether a second handshake reuses it
	# or provokes a second chooser -- and the answer depends on which PROCESS
	# resolves the URI the second time. Counted, not assumed.
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
		echo "second Start: grants before=$before after=$after (a new chooser is a new grant)"
	fi

	if [ "$KEEP" = 1 ]; then
		echo
		echo "${0##*/}: --keep: stack up on $DBUS_SESSION_BUS_ADDRESS"
		echo "  start URI https://localhost:$PORT$START_PATH"
		wait "$FE"
	fi

	touch "$LOGDIR/driver-stop"
	kill ${DRIVER:+"$DRIVER"} "$BE" "$CE" "$FE" "$SERVER" ${PERM:+"$PERM"} \
		${PROMPTER:+"$PROMPTER"} 2>/dev/null
	sleep 1

	echo
	echo "=== hop by hop ==="
	if [ "$CANCEL_CHOOSER" = 1 ]; then
		expect_log "1 webkit asked for a certificate" "$LOGDIR/backend.log" \
			'certificate-challenge host=localhost' || rc=1
		expect_log "5 the chooser was refused" "$LOGDIR/certificate.log" \
			'chooser-cancelled' || rc=1
		expect_log "the challenge was declined" "$LOGDIR/backend.log" \
			'certificate-declined reason=no_certificate_adapter' || rc=1
		expect_log "the window closed on an answer" "$LOGDIR/e2e.log" \
			'Response reason=no_certificate_adapter' || rc=1
		expect_log "the hardening window closed again" "$LOGDIR/backend.log" \
			'process-hardening outcome=identifiable-end' || rc=1
		if grep -qE 'certificate-answered' "$LOGDIR/backend.log"; then
			echo "  FAIL  a certificate was answered after a cancelled chooser"
			rc=1
		fi
	else
		expect_log "1 webkit asked for a certificate" "$LOGDIR/backend.log" \
			'certificate-challenge host=localhost' || rc=1
		expect_log "2 the portal provider took it" "$LOGDIR/backend.log" \
			'certificate-challenge provider=portal' || rc=1
		expect_log "2 this process became identifiable" "$LOGDIR/backend.log" \
			'process-hardening outcome=identifiable-begin' || rc=1
		expect_log "3 p11-kit loaded the module here" "$LOGDIR/backend.log" \
			'xdg-desktop-portal-webauth:[0-9]+\): pkcs11-portal-certificate-DEBUG' || rc=1
		expect_log "3 the module got a credential here" "$LOGDIR/backend.log" \
			'xdg-desktop-portal-webauth:[0-9]+\): pkcs11-portal-certificate-DEBUG.*grant acquired' ||
			rc=1
		expect_log "3 the module ran in the network process" "$LOGDIR/backend.log" \
			'^\(process:[0-9]+\): pkcs11-portal-certificate-DEBUG.*grant acquired' || rc=1
		# The frontend derived the caller's identity and forwarded it: the
		# certificate backend only ever sees an app id the FRONTEND resolved, so
		# a chooser naming one is proof the public call was routed. Both callers
		# are unsandboxed processes with no .desktop file, so "unidentified" is
		# the right answer and not a failure.
		expect_log "4 the frontend identified the caller" "$LOGDIR/certificate.log" \
			'chooser-shown app_id=\(none\) identity=unidentified' || rc=1
		expect_log "5 the backend created a grant" "$LOGDIR/certificate.log" \
			'grant-created' || rc=1
		expect_log "6 the PIN was accepted" "$LOGDIR/certificate.log" \
			'login-ok' || rc=1
		expect_log "6 the signature was produced" "$LOGDIR/certificate.log" \
			'operation-completed' || rc=1
		expect_log "2 the hardening window closed" "$LOGDIR/backend.log" \
			'process-hardening outcome=identifiable-end' || rc=1
		expect_log "2 this backend answered from portal" "$LOGDIR/backend.log" \
			'certificate-answered provider=portal' || rc=1
		expect_log "7 the server saw the card's CN" "$LOGDIR/server.log" \
			'client-cn=Portal Test User' || rc=1
		expect_log "8 the flow completed" "$LOGDIR/e2e.log" '^PASS$' || rc=1
		check_completion_was_not_fetched || rc=1
	fi

	echo
	# HOW MANY WINDOWS A USER ACTUALLY SAW, counted rather than assumed. One
	# handshake needs the certificate built in the BACKEND's process and the key
	# used in WebKit's NETWORK process; each is a separate p11-kit module
	# instance with a grant of its own, so each puts up its own chooser. The PIN
	# is asked once, at the first Sign, because only the network process signs.
	echo "module instances: $(grep -c 'the Certificate portal offers' "$LOGDIR/backend.log" 2>/dev/null | tail -1)"
	echo "choosers granted: $(grep -c 'grant-created' "$LOGDIR/certificate.log" 2>/dev/null | tail -1)"
	echo "PIN prompts:      $(grep -c 'login-ok' "$LOGDIR/certificate.log" 2>/dev/null | tail -1)"
	echo "signatures:       $(grep -c 'operation-completed' "$LOGDIR/certificate.log" 2>/dev/null | tail -1)"

	return "$rc"
}

# ------------------------------------------------------------ the display
#
# X11 rather than Wayland, because a headless X server is something a machine
# with no compositor can start and xdotool can drive. --live uses the desktop
# the person running it is sitting at.
XDG_DESKTOP_PORTAL_DIR="$DEVDIR"
# Private mode only: --live leaves $XDG_CONFIG_HOME as the real environment's
# value, see install_module_file() and the p11-kit comment above.
[ "$MODE" = private ] && XDG_CONFIG_HOME="$CONFDIR"

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
	# The windows must not depend on what the machine running the test has its
	# desktop set to.
	export ADW_DEBUG_COLOR_SCHEME="${ADW_DEBUG_COLOR_SCHEME:-prefer-dark}"
else
	[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] ||
		die "no display. --live needs the desktop you are sitting at."
fi

# THE ENVIRONMENT EVERY PROCESS IN THE STACK SHARES. Two of these are the whole
# integration:
#
#   XDG_CONFIG_HOME    where p11-kit finds the certificate portal's module, in
#                      THIS backend's process and in WebKit's network process,
#                      which inherits it. In private mode this points at the
#                      throwaway CONFDIR above; in --live it is left as the
#                      real environment's value, and install_module_file()
#                      put the module where that real value actually points.
#   XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL
#                      BOTH gates. Neither portal is exported without its name.
#
# The PKCS11_PORTAL_CERTIFICATE_* variables are the only channel a module loaded
# by p11-kit has: a PKCS#11 consumer cannot say "RSA only" or "here is why I am
# asking". KEY_ALGORITHMS is pinned so the chooser has one row and the fixture
# server's trust anchor is known in advance.
export DISPLAY XDG_DESKTOP_PORTAL_DIR XDG_CONFIG_HOME
export XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=certificate,web-authentication
export XDG_CURRENT_DESKTOP="${XDG_CURRENT_DESKTOP:-dev}"
export SOFTHSM2_CONF="$CERT_SOFTHSM_DIR/softhsm2.conf"
export XDG_DATA_HOME="$LOGDIR/data-home"
mkdir -p "$XDG_DATA_HOME"
export PKCS11_PORTAL_CERTIFICATE_PURPOSE=client_auth
export PKCS11_PORTAL_CERTIFICATE_REASON="Sign in to the fixture identity provider"
# SPACE separated, not comma: GLib splits $G_MESSAGES_DEBUG on spaces.
# "webauth" as well as the module's domain, because --verbose stops
# choosing the domains itself as soon as this variable is set.
export G_MESSAGES_DEBUG="webauth pkcs11-portal-certificate"
[ "$MODE" = private ] && export PKCS11_PORTAL_CERTIFICATE_KEY_ALGORITHMS="$KEY_ALGORITHM"

export PIN LOGDIR REPO XDP_BUILD BACKEND CERT_BACKEND CERT_BUILD CERT_MODULE_SO \
	CERT_SOFTHSM_DIR CERT_FIXTURE SOFTHSM_MODULE XDOTOOL DRIVE PIN_PROMPT MODE \
	COMPLETION_URI START_PATH SESSION_MODE SECOND_START CANCEL_CHOOSER KEEP

# The e2e arguments cross into the dbus-run-session shell as one newline
# separated string, because an array does not survive an export.
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
