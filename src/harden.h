/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_HARDEN_H
#define WEBAUTH_HARDEN_H

#include <glib.h>

/** @file
 *  TWO LINES THAT DECIDE WHERE A CREDENTIAL CAN END UP, run before anything
 *  else can crash. This process holds completion URIs carrying authorization
 *  codes, session cookies for an identity provider, and -- on the pkcs11
 *  provider -- a token PIN.
 *
 *  PR_SET_DUMPABLE(0) stops a core dump being written at all AND makes
 *  /proc/self/mem and the rest root-owned, which is what blocks a same-uid
 *  ptrace attach on a normal kernel. RLIMIT_CORE 0 is the belt to that braces.
 *
 *  IT DOES NOT REACH THE WEB ENGINE'S CHILDREN, and that is by design rather
 *  than an oversight: PR_SET_DUMPABLE is reset to 1 by execve (fs/exec.c,
 *  commit_creds), so WebKitNetworkProcess and WebKitWebProcess start dumpable
 *  however this process is set. They are separate processes with their own
 *  hardening story -- WebKit's own sandbox -- and the credential-bearing state
 *  this flag protects (the PIN buffer, the completion URI) lives here, in the
 *  UI process. docs/SECURITY.md says which half is covered.
 */

/** Apply it. @allow_core skips the whole thing, for a debugging build.
 *  A command-line flag rather than an environment variable on purpose: an
 *  installed service file's Exec line is fixed, so nothing that merely shares
 *  the session can turn the hardening off the way
 *  dbus-update-activation-environment could. */
void webauth_harden(gboolean allow_core);

/** BE IDENTIFIABLE TO xdg-desktop-portal FOR AS LONG AS THIS IS HELD, and not
 *  one moment longer.
 *
 *  THE PROBLEM, MEASURED. xdg-desktop-portal identifies a caller by reading
 *  /proc/<pid> -- xdp_app_info_from_pid() opens /proc/<pid>/root to decide
 *  whether the caller is in a sandbox. PR_SET_DUMPABLE(0) makes those entries
 *  root-owned, so the frontend cannot open them and answers every call from
 *  this process with
 *
 *      org.freedesktop.DBus.Error.AccessDenied: Portal operation not allowed:
 *      Unable to open /proc/<pid>/root
 *
 *  That is fine while this process only ANSWERS the portal. It stops being fine
 *  on the portal certificate provider, where the certificate portal's
 *  client-side PKCS#11 module runs INSIDE this process and has to CALL the
 *  portal -- CreateSession, AcquireCredential -- as an ordinary application.
 *  Hardened, those calls are refused before a chooser is ever drawn, and the
 *  only symptom a consumer sees is GnuTLS reporting that the object was not
 *  found.
 *
 *  WHY A WINDOW RATHER THAN GIVING THE FLAG UP. The window is opened when a TLS
 *  client-certificate challenge arrives and closed when the certificate has
 *  been built. In that interval this process holds no PIN -- the portal
 *  provider never has one, by construction -- and no authorization code, because
 *  the challenge is answered before the flow has redirected anywhere. What is
 *  exposed is what /proc exposes to the SAME USER for the length of a chooser:
 *  the maps, the fds and the environment. Giving the flag up for the life of
 *  the process would expose the same things while the completion URI is in
 *  memory, which is the one moment that matters.
 *
 *  Nested calls are counted, so a provider may hold it across several
 *  operations; it is closed for real when the last holder releases it.
 *
 *  ON THE pkcs11 PROVIDER NOTHING OPENS THIS WINDOW. That provider calls no
 *  portal, and the PIN it holds is exactly what the flag is there to protect. */
void webauth_harden_identifiable_begin(void);
void webauth_harden_identifiable_end(void);

/** Whether the window is open. For the tests and the start-up summary. */
gboolean webauth_harden_is_identifiable(void);

#endif /* WEBAUTH_HARDEN_H */
