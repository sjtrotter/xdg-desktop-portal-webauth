/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * entra-token-helper - Entra ID / AVD token client; a portal CONSUMER.
 *
 * The command line front end described in docs/ENTRA-CLIENT-CLI.md: it parses the
 * four verbs and their options, validates them, and hands the request to acquire.c.
 * It talks to xdg-desktop-portal and to nothing else, and it never sees, names or
 * depends on whichever backend the portal routes to.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>

#include "acquire.h"
#include "cache/keyring.h"
#include "entra-config.h"
#include "entra-error.h"
#include "ipc/request.h"
#include "log/redact.h"
#include "oauth/clouds.h"
#include "webauth_client.h"

typedef enum
{
	ETH_VERB_NONE,
	ETH_VERB_LOGIN,
	ETH_VERB_TOKEN,
	ETH_VERB_ACCOUNTS,
	ETH_VERB_LOGOUT
} EthVerb;

typedef struct
{
	EthVerb verb;
	const char* authority;
	const char* tenant;
	const char* cloud;
	const char* client_id;
	GPtrArray* scopes; /* of const char*, borrowed from argv */
	const char* req_cnf;
	const char* account;
	const char* config;
	const char* parent_window;
	const char* session_mode;
	const char* format;
	const char* timeout;
	EntraPrompt prompt;
	gboolean json;
	gboolean verbose;
} EthRequest;

#define ETH_DEFAULT_TIMEOUT_SECONDS 300

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
	        "  --authority <a>        the Entra ID authority: a host\n"
	        "                         (login.microsoftonline.us) or the https URL form\n"
	        "                         (https://login.microsoftonline.us/<tenant>)\n"
	        "  --tenant <id>          tenant identifier, or 'common'\n"
	        "  --cloud <name>         commercial | usgov: fills in the authority host and\n"
	        "                         the default AVD scope\n"
	        "  --client-id <guid>     public client id [default: " ENTRA_AVD_CLIENT_ID "]\n"
	        "  --scope <scope>        repeatable, and may carry a space separated list\n"
	        "  --req-cnf <b64url>     PoP confirmation object from the caller;\n"
	        "                         its presence makes this a proof-of-possession request\n"
	        "  --account <id>         which stored account to use\n"
	        "  --prompt <policy>      auto (default) | always | never\n"
	        "  --parent-window <h>    the caller's window handle, passed to the portal\n"
	        "  --session-mode <m>     shared | ephemeral, passed to the portal\n"
	        "  --format <f>           raw (default) | json\n"
	        "  --json                 the same as --format json\n"
	        "  --timeout <seconds>    how long the sign-in window may stay up\n"
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
	        "THE REDIRECT IS THE SAME IN BOTH CLOUDS\n"
	        "  " ENTRA_NATIVECLIENT_REDIRECT "\n"
	        "  is the AVD application registration's only redirect URI. There is no .us\n"
	        "  variant; it was tried against login.microsoftonline.us on hardware and\n"
	        "  rejected with AADSTS50011. It is never taken from a caller.\n"
	        "\n"
	        "THE PORTAL, AND WHY IT MAY NOT BE THERE\n"
	        "  Interactive sign-in goes through xdg-desktop-portal:\n"
	        "    " ENTRA_PORTAL_INTERFACE "\n"
	        "  on " ENTRA_PORTAL_BUS_NAME " at " ENTRA_PORTAL_OBJECT_PATH ".\n"
	        "  This client calls the portal and never a backend.\n"
	        "\n"
	        "  That interface is EXPERIMENTAL and is not exported unless the portal was\n"
	        "  started with\n"
	        "    XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=" ENTRA_PORTAL_EXPERIMENTAL_FLAG "\n"
	        "  With the gate off the interface is absent entirely, and an interactive\n"
	        "  request exits 40 (unavailable) saying so, so that a dispatcher can fall\n"
	        "  through to another provider -- FreeRDP's terminal paste flow, say. The\n"
	        "  same 40 covers a portal with the gate on but no backend installed, which\n"
	        "  is indistinguishable by design.\n");
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
static int eth_usage_error(const char* fmt, ...) G_GNUC_PRINTF(1, 2);

