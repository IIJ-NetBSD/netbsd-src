/*	$NetBSD: argon2.c,v 1.3 2025/09/05 21:16:32 christos Exp $	*/

/* argon2.c - Password module for argon2 */
/* $OpenLDAP$ */
/* This work is part of OpenLDAP Software <http://www.openldap.org/>.
 *
 * Copyright 2017-2024 The OpenLDAP Foundation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted only as authorized by the OpenLDAP
 * Public License.
 *
 * A copy of this license is available in the file LICENSE in the
 * top-level directory of the distribution or, alternatively, at
 * <http://www.OpenLDAP.org/license.html>.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: argon2.c,v 1.3 2025/09/05 21:16:32 christos Exp $");

#include "portable.h"
#ifdef SLAPD_PWMOD_PW_ARGON2
#include "ac/string.h"
#include "lber_pvt.h"
#include "lutil.h"

#include "slap.h"

#include <stdint.h>
#include <stdlib.h>

#ifdef HAVE_LIBARGON2
#include <argon2.h>

/*
 * Use OWASP recommended values (retrieved on 2023-08-07)
 * @see https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html
 */
#define SLAPD_ARGON2_ITERATIONS 5
#define SLAPD_ARGON2_MEMORY 7168
#define SLAPD_ARGON2_PARALLELISM 1
#define SLAPD_ARGON2_SALT_LENGTH 16
#define SLAPD_ARGON2_HASH_LENGTH 32

#else /* !HAVE_LIBARGON2 */
#include <sodium.h>

/*
 * Or libsodium interactive settings
 */
#define SLAPD_ARGON2_ITERATIONS crypto_pwhash_argon2id_OPSLIMIT_INTERACTIVE
#define SLAPD_ARGON2_MEMORY (crypto_pwhash_argon2id_MEMLIMIT_INTERACTIVE / 1024)
#define SLAPD_ARGON2_PARALLELISM 1
#define SLAPD_ARGON2_SALT_LENGTH crypto_pwhash_argon2id_SALTBYTES
#define SLAPD_ARGON2_HASH_LENGTH 32

#endif

static unsigned long iterations = SLAPD_ARGON2_ITERATIONS;
static unsigned long memory = SLAPD_ARGON2_MEMORY;
static unsigned long parallelism = SLAPD_ARGON2_PARALLELISM;

const struct berval slapd_argon2_scheme = BER_BVC("{ARGON2}");

static int
slapd_argon2_hash(
		const struct berval *scheme,
		const struct berval *passwd,
		struct berval *hash,
		const char **text )
{

	/*
	 * Duplicate these values here so future code which allows
	 * configuration has an easier time.
	 */
	uint32_t salt_length, hash_length;
	char *p;
	int rc = LUTIL_PASSWD_ERR;

#ifdef HAVE_LIBARGON2
	struct berval salt;
	size_t encoded_length;

	salt_length = SLAPD_ARGON2_SALT_LENGTH;
	hash_length = SLAPD_ARGON2_HASH_LENGTH;

	encoded_length = argon2_encodedlen( iterations, memory, parallelism,
			salt_length, hash_length, Argon2_id );

	salt.bv_len = salt_length;
	salt.bv_val = ber_memalloc( salt.bv_len );

	if ( salt.bv_val == NULL ) {
		return LUTIL_PASSWD_ERR;
	}

	if ( lutil_entropy( (unsigned char*)salt.bv_val, salt.bv_len ) ) {
		ber_memfree( salt.bv_val );
		return LUTIL_PASSWD_ERR;
	}

	p = hash->bv_val = ber_memalloc( scheme->bv_len + encoded_length );
	if ( p == NULL ) {
		ber_memfree( salt.bv_val );
		return LUTIL_PASSWD_ERR;
	}

	AC_MEMCPY( p, scheme->bv_val, scheme->bv_len );
	p += scheme->bv_len;

	/*
	 * Do the actual heavy lifting
	 */
	if ( argon2id_hash_encoded( iterations, memory, parallelism,
				passwd->bv_val, passwd->bv_len,
				salt.bv_val, salt_length, hash_length,
				p, encoded_length ) == 0 ) {
		rc = LUTIL_PASSWD_OK;
	}
	hash->bv_len = scheme->bv_len + encoded_length;
	ber_memfree( salt.bv_val );

#else /* !HAVE_LIBARGON2 */
	/* Not exposed by libsodium
	salt_length = SLAPD_ARGON2_SALT_LENGTH;
	hash_length = SLAPD_ARGON2_HASH_LENGTH;
	*/

	p = hash->bv_val = ber_memalloc( scheme->bv_len + crypto_pwhash_STRBYTES );
	if ( p == NULL ) {
		return LUTIL_PASSWD_ERR;
	}

	AC_MEMCPY( hash->bv_val, scheme->bv_val, scheme->bv_len );
	p += scheme->bv_len;

	if ( crypto_pwhash_str_alg( p, passwd->bv_val, passwd->bv_len,
				iterations, memory * 1024,
				crypto_pwhash_ALG_ARGON2ID13 ) == 0 ) {
		hash->bv_len = strlen( hash->bv_val );
		rc = LUTIL_PASSWD_OK;
	}
#endif

	if ( rc ) {
		ber_memfree( hash->bv_val );
		return LUTIL_PASSWD_ERR;
	}

	return LUTIL_PASSWD_OK;
}

