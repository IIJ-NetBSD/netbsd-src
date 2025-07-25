/*	$NetBSD: dst.h,v 1.13 2025/07/17 19:01:46 christos Exp $	*/

/*
 * Copyright (C) Internet Systems Consortium, Inc. ("ISC")
 *
 * SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * See the COPYRIGHT file distributed with this work for additional
 * information regarding copyright ownership.
 */

#pragma once

/*! \file dst/dst.h */

#include <inttypes.h>
#include <stdbool.h>

#include <isc/lang.h>
#include <isc/stdtime.h>

#include <dns/ds.h>
#include <dns/dsdigest.h>
#include <dns/log.h>
#include <dns/name.h>
#include <dns/secalg.h>
#include <dns/types.h>

#include <dst/gssapi.h>

ISC_LANG_BEGINDECLS

/***
 *** Types
 ***/

/*%
 * The dst_key structure is opaque.  Applications should use the accessor
 * functions provided to retrieve key attributes.  If an application needs
 * to set attributes, new accessor functions will be written.
 */

typedef struct dst_key	   dst_key_t;
typedef struct dst_context dst_context_t;

/*%
 * Key states for the DNSSEC records related to a key: DNSKEY, RRSIG (ksk),
 * RRSIG (zsk), and DS.
 *
 * DST_KEY_STATE_HIDDEN:      Records of this type are not published in zone.
 *                            This may be because the key parts were never
 *                            introduced in the zone, or because the key has
 *                            retired and has no records of this type left in
 *                            the zone.
 * DST_KEY_STATE_RUMOURED:    Records of this type are published in zone, but
 *                            not long enough to ensure all resolvers know
 *                            about it.
 * DST_KEY_STATE_OMNIPRESENT: Records of this type are published in zone long
 *                            enough so that all resolvers that know about
 *                            these records, no longer have outdated data.
 * DST_KEY_STATE_UNRETENTIVE: Records of this type have been removed from the
 *                            zone, but there may be resolvers that still have
 *                            have predecessor records cached.  Note that RRSIG
 *                            records in this state may actually still be in the
 *                            zone because they are reused, but retired RRSIG
 *                            records will never be refreshed: A successor key
 *                            is used to create signatures.
 * DST_KEY_STATE_NA:          The state is not applicable for this record type.
 */
typedef enum dst_key_state {
	DST_KEY_STATE_HIDDEN = 0,
	DST_KEY_STATE_RUMOURED = 1,
	DST_KEY_STATE_OMNIPRESENT = 2,
	DST_KEY_STATE_UNRETENTIVE = 3,
	DST_KEY_STATE_NA = 4
} dst_key_state_t;

/* DST algorithm codes */
typedef enum dst_algorithm {
	DST_ALG_UNKNOWN = 0,
	DST_ALG_RSA = 1, /* Used for parsing RSASHA1, RSASHA256 and RSASHA512 */
	DST_ALG_RSAMD5 = 1,
	DST_ALG_DH = 2, /* Deprecated */
	DST_ALG_DSA = 3,
	DST_ALG_ECC = 4,
	DST_ALG_RSASHA1 = 5,
	DST_ALG_NSEC3DSA = 6,
	DST_ALG_NSEC3RSASHA1 = 7,
	DST_ALG_RSASHA256 = 8,
	DST_ALG_RSASHA512 = 10,
	DST_ALG_ECCGOST = 12,
	DST_ALG_ECDSA256 = 13,
	DST_ALG_ECDSA384 = 14,
	DST_ALG_ED25519 = 15,
	DST_ALG_ED448 = 16,

	/*
	 * Do not renumber HMAC algorithms as they are used externally to named
	 * in legacy K* key pair files.
	 * Do not add non HMAC between DST_ALG_HMACMD5 and DST_ALG_HMACSHA512.
	 */
	DST_ALG_HMACMD5 = 157,
	DST_ALG_HMAC_FIRST = DST_ALG_HMACMD5,
	DST_ALG_GSSAPI = 160,	  /* Internal use only. Exception. */
	DST_ALG_HMACSHA1 = 161,	  /* XXXMPA */
	DST_ALG_HMACSHA224 = 162, /* XXXMPA */
	DST_ALG_HMACSHA256 = 163, /* XXXMPA */
	DST_ALG_HMACSHA384 = 164, /* XXXMPA */
	DST_ALG_HMACSHA512 = 165, /* XXXMPA */
	DST_ALG_HMAC_LAST = DST_ALG_HMACSHA512,

	DST_ALG_INDIRECT = 252,
	DST_ALG_PRIVATE = 254,
	DST_MAX_ALGS = 256,
} dst_algorithm_t;

/*% A buffer of this size is large enough to hold any key */
#define DST_KEY_MAXSIZE 1280

/*%
 * A buffer of this size is large enough to hold the textual representation
 * of any key
 */
#define DST_KEY_MAXTEXTSIZE 2048

