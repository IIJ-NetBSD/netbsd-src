/*	$NetBSD: server.c,v 1.25 2025/07/17 19:01:43 christos Exp $	*/

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

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef HAVE_DNSTAP
#include <fstrm.h>
#endif

#include <isc/async.h>
#include <isc/attributes.h>
#include <isc/base64.h>
#include <isc/commandline.h>
#include <isc/dir.h>
#include <isc/file.h>
#include <isc/fips.h>
#include <isc/hash.h>
#include <isc/hex.h>
#include <isc/hmac.h>
#include <isc/httpd.h>
#include <isc/job.h>
#include <isc/lex.h>
#include <isc/loop.h>
#include <isc/meminfo.h>
#include <isc/netmgr.h>
#include <isc/nonce.h>
#include <isc/parseint.h>
#include <isc/portset.h>
#include <isc/refcount.h>
#include <isc/result.h>
#include <isc/signal.h>
#include <isc/siphash.h>
#include <isc/stats.h>
#include <isc/stdio.h>
#include <isc/string.h>
#include <isc/time.h>
#include <isc/timer.h>
#include <isc/util.h>

#include <dns/adb.h>
#include <dns/badcache.h>
#include <dns/cache.h>
#include <dns/catz.h>
#include <dns/db.h>
#include <dns/dispatch.h>
#include <dns/dlz.h>
#include <dns/dns64.h>
#include <dns/dnsrps.h>
#include <dns/dnssec.h>
#include <dns/dyndb.h>
#include <dns/fixedname.h>
#include <dns/forward.h>
#include <dns/geoip.h>
#include <dns/journal.h>
#include <dns/kasp.h>
#include <dns/keymgr.h>
#include <dns/keystore.h>
#include <dns/keytable.h>
#include <dns/keyvalues.h>
#include <dns/master.h>
#include <dns/masterdump.h>
#include <dns/nametree.h>
#include <dns/nsec3.h>
#include <dns/nta.h>
#include <dns/order.h>
#include <dns/peer.h>
#include <dns/private.h>
#include <dns/rbt.h>
#include <dns/rdataclass.h>
#include <dns/rdatalist.h>
#include <dns/rdataset.h>
#include <dns/rdatastruct.h>
#include <dns/resolver.h>
#include <dns/rootns.h>
#include <dns/rriterator.h>
#include <dns/secalg.h>
#include <dns/soa.h>
#include <dns/stats.h>
#include <dns/time.h>
#include <dns/tkey.h>
#include <dns/tsig.h>
#include <dns/ttl.h>
#include <dns/view.h>
#include <dns/zone.h>
#include <dns/zt.h>

#include <dst/dst.h>

#include <isccfg/check.h>
#include <isccfg/grammar.h>
#include <isccfg/kaspconf.h>
#include <isccfg/namedconf.h>

#include <ns/client.h>
#include <ns/hooks.h>
#include <ns/interfacemgr.h>
#include <ns/listenlist.h>

#include <named/config.h>
#include <named/control.h>
#if defined(HAVE_GEOIP2)
#include <named/geoip.h>
#endif /* HAVE_GEOIP2 */
#include <named/log.h>
#include <named/logconf.h>
#include <named/main.h>
#include <named/os.h>
#include <named/server.h>
#include <named/statschannel.h>
#include <named/tkeyconf.h>
#include <named/transportconf.h>
#include <named/tsigconf.h>
#include <named/zoneconf.h>
#ifdef HAVE_LIBSCF
#include <stdlib.h>

#include <named/smf_globals.h>
#endif /* ifdef HAVE_LIBSCF */

/* On DragonFly BSD the header does not provide jemalloc API */
#if defined(HAVE_MALLOC_NP_H) && !defined(__DragonFly__)
#include <malloc_np.h>
#define JEMALLOC_API_SUPPORTED 1
#elif defined(HAVE_JEMALLOC)
#include <jemalloc/jemalloc.h>
#define JEMALLOC_API_SUPPORTED 1
#endif

#ifdef HAVE_LMDB
#include <lmdb.h>
#define configure_newzones configure_newzones_db
#define dumpzone	   dumpzone_db
#else /* HAVE_LMDB */
#define configure_newzones configure_newzones_file
#define dumpzone	   dumpzone_file
#endif /* HAVE_LMDB */

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif /* ifndef SIZE_MAX */

#ifndef SIZE_AS_PERCENT
#define SIZE_AS_PERCENT ((size_t)-2)
#endif /* ifndef SIZE_AS_PERCENT */

/* RFC7828 defines timeout as 16-bit value specified in units of 100
 * milliseconds, so the maximum and minimum advertised and keepalive
 * timeouts are capped by the data type (it's ~109 minutes)
 */
#define MIN_INITIAL_TIMEOUT    UINT32_C(2500)	/* 2.5 seconds */
#define MAX_INITIAL_TIMEOUT    UINT32_C(120000) /* 2 minutes */
#define MIN_IDLE_TIMEOUT       UINT32_C(100)	/* 0.1 seconds */
#define MAX_IDLE_TIMEOUT       UINT32_C(120000) /* 2 minutes */
#define MIN_KEEPALIVE_TIMEOUT  UINT32_C(100)	/* 0.1 seconds */
#define MAX_KEEPALIVE_TIMEOUT  UINT32_C(UINT16_MAX * 100)
#define MIN_ADVERTISED_TIMEOUT UINT32_C(0) /* No minimum */
#define MAX_ADVERTISED_TIMEOUT UINT32_C(UINT16_MAX * 100)

/*%
 * Check an operation for failure.  Assumes that the function
 * using it has a 'result' variable and a 'cleanup' label.
 */
#define CHECK(op)                            \
	do {                                 \
		result = (op);               \
		if (result != ISC_R_SUCCESS) \
			goto cleanup;        \
	} while (0)

#define TCHECK(op)                               \
	do {                                     \
		tresult = (op);                  \
		if (tresult != ISC_R_SUCCESS) {  \
			isc_buffer_clear(*text); \
			goto cleanup;            \
		}                                \
	} while (0)

#define CHECKM(op, msg)                                                        \
	do {                                                                   \
		result = (op);                                                 \
		if (result != ISC_R_SUCCESS) {                                 \
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL, \
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,   \
				      "%s: %s", msg,                           \
				      isc_result_totext(result));              \
			goto cleanup;                                          \
		}                                                              \
	} while (0)

#define CHECKMF(op, msg, file)                                                 \
	do {                                                                   \
		result = (op);                                                 \
		if (result != ISC_R_SUCCESS) {                                 \
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL, \
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,   \
				      "%s '%s': %s", msg, file,                \
				      isc_result_totext(result));              \
			goto cleanup;                                          \
		}                                                              \
	} while (0)

#define CHECKFATAL(op, msg)                    \
	{                                      \
		result = (op);                 \
		if (result != ISC_R_SUCCESS) { \
			fatal(msg, result);    \
		}                              \
	}

/*%
 * Maximum ADB size for views that share a cache.  Use this limit to suppress
 * the total of memory footprint, which should be the main reason for sharing
 * a cache.  Only effective when a finite max-cache-size is specified.
 * This is currently defined to be 8MB.
 */
#define MAX_ADB_SIZE_FOR_CACHESHARE 8388608U

struct named_dispatch {
	isc_sockaddr_t addr;
	unsigned int dispatchgen;
	dns_dispatch_t *dispatch;
	ISC_LINK(struct named_dispatch) link;
};

struct named_cache {
	dns_cache_t *cache;
	dns_view_t *primaryview;
	bool needflush;
	bool adbsizeadjusted;
	dns_rdataclass_t rdclass;
	ISC_LINK(named_cache_t) link;
};

struct dumpcontext {
	isc_mem_t *mctx;
	bool dumpcache;
	bool dumpzones;
	bool dumpadb;
	bool dumpexpired;
	bool dumpfail;
	FILE *fp;
	ISC_LIST(struct viewlistentry) viewlist;
	struct viewlistentry *view;
	struct zonelistentry *zone;
	dns_dumpctx_t *mdctx;
	dns_db_t *db;
	dns_db_t *cache;
	isc_loop_t *loop;
	dns_dbversion_t *version;
};

struct viewlistentry {
	dns_view_t *view;
	ISC_LINK(struct viewlistentry) link;
	ISC_LIST(struct zonelistentry) zonelist;
};

struct zonelistentry {
	dns_zone_t *zone;
	ISC_LINK(struct zonelistentry) link;
};

/*%
 * Message-to-view matching context to run message signature validation
 * asynchronously.
 */
typedef struct matching_view_ctx {
	isc_netaddr_t *srcaddr;
	isc_netaddr_t *destaddr;
	dns_message_t *message;
	dns_aclenv_t *env;
	ns_server_t *sctx;
	isc_loop_t *loop;
	isc_job_cb cb;
	void *cbarg;
	isc_result_t *sigresult;
	isc_result_t *viewmatchresult;
	isc_result_t quota_result;
	dns_view_t **viewp;
	dns_view_t *view;
} matching_view_ctx_t;

/*%
 * Configuration context to retain for each view that allows
 * new zones to be added at runtime.
 */
typedef struct ns_cfgctx {
	isc_mem_t *mctx;
	cfg_parser_t *conf_parser;
	cfg_parser_t *add_parser;
	cfg_obj_t *config;
	cfg_obj_t *vconfig;
	cfg_obj_t *nzf_config;
	cfg_aclconfctx_t *actx;
} ns_cfgctx_t;

/*%
 * A function to write out added-zone configuration to the new_zone_file
 * specified in 'view'. Maybe called by delete_zoneconf().
 */
typedef isc_result_t (*nzfwriter_t)(const cfg_obj_t *config, dns_view_t *view);

/*%
 * Holds state information for the initial zone loading process.
 * Uses the isc_refcount structure to count the number of views
 * with pending zone loads, dereferencing as each view finishes.
 */
typedef struct {
	named_server_t *server;
	bool reconfig;
	isc_refcount_t refs;
} ns_zoneload_t;

typedef struct {
	named_server_t *server;
} catz_cb_data_t;

typedef struct catz_chgzone {
	isc_mem_t *mctx;
	dns_catz_entry_t *entry;
	dns_catz_zone_t *origin;
	dns_view_t *view;
	catz_cb_data_t *cbd;
	bool mod;
} catz_chgzone_t;

typedef struct catz_reconfig_data {
	dns_catz_zone_t *catz;
	const cfg_obj_t *config;
	catz_cb_data_t *cbd;
} catz_reconfig_data_t;

typedef enum {
	CATZ_ADDZONE,
	CATZ_MODZONE,
	CATZ_DELZONE,
} catz_type_t;

typedef struct {
	unsigned int magic;
#define DZARG_MAGIC ISC_MAGIC('D', 'z', 'a', 'r')
	isc_buffer_t **text;
	isc_result_t result;
} ns_dzarg_t;

typedef enum {
	MEMPROF_UNSUPPORTED = 0x00,
	MEMPROF_INACTIVE = 0x01,
	MEMPROF_FAILING = 0x02,
	MEMPROF_OFF = 0x03,
	MEMPROF_ON = 0x04,
} memprof_status;

static const char *memprof_status_text[] = {
	[MEMPROF_UNSUPPORTED] = "UNSUPPORTED",
	[MEMPROF_INACTIVE] = "INACTIVE",
	[MEMPROF_FAILING] = "FAILING",
	[MEMPROF_OFF] = "OFF",
	[MEMPROF_ON] = "ON",
};

/*
 * These zones should not leak onto the Internet.
 */
const char *empty_zones[] = {
	/* RFC 1918 */
	"10.IN-ADDR.ARPA", "16.172.IN-ADDR.ARPA", "17.172.IN-ADDR.ARPA",
	"18.172.IN-ADDR.ARPA", "19.172.IN-ADDR.ARPA", "20.172.IN-ADDR.ARPA",
	"21.172.IN-ADDR.ARPA", "22.172.IN-ADDR.ARPA", "23.172.IN-ADDR.ARPA",
	"24.172.IN-ADDR.ARPA", "25.172.IN-ADDR.ARPA", "26.172.IN-ADDR.ARPA",
	"27.172.IN-ADDR.ARPA", "28.172.IN-ADDR.ARPA", "29.172.IN-ADDR.ARPA",
	"30.172.IN-ADDR.ARPA", "31.172.IN-ADDR.ARPA", "168.192.IN-ADDR.ARPA",

	/* RFC 6598 */
	"64.100.IN-ADDR.ARPA", "65.100.IN-ADDR.ARPA", "66.100.IN-ADDR.ARPA",
	"67.100.IN-ADDR.ARPA", "68.100.IN-ADDR.ARPA", "69.100.IN-ADDR.ARPA",
	"70.100.IN-ADDR.ARPA", "71.100.IN-ADDR.ARPA", "72.100.IN-ADDR.ARPA",
	"73.100.IN-ADDR.ARPA", "74.100.IN-ADDR.ARPA", "75.100.IN-ADDR.ARPA",
	"76.100.IN-ADDR.ARPA", "77.100.IN-ADDR.ARPA", "78.100.IN-ADDR.ARPA",
	"79.100.IN-ADDR.ARPA", "80.100.IN-ADDR.ARPA", "81.100.IN-ADDR.ARPA",
	"82.100.IN-ADDR.ARPA", "83.100.IN-ADDR.ARPA", "84.100.IN-ADDR.ARPA",
	"85.100.IN-ADDR.ARPA", "86.100.IN-ADDR.ARPA", "87.100.IN-ADDR.ARPA",
	"88.100.IN-ADDR.ARPA", "89.100.IN-ADDR.ARPA", "90.100.IN-ADDR.ARPA",
	"91.100.IN-ADDR.ARPA", "92.100.IN-ADDR.ARPA", "93.100.IN-ADDR.ARPA",
	"94.100.IN-ADDR.ARPA", "95.100.IN-ADDR.ARPA", "96.100.IN-ADDR.ARPA",
	"97.100.IN-ADDR.ARPA", "98.100.IN-ADDR.ARPA", "99.100.IN-ADDR.ARPA",
	"100.100.IN-ADDR.ARPA", "101.100.IN-ADDR.ARPA", "102.100.IN-ADDR.ARPA",
	"103.100.IN-ADDR.ARPA", "104.100.IN-ADDR.ARPA", "105.100.IN-ADDR.ARPA",
	"106.100.IN-ADDR.ARPA", "107.100.IN-ADDR.ARPA", "108.100.IN-ADDR.ARPA",
	"109.100.IN-ADDR.ARPA", "110.100.IN-ADDR.ARPA", "111.100.IN-ADDR.ARPA",
	"112.100.IN-ADDR.ARPA", "113.100.IN-ADDR.ARPA", "114.100.IN-ADDR.ARPA",
	"115.100.IN-ADDR.ARPA", "116.100.IN-ADDR.ARPA", "117.100.IN-ADDR.ARPA",
	"118.100.IN-ADDR.ARPA", "119.100.IN-ADDR.ARPA", "120.100.IN-ADDR.ARPA",
	"121.100.IN-ADDR.ARPA", "122.100.IN-ADDR.ARPA", "123.100.IN-ADDR.ARPA",
	"124.100.IN-ADDR.ARPA", "125.100.IN-ADDR.ARPA", "126.100.IN-ADDR.ARPA",
	"127.100.IN-ADDR.ARPA",

	/* RFC 5735 and RFC 5737 */
	"0.IN-ADDR.ARPA",		/* THIS NETWORK */
	"127.IN-ADDR.ARPA",		/* LOOPBACK */
	"254.169.IN-ADDR.ARPA",		/* LINK LOCAL */
	"2.0.192.IN-ADDR.ARPA",		/* TEST NET */
	"100.51.198.IN-ADDR.ARPA",	/* TEST NET 2 */
	"113.0.203.IN-ADDR.ARPA",	/* TEST NET 3 */
	"255.255.255.255.IN-ADDR.ARPA", /* BROADCAST */

	/* Local IPv6 Unicast Addresses */
	"0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.IP6."
	"ARPA",
	"1.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.IP6."
	"ARPA",
	/* LOCALLY ASSIGNED LOCAL ADDRESS SCOPE */
	"D.F.IP6.ARPA", "8.E.F.IP6.ARPA", /* LINK LOCAL */
	"9.E.F.IP6.ARPA",		  /* LINK LOCAL */
	"A.E.F.IP6.ARPA",		  /* LINK LOCAL */
	"B.E.F.IP6.ARPA",		  /* LINK LOCAL */

	/* Example Prefix, RFC 3849. */
	"8.B.D.0.1.0.0.2.IP6.ARPA",

	/* RFC 7534 */
	"EMPTY.AS112.ARPA",

	/* RFC 8375 */
	"HOME.ARPA",

	/* RFC 9462 */
	"RESOLVER.ARPA",

	NULL
};

noreturn static void
fatal(const char *msg, isc_result_t result);

static void
named_server_reload(void *arg);

#ifdef HAVE_LIBNGHTTP2
static isc_result_t
listenelt_http(const cfg_obj_t *http, const uint16_t family, bool tls,
	       const ns_listen_tls_params_t *tls_params,
	       isc_tlsctx_cache_t *tlsctx_cache, in_port_t port,
	       isc_mem_t *mctx, isc_nm_proxy_type_t proxy,
	       ns_listenelt_t **target);
#endif

static isc_result_t
listenelt_fromconfig(const cfg_obj_t *listener, const cfg_obj_t *config,
		     cfg_aclconfctx_t *actx, isc_mem_t *mctx, uint16_t family,
		     isc_tlsctx_cache_t *tlsctx_cache, ns_listenelt_t **target);

static isc_result_t
listenlist_fromconfig(const cfg_obj_t *listenlist, const cfg_obj_t *config,
		      cfg_aclconfctx_t *actx, isc_mem_t *mctx, uint16_t family,
		      isc_tlsctx_cache_t *tlsctx_cache,
		      ns_listenlist_t **target);

static isc_result_t
configure_forward(const cfg_obj_t *config, dns_view_t *view,
		  const dns_name_t *origin, const cfg_obj_t *forwarders,
		  const cfg_obj_t *forwardtype);

static isc_result_t
configure_alternates(const cfg_obj_t *config, dns_view_t *view,
		     const cfg_obj_t *alternates);

static isc_result_t
configure_zone(const cfg_obj_t *config, const cfg_obj_t *zconfig,
	       const cfg_obj_t *vconfig, dns_view_t *view,
	       dns_viewlist_t *viewlist, dns_kasplist_t *kasplist,
	       dns_keystorelist_t *keystores, cfg_aclconfctx_t *aclconf,
	       bool added, bool old_rpz_ok, bool is_catz_member, bool modify);

static void
configure_zone_setviewcommit(isc_result_t result, const cfg_obj_t *zconfig,
			     dns_view_t *view);

static isc_result_t
configure_newzones(dns_view_t *view, cfg_obj_t *config, cfg_obj_t *vconfig,
		   cfg_aclconfctx_t *actx);

static const cfg_obj_t *
find_maplist(const cfg_obj_t *config, const char *listname, const char *name);

static isc_result_t
add_keydata_zone(dns_view_t *view, const char *directory, isc_mem_t *mctx);

static void
newzone_cfgctx_destroy(void **cfgp);

static isc_result_t
putstr(isc_buffer_t **b, const char *str);

static isc_result_t
putmem(isc_buffer_t **b, const char *str, size_t len);

static isc_result_t
putuint8(isc_buffer_t **b, uint8_t val);

static isc_result_t
putnull(isc_buffer_t **b);

#ifdef HAVE_LMDB
static isc_result_t
nzd_writable(dns_view_t *view);

static isc_result_t
nzd_open(dns_view_t *view, unsigned int flags, MDB_txn **txnp, MDB_dbi *dbi);

static isc_result_t
nzd_env_reopen(dns_view_t *view);

static void
nzd_env_close(dns_view_t *view);

static isc_result_t
nzd_close(MDB_txn **txnp, bool commit);
#else  /* ifdef HAVE_LMDB */
static isc_result_t
nzf_append(dns_view_t *view, const cfg_obj_t *zconfig);
#endif /* ifdef HAVE_LMDB */

static isc_result_t
load_nzf(dns_view_t *view, ns_cfgctx_t *nzcfg);

/*%
 * Configure a single view ACL at '*aclp'.  Get its configuration from
 * 'vconfig' (for per-view configuration) and maybe from 'config'
 */
static isc_result_t
configure_view_acl(const cfg_obj_t *vconfig, const cfg_obj_t *config,
		   const cfg_obj_t *gconfig, const char *aclname,
		   const char *acltuplename, cfg_aclconfctx_t *actx,
		   isc_mem_t *mctx, dns_acl_t **aclp) {
	isc_result_t result;
	const cfg_obj_t *maps[4];
	const cfg_obj_t *aclobj = NULL;
	int i = 0;

	if (*aclp != NULL) {
		dns_acl_detach(aclp);
	}
	if (vconfig != NULL) {
		maps[i++] = cfg_tuple_get(vconfig, "options");
	}
	if (config != NULL) {
		const cfg_obj_t *options = NULL;
		(void)cfg_map_get(config, "options", &options);
		if (options != NULL) {
			maps[i++] = options;
		}
	}
	if (gconfig != NULL) {
		const cfg_obj_t *options = NULL;
		(void)cfg_map_get(gconfig, "options", &options);
		if (options != NULL) {
			maps[i++] = options;
		}
	}
	maps[i] = NULL;

	(void)named_config_get(maps, aclname, &aclobj);
	if (aclobj == NULL) {
		/*
		 * No value available.	*aclp == NULL.
		 */
		return ISC_R_SUCCESS;
	}

	if (acltuplename != NULL) {
		/*
		 * If the ACL is given in an optional tuple, retrieve it.
		 * The parser should have ensured that a valid object be
		 * returned.
		 */
		aclobj = cfg_tuple_get(aclobj, acltuplename);
	}

	result = cfg_acl_fromconfig(aclobj, config, named_g_lctx, actx, mctx, 0,
				    aclp);

	return result;
}

/*%
 * Configure a sortlist at '*aclp'.  Essentially the same as
 * configure_view_acl() except it calls cfg_acl_fromconfig with a
 * nest_level value of 2.
 */
static isc_result_t
configure_view_sortlist(const cfg_obj_t *vconfig, const cfg_obj_t *config,
			cfg_aclconfctx_t *actx, isc_mem_t *mctx,
			dns_acl_t **aclp) {
	isc_result_t result;
	const cfg_obj_t *maps[3];
	const cfg_obj_t *aclobj = NULL;
	int i = 0;

	if (*aclp != NULL) {
		dns_acl_detach(aclp);
	}
	if (vconfig != NULL) {
		maps[i++] = cfg_tuple_get(vconfig, "options");
	}
	if (config != NULL) {
		const cfg_obj_t *options = NULL;
		(void)cfg_map_get(config, "options", &options);
		if (options != NULL) {
			maps[i++] = options;
		}
	}
	maps[i] = NULL;

	(void)named_config_get(maps, "sortlist", &aclobj);
	if (aclobj == NULL) {
		return ISC_R_SUCCESS;
	}

	/*
	 * Use a nest level of 3 for the "top level" of the sortlist;
	 * this means each entry in the top three levels will be stored
	 * as lists of separate, nested ACLs, rather than merged together
	 * into IP tables as is usually done with ACLs.
	 */
	result = cfg_acl_fromconfig(aclobj, config, named_g_lctx, actx, mctx, 3,
				    aclp);

	return result;
}

static isc_result_t
configure_view_nametable(const cfg_obj_t *vconfig, const cfg_obj_t *config,
			 const char *confname, const char *conftuplename,
			 isc_mem_t *mctx, dns_nametree_t **ntp) {
	isc_result_t result = ISC_R_SUCCESS;
	const cfg_obj_t *maps[3];
	const cfg_obj_t *obj = NULL;
	const cfg_listelt_t *element = NULL;
	int i = 0;
	dns_fixedname_t fixed;
	dns_name_t *name = NULL;
	isc_buffer_t b;
	const char *str = NULL;
	const cfg_obj_t *nameobj = NULL;

	if (*ntp != NULL) {
		dns_nametree_detach(ntp);
	}
	dns_nametree_create(mctx, DNS_NAMETREE_BOOL, confname, ntp);

	if (vconfig != NULL) {
		maps[i++] = cfg_tuple_get(vconfig, "options");
	}
	if (config != NULL) {
		const cfg_obj_t *options = NULL;
		(void)cfg_map_get(config, "options", &options);
		if (options != NULL) {
			maps[i++] = options;
		}
	}
	maps[i] = NULL;

	(void)named_config_get(maps, confname, &obj);
	if (obj == NULL) {
		/*
		 * No value available.	*ntp == NULL.
		 */
		return ISC_R_SUCCESS;
	}

	if (conftuplename != NULL) {
		obj = cfg_tuple_get(obj, conftuplename);
		if (cfg_obj_isvoid(obj)) {
			return ISC_R_SUCCESS;
		}
	}

	name = dns_fixedname_initname(&fixed);
	for (element = cfg_list_first(obj); element != NULL;
	     element = cfg_list_next(element))
	{
		nameobj = cfg_listelt_value(element);
		str = cfg_obj_asstring(nameobj);
		isc_buffer_constinit(&b, str, strlen(str));
		isc_buffer_add(&b, strlen(str));
		CHECK(dns_name_fromtext(name, &b, dns_rootname, 0, NULL));
		result = dns_nametree_add(*ntp, name, true);
		if (result != ISC_R_SUCCESS) {
			cfg_obj_log(nameobj, named_g_lctx, ISC_LOG_ERROR,
				    "failed to add %s for %s: %s", str,
				    confname, isc_result_totext(result));
			goto cleanup;
		}
	}

	return ISC_R_SUCCESS;

cleanup:
	dns_nametree_detach(ntp);
	return result;
}

static isc_result_t
ta_fromconfig(const cfg_obj_t *key, bool *initialp, const char **namestrp,
	      unsigned char *digest, dns_rdata_ds_t *ds) {
	isc_result_t result;
	dns_rdata_dnskey_t keystruct;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	uint32_t rdata1, rdata2, rdata3;
	const char *datastr = NULL, *namestr = NULL;
	unsigned char data[4096];
	isc_buffer_t databuf;
	unsigned char rrdata[4096];
	isc_buffer_t rrdatabuf;
	isc_region_t r;
	dns_fixedname_t fname;
	dns_name_t *name = NULL;
	isc_buffer_t namebuf;
	const char *atstr = NULL;
	enum {
		INIT_DNSKEY,
		STATIC_DNSKEY,
		INIT_DS,
		STATIC_DS,
		TRUSTED
	} anchortype;

	REQUIRE(namestrp != NULL && *namestrp == NULL);
	REQUIRE(ds != NULL);

	/* if DNSKEY, flags; if DS, key tag */
	rdata1 = cfg_obj_asuint32(cfg_tuple_get(key, "rdata1"));

	/* if DNSKEY, protocol; if DS, algorithm */
	rdata2 = cfg_obj_asuint32(cfg_tuple_get(key, "rdata2"));

	/* if DNSKEY, algorithm; if DS, digest type */
	rdata3 = cfg_obj_asuint32(cfg_tuple_get(key, "rdata3"));

	namestr = cfg_obj_asstring(cfg_tuple_get(key, "name"));
	*namestrp = namestr;

	name = dns_fixedname_initname(&fname);
	isc_buffer_constinit(&namebuf, namestr, strlen(namestr));
	isc_buffer_add(&namebuf, strlen(namestr));
	CHECK(dns_name_fromtext(name, &namebuf, dns_rootname, 0, NULL));

	if (*initialp) {
		atstr = cfg_obj_asstring(cfg_tuple_get(key, "anchortype"));

		if (strcasecmp(atstr, "static-key") == 0) {
			*initialp = false;
			anchortype = STATIC_DNSKEY;
		} else if (strcasecmp(atstr, "static-ds") == 0) {
			*initialp = false;
			anchortype = STATIC_DS;
		} else if (strcasecmp(atstr, "initial-key") == 0) {
			anchortype = INIT_DNSKEY;
		} else if (strcasecmp(atstr, "initial-ds") == 0) {
			anchortype = INIT_DS;
		} else {
			cfg_obj_log(key, named_g_lctx, ISC_LOG_ERROR,
				    "key '%s': "
				    "invalid initialization method '%s'",
				    namestr, atstr);
			result = ISC_R_FAILURE;
			goto cleanup;
		}
	} else {
		anchortype = TRUSTED;
	}

	isc_buffer_init(&databuf, data, sizeof(data));
	isc_buffer_init(&rrdatabuf, rrdata, sizeof(rrdata));

	*ds = (dns_rdata_ds_t){ .common.rdclass = dns_rdataclass_in,
				.common.rdtype = dns_rdatatype_ds };

	ISC_LINK_INIT(&ds->common, link);

	switch (anchortype) {
	case INIT_DNSKEY:
	case STATIC_DNSKEY:
	case TRUSTED:
		/*
		 * This function should never be reached for view
		 * class other than IN
		 */
		keystruct.common.rdclass = dns_rdataclass_in;
		keystruct.common.rdtype = dns_rdatatype_dnskey;

		/*
		 * The key data in keystruct is not dynamically allocated.
		 */
		keystruct.mctx = NULL;

		ISC_LINK_INIT(&keystruct.common, link);

		if (rdata1 > 0xffff) {
			CHECKM(ISC_R_RANGE, "key flags");
		}
		if (rdata1 & DNS_KEYFLAG_REVOKE) {
			CHECKM(DST_R_BADKEYTYPE, "key flags revoke bit set");
		}
		if (rdata2 > 0xff) {
			CHECKM(ISC_R_RANGE, "key protocol");
		}
		if (rdata3 > 0xff) {
			CHECKM(ISC_R_RANGE, "key algorithm");
		}

		keystruct.flags = (uint16_t)rdata1;
		keystruct.protocol = (uint8_t)rdata2;
		keystruct.algorithm = (uint8_t)rdata3;

		if (!dst_algorithm_supported(keystruct.algorithm)) {
			CHECK(DST_R_UNSUPPORTEDALG);
		}

		datastr = cfg_obj_asstring(cfg_tuple_get(key, "data"));
		CHECK(isc_base64_decodestring(datastr, &databuf));
		isc_buffer_usedregion(&databuf, &r);
		keystruct.datalen = r.length;
		keystruct.data = r.base;

		CHECK(dns_rdata_fromstruct(&rdata, keystruct.common.rdclass,
					   keystruct.common.rdtype, &keystruct,
					   &rrdatabuf));
		CHECK(dns_ds_fromkeyrdata(name, &rdata, DNS_DSDIGEST_SHA256,
					  digest, ds));
		break;

	case INIT_DS:
	case STATIC_DS:
		if (rdata1 > 0xffff) {
			CHECKM(ISC_R_RANGE, "key tag");
		}
		if (rdata2 > 0xff) {
			CHECKM(ISC_R_RANGE, "key algorithm");
		}
		if (rdata3 > 0xff) {
			CHECKM(ISC_R_RANGE, "digest type");
		}

		ds->key_tag = (uint16_t)rdata1;
		ds->algorithm = (uint8_t)rdata2;
		ds->digest_type = (uint8_t)rdata3;

		datastr = cfg_obj_asstring(cfg_tuple_get(key, "data"));
		CHECK(isc_hex_decodestring(datastr, &databuf));
		isc_buffer_usedregion(&databuf, &r);

		switch (ds->digest_type) {
		case DNS_DSDIGEST_SHA1:
			if (r.length != ISC_SHA1_DIGESTLENGTH) {
				CHECK(ISC_R_UNEXPECTEDEND);
			}
			break;
		case DNS_DSDIGEST_SHA256:
			if (r.length != ISC_SHA256_DIGESTLENGTH) {
				CHECK(ISC_R_UNEXPECTEDEND);
			}
			break;
		case DNS_DSDIGEST_SHA384:
			if (r.length != ISC_SHA384_DIGESTLENGTH) {
				CHECK(ISC_R_UNEXPECTEDEND);
			}
			break;
		default:
			cfg_obj_log(key, named_g_lctx, ISC_LOG_ERROR,
				    "key '%s': "
				    "unknown ds digest type %u",
				    namestr, ds->digest_type);
			result = ISC_R_FAILURE;
			goto cleanup;
			break;
		}

		ds->length = r.length;
		ds->digest = digest;
		memmove(ds->digest, r.base, r.length);

		break;

	default:
		UNREACHABLE();
	}

	return ISC_R_SUCCESS;

cleanup:
	return result;
}

static void
sfd_add(const dns_name_t *name, void *arg) {
	if (arg != NULL) {
		dns_view_sfd_add(arg, name);
	}
}

/*%
 * Parse 'key' in the context of view configuration 'vconfig'.  If successful,
 * add the key to 'secroots' if both of the following conditions are true:
 *
 *   - 'keyname_match' is NULL or it matches the owner name of 'key',
 *   - support for the algorithm used by 'key' is not disabled by 'resolver'
 *     for the owner name of 'key'.
 *
 * 'managed' is true for managed keys and false for trusted keys.  'mctx' is
 * the memory context to use for allocating memory.
 */
static isc_result_t
process_key(const cfg_obj_t *key, dns_keytable_t *secroots,
	    const dns_name_t *keyname_match, dns_view_t *view, bool managed) {
	dns_fixedname_t fkeyname;
	dns_name_t *keyname = NULL;
	const char *namestr = NULL;
	dns_rdata_ds_t ds;
	isc_result_t result;
	bool initializing = managed;
	unsigned char digest[ISC_MAX_MD_SIZE];
	isc_buffer_t b;

	result = ta_fromconfig(key, &initializing, &namestr, digest, &ds);

	switch (result) {
	case ISC_R_SUCCESS:
		/*
		 * Trust anchor was parsed correctly.
		 */
		isc_buffer_constinit(&b, namestr, strlen(namestr));
		isc_buffer_add(&b, strlen(namestr));
		keyname = dns_fixedname_initname(&fkeyname);
		result = dns_name_fromtext(keyname, &b, dns_rootname, 0, NULL);
		if (result != ISC_R_SUCCESS) {
			return result;
		}
		break;
	case DST_R_UNSUPPORTEDALG:
	case DST_R_BADKEYTYPE:
		/*
		 * Key was parsed correctly, but it cannot be used; this is not
		 * a fatal error - log a warning about this key being ignored,
		 * but do not prevent any further ones from being processed.
		 */
		cfg_obj_log(key, named_g_lctx, ISC_LOG_WARNING,
			    "ignoring %s for '%s': %s",
			    initializing ? "initial-key" : "static-key",
			    namestr, isc_result_totext(result));
		return ISC_R_SUCCESS;
	case DST_R_NOCRYPTO:
		/*
		 * Crypto support is not available.
		 */
		cfg_obj_log(key, named_g_lctx, ISC_LOG_ERROR,
			    "ignoring %s for '%s': no crypto support",
			    initializing ? "initial-key" : "static-key",
			    namestr);
		return result;
	default:
		/*
		 * Something unexpected happened; we have no choice but to
		 * indicate an error so that the configuration loading process
		 * is interrupted.
		 */
		cfg_obj_log(key, named_g_lctx, ISC_LOG_ERROR,
			    "configuring %s for '%s': %s",
			    initializing ? "initial-key" : "static-key",
			    namestr, isc_result_totext(result));
		return ISC_R_FAILURE;
	}

	/*
	 * If the caller requested to only load keys for a specific name and
	 * the owner name of this key does not match the requested name, do not
	 * load it.
	 */
	if (keyname_match != NULL && !dns_name_equal(keyname_match, keyname)) {
		goto done;
	}

	/*
	 * Ensure that 'resolver' allows using the algorithm of this key for
	 * its owner name.  If it does not, do not load the key and log a
	 * warning, but do not prevent further keys from being processed.
	 */
	if (!dns_resolver_algorithm_supported(view->resolver, keyname,
					      ds.algorithm))
	{
		cfg_obj_log(key, named_g_lctx, ISC_LOG_WARNING,
			    "ignoring %s for '%s': algorithm is disabled",
			    initializing ? "initial-key" : "static-key",
			    namestr);
		goto done;
	}

	/*
	 * Add the key to 'secroots'.  Keys from a "trust-anchors" or
	 * "managed-keys" statement may be either static or initializing
	 * keys. If it's not initializing, we don't want to treat it as
	 * managed, so we use 'initializing' twice here, for both the
	 * 'managed' and 'initializing' arguments to dns_keytable_add().
	 */
	result = dns_keytable_add(secroots, initializing, initializing, keyname,
				  &ds, sfd_add, view);

done:
	return result;
}

/*
 * Load keys from configuration into key table. If 'keyname' is specified,
 * only load keys matching that name. If 'managed' is true, load the key as
 * an initializing key.
 */
static isc_result_t
load_view_keys(const cfg_obj_t *keys, dns_view_t *view, bool managed,
	       const dns_name_t *keyname) {
	const cfg_listelt_t *elt, *elt2;
	const cfg_obj_t *keylist;
	isc_result_t result;
	dns_keytable_t *secroots = NULL;

	CHECK(dns_view_getsecroots(view, &secroots));

	for (elt = cfg_list_first(keys); elt != NULL; elt = cfg_list_next(elt))
	{
		keylist = cfg_listelt_value(elt);

		for (elt2 = cfg_list_first(keylist); elt2 != NULL;
		     elt2 = cfg_list_next(elt2))
		{
			CHECK(process_key(cfg_listelt_value(elt2), secroots,
					  keyname, view, managed));
		}
	}

cleanup:
	if (secroots != NULL) {
		dns_keytable_detach(&secroots);
	}
	if (result == DST_R_NOCRYPTO) {
		result = ISC_R_SUCCESS;
	}
	return result;
}

/*%
 * Check whether a key has been successfully loaded.
 */
static bool
keyloaded(dns_view_t *view, const dns_name_t *name) {
	isc_result_t result;
	dns_keytable_t *secroots = NULL;
	dns_keynode_t *keynode = NULL;

	result = dns_view_getsecroots(view, &secroots);
	if (result != ISC_R_SUCCESS) {
		return false;
	}

	result = dns_keytable_find(secroots, name, &keynode);

	if (keynode != NULL) {
		dns_keynode_detach(&keynode);
	}
	if (secroots != NULL) {
		dns_keytable_detach(&secroots);
	}

	return result == ISC_R_SUCCESS;
}

/*%
 * Configure DNSSEC keys for a view.
 *
 * The per-view configuration values and the server-global defaults are read
 * from 'vconfig' and 'config'.
 */
static isc_result_t
configure_view_dnsseckeys(dns_view_t *view, const cfg_obj_t *vconfig,
			  const cfg_obj_t *config, const cfg_obj_t *bindkeys,
			  bool auto_root) {
	isc_result_t result = ISC_R_SUCCESS;
	const cfg_obj_t *view_keys = NULL;
	const cfg_obj_t *global_keys = NULL;
	const cfg_obj_t *view_managed_keys = NULL;
	const cfg_obj_t *view_trust_anchors = NULL;
	const cfg_obj_t *global_managed_keys = NULL;
	const cfg_obj_t *global_trust_anchors = NULL;
	const cfg_obj_t *maps[4];
	const cfg_obj_t *voptions = NULL;
	const cfg_obj_t *options = NULL;
	const cfg_obj_t *obj = NULL;
	const char *directory;
	int i = 0;

	/* We don't need trust anchors for the _bind view */
	if (strcmp(view->name, "_bind") == 0 &&
	    view->rdclass == dns_rdataclass_chaos)
	{
		return ISC_R_SUCCESS;
	}

	if (vconfig != NULL) {
		voptions = cfg_tuple_get(vconfig, "options");
		if (voptions != NULL) {
			(void)cfg_map_get(voptions, "trusted-keys", &view_keys);

			/* managed-keys and trust-anchors are synonyms. */
			(void)cfg_map_get(voptions, "managed-keys",
					  &view_managed_keys);
			(void)cfg_map_get(voptions, "trust-anchors",
					  &view_trust_anchors);

			maps[i++] = voptions;
		}
	}

	if (config != NULL) {
		(void)cfg_map_get(config, "trusted-keys", &global_keys);

		/* managed-keys and trust-anchors are synonyms. */
		(void)cfg_map_get(config, "managed-keys", &global_managed_keys);
		(void)cfg_map_get(config, "trust-anchors",
				  &global_trust_anchors);

		(void)cfg_map_get(config, "options", &options);
		if (options != NULL) {
			maps[i++] = options;
		}
	}

	maps[i++] = named_g_defaults;
	maps[i] = NULL;

	dns_view_initsecroots(view);
	dns_view_initntatable(view, named_g_loopmgr);

	if (auto_root && view->rdclass == dns_rdataclass_in) {
		const cfg_obj_t *builtin_keys = NULL;

		/*
		 * If bind.keys exists and is populated, it overrides
		 * the trust-anchors clause hard-coded in named_g_config.
		 */
		if (bindkeys != NULL) {
			isc_log_write(named_g_lctx, DNS_LOGCATEGORY_SECURITY,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
				      "obtaining root key for view %s "
				      "from '%s'",
				      view->name, named_g_server->bindkeysfile);

			(void)cfg_map_get(bindkeys, "trust-anchors",
					  &builtin_keys);

			if (builtin_keys == NULL) {
				isc_log_write(
					named_g_lctx, DNS_LOGCATEGORY_SECURITY,
					NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
					"dnssec-validation auto: "
					"WARNING: root zone key "
					"not found");
			}
		}

		if (builtin_keys == NULL) {
			isc_log_write(named_g_lctx, DNS_LOGCATEGORY_SECURITY,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
				      "using built-in root key for view %s",
				      view->name);

			(void)cfg_map_get(named_g_config, "trust-anchors",
					  &builtin_keys);
		}

		if (builtin_keys != NULL) {
			CHECK(load_view_keys(builtin_keys, view, true,
					     dns_rootname));
		}

		if (!keyloaded(view, dns_rootname)) {
			isc_log_write(named_g_lctx, DNS_LOGCATEGORY_SECURITY,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "root key not loaded");
			result = ISC_R_FAILURE;
			goto cleanup;
		}
	}

	if (view->rdclass == dns_rdataclass_in) {
		CHECK(load_view_keys(view_keys, view, false, NULL));
		CHECK(load_view_keys(view_trust_anchors, view, true, NULL));
		CHECK(load_view_keys(view_managed_keys, view, true, NULL));

		CHECK(load_view_keys(global_keys, view, false, NULL));
		CHECK(load_view_keys(global_trust_anchors, view, true, NULL));
		CHECK(load_view_keys(global_managed_keys, view, true, NULL));
	}

	/*
	 * Add key zone for managed keys.
	 */
	obj = NULL;
	(void)named_config_get(maps, "managed-keys-directory", &obj);
	directory = (obj != NULL ? cfg_obj_asstring(obj) : NULL);
	if (directory != NULL) {
		result = isc_file_isdirectory(directory);
	}
	if (result != ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, DNS_LOGCATEGORY_SECURITY,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "invalid managed-keys-directory %s: %s",
			      directory, isc_result_totext(result));
		goto cleanup;
	} else if (directory != NULL) {
		if (!isc_file_isdirwritable(directory)) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "managed-keys-directory '%s' "
				      "is not writable",
				      directory);
			result = ISC_R_NOPERM;
			goto cleanup;
		}
	}

	if (auto_root) {
		CHECK(add_keydata_zone(view, directory, named_g_mctx));
	}

cleanup:
	return result;
}

static isc_result_t
mustbesecure(const cfg_obj_t *mbs, dns_resolver_t *resolver) {
	const cfg_listelt_t *element;
	const cfg_obj_t *obj;
	const char *str;
	dns_fixedname_t fixed;
	dns_name_t *name;
	bool value;
	isc_result_t result;
	isc_buffer_t b;

	name = dns_fixedname_initname(&fixed);
	for (element = cfg_list_first(mbs); element != NULL;
	     element = cfg_list_next(element))
	{
		obj = cfg_listelt_value(element);
		str = cfg_obj_asstring(cfg_tuple_get(obj, "name"));
		isc_buffer_constinit(&b, str, strlen(str));
		isc_buffer_add(&b, strlen(str));
		CHECK(dns_name_fromtext(name, &b, dns_rootname, 0, NULL));
		value = cfg_obj_asboolean(cfg_tuple_get(obj, "value"));
		CHECK(dns_resolver_setmustbesecure(resolver, name, value));
	}

	result = ISC_R_SUCCESS;

cleanup:
	return result;
}

/*%
 * Get a dispatch appropriate for the resolver of a given view.
 */
static isc_result_t
get_view_querysource_dispatch(const cfg_obj_t **maps, int af,
			      dns_dispatch_t **dispatchp, bool is_firstview) {
	isc_result_t result = ISC_R_FAILURE;
	dns_dispatch_t *disp = NULL;
	isc_sockaddr_t sa;
	const cfg_obj_t *obj = NULL;

	switch (af) {
	case AF_INET:
		result = named_config_get(maps, "query-source", &obj);
		INSIST(result == ISC_R_SUCCESS);
		break;
	case AF_INET6:
		result = named_config_get(maps, "query-source-v6", &obj);
		INSIST(result == ISC_R_SUCCESS);
		break;
	default:
		UNREACHABLE();
	}

	if (cfg_obj_isvoid(obj)) {
		/*
		 * We don't want to use this address family, let's
		 * bail now. The dispatch object for this family will
		 * be null then not used to run queries.
		 */
		return ISC_R_SUCCESS;
	} else {
		/*
		 * obj _has_ to be sockaddr here, cfg_obj_assockaddr()
		 * asserts this internally.
		 */
		sa = *(cfg_obj_assockaddr(obj));
		INSIST(isc_sockaddr_pf(&sa) == af);
	}

	/*
	 * If we don't support this address family, we're done!
	 */
	switch (af) {
	case AF_INET:
		result = isc_net_probeipv4();
		break;
	case AF_INET6:
		result = isc_net_probeipv6();
		break;
	default:
		UNREACHABLE();
	}
	if (result != ISC_R_SUCCESS) {
		return ISC_R_SUCCESS;
	}

	/*
	 * Try to find a dispatcher that we can share.
	 */
	if (isc_sockaddr_getport(&sa) != 0) {
		INSIST(obj != NULL);
		if (is_firstview) {
			cfg_obj_log(obj, named_g_lctx, ISC_LOG_INFO,
				    "using specific query-source port "
				    "suppresses port randomization and can be "
				    "insecure.");
		}
	}

	result = dns_dispatch_createudp(named_g_dispatchmgr, &sa, &disp);
	if (result != ISC_R_SUCCESS) {
		isc_sockaddr_t any;
		char buf[ISC_SOCKADDR_FORMATSIZE];

		switch (af) {
		case AF_INET:
			isc_sockaddr_any(&any);
			break;
		case AF_INET6:
			isc_sockaddr_any6(&any);
			break;
		}
		if (isc_sockaddr_equal(&sa, &any)) {
			return ISC_R_SUCCESS;
		}
		isc_sockaddr_format(&sa, buf, sizeof(buf));
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "could not get query source dispatcher (%s): %s",
			      buf, isc_result_totext(result));
		return result;
	}

	*dispatchp = disp;

	return ISC_R_SUCCESS;
}

static isc_result_t
configure_order(dns_order_t *order, const cfg_obj_t *ent) {
	dns_rdataclass_t rdclass;
	dns_rdatatype_t rdtype;
	const cfg_obj_t *obj;
	dns_fixedname_t fixed;
	unsigned int mode = 0;
	const char *str;
	isc_buffer_t b;
	isc_result_t result;
	bool addroot;

	result = named_config_getclass(cfg_tuple_get(ent, "class"),
				       dns_rdataclass_any, &rdclass);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	result = named_config_gettype(cfg_tuple_get(ent, "type"),
				      dns_rdatatype_any, &rdtype);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	obj = cfg_tuple_get(ent, "name");
	if (cfg_obj_isstring(obj)) {
		str = cfg_obj_asstring(obj);
	} else {
		str = "*";
	}
	addroot = (strcmp(str, "*") == 0);
	isc_buffer_constinit(&b, str, strlen(str));
	isc_buffer_add(&b, strlen(str));
	dns_fixedname_init(&fixed);
	result = dns_name_fromtext(dns_fixedname_name(&fixed), &b, dns_rootname,
				   0, NULL);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	obj = cfg_tuple_get(ent, "ordering");
	INSIST(cfg_obj_isstring(obj));
	str = cfg_obj_asstring(obj);
	if (!strcasecmp(str, "fixed")) {
#if DNS_RDATASET_FIXED
		mode = DNS_RDATASETATTR_FIXEDORDER;
#else  /* if DNS_RDATASET_FIXED */
		mode = DNS_RDATASETATTR_CYCLIC;
#endif /* DNS_RDATASET_FIXED */
	} else if (!strcasecmp(str, "random")) {
		mode = DNS_RDATASETATTR_RANDOMIZE;
	} else if (!strcasecmp(str, "cyclic")) {
		mode = DNS_RDATASETATTR_CYCLIC;
	} else if (!strcasecmp(str, "none")) {
		mode = DNS_RDATASETATTR_NONE;
	} else {
		UNREACHABLE();
	}

	/*
	 * "*" should match everything including the root (BIND 8 compat).
	 * As dns_name_matcheswildcard(".", "*.") returns FALSE add a
	 * explicit entry for "." when the name is "*".
	 */
	if (addroot) {
		result = dns_order_add(order, dns_rootname, rdtype, rdclass,
				       mode);
		if (result != ISC_R_SUCCESS) {
			return result;
		}
	}

	return dns_order_add(order, dns_fixedname_name(&fixed), rdtype, rdclass,
			     mode);
}

static isc_result_t
configure_peer(const cfg_obj_t *cpeer, isc_mem_t *mctx, dns_peer_t **peerp) {
	isc_netaddr_t na;
	dns_peer_t *peer;
	const cfg_obj_t *obj;
	const char *str;
	isc_result_t result;
	unsigned int prefixlen;

	cfg_obj_asnetprefix(cfg_map_getname(cpeer), &na, &prefixlen);

	peer = NULL;
	result = dns_peer_newprefix(mctx, &na, prefixlen, &peer);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	obj = NULL;
	(void)cfg_map_get(cpeer, "bogus", &obj);
	if (obj != NULL) {
		CHECK(dns_peer_setbogus(peer, cfg_obj_asboolean(obj)));
	}

	obj = NULL;
	(void)cfg_map_get(cpeer, "provide-ixfr", &obj);
	if (obj != NULL) {
		CHECK(dns_peer_setprovideixfr(peer, cfg_obj_asboolean(obj)));
	}

	obj = NULL;
	(void)cfg_map_get(cpeer, "request-expire", &obj);
	if (obj != NULL) {
		CHECK(dns_peer_setrequestexpire(peer, cfg_obj_asboolean(obj)));
	}

	obj = NULL;
	(void)cfg_map_get(cpeer, "request-ixfr", &obj);
	if (obj != NULL) {
		CHECK(dns_peer_setrequestixfr(peer, cfg_obj_asboolean(obj)));
	}

	obj = NULL;
	(void)cfg_map_get(cpeer, "request-nsid", &obj);
	if (obj != NULL) {
		CHECK(dns_peer_setrequestnsid(peer, cfg_obj_asboolean(obj)));
	}

	obj = NULL;
	(void)cfg_map_get(cpeer, "send-cookie", &obj);
	if (obj != NULL) {
		CHECK(dns_peer_setsendcookie(peer, cfg_obj_asboolean(obj)));
	}

	obj = NULL;
	(void)cfg_map_get(cpeer, "require-cookie", &obj);
	if (obj != NULL) {
		CHECK(dns_peer_setrequirecookie(peer, cfg_obj_asboolean(obj)));
	}

	obj = NULL;
	(void)cfg_map_get(cpeer, "edns", &obj);
	if (obj != NULL) {
		CHECK(dns_peer_setsupportedns(peer, cfg_obj_asboolean(obj)));
	}

	obj = NULL;
	(void)cfg_map_get(cpeer, "edns-udp-size", &obj);
	if (obj != NULL) {
		uint32_t udpsize = cfg_obj_asuint32(obj);
		if (udpsize < 512U) {
			udpsize = 512U;
		}
		if (udpsize > 4096U) {
			udpsize = 4096U;
		}
		CHECK(dns_peer_setudpsize(peer, (uint16_t)udpsize));
	}

	obj = NULL;
	(void)cfg_map_get(cpeer, "edns-version", &obj);
	if (obj != NULL) {
		uint32_t ednsversion = cfg_obj_asuint32(obj);
		if (ednsversion > 255U) {
			ednsversion = 255U;
		}
		CHECK(dns_peer_setednsversion(peer, (uint8_t)ednsversion));
	}

	obj = NULL;
	(void)cfg_map_get(cpeer, "max-udp-size", &obj);
	if (obj != NULL) {
		uint32_t udpsize = cfg_obj_asuint32(obj);
		if (udpsize < 512U) {
			udpsize = 512U;
		}
		if (udpsize > 4096U) {
			udpsize = 4096U;
		}
		CHECK(dns_peer_setmaxudp(peer, (uint16_t)udpsize));
	}

	obj = NULL;
	(void)cfg_map_get(cpeer, "padding", &obj);
	if (obj != NULL) {
		uint32_t padding = cfg_obj_asuint32(obj);
		if (padding > 512U) {
			cfg_obj_log(obj, named_g_lctx, ISC_LOG_WARNING,
				    "server padding value cannot "
				    "exceed 512: lowering");
			padding = 512U;
		}
		CHECK(dns_peer_setpadding(peer, (uint16_t)padding));
	}

	obj = NULL;
	(void)cfg_map_get(cpeer, "tcp-only", &obj);
	if (obj != NULL) {
		CHECK(dns_peer_setforcetcp(peer, cfg_obj_asboolean(obj)));
	}

	obj = NULL;
	(void)cfg_map_get(cpeer, "tcp-keepalive", &obj);
	if (obj != NULL) {
		CHECK(dns_peer_settcpkeepalive(peer, cfg_obj_asboolean(obj)));
	}

	obj = NULL;
	(void)cfg_map_get(cpeer, "transfers", &obj);
	if (obj != NULL) {
		CHECK(dns_peer_settransfers(peer, cfg_obj_asuint32(obj)));
	}

	obj = NULL;
	(void)cfg_map_get(cpeer, "transfer-format", &obj);
	if (obj != NULL) {
		str = cfg_obj_asstring(obj);
		if (strcasecmp(str, "many-answers") == 0) {
			CHECK(dns_peer_settransferformat(peer,
							 dns_many_answers));
		} else if (strcasecmp(str, "one-answer") == 0) {
			CHECK(dns_peer_settransferformat(peer, dns_one_answer));
		} else {
			UNREACHABLE();
		}
	}

	obj = NULL;
	(void)cfg_map_get(cpeer, "keys", &obj);
	if (obj != NULL) {
		result = dns_peer_setkeybycharp(peer, cfg_obj_asstring(obj));
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}
	}

	obj = NULL;
	if (na.family == AF_INET) {
		(void)cfg_map_get(cpeer, "transfer-source", &obj);
	} else {
		(void)cfg_map_get(cpeer, "transfer-source-v6", &obj);
	}
	if (obj != NULL) {
		result = dns_peer_settransfersource(peer,
						    cfg_obj_assockaddr(obj));
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}
	}

	obj = NULL;
	if (na.family == AF_INET) {
		(void)cfg_map_get(cpeer, "notify-source", &obj);
	} else {
		(void)cfg_map_get(cpeer, "notify-source-v6", &obj);
	}
	if (obj != NULL) {
		result = dns_peer_setnotifysource(peer,
						  cfg_obj_assockaddr(obj));
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}
	}

	obj = NULL;
	if (na.family == AF_INET) {
		(void)cfg_map_get(cpeer, "query-source", &obj);
	} else {
		(void)cfg_map_get(cpeer, "query-source-v6", &obj);
	}
	if (obj != NULL) {
		INSIST(cfg_obj_issockaddr(obj));
		result = dns_peer_setquerysource(peer, cfg_obj_assockaddr(obj));
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}
	}

	*peerp = peer;
	return ISC_R_SUCCESS;

cleanup:
	dns_peer_detach(&peer);
	return result;
}

static isc_result_t
configure_dyndb(const cfg_obj_t *dyndb, isc_mem_t *mctx,
		const dns_dyndbctx_t *dctx) {
	isc_result_t result = ISC_R_SUCCESS;
	const cfg_obj_t *obj;
	const char *name, *library;

	/* Get the name of the dyndb instance and the library path . */
	name = cfg_obj_asstring(cfg_tuple_get(dyndb, "name"));
	library = cfg_obj_asstring(cfg_tuple_get(dyndb, "library"));

	obj = cfg_tuple_get(dyndb, "parameters");
	if (obj != NULL) {
		result = dns_dyndb_load(library, name, cfg_obj_asstring(obj),
					cfg_obj_file(obj), cfg_obj_line(obj),
					mctx, dctx);
	}

	if (result != ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "dynamic database '%s' configuration failed: %s",
			      name, isc_result_totext(result));
	}
	return result;
}

static isc_result_t
disable_algorithms(const cfg_obj_t *disabled, dns_resolver_t *resolver) {
	isc_result_t result;
	const cfg_obj_t *algorithms;
	const cfg_listelt_t *element;
	const char *str;
	dns_fixedname_t fixed;
	dns_name_t *name;
	isc_buffer_t b;

	name = dns_fixedname_initname(&fixed);
	str = cfg_obj_asstring(cfg_tuple_get(disabled, "name"));
	isc_buffer_constinit(&b, str, strlen(str));
	isc_buffer_add(&b, strlen(str));
	CHECK(dns_name_fromtext(name, &b, dns_rootname, 0, NULL));

	algorithms = cfg_tuple_get(disabled, "algorithms");
	for (element = cfg_list_first(algorithms); element != NULL;
	     element = cfg_list_next(element))
	{
		isc_textregion_t r;
		dns_secalg_t alg;

		r.base = UNCONST(cfg_obj_asstring(cfg_listelt_value(element)));
		r.length = strlen(r.base);

		result = dns_secalg_fromtext(&alg, &r);
		if (result != ISC_R_SUCCESS) {
			uint8_t ui;
			result = isc_parse_uint8(&ui, r.base, 10);
			alg = ui;
		}
		if (result != ISC_R_SUCCESS) {
			cfg_obj_log(cfg_listelt_value(element), named_g_lctx,
				    ISC_LOG_ERROR, "invalid algorithm");
			CHECK(result);
		}
		CHECK(dns_resolver_disable_algorithm(resolver, name, alg));
	}
cleanup:
	return result;
}

static isc_result_t
disable_ds_digests(const cfg_obj_t *disabled, dns_resolver_t *resolver) {
	isc_result_t result;
	const cfg_obj_t *digests;
	const cfg_listelt_t *element;
	const char *str;
	dns_fixedname_t fixed;
	dns_name_t *name;
	isc_buffer_t b;

	name = dns_fixedname_initname(&fixed);
	str = cfg_obj_asstring(cfg_tuple_get(disabled, "name"));
	isc_buffer_constinit(&b, str, strlen(str));
	isc_buffer_add(&b, strlen(str));
	CHECK(dns_name_fromtext(name, &b, dns_rootname, 0, NULL));

	digests = cfg_tuple_get(disabled, "digests");
	for (element = cfg_list_first(digests); element != NULL;
	     element = cfg_list_next(element))
	{
		isc_textregion_t r;
		dns_dsdigest_t digest;

		r.base = UNCONST(cfg_obj_asstring(cfg_listelt_value(element)));
		r.length = strlen(r.base);

		/* disable_ds_digests handles numeric values. */
		result = dns_dsdigest_fromtext(&digest, &r);
		if (result != ISC_R_SUCCESS) {
			cfg_obj_log(cfg_listelt_value(element), named_g_lctx,
				    ISC_LOG_ERROR, "invalid algorithm");
			CHECK(result);
		}
		CHECK(dns_resolver_disable_ds_digest(resolver, name, digest));
	}
cleanup:
	return result;
}

static bool
on_disable_list(const cfg_obj_t *disablelist, dns_name_t *zonename) {
	const cfg_listelt_t *element;
	dns_fixedname_t fixed;
	dns_name_t *name;
	isc_result_t result;
	const cfg_obj_t *value;
	const char *str;
	isc_buffer_t b;

	name = dns_fixedname_initname(&fixed);

	for (element = cfg_list_first(disablelist); element != NULL;
	     element = cfg_list_next(element))
	{
		value = cfg_listelt_value(element);
		str = cfg_obj_asstring(value);
		isc_buffer_constinit(&b, str, strlen(str));
		isc_buffer_add(&b, strlen(str));
		result = dns_name_fromtext(name, &b, dns_rootname, 0, NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
		if (dns_name_equal(name, zonename)) {
			return true;
		}
	}
	return false;
}

static isc_result_t
check_dbtype(dns_zone_t *zone, unsigned int dbtypec, const char **dbargv,
	     isc_mem_t *mctx) {
	char **argv = NULL;
	unsigned int i;
	isc_result_t result = ISC_R_SUCCESS;

	CHECK(dns_zone_getdbtype(zone, &argv, mctx));

	/*
	 * Check that all the arguments match.
	 */
	for (i = 0; i < dbtypec; i++) {
		if (argv[i] == NULL || strcmp(argv[i], dbargv[i]) != 0) {
			CHECK(ISC_R_FAILURE);
		}
	}

	/*
	 * Check that there are not extra arguments.
	 */
	if (i == dbtypec && argv[i] != NULL) {
		result = ISC_R_FAILURE;
	}

cleanup:
	isc_mem_free(mctx, argv);
	return result;
}

static void
setquerystats(dns_zone_t *zone, isc_mem_t *mctx, dns_zonestat_level_t level) {
	isc_stats_t *zoneqrystats;

	dns_zone_setstatlevel(zone, level);

	zoneqrystats = NULL;
	if (level == dns_zonestat_full) {
		isc_stats_create(mctx, &zoneqrystats, ns_statscounter_max);
	}
	dns_zone_setrequeststats(zone, zoneqrystats);
	if (zoneqrystats != NULL) {
		isc_stats_detach(&zoneqrystats);
	}
}

static named_cache_t *
cachelist_find(named_cachelist_t *cachelist, const char *cachename,
	       dns_rdataclass_t rdclass) {
	for (named_cache_t *nsc = ISC_LIST_HEAD(*cachelist); nsc != NULL;
	     nsc = ISC_LIST_NEXT(nsc, link))
	{
		if (nsc->rdclass == rdclass &&
		    strcmp(dns_cache_getname(nsc->cache), cachename) == 0)
		{
			return nsc;
		}
	}

	return NULL;
}

static bool
cache_reusable(dns_view_t *originview, dns_view_t *view,
	       bool new_zero_no_soattl) {
	if (originview->rdclass != view->rdclass ||
	    originview->checknames != view->checknames ||
	    originview->acceptexpired != view->acceptexpired ||
	    originview->enablevalidation != view->enablevalidation ||
	    originview->maxcachettl != view->maxcachettl ||
	    originview->maxncachettl != view->maxncachettl ||
	    originview->resolver == NULL ||
	    dns_resolver_getzeronosoattl(originview->resolver) !=
		    new_zero_no_soattl)
	{
		return false;
	}

	return true;
}

static bool
cache_sharable(dns_view_t *originview, dns_view_t *view,
	       bool new_zero_no_soattl, uint64_t new_max_cache_size,
	       uint32_t new_stale_ttl, uint32_t new_stale_refresh_time) {
	/*
	 * If the cache cannot even reused for the same view, it cannot be
	 * shared with other views.
	 */
	if (!cache_reusable(originview, view, new_zero_no_soattl)) {
		return false;
	}

	/*
	 * Check other cache related parameters that must be consistent among
	 * the sharing views.
	 */
	if (dns_cache_getservestalettl(originview->cache) != new_stale_ttl ||
	    dns_cache_getservestalerefresh(originview->cache) !=
		    new_stale_refresh_time ||
	    dns_cache_getcachesize(originview->cache) != new_max_cache_size)
	{
		return false;
	}

	return true;
}

/*
 * Callback from DLZ configure when the driver sets up a writeable zone
 */
static isc_result_t
dlzconfigure_callback(dns_view_t *view, dns_dlzdb_t *dlzdb, dns_zone_t *zone) {
	dns_name_t *origin = dns_zone_getorigin(zone);
	dns_rdataclass_t zclass = view->rdclass;
	isc_result_t result;

	result = dns_zonemgr_managezone(named_g_server->zonemgr, zone);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	dns_zone_setstats(zone, named_g_server->zonestats);

	return named_zone_configure_writeable_dlz(dlzdb, zone, zclass, origin);
}

static isc_result_t
dns64_reverse(dns_view_t *view, isc_mem_t *mctx, isc_netaddr_t *na,
	      unsigned int prefixlen, const char *server, const char *contact) {
	char reverse[48 + sizeof("ip6.arpa.")] = { 0 };
	char buf[sizeof("x.x.")];
	const char *dns64_dbtype[4] = { "_dns64", "dns64", ".", "." };
	const char *sep = ": view ";
	const char *viewname = view->name;
	const unsigned char *s6;
	dns_fixedname_t fixed;
	dns_name_t *name;
	dns_zone_t *zone = NULL;
	int dns64_dbtypec = 4;
	isc_buffer_t b;
	isc_result_t result;

	REQUIRE(prefixlen == 32 || prefixlen == 40 || prefixlen == 48 ||
		prefixlen == 56 || prefixlen == 64 || prefixlen == 96);

	if (!strcmp(viewname, "_default")) {
		sep = "";
		viewname = "";
	}

	/*
	 * Construct the reverse name of the zone.
	 */
	s6 = na->type.in6.s6_addr;
	while (prefixlen > 0) {
		prefixlen -= 8;
		snprintf(buf, sizeof(buf), "%x.%x.", s6[prefixlen / 8] & 0xf,
			 (s6[prefixlen / 8] >> 4) & 0xf);
		strlcat(reverse, buf, sizeof(reverse));
	}
	strlcat(reverse, "ip6.arpa.", sizeof(reverse));

	/*
	 * Create the actual zone.
	 */
	if (server != NULL) {
		dns64_dbtype[2] = server;
	}
	if (contact != NULL) {
		dns64_dbtype[3] = contact;
	}
	name = dns_fixedname_initname(&fixed);
	isc_buffer_constinit(&b, reverse, strlen(reverse));
	isc_buffer_add(&b, strlen(reverse));
	CHECK(dns_name_fromtext(name, &b, dns_rootname, 0, NULL));
	dns_zone_create(&zone, mctx, 0);
	CHECK(dns_zone_setorigin(zone, name));
	dns_zone_setview(zone, view);
	CHECK(dns_zonemgr_managezone(named_g_server->zonemgr, zone));
	dns_zone_setclass(zone, view->rdclass);
	dns_zone_settype(zone, dns_zone_primary);
	dns_zone_setstats(zone, named_g_server->zonestats);
	dns_zone_setdbtype(zone, dns64_dbtypec, dns64_dbtype);
	if (view->queryacl != NULL) {
		dns_zone_setqueryacl(zone, view->queryacl);
	}
	if (view->queryonacl != NULL) {
		dns_zone_setqueryonacl(zone, view->queryonacl);
	}
	dns_zone_setdialup(zone, dns_dialuptype_no);
	dns_zone_setcheckdstype(zone, dns_checkdstype_no);
	dns_zone_setnotifytype(zone, dns_notifytype_no);
	dns_zone_setoption(zone, DNS_ZONEOPT_NOCHECKNS, true);
	setquerystats(zone, mctx, dns_zonestat_none);
	CHECK(dns_view_addzone(view, zone));
	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		      "dns64 reverse zone%s%s: %s", sep, viewname, reverse);

cleanup:
	if (zone != NULL) {
		dns_zone_detach(&zone);
	}
	return result;
}

#ifdef USE_DNSRPS
typedef struct conf_dnsrps_ctx conf_dnsrps_ctx_t;
struct conf_dnsrps_ctx {
	isc_result_t result;
	char *cstr;
	size_t cstr_size;
	isc_mem_t *mctx;
};

/*
 * Add to the DNSRPS configuration string.
 */
static bool
conf_dnsrps_sadd(conf_dnsrps_ctx_t *ctx, const char *p, ...) {
	size_t new_len, cur_len, new_cstr_size;
	char *new_cstr;
	va_list args;

	if (ctx->cstr == NULL) {
		ctx->cstr = isc_mem_get(ctx->mctx, 256);
		ctx->cstr[0] = '\0';
		ctx->cstr_size = 256;
	}

	cur_len = strlen(ctx->cstr);
	va_start(args, p);
	new_len = vsnprintf(ctx->cstr + cur_len, ctx->cstr_size - cur_len, p,
			    args) +
		  1;
	va_end(args);

	if (cur_len + new_len <= ctx->cstr_size) {
		return true;
	}

	new_cstr_size = ((cur_len + new_len) / 256 + 1) * 256;
	new_cstr = isc_mem_get(ctx->mctx, new_cstr_size);

	memmove(new_cstr, ctx->cstr, cur_len);
	isc_mem_put(ctx->mctx, ctx->cstr, ctx->cstr_size);
	ctx->cstr_size = new_cstr_size;
	ctx->cstr = new_cstr;

	/* cannot use args twice after a single va_start()on some systems */
	va_start(args, p);
	vsnprintf(ctx->cstr + cur_len, ctx->cstr_size - cur_len, p, args);
	va_end(args);
	return true;
}

/*
 * Get a DNSRPS configuration value using the global and view options
 * for the default.  Return false upon failure.
 */
static bool
conf_dnsrps_get(const cfg_obj_t **sub_obj, const cfg_obj_t **maps,
		const cfg_obj_t *obj, const char *name,
		conf_dnsrps_ctx_t *ctx) {
	if (ctx != NULL && ctx->result != ISC_R_SUCCESS) {
		*sub_obj = NULL;
		return false;
	}

	*sub_obj = cfg_tuple_get(obj, name);
	if (cfg_obj_isvoid(*sub_obj)) {
		*sub_obj = NULL;
		if (maps != NULL &&
		    ISC_R_SUCCESS != named_config_get(maps, name, sub_obj))
		{
			*sub_obj = NULL;
		}
	}
	return true;
}

/*
 * Handle a DNSRPS boolean configuration value with the global and view
 * options providing the default.
 */
static void
conf_dnsrps_yes_no(const cfg_obj_t *obj, const char *name,
		   conf_dnsrps_ctx_t *ctx) {
	const cfg_obj_t *sub_obj;

	if (!conf_dnsrps_get(&sub_obj, NULL, obj, name, ctx)) {
		return;
	}
	if (sub_obj == NULL) {
		return;
	}
	if (ctx == NULL) {
		cfg_obj_log(obj, named_g_lctx, ISC_LOG_ERROR,
			    "\"%s\" without \"dnsrps-enable yes\"", name);
		return;
	}

	conf_dnsrps_sadd(ctx, " %s %s", name,
			 cfg_obj_asboolean(sub_obj) ? "yes" : "no");
}

static void
conf_dnsrps_num(const cfg_obj_t *obj, const char *name,
		conf_dnsrps_ctx_t *ctx) {
	const cfg_obj_t *sub_obj;

	if (!conf_dnsrps_get(&sub_obj, NULL, obj, name, ctx)) {
		return;
	}
	if (sub_obj == NULL) {
		return;
	}
	if (ctx == NULL) {
		cfg_obj_log(obj, named_g_lctx, ISC_LOG_ERROR,
			    "\"%s\" without \"dnsrps-enable yes\"", name);
		return;
	}

	if (cfg_obj_isduration(sub_obj)) {
		conf_dnsrps_sadd(ctx, " %s %d", name,
				 cfg_obj_asduration(sub_obj));
	} else {
		conf_dnsrps_sadd(ctx, " %s %d", name,
				 cfg_obj_asuint32(sub_obj));
	}
}

/*
 * Convert the parsed RPZ configuration statement to a string for
 * dns_rpz_new_zones().
 */
static isc_result_t
conf_dnsrps(dns_view_t *view, const cfg_obj_t **maps, bool nsip_enabled,
	    bool nsdname_enabled, dns_rpz_zbits_t *nsip_on,
	    dns_rpz_zbits_t *nsdname_on, char **rps_cstr, size_t *rps_cstr_size,
	    const cfg_obj_t *rpz_obj, const cfg_listelt_t *zone_element) {
	conf_dnsrps_ctx_t ctx;
	const cfg_obj_t *zone_obj, *obj;
	dns_rpz_num_t rpz_num;
	bool on;
	const char *s;

	memset(&ctx, 0, sizeof(ctx));
	ctx.result = ISC_R_SUCCESS;
	ctx.mctx = view->mctx;

	for (rpz_num = 0; zone_element != NULL && ctx.result == ISC_R_SUCCESS;
	     ++rpz_num)
	{
		zone_obj = cfg_listelt_value(zone_element);

		s = cfg_obj_asstring(cfg_tuple_get(zone_obj, "zone name"));
		conf_dnsrps_sadd(&ctx, "zone \"%s\"", s);

		obj = cfg_tuple_get(zone_obj, "policy");
		if (!cfg_obj_isvoid(obj)) {
			s = cfg_obj_asstring(cfg_tuple_get(obj, "policy name"));
			conf_dnsrps_sadd(&ctx, " policy %s", s);
			if (strcasecmp(s, "cname") == 0) {
				s = cfg_obj_asstring(
					cfg_tuple_get(obj, "cname"));
				conf_dnsrps_sadd(&ctx, " %s", s);
			}
		}

		conf_dnsrps_yes_no(zone_obj, "recursive-only", &ctx);
		conf_dnsrps_yes_no(zone_obj, "log", &ctx);
		conf_dnsrps_num(zone_obj, "max-policy-ttl", &ctx);
		obj = cfg_tuple_get(rpz_obj, "nsip-enable");
		if (!cfg_obj_isvoid(obj)) {
			if (cfg_obj_asboolean(obj)) {
				*nsip_on |= DNS_RPZ_ZBIT(rpz_num);
			} else {
				*nsip_on &= ~DNS_RPZ_ZBIT(rpz_num);
			}
		}
		on = ((*nsip_on & DNS_RPZ_ZBIT(rpz_num)) != 0);
		if (nsip_enabled != on) {
			conf_dnsrps_sadd(&ctx, on ? " nsip-enable yes "
						  : " nsip-enable no ");
		}
		obj = cfg_tuple_get(rpz_obj, "nsdname-enable");
		if (!cfg_obj_isvoid(obj)) {
			if (cfg_obj_asboolean(obj)) {
				*nsdname_on |= DNS_RPZ_ZBIT(rpz_num);
			} else {
				*nsdname_on &= ~DNS_RPZ_ZBIT(rpz_num);
			}
		}
		on = ((*nsdname_on & DNS_RPZ_ZBIT(rpz_num)) != 0);
		if (nsdname_enabled != on) {
			conf_dnsrps_sadd(&ctx, on ? " nsdname-enable yes "
						  : " nsdname-enable no ");
		}
		conf_dnsrps_sadd(&ctx, ";\n");
		zone_element = cfg_list_next(zone_element);
	}

	conf_dnsrps_yes_no(rpz_obj, "recursive-only", &ctx);
	conf_dnsrps_num(rpz_obj, "max-policy-ttl", &ctx);
	conf_dnsrps_num(rpz_obj, "min-ns-dots", &ctx);
	conf_dnsrps_yes_no(rpz_obj, "qname-wait-recurse", &ctx);
	conf_dnsrps_yes_no(rpz_obj, "break-dnssec", &ctx);
	if (!nsip_enabled) {
		conf_dnsrps_sadd(&ctx, " nsip-enable no ");
	}
	if (!nsdname_enabled) {
		conf_dnsrps_sadd(&ctx, " nsdname-enable no ");
	}

	/*
	 * Get the general dnsrpzd parameters from the response-policy
	 * statement in the view and the general options.
	 */
	if (conf_dnsrps_get(&obj, maps, rpz_obj, "dnsrps-options", &ctx) &&
	    obj != NULL)
	{
		conf_dnsrps_sadd(&ctx, " %s\n", cfg_obj_asstring(obj));
	}

	if (ctx.result == ISC_R_SUCCESS) {
		*rps_cstr = ctx.cstr;
		*rps_cstr_size = ctx.cstr_size;
	} else {
		if (ctx.cstr != NULL) {
			isc_mem_put(ctx.mctx, ctx.cstr, ctx.cstr_size);
		}
		*rps_cstr = NULL;
		*rps_cstr_size = 0;
	}
	return ctx.result;
}
#endif /* ifdef USE_DNSRPS */

static isc_result_t
configure_rpz_name(dns_view_t *view, const cfg_obj_t *obj, dns_name_t *name,
		   const char *str, const char *msg) {
	isc_result_t result;

	result = dns_name_fromstring(name, str, dns_rootname, DNS_NAME_DOWNCASE,
				     view->mctx);
	if (result != ISC_R_SUCCESS) {
		cfg_obj_log(obj, named_g_lctx, DNS_RPZ_ERROR_LEVEL,
			    "invalid %s '%s'", msg, str);
	}
	return result;
}

static isc_result_t
configure_rpz_name2(dns_view_t *view, const cfg_obj_t *obj, dns_name_t *name,
		    const char *str, const dns_name_t *origin) {
	isc_result_t result;

	result = dns_name_fromstring(name, str, origin, DNS_NAME_DOWNCASE,
				     view->mctx);
	if (result != ISC_R_SUCCESS) {
		cfg_obj_log(obj, named_g_lctx, DNS_RPZ_ERROR_LEVEL,
			    "invalid zone '%s'", str);
	}
	return result;
}

static isc_result_t
configure_rpz_zone(dns_view_t *view, const cfg_listelt_t *element,
		   bool recursive_only_default, bool add_soa_default,
		   dns_ttl_t ttl_default, uint32_t minupdateinterval_default,
		   const dns_rpz_zone_t *old, bool *old_rpz_okp) {
	const cfg_obj_t *rpz_obj, *obj;
	const char *str;
	dns_rpz_zone_t *zone = NULL;
	isc_result_t result;
	dns_rpz_num_t rpz_num;

	REQUIRE(old != NULL || !*old_rpz_okp);

	rpz_obj = cfg_listelt_value(element);

	if (view->rpzs->p.num_zones >= DNS_RPZ_MAX_ZONES) {
		cfg_obj_log(rpz_obj, named_g_lctx, DNS_RPZ_ERROR_LEVEL,
			    "limit of %d response policy zones exceeded",
			    DNS_RPZ_MAX_ZONES);
		return ISC_R_FAILURE;
	}

	result = dns_rpz_new_zone(view->rpzs, &zone);
	if (result != ISC_R_SUCCESS) {
		cfg_obj_log(rpz_obj, named_g_lctx, DNS_RPZ_ERROR_LEVEL,
			    "Error creating new RPZ zone : %s",
			    isc_result_totext(result));
		return result;
	}

	obj = cfg_tuple_get(rpz_obj, "recursive-only");
	if (cfg_obj_isvoid(obj) ? recursive_only_default
				: cfg_obj_asboolean(obj))
	{
		view->rpzs->p.no_rd_ok &= ~DNS_RPZ_ZBIT(zone->num);
	} else {
		view->rpzs->p.no_rd_ok |= DNS_RPZ_ZBIT(zone->num);
	}

	obj = cfg_tuple_get(rpz_obj, "log");
	if (!cfg_obj_isvoid(obj) && !cfg_obj_asboolean(obj)) {
		view->rpzs->p.no_log |= DNS_RPZ_ZBIT(zone->num);
	} else {
		view->rpzs->p.no_log &= ~DNS_RPZ_ZBIT(zone->num);
	}

	obj = cfg_tuple_get(rpz_obj, "max-policy-ttl");
	if (cfg_obj_isduration(obj)) {
		zone->max_policy_ttl = cfg_obj_asduration(obj);
	} else {
		zone->max_policy_ttl = ttl_default;
	}
	if (*old_rpz_okp && zone->max_policy_ttl != old->max_policy_ttl) {
		*old_rpz_okp = false;
	}

	obj = cfg_tuple_get(rpz_obj, "min-update-interval");
	if (cfg_obj_isduration(obj)) {
		zone->min_update_interval = cfg_obj_asduration(obj);
	} else {
		zone->min_update_interval = minupdateinterval_default;
	}
	if (*old_rpz_okp &&
	    zone->min_update_interval != old->min_update_interval)
	{
		*old_rpz_okp = false;
	}

	str = cfg_obj_asstring(cfg_tuple_get(rpz_obj, "zone name"));
	result = configure_rpz_name(view, rpz_obj, &zone->origin, str, "zone");
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	if (dns_name_equal(&zone->origin, dns_rootname)) {
		cfg_obj_log(rpz_obj, named_g_lctx, DNS_RPZ_ERROR_LEVEL,
			    "invalid zone name '%s'", str);
		return DNS_R_EMPTYLABEL;
	}
	if (!view->rpzs->p.dnsrps_enabled) {
		for (rpz_num = 0; rpz_num < view->rpzs->p.num_zones - 1;
		     ++rpz_num)
		{
			if (dns_name_equal(&view->rpzs->zones[rpz_num]->origin,
					   &zone->origin))
			{
				cfg_obj_log(rpz_obj, named_g_lctx,
					    DNS_RPZ_ERROR_LEVEL,
					    "duplicate '%s'", str);
				result = DNS_R_DUPLICATE;
				return result;
			}
		}
	}
	if (*old_rpz_okp && !dns_name_equal(&old->origin, &zone->origin)) {
		*old_rpz_okp = false;
	}

	result = configure_rpz_name2(view, rpz_obj, &zone->client_ip,
				     DNS_RPZ_CLIENT_IP_ZONE, &zone->origin);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	result = configure_rpz_name2(view, rpz_obj, &zone->ip, DNS_RPZ_IP_ZONE,
				     &zone->origin);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	result = configure_rpz_name2(view, rpz_obj, &zone->nsdname,
				     DNS_RPZ_NSDNAME_ZONE, &zone->origin);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	result = configure_rpz_name2(view, rpz_obj, &zone->nsip,
				     DNS_RPZ_NSIP_ZONE, &zone->origin);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	result = configure_rpz_name(view, rpz_obj, &zone->passthru,
				    DNS_RPZ_PASSTHRU_NAME, "name");
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	result = configure_rpz_name(view, rpz_obj, &zone->drop,
				    DNS_RPZ_DROP_NAME, "name");
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	result = configure_rpz_name(view, rpz_obj, &zone->tcp_only,
				    DNS_RPZ_TCP_ONLY_NAME, "name");
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	obj = cfg_tuple_get(rpz_obj, "policy");
	if (cfg_obj_isvoid(obj)) {
		zone->policy = DNS_RPZ_POLICY_GIVEN;
	} else {
		str = cfg_obj_asstring(cfg_tuple_get(obj, "policy name"));
		zone->policy = dns_rpz_str2policy(str);
		INSIST(zone->policy != DNS_RPZ_POLICY_ERROR);
		if (zone->policy == DNS_RPZ_POLICY_CNAME) {
			str = cfg_obj_asstring(cfg_tuple_get(obj, "cname"));
			result = configure_rpz_name(view, rpz_obj, &zone->cname,
						    str, "cname");
			if (result != ISC_R_SUCCESS) {
				return result;
			}
		}
	}
	if (*old_rpz_okp && (zone->policy != old->policy ||
			     !dns_name_equal(&old->cname, &zone->cname)))
	{
		*old_rpz_okp = false;
	}

	obj = cfg_tuple_get(rpz_obj, "ede");
	if (!cfg_obj_isstring(obj)) {
		zone->ede = 0;
	} else {
		str = cfg_obj_asstring(obj);
		zone->ede = dns_rpz_str2ede(str);
		INSIST(zone->ede != UINT16_MAX);
	}
	if (*old_rpz_okp && zone->ede != old->ede) {
		*old_rpz_okp = false;
	}

	obj = cfg_tuple_get(rpz_obj, "add-soa");
	if (cfg_obj_isvoid(obj)) {
		zone->addsoa = add_soa_default;
	} else {
		zone->addsoa = cfg_obj_asboolean(obj);
	}
	if (*old_rpz_okp && zone->addsoa != old->addsoa) {
		*old_rpz_okp = false;
	}

	return ISC_R_SUCCESS;
}

static isc_result_t
configure_rpz(dns_view_t *view, dns_view_t *pview, const cfg_obj_t **maps,
	      const cfg_obj_t *rpz_obj, bool *old_rpz_okp) {
	bool dnsrps_enabled;
	const cfg_listelt_t *zone_element;
	char *rps_cstr;
	size_t rps_cstr_size;
	const cfg_obj_t *sub_obj;
	bool recursive_only_default, add_soa_default;
	bool nsip_enabled, nsdname_enabled;
	dns_rpz_zbits_t nsip_on, nsdname_on;
	dns_ttl_t ttl_default;
	uint32_t minupdateinterval_default;
	dns_rpz_zones_t *zones;
	const dns_rpz_zones_t *old;
	bool pview_must_detach = false;
	const dns_rpz_zone_t *old_zone;
	isc_result_t result;
	int i;

	*old_rpz_okp = false;

	zone_element = cfg_list_first(cfg_tuple_get(rpz_obj, "zone list"));
	if (zone_element == NULL) {
		return ISC_R_SUCCESS;
	}

	nsip_enabled = true;
	sub_obj = cfg_tuple_get(rpz_obj, "nsip-enable");
	if (!cfg_obj_isvoid(sub_obj)) {
		nsip_enabled = cfg_obj_asboolean(sub_obj);
	}
	nsip_on = nsip_enabled ? DNS_RPZ_ALL_ZBITS : 0;

	nsdname_enabled = true;
	sub_obj = cfg_tuple_get(rpz_obj, "nsdname-enable");
	if (!cfg_obj_isvoid(sub_obj)) {
		nsdname_enabled = cfg_obj_asboolean(sub_obj);
	}
	nsdname_on = nsdname_enabled ? DNS_RPZ_ALL_ZBITS : 0;

	/*
	 * "dnsrps-enable yes|no" can be either a global or response-policy
	 * clause.
	 */
	dnsrps_enabled = false;
	rps_cstr = NULL;
	rps_cstr_size = 0;
	sub_obj = NULL;
	(void)named_config_get(maps, "dnsrps-enable", &sub_obj);
	if (sub_obj != NULL) {
		dnsrps_enabled = cfg_obj_asboolean(sub_obj);
	}
	sub_obj = cfg_tuple_get(rpz_obj, "dnsrps-enable");
	if (!cfg_obj_isvoid(sub_obj)) {
		dnsrps_enabled = cfg_obj_asboolean(sub_obj);
	}
#ifndef USE_DNSRPS
	if (dnsrps_enabled) {
		cfg_obj_log(rpz_obj, named_g_lctx, DNS_RPZ_ERROR_LEVEL,
			    "\"dnsrps-enable yes\" but"
			    " without `./configure --enable-dnsrps`");
		return ISC_R_FAILURE;
	}
#else  /* ifndef USE_DNSRPS */
	if (dnsrps_enabled) {
		if (librpz == NULL) {
			cfg_obj_log(rpz_obj, named_g_lctx, DNS_RPZ_ERROR_LEVEL,
				    "\"dnsrps-enable yes\" but %s",
				    librpz_lib_open_emsg.c);
			return ISC_R_FAILURE;
		}

		/*
		 * Generate the DNS Response Policy Service
		 * configuration string.
		 */
		result = conf_dnsrps(view, maps, nsip_enabled, nsdname_enabled,
				     &nsip_on, &nsdname_on, &rps_cstr,
				     &rps_cstr_size, rpz_obj, zone_element);
		if (result != ISC_R_SUCCESS) {
			return result;
		}
	}
#endif /* ifndef USE_DNSRPS */

	result = dns_rpz_new_zones(view, named_g_loopmgr, rps_cstr,
				   rps_cstr_size, &view->rpzs);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	zones = view->rpzs;

	zones->p.nsip_on = nsip_on;
	zones->p.nsdname_on = nsdname_on;

	sub_obj = cfg_tuple_get(rpz_obj, "recursive-only");
	if (!cfg_obj_isvoid(sub_obj) && !cfg_obj_asboolean(sub_obj)) {
		recursive_only_default = false;
	} else {
		recursive_only_default = true;
	}

	sub_obj = cfg_tuple_get(rpz_obj, "add-soa");
	if (!cfg_obj_isvoid(sub_obj) && !cfg_obj_asboolean(sub_obj)) {
		add_soa_default = false;
	} else {
		add_soa_default = true;
	}

	sub_obj = cfg_tuple_get(rpz_obj, "break-dnssec");
	if (!cfg_obj_isvoid(sub_obj) && cfg_obj_asboolean(sub_obj)) {
		zones->p.break_dnssec = true;
	} else {
		zones->p.break_dnssec = false;
	}

	sub_obj = cfg_tuple_get(rpz_obj, "max-policy-ttl");
	if (cfg_obj_isduration(sub_obj)) {
		ttl_default = cfg_obj_asduration(sub_obj);
	} else {
		ttl_default = DNS_RPZ_MAX_TTL_DEFAULT;
	}

	sub_obj = cfg_tuple_get(rpz_obj, "min-update-interval");
	if (cfg_obj_isduration(sub_obj)) {
		minupdateinterval_default = cfg_obj_asduration(sub_obj);
	} else {
		minupdateinterval_default = DNS_RPZ_MINUPDATEINTERVAL_DEFAULT;
	}

	sub_obj = cfg_tuple_get(rpz_obj, "min-ns-dots");
	if (cfg_obj_isuint32(sub_obj)) {
		zones->p.min_ns_labels = cfg_obj_asuint32(sub_obj) + 1;
	} else {
		zones->p.min_ns_labels = 2;
	}

	sub_obj = cfg_tuple_get(rpz_obj, "qname-wait-recurse");
	if (cfg_obj_isvoid(sub_obj) || cfg_obj_asboolean(sub_obj)) {
		zones->p.qname_wait_recurse = true;
	} else {
		zones->p.qname_wait_recurse = false;
	}

	sub_obj = cfg_tuple_get(rpz_obj, "nsdname-wait-recurse");
	if (cfg_obj_isvoid(sub_obj) || cfg_obj_asboolean(sub_obj)) {
		zones->p.nsdname_wait_recurse = true;
	} else {
		zones->p.nsdname_wait_recurse = false;
	}

	sub_obj = cfg_tuple_get(rpz_obj, "nsip-wait-recurse");
	if (cfg_obj_isvoid(sub_obj) || cfg_obj_asboolean(sub_obj)) {
		zones->p.nsip_wait_recurse = true;
	} else {
		zones->p.nsip_wait_recurse = false;
	}

	if (pview != NULL) {
		old = pview->rpzs;
	} else {
		result = dns_viewlist_find(&named_g_server->viewlist,
					   view->name, view->rdclass, &pview);
		if (result == ISC_R_SUCCESS) {
			pview_must_detach = true;
			old = pview->rpzs;
		} else {
			old = NULL;
		}
	}

	if (old == NULL) {
		*old_rpz_okp = false;
	} else {
		*old_rpz_okp = true;
	}

	for (i = 0; zone_element != NULL;
	     ++i, zone_element = cfg_list_next(zone_element))
	{
		INSIST(!*old_rpz_okp || old != NULL);
		if (*old_rpz_okp && i < old->p.num_zones) {
			old_zone = old->zones[i];
		} else {
			*old_rpz_okp = false;
			old_zone = NULL;
		}
		result = configure_rpz_zone(
			view, zone_element, recursive_only_default,
			add_soa_default, ttl_default, minupdateinterval_default,
			old_zone, old_rpz_okp);
		if (result != ISC_R_SUCCESS) {
			if (pview_must_detach) {
				dns_view_detach(&pview);
			}
			return result;
		}
	}

	/*
	 * If this is a reloading and the parameters and list of policy
	 * zones are unchanged, then use the same policy data.
	 * Data for individual zones that must be reloaded will be merged.
	 */
	if (*old_rpz_okp) {
		if (old != NULL &&
		    memcmp(&old->p, &zones->p, sizeof(zones->p)) != 0)
		{
			*old_rpz_okp = false;
		} else if ((old == NULL || old->rps_cstr == NULL) !=
			   (zones->rps_cstr == NULL))
		{
			*old_rpz_okp = false;
		} else if (old != NULL && zones->rps_cstr != NULL &&
			   strcmp(old->rps_cstr, zones->rps_cstr) != 0)
		{
			*old_rpz_okp = false;
		}
	}

	if (*old_rpz_okp) {
		dns_rpz_zones_shutdown(view->rpzs);
		dns_rpz_zones_detach(&view->rpzs);
		dns_rpz_zones_attach(pview->rpzs, &view->rpzs);
		dns_rpz_zones_detach(&pview->rpzs);
	} else if (old != NULL && pview != NULL) {
		++pview->rpzs->rpz_ver;
		view->rpzs->rpz_ver = pview->rpzs->rpz_ver;
		cfg_obj_log(rpz_obj, named_g_lctx, DNS_RPZ_DEBUG_LEVEL1,
			    "updated RPZ policy: version %d",
			    view->rpzs->rpz_ver);
	}

	if (pview_must_detach) {
		dns_view_detach(&pview);
	}

	return ISC_R_SUCCESS;
}

static void
catz_addmodzone_cb(void *arg) {
	catz_chgzone_t *cz = (catz_chgzone_t *)arg;
	isc_result_t result;
	dns_forwarders_t *dnsforwarders = NULL;
	dns_name_t *name = NULL;
	isc_buffer_t namebuf;
	isc_buffer_t *confbuf = NULL;
	char nameb[DNS_NAME_FORMATSIZE];
	const cfg_obj_t *zlist = NULL;
	cfg_obj_t *zoneconf = NULL;
	cfg_obj_t *zoneobj = NULL;
	ns_cfgctx_t *cfg = NULL;
	dns_zone_t *zone = NULL;

	if (isc_loop_shuttingdown(isc_loop_get(named_g_loopmgr, isc_tid()))) {
		goto cleanup;
	}

	/*
	 * A non-empty 'catalog-zones' statement implies that 'allow-new-zones'
	 * is true, so this is expected to be non-NULL.
	 */
	cfg = (ns_cfgctx_t *)cz->view->new_zone_config;
	if (cfg == NULL) {
		CHECK(ISC_R_FAILURE);
	}

	name = dns_catz_entry_getname(cz->entry);

	isc_buffer_init(&namebuf, nameb, DNS_NAME_FORMATSIZE);
	dns_name_totext(name, DNS_NAME_OMITFINALDOT, &namebuf);
	isc_buffer_putuint8(&namebuf, 0);

	result = dns_fwdtable_find(cz->view->fwdtable, name, &dnsforwarders);
	if (result == ISC_R_SUCCESS &&
	    dnsforwarders->fwdpolicy == dns_fwdpolicy_only)
	{
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
			      "catz: catz_addmodzone_cb: "
			      "zone '%s' will not be processed because of the "
			      "explicitly configured forwarding for that zone",
			      nameb);
		goto cleanup;
	}

	result = dns_view_findzone(cz->view, name, DNS_ZTFIND_EXACT, &zone);

	if (cz->mod) {
		dns_catz_zone_t *parentcatz;

		if (result != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
				      "catz: error \"%s\" while trying to "
				      "modify zone '%s'",
				      isc_result_totext(result), nameb);
			goto cleanup;
		}

		if (!dns_zone_getadded(zone)) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
				      "catz: catz_addmodzone_cb: "
				      "zone '%s' is not a dynamically "
				      "added zone",
				      nameb);
			goto cleanup;
		}

		parentcatz = dns_zone_get_parentcatz(zone);

		if (parentcatz == NULL) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
				      "catz: catz_addmodzone_cb: "
				      "zone '%s' exists and is not added by "
				      "a catalog zone, so won't be modified",
				      nameb);
			goto cleanup;
		}
		if (parentcatz != cz->origin) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
				      "catz: catz_addmodzone_cb: "
				      "zone '%s' exists in multiple "
				      "catalog zones",
				      nameb);
			goto cleanup;
		}

		dns_zone_detach(&zone);
	} else {
		/* Zone shouldn't already exist when adding */
		if (result == ISC_R_SUCCESS) {
			if (dns_zone_get_parentcatz(zone) == NULL) {
				isc_log_write(
					named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
					NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
					"catz: "
					"catz_addmodzone_cb: "
					"zone '%s' will not be added "
					"because it is an explicitly "
					"configured zone",
					nameb);
			} else {
				isc_log_write(
					named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
					NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
					"catz: "
					"catz_addmodzone_cb: "
					"zone '%s' will not be added "
					"because another catalog zone "
					"already contains an entry with "
					"that zone",
					nameb);
			}
			goto cleanup;
		} else {
			RUNTIME_CHECK(result == ISC_R_NOTFOUND);
		}
	}
	RUNTIME_CHECK(zone == NULL);
	/* Create a config for new zone */
	confbuf = NULL;
	result = dns_catz_generate_zonecfg(cz->origin, cz->entry, &confbuf);
	if (result == ISC_R_SUCCESS) {
		cfg_parser_reset(cfg->add_parser);
		result = cfg_parse_buffer(cfg->add_parser, confbuf, "catz", 0,
					  &cfg_type_addzoneconf, 0, &zoneconf);
		isc_buffer_free(&confbuf);
	}
	/*
	 * Fail if either dns_catz_generate_zonecfg() or cfg_parse_buffer()
	 * failed.
	 */
	if (result != ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "catz: error \"%s\" while trying to generate "
			      "config for zone '%s'",
			      isc_result_totext(result), nameb);
		goto cleanup;
	}
	CHECK(cfg_map_get(zoneconf, "zone", &zlist));
	if (!cfg_obj_islist(zlist)) {
		CHECK(ISC_R_FAILURE);
	}

	/* For now we only support adding one zone at a time */
	zoneobj = cfg_listelt_value(cfg_list_first(zlist));

	/* Mark view unfrozen so that zone can be added */
	isc_loopmgr_pause(named_g_loopmgr);
	dns_view_thaw(cz->view);
	result = configure_zone(cfg->config, zoneobj, cfg->vconfig, cz->view,
				&cz->cbd->server->viewlist,
				&cz->cbd->server->kasplist,
				&cz->cbd->server->keystorelist, cfg->actx, true,
				false, true, cz->mod);
	dns_view_freeze(cz->view);
	isc_loopmgr_resume(named_g_loopmgr);

	if (result != ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
			      "catz: failed to configure zone '%s' - %d", nameb,
			      result);
		goto cleanup;
	}

	/* Is it there yet? */
	CHECK(dns_view_findzone(cz->view, name, DNS_ZTFIND_EXACT, &zone));

	/*
	 * Load the zone from the master file.	If this fails, we'll
	 * need to undo the configuration we've done already.
	 */
	result = dns_zone_load(zone, true);
	if (result != ISC_R_SUCCESS) {
		dns_db_t *dbp = NULL;
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "catz: dns_zone_load() failed "
			      "with %s; reverting.",
			      isc_result_totext(result));

		/* If the zone loaded partially, unload it */
		if (dns_zone_getdb(zone, &dbp) == ISC_R_SUCCESS) {
			dns_db_detach(&dbp);
			dns_zone_unload(zone);
		}

		/* Remove the zone from the zone table */
		dns_view_delzone(cz->view, zone);
		goto cleanup;
	}

	/* Flag the zone as having been added at runtime */
	dns_zone_setadded(zone, true);
	dns_zone_set_parentcatz(zone, cz->origin);

cleanup:
	if (zone != NULL) {
		dns_zone_detach(&zone);
	}
	if (zoneconf != NULL) {
		cfg_obj_destroy(cfg->add_parser, &zoneconf);
	}
	if (dnsforwarders != NULL) {
		dns_forwarders_detach(&dnsforwarders);
	}
	dns_catz_entry_detach(cz->origin, &cz->entry);
	dns_catz_zone_detach(&cz->origin);
	dns_view_weakdetach(&cz->view);
	isc_mem_putanddetach(&cz->mctx, cz, sizeof(*cz));
}

static void
catz_delzone_cb(void *arg) {
	catz_chgzone_t *cz = (catz_chgzone_t *)arg;
	isc_result_t result;
	dns_zone_t *zone = NULL;
	dns_db_t *dbp = NULL;
	char cname[DNS_NAME_FORMATSIZE];
	const char *file = NULL;

	if (isc_loop_shuttingdown(isc_loop_get(named_g_loopmgr, isc_tid()))) {
		goto cleanup;
	}

	isc_loopmgr_pause(named_g_loopmgr);

	dns_name_format(dns_catz_entry_getname(cz->entry), cname,
			DNS_NAME_FORMATSIZE);
	result = dns_view_findzone(cz->view, dns_catz_entry_getname(cz->entry),
				   DNS_ZTFIND_EXACT, &zone);
	if (result != ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
			      "catz: catz_delzone_cb: "
			      "zone '%s' not found",
			      cname);
		goto resume;
	}

	if (!dns_zone_getadded(zone)) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
			      "catz: catz_delzone_cb: "
			      "zone '%s' is not a dynamically added zone",
			      cname);
		goto resume;
	}

	if (dns_zone_get_parentcatz(zone) != cz->origin) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
			      "catz: catz_delzone_cb: zone "
			      "'%s' exists in multiple catalog zones",
			      cname);
		goto resume;
	}

	/* Stop answering for this zone */
	if (dns_zone_getdb(zone, &dbp) == ISC_R_SUCCESS) {
		dns_db_detach(&dbp);
		dns_zone_unload(zone);
	}

	if (dns_view_delzone(cz->view, zone) != ISC_R_SUCCESS) {
		goto resume;
	}
	file = dns_zone_getfile(zone);
	if (file != NULL) {
		isc_file_remove(file);
		file = dns_zone_getjournal(zone);
		if (file != NULL) {
			isc_file_remove(file);
		}
	}

	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
		      "catz: catz_delzone_cb: "
		      "zone '%s' deleted",
		      cname);
resume:
	isc_loopmgr_resume(named_g_loopmgr);
cleanup:
	if (zone != NULL) {
		dns_zone_detach(&zone);
	}
	dns_catz_entry_detach(cz->origin, &cz->entry);
	dns_catz_zone_detach(&cz->origin);
	dns_view_weakdetach(&cz->view);
	isc_mem_putanddetach(&cz->mctx, cz, sizeof(*cz));
}

static isc_result_t
catz_run(dns_catz_entry_t *entry, dns_catz_zone_t *origin, dns_view_t *view,
	 void *udata, catz_type_t type) {
	catz_chgzone_t *cz = NULL;
	isc_job_cb action = NULL;

	switch (type) {
	case CATZ_ADDZONE:
	case CATZ_MODZONE:
		action = catz_addmodzone_cb;
		break;
	case CATZ_DELZONE:
		action = catz_delzone_cb;
		break;
	default:
		REQUIRE(0);
		UNREACHABLE();
	}

	cz = isc_mem_get(view->mctx, sizeof(*cz));
	*cz = (catz_chgzone_t){
		.cbd = (catz_cb_data_t *)udata,
		.mod = (type == CATZ_MODZONE),
	};
	isc_mem_attach(view->mctx, &cz->mctx);

	dns_catz_entry_attach(entry, &cz->entry);
	dns_catz_zone_attach(origin, &cz->origin);
	dns_view_weakattach(view, &cz->view);

	isc_async_run(named_g_mainloop, action, cz);

	return ISC_R_SUCCESS;
}

static isc_result_t
catz_addzone(dns_catz_entry_t *entry, dns_catz_zone_t *origin, dns_view_t *view,
	     void *udata) {
	return catz_run(entry, origin, view, udata, CATZ_ADDZONE);
}

static isc_result_t
catz_delzone(dns_catz_entry_t *entry, dns_catz_zone_t *origin, dns_view_t *view,
	     void *udata) {
	return catz_run(entry, origin, view, udata, CATZ_DELZONE);
}

static isc_result_t
catz_modzone(dns_catz_entry_t *entry, dns_catz_zone_t *origin, dns_view_t *view,
	     void *udata) {
	return catz_run(entry, origin, view, udata, CATZ_MODZONE);
}

static void
catz_changeview(dns_catz_entry_t *entry, void *arg1, void *arg2) {
	dns_view_t *pview = arg1;
	dns_view_t *view = arg2;

	dns_zone_t *zone = NULL;
	isc_result_t result = dns_view_findzone(
		pview, dns_catz_entry_getname(entry), DNS_ZTFIND_EXACT, &zone);

	if (result != ISC_R_SUCCESS) {
		return;
	}

	dns_zone_setview(zone, view);
	dns_view_addzone(view, zone);

	dns_zone_detach(&zone);
}

static void
catz_reconfigure(dns_catz_entry_t *entry, void *arg1, void *arg2) {
	dns_view_t *view = arg1;
	catz_reconfig_data_t *data = arg2;
	isc_buffer_t namebuf;
	isc_buffer_t *confbuf = NULL;
	const cfg_obj_t *zlist = NULL;
	char nameb[DNS_NAME_FORMATSIZE];
	cfg_obj_t *zoneconf = NULL;
	cfg_obj_t *zoneobj = NULL;
	ns_cfgctx_t *cfg = NULL;
	dns_zone_t *zone = NULL;
	isc_result_t result;

	isc_buffer_init(&namebuf, nameb, DNS_NAME_FORMATSIZE);
	dns_name_totext(dns_catz_entry_getname(entry), DNS_NAME_OMITFINALDOT,
			&namebuf);
	isc_buffer_putuint8(&namebuf, 0);

	result = dns_view_findzone(view, dns_catz_entry_getname(entry),
				   DNS_ZTFIND_EXACT, &zone);
	if (result != ISC_R_SUCCESS) {
		return;
	}

	/*
	 * A non-empty 'catalog-zones' statement implies that 'allow-new-zones'
	 * is true, so this is expected to be non-NULL.
	 */
	cfg = (ns_cfgctx_t *)view->new_zone_config;
	if (cfg == NULL) {
		CHECK(ISC_R_FAILURE);
	}

	result = dns_catz_generate_zonecfg(data->catz, entry, &confbuf);
	if (result == ISC_R_SUCCESS) {
		cfg_parser_reset(cfg->add_parser);
		result = cfg_parse_buffer(cfg->add_parser, confbuf, "catz", 0,
					  &cfg_type_addzoneconf, 0, &zoneconf);
		isc_buffer_free(&confbuf);
	}
	/*
	 * Fail if either dns_catz_generate_zonecfg() or cfg_parse_buffer()
	 * failed.
	 */
	if (result != ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "catz_reconfigure: error \"%s\" while trying to "
			      "generate config for member zone '%s'",
			      isc_result_totext(result), nameb);
		goto cleanup;
	}

	CHECK(cfg_map_get(zoneconf, "zone", &zlist));
	if (!cfg_obj_islist(zlist)) {
		CHECK(ISC_R_FAILURE);
	}
	zoneobj = cfg_listelt_value(cfg_list_first(zlist));

	result = configure_zone(data->config, zoneobj, cfg->vconfig, view,
				&data->cbd->server->viewlist,
				&data->cbd->server->kasplist,
				&data->cbd->server->keystorelist, cfg->actx,
				true, false, true, true);
	if (result != ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "catz_reconfigure : error \"%s\" while trying to "
			      "reconfigure member zone '%s'",
			      isc_result_totext(result), nameb);
		goto cleanup;
	}

cleanup:
	if (zoneconf != NULL) {
		cfg_obj_destroy(cfg->add_parser, &zoneconf);
	}

	dns_zone_detach(&zone);
}

static isc_result_t
configure_catz_zone(dns_view_t *view, dns_view_t *pview,
		    const cfg_obj_t *config, const cfg_listelt_t *element) {
	const cfg_obj_t *catz_obj, *obj;
	dns_catz_zone_t *zone = NULL;
	const char *str;
	isc_result_t result;
	dns_name_t origin;
	dns_catz_options_t *opts;

	dns_name_init(&origin, NULL);
	catz_obj = cfg_listelt_value(element);

	str = cfg_obj_asstring(cfg_tuple_get(catz_obj, "zone name"));

	result = dns_name_fromstring(&origin, str, dns_rootname,
				     DNS_NAME_DOWNCASE, view->mctx);
	if (result == ISC_R_SUCCESS && dns_name_equal(&origin, dns_rootname)) {
		result = DNS_R_EMPTYLABEL;
	}

	if (result != ISC_R_SUCCESS) {
		cfg_obj_log(catz_obj, named_g_lctx, DNS_CATZ_ERROR_LEVEL,
			    "catz: invalid zone name '%s'", str);
		goto cleanup;
	}

	result = dns_catz_zone_add(view->catzs, &origin, &zone);
	if (result == ISC_R_EXISTS) {
		catz_reconfig_data_t data = {
			.catz = zone,
			.config = config,
			.cbd = (catz_cb_data_t *)dns_catz_zones_get_udata(
				view->catzs),
		};

		/*
		 * We have to walk through all the member zones, re-attach
		 * them to the current view and reconfigure
		 */
		dns_catz_zone_for_each_entry2(zone, catz_changeview, pview,
					      view);
		dns_catz_zone_for_each_entry2(zone, catz_reconfigure, view,
					      &data);
	}

	dns_catz_zone_resetdefoptions(zone);
	opts = dns_catz_zone_getdefoptions(zone);

	obj = cfg_tuple_get(catz_obj, "default-masters");
	if (obj == NULL || !cfg_obj_istuple(obj)) {
		obj = cfg_tuple_get(catz_obj, "default-primaries");
	}
	if (obj != NULL && cfg_obj_istuple(obj)) {
		result = named_config_getipandkeylist(config, obj, view->mctx,
						      &opts->masters);
	}

	obj = cfg_tuple_get(catz_obj, "in-memory");
	if (obj != NULL && cfg_obj_isboolean(obj)) {
		opts->in_memory = cfg_obj_asboolean(obj);
	}

	obj = cfg_tuple_get(catz_obj, "zone-directory");
	if (!opts->in_memory && obj != NULL && cfg_obj_isstring(obj)) {
		opts->zonedir = isc_mem_strdup(view->mctx,
					       cfg_obj_asstring(obj));
		if (isc_file_isdirectory(opts->zonedir) != ISC_R_SUCCESS) {
			cfg_obj_log(obj, named_g_lctx, DNS_CATZ_ERROR_LEVEL,
				    "catz: zone-directory '%s' "
				    "not found; zone files will not be "
				    "saved",
				    opts->zonedir);
			opts->in_memory = true;
		}
	}

	obj = cfg_tuple_get(catz_obj, "min-update-interval");
	if (obj != NULL && cfg_obj_isduration(obj)) {
		opts->min_update_interval = cfg_obj_asduration(obj);
	}

cleanup:
	dns_name_free(&origin, view->mctx);

	return result;
}

static catz_cb_data_t ns_catz_cbdata;
static dns_catz_zonemodmethods_t ns_catz_zonemodmethods = {
	catz_addzone, catz_modzone, catz_delzone, &ns_catz_cbdata
};

static isc_result_t
configure_catz(dns_view_t *view, dns_view_t *pview, const cfg_obj_t *config,
	       const cfg_obj_t *catz_obj) {
	const cfg_listelt_t *zone_element = NULL;
	const dns_catz_zones_t *old = NULL;
	bool pview_must_detach = false;
	isc_result_t result;

	/* xxxwpk TODO do it cleaner, once, somewhere */
	ns_catz_cbdata.server = named_g_server;

	zone_element = cfg_list_first(cfg_tuple_get(catz_obj, "zone list"));
	if (zone_element == NULL) {
		return ISC_R_SUCCESS;
	}

	if (pview != NULL) {
		old = pview->catzs;
	} else {
		result = dns_viewlist_find(&named_g_server->viewlist,
					   view->name, view->rdclass, &pview);
		if (result == ISC_R_SUCCESS) {
			pview_must_detach = true;
			old = pview->catzs;
		}
	}

	if (old != NULL) {
		dns_catz_zones_attach(pview->catzs, &view->catzs);
		dns_catz_zones_detach(&pview->catzs);
		dns_catz_prereconfig(view->catzs);
	} else {
		view->catzs = dns_catz_zones_new(view->mctx, named_g_loopmgr,
						 &ns_catz_zonemodmethods);
	}

	while (zone_element != NULL) {
		CHECK(configure_catz_zone(view, pview, config, zone_element));
		zone_element = cfg_list_next(zone_element);
	}

	if (old != NULL) {
		dns_catz_postreconfig(view->catzs);
	}

	result = ISC_R_SUCCESS;

cleanup:
	if (pview_must_detach) {
		dns_view_detach(&pview);
	}

	return result;
}

#define CHECK_RRL(cond, pat, val1, val2)                                   \
	do {                                                               \
		if (!(cond)) {                                             \
			cfg_obj_log(obj, named_g_lctx, ISC_LOG_ERROR, pat, \
				    val1, val2);                           \
			result = ISC_R_RANGE;                              \
			goto cleanup;                                      \
		}                                                          \
	} while (0)

#define CHECK_RRL_RATE(rate, def, max_rate, name)                           \
	do {                                                                \
		obj = NULL;                                                 \
		rrl->rate.str = name;                                       \
		result = cfg_map_get(map, name, &obj);                      \
		if (result == ISC_R_SUCCESS) {                              \
			rrl->rate.r = cfg_obj_asuint32(obj);                \
			CHECK_RRL(rrl->rate.r <= max_rate, name " %d > %d", \
				  rrl->rate.r, max_rate);                   \
		} else {                                                    \
			rrl->rate.r = def;                                  \
		}                                                           \
		rrl->rate.scaled = rrl->rate.r;                             \
	} while (0)

static isc_result_t
configure_rrl(dns_view_t *view, const cfg_obj_t *config, const cfg_obj_t *map) {
	const cfg_obj_t *obj;
	dns_rrl_t *rrl;
	isc_result_t result;
	int min_entries, i, j;

	/*
	 * Most DNS servers have few clients, but intentinally open
	 * recursive and authoritative servers often have many.
	 * So start with a small number of entries unless told otherwise
	 * to reduce cold-start costs.
	 */
	min_entries = 500;
	obj = NULL;
	result = cfg_map_get(map, "min-table-size", &obj);
	if (result == ISC_R_SUCCESS) {
		min_entries = cfg_obj_asuint32(obj);
		if (min_entries < 1) {
			min_entries = 1;
		}
	}
	result = dns_rrl_init(&rrl, view, min_entries);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	i = ISC_MAX(20000, min_entries);
	obj = NULL;
	result = cfg_map_get(map, "max-table-size", &obj);
	if (result == ISC_R_SUCCESS) {
		i = cfg_obj_asuint32(obj);
		CHECK_RRL(i >= min_entries,
			  "max-table-size %d < min-table-size %d", i,
			  min_entries);
	}
	rrl->max_entries = i;

	CHECK_RRL_RATE(responses_per_second, 0, DNS_RRL_MAX_RATE,
		       "responses-per-second");
	CHECK_RRL_RATE(referrals_per_second, rrl->responses_per_second.r,
		       DNS_RRL_MAX_RATE, "referrals-per-second");
	CHECK_RRL_RATE(nodata_per_second, rrl->responses_per_second.r,
		       DNS_RRL_MAX_RATE, "nodata-per-second");
	CHECK_RRL_RATE(nxdomains_per_second, rrl->responses_per_second.r,
		       DNS_RRL_MAX_RATE, "nxdomains-per-second");
	CHECK_RRL_RATE(errors_per_second, rrl->responses_per_second.r,
		       DNS_RRL_MAX_RATE, "errors-per-second");

	CHECK_RRL_RATE(all_per_second, 0, DNS_RRL_MAX_RATE, "all-per-second");

	CHECK_RRL_RATE(slip, 2, DNS_RRL_MAX_SLIP, "slip");

	i = 15;
	obj = NULL;
	result = cfg_map_get(map, "window", &obj);
	if (result == ISC_R_SUCCESS) {
		i = cfg_obj_asuint32(obj);
		CHECK_RRL(i >= 1 && i <= DNS_RRL_MAX_WINDOW,
			  "window %d < 1 or > %d", i, DNS_RRL_MAX_WINDOW);
	}
	rrl->window = i;

	i = 0;
	obj = NULL;
	result = cfg_map_get(map, "qps-scale", &obj);
	if (result == ISC_R_SUCCESS) {
		i = cfg_obj_asuint32(obj);
		CHECK_RRL(i >= 1, "invalid 'qps-scale %d'%s", i, "");
	}
	rrl->qps_scale = i;
	rrl->qps = 1.0;

	i = 24;
	obj = NULL;
	result = cfg_map_get(map, "ipv4-prefix-length", &obj);
	if (result == ISC_R_SUCCESS) {
		i = cfg_obj_asuint32(obj);
		CHECK_RRL(i >= 8 && i <= 32,
			  "invalid 'ipv4-prefix-length %d'%s", i, "");
	}
	rrl->ipv4_prefixlen = i;
	if (i == 32) {
		rrl->ipv4_mask = 0xffffffff;
	} else {
		rrl->ipv4_mask = htonl(0xffffffff << (32 - i));
	}

	i = 56;
	obj = NULL;
	result = cfg_map_get(map, "ipv6-prefix-length", &obj);
	if (result == ISC_R_SUCCESS) {
		i = cfg_obj_asuint32(obj);
		CHECK_RRL(i >= 16 && i <= DNS_RRL_MAX_PREFIX,
			  "ipv6-prefix-length %d < 16 or > %d", i,
			  DNS_RRL_MAX_PREFIX);
	}
	rrl->ipv6_prefixlen = i;
	for (j = 0; j < 4; ++j) {
		if (i <= 0) {
			rrl->ipv6_mask[j] = 0;
		} else if (i < 32) {
			rrl->ipv6_mask[j] = htonl(0xffffffff << (32 - i));
		} else {
			rrl->ipv6_mask[j] = 0xffffffff;
		}
		i -= 32;
	}

	obj = NULL;
	result = cfg_map_get(map, "exempt-clients", &obj);
	if (result == ISC_R_SUCCESS) {
		result = cfg_acl_fromconfig(obj, config, named_g_lctx,
					    named_g_aclconfctx, named_g_mctx, 0,
					    &rrl->exempt);
		CHECK_RRL(result == ISC_R_SUCCESS, "invalid %s%s",
			  "address match list", "");
	}

	obj = NULL;
	result = cfg_map_get(map, "log-only", &obj);
	if (result == ISC_R_SUCCESS && cfg_obj_asboolean(obj)) {
		rrl->log_only = true;
	} else {
		rrl->log_only = false;
	}

	return ISC_R_SUCCESS;

cleanup:
	dns_rrl_view_destroy(view);
	return result;
}

static isc_result_t
add_soa(dns_db_t *db, dns_dbversion_t *version, const dns_name_t *name,
	const dns_name_t *origin, const dns_name_t *contact) {
	dns_dbnode_t *node = NULL;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	dns_rdatalist_t rdatalist;
	dns_rdataset_t rdataset;
	isc_result_t result;
	unsigned char buf[DNS_SOA_BUFFERSIZE];

	CHECK(dns_soa_buildrdata(origin, contact, dns_db_class(db), 0, 28800,
				 7200, 604800, 86400, buf, &rdata));

	dns_rdatalist_init(&rdatalist);
	rdatalist.type = rdata.type;
	rdatalist.rdclass = rdata.rdclass;
	rdatalist.ttl = 86400;
	ISC_LIST_APPEND(rdatalist.rdata, &rdata, link);

	dns_rdataset_init(&rdataset);
	dns_rdatalist_tordataset(&rdatalist, &rdataset);
	CHECK(dns_db_findnode(db, name, true, &node));
	CHECK(dns_db_addrdataset(db, node, version, 0, &rdataset, 0, NULL));

cleanup:
	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}
	return result;
}

static isc_result_t
add_ns(dns_db_t *db, dns_dbversion_t *version, const dns_name_t *name,
       const dns_name_t *nsname) {
	dns_dbnode_t *node = NULL;
	dns_rdata_ns_t ns;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	dns_rdatalist_t rdatalist;
	dns_rdataset_t rdataset;
	isc_result_t result;
	isc_buffer_t b;
	unsigned char buf[DNS_NAME_MAXWIRE];

	isc_buffer_init(&b, buf, sizeof(buf));

	ns.common.rdtype = dns_rdatatype_ns;
	ns.common.rdclass = dns_db_class(db);
	ns.mctx = NULL;
	dns_name_init(&ns.name, NULL);
	dns_name_clone(nsname, &ns.name);
	CHECK(dns_rdata_fromstruct(&rdata, dns_db_class(db), dns_rdatatype_ns,
				   &ns, &b));

	dns_rdatalist_init(&rdatalist);
	rdatalist.type = rdata.type;
	rdatalist.rdclass = rdata.rdclass;
	rdatalist.ttl = 86400;
	ISC_LIST_APPEND(rdatalist.rdata, &rdata, link);

	dns_rdataset_init(&rdataset);
	dns_rdatalist_tordataset(&rdatalist, &rdataset);
	CHECK(dns_db_findnode(db, name, true, &node));
	CHECK(dns_db_addrdataset(db, node, version, 0, &rdataset, 0, NULL));

cleanup:
	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}
	return result;
}

static isc_result_t
create_empty_zone(dns_zone_t *pzone, dns_name_t *name, dns_view_t *view,
		  const cfg_obj_t *zonelist, const char **empty_dbtype,
		  int empty_dbtypec, dns_zonestat_level_t statlevel) {
	char namebuf[DNS_NAME_FORMATSIZE];
	const cfg_listelt_t *element;
	const cfg_obj_t *obj;
	const cfg_obj_t *zconfig;
	const cfg_obj_t *zoptions;
	const char *default_dbtype[4] = { ZONEDB_DEFAULT };
	const char *sep = ": view ";
	const char *str;
	const char *viewname = view->name;
	dns_db_t *db = NULL;
	dns_dbversion_t *version = NULL;
	dns_fixedname_t cfixed;
	dns_fixedname_t fixed;
	dns_fixedname_t nsfixed;
	dns_name_t *contact;
	dns_name_t *ns;
	dns_name_t *zname;
	dns_zone_t *zone = NULL;
	int default_dbtypec = 1;
	isc_result_t result;
	dns_namereln_t namereln;
	int order;
	unsigned int nlabels;

	zname = dns_fixedname_initname(&fixed);
	ns = dns_fixedname_initname(&nsfixed);
	contact = dns_fixedname_initname(&cfixed);

	/*
	 * Look for forward "zones" beneath this empty zone and if so
	 * create a custom db for the empty zone.
	 */
	for (element = cfg_list_first(zonelist); element != NULL;
	     element = cfg_list_next(element))
	{
		zconfig = cfg_listelt_value(element);
		str = cfg_obj_asstring(cfg_tuple_get(zconfig, "name"));
		CHECK(dns_name_fromstring(zname, str, dns_rootname, 0, NULL));
		namereln = dns_name_fullcompare(zname, name, &order, &nlabels);
		if (namereln != dns_namereln_subdomain) {
			continue;
		}

		zoptions = cfg_tuple_get(zconfig, "options");

		obj = NULL;
		(void)cfg_map_get(zoptions, "type", &obj);
		if (obj != NULL &&
		    strcasecmp(cfg_obj_asstring(obj), "forward") == 0)
		{
			obj = NULL;
			(void)cfg_map_get(zoptions, "forward", &obj);
			if (obj == NULL) {
				continue;
			}
			if (strcasecmp(cfg_obj_asstring(obj), "only") != 0) {
				continue;
			}
		}
		if (db == NULL) {
			CHECK(dns_db_create(view->mctx, ZONEDB_DEFAULT, name,
					    dns_dbtype_zone, view->rdclass, 0,
					    NULL, &db));
			CHECK(dns_db_newversion(db, &version));
			if (strcmp(empty_dbtype[2], "@") == 0) {
				dns_name_clone(name, ns);
			} else {
				CHECK(dns_name_fromstring(ns, empty_dbtype[2],
							  dns_rootname, 0,
							  NULL));
			}
			CHECK(dns_name_fromstring(contact, empty_dbtype[3],
						  dns_rootname, 0, NULL));
			CHECK(add_soa(db, version, name, ns, contact));
			CHECK(add_ns(db, version, name, ns));
		}
		CHECK(add_ns(db, version, zname, dns_rootname));
	}

	/*
	 * Is the existing zone ok to use?
	 */
	if (pzone != NULL) {
		unsigned int typec;
		const char **dbargv = NULL;

		if (db != NULL) {
			typec = default_dbtypec;
			dbargv = default_dbtype;
		} else {
			typec = empty_dbtypec;
			dbargv = empty_dbtype;
		}

		result = check_dbtype(pzone, typec, dbargv, view->mctx);
		if (result != ISC_R_SUCCESS) {
			pzone = NULL;
		}

		if (pzone != NULL &&
		    dns_zone_gettype(pzone) != dns_zone_primary)
		{
			pzone = NULL;
		}
		if (pzone != NULL && dns_zone_getfile(pzone) != NULL) {
			pzone = NULL;
		}
		if (pzone != NULL) {
			dns_zone_getraw(pzone, &zone);
			if (zone != NULL) {
				dns_zone_detach(&zone);
				pzone = NULL;
			}
		}
	}

	if (pzone == NULL) {
		CHECK(dns_zonemgr_createzone(named_g_server->zonemgr, &zone));
		CHECK(dns_zone_setorigin(zone, name));
		CHECK(dns_zonemgr_managezone(named_g_server->zonemgr, zone));
		if (db == NULL) {
			dns_zone_setdbtype(zone, empty_dbtypec, empty_dbtype);
		}
		dns_zone_setclass(zone, view->rdclass);
		dns_zone_settype(zone, dns_zone_primary);
		dns_zone_setstats(zone, named_g_server->zonestats);
	} else {
		dns_zone_attach(pzone, &zone);
	}

	dns_zone_setoption(zone, ~DNS_ZONEOPT_NOCHECKNS, false);
	dns_zone_setoption(zone, DNS_ZONEOPT_NOCHECKNS, true);
	dns_zone_setcheckdstype(zone, dns_checkdstype_no);
	dns_zone_setnotifytype(zone, dns_notifytype_no);
	dns_zone_setdialup(zone, dns_dialuptype_no);
	dns_zone_setautomatic(zone, true);
	if (view->queryacl != NULL) {
		dns_zone_setqueryacl(zone, view->queryacl);
	} else {
		dns_zone_clearqueryacl(zone);
	}
	if (view->queryonacl != NULL) {
		dns_zone_setqueryonacl(zone, view->queryonacl);
	} else {
		dns_zone_clearqueryonacl(zone);
	}
	dns_zone_clearupdateacl(zone);
	if (view->transferacl != NULL) {
		dns_zone_setxfracl(zone, view->transferacl);
	} else {
		dns_zone_clearxfracl(zone);
	}

	setquerystats(zone, view->mctx, statlevel);
	if (db != NULL) {
		dns_db_closeversion(db, &version, true);
		CHECK(dns_zone_replacedb(zone, db, false));
	}
	dns_zone_setoption(zone, DNS_ZONEOPT_AUTOEMPTY, true);
	dns_zone_setview(zone, view);
	CHECK(dns_view_addzone(view, zone));

	if (!strcmp(viewname, "_default")) {
		sep = "";
		viewname = "";
	}
	dns_name_format(name, namebuf, sizeof(namebuf));
	isc_log_write(named_g_lctx, DNS_LOGCATEGORY_ZONELOAD,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		      "automatic empty zone%s%s: %s", sep, viewname, namebuf);

cleanup:
	if (zone != NULL) {
		dns_zone_detach(&zone);
	}
	if (version != NULL) {
		dns_db_closeversion(db, &version, false);
	}
	if (db != NULL) {
		dns_db_detach(&db);
	}

	INSIST(version == NULL);

	return result;
}

static isc_result_t
create_ipv4only_zone(dns_zone_t *pzone, dns_view_t *view,
		     const dns_name_t *name, const char *type, isc_mem_t *mctx,
		     const char *server, const char *contact) {
	char namebuf[DNS_NAME_FORMATSIZE];
	const char *dbtype[4] = { "_builtin", NULL, "@", "." };
	const char *sep = ": view ";
	const char *viewname = view->name;
	dns_zone_t *zone = NULL;
	int dbtypec = 4;
	isc_result_t result;

	REQUIRE(type != NULL);

	if (!strcmp(viewname, "_default")) {
		sep = "";
		viewname = "";
	}

	dbtype[1] = type;
	if (server != NULL) {
		dbtype[2] = server;
	}
	if (contact != NULL) {
		dbtype[3] = contact;
	}

	if (pzone != NULL) {
		result = check_dbtype(pzone, dbtypec, dbtype, view->mctx);
		if (result != ISC_R_SUCCESS) {
			pzone = NULL;
		}
	}

	if (pzone == NULL) {
		/*
		 * Create the actual zone.
		 */
		dns_zone_create(&zone, mctx, 0);
		CHECK(dns_zone_setorigin(zone, name));
		CHECK(dns_zonemgr_managezone(named_g_server->zonemgr, zone));
		dns_zone_setclass(zone, view->rdclass);
		dns_zone_settype(zone, dns_zone_primary);
		dns_zone_setstats(zone, named_g_server->zonestats);
		dns_zone_setdbtype(zone, dbtypec, dbtype);
		dns_zone_setdialup(zone, dns_dialuptype_no);
		dns_zone_setcheckdstype(zone, dns_checkdstype_no);
		dns_zone_setnotifytype(zone, dns_notifytype_no);
		dns_zone_setautomatic(zone, true);
		dns_zone_setoption(zone, DNS_ZONEOPT_NOCHECKNS, true);
	} else {
		dns_zone_attach(pzone, &zone);
	}
	if (view->queryacl != NULL) {
		dns_zone_setqueryacl(zone, view->queryacl);
	} else {
		dns_zone_clearqueryacl(zone);
	}
	if (view->queryonacl != NULL) {
		dns_zone_setqueryonacl(zone, view->queryonacl);
	} else {
		dns_zone_clearqueryonacl(zone);
	}
	dns_zone_setview(zone, view);
	CHECK(dns_view_addzone(view, zone));

	dns_name_format(name, namebuf, sizeof(namebuf));
	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		      "automatic ipv4only zone%s%s: %s", sep, viewname,
		      namebuf);

cleanup:
	if (zone != NULL) {
		dns_zone_detach(&zone);
	}
	return result;
}

#ifdef HAVE_DNSTAP
static isc_result_t
configure_dnstap(const cfg_obj_t **maps, dns_view_t *view) {
	isc_result_t result;
	const cfg_obj_t *obj, *obj2;
	const cfg_listelt_t *element;
	const char *dpath;
	const cfg_obj_t *dlist = NULL;
	dns_dtmsgtype_t dttypes = 0;
	unsigned int i;
	struct fstrm_iothr_options *fopt = NULL;

	result = named_config_get(maps, "dnstap", &dlist);
	if (result != ISC_R_SUCCESS) {
		return ISC_R_SUCCESS;
	}

	for (element = cfg_list_first(dlist); element != NULL;
	     element = cfg_list_next(element))
	{
		const char *str;
		dns_dtmsgtype_t dt = 0;

		obj = cfg_listelt_value(element);
		obj2 = cfg_tuple_get(obj, "type");
		str = cfg_obj_asstring(obj2);
		if (strcasecmp(str, "client") == 0) {
			dt |= DNS_DTTYPE_CQ | DNS_DTTYPE_CR;
		} else if (strcasecmp(str, "auth") == 0) {
			dt |= DNS_DTTYPE_AQ | DNS_DTTYPE_AR;
		} else if (strcasecmp(str, "resolver") == 0) {
			dt |= DNS_DTTYPE_RQ | DNS_DTTYPE_RR;
		} else if (strcasecmp(str, "forwarder") == 0) {
			dt |= DNS_DTTYPE_FQ | DNS_DTTYPE_FR;
		} else if (strcasecmp(str, "update") == 0) {
			dt |= DNS_DTTYPE_UQ | DNS_DTTYPE_UR;
		} else if (strcasecmp(str, "all") == 0) {
			dt |= DNS_DTTYPE_CQ | DNS_DTTYPE_CR | DNS_DTTYPE_AQ |
			      DNS_DTTYPE_AR | DNS_DTTYPE_RQ | DNS_DTTYPE_RR |
			      DNS_DTTYPE_FQ | DNS_DTTYPE_FR | DNS_DTTYPE_UQ |
			      DNS_DTTYPE_UR;
		}

		obj2 = cfg_tuple_get(obj, "mode");
		if (obj2 == NULL || cfg_obj_isvoid(obj2)) {
			dttypes |= dt;
			continue;
		}

		str = cfg_obj_asstring(obj2);
		if (strcasecmp(str, "query") == 0) {
			dt &= ~DNS_DTTYPE_RESPONSE;
		} else if (strcasecmp(str, "response") == 0) {
			dt &= ~DNS_DTTYPE_QUERY;
		}

		dttypes |= dt;
	}

	if (named_g_server->dtenv == NULL && dttypes != 0) {
		dns_dtmode_t dmode;
		uint64_t max_size = 0;
		uint32_t rolls = 0;
		isc_log_rollsuffix_t suffix = isc_log_rollsuffix_increment;

		obj = NULL;
		CHECKM(named_config_get(maps, "dnstap-output", &obj),
		       "'dnstap-output' must be set if 'dnstap' is set");

		obj2 = cfg_tuple_get(obj, "mode");
		if (obj2 == NULL) {
			CHECKM(ISC_R_FAILURE, "dnstap-output mode not found");
		}
		if (strcasecmp(cfg_obj_asstring(obj2), "file") == 0) {
			dmode = dns_dtmode_file;
		} else {
			dmode = dns_dtmode_unix;
		}

		obj2 = cfg_tuple_get(obj, "path");
		if (obj2 == NULL) {
			CHECKM(ISC_R_FAILURE, "dnstap-output path not found");
		}

		dpath = cfg_obj_asstring(obj2);

		obj2 = cfg_tuple_get(obj, "size");
		if (obj2 != NULL && cfg_obj_isuint64(obj2)) {
			max_size = cfg_obj_asuint64(obj2);
			if (max_size > SIZE_MAX) {
				cfg_obj_log(obj2, named_g_lctx, ISC_LOG_WARNING,
					    "'dnstap-output size "
					    "%" PRIu64 "' "
					    "is too large for this "
					    "system; reducing to %lu",
					    max_size, (unsigned long)SIZE_MAX);
				max_size = SIZE_MAX;
			}
		}

		obj2 = cfg_tuple_get(obj, "versions");
		if (obj2 != NULL && cfg_obj_isuint32(obj2)) {
			rolls = cfg_obj_asuint32(obj2);
		} else {
			rolls = ISC_LOG_ROLLINFINITE;
		}

		obj2 = cfg_tuple_get(obj, "suffix");
		if (obj2 != NULL && cfg_obj_isstring(obj2) &&
		    strcasecmp(cfg_obj_asstring(obj2), "timestamp") == 0)
		{
			suffix = isc_log_rollsuffix_timestamp;
		}

		fopt = fstrm_iothr_options_init();
		/*
		 * Both network threads and worker threads may log dnstap data.
		 */
		fstrm_iothr_options_set_num_input_queues(fopt,
							 2 * named_g_cpus);
		fstrm_iothr_options_set_queue_model(
			fopt, FSTRM_IOTHR_QUEUE_MODEL_MPSC);

		obj = NULL;
		result = named_config_get(maps, "fstrm-set-buffer-hint", &obj);
		if (result == ISC_R_SUCCESS) {
			i = cfg_obj_asuint32(obj);
			fstrm_iothr_options_set_buffer_hint(fopt, i);
		}

		obj = NULL;
		result = named_config_get(maps, "fstrm-set-flush-timeout",
					  &obj);
		if (result == ISC_R_SUCCESS) {
			i = cfg_obj_asuint32(obj);
			fstrm_iothr_options_set_flush_timeout(fopt, i);
		}

		obj = NULL;
		result = named_config_get(maps, "fstrm-set-input-queue-size",
					  &obj);
		if (result == ISC_R_SUCCESS) {
			i = cfg_obj_asuint32(obj);
			fstrm_iothr_options_set_input_queue_size(fopt, i);
		}

		obj = NULL;
		result = named_config_get(
			maps, "fstrm-set-output-notify-threshold", &obj);
		if (result == ISC_R_SUCCESS) {
			i = cfg_obj_asuint32(obj);
			fstrm_iothr_options_set_queue_notify_threshold(fopt, i);
		}

		obj = NULL;
		result = named_config_get(maps, "fstrm-set-output-queue-model",
					  &obj);
		if (result == ISC_R_SUCCESS) {
			if (strcasecmp(cfg_obj_asstring(obj), "spsc") == 0) {
				i = FSTRM_IOTHR_QUEUE_MODEL_SPSC;
			} else {
				i = FSTRM_IOTHR_QUEUE_MODEL_MPSC;
			}
			fstrm_iothr_options_set_queue_model(fopt, i);
		}

		obj = NULL;
		result = named_config_get(maps, "fstrm-set-output-queue-size",
					  &obj);
		if (result == ISC_R_SUCCESS) {
			i = cfg_obj_asuint32(obj);
			fstrm_iothr_options_set_output_queue_size(fopt, i);
		}

		obj = NULL;
		result = named_config_get(maps, "fstrm-set-reopen-interval",
					  &obj);
		if (result == ISC_R_SUCCESS) {
			i = cfg_obj_asduration(obj);
			fstrm_iothr_options_set_reopen_interval(fopt, i);
		}

		CHECKM(dns_dt_create(named_g_mctx, dmode, dpath, &fopt,
				     named_g_mainloop, &named_g_server->dtenv),
		       "unable to create dnstap environment");

		CHECKM(dns_dt_setupfile(named_g_server->dtenv, max_size, rolls,
					suffix),
		       "unable to set up dnstap logfile");
	}

	if (named_g_server->dtenv == NULL) {
		return ISC_R_SUCCESS;
	}

	obj = NULL;
	result = named_config_get(maps, "dnstap-version", &obj);
	if (result != ISC_R_SUCCESS) {
		/* not specified; use the product and version */
		dns_dt_setversion(named_g_server->dtenv, PACKAGE_STRING);
	} else if (result == ISC_R_SUCCESS && !cfg_obj_isvoid(obj)) {
		/* Quoted string */
		dns_dt_setversion(named_g_server->dtenv, cfg_obj_asstring(obj));
	}

	obj = NULL;
	result = named_config_get(maps, "dnstap-identity", &obj);
	if (result == ISC_R_SUCCESS && cfg_obj_isboolean(obj)) {
		/* "hostname" is interpreted as boolean true */
		char buf[256];
		if (gethostname(buf, sizeof(buf)) == 0) {
			dns_dt_setidentity(named_g_server->dtenv, buf);
		}
	} else if (result == ISC_R_SUCCESS && !cfg_obj_isvoid(obj)) {
		/* Quoted string */
		dns_dt_setidentity(named_g_server->dtenv,
				   cfg_obj_asstring(obj));
	}

	dns_dt_attach(named_g_server->dtenv, &view->dtenv);
	view->dttypes = dttypes;

	result = ISC_R_SUCCESS;

cleanup:
	if (fopt != NULL) {
		fstrm_iothr_options_destroy(&fopt);
	}

	return result;
}
#endif /* HAVE_DNSTAP */

static isc_result_t
create_mapped_acl(void) {
	isc_result_t result;
	dns_acl_t *acl = NULL;
	struct in6_addr in6 = IN6ADDR_V4MAPPED_INIT;
	isc_netaddr_t addr;

	isc_netaddr_fromin6(&addr, &in6);

	dns_acl_create(named_g_mctx, 1, &acl);

	result = dns_iptable_addprefix(acl->iptable, &addr, 96, true);
	if (result == ISC_R_SUCCESS) {
		dns_acl_attach(acl, &named_g_mapped);
	}
	dns_acl_detach(&acl);
	return result;
}

/*%
 * A callback for the cfg_pluginlist_foreach() call in configure_view() below.
 * If registering any plugin fails, registering subsequent ones is not
 * attempted.
 */
static isc_result_t
register_one_plugin(const cfg_obj_t *config, const cfg_obj_t *obj,
		    const char *plugin_path, const char *parameters,
		    void *callback_data) {
	dns_view_t *view = callback_data;
	char full_path[PATH_MAX];
	isc_result_t result;

	result = ns_plugin_expandpath(plugin_path, full_path,
				      sizeof(full_path));
	if (result != ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "%s: plugin configuration failed: "
			      "unable to get full plugin path: %s",
			      plugin_path, isc_result_totext(result));
		return result;
	}

	result = ns_plugin_register(full_path, parameters, config,
				    cfg_obj_file(obj), cfg_obj_line(obj),
				    named_g_mctx, named_g_lctx,
				    named_g_aclconfctx, view);
	if (result != ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "%s: plugin configuration failed: %s", full_path,
			      isc_result_totext(result));
	}

	return result;
}

/*
 * Determine if a minimal-sized cache can be used for a given view, according
 * to 'maps' (implicit defaults, global options, view options) and 'optionmaps'
 * (global options, view options).  This is only allowed for views which have
 * recursion disabled and do not have "max-cache-size" set explicitly.  Using
 * minimal-sized caches prevents a situation in which all explicitly configured
 * and built-in views inherit the default "max-cache-size 90%;" setting, which
 * could lead to memory exhaustion with multiple views configured.
 */
static bool
minimal_cache_allowed(const cfg_obj_t *maps[4],
		      const cfg_obj_t *optionmaps[3]) {
	const cfg_obj_t *obj;

	/*
	 * Do not use a minimal-sized cache for a view with recursion enabled.
	 */
	obj = NULL;
	(void)named_config_get(maps, "recursion", &obj);
	INSIST(obj != NULL);
	if (cfg_obj_asboolean(obj)) {
		return false;
	}

	/*
	 * Do not use a minimal-sized cache if a specific size was requested.
	 */
	obj = NULL;
	(void)named_config_get(optionmaps, "max-cache-size", &obj);
	if (obj != NULL) {
		return false;
	}

	return true;
}

static const char *const response_synonyms[] = { "response", NULL };

/*
 * Configure 'view' according to 'vconfig', taking defaults from
 * 'config' where values are missing in 'vconfig'.
 *
 * When configuring the default view, 'vconfig' will be NULL and the
 * global defaults in 'config' used exclusively.
 */
static isc_result_t
configure_view(dns_view_t *view, dns_viewlist_t *viewlist, cfg_obj_t *config,
	       cfg_obj_t *vconfig, named_cachelist_t *cachelist,
	       named_cachelist_t *oldcachelist, dns_kasplist_t *kasplist,
	       dns_keystorelist_t *keystores, const cfg_obj_t *bindkeys,
	       isc_mem_t *mctx, cfg_aclconfctx_t *actx, bool need_hints) {
	const cfg_obj_t *maps[4];
	const cfg_obj_t *cfgmaps[3];
	const cfg_obj_t *optionmaps[3];
	const cfg_obj_t *options = NULL;
	const cfg_obj_t *voptions = NULL;
	const cfg_obj_t *forwardtype;
	const cfg_obj_t *forwarders;
	const cfg_obj_t *alternates;
	const cfg_obj_t *zonelist;
	const cfg_obj_t *dlzlist;
	const cfg_obj_t *dlz;
	const cfg_obj_t *prefetch_trigger;
	const cfg_obj_t *prefetch_eligible;
	unsigned int dlzargc;
	char **dlzargv;
	const cfg_obj_t *dyndb_list, *plugin_list;
	const cfg_obj_t *disabled;
	const cfg_obj_t *obj, *obj2;
	const cfg_listelt_t *element = NULL;
	const cfg_listelt_t *zone_element_latest = NULL;
	in_port_t port;
	dns_cache_t *cache = NULL;
	isc_result_t result;
	size_t max_cache_size;
	uint32_t max_cache_size_percent = 0;
	size_t max_adb_size;
	uint32_t lame_ttl, fail_ttl;
	uint32_t max_stale_ttl = 0;
	uint32_t stale_refresh_time = 0;
	dns_tsigkeyring_t *ring = NULL;
	dns_transport_list_t *transports = NULL;
	dns_view_t *pview = NULL; /* Production view */
	dns_dispatch_t *dispatch4 = NULL;
	dns_dispatch_t *dispatch6 = NULL;
	bool rpz_configured = false;
	bool catz_configured = false;
	bool shared_cache = false;
	int i = 0, j = 0, k = 0;
	const char *str;
	const char *cachename = NULL;
	dns_order_t *order = NULL;
	uint32_t udpsize;
	uint32_t maxbits;
	unsigned int resopts = 0;
	dns_zone_t *zone = NULL;
	uint32_t clients_per_query, max_clients_per_query;
	bool empty_zones_enable;
	const cfg_obj_t *disablelist = NULL;
	isc_stats_t *resstats = NULL;
	dns_stats_t *resquerystats = NULL;
	bool auto_root = false;
	named_cache_t *nsc = NULL;
	bool zero_no_soattl;
	dns_acl_t *clients = NULL, *mapped = NULL, *excluded = NULL;
	unsigned int query_timeout;
	bool old_rpz_ok = false;
	dns_dyndbctx_t *dctx = NULL;
	dns_ntatable_t *ntatable = NULL;
	const char *qminmode = NULL;
	dns_adb_t *adb = NULL;
	bool oldcache = false;

	REQUIRE(DNS_VIEW_VALID(view));

	if (config != NULL) {
		(void)cfg_map_get(config, "options", &options);
	}

	/*
	 * maps: view options, options, defaults
	 * cfgmaps: view options, config
	 * optionmaps: view options, options
	 */
	if (vconfig != NULL) {
		voptions = cfg_tuple_get(vconfig, "options");
		maps[i++] = voptions;
		optionmaps[j++] = voptions;
		cfgmaps[k++] = voptions;
	}
	if (options != NULL) {
		maps[i++] = options;
		optionmaps[j++] = options;
	}

	maps[i++] = named_g_defaults;
	maps[i] = NULL;
	optionmaps[j] = NULL;
	if (config != NULL) {
		cfgmaps[k++] = config;
	}
	cfgmaps[k] = NULL;

	/*
	 * Set the view's port number for outgoing queries.
	 */
	CHECKM(named_config_getport(config, "port", &port), "port");
	dns_view_setdstport(view, port);

	/*
	 * Make the list of response policy zone names for a view that
	 * is used for real lookups and so cares about hints.
	 */
	obj = NULL;
	if (view->rdclass == dns_rdataclass_in && need_hints &&
	    named_config_get(maps, "response-policy", &obj) == ISC_R_SUCCESS)
	{
		CHECK(configure_rpz(view, NULL, maps, obj, &old_rpz_ok));
		rpz_configured = true;
	}

	obj = NULL;
	if (view->rdclass != dns_rdataclass_in && need_hints &&
	    named_config_get(maps, "catalog-zones", &obj) == ISC_R_SUCCESS)
	{
		cfg_obj_log(obj, named_g_lctx, ISC_LOG_WARNING,
			    "'catalog-zones' option is only supported "
			    "for views with class IN");
	}

	obj = NULL;
	if (view->rdclass == dns_rdataclass_in && need_hints &&
	    named_config_get(maps, "catalog-zones", &obj) == ISC_R_SUCCESS)
	{
		CHECK(configure_catz(view, NULL, config, obj));
		catz_configured = true;
	}

	/*
	 * Configure the zones.
	 */
	zonelist = NULL;
	if (voptions != NULL) {
		(void)cfg_map_get(voptions, "zone", &zonelist);
	} else {
		(void)cfg_map_get(config, "zone", &zonelist);
	}

	/*
	 * Load zone configuration
	 */
	for (element = cfg_list_first(zonelist); element != NULL;
	     element = cfg_list_next(element))
	{
		const cfg_obj_t *zconfig = cfg_listelt_value(element);
		CHECK(configure_zone(config, zconfig, vconfig, view, viewlist,
				     kasplist, keystores, actx, false,
				     old_rpz_ok, false, false));
		zone_element_latest = element;
	}

	/*
	 * Check that a primary or secondary zone was found for each
	 * zone named in the response policy statement, unless we are
	 * using RPZ service interface.
	 */
	if (view->rpzs != NULL && !view->rpzs->p.dnsrps_enabled) {
		dns_rpz_num_t n;

		for (n = 0; n < view->rpzs->p.num_zones; ++n) {
			if ((view->rpzs->defined & DNS_RPZ_ZBIT(n)) == 0) {
				char namebuf[DNS_NAME_FORMATSIZE];

				dns_name_format(&view->rpzs->zones[n]->origin,
						namebuf, sizeof(namebuf));
				isc_log_write(named_g_lctx,
					      NAMED_LOGCATEGORY_GENERAL,
					      NAMED_LOGMODULE_SERVER,
					      DNS_RPZ_ERROR_LEVEL,
					      "rpz '%s' is not a primary or a "
					      "secondary zone",
					      namebuf);
				result = ISC_R_NOTFOUND;
				goto cleanup;
			}
		}
	}

	/*
	 * If we're allowing added zones, then load zone configuration
	 * from the newzone file for zones that were added during previous
	 * runs.
	 */
	CHECK(configure_newzones(view, config, vconfig, actx));

	/*
	 * Create Dynamically Loadable Zone driver.
	 */
	dlzlist = NULL;
	if (voptions != NULL) {
		(void)cfg_map_get(voptions, "dlz", &dlzlist);
	} else {
		(void)cfg_map_get(config, "dlz", &dlzlist);
	}

	for (element = cfg_list_first(dlzlist); element != NULL;
	     element = cfg_list_next(element))
	{
		dlz = cfg_listelt_value(element);

		obj = NULL;
		(void)cfg_map_get(dlz, "database", &obj);
		if (obj != NULL) {
			dns_dlzdb_t *dlzdb = NULL;
			const cfg_obj_t *name, *search = NULL;
			char *s = isc_mem_strdup(mctx, cfg_obj_asstring(obj));

			if (s == NULL) {
				result = ISC_R_NOMEMORY;
				goto cleanup;
			}

			result = isc_commandline_strtoargv(mctx, s, &dlzargc,
							   &dlzargv, 0);
			if (result != ISC_R_SUCCESS) {
				isc_mem_free(mctx, s);
				goto cleanup;
			}

			name = cfg_map_getname(dlz);
			result = dns_dlzcreate(mctx, cfg_obj_asstring(name),
					       dlzargv[0], dlzargc, dlzargv,
					       &dlzdb);
			isc_mem_free(mctx, s);
			isc_mem_cput(mctx, dlzargv, dlzargc, sizeof(*dlzargv));
			if (result != ISC_R_SUCCESS) {
				goto cleanup;
			}

			/*
			 * If the DLZ backend supports configuration,
			 * and is searchable, then call its configure
			 * method now.  If not searchable, we'll take
			 * care of it when we process the zone statement.
			 */
			(void)cfg_map_get(dlz, "search", &search);
			if (search == NULL || cfg_obj_asboolean(search)) {
				dlzdb->search = true;
				result = dns_dlzconfigure(
					view, dlzdb, dlzconfigure_callback);
				if (result != ISC_R_SUCCESS) {
					goto cleanup;
				}
				ISC_LIST_APPEND(view->dlz_searched, dlzdb,
						link);
			} else {
				dlzdb->search = false;
				ISC_LIST_APPEND(view->dlz_unsearched, dlzdb,
						link);
			}
		}
	}

	/*
	 * Obtain configuration parameters that affect the decision of whether
	 * we can reuse/share an existing cache.
	 */
	obj = NULL;
	result = named_config_get(maps, "max-cache-size", &obj);
	INSIST(result == ISC_R_SUCCESS);
	/*
	 * If "-T maxcachesize=..." is in effect, it overrides any other
	 * "max-cache-size" setting found in configuration, either implicit or
	 * explicit.  For simplicity, the value passed to that command line
	 * option is always treated as the number of bytes to set
	 * "max-cache-size" to.
	 */
	if (named_g_maxcachesize != 0) {
		max_cache_size = named_g_maxcachesize;
	} else if (minimal_cache_allowed(maps, optionmaps)) {
		/*
		 * dns_cache_setcachesize() will adjust this to the smallest
		 * allowed value.
		 */
		max_cache_size = 1;
	} else if (cfg_obj_isstring(obj)) {
		str = cfg_obj_asstring(obj);
		INSIST(strcasecmp(str, "unlimited") == 0);
		max_cache_size = 0;
	} else if (cfg_obj_ispercentage(obj)) {
		max_cache_size = SIZE_AS_PERCENT;
		max_cache_size_percent = cfg_obj_aspercentage(obj);
	} else {
		uint64_t value = cfg_obj_asuint64(obj);
		if (value > SIZE_MAX) {
			cfg_obj_log(obj, named_g_lctx, ISC_LOG_WARNING,
				    "'max-cache-size "
				    "%" PRIu64 "' "
				    "is too large for this "
				    "system; reducing to %lu",
				    value, (unsigned long)SIZE_MAX);
			value = SIZE_MAX;
		}
		max_cache_size = (size_t)value;
	}

	if (max_cache_size == SIZE_AS_PERCENT) {
		uint64_t totalphys = isc_meminfo_totalphys();

		max_cache_size =
			(size_t)(totalphys * max_cache_size_percent / 100);
		if (totalphys == 0) {
			cfg_obj_log(obj, named_g_lctx, ISC_LOG_WARNING,
				    "Unable to determine amount of physical "
				    "memory, setting 'max-cache-size' to "
				    "unlimited");
		} else {
			cfg_obj_log(obj, named_g_lctx, ISC_LOG_INFO,
				    "'max-cache-size %d%%' "
				    "- setting to %" PRIu64 "MB "
				    "(out of %" PRIu64 "MB)",
				    max_cache_size_percent,
				    (uint64_t)(max_cache_size / (1024 * 1024)),
				    totalphys / (1024 * 1024));
		}
	}

	/* Check-names. */
	obj = NULL;
	result = named_checknames_get(maps, response_synonyms, &obj);
	INSIST(result == ISC_R_SUCCESS);

	str = cfg_obj_asstring(obj);
	if (strcasecmp(str, "fail") == 0) {
		resopts |= DNS_RESOLVER_CHECKNAMES |
			   DNS_RESOLVER_CHECKNAMESFAIL;
		view->checknames = true;
	} else if (strcasecmp(str, "warn") == 0) {
		resopts |= DNS_RESOLVER_CHECKNAMES;
		view->checknames = false;
	} else if (strcasecmp(str, "ignore") == 0) {
		view->checknames = false;
	} else {
		UNREACHABLE();
	}

	obj = NULL;
	result = named_config_get(maps, "zero-no-soa-ttl-cache", &obj);
	INSIST(result == ISC_R_SUCCESS);
	zero_no_soattl = cfg_obj_asboolean(obj);

	obj = NULL;
	result = named_config_get(maps, "resolver-use-dns64", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->usedns64 = cfg_obj_asboolean(obj);

	obj = NULL;
	result = named_config_get(maps, "dns64", &obj);
	if (result == ISC_R_SUCCESS && strcmp(view->name, "_bind") &&
	    strcmp(view->name, "_meta"))
	{
		isc_netaddr_t na, suffix, *sp;
		unsigned int prefixlen;
		const char *server, *contact;
		const cfg_obj_t *myobj;

		myobj = NULL;
		result = named_config_get(maps, "dns64-server", &myobj);
		if (result == ISC_R_SUCCESS) {
			server = cfg_obj_asstring(myobj);
		} else {
			server = NULL;
		}

		myobj = NULL;
		result = named_config_get(maps, "dns64-contact", &myobj);
		if (result == ISC_R_SUCCESS) {
			contact = cfg_obj_asstring(myobj);
		} else {
			contact = NULL;
		}

		for (element = cfg_list_first(obj); element != NULL;
		     element = cfg_list_next(element))
		{
			const cfg_obj_t *map = cfg_listelt_value(element);
			dns_dns64_t *dns64 = NULL;
			unsigned int dns64options = 0;

			cfg_obj_asnetprefix(cfg_map_getname(map), &na,
					    &prefixlen);

			obj = NULL;
			(void)cfg_map_get(map, "suffix", &obj);
			if (obj != NULL) {
				sp = &suffix;
				isc_netaddr_fromsockaddr(
					sp, cfg_obj_assockaddr(obj));
			} else {
				sp = NULL;
			}

			clients = mapped = excluded = NULL;
			obj = NULL;
			(void)cfg_map_get(map, "clients", &obj);
			if (obj != NULL) {
				result = cfg_acl_fromconfig(obj, config,
							    named_g_lctx, actx,
							    mctx, 0, &clients);
				if (result != ISC_R_SUCCESS) {
					goto cleanup;
				}
			}
			obj = NULL;
			(void)cfg_map_get(map, "mapped", &obj);
			if (obj != NULL) {
				result = cfg_acl_fromconfig(obj, config,
							    named_g_lctx, actx,
							    mctx, 0, &mapped);
				if (result != ISC_R_SUCCESS) {
					goto cleanup;
				}
			}
			obj = NULL;
			(void)cfg_map_get(map, "exclude", &obj);
			if (obj != NULL) {
				result = cfg_acl_fromconfig(obj, config,
							    named_g_lctx, actx,
							    mctx, 0, &excluded);
				if (result != ISC_R_SUCCESS) {
					goto cleanup;
				}
			} else {
				if (named_g_mapped == NULL) {
					result = create_mapped_acl();
					if (result != ISC_R_SUCCESS) {
						goto cleanup;
					}
				}
				dns_acl_attach(named_g_mapped, &excluded);
			}

			obj = NULL;
			(void)cfg_map_get(map, "recursive-only", &obj);
			if (obj != NULL && cfg_obj_asboolean(obj)) {
				dns64options |= DNS_DNS64_RECURSIVE_ONLY;
			}

			obj = NULL;
			(void)cfg_map_get(map, "break-dnssec", &obj);
			if (obj != NULL && cfg_obj_asboolean(obj)) {
				dns64options |= DNS_DNS64_BREAK_DNSSEC;
			}

			result = dns_dns64_create(mctx, &na, prefixlen, sp,
						  clients, mapped, excluded,
						  dns64options, &dns64);
			if (result != ISC_R_SUCCESS) {
				goto cleanup;
			}
			dns_dns64_append(&view->dns64, dns64);
			view->dns64cnt++;
			result = dns64_reverse(view, mctx, &na, prefixlen,
					       server, contact);
			if (result != ISC_R_SUCCESS) {
				goto cleanup;
			}
			if (clients != NULL) {
				dns_acl_detach(&clients);
			}
			if (mapped != NULL) {
				dns_acl_detach(&mapped);
			}
			if (excluded != NULL) {
				dns_acl_detach(&excluded);
			}
		}
	}

	obj = NULL;
	result = named_config_get(maps, "dnssec-accept-expired", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->acceptexpired = cfg_obj_asboolean(obj);

	obj = NULL;
	/* 'optionmaps', not 'maps': don't check named_g_defaults yet */
	(void)named_config_get(optionmaps, "dnssec-validation", &obj);
	if (obj == NULL) {
		/*
		 * Default to VALIDATION_DEFAULT as set in config.c.
		 */
		(void)cfg_map_get(named_g_defaults, "dnssec-validation", &obj);
		INSIST(obj != NULL);
	}
	if (obj != NULL) {
		if (cfg_obj_isboolean(obj)) {
			view->enablevalidation = cfg_obj_asboolean(obj);
		} else {
			/*
			 * If dnssec-validation is set but not boolean,
			 * then it must be "auto"
			 */
			view->enablevalidation = true;
			auto_root = true;
		}
	}

	obj = NULL;
	result = named_config_get(maps, "max-cache-ttl", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->maxcachettl = cfg_obj_asduration(obj);

	obj = NULL;
	result = named_config_get(maps, "max-ncache-ttl", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->maxncachettl = cfg_obj_asduration(obj);

	obj = NULL;
	result = named_config_get(maps, "min-cache-ttl", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->mincachettl = cfg_obj_asduration(obj);

	obj = NULL;
	result = named_config_get(maps, "min-ncache-ttl", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->minncachettl = cfg_obj_asduration(obj);

	obj = NULL;
	result = named_config_get(maps, "synth-from-dnssec", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->synthfromdnssec = cfg_obj_asboolean(obj);

	obj = NULL;
	result = named_config_get(maps, "stale-cache-enable", &obj);
	INSIST(result == ISC_R_SUCCESS);
	if (cfg_obj_asboolean(obj)) {
		obj = NULL;
		result = named_config_get(maps, "max-stale-ttl", &obj);
		INSIST(result == ISC_R_SUCCESS);
		max_stale_ttl = ISC_MAX(cfg_obj_asduration(obj), 1);
	}
	/*
	 * If 'stale-cache-enable' is false, max_stale_ttl is set to 0,
	 * meaning keeping stale RRsets in cache is disabled.
	 */

	obj = NULL;
	result = named_config_get(maps, "stale-answer-enable", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->staleanswersenable = cfg_obj_asboolean(obj);

	result = dns_viewlist_find(&named_g_server->viewlist, view->name,
				   view->rdclass, &pview);
	if (result == ISC_R_SUCCESS) {
		view->staleanswersok = pview->staleanswersok;
		dns_view_detach(&pview);
	} else {
		view->staleanswersok = dns_stale_answer_conf;
	}

	obj = NULL;
	result = named_config_get(maps, "stale-answer-client-timeout", &obj);
	INSIST(result == ISC_R_SUCCESS);
	if (cfg_obj_isstring(obj)) {
		/*
		 * The only string values available for this option
		 * are "disabled" and "off".
		 * We use (uint32_t) -1 to represent disabled since
		 * a value of zero means that stale data can be used
		 * to promptly answer the query, while an attempt to
		 * refresh the RRset will still be made in background.
		 */
		view->staleanswerclienttimeout = (uint32_t)-1;
	} else {
		view->staleanswerclienttimeout = cfg_obj_asuint32(obj);

		/*
		 * BIND 9 no longer supports non-zero values of
		 * stale-answer-client-timeout.
		 */
		if (view->staleanswerclienttimeout != 0) {
			view->staleanswerclienttimeout = 0;
			isc_log_write(
				named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
				"BIND 9 no longer supports non-zero values of "
				"stale-answer-client-timeout, adjusted to 0");
		}
	}

	obj = NULL;
	result = named_config_get(maps, "stale-refresh-time", &obj);
	INSIST(result == ISC_R_SUCCESS);
	stale_refresh_time = cfg_obj_asduration(obj);

	/*
	 * Configure the view's cache.
	 *
	 * First, check to see if there are any attach-cache options.  If yes,
	 * attempt to lookup an existing cache at attach it to the view.  If
	 * there is not one, then try to reuse an existing cache if possible;
	 * otherwise create a new cache.
	 *
	 * Note that the ADB is not preserved or shared in either case.
	 *
	 * When a matching view is found, the associated statistics are also
	 * retrieved and reused.
	 *
	 * XXX Determining when it is safe to reuse or share a cache is tricky.
	 * When the view's configuration changes, the cached data may become
	 * invalid because it reflects our old view of the world.  We check
	 * some of the configuration parameters that could invalidate the cache
	 * or otherwise make it unshareable, but there are other configuration
	 * options that should be checked.  For example, if a view uses a
	 * forwarder, changes in the forwarder configuration may invalidate
	 * the cache.  At the moment, it's the administrator's responsibility to
	 * ensure these configuration options don't invalidate reusing/sharing.
	 */
	obj = NULL;
	result = named_config_get(maps, "attach-cache", &obj);
	if (result == ISC_R_SUCCESS) {
		cachename = cfg_obj_asstring(obj);
	} else {
		cachename = view->name;
	}

	nsc = cachelist_find(cachelist, cachename, view->rdclass);
	if (result == ISC_R_SUCCESS && nsc == NULL) {
		/*
		 * If we're using 'attach-cache' but didn't find the
		 * specified cache in the cache list already, check
		 * the old list.
		 */
		nsc = cachelist_find(oldcachelist, cachename, view->rdclass);
		oldcache = true;
	}
	if (nsc != NULL) {
		if (!cache_sharable(nsc->primaryview, view, zero_no_soattl,
				    max_cache_size, max_stale_ttl,
				    stale_refresh_time))
		{
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
				      "view %s can't use existing cache %s due "
				      "to configuration parameter mismatch",
				      view->name,
				      dns_cache_getname(nsc->cache));
			nsc = NULL;
		} else {
			if (oldcache) {
				ISC_LIST_UNLINK(*oldcachelist, nsc, link);
				ISC_LIST_APPEND(*cachelist, nsc, link);
				nsc->primaryview = view;
			}
			dns_cache_attach(nsc->cache, &cache);
			shared_cache = true;
		}
	} else if (strcmp(cachename, view->name) == 0) {
		result = dns_viewlist_find(&named_g_server->viewlist, cachename,
					   view->rdclass, &pview);
		if (result != ISC_R_NOTFOUND && result != ISC_R_SUCCESS) {
			goto cleanup;
		}
		if (pview != NULL) {
			if (!cache_reusable(pview, view, zero_no_soattl)) {
				isc_log_write(named_g_lctx,
					      NAMED_LOGCATEGORY_GENERAL,
					      NAMED_LOGMODULE_SERVER,
					      ISC_LOG_DEBUG(1),
					      "cache cannot be reused "
					      "for view %s due to "
					      "configuration parameter "
					      "mismatch",
					      view->name);
			} else {
				INSIST(pview->cache != NULL);
				isc_log_write(named_g_lctx,
					      NAMED_LOGCATEGORY_GENERAL,
					      NAMED_LOGMODULE_SERVER,
					      ISC_LOG_DEBUG(3),
					      "reusing existing cache");
				dns_cache_attach(pview->cache, &cache);
			}
			dns_resolver_getstats(pview->resolver, &resstats);
			dns_resolver_getquerystats(pview->resolver,
						   &resquerystats);
			dns_view_detach(&pview);
		}
	}

	if (nsc == NULL) {
		/*
		 * Create a cache with the desired name.  This normally
		 * equals the view name, but may also be a forward
		 * reference to a view that share the cache with this
		 * view but is not yet configured.  If it is not the
		 * view name but not a forward reference either, then it
		 * is simply a named cache that is not shared.
		 */
		if (cache == NULL) {
			CHECK(dns_cache_create(named_g_loopmgr, view->rdclass,
					       cachename, mctx, &cache));
		}

		nsc = isc_mem_get(mctx, sizeof(*nsc));
		*nsc = (named_cache_t){
			.primaryview = view,
			.rdclass = view->rdclass,
			.link = ISC_LINK_INITIALIZER,
		};

		dns_cache_attach(cache, &nsc->cache);
		ISC_LIST_APPEND(*cachelist, nsc, link);
	}

	dns_view_setcache(view, cache, shared_cache);

	dns_cache_setcachesize(cache, max_cache_size);
	dns_cache_setservestalettl(cache, max_stale_ttl);
	dns_cache_setservestalerefresh(cache, stale_refresh_time);

	dns_cache_detach(&cache);

	obj = NULL;
	result = named_config_get(maps, "stale-answer-ttl", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->staleanswerttl = ISC_MAX(cfg_obj_asduration(obj), 1);

	/*
	 * Resolver.
	 */
	CHECK(get_view_querysource_dispatch(maps, AF_INET, &dispatch4,
					    ISC_LIST_PREV(view, link) == NULL));
	CHECK(get_view_querysource_dispatch(maps, AF_INET6, &dispatch6,
					    ISC_LIST_PREV(view, link) == NULL));
	if (dispatch4 == NULL && dispatch6 == NULL) {
		UNEXPECTED_ERROR("unable to obtain either an IPv4 or"
				 " an IPv6 dispatch");
		result = ISC_R_UNEXPECTED;
		goto cleanup;
	}

	CHECK(dns_view_createresolver(view, named_g_netmgr, resopts,
				      named_g_server->tlsctx_client_cache,
				      dispatch4, dispatch6));

	if (resstats == NULL) {
		isc_stats_create(mctx, &resstats, dns_resstatscounter_max);
	}
	dns_resolver_setstats(view->resolver, resstats);
	if (resquerystats == NULL) {
		dns_rdatatypestats_create(mctx, &resquerystats);
	}
	dns_resolver_setquerystats(view->resolver, resquerystats);

	/*
	 * Set the ADB cache size to 1/8th of the max-cache-size or
	 * MAX_ADB_SIZE_FOR_CACHESHARE when the cache is shared.
	 */
	max_adb_size = 0;
	if (max_cache_size != 0U) {
		max_adb_size = max_cache_size / 8;
		if (max_adb_size == 0U) {
			max_adb_size = 1; /* Force minimum. */
		}
		if (view != nsc->primaryview &&
		    max_adb_size > MAX_ADB_SIZE_FOR_CACHESHARE)
		{
			max_adb_size = MAX_ADB_SIZE_FOR_CACHESHARE;
			if (!nsc->adbsizeadjusted) {
				dns_view_getadb(nsc->primaryview, &adb);
				if (adb != NULL) {
					dns_adb_setadbsize(
						adb,
						MAX_ADB_SIZE_FOR_CACHESHARE);
					nsc->adbsizeadjusted = true;
					dns_adb_detach(&adb);
				}
			}
		}
	}
	dns_view_getadb(view, &adb);
	if (adb != NULL) {
		dns_adb_setadbsize(adb, max_adb_size);
		dns_adb_detach(&adb);
	}

	/*
	 * Set up ADB quotas
	 */
	{
		uint32_t fps, freq;
		double low, high, discount;

		obj = NULL;
		result = named_config_get(maps, "fetches-per-server", &obj);
		INSIST(result == ISC_R_SUCCESS);
		obj2 = cfg_tuple_get(obj, "fetches");
		fps = cfg_obj_asuint32(obj2);
		obj2 = cfg_tuple_get(obj, "response");
		if (!cfg_obj_isvoid(obj2)) {
			const char *resp = cfg_obj_asstring(obj2);
			isc_result_t r = DNS_R_SERVFAIL;

			if (strcasecmp(resp, "drop") == 0) {
				r = DNS_R_DROP;
			} else if (strcasecmp(resp, "fail") == 0) {
				r = DNS_R_SERVFAIL;
			} else {
				UNREACHABLE();
			}

			dns_resolver_setquotaresponse(view->resolver,
						      dns_quotatype_server, r);
		}

		obj = NULL;
		result = named_config_get(maps, "fetch-quota-params", &obj);
		INSIST(result == ISC_R_SUCCESS);

		obj2 = cfg_tuple_get(obj, "frequency");
		freq = cfg_obj_asuint32(obj2);

		obj2 = cfg_tuple_get(obj, "low");
		low = (double)cfg_obj_asfixedpoint(obj2) / 100.0;

		obj2 = cfg_tuple_get(obj, "high");
		high = (double)cfg_obj_asfixedpoint(obj2) / 100.0;

		obj2 = cfg_tuple_get(obj, "discount");
		discount = (double)cfg_obj_asfixedpoint(obj2) / 100.0;

		dns_view_getadb(view, &adb);
		if (adb != NULL) {
			dns_adb_setquota(adb, fps, freq, low, high, discount);
			dns_adb_detach(&adb);
		}
	}

	/*
	 * Set resolver's lame-ttl.
	 */
	obj = NULL;
	result = named_config_get(maps, "lame-ttl", &obj);
	INSIST(result == ISC_R_SUCCESS);
	lame_ttl = cfg_obj_asduration(obj);
	if (lame_ttl > 0) {
		cfg_obj_log(obj, named_g_lctx, ISC_LOG_WARNING,
			    "disabling lame cache despite lame-ttl > 0 as it "
			    "may cause performance issues");
	}

	/*
	 * Set the resolver's query timeout.
	 */
	obj = NULL;
	result = named_config_get(maps, "resolver-query-timeout", &obj);
	INSIST(result == ISC_R_SUCCESS);
	query_timeout = cfg_obj_asuint32(obj);
	dns_resolver_settimeout(view->resolver, query_timeout);

	/* Specify whether to use 0-TTL for negative response for SOA query */
	dns_resolver_setzeronosoattl(view->resolver, zero_no_soattl);

	/*
	 * Set the resolver's EDNS UDP size.
	 */
	obj = NULL;
	result = named_config_get(maps, "edns-udp-size", &obj);
	INSIST(result == ISC_R_SUCCESS);
	udpsize = cfg_obj_asuint32(obj);
	if (udpsize < 512) {
		udpsize = 512;
	}
	if (udpsize > 4096) {
		udpsize = 4096;
	}
	dns_view_setudpsize(view, (uint16_t)udpsize);

	/*
	 * Set the maximum UDP response size.
	 */
	obj = NULL;
	result = named_config_get(maps, "max-udp-size", &obj);
	INSIST(result == ISC_R_SUCCESS);
	udpsize = cfg_obj_asuint32(obj);
	if (udpsize < 512) {
		udpsize = 512;
	}
	if (udpsize > 4096) {
		udpsize = 4096;
	}
	view->maxudp = udpsize;

	/*
	 * Set the maximum UDP when a COOKIE is not provided.
	 */
	obj = NULL;
	result = named_config_get(maps, "nocookie-udp-size", &obj);
	INSIST(result == ISC_R_SUCCESS);
	udpsize = cfg_obj_asuint32(obj);
	if (udpsize < 128) {
		udpsize = 128;
	}
	if (udpsize > view->maxudp) {
		udpsize = view->maxudp;
	}
	view->nocookieudp = udpsize;

	/*
	 * Set the maximum rsa exponent bits.
	 */
	obj = NULL;
	result = named_config_get(maps, "max-rsa-exponent-size", &obj);
	INSIST(result == ISC_R_SUCCESS);
	maxbits = cfg_obj_asuint32(obj);
	if (maxbits != 0 && maxbits < 35) {
		maxbits = 35;
	}
	if (maxbits > 4096) {
		maxbits = 4096;
	}
	view->maxbits = maxbits;

	/*
	 * Set supported DNSSEC algorithms.
	 */
	disabled = NULL;
	(void)named_config_get(maps, "disable-algorithms", &disabled);
	if (disabled != NULL) {
		for (element = cfg_list_first(disabled); element != NULL;
		     element = cfg_list_next(element))
		{
			CHECK(disable_algorithms(cfg_listelt_value(element),
						 view->resolver));
		}
	}

	/*
	 * Set supported DS digest types.
	 */
	disabled = NULL;
	(void)named_config_get(maps, "disable-ds-digests", &disabled);
	if (disabled != NULL) {
		for (element = cfg_list_first(disabled); element != NULL;
		     element = cfg_list_next(element))
		{
			CHECK(disable_ds_digests(cfg_listelt_value(element),
						 view->resolver));
		}
	}

	/*
	 * A global or view "forwarders" option, if present,
	 * creates an entry for "." in the forwarding table.
	 */
	forwardtype = NULL;
	forwarders = NULL;
	(void)named_config_get(maps, "forward", &forwardtype);
	(void)named_config_get(maps, "forwarders", &forwarders);
	if (forwarders != NULL) {
		CHECK(configure_forward(config, view, dns_rootname, forwarders,
					forwardtype));
	}

	/*
	 * Dual Stack Servers.
	 */
	alternates = NULL;
	(void)named_config_get(maps, "dual-stack-servers", &alternates);
	if (alternates != NULL) {
		CHECK(configure_alternates(config, view, alternates));
	}

	/*
	 * We have default hints for class IN if we need them.
	 */
	if (view->rdclass == dns_rdataclass_in && view->hints == NULL) {
		dns_view_sethints(view, named_g_server->in_roothints);
	}

	/*
	 * If we still have no hints, this is a non-IN view with no
	 * "hints zone" configured.  Issue a warning, except if this
	 * is a root server.  Root servers never need to consult
	 * their hints, so it's no point requiring users to configure
	 * them.
	 */
	if (view->hints == NULL) {
		dns_zone_t *rootzone = NULL;
		(void)dns_view_findzone(view, dns_rootname, DNS_ZTFIND_EXACT,
					&rootzone);
		if (rootzone != NULL) {
			dns_zone_detach(&rootzone);
			need_hints = false;
		}
		if (need_hints) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
				      "no root hints for view '%s'",
				      view->name);
		}
	}

	/*
	 * Configure the view's transports (DoT/DoH)
	 */
	CHECK(named_transports_fromconfig(config, vconfig, view->mctx,
					  &transports));
	dns_view_settransports(view, transports);
	dns_transport_list_detach(&transports);

	/*
	 * Configure SIG(0) check limits when matching a DNS message to a view.
	 */
	obj = NULL;
	result = named_config_get(maps, "sig0key-checks-limit", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->sig0key_checks_limit = cfg_obj_asuint32(obj);

	obj = NULL;
	result = named_config_get(maps, "sig0message-checks-limit", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->sig0message_checks_limit = cfg_obj_asuint32(obj);

	/*
	 * Configure the view's TSIG keys.
	 */
	CHECK(named_tsigkeyring_fromconfig(config, vconfig, view->mctx, &ring));
	if (named_g_server->sessionkey != NULL) {
		dns_tsigkey_t *tsigkey = NULL;
		result = dns_tsigkey_createfromkey(
			named_g_server->session_keyname,
			named_g_server->session_keyalg,
			named_g_server->sessionkey, false, false, NULL, 0, 0,
			mctx, &tsigkey);
		if (result == ISC_R_SUCCESS) {
			result = dns_tsigkeyring_add(ring, tsigkey);
			dns_tsigkey_detach(&tsigkey);
		}
		CHECK(result);
	}
	dns_view_setkeyring(view, ring);
	dns_tsigkeyring_detach(&ring);

	/*
	 * See if we can re-use a dynamic key ring.
	 */
	result = dns_viewlist_find(&named_g_server->viewlist, view->name,
				   view->rdclass, &pview);
	if (result != ISC_R_NOTFOUND && result != ISC_R_SUCCESS) {
		goto cleanup;
	}
	if (pview != NULL) {
		dns_view_getdynamickeyring(pview, &ring);
		if (ring != NULL) {
			dns_view_setdynamickeyring(view, ring);
		}
		dns_tsigkeyring_detach(&ring);
		dns_view_detach(&pview);
	} else {
		dns_view_restorekeyring(view);
	}

	/*
	 * Configure the view's peer list.
	 */
	{
		const cfg_obj_t *peers = NULL;
		dns_peerlist_t *newpeers = NULL;

		(void)named_config_get(cfgmaps, "server", &peers);
		CHECK(dns_peerlist_new(mctx, &newpeers));
		for (element = cfg_list_first(peers); element != NULL;
		     element = cfg_list_next(element))
		{
			const cfg_obj_t *cpeer = cfg_listelt_value(element);
			dns_peer_t *peer;

			CHECK(configure_peer(cpeer, mctx, &peer));
			dns_peerlist_addpeer(newpeers, peer);
			dns_peer_detach(&peer);
		}
		dns_peerlist_detach(&view->peers);
		view->peers = newpeers; /* Transfer ownership. */
	}

	/*
	 *	Configure the views rrset-order.
	 */
	{
		const cfg_obj_t *rrsetorder = NULL;

		(void)named_config_get(maps, "rrset-order", &rrsetorder);
		CHECK(dns_order_create(mctx, &order));
		for (element = cfg_list_first(rrsetorder); element != NULL;
		     element = cfg_list_next(element))
		{
			const cfg_obj_t *ent = cfg_listelt_value(element);

			CHECK(configure_order(order, ent));
		}
		if (view->order != NULL) {
			dns_order_detach(&view->order);
		}
		dns_order_attach(order, &view->order);
		dns_order_detach(&order);
	}
	/*
	 * Copy the aclenv object.
	 */
	dns_aclenv_copy(view->aclenv, ns_interfacemgr_getaclenv(
					      named_g_server->interfacemgr));

	/*
	 * Configure the "match-clients" and "match-destinations" ACL.
	 * (These are only meaningful at the view level, but 'config'
	 * must be passed so that named ACLs defined at the global level
	 * can be retrieved.)
	 */
	CHECK(configure_view_acl(vconfig, config, NULL, "match-clients", NULL,
				 actx, named_g_mctx, &view->matchclients));
	CHECK(configure_view_acl(vconfig, config, NULL, "match-destinations",
				 NULL, actx, named_g_mctx,
				 &view->matchdestinations));

	/*
	 * Configure the "match-recursive-only" option.
	 */
	obj = NULL;
	(void)named_config_get(maps, "match-recursive-only", &obj);
	if (obj != NULL && cfg_obj_asboolean(obj)) {
		view->matchrecursiveonly = true;
	} else {
		view->matchrecursiveonly = false;
	}

	/*
	 * Configure other configurable data.
	 */
	obj = NULL;
	result = named_config_get(maps, "recursion", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->recursion = cfg_obj_asboolean(obj);

	obj = NULL;
	result = named_config_get(maps, "qname-minimization", &obj);
	INSIST(result == ISC_R_SUCCESS);
	qminmode = cfg_obj_asstring(obj);
	INSIST(qminmode != NULL);
	if (!strcmp(qminmode, "strict")) {
		view->qminimization = true;
		view->qmin_strict = true;
	} else if (!strcmp(qminmode, "relaxed")) {
		view->qminimization = true;
		view->qmin_strict = false;
	} else { /* "disabled" or "off" */
		view->qminimization = false;
		view->qmin_strict = false;
	}

	obj = NULL;
	result = named_config_get(maps, "auth-nxdomain", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->auth_nxdomain = cfg_obj_asboolean(obj);

	obj = NULL;
	result = named_config_get(maps, "minimal-any", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->minimal_any = cfg_obj_asboolean(obj);

	obj = NULL;
	result = named_config_get(maps, "minimal-responses", &obj);
	INSIST(result == ISC_R_SUCCESS);
	if (cfg_obj_isboolean(obj)) {
		if (cfg_obj_asboolean(obj)) {
			view->minimalresponses = dns_minimal_yes;
		} else {
			view->minimalresponses = dns_minimal_no;
		}
	} else {
		str = cfg_obj_asstring(obj);
		if (strcasecmp(str, "no-auth") == 0) {
			view->minimalresponses = dns_minimal_noauth;
		} else if (strcasecmp(str, "no-auth-recursive") == 0) {
			view->minimalresponses = dns_minimal_noauthrec;
		} else {
			UNREACHABLE();
		}
	}

	obj = NULL;
	result = named_config_get(maps, "transfer-format", &obj);
	INSIST(result == ISC_R_SUCCESS);
	str = cfg_obj_asstring(obj);
	if (strcasecmp(str, "many-answers") == 0) {
		view->transfer_format = dns_many_answers;
	} else if (strcasecmp(str, "one-answer") == 0) {
		view->transfer_format = dns_one_answer;
	} else {
		UNREACHABLE();
	}

	obj = NULL;
	result = named_config_get(maps, "trust-anchor-telemetry", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->trust_anchor_telemetry = cfg_obj_asboolean(obj);

	obj = NULL;
	result = named_config_get(maps, "root-key-sentinel", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->root_key_sentinel = cfg_obj_asboolean(obj);

	/*
	 * Set the "allow-query", "allow-query-cache", "allow-recursion",
	 * "allow-recursion-on" and "allow-query-cache-on" ACLs if
	 * configured in named.conf, but NOT from the global defaults.
	 * This is done by leaving the third argument to configure_view_acl()
	 * NULL.
	 *
	 * We ignore the global defaults here because these ACLs
	 * can inherit from each other.  If any are still unset after
	 * applying the inheritance rules, we'll look up the defaults at
	 * that time.
	 */

	/* named.conf only */
	CHECK(configure_view_acl(vconfig, config, NULL, "allow-query", NULL,
				 actx, named_g_mctx, &view->queryacl));

	/* named.conf only */
	CHECK(configure_view_acl(vconfig, config, NULL, "allow-query-cache",
				 NULL, actx, named_g_mctx, &view->cacheacl));
	/* named.conf only */
	CHECK(configure_view_acl(vconfig, config, NULL, "allow-query-cache-on",
				 NULL, actx, named_g_mctx, &view->cacheonacl));

	CHECK(configure_view_acl(vconfig, config, named_g_config, "allow-proxy",
				 NULL, actx, named_g_mctx, &view->proxyacl));

	CHECK(configure_view_acl(vconfig, config, named_g_config,
				 "allow-proxy-on", NULL, actx, named_g_mctx,
				 &view->proxyonacl));

	if (strcmp(view->name, "_bind") != 0 &&
	    view->rdclass != dns_rdataclass_chaos)
	{
		/* named.conf only */
		CHECK(configure_view_acl(vconfig, config, NULL,
					 "allow-recursion", NULL, actx,
					 named_g_mctx, &view->recursionacl));
		/* named.conf only */
		CHECK(configure_view_acl(vconfig, config, NULL,
					 "allow-recursion-on", NULL, actx,
					 named_g_mctx, &view->recursiononacl));
	}

	if (view->recursion) {
		/*
		 * "allow-query-cache" inherits from "allow-recursion" if set,
		 * otherwise from "allow-query" if set.
		 */
		if (view->cacheacl == NULL) {
			if (view->recursionacl != NULL) {
				dns_acl_attach(view->recursionacl,
					       &view->cacheacl);
			} else if (view->queryacl != NULL) {
				dns_acl_attach(view->queryacl, &view->cacheacl);
			}
		}

		/*
		 * "allow-recursion" inherits from "allow-query-cache" if set,
		 * otherwise from "allow-query" if set.
		 */
		if (view->recursionacl == NULL) {
			if (view->cacheacl != NULL) {
				dns_acl_attach(view->cacheacl,
					       &view->recursionacl);
			} else if (view->queryacl != NULL) {
				dns_acl_attach(view->queryacl,
					       &view->recursionacl);
			}
		}

		/*
		 * "allow-query-cache-on" inherits from "allow-recursion-on"
		 * if set.
		 */
		if (view->cacheonacl == NULL) {
			if (view->recursiononacl != NULL) {
				dns_acl_attach(view->recursiononacl,
					       &view->cacheonacl);
			}
		}

		/*
		 * "allow-recursion-on" inherits from "allow-query-cache-on"
		 * if set.
		 */
		if (view->recursiononacl == NULL) {
			if (view->cacheonacl != NULL) {
				dns_acl_attach(view->cacheonacl,
					       &view->recursiononacl);
			}
		}

		/*
		 * If any are still unset at this point, we now get default
		 * values for from the global config.
		 */

		if (view->recursionacl == NULL) {
			/* global default only */
			CHECK(configure_view_acl(
				NULL, NULL, named_g_config, "allow-recursion",
				NULL, actx, named_g_mctx, &view->recursionacl));
		}
		if (view->recursiononacl == NULL) {
			/* global default only */
			CHECK(configure_view_acl(NULL, NULL, named_g_config,
						 "allow-recursion-on", NULL,
						 actx, named_g_mctx,
						 &view->recursiononacl));
		}
		if (view->cacheacl == NULL) {
			/* global default only */
			CHECK(configure_view_acl(
				NULL, NULL, named_g_config, "allow-query-cache",
				NULL, actx, named_g_mctx, &view->cacheacl));
		}
		if (view->cacheonacl == NULL) {
			/* global default only */
			CHECK(configure_view_acl(NULL, NULL, named_g_config,
						 "allow-query-cache-on", NULL,
						 actx, named_g_mctx,
						 &view->cacheonacl));
		}
	} else {
		/*
		 * We're not recursive; if the query-cache ACLs haven't
		 * been set at the options/view level, set them to none.
		 */
		if (view->cacheacl == NULL) {
			CHECK(dns_acl_none(mctx, &view->cacheacl));
		}
		if (view->cacheonacl == NULL) {
			CHECK(dns_acl_none(mctx, &view->cacheonacl));
		}
	}

	/*
	 * Finished setting recursion and query-cache ACLs, so now we
	 * can get the allow-query default if it wasn't set in named.conf
	 */
	if (view->queryacl == NULL) {
		/* global default only */
		CHECK(configure_view_acl(NULL, NULL, named_g_config,
					 "allow-query", NULL, actx,
					 named_g_mctx, &view->queryacl));
	}

	/*
	 * Ignore case when compressing responses to the specified
	 * clients. This causes case not always to be preserved,
	 * and is needed by some broken clients.
	 */
	CHECK(configure_view_acl(vconfig, config, named_g_config,
				 "no-case-compress", NULL, actx, named_g_mctx,
				 &view->nocasecompress));

	/*
	 * Disable name compression completely, this is a tradeoff
	 * between CPU and network usage.
	 */
	obj = NULL;
	result = named_config_get(maps, "message-compression", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->msgcompression = cfg_obj_asboolean(obj);

	/*
	 * Filter setting on addresses in the answer section.
	 */
	CHECK(configure_view_acl(vconfig, config, named_g_config,
				 "deny-answer-addresses", "acl", actx,
				 named_g_mctx, &view->denyansweracl));
	CHECK(configure_view_nametable(vconfig, config, "deny-answer-addresses",
				       "except-from", named_g_mctx,
				       &view->answeracl_exclude));

	/*
	 * Filter setting on names (CNAME/DNAME targets) in the answer section.
	 */
	CHECK(configure_view_nametable(vconfig, config, "deny-answer-aliases",
				       "name", named_g_mctx,
				       &view->denyanswernames));
	CHECK(configure_view_nametable(vconfig, config, "deny-answer-aliases",
				       "except-from", named_g_mctx,
				       &view->answernames_exclude));

	/*
	 * Configure sortlist, if set
	 */
	CHECK(configure_view_sortlist(vconfig, config, actx, named_g_mctx,
				      &view->sortlist));

	/*
	 * Configure default allow-update and allow-update-forwarding ACLs,
	 * so they can be inherited by zones. (XXX: These are not
	 * read from the options/view level here. However, they may be
	 * read from there in zoneconf.c:configure_zone_acl() later.)
	 */
	if (view->updateacl == NULL) {
		CHECK(configure_view_acl(NULL, NULL, named_g_config,
					 "allow-update", NULL, actx,
					 named_g_mctx, &view->updateacl));
	}
	if (view->upfwdacl == NULL) {
		CHECK(configure_view_acl(NULL, NULL, named_g_config,
					 "allow-update-forwarding", NULL, actx,
					 named_g_mctx, &view->upfwdacl));
	}

	/*
	 * Configure default allow-transfer and allow-notify ACLs so they
	 * can be inherited by zones.
	 */
	if (view->transferacl == NULL) {
		CHECK(configure_view_acl(vconfig, config, named_g_config,
					 "allow-transfer", NULL, actx,
					 named_g_mctx, &view->transferacl));
	}
	if (view->notifyacl == NULL) {
		CHECK(configure_view_acl(vconfig, config, named_g_config,
					 "allow-notify", NULL, actx,
					 named_g_mctx, &view->notifyacl));
	}

	obj = NULL;
	result = named_config_get(maps, "provide-ixfr", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->provideixfr = cfg_obj_asboolean(obj);

	obj = NULL;
	result = named_config_get(maps, "request-nsid", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->requestnsid = cfg_obj_asboolean(obj);

	obj = NULL;
	result = named_config_get(maps, "send-cookie", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->sendcookie = cfg_obj_asboolean(obj);

	obj = NULL;
	if (view->pad_acl != NULL) {
		dns_acl_detach(&view->pad_acl);
	}
	result = named_config_get(optionmaps, "response-padding", &obj);
	if (result == ISC_R_SUCCESS) {
		const cfg_obj_t *padobj = cfg_tuple_get(obj, "block-size");
		const cfg_obj_t *aclobj = cfg_tuple_get(obj, "acl");
		uint32_t padding = cfg_obj_asuint32(padobj);

		if (padding > 512U) {
			cfg_obj_log(obj, named_g_lctx, ISC_LOG_WARNING,
				    "response-padding block-size cannot "
				    "exceed 512: lowering");
			padding = 512U;
		}
		view->padding = (uint16_t)padding;
		CHECK(cfg_acl_fromconfig(aclobj, config, named_g_lctx, actx,
					 named_g_mctx, 0, &view->pad_acl));
	}

	obj = NULL;
	result = named_config_get(maps, "require-server-cookie", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->requireservercookie = cfg_obj_asboolean(obj);

	obj = NULL;
	result = named_config_get(maps, "v6-bias", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->v6bias = cfg_obj_asuint32(obj) * 1000;

	obj = NULL;
	result = named_config_get(maps, "clients-per-query", &obj);
	INSIST(result == ISC_R_SUCCESS);
	clients_per_query = cfg_obj_asuint32(obj);

	obj = NULL;
	result = named_config_get(maps, "max-clients-per-query", &obj);
	INSIST(result == ISC_R_SUCCESS);
	max_clients_per_query = cfg_obj_asuint32(obj);

	if (max_clients_per_query < clients_per_query) {
		cfg_obj_log(obj, named_g_lctx, ISC_LOG_WARNING,
			    "configured clients-per-query (%u) exceeds "
			    "max-clients-per-query (%u); automatically "
			    "adjusting max-clients-per-query to (%u)",
			    clients_per_query, max_clients_per_query,
			    clients_per_query);
		max_clients_per_query = clients_per_query;
	}
	dns_resolver_setclientsperquery(view->resolver, clients_per_query,
					max_clients_per_query);

	/*
	 * This is used for the cache and also as a default value
	 * for zone databases.
	 */
	obj = NULL;
	result = named_config_get(maps, "max-records-per-type", &obj);
	INSIST(result == ISC_R_SUCCESS);
	dns_view_setmaxrrperset(view, cfg_obj_asuint32(obj));

	/*
	 * This is used for the cache and also as a default value
	 * for zone databases.
	 */
	obj = NULL;
	result = named_config_get(maps, "max-types-per-name", &obj);
	INSIST(result == ISC_R_SUCCESS);
	dns_view_setmaxtypepername(view, cfg_obj_asuint32(obj));

	obj = NULL;
	result = named_config_get(maps, "max-recursion-depth", &obj);
	INSIST(result == ISC_R_SUCCESS);
	dns_resolver_setmaxdepth(view->resolver, cfg_obj_asuint32(obj));

	obj = NULL;
	result = named_config_get(maps, "max-recursion-queries", &obj);
	INSIST(result == ISC_R_SUCCESS);
	dns_resolver_setmaxqueries(view->resolver, cfg_obj_asuint32(obj));

	obj = NULL;
	result = named_config_get(maps, "max-query-restarts", &obj);
	INSIST(result == ISC_R_SUCCESS);
	dns_view_setmaxrestarts(view, cfg_obj_asuint32(obj));

	obj = NULL;
	result = named_config_get(maps, "max-query-count", &obj);
	INSIST(result == ISC_R_SUCCESS);
	dns_view_setmaxqueries(view, cfg_obj_asuint32(obj));

	obj = NULL;
	result = named_config_get(maps, "max-validations-per-fetch", &obj);
	if (result == ISC_R_SUCCESS) {
		dns_resolver_setmaxvalidations(view->resolver,
					       cfg_obj_asuint32(obj));
	}

	obj = NULL;
	result = named_config_get(maps, "max-validation-failures-per-fetch",
				  &obj);
	if (result == ISC_R_SUCCESS) {
		dns_resolver_setmaxvalidationfails(view->resolver,
						   cfg_obj_asuint32(obj));
	}

	obj = NULL;
	result = named_config_get(maps, "fetches-per-zone", &obj);
	INSIST(result == ISC_R_SUCCESS);
	obj2 = cfg_tuple_get(obj, "fetches");
	dns_resolver_setfetchesperzone(view->resolver, cfg_obj_asuint32(obj2));
	obj2 = cfg_tuple_get(obj, "response");
	if (!cfg_obj_isvoid(obj2)) {
		const char *resp = cfg_obj_asstring(obj2);
		isc_result_t r = DNS_R_SERVFAIL;

		if (strcasecmp(resp, "drop") == 0) {
			r = DNS_R_DROP;
		} else if (strcasecmp(resp, "fail") == 0) {
			r = DNS_R_SERVFAIL;
		} else {
			UNREACHABLE();
		}

		dns_resolver_setquotaresponse(view->resolver,
					      dns_quotatype_zone, r);
	}

	obj = NULL;
	result = named_config_get(maps, "prefetch", &obj);
	INSIST(result == ISC_R_SUCCESS);
	prefetch_trigger = cfg_tuple_get(obj, "trigger");
	view->prefetch_trigger = cfg_obj_asuint32(prefetch_trigger);
	if (view->prefetch_trigger > 10) {
		view->prefetch_trigger = 10;
	}
	prefetch_eligible = cfg_tuple_get(obj, "eligible");
	if (cfg_obj_isvoid(prefetch_eligible)) {
		int m;
		for (m = 1; maps[m] != NULL; m++) {
			obj = NULL;
			result = named_config_get(&maps[m], "prefetch", &obj);
			INSIST(result == ISC_R_SUCCESS);
			prefetch_eligible = cfg_tuple_get(obj, "eligible");
			if (cfg_obj_isuint32(prefetch_eligible)) {
				break;
			}
		}
		INSIST(cfg_obj_isuint32(prefetch_eligible));
	}
	view->prefetch_eligible = cfg_obj_asuint32(prefetch_eligible);
	if (view->prefetch_eligible < view->prefetch_trigger + 6) {
		view->prefetch_eligible = view->prefetch_trigger + 6;
	}

	/*
	 * For now, there is only one kind of trusted keys, the
	 * "security roots".
	 */
	CHECK(configure_view_dnsseckeys(view, vconfig, config, bindkeys,
					auto_root));

	obj = NULL;
	result = named_config_get(maps, "dnssec-must-be-secure", &obj);
	if (result == ISC_R_SUCCESS) {
		CHECK(mustbesecure(obj, view->resolver));
	}

	obj = NULL;
	result = named_config_get(maps, "nta-recheck", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->nta_recheck = cfg_obj_asduration(obj);

	obj = NULL;
	result = named_config_get(maps, "nta-lifetime", &obj);
	INSIST(result == ISC_R_SUCCESS);
	view->nta_lifetime = cfg_obj_asduration(obj);

	obj = NULL;
	result = named_config_get(maps, "preferred-glue", &obj);
	if (result == ISC_R_SUCCESS) {
		str = cfg_obj_asstring(obj);
		if (strcasecmp(str, "a") == 0) {
			view->preferred_glue = dns_rdatatype_a;
		} else if (strcasecmp(str, "aaaa") == 0) {
			view->preferred_glue = dns_rdatatype_aaaa;
		} else {
			view->preferred_glue = 0;
		}
	} else {
		view->preferred_glue = 0;
	}

	/*
	 * Load DynDB modules.
	 */
	dyndb_list = NULL;
	if (voptions != NULL) {
		(void)cfg_map_get(voptions, "dyndb", &dyndb_list);
	} else {
		(void)cfg_map_get(config, "dyndb", &dyndb_list);
	}

	for (element = cfg_list_first(dyndb_list); element != NULL;
	     element = cfg_list_next(element))
	{
		const cfg_obj_t *dyndb = cfg_listelt_value(element);

		if (dctx == NULL) {
			const void *hashinit = isc_hash_get_initializer();
			CHECK(dns_dyndb_createctx(mctx, hashinit, named_g_lctx,
						  view, named_g_server->zonemgr,
						  named_g_loopmgr, &dctx));
		}

		CHECK(configure_dyndb(dyndb, mctx, dctx));
	}

	/*
	 * Load plugins.
	 */
	plugin_list = NULL;
	if (voptions != NULL) {
		(void)cfg_map_get(voptions, "plugin", &plugin_list);
	} else {
		(void)cfg_map_get(config, "plugin", &plugin_list);
	}

	if (plugin_list != NULL) {
		INSIST(view->hooktable == NULL);
		CHECK(ns_hooktable_create(view->mctx,
					  (ns_hooktable_t **)&view->hooktable));
		view->hooktable_free = ns_hooktable_free;

		ns_plugins_create(view->mctx, (ns_plugins_t **)&view->plugins);
		view->plugins_free = ns_plugins_free;

		CHECK(cfg_pluginlist_foreach(config, plugin_list, named_g_lctx,
					     register_one_plugin, view));
	}

	/*
	 * Setup automatic empty zones.  If recursion is off then
	 * they are disabled by default.
	 */
	obj = NULL;
	(void)named_config_get(maps, "empty-zones-enable", &obj);
	(void)named_config_get(maps, "disable-empty-zone", &disablelist);
	if (obj == NULL && disablelist == NULL &&
	    view->rdclass == dns_rdataclass_in)
	{
		empty_zones_enable = view->recursion;
	} else if (view->rdclass == dns_rdataclass_in) {
		if (obj != NULL) {
			empty_zones_enable = cfg_obj_asboolean(obj);
		} else {
			empty_zones_enable = view->recursion;
		}
	} else {
		empty_zones_enable = false;
	}

	if (empty_zones_enable) {
		const char *empty;
		int empty_zone = 0;
		dns_fixedname_t fixed;
		dns_name_t *name;
		isc_buffer_t buffer;
		char server[DNS_NAME_FORMATSIZE + 1];
		char contact[DNS_NAME_FORMATSIZE + 1];
		const char *empty_dbtype[4] = { "_builtin", "empty", NULL,
						NULL };
		int empty_dbtypec = 4;
		dns_zonestat_level_t statlevel = dns_zonestat_none;

		name = dns_fixedname_initname(&fixed);

		obj = NULL;
		result = named_config_get(maps, "empty-server", &obj);
		if (result == ISC_R_SUCCESS) {
			CHECK(dns_name_fromstring(name, cfg_obj_asstring(obj),
						  dns_rootname, 0, NULL));
			isc_buffer_init(&buffer, server, sizeof(server) - 1);
			CHECK(dns_name_totext(name, 0, &buffer));
			server[isc_buffer_usedlength(&buffer)] = 0;
			empty_dbtype[2] = server;
		} else {
			empty_dbtype[2] = "@";
		}

		obj = NULL;
		result = named_config_get(maps, "empty-contact", &obj);
		if (result == ISC_R_SUCCESS) {
			CHECK(dns_name_fromstring(name, cfg_obj_asstring(obj),
						  dns_rootname, 0, NULL));
			isc_buffer_init(&buffer, contact, sizeof(contact) - 1);
			CHECK(dns_name_totext(name, 0, &buffer));
			contact[isc_buffer_usedlength(&buffer)] = 0;
			empty_dbtype[3] = contact;
		} else {
			empty_dbtype[3] = ".";
		}

		obj = NULL;
		result = named_config_get(maps, "zone-statistics", &obj);
		INSIST(result == ISC_R_SUCCESS);
		if (cfg_obj_isboolean(obj)) {
			if (cfg_obj_asboolean(obj)) {
				statlevel = dns_zonestat_full;
			} else {
				statlevel = dns_zonestat_none;
			}
		} else {
			const char *levelstr = cfg_obj_asstring(obj);
			if (strcasecmp(levelstr, "full") == 0) {
				statlevel = dns_zonestat_full;
			} else if (strcasecmp(levelstr, "terse") == 0) {
				statlevel = dns_zonestat_terse;
			} else if (strcasecmp(levelstr, "none") == 0) {
				statlevel = dns_zonestat_none;
			} else {
				UNREACHABLE();
			}
		}

		for (empty = empty_zones[empty_zone]; empty != NULL;
		     empty = empty_zones[++empty_zone])
		{
			dns_forwarders_t *dnsforwarders = NULL;
			dns_fwdpolicy_t fwdpolicy = dns_fwdpolicy_none;

			/*
			 * Look for zone on drop list.
			 */
			CHECK(dns_name_fromstring(name, empty, dns_rootname, 0,
						  NULL));
			if (disablelist != NULL &&
			    on_disable_list(disablelist, name))
			{
				continue;
			}

			/*
			 * This zone already exists.
			 */
			(void)dns_view_findzone(view, name, DNS_ZTFIND_EXACT,
						&zone);
			if (zone != NULL) {
				dns_zone_detach(&zone);
				continue;
			}

			/*
			 * If we would forward this name don't add a
			 * empty zone for it.
			 */
			result = dns_fwdtable_find(view->fwdtable, name,
						   &dnsforwarders);
			if (result == ISC_R_SUCCESS ||
			    result == DNS_R_PARTIALMATCH)
			{
				fwdpolicy = dnsforwarders->fwdpolicy;
				dns_forwarders_detach(&dnsforwarders);
			}
			if (fwdpolicy == dns_fwdpolicy_only) {
				continue;
			}

			/*
			 * See if we can re-use a existing zone.
			 */
			result = dns_viewlist_find(&named_g_server->viewlist,
						   view->name, view->rdclass,
						   &pview);
			if (result != ISC_R_NOTFOUND && result != ISC_R_SUCCESS)
			{
				goto cleanup;
			}

			if (pview != NULL) {
				(void)dns_view_findzone(
					pview, name, DNS_ZTFIND_EXACT, &zone);
				dns_view_detach(&pview);
			}

			CHECK(create_empty_zone(zone, name, view, zonelist,
						empty_dbtype, empty_dbtypec,
						statlevel));
			if (zone != NULL) {
				dns_zone_detach(&zone);
			}
		}
	}

	obj = NULL;
	if (view->rdclass == dns_rdataclass_in) {
		(void)named_config_get(maps, "ipv4only-enable", &obj);
	}
	if (view->rdclass == dns_rdataclass_in && (obj != NULL)
		    ? cfg_obj_asboolean(obj)
		    : !ISC_LIST_EMPTY(view->dns64))
	{
		const char *server, *contact;
		dns_fixedname_t fixed;
		dns_name_t *name;
		struct {
			const char *name;
			const char *type;
		} zones[] = {
			{ "ipv4only.arpa", "ipv4only" },
			{ "170.0.0.192.in-addr.arpa", "ipv4reverse" },
			{ "171.0.0.192.in-addr.arpa", "ipv4reverse" },
		};
		size_t ipv4only_zone;

		obj = NULL;
		result = named_config_get(maps, "ipv4only-server", &obj);
		if (result == ISC_R_SUCCESS) {
			server = cfg_obj_asstring(obj);
		} else {
			server = NULL;
		}

		obj = NULL;
		result = named_config_get(maps, "ipv4only-contact", &obj);
		if (result == ISC_R_SUCCESS) {
			contact = cfg_obj_asstring(obj);
		} else {
			contact = NULL;
		}

		name = dns_fixedname_initname(&fixed);
		for (ipv4only_zone = 0; ipv4only_zone < ARRAY_SIZE(zones);
		     ipv4only_zone++)
		{
			dns_forwarders_t *dnsforwarders = NULL;
			dns_fwdpolicy_t fwdpolicy = dns_fwdpolicy_none;

			CHECK(dns_name_fromstring(name,
						  zones[ipv4only_zone].name,
						  dns_rootname, 0, NULL));

			(void)dns_view_findzone(view, name, DNS_ZTFIND_EXACT,
						&zone);
			if (zone != NULL) {
				dns_zone_detach(&zone);
				continue;
			}

			/*
			 * If we would forward this name don't add it.
			 */
			result = dns_fwdtable_find(view->fwdtable, name,
						   &dnsforwarders);
			if (result == ISC_R_SUCCESS ||
			    result == DNS_R_PARTIALMATCH)
			{
				fwdpolicy = dnsforwarders->fwdpolicy;
				dns_forwarders_detach(&dnsforwarders);
			}
			if (fwdpolicy == dns_fwdpolicy_only) {
				continue;
			}

			/*
			 * See if we can re-use a existing zone.
			 */
			result = dns_viewlist_find(&named_g_server->viewlist,
						   view->name, view->rdclass,
						   &pview);
			if (result != ISC_R_NOTFOUND && result != ISC_R_SUCCESS)
			{
				goto cleanup;
			}

			if (pview != NULL) {
				(void)dns_view_findzone(
					pview, name, DNS_ZTFIND_EXACT, &zone);
				dns_view_detach(&pview);
			}

			CHECK(create_ipv4only_zone(zone, view, name,
						   zones[ipv4only_zone].type,
						   mctx, server, contact));
			if (zone != NULL) {
				dns_zone_detach(&zone);
			}
		}
	}

	obj = NULL;
	result = named_config_get(maps, "rate-limit", &obj);
	if (result == ISC_R_SUCCESS) {
		result = configure_rrl(view, config, obj);
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}
	}

	/*
	 * Set the servfail-ttl.
	 */
	obj = NULL;
	result = named_config_get(maps, "servfail-ttl", &obj);
	INSIST(result == ISC_R_SUCCESS);
	fail_ttl = cfg_obj_asduration(obj);
	if (fail_ttl > 30) {
		fail_ttl = 30;
	}
	dns_view_setfailttl(view, fail_ttl);

	/*
	 * Name space to look up redirect information in.
	 */
	obj = NULL;
	result = named_config_get(maps, "nxdomain-redirect", &obj);
	if (result == ISC_R_SUCCESS) {
		dns_name_t *name = dns_fixedname_name(&view->redirectfixed);
		CHECK(dns_name_fromstring(name, cfg_obj_asstring(obj),
					  dns_rootname, 0, NULL));
		view->redirectzone = name;
	} else {
		view->redirectzone = NULL;
	}

	/*
	 * Exceptions to DNSSEC validation.
	 */
	obj = NULL;
	result = named_config_get(maps, "validate-except", &obj);
	if (result == ISC_R_SUCCESS) {
		result = dns_view_getntatable(view, &ntatable);
	}
	if (result == ISC_R_SUCCESS) {
		for (element = cfg_list_first(obj); element != NULL;
		     element = cfg_list_next(element))
		{
			dns_fixedname_t fntaname;
			dns_name_t *ntaname;

			ntaname = dns_fixedname_initname(&fntaname);
			obj = cfg_listelt_value(element);
			CHECK(dns_name_fromstring(ntaname,
						  cfg_obj_asstring(obj),
						  dns_rootname, 0, NULL));
			CHECK(dns_ntatable_add(ntatable, ntaname, true, 0,
					       0xffffffffU));
		}
	}

#ifdef HAVE_DNSTAP
	/*
	 * Set up the dnstap environment and configure message
	 * types to log.
	 */
	CHECK(configure_dnstap(maps, view));
#endif /* HAVE_DNSTAP */

	result = ISC_R_SUCCESS;

cleanup:
	/*
	 * Revert to the old view if there was an error.
	 */
	if (result != ISC_R_SUCCESS) {
		isc_result_t result2;

		result2 = dns_viewlist_find(&named_g_server->viewlist,
					    view->name, view->rdclass, &pview);
		if (result2 == ISC_R_SUCCESS) {
			dns_view_thaw(pview);

			obj = NULL;
			if (rpz_configured &&
			    pview->rdclass == dns_rdataclass_in && need_hints &&
			    named_config_get(maps, "response-policy", &obj) ==
				    ISC_R_SUCCESS)
			{
				/*
				 * We are swapping the places of the `view` and
				 * `pview` in the function's parameters list
				 * because we are reverting the same operation
				 * done previously in the "correct" order.
				 */
				result2 = configure_rpz(pview, view, maps, obj,
							&old_rpz_ok);
				if (result2 != ISC_R_SUCCESS) {
					isc_log_write(named_g_lctx,
						      NAMED_LOGCATEGORY_GENERAL,
						      NAMED_LOGMODULE_SERVER,
						      ISC_LOG_ERROR,
						      "rpz configuration "
						      "revert failed for view "
						      "'%s'",
						      pview->name);
				}
			}

			obj = NULL;
			if (catz_configured &&
			    pview->rdclass == dns_rdataclass_in && need_hints &&
			    named_config_get(maps, "catalog-zones", &obj) ==
				    ISC_R_SUCCESS)
			{
				/*
				 * We are swapping the places of the `view` and
				 * `pview` in the function's parameters list
				 * because we are reverting the same operation
				 * done previously in the "correct" order.
				 */
				result2 = configure_catz(pview, view, config,
							 obj);
				if (result2 != ISC_R_SUCCESS) {
					isc_log_write(named_g_lctx,
						      NAMED_LOGCATEGORY_GENERAL,
						      NAMED_LOGMODULE_SERVER,
						      ISC_LOG_ERROR,
						      "catz configuration "
						      "revert failed for view "
						      "'%s'",
						      pview->name);
				}
			}

			dns_view_freeze(pview);
		}

		if (pview != NULL) {
			dns_view_detach(&pview);
		}

		if (zone_element_latest != NULL) {
			for (element = cfg_list_first(zonelist);
			     element != NULL; element = cfg_list_next(element))
			{
				const cfg_obj_t *zconfig =
					cfg_listelt_value(element);
				configure_zone_setviewcommit(result, zconfig,
							     view);
				if (element == zone_element_latest) {
					/*
					 * This was the latest element that was
					 * successfully configured earlier.
					 */
					break;
				}
			}
		}
	}

	if (ntatable != NULL) {
		dns_ntatable_detach(&ntatable);
	}
	if (clients != NULL) {
		dns_acl_detach(&clients);
	}
	if (mapped != NULL) {
		dns_acl_detach(&mapped);
	}
	if (excluded != NULL) {
		dns_acl_detach(&excluded);
	}
	if (ring != NULL) {
		dns_tsigkeyring_detach(&ring);
	}
	if (zone != NULL) {
		dns_zone_detach(&zone);
	}
	if (dispatch4 != NULL) {
		dns_dispatch_detach(&dispatch4);
	}
	if (dispatch6 != NULL) {
		dns_dispatch_detach(&dispatch6);
	}
	if (resstats != NULL) {
		isc_stats_detach(&resstats);
	}
	if (resquerystats != NULL) {
		dns_stats_detach(&resquerystats);
	}
	if (order != NULL) {
		dns_order_detach(&order);
	}
	if (cache != NULL) {
		dns_cache_detach(&cache);
	}
	if (dctx != NULL) {
		dns_dyndb_destroyctx(&dctx);
	}

	return result;
}

static isc_result_t
configure_hints(dns_view_t *view, const char *filename) {
	isc_result_t result;
	dns_db_t *db;

	db = NULL;
	result = dns_rootns_create(view->mctx, view->rdclass, filename, &db);
	if (result == ISC_R_SUCCESS) {
		dns_view_sethints(view, db);
		dns_db_detach(&db);
	}

	return result;
}

static isc_result_t
configure_alternates(const cfg_obj_t *config, dns_view_t *view,
		     const cfg_obj_t *alternates) {
	const cfg_obj_t *portobj;
	const cfg_obj_t *addresses;
	const cfg_listelt_t *element;
	isc_result_t result = ISC_R_SUCCESS;
	in_port_t port;

	/*
	 * Determine which port to send requests to.
	 */
	CHECKM(named_config_getport(config, "port", &port), "port");

	if (alternates != NULL) {
		portobj = cfg_tuple_get(alternates, "port");
		if (cfg_obj_isuint32(portobj)) {
			uint32_t val = cfg_obj_asuint32(portobj);
			if (val > UINT16_MAX) {
				cfg_obj_log(portobj, named_g_lctx,
					    ISC_LOG_ERROR,
					    "port '%u' out of range", val);
				return ISC_R_RANGE;
			}
			port = (in_port_t)val;
		}
	}

	addresses = NULL;
	if (alternates != NULL) {
		addresses = cfg_tuple_get(alternates, "addresses");
	}

	for (element = cfg_list_first(addresses); element != NULL;
	     element = cfg_list_next(element))
	{
		const cfg_obj_t *alternate = cfg_listelt_value(element);
		isc_sockaddr_t sa;

		if (!cfg_obj_issockaddr(alternate)) {
			dns_fixedname_t fixed;
			dns_name_t *name;
			const char *str = cfg_obj_asstring(
				cfg_tuple_get(alternate, "name"));
			isc_buffer_t buffer;
			in_port_t myport = port;

			isc_buffer_constinit(&buffer, str, strlen(str));
			isc_buffer_add(&buffer, strlen(str));
			name = dns_fixedname_initname(&fixed);
			CHECK(dns_name_fromtext(name, &buffer, dns_rootname, 0,
						NULL));

			portobj = cfg_tuple_get(alternate, "port");
			if (cfg_obj_isuint32(portobj)) {
				uint32_t val = cfg_obj_asuint32(portobj);
				if (val > UINT16_MAX) {
					cfg_obj_log(portobj, named_g_lctx,
						    ISC_LOG_ERROR,
						    "port '%u' out of range",
						    val);
					return ISC_R_RANGE;
				}
				myport = (in_port_t)val;
			}
			dns_resolver_addalternate(view->resolver, NULL, name,
						  myport);
			continue;
		}

		sa = *cfg_obj_assockaddr(alternate);
		if (isc_sockaddr_getport(&sa) == 0) {
			isc_sockaddr_setport(&sa, port);
		}
		dns_resolver_addalternate(view->resolver, &sa, NULL, 0);
	}

cleanup:
	return result;
}

static isc_result_t
validate_tls(const cfg_obj_t *config, dns_view_t *view, const cfg_obj_t *obj,
	     isc_log_t *logctx, const char *str, dns_name_t **name) {
	dns_fixedname_t fname;
	dns_name_t *nm = dns_fixedname_initname(&fname);
	isc_result_t result = dns_name_fromstring(nm, str, dns_rootname, 0,
						  NULL);

	if (result != ISC_R_SUCCESS) {
		cfg_obj_log(obj, logctx, ISC_LOG_ERROR,
			    "'%s' is not a valid name", str);
		return result;
	}

	if (strcasecmp(str, "ephemeral") != 0) {
		const cfg_obj_t *tlsmap = find_maplist(config, "tls", str);

		if (tlsmap == NULL) {
			cfg_obj_log(obj, logctx, ISC_LOG_ERROR,
				    "tls '%s' is not defined", str);
			return ISC_R_FAILURE;
		}
	}

	if (name != NULL && *name == NULL) {
		*name = isc_mem_get(view->mctx, sizeof(dns_name_t));
		dns_name_init(*name, NULL);
		dns_name_dup(nm, view->mctx, *name);
	}

	return ISC_R_SUCCESS;
}

static isc_result_t
configure_forward(const cfg_obj_t *config, dns_view_t *view,
		  const dns_name_t *origin, const cfg_obj_t *forwarders,
		  const cfg_obj_t *forwardtype) {
	const cfg_obj_t *portobj = NULL;
	const cfg_obj_t *tlspobj = NULL;
	const cfg_obj_t *faddresses = NULL;
	const cfg_listelt_t *element = NULL;
	dns_fwdpolicy_t fwdpolicy = dns_fwdpolicy_none;
	dns_forwarderlist_t fwdlist;
	dns_forwarder_t *fwd = NULL;
	isc_result_t result;
	in_port_t port;
	in_port_t tls_port;
	const char *tls = NULL;

	ISC_LIST_INIT(fwdlist);

	/*
	 * Determine which port to send forwarded requests to.
	 */
	CHECKM(named_config_getport(config, "port", &port), "port");
	CHECKM(named_config_getport(config, "tls-port", &tls_port), "tls-port");

	if (forwarders != NULL) {
		portobj = cfg_tuple_get(forwarders, "port");
		if (cfg_obj_isuint32(portobj)) {
			uint32_t val = cfg_obj_asuint32(portobj);
			if (val > UINT16_MAX) {
				cfg_obj_log(portobj, named_g_lctx,
					    ISC_LOG_ERROR,
					    "port '%u' out of range", val);
				return ISC_R_RANGE;
			}
			port = tls_port = (in_port_t)val;
		}
	}

	/*
	 * TLS value for forwarded requests.
	 */
	if (forwarders != NULL) {
		tlspobj = cfg_tuple_get(forwarders, "tls");
		if (cfg_obj_isstring(tlspobj)) {
			tls = cfg_obj_asstring(tlspobj);
			if (tls != NULL) {
				result = validate_tls(config, view, tlspobj,
						      named_g_lctx, tls, NULL);
				if (result != ISC_R_SUCCESS) {
					return result;
				}
			}
		}
	}

	faddresses = NULL;
	if (forwarders != NULL) {
		faddresses = cfg_tuple_get(forwarders, "addresses");
	}

	for (element = cfg_list_first(faddresses); element != NULL;
	     element = cfg_list_next(element))
	{
		const cfg_obj_t *forwarder = cfg_listelt_value(element);
		const char *cur_tls = NULL;

		fwd = isc_mem_get(view->mctx, sizeof(dns_forwarder_t));
		fwd->tlsname = NULL;
		cur_tls = cfg_obj_getsockaddrtls(forwarder);
		if (cur_tls == NULL) {
			cur_tls = tls;
		}
		if (cur_tls != NULL) {
			result = validate_tls(config, view, faddresses,
					      named_g_lctx, cur_tls,
					      &fwd->tlsname);
			if (result != ISC_R_SUCCESS) {
				isc_mem_put(view->mctx, fwd,
					    sizeof(dns_forwarder_t));
				goto cleanup;
			}
		}
		fwd->addr = *cfg_obj_assockaddr(forwarder);
		if (isc_sockaddr_getport(&fwd->addr) == 0) {
			isc_sockaddr_setport(&fwd->addr,
					     cur_tls != NULL ? tls_port : port);
		}
		ISC_LINK_INIT(fwd, link);
		ISC_LIST_APPEND(fwdlist, fwd, link);
	}

	if (ISC_LIST_EMPTY(fwdlist)) {
		if (forwardtype != NULL) {
			cfg_obj_log(forwardtype, named_g_lctx, ISC_LOG_WARNING,
				    "no forwarders seen; disabling "
				    "forwarding");
		}
		fwdpolicy = dns_fwdpolicy_none;
	} else {
		if (forwardtype == NULL) {
			fwdpolicy = dns_fwdpolicy_first;
		} else {
			const char *forwardstr = cfg_obj_asstring(forwardtype);
			if (strcasecmp(forwardstr, "first") == 0) {
				fwdpolicy = dns_fwdpolicy_first;
			} else if (strcasecmp(forwardstr, "only") == 0) {
				fwdpolicy = dns_fwdpolicy_only;
			} else {
				UNREACHABLE();
			}
		}
	}

	result = dns_fwdtable_addfwd(view->fwdtable, origin, &fwdlist,
				     fwdpolicy);
	if (result != ISC_R_SUCCESS) {
		char namebuf[DNS_NAME_FORMATSIZE];
		dns_name_format(origin, namebuf, sizeof(namebuf));
		cfg_obj_log(forwarders, named_g_lctx, ISC_LOG_WARNING,
			    "could not set up forwarding for domain '%s': %s",
			    namebuf, isc_result_totext(result));
		goto cleanup;
	}

	if (fwdpolicy == dns_fwdpolicy_only) {
		dns_view_sfd_add(view, origin);
	}

	result = ISC_R_SUCCESS;

cleanup:

	while (!ISC_LIST_EMPTY(fwdlist)) {
		fwd = ISC_LIST_HEAD(fwdlist);
		ISC_LIST_UNLINK(fwdlist, fwd, link);
		if (fwd->tlsname != NULL) {
			dns_name_free(fwd->tlsname, view->mctx);
			isc_mem_put(view->mctx, fwd->tlsname,
				    sizeof(dns_name_t));
		}
		isc_mem_put(view->mctx, fwd, sizeof(dns_forwarder_t));
	}

	return result;
}

static isc_result_t
get_viewinfo(const cfg_obj_t *vconfig, const char **namep,
	     dns_rdataclass_t *classp) {
	isc_result_t result = ISC_R_SUCCESS;
	const char *viewname;
	dns_rdataclass_t viewclass;

	REQUIRE(namep != NULL && *namep == NULL);
	REQUIRE(classp != NULL);

	if (vconfig != NULL) {
		const cfg_obj_t *classobj = NULL;

		viewname = cfg_obj_asstring(cfg_tuple_get(vconfig, "name"));
		classobj = cfg_tuple_get(vconfig, "class");
		CHECK(named_config_getclass(classobj, dns_rdataclass_in,
					    &viewclass));
		if (dns_rdataclass_ismeta(viewclass)) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "view '%s': class must not be meta",
				      viewname);
			CHECK(ISC_R_FAILURE);
		}
	} else {
		viewname = "_default";
		viewclass = dns_rdataclass_in;
	}

	*namep = viewname;
	*classp = viewclass;

cleanup:
	return result;
}

/*
 * Find a view based on its configuration info and attach to it.
 *
 * If 'vconfig' is NULL, attach to the default view.
 */
static isc_result_t
find_view(const cfg_obj_t *vconfig, dns_viewlist_t *viewlist,
	  dns_view_t **viewp) {
	isc_result_t result;
	const char *viewname = NULL;
	dns_rdataclass_t viewclass;
	dns_view_t *view = NULL;

	result = get_viewinfo(vconfig, &viewname, &viewclass);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	result = dns_viewlist_find(viewlist, viewname, viewclass, &view);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	*viewp = view;
	return ISC_R_SUCCESS;
}

/*
 * Create a new view and add it to the list.
 *
 * If 'vconfig' is NULL, create the default view.
 *
 * The view created is attached to '*viewp'.
 */
static isc_result_t
create_view(const cfg_obj_t *vconfig, dns_viewlist_t *viewlist,
	    dns_view_t **viewp) {
	isc_result_t result;
	const char *viewname = NULL;
	dns_rdataclass_t viewclass;
	dns_view_t *view = NULL;

	result = get_viewinfo(vconfig, &viewname, &viewclass);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	result = dns_viewlist_find(viewlist, viewname, viewclass, &view);
	if (result == ISC_R_SUCCESS) {
		return ISC_R_EXISTS;
	}
	if (result != ISC_R_NOTFOUND) {
		return result;
	}
	INSIST(view == NULL);

	result = dns_view_create(named_g_mctx, named_g_loopmgr,
				 named_g_dispatchmgr, viewclass, viewname,
				 &view);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	isc_nonce_buf(view->secret, sizeof(view->secret));

	ISC_LIST_APPEND(*viewlist, view, link);
	dns_view_attach(view, viewp);
	return ISC_R_SUCCESS;
}

/*
 * Configure or reconfigure a zone.
 */
static isc_result_t
configure_zone(const cfg_obj_t *config, const cfg_obj_t *zconfig,
	       const cfg_obj_t *vconfig, dns_view_t *view,
	       dns_viewlist_t *viewlist, dns_kasplist_t *kasplist,
	       dns_keystorelist_t *keystores, cfg_aclconfctx_t *aclconf,
	       bool added, bool old_rpz_ok, bool is_catz_member, bool modify) {
	dns_view_t *pview = NULL; /* Production view */
	dns_zone_t *zone = NULL;  /* New or reused zone */
	dns_zone_t *raw = NULL;	  /* New or reused raw zone */
	dns_zone_t *dupzone = NULL;
	const cfg_obj_t *options = NULL;
	const cfg_obj_t *zoptions = NULL;
	const cfg_obj_t *typeobj = NULL;
	const cfg_obj_t *forwarders = NULL;
	const cfg_obj_t *forwardtype = NULL;
	const cfg_obj_t *ixfrfromdiffs = NULL;
	const cfg_obj_t *viewobj = NULL;
	isc_result_t result = ISC_R_SUCCESS;
	isc_result_t tresult;
	isc_buffer_t buffer;
	dns_fixedname_t fixorigin;
	dns_name_t *origin;
	const char *zname;
	dns_rdataclass_t zclass;
	const char *ztypestr;
	dns_rpz_num_t rpz_num;
	bool zone_is_catz = false;
	bool zone_maybe_inline = false;
	bool inline_signing = false;
	bool fullsign = false;

	options = NULL;
	(void)cfg_map_get(config, "options", &options);

	zoptions = cfg_tuple_get(zconfig, "options");

	/*
	 * Get the zone origin as a dns_name_t.
	 */
	zname = cfg_obj_asstring(cfg_tuple_get(zconfig, "name"));
	isc_buffer_constinit(&buffer, zname, strlen(zname));
	isc_buffer_add(&buffer, strlen(zname));
	dns_fixedname_init(&fixorigin);
	CHECK(dns_name_fromtext(dns_fixedname_name(&fixorigin), &buffer,
				dns_rootname, 0, NULL));
	origin = dns_fixedname_name(&fixorigin);

	CHECK(named_config_getclass(cfg_tuple_get(zconfig, "class"),
				    view->rdclass, &zclass));
	if (zclass != view->rdclass) {
		const char *vname = NULL;
		if (vconfig != NULL) {
			vname = cfg_obj_asstring(
				cfg_tuple_get(vconfig, "name"));
		} else {
			vname = "<default view>";
		}

		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "zone '%s': wrong class for view '%s'", zname,
			      vname);
		result = ISC_R_FAILURE;
		goto cleanup;
	}

	(void)cfg_map_get(zoptions, "in-view", &viewobj);
	if (viewobj != NULL) {
		const char *inview = cfg_obj_asstring(viewobj);
		dns_view_t *otherview = NULL;

		if (viewlist == NULL) {
			cfg_obj_log(zconfig, named_g_lctx, ISC_LOG_ERROR,
				    "'in-view' option is not permitted in "
				    "dynamically added zones");
			result = ISC_R_FAILURE;
			goto cleanup;
		}

		result = dns_viewlist_find(viewlist, inview, view->rdclass,
					   &otherview);
		if (result != ISC_R_SUCCESS) {
			cfg_obj_log(zconfig, named_g_lctx, ISC_LOG_ERROR,
				    "view '%s' is not yet defined.", inview);
			result = ISC_R_FAILURE;
			goto cleanup;
		}

		result = dns_view_findzone(otherview, origin, DNS_ZTFIND_EXACT,
					   &zone);
		dns_view_detach(&otherview);
		if (result != ISC_R_SUCCESS) {
			cfg_obj_log(zconfig, named_g_lctx, ISC_LOG_ERROR,
				    "zone '%s' not defined in view '%s'", zname,
				    inview);
			result = ISC_R_FAILURE;
			goto cleanup;
		}

		CHECK(dns_view_addzone(view, zone));
		dns_zone_detach(&zone);

		/*
		 * If the zone contains a 'forwarders' statement, configure
		 * selective forwarding.  Note: this is not inherited from the
		 * other view.
		 */
		forwarders = NULL;
		result = cfg_map_get(zoptions, "forwarders", &forwarders);
		if (result == ISC_R_SUCCESS) {
			forwardtype = NULL;
			(void)cfg_map_get(zoptions, "forward", &forwardtype);
			CHECK(configure_forward(config, view, origin,
						forwarders, forwardtype));
		}
		result = ISC_R_SUCCESS;
		goto cleanup;
	}

	(void)cfg_map_get(zoptions, "type", &typeobj);
	if (typeobj == NULL) {
		cfg_obj_log(zconfig, named_g_lctx, ISC_LOG_ERROR,
			    "zone '%s' 'type' not specified", zname);
		result = ISC_R_FAILURE;
		goto cleanup;
	}
	ztypestr = cfg_obj_asstring(typeobj);

	/*
	 * "hints zones" aren't zones.	If we've got one,
	 * configure it and return.
	 */
	if (strcasecmp(ztypestr, "hint") == 0) {
		const cfg_obj_t *fileobj = NULL;
		if (cfg_map_get(zoptions, "file", &fileobj) != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "zone '%s': 'file' not specified", zname);
			result = ISC_R_FAILURE;
			goto cleanup;
		}
		if (dns_name_equal(origin, dns_rootname)) {
			const char *hintsfile = cfg_obj_asstring(fileobj);

			CHECK(configure_hints(view, hintsfile));
		} else {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
				      "ignoring non-root hint zone '%s'",
				      zname);
			result = ISC_R_SUCCESS;
		}
		/* Skip ordinary zone processing. */
		goto cleanup;
	}

	/*
	 * "forward zones" aren't zones either.  Translate this syntax into
	 * the appropriate selective forwarding configuration and return.
	 */
	if (strcasecmp(ztypestr, "forward") == 0) {
		forwardtype = NULL;
		forwarders = NULL;

		(void)cfg_map_get(zoptions, "forward", &forwardtype);
		(void)cfg_map_get(zoptions, "forwarders", &forwarders);
		CHECK(configure_forward(config, view, origin, forwarders,
					forwardtype));
		goto cleanup;
	}

	/*
	 * Redirect zones only require minimal configuration.
	 */
	if (strcasecmp(ztypestr, "redirect") == 0) {
		if (view->redirect != NULL) {
			cfg_obj_log(zconfig, named_g_lctx, ISC_LOG_ERROR,
				    "redirect zone already exists");
			result = ISC_R_EXISTS;
			goto cleanup;
		}
		result = dns_viewlist_find(viewlist, view->name, view->rdclass,
					   &pview);
		if (result != ISC_R_NOTFOUND && result != ISC_R_SUCCESS) {
			goto cleanup;
		}
		if (pview != NULL && pview->redirect != NULL) {
			dns_zone_attach(pview->redirect, &zone);
			dns_zone_setview(zone, view);
		} else {
			CHECK(dns_zonemgr_createzone(named_g_server->zonemgr,
						     &zone));
			CHECK(dns_zone_setorigin(zone, origin));
			dns_zone_setview(zone, view);
			CHECK(dns_zonemgr_managezone(named_g_server->zonemgr,
						     zone));
			dns_zone_setstats(zone, named_g_server->zonestats);
		}
		CHECK(named_zone_configure(config, vconfig, zconfig, aclconf,
					   kasplist, keystores, zone, NULL));
		dns_zone_attach(zone, &view->redirect);
		goto cleanup;
	}

	if (!modify) {
		/*
		 * Check for duplicates in the new zone table.
		 */
		result = dns_view_findzone(view, origin, DNS_ZTFIND_EXACT,
					   &dupzone);
		if (result == ISC_R_SUCCESS) {
			/*
			 * We already have this zone!
			 */
			cfg_obj_log(zconfig, named_g_lctx, ISC_LOG_ERROR,
				    "zone '%s' already exists", zname);
			dns_zone_detach(&dupzone);
			result = ISC_R_EXISTS;
			goto cleanup;
		}
		INSIST(dupzone == NULL);
	}

	/*
	 * Note whether this is a response policy zone and which one if so,
	 * unless we are using RPZ service interface.  In that case, the
	 * BIND zone database has nothing to do with rpz and so we don't care.
	 */
	for (rpz_num = 0;; ++rpz_num) {
		if (view->rpzs == NULL || rpz_num >= view->rpzs->p.num_zones ||
		    view->rpzs->p.dnsrps_enabled)
		{
			rpz_num = DNS_RPZ_INVALID_NUM;
			break;
		}
		if (dns_name_equal(&view->rpzs->zones[rpz_num]->origin, origin))
		{
			break;
		}
	}

	if (!is_catz_member && view->catzs != NULL &&
	    dns_catz_zone_get(view->catzs, origin) != NULL)
	{
		zone_is_catz = true;
	}

	/*
	 * See if we can reuse an existing zone.  This is
	 * only possible if all of these are true:
	 *   - The zone's view exists
	 *   - A zone with the right name exists in the view
	 *   - The zone is compatible with the config
	 *     options (e.g., an existing primary zone cannot
	 *     be reused if the options specify a secondary zone)
	 *   - The zone was not and is still not a response policy zone
	 *     or the zone is a policy zone with an unchanged number
	 *     and we are using the old policy zone summary data.
	 */
	result = dns_viewlist_find(&named_g_server->viewlist, view->name,
				   view->rdclass, &pview);
	if (result != ISC_R_NOTFOUND && result != ISC_R_SUCCESS) {
		goto cleanup;
	}
	if (pview != NULL) {
		result = dns_view_findzone(pview, origin, DNS_ZTFIND_EXACT,
					   &zone);
	}
	if (result != ISC_R_NOTFOUND && result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	if (zone != NULL &&
	    !named_zone_reusable(zone, zconfig, vconfig, config, kasplist))
	{
		dns_zone_detach(&zone);
		fullsign = true;
	}

	if (zone != NULL && (rpz_num != dns_zone_get_rpz_num(zone) ||
			     (rpz_num != DNS_RPZ_INVALID_NUM && !old_rpz_ok)))
	{
		dns_zone_detach(&zone);
	}

	if (zone != NULL) {
		/*
		 * We found a reusable zone.  Make it use the
		 * new view.
		 */
		dns_zone_setview(zone, view);
	} else {
		/*
		 * We cannot reuse an existing zone, we have
		 * to create a new one.
		 */
		CHECK(dns_zonemgr_createzone(named_g_server->zonemgr, &zone));
		CHECK(dns_zone_setorigin(zone, origin));
		dns_zone_setview(zone, view);
		CHECK(dns_zonemgr_managezone(named_g_server->zonemgr, zone));
		dns_zone_setstats(zone, named_g_server->zonestats);
	}
	if (rpz_num != DNS_RPZ_INVALID_NUM) {
		result = dns_zone_rpz_enable(zone, view->rpzs, rpz_num);
		if (result != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "zone '%s': incompatible"
				      " masterfile-format or database"
				      " for a response policy zone",
				      zname);
			goto cleanup;
		}
	}

	if (zone_is_catz) {
		dns_zone_catz_enable(zone, view->catzs);
	} else if (dns_zone_catz_is_enabled(zone)) {
		dns_zone_catz_disable(zone);
	}

	/*
	 * If the zone contains a 'forwarders' statement, configure
	 * selective forwarding.
	 */
	forwarders = NULL;
	if (cfg_map_get(zoptions, "forwarders", &forwarders) == ISC_R_SUCCESS) {
		forwardtype = NULL;
		(void)cfg_map_get(zoptions, "forward", &forwardtype);
		CHECK(configure_forward(config, view, origin, forwarders,
					forwardtype));
	}

	/*
	 * Mark whether the zone was originally added at runtime or not
	 */
	dns_zone_setadded(zone, added);

	/*
	 * Determine if we need to set up inline signing.
	 */
	zone_maybe_inline = (strcasecmp(ztypestr, "primary") == 0 ||
			     strcasecmp(ztypestr, "master") == 0 ||
			     strcasecmp(ztypestr, "secondary") == 0 ||
			     strcasecmp(ztypestr, "slave") == 0);

	if (zone_maybe_inline) {
		inline_signing = named_zone_inlinesigning(zconfig, vconfig,
							  config, kasplist);
	}
	if (inline_signing) {
		dns_zone_getraw(zone, &raw);
		if (raw == NULL) {
			dns_zone_create(&raw, dns_zone_getmem(zone),
					dns_zone_gettid(zone));
			CHECK(dns_zone_setorigin(raw, origin));
			dns_zone_setview(raw, view);
			dns_zone_setstats(raw, named_g_server->zonestats);
			CHECK(dns_zone_link(zone, raw));
		}
		if (cfg_map_get(zoptions, "ixfr-from-differences",
				&ixfrfromdiffs) == ISC_R_SUCCESS)
		{
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
				      "zone '%s': 'ixfr-from-differences' is "
				      "ignored for inline-signed zones",
				      zname);
		}
	}

	/*
	 * Configure the zone.
	 */
	CHECK(named_zone_configure(config, vconfig, zconfig, aclconf, kasplist,
				   keystores, zone, raw));

	/*
	 * Add the zone to its view in the new view list.
	 */
	if (!modify) {
		CHECK(dns_view_addzone(view, zone));
	}

	if (zone_is_catz) {
		/*
		 * force catz reload if the zone is loaded;
		 * if it's not it'll get reloaded on zone load
		 */
		dns_db_t *db = NULL;

		tresult = dns_zone_getdb(zone, &db);
		if (tresult == ISC_R_SUCCESS) {
			dns_catz_dbupdate_callback(db, view->catzs);
			dns_db_detach(&db);
		}
	}

	/*
	 * Ensure that zone keys are reloaded on reconfig
	 */
	if ((dns_zone_getkeyopts(zone) & DNS_ZONEKEY_MAINTAIN) != 0) {
		dns_zone_rekey(zone, fullsign);
	}

cleanup:
	if (zone != NULL) {
		dns_zone_detach(&zone);
	}
	if (raw != NULL) {
		dns_zone_detach(&raw);
	}
	if (pview != NULL) {
		dns_view_detach(&pview);
	}

	return result;
}

/*
 * Configure built-in zone for storing managed-key data.
 */
static isc_result_t
add_keydata_zone(dns_view_t *view, const char *directory, isc_mem_t *mctx) {
	isc_result_t result;
	dns_view_t *pview = NULL;
	dns_zone_t *zone = NULL;
	dns_acl_t *none = NULL;
	char filename[PATH_MAX];
	bool defaultview;

	REQUIRE(view != NULL);

	/* See if we can re-use an existing keydata zone. */
	result = dns_viewlist_find(&named_g_server->viewlist, view->name,
				   view->rdclass, &pview);
	if (result != ISC_R_NOTFOUND && result != ISC_R_SUCCESS) {
		return result;
	}

	if (pview != NULL) {
		if (pview->managed_keys != NULL) {
			dns_zone_attach(pview->managed_keys,
					&view->managed_keys);
			dns_zone_setview(pview->managed_keys, view);
			dns_zone_setviewcommit(pview->managed_keys);
			dns_view_detach(&pview);
			dns_zone_synckeyzone(view->managed_keys);
			return ISC_R_SUCCESS;
		}

		dns_view_detach(&pview);
	}

	/* No existing keydata zone was found; create one */
	CHECK(dns_zonemgr_createzone(named_g_server->zonemgr, &zone));
	CHECK(dns_zone_setorigin(zone, dns_rootname));

	defaultview = (strcmp(view->name, "_default") == 0);
	CHECK(isc_file_sanitize(
		directory, defaultview ? "managed-keys" : view->name,
		defaultview ? "bind" : "mkeys", filename, sizeof(filename)));
	CHECK(dns_zone_setfile(zone, filename, dns_masterformat_text,
			       &dns_master_style_default));

	dns_zone_setview(zone, view);
	dns_zone_settype(zone, dns_zone_key);
	dns_zone_setclass(zone, view->rdclass);

	CHECK(dns_zonemgr_managezone(named_g_server->zonemgr, zone));

	CHECK(dns_acl_none(mctx, &none));
	dns_zone_setqueryacl(zone, none);
	dns_zone_setqueryonacl(zone, none);
	dns_acl_detach(&none);

	dns_zone_setdialup(zone, dns_dialuptype_no);
	dns_zone_setcheckdstype(zone, dns_checkdstype_no);
	dns_zone_setnotifytype(zone, dns_notifytype_no);
	dns_zone_setoption(zone, DNS_ZONEOPT_NOCHECKNS, true);
	dns_zone_setjournalsize(zone, 0);

	dns_zone_setstats(zone, named_g_server->zonestats);
	setquerystats(zone, mctx, dns_zonestat_none);

	if (view->managed_keys != NULL) {
		dns_zone_detach(&view->managed_keys);
	}
	dns_zone_attach(zone, &view->managed_keys);

	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		      "set up managed keys zone for view %s, file '%s'",
		      view->name, filename);

cleanup:
	if (zone != NULL) {
		dns_zone_detach(&zone);
	}
	if (none != NULL) {
		dns_acl_detach(&none);
	}

	return result;
}

/*
 * Configure a single server quota.
 */
static void
configure_server_quota(const cfg_obj_t **maps, const char *name,
		       isc_quota_t *quota) {
	const cfg_obj_t *obj = NULL;
	isc_result_t result;

	result = named_config_get(maps, name, &obj);
	INSIST(result == ISC_R_SUCCESS);
	isc_quota_max(quota, cfg_obj_asuint32(obj));
}

/*
 * This function is called as soon as the 'directory' statement has been
 * parsed.  This can be extended to support other options if necessary.
 */
static isc_result_t
directory_callback(const char *clausename, const cfg_obj_t *obj, void *arg) {
	isc_result_t result;
	const char *directory;

	REQUIRE(strcasecmp("directory", clausename) == 0);

	UNUSED(arg);
	UNUSED(clausename);

	/*
	 * Change directory.
	 */
	directory = cfg_obj_asstring(obj);

	if (!isc_file_ischdiridempotent(directory)) {
		cfg_obj_log(obj, named_g_lctx, ISC_LOG_WARNING,
			    "option 'directory' contains relative path '%s'",
			    directory);
	}

	if (!isc_file_isdirwritable(directory)) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "directory '%s' is not writable", directory);
		return ISC_R_NOPERM;
	}

	result = isc_dir_chdir(directory);
	if (result != ISC_R_SUCCESS) {
		cfg_obj_log(obj, named_g_lctx, ISC_LOG_ERROR,
			    "change directory to '%s' failed: %s", directory,
			    isc_result_totext(result));
		return result;
	}

	char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof(cwd)) == cwd) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
			      "the working directory is now '%s'", cwd);
	}

	return ISC_R_SUCCESS;
}

/*
 * This event callback is invoked to do periodic network interface
 * scanning.
 */

static void
interface_timer_tick(void *arg) {
	named_server_t *server = (named_server_t *)arg;

	(void)ns_interfacemgr_scan(server->interfacemgr, false, false);
}

static void
heartbeat_timer_tick(void *arg) {
	named_server_t *server = (named_server_t *)arg;
	dns_view_t *view = NULL;

	view = ISC_LIST_HEAD(server->viewlist);
	while (view != NULL) {
		dns_view_dialup(view);
		view = ISC_LIST_NEXT(view, link);
	}
}

typedef struct {
	isc_mem_t *mctx;
	isc_loop_t *loop;
	dns_fetch_t *fetch;
	dns_view_t *view;
	dns_fixedname_t tatname;
	dns_fixedname_t keyname;
	dns_rdataset_t rdataset;
	dns_rdataset_t sigrdataset;
} ns_tat_t;

static int
cid(const void *a, const void *b) {
	const uint16_t ida = *(const uint16_t *)a;
	const uint16_t idb = *(const uint16_t *)b;
	if (ida < idb) {
		return -1;
	} else if (ida > idb) {
		return 1;
	} else {
		return 0;
	}
}

static void
tat_done(void *arg) {
	dns_fetchresponse_t *resp = (dns_fetchresponse_t *)arg;
	ns_tat_t *tat = NULL;

	INSIST(resp != NULL);

	tat = resp->arg;

	INSIST(tat != NULL);

	/* Free resources which are not of interest */
	if (resp->node != NULL) {
		dns_db_detachnode(resp->db, &resp->node);
	}
	if (resp->db != NULL) {
		dns_db_detach(&resp->db);
	}

	dns_resolver_freefresp(&resp);
	dns_resolver_destroyfetch(&tat->fetch);
	if (dns_rdataset_isassociated(&tat->rdataset)) {
		dns_rdataset_disassociate(&tat->rdataset);
	}
	if (dns_rdataset_isassociated(&tat->sigrdataset)) {
		dns_rdataset_disassociate(&tat->sigrdataset);
	}
	dns_view_detach(&tat->view);
	isc_mem_putanddetach(&tat->mctx, tat, sizeof(*tat));
}

struct dotat_arg {
	dns_view_t *view;
	isc_loop_t *loop;
};

/*%
 * Prepare the QNAME for the TAT query to be sent by processing the trust
 * anchors present at 'keynode' of 'keytable'.  Store the result in 'dst' and
 * the domain name which 'keynode' is associated with in 'origin'.
 *
 * A maximum of 12 key IDs can be reported in a single TAT query due to the
 * 63-octet length limit for any single label in a domain name.  If there are
 * more than 12 keys configured at 'keynode', only the first 12 will be
 * reported in the TAT query.
 */
static isc_result_t
get_tat_qname(dns_name_t *target, dns_name_t *keyname, dns_keynode_t *keynode) {
	dns_rdataset_t dsset;
	unsigned int i, n = 0;
	uint16_t ids[12];
	isc_textregion_t r;
	char label[64];
	int m;

	dns_rdataset_init(&dsset);
	if (dns_keynode_dsset(keynode, &dsset)) {
		isc_result_t result;

		for (result = dns_rdataset_first(&dsset);
		     result == ISC_R_SUCCESS;
		     result = dns_rdataset_next(&dsset))
		{
			dns_rdata_t rdata = DNS_RDATA_INIT;
			dns_rdata_ds_t ds;

			dns_rdata_reset(&rdata);
			dns_rdataset_current(&dsset, &rdata);
			result = dns_rdata_tostruct(&rdata, &ds, NULL);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);
			if (n < (sizeof(ids) / sizeof(ids[0]))) {
				ids[n] = ds.key_tag;
				n++;
			}
		}
		dns_rdataset_disassociate(&dsset);
	}

	if (n == 0) {
		return DNS_R_EMPTYNAME;
	}

	if (n > 1) {
		qsort(ids, n, sizeof(ids[0]), cid);
	}

	/*
	 * Encoded as "_ta-xxxx\(-xxxx\)*" where xxxx is the hex version of
	 * of the keyid.
	 */
	label[0] = 0;
	r.base = label;
	r.length = sizeof(label);
	m = snprintf(r.base, r.length, "_ta");
	if (m < 0 || (unsigned int)m > r.length) {
		return ISC_R_FAILURE;
	}
	isc_textregion_consume(&r, m);
	for (i = 0; i < n; i++) {
		m = snprintf(r.base, r.length, "-%04x", ids[i]);
		if (m < 0 || (unsigned int)m > r.length) {
			return ISC_R_FAILURE;
		}
		isc_textregion_consume(&r, m);
	}

	return dns_name_fromstring(target, label, keyname, 0, NULL);
}

static void
tat_send(void *arg) {
	ns_tat_t *tat = (ns_tat_t *)arg;
	char namebuf[DNS_NAME_FORMATSIZE];
	dns_fixedname_t fdomain;
	dns_name_t *domain = NULL;
	dns_rdataset_t nameservers;
	isc_result_t result;
	dns_name_t *keyname = NULL;
	dns_name_t *tatname = NULL;

	keyname = dns_fixedname_name(&tat->keyname);
	tatname = dns_fixedname_name(&tat->tatname);

	dns_name_format(tatname, namebuf, sizeof(namebuf));
	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		      "%s: sending trust-anchor-telemetry query '%s/NULL'",
		      tat->view->name, namebuf);

	/*
	 * TAT queries should be sent to the authoritative servers for a given
	 * zone.  If this function is called for a keytable node corresponding
	 * to a locally served zone, calling dns_resolver_createfetch() with
	 * NULL 'domain' and 'nameservers' arguments will cause 'tatname' to be
	 * resolved locally, without sending any TAT queries upstream.
	 *
	 * Work around this issue by calling dns_view_findzonecut() first.  If
	 * the zone is served locally, the NS RRset for the given domain name
	 * will be retrieved from local data; if it is not, the deepest zone
	 * cut we have for it will be retrieved from cache.  In either case,
	 * passing the results to dns_resolver_createfetch() will prevent it
	 * from returning NXDOMAIN for 'tatname' while still allowing it to
	 * chase down any potential delegations returned by upstream servers in
	 * order to eventually find the destination host to send the TAT query
	 * to.
	 *
	 * After the dns_view_findzonecut() call, 'domain' will hold the
	 * deepest zone cut we can find for 'keyname' while 'nameservers' will
	 * hold the NS RRset at that zone cut.
	 */
	domain = dns_fixedname_initname(&fdomain);
	dns_rdataset_init(&nameservers);
	result = dns_view_findzonecut(tat->view, keyname, domain, NULL, 0, 0,
				      true, true, &nameservers, NULL);
	if (result == ISC_R_SUCCESS) {
		result = dns_resolver_createfetch(
			tat->view->resolver, tatname, dns_rdatatype_null,
			domain, &nameservers, NULL, NULL, 0, 0, 0, NULL, NULL,
			tat->loop, tat_done, tat, NULL, &tat->rdataset,
			&tat->sigrdataset, &tat->fetch);
	}

	/*
	 * 'domain' holds the dns_name_t pointer inside a dst_key_t structure.
	 * dns_resolver_createfetch() creates its own copy of 'domain' if it
	 * succeeds.  Thus, 'domain' is not freed here.
	 *
	 * Even if dns_view_findzonecut() returned something else than
	 * ISC_R_SUCCESS, it still could have associated 'nameservers'.
	 * dns_resolver_createfetch() creates its own copy of 'nameservers' if
	 * it succeeds.  Thus, we need to check whether 'nameservers' is
	 * associated and release it if it is.
	 */
	if (dns_rdataset_isassociated(&nameservers)) {
		dns_rdataset_disassociate(&nameservers);
	}

	if (result != ISC_R_SUCCESS) {
		dns_view_detach(&tat->view);
		isc_mem_putanddetach(&tat->mctx, tat, sizeof(*tat));
	}
}

static void
dotat(dns_keytable_t *keytable, dns_keynode_t *keynode, dns_name_t *keyname,
      void *arg) {
	struct dotat_arg *dotat_arg = (struct dotat_arg *)arg;
	isc_result_t result;
	dns_view_t *view = NULL;
	ns_tat_t *tat = NULL;

	REQUIRE(keytable != NULL);
	REQUIRE(keynode != NULL);
	REQUIRE(dotat_arg != NULL);

	view = dotat_arg->view;

	tat = isc_mem_get(view->mctx, sizeof(*tat));
	*tat = (ns_tat_t){ 0 };

	dns_rdataset_init(&tat->rdataset);
	dns_rdataset_init(&tat->sigrdataset);
	dns_name_copy(keyname, dns_fixedname_initname(&tat->keyname));
	result = get_tat_qname(dns_fixedname_initname(&tat->tatname), keyname,
			       keynode);
	if (result != ISC_R_SUCCESS) {
		isc_mem_put(view->mctx, tat, sizeof(*tat));
		return;
	}
	isc_mem_attach(view->mctx, &tat->mctx);
	tat->loop = dotat_arg->loop;
	dns_view_attach(view, &tat->view);

	/*
	 * We don't want to be holding the keytable lock when calling
	 * dns_view_findzonecut() as it creates a lock order loop so
	 * call dns_view_findzonecut() in a event handler.
	 *
	 * zone->lock (dns_zone_setviewcommit) while holding view->lock
	 * (dns_view_setviewcommit)
	 *
	 * keytable->lock (dns_keytable_find) while holding zone->lock
	 * (zone_asyncload)
	 *
	 * view->lock (dns_view_findzonecut) while holding keytable->lock
	 * (dns_keytable_forall)
	 */
	isc_async_run(named_g_mainloop, tat_send, tat);
}

static void
tat_timer_tick(void *arg) {
	isc_result_t result;
	named_server_t *server = (named_server_t *)arg;
	struct dotat_arg dotat_arg = { 0 };
	dns_view_t *view = NULL;
	dns_keytable_t *secroots = NULL;

	for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
	     view = ISC_LIST_NEXT(view, link))
	{
		if (!view->trust_anchor_telemetry || !view->enablevalidation) {
			continue;
		}

		result = dns_view_getsecroots(view, &secroots);
		if (result != ISC_R_SUCCESS) {
			continue;
		}

		dotat_arg.view = view;
		dotat_arg.loop = named_g_mainloop;
		dns_keytable_forall(secroots, dotat, &dotat_arg);
		dns_keytable_detach(&secroots);
	}
}

static void
pps_timer_tick(void *arg) {
	static unsigned int oldrequests = 0;
	unsigned int requests = atomic_load_relaxed(&ns_client_requests);

	UNUSED(arg);

	/*
	 * Don't worry about wrapping as the overflow result will be right.
	 */
	dns_pps = (requests - oldrequests) / 1200;
	oldrequests = requests;
}

/*
 * Replace the current value of '*field', a dynamically allocated
 * string or NULL, with a dynamically allocated copy of the
 * null-terminated string pointed to by 'value', or NULL.
 */
static void
setstring(named_server_t *server, char **field, const char *value) {
	char *copy;

	if (value != NULL) {
		copy = isc_mem_strdup(server->mctx, value);
	} else {
		copy = NULL;
	}

	if (*field != NULL) {
		isc_mem_free(server->mctx, *field);
	}

	*field = copy;
}

/*
 * Replace the current value of '*field', a dynamically allocated
 * string or NULL, with another dynamically allocated string
 * or NULL if whether 'obj' is a string or void value, respectively.
 */
static void
setoptstring(named_server_t *server, char **field, const cfg_obj_t *obj) {
	if (cfg_obj_isvoid(obj)) {
		setstring(server, field, NULL);
	} else {
		setstring(server, field, cfg_obj_asstring(obj));
	}
}

static void
portset_fromconf(isc_portset_t *portset, const cfg_obj_t *ports,
		 bool positive) {
	const cfg_listelt_t *element;

	for (element = cfg_list_first(ports); element != NULL;
	     element = cfg_list_next(element))
	{
		const cfg_obj_t *obj = cfg_listelt_value(element);

		if (cfg_obj_isuint32(obj)) {
			in_port_t port = (in_port_t)cfg_obj_asuint32(obj);

			if (positive) {
				isc_portset_add(portset, port);
			} else {
				isc_portset_remove(portset, port);
			}
		} else {
			const cfg_obj_t *obj_loport, *obj_hiport;
			in_port_t loport, hiport;

			obj_loport = cfg_tuple_get(obj, "loport");
			loport = (in_port_t)cfg_obj_asuint32(obj_loport);
			obj_hiport = cfg_tuple_get(obj, "hiport");
			hiport = (in_port_t)cfg_obj_asuint32(obj_hiport);

			if (positive) {
				isc_portset_addrange(portset, loport, hiport);
			} else {
				isc_portset_removerange(portset, loport,
							hiport);
			}
		}
	}
}

static isc_result_t
removed(dns_zone_t *zone, void *uap) {
	if (dns_zone_getview(zone) != uap) {
		return ISC_R_SUCCESS;
	}

	dns_zone_log(zone, ISC_LOG_INFO, "(%s) removed",
		     dns_zonetype_name(dns_zone_gettype(zone)));
	return ISC_R_SUCCESS;
}

static void
cleanup_session_key(named_server_t *server, isc_mem_t *mctx) {
	if (server->session_keyfile != NULL) {
		isc_file_remove(server->session_keyfile);
		isc_mem_free(mctx, server->session_keyfile);
		server->session_keyfile = NULL;
	}

	if (server->session_keyname != NULL) {
		if (dns_name_dynamic(server->session_keyname)) {
			dns_name_free(server->session_keyname, mctx);
		}
		isc_mem_put(mctx, server->session_keyname, sizeof(dns_name_t));
		server->session_keyname = NULL;
	}

	if (server->sessionkey != NULL) {
		dst_key_free(&server->sessionkey);
	}

	server->session_keyalg = DST_ALG_UNKNOWN;
	server->session_keybits = 0;
}

static isc_result_t
generate_session_key(const char *filename, const char *keynamestr,
		     const dns_name_t *keyname, dst_algorithm_t alg,
		     uint16_t bits, isc_mem_t *mctx, bool first_time,
		     dst_key_t **keyp) {
	isc_result_t result = ISC_R_SUCCESS;
	dst_key_t *key = NULL;
	isc_buffer_t key_txtbuffer;
	isc_buffer_t key_rawbuffer;
	char key_txtsecret[256];
	char key_rawsecret[64];
	isc_region_t key_rawregion;
	FILE *fp = NULL;

	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		      "generating session key for dynamic DNS");

	/* generate key */
	result = dst_key_generate(keyname, alg, bits, 1, 0, DNS_KEYPROTO_ANY,
				  dns_rdataclass_in, NULL, mctx, &key, NULL);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	/*
	 * Dump the key to the buffer for later use.
	 */
	isc_buffer_init(&key_rawbuffer, &key_rawsecret, sizeof(key_rawsecret));
	CHECK(dst_key_tobuffer(key, &key_rawbuffer));

	isc_buffer_usedregion(&key_rawbuffer, &key_rawregion);
	isc_buffer_init(&key_txtbuffer, &key_txtsecret, sizeof(key_txtsecret));
	CHECK(isc_base64_totext(&key_rawregion, -1, "", &key_txtbuffer));

	/* Dump the key to the key file. */
	fp = named_os_openfile(filename, S_IRUSR | S_IWUSR, first_time);
	if (fp == NULL) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "could not create %s", filename);
		result = ISC_R_NOPERM;
		goto cleanup;
	}

	fprintf(fp,
		"key \"%s\" {\n"
		"\talgorithm %s;\n"
		"\tsecret \"%.*s\";\n};\n",
		keynamestr, dst_hmac_algorithm_totext(alg),
		(int)isc_buffer_usedlength(&key_txtbuffer),
		(char *)isc_buffer_base(&key_txtbuffer));

	CHECK(isc_stdio_flush(fp));
	result = isc_stdio_close(fp);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	*keyp = key;
	return ISC_R_SUCCESS;

cleanup:
	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
		      "failed to generate session key "
		      "for dynamic DNS: %s",
		      isc_result_totext(result));
	if (fp != NULL) {
		(void)isc_stdio_close(fp);
		(void)isc_file_remove(filename);
	}
	if (key != NULL) {
		dst_key_free(&key);
	}

	return result;
}

static isc_result_t
configure_session_key(const cfg_obj_t **maps, named_server_t *server,
		      isc_mem_t *mctx, bool first_time) {
	const char *keyfile = NULL, *keynamestr = NULL, *algstr = NULL;
	unsigned int algtype;
	dns_fixedname_t fname;
	dns_name_t *keyname = NULL;
	isc_buffer_t buffer;
	uint16_t bits;
	const cfg_obj_t *obj = NULL;
	bool need_deleteold = false;
	bool need_createnew = false;
	isc_result_t result;

	obj = NULL;
	result = named_config_get(maps, "session-keyfile", &obj);
	if (result == ISC_R_SUCCESS) {
		if (cfg_obj_isvoid(obj)) {
			keyfile = NULL; /* disable it */
		} else {
			keyfile = cfg_obj_asstring(obj);
		}
	} else {
		keyfile = named_g_defaultsessionkeyfile;
	}

	obj = NULL;
	result = named_config_get(maps, "session-keyname", &obj);
	INSIST(result == ISC_R_SUCCESS);
	keynamestr = cfg_obj_asstring(obj);
	isc_buffer_constinit(&buffer, keynamestr, strlen(keynamestr));
	isc_buffer_add(&buffer, strlen(keynamestr));
	keyname = dns_fixedname_initname(&fname);
	result = dns_name_fromtext(keyname, &buffer, dns_rootname, 0, NULL);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	obj = NULL;
	result = named_config_get(maps, "session-keyalg", &obj);
	INSIST(result == ISC_R_SUCCESS);
	algstr = cfg_obj_asstring(obj);
	result = named_config_getkeyalgorithm(algstr, &algtype, &bits);
	if (result != ISC_R_SUCCESS) {
		const char *s = " (keeping current key)";

		cfg_obj_log(obj, named_g_lctx, ISC_LOG_ERROR,
			    "session-keyalg: "
			    "unsupported or unknown algorithm '%s'%s",
			    algstr, server->session_keyfile != NULL ? s : "");
		return result;
	}

	/* See if we need to (re)generate a new key. */
	if (keyfile == NULL) {
		if (server->session_keyfile != NULL) {
			need_deleteold = true;
		}
	} else if (server->session_keyfile == NULL) {
		need_createnew = true;
	} else if (strcmp(keyfile, server->session_keyfile) != 0 ||
		   !dns_name_equal(server->session_keyname, keyname) ||
		   server->session_keyalg != algtype ||
		   server->session_keybits != bits)
	{
		need_deleteold = true;
		need_createnew = true;
	}

	if (need_deleteold) {
		INSIST(server->session_keyfile != NULL);
		INSIST(server->session_keyname != NULL);
		INSIST(server->sessionkey != NULL);

		cleanup_session_key(server, mctx);
	}

	if (need_createnew) {
		INSIST(server->sessionkey == NULL);
		INSIST(server->session_keyfile == NULL);
		INSIST(server->session_keyname == NULL);
		INSIST(server->session_keyalg == DST_ALG_UNKNOWN);
		INSIST(server->session_keybits == 0);

		server->session_keyname = isc_mem_get(mctx, sizeof(dns_name_t));
		dns_name_init(server->session_keyname, NULL);
		dns_name_dup(keyname, mctx, server->session_keyname);

		server->session_keyfile = isc_mem_strdup(mctx, keyfile);

		server->session_keyalg = algtype;
		server->session_keybits = bits;

		CHECK(generate_session_key(keyfile, keynamestr, keyname,
					   algtype, bits, mctx, first_time,
					   &server->sessionkey));
	}

	return result;

cleanup:
	cleanup_session_key(server, mctx);
	return result;
}

static isc_result_t
setup_newzones(dns_view_t *view, cfg_obj_t *config, cfg_obj_t *vconfig,
	       cfg_parser_t *conf_parser, cfg_aclconfctx_t *actx) {
	isc_result_t result = ISC_R_SUCCESS;
	bool allow = false;
	ns_cfgctx_t *nzcfg = NULL;
	const cfg_obj_t *maps[4];
	const cfg_obj_t *options = NULL, *voptions = NULL;
	const cfg_obj_t *nz = NULL;
	const cfg_obj_t *nzdir = NULL;
	const char *dir = NULL;
	const cfg_obj_t *obj = NULL;
	int i = 0;
	uint64_t mapsize = 0ULL;

	REQUIRE(config != NULL);

	if (vconfig != NULL) {
		voptions = cfg_tuple_get(vconfig, "options");
	}
	if (voptions != NULL) {
		maps[i++] = voptions;
	}
	result = cfg_map_get(config, "options", &options);
	if (result == ISC_R_SUCCESS) {
		maps[i++] = options;
	}
	maps[i++] = named_g_defaults;
	maps[i] = NULL;

	result = named_config_get(maps, "allow-new-zones", &nz);
	if (result == ISC_R_SUCCESS) {
		allow = cfg_obj_asboolean(nz);
	}
	result = named_config_get(maps, "new-zones-directory", &nzdir);
	if (result == ISC_R_SUCCESS) {
		dir = cfg_obj_asstring(nzdir);
		result = isc_file_isdirectory(dir);
		if (result != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, DNS_LOGCATEGORY_SECURITY,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "invalid new-zones-directory %s: %s", dir,
				      isc_result_totext(result));
			return result;
		}
		if (!isc_file_isdirwritable(dir)) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "new-zones-directory '%s' "
				      "is not writable",
				      dir);
			return ISC_R_NOPERM;
		}

		dns_view_setnewzonedir(view, dir);
	}

#ifdef HAVE_LMDB
	result = named_config_get(maps, "lmdb-mapsize", &obj);
	if (result == ISC_R_SUCCESS && obj != NULL) {
		mapsize = cfg_obj_asuint64(obj);
		if (mapsize < (1ULL << 20)) { /* 1 megabyte */
			cfg_obj_log(obj, named_g_lctx, ISC_LOG_ERROR,
				    "'lmdb-mapsize "
				    "%" PRId64 "' "
				    "is too small",
				    mapsize);
			return ISC_R_FAILURE;
		} else if (mapsize > (1ULL << 40)) { /* 1 terabyte */
			cfg_obj_log(obj, named_g_lctx, ISC_LOG_ERROR,
				    "'lmdb-mapsize "
				    "%" PRId64 "' "
				    "is too large",
				    mapsize);
			return ISC_R_FAILURE;
		}
	}
#else  /* ifdef HAVE_LMDB */
	UNUSED(obj);
#endif /* HAVE_LMDB */

	/*
	 * A non-empty catalog-zones statement implies allow-new-zones
	 */
	if (!allow) {
		const cfg_obj_t *cz = NULL;
		result = named_config_get(maps, "catalog-zones", &cz);
		if (result == ISC_R_SUCCESS) {
			const cfg_listelt_t *e =
				cfg_list_first(cfg_tuple_get(cz, "zone list"));
			if (e != NULL) {
				allow = true;
			}
		}
	}

	if (!allow) {
		dns_view_setnewzones(view, false, NULL, NULL, 0ULL);
		return ISC_R_SUCCESS;
	}

	nzcfg = isc_mem_get(view->mctx, sizeof(*nzcfg));
	*nzcfg = (ns_cfgctx_t){ 0 };

	/*
	 * We attach the parser that was used for config as well
	 * as the one that will be used for added zones, to avoid
	 * a shutdown race later.
	 */
	isc_mem_attach(view->mctx, &nzcfg->mctx);
	cfg_parser_attach(conf_parser, &nzcfg->conf_parser);
	cfg_parser_attach(named_g_addparser, &nzcfg->add_parser);
	cfg_aclconfctx_attach(actx, &nzcfg->actx);

	result = dns_view_setnewzones(view, true, nzcfg, newzone_cfgctx_destroy,
				      mapsize);
	if (result != ISC_R_SUCCESS) {
		cfg_aclconfctx_detach(&nzcfg->actx);
		cfg_parser_destroy(&nzcfg->add_parser);
		cfg_parser_destroy(&nzcfg->conf_parser);
		isc_mem_putanddetach(&nzcfg->mctx, nzcfg, sizeof(*nzcfg));
		dns_view_setnewzones(view, false, NULL, NULL, 0ULL);
		return result;
	}

	cfg_obj_attach(config, &nzcfg->config);
	if (vconfig != NULL) {
		cfg_obj_attach(vconfig, &nzcfg->vconfig);
	}

	result = load_nzf(view, nzcfg);
	return result;
}

static void
configure_zone_setviewcommit(isc_result_t result, const cfg_obj_t *zconfig,
			     dns_view_t *view) {
	const char *zname;
	dns_fixedname_t fixorigin;
	dns_name_t *origin;
	isc_result_t result2;
	dns_view_t *pview = NULL;
	dns_zone_t *zone = NULL;

	zname = cfg_obj_asstring(cfg_tuple_get(zconfig, "name"));
	origin = dns_fixedname_initname(&fixorigin);

	result2 = dns_name_fromstring(origin, zname, dns_rootname, 0, NULL);
	if (result2 != ISC_R_SUCCESS) {
		return;
	}

	result2 = dns_viewlist_find(&named_g_server->viewlist, view->name,
				    view->rdclass, &pview);
	if (result2 != ISC_R_SUCCESS) {
		return;
	}

	result2 = dns_view_findzone(pview, origin, DNS_ZTFIND_EXACT, &zone);
	if (result2 != ISC_R_SUCCESS) {
		dns_view_detach(&pview);
		return;
	}

	if (result == ISC_R_SUCCESS) {
		dns_zone_setviewcommit(zone);
	} else {
		dns_zone_setviewrevert(zone);
	}

	dns_zone_detach(&zone);
	dns_view_detach(&pview);
}

#ifndef HAVE_LMDB

static isc_result_t
configure_newzones(dns_view_t *view, cfg_obj_t *config, cfg_obj_t *vconfig,
		   cfg_aclconfctx_t *actx) {
	isc_result_t result;
	ns_cfgctx_t *nzctx;
	const cfg_obj_t *zonelist;
	const cfg_listelt_t *element;

	nzctx = view->new_zone_config;
	if (nzctx == NULL || nzctx->nzf_config == NULL) {
		return ISC_R_SUCCESS;
	}

	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		      "loading additional zones for view '%s'", view->name);

	zonelist = NULL;
	cfg_map_get(nzctx->nzf_config, "zone", &zonelist);

	for (element = cfg_list_first(zonelist); element != NULL;
	     element = cfg_list_next(element))
	{
		const cfg_obj_t *zconfig = cfg_listelt_value(element);
		CHECK(configure_zone(config, zconfig, vconfig, view,
				     &named_g_server->viewlist,
				     &named_g_server->kasplist,
				     &named_g_server->keystorelist, actx, true,
				     false, false, false));
	}

	result = ISC_R_SUCCESS;

cleanup:
	for (element = cfg_list_first(zonelist); element != NULL;
	     element = cfg_list_next(element))
	{
		const cfg_obj_t *zconfig = cfg_listelt_value(element);
		configure_zone_setviewcommit(result, zconfig, view);
	}

	return result;
}

#else /* HAVE_LMDB */

static isc_result_t
data_to_cfg(dns_view_t *view, MDB_val *key, MDB_val *data, isc_buffer_t **text,
	    cfg_obj_t **zoneconfig) {
	isc_result_t result;
	const char *zone_name;
	size_t zone_name_len;
	const char *zone_config;
	size_t zone_config_len;
	cfg_obj_t *zoneconf = NULL;
	char bufname[DNS_NAME_FORMATSIZE];

	REQUIRE(view != NULL);
	REQUIRE(key != NULL);
	REQUIRE(data != NULL);
	REQUIRE(text != NULL);
	REQUIRE(zoneconfig != NULL && *zoneconfig == NULL);

	if (*text == NULL) {
		isc_buffer_allocate(view->mctx, text, 256);
	} else {
		isc_buffer_clear(*text);
	}

	zone_name = (const char *)key->mv_data;
	zone_name_len = key->mv_size;
	INSIST(zone_name != NULL && zone_name_len > 0);

	zone_config = (const char *)data->mv_data;
	zone_config_len = data->mv_size;
	INSIST(zone_config != NULL && zone_config_len > 0);

	/* zone zonename { config; }; */
	result = isc_buffer_reserve(*text, 6 + zone_name_len + 2 +
						   zone_config_len + 2);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	CHECK(putstr(text, "zone \""));
	CHECK(putmem(text, (const void *)zone_name, zone_name_len));
	CHECK(putstr(text, "\" "));
	CHECK(putmem(text, (const void *)zone_config, zone_config_len));
	CHECK(putstr(text, ";\n"));

	snprintf(bufname, sizeof(bufname), "%.*s", (int)zone_name_len,
		 zone_name);

	cfg_parser_reset(named_g_addparser);
	result = cfg_parse_buffer(named_g_addparser, *text, bufname, 0,
				  &cfg_type_addzoneconf, 0, &zoneconf);
	if (result != ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "parsing config for zone '%.*s' in "
			      "NZD database '%s' failed",
			      (int)zone_name_len, zone_name, view->new_zone_db);
		goto cleanup;
	}

	*zoneconfig = zoneconf;
	zoneconf = NULL;
	result = ISC_R_SUCCESS;

cleanup:
	if (zoneconf != NULL) {
		cfg_obj_destroy(named_g_addparser, &zoneconf);
	}

	return result;
}

/*%
 * Prototype for a callback which can be used with for_all_newzone_cfgs().
 */
typedef isc_result_t (*newzone_cfg_cb_t)(const cfg_obj_t *zconfig,
					 cfg_obj_t *config, cfg_obj_t *vconfig,
					 dns_view_t *view,
					 cfg_aclconfctx_t *actx);

/*%
 * For each zone found in a NZD opened by the caller, create an object
 * representing its configuration and invoke "callback" with the created
 * object, "config", "vconfig", "mctx", "view" and "actx" as arguments (all
 * these are non-global variables required to invoke configure_zone()).
 * Immediately interrupt processing if an error is encountered while
 * transforming NZD data into a zone configuration object or if "callback"
 * returns an error.
 *
 * Caller must hold 'view->new_zone_lock'.
 */
static isc_result_t
for_all_newzone_cfgs(newzone_cfg_cb_t callback, cfg_obj_t *config,
		     cfg_obj_t *vconfig, dns_view_t *view,
		     cfg_aclconfctx_t *actx, MDB_txn *txn, MDB_dbi dbi) {
	const cfg_obj_t *zconfig, *zlist;
	isc_result_t result = ISC_R_SUCCESS;
	cfg_obj_t *zconfigobj = NULL;
	isc_buffer_t *text = NULL;
	MDB_cursor *cursor = NULL;
	MDB_val data, key;
	int status;

	status = mdb_cursor_open(txn, dbi, &cursor);
	if (status != MDB_SUCCESS) {
		return ISC_R_FAILURE;
	}

	for (status = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
	     status == MDB_SUCCESS;
	     status = mdb_cursor_get(cursor, &key, &data, MDB_NEXT))
	{
		/*
		 * Create a configuration object from data fetched from NZD.
		 */
		result = data_to_cfg(view, &key, &data, &text, &zconfigobj);
		if (result != ISC_R_SUCCESS) {
			break;
		}

		/*
		 * Extract zone configuration from configuration object.
		 */
		zlist = NULL;
		result = cfg_map_get(zconfigobj, "zone", &zlist);
		if (result != ISC_R_SUCCESS) {
			break;
		} else if (!cfg_obj_islist(zlist)) {
			result = ISC_R_FAILURE;
			break;
		}
		zconfig = cfg_listelt_value(cfg_list_first(zlist));

		/*
		 * Invoke callback.
		 */
		result = callback(zconfig, config, vconfig, view, actx);
		if (result != ISC_R_SUCCESS) {
			break;
		}

		/*
		 * Destroy the configuration object created in this iteration.
		 */
		cfg_obj_destroy(named_g_addparser, &zconfigobj);
	}

	if (text != NULL) {
		isc_buffer_free(&text);
	}
	if (zconfigobj != NULL) {
		cfg_obj_destroy(named_g_addparser, &zconfigobj);
	}
	mdb_cursor_close(cursor);

	return result;
}

/*%
 * Attempt to configure a zone found in NZD and return the result.
 */
static isc_result_t
configure_newzone(const cfg_obj_t *zconfig, cfg_obj_t *config,
		  cfg_obj_t *vconfig, dns_view_t *view,
		  cfg_aclconfctx_t *actx) {
	return configure_zone(
		config, zconfig, vconfig, view, &named_g_server->viewlist,
		&named_g_server->kasplist, &named_g_server->keystorelist, actx,
		true, false, false, false);
}

/*%
 * Revert new view assignment for a zone found in NZD.
 */
static isc_result_t
configure_newzone_revert(const cfg_obj_t *zconfig, cfg_obj_t *config,
			 cfg_obj_t *vconfig, dns_view_t *view,
			 cfg_aclconfctx_t *actx) {
	UNUSED(config);
	UNUSED(vconfig);
	UNUSED(actx);

	configure_zone_setviewcommit(ISC_R_FAILURE, zconfig, view);

	return ISC_R_SUCCESS;
}

static isc_result_t
configure_newzones(dns_view_t *view, cfg_obj_t *config, cfg_obj_t *vconfig,
		   cfg_aclconfctx_t *actx) {
	isc_result_t result;
	MDB_txn *txn = NULL;
	MDB_dbi dbi;

	if (view->new_zone_config == NULL) {
		return ISC_R_SUCCESS;
	}

	LOCK(&view->new_zone_lock);

	result = nzd_open(view, MDB_RDONLY, &txn, &dbi);
	if (result != ISC_R_SUCCESS) {
		UNLOCK(&view->new_zone_lock);
		return ISC_R_SUCCESS;
	}

	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		      "loading NZD configs from '%s' "
		      "for view '%s'",
		      view->new_zone_db, view->name);

	result = for_all_newzone_cfgs(configure_newzone, config, vconfig, view,
				      actx, txn, dbi);
	if (result != ISC_R_SUCCESS) {
		/*
		 * An error was encountered while attempting to configure zones
		 * found in NZD.  As this error may have been caused by a
		 * configure_zone() failure, try restoring a sane configuration
		 * by reattaching all zones found in NZD to the old view.  If
		 * this also fails, too bad, there is nothing more we can do in
		 * terms of trying to make things right.
		 */
		(void)for_all_newzone_cfgs(configure_newzone_revert, config,
					   vconfig, view, actx, txn, dbi);
	}

	(void)nzd_close(&txn, false);

	UNLOCK(&view->new_zone_lock);

	return result;
}

static isc_result_t
get_newzone_config(dns_view_t *view, const char *zonename,
		   cfg_obj_t **zoneconfig) {
	isc_result_t result;
	int status;
	cfg_obj_t *zoneconf = NULL;
	isc_buffer_t *text = NULL;
	MDB_txn *txn = NULL;
	MDB_dbi dbi;
	MDB_val key, data;
	char zname[DNS_NAME_FORMATSIZE];
	dns_fixedname_t fname;
	dns_name_t *name;
	isc_buffer_t b;

	INSIST(zoneconfig != NULL && *zoneconfig == NULL);

	LOCK(&view->new_zone_lock);

	CHECK(nzd_open(view, MDB_RDONLY, &txn, &dbi));

	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		      "loading NZD config from '%s' "
		      "for zone '%s'",
		      view->new_zone_db, zonename);

	/* Normalize zone name */
	isc_buffer_constinit(&b, zonename, strlen(zonename));
	isc_buffer_add(&b, strlen(zonename));
	name = dns_fixedname_initname(&fname);
	CHECK(dns_name_fromtext(name, &b, dns_rootname, DNS_NAME_DOWNCASE,
				NULL));
	dns_name_format(name, zname, sizeof(zname));

	key.mv_data = zname;
	key.mv_size = strlen(zname);

	status = mdb_get(txn, dbi, &key, &data);
	if (status != MDB_SUCCESS) {
		CHECK(ISC_R_FAILURE);
	}

	CHECK(data_to_cfg(view, &key, &data, &text, &zoneconf));

	*zoneconfig = zoneconf;
	zoneconf = NULL;
	result = ISC_R_SUCCESS;

cleanup:
	(void)nzd_close(&txn, false);

	UNLOCK(&view->new_zone_lock);

	if (zoneconf != NULL) {
		cfg_obj_destroy(named_g_addparser, &zoneconf);
	}
	if (text != NULL) {
		isc_buffer_free(&text);
	}

	return result;
}

#endif /* HAVE_LMDB */

static isc_result_t
load_configuration(const char *filename, named_server_t *server,
		   bool first_time) {
	cfg_obj_t *config = NULL, *bindkeys = NULL;
	cfg_parser_t *conf_parser = NULL, *bindkeys_parser = NULL;
	const cfg_listelt_t *element;
	const cfg_obj_t *builtin_views;
	const cfg_obj_t *maps[3];
	const cfg_obj_t *obj;
	const cfg_obj_t *options;
	const cfg_obj_t *usev4ports, *avoidv4ports, *usev6ports, *avoidv6ports;
	const cfg_obj_t *kasps;
	const cfg_obj_t *keystores;
	dns_kasp_t *kasp = NULL;
	dns_kasp_t *kasp_next = NULL;
	dns_kasp_t *default_kasp = NULL;
	dns_kasplist_t tmpkasplist, kasplist;
	dns_keystore_t *keystore = NULL;
	dns_keystore_t *keystore_next = NULL;
	dns_keystorelist_t tmpkeystorelist, keystorelist;
	const cfg_obj_t *views = NULL;
	dns_view_t *view_next = NULL;
	dns_viewlist_t tmpviewlist;
	dns_viewlist_t viewlist, builtin_viewlist;
	in_port_t listen_port, udpport_low, udpport_high;
	int i, backlog;
	isc_interval_t interval;
	isc_logconfig_t *logc = NULL;
	isc_portset_t *v4portset = NULL;
	isc_portset_t *v6portset = NULL;
	isc_result_t result;
	uint32_t heartbeat_interval;
	uint32_t interface_interval;
	uint32_t udpsize;
	uint32_t transfer_message_size;
	uint32_t recv_tcp_buffer_size;
	uint32_t send_tcp_buffer_size;
	uint32_t recv_udp_buffer_size;
	uint32_t send_udp_buffer_size;
	named_cache_t *nsc = NULL;
	named_cachelist_t cachelist, tmpcachelist;
	ns_altsecret_t *altsecret;
	ns_altsecretlist_t altsecrets, tmpaltsecrets;
	uint32_t softquota = 0;
	uint32_t max;
	uint64_t initial, idle, keepalive, advertised;
	bool loadbalancesockets;
	bool exclusive = true;
	dns_aclenv_t *env =
		ns_interfacemgr_getaclenv(named_g_server->interfacemgr);

	/*
	 * Require the reconfiguration to happen always on the main loop
	 */
	REQUIRE(isc_loop() == named_g_mainloop);

	ISC_LIST_INIT(kasplist);
	ISC_LIST_INIT(keystorelist);
	ISC_LIST_INIT(viewlist);
	ISC_LIST_INIT(builtin_viewlist);
	ISC_LIST_INIT(cachelist);
	ISC_LIST_INIT(altsecrets);

	/* Ensure exclusive access to configuration data. */
	isc_loopmgr_pause(named_g_loopmgr);

	/* Create the ACL configuration context */
	if (named_g_aclconfctx != NULL) {
		cfg_aclconfctx_detach(&named_g_aclconfctx);
	}
	result = cfg_aclconfctx_create(named_g_mctx, &named_g_aclconfctx);
	if (result != ISC_R_SUCCESS) {
		goto cleanup_exclusive;
	}

	/*
	 * Shut down all dyndb instances.
	 */
	dns_dyndb_cleanup(false);

	/*
	 * Parse the global default pseudo-config file.
	 */
	if (first_time) {
		result = named_config_parsedefaults(named_g_parser,
						    &named_g_config);
		if (result != ISC_R_SUCCESS) {
			named_main_earlyfatal("unable to load "
					      "internal defaults: %s",
					      isc_result_totext(result));
		}
		RUNTIME_CHECK(cfg_map_get(named_g_config, "options",
					  &named_g_defaults) == ISC_R_SUCCESS);
	}

	/*
	 * Log the current working directory.
	 */
	if (first_time) {
		char cwd[PATH_MAX];
		if (getcwd(cwd, sizeof(cwd)) == cwd) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
				      "the initial working directory is '%s'",
				      cwd);
		}
	}

	/*
	 * Parse the configuration file using the new config code.
	 */
	config = NULL;
	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		      "loading configuration from '%s'", filename);
	result = cfg_parser_create(named_g_mctx, named_g_lctx, &conf_parser);
	if (result != ISC_R_SUCCESS) {
		goto cleanup_exclusive;
	}

	cfg_parser_setcallback(conf_parser, directory_callback, NULL);
	result = cfg_parse_file(conf_parser, filename, &cfg_type_namedconf,
				&config);
	if (result != ISC_R_SUCCESS) {
		goto cleanup_conf_parser;
	}

	/*
	 * Check the validity of the configuration.
	 *
	 * (Ignore plugin parameters for now; they will be
	 * checked later when the modules are actually loaded and
	 * registered.)
	 */
	result = isccfg_check_namedconf(config, BIND_CHECK_ALGORITHMS,
					named_g_lctx, named_g_mctx);
	if (result != ISC_R_SUCCESS) {
		goto cleanup_config;
	}

	/* Let's recreate the TLS context cache */
	if (server->tlsctx_server_cache != NULL) {
		isc_tlsctx_cache_detach(&server->tlsctx_server_cache);
	}

	isc_tlsctx_cache_create(named_g_mctx, &server->tlsctx_server_cache);

	if (server->tlsctx_client_cache != NULL) {
		isc_tlsctx_cache_detach(&server->tlsctx_client_cache);
	}

	isc_tlsctx_cache_create(named_g_mctx, &server->tlsctx_client_cache);

	dns_zonemgr_set_tlsctx_cache(server->zonemgr,
				     server->tlsctx_client_cache);

	/*
	 * Fill in the maps array, used for resolving defaults.
	 */
	i = 0;
	options = NULL;
	result = cfg_map_get(config, "options", &options);
	if (result == ISC_R_SUCCESS) {
		maps[i++] = options;
	}
	maps[i++] = named_g_defaults;
	maps[i] = NULL;

#if HAVE_LIBNGHTTP2
	obj = NULL;
	result = named_config_get(maps, "http-port", &obj);
	INSIST(result == ISC_R_SUCCESS);
	named_g_httpport = (in_port_t)cfg_obj_asuint32(obj);

	obj = NULL;
	result = named_config_get(maps, "https-port", &obj);
	INSIST(result == ISC_R_SUCCESS);
	named_g_httpsport = (in_port_t)cfg_obj_asuint32(obj);

	obj = NULL;
	result = named_config_get(maps, "http-listener-clients", &obj);
	INSIST(result == ISC_R_SUCCESS);
	named_g_http_listener_clients = cfg_obj_asuint32(obj);

	obj = NULL;
	result = named_config_get(maps, "http-streams-per-connection", &obj);
	INSIST(result == ISC_R_SUCCESS);
	named_g_http_streams_per_conn = cfg_obj_asuint32(obj);
#endif

	/*
	 * If "dnssec-validation auto" is turned on, the root key
	 * will be used as a default trust anchor. The root key
	 * is built in, but if bindkeys-file is set, then it will
	 * be overridden with the key in that file.
	 */
	obj = NULL;
	(void)named_config_get(maps, "bindkeys-file", &obj);
	if (obj != NULL) {
		setstring(server, &server->bindkeysfile, cfg_obj_asstring(obj));
		INSIST(server->bindkeysfile != NULL);
		if (access(server->bindkeysfile, R_OK) != 0) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
				      "unable to open '%s'; using built-in "
				      "keys instead",
				      server->bindkeysfile);
		} else {
			result = cfg_parser_create(named_g_mctx, named_g_lctx,
						   &bindkeys_parser);
			if (result != ISC_R_SUCCESS) {
				goto cleanup_config;
			}

			result = cfg_parse_file(bindkeys_parser,
						server->bindkeysfile,
						&cfg_type_bindkeys, &bindkeys);
			if (result != ISC_R_SUCCESS) {
				isc_log_write(
					named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
					NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
					"unable to parse '%s' "
					"error '%s'; using "
					"built-in keys instead",
					server->bindkeysfile,
					isc_result_totext(result));
			}
		}
	} else {
		setstring(server, &server->bindkeysfile, NULL);
	}

#if defined(HAVE_GEOIP2)
	/*
	 * Release any previously opened GeoIP2 databases.
	 */
	named_geoip_unload();

	/*
	 * Initialize GeoIP databases from the configured location.
	 * This should happen before configuring any ACLs, so that we
	 * know what databases are available and can reject any GeoIP
	 * ACLs that can't work.
	 */
	obj = NULL;
	result = named_config_get(maps, "geoip-directory", &obj);
	INSIST(result == ISC_R_SUCCESS);
	if (cfg_obj_isstring(obj)) {
		char *dir = UNCONST(cfg_obj_asstring(obj));
		named_geoip_load(dir);
	}
	named_g_aclconfctx->geoip = named_g_geoip;
#endif /* HAVE_GEOIP2 */

	/*
	 * Configure various server options.
	 */
	configure_server_quota(maps, "transfers-out",
			       &server->sctx->xfroutquota);
	configure_server_quota(maps, "tcp-clients", &server->sctx->tcpquota);
	configure_server_quota(maps, "recursive-clients",
			       &server->sctx->recursionquota);
	configure_server_quota(maps, "update-quota", &server->sctx->updquota);
	configure_server_quota(maps, "sig0checks-quota",
			       &server->sctx->sig0checksquota);

	max = isc_quota_getmax(&server->sctx->recursionquota);
	if (max > 1000) {
		unsigned int margin = ISC_MAX(100, named_g_cpus + 1);
		if (margin + 100 > max) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "'recursive-clients %d' too low when "
				      "running with %d worker threads",
				      max, named_g_cpus);
			result = ISC_R_RANGE;

			goto cleanup_bindkeys_parser;
		}
		softquota = max - margin;
	} else {
		softquota = (max * 90) / 100;
	}
	isc_quota_soft(&server->sctx->recursionquota, softquota);

	obj = NULL;
	result = named_config_get(maps, "sig0checks-quota-exempt", &obj);
	if (result == ISC_R_SUCCESS) {
		result = cfg_acl_fromconfig(
			obj, config, named_g_lctx, named_g_aclconfctx,
			named_g_mctx, 0, &server->sctx->sig0checksquota_exempt);
		INSIST(result == ISC_R_SUCCESS);
	}

	/*
	 * Set "blackhole". Only legal at options level; there is
	 * no default.
	 */
	result = configure_view_acl(NULL, config, NULL, "blackhole", NULL,
				    named_g_aclconfctx, named_g_mctx,
				    &server->sctx->blackholeacl);
	if (result != ISC_R_SUCCESS) {
		goto cleanup_bindkeys_parser;
	}

	if (server->sctx->blackholeacl != NULL) {
		dns_dispatchmgr_setblackhole(named_g_dispatchmgr,
					     server->sctx->blackholeacl);
	}

	obj = NULL;
	result = named_config_get(maps, "match-mapped-addresses", &obj);
	INSIST(result == ISC_R_SUCCESS);
	env->match_mapped = cfg_obj_asboolean(obj);

	/*
	 * Configure the network manager
	 */
	obj = NULL;
	result = named_config_get(maps, "tcp-initial-timeout", &obj);
	INSIST(result == ISC_R_SUCCESS);
	initial = cfg_obj_asuint32(obj) * 100;
	if (initial > MAX_INITIAL_TIMEOUT) {
		cfg_obj_log(obj, named_g_lctx, ISC_LOG_WARNING,
			    "tcp-initial-timeout value is out of range: "
			    "lowering to %" PRIu32,
			    MAX_INITIAL_TIMEOUT / 100);
		initial = MAX_INITIAL_TIMEOUT;
	} else if (initial < MIN_INITIAL_TIMEOUT) {
		cfg_obj_log(obj, named_g_lctx, ISC_LOG_WARNING,
			    "tcp-initial-timeout value is out of range: "
			    "raising to %" PRIu32,
			    MIN_INITIAL_TIMEOUT / 100);
		initial = MIN_INITIAL_TIMEOUT;
	}

	obj = NULL;
	result = named_config_get(maps, "tcp-idle-timeout", &obj);
	INSIST(result == ISC_R_SUCCESS);
	idle = cfg_obj_asuint32(obj) * 100;
	if (idle > MAX_IDLE_TIMEOUT) {
		cfg_obj_log(obj, named_g_lctx, ISC_LOG_WARNING,
			    "tcp-idle-timeout value is out of range: "
			    "lowering to %" PRIu32,
			    MAX_IDLE_TIMEOUT / 100);
		idle = MAX_IDLE_TIMEOUT;
	} else if (idle < MIN_IDLE_TIMEOUT) {
		cfg_obj_log(obj, named_g_lctx, ISC_LOG_WARNING,
			    "tcp-idle-timeout value is out of range: "
			    "raising to %" PRIu32,
			    MIN_IDLE_TIMEOUT / 100);
		idle = MIN_IDLE_TIMEOUT;
	}

	obj = NULL;
	result = named_config_get(maps, "tcp-keepalive-timeout", &obj);
	INSIST(result == ISC_R_SUCCESS);
	keepalive = cfg_obj_asuint32(obj) * 100;
	if (keepalive > MAX_KEEPALIVE_TIMEOUT) {
		cfg_obj_log(obj, named_g_lctx, ISC_LOG_WARNING,
			    "tcp-keepalive-timeout value is out of range: "
			    "lowering to %" PRIu32,
			    MAX_KEEPALIVE_TIMEOUT / 100);
		keepalive = MAX_KEEPALIVE_TIMEOUT;
	} else if (keepalive < MIN_KEEPALIVE_TIMEOUT) {
		cfg_obj_log(obj, named_g_lctx, ISC_LOG_WARNING,
			    "tcp-keepalive-timeout value is out of range: "
			    "raising to %" PRIu32,
			    MIN_KEEPALIVE_TIMEOUT / 100);
		keepalive = MIN_KEEPALIVE_TIMEOUT;
	}

	obj = NULL;
	result = named_config_get(maps, "tcp-advertised-timeout", &obj);
	INSIST(result == ISC_R_SUCCESS);
	advertised = cfg_obj_asuint32(obj) * 100;
	if (advertised > MAX_ADVERTISED_TIMEOUT) {
		cfg_obj_log(obj, named_g_lctx, ISC_LOG_WARNING,
			    "tcp-advertized-timeout value is out of range: "
			    "lowering to %" PRIu32,
			    MAX_ADVERTISED_TIMEOUT / 100);
		advertised = MAX_ADVERTISED_TIMEOUT;
	}

	isc_nm_settimeouts(named_g_netmgr, initial, idle, keepalive,
			   advertised);

#define CAP_IF_NOT_ZERO(v, min, max) \
	if (v > 0 && v < min) {      \
		v = min;             \
	} else if (v > max) {        \
		v = max;             \
	}

	/* Set the kernel send and receive buffer sizes */
	obj = NULL;
	result = named_config_get(maps, "tcp-receive-buffer", &obj);
	INSIST(result == ISC_R_SUCCESS);
	recv_tcp_buffer_size = cfg_obj_asuint32(obj);
	CAP_IF_NOT_ZERO(recv_tcp_buffer_size, 4096, INT32_MAX);

	obj = NULL;
	result = named_config_get(maps, "tcp-send-buffer", &obj);
	INSIST(result == ISC_R_SUCCESS);
	send_tcp_buffer_size = cfg_obj_asuint32(obj);
	CAP_IF_NOT_ZERO(send_tcp_buffer_size, 4096, INT32_MAX);

	obj = NULL;
	result = named_config_get(maps, "udp-receive-buffer", &obj);
	INSIST(result == ISC_R_SUCCESS);
	recv_udp_buffer_size = cfg_obj_asuint32(obj);
	CAP_IF_NOT_ZERO(recv_udp_buffer_size, 4096, INT32_MAX);

	obj = NULL;
	result = named_config_get(maps, "udp-send-buffer", &obj);
	INSIST(result == ISC_R_SUCCESS);
	send_udp_buffer_size = cfg_obj_asuint32(obj);
	CAP_IF_NOT_ZERO(send_udp_buffer_size, 4096, INT32_MAX);

	isc_nm_setnetbuffers(named_g_netmgr, recv_tcp_buffer_size,
			     send_tcp_buffer_size, recv_udp_buffer_size,
			     send_udp_buffer_size);

#undef CAP_IF_NOT_ZERO

	/*
	 * Configure sets of UDP query source ports.
	 */
	result = isc_portset_create(named_g_mctx, &v4portset);
	if (result != ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "creating UDP/IPv4 port set: %s",
			      isc_result_totext(result));
		goto cleanup_bindkeys_parser;
	}
	result = isc_portset_create(named_g_mctx, &v6portset);
	if (result != ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "creating UDP/IPv6 port set: %s",
			      isc_result_totext(result));
		goto cleanup_v4portset;
	}

	usev4ports = NULL;
	usev6ports = NULL;
	avoidv4ports = NULL;
	avoidv6ports = NULL;

	(void)named_config_get(maps, "use-v4-udp-ports", &usev4ports);
	if (usev4ports != NULL) {
		portset_fromconf(v4portset, usev4ports, true);
	} else {
		result = isc_net_getudpportrange(AF_INET, &udpport_low,
						 &udpport_high);
		if (result != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "get the default UDP/IPv4 port range: %s",
				      isc_result_totext(result));
			goto cleanup_v6portset;
		}

		if (udpport_low == udpport_high) {
			isc_portset_add(v4portset, udpport_low);
		} else {
			isc_portset_addrange(v4portset, udpport_low,
					     udpport_high);
		}
		if (!ns_server_getoption(server->sctx, NS_SERVER_DISABLE4)) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
				      "using default UDP/IPv4 port range: "
				      "[%d, %d]",
				      udpport_low, udpport_high);
		}
	}
	(void)named_config_get(maps, "avoid-v4-udp-ports", &avoidv4ports);
	if (avoidv4ports != NULL) {
		portset_fromconf(v4portset, avoidv4ports, false);
	}

	(void)named_config_get(maps, "use-v6-udp-ports", &usev6ports);
	if (usev6ports != NULL) {
		portset_fromconf(v6portset, usev6ports, true);
	} else {
		result = isc_net_getudpportrange(AF_INET6, &udpport_low,
						 &udpport_high);
		if (result != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "get the default UDP/IPv6 port range: %s",
				      isc_result_totext(result));
			goto cleanup_v6portset;
		}
		if (udpport_low == udpport_high) {
			isc_portset_add(v6portset, udpport_low);
		} else {
			isc_portset_addrange(v6portset, udpport_low,
					     udpport_high);
		}
		if (!ns_server_getoption(server->sctx, NS_SERVER_DISABLE6)) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
				      "using default UDP/IPv6 port range: "
				      "[%d, %d]",
				      udpport_low, udpport_high);
		}
	}
	(void)named_config_get(maps, "avoid-v6-udp-ports", &avoidv6ports);
	if (avoidv6ports != NULL) {
		portset_fromconf(v6portset, avoidv6ports, false);
	}

	dns_dispatchmgr_setavailports(named_g_dispatchmgr, v4portset,
				      v6portset);

	/*
	 * Set the EDNS UDP size when we don't match a view.
	 */
	obj = NULL;
	result = named_config_get(maps, "edns-udp-size", &obj);
	INSIST(result == ISC_R_SUCCESS);
	udpsize = cfg_obj_asuint32(obj);
	if (udpsize < 512) {
		udpsize = 512;
	}
	if (udpsize > 4096) {
		udpsize = 4096;
	}
	server->sctx->udpsize = (uint16_t)udpsize;

	/* Set the transfer message size for TCP */
	obj = NULL;
	result = named_config_get(maps, "transfer-message-size", &obj);
	INSIST(result == ISC_R_SUCCESS);
	transfer_message_size = cfg_obj_asuint32(obj);
	if (transfer_message_size < 512) {
		transfer_message_size = 512;
	} else if (transfer_message_size > 65535) {
		transfer_message_size = 65535;
	}
	server->sctx->transfer_tcp_message_size =
		(uint16_t)transfer_message_size;

	/*
	 * Configure the zone manager.
	 */
	obj = NULL;
	result = named_config_get(maps, "transfers-in", &obj);
	INSIST(result == ISC_R_SUCCESS);
	dns_zonemgr_settransfersin(server->zonemgr, cfg_obj_asuint32(obj));

	obj = NULL;
	result = named_config_get(maps, "transfers-per-ns", &obj);
	INSIST(result == ISC_R_SUCCESS);
	dns_zonemgr_settransfersperns(server->zonemgr, cfg_obj_asuint32(obj));

	obj = NULL;
	result = named_config_get(maps, "notify-rate", &obj);
	INSIST(result == ISC_R_SUCCESS);
	dns_zonemgr_setnotifyrate(server->zonemgr, cfg_obj_asuint32(obj));

	obj = NULL;
	result = named_config_get(maps, "startup-notify-rate", &obj);
	INSIST(result == ISC_R_SUCCESS);
	dns_zonemgr_setstartupnotifyrate(server->zonemgr,
					 cfg_obj_asuint32(obj));

	obj = NULL;
	result = named_config_get(maps, "serial-query-rate", &obj);
	INSIST(result == ISC_R_SUCCESS);
	dns_zonemgr_setserialqueryrate(server->zonemgr, cfg_obj_asuint32(obj));

	/*
	 * Determine which port to use for listening for incoming connections.
	 */
	if (named_g_port != 0) {
		listen_port = named_g_port;
	} else {
		result = named_config_getport(config, "port", &listen_port);
		if (result != ISC_R_SUCCESS) {
			goto cleanup_v6portset;
		}
	}

	/*
	 * Find the listen queue depth.
	 */
	obj = NULL;
	result = named_config_get(maps, "tcp-listen-queue", &obj);
	INSIST(result == ISC_R_SUCCESS);
	backlog = cfg_obj_asuint32(obj);
	if ((backlog > 0) && (backlog < 10)) {
		backlog = 10;
	}
	ns_interfacemgr_setbacklog(server->interfacemgr, backlog);

	obj = NULL;
	result = named_config_get(maps, "reuseport", &obj);
	INSIST(result == ISC_R_SUCCESS);
	loadbalancesockets = cfg_obj_asboolean(obj);
#if HAVE_SO_REUSEPORT_LB
	if (first_time) {
		isc_nm_setloadbalancesockets(named_g_netmgr,
					     cfg_obj_asboolean(obj));
	} else if (loadbalancesockets !=
		   isc_nm_getloadbalancesockets(named_g_netmgr))
	{
		cfg_obj_log(obj, named_g_lctx, ISC_LOG_WARNING,
			    "changing reuseport value requires server restart");
	}
#else
	if (loadbalancesockets) {
		cfg_obj_log(obj, named_g_lctx, ISC_LOG_WARNING,
			    "reuseport has no effect on this system");
	}
#endif

	/*
	 * Configure the interface manager according to the "listen-on"
	 * statement.
	 */
	{
		const cfg_obj_t *clistenon = NULL;
		ns_listenlist_t *listenon = NULL;

		/*
		 * Even though listen-on is present in the default
		 * configuration, this way is easier.
		 */
		if (options != NULL) {
			(void)cfg_map_get(options, "listen-on", &clistenon);
		}
		if (clistenon != NULL) {
			result = listenlist_fromconfig(
				clistenon, config, named_g_aclconfctx,
				named_g_mctx, AF_INET,
				server->tlsctx_server_cache, &listenon);
		} else {
			/*
			 * Not specified, use default.
			 */
			result = ns_listenlist_default(named_g_mctx,
						       listen_port, true,
						       AF_INET, &listenon);
		}
		if (result != ISC_R_SUCCESS) {
			goto cleanup_v6portset;
		}

		if (listenon != NULL) {
			ns_interfacemgr_setlistenon4(server->interfacemgr,
						     listenon);
			ns_listenlist_detach(&listenon);
		}
	}

	/*
	 * Ditto for IPv6.
	 */
	{
		const cfg_obj_t *clistenon = NULL;
		ns_listenlist_t *listenon = NULL;

		if (options != NULL) {
			(void)cfg_map_get(options, "listen-on-v6", &clistenon);
		}
		if (clistenon != NULL) {
			result = listenlist_fromconfig(
				clistenon, config, named_g_aclconfctx,
				named_g_mctx, AF_INET6,
				server->tlsctx_server_cache, &listenon);
		} else {
			/*
			 * Not specified, use default.
			 */
			result = ns_listenlist_default(named_g_mctx,
						       listen_port, true,
						       AF_INET6, &listenon);
		}
		if (result != ISC_R_SUCCESS) {
			goto cleanup_v6portset;
		}
		if (listenon != NULL) {
			ns_interfacemgr_setlistenon6(server->interfacemgr,
						     listenon);
			ns_listenlist_detach(&listenon);
		}
	}

	if (first_time) {
		/*
		 * Rescan the interface list to pick up changes in the
		 * listen-on option. This requires the loopmgr to be
		 * temporarily resumed.
		 */
		isc_loopmgr_resume(named_g_loopmgr);
		result = ns_interfacemgr_scan(server->interfacemgr, true, true);
		isc_loopmgr_pause(named_g_loopmgr);

		/*
		 * Check that named is able to TCP listen on at least one
		 * interface. Otherwise, another named process could be running
		 * and we should fail.
		 */
		if (result == ISC_R_ADDRINUSE) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "unable to listen on any configured "
				      "interfaces");
			result = ISC_R_FAILURE;
			goto cleanup_v6portset;
		}
	}

	/*
	 * Arrange for further interface scanning to occur periodically
	 * as specified by the "interface-interval" option.
	 */
	obj = NULL;
	result = named_config_get(maps, "interface-interval", &obj);
	INSIST(result == ISC_R_SUCCESS);
	interface_interval = cfg_obj_asduration(obj);
	server->interface_interval = interface_interval;

	/*
	 * Enable automatic interface scans.
	 */
	obj = NULL;
	result = named_config_get(maps, "automatic-interface-scan", &obj);
	INSIST(result == ISC_R_SUCCESS);
	server->sctx->interface_auto = cfg_obj_asboolean(obj);

	if (server->sctx->interface_auto) {
		if (ns_interfacemgr_dynamic_updates_are_reliable() &&
		    server->interface_interval != 0)
		{
			/*
			 * In some cases the user might expect a certain
			 * behaviour from the rescan timer, let's try to deduce
			 * that from the configuration options.
			 */
			isc_log_write(
				named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
				"Disabling periodic interface re-scans timer");
			server->interface_interval = 0;
		}

		ns_interfacemgr_routeconnect(server->interfacemgr);
	} else {
		ns_interfacemgr_routedisconnect(server->interfacemgr);
	}

	if (server->interface_interval == 0) {
		isc_timer_stop(server->interface_timer);
	} else {
		isc_interval_set(&interval, interface_interval, 0);
		isc_timer_start(server->interface_timer, isc_timertype_ticker,
				&interval);
	}

	/*
	 * Configure the dialup heartbeat timer.
	 */
	obj = NULL;
	result = named_config_get(maps, "heartbeat-interval", &obj);
	INSIST(result == ISC_R_SUCCESS);
	heartbeat_interval = cfg_obj_asuint32(obj) * 60;
	if (heartbeat_interval == 0) {
		isc_timer_stop(server->heartbeat_timer);
	} else if (server->heartbeat_interval != heartbeat_interval) {
		isc_interval_set(&interval, heartbeat_interval, 0);
		isc_timer_start(server->heartbeat_timer, isc_timertype_ticker,
				&interval);
	}
	server->heartbeat_interval = heartbeat_interval;

	isc_interval_set(&interval, 1200, 0);
	isc_timer_start(server->pps_timer, isc_timertype_ticker, &interval);

	isc_interval_set(&interval, named_g_tat_interval, 0);
	isc_timer_start(server->tat_timer, isc_timertype_ticker, &interval);

	/*
	 * Write the PID file.
	 */
	obj = NULL;
	if (named_config_get(maps, "pid-file", &obj) == ISC_R_SUCCESS) {
		if (cfg_obj_isvoid(obj)) {
			named_os_writepidfile(NULL, first_time);
		} else {
			named_os_writepidfile(cfg_obj_asstring(obj),
					      first_time);
		}
	} else {
		named_os_writepidfile(named_g_defaultpidfile, first_time);
	}

	/*
	 * Configure the server-wide session key.  This must be done before
	 * configure views because zone configuration may need to know
	 * session-keyname.
	 *
	 * Failure of session key generation isn't fatal at this time; if it
	 * turns out that a session key is really needed but doesn't exist,
	 * we'll treat it as a fatal error then.
	 */
	(void)configure_session_key(maps, server, named_g_mctx, first_time);

	/*
	 * Create the built-in key store ("key-directory").
	 */
	result = cfg_keystore_fromconfig(NULL, named_g_mctx, named_g_lctx,
					 named_g_engine, &keystorelist, NULL);
	if (result != ISC_R_SUCCESS) {
		goto cleanup_keystorelist;
	}

	/*
	 * Create the DNSSEC key stores.
	 */
	keystores = NULL;
	(void)cfg_map_get(config, "key-store", &keystores);
	for (element = cfg_list_first(keystores); element != NULL;
	     element = cfg_list_next(element))
	{
		cfg_obj_t *kconfig = cfg_listelt_value(element);
		keystore = NULL;
		result = cfg_keystore_fromconfig(kconfig, named_g_mctx,
						 named_g_lctx, named_g_engine,
						 &keystorelist, NULL);
		if (result != ISC_R_SUCCESS) {
			goto cleanup_keystorelist;
		}
	}

	/*
	 * Create the built-in kasp policies ("default", "insecure").
	 */
	kasps = NULL;
	(void)cfg_map_get(named_g_config, "dnssec-policy", &kasps);
	for (element = cfg_list_first(kasps); element != NULL;
	     element = cfg_list_next(element))
	{
		cfg_obj_t *kconfig = cfg_listelt_value(element);

		kasp = NULL;
		result = cfg_kasp_fromconfig(kconfig, default_kasp, true,
					     named_g_mctx, named_g_lctx,
					     &keystorelist, &kasplist, &kasp);
		if (result != ISC_R_SUCCESS) {
			goto cleanup_kasplist;
		}
		INSIST(kasp != NULL);
		dns_kasp_freeze(kasp);

		/* Insist that the first built-in policy is the default one. */
		if (default_kasp == NULL) {
			INSIST(strcmp(dns_kasp_getname(kasp), "default") == 0);
			dns_kasp_attach(kasp, &default_kasp);
		}

		dns_kasp_detach(&kasp);
	}
	INSIST(default_kasp != NULL);

	/*
	 * Create the DNSSEC key and signing policies (KASP).
	 */
	kasps = NULL;
	(void)cfg_map_get(config, "dnssec-policy", &kasps);
	for (element = cfg_list_first(kasps); element != NULL;
	     element = cfg_list_next(element))
	{
		cfg_obj_t *kconfig = cfg_listelt_value(element);
		kasp = NULL;
		result = cfg_kasp_fromconfig(kconfig, default_kasp, true,
					     named_g_mctx, named_g_lctx,
					     &keystorelist, &kasplist, &kasp);
		if (result != ISC_R_SUCCESS) {
			goto cleanup_kasplist;
		}
		INSIST(kasp != NULL);
		dns_kasp_freeze(kasp);
		dns_kasp_detach(&kasp);
	}
	dns_kasp_detach(&default_kasp);

	/*
	 * Save keystore list and kasp list.
	 */
	tmpkeystorelist = server->keystorelist;
	server->keystorelist = keystorelist;
	keystorelist = tmpkeystorelist;

	tmpkasplist = server->kasplist;
	server->kasplist = kasplist;
	kasplist = tmpkasplist;

#ifdef USE_DNSRPS
	/*
	 * Find the path to the DNSRPS implementation library.
	 */
	obj = NULL;
	if (named_config_get(maps, "dnsrps-library", &obj) == ISC_R_SUCCESS) {
		if (server->dnsrpslib != NULL) {
			dns_dnsrps_server_destroy();
			isc_mem_free(server->mctx, server->dnsrpslib);
			server->dnsrpslib = NULL;
		}
		setstring(server, &server->dnsrpslib, cfg_obj_asstring(obj));
		result = dns_dnsrps_server_create(server->dnsrpslib);
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_DEBUG(1),
			      "initializing DNSRPS RPZ provider '%s': %s",
			      server->dnsrpslib, isc_result_totext(result));
		/*
		 * It's okay if librpz isn't available. We'll complain
		 * later if it turns out to be needed for a view with
		 * "dnsrps-enable yes".
		 */
		if (result == ISC_R_FILENOTFOUND) {
			result = ISC_R_SUCCESS;
		}
		CHECKFATAL(result, "initializing RPZ service interface");
	}
#endif /* ifdef USE_DNSRPS */

	/*
	 * Configure the views.
	 */
	views = NULL;
	(void)cfg_map_get(config, "view", &views);

	/*
	 * Create the views.
	 */
	for (element = cfg_list_first(views); element != NULL;
	     element = cfg_list_next(element))
	{
		cfg_obj_t *vconfig = cfg_listelt_value(element);
		dns_view_t *view = NULL;

		result = create_view(vconfig, &viewlist, &view);
		if (result != ISC_R_SUCCESS) {
			goto cleanup_viewlist;
		}
		INSIST(view != NULL);

		result = setup_newzones(view, config, vconfig, conf_parser,
					named_g_aclconfctx);
		dns_view_detach(&view);

		if (result != ISC_R_SUCCESS) {
			goto cleanup_viewlist;
		}
	}

	/*
	 * If there were no explicit views then we do the default
	 * view here.
	 */
	if (views == NULL) {
		dns_view_t *view = NULL;

		result = create_view(NULL, &viewlist, &view);
		if (result != ISC_R_SUCCESS) {
			goto cleanup_viewlist;
		}
		INSIST(view != NULL);

		result = setup_newzones(view, config, NULL, conf_parser,
					named_g_aclconfctx);

		dns_view_detach(&view);
		if (result != ISC_R_SUCCESS) {
			goto cleanup_viewlist;
		}
	}

	/*
	 * Configure and freeze all explicit views.  Explicit
	 * views that have zones were already created at parsing
	 * time, but views with no zones must be created here.
	 */
	for (element = cfg_list_first(views); element != NULL;
	     element = cfg_list_next(element))
	{
		cfg_obj_t *vconfig = cfg_listelt_value(element);
		dns_view_t *view = NULL;

		view = NULL;
		result = find_view(vconfig, &viewlist, &view);
		if (result != ISC_R_SUCCESS) {
			goto cleanup_cachelist;
		}

		result = configure_view(view, &viewlist, config, vconfig,
					&cachelist, &server->cachelist,
					&server->kasplist,
					&server->keystorelist, bindkeys,
					named_g_mctx, named_g_aclconfctx, true);
		if (result != ISC_R_SUCCESS) {
			dns_view_detach(&view);
			goto cleanup_cachelist;
		}
		dns_view_freeze(view);
		dns_view_detach(&view);
	}

	/*
	 * Make sure we have a default view if and only if there
	 * were no explicit views.
	 */
	if (views == NULL) {
		dns_view_t *view = NULL;
		result = find_view(NULL, &viewlist, &view);
		if (result != ISC_R_SUCCESS) {
			goto cleanup_cachelist;
		}
		result = configure_view(view, &viewlist, config, NULL,
					&cachelist, &server->cachelist,
					&server->kasplist,
					&server->keystorelist, bindkeys,
					named_g_mctx, named_g_aclconfctx, true);
		if (result != ISC_R_SUCCESS) {
			dns_view_detach(&view);
			goto cleanup_cachelist;
		}
		dns_view_freeze(view);
		dns_view_detach(&view);
	}

	/*
	 * Create (or recreate) the built-in views.
	 */
	builtin_views = NULL;
	RUNTIME_CHECK(cfg_map_get(named_g_config, "view", &builtin_views) ==
		      ISC_R_SUCCESS);
	for (element = cfg_list_first(builtin_views); element != NULL;
	     element = cfg_list_next(element))
	{
		cfg_obj_t *vconfig = cfg_listelt_value(element);
		dns_view_t *view = NULL;

		result = create_view(vconfig, &builtin_viewlist, &view);
		if (result != ISC_R_SUCCESS) {
			goto cleanup_cachelist;
		}

		result = configure_view(
			view, &viewlist, config, vconfig, &cachelist,
			&server->cachelist, &server->kasplist,
			&server->keystorelist, bindkeys, named_g_mctx,
			named_g_aclconfctx, false);
		if (result != ISC_R_SUCCESS) {
			dns_view_detach(&view);
			goto cleanup_cachelist;
		}
		dns_view_freeze(view);
		dns_view_detach(&view);
	}

	/* Now combine the two viewlists into one */
	ISC_LIST_APPENDLIST(viewlist, builtin_viewlist, link);

	/*
	 * Commit any dns_zone_setview() calls on all zones in the new
	 * view.
	 */
	for (dns_view_t *view = ISC_LIST_HEAD(viewlist); view != NULL;
	     view = ISC_LIST_NEXT(view, link))
	{
		dns_view_setviewcommit(view);
	}

	/* Swap our new view list with the production one. */
	tmpviewlist = server->viewlist;
	server->viewlist = viewlist;
	viewlist = tmpviewlist;

	/* Make the view list available to each of the views */
	for (dns_view_t *view = ISC_LIST_HEAD(server->viewlist); view != NULL;
	     view = ISC_LIST_NEXT(view, link))
	{
		view->viewlist = &server->viewlist;
	}

	/* Swap our new cache list with the production one. */
	tmpcachelist = server->cachelist;
	server->cachelist = cachelist;
	cachelist = tmpcachelist;

	/* Load the TKEY information from the configuration. */
	if (options != NULL) {
		dns_tkeyctx_t *tkeyctx = NULL;

		result = named_tkeyctx_fromconfig(options, named_g_mctx,
						  &tkeyctx);
		if (result != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "configuring TKEY: %s",
				      isc_result_totext(result));
			goto cleanup_cachelist;
		}
		if (server->sctx->tkeyctx != NULL) {
			dns_tkeyctx_destroy(&server->sctx->tkeyctx);
		}
		server->sctx->tkeyctx = tkeyctx;
	}

#ifdef HAVE_LMDB
	/*
	 * If we're using LMDB, we may have created newzones databases
	 * as root, making it impossible to reopen them later after
	 * switching to a new userid. We close them now, and reopen
	 * after relinquishing privileges them.
	 */
	if (first_time) {
		for (dns_view_t *view = ISC_LIST_HEAD(server->viewlist);
		     view != NULL; view = ISC_LIST_NEXT(view, link))
		{
			nzd_env_close(view);
		}
	}
#endif /* HAVE_LMDB */

	/*
	 * Switch to the effective UID for setting up files.
	 * Later, after configuring all the listening ports,
	 * we'll relinquish root privileges permanently.
	 */
	if (first_time) {
		named_os_changeuser(false);
	}

	/*
	 * Check that the working directory is writable.
	 */
	if (!isc_file_isdirwritable(".")) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "the working directory is not writable");
		result = ISC_R_NOPERM;
		goto cleanup_cachelist;
	}

#ifdef HAVE_LMDB
	/*
	 * Reopen NZD databases.
	 */
	if (first_time) {
		for (dns_view_t *view = ISC_LIST_HEAD(server->viewlist);
		     view != NULL; view = ISC_LIST_NEXT(view, link))
		{
			nzd_env_reopen(view);
		}
	}
#endif /* HAVE_LMDB */

	/*
	 * Configure the logging system.
	 *
	 * Do this after changing UID to make sure that any log
	 * files specified in named.conf get created by the
	 * unprivileged user, not root.
	 */
	if (named_g_logstderr) {
		const cfg_obj_t *logobj = NULL;

		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
			      "not using config file logging "
			      "statement for logging due to "
			      "-g option");

		(void)cfg_map_get(config, "logging", &logobj);
		if (logobj != NULL) {
			result = named_logconfig(NULL, logobj);
			if (result != ISC_R_SUCCESS) {
				isc_log_write(
					named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
					NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
					"checking logging configuration "
					"failed: %s",
					isc_result_totext(result));
				goto cleanup_cachelist;
			}
		}
	} else {
		const cfg_obj_t *logobj = NULL;

		isc_logconfig_create(named_g_lctx, &logc);

		logobj = NULL;
		(void)cfg_map_get(config, "logging", &logobj);
		if (logobj != NULL) {
			result = named_logconfig(logc, logobj);
			if (result != ISC_R_SUCCESS) {
				isc_log_write(
					named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
					NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
					"configuring logging: %s",
					isc_result_totext(result));
				goto cleanup_logc;
			}
		} else {
			named_log_setdefaultchannels(logc);
			named_log_setdefaultsslkeylogfile(logc);
			result = named_log_setunmatchedcategory(logc);
			if (result != ISC_R_SUCCESS) {
				isc_log_write(
					named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
					NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
					"setting up default 'category "
					"unmatched': %s",
					isc_result_totext(result));
				goto cleanup_logc;
			}
			result = named_log_setdefaultcategory(logc);
			if (result != ISC_R_SUCCESS) {
				isc_log_write(
					named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
					NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
					"setting up default 'category "
					"default': %s",
					isc_result_totext(result));
				goto cleanup_logc;
			}
		}

		isc_logconfig_use(named_g_lctx, logc);
		logc = NULL;

		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_DEBUG(1),
			      "now using logging configuration from "
			      "config file");
	}

	/*
	 * Set the default value of the query logging flag depending
	 * whether a "queries" category has been defined.  This is
	 * a disgusting hack, but we need to do this for BIND 8
	 * compatibility.
	 */
	if (first_time) {
		const cfg_obj_t *logobj = NULL;
		const cfg_obj_t *categories = NULL;

		obj = NULL;
		if (named_config_get(maps, "querylog", &obj) == ISC_R_SUCCESS) {
			ns_server_setoption(server->sctx, NS_SERVER_LOGQUERIES,
					    cfg_obj_asboolean(obj));
		} else {
			(void)cfg_map_get(config, "logging", &logobj);
			if (logobj != NULL) {
				(void)cfg_map_get(logobj, "category",
						  &categories);
			}
			if (categories != NULL) {
				for (element = cfg_list_first(categories);
				     element != NULL;
				     element = cfg_list_next(element))
				{
					const cfg_obj_t *catobj;
					const char *str;

					obj = cfg_listelt_value(element);
					catobj = cfg_tuple_get(obj, "name");
					str = cfg_obj_asstring(catobj);
					if (strcasecmp(str, "queries") == 0) {
						ns_server_setoption(
							server->sctx,
							NS_SERVER_LOGQUERIES,
							true);
					}
				}
			}
		}
		obj = NULL;
		result = named_config_get(maps, "responselog", &obj);
		if (result == ISC_R_SUCCESS) {
			ns_server_setoption(server->sctx,
					    NS_SERVER_LOGRESPONSES,
					    cfg_obj_asboolean(obj));
		}
	}

	obj = NULL;
	if (options != NULL &&
	    cfg_map_get(options, "memstatistics", &obj) == ISC_R_SUCCESS)
	{
		named_g_memstatistics = cfg_obj_asboolean(obj);
	} else {
		named_g_memstatistics =
			((isc_mem_debugging & ISC_MEM_DEBUGRECORD) != 0);
	}

	obj = NULL;
	if (named_config_get(maps, "memstatistics-file", &obj) == ISC_R_SUCCESS)
	{
		named_main_setmemstats(cfg_obj_asstring(obj));
	} else if (named_g_memstatistics) {
		named_main_setmemstats("named.memstats");
	} else {
		named_main_setmemstats(NULL);
	}

	obj = NULL;
	result = named_config_get(maps, "statistics-file", &obj);
	INSIST(result == ISC_R_SUCCESS);
	setstring(server, &server->statsfile, cfg_obj_asstring(obj));

	obj = NULL;
	result = named_config_get(maps, "dump-file", &obj);
	INSIST(result == ISC_R_SUCCESS);
	setstring(server, &server->dumpfile, cfg_obj_asstring(obj));

	obj = NULL;
	result = named_config_get(maps, "secroots-file", &obj);
	INSIST(result == ISC_R_SUCCESS);
	setstring(server, &server->secrootsfile, cfg_obj_asstring(obj));

	obj = NULL;
	result = named_config_get(maps, "recursing-file", &obj);
	INSIST(result == ISC_R_SUCCESS);
	setstring(server, &server->recfile, cfg_obj_asstring(obj));

	obj = NULL;
	result = named_config_get(maps, "version", &obj);
	if (result == ISC_R_SUCCESS) {
		setoptstring(server, &server->version, obj);
		server->version_set = true;
	} else {
		server->version_set = false;
	}

	obj = NULL;
	result = named_config_get(maps, "hostname", &obj);
	if (result == ISC_R_SUCCESS) {
		setoptstring(server, &server->hostname, obj);
		server->hostname_set = true;
	} else {
		server->hostname_set = false;
	}

	obj = NULL;
	result = named_config_get(maps, "server-id", &obj);
	server->sctx->usehostname = false;
	if (result == ISC_R_SUCCESS && cfg_obj_isboolean(obj)) {
		/* The parser translates "hostname" to true */
		server->sctx->usehostname = true;
		result = ns_server_setserverid(server->sctx, NULL);
	} else if (result == ISC_R_SUCCESS && !cfg_obj_isvoid(obj)) {
		/* Found a quoted string */
		result = ns_server_setserverid(server->sctx,
					       cfg_obj_asstring(obj));
	} else {
		result = ns_server_setserverid(server->sctx, NULL);
	}
	RUNTIME_CHECK(result == ISC_R_SUCCESS);

	obj = NULL;
	result = named_config_get(maps, "flush-zones-on-shutdown", &obj);
	if (result == ISC_R_SUCCESS) {
		server->flushonshutdown = cfg_obj_asboolean(obj);
	} else {
		server->flushonshutdown = false;
	}

	obj = NULL;
	result = named_config_get(maps, "answer-cookie", &obj);
	INSIST(result == ISC_R_SUCCESS);
	server->sctx->answercookie = cfg_obj_asboolean(obj);

	obj = NULL;
	result = named_config_get(maps, "cookie-algorithm", &obj);
	INSIST(result == ISC_R_SUCCESS);
	if (strcasecmp(cfg_obj_asstring(obj), "siphash24") == 0) {
		server->sctx->cookiealg = ns_cookiealg_siphash24;
	} else {
		UNREACHABLE();
	}

	obj = NULL;
	result = named_config_get(maps, "cookie-secret", &obj);
	if (result == ISC_R_SUCCESS) {
		const char *str;
		bool first = true;
		isc_buffer_t b;
		unsigned int usedlength;
		unsigned int expectedlength;

		for (element = cfg_list_first(obj); element != NULL;
		     element = cfg_list_next(element))
		{
			obj = cfg_listelt_value(element);
			str = cfg_obj_asstring(obj);

			if (first) {
				memset(server->sctx->secret, 0,
				       sizeof(server->sctx->secret));
				isc_buffer_init(&b, server->sctx->secret,
						sizeof(server->sctx->secret));
				result = isc_hex_decodestring(str, &b);
				if (result != ISC_R_SUCCESS &&
				    result != ISC_R_NOSPACE)
				{
					goto cleanup_altsecrets;
				}
				first = false;
			} else {
				altsecret = isc_mem_get(server->sctx->mctx,
							sizeof(*altsecret));
				isc_buffer_init(&b, altsecret->secret,
						sizeof(altsecret->secret));
				result = isc_hex_decodestring(str, &b);
				if (result != ISC_R_SUCCESS &&
				    result != ISC_R_NOSPACE)
				{
					isc_mem_put(server->sctx->mctx,
						    altsecret,
						    sizeof(*altsecret));
					goto cleanup_altsecrets;
				}
				ISC_LIST_INITANDAPPEND(altsecrets, altsecret,
						       link);
			}

			usedlength = isc_buffer_usedlength(&b);
			switch (server->sctx->cookiealg) {
			case ns_cookiealg_siphash24:
				expectedlength = ISC_SIPHASH24_KEY_LENGTH;
				if (usedlength != expectedlength) {
					result = ISC_R_RANGE;
					isc_log_write(
						named_g_lctx,
						NAMED_LOGCATEGORY_GENERAL,
						NAMED_LOGMODULE_SERVER,
						ISC_LOG_ERROR,
						"SipHash-2-4 cookie-secret "
						"must be 128 bits: %s",
						isc_result_totext(result));
					goto cleanup_altsecrets;
				}
				break;
			}
		}
	} else {
		isc_nonce_buf(server->sctx->secret,
			      sizeof(server->sctx->secret));
	}

	/*
	 * Swap altsecrets lists.
	 */
	tmpaltsecrets = server->sctx->altsecrets;
	server->sctx->altsecrets = altsecrets;
	altsecrets = tmpaltsecrets;

	(void)named_server_loadnta(server);

#ifdef USE_DNSRPS
	/*
	 * Start and connect to the DNS Response Policy Service
	 * daemon, dnsrpzd, for each view that uses DNSRPS.
	 */
	for (dns_view_t *view = ISC_LIST_HEAD(server->viewlist); view != NULL;
	     view = ISC_LIST_NEXT(view, link))
	{
		result = dns_dnsrps_connect(view->rpzs);
		if (result != ISC_R_SUCCESS) {
			view = NULL;
			goto cleanup_altsecrets;
		}
	}
#endif /* ifdef USE_DNSRPS */

	/*
	 * Record the time of most recent configuration
	 */
	named_g_configtime = isc_time_now();

	isc_loopmgr_resume(named_g_loopmgr);
	exclusive = false;

	/* Take back root privileges temporarily */
	if (first_time) {
		named_os_restoreuser();
	}

	/* Configure the statistics channel(s) */
	result = named_statschannels_configure(named_g_server, config,
					       named_g_aclconfctx);
	if (result != ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "configuring statistics server(s): %s",
			      isc_result_totext(result));
		goto cleanup_altsecrets;
	}

	/*
	 * Bind the control port(s).
	 */
	result = named_controls_configure(named_g_server->controls, config,
					  named_g_aclconfctx);
	if (result != ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "binding control channel(s): %s",
			      isc_result_totext(result));
		goto cleanup_altsecrets;
	}

	(void)ns_interfacemgr_scan(server->interfacemgr, true, true);

	/*
	 * Permanently drop root privileges now.
	 */
	if (first_time) {
		named_os_changeuser(true);
	}

	/*
	 * These cleans up either the old production view list
	 * or our temporary list depending on whether they
	 * were swapped above or not.
	 */
cleanup_altsecrets:
	while ((altsecret = ISC_LIST_HEAD(altsecrets)) != NULL) {
		ISC_LIST_UNLINK(altsecrets, altsecret, link);
		isc_mem_put(server->sctx->mctx, altsecret, sizeof(*altsecret));
	}

cleanup_logc:
	if (logc != NULL) {
		isc_logconfig_destroy(&logc);
	}

cleanup_cachelist:
	while ((nsc = ISC_LIST_HEAD(cachelist)) != NULL) {
		ISC_LIST_UNLINK(cachelist, nsc, link);
		dns_cache_detach(&nsc->cache);
		isc_mem_put(server->mctx, nsc, sizeof(*nsc));
	}

	ISC_LIST_APPENDLIST(viewlist, builtin_viewlist, link);

cleanup_viewlist:
	for (dns_view_t *view = ISC_LIST_HEAD(viewlist); view != NULL;
	     view = view_next)
	{
		view_next = ISC_LIST_NEXT(view, link);
		ISC_LIST_UNLINK(viewlist, view, link);
		if (result == ISC_R_SUCCESS && strcmp(view->name, "_bind") != 0)
		{
			dns_view_setviewrevert(view);
			(void)dns_view_apply(view, false, NULL, removed, view);
		}
		dns_view_detach(&view);
	}

cleanup_kasplist:
	for (kasp = ISC_LIST_HEAD(kasplist); kasp != NULL; kasp = kasp_next) {
		kasp_next = ISC_LIST_NEXT(kasp, link);
		ISC_LIST_UNLINK(kasplist, kasp, link);
		dns_kasp_detach(&kasp);
	}

cleanup_keystorelist:
	for (keystore = ISC_LIST_HEAD(keystorelist); keystore != NULL;
	     keystore = keystore_next)
	{
		keystore_next = ISC_LIST_NEXT(keystore, link);
		ISC_LIST_UNLINK(keystorelist, keystore, link);
		dns_keystore_detach(&keystore);
	}

cleanup_v6portset:
	isc_portset_destroy(named_g_mctx, &v6portset);

cleanup_v4portset:
	isc_portset_destroy(named_g_mctx, &v4portset);

cleanup_bindkeys_parser:

	if (bindkeys_parser != NULL) {
		if (bindkeys != NULL) {
			cfg_obj_destroy(bindkeys_parser, &bindkeys);
		}
		cfg_parser_destroy(&bindkeys_parser);
	}

cleanup_config:
	cfg_obj_destroy(conf_parser, &config);

cleanup_conf_parser:
	cfg_parser_destroy(&conf_parser);

cleanup_exclusive:
	if (exclusive) {
		isc_loopmgr_resume(named_g_loopmgr);
	}

	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_DEBUG(1),
		      "load_configuration: %s", isc_result_totext(result));

	return result;
}

static isc_result_t
view_loaded(void *arg) {
	isc_result_t result;
	ns_zoneload_t *zl = (ns_zoneload_t *)arg;

	/*
	 * Force zone maintenance.  Do this after loading
	 * so that we know when we need to force AXFR of
	 * secondary zones whose master files are missing.
	 *
	 * We use the zoneload reference counter to let us
	 * know when all views are finished.
	 */
	if (isc_refcount_decrement(&zl->refs) == 1) {
		named_server_t *server = zl->server;
		bool reconfig = zl->reconfig;
		dns_view_t *view = NULL;

		isc_refcount_destroy(&zl->refs);
		isc_mem_put(server->mctx, zl, sizeof(*zl));

		/*
		 * To maintain compatibility with log parsing tools that might
		 * be looking for this string after "rndc reconfig", we keep it
		 * as it is
		 */
		if (reconfig) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
				      "any newly configured zones are now "
				      "loaded");
		} else {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_NOTICE,
				      "all zones loaded");
		}

		for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
		     view = ISC_LIST_NEXT(view, link))
		{
			if (view->managed_keys != NULL) {
				result = dns_zone_synckeyzone(
					view->managed_keys);
				if (result != ISC_R_SUCCESS) {
					isc_log_write(
						named_g_lctx,
						DNS_LOGCATEGORY_DNSSEC,
						DNS_LOGMODULE_DNSSEC,
						ISC_LOG_ERROR,
						"failed to initialize "
						"managed-keys for view %s "
						"(%s): DNSSEC validation is "
						"at risk",
						view->name,
						isc_result_totext(result));
				}
			}
		}

		CHECKFATAL(dns_zonemgr_forcemaint(server->zonemgr),
			   "forcing zone maintenance");

		named_os_started();

		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_NOTICE,
			      "FIPS mode is %s",
			      isc_fips_mode() ? "enabled" : "disabled");

		named_os_notify_systemd("READY=1\n"
					"STATUS=running\n"
					"MAINPID=%" PRId64 "\n",
					(int64_t)getpid());

		atomic_store(&server->reload_status, NAMED_RELOAD_DONE);

		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_NOTICE,
			      "running");
	}

	return ISC_R_SUCCESS;
}

static isc_result_t
load_zones(named_server_t *server, bool reconfig) {
	isc_result_t result = ISC_R_SUCCESS;
	ns_zoneload_t *zl = NULL;
	dns_view_t *view = NULL;

	zl = isc_mem_get(server->mctx, sizeof(*zl));
	zl->server = server;
	zl->reconfig = reconfig;

	isc_loopmgr_pause(named_g_loopmgr);

	isc_refcount_init(&zl->refs, 1);

	/*
	 * Schedule zones to be loaded from disk.
	 */
	for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
	     view = ISC_LIST_NEXT(view, link))
	{
		if (view->managed_keys != NULL) {
			result = dns_zone_load(view->managed_keys, false);
			if (result != ISC_R_SUCCESS &&
			    result != DNS_R_UPTODATE &&
			    result != DNS_R_CONTINUE)
			{
				goto cleanup;
			}
		}
		if (view->redirect != NULL) {
			result = dns_zone_load(view->redirect, false);
			if (result != ISC_R_SUCCESS &&
			    result != DNS_R_UPTODATE &&
			    result != DNS_R_CONTINUE)
			{
				goto cleanup;
			}
		}

		/*
		 * 'dns_view_asyncload' calls view_loaded if there are no
		 * zones.
		 */
		isc_refcount_increment(&zl->refs);
		result = dns_view_asyncload(view, reconfig, view_loaded, zl);
		if (result != ISC_R_SUCCESS) {
			isc_refcount_decrement1(&zl->refs);
			goto cleanup;
		}
	}

cleanup:
	if (isc_refcount_decrement(&zl->refs) == 1) {
		isc_refcount_destroy(&zl->refs);
		isc_mem_put(server->mctx, zl, sizeof(*zl));
	}

	isc_loopmgr_resume(named_g_loopmgr);

	return result;
}

static void
run_server(void *arg) {
	isc_result_t result;
	named_server_t *server = (named_server_t *)arg;
	dns_geoip_databases_t *geoip = NULL;

	dns_zonemgr_create(named_g_mctx, named_g_netmgr, &server->zonemgr);

	CHECKFATAL(dns_dispatchmgr_create(named_g_mctx, named_g_loopmgr,
					  named_g_netmgr, &named_g_dispatchmgr),
		   "creating dispatch manager");

	dns_dispatchmgr_setstats(named_g_dispatchmgr, server->resolverstats);

#if defined(HAVE_GEOIP2)
	geoip = named_g_geoip;
#else  /* if defined(HAVE_GEOIP2) */
	geoip = NULL;
#endif /* if defined(HAVE_GEOIP2) */

	CHECKFATAL(ns_interfacemgr_create(named_g_mctx, server->sctx,
					  named_g_loopmgr, named_g_netmgr,
					  named_g_dispatchmgr, geoip,
					  &server->interfacemgr),
		   "creating interface manager");

	isc_timer_create(named_g_mainloop, interface_timer_tick, server,
			 &server->interface_timer);

	isc_timer_create(named_g_mainloop, heartbeat_timer_tick, server,
			 &server->heartbeat_timer);

	isc_timer_create(named_g_mainloop, tat_timer_tick, server,
			 &server->tat_timer);

	isc_timer_create(named_g_mainloop, pps_timer_tick, server,
			 &server->pps_timer);

	CHECKFATAL(
		cfg_parser_create(named_g_mctx, named_g_lctx, &named_g_parser),
		"creating default configuration parser");

	CHECKFATAL(cfg_parser_create(named_g_mctx, named_g_lctx,
				     &named_g_addparser),
		   "creating additional configuration parser");

	CHECKFATAL(load_configuration(named_g_conffile, server, true),
		   "loading configuration");

	CHECKFATAL(load_zones(server, false), "loading zones");
#ifdef ENABLE_AFL
	named_g_run_done = true;
#endif /* ifdef ENABLE_AFL */
}

void
named_server_flushonshutdown(named_server_t *server, bool flush) {
	REQUIRE(NAMED_SERVER_VALID(server));

	server->flushonshutdown = flush;
}

static void
shutdown_server(void *arg) {
	named_server_t *server = (named_server_t *)arg;
	dns_view_t *view = NULL, *view_next = NULL;
	dns_kasp_t *kasp = NULL, *kasp_next = NULL;
	dns_keystore_t *keystore = NULL, *keystore_next = NULL;
	bool flush = server->flushonshutdown;
	named_cache_t *nsc = NULL;

	named_os_notify_systemd("STOPPING=1\n");
	named_os_notify_close();

	isc_signal_stop(server->sighup);
	isc_signal_destroy(&server->sighup);

	/*
	 * We need to shutdown the interface before going
	 * exclusive (which would pause the netmgr).
	 */
	ns_interfacemgr_shutdown(server->interfacemgr);

	named_controls_shutdown(server->controls);

	named_statschannels_shutdown(server);

	isc_loopmgr_pause(named_g_loopmgr);

	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO, "shutting down%s",
		      flush ? ": flushing changes" : "");

	cleanup_session_key(server, server->mctx);

	if (named_g_aclconfctx != NULL) {
		cfg_aclconfctx_detach(&named_g_aclconfctx);
	}

	cfg_obj_destroy(named_g_parser, &named_g_config);
	cfg_parser_destroy(&named_g_parser);
	cfg_parser_destroy(&named_g_addparser);

	(void)named_server_saventa(server);

	for (kasp = ISC_LIST_HEAD(server->kasplist); kasp != NULL;
	     kasp = kasp_next)
	{
		kasp_next = ISC_LIST_NEXT(kasp, link);
		ISC_LIST_UNLINK(server->kasplist, kasp, link);
		dns_kasp_detach(&kasp);
	}

	for (keystore = ISC_LIST_HEAD(server->keystorelist); keystore != NULL;
	     keystore = keystore_next)
	{
		keystore_next = ISC_LIST_NEXT(keystore, link);
		ISC_LIST_UNLINK(server->keystorelist, keystore, link);
		dns_keystore_detach(&keystore);
	}

	for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
	     view = view_next)
	{
		view_next = ISC_LIST_NEXT(view, link);
		ISC_LIST_UNLINK(server->viewlist, view, link);
		dns_view_flushonshutdown(view, flush);
		dns_view_detach(&view);
	}

	/*
	 * Shut down all dyndb instances.
	 */
	dns_dyndb_cleanup(true);

	while ((nsc = ISC_LIST_HEAD(server->cachelist)) != NULL) {
		ISC_LIST_UNLINK(server->cachelist, nsc, link);
		dns_cache_detach(&nsc->cache);
		isc_mem_put(server->mctx, nsc, sizeof(*nsc));
	}

	isc_timer_destroy(&server->interface_timer);
	isc_timer_destroy(&server->heartbeat_timer);
	isc_timer_destroy(&server->pps_timer);
	isc_timer_destroy(&server->tat_timer);

	ns_interfacemgr_detach(&server->interfacemgr);

	dns_dispatchmgr_detach(&named_g_dispatchmgr);

	dns_zonemgr_shutdown(server->zonemgr);

#if defined(HAVE_GEOIP2)
	named_geoip_shutdown();
#endif /* HAVE_GEOIP2 */

	dns_db_detach(&server->in_roothints);

	isc_loopmgr_resume(named_g_loopmgr);
}

static isc_result_t
get_matching_view_sync(isc_netaddr_t *srcaddr, isc_netaddr_t *destaddr,
		       dns_message_t *message, dns_aclenv_t *env,
		       isc_result_t *sigresult, dns_view_t **viewp) {
	dns_view_t *view;

	/*
	 * We should not be running synchronous view matching if signature
	 * checking involves SIG(0). TSIG has priority of SIG(0), so if TSIG
	 * is set then we proceed anyway.
	 */
	INSIST(message->tsigkey != NULL || message->tsig != NULL ||
	       message->sig0 == NULL);

	for (view = ISC_LIST_HEAD(named_g_server->viewlist); view != NULL;
	     view = ISC_LIST_NEXT(view, link))
	{
		if (message->rdclass == view->rdclass ||
		    message->rdclass == dns_rdataclass_any)
		{
			const dns_name_t *tsig = NULL;

			dns_message_resetsig(message);
			*sigresult = dns_message_checksig(message, view);
			if (*sigresult == ISC_R_SUCCESS) {
				tsig = dns_tsigkey_identity(message->tsigkey);
			}

			if (dns_acl_allowed(srcaddr, tsig, view->matchclients,
					    env) &&
			    dns_acl_allowed(destaddr, tsig,
					    view->matchdestinations, env) &&
			    !(view->matchrecursiveonly &&
			      (message->flags & DNS_MESSAGEFLAG_RD) == 0))
			{
				dns_view_attach(view, viewp);
				return ISC_R_SUCCESS;
			}
		}
	}

	return ISC_R_NOTFOUND;
}

static void
get_matching_view_done(void *cbarg) {
	matching_view_ctx_t *mvctx = cbarg;
	dns_message_t *message = mvctx->message;

	if (*mvctx->viewmatchresult == ISC_R_SUCCESS) {
		INSIST(mvctx->view != NULL);
		dns_view_attach(mvctx->view, mvctx->viewp);
	}

	mvctx->cb(mvctx->cbarg);

	if (mvctx->quota_result == ISC_R_SUCCESS) {
		isc_quota_release(&mvctx->sctx->sig0checksquota);
	}
	if (mvctx->view != NULL) {
		dns_view_detach(&mvctx->view);
	}
	isc_loop_detach(&mvctx->loop);
	ns_server_detach(&mvctx->sctx);
	isc_mem_put(message->mctx, mvctx, sizeof(*mvctx));
	dns_message_detach(&message);
}

static dns_view_t *
get_matching_view_next(dns_view_t *view, dns_rdataclass_t rdclass) {
	if (view == NULL) {
		view = ISC_LIST_HEAD(named_g_server->viewlist);
	} else {
		view = ISC_LIST_NEXT(view, link);
	}
	while (true) {
		if (view == NULL || rdclass == view->rdclass ||
		    rdclass == dns_rdataclass_any)
		{
			return view;
		}
		view = ISC_LIST_NEXT(view, link);
	};
}

static void
get_matching_view_continue(void *cbarg, isc_result_t result) {
	matching_view_ctx_t *mvctx = cbarg;
	dns_view_t *view = NULL;
	const dns_name_t *tsig = NULL;

	*mvctx->sigresult = result;

	if (result == ISC_R_SUCCESS) {
		tsig = dns_tsigkey_identity(mvctx->message->tsigkey);
	}

	if (dns_acl_allowed(mvctx->srcaddr, tsig, mvctx->view->matchclients,
			    mvctx->env) &&
	    dns_acl_allowed(mvctx->destaddr, tsig,
			    mvctx->view->matchdestinations, mvctx->env) &&
	    !(mvctx->view->matchrecursiveonly &&
	      (mvctx->message->flags & DNS_MESSAGEFLAG_RD) == 0))
	{
		/*
		 * A matching view is found.
		 */
		*mvctx->viewmatchresult = ISC_R_SUCCESS;
		get_matching_view_done(cbarg);
		return;
	}

	dns_message_resetsig(mvctx->message);

	view = get_matching_view_next(mvctx->view, mvctx->message->rdclass);
	dns_view_detach(&mvctx->view);
	if (view != NULL) {
		/*
		 * Try the next view.
		 */
		dns_view_attach(view, &mvctx->view);
		result = dns_message_checksig_async(
			mvctx->message, view, mvctx->loop,
			get_matching_view_continue, mvctx);
		INSIST(result == DNS_R_WAIT);
		return;
	}

	/*
	 * No matching view is found.
	 */
	*mvctx->viewmatchresult = ISC_R_NOTFOUND;
	get_matching_view_done(cbarg);
}

/*%
 * Find a view that matches the source and destination addresses of a query.
 */
static isc_result_t
get_matching_view(isc_netaddr_t *srcaddr, isc_netaddr_t *destaddr,
		  dns_message_t *message, dns_aclenv_t *env, ns_server_t *sctx,
		  isc_loop_t *loop, isc_job_cb cb, void *cbarg,
		  isc_result_t *sigresult, isc_result_t *viewmatchresult,
		  dns_view_t **viewp) {
	dns_view_t *view = NULL;
	isc_result_t result;

	REQUIRE(message != NULL);
	REQUIRE(sctx != NULL);
	REQUIRE(loop == NULL || cb != NULL);
	REQUIRE(sigresult != NULL);
	REQUIRE(viewmatchresult != NULL);
	REQUIRE(viewp != NULL && *viewp == NULL);

	/* No offloading is requested if the loop is unset. */
	if (loop == NULL) {
		*viewmatchresult = get_matching_view_sync(
			srcaddr, destaddr, message, env, sigresult, viewp);
		return *viewmatchresult;
	}

	/* Also no offloading when there is no view at all to match against. */
	view = get_matching_view_next(NULL, message->rdclass);
	if (view == NULL) {
		*viewmatchresult = ISC_R_NOTFOUND;
		return *viewmatchresult;
	}

	dns_message_resetsig(message);

	matching_view_ctx_t *mvctx = isc_mem_get(message->mctx, sizeof(*mvctx));
	*mvctx = (matching_view_ctx_t){
		.srcaddr = srcaddr,
		.destaddr = destaddr,
		.env = env,
		.cb = cb,
		.cbarg = cbarg,
		.sigresult = sigresult,
		.viewmatchresult = viewmatchresult,
		.quota_result = ISC_R_UNSET,
		.viewp = viewp,
	};
	ns_server_attach(sctx, &mvctx->sctx);
	isc_loop_attach(loop, &mvctx->loop);
	dns_message_attach(message, &mvctx->message);

	/*
	 * If the message has a SIG0 signature which we are going to
	 * check, and the client is not exempt from the SIG(0) quota,
	 * then acquire a quota. TSIG has priority over SIG(0), so if
	 * TSIG is set then we don't care.
	 */
	if (message->tsigkey == NULL && message->tsig == NULL &&
	    message->sig0 != NULL)
	{
		if (sctx->sig0checksquota_exempt != NULL) {
			int exempt_match;

			result = dns_acl_match(srcaddr, NULL,
					       sctx->sig0checksquota_exempt,
					       env, &exempt_match, NULL);
			if (result == ISC_R_SUCCESS && exempt_match > 0) {
				mvctx->quota_result = ISC_R_EXISTS;
			}
		}
		if (mvctx->quota_result == ISC_R_UNSET) {
			mvctx->quota_result =
				isc_quota_acquire(&sctx->sig0checksquota);
		}
		if (mvctx->quota_result == ISC_R_SOFTQUOTA) {
			isc_quota_release(&sctx->sig0checksquota);
		}
		if (mvctx->quota_result != ISC_R_SUCCESS &&
		    mvctx->quota_result != ISC_R_EXISTS)
		{
			*mvctx->viewmatchresult = ISC_R_QUOTA;
			isc_async_run(loop, get_matching_view_done, mvctx);
			return DNS_R_WAIT;
		}
	}

	dns_view_attach(view, &mvctx->view);
	result = dns_message_checksig_async(message, view, loop,
					    get_matching_view_continue, mvctx);
	INSIST(result == DNS_R_WAIT);

	return DNS_R_WAIT;
}

void
named_server_create(isc_mem_t *mctx, named_server_t **serverp) {
	isc_result_t result;
	named_server_t *server = isc_mem_get(mctx, sizeof(*server));

	*server = (named_server_t){
		.mctx = mctx,
		.statsfile = isc_mem_strdup(mctx, "named.stats"),
		.dumpfile = isc_mem_strdup(mctx, "named_dump.db"),
		.secrootsfile = isc_mem_strdup(mctx, "named.secroots"),
		.recfile = isc_mem_strdup(mctx, "named.recursing"),
	};

	/* Initialize server data structures. */
	ISC_LIST_INIT(server->kasplist);
	ISC_LIST_INIT(server->keystorelist);
	ISC_LIST_INIT(server->viewlist);

	/* Must be first. */
	CHECKFATAL(dst_lib_init(named_g_mctx, named_g_engine),
		   "initializing DST");

	CHECKFATAL(dns_rootns_create(mctx, dns_rdataclass_in, NULL,
				     &server->in_roothints),
		   "setting up root hints");

	atomic_init(&server->reload_status, NAMED_RELOAD_IN_PROGRESS);

	ns_server_create(mctx, get_matching_view, &server->sctx);

#if defined(HAVE_GEOIP2)
	/*
	 * GeoIP must be initialized before the interface
	 * manager (which includes the ACL environment)
	 * is created.
	 */
	named_geoip_init();
#endif /* HAVE_GEOIP2 */

#ifdef ENABLE_AFL
	server->sctx->fuzztype = named_g_fuzz_type;
	server->sctx->fuzznotify = named_fuzz_notify;
#endif /* ifdef ENABLE_AFL */

	named_g_mainloop = isc_loop_main(named_g_loopmgr);

	isc_loop_setup(named_g_mainloop, run_server, server);
	isc_loop_teardown(named_g_mainloop, shutdown_server, server);

	/* Add SIGHUP reload handler  */
	server->sighup = isc_signal_new(
		named_g_loopmgr, named_server_reloadwanted, server, SIGHUP);

	isc_stats_create(server->mctx, &server->sockstats,
			 isc_sockstatscounter_max);
	isc_nm_setstats(named_g_netmgr, server->sockstats);

	isc_stats_create(named_g_mctx, &server->zonestats,
			 dns_zonestatscounter_max);

	isc_stats_create(named_g_mctx, &server->resolverstats,
			 dns_resstatscounter_max);

	CHECKFATAL(named_controls_create(server, &server->controls),
		   "named_controls_create");

	ISC_LIST_INIT(server->statschannels);

	ISC_LIST_INIT(server->cachelist);

	server->magic = NAMED_SERVER_MAGIC;

	*serverp = server;
}

void
named_server_destroy(named_server_t **serverp) {
	named_server_t *server = *serverp;
	REQUIRE(NAMED_SERVER_VALID(server));

#ifdef HAVE_DNSTAP
	if (server->dtenv != NULL) {
		dns_dt_detach(&server->dtenv);
	}
#endif /* HAVE_DNSTAP */

#ifdef USE_DNSRPS
	dns_dnsrps_server_destroy();
	isc_mem_free(server->mctx, server->dnsrpslib);
#endif /* ifdef USE_DNSRPS */

	named_controls_destroy(&server->controls);

	isc_stats_detach(&server->zonestats);
	isc_stats_detach(&server->sockstats);
	isc_stats_detach(&server->resolverstats);

	if (server->sctx != NULL) {
		ns_server_detach(&server->sctx);
	}

	isc_mem_free(server->mctx, server->statsfile);
	isc_mem_free(server->mctx, server->dumpfile);
	isc_mem_free(server->mctx, server->secrootsfile);
	isc_mem_free(server->mctx, server->recfile);

	if (server->bindkeysfile != NULL) {
		isc_mem_free(server->mctx, server->bindkeysfile);
	}

	if (server->version != NULL) {
		isc_mem_free(server->mctx, server->version);
	}
	if (server->hostname != NULL) {
		isc_mem_free(server->mctx, server->hostname);
	}

	if (server->zonemgr != NULL) {
		dns_zonemgr_detach(&server->zonemgr);
	}

	dst_lib_destroy();

	INSIST(ISC_LIST_EMPTY(server->kasplist));
	INSIST(ISC_LIST_EMPTY(server->keystorelist));
	INSIST(ISC_LIST_EMPTY(server->viewlist));
	INSIST(ISC_LIST_EMPTY(server->cachelist));

	if (server->tlsctx_server_cache != NULL) {
		isc_tlsctx_cache_detach(&server->tlsctx_server_cache);
	}

	if (server->tlsctx_client_cache != NULL) {
		isc_tlsctx_cache_detach(&server->tlsctx_client_cache);
	}

	server->magic = 0;
	isc_mem_put(server->mctx, server, sizeof(*server));
	*serverp = NULL;
}

static void
fatal(const char *msg, isc_result_t result) {
	if (named_g_loopmgr_running) {
		isc_loopmgr_pause(named_g_loopmgr);
	}
	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_CRITICAL, "%s: %s", msg,
		      isc_result_totext(result));
	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_CRITICAL,
		      "exiting (due to fatal error)");
	named_os_shutdown();
	_exit(EXIT_FAILURE);
}

static isc_result_t
loadconfig(named_server_t *server) {
	isc_result_t result;
	result = load_configuration(named_g_conffile, server, false);
	if (result == ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
			      "reloading configuration succeeded");
	} else {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "reloading configuration failed: %s",
			      isc_result_totext(result));
		atomic_store(&server->reload_status, NAMED_RELOAD_FAILED);
	}

	return result;
}

static isc_result_t
reload(named_server_t *server) {
	isc_result_t result;

	atomic_store(&server->reload_status, NAMED_RELOAD_IN_PROGRESS);

	named_os_notify_systemd("RELOADING=1\n"
				"MONOTONIC_USEC=%" PRIu64 "\n"
				"STATUS=reload command received\n",
				(uint64_t)isc_time_monotonic() / NS_PER_US);

	CHECK(loadconfig(server));

	result = load_zones(server, false);
	if (result == ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
			      "reloading zones succeeded");
	} else {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "reloading zones failed: %s",
			      isc_result_totext(result));
		atomic_store(&server->reload_status, NAMED_RELOAD_FAILED);
	}
cleanup:
	named_os_notify_systemd("READY=1\n"
				"STATUS=reload command finished: %s\n",
				isc_result_totext(result));

	return result;
}

/*
 * Handle a reload event (from SIGHUP).
 */
static void
named_server_reload(void *arg) {
	named_server_t *server = (named_server_t *)arg;

	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		      "received SIGHUP signal to reload zones");
	(void)reload(server);
}

void
named_server_reloadwanted(void *arg, int signum) {
	named_server_t *server = (named_server_t *)arg;

	REQUIRE(signum == SIGHUP);

	isc_async_run(named_g_mainloop, named_server_reload, server);
}

#ifdef JEMALLOC_API_SUPPORTED
static isc_result_t
memprof_toggle(bool active) {
	if (mallctl("prof.active", NULL, NULL, &active, sizeof(active)) != 0) {
		return ISC_R_FAILURE;
	}

	return ISC_R_SUCCESS;
}

static isc_result_t
memprof_dump(void) {
	if (mallctl("prof.dump", NULL, NULL, NULL, 0) != 0) {
		return ISC_R_FAILURE;
	}

	return ISC_R_SUCCESS;
}
#else
static isc_result_t
memprof_toggle(bool active) {
	UNUSED(active);

	return ISC_R_NOTIMPLEMENTED;
}

static isc_result_t
memprof_dump(void) {
	return ISC_R_NOTIMPLEMENTED;
}

#endif /* JEMALLOC_API_SUPPORTED */

void
named_server_scan_interfaces(named_server_t *server) {
	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_DEBUG(1),
		      "automatic interface rescan");

	ns_interfacemgr_scan(server->interfacemgr, true, false);
}

/*
 * Get the next token from lexer 'lex'.
 *
 * NOTE: the token value for string tokens always uses the same pointer
 * value.  Multiple calls to this function on the same lexer will always
 * return either that value (lex->data) or NULL. It is necessary to copy
 * the token into local storage if it needs to be referenced after the next
 * call to next_token().
 */
static char *
next_token(isc_lex_t *lex, isc_buffer_t **text) {
	isc_result_t result;
	isc_token_t token;

	token.type = isc_tokentype_unknown;
	result = isc_lex_gettoken(lex, ISC_LEXOPT_EOF | ISC_LEXOPT_QSTRING,
				  &token);

	switch (result) {
	case ISC_R_NOMORE:
		(void)isc_lex_close(lex);
		break;
	case ISC_R_SUCCESS:
		if (token.type == isc_tokentype_eof) {
			(void)isc_lex_close(lex);
		}
		break;
	case ISC_R_NOSPACE:
		if (text != NULL) {
			(void)putstr(text, "token too large");
			(void)putnull(text);
		}
		return NULL;
	default:
		if (text != NULL) {
			(void)putstr(text, isc_result_totext(result));
			(void)putnull(text);
		}
		return NULL;
	}

	if (token.type == isc_tokentype_string ||
	    token.type == isc_tokentype_qstring)
	{
		return token.value.as_textregion.base;
	}

	return NULL;
}

/*
 * Find the zone specified in the control channel command, if any.
 * If a zone is specified, point '*zonep' at it, otherwise
 * set '*zonep' to NULL, and f 'zonename' is not NULL, copy
 * the zone name into it (N.B. 'zonename' must have space to hold
 * a full DNS name).
 *
 * If 'zonetxt' is set, the caller has already pulled a token
 * off the command line that is to be used as the zone name. (This
 * is sometimes done when it's necessary to check for an optional
 * argument before the zone name, as in "rndc sync [-clean] zone".)
 */
static isc_result_t
zone_from_args(named_server_t *server, isc_lex_t *lex, const char *zonetxt,
	       dns_zone_t **zonep, char *zonename, isc_buffer_t **text,
	       bool skip) {
	char *ptr;
	char *classtxt;
	const char *viewtxt = NULL;
	dns_fixedname_t fname;
	dns_name_t *name;
	isc_result_t result;
	dns_view_t *view = NULL;
	dns_rdataclass_t rdclass;
	char problem[DNS_NAME_FORMATSIZE + 500] = "";
	char zonebuf[DNS_NAME_FORMATSIZE];
	bool redirect = false;

	REQUIRE(zonep != NULL && *zonep == NULL);

	if (skip) {
		/* Skip the command name. */
		ptr = next_token(lex, text);
		if (ptr == NULL) {
			return ISC_R_UNEXPECTEDEND;
		}
	}

	/* Look for the zone name. */
	if (zonetxt == NULL) {
		zonetxt = next_token(lex, text);
	}
	if (zonetxt == NULL) {
		return ISC_R_SUCCESS;
	}

	/* Copy zonetxt because it'll be overwritten by next_token() */
	/* To locate a zone named "-redirect" use "-redirect." */
	if (strcmp(zonetxt, "-redirect") == 0) {
		redirect = true;
		strlcpy(zonebuf, ".", DNS_NAME_FORMATSIZE);
	} else {
		strlcpy(zonebuf, zonetxt, DNS_NAME_FORMATSIZE);
	}
	if (zonename != NULL) {
		strlcpy(zonename, redirect ? "." : zonetxt,
			DNS_NAME_FORMATSIZE);
	}

	name = dns_fixedname_initname(&fname);
	CHECK(dns_name_fromstring(name, zonebuf, dns_rootname, 0, NULL));

	/* Look for the optional class name. */
	classtxt = next_token(lex, text);
	if (classtxt != NULL) {
		isc_textregion_t r;
		r.base = classtxt;
		r.length = strlen(classtxt);
		CHECK(dns_rdataclass_fromtext(&rdclass, &r));

		/* Look for the optional view name. */
		viewtxt = next_token(lex, text);
	} else {
		rdclass = dns_rdataclass_in;
	}

	if (viewtxt == NULL) {
		if (redirect) {
			result = dns_viewlist_find(&server->viewlist,
						   "_default",
						   dns_rdataclass_in, &view);
			if (result != ISC_R_SUCCESS || view->redirect == NULL) {
				result = ISC_R_NOTFOUND;
				snprintf(problem, sizeof(problem),
					 "redirect zone not found in "
					 "_default view");
			} else {
				dns_zone_attach(view->redirect, zonep);
				result = ISC_R_SUCCESS;
			}
		} else {
			result = dns_viewlist_findzone(&server->viewlist, name,
						       classtxt == NULL,
						       rdclass, zonep);
			if (result == ISC_R_NOTFOUND) {
				snprintf(problem, sizeof(problem),
					 "no matching zone '%s' in any view",
					 zonebuf);
			} else if (result == ISC_R_MULTIPLE) {
				snprintf(problem, sizeof(problem),
					 "zone '%s' was found in multiple "
					 "views",
					 zonebuf);
			}
		}
	} else {
		result = dns_viewlist_find(&server->viewlist, viewtxt, rdclass,
					   &view);
		if (result != ISC_R_SUCCESS) {
			snprintf(problem, sizeof(problem),
				 "no matching view '%s'", viewtxt);
			goto report;
		}

		if (redirect) {
			if (view->redirect != NULL) {
				dns_zone_attach(view->redirect, zonep);
				result = ISC_R_SUCCESS;
			} else {
				result = ISC_R_NOTFOUND;
			}
		} else {
			result = dns_view_findzone(view, name, DNS_ZTFIND_EXACT,
						   zonep);
		}
		if (result != ISC_R_SUCCESS) {
			snprintf(problem, sizeof(problem),
				 "no matching zone '%s' in view '%s'", zonebuf,
				 viewtxt);
		}
	}

	/* Partial match? */
	if (result != ISC_R_SUCCESS && *zonep != NULL) {
		dns_zone_detach(zonep);
	}
	if (result == DNS_R_PARTIALMATCH) {
		result = ISC_R_NOTFOUND;
	}
report:
	if (result != ISC_R_SUCCESS) {
		isc_result_t tresult;

		tresult = putstr(text, problem);
		if (tresult == ISC_R_SUCCESS) {
			(void)putnull(text);
		}
	}

cleanup:
	if (view != NULL) {
		dns_view_detach(&view);
	}

	return result;
}

/*
 * Act on a "retransfer" command from the command channel.
 */
isc_result_t
named_server_retransfercommand(named_server_t *server, isc_lex_t *lex,
			       isc_buffer_t **text) {
	isc_result_t result;
	const char *arg = NULL;
	dns_zone_t *zone = NULL;
	dns_zone_t *raw = NULL;
	dns_zonetype_t type;
	bool force = false;

	REQUIRE(text != NULL);

	/* Skip the command name. */
	(void)next_token(lex, text);

	arg = next_token(lex, text);
	if (arg != NULL && (strcmp(arg, "-force") == 0)) {
		force = true;
		arg = next_token(lex, text);
	}

	result = zone_from_args(server, lex, arg, &zone, NULL, text, false);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	if (zone == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}
	dns_zone_getraw(zone, &raw);
	if (raw != NULL) {
		dns_zone_detach(&zone);
		dns_zone_attach(raw, &zone);
		dns_zone_detach(&raw);
	}
	type = dns_zone_gettype(zone);
	if (type == dns_zone_secondary || type == dns_zone_mirror ||
	    type == dns_zone_stub ||
	    (type == dns_zone_redirect &&
	     dns_zone_getredirecttype(zone) == dns_zone_secondary))
	{
		if (force) {
			dns_zone_stopxfr(zone);
		}
		dns_zone_forcexfr(zone);
	} else {
		(void)putstr(text, "retransfer: inappropriate zone type: ");
		(void)putstr(text, dns_zonetype_name(type));
		if (type == dns_zone_redirect) {
			type = dns_zone_getredirecttype(zone);
			(void)putstr(text, "(");
			(void)putstr(text, dns_zonetype_name(type));
			(void)putstr(text, ")");
		}
		(void)putnull(text);
		result = ISC_R_FAILURE;
	}
	dns_zone_detach(&zone);
	return result;
}

/*
 * Act on a "reload" command from the command channel.
 */
isc_result_t
named_server_reloadcommand(named_server_t *server, isc_lex_t *lex,
			   isc_buffer_t **text) {
	isc_result_t result;
	dns_zone_t *zone = NULL;
	dns_zonetype_t type;
	const char *msg = NULL;

	REQUIRE(text != NULL);

	result = zone_from_args(server, lex, NULL, &zone, NULL, text, true);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	if (zone == NULL) {
		result = reload(server);
		if (result == ISC_R_SUCCESS) {
			msg = "server reload successful";
		}
	} else {
		type = dns_zone_gettype(zone);
		if (type == dns_zone_secondary || type == dns_zone_mirror ||
		    type == dns_zone_stub)
		{
			dns_zone_refresh(zone);
			dns_zone_detach(&zone);
			msg = "zone refresh queued";
		} else {
			result = dns_zone_load(zone, false);
			dns_zone_detach(&zone);
			switch (result) {
			case ISC_R_SUCCESS:
				msg = "zone reload successful";
				break;
			case DNS_R_CONTINUE:
				msg = "zone reload queued";
				result = ISC_R_SUCCESS;
				break;
			case DNS_R_UPTODATE:
				msg = "zone reload up-to-date";
				result = ISC_R_SUCCESS;
				break;
			default:
				/* failure message will be generated by rndc */
				break;
			}
		}
	}
	if (msg != NULL) {
		(void)putstr(text, msg);
		(void)putnull(text);
	}
	return result;
}

/*
 * Act on a "reset-stats" command from the command channel.
 */
isc_result_t
named_server_resetstatscommand(named_server_t *server, isc_lex_t *lex,
			       isc_buffer_t **text) {
	const char *arg = NULL;
	bool recursive_high_water = false;
	bool tcp_high_water = false;

	REQUIRE(text != NULL);

	/* Skip the command name. */
	(void)next_token(lex, text);

	arg = next_token(lex, text);
	if (arg == NULL) {
		(void)putstr(text, "reset-stats: argument expected");
		(void)putnull(text);
		return ISC_R_UNEXPECTEDEND;
	}
	while (arg != NULL) {
		if (strcmp(arg, "recursive-high-water") == 0) {
			recursive_high_water = true;
		} else if (strcmp(arg, "tcp-high-water") == 0) {
			tcp_high_water = true;
		} else {
			(void)putstr(text, "reset-stats: "
					   "unrecognized argument: ");
			(void)putstr(text, arg);
			(void)putnull(text);
			return ISC_R_FAILURE;
		}
		arg = next_token(lex, text);
	}

	if (recursive_high_water) {
		isc_stats_set(ns_stats_get(server->sctx->nsstats), 0,
			      ns_statscounter_recurshighwater);
	}
	if (tcp_high_water) {
		isc_stats_set(ns_stats_get(server->sctx->nsstats), 0,
			      ns_statscounter_tcphighwater);
	}

	return ISC_R_SUCCESS;
}

/*
 * Act on a "reconfig" command from the command channel.
 */
isc_result_t
named_server_reconfigcommand(named_server_t *server) {
	isc_result_t result;
	atomic_store(&server->reload_status, NAMED_RELOAD_IN_PROGRESS);

	named_os_notify_systemd("RELOADING=1\n"
				"MONOTONIC_USEC=%" PRIu64 "\n"
				"STATUS=reconfig command received\n",
				(uint64_t)isc_time_monotonic() / NS_PER_US);

	CHECK(loadconfig(server));

	result = load_zones(server, true);
	if (result == ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
			      "scheduled loading new zones");
	} else {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "loading new zones failed: %s",
			      isc_result_totext(result));
		atomic_store(&server->reload_status, NAMED_RELOAD_FAILED);
	}
cleanup:
	named_os_notify_systemd("READY=1\n"
				"STATUS=reconfig command finished: %s\n",
				isc_result_totext(result));

	return result;
}

/*
 * Act on a "notify" command from the command channel.
 */
isc_result_t
named_server_notifycommand(named_server_t *server, isc_lex_t *lex,
			   isc_buffer_t **text) {
	isc_result_t result;
	dns_zone_t *zone = NULL;
	const char msg[] = "zone notify queued";

	REQUIRE(text != NULL);

	result = zone_from_args(server, lex, NULL, &zone, NULL, text, true);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	if (zone == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	dns_zone_notify(zone, true);
	dns_zone_detach(&zone);
	(void)putstr(text, msg);
	(void)putnull(text);

	return ISC_R_SUCCESS;
}

/*
 * Act on a "refresh" command from the command channel.
 */
isc_result_t
named_server_refreshcommand(named_server_t *server, isc_lex_t *lex,
			    isc_buffer_t **text) {
	isc_result_t result;
	dns_zone_t *zone = NULL, *raw = NULL;
	const char msg1[] = "zone refresh queued";
	const char msg2[] = "not a secondary, mirror, or stub zone";
	dns_zonetype_t type;

	REQUIRE(text != NULL);

	result = zone_from_args(server, lex, NULL, &zone, NULL, text, true);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	if (zone == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	dns_zone_getraw(zone, &raw);
	if (raw != NULL) {
		dns_zone_detach(&zone);
		dns_zone_attach(raw, &zone);
		dns_zone_detach(&raw);
	}

	type = dns_zone_gettype(zone);
	if (type == dns_zone_secondary || type == dns_zone_mirror ||
	    type == dns_zone_stub)
	{
		dns_zone_refresh(zone);
		dns_zone_detach(&zone);
		(void)putstr(text, msg1);
		(void)putnull(text);
		return ISC_R_SUCCESS;
	}

	dns_zone_detach(&zone);
	(void)putstr(text, msg2);
	(void)putnull(text);
	return ISC_R_FAILURE;
}

isc_result_t
named_server_setortoggle(named_server_t *server, const char *optname,
			 unsigned int option, isc_lex_t *lex) {
	bool prev, value;
	char *ptr = NULL;

	/* Skip the command name. */
	ptr = next_token(lex, NULL);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	prev = ns_server_getoption(server->sctx, option);

	ptr = next_token(lex, NULL);
	if (ptr == NULL) {
		value = !prev;
	} else if (!strcasecmp(ptr, "on") || !strcasecmp(ptr, "yes") ||
		   !strcasecmp(ptr, "enable") || !strcasecmp(ptr, "true"))
	{
		value = true;
	} else if (!strcasecmp(ptr, "off") || !strcasecmp(ptr, "no") ||
		   !strcasecmp(ptr, "disable") || !strcasecmp(ptr, "false"))
	{
		value = false;
	} else {
		return DNS_R_SYNTAX;
	}

	if (value == prev) {
		return ISC_R_SUCCESS;
	}

	ns_server_setoption(server->sctx, option, value);

	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO, "%s is now %s",
		      optname, value ? "on" : "off");
	return ISC_R_SUCCESS;
}

static isc_result_t
listenlist_fromconfig(const cfg_obj_t *listenlist, const cfg_obj_t *config,
		      cfg_aclconfctx_t *actx, isc_mem_t *mctx, uint16_t family,
		      isc_tlsctx_cache_t *tlsctx_cache,
		      ns_listenlist_t **target) {
	isc_result_t result;
	const cfg_listelt_t *element;
	ns_listenlist_t *dlist = NULL;

	REQUIRE(target != NULL && *target == NULL);

	result = ns_listenlist_create(mctx, &dlist);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	for (element = cfg_list_first(listenlist); element != NULL;
	     element = cfg_list_next(element))
	{
		ns_listenelt_t *delt = NULL;
		const cfg_obj_t *listener = cfg_listelt_value(element);
		result = listenelt_fromconfig(listener, config, actx, mctx,
					      family, tlsctx_cache, &delt);
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}
		ISC_LIST_APPEND(dlist->elts, delt, link);
	}
	*target = dlist;
	return ISC_R_SUCCESS;

cleanup:
	ns_listenlist_detach(&dlist);
	return result;
}

static const cfg_obj_t *
find_maplist(const cfg_obj_t *config, const char *listname, const char *name) {
	isc_result_t result;
	const cfg_obj_t *maplist = NULL;
	const cfg_listelt_t *elt = NULL;

	REQUIRE(config != NULL);
	REQUIRE(name != NULL);

	result = cfg_map_get(config, listname, &maplist);
	if (result != ISC_R_SUCCESS) {
		return NULL;
	}

	for (elt = cfg_list_first(maplist); elt != NULL;
	     elt = cfg_list_next(elt))
	{
		const cfg_obj_t *map = cfg_listelt_value(elt);
		if (strcasecmp(cfg_obj_asstring(cfg_map_getname(map)), name) ==
		    0)
		{
			return map;
		}
	}

	return NULL;
}

/*
 * Create a listen list from the corresponding configuration
 * data structure.
 */
static isc_result_t
listenelt_fromconfig(const cfg_obj_t *listener, const cfg_obj_t *config,
		     cfg_aclconfctx_t *actx, isc_mem_t *mctx, uint16_t family,
		     isc_tlsctx_cache_t *tlsctx_cache,
		     ns_listenelt_t **target) {
	isc_result_t result;
	const cfg_obj_t *ltup = NULL;
	const cfg_obj_t *tlsobj = NULL, *httpobj = NULL;
	const cfg_obj_t *portobj = NULL;
	const cfg_obj_t *http_server = NULL;
	const cfg_obj_t *proxyobj = NULL;
	in_port_t port = 0;
	const char *key = NULL, *cert = NULL, *ca_file = NULL,
		   *dhparam_file = NULL, *ciphers = NULL, *cipher_suites = NULL;
	bool tls_prefer_server_ciphers = false,
	     tls_prefer_server_ciphers_set = false;
	bool tls_session_tickets = false, tls_session_tickets_set = false;
	bool do_tls = false, no_tls = false, http = false;
	ns_listenelt_t *delt = NULL;
	uint32_t tls_protos = 0;
	ns_listen_tls_params_t tls_params = { 0 };
	const char *tlsname = NULL;
	isc_nm_proxy_type_t proxy = ISC_NM_PROXY_NONE;

	REQUIRE(target != NULL && *target == NULL);

	ltup = cfg_tuple_get(listener, "tuple");
	RUNTIME_CHECK(ltup != NULL);

	tlsobj = cfg_tuple_get(ltup, "tls");
	if (tlsobj != NULL && cfg_obj_isstring(tlsobj)) {
		tlsname = cfg_obj_asstring(tlsobj);

		if (strcasecmp(tlsname, "none") == 0) {
			no_tls = true;
		} else if (strcasecmp(tlsname, "ephemeral") == 0) {
			do_tls = true;
		} else {
			const cfg_obj_t *keyobj = NULL, *certobj = NULL,
					*ca_obj = NULL, *dhparam_obj = NULL;
			const cfg_obj_t *tlsmap = NULL;
			const cfg_obj_t *tls_proto_list = NULL;
			const cfg_obj_t *ciphers_obj = NULL;
			const cfg_obj_t *cipher_suites_obj = NULL;
			const cfg_obj_t *prefer_server_ciphers_obj = NULL;
			const cfg_obj_t *session_tickets_obj = NULL;

			do_tls = true;

			tlsmap = find_maplist(config, "tls", tlsname);
			if (tlsmap == NULL) {
				cfg_obj_log(tlsobj, named_g_lctx, ISC_LOG_ERROR,
					    "tls '%s' is not defined",
					    cfg_obj_asstring(tlsobj));
				return ISC_R_FAILURE;
			}

			CHECK(cfg_map_get(tlsmap, "key-file", &keyobj));
			key = cfg_obj_asstring(keyobj);

			CHECK(cfg_map_get(tlsmap, "cert-file", &certobj));
			cert = cfg_obj_asstring(certobj);

			if (cfg_map_get(tlsmap, "ca-file", &ca_obj) ==
			    ISC_R_SUCCESS)
			{
				ca_file = cfg_obj_asstring(ca_obj);
			}

			if (cfg_map_get(tlsmap, "protocols", &tls_proto_list) ==
			    ISC_R_SUCCESS)
			{
				const cfg_listelt_t *proto = NULL;
				INSIST(tls_proto_list != NULL);
				for (proto = cfg_list_first(tls_proto_list);
				     proto != 0; proto = cfg_list_next(proto))
				{
					const cfg_obj_t *tls_proto_obj =
						cfg_listelt_value(proto);
					const char *tls_sver =
						cfg_obj_asstring(tls_proto_obj);
					const isc_tls_protocol_version_t ver =
						isc_tls_protocol_name_to_version(
							tls_sver);

					INSIST(ver !=
					       ISC_TLS_PROTO_VER_UNDEFINED);
					INSIST(isc_tls_protocol_supported(ver));
					tls_protos |= ver;
				}
			}

			if (cfg_map_get(tlsmap, "dhparam-file", &dhparam_obj) ==
			    ISC_R_SUCCESS)
			{
				dhparam_file = cfg_obj_asstring(dhparam_obj);
			}

			if (cfg_map_get(tlsmap, "ciphers", &ciphers_obj) ==
			    ISC_R_SUCCESS)
			{
				ciphers = cfg_obj_asstring(ciphers_obj);
			}

			if (cfg_map_get(tlsmap, "cipher-suites",
					&cipher_suites_obj) == ISC_R_SUCCESS)
			{
				cipher_suites =
					cfg_obj_asstring(cipher_suites_obj);
			}

			if (cfg_map_get(tlsmap, "prefer-server-ciphers",
					&prefer_server_ciphers_obj) ==
			    ISC_R_SUCCESS)
			{
				tls_prefer_server_ciphers = cfg_obj_asboolean(
					prefer_server_ciphers_obj);
				tls_prefer_server_ciphers_set = true;
			}

			if (cfg_map_get(tlsmap, "session-tickets",
					&session_tickets_obj) == ISC_R_SUCCESS)
			{
				tls_session_tickets =
					cfg_obj_asboolean(session_tickets_obj);
				tls_session_tickets_set = true;
			}
		}
	}

	tls_params = (ns_listen_tls_params_t){
		.name = tlsname,
		.key = key,
		.cert = cert,
		.ca_file = ca_file,
		.protocols = tls_protos,
		.dhparam_file = dhparam_file,
		.ciphers = ciphers,
		.cipher_suites = cipher_suites,
		.prefer_server_ciphers = tls_prefer_server_ciphers,
		.prefer_server_ciphers_set = tls_prefer_server_ciphers_set,
		.session_tickets = tls_session_tickets,
		.session_tickets_set = tls_session_tickets_set
	};

	httpobj = cfg_tuple_get(ltup, "http");
	if (httpobj != NULL && cfg_obj_isstring(httpobj)) {
		const char *httpname = cfg_obj_asstring(httpobj);

		if (!do_tls && !no_tls) {
			return ISC_R_FAILURE;
		}

		http_server = find_maplist(config, "http", httpname);
		if (http_server == NULL && strcasecmp(httpname, "default") != 0)
		{
			cfg_obj_log(httpobj, named_g_lctx, ISC_LOG_ERROR,
				    "http '%s' is not defined",
				    cfg_obj_asstring(httpobj));
			return ISC_R_FAILURE;
		}

		http = true;
	}

	portobj = cfg_tuple_get(ltup, "port");
	if (!cfg_obj_isuint32(portobj)) {
		if (http && do_tls) {
			if (named_g_httpsport != 0) {
				port = named_g_httpsport;
			} else {
				result = named_config_getport(
					config, "https-port", &port);
				if (result != ISC_R_SUCCESS) {
					return result;
				}
			}
		} else if (http && !do_tls) {
			if (named_g_httpport != 0) {
				port = named_g_httpport;
			} else {
				result = named_config_getport(
					config, "http-port", &port);
				if (result != ISC_R_SUCCESS) {
					return result;
				}
			}
		} else if (do_tls) {
			if (named_g_tlsport != 0) {
				port = named_g_tlsport;
			} else {
				result = named_config_getport(
					config, "tls-port", &port);
				if (result != ISC_R_SUCCESS) {
					return result;
				}
			}
		} else {
			if (named_g_port != 0) {
				port = named_g_port;
			} else {
				result = named_config_getport(config, "port",
							      &port);
				if (result != ISC_R_SUCCESS) {
					return result;
				}
			}
		}
	} else {
		if (cfg_obj_asuint32(portobj) >= UINT16_MAX) {
			return ISC_R_RANGE;
		}
		port = (in_port_t)cfg_obj_asuint32(portobj);
	}

	proxyobj = cfg_tuple_get(ltup, "proxy");
	if (proxyobj != NULL && cfg_obj_isstring(proxyobj)) {
		const char *proxyval = cfg_obj_asstring(proxyobj);

		if (strcasecmp(proxyval, "encrypted") == 0) {
			INSIST(do_tls == true);
			proxy = ISC_NM_PROXY_ENCRYPTED;
		} else if (strcasecmp(proxyval, "plain") == 0) {
			proxy = ISC_NM_PROXY_PLAIN;
		} else {
			UNREACHABLE();
		}
	}

#ifdef HAVE_LIBNGHTTP2
	if (http) {
		CHECK(listenelt_http(http_server, family, do_tls, &tls_params,
				     tlsctx_cache, port, mctx, proxy, &delt));
	}
#endif /* HAVE_LIBNGHTTP2 */

	if (!http) {
		CHECK(ns_listenelt_create(mctx, port, NULL, family, do_tls,
					  &tls_params, tlsctx_cache, proxy,
					  &delt));
	}

	result = cfg_acl_fromconfig(cfg_tuple_get(listener, "acl"), config,
				    named_g_lctx, actx, mctx, family,
				    &delt->acl);
	if (result != ISC_R_SUCCESS) {
		ns_listenelt_destroy(delt);
		return result;
	}
	*target = delt;

cleanup:
	return result;
}

#ifdef HAVE_LIBNGHTTP2
static isc_result_t
listenelt_http(const cfg_obj_t *http, const uint16_t family, bool tls,
	       const ns_listen_tls_params_t *tls_params,
	       isc_tlsctx_cache_t *tlsctx_cache, in_port_t port,
	       isc_mem_t *mctx, isc_nm_proxy_type_t proxy,
	       ns_listenelt_t **target) {
	isc_result_t result = ISC_R_SUCCESS;
	ns_listenelt_t *delt = NULL;
	char **endpoints = NULL;
	const cfg_obj_t *eplist = NULL;
	const cfg_listelt_t *elt = NULL;
	size_t len = 1, i = 0;
	uint32_t max_clients = named_g_http_listener_clients;
	uint32_t max_streams = named_g_http_streams_per_conn;

	REQUIRE(target != NULL && *target == NULL);

	if (tls) {
		INSIST(tls_params != NULL);
		INSIST((tls_params->key == NULL) == (tls_params->cert == NULL));
	}

	if (port == 0) {
		port = tls ? named_g_httpsport : named_g_httpport;
	}

	/*
	 * If "default" was used, we set up the default endpoint
	 * of "/dns-query".
	 */
	if (http != NULL) {
		const cfg_obj_t *cfg_max_clients = NULL;
		const cfg_obj_t *cfg_max_streams = NULL;

		if (cfg_map_get(http, "endpoints", &eplist) == ISC_R_SUCCESS) {
			INSIST(eplist != NULL);
			len = cfg_list_length(eplist, false);
		}

		if (cfg_map_get(http, "listener-clients", &cfg_max_clients) ==
		    ISC_R_SUCCESS)
		{
			INSIST(cfg_max_clients != NULL);
			max_clients = cfg_obj_asuint32(cfg_max_clients);
		}

		if (cfg_map_get(http, "streams-per-connection",
				&cfg_max_streams) == ISC_R_SUCCESS)
		{
			INSIST(cfg_max_streams != NULL);
			max_streams = cfg_obj_asuint32(cfg_max_streams);
		}
	}

	endpoints = isc_mem_allocate(mctx, sizeof(endpoints[0]) * len);

	if (http != NULL && eplist != NULL) {
		for (elt = cfg_list_first(eplist); elt != NULL;
		     elt = cfg_list_next(elt))
		{
			const cfg_obj_t *ep = cfg_listelt_value(elt);
			const char *path = cfg_obj_asstring(ep);
			endpoints[i++] = isc_mem_strdup(mctx, path);
		}
	} else {
		endpoints[i++] = isc_mem_strdup(mctx, ISC_NM_HTTP_DEFAULT_PATH);
	}

	INSIST(i == len);

	result = ns_listenelt_create_http(
		mctx, port, NULL, family, tls, tls_params, tlsctx_cache, proxy,
		endpoints, len, max_clients, max_streams, &delt);
	if (result != ISC_R_SUCCESS) {
		goto error;
	}

	*target = delt;

	return result;
error:
	if (delt != NULL) {
		ns_listenelt_destroy(delt);
	}
	return result;
}
#endif /* HAVE_LIBNGHTTP2 */

isc_result_t
named_server_dumpstats(named_server_t *server) {
	isc_result_t result;
	FILE *fp = NULL;

	CHECKMF(isc_stdio_open(server->statsfile, "a", &fp),
		"could not open statistics dump file", server->statsfile);

	result = named_stats_dump(server, fp);

cleanup:
	if (fp != NULL) {
		(void)isc_stdio_close(fp);
	}
	if (result == ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
			      "dumpstats complete");
	} else {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "dumpstats failed: %s",
			      isc_result_totext(result));
	}
	return result;
}

static isc_result_t
add_zone_tolist(dns_zone_t *zone, void *uap) {
	struct dumpcontext *dctx = uap;
	struct zonelistentry *zle;

	zle = isc_mem_get(dctx->mctx, sizeof *zle);
	zle->zone = NULL;
	dns_zone_attach(zone, &zle->zone);
	ISC_LINK_INIT(zle, link);
	ISC_LIST_APPEND(ISC_LIST_TAIL(dctx->viewlist)->zonelist, zle, link);
	return ISC_R_SUCCESS;
}

static isc_result_t
add_view_tolist(struct dumpcontext *dctx, dns_view_t *view) {
	struct viewlistentry *vle;
	isc_result_t result = ISC_R_SUCCESS;

	/*
	 * Prevent duplicate views.
	 */
	for (vle = ISC_LIST_HEAD(dctx->viewlist); vle != NULL;
	     vle = ISC_LIST_NEXT(vle, link))
	{
		if (vle->view == view) {
			return ISC_R_SUCCESS;
		}
	}

	vle = isc_mem_get(dctx->mctx, sizeof *vle);
	vle->view = NULL;
	dns_view_attach(view, &vle->view);
	ISC_LINK_INIT(vle, link);
	ISC_LIST_INIT(vle->zonelist);
	ISC_LIST_APPEND(dctx->viewlist, vle, link);
	if (dctx->dumpzones) {
		result = dns_view_apply(view, true, NULL, add_zone_tolist,
					dctx);
	}
	return result;
}

static void
dumpcontext_destroy(struct dumpcontext *dctx) {
	struct viewlistentry *vle;
	struct zonelistentry *zle;

	vle = ISC_LIST_HEAD(dctx->viewlist);
	while (vle != NULL) {
		ISC_LIST_UNLINK(dctx->viewlist, vle, link);
		zle = ISC_LIST_HEAD(vle->zonelist);
		while (zle != NULL) {
			ISC_LIST_UNLINK(vle->zonelist, zle, link);
			dns_zone_detach(&zle->zone);
			isc_mem_put(dctx->mctx, zle, sizeof *zle);
			zle = ISC_LIST_HEAD(vle->zonelist);
		}
		dns_view_detach(&vle->view);
		isc_mem_put(dctx->mctx, vle, sizeof *vle);
		vle = ISC_LIST_HEAD(dctx->viewlist);
	}
	if (dctx->version != NULL) {
		dns_db_closeversion(dctx->db, &dctx->version, false);
	}
	if (dctx->db != NULL) {
		dns_db_detach(&dctx->db);
	}
	if (dctx->cache != NULL) {
		dns_db_detach(&dctx->cache);
	}
	if (dctx->fp != NULL) {
		(void)isc_stdio_close(dctx->fp);
	}
	if (dctx->mdctx != NULL) {
		dns_dumpctx_detach(&dctx->mdctx);
	}
	isc_mem_put(dctx->mctx, dctx, sizeof *dctx);
}

static void
dumpdone(void *arg, isc_result_t result) {
	struct dumpcontext *dctx = arg;
	char buf[1024 + 32];
	const dns_master_style_t *style;

	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}
	if (dctx->mdctx != NULL) {
		dns_dumpctx_detach(&dctx->mdctx);
	}
	if (dctx->view == NULL) {
		dctx->view = ISC_LIST_HEAD(dctx->viewlist);
		if (dctx->view == NULL) {
			goto done;
		}
		INSIST(dctx->zone == NULL);
	} else {
		goto resume;
	}
nextview:
	fprintf(dctx->fp, ";\n; Start view %s\n;\n", dctx->view->view->name);
resume:
	if (dctx->dumpcache && dns_view_iscacheshared(dctx->view->view)) {
		fprintf(dctx->fp, ";\n; Cache of view '%s' is shared as '%s'\n",
			dctx->view->view->name,
			dns_cache_getname(dctx->view->view->cache));
	} else if (dctx->zone == NULL && dctx->cache == NULL && dctx->dumpcache)
	{
		if (dctx->dumpexpired) {
			style = &dns_master_style_cache_with_expired;
		} else {
			style = &dns_master_style_cache;
		}
		/* start cache dump */
		if (dctx->view->view->cachedb != NULL) {
			dns_db_attach(dctx->view->view->cachedb, &dctx->cache);
		}
		if (dctx->cache != NULL) {
			fprintf(dctx->fp,
				";\n; Cache dump of view '%s' (cache %s)\n;\n",
				dctx->view->view->name,
				dns_cache_getname(dctx->view->view->cache));
			result = dns_master_dumptostreamasync(
				dctx->mctx, dctx->cache, NULL, style, dctx->fp,
				named_g_mainloop, dumpdone, dctx, &dctx->mdctx);
			if (result == ISC_R_SUCCESS) {
				return;
			}
			if (result == ISC_R_NOTIMPLEMENTED) {
				fprintf(dctx->fp, "; %s\n",
					isc_result_totext(result));
			} else if (result != ISC_R_SUCCESS) {
				goto cleanup;
			}
		}
	}

	if ((dctx->dumpadb || dctx->dumpfail) && dctx->cache == NULL &&
	    dctx->view->view->cachedb != NULL)
	{
		dns_db_attach(dctx->view->view->cachedb, &dctx->cache);
	}

	if (dctx->cache != NULL) {
		if (dctx->dumpadb) {
			dns_adb_t *adb = NULL;
			dns_view_getadb(dctx->view->view, &adb);
			if (adb != NULL) {
				dns_adb_dump(adb, dctx->fp);
				dns_adb_detach(&adb);
			}
		}
		if (dctx->dumpfail) {
			dns_badcache_print(dctx->view->view->failcache,
					   "SERVFAIL cache", dctx->fp);
		}
		dns_db_detach(&dctx->cache);
	}
	if (dctx->dumpzones) {
		style = &dns_master_style_full;
	nextzone:
		if (dctx->version != NULL) {
			dns_db_closeversion(dctx->db, &dctx->version, false);
		}
		if (dctx->db != NULL) {
			dns_db_detach(&dctx->db);
		}
		if (dctx->zone == NULL) {
			dctx->zone = ISC_LIST_HEAD(dctx->view->zonelist);
		} else {
			dctx->zone = ISC_LIST_NEXT(dctx->zone, link);
		}
		if (dctx->zone != NULL) {
			/* start zone dump */
			dns_zone_name(dctx->zone->zone, buf, sizeof(buf));
			fprintf(dctx->fp, ";\n; Zone dump of '%s'\n;\n", buf);
			result = dns_zone_getdb(dctx->zone->zone, &dctx->db);
			if (result != ISC_R_SUCCESS) {
				fprintf(dctx->fp, "; %s\n",
					isc_result_totext(result));
				goto nextzone;
			}
			dns_db_currentversion(dctx->db, &dctx->version);
			result = dns_master_dumptostreamasync(
				dctx->mctx, dctx->db, dctx->version, style,
				dctx->fp, dns_zone_getloop(dctx->zone->zone),
				dumpdone, dctx, &dctx->mdctx);
			if (result == ISC_R_SUCCESS) {
				return;
			}
			if (result == ISC_R_NOTIMPLEMENTED) {
				fprintf(dctx->fp, "; %s\n",
					isc_result_totext(result));
				result = ISC_R_SUCCESS;
				POST(result);
				goto nextzone;
			}
			if (result != ISC_R_SUCCESS) {
				goto cleanup;
			}
		}
	}
	if (dctx->view != NULL) {
		dctx->view = ISC_LIST_NEXT(dctx->view, link);
		if (dctx->view != NULL) {
			goto nextview;
		}
	}
done:
	fprintf(dctx->fp, "; Dump complete\n");
	result = isc_stdio_flush(dctx->fp);
	if (result == ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
			      "dumpdb complete");
	}
cleanup:
	if (result != ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "dumpdb failed: %s", isc_result_totext(result));
	}
	dumpcontext_destroy(dctx);
}

isc_result_t
named_server_dumpdb(named_server_t *server, isc_lex_t *lex,
		    isc_buffer_t **text) {
	struct dumpcontext *dctx = NULL;
	dns_view_t *view;
	isc_result_t result;
	char *ptr;
	const char *sep;
	bool found;

	REQUIRE(text != NULL);

	/* Skip the command name. */
	ptr = next_token(lex, NULL);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	dctx = isc_mem_get(server->mctx, sizeof(*dctx));
	*dctx = (struct dumpcontext){
		.mctx = server->mctx,
		.dumpcache = true,
		.dumpadb = true,
		.dumpfail = true,
		.viewlist = ISC_LIST_INITIALIZER,
	};

	CHECKMF(isc_stdio_open(server->dumpfile, "w", &dctx->fp),
		"could not open dump file", server->dumpfile);

	ptr = next_token(lex, NULL);
	sep = (ptr == NULL) ? "" : ": ";
	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		      "dumpdb started%s%s", sep, (ptr != NULL) ? ptr : "");

	if (ptr != NULL && strcmp(ptr, "-all") == 0) {
		/* also dump zones */
		dctx->dumpzones = true;
		ptr = next_token(lex, NULL);
	} else if (ptr != NULL && strcmp(ptr, "-cache") == 0) {
		/* this is the default */
		ptr = next_token(lex, NULL);
	} else if (ptr != NULL && strcmp(ptr, "-expired") == 0) {
		/* this is the same as -cache but includes expired data */
		dctx->dumpexpired = true;
		ptr = next_token(lex, NULL);
	} else if (ptr != NULL && strcmp(ptr, "-zones") == 0) {
		/* only dump zones, suppress caches */
		dctx->dumpadb = false;
		dctx->dumpcache = false;
		dctx->dumpfail = false;
		dctx->dumpzones = true;
		ptr = next_token(lex, NULL);
	} else if (ptr != NULL && strcmp(ptr, "-adb") == 0) {
		/* only dump adb, suppress other caches */
		dctx->dumpcache = false;
		dctx->dumpfail = false;
		ptr = next_token(lex, NULL);
	} else if (ptr != NULL && strcmp(ptr, "-bad") == 0) {
		/* only dump badcache, suppress other caches */
		dctx->dumpadb = false;
		dctx->dumpcache = false;
		dctx->dumpfail = false;
		ptr = next_token(lex, NULL);
	} else if (ptr != NULL && strcmp(ptr, "-fail") == 0) {
		/* only dump servfail cache, suppress other caches */
		dctx->dumpadb = false;
		dctx->dumpcache = false;
		ptr = next_token(lex, NULL);
	}

nextview:
	found = false;
	for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
	     view = ISC_LIST_NEXT(view, link))
	{
		if (ptr != NULL && strcmp(view->name, ptr) != 0) {
			continue;
		}
		found = true;
		CHECK(add_view_tolist(dctx, view));
	}
	if (ptr != NULL) {
		if (!found) {
			CHECK(putstr(text, "view '"));
			CHECK(putstr(text, ptr));
			CHECK(putstr(text, "' not found"));
			CHECK(putnull(text));
			result = ISC_R_NOTFOUND;
			dumpdone(dctx, result);
			return result;
		}
		ptr = next_token(lex, NULL);
		if (ptr != NULL) {
			goto nextview;
		}
	}
	dumpdone(dctx, ISC_R_SUCCESS);
	return ISC_R_SUCCESS;

cleanup:
	dumpcontext_destroy(dctx);
	return result;
}

isc_result_t
named_server_dumpsecroots(named_server_t *server, isc_lex_t *lex,
			  isc_buffer_t **text) {
	dns_view_t *view;
	dns_keytable_t *secroots = NULL;
	dns_ntatable_t *ntatable = NULL;
	isc_result_t result;
	char *ptr;
	FILE *fp = NULL;
	isc_time_t now;
	char tbuf[64];
	unsigned int used = isc_buffer_usedlength(*text);
	bool first = true;

	REQUIRE(text != NULL);

	/* Skip the command name. */
	ptr = next_token(lex, text);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	/* "-" here means print the output instead of dumping to file */
	ptr = next_token(lex, text);
	if (ptr != NULL && strcmp(ptr, "-") == 0) {
		ptr = next_token(lex, text);
	} else {
		result = isc_stdio_open(server->secrootsfile, "w", &fp);
		if (result != ISC_R_SUCCESS) {
			(void)putstr(text, "could not open ");
			(void)putstr(text, server->secrootsfile);
			CHECKMF(result, "could not open secroots dump file",
				server->secrootsfile);
		}
	}

	now = isc_time_now();
	isc_time_formattimestamp(&now, tbuf, sizeof(tbuf));
	CHECK(putstr(text, "secure roots as of "));
	CHECK(putstr(text, tbuf));
	CHECK(putstr(text, ":\n"));
	used = isc_buffer_usedlength(*text);

	do {
		for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
		     view = ISC_LIST_NEXT(view, link))
		{
			if (ptr != NULL && strcmp(view->name, ptr) != 0) {
				continue;
			}
			if (secroots != NULL) {
				dns_keytable_detach(&secroots);
			}
			result = dns_view_getsecroots(view, &secroots);
			if (result == ISC_R_NOTFOUND) {
				result = ISC_R_SUCCESS;
				continue;
			}
			if (first || used != isc_buffer_usedlength(*text)) {
				CHECK(putstr(text, "\n"));
				first = false;
			}
			CHECK(putstr(text, " Start view "));
			CHECK(putstr(text, view->name));
			CHECK(putstr(text, "\n   Secure roots:\n\n"));
			used = isc_buffer_usedlength(*text);
			CHECK(dns_keytable_totext(secroots, text));

			if (ntatable != NULL) {
				dns_ntatable_detach(&ntatable);
			}
			result = dns_view_getntatable(view, &ntatable);
			if (result == ISC_R_NOTFOUND) {
				result = ISC_R_SUCCESS;
				continue;
			}
			if (used != isc_buffer_usedlength(*text)) {
				CHECK(putstr(text, "\n"));
			}
			CHECK(putstr(text, "   Negative trust anchors:\n\n"));
			used = isc_buffer_usedlength(*text);
			CHECK(dns_ntatable_totext(ntatable, NULL, text));
		}

		if (ptr != NULL) {
			ptr = next_token(lex, text);
		}
	} while (ptr != NULL);

cleanup:
	if (secroots != NULL) {
		dns_keytable_detach(&secroots);
	}
	if (ntatable != NULL) {
		dns_ntatable_detach(&ntatable);
	}

	if (fp != NULL) {
		if (used != isc_buffer_usedlength(*text)) {
			(void)putstr(text, "\n");
		}
		fprintf(fp, "%.*s", (int)isc_buffer_usedlength(*text),
			(char *)isc_buffer_base(*text));
		isc_buffer_clear(*text);
		(void)isc_stdio_close(fp);
	} else if (isc_buffer_usedlength(*text) > 0) {
		(void)putnull(text);
	}

	if (result == ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
			      "dumpsecroots complete");
	} else {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "dumpsecroots failed: %s",
			      isc_result_totext(result));
	}
	return result;
}

isc_result_t
named_server_dumprecursing(named_server_t *server) {
	FILE *fp = NULL;
	dns_view_t *view;
	isc_result_t result;

	CHECKMF(isc_stdio_open(server->recfile, "w", &fp),
		"could not open dump file", server->recfile);
	fprintf(fp, ";\n; Recursing Queries\n;\n");
	ns_interfacemgr_dumprecursing(fp, server->interfacemgr);

	for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
	     view = ISC_LIST_NEXT(view, link))
	{
		fprintf(fp, ";\n; Active fetch domains [view: %s]\n;\n",
			view->name);
		dns_resolver_dumpfetches(view->resolver, isc_statsformat_file,
					 fp);
	}

	fprintf(fp, "; Dump complete\n");

cleanup:
	if (fp != NULL) {
		result = isc_stdio_close(fp);
	}
	if (result == ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
			      "dumprecursing complete");
	} else {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "dumprecursing failed: %s",
			      isc_result_totext(result));
	}
	return result;
}

isc_result_t
named_server_setdebuglevel(named_server_t *server, isc_lex_t *lex) {
	char *ptr;
	char *endp;
	long newlevel;

	UNUSED(server);

	/* Skip the command name. */
	ptr = next_token(lex, NULL);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	/* Look for the new level name. */
	ptr = next_token(lex, NULL);
	if (ptr == NULL) {
		if (named_g_debuglevel < 99) {
			named_g_debuglevel++;
		}
	} else {
		newlevel = strtol(ptr, &endp, 10);
		if (*endp != '\0' || newlevel < 0 || newlevel > 99) {
			return ISC_R_RANGE;
		}
		named_g_debuglevel = (unsigned int)newlevel;
	}
	isc_log_setdebuglevel(named_g_lctx, named_g_debuglevel);
	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		      "debug level is now %u", named_g_debuglevel);
	return ISC_R_SUCCESS;
}

isc_result_t
named_server_validation(named_server_t *server, isc_lex_t *lex,
			isc_buffer_t **text) {
	char *ptr;
	dns_view_t *view;
	bool changed = false;
	isc_result_t result;
	bool enable = true, set = true, first = true;

	REQUIRE(text != NULL);

	/* Skip the command name. */
	ptr = next_token(lex, text);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	/* Find out what we are to do. */
	ptr = next_token(lex, text);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	if (!strcasecmp(ptr, "on") || !strcasecmp(ptr, "yes") ||
	    !strcasecmp(ptr, "enable") || !strcasecmp(ptr, "true"))
	{
		enable = true;
	} else if (!strcasecmp(ptr, "off") || !strcasecmp(ptr, "no") ||
		   !strcasecmp(ptr, "disable") || !strcasecmp(ptr, "false"))
	{
		enable = false;
	} else if (!strcasecmp(ptr, "check") || !strcasecmp(ptr, "status")) {
		set = false;
	} else {
		return DNS_R_SYNTAX;
	}

	/* Look for the view name. */
	ptr = next_token(lex, text);

	isc_loopmgr_pause(named_g_loopmgr);
	for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
	     view = ISC_LIST_NEXT(view, link))
	{
		if ((ptr != NULL && strcasecmp(ptr, view->name) != 0) ||
		    strcasecmp("_bind", view->name) == 0)
		{
			continue;
		}

		if (set) {
			CHECK(dns_view_flushcache(view, false));
			view->enablevalidation = enable;
			changed = true;
		} else {
			if (!first) {
				CHECK(putstr(text, "\n"));
			}
			CHECK(putstr(text, "DNSSEC validation is "));
			CHECK(putstr(text, view->enablevalidation
						   ? "enabled"
						   : "disabled"));
			CHECK(putstr(text, " (view "));
			CHECK(putstr(text, view->name));
			CHECK(putstr(text, ")"));
			first = false;
		}
	}
	CHECK(putnull(text));

	if (!set) {
		result = ISC_R_SUCCESS;
	} else if (changed) {
		result = ISC_R_SUCCESS;
	} else {
		result = ISC_R_FAILURE;
	}
cleanup:
	isc_loopmgr_resume(named_g_loopmgr);
	return result;
}

isc_result_t
named_server_flushcache(named_server_t *server, isc_lex_t *lex) {
	char *ptr;
	dns_view_t *view;
	bool flushed;
	bool found;
	isc_result_t result;
	named_cache_t *nsc;

	/* Skip the command name. */
	ptr = next_token(lex, NULL);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	/* Look for the view name. */
	ptr = next_token(lex, NULL);

	isc_loopmgr_pause(named_g_loopmgr);
	flushed = true;
	found = false;

	/*
	 * Flushing a cache is tricky when caches are shared by multiple views.
	 * We first identify which caches should be flushed in the local cache
	 * list, flush these caches, and then update other views that refer to
	 * the flushed cache DB.
	 */
	if (ptr != NULL) {
		/*
		 * Mark caches that need to be flushed.  This is an O(#view^2)
		 * operation in the very worst case, but should be normally
		 * much more lightweight because only a few (most typically just
		 * one) views will match.
		 */
		for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
		     view = ISC_LIST_NEXT(view, link))
		{
			if (strcasecmp(ptr, view->name) != 0) {
				continue;
			}
			found = true;
			for (nsc = ISC_LIST_HEAD(server->cachelist);
			     nsc != NULL; nsc = ISC_LIST_NEXT(nsc, link))
			{
				if (nsc->cache == view->cache) {
					break;
				}
			}
			INSIST(nsc != NULL);
			nsc->needflush = true;
		}
	} else {
		found = true;
	}

	/* Perform flush */
	for (nsc = ISC_LIST_HEAD(server->cachelist); nsc != NULL;
	     nsc = ISC_LIST_NEXT(nsc, link))
	{
		if (ptr != NULL && !nsc->needflush) {
			continue;
		}
		nsc->needflush = true;
		result = dns_view_flushcache(nsc->primaryview, false);
		if (result != ISC_R_SUCCESS) {
			flushed = false;
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "flushing cache in view '%s' failed: %s",
				      nsc->primaryview->name,
				      isc_result_totext(result));
		}
	}

	/*
	 * Fix up views that share a flushed cache: let the views update the
	 * cache DB they're referring to.  This could also be an expensive
	 * operation, but should typically be marginal: the inner loop is only
	 * necessary for views that share a cache, and if there are many such
	 * views the number of shared cache should normally be small.
	 * A worst case is that we have n views and n/2 caches, each shared by
	 * two views.  Then this will be a O(n^2/4) operation.
	 */
	for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
	     view = ISC_LIST_NEXT(view, link))
	{
		if (!dns_view_iscacheshared(view)) {
			continue;
		}
		for (nsc = ISC_LIST_HEAD(server->cachelist); nsc != NULL;
		     nsc = ISC_LIST_NEXT(nsc, link))
		{
			if (!nsc->needflush || nsc->cache != view->cache) {
				continue;
			}
			result = dns_view_flushcache(view, true);
			if (result != ISC_R_SUCCESS) {
				flushed = false;
				isc_log_write(
					named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
					NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
					"fixing cache in view '%s' "
					"failed: %s",
					view->name, isc_result_totext(result));
			}
		}
	}

	/* Cleanup the cache list. */
	for (nsc = ISC_LIST_HEAD(server->cachelist); nsc != NULL;
	     nsc = ISC_LIST_NEXT(nsc, link))
	{
		nsc->needflush = false;
	}

	if (flushed && found) {
		if (ptr != NULL) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
				      "flushing cache in view '%s' succeeded",
				      ptr);
		} else {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
				      "flushing caches in all views succeeded");
		}
		result = ISC_R_SUCCESS;
	} else {
		if (!found) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "flushing cache in view '%s' failed: "
				      "view not found",
				      ptr);
			result = ISC_R_NOTFOUND;
		} else {
			result = ISC_R_FAILURE;
		}
	}
	isc_loopmgr_resume(named_g_loopmgr);
	return result;
}

isc_result_t
named_server_flushnode(named_server_t *server, isc_lex_t *lex, bool tree) {
	char *ptr, *viewname;
	char target[DNS_NAME_FORMATSIZE];
	dns_view_t *view;
	bool flushed;
	bool found;
	isc_result_t result;
	isc_buffer_t b;
	dns_fixedname_t fixed;
	dns_name_t *name;

	/* Skip the command name. */
	ptr = next_token(lex, NULL);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	/* Find the domain name to flush. */
	ptr = next_token(lex, NULL);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	strlcpy(target, ptr, DNS_NAME_FORMATSIZE);
	isc_buffer_constinit(&b, target, strlen(target));
	isc_buffer_add(&b, strlen(target));
	name = dns_fixedname_initname(&fixed);
	result = dns_name_fromtext(name, &b, dns_rootname, 0, NULL);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	/* Look for the view name. */
	viewname = next_token(lex, NULL);

	isc_loopmgr_pause(named_g_loopmgr);
	flushed = true;
	found = false;
	for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
	     view = ISC_LIST_NEXT(view, link))
	{
		if (viewname != NULL && strcasecmp(viewname, view->name) != 0) {
			continue;
		}
		found = true;
		/*
		 * It's a little inefficient to try flushing name for all views
		 * if some of the views share a single cache.  But since the
		 * operation is lightweight we prefer simplicity here.
		 */
		result = dns_view_flushnode(view, name, tree);
		if (result != ISC_R_SUCCESS) {
			flushed = false;
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "flushing %s '%s' in cache view '%s' "
				      "failed: %s",
				      tree ? "tree" : "name", target,
				      view->name, isc_result_totext(result));
		}
	}
	if (flushed && found) {
		if (viewname != NULL) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
				      "flushing %s '%s' in cache view '%s' "
				      "succeeded",
				      tree ? "tree" : "name", target, viewname);
		} else {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
				      "flushing %s '%s' in all cache views "
				      "succeeded",
				      tree ? "tree" : "name", target);
		}
		result = ISC_R_SUCCESS;
	} else {
		if (!found) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "flushing %s '%s' in cache view '%s' "
				      "failed: view not found",
				      tree ? "tree" : "name", target, viewname);
		}
		result = ISC_R_FAILURE;
	}
	isc_loopmgr_resume(named_g_loopmgr);
	return result;
}

isc_result_t
named_server_status(named_server_t *server, isc_buffer_t **text) {
	isc_result_t result;
	unsigned int zonecount, xferrunning, xferdeferred, xferfirstrefresh;
	unsigned int soaqueries, automatic;
	const char *ob = "", *cb = "", *alt = "";
	char boottime[ISC_FORMATHTTPTIMESTAMP_SIZE];
	char configtime[ISC_FORMATHTTPTIMESTAMP_SIZE];
	char line[1024], hostname[256];
	named_reload_t reload_status;

	REQUIRE(text != NULL);

	if (named_g_server->version_set) {
		ob = " (";
		cb = ")";
		if (named_g_server->version == NULL) {
			alt = "version.bind/txt/ch disabled";
		} else {
			alt = named_g_server->version;
		}
	}
	zonecount = dns_zonemgr_getcount(server->zonemgr, DNS_ZONESTATE_ANY);
	xferrunning = dns_zonemgr_getcount(server->zonemgr,
					   DNS_ZONESTATE_XFERRUNNING);
	xferdeferred = dns_zonemgr_getcount(server->zonemgr,
					    DNS_ZONESTATE_XFERDEFERRED);
	xferfirstrefresh = dns_zonemgr_getcount(server->zonemgr,
						DNS_ZONESTATE_XFERFIRSTREFRESH);
	soaqueries = dns_zonemgr_getcount(server->zonemgr,
					  DNS_ZONESTATE_SOAQUERY);
	automatic = dns_zonemgr_getcount(server->zonemgr,
					 DNS_ZONESTATE_AUTOMATIC);

	isc_time_formathttptimestamp(&named_g_boottime, boottime,
				     sizeof(boottime));
	isc_time_formathttptimestamp(&named_g_configtime, configtime,
				     sizeof(configtime));

	snprintf(line, sizeof(line), "version: %s%s <id:%s>%s%s%s\n",
		 PACKAGE_STRING, PACKAGE_DESCRIPTION, PACKAGE_SRCID, ob, alt,
		 cb);
	CHECK(putstr(text, line));

	if (gethostname(hostname, sizeof(hostname)) == 0) {
		strlcpy(hostname, "localhost", sizeof(hostname));
	}
	snprintf(line, sizeof(line), "running on %s: %s\n", hostname,
		 named_os_uname());
	CHECK(putstr(text, line));

	snprintf(line, sizeof(line), "boot time: %s\n", boottime);
	CHECK(putstr(text, line));

	snprintf(line, sizeof(line), "last configured: %s\n", configtime);
	CHECK(putstr(text, line));

	if (named_g_chrootdir != NULL) {
		snprintf(line, sizeof(line), "configuration file: %s (%s%s)\n",
			 named_g_conffile, named_g_chrootdir, named_g_conffile);
	} else {
		snprintf(line, sizeof(line), "configuration file: %s\n",
			 named_g_conffile);
	}
	CHECK(putstr(text, line));

	snprintf(line, sizeof(line), "CPUs found: %u\n", named_g_cpus_detected);
	CHECK(putstr(text, line));

	snprintf(line, sizeof(line), "worker threads: %u\n", named_g_cpus);
	CHECK(putstr(text, line));

	snprintf(line, sizeof(line), "number of zones: %u (%u automatic)\n",
		 zonecount, automatic);
	CHECK(putstr(text, line));

	snprintf(line, sizeof(line), "debug level: %u\n", named_g_debuglevel);
	CHECK(putstr(text, line));

	snprintf(line, sizeof(line), "xfers running: %u\n", xferrunning);
	CHECK(putstr(text, line));

	snprintf(line, sizeof(line), "xfers deferred: %u\n", xferdeferred);
	CHECK(putstr(text, line));

	snprintf(line, sizeof(line), "xfers first refresh: %u\n",
		 xferfirstrefresh);
	CHECK(putstr(text, line));

	snprintf(line, sizeof(line), "soa queries in progress: %u\n",
		 soaqueries);
	CHECK(putstr(text, line));

	snprintf(line, sizeof(line), "query logging is %s\n",
		 ns_server_getoption(server->sctx, NS_SERVER_LOGQUERIES)
			 ? "ON"
			 : "OFF");
	CHECK(putstr(text, line));

	snprintf(line, sizeof(line), "response logging is %s\n",
		 ns_server_getoption(server->sctx, NS_SERVER_LOGRESPONSES)
			 ? "ON"
			 : "OFF");
	CHECK(putstr(text, line));

	snprintf(line, sizeof(line), "memory profiling is %s\n",
		 named_server_getmemprof());
	CHECK(putstr(text, line));

	snprintf(line, sizeof(line), "recursive clients: %u/%u/%u\n",
		 isc_quota_getused(&server->sctx->recursionquota),
		 isc_quota_getsoft(&server->sctx->recursionquota),
		 isc_quota_getmax(&server->sctx->recursionquota));
	CHECK(putstr(text, line));

	snprintf(line, sizeof(line), "recursive high-water: %u\n",
		 (unsigned int)ns_stats_get_counter(
			 server->sctx->nsstats,
			 ns_statscounter_recurshighwater));
	CHECK(putstr(text, line));

	snprintf(line, sizeof(line), "tcp clients: %u/%u\n",
		 isc_quota_getused(&server->sctx->tcpquota),
		 isc_quota_getmax(&server->sctx->tcpquota));
	CHECK(putstr(text, line));

	snprintf(line, sizeof(line), "TCP high-water: %u\n",
		 (unsigned int)ns_stats_get_counter(
			 server->sctx->nsstats, ns_statscounter_tcphighwater));
	CHECK(putstr(text, line));

	reload_status = atomic_load(&server->reload_status);
	if (reload_status != NAMED_RELOAD_DONE) {
		snprintf(line, sizeof(line), "reload/reconfig %s\n",
			 reload_status == NAMED_RELOAD_FAILED ? "failed"
							      : "in progress");
		CHECK(putstr(text, line));
	}

	CHECK(putstr(text, "server is up and running"));
	CHECK(putnull(text));

	return ISC_R_SUCCESS;
cleanup:
	return result;
}

isc_result_t
named_server_testgen(isc_lex_t *lex, isc_buffer_t **text) {
	isc_result_t result;
	char *ptr;
	unsigned long count;
	unsigned long i;
	const unsigned char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";

	REQUIRE(text != NULL);

	/* Skip the command name. */
	ptr = next_token(lex, text);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	ptr = next_token(lex, text);
	if (ptr == NULL) {
		count = 26;
	} else {
		count = strtoul(ptr, NULL, 10);
	}

	CHECK(isc_buffer_reserve(*text, count));
	for (i = 0; i < count; i++) {
		CHECK(putuint8(text, chars[i % (sizeof(chars) - 1)]));
	}

	CHECK(putnull(text));

cleanup:
	return result;
}

/*
 * Act on a "sign" or "loadkeys" command from the command channel.
 */
isc_result_t
named_server_rekey(named_server_t *server, isc_lex_t *lex,
		   isc_buffer_t **text) {
	isc_result_t result;
	dns_zone_t *zone = NULL;
	dns_zonetype_t type;
	uint16_t keyopts;
	bool fullsign = false;
	char *ptr;

	REQUIRE(text != NULL);

	ptr = next_token(lex, text);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	if (strcasecmp(ptr, NAMED_COMMAND_SIGN) == 0) {
		fullsign = true;
	}

	REQUIRE(text != NULL);

	result = zone_from_args(server, lex, NULL, &zone, NULL, text, false);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	if (zone == NULL) {
		return ISC_R_UNEXPECTEDEND; /* XXX: or do all zones? */
	}

	type = dns_zone_gettype(zone);
	if (type != dns_zone_primary) {
		dns_zone_detach(&zone);
		return DNS_R_NOTPRIMARY;
	}

	keyopts = dns_zone_getkeyopts(zone);

	/*
	 * "rndc loadkeys" requires a "dnssec-policy".
	 */
	if ((keyopts & DNS_ZONEKEY_ALLOW) == 0) {
		result = ISC_R_NOPERM;
	} else if ((keyopts & DNS_ZONEKEY_MAINTAIN) == 0 && !fullsign) {
		result = ISC_R_NOPERM;
	} else {
		dns_zone_rekey(zone, fullsign);
	}

	dns_zone_detach(&zone);
	return result;
}

/*
 * Act on a "sync" command from the command channel.
 */
static isc_result_t
synczone(dns_zone_t *zone, void *uap) {
	bool cleanup = *(bool *)uap;
	isc_result_t result;
	dns_zone_t *raw = NULL;
	char *journal;

	dns_zone_getraw(zone, &raw);
	if (raw != NULL) {
		synczone(raw, uap);
		dns_zone_detach(&raw);
	}

	result = dns_zone_flush(zone);
	if (result != ISC_R_SUCCESS) {
		cleanup = false;
	}
	if (cleanup) {
		journal = dns_zone_getjournal(zone);
		if (journal != NULL) {
			(void)isc_file_remove(journal);
		}
	}

	return result;
}

isc_result_t
named_server_sync(named_server_t *server, isc_lex_t *lex, isc_buffer_t **text) {
	isc_result_t result, tresult;
	dns_view_t *view = NULL;
	dns_zone_t *zone = NULL;
	char classstr[DNS_RDATACLASS_FORMATSIZE];
	char zonename[DNS_NAME_FORMATSIZE];
	const char *vname = NULL, *sep = NULL, *arg = NULL;
	bool cleanup = false;

	REQUIRE(text != NULL);

	(void)next_token(lex, text);

	arg = next_token(lex, text);
	if (arg != NULL &&
	    (strcmp(arg, "-clean") == 0 || strcmp(arg, "-clear") == 0))
	{
		cleanup = true;
		arg = next_token(lex, text);
	}

	REQUIRE(text != NULL);

	result = zone_from_args(server, lex, arg, &zone, NULL, text, false);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	if (zone == NULL) {
		isc_loopmgr_pause(named_g_loopmgr);
		tresult = ISC_R_SUCCESS;
		for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
		     view = ISC_LIST_NEXT(view, link))
		{
			result = dns_view_apply(view, false, NULL, synczone,
						&cleanup);
			if (result != ISC_R_SUCCESS && tresult == ISC_R_SUCCESS)
			{
				tresult = result;
			}
		}
		isc_loopmgr_resume(named_g_loopmgr);
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
			      "dumping all zones%s: %s",
			      cleanup ? ", removing journal files" : "",
			      isc_result_totext(result));
		return tresult;
	}

	isc_loopmgr_pause(named_g_loopmgr);
	result = synczone(zone, &cleanup);
	isc_loopmgr_resume(named_g_loopmgr);

	view = dns_zone_getview(zone);
	if (strcmp(view->name, "_default") == 0 ||
	    strcmp(view->name, "_bind") == 0)
	{
		vname = "";
		sep = "";
	} else {
		vname = view->name;
		sep = " ";
	}
	dns_rdataclass_format(dns_zone_getclass(zone), classstr,
			      sizeof(classstr));
	dns_name_format(dns_zone_getorigin(zone), zonename, sizeof(zonename));
	isc_log_write(
		named_g_lctx, NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_SERVER,
		ISC_LOG_INFO, "sync: dumping zone '%s/%s'%s%s%s: %s", zonename,
		classstr, sep, vname, cleanup ? ", removing journal file" : "",
		isc_result_totext(result));
	dns_zone_detach(&zone);
	return result;
}

/*
 * Act on a "freeze" or "thaw" command from the command channel.
 */
isc_result_t
named_server_freeze(named_server_t *server, bool freeze, isc_lex_t *lex,
		    isc_buffer_t **text) {
	isc_result_t result, tresult;
	dns_zone_t *mayberaw = NULL, *raw = NULL;
	dns_zonetype_t type;
	char classstr[DNS_RDATACLASS_FORMATSIZE];
	char zonename[DNS_NAME_FORMATSIZE];
	dns_view_t *view;
	const char *vname, *sep;
	bool frozen;
	const char *msg = NULL;

	REQUIRE(text != NULL);

	result = zone_from_args(server, lex, NULL, &mayberaw, NULL, text, true);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	if (mayberaw == NULL) {
		isc_loopmgr_pause(named_g_loopmgr);
		tresult = ISC_R_SUCCESS;
		for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
		     view = ISC_LIST_NEXT(view, link))
		{
			result = dns_view_freezezones(view, freeze);
			if (result != ISC_R_SUCCESS && tresult == ISC_R_SUCCESS)
			{
				tresult = result;
			}
		}
		isc_loopmgr_resume(named_g_loopmgr);
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
			      "%s all zones: %s",
			      freeze ? "freezing" : "thawing",
			      isc_result_totext(tresult));
		return tresult;
	}
	dns_zone_getraw(mayberaw, &raw);
	if (raw != NULL) {
		dns_zone_detach(&mayberaw);
		dns_zone_attach(raw, &mayberaw);
		dns_zone_detach(&raw);
	}
	type = dns_zone_gettype(mayberaw);
	if (type != dns_zone_primary) {
		dns_zone_detach(&mayberaw);
		return DNS_R_NOTPRIMARY;
	}

	if (freeze && !dns_zone_isdynamic(mayberaw, true)) {
		dns_zone_detach(&mayberaw);
		return DNS_R_NOTDYNAMIC;
	}

	isc_loopmgr_pause(named_g_loopmgr);
	frozen = dns_zone_getupdatedisabled(mayberaw);
	if (freeze) {
		if (frozen) {
			msg = "WARNING: The zone was already frozen.\n"
			      "Someone else may be editing it or "
			      "it may still be re-loading.";
			result = DNS_R_FROZEN;
		}
		if (result == ISC_R_SUCCESS) {
			result = dns_zone_flush(mayberaw);
			if (result != ISC_R_SUCCESS) {
				msg = "Flushing the zone updates to "
				      "disk failed.";
			}
		}
		if (result == ISC_R_SUCCESS) {
			dns_zone_setupdatedisabled(mayberaw, freeze);
		}
	} else {
		if (frozen) {
			result = dns_zone_loadandthaw(mayberaw);
			switch (result) {
			case ISC_R_SUCCESS:
			case DNS_R_UPTODATE:
				msg = "The zone reload and thaw was "
				      "successful.";
				result = ISC_R_SUCCESS;
				break;
			case DNS_R_CONTINUE:
				msg = "A zone reload and thaw was started.\n"
				      "Check the logs to see the result.";
				result = ISC_R_SUCCESS;
				break;
			default:
				break;
			}
		}
	}
	isc_loopmgr_resume(named_g_loopmgr);

	if (msg != NULL) {
		(void)putstr(text, msg);
		(void)putnull(text);
	}

	view = dns_zone_getview(mayberaw);
	if (strcmp(view->name, "_default") == 0 ||
	    strcmp(view->name, "_bind") == 0)
	{
		vname = "";
		sep = "";
	} else {
		vname = view->name;
		sep = " ";
	}
	dns_rdataclass_format(dns_zone_getclass(mayberaw), classstr,
			      sizeof(classstr));
	dns_name_format(dns_zone_getorigin(mayberaw), zonename,
			sizeof(zonename));
	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		      "%s zone '%s/%s'%s%s: %s",
		      freeze ? "freezing" : "thawing", zonename, classstr, sep,
		      vname, isc_result_totext(result));
	dns_zone_detach(&mayberaw);
	return result;
}

#ifdef HAVE_LIBSCF
/*
 * This function adds a message for rndc to echo if named
 * is managed by smf and is also running chroot.
 */
isc_result_t
named_smf_add_message(isc_buffer_t **text) {
	REQUIRE(text != NULL);

	return putstr(text, "use svcadm(1M) to manage named");
}
#endif /* HAVE_LIBSCF */

#ifndef HAVE_LMDB

/*
 * Emit a comment at the top of the nzf file containing the viewname
 * Expects the fp to already be open for writing
 */
#define HEADER1 "# New zone file for view: "
#define HEADER2                                                     \
	"\n# This file contains configuration for zones added by\n" \
	"# the 'rndc addzone' command. DO NOT EDIT BY HAND.\n"
static isc_result_t
add_comment(FILE *fp, const char *viewname) {
	isc_result_t result;
	CHECK(isc_stdio_write(HEADER1, sizeof(HEADER1) - 1, 1, fp, NULL));
	CHECK(isc_stdio_write(viewname, strlen(viewname), 1, fp, NULL));
	CHECK(isc_stdio_write(HEADER2, sizeof(HEADER2) - 1, 1, fp, NULL));
cleanup:
	return result;
}

static void
dumpzone(void *arg, const char *buf, int len) {
	FILE *fp = arg;

	(void)isc_stdio_write(buf, len, 1, fp, NULL);
}

static isc_result_t
nzf_append(dns_view_t *view, const cfg_obj_t *zconfig) {
	isc_result_t result;
	off_t offset;
	FILE *fp = NULL;
	bool offsetok = false;

	LOCK(&view->new_zone_lock);

	CHECK(isc_stdio_open(view->new_zone_file, "a", &fp));
	CHECK(isc_stdio_seek(fp, 0, SEEK_END));

	CHECK(isc_stdio_tell(fp, &offset));
	offsetok = true;
	if (offset == 0) {
		CHECK(add_comment(fp, view->name));
	}

	CHECK(isc_stdio_write("zone ", 5, 1, fp, NULL));
	cfg_printx(zconfig, CFG_PRINTER_ONELINE, dumpzone, fp);
	CHECK(isc_stdio_write(";\n", 2, 1, fp, NULL));
	CHECK(isc_stdio_flush(fp));
	result = isc_stdio_close(fp);
	fp = NULL;

cleanup:
	if (fp != NULL) {
		(void)isc_stdio_close(fp);
		if (offsetok) {
			isc_result_t result2;

			result2 = isc_file_truncate(view->new_zone_file,
						    offset);
			if (result2 != ISC_R_SUCCESS) {
				isc_log_write(
					named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
					NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
					"Error truncating NZF file '%s' "
					"during rollback from append: "
					"%s",
					view->new_zone_file,
					isc_result_totext(result2));
			}
		}
	}
	UNLOCK(&view->new_zone_lock);
	return result;
}

static isc_result_t
nzf_writeconf(const cfg_obj_t *config, dns_view_t *view) {
	const cfg_obj_t *zl = NULL;
	cfg_list_t *list;
	const cfg_listelt_t *elt;

	FILE *fp = NULL;
	char tmp[1024];
	isc_result_t result;

	result = isc_file_template(view->new_zone_file, "nzf-XXXXXXXX", tmp,
				   sizeof(tmp));
	if (result == ISC_R_SUCCESS) {
		result = isc_file_openunique(tmp, &fp);
	}
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	cfg_map_get(config, "zone", &zl);
	if (!cfg_obj_islist(zl)) {
		CHECK(ISC_R_FAILURE);
	}

	list = UNCONST(&zl->value.list);

	CHECK(add_comment(fp, view->name)); /* force a comment */

	for (elt = ISC_LIST_HEAD(*list); elt != NULL;
	     elt = ISC_LIST_NEXT(elt, link))
	{
		const cfg_obj_t *zconfig = cfg_listelt_value(elt);

		CHECK(isc_stdio_write("zone ", 5, 1, fp, NULL));
		cfg_printx(zconfig, CFG_PRINTER_ONELINE, dumpzone, fp);
		CHECK(isc_stdio_write(";\n", 2, 1, fp, NULL));
	}

	CHECK(isc_stdio_flush(fp));
	result = isc_stdio_close(fp);
	fp = NULL;
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}
	CHECK(isc_file_rename(tmp, view->new_zone_file));
	return result;

cleanup:
	if (fp != NULL) {
		(void)isc_stdio_close(fp);
	}
	(void)isc_file_remove(tmp);
	return result;
}

static isc_result_t
load_nzf(dns_view_t *view, ns_cfgctx_t *nzcfg) {
	isc_result_t result;

	/* The new zone file may not exist. That is OK. */
	if (!isc_file_exists(view->new_zone_file)) {
		return ISC_R_SUCCESS;
	}

	/*
	 * Parse the configuration in the NZF file.  This may be called in
	 * multiple views, so we reset the parser each time.
	 */
	cfg_parser_reset(named_g_addparser);
	result = cfg_parse_file(named_g_addparser, view->new_zone_file,
				&cfg_type_addzoneconf, &nzcfg->nzf_config);
	if (result != ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "Error parsing NZF file '%s': %s",
			      view->new_zone_file, isc_result_totext(result));
	}

	return result;
}
#else  /* HAVE_LMDB */

static void
nzd_setkey(MDB_val *key, dns_name_t *name, char *namebuf, size_t buflen) {
	dns_fixedname_t fixed;

	dns_fixedname_init(&fixed);
	dns_name_downcase(name, dns_fixedname_name(&fixed), NULL);
	dns_name_format(dns_fixedname_name(&fixed), namebuf, buflen);

	key->mv_data = namebuf;
	key->mv_size = strlen(namebuf);
}

static void
dumpzone(void *arg, const char *buf, int len) {
	ns_dzarg_t *dzarg = arg;
	isc_result_t result;

	REQUIRE(dzarg != NULL && ISC_MAGIC_VALID(dzarg, DZARG_MAGIC));

	result = putmem(dzarg->text, buf, len);
	if (result != ISC_R_SUCCESS && dzarg->result == ISC_R_SUCCESS) {
		dzarg->result = result;
	}
}

static isc_result_t
nzd_save(MDB_txn **txnp, MDB_dbi dbi, dns_zone_t *zone,
	 const cfg_obj_t *zconfig) {
	isc_result_t result;
	int status;
	dns_view_t *view;
	bool commit = false;
	isc_buffer_t *text = NULL;
	char namebuf[1024];
	MDB_val key, data;
	ns_dzarg_t dzarg;

	view = dns_zone_getview(zone);

	nzd_setkey(&key, dns_zone_getorigin(zone), namebuf, sizeof(namebuf));

	if (zconfig == NULL) {
		/* We're deleting the zone from the database */
		status = mdb_del(*txnp, dbi, &key, NULL);
		if (status != MDB_SUCCESS && status != MDB_NOTFOUND) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "Error deleting zone %s "
				      "from NZD database: %s",
				      namebuf, mdb_strerror(status));
			result = ISC_R_FAILURE;
			goto cleanup;
		} else if (status != MDB_NOTFOUND) {
			commit = true;
		}
	} else {
		/* We're creating or overwriting the zone */
		const cfg_obj_t *zoptions;

		isc_buffer_allocate(view->mctx, &text, 256);

		zoptions = cfg_tuple_get(zconfig, "options");
		if (zoptions == NULL) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "Unable to get options from config in "
				      "nzd_save()");
			result = ISC_R_FAILURE;
			goto cleanup;
		}

		dzarg.magic = DZARG_MAGIC;
		dzarg.text = &text;
		dzarg.result = ISC_R_SUCCESS;
		cfg_printx(zoptions, CFG_PRINTER_ONELINE, dumpzone, &dzarg);
		if (dzarg.result != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "Error writing zone config to "
				      "buffer in nzd_save(): %s",
				      isc_result_totext(dzarg.result));
			result = dzarg.result;
			goto cleanup;
		}

		data.mv_data = isc_buffer_base(text);
		data.mv_size = isc_buffer_usedlength(text);

		status = mdb_put(*txnp, dbi, &key, &data, 0);
		if (status != MDB_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "Error inserting zone in "
				      "NZD database: %s",
				      mdb_strerror(status));
			result = ISC_R_FAILURE;
			goto cleanup;
		}

		commit = true;
	}

	result = ISC_R_SUCCESS;

cleanup:
	if (!commit || result != ISC_R_SUCCESS) {
		(void)mdb_txn_abort(*txnp);
	} else {
		status = mdb_txn_commit(*txnp);
		if (status != MDB_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "Error committing "
				      "NZD database: %s",
				      mdb_strerror(status));
			result = ISC_R_FAILURE;
		}
	}
	*txnp = NULL;

	if (text != NULL) {
		isc_buffer_free(&text);
	}

	return result;
}

/*
 * Check whether the new zone database for 'view' can be opened for writing.
 *
 * Caller must hold 'view->new_zone_lock'.
 */
static isc_result_t
nzd_writable(dns_view_t *view) {
	isc_result_t result = ISC_R_SUCCESS;
	int status;
	MDB_dbi dbi;
	MDB_txn *txn = NULL;

	REQUIRE(view != NULL);

	status = mdb_txn_begin((MDB_env *)view->new_zone_dbenv, 0, 0, &txn);
	if (status != MDB_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
			      "mdb_txn_begin: %s", mdb_strerror(status));
		return ISC_R_FAILURE;
	}

	status = mdb_dbi_open(txn, NULL, 0, &dbi);
	if (status != MDB_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
			      "mdb_dbi_open: %s", mdb_strerror(status));
		result = ISC_R_FAILURE;
	}

	mdb_txn_abort(txn);
	return result;
}

/*
 * Open the new zone database for 'view' and start a transaction for it.
 *
 * Caller must hold 'view->new_zone_lock'.
 */
static isc_result_t
nzd_open(dns_view_t *view, unsigned int flags, MDB_txn **txnp, MDB_dbi *dbi) {
	int status;
	MDB_txn *txn = NULL;

	REQUIRE(view != NULL);
	REQUIRE(txnp != NULL && *txnp == NULL);
	REQUIRE(dbi != NULL);

	status = mdb_txn_begin((MDB_env *)view->new_zone_dbenv, 0, flags, &txn);
	if (status != MDB_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
			      "mdb_txn_begin: %s", mdb_strerror(status));
		goto cleanup;
	}

	status = mdb_dbi_open(txn, NULL, 0, dbi);
	if (status != MDB_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
			      "mdb_dbi_open: %s", mdb_strerror(status));
		goto cleanup;
	}

	*txnp = txn;

cleanup:
	if (status != MDB_SUCCESS) {
		if (txn != NULL) {
			mdb_txn_abort(txn);
		}
		return ISC_R_FAILURE;
	}

	return ISC_R_SUCCESS;
}

/*
 * nzd_env_close() and nzd_env_reopen are a kluge to address the
 * problem of an NZD file possibly being created before we drop
 * root privileges.
 */
static void
nzd_env_close(dns_view_t *view) {
	const char *dbpath = NULL;
	char dbpath_copy[PATH_MAX];
	char lockpath[PATH_MAX];
	int status, ret;

	if (view->new_zone_dbenv == NULL) {
		return;
	}

	status = mdb_env_get_path(view->new_zone_dbenv, &dbpath);
	INSIST(status == MDB_SUCCESS);
	snprintf(lockpath, sizeof(lockpath), "%s-lock", dbpath);
	strlcpy(dbpath_copy, dbpath, sizeof(dbpath_copy));
	mdb_env_close((MDB_env *)view->new_zone_dbenv);

	/*
	 * Database files must be owned by the eventual user, not by root.
	 */
	ret = chown(dbpath_copy, named_os_uid(), -1);
	UNUSED(ret);

	/*
	 * Some platforms need the lockfile not to exist when we reopen the
	 * environment.
	 */
	(void)isc_file_remove(lockpath);

	view->new_zone_dbenv = NULL;
}

static isc_result_t
nzd_env_reopen(dns_view_t *view) {
	isc_result_t result;
	MDB_env *env = NULL;
	int status;

	if (view->new_zone_db == NULL) {
		return ISC_R_SUCCESS;
	}

	nzd_env_close(view);

	status = mdb_env_create(&env);
	if (status != MDB_SUCCESS) {
		isc_log_write(dns_lctx, DNS_LOGCATEGORY_GENERAL,
			      ISC_LOGMODULE_OTHER, ISC_LOG_ERROR,
			      "mdb_env_create failed: %s",
			      mdb_strerror(status));
		CHECK(ISC_R_FAILURE);
	}

	if (view->new_zone_mapsize != 0ULL) {
		status = mdb_env_set_mapsize(env, view->new_zone_mapsize);
		if (status != MDB_SUCCESS) {
			isc_log_write(dns_lctx, DNS_LOGCATEGORY_GENERAL,
				      ISC_LOGMODULE_OTHER, ISC_LOG_ERROR,
				      "mdb_env_set_mapsize failed: %s",
				      mdb_strerror(status));
			CHECK(ISC_R_FAILURE);
		}
	}

	status = mdb_env_open(env, view->new_zone_db, DNS_LMDB_FLAGS, 0600);
	if (status != MDB_SUCCESS) {
		isc_log_write(dns_lctx, DNS_LOGCATEGORY_GENERAL,
			      ISC_LOGMODULE_OTHER, ISC_LOG_ERROR,
			      "mdb_env_open of '%s' failed: %s",
			      view->new_zone_db, mdb_strerror(status));
		CHECK(ISC_R_FAILURE);
	}

	view->new_zone_dbenv = env;
	env = NULL;
	result = ISC_R_SUCCESS;

cleanup:
	if (env != NULL) {
		mdb_env_close(env);
	}
	return result;
}

/*
 * If 'commit' is true, commit the new zone database transaction pointed to by
 * 'txnp'; otherwise, abort that transaction.
 *
 * Caller must hold 'view->new_zone_lock' for the view that the transaction
 * pointed to by 'txnp' was started for.
 */
static isc_result_t
nzd_close(MDB_txn **txnp, bool commit) {
	isc_result_t result = ISC_R_SUCCESS;
	int status;

	REQUIRE(txnp != NULL);

	if (*txnp != NULL) {
		if (commit) {
			status = mdb_txn_commit(*txnp);
			if (status != MDB_SUCCESS) {
				result = ISC_R_FAILURE;
			}
		} else {
			mdb_txn_abort(*txnp);
		}
		*txnp = NULL;
	}

	return result;
}

/*
 * If there's an existing NZF file, load it and migrate its data
 * to the NZD.
 *
 * Caller must hold view->new_zone_lock.
 */
static isc_result_t
load_nzf(dns_view_t *view, ns_cfgctx_t *nzcfg) {
	isc_result_t result;
	cfg_obj_t *nzf_config = NULL;
	int status;
	isc_buffer_t *text = NULL;
	bool commit = false;
	const cfg_obj_t *zonelist;
	const cfg_listelt_t *element;
	char tempname[PATH_MAX];
	MDB_txn *txn = NULL;
	MDB_dbi dbi;
	MDB_val key, data;
	ns_dzarg_t dzarg;

	UNUSED(nzcfg);

	/*
	 * If NZF file doesn't exist, or NZD DB exists and already
	 * has data, return without attempting migration.
	 */
	if (!isc_file_exists(view->new_zone_file)) {
		result = ISC_R_SUCCESS;
		goto cleanup;
	}

	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		      "Migrating zones from NZF file '%s' to "
		      "NZD database '%s'",
		      view->new_zone_file, view->new_zone_db);
	/*
	 * Instead of blindly copying lines, we parse the NZF file using
	 * the configuration parser, because it validates it against the
	 * config type, giving us a guarantee that valid configuration
	 * will be written to DB.
	 */
	cfg_parser_reset(named_g_addparser);
	result = cfg_parse_file(named_g_addparser, view->new_zone_file,
				&cfg_type_addzoneconf, &nzf_config);
	if (result != ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "Error parsing NZF file '%s': %s",
			      view->new_zone_file, isc_result_totext(result));
		goto cleanup;
	}

	zonelist = NULL;
	CHECK(cfg_map_get(nzf_config, "zone", &zonelist));
	if (!cfg_obj_islist(zonelist)) {
		CHECK(ISC_R_FAILURE);
	}

	CHECK(nzd_open(view, 0, &txn, &dbi));

	isc_buffer_allocate(view->mctx, &text, 256);

	for (element = cfg_list_first(zonelist); element != NULL;
	     element = cfg_list_next(element))
	{
		const cfg_obj_t *zconfig;
		const cfg_obj_t *zoptions;
		char zname[DNS_NAME_FORMATSIZE];
		dns_fixedname_t fname;
		dns_name_t *name;
		const char *origin;
		isc_buffer_t b;

		zconfig = cfg_listelt_value(element);

		origin = cfg_obj_asstring(cfg_tuple_get(zconfig, "name"));
		if (origin == NULL) {
			result = ISC_R_FAILURE;
			goto cleanup;
		}

		/* Normalize zone name */
		isc_buffer_constinit(&b, origin, strlen(origin));
		isc_buffer_add(&b, strlen(origin));
		name = dns_fixedname_initname(&fname);
		CHECK(dns_name_fromtext(name, &b, dns_rootname,
					DNS_NAME_DOWNCASE, NULL));
		dns_name_format(name, zname, sizeof(zname));

		key.mv_data = zname;
		key.mv_size = strlen(zname);

		zoptions = cfg_tuple_get(zconfig, "options");
		if (zoptions == NULL) {
			result = ISC_R_FAILURE;
			goto cleanup;
		}

		isc_buffer_clear(text);
		dzarg.magic = DZARG_MAGIC;
		dzarg.text = &text;
		dzarg.result = ISC_R_SUCCESS;
		cfg_printx(zoptions, CFG_PRINTER_ONELINE, dumpzone, &dzarg);
		if (dzarg.result != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "Error writing zone config to "
				      "buffer in load_nzf(): %s",
				      isc_result_totext(result));
			result = dzarg.result;
			goto cleanup;
		}

		data.mv_data = isc_buffer_base(text);
		data.mv_size = isc_buffer_usedlength(text);

		status = mdb_put(txn, dbi, &key, &data, MDB_NOOVERWRITE);
		if (status != MDB_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "Error inserting zone in "
				      "NZD database: %s",
				      mdb_strerror(status));
			result = ISC_R_FAILURE;
			goto cleanup;
		}

		commit = true;
	}

	result = ISC_R_SUCCESS;

	/*
	 * Leaving the NZF file in place is harmless as we won't use it
	 * if an NZD database is found for the view. But we rename NZF file
	 * to a backup name here.
	 */
	strlcpy(tempname, view->new_zone_file, sizeof(tempname));
	if (strlen(tempname) < sizeof(tempname) - 1) {
		strlcat(tempname, "~", sizeof(tempname));
		isc_file_rename(view->new_zone_file, tempname);
	}

cleanup:
	if (result != ISC_R_SUCCESS) {
		(void)nzd_close(&txn, false);
	} else {
		result = nzd_close(&txn, commit);
	}

	if (text != NULL) {
		isc_buffer_free(&text);
	}

	if (nzf_config != NULL) {
		cfg_obj_destroy(named_g_addparser, &nzf_config);
	}

	return result;
}
#endif /* HAVE_LMDB */

static isc_result_t
newzone_parse(named_server_t *server, char *command, dns_view_t **viewp,
	      cfg_obj_t **zoneconfp, const cfg_obj_t **zoneobjp,
	      bool *redirectp, isc_buffer_t **text) {
	isc_result_t result;
	isc_buffer_t argbuf;
	bool redirect = false;
	cfg_obj_t *zoneconf = NULL;
	const cfg_obj_t *zlist = NULL;
	const cfg_obj_t *zoneobj = NULL;
	const cfg_obj_t *zoptions = NULL;
	const cfg_obj_t *obj = NULL;
	const char *viewname = NULL;
	dns_rdataclass_t rdclass;
	dns_view_t *view = NULL;
	const char *bn = NULL;

	REQUIRE(viewp != NULL && *viewp == NULL);
	REQUIRE(zoneobjp != NULL && *zoneobjp == NULL);
	REQUIRE(zoneconfp != NULL && *zoneconfp == NULL);
	REQUIRE(redirectp != NULL);

	/* Try to parse the argument string */
	isc_buffer_init(&argbuf, command, (unsigned int)strlen(command));
	isc_buffer_add(&argbuf, strlen(command));

	if (strncasecmp(command, "add", 3) == 0) {
		bn = "addzone";
	} else if (strncasecmp(command, "mod", 3) == 0) {
		bn = "modzone";
	} else {
		UNREACHABLE();
	}

	/*
	 * Convert the "addzone" or "modzone" to just "zone", for
	 * the benefit of the parser
	 */
	isc_buffer_forward(&argbuf, 3);

	cfg_parser_reset(named_g_addparser);
	CHECK(cfg_parse_buffer(named_g_addparser, &argbuf, bn, 0,
			       &cfg_type_addzoneconf, 0, &zoneconf));
	CHECK(cfg_map_get(zoneconf, "zone", &zlist));
	if (!cfg_obj_islist(zlist)) {
		CHECK(ISC_R_FAILURE);
	}

	/* For now we only support adding one zone at a time */
	zoneobj = cfg_listelt_value(cfg_list_first(zlist));

	/* Check the zone type for ones that are not supported by addzone. */
	zoptions = cfg_tuple_get(zoneobj, "options");

	obj = NULL;
	(void)cfg_map_get(zoptions, "type", &obj);
	if (obj == NULL) {
		(void)cfg_map_get(zoptions, "in-view", &obj);
		if (obj != NULL) {
			(void)putstr(text, "'in-view' zones not supported by ");
			(void)putstr(text, bn);
		} else {
			(void)putstr(text, "zone type not specified");
		}
		CHECK(ISC_R_FAILURE);
	}

	if (strcasecmp(cfg_obj_asstring(obj), "hint") == 0 ||
	    strcasecmp(cfg_obj_asstring(obj), "forward") == 0)
	{
		(void)putstr(text, "'");
		(void)putstr(text, cfg_obj_asstring(obj));
		(void)putstr(text, "' zones not supported by ");
		(void)putstr(text, bn);
		CHECK(ISC_R_FAILURE);
	}

	if (strcasecmp(cfg_obj_asstring(obj), "redirect") == 0) {
		redirect = true;
	}

	/* Make sense of optional class argument */
	obj = cfg_tuple_get(zoneobj, "class");
	CHECK(named_config_getclass(obj, dns_rdataclass_in, &rdclass));

	/* Make sense of optional view argument */
	obj = cfg_tuple_get(zoneobj, "view");
	if (obj && cfg_obj_isstring(obj)) {
		viewname = cfg_obj_asstring(obj);
	}
	if (viewname == NULL || *viewname == '\0') {
		viewname = "_default";
	}
	result = dns_viewlist_find(&server->viewlist, viewname, rdclass, &view);
	if (result == ISC_R_NOTFOUND) {
		(void)putstr(text, "no matching view found for '");
		(void)putstr(text, viewname);
		(void)putstr(text, "'");
		goto cleanup;
	} else if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	*viewp = view;
	*zoneobjp = zoneobj;
	*zoneconfp = zoneconf;
	*redirectp = redirect;

	return ISC_R_SUCCESS;

cleanup:
	if (zoneconf != NULL) {
		cfg_obj_destroy(named_g_addparser, &zoneconf);
	}
	if (view != NULL) {
		dns_view_detach(&view);
	}

	return result;
}

static isc_result_t
delete_zoneconf(dns_view_t *view, cfg_parser_t *pctx, const cfg_obj_t *config,
		const dns_name_t *zname, nzfwriter_t nzfwriter) {
	isc_result_t result = ISC_R_NOTFOUND;
	const cfg_listelt_t *elt = NULL;
	const cfg_obj_t *zl = NULL;
	cfg_list_t *list;
	dns_fixedname_t myfixed;
	dns_name_t *myname;

	REQUIRE(view != NULL);
	REQUIRE(pctx != NULL);
	REQUIRE(config != NULL);
	REQUIRE(zname != NULL);

	LOCK(&view->new_zone_lock);

	cfg_map_get(config, "zone", &zl);

	if (!cfg_obj_islist(zl)) {
		CHECK(ISC_R_FAILURE);
	}

	list = UNCONST(&zl->value.list);

	myname = dns_fixedname_initname(&myfixed);

	for (elt = ISC_LIST_HEAD(*list); elt != NULL;
	     elt = ISC_LIST_NEXT(elt, link))
	{
		const cfg_obj_t *zconf = cfg_listelt_value(elt);
		const char *zn;
		cfg_listelt_t *e;

		zn = cfg_obj_asstring(cfg_tuple_get(zconf, "name"));
		result = dns_name_fromstring(myname, zn, dns_rootname, 0, NULL);
		if (result != ISC_R_SUCCESS || !dns_name_equal(zname, myname)) {
			continue;
		}

		e = UNCONST(elt);
		ISC_LIST_UNLINK(*list, e, link);
		cfg_obj_destroy(pctx, &e->obj);
		isc_mem_put(pctx->mctx, e, sizeof(*e));
		result = ISC_R_SUCCESS;
		break;
	}

	/*
	 * Write config to NZF file if appropriate
	 */
	if (nzfwriter != NULL && view->new_zone_file != NULL) {
		result = nzfwriter(config, view);
	}

cleanup:
	UNLOCK(&view->new_zone_lock);
	return result;
}

static isc_result_t
do_addzone(named_server_t *server, ns_cfgctx_t *cfg, dns_view_t *view,
	   dns_name_t *name, cfg_obj_t *zoneconf, const cfg_obj_t *zoneobj,
	   bool redirect, isc_buffer_t **text) {
	isc_result_t result, tresult;
	dns_zone_t *zone = NULL;
#ifndef HAVE_LMDB
	FILE *fp = NULL;
	bool cleanup_config = false;
#else /* HAVE_LMDB */
	MDB_txn *txn = NULL;
	MDB_dbi dbi;
	bool locked = false;

	UNUSED(zoneconf);
#endif

	/* Zone shouldn't already exist */
	if (redirect) {
		result = (view->redirect == NULL) ? ISC_R_NOTFOUND
						  : ISC_R_EXISTS;
	} else {
		result = dns_view_findzone(view, name, DNS_ZTFIND_EXACT, &zone);
		if (result == ISC_R_SUCCESS) {
			result = ISC_R_EXISTS;
		}
	}
	if (result != ISC_R_NOTFOUND) {
		goto cleanup;
	}

	isc_loopmgr_pause(named_g_loopmgr);

#ifndef HAVE_LMDB
	/*
	 * Make sure we can open the configuration save file
	 */
	result = isc_stdio_open(view->new_zone_file, "a", &fp);
	if (result != ISC_R_SUCCESS) {
		isc_loopmgr_resume(named_g_loopmgr);
		TCHECK(putstr(text, "unable to create '"));
		TCHECK(putstr(text, view->new_zone_file));
		TCHECK(putstr(text, "': "));
		TCHECK(putstr(text, isc_result_totext(result)));
		goto cleanup;
	}

	(void)isc_stdio_close(fp);
	fp = NULL;
#else  /* HAVE_LMDB */
	LOCK(&view->new_zone_lock);
	locked = true;
	/* Make sure we can open the NZD database */
	result = nzd_writable(view);
	if (result != ISC_R_SUCCESS) {
		isc_loopmgr_resume(named_g_loopmgr);
		TCHECK(putstr(text, "unable to open NZD database for '"));
		TCHECK(putstr(text, view->new_zone_db));
		TCHECK(putstr(text, "'"));
		result = ISC_R_FAILURE;
		goto cleanup;
	}
#endif /* HAVE_LMDB */

	/* Mark view unfrozen and configure zone */
	dns_view_thaw(view);
	result = configure_zone(cfg->config, zoneobj, cfg->vconfig, view,
				&server->viewlist, &server->kasplist,
				&server->keystorelist, cfg->actx, true, false,
				false, false);
	dns_view_freeze(view);

	isc_loopmgr_resume(named_g_loopmgr);

	if (result != ISC_R_SUCCESS) {
		TCHECK(putstr(text, "configure_zone failed: "));
		TCHECK(putstr(text, isc_result_totext(result)));
		goto cleanup;
	}

	/* Is it there yet? */
	if (redirect) {
		if (view->redirect == NULL) {
			CHECK(ISC_R_NOTFOUND);
		}
		dns_zone_attach(view->redirect, &zone);
	} else {
		result = dns_view_findzone(view, name, DNS_ZTFIND_EXACT, &zone);
		if (result != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "added new zone was not found: %s",
				      isc_result_totext(result));
			goto cleanup;
		}
	}

#ifndef HAVE_LMDB
	/*
	 * If there wasn't a previous newzone config, just save the one
	 * we've created. If there was a previous one, merge the new
	 * zone into it.
	 */
	if (cfg->nzf_config == NULL) {
		cfg_obj_attach(zoneconf, &cfg->nzf_config);
	} else {
		cfg_obj_t *z = UNCONST(zoneobj);
		CHECK(cfg_parser_mapadd(cfg->add_parser, cfg->nzf_config, z,
					"zone"));
	}
	cleanup_config = true;
#endif /* HAVE_LMDB */

	/*
	 * Load the zone from the master file.  If this fails, we'll
	 * need to undo the configuration we've done already.
	 */
	result = dns_zone_load(zone, true);
	if (result != ISC_R_SUCCESS) {
		dns_db_t *dbp = NULL;

		TCHECK(putstr(text, "dns_zone_loadnew failed: "));
		TCHECK(putstr(text, isc_result_totext(result)));

		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
			      "addzone failed; reverting.");

		/* If the zone loaded partially, unload it */
		if (dns_zone_getdb(zone, &dbp) == ISC_R_SUCCESS) {
			dns_db_detach(&dbp);
			dns_zone_unload(zone);
		}

		/* Remove the zone from the zone table */
		dns_view_delzone(view, zone);
		goto cleanup;
	}

	/* Flag the zone as having been added at runtime */
	dns_zone_setadded(zone, true);

#ifdef HAVE_LMDB
	/* Save the new zone configuration into the NZD */
	CHECK(nzd_open(view, 0, &txn, &dbi));
	CHECK(nzd_save(&txn, dbi, zone, zoneobj));
#else  /* ifdef HAVE_LMDB */
	/* Append the zone configuration to the NZF */
	result = nzf_append(view, zoneobj);
#endif /* HAVE_LMDB */

cleanup:

#ifndef HAVE_LMDB
	if (fp != NULL) {
		(void)isc_stdio_close(fp);
	}
	if (result != ISC_R_SUCCESS && cleanup_config) {
		tresult = delete_zoneconf(view, cfg->add_parser,
					  cfg->nzf_config, name, NULL);
		RUNTIME_CHECK(tresult == ISC_R_SUCCESS);
	}
#else  /* HAVE_LMDB */
	if (txn != NULL) {
		(void)nzd_close(&txn, false);
	}
	if (locked) {
		UNLOCK(&view->new_zone_lock);
	}
#endif /* HAVE_LMDB */

	if (zone != NULL) {
		dns_zone_detach(&zone);
	}

	return result;
}

static isc_result_t
do_modzone(named_server_t *server, ns_cfgctx_t *cfg, dns_view_t *view,
	   dns_name_t *name, const char *zname, const cfg_obj_t *zoneobj,
	   bool redirect, isc_buffer_t **text) {
	isc_result_t result, tresult;
	dns_zone_t *zone = NULL;
	bool added;
#ifndef HAVE_LMDB
	FILE *fp = NULL;
	cfg_obj_t *z;
#else  /* HAVE_LMDB */
	MDB_txn *txn = NULL;
	MDB_dbi dbi;
	bool locked = false;
#endif /* HAVE_LMDB */

	/* Zone must already exist */
	if (redirect) {
		if (view->redirect != NULL) {
			dns_zone_attach(view->redirect, &zone);
			result = ISC_R_SUCCESS;
		} else {
			result = ISC_R_NOTFOUND;
		}
	} else {
		result = dns_view_findzone(view, name, DNS_ZTFIND_EXACT, &zone);
	}
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	added = dns_zone_getadded(zone);
	dns_zone_detach(&zone);

#ifndef HAVE_LMDB
	cfg = (ns_cfgctx_t *)view->new_zone_config;
	if (cfg == NULL) {
		TCHECK(putstr(text, "new zone config is not set"));
		CHECK(ISC_R_FAILURE);
	}
#endif /* ifndef HAVE_LMDB */

	isc_loopmgr_pause(named_g_loopmgr);

#ifndef HAVE_LMDB
	/* Make sure we can open the configuration save file */
	result = isc_stdio_open(view->new_zone_file, "a", &fp);
	if (result != ISC_R_SUCCESS) {
		TCHECK(putstr(text, "unable to open '"));
		TCHECK(putstr(text, view->new_zone_file));
		TCHECK(putstr(text, "': "));
		TCHECK(putstr(text, isc_result_totext(result)));
		isc_loopmgr_resume(named_g_loopmgr);
		goto cleanup;
	}
	(void)isc_stdio_close(fp);
	fp = NULL;
#else  /* HAVE_LMDB */
	LOCK(&view->new_zone_lock);
	locked = true;
	/* Make sure we can open the NZD database */
	result = nzd_writable(view);
	if (result != ISC_R_SUCCESS) {
		TCHECK(putstr(text, "unable to open NZD database for '"));
		TCHECK(putstr(text, view->new_zone_db));
		TCHECK(putstr(text, "'"));
		result = ISC_R_FAILURE;
		isc_loopmgr_resume(named_g_loopmgr);
		goto cleanup;
	}
#endif /* HAVE_LMDB */

	/* Reconfigure the zone */
	dns_view_thaw(view);
	result = configure_zone(cfg->config, zoneobj, cfg->vconfig, view,
				&server->viewlist, &server->kasplist,
				&server->keystorelist, cfg->actx, true, false,
				false, true);
	dns_view_freeze(view);

	isc_loopmgr_resume(named_g_loopmgr);

	if (result != ISC_R_SUCCESS) {
		TCHECK(putstr(text, "configure_zone failed: "));
		TCHECK(putstr(text, isc_result_totext(result)));
		goto cleanup;
	}

	/* Is it there yet? */
	if (redirect) {
		if (view->redirect == NULL) {
			CHECK(ISC_R_NOTFOUND);
		}
		dns_zone_attach(view->redirect, &zone);
	} else {
		CHECK(dns_view_findzone(view, name, DNS_ZTFIND_EXACT, &zone));
	}

#ifndef HAVE_LMDB
	/* Remove old zone from configuration (and NZF file if applicable) */
	if (added) {
		result = delete_zoneconf(view, cfg->add_parser, cfg->nzf_config,
					 dns_zone_getorigin(zone),
					 nzf_writeconf);
		if (result != ISC_R_SUCCESS) {
			TCHECK(putstr(text, "former zone configuration "
					    "not deleted: "));
			TCHECK(putstr(text, isc_result_totext(result)));
			goto cleanup;
		}
	}
#endif /* HAVE_LMDB */

	if (!added) {
		if (cfg->vconfig == NULL) {
			result = delete_zoneconf(
				view, cfg->conf_parser, cfg->config,
				dns_zone_getorigin(zone), NULL);
		} else {
			const cfg_obj_t *voptions = cfg_tuple_get(cfg->vconfig,
								  "options");
			result = delete_zoneconf(
				view, cfg->conf_parser, voptions,
				dns_zone_getorigin(zone), NULL);
		}

		if (result != ISC_R_SUCCESS) {
			TCHECK(putstr(text, "former zone configuration "
					    "not deleted: "));
			TCHECK(putstr(text, isc_result_totext(result)));
			goto cleanup;
		}
	}

	/* Load the zone from the master file if it needs reloading. */
	result = dns_zone_load(zone, true);

	/*
	 * Dynamic zones need no reloading, so we can pass this result.
	 */
	if (result == DNS_R_DYNAMIC) {
		result = ISC_R_SUCCESS;
	}

	if (result != ISC_R_SUCCESS) {
		dns_db_t *dbp = NULL;

		TCHECK(putstr(text, "failed to load zone '"));
		TCHECK(putstr(text, zname));
		TCHECK(putstr(text, "': "));
		TCHECK(putstr(text, isc_result_totext(result)));
		TCHECK(putstr(text, "\nThe zone is no longer being served. "));
		TCHECK(putstr(text, "Use 'rndc addzone' to correct\n"));
		TCHECK(putstr(text, "the problem and restore service."));

		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
			      "modzone failed; removing zone.");

		/* If the zone loaded partially, unload it */
		if (dns_zone_getdb(zone, &dbp) == ISC_R_SUCCESS) {
			dns_db_detach(&dbp);
			dns_zone_unload(zone);
		}

		/* Remove the zone from the zone table */
		dns_view_delzone(view, zone);
		goto cleanup;
	}

#ifndef HAVE_LMDB
	/* Store the new zone configuration; also in NZF if applicable */
	z = UNCONST(zoneobj);
	CHECK(cfg_parser_mapadd(cfg->add_parser, cfg->nzf_config, z, "zone"));
#endif /* HAVE_LMDB */

	if (added) {
#ifdef HAVE_LMDB
		CHECK(nzd_open(view, 0, &txn, &dbi));
		CHECK(nzd_save(&txn, dbi, zone, zoneobj));
#else  /* ifdef HAVE_LMDB */
		result = nzf_append(view, zoneobj);
		if (result != ISC_R_SUCCESS) {
			TCHECK(putstr(text, "\nNew zone config not saved: "));
			TCHECK(putstr(text, isc_result_totext(result)));
			goto cleanup;
		}
#endif /* HAVE_LMDB */

		TCHECK(putstr(text, "zone '"));
		TCHECK(putstr(text, zname));
		TCHECK(putstr(text, "' reconfigured."));
	} else {
		TCHECK(putstr(text, "zone '"));
		TCHECK(putstr(text, zname));
		TCHECK(putstr(text, "' must also be reconfigured in\n"));
		TCHECK(putstr(text, "named.conf to make changes permanent."));
	}

cleanup:

#ifndef HAVE_LMDB
	if (fp != NULL) {
		(void)isc_stdio_close(fp);
	}
#else  /* HAVE_LMDB */
	if (txn != NULL) {
		(void)nzd_close(&txn, false);
	}
	if (locked) {
		UNLOCK(&view->new_zone_lock);
	}
#endif /* HAVE_LMDB */

	if (zone != NULL) {
		dns_zone_detach(&zone);
	}

	return result;
}

/*
 * Act on an "addzone" or "modzone" command from the command channel.
 */
isc_result_t
named_server_changezone(named_server_t *server, char *command,
			isc_buffer_t **text) {
	isc_result_t result;
	bool addzone;
	bool redirect = false;
	ns_cfgctx_t *cfg = NULL;
	cfg_obj_t *zoneconf = NULL;
	const cfg_obj_t *zoneobj = NULL;
	const char *zonename;
	dns_view_t *view = NULL;
	isc_buffer_t buf;
	dns_fixedname_t fname;
	dns_name_t *dnsname;

	REQUIRE(text != NULL);

	if (strncasecmp(command, "add", 3) == 0) {
		addzone = true;
	} else {
		INSIST(strncasecmp(command, "mod", 3) == 0);
		addzone = false;
	}

	CHECK(newzone_parse(server, command, &view, &zoneconf, &zoneobj,
			    &redirect, text));

	/* Are we accepting new zones in this view? */
#ifdef HAVE_LMDB
	if (view->new_zone_db == NULL)
#else  /* ifdef HAVE_LMDB */
	if (view->new_zone_file == NULL)
#endif /* HAVE_LMDB */
	{
		(void)putstr(text, "Not allowing new zones in view '");
		(void)putstr(text, view->name);
		(void)putstr(text, "'");
		result = ISC_R_NOPERM;
		goto cleanup;
	}

	cfg = (ns_cfgctx_t *)view->new_zone_config;
	if (cfg == NULL) {
		result = ISC_R_FAILURE;
		goto cleanup;
	}

	zonename = cfg_obj_asstring(cfg_tuple_get(zoneobj, "name"));
	isc_buffer_constinit(&buf, zonename, strlen(zonename));
	isc_buffer_add(&buf, strlen(zonename));

	dnsname = dns_fixedname_initname(&fname);
	CHECK(dns_name_fromtext(dnsname, &buf, dns_rootname, 0, NULL));

	if (redirect) {
		if (!dns_name_equal(dnsname, dns_rootname)) {
			(void)putstr(text, "redirect zones must be called "
					   "\".\"");
			CHECK(ISC_R_FAILURE);
		}
	}

	if (addzone) {
		CHECK(do_addzone(server, cfg, view, dnsname, zoneconf, zoneobj,
				 redirect, text));
	} else {
		CHECK(do_modzone(server, cfg, view, dnsname, zonename, zoneobj,
				 redirect, text));
	}

	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		      "%s zone %s in view %s via %s",
		      addzone ? "added" : "updated", zonename, view->name,
		      addzone ? NAMED_COMMAND_ADDZONE : NAMED_COMMAND_MODZONE);

	/* Changing a zone counts as reconfiguration */
	named_g_configtime = isc_time_now();

cleanup:
	if (isc_buffer_usedlength(*text) > 0) {
		(void)putnull(text);
	}
	if (zoneconf != NULL) {
		cfg_obj_destroy(named_g_addparser, &zoneconf);
	}
	if (view != NULL) {
		dns_view_detach(&view);
	}

	return result;
}

static bool
inuse(const char *file, bool first, isc_buffer_t **text) {
	if (file != NULL && isc_file_exists(file)) {
		if (first) {
			(void)putstr(text, "The following files were in use "
					   "and may now be removed:\n");
		} else {
			(void)putstr(text, "\n");
		}
		(void)putstr(text, file);
		(void)putnull(text);
		return false;
	}
	return first;
}

typedef struct {
	dns_zone_t *zone;
	bool cleanup;
} ns_dzctx_t;

/*
 * Carry out a zone deletion scheduled by named_server_delzone().
 */
static void
rmzone(void *arg) {
	ns_dzctx_t *dz = (ns_dzctx_t *)arg;
	dns_zone_t *zone = NULL, *raw = NULL, *mayberaw = NULL;
	dns_catz_zone_t *catz = NULL;
	char zonename[DNS_NAME_FORMATSIZE];
	dns_view_t *view = NULL;
	ns_cfgctx_t *cfg = NULL;
	dns_db_t *dbp = NULL;
	bool added;
	isc_result_t result;
#ifdef HAVE_LMDB
	MDB_txn *txn = NULL;
	MDB_dbi dbi;
#endif /* ifdef HAVE_LMDB */

	REQUIRE(dz != NULL);

	/* Dig out configuration for this zone */
	zone = dz->zone;
	view = dns_zone_getview(zone);
	cfg = (ns_cfgctx_t *)view->new_zone_config;
	dns_name_format(dns_zone_getorigin(zone), zonename, sizeof(zonename));

	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		      "deleting zone %s in view %s via delzone", zonename,
		      view->name);

	/*
	 * Remove the zone from configuration (and NZF file if applicable)
	 * (If this is a catalog zone member then nzf_config can be NULL)
	 */
	added = dns_zone_getadded(zone);
	catz = dns_zone_get_parentcatz(zone);

	if (added && catz == NULL && cfg != NULL) {
#ifdef HAVE_LMDB
		/* Make sure we can open the NZD database */
		LOCK(&view->new_zone_lock);
		result = nzd_open(view, 0, &txn, &dbi);
		if (result != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "unable to open NZD database for '%s'",
				      view->new_zone_db);
		} else {
			result = nzd_save(&txn, dbi, zone, NULL);
		}

		if (result != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "unable to delete zone configuration: %s",
				      isc_result_totext(result));
		}

		if (txn != NULL) {
			(void)nzd_close(&txn, false);
		}
		UNLOCK(&view->new_zone_lock);
#else  /* ifdef HAVE_LMDB */
		result = delete_zoneconf(view, cfg->add_parser, cfg->nzf_config,
					 dns_zone_getorigin(zone),
					 nzf_writeconf);
		if (result != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "unable to delete zone configuration: %s",
				      isc_result_totext(result));
		}
#endif /* HAVE_LMDB */
	}

	if (!added && cfg != NULL) {
		if (cfg->vconfig != NULL) {
			const cfg_obj_t *voptions = cfg_tuple_get(cfg->vconfig,
								  "options");
			result = delete_zoneconf(
				view, cfg->conf_parser, voptions,
				dns_zone_getorigin(zone), NULL);
		} else {
			result = delete_zoneconf(
				view, cfg->conf_parser, cfg->config,
				dns_zone_getorigin(zone), NULL);
		}
		if (result != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "unable to delete zone configuration: %s",
				      isc_result_totext(result));
		}
	}

	/* Unload zone database */
	if (dns_zone_getdb(zone, &dbp) == ISC_R_SUCCESS) {
		dns_db_detach(&dbp);
		dns_zone_unload(zone);
	}

	/* Clean up stub/secondary zone files if requested to do so */
	dns_zone_getraw(zone, &raw);
	mayberaw = (raw != NULL) ? raw : zone;

	if (added && dz->cleanup) {
		const char *file;

		file = dns_zone_getfile(mayberaw);
		result = isc_file_remove(file);
		if (result != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
				      "file %s not removed: %s", file,
				      isc_result_totext(result));
		}

		file = dns_zone_getjournal(mayberaw);
		result = isc_file_remove(file);
		if (result != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
				      "file %s not removed: %s", file,
				      isc_result_totext(result));
		}

		if (zone != mayberaw) {
			file = dns_zone_getfile(zone);
			result = isc_file_remove(file);
			if (result != ISC_R_SUCCESS) {
				isc_log_write(
					named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
					NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
					"file %s not removed: %s", file,
					isc_result_totext(result));
			}

			file = dns_zone_getjournal(zone);
			result = isc_file_remove(file);
			if (result != ISC_R_SUCCESS) {
				isc_log_write(
					named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
					NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
					"file %s not removed: %s", file,
					isc_result_totext(result));
			}
		}
	}

	if (raw != NULL) {
		dns_zone_detach(&raw);
	}
	dns_zone_detach(&zone);
	isc_mem_put(named_g_mctx, dz, sizeof(*dz));
}

/*
 * Act on a "delzone" command from the command channel.
 */
isc_result_t
named_server_delzone(named_server_t *server, isc_lex_t *lex,
		     isc_buffer_t **text) {
	isc_result_t result, tresult;
	dns_zone_t *zone = NULL;
	dns_zone_t *raw = NULL;
	dns_zone_t *mayberaw;
	dns_view_t *view = NULL;
	char zonename[DNS_NAME_FORMATSIZE];
	bool cleanup = false;
	const char *ptr;
	bool added;
	ns_dzctx_t *dz = NULL;

	REQUIRE(text != NULL);

	/* Skip the command name. */
	ptr = next_token(lex, text);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	/* Find out what we are to do. */
	ptr = next_token(lex, text);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	if (strcmp(ptr, "-clean") == 0 || strcmp(ptr, "-clear") == 0) {
		cleanup = true;
		ptr = next_token(lex, text);
	}

	CHECK(zone_from_args(server, lex, ptr, &zone, zonename, text, false));
	if (zone == NULL) {
		result = ISC_R_UNEXPECTEDEND;
		goto cleanup;
	}

	INSIST(zonename != NULL);

	/* Is this a policy zone? */
	if (dns_zone_get_rpz_num(zone) != DNS_RPZ_INVALID_NUM) {
		TCHECK(putstr(text, "zone '"));
		TCHECK(putstr(text, zonename));
		TCHECK(putstr(text,
			      "' cannot be deleted: response-policy zone."));
		result = ISC_R_FAILURE;
		goto cleanup;
	}

	view = dns_zone_getview(zone);
	if (dns_zone_gettype(zone) == dns_zone_redirect) {
		dns_zone_detach(&view->redirect);
	} else {
		CHECK(dns_view_delzone(view, zone));
	}

	/* Send cleanup event */
	dz = isc_mem_get(named_g_mctx, sizeof(*dz));
	*dz = (ns_dzctx_t){
		.cleanup = cleanup,
	};
	dns_zone_attach(zone, &dz->zone);
	isc_async_run(dns_zone_getloop(zone), rmzone, dz);

	/* Inform user about cleaning up stub/secondary zone files */
	dns_zone_getraw(zone, &raw);
	mayberaw = (raw != NULL) ? raw : zone;

	added = dns_zone_getadded(zone);
	if (!added) {
		TCHECK(putstr(text, "zone '"));
		TCHECK(putstr(text, zonename));
		TCHECK(putstr(text, "' is no longer active and will be "
				    "deleted.\n"));
		TCHECK(putstr(text, "To keep it from returning "));
		TCHECK(putstr(text, "when the server is restarted, it\n"));
		TCHECK(putstr(text, "must also be removed from named.conf."));
	} else if (cleanup) {
		TCHECK(putstr(text, "zone '"));
		TCHECK(putstr(text, zonename));
		TCHECK(putstr(text, "' and associated files will be deleted."));
	} else if (dns_zone_gettype(mayberaw) == dns_zone_secondary ||
		   dns_zone_gettype(mayberaw) == dns_zone_mirror ||
		   dns_zone_gettype(mayberaw) == dns_zone_stub)
	{
		bool first;
		const char *file;

		TCHECK(putstr(text, "zone '"));
		TCHECK(putstr(text, zonename));
		TCHECK(putstr(text, "' will be deleted."));

		file = dns_zone_getfile(mayberaw);
		first = inuse(file, true, text);

		file = dns_zone_getjournal(mayberaw);
		first = inuse(file, first, text);

		if (zone != mayberaw) {
			file = dns_zone_getfile(zone);
			first = inuse(file, first, text);

			file = dns_zone_getjournal(zone);
			(void)inuse(file, first, text);
		}
	}

	isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
		      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		      "zone %s scheduled for removal via delzone", zonename);

	/* Removing a zone counts as reconfiguration */
	named_g_configtime = isc_time_now();

	result = ISC_R_SUCCESS;

cleanup:
	if (isc_buffer_usedlength(*text) > 0) {
		(void)putnull(text);
	}
	if (raw != NULL) {
		dns_zone_detach(&raw);
	}
	if (zone != NULL) {
		dns_zone_detach(&zone);
	}

	return result;
}

static const cfg_obj_t *
find_name_in_list_from_map(const cfg_obj_t *config,
			   const char *map_key_for_list, const char *name,
			   bool redirect) {
	const cfg_obj_t *list = NULL;
	const cfg_listelt_t *element;
	const cfg_obj_t *obj = NULL;
	dns_fixedname_t fixed1, fixed2;
	dns_name_t *name1 = NULL, *name2 = NULL;
	isc_result_t result;

	if (strcmp(map_key_for_list, "zone") == 0) {
		name1 = dns_fixedname_initname(&fixed1);
		name2 = dns_fixedname_initname(&fixed2);
		result = dns_name_fromstring(name1, name, dns_rootname, 0,
					     NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
	}

	cfg_map_get(config, map_key_for_list, &list);
	for (element = cfg_list_first(list); element != NULL;
	     element = cfg_list_next(element))
	{
		const char *vname;

		obj = cfg_listelt_value(element);
		INSIST(obj != NULL);
		vname = cfg_obj_asstring(cfg_tuple_get(obj, "name"));
		if (vname == NULL) {
			obj = NULL;
			continue;
		}

		if (name1 != NULL) {
			result = dns_name_fromstring(name2, vname, dns_rootname,
						     0, NULL);
			if (result == ISC_R_SUCCESS &&
			    dns_name_equal(name1, name2))
			{
				const cfg_obj_t *zoptions;
				const cfg_obj_t *typeobj = NULL;
				zoptions = cfg_tuple_get(obj, "options");

				if (zoptions != NULL) {
					cfg_map_get(zoptions, "type", &typeobj);
				}
				if (redirect && typeobj != NULL &&
				    strcasecmp(cfg_obj_asstring(typeobj),
					       "redirect") == 0)
				{
					break;
				} else if (!redirect) {
					break;
				}
			}
		} else if (strcasecmp(vname, name) == 0) {
			break;
		}

		obj = NULL;
	}

	return obj;
}

static void
emitzone(void *arg, const char *buf, int len) {
	ns_dzarg_t *dzarg = arg;
	isc_result_t result;

	REQUIRE(dzarg != NULL && ISC_MAGIC_VALID(dzarg, DZARG_MAGIC));
	result = putmem(dzarg->text, buf, len);
	if (result != ISC_R_SUCCESS && dzarg->result == ISC_R_SUCCESS) {
		dzarg->result = result;
	}
}

/*
 * Act on a "showzone" command from the command channel.
 */
isc_result_t
named_server_showzone(named_server_t *server, isc_lex_t *lex,
		      isc_buffer_t **text) {
	isc_result_t result;
	const cfg_obj_t *vconfig = NULL, *zconfig = NULL;
	char zonename[DNS_NAME_FORMATSIZE];
	const cfg_obj_t *map;
	dns_view_t *view = NULL;
	dns_zone_t *zone = NULL;
	ns_cfgctx_t *cfg = NULL;
#ifdef HAVE_LMDB
	cfg_obj_t *nzconfig = NULL;
#endif /* HAVE_LMDB */
	bool added, redirect;
	ns_dzarg_t dzarg;

	REQUIRE(text != NULL);

	/* Parse parameters */
	CHECK(zone_from_args(server, lex, NULL, &zone, zonename, text, true));
	if (zone == NULL) {
		result = ISC_R_UNEXPECTEDEND;
		goto cleanup;
	}

	redirect = dns_zone_gettype(zone) == dns_zone_redirect;
	added = dns_zone_getadded(zone);
	view = dns_zone_getview(zone);
	dns_zone_detach(&zone);

	cfg = (ns_cfgctx_t *)view->new_zone_config;
	if (cfg == NULL) {
		result = ISC_R_FAILURE;
		goto cleanup;
	}

	if (!added) {
		/* Find the view statement */
		vconfig = find_name_in_list_from_map(cfg->config, "view",
						     view->name, false);

		/* Find the zone statement */
		if (vconfig != NULL) {
			map = cfg_tuple_get(vconfig, "options");
		} else {
			map = cfg->config;
		}

		zconfig = find_name_in_list_from_map(map, "zone", zonename,
						     redirect);
	}

#ifndef HAVE_LMDB
	if (zconfig == NULL && cfg->nzf_config != NULL) {
		zconfig = find_name_in_list_from_map(cfg->nzf_config, "zone",
						     zonename, redirect);
	}
#else  /* HAVE_LMDB */
	if (zconfig == NULL) {
		const cfg_obj_t *zlist = NULL;
		CHECK(get_newzone_config(view, zonename, &nzconfig));
		CHECK(cfg_map_get(nzconfig, "zone", &zlist));
		if (!cfg_obj_islist(zlist)) {
			CHECK(ISC_R_FAILURE);
		}

		zconfig = cfg_listelt_value(cfg_list_first(zlist));
	}
#endif /* HAVE_LMDB */

	if (zconfig == NULL) {
		CHECK(ISC_R_NOTFOUND);
	}

	CHECK(putstr(text, "zone "));
	dzarg.magic = DZARG_MAGIC;
	dzarg.text = text;
	dzarg.result = ISC_R_SUCCESS;
	cfg_printx(zconfig, CFG_PRINTER_ONELINE, emitzone, &dzarg);
	CHECK(dzarg.result);

	CHECK(putstr(text, ";"));

	result = ISC_R_SUCCESS;

cleanup:
#ifdef HAVE_LMDB
	if (nzconfig != NULL) {
		cfg_obj_destroy(named_g_addparser, &nzconfig);
	}
#endif /* HAVE_LMDB */
	if (isc_buffer_usedlength(*text) > 0) {
		(void)putnull(text);
	}

	return result;
}

static void
newzone_cfgctx_destroy(void **cfgp) {
	ns_cfgctx_t *cfg;

	REQUIRE(cfgp != NULL && *cfgp != NULL);

	cfg = *cfgp;

	if (cfg->conf_parser != NULL) {
		if (cfg->config != NULL) {
			cfg_obj_destroy(cfg->conf_parser, &cfg->config);
		}
		if (cfg->vconfig != NULL) {
			cfg_obj_destroy(cfg->conf_parser, &cfg->vconfig);
		}
		cfg_parser_destroy(&cfg->conf_parser);
	}
	if (cfg->add_parser != NULL) {
		if (cfg->nzf_config != NULL) {
			cfg_obj_destroy(cfg->add_parser, &cfg->nzf_config);
		}
		cfg_parser_destroy(&cfg->add_parser);
	}

	if (cfg->actx != NULL) {
		cfg_aclconfctx_detach(&cfg->actx);
	}

	isc_mem_putanddetach(&cfg->mctx, cfg, sizeof(*cfg));
	*cfgp = NULL;
}

isc_result_t
named_server_signing(named_server_t *server, isc_lex_t *lex,
		     isc_buffer_t **text) {
	isc_result_t result = ISC_R_SUCCESS;
	dns_zone_t *zone = NULL;
	dns_name_t *origin;
	dns_db_t *db = NULL;
	dns_dbnode_t *node = NULL;
	dns_dbversion_t *version = NULL;
	dns_rdatatype_t privatetype;
	dns_rdataset_t privset;
	bool first = true;
	bool list = false, clear = false;
	bool chain = false;
	bool setserial = false;
	bool resalt = false;
	uint32_t serial = 0;
	char keystr[DNS_SECALG_FORMATSIZE + 7]; /* <5-digit keyid>/<alg> */
	unsigned short hash = 0, flags = 0, iter = 0, saltlen = 0;
	unsigned char salt[255];
	const char *ptr;
	size_t n;
	bool kasp = false;

	REQUIRE(text != NULL);

	dns_rdataset_init(&privset);

	/* Skip the command name. */
	ptr = next_token(lex, text);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	/* Find out what we are to do. */
	ptr = next_token(lex, text);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	if (strcasecmp(ptr, "-list") == 0) {
		list = true;
	} else if ((strcasecmp(ptr, "-clear") == 0) ||
		   (strcasecmp(ptr, "-clean") == 0))
	{
		clear = true;
		ptr = next_token(lex, text);
		if (ptr == NULL) {
			return ISC_R_UNEXPECTEDEND;
		}
		strlcpy(keystr, ptr, sizeof(keystr));
	} else if (strcasecmp(ptr, "-nsec3param") == 0) {
		char hashbuf[64], flagbuf[64], iterbuf[64];
		char nbuf[256];

		chain = true;
		ptr = next_token(lex, text);
		if (ptr == NULL) {
			return ISC_R_UNEXPECTEDEND;
		}

		if (strcasecmp(ptr, "none") == 0) {
			hash = 0;
		} else {
			strlcpy(hashbuf, ptr, sizeof(hashbuf));

			ptr = next_token(lex, text);
			if (ptr == NULL) {
				return ISC_R_UNEXPECTEDEND;
			}
			strlcpy(flagbuf, ptr, sizeof(flagbuf));

			ptr = next_token(lex, text);
			if (ptr == NULL) {
				return ISC_R_UNEXPECTEDEND;
			}
			strlcpy(iterbuf, ptr, sizeof(iterbuf));
			n = snprintf(nbuf, sizeof(nbuf), "%s %s %s", hashbuf,
				     flagbuf, iterbuf);
			if (n == sizeof(nbuf)) {
				return ISC_R_NOSPACE;
			}
			n = sscanf(nbuf, "%hu %hu %hu", &hash, &flags, &iter);
			if (n != 3U) {
				return ISC_R_BADNUMBER;
			}

			if (hash > 0xffU || flags > 0xffU ||
			    iter > dns_nsec3_maxiterations())
			{
				return ISC_R_RANGE;
			}

			ptr = next_token(lex, text);
			if (ptr == NULL) {
				return ISC_R_UNEXPECTEDEND;
			} else if (strcasecmp(ptr, "auto") == 0) {
				/* Auto-generate a random salt.
				 * XXXMUKS: This currently uses the
				 * minimum recommended length by RFC
				 * 5155 (64 bits). It should be made
				 * configurable.
				 */
				saltlen = 8;
				resalt = true;
			} else if (strcmp(ptr, "-") != 0) {
				isc_buffer_t buf;

				isc_buffer_init(&buf, salt, sizeof(salt));
				CHECK(isc_hex_decodestring(ptr, &buf));
				saltlen = isc_buffer_usedlength(&buf);
			}
		}
	} else if (strcasecmp(ptr, "-serial") == 0) {
		ptr = next_token(lex, text);
		if (ptr == NULL) {
			return ISC_R_UNEXPECTEDEND;
		}
		CHECK(isc_parse_uint32(&serial, ptr, 10));
		setserial = true;
	} else {
		CHECK(DNS_R_SYNTAX);
	}

	CHECK(zone_from_args(server, lex, NULL, &zone, NULL, text, false));
	if (zone == NULL) {
		CHECK(ISC_R_UNEXPECTEDEND);
	}

	if (dns_zone_getkasp(zone) != NULL) {
		kasp = true;
	}

	if (clear) {
		CHECK(dns_zone_keydone(zone, keystr));
		(void)putstr(text, "request queued");
		(void)putnull(text);
	} else if (chain && !kasp) {
		CHECK(dns_zone_setnsec3param(
			zone, (uint8_t)hash, (uint8_t)flags, iter,
			(uint8_t)saltlen, salt, true, resalt));
		(void)putstr(text, "nsec3param request queued");
		(void)putnull(text);
	} else if (setserial) {
		CHECK(dns_zone_setserial(zone, serial));
		(void)putstr(text, "serial request queued");
		(void)putnull(text);
	} else if (list) {
		privatetype = dns_zone_getprivatetype(zone);
		origin = dns_zone_getorigin(zone);
		CHECK(dns_zone_getdb(zone, &db));
		CHECK(dns_db_findnode(db, origin, false, &node));
		dns_db_currentversion(db, &version);

		result = dns_db_findrdataset(db, node, version, privatetype,
					     dns_rdatatype_none, 0, &privset,
					     NULL);
		if (result == ISC_R_NOTFOUND) {
			(void)putstr(text, "No signing records found");
			(void)putnull(text);
			result = ISC_R_SUCCESS;
			goto cleanup;
		}

		for (result = dns_rdataset_first(&privset);
		     result == ISC_R_SUCCESS;
		     result = dns_rdataset_next(&privset))
		{
			dns_rdata_t priv = DNS_RDATA_INIT;
			/*
			 * In theory, the output buffer could hold a full RDATA
			 * record which is 16-bit and then some text around
			 * it
			 */
			char output[UINT16_MAX + BUFSIZ];
			isc_buffer_t buf;

			dns_rdataset_current(&privset, &priv);

			isc_buffer_init(&buf, output, sizeof(output));
			CHECK(dns_private_totext(&priv, &buf));
			if (!first) {
				CHECK(putstr(text, "\n"));
			}
			CHECK(putstr(text, output));
			first = false;
		}
		if (!first) {
			CHECK(putnull(text));
		}

		if (result == ISC_R_NOMORE) {
			result = ISC_R_SUCCESS;
		}
	} else if (kasp) {
		(void)putstr(text, "zone uses dnssec-policy, use rndc dnssec "
				   "command instead");
		(void)putnull(text);
	}

cleanup:
	if (dns_rdataset_isassociated(&privset)) {
		dns_rdataset_disassociate(&privset);
	}
	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}
	if (version != NULL) {
		dns_db_closeversion(db, &version, false);
	}
	if (db != NULL) {
		dns_db_detach(&db);
	}
	if (zone != NULL) {
		dns_zone_detach(&zone);
	}

	return result;
}

static bool
argcheck(char *cmd, const char *full) {
	size_t l;

	if (cmd == NULL || cmd[0] != '-') {
		return false;
	}

	cmd++;
	l = strlen(cmd);
	if (l > strlen(full) || strncasecmp(cmd, full, l) != 0) {
		return false;
	}

	return true;
}

isc_result_t
named_server_dnssec(named_server_t *server, isc_lex_t *lex,
		    isc_buffer_t **text) {
	isc_result_t result = ISC_R_SUCCESS;
	dns_zone_t *zone = NULL;
	dns_kasp_t *kasp = NULL;
	dns_dnsseckeylist_t keys;
	dns_dnsseckey_t *key;
	char *ptr, *zonetext = NULL;
	const char *msg = NULL;
	/* variables for -checkds */
	bool checkds = false, dspublish = false;
	/* variables for -rollover */
	bool rollover = false;
	/* variables for -key */
	bool use_keyid = false;
	dns_keytag_t keyid = 0;
	uint8_t algorithm = 0;
	/* variables for -status */
	bool status = false;
	char output[4096];
	isc_stdtime_t now, when;
	isc_time_t timenow, timewhen;
	dns_db_t *db = NULL;
	dns_dbversion_t *version = NULL;

	REQUIRE(text != NULL);

	/* Skip the command name. */
	ptr = next_token(lex, text);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	/* Find out what we are to do. */
	ptr = next_token(lex, text);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	/* Initialize current time and key list. */
	timenow = isc_time_now();
	now = isc_time_seconds(&timenow);
	when = now;

	ISC_LIST_INIT(keys);

	if (strcasecmp(ptr, "-status") == 0) {
		status = true;
	} else if (strcasecmp(ptr, "-rollover") == 0) {
		rollover = true;
	} else if (strcasecmp(ptr, "-checkds") == 0) {
		checkds = true;
	} else {
		CHECK(DNS_R_SYNTAX);
	}

	if (rollover || checkds) {
		/* Check for options */
		for (;;) {
			ptr = next_token(lex, text);
			if (ptr == NULL) {
				msg = "Bad format";
				CHECK(ISC_R_UNEXPECTEDEND);
			} else if (argcheck(ptr, "alg")) {
				isc_consttextregion_t alg;
				ptr = next_token(lex, text);
				if (ptr == NULL) {
					msg = "No key algorithm specified";
					CHECK(ISC_R_UNEXPECTEDEND);
				}
				alg.base = ptr;
				alg.length = strlen(alg.base);
				result = dns_secalg_fromtext(
					&algorithm, (isc_textregion_t *)&alg);
				if (result != ISC_R_SUCCESS) {
					msg = "Bad algorithm";
					CHECK(DNS_R_SYNTAX);
				}
				continue;
			} else if (argcheck(ptr, "key")) {
				uint16_t id;
				ptr = next_token(lex, text);
				if (ptr == NULL) {
					msg = "No key identifier specified";
					CHECK(ISC_R_UNEXPECTEDEND);
				}
				CHECK(isc_parse_uint16(&id, ptr, 10));
				keyid = (dns_keytag_t)id;
				use_keyid = true;
				continue;
			} else if (argcheck(ptr, "when")) {
				uint32_t tw;
				ptr = next_token(lex, text);
				if (ptr == NULL) {
					msg = "No time specified";
					CHECK(ISC_R_UNEXPECTEDEND);
				}
				CHECK(dns_time32_fromtext(ptr, &tw));
				when = (isc_stdtime_t)tw;
				continue;
			} else if (ptr[0] == '-') {
				msg = "Unknown option";
				CHECK(DNS_R_SYNTAX);
			} else if (checkds) {
				/*
				 * No arguments provided, so we must be
				 * parsing "published|withdrawn".
				 */
				if (strcasecmp(ptr, "published") == 0) {
					dspublish = true;
				} else if (strcasecmp(ptr, "withdrawn") != 0) {
					CHECK(DNS_R_SYNTAX);
				}
			} else if (rollover) {
				/*
				 * No arguments provided, so we must be
				 * parsing the zone.
				 */
				zonetext = ptr;
			}
			break;
		}

		if (rollover && !use_keyid) {
			msg = "Key id is required when scheduling rollover";
			CHECK(DNS_R_SYNTAX);
		}

		if (algorithm > 0 && !use_keyid) {
			msg = "Key id is required when setting algorithm";
			CHECK(DNS_R_SYNTAX);
		}
	}

	/* Get zone. */
	CHECK(zone_from_args(server, lex, zonetext, &zone, NULL, text, false));
	if (zone == NULL) {
		msg = "Zone not found";
		CHECK(ISC_R_UNEXPECTEDEND);
	}

	/* Trailing garbage? */
	ptr = next_token(lex, text);
	if (ptr != NULL) {
		msg = "Too many arguments";
		CHECK(DNS_R_SYNTAX);
	}

	/* Get dnssec-policy. */
	kasp = dns_zone_getkasp(zone);
	if (kasp == NULL) {
		msg = "Zone does not have dnssec-policy";
		goto cleanup;
	}

	/* Get DNSSEC keys. */
	CHECK(dns_zone_getdb(zone, &db));
	dns_db_currentversion(db, &version);
	LOCK(&kasp->lock);
	result = dns_zone_getdnsseckeys(zone, db, version, now, &keys);
	UNLOCK(&kasp->lock);
	if (result != ISC_R_SUCCESS) {
		if (result != ISC_R_NOTFOUND) {
			goto cleanup;
		}
	}

	if (status) {
		/*
		 * Output the DNSSEC status of the key and signing policy.
		 */
		isc_result_t r;
		LOCK(&kasp->lock);
		r = dns_keymgr_status(kasp, &keys, now, &output[0],
				      sizeof(output));
		UNLOCK(&kasp->lock);
		CHECK(putstr(text, output));
		if (r != ISC_R_SUCCESS) {
			CHECK(putstr(text,
				     "\n\nStatus output is truncated..."));
		}
	} else if (checkds) {
		/*
		 * Mark DS record has been seen, so it may move to the
		 * rumoured state.
		 */
		char whenbuf[80];
		isc_time_set(&timewhen, when, 0);
		isc_time_formattimestamp(&timewhen, whenbuf, sizeof(whenbuf));
		isc_result_t ret;

		LOCK(&kasp->lock);
		if (use_keyid) {
			result = dns_keymgr_checkds_id(kasp, &keys, now, when,
						       dspublish, keyid,
						       (unsigned int)algorithm);
		} else {
			result = dns_keymgr_checkds(kasp, &keys, now, when,
						    dspublish);
		}
		UNLOCK(&kasp->lock);

		switch (result) {
		case ISC_R_SUCCESS:
			/*
			 * Rekey after checkds command because the next key
			 * event may have changed.
			 */
			dns_zone_rekey(zone, false);

			if (use_keyid) {
				char tagbuf[6];
				snprintf(tagbuf, sizeof(tagbuf), "%u", keyid);
				CHECK(putstr(text, "KSK "));
				CHECK(putstr(text, tagbuf));
				CHECK(putstr(text, ": "));
			}
			CHECK(putstr(text, "Marked DS as "));
			if (dspublish) {
				CHECK(putstr(text, "published "));
			} else {
				CHECK(putstr(text, "withdrawn "));
			}
			CHECK(putstr(text, "since "));
			CHECK(putstr(text, whenbuf));
			break;
		case DNS_R_TOOMANYKEYS:
			CHECK(putstr(text,
				     "Error: multiple possible keys found, "
				     "retry command with -key id"));
			break;
		default:
			ret = result;
			CHECK(putstr(text,
				     "Error executing checkds command: "));
			CHECK(putstr(text, isc_result_totext(ret)));
			break;
		}
	} else if (rollover) {
		/*
		 * Manually rollover a key.
		 */
		char whenbuf[80];
		isc_time_set(&timewhen, when, 0);
		isc_time_formattimestamp(&timewhen, whenbuf, sizeof(whenbuf));
		isc_result_t ret;

		LOCK(&kasp->lock);
		result = dns_keymgr_rollover(kasp, &keys, now, when, keyid,
					     (unsigned int)algorithm);
		UNLOCK(&kasp->lock);

		switch (result) {
		case ISC_R_SUCCESS:
			/*
			 * Rekey after rollover command because the next key
			 * event may have changed.
			 */
			dns_zone_rekey(zone, false);

			if (use_keyid) {
				char tagbuf[6];
				snprintf(tagbuf, sizeof(tagbuf), "%u", keyid);
				CHECK(putstr(text, "Key "));
				CHECK(putstr(text, tagbuf));
				CHECK(putstr(text, ": "));
			}
			CHECK(putstr(text, "Rollover scheduled on "));
			CHECK(putstr(text, whenbuf));
			break;
		case DNS_R_TOOMANYKEYS:
			CHECK(putstr(text,
				     "Error: multiple possible keys found, "
				     "retry command with -alg algorithm"));
			break;
		default:
			ret = result;
			CHECK(putstr(text,
				     "Error executing rollover command: "));
			CHECK(putstr(text, isc_result_totext(ret)));
			break;
		}
	}
	CHECK(putnull(text));

cleanup:
	if (msg != NULL) {
		(void)putstr(text, msg);
		(void)putnull(text);
	}

	if (version != NULL) {
		dns_db_closeversion(db, &version, false);
	}
	if (db != NULL) {
		dns_db_detach(&db);
	}

	while (!ISC_LIST_EMPTY(keys)) {
		key = ISC_LIST_HEAD(keys);
		ISC_LIST_UNLINK(keys, key, link);
		dns_dnsseckey_destroy(dns_zone_getmctx(zone), &key);
	}

	if (zone != NULL) {
		dns_zone_detach(&zone);
	}

	return result;
}

static isc_result_t
putmem(isc_buffer_t **b, const char *str, size_t len) {
	isc_result_t result;

	result = isc_buffer_reserve(*b, (unsigned int)len);
	if (result != ISC_R_SUCCESS) {
		return ISC_R_NOSPACE;
	}

	isc_buffer_putmem(*b, (const unsigned char *)str, (unsigned int)len);
	return ISC_R_SUCCESS;
}

static isc_result_t
putstr(isc_buffer_t **b, const char *str) {
	return putmem(b, str, strlen(str));
}

static isc_result_t
putuint8(isc_buffer_t **b, uint8_t val) {
	isc_result_t result;

	result = isc_buffer_reserve(*b, 1);
	if (result != ISC_R_SUCCESS) {
		return ISC_R_NOSPACE;
	}

	isc_buffer_putuint8(*b, val);
	return ISC_R_SUCCESS;
}

static isc_result_t
putnull(isc_buffer_t **b) {
	return putuint8(b, 0);
}

isc_result_t
named_server_zonestatus(named_server_t *server, isc_lex_t *lex,
			isc_buffer_t **text) {
	isc_result_t result = ISC_R_SUCCESS;
	dns_zone_t *zone = NULL, *raw = NULL, *mayberaw = NULL;
	const char *type, *file;
	char zonename[DNS_NAME_FORMATSIZE];
	uint32_t serial, signed_serial, nodes;
	char serbuf[16], sserbuf[16], nodebuf[16];
	char resignbuf[DNS_NAME_FORMATSIZE + DNS_RDATATYPE_FORMATSIZE + 2];
	char lbuf[ISC_FORMATHTTPTIMESTAMP_SIZE];
	char xbuf[ISC_FORMATHTTPTIMESTAMP_SIZE];
	char rbuf[ISC_FORMATHTTPTIMESTAMP_SIZE];
	char kbuf[ISC_FORMATHTTPTIMESTAMP_SIZE];
	char rtbuf[ISC_FORMATHTTPTIMESTAMP_SIZE];
	isc_time_t loadtime, expiretime, refreshtime;
	isc_time_t refreshkeytime, resigntime;
	dns_zonetype_t zonetype;
	bool dynamic = false, frozen = false;
	bool hasraw = false;
	bool secure, maintain, allow;
	dns_db_t *db = NULL, *rawdb = NULL;
	char **incfiles = NULL;
	int nfiles = 0;

	REQUIRE(text != NULL);

	isc_time_settoepoch(&loadtime);
	isc_time_settoepoch(&refreshtime);
	isc_time_settoepoch(&expiretime);
	isc_time_settoepoch(&refreshkeytime);
	isc_time_settoepoch(&resigntime);

	CHECK(zone_from_args(server, lex, NULL, &zone, zonename, text, true));
	if (zone == NULL) {
		result = ISC_R_UNEXPECTEDEND;
		goto cleanup;
	}

	/* Inline signing? */
	CHECK(dns_zone_getdb(zone, &db));
	dns_zone_getraw(zone, &raw);
	hasraw = (raw != NULL);
	if (hasraw) {
		mayberaw = raw;
		zonetype = dns_zone_gettype(raw);
		CHECK(dns_zone_getdb(raw, &rawdb));
	} else {
		mayberaw = zone;
		zonetype = dns_zone_gettype(zone);
	}

	type = dns_zonetype_name(zonetype);

	/* Serial number */
	result = dns_zone_getserial(mayberaw, &serial);

	/* This is to mirror old behavior with dns_zone_getserial */
	if (result != ISC_R_SUCCESS) {
		serial = 0;
	}

	snprintf(serbuf, sizeof(serbuf), "%u", serial);
	if (hasraw) {
		result = dns_zone_getserial(zone, &signed_serial);
		if (result != ISC_R_SUCCESS) {
			serial = 0;
		}
		snprintf(sserbuf, sizeof(sserbuf), "%u", signed_serial);
	}

	/* Database node count */
	nodes = dns_db_nodecount(hasraw ? rawdb : db, dns_dbtree_main);
	snprintf(nodebuf, sizeof(nodebuf), "%u", nodes);

	/* Security */
	secure = dns_db_issecure(db);
	allow = ((dns_zone_getkeyopts(zone) & DNS_ZONEKEY_ALLOW) != 0);
	maintain = ((dns_zone_getkeyopts(zone) & DNS_ZONEKEY_MAINTAIN) != 0);

	/* Master files */
	file = dns_zone_getfile(mayberaw);
	nfiles = dns_zone_getincludes(mayberaw, &incfiles);

	/* Load time */
	dns_zone_getloadtime(zone, &loadtime);
	isc_time_formathttptimestamp(&loadtime, lbuf, sizeof(lbuf));

	/* Refresh/expire times */
	if (zonetype == dns_zone_secondary || zonetype == dns_zone_mirror ||
	    zonetype == dns_zone_stub || zonetype == dns_zone_redirect)
	{
		dns_zone_getexpiretime(mayberaw, &expiretime);
		isc_time_formathttptimestamp(&expiretime, xbuf, sizeof(xbuf));
		dns_zone_getrefreshtime(mayberaw, &refreshtime);
		isc_time_formathttptimestamp(&refreshtime, rbuf, sizeof(rbuf));
	}

	/* Key refresh time */
	if (zonetype == dns_zone_primary ||
	    (zonetype == dns_zone_secondary && hasraw))
	{
		dns_zone_getrefreshkeytime(zone, &refreshkeytime);
		isc_time_formathttptimestamp(&refreshkeytime, kbuf,
					     sizeof(kbuf));
	}

	/* Dynamic? */
	if (zonetype == dns_zone_primary) {
		dynamic = dns_zone_isdynamic(mayberaw, true);
		frozen = dynamic && !dns_zone_isdynamic(mayberaw, false);
	}

	/* Next resign event */
	if (secure && (zonetype == dns_zone_primary ||
		       (zonetype == dns_zone_secondary && hasraw)))
	{
		dns_name_t *name;
		dns_fixedname_t fixed;
		isc_stdtime_t resign;
		dns_typepair_t typepair;

		name = dns_fixedname_initname(&fixed);

		result = dns_db_getsigningtime(db, &resign, name, &typepair);
		if (result == ISC_R_SUCCESS) {
			char namebuf[DNS_NAME_FORMATSIZE];
			char typebuf[DNS_RDATATYPE_FORMATSIZE];

			resign -= dns_zone_getsigresigninginterval(zone);

			dns_name_format(name, namebuf, sizeof(namebuf));
			dns_rdatatype_format(DNS_TYPEPAIR_COVERS(typepair),
					     typebuf, sizeof(typebuf));
			snprintf(resignbuf, sizeof(resignbuf), "%s/%s", namebuf,
				 typebuf);
			isc_time_set(&resigntime, resign, 0);
			isc_time_formathttptimestamp(&resigntime, rtbuf,
						     sizeof(rtbuf));
		}
	}

	/* Create text */
	CHECK(putstr(text, "name: "));
	CHECK(putstr(text, zonename));

	CHECK(putstr(text, "\ntype: "));
	CHECK(putstr(text, type));

	if (file != NULL) {
		int i;
		CHECK(putstr(text, "\nfiles: "));
		CHECK(putstr(text, file));
		for (i = 0; i < nfiles; i++) {
			CHECK(putstr(text, ", "));
			if (incfiles[i] != NULL) {
				CHECK(putstr(text, incfiles[i]));
			}
		}
	}

	CHECK(putstr(text, "\nserial: "));
	CHECK(putstr(text, serbuf));
	if (hasraw) {
		CHECK(putstr(text, "\nsigned serial: "));
		CHECK(putstr(text, sserbuf));
	}

	CHECK(putstr(text, "\nnodes: "));
	CHECK(putstr(text, nodebuf));

	if (!isc_time_isepoch(&loadtime)) {
		CHECK(putstr(text, "\nlast loaded: "));
		CHECK(putstr(text, lbuf));
	}

	if (!isc_time_isepoch(&refreshtime)) {
		CHECK(putstr(text, "\nnext refresh: "));
		CHECK(putstr(text, rbuf));
	}

	if (!isc_time_isepoch(&expiretime)) {
		CHECK(putstr(text, "\nexpires: "));
		CHECK(putstr(text, xbuf));
	}

	if (secure) {
		CHECK(putstr(text, "\nsecure: yes"));
		if (hasraw) {
			CHECK(putstr(text, "\ninline signing: yes"));
		} else {
			CHECK(putstr(text, "\ninline signing: no"));
		}
	} else {
		CHECK(putstr(text, "\nsecure: no"));
	}

	if (maintain) {
		CHECK(putstr(text, "\nkey maintenance: automatic"));
		if (!isc_time_isepoch(&refreshkeytime)) {
			CHECK(putstr(text, "\nnext key event: "));
			CHECK(putstr(text, kbuf));
		}
	} else if (allow) {
		CHECK(putstr(text, "\nkey maintenance: on command"));
	} else if (secure || hasraw) {
		CHECK(putstr(text, "\nkey maintenance: none"));
	}

	if (!isc_time_isepoch(&resigntime)) {
		CHECK(putstr(text, "\nnext resign node: "));
		CHECK(putstr(text, resignbuf));
		CHECK(putstr(text, "\nnext resign time: "));
		CHECK(putstr(text, rtbuf));
	}

	if (dynamic) {
		CHECK(putstr(text, "\ndynamic: yes"));
		if (frozen) {
			CHECK(putstr(text, "\nfrozen: yes"));
		} else {
			CHECK(putstr(text, "\nfrozen: no"));
		}
	} else {
		CHECK(putstr(text, "\ndynamic: no"));
	}

	CHECK(putstr(text, "\nreconfigurable via modzone: "));
	CHECK(putstr(text, dns_zone_getadded(zone) ? "yes" : "no"));

cleanup:
	/* Indicate truncated output if possible. */
	if (result == ISC_R_NOSPACE) {
		(void)putstr(text, "\n...");
	}
	if (result == ISC_R_SUCCESS || result == ISC_R_NOSPACE) {
		(void)putnull(text);
	}

	if (db != NULL) {
		dns_db_detach(&db);
	}
	if (rawdb != NULL) {
		dns_db_detach(&rawdb);
	}
	if (incfiles != NULL && mayberaw != NULL) {
		int i;
		isc_mem_t *mctx = dns_zone_getmctx(mayberaw);

		for (i = 0; i < nfiles; i++) {
			if (incfiles[i] != NULL) {
				isc_mem_free(mctx, incfiles[i]);
			}
		}
		isc_mem_free(mctx, incfiles);
	}
	if (raw != NULL) {
		dns_zone_detach(&raw);
	}
	if (zone != NULL) {
		dns_zone_detach(&zone);
	}
	return result;
}

isc_result_t
named_server_nta(named_server_t *server, isc_lex_t *lex, bool readonly,
		 isc_buffer_t **text) {
	dns_view_t *view;
	dns_ntatable_t *ntatable = NULL;
	isc_result_t result = ISC_R_SUCCESS;
	char *ptr, *nametext = NULL, *viewname;
	char namebuf[DNS_NAME_FORMATSIZE];
	char viewbuf[DNS_NAME_FORMATSIZE];
	isc_stdtime_t now, when;
	isc_time_t t;
	char tbuf[64];
	const char *msg = NULL;
	bool dump = false, force = false;
	dns_fixedname_t fn;
	const dns_name_t *ntaname;
	dns_name_t *fname;
	dns_ttl_t ntattl;
	bool ttlset = false, viewfound = false;
	dns_rdataclass_t rdclass = dns_rdataclass_in;
	bool first = true;

	REQUIRE(text != NULL);

	UNUSED(force);

	fname = dns_fixedname_initname(&fn);

	/* Skip the command name. */
	ptr = next_token(lex, text);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	for (;;) {
		/* Check for options */
		ptr = next_token(lex, text);
		if (ptr == NULL) {
			return ISC_R_UNEXPECTEDEND;
		}

		if (strcmp(ptr, "--") == 0) {
			break;
		} else if (argcheck(ptr, "dump")) {
			dump = true;
		} else if (argcheck(ptr, "remove")) {
			ntattl = 0;
			ttlset = true;
		} else if (argcheck(ptr, "force")) {
			force = true;
			continue;
		} else if (argcheck(ptr, "lifetime")) {
			isc_textregion_t tr;

			ptr = next_token(lex, text);
			if (ptr == NULL) {
				msg = "No lifetime specified";
				CHECK(ISC_R_UNEXPECTEDEND);
			}

			tr.base = ptr;
			tr.length = strlen(ptr);
			result = dns_ttl_fromtext(&tr, &ntattl);
			if (result != ISC_R_SUCCESS) {
				msg = "could not parse NTA lifetime";
				CHECK(result);
			}

			if (ntattl > 604800) {
				msg = "NTA lifetime cannot exceed one week";
				CHECK(ISC_R_RANGE);
			}

			ttlset = true;
			continue;
		} else if (argcheck(ptr, "class")) {
			isc_textregion_t tr;

			ptr = next_token(lex, text);
			if (ptr == NULL) {
				msg = "No class specified";
				CHECK(ISC_R_UNEXPECTEDEND);
			}

			tr.base = ptr;
			tr.length = strlen(ptr);
			CHECK(dns_rdataclass_fromtext(&rdclass, &tr));
			continue;
		} else if (ptr[0] == '-') {
			msg = "Unknown option";
			CHECK(DNS_R_SYNTAX);
		} else {
			nametext = ptr;
		}

		break;
	}

	/*
	 * If -dump was specified, list NTA's and return
	 */
	if (dump) {
		size_t last = 0;

		for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
		     view = ISC_LIST_NEXT(view, link))
		{
			if (ntatable != NULL) {
				dns_ntatable_detach(&ntatable);
			}
			result = dns_view_getntatable(view, &ntatable);
			if (result == ISC_R_NOTFOUND) {
				continue;
			}

			if (last != isc_buffer_usedlength(*text)) {
				CHECK(putstr(text, "\n"));
			}

			last = isc_buffer_usedlength(*text);

			CHECK(dns_ntatable_totext(ntatable, view->name, text));
		}
		CHECK(putnull(text));

		goto cleanup;
	}

	if (readonly) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_CONTROL, ISC_LOG_INFO,
			      "rejecting restricted control channel "
			      "NTA command");
		CHECK(ISC_R_FAILURE);
	}

	/* Get the NTA name if not found above. */
	if (nametext == NULL) {
		nametext = next_token(lex, text);
	}
	if (nametext == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	/* Copy nametext as it'll be overwritten by next_token() */
	strlcpy(namebuf, nametext, DNS_NAME_FORMATSIZE);

	if (strcmp(namebuf, ".") == 0) {
		ntaname = dns_rootname;
	} else {
		isc_buffer_t b;
		isc_buffer_init(&b, namebuf, strlen(namebuf));
		isc_buffer_add(&b, strlen(namebuf));
		CHECK(dns_name_fromtext(fname, &b, dns_rootname, 0, NULL));
		ntaname = fname;
	}

	/* Look for the view name. */
	viewname = next_token(lex, text);
	if (viewname != NULL) {
		strlcpy(viewbuf, viewname, DNS_NAME_FORMATSIZE);
		viewname = viewbuf;
	}

	if (next_token(lex, text) != NULL) {
		CHECK(DNS_R_SYNTAX);
	}

	now = isc_stdtime_now();

	isc_loopmgr_pause(named_g_loopmgr);
	for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
	     view = ISC_LIST_NEXT(view, link))
	{
		if (viewname != NULL && strcmp(view->name, viewname) != 0) {
			continue;
		}
		viewfound = true;

		if (view->rdclass != rdclass && rdclass != dns_rdataclass_any) {
			continue;
		}

		if (view->nta_lifetime == 0) {
			continue;
		}

		if (!ttlset) {
			ntattl = view->nta_lifetime;
		}

		if (ntatable != NULL) {
			dns_ntatable_detach(&ntatable);
		}

		result = dns_view_getntatable(view, &ntatable);
		if (result == ISC_R_NOTFOUND) {
			result = ISC_R_SUCCESS;
			continue;
		}

		result = dns_view_flushnode(view, ntaname, true);
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
			      "flush tree '%s' in cache view '%s': %s", namebuf,
			      view->name, isc_result_totext(result));

		if (ntattl != 0) {
			CHECK(dns_ntatable_add(ntatable, ntaname, force, now,
					       ntattl));

			when = now + ntattl;
			isc_time_set(&t, when, 0);
			isc_time_formattimestamp(&t, tbuf, sizeof(tbuf));

			if (!first) {
				CHECK(putstr(text, "\n"));
			}
			first = false;

			CHECK(putstr(text, "Negative trust anchor added: "));
			CHECK(putstr(text, namebuf));
			CHECK(putstr(text, "/"));
			CHECK(putstr(text, view->name));
			CHECK(putstr(text, ", expires "));
			CHECK(putstr(text, tbuf));

			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
				      "added NTA '%s' (%d sec) in view '%s'",
				      namebuf, ntattl, view->name);
		} else {
			bool wasremoved;

			result = dns_ntatable_delete(ntatable, ntaname);
			if (result == ISC_R_SUCCESS) {
				wasremoved = true;
			} else if (result == ISC_R_NOTFOUND) {
				wasremoved = false;
			} else {
				goto cleanup_exclusive;
			}

			if (!first) {
				CHECK(putstr(text, "\n"));
			}
			first = false;

			CHECK(putstr(text, "Negative trust anchor "));
			CHECK(putstr(text,
				     wasremoved ? "removed: " : "not found: "));
			CHECK(putstr(text, namebuf));
			CHECK(putstr(text, "/"));
			CHECK(putstr(text, view->name));

			if (wasremoved) {
				isc_log_write(
					named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
					NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
					"removed NTA '%s' in view %s", namebuf,
					view->name);
			}
		}

		result = dns_view_saventa(view);
		if (result != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "error writing NTA file "
				      "for view '%s': %s",
				      view->name, isc_result_totext(result));
		}
	}

	if (!viewfound) {
		msg = "No such view";
		result = ISC_R_NOTFOUND;
	} else {
		(void)putnull(text);
	}

cleanup_exclusive:
	isc_loopmgr_resume(named_g_loopmgr);

cleanup:

	if (msg != NULL) {
		(void)putstr(text, msg);
		(void)putnull(text);
	}

	if (ntatable != NULL) {
		dns_ntatable_detach(&ntatable);
	}
	return result;
}

isc_result_t
named_server_saventa(named_server_t *server) {
	dns_view_t *view;

	for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
	     view = ISC_LIST_NEXT(view, link))
	{
		isc_result_t result = dns_view_saventa(view);

		if (result != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "error writing NTA file "
				      "for view '%s': %s",
				      view->name, isc_result_totext(result));
		}
	}

	return ISC_R_SUCCESS;
}

isc_result_t
named_server_loadnta(named_server_t *server) {
	dns_view_t *view;

	for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
	     view = ISC_LIST_NEXT(view, link))
	{
		isc_result_t result = dns_view_loadnta(view);

		if ((result != ISC_R_SUCCESS) &&
		    (result != ISC_R_FILENOTFOUND) &&
		    (result != ISC_R_NOTFOUND))
		{
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "error loading NTA file "
				      "for view '%s': %s",
				      view->name, isc_result_totext(result));
		}
	}

	return ISC_R_SUCCESS;
}

static isc_result_t
mkey_refresh(dns_view_t *view, isc_buffer_t **text) {
	isc_result_t result;
	char msg[DNS_NAME_FORMATSIZE + 500] = "";

	snprintf(msg, sizeof(msg), "refreshing managed keys for '%s'",
		 view->name);
	CHECK(putstr(text, msg));
	CHECK(dns_zone_synckeyzone(view->managed_keys));

cleanup:
	return result;
}

static isc_result_t
mkey_destroy(dns_view_t *view, isc_buffer_t **text) {
	isc_result_t result;
	char msg[DNS_NAME_FORMATSIZE + 500] = "";
	const char *file = NULL;
	dns_db_t *dbp = NULL;
	dns_zone_t *mkzone = NULL;
	bool removed_a_file = false;

	if (view->managed_keys == NULL) {
		CHECK(ISC_R_NOTFOUND);
	}

	snprintf(msg, sizeof(msg), "destroying managed-keys database for '%s'",
		 view->name);
	CHECK(putstr(text, msg));

	isc_loopmgr_pause(named_g_loopmgr);

	/* Remove and clean up managed keys zone from view */
	mkzone = view->managed_keys;
	view->managed_keys = NULL;
	(void)dns_zone_flush(mkzone);

	/* Unload zone database */
	if (dns_zone_getdb(mkzone, &dbp) == ISC_R_SUCCESS) {
		dns_db_detach(&dbp);
		dns_zone_unload(mkzone);
	}

	/* Delete files */
	file = dns_zone_getfile(mkzone);
	result = isc_file_remove(file);
	if (result == ISC_R_SUCCESS) {
		removed_a_file = true;
	} else {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
			      "file %s not removed: %s", file,
			      isc_result_totext(result));
	}

	file = dns_zone_getjournal(mkzone);
	result = isc_file_remove(file);
	if (result == ISC_R_SUCCESS) {
		removed_a_file = true;
	} else {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_WARNING,
			      "file %s not removed: %s", file,
			      isc_result_totext(result));
	}

	if (!removed_a_file) {
		CHECK(putstr(text, "error: no files could be removed"));
		CHECK(ISC_R_FAILURE);
	}

	dns_zone_detach(&mkzone);
	result = ISC_R_SUCCESS;

cleanup:
	isc_loopmgr_resume(named_g_loopmgr);
	return result;
}

static isc_result_t
mkey_dumpzone(dns_view_t *view, isc_buffer_t **text) {
	isc_result_t result;
	dns_db_t *db = NULL;
	dns_dbversion_t *ver = NULL;
	dns_rriterator_t rrit;
	isc_stdtime_t now = isc_stdtime_now();
	dns_name_t *prevname = NULL;

	CHECK(dns_zone_getdb(view->managed_keys, &db));
	dns_db_currentversion(db, &ver);
	dns_rriterator_init(&rrit, db, ver, 0);
	for (result = dns_rriterator_first(&rrit); result == ISC_R_SUCCESS;
	     result = dns_rriterator_nextrrset(&rrit))
	{
		char buf[DNS_NAME_FORMATSIZE + 500];
		dns_name_t *name = NULL;
		dns_rdataset_t *kdset = NULL;
		dns_rdata_t rdata = DNS_RDATA_INIT;
		dns_rdata_keydata_t kd;
		uint32_t ttl;

		dns_rriterator_current(&rrit, &name, &ttl, &kdset, NULL);
		if (kdset == NULL || kdset->type != dns_rdatatype_keydata ||
		    !dns_rdataset_isassociated(kdset))
		{
			continue;
		}

		if (name != prevname) {
			char nbuf[DNS_NAME_FORMATSIZE];
			dns_name_format(name, nbuf, sizeof(nbuf));
			snprintf(buf, sizeof(buf), "\n\n    name: %s", nbuf);
			CHECK(putstr(text, buf));
		}

		for (result = dns_rdataset_first(kdset);
		     result == ISC_R_SUCCESS; result = dns_rdataset_next(kdset))
		{
			char alg[DNS_SECALG_FORMATSIZE];
			char tbuf[ISC_FORMATHTTPTIMESTAMP_SIZE];
			dns_keytag_t keyid;
			isc_region_t r;
			isc_time_t t;
			bool revoked;

			dns_rdata_reset(&rdata);
			dns_rdataset_current(kdset, &rdata);
			result = dns_rdata_tostruct(&rdata, &kd, NULL);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);

			dns_rdata_toregion(&rdata, &r);
			isc_region_consume(&r, 12);
			keyid = dst_region_computeid(&r);

			snprintf(buf, sizeof(buf), "\n    keyid: %u", keyid);
			CHECK(putstr(text, buf));

			dns_secalg_format(kd.algorithm, alg, sizeof(alg));
			snprintf(buf, sizeof(buf), "\n\talgorithm: %s", alg);
			CHECK(putstr(text, buf));

			revoked = ((kd.flags & DNS_KEYFLAG_REVOKE) != 0);
			snprintf(buf, sizeof(buf), "\n\tflags:%s%s%s",
				 revoked ? " REVOKE" : "",
				 ((kd.flags & DNS_KEYFLAG_KSK) != 0) ? " SEP"
								     : "",
				 (kd.flags == 0) ? " (none)" : "");
			CHECK(putstr(text, buf));

			isc_time_set(&t, kd.refresh, 0);
			isc_time_formathttptimestamp(&t, tbuf, sizeof(tbuf));
			snprintf(buf, sizeof(buf), "\n\tnext refresh: %s",
				 tbuf);
			CHECK(putstr(text, buf));

			if (kd.removehd != 0) {
				isc_time_set(&t, kd.removehd, 0);
				isc_time_formathttptimestamp(&t, tbuf,
							     sizeof(tbuf));
				snprintf(buf, sizeof(buf), "\n\tremove at: %s",
					 tbuf);
				CHECK(putstr(text, buf));
			}

			isc_time_set(&t, kd.addhd, 0);
			isc_time_formathttptimestamp(&t, tbuf, sizeof(tbuf));
			if (kd.addhd == 0) {
				snprintf(buf, sizeof(buf), "\n\tno trust");
			} else if (revoked) {
				snprintf(buf, sizeof(buf), "\n\ttrust revoked");
			} else if (kd.addhd <= now) {
				snprintf(buf, sizeof(buf),
					 "\n\ttrusted since: %s", tbuf);
			} else if (kd.addhd > now) {
				snprintf(buf, sizeof(buf),
					 "\n\ttrust pending: %s", tbuf);
			}
			CHECK(putstr(text, buf));
		}
	}

	if (result == ISC_R_NOMORE) {
		result = ISC_R_SUCCESS;
	}

cleanup:
	if (ver != NULL) {
		dns_rriterator_destroy(&rrit);
		dns_db_closeversion(db, &ver, false);
	}
	if (db != NULL) {
		dns_db_detach(&db);
	}

	return result;
}

static isc_result_t
mkey_status(dns_view_t *view, isc_buffer_t **text) {
	isc_result_t result;
	char msg[ISC_FORMATHTTPTIMESTAMP_SIZE];
	isc_time_t t;

	CHECK(putstr(text, "view: "));
	CHECK(putstr(text, view->name));

	CHECK(putstr(text, "\nnext scheduled event: "));

	dns_zone_getrefreshkeytime(view->managed_keys, &t);
	if (isc_time_isepoch(&t)) {
		CHECK(putstr(text, "never"));
	} else {
		isc_time_formathttptimestamp(&t, msg, sizeof(msg));
		CHECK(putstr(text, msg));
	}

	CHECK(mkey_dumpzone(view, text));

cleanup:
	return result;
}

isc_result_t
named_server_mkeys(named_server_t *server, isc_lex_t *lex,
		   isc_buffer_t **text) {
	char *cmd, *classtxt, *viewtxt = NULL;
	isc_result_t result = ISC_R_SUCCESS;
	dns_view_t *view = NULL;
	dns_rdataclass_t rdclass;
	char msg[DNS_NAME_FORMATSIZE + 500] = "";
	enum { NONE, STAT, REFRESH, SYNC, DESTROY } opt = NONE;
	bool found = false;
	bool first = true;

	REQUIRE(text != NULL);

	/* Skip rndc command name */
	cmd = next_token(lex, text);
	if (cmd == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	/* Get managed-keys subcommand */
	cmd = next_token(lex, text);
	if (cmd == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	if (strcasecmp(cmd, "status") == 0) {
		opt = STAT;
	} else if (strcasecmp(cmd, "refresh") == 0) {
		opt = REFRESH;
	} else if (strcasecmp(cmd, "sync") == 0) {
		opt = SYNC;
	} else if (strcasecmp(cmd, "destroy") == 0) {
		opt = DESTROY;
	} else {
		snprintf(msg, sizeof(msg), "unknown command '%s'", cmd);
		(void)putstr(text, msg);
		result = ISC_R_UNEXPECTED;
		goto cleanup;
	}

	/* Look for the optional class name. */
	classtxt = next_token(lex, text);
	if (classtxt != NULL) {
		isc_textregion_t r;
		r.base = classtxt;
		r.length = strlen(classtxt);
		result = dns_rdataclass_fromtext(&rdclass, &r);
		if (result != ISC_R_SUCCESS) {
			snprintf(msg, sizeof(msg), "unknown class '%s'",
				 classtxt);
			(void)putstr(text, msg);
			goto cleanup;
		}
		viewtxt = next_token(lex, text);
	}

	for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
	     view = ISC_LIST_NEXT(view, link))
	{
		if (viewtxt != NULL && (rdclass != view->rdclass ||
					strcmp(view->name, viewtxt) != 0))
		{
			continue;
		}

		if (view->managed_keys == NULL) {
			if (viewtxt != NULL) {
				snprintf(msg, sizeof(msg),
					 "view '%s': no managed keys", viewtxt);
				CHECK(putstr(text, msg));
				goto cleanup;
			} else {
				continue;
			}
		}

		found = true;

		switch (opt) {
		case REFRESH:
			if (!first) {
				CHECK(putstr(text, "\n"));
			}
			CHECK(mkey_refresh(view, text));
			break;
		case STAT:
			if (!first) {
				CHECK(putstr(text, "\n\n"));
			}
			CHECK(mkey_status(view, text));
			break;
		case SYNC:
			CHECK(dns_zone_flush(view->managed_keys));
			break;
		case DESTROY:
			if (!first) {
				CHECK(putstr(text, "\n"));
			}
			CHECK(mkey_destroy(view, text));
			break;
		default:
			UNREACHABLE();
		}

		if (viewtxt != NULL) {
			break;
		}
		first = false;
	}

	if (!found) {
		CHECK(putstr(text, "no views with managed keys"));
	}

cleanup:
	if (isc_buffer_usedlength(*text) > 0) {
		(void)putnull(text);
	}

	return result;
}

isc_result_t
named_server_dnstap(named_server_t *server, isc_lex_t *lex,
		    isc_buffer_t **text) {
#ifdef HAVE_DNSTAP
	char *ptr;
	isc_result_t result;
	bool reopen = false;
	int backups = 0;

	REQUIRE(text != NULL);

	if (server->dtenv == NULL) {
		return ISC_R_NOTFOUND;
	}

	/* Check the command name. */
	ptr = next_token(lex, text);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	/* "dnstap-reopen" was used in 9.11.0b1 */
	if (strcasecmp(ptr, "dnstap-reopen") == 0) {
		reopen = true;
	} else {
		ptr = next_token(lex, text);
		if (ptr == NULL) {
			return ISC_R_UNEXPECTEDEND;
		}
	}

	if (reopen || strcasecmp(ptr, "-reopen") == 0) {
		backups = ISC_LOG_ROLLNEVER;
	} else if (strcasecmp(ptr, "-roll") == 0) {
		unsigned int n;
		ptr = next_token(lex, text);
		if (ptr != NULL) {
			unsigned int u;
			n = sscanf(ptr, "%u", &u);
			if (n != 1U || u > INT_MAX) {
				return ISC_R_BADNUMBER;
			}
			backups = u;
		} else {
			backups = ISC_LOG_ROLLINFINITE;
		}
	} else {
		return DNS_R_SYNTAX;
	}

	result = dns_dt_reopen(server->dtenv, backups);
	return result;
#else  /* ifdef HAVE_DNSTAP */
	UNUSED(server);
	UNUSED(lex);
	UNUSED(text);
	return ISC_R_NOTIMPLEMENTED;
#endif /* ifdef HAVE_DNSTAP */
}

isc_result_t
named_server_tcptimeouts(isc_lex_t *lex, isc_buffer_t **text) {
	char *ptr;
	isc_result_t result = ISC_R_SUCCESS;
	uint32_t initial, idle, keepalive, advertised;
	char msg[128];

	/* Skip the command name. */
	ptr = next_token(lex, text);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	isc_nm_gettimeouts(named_g_netmgr, &initial, &idle, &keepalive,
			   &advertised);

	/* Look for optional arguments. */
	ptr = next_token(lex, NULL);
	if (ptr != NULL) {
		CHECK(isc_parse_uint32(&initial, ptr, 10));
		initial *= 100;
		if (initial > MAX_INITIAL_TIMEOUT) {
			CHECK(ISC_R_RANGE);
		}
		if (initial < MIN_INITIAL_TIMEOUT) {
			CHECK(ISC_R_RANGE);
		}

		ptr = next_token(lex, text);
		if (ptr == NULL) {
			return ISC_R_UNEXPECTEDEND;
		}
		CHECK(isc_parse_uint32(&idle, ptr, 10));
		idle *= 100;
		if (idle > MAX_IDLE_TIMEOUT) {
			CHECK(ISC_R_RANGE);
		}
		if (idle < MIN_IDLE_TIMEOUT) {
			CHECK(ISC_R_RANGE);
		}

		ptr = next_token(lex, text);
		if (ptr == NULL) {
			return ISC_R_UNEXPECTEDEND;
		}
		CHECK(isc_parse_uint32(&keepalive, ptr, 10));
		keepalive *= 100;
		if (keepalive > MAX_KEEPALIVE_TIMEOUT) {
			CHECK(ISC_R_RANGE);
		}
		if (keepalive < MIN_KEEPALIVE_TIMEOUT) {
			CHECK(ISC_R_RANGE);
		}

		ptr = next_token(lex, text);
		if (ptr == NULL) {
			return ISC_R_UNEXPECTEDEND;
		}
		CHECK(isc_parse_uint32(&advertised, ptr, 10));
		advertised *= 100;
		if (advertised > MAX_ADVERTISED_TIMEOUT) {
			CHECK(ISC_R_RANGE);
		}

		isc_nm_settimeouts(named_g_netmgr, initial, idle, keepalive,
				   advertised);
	}

	snprintf(msg, sizeof(msg), "tcp-initial-timeout=%u\n", initial / 100);
	CHECK(putstr(text, msg));
	snprintf(msg, sizeof(msg), "tcp-idle-timeout=%u\n", idle / 100);
	CHECK(putstr(text, msg));
	snprintf(msg, sizeof(msg), "tcp-keepalive-timeout=%u\n",
		 keepalive / 100);
	CHECK(putstr(text, msg));
	snprintf(msg, sizeof(msg), "tcp-advertised-timeout=%u",
		 advertised / 100);
	CHECK(putstr(text, msg));

cleanup:
	if (isc_buffer_usedlength(*text) > 0) {
		(void)putnull(text);
	}

	return result;
}

isc_result_t
named_server_servestale(named_server_t *server, isc_lex_t *lex,
			isc_buffer_t **text) {
	char *ptr, *classtxt, *viewtxt = NULL;
	char msg[128];
	dns_rdataclass_t rdclass = dns_rdataclass_in;
	dns_view_t *view;
	bool found = false;
	dns_stale_answer_t staleanswersok = dns_stale_answer_conf;
	bool wantstatus = false;
	isc_result_t result = ISC_R_SUCCESS;

	REQUIRE(text != NULL);

	/* Skip the command name. */
	ptr = next_token(lex, text);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	ptr = next_token(lex, NULL);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	if (!strcasecmp(ptr, "on") || !strcasecmp(ptr, "yes") ||
	    !strcasecmp(ptr, "enable") || !strcasecmp(ptr, "true"))
	{
		staleanswersok = dns_stale_answer_yes;
	} else if (!strcasecmp(ptr, "off") || !strcasecmp(ptr, "no") ||
		   !strcasecmp(ptr, "disable") || !strcasecmp(ptr, "false"))
	{
		staleanswersok = dns_stale_answer_no;
	} else if (strcasecmp(ptr, "reset") == 0) {
		staleanswersok = dns_stale_answer_conf;
	} else if (!strcasecmp(ptr, "check") || !strcasecmp(ptr, "status")) {
		wantstatus = true;
	} else {
		return DNS_R_SYNTAX;
	}

	/* Look for the optional class name. */
	classtxt = next_token(lex, text);
	if (classtxt != NULL) {
		isc_textregion_t r;

		/* Look for the optional view name. */
		viewtxt = next_token(lex, text);

		/*
		 * If 'classtext' is not a valid class then it us a view name.
		 */
		r.base = classtxt;
		r.length = strlen(classtxt);
		result = dns_rdataclass_fromtext(&rdclass, &r);
		if (result != ISC_R_SUCCESS) {
			if (viewtxt != NULL) {
				snprintf(msg, sizeof(msg), "unknown class '%s'",
					 classtxt);
				(void)putstr(text, msg);
				goto cleanup;
			}

			viewtxt = classtxt;
			classtxt = NULL;
		}
	}

	isc_loopmgr_pause(named_g_loopmgr);

	for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
	     view = ISC_LIST_NEXT(view, link))
	{
		dns_ttl_t stale_ttl = 0;
		uint32_t stale_refresh = 0;
		dns_db_t *db = NULL;

		if (classtxt != NULL && rdclass != view->rdclass) {
			continue;
		}

		if (viewtxt != NULL && strcmp(view->name, viewtxt) != 0) {
			continue;
		}

		if (!wantstatus) {
			view->staleanswersok = staleanswersok;
			found = true;
			continue;
		}

		db = NULL;
		dns_db_attach(view->cachedb, &db);
		(void)dns_db_getservestalettl(db, &stale_ttl);
		(void)dns_db_getservestalerefresh(db, &stale_refresh);
		dns_db_detach(&db);
		if (found) {
			CHECK(putstr(text, "\n"));
		}
		CHECK(putstr(text, view->name));
		CHECK(putstr(text, ": "));
		switch (view->staleanswersok) {
		case dns_stale_answer_yes:
			if (stale_ttl > 0) {
				CHECK(putstr(text, "stale cache "
						   "enabled; stale "
						   "answers enabled"));
			} else {
				CHECK(putstr(text, "stale cache disabled; "
						   "stale "
						   "answers unavailable"));
			}
			break;
		case dns_stale_answer_no:
			if (stale_ttl > 0) {
				CHECK(putstr(text, "stale cache "
						   "enabled; stale "
						   "answers disabled"));
			} else {
				CHECK(putstr(text, "stale cache disabled; "
						   "stale "
						   "answers unavailable"));
			}
			break;
		case dns_stale_answer_conf:
			if (view->staleanswersenable && stale_ttl > 0) {
				CHECK(putstr(text, "stale cache "
						   "enabled; stale "
						   "answers enabled"));
			} else if (stale_ttl > 0) {
				CHECK(putstr(text, "stale cache "
						   "enabled; stale "
						   "answers disabled"));
			} else {
				CHECK(putstr(text, "stale cache disabled; "
						   "stale "
						   "answers unavailable"));
			}
			break;
		}
		if (stale_ttl > 0) {
			snprintf(msg, sizeof(msg),
				 " (stale-answer-ttl=%u "
				 "max-stale-ttl=%u "
				 "stale-refresh-time=%u)",
				 view->staleanswerttl, stale_ttl,
				 stale_refresh);
			CHECK(putstr(text, msg));
		}
		found = true;
	}

	if (!found) {
		result = ISC_R_NOTFOUND;
	}

cleanup:
	isc_loopmgr_resume(named_g_loopmgr);

	if (isc_buffer_usedlength(*text) > 0) {
		(void)putnull(text);
	}

	return result;
}

isc_result_t
named_server_fetchlimit(named_server_t *server, isc_lex_t *lex,
			isc_buffer_t **text) {
	isc_result_t result = ISC_R_SUCCESS;
	dns_view_t *view = NULL;
	char *ptr = NULL, *viewname = NULL;
	bool first = true;
	dns_adb_t *adb = NULL;

	REQUIRE(text != NULL);

	/* Skip the command name. */
	ptr = next_token(lex, text);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	/* Look for the view name. */
	viewname = next_token(lex, text);
	for (view = ISC_LIST_HEAD(server->viewlist); view != NULL;
	     view = ISC_LIST_NEXT(view, link))
	{
		char tbuf[100];
		unsigned int used;
		uint32_t val;
		int s;

		if (view->rdclass != dns_rdataclass_in) {
			continue;
		}

		if (viewname != NULL && strcasecmp(view->name, viewname) != 0) {
			continue;
		}

		dns_view_getadb(view, &adb);
		if (adb == NULL) {
			continue;
		}

		if (!first) {
			CHECK(putstr(text, "\n"));
		}
		CHECK(putstr(text, "Rate limited servers, view "));
		CHECK(putstr(text, view->name));

		dns_adb_getquota(adb, &val, NULL, NULL, NULL, NULL);
		s = snprintf(tbuf, sizeof(tbuf),
			     " (fetches-per-server %u):", val);
		if (s < 0 || (unsigned int)s > sizeof(tbuf)) {
			CHECK(ISC_R_NOSPACE);
		}
		first = false;
		CHECK(putstr(text, tbuf));
		used = isc_buffer_usedlength(*text);
		CHECK(dns_adb_dumpquota(adb, text));
		if (used == isc_buffer_usedlength(*text)) {
			CHECK(putstr(text, "\n  None."));
		}

		CHECK(putstr(text, "\nRate limited servers, view "));
		CHECK(putstr(text, view->name));
		val = dns_resolver_getfetchesperzone(view->resolver);
		s = snprintf(tbuf, sizeof(tbuf),
			     " (fetches-per-zone %u):", val);
		if (s < 0 || (unsigned int)s > sizeof(tbuf)) {
			CHECK(ISC_R_NOSPACE);
		}
		CHECK(putstr(text, tbuf));
		used = isc_buffer_usedlength(*text);
		CHECK(dns_resolver_dumpquota(view->resolver, text));
		if (used == isc_buffer_usedlength(*text)) {
			CHECK(putstr(text, "\n  None."));
		}
		dns_adb_detach(&adb);
	}
cleanup:
	if (adb != NULL) {
		dns_adb_detach(&adb);
	}
	if (isc_buffer_usedlength(*text) > 0) {
		(void)putnull(text);
	}

	return result;
}

isc_result_t
named_server_skr(named_server_t *server, isc_lex_t *lex, isc_buffer_t **text) {
	isc_result_t result = ISC_R_SUCCESS;
	dns_zone_t *zone = NULL;
	dns_kasp_t *kasp = NULL;
	const char *ptr;
	char skrfile[PATH_MAX];

	/* Skip the command name. */
	ptr = next_token(lex, text);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	/* Find out what we are to do. */
	ptr = next_token(lex, text);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	if (strcasecmp(ptr, "-import") != 0) {
		CHECK(DNS_R_SYNTAX);
	}

	ptr = next_token(lex, NULL);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}
	(void)snprintf(skrfile, sizeof(skrfile), "%s", ptr);

	CHECK(zone_from_args(server, lex, NULL, &zone, NULL, text, false));
	if (zone == NULL) {
		CHECK(ISC_R_UNEXPECTEDEND);
	}
	kasp = dns_zone_getkasp(zone);
	if (kasp == NULL) {
		CHECK(putstr(text, "zone does not have a dnssec-policy"));
		CHECK(putnull(text));
		goto cleanup;
	}

	if (!dns_kasp_offlineksk(kasp)) {
		CHECK(putstr(text, "zone does not have offline-ksk enabled"));
		CHECK(putnull(text));
		goto cleanup;
	}

	result = dns_zone_import_skr(zone, skrfile);
	if (result != ISC_R_SUCCESS) {
		CHECK(putstr(text, "import failed: "));
		CHECK(putstr(text, isc_result_totext(result)));
		CHECK(putnull(text));
	} else {
		/* Schedule a rekey */
		dns_zone_rekey(zone, false);
	}

cleanup:
	if (zone != NULL) {
		dns_zone_detach(&zone);
	}

	return result;
}

isc_result_t
named_server_togglememprof(isc_lex_t *lex) {
	isc_result_t result = ISC_R_FAILURE;
	bool active;
	char *ptr;

	/* Skip the command name. */
	ptr = next_token(lex, NULL);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	}

	ptr = next_token(lex, NULL);
	if (ptr == NULL) {
		return ISC_R_UNEXPECTEDEND;
	} else if (!strcasecmp(ptr, "dump")) {
		result = memprof_dump();
		if (result != ISC_R_SUCCESS) {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "failed to dump memory profile");

		} else {
			isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
				      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
				      "memory profile dumped");
		}

		goto done;
	} else if (!strcasecmp(ptr, "on") || !strcasecmp(ptr, "yes") ||
		   !strcasecmp(ptr, "enable") || !strcasecmp(ptr, "true"))
	{
		active = true;
	} else if (!strcasecmp(ptr, "off") || !strcasecmp(ptr, "no") ||
		   !strcasecmp(ptr, "disable") || !strcasecmp(ptr, "false"))
	{
		active = false;
	} else {
		return DNS_R_SYNTAX;
	}

	result = memprof_toggle(active);
	if (result != ISC_R_SUCCESS) {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_ERROR,
			      "failed to toggle memory profiling");
	} else {
		isc_log_write(named_g_lctx, NAMED_LOGCATEGORY_GENERAL,
			      NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
			      "memory profiling %s",
			      active ? "enabled" : "disabled");
	}

done:
	return result;
}

#ifdef JEMALLOC_API_SUPPORTED
const char *
named_server_getmemprof(void) {
	memprof_status status = MEMPROF_ON;
	bool is_enabled;
	size_t len = sizeof(is_enabled);

	if (mallctl("config.prof", &is_enabled, &len, NULL, 0) != 0) {
		status = MEMPROF_FAILING;
		goto done;
	}

	INSIST(len == sizeof(is_enabled));

	if (!is_enabled) {
		status = MEMPROF_UNSUPPORTED;
		goto done;
	}

	if (mallctl("opt.prof", &is_enabled, &len, NULL, 0) != 0) {
		status = MEMPROF_FAILING;
		goto done;
	}

	INSIST(len == sizeof(is_enabled));

	if (!is_enabled) {
		status = MEMPROF_INACTIVE;
		goto done;
	}

	len = sizeof(is_enabled);
	if (mallctl("prof.active", &is_enabled, &len, NULL, 0) != 0) {
		status = MEMPROF_FAILING;
		goto done;
	}

	INSIST(len == sizeof(is_enabled));

	if (!is_enabled) {
		status = MEMPROF_OFF;
	}

done:
	return memprof_status_text[status];
}

#else  /* JEMALLOC_API_SUPPORTED */
const char *
named_server_getmemprof(void) {
	return memprof_status_text[MEMPROF_UNSUPPORTED];
}
#endif /* JEMALLOC_API_SUPPORTED */
