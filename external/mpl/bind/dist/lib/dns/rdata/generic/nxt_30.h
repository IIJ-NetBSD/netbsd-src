/*	$NetBSD: nxt_30.h,v 1.8 2025/01/26 16:25:32 christos Exp $	*/

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

/*!
 *  \brief RFC2535 */

typedef struct dns_rdata_nxt {
	dns_rdatacommon_t common;
	isc_mem_t *mctx;
	dns_name_t next;
	unsigned char *typebits;
	uint16_t len;
} dns_rdata_nxt_t;