/*% 'Type' for dst_read_key() */
#define DST_TYPE_KEY	  0x1000000 /* KEY key */
#define DST_TYPE_PRIVATE  0x2000000
#define DST_TYPE_PUBLIC	  0x4000000
#define DST_TYPE_STATE	  0x8000000
#define DST_TYPE_TEMPLATE 0x10000000

/* Key timing metadata definitions */
#define DST_TIME_CREATED     0
#define DST_TIME_PUBLISH     1
#define DST_TIME_ACTIVATE    2
#define DST_TIME_REVOKE	     3
#define DST_TIME_INACTIVE    4
#define DST_TIME_DELETE	     5
#define DST_TIME_DSPUBLISH   6
#define DST_TIME_SYNCPUBLISH 7
#define DST_TIME_SYNCDELETE  8
#define DST_TIME_DNSKEY	     9
#define DST_TIME_ZRRSIG	     10
#define DST_TIME_KRRSIG	     11
#define DST_TIME_DS	     12
#define DST_TIME_DSDELETE    13
#define DST_MAX_TIMES	     13

/* Numeric metadata definitions */
#define DST_NUM_PREDECESSOR 0
#define DST_NUM_SUCCESSOR   1
#define DST_NUM_MAXTTL	    2
#define DST_NUM_ROLLPERIOD  3
#define DST_NUM_LIFETIME    4
#define DST_NUM_DSPUBCOUNT  5
#define DST_NUM_DSDELCOUNT  6
#define DST_MAX_NUMERIC	    6

/* Boolean metadata definitions */
#define DST_BOOL_KSK	0
#define DST_BOOL_ZSK	1
#define DST_MAX_BOOLEAN 1

/* Key state metadata definitions */
#define DST_KEY_DNSKEY	  0
#define DST_KEY_ZRRSIG	  1
#define DST_KEY_KRRSIG	  2
#define DST_KEY_DS	  3
#define DST_KEY_GOAL	  4
#define DST_MAX_KEYSTATES 4

/*
 * Current format version number of the private key parser.
 *
 * When parsing a key file with the same major number but a higher minor
 * number, the key parser will ignore any fields it does not recognize.
 * Thus, DST_MINOR_VERSION should be incremented whenever new
 * fields are added to the private key file (such as new metadata).
 *
 * When rewriting these keys, those fields will be dropped, and the
 * format version set back to the current one..
 *
 * When a key is seen with a higher major number, the key parser will
 * reject it as invalid.  Thus, DST_MAJOR_VERSION should be incremented
 * and DST_MINOR_VERSION set to zero whenever there is a format change
 * which is not backward compatible to previous versions of the dst_key
 * parser, such as change in the syntax of an existing field, the removal
 * of a currently mandatory field, or a new field added which would
 * alter the functioning of the key if it were absent.
 */
#define DST_MAJOR_VERSION 1
#define DST_MINOR_VERSION 3

/***
 *** Functions
 ***/
isc_result_t
dst_lib_init(isc_mem_t *mctx, const char *engine);
/*%<
 * Initializes the DST subsystem.
 *
 * Requires:
 * \li 	"mctx" is a valid memory context
 *
 * Returns:
 * \li	ISC_R_SUCCESS
 * \li	ISC_R_NOMEMORY
 * \li	DST_R_NOENGINE
 *
 * Ensures:
 * \li	DST is properly initialized.
 */

void
dst_lib_destroy(void);
/*%<
 * Releases all resources allocated by DST.
 */

bool
dst_algorithm_supported(unsigned int alg);
/*%<
 * Checks that a given algorithm is supported by DST.
 *
 * Returns:
 * \li	true
 * \li	false
 */

bool
dst_ds_digest_supported(unsigned int digest_type);
/*%<
 * Checks that a given digest algorithm is supported by DST.
 *
 * Returns:
 * \li	true
 * \li	false
 */

isc_result_t
dst_context_create(dst_key_t *key, isc_mem_t *mctx, isc_logcategory_t *category,
		   bool useforsigning, int maxbits, dst_context_t **dctxp);
/*%<
 * Creates a context to be used for a sign or verify operation.
 *
 * Requires:
 * \li	"key" is a valid key.
 * \li	"mctx" is a valid memory context.
 * \li	dctxp != NULL && *dctxp == NULL
 *
 * Returns:
 * \li	ISC_R_SUCCESS
 * \li	ISC_R_NOMEMORY
 *
 * Ensures:
 * \li	*dctxp will contain a usable context.
 */

void
dst_context_destroy(dst_context_t **dctxp);
/*%<
 * Destroys all memory associated with a context.
 *
 * Requires:
 * \li	*dctxp != NULL && *dctxp == NULL
 *
 * Ensures:
 * \li	*dctxp == NULL
 */

isc_result_t
dst_context_adddata(dst_context_t *dctx, const isc_region_t *data);
/*%<
 * Incrementally adds data to the context to be used in a sign or verify
 * operation.
 *
 * Requires:
 * \li	"dctx" is a valid context
 * \li	"data" is a valid region
 *
 * Returns:
 * \li	ISC_R_SUCCESS
 * \li	DST_R_SIGNFAILURE
 * \li	all other errors indicate failure
 */

