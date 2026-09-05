/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * xdg-desktop-portal-webauth
 */

#include "webkit-session.h"

#include <adwaita.h>
#include <webkit/webkit.h>

#include "chrome.h"
#include "completion.h"
#include "external-window.h"
#include "redact.h"
#include "storage.h"
#include "tls/client_cert.h"

struct WebAuthWebkitSession
{
	WebAuthTransaction* transaction;
	WebAuthChrome* chrome;
	WebKitNetworkSession* network_session;
	WebKitWebView* view;

	const WebAuthCertAdapter* adapter;

	char* parent_window;
	char* activation_token;

	/* The host of the page a challenge may legitimately come from: the one being
	 * loaded, and the one already loaded. */
	char* pending_host;
	char* current_host;

	/* Why the load failed, when the reason is better than "load_failed": a
	 * declined certificate challenge is the usual case, and a server that
	 * required one then closes the connection. */
	const char* pending_reason;

	gboolean torn_down;
};

/* Development-only pinned server certificates: host -> GTlsCertificate. */
static GHashTable* debug_trust = NULL;

gboolean webauth_webkit_debug_trust_add(const char* spec, GError** error)
{
	g_auto(GStrv) parts = g_strsplit(spec, "=", 2);
	g_autoptr(GTlsCertificate) certificate = NULL;

	if (g_strv_length(parts) != 2 || *parts[0] == '\0' || *parts[1] == '\0')
	{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "--debug-trust-certificate takes host=/path/to/certificate.pem");
		return FALSE;
	}

	certificate = g_tls_certificate_new_from_file(parts[1], error);
	if (certificate == NULL)
		return FALSE;

	if (debug_trust == NULL)
		debug_trust = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);

	g_hash_table_insert(debug_trust, g_strdup(parts[0]), g_steal_pointer(&certificate));

	webauth_log_event(G_LOG_LEVEL_WARNING, WEBAUTH_EVENT_TLS_ERROR, "outcome",
	                  WEBAUTH_FIELD_OUTCOME, "debug-trust-pinned", "host", WEBAUTH_FIELD_HOST,
	                  parts[0], NULL);

	return TRUE;
}

static void apply_debug_trust(WebKitNetworkSession* session)
{
	GHashTableIter iter;
	gpointer host = NULL;
	gpointer certificate = NULL;

	if (debug_trust == NULL)
		return;

	g_hash_table_iter_init(&iter, debug_trust);
	while (g_hash_table_iter_next(&iter, &host, &certificate))
		webkit_network_session_allow_tls_certificate_for_host(session, certificate, host);
}

gboolean webauth_webkit_session_available(GError** error)
{
	if (gdk_display_get_default() == NULL)
	{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no display");
		return FALSE;
	}

	/* The engine is a link-time dependency, so its absence is a missing library
	 * rather than a run-time condition; what can still be missing at run time is
	 * a web process, and that is discovered when one is spawned. */
	return TRUE;
}

static char* host_of(const char* uri_string)
{
	g_autoptr(GUri) uri = NULL;

	if (uri_string == NULL)
		return NULL;

	uri = g_uri_parse(uri_string, G_URI_FLAGS_ENCODED, NULL);
	if (uri == NULL || g_uri_get_host(uri) == NULL)
		return NULL;

	return g_strdup(g_uri_get_host(uri));
}

static void teardown(gpointer user_data)
{
	WebAuthWebkitSession* self = user_data;

	if (self->torn_down)
		return;

	self->torn_down = TRUE;

	if (self->adapter != NULL)
		self->adapter->release();

	if (self->view != NULL)
	{
		g_signal_handlers_disconnect_by_data(self->view, self);
		webkit_web_view_stop_loading(self->view);
	}

	if (self->network_session != NULL)
		g_signal_handlers_disconnect_by_data(self->network_session, self);

	g_clear_pointer(&self->chrome, webauth_chrome_free);
	g_clear_object(&self->network_session);
	g_clear_pointer(&self->pending_host, g_free);
	g_clear_pointer(&self->current_host, g_free);
	g_clear_pointer(&self->parent_window, g_free);
	g_clear_pointer(&self->activation_token, g_free);

	g_clear_pointer(&self->transaction, webauth_transaction_unref);

	g_free(self);
}

