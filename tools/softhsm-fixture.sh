#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
#
# Adapted from the sibling backend xdg-desktop-portal-certificate's
# tools/softhsm-fixture.sh (same author, same licence).
#
# Usage: tools/softhsm-fixture.sh [--clean]
# Create a disposable SoftHSM client token, CA and TLS server certificate.
# Use --clean to remove the validated fixture directory.
# SOFTHSM_DIR selects the directory under TMPDIR (default: ${TMPDIR:-/tmp}/xdp-webauth-softhsm).
# SOFTHSM_MODULE and SOFTHSM_UTIL select the module and utility paths.
# PIN and SO_PIN are disposable fixture values; do not use real-token PINs.
# See docs/TESTING.md for hardware-token tests.

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

	die "libsofthsm2.so not found; install softhsm or set SOFTHSM_MODULE"
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

	# Unpacked RPMs place the utility beside the module's library directory.
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

# WebKit's network process resolves the token through the inherited p11-kit configuration.
cat >"$SOFTHSM_DIR/pkcs11/modules/webauth-softhsm.module" <<EOF
module: $MODULE
critical: no
EOF

# Keep the fixture PIN in a private file for the backend to read.
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
# softhsm2-util requires PKCS#8; certtool writes RSA keys as PKCS#1.
openssl pkcs8 -topk8 -nocrypt -in client.key -out client.p8

"$UTIL" --module "$MODULE" --init-token --slot 0 --label "$LABEL" \
	--so-pin "$SO_PIN" --pin "$PIN" >/dev/null

# These utilities expose the disposable fixture PIN in argv; real-token code must keep PINs out of argv.
"$UTIL" --module "$MODULE" --import client.p8 --token "$LABEL" \
	--label "$OBJECT" --id 01 --pin "$PIN" >/dev/null

pkcs11-tool --module "$MODULE" --token-label "$LABEL" --login --pin "$PIN" \
	--write-object client.der --type cert --id 01 --label "$OBJECT" >/dev/null

# Remove fixture key files so the handshake must use the token.
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
