/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_PORTAL_WEBAUTHENTICATION_H
#define WEBAUTH_PORTAL_WEBAUTHENTICATION_H

#include <gio/gio.h>

#include "app-info.h"
#include "portal-impl.h"
#include "request.h"

/** @file
 *  The WebAuthentication1 portal, frontend half: policy, and nothing drawn.
 *
 *  This is upstream's desktop-portal/account.c under another name, and it does
 *  what that file does, in that order:
 *
 *    init_webauthentication(context)
 *      -> webauth_portal_config_find(config, WEBAUTH_IMPL_INTERFACE)
 *      -> if no backend, DO NOT EXPORT the interface at all
 *      -> create a proxy for the backend at WEBAUTH_PORTAL_OBJECT_PATH on the
 *         backend's bus name, with a default timeout of G_MAXINT because a
 *         human with a smart card is not a stalled call
 *      -> export the public skeleton, version 1
 *
 *    handle_start(invocation, parent_window, start_uri, completion_uri, options)
 *      -> app_id = webauth_app_info_get_id(request->app_info)   [DERIVED, never claimed]
 *      -> mint and EXPORT the Request before calling the backend
 *      -> filter and validate the options against the table below
 *      -> call the impl method with (handle, app_id, parent_window, start_uri,
 *         completion_uri, filtered options)
 *      -> return the request handle to the caller immediately
 *
 *    start_done(response, results)
 *      -> re-check what came back
 *      -> emit Response on the public Request and unexport it
 *
 *  WHAT THE FRONTEND ENFORCES, and why each thing is here rather than there:
 *
 *  - CALLER IDENTITY. Only the frontend can derive it (app-info.h). The backend
 *    is told, and must never ask: a backend that resolved its own peer would
 *    resolve the FRONTEND, which is not the application.
 *
 *  - SAME-UID. Checked before the request is parsed.
 *
 *  - ARGUMENT VALIDATION. Upstream's rule, from every portal in
 *    desktop-portal/: the frontend validates what it forwards. start_uri must be
 *    absolute https with a host and no userinfo; completion_uri must be absolute,
 *    https or an exactly named custom scheme, with no userinfo and no wildcard;
 *    both must be free of control characters, malformed percent escapes, %00,
 *    and backslashes in authority-sensitive positions. A malformed request is a
 *    D-Bus error return, not a Response with code 2: no window is opened and no
 *    backend is woken. This is the whole reason an invalid request costs
 *    nothing.
 *
 *  - OPTION FILTERING. An XdpOptionKey-style table with a validator per key,
 *    passed through the equivalent of xdp_filter_options(): unknown keys are
 *    DROPPED rather than forwarded, so a backend can never be steered by a key
 *    the frontend does not know about. handle_token must be a valid object path
 *    element; session_mode must be exactly "shared" or "ephemeral" (an unknown
 *    value is an error, never a fallback); timeout is clamped to the 900 second
 *    ceiling; title is length-limited exactly as upstream's Account portal
 *    length-limits "reason".
 *
 *  - POLICY THE CALLER MAY NOT INFLUENCE, forwarded as facts rather than
 *    requests: the app id and its honesty level, and a session_mode that policy
 *    may have forced to ephemeral.
 *
 *  - THE ANSWER. Exactly one Response per Request, bound to the initiating
 *    connection, and a defined answer when the backend dies mid-transaction.
 *
 *  WHAT THE BACKEND ENFORCES, because only it can: the interception itself.
 *  Only the backend has the web view, so only the backend can test a live
 *  navigation against the completion URI and stop it before it loads.
 *
 *  THE MATCHING RULE THEREFORE HAS TWO ENFORCEMENT POINTS AND MUST HAVE ONE
 *  DEFINITION. It is defined once in docs/PUBLIC-INTERFACE.md, implemented in
 *  the backend (backends/gtk/src/completion.h) where the navigation happens, and
 *  applied again here to what comes back:
 *  webauth_completion_is_the_requested_one() re-parses the backend's
 *  "completion_uri" result and compares it to the completion_uri the caller
 *  asked for, under the same rules. If they differ, the frontend answers 2 with
 *  reason "backend_completion_mismatch" and does not pass the URI on.
 *
 *  That second check is not paranoia about a hypothetical hostile backend. It
 *  is what makes the response the frontend emits a statement the FRONTEND can
 *  make: the application trusts the frontend's bus name, not whichever backend
 *  a distribution happened to install, and a mismatch is a bug that would
 *  otherwise deliver an attacker-chosen URI - carrying, in the AVD case, an
 *  authorization code - to an application that had asked for a different one.
 *  Upstream does the same kind of re-processing rather than trusting results
 *  verbatim: Account re-registers the returned image URI as a document, and
 *  FileChooser validates the URIs a backend returns before handing them over.
 *
 *  A third consequence, and the honest cost: the two implementations of one rule
 *  can drift. The shared fixture table in tests/README.md is the mitigation, and
 *  at upstream acceptance the matcher belongs in xdg-desktop-portal's shared/
 *  where both halves link the same code.
 *
 *  Sketch only; nothing here is implemented.
 */

#define WEBAUTH_PORTAL_INTERFACE "io.github.sjtrotter.portal.WebAuthentication1"
#define WEBAUTH_IMPL_INTERFACE "io.github.sjtrotter.impl.portal.WebAuthentication1"
#define WEBAUTH_INTERFACE_VERSION 1u

#define WEBAUTH_TIMEOUT_DEFAULT_SECONDS 300u
#define WEBAUTH_TIMEOUT_CEILING_SECONDS 900u
#define WEBAUTH_TITLE_MAX_CHARS 256u

/** Export the portal on @connection if a backend for WEBAUTH_IMPL_INTERFACE is
 *  configured, and do nothing at all if none is: an interface nothing can serve
 *  should be absent rather than failing every call. */
gboolean webauth_portal_init(GDBusConnection* connection, WebAuthPortalConfig* config,
                             GError** error);

/** Validate a start URI before anything is forwarded. */
gboolean webauth_portal_validate_start_uri(const char* uri, GError** error);

/** Validate a completion URI before anything is forwarded. */
gboolean webauth_portal_validate_completion_uri(const char* uri, GError** error);

/** Filter @options against the frontend's option table, dropping unknown keys and
 *  failing on an unknown value for a known key. */
GVariant* webauth_portal_filter_options(GVariant* options, GError** error);

/** Whether the "completion_uri" a backend returned is the one that was
 *  requested, under the matching rule in docs/PUBLIC-INTERFACE.md. A FALSE here
 *  turns a response 0 into a response 2 with reason
 *  "backend_completion_mismatch". */
gboolean webauth_completion_is_the_requested_one(const char* returned_uri,
                                                 const char* requested_completion_uri);

/** Rate limit @app_info. Repeated background requests from one connection are
 *  the cheapest way to turn this portal into a phishing launcher. */
gboolean webauth_portal_rate_limit_allows(const WebAuthAppInfo* app_info);

#endif /* WEBAUTH_PORTAL_WEBAUTHENTICATION_H */