isc_result_t
dst_context_sign(dst_context_t *dctx, isc_buffer_t *sig);
/*%<
 * Computes a signature using the data and key stored in the context.
 *
 * Requires:
 * \li	"dctx" is a valid context.
 * \li	"sig" is a valid buffer.
 *
 * Returns:
 * \li	ISC_R_SUCCESS
 * \li	DST_R_VERIFYFAILURE
 * \li	all other errors indicate failure
 *
 * Ensures:
 * \li	"sig" will contain the signature
 */

isc_result_t
dst_context_verify(dst_context_t *dctx, isc_region_t *sig);

isc_result_t
dst_context_verify2(dst_context_t *dctx, unsigned int maxbits,
		    isc_region_t *sig);
/*%<
 * Verifies the signature using the data and key stored in the context.
 *
 * 'maxbits' specifies the maximum number of bits permitted in the RSA
 * exponent.
 *
 * Requires:
 * \li	"dctx" is a valid context.
 * \li	"sig" is a valid region.
 *
 * Returns:
 * \li	ISC_R_SUCCESS
 * \li	all other errors indicate failure
 *
 * Ensures:
 * \li	"sig" will contain the signature
 */

isc_result_t
dst_key_computesecret(const dst_key_t *pub, const dst_key_t *priv,
		      isc_buffer_t *secret);
/*%<
 * Computes a shared secret from two (Diffie-Hellman) keys.
 *
 * Requires:
 * \li	"pub" is a valid key that can be used to derive a shared secret
 * \li	"priv" is a valid private key that can be used to derive a shared secret
 * \li	"secret" is a valid buffer
 *
 * Returns:
 * \li	ISC_R_SUCCESS
 * \li	any other result indicates failure
 *
 * Ensures:
 * \li	If successful, secret will contain the derived shared secret.
 */

isc_result_t
dst_key_getfilename(dns_name_t *name, dns_keytag_t id, unsigned int alg,
		    int type, const char *directory, isc_mem_t *mctx,
		    isc_buffer_t *buf);
/*%<
 * Generates a key filename for the name, algorithm, and
 * id, and places it in the buffer 'buf'. If directory is NULL, the
 * current directory is assumed.
 *
 * Requires:
 * \li	"name" is a valid absolute dns name.
 * \li	"id" is a valid key tag identifier.
 * \li	"alg" is a supported key algorithm.
 * \li	"type" is DST_TYPE_PUBLIC, DST_TYPE_PRIVATE, or the bitwise union.
 *		  DST_TYPE_KEY look for a KEY record otherwise DNSKEY
 * \li	"mctx" is a valid memory context.
 * \li	"buf" is not NULL.
 *
 * Returns:
 * \li	ISC_R_SUCCESS
 * \li	any other result indicates failure
 */

isc_result_t
dst_key_fromfile(dns_name_t *name, dns_keytag_t id, unsigned int alg, int type,
		 const char *directory, isc_mem_t *mctx, dst_key_t **keyp);
/*%<
 * Reads a key from permanent storage.  The key can either be a public or
 * private key, or a key state. It specified by name, algorithm, and id.  If
 * a private key or key state is specified, the public key must also be
 * present.  If directory is NULL, the current directory is assumed.
 *
 * Requires:
 * \li	"name" is a valid absolute dns name.
 * \li	"id" is a valid key tag identifier.
 * \li	"alg" is a supported key algorithm.
 * \li	"type" is DST_TYPE_PUBLIC, DST_TYPE_PRIVATE or the bitwise union.
 *		  DST_TYPE_KEY look for a KEY record otherwise DNSKEY.
 *		  DST_TYPE_STATE to also read the key state.
 * \li	"mctx" is a valid memory context.
 * \li	"keyp" is not NULL and "*keyp" is NULL.
 *
 * Returns:
 * \li	ISC_R_SUCCESS
 * \li	any other result indicates failure
 *
 * Ensures:
 * \li	If successful, *keyp will contain a valid key.
 */

isc_result_t
dst_key_fromnamedfile(const char *filename, const char *dirname, int type,
		      isc_mem_t *mctx, dst_key_t **keyp);
/*%<
 * Reads a key from permanent storage.  The key can either be a public or
 * private key, or a key state. It is specified by filename.  If a private key
 * or key state is specified, the public key must also be present.
 *
 * If 'dirname' is not NULL, and 'filename' is a relative path,
 * then the file is looked up relative to the given directory.
 * If 'filename' is an absolute path, 'dirname' is ignored.
 *
 * Requires:
 * \li	"filename" is not NULL
 * \li	"type" is DST_TYPE_PUBLIC, DST_TYPE_PRIVATE, or the bitwise union.
 *		  DST_TYPE_KEY look for a KEY record otherwise DNSKEY.
 *		  DST_TYPE_STATE to also read the key state.
 * \li	"mctx" is a valid memory context
 * \li	"keyp" is not NULL and "*keyp" is NULL.
 *
 * Returns:
 * \li	ISC_R_SUCCESS
 * \li	any other result indicates failure
 *
 * Ensures:
 * \li	If successful, *keyp will contain a valid key.
 */

