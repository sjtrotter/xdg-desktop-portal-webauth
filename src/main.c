/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * The main() shape -- gtk_init plus a plain GMainLoop rather than
 * GtkApplication, g_bus_own_name with REPLACE under --replace, and quitting on
 * name-lost -- is xdg-desktop-portal-gtk's, LGPL-2.1-or-later, Copyright (C)
 * 2016 Red Hat, Inc, by way of the sibling backend
 * xdg-desktop-portal-certificate.
 */

#include <locale.h>
#include <stdio.h>

#include <adwaita.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <gtk/gtk.h>

#include "config.h"

#include "harden.h"
#include "redact.h"
#include "tls/client_cert.h"
#include "webauthentication-impl.h"
#include "webkit-session.h"

/* Process exit codes follow the Entra client and certificate backend conventions. */
#define WEBAUTH_EXIT_SUCCESS 0
#define WEBAUTH_EXIT_UNAVAILABLE 40
#define WEBAUTH_EXIT_USAGE 64
#define WEBAUTH_EXIT_INTERNAL 70

static GMainLoop* loop = NULL;
static WebAuthBackend* backend = NULL;

static gboolean opt_replace = FALSE;
static gboolean opt_allow_replacement = FALSE;
static gboolean opt_verbose = FALSE;
static gboolean opt_version = FALSE;
static gboolean opt_no_activate = FALSE;
static gboolean opt_allow_core = FALSE;
static char* opt_cert_adapter = NULL;
static char* opt_client_cert_uri = NULL;
static char* opt_client_key_uri = NULL;
static char* opt_client_cert_pin_file = NULL;
static char** opt_debug_trust = NULL;

static const GOptionEntry entries[] = {
	{ "replace", 'r', 0, G_OPTION_ARG_NONE, &opt_replace,
	  "Take the bus name from a running instance that permitted it", NULL },
	{ "allow-replacement", 0, 0, G_OPTION_ARG_NONE, &opt_allow_replacement,
	  "Let a later instance take the bus name from this one", NULL },
	{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose, "Log backend events on stderr",
	  NULL },
	{ "cert-adapter", 0, 0, G_OPTION_ARG_STRING, &opt_cert_adapter,
	  "Client certificate provider: auto (default), portal, pkcs11 or none", "WHICH" },
	{ "client-cert-uri", 0, 0, G_OPTION_ARG_STRING, &opt_client_cert_uri,
	  "PKCS#11 URI of the client certificate, for --cert-adapter pkcs11", "URI" },
	{ "client-key-uri", 0, 0, G_OPTION_ARG_STRING, &opt_client_key_uri,
	  "PKCS#11 URI of its private key; derived from --client-cert-uri if absent", "URI" },
	{ "client-cert-pin-file", 0, 0, G_OPTION_ARG_FILENAME, &opt_client_cert_pin_file,
	  "File holding the token PIN, for a token that asks for one", "PATH" },
	{ "debug-trust-certificate", 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_debug_trust,
	  "Development: accept host=FILE as the server certificate; repeatable",
	  "HOST=FILE" },
	{ "no-activate", 0, 0, G_OPTION_ARG_NONE, &opt_no_activate,
	  "Do not request the bus name; start, check the engine, and exit", NULL },
	{ "debug-allow-core", 0, 0, G_OPTION_ARG_NONE, &opt_allow_core,
	  "Development: leave core dumps and ptrace attach enabled", NULL },
	{ "version", 0, 0, G_OPTION_ARG_NONE, &opt_version, "Print the version and exit", NULL },
	{ NULL, 0, 0, 0, NULL, NULL, NULL },
};

