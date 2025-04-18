/*	$NetBSD: ccmsg.h,v 1.8 2025/01/26 16:25:44 christos Exp $	*/

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

#pragma once

/*! \file isccc/ccmsg.h */

#include <inttypes.h>

#include <isc/buffer.h>
#include <isc/lang.h>
#include <isc/netmgr.h>
#include <isc/sockaddr.h>

#include <isccc/types.h>

/*% ISCCC Message Structure */
typedef struct isccc_ccmsg {
	/* private (don't touch!) */
	unsigned int	magic;
	uint32_t	size;
	isc_buffer_t   *buffer;
	unsigned int	maxsize;
	isc_mem_t      *mctx;
	isc_nmhandle_t *handle;
	isc_nm_cb_t	recv_cb;
	void	       *recv_cbarg;
	isc_nm_cb_t	send_cb;
	void	       *send_cbarg;
} isccc_ccmsg_t;

ISC_LANG_BEGINDECLS

void
isccc_ccmsg_init(isc_mem_t *mctx, isc_nmhandle_t *handle, isccc_ccmsg_t *ccmsg);
/*%
 * Associate a cc message state with a given memory context and
 * netmgr handle. (Note that the caller must hold a reference to
 * the handle during asynchronous ccmsg operations; the ccmsg code
 * does not hold the reference itself.)
 *
 * Requires:
 *
 *\li	"mctx" be a valid memory context.
 *
 *\li	"handle" be a netmgr handle for a stream socket.
 *
 *\li	"ccmsg" be non-NULL and an uninitialized or invalidated structure.
 *
 * Ensures:
 *
 *\li	"ccmsg" is a valid structure.
 */

void
isccc_ccmsg_setmaxsize(isccc_ccmsg_t *ccmsg, unsigned int maxsize);
/*%
 * Set the maximum packet size to "maxsize"
 *
 * Requires:
 *
 *\li	"ccmsg" be valid.
 *
 *\li	512 <= "maxsize" <= 4294967296
 */

void
isccc_ccmsg_readmessage(isccc_ccmsg_t *ccmsg, isc_nm_cb_t cb, void *cbarg);
/*%
 * Schedule an event to be delivered when a command channel message is
 * readable, or when an error occurs on the socket.
 *
 * Requires:
 *
 *\li	"ccmsg" be valid.
 *
 * Notes:
 *
 *\li	The event delivered is a fully generic event.  It will contain no
 *	actual data.  The sender will be a pointer to the isccc_ccmsg_t.
 *	The result code inside that structure should be checked to see
 *	what the final result was.
 */

void
isccc_ccmsg_sendmessage(isccc_ccmsg_t *ccmsg, isc_region_t *region,
			isc_nm_cb_t cb, void *cbarg);
/*%
 * Sends region over the command channel message.
 *
 * CAVEAT: Only a single send message can be scheduled at the time.
 */

void
isccc_ccmsg_disconnect(isccc_ccmsg_t *ccmsg);
/*%
 * Disconnect from the connected netmgr handle associated with a command
 * channel message.
 *
 * Requires:
 *
 *\li	"ccmsg" to be valid.
 */

void
isccc_ccmsg_invalidate(isccc_ccmsg_t *ccmsg);
/*%
 * Clean up the magic number and the dynamic buffer associated with a command
 * channel message.
 *
 * Requires:
 *
 *\li	"ccmsg" to be valid.
 */

void
isccc_ccmsg_toregion(isccc_ccmsg_t *ccmsg, isccc_region_t *ccregion);

ISC_LANG_ENDDECLS
