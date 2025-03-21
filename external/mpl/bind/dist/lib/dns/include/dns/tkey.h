/*	$NetBSD: tkey.h,v 1.9 2025/01/26 16:25:28 christos Exp $	*/

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

/*! \file dns/tkey.h */

#include <inttypes.h>
#include <stdbool.h>

#include <isc/lang.h>

#include <dns/types.h>

#include <dst/dst.h>
#include <dst/gssapi.h>

ISC_LANG_BEGINDECLS

/* Key agreement modes */
#define DNS_TKEYMODE_SERVERASSIGNED   1
#define DNS_TKEYMODE_DIFFIEHELLMAN    2
#define DNS_TKEYMODE_GSSAPI	      3
#define DNS_TKEYMODE_RESOLVERASSIGNED 4
#define DNS_TKEYMODE_DELETE	      5

struct dns_tkeyctx {
	dns_name_t	 *domain;
	dns_gss_cred_id_t gsscred;
	isc_mem_t	 *mctx;
	char		 *gssapi_keytab;
};

isc_result_t
dns_tkeyctx_create(isc_mem_t *mctx, dns_tkeyctx_t **tctxp);
/*%<
 *	Create an empty TKEY context.
 *
 * 	Requires:
 *\li		'mctx' is not NULL
 *\li		'tctx' is not NULL
 *\li		'*tctx' is NULL
 *
 *	Returns
 *\li		#ISC_R_SUCCESS
 *\li		#ISC_R_NOMEMORY
 *\li		return codes from dns_name_fromtext()
 */

void
dns_tkeyctx_destroy(dns_tkeyctx_t **tctxp);
/*%<
 *      Frees all data associated with the TKEY context
 *
 * 	Requires:
 *\li		'tctx' is not NULL
 *\li		'*tctx' is not NULL
 */

isc_result_t
dns_tkey_processquery(dns_message_t *msg, dns_tkeyctx_t *tctx,
		      dns_tsigkeyring_t *ring);
/*%<
 *	Processes a query containing a TKEY record, adding or deleting TSIG
 *	keys if necessary, and modifies the message to contain the response.
 *
 *	Requires:
 *\li		'msg' is a valid message
 *\li		'tctx' is a valid TKEY context
 *\li		'ring' is a valid TSIG keyring
 *
 *	Returns
 *\li		#ISC_R_SUCCESS	msg was updated (the TKEY operation succeeded,
 *				or msg now includes a TKEY with an error set)
 *		DNS_R_FORMERR	the packet was malformed (missing a TKEY
 *				or KEY).
 *\li		other		An error occurred while processing the message
 */

isc_result_t
dns_tkey_buildgssquery(dns_message_t *msg, const dns_name_t *name,
		       const dns_name_t *gname, uint32_t lifetime,
		       dns_gss_ctx_id_t *context, isc_mem_t *mctx,
		       char **err_message);
/*%<
 *	Builds a query containing a TKEY that will generate a GSSAPI context.
 *	The key is requested to have the specified lifetime (in seconds).
 *
 *	Requires:
 *\li		'msg'	  is a valid message
 *\li		'name'	  is a valid name
 *\li		'gname'	  is a valid name
 *\li		'context' is a pointer to a valid gss_ctx_id_t
 *			  (which may have the value GSS_C_NO_CONTEXT)
 *
 *	Returns:
 *\li		ISC_R_SUCCESS	msg was successfully updated to include the
 *				query to be sent
 *\li		other		an error occurred while building the message
 *\li		*err_message	optional error message
 */

isc_result_t
dns_tkey_gssnegotiate(dns_message_t *qmsg, dns_message_t *rmsg,
		      const dns_name_t *server, dns_gss_ctx_id_t *context,
		      dns_tsigkey_t **outkey, dns_tsigkeyring_t *ring,
		      char **err_message);
/*%<
 *	Client side negotiation of GSS-TSIG.  Process the response
 *	to a TKEY, and establish a TSIG key if negotiation was successful.
 *	Build a response to the input TKEY message.  Can take multiple
 *	calls to successfully establish the context.
 *
 *	Requires:
 *		'qmsg'    is a valid message, the original TKEY request;
 *			     it will be filled with the new message to send
 *		'rmsg'    is a valid message, the incoming TKEY message
 *		'server'  is the server name
 *		'context' is the input context handle
 *		'outkey'  receives the established key, if non-NULL;
 *			      if non-NULL must point to NULL
 *		'ring'	  is the keyring in which to establish the key,
 *			      or NULL
 *
 *	Returns:
 *		ISC_R_SUCCESS	context was successfully established
 *		ISC_R_NOTFOUND  couldn't find a needed part of the query
 *					or response
 *		DNS_R_CONTINUE  additional context negotiation is required;
 *					send the new qmsg to the server
 */
ISC_LANG_ENDDECLS