static void on_cancel(gpointer user_data)
{
	WebAuthWebkitSession* self = user_data;

	webauth_transaction_finish(self->transaction, WEBAUTH_RESPONSE_CANCELLED, NULL,
	                           WEBAUTH_REASON_USER_CANCELLED);
}

/* THE MOMENT THE WHOLE DESIGN EXISTS FOR. The matched navigation is IGNORED, not
 * followed: the URI carries the authorization code, and letting the engine fetch
 * it would send the code to a page that has no part in the exchange. */
static gboolean complete_on(WebAuthWebkitSession* self, const char* uri)
{
	if (!webauth_completion_matches(uri, webauth_transaction_completion_uri(self->transaction)))
		return FALSE;

	webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_NAVIGATION, "outcome",
	                  WEBAUTH_FIELD_OUTCOME, "matched", "uri", WEBAUTH_FIELD_URI_SHAPE, uri, NULL);

	webauth_transaction_finish(self->transaction, WEBAUTH_RESPONSE_COMPLETED, uri, NULL);

	return TRUE;
}

static gboolean on_decide_policy(WebKitWebView* view, WebKitPolicyDecision* decision,
                                 WebKitPolicyDecisionType type, gpointer user_data)
{
	WebAuthWebkitSession* self = user_data;
	const char* uri = NULL;

	switch (type)
	{
		case WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION:
		{
			WebKitNavigationPolicyDecision* navigation =
			    WEBKIT_NAVIGATION_POLICY_DECISION(decision);
			WebKitNavigationAction* action =
			    webkit_navigation_policy_decision_get_navigation_action(navigation);

			uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(action));

			if (complete_on(self, uri))
			{
				webkit_policy_decision_ignore(decision);
				return TRUE;
			}

			g_clear_pointer(&self->pending_host, g_free);
			self->pending_host = host_of(uri);

			webauth_log_event(G_LOG_LEVEL_DEBUG, WEBAUTH_EVENT_NAVIGATION, "outcome",
			                  WEBAUTH_FIELD_OUTCOME, "unrelated", "uri", WEBAUTH_FIELD_URI_SHAPE,
			                  uri, NULL);
			return FALSE;
		}

		case WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION:
			/* There is one window and it belongs to the transaction. A flow that
			 * needs a popup is a flow this backend does not serve. */
			webkit_policy_decision_ignore(decision);
			return TRUE;

		case WEBKIT_POLICY_DECISION_TYPE_RESPONSE:
		{
			WebKitResponsePolicyDecision* response = WEBKIT_RESPONSE_POLICY_DECISION(decision);

			uri = webkit_uri_request_get_uri(webkit_response_policy_decision_get_request(response));

			/* The belt to the navigation braces: a response for the completion
			 * URI means the navigation rule missed it, and the body must still
			 * not be rendered. */
			if (complete_on(self, uri))
			{
				webkit_policy_decision_ignore(decision);
				return TRUE;
			}

			if (!webkit_response_policy_decision_is_mime_type_supported(response))
			{
				/* Anything the engine would hand to a download instead of
				 * rendering. Downloads are off; refusing the response is how
				 * that is enforced before one starts. */
				webkit_policy_decision_ignore(decision);
				return TRUE;
			}

			return FALSE;
		}

		default:
			return FALSE;
	}
}

static void update_origin(WebAuthWebkitSession* self)
{
	GTlsCertificate* certificate = NULL;
	GTlsCertificateFlags errors = 0;
	gboolean secure = webkit_web_view_get_tls_info(self->view, &certificate, &errors) && errors == 0;

	webauth_chrome_set_origin(self->chrome, self->current_host, secure);
}

