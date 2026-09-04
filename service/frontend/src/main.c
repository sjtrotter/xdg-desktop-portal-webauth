/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * webauth-portal-frontend - the portal frontend: routing and policy, no windows.
 *
 * Copyright (C) 2026 the webauth-portal authors
 *
 * This would be the D-Bus activated per-user process that owns
 * io.github.sjtrotter.portal.Desktop and exports
 * io.github.sjtrotter.portal.WebAuthentication1 on
 * /io/github/sjtrotter/portal/desktop -- the incubating stand-ins for
 * org.freedesktop.portal.Desktop, org.freedesktop.portal.<Name> and
 * /org/freedesktop/portal/desktop. It draws nothing: it derives the caller's app
 * id, validates the arguments, finds a backend through a .portal file, forwards
 * the call over io.github.sjtrotter.impl.portal.WebAuthentication1, and turns the
 * backend's reply into exactly one Response.
 *
 * This is xdg-desktop-portal's own shape, deliberately: see
 * https://flatpak.github.io/xdg-desktop-portal/docs/writing-a-new-backend.html
 * and docs/decisions/0008-build-to-the-upstream-shape.md for why a sketch is
 * built this way before anyone has accepted anything.
 *
 * At acceptance this whole directory is deleted and its contents become
 * xdg-desktop-portal/desktop-portal/webauthentication.c; see docs/UPSTREAMING.md.
 *
 * Nothing is implemented: this is a design sketch, so the frontend exits 70.
 */

#include <stdio.h>

#include <glib.h>

/* Process exit codes. These describe how the frontend itself started or stopped
 * and have nothing to do with the D-Bus response codes in Response(), which are
 * 0, 1 and 2 per data/io.github.sjtrotter.portal.WebAuthentication1.xml. The
 * numbering deliberately matches the sysexits convention, and matches the
 * backend's and the smart card portal's, so a supervisor sees one scheme. */
#define WEBAUTH_EXIT_SUCCESS 0
#define WEBAUTH_EXIT_UNAVAILABLE 40 /* no session bus, or the bus name is taken */
#define WEBAUTH_EXIT_USAGE 64
#define WEBAUTH_EXIT_INTERNAL 70

#define WEBAUTH_VERSION "0.0.0"
#define WEBAUTH_PORTAL_BUS_NAME "io.github.sjtrotter.portal.Desktop"
#define WEBAUTH_PORTAL_OBJECT_PATH "/io/github/sjtrotter/portal/desktop"
#define WEBAUTH_PORTAL_INTERFACE "io.github.sjtrotter.portal.WebAuthentication1"
#define WEBAUTH_IMPL_INTERFACE "io.github.sjtrotter.impl.portal.WebAuthentication1"

static void webauth_usage(FILE* out)
{
	fprintf(out,
	        "webauth-portal-frontend - the web authentication portal frontend\n"
	        "\n"
	        "USAGE\n"
	        "  webauth-portal-frontend [options]\n"
	        "\n"
	        "  Normally started by D-Bus activation, not from a shell. It owns\n"
	        "    " WEBAUTH_PORTAL_BUS_NAME "\n"
	        "  on the session bus, exporting\n"
	        "    " WEBAUTH_PORTAL_INTERFACE "\n"
	        "  on\n"
	        "    " WEBAUTH_PORTAL_OBJECT_PATH "\n"
	        "  and forwards each call to a backend implementing\n"
	        "    " WEBAUTH_IMPL_INTERFACE "\n"
	        "  found through a .portal file, as xdg-desktop-portal does.\n"
	        "\n"
	        "  It opens no window and hosts no web engine. That is the backend's\n"
	        "  job: webauth-portal-gtk, or another one named in portals.conf.\n"
	        "\n"
	        "OPTIONS\n"
	        "  --replace              take the name from a running instance\n"
	        "  --backend <name>       force a backend, ignoring portals.conf\n"
	        "  --verbose              raise the log level on stderr\n"
	        "  --help, --version      print this, or the version, and exit\n"
	        "\n"
	        "CONFIGURATION\n"
	        "  backends   $XDG_DATA_DIRS/webauth-portal/portals/*.portal\n"
	        "  preference $XDG_CONFIG_HOME/webauth-portal/portals.conf, then\n"
	        "             $XDG_CONFIG_DIRS, sysconfdir, $XDG_DATA_HOME, $XDG_DATA_DIRS\n"
	        "  Its own directories, NOT xdg-desktop-portal's: an unaccepted\n"
	        "  prototype must not read the real portal's configuration.\n"
	        "\n"
	        "EXIT CODES\n"
	        "   0 clean shutdown        40 unavailable (no bus, or name taken)\n"
	        "  64 usage                 70 internal\n"
	        "\n"
	        "STATUS\n"
	        "  Design sketch. Nothing is implemented: the frontend exits 70.\n"
	        "  Both the public and the impl interface are INCUBATING under\n"
	        "  project-controlled names. They are not xdg-desktop-portal\n"
	        "  interfaces and have not been proposed as any.\n"
	        "\n"
	        "  The bus name above is a singleton stand-in for\n"
	        "  org.freedesktop.portal.Desktop. Only one incubating frontend can\n"
	        "  hold it; see docs/decisions/0008-build-to-the-upstream-shape.md.\n");
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
			printf("webauth-portal-frontend " WEBAUTH_VERSION
			       " (design sketch, not implemented; interface version 1, incubating)\n");
			return WEBAUTH_EXIT_SUCCESS;
		}

		if (g_strcmp0(arg, "--replace") == 0 || g_strcmp0(arg, "--verbose") == 0)
			continue;

		if (g_strcmp0(arg, "--backend") == 0)
		{
			if (i + 1 >= argc)
			{
				fprintf(stderr, "webauth-portal-frontend: --backend needs a value\n");
				return WEBAUTH_EXIT_USAGE;
			}
			i++;
			continue;
		}

		fprintf(stderr, "webauth-portal-frontend: unknown option '%s'\n", arg);
		fprintf(stderr, "Try 'webauth-portal-frontend --help'.\n");
		return WEBAUTH_EXIT_USAGE;
	}

	/* Everything past this point would connect to the session bus, load the
	 * .portal files and portals.conf (src/portal-impl.h), export the portal if a
	 * backend implements it (src/webauthentication.h), acquire the bus name and
	 * run a main loop. None of that exists. */
	fprintf(stderr, "webauth-portal-frontend: not implemented (design sketch)\n");
	return WEBAUTH_EXIT_INTERNAL;
}
