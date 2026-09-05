/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * The cloud table, the authority however it was spelled, the discovery document,
 * and the authorization URL built from all three.
 */

#include <string.h>

#include <glib.h>

#include "entra-error.h"
#include "oauth/clouds.h"
#include "oauth/discovery.h"
#include "oauth/token.h"

static void test_the_cloud_table(void)
{
	const EntraCloud* commercial = entra_cloud_by_name("commercial");
	const EntraCloud* usgov = entra_cloud_by_name("usgov");

	g_assert_nonnull(commercial);
	g_assert_nonnull(usgov);
	g_assert_cmpstr(commercial->authority, ==, "login.microsoftonline.com");
	g_assert_cmpstr(usgov->authority, ==, "login.microsoftonline.us");
	g_assert_cmpstr(commercial->avd_scope, ==, "https://www.wvd.microsoft.com/.default");
	g_assert_cmpstr(usgov->avd_scope, ==, "https://www.wvd.azure.us/.default");

	/* THE SAME REDIRECT IN BOTH ROWS. There is no .us variant; asking for one is
	 * rejected with AADSTS50011. */
	g_assert_cmpstr(commercial->redirect, ==, usgov->redirect);
	g_assert_cmpstr(usgov->redirect, ==, ENTRA_NATIVECLIENT_REDIRECT);
	g_assert_true(g_str_has_prefix(usgov->redirect, "https://login.microsoftonline.com/"));

	g_assert_nonnull(entra_cloud_for_authority("LOGIN.MICROSOFTONLINE.US"));
	g_assert_null(entra_cloud_for_authority("login.example.invalid"));
	g_assert_null(entra_cloud_by_name("moon"));
	g_assert_null(entra_cloud_nth(2));
}

static void test_only_the_avd_client_is_allowlisted(void)
{
	g_assert_true(entra_cloud_client_id_allowed(ENTRA_AVD_CLIENT_ID, NULL));
	g_assert_false(entra_cloud_client_id_allowed("00000000-0000-0000-0000-000000000000", NULL));
	g_assert_false(entra_cloud_client_id_allowed(NULL, NULL));
}

static void test_an_authority_host(void)
{
	EntraAuthority authority = { 0 };

	g_assert_true(entra_authority_parse("login.microsoftonline.us", "tenant-1", &authority, NULL));
	g_assert_cmpstr(authority.host, ==, "login.microsoftonline.us");
	g_assert_cmpstr(authority.tenant, ==, "tenant-1");
	g_assert_cmpstr(authority.base, ==, "https://login.microsoftonline.us/tenant-1");
	entra_authority_clear(&authority);
}

static void test_an_authority_url(void)
{
	EntraAuthority authority = { 0 };

	g_assert_true(entra_authority_parse("https://login.microsoftonline.us/tenant-1", NULL,
	                                    &authority, NULL));
	g_assert_cmpstr(authority.host, ==, "login.microsoftonline.us");
	g_assert_cmpstr(authority.tenant, ==, "tenant-1");
	entra_authority_clear(&authority);
}

static void test_an_authority_with_no_tenant_is_common(void)
{
	EntraAuthority authority = { 0 };

	g_assert_true(entra_authority_parse("https://login.microsoftonline.com/", NULL, &authority,
	                                    NULL));
	g_assert_cmpstr(authority.tenant, ==, "common");
	entra_authority_clear(&authority);
}

static void test_refused_authorities(void)
{
	EntraAuthority authority = { 0 };
	g_autoptr(GError) error = NULL;

	g_assert_false(entra_authority_parse(NULL, NULL, &authority, NULL));
	g_assert_false(entra_authority_parse("", NULL, &authority, NULL));

	/* Never a token endpoint, never a scheme this client does not speak, never a
	 * path deeper than the tenant, never a query. */
	g_assert_false(entra_authority_parse("http://login.microsoftonline.us/t", NULL, &authority,
	                                     NULL));
	g_assert_false(entra_authority_parse("https://login.microsoftonline.us/t/oauth2/v2.0/token",
	                                     NULL, &authority, NULL));
	g_assert_false(entra_authority_parse("https://login.microsoftonline.us/t?x=1", NULL, &authority,
	                                     NULL));
	g_assert_false(entra_authority_parse("https://user@login.microsoftonline.us/t", NULL,
	                                     &authority, NULL));
	g_assert_false(entra_authority_parse("login.microsoftonline.us/t", NULL, &authority, NULL));
	g_assert_false(entra_authority_parse("https://login.microsoftonline.us/a", "b", &authority,
	                                     &error));
	g_assert_nonnull(error);
}

static void test_the_allowlist(void)
{
	EntraAuthority allowed = { 0 };
	EntraAuthority refused = { 0 };

	g_assert_true(entra_authority_parse("login.microsoftonline.com", NULL, &allowed, NULL));
	g_assert_true(entra_authority_allowed(&allowed, NULL));

	g_assert_true(entra_authority_parse("localhost:8443", NULL, &refused, NULL));
	g_assert_false(entra_authority_allowed(&refused, NULL));

	entra_authority_clear(&allowed);
	entra_authority_clear(&refused);
}