static int eth_usage_error(const char* fmt, ...)
{
	va_list ap;

	fputs("entra-token-helper: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	fputs("Try 'entra-token-helper --help'.\n", stderr);

	return entra_status_exit_code(ENTRA_STATUS_USAGE);
}

/* Take the value of an option, either "--opt value" or "--opt=value". */
static gboolean eth_take_value(int argc, char** argv, int* i, const char* name, const char* eq,
                               const char** out)
{
	if (*out != NULL)
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

/* Every failure leaves stdout empty and says why on stderr, in the same shape the
 * request was made in. The message has already been redacted by whoever set it. */
static int eth_fail(const EthRequest* request, GError* error)
{
	EntraStatus status = entra_status_from_error(error);

	if (request->json)
	{
		g_autofree char* json = entra_response_error_json(status, entra_status_symbol(status),
		                                                  error != NULL ? error->message : "failed");

		fprintf(stderr, "%s\n", json);
	}
	else
	{
		fprintf(stderr, "entra-token-helper: %s\n", error != NULL ? error->message : "failed");
	}

	return entra_status_exit_code(status);
}

static gboolean eth_build_acquire(const EthRequest* request, EntraAcquire* acquire,
                                  EntraConfig* config, GError** error)
{
	const EntraCloud* cloud = entra_cloud_by_name(request->cloud);
	const char* authority = request->authority;
	g_autoptr(GPtrArray) scopes = g_ptr_array_new();

	if (request->cloud != NULL && cloud == NULL)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_USAGE,
		                    "--cloud takes commercial or usgov");
		return FALSE;
	}

	if (authority == NULL && cloud != NULL)
		authority = cloud->authority;

	if (!entra_authority_parse(authority, request->tenant, &acquire->authority, error))
		return FALSE;

	if (cloud != NULL && entra_cloud_for_authority(acquire->authority.host) != cloud)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_USAGE,
		                    "--cloud and --authority name different clouds");
		return FALSE;
	}

	for (guint i = 0; i < request->scopes->len; i++)
		g_ptr_array_add(scopes, g_ptr_array_index(request->scopes, i));

	if (scopes->len == 0 && cloud != NULL)
	{
		g_ptr_array_add(scopes, (gpointer)cloud->avd_scope);
		g_ptr_array_add(scopes, (gpointer) "openid");
		g_ptr_array_add(scopes, (gpointer) "profile");
		g_ptr_array_add(scopes, (gpointer) "offline_access");
	}

	g_ptr_array_add(scopes, NULL);

	acquire->client_id = request->client_id != NULL ? request->client_id : ENTRA_AVD_CLIENT_ID;
	acquire->scopes = entra_scopes_split((const char* const*)scopes->pdata);
	acquire->account = request->account;
	acquire->req_cnf = request->req_cnf;
	acquire->parent_window = request->parent_window;
	acquire->session_mode = request->session_mode;
	acquire->prompt = request->prompt;
	acquire->config = config;
	acquire->timeout_seconds =
	    request->timeout != NULL ? (guint)g_ascii_strtoull(request->timeout, NULL, 10)
	                             : ETH_DEFAULT_TIMEOUT_SECONDS;

	return entra_acquire_prepare(acquire, error);
}

static int eth_do_login(const EthRequest* request, EntraConfig* config)
{
	EntraAcquire acquire = { 0 };
	g_autoptr(GError) error = NULL;
	g_autofree char* account = NULL;
	int rc;

	if (!eth_build_acquire(request, &acquire, config, &error) ||
	    !entra_acquire_login(&acquire, &account, &error))
	{
		rc = eth_fail(request, error);
		entra_acquire_clear(&acquire);
		return rc;
	}

	if (request->json)
	{
		g_autoptr(GPtrArray) records = g_ptr_array_new_with_free_func(
		    (GDestroyNotify)entra_account_record_free);
		EntraAccountRecord* record = entra_account_record_new();
		g_autofree char* json = NULL;

		record->account = g_strdup(account);
		record->host = g_strdup(acquire.authority.host);
		record->tenant = g_strdup(acquire.authority.tenant);
		record->client_id = g_strdup(acquire.client_id);
		g_ptr_array_add(records, record);

		json = entra_response_accounts_json(records);
		printf("%s\n", json);
	}
	else
	{
		printf("Signed in as %s\n", account);
	}

	entra_acquire_clear(&acquire);
	return 0;
}

static int eth_do_token(const EthRequest* request, EntraConfig* config)
{
	EntraAcquire acquire = { 0 };
	EntraTokenResult result = { 0 };
	g_autoptr(GError) error = NULL;
	int rc = 0;

	if (!eth_build_acquire(request, &acquire, config, &error) ||
	    !entra_acquire_token(&acquire, &result, &error))
	{
		rc = eth_fail(request, error);
		entra_token_result_clear(&result);
		entra_acquire_clear(&acquire);
		return rc;
	}

	if (request->json)
	{
		g_autofree char* json = entra_response_token_json(result.access_token, result.token_type,
		                                                  result.expires_in, result.scope,
		                                                  result.account);

		printf("%s\n", json);
	}
	else
	{
		/* The token and a newline, nothing else: a caller captures stdout. */
		printf("%s\n", result.access_token);
	}

	entra_token_result_clear(&result);
	entra_acquire_clear(&acquire);
	return 0;
}

