/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */

#include <string.h>

#include <gio/gio.h>
#include <glib.h>

#include "../entra-error.h"
#include "../log/redact.h"
#include "transaction.h"

/* RFC 7636 puts the verifier between 43 and 128 characters. 64 random bytes is
 * 86 base64url characters: comfortably inside the range and 512 bits of entropy. */
#define ENTRA_VERIFIER_BYTES 64
#define ENTRA_STATE_BYTES 32

struct EntraTransaction
{
	char* state;
	char* verifier;
	char* challenge;
	char* redirect_uri;
	guint timeout_seconds;
	gboolean consumed;
};

/* base64url with the padding removed, which is what both RFC 7636 and Entra want. */
static char* entra_base64url(const guchar* data, gsize length)
{
	g_autofree char* standard = g_base64_encode(data, length);
	char* out = g_strdup(standard);

	for (char* p = out; *p != '\0'; p++)
	{
		if (*p == '+')
			*p = '-';
		else if (*p == '/')
			*p = '_';
	}

	for (gsize i = strlen(out); i > 0 && out[i - 1] == '='; i--)
		out[i - 1] = '\0';

	return out;
}

/* THE KERNEL'S BYTES OR NOTHING. g_random_int() is a Mersenne twister: fine for
 * a jitter, useless for a `state` an attacker must not be able to guess. A
 * client that cannot read /dev/urandom does not quietly use a weaker source, it
 * fails. */
static char* entra_random_base64url(gsize bytes, GError** error)
{
	g_autofree guchar* buffer = g_malloc0(bytes);
	g_autoptr(GFile) urandom = g_file_new_for_path("/dev/urandom");
	g_autoptr(GFileInputStream) stream = NULL;
	char* out = NULL;
	gsize got = 0;

	stream = g_file_read(urandom, NULL, error);
	if (stream == NULL)
	{
		g_prefix_error(error, "no random source: ");
		return NULL;
	}

	if (!g_input_stream_read_all(G_INPUT_STREAM(stream), buffer, bytes, &got, NULL, error) ||
	    got != bytes)
	{
		if (error != NULL && *error == NULL)
			g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_INTERNAL,
			                    "short read from the random source");
		return NULL;
	}

	out = entra_base64url(buffer, bytes);
	memset(buffer, 0, bytes);
	return out;
}

char* entra_pkce_challenge_for_verifier(const char* verifier)
{
	guchar digest[32];
	gsize length = sizeof(digest);
	g_autoptr(GChecksum) sum = g_checksum_new(G_CHECKSUM_SHA256);

	g_checksum_update(sum, (const guchar*)verifier, (gssize)strlen(verifier));
	g_checksum_get_digest(sum, digest, &length);

	return entra_base64url(digest, length);
}

EntraTransaction* entra_transaction_new(const char* redirect_uri, guint timeout_seconds,
                                        GError** error)
{
	EntraTransaction* self = NULL;

	if (redirect_uri == NULL || *redirect_uri == '\0')
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_INTERNAL,
		                    "a transaction needs a redirect URI");
		return NULL;
	}

	self = g_new0(EntraTransaction, 1);
	self->redirect_uri = g_strdup(redirect_uri);
	self->timeout_seconds = timeout_seconds;

	self->state = entra_random_base64url(ENTRA_STATE_BYTES, error);
	if (self->state != NULL)
		self->verifier = entra_random_base64url(ENTRA_VERIFIER_BYTES, error);

	if (self->verifier == NULL)
	{
		entra_transaction_free(self);
		return NULL;
	}

	self->challenge = entra_pkce_challenge_for_verifier(self->verifier);
	return self;
}

const char* entra_transaction_state(EntraTransaction* self)
{
	return self->state;
}

const char* entra_transaction_challenge(EntraTransaction* self)
{
	return self->challenge;
}

const char* entra_transaction_verifier(EntraTransaction* self)
{
	return self->verifier;
}

const char* entra_transaction_redirect_uri(EntraTransaction* self)
{
	return self->redirect_uri;
}

guint entra_transaction_timeout(EntraTransaction* self)
{
	return self->timeout_seconds;
}

gboolean entra_transaction_state_equal(EntraTransaction* self, const char* candidate)
{
	gsize mine, theirs, span;
	guchar difference;

	if (self == NULL || self->state == NULL || candidate == NULL)
		return FALSE;

	mine = strlen(self->state);
	theirs = strlen(candidate);
	span = MAX(mine, theirs);

	/* No early return anywhere: every byte of the longer string is read whatever
	 * happens, so the time this takes does not say where the first difference
	 * was. A length mismatch is folded in as a difference rather than checked. */
	difference = (guchar)((mine ^ theirs) != 0);

	for (gsize i = 0; i < span; i++)
	{
		guchar a = i < mine ? (guchar)self->state[i] : 0;
		guchar b = i < theirs ? (guchar)candidate[i] : 1;

		difference |= (guchar)(a ^ b);
	}

	return difference == 0;
}

gboolean entra_transaction_consume(EntraTransaction* self)
{
	if (self == NULL || self->consumed)
		return FALSE;

	self->consumed = TRUE;
	return TRUE;
}

void entra_transaction_free(EntraTransaction* self)
{
	if (self == NULL)
		return;

	entra_scrub(self->state);
	entra_scrub(self->verifier);
	g_free(self->challenge);
	g_free(self->redirect_uri);
	g_free(self);
}
