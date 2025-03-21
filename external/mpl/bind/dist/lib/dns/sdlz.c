/*	$NetBSD: sdlz.c,v 1.14 2025/01/26 16:25:25 christos Exp $	*/

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
 * Copyright (C) 2002 Stichting NLnet, Netherlands, stichting@nlnet.nl.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND STICHTING NLNET
 * DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
 * STICHTING NLNET BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE
 * USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * The development of Dynamically Loadable Zones (DLZ) for Bind 9 was
 * conceived and contributed by Rob Butler.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ROB BUTLER
 * DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
 * ROB BUTLER BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE
 * USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*! \file */

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include <isc/ascii.h>
#include <isc/buffer.h>
#include <isc/lex.h>
#include <isc/log.h>
#include <isc/magic.h>
#include <isc/mem.h>
#include <isc/once.h>
#include <isc/region.h>
#include <isc/result.h>
#include <isc/rwlock.h>
#include <isc/string.h>
#include <isc/util.h>

#include <dns/callbacks.h>
#include <dns/db.h>
#include <dns/dbiterator.h>
#include <dns/dlz.h>
#include <dns/fixedname.h>
#include <dns/log.h>
#include <dns/master.h>
#include <dns/rdata.h>
#include <dns/rdatalist.h>
#include <dns/rdataset.h>
#include <dns/rdatasetiter.h>
#include <dns/rdatatype.h>
#include <dns/sdlz.h>
#include <dns/types.h>

/*
 * Private Types
 */

struct dns_sdlzimplementation {
	const dns_sdlzmethods_t *methods;
	isc_mem_t *mctx;
	void *driverarg;
	unsigned int flags;
	isc_mutex_t driverlock;
	dns_dlzimplementation_t *dlz_imp;
};

struct dns_sdlz_db {
	/* Unlocked */
	dns_db_t common;
	void *dbdata;
	dns_sdlzimplementation_t *dlzimp;

	/* Locked */
	dns_dbversion_t *future_version;
	int dummy_version;
};

struct dns_sdlzlookup {
	/* Unlocked */
	unsigned int magic;
	dns_sdlz_db_t *sdlz;
	ISC_LIST(dns_rdatalist_t) lists;
	ISC_LIST(isc_buffer_t) buffers;
	dns_name_t *name;
	ISC_LINK(dns_sdlzlookup_t) link;
	dns_rdatacallbacks_t callbacks;

	/* Atomic */
	isc_refcount_t references;
};

typedef struct dns_sdlzlookup dns_sdlznode_t;

struct dns_sdlzallnodes {
	dns_dbiterator_t common;
	ISC_LIST(dns_sdlznode_t) nodelist;
	dns_sdlznode_t *current;
	dns_sdlznode_t *origin;
};

typedef dns_sdlzallnodes_t sdlz_dbiterator_t;

typedef struct sdlz_rdatasetiter {
	dns_rdatasetiter_t common;
	dns_rdatalist_t *current;
} sdlz_rdatasetiter_t;

#define SDLZDB_MAGIC ISC_MAGIC('D', 'L', 'Z', 'S')

/*
 * Note that "impmagic" is not the first four bytes of the struct, so
 * ISC_MAGIC_VALID cannot be used.
 */

#define VALID_SDLZDB(sdlzdb) \
	((sdlzdb) != NULL && (sdlzdb)->common.impmagic == SDLZDB_MAGIC)

#define SDLZLOOKUP_MAGIC	ISC_MAGIC('D', 'L', 'Z', 'L')
#define VALID_SDLZLOOKUP(sdlzl) ISC_MAGIC_VALID(sdlzl, SDLZLOOKUP_MAGIC)
#define VALID_SDLZNODE(sdlzn)	VALID_SDLZLOOKUP(sdlzn)

/* These values are taken from RFC 1537 */
#define SDLZ_DEFAULT_REFRESH 28800U  /* 8 hours */
#define SDLZ_DEFAULT_RETRY   7200U   /* 2 hours */
#define SDLZ_DEFAULT_EXPIRE  604800U /* 7 days */
#define SDLZ_DEFAULT_MINIMUM 86400U  /* 1 day */

/* This is a reasonable value */
#define SDLZ_DEFAULT_TTL (60 * 60 * 24)

#ifdef __COVERITY__
#define MAYBE_LOCK(imp)	  LOCK(&imp->driverlock)
#define MAYBE_UNLOCK(imp) UNLOCK(&imp->driverlock)
#else /* ifdef __COVERITY__ */
#define MAYBE_LOCK(imp)                                     \
	do {                                                \
		unsigned int flags = imp->flags;            \
		if ((flags & DNS_SDLZFLAG_THREADSAFE) == 0) \
			LOCK(&imp->driverlock);             \
	} while (0)

#define MAYBE_UNLOCK(imp)                                   \
	do {                                                \
		unsigned int flags = imp->flags;            \
		if ((flags & DNS_SDLZFLAG_THREADSAFE) == 0) \
			UNLOCK(&imp->driverlock);           \
	} while (0)
#endif /* ifdef __COVERITY__ */

/*
 * Forward references.
 */
static isc_result_t
getnodedata(dns_db_t *db, const dns_name_t *name, bool create,
	    unsigned int options, dns_clientinfomethods_t *methods,
	    dns_clientinfo_t *clientinfo, dns_dbnode_t **nodep);

static void
list_tordataset(dns_rdatalist_t *rdatalist, dns_db_t *db, dns_dbnode_t *node,
		dns_rdataset_t *rdataset);

static void
detachnode(dns_db_t *db, dns_dbnode_t **targetp DNS__DB_FLARG);

static void
dbiterator_destroy(dns_dbiterator_t **iteratorp DNS__DB_FLARG);
static isc_result_t
dbiterator_first(dns_dbiterator_t *iterator DNS__DB_FLARG);
static isc_result_t
dbiterator_last(dns_dbiterator_t *iterator DNS__DB_FLARG);
static isc_result_t
dbiterator_seek(dns_dbiterator_t *iterator,
		const dns_name_t *name DNS__DB_FLARG);
static isc_result_t
dbiterator_prev(dns_dbiterator_t *iterator DNS__DB_FLARG);
static isc_result_t
dbiterator_next(dns_dbiterator_t *iterator DNS__DB_FLARG);
static isc_result_t
dbiterator_current(dns_dbiterator_t *iterator, dns_dbnode_t **nodep,
		   dns_name_t *name DNS__DB_FLARG);
static isc_result_t
dbiterator_pause(dns_dbiterator_t *iterator);
static isc_result_t
dbiterator_origin(dns_dbiterator_t *iterator, dns_name_t *name);

static dns_dbiteratormethods_t dbiterator_methods = {
	dbiterator_destroy, dbiterator_first, dbiterator_last,
	dbiterator_seek,    dbiterator_prev,  dbiterator_next,
	dbiterator_current, dbiterator_pause, dbiterator_origin
};

/*
 * Utility functions
 */

/*
 * Log a message at the given level
 */
static void
sdlz_log(int level, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	isc_log_vwrite(dns_lctx, DNS_LOGCATEGORY_DATABASE, DNS_LOGMODULE_DLZ,
		       ISC_LOG_DEBUG(level), fmt, ap);
	va_end(ap);
}

static unsigned int
initial_size(const char *data) {
	unsigned int len = (strlen(data) / 64) + 1;
	return len * 64 + 64;
}

/*
 * Rdataset Iterator Methods. These methods were "borrowed" from the SDB
 * driver interface.  See the SDB driver interface documentation for more info.
 */

static void
rdatasetiter_destroy(dns_rdatasetiter_t **iteratorp DNS__DB_FLARG) {
	sdlz_rdatasetiter_t *sdlziterator = (sdlz_rdatasetiter_t *)(*iteratorp);

	detachnode(sdlziterator->common.db,
		   &sdlziterator->common.node DNS__DB_FLARG_PASS);
	isc_mem_put(sdlziterator->common.db->mctx, sdlziterator,
		    sizeof(sdlz_rdatasetiter_t));
	*iteratorp = NULL;
}

static isc_result_t
rdatasetiter_first(dns_rdatasetiter_t *iterator DNS__DB_FLARG) {
	sdlz_rdatasetiter_t *sdlziterator = (sdlz_rdatasetiter_t *)iterator;
	dns_sdlznode_t *sdlznode = (dns_sdlznode_t *)iterator->node;

	if (ISC_LIST_EMPTY(sdlznode->lists)) {
		return ISC_R_NOMORE;
	}
	sdlziterator->current = ISC_LIST_HEAD(sdlznode->lists);
	return ISC_R_SUCCESS;
}