static void on_load_changed(WebKitWebView* view, WebKitLoadEvent event, gpointer user_data)
{
	WebAuthWebkitSession* self = user_data;

	switch (event)
	{
		case WEBKIT_LOAD_STARTED:
		case WEBKIT_LOAD_REDIRECTED:
			webauth_chrome_set_busy(self->chrome, TRUE);
			g_clear_pointer(&self->current_host, g_free);
			self->current_host = host_of(webkit_web_view_get_uri(view));
			update_origin(self);
			break;

		case WEBKIT_LOAD_COMMITTED:
			g_clear_pointer(&self->current_host, g_free);
			self->current_host = host_of(webkit_web_view_get_uri(view));
			update_origin(self);
			break;

		case WEBKIT_LOAD_FINISHED:
		default:
			webauth_chrome_set_busy(self->chrome, FALSE);
			update_origin(self);
			break;
	}
}

static gboolean on_load_failed(WebKitWebView* view, WebKitLoadEvent event, const char* uri,
                               GError* error, gpointer user_data)
{
	WebAuthWebkitSession* self = user_data;
	g_autofree char* safe = webauth_redact_error_text(error->message);
	const char* reason = self->pending_reason != NULL ? self->pending_reason
	                                                  : WEBAUTH_REASON_LOAD_FAILED;

	/* A cancelled load is this backend's own doing -- an ignored policy decision
	 * on the completion URI -- and is not a failure. */
	if (g_error_matches(error, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED) ||
	    webauth_transaction_is_done(self->transaction))
		return TRUE;

	webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_LOAD_FAILED, "reason",
	                  WEBAUTH_FIELD_OUTCOME, reason, "detail", WEBAUTH_FIELD_OUTCOME, safe, NULL);

	webauth_transaction_finish(self->transaction, WEBAUTH_RESPONSE_OTHER, NULL, reason);

	return TRUE;
}

/* FAIL CLOSED, WITH NO WAY TO SAY OTHERWISE. There is no "continue anyway"
 * button and no option that would add one: a sign-in page whose certificate does
 * not verify is the case this portal exists to make safe. */
static gboolean on_load_failed_tls(WebKitWebView* view, const char* uri, GTlsCertificate* cert,
                                   GTlsCertificateFlags errors, gpointer user_data)
{
	WebAuthWebkitSession* self = user_data;
	g_autofree char* flags = g_strdup_printf("%u", (unsigned) errors);

	webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_TLS_ERROR, "flags", WEBAUTH_FIELD_COUNT,
	                  flags, "uri", WEBAUTH_FIELD_URI_SHAPE, uri, NULL);

	webauth_transaction_finish(self->transaction, WEBAUTH_RESPONSE_OTHER, NULL,
	                           WEBAUTH_REASON_TLS_ERROR);

	return TRUE;
}

static void on_web_process_terminated(WebKitWebView* view, WebKitWebProcessTerminationReason reason,
                                      gpointer user_data)
{
	WebAuthWebkitSession* self = user_data;

	webauth_log_event(G_LOG_LEVEL_WARNING, WEBAUTH_EVENT_WEB_PROCESS_GONE, "outcome",
	                  WEBAUTH_FIELD_OUTCOME, "terminated", NULL);

	webauth_transaction_finish(self->transaction, WEBAUTH_RESPONSE_OTHER, NULL,
	                           WEBAUTH_REASON_SESSION_TERMINATED);
}

/* The challenge must belong to the page the window is showing. A subresource on
 * a third host asking a hardware token to authenticate the user is not part of
 * the sign-in, and the user is not in a position to tell. */
static gboolean challenge_is_related(WebAuthWebkitSession* self, const char* host)
{
	if (host == NULL)
		return FALSE;

	if (self->pending_host != NULL && g_ascii_strcasecmp(host, self->pending_host) == 0)
		return TRUE;

	return self->current_host != NULL && g_ascii_strcasecmp(host, self->current_host) == 0;
}

