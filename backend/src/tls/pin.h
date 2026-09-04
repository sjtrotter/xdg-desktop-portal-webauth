/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_TLS_PIN_H
#define WEBAUTH_TLS_PIN_H

#include <glib.h>

/** @file
 *  The PIN prompt used by the in-process adapter.
 *
 *  Never reached when the portal adapter is in use: the PIN is then entered in the smart
 *  card portal's window and never enters this process at all. That difference is the
 *  strongest argument for the portal adapter, and it is why this file's rules matter
 *  only for as long as the fallback exists.
 *
 *  A PIN is never stored anywhere, not even for the length of a transaction: it is read,
 *  passed to the challenge, and the buffer is overwritten on every exit path —
 *  completion, failure, timeout and cancellation alike. It never enters the DOM, is
 *  never handed to the engine's credential storage, never crosses the bus, and is never
 *  logged, not even redacted: a redacted PIN in a log still says one was entered and how
 *  long it was.
 *
 *  A prompt is bound to the certificate transaction that raised it, and is answered once
 *  per challenge plus at most one retry the TLS stack itself initiated. Anything beyond
 *  that is refused: automatically re-answering is how a card gets locked, and burning a
 *  user's last PIN attempt is not a bug this backend is allowed to have. Retry
 *  exhaustion is reported in plain language, not as a generic failure.
 *
 *  Sketch only; nothing here is implemented.
 */

typedef void (*WebAuthPinDone)(const char* pin, gpointer user_data);

/** Ask for the PIN of @token_label, after the chooser has already named the caller, the
 *  origin and the certificate. @attempt is 0 for the first prompt and 1 for the one
 *  permitted retry. @done receives the PIN, or NULL if the user cancelled; the buffer it
 *  is given is scrubbed when @done returns. */
void webauth_pin_present(gpointer parent, const char* origin, const char* token_label,
                         guint attempt, GCancellable* cancellable, WebAuthPinDone done,
                         gpointer user_data);

/** Tell the user their card has no attempts left, and why the backend will not ask
 *  again. */
void webauth_pin_report_locked(gpointer parent, const char* token_label);

#endif /* WEBAUTH_TLS_PIN_H */
