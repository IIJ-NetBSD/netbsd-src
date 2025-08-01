/*	$NetBSD: keymgr.h,v 1.10 2025/07/17 19:01:46 christos Exp $	*/

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

/*! \file dns/keymgr.h */

#include <isc/lang.h>
#include <isc/stdtime.h>

#include <dns/types.h>

#include <dst/dst.h>

ISC_LANG_BEGINDECLS

void
dns_keymgr_settime_syncpublish(dst_key_t *key, dns_kasp_t *kasp, bool first);
/*%<
 * Set the SyncPublish time (when the DS may be submitted to the parent).
 * If 'first' is true, also make sure that the zone signatures are omnipresent.
 *
 *      Requires:
 *\li           'key' is a valid DNSSEC key.
 *\li           'kasp' is a valid DNSSEC policy.
 */

isc_result_t
dns_keymgr_run(const dns_name_t *origin, dns_rdataclass_t rdclass,
	       isc_mem_t *mctx, dns_dnsseckeylist_t *keyring,
	       dns_dnsseckeylist_t *dnskeys, const char *keydir,
	       dns_kasp_t *kasp, isc_stdtime_t now, isc_stdtime_t *nexttime);
/*%<
 * Manage keys in 'keyring' and update timing data according to 'kasp' policy.
 * Create new keys for 'origin' if necessary.  Append all such keys, along
 * with use hints gleaned from their metadata, onto 'keyring'.
 *
 * Update key states and store changes back to disk. Store when to run next
 * in 'nexttime'.
 *
 *	Requires:
 *\li		'origin' is a valid FQDN.
 *\li		'mctx' is a valid memory context.
 *\li		'keyring' is not NULL.
 *\li		'kasp' is not NULL.
 *
 *	Returns:
 *\li		#ISC_R_SUCCESS
 *\li		any error returned by dst_key_generate(), isc_dir_open(),
 *		dst_key_to_file(), or dns_dnsseckey_create().
 *
 *	Ensures:
 *\li		On error, keypool is unchanged
 */

isc_result_t
dns_keymgr_offline(const dns_name_t *origin, dns_dnsseckeylist_t *keyring,
		   dns_kasp_t *kasp, isc_stdtime_t now,
		   isc_stdtime_t *nexttime);
/*%<
 * Manage keys in 'keyring' when in offline-ksk mode, and update timing data
 * according to the metadata in the key files. KSKs are skipped.
 *
 * Update key states and store changes back to disk. Store when to run next
 * in 'nexttime'.
 *
 *	Requires:
 *\li		'origin' is a valid FQDN.
 *\li		'mctx' is a valid memory context.
 *\li		'keyring' is not NULL.
 *\li		'kasp' is not NULL.
 *
 *	Returns:
 *\li		#ISC_R_SUCCESS
 *\li		any error returned by dst_key_getstate(), dst_key_gettime(),
 *		isc_dir_open(), or dst_key_to_file().
 */

isc_result_t
dns_keymgr_checkds(dns_kasp_t *kasp, dns_dnsseckeylist_t *keyring,
		   isc_stdtime_t now, isc_stdtime_t when, bool dspublish);
isc_result_t
dns_keymgr_checkds_id(dns_kasp_t *kasp, dns_dnsseckeylist_t *keyring,
		      isc_stdtime_t now, isc_stdtime_t when, bool dspublish,
		      dns_keytag_t id, unsigned int algorithm);
/*%<
 * Check DS for one key in 'keyring'. The key must have the KSK role.
 * If 'dspublish' is set to true, set the DS Publish time to 'now'.
 * If 'dspublish' is set to false, set the DS Removed time to 'now'.
 * If a specific key 'id' is given it must match the keytag.
 * If the 'algorithm' is non-zero, it must match the key's algorithm.
 * The result is stored in the key state file.
 *
 *	Requires:
 *\li		'kasp' is not NULL.
 *\li		'keyring' is not NULL.
 *
 *	Returns:
 *\li		#ISC_R_SUCCESS (No error).
 *\li		#DNS_R_NOKEYMATCH (No matching keys found).
 *\li		#DNS_R_TOOMANYKEYS (More than one matching keys found).
 *
 */

isc_result_t
dns_keymgr_rollover(dns_kasp_t *kasp, dns_dnsseckeylist_t *keyring,
		    isc_stdtime_t now, isc_stdtime_t when, dns_keytag_t id,
		    unsigned int algorithm);
/*%<
 * Rollover key with given 'id'. If the 'algorithm' is non-zero, it must
 * match the key's algorithm. The changes are stored in the key state file.
 *
 * A rollover means adjusting the key metadata so that keymgr will start the
 * actual rollover on the next run. Update the 'inactive' time and adjust
 * key lifetime to match the 'when' to rollover time.
 *
 * The 'when' time may be in the past. In that case keymgr will roll the
 * key as soon as possible.
 *
 * The 'when' time may be in the future. This may extend the lifetime,
 * overriding the default lifetime from the policy.
 *
 *	Requires:
 *\li		'kasp' is not NULL.
 *\li		'keyring' is not NULL.
 *
 *	Returns:
 *\li		#ISC_R_SUCCESS (No error).
 *\li		#DNS_R_NOKEYMATCH (No matching keys found).
 *\li		#DNS_R_TOOMANYKEYS (More than one matching keys found).
 *\li		#DNS_R_KEYNOTACTIVE (Key is not active).
 *
 */

isc_result_t
dns_keymgr_status(dns_kasp_t *kasp, dns_dnsseckeylist_t *keyring,
		  isc_stdtime_t now, char *out, size_t out_len);
/*%<
 * Retrieve the status of given 'kasp' policy and keys in the
 * 'keyring' and store the printable output in the 'out' buffer.
 *
 *	Requires:
 *\li		'kasp' is not NULL.
 *\li		'keyring' is not NULL.
 *\li		'out' is not NULL.
 *
 *	Returns:
 *\li		ISC_R_SUCCESS on success.
 *\li		ISC_R_NOSPACE if the 'out' buffer is too small.
 *\li		ISC_R_FAILURE if other error occurred.
 *\li		Printable status in 'out'.
 *
 */

bool
dns_keymgr_key_may_be_purged(const dst_key_t *key, uint32_t after,
			     isc_stdtime_t now);
/*%<
 * Checks if the key files for 'key' may be removed from disk.
 *
 *     Requires:
 *\li          'key' is a valid key.
 *
 *     Returns:
 *\li          true if the key files may be purged, false otherwise.
 */

ISC_LANG_ENDDECLS