static const char* description =
    "WebKitGTK backend for the experimental WebAuthentication portal.\n"
    "D-Bus activates this service; only the owner of org.freedesktop.portal.Desktop may call it.\n"
    "Bus name: " WEBAUTH_BACKEND_BUS_NAME "\n"
    "Interface: " WEBAUTH_IMPL_INTERFACE "\n"
    "Object path: " WEBAUTH_BACKEND_OBJECT_PATH "\n"
    "\n"
    "The frontend requires xdg-desktop-portal branch experimental/integration, a configured\n"
    "backend, and XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=WebAuthentication1.\n"
    "Add Certificate1 to the same variable to enable the Certificate portal.\n"
    "\n"
    "The auto certificate provider tries the Certificate portal and its p11-kit module,\n"
    "then the token named by --client-cert-uri. PINs must come from --client-cert-pin-file;\n"
    "pin-value in the certificate URI is refused. This process has no chooser or PIN prompt.\n"
    "See docs/decisions/0007-certificate-adapter.md and docs/TESTING.md.\n"
    "\n"
    "Exit codes: 0 clean shutdown; 40 unavailable; 64 usage; 70 internal error.\n"
    "The interface is experimental; breaking changes increment its trailing number.";

static void apply_colour_scheme(GSettings* settings, const char* key, gpointer user_data)
{
	g_autofree char* scheme = g_settings_get_string(settings, "color-scheme");
	AdwColorScheme wanted = ADW_COLOR_SCHEME_DEFAULT;

	if (g_strcmp0(scheme, "prefer-dark") == 0)
		wanted = ADW_COLOR_SCHEME_PREFER_DARK;
	else if (g_strcmp0(scheme, "prefer-light") == 0)
		wanted = ADW_COLOR_SCHEME_PREFER_LIGHT;

	adw_style_manager_set_color_scheme(adw_style_manager_get_default(), wanted);
}

/* GSettings avoids the Settings portal's /proc identity check while this process is hardened. */
static void follow_colour_scheme(void)
{
	GSettingsSchemaSource* source = g_settings_schema_source_get_default();
	g_autoptr(GSettingsSchema) schema = NULL;
	const char* forced = g_getenv("ADW_DEBUG_COLOR_SCHEME");

	if (forced != NULL && *forced != '\0')
		return;

	if (source != NULL)
		schema = g_settings_schema_source_lookup(source, "org.gnome.desktop.interface", TRUE);

	if (schema == NULL || !g_settings_schema_has_key(schema, "color-scheme"))
		return;

	/* The settings object and its signal handler live for the process lifetime. */
	{
		GSettings* settings = g_settings_new("org.gnome.desktop.interface");

		g_signal_connect(settings, "changed::color-scheme", G_CALLBACK(apply_colour_scheme), NULL);
		apply_colour_scheme(settings, "color-scheme", NULL);
	}
}

static gboolean on_signal(gpointer user_data)
{
	g_main_loop_quit(loop);
	return G_SOURCE_REMOVE;
}

static void on_name_acquired(GDBusConnection* connection, const char* name, gpointer user_data)
{
	g_debug("Bus name acquired: %s", name);
}

static void on_name_lost(GDBusConnection* connection, const char* name, gpointer user_data)
{
	g_main_loop_quit(loop);
}