isc_result_t
dst_key_read_public(const char *filename, int type, isc_mem_t *mctx,
		    dst_key_t **keyp);
/*%<
 * Reads a public key from permanent storage.  The key must be a public key.
 *
 * Requires:
 * \li	"filename" is not NULL.
 * \li	"type" is DST_TYPE_KEY look for a KEY record otherwise DNSKEY.
 * \li	"mctx" is a valid memory context.
 * \li	"keyp" is not NULL and "*keyp" is NULL.
 *
 * Returns:
 * \li	ISC_R_SUCCESS
 * \li	DST_R_BADKEYTYPE if the key type is not the expected one
 * \li	ISC_R_UNEXPECTEDTOKEN if the file can not be parsed as a public key
 * \li	any other result indicates failure
 *
 * Ensures:
 * \li	If successful, *keyp will contain a valid key.
 */

isc_result_t
dst_key_read_state(const char *filename, isc_mem_t *mctx, dst_key_t **keyp);
/*%<
 * Reads a key state from permanent storage.
 *
 * Requires:
 * \li	"filename" is not NULL.
 * \li	"mctx" is a valid memory context.
 * \li	"keyp" is not NULL and "*keyp" is NULL.
 *
 * Returns:
 * \li	ISC_R_SUCCESS
 * \li	ISC_R_UNEXPECTEDTOKEN if the file can not be parsed as a public key
 * \li	any other result indicates failure
 */

isc_result_t
dst_key_tofile(const dst_key_t *key, int type, const char *directory);
/*%<
 * Writes a key to permanent storage.  The key can either be a public or
 * private key.  Public keys are written in DNS format and private keys
 * are written as a set of base64 encoded values.  If directory is NULL,
 * the current directory is assumed.
 *
 * Requires:
 * \li	"key" is a valid key.
 * \li	"type" is DST_TYPE_PUBLIC, DST_TYPE_PRIVATE, or the bitwise union
 *
 * Returns:
 * \li	ISC_R_SUCCESS
 * \li	any other result indicates failure
 */

isc_result_t
dst_key_fromdns_ex(const dns_name_t *name, dns_rdataclass_t rdclass,
		   isc_buffer_t *source, isc_mem_t *mctx, bool no_rdata,
		   dst_key_t **keyp);
isc_result_t
dst_key_fromdns(const dns_name_t *name, dns_rdataclass_t rdclass,
		isc_buffer_t *source, isc_mem_t *mctx, dst_key_t **keyp);
/*%<
 * Converts a DNS KEY record into a DST key.
 *
 * Requires:
 * \li	"name" is a valid absolute dns name.
 * \li	"source" is a valid buffer.  There must be at least 4 bytes available.
 * \li	"mctx" is a valid memory context.
 * \li	"keyp" is not NULL and "*keyp" is NULL.
 *
 * Returns:
 * \li	ISC_R_SUCCESS
 * \li	any other result indicates failure
 *
 * Ensures:
 * \li	If successful, *keyp will contain a valid key, and the consumed
 *	pointer in data will be advanced.
 */

isc_result_t
dst_key_todns(const dst_key_t *key, isc_buffer_t *target);
/*%<
 * Converts a DST key into a DNS KEY record.
 *
 * Requires:
 * \li	"key" is a valid key.
 * \li	"target" is a valid buffer.  There must be at least 4 bytes unused.
 *
 * Returns:
 * \li	ISC_R_SUCCESS
 * \li	any other result indicates failure
 *
 * Ensures:
 * \li	If successful, the used pointer in 'target' is advanced by at least 4.
 */

isc_result_t
dst_key_frombuffer(const dns_name_t *name, unsigned int alg, unsigned int flags,
		   unsigned int protocol, dns_rdataclass_t rdclass,
		   isc_buffer_t *source, isc_mem_t *mctx, dst_key_t **keyp);
/*%<
 * Converts a buffer containing DNS KEY RDATA into a DST key.
 *
 * Requires:
 *\li	"name" is a valid absolute dns name.
 *\li	"alg" is a supported key algorithm.
 *\li	"source" is a valid buffer.
 *\li	"mctx" is a valid memory context.
 *\li	"keyp" is not NULL and "*keyp" is NULL.
 *
 * Returns:
 *\li 	ISC_R_SUCCESS
 * \li	any other result indicates failure
 *
 * Ensures:
 *\li	If successful, *keyp will contain a valid key, and the consumed
 *	pointer in source will be advanced.
 */

