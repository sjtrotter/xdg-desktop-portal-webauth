#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
#
# Adapted from the sibling backend xdg-desktop-portal-certificate's
# tools/softhsm-fixture.sh (same author, same licence): the token now
# holds ONE client certificate, and the fixture also issues the CA and the
# server certificate tools/mtls-server.py needs, because here the point is a
# mutual-TLS handshake rather than a signature.
#
# It builds, in one throwaway directory nothing else on the machine sees:
#
#   ca.pem              the CA that signs both ends of the fixture
#   server.pem/.key     what tools/mtls-server.py listens with
#   tokens/             a SoftHSM token holding the client certificate and its
#                       private key, non-extractable, behind a PIN
#   pin                 that PIN, mode 0600, which is how the backend is given
#                       it: a PIN on a command line is world-readable
#   pkcs11/modules/     a p11-kit module directory naming libsofthsm2.so, to be
#                       used as $XDG_CONFIG_HOME so that the token is visible to
#                       GnuTLS -- and therefore to WebKit's network process --
#                       without touching the machine's own configuration
#
# A TEST THAT HAS ONLY BEEN RUN AGAINST A SOFTWARE TOKEN HAS NOT BEEN RUN. This
# is the rehearsal; docs/TESTING.md lists what only a card can answer.
#
#     tools/softhsm-fixture.sh              # create it
#     tools/softhsm-fixture.sh --clean      # remove it
#
#   $SOFTHSM_DIR     where to put it. Default ${TMPDIR:-/tmp}/xdp-webauth-softhsm
#   $SOFTHSM_MODULE  path to libsofthsm2.so. Searched for if unset.
#   $SOFTHSM_UTIL    path to softhsm2-util. Searched for if unset.
#
# THE DIRECTORY IS CHECKED BEFORE IT IS USED, AND BEFORE IT IS DELETED, by
# tools/lib.sh: ownership, containment under $TMPDIR, no symlinks on the path,
# and this project's marker file. An `rm -rf` gated on ownership alone is an
# `rm -rf` a mistyped SOFTHSM_DIR aims at $HOME.
#
# The PIN is 123456 and it is a test PIN in a scratch directory; do not reuse it.

set -eu

SOFTHSM_DIR="${SOFTHSM_DIR:-${TMPDIR:-/tmp}/xdp-webauth-softhsm}"
PIN="${PIN:-123456}"
SO_PIN="${SO_PIN:-3737}"
LABEL="${TOKEN_LABEL:-Portal WebAuth Token}"
OBJECT="${OBJECT_LABEL:-portal-client}"

die() {
	echo "${0##*/}: $*" >&2
	exit 40
}

# shellcheck source=tools/lib.sh
. "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/lib.sh"

find_module() {
	if [ -n "${SOFTHSM_MODULE:-}" ]; then
		echo "$SOFTHSM_MODULE"
		return
	fi

	for candidate in \
		/usr/lib64/pkcs11/libsofthsm2.so \
		/usr/lib64/softhsm/libsofthsm2.so \
		/usr/lib/softhsm/libsofthsm2.so \
		/usr/lib/x86_64-linux-gnu/softhsm/libsofthsm2.so; do
		[ -f "$candidate" ] && {
			echo "$candidate"
			return
		}
	done

	die "libsofthsm2.so not found. Install softhsm, or set SOFTHSM_MODULE.
Without root, 'dnf download softhsm' and 'rpm2cpio ... | cpio -idmu' into a
scratch directory is enough: nothing here needs the package installed."
}

find_util() {
	if [ -n "${SOFTHSM_UTIL:-}" ]; then
		echo "$SOFTHSM_UTIL"
		return
	fi

	if command -v softhsm2-util >/dev/null; then
		command -v softhsm2-util
		return
	fi

	# Alongside the module, in an unpacked-rpm layout.
	candidate="$(dirname "$(dirname "$(dirname "$1")")")/bin/softhsm2-util"
	[ -x "$candidate" ] && {
		echo "$candidate"
		return
	}

	die "softhsm2-util not found; set SOFTHSM_UTIL or PATH"
}

if [ "${1:-}" = "--clean" ]; then
	fixture_remove "$SOFTHSM_DIR" softhsm
	echo "removed $SOFTHSM_DIR"
	exit 0
fi

command -v certtool >/dev/null || die "certtool not found (gnutls-utils)"
command -v pkcs11-tool >/dev/null || die "pkcs11-tool not found (opensc)"
command -v openssl >/dev/null || die "openssl not found"

MODULE="$(find_module)"
UTIL="$(find_util "$MODULE")"

fixture_remove "$SOFTHSM_DIR" softhsm
fixture_make "$SOFTHSM_DIR" softhsm
(umask 077 && mkdir -p "$SOFTHSM_DIR/tokens" "$SOFTHSM_DIR/pkcs11/modules")

