/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_CHROME_H
#define WEBAUTH_CHROME_H

#include <glib.h>

#include "identity.h"

/** @file
 *  The security chrome: the part of the window the caller cannot influence.
 *
 *  Any same-UID application can ask this service to show a convincing Microsoft,
 *  Google or corporate sign-in page. The only thing standing between that and a
 *  phishing launcher is chrome that states, independently of anything the caller
 *  supplied, which application asked and which origin is being shown. So the chrome
 *  renders the resolved caller identity — and says plainly when it could not be
 *  verified — alongside the engine's own idea of the current origin, and it renders
 *  the caller's "title" hint, if any, visibly beneath and marked as coming from the
 *  application.
 *
 *  Whether the certificate chooser and PIN prompt are part of this chrome depends on
 *  which adapter is in use (tls/client_cert.h). With the portal adapter they belong to
 *  the smart card service and this service's job is to have already established, and to
 *  still be displaying, the caller and origin that the other window will restate. With
 *  the in-process adapter they are this service's own windows and carry the full
 *  obligation directly. Either way they must state the same four things — requesting
 *  application, target origin, certificate identity, purpose — so that what a user
 *  learns from one transfers to the other.
 *
 *  Accessibility is an acceptance criterion here, not a refinement: every control is
 *  exposed over AT-SPI, the caller and origin are announced to a screen reader,
 *  nothing is conveyed by colour alone, and focus returns to the calling application
 *  when the window closes.
 *
 *  Sketch only; nothing here is implemented.
 */

/** Build the trusted chrome for a transaction. @title_hint is caller text and is
 *  rendered as such, never as the window's identity. */
gpointer webauth_chrome_new(const WebAuthIdentity* caller, const char* title_hint);

/** Update the displayed origin as the engine navigates. Takes the engine's verified
 *  origin, never a URI from anywhere else. */
void webauth_chrome_set_origin(gpointer chrome, const char* verified_origin);

/** Announce the caller and the current origin to assistive technology. */
void webauth_chrome_announce(gpointer chrome);

#endif /* WEBAUTH_CHROME_H */
