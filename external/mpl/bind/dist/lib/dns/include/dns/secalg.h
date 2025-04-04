/*	$NetBSD: secalg.h,v 1.7 2025/01/26 16:25:28 christos Exp $	*/

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

/*! \file dns/secalg.h */

#include <isc/lang.h>

#include <dns/types.h>

ISC_LANG_BEGINDECLS

isc_result_t
dns_secalg_fromtext(dns_secalg_t *secalgp, isc_textregion_t *source);
/*%<
 * Convert the text 'source' refers to into a DNSSEC security algorithm value.
 * The text may contain either a mnemonic algorithm name or a decimal algorithm
 * number.
 *
 * Requires:
 *\li	'secalgp' is a valid pointer.
 *
 *\li	'source' is a valid text region.
 *
 * Returns:
 *\li	ISC_R_SUCCESS			on success
 *\li	ISC_R_RANGE			numeric type is out of range
 *\li	DNS_R_UNKNOWN			mnemonic type is unknown
 */

isc_result_t
dns_secalg_totext(dns_secalg_t secalg, isc_buffer_t *target);
/*%<
 * Put a textual representation of the DNSSEC security algorithm 'secalg'
 * into 'target'.
 *
 * Requires:
 *\li	'secalg' is a valid secalg.
 *
 *\li	'target' is a valid text buffer.
 *
 * Ensures,
 *	if the result is success:
 *\li		The used space in 'target' is updated.
 *
 * Returns:
 *\li	ISC_R_SUCCESS			on success
 *\li	ISC_R_NOSPACE			target buffer is too small
 */

#define DNS_SECALG_FORMATSIZE 20
void
dns_secalg_format(dns_secalg_t alg, char *cp, unsigned int size);
/*%<
 * Wrapper for dns_secalg_totext(), writing text into 'cp'
 */

ISC_LANG_ENDDECLS
