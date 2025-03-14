/*	$NetBSD: fuzz.h,v 1.8 2025/01/26 16:25:20 christos Exp $	*/

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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <isc/dir.h>
#include <isc/lang.h>
#include <isc/mem.h>
#include <isc/once.h>
#include <isc/types.h>
#include <isc/util.h>

#include <dst/dst.h>

ISC_LANG_BEGINDECLS

extern bool debug;

int
LLVMFuzzerInitialize(int *argc ISC_ATTR_UNUSED, char ***argv ISC_ATTR_UNUSED);

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#define CHECK(x)                    \
	if ((x) != ISC_R_SUCCESS) { \
		return (0);         \
	}

ISC_LANG_ENDDECLS