static isc_result_t
rdatasetiter_next(dns_rdatasetiter_t *iterator DNS__DB_FLARG) {
	sdlz_rdatasetiter_t *sdlziterator = (sdlz_rdatasetiter_t *)iterator;

	sdlziterator->current = ISC_LIST_NEXT(sdlziterator->current, link);
	if (sdlziterator->current == NULL) {
		return ISC_R_NOMORE;
	} else {
		return ISC_R_SUCCESS;
	}
}

static void
rdatasetiter_current(dns_rdatasetiter_t *iterator,
		     dns_rdataset_t *rdataset DNS__DB_FLARG) {
	sdlz_rdatasetiter_t *sdlziterator = (sdlz_rdatasetiter_t *)iterator;

	list_tordataset(sdlziterator->current, iterator->db, iterator->node,
			rdataset);
}

static dns_rdatasetitermethods_t rdatasetiter_methods = {
	rdatasetiter_destroy, rdatasetiter_first, rdatasetiter_next,
	rdatasetiter_current
};

/*
 * DB routines. These methods were "borrowed" from the SDB driver interface.
 * See the SDB driver interface documentation for more info.
 */

static void
destroy(dns_db_t *db) {
	dns_sdlz_db_t *sdlz = (dns_sdlz_db_t *)db;

	sdlz->common.magic = 0;
	sdlz->common.impmagic = 0;

	dns_name_free(&sdlz->common.origin, sdlz->common.mctx);

	isc_refcount_destroy(&sdlz->common.references);
	isc_mem_putanddetach(&sdlz->common.mctx, sdlz, sizeof(dns_sdlz_db_t));
}

static void
currentversion(dns_db_t *db, dns_dbversion_t **versionp) {
	dns_sdlz_db_t *sdlz = (dns_sdlz_db_t *)db;
	REQUIRE(VALID_SDLZDB(sdlz));
	REQUIRE(versionp != NULL && *versionp == NULL);

	*versionp = (void *)&sdlz->dummy_version;
	return;
}

static isc_result_t
newversion(dns_db_t *db, dns_dbversion_t **versionp) {
	dns_sdlz_db_t *sdlz = (dns_sdlz_db_t *)db;
	char origin[DNS_NAME_MAXTEXT + 1];
	isc_result_t result;

	REQUIRE(VALID_SDLZDB(sdlz));

	if (sdlz->dlzimp->methods->newversion == NULL) {
		return ISC_R_NOTIMPLEMENTED;
	}

	dns_name_format(&sdlz->common.origin, origin, sizeof(origin));

	result = sdlz->dlzimp->methods->newversion(
		origin, sdlz->dlzimp->driverarg, sdlz->dbdata, versionp);
	if (result != ISC_R_SUCCESS) {
		sdlz_log(ISC_LOG_ERROR,
			 "sdlz newversion on origin %s failed : %s", origin,
			 isc_result_totext(result));
		return result;
	}

	sdlz->future_version = *versionp;
	return ISC_R_SUCCESS;
}

static void
attachversion(dns_db_t *db, dns_dbversion_t *source,
	      dns_dbversion_t **targetp) {
	dns_sdlz_db_t *sdlz = (dns_sdlz_db_t *)db;

	REQUIRE(VALID_SDLZDB(sdlz));
	REQUIRE(source != NULL && source == (void *)&sdlz->dummy_version);

	*targetp = source;
}

static void
closeversion(dns_db_t *db, dns_dbversion_t **versionp,
	     bool commit DNS__DB_FLARG) {
	dns_sdlz_db_t *sdlz = (dns_sdlz_db_t *)db;
	char origin[DNS_NAME_MAXTEXT + 1];

	REQUIRE(VALID_SDLZDB(sdlz));
	REQUIRE(versionp != NULL);

	if (*versionp == (void *)&sdlz->dummy_version) {
		*versionp = NULL;
		return;
	}

	REQUIRE(*versionp == sdlz->future_version);
	REQUIRE(sdlz->dlzimp->methods->closeversion != NULL);

	dns_name_format(&sdlz->common.origin, origin, sizeof(origin));

	sdlz->dlzimp->methods->closeversion(origin, commit,
					    sdlz->dlzimp->driverarg,
					    sdlz->dbdata, versionp);
	if (*versionp != NULL) {
		sdlz_log(ISC_LOG_ERROR, "sdlz closeversion on origin %s failed",
			 origin);
	}

	sdlz->future_version = NULL;
}

static isc_result_t
createnode(dns_sdlz_db_t *sdlz, dns_sdlznode_t **nodep) {
	dns_sdlznode_t *node;

	node = isc_mem_get(sdlz->common.mctx, sizeof(dns_sdlznode_t));

	node->sdlz = NULL;
	dns_db_attach((dns_db_t *)sdlz, (dns_db_t **)&node->sdlz);
	ISC_LIST_INIT(node->lists);
	ISC_LIST_INIT(node->buffers);
	ISC_LINK_INIT(node, link);
	node->name = NULL;
	dns_rdatacallbacks_init(&node->callbacks);

	isc_refcount_init(&node->references, 1);
	node->magic = SDLZLOOKUP_MAGIC;

	*nodep = node;
	return ISC_R_SUCCESS;
}

static void
destroynode(dns_sdlznode_t *node) {
	dns_rdatalist_t *list;
	dns_rdata_t *rdata;
	isc_buffer_t *b;
	dns_sdlz_db_t *sdlz;
	dns_db_t *db;
	isc_mem_t *mctx;

	isc_refcount_destroy(&node->references);

	sdlz = node->sdlz;
	mctx = sdlz->common.mctx;

	while (!ISC_LIST_EMPTY(node->lists)) {
		list = ISC_LIST_HEAD(node->lists);
		while (!ISC_LIST_EMPTY(list->rdata)) {
			rdata = ISC_LIST_HEAD(list->rdata);
			ISC_LIST_UNLINK(list->rdata, rdata, link);
			isc_mem_put(mctx, rdata, sizeof(dns_rdata_t));
		}
		ISC_LIST_UNLINK(node->lists, list, link);
		isc_mem_put(mctx, list, sizeof(dns_rdatalist_t));
	}

	while (!ISC_LIST_EMPTY(node->buffers)) {
		b = ISC_LIST_HEAD(node->buffers);
		ISC_LIST_UNLINK(node->buffers, b, link);
		isc_buffer_free(&b);
	}

	if (node->name != NULL) {
		dns_name_free(node->name, mctx);
		isc_mem_put(mctx, node->name, sizeof(dns_name_t));
	}

	node->magic = 0;
	isc_mem_put(mctx, node, sizeof(dns_sdlznode_t));
	db = &sdlz->common;
	dns_db_detach(&db);
}

