/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_TLS_CLIENT_CERT_PORTAL_H
#define WEBAUTH_TLS_CLIENT_CERT_PORTAL_H

#include <gio/gio.h>

/** @file
 *  The portal provider: the certificate the Certificate portal granted, reached
 *  as a PKCS#11 token.
 *
 *  WHAT CHANGED, and it is the whole shape of this file. The original design had
 *  this backend call org.freedesktop.portal.Certificate.X1 as an
 *  ordinary client - AcquireCredential for a grant, then brokered Sign for each
 *  private key operation - and hand the signing to the TLS stack somehow. Spike
 *  S2 settled that "somehow": there is no external-signer seam. WebKitGTK's
 *  network process takes a GTlsCertificate carrying a PKCS#11 URI and resolves
 *  it itself; it takes no callback, no GTlsInteraction on the network session,
 *  and no signing function. A brokered Sign has nowhere to be plugged in.
 *
 *  So the facade moves: the certificate portal publishes a PKCS#11 MODULE, this
 *  backend names the token that module presents, and p11-kit is the seam. The
 *  Certificate portal still owns the card, the chooser, the consent and the PIN;
 *  what crosses into this process is a URI, and the private key operations
 *  happen inside the module, which calls the portal. tls/portal-token.h is the
 *  agreement, and it is the only thing the two repositories share.
 *
 *  WHAT THIS PROVIDER STILL DOES ITSELF: it checks that the Certificate portal
 *  interface is exported AND that the portal's module configuration is in one of
 *  p11-kit's module directories, before claiming to be available, so that a
 *  machine missing either half falls through to the pkcs11 provider instead of
 *  failing a handshake. It never asks for a PIN: the module's token declares
 *  CKF_PROTECTED_AUTHENTICATION_PATH and the portal prompts in its own window,
 *  which is the entire reason to prefer this provider.
 *
 *  IT IS USABLE. The module is xdg-desktop-portal-certificate's
 *  src/module/libpkcs11-portal-certificate.so, and the whole path -- WebKit
 *  authenticate, this provider, p11-kit, the module, CreateSession and
 *  AcquireCredential on the public interface, the certificate portal's chooser
 *  and PIN prompt, C_Sign, a completed mutual-TLS handshake -- has been run
 *  headless end to end by tools/portal-stack.sh. docs/TESTING.md tier 2 is the
 *  command; docs/decisions/0007-certificate-adapter.md records the decision and
 *  the evidence for it.
 */

/** Whether the Certificate portal interface is exported on
 *  org.freedesktop.portal.Desktop right now. Asked with NO_AUTO_START: a portal
 *  that is not running is a portal that cannot answer. */
gboolean webauth_cert_portal_interface_present(GError** error);

/** The portal's p11-kit module configuration file, searched for in the user
 *  directory, /etc/pkcs11/modules and p11-kit's own module directory, in that
 *  order; NULL when it is in none of them. Newly allocated. */
char* webauth_cert_portal_module_config_path(void);

#endif /* WEBAUTH_TLS_CLIENT_CERT_PORTAL_H */