isc_result_t
dst_key_tobuffer(const dst_key_t *key, isc_buffer_t *target);
/*%<
 * Converts a DST key into DNS KEY RDATA format.
 *
 * Requires:
 *\li	"key" is a valid key.
 *\li	"target" is a valid buffer.
 *
 * Returns:
 *\li 	ISC_R_SUCCESS
 * \li	any other result indicates failure
 *
 * Ensures:
 *\li	If successful, the used pointer in 'target' is advanced.
 */

isc_result_t
dst_key_privatefrombuffer(dst_key_t *key, isc_buffer_t *buffer);
/*%<
 * Converts a public key into a private key, reading the private key
 * information from the buffer.  The buffer should contain the same data
 * as the .private key file would.
 *
 * Requires:
 *\li	"key" is a valid public key.
 *\li	"buffer" is not NULL.
 *
 * Returns:
 *\li 	ISC_R_SUCCESS
 * \li	any other result indicates failure
 *
 * Ensures:
 *\li	If successful, key will contain a valid private key.
 */

dns_gss_ctx_id_t
dst_key_getgssctx(const dst_key_t *key);
/*%<
 * Returns the opaque key data.
 * Be cautions when using this value unless you know what you are doing.
 *
 * Requires:
 *\li	"key" is not NULL.
 *
 * Returns:
 *\li	gssctx key data, possibly NULL.
 */

isc_result_t
dst_key_fromgssapi(const dns_name_t *name, dns_gss_ctx_id_t gssctx,
		   isc_mem_t *mctx, dst_key_t **keyp, isc_region_t *intoken);
/*%<
 * Converts a GSSAPI opaque context id into a DST key.
 *
 * Requires:
 *\li	"name" is a valid absolute dns name.
 *\li	"gssctx" is a GSSAPI context id.
 *\li	"mctx" is a valid memory context.
 *\li	"keyp" is not NULL and "*keyp" is NULL.
 *
 * Returns:
 *\li 	ISC_R_SUCCESS
 * \li	any other result indicates failure
 *
 * Ensures:
 *\li	If successful, *keyp will contain a valid key and be responsible for
 *	the context id.
 */

#ifdef DST_KEY_INTERNAL
isc_result_t
dst_key_buildinternal(const dns_name_t *name, unsigned int alg,
		      unsigned int bits, unsigned int flags,
		      unsigned int protocol, dns_rdataclass_t rdclass,
		      void *data, isc_mem_t *mctx, dst_key_t **keyp);
#endif /* ifdef DST_KEY_INTERNAL */

isc_result_t
dst_key_fromlabel(const dns_name_t *name, int alg, unsigned int flags,
		  unsigned int protocol, dns_rdataclass_t rdclass,
		  const char *engine, const char *label, const char *pin,
		  isc_mem_t *mctx, dst_key_t **keyp);

isc_result_t
dst_key_generate(const dns_name_t *name, unsigned int alg, unsigned int bits,
		 unsigned int param, unsigned int flags, unsigned int protocol,
		 dns_rdataclass_t rdclass, const char *label, isc_mem_t *mctx,
		 dst_key_t **keyp, void (*callback)(int));

/*%<
 * Generate a DST key (or keypair) with the supplied parameters.  The
 * interpretation of the "param" field depends on the algorithm:
 * \code
 * 	RSA:	exponent
 * 		0	use exponent 3
 * 		!0	use Fermat4 (2^16 + 1)
 * 	DSA:	unused
 * 	HMACMD5: entropy
 *		0	default - require good entropy
 *		!0	lack of good entropy is ok
 *\endcode
 *
 * Requires:
 *\li	"name" is a valid absolute dns name.
 *\li	"keyp" is not NULL and "*keyp" is NULL.
 *
 * Returns:
 *\li 	ISC_R_SUCCESS
 * \li	any other result indicates failure
 *
 * Ensures:
 *\li	If successful, *keyp will contain a valid key.
 */

bool
dst_key_compare(const dst_key_t *key1, const dst_key_t *key2);
/*%<
 * Compares two DST keys.  Returns true if they match, false otherwise.
 *
 * Keys ARE NOT considered to match if one of them is the revoked version
 * of the other.
 *
 * Requires:
 *\li	"key1" is a valid key.
 *\li	"key2" is a valid key.
 *
 * Returns:
 *\li 	true
 * \li	false
 */

bool
dst_key_pubcompare(const dst_key_t *key1, const dst_key_t *key2,
		   bool match_revoked_key);
/*%<
 * Compares only the public portions of two DST keys.  Returns true
 * if they match, false otherwise.  This allows us, for example, to
 * determine whether a public key found in a zone matches up with a
 * key pair found on disk.
 *
 * If match_revoked_key is TRUE, then keys ARE considered to match if one
 * of them is the revoked version of the other. Otherwise, they are not.
 *
 * Requires:
 *\li	"key1" is a valid key.
 *\li	"key2" is a valid key.
 *
 * Returns:
 *\li 	true
 * \li	false
 */

