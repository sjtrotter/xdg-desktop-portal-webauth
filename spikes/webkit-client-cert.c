/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * Spike S2: can WebKitGTK 6.0 answer a TLS client certificate challenge with a
 * certificate whose private key is a PKCS#11 URI?
 *
 * The question is not whether GLib can build such a GTlsCertificate -- it can --
 * but whether the certificate survives the hop from the UI process, where the
 * "authenticate" signal is emitted, to the network process, which owns the
 * handshake. If WebKit serialises only the DER chain, the private key is lost
 * and the handshake fails; the whole certificate-adapter design in
 * docs/decisions/0007-certificate-adapter.md turns on that answer.
 *
 * Build:
 *   cc -o /tmp/webkit-client-cert spikes/webkit-client-cert.c \
 *      $(pkg-config --cflags --libs webkitgtk-6.0 gtk4 gio-2.0)
 *
 * WebKitNetworkSession has no GTlsInteraction setter, so the only two ways a PIN
 * can reach the token are a pin-value in the URI and
 * webkit_credential_new_for_certificate_pin(); both are tried here.
 *
 * Run (see docs/SPIKES.md for the fixture and the server):
 *   webkit-client-cert --url https://localhost:8443/start \
 *     --cert-uri 'pkcs11:token=...;object=...;type=cert' \
 *     --key-uri 'pkcs11:token=...;object=...;type=private' \
 *     [--pin 123456] [--server-cert server.pem] [--pem-cert c.pem --pem-key k.pem]
 *
 * Exit codes: 0 the page loaded, 1 the load failed, 2 usage or setup.
 */

#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <webkit/webkit.h>

static char* opt_url = NULL;
static char* opt_cert_uri = NULL;
static char* opt_key_uri = NULL;
static char* opt_pem_cert = NULL;
static char* opt_pem_key = NULL;
static char* opt_pin = NULL;
static char* opt_server_cert = NULL;
static int opt_timeout = 30;

static int status = 2;
static GMainLoop* loop = NULL;

static const GOptionEntry entries[] = {
	{ "url", 0, 0, G_OPTION_ARG_STRING, &opt_url, "The https URL to load", "URL" },
	{ "cert-uri", 0, 0, G_OPTION_ARG_STRING, &opt_cert_uri, "PKCS#11 URI of the certificate",
	  "URI" },
	{ "key-uri", 0, 0, G_OPTION_ARG_STRING, &opt_key_uri, "PKCS#11 URI of the private key",
	  "URI" },
	{ "pem-cert", 0, 0, G_OPTION_ARG_FILENAME, &opt_pem_cert, "PEM certificate, instead of a URI",
	  "FILE" },
	{ "pem-key", 0, 0, G_OPTION_ARG_FILENAME, &opt_pem_key, "PEM private key, instead of a URI",
	  "FILE" },
	{ "pin", 0, 0, G_OPTION_ARG_STRING, &opt_pin, "Token PIN, answered through GTlsInteraction",
	  "PIN" },
	{ "server-cert", 0, 0, G_OPTION_ARG_FILENAME, &opt_server_cert,
	  "Trust this server certificate for the URL's host", "FILE" },
	{ "timeout", 0, 0, G_OPTION_ARG_INT, &opt_timeout, "Give up after N seconds", "N" },
	{ NULL, 0, 0, 0, NULL, NULL, NULL },
};

static void finish(int code)
{
	if (status == 2)
		status = code;
	g_main_loop_quit(loop);
}

