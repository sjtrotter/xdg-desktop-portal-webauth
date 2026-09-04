/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * webauth-portal-gtk - the reference backend: a window, a web engine, a card.
 *
 * Copyright (C) 2026 the webauth-portal authors
 *
 * This would be the D-Bus activated per-user process that owns
 * io.github.sjtrotter.impl.portal.WebAuthentication.gtk and implements
 * io.github.sjtrotter.impl.portal.WebAuthentication1 on
 * /io/github/sjtrotter/portal/WebAuthentication -- this project's own
 * incubating stand-ins for org.freedesktop.impl.portal.desktop.<backend> and
 * org.freedesktop.impl.portal.<Name>.
 *
 * It is the shape of xdg-desktop-portal-gtk: one file per portal in src/, one
 * .portal file in data/ declaring which impl interfaces this backend implements,
 * a D-Bus service file for activation, and a GTK main loop.
 * https://flatpak.github.io/xdg-desktop-portal/docs/writing-a-new-backend.html
 *
 * APPLICATIONS MUST NOT CALL THIS PROCESS. It exists to serve one caller: the
 * portal frontend, which has already established who the application is, already
 * validated the arguments, and already applied the policy this backend obeys.
 * See docs/SECURITY.md.
 *
 * Nothing is implemented: this is a design sketch, so the backend exits 70.
 */

#include <stdio.h>

#include <glib.h>

/* Process exit codes, matching the frontend's and the smart card portal's so a
 * supervisor sees one scheme. They have nothing to do with the D-Bus response
 * codes 0, 1 and 2. */
#define WEBAUTH_EXIT_SUCCESS 0
#define WEBAUTH_EXIT_UNAVAILABLE 40 /* no session bus, no display, no web engine */
#define WEBAUTH_EXIT_USAGE 64
#define WEBAUTH_EXIT_INTERNAL 70

#define WEBAUTH_VERSION "0.0.0"
#define WEBAUTH_BACKEND_BUS_NAME "io.github.sjtrotter.impl.portal.WebAuthentication.gtk"
#define WEBAUTH_BACKEND_OBJECT_PATH "/io/github/sjtrotter/portal/WebAuthentication"
#define WEBAUTH_IMPL_INTERFACE "io.github.sjtrotter.impl.portal.WebAuthentication1"

static void webauth_usage(FILE* out)
{
	fprintf(out,
	        "webauth-portal-gtk - the reference web authentication portal backend\n"
	        "\n"
	        "USAGE\n"
	        "  webauth-portal-gtk [options]\n"
	        "\n"
	        "  Normally started by D-Bus activation, not from a shell, and called\n"
	        "  only by the portal frontend. It owns\n"
	        "    " WEBAUTH_BACKEND_BUS_NAME "\n"
	        "  on the session bus, implementing\n"
	        "    " WEBAUTH_IMPL_INTERFACE "\n"
	        "  on\n"
	        "    " WEBAUTH_BACKEND_OBJECT_PATH "\n"
	        "\n"
	        "  This is a BACKEND interface. Applications call the frontend\n"
	        "  (io.github.sjtrotter.portal.WebAuthentication1) and never this.\n"
	        "\n"
	        "  It hosts the GTK4/WebKitGTK 6.0 web view, applies the storage\n"
	        "  partition the frontend decided, shows the security chrome, and\n"
	        "  answers TLS client certificate challenges through an adapter:\n"
	        "  the smart card portal where it is available and proven, an\n"
	        "  in-process PKCS#11 chooser and PIN prompt otherwise.\n"
	        "\n"
	        "OPTIONS\n"
	        "  --replace              take the name from a running instance\n"
	        "  --cert-adapter <impl>  portal | inproc | auto (default: auto)\n"
	        "  --verbose              raise the log level on stderr\n"
	        "  --help, --version      print this, or the version, and exit\n"
	        "\n"
	        "INSTALLED FILES\n"
	        "  $datadir/webauth-portal/portals/webauth-gtk.portal\n"
	        "      declares DBusName and Interfaces so the frontend can find it\n"
	        "  $datadir/dbus-1/services/…impl.portal.WebAuthentication.gtk.service\n"
	        "      D-Bus activation\n"
	        "\n"
	        "EXIT CODES\n"
	        "   0 clean shutdown        40 unavailable (no bus, no display, no engine)\n"
	        "  64 usage                 70 internal\n"
	        "\n"
	        "STATUS\n"
	        "  Design sketch. Nothing is implemented: the backend exits 70.\n"
	        "  The impl interface is INCUBATING under a project-controlled name.\n"
	        "  It is not an xdg-desktop-portal backend interface and has not been\n"
	        "  proposed as one.\n");
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
			printf("webauth-portal-gtk " WEBAUTH_VERSION
			       " (design sketch, not implemented; impl interface version 1, incubating)\n");
			return WEBAUTH_EXIT_SUCCESS;
		}

		if (g_strcmp0(arg, "--replace") == 0 || g_strcmp0(arg, "--verbose") == 0)
			continue;

		if (g_strcmp0(arg, "--cert-adapter") == 0)
		{
			if (i + 1 >= argc)
			{
				fprintf(stderr, "webauth-portal-gtk: --cert-adapter needs a value\n");
				return WEBAUTH_EXIT_USAGE;
			}
			i++;
			continue;
		}

		fprintf(stderr, "webauth-portal-gtk: unknown option '%s'\n", arg);
		fprintf(stderr, "Try 'webauth-portal-gtk --help'.\n");
		return WEBAUTH_EXIT_USAGE;
	}

	/* Everything past this point would check that a web engine and a display
	 * exist (src/webkit_session.h), export the impl interface
	 * (src/webauthentication.h), acquire the backend bus name and run a GTK main
	 * loop. None of that exists. */
	fprintf(stderr, "webauth-portal-gtk: not implemented (design sketch)\n");
	return WEBAUTH_EXIT_INTERNAL;
}
