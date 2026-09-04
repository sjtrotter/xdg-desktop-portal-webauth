/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_GTK_TLS_CLIENT_CERT_H
#define WEBAUTH_GTK_TLS_CLIENT_CERT_H

#include <gio/gio.h>

#include "../transaction.h"

/** @file
 *  Answering a TLS client certificate challenge, behind an adapter.
 *
 *  This is BACKEND work and could never be anything else: the challenge arrives
 *  inside a TLS handshake made by this process's web engine, and the frontend
 *  has neither the handshake nor a window to put a dialog in.
 *
 *  There are two ways to satisfy a client certificate challenge and this backend
 *  supports both, chosen at run time. The interface between them is deliberately
 *  tiny: a challenge goes in, a GTlsCertificate comes out, and the rest of the
 *  web view does not know or care which implementation produced it.
 *
 *    portal  - ask the certificate portal (public interface
 *              io.github.sjtrotter.portal.Certificate1) to select a credential and
 *              either broker the signing operation or hand back a PKCS#11
 *              endpoint. PREFERRED when available, because the chooser and the
 *              PIN then belong to one trusted service shared by every
 *              application, and the PIN never enters this process.
 *    inproc  - the proven path: enumerate with p11-kit, show this backend's own
 *              chooser and PIN prompt, and build the certificate with
 *              g_tls_certificate_new_from_pkcs11_uris() against the system
 *              p11-kit configuration. RETAINED as the fallback.
 *
 *  NOTE WHAT THE PORTAL ADAPTER IS, IN PORTAL TERMS: this backend acts as an
 *  ORDINARY CLIENT of another portal. It is not a backend calling a backend, and
 *  it must not be: it calls the smart card portal's PUBLIC frontend interface,
 *  gets a Request handle back, and waits for a Response, exactly as any
 *  application would. Its own identity as seen by that portal is
 *  webauth-portal-gtk's, not the original application's, which is a real
 *  limitation and is discussed in client_cert_portal.h and docs/SECURITY.md.
 *
 *  Why the fallback is not temporary scaffolding: the portal path is unproven at
 *  the point where it matters most. A PKCS#11 URI cannot name a socket; GLib's
 *  g_tls_certificate_new_from_pkcs11_uris() has no module parameter; and
 *  WebKit's network process may not see a module registered after it started. So
 *  "call the other portal and build a GTlsCertificate from a returned URI" is an
 *  assumption, not a mechanism, until spike S2 has completed a real mutual-TLS
 *  handshake that way. Until then this backend must not hard-depend on it. See
 *  docs/SPIKES.md and docs/decisions/0007-certificate-adapter.md.
 *
 *  Sketch only; nothing here is implemented.
 */

typedef struct
{
	const char* origin;        /**< the verified origin that raised the challenge */
	const char* app_id;        /**< the app id the FRONTEND derived; "" if unidentified */
	const char* app_id_kind;   /**< "sandboxed", "cgroup" or "host" */
	GPtrArray* acceptable_cas; /**< distinguished names the server will accept, may be empty */
	gpointer parent;           /**< the transaction's window, to parent any dialog to */
} WebAuthCertChallenge;

typedef void (*WebAuthCertDone)(GTlsCertificate* certificate, gpointer user_data);

/** One way of satisfying a challenge. */
typedef struct
{
	const char* name; /**< "portal" or "inproc" */

	/** Whether this implementation can run right now, checked before a challenge
	 *  arrives so a transaction can fail early and clearly rather than mid-handshake. */
	gboolean (*available)(GError** error);

	/** Select a credential, present whatever UI that needs, and produce a certificate.
	 *  @done is called exactly once: with a certificate, or with NULL if the user
	 *  cancelled or no credential could be produced. */
	void (*select_and_present)(const WebAuthCertChallenge* challenge, GCancellable* cancellable,
	                           WebAuthCertDone done, gpointer user_data);

	/** Release anything held for the transaction: a grant, an endpoint, a PKCS#11
	 *  session. Called on EVERY exit path, including cancellation and timeout. */
	void (*release)(void);
} WebAuthCertAdapter;

/** Choose an adapter: the one named in configuration, else "portal" if the smart
 *  card portal answers, else "inproc". Returns NULL when neither can run, in
 *  which case the challenge is declined and the transaction ends with
 *  WEBAUTH_RESPONSE_OTHER and reason "no_certificate_adapter". */
const WebAuthCertAdapter* webauth_cert_adapter_select(WebAuthTransaction* transaction,
                                                      GError** error);

const WebAuthCertAdapter* webauth_cert_adapter_portal(void);
const WebAuthCertAdapter* webauth_cert_adapter_inproc(void);

#endif /* WEBAUTH_GTK_TLS_CLIENT_CERT_H */