static void test_endpoints_are_derived_without_a_network(void)
{
	EntraAuthority authority = { 0 };
	EntraEndpoints endpoints = { 0 };

	g_assert_true(entra_authority_parse("login.microsoftonline.us", "t1", &authority, NULL));
	g_assert_true(entra_endpoints_derive(&authority, &endpoints, NULL));

	g_assert_cmpstr(endpoints.authorization_endpoint, ==,
	                "https://login.microsoftonline.us/t1/oauth2/v2.0/authorize");
	g_assert_cmpstr(endpoints.token_endpoint, ==,
	                "https://login.microsoftonline.us/t1/oauth2/v2.0/token");

	{
		g_autofree char* url = entra_discovery_url(&authority);

		g_assert_cmpstr(url, ==,
		                "https://login.microsoftonline.us/t1/v2.0/.well-known/"
		                "openid-configuration");
	}

	entra_endpoints_clear(&endpoints);
	entra_authority_clear(&authority);
}

static void test_a_discovery_document_refines_the_endpoints(void)
{
	static const char document[] =
	    "{\"issuer\":\"https://login.microsoftonline.us/t1/v2.0\","
	    "\"authorization_endpoint\":\"https://login.microsoftonline.us/t1/oauth2/v2.0/authorize\","
	    "\"token_endpoint\":\"https://login.microsoftonline.us/t1/oauth2/v2.0/token\"}";
	EntraEndpoints endpoints = { 0 };

	g_assert_true(entra_endpoints_parse_document(document, sizeof(document) - 1,
	                                             "login.microsoftonline.us", &endpoints, NULL));
	g_assert_cmpstr(endpoints.token_endpoint, ==,
	                "https://login.microsoftonline.us/t1/oauth2/v2.0/token");
	entra_endpoints_clear(&endpoints);
}

/* A discovery document is not a licence to move the exchange somewhere else. */
static void test_an_endpoint_on_another_host_is_discarded(void)
{
	static const char document[] =
	    "{\"authorization_endpoint\":\"https://login.microsoftonline.us/t1/oauth2/v2.0/authorize\","
	    "\"token_endpoint\":\"https://evil.invalid/token\"}";
	EntraEndpoints endpoints = { 0 };
	g_autoptr(GError) error = NULL;

	g_assert_false(entra_endpoints_parse_document(document, sizeof(document) - 1,
	                                              "login.microsoftonline.us", &endpoints, &error));
	g_assert_nonnull(error);
	g_assert_null(endpoints.token_endpoint);
}

static void test_a_plaintext_endpoint_is_discarded(void)
{
	static const char document[] =
	    "{\"authorization_endpoint\":\"http://login.microsoftonline.us/t1/oauth2/v2.0/authorize\","
	    "\"token_endpoint\":\"http://login.microsoftonline.us/t1/oauth2/v2.0/token\"}";
	EntraEndpoints endpoints = { 0 };

	g_assert_false(entra_endpoints_parse_document(document, sizeof(document) - 1,
	                                              "login.microsoftonline.us", &endpoints, NULL));
	entra_endpoints_clear(&endpoints);
}

static void test_rubbish_documents(void)
{
	EntraEndpoints endpoints = { 0 };

	g_assert_false(entra_endpoints_parse_document("not json", 8, "x", &endpoints, NULL));
	g_assert_false(entra_endpoints_parse_document("[]", 2, "x", &endpoints, NULL));
	g_assert_false(entra_endpoints_parse_document("{}", 2, "x", &endpoints, NULL));
}

static void test_the_authorization_url(void)
{
	EntraEndpoints endpoints = {
		(char*)"https://login.microsoftonline.us/t1/oauth2/v2.0/authorize", NULL
	};
	g_autofree char* url = entra_build_authorize_url(
	    &endpoints, ENTRA_AVD_CLIENT_ID, "https://www.wvd.azure.us/.default openid", "STATE",
	    "CHALLENGE", ENTRA_NATIVECLIENT_REDIRECT, "select_account", NULL);

	g_assert_true(g_str_has_prefix(url, "https://login.microsoftonline.us/t1/oauth2/v2.0/"
	                                    "authorize?"));
	g_assert_nonnull(strstr(url, "response_type=code"));
	g_assert_nonnull(strstr(url, "code_challenge=CHALLENGE"));
	g_assert_nonnull(strstr(url, "code_challenge_method=S256"));
	g_assert_nonnull(strstr(url, "state=STATE"));
	g_assert_nonnull(strstr(url, "prompt=select_account"));
	g_assert_nonnull(strstr(url, "client_id=" ENTRA_AVD_CLIENT_ID));
	/* The redirect is escaped, and it is the commercial one. */
	g_assert_nonnull(strstr(url, "redirect_uri=https%3A%2F%2Flogin.microsoftonline.com%2F"));
	g_assert_null(strstr(url, "code_verifier"));
}