bool
dst_key_paramcompare(const dst_key_t *key1, const dst_key_t *key2);
/*%<
 * Compares the parameters of two DST keys.  This is used to determine if
 * two (Diffie-Hellman) keys can be used to derive a shared secret.
 *
 * Requires:
 *\li	"key1" is a valid key.
 *\li	"key2" is a valid key.
 *
 * Returns:
 *\li 	true
 * \li	false
 */

void
dst_key_attach(dst_key_t *source, dst_key_t **target);
/*
 * Attach to a existing key increasing the reference count.
 *
 * Requires:
 *\li 'source' to be a valid key.
 *\li 'target' to be non-NULL and '*target' to be NULL.
 */

void
dst_key_free(dst_key_t **keyp);
/*%<
 * Decrement the key's reference counter and, when it reaches zero,
 * release all memory associated with the key.
 *
 * Requires:
 *\li	"keyp" is not NULL and "*keyp" is a valid key.
 *\li	reference counter greater than zero.
 *
 * Ensures:
 *\li	All memory associated with "*keyp" will be freed.
 *\li	*keyp == NULL
 */

/*%<
 * Accessor functions to obtain key fields.
 *
 * Require:
 *\li	"key" is a valid key.
 */
dns_name_t *
dst_key_name(const dst_key_t *key);

unsigned int
dst_key_size(const dst_key_t *key);

unsigned int
dst_key_proto(const dst_key_t *key);

unsigned int
dst_key_alg(const dst_key_t *key);

uint32_t
dst_key_flags(const dst_key_t *key);

dns_keytag_t
dst_key_id(const dst_key_t *key);

dns_keytag_t
dst_key_rid(const dst_key_t *key);

dns_rdataclass_t
dst_key_class(const dst_key_t *key);

const char *
dst_key_directory(const dst_key_t *key);

bool
dst_key_isprivate(const dst_key_t *key);

bool
dst_key_iszonekey(const dst_key_t *key);

bool
dst_key_isnullkey(const dst_key_t *key);

bool
dst_key_have_ksk_and_zsk(dst_key_t **keys, unsigned int nkeys, unsigned int i,
			 bool check_offline, bool ksk, bool zsk, bool *have_ksk,
			 bool *have_zsk);
/*%<
 *
 * Check the list of 'keys' to see if both a KSK and ZSK are present, given key
 * 'i'. The values stored in 'ksk' and 'zsk' tell whether key 'i' is a KSK, ZSK,
 * or both (CSK). If 'check_offline' is true, don't consider KSKs that are
 * currently offline (e.g. their private key file is not available).
 *
 * Requires:
 *\li	"keys" is not NULL.
 *
 * Returns:
 *\li	true if there is one or more keys such that both the KSK and ZSK roles
 *are covered, false otherwise.
 */

isc_result_t
dst_key_buildfilename(const dst_key_t *key, int type, const char *directory,
		      isc_buffer_t *out);
/*%<
 * Generates the filename used by dst to store the specified key.
 * If directory is NULL, the current directory is assumed.
 * If tmp is not NULL, generates a template for mkstemp().
 *
 * Requires:
 *\li	"key" is a valid key
 *\li	"type" is either DST_TYPE_PUBLIC, DST_TYPE_PRIVATE, or 0 for no suffix.
 *\li	"out" is a valid buffer
 *\li	"tmp" is a valid buffer or NULL
 *
 * Ensures:
 *\li	the file name will be written to "out", and the used pointer will
 *		be advanced.
 */

isc_result_t
dst_key_sigsize(const dst_key_t *key, unsigned int *n);
/*%<
 * Computes the size of a signature generated by the given key.
 *
 * Requires:
 *\li	"key" is a valid key.
 *\li	"n" is not NULL
 *
 * Returns:
 *\li	#ISC_R_SUCCESS
 *\li	DST_R_UNSUPPORTEDALG
 *
 * Ensures:
 *\li	"n" stores the size of a generated signature
 */

uint16_t
dst_region_computeid(const isc_region_t *source);
uint16_t
dst_region_computerid(const isc_region_t *source);
/*%<
 * Computes the (revoked) key id of the key stored in the provided
 * region.
 *
 * Requires:
 *\li	"source" contains a valid, non-NULL region.
 *
 * Returns:
 *\li 	the key id
 */

uint16_t
dst_key_getbits(const dst_key_t *key);
/*%<
 * Get the number of digest bits required (0 == MAX).
 *
 * Requires:
 *	"key" is a valid key.
 */

void
dst_key_setbits(dst_key_t *key, uint16_t bits);
/*%<
 * Set the number of digest bits required (0 == MAX).
 *
 * Requires:
 *	"key" is a valid key.
 */

void
dst_key_setttl(dst_key_t *key, dns_ttl_t ttl);
/*%<
 * Set the default TTL to use when converting the key
 * to a KEY or DNSKEY RR.
 *
 * Requires:
 *	"key" is a valid key.
 */

dns_ttl_t
dst_key_getttl(const dst_key_t *key);
/*%<
 * Get the default TTL to use when converting the key
 * to a KEY or DNSKEY RR.
 *
 * Requires:
 *	"key" is a valid key.
 */