static isc_result_t
getnodedata(dns_db_t *db, const dns_name_t *name, bool create,
	    unsigned int options, dns_clientinfomethods_t *methods,
	    dns_clientinfo_t *clientinfo, dns_dbnode_t **nodep) {
	dns_sdlz_db_t *sdlz = (dns_sdlz_db_t *)db;
	dns_sdlznode_t *node = NULL;
	isc_result_t result;
	isc_buffer_t b;
	char namestr[DNS_NAME_MAXTEXT + 1];
	isc_buffer_t b2;
	char zonestr[DNS_NAME_MAXTEXT + 1];
	bool isorigin;
	dns_sdlzauthorityfunc_t authority;

	REQUIRE(VALID_SDLZDB(sdlz));
	REQUIRE(nodep != NULL && *nodep == NULL);

	if (sdlz->dlzimp->methods->newversion == NULL) {
		REQUIRE(!create);
	}

	isc_buffer_init(&b, namestr, sizeof(namestr));
	if ((sdlz->dlzimp->flags & DNS_SDLZFLAG_RELATIVEOWNER) != 0) {
		dns_name_t relname;
		unsigned int labels;

		labels = dns_name_countlabels(name) -
			 dns_name_countlabels(&sdlz->common.origin);
		dns_name_init(&relname, NULL);
		dns_name_getlabelsequence(name, 0, labels, &relname);
		result = dns_name_totext(&relname, DNS_NAME_OMITFINALDOT, &b);
		if (result != ISC_R_SUCCESS) {
			return result;
		}
	} else {
		result = dns_name_totext(name, DNS_NAME_OMITFINALDOT, &b);
		if (result != ISC_R_SUCCESS) {
			return result;
		}
	}
	isc_buffer_putuint8(&b, 0);

	isc_buffer_init(&b2, zonestr, sizeof(zonestr));
	result = dns_name_totext(&sdlz->common.origin, DNS_NAME_OMITFINALDOT,
				 &b2);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	isc_buffer_putuint8(&b2, 0);

	result = createnode(sdlz, &node);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	isorigin = dns_name_equal(name, &sdlz->common.origin);

	/* make sure strings are always lowercase */
	isc_ascii_strtolower(zonestr);
	isc_ascii_strtolower(namestr);

	MAYBE_LOCK(sdlz->dlzimp);

	/* try to lookup the host (namestr) */
	result = sdlz->dlzimp->methods->lookup(
		zonestr, namestr, sdlz->dlzimp->driverarg, sdlz->dbdata, node,
		methods, clientinfo);

	/*
	 * If the name was not found and DNS_DBFIND_NOWILD is not
	 * set, then we try to find a wildcard entry.
	 *
	 * If DNS_DBFIND_NOZONECUT is set and there are multiple
	 * levels between the host and the zone origin, we also look
	 * for wildcards at each level.
	 */
	if (result == ISC_R_NOTFOUND && !create &&
	    (options & DNS_DBFIND_NOWILD) == 0)
	{
		unsigned int i, dlabels, nlabels;

		nlabels = dns_name_countlabels(name);
		dlabels = nlabels - dns_name_countlabels(&sdlz->common.origin);
		for (i = 0; i < dlabels; i++) {
			char wildstr[DNS_NAME_MAXTEXT + 1];
			dns_fixedname_t fixed;
			const dns_name_t *wild;

			dns_fixedname_init(&fixed);
			if (i == dlabels - 1) {
				wild = dns_wildcardname;
			} else {
				dns_name_t *fname;
				fname = dns_fixedname_name(&fixed);
				dns_name_getlabelsequence(
					name, i + 1, dlabels - i - 1, fname);
				result = dns_name_concatenate(
					dns_wildcardname, fname, fname, NULL);
				if (result != ISC_R_SUCCESS) {
					MAYBE_UNLOCK(sdlz->dlzimp);
					return result;
				}
				wild = fname;
			}

			isc_buffer_init(&b, wildstr, sizeof(wildstr));
			result = dns_name_totext(wild, DNS_NAME_OMITFINALDOT,
						 &b);
			if (result != ISC_R_SUCCESS) {
				MAYBE_UNLOCK(sdlz->dlzimp);
				return result;
			}
			isc_buffer_putuint8(&b, 0);

			result = sdlz->dlzimp->methods->lookup(
				zonestr, wildstr, sdlz->dlzimp->driverarg,
				sdlz->dbdata, node, methods, clientinfo);
			if (result == ISC_R_SUCCESS) {
				break;
			}
		}
	}

	MAYBE_UNLOCK(sdlz->dlzimp);

	if (result == ISC_R_NOTFOUND && (isorigin || create)) {
		result = ISC_R_SUCCESS;
	}

	if (result != ISC_R_SUCCESS) {
		isc_refcount_decrementz(&node->references);
		destroynode(node);
		return result;
	}

	if (isorigin && sdlz->dlzimp->methods->authority != NULL) {
		MAYBE_LOCK(sdlz->dlzimp);
		authority = sdlz->dlzimp->methods->authority;
		result = (*authority)(zonestr, sdlz->dlzimp->driverarg,
				      sdlz->dbdata, node);
		MAYBE_UNLOCK(sdlz->dlzimp);
		if (result != ISC_R_SUCCESS && result != ISC_R_NOTIMPLEMENTED) {
			isc_refcount_decrementz(&node->references);
			destroynode(node);
			return result;
		}
	}

	if (node->name == NULL) {
		node->name = isc_mem_get(sdlz->common.mctx, sizeof(dns_name_t));
		dns_name_init(node->name, NULL);
		dns_name_dup(name, sdlz->common.mctx, node->name);
	}

	*nodep = node;
	return ISC_R_SUCCESS;
}

static isc_result_t
findnodeext(dns_db_t *db, const dns_name_t *name, bool create,
	    dns_clientinfomethods_t *methods, dns_clientinfo_t *clientinfo,
	    dns_dbnode_t **nodep DNS__DB_FLARG) {
	return getnodedata(db, name, create, 0, methods, clientinfo, nodep);
}

static isc_result_t
findnode(dns_db_t *db, const dns_name_t *name, bool create,
	 dns_dbnode_t **nodep DNS__DB_FLARG) {
	return getnodedata(db, name, create, 0, NULL, NULL, nodep);
}

static void
attachnode(dns_db_t *db, dns_dbnode_t *source,
	   dns_dbnode_t **targetp DNS__DB_FLARG) {
	dns_sdlz_db_t *sdlz = (dns_sdlz_db_t *)db;
	dns_sdlznode_t *node = (dns_sdlznode_t *)source;
	uint_fast32_t refs;

	REQUIRE(VALID_SDLZDB(sdlz));

	UNUSED(sdlz);

	refs = isc_refcount_increment(&node->references);
#if DNS_DB_NODETRACE
	fprintf(stderr, "incr:node:%s:%s:%u:%p->references = %" PRIuFAST32 "\n",
		func, file, line, node, refs + 1);
#else
	UNUSED(refs);
#endif

	*targetp = source;
}

static void
detachnode(dns_db_t *db, dns_dbnode_t **targetp DNS__DB_FLARG) {
	dns_sdlz_db_t *sdlz = (dns_sdlz_db_t *)db;
	dns_sdlznode_t *node;
	uint_fast32_t refs;

	REQUIRE(VALID_SDLZDB(sdlz));
	REQUIRE(targetp != NULL && *targetp != NULL);

	UNUSED(sdlz);

	node = (dns_sdlznode_t *)(*targetp);
	*targetp = NULL;

	refs = isc_refcount_decrement(&node->references);
#if DNS_DB_NODETRACE
	fprintf(stderr, "decr:node:%s:%s:%u:%p->references = %" PRIuFAST32 "\n",
		func, file, line, node, refs - 1);
#else
	UNUSED(refs);
#endif

	if (refs == 1) {
		destroynode(node);
	}
}

static isc_result_t
createiterator(dns_db_t *db, unsigned int options,
	       dns_dbiterator_t **iteratorp) {
	dns_sdlz_db_t *sdlz = (dns_sdlz_db_t *)db;
	sdlz_dbiterator_t *sdlziter;
	isc_result_t result;
	isc_buffer_t b;
	char zonestr[DNS_NAME_MAXTEXT + 1];

	REQUIRE(VALID_SDLZDB(sdlz));

	if (sdlz->dlzimp->methods->allnodes == NULL) {
		return ISC_R_NOTIMPLEMENTED;
	}

	if ((options & DNS_DB_NSEC3ONLY) != 0 ||
	    (options & DNS_DB_NONSEC3) != 0)
	{
		return ISC_R_NOTIMPLEMENTED;
	}

	isc_buffer_init(&b, zonestr, sizeof(zonestr));
	result = dns_name_totext(&sdlz->common.origin, DNS_NAME_OMITFINALDOT,
				 &b);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	isc_buffer_putuint8(&b, 0);

	sdlziter = isc_mem_get(sdlz->common.mctx, sizeof(sdlz_dbiterator_t));

	sdlziter->common.methods = &dbiterator_methods;
	sdlziter->common.db = NULL;
	dns_db_attach(db, &sdlziter->common.db);
	sdlziter->common.relative_names = ((options & DNS_DB_RELATIVENAMES) !=
					   0);
	sdlziter->common.magic = DNS_DBITERATOR_MAGIC;
	ISC_LIST_INIT(sdlziter->nodelist);
	sdlziter->current = NULL;
	sdlziter->origin = NULL;

	/* make sure strings are always lowercase */
	isc_ascii_strtolower(zonestr);

	MAYBE_LOCK(sdlz->dlzimp);
	result = sdlz->dlzimp->methods->allnodes(
		zonestr, sdlz->dlzimp->driverarg, sdlz->dbdata, sdlziter);
	MAYBE_UNLOCK(sdlz->dlzimp);
	if (result != ISC_R_SUCCESS) {
		dns_dbiterator_t *iter = &sdlziter->common;
		dbiterator_destroy(&iter DNS__DB_FILELINE);
		return result;
	}

	if (sdlziter->origin != NULL) {
		ISC_LIST_UNLINK(sdlziter->nodelist, sdlziter->origin, link);
		ISC_LIST_PREPEND(sdlziter->nodelist, sdlziter->origin, link);
	}

	*iteratorp = (dns_dbiterator_t *)sdlziter;

	return ISC_R_SUCCESS;
}