static int eth_do_accounts(const EthRequest* request)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) records = NULL;

	if (!entra_keyring_available(&error))
		return eth_fail(request, error);

	records = entra_keyring_list(&error);
	if (records == NULL)
		return eth_fail(request, error);

	if (request->json)
	{
		g_autofree char* json = entra_response_accounts_json(records);

		printf("%s\n", json);
		return 0;
	}

	for (guint i = 0; i < records->len; i++)
	{
		const EntraAccountRecord* record = g_ptr_array_index(records, i);

		printf("%s\t%s\t%s\n", record->account, record->host != NULL ? record->host : "-",
		       record->tenant != NULL ? record->tenant : "-");
	}

	return 0;
}

static int eth_do_logout(const EthRequest* request)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) records = NULL;
	guint removed = 0;

	if (!entra_keyring_available(&error))
		return eth_fail(request, error);

	records = entra_keyring_list(&error);
	if (records == NULL)
		return eth_fail(request, error);

	if (!entra_keyring_forget(NULL, NULL, request->account, &removed, &error))
		return eth_fail(request, error);

	if (request->json)
	{
		g_autoptr(GPtrArray) gone = g_ptr_array_new();
		g_autofree char* json = NULL;

		for (guint i = 0; i < records->len; i++)
		{
			EntraAccountRecord* record = g_ptr_array_index(records, i);

			if (request->account == NULL || g_strcmp0(record->account, request->account) == 0)
				g_ptr_array_add(gone, record);
		}

		json = entra_response_accounts_json(gone);
		printf("%s\n", json);
		return 0;
	}

	for (guint i = 0; i < records->len; i++)
	{
		const EntraAccountRecord* record = g_ptr_array_index(records, i);

		if (request->account == NULL || g_strcmp0(record->account, request->account) == 0)
			printf("Removed %s\n", record->account);
	}

	if (removed == 0)
		fprintf(stderr, "entra-token-helper: nothing to remove\n");

	return 0;
}

