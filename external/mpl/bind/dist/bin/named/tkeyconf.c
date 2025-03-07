/*	$NetBSD: tkeyconf.c,v 1.8 2025/01/26 16:24:33 christos Exp $	*/

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

/*! \file */

#include <inttypes.h>

#include <isc/buffer.h>
#include <isc/mem.h>
#include <isc/string.h>

#include <dns/fixedname.h>
#include <dns/keyvalues.h>
#include <dns/name.h>
#include <dns/tkey.h>

#include <dst/gssapi.h>

#include <isccfg/cfg.h>

#include <named/tkeyconf.h>

#define RETERR(x)                            \
	do {                                 \
		result = (x);                \
		if (result != ISC_R_SUCCESS) \
			goto failure;        \
	} while (0)

#include <named/log.h>
#define LOG(msg)                                               \
	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL, \
		      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR, "%s", msg)

isc_result_t
named_tkeyctx_fromconfig(const cfg_obj_t *options, isc_mem_t *mctx,
			 dns_tkeyctx_t **tctxp) {
	isc_result_t result;
	dns_tkeyctx_t *tctx = NULL;
	const char *s;
	dns_fixedname_t fname;
	dns_name_t *name;
	isc_buffer_t b;
	const cfg_obj_t *obj;

	result = dns_tkeyctx_create(mctx, &tctx);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	obj = NULL;
	result = cfg_map_get(options, "tkey-domain", &obj);
	if (result == ISC_R_SUCCESS) {
		s = cfg_obj_asstring(obj);
		isc_buffer_constinit(&b, s, strlen(s));
		isc_buffer_add(&b, strlen(s));
		name = dns_fixedname_initname(&fname);
		RETERR(dns_name_fromtext(name, &b, dns_rootname, 0, NULL));
		tctx->domain = isc_mem_get(mctx, sizeof(dns_name_t));
		dns_name_init(tctx->domain, NULL);
		dns_name_dup(name, mctx, tctx->domain);
	}

	obj = NULL;
	result = cfg_map_get(options, "tkey-gssapi-credential", &obj);
	if (result == ISC_R_SUCCESS) {
		s = cfg_obj_asstring(obj);

		isc_buffer_constinit(&b, s, strlen(s));
		isc_buffer_add(&b, strlen(s));
		name = dns_fixedname_initname(&fname);
		RETERR(dns_name_fromtext(name, &b, dns_rootname, 0, NULL));
		RETERR(dst_gssapi_acquirecred(name, false, &tctx->gsscred));
	}

	obj = NULL;
	result = cfg_map_get(options, "tkey-gssapi-keytab", &obj);
	if (result == ISC_R_SUCCESS) {
		s = cfg_obj_asstring(obj);
		tctx->gssapi_keytab = isc_mem_strdup(mctx, s);
	}

	*tctxp = tctx;
	return ISC_R_SUCCESS;

failure:
	dns_tkeyctx_destroy(&tctx);
	return result;
}