static isc_result_t
findrdataset(dns_db_t *db, dns_dbnode_t *node, dns_dbversion_t *version,
	     dns_rdatatype_t type, dns_rdatatype_t covers, isc_stdtime_t now,
	     dns_rdataset_t *rdataset,
	     dns_rdataset_t *sigrdataset DNS__DB_FLARG) {
	REQUIRE(VALID_SDLZNODE(node));
	dns_rdatalist_t *list;
	dns_sdlznode_t *sdlznode = (dns_sdlznode_t *)node;

	UNUSED(db);
	UNUSED(version);
	UNUSED(covers);
	UNUSED(now);
	UNUSED(sigrdataset);

	if (type == dns_rdatatype_sig || type == dns_rdatatype_rrsig) {
		return ISC_R_NOTIMPLEMENTED;
	}

	list = ISC_LIST_HEAD(sdlznode->lists);
	while (list != NULL) {
		if (list->type == type) {
			break;
		}
		list = ISC_LIST_NEXT(list, link);
	}
	if (list == NULL) {
		return ISC_R_NOTFOUND;
	}

	list_tordataset(list, db, node, rdataset);

	return ISC_R_SUCCESS;
}

static isc_result_t
findext(dns_db_t *db, const dns_name_t *name, dns_dbversion_t *version,
	dns_rdatatype_t type, unsigned int options, isc_stdtime_t now,
	dns_dbnode_t **nodep, dns_name_t *foundname,
	dns_clientinfomethods_t *methods, dns_clientinfo_t *clientinfo,
	dns_rdataset_t *rdataset, dns_rdataset_t *sigrdataset DNS__DB_FLARG) {
	dns_sdlz_db_t *sdlz = (dns_sdlz_db_t *)db;
	dns_dbnode_t *node = NULL;
	dns_fixedname_t fname;
	dns_rdataset_t xrdataset;
	dns_name_t *xname;
	unsigned int nlabels, olabels;
	isc_result_t result;
	unsigned int i;

	REQUIRE(VALID_SDLZDB(sdlz));
	REQUIRE(nodep == NULL || *nodep == NULL);
	REQUIRE(version == NULL || version == (void *)&sdlz->dummy_version ||
		version == sdlz->future_version);

	UNUSED(sdlz);

	if (!dns_name_issubdomain(name, &db->origin)) {
		return DNS_R_NXDOMAIN;
	}

	olabels = dns_name_countlabels(&db->origin);
	nlabels = dns_name_countlabels(name);

	xname = dns_fixedname_initname(&fname);

	if (rdataset == NULL) {
		dns_rdataset_init(&xrdataset);
		rdataset = &xrdataset;
	}

	result = DNS_R_NXDOMAIN;

	/*
	 * If we're not walking down searching for zone
	 * cuts, we can cut straight to the chase
	 */
	if ((options & DNS_DBFIND_NOZONECUT) != 0) {
		i = nlabels;
		goto search;
	}

	for (i = olabels; i <= nlabels; i++) {
	search:
		/*
		 * Look up the next label.
		 */
		dns_name_getlabelsequence(name, nlabels - i, i, xname);
		result = getnodedata(db, xname, false, options, methods,
				     clientinfo, &node);
		if (result == ISC_R_NOTFOUND) {
			result = DNS_R_NXDOMAIN;
			continue;
		} else if (result != ISC_R_SUCCESS) {
			break;
		}

		/*
		 * Look for a DNAME at the current label, unless this is
		 * the qname.
		 */
		if (i < nlabels) {
			result = findrdataset(
				db, node, version, dns_rdatatype_dname, 0, now,
				rdataset, sigrdataset DNS__DB_FLARG_PASS);
			if (result == ISC_R_SUCCESS) {
				result = DNS_R_DNAME;
				break;
			}
		}

		/*
		 * Look for an NS at the current label, unless this is the
		 * origin, glue is ok, or there are known to be no zone cuts.
		 */
		if (i != olabels && (options & DNS_DBFIND_GLUEOK) == 0 &&
		    (options & DNS_DBFIND_NOZONECUT) == 0)
		{
			result = findrdataset(
				db, node, version, dns_rdatatype_ns, 0, now,
				rdataset, sigrdataset DNS__DB_FLARG_PASS);

			if (result == ISC_R_SUCCESS && i == nlabels &&
			    type == dns_rdatatype_any)
			{
				result = DNS_R_ZONECUT;
				dns_rdataset_disassociate(rdataset);
				if (sigrdataset != NULL &&
				    dns_rdataset_isassociated(sigrdataset))
				{
					dns_rdataset_disassociate(sigrdataset);
				}
				break;
			} else if (result == ISC_R_SUCCESS) {
				result = DNS_R_DELEGATION;
				break;
			}
		}

		/*
		 * If the current name is not the qname, add another label
		 * and try again.
		 */
		if (i < nlabels) {
			detachnode(db, &node DNS__DB_FLARG_PASS);
			node = NULL;
			continue;
		}

		/*
		 * If we're looking for ANY, we're done.
		 */
		if (type == dns_rdatatype_any) {
			result = ISC_R_SUCCESS;
			break;
		}

		/*
		 * Look for the qtype.
		 */
		result = findrdataset(db, node, version, type, 0, now, rdataset,
				      sigrdataset DNS__DB_FLARG_PASS);
		if (result == ISC_R_SUCCESS) {
			break;
		}

		/*
		 * Look for a CNAME
		 */
		if (type != dns_rdatatype_cname) {
			result = findrdataset(
				db, node, version, dns_rdatatype_cname, 0, now,
				rdataset, sigrdataset DNS__DB_FLARG_PASS);
			if (result == ISC_R_SUCCESS) {
				result = DNS_R_CNAME;
				break;
			}
		}

		result = DNS_R_NXRRSET;
		break;
	}

	if (rdataset == &xrdataset && dns_rdataset_isassociated(rdataset)) {
		dns_rdataset_disassociate(rdataset);
	}

	if (foundname != NULL) {
		dns_name_copy(xname, foundname);
	}

	if (nodep != NULL) {
		*nodep = node;
	} else if (node != NULL) {
		detachnode(db, &node DNS__DB_FLARG_PASS);
	}

	return result;
}

static isc_result_t
find(dns_db_t *db, const dns_name_t *name, dns_dbversion_t *version,
     dns_rdatatype_t type, unsigned int options, isc_stdtime_t now,
     dns_dbnode_t **nodep, dns_name_t *foundname, dns_rdataset_t *rdataset,
     dns_rdataset_t *sigrdataset DNS__DB_FLARG) {
	return findext(db, name, version, type, options, now, nodep, foundname,
		       NULL, NULL, rdataset, sigrdataset DNS__DB_FLARG_PASS);
}

static isc_result_t
allrdatasets(dns_db_t *db, dns_dbnode_t *node, dns_dbversion_t *version,
	     unsigned int options, isc_stdtime_t now,
	     dns_rdatasetiter_t **iteratorp DNS__DB_FLARG) {
	dns_sdlz_db_t *sdlz = (dns_sdlz_db_t *)db;
	sdlz_rdatasetiter_t *iterator;

	REQUIRE(VALID_SDLZDB(sdlz));

	REQUIRE(version == NULL || version == (void *)&sdlz->dummy_version ||
		version == sdlz->future_version);

	UNUSED(version);
	UNUSED(now);

	iterator = isc_mem_get(db->mctx, sizeof(sdlz_rdatasetiter_t));

	iterator->common.magic = DNS_RDATASETITER_MAGIC;
	iterator->common.methods = &rdatasetiter_methods;
	iterator->common.db = db;
	iterator->common.node = NULL;
	attachnode(db, node, &iterator->common.node DNS__DB_FLARG_PASS);
	iterator->common.version = version;
	iterator->common.options = options;
	iterator->common.now = now;

	*iteratorp = (dns_rdatasetiter_t *)iterator;

	return ISC_R_SUCCESS;
}

