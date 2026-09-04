/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * webauth-service - one interactive web authentication transaction, over D-Bus.
 *
 * Copyright (C) 2026 the webauth-service authors
 *
 * This would be the D-Bus activated per-user service that owns
 * io.github.sjtrotter.WebAuthentication1 and, inside the same process, the browser
 * session that shows the page. Version 0 is deliberately ONE service with an
 * in-process browser-session abstraction: there is no org.freedesktop.impl.portal.*
 * backend ABI, and the name is a project-controlled one rather than a freedesktop
 * portal name it has not been granted.
 *
 * See data/io.github.sjtrotter.WebAuthentication1.xml for the interface and
 * docs/SERVICE-INTERFACE.md for what it means.
 *
 * Nothing is implemented: this is a design sketch, so the service exits 70.
 */

#include <stdio.h>

#include <glib.h>

/* Process exit codes. These describe how the service itself started or stopped and
 * have nothing to do with the D-Bus response codes in Response(), which are 0, 1 and 2
 * per data/io.github.sjtrotter.WebAuthentication1.xml. The numbering deliberately
 * matches the sysexits convention so a supervisor sees one scheme. */
#define WEBAUTH_EXIT_SUCCESS 0
#define WEBAUTH_EXIT_UNAVAILABLE 40 /* no session bus, no display, no web engine */
#define WEBAUTH_EXIT_USAGE 64
#define WEBAUTH_EXIT_INTERNAL 70

#define WEBAUTH_VERSION "0.0.0"
#define WEBAUTH_BUS_NAME "io.github.sjtrotter.WebAuthentication1"
#define WEBAUTH_OBJECT_PATH "/io/github/sjtrotter/WebAuthentication1"

static void webauth_usage(FILE* out)
{
	fprintf(out,
	        "webauth-service - hosted web authentication transactions over D-Bus\n"
	        "\n"
	        "USAGE\n"
	        "  webauth-service [options]\n"
	        "\n"
	        "  Normally started by D-Bus activation, not from a shell. It owns\n"
	        "    " WEBAUTH_BUS_NAME "\n"
	        "  on the session bus, exporting\n"
	        "    " WEBAUTH_OBJECT_PATH "\n"
	        "\n"
	        "OPTIONS\n"
	        "  --replace              take the name from a running instance\n"
	        "  --session <impl>       browser session implementation (default: auto)\n"
	        "  --verbose              raise the log level on stderr\n"
	        "  --help, --version      print this, or the version, and exit\n"
	        "\n"
	        "EXIT CODES\n"
	        "   0 clean shutdown        40 unavailable (no bus, no display, no engine)\n"
	        "  64 usage                 70 internal\n"
	        "\n"
	        "STATUS\n"
	        "  Design sketch. Nothing is implemented: the service exits 70.\n"
	        "  The interface is INCUBATING under a project-controlled name; it is not\n"
	        "  an xdg-desktop-portal interface and has not been proposed as one yet.\n");
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
			printf("webauth-service " WEBAUTH_VERSION
			       " (design sketch, not implemented; interface version 1, incubating)\n");
			return WEBAUTH_EXIT_SUCCESS;
		}

		if (g_strcmp0(arg, "--replace") == 0 || g_strcmp0(arg, "--verbose") == 0)
			continue;

		if (g_strcmp0(arg, "--session") == 0)
		{
			if (i + 1 >= argc)
			{
				fprintf(stderr, "webauth-service: --session needs a value\n");
				return WEBAUTH_EXIT_USAGE;
			}
			i++;
			continue;
		}

		fprintf(stderr, "webauth-service: unknown option '%s'\n", arg);
		fprintf(stderr, "Try 'webauth-service --help'.\n");
		return WEBAUTH_EXIT_USAGE;
	}

	/* Everything past this point would connect to the session bus, export the
	 * transaction layer (src/service.h), select a browser session
	 * (src/browser_session.h) and run a main loop. None of that exists. */
	fprintf(stderr, "webauth-service: not implemented (design sketch)\n");
	return WEBAUTH_EXIT_INTERNAL;
}
