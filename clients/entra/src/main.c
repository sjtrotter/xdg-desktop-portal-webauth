/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * entra-token-helper - Entra ID / AVD token client; a portal CONSUMER.
 *
 * This file is the command line front end described in docs/ENTRA-CLIENT-CLI.md. It
 * parses the four verbs and their options, validates them, and would hand a request
 * object (see ipc/request.h) to the OAuth components under clients/entra/, which do
 * the interactive part by calling the web authentication portal's PUBLIC interface
 * (webauth_client.h) rather than by owning a web view. It talks to
 * xdg-desktop-portal and to nothing else: it never sees, names or depends on
 * whichever backend the portal routes to. None of those components exist: this is
 * a design sketch, so every verb reports ETH_EXIT_INTERNAL with
 * "not implemented (design sketch)".
 */

#include <stdio.h>
#include <string.h>

#include <glib.h>

/* Exit codes. docs/ENTRA-CLIENT-CLI.md is normative; keep the two in step. */
typedef enum
{
	ETH_EXIT_SUCCESS = 0,              /* the request succeeded                        */
	ETH_EXIT_INTERACTION_REQUIRED = 10, /* a window was needed and --prompt never given */
	ETH_EXIT_CANCELLED = 20,           /* the user cancelled                           */
	ETH_EXIT_NO_ACCOUNT = 30,          /* no such account / not signed in              */
	ETH_EXIT_UNAVAILABLE = 40,         /* no display, no keyring, no WebKit            */
	ETH_EXIT_SERVER_ERROR = 50,        /* the authorization server refused             */
	ETH_EXIT_USAGE = 64,               /* EX_USAGE                                     */
	ETH_EXIT_INTERNAL = 70             /* EX_SOFTWARE                                  */
} EthExit;

typedef enum
{
	ETH_VERB_NONE,
	ETH_VERB_LOGIN,
	ETH_VERB_TOKEN,
	ETH_VERB_ACCOUNTS,
	ETH_VERB_LOGOUT
} EthVerb;

typedef enum
{
	ETH_PROMPT_AUTO,
	ETH_PROMPT_ALWAYS,
	ETH_PROMPT_NEVER
} EthPrompt;

/* The request docs/ENTRA-CLIENT-CLI.md describes, as far as argv can fill it in. Nothing
 * reads it yet; it is here so the option parser has somewhere honest to put things. */
typedef struct
{
	EthVerb verb;
	const char* authority;
	const char* tenant;
	const char* client_id;
	GPtrArray* scopes; /* of const char*, borrowed from argv */
	const char* req_cnf;
	const char* account;
	const char* config;
	EthPrompt prompt;
	gboolean json;
	gboolean verbose;
} EthRequest;

#define ETH_VERSION "0.0.0"

/* The one client id and the constants it is registered with. See README.md. */
#define ETH_AVD_CLIENT_ID "a85cf173-4192-42f8-81fa-777a763e6e2c"
#define ETH_REDIRECT_URI "https://login.microsoftonline.com/common/oauth2/nativeclient"

static void eth_usage(FILE* out)
{
	fprintf(out,
	        "entra-token-helper - Entra ID sign-in helper for Azure Virtual Desktop\n"
	        "\n"
	        "USAGE\n"
	        "  entra-token-helper <verb> [options]\n"
	        "\n"
	        "VERBS\n"
	        "  login       sign in and store an account\n"
	        "  token       acquire an access token (bearer, or PoP with --req-cnf)\n"
	        "  accounts    list stored accounts\n"
	        "  logout      forget an account\n"
	        "\n"
	        "OPTIONS\n"
	        "  --authority <host>     Entra ID authority host\n"
	        "                         (login.microsoftonline.com, login.microsoftonline.us)\n"
	        "  --tenant <id>          tenant identifier, or 'common'\n"
	        "  --client-id <guid>     public client id [default: " ETH_AVD_CLIENT_ID "]\n"
	        "  --scope <scope>        one scope, repeatable, given decoded\n"
	        "  --req-cnf <b64url>     PoP confirmation object from the caller;\n"
	        "                         its presence makes this a proof-of-possession request\n"
	        "  --account <id>         which stored account to use\n"
	        "  --prompt <policy>      auto (default) | always | never\n"
	        "  --json                 emit a JSON object on stdout\n"
	        "  --config <path>        configuration file (or ENTRA_TOKEN_HELPER_CONFIG)\n"
	        "  --verbose              raise the log level on stderr\n"
	        "  --help, --version      print this, or the version, and exit\n"
	        "\n"
	        "EXIT CODES\n"
	        "   0 success                   30 no such account / not signed in\n"
	        "  10 interaction required      40 unavailable (no portal, no keyring)\n"
	        "  20 cancelled by user         50 authorization server error\n"
	        "  64 usage                     70 internal\n"
	        "\n"
	        "THE PORTAL, AND WHY IT MAY NOT BE THERE\n"
	        "  Interactive sign-in goes through xdg-desktop-portal:\n"
	        "    org.freedesktop.portal.experimental.WebAuthentication\n"
	        "  on org.freedesktop.portal.Desktop at /org/freedesktop/portal/desktop.\n"
	        "  This client calls the portal and never a backend.\n"
	        "\n"
	        "  That interface is EXPERIMENTAL and is not exported unless the portal was\n"
	        "  started with\n"
	        "    XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication\n"
	        "  With the gate off the interface is absent entirely, and an interactive\n"
	        "  request exits 40 (unavailable) saying so, so that a dispatcher can fall\n"
	        "  through to another provider -- FreeRDP's terminal paste flow, say. The\n"
	        "  same 40 covers a portal with the gate on but no backend installed, which\n"
	        "  is indistinguishable by design.\n"
	        "\n"
	        "STATUS\n"
	        "  Design sketch. Nothing is implemented: every verb exits 70.\n"
	        "  See docs/ENTRA-CLIENT-CLI.md and docs/ARCHITECTURE.md.\n");
}

