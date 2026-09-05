/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_TLS_CLIENT_CERT_H
#define WEBAUTH_TLS_CLIENT_CERT_H

#include <gio/gio.h>

/** @file
 *  Answering a TLS client certificate challenge, behind an adapter.
 *
 *  This is BACKEND work and could never be anything else: the challenge arrives
 *  inside a TLS handshake made by this process's web engine, and the frontend
 *  has neither the handshake nor a window to put a dialog in.
 *
 *  TWO PROVIDERS, ONE INTERFACE. A challenge goes in, a GTlsCertificate comes
 *  out, and the web view does not know which provider produced it.
 *
 *    portal   the certificate the user granted to this application, presented
 *             by the Certificate portal's client-side PKCS#11 module and named
 *             by the URI in portal-token.h. PREFERRED: the card, the chooser
 *             and the PIN all stay in that service, and this process never sees
 *             a PIN. The module does not exist yet, so this provider reports
 *             itself unavailable and selection falls through.
 *    pkcs11   any p11-kit token, named by --client-cert-uri. This is what the
 *             end-to-end tests use against SoftHSM, and what an operator with a
 *             card and no certificate portal uses today.
 *
 *  THERE IS NO "inproc" PROVIDER, and its absence is a decision rather than an
 *  omission: a chooser and a PIN prompt inside a web browser process is the
 *  design the certificate portal exists to replace, and building one here would
 *  make this backend a second place where card handling lives. This process
 *  never enumerates tokens, never draws a chooser, and never asks for a PIN it
 *  was not given on the command line by the person running it. See
 *  docs/decisions/0007-certificate-adapter.md.
 *
 *  BOTH PROVIDERS END IN THE SAME CALL: g_tls_certificate_new_from_pkcs11_uris()
 *  against the p11-kit configuration this process can see. Spike S2 established
 *  that WebKitGTK 2.52 carries such a certificate to its network process by URI
 *  and completes the handshake with the key still on the token; without that
 *  result neither provider would be possible. See docs/SPIKES.md.
 */

typedef struct
{
	const char* origin;      /**< the verified host that raised the challenge */
	const char* app_id;      /**< the app id xdg-desktop-portal derived; "" if unidentified */
	const char* app_id_kind; /**< "sandboxed", "cgroup" or "host" */
	gpointer parent;         /**< the transaction's window, to parent any dialog to */
} WebAuthCertChallenge;

/** One way of satisfying a challenge. */
typedef struct
{
	const char* name; /**< "portal" or "pkcs11" */

	/** Whether this provider can run right now, checked before a challenge
	 *  arrives so a transaction can fail early and clearly rather than
	 *  mid-handshake. */
	gboolean (*available)(GError** error);

	/** Build the certificate to present. Returns NULL and sets @error when no
	 *  credential can be produced; the challenge is then declined. */
	GTlsCertificate* (*acquire)(const WebAuthCertChallenge* challenge, GError** error);

	/** The PIN to answer a WEBKIT_AUTHENTICATION_SCHEME_CLIENT_CERTIFICATE_PIN_REQUESTED
	 *  with, or NULL when this provider has none - which is the portal
	 *  provider's permanent answer, because its token declares a protected
	 *  authentication path and prompts elsewhere. */
	const char* (*pin)(void);

	/** Release anything held for the transaction, including the PIN buffer.
	 *  Called on EVERY exit path, including cancellation and timeout. */
	void (*release)(void);
} WebAuthCertAdapter;

/** Configure the adapters from the command line. @name is "auto", "portal",
 *  "pkcs11" or "none". @cert_uri and @key_uri are the pkcs11 provider's token
 *  URIs; @key_uri may be NULL, in which case it is derived from @cert_uri.
 *  @pin_file is a file holding the token PIN and nothing else: a PIN on a
 *  command line is readable by every process on the machine. */
gboolean webauth_cert_adapter_configure(const char* name, const char* cert_uri,
                                        const char* key_uri, const char* pin_file,
                                        GError** error);

/** The provider a transaction will use, or NULL when none can run - in which
 *  case a challenge is declined and the transaction ends with
 *  WEBAUTH_RESPONSE_OTHER and reason "no_certificate_adapter". */
const WebAuthCertAdapter* webauth_cert_adapter_select(GError** error);

/** The configured name, for the start-up summary: "auto", "portal", "pkcs11" or
 *  "none". */
const char* webauth_cert_adapter_configured_name(void);

const WebAuthCertAdapter* webauth_cert_adapter_portal(void);
const WebAuthCertAdapter* webauth_cert_adapter_pkcs11(void);

/** Set by the pkcs11 provider's configuration; the portal provider ignores them. */
const char* webauth_cert_adapter_cert_uri(void);
const char* webauth_cert_adapter_key_uri(void);
const char* webauth_cert_adapter_pin_file(void);

#endif /* WEBAUTH_TLS_CLIENT_CERT_H */
