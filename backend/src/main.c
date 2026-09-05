/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * xdg-desktop-portal-webauth - an out-of-tree xdg-desktop-portal BACKEND:
 * a window, a web engine, a card.
 *
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * The D-Bus activated per-user process that owns
 * org.freedesktop.impl.portal.desktop.webauth and implements
 * org.freedesktop.impl.portal.experimental.WebAuthentication on
 * /org/freedesktop/portal/desktop -- the object path every portal backend
 * exports on.
 *
 * APPLICATIONS MUST NOT CALL THIS PROCESS. It exists to serve one caller:
 * xdg-desktop-portal, which has already established who the application is,
 * already validated the arguments, and already applied the policy this backend
 * obeys. See docs/SECURITY.md.
 *
 * THE FRONTEND IS NOT IN THIS REPOSITORY. It is a branch of xdg-desktop-portal,
 * experimental/certificate-webauthentication, which exports
 * org.freedesktop.portal.experimental.WebAuthentication only when
 * XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL contains "web-authentication". See
 * docs/decisions/0010-backend-only-frontend-lives-upstream.md.
 *
 * The main() shape -- gtk_init plus a plain GMainLoop rather than
 * GtkApplication, g_bus_own_name with REPLACE under --replace, and quitting on
 * name-lost -- is xdg-desktop-portal-gtk's, LGPL-2.1-or-later, Copyright (C)
 * 2016 Red Hat, Inc, by way of the sibling backend
 * xdg-desktop-portal-certificate.
 */

#include <locale.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/resource.h>

#include <adwaita.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <gtk/gtk.h>

#include "config.h"
#include "redact.h"
#include "tls/client_cert.h"
#include "webauthentication-impl.h"
#include "webkit-session.h"

/* Process exit codes, matching the Entra client's and the certificate portal
 * backend's so a supervisor sees one scheme. They have nothing to do with the
 * D-Bus response codes 0, 1 and 2. */
#define WEBAUTH_EXIT_SUCCESS 0
#define WEBAUTH_EXIT_UNAVAILABLE 40 /* no session bus, no display, no web engine */
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
	{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose, "Log decisions and breadcrumbs on stderr",
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
	  "DEVELOPMENT ONLY: accept host=FILE as that host's server certificate; repeatable",
	  "HOST=FILE" },
	{ "no-activate", 0, 0, G_OPTION_ARG_NONE, &opt_no_activate,
	  "Do not request the bus name; start, check the engine, and exit", NULL },
	{ "debug-allow-core", 0, 0, G_OPTION_ARG_NONE, &opt_allow_core,
	  "DEVELOPMENT ONLY: leave core dumps and ptrace attach enabled", NULL },
	{ "version", 0, 0, G_OPTION_ARG_NONE, &opt_version, "Print the version and exit", NULL },
	{ NULL, 0, 0, 0, NULL, NULL, NULL },
};