static int
slapd_argon2_verify(
		const struct berval *scheme,
		const struct berval *passwd,
		const struct berval *cred,
		const char **text )
{
	int rc = LUTIL_PASSWD_ERR;

#ifdef HAVE_LIBARGON2
	if ( strncmp( passwd->bv_val, "$argon2i$", STRLENOF("$argon2i$") ) == 0 ) {
		rc = argon2i_verify( passwd->bv_val, cred->bv_val, cred->bv_len );
	} else if ( strncmp( passwd->bv_val, "$argon2d$", STRLENOF("$argon2d$") ) == 0 ) {
		rc = argon2d_verify( passwd->bv_val, cred->bv_val, cred->bv_len );
	} else if ( strncmp( passwd->bv_val, "$argon2id$", STRLENOF("$argon2id$") ) == 0 ) {
		rc = argon2id_verify( passwd->bv_val, cred->bv_val, cred->bv_len );
	}
#else /* !HAVE_LIBARGON2 */
	rc = crypto_pwhash_str_verify( passwd->bv_val, cred->bv_val, cred->bv_len );
#endif

	if ( rc ) {
		return LUTIL_PASSWD_ERR;
	}
	return LUTIL_PASSWD_OK;
}

int init_module( int argc, char *argv[] )
{
	int i;

#ifdef HAVE_LIBSODIUM
	if ( sodium_init() == -1 ) {
		return -1;
	}
#endif

	for ( i=0; i < argc; i++ ) {
		char *p;
		unsigned long value;

		switch ( *argv[i] ) {
			case 'm':
				p = strchr( argv[i], '=' );
				if ( !p || lutil_atoulx( &value, p+1, 0 ) ) {
					return -1;
				}
				memory = value;
				break;

			case 't':
				p = strchr( argv[i], '=' );
				if ( !p || lutil_atoulx( &value, p+1, 0 ) ) {
					return -1;
				}
				iterations = value;
				break;

			case 'p':
				p = strchr( argv[i], '=' );
				if ( !p || lutil_atoulx( &value, p+1, 0 ) ) {
					return -1;
				}
				parallelism = value;
				break;

			default:
				return -1;
		}
	}

#ifndef HAVE_LIBARGON2
	/* At the moment, we can only use libargon2 to set parallelism for new
	 * hashes */
	if ( parallelism != SLAPD_ARGON2_PARALLELISM ) {
		Debug( LDAP_DEBUG_ANY, "pw-argon2: "
				"non-default parallelism only supported when linked with "
				"libargon2, got p=%lu\n",
				parallelism );

		if ( (slapMode & SLAP_MODE) != SLAP_TOOL_MODE ||
				slapTool == SLAPPASSWD || slapTool == SLAPTEST ) {
			return 1;
		}
	}
#endif

	return lutil_passwd_add( (struct berval *)&slapd_argon2_scheme,
			slapd_argon2_verify, slapd_argon2_hash );
}
#endif /* SLAPD_OVER_PW_ARGON2 */
