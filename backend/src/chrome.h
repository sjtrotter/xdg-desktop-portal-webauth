/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_CHROME_H
#define WEBAUTH_CHROME_H

#include <glib.h>

/** @file
 *  The security chrome: the part of the window nobody outside this process can
 *  influence.
 *
 *  Any application can ask the portal to show a convincing Microsoft, Google or
 *  corporate sign-in page. The only thing standing between that and a phishing
 *  launcher is chrome that states, independently of anything the application
 *  supplied, which application asked and which origin is being shown. So the
 *  chrome renders the app id THE FRONTEND ESTABLISHED - and says plainly when
 *  the frontend could not verify it - alongside the engine's own idea of the
 *  current origin, and it renders the application's "title" hint, if any,
 *  visibly beneath and marked as coming from the application.
 *
 *  WHAT THE SPLIT CHANGED HERE, and it is the strongest single argument for it:
 *  the name in this window is no longer derived by the same process that draws
 *  it. It arrives as @app_id and @app_id_kind from a process the application
 *  cannot talk to, cannot influence, and does not share a bus name with. In the
 *  single-process design the identity resolution and the phishing surface were
 *  in one address space; now the thing being protected and the thing
 *  establishing who is asking are separated by a process boundary that runs the
 *  other way from the attacker.
 *
 *  What did NOT change: an empty @app_id means unidentified, and unidentified
 *  must be displayed as "an unidentified application" with the strongest warning
 *  this design has. Renaming the derivation did not make host callers
 *  identifiable.
 *
 *  Whether the certificate chooser and PIN prompt are part of this chrome
 *  depends on which adapter is in use (tls/client_cert.h). With the portal
 *  adapter they belong to the smart card portal and this backend's job is to
 *  have already established, and to still be displaying, the caller and origin
 *  that the other window will restate. With the in-process adapter they are this
 *  backend's own windows and carry the full obligation directly. Either way they
 *  must state the same four things - requesting application, target origin,
 *  certificate identity, purpose - so that what a user learns from one transfers
 *  to the other.
 *
 *  Accessibility is an acceptance criterion here, not a refinement: every
 *  control is exposed over AT-SPI, the caller and origin are announced to a
 *  screen reader, nothing is conveyed by colour alone, and focus returns to the
 *  calling application when the window closes (externalwindow.h).
 *
 *  Sketch only; nothing here is implemented.
 */

/** Build the trusted chrome for a transaction. @app_id and @app_id_kind come
 *  from the frontend; @title_hint is application text and is rendered as such,
 *  never as the window's identity. */
gpointer webauth_chrome_new(const char* app_id, const char* app_id_kind, const char* title_hint);

/** Update the displayed origin as the engine navigates. Takes the engine's
 *  verified origin, never a URI from anywhere else. */
void webauth_chrome_set_origin(gpointer chrome, const char* verified_origin);

/** Announce the requesting application and the current origin to assistive
 *  technology. */
void webauth_chrome_announce(gpointer chrome);

#endif /* WEBAUTH_CHROME_H */
