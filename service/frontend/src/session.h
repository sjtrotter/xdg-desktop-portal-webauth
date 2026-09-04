/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_PORTAL_SESSION_H
#define WEBAUTH_PORTAL_SESSION_H

#include <gio/gio.h>

/** @file
 *  The Session pattern, and why WebAuthentication1 version 1 does not use it.
 *
 *  xdg-desktop-portal has a second shared object beside Request: Session
 *  (desktop-portal/xdp-session.[ch], formerly src/session.c), used by every
 *  portal "that involves long lived sessions" - ScreenCast, RemoteDesktop,
 *  GlobalShortcuts, InputCapture. A session handle is
 *
 *    /org/freedesktop/portal/desktop/session/<SENDER>/<TOKEN>
 *
 *  minted from a session_handle_token option, it has Close() and a Closed
 *  signal, and "a client who started a session vanishing from the D-Bus is
 *  equivalent to closing all active sessions made by said client".
 *  https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Session.html
 *
 *  This header exists so the omission is a decision rather than an oversight.
 *
 *  WEBAUTHENTICATION1 VERSION 1 CREATES NO SESSION OBJECT. A web authentication
 *  transaction is a Request, exactly like Account.GetUserInformation or
 *  FileChooser.OpenFile: one interaction, one terminal Response, no state the
 *  caller holds afterwards. Adding a Session would give the caller a handle to
 *  something the caller must not own - the shared website data store, which is
 *  the backend's and is governed by the backend's policy, not by whoever asked
 *  for it first.
 *
 *  DO NOT CONFUSE THIS WITH session_mode. The "session_mode" option
 *  (shared|ephemeral) names a WEBSITE DATA STORE, not a portal Session. The
 *  collision of vocabulary is unfortunate and predates this restructuring; the
 *  interface XML says so explicitly in both places, and any renaming belongs to
 *  the upstream discussion rather than to a sketch.
 *
 *  WHAT WOULD NEED A SESSION, if it is ever built: a caller that wants several
 *  transactions to share one deliberately scoped store and to be able to
 *  destroy it on demand - the "app_persistent" mode that docs/ROADMAP.md defers
 *  until caller identity is credible enough to key one, and the "discard this
 *  account's web session" call that the Entra client's logout needs. Both are
 *  Session-shaped. Neither is version 1. When one arrives, this file becomes
 *  the frontend's Session object and it follows the upstream pattern without
 *  invention.
 *
 *  Sketch only; nothing here is implemented.
 */

#define WEBAUTH_PORTAL_SESSION_INTERFACE "io.github.sjtrotter.portal.Session"

/** The path a session handle WOULD take, given the caller's unique name and a
 *  session_handle_token: /io/github/sjtrotter/portal/WebAuthentication/session/<SENDER>/<TOKEN>.
 *  Present so that the convention is written down once, and so that the first
 *  interface here that needs a session cannot invent a different one. */
char* webauth_session_path_for(const char* sender, const char* session_handle_token);

#endif /* WEBAUTH_PORTAL_SESSION_H */
