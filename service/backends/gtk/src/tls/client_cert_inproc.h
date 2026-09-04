/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_GTK_TLS_CLIENT_CERT_INPROC_H
#define WEBAUTH_GTK_TLS_CLIENT_CERT_INPROC_H

#include <gio/gio.h>

/** @file
 *  The in-process adapter: the path that is known to work.
 *
 *  Enumerate the certificates on the machine's PKCS#11 tokens (pkcs11.h), show this
 *  backend's own chooser (chooser.h) and PIN prompt (pin.h), and build the certificate
 *  with g_tls_certificate_new_from_pkcs11_uris() against the SYSTEM p11-kit
 *  configuration — no forwarded module, no dynamic registration, nothing unproven
 *  between the chooser and the handshake.
 *
 *  This is the fallback, and it is retained rather than deleted because the portal
 *  adapter's weakest link sits precisely where this one has no link at all. It is also
 *  the reason the backend has no hard dependency on the smart card portal in v0: a
 *  machine without one still signs in.
 *
 *  When this adapter is in use, the chooser and PIN prompt are THIS backend's windows
 *  and inherit every rule in chrome.h and docs/SECURITY.md: the chooser names the
 *  requesting application, the origin, the certificate and the purpose before any PIN;
 *  the PIN is never stored, never logged, never in the DOM, and its buffer is cleared on
 *  every exit path; a challenge is answered once plus at most one retry the TLS stack
 *  itself initiated; and cancelling anywhere cancels the transaction.
 *
 *  Sketch only; nothing here is implemented.
 */

/** Whether p11-kit and a usable UI are present. */
gboolean webauth_cert_inproc_available(GError** error);

/** Build a certificate from the chosen certificate and private key URIs against the
 *  system p11-kit configuration. */
GTlsCertificate* webauth_cert_inproc_load(const char* certificate_uri, const char* key_uri,
                                          GError** error);

/** End the PKCS#11 login where that is practical. No promise is made that a card,
 *  middleware daemon or token firmware can be made to forget authentication on demand —
 *  several cache it internally and this backend cannot override them. */
void webauth_cert_inproc_logout(void);

#endif /* WEBAUTH_GTK_TLS_CLIENT_CERT_INPROC_H */
