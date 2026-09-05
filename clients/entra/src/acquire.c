/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */

#include <string.h>

#include <glib.h>

#include "acquire.h"
#include "entra-error.h"
#include "log/redact.h"
#include "oauth/callback.h"
#include "oauth/jwt.h"
#include "oauth/token.h"
#include "oauth/transaction.h"
#include "webauth_client.h"

void entra_token_result_clear(EntraTokenResult* result)
{
	if (result == NULL)
		return;

	entra_scrub(result->access_token);
	g_clear_pointer(&result->token_type, g_free);
	g_clear_pointer(&result->scope, g_free);
	g_clear_pointer(&result->account, g_free);
	result->access_token = NULL;
	result->expires_in = 0;
}

gboolean entra_acquire_prepare(EntraAcquire* self, GError** error)
{
	if (!entra_authority_allowed(&self->authority, self->config))
	{
		g_set_error(error, ENTRA_ERROR, ENTRA_ERROR_USAGE,
		            "%s is not an authority this client will talk to. The allowlist is "
		            "login.microsoftonline.com and login.microsoftonline.us; a user may add to "
		            "it in the configuration file, and nothing else can.",
		            self->authority.host);
		return FALSE;
	}

	if (!entra_cloud_client_id_allowed(self->client_id, self->config))
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_USAGE,
		                    "that client id is not allowlisted; a user may add it in the "
		                    "configuration file, and nothing else can");
		return FALSE;
	}

	if (self->scopes == NULL || self->scopes[0] == NULL)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_USAGE, "at least one --scope");
		return FALSE;
	}

	self->redirect = ENTRA_NATIVECLIENT_REDIRECT;
	self->scope_request = g_strjoinv(" ", self->scopes);
	self->scope_key = entra_cache_scope_key((const char* const*)self->scopes);

	self->http = entra_http_new(self->config, error);
	if (self->http == NULL)
		return FALSE;

	return entra_endpoints_derive(&self->authority, &self->endpoints, error);
}

/* Discovery REFINES; it does not gate, and it does not happen until an endpoint
 * is about to be used. A failure is a DEBUG line and the derived endpoints stand.
 * FreeRDP has the opposite coupling -- it fetches the OpenID configuration before
 * asking a provider for a token at all -- and it is why a provider perfectly able
 * to resolve the authority is blocked when that fetch fails. */
static void entra_acquire_discover(EntraAcquire* self)
{
	g_autoptr(GError) local = NULL;

	if (self->discovered)
		return;

	self->discovered = TRUE;

	if (!entra_endpoints_discover(self->http, &self->authority, &self->endpoints, NULL, &local))
		entra_log_event(G_LOG_LEVEL_DEBUG, ENTRA_EVENT_DISCOVERY, "outcome", ENTRA_FIELD_OUTCOME,
		                "derived", "authority", ENTRA_FIELD_AUTHORITY, self->authority.host, NULL);
}

void entra_acquire_clear(EntraAcquire* self)
{
	if (self == NULL)
		return;

	entra_authority_clear(&self->authority);
	entra_endpoints_clear(&self->endpoints);
	g_clear_pointer(&self->scopes, g_strfreev);
	g_clear_pointer(&self->scope_request, g_free);
	g_clear_pointer(&self->scope_key, g_free);
	g_clear_pointer(&self->http, entra_http_free);
}

/* One interactive transaction: authorize through the portal, classify what comes
 * back, exchange the code. @prompt_value is what goes in the authorization URL's
 * prompt parameter, or NULL to let the authority decide — which is what the
 * proof-of-possession step wants, because that is where Entra shows the "you are
 * connecting to a remote desktop" interstitial and a prompt of ours would fight it. */