static const char* description =
    "WHAT THIS IS\n"
    "  An out-of-tree BACKEND of xdg-desktop-portal. It hosts the web view a\n"
    "  sign-in flow runs in, shows the security chrome, intercepts the completion\n"
    "  navigation before it loads, and answers TLS client certificate challenges.\n"
    "  It is not a portal frontend, it owns no public interface, and no\n"
    "  application talks to it.\n"
    "\n"
    "  Normally started by D-Bus activation when xdg-desktop-portal selects this\n"
    "  backend, not from a shell. It owns\n"
    "    " WEBAUTH_BACKEND_BUS_NAME "\n"
    "  on the session bus, implementing\n"
    "    " WEBAUTH_IMPL_INTERFACE "\n"
    "  on\n"
    "    " WEBAUTH_BACKEND_OBJECT_PATH "\n"
    "  as declared in data/webauth.portal.\n"
    "\n"
    "  ONLY xdg-desktop-portal CALLS THIS. The app id is an argument supplied by\n"
    "  the portal; a caller that reached this bus name directly would be naming\n"
    "  itself. Calls from any sender that does not own\n"
    "  org.freedesktop.portal.Desktop are refused with AccessDenied.\n"
    "\n"
    "ENABLING THE FRONTEND\n"
    "  The public interface\n"
    "    org.freedesktop.portal.experimental.WebAuthentication\n"
    "  lives in xdg-desktop-portal itself, on the branch\n"
    "    experimental/certificate-webauthentication\n"
    "  and is EXPERIMENTAL: it is not exported unless the portal is started with\n"
    "    XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication\n"
    "  (\"all\" and a comma separated list also work). With the gate off, the\n"
    "  interface is absent from introspection and this backend is never called.\n"
    "  tools/dev-stack.sh wires a development frontend, this backend and an\n"
    "  end-to-end client together on a private bus; docs/TESTING.md has the\n"
    "  commands, including a run against a real identity provider.\n"
    "\n"
    "CLIENT CERTIFICATES\n"
    "  --cert-adapter portal uses the certificate the Certificate portal granted,\n"
    "  presented by that portal's own PKCS#11 module: the card, the chooser and\n"
    "  the PIN all stay there. The module does not exist yet, so \"auto\" falls\n"
    "  through to pkcs11.\n"
    "  --cert-adapter pkcs11 uses the token named by --client-cert-uri. A PIN, if\n"
    "  the token wants one, is read from --client-cert-pin-file: a PIN on a\n"
    "  command line is readable by every process on the machine, and a pin-value\n"
    "  in the URI is refused for the same reason.\n"
    "  There is deliberately NO in-process chooser and no PIN prompt. See\n"
    "  docs/decisions/0007-certificate-adapter.md.\n"
    "\n"
    "EXIT CODES\n"
    "   0 clean shutdown        40 unavailable (no bus, no display, no engine)\n"
    "  64 usage                 70 internal\n"
    "\n"
    "STATUS\n"
    "  EXPERIMENTAL. The impl interface is defined by an xdg-desktop-portal\n"
    "  branch that has not been proposed to anyone, and it can change or be\n"
    "  removed without a version bump.";

/* TWO LINES THAT DECIDE WHERE A CREDENTIAL CAN END UP, run before anything else
 * can crash. This process holds completion URIs carrying authorization codes,
 * session cookies for an identity provider, and -- on the pkcs11 provider -- a
 * token PIN.
 *
 * PR_SET_DUMPABLE(0) stops a core dump being written at all AND makes
 * /proc/self/mem and the rest root-owned, which is what blocks a same-uid ptrace
 * attach on a normal kernel. RLIMIT_CORE 0 is the belt to that braces.
 *
 * IT DOES NOT REACH THE WEB ENGINE'S CHILDREN, and that is by design rather than
 * an oversight: PR_SET_DUMPABLE is reset to 1 by execve (fs/exec.c,
 * commit_creds), so WebKitNetworkProcess and WebKitWebProcess start dumpable
 * however this process is set. They are separate processes with their own
 * hardening story -- WebKit's own sandbox -- and the credential-bearing state
 * this flag protects (the PIN buffer, the completion URI) lives here, in the UI
 * process. docs/SECURITY.md says which half is covered. */
static void harden(void)
{
	struct rlimit no_core = { 0, 0 };

	/* A command-line flag rather than an environment variable on purpose: an
	 * installed service file's Exec line is fixed, so nothing that merely shares
	 * the session can turn the hardening off the way
	 * dbus-update-activation-environment could. */
	if (opt_allow_core)
	{
		webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_HARDENING, "outcome",
		                  WEBAUTH_FIELD_OUTCOME, "disabled-by-flag", NULL);
		return;
	}

	if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0)
		webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_HARDENING, "outcome",
		                  WEBAUTH_FIELD_OUTCOME, "prctl-dumpable-failed", NULL);

	if (setrlimit(RLIMIT_CORE, &no_core) != 0)
		webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_HARDENING, "outcome",
		                  WEBAUTH_FIELD_OUTCOME, "rlimit-core-failed", NULL);
}