static isc_result_t
modrdataset(dns_db_t *db, dns_dbnode_t *node, dns_dbversion_t *version,
	    dns_rdataset_t *rdataset, unsigned int options,
	    dns_sdlzmodrdataset_t mod_function) {
	dns_sdlz_db_t *sdlz = (dns_sdlz_db_t *)db;
	dns_master_style_t *style = NULL;
	isc_result_t result;
	isc_buffer_t *buffer = NULL;
	isc_mem_t *mctx;
	dns_sdlznode_t *sdlznode;
	char *rdatastr = NULL;
	char name[DNS_NAME_MAXTEXT + 1];

	REQUIRE(VALID_SDLZDB(sdlz));

	if (mod_function == NULL) {
		return ISC_R_NOTIMPLEMENTED;
	}

	sdlznode = (dns_sdlznode_t *)node;

	UNUSED(options);

	dns_name_format(sdlznode->name, name, sizeof(name));

	mctx = sdlz->common.mctx;

	isc_buffer_allocate(mctx, &buffer, 1024);

	result = dns_master_stylecreate(&style, 0, 0, 0, 0, 0, 0, 1, 0xffffffff,
					mctx);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	result = dns_master_rdatasettotext(sdlznode->name, rdataset, style,
					   NULL, buffer);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	if (isc_buffer_usedlength(buffer) < 1) {
		result = ISC_R_BADADDRESSFORM;
		goto cleanup;
	}

	rdatastr = isc_buffer_base(buffer);
	if (rdatastr == NULL) {
		result = ISC_R_NOMEMORY;
		goto cleanup;
	}
	rdatastr[isc_buffer_usedlength(buffer) - 1] = 0;

	MAYBE_LOCK(sdlz->dlzimp);
	result = mod_function(name, rdatastr, sdlz->dlzimp->driverarg,
			      sdlz->dbdata, version);
	MAYBE_UNLOCK(sdlz->dlzimp);

cleanup:
	isc_buffer_free(&buffer);
	if (style != NULL) {
		dns_master_styledestroy(&style, mctx);
	}

	return result;
}

static isc_result_t
addrdataset(dns_db_t *db, dns_dbnode_t *node, dns_dbversion_t *version,
	    isc_stdtime_t now, dns_rdataset_t *rdataset, unsigned int options,
	    dns_rdataset_t *addedrdataset DNS__DB_FLARG) {
	dns_sdlz_db_t *sdlz = (dns_sdlz_db_t *)db;
	isc_result_t result;

	UNUSED(now);
	UNUSED(addedrdataset);
	REQUIRE(VALID_SDLZDB(sdlz));

	if (sdlz->dlzimp->methods->addrdataset == NULL) {
		return ISC_R_NOTIMPLEMENTED;
	}

	result = modrdataset(db, node, version, rdataset, options,
			     sdlz->dlzimp->methods->addrdataset);
	return result;
}

static isc_result_t
subtractrdataset(dns_db_t *db, dns_dbnode_t *node, dns_dbversion_t *version,
		 dns_rdataset_t *rdataset, unsigned int options,
		 dns_rdataset_t *newrdataset DNS__DB_FLARG) {
	dns_sdlz_db_t *sdlz = (dns_sdlz_db_t *)db;
	isc_result_t result;

	UNUSED(newrdataset);
	REQUIRE(VALID_SDLZDB(sdlz));

	if (sdlz->dlzimp->methods->subtractrdataset == NULL) {
		return ISC_R_NOTIMPLEMENTED;
	}

	result = modrdataset(db, node, version, rdataset, options,
			     sdlz->dlzimp->methods->subtractrdataset);
	return result;
}

static isc_result_t
deleterdataset(dns_db_t *db, dns_dbnode_t *node, dns_dbversion_t *version,
	       dns_rdatatype_t type, dns_rdatatype_t covers DNS__DB_FLARG) {
	dns_sdlz_db_t *sdlz = (dns_sdlz_db_t *)db;
	char name[DNS_NAME_MAXTEXT + 1];
	char b_type[DNS_RDATATYPE_FORMATSIZE];
	dns_sdlznode_t *sdlznode;
	isc_result_t result;

	UNUSED(covers);

	REQUIRE(VALID_SDLZDB(sdlz));

	if (sdlz->dlzimp->methods->delrdataset == NULL) {
		return ISC_R_NOTIMPLEMENTED;
	}

	sdlznode = (dns_sdlznode_t *)node;
	dns_name_format(sdlznode->name, name, sizeof(name));
	dns_rdatatype_format(type, b_type, sizeof(b_type));

	MAYBE_LOCK(sdlz->dlzimp);
	result = sdlz->dlzimp->methods->delrdataset(
		name, b_type, sdlz->dlzimp->driverarg, sdlz->dbdata, version);
	MAYBE_UNLOCK(sdlz->dlzimp);

	return result;
}

static bool
issecure(dns_db_t *db) {
	UNUSED(db);

	return false;
}

static unsigned int
nodecount(dns_db_t *db, dns_dbtree_t tree) {
	UNUSED(db);
	UNUSED(tree);

	return 0;
}

static void
setloop(dns_db_t *db, isc_loop_t *loop) {
	UNUSED(db);
	UNUSED(loop);
}

/*
 * getoriginnode() is used by the update code to find the
 * dns_rdatatype_dnskey record for a zone
 */
static isc_result_t
getoriginnode(dns_db_t *db, dns_dbnode_t **nodep DNS__DB_FLARG) {
	dns_sdlz_db_t *sdlz = (dns_sdlz_db_t *)db;
	isc_result_t result;

	REQUIRE(VALID_SDLZDB(sdlz));
	if (sdlz->dlzimp->methods->newversion == NULL) {
		return ISC_R_NOTIMPLEMENTED;
	}

	result = getnodedata(db, &sdlz->common.origin, false, 0, NULL, NULL,
			     nodep);
	if (result != ISC_R_SUCCESS) {
		sdlz_log(ISC_LOG_ERROR, "sdlz getoriginnode failed: %s",
			 isc_result_totext(result));
	}
	return result;
}

static dns_dbmethods_t sdlzdb_methods = {
	.destroy = destroy,
	.currentversion = currentversion,
	.newversion = newversion,
	.attachversion = attachversion,
	.closeversion = closeversion,
	.findnode = findnode,
	.find = find,
	.attachnode = attachnode,
	.detachnode = detachnode,
	.createiterator = createiterator,
	.findrdataset = findrdataset,
	.allrdatasets = allrdatasets,
	.addrdataset = addrdataset,
	.subtractrdataset = subtractrdataset,
	.deleterdataset = deleterdataset,
	.issecure = issecure,
	.nodecount = nodecount,
	.setloop = setloop,
	.getoriginnode = getoriginnode,
	.findnodeext = findnodeext,
	.findext = findext,
};

/*
 * Database Iterator Methods.  These methods were "borrowed" from the SDB
 * driver interface.  See the SDB driver interface documentation for more info.
 */

static void
dbiterator_destroy(dns_dbiterator_t **iteratorp DNS__DB_FLARG) {
	sdlz_dbiterator_t *sdlziter = (sdlz_dbiterator_t *)(*iteratorp);
	dns_sdlz_db_t *sdlz = (dns_sdlz_db_t *)sdlziter->common.db;

	while (!ISC_LIST_EMPTY(sdlziter->nodelist)) {
		dns_sdlznode_t *node;
		node = ISC_LIST_HEAD(sdlziter->nodelist);
		ISC_LIST_UNLINK(sdlziter->nodelist, node, link);
		isc_refcount_decrementz(&node->references);
		destroynode(node);
	}

	dns_db_detach(&sdlziter->common.db);
	isc_mem_put(sdlz->common.mctx, sdlziter, sizeof(sdlz_dbiterator_t));

	*iteratorp = NULL;
}

static isc_result_t
dbiterator_first(dns_dbiterator_t *iterator DNS__DB_FLARG) {
	sdlz_dbiterator_t *sdlziter = (sdlz_dbiterator_t *)iterator;

	sdlziter->current = ISC_LIST_HEAD(sdlziter->nodelist);
	if (sdlziter->current == NULL) {
		return ISC_R_NOMORE;
	} else {
		return ISC_R_SUCCESS;
	}
}

static isc_result_t
dbiterator_last(dns_dbiterator_t *iterator DNS__DB_FLARG) {
	sdlz_dbiterator_t *sdlziter = (sdlz_dbiterator_t *)iterator;

	sdlziter->current = ISC_LIST_TAIL(sdlziter->nodelist);
	if (sdlziter->current == NULL) {
		return ISC_R_NOMORE;
	} else {
		return ISC_R_SUCCESS;
	}
}