static gboolean entra_interactive(EntraAcquire* self, const char* scope, const char* req_cnf,
                                  const char* prompt_value, const char* login_hint,
                                  EntraTokenSet* out, GError** error)
{
	g_autoptr(EntraTransaction) transaction = NULL;
	g_autofree char* start_uri = NULL;
	g_autofree char* completion = NULL;
	g_autofree char* reason = NULL;
	g_autofree char* code = NULL;
	g_autofree char* oauth_error = NULL;
	g_autofree char* callback_error = NULL;
	EntraCallbackResult classified;
	EntraWebAuthResult started;
	gboolean ok;

	if (self->prompt == ENTRA_PROMPT_NEVER)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_INTERACTION_REQUIRED,
		                    "a sign-in window is required and --prompt never was given");
		return FALSE;
	}

	if (!entra_webauth_available(error))
		return FALSE;

	entra_acquire_discover(self);

	transaction = entra_transaction_new(self->redirect, self->timeout_seconds, error);
	if (transaction == NULL)
		return FALSE;

	start_uri = entra_build_authorize_url(
	    &self->endpoints, self->client_id, scope, entra_transaction_state(transaction),
	    entra_transaction_challenge(transaction), self->redirect, prompt_value, login_hint);

	entra_log_event(G_LOG_LEVEL_DEBUG, ENTRA_EVENT_AUTHORIZE, "authority", ENTRA_FIELD_AUTHORITY,
	                self->authority.host, "tenant", ENTRA_FIELD_TENANT, self->authority.tenant,
	                "pop", ENTRA_FIELD_OUTCOME, req_cnf != NULL ? "yes" : "no", NULL);

	started = entra_webauth_start(self->parent_window, start_uri, self->redirect,
	                              self->session_mode, "Sign in to Azure Virtual Desktop",
	                              self->timeout_seconds, NULL, &completion, &reason, error);
	if (started != ENTRA_WEBAUTH_COMPLETED)
		return FALSE;

	classified = entra_callback_classify(transaction, completion, &code, &callback_error);
	entra_log_event(G_LOG_LEVEL_DEBUG, ENTRA_EVENT_CALLBACK, "outcome", ENTRA_FIELD_OUTCOME,
	                entra_callback_result_str(classified), NULL);

	switch (classified)
	{
		case ENTRA_CALLBACK_CODE:
			break;

		case ENTRA_CALLBACK_ERROR:
			g_set_error(error, ENTRA_ERROR, ENTRA_ERROR_SERVER, "the authority declined: %s",
			            callback_error);
			return FALSE;

		case ENTRA_CALLBACK_UNRELATED:
		case ENTRA_CALLBACK_INVALID:
		default:
			g_set_error(error, ENTRA_ERROR, ENTRA_ERROR_INTERNAL,
			            "the sign-in ended somewhere this request cannot accept: %s",
			            entra_callback_result_str(classified));
			return FALSE;
	}

	ok = entra_token_by_code(self->http, self->endpoints.token_endpoint, self->client_id, code,
	                         self->redirect, entra_transaction_verifier(transaction), scope,
	                         req_cnf, out, &oauth_error, NULL, error);
	entra_scrub(g_steal_pointer(&code));
	return ok;
}

static const char* entra_prompt_value(EntraAcquire* self)
{
	/* select_account by default: on a machine with two cards or two tenants,
	 * silently reusing whichever session the browser store remembers is how a
	 * person ends up signed in as the wrong identity without being asked. */
	return self->prompt == ENTRA_PROMPT_ALWAYS ? "login" : "select_account";
}

static EntraAccountRecord* entra_record_for(EntraAcquire* self, const EntraTokenSet* set,
                                            const char* account)
{
	EntraAccountRecord* record = entra_account_record_new();

	record->account = g_strdup(account);
	record->authority = g_strdup(self->authority.base);
	record->host = g_strdup(self->authority.host);
	record->tenant = g_strdup(self->authority.tenant);
	record->client_id = g_strdup(self->client_id);
	record->refresh_token = g_strdup(set->refresh_token);
	record->id_token = g_strdup(set->id_token);
	return record;
}

