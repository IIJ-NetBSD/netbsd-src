/*	$NetBSD: dhcid_49.h,v 1.1 2024/02/18 20:57:46 christos Exp $	*/

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

/* */
#ifndef IN_1_DHCID_49_H
#define IN_1_DHCID_49_H 1

typedef struct dns_rdata_in_dhcid {
	dns_rdatacommon_t common;
	isc_mem_t *mctx;
	unsigned char *dhcid;
	unsigned int length;
} dns_rdata_in_dhcid_t;

#endif /* IN_1_DHCID_49_H */