static gboolean on_authenticate(WebKitWebView* view, WebKitAuthenticationRequest* request,
                                gpointer user_data)
{
	WebKitAuthenticationScheme scheme = webkit_authentication_request_get_scheme(request);
	g_autoptr(GTlsCertificate) certificate = NULL;
	g_autoptr(GError) error = NULL;
	WebKitCredential* credential = NULL;

	g_print("authenticate scheme=%d host=%s\n", (int) scheme,
	        webkit_authentication_request_get_host(request));

	if (scheme == WEBKIT_AUTHENTICATION_SCHEME_CLIENT_CERTIFICATE_PIN_REQUESTED)
	{
		if (opt_pin == NULL)
		{
			g_print("authenticate outcome=no-pin\n");
			webkit_authentication_request_cancel(request);
			return TRUE;
		}

		credential = webkit_credential_new_for_certificate_pin(
		    opt_pin, WEBKIT_CREDENTIAL_PERSISTENCE_FOR_SESSION);
		webkit_authentication_request_authenticate(request, credential);
		webkit_credential_free(credential);
		return TRUE;
	}

	if (scheme != WEBKIT_AUTHENTICATION_SCHEME_CLIENT_CERTIFICATE_REQUESTED)
		return FALSE;

	if (opt_pem_cert != NULL)
		certificate = g_tls_certificate_new_from_files(opt_pem_cert, opt_pem_key, &error);
	else
		certificate = g_tls_certificate_new_from_pkcs11_uris(opt_cert_uri, opt_key_uri, &error);

	if (certificate == NULL)
	{
		g_print("certificate outcome=failed detail=%s\n", error->message);
		webkit_authentication_request_cancel(request);
		finish(1);
		return TRUE;
	}

	g_print("certificate outcome=built kind=%s\n", opt_pem_cert != NULL ? "pem" : "pkcs11");

	credential =
	    webkit_credential_new_for_certificate(certificate, WEBKIT_CREDENTIAL_PERSISTENCE_NONE);
	webkit_authentication_request_authenticate(request, credential);
	webkit_credential_free(credential);

	return TRUE;
}

static void on_load_changed(WebKitWebView* view, WebKitLoadEvent event, gpointer user_data)
{
	if (event != WEBKIT_LOAD_FINISHED)
		return;

	g_print("load outcome=finished uri=%s\n", webkit_web_view_get_uri(view));
	finish(0);
}

static gboolean on_load_failed(WebKitWebView* view, WebKitLoadEvent event, const char* uri,
                               GError* error, gpointer user_data)
{
	g_print("load outcome=failed detail=%s\n", error->message);
	finish(1);
	return TRUE;
}

static gboolean on_load_failed_tls(WebKitWebView* view, const char* uri, GTlsCertificate* cert,
                                   GTlsCertificateFlags errors, gpointer user_data)
{
	g_print("load outcome=tls-failed flags=%u\n", (unsigned) errors);
	finish(1);
	return TRUE;
}

static gboolean on_timeout(gpointer user_data)
{
	g_print("load outcome=timeout\n");
	finish(1);
	return G_SOURCE_REMOVE;
}

int main(int argc, char** argv)
{
	g_autoptr(GOptionContext) context = NULL;
	g_autoptr(GError) error = NULL;
	WebKitNetworkSession* session = NULL;
	WebKitWebView* view = NULL;
	GtkWidget* window = NULL;

	context = g_option_context_new("- WebKitGTK client certificate spike");
	g_option_context_add_main_entries(context, entries, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error))
	{
		g_printerr("webkit-client-cert: %s\n", error->message);
		return 2;
	}

	if (opt_url == NULL || (opt_cert_uri == NULL && opt_pem_cert == NULL))
	{
		g_printerr("webkit-client-cert: --url and either --cert-uri or --pem-cert are required\n");
		return 2;
	}

	if (!gtk_init_check())
	{
		g_printerr("webkit-client-cert: no display\n");
		return 2;
	}

	loop = g_main_loop_new(NULL, FALSE);

	session = webkit_network_session_new_ephemeral();

	if (opt_server_cert != NULL)
	{
		g_autoptr(GTlsCertificate) server = NULL;
		g_autoptr(GUri) uri = g_uri_parse(opt_url, G_URI_FLAGS_NONE, NULL);

		server = g_tls_certificate_new_from_file(opt_server_cert, &error);
		if (server == NULL)
		{
			g_printerr("webkit-client-cert: %s\n", error->message);
			return 2;
		}

		webkit_network_session_allow_tls_certificate_for_host(session, server,
		                                                      g_uri_get_host(uri));
	}

	view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW, "network-session", session, NULL));

	g_signal_connect(view, "authenticate", G_CALLBACK(on_authenticate), NULL);
	g_signal_connect(view, "load-changed", G_CALLBACK(on_load_changed), NULL);
	g_signal_connect(view, "load-failed", G_CALLBACK(on_load_failed), NULL);
	g_signal_connect(view, "load-failed-with-tls-errors", G_CALLBACK(on_load_failed_tls), NULL);

	window = gtk_window_new();
	gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
	gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(view));
	gtk_window_present(GTK_WINDOW(window));

	webkit_web_view_load_uri(view, opt_url);

	g_timeout_add_seconds((guint) opt_timeout, on_timeout, NULL);
	g_main_loop_run(loop);

	return status;
}