static isc_result_t
dbiterator_seek(dns_dbiterator_t *iterator,
		const dns_name_t *name DNS__DB_FLARG) {
	sdlz_dbiterator_t *sdlziter = (sdlz_dbiterator_t *)iterator;

	sdlziter->current = ISC_LIST_HEAD(sdlziter->nodelist);
	while (sdlziter->current != NULL) {
		if (dns_name_equal(sdlziter->current->name, name)) {
			return ISC_R_SUCCESS;
		}
		sdlziter->current = ISC_LIST_NEXT(sdlziter->current, link);
	}
	return ISC_R_NOTFOUND;
}

static isc_result_t
dbiterator_prev(dns_dbiterator_t *iterator DNS__DB_FLARG) {
	sdlz_dbiterator_t *sdlziter = (sdlz_dbiterator_t *)iterator;

	sdlziter->current = ISC_LIST_PREV(sdlziter->current, link);
	if (sdlziter->current == NULL) {
		return ISC_R_NOMORE;
	} else {
		return ISC_R_SUCCESS;
	}
}

static isc_result_t
dbiterator_next(dns_dbiterator_t *iterator DNS__DB_FLARG) {
	sdlz_dbiterator_t *sdlziter = (sdlz_dbiterator_t *)iterator;

	sdlziter->current = ISC_LIST_NEXT(sdlziter->current, link);
	if (sdlziter->current == NULL) {
		return ISC_R_NOMORE;
	} else {
		return ISC_R_SUCCESS;
	}
}

static isc_result_t
dbiterator_current(dns_dbiterator_t *iterator, dns_dbnode_t **nodep,
		   dns_name_t *name DNS__DB_FLARG) {
	sdlz_dbiterator_t *sdlziter = (sdlz_dbiterator_t *)iterator;

	attachnode(iterator->db, sdlziter->current, nodep DNS__DB_FLARG_PASS);
	if (name != NULL) {
		dns_name_copy(sdlziter->current->name, name);
		return ISC_R_SUCCESS;
	}
	return ISC_R_SUCCESS;
}

static isc_result_t
dbiterator_pause(dns_dbiterator_t *iterator) {
	UNUSED(iterator);
	return ISC_R_SUCCESS;
}

static isc_result_t
dbiterator_origin(dns_dbiterator_t *iterator, dns_name_t *name) {
	UNUSED(iterator);
	dns_name_copy(dns_rootname, name);
	return ISC_R_SUCCESS;
}

/*
 * Rdataset Methods. These methods were "borrowed" from the SDB driver
 * interface.  See the SDB driver interface documentation for more info.
 */

static void
disassociate(dns_rdataset_t *rdataset DNS__DB_FLARG) {
	dns_dbnode_t *node = rdataset->rdlist.node;
	dns_sdlznode_t *sdlznode = (dns_sdlznode_t *)node;
	dns_db_t *db = (dns_db_t *)sdlznode->sdlz;

	detachnode(db, &node DNS__DB_FLARG_PASS);
	dns_rdatalist_disassociate(rdataset DNS__DB_FLARG_PASS);
}

static void
rdataset_clone(dns_rdataset_t *source, dns_rdataset_t *target DNS__DB_FLARG) {
	dns_dbnode_t *node = source->rdlist.node;
	dns_sdlznode_t *sdlznode = (dns_sdlznode_t *)node;
	dns_db_t *db = (dns_db_t *)sdlznode->sdlz;

	dns_rdatalist_clone(source, target DNS__DB_FLARG_PASS);
	attachnode(db, node, &target->rdlist.node DNS__DB_FLARG_PASS);
}

static dns_rdatasetmethods_t rdataset_methods = {
	.disassociate = disassociate,
	.first = dns_rdatalist_first,
	.next = dns_rdatalist_next,
	.current = dns_rdatalist_current,
	.clone = rdataset_clone,
	.count = dns_rdatalist_count,
	.addnoqname = dns_rdatalist_addnoqname,
	.getnoqname = dns_rdatalist_getnoqname,
};

static void
list_tordataset(dns_rdatalist_t *rdatalist, dns_db_t *db, dns_dbnode_t *node,
		dns_rdataset_t *rdataset) {
	/*
	 * The sdlz rdataset is an rdatalist, but additionally holds
	 * a database node reference.
	 */

	dns_rdatalist_tordataset(rdatalist, rdataset);
	rdataset->methods = &rdataset_methods;
	dns_db_attachnode(db, node, &rdataset->rdlist.node);
}

/*
 * SDLZ core methods. This is the core of the new DLZ functionality.
 */

/*%
 * Build a 'bind' database driver structure to be returned by
 * either the find zone or the allow zone transfer method.
 * This method is only available in this source file, it is
 * not made available anywhere else.
 */

static isc_result_t
dns_sdlzcreateDBP(isc_mem_t *mctx, void *driverarg, void *dbdata,
		  const dns_name_t *name, dns_rdataclass_t rdclass,
		  dns_db_t **dbp) {
	dns_sdlz_db_t *sdlzdb;
	dns_sdlzimplementation_t *imp;

	/* check that things are as we expect */
	REQUIRE(dbp != NULL && *dbp == NULL);
	REQUIRE(name != NULL);

	imp = (dns_sdlzimplementation_t *)driverarg;

	/* allocate and zero memory for driver structure */
	sdlzdb = isc_mem_get(mctx, sizeof(*sdlzdb));

	*sdlzdb = (dns_sdlz_db_t) {
		.dlzimp = imp,
		.common = { .methods = &sdlzdb_methods,
			.rdclass = rdclass, },
			.dbdata = dbdata,
	};

	/* initialize and set origin */
	dns_name_init(&sdlzdb->common.origin, NULL);
	dns_name_dupwithoffsets(name, mctx, &sdlzdb->common.origin);

	isc_refcount_init(&sdlzdb->common.references, 1);

	/* attach to the memory context */
	isc_mem_attach(mctx, &sdlzdb->common.mctx);

	/* mark structure as valid */
	sdlzdb->common.magic = DNS_DB_MAGIC;
	sdlzdb->common.impmagic = SDLZDB_MAGIC;
	*dbp = (dns_db_t *)sdlzdb;

	return ISC_R_SUCCESS;
}

static isc_result_t
dns_sdlzallowzonexfr(void *driverarg, void *dbdata, isc_mem_t *mctx,
		     dns_rdataclass_t rdclass, const dns_name_t *name,
		     const isc_sockaddr_t *clientaddr, dns_db_t **dbp) {
	isc_buffer_t b;
	isc_buffer_t b2;
	char namestr[DNS_NAME_MAXTEXT + 1];
	char clientstr[(sizeof "xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:255.255.255."
			       "255") +
		       1];
	isc_netaddr_t netaddr;
	isc_result_t result;
	dns_sdlzimplementation_t *imp;

	/*
	 * Perform checks to make sure data is as we expect it to be.
	 */
	REQUIRE(driverarg != NULL);
	REQUIRE(name != NULL);
	REQUIRE(clientaddr != NULL);
	REQUIRE(dbp != NULL && *dbp == NULL);

	imp = (dns_sdlzimplementation_t *)driverarg;

	/* Convert DNS name to ascii text */
	isc_buffer_init(&b, namestr, sizeof(namestr));
	result = dns_name_totext(name, DNS_NAME_OMITFINALDOT, &b);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	isc_buffer_putuint8(&b, 0);

	/* convert client address to ascii text */
	isc_buffer_init(&b2, clientstr, sizeof(clientstr));
	isc_netaddr_fromsockaddr(&netaddr, clientaddr);
	result = isc_netaddr_totext(&netaddr, &b2);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	isc_buffer_putuint8(&b2, 0);

	/* make sure strings are always lowercase */
	isc_ascii_strtolower(namestr);
	isc_ascii_strtolower(clientstr);

	/* Call SDLZ driver's find zone method */
	if (imp->methods->allowzonexfr != NULL) {
		isc_result_t rresult = ISC_R_SUCCESS;

		MAYBE_LOCK(imp);
		result = imp->methods->allowzonexfr(imp->driverarg, dbdata,
						    namestr, clientstr);
		MAYBE_UNLOCK(imp);
		/*
		 * if zone is supported and transfers are (or might be)
		 * allowed, build a 'bind' database driver
		 */
		if (result == ISC_R_SUCCESS || result == ISC_R_DEFAULT) {
			rresult = dns_sdlzcreateDBP(mctx, driverarg, dbdata,
						    name, rdclass, dbp);
		}
		if (rresult != ISC_R_SUCCESS) {
			result = rresult;
		}
		return result;
	}

	return ISC_R_NOTIMPLEMENTED;
}