static EthVerb eth_verb_from_string(const char* s)
{
	if (g_strcmp0(s, "login") == 0)
		return ETH_VERB_LOGIN;
	if (g_strcmp0(s, "token") == 0)
		return ETH_VERB_TOKEN;
	if (g_strcmp0(s, "accounts") == 0)
		return ETH_VERB_ACCOUNTS;
	if (g_strcmp0(s, "logout") == 0)
		return ETH_VERB_LOGOUT;
	return ETH_VERB_NONE;
}

static const char* eth_verb_to_string(EthVerb verb)
{
	switch (verb)
	{
		case ETH_VERB_LOGIN:
			return "login";
		case ETH_VERB_TOKEN:
			return "token";
		case ETH_VERB_ACCOUNTS:
			return "accounts";
		case ETH_VERB_LOGOUT:
			return "logout";
		case ETH_VERB_NONE:
		default:
			return "none";
	}
}

/* Report a usage problem on stderr. stdout stays empty on every non-zero exit. */
static EthExit eth_usage_error(const char* fmt, ...) G_GNUC_PRINTF(1, 2);

static EthExit eth_usage_error(const char* fmt, ...)
{
	va_list ap;

	fputs("entra-token-helper: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	fputs("Try 'entra-token-helper --help'.\n", stderr);

	return ETH_EXIT_USAGE;
}

/* Take the value of an option, either "--opt value" or "--opt=value". */
static gboolean eth_take_value(int argc, char** argv, int* i, const char* name, const char* eq,
                               const char** out)
{
	if (*out != NULL && g_strcmp0(name, "--scope") != 0)
	{
		(void)eth_usage_error("%s given more than once", name);
		return FALSE;
	}

	if (eq != NULL)
	{
		if (eq[1] == '\0')
		{
			(void)eth_usage_error("%s needs a value", name);
			return FALSE;
		}
		*out = &eq[1];
		return TRUE;
	}

	if (*i + 1 >= argc)
	{
		(void)eth_usage_error("%s needs a value", name);
		return FALSE;
	}

	*i += 1;
	*out = argv[*i];
	return TRUE;
}

/* Print the "not implemented" response in whichever form was asked for.
 *
 * The JSON shape is the error response of docs/ENTRA-CLIENT-CLI.md, so a caller written
 * against the contract sees a well formed object even from the sketch. It goes to
 * stderr, not stdout: a non-zero exit leaves stdout empty. */
static EthExit eth_not_implemented(const EthRequest* req)
{
	static const char message[] = "not implemented (design sketch)";

	if (req->json)
	{
		fprintf(stderr,
		        "{\n"
		        "  \"schema\": 1,\n"
		        "  \"status\": \"internal\",\n"
		        "  \"error\": \"not_implemented\",\n"
		        "  \"message\": \"%s\",\n"
		        "  \"verb\": \"%s\"\n"
		        "}\n",
		        message, eth_verb_to_string(req->verb));
	}
	else
		fprintf(stderr, "entra-token-helper: %s: %s\n", eth_verb_to_string(req->verb), message);

	return ETH_EXIT_INTERNAL;
}

int main(int argc, char** argv)
{
	EthRequest req = { 0 };
	const char* prompt = NULL;
	int rc = ETH_EXIT_USAGE;

	req.client_id = ETH_AVD_CLIENT_ID;
	req.prompt = ETH_PROMPT_AUTO;
	req.scopes = g_ptr_array_new();

	for (int i = 1; i < argc; i++)
	{
		const char* arg = argv[i];

		if (g_strcmp0(arg, "--help") == 0 || g_strcmp0(arg, "-h") == 0)
		{
			eth_usage(stdout);
			rc = ETH_EXIT_SUCCESS;
			goto out;
		}

		if (g_strcmp0(arg, "--version") == 0 || g_strcmp0(arg, "-V") == 0)
		{
			printf("entra-token-helper " ETH_VERSION " (design sketch, not implemented)\n");
			rc = ETH_EXIT_SUCCESS;
			goto out;
		}

		if (g_strcmp0(arg, "--") == 0)
		{
			if (i + 1 < argc)
			{
				rc = eth_usage_error("unexpected argument '%s'", argv[i + 1]);
				goto out;
			}
			continue;
		}

		if (arg[0] != '-')
		{
			if (req.verb != ETH_VERB_NONE)
			{
				rc = eth_usage_error("unexpected argument '%s'", arg);
				goto out;
			}

			req.verb = eth_verb_from_string(arg);
			if (req.verb == ETH_VERB_NONE)
			{
				rc = eth_usage_error("unknown verb '%s'", arg);
				goto out;
			}
			continue;
		}

		{
			char* eq = strchr(arg, '=');
			gsize namelen = eq ? (gsize)(eq - arg) : strlen(arg);
			const char* value = NULL;

			if (strncmp(arg, "--json", namelen) == 0 && namelen == 6)
			{
				if (eq)
				{
					rc = eth_usage_error("--json takes no value");
					goto out;
				}
				req.json = TRUE;
				continue;
			}

			if (strncmp(arg, "--verbose", namelen) == 0 && namelen == 9)
			{
				if (eq)
				{
					rc = eth_usage_error("--verbose takes no value");
					goto out;
				}
				req.verbose = TRUE;
				continue;
			}

#define ETH_OPT(literal, field)                                                      \
	if (namelen == strlen(literal) && strncmp(arg, literal, namelen) == 0)           \
	{                                                                                \
		if (!eth_take_value(argc, argv, &i, literal, eq, &(field)))                  \
		{                                                                            \
			rc = ETH_EXIT_USAGE;                                                     \
			goto out;                                                                \
		}                                                                            \
		continue;                                                                    \
	}

			ETH_OPT("--authority", req.authority)
			ETH_OPT("--tenant", req.tenant)
			ETH_OPT("--client-id", req.client_id)
			ETH_OPT("--req-cnf", req.req_cnf)
			ETH_OPT("--account", req.account)
			ETH_OPT("--config", req.config)
			ETH_OPT("--prompt", prompt)

#undef ETH_OPT

			if (namelen == 7 && strncmp(arg, "--scope", namelen) == 0)
			{
				value = NULL;
				if (!eth_take_value(argc, argv, &i, "--scope", eq, &value))
				{
					rc = ETH_EXIT_USAGE;
					goto out;
				}
				g_ptr_array_add(req.scopes, (gpointer)value);
				continue;
			}

			rc = eth_usage_error("unknown option '%.*s'", (int)namelen, arg);
			goto out;
		}
	}

	if (req.verb == ETH_VERB_NONE)
	{
		eth_usage(stderr);
		rc = ETH_EXIT_USAGE;
		goto out;
	}

	if (prompt != NULL)
	{
		if (g_strcmp0(prompt, "auto") == 0)
			req.prompt = ETH_PROMPT_AUTO;
		else if (g_strcmp0(prompt, "always") == 0)
			req.prompt = ETH_PROMPT_ALWAYS;
		else if (g_strcmp0(prompt, "never") == 0)
			req.prompt = ETH_PROMPT_NEVER;
		else
		{
			rc = eth_usage_error("--prompt must be one of auto, always, never");
			goto out;
		}
	}

	/* Option/verb combinations the contract rules out. Checked here so that a bad
	 * invocation is a usage error even in the sketch, and so the rules are written
	 * down in code as well as in docs/ENTRA-CLIENT-CLI.md. */
	switch (req.verb)
	{
		case ETH_VERB_LOGIN:
			if (req.prompt == ETH_PROMPT_NEVER)
			{
				rc = eth_usage_error("login cannot be run with --prompt never");
				goto out;
			}
			if (req.req_cnf != NULL)
			{
				rc = eth_usage_error("--req-cnf applies to 'token', not 'login'");
				goto out;
			}
			break;

		case ETH_VERB_TOKEN:
			if (req.scopes->len == 0)
			{
				rc = eth_usage_error("token needs at least one --scope");
				goto out;
			}
			break;

		case ETH_VERB_ACCOUNTS:
		case ETH_VERB_LOGOUT:
			if (req.req_cnf != NULL || req.scopes->len > 0 || prompt != NULL)
			{
				rc = eth_usage_error("%s takes only --account, --json and --config",
				                     eth_verb_to_string(req.verb));
				goto out;
			}
			break;

		case ETH_VERB_NONE:
		default:
			g_assert_not_reached();
	}

	if ((req.verb == ETH_VERB_LOGIN || req.verb == ETH_VERB_TOKEN) && req.authority == NULL)
	{
		rc = eth_usage_error("%s needs --authority", eth_verb_to_string(req.verb));
		goto out;
	}

	/* Everything past this point would build the request object of ipc/request.h and
	 * hand it to the OAuth components, which would call xdg-desktop-portal when
	 * interaction is needed. None of that exists. */
	rc = eth_not_implemented(&req);

out:
	g_ptr_array_free(req.scopes, TRUE);
	return rc;
}