gboolean entra_acquire_login(EntraAcquire* self, char** account_out, GError** error)
{
	EntraTokenSet set = { 0 };
	g_autoptr(EntraAccountRecord) record = NULL;
	g_autofree char* account = NULL;
	gboolean ok = FALSE;

	if (!entra_keyring_available(error))
		return FALSE;

	if (!entra_interactive(self, self->scope_request, NULL, entra_prompt_value(self), self->account,
	                       &set, error))
		return FALSE;

	account = entra_jwt_account_name(set.id_token);
	if (account == NULL)
	{
		/* No ID token, or no usable name in it. The account still has to be
		 * called something a person can pick out of `accounts`. */
		account = g_strdup_printf("unknown@%s", self->authority.host);
	}

	record = entra_record_for(self, &set, account);
	if (set.access_token != NULL)
		entra_account_record_store_token(record, self->scope_key, set.access_token, set.token_type,
		                                 set.scope, set.expires_at);

	ok = entra_keyring_store(record, error);
	entra_token_set_clear(&set);

	if (ok && account_out != NULL)
		*account_out = g_steal_pointer(&account);

	return ok;
}

static void entra_result_from_set(EntraAcquire* self, const EntraTokenSet* set, const char* account,
                                  EntraTokenResult* result)
{
	entra_token_result_clear(result);
	result->access_token = g_strdup(set->access_token);
	result->token_type = g_strdup(set->token_type);
	result->expires_in = set->expires_in;
	result->scope = g_strdup(set->scope != NULL ? set->scope : self->scope_request);
	result->account = g_strdup(account);
}

/* The proof-of-possession path. Never cached, always a fresh grant, and when the
 * authority says the human has to be asked — which for the RDS-AAD scope it
 * routinely does, because that is the scope carrying the "make sure you trust this
 * client" interstitial — a full interactive authorize for THAT scope, exchanged
 * with req_cnf on the authorization code grant. */
static gboolean entra_acquire_pop(EntraAcquire* self, EntraAccountRecord* record,
                                  EntraTokenResult* result, GError** error)
{
	EntraTokenSet set = { 0 };
	g_autofree char* oauth_error = NULL;
	g_autoptr(GError) local = NULL;

	if (record != NULL && record->refresh_token != NULL)
	{
		entra_acquire_discover(self);

		if (entra_token_by_refresh(self->http, self->endpoints.token_endpoint, self->client_id,
		                           record->refresh_token, self->scope_request, self->req_cnf, &set,
		                           &oauth_error, NULL, &local))
		{
			entra_result_from_set(self, &set, record->account, result);
			entra_token_set_clear(&set);
			return TRUE;
		}
	}

	if (record != NULL && local != NULL &&
	    !g_error_matches(local, ENTRA_ERROR, ENTRA_ERROR_INTERACTION_REQUIRED))
	{
		g_propagate_error(error, g_steal_pointer(&local));
		return FALSE;
	}

	entra_log_event(G_LOG_LEVEL_DEBUG, ENTRA_EVENT_TOKEN_RESPONSE, "outcome", ENTRA_FIELD_OUTCOME,
	                "pop-needs-interaction", "error", ENTRA_FIELD_ERROR_CODE, oauth_error, NULL);

	if (self->prompt == ENTRA_PROMPT_NEVER)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_INTERACTION_REQUIRED,
		                    "the proof-of-possession token needs a sign-in window and --prompt "
		                    "never was given");
		return FALSE;
	}

	/* No prompt parameter: this is where Entra shows its own interstitial, and
	 * naming a prompt of ours would only get in its way. */
	if (!entra_interactive(self, self->scope_request, self->req_cnf, NULL,
	                       record != NULL ? record->account : self->account, &set, error))
		return FALSE;

	if (record != NULL && set.refresh_token != NULL)
	{
		g_free(record->refresh_token);
		record->refresh_token = g_strdup(set.refresh_token);
		if (!entra_keyring_store(record, error))
		{
			entra_token_set_clear(&set);
			return FALSE;
		}
	}

	entra_result_from_set(self, &set, record != NULL ? record->account : NULL, result);
	entra_token_set_clear(&set);
	return TRUE;
}