static isc_result_t
dns_sdlzcreate(isc_mem_t *mctx, const char *dlzname, unsigned int argc,
	       char *argv[], void *driverarg, void **dbdata) {
	dns_sdlzimplementation_t *imp;
	isc_result_t result = ISC_R_NOTFOUND;

	/* Write debugging message to log */
	sdlz_log(ISC_LOG_DEBUG(2), "Loading SDLZ driver.");

	/*
	 * Performs checks to make sure data is as we expect it to be.
	 */
	REQUIRE(driverarg != NULL);
	REQUIRE(dlzname != NULL);
	REQUIRE(dbdata != NULL);
	UNUSED(mctx);

	imp = driverarg;

	/* If the create method exists, call it. */
	if (imp->methods->create != NULL) {
		MAYBE_LOCK(imp);
		result = imp->methods->create(dlzname, argc, argv,
					      imp->driverarg, dbdata);
		MAYBE_UNLOCK(imp);
	}

	/* Write debugging message to log */
	if (result == ISC_R_SUCCESS) {
		sdlz_log(ISC_LOG_DEBUG(2), "SDLZ driver loaded successfully.");
	} else {
		sdlz_log(ISC_LOG_ERROR, "SDLZ driver failed to load.");
	}

	return result;
}

static void
dns_sdlzdestroy(void *driverdata, void **dbdata) {
	dns_sdlzimplementation_t *imp;

	/* Write debugging message to log */
	sdlz_log(ISC_LOG_DEBUG(2), "Unloading SDLZ driver.");

	imp = driverdata;

	/* If the destroy method exists, call it. */
	if (imp->methods->destroy != NULL) {
		MAYBE_LOCK(imp);
		imp->methods->destroy(imp->driverarg, dbdata);
		MAYBE_UNLOCK(imp);
	}
}

static isc_result_t
dns_sdlzfindzone(void *driverarg, void *dbdata, isc_mem_t *mctx,
		 dns_rdataclass_t rdclass, const dns_name_t *name,
		 dns_clientinfomethods_t *methods, dns_clientinfo_t *clientinfo,
		 dns_db_t **dbp) {
	isc_buffer_t b;
	char namestr[DNS_NAME_MAXTEXT + 1];
	isc_result_t result;
	dns_sdlzimplementation_t *imp;

	/*
	 * Perform checks to make sure data is as we expect it to be.
	 */
	REQUIRE(driverarg != NULL);
	REQUIRE(name != NULL);
	REQUIRE(dbp != NULL && *dbp == NULL);

	imp = (dns_sdlzimplementation_t *)driverarg;

	/* Convert DNS name to ascii text */
	isc_buffer_init(&b, namestr, sizeof(namestr));
	result = dns_name_totext(name, DNS_NAME_OMITFINALDOT, &b);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	isc_buffer_putuint8(&b, 0);

	/* make sure strings are always lowercase */
	isc_ascii_strtolower(namestr);

	/* Call SDLZ driver's find zone method */
	MAYBE_LOCK(imp);
	result = imp->methods->findzone(imp->driverarg, dbdata, namestr,
					methods, clientinfo);
	MAYBE_UNLOCK(imp);

	/*
	 * if zone is supported build a 'bind' database driver
	 * structure to return
	 */
	if (result == ISC_R_SUCCESS) {
		result = dns_sdlzcreateDBP(mctx, driverarg, dbdata, name,
					   rdclass, dbp);
	}

	return result;
}

static isc_result_t
dns_sdlzconfigure(void *driverarg, void *dbdata, dns_view_t *view,
		  dns_dlzdb_t *dlzdb) {
	isc_result_t result;
	dns_sdlzimplementation_t *imp;

	REQUIRE(driverarg != NULL);

	imp = (dns_sdlzimplementation_t *)driverarg;

	/* Call SDLZ driver's configure method */
	if (imp->methods->configure != NULL) {
		MAYBE_LOCK(imp);
		result = imp->methods->configure(view, dlzdb, imp->driverarg,
						 dbdata);
		MAYBE_UNLOCK(imp);
	} else {
		result = ISC_R_SUCCESS;
	}

	return result;
}

static bool
dns_sdlzssumatch(const dns_name_t *signer, const dns_name_t *name,
		 const isc_netaddr_t *tcpaddr, dns_rdatatype_t type,
		 const dst_key_t *key, void *driverarg, void *dbdata) {
	dns_sdlzimplementation_t *imp;
	char b_signer[DNS_NAME_FORMATSIZE];
	char b_name[DNS_NAME_FORMATSIZE];
	char b_addr[ISC_NETADDR_FORMATSIZE];
	char b_type[DNS_RDATATYPE_FORMATSIZE];
	char b_key[DST_KEY_FORMATSIZE];
	isc_buffer_t *tkey_token = NULL;
	isc_region_t token_region = { NULL, 0 };
	uint32_t token_len = 0;
	bool ret;

	REQUIRE(driverarg != NULL);

	imp = (dns_sdlzimplementation_t *)driverarg;
	if (imp->methods->ssumatch == NULL) {
		return false;
	}

	/*
	 * Format the request elements. sdlz operates on strings, not
	 * structures
	 */
	if (signer != NULL) {
		dns_name_format(signer, b_signer, sizeof(b_signer));
	} else {
		b_signer[0] = 0;
	}

	dns_name_format(name, b_name, sizeof(b_name));

	if (tcpaddr != NULL) {
		isc_netaddr_format(tcpaddr, b_addr, sizeof(b_addr));
	} else {
		b_addr[0] = 0;
	}

	dns_rdatatype_format(type, b_type, sizeof(b_type));

	if (key != NULL) {
		dst_key_format(key, b_key, sizeof(b_key));
		tkey_token = dst_key_tkeytoken(key);
	} else {
		b_key[0] = 0;
	}

	if (tkey_token != NULL) {
		isc_buffer_region(tkey_token, &token_region);
		token_len = token_region.length;
	}

	MAYBE_LOCK(imp);
	ret = imp->methods->ssumatch(b_signer, b_name, b_addr, b_type, b_key,
				     token_len,
				     token_len != 0 ? token_region.base : NULL,
				     imp->driverarg, dbdata);
	MAYBE_UNLOCK(imp);
	return ret;
}

static dns_dlzmethods_t sdlzmethods = { dns_sdlzcreate,	   dns_sdlzdestroy,
					dns_sdlzfindzone,  dns_sdlzallowzonexfr,
					dns_sdlzconfigure, dns_sdlzssumatch };

/*
 * Public functions.
 */

isc_result_t
dns_sdlz_putrr(dns_sdlzlookup_t *lookup, const char *type, dns_ttl_t ttl,
	       const char *data) {
	dns_rdatalist_t *rdatalist;
	dns_rdata_t *rdata;
	dns_rdatatype_t typeval;
	isc_consttextregion_t r;
	isc_buffer_t b;
	isc_buffer_t *rdatabuf = NULL;
	isc_lex_t *lex;
	isc_result_t result;
	unsigned int size;
	isc_mem_t *mctx;
	const dns_name_t *origin;

	REQUIRE(VALID_SDLZLOOKUP(lookup));
	REQUIRE(type != NULL);
	REQUIRE(data != NULL);

	mctx = lookup->sdlz->common.mctx;

	r.base = type;
	r.length = strlen(type);
	result = dns_rdatatype_fromtext(&typeval, (void *)&r);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	rdatalist = ISC_LIST_HEAD(lookup->lists);
	while (rdatalist != NULL) {
		if (rdatalist->type == typeval) {
			break;
		}
		rdatalist = ISC_LIST_NEXT(rdatalist, link);
	}

	if (rdatalist == NULL) {
		rdatalist = isc_mem_get(mctx, sizeof(dns_rdatalist_t));
		dns_rdatalist_init(rdatalist);
		rdatalist->rdclass = lookup->sdlz->common.rdclass;
		rdatalist->type = typeval;
		rdatalist->ttl = ttl;
		ISC_LIST_APPEND(lookup->lists, rdatalist, link);
	} else if (rdatalist->ttl > ttl) {
		/*
		 * BIND9 doesn't enforce all RRs in an RRset
		 * having the same TTL, as per RFC 2136,
		 * section 7.12. If a DLZ backend has
		 * different TTLs, then the best
		 * we can do is return the lowest.
		 */
		rdatalist->ttl = ttl;
	}

	rdata = isc_mem_get(mctx, sizeof(dns_rdata_t));
	dns_rdata_init(rdata);

	if ((lookup->sdlz->dlzimp->flags & DNS_SDLZFLAG_RELATIVERDATA) != 0) {
		origin = &lookup->sdlz->common.origin;
	} else {
		origin = dns_rootname;
	}

	lex = NULL;
	isc_lex_create(mctx, 64, &lex);

	size = initial_size(data);
	do {
		isc_buffer_constinit(&b, data, strlen(data));
		isc_buffer_add(&b, strlen(data));

		result = isc_lex_openbuffer(lex, &b);
		if (result != ISC_R_SUCCESS) {
			goto failure;
		}

		rdatabuf = NULL;
		isc_buffer_allocate(mctx, &rdatabuf, size);

		result = dns_rdata_fromtext(rdata, rdatalist->rdclass,
					    rdatalist->type, lex, origin, false,
					    mctx, rdatabuf, &lookup->callbacks);
		if (result != ISC_R_SUCCESS) {
			isc_buffer_free(&rdatabuf);
		}
		if (size >= 65535) {
			break;
		}
		size *= 2;
		if (size >= 65535) {
			size = 65535;
		}
	} while (result == ISC_R_NOSPACE);

	if (result != ISC_R_SUCCESS) {
		result = DNS_R_SERVFAIL;
		goto failure;
	}

	ISC_LIST_APPEND(rdatalist->rdata, rdata, link);
	ISC_LIST_APPEND(lookup->buffers, rdatabuf, link);

	if (lex != NULL) {
		isc_lex_destroy(&lex);
	}

	return ISC_R_SUCCESS;

failure:
	if (rdatabuf != NULL) {
		isc_buffer_free(&rdatabuf);
	}
	if (lex != NULL) {
		isc_lex_destroy(&lex);
	}
	isc_mem_put(mctx, rdata, sizeof(dns_rdata_t));

	return result;
}