static gboolean on_authenticate(WebKitWebView* view, WebKitAuthenticationRequest* request,
                                gpointer user_data)
{
	WebAuthWebkitSession* self = user_data;
	WebKitAuthenticationScheme scheme = webkit_authentication_request_get_scheme(request);
	const char* host = webkit_authentication_request_get_host(request);
	g_autoptr(GTlsCertificate) certificate = NULL;
	g_autoptr(GError) error = NULL;
	WebKitCredential* credential = NULL;

	if (scheme == WEBKIT_AUTHENTICATION_SCHEME_CLIENT_CERTIFICATE_PIN_REQUESTED)
	{
		const char* pin = self->adapter != NULL ? self->adapter->pin() : NULL;

		webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_PIN_REQUESTED, "provider",
		                  WEBAUTH_FIELD_OUTCOME,
		                  self->adapter != NULL ? self->adapter->name : "none", NULL);

		if (pin == NULL)
		{
			/* The portal provider's token prompts on its own protected path, so
			 * a PIN request reaching this process means the token is not the one
			 * that was expected. */
			self->pending_reason = WEBAUTH_REASON_NO_CERTIFICATE_ADAPTER;
			webkit_authentication_request_cancel(request);
			return TRUE;
		}

		credential =
		    webkit_credential_new_for_certificate_pin(pin, WEBKIT_CREDENTIAL_PERSISTENCE_NONE);
		webkit_authentication_request_authenticate(request, credential);
		webkit_credential_free(credential);

		return TRUE;
	}

	if (scheme != WEBKIT_AUTHENTICATION_SCHEME_CLIENT_CERTIFICATE_REQUESTED)
		return FALSE;

	webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_CERT_CHALLENGE, "host",
	                  WEBAUTH_FIELD_HOST, host, NULL);

	if (!challenge_is_related(self, host))
	{
		webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_CERT_DECLINED, "reason",
		                  WEBAUTH_FIELD_OUTCOME, WEBAUTH_REASON_UNRELATED_CERTIFICATE_CHALLENGE,
		                  "host", WEBAUTH_FIELD_HOST, host, NULL);
		self->pending_reason = WEBAUTH_REASON_UNRELATED_CERTIFICATE_CHALLENGE;
		webkit_authentication_request_cancel(request);
		return TRUE;
	}

	if (self->adapter == NULL)
	{
		webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_CERT_DECLINED, "reason",
		                  WEBAUTH_FIELD_OUTCOME, WEBAUTH_REASON_NO_CERTIFICATE_ADAPTER, NULL);
		self->pending_reason = WEBAUTH_REASON_NO_CERTIFICATE_ADAPTER;
		webkit_authentication_request_cancel(request);
		return TRUE;
	}

	{
		WebAuthCertChallenge challenge = {
			.origin = host,
			.app_id = webauth_transaction_app_id(self->transaction),
			.app_id_kind = webauth_transaction_app_id_kind(self->transaction),
			.parent = webauth_chrome_window(self->chrome),
		};

		certificate = self->adapter->acquire(&challenge, &error);
	}

	if (certificate == NULL)
	{
		webauth_log_event(G_LOG_LEVEL_WARNING, WEBAUTH_EVENT_CERT_DECLINED, "reason",
		                  WEBAUTH_FIELD_OUTCOME, WEBAUTH_REASON_NO_CERTIFICATE_ADAPTER, "detail",
		                  WEBAUTH_FIELD_OUTCOME, error->message, NULL);
		self->pending_reason = WEBAUTH_REASON_NO_CERTIFICATE_ADAPTER;
		webkit_authentication_request_cancel(request);
		return TRUE;
	}

	webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_CERT_ANSWERED, "provider",
	                  WEBAUTH_FIELD_OUTCOME, self->adapter->name, "host", WEBAUTH_FIELD_HOST, host,
	                  NULL);

	credential =
	    webkit_credential_new_for_certificate(certificate, WEBKIT_CREDENTIAL_PERSISTENCE_NONE);
	webkit_authentication_request_authenticate(request, credential);
	webkit_credential_free(credential);

	return TRUE;
}