/* THE WINDOW FOLLOWS THE SESSION'S LIGHT/DARK SETTING, AND HAS TO BE TOLD IT
 * DIRECTLY. libadwaita takes the colour scheme from the SETTINGS PORTAL, which
 * looks the caller up by /proc/<pid> -- and PR_SET_DUMPABLE(0) makes this
 * process's /proc entries root-owned on purpose, so the portal answers
 * AccessDenied. GSettings talks to dconf and looks nobody up by pid, so it works
 * in a process the portal cannot identify. The schema is looked up rather than
 * assumed: a missing schema aborts in g_settings_new(). */
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

	/* Leaked on purpose: it lives for the life of the process, and the "changed"
	 * handler is what keeps the window following the session. */
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
	g_debug("owning %s", name);
}

static void on_name_lost(GDBusConnection* connection, const char* name, gpointer user_data)
{
	/* Either another instance replaced this one, or the name could not be taken.
	 * Either way this process is done: a backend that stays running without the
	 * name is a backend holding a window nobody can answer for. */
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

	/* Avoid pointless and confusing recursion: this process must not route its
	 * own dialogs through the portal it is a backend of. */
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

	/* As early as the options allow: everything before this point is
	 * g_option_context_parse() on a fixed argv. */
	harden();

	if (opt_version)
	{
		g_print("xdg-desktop-portal-webauth " PACKAGE_VERSION
		        " (impl interface version %u, experimental)\n",
		        WEBAUTH_IMPL_INTERFACE_VERSION);
		return WEBAUTH_EXIT_SUCCESS;
	}

	webauth_log_set_verbose(opt_verbose);

	/* --verbose promises breadcrumbs, so it has to turn the debug level on as
	 * well: GLib's default writer drops g_debug() unless a domain is enabled.
	 * This backend's domain only -- turning on "all" buries the six lines that
	 * matter under GIO's, dconf's and GDK's. */
	if (opt_verbose)
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

	/* A DISPLAY IS REQUIRED TO START, unlike the certificate backend: every
	 * method this backend has opens a window. A backend that cannot show a page
	 * must not claim the interface, so that the frontend can fall through to
	 * another one. */
	if (!gtk_init_check())
	{
		g_printerr("xdg-desktop-portal-webauth: no display\n");
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
		g_printerr("xdg-desktop-portal-webauth: no session bus: %s\n", error->message);
		return WEBAUTH_EXIT_UNAVAILABLE;
	}

	if (opt_no_activate)
	{
		g_print("xdg-desktop-portal-webauth " PACKAGE_VERSION
		        ": engine present, display available, session bus reachable, certificate "
		        "provider %s. Not requesting the bus name.\n",
		        webauth_cert_adapter_configured_name());
		return WEBAUTH_EXIT_SUCCESS;
	}

	g_unix_signal_add(SIGINT, on_signal, NULL);
	g_unix_signal_add(SIGTERM, on_signal, NULL);

	backend = webauth_backend_new(connection, &error);
	if (backend == NULL)
	{
		g_printerr("xdg-desktop-portal-webauth: could not export the backend interface: %s\n",
		           error->message);
		return WEBAUTH_EXIT_INTERNAL;
	}

	/* REPLACEMENT IS NOT OFFERED TO ANYONE UNLESS IT IS ASKED FOR, AND THE TWO
	 * HALVES ARE SEPARATE FLAGS. ALLOW_REPLACEMENT is not "let the package
	 * manager replace me"; it is "let whoever asks next replace me", and D-Bus
	 * offers nothing finer -- no uid check, no peer identity. What would be taken
	 * over here is the window that shows a corporate sign-in page and the process
	 * that answers a certificate challenge, so the default is a name nobody can
	 * take. An upgrade is therefore a restart, and --replace exists for a
	 * development loop against an instance started with --allow-replacement. */
	owner_id = g_bus_own_name_on_connection(
	    connection, WEBAUTH_BACKEND_BUS_NAME,
	    (opt_allow_replacement ? G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT
	                           : G_BUS_NAME_OWNER_FLAGS_NONE) |
	        (opt_replace ? G_BUS_NAME_OWNER_FLAGS_REPLACE : G_BUS_NAME_OWNER_FLAGS_NONE),
	    on_name_acquired, on_name_lost, NULL, NULL);

	g_main_loop_run(loop);

	webauth_backend_shutdown(backend);

	/* The answers emitted above have to reach the bus before the process goes
	 * away, or the frontend learns about the loss by waiting for ever. */
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