int main(int argc, char** argv)
{
	EthRequest request = { 0 };
	g_autoptr(EntraConfig) config = NULL;
	g_autoptr(GError) error = NULL;
	const char* prompt = NULL;
	int rc = entra_status_exit_code(ENTRA_STATUS_USAGE);

	request.prompt = ENTRA_PROMPT_AUTO;
	request.scopes = g_ptr_array_new();

	for (int i = 1; i < argc; i++)
	{
		const char* arg = argv[i];
		char* eq = NULL;
		gsize namelen = 0;

		if (g_strcmp0(arg, "--help") == 0 || g_strcmp0(arg, "-h") == 0)
		{
			eth_usage(stdout);
			rc = 0;
			goto out;
		}

		if (g_strcmp0(arg, "--version") == 0 || g_strcmp0(arg, "-V") == 0)
		{
			printf("entra-token-helper " ENTRA_VERSION "\n");
			rc = 0;
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
			if (request.verb != ETH_VERB_NONE)
			{
				rc = eth_usage_error("unexpected argument '%s'", arg);
				goto out;
			}

			request.verb = eth_verb_from_string(arg);
			if (request.verb == ETH_VERB_NONE)
			{
				rc = eth_usage_error("unknown verb '%s'", arg);
				goto out;
			}
			continue;
		}

		eq = strchr(arg, '=');
		namelen = eq ? (gsize)(eq - arg) : strlen(arg);

		if (namelen == 6 && strncmp(arg, "--json", namelen) == 0)
		{
			if (eq)
			{
				rc = eth_usage_error("--json takes no value");
				goto out;
			}
			request.json = TRUE;
			continue;
		}

		if (namelen == 9 && strncmp(arg, "--verbose", namelen) == 0)
		{
			if (eq)
			{
				rc = eth_usage_error("--verbose takes no value");
				goto out;
			}
			request.verbose = TRUE;
			continue;
		}

#define ETH_OPT(literal, field)                                            \
	if (namelen == strlen(literal) && strncmp(arg, literal, namelen) == 0) \
	{                                                                      \
		if (!eth_take_value(argc, argv, &i, literal, eq, &(field)))        \
		{                                                                  \
			rc = entra_status_exit_code(ENTRA_STATUS_USAGE);               \
			goto out;                                                      \
		}                                                                  \
		continue;                                                          \
	}

		ETH_OPT("--authority", request.authority)
		ETH_OPT("--tenant", request.tenant)
		ETH_OPT("--cloud", request.cloud)
		ETH_OPT("--client-id", request.client_id)
		ETH_OPT("--req-cnf", request.req_cnf)
		ETH_OPT("--account", request.account)
		ETH_OPT("--config", request.config)
		ETH_OPT("--prompt", prompt)
		ETH_OPT("--parent-window", request.parent_window)
		ETH_OPT("--session-mode", request.session_mode)
		ETH_OPT("--format", request.format)
		ETH_OPT("--timeout", request.timeout)

#undef ETH_OPT

		if (namelen == 7 && strncmp(arg, "--scope", namelen) == 0)
		{
			const char* value = NULL;

			if (!eth_take_value(argc, argv, &i, "--scope", eq, &value))
			{
				rc = entra_status_exit_code(ENTRA_STATUS_USAGE);
				goto out;
			}

			g_ptr_array_add(request.scopes, (gpointer)value);
			continue;
		}

		rc = eth_usage_error("unknown option '%.*s'", (int)namelen, arg);
		goto out;
	}

	if (request.verb == ETH_VERB_NONE)
	{
		eth_usage(stderr);
		rc = entra_status_exit_code(ENTRA_STATUS_USAGE);
		goto out;
	}

	if (prompt != NULL)
	{
		if (g_strcmp0(prompt, "auto") == 0)
			request.prompt = ENTRA_PROMPT_AUTO;
		else if (g_strcmp0(prompt, "always") == 0)
			request.prompt = ENTRA_PROMPT_ALWAYS;
		else if (g_strcmp0(prompt, "never") == 0)
			request.prompt = ENTRA_PROMPT_NEVER;
		else
		{
			rc = eth_usage_error("--prompt must be one of auto, always, never");
			goto out;
		}
	}

	if (request.format != NULL)
	{
		if (g_strcmp0(request.format, "json") == 0)
			request.json = TRUE;
		else if (g_strcmp0(request.format, "raw") != 0)
		{
			rc = eth_usage_error("--format must be raw or json");
			goto out;
		}
	}

	if (request.session_mode != NULL && g_strcmp0(request.session_mode, "shared") != 0 &&
	    g_strcmp0(request.session_mode, "ephemeral") != 0)
	{
		rc = eth_usage_error("--session-mode must be shared or ephemeral");
		goto out;
	}

	/* Option/verb combinations the contract rules out, written down in code as
	 * well as in docs/ENTRA-CLIENT-CLI.md. */
	switch (request.verb)
	{
		case ETH_VERB_LOGIN:
			if (request.prompt == ENTRA_PROMPT_NEVER)
			{
				rc = eth_usage_error("login cannot be run with --prompt never");
				goto out;
			}
			if (request.req_cnf != NULL)
			{
				rc = eth_usage_error("--req-cnf applies to 'token', not 'login'");
				goto out;
			}
			break;

		case ETH_VERB_TOKEN:
			if (request.scopes->len == 0 && request.cloud == NULL)
			{
				rc = eth_usage_error("token needs at least one --scope, or --cloud");
				goto out;
			}
			break;

		case ETH_VERB_ACCOUNTS:
		case ETH_VERB_LOGOUT:
			if (request.req_cnf != NULL || request.scopes->len > 0 || prompt != NULL)
			{
				rc = eth_usage_error("%s takes only --account, --format and --config",
				                     eth_verb_to_string(request.verb));
				goto out;
			}
			break;

		case ETH_VERB_NONE:
		default:
			g_assert_not_reached();
	}

	if ((request.verb == ETH_VERB_LOGIN || request.verb == ETH_VERB_TOKEN) &&
	    request.authority == NULL && request.cloud == NULL)
	{
		rc = eth_usage_error("%s needs --authority", eth_verb_to_string(request.verb));
		goto out;
	}

	entra_log_set_verbose(request.verbose);

	config = entra_config_load(request.config, &error);
	if (config == NULL)
	{
		rc = eth_fail(&request, error);
		goto out;
	}

	switch (request.verb)
	{
		case ETH_VERB_LOGIN:
			rc = eth_do_login(&request, config);
			break;
		case ETH_VERB_TOKEN:
			rc = eth_do_token(&request, config);
			break;
		case ETH_VERB_ACCOUNTS:
			rc = eth_do_accounts(&request);
			break;
		case ETH_VERB_LOGOUT:
			rc = eth_do_logout(&request);
			break;
		case ETH_VERB_NONE:
		default:
			g_assert_not_reached();
	}

out:
	g_ptr_array_free(request.scopes, TRUE);
	return rc;
}