isc_result_t
dst_key_setflags(dst_key_t *key, uint32_t flags);
/*
 * Set the key flags, and recompute the key ID.
 *
 * Requires:
 *	"key" is a valid key.
 */

isc_result_t
dst_key_getbool(const dst_key_t *key, int type, bool *valuep);
/*%<
 * Get a member of the boolean metadata array and place it in '*valuep'.
 *
 * Requires:
 *	"key" is a valid key.
 *	"type" is no larger than DST_MAX_BOOLEAN
 *	"valuep" is not null.
 */

void
dst_key_setbool(dst_key_t *key, int type, bool value);
/*%<
 * Set a member of the boolean metadata array.
 *
 * Requires:
 *	"key" is a valid key.
 *	"type" is no larger than DST_MAX_BOOLEAN
 */

void
dst_key_unsetbool(dst_key_t *key, int type);
/*%<
 * Flag a member of the boolean metadata array as "not set".
 *
 * Requires:
 *	"key" is a valid key.
 *	"type" is no larger than DST_MAX_BOOLEAN
 */

isc_result_t
dst_key_getnum(const dst_key_t *key, int type, uint32_t *valuep);
/*%<
 * Get a member of the numeric metadata array and place it in '*valuep'.
 *
 * Requires:
 *	"key" is a valid key.
 *	"type" is no larger than DST_MAX_NUMERIC
 *	"valuep" is not null.
 */

void
dst_key_setnum(dst_key_t *key, int type, uint32_t value);
/*%<
 * Set a member of the numeric metadata array.
 *
 * Requires:
 *	"key" is a valid key.
 *	"type" is no larger than DST_MAX_NUMERIC
 */

void
dst_key_unsetnum(dst_key_t *key, int type);
/*%<
 * Flag a member of the numeric metadata array as "not set".
 *
 * Requires:
 *	"key" is a valid key.
 *	"type" is no larger than DST_MAX_NUMERIC
 */

isc_result_t
dst_key_gettime(const dst_key_t *key, int type, isc_stdtime_t *timep);
/*%<
 * Get a member of the timing metadata array and place it in '*timep'.
 *
 * Requires:
 *	"key" is a valid key.
 *	"type" is no larger than DST_MAX_TIMES
 *	"timep" is not null.
 */

void
dst_key_settime(dst_key_t *key, int type, isc_stdtime_t when);
/*%<
 * Set a member of the timing metadata array.
 *
 * Requires:
 *	"key" is a valid key.
 *	"type" is no larger than DST_MAX_TIMES
 */

void
dst_key_unsettime(dst_key_t *key, int type);
/*%<
 * Flag a member of the timing metadata array as "not set".
 *
 * Requires:
 *	"key" is a valid key.
 *	"type" is no larger than DST_MAX_TIMES
 */

isc_result_t
dst_key_getstate(const dst_key_t *key, int type, dst_key_state_t *statep);
/*%<
 * Get a member of the keystate metadata array and place it in '*statep'.
 *
 * Requires:
 *	"key" is a valid key.
 *	"type" is no larger than DST_MAX_KEYSTATES
 *	"statep" is not null.
 */

void
dst_key_setstate(dst_key_t *key, int type, dst_key_state_t state);
/*%<
 * Set a member of the keystate metadata array.
 *
 * Requires:
 *	"key" is a valid key.
 *	"state" is a valid state.
 *	"type" is no larger than DST_MAX_KEYSTATES
 */

void
dst_key_unsetstate(dst_key_t *key, int type);
/*%<
 * Flag a member of the keystate metadata array as "not set".
 *
 * Requires:
 *	"key" is a valid key.
 *	"type" is no larger than DST_MAX_KEYSTATES
 */

isc_result_t
dst_key_getprivateformat(const dst_key_t *key, int *majorp, int *minorp);
/*%<
 * Get the private key format version number.  (If the key does not have
 * a private key associated with it, the version will be 0.0.)  The major
 * version number is placed in '*majorp', and the minor version number in
 * '*minorp'.
 *
 * Requires:
 *	"key" is a valid key.
 *	"majorp" is not NULL.
 *	"minorp" is not NULL.
 */

void
dst_key_setprivateformat(dst_key_t *key, int major, int minor);
/*%<
 * Set the private key format version number.
 *
 * Requires:
 *	"key" is a valid key.
 */

#define DST_KEY_FORMATSIZE (DNS_NAME_FORMATSIZE + DNS_SECALG_FORMATSIZE + 7)

void
dst_key_format(const dst_key_t *key, char *cp, unsigned int size);
/*%<
 * Write the uniquely identifying information about the key (name,
 * algorithm, key ID) into a string 'cp' of size 'size'.
 */

isc_buffer_t *
dst_key_tkeytoken(const dst_key_t *key);
/*%<
 * Return the token from the TKEY request, if any.  If this key was
 * not negotiated via TKEY, return NULL.
 *
 * Requires:
 *	"key" is a valid key.
 */