int main(int argc, char** argv)
{
	g_autoptr(GOptionContext) context = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GDBusConnection) connection = NULL;
	guint owner_id = 0;
	int status = WEBAUTH_EXIT_SUCCESS;

	setlocale(LC_ALL, "");

	/* Backend dialogs must not recurse through the portal. */
	g_unsetenv("GTK_USE_PORTAL");

	g_set_prgname("xdg-desktop-portal-webauth");

	context = g_option_context_new("- a backend for the experimental WebAuthentication portal");
	g_option_context_add_main_entries(context, entries, NULL);
	g_option_context_set_description(context, description);

	if (!g_option_context_parse(context, &argc, &argv, &error))
	{
		g_printerr("xdg-desktop-portal-webauth: %s\n", error->message);
		g_printerr("Try 'xdg-desktop-portal-webauth --help'.\n");
		return WEBAUTH_EXIT_USAGE;
	}

	/* Apply hardening before loading credentials or creating windows. */
	webauth_harden(opt_allow_core);

	if (opt_version)
	{
		g_print("xdg-desktop-portal-webauth " PACKAGE_VERSION
		        " (impl interface version %u, experimental)\n",
		        WEBAUTH_IMPL_INTERFACE_VERSION);
		return WEBAUTH_EXIT_SUCCESS;
	}

	webauth_log_set_verbose(opt_verbose);

	/* The debug-domain setter replaces G_MESSAGES_DEBUG; preserve an explicit operator selection. */
	if (opt_verbose && g_getenv("G_MESSAGES_DEBUG") == NULL)
	{
		const char* domains[] = { G_LOG_DOMAIN, NULL };

		g_log_writer_default_set_debug_domains(domains);
	}

	if (!webauth_cert_adapter_configure(opt_cert_adapter, opt_client_cert_uri, opt_client_key_uri,
	                                    opt_client_cert_pin_file, &error))
	{
		g_printerr("xdg-desktop-portal-webauth: %s\n", error->message);
		return WEBAUTH_EXIT_USAGE;
	}

	for (int i = 0; opt_debug_trust != NULL && opt_debug_trust[i] != NULL; i++)
	{
		if (!webauth_webkit_debug_trust_add(opt_debug_trust[i], &error))
		{
			g_printerr("xdg-desktop-portal-webauth: %s\n", error->message);
			return WEBAUTH_EXIT_USAGE;
		}
	}

	/* A backend must have a display before it claims the interface. */
	if (!gtk_init_check())
	{
		g_printerr("xdg-desktop-portal-webauth: display unavailable\n");
		return WEBAUTH_EXIT_UNAVAILABLE;
	}

	adw_init();
	follow_colour_scheme();

	if (!webauth_webkit_session_available(&error))
	{
		g_printerr("xdg-desktop-portal-webauth: %s\n", error->message);
		return WEBAUTH_EXIT_UNAVAILABLE;
	}

	loop = g_main_loop_new(NULL, FALSE);

	connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
	if (connection == NULL)
	{
		g_printerr("xdg-desktop-portal-webauth: session bus unavailable: %s\n", error->message);
		return WEBAUTH_EXIT_UNAVAILABLE;
	}

	if (opt_no_activate)
	{
		g_print("xdg-desktop-portal-webauth " PACKAGE_VERSION
		        ": startup checks passed; certificate provider %s; bus name unrequested\n",
		        webauth_cert_adapter_configured_name());
		return WEBAUTH_EXIT_SUCCESS;
	}

	g_unix_signal_add(SIGINT, on_signal, NULL);
	g_unix_signal_add(SIGTERM, on_signal, NULL);

	backend = webauth_backend_new(connection, &error);
	if (backend == NULL)
	{
		g_printerr("xdg-desktop-portal-webauth: backend interface export failed: %s\n",
		           error->message);
		return WEBAUTH_EXIT_INTERNAL;
	}

	/* Allowing replacement lets any session-bus peer take the name. */
	owner_id = g_bus_own_name_on_connection(
	    connection, WEBAUTH_BACKEND_BUS_NAME,
	    (opt_allow_replacement ? G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT
	                           : G_BUS_NAME_OWNER_FLAGS_NONE) |
	        (opt_replace ? G_BUS_NAME_OWNER_FLAGS_REPLACE : G_BUS_NAME_OWNER_FLAGS_NONE),
	    on_name_acquired, on_name_lost, NULL, NULL);

	g_main_loop_run(loop);

	webauth_backend_shutdown(backend);

	/* Flush pending replies before disconnecting from the bus. */
	g_dbus_connection_flush_sync(connection, NULL, NULL);

	webauth_backend_free(backend);
	g_bus_unown_name(owner_id);
	g_main_loop_unref(loop);
	g_free(opt_cert_adapter);
	g_free(opt_client_cert_uri);
	g_free(opt_client_key_uri);
	g_free(opt_client_cert_pin_file);
	g_strfreev(opt_debug_trust);

	return status;
}