static gboolean on_permission_request(WebKitWebView* view, WebKitPermissionRequest* request,
                                      gpointer user_data)
{
	/* Geolocation, notifications, the camera, the microphone, clipboard reads:
	 * a sign-in page needs none of them and this window grants none of them. */
	webkit_permission_request_deny(request);
	return TRUE;
}

static void on_download_started(WebKitNetworkSession* session, WebKitDownload* download,
                                gpointer user_data)
{
	webkit_download_cancel(download);
}

static WebKitNetworkSession* build_network_session(WebAuthTransaction* transaction, GError** error)
{
	WebKitNetworkSession* session = NULL;

	if (webauth_transaction_session_mode(transaction) == WEBAUTH_SESSION_EPHEMERAL)
	{
		session = webkit_network_session_new_ephemeral();

		if (session == NULL)
		{
			/* An ephemeral store that cannot be made is a transaction that
			 * fails. It is never quietly served from the shared one. */
			g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
			                    "could not create an ephemeral website data store");
			return NULL;
		}

		webauth_log_event(G_LOG_LEVEL_DEBUG, WEBAUTH_EVENT_STORAGE, "mode", WEBAUTH_FIELD_OUTCOME,
		                  "ephemeral", NULL);
	}
	else
	{
		g_autofree char* data = NULL;
		g_autofree char* cache = NULL;

		if (!webauth_storage_paths(webauth_transaction_app_id(transaction), &data, &cache, error))
			return NULL;

		session = webkit_network_session_new(data, cache);

		/* THE COOKIE JAR IS NOT PERSISTED BY THE DATA DIRECTORY ALONE. A
		 * WebKitNetworkSession with a data directory keeps caches, storage and
		 * HSTS state there, but cookies stay in memory until the cookie manager
		 * is given a file -- so a "shared" store without this line is an
		 * ephemeral one with a directory next to it. */
		{
			g_autofree char* jar = g_build_filename(data, "cookies.sqlite", NULL);

			webkit_cookie_manager_set_persistent_storage(
			    webkit_network_session_get_cookie_manager(session), jar,
			    WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
		}

		webauth_log_event(G_LOG_LEVEL_DEBUG, WEBAUTH_EVENT_STORAGE, "mode", WEBAUTH_FIELD_OUTCOME,
		                  "shared", "app_id", WEBAUTH_FIELD_APP_ID,
		                  webauth_transaction_app_id(transaction), NULL);
	}

	webkit_network_session_set_tls_errors_policy(session, WEBKIT_TLS_ERRORS_POLICY_FAIL);
	apply_debug_trust(session);
	webkit_network_session_set_itp_enabled(session, TRUE);

	return session;
}

static void harden_settings(WebKitSettings* settings)
{
	webkit_settings_set_enable_developer_extras(settings, FALSE);
	webkit_settings_set_enable_write_console_messages_to_stdout(settings, FALSE);
	webkit_settings_set_javascript_can_open_windows_automatically(settings, FALSE);
	webkit_settings_set_javascript_can_access_clipboard(settings, FALSE);
	webkit_settings_set_enable_html5_database(settings, FALSE);
	webkit_settings_set_enable_media_stream(settings, FALSE);
	webkit_settings_set_enable_webaudio(settings, FALSE);
	webkit_settings_set_enable_webgl(settings, FALSE);
	webkit_settings_set_enable_encrypted_media(settings, FALSE);
	webkit_settings_set_media_playback_requires_user_gesture(settings, TRUE);
}