isc_result_t
dns_sdlz_putnamedrr(dns_sdlzallnodes_t *allnodes, const char *name,
		    const char *type, dns_ttl_t ttl, const char *data) {
	dns_name_t *newname;
	const dns_name_t *origin;
	dns_fixedname_t fnewname;
	dns_sdlz_db_t *sdlz = (dns_sdlz_db_t *)allnodes->common.db;
	dns_sdlznode_t *sdlznode;
	isc_mem_t *mctx = sdlz->common.mctx;
	isc_buffer_t b;
	isc_result_t result;

	newname = dns_fixedname_initname(&fnewname);

	if ((sdlz->dlzimp->flags & DNS_SDLZFLAG_RELATIVERDATA) != 0) {
		origin = &sdlz->common.origin;
	} else {
		origin = dns_rootname;
	}
	isc_buffer_constinit(&b, name, strlen(name));
	isc_buffer_add(&b, strlen(name));

	result = dns_name_fromtext(newname, &b, origin, 0, NULL);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	if (allnodes->common.relative_names) {
		/* All names are relative to the root */
		unsigned int nlabels = dns_name_countlabels(newname);
		dns_name_getlabelsequence(newname, 0, nlabels - 1, newname);
	}

	sdlznode = ISC_LIST_HEAD(allnodes->nodelist);
	if (sdlznode == NULL || !dns_name_equal(sdlznode->name, newname)) {
		sdlznode = NULL;
		result = createnode(sdlz, &sdlznode);
		if (result != ISC_R_SUCCESS) {
			return result;
		}
		sdlznode->name = isc_mem_get(mctx, sizeof(dns_name_t));
		dns_name_init(sdlznode->name, NULL);
		dns_name_dup(newname, mctx, sdlznode->name);
		ISC_LIST_PREPEND(allnodes->nodelist, sdlznode, link);
		if (allnodes->origin == NULL &&
		    dns_name_equal(newname, &sdlz->common.origin))
		{
			allnodes->origin = sdlznode;
		}
	}
	return dns_sdlz_putrr(sdlznode, type, ttl, data);
}

isc_result_t
dns_sdlz_putsoa(dns_sdlzlookup_t *lookup, const char *mname, const char *rname,
		uint32_t serial) {
	char str[2 * DNS_NAME_MAXTEXT + 5 * (sizeof("2147483647")) + 7];
	int n;

	REQUIRE(mname != NULL);
	REQUIRE(rname != NULL);

	n = snprintf(str, sizeof str, "%s %s %u %u %u %u %u", mname, rname,
		     serial, SDLZ_DEFAULT_REFRESH, SDLZ_DEFAULT_RETRY,
		     SDLZ_DEFAULT_EXPIRE, SDLZ_DEFAULT_MINIMUM);
	if (n >= (int)sizeof(str) || n < 0) {
		return ISC_R_NOSPACE;
	}
	return dns_sdlz_putrr(lookup, "SOA", SDLZ_DEFAULT_TTL, str);
}

isc_result_t
dns_sdlzregister(const char *drivername, const dns_sdlzmethods_t *methods,
		 void *driverarg, unsigned int flags, isc_mem_t *mctx,
		 dns_sdlzimplementation_t **sdlzimp) {
	dns_sdlzimplementation_t *imp;
	isc_result_t result;

	/*
	 * Performs checks to make sure data is as we expect it to be.
	 */
	REQUIRE(drivername != NULL);
	REQUIRE(methods != NULL);
	REQUIRE(methods->findzone != NULL);
	REQUIRE(methods->lookup != NULL);
	REQUIRE(mctx != NULL);
	REQUIRE(sdlzimp != NULL && *sdlzimp == NULL);
	REQUIRE((flags &
		 ~(DNS_SDLZFLAG_RELATIVEOWNER | DNS_SDLZFLAG_RELATIVERDATA |
		   DNS_SDLZFLAG_THREADSAFE)) == 0);

	/* Write debugging message to log */
	sdlz_log(ISC_LOG_DEBUG(2), "Registering SDLZ driver '%s'", drivername);

	/*
	 * Allocate memory for a sdlz_implementation object.  Error if
	 * we cannot.
	 */
	imp = isc_mem_get(mctx, sizeof(*imp));

	/* Store the data passed into this method */
	*imp = (dns_sdlzimplementation_t){
		.methods = methods,
		.driverarg = driverarg,
		.flags = flags,
	};

	/* attach the new sdlz_implementation object to a memory context */
	isc_mem_attach(mctx, &imp->mctx);

	/*
	 * initialize the driver lock, error if we cannot
	 * (used if a driver does not support multiple threads)
	 */
	isc_mutex_init(&imp->driverlock);

	/*
	 * register the DLZ driver.  Pass in our "extra" sdlz information as
	 * a driverarg.  (that's why we stored the passed in driver arg in our
	 * sdlz_implementation structure)  Also, store the dlz_implementation
	 * structure in our sdlz_implementation.
	 */
	result = dns_dlzregister(drivername, &sdlzmethods, imp, mctx,
				 &imp->dlz_imp);

	/* if registration fails, cleanup and get outta here. */
	if (result != ISC_R_SUCCESS) {
		goto cleanup_mutex;
	}

	*sdlzimp = imp;

	return ISC_R_SUCCESS;

cleanup_mutex:
	/* destroy the driver lock, we don't need it anymore */
	isc_mutex_destroy(&imp->driverlock);

	/*
	 * return the memory back to the available memory pool and
	 * remove it from the memory context.
	 */
	isc_mem_putanddetach(&imp->mctx, imp, sizeof(*imp));
	return result;
}

void
dns_sdlzunregister(dns_sdlzimplementation_t **sdlzimp) {
	dns_sdlzimplementation_t *imp;

	/* Write debugging message to log */
	sdlz_log(ISC_LOG_DEBUG(2), "Unregistering SDLZ driver.");

	/*
	 * Performs checks to make sure data is as we expect it to be.
	 */
	REQUIRE(sdlzimp != NULL && *sdlzimp != NULL);

	imp = *sdlzimp;
	*sdlzimp = NULL;

	/* Unregister the DLZ driver implementation */
	dns_dlzunregister(&imp->dlz_imp);

	/* destroy the driver lock, we don't need it anymore */
	isc_mutex_destroy(&imp->driverlock);

	/*
	 * return the memory back to the available memory pool and
	 * remove it from the memory context.
	 */
	isc_mem_putanddetach(&imp->mctx, imp, sizeof(dns_sdlzimplementation_t));
}

isc_result_t
dns_sdlz_setdb(dns_dlzdb_t *dlzdatabase, dns_rdataclass_t rdclass,
	       const dns_name_t *name, dns_db_t **dbp) {
	isc_result_t result;

	result = dns_sdlzcreateDBP(dlzdatabase->mctx,
				   dlzdatabase->implementation->driverarg,
				   dlzdatabase->dbdata, name, rdclass, dbp);
	return result;
}