static void test_the_authorization_url_omits_an_absent_prompt(void)
{
	EntraEndpoints endpoints = {
		(char*)"https://login.microsoftonline.us/t1/oauth2/v2.0/authorize", NULL
	};
	g_autofree char* url =
	    entra_build_authorize_url(&endpoints, ENTRA_AVD_CLIENT_ID, "scope", "S", "C",
	                              ENTRA_NATIVECLIENT_REDIRECT, NULL, "a@b.mil");

	g_assert_null(strstr(url, "prompt="));
	g_assert_nonnull(strstr(url, "login_hint=a%40b.mil"));
}

/* The token endpoint's error object decides whether a human has to be asked. */
static void test_token_errors_are_classified(void)
{
	static const struct
	{
		const char* document;
		gboolean interactive;
	} cases[] = {
		{ "{\"error\":\"invalid_grant\",\"error_description\":\"AADSTS50173: user@tenant\"}",
		  TRUE },
		{ "{\"error\":\"interaction_required\"}", TRUE },
		{ "{\"error\":\"consent_required\"}", TRUE },
		{ "{\"error\":\"login_required\"}", TRUE },
		{ "{\"error\":\"invalid_client\",\"suberror\":\"basic_action\"}", FALSE },
		{ "{\"error\":\"unauthorized_client\",\"suberror\":\"consent_required\"}", TRUE },
		{ "{\"error\":\"server_error\"}", FALSE },
	};

	for (gsize i = 0; i < G_N_ELEMENTS(cases); i++)
	{
		EntraTokenSet set = { 0 };
		g_autofree char* oauth_error = NULL;
		g_autoptr(GError) error = NULL;

		g_assert_false(entra_token_set_from_json(cases[i].document, strlen(cases[i].document), &set,
		                                         &oauth_error, &error));
		g_assert_nonnull(error);
		g_assert_nonnull(oauth_error);
		g_assert_cmpint(g_error_matches(error, ENTRA_ERROR, ENTRA_ERROR_INTERACTION_REQUIRED), ==,
		                cases[i].interactive);

		/* The description names the account and the policy. It must not survive. */
		g_assert_null(strstr(error->message, "user@tenant"));
		g_assert_null(strstr(error->message, "AADSTS50173"));
	}
}

static void test_a_pop_response_is_parsed(void)
{
	/* The shape observed on hardware: no refresh token comes back with a PoP one. */
	static const char document[] =
	    "{\"token_type\":\"pop\",\"scope\":\"ms-device-service://termsrv.wvd.microsoft.com/Name/"
	    "host/user_impersonation\",\"expires_in\":3599,\"ext_expires_in\":3599,"
	    "\"access_token\":\"eyJ0eXAi\"}";
	EntraTokenSet set = { 0 };

	g_assert_true(entra_token_set_from_json(document, sizeof(document) - 1, &set, NULL, NULL));
	g_assert_cmpstr(set.token_type, ==, "pop");
	g_assert_cmpstr(set.access_token, ==, "eyJ0eXAi");
	g_assert_null(set.refresh_token);
	g_assert_cmpint(set.expires_in, ==, 3599);
	g_assert_cmpint(set.expires_at, >, 0);
	entra_token_set_clear(&set);
}

static void test_a_response_with_no_token_is_refused(void)
{
	EntraTokenSet set = { 0 };

	g_assert_false(entra_token_set_from_json("{\"scope\":\"x\"}", 13, &set, NULL, NULL));
}

int main(int argc, char** argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/clouds/table", test_the_cloud_table);
	g_test_add_func("/clouds/client-id", test_only_the_avd_client_is_allowlisted);
	g_test_add_func("/authority/host", test_an_authority_host);
	g_test_add_func("/authority/url", test_an_authority_url);
	g_test_add_func("/authority/common", test_an_authority_with_no_tenant_is_common);
	g_test_add_func("/authority/refused", test_refused_authorities);
	g_test_add_func("/authority/allowlist", test_the_allowlist);
	g_test_add_func("/discovery/derived", test_endpoints_are_derived_without_a_network);
	g_test_add_func("/discovery/refined", test_a_discovery_document_refines_the_endpoints);
	g_test_add_func("/discovery/other-host", test_an_endpoint_on_another_host_is_discarded);
	g_test_add_func("/discovery/plaintext", test_a_plaintext_endpoint_is_discarded);
	g_test_add_func("/discovery/rubbish", test_rubbish_documents);
	g_test_add_func("/authorize/url", test_the_authorization_url);
	g_test_add_func("/authorize/no-prompt", test_the_authorization_url_omits_an_absent_prompt);
	g_test_add_func("/token/errors", test_token_errors_are_classified);
	g_test_add_func("/token/pop", test_a_pop_response_is_parsed);
	g_test_add_func("/token/no-token", test_a_response_with_no_token_is_refused);

	return g_test_run();
}