WebAuthWebkitSession* webauth_webkit_session_new(WebAuthTransaction* transaction,
                                                 const char* parent_window,
                                                 const char* activation_token,
                                                 const char* title_hint, GError** error)
{
	WebAuthWebkitSession* self = g_new0(WebAuthWebkitSession, 1);
	g_autoptr(GError) adapter_error = NULL;

	self->transaction = webauth_transaction_ref(transaction);
	self->parent_window = g_strdup(parent_window);
	self->activation_token = g_strdup(activation_token);

	self->network_session = build_network_session(transaction, error);
	if (self->network_session == NULL)
	{
		g_clear_pointer(&self->transaction, webauth_transaction_unref);
		g_clear_pointer(&self->parent_window, g_free);
		g_clear_pointer(&self->activation_token, g_free);
		g_free(self);
		return NULL;
	}

	/* Chosen before the window opens rather than in the middle of a handshake,
	 * so that a machine with no provider says so once, in the log, instead of
	 * failing a TLS negotiation with nothing to point at. A NULL adapter is not
	 * an error: most sign-ins need no certificate at all. */
	self->adapter = webauth_cert_adapter_select(&adapter_error);
	if (self->adapter == NULL)
		webauth_log_event(G_LOG_LEVEL_DEBUG, WEBAUTH_EVENT_CERT_DECLINED, "provider",
		                  WEBAUTH_FIELD_OUTCOME, "none", "detail", WEBAUTH_FIELD_OUTCOME,
		                  adapter_error->message, NULL);

	self->chrome = webauth_chrome_new(webauth_transaction_app_id(transaction),
	                                  webauth_transaction_app_id_kind(transaction), title_hint,
	                                  on_cancel, self);

	self->view = WEBKIT_WEB_VIEW(
	    g_object_new(WEBKIT_TYPE_WEB_VIEW, "network-session", self->network_session, NULL));

	harden_settings(webkit_web_view_get_settings(self->view));

	gtk_widget_set_vexpand(GTK_WIDGET(self->view), TRUE);
	webauth_chrome_set_content(self->chrome, GTK_WIDGET(self->view));

	g_signal_connect(self->view, "decide-policy", G_CALLBACK(on_decide_policy), self);
	g_signal_connect(self->view, "load-changed", G_CALLBACK(on_load_changed), self);
	g_signal_connect(self->view, "load-failed", G_CALLBACK(on_load_failed), self);
	g_signal_connect(self->view, "load-failed-with-tls-errors", G_CALLBACK(on_load_failed_tls),
	                 self);
	g_signal_connect(self->view, "web-process-terminated", G_CALLBACK(on_web_process_terminated),
	                 self);
	g_signal_connect(self->view, "authenticate", G_CALLBACK(on_authenticate), self);
	g_signal_connect(self->view, "permission-request", G_CALLBACK(on_permission_request), self);
	g_signal_connect(self->network_session, "download-started", G_CALLBACK(on_download_started),
	                 self);

	webauth_transaction_set_teardown(transaction, teardown, self);

	return self;
}

void webauth_webkit_session_present(WebAuthWebkitSession* self)
{
	const char* start_uri = webauth_transaction_start_uri(self->transaction);

	g_clear_pointer(&self->pending_host, g_free);
	self->pending_host = host_of(start_uri);

	webauth_chrome_set_origin(self->chrome, self->pending_host, TRUE);

	webauth_external_window_present(webauth_chrome_window(self->chrome), self->parent_window,
	                                self->activation_token);

	webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_WINDOW_OPENED, "app_id",
	                  WEBAUTH_FIELD_APP_ID, webauth_transaction_app_id(self->transaction), "uri",
	                  WEBAUTH_FIELD_URI_SHAPE, start_uri, NULL);

	webkit_web_view_load_uri(self->view, start_uri);

	/* The clock starts when the window is up: a slow start-up must not eat the
	 * application's timeout. */
	webauth_transaction_start_deadline(self->transaction);
}