isc_result_t
dst_key_dump(dst_key_t *key, isc_mem_t *mctx, char **buffer, int *length);
/*%<
 * Allocate 'buffer' and dump the key into it in base64 format. The buffer
 * is not NUL terminated. The length of the buffer is returned in *length.
 *
 * 'buffer' needs to be freed using isc_mem_put(mctx, buffer, length);
 *
 * Requires:
 *	'buffer' to be non NULL and *buffer to be NULL.
 *	'length' to be non NULL and *length to be zero.
 *
 * Returns:
 *	ISC_R_SUCCESS
 *	ISC_R_NOMEMORY
 *	ISC_R_NOTIMPLEMENTED
 *	others.
 */

isc_result_t
dst_key_restore(dns_name_t *name, unsigned int alg, unsigned int flags,
		unsigned int protocol, dns_rdataclass_t rdclass,
		isc_mem_t *mctx, const char *keystr, dst_key_t **keyp);

bool
dst_key_inactive(const dst_key_t *key);
/*%<
 * Determines if the private key is missing due the key being deemed inactive.
 *
 * Requires:
 *	'key' to be valid.
 */

void
dst_key_setinactive(dst_key_t *key, bool inactive);
/*%<
 * Set key inactive state.
 *
 * Requires:
 *	'key' to be valid.
 */

void
dst_key_setexternal(dst_key_t *key, bool value);
/*%<
 * Set key external state.
 *
 * Requires:
 *	'key' to be valid.
 */

bool
dst_key_isexternal(dst_key_t *key);
/*%<
 * Check if this is an external key.
 *
 * Requires:
 *	'key' to be valid.
 */

void
dst_key_setmodified(dst_key_t *key, bool value);
/*%<
 * If 'value' is true, this marks the key to indicate that key file metadata
 * has been modified. If 'value' is false, this resets the value, for example
 * after you have written the key to file.
 *
 * Requires:
 *	'key' to be valid.
 */

bool
dst_key_ismodified(const dst_key_t *key);
/*%<
 * Check if the key file has been modified.
 *
 * Requires:
 *	'key' to be valid.
 */

bool
dst_key_haskasp(dst_key_t *key);
/*%<
 * Check if this key has state (and thus uses KASP).
 *
 * Requires:
 *	'key' to be valid.
 */

bool
dst_key_is_unused(const dst_key_t *key);
/*%<
 * Check if this key is unused.
 *
 * Requires:
 *	'key' to be valid.
 */

bool
dst_key_is_published(dst_key_t *key, isc_stdtime_t now, isc_stdtime_t *publish);
/*%<
 * Check if it is safe to publish this key (e.g. put the DNSKEY in the zone).
 *
 * Requires:
 *	'key' to be valid.
 */

bool
dst_key_is_active(dst_key_t *key, isc_stdtime_t now);
/*%<
 * Check if this key is active. This means that it is creating RRSIG records
 * (ZSK), or that it is used to create a chain of trust (KSK), or both (CSK).
 *
 * Requires:
 *	'key' to be valid.
 */

bool
dst_key_is_signing(dst_key_t *key, int role, isc_stdtime_t now,
		   isc_stdtime_t *active);
/*%<
 * Check if it is safe to use this key for signing, given the role.
 *
 * Requires:
 *	'key' to be valid.
 */

bool
dst_key_is_revoked(dst_key_t *key, isc_stdtime_t now, isc_stdtime_t *revoke);
/*%<
 * Check if this key is revoked.
 *
 * Requires:
 *	'key' to be valid.
 */

bool
dst_key_is_removed(dst_key_t *key, isc_stdtime_t now, isc_stdtime_t *remove);
/*%<
 * Check if this key is removed from the zone (e.g. the DNSKEY record should
 * no longer be in the zone).
 *
 * Requires:
 *	'key' to be valid.
 */

dst_key_state_t
dst_key_goal(const dst_key_t *key);
/*%<
 * Get the key goal. Should be OMNIPRESENT or HIDDEN.
 * This can be used to determine if the key is being introduced or
 * is on its way out.
 *
 * Requires:
 *	'key' to be valid.
 */

isc_result_t
dst_key_role(dst_key_t *key, bool *ksk, bool *zsk);
/*%<
 * Get the key role. A key can have the KSK or the ZSK role, or both.
 *
 * Requires:
 *	'key' to be valid.
 */

void
dst_key_copy_metadata(dst_key_t *to, dst_key_t *from);
/*%<
 * Copy key metadata from one key to another.
 *
 * Requires:
 *	'to' and 'from' to be valid.
 */

void
dst_key_setdirectory(dst_key_t *key, const char *dir);
/*%<
 * Set the directory where to store key files for this key.
 *
 * Requires:
 *	'key' to be valid.
 */

const char *
dst_hmac_algorithm_totext(dst_algorithm_t alg);
/*$<
 * Return the name associtated with the HMAC algorithm 'alg'
 * or return "unknown".
 */

ISC_LANG_ENDDECLS
