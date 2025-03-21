/*	$NetBSD: base64.c,v 1.7 2025/01/26 16:25:44 christos Exp $	*/

/*
 * Copyright (C) Internet Systems Consortium, Inc. ("ISC")
 *
 * SPDX-License-Identifier: MPL-2.0 AND ISC
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * See the COPYRIGHT file distributed with this work for additional
 * information regarding copyright ownership.
 */

/*
 * Copyright (C) 2001 Nominum, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC AND NOMINUM DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*! \file */

#include <isc/base64.h>
#include <isc/buffer.h>
#include <isc/region.h>
#include <isc/result.h>

#include <isccc/base64.h>
#include <isccc/util.h>

isc_result_t
isccc_base64_encode(isccc_region_t *source, int wordlength,
		    const char *wordbreak, isccc_region_t *target) {
	isc_region_t sr;
	isc_buffer_t tb;
	isc_result_t result;

	sr.base = source->rstart;
	sr.length = (unsigned int)(source->rend - source->rstart);
	isc_buffer_init(&tb, target->rstart,
			(unsigned int)(target->rend - target->rstart));

	result = isc_base64_totext(&sr, wordlength, wordbreak, &tb);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	source->rstart = source->rend;
	target->rstart = isc_buffer_used(&tb);
	return ISC_R_SUCCESS;
}

isc_result_t
isccc_base64_decode(const char *cstr, isccc_region_t *target) {
	isc_buffer_t b;
	isc_result_t result;

	isc_buffer_init(&b, target->rstart,
			(unsigned int)(target->rend - target->rstart));
	result = isc_base64_decodestring(cstr, &b);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	target->rstart = isc_buffer_used(&b);
	return ISC_R_SUCCESS;
}