gboolean entra_acquire_token(EntraAcquire* self, EntraTokenResult* result, GError** error)
{
	g_autoptr(EntraAccountRecord) record = NULL;
	g_autoptr(GError) load_error = NULL;
	g_autofree char* oauth_error = NULL;
	g_autoptr(GError) local = NULL;
	EntraTokenSet set = { 0 };

	if (!entra_keyring_available(error))
		return FALSE;

	record = entra_keyring_load(self->authority.base, self->client_id, self->account, &load_error);

	if (record == NULL && !g_error_matches(load_error, ENTRA_ERROR, ENTRA_ERROR_NO_ACCOUNT))
	{
		g_propagate_error(error, g_steal_pointer(&load_error));
		return FALSE;
	}

	if (self->req_cnf != NULL)
	{
		if (record == NULL && self->prompt == ENTRA_PROMPT_NEVER)
		{
			g_propagate_error(error, g_steal_pointer(&load_error));
			return FALSE;
		}

		return entra_acquire_pop(self, record, result, error);
	}

	if (record != NULL && self->prompt != ENTRA_PROMPT_ALWAYS)
	{
		const EntraCachedToken* cached = entra_account_record_lookup(record, self->scope_key);

		if (cached != NULL)
		{
			entra_log_event(G_LOG_LEVEL_DEBUG, ENTRA_EVENT_CACHE, "outcome", ENTRA_FIELD_OUTCOME,
			                "hit", NULL);
			entra_token_result_clear(result);
			result->access_token = g_strdup(cached->access_token);
			result->token_type = g_strdup(cached->token_type);
			result->expires_in = cached->expires_at - g_get_real_time() / G_USEC_PER_SEC;
			result->scope = g_strdup(cached->scope != NULL ? cached->scope : self->scope_request);
			result->account = g_strdup(record->account);
			return TRUE;
		}

		entra_log_event(G_LOG_LEVEL_DEBUG, ENTRA_EVENT_CACHE, "outcome", ENTRA_FIELD_OUTCOME, "miss",
		                NULL);
	}

	if (record != NULL && record->refresh_token != NULL && self->prompt != ENTRA_PROMPT_ALWAYS)
	{
		entra_acquire_discover(self);

		if (entra_token_by_refresh(self->http, self->endpoints.token_endpoint, self->client_id,
		                           record->refresh_token, self->scope_request, NULL, &set,
		                           &oauth_error, NULL, &local))
		{
			if (set.refresh_token != NULL)
			{
				g_free(record->refresh_token);
				record->refresh_token = g_strdup(set.refresh_token);
			}

			entra_account_record_store_token(record, self->scope_key, set.access_token,
			                                 set.token_type, set.scope, set.expires_at);
			if (!entra_keyring_store(record, error))
			{
				entra_token_set_clear(&set);
				return FALSE;
			}

			entra_result_from_set(self, &set, record->account, result);
			entra_token_set_clear(&set);
			return TRUE;
		}
	}

	if (local != NULL && !g_error_matches(local, ENTRA_ERROR, ENTRA_ERROR_INTERACTION_REQUIRED))
	{
		g_propagate_error(error, g_steal_pointer(&local));
		return FALSE;
	}

	if (self->prompt == ENTRA_PROMPT_NEVER)
	{
		if (record == NULL)
			g_propagate_error(error, g_steal_pointer(&load_error));
		else
			g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_INTERACTION_REQUIRED,
			                    "a sign-in window is required and --prompt never was given");
		return FALSE;
	}

	{
		g_autofree char* account = NULL;

		if (!entra_acquire_login(self, &account, error))
			return FALSE;

		g_clear_pointer(&record, entra_account_record_free);
		record = entra_keyring_load(self->authority.base, self->client_id, account, error);
		if (record == NULL)
			return FALSE;

		{
			const EntraCachedToken* cached = entra_account_record_lookup(record, self->scope_key);

			if (cached == NULL)
			{
				g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_INTERNAL,
				                    "the sign-in produced no token for these scopes");
				return FALSE;
			}

			entra_token_result_clear(result);
			result->access_token = g_strdup(cached->access_token);
			result->token_type = g_strdup(cached->token_type);
			result->expires_in = cached->expires_at - g_get_real_time() / G_USEC_PER_SEC;
			result->scope = g_strdup(cached->scope != NULL ? cached->scope : self->scope_request);
			result->account = g_strdup(record->account);
		}
	}

	return TRUE;
}
