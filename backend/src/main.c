/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * xdg-desktop-portal-webauth - an out-of-tree xdg-desktop-portal BACKEND:
 * a window, a web engine, a card.
 *
 * Copyright (C) 2026 the xdg-desktop-portal-webauth authors
 *
 * This would be the D-Bus activated per-user process that owns
 * org.freedesktop.impl.portal.desktop.webauth and implements
 * org.freedesktop.impl.portal.experimental.WebAuthentication on
 * /org/freedesktop/portal/desktop -- the object path every portal backend
 * exports on.
 *
 * It is the shape of every out-of-tree backend: one file per portal in src/,
 * one .portal file in data/ declaring which impl interfaces this backend
 * implements, a D-Bus service file for activation, and a GTK main loop.
 * https://flatpak.github.io/xdg-desktop-portal/docs/writing-a-new-backend.html
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
 * Nothing is implemented: this is a design sketch, so the backend exits 70.
 */

#include <stdio.h>

#include <glib.h>

/* Process exit codes, matching the Entra client's and the certificate portal
 * backend's so a supervisor sees one scheme. They have nothing to do with the
 * D-Bus response codes 0, 1 and 2. */
#define WEBAUTH_EXIT_SUCCESS 0
#define WEBAUTH_EXIT_UNAVAILABLE 40 /* no session bus, no display, no web engine */
#define WEBAUTH_EXIT_USAGE 64
#define WEBAUTH_EXIT_INTERNAL 70

#define WEBAUTH_VERSION "0.0.0"
#define WEBAUTH_BACKEND_BUS_NAME "org.freedesktop.impl.portal.desktop.webauth"
#define WEBAUTH_BACKEND_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define WEBAUTH_IMPL_INTERFACE "org.freedesktop.impl.portal.experimental.WebAuthentication"
#define WEBAUTH_PUBLIC_INTERFACE "org.freedesktop.portal.experimental.WebAuthentication"

static void webauth_usage(FILE* out)
{
	fprintf(out,
	        "xdg-desktop-portal-webauth - a backend for the experimental WebAuthentication portal\n"
	        "\n"
	        "WHAT THIS IS\n"
	        "  An out-of-tree BACKEND of xdg-desktop-portal. It hosts the web view a\n"
	        "  sign-in flow runs in, shows the security chrome, intercepts the completion\n"
	        "  navigation before it loads, and answers TLS client certificate challenges.\n"
	        "  It is not a portal frontend, it owns no public interface, and no\n"
	        "  application talks to it.\n"
	        "\n"
	        "USAGE\n"
	        "  xdg-desktop-portal-webauth [options]\n"
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
	        "  itself. Calls from anything but the portal are refused.\n"
	        "\n"
	        "ENABLING THE FRONTEND\n"
	        "  The public interface\n"
	        "    " WEBAUTH_PUBLIC_INTERFACE "\n"
	        "  lives in xdg-desktop-portal itself, on the branch\n"
	        "    experimental/certificate-webauthentication\n"
	        "  and is EXPERIMENTAL: it is not exported unless the portal is started with\n"
	        "    XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication\n"
	        "  (\"all\" and a comma separated list also work). With the gate off, the\n"
	        "  interface is absent from introspection and this backend is never called;\n"
	        "  entra-token-helper reports exit 40 and says so.\n"
	        "  tools/dev-stack.sh wires a development frontend, this backend and a test\n"
	        "  call together on a private bus.\n"
	        "\n"
	        "OPTIONS\n"
	        "  --replace              take the name from a running instance\n"
	        "  --cert-adapter <impl>  portal | inproc | auto (default: auto)\n"
	        "  --verbose              raise the log level on stderr\n"
	        "  --help, --version      print this, or the version, and exit\n"
	        "\n"
	        "INSTALLED FILES\n"
	        "  $datadir/xdg-desktop-portal/portals/webauth.portal\n"
	        "      declares DBusName and Interfaces so the portal can find it\n"
	        "  $datadir/dbus-1/services/…impl.portal.desktop.webauth.service\n"
	        "      D-Bus activation\n"
	        "\n"
	        "EXIT CODES\n"
	        "   0 clean shutdown        40 unavailable (no bus, no display, no engine)\n"
	        "  64 usage                 70 internal\n"
	        "\n"
	        "STATUS\n"
	        "  Design sketch. Nothing is implemented: the backend exits 70.\n"
	        "  The impl interface is EXPERIMENTAL. It is defined by an xdg-desktop-portal\n"
	        "  branch that has not been proposed to anyone, and it can change or be\n"
	        "  removed without a version bump.\n");
}

int main(int argc, char** argv)
{
	for (int i = 1; i < argc; i++)
	{
		const char* arg = argv[i];

		if (g_strcmp0(arg, "--help") == 0 || g_strcmp0(arg, "-h") == 0)
		{
			webauth_usage(stdout);
			return WEBAUTH_EXIT_SUCCESS;
		}

		if (g_strcmp0(arg, "--version") == 0 || g_strcmp0(arg, "-V") == 0)
		{
			printf("xdg-desktop-portal-webauth " WEBAUTH_VERSION
			       " (design sketch, not implemented; impl interface version 1, experimental)\n");
			return WEBAUTH_EXIT_SUCCESS;
		}

		if (g_strcmp0(arg, "--replace") == 0 || g_strcmp0(arg, "--verbose") == 0)
			continue;

		if (g_strcmp0(arg, "--cert-adapter") == 0)
		{
			if (i + 1 >= argc)
			{
				fprintf(stderr, "xdg-desktop-portal-webauth: --cert-adapter needs a value\n");
				return WEBAUTH_EXIT_USAGE;
			}
			i++;
			continue;
		}

		fprintf(stderr, "xdg-desktop-portal-webauth: unknown option '%s'\n", arg);
		fprintf(stderr, "Try 'xdg-desktop-portal-webauth --help'.\n");
		return WEBAUTH_EXIT_USAGE;
	}

	/* Everything past this point would check that a web engine and a display
	 * exist (src/webkit_session.h), export the impl interface
	 * (src/webauthentication-impl.h), acquire the backend bus name and run a GTK
	 * main loop. None of that exists. */
	fprintf(stderr, "xdg-desktop-portal-webauth: not implemented (design sketch)\n");
	return WEBAUTH_EXIT_INTERNAL;
}