cat >"$SOFTHSM_DIR/softhsm2.conf" <<EOF
directories.tokendir = $SOFTHSM_DIR/tokens
objectstore.backend = file
log.level = ERROR
slots.removable = false
EOF

printf '%s' "$MODULE" >"$SOFTHSM_DIR/module-path"

# THE MODULE HAS TO BE VISIBLE TO p11-kit, NOT JUST TO US. The certificate is
# resolved from a URI inside WebKit's NETWORK PROCESS, which is a child of the
# backend and has no idea what this script decided: it finds the token only
# because p11-kit reads $XDG_CONFIG_HOME/pkcs11/modules, and that variable is
# pointed here. The machine's own p11-kit configuration is not touched.
cat >"$SOFTHSM_DIR/pkcs11/modules/webauth-softhsm.module" <<EOF
module: $MODULE
critical: no
EOF

# The PIN goes in a file, not in argv and not in a URI's pin-value: the backend
# refuses a pin-value for exactly this reason, and /proc/*/cmdline is readable
# by every user on the machine.
(umask 177 && printf '%s' "$PIN" >"$SOFTHSM_DIR/pin")

export SOFTHSM2_CONF="$SOFTHSM_DIR/softhsm2.conf"

cd "$SOFTHSM_DIR"

cat >ca.tmpl <<'EOF'
cn = "Portal WebAuth Test CA"
organization = "Example Org"
serial = 1
expiration_days = 3650
ca
cert_signing_key
crl_signing_key
EOF

cat >client.tmpl <<'EOF'
cn = "Portal Test User"
organization = "Example Org"
serial = 2
expiration_days = 3650
signing_key
encryption_key
tls_www_client
EOF

cat >server.tmpl <<'EOF'
cn = "localhost"
organization = "Example Org"
serial = 3
expiration_days = 3650
dns_name = "localhost"
ip_address = "127.0.0.1"
signing_key
encryption_key
tls_www_server
EOF

certtool --generate-privkey --key-type=rsa --bits=3072 --outfile ca.key 2>/dev/null
certtool --generate-self-signed --load-privkey ca.key --template ca.tmpl \
	--outfile ca.pem 2>/dev/null

certtool --generate-privkey --key-type=rsa --bits=2048 --outfile client.key 2>/dev/null
certtool --generate-certificate --load-privkey client.key --load-ca-privkey ca.key \
	--load-ca-certificate ca.pem --template client.tmpl --outfile client.pem 2>/dev/null

certtool --generate-privkey --key-type=rsa --bits=2048 --outfile server.key 2>/dev/null
certtool --generate-certificate --load-privkey server.key --load-ca-privkey ca.key \
	--load-ca-certificate ca.pem --template server.tmpl --outfile server.pem 2>/dev/null

openssl x509 -in client.pem -outform der -out client.der
# softhsm2-util imports PKCS#8 only, and certtool writes PKCS#1 for RSA.
openssl pkcs8 -topk8 -nocrypt -in client.key -out client.p8

"$UTIL" --module "$MODULE" --init-token --slot 0 --label "$LABEL" \
	--so-pin "$SO_PIN" --pin "$PIN" >/dev/null

# softhsm2-util and pkcs11-tool take the PIN on argv and offer no other way, so
# it is visible in ps for the length of these two calls. THAT IS ONLY ACCEPTABLE
# BECAUSE THIS IS A THROWAWAY FIXTURE PIN in a scratch directory. Nothing that
# touches a real card may copy this pattern.
"$UTIL" --module "$MODULE" --import client.p8 --token "$LABEL" \
	--label "$OBJECT" --id 01 --pin "$PIN" >/dev/null

pkcs11-tool --module "$MODULE" --token-label "$LABEL" --login --pin "$PIN" \
	--write-object client.der --type cert --id 01 --label "$OBJECT" >/dev/null

# The private key stays on the token: what the backend is given is a URI. The
# fixture's own client.key and client.p8 are removed so that a run cannot
# accidentally prove the pipeline with a file instead of the token.
rm -f client.key client.p8 client.der ca.key ca.tmpl client.tmpl server.tmpl

CERT_URI="pkcs11:token=$(printf '%s' "$LABEL" | sed 's/ /%20/g');object=$OBJECT;type=cert"
printf '%s' "$CERT_URI" >"$SOFTHSM_DIR/cert-uri"

cat <<EOF

SoftHSM fixture ready.

  directory     $SOFTHSM_DIR
  module        $MODULE
  token         $LABEL
  certificate   $CERT_URI
  PIN           in $SOFTHSM_DIR/pin (a test PIN in a scratch directory)

Run the whole stack against it:

  tools/ui-smoke.sh --mtls
EOF
