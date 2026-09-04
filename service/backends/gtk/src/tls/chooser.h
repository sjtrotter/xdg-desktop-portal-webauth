/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_GTK_TLS_CHOOSER_H
#define WEBAUTH_GTK_TLS_CHOOSER_H

#include <glib.h>

#include "pkcs11.h"

/** @file
 *  The certificate chooser used by the in-process adapter.
 *
 *  When the portal adapter is in use this window never appears: the smart card portal
 *  shows its own, which is the point of preferring it. When the in-process adapter is in
 *  use this is the trusted dialog, and it carries the full obligation.
 *
 *  It names the requesting application, the target origin, the certificate identity and
 *  the purpose, because this is the moment the backend stops rendering a page and starts
 *  asking a hardware token to authenticate on someone's behalf. A chooser that does not
 *  say who wants the certificate is teaching the user to click through. Loading can take
 *  seconds on a slow reader, so it happens off the UI thread under a bounded progress
 *  dialog and can be cancelled. Cancelling at any point cancels the whole transaction;
 *  it never falls back to "no certificate". Keyboard-only operation is an acceptance
 *  criterion, not a refinement.
 *
 *  Sketch only; nothing here is implemented.
 */

typedef void (*WebAuthChooserDone)(const WebAuthCertificate* chosen, gpointer user_data);

/** Ask the user to pick one of @certificates. @done is called exactly once with the
 *  chosen certificate, or with NULL if the user cancelled. */
void webauth_chooser_present(gpointer parent, const char* caller_name, const char* origin,
                             GPtrArray* certificates, GCancellable* cancellable,
                             WebAuthChooserDone done, gpointer user_data);

#endif /* WEBAUTH_GTK_TLS_CHOOSER_H */
