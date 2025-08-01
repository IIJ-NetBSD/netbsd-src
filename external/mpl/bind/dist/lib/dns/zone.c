/*	$NetBSD: zone.c,v 1.25 2025/07/17 19:01:45 christos Exp $	*/

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

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>

#include <isc/async.h>
#include <isc/atomic.h>
#include <isc/file.h>
#include <isc/hash.h>
#include <isc/hashmap.h>
#include <isc/hex.h>
#include <isc/loop.h>
#include <isc/md.h>
#include <isc/mutex.h>
#include <isc/overflow.h>
#include <isc/random.h>
#include <isc/ratelimiter.h>
#include <isc/refcount.h>
#include <isc/result.h>
#include <isc/rwlock.h>
#include <isc/serial.h>
#include <isc/stats.h>
#include <isc/stdtime.h>
#include <isc/strerr.h>
#include <isc/string.h>
#include <isc/thread.h>
#include <isc/tid.h>
#include <isc/timer.h>
#include <isc/tls.h>
#include <isc/util.h>

#include <dns/acl.h>
#include <dns/adb.h>
#include <dns/callbacks.h>
#include <dns/catz.h>
#include <dns/db.h>
#include <dns/dbiterator.h>
#include <dns/dlz.h>
#include <dns/dnssec.h>
#include <dns/journal.h>
#include <dns/kasp.h>
#include <dns/keydata.h>
#include <dns/keymgr.h>
#include <dns/keytable.h>
#include <dns/keyvalues.h>
#include <dns/log.h>
#include <dns/master.h>
#include <dns/masterdump.h>
#include <dns/message.h>
#include <dns/name.h>
#include <dns/nsec.h>
#include <dns/nsec3.h>
#include <dns/opcode.h>
#include <dns/peer.h>
#include <dns/private.h>
#include <dns/rcode.h>
#include <dns/rdata.h>
#include <dns/rdataclass.h>
#include <dns/rdatalist.h>
#include <dns/rdataset.h>
#include <dns/rdatasetiter.h>
#include <dns/rdatastruct.h>
#include <dns/rdatatype.h>
#include <dns/remote.h>
#include <dns/request.h>
#include <dns/resolver.h>
#include <dns/rriterator.h>
#include <dns/skr.h>
#include <dns/soa.h>
#include <dns/ssu.h>
#include <dns/stats.h>
#include <dns/time.h>
#include <dns/tsig.h>
#include <dns/ttl.h>
#include <dns/update.h>
#include <dns/xfrin.h>
#include <dns/zone.h>
#include <dns/zoneverify.h>
#include <dns/zt.h>

#include <dst/dst.h>

#include "zone_p.h"

#define ZONE_MAGIC	     ISC_MAGIC('Z', 'O', 'N', 'E')
#define DNS_ZONE_VALID(zone) ISC_MAGIC_VALID(zone, ZONE_MAGIC)

#define NOTIFY_MAGIC		 ISC_MAGIC('N', 't', 'f', 'y')
#define DNS_NOTIFY_VALID(notify) ISC_MAGIC_VALID(notify, NOTIFY_MAGIC)

#define CHECKDS_MAGIC		   ISC_MAGIC('C', 'h', 'D', 'S')
#define DNS_CHECKDS_VALID(checkds) ISC_MAGIC_VALID(checkds, CHECKDS_MAGIC)

#define STUB_MAGIC	     ISC_MAGIC('S', 't', 'u', 'b')
#define DNS_STUB_VALID(stub) ISC_MAGIC_VALID(stub, STUB_MAGIC)

#define ZONEMGR_MAGIC		ISC_MAGIC('Z', 'm', 'g', 'r')
#define DNS_ZONEMGR_VALID(stub) ISC_MAGIC_VALID(stub, ZONEMGR_MAGIC)

#define FORWARD_MAGIC		ISC_MAGIC('F', 'o', 'r', 'w')
#define DNS_FORWARD_VALID(load) ISC_MAGIC_VALID(load, FORWARD_MAGIC)

#define IO_MAGIC	   ISC_MAGIC('Z', 'm', 'I', 'O')
#define DNS_IO_VALID(load) ISC_MAGIC_VALID(load, IO_MAGIC)

#define KEYMGMT_MAGIC		ISC_MAGIC('M', 'g', 'm', 't')
#define DNS_KEYMGMT_VALID(load) ISC_MAGIC_VALID(load, KEYMGMT_MAGIC)

#define KEYFILEIO_MAGIC		  ISC_MAGIC('K', 'y', 'I', 'O')
#define DNS_KEYFILEIO_VALID(kfio) ISC_MAGIC_VALID(kfio, KEYFILEIO_MAGIC)

/*%
 * Ensure 'a' is at least 'min' but not more than 'max'.
 */
#define RANGE(a, min, max) (((a) < (min)) ? (min) : ((a) < (max) ? (a) : (max)))

#define NSEC3REMOVE(x) (((x) & DNS_NSEC3FLAG_REMOVE) != 0)

/*%
 * Key flags
 */
#define REVOKE(x) ((dst_key_flags(x) & DNS_KEYFLAG_REVOKE) != 0)
#define KSK(x)	  ((dst_key_flags(x) & DNS_KEYFLAG_KSK) != 0)
#define ID(x)	  dst_key_id(x)
#define ALG(x)	  dst_key_alg(x)

/*%
 * KASP flags
 */
#define KASP_LOCK(k)                \
	if ((k) != NULL) {          \
		LOCK(&((k)->lock)); \
	}

#define KASP_UNLOCK(k)                \
	if ((k) != NULL) {            \
		UNLOCK(&((k)->lock)); \
	}

/*
 * Default values.
 */
#define DNS_DEFAULT_IDLEIN  3600       /*%< 1 hour */
#define DNS_DEFAULT_IDLEOUT 3600       /*%< 1 hour */
#define MAX_XFER_TIME	    (2 * 3600) /*%< Documented default is 2 hours */
#define RESIGN_DELAY	    3600       /*%< 1 hour */

#ifndef DNS_MAX_EXPIRE
#define DNS_MAX_EXPIRE 14515200 /*%< 24 weeks */
#endif				/* ifndef DNS_MAX_EXPIRE */

#ifndef DNS_DUMP_DELAY
#define DNS_DUMP_DELAY 900 /*%< 15 minutes */
#endif			   /* ifndef DNS_DUMP_DELAY */

typedef struct dns_notify dns_notify_t;
typedef struct dns_checkds dns_checkds_t;
typedef struct dns_stub dns_stub_t;
typedef struct dns_load dns_load_t;
typedef struct dns_forward dns_forward_t;
typedef ISC_LIST(dns_forward_t) dns_forwardlist_t;
typedef struct dns_keymgmt dns_keymgmt_t;
typedef struct dns_signing dns_signing_t;
typedef ISC_LIST(dns_signing_t) dns_signinglist_t;
typedef struct dns_nsec3chain dns_nsec3chain_t;
typedef ISC_LIST(dns_nsec3chain_t) dns_nsec3chainlist_t;
typedef struct dns_nsfetch dns_nsfetch_t;
typedef struct dns_keyfetch dns_keyfetch_t;
typedef struct dns_asyncload dns_asyncload_t;
typedef struct dns_include dns_include_t;

#define DNS_ZONE_CHECKLOCK
#ifdef DNS_ZONE_CHECKLOCK
#define LOCK_ZONE(z)                  \
	do {                          \
		LOCK(&(z)->lock);     \
		INSIST(!(z)->locked); \
		(z)->locked = true;   \
	} while (0)
#define UNLOCK_ZONE(z)               \
	do {                         \
		INSIST((z)->locked); \
		(z)->locked = false; \
		UNLOCK(&(z)->lock);  \
	} while (0)
#define LOCKED_ZONE(z) ((z)->locked)
#define TRYLOCK_ZONE(result, z)                         \
	do {                                            \
		result = isc_mutex_trylock(&(z)->lock); \
		if (result == ISC_R_SUCCESS) {          \
			INSIST(!(z)->locked);           \
			(z)->locked = true;             \
		}                                       \
	} while (0)
#else /* ifdef DNS_ZONE_CHECKLOCK */
#define LOCK_ZONE(z)   LOCK(&(z)->lock)
#define UNLOCK_ZONE(z) UNLOCK(&(z)->lock)
#define LOCKED_ZONE(z) true
#define TRYLOCK_ZONE(result, z)                         \
	do {                                            \
		result = isc_mutex_trylock(&(z)->lock); \
	} while (0)
#endif /* ifdef DNS_ZONE_CHECKLOCK */

#define ZONEDB_INITLOCK(l)    isc_rwlock_init(l)
#define ZONEDB_DESTROYLOCK(l) isc_rwlock_destroy(l)
#define ZONEDB_LOCK(l, t)     RWLOCK((l), (t))
#define ZONEDB_UNLOCK(l, t)   RWUNLOCK((l), (t))

#define RETERR(x)                            \
	do {                                 \
		result = (x);                \
		if (result != ISC_R_SUCCESS) \
			goto failure;        \
	} while (0)

#ifdef ENABLE_AFL
extern bool dns_fuzzing_resolver;
#endif /* ifdef ENABLE_AFL */

/*%
 *	Hold key file IO locks.
 */
typedef struct dns_keyfileio {
	unsigned int magic;
	isc_mutex_t lock;
	isc_refcount_t references;
	dns_name_t *name;
	dns_fixedname_t fname;
} dns_keyfileio_t;

struct dns_keymgmt {
	unsigned int magic;
	isc_rwlock_t lock;
	isc_mem_t *mctx;
	isc_hashmap_t *table;
};

/*
 * Initial size of the keymgmt hash table.
 */
#define DNS_KEYMGMT_HASH_BITS 12

struct dns_zone {
	/* Unlocked */
	unsigned int magic;
	isc_mutex_t lock;
#ifndef _LP64
	isc_mutex_t atomic_lock;
#endif
#ifdef DNS_ZONE_CHECKLOCK
	bool locked;
#endif /* ifdef DNS_ZONE_CHECKLOCK */
	isc_mem_t *mctx;
	isc_refcount_t references;

	isc_rwlock_t dblock;
	dns_db_t *db; /* Locked by dblock */

	unsigned int tid;

	/* Locked */
	dns_zonemgr_t *zmgr;
	ISC_LINK(dns_zone_t) link; /* Used by zmgr. */
	isc_loop_t *loop;
	isc_timer_t *timer;
	isc_refcount_t irefs;
	dns_name_t origin;
	char *masterfile;
	const FILE *stream;		     /* loading from a stream? */
	ISC_LIST(dns_include_t) includes;    /* Include files */
	ISC_LIST(dns_include_t) newincludes; /* Loading */
	unsigned int nincludes;
	dns_masterformat_t masterformat;
	const dns_master_style_t *masterstyle;
	char *journal;
	int32_t journalsize;
	dns_rdataclass_t rdclass;
	dns_zonetype_t type;
#ifdef _LP64
	atomic_uint_fast64_t flags;
	atomic_uint_fast64_t options;
#else
	uint64_t flags;
	uint64_t options;
#endif
	unsigned int db_argc;
	char **db_argv;
	isc_time_t expiretime;
	isc_time_t refreshtime;
	isc_time_t dumptime;
	isc_time_t loadtime;
	isc_time_t notifytime;
	isc_time_t resigntime;
	isc_time_t keywarntime;
	isc_time_t signingtime;
	isc_time_t nsec3chaintime;
	isc_time_t refreshkeytime;
	isc_time_t xfrintime;
	uint32_t refreshkeyinterval;
	uint32_t refreshkeycount;
	uint32_t refresh;
	uint32_t retry;
	uint32_t expire;
	uint32_t minimum;
	isc_stdtime_t key_expiry;
	isc_stdtime_t log_key_expired_timer;
	char *keydirectory;
	dns_keyfileio_t *kfio;
	dns_keystorelist_t *keystores;
	dns_xfrin_t *xfr;

	uint32_t maxrefresh;
	uint32_t minrefresh;
	uint32_t maxretry;
	uint32_t minretry;

	uint32_t maxrecords;
	uint32_t maxrrperset;
	uint32_t maxtypepername;

	dns_remote_t primaries;

	dns_remote_t parentals;
	dns_dnsseckeylist_t checkds_ok;
	dns_checkdstype_t checkdstype;
	uint32_t nsfetchcount;
	uint32_t parent_nscount;

	dns_remote_t notify;
	dns_notifytype_t notifytype;
	isc_sockaddr_t notifyfrom;
	isc_sockaddr_t notifysrc4;
	isc_sockaddr_t notifysrc6;
	isc_sockaddr_t parentalsrc4;
	isc_sockaddr_t parentalsrc6;
	isc_sockaddr_t xfrsource4;
	isc_sockaddr_t xfrsource6;
	isc_sockaddr_t sourceaddr;
	dns_tsigkey_t *tsigkey;	    /* key used for xfr */
	dns_transport_t *transport; /* transport used for xfr */
	/* Access Control Lists */
	dns_acl_t *update_acl;
	dns_acl_t *forward_acl;
	dns_acl_t *notify_acl;
	dns_acl_t *query_acl;
	dns_acl_t *queryon_acl;
	dns_acl_t *xfr_acl;
	bool update_disabled;
	bool zero_no_soa_ttl;
	dns_severity_t check_names;
	ISC_LIST(dns_notify_t) notifies;
	ISC_LIST(dns_checkds_t) checkds_requests;
	dns_request_t *request;
	dns_loadctx_t *loadctx;
	dns_dumpctx_t *dumpctx;
	uint32_t minxfrratebytesin;
	uint32_t minxfrratesecondsin;
	uint32_t maxxfrin;
	uint32_t maxxfrout;
	uint32_t idlein;
	uint32_t idleout;
	dns_ssutable_t *ssutable;
	uint32_t sigvalidityinterval;
	uint32_t keyvalidityinterval;
	uint32_t sigresigninginterval;
	dns_view_t *view;
	dns_view_t *prev_view;
	dns_kasp_t *kasp;
	dns_kasp_t *defaultkasp;
	dns_dnsseckeylist_t keyring;
	dns_checkmxfunc_t checkmx;
	dns_checksrvfunc_t checksrv;
	dns_checknsfunc_t checkns;
	/*%
	 * Zones in certain states such as "waiting for zone transfer"
	 * or "zone transfer in progress" are kept on per-state linked lists
	 * in the zone manager using the 'statelink' field.  The 'statelist'
	 * field points at the list the zone is currently on.  It the zone
	 * is not on any such list, statelist is NULL.
	 */
	ISC_LINK(dns_zone_t) statelink;
	dns_zonelist_t *statelist;
	/*%
	 * Statistics counters about zone management.
	 */
	isc_stats_t *stats;
	/*%
	 * Optional per-zone statistics counters.  Counted outside of this
	 * module.
	 */
	dns_zonestat_level_t statlevel;
	bool requeststats_on;
	isc_stats_t *requeststats;
	dns_stats_t *rcvquerystats;
	dns_stats_t *dnssecsignstats;
	uint32_t notifydelay;
	uint32_t notifydefer;
	dns_isselffunc_t isself;
	void *isselfarg;

	char *strnamerd;
	char *strname;
	char *strrdclass;
	char *strviewname;

	/*%
	 * Serial number for deferred journal compaction.
	 */
	uint32_t compact_serial;
	/*%
	 * Keys that are signing the zone for the first time.
	 */
	dns_signinglist_t signing;
	dns_nsec3chainlist_t nsec3chain;
	/*%
	 * List of outstanding NSEC3PARAM change requests.
	 */
	ISC_LIST(struct np3) setnsec3param_queue;
	/*%
	 * Signing / re-signing quantum stopping parameters.
	 */
	uint32_t signatures;
	uint32_t nodes;
	dns_rdatatype_t privatetype;

	/*%
	 * Autosigning/key-maintenance options
	 */
#ifdef _LP64
	atomic_uint_fast64_t keyopts;
#else
	uint64_t keyopts;
#endif

	/*%
	 * True if added by "rndc addzone"
	 */
	bool added;

	/*%
	 * True if added by automatically by named.
	 */
	bool automatic;

	/*%
	 * response policy data to be relayed to the database
	 */
	dns_rpz_zones_t *rpzs;
	dns_rpz_num_t rpz_num;

	/*%
	 * catalog zone data
	 */
	dns_catz_zones_t *catzs;

	/*%
	 * parent catalog zone
	 */
	dns_catz_zone_t *parentcatz;

	/*%
	 * Serial number update method.
	 */
	dns_updatemethod_t updatemethod;

	/*%
	 * whether ixfr is requested
	 */
	bool requestixfr;
	uint32_t ixfr_ratio;

	/*%
	 * whether EDNS EXPIRE is requested
	 */
	bool requestexpire;

	/*%
	 * Outstanding forwarded UPDATE requests.
	 */
	dns_forwardlist_t forwards;

	dns_zone_t *raw;
	dns_zone_t *secure;

	bool sourceserialset;
	uint32_t sourceserial;

	/*%
	 * soa and maximum zone ttl
	 */
	dns_ttl_t soattl;
	dns_ttl_t maxttl;

	/*
	 * Inline zone signing state.
	 */
	dns_diff_t rss_diff;
	dns_dbversion_t *rss_newver;
	dns_dbversion_t *rss_oldver;
	dns_db_t *rss_db;
	dns_zone_t *rss_raw;
	struct rss *rss;
	dns_update_state_t *rss_state;

	isc_stats_t *gluecachestats;

	/*%
	 * Offline KSK signed key responses.
	 */
	dns_skr_t *skr;
	dns_skrbundle_t *skrbundle;
};

#define zonediff_init(z, d)                \
	do {                               \
		dns__zonediff_t *_z = (z); \
		(_z)->diff = (d);          \
		(_z)->offline = false;     \
	} while (0)
#ifdef _LP64
#define ISC_ZONE_GET(z, f) atomic_load_relaxed(&(z)->f)
#define ISC_ZONE_SET(z, f, o) atomic_fetch_or(&(z)->f, (o))
#define ISC_ZONE_CLR(z, f, o) atomic_fetch_and(&(z)->f, ~(o))
#else
#define ISC_ZONE_GET(z, f) \
	({ \
		isc_mutex_lock(&(z)->atomic_lock); \
		uint64_t x = (z)->f; \
		isc_mutex_unlock(&(z)->atomic_lock); \
		x; \
	})
#define ISC_ZONE_SET(z, f, o) \
	({ \
		isc_mutex_lock(&(z)->atomic_lock); \
		uint64_t x = ((z)->f | (o)); \
		isc_mutex_unlock(&(z)->atomic_lock); \
		x; \
	})
#define ISC_ZONE_CLR(z, f, o) \
	({ \
		isc_mutex_lock(&(z)->atomic_lock); \
		uint64_t x = ((z)->f & ~(o)); \
		isc_mutex_unlock(&(z)->atomic_lock); \
		x; \
	})
#endif
#define ISC_ZONE_TEST(z, f, o) ((ISC_ZONE_GET(z, f) & (o)) != 0)

#define DNS_ZONE_FLAG(z, f)    ISC_ZONE_TEST(z, flags, f)
#define DNS_ZONE_SETFLAG(z, f) ISC_ZONE_SET(z, flags, f)
#define DNS_ZONE_CLRFLAG(z, f) ISC_ZONE_CLR(z, flags, f)
typedef enum {
	DNS_ZONEFLG_REFRESH = 0x00000001U,     /*%< refresh check in progress */
	DNS_ZONEFLG_NEEDDUMP = 0x00000002U,    /*%< zone need consolidation */
	DNS_ZONEFLG_USEVC = 0x00000004U,       /*%< use tcp for refresh query */
	DNS_ZONEFLG_DUMPING = 0x00000008U,     /*%< a dump is in progress */
	DNS_ZONEFLG_HASINCLUDE = 0x00000010U,  /*%< $INCLUDE in zone file */
	DNS_ZONEFLG_LOADED = 0x00000020U,      /*%< database has loaded */
	DNS_ZONEFLG_EXITING = 0x00000040U,     /*%< zone is being destroyed */
	DNS_ZONEFLG_EXPIRED = 0x00000080U,     /*%< zone has expired */
	DNS_ZONEFLG_NEEDREFRESH = 0x00000100U, /*%< refresh check needed */
	DNS_ZONEFLG_UPTODATE = 0x00000200U,    /*%< zone contents are
						* up-to-date */
	DNS_ZONEFLG_NEEDNOTIFY = 0x00000400U,  /*%< need to send out notify
						* messages */
	DNS_ZONEFLG_FIXJOURNAL = 0x00000800U,  /*%< journal file had
						* recoverable error,
						* needs rewriting */
	DNS_ZONEFLG_NOPRIMARIES = 0x00001000U, /*%< an attempt to refresh a
						* zone with no primaries
						* occurred */
	DNS_ZONEFLG_LOADING = 0x00002000U,     /*%< load from disk in progress*/
	DNS_ZONEFLG_HAVETIMERS = 0x00004000U,  /*%< timer values have been set
						* from SOA (if not set, we
						* are still using
						* default timer values) */
	DNS_ZONEFLG_FORCEXFER = 0x00008000U,   /*%< Force a zone xfer */
	DNS_ZONEFLG_NOREFRESH = 0x00010000U,
	DNS_ZONEFLG_DIALNOTIFY = 0x00020000U,
	DNS_ZONEFLG_DIALREFRESH = 0x00040000U,
	DNS_ZONEFLG_SHUTDOWN = 0x00080000U,
	DNS_ZONEFLG_NOIXFR = 0x00100000U, /*%< IXFR failed, force AXFR */
	DNS_ZONEFLG_FLUSH = 0x00200000U,
	DNS_ZONEFLG_NOEDNS = 0x00400000U,
	DNS_ZONEFLG_USEALTXFRSRC = 0x00800000U, /*%< Obsoleted. */
	DNS_ZONEFLG_SOABEFOREAXFR = 0x01000000U,
	DNS_ZONEFLG_NEEDCOMPACT = 0x02000000U,
	DNS_ZONEFLG_REFRESHING = 0x04000000U, /*%< Refreshing keydata */
	DNS_ZONEFLG_THAW = 0x08000000U,
	DNS_ZONEFLG_LOADPENDING = 0x10000000U, /*%< Loading scheduled */
	DNS_ZONEFLG_NODELAY = 0x20000000U,
	DNS_ZONEFLG_SENDSECURE = 0x40000000U,
	DNS_ZONEFLG_NEEDSTARTUPNOTIFY = 0x80000000U, /*%< need to send out
						      * notify due to the zone
						      * just being loaded for
						      * the first time. */
	DNS_ZONEFLG_NOTIFYNODEFER = 0x100000000U,    /*%< ignore the
						      * notify-defer option. */
	DNS_ZONEFLG_NOTIFYDEFERRED = 0x200000000U,   /*%< notify was deferred
						      * according to the
						      * notify-defer option. */
	DNS_ZONEFLG_FIRSTREFRESH = 0x400000000U, /*%< First refresh pending */
	DNS_ZONEFLG___MAX = UINT64_MAX, /* trick to make the ENUM 64-bit wide */
} dns_zoneflg_t;


#define DNS_ZONE_OPTION(z, o)		ISC_ZONE_TEST(z, options, o)
#define DNS_ZONE_SETOPTION(z, o)	ISC_ZONE_SET(z, options, o)
#define DNS_ZONE_CLROPTION(z, o)	ISC_ZONE_CLR(z, options, o)
#define DNS_ZONEKEY_OPTION(z, o)	ISC_ZONE_TEST(z, keyopts, o)
#define DNS_ZONEKEY_SETOPTION(z, o)	ISC_ZONE_SET(z, keyopts, o)
#define DNS_ZONEKEY_CLROPTION(z, o)	ISC_ZONE_CLR(z, keyopts, o)


/* Flags for zone_load() */
typedef enum {
	DNS_ZONELOADFLAG_NOSTAT = 0x00000001U, /* Do not stat() master files */
	DNS_ZONELOADFLAG_THAW = 0x00000002U,   /* Thaw the zone on successful
						* load. */
} dns_zoneloadflag_t;

#define UNREACH_CACHE_SIZE 10U
#define UNREACH_HOLD_TIME  600 /* 10 minutes */

#define CHECK(op)                            \
	do {                                 \
		result = (op);               \
		if (result != ISC_R_SUCCESS) \
			goto failure;        \
	} while (0)

struct dns_unreachable {
	isc_sockaddr_t remote;
	isc_sockaddr_t local;
	atomic_uint_fast32_t expire;
	atomic_uint_fast32_t last;
	uint32_t count;
};

struct dns_zonemgr {
	unsigned int magic;
	isc_mem_t *mctx;
	isc_refcount_t refs;
	isc_loopmgr_t *loopmgr;
	isc_nm_t *netmgr;
	uint32_t workers;
	isc_mem_t **mctxpool;
	isc_ratelimiter_t *checkdsrl;
	isc_ratelimiter_t *notifyrl;
	isc_ratelimiter_t *refreshrl;
	isc_ratelimiter_t *startupnotifyrl;
	isc_ratelimiter_t *startuprefreshrl;
	isc_rwlock_t rwlock;
	isc_rwlock_t urlock;

	/* Locked by rwlock. */
	dns_zonelist_t zones;
	dns_zonelist_t waiting_for_xfrin;
	dns_zonelist_t xfrin_in_progress;

	/* Configuration data. */
	uint32_t transfersin;
	uint32_t transfersperns;
	unsigned int checkdsrate;
	unsigned int notifyrate;
	unsigned int startupnotifyrate;
	unsigned int serialqueryrate;
	unsigned int startupserialqueryrate;

	/* Locked by urlock. */
	/* LRU cache */
	struct dns_unreachable unreachable[UNREACH_CACHE_SIZE];

	dns_keymgmt_t *keymgmt;

	isc_tlsctx_cache_t *tlsctx_cache;
	isc_rwlock_t tlsctx_cache_rwlock;
};

/*%
 * Hold notify state.
 */
struct dns_notify {
	unsigned int magic;
	unsigned int flags;
	isc_mem_t *mctx;
	dns_zone_t *zone;
	dns_adbfind_t *find;
	dns_request_t *request;
	dns_name_t ns;
	isc_sockaddr_t src;
	isc_sockaddr_t dst;
	dns_tsigkey_t *key;
	dns_transport_t *transport;
	ISC_LINK(dns_notify_t) link;
	isc_rlevent_t *rlevent;
};

typedef enum dns_notify_flags {
	DNS_NOTIFY_NOSOA = 1 << 0,
	DNS_NOTIFY_STARTUP = 1 << 1,
	DNS_NOTIFY_TCP = 1 << 2,
} dns_notify_flags_t;

/*%
 * Hold checkds state.
 */
struct dns_checkds {
	unsigned int magic;
	dns_notify_flags_t flags;
	isc_mem_t *mctx;
	dns_zone_t *zone;
	dns_adbfind_t *find;
	dns_request_t *request;
	dns_name_t ns;
	isc_sockaddr_t src;
	isc_sockaddr_t dst;
	dns_tsigkey_t *key;
	dns_transport_t *transport;
	ISC_LINK(dns_checkds_t) link;
	isc_rlevent_t *rlevent;
};

/*%
 *	dns_stub holds state while performing a 'stub' transfer.
 *	'db' is the zone's 'db' or a new one if this is the initial
 *	transfer.
 */

struct dns_stub {
	unsigned int magic;
	isc_mem_t *mctx;
	dns_zone_t *zone;
	dns_db_t *db;
	dns_dbversion_t *version;
	atomic_uint_fast32_t pending_requests;
};

/*%
 *	Hold load state.
 */
struct dns_load {
	dns_zone_t *zone;
	dns_db_t *db;
	isc_time_t loadtime;
	dns_rdatacallbacks_t callbacks;
};

/*%
 *	Hold forward state.
 */
struct dns_forward {
	unsigned int magic;
	isc_mem_t *mctx;
	dns_zone_t *zone;
	isc_buffer_t *msgbuf;
	dns_request_t *request;
	uint32_t which;
	isc_sockaddr_t addr;
	dns_transport_t *transport;
	dns_updatecallback_t callback;
	void *callback_arg;
	unsigned int options;
	ISC_LINK(dns_forward_t) link;
};

/*%
 *	Hold state for when we are signing a zone with a new
 *	DNSKEY as result of an update.
 */
struct dns_signing {
	unsigned int magic;
	dns_db_t *db;
	dns_dbiterator_t *dbiterator;
	dns_secalg_t algorithm;
	uint16_t keyid;
	bool deleteit;
	bool done;
	ISC_LINK(dns_signing_t) link;
};

struct dns_nsec3chain {
	unsigned int magic;
	dns_db_t *db;
	dns_dbiterator_t *dbiterator;
	dns_rdata_nsec3param_t nsec3param;
	unsigned char salt[255];
	bool done;
	bool seen_nsec;
	bool delete_nsec;
	bool save_delete_nsec;
	ISC_LINK(dns_nsec3chain_t) link;
};

/*%<
 * 'dbiterator' contains a iterator for the database.  If we are creating
 * a NSEC3 chain only the non-NSEC3 nodes will be iterated.  If we are
 * removing a NSEC3 chain then both NSEC3 and non-NSEC3 nodes will be
 * iterated.
 *
 * 'nsec3param' contains the parameters of the NSEC3 chain being created
 * or removed.
 *
 * 'salt' is buffer space and is referenced via 'nsec3param.salt'.
 *
 * 'seen_nsec' will be set to true if, while iterating the zone to create a
 * NSEC3 chain, a NSEC record is seen.
 *
 * 'delete_nsec' will be set to true if, at the completion of the creation
 * of a NSEC3 chain, 'seen_nsec' is true.  If 'delete_nsec' is true then we
 * are in the process of deleting the NSEC chain.
 *
 * 'save_delete_nsec' is used to store the initial state of 'delete_nsec'
 * so it can be recovered in the event of a error.
 */

struct dns_keyfetch {
	isc_mem_t *mctx;
	dns_fixedname_t name;
	dns_rdataset_t keydataset;
	dns_rdataset_t dnskeyset;
	dns_rdataset_t dnskeysigset;
	dns_zone_t *zone;
	dns_db_t *db;
	dns_fetch_t *fetch;
};

struct dns_nsfetch {
	isc_mem_t *mctx;
	dns_fixedname_t name;
	dns_name_t pname;
	dns_rdataset_t nsrrset;
	dns_rdataset_t nssigset;
	dns_zone_t *zone;
	dns_fetch_t *fetch;
};

/*%
 * Hold state for an asynchronous load
 */
struct dns_asyncload {
	dns_zone_t *zone;
	unsigned int flags;
	dns_zt_callback_t *loaded;
	void *loaded_arg;
};

/*%
 * Reference to an include file encountered during loading
 */
struct dns_include {
	char *name;
	isc_time_t filetime;
	ISC_LINK(dns_include_t) link;
};

/*
 * These can be overridden by the -T mkeytimers option on the command
 * line, so that we can test with shorter periods than specified in
 * RFC 5011.
 */
#define HOUR  3600
#define DAY   (24 * HOUR)
#define MONTH (30 * DAY)
unsigned int dns_zone_mkey_hour = HOUR;
unsigned int dns_zone_mkey_day = DAY;
unsigned int dns_zone_mkey_month = MONTH;

#define SEND_BUFFER_SIZE 2048

static void
zone_timer_set(dns_zone_t *zone, isc_time_t *next, isc_time_t *now);

typedef struct zone_settimer {
	dns_zone_t *zone;
	isc_time_t now;
} zone_settimer_t;

static void
zone_settimer(dns_zone_t *, isc_time_t *);
static void
cancel_refresh(dns_zone_t *);
static void
zone_debuglogc(dns_zone_t *zone, isc_logcategory_t *category, const char *me,
	       int debuglevel, const char *fmt, ...);
static void
zone_debuglog(dns_zone_t *zone, const char *, int debuglevel, const char *msg,
	      ...) ISC_FORMAT_PRINTF(4, 5);
static void
notify_log(dns_zone_t *zone, int level, const char *fmt, ...)
	ISC_FORMAT_PRINTF(3, 4);
static void
dnssec_log(dns_zone_t *zone, int level, const char *fmt, ...)
	ISC_FORMAT_PRINTF(3, 4);
static void
queue_xfrin(dns_zone_t *zone);
static isc_result_t
update_one_rr(dns_db_t *db, dns_dbversion_t *ver, dns_diff_t *diff,
	      dns_diffop_t op, dns_name_t *name, dns_ttl_t ttl,
	      dns_rdata_t *rdata);
static void
zone_unload(dns_zone_t *zone);
static void
zone_expire(dns_zone_t *zone);
static void
zone_refresh(dns_zone_t *zone);
static void
zone_iattach(dns_zone_t *source, dns_zone_t **target);
static void
zone_idetach(dns_zone_t **zonep);
static isc_result_t
zone_replacedb(dns_zone_t *zone, dns_db_t *db, bool dump);
static void
zone_attachdb(dns_zone_t *zone, dns_db_t *db);
static void
zone_detachdb(dns_zone_t *zone);
static void
zone_catz_enable(dns_zone_t *zone, dns_catz_zones_t *catzs);
static void
zone_catz_disable(dns_zone_t *zone);
static isc_result_t
default_journal(dns_zone_t *zone);
static void
zone_xfrdone(dns_zone_t *zone, uint32_t *expireopt, isc_result_t result);
static isc_result_t
zone_postload(dns_zone_t *zone, dns_db_t *db, isc_time_t loadtime,
	      isc_result_t result);
static void
zone_needdump(dns_zone_t *zone, unsigned int delay);
static void
zone_shutdown(void *arg);
static void
zone_loaddone(void *arg, isc_result_t result);
static isc_result_t
zone_startload(dns_db_t *db, dns_zone_t *zone, isc_time_t loadtime);
static void
zone_namerd_tostr(dns_zone_t *zone, char *buf, size_t length);
static void
zone_name_tostr(dns_zone_t *zone, char *buf, size_t length);
static void
zone_rdclass_tostr(dns_zone_t *zone, char *buf, size_t length);
static void
zone_viewname_tostr(dns_zone_t *zone, char *buf, size_t length);
static isc_result_t
zone_send_secureserial(dns_zone_t *zone, uint32_t serial);
static void
refresh_callback(void *arg);
static void
stub_callback(void *arg);
static void
queue_soa_query(dns_zone_t *zone);
static void
soa_query(void *arg);
static void
ns_query(dns_zone_t *zone, dns_rdataset_t *soardataset, dns_stub_t *stub);
static int
message_count(dns_message_t *msg, dns_section_t section, dns_rdatatype_t type);
static void
checkds_cancel(dns_zone_t *zone);
static void
checkds_find_address(dns_checkds_t *checkds);
static void
checkds_send(dns_zone_t *zone);
static void
checkds_createmessage(dns_zone_t *zone, dns_message_t **messagep);
static void
checkds_done(void *arg);
static void
checkds_send_tons(dns_checkds_t *checkds);
static void
checkds_send_toaddr(void *arg);
static void
nsfetch_levelup(dns_nsfetch_t *nsfetch);
static void
notify_cancel(dns_zone_t *zone);
static void
notify_find_address(dns_notify_t *notify);
static void
notify_send(dns_notify_t *notify);
static isc_result_t
notify_createmessage(dns_zone_t *zone, unsigned int flags,
		     dns_message_t **messagep);
static void
notify_done(void *arg);
static void
notify_send_toaddr(void *arg);
static isc_result_t
zone_dump(dns_zone_t *, bool);
static void
got_transfer_quota(void *arg);
static isc_result_t
zmgr_start_xfrin_ifquota(dns_zonemgr_t *zmgr, dns_zone_t *zone);
static void
zmgr_resume_xfrs(dns_zonemgr_t *zmgr, bool multi);
static void
zonemgr_free(dns_zonemgr_t *zmgr);
static void
rss_post(void *arg);

static isc_result_t
zone_get_from_db(dns_zone_t *zone, dns_db_t *db, unsigned int *nscount,
		 unsigned int *soacount, uint32_t *soattl, uint32_t *serial,
		 uint32_t *refresh, uint32_t *retry, uint32_t *expire,
		 uint32_t *minimum, unsigned int *errors);

static void
zone_freedbargs(dns_zone_t *zone);
static void
forward_callback(void *arg);
static void
zone_saveunique(dns_zone_t *zone, const char *path, const char *templat);
static void
zone_maintenance(dns_zone_t *zone);
static void
zone_notify(dns_zone_t *zone, isc_time_t *now);
static void
dump_done(void *arg, isc_result_t result);
static isc_result_t
zone_signwithkey(dns_zone_t *zone, dns_secalg_t algorithm, uint16_t keyid,
		 bool deleteit);
static isc_result_t
delete_nsec(dns_db_t *db, dns_dbversion_t *ver, dns_dbnode_t *node,
	    dns_name_t *name, dns_diff_t *diff);
static void
zone_rekey(dns_zone_t *zone);
static isc_result_t
zone_send_securedb(dns_zone_t *zone, dns_db_t *db);
static dns_ttl_t
zone_nsecttl(dns_zone_t *zone);
static void
setrl(isc_ratelimiter_t *rl, unsigned int *rate, unsigned int value);
static void
zone_journal_compact(dns_zone_t *zone, dns_db_t *db, uint32_t serial);
static isc_result_t
zone_journal_rollforward(dns_zone_t *zone, dns_db_t *db, bool *needdump,
			 bool *fixjournal);
static void
setnsec3param(void *arg);

static void
zmgr_tlsctx_attach(dns_zonemgr_t *zmgr, isc_tlsctx_cache_t **ptlsctx_cache);
/*%<
 *	Attach to TLS client context cache used for zone transfers via
 *	encrypted transports (e.g. XoT).
 *
 * The obtained reference needs to be detached by a call to
 * 'isc_tlsctx_cache_detach()' when not needed anymore.
 *
 * Requires:
 *\li	'zmgr' is a valid zone manager.
 *\li	'ptlsctx_cache' is not 'NULL' and points to 'NULL'.
 */

#define ENTER zone_debuglog(zone, __func__, 1, "enter")

static const unsigned int dbargc_default = 1;
static const char *dbargv_default[] = { ZONEDB_DEFAULT };

#define DNS_ZONE_JITTER_ADD(a, b, c)                                         \
	do {                                                                 \
		isc_interval_t _i;                                           \
		uint32_t _j;                                                 \
		_j = (b) - isc_random_uniform((b) / 4);                      \
		isc_interval_set(&_i, _j, 0);                                \
		if (isc_time_add((a), &_i, (c)) != ISC_R_SUCCESS) {          \
			dns_zone_log(zone, ISC_LOG_WARNING,                  \
				     "epoch approaching: upgrade required: " \
				     "now + %s failed",                      \
				     #b);                                    \
			isc_interval_set(&_i, _j / 2, 0);                    \
			(void)isc_time_add((a), &_i, (c));                   \
		}                                                            \
	} while (0)

#define DNS_ZONE_TIME_ADD(a, b, c)                                           \
	do {                                                                 \
		isc_interval_t _i;                                           \
		isc_interval_set(&_i, (b), 0);                               \
		if (isc_time_add((a), &_i, (c)) != ISC_R_SUCCESS) {          \
			dns_zone_log(zone, ISC_LOG_WARNING,                  \
				     "epoch approaching: upgrade required: " \
				     "now + %s failed",                      \
				     #b);                                    \
			isc_interval_set(&_i, (b) / 2, 0);                   \
			(void)isc_time_add((a), &_i, (c));                   \
		}                                                            \
	} while (0)

#define DNS_ZONE_TIME_SUBTRACT(a, b, c)                                      \
	do {                                                                 \
		isc_interval_t _i;                                           \
		isc_interval_set(&_i, (b), 0);                               \
		if (isc_time_subtract((a), &_i, (c)) != ISC_R_SUCCESS) {     \
			dns_zone_log(zone, ISC_LOG_WARNING,                  \
				     "epoch approaching: upgrade required: " \
				     "isc_time_subtract() failed");          \
			isc_interval_set(&_i, (b) / 2, 0);                   \
			(void)isc_time_subtract((a), &_i, (c));              \
		}                                                            \
	} while (0)

typedef struct nsec3param nsec3param_t;
struct nsec3param {
	dns_rdata_nsec3param_t rdata;
	unsigned char data[DNS_NSEC3PARAM_BUFFERSIZE + 1];
	unsigned int length;
	bool nsec;
	bool replace;
	bool resalt;
	bool lookup;
	ISC_LINK(nsec3param_t) link;
};
typedef ISC_LIST(nsec3param_t) nsec3paramlist_t;

struct np3 {
	dns_zone_t *zone;
	nsec3param_t params;
	ISC_LINK(struct np3) link;
};

struct setserial {
	dns_zone_t *zone;
	uint32_t serial;
};

struct stub_cb_args {
	dns_stub_t *stub;
	dns_tsigkey_t *tsig_key;
	uint16_t udpsize;
	int timeout;
	bool reqnsid;
};

struct stub_glue_request {
	dns_request_t *request;
	dns_name_t name;
	struct stub_cb_args *args;
	bool ipv4;
};

/*%
 * Increment resolver-related statistics counters.  Zone must be locked.
 */
static void
inc_stats(dns_zone_t *zone, isc_statscounter_t counter) {
	if (zone->stats != NULL) {
		isc_stats_increment(zone->stats, counter);
	}
}

/***
 ***	Public functions.
 ***/

void
dns_zone_create(dns_zone_t **zonep, isc_mem_t *mctx, unsigned int tid) {
	isc_time_t now;
	dns_zone_t *zone = NULL;

	REQUIRE(zonep != NULL && *zonep == NULL);
	REQUIRE(mctx != NULL);

	now = isc_time_now();
	zone = isc_mem_get(mctx, sizeof(*zone));
	*zone = (dns_zone_t){
		.masterformat = dns_masterformat_none,
		.journalsize = -1,
		.rdclass = dns_rdataclass_none,
		.type = dns_zone_none,
		.refresh = DNS_ZONE_DEFAULTREFRESH,
		.retry = DNS_ZONE_DEFAULTRETRY,
		.maxrefresh = DNS_ZONE_MAXREFRESH,
		.minrefresh = DNS_ZONE_MINREFRESH,
		.maxretry = DNS_ZONE_MAXRETRY,
		.minretry = DNS_ZONE_MINRETRY,
		.checkdstype = dns_checkdstype_yes,
		.notifytype = dns_notifytype_yes,
		.zero_no_soa_ttl = true,
		.check_names = dns_severity_ignore,
		.idlein = DNS_DEFAULT_IDLEIN,
		.idleout = DNS_DEFAULT_IDLEOUT,
		.maxxfrin = MAX_XFER_TIME,
		.maxxfrout = MAX_XFER_TIME,
		.sigvalidityinterval = 30 * 24 * 3600,
		.sigresigninginterval = 7 * 24 * 3600,
		.statlevel = dns_zonestat_none,
		.notifydelay = 5,
		.signatures = 10,
		.nodes = 100,
		.privatetype = (dns_rdatatype_t)0xffffU,
		.rpz_num = DNS_RPZ_INVALID_NUM,
		.requestixfr = true,
		.ixfr_ratio = 100,
		.requestexpire = true,
		.updatemethod = dns_updatemethod_increment,
		.tid = tid,
		.notifytime = now,
		.newincludes = ISC_LIST_INITIALIZER,
		.notifies = ISC_LIST_INITIALIZER,
		.checkds_requests = ISC_LIST_INITIALIZER,
		.signing = ISC_LIST_INITIALIZER,
		.nsec3chain = ISC_LIST_INITIALIZER,
		.setnsec3param_queue = ISC_LIST_INITIALIZER,
		.forwards = ISC_LIST_INITIALIZER,
		.link = ISC_LINK_INITIALIZER,
		.statelink = ISC_LINK_INITIALIZER,
	};
	dns_remote_t r = {
		.magic = DNS_REMOTE_MAGIC,
	};

	isc_mem_attach(mctx, &zone->mctx);
	isc_mutex_init(&zone->lock);
#ifndef _LP64
	isc_mutex_init(&zone->atomic_lock);
#endif
	ZONEDB_INITLOCK(&zone->dblock);

	isc_refcount_init(&zone->references, 1);
	isc_refcount_init(&zone->irefs, 0);
	dns_name_init(&zone->origin, NULL);
	isc_sockaddr_any(&zone->notifysrc4);
	isc_sockaddr_any6(&zone->notifysrc6);
	isc_sockaddr_any(&zone->parentalsrc4);
	isc_sockaddr_any6(&zone->parentalsrc6);
	isc_sockaddr_any(&zone->xfrsource4);
	isc_sockaddr_any6(&zone->xfrsource6);

	zone->primaries = r;
	zone->parentals = r;
	zone->notify = r;
	zone->defaultkasp = NULL;
	ISC_LIST_INIT(zone->keyring);

	isc_stats_create(mctx, &zone->gluecachestats,
			 dns_gluecachestatscounter_max);

	zone->magic = ZONE_MAGIC;

	/* Must be after magic is set. */
	dns_zone_setdbtype(zone, dbargc_default, dbargv_default);

	*zonep = zone;
}

static void
clear_keylist(dns_dnsseckeylist_t *list, isc_mem_t *mctx) {
	dns_dnsseckey_t *key;
	while (!ISC_LIST_EMPTY(*list)) {
		key = ISC_LIST_HEAD(*list);
		ISC_LIST_UNLINK(*list, key, link);
		dns_dnsseckey_destroy(mctx, &key);
	}
}

/*
 * Free a zone.  Because we require that there be no more
 * outstanding events or references, no locking is necessary.
 */
static void
zone_free(dns_zone_t *zone) {
	dns_signing_t *signing = NULL;
	dns_nsec3chain_t *nsec3chain = NULL;
	dns_include_t *include = NULL;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(!LOCKED_ZONE(zone));
	REQUIRE(zone->timer == NULL);
	REQUIRE(zone->zmgr == NULL);

	isc_refcount_destroy(&zone->references);
	isc_refcount_destroy(&zone->irefs);

	/*
	 * Managed objects.  Order is important.
	 */
	if (zone->request != NULL) {
		dns_request_destroy(&zone->request); /* XXXMPA */
	}
	INSIST(zone->statelist == NULL);
	INSIST(zone->view == NULL);
	INSIST(zone->prev_view == NULL);

	/* Unmanaged objects */
	for (struct np3 *npe = ISC_LIST_HEAD(zone->setnsec3param_queue);
	     npe != NULL; npe = ISC_LIST_HEAD(zone->setnsec3param_queue))
	{
		ISC_LIST_UNLINK(zone->setnsec3param_queue, npe, link);
		isc_mem_put(zone->mctx, npe, sizeof(*npe));
	}

	for (signing = ISC_LIST_HEAD(zone->signing); signing != NULL;
	     signing = ISC_LIST_HEAD(zone->signing))
	{
		ISC_LIST_UNLINK(zone->signing, signing, link);
		dns_db_detach(&signing->db);
		dns_dbiterator_destroy(&signing->dbiterator);
		isc_mem_put(zone->mctx, signing, sizeof *signing);
	}
	for (nsec3chain = ISC_LIST_HEAD(zone->nsec3chain); nsec3chain != NULL;
	     nsec3chain = ISC_LIST_HEAD(zone->nsec3chain))
	{
		ISC_LIST_UNLINK(zone->nsec3chain, nsec3chain, link);
		dns_db_detach(&nsec3chain->db);
		dns_dbiterator_destroy(&nsec3chain->dbiterator);
		isc_mem_put(zone->mctx, nsec3chain, sizeof *nsec3chain);
	}
	for (include = ISC_LIST_HEAD(zone->includes); include != NULL;
	     include = ISC_LIST_HEAD(zone->includes))
	{
		ISC_LIST_UNLINK(zone->includes, include, link);
		isc_mem_free(zone->mctx, include->name);
		isc_mem_put(zone->mctx, include, sizeof *include);
	}
	for (include = ISC_LIST_HEAD(zone->newincludes); include != NULL;
	     include = ISC_LIST_HEAD(zone->newincludes))
	{
		ISC_LIST_UNLINK(zone->newincludes, include, link);
		isc_mem_free(zone->mctx, include->name);
		isc_mem_put(zone->mctx, include, sizeof *include);
	}
	if (zone->masterfile != NULL) {
		isc_mem_free(zone->mctx, zone->masterfile);
	}
	zone->masterfile = NULL;
	if (zone->keydirectory != NULL) {
		isc_mem_free(zone->mctx, zone->keydirectory);
	}
	zone->keydirectory = NULL;
	if (zone->kasp != NULL) {
		dns_kasp_detach(&zone->kasp);
	}
	if (zone->defaultkasp != NULL) {
		dns_kasp_detach(&zone->defaultkasp);
	}
	if (!ISC_LIST_EMPTY(zone->keyring)) {
		clear_keylist(&zone->keyring, zone->mctx);
	}
	if (!ISC_LIST_EMPTY(zone->checkds_ok)) {
		clear_keylist(&zone->checkds_ok, zone->mctx);
	}
	if (zone->skr != NULL) {
		zone->skrbundle = NULL;
		dns_skr_detach(&zone->skr);
	}

	zone->journalsize = -1;
	if (zone->journal != NULL) {
		isc_mem_free(zone->mctx, zone->journal);
	}
	zone->journal = NULL;
	if (zone->stats != NULL) {
		isc_stats_detach(&zone->stats);
	}
	if (zone->requeststats != NULL) {
		isc_stats_detach(&zone->requeststats);
	}
	if (zone->rcvquerystats != NULL) {
		dns_stats_detach(&zone->rcvquerystats);
	}
	if (zone->dnssecsignstats != NULL) {
		dns_stats_detach(&zone->dnssecsignstats);
	}
	if (zone->db != NULL) {
		zone_detachdb(zone);
	}
	if (zone->rpzs != NULL) {
		REQUIRE(zone->rpz_num < zone->rpzs->p.num_zones);
		dns_rpz_zones_detach(&zone->rpzs);
		zone->rpz_num = DNS_RPZ_INVALID_NUM;
	}
	if (zone->catzs != NULL) {
		dns_catz_zones_detach(&zone->catzs);
	}
	zone_freedbargs(zone);

	dns_zone_setparentals(zone, NULL, NULL, NULL, NULL, 0);
	dns_zone_setprimaries(zone, NULL, NULL, NULL, NULL, 0);
	dns_zone_setalsonotify(zone, NULL, NULL, NULL, NULL, 0);

	zone->check_names = dns_severity_ignore;
	if (zone->update_acl != NULL) {
		dns_acl_detach(&zone->update_acl);
	}
	if (zone->forward_acl != NULL) {
		dns_acl_detach(&zone->forward_acl);
	}
	if (zone->notify_acl != NULL) {
		dns_acl_detach(&zone->notify_acl);
	}
	if (zone->query_acl != NULL) {
		dns_acl_detach(&zone->query_acl);
	}
	if (zone->queryon_acl != NULL) {
		dns_acl_detach(&zone->queryon_acl);
	}
	if (zone->xfr_acl != NULL) {
		dns_acl_detach(&zone->xfr_acl);
	}
	if (dns_name_dynamic(&zone->origin)) {
		dns_name_free(&zone->origin, zone->mctx);
	}
	if (zone->strnamerd != NULL) {
		isc_mem_free(zone->mctx, zone->strnamerd);
	}
	if (zone->strname != NULL) {
		isc_mem_free(zone->mctx, zone->strname);
	}
	if (zone->strrdclass != NULL) {
		isc_mem_free(zone->mctx, zone->strrdclass);
	}
	if (zone->strviewname != NULL) {
		isc_mem_free(zone->mctx, zone->strviewname);
	}
	if (zone->ssutable != NULL) {
		dns_ssutable_detach(&zone->ssutable);
	}
	if (zone->gluecachestats != NULL) {
		isc_stats_detach(&zone->gluecachestats);
	}

	/* last stuff */
	ZONEDB_DESTROYLOCK(&zone->dblock);
	isc_mutex_destroy(&zone->lock);
#ifndef _LP64
	isc_mutex_destroy(&zone->atomic_lock);
#endif
	zone->magic = 0;
	isc_mem_putanddetach(&zone->mctx, zone, sizeof(*zone));
}

/*
 * Returns true iff this the signed side of an inline-signing zone.
 * Caller should hold zone lock.
 */
static bool
inline_secure(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));
	if (zone->raw != NULL) {
		return true;
	}
	return false;
}

/*
 * Returns true iff this the unsigned side of an inline-signing zone
 * Caller should hold zone lock.
 */
static bool
inline_raw(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));
	if (zone->secure != NULL) {
		return true;
	}
	return false;
}

/*
 *	Single shot.
 */
void
dns_zone_setclass(dns_zone_t *zone, dns_rdataclass_t rdclass) {
	char namebuf[1024];

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(rdclass != dns_rdataclass_none);

	/*
	 * Test and set.
	 */
	LOCK_ZONE(zone);
	INSIST(zone != zone->raw);
	REQUIRE(zone->rdclass == dns_rdataclass_none ||
		zone->rdclass == rdclass);
	zone->rdclass = rdclass;

	if (zone->strnamerd != NULL) {
		isc_mem_free(zone->mctx, zone->strnamerd);
	}
	if (zone->strrdclass != NULL) {
		isc_mem_free(zone->mctx, zone->strrdclass);
	}

	zone_namerd_tostr(zone, namebuf, sizeof namebuf);
	zone->strnamerd = isc_mem_strdup(zone->mctx, namebuf);
	zone_rdclass_tostr(zone, namebuf, sizeof namebuf);
	zone->strrdclass = isc_mem_strdup(zone->mctx, namebuf);

	if (inline_secure(zone)) {
		dns_zone_setclass(zone->raw, rdclass);
	}
	UNLOCK_ZONE(zone);
}

dns_rdataclass_t
dns_zone_getclass(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->rdclass;
}

void
dns_zone_setnotifytype(dns_zone_t *zone, dns_notifytype_t notifytype) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	zone->notifytype = notifytype;
	UNLOCK_ZONE(zone);
}

void
dns_zone_setcheckdstype(dns_zone_t *zone, dns_checkdstype_t checkdstype) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	zone->checkdstype = checkdstype;
	UNLOCK_ZONE(zone);
}

isc_result_t
dns_zone_getserial(dns_zone_t *zone, uint32_t *serialp) {
	isc_result_t result;
	unsigned int soacount;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(serialp != NULL);

	LOCK_ZONE(zone);
	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	if (zone->db != NULL) {
		result = zone_get_from_db(zone, zone->db, NULL, &soacount, NULL,
					  serialp, NULL, NULL, NULL, NULL,
					  NULL);
		if (result == ISC_R_SUCCESS && soacount == 0) {
			result = ISC_R_FAILURE;
		}
	} else {
		result = DNS_R_NOTLOADED;
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
	UNLOCK_ZONE(zone);

	return result;
}

/*
 *	Single shot.
 */
void
dns_zone_settype(dns_zone_t *zone, dns_zonetype_t type) {
	char namebuf[1024];

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(type != dns_zone_none);

	/*
	 * Test and set.
	 */
	LOCK_ZONE(zone);
	REQUIRE(zone->type == dns_zone_none || zone->type == type);
	zone->type = type;

	if (zone->strnamerd != NULL) {
		isc_mem_free(zone->mctx, zone->strnamerd);
	}

	zone_namerd_tostr(zone, namebuf, sizeof namebuf);
	zone->strnamerd = isc_mem_strdup(zone->mctx, namebuf);
	UNLOCK_ZONE(zone);
}

static void
zone_freedbargs(dns_zone_t *zone) {
	unsigned int i;

	/* Free the old database argument list. */
	if (zone->db_argv != NULL) {
		for (i = 0; i < zone->db_argc; i++) {
			isc_mem_free(zone->mctx, zone->db_argv[i]);
		}
		isc_mem_cput(zone->mctx, zone->db_argv, zone->db_argc,
			     sizeof(*zone->db_argv));
	}
	zone->db_argc = 0;
	zone->db_argv = NULL;
}

isc_result_t
dns_zone_getdbtype(dns_zone_t *zone, char ***argv, isc_mem_t *mctx) {
	size_t size = 0;
	unsigned int i;
	isc_result_t result = ISC_R_SUCCESS;
	void *mem;
	char **tmp, *tmp2, *base;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(argv != NULL && *argv == NULL);

	LOCK_ZONE(zone);
	size = ISC_CHECKED_MUL(zone->db_argc + 1, sizeof(char *));
	for (i = 0; i < zone->db_argc; i++) {
		size += strlen(zone->db_argv[i]) + 1;
	}
	mem = isc_mem_allocate(mctx, size);
	{
		tmp = mem;
		tmp2 = mem;
		base = mem;
		tmp2 += ISC_CHECKED_MUL(zone->db_argc + 1, sizeof(char *));
		for (i = 0; i < zone->db_argc; i++) {
			*tmp++ = tmp2;
			strlcpy(tmp2, zone->db_argv[i], size - (tmp2 - base));
			tmp2 += strlen(tmp2) + 1;
		}
		*tmp = NULL;
	}
	UNLOCK_ZONE(zone);
	*argv = mem;
	return result;
}

void
dns_zone_setdbtype(dns_zone_t *zone, unsigned int dbargc,
		   const char *const *dbargv) {
	char **argv = NULL;
	unsigned int i;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(dbargc >= 1);
	REQUIRE(dbargv != NULL);

	LOCK_ZONE(zone);

	/* Set up a new database argument list. */
	argv = isc_mem_cget(zone->mctx, dbargc, sizeof(*argv));
	for (i = 0; i < dbargc; i++) {
		argv[i] = NULL;
	}
	for (i = 0; i < dbargc; i++) {
		argv[i] = isc_mem_strdup(zone->mctx, dbargv[i]);
	}

	/* Free the old list. */
	zone_freedbargs(zone);

	zone->db_argc = dbargc;
	zone->db_argv = argv;

	UNLOCK_ZONE(zone);
}

static void
dns_zone_setview_helper(dns_zone_t *zone, dns_view_t *view) {
	char namebuf[1024];

	if (zone->prev_view == NULL && zone->view != NULL) {
		dns_view_weakattach(zone->view, &zone->prev_view);
	}

	INSIST(zone != zone->raw);
	if (zone->view != NULL) {
		dns_view_sfd_del(zone->view, &zone->origin);
		dns_view_weakdetach(&zone->view);
	}
	dns_view_weakattach(view, &zone->view);
	dns_view_sfd_add(view, &zone->origin);

	if (zone->strviewname != NULL) {
		isc_mem_free(zone->mctx, zone->strviewname);
	}
	if (zone->strnamerd != NULL) {
		isc_mem_free(zone->mctx, zone->strnamerd);
	}

	zone_namerd_tostr(zone, namebuf, sizeof namebuf);
	zone->strnamerd = isc_mem_strdup(zone->mctx, namebuf);
	zone_viewname_tostr(zone, namebuf, sizeof namebuf);
	zone->strviewname = isc_mem_strdup(zone->mctx, namebuf);

	if (inline_secure(zone)) {
		dns_zone_setview(zone->raw, view);
	}
}

void
dns_zone_setview(dns_zone_t *zone, dns_view_t *view) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	dns_zone_setview_helper(zone, view);
	UNLOCK_ZONE(zone);
}

dns_view_t *
dns_zone_getview(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->view;
}

void
dns_zone_setviewcommit(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->prev_view != NULL) {
		dns_view_weakdetach(&zone->prev_view);
	}
	if (inline_secure(zone)) {
		dns_zone_setviewcommit(zone->raw);
	}
	UNLOCK_ZONE(zone);
}

void
dns_zone_setviewrevert(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->prev_view != NULL) {
		dns_zone_setview_helper(zone, zone->prev_view);
		dns_view_weakdetach(&zone->prev_view);
	}
	if (zone->catzs != NULL) {
		zone_catz_enable(zone, zone->catzs);
	}
	if (inline_secure(zone)) {
		dns_zone_setviewrevert(zone->raw);
	}
	UNLOCK_ZONE(zone);
}

isc_result_t
dns_zone_setorigin(dns_zone_t *zone, const dns_name_t *origin) {
	isc_result_t result = ISC_R_SUCCESS;
	char namebuf[1024];

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(origin != NULL);

	LOCK_ZONE(zone);
	INSIST(zone != zone->raw);
	if (dns_name_dynamic(&zone->origin)) {
		dns_name_free(&zone->origin, zone->mctx);
		dns_name_init(&zone->origin, NULL);
	}
	dns_name_dup(origin, zone->mctx, &zone->origin);

	if (zone->strnamerd != NULL) {
		isc_mem_free(zone->mctx, zone->strnamerd);
	}
	if (zone->strname != NULL) {
		isc_mem_free(zone->mctx, zone->strname);
	}

	zone_namerd_tostr(zone, namebuf, sizeof namebuf);
	zone->strnamerd = isc_mem_strdup(zone->mctx, namebuf);
	zone_name_tostr(zone, namebuf, sizeof namebuf);
	zone->strname = isc_mem_strdup(zone->mctx, namebuf);

	if (inline_secure(zone)) {
		result = dns_zone_setorigin(zone->raw, origin);
	}
	UNLOCK_ZONE(zone);
	return result;
}

static isc_result_t
dns_zone_setstring(dns_zone_t *zone, char **field, const char *value) {
	char *copy;

	if (value != NULL) {
		copy = isc_mem_strdup(zone->mctx, value);
	} else {
		copy = NULL;
	}

	if (*field != NULL) {
		isc_mem_free(zone->mctx, *field);
	}

	*field = copy;
	return ISC_R_SUCCESS;
}

isc_result_t
dns_zone_setfile(dns_zone_t *zone, const char *file, dns_masterformat_t format,
		 const dns_master_style_t *style) {
	isc_result_t result = ISC_R_SUCCESS;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(zone->stream == NULL);

	LOCK_ZONE(zone);
	result = dns_zone_setstring(zone, &zone->masterfile, file);
	if (result == ISC_R_SUCCESS) {
		zone->masterformat = format;
		if (format == dns_masterformat_text) {
			zone->masterstyle = style;
		}
		result = default_journal(zone);
	}
	UNLOCK_ZONE(zone);

	return result;
}

const char *
dns_zone_getfile(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->masterfile;
}

isc_result_t
dns_zone_setstream(dns_zone_t *zone, const FILE *stream,
		   dns_masterformat_t format, const dns_master_style_t *style) {
	isc_result_t result = ISC_R_SUCCESS;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(stream != NULL);
	REQUIRE(zone->masterfile == NULL);

	LOCK_ZONE(zone);
	zone->stream = stream;
	zone->masterformat = format;
	if (format == dns_masterformat_text) {
		zone->masterstyle = style;
	}
	result = default_journal(zone);
	UNLOCK_ZONE(zone);

	return result;
}

dns_ttl_t
dns_zone_getmaxttl(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->maxttl;
}

void
dns_zone_setmaxttl(dns_zone_t *zone, dns_ttl_t maxttl) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (maxttl != 0) {
		DNS_ZONE_SETOPTION(zone, DNS_ZONEOPT_CHECKTTL);
	} else {
		DNS_ZONE_CLROPTION(zone, DNS_ZONEOPT_CHECKTTL);
	}
	zone->maxttl = maxttl;
	UNLOCK_ZONE(zone);

	return;
}

static isc_result_t
default_journal(dns_zone_t *zone) {
	isc_result_t result;
	char *journal;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(LOCKED_ZONE(zone));

	if (zone->masterfile != NULL) {
		/* Calculate string length including '\0'. */
		int len = strlen(zone->masterfile) + sizeof(".jnl");
		journal = isc_mem_allocate(zone->mctx, len);
		strlcpy(journal, zone->masterfile, len);
		strlcat(journal, ".jnl", len);
	} else {
		journal = NULL;
	}
	result = dns_zone_setstring(zone, &zone->journal, journal);
	if (journal != NULL) {
		isc_mem_free(zone->mctx, journal);
	}
	return result;
}

isc_result_t
dns_zone_setjournal(dns_zone_t *zone, const char *myjournal) {
	isc_result_t result = ISC_R_SUCCESS;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	result = dns_zone_setstring(zone, &zone->journal, myjournal);
	UNLOCK_ZONE(zone);

	return result;
}

char *
dns_zone_getjournal(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->journal;
}

/*
 * Return true iff the zone is "dynamic", in the sense that the zone's
 * master file (if any) is written by the server, rather than being
 * updated manually and read by the server.
 *
 * This is true for secondary zones, mirror zones, stub zones, key zones,
 * and zones that allow dynamic updates either by having an update
 * policy ("ssutable") or an "allow-update" ACL with a value other than
 * exactly "{ none; }".
 */
bool
dns_zone_isdynamic(dns_zone_t *zone, bool ignore_freeze) {
	REQUIRE(DNS_ZONE_VALID(zone));

	if (zone->type == dns_zone_secondary || zone->type == dns_zone_mirror ||
	    zone->type == dns_zone_stub || zone->type == dns_zone_key ||
	    (zone->type == dns_zone_redirect &&
	     dns_remote_addresses(&zone->primaries) != NULL))
	{
		return true;
	}

	/* Inline zones are always dynamic. */
	if (zone->type == dns_zone_primary && zone->raw != NULL) {
		return true;
	}

	/* If !ignore_freeze, we need check whether updates are disabled.  */
	if (zone->type == dns_zone_primary &&
	    (!zone->update_disabled || ignore_freeze) &&
	    ((zone->ssutable != NULL) ||
	     (zone->update_acl != NULL && !dns_acl_isnone(zone->update_acl))))
	{
		return true;
	}

	return false;
}

/*
 * Set the response policy index and information for a zone.
 */
isc_result_t
dns_zone_rpz_enable(dns_zone_t *zone, dns_rpz_zones_t *rpzs,
		    dns_rpz_num_t rpz_num) {
	/*
	 * This must happen only once or be redundant.
	 */
	LOCK_ZONE(zone);
	if (zone->rpzs != NULL) {
		REQUIRE(zone->rpzs == rpzs && zone->rpz_num == rpz_num);
	} else {
		REQUIRE(zone->rpz_num == DNS_RPZ_INVALID_NUM);
		dns_rpz_zones_attach(rpzs, &zone->rpzs);
		zone->rpz_num = rpz_num;
	}
	rpzs->defined |= DNS_RPZ_ZBIT(rpz_num);
	UNLOCK_ZONE(zone);

	return ISC_R_SUCCESS;
}

dns_rpz_num_t
dns_zone_get_rpz_num(dns_zone_t *zone) {
	return zone->rpz_num;
}

/*
 * If a zone is a response policy zone, mark its new database.
 */
void
dns_zone_rpz_enable_db(dns_zone_t *zone, dns_db_t *db) {
	if (zone->rpz_num == DNS_RPZ_INVALID_NUM) {
		return;
	}
	REQUIRE(zone->rpzs != NULL);
	dns_rpz_dbupdate_register(db, zone->rpzs->zones[zone->rpz_num]);
}

static void
dns_zone_rpz_disable_db(dns_zone_t *zone, dns_db_t *db) {
	if (zone->rpz_num == DNS_RPZ_INVALID_NUM) {
		return;
	}
	REQUIRE(zone->rpzs != NULL);
	dns_rpz_dbupdate_unregister(db, zone->rpzs->zones[zone->rpz_num]);
}

/*
 * If a zone is a catalog zone, attach it to update notification in database.
 */
void
dns_zone_catz_enable_db(dns_zone_t *zone, dns_db_t *db) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(db != NULL);

	if (zone->catzs != NULL) {
		dns_catz_dbupdate_register(db, zone->catzs);
	}
}

static void
dns_zone_catz_disable_db(dns_zone_t *zone, dns_db_t *db) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(db != NULL);

	if (zone->catzs != NULL) {
		dns_catz_dbupdate_unregister(db, zone->catzs);
	}
}

static void
zone_catz_enable(dns_zone_t *zone, dns_catz_zones_t *catzs) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(catzs != NULL);

	INSIST(zone->catzs == NULL || zone->catzs == catzs);
	dns_catz_catzs_set_view(catzs, zone->view);
	if (zone->catzs == NULL) {
		dns_catz_zones_attach(catzs, &zone->catzs);
	}
}

void
dns_zone_catz_enable(dns_zone_t *zone, dns_catz_zones_t *catzs) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	zone_catz_enable(zone, catzs);
	UNLOCK_ZONE(zone);
}

static void
zone_catz_disable(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	if (zone->catzs != NULL) {
		if (zone->db != NULL) {
			dns_zone_catz_disable_db(zone, zone->db);
		}
		dns_catz_zones_detach(&zone->catzs);
	}
}

void
dns_zone_catz_disable(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	zone_catz_disable(zone);
	UNLOCK_ZONE(zone);
}

bool
dns_zone_catz_is_enabled(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->catzs != NULL;
}

/*
 * Set catalog zone ownership of the zone
 */
void
dns_zone_set_parentcatz(dns_zone_t *zone, dns_catz_zone_t *catz) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(catz != NULL);
	LOCK_ZONE(zone);
	INSIST(zone->parentcatz == NULL || zone->parentcatz == catz);
	zone->parentcatz = catz;
	UNLOCK_ZONE(zone);
}

dns_catz_zone_t *
dns_zone_get_parentcatz(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	dns_catz_zone_t *parentcatz = NULL;

	LOCK_ZONE(zone);
	parentcatz = zone->parentcatz;
	UNLOCK_ZONE(zone);

	return parentcatz;
}

static bool
zone_touched(dns_zone_t *zone) {
	isc_result_t result;
	isc_time_t modtime;
	dns_include_t *include;

	REQUIRE(DNS_ZONE_VALID(zone));

	result = isc_file_getmodtime(zone->masterfile, &modtime);
	if (result != ISC_R_SUCCESS ||
	    isc_time_compare(&modtime, &zone->loadtime) > 0)
	{
		return true;
	}

	for (include = ISC_LIST_HEAD(zone->includes); include != NULL;
	     include = ISC_LIST_NEXT(include, link))
	{
		result = isc_file_getmodtime(include->name, &modtime);
		if (result != ISC_R_SUCCESS ||
		    isc_time_compare(&modtime, &include->filetime) > 0)
		{
			return true;
		}
	}

	return false;
}

/*
 * Note: when dealing with inline-signed zones, external callers will always
 * call zone_load() for the secure zone; zone_load() calls itself recursively
 * in order to load the raw zone.
 */
static isc_result_t
zone_load(dns_zone_t *zone, unsigned int flags, bool locked) {
	isc_result_t result;
	isc_time_t now;
	isc_time_t loadtime;
	dns_db_t *db = NULL;
	bool rbt, hasraw, is_dynamic;

	REQUIRE(DNS_ZONE_VALID(zone));

	if (!locked) {
		LOCK_ZONE(zone);
	}

	INSIST(zone != zone->raw);
	hasraw = inline_secure(zone);
	if (hasraw) {
		/*
		 * We are trying to load an inline-signed zone.  First call
		 * self recursively to try loading the raw version of the zone.
		 * Assuming the raw zone file is readable, there are two
		 * possibilities:
		 *
		 *  a) the raw zone was not yet loaded and thus it will be
		 *     loaded now, synchronously; if this succeeds, a
		 *     subsequent attempt to load the signed zone file will
		 *     take place and thus zone_postload() will be called
		 *     twice: first for the raw zone and then for the secure
		 *     zone; the latter call will take care of syncing the raw
		 *     version with the secure version,
		 *
		 *  b) the raw zone was already loaded and we are trying to
		 *     reload it, which will happen asynchronously; this means
		 *     zone_postload() will only be called for the raw zone
		 *     because "result" returned by the zone_load() call below
		 *     will not be ISC_R_SUCCESS but rather DNS_R_CONTINUE;
		 *     zone_postload() called for the raw zone will take care
		 *     of syncing the raw version with the secure version.
		 */
		result = zone_load(zone->raw, flags, false);
		if (result != ISC_R_SUCCESS) {
			if (!locked) {
				UNLOCK_ZONE(zone);
			}
			return result;
		}
		LOCK_ZONE(zone->raw);
	}

	now = isc_time_now();

	INSIST(zone->type != dns_zone_none);

	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADING)) {
		if ((flags & DNS_ZONELOADFLAG_THAW) != 0) {
			DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_THAW);
		}
		result = DNS_R_CONTINUE;
		goto cleanup;
	}

	INSIST(zone->db_argc >= 1);

	rbt = strcmp(zone->db_argv[0], ZONEDB_DEFAULT) == 0;

	if (zone->db != NULL && zone->masterfile == NULL && rbt) {
		/*
		 * The zone has no master file configured.
		 */
		result = ISC_R_SUCCESS;
		goto cleanup;
	}

	is_dynamic = dns_zone_isdynamic(zone, false);
	if (zone->db != NULL && is_dynamic) {
		/*
		 * This is a secondary, stub, or dynamically updated zone
		 * being reloaded.  Do nothing - the database we already
		 * have is guaranteed to be up-to-date.
		 */
		if (zone->type == dns_zone_primary && !hasraw) {
			result = DNS_R_DYNAMIC;
		} else {
			result = ISC_R_SUCCESS;
		}
		goto cleanup;
	}

	/*
	 * Store the current time before the zone is loaded, so that if the
	 * file changes between the time of the load and the time that
	 * zone->loadtime is set, then the file will still be reloaded
	 * the next time dns_zone_load is called.
	 */
	loadtime = isc_time_now();

	/*
	 * Don't do the load if the file that stores the zone is older
	 * than the last time the zone was loaded.  If the zone has not
	 * been loaded yet, zone->loadtime will be the epoch.
	 */
	if (zone->masterfile != NULL) {
		isc_time_t filetime;

		/*
		 * The file is already loaded.	If we are just doing a
		 * "rndc reconfig", we are done.
		 */
		if (!isc_time_isepoch(&zone->loadtime) &&
		    (flags & DNS_ZONELOADFLAG_NOSTAT) != 0)
		{
			result = ISC_R_SUCCESS;
			goto cleanup;
		}

		if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADED) &&
		    !zone_touched(zone))
		{
			dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD,
				      ISC_LOG_DEBUG(1),
				      "skipping load: master file "
				      "older than last load");
			result = DNS_R_UPTODATE;
			goto cleanup;
		}

		/*
		 * If the file modification time is in the past
		 * set loadtime to that value.
		 */
		result = isc_file_getmodtime(zone->masterfile, &filetime);
		if (result == ISC_R_SUCCESS &&
		    isc_time_compare(&loadtime, &filetime) > 0)
		{
			loadtime = filetime;
		}
	}

	/*
	 * Built in zones (with the exception of empty zones) don't need
	 * to be reloaded.
	 */
	if (zone->type == dns_zone_primary &&
	    strcmp(zone->db_argv[0], "_builtin") == 0 &&
	    (zone->db_argc < 2 || strcmp(zone->db_argv[1], "empty") != 0) &&
	    DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADED))
	{
		result = ISC_R_SUCCESS;
		goto cleanup;
	}

	/*
	 * Zones associated with a DLZ don't need to be loaded either,
	 * but we need to associate the database with the zone object.
	 */
	if (strcmp(zone->db_argv[0], "dlz") == 0) {
		dns_dlzdb_t *dlzdb;
		dns_dlzfindzone_t findzone;

		for (dlzdb = ISC_LIST_HEAD(zone->view->dlz_unsearched);
		     dlzdb != NULL; dlzdb = ISC_LIST_NEXT(dlzdb, link))
		{
			INSIST(DNS_DLZ_VALID(dlzdb));
			if (strcmp(zone->db_argv[1], dlzdb->dlzname) == 0) {
				break;
			}
		}

		if (dlzdb == NULL) {
			dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD,
				      ISC_LOG_ERROR,
				      "DLZ %s does not exist or is set "
				      "to 'search yes;'",
				      zone->db_argv[1]);
			result = ISC_R_NOTFOUND;
			goto cleanup;
		}

		ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_write);
		/* ask SDLZ driver if the zone is supported */
		findzone = dlzdb->implementation->methods->findzone;
		result = (*findzone)(dlzdb->implementation->driverarg,
				     dlzdb->dbdata, dlzdb->mctx,
				     zone->view->rdclass, &zone->origin, NULL,
				     NULL, &db);
		if (result != ISC_R_NOTFOUND) {
			if (zone->db != NULL) {
				zone_detachdb(zone);
			}
			zone_attachdb(zone, db);
			dns_db_detach(&db);
			result = ISC_R_SUCCESS;
		}
		ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_write);

		if (result == ISC_R_SUCCESS) {
			if (dlzdb->configure_callback == NULL) {
				goto cleanup;
			}

			result = (*dlzdb->configure_callback)(zone->view, dlzdb,
							      zone);
			if (result != ISC_R_SUCCESS) {
				dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD,
					      ISC_LOG_ERROR,
					      "DLZ configuration callback: %s",
					      isc_result_totext(result));
			}
		}
		goto cleanup;
	}

	if ((zone->type == dns_zone_secondary ||
	     zone->type == dns_zone_mirror || zone->type == dns_zone_stub ||
	     (zone->type == dns_zone_redirect &&
	      dns_remote_addresses(&zone->primaries) != NULL)) &&
	    rbt)
	{
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_FIRSTREFRESH);

		if (zone->stream == NULL &&
		    (zone->masterfile == NULL ||
		     !isc_file_exists(zone->masterfile)))
		{
			if (zone->masterfile != NULL) {
				dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD,
					      ISC_LOG_DEBUG(1),
					      "no master file");
			}
			zone->refreshtime = now;
			if (zone->loop != NULL) {
				zone_settimer(zone, &now);
			}
			result = ISC_R_SUCCESS;
			goto cleanup;
		}
	}

	dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD, ISC_LOG_DEBUG(1),
		      "starting load");

	result = dns_zone_makedb(zone, &db);
	if (result != ISC_R_SUCCESS) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD, ISC_LOG_ERROR,
			      "loading zone: creating database: %s",
			      isc_result_totext(result));
		goto cleanup;
	}

	if (!dns_db_ispersistent(db)) {
		if (zone->masterfile != NULL || zone->stream != NULL) {
			result = zone_startload(db, zone, loadtime);
		} else {
			result = DNS_R_NOMASTERFILE;
			if (zone->type == dns_zone_primary ||
			    (zone->type == dns_zone_redirect &&
			     dns_remote_addresses(&zone->primaries) == NULL))
			{
				dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD,
					      ISC_LOG_ERROR,
					      "loading zone: "
					      "no master file configured");
				goto cleanup;
			}
			dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD,
				      ISC_LOG_INFO,
				      "loading zone: "
				      "no master file configured: continuing");
		}
	}

	if (result == DNS_R_CONTINUE) {
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_LOADING);
		if ((flags & DNS_ZONELOADFLAG_THAW) != 0) {
			DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_THAW);
		}
		goto cleanup;
	}

	result = zone_postload(zone, db, loadtime, result);

cleanup:
	if (hasraw) {
		UNLOCK_ZONE(zone->raw);
	}
	if (!locked) {
		UNLOCK_ZONE(zone);
	}
	if (db != NULL) {
		dns_db_detach(&db);
	}
	return result;
}

isc_result_t
dns_zone_load(dns_zone_t *zone, bool newonly) {
	return zone_load(zone, newonly ? DNS_ZONELOADFLAG_NOSTAT : 0, false);
}

static void
zone_asyncload(void *arg) {
	dns_asyncload_t *asl = arg;
	dns_zone_t *zone = asl->zone;
	isc_result_t result;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	result = zone_load(zone, asl->flags, true);
	if (result != DNS_R_CONTINUE) {
		DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_LOADPENDING);
	}
	UNLOCK_ZONE(zone);

	/* Inform the zone table we've finished loading */
	if (asl->loaded != NULL) {
		asl->loaded(asl->loaded_arg);
	}

	isc_mem_put(zone->mctx, asl, sizeof(*asl));
	dns_zone_idetach(&zone);
}

isc_result_t
dns_zone_asyncload(dns_zone_t *zone, bool newonly, dns_zt_callback_t *done,
		   void *arg) {
	dns_asyncload_t *asl = NULL;

	REQUIRE(DNS_ZONE_VALID(zone));

	if (zone->zmgr == NULL) {
		return ISC_R_FAILURE;
	}

	/* If we already have a load pending, stop now */
	LOCK_ZONE(zone);
	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADPENDING)) {
		UNLOCK_ZONE(zone);
		return ISC_R_ALREADYRUNNING;
	}

	asl = isc_mem_get(zone->mctx, sizeof(*asl));

	asl->zone = NULL;
	asl->flags = newonly ? DNS_ZONELOADFLAG_NOSTAT : 0;
	asl->loaded = done;
	asl->loaded_arg = arg;

	zone_iattach(zone, &asl->zone);
	DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_LOADPENDING);
	isc_async_run(zone->loop, zone_asyncload, asl);
	UNLOCK_ZONE(zone);

	return ISC_R_SUCCESS;
}

bool
dns__zone_loadpending(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADPENDING);
}

isc_result_t
dns_zone_loadandthaw(dns_zone_t *zone) {
	isc_result_t result;

	if (inline_raw(zone)) {
		result = zone_load(zone->secure, DNS_ZONELOADFLAG_THAW, false);
	} else {
		/*
		 * When thawing a zone, we don't know what changes
		 * have been made. If we do DNSSEC maintenance on this
		 * zone, schedule a full sign for this zone.
		 */
		if (zone->type == dns_zone_primary &&
		    DNS_ZONEKEY_OPTION(zone, DNS_ZONEKEY_MAINTAIN))
		{
			DNS_ZONEKEY_SETOPTION(zone, DNS_ZONEKEY_FULLSIGN);
		}
		result = zone_load(zone, DNS_ZONELOADFLAG_THAW, false);
	}

	switch (result) {
	case DNS_R_CONTINUE:
		/* Deferred thaw. */
		break;
	case DNS_R_UPTODATE:
	case ISC_R_SUCCESS:
	case DNS_R_SEENINCLUDE:
		zone->update_disabled = false;
		break;
	case DNS_R_NOMASTERFILE:
		zone->update_disabled = false;
		break;
	default:
		/* Error, remain in disabled state. */
		break;
	}
	return result;
}

static unsigned int
get_primary_options(dns_zone_t *zone) {
	unsigned int options;

	options = DNS_MASTER_ZONE | DNS_MASTER_RESIGN;
	if (zone->type == dns_zone_secondary || zone->type == dns_zone_mirror ||
	    (zone->type == dns_zone_redirect &&
	     dns_remote_addresses(&zone->primaries) == NULL))
	{
		options |= DNS_MASTER_SECONDARY;
	}
	if (zone->type == dns_zone_key) {
		options |= DNS_MASTER_KEY;
	}
	if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_CHECKNS)) {
		options |= DNS_MASTER_CHECKNS;
	}
	if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_FATALNS)) {
		options |= DNS_MASTER_FATALNS;
	}
	if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_CHECKNAMES)) {
		options |= DNS_MASTER_CHECKNAMES;
	}
	if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_CHECKNAMESFAIL)) {
		options |= DNS_MASTER_CHECKNAMESFAIL;
	}
	if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_CHECKMX)) {
		options |= DNS_MASTER_CHECKMX;
	}
	if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_CHECKMXFAIL)) {
		options |= DNS_MASTER_CHECKMXFAIL;
	}
	if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_CHECKWILDCARD)) {
		options |= DNS_MASTER_CHECKWILDCARD;
	}
	if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_CHECKTTL)) {
		options |= DNS_MASTER_CHECKTTL;
	}
	if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_CHECKSVCB)) {
		options |= DNS_MASTER_CHECKSVCB;
	}

	return options;
}

static void
zone_registerinclude(const char *filename, void *arg) {
	isc_result_t result;
	dns_zone_t *zone = (dns_zone_t *)arg;
	dns_include_t *inc = NULL;

	REQUIRE(DNS_ZONE_VALID(zone));

	if (filename == NULL) {
		return;
	}

	/*
	 * Suppress duplicates.
	 */
	for (inc = ISC_LIST_HEAD(zone->newincludes); inc != NULL;
	     inc = ISC_LIST_NEXT(inc, link))
	{
		if (strcmp(filename, inc->name) == 0) {
			return;
		}
	}

	inc = isc_mem_get(zone->mctx, sizeof(dns_include_t));
	inc->name = isc_mem_strdup(zone->mctx, filename);
	ISC_LINK_INIT(inc, link);

	result = isc_file_getmodtime(filename, &inc->filetime);
	if (result != ISC_R_SUCCESS) {
		isc_time_settoepoch(&inc->filetime);
	}

	ISC_LIST_APPEND(zone->newincludes, inc, link);
}

static void
get_raw_serial(dns_zone_t *raw, dns_masterrawheader_t *rawdata) {
	isc_result_t result;
	unsigned int soacount;

	LOCK(&raw->lock);
	if (raw->db != NULL) {
		result = zone_get_from_db(raw, raw->db, NULL, &soacount, NULL,
					  &rawdata->sourceserial, NULL, NULL,
					  NULL, NULL, NULL);
		if (result == ISC_R_SUCCESS && soacount > 0U) {
			rawdata->flags |= DNS_MASTERRAW_SOURCESERIALSET;
		}
	}
	UNLOCK(&raw->lock);
}

/*
 * Save the raw serial number for inline-signing zones.
 * (XXX: Other information from the header will be used
 * for other purposes in the future, but for now this is
 * all we're interested in.)
 */
static void
zone_setrawdata(dns_zone_t *zone, dns_masterrawheader_t *header) {
	if ((header->flags & DNS_MASTERRAW_SOURCESERIALSET) == 0) {
		return;
	}

	zone->sourceserial = header->sourceserial;
	zone->sourceserialset = true;
}

void
dns_zone_setrawdata(dns_zone_t *zone, dns_masterrawheader_t *header) {
	if (zone == NULL) {
		return;
	}

	LOCK_ZONE(zone);
	zone_setrawdata(zone, header);
	UNLOCK_ZONE(zone);
}

static isc_result_t
zone_startload(dns_db_t *db, dns_zone_t *zone, isc_time_t loadtime) {
	isc_result_t result;
	isc_result_t tresult;
	unsigned int options;
	dns_load_t *load = isc_mem_get(zone->mctx, sizeof(*load));

	ENTER;

	*load = (dns_load_t){
		.loadtime = loadtime,
	};

	dns_zone_rpz_enable_db(zone, db);
	dns_zone_catz_enable_db(zone, db);

	options = get_primary_options(zone);
	if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_MANYERRORS)) {
		options |= DNS_MASTER_MANYERRORS;
	}

	zone_iattach(zone, &load->zone);
	dns_db_attach(db, &load->db);

	dns_rdatacallbacks_init(&load->callbacks);
	load->callbacks.rawdata = zone_setrawdata;
	zone_iattach(zone, &load->callbacks.zone);

	result = dns_db_beginload(db, &load->callbacks);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	if (zone->zmgr != NULL && zone->db != NULL) {
		result = dns_master_loadfileasync(
			zone->masterfile, dns_db_origin(db), dns_db_origin(db),
			zone->rdclass, options, 0, &load->callbacks, zone->loop,
			zone_loaddone, load, &zone->loadctx,
			zone_registerinclude, zone, zone->mctx,
			zone->masterformat, zone->maxttl);
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}

		return DNS_R_CONTINUE;
	} else if (zone->stream != NULL) {
		FILE *stream = UNCONST(zone->stream);
		result = dns_master_loadstream(
			stream, &zone->origin, &zone->origin, zone->rdclass,
			options, &load->callbacks, zone->mctx);
	} else {
		result = dns_master_loadfile(
			zone->masterfile, &zone->origin, &zone->origin,
			zone->rdclass, options, 0, &load->callbacks,
			zone_registerinclude, zone, zone->mctx,
			zone->masterformat, zone->maxttl);
	}

cleanup:
	if (result != ISC_R_SUCCESS && result != DNS_R_SEENINCLUDE) {
		dns_zone_rpz_disable_db(zone, load->db);
		dns_zone_catz_disable_db(zone, load->db);
	}

	tresult = dns_db_endload(db, &load->callbacks);
	if (result == ISC_R_SUCCESS || result == DNS_R_SEENINCLUDE) {
		result = tresult;
	}

	zone_idetach(&load->callbacks.zone);
	dns_db_detach(&load->db);
	zone_idetach(&load->zone);

	isc_mem_put(zone->mctx, load, sizeof(*load));
	return result;
}

static bool
zone_check_mx(dns_zone_t *zone, dns_db_t *db, dns_name_t *name,
	      dns_name_t *owner) {
	isc_result_t result;
	char ownerbuf[DNS_NAME_FORMATSIZE];
	char namebuf[DNS_NAME_FORMATSIZE];
	char altbuf[DNS_NAME_FORMATSIZE];
	dns_fixedname_t fixed;
	dns_name_t *foundname;
	int level;

	/*
	 * "." means the services does not exist.
	 */
	if (dns_name_equal(name, dns_rootname)) {
		return true;
	}

	/*
	 * Outside of zone.
	 */
	if (!dns_name_issubdomain(name, &zone->origin)) {
		if (zone->checkmx != NULL) {
			return (zone->checkmx)(zone, name, owner);
		}
		return true;
	}

	if (zone->type == dns_zone_primary) {
		level = ISC_LOG_ERROR;
	} else {
		level = ISC_LOG_WARNING;
	}

	foundname = dns_fixedname_initname(&fixed);

	result = dns_db_find(db, name, NULL, dns_rdatatype_a, 0, 0, NULL,
			     foundname, NULL, NULL);
	if (result == ISC_R_SUCCESS) {
		return true;
	}

	if (result == DNS_R_NXRRSET) {
		result = dns_db_find(db, name, NULL, dns_rdatatype_aaaa, 0, 0,
				     NULL, foundname, NULL, NULL);
		if (result == ISC_R_SUCCESS) {
			return true;
		}
	}

	dns_name_format(owner, ownerbuf, sizeof ownerbuf);
	dns_name_format(name, namebuf, sizeof namebuf);
	if (result == DNS_R_NXRRSET || result == DNS_R_NXDOMAIN ||
	    result == DNS_R_EMPTYNAME)
	{
		if (!DNS_ZONE_OPTION(zone, DNS_ZONEOPT_CHECKMXFAIL)) {
			level = ISC_LOG_WARNING;
		}
		dns_zone_log(zone, level,
			     "%s/MX '%s' has no address records (A or AAAA)",
			     ownerbuf, namebuf);
		return (level == ISC_LOG_WARNING) ? true : false;
	}

	if (result == DNS_R_CNAME) {
		if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_WARNMXCNAME) ||
		    DNS_ZONE_OPTION(zone, DNS_ZONEOPT_IGNOREMXCNAME))
		{
			level = ISC_LOG_WARNING;
		}
		if (!DNS_ZONE_OPTION(zone, DNS_ZONEOPT_IGNOREMXCNAME)) {
			dns_zone_log(zone, level,
				     "%s/MX '%s' is a CNAME (illegal)",
				     ownerbuf, namebuf);
		}
		return (level == ISC_LOG_WARNING) ? true : false;
	}

	if (result == DNS_R_DNAME) {
		if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_WARNMXCNAME) ||
		    DNS_ZONE_OPTION(zone, DNS_ZONEOPT_IGNOREMXCNAME))
		{
			level = ISC_LOG_WARNING;
		}
		if (!DNS_ZONE_OPTION(zone, DNS_ZONEOPT_IGNOREMXCNAME)) {
			dns_name_format(foundname, altbuf, sizeof altbuf);
			dns_zone_log(zone, level,
				     "%s/MX '%s' is below a DNAME"
				     " '%s' (illegal)",
				     ownerbuf, namebuf, altbuf);
		}
		return (level == ISC_LOG_WARNING) ? true : false;
	}

	if (zone->checkmx != NULL && result == DNS_R_DELEGATION) {
		return (zone->checkmx)(zone, name, owner);
	}

	return true;
}

static bool
zone_check_srv(dns_zone_t *zone, dns_db_t *db, dns_name_t *name,
	       dns_name_t *owner) {
	isc_result_t result;
	char ownerbuf[DNS_NAME_FORMATSIZE];
	char namebuf[DNS_NAME_FORMATSIZE];
	char altbuf[DNS_NAME_FORMATSIZE];
	dns_fixedname_t fixed;
	dns_name_t *foundname;
	int level;

	/*
	 * "." means the services does not exist.
	 */
	if (dns_name_equal(name, dns_rootname)) {
		return true;
	}

	/*
	 * Outside of zone.
	 */
	if (!dns_name_issubdomain(name, &zone->origin)) {
		if (zone->checksrv != NULL) {
			return (zone->checksrv)(zone, name, owner);
		}
		return true;
	}

	if (zone->type == dns_zone_primary) {
		level = ISC_LOG_ERROR;
	} else {
		level = ISC_LOG_WARNING;
	}

	foundname = dns_fixedname_initname(&fixed);

	result = dns_db_find(db, name, NULL, dns_rdatatype_a, 0, 0, NULL,
			     foundname, NULL, NULL);
	if (result == ISC_R_SUCCESS) {
		return true;
	}

	if (result == DNS_R_NXRRSET) {
		result = dns_db_find(db, name, NULL, dns_rdatatype_aaaa, 0, 0,
				     NULL, foundname, NULL, NULL);
		if (result == ISC_R_SUCCESS) {
			return true;
		}
	}

	dns_name_format(owner, ownerbuf, sizeof ownerbuf);
	dns_name_format(name, namebuf, sizeof namebuf);
	if (result == DNS_R_NXRRSET || result == DNS_R_NXDOMAIN ||
	    result == DNS_R_EMPTYNAME)
	{
		dns_zone_log(zone, level,
			     "%s/SRV '%s' has no address records (A or AAAA)",
			     ownerbuf, namebuf);
		/* XXX950 make fatal for 9.5.0. */
		return true;
	}

	if (result == DNS_R_CNAME) {
		if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_WARNSRVCNAME) ||
		    DNS_ZONE_OPTION(zone, DNS_ZONEOPT_IGNORESRVCNAME))
		{
			level = ISC_LOG_WARNING;
		}
		if (!DNS_ZONE_OPTION(zone, DNS_ZONEOPT_IGNORESRVCNAME)) {
			dns_zone_log(zone, level,
				     "%s/SRV '%s' is a CNAME (illegal)",
				     ownerbuf, namebuf);
		}
		return (level == ISC_LOG_WARNING) ? true : false;
	}

	if (result == DNS_R_DNAME) {
		if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_WARNSRVCNAME) ||
		    DNS_ZONE_OPTION(zone, DNS_ZONEOPT_IGNORESRVCNAME))
		{
			level = ISC_LOG_WARNING;
		}
		if (!DNS_ZONE_OPTION(zone, DNS_ZONEOPT_IGNORESRVCNAME)) {
			dns_name_format(foundname, altbuf, sizeof altbuf);
			dns_zone_log(zone, level,
				     "%s/SRV '%s' is below a "
				     "DNAME '%s' (illegal)",
				     ownerbuf, namebuf, altbuf);
		}
		return (level == ISC_LOG_WARNING) ? true : false;
	}

	if (zone->checksrv != NULL && result == DNS_R_DELEGATION) {
		return (zone->checksrv)(zone, name, owner);
	}

	return true;
}

static bool
zone_check_glue(dns_zone_t *zone, dns_db_t *db, dns_name_t *name,
		dns_name_t *owner) {
	bool answer = true;
	isc_result_t result, tresult;
	char ownerbuf[DNS_NAME_FORMATSIZE];
	char namebuf[DNS_NAME_FORMATSIZE];
	char altbuf[DNS_NAME_FORMATSIZE];
	dns_fixedname_t fixed;
	dns_name_t *foundname;
	dns_rdataset_t a;
	dns_rdataset_t aaaa;
	int level;

	/*
	 * Outside of zone.
	 */
	if (!dns_name_issubdomain(name, &zone->origin)) {
		if (zone->checkns != NULL) {
			return (zone->checkns)(zone, name, owner, NULL, NULL);
		}
		return true;
	}

	if (zone->type == dns_zone_primary) {
		level = ISC_LOG_ERROR;
	} else {
		level = ISC_LOG_WARNING;
	}

	foundname = dns_fixedname_initname(&fixed);
	dns_rdataset_init(&a);
	dns_rdataset_init(&aaaa);

	/*
	 * Perform a regular lookup to catch DNAME records then look
	 * for glue.
	 */
	result = dns_db_find(db, name, NULL, dns_rdatatype_a, 0, 0, NULL,
			     foundname, &a, NULL);
	switch (result) {
	case ISC_R_SUCCESS:
	case DNS_R_DNAME:
	case DNS_R_CNAME:
		break;
	default:
		if (dns_rdataset_isassociated(&a)) {
			dns_rdataset_disassociate(&a);
		}
		result = dns_db_find(db, name, NULL, dns_rdatatype_a,
				     DNS_DBFIND_GLUEOK, 0, NULL, foundname, &a,
				     NULL);
	}
	if (result == ISC_R_SUCCESS) {
		dns_rdataset_disassociate(&a);
		return true;
	} else if (result == DNS_R_DELEGATION) {
		dns_rdataset_disassociate(&a);
	}

	if (result == DNS_R_NXRRSET || result == DNS_R_DELEGATION ||
	    result == DNS_R_GLUE)
	{
		tresult = dns_db_find(db, name, NULL, dns_rdatatype_aaaa,
				      DNS_DBFIND_GLUEOK, 0, NULL, foundname,
				      &aaaa, NULL);
		if (tresult == ISC_R_SUCCESS) {
			if (dns_rdataset_isassociated(&a)) {
				dns_rdataset_disassociate(&a);
			}
			dns_rdataset_disassociate(&aaaa);
			return true;
		}
		if (tresult == DNS_R_DELEGATION || tresult == DNS_R_DNAME) {
			dns_rdataset_disassociate(&aaaa);
		}
		if (result == DNS_R_GLUE || tresult == DNS_R_GLUE) {
			/*
			 * Check glue against child zone.
			 */
			if (zone->checkns != NULL) {
				answer = (zone->checkns)(zone, name, owner, &a,
							 &aaaa);
			}
			if (dns_rdataset_isassociated(&a)) {
				dns_rdataset_disassociate(&a);
			}
			if (dns_rdataset_isassociated(&aaaa)) {
				dns_rdataset_disassociate(&aaaa);
			}
			return answer;
		}
	}

	dns_name_format(owner, ownerbuf, sizeof ownerbuf);
	dns_name_format(name, namebuf, sizeof namebuf);
	if (result == DNS_R_NXRRSET || result == DNS_R_NXDOMAIN ||
	    result == DNS_R_EMPTYNAME || result == DNS_R_DELEGATION)
	{
		const char *what;
		bool required = false;
		if (dns_name_issubdomain(name, owner)) {
			what = "REQUIRED GLUE ";
			required = true;
		} else if (result == DNS_R_DELEGATION) {
			what = "SIBLING GLUE ";
		} else {
			what = "";
		}

		if (result != DNS_R_DELEGATION || required ||
		    DNS_ZONE_OPTION(zone, DNS_ZONEOPT_CHECKSIBLING))
		{
			dns_zone_log(zone, level,
				     "%s/NS '%s' has no %s"
				     "address records (A or AAAA)",
				     ownerbuf, namebuf, what);
			/*
			 * Log missing address record.
			 */
			if (result == DNS_R_DELEGATION && zone->checkns != NULL)
			{
				(void)(zone->checkns)(zone, name, owner, &a,
						      &aaaa);
			}
			/* XXX950 make fatal for 9.5.0. */
			/* answer = false; */
		}
	} else if (result == DNS_R_CNAME) {
		dns_zone_log(zone, level, "%s/NS '%s' is a CNAME (illegal)",
			     ownerbuf, namebuf);
		/* XXX950 make fatal for 9.5.0. */
		/* answer = false; */
	} else if (result == DNS_R_DNAME) {
		dns_name_format(foundname, altbuf, sizeof altbuf);
		dns_zone_log(zone, level,
			     "%s/NS '%s' is below a DNAME '%s' (illegal)",
			     ownerbuf, namebuf, altbuf);
		/* XXX950 make fatal for 9.5.0. */
		/* answer = false; */
	}

	if (dns_rdataset_isassociated(&a)) {
		dns_rdataset_disassociate(&a);
	}
	if (dns_rdataset_isassociated(&aaaa)) {
		dns_rdataset_disassociate(&aaaa);
	}
	return answer;
}

static bool
zone_rrset_check_dup(dns_zone_t *zone, dns_name_t *owner,
		     dns_rdataset_t *rdataset) {
	dns_rdataset_t tmprdataset;
	isc_result_t result;
	bool answer = true;
	bool format = true;
	int level = ISC_LOG_WARNING;
	char ownerbuf[DNS_NAME_FORMATSIZE];
	char typebuf[DNS_RDATATYPE_FORMATSIZE];
	unsigned int count1 = 0;

	if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_CHECKDUPRRFAIL)) {
		level = ISC_LOG_ERROR;
	}

	dns_rdataset_init(&tmprdataset);
	for (result = dns_rdataset_first(rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(rdataset))
	{
		dns_rdata_t rdata1 = DNS_RDATA_INIT;
		unsigned int count2 = 0;

		count1++;
		dns_rdataset_current(rdataset, &rdata1);
		dns_rdataset_clone(rdataset, &tmprdataset);
		for (result = dns_rdataset_first(&tmprdataset);
		     result == ISC_R_SUCCESS;
		     result = dns_rdataset_next(&tmprdataset))
		{
			dns_rdata_t rdata2 = DNS_RDATA_INIT;
			count2++;
			if (count1 >= count2) {
				continue;
			}
			dns_rdataset_current(&tmprdataset, &rdata2);
			if (dns_rdata_casecompare(&rdata1, &rdata2) == 0) {
				if (format) {
					dns_name_format(owner, ownerbuf,
							sizeof ownerbuf);
					dns_rdatatype_format(rdata1.type,
							     typebuf,
							     sizeof(typebuf));
					format = false;
				}
				dns_zone_log(zone, level,
					     "%s/%s has "
					     "semantically identical records",
					     ownerbuf, typebuf);
				if (level == ISC_LOG_ERROR) {
					answer = false;
				}
				break;
			}
		}
		dns_rdataset_disassociate(&tmprdataset);
		if (!format) {
			break;
		}
	}
	return answer;
}

static bool
zone_check_dup(dns_zone_t *zone, dns_db_t *db) {
	dns_dbiterator_t *dbiterator = NULL;
	dns_dbnode_t *node = NULL;
	dns_fixedname_t fixed;
	dns_name_t *name;
	dns_rdataset_t rdataset;
	dns_rdatasetiter_t *rdsit = NULL;
	bool ok = true;
	isc_result_t result;

	name = dns_fixedname_initname(&fixed);
	dns_rdataset_init(&rdataset);

	result = dns_db_createiterator(db, 0, &dbiterator);
	if (result != ISC_R_SUCCESS) {
		return true;
	}

	for (result = dns_dbiterator_first(dbiterator); result == ISC_R_SUCCESS;
	     result = dns_dbiterator_next(dbiterator))
	{
		result = dns_dbiterator_current(dbiterator, &node, name);
		if (result != ISC_R_SUCCESS) {
			continue;
		}

		result = dns_db_allrdatasets(db, node, NULL, 0, 0, &rdsit);
		if (result != ISC_R_SUCCESS) {
			continue;
		}

		for (result = dns_rdatasetiter_first(rdsit);
		     result == ISC_R_SUCCESS;
		     result = dns_rdatasetiter_next(rdsit))
		{
			dns_rdatasetiter_current(rdsit, &rdataset);
			if (!zone_rrset_check_dup(zone, name, &rdataset)) {
				ok = false;
			}
			dns_rdataset_disassociate(&rdataset);
		}
		dns_rdatasetiter_destroy(&rdsit);
		dns_db_detachnode(db, &node);
	}

	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}
	dns_dbiterator_destroy(&dbiterator);

	return ok;
}

static bool
isspf(const dns_rdata_t *rdata) {
	char buf[1024];
	const unsigned char *data = rdata->data;
	unsigned int rdl = rdata->length, i = 0, tl, len;

	while (rdl > 0U) {
		len = tl = *data;
		++data;
		--rdl;
		INSIST(tl <= rdl);
		if (len > sizeof(buf) - i - 1) {
			len = sizeof(buf) - i - 1;
		}
		memmove(buf + i, data, len);
		i += len;
		data += tl;
		rdl -= tl;
	}

	if (i < 6U) {
		return false;
	}

	buf[i] = 0;
	if (strncmp(buf, "v=spf1", 6) == 0 && (buf[6] == 0 || buf[6] == ' ')) {
		return true;
	}
	return false;
}

static bool
integrity_checks(dns_zone_t *zone, dns_db_t *db) {
	dns_dbiterator_t *dbiterator = NULL;
	dns_dbnode_t *node = NULL;
	dns_rdataset_t rdataset;
	dns_fixedname_t fixed;
	dns_fixedname_t fixedbottom;
	dns_rdata_mx_t mx;
	dns_rdata_ns_t ns;
	dns_rdata_in_srv_t srv;
	dns_rdata_t rdata;
	dns_name_t *name;
	dns_name_t *bottom;
	isc_result_t result;
	bool ok = true, have_spf, have_txt;
	int level;
	char namebuf[DNS_NAME_FORMATSIZE];

	name = dns_fixedname_initname(&fixed);
	bottom = dns_fixedname_initname(&fixedbottom);
	dns_rdataset_init(&rdataset);
	dns_rdata_init(&rdata);

	result = dns_db_createiterator(db, 0, &dbiterator);
	if (result != ISC_R_SUCCESS) {
		return true;
	}

	result = dns_dbiterator_first(dbiterator);
	while (result == ISC_R_SUCCESS) {
		result = dns_dbiterator_current(dbiterator, &node, name);
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}

		/*
		 * Is this name visible in the zone?
		 */
		if (!dns_name_issubdomain(name, &zone->origin) ||
		    (dns_name_countlabels(bottom) > 0 &&
		     dns_name_issubdomain(name, bottom)))
		{
			goto next;
		}

		dns_dbiterator_pause(dbiterator);

		/*
		 * Don't check the NS records at the origin.
		 */
		if (dns_name_equal(name, &zone->origin)) {
			goto checkfords;
		}

		result = dns_db_findrdataset(db, node, NULL, dns_rdatatype_ns,
					     0, 0, &rdataset, NULL);
		if (result != ISC_R_SUCCESS) {
			goto checkfords;
		}
		/*
		 * Remember bottom of zone due to NS.
		 */
		dns_name_copy(name, bottom);

		result = dns_rdataset_first(&rdataset);
		while (result == ISC_R_SUCCESS) {
			dns_rdataset_current(&rdataset, &rdata);
			result = dns_rdata_tostruct(&rdata, &ns, NULL);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);
			if (!zone_check_glue(zone, db, &ns.name, name)) {
				ok = false;
			}
			dns_rdata_reset(&rdata);
			result = dns_rdataset_next(&rdataset);
		}
		dns_rdataset_disassociate(&rdataset);
		goto next;

	checkfords:
		result = dns_db_findrdataset(db, node, NULL, dns_rdatatype_ds,
					     0, 0, &rdataset, NULL);
		if (result != ISC_R_SUCCESS) {
			goto checkfordname;
		}
		dns_rdataset_disassociate(&rdataset);

		if (zone->type == dns_zone_primary) {
			level = ISC_LOG_ERROR;
			ok = false;
		} else {
			level = ISC_LOG_WARNING;
		}
		dns_name_format(name, namebuf, sizeof(namebuf));
		dns_zone_log(zone, level, "DS not at delegation point (%s)",
			     namebuf);

	checkfordname:
		result = dns_db_findrdataset(db, node, NULL,
					     dns_rdatatype_dname, 0, 0,
					     &rdataset, NULL);
		if (result == ISC_R_SUCCESS) {
			/*
			 * Remember bottom of zone due to DNAME.
			 */
			dns_name_copy(name, bottom);
			dns_rdataset_disassociate(&rdataset);
		}

		result = dns_db_findrdataset(db, node, NULL, dns_rdatatype_mx,
					     0, 0, &rdataset, NULL);
		if (result != ISC_R_SUCCESS) {
			goto checksrv;
		}
		result = dns_rdataset_first(&rdataset);
		while (result == ISC_R_SUCCESS) {
			dns_rdataset_current(&rdataset, &rdata);
			result = dns_rdata_tostruct(&rdata, &mx, NULL);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);
			if (!zone_check_mx(zone, db, &mx.mx, name)) {
				ok = false;
			}
			dns_rdata_reset(&rdata);
			result = dns_rdataset_next(&rdataset);
		}
		dns_rdataset_disassociate(&rdataset);

	checksrv:
		if (zone->rdclass != dns_rdataclass_in) {
			goto next;
		}
		result = dns_db_findrdataset(db, node, NULL, dns_rdatatype_srv,
					     0, 0, &rdataset, NULL);
		if (result != ISC_R_SUCCESS) {
			goto checkspf;
		}
		result = dns_rdataset_first(&rdataset);
		while (result == ISC_R_SUCCESS) {
			dns_rdataset_current(&rdataset, &rdata);
			result = dns_rdata_tostruct(&rdata, &srv, NULL);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);
			if (!zone_check_srv(zone, db, &srv.target, name)) {
				ok = false;
			}
			dns_rdata_reset(&rdata);
			result = dns_rdataset_next(&rdataset);
		}
		dns_rdataset_disassociate(&rdataset);

	checkspf:
		/*
		 * Check if there is a type SPF record without an
		 * SPF-formatted type TXT record also being present.
		 */
		if (!DNS_ZONE_OPTION(zone, DNS_ZONEOPT_CHECKSPF)) {
			goto next;
		}
		if (zone->rdclass != dns_rdataclass_in) {
			goto next;
		}
		have_spf = have_txt = false;
		result = dns_db_findrdataset(db, node, NULL, dns_rdatatype_spf,
					     0, 0, &rdataset, NULL);
		if (result == ISC_R_SUCCESS) {
			dns_rdataset_disassociate(&rdataset);
			have_spf = true;
		}
		result = dns_db_findrdataset(db, node, NULL, dns_rdatatype_txt,
					     0, 0, &rdataset, NULL);
		if (result != ISC_R_SUCCESS) {
			goto notxt;
		}
		result = dns_rdataset_first(&rdataset);
		while (result == ISC_R_SUCCESS) {
			dns_rdataset_current(&rdataset, &rdata);
			have_txt = isspf(&rdata);
			dns_rdata_reset(&rdata);
			if (have_txt) {
				break;
			}
			result = dns_rdataset_next(&rdataset);
		}
		dns_rdataset_disassociate(&rdataset);

	notxt:
		if (have_spf && !have_txt) {
			dns_name_format(name, namebuf, sizeof(namebuf));
			dns_zone_log(zone, ISC_LOG_WARNING,
				     "'%s' found type "
				     "SPF record but no SPF TXT record found, "
				     "add matching type TXT record",
				     namebuf);
		}

	next:
		dns_db_detachnode(db, &node);
		result = dns_dbiterator_next(dbiterator);
	}

cleanup:
	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}
	dns_dbiterator_destroy(&dbiterator);

	return ok;
}

/*
 * OpenSSL verification of RSA keys with exponent 3 is known to be
 * broken prior OpenSSL 0.9.8c/0.9.7k.	Look for such keys and warn
 * if they are in use.
 */
static void
zone_check_dnskeys(dns_zone_t *zone, dns_db_t *db) {
	dns_dbnode_t *node = NULL;
	dns_dbversion_t *version = NULL;
	dns_rdata_dnskey_t dnskey;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	dns_rdataset_t rdataset;
	isc_result_t result;

	result = dns_db_findnode(db, &zone->origin, false, &node);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	dns_db_currentversion(db, &version);
	dns_rdataset_init(&rdataset);
	result = dns_db_findrdataset(db, node, version, dns_rdatatype_dnskey,
				     dns_rdatatype_none, 0, &rdataset, NULL);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	for (result = dns_rdataset_first(&rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&rdataset))
	{
		dns_rdataset_current(&rdataset, &rdata);
		result = dns_rdata_tostruct(&rdata, &dnskey, NULL);
		INSIST(result == ISC_R_SUCCESS);

		/*
		 * RFC 3110, section 4: Performance Considerations:
		 *
		 * A public exponent of 3 minimizes the effort needed to verify
		 * a signature.  Use of 3 as the public exponent is weak for
		 * confidentiality uses since, if the same data can be collected
		 * encrypted under three different keys with an exponent of 3
		 * then, using the Chinese Remainder Theorem [NETSEC], the
		 * original plain text can be easily recovered.  If a key is
		 * known to be used only for authentication, as is the case with
		 * DNSSEC, then an exponent of 3 is acceptable.  However other
		 * applications in the future may wish to leverage DNS
		 * distributed keys for applications that do require
		 * confidentiality.  For keys which might have such other uses,
		 * a more conservative choice would be 65537 (F4, the fourth
		 * fermat number).
		 */
		if (dnskey.datalen > 1 && dnskey.data[0] == 1 &&
		    dnskey.data[1] == 3 &&
		    (dnskey.algorithm == DNS_KEYALG_RSAMD5 ||
		     dnskey.algorithm == DNS_KEYALG_RSASHA1 ||
		     dnskey.algorithm == DNS_KEYALG_NSEC3RSASHA1 ||
		     dnskey.algorithm == DNS_KEYALG_RSASHA256 ||
		     dnskey.algorithm == DNS_KEYALG_RSASHA512))
		{
			char algorithm[DNS_SECALG_FORMATSIZE];
			isc_region_t r;

			dns_rdata_toregion(&rdata, &r);
			dns_secalg_format(dnskey.algorithm, algorithm,
					  sizeof(algorithm));

			dnssec_log(zone, ISC_LOG_WARNING,
				   "weak %s (%u) key found (exponent=3, id=%u)",
				   algorithm, dnskey.algorithm,
				   dst_region_computeid(&r));
		}
		dns_rdata_reset(&rdata);
	}
	dns_rdataset_disassociate(&rdataset);

cleanup:
	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}
	if (version != NULL) {
		dns_db_closeversion(db, &version, false);
	}
}

static void
resume_signingwithkey(dns_zone_t *zone) {
	dns_dbnode_t *node = NULL;
	dns_dbversion_t *version = NULL;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	dns_rdataset_t rdataset;
	isc_result_t result;
	dns_db_t *db = NULL;

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	if (zone->db != NULL) {
		dns_db_attach(zone->db, &db);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
	if (db == NULL) {
		goto cleanup;
	}

	result = dns_db_findnode(db, &zone->origin, false, &node);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	dns_db_currentversion(db, &version);
	dns_rdataset_init(&rdataset);
	result = dns_db_findrdataset(db, node, version, zone->privatetype,
				     dns_rdatatype_none, 0, &rdataset, NULL);
	if (result != ISC_R_SUCCESS) {
		INSIST(!dns_rdataset_isassociated(&rdataset));
		goto cleanup;
	}

	for (result = dns_rdataset_first(&rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&rdataset))
	{
		dns_rdataset_current(&rdataset, &rdata);
		if (rdata.length != 5 || rdata.data[0] == 0 ||
		    rdata.data[4] != 0)
		{
			dns_rdata_reset(&rdata);
			continue;
		}

		result = zone_signwithkey(zone, rdata.data[0],
					  (rdata.data[1] << 8) | rdata.data[2],
					  rdata.data[3]);
		if (result != ISC_R_SUCCESS) {
			dnssec_log(zone, ISC_LOG_ERROR,
				   "zone_signwithkey failed: %s",
				   isc_result_totext(result));
		}
		dns_rdata_reset(&rdata);
	}
	dns_rdataset_disassociate(&rdataset);

cleanup:
	if (db != NULL) {
		if (node != NULL) {
			dns_db_detachnode(db, &node);
		}
		if (version != NULL) {
			dns_db_closeversion(db, &version, false);
		}
		dns_db_detach(&db);
	}
}

/*
 * Initiate adding/removing NSEC3 records belonging to the chain defined by the
 * supplied NSEC3PARAM RDATA.
 *
 * Zone must be locked by caller.
 */
static isc_result_t
zone_addnsec3chain(dns_zone_t *zone, dns_rdata_nsec3param_t *nsec3param) {
	dns_nsec3chain_t *nsec3chain, *current;
	dns_dbversion_t *version = NULL;
	bool nseconly = false, nsec3ok = false;
	isc_result_t result;
	isc_time_t now;
	unsigned int options = 0;
	char saltbuf[255 * 2 + 1];
	char flags[sizeof("INITIAL|REMOVE|CREATE|NONSEC|OPTOUT")];
	dns_db_t *db = NULL;

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	if (zone->db != NULL) {
		dns_db_attach(zone->db, &db);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);

	if (db == NULL) {
		result = ISC_R_SUCCESS;
		goto cleanup;
	}

	/*
	 * If this zone is not NSEC3-capable, attempting to remove any NSEC3
	 * chain from it is pointless as it would not be possible for the
	 * latter to exist in the first place.
	 */
	dns_db_currentversion(db, &version);
	result = dns_nsec_nseconly(db, version, NULL, &nseconly);
	nsec3ok = (result == ISC_R_SUCCESS && !nseconly);
	dns_db_closeversion(db, &version, false);
	if (!nsec3ok && (nsec3param->flags & DNS_NSEC3FLAG_REMOVE) == 0) {
		result = ISC_R_SUCCESS;
		goto cleanup;
	}

	/*
	 * Allocate and initialize structure preserving state of
	 * adding/removing records belonging to this NSEC3 chain between
	 * separate zone_nsec3chain() calls.
	 */
	nsec3chain = isc_mem_get(zone->mctx, sizeof *nsec3chain);

	nsec3chain->magic = 0;
	nsec3chain->done = false;
	nsec3chain->db = NULL;
	nsec3chain->dbiterator = NULL;
	nsec3chain->nsec3param.common.rdclass = nsec3param->common.rdclass;
	nsec3chain->nsec3param.common.rdtype = nsec3param->common.rdtype;
	nsec3chain->nsec3param.hash = nsec3param->hash;
	nsec3chain->nsec3param.iterations = nsec3param->iterations;
	nsec3chain->nsec3param.flags = nsec3param->flags;
	nsec3chain->nsec3param.salt_length = nsec3param->salt_length;
	memmove(nsec3chain->salt, nsec3param->salt, nsec3param->salt_length);
	nsec3chain->nsec3param.salt = nsec3chain->salt;
	nsec3chain->seen_nsec = false;
	nsec3chain->delete_nsec = false;
	nsec3chain->save_delete_nsec = false;

	/*
	 * Log NSEC3 parameters defined by supplied NSEC3PARAM RDATA.
	 */
	if (nsec3param->flags == 0) {
		strlcpy(flags, "NONE", sizeof(flags));
	} else {
		flags[0] = '\0';
		if ((nsec3param->flags & DNS_NSEC3FLAG_REMOVE) != 0) {
			strlcat(flags, "REMOVE", sizeof(flags));
		}
		if ((nsec3param->flags & DNS_NSEC3FLAG_INITIAL) != 0) {
			if (flags[0] == '\0') {
				strlcpy(flags, "INITIAL", sizeof(flags));
			} else {
				strlcat(flags, "|INITIAL", sizeof(flags));
			}
		}
		if ((nsec3param->flags & DNS_NSEC3FLAG_CREATE) != 0) {
			if (flags[0] == '\0') {
				strlcpy(flags, "CREATE", sizeof(flags));
			} else {
				strlcat(flags, "|CREATE", sizeof(flags));
			}
		}
		if ((nsec3param->flags & DNS_NSEC3FLAG_NONSEC) != 0) {
			if (flags[0] == '\0') {
				strlcpy(flags, "NONSEC", sizeof(flags));
			} else {
				strlcat(flags, "|NONSEC", sizeof(flags));
			}
		}
		if ((nsec3param->flags & DNS_NSEC3FLAG_OPTOUT) != 0) {
			if (flags[0] == '\0') {
				strlcpy(flags, "OPTOUT", sizeof(flags));
			} else {
				strlcat(flags, "|OPTOUT", sizeof(flags));
			}
		}
	}
	result = dns_nsec3param_salttotext(nsec3param, saltbuf,
					   sizeof(saltbuf));
	RUNTIME_CHECK(result == ISC_R_SUCCESS);
	dnssec_log(zone, ISC_LOG_INFO, "zone_addnsec3chain(%u,%s,%u,%s)",
		   nsec3param->hash, flags, nsec3param->iterations, saltbuf);

	/*
	 * If the NSEC3 chain defined by the supplied NSEC3PARAM RDATA is
	 * currently being processed, interrupt its processing to avoid
	 * simultaneously adding and removing records for the same NSEC3 chain.
	 */
	for (current = ISC_LIST_HEAD(zone->nsec3chain); current != NULL;
	     current = ISC_LIST_NEXT(current, link))
	{
		if ((current->db == db) &&
		    (current->nsec3param.hash == nsec3param->hash) &&
		    (current->nsec3param.iterations ==
		     nsec3param->iterations) &&
		    (current->nsec3param.salt_length ==
		     nsec3param->salt_length) &&
		    memcmp(current->nsec3param.salt, nsec3param->salt,
			   nsec3param->salt_length) == 0)
		{
			current->done = true;
		}
	}

	/*
	 * Attach zone database to the structure initialized above and create
	 * an iterator for it with appropriate options in order to avoid
	 * creating NSEC3 records for NSEC3 records.
	 */
	dns_db_attach(db, &nsec3chain->db);
	if ((nsec3chain->nsec3param.flags & DNS_NSEC3FLAG_CREATE) != 0) {
		options = DNS_DB_NONSEC3;
	}
	result = dns_db_createiterator(nsec3chain->db, options,
				       &nsec3chain->dbiterator);
	if (result == ISC_R_SUCCESS) {
		result = dns_dbiterator_first(nsec3chain->dbiterator);
	}
	if (result == ISC_R_SUCCESS) {
		/*
		 * Database iterator initialization succeeded.  We are now
		 * ready to kick off adding/removing records belonging to this
		 * NSEC3 chain.  Append the structure initialized above to the
		 * "nsec3chain" list for the zone and set the appropriate zone
		 * timer so that zone_nsec3chain() is called as soon as
		 * possible.
		 */
		dns_dbiterator_pause(nsec3chain->dbiterator);
		ISC_LIST_INITANDAPPEND(zone->nsec3chain, nsec3chain, link);
		nsec3chain = NULL;
		if (isc_time_isepoch(&zone->nsec3chaintime)) {
			now = isc_time_now();
			zone->nsec3chaintime = now;
			if (zone->loop != NULL) {
				zone_settimer(zone, &now);
			}
		}
	}

	if (nsec3chain != NULL) {
		if (nsec3chain->db != NULL) {
			dns_db_detach(&nsec3chain->db);
		}
		if (nsec3chain->dbiterator != NULL) {
			dns_dbiterator_destroy(&nsec3chain->dbiterator);
		}
		isc_mem_put(zone->mctx, nsec3chain, sizeof *nsec3chain);
	}

cleanup:
	if (db != NULL) {
		dns_db_detach(&db);
	}
	return result;
}

/*
 * Find private-type records at the zone apex which signal that an NSEC3 chain
 * should be added or removed.  For each such record, extract NSEC3PARAM RDATA
 * and pass it to zone_addnsec3chain().
 *
 * Zone must be locked by caller.
 */
static void
resume_addnsec3chain(dns_zone_t *zone) {
	dns_dbnode_t *node = NULL;
	dns_dbversion_t *version = NULL;
	dns_rdataset_t rdataset;
	isc_result_t result;
	dns_rdata_nsec3param_t nsec3param;
	bool nseconly = false, nsec3ok = false;
	dns_db_t *db = NULL;

	INSIST(LOCKED_ZONE(zone));

	if (zone->privatetype == 0) {
		return;
	}

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	if (zone->db != NULL) {
		dns_db_attach(zone->db, &db);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
	if (db == NULL) {
		goto cleanup;
	}

	result = dns_db_findnode(db, &zone->origin, false, &node);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	dns_db_currentversion(db, &version);

	/*
	 * In order to create NSEC3 chains we need the DNSKEY RRset at zone
	 * apex to exist and contain no keys using NSEC-only algorithms.
	 */
	result = dns_nsec_nseconly(db, version, NULL, &nseconly);
	nsec3ok = (result == ISC_R_SUCCESS && !nseconly);

	/*
	 * Get the RRset containing all private-type records at the zone apex.
	 */
	dns_rdataset_init(&rdataset);
	result = dns_db_findrdataset(db, node, version, zone->privatetype,
				     dns_rdatatype_none, 0, &rdataset, NULL);
	if (result != ISC_R_SUCCESS) {
		INSIST(!dns_rdataset_isassociated(&rdataset));
		goto cleanup;
	}

	for (result = dns_rdataset_first(&rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&rdataset))
	{
		unsigned char buf[DNS_NSEC3PARAM_BUFFERSIZE];
		dns_rdata_t rdata = DNS_RDATA_INIT;
		dns_rdata_t private = DNS_RDATA_INIT;

		dns_rdataset_current(&rdataset, &private);
		/*
		 * Try extracting NSEC3PARAM RDATA from this private-type
		 * record.  Failure means this private-type record does not
		 * represent an NSEC3PARAM record, so skip it.
		 */
		if (!dns_nsec3param_fromprivate(&private, &rdata, buf,
						sizeof(buf)))
		{
			continue;
		}
		result = dns_rdata_tostruct(&rdata, &nsec3param, NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
		if (((nsec3param.flags & DNS_NSEC3FLAG_REMOVE) != 0) ||
		    ((nsec3param.flags & DNS_NSEC3FLAG_CREATE) != 0 && nsec3ok))
		{
			/*
			 * Pass the NSEC3PARAM RDATA contained in this
			 * private-type record to zone_addnsec3chain() so that
			 * it can kick off adding or removing NSEC3 records.
			 */
			result = zone_addnsec3chain(zone, &nsec3param);
			if (result != ISC_R_SUCCESS) {
				dnssec_log(zone, ISC_LOG_ERROR,
					   "zone_addnsec3chain failed: %s",
					   isc_result_totext(result));
			}
		}
	}
	dns_rdataset_disassociate(&rdataset);

cleanup:
	if (db != NULL) {
		if (node != NULL) {
			dns_db_detachnode(db, &node);
		}
		if (version != NULL) {
			dns_db_closeversion(db, &version, false);
		}
		dns_db_detach(&db);
	}
}

static void
set_resigntime(dns_zone_t *zone) {
	dns_fixedname_t fixed;
	isc_stdtime_t resign;
	isc_result_t result;
	uint32_t nanosecs;
	dns_db_t *db = NULL;
	dns_typepair_t typepair;

	INSIST(LOCKED_ZONE(zone));

	/* We only re-sign zones that can be dynamically updated */
	if (!dns_zone_isdynamic(zone, false)) {
		return;
	}

	if (inline_raw(zone)) {
		return;
	}

	dns_fixedname_init(&fixed);

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	if (zone->db != NULL) {
		dns_db_attach(zone->db, &db);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
	if (db == NULL) {
		isc_time_settoepoch(&zone->resigntime);
		return;
	}

	result = dns_db_getsigningtime(db, &resign, dns_fixedname_name(&fixed),
				       &typepair);
	if (result != ISC_R_SUCCESS) {
		isc_time_settoepoch(&zone->resigntime);
		goto cleanup;
	}

	resign -= dns_zone_getsigresigninginterval(zone);
	nanosecs = isc_random_uniform(1000000000);
	isc_time_set(&zone->resigntime, resign, nanosecs);

cleanup:
	dns_db_detach(&db);
	return;
}

static isc_result_t
check_nsec3param(dns_zone_t *zone, dns_db_t *db) {
	bool ok = false;
	dns_dbnode_t *node = NULL;
	dns_dbversion_t *version = NULL;
	dns_rdata_nsec3param_t nsec3param;
	dns_rdataset_t rdataset;
	isc_result_t result;
	bool dynamic = (zone->type == dns_zone_primary)
			       ? dns_zone_isdynamic(zone, false)
			       : false;

	dns_rdataset_init(&rdataset);
	result = dns_db_findnode(db, &zone->origin, false, &node);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "nsec3param lookup failure: %s",
			     isc_result_totext(result));
		return result;
	}
	dns_db_currentversion(db, &version);

	result = dns_db_findrdataset(db, node, version,
				     dns_rdatatype_nsec3param,
				     dns_rdatatype_none, 0, &rdataset, NULL);
	if (result == ISC_R_NOTFOUND) {
		INSIST(!dns_rdataset_isassociated(&rdataset));
		result = ISC_R_SUCCESS;
		goto cleanup;
	}
	if (result != ISC_R_SUCCESS) {
		INSIST(!dns_rdataset_isassociated(&rdataset));
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "nsec3param lookup failure: %s",
			     isc_result_totext(result));
		goto cleanup;
	}

	for (result = dns_rdataset_first(&rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&rdataset))
	{
		dns_rdata_t rdata = DNS_RDATA_INIT;

		dns_rdataset_current(&rdataset, &rdata);
		result = dns_rdata_tostruct(&rdata, &nsec3param, NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);

		/*
		 * For dynamic zones we must support every algorithm so we
		 * can regenerate all the NSEC3 chains.
		 * For non-dynamic zones we only need to find a supported
		 * algorithm.
		 */
		if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_NSEC3TESTZONE) &&
		    nsec3param.hash == DNS_NSEC3_UNKNOWNALG && !dynamic)
		{
			dns_zone_log(zone, ISC_LOG_WARNING,
				     "nsec3 test \"unknown\" hash algorithm "
				     "found: %u",
				     nsec3param.hash);
			ok = true;
		} else if (!dns_nsec3_supportedhash(nsec3param.hash)) {
			if (dynamic) {
				dns_zone_log(zone, ISC_LOG_ERROR,
					     "unsupported nsec3 hash algorithm"
					     " in dynamic zone: %u",
					     nsec3param.hash);
				result = DNS_R_BADZONE;
				/* Stop second error message. */
				ok = true;
				break;
			} else {
				dns_zone_log(zone, ISC_LOG_WARNING,
					     "unsupported nsec3 hash "
					     "algorithm: %u",
					     nsec3param.hash);
			}
		} else {
			ok = true;
		}

		/*
		 * Warn if the zone has excessive NSEC3 iterations.
		 */
		if (nsec3param.iterations > dns_nsec3_maxiterations()) {
			dnssec_log(zone, ISC_LOG_WARNING,
				   "excessive NSEC3PARAM iterations %u > %u",
				   nsec3param.iterations,
				   dns_nsec3_maxiterations());
		}
	}
	if (result == ISC_R_NOMORE) {
		result = ISC_R_SUCCESS;
	}

	if (!ok) {
		result = DNS_R_BADZONE;
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "no supported nsec3 hash algorithm");
	}

cleanup:
	if (dns_rdataset_isassociated(&rdataset)) {
		dns_rdataset_disassociate(&rdataset);
	}
	dns_db_closeversion(db, &version, false);
	dns_db_detachnode(db, &node);
	return result;
}

/*
 * Set the timer for refreshing the key zone to the soonest future time
 * of the set (current timer, keydata->refresh, keydata->addhd,
 * keydata->removehd).
 */
static void
set_refreshkeytimer(dns_zone_t *zone, dns_rdata_keydata_t *key,
		    isc_stdtime_t now, bool force) {
	isc_stdtime_t then;
	isc_time_t timenow, timethen;
	char timebuf[80];

	ENTER;
	then = key->refresh;
	if (force) {
		then = now;
	}
	if (key->addhd > now && key->addhd < then) {
		then = key->addhd;
	}
	if (key->removehd > now && key->removehd < then) {
		then = key->removehd;
	}

	timenow = isc_time_now();
	if (then > now) {
		DNS_ZONE_TIME_ADD(&timenow, then - now, &timethen);
	} else {
		timethen = timenow;
	}
	if (isc_time_compare(&zone->refreshkeytime, &timenow) < 0 ||
	    isc_time_compare(&timethen, &zone->refreshkeytime) < 0)
	{
		zone->refreshkeytime = timethen;
	}

	isc_time_formattimestamp(&zone->refreshkeytime, timebuf, 80);
	dns_zone_log(zone, ISC_LOG_DEBUG(1), "next key refresh: %s", timebuf);
	zone_settimer(zone, &timenow);
}

/*
 * If keynode references a key or a DS rdataset, and if the key
 * zone does not contain a KEYDATA record for the corresponding name,
 * then create an empty KEYDATA and push it into the zone as a placeholder,
 * then schedule a key refresh immediately. This new KEYDATA record will be
 * updated during the refresh.
 *
 * If the key zone is changed, set '*changed' to true.
 */
static isc_result_t
create_keydata(dns_zone_t *zone, dns_db_t *db, dns_dbversion_t *ver,
	       dns_diff_t *diff, dns_keynode_t *keynode, dns_name_t *keyname,
	       bool *changed) {
	isc_result_t result = ISC_R_SUCCESS;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	dns_rdata_keydata_t kd;
	unsigned char rrdata[4096];
	isc_buffer_t rrdatabuf;
	isc_stdtime_t now = isc_stdtime_now();

	REQUIRE(keynode != NULL);

	ENTER;

	/*
	 * If the keynode has no trust anchor set, we shouldn't be here.
	 */
	if (!dns_keynode_dsset(keynode, NULL)) {
		return ISC_R_FAILURE;
	}

	memset(&kd, 0, sizeof(kd));
	kd.common.rdclass = zone->rdclass;
	kd.common.rdtype = dns_rdatatype_keydata;
	ISC_LINK_INIT(&kd.common, link);

	isc_buffer_init(&rrdatabuf, rrdata, sizeof(rrdata));

	CHECK(dns_rdata_fromstruct(&rdata, zone->rdclass, dns_rdatatype_keydata,
				   &kd, &rrdatabuf));
	/* Add rdata to zone. */
	CHECK(update_one_rr(db, ver, diff, DNS_DIFFOP_ADD, keyname, 0, &rdata));
	*changed = true;

	/* Refresh new keys from the zone apex as soon as possible. */
	set_refreshkeytimer(zone, &kd, now, true);
	return ISC_R_SUCCESS;

failure:
	return result;
}

/*
 * Remove from the key zone all the KEYDATA records found in rdataset.
 */
static isc_result_t
delete_keydata(dns_db_t *db, dns_dbversion_t *ver, dns_diff_t *diff,
	       dns_name_t *name, dns_rdataset_t *rdataset) {
	dns_rdata_t rdata = DNS_RDATA_INIT;
	isc_result_t result, uresult;

	for (result = dns_rdataset_first(rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(rdataset))
	{
		dns_rdata_reset(&rdata);
		dns_rdataset_current(rdataset, &rdata);
		uresult = update_one_rr(db, ver, diff, DNS_DIFFOP_DEL, name, 0,
					&rdata);
		if (uresult != ISC_R_SUCCESS) {
			return uresult;
		}
	}
	if (result == ISC_R_NOMORE) {
		result = ISC_R_SUCCESS;
	}
	return result;
}

/*
 * Compute the DNSSEC key ID for a DNSKEY record.
 */
static isc_result_t
compute_tag(dns_name_t *name, dns_rdata_dnskey_t *dnskey, isc_mem_t *mctx,
	    dns_keytag_t *tag) {
	isc_result_t result;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	unsigned char data[4096];
	isc_buffer_t buffer;
	dst_key_t *dstkey = NULL;

	isc_buffer_init(&buffer, data, sizeof(data));
	dns_rdata_fromstruct(&rdata, dnskey->common.rdclass,
			     dns_rdatatype_dnskey, dnskey, &buffer);

	result = dns_dnssec_keyfromrdata(name, &rdata, mctx, &dstkey);
	if (result == ISC_R_SUCCESS) {
		*tag = dst_key_id(dstkey);
		dst_key_free(&dstkey);
	}

	return result;
}

/*
 * Synth-from-dnssec callbacks to add/delete names from namespace tree.
 */
static void
sfd_add(const dns_name_t *name, void *arg) {
	if (arg != NULL) {
		dns_view_sfd_add(arg, name);
	}
}

static void
sfd_del(const dns_name_t *name, void *arg) {
	if (arg != NULL) {
		dns_view_sfd_del(arg, name);
	}
}

/*
 * Add key to the security roots.
 */
static void
trust_key(dns_zone_t *zone, dns_name_t *keyname, dns_rdata_dnskey_t *dnskey,
	  bool initial) {
	isc_result_t result;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	unsigned char data[4096], digest[ISC_MAX_MD_SIZE];
	isc_buffer_t buffer;
	dns_keytable_t *sr = NULL;
	dns_rdata_ds_t ds;

	result = dns_view_getsecroots(zone->view, &sr);
	if (result != ISC_R_SUCCESS) {
		return;
	}

	/* Build DS record for key. */
	isc_buffer_init(&buffer, data, sizeof(data));
	dns_rdata_fromstruct(&rdata, dnskey->common.rdclass,
			     dns_rdatatype_dnskey, dnskey, &buffer);
	CHECK(dns_ds_fromkeyrdata(keyname, &rdata, DNS_DSDIGEST_SHA256, digest,
				  &ds));
	CHECK(dns_keytable_add(sr, true, initial, keyname, &ds, sfd_add,
			       zone->view));

	dns_keytable_detach(&sr);

failure:
	if (sr != NULL) {
		dns_keytable_detach(&sr);
	}
	return;
}

/*
 * Add a null key to the security roots for so that all queries
 * to the zone will fail.
 */
static void
fail_secure(dns_zone_t *zone, dns_name_t *keyname) {
	isc_result_t result;
	dns_keytable_t *sr = NULL;

	result = dns_view_getsecroots(zone->view, &sr);
	if (result == ISC_R_SUCCESS) {
		dns_keytable_marksecure(sr, keyname);
		dns_keytable_detach(&sr);
	}
}

/*
 * Scan a set of KEYDATA records from the key zone.  The ones that are
 * valid (i.e., the add holddown timer has expired) become trusted keys.
 */
static void
load_secroots(dns_zone_t *zone, dns_name_t *name, dns_rdataset_t *rdataset) {
	isc_result_t result;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	dns_rdata_keydata_t keydata;
	dns_rdata_dnskey_t dnskey;
	int trusted = 0, revoked = 0, pending = 0;
	isc_stdtime_t now = isc_stdtime_now();
	dns_keytable_t *sr = NULL;

	result = dns_view_getsecroots(zone->view, &sr);
	if (result == ISC_R_SUCCESS) {
		dns_keytable_delete(sr, name, sfd_del, zone->view);
		dns_keytable_detach(&sr);
	}

	/* Now insert all the accepted trust anchors from this keydata set. */
	for (result = dns_rdataset_first(rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(rdataset))
	{
		dns_rdata_reset(&rdata);
		dns_rdataset_current(rdataset, &rdata);

		/* Convert rdata to keydata. */
		result = dns_rdata_tostruct(&rdata, &keydata, NULL);
		if (result == ISC_R_NOTIMPLEMENTED) {
			continue;
		}
		RUNTIME_CHECK(result == ISC_R_SUCCESS);

		/* Set the key refresh timer to force a fast refresh. */
		set_refreshkeytimer(zone, &keydata, now, true);

		/* If the removal timer is nonzero, this key was revoked. */
		if (keydata.removehd != 0) {
			revoked++;
			continue;
		}

		/*
		 * If the add timer is still pending, this key is not
		 * trusted yet.
		 */
		if (now < keydata.addhd) {
			pending++;
			continue;
		}

		/* Convert keydata to dnskey. */
		dns_keydata_todnskey(&keydata, &dnskey, NULL);

		/* Add to keytables. */
		trusted++;
		trust_key(zone, name, &dnskey, keydata.addhd == 0);
	}

	if (trusted == 0 && pending != 0) {
		char namebuf[DNS_NAME_FORMATSIZE];
		dns_name_format(name, namebuf, sizeof namebuf);
		dnssec_log(zone, ISC_LOG_ERROR,
			   "No valid trust anchors for '%s'!", namebuf);
		dnssec_log(zone, ISC_LOG_ERROR,
			   "%d key(s) revoked, %d still pending", revoked,
			   pending);
		dnssec_log(zone, ISC_LOG_ERROR, "All queries to '%s' will fail",
			   namebuf);
		fail_secure(zone, name);
	}
}

static isc_result_t
do_one_tuple(dns_difftuple_t **tuple, dns_db_t *db, dns_dbversion_t *ver,
	     dns_diff_t *diff) {
	dns_diff_t temp_diff;
	isc_result_t result;

	/*
	 * Create a singleton diff.
	 */
	dns_diff_init(diff->mctx, &temp_diff);
	ISC_LIST_APPEND(temp_diff.tuples, *tuple, link);

	/*
	 * Apply it to the database.
	 */
	result = dns_diff_apply(&temp_diff, db, ver);
	ISC_LIST_UNLINK(temp_diff.tuples, *tuple, link);
	if (result != ISC_R_SUCCESS) {
		dns_difftuple_free(tuple);
		return result;
	}

	/*
	 * Merge it into the current pending journal entry.
	 */
	dns_diff_appendminimal(diff, tuple);

	/*
	 * Do not clear temp_diff.
	 */
	return ISC_R_SUCCESS;
}

static isc_result_t
update_one_rr(dns_db_t *db, dns_dbversion_t *ver, dns_diff_t *diff,
	      dns_diffop_t op, dns_name_t *name, dns_ttl_t ttl,
	      dns_rdata_t *rdata) {
	dns_difftuple_t *tuple = NULL;
	isc_result_t result;
	result = dns_difftuple_create(diff->mctx, op, name, ttl, rdata, &tuple);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	return do_one_tuple(&tuple, db, ver, diff);
}

static isc_result_t
update_soa_serial(dns_zone_t *zone, dns_db_t *db, dns_dbversion_t *ver,
		  dns_diff_t *diff, isc_mem_t *mctx,
		  dns_updatemethod_t method) {
	dns_difftuple_t *deltuple = NULL;
	dns_difftuple_t *addtuple = NULL;
	uint32_t serial;
	isc_result_t result;
	dns_updatemethod_t used = dns_updatemethod_none;

	INSIST(method != dns_updatemethod_none);

	CHECK(dns_db_createsoatuple(db, ver, mctx, DNS_DIFFOP_DEL, &deltuple));
	CHECK(dns_difftuple_copy(deltuple, &addtuple));
	addtuple->op = DNS_DIFFOP_ADD;

	serial = dns_soa_getserial(&addtuple->rdata);
	serial = dns_update_soaserial(serial, method, &used);
	if (method != used) {
		dns_zone_log(zone, ISC_LOG_WARNING,
			     "update_soa_serial:new serial would be lower than "
			     "old serial, using increment method instead");
	}
	dns_soa_setserial(serial, &addtuple->rdata);
	CHECK(do_one_tuple(&deltuple, db, ver, diff));
	CHECK(do_one_tuple(&addtuple, db, ver, diff));
	result = ISC_R_SUCCESS;

failure:
	if (addtuple != NULL) {
		dns_difftuple_free(&addtuple);
	}
	if (deltuple != NULL) {
		dns_difftuple_free(&deltuple);
	}
	return result;
}

/*
 * Write all transactions in 'diff' to the zone journal file.
 */
static isc_result_t
zone_journal(dns_zone_t *zone, dns_diff_t *diff, uint32_t *sourceserial,
	     const char *caller) {
	const char *journalfile;
	isc_result_t result = ISC_R_SUCCESS;
	dns_journal_t *journal = NULL;
	unsigned int mode = DNS_JOURNAL_CREATE | DNS_JOURNAL_WRITE;

	ENTER;
	journalfile = dns_zone_getjournal(zone);
	if (journalfile != NULL) {
		result = dns_journal_open(zone->mctx, journalfile, mode,
					  &journal);
		if (result != ISC_R_SUCCESS) {
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "%s:dns_journal_open -> %s", caller,
				     isc_result_totext(result));
			return result;
		}

		if (sourceserial != NULL) {
			dns_journal_set_sourceserial(journal, *sourceserial);
		}

		result = dns_journal_write_transaction(journal, diff);
		if (result != ISC_R_SUCCESS) {
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "%s:dns_journal_write_transaction -> %s",
				     caller, isc_result_totext(result));
		}
		dns_journal_destroy(&journal);
	}

	return result;
}

/*
 * Create an SOA record for a newly-created zone
 */
static isc_result_t
add_soa(dns_zone_t *zone, dns_db_t *db) {
	isc_result_t result;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	unsigned char buf[DNS_SOA_BUFFERSIZE];
	dns_dbversion_t *ver = NULL;
	dns_diff_t diff;

	dns_zone_log(zone, ISC_LOG_DEBUG(1), "creating SOA");

	dns_diff_init(zone->mctx, &diff);
	result = dns_db_newversion(db, &ver);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "add_soa:dns_db_newversion -> %s",
			     isc_result_totext(result));
		goto failure;
	}

	/* Build SOA record */
	result = dns_soa_buildrdata(&zone->origin, dns_rootname, zone->rdclass,
				    0, 0, 0, 0, 0, buf, &rdata);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "add_soa:dns_soa_buildrdata -> %s",
			     isc_result_totext(result));
		goto failure;
	}

	result = update_one_rr(db, ver, &diff, DNS_DIFFOP_ADD, &zone->origin, 0,
			       &rdata);

failure:
	dns_diff_clear(&diff);
	if (ver != NULL) {
		dns_db_closeversion(db, &ver, result == ISC_R_SUCCESS);
	}

	INSIST(ver == NULL);

	return result;
}

struct addifmissing_arg {
	dns_db_t *db;
	dns_dbversion_t *ver;
	dns_diff_t *diff;
	dns_zone_t *zone;
	bool *changed;
	isc_result_t result;
};

static void
addifmissing(dns_keytable_t *keytable, dns_keynode_t *keynode,
	     dns_name_t *keyname, void *arg) {
	dns_db_t *db = ((struct addifmissing_arg *)arg)->db;
	dns_dbversion_t *ver = ((struct addifmissing_arg *)arg)->ver;
	dns_diff_t *diff = ((struct addifmissing_arg *)arg)->diff;
	dns_zone_t *zone = ((struct addifmissing_arg *)arg)->zone;
	bool *changed = ((struct addifmissing_arg *)arg)->changed;
	isc_result_t result;
	dns_fixedname_t fname;

	UNUSED(keytable);

	if (((struct addifmissing_arg *)arg)->result != ISC_R_SUCCESS) {
		return;
	}

	if (!dns_keynode_managed(keynode)) {
		return;
	}

	/*
	 * If the keynode has no trust anchor set, return.
	 */
	if (!dns_keynode_dsset(keynode, NULL)) {
		return;
	}

	/*
	 * Check whether there's already a KEYDATA entry for this name;
	 * if so, we don't need to add another.
	 */
	dns_fixedname_init(&fname);
	result = dns_db_find(db, keyname, ver, dns_rdatatype_keydata,
			     DNS_DBFIND_NOWILD, 0, NULL,
			     dns_fixedname_name(&fname), NULL, NULL);
	if (result == ISC_R_SUCCESS) {
		return;
	}

	/*
	 * Create the keydata.
	 */
	result = create_keydata(zone, db, ver, diff, keynode, keyname, changed);
	if (result != ISC_R_SUCCESS && result != ISC_R_NOMORE) {
		((struct addifmissing_arg *)arg)->result = result;
	}
}

/*
 * Synchronize the set of initializing keys found in managed-keys {}
 * statements with the set of trust anchors found in the managed-keys.bind
 * zone.  If a domain is no longer named in managed-keys, delete all keys
 * from that domain from the key zone.	If a domain is configured as an
 * initial-key in trust-anchors, but there are no references to it in the
 * key zone, load the key zone with the initializing key(s) for that
 * domain and schedule a key refresh. If a domain is configured as
 * an initial-ds in trust-anchors, fetch the DNSKEY RRset, load the key
 * zone with the matching key, and schedule a key refresh.
 */
static isc_result_t
sync_keyzone(dns_zone_t *zone, dns_db_t *db) {
	isc_result_t result = ISC_R_SUCCESS;
	bool changed = false;
	bool commit = false;
	dns_keynode_t *keynode = NULL;
	dns_view_t *view = zone->view;
	dns_keytable_t *sr = NULL;
	dns_dbversion_t *ver = NULL;
	dns_diff_t diff;
	dns_rriterator_t rrit;
	struct addifmissing_arg arg;

	dns_zone_log(zone, ISC_LOG_DEBUG(1), "synchronizing trusted keys");

	dns_diff_init(zone->mctx, &diff);

	CHECK(dns_view_getsecroots(view, &sr));

	result = dns_db_newversion(db, &ver);
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR,
			   "sync_keyzone:dns_db_newversion -> %s",
			   isc_result_totext(result));
		goto failure;
	}

	/*
	 * Walk the zone DB.  If we find any keys whose names are no longer
	 * in trust-anchors, or which have been changed from initial to static,
	 * (meaning they are permanent and not RFC5011-maintained), delete
	 * them from the zone.  Otherwise call load_secroots(), which
	 * loads keys into secroots as appropriate.
	 */
	dns_rriterator_init(&rrit, db, ver, 0);
	for (result = dns_rriterator_first(&rrit); result == ISC_R_SUCCESS;
	     result = dns_rriterator_nextrrset(&rrit))
	{
		dns_rdataset_t *rdataset = NULL;
		dns_rdata_t rdata = DNS_RDATA_INIT;
		dns_rdata_keydata_t keydata;
		isc_stdtime_t now = isc_stdtime_now();
		bool load = true;
		dns_name_t *rrname = NULL;
		uint32_t ttl;

		dns_rriterator_current(&rrit, &rrname, &ttl, &rdataset, NULL);
		if (!dns_rdataset_isassociated(rdataset)) {
			dns_rriterator_destroy(&rrit);
			goto failure;
		}

		if (rdataset->type != dns_rdatatype_keydata) {
			continue;
		}

		/*
		 * The managed-keys zone can contain a placeholder instead of
		 * legitimate data, in which case we will not use it, and we
		 * will try to refresh it.
		 */
		for (result = dns_rdataset_first(rdataset);
		     result == ISC_R_SUCCESS;
		     result = dns_rdataset_next(rdataset))
		{
			isc_result_t iresult;

			dns_rdata_reset(&rdata);
			dns_rdataset_current(rdataset, &rdata);

			iresult = dns_rdata_tostruct(&rdata, &keydata, NULL);
			/* Do we have a valid placeholder KEYDATA record? */
			if (iresult == ISC_R_SUCCESS && keydata.flags == 0 &&
			    keydata.protocol == 0 && keydata.algorithm == 0)
			{
				set_refreshkeytimer(zone, &keydata, now, true);
				load = false;
			}
		}

		/*
		 * Release db wrlock to prevent LOR reports against
		 * dns_keytable_forall() call below.
		 */
		dns_rriterator_pause(&rrit);
		result = dns_keytable_find(sr, rrname, &keynode);
		if (result != ISC_R_SUCCESS || !dns_keynode_managed(keynode)) {
			CHECK(delete_keydata(db, ver, &diff, rrname, rdataset));
			changed = true;
		} else if (load) {
			load_secroots(zone, rrname, rdataset);
		}

		if (keynode != NULL) {
			dns_keynode_detach(&keynode);
		}
	}
	dns_rriterator_destroy(&rrit);

	/*
	 * Walk secroots to find any initial keys that aren't in
	 * the zone.  If we find any, add them to the zone directly.
	 * If any DS-style initial keys are found, refresh the key
	 * zone so that they'll be looked up.
	 */
	arg.db = db;
	arg.ver = ver;
	arg.result = ISC_R_SUCCESS;
	arg.diff = &diff;
	arg.zone = zone;
	arg.changed = &changed;
	dns_keytable_forall(sr, addifmissing, &arg);
	result = arg.result;
	if (changed) {
		/* Write changes to journal file. */
		CHECK(update_soa_serial(zone, db, ver, &diff, zone->mctx,
					zone->updatemethod));
		CHECK(zone_journal(zone, &diff, NULL, "sync_keyzone"));

		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_LOADED);
		zone_needdump(zone, 30);
		commit = true;
	}

failure:
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR,
			   "unable to synchronize managed keys: %s",
			   isc_result_totext(result));
		isc_time_settoepoch(&zone->refreshkeytime);
	}
	if (keynode != NULL) {
		dns_keynode_detach(&keynode);
	}
	if (sr != NULL) {
		dns_keytable_detach(&sr);
	}
	if (ver != NULL) {
		dns_db_closeversion(db, &ver, commit);
	}
	dns_diff_clear(&diff);

	INSIST(ver == NULL);

	return result;
}

isc_result_t
dns_zone_synckeyzone(dns_zone_t *zone) {
	isc_result_t result;
	dns_db_t *db = NULL;

	if (zone->type != dns_zone_key) {
		return DNS_R_BADZONE;
	}

	CHECK(dns_zone_getdb(zone, &db));

	LOCK_ZONE(zone);
	result = sync_keyzone(zone, db);
	UNLOCK_ZONE(zone);

failure:
	if (db != NULL) {
		dns_db_detach(&db);
	}
	return result;
}

static void
maybe_send_secure(dns_zone_t *zone) {
	isc_result_t result;

	/*
	 * We've finished loading, or else failed to load, an inline-signing
	 * 'secure' zone.  We now need information about the status of the
	 * 'raw' zone.  If we failed to load, then we need it to send a
	 * copy of its database; if we succeeded, we need it to send its
	 * serial number so that we can sync with it.  If it has not yet
	 * loaded, we set a flag so that it will send the necessary
	 * information when it has finished loading.
	 */
	if (zone->raw->db != NULL) {
		if (zone->db != NULL) {
			uint32_t serial;
			unsigned int soacount;

			result = zone_get_from_db(
				zone->raw, zone->raw->db, NULL, &soacount, NULL,
				&serial, NULL, NULL, NULL, NULL, NULL);
			if (result == ISC_R_SUCCESS && soacount > 0U) {
				zone_send_secureserial(zone->raw, serial);
			}
		} else {
			zone_send_securedb(zone->raw, zone->raw->db);
		}
	} else {
		DNS_ZONE_SETFLAG(zone->raw, DNS_ZONEFLG_SENDSECURE);
	}
}

static bool
zone_unchanged(dns_db_t *db1, dns_db_t *db2, isc_mem_t *mctx) {
	isc_result_t result;
	bool answer = false;
	dns_diff_t diff;

	dns_diff_init(mctx, &diff);
	result = dns_db_diffx(&diff, db1, NULL, db2, NULL, NULL);
	if (result == ISC_R_SUCCESS && ISC_LIST_EMPTY(diff.tuples)) {
		answer = true;
	}
	dns_diff_clear(&diff);
	return answer;
}

static void
process_zone_setnsec3param(dns_zone_t *zone) {
	struct np3 *npe = NULL;
	while ((npe = ISC_LIST_HEAD(zone->setnsec3param_queue)) != NULL) {
		ISC_LIST_UNLINK(zone->setnsec3param_queue, npe, link);
		zone_iattach(zone, &npe->zone);
		isc_async_run(zone->loop, setnsec3param, npe);
	}
}

/*
 * The zone is presumed to be locked.
 * If this is a inline_raw zone the secure version is also locked.
 */
static isc_result_t
zone_postload(dns_zone_t *zone, dns_db_t *db, isc_time_t loadtime,
	      isc_result_t result) {
	unsigned int soacount = 0;
	unsigned int nscount = 0;
	unsigned int errors = 0;
	uint32_t serial, oldserial, refresh, retry, expire, minimum, soattl;
	isc_time_t now;
	bool needdump = false;
	bool fixjournal = false;
	bool hasinclude = DNS_ZONE_FLAG(zone, DNS_ZONEFLG_HASINCLUDE);
	bool noprimary = false;
	bool had_db = false;
	dns_include_t *inc;
	bool is_dynamic = false;

	INSIST(LOCKED_ZONE(zone));
	if (inline_raw(zone)) {
		INSIST(LOCKED_ZONE(zone->secure));
	}

	now = isc_time_now();

	/*
	 * Initiate zone transfer?  We may need a error code that
	 * indicates that the "permanent" form does not exist.
	 * XXX better error feedback to log.
	 */
	if (result != ISC_R_SUCCESS && result != DNS_R_SEENINCLUDE) {
		if (zone->type == dns_zone_secondary ||
		    zone->type == dns_zone_mirror ||
		    zone->type == dns_zone_stub ||
		    (zone->type == dns_zone_redirect &&
		     dns_remote_addresses(&zone->primaries) == NULL))
		{
			if (result == ISC_R_FILENOTFOUND) {
				dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD,
					      ISC_LOG_DEBUG(1),
					      "no master file");
			} else if (result != DNS_R_NOMASTERFILE) {
				dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD,
					      ISC_LOG_ERROR,
					      "loading from master file %s "
					      "failed: %s",
					      zone->masterfile,
					      isc_result_totext(result));
			}
		} else if (zone->type == dns_zone_primary &&
			   inline_secure(zone) && result == ISC_R_FILENOTFOUND)
		{
			dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD,
				      ISC_LOG_DEBUG(1),
				      "no master file, requesting db");
			maybe_send_secure(zone);
		} else {
			int level = ISC_LOG_ERROR;
			if (zone->type == dns_zone_key &&
			    result == ISC_R_FILENOTFOUND)
			{
				level = ISC_LOG_DEBUG(1);
			}
			dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD, level,
				      "loading from master file %s failed: %s",
				      zone->masterfile,
				      isc_result_totext(result));
			noprimary = true;
		}

		if (zone->type != dns_zone_key) {
			goto cleanup;
		}
	}

	dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD, ISC_LOG_DEBUG(2),
		      "number of nodes in database: %u",
		      dns_db_nodecount(db, dns_dbtree_main));

	if (result == DNS_R_SEENINCLUDE) {
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_HASINCLUDE);
	} else {
		DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_HASINCLUDE);
	}

	/*
	 * If there's no master file for a key zone, then the zone is new:
	 * create an SOA record.  (We do this now, instead of later, so that
	 * if there happens to be a journal file, we can roll forward from
	 * a sane starting point.)
	 */
	if (noprimary && zone->type == dns_zone_key) {
		result = add_soa(zone, db);
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}
	}

	/*
	 * Apply update log, if any, on initial load.
	 */
	if (zone->journal != NULL &&
	    !DNS_ZONE_OPTION(zone, DNS_ZONEOPT_NOMERGE) &&
	    !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADED))
	{
		result = zone_journal_rollforward(zone, db, &needdump,
						  &fixjournal);
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}
	}

	/*
	 * Obtain ns, soa and cname counts for top of zone.
	 */
	INSIST(db != NULL);
	result = zone_get_from_db(zone, db, &nscount, &soacount, &soattl,
				  &serial, &refresh, &retry, &expire, &minimum,
				  &errors);
	if (result != ISC_R_SUCCESS && zone->type != dns_zone_key) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD, ISC_LOG_ERROR,
			      "could not find NS and/or SOA records");
	}

	/*
	 * Process any queued NSEC3PARAM change requests. Only for dynamic
	 * zones, an inline-signing zone will perform this action when
	 * receiving the secure db (receive_secure_db).
	 */
	is_dynamic = dns_zone_isdynamic(zone, true);
	if (is_dynamic) {
		process_zone_setnsec3param(zone);
	}

	/*
	 * Check to make sure the journal is up to date, and remove the
	 * journal file if it isn't, as we wouldn't be able to apply
	 * updates otherwise.
	 */
	if (zone->journal != NULL && is_dynamic &&
	    !DNS_ZONE_OPTION(zone, DNS_ZONEOPT_IXFRFROMDIFFS))
	{
		uint32_t jserial;
		dns_journal_t *journal = NULL;
		bool empty = false;

		result = dns_journal_open(zone->mctx, zone->journal,
					  DNS_JOURNAL_READ, &journal);
		if (result == ISC_R_SUCCESS) {
			jserial = dns_journal_last_serial(journal);
			empty = dns_journal_empty(journal);
			dns_journal_destroy(&journal);
		} else {
			jserial = serial;
			result = ISC_R_SUCCESS;
		}

		if (jserial != serial) {
			if (!empty) {
				dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD,
					      ISC_LOG_INFO,
					      "journal file is out of date: "
					      "removing journal file");
			}
			if (remove(zone->journal) < 0 && errno != ENOENT) {
				char strbuf[ISC_STRERRORSIZE];
				strerror_r(errno, strbuf, sizeof(strbuf));
				isc_log_write(dns_lctx, DNS_LOGCATEGORY_GENERAL,
					      DNS_LOGMODULE_ZONE,
					      ISC_LOG_WARNING,
					      "unable to remove journal "
					      "'%s': '%s'",
					      zone->journal, strbuf);
			}
		}
	}

	dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD, ISC_LOG_DEBUG(1),
		      "loaded; checking validity");

	/*
	 * Primary / Secondary / Mirror / Stub zones require both NS and SOA
	 * records at the top of the zone.
	 */

	switch (zone->type) {
	case dns_zone_dlz:
	case dns_zone_primary:
	case dns_zone_secondary:
	case dns_zone_mirror:
	case dns_zone_stub:
	case dns_zone_redirect:
		if (soacount != 1) {
			dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD,
				      ISC_LOG_ERROR, "has %d SOA records",
				      soacount);
			result = DNS_R_BADZONE;
		}
		if (nscount == 0) {
			dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD,
				      ISC_LOG_ERROR, "has no NS records");
			result = DNS_R_BADZONE;
		}
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}
		if (zone->type == dns_zone_primary && errors != 0) {
			result = DNS_R_BADZONE;
			goto cleanup;
		}
		if (zone->type != dns_zone_stub &&
		    zone->type != dns_zone_redirect)
		{
			result = check_nsec3param(zone, db);
			if (result != ISC_R_SUCCESS) {
				goto cleanup;
			}
		}
		if (zone->type == dns_zone_primary &&
		    DNS_ZONE_OPTION(zone, DNS_ZONEOPT_CHECKINTEGRITY) &&
		    !integrity_checks(zone, db))
		{
			result = DNS_R_BADZONE;
			goto cleanup;
		}
		if (zone->type == dns_zone_primary &&
		    DNS_ZONE_OPTION(zone, DNS_ZONEOPT_CHECKDUPRR) &&
		    !zone_check_dup(zone, db))
		{
			result = DNS_R_BADZONE;
			goto cleanup;
		}

		if (zone->type == dns_zone_primary) {
			result = dns_zone_cdscheck(zone, db, NULL);
			if (result != ISC_R_SUCCESS) {
				dns_zone_log(zone, ISC_LOG_ERROR,
					     "CDS/CDNSKEY consistency checks "
					     "failed");
				goto cleanup;
			}
		}

		result = dns_zone_verifydb(zone, db, NULL);
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}

		if (zone->db != NULL) {
			unsigned int oldsoacount;

			/*
			 * This is checked in zone_replacedb() for
			 * secondary zones as they don't reload from disk.
			 */
			result = zone_get_from_db(
				zone, zone->db, NULL, &oldsoacount, NULL,
				&oldserial, NULL, NULL, NULL, NULL, NULL);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);
			RUNTIME_CHECK(oldsoacount > 0U);
			if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_IXFRFROMDIFFS) &&
			    !isc_serial_gt(serial, oldserial))
			{
				uint32_t serialmin, serialmax;

				INSIST(zone->type == dns_zone_primary);
				INSIST(zone->raw == NULL);

				if (serial == oldserial &&
				    zone_unchanged(zone->db, db, zone->mctx))
				{
					dns_zone_logc(zone,
						      DNS_LOGCATEGORY_ZONELOAD,
						      ISC_LOG_INFO,
						      "ixfr-from-differences: "
						      "unchanged");
					zone->loadtime = loadtime;
					goto done;
				}

				serialmin = (oldserial + 1) & 0xffffffffU;
				serialmax = (oldserial + 0x7fffffffU) &
					    0xffffffffU;
				dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD,
					      ISC_LOG_ERROR,
					      "ixfr-from-differences: "
					      "new serial (%u) out of range "
					      "[%u - %u]",
					      serial, serialmin, serialmax);
				result = DNS_R_BADZONE;
				goto cleanup;
			} else if (!isc_serial_ge(serial, oldserial)) {
				dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD,
					      ISC_LOG_ERROR,
					      "zone serial (%u/%u) has gone "
					      "backwards",
					      serial, oldserial);
			} else if (serial == oldserial && !hasinclude &&
				   strcmp(zone->db_argv[0], "_builtin") != 0)
			{
				dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD,
					      ISC_LOG_ERROR,
					      "zone serial (%u) unchanged. "
					      "zone may fail to transfer "
					      "to secondaries.",
					      serial);
			}
		}

		if (zone->type == dns_zone_primary &&
		    (zone->update_acl != NULL || zone->ssutable != NULL) &&
		    dns_zone_getsigresigninginterval(zone) < (3 * refresh) &&
		    dns_db_issecure(db))
		{
			dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD,
				      ISC_LOG_WARNING,
				      "sig-re-signing-interval less than "
				      "3 * refresh.");
		}

		zone->refresh = RANGE(refresh, zone->minrefresh,
				      zone->maxrefresh);
		zone->retry = RANGE(retry, zone->minretry, zone->maxretry);
		zone->expire = RANGE(expire, zone->refresh + zone->retry,
				     DNS_MAX_EXPIRE);
		zone->soattl = soattl;
		zone->minimum = minimum;
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_HAVETIMERS);

		if (zone->type == dns_zone_secondary ||
		    zone->type == dns_zone_mirror ||
		    zone->type == dns_zone_stub ||
		    (zone->type == dns_zone_redirect &&
		     dns_remote_addresses(&zone->primaries) != NULL))
		{
			isc_time_t t;
			uint32_t delay;

			result = isc_file_getmodtime(zone->journal, &t);
			if (result != ISC_R_SUCCESS) {
				result = isc_file_getmodtime(zone->masterfile,
							     &t);
			}
			if (result == ISC_R_SUCCESS) {
				DNS_ZONE_TIME_ADD(&t, zone->expire,
						  &zone->expiretime);
			} else {
				DNS_ZONE_TIME_ADD(&now, zone->retry,
						  &zone->expiretime);
			}

			delay = (zone->retry -
				 isc_random_uniform((zone->retry * 3) / 4));
			DNS_ZONE_TIME_ADD(&now, delay, &zone->refreshtime);
			if (isc_time_compare(&zone->refreshtime,
					     &zone->expiretime) >= 0)
			{
				DNS_ZONE_SETFLAG(zone,
						 DNS_ZONEFLG_FIRSTREFRESH);
				zone->refreshtime = now;
			} else {
				/* The zone is up to date. */
				DNS_ZONE_CLRFLAG(zone,
						 DNS_ZONEFLG_FIRSTREFRESH);
			}
		}

		break;

	case dns_zone_key:
		/* Nothing needs to be done now */
		break;

	default:
		UNEXPECTED_ERROR("unexpected zone type %d", zone->type);
		result = ISC_R_UNEXPECTED;
		goto cleanup;
	}

	/*
	 * Check for weak DNSKEY's.
	 */
	if (zone->type == dns_zone_primary) {
		zone_check_dnskeys(zone, db);
	}

	/*
	 * Schedule DNSSEC key refresh.
	 */
	if (zone->type == dns_zone_primary &&
	    DNS_ZONEKEY_OPTION(zone, DNS_ZONEKEY_MAINTAIN))
	{
		zone->refreshkeytime = now;
	}

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_write);
	if (zone->db != NULL) {
		had_db = true;
		result = zone_replacedb(zone, db, false);
		ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_write);
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}
	} else {
		zone_attachdb(zone, db);
		ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_write);
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_LOADED |
					       DNS_ZONEFLG_NEEDSTARTUPNOTIFY);
		if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_SENDSECURE) &&
		    inline_raw(zone))
		{
			if (zone->secure->db == NULL) {
				zone_send_securedb(zone, db);
			} else {
				zone_send_secureserial(zone, serial);
			}
		}
	}

	/*
	 * Finished loading inline-signing zone; need to get status
	 * from the raw side now.
	 */
	if (zone->type == dns_zone_primary && inline_secure(zone)) {
		maybe_send_secure(zone);
	}

	result = ISC_R_SUCCESS;

	if (fixjournal) {
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_FIXJOURNAL);
		zone_journal_compact(zone, zone->db, 0);
	}
	if (needdump) {
		if (zone->type == dns_zone_key) {
			zone_needdump(zone, 30);
		} else {
			zone_needdump(zone, DNS_DUMP_DELAY);
		}
	}

	if (zone->loop != NULL) {
		if (zone->type == dns_zone_primary) {
			set_resigntime(zone);
			resume_signingwithkey(zone);
			resume_addnsec3chain(zone);
		}

		is_dynamic = dns_zone_isdynamic(zone, false);
		if (zone->type == dns_zone_primary && is_dynamic &&
		    dns_db_issecure(db) && !inline_raw(zone))
		{
			isc_stdtime_t resign;
			dns_name_t *name;
			dns_fixedname_t fixed;
			dns_typepair_t typepair;

			name = dns_fixedname_initname(&fixed);

			result = dns_db_getsigningtime(db, &resign, name,
						       &typepair);
			if (result == ISC_R_SUCCESS) {
				isc_stdtime_t timenow = isc_stdtime_now();
				char namebuf[DNS_NAME_FORMATSIZE];
				char typebuf[DNS_RDATATYPE_FORMATSIZE];

				dns_name_format(name, namebuf, sizeof(namebuf));
				dns_rdatatype_format(
					DNS_TYPEPAIR_COVERS(typepair), typebuf,
					sizeof(typebuf));
				dnssec_log(
					zone, ISC_LOG_DEBUG(3),
					"next resign: %s/%s "
					"in %d seconds",
					namebuf, typebuf,
					resign - timenow -
						dns_zone_getsigresigninginterval(
							zone));
			} else {
				dnssec_log(zone, ISC_LOG_WARNING,
					   "signed dynamic zone has no "
					   "resign event scheduled");
			}
		}

		zone_settimer(zone, &now);
	}

	/*
	 * Clear old include list.
	 */
	for (inc = ISC_LIST_HEAD(zone->includes); inc != NULL;
	     inc = ISC_LIST_HEAD(zone->includes))
	{
		ISC_LIST_UNLINK(zone->includes, inc, link);
		isc_mem_free(zone->mctx, inc->name);
		isc_mem_put(zone->mctx, inc, sizeof(*inc));
	}
	zone->nincludes = 0;

	/*
	 * Transfer new include list.
	 */
	for (inc = ISC_LIST_HEAD(zone->newincludes); inc != NULL;
	     inc = ISC_LIST_HEAD(zone->newincludes))
	{
		ISC_LIST_UNLINK(zone->newincludes, inc, link);
		ISC_LIST_APPEND(zone->includes, inc, link);
		zone->nincludes++;
	}

	if (!dns_db_ispersistent(db)) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD, ISC_LOG_INFO,
			      "loaded serial %u%s", serial,
			      dns_db_issecure(db) ? " (DNSSEC signed)" : "");
	}

	if (!had_db && zone->type == dns_zone_mirror) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD, ISC_LOG_INFO,
			      "mirror zone is now in use");
	}

	zone->loadtime = loadtime;
	goto done;

cleanup:
	if (result != ISC_R_SUCCESS) {
		dns_zone_rpz_disable_db(zone, db);
		dns_zone_catz_disable_db(zone, db);
	}

	for (inc = ISC_LIST_HEAD(zone->newincludes); inc != NULL;
	     inc = ISC_LIST_HEAD(zone->newincludes))
	{
		ISC_LIST_UNLINK(zone->newincludes, inc, link);
		isc_mem_free(zone->mctx, inc->name);
		isc_mem_put(zone->mctx, inc, sizeof(*inc));
	}
	if (zone->type == dns_zone_secondary || zone->type == dns_zone_mirror ||
	    zone->type == dns_zone_stub || zone->type == dns_zone_key ||
	    (zone->type == dns_zone_redirect &&
	     dns_remote_addresses(&zone->primaries) != NULL))
	{
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_FIRSTREFRESH);

		if (result != ISC_R_NOMEMORY) {
			if (zone->journal != NULL) {
				zone_saveunique(zone, zone->journal,
						"jn-XXXXXXXX");
			}
			if (zone->masterfile != NULL) {
				zone_saveunique(zone, zone->masterfile,
						"db-XXXXXXXX");
			}
		}

		/* Mark the zone for immediate refresh. */
		zone->refreshtime = now;
		if (zone->loop != NULL) {
			zone_settimer(zone, &now);
		}
		result = ISC_R_SUCCESS;
	} else if (zone->type == dns_zone_primary ||
		   zone->type == dns_zone_redirect)
	{
		if (!(inline_secure(zone) && result == ISC_R_FILENOTFOUND)) {
			dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD,
				      ISC_LOG_ERROR,
				      "not loaded due to errors.");
		} else if (zone->type == dns_zone_primary) {
			result = ISC_R_SUCCESS;
		}
	}

done:
	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_LOADPENDING);
	/*
	 * If this is an inline-signed zone and we were called for the raw
	 * zone, we need to clear DNS_ZONEFLG_LOADPENDING for the secure zone
	 * as well, but only if this is a reload, not an initial zone load: in
	 * the former case, zone_postload() will not be run for the secure
	 * zone; in the latter case, it will be.  Check which case we are
	 * dealing with by consulting the DNS_ZONEFLG_LOADED flag for the
	 * secure zone: if it is set, this must be a reload.
	 */
	if (inline_raw(zone) && DNS_ZONE_FLAG(zone->secure, DNS_ZONEFLG_LOADED))
	{
		DNS_ZONE_CLRFLAG(zone->secure, DNS_ZONEFLG_LOADPENDING);
		/*
		 * Re-start zone maintenance if it had been stalled
		 * due to DNS_ZONEFLG_LOADPENDING being set when
		 * zone_maintenance was called.
		 */
		if (zone->secure->loop != NULL) {
			zone_settimer(zone->secure, &now);
		}
	}

	zone_debuglog(zone, __func__, 99, "done");

	return result;
}

static bool
exit_check(dns_zone_t *zone) {
	REQUIRE(LOCKED_ZONE(zone));

	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_SHUTDOWN) &&
	    isc_refcount_current(&zone->irefs) == 0)
	{
		/*
		 * DNS_ZONEFLG_SHUTDOWN can only be set if references == 0.
		 */
		INSIST(isc_refcount_current(&zone->references) == 0);
		return true;
	}
	return false;
}

static bool
zone_check_ns(dns_zone_t *zone, dns_db_t *db, dns_dbversion_t *version,
	      dns_name_t *name, bool logit) {
	isc_result_t result;
	char namebuf[DNS_NAME_FORMATSIZE];
	char altbuf[DNS_NAME_FORMATSIZE];
	dns_fixedname_t fixed;
	dns_name_t *foundname;
	int level;

	if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_NOCHECKNS)) {
		return true;
	}

	if (zone->type == dns_zone_primary) {
		level = ISC_LOG_ERROR;
	} else {
		level = ISC_LOG_WARNING;
	}

	foundname = dns_fixedname_initname(&fixed);

	result = dns_db_find(db, name, version, dns_rdatatype_a, 0, 0, NULL,
			     foundname, NULL, NULL);
	if (result == ISC_R_SUCCESS) {
		return true;
	}

	if (result == DNS_R_NXRRSET) {
		result = dns_db_find(db, name, version, dns_rdatatype_aaaa, 0,
				     0, NULL, foundname, NULL, NULL);
		if (result == ISC_R_SUCCESS) {
			return true;
		}
	}

	if (result == DNS_R_NXRRSET || result == DNS_R_NXDOMAIN ||
	    result == DNS_R_EMPTYNAME)
	{
		if (logit) {
			dns_name_format(name, namebuf, sizeof namebuf);
			dns_zone_log(zone, level,
				     "NS '%s' has no address "
				     "records (A or AAAA)",
				     namebuf);
		}
		return false;
	}

	if (result == DNS_R_CNAME) {
		if (logit) {
			dns_name_format(name, namebuf, sizeof namebuf);
			dns_zone_log(zone, level,
				     "NS '%s' is a CNAME "
				     "(illegal)",
				     namebuf);
		}
		return false;
	}

	if (result == DNS_R_DNAME) {
		if (logit) {
			dns_name_format(name, namebuf, sizeof namebuf);
			dns_name_format(foundname, altbuf, sizeof altbuf);
			dns_zone_log(zone, level,
				     "NS '%s' is below a DNAME "
				     "'%s' (illegal)",
				     namebuf, altbuf);
		}
		return false;
	}

	return true;
}

static isc_result_t
zone_count_ns_rr(dns_zone_t *zone, dns_db_t *db, dns_dbnode_t *node,
		 dns_dbversion_t *version, unsigned int *nscount,
		 unsigned int *errors, bool logit) {
	isc_result_t result;
	unsigned int count = 0;
	unsigned int ecount = 0;
	dns_rdataset_t rdataset;
	dns_rdata_t rdata;
	dns_rdata_ns_t ns;

	dns_rdataset_init(&rdataset);
	result = dns_db_findrdataset(db, node, version, dns_rdatatype_ns,
				     dns_rdatatype_none, 0, &rdataset, NULL);
	if (result == ISC_R_NOTFOUND) {
		INSIST(!dns_rdataset_isassociated(&rdataset));
		goto success;
	}
	if (result != ISC_R_SUCCESS) {
		INSIST(!dns_rdataset_isassociated(&rdataset));
		goto invalidate_rdataset;
	}

	result = dns_rdataset_first(&rdataset);
	while (result == ISC_R_SUCCESS) {
		if (errors != NULL && zone->rdclass == dns_rdataclass_in &&
		    (zone->type == dns_zone_primary ||
		     zone->type == dns_zone_secondary ||
		     zone->type == dns_zone_mirror))
		{
			dns_rdata_init(&rdata);
			dns_rdataset_current(&rdataset, &rdata);
			result = dns_rdata_tostruct(&rdata, &ns, NULL);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);
			if (dns_name_issubdomain(&ns.name, &zone->origin) &&
			    !zone_check_ns(zone, db, version, &ns.name, logit))
			{
				ecount++;
			}
		}
		count++;
		result = dns_rdataset_next(&rdataset);
	}
	dns_rdataset_disassociate(&rdataset);

success:
	SET_IF_NOT_NULL(nscount, count);
	SET_IF_NOT_NULL(errors, ecount);

	result = ISC_R_SUCCESS;

invalidate_rdataset:
	dns_rdataset_invalidate(&rdataset);

	return result;
}

#define SET_SOA_VALUES(soattl_v, serial_v, refresh_v, retry_v, expire_v, \
		       minimum_v)                                        \
	{                                                                \
		SET_IF_NOT_NULL(soattl, soattl_v);                       \
		SET_IF_NOT_NULL(serial, serial_v);                       \
		SET_IF_NOT_NULL(refresh, refresh_v);                     \
		SET_IF_NOT_NULL(retry, retry_v);                         \
		SET_IF_NOT_NULL(expire, expire_v);                       \
		SET_IF_NOT_NULL(minimum, minimum_v);                     \
	}

#define CLR_SOA_VALUES()                          \
	{                                         \
		SET_SOA_VALUES(0, 0, 0, 0, 0, 0); \
	}

static isc_result_t
zone_load_soa_rr(dns_db_t *db, dns_dbnode_t *node, dns_dbversion_t *version,
		 unsigned int *soacount, uint32_t *soattl, uint32_t *serial,
		 uint32_t *refresh, uint32_t *retry, uint32_t *expire,
		 uint32_t *minimum) {
	isc_result_t result;
	unsigned int count = 0;
	dns_rdataset_t rdataset;
	dns_rdata_t rdata = DNS_RDATA_INIT;

	dns_rdataset_init(&rdataset);
	result = dns_db_findrdataset(db, node, version, dns_rdatatype_soa,
				     dns_rdatatype_none, 0, &rdataset, NULL);
	if (result == ISC_R_NOTFOUND) {
		INSIST(!dns_rdataset_isassociated(&rdataset));
		result = ISC_R_SUCCESS;
		goto invalidate_rdataset;
	}
	if (result != ISC_R_SUCCESS) {
		INSIST(!dns_rdataset_isassociated(&rdataset));
		goto invalidate_rdataset;
	}

	result = dns_rdataset_first(&rdataset);
	while (result == ISC_R_SUCCESS) {
		dns_rdata_init(&rdata);
		dns_rdataset_current(&rdataset, &rdata);
		count++;
		if (count == 1) {
			dns_rdata_soa_t soa;
			result = dns_rdata_tostruct(&rdata, &soa, NULL);
			SET_SOA_VALUES(rdataset.ttl, soa.serial, soa.refresh,
				       soa.retry, soa.expire, soa.minimum);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);
		}

		result = dns_rdataset_next(&rdataset);
		dns_rdata_reset(&rdata);
	}
	dns_rdataset_disassociate(&rdataset);

	result = ISC_R_SUCCESS;

invalidate_rdataset:
	SET_IF_NOT_NULL(soacount, count);
	if (count == 0) {
		CLR_SOA_VALUES();
	}

	dns_rdataset_invalidate(&rdataset);

	return result;
}

/*
 * zone must be locked.
 */
static isc_result_t
zone_get_from_db(dns_zone_t *zone, dns_db_t *db, unsigned int *nscount,
		 unsigned int *soacount, uint32_t *soattl, uint32_t *serial,
		 uint32_t *refresh, uint32_t *retry, uint32_t *expire,
		 uint32_t *minimum, unsigned int *errors) {
	isc_result_t result;
	isc_result_t answer = ISC_R_SUCCESS;
	dns_dbversion_t *version = NULL;
	dns_dbnode_t *node;

	REQUIRE(db != NULL);
	REQUIRE(zone != NULL);

	dns_db_currentversion(db, &version);

	SET_IF_NOT_NULL(nscount, 0);
	SET_IF_NOT_NULL(soacount, 0);
	SET_IF_NOT_NULL(errors, 0);
	CLR_SOA_VALUES();

	node = NULL;
	result = dns_db_findnode(db, &zone->origin, false, &node);
	if (result != ISC_R_SUCCESS) {
		answer = result;
		goto closeversion;
	}

	if (nscount != NULL || errors != NULL) {
		result = zone_count_ns_rr(zone, db, node, version, nscount,
					  errors, true);
		if (result != ISC_R_SUCCESS) {
			answer = result;
		}
	}

	if (soacount != NULL || soattl != NULL || serial != NULL ||
	    refresh != NULL || retry != NULL || expire != NULL ||
	    minimum != NULL)
	{
		result = zone_load_soa_rr(db, node, version, soacount, soattl,
					  serial, refresh, retry, expire,
					  minimum);
		if (result != ISC_R_SUCCESS) {
			answer = result;
		}
	}

	dns_db_detachnode(db, &node);
closeversion:
	dns_db_closeversion(db, &version, false);

	return answer;
}

static void
zone_destroy(dns_zone_t *zone) {
	/*
	 * Stop things being restarted after we cancel them below.
	 */
	DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_EXITING);
	dns_zone_log(zone, ISC_LOG_DEBUG(1), "final reference detached");

	if (zone->loop == NULL) {
		/*
		 * This zone is unmanaged; we're probably running in
		 * named-checkzone or a unit test. There's no loop, so we
		 * need to free it immediately.
		 */
		zone_shutdown(zone);
	} else {
		/*
		 * This zone has a loop; it can clean
		 * itself up asynchronously.
		 */
		isc_async_run(zone->loop, zone_shutdown, zone);
	}
}

#if DNS_ZONE_TRACE
ISC_REFCOUNT_TRACE_IMPL(dns_zone, zone_destroy);
#else
ISC_REFCOUNT_IMPL(dns_zone, zone_destroy);
#endif

void
dns_zone_iattach(dns_zone_t *source, dns_zone_t **target) {
	REQUIRE(DNS_ZONE_VALID(source));

	LOCK_ZONE(source);
	zone_iattach(source, target);
	UNLOCK_ZONE(source);
}

static void
zone_iattach(dns_zone_t *source, dns_zone_t **target) {
	REQUIRE(DNS_ZONE_VALID(source));
	REQUIRE(LOCKED_ZONE(source));
	REQUIRE(target != NULL && *target == NULL);
	INSIST(isc_refcount_increment0(&source->irefs) +
		       isc_refcount_current(&source->references) >
	       0);
	*target = source;
}

static void
zone_idetach(dns_zone_t **zonep) {
	dns_zone_t *zone;

	/*
	 * 'zone' locked by caller.
	 */
	REQUIRE(zonep != NULL && DNS_ZONE_VALID(*zonep));
	REQUIRE(LOCKED_ZONE(*zonep));

	zone = *zonep;
	*zonep = NULL;

	INSIST(isc_refcount_decrement(&zone->irefs) - 1 +
		       isc_refcount_current(&zone->references) >
	       0);
}

void
dns_zone_idetach(dns_zone_t **zonep) {
	dns_zone_t *zone;

	REQUIRE(zonep != NULL && DNS_ZONE_VALID(*zonep));

	zone = *zonep;
	*zonep = NULL;

	if (isc_refcount_decrement(&zone->irefs) == 1) {
		bool free_needed;
		LOCK_ZONE(zone);
		free_needed = exit_check(zone);
		UNLOCK_ZONE(zone);
		if (free_needed) {
			zone_free(zone);
		}
	}
}

isc_mem_t *
dns_zone_getmctx(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->mctx;
}

dns_zonemgr_t *
dns_zone_getmgr(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->zmgr;
}

void
dns_zone_setkasp(dns_zone_t *zone, dns_kasp_t *kasp) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->kasp != NULL) {
		dns_kasp_detach(&zone->kasp);
	}
	if (kasp != NULL) {
		dns_kasp_attach(kasp, &zone->kasp);
	}
	UNLOCK_ZONE(zone);
}

void
dns_zone_setdefaultkasp(dns_zone_t *zone, dns_kasp_t *kasp) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->defaultkasp != NULL) {
		dns_kasp_detach(&zone->defaultkasp);
	}
	if (kasp != NULL) {
		dns_kasp_attach(kasp, &zone->defaultkasp);
	}
	UNLOCK_ZONE(zone);
}

dns_kasp_t *
dns_zone_getkasp(dns_zone_t *zone) {
	dns_kasp_t *kasp;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (inline_raw(zone) && zone->secure != NULL) {
		kasp = zone->secure->kasp;
	} else {
		kasp = zone->kasp;
	}
	UNLOCK_ZONE(zone);

	return kasp;
}

static void
dns_zone_setskr(dns_zone_t *zone, dns_skr_t *skr) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	zone->skrbundle = NULL;
	if (zone->skr != NULL) {
		dns_skr_detach(&zone->skr);
	}
	if (skr != NULL) {
		dns_skr_attach(skr, &zone->skr);
	}
	UNLOCK_ZONE(zone);
}

dns_skrbundle_t *
dns_zone_getskrbundle(dns_zone_t *zone) {
	dns_skrbundle_t *bundle;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (inline_raw(zone) && zone->secure != NULL) {
		bundle = zone->secure->skrbundle;
	} else {
		bundle = zone->skrbundle;
	}
	UNLOCK_ZONE(zone);

	return bundle;
}

void
dns_zone_setoption(dns_zone_t *zone, dns_zoneopt_t option, bool value) {
	REQUIRE(DNS_ZONE_VALID(zone));

	if (value) {
		DNS_ZONE_SETOPTION(zone, option);
	} else {
		DNS_ZONE_CLROPTION(zone, option);
	}
}

dns_zoneopt_t
dns_zone_getoptions(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return ISC_ZONE_GET(zone, options);
}

void
dns_zone_setkeyopt(dns_zone_t *zone, unsigned int keyopt, bool value) {
	REQUIRE(DNS_ZONE_VALID(zone));

	if (value) {
		DNS_ZONEKEY_SETOPTION(zone, keyopt);
	} else {
		DNS_ZONEKEY_CLROPTION(zone, keyopt);
	}
}

unsigned int
dns_zone_getkeyopts(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return ISC_ZONE_GET(zone, keyopts);
}

void
dns_zone_setxfrsource4(dns_zone_t *zone, const isc_sockaddr_t *xfrsource) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(xfrsource != NULL);

	LOCK_ZONE(zone);
	zone->xfrsource4 = *xfrsource;
	UNLOCK_ZONE(zone);
}

void
dns_zone_getxfrsource4(dns_zone_t *zone, isc_sockaddr_t *xfrsource) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(xfrsource != NULL);

	LOCK_ZONE(zone);
	*xfrsource = zone->xfrsource4;
	UNLOCK_ZONE(zone);
}

void
dns_zone_setxfrsource6(dns_zone_t *zone, const isc_sockaddr_t *xfrsource) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(xfrsource != NULL);

	LOCK_ZONE(zone);
	zone->xfrsource6 = *xfrsource;
	UNLOCK_ZONE(zone);
}

void
dns_zone_getxfrsource6(dns_zone_t *zone, isc_sockaddr_t *xfrsource) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(xfrsource != NULL);

	LOCK_ZONE(zone);
	*xfrsource = zone->xfrsource6;
	UNLOCK_ZONE(zone);
}

void
dns_zone_setparentalsrc4(dns_zone_t *zone, const isc_sockaddr_t *parentalsrc) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(parentalsrc != NULL);

	LOCK_ZONE(zone);
	zone->parentalsrc4 = *parentalsrc;
	UNLOCK_ZONE(zone);
}

void
dns_zone_getparentalsrc4(dns_zone_t *zone, isc_sockaddr_t *parentalsrc) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(parentalsrc != NULL);

	LOCK_ZONE(zone);
	*parentalsrc = zone->parentalsrc4;
	UNLOCK_ZONE(zone);
}

void
dns_zone_setparentalsrc6(dns_zone_t *zone, const isc_sockaddr_t *parentalsrc) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	zone->parentalsrc6 = *parentalsrc;
	UNLOCK_ZONE(zone);
}

void
dns_zone_getparentalsrc6(dns_zone_t *zone, isc_sockaddr_t *parentalsrc) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(parentalsrc != NULL);

	LOCK_ZONE(zone);
	*parentalsrc = zone->parentalsrc6;
	UNLOCK_ZONE(zone);
}

void
dns_zone_setnotifysrc4(dns_zone_t *zone, const isc_sockaddr_t *notifysrc) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(notifysrc != NULL);

	LOCK_ZONE(zone);
	zone->notifysrc4 = *notifysrc;
	UNLOCK_ZONE(zone);
}

void
dns_zone_getnotifysrc4(dns_zone_t *zone, isc_sockaddr_t *notifysrc) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(notifysrc != NULL);

	LOCK_ZONE(zone);
	*notifysrc = zone->notifysrc4;
	UNLOCK_ZONE(zone);
}

void
dns_zone_setnotifysrc6(dns_zone_t *zone, const isc_sockaddr_t *notifysrc) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(notifysrc != NULL);

	LOCK_ZONE(zone);
	zone->notifysrc6 = *notifysrc;
	UNLOCK_ZONE(zone);
}

void
dns_zone_getnotifysrc6(dns_zone_t *zone, isc_sockaddr_t *notifysrc) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(notifysrc != NULL);

	LOCK_ZONE(zone);
	*notifysrc = zone->notifysrc6;
	UNLOCK_ZONE(zone);
}

void
dns_zone_setalsonotify(dns_zone_t *zone, isc_sockaddr_t *addresses,
		       isc_sockaddr_t *sources, dns_name_t **keynames,
		       dns_name_t **tlsnames, uint32_t count) {
	dns_remote_t remote;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);

	remote.magic = DNS_REMOTE_MAGIC;
	remote.addresses = addresses;
	remote.sources = sources;
	remote.keynames = keynames;
	remote.tlsnames = tlsnames;
	remote.addrcnt = count;

	if (dns_remote_equal(&zone->notify, &remote)) {
		goto unlock;
	}

	dns_remote_clear(&zone->notify);

	/*
	 * If count == 0, don't allocate any space for servers to notify.
	 */
	if (count == 0) {
		goto unlock;
	}

	/*
	 * Now set up the notify address and key lists.
	 */
	dns_remote_init(&zone->notify, count, addresses, sources, keynames,
			tlsnames, true, zone->mctx);

unlock:
	UNLOCK_ZONE(zone);
}

static bool
has_pf(isc_sockaddr_t *addresses, size_t count, int pf) {
	for (size_t i = 0; i < count; i++) {
		if (isc_sockaddr_pf(&addresses[i]) == pf) {
			return true;
		}
	}
	return false;
}

static void
report_no_active_addresses(dns_zone_t *zone, isc_sockaddr_t *addresses,
			   size_t count, const char *what) {
	if (isc_net_probeipv4() == ISC_R_DISABLED) {
		if (!has_pf(addresses, count, AF_INET6)) {
			dns_zone_log(zone, ISC_LOG_NOTICE,
				     "IPv4 disabled and no IPv6 %s", what);
		}
	} else if (isc_net_probeipv6() == ISC_R_DISABLED) {
		if (!has_pf(addresses, count, AF_INET)) {
			dns_zone_log(zone, ISC_LOG_NOTICE,
				     "IPv6 disabled and no IPv4 %s", what);
		}
	}
}

void
dns_zone_setprimaries(dns_zone_t *zone, isc_sockaddr_t *addresses,
		      isc_sockaddr_t *sources, dns_name_t **keynames,
		      dns_name_t **tlsnames, uint32_t count) {
	dns_remote_t remote;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);

	remote.magic = DNS_REMOTE_MAGIC;
	remote.addresses = addresses;
	remote.sources = sources;
	remote.keynames = keynames;
	remote.tlsnames = tlsnames;
	remote.addrcnt = count;

	/*
	 * The refresh code assumes that 'primaries' wouldn't change under it.
	 * If it will change then kill off any current refresh in progress
	 * and update the primaries info.  If it won't change then we can just
	 * unlock and exit.
	 */
	if (!dns_remote_equal(&zone->primaries, &remote)) {
		if (zone->request != NULL) {
			dns_request_cancel(zone->request);
		}
	} else {
		goto unlock;
	}

	dns_remote_clear(&zone->primaries);

	/*
	 * If count == 0, don't allocate any space for primaries.
	 */
	if (count == 0) {
		goto unlock;
	}

	report_no_active_addresses(zone, addresses, count, "primaries");

	/*
	 * Now set up the primaries and primary key lists.
	 */
	dns_remote_init(&zone->primaries, count, addresses, sources, keynames,
			tlsnames, true, zone->mctx);

	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_NOPRIMARIES);

unlock:
	UNLOCK_ZONE(zone);
}

void
dns_zone_setparentals(dns_zone_t *zone, isc_sockaddr_t *addresses,
		      isc_sockaddr_t *sources, dns_name_t **keynames,
		      dns_name_t **tlsnames, uint32_t count) {
	dns_remote_t remote;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);

	remote.magic = DNS_REMOTE_MAGIC;
	remote.addresses = addresses;
	remote.sources = sources;
	remote.keynames = keynames;
	remote.tlsnames = tlsnames;
	remote.addrcnt = count;

	if (dns_remote_equal(&zone->parentals, &remote)) {
		goto unlock;
	}

	dns_remote_clear(&zone->parentals);

	/*
	 * If count == 0, don't allocate any space for parentals.
	 */
	if (count == 0) {
		goto unlock;
	}

	report_no_active_addresses(zone, addresses, count, "parental-agents");

	/*
	 * Now set up the parentals and parental key lists.
	 */
	dns_remote_init(&zone->parentals, count, addresses, sources, keynames,
			tlsnames, true, zone->mctx);

	dns_zone_log(zone, ISC_LOG_NOTICE, "checkds: set %u parentals", count);

unlock:
	UNLOCK_ZONE(zone);
}

isc_result_t
dns_zone_getdb(dns_zone_t *zone, dns_db_t **dpb) {
	isc_result_t result = ISC_R_SUCCESS;

	REQUIRE(DNS_ZONE_VALID(zone));

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	if (zone->db == NULL) {
		result = DNS_R_NOTLOADED;
	} else {
		dns_db_attach(zone->db, dpb);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);

	return result;
}

void
dns_zone_setdb(dns_zone_t *zone, dns_db_t *db) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(zone->type == dns_zone_staticstub);

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_write);
	REQUIRE(zone->db == NULL);
	dns_db_attach(db, &zone->db);
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_write);
}

static bool
was_dumping(dns_zone_t *zone) {
	REQUIRE(LOCKED_ZONE(zone));

	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_DUMPING)) {
		return true;
	}

	DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_DUMPING);
	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_NEEDDUMP);
	isc_time_settoepoch(&zone->dumptime);
	return false;
}

static isc_result_t
keyfromfile(dns_zone_t *zone, dst_key_t *pubkey, isc_mem_t *mctx,
	    dst_key_t **key) {
	const char *directory = zone->keydirectory;
	dns_kasp_t *kasp = zone->kasp;
	dst_key_t *foundkey = NULL;
	isc_result_t result = ISC_R_NOTFOUND;

	if (kasp == NULL || (strcmp(dns_kasp_getname(kasp), "none") == 0) ||
	    (strcmp(dns_kasp_getname(kasp), "insecure") == 0))
	{
		result = dst_key_fromfile(
			dst_key_name(pubkey), dst_key_id(pubkey),
			dst_key_alg(pubkey),
			DST_TYPE_PUBLIC | DST_TYPE_PRIVATE | DST_TYPE_STATE,
			directory, mctx, &foundkey);
	} else {
		for (dns_kasp_key_t *kkey = ISC_LIST_HEAD(dns_kasp_keys(kasp));
		     kkey != NULL; kkey = ISC_LIST_NEXT(kkey, link))
		{
			dns_keystore_t *ks = dns_kasp_key_keystore(kkey);
			directory = dns_keystore_directory(ks,
							   zone->keydirectory);

			result = dst_key_fromfile(
				dst_key_name(pubkey), dst_key_id(pubkey),
				dst_key_alg(pubkey),
				DST_TYPE_PUBLIC | DST_TYPE_PRIVATE |
					DST_TYPE_STATE,
				directory, mctx, &foundkey);
			if (result == ISC_R_SUCCESS) {
				break;
			}
		}
	}

	*key = foundkey;
	return result;
}

#define is_zone_key(key) \
	((dst_key_flags(key) & DNS_KEYFLAG_OWNERMASK) == DNS_KEYOWNER_ZONE)

static isc_result_t
findzonekeys(dns_zone_t *zone, dns_db_t *db, dns_dbversion_t *ver,
	     dns_dbnode_t *node, const dns_name_t *name, isc_stdtime_t now,
	     isc_mem_t *mctx, unsigned int maxkeys, dst_key_t **keys,
	     unsigned int *nkeys) {
	dns_rdataset_t rdataset;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	isc_result_t result;
	dst_key_t *pubkey = NULL;
	unsigned int count = 0;

	*nkeys = 0;
	memset(keys, 0, sizeof(*keys) * maxkeys);
	dns_rdataset_init(&rdataset);
	RETERR(dns_db_findrdataset(db, node, ver, dns_rdatatype_dnskey, 0, 0,
				   &rdataset, NULL));
	RETERR(dns_rdataset_first(&rdataset));
	while (result == ISC_R_SUCCESS && count < maxkeys) {
		pubkey = NULL;
		dns_rdataset_current(&rdataset, &rdata);
		RETERR(dns_dnssec_keyfromrdata(name, &rdata, mctx, &pubkey));
		dst_key_setttl(pubkey, rdataset.ttl);

		if (!is_zone_key(pubkey)) {
			goto next;
		}
		/* Corrupted .key file? */
		if (!dns_name_equal(name, dst_key_name(pubkey))) {
			goto next;
		}
		keys[count] = NULL;
		result = keyfromfile(zone, pubkey, mctx, &keys[count]);

		/*
		 * If the key was revoked and the private file
		 * doesn't exist, maybe it was revoked internally
		 * by named.  Try loading the unrevoked version.
		 */
		if (result == ISC_R_FILENOTFOUND) {
			uint32_t flags;
			flags = dst_key_flags(pubkey);
			if ((flags & DNS_KEYFLAG_REVOKE) != 0) {
				dst_key_setflags(pubkey,
						 flags & ~DNS_KEYFLAG_REVOKE);
				result = keyfromfile(zone, pubkey, mctx,
						     &keys[count]);
				if (result == ISC_R_SUCCESS &&
				    dst_key_pubcompare(pubkey, keys[count],
						       false))
				{
					dst_key_setflags(keys[count], flags);
				}
				dst_key_setflags(pubkey, flags);
			}
		}

		if (result != ISC_R_SUCCESS) {
			char filename[DNS_NAME_FORMATSIZE +
				      DNS_SECALG_FORMATSIZE +
				      sizeof("key file for //65535")];
			isc_result_t result2;
			isc_buffer_t buf;

			isc_buffer_init(&buf, filename, sizeof(filename));
			result2 = dst_key_getfilename(
				dst_key_name(pubkey), dst_key_id(pubkey),
				dst_key_alg(pubkey),
				DST_TYPE_PUBLIC | DST_TYPE_PRIVATE |
					DST_TYPE_STATE,
				NULL, mctx, &buf);
			if (result2 != ISC_R_SUCCESS) {
				char namebuf[DNS_NAME_FORMATSIZE];
				char algbuf[DNS_SECALG_FORMATSIZE];

				dns_name_format(dst_key_name(pubkey), namebuf,
						sizeof(namebuf));
				dns_secalg_format(dst_key_alg(pubkey), algbuf,
						  sizeof(algbuf));
				snprintf(filename, sizeof(filename) - 1,
					 "key file for %s/%s/%d", namebuf,
					 algbuf, dst_key_id(pubkey));
			}

			isc_log_write(dns_lctx, DNS_LOGCATEGORY_GENERAL,
				      DNS_LOGMODULE_DNSSEC, ISC_LOG_WARNING,
				      "dns_zone_findkeys: error reading %s: %s",
				      filename, isc_result_totext(result));
		}

		if (result == ISC_R_FILENOTFOUND || result == ISC_R_NOPERM) {
			keys[count] = pubkey;
			pubkey = NULL;
			count++;
			goto next;
		}

		if (result != ISC_R_SUCCESS) {
			goto failure;
		}

		/*
		 * If a key is marked inactive, skip it
		 */
		if (!dns_dnssec_keyactive(keys[count], now)) {
			dst_key_setinactive(pubkey, true);
			dst_key_free(&keys[count]);
			keys[count] = pubkey;
			pubkey = NULL;
			count++;
			goto next;
		}

		/*
		 * Whatever the key's default TTL may have
		 * been, the rdataset TTL takes priority.
		 */
		dst_key_setttl(keys[count], rdataset.ttl);
		count++;
	next:
		if (pubkey != NULL) {
			dst_key_free(&pubkey);
		}
		dns_rdata_reset(&rdata);
		result = dns_rdataset_next(&rdataset);
	}
	if (result != ISC_R_NOMORE) {
		goto failure;
	}
	if (count == 0) {
		result = ISC_R_NOTFOUND;
	} else {
		result = ISC_R_SUCCESS;
	}

failure:
	if (dns_rdataset_isassociated(&rdataset)) {
		dns_rdataset_disassociate(&rdataset);
	}
	if (pubkey != NULL) {
		dst_key_free(&pubkey);
	}
	if (result != ISC_R_SUCCESS) {
		while (count > 0) {
			dst_key_free(&keys[--count]);
		}
	}
	*nkeys = count;
	return result;
}

/*%
 * Find up to 'maxkeys' DNSSEC keys used for signing version 'ver' of database
 * 'db' for zone 'zone' in its key directory, then load these keys into 'keys'.
 * Only load the public part of a given key if it is not active at timestamp
 * 'now'.  Store the number of keys found in 'nkeys'.
 */
isc_result_t
dns_zone_findkeys(dns_zone_t *zone, dns_db_t *db, dns_dbversion_t *ver,
		  isc_stdtime_t now, isc_mem_t *mctx, unsigned int maxkeys,
		  dst_key_t **keys, unsigned int *nkeys) {
	isc_result_t result;
	dns_dbnode_t *node = NULL;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(mctx != NULL);
	REQUIRE(nkeys != NULL);
	REQUIRE(keys != NULL);

	CHECK(dns_db_findnode(db, dns_db_origin(db), false, &node));

	dns_zone_lock_keyfiles(zone);

	result = findzonekeys(zone, db, ver, node, dns_db_origin(db), now, mctx,
			      maxkeys, keys, nkeys);

	dns_zone_unlock_keyfiles(zone);

	if (result == ISC_R_NOTFOUND) {
		result = ISC_R_SUCCESS;
	}

failure:

	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}
	return result;
}

void
dns_zone_prepare_shutdown(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_EXITING);
	UNLOCK_ZONE(zone);
}

/*%
 * Find DNSSEC keys used for signing zone with dnssec-policy. Load these keys
 * into 'keys'. Requires KASP to be locked.
 */
isc_result_t
dns_zone_getdnsseckeys(dns_zone_t *zone, dns_db_t *db, dns_dbversion_t *ver,
		       isc_stdtime_t now, dns_dnsseckeylist_t *keys) {
	isc_result_t result;
	const char *dir = dns_zone_getkeydirectory(zone);
	dns_dbnode_t *node = NULL;
	dns_dnsseckey_t *key, *key_next;
	dns_dnsseckeylist_t dnskeys;
	dns_name_t *origin = dns_zone_getorigin(zone);
	dns_kasp_t *kasp = zone->kasp;
	dns_rdataset_t keyset;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(kasp != NULL);

	ISC_LIST_INIT(dnskeys);

	dns_rdataset_init(&keyset);

	CHECK(dns_db_findnode(db, origin, false, &node));

	/* Get keys from private key files. */
	dns_zone_lock_keyfiles(zone);
	result = dns_dnssec_findmatchingkeys(origin, kasp, dir, zone->keystores,
					     now, dns_zone_getmctx(zone), keys);
	dns_zone_unlock_keyfiles(zone);

	if (result != ISC_R_SUCCESS && result != ISC_R_NOTFOUND) {
		goto failure;
	}

	/* Get public keys (dnskeys). */
	dns_rdataset_init(&keyset);
	result = dns_db_findrdataset(db, node, ver, dns_rdatatype_dnskey,
				     dns_rdatatype_none, 0, &keyset, NULL);
	if (result == ISC_R_SUCCESS) {
		CHECK(dns_dnssec_keylistfromrdataset(
			origin, kasp, dir, dns_zone_getmctx(zone), &keyset,
			NULL, NULL, false, false, &dnskeys));
	} else if (result != ISC_R_NOTFOUND) {
		CHECK(result);
	}

	/* Add new 'dnskeys' to 'keys'. */
	for (dns_dnsseckey_t *k1 = ISC_LIST_HEAD(dnskeys); k1 != NULL;
	     k1 = key_next)
	{
		dns_dnsseckey_t *k2 = NULL;
		key_next = ISC_LIST_NEXT(k1, link);

		for (k2 = ISC_LIST_HEAD(*keys); k2 != NULL;
		     k2 = ISC_LIST_NEXT(k2, link))
		{
			if (dst_key_compare(k1->key, k2->key)) {
				break;
			}
		}
		/* No match found, add the new key. */
		if (k2 == NULL) {
			ISC_LIST_UNLINK(dnskeys, k1, link);
			ISC_LIST_APPEND(*keys, k1, link);
		}
	}

failure:
	if (dns_rdataset_isassociated(&keyset)) {
		dns_rdataset_disassociate(&keyset);
	}
	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}
	while (!ISC_LIST_EMPTY(dnskeys)) {
		key = ISC_LIST_HEAD(dnskeys);
		ISC_LIST_UNLINK(dnskeys, key, link);
		dns_dnsseckey_destroy(dns_zone_getmctx(zone), &key);
	}
	return result;
}

static isc_result_t
offline(dns_db_t *db, dns_dbversion_t *ver, dns__zonediff_t *zonediff,
	dns_name_t *name, dns_ttl_t ttl, dns_rdata_t *rdata) {
	isc_result_t result;

	if ((rdata->flags & DNS_RDATA_OFFLINE) != 0) {
		return ISC_R_SUCCESS;
	}
	result = update_one_rr(db, ver, zonediff->diff, DNS_DIFFOP_DELRESIGN,
			       name, ttl, rdata);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	rdata->flags |= DNS_RDATA_OFFLINE;
	result = update_one_rr(db, ver, zonediff->diff, DNS_DIFFOP_ADDRESIGN,
			       name, ttl, rdata);
	zonediff->offline = true;
	return result;
}

static void
set_key_expiry_warning(dns_zone_t *zone, isc_stdtime_t when,
		       isc_stdtime_t now) {
	unsigned int delta;
	char timebuf[80];

	LOCK_ZONE(zone);
	zone->key_expiry = when;
	if (when <= now) {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "DNSKEY RRSIG(s) have expired");
		isc_time_settoepoch(&zone->keywarntime);
	} else if (when < now + 7 * 24 * 3600) {
		isc_time_t t;
		isc_time_set(&t, when, 0);
		isc_time_formattimestamp(&t, timebuf, 80);
		dns_zone_log(zone, ISC_LOG_WARNING,
			     "DNSKEY RRSIG(s) will expire within 7 days: %s",
			     timebuf);
		delta = when - now;
		delta--;	    /* loop prevention */
		delta /= 24 * 3600; /* to whole days */
		delta *= 24 * 3600; /* to seconds */
		isc_time_set(&zone->keywarntime, when - delta, 0);
	} else {
		isc_time_set(&zone->keywarntime, when - 7 * 24 * 3600, 0);
		isc_time_formattimestamp(&zone->keywarntime, timebuf, 80);
		dns_zone_log(zone, ISC_LOG_NOTICE, "setting keywarntime to %s",
			     timebuf);
	}
	UNLOCK_ZONE(zone);
}

/*
 * Helper function to del_sigs(). We don't want to delete RRSIGs that
 * have no new key.
 */
static bool
delsig_ok(dns_rdata_rrsig_t *rrsig_ptr, dst_key_t **keys, unsigned int nkeys,
	  bool kasp, bool *warn) {
	unsigned int i = 0;
	isc_result_t ret;
	bool have_ksk = false, have_zsk = false;
	bool have_pksk = false, have_pzsk = false;

	for (i = 0; i < nkeys; i++) {
		bool ksk, zsk;

		if (have_pksk && have_ksk && have_pzsk && have_zsk) {
			break;
		}

		if (rrsig_ptr->algorithm != dst_key_alg(keys[i])) {
			continue;
		}

		ret = dst_key_getbool(keys[i], DST_BOOL_KSK, &ksk);
		if (ret != ISC_R_SUCCESS) {
			ksk = KSK(keys[i]);
		}
		ret = dst_key_getbool(keys[i], DST_BOOL_ZSK, &zsk);
		if (ret != ISC_R_SUCCESS) {
			zsk = !KSK(keys[i]);
		}

		if (ksk) {
			have_ksk = true;
			if (dst_key_isprivate(keys[i])) {
				have_pksk = true;
			}
		}
		if (zsk) {
			have_zsk = true;
			if (dst_key_isprivate(keys[i])) {
				have_pzsk = true;
			}
		}
	}

	if (have_zsk && have_ksk && !have_pzsk) {
		*warn = true;
	}

	if (have_pksk && have_pzsk) {
		return true;
	}

	/*
	 * Deleting the SOA RRSIG is always okay.
	 */
	if (rrsig_ptr->covered == dns_rdatatype_soa) {
		return true;
	}

	/*
	 * It's okay to delete a signature if there is an active key with the
	 * same algorithm to replace it, unless that violates the DNSSEC
	 * policy.
	 */
	if (have_pksk || have_pzsk) {
		if (kasp && have_pzsk) {
			return true;
		}
		return !kasp;
	}

	/*
	 * Failing that, it is *not* okay to delete a signature
	 * if the associated public key is still in the DNSKEY RRset
	 */
	for (i = 0; i < nkeys; i++) {
		if ((rrsig_ptr->algorithm == dst_key_alg(keys[i])) &&
		    (rrsig_ptr->keyid == dst_key_id(keys[i])))
		{
			return false;
		}
	}

	/*
	 * But if the key is gone, then go ahead.
	 */
	return true;
}

/*
 * Delete expired RRsigs and any RRsigs we are about to re-sign.
 * See also update.c:del_keysigs().
 */
static isc_result_t
del_sigs(dns_zone_t *zone, dns_db_t *db, dns_dbversion_t *ver, dns_name_t *name,
	 dns_rdatatype_t type, dns__zonediff_t *zonediff, dst_key_t **keys,
	 unsigned int nkeys, isc_stdtime_t now, bool incremental) {
	isc_result_t result;
	dns_dbnode_t *node = NULL;
	dns_rdataset_t rdataset;
	unsigned int i;
	dns_rdata_rrsig_t rrsig;
	dns_kasp_t *kasp = zone->kasp;
	bool found;
	bool offlineksk = false;
	int64_t timewarn = 0, timemaybe = 0;

	dns_rdataset_init(&rdataset);

	if (kasp != NULL) {
		offlineksk = dns_kasp_offlineksk(kasp);
	}

	if (type == dns_rdatatype_nsec3) {
		result = dns_db_findnsec3node(db, name, false, &node);
	} else {
		result = dns_db_findnode(db, name, false, &node);
	}
	if (result == ISC_R_NOTFOUND) {
		return ISC_R_SUCCESS;
	}
	if (result != ISC_R_SUCCESS) {
		goto failure;
	}
	result = dns_db_findrdataset(db, node, ver, dns_rdatatype_rrsig, type,
				     (isc_stdtime_t)0, &rdataset, NULL);
	dns_db_detachnode(db, &node);

	if (result == ISC_R_NOTFOUND) {
		INSIST(!dns_rdataset_isassociated(&rdataset));
		return ISC_R_SUCCESS;
	}
	if (result != ISC_R_SUCCESS) {
		INSIST(!dns_rdataset_isassociated(&rdataset));
		goto failure;
	}

	for (result = dns_rdataset_first(&rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&rdataset))
	{
		dns_rdata_t rdata = DNS_RDATA_INIT;

		dns_rdataset_current(&rdataset, &rdata);
		result = dns_rdata_tostruct(&rdata, &rrsig, NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);

		if (!dns_rdatatype_iskeymaterial(type)) {
			bool warn = false, deleted = false;
			if (delsig_ok(&rrsig, keys, nkeys, kasp != NULL, &warn))
			{
				result = update_one_rr(db, ver, zonediff->diff,
						       DNS_DIFFOP_DELRESIGN,
						       name, rdataset.ttl,
						       &rdata);
				if (result != ISC_R_SUCCESS) {
					break;
				}
				deleted = true;
			}
			if (warn && !deleted) {
				/*
				 * At this point, we've got an RRSIG,
				 * which is signed by an inactive key.
				 * An administrator needs to provide a new
				 * key/alg, but until that time, we want to
				 * keep the old RRSIG.  Marking the key as
				 * offline will prevent us spinning waiting
				 * for the private part.
				 */
				if (incremental) {
					result = offline(db, ver, zonediff,
							 name, rdataset.ttl,
							 &rdata);
					if (result != ISC_R_SUCCESS) {
						break;
					}
				}

				/*
				 * Log the key id and algorithm of
				 * the inactive key with no replacement
				 */
				if (zone->log_key_expired_timer <= now) {
					char origin[DNS_NAME_FORMATSIZE];
					char algbuf[DNS_NAME_FORMATSIZE];
					dns_name_format(&zone->origin, origin,
							sizeof(origin));
					dns_secalg_format(rrsig.algorithm,
							  algbuf,
							  sizeof(algbuf));
					dns_zone_log(zone, ISC_LOG_WARNING,
						     "Key %s/%s/%d "
						     "missing or inactive "
						     "and has no replacement: "
						     "retaining signatures.",
						     origin, algbuf,
						     rrsig.keyid);
					zone->log_key_expired_timer = now +
								      3600;
				}
			}
			continue;
		}

		/*
		 * KSK RRSIGs requires special processing.
		 */
		found = false;
		for (i = 0; i < nkeys; i++) {
			if (rrsig.algorithm == dst_key_alg(keys[i]) &&
			    rrsig.keyid == dst_key_id(keys[i]))
			{
				found = true;
				/*
				 * Mark offline DNSKEY.
				 * We want the earliest offline expire time
				 * iff there is a new offline signature.
				 */
				if (!dst_key_inactive(keys[i]) &&
				    !dst_key_isprivate(keys[i]) && !offlineksk)
				{
					int64_t timeexpire = dns_time64_from32(
						rrsig.timeexpire);
					if (timewarn != 0 &&
					    timewarn > timeexpire)
					{
						timewarn = timeexpire;
					}
					if (rdata.flags & DNS_RDATA_OFFLINE) {
						if (timemaybe == 0 ||
						    timemaybe > timeexpire)
						{
							timemaybe = timeexpire;
						}
						break;
					}
					if (timewarn == 0) {
						timewarn = timemaybe;
					}
					if (timewarn == 0 ||
					    timewarn > timeexpire)
					{
						timewarn = timeexpire;
					}
					result = offline(db, ver, zonediff,
							 name, rdataset.ttl,
							 &rdata);
					break;
				}
				result = update_one_rr(db, ver, zonediff->diff,
						       DNS_DIFFOP_DELRESIGN,
						       name, rdataset.ttl,
						       &rdata);
				break;
			}
		}

		/*
		 * If there is not a matching DNSKEY then
		 * delete the RRSIG.
		 */
		if (!found) {
			result = update_one_rr(db, ver, zonediff->diff,
					       DNS_DIFFOP_DELRESIGN, name,
					       rdataset.ttl, &rdata);
		}
		if (result != ISC_R_SUCCESS) {
			break;
		}
	}

	dns_rdataset_disassociate(&rdataset);
	if (result == ISC_R_NOMORE) {
		result = ISC_R_SUCCESS;
	}
	if (timewarn > 0) {
		isc_stdtime_t stdwarn = (isc_stdtime_t)timewarn;
		if (timewarn == stdwarn) {
			set_key_expiry_warning(zone, (isc_stdtime_t)timewarn,
					       now);
		} else {
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "key expiry warning time out of range");
		}
	}
failure:
	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}
	return result;
}

static isc_result_t
add_sigs(dns_db_t *db, dns_dbversion_t *ver, dns_name_t *name, dns_zone_t *zone,
	 dns_rdatatype_t type, dns_diff_t *diff, dst_key_t **keys,
	 unsigned int nkeys, isc_mem_t *mctx, isc_stdtime_t now,
	 isc_stdtime_t inception, isc_stdtime_t expire) {
	isc_result_t result;
	dns_dbnode_t *node = NULL;
	dns_stats_t *dnssecsignstats;
	dns_rdataset_t rdataset;
	dns_rdata_t sig_rdata = DNS_RDATA_INIT;
	unsigned char data[1024]; /* XXX */
	isc_buffer_t buffer;
	unsigned int i;
	bool use_kasp = false;
	bool offlineksk = false;

	if (zone->kasp != NULL) {
		use_kasp = true;
		offlineksk = dns_kasp_offlineksk(zone->kasp);
	}

	dns_rdataset_init(&rdataset);
	isc_buffer_init(&buffer, data, sizeof(data));

	if (type == dns_rdatatype_nsec3) {
		result = dns_db_findnsec3node(db, name, false, &node);
	} else {
		result = dns_db_findnode(db, name, false, &node);
	}
	if (result == ISC_R_NOTFOUND) {
		return ISC_R_SUCCESS;
	}
	if (result != ISC_R_SUCCESS) {
		goto failure;
	}
	result = dns_db_findrdataset(db, node, ver, type, 0, (isc_stdtime_t)0,
				     &rdataset, NULL);
	dns_db_detachnode(db, &node);
	if (result == ISC_R_NOTFOUND) {
		INSIST(!dns_rdataset_isassociated(&rdataset));
		return ISC_R_SUCCESS;
	}
	if (result != ISC_R_SUCCESS) {
		INSIST(!dns_rdataset_isassociated(&rdataset));
		goto failure;
	}

	for (i = 0; i < nkeys; i++) {
		/* Don't add signatures for offline or inactive keys */
		if (!dst_key_isprivate(keys[i]) && !offlineksk) {
			continue;
		}
		if (dst_key_inactive(keys[i]) && !offlineksk) {
			continue;
		}

		if (use_kasp) {
			/*
			 * A dnssec-policy is found. Check what RRsets this
			 * key should sign.
			 */
			isc_result_t kresult;
			isc_stdtime_t when;
			bool ksk = false;
			bool zsk = false;
			bool have_zsk = false;

			kresult = dst_key_getbool(keys[i], DST_BOOL_KSK, &ksk);
			if (kresult != ISC_R_SUCCESS) {
				if (KSK(keys[i])) {
					ksk = true;
				}
			}
			kresult = dst_key_getbool(keys[i], DST_BOOL_ZSK, &zsk);
			if (kresult != ISC_R_SUCCESS) {
				if (!KSK(keys[i])) {
					zsk = true;
				}
			}

			/*
			 * Don't consider inactive keys or offline keys.
			 */
			if (!dst_key_isprivate(keys[i]) && offlineksk && zsk) {
				continue;
			}
			if (dst_key_inactive(keys[i]) && offlineksk && zsk) {
				continue;
			}

			if (offlineksk) {
				have_zsk = true;
			} else {
				(void)dst_key_have_ksk_and_zsk(keys, nkeys, i,
							       true, ksk, zsk,
							       NULL, &have_zsk);
			}

			if (dns_rdatatype_iskeymaterial(type)) {
				/*
				 * DNSKEY RRset is signed with KSK.
				 * CDS and CDNSKEY RRsets too (RFC 7344, 4.1).
				 */
				if (!ksk) {
					continue;
				}
			} else if (!zsk) {
				/*
				 * Other RRsets are signed with ZSK.
				 */
				if (type != dns_rdatatype_soa &&
				    type != zone->privatetype)
				{
					continue;
				}
				if (have_zsk) {
					continue;
				}
			} else if (!dst_key_is_signing(keys[i], DST_BOOL_ZSK,
						       now, &when))
			{
				/*
				 * This key is not active for zone-signing.
				 */
				continue;
			}
		} else if (!REVOKE(keys[i])) {
			/*
			 * Don't consider inactive keys, however the KSK may be
			 * temporary offline, so do consider keys which private
			 * key files are unavailable.
			 */
			bool both = dst_key_have_ksk_and_zsk(
				keys, nkeys, i, false, KSK(keys[i]),
				!KSK(keys[i]), NULL, NULL);
			if (both) {
				/*
				 * CDS and CDNSKEY are signed with KSK (RFC
				 * 7344, 4.1).
				 */
				if (dns_rdatatype_iskeymaterial(type)) {
					if (!KSK(keys[i])) {
						continue;
					}
				} else if (KSK(keys[i])) {
					continue;
				}
			}
		}

		/*
		 * If this key is revoked, it may only sign the DNSKEY RRset.
		 */
		if (REVOKE(keys[i]) && type != dns_rdatatype_dnskey) {
			continue;
		}

		/* Calculate the signature, creating a RRSIG RDATA. */
		isc_buffer_clear(&buffer);

		if (offlineksk && dns_rdatatype_iskeymaterial(type)) {
			/* Look up the signature in the SKR bundle */
			dns_skrbundle_t *bundle = dns_zone_getskrbundle(zone);
			if (bundle == NULL) {
				CHECK(DNS_R_NOSKRBUNDLE);
			}
			CHECK(dns_skrbundle_getsig(bundle, keys[i], type,
						   &sig_rdata));
		} else {
			CHECK(dns_dnssec_sign(name, &rdataset, keys[i],
					      &inception, &expire, mctx,
					      &buffer, &sig_rdata));
		}

		/* Update the database and journal with the RRSIG. */
		/* XXX inefficient - will cause dataset merging */
		CHECK(update_one_rr(db, ver, diff, DNS_DIFFOP_ADDRESIGN, name,
				    rdataset.ttl, &sig_rdata));
		dns_rdata_reset(&sig_rdata);
		isc_buffer_init(&buffer, data, sizeof(data));

		/* Update DNSSEC sign statistics. */
		dnssecsignstats = dns_zone_getdnssecsignstats(zone);
		if (dnssecsignstats != NULL) {
			/* Generated a new signature. */
			dns_dnssecsignstats_increment(dnssecsignstats,
						      ID(keys[i]),
						      (uint8_t)ALG(keys[i]),
						      dns_dnssecsignstats_sign);
			/* This is a refresh. */
			dns_dnssecsignstats_increment(
				dnssecsignstats, ID(keys[i]),
				(uint8_t)ALG(keys[i]),
				dns_dnssecsignstats_refresh);
		}
	}

failure:
	if (dns_rdataset_isassociated(&rdataset)) {
		dns_rdataset_disassociate(&rdataset);
	}
	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}
	return result;
}

static void
calculate_rrsig_validity(dns_zone_t *zone, isc_stdtime_t now,
			 isc_stdtime_t *inception, isc_stdtime_t *soaexpire,
			 isc_stdtime_t *expire, isc_stdtime_t *fullexpire) {
	REQUIRE(inception != NULL);
	REQUIRE(soaexpire != NULL);
	/* expire and fullexpire are optional */

	isc_stdtime_t jitter = DEFAULT_JITTER;
	isc_stdtime_t sigvalidity = dns_zone_getsigvalidityinterval(zone);
	isc_stdtime_t shortjitter = 0, fulljitter = 0;

	if (zone->kasp != NULL) {
		jitter = dns_kasp_sigjitter(zone->kasp);
		sigvalidity = dns_kasp_sigvalidity(zone->kasp);
		INSIST(jitter <= sigvalidity);
	}

	if (jitter > sigvalidity) {
		jitter = sigvalidity;
	}

	*inception = now - 3600; /* Allow for clock skew. */
	*soaexpire = now + sigvalidity;

	/*
	 * Spread out signatures over time if they happen to be
	 * clumped.  We don't do this for each add_sigs() call as
	 * we still want some clustering to occur.  In normal operations
	 * the records should be re-signed as they fall due and they should
	 * already be spread out.  However if the server is off for a
	 * period we need to ensure that the clusters don't become
	 * synchronised by using the full jitter range.
	 */
	if (sigvalidity >= 3600U) {
		if (sigvalidity > 7200U) {
			shortjitter = isc_random_uniform(3600);
			fulljitter = isc_random_uniform(jitter);
		} else {
			shortjitter = fulljitter = isc_random_uniform(1200);
		}
	}

	SET_IF_NOT_NULL(expire, *soaexpire - shortjitter - 1);
	SET_IF_NOT_NULL(fullexpire, *soaexpire - fulljitter - 1);
}

static void
zone_resigninc(dns_zone_t *zone) {
	dns_db_t *db = NULL;
	dns_dbversion_t *version = NULL;
	dns_diff_t _sig_diff;
	dns__zonediff_t zonediff;
	dns_fixedname_t fixed;
	dns_name_t *name;
	dns_typepair_t typepair;
	dst_key_t *zone_keys[DNS_MAXZONEKEYS];
	isc_result_t result;
	isc_stdtime_t now, inception, soaexpire, expire, fullexpire, stop;
	unsigned int i;
	unsigned int nkeys = 0;
	isc_stdtime_t resign;

	ENTER;

	dns_diff_init(zone->mctx, &_sig_diff);
	zonediff_init(&zonediff, &_sig_diff);

	/*
	 * Zone is frozen. Pause for 5 minutes.
	 */
	if (zone->update_disabled) {
		result = ISC_R_FAILURE;
		goto failure;
	}

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	if (zone->db != NULL) {
		dns_db_attach(zone->db, &db);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
	if (db == NULL) {
		result = ISC_R_FAILURE;
		goto failure;
	}

	result = dns_db_newversion(db, &version);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "zone_resigninc:dns_db_newversion -> %s",
			     isc_result_totext(result));
		goto failure;
	}

	now = isc_stdtime_now();

	result = dns_zone_findkeys(zone, db, version, now, zone->mctx,
				   DNS_MAXZONEKEYS, zone_keys, &nkeys);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "zone_resigninc:dns_zone_findkeys -> %s",
			     isc_result_totext(result));
		goto failure;
	}

	calculate_rrsig_validity(zone, now, &inception, &soaexpire, &expire,
				 &fullexpire);

	stop = now + 5;

	name = dns_fixedname_initname(&fixed);
	result = dns_db_getsigningtime(db, &resign, name, &typepair);
	if (result != ISC_R_SUCCESS && result != ISC_R_NOTFOUND) {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "zone_resigninc:dns_db_getsigningtime -> %s",
			     isc_result_totext(result));
	}

	i = 0;
	while (result == ISC_R_SUCCESS) {
		dns_rdatatype_t covers = DNS_TYPEPAIR_COVERS(typepair);

		resign -= dns_zone_getsigresigninginterval(zone);

		/*
		 * Stop if we hit the SOA as that means we have walked the
		 * entire zone.  The SOA record should always be the most
		 * recent signature.
		 */
		/* XXXMPA increase number of RRsets signed pre call */
		if ((covers == dns_rdatatype_soa &&
		     dns_name_equal(name, &zone->origin)) ||
		    i++ > zone->signatures || resign > stop)
		{
			break;
		}

		result = del_sigs(zone, db, version, name, covers, &zonediff,
				  zone_keys, nkeys, now, true);
		if (result != ISC_R_SUCCESS) {
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "zone_resigninc:del_sigs -> %s",
				     isc_result_totext(result));
			break;
		}

		/*
		 * If re-signing is over 5 minutes late use 'fullexpire'
		 * to redistribute the signature over the complete
		 * re-signing window, otherwise only add a small amount
		 * of jitter.
		 */
		result = add_sigs(db, version, name, zone, covers,
				  zonediff.diff, zone_keys, nkeys, zone->mctx,
				  now, inception,
				  resign > (now - 300) ? expire : fullexpire);
		if (result != ISC_R_SUCCESS) {
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "zone_resigninc:add_sigs -> %s",
				     isc_result_totext(result));
			break;
		}
		result = dns_db_getsigningtime(db, &resign, name, &typepair);
		if (nkeys == 0 && result == ISC_R_NOTFOUND) {
			result = ISC_R_SUCCESS;
			break;
		}
		if (result != ISC_R_SUCCESS) {
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "zone_resigninc:dns_db_getsigningtime -> "
				     "%s",
				     isc_result_totext(result));
		}
	}

	if (result != ISC_R_NOMORE && result != ISC_R_SUCCESS) {
		goto failure;
	}

	result = del_sigs(zone, db, version, &zone->origin, dns_rdatatype_soa,
			  &zonediff, zone_keys, nkeys, now, true);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "zone_resigninc:del_sigs -> %s",
			     isc_result_totext(result));
		goto failure;
	}

	/*
	 * Did we change anything in the zone?
	 */
	if (ISC_LIST_EMPTY(zonediff.diff->tuples)) {
		/*
		 * Commit the changes if any key has been marked as offline.
		 */
		if (zonediff.offline) {
			dns_db_closeversion(db, &version, true);
		}
		goto failure;
	}

	/* Increment SOA serial if we have made changes */
	result = update_soa_serial(zone, db, version, zonediff.diff, zone->mctx,
				   zone->updatemethod);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "zone_resigninc:update_soa_serial -> %s",
			     isc_result_totext(result));
		goto failure;
	}

	/*
	 * Generate maximum life time signatures so that the above loop
	 * termination is sensible.
	 */
	result = add_sigs(db, version, &zone->origin, zone, dns_rdatatype_soa,
			  zonediff.diff, zone_keys, nkeys, zone->mctx, now,
			  inception, soaexpire);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "zone_resigninc:add_sigs -> %s",
			     isc_result_totext(result));
		goto failure;
	}

	/* Write changes to journal file. */
	CHECK(zone_journal(zone, zonediff.diff, NULL, "zone_resigninc"));

	/* Everything has succeeded. Commit the changes. */
	dns_db_closeversion(db, &version, true);

failure:
	dns_diff_clear(&_sig_diff);
	for (i = 0; i < nkeys; i++) {
		dst_key_free(&zone_keys[i]);
	}
	if (version != NULL) {
		dns_db_closeversion(db, &version, false);
		dns_db_detach(&db);
	} else if (db != NULL) {
		dns_db_detach(&db);
	}

	LOCK_ZONE(zone);
	if (result == ISC_R_SUCCESS) {
		set_resigntime(zone);
		zone_needdump(zone, DNS_DUMP_DELAY);
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NEEDNOTIFY);
	} else {
		/*
		 * Something failed.  Retry in 5 minutes.
		 */
		isc_interval_t ival;
		isc_interval_set(&ival, 300, 0);
		isc_time_nowplusinterval(&zone->resigntime, &ival);
	}
	UNLOCK_ZONE(zone);

	INSIST(version == NULL);
}

static isc_result_t
next_active(dns_db_t *db, dns_dbversion_t *version, dns_name_t *oldname,
	    dns_name_t *newname, bool bottom) {
	isc_result_t result;
	dns_dbiterator_t *dbit = NULL;
	dns_rdatasetiter_t *rdsit = NULL;
	dns_dbnode_t *node = NULL;

	CHECK(dns_db_createiterator(db, DNS_DB_NONSEC3, &dbit));
	CHECK(dns_dbiterator_seek(dbit, oldname));
	do {
		result = dns_dbiterator_next(dbit);
		if (result == ISC_R_NOMORE) {
			CHECK(dns_dbiterator_first(dbit));
		}
		CHECK(dns_dbiterator_current(dbit, &node, newname));
		if (bottom && dns_name_issubdomain(newname, oldname) &&
		    !dns_name_equal(newname, oldname))
		{
			dns_db_detachnode(db, &node);
			continue;
		}
		/*
		 * Is this node empty?
		 */
		CHECK(dns_db_allrdatasets(db, node, version, 0, 0, &rdsit));
		result = dns_rdatasetiter_first(rdsit);
		dns_db_detachnode(db, &node);
		dns_rdatasetiter_destroy(&rdsit);
		if (result != ISC_R_NOMORE) {
			break;
		}
	} while (1);
failure:
	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}
	if (dbit != NULL) {
		dns_dbiterator_destroy(&dbit);
	}
	return result;
}

static bool
signed_with_good_key(dns_zone_t *zone, dns_db_t *db, dns_dbnode_t *node,
		     dns_dbversion_t *version, dns_rdatatype_t type,
		     dst_key_t *key) {
	isc_result_t result;
	dns_rdataset_t rdataset;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	dns_rdata_rrsig_t rrsig;
	int count = 0;
	dns_kasp_t *kasp = zone->kasp;

	dns_rdataset_init(&rdataset);
	result = dns_db_findrdataset(db, node, version, dns_rdatatype_rrsig,
				     type, 0, &rdataset, NULL);
	if (result != ISC_R_SUCCESS) {
		INSIST(!dns_rdataset_isassociated(&rdataset));
		return false;
	}
	for (result = dns_rdataset_first(&rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&rdataset))
	{
		dns_rdataset_current(&rdataset, &rdata);
		result = dns_rdata_tostruct(&rdata, &rrsig, NULL);
		INSIST(result == ISC_R_SUCCESS);
		if (rrsig.algorithm == dst_key_alg(key) &&
		    rrsig.keyid == dst_key_id(key))
		{
			dns_rdataset_disassociate(&rdataset);
			return true;
		}
		if (rrsig.algorithm == dst_key_alg(key)) {
			count++;
		}
		dns_rdata_reset(&rdata);
	}

	if (zone->kasp != NULL) {
		dns_kasp_key_t *kkey;
		int zsk_count = 0;
		bool approved;

		KASP_LOCK(kasp);
		for (kkey = ISC_LIST_HEAD(dns_kasp_keys(kasp)); kkey != NULL;
		     kkey = ISC_LIST_NEXT(kkey, link))
		{
			if (dns_kasp_key_algorithm(kkey) != dst_key_alg(key)) {
				continue;
			}
			if (dns_kasp_key_zsk(kkey)) {
				zsk_count++;
			}
		}
		KASP_UNLOCK(kasp);

		if (dns_rdatatype_iskeymaterial(type)) {
			/*
			 * CDS and CDNSKEY are signed with KSK like DNSKEY.
			 * (RFC 7344, section 4.1 specifies that they must
			 * be signed with a key in the current DS RRset,
			 * which would only include KSK's.)
			 */
			approved = false;
		} else {
			approved = (zsk_count == count);
		}

		dns_rdataset_disassociate(&rdataset);
		return approved;
	}

	dns_rdataset_disassociate(&rdataset);
	return false;
}

static isc_result_t
add_nsec(dns_db_t *db, dns_dbversion_t *version, dns_name_t *name,
	 dns_dbnode_t *node, dns_ttl_t ttl, bool bottom, dns_diff_t *diff) {
	dns_fixedname_t fixed;
	dns_name_t *next;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	isc_result_t result;
	unsigned char nsecbuffer[DNS_NSEC_BUFFERSIZE];

	next = dns_fixedname_initname(&fixed);

	CHECK(next_active(db, version, name, next, bottom));
	CHECK(dns_nsec_buildrdata(db, version, node, next, nsecbuffer, &rdata));
	CHECK(update_one_rr(db, version, diff, DNS_DIFFOP_ADD, name, ttl,
			    &rdata));
failure:
	return result;
}

static isc_result_t
check_if_bottom_of_zone(dns_db_t *db, dns_dbnode_t *node,
			dns_dbversion_t *version, bool *is_bottom_of_zone) {
	isc_result_t result;
	dns_rdatasetiter_t *iterator = NULL;
	dns_rdataset_t rdataset;
	bool seen_soa = false, seen_ns = false, seen_dname = false;

	REQUIRE(is_bottom_of_zone != NULL);

	result = dns_db_allrdatasets(db, node, version, 0, 0, &iterator);
	if (result != ISC_R_SUCCESS) {
		if (result == ISC_R_NOTFOUND) {
			result = ISC_R_SUCCESS;
		}
		return result;
	}

	dns_rdataset_init(&rdataset);
	for (result = dns_rdatasetiter_first(iterator); result == ISC_R_SUCCESS;
	     result = dns_rdatasetiter_next(iterator))
	{
		dns_rdatasetiter_current(iterator, &rdataset);
		switch (rdataset.type) {
		case dns_rdatatype_soa:
			seen_soa = true;
			break;
		case dns_rdatatype_ns:
			seen_ns = true;
			break;
		case dns_rdatatype_dname:
			seen_dname = true;
			break;
		}
		dns_rdataset_disassociate(&rdataset);
	}
	if (result != ISC_R_NOMORE) {
		goto failure;
	}
	if ((seen_ns && !seen_soa) || seen_dname) {
		*is_bottom_of_zone = true;
	}
	result = ISC_R_SUCCESS;

failure:
	dns_rdatasetiter_destroy(&iterator);

	return result;
}

static isc_result_t
sign_a_node(dns_db_t *db, dns_zone_t *zone, dns_name_t *name,
	    dns_dbnode_t *node, dns_dbversion_t *version, bool build_nsec3,
	    bool build_nsec, dst_key_t *key, isc_stdtime_t now,
	    isc_stdtime_t inception, isc_stdtime_t expire, dns_ttl_t nsecttl,
	    bool both, bool is_ksk, bool is_zsk, bool is_bottom_of_zone,
	    dns_diff_t *diff, int32_t *signatures, isc_mem_t *mctx) {
	isc_result_t result;
	dns_rdatasetiter_t *iterator = NULL;
	dns_rdataset_t rdataset;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	dns_stats_t *dnssecsignstats;
	bool offlineksk = false;
	isc_buffer_t buffer;
	unsigned char data[1024];
	bool seen_soa, seen_ns, seen_rr, seen_nsec, seen_nsec3, seen_ds;

	if (zone->kasp != NULL) {
		offlineksk = dns_kasp_offlineksk(zone->kasp);
	}

	result = dns_db_allrdatasets(db, node, version, 0, 0, &iterator);
	if (result != ISC_R_SUCCESS) {
		if (result == ISC_R_NOTFOUND) {
			result = ISC_R_SUCCESS;
		}
		return result;
	}

	dns_rdataset_init(&rdataset);
	isc_buffer_init(&buffer, data, sizeof(data));
	seen_rr = seen_soa = seen_ns = seen_nsec = seen_nsec3 = seen_ds = false;
	for (result = dns_rdatasetiter_first(iterator); result == ISC_R_SUCCESS;
	     result = dns_rdatasetiter_next(iterator))
	{
		dns_rdatasetiter_current(iterator, &rdataset);
		if (rdataset.type == dns_rdatatype_soa) {
			seen_soa = true;
		} else if (rdataset.type == dns_rdatatype_ns) {
			seen_ns = true;
		} else if (rdataset.type == dns_rdatatype_ds) {
			seen_ds = true;
		} else if (rdataset.type == dns_rdatatype_nsec) {
			seen_nsec = true;
		} else if (rdataset.type == dns_rdatatype_nsec3) {
			seen_nsec3 = true;
		}
		if (rdataset.type != dns_rdatatype_rrsig) {
			seen_rr = true;
		}
		dns_rdataset_disassociate(&rdataset);
	}
	if (result != ISC_R_NOMORE) {
		goto failure;
	}
	/*
	 * Going from insecure to NSEC3.
	 * Don't generate NSEC3 records for NSEC3 records.
	 */
	if (build_nsec3 && !seen_nsec3 && seen_rr) {
		bool unsecure = !seen_ds && seen_ns && !seen_soa;
		CHECK(dns_nsec3_addnsec3s(db, version, name, nsecttl, unsecure,
					  diff));
		(*signatures)--;
	}
	/*
	 * Going from insecure to NSEC.
	 * Don't generate NSEC records for NSEC3 records.
	 */
	if (build_nsec && !seen_nsec3 && !seen_nsec && seen_rr) {
		/*
		 * Build a NSEC record except at the origin.
		 */
		if (!dns_name_equal(name, dns_db_origin(db))) {
			CHECK(add_nsec(db, version, name, node, nsecttl,
				       is_bottom_of_zone, diff));
			/* Count a NSEC generation as a signature generation. */
			(*signatures)--;
		}
	}
	result = dns_rdatasetiter_first(iterator);
	while (result == ISC_R_SUCCESS) {
		isc_stdtime_t when;

		dns_rdatasetiter_current(iterator, &rdataset);
		if (rdataset.type == dns_rdatatype_soa ||
		    rdataset.type == dns_rdatatype_rrsig)
		{
			goto next_rdataset;
		}
		if (dns_rdatatype_iskeymaterial(rdataset.type)) {
			/*
			 * CDS and CDNSKEY are signed with KSK like DNSKEY.
			 * (RFC 7344, section 4.1 specifies that they must
			 * be signed with a key in the current DS RRset,
			 * which would only include KSK's.)
			 */
			if (!is_ksk && both) {
				goto next_rdataset;
			}
		} else if (!is_zsk && both) {
			goto next_rdataset;
		} else if (is_zsk &&
			   !dst_key_is_signing(key, DST_BOOL_ZSK, now, &when))
		{
			/* Only applies to dnssec-policy. */
			if (zone->kasp != NULL) {
				goto next_rdataset;
			}
		}

		if (seen_ns && !seen_soa && rdataset.type != dns_rdatatype_ds &&
		    rdataset.type != dns_rdatatype_nsec)
		{
			goto next_rdataset;
		}
		if (signed_with_good_key(zone, db, node, version, rdataset.type,
					 key))
		{
			goto next_rdataset;
		}

		/* Calculate the signature, creating a RRSIG RDATA. */
		isc_buffer_clear(&buffer);
		if (offlineksk && dns_rdatatype_iskeymaterial(rdataset.type)) {
			/* Look up the signature in the SKR bundle */
			dns_skrbundle_t *bundle = dns_zone_getskrbundle(zone);
			if (bundle == NULL) {
				CHECK(DNS_R_NOSKRBUNDLE);
			}
			CHECK(dns_skrbundle_getsig(bundle, key, rdataset.type,
						   &rdata));
		} else {
			CHECK(dns_dnssec_sign(name, &rdataset, key, &inception,
					      &expire, mctx, &buffer, &rdata));
		}

		/* Update the database and journal with the RRSIG. */
		/* XXX inefficient - will cause dataset merging */
		CHECK(update_one_rr(db, version, diff, DNS_DIFFOP_ADDRESIGN,
				    name, rdataset.ttl, &rdata));
		dns_rdata_reset(&rdata);

		/* Update DNSSEC sign statistics. */
		dnssecsignstats = dns_zone_getdnssecsignstats(zone);
		if (dnssecsignstats != NULL) {
			/* Generated a new signature. */
			dns_dnssecsignstats_increment(dnssecsignstats, ID(key),
						      ALG(key),
						      dns_dnssecsignstats_sign);
			/* This is a refresh. */
			dns_dnssecsignstats_increment(
				dnssecsignstats, ID(key), ALG(key),
				dns_dnssecsignstats_refresh);
		}

		(*signatures)--;
	next_rdataset:
		dns_rdataset_disassociate(&rdataset);
		result = dns_rdatasetiter_next(iterator);
	}
	if (result == ISC_R_NOMORE) {
		result = ISC_R_SUCCESS;
	}
failure:
	if (dns_rdataset_isassociated(&rdataset)) {
		dns_rdataset_disassociate(&rdataset);
	}
	if (iterator != NULL) {
		dns_rdatasetiter_destroy(&iterator);
	}
	return result;
}

/*
 * If 'update_only' is set then don't create a NSEC RRset if it doesn't exist.
 */
static isc_result_t
updatesecure(dns_db_t *db, dns_dbversion_t *version, dns_name_t *name,
	     dns_ttl_t nsecttl, bool update_only, dns_diff_t *diff) {
	isc_result_t result;
	dns_rdataset_t rdataset;
	dns_dbnode_t *node = NULL;

	CHECK(dns_db_getoriginnode(db, &node));
	if (update_only) {
		dns_rdataset_init(&rdataset);
		result = dns_db_findrdataset(
			db, node, version, dns_rdatatype_nsec,
			dns_rdatatype_none, 0, &rdataset, NULL);
		if (dns_rdataset_isassociated(&rdataset)) {
			dns_rdataset_disassociate(&rdataset);
		}
		if (result == ISC_R_NOTFOUND) {
			goto success;
		}
		if (result != ISC_R_SUCCESS) {
			goto failure;
		}
	}
	CHECK(delete_nsec(db, version, node, name, diff));
	CHECK(add_nsec(db, version, name, node, nsecttl, false, diff));
success:
	result = ISC_R_SUCCESS;
failure:
	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}
	return result;
}

static isc_result_t
updatesignwithkey(dns_zone_t *zone, dns_signing_t *signing,
		  dns_dbversion_t *version, bool build_nsec3, dns_ttl_t nsecttl,
		  dns_diff_t *diff) {
	isc_result_t result;
	dns_dbnode_t *node = NULL;
	dns_rdataset_t rdataset;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	unsigned char data[5];
	bool seen_done = false;
	bool have_rr = false;

	dns_rdataset_init(&rdataset);
	result = dns_db_getoriginnode(signing->db, &node);
	if (result != ISC_R_SUCCESS) {
		goto failure;
	}

	result = dns_db_findrdataset(signing->db, node, version,
				     zone->privatetype, dns_rdatatype_none, 0,
				     &rdataset, NULL);
	if (result == ISC_R_NOTFOUND) {
		INSIST(!dns_rdataset_isassociated(&rdataset));
		result = ISC_R_SUCCESS;
		goto failure;
	}
	if (result != ISC_R_SUCCESS) {
		INSIST(!dns_rdataset_isassociated(&rdataset));
		goto failure;
	}
	for (result = dns_rdataset_first(&rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&rdataset))
	{
		dns_rdataset_current(&rdataset, &rdata);
		/*
		 * If we don't match the algorithm or keyid skip the record.
		 */
		if (rdata.length != 5 || rdata.data[0] != signing->algorithm ||
		    rdata.data[1] != ((signing->keyid >> 8) & 0xff) ||
		    rdata.data[2] != (signing->keyid & 0xff))
		{
			have_rr = true;
			dns_rdata_reset(&rdata);
			continue;
		}
		/*
		 * We have a match.  If we were signing (!signing->deleteit)
		 * and we already have a record indicating that we have
		 * finished signing (rdata.data[4] != 0) then keep it.
		 * Otherwise it needs to be deleted as we have removed all
		 * the signatures (signing->deleteit), so any record indicating
		 * completion is now out of date, or we have finished signing
		 * with the new record so we no longer need to remember that
		 * we need to sign the zone with the matching key across a
		 * nameserver re-start.
		 */
		if (!signing->deleteit && rdata.data[4] != 0) {
			seen_done = true;
			have_rr = true;
		} else {
			CHECK(update_one_rr(signing->db, version, diff,
					    DNS_DIFFOP_DEL, &zone->origin,
					    rdataset.ttl, &rdata));
		}
		dns_rdata_reset(&rdata);
	}
	if (result == ISC_R_NOMORE) {
		result = ISC_R_SUCCESS;
	}
	if (!signing->deleteit && !seen_done) {
		/*
		 * If we were signing then we need to indicate that we have
		 * finished signing the zone with this key.  If it is already
		 * there we don't need to add it a second time.
		 */
		data[0] = signing->algorithm;
		data[1] = (signing->keyid >> 8) & 0xff;
		data[2] = signing->keyid & 0xff;
		data[3] = 0;
		data[4] = 1;
		rdata.length = sizeof(data);
		rdata.data = data;
		rdata.type = zone->privatetype;
		rdata.rdclass = dns_db_class(signing->db);
		CHECK(update_one_rr(signing->db, version, diff, DNS_DIFFOP_ADD,
				    &zone->origin, rdataset.ttl, &rdata));
	} else if (!have_rr) {
		dns_name_t *origin = dns_db_origin(signing->db);
		/*
		 * Rebuild the NSEC/NSEC3 record for the origin as we no
		 * longer have any private records.
		 */
		if (build_nsec3) {
			CHECK(dns_nsec3_addnsec3s(signing->db, version, origin,
						  nsecttl, false, diff));
		}
		CHECK(updatesecure(signing->db, version, origin, nsecttl, true,
				   diff));
	}

failure:
	if (dns_rdataset_isassociated(&rdataset)) {
		dns_rdataset_disassociate(&rdataset);
	}
	if (node != NULL) {
		dns_db_detachnode(signing->db, &node);
	}
	return result;
}

/*
 * Called from zone_nsec3chain() in order to update zone records indicating
 * processing status of given NSEC3 chain:
 *
 *   - If the supplied dns_nsec3chain_t structure has been fully processed
 *     (which is indicated by "active" being set to false):
 *
 *       - remove all NSEC3PARAM records matching the relevant NSEC3 chain,
 *
 *       - remove all private-type records containing NSEC3PARAM RDATA matching
 *         the relevant NSEC3 chain.
 *
 *   - If the supplied dns_nsec3chain_t structure has not been fully processed
 *     (which is indicated by "active" being set to true), only remove the
 *     NSEC3PARAM record which matches the relevant NSEC3 chain and has the
 *     "flags" field set to 0.
 *
 *   - If given NSEC3 chain is being added, add an NSEC3PARAM record contained
 *     in the relevant private-type record, but with the "flags" field set to
 *     0, indicating that this NSEC3 chain is now complete for this zone.
 *
 * Note that this function is called at different processing stages for NSEC3
 * chain additions vs. removals and needs to handle all cases properly.
 */
static isc_result_t
fixup_nsec3param(dns_db_t *db, dns_dbversion_t *ver, dns_nsec3chain_t *chain,
		 bool active, dns_rdatatype_t privatetype, dns_diff_t *diff) {
	dns_dbnode_t *node = NULL;
	dns_name_t *name = dns_db_origin(db);
	dns_rdata_t rdata = DNS_RDATA_INIT;
	dns_rdataset_t rdataset;
	dns_rdata_nsec3param_t nsec3param;
	dns_rdata_soa_t soa;
	isc_result_t result;
	isc_buffer_t buffer;
	unsigned char parambuf[DNS_NSEC3PARAM_BUFFERSIZE];
	dns_ttl_t ttl = 0;
	bool nseconly = false, nsec3ok = false;

	dns_rdataset_init(&rdataset);

	result = dns_db_getoriginnode(db, &node);
	RUNTIME_CHECK(result == ISC_R_SUCCESS);

	/* Default TTL is SOA MINIMUM */
	result = dns_db_findrdataset(db, node, ver, dns_rdatatype_soa, 0, 0,
				     &rdataset, NULL);
	if (result == ISC_R_SUCCESS) {
		CHECK(dns_rdataset_first(&rdataset));
		dns_rdataset_current(&rdataset, &rdata);
		CHECK(dns_rdata_tostruct(&rdata, &soa, NULL));
		ttl = soa.minimum;
		dns_rdata_reset(&rdata);
	}
	if (dns_rdataset_isassociated(&rdataset)) {
		dns_rdataset_disassociate(&rdataset);
	}

	result = dns_db_findrdataset(db, node, ver, dns_rdatatype_nsec3param, 0,
				     0, &rdataset, NULL);
	if (result == ISC_R_NOTFOUND) {
		goto try_private;
	}
	if (result != ISC_R_SUCCESS) {
		goto failure;
	}

	/*
	 * Delete all NSEC3PARAM records which match that in nsec3chain.
	 */
	for (result = dns_rdataset_first(&rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&rdataset))
	{
		dns_rdataset_current(&rdataset, &rdata);
		CHECK(dns_rdata_tostruct(&rdata, &nsec3param, NULL));

		if (nsec3param.hash != chain->nsec3param.hash ||
		    (active && nsec3param.flags != 0) ||
		    nsec3param.iterations != chain->nsec3param.iterations ||
		    nsec3param.salt_length != chain->nsec3param.salt_length ||
		    memcmp(nsec3param.salt, chain->nsec3param.salt,
			   nsec3param.salt_length))
		{
			/*
			 * If the SOA minimum is different to the current TTL,
			 * delete the record.  We will re-add it with the new
			 * TTL below.
			 */
			if (rdataset.ttl != ttl) {
				CHECK(update_one_rr(db, ver, diff,
						    DNS_DIFFOP_DEL, name,
						    rdataset.ttl, &rdata));
			}
			dns_rdata_reset(&rdata);
			continue;
		}

		CHECK(update_one_rr(db, ver, diff, DNS_DIFFOP_DEL, name,
				    rdataset.ttl, &rdata));
		dns_rdata_reset(&rdata);
	}
	if (result != ISC_R_NOMORE) {
		goto failure;
	}

	/*
	 * Restore any NSEC3PARAM records that we deleted to change the TTL.
	 */
	if (rdataset.ttl != ttl) {
		for (result = dns_rdataset_first(&rdataset);
		     result == ISC_R_SUCCESS;
		     result = dns_rdataset_next(&rdataset))
		{
			dns_rdataset_current(&rdataset, &rdata);
			CHECK(dns_rdata_tostruct(&rdata, &nsec3param, NULL));

			if (nsec3param.hash != chain->nsec3param.hash ||
			    (active && nsec3param.flags != 0) ||
			    nsec3param.iterations !=
				    chain->nsec3param.iterations ||
			    nsec3param.salt_length !=
				    chain->nsec3param.salt_length ||
			    memcmp(nsec3param.salt, chain->nsec3param.salt,
				   nsec3param.salt_length))
			{
				CHECK(update_one_rr(db, ver, diff,
						    DNS_DIFFOP_ADD, name, ttl,
						    &rdata));
			}
			dns_rdata_reset(&rdata);
		}
	}

	dns_rdataset_disassociate(&rdataset);

try_private:

	if (active) {
		goto add;
	}

	result = dns_nsec_nseconly(db, ver, diff, &nseconly);
	nsec3ok = (result == ISC_R_SUCCESS && !nseconly);

	/*
	 * Delete all private records which match that in nsec3chain.
	 */
	result = dns_db_findrdataset(db, node, ver, privatetype, 0, 0,
				     &rdataset, NULL);
	if (result == ISC_R_NOTFOUND) {
		goto add;
	}
	if (result != ISC_R_SUCCESS) {
		goto failure;
	}

	for (result = dns_rdataset_first(&rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&rdataset))
	{
		dns_rdata_t private = DNS_RDATA_INIT;
		unsigned char buf[DNS_NSEC3PARAM_BUFFERSIZE];

		dns_rdataset_current(&rdataset, &private);
		if (!dns_nsec3param_fromprivate(&private, &rdata, buf,
						sizeof(buf)))
		{
			continue;
		}
		CHECK(dns_rdata_tostruct(&rdata, &nsec3param, NULL));

		if ((!nsec3ok &&
		     (nsec3param.flags & DNS_NSEC3FLAG_INITIAL) != 0) ||
		    nsec3param.hash != chain->nsec3param.hash ||
		    nsec3param.iterations != chain->nsec3param.iterations ||
		    nsec3param.salt_length != chain->nsec3param.salt_length ||
		    memcmp(nsec3param.salt, chain->nsec3param.salt,
			   nsec3param.salt_length))
		{
			dns_rdata_reset(&rdata);
			continue;
		}

		CHECK(update_one_rr(db, ver, diff, DNS_DIFFOP_DEL, name,
				    rdataset.ttl, &private));
		dns_rdata_reset(&rdata);
	}
	if (result != ISC_R_NOMORE) {
		goto failure;
	}

add:
	if ((chain->nsec3param.flags & DNS_NSEC3FLAG_REMOVE) != 0) {
		result = ISC_R_SUCCESS;
		goto failure;
	}

	/*
	 * Add a NSEC3PARAM record which matches that in nsec3chain but
	 * with all flags bits cleared.
	 *
	 * Note: we do not clear chain->nsec3param.flags as this change
	 * may be reversed.
	 */
	isc_buffer_init(&buffer, &parambuf, sizeof(parambuf));
	CHECK(dns_rdata_fromstruct(&rdata, dns_db_class(db),
				   dns_rdatatype_nsec3param, &chain->nsec3param,
				   &buffer));
	rdata.data[1] = 0; /* Clear flag bits. */
	CHECK(update_one_rr(db, ver, diff, DNS_DIFFOP_ADD, name, ttl, &rdata));

failure:
	dns_db_detachnode(db, &node);
	if (dns_rdataset_isassociated(&rdataset)) {
		dns_rdataset_disassociate(&rdataset);
	}
	return result;
}

static isc_result_t
delete_nsec(dns_db_t *db, dns_dbversion_t *ver, dns_dbnode_t *node,
	    dns_name_t *name, dns_diff_t *diff) {
	dns_rdataset_t rdataset;
	isc_result_t result;

	dns_rdataset_init(&rdataset);

	result = dns_db_findrdataset(db, node, ver, dns_rdatatype_nsec, 0, 0,
				     &rdataset, NULL);
	if (result == ISC_R_NOTFOUND) {
		return ISC_R_SUCCESS;
	}
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	for (result = dns_rdataset_first(&rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&rdataset))
	{
		dns_rdata_t rdata = DNS_RDATA_INIT;

		dns_rdataset_current(&rdataset, &rdata);
		CHECK(update_one_rr(db, ver, diff, DNS_DIFFOP_DEL, name,
				    rdataset.ttl, &rdata));
	}
	if (result == ISC_R_NOMORE) {
		result = ISC_R_SUCCESS;
	}
failure:
	dns_rdataset_disassociate(&rdataset);
	return result;
}

static isc_result_t
deletematchingnsec3(dns_db_t *db, dns_dbversion_t *ver, dns_dbnode_t *node,
		    dns_name_t *name, const dns_rdata_nsec3param_t *param,
		    dns_diff_t *diff) {
	dns_rdataset_t rdataset;
	dns_rdata_nsec3_t nsec3;
	isc_result_t result;

	dns_rdataset_init(&rdataset);
	result = dns_db_findrdataset(db, node, ver, dns_rdatatype_nsec3, 0, 0,
				     &rdataset, NULL);
	if (result == ISC_R_NOTFOUND) {
		return ISC_R_SUCCESS;
	}
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	for (result = dns_rdataset_first(&rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&rdataset))
	{
		dns_rdata_t rdata = DNS_RDATA_INIT;

		dns_rdataset_current(&rdataset, &rdata);
		CHECK(dns_rdata_tostruct(&rdata, &nsec3, NULL));
		if (nsec3.hash != param->hash ||
		    nsec3.iterations != param->iterations ||
		    nsec3.salt_length != param->salt_length ||
		    memcmp(nsec3.salt, param->salt, nsec3.salt_length))
		{
			continue;
		}
		CHECK(update_one_rr(db, ver, diff, DNS_DIFFOP_DEL, name,
				    rdataset.ttl, &rdata));
	}
	if (result == ISC_R_NOMORE) {
		result = ISC_R_SUCCESS;
	}
failure:
	dns_rdataset_disassociate(&rdataset);
	return result;
}

static isc_result_t
need_nsec_chain(dns_db_t *db, dns_dbversion_t *ver,
		const dns_rdata_nsec3param_t *param, bool *answer) {
	dns_dbnode_t *node = NULL;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	dns_rdata_nsec3param_t myparam;
	dns_rdataset_t rdataset;
	isc_result_t result;

	*answer = false;

	result = dns_db_getoriginnode(db, &node);
	RUNTIME_CHECK(result == ISC_R_SUCCESS);

	dns_rdataset_init(&rdataset);

	result = dns_db_findrdataset(db, node, ver, dns_rdatatype_nsec, 0, 0,
				     &rdataset, NULL);
	if (result == ISC_R_SUCCESS) {
		dns_rdataset_disassociate(&rdataset);
		dns_db_detachnode(db, &node);
		return result;
	}
	if (result != ISC_R_NOTFOUND) {
		dns_db_detachnode(db, &node);
		return result;
	}

	result = dns_db_findrdataset(db, node, ver, dns_rdatatype_nsec3param, 0,
				     0, &rdataset, NULL);
	if (result == ISC_R_NOTFOUND) {
		*answer = true;
		dns_db_detachnode(db, &node);
		return ISC_R_SUCCESS;
	}
	if (result != ISC_R_SUCCESS) {
		dns_db_detachnode(db, &node);
		return result;
	}

	for (result = dns_rdataset_first(&rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&rdataset))
	{
		dns_rdataset_current(&rdataset, &rdata);
		CHECK(dns_rdata_tostruct(&rdata, &myparam, NULL));
		dns_rdata_reset(&rdata);
		/*
		 * Ignore any NSEC3PARAM removals.
		 */
		if (NSEC3REMOVE(myparam.flags)) {
			continue;
		}
		/*
		 * Ignore the chain that we are in the process of deleting.
		 */
		if (myparam.hash == param->hash &&
		    myparam.iterations == param->iterations &&
		    myparam.salt_length == param->salt_length &&
		    !memcmp(myparam.salt, param->salt, myparam.salt_length))
		{
			continue;
		}
		/*
		 * Found an active NSEC3 chain.
		 */
		break;
	}
	if (result == ISC_R_NOMORE) {
		*answer = true;
		result = ISC_R_SUCCESS;
	}

failure:
	if (dns_rdataset_isassociated(&rdataset)) {
		dns_rdataset_disassociate(&rdataset);
	}
	dns_db_detachnode(db, &node);
	return result;
}

/*%
 * Given a tuple which is part of a diff, return a pointer to the next tuple in
 * that diff which has the same name and type (or NULL if no such tuple is
 * found).
 */
static dns_difftuple_t *
find_next_matching_tuple(dns_difftuple_t *cur) {
	dns_difftuple_t *next = cur;

	while ((next = ISC_LIST_NEXT(next, link)) != NULL) {
		if (cur->rdata.type == next->rdata.type &&
		    dns_name_equal(&cur->name, &next->name))
		{
			return next;
		}
	}

	return NULL;
}

/*%
 * Remove all tuples with the same name and type as 'cur' from 'src' and append
 * them to 'dst'.
 */
static void
move_matching_tuples(dns_difftuple_t *cur, dns_diff_t *src, dns_diff_t *dst) {
	do {
		dns_difftuple_t *next = find_next_matching_tuple(cur);
		ISC_LIST_UNLINK(src->tuples, cur, link);
		dns_diff_appendminimal(dst, &cur);
		cur = next;
	} while (cur != NULL);
}

/*%
 * Add/remove DNSSEC signatures for the list of "raw" zone changes supplied in
 * 'diff'.  Gradually remove tuples from 'diff' and append them to 'zonediff'
 * along with tuples representing relevant signature changes.
 */
isc_result_t
dns__zone_updatesigs(dns_diff_t *diff, dns_db_t *db, dns_dbversion_t *version,
		     dst_key_t *zone_keys[], unsigned int nkeys,
		     dns_zone_t *zone, isc_stdtime_t inception,
		     isc_stdtime_t expire, isc_stdtime_t keyexpire,
		     isc_stdtime_t now, dns__zonediff_t *zonediff) {
	dns_difftuple_t *tuple;
	isc_result_t result;

	while ((tuple = ISC_LIST_HEAD(diff->tuples)) != NULL) {
		isc_stdtime_t exp = expire;

		if (keyexpire != 0 &&
		    dns_rdatatype_iskeymaterial(tuple->rdata.type))
		{
			exp = keyexpire;
		}

		result = del_sigs(zone, db, version, &tuple->name,
				  tuple->rdata.type, zonediff, zone_keys, nkeys,
				  now, false);
		if (result != ISC_R_SUCCESS) {
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "dns__zone_updatesigs:del_sigs -> %s",
				     isc_result_totext(result));
			return result;
		}
		result = add_sigs(db, version, &tuple->name, zone,
				  tuple->rdata.type, zonediff->diff, zone_keys,
				  nkeys, zone->mctx, now, inception, exp);
		if (result != ISC_R_SUCCESS) {
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "dns__zone_updatesigs:add_sigs -> %s",
				     isc_result_totext(result));
			return result;
		}

		/*
		 * Signature changes for all RRs with name tuple->name and type
		 * tuple->rdata.type were appended to zonediff->diff.  Now we
		 * remove all the "raw" changes with the same name and type
		 * from diff (so that they are not processed by this loop
		 * again) and append them to zonediff so that they get applied.
		 */
		move_matching_tuples(tuple, diff, zonediff->diff);
	}
	return ISC_R_SUCCESS;
}

/*
 * Incrementally build and sign a new NSEC3 chain using the parameters
 * requested.
 */
static void
zone_nsec3chain(dns_zone_t *zone) {
	dns_db_t *db = NULL;
	dns_dbnode_t *node = NULL;
	dns_dbversion_t *version = NULL;
	dns_diff_t _sig_diff;
	dns_diff_t nsec_diff;
	dns_diff_t nsec3_diff;
	dns_diff_t param_diff;
	dns__zonediff_t zonediff;
	dns_fixedname_t fixed;
	dns_fixedname_t nextfixed;
	dns_name_t *name, *nextname;
	dns_rdataset_t rdataset;
	dns_nsec3chain_t *nsec3chain = NULL, *nextnsec3chain;
	dns_nsec3chainlist_t cleanup;
	dst_key_t *zone_keys[DNS_MAXZONEKEYS];
	int32_t signatures;
	bool delegation;
	bool first;
	isc_result_t result;
	isc_stdtime_t now, inception, soaexpire, expire;
	unsigned int i;
	unsigned int nkeys = 0;
	uint32_t nodes;
	bool unsecure = false;
	bool seen_soa, seen_ns, seen_dname, seen_ds;
	bool seen_nsec, seen_nsec3, seen_rr;
	dns_rdatasetiter_t *iterator = NULL;
	bool buildnsecchain;
	bool updatensec = false;
	dns_rdatatype_t privatetype = zone->privatetype;

	ENTER;

	dns_rdataset_init(&rdataset);
	name = dns_fixedname_initname(&fixed);
	nextname = dns_fixedname_initname(&nextfixed);
	dns_diff_init(zone->mctx, &param_diff);
	dns_diff_init(zone->mctx, &nsec3_diff);
	dns_diff_init(zone->mctx, &nsec_diff);
	dns_diff_init(zone->mctx, &_sig_diff);
	zonediff_init(&zonediff, &_sig_diff);
	ISC_LIST_INIT(cleanup);

	/*
	 * Updates are disabled.  Pause for 5 minutes.
	 */
	if (zone->update_disabled) {
		result = ISC_R_FAILURE;
		goto failure;
	}

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	/*
	 * This function is called when zone timer fires, after the latter gets
	 * set by zone_addnsec3chain().  If the action triggering the call to
	 * zone_addnsec3chain() is closely followed by a zone deletion request,
	 * it might turn out that the timer thread will not be woken up until
	 * after the zone is deleted by rmzone(), which calls dns_db_detach()
	 * for zone->db, causing the latter to become NULL.  Return immediately
	 * if that happens.
	 */
	if (zone->db != NULL) {
		dns_db_attach(zone->db, &db);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
	if (db == NULL) {
		return;
	}

	result = dns_db_newversion(db, &version);
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR,
			   "zone_nsec3chain:dns_db_newversion -> %s",
			   isc_result_totext(result));
		goto failure;
	}

	now = isc_stdtime_now();

	result = dns_zone_findkeys(zone, db, version, now, zone->mctx,
				   DNS_MAXZONEKEYS, zone_keys, &nkeys);
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR,
			   "zone_nsec3chain:dns_zone_findkeys -> %s",
			   isc_result_totext(result));
		goto failure;
	}

	calculate_rrsig_validity(zone, now, &inception, &soaexpire, NULL,
				 &expire);

	/*
	 * We keep pulling nodes off each iterator in turn until
	 * we have no more nodes to pull off or we reach the limits
	 * for this quantum.
	 */
	nodes = zone->nodes;
	signatures = zone->signatures;
	LOCK_ZONE(zone);
	nsec3chain = ISC_LIST_HEAD(zone->nsec3chain);
	UNLOCK_ZONE(zone);
	first = true;

	if (nsec3chain != NULL) {
		nsec3chain->save_delete_nsec = nsec3chain->delete_nsec;
	}
	/*
	 * Generate new NSEC3 chains first.
	 *
	 * The following while loop iterates over nodes in the zone database,
	 * updating the NSEC3 chain by calling dns_nsec3_addnsec3() for each of
	 * them.  Once all nodes are processed, the "delete_nsec" field is
	 * consulted to check whether we are supposed to remove NSEC records
	 * from the zone database; if so, the database iterator is reset to
	 * point to the first node and the loop traverses all of them again,
	 * this time removing NSEC records.  If we hit a node which is obscured
	 * by a delegation or a DNAME, nodes are skipped over until we find one
	 * that is not obscured by the same obscuring name and then normal
	 * processing is resumed.
	 *
	 * The above is repeated until all requested NSEC3 chain changes are
	 * applied or when we reach the limits for this quantum, whichever
	 * happens first.
	 *
	 * Note that the "signatures" variable is only used here to limit the
	 * amount of work performed.  Actual DNSSEC signatures are only
	 * generated by dns__zone_updatesigs() calls later in this function.
	 */
	while (nsec3chain != NULL && nodes-- > 0 && signatures > 0) {
		dns_dbiterator_pause(nsec3chain->dbiterator);

		LOCK_ZONE(zone);
		nextnsec3chain = ISC_LIST_NEXT(nsec3chain, link);

		ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
		if (nsec3chain->done || nsec3chain->db != zone->db) {
			ISC_LIST_UNLINK(zone->nsec3chain, nsec3chain, link);
			ISC_LIST_APPEND(cleanup, nsec3chain, link);
		}
		ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
		UNLOCK_ZONE(zone);
		if (ISC_LIST_TAIL(cleanup) == nsec3chain) {
			goto next_addchain;
		}

		/*
		 * Possible future db.
		 */
		if (nsec3chain->db != db) {
			goto next_addchain;
		}

		if (NSEC3REMOVE(nsec3chain->nsec3param.flags)) {
			goto next_addchain;
		}

		dns_dbiterator_current(nsec3chain->dbiterator, &node, name);

		if (nsec3chain->delete_nsec) {
			delegation = false;
			dns_dbiterator_pause(nsec3chain->dbiterator);
			CHECK(delete_nsec(db, version, node, name, &nsec_diff));
			goto next_addnode;
		}
		/*
		 * On the first pass we need to check if the current node
		 * has not been obscured.
		 */
		delegation = false;
		unsecure = false;
		if (first) {
			dns_fixedname_t ffound;
			dns_name_t *found;
			found = dns_fixedname_initname(&ffound);
			result = dns_db_find(
				db, name, version, dns_rdatatype_soa,
				DNS_DBFIND_NOWILD, 0, NULL, found, NULL, NULL);
			if ((result == DNS_R_DELEGATION ||
			     result == DNS_R_DNAME) &&
			    !dns_name_equal(name, found))
			{
				/*
				 * Remember the obscuring name so that
				 * we skip all obscured names.
				 */
				dns_name_copy(found, name);
				delegation = true;
				goto next_addnode;
			}
		}

		/*
		 * Check to see if this is a bottom of zone node.
		 */
		result = dns_db_allrdatasets(db, node, version, 0, 0,
					     &iterator);
		if (result == ISC_R_NOTFOUND) {
			/* Empty node? */
			goto next_addnode;
		}
		if (result != ISC_R_SUCCESS) {
			goto failure;
		}

		seen_soa = seen_ns = seen_dname = seen_ds = seen_nsec = false;
		for (result = dns_rdatasetiter_first(iterator);
		     result == ISC_R_SUCCESS;
		     result = dns_rdatasetiter_next(iterator))
		{
			dns_rdatasetiter_current(iterator, &rdataset);
			INSIST(rdataset.type != dns_rdatatype_nsec3);
			if (rdataset.type == dns_rdatatype_soa) {
				seen_soa = true;
			} else if (rdataset.type == dns_rdatatype_ns) {
				seen_ns = true;
			} else if (rdataset.type == dns_rdatatype_dname) {
				seen_dname = true;
			} else if (rdataset.type == dns_rdatatype_ds) {
				seen_ds = true;
			} else if (rdataset.type == dns_rdatatype_nsec) {
				seen_nsec = true;
			}
			dns_rdataset_disassociate(&rdataset);
		}
		dns_rdatasetiter_destroy(&iterator);
		/*
		 * Is there a NSEC chain than needs to be cleaned up?
		 */
		if (seen_nsec) {
			nsec3chain->seen_nsec = true;
		}
		if (seen_ns && !seen_soa && !seen_ds) {
			unsecure = true;
		}
		if ((seen_ns && !seen_soa) || seen_dname) {
			delegation = true;
		}

		/*
		 * Process one node.
		 */
		dns_dbiterator_pause(nsec3chain->dbiterator);
		result = dns_nsec3_addnsec3(
			db, version, name, &nsec3chain->nsec3param,
			zone_nsecttl(zone), unsecure, &nsec3_diff);
		if (result != ISC_R_SUCCESS) {
			dnssec_log(zone, ISC_LOG_ERROR,
				   "zone_nsec3chain:"
				   "dns_nsec3_addnsec3 -> %s",
				   isc_result_totext(result));
			goto failure;
		}

		/*
		 * Treat each call to dns_nsec3_addnsec3() as if it's cost is
		 * two signatures.  Additionally there will, in general, be
		 * two signature generated below.
		 *
		 * If we are only changing the optout flag the cost is half
		 * that of the cost of generating a completely new chain.
		 */
		signatures -= 4;

		/*
		 * Go onto next node.
		 */
	next_addnode:
		first = false;
		dns_db_detachnode(db, &node);
		do {
			result = dns_dbiterator_next(nsec3chain->dbiterator);

			if (result == ISC_R_NOMORE && nsec3chain->delete_nsec) {
				dns_dbiterator_pause(nsec3chain->dbiterator);
				CHECK(fixup_nsec3param(db, version, nsec3chain,
						       false, privatetype,
						       &param_diff));
				LOCK_ZONE(zone);
				ISC_LIST_UNLINK(zone->nsec3chain, nsec3chain,
						link);
				UNLOCK_ZONE(zone);
				ISC_LIST_APPEND(cleanup, nsec3chain, link);
				goto next_addchain;
			}
			if (result == ISC_R_NOMORE) {
				dns_dbiterator_pause(nsec3chain->dbiterator);
				if (nsec3chain->seen_nsec) {
					CHECK(fixup_nsec3param(
						db, version, nsec3chain, true,
						privatetype, &param_diff));
					nsec3chain->delete_nsec = true;
					goto same_addchain;
				}
				CHECK(fixup_nsec3param(db, version, nsec3chain,
						       false, privatetype,
						       &param_diff));
				LOCK_ZONE(zone);
				ISC_LIST_UNLINK(zone->nsec3chain, nsec3chain,
						link);
				UNLOCK_ZONE(zone);
				ISC_LIST_APPEND(cleanup, nsec3chain, link);
				goto next_addchain;
			} else if (result != ISC_R_SUCCESS) {
				dnssec_log(zone, ISC_LOG_ERROR,
					   "zone_nsec3chain:"
					   "dns_dbiterator_next -> %s",
					   isc_result_totext(result));
				goto failure;
			} else if (delegation) {
				dns_dbiterator_current(nsec3chain->dbiterator,
						       &node, nextname);
				dns_db_detachnode(db, &node);
				if (!dns_name_issubdomain(nextname, name)) {
					break;
				}
			} else {
				break;
			}
		} while (1);
		continue;

	same_addchain:
		CHECK(dns_dbiterator_first(nsec3chain->dbiterator));
		first = true;
		continue;

	next_addchain:
		dns_dbiterator_pause(nsec3chain->dbiterator);
		nsec3chain = nextnsec3chain;
		first = true;
		if (nsec3chain != NULL) {
			nsec3chain->save_delete_nsec = nsec3chain->delete_nsec;
		}
	}

	if (nsec3chain != NULL) {
		goto skip_removals;
	}

	/*
	 * Process removals.
	 *
	 * This is a counterpart of the above while loop which takes care of
	 * removing an NSEC3 chain.  It starts with determining whether the
	 * zone needs to switch from NSEC3 to NSEC; if so, it first builds an
	 * NSEC chain by iterating over all nodes in the zone database and only
	 * then goes on to remove NSEC3 records be iterating over all nodes
	 * again and calling deletematchingnsec3() for each of them; otherwise,
	 * it starts removing NSEC3 records immediately.  Rules for processing
	 * obscured nodes and interrupting work are the same as for the while
	 * loop above.
	 */
	LOCK_ZONE(zone);
	nsec3chain = ISC_LIST_HEAD(zone->nsec3chain);
	UNLOCK_ZONE(zone);
	first = true;
	buildnsecchain = false;
	while (nsec3chain != NULL && nodes-- > 0 && signatures > 0) {
		dns_dbiterator_pause(nsec3chain->dbiterator);

		LOCK_ZONE(zone);
		nextnsec3chain = ISC_LIST_NEXT(nsec3chain, link);
		UNLOCK_ZONE(zone);

		if (nsec3chain->db != db) {
			goto next_removechain;
		}

		if (!NSEC3REMOVE(nsec3chain->nsec3param.flags)) {
			goto next_removechain;
		}

		/*
		 * Work out if we need to build a NSEC chain as a consequence
		 * of removing this NSEC3 chain.
		 */
		if (first && !updatensec &&
		    (nsec3chain->nsec3param.flags & DNS_NSEC3FLAG_NONSEC) == 0)
		{
			result = need_nsec_chain(db, version,
						 &nsec3chain->nsec3param,
						 &buildnsecchain);
			if (result != ISC_R_SUCCESS) {
				dnssec_log(zone, ISC_LOG_ERROR,
					   "zone_nsec3chain:"
					   "need_nsec_chain -> %s",
					   isc_result_totext(result));
				goto failure;
			}
		}

		if (first) {
			dnssec_log(zone, ISC_LOG_DEBUG(3),
				   "zone_nsec3chain:buildnsecchain = %u\n",
				   buildnsecchain);
		}

		dns_dbiterator_current(nsec3chain->dbiterator, &node, name);
		dns_dbiterator_pause(nsec3chain->dbiterator);
		delegation = false;

		if (!buildnsecchain) {
			/*
			 * Delete the NSEC3PARAM record matching this chain.
			 */
			if (first) {
				result = fixup_nsec3param(
					db, version, nsec3chain, true,
					privatetype, &param_diff);
				if (result != ISC_R_SUCCESS) {
					dnssec_log(zone, ISC_LOG_ERROR,
						   "zone_nsec3chain:"
						   "fixup_nsec3param -> %s",
						   isc_result_totext(result));
					goto failure;
				}
			}

			/*
			 * Delete the NSEC3 records.
			 */
			result = deletematchingnsec3(db, version, node, name,
						     &nsec3chain->nsec3param,
						     &nsec3_diff);
			if (result != ISC_R_SUCCESS) {
				dnssec_log(zone, ISC_LOG_ERROR,
					   "zone_nsec3chain:"
					   "deletematchingnsec3 -> %s",
					   isc_result_totext(result));
				goto failure;
			}
			goto next_removenode;
		}

		if (first) {
			dns_fixedname_t ffound;
			dns_name_t *found;
			found = dns_fixedname_initname(&ffound);
			result = dns_db_find(
				db, name, version, dns_rdatatype_soa,
				DNS_DBFIND_NOWILD, 0, NULL, found, NULL, NULL);
			if ((result == DNS_R_DELEGATION ||
			     result == DNS_R_DNAME) &&
			    !dns_name_equal(name, found))
			{
				/*
				 * Remember the obscuring name so that
				 * we skip all obscured names.
				 */
				dns_name_copy(found, name);
				delegation = true;
				goto next_removenode;
			}
		}

		/*
		 * Check to see if this is a bottom of zone node.
		 */
		result = dns_db_allrdatasets(db, node, version, 0, 0,
					     &iterator);
		if (result == ISC_R_NOTFOUND) {
			/* Empty node? */
			goto next_removenode;
		}
		if (result != ISC_R_SUCCESS) {
			goto failure;
		}

		seen_soa = seen_ns = seen_dname = seen_nsec3 = seen_nsec =
			seen_rr = false;
		for (result = dns_rdatasetiter_first(iterator);
		     result == ISC_R_SUCCESS;
		     result = dns_rdatasetiter_next(iterator))
		{
			dns_rdatasetiter_current(iterator, &rdataset);
			if (rdataset.type == dns_rdatatype_soa) {
				seen_soa = true;
			} else if (rdataset.type == dns_rdatatype_ns) {
				seen_ns = true;
			} else if (rdataset.type == dns_rdatatype_dname) {
				seen_dname = true;
			} else if (rdataset.type == dns_rdatatype_nsec) {
				seen_nsec = true;
			} else if (rdataset.type == dns_rdatatype_nsec3) {
				seen_nsec3 = true;
			} else if (rdataset.type != dns_rdatatype_rrsig) {
				seen_rr = true;
			}
			dns_rdataset_disassociate(&rdataset);
		}
		dns_rdatasetiter_destroy(&iterator);

		if (!seen_rr || seen_nsec3 || seen_nsec) {
			goto next_removenode;
		}
		if ((seen_ns && !seen_soa) || seen_dname) {
			delegation = true;
		}

		/*
		 * Add a NSEC record except at the origin.
		 */
		if (!dns_name_equal(name, dns_db_origin(db))) {
			dns_dbiterator_pause(nsec3chain->dbiterator);
			CHECK(add_nsec(db, version, name, node,
				       zone_nsecttl(zone), delegation,
				       &nsec_diff));
			signatures--;
		}

	next_removenode:
		first = false;
		dns_db_detachnode(db, &node);
		do {
			result = dns_dbiterator_next(nsec3chain->dbiterator);
			if (result == ISC_R_NOMORE && buildnsecchain) {
				/*
				 * The NSEC chain should now be built.
				 * We can now remove the NSEC3 chain.
				 */
				updatensec = true;
				goto same_removechain;
			}
			if (result == ISC_R_NOMORE) {
				dns_dbiterator_pause(nsec3chain->dbiterator);
				LOCK_ZONE(zone);
				ISC_LIST_UNLINK(zone->nsec3chain, nsec3chain,
						link);
				UNLOCK_ZONE(zone);
				ISC_LIST_APPEND(cleanup, nsec3chain, link);
				result = fixup_nsec3param(
					db, version, nsec3chain, false,
					privatetype, &param_diff);
				if (result != ISC_R_SUCCESS) {
					dnssec_log(zone, ISC_LOG_ERROR,
						   "zone_nsec3chain:"
						   "fixup_nsec3param -> %s",
						   isc_result_totext(result));
					goto failure;
				}
				goto next_removechain;
			} else if (result != ISC_R_SUCCESS) {
				dnssec_log(zone, ISC_LOG_ERROR,
					   "zone_nsec3chain:"
					   "dns_dbiterator_next -> %s",
					   isc_result_totext(result));
				goto failure;
			} else if (delegation) {
				dns_dbiterator_current(nsec3chain->dbiterator,
						       &node, nextname);
				dns_db_detachnode(db, &node);
				if (!dns_name_issubdomain(nextname, name)) {
					break;
				}
			} else {
				break;
			}
		} while (1);
		continue;

	same_removechain:
		CHECK(dns_dbiterator_first(nsec3chain->dbiterator));
		buildnsecchain = false;
		first = true;
		continue;

	next_removechain:
		dns_dbiterator_pause(nsec3chain->dbiterator);
		nsec3chain = nextnsec3chain;
		first = true;
	}

skip_removals:
	/*
	 * We may need to update the NSEC/NSEC3 records for the zone apex.
	 */
	if (!ISC_LIST_EMPTY(param_diff.tuples)) {
		bool rebuild_nsec = false, rebuild_nsec3 = false;
		result = dns_db_getoriginnode(db, &node);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
		result = dns_db_allrdatasets(db, node, version, 0, 0,
					     &iterator);
		if (result != ISC_R_SUCCESS) {
			dnssec_log(zone, ISC_LOG_ERROR,
				   "zone_nsec3chain:dns_db_allrdatasets -> %s",
				   isc_result_totext(result));
			goto failure;
		}
		for (result = dns_rdatasetiter_first(iterator);
		     result == ISC_R_SUCCESS;
		     result = dns_rdatasetiter_next(iterator))
		{
			dns_rdatasetiter_current(iterator, &rdataset);
			if (rdataset.type == dns_rdatatype_nsec) {
				rebuild_nsec = true;
			} else if (rdataset.type == dns_rdatatype_nsec3param) {
				rebuild_nsec3 = true;
			}
			dns_rdataset_disassociate(&rdataset);
		}
		dns_rdatasetiter_destroy(&iterator);
		dns_db_detachnode(db, &node);

		if (rebuild_nsec) {
			if (nsec3chain != NULL) {
				dns_dbiterator_pause(nsec3chain->dbiterator);
			}

			result = updatesecure(db, version, &zone->origin,
					      zone_nsecttl(zone), true,
					      &nsec_diff);
			if (result != ISC_R_SUCCESS) {
				dnssec_log(zone, ISC_LOG_ERROR,
					   "zone_nsec3chain:updatesecure -> %s",
					   isc_result_totext(result));
				goto failure;
			}
		}

		if (rebuild_nsec3) {
			if (nsec3chain != NULL) {
				dns_dbiterator_pause(nsec3chain->dbiterator);
			}

			result = dns_nsec3_addnsec3s(
				db, version, dns_db_origin(db),
				zone_nsecttl(zone), false, &nsec3_diff);
			if (result != ISC_R_SUCCESS) {
				dnssec_log(zone, ISC_LOG_ERROR,
					   "zone_nsec3chain:"
					   "dns_nsec3_addnsec3s -> %s",
					   isc_result_totext(result));
				goto failure;
			}
		}
	}

	/*
	 * Add / update signatures for the NSEC3 records.
	 */
	if (nsec3chain != NULL) {
		dns_dbiterator_pause(nsec3chain->dbiterator);
	}
	result = dns__zone_updatesigs(&nsec3_diff, db, version, zone_keys,
				      nkeys, zone, inception, expire, 0, now,
				      &zonediff);
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR,
			   "zone_nsec3chain:dns__zone_updatesigs -> %s",
			   isc_result_totext(result));
		goto failure;
	}

	/*
	 * We have changed the NSEC3PARAM or private RRsets
	 * above so we need to update the signatures.
	 */
	result = dns__zone_updatesigs(&param_diff, db, version, zone_keys,
				      nkeys, zone, inception, expire, 0, now,
				      &zonediff);
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR,
			   "zone_nsec3chain:dns__zone_updatesigs -> %s",
			   isc_result_totext(result));
		goto failure;
	}

	if (updatensec) {
		result = updatesecure(db, version, &zone->origin,
				      zone_nsecttl(zone), false, &nsec_diff);
		if (result != ISC_R_SUCCESS) {
			dnssec_log(zone, ISC_LOG_ERROR,
				   "zone_nsec3chain:updatesecure -> %s",
				   isc_result_totext(result));
			goto failure;
		}
	}

	result = dns__zone_updatesigs(&nsec_diff, db, version, zone_keys, nkeys,
				      zone, inception, expire, 0, now,
				      &zonediff);
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR,
			   "zone_nsec3chain:dns__zone_updatesigs -> %s",
			   isc_result_totext(result));
		goto failure;
	}

	/*
	 * If we made no effective changes to the zone then we can just
	 * cleanup otherwise we need to increment the serial.
	 */
	if (ISC_LIST_EMPTY(zonediff.diff->tuples)) {
		/*
		 * No need to call dns_db_closeversion() here as it is
		 * called with commit = true below.
		 */
		goto done;
	}

	result = del_sigs(zone, db, version, &zone->origin, dns_rdatatype_soa,
			  &zonediff, zone_keys, nkeys, now, false);
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR,
			   "zone_nsec3chain:del_sigs -> %s",
			   isc_result_totext(result));
		goto failure;
	}

	result = update_soa_serial(zone, db, version, zonediff.diff, zone->mctx,
				   zone->updatemethod);
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR,
			   "zone_nsec3chain:update_soa_serial -> %s",
			   isc_result_totext(result));
		goto failure;
	}

	result = add_sigs(db, version, &zone->origin, zone, dns_rdatatype_soa,
			  zonediff.diff, zone_keys, nkeys, zone->mctx, now,
			  inception, soaexpire);
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR,
			   "zone_nsec3chain:add_sigs -> %s",
			   isc_result_totext(result));
		goto failure;
	}

	/* Write changes to journal file. */
	CHECK(zone_journal(zone, zonediff.diff, NULL, "zone_nsec3chain"));

	LOCK_ZONE(zone);
	zone_needdump(zone, DNS_DUMP_DELAY);
	DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NEEDNOTIFY);
	UNLOCK_ZONE(zone);

done:
	/*
	 * Pause all iterators so that dns_db_closeversion() can succeed.
	 */
	LOCK_ZONE(zone);
	for (nsec3chain = ISC_LIST_HEAD(zone->nsec3chain); nsec3chain != NULL;
	     nsec3chain = ISC_LIST_NEXT(nsec3chain, link))
	{
		dns_dbiterator_pause(nsec3chain->dbiterator);
	}
	UNLOCK_ZONE(zone);

	/*
	 * Everything has succeeded. Commit the changes.
	 * Unconditionally commit as zonediff.offline not checked above.
	 */
	dns_db_closeversion(db, &version, true);

	/*
	 * Everything succeeded so we can clean these up now.
	 */
	nsec3chain = ISC_LIST_HEAD(cleanup);
	while (nsec3chain != NULL) {
		ISC_LIST_UNLINK(cleanup, nsec3chain, link);
		dns_db_detach(&nsec3chain->db);
		dns_dbiterator_destroy(&nsec3chain->dbiterator);
		isc_mem_put(zone->mctx, nsec3chain, sizeof *nsec3chain);
		nsec3chain = ISC_LIST_HEAD(cleanup);
	}

	LOCK_ZONE(zone);
	set_resigntime(zone);
	UNLOCK_ZONE(zone);

failure:
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR, "zone_nsec3chain: %s",
			   isc_result_totext(result));
	}

	/*
	 * On error roll back the current nsec3chain.
	 */
	if (result != ISC_R_SUCCESS && nsec3chain != NULL) {
		if (nsec3chain->done) {
			dns_db_detach(&nsec3chain->db);
			dns_dbiterator_destroy(&nsec3chain->dbiterator);
			isc_mem_put(zone->mctx, nsec3chain, sizeof *nsec3chain);
		} else {
			result = dns_dbiterator_first(nsec3chain->dbiterator);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);
			dns_dbiterator_pause(nsec3chain->dbiterator);
			nsec3chain->delete_nsec = nsec3chain->save_delete_nsec;
		}
	}

	/*
	 * Rollback the cleanup list.
	 */
	nsec3chain = ISC_LIST_TAIL(cleanup);
	while (nsec3chain != NULL) {
		ISC_LIST_UNLINK(cleanup, nsec3chain, link);
		if (nsec3chain->done) {
			dns_db_detach(&nsec3chain->db);
			dns_dbiterator_destroy(&nsec3chain->dbiterator);
			isc_mem_put(zone->mctx, nsec3chain, sizeof *nsec3chain);
		} else {
			LOCK_ZONE(zone);
			ISC_LIST_PREPEND(zone->nsec3chain, nsec3chain, link);
			UNLOCK_ZONE(zone);
			result = dns_dbiterator_first(nsec3chain->dbiterator);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);
			dns_dbiterator_pause(nsec3chain->dbiterator);
			nsec3chain->delete_nsec = nsec3chain->save_delete_nsec;
		}
		nsec3chain = ISC_LIST_TAIL(cleanup);
	}

	LOCK_ZONE(zone);
	for (nsec3chain = ISC_LIST_HEAD(zone->nsec3chain); nsec3chain != NULL;
	     nsec3chain = ISC_LIST_NEXT(nsec3chain, link))
	{
		dns_dbiterator_pause(nsec3chain->dbiterator);
	}
	UNLOCK_ZONE(zone);

	dns_diff_clear(&param_diff);
	dns_diff_clear(&nsec3_diff);
	dns_diff_clear(&nsec_diff);
	dns_diff_clear(&_sig_diff);

	if (iterator != NULL) {
		dns_rdatasetiter_destroy(&iterator);
	}

	for (i = 0; i < nkeys; i++) {
		dst_key_free(&zone_keys[i]);
	}

	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}
	if (version != NULL) {
		dns_db_closeversion(db, &version, false);
		dns_db_detach(&db);
	} else if (db != NULL) {
		dns_db_detach(&db);
	}

	LOCK_ZONE(zone);
	if (ISC_LIST_HEAD(zone->nsec3chain) != NULL) {
		isc_interval_t interval;
		if (zone->update_disabled || result != ISC_R_SUCCESS) {
			isc_interval_set(&interval, 60, 0); /* 1 minute */
		} else {
			isc_interval_set(&interval, 0, 10000000); /* 10 ms */
		}
		isc_time_nowplusinterval(&zone->nsec3chaintime, &interval);
	} else {
		isc_time_settoepoch(&zone->nsec3chaintime);
	}
	UNLOCK_ZONE(zone);

	INSIST(version == NULL);
}

/*%
 * Delete all RRSIG records with the given algorithm and keyid.
 * Remove the NSEC record and RRSIGs if nkeys is zero.
 * If all remaining RRsets are signed with the given algorithm
 * set *has_algp to true.
 */
static isc_result_t
del_sig(dns_db_t *db, dns_dbversion_t *version, dns_name_t *name,
	dns_dbnode_t *node, unsigned int nkeys, dns_secalg_t algorithm,
	uint16_t keyid, bool *has_algp, dns_diff_t *diff) {
	dns_rdata_rrsig_t rrsig;
	dns_rdataset_t rdataset;
	dns_rdatasetiter_t *iterator = NULL;
	isc_result_t result;
	bool alg_missed = false;
	bool alg_found = false;

	char namebuf[DNS_NAME_FORMATSIZE];
	dns_name_format(name, namebuf, sizeof(namebuf));

	result = dns_db_allrdatasets(db, node, version, 0, 0, &iterator);
	if (result != ISC_R_SUCCESS) {
		if (result == ISC_R_NOTFOUND) {
			result = ISC_R_SUCCESS;
		}
		return result;
	}

	dns_rdataset_init(&rdataset);
	for (result = dns_rdatasetiter_first(iterator); result == ISC_R_SUCCESS;
	     result = dns_rdatasetiter_next(iterator))
	{
		bool has_alg = false;
		dns_rdatasetiter_current(iterator, &rdataset);
		if (nkeys == 0 && rdataset.type == dns_rdatatype_nsec) {
			for (result = dns_rdataset_first(&rdataset);
			     result == ISC_R_SUCCESS;
			     result = dns_rdataset_next(&rdataset))
			{
				dns_rdata_t rdata = DNS_RDATA_INIT;
				dns_rdataset_current(&rdataset, &rdata);
				CHECK(update_one_rr(db, version, diff,
						    DNS_DIFFOP_DEL, name,
						    rdataset.ttl, &rdata));
			}
			if (result != ISC_R_NOMORE) {
				goto failure;
			}
			dns_rdataset_disassociate(&rdataset);
			continue;
		}
		if (rdataset.type != dns_rdatatype_rrsig) {
			dns_rdataset_disassociate(&rdataset);
			continue;
		}
		for (result = dns_rdataset_first(&rdataset);
		     result == ISC_R_SUCCESS;
		     result = dns_rdataset_next(&rdataset))
		{
			dns_rdata_t rdata = DNS_RDATA_INIT;
			dns_rdataset_current(&rdataset, &rdata);
			CHECK(dns_rdata_tostruct(&rdata, &rrsig, NULL));
			if (nkeys != 0 && (rrsig.algorithm != algorithm ||
					   rrsig.keyid != keyid))
			{
				if (rrsig.algorithm == algorithm) {
					has_alg = true;
				}
				continue;
			}
			CHECK(update_one_rr(db, version, diff,
					    DNS_DIFFOP_DELRESIGN, name,
					    rdataset.ttl, &rdata));
		}
		dns_rdataset_disassociate(&rdataset);
		if (result != ISC_R_NOMORE) {
			break;
		}

		/*
		 * After deleting, if there's still a signature for
		 * 'algorithm', set alg_found; if not, set alg_missed.
		 */
		if (has_alg) {
			alg_found = true;
		} else {
			alg_missed = true;
		}
	}
	if (result == ISC_R_NOMORE) {
		result = ISC_R_SUCCESS;
	}

	/*
	 * Set `has_algp` if the algorithm was found in every RRset:
	 * i.e., found in at least one, and not missing from any.
	 */
	*has_algp = (alg_found && !alg_missed);
failure:
	if (dns_rdataset_isassociated(&rdataset)) {
		dns_rdataset_disassociate(&rdataset);
	}
	dns_rdatasetiter_destroy(&iterator);
	return result;
}

/*
 * Prevent the zone entering a inconsistent state where
 * NSEC only DNSKEYs are present with NSEC3 chains.
 */
bool
dns_zone_check_dnskey_nsec3(dns_zone_t *zone, dns_db_t *db,
			    dns_dbversion_t *ver, dns_diff_t *diff,
			    dst_key_t **keys, unsigned int numkeys) {
	uint8_t alg;
	dns_rdatatype_t privatetype;
	;
	bool nseconly = false, nsec3 = false;
	isc_result_t result;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(db != NULL);

	privatetype = dns_zone_getprivatetype(zone);

	/* Scan the tuples for an NSEC-only DNSKEY */
	if (diff != NULL) {
		for (dns_difftuple_t *tuple = ISC_LIST_HEAD(diff->tuples);
		     tuple != NULL; tuple = ISC_LIST_NEXT(tuple, link))
		{
			if (nseconly && nsec3) {
				break;
			}

			if (tuple->op != DNS_DIFFOP_ADD) {
				continue;
			}

			if (tuple->rdata.type == dns_rdatatype_nsec3param) {
				nsec3 = true;
			}

			if (tuple->rdata.type != dns_rdatatype_dnskey) {
				continue;
			}

			alg = tuple->rdata.data[3];
			if (alg == DNS_KEYALG_RSAMD5 || alg == DNS_KEYALG_DSA ||
			    alg == DNS_KEYALG_RSASHA1)
			{
				nseconly = true;
			}
		}
	}
	/* Scan the zone keys for an NSEC-only DNSKEY */
	if (keys != NULL && !nseconly) {
		for (unsigned int i = 0; i < numkeys; i++) {
			alg = dst_key_alg(keys[i]);
			if (alg == DNS_KEYALG_RSAMD5 || alg == DNS_KEYALG_DSA ||
			    alg == DNS_KEYALG_RSASHA1)
			{
				nseconly = true;
				break;
			}
		}
	}

	/* Check DB for NSEC-only DNSKEY */
	if (!nseconly) {
		result = dns_nsec_nseconly(db, ver, diff, &nseconly);
		/*
		 * Adding an NSEC3PARAM record can proceed without a
		 * DNSKEY (it will trigger a delayed change), so we can
		 * ignore ISC_R_NOTFOUND here.
		 */
		if (result == ISC_R_NOTFOUND) {
			result = ISC_R_SUCCESS;
		}
		CHECK(result);
	}

	/* Check existing DB for NSEC3 */
	if (!nsec3) {
		CHECK(dns_nsec3_activex(db, ver, false, privatetype, &nsec3));
	}

	/* Check kasp for NSEC3PARAM settings */
	if (!nsec3) {
		dns_kasp_t *kasp = zone->kasp;
		if (kasp != NULL) {
			nsec3 = dns_kasp_nsec3(kasp);
		}
	}

	/* Refuse to allow NSEC3 with NSEC-only keys */
	if (nseconly && nsec3) {
		goto failure;
	}

	return true;

failure:
	return false;
}

/*
 * Incrementally sign the zone using the keys requested.
 * Builds the NSEC chain if required.
 */
static void
zone_sign(dns_zone_t *zone) {
	dns_db_t *db = NULL;
	dns_dbnode_t *node = NULL;
	dns_dbversion_t *version = NULL;
	dns_diff_t _sig_diff;
	dns_diff_t post_diff;
	dns__zonediff_t zonediff;
	dns_fixedname_t fixed;
	dns_fixedname_t nextfixed;
	dns_kasp_t *kasp;
	dns_name_t *name, *nextname;
	dns_rdataset_t rdataset;
	dns_signing_t *signing, *nextsigning;
	dns_signinglist_t cleanup;
	dst_key_t *zone_keys[DNS_MAXZONEKEYS];
	int32_t signatures;
	bool is_ksk, is_zsk;
	bool with_ksk, with_zsk;
	bool commit = false;
	bool is_bottom_of_zone;
	bool build_nsec = false;
	bool build_nsec3 = false;
	bool use_kasp = false;
	bool first;
	isc_result_t result;
	isc_stdtime_t now, inception, soaexpire, expire;
	unsigned int i, j;
	unsigned int nkeys = 0;
	uint32_t nodes;

	ENTER;

	dns_rdataset_init(&rdataset);
	name = dns_fixedname_initname(&fixed);
	nextname = dns_fixedname_initname(&nextfixed);
	dns_diff_init(zone->mctx, &_sig_diff);
	dns_diff_init(zone->mctx, &post_diff);
	zonediff_init(&zonediff, &_sig_diff);
	ISC_LIST_INIT(cleanup);

	/*
	 * Updates are disabled.  Pause for 1 minute.
	 */
	if (zone->update_disabled) {
		result = ISC_R_FAILURE;
		goto cleanup;
	}

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	if (zone->db != NULL) {
		dns_db_attach(zone->db, &db);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
	if (db == NULL) {
		result = ISC_R_FAILURE;
		goto cleanup;
	}

	result = dns_db_newversion(db, &version);
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR,
			   "zone_sign:dns_db_newversion -> %s",
			   isc_result_totext(result));
		goto cleanup;
	}

	now = isc_stdtime_now();

	result = dns_zone_findkeys(zone, db, version, now, zone->mctx,
				   DNS_MAXZONEKEYS, zone_keys, &nkeys);
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR,
			   "zone_sign:dns_zone_findkeys -> %s",
			   isc_result_totext(result));
		goto cleanup;
	}

	kasp = zone->kasp;

	calculate_rrsig_validity(zone, now, &inception, &soaexpire, NULL,
				 &expire);

	/*
	 * We keep pulling nodes off each iterator in turn until
	 * we have no more nodes to pull off or we reach the limits
	 * for this quantum.
	 */
	nodes = zone->nodes;
	signatures = zone->signatures;
	signing = ISC_LIST_HEAD(zone->signing);
	first = true;

	if (kasp != NULL) {
		use_kasp = true;
	}
	dnssec_log(zone, ISC_LOG_DEBUG(3), "zone_sign:use kasp -> %s",
		   use_kasp ? "yes" : "no");

	/* Determine which type of chain to build */
	CHECK(dns_private_chains(db, version, zone->privatetype, &build_nsec,
				 &build_nsec3));
	if (!build_nsec && !build_nsec3) {
		if (use_kasp) {
			build_nsec3 = dns_kasp_nsec3(kasp);
			if (!dns_zone_check_dnskey_nsec3(
				    zone, db, version, NULL,
				    (dst_key_t **)&zone_keys, nkeys))
			{
				dnssec_log(zone, ISC_LOG_INFO,
					   "wait building NSEC3 chain until "
					   "NSEC only DNSKEYs are removed");
				build_nsec3 = false;
			}
			build_nsec = !build_nsec3;
		} else {
			/* If neither chain is found, default to NSEC */
			build_nsec = true;
		}
	}

	while (signing != NULL && nodes-- > 0 && signatures > 0) {
		bool has_alg = false;

		dns_dbiterator_pause(signing->dbiterator);
		nextsigning = ISC_LIST_NEXT(signing, link);

		ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
		if (signing->done || signing->db != zone->db) {
			/*
			 * The zone has been reloaded.	We will have to
			 * created new signings as part of the reload
			 * process so we can destroy this one.
			 */
			ISC_LIST_UNLINK(zone->signing, signing, link);
			ISC_LIST_APPEND(cleanup, signing, link);
			ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
			goto next_signing;
		}
		ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);

		if (signing->db != db) {
			goto next_signing;
		}

		is_bottom_of_zone = false;

		if (first && signing->deleteit) {
			/*
			 * Remove the key we are deleting from consideration.
			 */
			for (i = 0, j = 0; i < nkeys; i++) {
				/*
				 * Find the key we want to remove.
				 */
				if (ALG(zone_keys[i]) == signing->algorithm &&
				    dst_key_id(zone_keys[i]) == signing->keyid)
				{
					dst_key_free(&zone_keys[i]);
					continue;
				}
				zone_keys[j] = zone_keys[i];
				j++;
			}
			for (i = j; i < nkeys; i++) {
				zone_keys[i] = NULL;
			}
			nkeys = j;
		}

		dns_dbiterator_current(signing->dbiterator, &node, name);

		if (signing->deleteit) {
			dns_dbiterator_pause(signing->dbiterator);
			CHECK(del_sig(db, version, name, node, nkeys,
				      signing->algorithm, signing->keyid,
				      &has_alg, zonediff.diff));
		}

		/*
		 * On the first pass we need to check if the current node
		 * has not been obscured.
		 */
		if (first) {
			dns_fixedname_t ffound;
			dns_name_t *found;
			found = dns_fixedname_initname(&ffound);
			result = dns_db_find(
				db, name, version, dns_rdatatype_soa,
				DNS_DBFIND_NOWILD, 0, NULL, found, NULL, NULL);
			if ((result == DNS_R_DELEGATION ||
			     result == DNS_R_DNAME) &&
			    !dns_name_equal(name, found))
			{
				/*
				 * Remember the obscuring name so that
				 * we skip all obscured names.
				 */
				dns_name_copy(found, name);
				is_bottom_of_zone = true;
				goto next_node;
			}
		}

		/*
		 * Process one node.
		 */
		with_ksk = false;
		with_zsk = false;
		dns_dbiterator_pause(signing->dbiterator);

		CHECK(check_if_bottom_of_zone(db, node, version,
					      &is_bottom_of_zone));

		for (i = 0; !has_alg && i < nkeys; i++) {
			bool both = false;
			/*
			 * Find the keys we want to sign with.
			 */
			if (!dst_key_isprivate(zone_keys[i])) {
				continue;
			}
			if (dst_key_inactive(zone_keys[i])) {
				continue;
			}

			/*
			 * When adding look for the specific key.
			 */
			if (!signing->deleteit &&
			    (dst_key_alg(zone_keys[i]) != signing->algorithm ||
			     dst_key_id(zone_keys[i]) != signing->keyid))
			{
				continue;
			}

			/*
			 * When deleting make sure we are properly signed
			 * with the algorithm that was being removed.
			 */
			if (signing->deleteit &&
			    ALG(zone_keys[i]) != signing->algorithm)
			{
				continue;
			}

			/*
			 * We do KSK processing.
			 */
			if (use_kasp) {
				/*
				 * A dnssec-policy is found. Check what
				 * RRsets this key can sign.
				 */
				isc_result_t kresult;
				is_ksk = false;
				kresult = dst_key_getbool(
					zone_keys[i], DST_BOOL_KSK, &is_ksk);
				if (kresult != ISC_R_SUCCESS) {
					if (KSK(zone_keys[i])) {
						is_ksk = true;
					}
				}

				is_zsk = false;
				kresult = dst_key_getbool(
					zone_keys[i], DST_BOOL_ZSK, &is_zsk);
				if (kresult != ISC_R_SUCCESS) {
					if (!KSK(zone_keys[i])) {
						is_zsk = true;
					}
				}
				both = true;
			} else {
				is_ksk = KSK(zone_keys[i]);
				is_zsk = !is_ksk;

				/*
				 * Don't consider inactive keys, however the key
				 * may be temporary offline, so do consider KSKs
				 * which private key files are unavailable.
				 */
				both = dst_key_have_ksk_and_zsk(
					zone_keys, nkeys, i, false, is_ksk,
					is_zsk, NULL, NULL);
				if (both || REVOKE(zone_keys[i])) {
					is_ksk = KSK(zone_keys[i]);
					is_zsk = !KSK(zone_keys[i]);
				} else {
					is_ksk = false;
					is_zsk = false;
				}
			}

			/*
			 * If deleting signatures, we need to ensure that
			 * the RRset is still signed at least once by a
			 * KSK and a ZSK.
			 */
			if (signing->deleteit && is_zsk && with_zsk) {
				continue;
			}

			if (signing->deleteit && is_ksk && with_ksk) {
				continue;
			}

			CHECK(sign_a_node(
				db, zone, name, node, version, build_nsec3,
				build_nsec, zone_keys[i], now, inception,
				expire, zone_nsecttl(zone), both, is_ksk,
				is_zsk, is_bottom_of_zone, zonediff.diff,
				&signatures, zone->mctx));
			/*
			 * If we are adding we are done.  Look for other keys
			 * of the same algorithm if deleting.
			 */
			if (!signing->deleteit) {
				break;
			}
			if (is_zsk) {
				with_zsk = true;
			}
			if (is_ksk) {
				with_ksk = true;
			}
		}

		/*
		 * Go onto next node.
		 */
	next_node:
		first = false;
		dns_db_detachnode(db, &node);
		do {
			result = dns_dbiterator_next(signing->dbiterator);
			if (result == ISC_R_NOMORE) {
				ISC_LIST_UNLINK(zone->signing, signing, link);
				ISC_LIST_APPEND(cleanup, signing, link);
				dns_dbiterator_pause(signing->dbiterator);
				if (nkeys != 0 && build_nsec) {
					/*
					 * We have finished regenerating the
					 * zone with a zone signing key.
					 * The NSEC chain is now complete and
					 * there is a full set of signatures
					 * for the zone.  We can now clear the
					 * OPT bit from the NSEC record.
					 */
					result = updatesecure(
						db, version, &zone->origin,
						zone_nsecttl(zone), false,
						&post_diff);
					if (result != ISC_R_SUCCESS) {
						dnssec_log(zone, ISC_LOG_ERROR,
							   "updatesecure -> %s",
							   isc_result_totext(
								   result));
						goto cleanup;
					}
				}
				result = updatesignwithkey(
					zone, signing, version, build_nsec3,
					zone_nsecttl(zone), &post_diff);
				if (result != ISC_R_SUCCESS) {
					dnssec_log(zone, ISC_LOG_ERROR,
						   "updatesignwithkey -> %s",
						   isc_result_totext(result));
					goto cleanup;
				}
				build_nsec = false;
				goto next_signing;
			} else if (result != ISC_R_SUCCESS) {
				dnssec_log(zone, ISC_LOG_ERROR,
					   "zone_sign:"
					   "dns_dbiterator_next -> %s",
					   isc_result_totext(result));
				goto cleanup;
			} else if (is_bottom_of_zone) {
				dns_dbiterator_current(signing->dbiterator,
						       &node, nextname);
				dns_db_detachnode(db, &node);
				if (!dns_name_issubdomain(nextname, name)) {
					break;
				}
			} else {
				break;
			}
		} while (1);
		continue;

	next_signing:
		dns_dbiterator_pause(signing->dbiterator);
		signing = nextsigning;
		first = true;
	}

	if (ISC_LIST_HEAD(post_diff.tuples) != NULL) {
		result = dns__zone_updatesigs(&post_diff, db, version,
					      zone_keys, nkeys, zone, inception,
					      expire, 0, now, &zonediff);
		if (result != ISC_R_SUCCESS) {
			dnssec_log(zone, ISC_LOG_ERROR,
				   "zone_sign:dns__zone_updatesigs -> %s",
				   isc_result_totext(result));
			goto cleanup;
		}
	}

	/*
	 * Have we changed anything?
	 */
	if (ISC_LIST_EMPTY(zonediff.diff->tuples)) {
		if (zonediff.offline) {
			commit = true;
		}
		result = ISC_R_SUCCESS;
		goto pauseall;
	}

	commit = true;

	result = del_sigs(zone, db, version, &zone->origin, dns_rdatatype_soa,
			  &zonediff, zone_keys, nkeys, now, false);
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR, "zone_sign:del_sigs -> %s",
			   isc_result_totext(result));
		goto cleanup;
	}

	result = update_soa_serial(zone, db, version, zonediff.diff, zone->mctx,
				   zone->updatemethod);
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR,
			   "zone_sign:update_soa_serial -> %s",
			   isc_result_totext(result));
		goto cleanup;
	}

	/*
	 * Generate maximum life time signatures so that the above loop
	 * termination is sensible.
	 */
	result = add_sigs(db, version, &zone->origin, zone, dns_rdatatype_soa,
			  zonediff.diff, zone_keys, nkeys, zone->mctx, now,
			  inception, soaexpire);
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR, "zone_sign:add_sigs -> %s",
			   isc_result_totext(result));
		goto cleanup;
	}

	/*
	 * Write changes to journal file.
	 */
	CHECK(zone_journal(zone, zonediff.diff, NULL, "zone_sign"));

pauseall:
	/*
	 * Pause all iterators so that dns_db_closeversion() can succeed.
	 */
	for (signing = ISC_LIST_HEAD(zone->signing); signing != NULL;
	     signing = ISC_LIST_NEXT(signing, link))
	{
		dns_dbiterator_pause(signing->dbiterator);
	}

	for (signing = ISC_LIST_HEAD(cleanup); signing != NULL;
	     signing = ISC_LIST_NEXT(signing, link))
	{
		dns_dbiterator_pause(signing->dbiterator);
	}

	/*
	 * Everything has succeeded. Commit the changes.
	 */
	dns_db_closeversion(db, &version, commit);

	/*
	 * Everything succeeded so we can clean these up now.
	 */
	signing = ISC_LIST_HEAD(cleanup);
	while (signing != NULL) {
		ISC_LIST_UNLINK(cleanup, signing, link);
		dns_db_detach(&signing->db);
		dns_dbiterator_destroy(&signing->dbiterator);
		isc_mem_put(zone->mctx, signing, sizeof *signing);
		signing = ISC_LIST_HEAD(cleanup);
	}

	LOCK_ZONE(zone);
	set_resigntime(zone);
	if (commit) {
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NEEDNOTIFY);
		zone_needdump(zone, DNS_DUMP_DELAY);
	}
	UNLOCK_ZONE(zone);

failure:
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR, "zone_sign: failed: %s",
			   isc_result_totext(result));
	}

cleanup:
	/*
	 * Pause all dbiterators.
	 */
	for (signing = ISC_LIST_HEAD(zone->signing); signing != NULL;
	     signing = ISC_LIST_NEXT(signing, link))
	{
		dns_dbiterator_pause(signing->dbiterator);
	}

	/*
	 * Rollback the cleanup list.
	 */
	signing = ISC_LIST_HEAD(cleanup);
	while (signing != NULL) {
		ISC_LIST_UNLINK(cleanup, signing, link);
		ISC_LIST_PREPEND(zone->signing, signing, link);
		dns_dbiterator_first(signing->dbiterator);
		dns_dbiterator_pause(signing->dbiterator);
		signing = ISC_LIST_HEAD(cleanup);
	}

	dns_diff_clear(&_sig_diff);
	dns_diff_clear(&post_diff);

	for (i = 0; i < nkeys; i++) {
		dst_key_free(&zone_keys[i]);
	}

	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}

	if (version != NULL) {
		dns_db_closeversion(db, &version, false);
		dns_db_detach(&db);
	} else if (db != NULL) {
		dns_db_detach(&db);
	}

	LOCK_ZONE(zone);
	if (ISC_LIST_HEAD(zone->signing) != NULL) {
		isc_interval_t interval;
		if (zone->update_disabled || result != ISC_R_SUCCESS) {
			isc_interval_set(&interval, 60, 0); /* 1 minute */
		} else {
			isc_interval_set(&interval, 0, 10000000); /* 10 ms */
		}
		isc_time_nowplusinterval(&zone->signingtime, &interval);
	} else {
		isc_time_settoepoch(&zone->signingtime);
	}
	UNLOCK_ZONE(zone);

	INSIST(version == NULL);
}

static isc_result_t
normalize_key(dns_rdata_t *rr, dns_rdata_t *target, unsigned char *data,
	      int size) {
	dns_rdata_dnskey_t dnskey;
	dns_rdata_keydata_t keydata;
	isc_buffer_t buf;
	isc_result_t result;

	dns_rdata_reset(target);
	isc_buffer_init(&buf, data, size);

	switch (rr->type) {
	case dns_rdatatype_dnskey:
		result = dns_rdata_tostruct(rr, &dnskey, NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
		dnskey.flags &= ~DNS_KEYFLAG_REVOKE;
		dns_rdata_fromstruct(target, rr->rdclass, dns_rdatatype_dnskey,
				     &dnskey, &buf);
		break;
	case dns_rdatatype_keydata:
		result = dns_rdata_tostruct(rr, &keydata, NULL);
		if (result == ISC_R_UNEXPECTEDEND) {
			return result;
		}
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
		dns_keydata_todnskey(&keydata, &dnskey, NULL);
		dns_rdata_fromstruct(target, rr->rdclass, dns_rdatatype_dnskey,
				     &dnskey, &buf);
		break;
	default:
		UNREACHABLE();
	}
	return ISC_R_SUCCESS;
}

/*
 * 'rdset' contains either a DNSKEY rdataset from the zone apex, or
 * a KEYDATA rdataset from the key zone.
 *
 * 'rr' contains either a DNSKEY record, or a KEYDATA record
 *
 * After normalizing keys to the same format (DNSKEY, with revoke bit
 * cleared), return true if a key that matches 'rr' is found in
 * 'rdset', or false if not.
 */

static bool
matchkey(dns_rdataset_t *rdset, dns_rdata_t *rr) {
	unsigned char data1[4096], data2[4096];
	dns_rdata_t rdata, rdata1, rdata2;
	isc_result_t result;

	dns_rdata_init(&rdata);
	dns_rdata_init(&rdata1);
	dns_rdata_init(&rdata2);

	result = normalize_key(rr, &rdata1, data1, sizeof(data1));
	if (result != ISC_R_SUCCESS) {
		return false;
	}

	for (result = dns_rdataset_first(rdset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(rdset))
	{
		dns_rdata_reset(&rdata);
		dns_rdataset_current(rdset, &rdata);
		result = normalize_key(&rdata, &rdata2, data2, sizeof(data2));
		if (result != ISC_R_SUCCESS) {
			continue;
		}
		if (dns_rdata_compare(&rdata1, &rdata2) == 0) {
			return true;
		}
	}

	return false;
}

/*
 * Calculate the refresh interval for a keydata zone, per
 * RFC5011: MAX(1 hr,
 *		MIN(15 days,
 *		    1/2 * OrigTTL,
 *		    1/2 * RRSigExpirationInterval))
 * or for retries: MAX(1 hr,
 *		       MIN(1 day,
 *			   1/10 * OrigTTL,
 *			   1/10 * RRSigExpirationInterval))
 */
static isc_stdtime_t
refresh_time(dns_keyfetch_t *kfetch, bool retry) {
	isc_result_t result;
	uint32_t t;
	dns_rdataset_t *rdset;
	dns_rdata_t sigrr = DNS_RDATA_INIT;
	dns_rdata_sig_t sig;
	isc_stdtime_t now = isc_stdtime_now();

	if (dns_rdataset_isassociated(&kfetch->dnskeysigset)) {
		rdset = &kfetch->dnskeysigset;
	} else {
		return now + dns_zone_mkey_hour;
	}

	result = dns_rdataset_first(rdset);
	if (result != ISC_R_SUCCESS) {
		return now + dns_zone_mkey_hour;
	}

	dns_rdataset_current(rdset, &sigrr);
	result = dns_rdata_tostruct(&sigrr, &sig, NULL);
	RUNTIME_CHECK(result == ISC_R_SUCCESS);

	if (!retry) {
		t = sig.originalttl / 2;

		if (isc_serial_gt(sig.timeexpire, now)) {
			uint32_t exp = (sig.timeexpire - now) / 2;
			if (t > exp) {
				t = exp;
			}
		}

		if (t > (15 * dns_zone_mkey_day)) {
			t = (15 * dns_zone_mkey_day);
		}

		if (t < dns_zone_mkey_hour) {
			t = dns_zone_mkey_hour;
		}
	} else {
		t = sig.originalttl / 10;

		if (isc_serial_gt(sig.timeexpire, now)) {
			uint32_t exp = (sig.timeexpire - now) / 10;
			if (t > exp) {
				t = exp;
			}
		}

		if (t > dns_zone_mkey_day) {
			t = dns_zone_mkey_day;
		}

		if (t < dns_zone_mkey_hour) {
			t = dns_zone_mkey_hour;
		}
	}

	return now + t;
}

/*
 * This routine is called when no changes are needed in a KEYDATA
 * record except to simply update the refresh timer.  Caller should
 * hold zone lock.
 */
static isc_result_t
minimal_update(dns_keyfetch_t *kfetch, dns_dbversion_t *ver, dns_diff_t *diff) {
	isc_result_t result;
	isc_buffer_t keyb;
	unsigned char key_buf[4096];
	dns_rdata_t rdata = DNS_RDATA_INIT;
	dns_rdata_keydata_t keydata;
	dns_name_t *name;
	dns_zone_t *zone = kfetch->zone;
	isc_stdtime_t now = isc_stdtime_now();

	name = dns_fixedname_name(&kfetch->name);

	for (result = dns_rdataset_first(&kfetch->keydataset);
	     result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&kfetch->keydataset))
	{
		dns_rdata_reset(&rdata);
		dns_rdataset_current(&kfetch->keydataset, &rdata);

		/* Delete old version */
		CHECK(update_one_rr(kfetch->db, ver, diff, DNS_DIFFOP_DEL, name,
				    0, &rdata));

		/* Update refresh timer */
		result = dns_rdata_tostruct(&rdata, &keydata, NULL);
		if (result == ISC_R_UNEXPECTEDEND) {
			continue;
		}
		if (result != ISC_R_SUCCESS) {
			goto failure;
		}
		keydata.refresh = refresh_time(kfetch, true);
		set_refreshkeytimer(zone, &keydata, now, false);

		dns_rdata_reset(&rdata);
		isc_buffer_init(&keyb, key_buf, sizeof(key_buf));
		CHECK(dns_rdata_fromstruct(&rdata, zone->rdclass,
					   dns_rdatatype_keydata, &keydata,
					   &keyb));

		/* Insert updated version */
		CHECK(update_one_rr(kfetch->db, ver, diff, DNS_DIFFOP_ADD, name,
				    0, &rdata));
	}
	result = ISC_R_SUCCESS;
failure:
	return result;
}

/*
 * Verify that DNSKEY set is signed by the key specified in 'keydata'.
 */
static bool
revocable(dns_keyfetch_t *kfetch, dns_rdata_keydata_t *keydata) {
	isc_result_t result;
	dns_name_t *keyname;
	isc_mem_t *mctx;
	dns_rdata_t sigrr = DNS_RDATA_INIT;
	dns_rdata_t rr = DNS_RDATA_INIT;
	dns_rdata_rrsig_t sig;
	dns_rdata_dnskey_t dnskey;
	dst_key_t *dstkey = NULL;
	unsigned char key_buf[4096];
	isc_buffer_t keyb;
	bool answer = false;

	REQUIRE(kfetch != NULL && keydata != NULL);
	REQUIRE(dns_rdataset_isassociated(&kfetch->dnskeysigset));

	keyname = dns_fixedname_name(&kfetch->name);
	mctx = kfetch->zone->view->mctx;

	/* Generate a key from keydata */
	isc_buffer_init(&keyb, key_buf, sizeof(key_buf));
	dns_keydata_todnskey(keydata, &dnskey, NULL);
	dns_rdata_fromstruct(&rr, keydata->common.rdclass, dns_rdatatype_dnskey,
			     &dnskey, &keyb);
	result = dns_dnssec_keyfromrdata(keyname, &rr, mctx, &dstkey);
	if (result != ISC_R_SUCCESS) {
		return false;
	}

	/* See if that key generated any of the signatures */
	for (result = dns_rdataset_first(&kfetch->dnskeysigset);
	     result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&kfetch->dnskeysigset))
	{
		dns_fixedname_t fixed;
		dns_fixedname_init(&fixed);

		dns_rdata_reset(&sigrr);
		dns_rdataset_current(&kfetch->dnskeysigset, &sigrr);
		result = dns_rdata_tostruct(&sigrr, &sig, NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);

		if (dst_key_alg(dstkey) == sig.algorithm &&
		    dst_key_rid(dstkey) == sig.keyid)
		{
			result = dns_dnssec_verify(
				keyname, &kfetch->dnskeyset, dstkey, false, 0,
				mctx, &sigrr, dns_fixedname_name(&fixed));

			dnssec_log(kfetch->zone, ISC_LOG_DEBUG(3),
				   "Confirm revoked DNSKEY is self-signed: %s",
				   isc_result_totext(result));

			if (result == ISC_R_SUCCESS) {
				answer = true;
				break;
			}
		}
	}

	dst_key_free(&dstkey);
	return answer;
}

/*
 * A DNSKEY set has been fetched from the zone apex of a zone whose trust
 * anchors are being managed; scan the keyset, and update the key zone and the
 * local trust anchors according to RFC5011.
 */
static void
keyfetch_done(void *arg) {
	dns_fetchresponse_t *resp = (dns_fetchresponse_t *)arg;
	isc_result_t result, eresult;
	dns_keyfetch_t *kfetch = NULL;
	dns_zone_t *zone = NULL;
	isc_mem_t *mctx = NULL;
	dns_keytable_t *secroots = NULL;
	dns_dbversion_t *ver = NULL;
	dns_diff_t diff;
	bool alldone = false;
	bool commit = false;
	dns_name_t *keyname = NULL;
	dns_rdata_t sigrr = DNS_RDATA_INIT;
	dns_rdata_t dnskeyrr = DNS_RDATA_INIT;
	dns_rdata_t keydatarr = DNS_RDATA_INIT;
	dns_rdata_rrsig_t sig;
	dns_rdata_dnskey_t dnskey;
	dns_rdata_keydata_t keydata;
	bool initializing;
	char namebuf[DNS_NAME_FORMATSIZE];
	unsigned char key_buf[4096];
	isc_buffer_t keyb;
	dst_key_t *dstkey = NULL;
	isc_stdtime_t now;
	int pending = 0;
	bool secure = false, initial = false;
	bool free_needed;
	dns_keynode_t *keynode = NULL;
	dns_rdataset_t *dnskeys = NULL, *dnskeysigs = NULL;
	dns_rdataset_t *keydataset = NULL, dsset;

	INSIST(resp != NULL);

	kfetch = resp->arg;

	INSIST(kfetch != NULL);

	zone = kfetch->zone;
	mctx = kfetch->mctx;
	keyname = dns_fixedname_name(&kfetch->name);
	dnskeys = &kfetch->dnskeyset;
	dnskeysigs = &kfetch->dnskeysigset;
	keydataset = &kfetch->keydataset;

	eresult = resp->result;

	/* Free resources which are not of interest */
	if (resp->node != NULL) {
		dns_db_detachnode(resp->db, &resp->node);
	}
	if (resp->db != NULL) {
		dns_db_detach(&resp->db);
	}

	dns_resolver_destroyfetch(&kfetch->fetch);

	LOCK_ZONE(zone);
	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING) || zone->view == NULL) {
		goto cleanup;
	}

	now = isc_stdtime_now();
	dns_name_format(keyname, namebuf, sizeof(namebuf));

	result = dns_view_getsecroots(zone->view, &secroots);
	INSIST(result == ISC_R_SUCCESS);

	dns_diff_init(mctx, &diff);

	CHECK(dns_db_newversion(kfetch->db, &ver));

	zone->refreshkeycount--;
	alldone = (zone->refreshkeycount == 0);

	if (alldone) {
		DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_REFRESHING);
	}

	dnssec_log(zone, ISC_LOG_DEBUG(3),
		   "Returned from key fetch in keyfetch_done() for '%s': %s",
		   namebuf, isc_result_totext(eresult));

	/* Fetch failed */
	if (eresult != ISC_R_SUCCESS || !dns_rdataset_isassociated(dnskeys)) {
		dnssec_log(zone, ISC_LOG_WARNING,
			   "Unable to fetch DNSKEY set '%s': %s", namebuf,
			   isc_result_totext(eresult));
		CHECK(minimal_update(kfetch, ver, &diff));
		goto done;
	}

	/* No RRSIGs found */
	if (!dns_rdataset_isassociated(dnskeysigs)) {
		dnssec_log(zone, ISC_LOG_WARNING,
			   "No DNSKEY RRSIGs found for '%s': %s", namebuf,
			   isc_result_totext(eresult));
		CHECK(minimal_update(kfetch, ver, &diff));
		goto done;
	}

	/*
	 * Clear any cached trust level, as we need to run validation
	 * over again; trusted keys might have changed.
	 */
	dnskeys->trust = dnskeysigs->trust = dns_trust_none;

	/* Look up the trust anchor */
	result = dns_keytable_find(secroots, keyname, &keynode);
	if (result != ISC_R_SUCCESS) {
		goto anchors_done;
	}

	/*
	 * If the keynode has a DS trust anchor, use it for verification.
	 */
	dns_rdataset_init(&dsset);
	if (dns_keynode_dsset(keynode, &dsset)) {
		for (result = dns_rdataset_first(dnskeysigs);
		     result == ISC_R_SUCCESS;
		     result = dns_rdataset_next(dnskeysigs))
		{
			isc_result_t tresult;
			dns_rdata_t keyrdata = DNS_RDATA_INIT;

			dns_rdata_reset(&sigrr);
			dns_rdataset_current(dnskeysigs, &sigrr);
			result = dns_rdata_tostruct(&sigrr, &sig, NULL);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);

			for (tresult = dns_rdataset_first(&dsset);
			     tresult == ISC_R_SUCCESS;
			     tresult = dns_rdataset_next(&dsset))
			{
				dns_rdata_t dsrdata = DNS_RDATA_INIT;
				dns_rdata_ds_t ds;

				dns_rdata_reset(&dsrdata);
				dns_rdataset_current(&dsset, &dsrdata);
				tresult = dns_rdata_tostruct(&dsrdata, &ds,
							     NULL);
				RUNTIME_CHECK(tresult == ISC_R_SUCCESS);

				if (ds.key_tag != sig.keyid ||
				    ds.algorithm != sig.algorithm)
				{
					continue;
				}

				result = dns_dnssec_matchdskey(
					keyname, &dsrdata, dnskeys, &keyrdata);
				if (result == ISC_R_SUCCESS) {
					break;
				}
			}

			if (tresult == ISC_R_NOMORE) {
				continue;
			}

			result = dns_dnssec_keyfromrdata(keyname, &keyrdata,
							 mctx, &dstkey);
			if (result != ISC_R_SUCCESS) {
				continue;
			}

			result = dns_dnssec_verify(keyname, dnskeys, dstkey,
						   false, 0, mctx, &sigrr,
						   NULL);
			dst_key_free(&dstkey);

			dnssec_log(zone, ISC_LOG_DEBUG(3),
				   "Verifying DNSKEY set for zone "
				   "'%s' using DS %d/%d: %s",
				   namebuf, sig.keyid, sig.algorithm,
				   isc_result_totext(result));

			if (result == ISC_R_SUCCESS) {
				dnskeys->trust = dns_trust_secure;
				dnskeysigs->trust = dns_trust_secure;
				initial = dns_keynode_initial(keynode);
				dns_keynode_trust(keynode);
				secure = true;
				break;
			}
		}
		dns_rdataset_disassociate(&dsset);
	}

anchors_done:
	if (keynode != NULL) {
		dns_keynode_detach(&keynode);
	}

	/*
	 * If we were not able to verify the answer using the current
	 * trusted keys then all we can do is look at any revoked keys.
	 */
	if (!secure) {
		dnssec_log(zone, ISC_LOG_INFO,
			   "DNSKEY set for zone '%s' could not be verified "
			   "with current keys",
			   namebuf);
	}

	/*
	 * First scan keydataset to find keys that are not in dnskeyset
	 *   - Missing keys which are not scheduled for removal,
	 *     log a warning
	 *   - Missing keys which are scheduled for removal and
	 *     the remove hold-down timer has completed should
	 *     be removed from the key zone
	 *   - Missing keys whose acceptance timers have not yet
	 *     completed, log a warning and reset the acceptance
	 *     timer to 30 days in the future
	 *   - All keys not being removed have their refresh timers
	 *     updated
	 */
	initializing = true;
	for (result = dns_rdataset_first(keydataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(keydataset))
	{
		dns_keytag_t keytag;

		dns_rdata_reset(&keydatarr);
		dns_rdataset_current(keydataset, &keydatarr);
		result = dns_rdata_tostruct(&keydatarr, &keydata, NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);

		dns_keydata_todnskey(&keydata, &dnskey, NULL);
		result = compute_tag(keyname, &dnskey, mctx, &keytag);
		if (result != ISC_R_SUCCESS) {
			/*
			 * Skip if we cannot compute the key tag.
			 * This may happen if the algorithm is unsupported
			 */
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "Cannot compute tag for key in zone %s: "
				     "%s "
				     "(skipping)",
				     namebuf, isc_result_totext(result));
			continue;
		}
		RUNTIME_CHECK(result == ISC_R_SUCCESS);

		/*
		 * If any keydata record has a nonzero add holddown, then
		 * there was a pre-existing trust anchor for this domain;
		 * that means we are *not* initializing it and shouldn't
		 * automatically trust all the keys we find at the zone apex.
		 */
		initializing = initializing && (keydata.addhd == 0);

		if (!matchkey(dnskeys, &keydatarr)) {
			bool deletekey = false;

			if (!secure) {
				if (keydata.removehd != 0 &&
				    keydata.removehd <= now)
				{
					deletekey = true;
				}
			} else if (keydata.addhd == 0) {
				deletekey = true;
			} else if (keydata.addhd > now) {
				dnssec_log(zone, ISC_LOG_INFO,
					   "Pending key %d for zone %s "
					   "unexpectedly missing from DNSKEY "
					   "RRset: restarting 30-day "
					   "acceptance timer",
					   keytag, namebuf);
				if (keydata.addhd < now + dns_zone_mkey_month) {
					keydata.addhd = now +
							dns_zone_mkey_month;
				}
				keydata.refresh = refresh_time(kfetch, false);
			} else if (keydata.removehd == 0) {
				dnssec_log(zone, ISC_LOG_INFO,
					   "Active key %d for zone %s "
					   "unexpectedly missing from DNSKEY "
					   "RRset",
					   keytag, namebuf);
				keydata.refresh = now + dns_zone_mkey_hour;
			} else if (keydata.removehd <= now) {
				deletekey = true;
				dnssec_log(
					zone, ISC_LOG_INFO,
					"Revoked key %d for zone %s no longer "
					"present in DNSKEY RRset: deleting "
					"from managed keys database",
					keytag, namebuf);
			} else {
				keydata.refresh = refresh_time(kfetch, false);
			}

			if (secure || deletekey) {
				/* Delete old version */
				CHECK(update_one_rr(kfetch->db, ver, &diff,
						    DNS_DIFFOP_DEL, keyname, 0,
						    &keydatarr));
			}

			if (!secure || deletekey) {
				continue;
			}

			dns_rdata_reset(&keydatarr);
			isc_buffer_init(&keyb, key_buf, sizeof(key_buf));
			dns_rdata_fromstruct(&keydatarr, zone->rdclass,
					     dns_rdatatype_keydata, &keydata,
					     &keyb);

			/* Insert updated version */
			CHECK(update_one_rr(kfetch->db, ver, &diff,
					    DNS_DIFFOP_ADD, keyname, 0,
					    &keydatarr));

			set_refreshkeytimer(zone, &keydata, now, false);
		}
	}

	/*
	 * Next scan dnskeyset:
	 *   - If new keys are found (i.e., lacking a match in keydataset)
	 *     add them to the key zone and set the acceptance timer
	 *     to 30 days in the future (or to immediately if we've
	 *     determined that we're initializing the zone for the
	 *     first time)
	 *   - Previously-known keys that have been revoked
	 *     must be scheduled for removal from the key zone (or,
	 *     if they hadn't been accepted as trust anchors yet
	 *     anyway, removed at once)
	 *   - Previously-known unrevoked keys whose acceptance timers
	 *     have completed are promoted to trust anchors
	 *   - All keys not being removed have their refresh
	 *     timers updated
	 */
	for (result = dns_rdataset_first(dnskeys); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(dnskeys))
	{
		bool revoked = false;
		bool newkey = false;
		bool updatekey = false;
		bool deletekey = false;
		bool trustkey = false;
		dns_keytag_t keytag;

		dns_rdata_reset(&dnskeyrr);
		dns_rdataset_current(dnskeys, &dnskeyrr);
		result = dns_rdata_tostruct(&dnskeyrr, &dnskey, NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);

		/* Skip ZSK's */
		if ((dnskey.flags & DNS_KEYFLAG_KSK) == 0) {
			continue;
		}

		result = compute_tag(keyname, &dnskey, mctx, &keytag);
		if (result != ISC_R_SUCCESS) {
			/*
			 * Skip if we cannot compute the key tag.
			 * This may happen if the algorithm is unsupported
			 */
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "Cannot compute tag for key in zone %s: "
				     "%s "
				     "(skipping)",
				     namebuf, isc_result_totext(result));
			continue;
		}
		RUNTIME_CHECK(result == ISC_R_SUCCESS);

		revoked = ((dnskey.flags & DNS_KEYFLAG_REVOKE) != 0);

		if (matchkey(keydataset, &dnskeyrr)) {
			dns_rdata_reset(&keydatarr);
			dns_rdataset_current(keydataset, &keydatarr);
			result = dns_rdata_tostruct(&keydatarr, &keydata, NULL);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);

			if (revoked && revocable(kfetch, &keydata)) {
				if (keydata.addhd > now) {
					/*
					 * Key wasn't trusted yet, and now
					 * it's been revoked?  Just remove it
					 */
					deletekey = true;
					dnssec_log(zone, ISC_LOG_INFO,
						   "Pending key %d for "
						   "zone %s is now revoked: "
						   "deleting from the "
						   "managed keys database",
						   keytag, namebuf);
				} else if (keydata.removehd == 0) {
					/*
					 * Remove key from secroots.
					 */
					dns_view_untrust(zone->view, keyname,
							 &dnskey);

					/* If initializing, delete now */
					if (keydata.addhd == 0) {
						deletekey = true;
					} else {
						keydata.removehd =
							now +
							dns_zone_mkey_month;
						keydata.flags |=
							DNS_KEYFLAG_REVOKE;
					}

					dnssec_log(zone, ISC_LOG_INFO,
						   "Trusted key %d for "
						   "zone %s is now revoked",
						   keytag, namebuf);
				} else if (keydata.removehd < now) {
					/* Scheduled for removal */
					deletekey = true;

					dnssec_log(zone, ISC_LOG_INFO,
						   "Revoked key %d for "
						   "zone %s removal timer "
						   "complete: deleting from "
						   "the managed keys database",
						   keytag, namebuf);
				}
			} else if (revoked && keydata.removehd == 0) {
				dnssec_log(zone, ISC_LOG_WARNING,
					   "Active key %d for zone "
					   "%s is revoked but "
					   "did not self-sign; "
					   "ignoring",
					   keytag, namebuf);
				continue;
			} else if (secure) {
				if (keydata.removehd != 0) {
					/*
					 * Key isn't revoked--but it
					 * seems it used to be.
					 * Remove it now and add it
					 * back as if it were a fresh key,
					 * with a 30-day acceptance timer.
					 */
					deletekey = true;
					newkey = true;
					keydata.removehd = 0;
					keydata.addhd = now +
							dns_zone_mkey_month;

					dnssec_log(zone, ISC_LOG_INFO,
						   "Revoked key %d for "
						   "zone %s has returned: "
						   "starting 30-day "
						   "acceptance timer",
						   keytag, namebuf);
				} else if (keydata.addhd > now) {
					pending++;
				} else if (keydata.addhd == 0) {
					keydata.addhd = now;
				}

				if (keydata.addhd <= now) {
					trustkey = true;
					dnssec_log(zone, ISC_LOG_INFO,
						   "Key %d for zone %s "
						   "is now trusted (%s)",
						   keytag, namebuf,
						   initial ? "initializing key "
							     "verified"
							   : "acceptance timer "
							     "complete");
				}
			} else if (keydata.addhd > now) {
				/*
				 * Not secure, and key is pending:
				 * reset the acceptance timer
				 */
				pending++;
				keydata.addhd = now + dns_zone_mkey_month;
				dnssec_log(zone, ISC_LOG_INFO,
					   "Pending key %d "
					   "for zone %s was "
					   "not validated: restarting "
					   "30-day acceptance timer",
					   keytag, namebuf);
			}

			if (!deletekey && !newkey) {
				updatekey = true;
			}
		} else if (secure) {
			/*
			 * Key wasn't in the key zone but it's
			 * revoked now anyway, so just skip it
			 */
			if (revoked) {
				continue;
			}

			/* Key wasn't in the key zone: add it */
			newkey = true;

			if (initializing) {
				dnssec_log(zone, ISC_LOG_WARNING,
					   "Initializing automatic trust "
					   "anchor management for zone '%s'; "
					   "DNSKEY ID %d is now trusted, "
					   "waiving the normal 30-day "
					   "waiting period.",
					   namebuf, keytag);
				trustkey = true;
			} else {
				dnssec_log(zone, ISC_LOG_INFO,
					   "New key %d observed "
					   "for zone '%s': "
					   "starting 30-day "
					   "acceptance timer",
					   keytag, namebuf);
			}
		} else {
			/*
			 * No previously known key, and the key is not
			 * secure, so skip it.
			 */
			continue;
		}

		/* Delete old version */
		if (deletekey || !newkey) {
			CHECK(update_one_rr(kfetch->db, ver, &diff,
					    DNS_DIFFOP_DEL, keyname, 0,
					    &keydatarr));
		}

		if (updatekey) {
			/* Set refresh timer */
			keydata.refresh = refresh_time(kfetch, false);
			dns_rdata_reset(&keydatarr);
			isc_buffer_init(&keyb, key_buf, sizeof(key_buf));
			dns_rdata_fromstruct(&keydatarr, zone->rdclass,
					     dns_rdatatype_keydata, &keydata,
					     &keyb);

			/* Insert updated version */
			CHECK(update_one_rr(kfetch->db, ver, &diff,
					    DNS_DIFFOP_ADD, keyname, 0,
					    &keydatarr));
		} else if (newkey) {
			/* Convert DNSKEY to KEYDATA */
			result = dns_rdata_tostruct(&dnskeyrr, &dnskey, NULL);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);
			dns_keydata_fromdnskey(&keydata, &dnskey, 0, 0, 0,
					       NULL);
			keydata.addhd = initializing
						? now
						: now + dns_zone_mkey_month;
			keydata.refresh = refresh_time(kfetch, false);
			dns_rdata_reset(&keydatarr);
			isc_buffer_init(&keyb, key_buf, sizeof(key_buf));
			dns_rdata_fromstruct(&keydatarr, zone->rdclass,
					     dns_rdatatype_keydata, &keydata,
					     &keyb);

			/* Insert into key zone */
			CHECK(update_one_rr(kfetch->db, ver, &diff,
					    DNS_DIFFOP_ADD, keyname, 0,
					    &keydatarr));
		}

		if (trustkey) {
			/* Trust this key. */
			result = dns_rdata_tostruct(&dnskeyrr, &dnskey, NULL);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);
			trust_key(zone, keyname, &dnskey, false);
		}

		if (secure && !deletekey) {
			INSIST(newkey || updatekey);
			set_refreshkeytimer(zone, &keydata, now, false);
		}
	}

	/*
	 * RFC5011 says, "A trust point that has all of its trust anchors
	 * revoked is considered deleted and is treated as if the trust
	 * point was never configured."  But if someone revoked their
	 * active key before the standby was trusted, that would mean the
	 * zone would suddenly be nonsecured.  We avoid this by checking to
	 * see if there's pending keydata.  If so, we put a null key in
	 * the security roots; then all queries to the zone will fail.
	 */
	if (pending != 0) {
		fail_secure(zone, keyname);
	}

done:
	if (!ISC_LIST_EMPTY(diff.tuples)) {
		/* Write changes to journal file. */
		CHECK(update_soa_serial(zone, kfetch->db, ver, &diff, mctx,
					zone->updatemethod));
		CHECK(zone_journal(zone, &diff, NULL, "keyfetch_done"));
		commit = true;

		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_LOADED);
		zone_needdump(zone, 30);
	} else if (result == ISC_R_NOMORE) {
		/*
		 * If "updatekey" was true for all keys found in the DNSKEY
		 * response and the previous update of those keys happened
		 * during the same second (only possible if a key refresh was
		 * externally triggered), it may happen that all relevant
		 * update_one_rr() calls will return ISC_R_SUCCESS, but
		 * diff.tuples will remain empty.  Reset result to
		 * ISC_R_SUCCESS to prevent a bogus warning from being logged.
		 */
		result = ISC_R_SUCCESS;
	}

failure:
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR,
			   "error during managed-keys processing (%s): "
			   "DNSSEC validation may be at risk",
			   isc_result_totext(result));
	}
	dns_diff_clear(&diff);
	if (ver != NULL) {
		dns_db_closeversion(kfetch->db, &ver, commit);
	}

cleanup:
	dns_db_detach(&kfetch->db);

	isc_refcount_decrement(&zone->irefs);

	if (dns_rdataset_isassociated(keydataset)) {
		dns_rdataset_disassociate(keydataset);
	}
	if (dns_rdataset_isassociated(dnskeys)) {
		dns_rdataset_disassociate(dnskeys);
	}
	if (dns_rdataset_isassociated(dnskeysigs)) {
		dns_rdataset_disassociate(dnskeysigs);
	}

	dns_resolver_freefresp(&resp);
	dns_name_free(keyname, mctx);
	isc_mem_putanddetach(&kfetch->mctx, kfetch, sizeof(dns_keyfetch_t));

	if (secroots != NULL) {
		dns_keytable_detach(&secroots);
	}

	free_needed = exit_check(zone);
	UNLOCK_ZONE(zone);

	if (free_needed) {
		zone_free(zone);
	}

	INSIST(ver == NULL);
}

static void
retry_keyfetch(dns_keyfetch_t *kfetch, dns_name_t *kname) {
	isc_time_t timenow, timethen;
	dns_zone_t *zone = kfetch->zone;
	bool free_needed;
	char namebuf[DNS_NAME_FORMATSIZE];

	dns_name_format(kname, namebuf, sizeof(namebuf));
	dnssec_log(zone, ISC_LOG_WARNING,
		   "Failed to create fetch for %s DNSKEY update", namebuf);

	/*
	 * Error during a key fetch; cancel and retry in an hour.
	 */
	LOCK_ZONE(zone);
	zone->refreshkeycount--;
	isc_refcount_decrement(&zone->irefs);
	dns_db_detach(&kfetch->db);
	dns_rdataset_disassociate(&kfetch->keydataset);
	dns_name_free(kname, zone->mctx);
	isc_mem_putanddetach(&kfetch->mctx, kfetch, sizeof(*kfetch));

	if (!DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING)) {
		/* Don't really retry if we are exiting */
		char timebuf[80];

		timenow = isc_time_now();
		DNS_ZONE_TIME_ADD(&timenow, dns_zone_mkey_hour, &timethen);
		zone->refreshkeytime = timethen;
		zone_settimer(zone, &timenow);

		isc_time_formattimestamp(&zone->refreshkeytime, timebuf, 80);
		dnssec_log(zone, ISC_LOG_DEBUG(1), "retry key refresh: %s",
			   timebuf);
	}

	free_needed = exit_check(zone);
	UNLOCK_ZONE(zone);

	if (free_needed) {
		zone_free(zone);
	}
}

static void
do_keyfetch(void *arg) {
	isc_result_t result;
	dns_keyfetch_t *kfetch = (dns_keyfetch_t *)arg;
	dns_name_t *kname = dns_fixedname_name(&kfetch->name);
	dns_resolver_t *resolver = NULL;
	dns_zone_t *zone = kfetch->zone;
	unsigned int options = DNS_FETCHOPT_NOVALIDATE | DNS_FETCHOPT_UNSHARED |
			       DNS_FETCHOPT_NOCACHED;

	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING)) {
		goto retry;
	}

	result = dns_view_getresolver(zone->view, &resolver);
	if (result != ISC_R_SUCCESS) {
		goto retry;
	}

	/*
	 * Use of DNS_FETCHOPT_NOCACHED is essential here.  If it is not
	 * set and the cache still holds a non-expired, validated version
	 * of the RRset being queried for by the time the response is
	 * received, the cached RRset will be passed to keyfetch_done()
	 * instead of the one received in the response as the latter will
	 * have a lower trust level due to not being validated until
	 * keyfetch_done() is called.
	 */
	result = dns_resolver_createfetch(
		resolver, kname, dns_rdatatype_dnskey, NULL, NULL, NULL, NULL,
		0, options, 0, NULL, NULL, zone->loop, keyfetch_done, kfetch,
		NULL, &kfetch->dnskeyset, &kfetch->dnskeysigset,
		&kfetch->fetch);

	dns_resolver_detach(&resolver);
	if (result == ISC_R_SUCCESS) {
		return;
	}
retry:
	retry_keyfetch(kfetch, kname);
}

/*
 * Refresh the data in the key zone.  Initiate a fetch to look up
 * DNSKEY records at the trust anchor name.
 */
static void
zone_refreshkeys(dns_zone_t *zone) {
	isc_result_t result;
	dns_rriterator_t rrit;
	dns_db_t *db = NULL;
	dns_dbversion_t *ver = NULL;
	dns_diff_t diff;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	dns_rdata_keydata_t kd;
	isc_stdtime_t now = isc_stdtime_now();
	bool commit = false;
	bool fetching = false;
	bool timerset = false;

	ENTER;
	REQUIRE(zone->db != NULL);

	LOCK_ZONE(zone);
	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING)) {
		isc_time_settoepoch(&zone->refreshkeytime);
		UNLOCK_ZONE(zone);
		return;
	}

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	dns_db_attach(zone->db, &db);
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);

	dns_diff_init(zone->mctx, &diff);

	CHECK(dns_db_newversion(db, &ver));

	DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_REFRESHING);

	dns_rriterator_init(&rrit, db, ver, 0);
	for (result = dns_rriterator_first(&rrit); result == ISC_R_SUCCESS;
	     result = dns_rriterator_nextrrset(&rrit))
	{
		isc_stdtime_t timer = 0xffffffff;
		dns_name_t *name = NULL, *kname = NULL;
		dns_rdataset_t *kdset = NULL;
		uint32_t ttl;

		dns_rriterator_current(&rrit, &name, &ttl, &kdset, NULL);
		if (kdset == NULL || kdset->type != dns_rdatatype_keydata ||
		    !dns_rdataset_isassociated(kdset))
		{
			continue;
		}

		/*
		 * Scan the stored keys looking for ones that need
		 * removal or refreshing
		 */
		for (result = dns_rdataset_first(kdset);
		     result == ISC_R_SUCCESS; result = dns_rdataset_next(kdset))
		{
			dns_rdata_reset(&rdata);
			dns_rdataset_current(kdset, &rdata);
			result = dns_rdata_tostruct(&rdata, &kd, NULL);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);

			/* Removal timer expired? */
			if (kd.removehd != 0 && kd.removehd < now) {
				dns_rriterator_pause(&rrit);
				CHECK(update_one_rr(db, ver, &diff,
						    DNS_DIFFOP_DEL, name, ttl,
						    &rdata));
				continue;
			}

			/* Acceptance timer expired? */
			if (kd.addhd <= now) {
				timer = kd.addhd;
			}

			/* Or do we just need to refresh the keyset? */
			if (timer > kd.refresh) {
				timer = kd.refresh;
			}

			dns_rriterator_pause(&rrit);
			set_refreshkeytimer(zone, &kd, now, false);
			timerset = true;
		}

		if (timer > now) {
			continue;
		}

		dns_rriterator_pause(&rrit);

#ifdef ENABLE_AFL
		if (!dns_fuzzing_resolver) {
#endif /* ifdef ENABLE_AFL */
			dns_keyfetch_t *kfetch = NULL;

			kfetch = isc_mem_get(zone->mctx,
					     sizeof(dns_keyfetch_t));
			*kfetch = (dns_keyfetch_t){ .zone = zone };
			isc_mem_attach(zone->mctx, &kfetch->mctx);

			zone->refreshkeycount++;
			isc_refcount_increment0(&zone->irefs);
			kname = dns_fixedname_initname(&kfetch->name);
			dns_name_dup(name, zone->mctx, kname);
			dns_rdataset_init(&kfetch->dnskeyset);
			dns_rdataset_init(&kfetch->dnskeysigset);
			dns_rdataset_init(&kfetch->keydataset);
			dns_rdataset_clone(kdset, &kfetch->keydataset);
			dns_db_attach(db, &kfetch->db);

			if (isc_log_wouldlog(dns_lctx, ISC_LOG_DEBUG(3))) {
				char namebuf[DNS_NAME_FORMATSIZE];
				dns_name_format(kname, namebuf,
						sizeof(namebuf));
				dnssec_log(zone, ISC_LOG_DEBUG(3),
					   "Creating key fetch in "
					   "zone_refreshkeys() for '%s'",
					   namebuf);
			}

			isc_async_run(zone->loop, do_keyfetch, kfetch);
			fetching = true;
#ifdef ENABLE_AFL
		}
#endif /* ifdef ENABLE_AFL */
	}
	if (!ISC_LIST_EMPTY(diff.tuples)) {
		CHECK(update_soa_serial(zone, db, ver, &diff, zone->mctx,
					zone->updatemethod));
		CHECK(zone_journal(zone, &diff, NULL, "zone_refreshkeys"));
		commit = true;
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_LOADED);
		zone_needdump(zone, 30);
	}

failure:
	if (!timerset) {
		isc_time_settoepoch(&zone->refreshkeytime);
	}

	if (!fetching) {
		DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_REFRESHING);
	}

	dns_diff_clear(&diff);
	if (ver != NULL) {
		dns_rriterator_destroy(&rrit);
		dns_db_closeversion(db, &ver, commit);
	}
	dns_db_detach(&db);

	UNLOCK_ZONE(zone);

	INSIST(ver == NULL);
}

static void
zone_maintenance(dns_zone_t *zone) {
	isc_time_t now;
	isc_result_t result;
	bool load_pending, exiting, dumping, viewok = false, notify;
	bool refreshkeys, sign, resign, rekey, chain, warn_expire;

	REQUIRE(DNS_ZONE_VALID(zone));
	ENTER;

	/*
	 * Are we pending load/reload, exiting, or unconfigured
	 * (e.g. because of a syntax failure in the config file)?
	 * If so, don't attempt maintenance.
	 */
	LOCK_ZONE(zone);
	load_pending = DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADPENDING);
	exiting = DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING);
	if (!load_pending && !exiting && zone->view != NULL) {
		dns_adb_t *adb = NULL;
		dns_view_getadb(zone->view, &adb);
		if (adb != NULL) {
			dns_adb_detach(&adb);
			viewok = true;
		}
	}
	UNLOCK_ZONE(zone);

	if (load_pending || exiting || !viewok) {
		return;
	}

	now = isc_time_now();

	/*
	 * Expire check.
	 */
	switch (zone->type) {
	case dns_zone_redirect:
		if (dns_remote_addresses(&zone->primaries) == NULL) {
			break;
		}
		FALLTHROUGH;
	case dns_zone_secondary:
	case dns_zone_mirror:
	case dns_zone_stub:
		LOCK_ZONE(zone);
		if (isc_time_compare(&now, &zone->expiretime) >= 0 &&
		    DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADED))
		{
			zone_expire(zone);
			zone->refreshtime = now;
		}
		UNLOCK_ZONE(zone);
		break;
	default:
		break;
	}

	/*
	 * Up to date check.
	 */
	switch (zone->type) {
	case dns_zone_redirect:
		if (dns_remote_addresses(&zone->primaries) == NULL) {
			break;
		}
		FALLTHROUGH;
	case dns_zone_secondary:
	case dns_zone_mirror:
	case dns_zone_stub:
		LOCK_ZONE(zone);
		if (!DNS_ZONE_FLAG(zone, DNS_ZONEFLG_DIALREFRESH) &&
		    isc_time_compare(&now, &zone->refreshtime) >= 0)
		{
			zone_refresh(zone);
		}
		UNLOCK_ZONE(zone);
		break;
	default:
		break;
	}

	/*
	 * Secondaries send notifies before backing up to disk,
	 * primaries after.
	 */
	LOCK_ZONE(zone);
	if (zone->notifydefer != 0 &&
	    !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NOTIFYNODEFER) &&
	    !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NOTIFYDEFERRED))
	{
		if (isc_time_compare(&now, &zone->notifytime) > 0) {
			zone->notifytime = now;
		}
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NOTIFYDEFERRED);
		DNS_ZONE_TIME_ADD(&zone->notifytime, zone->notifydefer,
				  &zone->notifytime);
	}
	notify = (zone->type == dns_zone_secondary ||
		  zone->type == dns_zone_mirror) &&
		 (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDNOTIFY) ||
		  DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDSTARTUPNOTIFY)) &&
		 isc_time_compare(&now, &zone->notifytime) >= 0;
	UNLOCK_ZONE(zone);

	if (notify) {
		zone_notify(zone, &now);
	}

	/*
	 * Do we need to consolidate the backing store?
	 */
	switch (zone->type) {
	case dns_zone_primary:
	case dns_zone_secondary:
	case dns_zone_mirror:
	case dns_zone_key:
	case dns_zone_redirect:
	case dns_zone_stub:
		LOCK_ZONE(zone);
		if (zone->masterfile != NULL &&
		    isc_time_compare(&now, &zone->dumptime) >= 0 &&
		    DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADED) &&
		    DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDDUMP))
		{
			dumping = was_dumping(zone);
		} else {
			dumping = true;
		}
		UNLOCK_ZONE(zone);
		if (!dumping) {
			result = zone_dump(zone, true); /* loop locked */
			if (result != ISC_R_SUCCESS) {
				dns_zone_log(zone, ISC_LOG_WARNING,
					     "dump failed: %s",
					     isc_result_totext(result));
			}
		}
		break;
	default:
		break;
	}

	/*
	 * Primary/redirect zones send notifies now, if needed
	 */
	switch (zone->type) {
	case dns_zone_primary:
	case dns_zone_redirect:
		LOCK_ZONE(zone);
		notify = (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDNOTIFY) ||
			  DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDSTARTUPNOTIFY)) &&
			 isc_time_compare(&now, &zone->notifytime) >= 0;
		UNLOCK_ZONE(zone);
		if (notify) {
			zone_notify(zone, &now);
		}
	default:
		break;
	}

	/*
	 * Do we need to refresh keys?
	 */
	switch (zone->type) {
	case dns_zone_key:
		LOCK_ZONE(zone);
		refreshkeys = isc_time_compare(&now, &zone->refreshkeytime) >=
				      0 &&
			      DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADED) &&
			      !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_REFRESHING);
		UNLOCK_ZONE(zone);
		if (refreshkeys) {
			zone_refreshkeys(zone);
		}
		break;
	case dns_zone_primary:
		LOCK_ZONE(zone);
		if (zone->rss != NULL) {
			isc_time_settoepoch(&zone->refreshkeytime);
			UNLOCK_ZONE(zone);
			break;
		}
		rekey = (!isc_time_isepoch(&zone->refreshkeytime) &&
			 isc_time_compare(&now, &zone->refreshkeytime) >= 0);
		UNLOCK_ZONE(zone);
		if (rekey) {
			zone_rekey(zone);
		}
	default:
		break;
	}

	switch (zone->type) {
	case dns_zone_primary:
	case dns_zone_redirect:
	case dns_zone_secondary:
		/*
		 * Do we need to sign/resign some RRsets?
		 */
		LOCK_ZONE(zone);
		if (zone->rss != NULL) {
			isc_time_settoepoch(&zone->signingtime);
			isc_time_settoepoch(&zone->resigntime);
			isc_time_settoepoch(&zone->nsec3chaintime);
			isc_time_settoepoch(&zone->keywarntime);
			UNLOCK_ZONE(zone);
			break;
		}
		sign = !isc_time_isepoch(&zone->signingtime) &&
		       isc_time_compare(&now, &zone->signingtime) >= 0;
		resign = !isc_time_isepoch(&zone->resigntime) &&
			 isc_time_compare(&now, &zone->resigntime) >= 0;
		chain = !isc_time_isepoch(&zone->nsec3chaintime) &&
			isc_time_compare(&now, &zone->nsec3chaintime) >= 0;
		warn_expire = !isc_time_isepoch(&zone->keywarntime) &&
			      isc_time_compare(&now, &zone->keywarntime) >= 0;
		UNLOCK_ZONE(zone);

		if (sign) {
			zone_sign(zone);
		} else if (resign) {
			zone_resigninc(zone);
		} else if (chain) {
			zone_nsec3chain(zone);
		}

		/*
		 * Do we need to issue a key expiry warning?
		 */
		if (warn_expire) {
			set_key_expiry_warning(zone, zone->key_expiry,
					       isc_time_seconds(&now));
		}
		break;

	default:
		break;
	}
	LOCK_ZONE(zone);
	zone_settimer(zone, &now);
	UNLOCK_ZONE(zone);
}

void
dns_zone_markdirty(dns_zone_t *zone) {
	uint32_t serial;
	isc_result_t result = ISC_R_SUCCESS;
	dns_zone_t *secure = NULL;

	/*
	 * Obtaining a lock on the zone->secure (see zone_send_secureserial)
	 * could result in a deadlock due to a LOR so we will spin if we
	 * can't obtain the both locks.
	 */
again:
	LOCK_ZONE(zone);
	if (zone->type == dns_zone_primary) {
		if (inline_raw(zone)) {
			unsigned int soacount;
			secure = zone->secure;
			INSIST(secure != zone);
			TRYLOCK_ZONE(result, secure);
			if (result != ISC_R_SUCCESS) {
				UNLOCK_ZONE(zone);
				secure = NULL;
				isc_thread_yield();
				goto again;
			}

			ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
			if (zone->db != NULL) {
				result = zone_get_from_db(
					zone, zone->db, NULL, &soacount, NULL,
					&serial, NULL, NULL, NULL, NULL, NULL);
			} else {
				result = DNS_R_NOTLOADED;
			}
			ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
			if (result == ISC_R_SUCCESS && soacount > 0U) {
				zone_send_secureserial(zone, serial);
			}
		}

		/* XXXMPA make separate call back */
		if (result == ISC_R_SUCCESS) {
			set_resigntime(zone);
			if (zone->loop != NULL) {
				isc_time_t now;
				now = isc_time_now();
				zone_settimer(zone, &now);
			}
		}
	}
	if (secure != NULL) {
		UNLOCK_ZONE(secure);
	}
	zone_needdump(zone, DNS_DUMP_DELAY);
	UNLOCK_ZONE(zone);
}

void
dns_zone_expire(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	zone_expire(zone);
	UNLOCK_ZONE(zone);
}

static void
zone_expire(dns_zone_t *zone) {
	dns_db_t *db = NULL;

	/*
	 * 'zone' locked by caller.
	 */

	REQUIRE(LOCKED_ZONE(zone));

	dns_zone_log(zone, ISC_LOG_WARNING, "expired");

	DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_EXPIRED);
	zone->refresh = DNS_ZONE_DEFAULTREFRESH;
	zone->retry = DNS_ZONE_DEFAULTRETRY;
	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_HAVETIMERS);

	/*
	 * An RPZ zone has expired; before unloading it, we must
	 * first remove it from the RPZ summary database. The
	 * easiest way to do this is "update" it with an empty
	 * database so that the update callback synchronizes
	 * the diff automatically.
	 */
	if (zone->rpzs != NULL && zone->rpz_num != DNS_RPZ_INVALID_NUM) {
		isc_result_t result;
		dns_rpz_zone_t *rpz = zone->rpzs->zones[zone->rpz_num];

		CHECK(dns_db_create(zone->mctx, ZONEDB_DEFAULT, &zone->origin,
				    dns_dbtype_zone, zone->rdclass, 0, NULL,
				    &db));
		CHECK(dns_rpz_dbupdate_callback(db, rpz));
		dns_zone_log(zone, ISC_LOG_WARNING,
			     "response-policy zone expired; "
			     "policies unloaded");
	}

failure:
	if (db != NULL) {
		dns_db_detach(&db);
	}

	zone_unload(zone);
}

static void
zone_refresh(dns_zone_t *zone) {
	isc_interval_t i;
	uint32_t oldflags;
	isc_result_t result;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(LOCKED_ZONE(zone));

	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING)) {
		return;
	}

	/*
	 * Set DNS_ZONEFLG_REFRESH so that there is only one refresh operation
	 * in progress at a time.
	 */

	oldflags = ISC_ZONE_GET(zone, flags);
	if (dns_remote_addresses(&zone->primaries) == NULL) {
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NOPRIMARIES);
		if ((oldflags & DNS_ZONEFLG_NOPRIMARIES) == 0) {
			dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
				      ISC_LOG_ERROR,
				      "cannot refresh: no primaries");
		}
		return;
	}
	DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_REFRESH);
	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_NOEDNS);
	if ((oldflags & (DNS_ZONEFLG_REFRESH | DNS_ZONEFLG_LOADING)) != 0) {
		return;
	}

	/*
	 * Set the next refresh time as if refresh check has failed.
	 * Setting this to the retry time will do that.  XXXMLG
	 * If we are successful it will be reset using zone->refresh.
	 */
	isc_interval_set(&i, zone->retry - isc_random_uniform(zone->retry / 4),
			 0);
	result = isc_time_nowplusinterval(&zone->refreshtime, &i);
	if (result != ISC_R_SUCCESS) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_WARNING,
			      "isc_time_nowplusinterval() failed: %s",
			      isc_result_totext(result));
	}

	/*
	 * When lacking user-specified timer values from the SOA,
	 * do exponential backoff of the retry time up to a
	 * maximum of six hours.
	 */
	if (!DNS_ZONE_FLAG(zone, DNS_ZONEFLG_HAVETIMERS)) {
		zone->retry = ISC_MIN(zone->retry * 2, 6 * 3600);
	}

	dns_remote_reset(&zone->primaries, true);

	/* initiate soa query */
	queue_soa_query(zone);
}

static void
zone_refresh_async(void *arg) {
	dns_zone_t *zone = arg;

	LOCK_ZONE(zone);
	zone_refresh(zone);
	UNLOCK_ZONE(zone);

	dns_zone_detach(&zone);
}

void
dns_zone_refresh(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	dns_zone_ref(zone);
	isc_async_run(zone->loop, zone_refresh_async, zone);
}

static isc_result_t
zone_journal_rollforward(dns_zone_t *zone, dns_db_t *db, bool *needdump,
			 bool *fixjournal) {
	dns_journal_t *journal = NULL;
	unsigned int options;
	isc_result_t result;

	if (zone->type == dns_zone_primary &&
	    (inline_secure(zone) ||
	     (zone->update_acl != NULL || zone->ssutable != NULL)))
	{
		options = DNS_JOURNALOPT_RESIGN;
	} else {
		options = 0;
	}

	result = dns_journal_open(zone->mctx, zone->journal, DNS_JOURNAL_READ,
				  &journal);
	if (result == ISC_R_NOTFOUND) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD, ISC_LOG_DEBUG(3),
			      "no journal file, but that's OK ");
		return ISC_R_SUCCESS;
	} else if (result != ISC_R_SUCCESS) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD, ISC_LOG_ERROR,
			      "journal open failed: %s",
			      isc_result_totext(result));
		return result;
	}

	if (dns_journal_empty(journal)) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD, ISC_LOG_DEBUG(1),
			      "journal empty");
		dns_journal_destroy(&journal);
		return ISC_R_SUCCESS;
	}

	result = dns_journal_rollforward(journal, db, options);
	switch (result) {
	case ISC_R_SUCCESS:
		*needdump = true;
		FALLTHROUGH;
	case DNS_R_UPTODATE:
		if (dns_journal_recovered(journal)) {
			*fixjournal = true;
			dns_zone_logc(
				zone, DNS_LOGCATEGORY_ZONELOAD,
				ISC_LOG_DEBUG(1),
				"journal rollforward completed successfully "
				"using old journal format: %s",
				isc_result_totext(result));
		} else {
			dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD,
				      ISC_LOG_DEBUG(1),
				      "journal rollforward completed "
				      "successfully: %s",
				      isc_result_totext(result));
		}

		dns_journal_destroy(&journal);
		return ISC_R_SUCCESS;
	case ISC_R_NOTFOUND:
	case ISC_R_RANGE:
		dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD, ISC_LOG_ERROR,
			      "journal rollforward failed: journal out of sync "
			      "with zone");
		dns_journal_destroy(&journal);
		return result;
	default:
		dns_zone_logc(zone, DNS_LOGCATEGORY_ZONELOAD, ISC_LOG_ERROR,
			      "journal rollforward failed: %s",
			      isc_result_totext(result));
		dns_journal_destroy(&journal);
		return result;
	}
}

static void
zone_journal_compact(dns_zone_t *zone, dns_db_t *db, uint32_t serial) {
	isc_result_t result;
	int32_t journalsize;
	dns_dbversion_t *ver = NULL;
	uint64_t dbsize;
	uint32_t options = 0;

	INSIST(LOCKED_ZONE(zone));
	if (inline_raw(zone)) {
		INSIST(LOCKED_ZONE(zone->secure));
	}

	journalsize = zone->journalsize;
	if (journalsize == -1) {
		journalsize = DNS_JOURNAL_SIZE_MAX;
		dns_db_currentversion(db, &ver);
		result = dns_db_getsize(db, ver, NULL, &dbsize);
		dns_db_closeversion(db, &ver, false);
		if (result != ISC_R_SUCCESS) {
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "zone_journal_compact: "
				     "could not get zone size: %s",
				     isc_result_totext(result));
		} else if (dbsize < DNS_JOURNAL_SIZE_MAX / 2) {
			journalsize = (int32_t)dbsize * 2;
		}
	}
	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_FIXJOURNAL)) {
		options |= DNS_JOURNAL_COMPACTALL;
		DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_FIXJOURNAL);
		zone_debuglog(zone, __func__, 1, "repair full journal");
	} else {
		zone_debuglog(zone, __func__, 1, "target journal size %d",
			      journalsize);
	}
	result = dns_journal_compact(zone->mctx, zone->journal, serial, options,
				     journalsize);
	switch (result) {
	case ISC_R_SUCCESS:
	case ISC_R_NOSPACE:
	case ISC_R_NOTFOUND:
		dns_zone_log(zone, ISC_LOG_DEBUG(3), "dns_journal_compact: %s",
			     isc_result_totext(result));
		break;
	default:
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "dns_journal_compact failed: %s",
			     isc_result_totext(result));
		break;
	}
}

isc_result_t
dns_zone_flush(dns_zone_t *zone) {
	isc_result_t result = ISC_R_SUCCESS;
	bool dumping;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_FLUSH);
	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDDUMP) &&
	    zone->masterfile != NULL)
	{
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NEEDCOMPACT);
		result = ISC_R_ALREADYRUNNING;
		dumping = was_dumping(zone);
	} else {
		dumping = true;
	}
	UNLOCK_ZONE(zone);
	if (!dumping) {
		result = zone_dump(zone, true);
	}
	return result;
}

isc_result_t
dns_zone_dump(dns_zone_t *zone) {
	isc_result_t result = ISC_R_ALREADYRUNNING;
	bool dumping;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	dumping = was_dumping(zone);
	UNLOCK_ZONE(zone);
	if (!dumping) {
		result = zone_dump(zone, false);
	}
	return result;
}

static void
zone_needdump(dns_zone_t *zone, unsigned int delay) {
	isc_time_t dumptime;
	isc_time_t now;

	/*
	 * 'zone' locked by caller
	 */

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(LOCKED_ZONE(zone));
	ENTER;

	/*
	 * Do we have a place to dump to and are we loaded?
	 */
	if (zone->masterfile == NULL ||
	    DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADED) == 0)
	{
		return;
	}

	now = isc_time_now();
	/* add some noise */
	DNS_ZONE_JITTER_ADD(&now, delay, &dumptime);

	DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NEEDDUMP);
	if (isc_time_isepoch(&zone->dumptime) ||
	    isc_time_compare(&zone->dumptime, &dumptime) > 0)
	{
		zone->dumptime = dumptime;
	}
	if (zone->loop != NULL) {
		zone_settimer(zone, &now);
	}
}

static void
dump_done(void *arg, isc_result_t result) {
	dns_zone_t *zone = arg;
	dns_zone_t *secure = NULL;
	dns_db_t *db;
	dns_dbversion_t *version;
	bool again = false;
	bool compact = false;
	uint32_t serial;
	isc_result_t tresult;

	REQUIRE(DNS_ZONE_VALID(zone));

	ENTER;

	/*
	 * Adjust modification time of zone file to preserve expire timing.
	 */
	if ((zone->type == dns_zone_secondary ||
	     zone->type == dns_zone_mirror ||
	     zone->type == dns_zone_redirect) &&
	    result == ISC_R_SUCCESS)
	{
		LOCK_ZONE(zone);
		isc_time_t when;
		isc_interval_t i;
		isc_interval_set(&i, zone->expire, 0);
		result = isc_time_subtract(&zone->expiretime, &i, &when);
		if (result == ISC_R_SUCCESS) {
			(void)isc_file_settime(zone->masterfile, &when);
		} else {
			result = ISC_R_SUCCESS;
		}
		UNLOCK_ZONE(zone);
	}

	if (result == ISC_R_SUCCESS && zone->journal != NULL) {
		/*
		 * We don't own these, zone->dctx must stay valid.
		 */
		db = dns_dumpctx_db(zone->dumpctx);
		version = dns_dumpctx_version(zone->dumpctx);
		tresult = dns_db_getsoaserial(db, version, &serial);

		/*
		 * Handle lock order inversion.
		 */
	again:
		LOCK_ZONE(zone);
		if (inline_raw(zone)) {
			secure = zone->secure;
			INSIST(secure != zone);
			TRYLOCK_ZONE(result, secure);
			if (result != ISC_R_SUCCESS) {
				UNLOCK_ZONE(zone);
				secure = NULL;
				isc_thread_yield();
				goto again;
			}
		}

		/*
		 * If there is a secure version of this zone
		 * use its serial if it is less than ours.
		 */
		if (tresult == ISC_R_SUCCESS && secure != NULL) {
			uint32_t sserial;
			isc_result_t mresult;

			ZONEDB_LOCK(&secure->dblock, isc_rwlocktype_read);
			if (secure->db != NULL) {
				mresult = dns_db_getsoaserial(zone->secure->db,
							      NULL, &sserial);
				if (mresult == ISC_R_SUCCESS &&
				    isc_serial_lt(sserial, serial))
				{
					serial = sserial;
				}
			}
			ZONEDB_UNLOCK(&secure->dblock, isc_rwlocktype_read);
		}
		if (tresult == ISC_R_SUCCESS && zone->xfr == NULL) {
			dns_db_t *zdb = NULL;
			if (dns_zone_getdb(zone, &zdb) == ISC_R_SUCCESS) {
				zone_journal_compact(zone, zdb, serial);
				dns_db_detach(&zdb);
			}
		} else if (tresult == ISC_R_SUCCESS) {
			compact = true;
			zone->compact_serial = serial;
		}
		if (secure != NULL) {
			UNLOCK_ZONE(secure);
		}
		UNLOCK_ZONE(zone);
	}

	LOCK_ZONE(zone);
	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_DUMPING);
	if (compact) {
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NEEDCOMPACT);
	}
	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_SHUTDOWN)) {
		/*
		 * If DNS_ZONEFLG_SHUTDOWN is set, all external references to
		 * the zone are gone, which means it is in the process of being
		 * cleaned up, so do not reschedule dumping.
		 *
		 * Detach from the raw version of the zone in case this
		 * operation has been deferred in zone_shutdown().
		 */
		if (zone->raw != NULL) {
			dns_zone_detach(&zone->raw);
		}
		if (result == ISC_R_SUCCESS) {
			DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_FLUSH);
		}
	} else if (result != ISC_R_SUCCESS && result != ISC_R_CANCELED) {
		/*
		 * Try again in a short while.
		 */
		zone_needdump(zone, DNS_DUMP_DELAY);
	} else if (result == ISC_R_SUCCESS &&
		   DNS_ZONE_FLAG(zone, DNS_ZONEFLG_FLUSH) &&
		   DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDDUMP) &&
		   DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADED))
	{
		DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_NEEDDUMP);
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_DUMPING);
		isc_time_settoepoch(&zone->dumptime);
		again = true;
	} else if (result == ISC_R_SUCCESS) {
		DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_FLUSH);
	}

	if (zone->dumpctx != NULL) {
		dns_dumpctx_detach(&zone->dumpctx);
	}
	UNLOCK_ZONE(zone);
	if (again) {
		(void)zone_dump(zone, false);
	}
	dns_zone_idetach(&zone);
}

static isc_result_t
zone_dump(dns_zone_t *zone, bool compact) {
	isc_result_t result;
	dns_dbversion_t *version = NULL;
	bool again = false;
	dns_db_t *db = NULL;
	char *masterfile = NULL;
	dns_masterformat_t masterformat = dns_masterformat_none;
	const dns_master_style_t *masterstyle = NULL;
	dns_masterrawheader_t rawdata;

	/*
	 * 'compact' MUST only be set if we are loop locked.
	 */

	REQUIRE(DNS_ZONE_VALID(zone));
	ENTER;

redo:
	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	if (zone->db != NULL) {
		dns_db_attach(zone->db, &db);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
	LOCK_ZONE(zone);
	if (zone->masterfile != NULL) {
		masterfile = isc_mem_strdup(zone->mctx, zone->masterfile);
		masterformat = zone->masterformat;
	}
	if (zone->type == dns_zone_key) {
		masterstyle = &dns_master_style_keyzone;
	} else if (zone->masterstyle != NULL) {
		masterstyle = zone->masterstyle;
	} else {
		masterstyle = &dns_master_style_default;
	}
	UNLOCK_ZONE(zone);
	if (db == NULL) {
		result = DNS_R_NOTLOADED;
		goto fail;
	}
	if (masterfile == NULL) {
		result = DNS_R_NOMASTERFILE;
		goto fail;
	}

	dns_db_currentversion(db, &version);

	dns_master_initrawheader(&rawdata);

	if (inline_secure(zone)) {
		get_raw_serial(zone->raw, &rawdata);
	}

	if (compact && zone->type != dns_zone_stub) {
		LOCK_ZONE(zone);
		zone_iattach(zone, &(dns_zone_t *){ NULL });

		INSIST(zone != zone->raw);

		result = dns_master_dumpasync(
			zone->mctx, db, version, masterstyle, masterfile,
			zone->loop, dump_done, zone, &zone->dumpctx,
			masterformat, &rawdata);

		UNLOCK_ZONE(zone);
		if (result != ISC_R_SUCCESS) {
			dns_zone_idetach(&(dns_zone_t *){ zone });
			goto fail;
		}
		result = DNS_R_CONTINUE;
	} else {
		result = dns_master_dump(zone->mctx, db, version, masterstyle,
					 masterfile, masterformat, &rawdata);
		if ((zone->type == dns_zone_secondary ||
		     zone->type == dns_zone_mirror ||
		     zone->type == dns_zone_redirect) &&
		    result == ISC_R_SUCCESS)
		{
			isc_time_t when;
			isc_interval_t i;
			isc_interval_set(&i, zone->expire, 0);
			result = isc_time_subtract(&zone->expiretime, &i,
						   &when);
			if (result == ISC_R_SUCCESS) {
				(void)isc_file_settime(zone->masterfile, &when);
			} else {
				result = ISC_R_SUCCESS;
			}
		}
	}
fail:
	if (version != NULL) {
		dns_db_closeversion(db, &version, false);
	}
	if (db != NULL) {
		dns_db_detach(&db);
	}
	if (masterfile != NULL) {
		isc_mem_free(zone->mctx, masterfile);
		masterfile = NULL;
	}

	if (result == DNS_R_CONTINUE) {
		/*
		 * Asyncronous write is in progress.  Zone flags will get
		 * updated on completion.  Cleanup is complete.  We are done.
		 */
		return ISC_R_SUCCESS;
	}

	again = false;
	LOCK_ZONE(zone);
	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_DUMPING);
	if (result != ISC_R_SUCCESS) {
		/*
		 * Try again in a short while.
		 */
		zone_needdump(zone, DNS_DUMP_DELAY);
	} else if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_FLUSH) &&
		   DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDDUMP) &&
		   DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADED))
	{
		DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_NEEDDUMP);
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_DUMPING);
		isc_time_settoepoch(&zone->dumptime);
		again = true;
	} else {
		DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_FLUSH);
	}
	UNLOCK_ZONE(zone);
	if (again) {
		goto redo;
	}

	return result;
}

static isc_result_t
dumptostream(dns_zone_t *zone, FILE *fd, const dns_master_style_t *style,
	     dns_masterformat_t format, const uint32_t rawversion) {
	isc_result_t result;
	dns_dbversion_t *version = NULL;
	dns_db_t *db = NULL;
	dns_masterrawheader_t rawdata;

	REQUIRE(DNS_ZONE_VALID(zone));

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	if (zone->db != NULL) {
		dns_db_attach(zone->db, &db);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
	if (db == NULL) {
		return DNS_R_NOTLOADED;
	}

	dns_db_currentversion(db, &version);
	dns_master_initrawheader(&rawdata);
	if (rawversion == 0) {
		rawdata.flags |= DNS_MASTERRAW_COMPAT;
	} else if (inline_secure(zone)) {
		get_raw_serial(zone->raw, &rawdata);
	} else if (zone->sourceserialset) {
		rawdata.flags = DNS_MASTERRAW_SOURCESERIALSET;
		rawdata.sourceserial = zone->sourceserial;
	}
	result = dns_master_dumptostream(zone->mctx, db, version, style, format,
					 &rawdata, fd);
	dns_db_closeversion(db, &version, false);
	dns_db_detach(&db);
	return result;
}

isc_result_t
dns_zone_dumptostream(dns_zone_t *zone, FILE *fd, dns_masterformat_t format,
		      const dns_master_style_t *style,
		      const uint32_t rawversion) {
	return dumptostream(zone, fd, style, format, rawversion);
}

void
dns_zone_unload(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	zone_unload(zone);
	UNLOCK_ZONE(zone);
}

static void
notify_cancel(dns_zone_t *zone) {
	dns_notify_t *notify;

	/*
	 * 'zone' locked by caller.
	 */

	REQUIRE(LOCKED_ZONE(zone));

	for (notify = ISC_LIST_HEAD(zone->notifies); notify != NULL;
	     notify = ISC_LIST_NEXT(notify, link))
	{
		if (notify->find != NULL) {
			dns_adb_cancelfind(notify->find);
		}
		if (notify->request != NULL) {
			dns_request_cancel(notify->request);
		}
	}
}

static void
checkds_cancel(dns_zone_t *zone) {
	dns_checkds_t *checkds;

	/*
	 * 'zone' locked by caller.
	 */

	REQUIRE(LOCKED_ZONE(zone));

	for (checkds = ISC_LIST_HEAD(zone->checkds_requests); checkds != NULL;
	     checkds = ISC_LIST_NEXT(checkds, link))
	{
		if (checkds->find != NULL) {
			dns_adb_cancelfind(checkds->find);
		}
		if (checkds->request != NULL) {
			dns_request_cancel(checkds->request);
		}
	}
}

static void
forward_cancel(dns_zone_t *zone) {
	dns_forward_t *forward;

	/*
	 * 'zone' locked by caller.
	 */

	REQUIRE(LOCKED_ZONE(zone));

	for (forward = ISC_LIST_HEAD(zone->forwards); forward != NULL;
	     forward = ISC_LIST_NEXT(forward, link))
	{
		if (forward->request != NULL) {
			dns_request_cancel(forward->request);
		}
	}
}

static void
zone_unload(dns_zone_t *zone) {
	/*
	 * 'zone' locked by caller.
	 */

	REQUIRE(LOCKED_ZONE(zone));

	if (!DNS_ZONE_FLAG(zone, DNS_ZONEFLG_FLUSH) ||
	    !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_DUMPING))
	{
		if (zone->dumpctx != NULL) {
			dns_dumpctx_cancel(zone->dumpctx);
		}
	}
	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_write);
	zone_detachdb(zone);
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_write);
	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_LOADED);
	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_NEEDDUMP);

	if (zone->type == dns_zone_mirror) {
		dns_zone_log(zone, ISC_LOG_INFO,
			     "mirror zone is no longer in use; "
			     "reverting to normal recursion");
	}
}

void
dns_zone_setminrefreshtime(dns_zone_t *zone, uint32_t val) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(val > 0);

	zone->minrefresh = val;
}

void
dns_zone_setmaxrefreshtime(dns_zone_t *zone, uint32_t val) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(val > 0);

	zone->maxrefresh = val;
}

void
dns_zone_setminretrytime(dns_zone_t *zone, uint32_t val) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(val > 0);

	zone->minretry = val;
}

void
dns_zone_setmaxretrytime(dns_zone_t *zone, uint32_t val) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(val > 0);

	zone->maxretry = val;
}

uint32_t
dns_zone_getmaxrecords(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->maxrecords;
}

void
dns_zone_setmaxrecords(dns_zone_t *zone, uint32_t val) {
	REQUIRE(DNS_ZONE_VALID(zone));

	zone->maxrecords = val;
}

void
dns_zone_setmaxrrperset(dns_zone_t *zone, uint32_t val) {
	REQUIRE(DNS_ZONE_VALID(zone));

	zone->maxrrperset = val;
	if (zone->db != NULL) {
		dns_db_setmaxrrperset(zone->db, val);
	}
}

void
dns_zone_setmaxtypepername(dns_zone_t *zone, uint32_t val) {
	REQUIRE(DNS_ZONE_VALID(zone));

	zone->maxtypepername = val;
	if (zone->db != NULL) {
		dns_db_setmaxtypepername(zone->db, val);
	}
}

static bool
notify_isqueued(dns_zone_t *zone, unsigned int flags, dns_name_t *name,
		isc_sockaddr_t *addr, dns_tsigkey_t *key,
		dns_transport_t *transport) {
	dns_notify_t *notify;
	dns_zonemgr_t *zmgr;
	isc_result_t result;

	for (notify = ISC_LIST_HEAD(zone->notifies); notify != NULL;
	     notify = ISC_LIST_NEXT(notify, link))
	{
		if (notify->request != NULL) {
			continue;
		}
		if (name != NULL && dns_name_dynamic(&notify->ns) &&
		    dns_name_equal(name, &notify->ns))
		{
			goto requeue;
		}
		if (addr != NULL && isc_sockaddr_equal(addr, &notify->dst) &&
		    notify->key == key && notify->transport == transport)
		{
			goto requeue;
		}
	}
	return false;

requeue:
	/*
	 * If we are enqueued on the startup ratelimiter and this is
	 * not a startup notify, re-enqueue on the normal notify
	 * ratelimiter.
	 */
	if (notify->rlevent != NULL && (flags & DNS_NOTIFY_STARTUP) == 0 &&
	    (notify->flags & DNS_NOTIFY_STARTUP) != 0)
	{
		zmgr = notify->zone->zmgr;
		result = isc_ratelimiter_dequeue(zmgr->startupnotifyrl,
						 &notify->rlevent);
		if (result != ISC_R_SUCCESS) {
			return true;
		}

		notify->flags &= ~DNS_NOTIFY_STARTUP;
		result = isc_ratelimiter_enqueue(
			notify->zone->zmgr->notifyrl, notify->zone->loop,
			notify_send_toaddr, notify, &notify->rlevent);
		if (result != ISC_R_SUCCESS) {
			return false;
		}
	}

	return true;
}

static bool
notify_isself(dns_zone_t *zone, isc_sockaddr_t *dst) {
	dns_tsigkey_t *key = NULL;
	isc_sockaddr_t src;
	isc_sockaddr_t any;
	bool isself;
	isc_netaddr_t dstaddr;
	isc_result_t result;

	if (zone->view == NULL || zone->isself == NULL) {
		return false;
	}

	switch (isc_sockaddr_pf(dst)) {
	case PF_INET:
		src = zone->notifysrc4;
		isc_sockaddr_any(&any);
		break;
	case PF_INET6:
		src = zone->notifysrc6;
		isc_sockaddr_any6(&any);
		break;
	default:
		return false;
	}

	/*
	 * When sending from any the kernel will assign a source address
	 * that matches the destination address.
	 */
	if (isc_sockaddr_eqaddr(&any, &src)) {
		src = *dst;
	}

	isc_netaddr_fromsockaddr(&dstaddr, dst);
	result = dns_view_getpeertsig(zone->view, &dstaddr, &key);
	if (result != ISC_R_SUCCESS && result != ISC_R_NOTFOUND) {
		return false;
	}
	isself = (zone->isself)(zone->view, key, &src, dst, zone->rdclass,
				zone->isselfarg);
	if (key != NULL) {
		dns_tsigkey_detach(&key);
	}
	return isself;
}

static void
notify_destroy(dns_notify_t *notify, bool locked) {
	isc_mem_t *mctx;

	REQUIRE(DNS_NOTIFY_VALID(notify));

	if (notify->zone != NULL) {
		if (!locked) {
			LOCK_ZONE(notify->zone);
		}
		REQUIRE(LOCKED_ZONE(notify->zone));
		if (ISC_LINK_LINKED(notify, link)) {
			ISC_LIST_UNLINK(notify->zone->notifies, notify, link);
		}
		if (!locked) {
			UNLOCK_ZONE(notify->zone);
		}
		if (locked) {
			zone_idetach(&notify->zone);
		} else {
			dns_zone_idetach(&notify->zone);
		}
	}
	if (notify->find != NULL) {
		dns_adb_destroyfind(&notify->find);
	}
	if (notify->request != NULL) {
		dns_request_destroy(&notify->request);
	}
	if (dns_name_dynamic(&notify->ns)) {
		dns_name_free(&notify->ns, notify->mctx);
	}
	if (notify->key != NULL) {
		dns_tsigkey_detach(&notify->key);
	}
	if (notify->transport != NULL) {
		dns_transport_detach(&notify->transport);
	}
	mctx = notify->mctx;
	isc_mem_put(notify->mctx, notify, sizeof(*notify));
	isc_mem_detach(&mctx);
}

static isc_result_t
notify_create(isc_mem_t *mctx, unsigned int flags, dns_notify_t **notifyp) {
	dns_notify_t *notify;

	REQUIRE(notifyp != NULL && *notifyp == NULL);

	notify = isc_mem_get(mctx, sizeof(*notify));
	*notify = (dns_notify_t){
		.flags = flags,
	};

	isc_mem_attach(mctx, &notify->mctx);
	isc_sockaddr_any(&notify->src);
	isc_sockaddr_any(&notify->dst);
	dns_name_init(&notify->ns, NULL);
	ISC_LINK_INIT(notify, link);
	notify->magic = NOTIFY_MAGIC;
	*notifyp = notify;
	return ISC_R_SUCCESS;
}

/*
 * XXXAG should check for DNS_ZONEFLG_EXITING
 */
static void
process_notify_adb_event(void *arg) {
	dns_adbfind_t *find = (dns_adbfind_t *)arg;
	dns_notify_t *notify = (dns_notify_t *)find->cbarg;
	dns_adbstatus_t astat = find->status;

	REQUIRE(DNS_NOTIFY_VALID(notify));
	REQUIRE(find == notify->find);

	switch (astat) {
	case DNS_ADB_MOREADDRESSES:
		dns_adb_destroyfind(&notify->find);
		notify_find_address(notify);
		return;

	case DNS_ADB_NOMOREADDRESSES:
		LOCK_ZONE(notify->zone);
		notify_send(notify);
		UNLOCK_ZONE(notify->zone);
		break;

	default:
		break;
	}

	notify_destroy(notify, false);
}

static void
notify_find_address(dns_notify_t *notify) {
	isc_result_t result;
	unsigned int options;
	dns_adb_t *adb = NULL;

	REQUIRE(DNS_NOTIFY_VALID(notify));

	options = DNS_ADBFIND_WANTEVENT;
	if (isc_net_probeipv4() != ISC_R_DISABLED) {
		options |= DNS_ADBFIND_INET;
	}
	if (isc_net_probeipv6() != ISC_R_DISABLED) {
		options |= DNS_ADBFIND_INET6;
	}

	dns_view_getadb(notify->zone->view, &adb);
	if (adb == NULL) {
		goto destroy;
	}

	result = dns_adb_createfind(
		adb, notify->zone->loop, process_notify_adb_event, notify,
		&notify->ns, dns_rootname, 0, options, 0, NULL,
		notify->zone->view->dstport, 0, NULL, NULL, &notify->find);
	dns_adb_detach(&adb);

	/* Something failed? */
	if (result != ISC_R_SUCCESS) {
		goto destroy;
	}

	/* More addresses pending? */
	if ((notify->find->options & DNS_ADBFIND_WANTEVENT) != 0) {
		return;
	}

	/* We have as many addresses as we can get. */
	LOCK_ZONE(notify->zone);
	notify_send(notify);
	UNLOCK_ZONE(notify->zone);

destroy:
	notify_destroy(notify, false);
}

static isc_result_t
notify_send_queue(dns_notify_t *notify, bool startup) {
	return isc_ratelimiter_enqueue(
		startup ? notify->zone->zmgr->startupnotifyrl
			: notify->zone->zmgr->notifyrl,
		notify->zone->loop, notify_send_toaddr, notify,
		&notify->rlevent);
}

static void
notify_send_toaddr(void *arg) {
	dns_notify_t *notify = (dns_notify_t *)arg;
	isc_result_t result;
	dns_message_t *message = NULL;
	isc_netaddr_t dstip;
	dns_tsigkey_t *key = NULL;
	char addrbuf[ISC_SOCKADDR_FORMATSIZE];
	isc_sockaddr_t src;
	unsigned int options, timeout, udptimeout;
	bool have_notifysource = false;
	isc_tlsctx_cache_t *zmgr_tlsctx_cache = NULL;

	REQUIRE(DNS_NOTIFY_VALID(notify));

	LOCK_ZONE(notify->zone);

	isc_sockaddr_format(&notify->dst, addrbuf, sizeof(addrbuf));

	if (DNS_ZONE_FLAG(notify->zone, DNS_ZONEFLG_LOADED) == 0 ||
	    notify->rlevent->canceled ||
	    DNS_ZONE_FLAG(notify->zone, DNS_ZONEFLG_EXITING) ||
	    notify->zone->view->requestmgr == NULL || notify->zone->db == NULL)
	{
		result = ISC_R_CANCELED;
		goto cleanup;
	}

	/*
	 * The raw IPv4 address should also exist.  Don't send to the
	 * mapped form.
	 */
	if (isc_sockaddr_pf(&notify->dst) == PF_INET6 &&
	    IN6_IS_ADDR_V4MAPPED(&notify->dst.type.sin6.sin6_addr))
	{
		notify_log(notify->zone, ISC_LOG_DEBUG(3),
			   "notify: ignoring IPv6 mapped IPV4 address: %s",
			   addrbuf);
		result = ISC_R_CANCELED;
		goto cleanup;
	}

	result = notify_createmessage(notify->zone, notify->flags, &message);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	if (notify->key != NULL) {
		/* Transfer ownership of key */
		key = notify->key;
		notify->key = NULL;
	} else {
		isc_netaddr_fromsockaddr(&dstip, &notify->dst);
		result = dns_view_getpeertsig(notify->zone->view, &dstip, &key);
		if (result != ISC_R_SUCCESS && result != ISC_R_NOTFOUND) {
			notify_log(notify->zone, ISC_LOG_ERROR,
				   "NOTIFY to %s not sent. "
				   "Peer TSIG key lookup failure.",
				   addrbuf);
			goto cleanup_message;
		}
	}

	if (key != NULL) {
		char namebuf[DNS_NAME_FORMATSIZE];

		dns_name_format(key->name, namebuf, sizeof(namebuf));
		notify_log(notify->zone, ISC_LOG_INFO,
			   "sending notify to %s : TSIG (%s)", addrbuf,
			   namebuf);
	} else {
		notify_log(notify->zone, ISC_LOG_INFO, "sending notify to %s",
			   addrbuf);
	}
	options = 0;
	if (notify->zone->view->peers != NULL) {
		dns_peer_t *peer = NULL;
		bool usetcp = false;
		result = dns_peerlist_peerbyaddr(notify->zone->view->peers,
						 &dstip, &peer);
		if (result == ISC_R_SUCCESS) {
			result = dns_peer_getnotifysource(peer, &src);
			if (result == ISC_R_SUCCESS) {
				have_notifysource = true;
			}
			result = dns_peer_getforcetcp(peer, &usetcp);
			if (result == ISC_R_SUCCESS && usetcp) {
				options |= DNS_FETCHOPT_TCP;
			}
		}
	}
	switch (isc_sockaddr_pf(&notify->dst)) {
	case PF_INET:
		if (!have_notifysource) {
			isc_sockaddr_t any;
			isc_sockaddr_any(&any);

			src = notify->src;
			if (isc_sockaddr_equal(&src, &any)) {
				src = notify->zone->notifysrc4;
			}
		}
		break;
	case PF_INET6:
		if (!have_notifysource) {
			isc_sockaddr_t any;
			isc_sockaddr_any6(&any);

			src = notify->src;
			if (isc_sockaddr_equal(&src, &any)) {
				src = notify->zone->notifysrc6;
			}
		}
		break;
	default:
		result = ISC_R_NOTIMPLEMENTED;
		goto cleanup_key;
	}
	udptimeout = 5;
	if (DNS_ZONE_FLAG(notify->zone, DNS_ZONEFLG_DIALNOTIFY)) {
		udptimeout = 30;
	}
	timeout = 3 * udptimeout + 1;
again:
	if ((notify->flags & DNS_NOTIFY_TCP) != 0) {
		options |= DNS_REQUESTOPT_TCP;
		udptimeout = 0;
		timeout = 15;
	}

	zmgr_tlsctx_attach(notify->zone->zmgr, &zmgr_tlsctx_cache);

	result = dns_request_create(notify->zone->view->requestmgr, message,
				    &src, &notify->dst, notify->transport,
				    zmgr_tlsctx_cache, options, key, timeout,
				    udptimeout, 2, notify->zone->loop,
				    notify_done, notify, &notify->request);

	isc_tlsctx_cache_detach(&zmgr_tlsctx_cache);

	if (result == ISC_R_SUCCESS) {
		if (isc_sockaddr_pf(&notify->dst) == AF_INET) {
			inc_stats(notify->zone,
				  dns_zonestatscounter_notifyoutv4);
		} else {
			inc_stats(notify->zone,
				  dns_zonestatscounter_notifyoutv6);
		}
	} else if (result == ISC_R_SHUTTINGDOWN || result == ISC_R_CANCELED) {
		goto cleanup_key;
	} else if ((notify->flags & DNS_NOTIFY_TCP) == 0) {
		notify_log(notify->zone, ISC_LOG_NOTICE,
			   "notify to %s failed: %s: retrying over TCP",
			   addrbuf, isc_result_totext(result));
		notify->flags |= DNS_NOTIFY_TCP;
		goto again;
	}

cleanup_key:
	if (key != NULL) {
		dns_tsigkey_detach(&key);
	}
cleanup_message:
	dns_message_detach(&message);
cleanup:
	UNLOCK_ZONE(notify->zone);
	if (notify->rlevent != NULL) {
		isc_rlevent_free(&notify->rlevent);
	}

	if (result != ISC_R_SUCCESS) {
		isc_sockaddr_format(&notify->dst, addrbuf, sizeof(addrbuf));
		notify_log(notify->zone, ISC_LOG_WARNING,
			   "notify to %s failed: %s", addrbuf,
			   isc_result_totext(result));
		notify_destroy(notify, false);
	}
}

static void
notify_send(dns_notify_t *notify) {
	dns_adbaddrinfo_t *ai;
	isc_sockaddr_t dst;
	isc_result_t result;
	dns_notify_t *newnotify = NULL;
	unsigned int flags;
	bool startup;

	/*
	 * Zone lock held by caller.
	 */
	REQUIRE(DNS_NOTIFY_VALID(notify));
	REQUIRE(LOCKED_ZONE(notify->zone));

	if (DNS_ZONE_FLAG(notify->zone, DNS_ZONEFLG_EXITING)) {
		return;
	}

	for (ai = ISC_LIST_HEAD(notify->find->list); ai != NULL;
	     ai = ISC_LIST_NEXT(ai, publink))
	{
		dst = ai->sockaddr;
		if (notify_isqueued(notify->zone, notify->flags, NULL, &dst,
				    NULL, NULL))
		{
			continue;
		}
		if (notify_isself(notify->zone, &dst)) {
			continue;
		}
		newnotify = NULL;
		flags = notify->flags & DNS_NOTIFY_NOSOA;
		result = notify_create(notify->mctx, flags, &newnotify);
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}
		zone_iattach(notify->zone, &newnotify->zone);
		ISC_LIST_APPEND(newnotify->zone->notifies, newnotify, link);
		newnotify->dst = dst;
		if (isc_sockaddr_pf(&dst) == AF_INET6) {
			isc_sockaddr_any6(&newnotify->src);
		}
		startup = ((notify->flags & DNS_NOTIFY_STARTUP) != 0);
		result = notify_send_queue(newnotify, startup);
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}
		newnotify = NULL;
	}

cleanup:
	if (newnotify != NULL) {
		notify_destroy(newnotify, true);
	}
}

void
dns_zone_notify(dns_zone_t *zone, bool nodefer) {
	isc_time_t now;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NEEDNOTIFY);
	if (nodefer) {
		if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NOTIFYDEFERRED)) {
			/*
			 * We have previously deferred the notify, but we have a
			 * new request not to defer it. Reverse the deferring
			 * operation.
			 */
			DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_NOTIFYDEFERRED);
			DNS_ZONE_TIME_SUBTRACT(&zone->notifytime,
					       zone->notifydefer,
					       &zone->notifytime);
		}
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NOTIFYNODEFER);
	}
	now = isc_time_now();
	zone_settimer(zone, &now);
	UNLOCK_ZONE(zone);
}

static void
zone_notify(dns_zone_t *zone, isc_time_t *now) {
	dns_dbnode_t *node = NULL;
	dns_db_t *zonedb = NULL;
	dns_dbversion_t *version = NULL;
	dns_name_t *origin = NULL;
	dns_name_t primary;
	dns_rdata_ns_t ns;
	dns_rdata_soa_t soa;
	uint32_t serial;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	dns_rdataset_t nsrdset;
	dns_rdataset_t soardset;
	isc_result_t result;
	isc_sockaddr_t src;
	isc_sockaddr_t dst;
	bool isqueued;
	dns_notifytype_t notifytype;
	unsigned int flags = 0;
	bool loggednotify = false;
	bool startup;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	startup = !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDNOTIFY);
	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_NEEDNOTIFY |
				       DNS_ZONEFLG_NEEDSTARTUPNOTIFY |
				       DNS_ZONEFLG_NOTIFYNODEFER |
				       DNS_ZONEFLG_NOTIFYDEFERRED);
	notifytype = zone->notifytype;
	DNS_ZONE_TIME_ADD(now, zone->notifydelay, &zone->notifytime);
	UNLOCK_ZONE(zone);

	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING) ||
	    !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADED))
	{
		return;
	}

	if (notifytype == dns_notifytype_no) {
		return;
	}

	if (notifytype == dns_notifytype_masteronly &&
	    zone->type != dns_zone_primary)
	{
		return;
	}

	origin = &zone->origin;

	/*
	 * If the zone is dialup we are done as we don't want to send
	 * the current soa so as to force a refresh query.
	 */
	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_DIALNOTIFY)) {
		flags |= DNS_NOTIFY_NOSOA;
	}

	/*
	 * Record that this was a notify due to starting up.
	 */
	if (startup) {
		flags |= DNS_NOTIFY_STARTUP;
	}

	/*
	 * Get SOA RRset.
	 */
	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	if (zone->db != NULL) {
		dns_db_attach(zone->db, &zonedb);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
	if (zonedb == NULL) {
		return;
	}
	dns_db_currentversion(zonedb, &version);
	result = dns_db_findnode(zonedb, origin, false, &node);
	if (result != ISC_R_SUCCESS) {
		goto cleanup1;
	}

	dns_rdataset_init(&soardset);
	result = dns_db_findrdataset(zonedb, node, version, dns_rdatatype_soa,
				     dns_rdatatype_none, 0, &soardset, NULL);
	if (result != ISC_R_SUCCESS) {
		goto cleanup2;
	}

	/*
	 * Find serial and primary server's name.
	 */
	dns_name_init(&primary, NULL);
	result = dns_rdataset_first(&soardset);
	if (result != ISC_R_SUCCESS) {
		goto cleanup3;
	}
	dns_rdataset_current(&soardset, &rdata);
	result = dns_rdata_tostruct(&rdata, &soa, NULL);
	RUNTIME_CHECK(result == ISC_R_SUCCESS);
	dns_rdata_reset(&rdata);
	dns_name_dup(&soa.origin, zone->mctx, &primary);
	serial = soa.serial;
	dns_rdataset_disassociate(&soardset);

	/*
	 * Enqueue notify requests for 'also-notify' servers.
	 */
	LOCK_ZONE(zone);

	dns_remote_reset(&zone->notify, false);
	while (!dns_remote_done(&zone->notify)) {
		dns_tsigkey_t *key = NULL;
		dns_transport_t *transport = NULL;
		dns_notify_t *notify = NULL;
		dns_view_t *view = dns_zone_getview(zone);

		if (dns_remote_keyname(&zone->notify) != NULL) {
			dns_name_t *keyname = dns_remote_keyname(&zone->notify);
			(void)dns_view_gettsig(view, keyname, &key);
		}

		if (dns_remote_tlsname(&zone->notify) != NULL) {
			dns_name_t *tlsname = dns_remote_tlsname(&zone->notify);
			result = dns_view_gettransport(view, DNS_TRANSPORT_TLS,
						       tlsname, &transport);

			if (result == ISC_R_SUCCESS) {
				notify_log(
					zone, ISC_LOG_INFO,
					"got TLS configuration for a notify");
			} else {
				dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
					      ISC_LOG_ERROR,
					      "could not get TLS configuration "
					      "for zone transfer: %s",
					      isc_result_totext(result));
				goto next;
			}

			flags |= DNS_NOTIFY_TCP;
		}

		/* TODO: glue the transport to the notify */

		dst = dns_remote_curraddr(&zone->notify);
		src = dns_remote_sourceaddr(&zone->notify);
		INSIST(isc_sockaddr_pf(&src) == isc_sockaddr_pf(&dst));

		if (isc_sockaddr_disabled(&dst)) {
			goto next;
		}

		if (notify_isqueued(zone, flags, NULL, &dst, key, transport)) {
			if (key != NULL) {
				dns_tsigkey_detach(&key);
			}
			if (transport != NULL) {
				dns_transport_detach(&transport);
			}
			goto next;
		}

		result = notify_create(zone->mctx, flags, &notify);
		if (result != ISC_R_SUCCESS) {
			if (key != NULL) {
				dns_tsigkey_detach(&key);
			}
			if (transport != NULL) {
				dns_transport_detach(&transport);
			}
			goto next;
		}

		zone_iattach(zone, &notify->zone);
		notify->src = src;
		notify->dst = dst;

		INSIST(notify->key == NULL);

		if (key != NULL) {
			notify->key = key;
			key = NULL;
		}

		INSIST(notify->transport == NULL);
		if (transport != NULL) {
			notify->transport = transport;
			transport = NULL;
		}

		ISC_LIST_APPEND(zone->notifies, notify, link);
		result = notify_send_queue(notify, startup);
		if (result != ISC_R_SUCCESS) {
			notify_destroy(notify, true);
		}
		if (!loggednotify) {
			notify_log(zone, ISC_LOG_INFO,
				   "sending notifies (serial %u)", serial);
			loggednotify = true;
		}
	next:
		flags &= ~DNS_NOTIFY_TCP;
		dns_remote_next(&zone->notify, false);
	}
	UNLOCK_ZONE(zone);

	if (notifytype == dns_notifytype_explicit) {
		goto cleanup3;
	}

	/*
	 * Process NS RRset to generate notifies.
	 */

	dns_rdataset_init(&nsrdset);
	result = dns_db_findrdataset(zonedb, node, version, dns_rdatatype_ns,
				     dns_rdatatype_none, 0, &nsrdset, NULL);
	if (result != ISC_R_SUCCESS) {
		goto cleanup3;
	}

	result = dns_rdataset_first(&nsrdset);
	while (result == ISC_R_SUCCESS) {
		dns_notify_t *notify = NULL;

		dns_rdataset_current(&nsrdset, &rdata);
		result = dns_rdata_tostruct(&rdata, &ns, NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
		dns_rdata_reset(&rdata);
		/*
		 * Don't notify the primary server unless explicitly
		 * configured to do so.
		 */
		if (!DNS_ZONE_OPTION(zone, DNS_ZONEOPT_NOTIFYTOSOA) &&
		    dns_name_compare(&primary, &ns.name) == 0)
		{
			result = dns_rdataset_next(&nsrdset);
			continue;
		}

		if (!loggednotify) {
			notify_log(zone, ISC_LOG_INFO,
				   "sending notifies (serial %u)", serial);
			loggednotify = true;
		}

		LOCK_ZONE(zone);
		isqueued = notify_isqueued(zone, flags, &ns.name, NULL, NULL,
					   NULL);
		UNLOCK_ZONE(zone);
		if (isqueued) {
			result = dns_rdataset_next(&nsrdset);
			continue;
		}
		result = notify_create(zone->mctx, flags, &notify);
		if (result != ISC_R_SUCCESS) {
			continue;
		}
		dns_zone_iattach(zone, &notify->zone);
		dns_name_dup(&ns.name, zone->mctx, &notify->ns);
		LOCK_ZONE(zone);
		ISC_LIST_APPEND(zone->notifies, notify, link);
		UNLOCK_ZONE(zone);
		notify_find_address(notify);
		result = dns_rdataset_next(&nsrdset);
	}
	dns_rdataset_disassociate(&nsrdset);

cleanup3:
	if (dns_name_dynamic(&primary)) {
		dns_name_free(&primary, zone->mctx);
	}
cleanup2:
	dns_db_detachnode(zonedb, &node);
cleanup1:
	dns_db_closeversion(zonedb, &version, false);
	dns_db_detach(&zonedb);
}

/***
 *** Private
 ***/
static void
create_query(dns_zone_t *zone, dns_rdatatype_t rdtype, dns_name_t *name,
	     dns_message_t **messagep) {
	dns_message_t *message = NULL;
	dns_name_t *qname = NULL;
	dns_rdataset_t *qrdataset = NULL;

	dns_message_create(zone->mctx, NULL, NULL, DNS_MESSAGE_INTENTRENDER,
			   &message);

	message->opcode = dns_opcode_query;
	message->rdclass = zone->rdclass;

	dns_message_gettempname(message, &qname);

	dns_message_gettemprdataset(message, &qrdataset);

	/*
	 * Make question.
	 */
	dns_name_clone(name, qname);
	dns_rdataset_makequestion(qrdataset, zone->rdclass, rdtype);
	ISC_LIST_APPEND(qname->list, qrdataset, link);
	dns_message_addname(message, qname, DNS_SECTION_QUESTION);

	*messagep = message;
}

static isc_result_t
add_opt(dns_message_t *message, uint16_t udpsize, bool reqnsid,
	bool reqexpire) {
	isc_result_t result;
	dns_rdataset_t *rdataset = NULL;
	dns_ednsopt_t ednsopts[DNS_EDNSOPTIONS];
	int count = 0;

	/* Set EDNS options if applicable. */
	if (reqnsid) {
		INSIST(count < DNS_EDNSOPTIONS);
		ednsopts[count].code = DNS_OPT_NSID;
		ednsopts[count].length = 0;
		ednsopts[count].value = NULL;
		count++;
	}
	if (reqexpire) {
		INSIST(count < DNS_EDNSOPTIONS);
		ednsopts[count].code = DNS_OPT_EXPIRE;
		ednsopts[count].length = 0;
		ednsopts[count].value = NULL;
		count++;
	}
	result = dns_message_buildopt(message, &rdataset, 0, udpsize, 0,
				      ednsopts, count);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	return dns_message_setopt(message, rdataset);
}

/*
 * Called when stub zone update is finished.
 * Update zone refresh, retry, expire values accordingly with
 * SOA received from primary, sync database to file, restart
 * zone management timer.
 */
static void
stub_finish_zone_update(dns_stub_t *stub, isc_time_t now) {
	uint32_t refresh, retry, expire;
	isc_result_t result;
	isc_interval_t i;
	unsigned int soacount;
	dns_zone_t *zone = stub->zone;

	/*
	 * Tidy up.
	 */
	dns_db_closeversion(stub->db, &stub->version, true);
	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_write);
	if (zone->db == NULL) {
		zone_attachdb(zone, stub->db);
	}
	result = zone_get_from_db(zone, zone->db, NULL, &soacount, NULL, NULL,
				  &refresh, &retry, &expire, NULL, NULL);
	if (result == ISC_R_SUCCESS && soacount > 0U) {
		zone->refresh = RANGE(refresh, zone->minrefresh,
				      zone->maxrefresh);
		zone->retry = RANGE(retry, zone->minretry, zone->maxretry);
		zone->expire = RANGE(expire, zone->refresh + zone->retry,
				     DNS_MAX_EXPIRE);
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_HAVETIMERS);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_write);
	dns_db_detach(&stub->db);

	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_REFRESH);
	DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_LOADED);
	DNS_ZONE_JITTER_ADD(&now, zone->refresh, &zone->refreshtime);
	isc_interval_set(&i, zone->expire, 0);
	DNS_ZONE_TIME_ADD(&now, zone->expire, &zone->expiretime);

	if (zone->masterfile != NULL) {
		zone_needdump(zone, 0);
	}

	zone_settimer(zone, &now);
}

/*
 * Process answers for A and AAAA queries when
 * resolving nameserver addresses for which glue
 * was missing in a previous answer for a NS query.
 */
static void
stub_glue_response(void *arg) {
	dns_request_t *request = (dns_request_t *)arg;
	struct stub_glue_request *sgr = dns_request_getarg(request);
	struct stub_cb_args *cb_args = sgr->args;
	dns_stub_t *stub = cb_args->stub;
	dns_message_t *msg = NULL;
	dns_zone_t *zone = NULL;
	char primary[ISC_SOCKADDR_FORMATSIZE];
	char source[ISC_SOCKADDR_FORMATSIZE];
	uint32_t addr_count, cnamecnt;
	isc_result_t result;
	isc_sockaddr_t curraddr;
	isc_time_t now;
	dns_rdataset_t *addr_rdataset = NULL;
	dns_dbnode_t *node = NULL;

	INSIST(DNS_STUB_VALID(stub));

	zone = stub->zone;

	ENTER;

	now = isc_time_now();

	LOCK_ZONE(zone);

	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING)) {
		zone_debuglog(zone, __func__, 1, "exiting");
		goto cleanup;
	}

	curraddr = dns_remote_curraddr(&zone->primaries);
	isc_sockaddr_format(&curraddr, primary, sizeof(primary));
	isc_sockaddr_format(&zone->sourceaddr, source, sizeof(source));

	if (dns_request_getresult(request) != ISC_R_SUCCESS) {
		dns_zonemgr_unreachableadd(zone->zmgr, &curraddr,
					   &zone->sourceaddr, &now);
		dns_zone_log(zone, ISC_LOG_INFO,
			     "could not refresh stub from primary %s"
			     " (source %s): %s",
			     primary, source,
			     isc_result_totext(dns_request_getresult(request)));
		goto cleanup;
	}

	dns_message_create(zone->mctx, NULL, NULL, DNS_MESSAGE_INTENTPARSE,
			   &msg);
	result = dns_request_getresponse(request, msg, 0);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_INFO,
			     "refreshing stub: unable to parse response (%s)",
			     isc_result_totext(result));
		goto cleanup;
	}

	/*
	 * Unexpected opcode.
	 */
	if (msg->opcode != dns_opcode_query) {
		char opcode[128];
		isc_buffer_t rb;

		isc_buffer_init(&rb, opcode, sizeof(opcode));
		(void)dns_opcode_totext(msg->opcode, &rb);

		dns_zone_log(zone, ISC_LOG_INFO,
			     "refreshing stub: "
			     "unexpected opcode (%.*s) from %s (source %s)",
			     (int)rb.used, opcode, primary, source);
		goto cleanup;
	}

	/*
	 * Unexpected rcode.
	 */
	if (msg->rcode != dns_rcode_noerror) {
		char rcode[128];
		isc_buffer_t rb;

		isc_buffer_init(&rb, rcode, sizeof(rcode));
		(void)dns_rcode_totext(msg->rcode, &rb);

		dns_zone_log(zone, ISC_LOG_INFO,
			     "refreshing stub: "
			     "unexpected rcode (%.*s) from %s (source %s)",
			     (int)rb.used, rcode, primary, source);
		goto cleanup;
	}

	/*
	 * We need complete messages.
	 */
	if ((msg->flags & DNS_MESSAGEFLAG_TC) != 0) {
		if (dns_request_usedtcp(request)) {
			dns_zone_log(zone, ISC_LOG_INFO,
				     "refreshing stub: truncated TCP "
				     "response from primary %s (source %s)",
				     primary, source);
		}
		goto cleanup;
	}

	/*
	 * If non-auth log.
	 */
	if ((msg->flags & DNS_MESSAGEFLAG_AA) == 0) {
		dns_zone_log(zone, ISC_LOG_INFO,
			     "refreshing stub: "
			     "non-authoritative answer from "
			     "primary %s (source %s)",
			     primary, source);
		goto cleanup;
	}

	/*
	 * Sanity checks.
	 */
	cnamecnt = message_count(msg, DNS_SECTION_ANSWER, dns_rdatatype_cname);
	addr_count = message_count(msg, DNS_SECTION_ANSWER,
				   sgr->ipv4 ? dns_rdatatype_a
					     : dns_rdatatype_aaaa);

	if (cnamecnt != 0) {
		dns_zone_log(zone, ISC_LOG_INFO,
			     "refreshing stub: unexpected CNAME response "
			     "from primary %s (source %s)",
			     primary, source);
		goto cleanup;
	}

	if (addr_count == 0) {
		dns_zone_log(zone, ISC_LOG_INFO,
			     "refreshing stub: no %s records in response "
			     "from primary %s (source %s)",
			     sgr->ipv4 ? "A" : "AAAA", primary, source);
		goto cleanup;
	}
	/*
	 * Extract A or AAAA RRset from message.
	 */
	result = dns_message_findname(msg, DNS_SECTION_ANSWER, &sgr->name,
				      sgr->ipv4 ? dns_rdatatype_a
						: dns_rdatatype_aaaa,
				      dns_rdatatype_none, NULL, &addr_rdataset);
	if (result != ISC_R_SUCCESS) {
		if (result != DNS_R_NXDOMAIN && result != DNS_R_NXRRSET) {
			char namebuf[DNS_NAME_FORMATSIZE];
			dns_name_format(&sgr->name, namebuf, sizeof(namebuf));
			dns_zone_log(
				zone, ISC_LOG_INFO,
				"refreshing stub: dns_message_findname(%s/%s) "
				"failed (%s)",
				namebuf, sgr->ipv4 ? "A" : "AAAA",
				isc_result_totext(result));
		}
		goto cleanup;
	}

	result = dns_db_findnode(stub->db, &sgr->name, true, &node);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_INFO,
			     "refreshing stub: "
			     "dns_db_findnode() failed: %s",
			     isc_result_totext(result));
		goto cleanup;
	}

	result = dns_db_addrdataset(stub->db, node, stub->version, 0,
				    addr_rdataset, 0, NULL);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_INFO,
			     "refreshing stub: "
			     "dns_db_addrdataset() failed: %s",
			     isc_result_totext(result));
	}
	dns_db_detachnode(stub->db, &node);

cleanup:
	if (msg != NULL) {
		dns_message_detach(&msg);
	}

	dns_name_free(&sgr->name, zone->mctx);
	dns_request_destroy(&sgr->request);
	isc_mem_put(zone->mctx, sgr, sizeof(*sgr));

	/* If last request, release all related resources */
	if (atomic_fetch_sub_release(&stub->pending_requests, 1) == 1) {
		isc_mem_put(zone->mctx, cb_args, sizeof(*cb_args));
		stub_finish_zone_update(stub, now);
		UNLOCK_ZONE(zone);
		stub->magic = 0;
		dns_zone_idetach(&stub->zone);
		INSIST(stub->db == NULL);
		INSIST(stub->version == NULL);
		isc_mem_put(stub->mctx, stub, sizeof(*stub));
	} else {
		UNLOCK_ZONE(zone);
	}
}

/*
 * Create and send an A or AAAA query to the primary
 * server of the stub zone given.
 */
static isc_result_t
stub_request_nameserver_address(struct stub_cb_args *args, bool ipv4,
				const dns_name_t *name) {
	dns_message_t *message = NULL;
	dns_zone_t *zone;
	isc_result_t result;
	struct stub_glue_request *sgr;
	isc_sockaddr_t curraddr;

	zone = args->stub->zone;
	sgr = isc_mem_get(zone->mctx, sizeof(*sgr));
	*sgr = (struct stub_glue_request){
		.args = args,
		.name = (dns_name_t)DNS_NAME_INITEMPTY,
		.ipv4 = ipv4,
	};

	dns_name_dup(name, zone->mctx, &sgr->name);

	create_query(zone, ipv4 ? dns_rdatatype_a : dns_rdatatype_aaaa,
		     &sgr->name, &message);

	if (!DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NOEDNS)) {
		result = add_opt(message, args->udpsize, args->reqnsid, false);
		if (result != ISC_R_SUCCESS) {
			zone_debuglog(zone, __func__, 1,
				      "unable to add opt record: %s",
				      isc_result_totext(result));
			goto fail;
		}
	}

	atomic_fetch_add_release(&args->stub->pending_requests, 1);

	curraddr = dns_remote_curraddr(&zone->primaries);
	result = dns_request_create(
		zone->view->requestmgr, message, &zone->sourceaddr, &curraddr,
		NULL, NULL, DNS_REQUESTOPT_TCP, args->tsig_key,
		args->timeout * 3, args->timeout, 2, zone->loop,
		stub_glue_response, sgr, &sgr->request);

	if (result != ISC_R_SUCCESS) {
		uint_fast32_t pr;
		pr = atomic_fetch_sub_release(&args->stub->pending_requests, 1);
		INSIST(pr > 1);
		zone_debuglog(zone, __func__, 1,
			      "dns_request_create() failed: %s",
			      isc_result_totext(result));
		goto fail;
	}

	dns_message_detach(&message);

	return ISC_R_SUCCESS;

fail:
	dns_name_free(&sgr->name, zone->mctx);
	isc_mem_put(zone->mctx, sgr, sizeof(*sgr));

	if (message != NULL) {
		dns_message_detach(&message);
	}

	return result;
}

static isc_result_t
save_nsrrset(dns_message_t *message, dns_name_t *name,
	     struct stub_cb_args *cb_args, dns_db_t *db,
	     dns_dbversion_t *version) {
	dns_rdataset_t *nsrdataset = NULL;
	dns_rdataset_t *rdataset = NULL;
	dns_dbnode_t *node = NULL;
	dns_rdata_ns_t ns;
	isc_result_t result;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	bool has_glue = false;
	dns_name_t *ns_name;
	/*
	 * List of NS entries in answer, keep names that will be used
	 * to resolve missing A/AAAA glue for each entry.
	 */
	dns_namelist_t ns_list;
	ISC_LIST_INIT(ns_list);

	/*
	 * Extract NS RRset from message.
	 */
	result = dns_message_findname(message, DNS_SECTION_ANSWER, name,
				      dns_rdatatype_ns, dns_rdatatype_none,
				      NULL, &nsrdataset);
	if (result != ISC_R_SUCCESS) {
		goto done;
	}

	/*
	 * Add NS rdataset.
	 */
	result = dns_db_findnode(db, name, true, &node);
	if (result != ISC_R_SUCCESS) {
		goto done;
	}
	result = dns_db_addrdataset(db, node, version, 0, nsrdataset, 0, NULL);
	dns_db_detachnode(db, &node);
	if (result != ISC_R_SUCCESS) {
		goto done;
	}
	/*
	 * Add glue rdatasets.
	 */
	for (result = dns_rdataset_first(nsrdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(nsrdataset))
	{
		dns_rdataset_current(nsrdataset, &rdata);
		result = dns_rdata_tostruct(&rdata, &ns, NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
		dns_rdata_reset(&rdata);

		if (!dns_name_issubdomain(&ns.name, name)) {
			continue;
		}
		rdataset = NULL;
		result = dns_message_findname(message, DNS_SECTION_ADDITIONAL,
					      &ns.name, dns_rdatatype_aaaa,
					      dns_rdatatype_none, NULL,
					      &rdataset);
		if (result == ISC_R_SUCCESS) {
			has_glue = true;
			result = dns_db_findnode(db, &ns.name, true, &node);
			if (result != ISC_R_SUCCESS) {
				goto done;
			}
			result = dns_db_addrdataset(db, node, version, 0,
						    rdataset, 0, NULL);
			dns_db_detachnode(db, &node);
			if (result != ISC_R_SUCCESS) {
				goto done;
			}
		}

		rdataset = NULL;
		result = dns_message_findname(
			message, DNS_SECTION_ADDITIONAL, &ns.name,
			dns_rdatatype_a, dns_rdatatype_none, NULL, &rdataset);
		if (result == ISC_R_SUCCESS) {
			has_glue = true;
			result = dns_db_findnode(db, &ns.name, true, &node);
			if (result != ISC_R_SUCCESS) {
				goto done;
			}
			result = dns_db_addrdataset(db, node, version, 0,
						    rdataset, 0, NULL);
			dns_db_detachnode(db, &node);
			if (result != ISC_R_SUCCESS) {
				goto done;
			}
		}

		/*
		 * If no glue is found so far, we add the name to the list to
		 * resolve the A/AAAA glue later. If any glue is found in any
		 * iteration step, this list will be discarded and only the glue
		 * provided in this message will be used.
		 */
		if (!has_glue && dns_name_issubdomain(&ns.name, name)) {
			dns_name_t *tmp_name;
			tmp_name = isc_mem_get(cb_args->stub->mctx,
					       sizeof(*tmp_name));
			dns_name_init(tmp_name, NULL);
			dns_name_dup(&ns.name, cb_args->stub->mctx, tmp_name);
			ISC_LIST_APPEND(ns_list, tmp_name, link);
		}
	}

	if (result != ISC_R_NOMORE) {
		goto done;
	}

	/*
	 * If no glue records were found, we attempt to resolve A/AAAA
	 * for each NS entry found in the answer.
	 */
	if (!has_glue) {
		for (ns_name = ISC_LIST_HEAD(ns_list); ns_name != NULL;
		     ns_name = ISC_LIST_NEXT(ns_name, link))
		{
			/*
			 * Resolve NS IPv4 address/A.
			 */
			result = stub_request_nameserver_address(cb_args, true,
								 ns_name);
			if (result != ISC_R_SUCCESS) {
				goto done;
			}
			/*
			 * Resolve NS IPv6 address/AAAA.
			 */
			result = stub_request_nameserver_address(cb_args, false,
								 ns_name);
			if (result != ISC_R_SUCCESS) {
				goto done;
			}
		}
	}

	result = ISC_R_SUCCESS;

done:
	while ((ns_name = ISC_LIST_HEAD(ns_list)) != NULL) {
		ISC_LIST_UNLINK(ns_list, ns_name, link);
		dns_name_free(ns_name, cb_args->stub->mctx);
		isc_mem_put(cb_args->stub->mctx, ns_name, sizeof(*ns_name));
	}
	return result;
}

static void
stub_callback(void *arg) {
	dns_request_t *request = (dns_request_t *)arg;
	struct stub_cb_args *cb_args = dns_request_getarg(request);
	dns_stub_t *stub = cb_args->stub;
	dns_message_t *msg = NULL;
	dns_zone_t *zone = NULL;
	char primary[ISC_SOCKADDR_FORMATSIZE];
	char source[ISC_SOCKADDR_FORMATSIZE];
	uint32_t nscnt, cnamecnt;
	isc_result_t result;
	isc_sockaddr_t curraddr;
	isc_time_t now;
	bool exiting = false;

	INSIST(DNS_STUB_VALID(stub));

	zone = stub->zone;

	ENTER;

	now = isc_time_now();

	LOCK_ZONE(zone);

	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING)) {
		goto exiting;
	}

	curraddr = dns_remote_curraddr(&zone->primaries);
	isc_sockaddr_format(&curraddr, primary, sizeof(primary));
	isc_sockaddr_format(&zone->sourceaddr, source, sizeof(source));

	result = dns_request_getresult(request);
	switch (result) {
	case ISC_R_SUCCESS:
		break;
	case ISC_R_SHUTTINGDOWN:
	case ISC_R_CANCELED:
		goto exiting;
	case ISC_R_TIMEDOUT:
		if (!DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NOEDNS)) {
			DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NOEDNS);
			dns_zone_log(zone, ISC_LOG_DEBUG(1),
				     "refreshing stub: timeout retrying "
				     "without EDNS primary %s (source %s)",
				     primary, source);
			goto same_primary;
		}
		FALLTHROUGH;
	default:
		dns_zonemgr_unreachableadd(zone->zmgr, &curraddr,
					   &zone->sourceaddr, &now);
		dns_zone_log(zone, ISC_LOG_INFO,
			     "could not refresh stub from primary "
			     "%s (source %s): %s",
			     primary, source, isc_result_totext(result));
		goto next_primary;
	}

	dns_message_create(zone->mctx, NULL, NULL, DNS_MESSAGE_INTENTPARSE,
			   &msg);

	result = dns_request_getresponse(request, msg, 0);
	if (result != ISC_R_SUCCESS) {
		goto next_primary;
	}

	/*
	 * Unexpected opcode.
	 */
	if (msg->opcode != dns_opcode_query) {
		char opcode[128];
		isc_buffer_t rb;

		isc_buffer_init(&rb, opcode, sizeof(opcode));
		(void)dns_opcode_totext(msg->opcode, &rb);

		dns_zone_log(zone, ISC_LOG_INFO,
			     "refreshing stub: "
			     "unexpected opcode (%.*s) from %s (source %s)",
			     (int)rb.used, opcode, primary, source);
		goto next_primary;
	}

	/*
	 * Unexpected rcode.
	 */
	if (msg->rcode != dns_rcode_noerror) {
		char rcode[128];
		isc_buffer_t rb;

		isc_buffer_init(&rb, rcode, sizeof(rcode));
		(void)dns_rcode_totext(msg->rcode, &rb);

		if (!DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NOEDNS) &&
		    (msg->rcode == dns_rcode_servfail ||
		     msg->rcode == dns_rcode_notimp ||
		     (msg->rcode == dns_rcode_formerr && msg->opt == NULL)))
		{
			dns_zone_log(zone, ISC_LOG_DEBUG(1),
				     "refreshing stub: rcode (%.*s) retrying "
				     "without EDNS primary %s (source %s)",
				     (int)rb.used, rcode, primary, source);
			DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NOEDNS);
			goto same_primary;
		}

		dns_zone_log(zone, ISC_LOG_INFO,
			     "refreshing stub: "
			     "unexpected rcode (%.*s) from %s (source %s)",
			     (int)rb.used, rcode, primary, source);
		goto next_primary;
	}

	/*
	 * We need complete messages.
	 */
	if ((msg->flags & DNS_MESSAGEFLAG_TC) != 0) {
		if (dns_request_usedtcp(request)) {
			dns_zone_log(zone, ISC_LOG_INFO,
				     "refreshing stub: truncated TCP "
				     "response from primary %s (source %s)",
				     primary, source);
			goto next_primary;
		}
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_USEVC);
		goto same_primary;
	}

	/*
	 * If non-auth log and next primary.
	 */
	if ((msg->flags & DNS_MESSAGEFLAG_AA) == 0) {
		dns_zone_log(zone, ISC_LOG_INFO,
			     "refreshing stub: "
			     "non-authoritative answer from "
			     "primary %s (source %s)",
			     primary, source);
		goto next_primary;
	}

	/*
	 * Sanity checks.
	 */
	cnamecnt = message_count(msg, DNS_SECTION_ANSWER, dns_rdatatype_cname);
	nscnt = message_count(msg, DNS_SECTION_ANSWER, dns_rdatatype_ns);

	if (cnamecnt != 0) {
		dns_zone_log(zone, ISC_LOG_INFO,
			     "refreshing stub: unexpected CNAME response "
			     "from primary %s (source %s)",
			     primary, source);
		goto next_primary;
	}

	if (nscnt == 0) {
		dns_zone_log(zone, ISC_LOG_INFO,
			     "refreshing stub: no NS records in response "
			     "from primary %s (source %s)",
			     primary, source);
		goto next_primary;
	}

	atomic_fetch_add(&stub->pending_requests, 1);

	/*
	 * Save answer.
	 */
	result = save_nsrrset(msg, &zone->origin, cb_args, stub->db,
			      stub->version);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_INFO,
			     "refreshing stub: unable to save NS records "
			     "from primary %s (source %s)",
			     primary, source);
		goto next_primary;
	}

	dns_message_detach(&msg);
	dns_request_destroy(&zone->request);

	/*
	 * Check to see if there are no outstanding requests and
	 * finish off if that is so.
	 */
	if (atomic_fetch_sub(&stub->pending_requests, 1) == 1) {
		isc_mem_put(zone->mctx, cb_args, sizeof(*cb_args));
		stub_finish_zone_update(stub, now);
		goto free_stub;
	}

	UNLOCK_ZONE(zone);
	return;

exiting:
	zone_debuglog(zone, __func__, 1, "exiting");
	exiting = true;

next_primary:
	isc_mem_put(zone->mctx, cb_args, sizeof(*cb_args));
	if (stub->version != NULL) {
		dns_db_closeversion(stub->db, &stub->version, false);
	}
	if (stub->db != NULL) {
		dns_db_detach(&stub->db);
	}
	if (msg != NULL) {
		dns_message_detach(&msg);
	}
	dns_request_destroy(&zone->request);
	/*
	 * Skip to next failed / untried primary.
	 */
	dns_remote_next(&zone->primaries, true);
	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_NOEDNS);
	if (exiting || dns_remote_done(&zone->primaries)) {
		DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_REFRESH);
		zone_settimer(zone, &now);
		goto free_stub;
	}
	queue_soa_query(zone);
	goto free_stub;

same_primary:
	isc_mem_put(zone->mctx, cb_args, sizeof(*cb_args));
	if (msg != NULL) {
		dns_message_detach(&msg);
	}
	dns_request_destroy(&zone->request);
	ns_query(zone, NULL, stub);
	UNLOCK_ZONE(zone);
	return;

free_stub:
	UNLOCK_ZONE(zone);
	stub->magic = 0;
	dns_zone_idetach(&stub->zone);
	INSIST(stub->db == NULL);
	INSIST(stub->version == NULL);
	isc_mem_put(stub->mctx, stub, sizeof(*stub));
}

/*
 * Get the EDNS EXPIRE option from the response and if it exists trim
 * expire to be not more than it.
 */
static void
get_edns_expire(dns_zone_t *zone, dns_message_t *message, uint32_t *expirep) {
	isc_result_t result;
	uint32_t expire;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	isc_buffer_t optbuf;
	uint16_t optcode;
	uint16_t optlen;

	REQUIRE(expirep != NULL);
	REQUIRE(message != NULL);

	if (message->opt == NULL) {
		return;
	}

	result = dns_rdataset_first(message->opt);
	if (result == ISC_R_SUCCESS) {
		dns_rdataset_current(message->opt, &rdata);
		isc_buffer_init(&optbuf, rdata.data, rdata.length);
		isc_buffer_add(&optbuf, rdata.length);
		while (isc_buffer_remaininglength(&optbuf) >= 4) {
			optcode = isc_buffer_getuint16(&optbuf);
			optlen = isc_buffer_getuint16(&optbuf);
			/*
			 * A EDNS EXPIRE response has a length of 4.
			 */
			if (optcode != DNS_OPT_EXPIRE || optlen != 4) {
				isc_buffer_forward(&optbuf, optlen);
				continue;
			}
			expire = isc_buffer_getuint32(&optbuf);
			dns_zone_log(zone, ISC_LOG_DEBUG(1),
				     "got EDNS EXPIRE of %u", expire);
			/*
			 * Trim *expirep?
			 */
			if (expire < *expirep) {
				*expirep = expire;
			}
			break;
		}
	}
}

/*
 * Set the file modification time zone->expire seconds before expiretime.
 */
static void
setmodtime(dns_zone_t *zone, isc_time_t *expiretime) {
	isc_result_t result;
	isc_time_t when;
	isc_interval_t i;

	isc_interval_set(&i, zone->expire, 0);
	result = isc_time_subtract(expiretime, &i, &when);
	if (result != ISC_R_SUCCESS) {
		return;
	}

	result = ISC_R_FAILURE;
	if (zone->journal != NULL) {
		result = isc_file_settime(zone->journal, &when);
	}
	if (result == ISC_R_SUCCESS &&
	    !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDDUMP) &&
	    !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_DUMPING))
	{
		result = isc_file_settime(zone->masterfile, &when);
	} else if (result != ISC_R_SUCCESS) {
		result = isc_file_settime(zone->masterfile, &when);
	}

	/*
	 * Someone removed the file from underneath us!
	 */
	if (result == ISC_R_FILENOTFOUND) {
		zone_needdump(zone, DNS_DUMP_DELAY);
	} else if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "refresh: could not set "
			     "file modification time of '%s': %s",
			     zone->masterfile, isc_result_totext(result));
	}
}

/*
 * An SOA query has finished (successfully or not).
 */
static void
refresh_callback(void *arg) {
	dns_request_t *request = (dns_request_t *)arg;
	dns_zone_t *zone = dns_request_getarg(request);
	dns_message_t *msg = NULL;
	uint32_t soacnt, cnamecnt, soacount, nscount;
	isc_time_t now;
	char primary[ISC_SOCKADDR_FORMATSIZE];
	char source[ISC_SOCKADDR_FORMATSIZE];
	dns_rdataset_t *rdataset = NULL;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	dns_rdata_soa_t soa;
	isc_result_t result;
	const isc_result_t eresult = dns_request_getresult(request);
	isc_sockaddr_t curraddr;
	uint32_t serial, oldserial = 0;
	bool do_queue_xfrin = false;

	INSIST(DNS_ZONE_VALID(zone));

	ENTER;

	if (isc_log_wouldlog(dns_lctx, ISC_LOG_DEBUG(3))) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_DEBUG(3),
			      "refresh: request result: %s",
			      isc_result_totext(eresult));
	}

	now = isc_time_now();

	LOCK_ZONE(zone);

	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING)) {
		goto exiting;
	}

	/*
	 * If timeout, log and try the next primary
	 */
	curraddr = dns_remote_curraddr(&zone->primaries);
	isc_sockaddr_format(&curraddr, primary, sizeof(primary));
	isc_sockaddr_format(&zone->sourceaddr, source, sizeof(source));

	switch (eresult) {
	case ISC_R_SUCCESS:
		break;
	case ISC_R_SHUTTINGDOWN:
	case ISC_R_CANCELED:
		goto exiting;
	case ISC_R_TIMEDOUT:
		if (!DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NOEDNS)) {
			DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NOEDNS);
			dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
				      ISC_LOG_DEBUG(1),
				      "refresh: timeout retrying without EDNS "
				      "primary %s (source %s)",
				      primary, source);
			goto same_primary;
		} else if (!dns_request_usedtcp(request)) {
			dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
				      ISC_LOG_INFO,
				      "refresh: retry limit for "
				      "primary %s exceeded (source %s)",
				      primary, source);
			/* Try with secondary with TCP. */
			if ((zone->type == dns_zone_secondary ||
			     zone->type == dns_zone_mirror ||
			     zone->type == dns_zone_redirect) &&
			    DNS_ZONE_OPTION(zone, DNS_ZONEOPT_TRYTCPREFRESH))
			{
				if (!dns_zonemgr_unreachable(
					    zone->zmgr, &curraddr,
					    &zone->sourceaddr, &now))
				{
					DNS_ZONE_SETFLAG(
						zone,
						DNS_ZONEFLG_SOABEFOREAXFR);
					goto tcp_transfer;
				}
				dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
					      ISC_LOG_DEBUG(1),
					      "refresh: skipped tcp fallback "
					      "as primary %s (source %s) is "
					      "unreachable (cached)",
					      primary, source);
			}
			goto next_primary;
		}
		FALLTHROUGH;
	default:
		result = eresult;
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_INFO,
			      "refresh: failure trying primary "
			      "%s (source %s): %s",
			      primary, source, isc_result_totext(result));
		goto next_primary;
	}

	dns_message_create(zone->mctx, NULL, NULL, DNS_MESSAGE_INTENTPARSE,
			   &msg);
	result = dns_request_getresponse(request, msg, 0);
	if (result != ISC_R_SUCCESS) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_INFO,
			      "refresh: failure trying primary "
			      "%s (source %s): %s",
			      primary, source, isc_result_totext(result));
		goto next_primary;
	}

	/*
	 * Unexpected opcode.
	 */
	if (msg->opcode != dns_opcode_query) {
		char opcode[128];
		isc_buffer_t rb;

		isc_buffer_init(&rb, opcode, sizeof(opcode));
		(void)dns_opcode_totext(msg->opcode, &rb);

		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_INFO,
			      "refresh: "
			      "unexpected opcode (%.*s) from %s (source %s)",
			      (int)rb.used, opcode, primary, source);
		goto next_primary;
	}

	/*
	 * Unexpected rcode.
	 */
	if (msg->rcode != dns_rcode_noerror) {
		char rcode[128];
		isc_buffer_t rb;

		isc_buffer_init(&rb, rcode, sizeof(rcode));
		(void)dns_rcode_totext(msg->rcode, &rb);

		if (!DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NOEDNS) &&
		    (msg->rcode == dns_rcode_servfail ||
		     msg->rcode == dns_rcode_notimp ||
		     (msg->rcode == dns_rcode_formerr && msg->opt == NULL)))
		{
			dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
				      ISC_LOG_DEBUG(1),
				      "refresh: rcode (%.*s) retrying without "
				      "EDNS primary %s (source %s)",
				      (int)rb.used, rcode, primary, source);
			DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NOEDNS);
			goto same_primary;
		}
		if (!DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NOEDNS) &&
		    msg->rcode == dns_rcode_badvers)
		{
			dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
				      ISC_LOG_DEBUG(1),
				      "refresh: rcode (%.*s) retrying without "
				      "EDNS EXPIRE OPTION primary %s "
				      "(source %s)",
				      (int)rb.used, rcode, primary, source);
			DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NOEDNS);
			goto same_primary;
		}
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_INFO,
			      "refresh: unexpected rcode (%.*s) from "
			      "primary %s (source %s)",
			      (int)rb.used, rcode, primary, source);
		/*
		 * Perhaps AXFR/IXFR is allowed even if SOA queries aren't.
		 */
		if (msg->rcode == dns_rcode_refused &&
		    (zone->type == dns_zone_secondary ||
		     zone->type == dns_zone_mirror ||
		     zone->type == dns_zone_redirect))
		{
			goto tcp_transfer;
		}
		goto next_primary;
	}

	/*
	 * If truncated punt to zone transfer which will query again.
	 */
	if ((msg->flags & DNS_MESSAGEFLAG_TC) != 0) {
		if (zone->type == dns_zone_secondary ||
		    zone->type == dns_zone_mirror ||
		    zone->type == dns_zone_redirect)
		{
			dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
				      ISC_LOG_INFO,
				      "refresh: truncated UDP answer, "
				      "initiating TCP zone xfer "
				      "for primary %s (source %s)",
				      primary, source);
			DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_SOABEFOREAXFR);
			goto tcp_transfer;
		} else {
			INSIST(zone->type == dns_zone_stub);
			if (dns_request_usedtcp(request)) {
				dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
					      ISC_LOG_INFO,
					      "refresh: truncated TCP response "
					      "from primary %s (source %s)",
					      primary, source);
				goto next_primary;
			}
			DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_USEVC);
			goto same_primary;
		}
	}

	/*
	 * If non-auth, log and try the next primary
	 */
	if ((msg->flags & DNS_MESSAGEFLAG_AA) == 0) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_INFO,
			      "refresh: non-authoritative answer from "
			      "primary %s (source %s)",
			      primary, source);
		goto next_primary;
	}

	cnamecnt = message_count(msg, DNS_SECTION_ANSWER, dns_rdatatype_cname);
	soacnt = message_count(msg, DNS_SECTION_ANSWER, dns_rdatatype_soa);
	nscount = message_count(msg, DNS_SECTION_AUTHORITY, dns_rdatatype_ns);
	soacount = message_count(msg, DNS_SECTION_AUTHORITY, dns_rdatatype_soa);

	/*
	 * There should not be a CNAME record at top of zone.
	 */
	if (cnamecnt != 0) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_INFO,
			      "refresh: CNAME at top of zone "
			      "in primary %s (source %s)",
			      primary, source);
		goto next_primary;
	}

	/*
	 * If referral, log and try the next primary;
	 */
	if (soacnt == 0 && soacount == 0 && nscount != 0) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_INFO,
			      "refresh: referral response "
			      "from primary %s (source %s)",
			      primary, source);
		goto next_primary;
	}

	/*
	 * If nodata, log and try the next primary;
	 */
	if (soacnt == 0 && (nscount == 0 || soacount != 0)) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_INFO,
			      "refresh: NODATA response "
			      "from primary %s (source %s)",
			      primary, source);
		goto next_primary;
	}

	/*
	 * Only one soa at top of zone.
	 */
	if (soacnt != 1) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_INFO,
			      "refresh: answer SOA count (%d) != 1 "
			      "from primary %s (source %s)",
			      soacnt, primary, source);
		goto next_primary;
	}

	/*
	 * Extract serial
	 */
	rdataset = NULL;
	result = dns_message_findname(msg, DNS_SECTION_ANSWER, &zone->origin,
				      dns_rdatatype_soa, dns_rdatatype_none,
				      NULL, &rdataset);
	if (result != ISC_R_SUCCESS) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_INFO,
			      "refresh: unable to get SOA record "
			      "from primary %s (source %s)",
			      primary, source);
		goto next_primary;
	}

	result = dns_rdataset_first(rdataset);
	if (result != ISC_R_SUCCESS) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_INFO,
			      "refresh: dns_rdataset_first() failed");
		goto next_primary;
	}

	dns_rdataset_current(rdataset, &rdata);
	result = dns_rdata_tostruct(&rdata, &soa, NULL);
	RUNTIME_CHECK(result == ISC_R_SUCCESS);

	serial = soa.serial;
	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADED)) {
		unsigned int dbsoacount;
		result = zone_get_from_db(zone, zone->db, NULL, &dbsoacount,
					  NULL, &oldserial, NULL, NULL, NULL,
					  NULL, NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
		RUNTIME_CHECK(dbsoacount > 0U);
		zone_debuglogc(zone, DNS_LOGCATEGORY_XFER_IN, __func__, 1,
			       "serial: new %u, old %u", serial, oldserial);
	} else {
		zone_debuglogc(zone, DNS_LOGCATEGORY_XFER_IN, __func__, 1,
			       "serial: new %u, old not loaded", serial);
	}

	if (!DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADED) ||
	    DNS_ZONE_FLAG(zone, DNS_ZONEFLG_FORCEXFER) ||
	    isc_serial_gt(serial, oldserial))
	{
		if (dns_zonemgr_unreachable(zone->zmgr, &curraddr,
					    &zone->sourceaddr, &now))
		{
			dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
				      ISC_LOG_INFO,
				      "refresh: skipping %s as primary %s "
				      "(source %s) is unreachable (cached)",
				      (zone->type == dns_zone_secondary ||
				       zone->type == dns_zone_mirror ||
				       zone->type == dns_zone_redirect)
					      ? "zone transfer"
					      : "NS query",
				      primary, source);
			goto next_primary;
		}
	tcp_transfer:
		dns_request_destroy(&zone->request);
		if (zone->type == dns_zone_secondary ||
		    zone->type == dns_zone_mirror ||
		    zone->type == dns_zone_redirect)
		{
			do_queue_xfrin = true;
		} else {
			INSIST(zone->type == dns_zone_stub);
			ns_query(zone, rdataset, NULL);
		}
		if (msg != NULL) {
			dns_message_detach(&msg);
		}
	} else if (isc_serial_eq(soa.serial, oldserial)) {
		isc_time_t expiretime;
		uint32_t expire;

		/*
		 * Compute the new expire time based on this response.
		 */
		expire = zone->expire;
		get_edns_expire(zone, msg, &expire);
		DNS_ZONE_TIME_ADD(&now, expire, &expiretime);

		/*
		 * Has the expire time improved?
		 */
		if (isc_time_compare(&expiretime, &zone->expiretime) > 0) {
			zone->expiretime = expiretime;
			if (zone->masterfile != NULL) {
				setmodtime(zone, &expiretime);
			}
		}

		DNS_ZONE_JITTER_ADD(&now, zone->refresh, &zone->refreshtime);
		dns_remote_mark(&zone->primaries, true);
		goto next_primary;
	} else {
		if (!DNS_ZONE_OPTION(zone, DNS_ZONEOPT_MULTIMASTER)) {
			dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
				      ISC_LOG_INFO,
				      "serial number (%u) "
				      "received from primary %s < ours (%u)",
				      soa.serial, primary, oldserial);
		} else {
			zone_debuglogc(zone, DNS_LOGCATEGORY_XFER_IN, __func__,
				       1, "ahead");
		}
		dns_remote_mark(&zone->primaries, true);
		goto next_primary;
	}
	if (msg != NULL) {
		dns_message_detach(&msg);
	}
	goto detach;

next_primary:
	if (msg != NULL) {
		dns_message_detach(&msg);
	}
	dns_request_destroy(&zone->request);
	/*
	 * Skip to next failed / untried primary.
	 */
	dns_remote_next(&zone->primaries, true);
	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_NOEDNS);
	if (dns_remote_done(&zone->primaries)) {
		DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_REFRESH);
		if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDREFRESH)) {
			DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_NEEDREFRESH);
			zone->refreshtime = now;
		}
		zone_settimer(zone, &now);
		goto detach;
	}

	queue_soa_query(zone);
	goto detach;

exiting:
	/*
	 * We can get here not only during shutdown, but also when the refresh
	 * is canceled during reconfiguration. In that case, make sure to clear
	 * the DNS_ZONEFLG_REFRESH flag so that future zone refreshes don't get
	 * stuck, and make sure a new refresh attempt is made again soon after
	 * the reconfiguration is complete.
	 */
	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_REFRESH);
	zone->refreshtime = now;
	zone_settimer(zone, &now);

	dns_request_destroy(&zone->request);
	goto detach;

same_primary:
	if (msg != NULL) {
		dns_message_detach(&msg);
	}
	dns_request_destroy(&zone->request);
	queue_soa_query(zone);

detach:
	if (do_queue_xfrin) {
		/* Shows in the statistics channel the duration of the step. */
		zone->xfrintime = isc_time_now();
	}
	UNLOCK_ZONE(zone);
	if (do_queue_xfrin) {
		queue_xfrin(zone);
	}
	dns_zone_idetach(&zone);
	return;
}

struct soaquery {
	dns_zone_t *zone;
	isc_rlevent_t *rlevent;
};

static void
queue_soa_query(dns_zone_t *zone) {
	isc_result_t result;
	struct soaquery *sq = NULL;

	ENTER;
	/*
	 * Locked by caller
	 */
	REQUIRE(LOCKED_ZONE(zone));

	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING)) {
		cancel_refresh(zone);
		return;
	}

	sq = isc_mem_get(zone->mctx, sizeof(*sq));
	*sq = (struct soaquery){ .zone = NULL };

	/* Shows in the statistics channel the duration of the current step. */
	zone->xfrintime = isc_time_now();

	/*
	 * Attach so that we won't clean up until the event is delivered.
	 */
	zone_iattach(zone, &sq->zone);
	result = isc_ratelimiter_enqueue(zone->zmgr->refreshrl, zone->loop,
					 soa_query, sq, &sq->rlevent);
	if (result != ISC_R_SUCCESS) {
		zone_idetach(&sq->zone);
		isc_mem_put(zone->mctx, sq, sizeof(*sq));
		cancel_refresh(zone);
	}
}

static void
soa_query(void *arg) {
	struct soaquery *sq = (struct soaquery *)arg;
	dns_zone_t *zone = sq->zone;
	isc_result_t result = ISC_R_FAILURE;
	dns_message_t *message = NULL;
	isc_netaddr_t primaryip;
	dns_tsigkey_t *key = NULL;
	dns_transport_t *transport = NULL;
	uint32_t options;
	bool cancel = true;
	int timeout;
	bool have_xfrsource = false, reqnsid, reqexpire;
	uint16_t udpsize = SEND_BUFFER_SIZE;
	isc_sockaddr_t curraddr, sourceaddr;
	bool do_queue_xfrin = false;

	REQUIRE(DNS_ZONE_VALID(zone));

	ENTER;

	LOCK_ZONE(zone);
	if (sq->rlevent->canceled || DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING) ||
	    zone->view->requestmgr == NULL)
	{
		if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING)) {
			cancel = false;
		}
		goto cleanup;
	}

again:
	dns_zone_logc(
		zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_DEBUG(3),
		"soa_query: remote server current address index %d count %d",
		zone->primaries.curraddr, zone->primaries.addrcnt);
	INSIST(dns_remote_count(&zone->primaries) > 0);
	INSIST(!dns_remote_done(&zone->primaries));

	sourceaddr = dns_remote_sourceaddr(&zone->primaries);
	curraddr = dns_remote_curraddr(&zone->primaries);
	isc_netaddr_fromsockaddr(&primaryip, &curraddr);

	if (isc_sockaddr_disabled(&curraddr)) {
		goto skip_primary;
	}

	/*
	 * First, look for a tsig key in the primaries statement, then
	 * try for a server key.
	 */
	if (dns_remote_keyname(&zone->primaries) != NULL) {
		dns_view_t *view = dns_zone_getview(zone);
		dns_name_t *keyname = dns_remote_keyname(&zone->primaries);
		result = dns_view_gettsig(view, keyname, &key);
		if (result != ISC_R_SUCCESS) {
			char namebuf[DNS_NAME_FORMATSIZE];
			dns_name_format(keyname, namebuf, sizeof(namebuf));
			dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
				      ISC_LOG_ERROR, "unable to find key: %s",
				      namebuf);
			goto skip_primary;
		}
	}
	if (key == NULL) {
		result = dns_view_getpeertsig(zone->view, &primaryip, &key);
		if (result != ISC_R_SUCCESS && result != ISC_R_NOTFOUND) {
			char addrbuf[ISC_NETADDR_FORMATSIZE];
			isc_netaddr_format(&primaryip, addrbuf,
					   sizeof(addrbuf));
			dns_zone_logc(
				zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_ERROR,
				"unable to find TSIG key for %s", addrbuf);
			goto skip_primary;
		}
	}

	if (dns_remote_tlsname(&zone->primaries) != NULL) {
		dns_view_t *view = dns_zone_getview(zone);
		dns_name_t *tlsname = dns_remote_tlsname(&zone->primaries);
		result = dns_view_gettransport(view, DNS_TRANSPORT_TLS, tlsname,
					       &transport);
		if (result != ISC_R_SUCCESS) {
			char namebuf[DNS_NAME_FORMATSIZE];
			dns_name_format(tlsname, namebuf, sizeof(namebuf));
			dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
				      ISC_LOG_ERROR,
				      "unable to find TLS configuration: %s",
				      namebuf);
			goto skip_primary;
		}
	}

	options = DNS_ZONE_FLAG(zone, DNS_ZONEFLG_USEVC) ? DNS_REQUESTOPT_TCP
							 : 0;
	reqnsid = zone->view->requestnsid;
	reqexpire = zone->requestexpire;
	if (zone->view->peers != NULL) {
		dns_peer_t *peer = NULL;
		bool edns, usetcp;
		result = dns_peerlist_peerbyaddr(zone->view->peers, &primaryip,
						 &peer);
		if (result == ISC_R_SUCCESS) {
			result = dns_peer_getsupportedns(peer, &edns);
			if (result == ISC_R_SUCCESS && !edns) {
				DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NOEDNS);
			}
			result = dns_peer_gettransfersource(peer,
							    &zone->sourceaddr);
			if (result == ISC_R_SUCCESS) {
				have_xfrsource = true;
			}
			udpsize = dns_view_getudpsize(zone->view);
			(void)dns_peer_getudpsize(peer, &udpsize);
			(void)dns_peer_getrequestnsid(peer, &reqnsid);
			(void)dns_peer_getrequestexpire(peer, &reqexpire);
			result = dns_peer_getforcetcp(peer, &usetcp);
			if (result == ISC_R_SUCCESS && usetcp) {
				options |= DNS_REQUESTOPT_TCP;
			}
		}
	}

	switch (isc_sockaddr_pf(&curraddr)) {
	case PF_INET:
		if (!have_xfrsource) {
			isc_sockaddr_t any;
			isc_sockaddr_any(&any);

			zone->sourceaddr = sourceaddr;
			if (isc_sockaddr_equal(&sourceaddr, &any)) {
				zone->sourceaddr = zone->xfrsource4;
			}
		}
		break;
	case PF_INET6:
		if (!have_xfrsource) {
			isc_sockaddr_t any;
			isc_sockaddr_any6(&any);

			zone->sourceaddr = sourceaddr;
			if (isc_sockaddr_equal(&zone->sourceaddr, &any)) {
				zone->sourceaddr = zone->xfrsource6;
			}
		}
		break;
	default:
		result = ISC_R_NOTIMPLEMENTED;
		goto cleanup;
	}

	/*
	 * FIXME(OS): This is a bit hackish, but it enforces the SOA query to go
	 * through the XFR channel instead of doing dns_request that doesn't
	 * have DoT support yet.
	 */
	if (transport != NULL) {
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_SOABEFOREAXFR);
		do_queue_xfrin = true;
		cancel = false;
		result = ISC_R_SUCCESS;
		goto cleanup;
	}

	create_query(zone, dns_rdatatype_soa, &zone->origin, &message);

	if (!DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NOEDNS)) {
		result = add_opt(message, udpsize, reqnsid, reqexpire);
		if (result != ISC_R_SUCCESS) {
			zone_debuglogc(zone, DNS_LOGCATEGORY_XFER_IN, __func__,
				       1, "unable to add opt record: %s",
				       isc_result_totext(result));
		}
	}

	zone_iattach(zone, &(dns_zone_t *){ NULL });
	timeout = 5;
	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_DIALREFRESH)) {
		timeout = 30;
	}
	result = dns_request_create(
		zone->view->requestmgr, message, &zone->sourceaddr, &curraddr,
		NULL, NULL, options, key, timeout * 3 + 1, timeout, 2,
		zone->loop, refresh_callback, zone, &zone->request);
	if (result != ISC_R_SUCCESS) {
		zone_idetach(&(dns_zone_t *){ zone });
		zone_debuglogc(zone, DNS_LOGCATEGORY_XFER_IN, __func__, 1,
			       "dns_request_create() failed: %s",
			       isc_result_totext(result));
		goto skip_primary;
	} else {
		/* Shows in the statistics channel the duration of the query. */
		zone->xfrintime = isc_time_now();

		if (isc_sockaddr_pf(&curraddr) == PF_INET) {
			inc_stats(zone, dns_zonestatscounter_soaoutv4);
		} else {
			inc_stats(zone, dns_zonestatscounter_soaoutv6);
		}
	}
	cancel = false;
cleanup:
	if (transport != NULL) {
		dns_transport_detach(&transport);
	}
	if (key != NULL) {
		dns_tsigkey_detach(&key);
	}
	if (result != ISC_R_SUCCESS) {
		DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_REFRESH);
	}
	if (message != NULL) {
		dns_message_detach(&message);
	}
	if (cancel) {
		cancel_refresh(zone);
	}
	if (do_queue_xfrin) {
		/* Shows in the statistics channel the duration of the step. */
		zone->xfrintime = isc_time_now();
	}
	UNLOCK_ZONE(zone);
	if (do_queue_xfrin) {
		queue_xfrin(zone);
	}
	isc_rlevent_free(&sq->rlevent);
	isc_mem_put(zone->mctx, sq, sizeof(*sq));
	dns_zone_idetach(&zone);
	return;

skip_primary:
	if (transport != NULL) {
		dns_transport_detach(&transport);
	}
	if (key != NULL) {
		dns_tsigkey_detach(&key);
	}
	if (message != NULL) {
		dns_message_detach(&message);
	}
	/*
	 * Skip to next failed / untried primary.
	 */
	dns_remote_next(&zone->primaries, true);
	if (!dns_remote_done(&zone->primaries)) {
		goto again;
	}
	dns_remote_reset(&zone->primaries, false);
	goto cleanup;
}

static void
ns_query(dns_zone_t *zone, dns_rdataset_t *soardataset, dns_stub_t *stub) {
	isc_result_t result;
	dns_message_t *message = NULL;
	isc_netaddr_t primaryip;
	dns_tsigkey_t *key = NULL;
	dns_dbnode_t *node = NULL;
	int timeout;
	bool have_xfrsource = false;
	bool reqnsid;
	uint16_t udpsize = SEND_BUFFER_SIZE;
	isc_sockaddr_t curraddr, sourceaddr;
	struct stub_cb_args *cb_args;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(LOCKED_ZONE(zone));
	REQUIRE((soardataset != NULL && stub == NULL) ||
		(soardataset == NULL && stub != NULL));
	REQUIRE(stub == NULL || DNS_STUB_VALID(stub));

	ENTER;

	if (stub == NULL) {
		stub = isc_mem_get(zone->mctx, sizeof(*stub));
		stub->magic = STUB_MAGIC;
		stub->mctx = zone->mctx;
		stub->zone = NULL;
		stub->db = NULL;
		stub->version = NULL;
		atomic_init(&stub->pending_requests, 0);

		/*
		 * Attach so that the zone won't disappear from under us.
		 */
		zone_iattach(zone, &stub->zone);

		/*
		 * If a db exists we will update it, otherwise we create a
		 * new one and attach it to the zone once we have the NS
		 * RRset and glue.
		 */
		ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
		if (zone->db != NULL) {
			dns_db_attach(zone->db, &stub->db);
			ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
		} else {
			ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);

			INSIST(zone->db_argc >= 1);
			result = dns_db_create(zone->mctx, zone->db_argv[0],
					       &zone->origin, dns_dbtype_stub,
					       zone->rdclass, zone->db_argc - 1,
					       zone->db_argv + 1, &stub->db);
			if (result != ISC_R_SUCCESS) {
				dns_zone_log(zone, ISC_LOG_ERROR,
					     "refreshing stub: "
					     "could not create "
					     "database: %s",
					     isc_result_totext(result));
				goto cleanup;
			}
			dns_db_setloop(stub->db, zone->loop);
			dns_db_setmaxrrperset(stub->db, zone->maxrrperset);
			dns_db_setmaxtypepername(stub->db,
						 zone->maxtypepername);
		}

		result = dns_db_newversion(stub->db, &stub->version);
		if (result != ISC_R_SUCCESS) {
			dns_zone_log(zone, ISC_LOG_INFO,
				     "refreshing stub: "
				     "dns_db_newversion() failed: %s",
				     isc_result_totext(result));
			goto cleanup;
		}

		/*
		 * Update SOA record.
		 */
		result = dns_db_findnode(stub->db, &zone->origin, true, &node);
		if (result != ISC_R_SUCCESS) {
			dns_zone_log(zone, ISC_LOG_INFO,
				     "refreshing stub: "
				     "dns_db_findnode() failed: %s",
				     isc_result_totext(result));
			goto cleanup;
		}

		result = dns_db_addrdataset(stub->db, node, stub->version, 0,
					    soardataset, 0, NULL);
		dns_db_detachnode(stub->db, &node);
		if (result != ISC_R_SUCCESS) {
			dns_zone_log(zone, ISC_LOG_INFO,
				     "refreshing stub: "
				     "dns_db_addrdataset() failed: %s",
				     isc_result_totext(result));
			goto cleanup;
		}
	}

	/*
	 * XXX Optimisation: Create message when zone is setup and reuse.
	 */
	create_query(zone, dns_rdatatype_ns, &zone->origin, &message);

	INSIST(dns_remote_count(&zone->primaries) > 0);
	INSIST(!dns_remote_done(&zone->primaries));

	sourceaddr = dns_remote_sourceaddr(&zone->primaries);
	curraddr = dns_remote_curraddr(&zone->primaries);
	isc_netaddr_fromsockaddr(&primaryip, &curraddr);
	/*
	 * First, look for a tsig key in the primaries statement, then
	 * try for a server key.
	 */
	if (dns_remote_keyname(&zone->primaries) != NULL) {
		dns_view_t *view = dns_zone_getview(zone);
		dns_name_t *keyname = dns_remote_keyname(&zone->primaries);
		result = dns_view_gettsig(view, keyname, &key);
		if (result != ISC_R_SUCCESS) {
			char namebuf[DNS_NAME_FORMATSIZE];
			dns_name_format(keyname, namebuf, sizeof(namebuf));
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "unable to find key: %s", namebuf);
		}
	}
	if (key == NULL) {
		(void)dns_view_getpeertsig(zone->view, &primaryip, &key);
	}

	/* FIXME(OS): Do we need the transport here too? Most probably yes */

	reqnsid = zone->view->requestnsid;
	if (zone->view->peers != NULL) {
		dns_peer_t *peer = NULL;
		bool edns;
		result = dns_peerlist_peerbyaddr(zone->view->peers, &primaryip,
						 &peer);
		if (result == ISC_R_SUCCESS) {
			result = dns_peer_getsupportedns(peer, &edns);
			if (result == ISC_R_SUCCESS && !edns) {
				DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NOEDNS);
			}
			result = dns_peer_gettransfersource(peer,
							    &zone->sourceaddr);
			if (result == ISC_R_SUCCESS) {
				have_xfrsource = true;
			}
			udpsize = dns_view_getudpsize(zone->view);
			(void)dns_peer_getudpsize(peer, &udpsize);
			(void)dns_peer_getrequestnsid(peer, &reqnsid);
		}
	}
	if (!DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NOEDNS)) {
		result = add_opt(message, udpsize, reqnsid, false);
		if (result != ISC_R_SUCCESS) {
			zone_debuglog(zone, __func__, 1,
				      "unable to add opt record: %s",
				      isc_result_totext(result));
		}
	}

	/*
	 * Always use TCP so that we shouldn't truncate in additional section.
	 */
	switch (isc_sockaddr_pf(&curraddr)) {
	case PF_INET:
		if (!have_xfrsource) {
			isc_sockaddr_t any;
			isc_sockaddr_any(&any);

			zone->sourceaddr = sourceaddr;
			if (isc_sockaddr_equal(&zone->sourceaddr, &any)) {
				zone->sourceaddr = zone->xfrsource4;
			}
		}
		break;
	case PF_INET6:
		if (!have_xfrsource) {
			isc_sockaddr_t any;
			isc_sockaddr_any6(&any);

			zone->sourceaddr = sourceaddr;
			if (isc_sockaddr_equal(&zone->sourceaddr, &any)) {
				zone->sourceaddr = zone->xfrsource6;
			}
		}
		break;
	default:
		result = ISC_R_NOTIMPLEMENTED;
		POST(result);
		goto cleanup;
	}
	timeout = 5;
	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_DIALREFRESH)) {
		timeout = 30;
	}

	/*
	 * Save request parameters so we can reuse them later on
	 * for resolving missing glue A/AAAA records.
	 */
	cb_args = isc_mem_get(zone->mctx, sizeof(*cb_args));
	cb_args->stub = stub;
	cb_args->tsig_key = key;
	cb_args->udpsize = udpsize;
	cb_args->timeout = timeout;
	cb_args->reqnsid = reqnsid;

	result = dns_request_create(
		zone->view->requestmgr, message, &zone->sourceaddr, &curraddr,
		NULL, NULL, DNS_REQUESTOPT_TCP, key, timeout * 3 + 1, timeout,
		2, zone->loop, stub_callback, cb_args, &zone->request);
	if (result != ISC_R_SUCCESS) {
		zone_debuglog(zone, __func__, 1,
			      "dns_request_create() failed: %s",
			      isc_result_totext(result));
		goto cleanup;
	}
	dns_message_detach(&message);
	goto unlock;

cleanup:
	cancel_refresh(zone);
	stub->magic = 0;
	if (stub->version != NULL) {
		dns_db_closeversion(stub->db, &stub->version, false);
	}
	if (stub->db != NULL) {
		dns_db_detach(&stub->db);
	}
	if (stub->zone != NULL) {
		zone_idetach(&stub->zone);
	}
	isc_mem_put(stub->mctx, stub, sizeof(*stub));
	if (message != NULL) {
		dns_message_detach(&message);
	}
unlock:
	if (key != NULL) {
		dns_tsigkey_detach(&key);
	}
	return;
}

/*
 * Shut the zone down.
 */
static void
zone_shutdown(void *arg) {
	dns_zone_t *zone = (dns_zone_t *)arg;
	bool free_needed, linked = false;
	dns_zone_t *raw = NULL, *secure = NULL;
	dns_view_t *view = NULL, *prev_view = NULL;

	REQUIRE(DNS_ZONE_VALID(zone));
	INSIST(isc_refcount_current(&zone->references) == 0);

	zone_debuglog(zone, __func__, 3, "shutting down");

	/*
	 * If we were waiting for xfrin quota, step out of
	 * the queue.
	 * If there's no zone manager, we can't be waiting for the
	 * xfrin quota
	 */
	if (zone->zmgr != NULL) {
		RWLOCK(&zone->zmgr->rwlock, isc_rwlocktype_write);
		if (zone->statelist == &zone->zmgr->waiting_for_xfrin) {
			ISC_LIST_UNLINK(zone->zmgr->waiting_for_xfrin, zone,
					statelink);
			linked = true;
			zone->statelist = NULL;
		}
		if (zone->statelist == &zone->zmgr->xfrin_in_progress) {
			ISC_LIST_UNLINK(zone->zmgr->xfrin_in_progress, zone,
					statelink);
			zone->statelist = NULL;
			zmgr_resume_xfrs(zone->zmgr, false);
		}
		RWUNLOCK(&zone->zmgr->rwlock, isc_rwlocktype_write);
	}

	/*
	 * In loop context, no locking required.  See zone_xfrdone().
	 */
	if (zone->xfr != NULL) {
		/* The final detach will happen in zone_xfrdone() */
		dns_xfrin_shutdown(zone->xfr);
	}

	/* Safe to release the zone now */
	if (zone->zmgr != NULL) {
		dns_zonemgr_releasezone(zone->zmgr, zone);
	}

	LOCK_ZONE(zone);
	INSIST(zone != zone->raw);

	/*
	 * Detach the views early, we don't need them anymore.  However, we need
	 * to detach them outside of the zone lock to break the lock loop
	 * between view, adb and zone locks.
	 */
	view = zone->view;
	zone->view = NULL;
	prev_view = zone->prev_view;
	zone->prev_view = NULL;

	if (linked) {
		isc_refcount_decrement(&zone->irefs);
	}
	if (zone->request != NULL) {
		dns_request_cancel(zone->request);
	}

	if (zone->loadctx != NULL) {
		dns_loadctx_cancel(zone->loadctx);
	}

	if (!DNS_ZONE_FLAG(zone, DNS_ZONEFLG_FLUSH) ||
	    !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_DUMPING))
	{
		if (zone->dumpctx != NULL) {
			dns_dumpctx_cancel(zone->dumpctx);
		}
	}

	checkds_cancel(zone);

	notify_cancel(zone);

	forward_cancel(zone);

	if (zone->timer != NULL) {
		isc_refcount_decrement(&zone->irefs);
		isc_timer_destroy(&zone->timer);
	}

	/*
	 * We have now canceled everything set the flag to allow exit_check()
	 * to succeed.	We must not unlock between setting this flag and
	 * calling exit_check().
	 */
	DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_SHUTDOWN);
	free_needed = exit_check(zone);
	/*
	 * If a dump is in progress for the secure zone, defer detaching from
	 * the raw zone as it may prevent the unsigned serial number from being
	 * stored in the raw-format dump of the secure zone.  In this scenario,
	 * dump_done() takes care of cleaning up the zone->raw reference.
	 */
	if (inline_secure(zone) && !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_DUMPING)) {
		raw = zone->raw;
		zone->raw = NULL;
	}
	if (inline_raw(zone)) {
		secure = zone->secure;
		zone->secure = NULL;
	}
	UNLOCK_ZONE(zone);

	if (view != NULL) {
		dns_view_weakdetach(&view);
	}
	if (prev_view != NULL) {
		dns_view_weakdetach(&prev_view);
	}

	if (raw != NULL) {
		dns_zone_detach(&raw);
	}
	if (secure != NULL) {
		dns_zone_idetach(&secure);
	}
	if (free_needed) {
		zone_free(zone);
	}
}

static void
zone_timer(void *arg) {
	dns_zone_t *zone = (dns_zone_t *)arg;

	REQUIRE(DNS_ZONE_VALID(zone));

	zone_maintenance(zone);
}

static void
zone_timer_stop(dns_zone_t *zone) {
	zone_debuglog(zone, __func__, 10, "stop zone timer");
	if (zone->timer != NULL) {
		isc_timer_stop(zone->timer);
	}
}

static void
zone_timer_set(dns_zone_t *zone, isc_time_t *next, isc_time_t *now) {
	isc_interval_t interval;

	if (isc_time_compare(next, now) <= 0) {
		isc_interval_set(&interval, 0, 0);
	} else {
		isc_time_subtract(next, now, &interval);
	}

	if (zone->loop == NULL) {
		zone_debuglog(zone, __func__, 10, "zone is not managed");
	} else if (zone->timer == NULL) {
		isc_refcount_increment0(&zone->irefs);
		isc_timer_create(zone->loop, zone_timer, zone, &zone->timer);
	}
	if (zone->timer != NULL) {
		isc_timer_start(zone->timer, isc_timertype_once, &interval);
	}
}

static void
zone__settimer(void *arg) {
	zone_settimer_t *data = arg;
	dns_zone_t *zone = data->zone;
	isc_time_t *now = &data->now;
	isc_time_t next;
	bool free_needed = false;

	REQUIRE(DNS_ZONE_VALID(zone));
	ENTER;

	LOCK_ZONE(zone);
	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING)) {
		goto free;
	}
	isc_time_settoepoch(&next);

	switch (zone->type) {
	case dns_zone_redirect:
		if (dns_remote_addresses(&zone->primaries) != NULL) {
			goto treat_as_secondary;
		}
		FALLTHROUGH;
	case dns_zone_primary:
		if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDNOTIFY) ||
		    DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDSTARTUPNOTIFY))
		{
			next = zone->notifytime;
		}
		if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDDUMP) &&
		    !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_DUMPING))
		{
			INSIST(!isc_time_isepoch(&zone->dumptime));
			if (isc_time_isepoch(&next) ||
			    isc_time_compare(&zone->dumptime, &next) < 0)
			{
				next = zone->dumptime;
			}
		}
		if (zone->type == dns_zone_redirect) {
			break;
		}
		if (!DNS_ZONE_FLAG(zone, DNS_ZONEFLG_REFRESHING) &&
		    !isc_time_isepoch(&zone->refreshkeytime))
		{
			if (isc_time_isepoch(&next) ||
			    isc_time_compare(&zone->refreshkeytime, &next) < 0)
			{
				next = zone->refreshkeytime;
			}
		}
		if (!isc_time_isepoch(&zone->resigntime)) {
			if (isc_time_isepoch(&next) ||
			    isc_time_compare(&zone->resigntime, &next) < 0)
			{
				next = zone->resigntime;
			}
		}
		if (!isc_time_isepoch(&zone->keywarntime)) {
			if (isc_time_isepoch(&next) ||
			    isc_time_compare(&zone->keywarntime, &next) < 0)
			{
				next = zone->keywarntime;
			}
		}
		if (!isc_time_isepoch(&zone->signingtime)) {
			if (isc_time_isepoch(&next) ||
			    isc_time_compare(&zone->signingtime, &next) < 0)
			{
				next = zone->signingtime;
			}
		}
		if (!isc_time_isepoch(&zone->nsec3chaintime)) {
			if (isc_time_isepoch(&next) ||
			    isc_time_compare(&zone->nsec3chaintime, &next) < 0)
			{
				next = zone->nsec3chaintime;
			}
		}
		break;

	case dns_zone_secondary:
	case dns_zone_mirror:
	treat_as_secondary:
		if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDNOTIFY) ||
		    DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDSTARTUPNOTIFY))
		{
			next = zone->notifytime;
		}
		FALLTHROUGH;
	case dns_zone_stub:
		if (!DNS_ZONE_FLAG(zone, DNS_ZONEFLG_REFRESH) &&
		    !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NOPRIMARIES) &&
		    !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NOREFRESH) &&
		    !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADING) &&
		    !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADPENDING) &&
		    !isc_time_isepoch(&zone->refreshtime) &&
		    (isc_time_isepoch(&next) ||
		     isc_time_compare(&zone->refreshtime, &next) < 0))
		{
			next = zone->refreshtime;
		}
		if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADED) &&
		    !isc_time_isepoch(&zone->expiretime))
		{
			if (isc_time_isepoch(&next) ||
			    isc_time_compare(&zone->expiretime, &next) < 0)
			{
				next = zone->expiretime;
			}
		}
		if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDDUMP) &&
		    !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_DUMPING))
		{
			INSIST(!isc_time_isepoch(&zone->dumptime));
			if (isc_time_isepoch(&next) ||
			    isc_time_compare(&zone->dumptime, &next) < 0)
			{
				next = zone->dumptime;
			}
		}
		break;

	case dns_zone_key:
		if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDDUMP) &&
		    !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_DUMPING))
		{
			INSIST(!isc_time_isepoch(&zone->dumptime));
			if (isc_time_isepoch(&next) ||
			    isc_time_compare(&zone->dumptime, &next) < 0)
			{
				next = zone->dumptime;
			}
		}
		if (!DNS_ZONE_FLAG(zone, DNS_ZONEFLG_REFRESHING)) {
			if (isc_time_isepoch(&next) ||
			    (!isc_time_isepoch(&zone->refreshkeytime) &&
			     isc_time_compare(&zone->refreshkeytime, &next) <
				     0))
			{
				next = zone->refreshkeytime;
			}
		}
		break;

	default:
		break;
	}

	if (isc_time_isepoch(&next)) {
		zone_timer_stop(zone);
	} else {
		zone_timer_set(zone, &next, now);
	}

free:
	isc_mem_put(zone->mctx, data, sizeof(*data));
	isc_refcount_decrement(&zone->irefs);
	free_needed = exit_check(zone);
	UNLOCK_ZONE(zone);
	if (free_needed) {
		zone_free(zone);
	}
}

static void
zone_settimer(dns_zone_t *zone, isc_time_t *now) {
	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING)) {
		return;
	}

	zone_settimer_t *arg = isc_mem_get(zone->mctx, sizeof(*arg));
	*arg = (zone_settimer_t){
		.zone = zone,
		.now = *now,
	};
	isc_refcount_increment0(&zone->irefs);
	isc_async_run(zone->loop, zone__settimer, arg);
}

static void
cancel_refresh(dns_zone_t *zone) {
	isc_time_t now;

	/*
	 * 'zone' locked by caller.
	 */

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(LOCKED_ZONE(zone));

	ENTER;

	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_REFRESH);
	now = isc_time_now();
	zone_settimer(zone, &now);
}

static isc_result_t
notify_createmessage(dns_zone_t *zone, unsigned int flags,
		     dns_message_t **messagep) {
	dns_db_t *zonedb = NULL;
	dns_dbnode_t *node = NULL;
	dns_dbversion_t *version = NULL;
	dns_message_t *message = NULL;
	dns_rdataset_t rdataset;
	dns_rdata_t rdata = DNS_RDATA_INIT;

	dns_name_t *tempname = NULL;
	dns_rdata_t *temprdata = NULL;
	dns_rdatalist_t *temprdatalist = NULL;
	dns_rdataset_t *temprdataset = NULL;

	isc_result_t result;
	isc_region_t r;
	isc_buffer_t *b = NULL;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(messagep != NULL && *messagep == NULL);

	dns_message_create(zone->mctx, NULL, NULL, DNS_MESSAGE_INTENTRENDER,
			   &message);

	message->opcode = dns_opcode_notify;
	message->flags |= DNS_MESSAGEFLAG_AA;
	message->rdclass = zone->rdclass;

	dns_message_gettempname(message, &tempname);

	dns_message_gettemprdataset(message, &temprdataset);

	/*
	 * Make question.
	 */
	dns_name_clone(&zone->origin, tempname);
	dns_rdataset_makequestion(temprdataset, zone->rdclass,
				  dns_rdatatype_soa);
	ISC_LIST_APPEND(tempname->list, temprdataset, link);
	dns_message_addname(message, tempname, DNS_SECTION_QUESTION);
	tempname = NULL;
	temprdataset = NULL;

	if ((flags & DNS_NOTIFY_NOSOA) != 0) {
		goto done;
	}

	dns_message_gettempname(message, &tempname);
	dns_message_gettemprdata(message, &temprdata);
	dns_message_gettemprdataset(message, &temprdataset);
	dns_message_gettemprdatalist(message, &temprdatalist);

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	INSIST(zone->db != NULL); /* XXXJT: is this assumption correct? */
	dns_db_attach(zone->db, &zonedb);
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);

	dns_name_clone(&zone->origin, tempname);
	dns_db_currentversion(zonedb, &version);
	result = dns_db_findnode(zonedb, tempname, false, &node);
	if (result != ISC_R_SUCCESS) {
		goto soa_cleanup;
	}

	dns_rdataset_init(&rdataset);
	result = dns_db_findrdataset(zonedb, node, version, dns_rdatatype_soa,
				     dns_rdatatype_none, 0, &rdataset, NULL);
	if (result != ISC_R_SUCCESS) {
		goto soa_cleanup;
	}
	result = dns_rdataset_first(&rdataset);
	if (result != ISC_R_SUCCESS) {
		goto soa_cleanup;
	}
	dns_rdataset_current(&rdataset, &rdata);
	dns_rdata_toregion(&rdata, &r);
	isc_buffer_allocate(zone->mctx, &b, r.length);
	isc_buffer_putmem(b, r.base, r.length);
	isc_buffer_usedregion(b, &r);
	dns_rdata_init(temprdata);
	dns_rdata_fromregion(temprdata, rdata.rdclass, rdata.type, &r);
	dns_message_takebuffer(message, &b);
	result = dns_rdataset_next(&rdataset);
	dns_rdataset_disassociate(&rdataset);
	if (result != ISC_R_NOMORE) {
		goto soa_cleanup;
	}
	temprdatalist->rdclass = rdata.rdclass;
	temprdatalist->type = rdata.type;
	temprdatalist->ttl = rdataset.ttl;
	ISC_LIST_APPEND(temprdatalist->rdata, temprdata, link);

	dns_rdatalist_tordataset(temprdatalist, temprdataset);

	ISC_LIST_APPEND(tempname->list, temprdataset, link);
	dns_message_addname(message, tempname, DNS_SECTION_ANSWER);
	temprdatalist = NULL;
	temprdataset = NULL;
	temprdata = NULL;
	tempname = NULL;

soa_cleanup:
	if (node != NULL) {
		dns_db_detachnode(zonedb, &node);
	}
	if (version != NULL) {
		dns_db_closeversion(zonedb, &version, false);
	}
	if (zonedb != NULL) {
		dns_db_detach(&zonedb);
	}
	if (tempname != NULL) {
		dns_message_puttempname(message, &tempname);
	}
	if (temprdata != NULL) {
		dns_message_puttemprdata(message, &temprdata);
	}
	if (temprdataset != NULL) {
		dns_message_puttemprdataset(message, &temprdataset);
	}
	if (temprdatalist != NULL) {
		dns_message_puttemprdatalist(message, &temprdatalist);
	}

done:
	*messagep = message;
	return ISC_R_SUCCESS;
}

isc_result_t
dns_zone_notifyreceive(dns_zone_t *zone, isc_sockaddr_t *from,
		       isc_sockaddr_t *to, dns_message_t *msg) {
	unsigned int i;
	dns_rdata_soa_t soa;
	dns_rdataset_t *rdataset = NULL;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	isc_result_t result;
	char fromtext[ISC_SOCKADDR_FORMATSIZE];
	int match = 0;
	isc_netaddr_t netaddr;
	uint32_t serial = 0;
	bool have_serial = false;
	dns_tsigkey_t *tsigkey;
	const dns_name_t *tsig;

	REQUIRE(DNS_ZONE_VALID(zone));

	/*
	 * If type != T_SOA return DNS_R_NOTIMP.  We don't yet support
	 * ROLLOVER.
	 *
	 * SOA:	RFC1996
	 * Check that 'from' is a valid notify source, (zone->primaries).
	 *	Return DNS_R_REFUSED if not.
	 *
	 * If the notify message contains a serial number check it
	 * against the zones serial and return if <= current serial
	 *
	 * If a refresh check is progress, if so just record the
	 * fact we received a NOTIFY and from where and return.
	 * We will perform a new refresh check when the current one
	 * completes. Return ISC_R_SUCCESS.
	 *
	 * Otherwise initiate a refresh check using 'from' as the
	 * first address to check.  Return ISC_R_SUCCESS.
	 */

	isc_sockaddr_format(from, fromtext, sizeof(fromtext));

	/*
	 * Notify messages are processed by the raw zone.
	 */
	LOCK_ZONE(zone);
	INSIST(zone != zone->raw);
	if (inline_secure(zone)) {
		result = dns_zone_notifyreceive(zone->raw, from, to, msg);
		UNLOCK_ZONE(zone);
		return result;
	}
	/*
	 *  We only handle NOTIFY (SOA) at the present.
	 */
	if (isc_sockaddr_pf(from) == PF_INET) {
		inc_stats(zone, dns_zonestatscounter_notifyinv4);
	} else {
		inc_stats(zone, dns_zonestatscounter_notifyinv6);
	}
	if (msg->counts[DNS_SECTION_QUESTION] == 0 ||
	    dns_message_findname(msg, DNS_SECTION_QUESTION, &zone->origin,
				 dns_rdatatype_soa, dns_rdatatype_none, NULL,
				 NULL) != ISC_R_SUCCESS)
	{
		UNLOCK_ZONE(zone);
		if (msg->counts[DNS_SECTION_QUESTION] == 0) {
			dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
				      ISC_LOG_NOTICE,
				      "NOTIFY with no question "
				      "section from: %s",
				      fromtext);
			return DNS_R_FORMERR;
		}
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_NOTICE,
			      "NOTIFY zone does not match");
		return DNS_R_NOTIMP;
	}

	/*
	 * If we are a primary zone just succeed.
	 */
	if (zone->type == dns_zone_primary) {
		UNLOCK_ZONE(zone);
		return ISC_R_SUCCESS;
	}

	isc_netaddr_fromsockaddr(&netaddr, from);
	for (i = 0; i < dns_remote_count(&zone->primaries); i++) {
		isc_sockaddr_t sockaddr = dns_remote_addr(&zone->primaries, i);
		if (isc_sockaddr_eqaddr(from, &sockaddr)) {
			break;
		}
		if (zone->view->aclenv->match_mapped &&
		    IN6_IS_ADDR_V4MAPPED(&from->type.sin6.sin6_addr) &&
		    isc_sockaddr_pf(&sockaddr) == AF_INET)
		{
			isc_netaddr_t na1, na2;
			isc_netaddr_fromv4mapped(&na1, &netaddr);
			isc_netaddr_fromsockaddr(&na2, &sockaddr);
			if (isc_netaddr_equal(&na1, &na2)) {
				break;
			}
		}
	}

	/*
	 * Accept notify requests from non primaries if they are on
	 * 'zone->notify_acl'.
	 */
	tsigkey = dns_message_gettsigkey(msg);
	tsig = dns_tsigkey_identity(tsigkey);
	if (i >= dns_remote_count(&zone->primaries) &&
	    zone->notify_acl != NULL &&
	    (dns_acl_match(&netaddr, tsig, zone->notify_acl, zone->view->aclenv,
			   &match, NULL) == ISC_R_SUCCESS) &&
	    match > 0)
	{
		/* Accept notify. */
	} else if (i >= dns_remote_count(&zone->primaries)) {
		UNLOCK_ZONE(zone);
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_INFO,
			      "refused notify from non-primary: %s", fromtext);
		inc_stats(zone, dns_zonestatscounter_notifyrej);
		return DNS_R_REFUSED;
	}

	/*
	 * If the zone is loaded and there are answers check the serial
	 * to see if we need to do a refresh.  Do not worry about this
	 * check if we are a dialup zone as we use the notify request
	 * to trigger a refresh check.
	 */
	if (msg->counts[DNS_SECTION_ANSWER] > 0 &&
	    DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADED) &&
	    !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NOREFRESH))
	{
		result = dns_message_findname(
			msg, DNS_SECTION_ANSWER, &zone->origin,
			dns_rdatatype_soa, dns_rdatatype_none, NULL, &rdataset);
		if (result == ISC_R_SUCCESS) {
			result = dns_rdataset_first(rdataset);
		}
		if (result == ISC_R_SUCCESS) {
			uint32_t oldserial;
			unsigned int soacount;

			dns_rdataset_current(rdataset, &rdata);
			result = dns_rdata_tostruct(&rdata, &soa, NULL);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);
			serial = soa.serial;
			have_serial = true;
			/*
			 * The following should safely be performed without DB
			 * lock and succeed in this context.
			 */
			result = zone_get_from_db(zone, zone->db, NULL,
						  &soacount, NULL, &oldserial,
						  NULL, NULL, NULL, NULL, NULL);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);
			RUNTIME_CHECK(soacount > 0U);
			if (isc_serial_le(serial, oldserial)) {
				dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
					      ISC_LOG_INFO,
					      "notify from %s: "
					      "zone is up to date",
					      fromtext);
				UNLOCK_ZONE(zone);
				return ISC_R_SUCCESS;
			}
		}
	}

	/*
	 * If we got this far and there was a refresh in progress just
	 * let it complete.  Record where we got the notify from so we
	 * can perform a refresh check when the current one completes
	 */
	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_REFRESH)) {
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NEEDREFRESH);
		zone->notifyfrom = *from;
		UNLOCK_ZONE(zone);
		if (have_serial) {
			dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
				      ISC_LOG_INFO,
				      "notify from %s: "
				      "serial %u: refresh in progress, "
				      "refresh check queued",
				      fromtext, serial);
		} else {
			dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
				      ISC_LOG_INFO,
				      "notify from %s: "
				      "refresh in progress, "
				      "refresh check queued",
				      fromtext);
		}
		return ISC_R_SUCCESS;
	}
	if (have_serial) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_INFO,
			      "notify from %s: serial %u", fromtext, serial);
	} else {
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_INFO,
			      "notify from %s: no serial", fromtext);
	}
	zone->notifyfrom = *from;
	UNLOCK_ZONE(zone);

	if (to != NULL) {
		dns_zonemgr_unreachabledel(zone->zmgr, from, to);
	}
	dns_zone_refresh(zone);
	return ISC_R_SUCCESS;
}

void
dns_zone_setnotifyacl(dns_zone_t *zone, dns_acl_t *acl) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->notify_acl != NULL) {
		dns_acl_detach(&zone->notify_acl);
	}
	dns_acl_attach(acl, &zone->notify_acl);
	UNLOCK_ZONE(zone);
}

void
dns_zone_setqueryacl(dns_zone_t *zone, dns_acl_t *acl) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->query_acl != NULL) {
		dns_acl_detach(&zone->query_acl);
	}
	dns_acl_attach(acl, &zone->query_acl);
	UNLOCK_ZONE(zone);
}

void
dns_zone_setqueryonacl(dns_zone_t *zone, dns_acl_t *acl) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->queryon_acl != NULL) {
		dns_acl_detach(&zone->queryon_acl);
	}
	dns_acl_attach(acl, &zone->queryon_acl);
	UNLOCK_ZONE(zone);
}

void
dns_zone_setupdateacl(dns_zone_t *zone, dns_acl_t *acl) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->update_acl != NULL) {
		dns_acl_detach(&zone->update_acl);
	}
	dns_acl_attach(acl, &zone->update_acl);
	UNLOCK_ZONE(zone);
}

void
dns_zone_setforwardacl(dns_zone_t *zone, dns_acl_t *acl) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->forward_acl != NULL) {
		dns_acl_detach(&zone->forward_acl);
	}
	dns_acl_attach(acl, &zone->forward_acl);
	UNLOCK_ZONE(zone);
}

void
dns_zone_setxfracl(dns_zone_t *zone, dns_acl_t *acl) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->xfr_acl != NULL) {
		dns_acl_detach(&zone->xfr_acl);
	}
	dns_acl_attach(acl, &zone->xfr_acl);
	UNLOCK_ZONE(zone);
}

dns_acl_t *
dns_zone_getnotifyacl(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->notify_acl;
}

dns_acl_t *
dns_zone_getqueryacl(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->query_acl;
}

dns_acl_t *
dns_zone_getqueryonacl(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->queryon_acl;
}

dns_acl_t *
dns_zone_getupdateacl(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->update_acl;
}

dns_acl_t *
dns_zone_getforwardacl(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->forward_acl;
}

dns_acl_t *
dns_zone_getxfracl(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->xfr_acl;
}

void
dns_zone_clearupdateacl(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->update_acl != NULL) {
		dns_acl_detach(&zone->update_acl);
	}
	UNLOCK_ZONE(zone);
}

void
dns_zone_clearforwardacl(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->forward_acl != NULL) {
		dns_acl_detach(&zone->forward_acl);
	}
	UNLOCK_ZONE(zone);
}

void
dns_zone_clearnotifyacl(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->notify_acl != NULL) {
		dns_acl_detach(&zone->notify_acl);
	}
	UNLOCK_ZONE(zone);
}

void
dns_zone_clearqueryacl(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->query_acl != NULL) {
		dns_acl_detach(&zone->query_acl);
	}
	UNLOCK_ZONE(zone);
}

void
dns_zone_clearqueryonacl(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->queryon_acl != NULL) {
		dns_acl_detach(&zone->queryon_acl);
	}
	UNLOCK_ZONE(zone);
}

void
dns_zone_clearxfracl(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->xfr_acl != NULL) {
		dns_acl_detach(&zone->xfr_acl);
	}
	UNLOCK_ZONE(zone);
}

bool
dns_zone_getupdatedisabled(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));
	return zone->update_disabled;
}

void
dns_zone_setupdatedisabled(dns_zone_t *zone, bool state) {
	REQUIRE(DNS_ZONE_VALID(zone));
	zone->update_disabled = state;
}

bool
dns_zone_getzeronosoattl(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));
	return zone->zero_no_soa_ttl;
}

void
dns_zone_setzeronosoattl(dns_zone_t *zone, bool state) {
	REQUIRE(DNS_ZONE_VALID(zone));
	zone->zero_no_soa_ttl = state;
}

void
dns_zone_setchecknames(dns_zone_t *zone, dns_severity_t severity) {
	REQUIRE(DNS_ZONE_VALID(zone));

	zone->check_names = severity;
}

dns_severity_t
dns_zone_getchecknames(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->check_names;
}

void
dns_zone_setjournalsize(dns_zone_t *zone, int32_t size) {
	REQUIRE(DNS_ZONE_VALID(zone));

	zone->journalsize = size;
}

int32_t
dns_zone_getjournalsize(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->journalsize;
}

static void
zone_namerd_tostr(dns_zone_t *zone, char *buf, size_t length) {
	isc_result_t result = ISC_R_FAILURE;
	isc_buffer_t buffer;

	REQUIRE(buf != NULL);
	REQUIRE(length > 1U);

	/*
	 * Leave space for terminating '\0'.
	 */
	isc_buffer_init(&buffer, buf, (unsigned int)length - 1);
	if (zone->type != dns_zone_redirect && zone->type != dns_zone_key) {
		if (dns_name_dynamic(&zone->origin)) {
			result = dns_name_totext(
				&zone->origin, DNS_NAME_OMITFINALDOT, &buffer);
		}
		if (result != ISC_R_SUCCESS &&
		    isc_buffer_availablelength(&buffer) >=
			    (sizeof("<UNKNOWN>") - 1))
		{
			isc_buffer_putstr(&buffer, "<UNKNOWN>");
		}

		if (isc_buffer_availablelength(&buffer) > 0) {
			isc_buffer_putstr(&buffer, "/");
		}
		(void)dns_rdataclass_totext(zone->rdclass, &buffer);
	}

	if (zone->view != NULL && strcmp(zone->view->name, "_bind") != 0 &&
	    strcmp(zone->view->name, "_default") != 0 &&
	    strlen(zone->view->name) < isc_buffer_availablelength(&buffer))
	{
		isc_buffer_putstr(&buffer, "/");
		isc_buffer_putstr(&buffer, zone->view->name);
	}
	if (inline_secure(zone) && 9U < isc_buffer_availablelength(&buffer)) {
		isc_buffer_putstr(&buffer, " (signed)");
	}
	if (inline_raw(zone) && 11U < isc_buffer_availablelength(&buffer)) {
		isc_buffer_putstr(&buffer, " (unsigned)");
	}

	buf[isc_buffer_usedlength(&buffer)] = '\0';
}

static void
zone_name_tostr(dns_zone_t *zone, char *buf, size_t length) {
	isc_result_t result = ISC_R_FAILURE;
	isc_buffer_t buffer;

	REQUIRE(buf != NULL);
	REQUIRE(length > 1U);

	/*
	 * Leave space for terminating '\0'.
	 */
	isc_buffer_init(&buffer, buf, (unsigned int)length - 1);
	if (dns_name_dynamic(&zone->origin)) {
		result = dns_name_totext(&zone->origin, DNS_NAME_OMITFINALDOT,
					 &buffer);
	}
	if (result != ISC_R_SUCCESS &&
	    isc_buffer_availablelength(&buffer) >= (sizeof("<UNKNOWN>") - 1))
	{
		isc_buffer_putstr(&buffer, "<UNKNOWN>");
	}

	buf[isc_buffer_usedlength(&buffer)] = '\0';
}

static void
zone_rdclass_tostr(dns_zone_t *zone, char *buf, size_t length) {
	isc_buffer_t buffer;

	REQUIRE(buf != NULL);
	REQUIRE(length > 1U);

	/*
	 * Leave space for terminating '\0'.
	 */
	isc_buffer_init(&buffer, buf, (unsigned int)length - 1);
	(void)dns_rdataclass_totext(zone->rdclass, &buffer);

	buf[isc_buffer_usedlength(&buffer)] = '\0';
}

static void
zone_viewname_tostr(dns_zone_t *zone, char *buf, size_t length) {
	isc_buffer_t buffer;

	REQUIRE(buf != NULL);
	REQUIRE(length > 1U);

	/*
	 * Leave space for terminating '\0'.
	 */
	isc_buffer_init(&buffer, buf, (unsigned int)length - 1);

	if (zone->view == NULL) {
		isc_buffer_putstr(&buffer, "_none");
	} else if (strlen(zone->view->name) <
		   isc_buffer_availablelength(&buffer))
	{
		isc_buffer_putstr(&buffer, zone->view->name);
	} else {
		isc_buffer_putstr(&buffer, "_toolong");
	}

	buf[isc_buffer_usedlength(&buffer)] = '\0';
}

void
dns_zone_name(dns_zone_t *zone, char *buf, size_t length) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(buf != NULL);

	LOCK_ZONE(zone);
	zone_namerd_tostr(zone, buf, length);
	UNLOCK_ZONE(zone);
}

void
dns_zone_nameonly(dns_zone_t *zone, char *buf, size_t length) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(buf != NULL);
	zone_name_tostr(zone, buf, length);
}

void
dns_zone_logv(dns_zone_t *zone, isc_logcategory_t *category, int level,
	      const char *prefix, const char *fmt, va_list ap) {
	char message[4096];
	const char *zstr;

	REQUIRE(DNS_ZONE_VALID(zone));

	if (!isc_log_wouldlog(dns_lctx, level)) {
		return;
	}

	vsnprintf(message, sizeof(message), fmt, ap);

	switch (zone->type) {
	case dns_zone_key:
		zstr = "managed-keys-zone";
		break;
	case dns_zone_redirect:
		zstr = "redirect-zone";
		break;
	default:
		zstr = "zone ";
	}

	isc_log_write(dns_lctx, category, DNS_LOGMODULE_ZONE, level,
		      "%s%s%s%s: %s", prefix != NULL ? prefix : "",
		      prefix != NULL ? ": " : "", zstr, zone->strnamerd,
		      message);
}

static void
notify_log(dns_zone_t *zone, int level, const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	dns_zone_logv(zone, DNS_LOGCATEGORY_NOTIFY, level, NULL, fmt, ap);
	va_end(ap);
}

void
dns_zone_logc(dns_zone_t *zone, isc_logcategory_t *category, int level,
	      const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	dns_zone_logv(zone, category, level, NULL, fmt, ap);
	va_end(ap);
}

void
dns_zone_log(dns_zone_t *zone, int level, const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	dns_zone_logv(zone, DNS_LOGCATEGORY_GENERAL, level, NULL, fmt, ap);
	va_end(ap);
}

static void
zone_debuglogc(dns_zone_t *zone, isc_logcategory_t *category, const char *me,
	       int debuglevel, const char *fmt, ...) {
	int level = ISC_LOG_DEBUG(debuglevel);
	va_list ap;

	va_start(ap, fmt);
	dns_zone_logv(zone, category, level, me, fmt, ap);
	va_end(ap);
}

static void
zone_debuglog(dns_zone_t *zone, const char *me, int debuglevel, const char *fmt,
	      ...) {
	int level = ISC_LOG_DEBUG(debuglevel);
	va_list ap;

	va_start(ap, fmt);
	dns_zone_logv(zone, DNS_LOGCATEGORY_GENERAL, level, me, fmt, ap);
	va_end(ap);
}

static void
dnssec_log(dns_zone_t *zone, int level, const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	dns_zone_logv(zone, DNS_LOGCATEGORY_DNSSEC, level, NULL, fmt, ap);
	va_end(ap);
}

static int
message_count(dns_message_t *msg, dns_section_t section, dns_rdatatype_t type) {
	isc_result_t result;
	dns_name_t *name;
	dns_rdataset_t *curr;
	int count = 0;

	result = dns_message_firstname(msg, section);
	while (result == ISC_R_SUCCESS) {
		name = NULL;
		dns_message_currentname(msg, section, &name);

		for (curr = ISC_LIST_TAIL(name->list); curr != NULL;
		     curr = ISC_LIST_PREV(curr, link))
		{
			if (curr->type == type) {
				count++;
			}
		}
		result = dns_message_nextname(msg, section);
	}

	return count;
}

void
dns_zone_setminxfrratein(dns_zone_t *zone, uint32_t bytes, uint32_t seconds) {
	REQUIRE(DNS_ZONE_VALID(zone));

	zone->minxfrratebytesin = bytes;
	zone->minxfrratesecondsin = seconds;
}

uint32_t
dns_zone_getminxfrratebytesin(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->minxfrratebytesin;
}

uint32_t
dns_zone_getminxfrratesecondsin(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->minxfrratesecondsin;
}

void
dns_zone_setmaxxfrin(dns_zone_t *zone, uint32_t maxxfrin) {
	REQUIRE(DNS_ZONE_VALID(zone));

	zone->maxxfrin = maxxfrin;
}

uint32_t
dns_zone_getmaxxfrin(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->maxxfrin;
}

void
dns_zone_setmaxxfrout(dns_zone_t *zone, uint32_t maxxfrout) {
	REQUIRE(DNS_ZONE_VALID(zone));
	zone->maxxfrout = maxxfrout;
}

uint32_t
dns_zone_getmaxxfrout(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->maxxfrout;
}

dns_zonetype_t
dns_zone_gettype(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->type;
}

const char *
dns_zonetype_name(dns_zonetype_t type) {
	switch (type) {
	case dns_zone_none:
		return "none";
	case dns_zone_primary:
		return "primary";
	case dns_zone_secondary:
		return "secondary";
	case dns_zone_mirror:
		return "mirror";
	case dns_zone_stub:
		return "stub";
	case dns_zone_staticstub:
		return "static-stub";
	case dns_zone_key:
		return "key";
	case dns_zone_dlz:
		return "dlz";
	case dns_zone_redirect:
		return "redirect";
	default:
		return "unknown";
	}
}

dns_zonetype_t
dns_zone_getredirecttype(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(zone->type == dns_zone_redirect);

	return dns_remote_addresses(&zone->primaries) == NULL
		       ? dns_zone_primary
		       : dns_zone_secondary;
}

dns_name_t *
dns_zone_getorigin(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return &zone->origin;
}

void
dns_zone_setidlein(dns_zone_t *zone, uint32_t idlein) {
	REQUIRE(DNS_ZONE_VALID(zone));

	if (idlein == 0) {
		idlein = DNS_DEFAULT_IDLEIN;
	}
	zone->idlein = idlein;
}

uint32_t
dns_zone_getidlein(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->idlein;
}

void
dns_zone_setidleout(dns_zone_t *zone, uint32_t idleout) {
	REQUIRE(DNS_ZONE_VALID(zone));

	zone->idleout = idleout;
}

uint32_t
dns_zone_getidleout(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->idleout;
}

static void
notify_done(void *arg) {
	dns_request_t *request = (dns_request_t *)arg;
	dns_notify_t *notify = dns_request_getarg(request);
	isc_result_t result;
	dns_message_t *message = NULL;
	isc_buffer_t buf;
	char rcode[128];
	char addrbuf[ISC_SOCKADDR_FORMATSIZE];

	REQUIRE(DNS_NOTIFY_VALID(notify));

	isc_buffer_init(&buf, rcode, sizeof(rcode));
	isc_sockaddr_format(&notify->dst, addrbuf, sizeof(addrbuf));
	dns_message_create(notify->zone->mctx, NULL, NULL,
			   DNS_MESSAGE_INTENTPARSE, &message);

	result = dns_request_getresult(request);
	if (result != ISC_R_SUCCESS) {
		goto fail;
	}

	result = dns_request_getresponse(request, message,
					 DNS_MESSAGEPARSE_PRESERVEORDER);
	if (result != ISC_R_SUCCESS) {
		goto fail;
	}

	result = dns_rcode_totext(message->rcode, &buf);
	if (result == ISC_R_SUCCESS) {
		notify_log(notify->zone, ISC_LOG_DEBUG(3),
			   "notify response from %s: %.*s", addrbuf,
			   (int)buf.used, rcode);
	}

fail:
	dns_message_detach(&message);

	if (result == ISC_R_SUCCESS) {
		notify_log(notify->zone, ISC_LOG_DEBUG(1),
			   "notify to %s successful", addrbuf);
	} else if (result == ISC_R_SHUTTINGDOWN || result == ISC_R_CANCELED) {
		/* just destroy the notify */
	} else if ((notify->flags & DNS_NOTIFY_TCP) == 0) {
		notify_log(notify->zone, ISC_LOG_NOTICE,
			   "notify to %s failed: %s: retrying over TCP",
			   addrbuf, isc_result_totext(result));
		notify->flags |= DNS_NOTIFY_TCP;
		dns_request_destroy(&notify->request);
		notify_send_queue(notify, notify->flags & DNS_NOTIFY_STARTUP);
		return;
	} else if (result == ISC_R_TIMEDOUT) {
		notify_log(notify->zone, ISC_LOG_WARNING,
			   "notify to %s failed: %s: retries exceeded", addrbuf,
			   isc_result_totext(result));
	} else {
		notify_log(notify->zone, ISC_LOG_WARNING,
			   "notify to %s failed: %s", addrbuf,
			   isc_result_totext(result));
	}
	notify_destroy(notify, false);
}

struct rss {
	dns_zone_t *zone;
	dns_db_t *db;
	uint32_t serial;
	ISC_LINK(struct rss) link;
};

static void
update_log_cb(void *arg, dns_zone_t *zone, int level, const char *message) {
	UNUSED(arg);
	dns_zone_log(zone, level, "%s", message);
}

static isc_result_t
dnskey_inuse(dns_zone_t *zone, dns_rdata_t *rdata, isc_mem_t *mctx,
	     dns_dnsseckeylist_t *keylist, bool *inuse) {
	isc_result_t result;
	dst_key_t *dstkey = NULL;

	result = dns_dnssec_keyfromrdata(dns_zone_getorigin(zone), rdata, mctx,
					 &dstkey);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "dns_dnssec_keyfromrdata() failed: %s",
			     isc_result_totext(result));
		return result;
	}

	for (dns_dnsseckey_t *k = ISC_LIST_HEAD(*keylist); k != NULL;
	     k = ISC_LIST_NEXT(k, link))
	{
		if (dst_key_pubcompare(k->key, dstkey, false)) {
			*inuse = true;
			break;
		}
	}

	dst_key_free(&dstkey);
	return ISC_R_SUCCESS;
}

static isc_result_t
cdnskey_inuse(dns_zone_t *zone, dns_rdata_t *rdata,
	      dns_dnsseckeylist_t *keylist, bool *inuse) {
	isc_result_t result;
	dns_rdata_cdnskey_t cdnskey;

	result = dns_rdata_tostruct(rdata, &cdnskey, NULL);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "dns_rdata_tostruct(cdnskey) failed: %s",
			     isc_result_totext(result));
		return result;
	}

	for (dns_dnsseckey_t *k = ISC_LIST_HEAD(*keylist); k != NULL;
	     k = ISC_LIST_NEXT(k, link))
	{
		dns_rdata_t cdnskeyrdata = DNS_RDATA_INIT;
		unsigned char keybuf[DST_KEY_MAXSIZE];

		result = dns_dnssec_make_dnskey(k->key, keybuf, sizeof(keybuf),
						&cdnskeyrdata);
		if (result != ISC_R_SUCCESS) {
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "dns_dnssec_make_dnskey() failed: %s",
				     isc_result_totext(result));
			return result;
		}

		cdnskeyrdata.type = dns_rdatatype_cdnskey;
		if (dns_rdata_compare(rdata, &cdnskeyrdata) == 0) {
			*inuse = true;
			break;
		}
	}

	return ISC_R_SUCCESS;
}

static isc_result_t
cds_inuse(dns_zone_t *zone, dns_rdata_t *rdata, dns_dnsseckeylist_t *keylist,
	  bool *inuse) {
	isc_result_t result;
	dns_rdata_ds_t cds;

	result = dns_rdata_tostruct(rdata, &cds, NULL);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "dns_rdata_tostruct(cds) failed: %s",
			     isc_result_totext(result));
		return result;
	}

	for (dns_dnsseckey_t *k = ISC_LIST_HEAD(*keylist); k != NULL;
	     k = ISC_LIST_NEXT(k, link))
	{
		dns_rdata_t dnskey = DNS_RDATA_INIT;
		dns_rdata_t cdsrdata = DNS_RDATA_INIT;
		unsigned char keybuf[DST_KEY_MAXSIZE];
		unsigned char cdsbuf[DNS_DS_BUFFERSIZE];

		if (dst_key_id(k->key) != cds.key_tag ||
		    dst_key_alg(k->key) != cds.algorithm)
		{
			continue;
		}
		result = dns_dnssec_make_dnskey(k->key, keybuf, sizeof(keybuf),
						&dnskey);
		if (result != ISC_R_SUCCESS) {
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "dns_dnssec_make_dnskey() failed: %s",
				     isc_result_totext(result));
			return result;
		}
		result = dns_ds_buildrdata(dns_zone_getorigin(zone), &dnskey,
					   cds.digest_type, cdsbuf, &cdsrdata);
		if (result != ISC_R_SUCCESS) {
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "dns_ds_buildrdata(keytag=%d, algo=%d, "
				     "digest=%d) failed: %s",
				     cds.key_tag, cds.algorithm,
				     cds.digest_type,
				     isc_result_totext(result));
			return result;
		}

		cdsrdata.type = dns_rdatatype_cds;
		if (dns_rdata_compare(rdata, &cdsrdata) == 0) {
			*inuse = true;
			break;
		}
	}

	return ISC_R_SUCCESS;
}

isc_result_t
dns_zone_dnskey_inuse(dns_zone_t *zone, dns_rdata_t *rdata, bool *inuse) {
	dns_dnsseckeylist_t keylist;
	dns_dnsseckey_t *key = NULL;
	isc_result_t result = ISC_R_SUCCESS;
	isc_stdtime_t now = isc_stdtime_now();
	isc_mem_t *mctx;
	dns_kasp_t *kasp;
	dns_keystorelist_t *keystores;
	const char *keydir;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(dns_rdatatype_iskeymaterial(rdata->type));

	mctx = zone->mctx;

	ISC_LIST_INIT(keylist);

	*inuse = false;

	kasp = dns_zone_getkasp(zone);
	keydir = dns_zone_getkeydirectory(zone);
	keystores = dns_zone_getkeystores(zone);

	dns_zone_lock_keyfiles(zone);
	result = dns_dnssec_findmatchingkeys(dns_zone_getorigin(zone), kasp,
					     keydir, keystores, now, mctx,
					     &keylist);
	dns_zone_unlock_keyfiles(zone);
	if (result == ISC_R_NOTFOUND) {
		return ISC_R_SUCCESS;
	} else if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "dns_dnssec_findmatchingkeys() failed: %s",
			     isc_result_totext(result));
		return result;
	}

	switch (rdata->type) {
	case dns_rdatatype_dnskey:
		result = dnskey_inuse(zone, rdata, mctx, &keylist, inuse);
		break;
	case dns_rdatatype_cdnskey:
		result = cdnskey_inuse(zone, rdata, &keylist, inuse);
		break;
	case dns_rdatatype_cds:
		result = cds_inuse(zone, rdata, &keylist, inuse);
		break;
	default:
		UNREACHABLE();
		break;
	}

	while (!ISC_LIST_EMPTY(keylist)) {
		key = ISC_LIST_HEAD(keylist);
		ISC_LIST_UNLINK(keylist, key, link);
		dns_dnsseckey_destroy(mctx, &key);
	}
	return result;
}

static isc_result_t
sync_secure_journal(dns_zone_t *zone, dns_zone_t *raw, dns_journal_t *journal,
		    uint32_t start, uint32_t end, dns_difftuple_t **soatuplep,
		    dns_diff_t *diff) {
	isc_result_t result;
	dns_difftuple_t *tuple = NULL;
	dns_diffop_t op = DNS_DIFFOP_ADD;
	int n_soa = 0;

	REQUIRE(soatuplep != NULL);

	if (start == end) {
		return DNS_R_UNCHANGED;
	}

	CHECK(dns_journal_iter_init(journal, start, end, NULL));
	for (result = dns_journal_first_rr(journal); result == ISC_R_SUCCESS;
	     result = dns_journal_next_rr(journal))
	{
		dns_name_t *name = NULL;
		uint32_t ttl;
		dns_rdata_t *rdata = NULL;
		dns_journal_current_rr(journal, &name, &ttl, &rdata);

		if (rdata->type == dns_rdatatype_soa) {
			n_soa++;
			if (n_soa == 2) {
				/*
				 * Save the latest raw SOA record.
				 */
				if (*soatuplep != NULL) {
					dns_difftuple_free(soatuplep);
				}
				CHECK(dns_difftuple_create(
					diff->mctx, DNS_DIFFOP_ADD, name, ttl,
					rdata, soatuplep));
			}
			if (n_soa == 3) {
				n_soa = 1;
			}
			continue;
		}

		/* Sanity. */
		if (n_soa == 0) {
			dns_zone_log(raw, ISC_LOG_ERROR,
				     "corrupt journal file: '%s'\n",
				     raw->journal);
			return ISC_R_FAILURE;
		}

		if (zone->privatetype != 0 && rdata->type == zone->privatetype)
		{
			continue;
		}

		/*
		 * Skip DNSSEC records that BIND maintains with inline-signing.
		 */
		if (rdata->type == dns_rdatatype_nsec ||
		    rdata->type == dns_rdatatype_rrsig ||
		    rdata->type == dns_rdatatype_nsec3 ||
		    rdata->type == dns_rdatatype_nsec3param)
		{
			continue;
		}
		/*
		 * Allow DNSKEY, CDNSKEY, CDS because users should be able to
		 * update the zone with these records from a different provider,
		 * but skip records that are under our control.
		 */
		if (dns_rdatatype_iskeymaterial(rdata->type)) {
			bool inuse = false;
			isc_result_t r = dns_zone_dnskey_inuse(zone, rdata,
							       &inuse);
			if (r == ISC_R_SUCCESS && inuse) {
				continue;
			}
		}

		op = (n_soa == 1) ? DNS_DIFFOP_DEL : DNS_DIFFOP_ADD;

		CHECK(dns_difftuple_create(diff->mctx, op, name, ttl, rdata,
					   &tuple));
		dns_diff_appendminimal(diff, &tuple);
	}
	if (result == ISC_R_NOMORE) {
		result = ISC_R_SUCCESS;
	}

failure:
	return result;
}

/*
 * Filter the key material preserving TTL changes.  If kasp in effect honour the
 * existing ttl.  The lists returned by sync_secure_db/dns_db_diffx should be
 * DNSSEC RRset order so we can process 'del' and 'add' in parallel rather than
 * searching for TTL only changes first and processing them, then checking the
 * 'in use' status on a subsequent pass.
 */

static void
filter_keymaterial(dns_zone_t *zone, dns_difftuplelist_t *del,
		   dns_difftuplelist_t *add, bool kasp, dns_ttl_t ttl) {
	dns_difftuple_t *deltuple = ISC_LIST_HEAD(*del);
	dns_difftuple_t *addtuple = ISC_LIST_HEAD(*add);
	isc_result_t result;

	while (deltuple != NULL || addtuple != NULL) {
		dns_difftuple_t *delnext = NULL, *addnext = NULL;
		bool inuse = false;
		if (deltuple != NULL) {
			delnext = ISC_LIST_NEXT(deltuple, link);
		}
		if (addtuple != NULL) {
			addnext = ISC_LIST_NEXT(addtuple, link);
		}
		if (deltuple != NULL && addtuple != NULL) {
			int n = dns_rdata_compare(&deltuple->rdata,
						  &addtuple->rdata);
			if (n == 0) {
				/*
				 * If the rdata is equal then the only
				 * difference will be a TTL change.
				 */
				if (kasp) {
					/* TTL is managed by dnssec-policy */
					ISC_LIST_UNLINK(*del, deltuple, link);
					dns_difftuple_free(&deltuple);
					ISC_LIST_UNLINK(*add, addtuple, link);
					dns_difftuple_free(&addtuple);
				}
				deltuple = delnext;
				addtuple = addnext;
				continue;
			}
			if (n < 0) {
				goto checkdel;
			}
			goto checkadd;
		} else if (deltuple != NULL) {
		checkdel:
			result = dns_zone_dnskey_inuse(zone, &deltuple->rdata,
						       &inuse);
			if (result == ISC_R_SUCCESS && inuse) {
				ISC_LIST_UNLINK(*del, deltuple, link);
				dns_difftuple_free(&deltuple);
			}
			deltuple = delnext;
		} else {
		checkadd:
			result = dns_zone_dnskey_inuse(zone, &addtuple->rdata,
						       &inuse);
			if (result == ISC_R_SUCCESS && inuse) {
				ISC_LIST_UNLINK(*add, addtuple, link);
				dns_difftuple_free(&addtuple);
			} else if (kasp) {
				addtuple->ttl = ttl;
			}
			addtuple = addnext;
		}
	}
}

static isc_result_t
sync_secure_db(dns_zone_t *seczone, dns_zone_t *raw, dns_db_t *secdb,
	       dns_dbversion_t *secver, dns_difftuple_t **soatuple,
	       dns_diff_t *diff) {
	isc_result_t result;
	dns_db_t *rawdb = NULL;
	dns_dbversion_t *rawver = NULL;
	dns_difftuple_t *tuple = NULL, *next;
	dns_difftuple_t *oldtuple = NULL, *newtuple = NULL;
	dns_rdata_soa_t oldsoa, newsoa;
	dns_difftuplelist_t add = ISC_LIST_INITIALIZER;
	dns_difftuplelist_t del = ISC_LIST_INITIALIZER;
	dns_difftuplelist_t keyadd = ISC_LIST_INITIALIZER;
	dns_difftuplelist_t keydel = ISC_LIST_INITIALIZER;
	dns_difftuplelist_t ckeyadd = ISC_LIST_INITIALIZER;
	dns_difftuplelist_t ckeydel = ISC_LIST_INITIALIZER;
	dns_difftuplelist_t cdsadd = ISC_LIST_INITIALIZER;
	dns_difftuplelist_t cdsdel = ISC_LIST_INITIALIZER;
	dns_kasp_t *kasp = NULL;
	dns_ttl_t keyttl = 0, ckeyttl = 0, cdsttl = 0;

	REQUIRE(DNS_ZONE_VALID(seczone));
	REQUIRE(soatuple != NULL && *soatuple == NULL);

	if (!seczone->sourceserialset) {
		return DNS_R_UNCHANGED;
	}

	dns_db_attach(raw->db, &rawdb);
	dns_db_currentversion(rawdb, &rawver);
	result = dns_db_diffx(diff, rawdb, rawver, secdb, secver, NULL);
	dns_db_closeversion(rawdb, &rawver, false);
	dns_db_detach(&rawdb);

	if (result != ISC_R_SUCCESS) {
		return result;
	}

	/*
	 * If kasp is in effect honour the existing DNSKEY, CDNSKEY and CDS
	 * TTLs.
	 */
	kasp = seczone->kasp;
	if (kasp != NULL) {
		dns_rdataset_t rdataset;
		dns_dbnode_t *node = NULL;
		dns_ttl_t ttl = dns_kasp_dnskeyttl(kasp);

		dns_rdataset_init(&rdataset);

		result = dns_db_getoriginnode(secdb, &node);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);

		result = dns_db_findrdataset(
			secdb, node, secver, dns_rdatatype_dnskey,
			dns_rdatatype_none, 0, &rdataset, NULL);
		keyttl = (result == ISC_R_SUCCESS) ? rdataset.ttl : ttl;
		if (dns_rdataset_isassociated(&rdataset)) {
			dns_rdataset_disassociate(&rdataset);
		}

		result = dns_db_findrdataset(
			secdb, node, secver, dns_rdatatype_cdnskey,
			dns_rdatatype_none, 0, &rdataset, NULL);
		ckeyttl = (result == ISC_R_SUCCESS) ? rdataset.ttl : ttl;
		if (dns_rdataset_isassociated(&rdataset)) {
			dns_rdataset_disassociate(&rdataset);
		}

		result = dns_db_findrdataset(
			secdb, node, secver, dns_rdatatype_cds,
			dns_rdatatype_none, 0, &rdataset, NULL);
		cdsttl = (result == ISC_R_SUCCESS) ? rdataset.ttl : ttl;
		if (dns_rdataset_isassociated(&rdataset)) {
			dns_rdataset_disassociate(&rdataset);
		}
		dns_db_detachnode(secdb, &node);
	}

	for (tuple = ISC_LIST_HEAD(diff->tuples); tuple != NULL; tuple = next) {
		dns_difftuplelist_t *al = &add, *dl = &del;

		next = ISC_LIST_NEXT(tuple, link);

		/*
		 * Skip DNSSEC records that BIND maintains with inline-signing.
		 */
		if (tuple->rdata.type == dns_rdatatype_nsec ||
		    tuple->rdata.type == dns_rdatatype_rrsig ||
		    tuple->rdata.type == dns_rdatatype_nsec3 ||
		    tuple->rdata.type == dns_rdatatype_nsec3param)
		{
			ISC_LIST_UNLINK(diff->tuples, tuple, link);
			dns_difftuple_free(&tuple);
			continue;
		}

		/*
		 * Apex DNSKEY, CDNSKEY and CDS need special processing so
		 * split them out.
		 */
		if (dns_rdatatype_iskeymaterial(tuple->rdata.type) &&
		    dns_name_equal(&tuple->name, &seczone->origin))
		{
			switch (tuple->rdata.type) {
			case dns_rdatatype_dnskey:
				al = &keyadd;
				dl = &keydel;
				break;
			case dns_rdatatype_cdnskey:
				al = &ckeyadd;
				dl = &ckeydel;
				break;
			case dns_rdatatype_cds:
				al = &cdsadd;
				dl = &cdsdel;
				break;
			default:
				UNREACHABLE();
			}
		}

		if (tuple->rdata.type == dns_rdatatype_soa) {
			if (tuple->op == DNS_DIFFOP_DEL) {
				INSIST(oldtuple == NULL);
				oldtuple = tuple;
			}
			if (tuple->op == DNS_DIFFOP_ADD) {
				INSIST(newtuple == NULL);
				newtuple = tuple;
			}
		}

		/*
		 * Split into deletions and additions.
		 */
		ISC_LIST_UNLINK(diff->tuples, tuple, link);
		switch (tuple->op) {
		case DNS_DIFFOP_DEL:
		case DNS_DIFFOP_DELRESIGN:
			ISC_LIST_APPEND(*dl, tuple, link);
			break;
		case DNS_DIFFOP_ADD:
		case DNS_DIFFOP_ADDRESIGN:
			ISC_LIST_APPEND(*al, tuple, link);
			break;
		default:
			UNREACHABLE();
		}
	}

	if (oldtuple != NULL && newtuple != NULL) {
		result = dns_rdata_tostruct(&oldtuple->rdata, &oldsoa, NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);

		result = dns_rdata_tostruct(&newtuple->rdata, &newsoa, NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);

		/*
		 * If the SOA records are the same except for the serial
		 * remove them from the diff.
		 */
		if (oldtuple->ttl == newtuple->ttl &&
		    oldsoa.refresh == newsoa.refresh &&
		    oldsoa.retry == newsoa.retry &&
		    oldsoa.minimum == newsoa.minimum &&
		    oldsoa.expire == newsoa.expire &&
		    dns_name_equal(&oldsoa.origin, &newsoa.origin) &&
		    dns_name_equal(&oldsoa.contact, &newsoa.contact))
		{
			ISC_LIST_UNLINK(del, oldtuple, link);
			dns_difftuple_free(&oldtuple);
			ISC_LIST_UNLINK(add, newtuple, link);
			dns_difftuple_free(&newtuple);
		}
	}

	/*
	 * Filter out keys we manage but still allow TTL changes.
	 */
	filter_keymaterial(seczone, &keydel, &keyadd, kasp != NULL, keyttl);
	filter_keymaterial(seczone, &ckeydel, &ckeyadd, kasp != NULL, ckeyttl);
	filter_keymaterial(seczone, &cdsdel, &cdsadd, kasp != NULL, cdsttl);

	/*
	 * Rebuild the diff now that we have filtered it
	 */
	ISC_LIST_APPENDLIST(diff->tuples, del, link);
	ISC_LIST_APPENDLIST(diff->tuples, keydel, link);
	ISC_LIST_APPENDLIST(diff->tuples, ckeydel, link);
	ISC_LIST_APPENDLIST(diff->tuples, cdsdel, link);
	ISC_LIST_APPENDLIST(diff->tuples, add, link);
	ISC_LIST_APPENDLIST(diff->tuples, keyadd, link);
	ISC_LIST_APPENDLIST(diff->tuples, ckeyadd, link);
	ISC_LIST_APPENDLIST(diff->tuples, cdsadd, link);

	if (ISC_LIST_EMPTY(diff->tuples)) {
		return DNS_R_UNCHANGED;
	}

	/*
	 * If there are still SOA records in the diff they can now be removed
	 * saving the new SOA record.
	 */
	if (oldtuple != NULL) {
		ISC_LIST_UNLINK(diff->tuples, oldtuple, link);
		dns_difftuple_free(&oldtuple);
	}

	if (newtuple != NULL) {
		ISC_LIST_UNLINK(diff->tuples, newtuple, link);
		*soatuple = newtuple;
	}

	return ISC_R_SUCCESS;
}

static void
receive_secure_serial(void *arg) {
	struct rss *rss = (struct rss *)arg;
	dns_zone_t *zone = rss->zone;
	isc_result_t result = ISC_R_SUCCESS;
	dns_journal_t *rjournal = NULL;
	dns_journal_t *sjournal = NULL;
	uint32_t start, end = rss->serial;
	dns_difftuple_t *tuple = NULL, *soatuple = NULL;
	dns_update_log_t log = { update_log_cb, NULL };
	uint32_t newserial = 0, desired = 0;
	isc_time_t timenow;
	int level = ISC_LOG_ERROR;

	ENTER;

	LOCK_ZONE(zone);

	/*
	 * The receive_secure_serial() is loop-serialized for the zone.  Make
	 * sure there's no processing currently running.
	 */

	INSIST(zone->rss == NULL || zone->rss == rss);

	if (zone->rss != NULL) {
		INSIST(zone->rss == rss);
		UNLOCK_ZONE(zone);
	} else {
		zone->rss = rss;
		dns_diff_init(zone->mctx, &zone->rss_diff);

		/*
		 * zone->db may be NULL, if the load from disk failed.
		 */
		result = ISC_R_SUCCESS;
		ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
		if (zone->db != NULL) {
			dns_db_attach(zone->db, &zone->rss_db);
		} else {
			result = ISC_R_FAILURE;
		}
		ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);

		if (result == ISC_R_SUCCESS && zone->raw != NULL) {
			dns_zone_attach(zone->raw, &zone->rss_raw);
		} else {
			result = ISC_R_FAILURE;
		}

		UNLOCK_ZONE(zone);

		CHECK(result);

		/*
		 * We first attempt to sync the raw zone to the secure zone
		 * by using the raw zone's journal, applying all the deltas
		 * from the latest source-serial of the secure zone up to
		 * the current serial number of the raw zone.
		 *
		 * If that fails, then we'll fall back to a direct comparison
		 * between raw and secure zones.
		 */
		CHECK(dns_journal_open(zone->rss_raw->mctx,
				       zone->rss_raw->journal,
				       DNS_JOURNAL_WRITE, &rjournal));

		result = dns_journal_open(zone->mctx, zone->journal,
					  DNS_JOURNAL_READ, &sjournal);
		if (result != ISC_R_SUCCESS && result != ISC_R_NOTFOUND) {
			goto failure;
		}

		if (!dns_journal_get_sourceserial(rjournal, &start)) {
			start = dns_journal_first_serial(rjournal);
			dns_journal_set_sourceserial(rjournal, start);
		}
		if (sjournal != NULL) {
			uint32_t serial;
			/*
			 * We read the secure journal first, if that
			 * exists use its value provided it is greater
			 * that from the raw journal.
			 */
			if (dns_journal_get_sourceserial(sjournal, &serial)) {
				if (isc_serial_gt(serial, start)) {
					start = serial;
				}
			}
			dns_journal_destroy(&sjournal);
		}

		dns_db_currentversion(zone->rss_db, &zone->rss_oldver);
		CHECK(dns_db_newversion(zone->rss_db, &zone->rss_newver));

		/*
		 * Try to apply diffs from the raw zone's journal to the secure
		 * zone.  If that fails, we recover by syncing up the databases
		 * directly.
		 */
		result = sync_secure_journal(zone, zone->rss_raw, rjournal,
					     start, end, &soatuple,
					     &zone->rss_diff);
		if (result == DNS_R_UNCHANGED) {
			goto failure;
		} else if (result != ISC_R_SUCCESS) {
			CHECK(sync_secure_db(zone, zone->rss_raw, zone->rss_db,
					     zone->rss_oldver, &soatuple,
					     &zone->rss_diff));
		}

		CHECK(dns_diff_apply(&zone->rss_diff, zone->rss_db,
				     zone->rss_newver));

		if (soatuple != NULL) {
			uint32_t oldserial;

			CHECK(dns_db_createsoatuple(
				zone->rss_db, zone->rss_oldver,
				zone->rss_diff.mctx, DNS_DIFFOP_DEL, &tuple));
			oldserial = dns_soa_getserial(&tuple->rdata);
			newserial = desired =
				dns_soa_getserial(&soatuple->rdata);
			if (!isc_serial_gt(newserial, oldserial)) {
				newserial = oldserial + 1;
				if (newserial == 0) {
					newserial++;
				}
				dns_soa_setserial(newserial, &soatuple->rdata);
			}
			CHECK(do_one_tuple(&tuple, zone->rss_db,
					   zone->rss_newver, &zone->rss_diff));
			CHECK(do_one_tuple(&soatuple, zone->rss_db,
					   zone->rss_newver, &zone->rss_diff));
		} else {
			CHECK(update_soa_serial(zone, zone->rss_db,
						zone->rss_newver,
						&zone->rss_diff, zone->mctx,
						zone->updatemethod));
		}
	}
	result = dns_update_signaturesinc(
		&log, zone, zone->rss_db, zone->rss_oldver, zone->rss_newver,
		&zone->rss_diff, zone->sigvalidityinterval, &zone->rss_state);
	if (result == DNS_R_CONTINUE) {
		if (rjournal != NULL) {
			dns_journal_destroy(&rjournal);
		}
		isc_async_run(zone->loop, receive_secure_serial, rss);
		return;
	}

	/*
	 * If something went wrong while trying to update the secure zone and
	 * the latter was already signed before, do not apply raw zone deltas
	 * to it as that would break existing DNSSEC signatures.  However, if
	 * the secure zone was not yet signed (e.g. because no signing keys
	 * were created for it), commence applying raw zone deltas to it so
	 * that contents of the raw zone and the secure zone are kept in sync.
	 */
	if (result != ISC_R_SUCCESS && dns_db_issecure(zone->rss_db)) {
		goto failure;
	}

	if (rjournal == NULL) {
		CHECK(dns_journal_open(zone->rss_raw->mctx,
				       zone->rss_raw->journal,
				       DNS_JOURNAL_WRITE, &rjournal));
	}
	CHECK(zone_journal(zone, &zone->rss_diff, &end,
			   "receive_secure_serial"));

	dns_journal_set_sourceserial(rjournal, end);
	dns_journal_commit(rjournal);

	LOCK_ZONE(zone);
	DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NEEDNOTIFY);

	zone->sourceserial = end;
	zone->sourceserialset = true;
	zone_needdump(zone, DNS_DUMP_DELAY);

	/*
	 * Set resign time to make sure it is set to the earliest
	 * signature expiration.
	 */
	set_resigntime(zone);
	timenow = isc_time_now();
	zone_settimer(zone, &timenow);
	UNLOCK_ZONE(zone);

	dns_db_closeversion(zone->rss_db, &zone->rss_oldver, false);
	dns_db_closeversion(zone->rss_db, &zone->rss_newver, true);

	if (newserial != 0) {
		dns_zone_log(zone, ISC_LOG_INFO, "serial %u (unsigned %u)",
			     newserial, desired);
	}

failure:
	isc_mem_put(zone->mctx, rss, sizeof(*rss));
	zone->rss = NULL;

	if (zone->rss_raw != NULL) {
		dns_zone_detach(&zone->rss_raw);
	}
	if (result != ISC_R_SUCCESS) {
		LOCK_ZONE(zone);
		set_resigntime(zone);
		timenow = isc_time_now();
		zone_settimer(zone, &timenow);
		UNLOCK_ZONE(zone);
		if (result == DNS_R_UNCHANGED) {
			level = ISC_LOG_INFO;
		}
		dns_zone_log(zone, level, "receive_secure_serial: %s",
			     isc_result_totext(result));
	}
	if (tuple != NULL) {
		dns_difftuple_free(&tuple);
	}
	if (soatuple != NULL) {
		dns_difftuple_free(&soatuple);
	}
	if (zone->rss_db != NULL) {
		if (zone->rss_oldver != NULL) {
			dns_db_closeversion(zone->rss_db, &zone->rss_oldver,
					    false);
		}
		if (zone->rss_newver != NULL) {
			dns_db_closeversion(zone->rss_db, &zone->rss_newver,
					    false);
		}
		dns_db_detach(&zone->rss_db);
	}
	INSIST(zone->rss_oldver == NULL);
	INSIST(zone->rss_newver == NULL);
	if (rjournal != NULL) {
		dns_journal_destroy(&rjournal);
	}
	dns_diff_clear(&zone->rss_diff);

	dns_zone_idetach(&zone);
}

static isc_result_t
zone_send_secureserial(dns_zone_t *zone, uint32_t serial) {
	struct rss *rss = NULL;

	rss = isc_mem_get(zone->secure->mctx, sizeof(*rss));
	*rss = (struct rss){
		.serial = serial,
		.link = ISC_LINK_INITIALIZER,
	};

	INSIST(LOCKED_ZONE(zone->secure));
	zone_iattach(zone->secure, &rss->zone);
	isc_async_run(zone->secure->loop, receive_secure_serial, rss);

	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_SENDSECURE);
	return ISC_R_SUCCESS;
}

static isc_result_t
checkandaddsoa(dns_db_t *db, dns_dbnode_t *node, dns_dbversion_t *version,
	       dns_name_t *name, dns_rdataset_t *rdataset, uint32_t oldserial) {
	dns_rdata_soa_t soa;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	dns_rdatalist_t temprdatalist;
	dns_rdataset_t temprdataset;
	isc_buffer_t b;
	isc_result_t result;
	unsigned char buf[DNS_SOA_BUFFERSIZE];

	result = dns_rdataset_first(rdataset);
	RUNTIME_CHECK(result == ISC_R_SUCCESS);
	dns_rdataset_current(rdataset, &rdata);
	result = dns_rdata_tostruct(&rdata, &soa, NULL);
	RUNTIME_CHECK(result == ISC_R_SUCCESS);

	if (isc_serial_gt(soa.serial, oldserial)) {
		return dns_db_addrdataset(db, node, version, 0, rdataset, 0,
					  NULL);
	}
	/*
	 * Always bump the serial.
	 */
	oldserial++;
	if (oldserial == 0) {
		oldserial++;
	}
	soa.serial = oldserial;

	/*
	 * Construct a replacement rdataset.
	 */
	dns_rdata_reset(&rdata);
	isc_buffer_init(&b, buf, sizeof(buf));
	result = dns_rdata_fromstruct(&rdata, rdataset->rdclass,
				      dns_rdatatype_soa, &soa, &b);
	RUNTIME_CHECK(result == ISC_R_SUCCESS);
	dns_rdatalist_init(&temprdatalist);
	temprdatalist.rdclass = rdata.rdclass;
	temprdatalist.type = rdata.type;
	temprdatalist.ttl = rdataset->ttl;
	ISC_LIST_APPEND(temprdatalist.rdata, &rdata, link);

	dns_rdataset_init(&temprdataset);
	dns_rdatalist_tordataset(&temprdatalist, &temprdataset);

	dns_rdataset_getownercase(rdataset, name);
	dns_rdataset_setownercase(&temprdataset, name);
	return dns_db_addrdataset(db, node, version, 0, &temprdataset, 0, NULL);
}

/*
 * This function should populate an nsec3paramlist_t with the
 * nsecparam_t data from a zone.
 */
static isc_result_t
save_nsec3param(dns_zone_t *zone, nsec3paramlist_t *nsec3list) {
	isc_result_t result;
	dns_dbnode_t *node = NULL;
	dns_rdataset_t rdataset, prdataset;
	dns_dbversion_t *version = NULL;
	nsec3param_t *nsec3param = NULL;
	nsec3param_t *nsec3p = NULL;
	nsec3param_t *next;
	dns_db_t *db = NULL;
	unsigned char buf[DNS_NSEC3PARAM_BUFFERSIZE];

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(nsec3list != NULL);
	REQUIRE(ISC_LIST_EMPTY(*nsec3list));

	dns_rdataset_init(&rdataset);
	dns_rdataset_init(&prdataset);

	dns_db_attach(zone->db, &db);
	CHECK(dns_db_getoriginnode(db, &node));

	dns_db_currentversion(db, &version);
	result = dns_db_findrdataset(db, node, version,
				     dns_rdatatype_nsec3param,
				     dns_rdatatype_none, 0, &rdataset, NULL);

	if (result != ISC_R_SUCCESS) {
		goto getprivate;
	}

	/*
	 * Walk nsec3param rdataset making a list of parameters (note that
	 * multiple simultaneous nsec3 chains are annoyingly legal -- this
	 * is why we use an nsec3list, even though we will usually only
	 * have one).
	 */
	for (result = dns_rdataset_first(&rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&rdataset))
	{
		dns_rdata_t rdata = DNS_RDATA_INIT;
		dns_rdata_t private = DNS_RDATA_INIT;

		dns_rdataset_current(&rdataset, &rdata);
		isc_log_write(dns_lctx, DNS_LOGCATEGORY_GENERAL,
			      DNS_LOGMODULE_ZONE, ISC_LOG_DEBUG(3),
			      "looping through nsec3param data");
		nsec3param = isc_mem_get(zone->mctx, sizeof(nsec3param_t));
		ISC_LINK_INIT(nsec3param, link);

		/*
		 * now transfer the data from the rdata to
		 * the nsec3param
		 */
		dns_nsec3param_toprivate(&rdata, &private, zone->privatetype,
					 nsec3param->data,
					 sizeof(nsec3param->data));
		nsec3param->length = private.length;
		ISC_LIST_APPEND(*nsec3list, nsec3param, link);
	}

getprivate:
	result = dns_db_findrdataset(db, node, version, zone->privatetype,
				     dns_rdatatype_none, 0, &prdataset, NULL);
	if (result != ISC_R_SUCCESS) {
		goto done;
	}

	/*
	 * walk private type records, converting them to nsec3 parameters
	 * using dns_nsec3param_fromprivate(), do the right thing based on
	 * CREATE and REMOVE flags
	 */
	for (result = dns_rdataset_first(&prdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&prdataset))
	{
		dns_rdata_t rdata = DNS_RDATA_INIT;
		dns_rdata_t private = DNS_RDATA_INIT;

		dns_rdataset_current(&prdataset, &private);
		isc_log_write(dns_lctx, DNS_LOGCATEGORY_GENERAL,
			      DNS_LOGMODULE_ZONE, ISC_LOG_DEBUG(3),
			      "looping through nsec3param private data");

		/*
		 * Do we have a valid private record?
		 */
		if (!dns_nsec3param_fromprivate(&private, &rdata, buf,
						sizeof(buf)))
		{
			continue;
		}

		/*
		 * Remove any NSEC3PARAM records scheduled to be removed.
		 */
		if (NSEC3REMOVE(rdata.data[1])) {
			/*
			 * Zero out the flags.
			 */
			rdata.data[1] = 0;

			for (nsec3p = ISC_LIST_HEAD(*nsec3list); nsec3p != NULL;
			     nsec3p = next)
			{
				next = ISC_LIST_NEXT(nsec3p, link);

				if (nsec3p->length ==
					    (unsigned int)rdata.length + 1 &&
				    memcmp(rdata.data, nsec3p->data + 1,
					   nsec3p->length - 1) == 0)
				{
					ISC_LIST_UNLINK(*nsec3list, nsec3p,
							link);
					isc_mem_put(zone->mctx, nsec3p,
						    sizeof(nsec3param_t));
				}
			}
			continue;
		}

		nsec3param = isc_mem_get(zone->mctx, sizeof(nsec3param_t));
		ISC_LINK_INIT(nsec3param, link);

		/*
		 * Copy the remaining private records so the nsec/nsec3
		 * chain gets created.
		 */
		INSIST(private.length <= sizeof(nsec3param->data));
		memmove(nsec3param->data, private.data, private.length);
		nsec3param->length = private.length;
		ISC_LIST_APPEND(*nsec3list, nsec3param, link);
	}

done:
	if (result == ISC_R_NOMORE || result == ISC_R_NOTFOUND) {
		result = ISC_R_SUCCESS;
	}

failure:
	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}
	if (version != NULL) {
		dns_db_closeversion(db, &version, false);
	}
	if (db != NULL) {
		dns_db_detach(&db);
	}
	if (dns_rdataset_isassociated(&rdataset)) {
		dns_rdataset_disassociate(&rdataset);
	}
	if (dns_rdataset_isassociated(&prdataset)) {
		dns_rdataset_disassociate(&prdataset);
	}
	return result;
}

/*
 * Populate new zone db with private type records found by save_nsec3param().
 */
static isc_result_t
restore_nsec3param(dns_zone_t *zone, dns_db_t *db, dns_dbversion_t *version,
		   nsec3paramlist_t *nsec3list) {
	isc_result_t result = ISC_R_SUCCESS;
	dns_diff_t diff;
	dns_rdata_t rdata;
	nsec3param_t *nsec3p = NULL;
	nsec3param_t *next;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(!ISC_LIST_EMPTY(*nsec3list));

	dns_diff_init(zone->mctx, &diff);

	/*
	 * Loop through the list of private-type records, set the INITIAL
	 * and CREATE flags, and the add the record to the apex of the tree
	 * in db.
	 */
	for (nsec3p = ISC_LIST_HEAD(*nsec3list); nsec3p != NULL; nsec3p = next)
	{
		next = ISC_LIST_NEXT(nsec3p, link);
		dns_rdata_init(&rdata);
		nsec3p->data[2] = DNS_NSEC3FLAG_CREATE | DNS_NSEC3FLAG_INITIAL;
		rdata.length = nsec3p->length;
		rdata.data = nsec3p->data;
		rdata.type = zone->privatetype;
		rdata.rdclass = zone->rdclass;
		result = update_one_rr(db, version, &diff, DNS_DIFFOP_ADD,
				       &zone->origin, 0, &rdata);
		if (result != ISC_R_SUCCESS) {
			break;
		}
	}

	dns_diff_clear(&diff);
	return result;
}

static isc_result_t
copy_non_dnssec_records(dns_db_t *db, dns_db_t *version, dns_db_t *rawdb,
			dns_dbiterator_t *dbiterator, unsigned int *oldserial) {
	dns_dbnode_t *rawnode = NULL, *node = NULL;
	dns_fixedname_t fixed;
	dns_name_t *name = dns_fixedname_initname(&fixed);
	dns_rdataset_t rdataset;
	dns_rdatasetiter_t *rdsit = NULL;
	isc_result_t result;

	result = dns_dbiterator_current(dbiterator, &rawnode, name);
	if (result != ISC_R_SUCCESS) {
		return ISC_R_SUCCESS;
	}

	dns_dbiterator_pause(dbiterator);

	result = dns_db_findnode(db, name, true, &node);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	result = dns_db_allrdatasets(rawdb, rawnode, NULL, 0, 0, &rdsit);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	dns_rdataset_init(&rdataset);

	for (result = dns_rdatasetiter_first(rdsit); result == ISC_R_SUCCESS;
	     result = dns_rdatasetiter_next(rdsit))
	{
		dns_rdatasetiter_current(rdsit, &rdataset);
		if (rdataset.type == dns_rdatatype_nsec ||
		    rdataset.type == dns_rdatatype_rrsig ||
		    rdataset.type == dns_rdatatype_nsec3 ||
		    rdataset.type == dns_rdatatype_nsec3param)
		{
			dns_rdataset_disassociate(&rdataset);
			continue;
		}
		/*
		 * Allow DNSKEY, CDNSKEY, CDS because users should be able to
		 * update the zone with these records from a different provider,
		 * and thus they may exist in the raw version of the zone.
		 */

		if (rdataset.type == dns_rdatatype_soa && oldserial != NULL) {
			result = checkandaddsoa(db, node, version, name,
						&rdataset, *oldserial);
		} else {
			result = dns_db_addrdataset(db, node, version, 0,
						    &rdataset, 0, NULL);
		}
		dns_rdataset_disassociate(&rdataset);
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}
	}
	if (result == ISC_R_NOMORE) {
		result = ISC_R_SUCCESS;
	}

cleanup:
	if (rdsit != NULL) {
		dns_rdatasetiter_destroy(&rdsit);
	}
	if (rawnode) {
		dns_db_detachnode(rawdb, &rawnode);
	}
	if (node) {
		dns_db_detachnode(db, &node);
	}
	return result;
}

static void
receive_secure_db(void *arg) {
	isc_result_t result;
	struct rss *rss = (struct rss *)arg;
	dns_zone_t *zone = rss->zone;
	dns_db_t *rawdb = rss->db, *db = NULL;
	dns_dbiterator_t *dbiterator = NULL;
	dns_dbversion_t *version = NULL;
	isc_time_t loadtime;
	unsigned int oldserial = 0, *oldserialp = NULL;
	nsec3paramlist_t nsec3list;

	ISC_LIST_INIT(nsec3list);

	LOCK_ZONE(zone);
	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING) || !inline_secure(zone)) {
		result = ISC_R_SHUTTINGDOWN;
		goto failure;
	}

	loadtime = isc_time_now();
	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	if (zone->db != NULL) {
		result = dns_db_getsoaserial(zone->db, NULL, &oldserial);
		if (result == ISC_R_SUCCESS) {
			oldserialp = &oldserial;
		}

		/*
		 * assemble nsec3parameters from the old zone, and set a flag
		 * if any are found
		 */
		result = save_nsec3param(zone, &nsec3list);
		if (result != ISC_R_SUCCESS) {
			ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
			goto failure;
		}
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);

	result = dns_db_create(zone->mctx, zone->db_argv[0], &zone->origin,
			       dns_dbtype_zone, zone->rdclass,
			       zone->db_argc - 1, zone->db_argv + 1, &db);
	if (result != ISC_R_SUCCESS) {
		goto failure;
	}

	result = dns_db_setgluecachestats(db, zone->gluecachestats);
	if (result != ISC_R_SUCCESS && result != ISC_R_NOTIMPLEMENTED) {
		goto failure;
	}

	result = dns_db_newversion(db, &version);
	if (result != ISC_R_SUCCESS) {
		goto failure;
	}

	result = dns_db_createiterator(rawdb, DNS_DB_NONSEC3, &dbiterator);
	if (result != ISC_R_SUCCESS) {
		goto failure;
	}

	for (result = dns_dbiterator_first(dbiterator); result == ISC_R_SUCCESS;
	     result = dns_dbiterator_next(dbiterator))
	{
		result = copy_non_dnssec_records(db, version, rawdb, dbiterator,
						 oldserialp);
		if (result != ISC_R_SUCCESS) {
			goto failure;
		}
	}
	dns_dbiterator_destroy(&dbiterator);
	if (result != ISC_R_NOMORE) {
		goto failure;
	}

	/*
	 * Call restore_nsec3param() to create private-type records from
	 * the old nsec3 parameters and insert them into db
	 */
	if (!ISC_LIST_EMPTY(nsec3list)) {
		result = restore_nsec3param(zone, db, version, &nsec3list);
		if (result != ISC_R_SUCCESS) {
			goto failure;
		}
	}

	dns_db_closeversion(db, &version, true);

	/*
	 * Lock hierarchy: zmgr, zone, raw.
	 */
	INSIST(zone != zone->raw);
	LOCK_ZONE(zone->raw);
	DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NEEDNOTIFY);
	result = zone_postload(zone, db, loadtime, ISC_R_SUCCESS);
	zone_needdump(zone, 0); /* XXXMPA */
	UNLOCK_ZONE(zone->raw);

	/*
	 * Process any queued NSEC3PARAM change requests.
	 */
	process_zone_setnsec3param(zone);

failure:
	UNLOCK_ZONE(zone);
	if (dbiterator != NULL) {
		dns_dbiterator_destroy(&dbiterator);
	}
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_ERROR, "receive_secure_db: %s",
			     isc_result_totext(result));
	}

	while (!ISC_LIST_EMPTY(nsec3list)) {
		nsec3param_t *nsec3p;
		nsec3p = ISC_LIST_HEAD(nsec3list);
		ISC_LIST_UNLINK(nsec3list, nsec3p, link);
		isc_mem_put(zone->mctx, nsec3p, sizeof(nsec3param_t));
	}
	if (db != NULL) {
		if (version != NULL) {
			dns_db_closeversion(db, &version, false);
		}
		dns_db_detach(&db);
	}

	dns_db_detach(&rawdb);
	isc_mem_put(zone->mctx, rss, sizeof(*rss));
	dns_zone_idetach(&zone);

	INSIST(version == NULL);
}

static isc_result_t
zone_send_securedb(dns_zone_t *zone, dns_db_t *db) {
	struct rss *rss = NULL;

	rss = isc_mem_get(zone->secure->mctx, sizeof(*rss));
	*rss = (struct rss){ .link = ISC_LINK_INITIALIZER };

	INSIST(LOCKED_ZONE(zone->secure));
	zone_iattach(zone->secure, &rss->zone);
	dns_db_attach(db, &rss->db);
	isc_async_run(zone->secure->loop, receive_secure_db, rss);

	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_SENDSECURE);
	return ISC_R_SUCCESS;
}

isc_result_t
dns_zone_replacedb(dns_zone_t *zone, dns_db_t *db, bool dump) {
	isc_result_t result;
	dns_zone_t *secure = NULL;

	REQUIRE(DNS_ZONE_VALID(zone));
again:
	LOCK_ZONE(zone);
	if (inline_raw(zone)) {
		secure = zone->secure;
		INSIST(secure != zone);
		TRYLOCK_ZONE(result, secure);
		if (result != ISC_R_SUCCESS) {
			UNLOCK_ZONE(zone);
			secure = NULL;
			isc_thread_yield();
			goto again;
		}
	}
	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_write);
	result = zone_replacedb(zone, db, dump);
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_write);
	if (secure != NULL) {
		UNLOCK_ZONE(secure);
	}
	UNLOCK_ZONE(zone);
	return result;
}

static isc_result_t
zone_replacedb(dns_zone_t *zone, dns_db_t *db, bool dump) {
	dns_dbversion_t *ver;
	isc_result_t result;
	unsigned int soacount = 0;
	unsigned int nscount = 0;

	/*
	 * 'zone' and 'zone->db' locked by caller.
	 */
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(LOCKED_ZONE(zone));
	if (inline_raw(zone)) {
		REQUIRE(LOCKED_ZONE(zone->secure));
	}

	result = zone_get_from_db(zone, db, &nscount, &soacount, NULL, NULL,
				  NULL, NULL, NULL, NULL, NULL);
	if (result == ISC_R_SUCCESS) {
		if (soacount != 1) {
			dns_zone_log(zone, ISC_LOG_ERROR, "has %d SOA records",
				     soacount);
			result = DNS_R_BADZONE;
		}
		if (nscount == 0 && zone->type != dns_zone_key) {
			dns_zone_log(zone, ISC_LOG_ERROR, "has no NS records");
			result = DNS_R_BADZONE;
		}
		if (result != ISC_R_SUCCESS) {
			return result;
		}
	} else {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "retrieving SOA and NS records failed: %s",
			     isc_result_totext(result));
		return result;
	}

	result = check_nsec3param(zone, db);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	ver = NULL;
	dns_db_currentversion(db, &ver);

	/*
	 * The initial version of a secondary zone is always dumped;
	 * subsequent versions may be journaled instead if this
	 * is enabled in the configuration.
	 */
	if (zone->db != NULL && zone->journal != NULL &&
	    DNS_ZONE_OPTION(zone, DNS_ZONEOPT_IXFRFROMDIFFS) &&
	    !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_FORCEXFER))
	{
		uint32_t serial, oldserial;

		dns_zone_log(zone, ISC_LOG_DEBUG(3), "generating diffs");

		result = dns_db_getsoaserial(db, ver, &serial);
		if (result != ISC_R_SUCCESS) {
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "ixfr-from-differences: unable to get "
				     "new serial");
			goto fail;
		}

		/*
		 * This is checked in zone_postload() for primary zones.
		 */
		result = zone_get_from_db(zone, zone->db, NULL, &soacount, NULL,
					  &oldserial, NULL, NULL, NULL, NULL,
					  NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
		RUNTIME_CHECK(soacount > 0U);
		if ((zone->type == dns_zone_secondary ||
		     (zone->type == dns_zone_redirect &&
		      dns_remote_addresses(&zone->primaries) != NULL)) &&
		    !isc_serial_gt(serial, oldserial))
		{
			uint32_t serialmin, serialmax;
			serialmin = (oldserial + 1) & 0xffffffffU;
			serialmax = (oldserial + 0x7fffffffU) & 0xffffffffU;
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "ixfr-from-differences: failed: "
				     "new serial (%u) out of range [%u - %u]",
				     serial, serialmin, serialmax);
			result = ISC_R_RANGE;
			goto fail;
		}

		result = dns_db_diff(zone->mctx, db, ver, zone->db, NULL,
				     zone->journal);
		if (result != ISC_R_SUCCESS) {
			char strbuf[ISC_STRERRORSIZE];
			strerror_r(errno, strbuf, sizeof(strbuf));
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "ixfr-from-differences: failed: "
				     "%s",
				     strbuf);
			goto fallback;
		}
		if (dump) {
			zone_needdump(zone, DNS_DUMP_DELAY);
		} else {
			zone_journal_compact(zone, zone->db, serial);
		}
		if (zone->type == dns_zone_primary && inline_raw(zone)) {
			zone_send_secureserial(zone, serial);
		}
	} else {
	fallback:
		if (dump && zone->masterfile != NULL) {
			/*
			 * If DNS_ZONEFLG_FORCEXFER was set we don't want
			 * to keep the old masterfile.
			 */
			if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_FORCEXFER) &&
			    remove(zone->masterfile) < 0 && errno != ENOENT)
			{
				char strbuf[ISC_STRERRORSIZE];
				strerror_r(errno, strbuf, sizeof(strbuf));
				isc_log_write(dns_lctx, DNS_LOGCATEGORY_GENERAL,
					      DNS_LOGMODULE_ZONE,
					      ISC_LOG_WARNING,
					      "unable to remove masterfile "
					      "'%s': '%s'",
					      zone->masterfile, strbuf);
			}
			if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADED) == 0) {
				DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NODELAY);
			} else {
				zone_needdump(zone, 0);
			}
		}
		if (dump && zone->journal != NULL) {
			/*
			 * The in-memory database just changed, and
			 * because 'dump' is set, it didn't change by
			 * being loaded from disk.  Also, we have not
			 * journaled diffs for this change.
			 * Therefore, the on-disk journal is missing
			 * the deltas for this change.	Since it can
			 * no longer be used to bring the zone
			 * up-to-date, it is useless and should be
			 * removed.
			 */
			isc_log_write(dns_lctx, DNS_LOGCATEGORY_GENERAL,
				      DNS_LOGMODULE_ZONE, ISC_LOG_DEBUG(3),
				      "removing journal file");
			if (remove(zone->journal) < 0 && errno != ENOENT) {
				char strbuf[ISC_STRERRORSIZE];
				strerror_r(errno, strbuf, sizeof(strbuf));
				isc_log_write(dns_lctx, DNS_LOGCATEGORY_GENERAL,
					      DNS_LOGMODULE_ZONE,
					      ISC_LOG_WARNING,
					      "unable to remove journal "
					      "'%s': '%s'",
					      zone->journal, strbuf);
			}
		}

		if (inline_raw(zone)) {
			zone_send_securedb(zone, db);
		}
	}

	dns_db_closeversion(db, &ver, false);

	dns_zone_log(zone, ISC_LOG_DEBUG(3), "replacing zone database");

	if (zone->db != NULL) {
		zone_detachdb(zone);
	}
	zone_attachdb(zone, db);
	dns_db_setloop(zone->db, zone->loop);
	dns_db_setmaxrrperset(zone->db, zone->maxrrperset);
	dns_db_setmaxtypepername(zone->db, zone->maxtypepername);
	DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_LOADED | DNS_ZONEFLG_NEEDNOTIFY);
	return ISC_R_SUCCESS;

fail:
	dns_db_closeversion(db, &ver, false);
	return result;
}

/* The caller must hold the dblock as a writer. */
static void
zone_attachdb(dns_zone_t *zone, dns_db_t *db) {
	REQUIRE(zone->db == NULL && db != NULL);

	dns_db_attach(db, &zone->db);
}

/* The caller must hold the dblock as a writer. */
static void
zone_detachdb(dns_zone_t *zone) {
	REQUIRE(zone->db != NULL);

	dns_zone_rpz_disable_db(zone, zone->db);
	dns_zone_catz_disable_db(zone, zone->db);
	dns_db_detach(&zone->db);
}

static void
zone_xfrdone(dns_zone_t *zone, uint32_t *expireopt, isc_result_t result) {
	isc_time_t now, expiretime;
	bool again = false;
	unsigned int soacount;
	unsigned int nscount;
	uint32_t serial, refresh, retry, expire, minimum, soattl, oldexpire;
	isc_result_t xfrresult = result;
	bool free_needed;
	dns_zone_t *secure = NULL;

	REQUIRE(DNS_ZONE_VALID(zone));

	dns_zone_logc(
		zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_DEBUG(1),
		expireopt == NULL ? "zone transfer finished: %s"
				  : "zone transfer finished: %s, expire=%u",
		isc_result_totext(result), expireopt != NULL ? *expireopt : 0);

	/*
	 * Obtaining a lock on the zone->secure (see zone_send_secureserial)
	 * could result in a deadlock due to a LOR so we will spin if we
	 * can't obtain both locks.
	 */
again:
	LOCK_ZONE(zone);
	if (inline_raw(zone)) {
		secure = zone->secure;
		INSIST(secure != zone);
		TRYLOCK_ZONE(result, secure);
		if (result != ISC_R_SUCCESS) {
			UNLOCK_ZONE(zone);
			secure = NULL;
			isc_thread_yield();
			goto again;
		}
	}

	INSIST(DNS_ZONE_FLAG(zone, DNS_ZONEFLG_REFRESH));
	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_REFRESH);
	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_SOABEFOREAXFR);

	now = isc_time_now();
	switch (xfrresult) {
	case ISC_R_SUCCESS:
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NEEDNOTIFY);
		FALLTHROUGH;
	case DNS_R_UPTODATE:
		DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_FORCEXFER |
					       DNS_ZONEFLG_FIRSTREFRESH);
		/*
		 * Has the zone expired underneath us?
		 */
		ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
		if (zone->db == NULL) {
			ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
			goto same_primary;
		}

		oldexpire = zone->expire;

		/*
		 * Update the zone structure's data from the actual
		 * SOA received.
		 */
		nscount = 0;
		soacount = 0;
		INSIST(zone->db != NULL);
		result = zone_get_from_db(zone, zone->db, &nscount, &soacount,
					  &soattl, &serial, &refresh, &retry,
					  &expire, &minimum, NULL);
		ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
		if (result == ISC_R_SUCCESS) {
			if (soacount != 1) {
				dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
					      ISC_LOG_ERROR,
					      "transferred zone "
					      "has %d SOA records",
					      soacount);
				if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_HAVETIMERS))
				{
					zone->refresh = DNS_ZONE_DEFAULTREFRESH;
					zone->retry = DNS_ZONE_DEFAULTRETRY;
				}
				DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_HAVETIMERS);
				zone_unload(zone);
				goto next_primary;
			}
			if (nscount == 0) {
				dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
					      ISC_LOG_ERROR,
					      "transferred zone "
					      "has no NS records");
				if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_HAVETIMERS))
				{
					zone->refresh = DNS_ZONE_DEFAULTREFRESH;
					zone->retry = DNS_ZONE_DEFAULTRETRY;
				}
				DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_HAVETIMERS);
				zone_unload(zone);
				goto next_primary;
			}
			zone->refresh = RANGE(refresh, zone->minrefresh,
					      zone->maxrefresh);
			zone->retry = RANGE(retry, zone->minretry,
					    zone->maxretry);
			zone->expire = RANGE(expire,
					     zone->refresh + zone->retry,
					     DNS_MAX_EXPIRE);
			zone->soattl = soattl;
			zone->minimum = minimum;
			DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_HAVETIMERS);
		}

		/*
		 * Set our next refresh time.
		 */
		if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDREFRESH)) {
			DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_NEEDREFRESH);
			zone->refreshtime = now;
		} else {
			DNS_ZONE_JITTER_ADD(&now, zone->refresh,
					    &zone->refreshtime);
		}

		/*
		 * Set our next expire time. If the parent returned
		 * an EXPIRE option use that to update zone->expiretime.
		 */
		expire = zone->expire;
		if (expireopt != NULL && *expireopt < expire) {
			expire = *expireopt;
		}
		DNS_ZONE_TIME_ADD(&now, expire, &expiretime);
		if (oldexpire != zone->expire ||
		    isc_time_compare(&expiretime, &zone->expiretime) > 0)
		{
			zone->expiretime = expiretime;
		}

		/*
		 * Set loadtime.
		 */
		zone->loadtime = now;

		if (result == ISC_R_SUCCESS && xfrresult == ISC_R_SUCCESS) {
			char buf[DNS_NAME_FORMATSIZE + sizeof(": TSIG ''")];
			if (zone->tsigkey != NULL) {
				char namebuf[DNS_NAME_FORMATSIZE];
				dns_name_format(zone->tsigkey->name, namebuf,
						sizeof(namebuf));
				snprintf(buf, sizeof(buf), ": TSIG '%s'",
					 namebuf);
			} else {
				buf[0] = '\0';
			}
			dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
				      ISC_LOG_INFO, "transferred serial %u%s",
				      serial, buf);
			if (inline_raw(zone)) {
				zone_send_secureserial(zone, serial);
			}
		}

		/*
		 * This is not necessary if we just performed a AXFR
		 * however it is necessary for an IXFR / UPTODATE and
		 * won't hurt with an AXFR.
		 */
		if (zone->masterfile != NULL || zone->journal != NULL) {
			unsigned int delay = DNS_DUMP_DELAY;
			isc_interval_t i;
			isc_time_t when;

			/*
			 * Compute effective modification time.
			 */
			isc_interval_set(&i, zone->expire, 0);
			result = isc_time_subtract(&zone->expiretime, &i,
						   &when);
			if (result != ISC_R_SUCCESS) {
				when = now;
			}

			result = ISC_R_FAILURE;
			if (zone->journal != NULL) {
				result = isc_file_settime(zone->journal, &when);
			}
			if (result != ISC_R_SUCCESS && zone->masterfile != NULL)
			{
				result = isc_file_settime(zone->masterfile,
							  &when);
			}

			if ((DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NODELAY) != 0) ||
			    result == ISC_R_FILENOTFOUND)
			{
				delay = 0;
			}

			if ((result == ISC_R_SUCCESS ||
			     result == ISC_R_FILENOTFOUND) &&
			    zone->masterfile != NULL)
			{
				zone_needdump(zone, delay);
			} else if (result != ISC_R_SUCCESS) {
				dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
					      ISC_LOG_ERROR,
					      "transfer: could not set file "
					      "modification time of '%s': %s",
					      zone->masterfile,
					      isc_result_totext(result));
			}
		}
		DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_NODELAY);
		inc_stats(zone, dns_zonestatscounter_xfrsuccess);
		break;

	case DNS_R_BADIXFR:
		/* Force retry with AXFR. */
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NOIXFR);
		goto same_primary;

	case DNS_R_TOOMANYRECORDS:
	case DNS_R_VERIFYFAILURE:
		DNS_ZONE_JITTER_ADD(&now, zone->refresh, &zone->refreshtime);
		inc_stats(zone, dns_zonestatscounter_xfrfail);
		break;

	case ISC_R_SHUTTINGDOWN:
		dns_remote_reset(&zone->primaries, true);
		break;

	default:
	next_primary:
		/*
		 * Skip to next failed / untried primary.
		 */
		dns_remote_next(&zone->primaries, true);
	same_primary:
		if (dns_remote_done(&zone->primaries)) {
			dns_remote_reset(&zone->primaries, false);
		} else {
			DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_REFRESH);
			again = true;
		}
		inc_stats(zone, dns_zonestatscounter_xfrfail);
		break;
	}
	zone_settimer(zone, &now);

	/*
	 * We are called as the done callback of a zone
	 * transfer object that just entered its shutting-down state or
	 * failed to start.  Since we are no longer responsible for shutting
	 * it down, we can detach our reference.
	 */
	if (zone->xfr != NULL) {
		dns_xfrin_detach(&zone->xfr);
	}

	if (zone->tsigkey != NULL) {
		dns_tsigkey_detach(&zone->tsigkey);
	}

	if (zone->transport != NULL) {
		dns_transport_detach(&zone->transport);
	}

	/*
	 * Handle any deferred journal compaction.
	 */
	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDCOMPACT)) {
		dns_db_t *db = NULL;
		if (dns_zone_getdb(zone, &db) == ISC_R_SUCCESS) {
			zone_journal_compact(zone, db, zone->compact_serial);
			dns_db_detach(&db);
			DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_NEEDCOMPACT);
		}
	}

	if (secure != NULL) {
		UNLOCK_ZONE(secure);
	}
	/*
	 * This transfer finishing freed up a transfer quota slot.
	 * Let any other zones waiting for quota have it.
	 */
	if (zone->zmgr != NULL &&
	    zone->statelist == &zone->zmgr->xfrin_in_progress)
	{
		UNLOCK_ZONE(zone);
		RWLOCK(&zone->zmgr->rwlock, isc_rwlocktype_write);
		ISC_LIST_UNLINK(zone->zmgr->xfrin_in_progress, zone, statelink);
		zone->statelist = NULL;
		zmgr_resume_xfrs(zone->zmgr, false);
		RWUNLOCK(&zone->zmgr->rwlock, isc_rwlocktype_write);
		LOCK_ZONE(zone);
	}

	/*
	 * Retry with a different server if necessary.
	 */
	if (again && !DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING)) {
		queue_soa_query(zone);
	}

	isc_refcount_decrement(&zone->irefs);
	free_needed = exit_check(zone);
	UNLOCK_ZONE(zone);
	if (free_needed) {
		zone_free(zone);
	}
}

static void
zone_loaddone(void *arg, isc_result_t result) {
	dns_load_t *load = arg;
	dns_zone_t *zone;
	isc_result_t tresult;
	dns_zone_t *secure = NULL;

	zone = load->zone;

	ENTER;

	/*
	 * If zone loading failed, remove the update db callbacks prior
	 * to calling the list of callbacks in the zone load structure.
	 */
	if (result != ISC_R_SUCCESS) {
		dns_zone_rpz_disable_db(zone, load->db);
		dns_zone_catz_disable_db(zone, load->db);
	}

	tresult = dns_db_endload(load->db, &load->callbacks);
	if (tresult != ISC_R_SUCCESS &&
	    (result == ISC_R_SUCCESS || result == DNS_R_SEENINCLUDE))
	{
		result = tresult;
	}

	/*
	 * Lock hierarchy: zmgr, zone, raw.
	 */
again:
	LOCK_ZONE(zone);
	INSIST(zone != zone->raw);
	if (inline_secure(zone)) {
		LOCK_ZONE(zone->raw);
	} else if (inline_raw(zone)) {
		secure = zone->secure;
		TRYLOCK_ZONE(tresult, secure);
		if (tresult != ISC_R_SUCCESS) {
			UNLOCK_ZONE(zone);
			secure = NULL;
			isc_thread_yield();
			goto again;
		}
	}
	(void)zone_postload(zone, load->db, load->loadtime, result);
	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_LOADING);
	zone_idetach(&load->callbacks.zone);
	/*
	 * Leave the zone frozen if the reload fails.
	 */
	if ((result == ISC_R_SUCCESS || result == DNS_R_SEENINCLUDE) &&
	    DNS_ZONE_FLAG(zone, DNS_ZONEFLG_THAW))
	{
		zone->update_disabled = false;
	}
	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_THAW);
	if (inline_secure(zone)) {
		UNLOCK_ZONE(zone->raw);
	} else if (secure != NULL) {
		UNLOCK_ZONE(secure);
	}
	UNLOCK_ZONE(zone);

	dns_db_detach(&load->db);
	if (zone->loadctx != NULL) {
		dns_loadctx_detach(&zone->loadctx);
	}
	isc_mem_put(zone->mctx, load, sizeof(*load));

	dns_zone_idetach(&zone);
}

void
dns_zone_getssutable(dns_zone_t *zone, dns_ssutable_t **table) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(table != NULL);
	REQUIRE(*table == NULL);

	LOCK_ZONE(zone);
	if (zone->ssutable != NULL) {
		dns_ssutable_attach(zone->ssutable, table);
	}
	UNLOCK_ZONE(zone);
}

void
dns_zone_setssutable(dns_zone_t *zone, dns_ssutable_t *table) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->ssutable != NULL) {
		dns_ssutable_detach(&zone->ssutable);
	}
	if (table != NULL) {
		dns_ssutable_attach(table, &zone->ssutable);
	}
	UNLOCK_ZONE(zone);
}

void
dns_zone_setsigvalidityinterval(dns_zone_t *zone, uint32_t interval) {
	REQUIRE(DNS_ZONE_VALID(zone));

	zone->sigvalidityinterval = interval;
}

uint32_t
dns_zone_getsigvalidityinterval(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->sigvalidityinterval;
}

void
dns_zone_setkeyvalidityinterval(dns_zone_t *zone, uint32_t interval) {
	REQUIRE(DNS_ZONE_VALID(zone));

	zone->keyvalidityinterval = interval;
}

uint32_t
dns_zone_getkeyvalidityinterval(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->keyvalidityinterval;
}

void
dns_zone_setsigresigninginterval(dns_zone_t *zone, uint32_t interval) {
	isc_time_t now;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	zone->sigresigninginterval = interval;
	set_resigntime(zone);
	if (zone->loop != NULL) {
		now = isc_time_now();
		zone_settimer(zone, &now);
	}
	UNLOCK_ZONE(zone);
}

uint32_t
dns_zone_getsigresigninginterval(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->sigresigninginterval;
}

void
dns_zone_getsourceaddr(dns_zone_t *zone, isc_sockaddr_t *sourceaddr) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(sourceaddr != NULL);

	LOCK_ZONE(zone);
	INSIST(dns_remote_count(&zone->primaries) > 0);
	*sourceaddr = zone->sourceaddr;
	UNLOCK_ZONE(zone);
}

isc_result_t
dns_zone_getprimaryaddr(dns_zone_t *zone, isc_sockaddr_t *primaryaddr) {
	isc_result_t result = ISC_R_NOMORE;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(primaryaddr != NULL);

	LOCK_ZONE(zone);
	INSIST(dns_remote_count(&zone->primaries) > 0);
	if (!dns_remote_done(&zone->primaries)) {
		*primaryaddr = dns_remote_curraddr(&zone->primaries);
		result = ISC_R_SUCCESS;
	}
	UNLOCK_ZONE(zone);

	return result;
}

isc_time_t
dns_zone_getxfrintime(dns_zone_t *zone) {
	isc_time_t xfrintime;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	xfrintime = zone->xfrintime;
	UNLOCK_ZONE(zone);

	return xfrintime;
}

static void
queue_xfrin(dns_zone_t *zone) {
	isc_result_t result;
	dns_zonemgr_t *zmgr = zone->zmgr;

	ENTER;

	INSIST(zone->statelist == NULL);

	RWLOCK(&zmgr->rwlock, isc_rwlocktype_write);
	ISC_LIST_APPEND(zmgr->waiting_for_xfrin, zone, statelink);
	isc_refcount_increment0(&zone->irefs);
	zone->statelist = &zmgr->waiting_for_xfrin;
	result = zmgr_start_xfrin_ifquota(zmgr, zone);
	RWUNLOCK(&zmgr->rwlock, isc_rwlocktype_write);

	if (result == ISC_R_QUOTA) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_INFO,
			      "zone transfer deferred due to quota");
	} else if (result != ISC_R_SUCCESS) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_ERROR,
			      "starting zone transfer: %s",
			      isc_result_totext(result));
	}
}

/*
 * Get the transport type used for the SOA query to the current primary server
 * before an ongoing incoming zone transfer.
 *
 * Requires:
 *	The zone is locked by the caller.
 */
static dns_transport_type_t
get_request_transport_type(dns_zone_t *zone) {
	dns_transport_type_t transport_type = DNS_TRANSPORT_NONE;

	if (zone->transport != NULL) {
		transport_type = dns_transport_get_type(zone->transport);
	} else {
		transport_type = (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_USEVC))
					 ? DNS_TRANSPORT_TCP
					 : DNS_TRANSPORT_UDP;

		/* Check if the peer is forced to always use TCP. */
		if (transport_type != DNS_TRANSPORT_TCP &&
		    !dns_remote_done(&zone->primaries))
		{
			isc_result_t result;
			isc_sockaddr_t primaryaddr;
			isc_netaddr_t primaryip;
			dns_peer_t *peer = NULL;

			primaryaddr = dns_remote_curraddr(&zone->primaries);
			isc_netaddr_fromsockaddr(&primaryip, &primaryaddr);
			result = dns_peerlist_peerbyaddr(zone->view->peers,
							 &primaryip, &peer);
			if (result == ISC_R_SUCCESS && peer != NULL) {
				bool usetcp;
				result = dns_peer_getforcetcp(peer, &usetcp);
				if (result == ISC_R_SUCCESS && usetcp) {
					transport_type = DNS_TRANSPORT_TCP;
				}
			}
		}
	}

	return transport_type;
}

dns_transport_type_t
dns_zone_getrequesttransporttype(dns_zone_t *zone) {
	dns_transport_type_t transport_type;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	transport_type = get_request_transport_type(zone);
	UNLOCK_ZONE(zone);

	return transport_type;
}

/*
 * This event callback is called when a zone has received
 * any necessary zone transfer quota.  This is the time
 * to go ahead and start the transfer.
 */
static void
got_transfer_quota(void *arg) {
	dns_zone_t *zone = (dns_zone_t *)arg;
	isc_result_t result = ISC_R_SUCCESS;
	dns_peer_t *peer = NULL;
	char primary[ISC_SOCKADDR_FORMATSIZE];
	char source[ISC_SOCKADDR_FORMATSIZE];
	dns_rdatatype_t xfrtype;
	isc_netaddr_t primaryip;
	isc_sockaddr_t primaryaddr;
	isc_sockaddr_t sourceaddr;
	isc_time_t now;
	dns_transport_type_t soa_transport_type = DNS_TRANSPORT_NONE;
	const char *soa_before = "";
	bool loaded;
	isc_tlsctx_cache_t *zmgr_tlsctx_cache = NULL;
	dns_xfrin_t *xfr = NULL;

	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING)) {
		zone_xfrdone(zone, NULL, ISC_R_CANCELED);
		return;
	}

	now = isc_time_now();

	primaryaddr = dns_remote_curraddr(&zone->primaries);
	isc_sockaddr_format(&primaryaddr, primary, sizeof(primary));
	if (dns_zonemgr_unreachable(zone->zmgr, &primaryaddr, &zone->sourceaddr,
				    &now))
	{
		isc_sockaddr_format(&zone->sourceaddr, source, sizeof(source));
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_INFO,
			      "got_transfer_quota: skipping zone transfer as "
			      "primary %s (source %s) is unreachable (cached)",
			      primary, source);
		zone_xfrdone(zone, NULL, ISC_R_CANCELED);
		return;
	}

	isc_netaddr_fromsockaddr(&primaryip, &primaryaddr);
	(void)dns_peerlist_peerbyaddr(zone->view->peers, &primaryip, &peer);

	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_SOABEFOREAXFR)) {
		soa_before = "SOA before ";
	}
	/*
	 * Decide whether we should request IXFR or AXFR.
	 */
	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	loaded = (zone->db != NULL);
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);

	if (!loaded) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_DEBUG(1),
			      "no database exists yet, requesting AXFR of "
			      "initial version from %s",
			      primary);
		xfrtype = dns_rdatatype_axfr;
	} else if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_FORCEXFER)) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_DEBUG(1),
			      "forced reload, requesting AXFR of "
			      "initial version from %s",
			      primary);
		xfrtype = dns_rdatatype_axfr;
	} else if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NOIXFR)) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_DEBUG(1),
			      "retrying with AXFR from %s due to "
			      "previous IXFR failure",
			      primary);
		xfrtype = dns_rdatatype_axfr;
		LOCK_ZONE(zone);
		DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_NOIXFR);
		UNLOCK_ZONE(zone);
	} else {
		bool use_ixfr = true;
		if (peer != NULL) {
			result = dns_peer_getrequestixfr(peer, &use_ixfr);
		}
		if (peer == NULL || result != ISC_R_SUCCESS) {
			use_ixfr = zone->requestixfr;
		}
		if (!use_ixfr) {
			dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
				      ISC_LOG_DEBUG(1),
				      "IXFR disabled, "
				      "requesting %sAXFR from %s",
				      soa_before, primary);
			if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_SOABEFOREAXFR)) {
				xfrtype = dns_rdatatype_soa;
			} else {
				xfrtype = dns_rdatatype_axfr;
			}
		} else {
			dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
				      ISC_LOG_DEBUG(1),
				      "requesting IXFR from %s", primary);
			xfrtype = dns_rdatatype_ixfr;
		}
	}

	/*
	 * Determine if we should attempt to sign the request with TSIG.
	 */
	result = ISC_R_NOTFOUND;

	/*
	 * First, look for a tsig key in the primaries statement, then
	 * try for a server key.
	 */
	if (dns_remote_keyname(&zone->primaries) != NULL) {
		dns_view_t *view = dns_zone_getview(zone);
		dns_name_t *keyname = dns_remote_keyname(&zone->primaries);
		result = dns_view_gettsig(view, keyname, &zone->tsigkey);
	}
	if (result != ISC_R_SUCCESS) {
		INSIST(zone->tsigkey == NULL);
		result = dns_view_getpeertsig(zone->view, &primaryip,
					      &zone->tsigkey);
	}
	if (result != ISC_R_SUCCESS && result != ISC_R_NOTFOUND) {
		dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_ERROR,
			      "could not get TSIG key for zone transfer: %s",
			      isc_result_totext(result));
	}

	/*
	 * Get the TLS transport for the primary, if configured.
	 */
	if (dns_remote_tlsname(&zone->primaries) != NULL) {
		dns_view_t *view = dns_zone_getview(zone);
		dns_name_t *tlsname = dns_remote_tlsname(&zone->primaries);
		result = dns_view_gettransport(view, DNS_TRANSPORT_TLS, tlsname,
					       &zone->transport);
		if (result != ISC_R_SUCCESS && result != ISC_R_NOTFOUND) {
			dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
				      ISC_LOG_ERROR,
				      "could not get TLS configuration for "
				      "zone transfer: %s",
				      isc_result_totext(result));
		}
	}

	LOCK_ZONE(zone);
	if (xfrtype != dns_rdatatype_soa) {
		/*
		 * If 'xfrtype' is dns_rdatatype_soa, then the SOA query will be
		 * performed by xfrin, otherwise, the SOA request performed by
		 * soa_query() was successful and we should inform the xfrin
		 * about the transport type used for that query, so that the
		 * information can be presented in the statistics channel.
		 */
		soa_transport_type = get_request_transport_type(zone);
	}
	sourceaddr = zone->sourceaddr;
	UNLOCK_ZONE(zone);

	INSIST(isc_sockaddr_pf(&primaryaddr) == isc_sockaddr_pf(&sourceaddr));

	zmgr_tlsctx_attach(zone->zmgr, &zmgr_tlsctx_cache);

	dns_xfrin_create(zone, xfrtype, &primaryaddr, &sourceaddr,
			 zone->tsigkey, soa_transport_type, zone->transport,
			 zmgr_tlsctx_cache, zone->mctx, &xfr);
	INSIST(xfr != NULL);

	isc_tlsctx_cache_detach(&zmgr_tlsctx_cache);

	LOCK_ZONE(zone);
	if (zone->xfr != NULL) {
		dns_xfrin_detach(&zone->xfr);
	}
	dns_xfrin_attach(xfr, &zone->xfr);
	UNLOCK_ZONE(zone);

	dns_xfrin_detach(&xfr);

	/*
	 * Any failure in this function is handled like a failed
	 * zone transfer.  This ensures that we get removed from
	 * zmgr->xfrin_in_progress.
	 */
	result = dns_xfrin_start(zone->xfr, zone_xfrdone);
	if (result != ISC_R_SUCCESS) {
		zone_xfrdone(zone, NULL, result);
		return;
	}

	LOCK_ZONE(zone);
	if (xfrtype == dns_rdatatype_axfr) {
		if (isc_sockaddr_pf(&primaryaddr) == PF_INET) {
			inc_stats(zone, dns_zonestatscounter_axfrreqv4);
		} else {
			inc_stats(zone, dns_zonestatscounter_axfrreqv6);
		}
	} else if (xfrtype == dns_rdatatype_ixfr) {
		if (isc_sockaddr_pf(&primaryaddr) == PF_INET) {
			inc_stats(zone, dns_zonestatscounter_ixfrreqv4);
		} else {
			inc_stats(zone, dns_zonestatscounter_ixfrreqv6);
		}
	}
	UNLOCK_ZONE(zone);
}

/*
 * Update forwarding support.
 */

static void
forward_destroy(dns_forward_t *forward) {
	forward->magic = 0;
	if (forward->request != NULL) {
		dns_request_destroy(&forward->request);
	}
	if (forward->msgbuf != NULL) {
		isc_buffer_free(&forward->msgbuf);
	}
	if (forward->transport != NULL) {
		dns_transport_detach(&forward->transport);
	}
	if (forward->zone != NULL) {
		LOCK(&forward->zone->lock);
		if (ISC_LINK_LINKED(forward, link)) {
			ISC_LIST_UNLINK(forward->zone->forwards, forward, link);
		}
		UNLOCK(&forward->zone->lock);
		dns_zone_idetach(&forward->zone);
	}
	isc_mem_putanddetach(&forward->mctx, forward, sizeof(*forward));
}

static isc_result_t
sendtoprimary(dns_forward_t *forward) {
	isc_result_t result;
	isc_sockaddr_t src, any;
	dns_zone_t *zone = forward->zone;
	bool tls_transport_invalid = false;
	isc_tlsctx_cache_t *zmgr_tlsctx_cache = NULL;

	LOCK_ZONE(zone);

	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING)) {
		UNLOCK_ZONE(zone);
		return ISC_R_CANCELED;
	}

next:
	if (forward->which >= dns_remote_count(&forward->zone->primaries)) {
		UNLOCK_ZONE(zone);
		return ISC_R_NOMORE;
	}

	forward->addr = dns_remote_addr(&zone->primaries, forward->which);

	if (isc_sockaddr_disabled(&forward->addr)) {
		forward->which++;
		goto next;
	}

	/*
	 * Always use TCP regardless of whether the original update
	 * used TCP.
	 * XXX The timeout may but a bit small if we are far down a
	 * transfer graph and have to try several primaries.
	 */
	switch (isc_sockaddr_pf(&forward->addr)) {
	case PF_INET:
		isc_sockaddr_any(&any);
		src = zone->primaries.sources[forward->which];
		if (isc_sockaddr_equal(&src, &any)) {
			src = zone->xfrsource4;
		}
		break;
	case PF_INET6:
		isc_sockaddr_any6(&any);
		src = zone->primaries.sources[forward->which];
		if (isc_sockaddr_equal(&src, &any)) {
			src = zone->xfrsource6;
		}
		break;
	default:
		result = ISC_R_NOTIMPLEMENTED;
		goto unlock;
	}

	if (forward->transport != NULL) {
		dns_transport_detach(&forward->transport);
	}

	if (dns_remote_tlsname(&zone->primaries) != NULL &&
	    zone->primaries.tlsnames[forward->which] != NULL)
	{
		dns_view_t *view = dns_zone_getview(zone);
		dns_name_t *tlsname = zone->primaries.tlsnames[forward->which];

		result = dns_view_gettransport(view, DNS_TRANSPORT_TLS, tlsname,
					       &forward->transport);

		if (result != ISC_R_SUCCESS) {
			/* Log the error message when unlocked. */
			tls_transport_invalid = true;
			goto unlock;
		}
	}

	zmgr_tlsctx_attach(zone->zmgr, &zmgr_tlsctx_cache);

	result = dns_request_createraw(
		forward->zone->view->requestmgr, forward->msgbuf, &src,
		&forward->addr, forward->transport, zmgr_tlsctx_cache,
		forward->options, 15 /* XXX */, 0, 0, forward->zone->loop,
		forward_callback, forward, &forward->request);

	isc_tlsctx_cache_detach(&zmgr_tlsctx_cache);

	if (result == ISC_R_SUCCESS) {
		if (!ISC_LINK_LINKED(forward, link)) {
			ISC_LIST_APPEND(zone->forwards, forward, link);
		}
	}

unlock:
	UNLOCK_ZONE(zone);

	if (tls_transport_invalid) {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "could not get TLS configuration "
			     "for dynamic update: %s",
			     isc_result_totext(result));
	}

	return result;
}

static void
forward_callback(void *arg) {
	dns_request_t *request = (dns_request_t *)arg;
	dns_forward_t *forward = dns_request_getarg(request);
	dns_message_t *msg = NULL;
	char primary[ISC_SOCKADDR_FORMATSIZE];
	isc_result_t result;
	dns_zone_t *zone;

	INSIST(DNS_FORWARD_VALID(forward));
	zone = forward->zone;
	INSIST(DNS_ZONE_VALID(zone));

	ENTER;

	isc_sockaddr_format(&forward->addr, primary, sizeof(primary));

	result = dns_request_getresult(request);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_INFO,
			     "could not forward dynamic update to %s: %s",
			     primary, isc_result_totext(result));
		goto next_primary;
	}

	dns_message_create(zone->mctx, NULL, NULL, DNS_MESSAGE_INTENTPARSE,
			   &msg);

	result = dns_request_getresponse(request, msg,
					 DNS_MESSAGEPARSE_PRESERVEORDER |
						 DNS_MESSAGEPARSE_CLONEBUFFER);
	if (result != ISC_R_SUCCESS) {
		goto next_primary;
	}

	/*
	 * Unexpected opcode.
	 */
	if (msg->opcode != dns_opcode_update) {
		char opcode[128];
		isc_buffer_t rb;

		isc_buffer_init(&rb, opcode, sizeof(opcode));
		(void)dns_opcode_totext(msg->opcode, &rb);

		dns_zone_log(zone, ISC_LOG_INFO,
			     "forwarding dynamic update: "
			     "unexpected opcode (%.*s) from %s",
			     (int)rb.used, opcode, primary);
		goto next_primary;
	}

	switch (msg->rcode) {
	/*
	 * Pass these rcodes back to client.
	 */
	case dns_rcode_noerror:
	case dns_rcode_yxdomain:
	case dns_rcode_yxrrset:
	case dns_rcode_nxrrset:
	case dns_rcode_refused:
	case dns_rcode_nxdomain: {
		char rcode[128];
		isc_buffer_t rb;

		isc_buffer_init(&rb, rcode, sizeof(rcode));
		(void)dns_rcode_totext(msg->rcode, &rb);
		dns_zone_log(zone, ISC_LOG_INFO,
			     "forwarded dynamic update: "
			     "primary %s returned: %.*s",
			     primary, (int)rb.used, rcode);
		break;
	}

	/* These should not occur if the primaries/zone are valid. */
	case dns_rcode_notzone:
	case dns_rcode_notauth: {
		char rcode[128];
		isc_buffer_t rb;

		isc_buffer_init(&rb, rcode, sizeof(rcode));
		(void)dns_rcode_totext(msg->rcode, &rb);
		dns_zone_log(zone, ISC_LOG_WARNING,
			     "forwarding dynamic update: "
			     "unexpected response: primary %s returned: %.*s",
			     primary, (int)rb.used, rcode);
		goto next_primary;
	}

	/* Try another server for these rcodes. */
	case dns_rcode_formerr:
	case dns_rcode_servfail:
	case dns_rcode_notimp:
	case dns_rcode_badvers:
	default:
		goto next_primary;
	}

	/* call callback */
	(forward->callback)(forward->callback_arg, ISC_R_SUCCESS, msg);
	msg = NULL;
	dns_request_destroy(&forward->request);
	forward_destroy(forward);
	return;

next_primary:
	if (msg != NULL) {
		dns_message_detach(&msg);
	}
	forward->which++;
	dns_request_destroy(&forward->request);
	result = sendtoprimary(forward);
	if (result != ISC_R_SUCCESS) {
		/* call callback */
		dns_zone_log(zone, ISC_LOG_DEBUG(3),
			     "exhausted dynamic update forwarder list");
		(forward->callback)(forward->callback_arg, result, NULL);
		forward_destroy(forward);
	}
}

isc_result_t
dns_zone_forwardupdate(dns_zone_t *zone, dns_message_t *msg,
		       dns_updatecallback_t callback, void *callback_arg) {
	dns_forward_t *forward;
	isc_result_t result;
	isc_region_t *mr;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(msg != NULL);
	REQUIRE(callback != NULL);

	forward = isc_mem_get(zone->mctx, sizeof(*forward));
	*forward = (dns_forward_t){ .callback = callback,
				    .callback_arg = callback_arg,
				    .options = DNS_REQUESTOPT_TCP };
	ISC_LINK_INIT(forward, link);
	forward->magic = FORWARD_MAGIC;

	/*
	 * If we have a SIG(0) signed message we need to preserve the
	 * query id as that is included in the SIG(0) computation.
	 */
	if (msg->sig0 != NULL) {
		forward->options |= DNS_REQUESTOPT_FIXEDID;
	}

	mr = dns_message_getrawmessage(msg);
	if (mr == NULL) {
		result = ISC_R_UNEXPECTEDEND;
		goto cleanup;
	}

	isc_buffer_allocate(zone->mctx, &forward->msgbuf, mr->length);
	result = isc_buffer_copyregion(forward->msgbuf, mr);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	isc_mem_attach(zone->mctx, &forward->mctx);
	dns_zone_iattach(zone, &forward->zone);
	result = sendtoprimary(forward);

cleanup:
	if (result != ISC_R_SUCCESS) {
		forward_destroy(forward);
	}
	return result;
}

isc_result_t
dns_zone_next(dns_zone_t *zone, dns_zone_t **next) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(next != NULL && *next == NULL);

	*next = ISC_LIST_NEXT(zone, link);
	if (*next == NULL) {
		return ISC_R_NOMORE;
	} else {
		return ISC_R_SUCCESS;
	}
}

isc_result_t
dns_zone_first(dns_zonemgr_t *zmgr, dns_zone_t **first) {
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));
	REQUIRE(first != NULL && *first == NULL);

	*first = ISC_LIST_HEAD(zmgr->zones);
	if (*first == NULL) {
		return ISC_R_NOMORE;
	} else {
		return ISC_R_SUCCESS;
	}
}

/***
 ***	Zone manager.
 ***/

static void
zonemgr_keymgmt_init(dns_zonemgr_t *zmgr) {
	dns_keymgmt_t *mgmt = isc_mem_get(zmgr->mctx, sizeof(*mgmt));

	*mgmt = (dns_keymgmt_t){
		.magic = KEYMGMT_MAGIC,
	};

	isc_mem_attach(zmgr->mctx, &mgmt->mctx);
	isc_rwlock_init(&mgmt->lock);
	isc_hashmap_create(mgmt->mctx, DNS_KEYMGMT_HASH_BITS, &mgmt->table);

	zmgr->keymgmt = mgmt;
}

static void
zonemgr_keymgmt_destroy(dns_zonemgr_t *zmgr) {
	dns_keymgmt_t *mgmt = zmgr->keymgmt;

	REQUIRE(DNS_KEYMGMT_VALID(mgmt));

	mgmt->magic = 0;

	RWLOCK(&mgmt->lock, isc_rwlocktype_write);
	INSIST(isc_hashmap_count(mgmt->table) == 0);
	RWUNLOCK(&mgmt->lock, isc_rwlocktype_write);
	isc_hashmap_destroy(&mgmt->table);

	isc_rwlock_destroy(&mgmt->lock);
	isc_mem_putanddetach(&mgmt->mctx, mgmt, sizeof(dns_keymgmt_t));
}

static bool
kfio_match(void *node, const void *key) {
	const dns_keyfileio_t *kfio = node;

	return dns_name_equal(kfio->name, key);
}

static void
zonemgr_keymgmt_add(dns_zonemgr_t *zmgr, dns_zone_t *zone,
		    dns_keyfileio_t **added) {
	dns_keymgmt_t *mgmt = zmgr->keymgmt;
	dns_keyfileio_t *kfio = NULL;
	isc_result_t result;
	dns_fixedname_t fname;
	dns_name_t *name;

	REQUIRE(DNS_KEYMGMT_VALID(mgmt));
	REQUIRE(added != NULL && *added == NULL);

	name = dns_fixedname_initname(&fname);
	dns_name_downcase(&zone->origin, name, NULL);

	RWLOCK(&mgmt->lock, isc_rwlocktype_write);

	result = isc_hashmap_find(mgmt->table, dns_name_hash(name), kfio_match,
				  name, (void **)&kfio);
	switch (result) {
	case ISC_R_SUCCESS:
		isc_refcount_increment(&kfio->references);
		break;
	case ISC_R_NOTFOUND:
		kfio = isc_mem_get(mgmt->mctx, sizeof(*kfio));
		*kfio = (dns_keyfileio_t){
			.magic = KEYFILEIO_MAGIC,
		};
		isc_refcount_init(&kfio->references, 1);
		kfio->name = dns_fixedname_initname(&kfio->fname);
		dns_name_copy(name, kfio->name);

		isc_mutex_init(&kfio->lock);
		result = isc_hashmap_add(mgmt->table, dns_name_hash(kfio->name),
					 kfio_match, kfio->name, kfio, NULL);
		INSIST(result == ISC_R_SUCCESS);
		break;
	default:
		UNREACHABLE();
	}
	*added = kfio;
	RWUNLOCK(&mgmt->lock, isc_rwlocktype_write);
}

static bool
match_ptr(void *node, const void *key) {
	return node == key;
}

static void
zonemgr_keymgmt_delete(dns_zonemgr_t *zmgr, dns_keyfileio_t **deleted) {
	REQUIRE(DNS_KEYMGMT_VALID(zmgr->keymgmt));
	REQUIRE(deleted != NULL && DNS_KEYFILEIO_VALID(*deleted));

	dns_keymgmt_t *mgmt = zmgr->keymgmt;
	dns_keyfileio_t *kfio = *deleted;
	isc_result_t result;

	*deleted = NULL;

	RWLOCK(&mgmt->lock, isc_rwlocktype_write);

	if (isc_refcount_decrement(&kfio->references) == 1) {
		isc_refcount_destroy(&kfio->references);
		kfio->magic = 0;
		isc_mutex_destroy(&kfio->lock);

		result = isc_hashmap_delete(mgmt->table,
					    dns_name_hash(kfio->name),
					    match_ptr, kfio);
		INSIST(result == ISC_R_SUCCESS);

		isc_mem_put(mgmt->mctx, kfio, sizeof(*kfio));
	}

	RWUNLOCK(&mgmt->lock, isc_rwlocktype_write);
}

void
dns_zonemgr_create(isc_mem_t *mctx, isc_nm_t *netmgr, dns_zonemgr_t **zmgrp) {
	dns_zonemgr_t *zmgr = NULL;
	isc_loop_t *loop = isc_loop();
	isc_loopmgr_t *loopmgr = isc_loop_getloopmgr(loop);

	REQUIRE(mctx != NULL);
	REQUIRE(netmgr != NULL);
	REQUIRE(zmgrp != NULL && *zmgrp == NULL);

	zmgr = isc_mem_get(mctx, sizeof(*zmgr));

	*zmgr = (dns_zonemgr_t){
		.loopmgr = loopmgr,
		.netmgr = netmgr,
		.workers = isc_loopmgr_nloops(loopmgr),
		.transfersin = 10,
		.transfersperns = 2,
	};

	isc_refcount_init(&zmgr->refs, 1);
	isc_mem_attach(mctx, &zmgr->mctx);

	ISC_LIST_INIT(zmgr->zones);
	ISC_LIST_INIT(zmgr->waiting_for_xfrin);
	ISC_LIST_INIT(zmgr->xfrin_in_progress);
	memset(zmgr->unreachable, 0, sizeof(zmgr->unreachable));
	for (size_t i = 0; i < UNREACH_CACHE_SIZE; i++) {
		atomic_init(&zmgr->unreachable[i].expire, 0);
	}
	isc_rwlock_init(&zmgr->rwlock);

	/* Unreachable lock. */
	isc_rwlock_init(&zmgr->urlock);

	isc_ratelimiter_create(loop, &zmgr->checkdsrl);
	isc_ratelimiter_create(loop, &zmgr->notifyrl);
	isc_ratelimiter_create(loop, &zmgr->refreshrl);
	isc_ratelimiter_create(loop, &zmgr->startupnotifyrl);
	isc_ratelimiter_create(loop, &zmgr->startuprefreshrl);

	zmgr->mctxpool = isc_mem_cget(zmgr->mctx, zmgr->workers,
				      sizeof(zmgr->mctxpool[0]));
	for (size_t i = 0; i < zmgr->workers; i++) {
		isc_mem_create(&zmgr->mctxpool[i]);
		isc_mem_setname(zmgr->mctxpool[i], "zonemgr-mctxpool");
	}

	/* Key file I/O locks. */
	zonemgr_keymgmt_init(zmgr);

	/* Default to 20 refresh queries / notifies / checkds per second. */
	setrl(zmgr->checkdsrl, &zmgr->checkdsrate, 20);
	setrl(zmgr->notifyrl, &zmgr->notifyrate, 20);
	setrl(zmgr->startupnotifyrl, &zmgr->startupnotifyrate, 20);
	setrl(zmgr->refreshrl, &zmgr->serialqueryrate, 20);
	setrl(zmgr->startuprefreshrl, &zmgr->startupserialqueryrate, 20);
	isc_ratelimiter_setpushpop(zmgr->startupnotifyrl, true);
	isc_ratelimiter_setpushpop(zmgr->startuprefreshrl, true);

	zmgr->tlsctx_cache = NULL;
	isc_rwlock_init(&zmgr->tlsctx_cache_rwlock);

	zmgr->magic = ZONEMGR_MAGIC;

	*zmgrp = zmgr;
}

isc_result_t
dns_zonemgr_createzone(dns_zonemgr_t *zmgr, dns_zone_t **zonep) {
	isc_mem_t *mctx = NULL;
	dns_zone_t *zone = NULL;
	unsigned int tid;

	REQUIRE(DNS_ZONEMGR_VALID(zmgr));
	REQUIRE(zonep != NULL && *zonep == NULL);

	if (zmgr->mctxpool == NULL) {
		return ISC_R_FAILURE;
	}

	tid = isc_random_uniform(zmgr->workers);

	mctx = zmgr->mctxpool[tid];
	if (mctx == NULL) {
		return ISC_R_FAILURE;
	}

	dns_zone_create(&zone, mctx, tid);

	*zonep = zone;

	return ISC_R_SUCCESS;
}

isc_result_t
dns_zonemgr_managezone(dns_zonemgr_t *zmgr, dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	RWLOCK(&zmgr->rwlock, isc_rwlocktype_write);
	LOCK_ZONE(zone);
	REQUIRE(zone->timer == NULL);
	REQUIRE(zone->zmgr == NULL);

	isc_loop_t *loop = isc_loop_get(zmgr->loopmgr, zone->tid);
	isc_loop_attach(loop, &zone->loop);

	zonemgr_keymgmt_add(zmgr, zone, &zone->kfio);
	INSIST(zone->kfio != NULL);

	ISC_LIST_APPEND(zmgr->zones, zone, link);
	zone->zmgr = zmgr;

	isc_refcount_increment(&zmgr->refs);

	UNLOCK_ZONE(zone);
	RWUNLOCK(&zmgr->rwlock, isc_rwlocktype_write);
	return ISC_R_SUCCESS;
}

void
dns_zonemgr_releasezone(dns_zonemgr_t *zmgr, dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));
	REQUIRE(zone->zmgr == zmgr);

	RWLOCK(&zmgr->rwlock, isc_rwlocktype_write);
	LOCK_ZONE(zone);

	ISC_LIST_UNLINK(zmgr->zones, zone, link);

	if (zone->kfio != NULL) {
		zonemgr_keymgmt_delete(zmgr, &zone->kfio);
		ENSURE(zone->kfio == NULL);
	}

	if (zone->timer != NULL) {
		isc_refcount_decrement(&zone->irefs);
		isc_timer_destroy(&zone->timer);
	}

	isc_loop_detach(&zone->loop);

	/* Detach below, outside of the write lock. */
	zone->zmgr = NULL;

	UNLOCK_ZONE(zone);
	RWUNLOCK(&zmgr->rwlock, isc_rwlocktype_write);

	dns_zonemgr_detach(&zmgr);
}

void
dns_zonemgr_attach(dns_zonemgr_t *source, dns_zonemgr_t **target) {
	REQUIRE(DNS_ZONEMGR_VALID(source));
	REQUIRE(target != NULL && *target == NULL);

	isc_refcount_increment(&source->refs);

	*target = source;
}

void
dns_zonemgr_detach(dns_zonemgr_t **zmgrp) {
	dns_zonemgr_t *zmgr;

	REQUIRE(zmgrp != NULL);
	zmgr = *zmgrp;
	*zmgrp = NULL;
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	if (isc_refcount_decrement(&zmgr->refs) == 1) {
		zonemgr_free(zmgr);
	}
}

isc_result_t
dns_zonemgr_forcemaint(dns_zonemgr_t *zmgr) {
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	RWLOCK(&zmgr->rwlock, isc_rwlocktype_read);
	for (dns_zone_t *zone = ISC_LIST_HEAD(zmgr->zones); zone != NULL;
	     zone = ISC_LIST_NEXT(zone, link))
	{
		isc_time_t now;

		LOCK_ZONE(zone);
		now = isc_time_now();
		zone_settimer(zone, &now);
		UNLOCK_ZONE(zone);
	}
	RWUNLOCK(&zmgr->rwlock, isc_rwlocktype_read);

	/*
	 * Recent configuration changes may have increased the
	 * amount of available transfers quota.  Make sure any
	 * transfers currently blocked on quota get started if
	 * possible.
	 */
	RWLOCK(&zmgr->rwlock, isc_rwlocktype_write);
	zmgr_resume_xfrs(zmgr, true);
	RWUNLOCK(&zmgr->rwlock, isc_rwlocktype_write);
	return ISC_R_SUCCESS;
}

void
dns_zonemgr_resumexfrs(dns_zonemgr_t *zmgr) {
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	RWLOCK(&zmgr->rwlock, isc_rwlocktype_write);
	zmgr_resume_xfrs(zmgr, true);
	RWUNLOCK(&zmgr->rwlock, isc_rwlocktype_write);
}

void
dns_zonemgr_shutdown(dns_zonemgr_t *zmgr) {
	dns_zone_t *zone;

	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	isc_ratelimiter_shutdown(zmgr->checkdsrl);
	isc_ratelimiter_shutdown(zmgr->notifyrl);
	isc_ratelimiter_shutdown(zmgr->refreshrl);
	isc_ratelimiter_shutdown(zmgr->startupnotifyrl);
	isc_ratelimiter_shutdown(zmgr->startuprefreshrl);

	for (size_t i = 0; i < zmgr->workers; i++) {
		isc_mem_detach(&zmgr->mctxpool[i]);
	}

	RWLOCK(&zmgr->rwlock, isc_rwlocktype_read);
	for (zone = ISC_LIST_HEAD(zmgr->zones); zone != NULL;
	     zone = ISC_LIST_NEXT(zone, link))
	{
		LOCK_ZONE(zone);
		forward_cancel(zone);
		UNLOCK_ZONE(zone);
	}
	RWUNLOCK(&zmgr->rwlock, isc_rwlocktype_read);
}

static void
zonemgr_free(dns_zonemgr_t *zmgr) {
	REQUIRE(ISC_LIST_EMPTY(zmgr->zones));

	zmgr->magic = 0;

	isc_refcount_destroy(&zmgr->refs);
	isc_ratelimiter_detach(&zmgr->checkdsrl);
	isc_ratelimiter_detach(&zmgr->notifyrl);
	isc_ratelimiter_detach(&zmgr->refreshrl);
	isc_ratelimiter_detach(&zmgr->startupnotifyrl);
	isc_ratelimiter_detach(&zmgr->startuprefreshrl);

	isc_mem_cput(zmgr->mctx, zmgr->mctxpool, zmgr->workers,
		     sizeof(zmgr->mctxpool[0]));

	isc_rwlock_destroy(&zmgr->urlock);
	isc_rwlock_destroy(&zmgr->rwlock);
	isc_rwlock_destroy(&zmgr->tlsctx_cache_rwlock);

	zonemgr_keymgmt_destroy(zmgr);

	if (zmgr->tlsctx_cache != NULL) {
		isc_tlsctx_cache_detach(&zmgr->tlsctx_cache);
	}
	isc_mem_putanddetach(&zmgr->mctx, zmgr, sizeof(*zmgr));
}

void
dns_zonemgr_settransfersin(dns_zonemgr_t *zmgr, uint32_t value) {
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	zmgr->transfersin = value;
}

uint32_t
dns_zonemgr_gettransfersin(dns_zonemgr_t *zmgr) {
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	return zmgr->transfersin;
}

void
dns_zonemgr_settransfersperns(dns_zonemgr_t *zmgr, uint32_t value) {
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	zmgr->transfersperns = value;
}

uint32_t
dns_zonemgr_gettransfersperns(dns_zonemgr_t *zmgr) {
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	return zmgr->transfersperns;
}

/*
 * Try to start a new incoming zone transfer to fill a quota
 * slot that was just vacated.
 *
 * Requires:
 *	The zone manager is locked by the caller.
 */
static void
zmgr_resume_xfrs(dns_zonemgr_t *zmgr, bool multi) {
	dns_zone_t *zone;
	dns_zone_t *next;

	for (zone = ISC_LIST_HEAD(zmgr->waiting_for_xfrin); zone != NULL;
	     zone = next)
	{
		isc_result_t result;
		next = ISC_LIST_NEXT(zone, statelink);
		result = zmgr_start_xfrin_ifquota(zmgr, zone);
		if (result == ISC_R_SUCCESS) {
			if (multi) {
				continue;
			}
			/*
			 * We successfully filled the slot.  We're done.
			 */
			break;
		} else if (result == ISC_R_QUOTA) {
			/*
			 * Not enough quota.  This is probably the per-server
			 * quota, because we usually get called when a unit of
			 * global quota has just been freed.  Try the next
			 * zone, it may succeed if it uses another primary.
			 */
			continue;
		} else {
			dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN,
				      ISC_LOG_DEBUG(1),
				      "starting zone transfer: %s",
				      isc_result_totext(result));
			break;
		}
	}
}

/*
 * Try to start an incoming zone transfer for 'zone', quota permitting.
 *
 * Requires:
 *	The zone manager is locked by the caller.
 *
 * Returns:
 *	ISC_R_SUCCESS	There was enough quota and we attempted to
 *			start a transfer.  zone_xfrdone() has been or will
 *			be called.
 *	ISC_R_QUOTA	Not enough quota.
 *	Others		Failure.
 */
static isc_result_t
zmgr_start_xfrin_ifquota(dns_zonemgr_t *zmgr, dns_zone_t *zone) {
	dns_peer_t *peer = NULL;
	isc_netaddr_t primaryip;
	isc_sockaddr_t curraddr;
	uint32_t nxfrsin, nxfrsperns;
	dns_zone_t *x = NULL;
	uint32_t maxtransfersin, maxtransfersperns;

	/*
	 * If we are exiting just pretend we got quota so the zone will
	 * be cleaned up in the zone's loop context.
	 */
	LOCK_ZONE(zone);
	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING)) {
		UNLOCK_ZONE(zone);
		goto gotquota;
	}

	/*
	 * Find any configured information about the server we'd
	 * like to transfer this zone from.
	 */
	curraddr = dns_remote_curraddr(&zone->primaries);
	isc_netaddr_fromsockaddr(&primaryip, &curraddr);
	(void)dns_peerlist_peerbyaddr(zone->view->peers, &primaryip, &peer);
	UNLOCK_ZONE(zone);

	/*
	 * Determine the total maximum number of simultaneous
	 * transfers allowed, and the maximum for this specific
	 * primary.
	 */
	maxtransfersin = zmgr->transfersin;
	maxtransfersperns = zmgr->transfersperns;
	if (peer != NULL) {
		(void)dns_peer_gettransfers(peer, &maxtransfersperns);
	}

	/*
	 * Count the total number of transfers that are in progress,
	 * and the number of transfers in progress from this primary.
	 * We linearly scan a list of all transfers; if this turns
	 * out to be too slow, we could hash on the primary address.
	 */
	nxfrsin = nxfrsperns = 0;
	for (x = ISC_LIST_HEAD(zmgr->xfrin_in_progress); x != NULL;
	     x = ISC_LIST_NEXT(x, statelink))
	{
		isc_netaddr_t xip;
		isc_sockaddr_t xaddr;

		LOCK_ZONE(x);
		xaddr = dns_remote_curraddr(&x->primaries);
		isc_netaddr_fromsockaddr(&xip, &xaddr);
		UNLOCK_ZONE(x);

		nxfrsin++;
		if (isc_netaddr_equal(&xip, &primaryip)) {
			nxfrsperns++;
		}
	}

	/* Enforce quota. */
	if (nxfrsin >= maxtransfersin) {
		return ISC_R_QUOTA;
	}

	if (nxfrsperns >= maxtransfersperns) {
		return ISC_R_QUOTA;
	}

gotquota:
	/*
	 * We have sufficient quota.  Move the zone to the "xfrin_in_progress"
	 * list and start the actual transfer asynchronously.
	 */
	LOCK_ZONE(zone);
	INSIST(zone->statelist == &zmgr->waiting_for_xfrin);
	ISC_LIST_UNLINK(zmgr->waiting_for_xfrin, zone, statelink);
	ISC_LIST_APPEND(zmgr->xfrin_in_progress, zone, statelink);
	zone->statelist = &zmgr->xfrin_in_progress;
	isc_async_run(zone->loop, got_transfer_quota, zone);
	dns_zone_logc(zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_INFO,
		      "Transfer started.");
	UNLOCK_ZONE(zone);

	return ISC_R_SUCCESS;
}

static void
zone_saveunique(dns_zone_t *zone, const char *path, const char *templat) {
	char *buf;
	int buflen;
	isc_result_t result;

	buflen = strlen(path) + strlen(templat) + 2;

	buf = isc_mem_get(zone->mctx, buflen);

	result = isc_file_template(path, templat, buf, buflen);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	result = isc_file_renameunique(path, buf);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	dns_zone_log(zone, ISC_LOG_WARNING,
		     "unable to load from '%s'; "
		     "renaming file to '%s' for failure analysis and "
		     "retransferring.",
		     path, buf);

cleanup:
	isc_mem_put(zone->mctx, buf, buflen);
}

static void
setrl(isc_ratelimiter_t *rl, unsigned int *rate, unsigned int value) {
	isc_interval_t interval;
	uint32_t s, ns;
	uint32_t pertic;

	if (value == 0) {
		value = 1;
	}

	if (value == 1) {
		s = 1;
		ns = 0;
		pertic = 1;
	} else if (value <= 10) {
		s = 0;
		ns = 1000000000 / value;
		pertic = 1;
	} else {
		s = 0;
		ns = (1000000000 / value) * 10;
		pertic = 10;
	}

	isc_interval_set(&interval, s, ns);

	isc_ratelimiter_setinterval(rl, &interval);
	isc_ratelimiter_setpertic(rl, pertic);

	*rate = value;
}

void
dns_zonemgr_setcheckdsrate(dns_zonemgr_t *zmgr, unsigned int value) {
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	setrl(zmgr->checkdsrl, &zmgr->checkdsrate, value);
}

void
dns_zonemgr_setnotifyrate(dns_zonemgr_t *zmgr, unsigned int value) {
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	setrl(zmgr->notifyrl, &zmgr->notifyrate, value);
}

void
dns_zonemgr_setstartupnotifyrate(dns_zonemgr_t *zmgr, unsigned int value) {
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	setrl(zmgr->startupnotifyrl, &zmgr->startupnotifyrate, value);
}

void
dns_zonemgr_setserialqueryrate(dns_zonemgr_t *zmgr, unsigned int value) {
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	setrl(zmgr->refreshrl, &zmgr->serialqueryrate, value);
	/* XXXMPA separate out once we have the code to support this. */
	setrl(zmgr->startuprefreshrl, &zmgr->startupserialqueryrate, value);
}

unsigned int
dns_zonemgr_getnotifyrate(dns_zonemgr_t *zmgr) {
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	return zmgr->notifyrate;
}

unsigned int
dns_zonemgr_getstartupnotifyrate(dns_zonemgr_t *zmgr) {
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	return zmgr->startupnotifyrate;
}

unsigned int
dns_zonemgr_getserialqueryrate(dns_zonemgr_t *zmgr) {
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	return zmgr->serialqueryrate;
}

bool
dns_zonemgr_unreachable(dns_zonemgr_t *zmgr, isc_sockaddr_t *remote,
			isc_sockaddr_t *local, isc_time_t *now) {
	unsigned int i;
	uint32_t seconds = isc_time_seconds(now);
	uint32_t count = 0;

	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	RWLOCK(&zmgr->urlock, isc_rwlocktype_read);
	for (i = 0; i < UNREACH_CACHE_SIZE; i++) {
		if (atomic_load(&zmgr->unreachable[i].expire) >= seconds &&
		    isc_sockaddr_equal(&zmgr->unreachable[i].remote, remote) &&
		    isc_sockaddr_equal(&zmgr->unreachable[i].local, local))
		{
			atomic_store_relaxed(&zmgr->unreachable[i].last,
					     seconds);
			count = zmgr->unreachable[i].count;
			break;
		}
	}
	RWUNLOCK(&zmgr->urlock, isc_rwlocktype_read);
	return i < UNREACH_CACHE_SIZE && count > 1U;
}

void
dns_zonemgr_unreachabledel(dns_zonemgr_t *zmgr, isc_sockaddr_t *remote,
			   isc_sockaddr_t *local) {
	unsigned int i;

	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	RWLOCK(&zmgr->urlock, isc_rwlocktype_read);
	for (i = 0; i < UNREACH_CACHE_SIZE; i++) {
		if (isc_sockaddr_equal(&zmgr->unreachable[i].remote, remote) &&
		    isc_sockaddr_equal(&zmgr->unreachable[i].local, local))
		{
			atomic_store_relaxed(&zmgr->unreachable[i].expire, 0);
			break;
		}
	}
	RWUNLOCK(&zmgr->urlock, isc_rwlocktype_read);
}

void
dns_zonemgr_unreachableadd(dns_zonemgr_t *zmgr, isc_sockaddr_t *remote,
			   isc_sockaddr_t *local, isc_time_t *now) {
	uint32_t seconds = isc_time_seconds(now);
	uint32_t expire = 0, last = seconds;
	unsigned int slot = UNREACH_CACHE_SIZE, oldest = 0;
	bool update_entry = true;
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	RWLOCK(&zmgr->urlock, isc_rwlocktype_write);
	for (unsigned int i = 0; i < UNREACH_CACHE_SIZE; i++) {
		/* Existing entry? */
		if (isc_sockaddr_equal(&zmgr->unreachable[i].remote, remote) &&
		    isc_sockaddr_equal(&zmgr->unreachable[i].local, local))
		{
			update_entry = false;
			slot = i;
			expire = atomic_load_relaxed(
				&zmgr->unreachable[i].expire);
			break;
		}
		/* Pick first empty slot? */
		if (atomic_load_relaxed(&zmgr->unreachable[i].expire) < seconds)
		{
			slot = i;
			break;
		}
		/* The worst case, least recently used slot? */
		if (atomic_load_relaxed(&zmgr->unreachable[i].last) < last) {
			last = atomic_load_relaxed(&zmgr->unreachable[i].last);
			oldest = i;
		}
	}

	/* We haven't found any existing or free slots, use the oldest */
	if (slot == UNREACH_CACHE_SIZE) {
		slot = oldest;
	}

	if (expire < seconds) {
		/* Expired or new entry, reset count to 1 */
		zmgr->unreachable[slot].count = 1;
	} else {
		zmgr->unreachable[slot].count++;
	}
	atomic_store_relaxed(&zmgr->unreachable[slot].expire,
			     seconds + UNREACH_HOLD_TIME);
	atomic_store_relaxed(&zmgr->unreachable[slot].last, seconds);
	if (update_entry) {
		zmgr->unreachable[slot].remote = *remote;
		zmgr->unreachable[slot].local = *local;
	}

	RWUNLOCK(&zmgr->urlock, isc_rwlocktype_write);
}

void
dns_zone_stopxfr(dns_zone_t *zone) {
	dns_xfrin_t *xfr = NULL;

	REQUIRE(DNS_ZONE_VALID(zone));

	RWLOCK(&zone->zmgr->rwlock, isc_rwlocktype_read);
	LOCK_ZONE(zone);
	if (zone->statelist == &zone->zmgr->xfrin_in_progress &&
	    zone->xfr != NULL)
	{
		dns_xfrin_attach(zone->xfr, &xfr);
	}
	UNLOCK_ZONE(zone);
	RWUNLOCK(&zone->zmgr->rwlock, isc_rwlocktype_read);

	if (xfr != NULL) {
		dns_xfrin_shutdown(xfr);
		dns_xfrin_detach(&xfr);
	}
}

void
dns_zone_forcexfr(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	if (zone->type == dns_zone_primary ||
	    (zone->type == dns_zone_redirect &&
	     dns_remote_addresses(&zone->primaries) == NULL))
	{
		return;
	}

	LOCK_ZONE(zone);
	DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_FORCEXFER);
	UNLOCK_ZONE(zone);
	dns_zone_refresh(zone);
}

bool
dns_zone_isforced(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return DNS_ZONE_FLAG(zone, DNS_ZONEFLG_FORCEXFER);
}

isc_result_t
dns_zone_setstatistics(dns_zone_t *zone, bool on) {
	/*
	 * This function is obsoleted.
	 */
	UNUSED(zone);
	UNUSED(on);
	return ISC_R_NOTIMPLEMENTED;
}

uint64_t *
dns_zone_getstatscounters(dns_zone_t *zone) {
	/*
	 * This function is obsoleted.
	 */
	UNUSED(zone);
	return NULL;
}

void
dns_zone_setstats(dns_zone_t *zone, isc_stats_t *stats) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(zone->stats == NULL);

	LOCK_ZONE(zone);
	zone->stats = NULL;
	isc_stats_attach(stats, &zone->stats);
	UNLOCK_ZONE(zone);
}

void
dns_zone_setrequeststats(dns_zone_t *zone, isc_stats_t *stats) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->requeststats_on && stats == NULL) {
		zone->requeststats_on = false;
	} else if (!zone->requeststats_on && stats != NULL) {
		if (zone->requeststats == NULL) {
			isc_stats_attach(stats, &zone->requeststats);
		}
		zone->requeststats_on = true;
	}
	UNLOCK_ZONE(zone);
}

void
dns_zone_setrcvquerystats(dns_zone_t *zone, dns_stats_t *stats) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (zone->requeststats_on && stats != NULL) {
		if (zone->rcvquerystats == NULL) {
			dns_stats_attach(stats, &zone->rcvquerystats);
			zone->requeststats_on = true;
		}
	}
	UNLOCK_ZONE(zone);
}

void
dns_zone_setdnssecsignstats(dns_zone_t *zone, dns_stats_t *stats) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (stats != NULL && zone->dnssecsignstats == NULL) {
		dns_stats_attach(stats, &zone->dnssecsignstats);
	}
	UNLOCK_ZONE(zone);
}

dns_stats_t *
dns_zone_getdnssecsignstats(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->dnssecsignstats;
}

isc_stats_t *
dns_zone_getrequeststats(dns_zone_t *zone) {
	/*
	 * We don't lock zone for efficiency reason.  This is not catastrophic
	 * because requeststats must always be valid when requeststats_on is
	 * true.
	 * Some counters may be incremented while requeststats_on is becoming
	 * false, or some cannot be incremented just after the statistics are
	 * installed, but it shouldn't matter much in practice.
	 */
	if (zone->requeststats_on) {
		return zone->requeststats;
	} else {
		return NULL;
	}
}

/*
 * Return the received query stats bucket
 * see note from dns_zone_getrequeststats()
 */
dns_stats_t *
dns_zone_getrcvquerystats(dns_zone_t *zone) {
	if (zone->requeststats_on) {
		return zone->rcvquerystats;
	} else {
		return NULL;
	}
}

void
dns_zone_dialup(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	zone_debuglog(zone, __func__, 3, "notify = %d, refresh = %d",
		      DNS_ZONE_FLAG(zone, DNS_ZONEFLG_DIALNOTIFY),
		      DNS_ZONE_FLAG(zone, DNS_ZONEFLG_DIALREFRESH));

	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_DIALNOTIFY)) {
		dns_zone_notify(zone, true);
	}
	if (zone->type != dns_zone_primary &&
	    dns_remote_addresses(&zone->primaries) != NULL &&
	    DNS_ZONE_FLAG(zone, DNS_ZONEFLG_DIALREFRESH))
	{
		dns_zone_refresh(zone);
	}
}

void
dns_zone_setdialup(dns_zone_t *zone, dns_dialuptype_t dialup) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	DNS_ZONE_CLRFLAG(zone, DNS_ZONEFLG_DIALNOTIFY |
				       DNS_ZONEFLG_DIALREFRESH |
				       DNS_ZONEFLG_NOREFRESH);
	switch (dialup) {
	case dns_dialuptype_no:
		break;
	case dns_dialuptype_yes:
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_DIALNOTIFY |
					       DNS_ZONEFLG_DIALREFRESH |
					       DNS_ZONEFLG_NOREFRESH);
		break;
	case dns_dialuptype_notify:
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_DIALNOTIFY);
		break;
	case dns_dialuptype_notifypassive:
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_DIALNOTIFY);
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NOREFRESH);
		break;
	case dns_dialuptype_refresh:
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_DIALREFRESH);
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NOREFRESH);
		break;
	case dns_dialuptype_passive:
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NOREFRESH);
		break;
	default:
		UNREACHABLE();
	}
	UNLOCK_ZONE(zone);
}

isc_result_t
dns_zone_setkeydirectory(dns_zone_t *zone, const char *directory) {
	isc_result_t result = ISC_R_SUCCESS;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	result = dns_zone_setstring(zone, &zone->keydirectory, directory);
	UNLOCK_ZONE(zone);

	return result;
}

const char *
dns_zone_getkeydirectory(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->keydirectory;
}

void
dns_zone_setkeystores(dns_zone_t *zone, dns_keystorelist_t *keystores) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	zone->keystores = keystores;
	UNLOCK_ZONE(zone);
}

dns_keystorelist_t *
dns_zone_getkeystores(dns_zone_t *zone) {
	dns_keystorelist_t *ks = NULL;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	if (inline_raw(zone) && zone->secure != NULL) {
		ks = zone->secure->keystores;
	} else {
		ks = zone->keystores;
	}
	UNLOCK_ZONE(zone);

	return ks;
}

unsigned int
dns_zonemgr_getcount(dns_zonemgr_t *zmgr, dns_zonestate_t state) {
	dns_zone_t *zone;
	unsigned int count = 0;

	REQUIRE(DNS_ZONEMGR_VALID(zmgr));

	RWLOCK(&zmgr->rwlock, isc_rwlocktype_read);
	switch (state) {
	case DNS_ZONESTATE_XFERRUNNING:
		for (zone = ISC_LIST_HEAD(zmgr->xfrin_in_progress);
		     zone != NULL; zone = ISC_LIST_NEXT(zone, statelink))
		{
			count++;
		}
		break;
	case DNS_ZONESTATE_XFERDEFERRED:
		for (zone = ISC_LIST_HEAD(zmgr->waiting_for_xfrin);
		     zone != NULL; zone = ISC_LIST_NEXT(zone, statelink))
		{
			count++;
		}
		break;
	case DNS_ZONESTATE_XFERFIRSTREFRESH:
		for (zone = ISC_LIST_HEAD(zmgr->zones); zone != NULL;
		     zone = ISC_LIST_NEXT(zone, link))
		{
			if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_FIRSTREFRESH)) {
				count++;
			}
		}
		break;
	case DNS_ZONESTATE_SOAQUERY:
		for (zone = ISC_LIST_HEAD(zmgr->zones); zone != NULL;
		     zone = ISC_LIST_NEXT(zone, link))
		{
			if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_REFRESH)) {
				count++;
			}
		}
		break;
	case DNS_ZONESTATE_ANY:
		for (zone = ISC_LIST_HEAD(zmgr->zones); zone != NULL;
		     zone = ISC_LIST_NEXT(zone, link))
		{
			dns_view_t *view = zone->view;
			if (view != NULL && strcmp(view->name, "_bind") == 0) {
				continue;
			}
			count++;
		}
		break;
	case DNS_ZONESTATE_AUTOMATIC:
		for (zone = ISC_LIST_HEAD(zmgr->zones); zone != NULL;
		     zone = ISC_LIST_NEXT(zone, link))
		{
			dns_view_t *view = zone->view;
			if (view != NULL && strcmp(view->name, "_bind") == 0) {
				continue;
			}
			if (zone->automatic) {
				count++;
			}
		}
		break;
	default:
		UNREACHABLE();
	}

	RWUNLOCK(&zmgr->rwlock, isc_rwlocktype_read);

	return count;
}

isc_result_t
dns_zone_getxfr(dns_zone_t *zone, dns_xfrin_t **xfrp, bool *is_firstrefresh,
		bool *is_running, bool *is_deferred, bool *is_presoa,
		bool *is_pending, bool *needs_refresh) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(xfrp != NULL && *xfrp == NULL);

	if (zone->zmgr == NULL) {
		return ISC_R_FAILURE;
	}

	/* Reset. */
	*is_firstrefresh = false;
	*is_running = false;
	*is_deferred = false;
	*is_presoa = false;
	*is_pending = false;
	*needs_refresh = false;

	RWLOCK(&zone->zmgr->rwlock, isc_rwlocktype_read);
	LOCK_ZONE(zone);
	*is_firstrefresh = DNS_ZONE_FLAG(zone, DNS_ZONEFLG_FIRSTREFRESH);
	if (zone->xfr != NULL) {
		dns_xfrin_attach(zone->xfr, xfrp);
	}
	if (zone->statelist == &zone->zmgr->xfrin_in_progress) {
		*is_running = true;
		/*
		 * The NEEDREFRESH flag is set only when a notify was received
		 * while the current zone transfer is running.
		 */
		*needs_refresh = DNS_ZONE_FLAG(zone, DNS_ZONEFLG_NEEDREFRESH);
	} else if (zone->statelist == &zone->zmgr->waiting_for_xfrin) {
		*is_deferred = true;
	} else if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_REFRESH)) {
		if (zone->request != NULL) {
			*is_presoa = true;
		} else {
			*is_pending = true;
		}
	} else {
		/*
		 * No operation is ongoing or pending, just check if the zone
		 * needs a refresh by looking at the refresh and expire times.
		 */
		if (!DNS_ZONE_FLAG(zone, DNS_ZONEFLG_DIALREFRESH) &&
		    (zone->type == dns_zone_secondary ||
		     zone->type == dns_zone_mirror ||
		     zone->type == dns_zone_stub))
		{
			isc_time_t now = isc_time_now();
			if (isc_time_compare(&now, &zone->refreshtime) >= 0 ||
			    isc_time_compare(&now, &zone->expiretime) >= 0)
			{
				*needs_refresh = true;
			}
		}
	}
	UNLOCK_ZONE(zone);
	RWUNLOCK(&zone->zmgr->rwlock, isc_rwlocktype_read);

	return ISC_R_SUCCESS;
}

void
dns_zone_lock_keyfiles(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	if (zone->kasp == NULL) {
		/* No need to lock, nothing is writing key files. */
		return;
	}

	REQUIRE(DNS_KEYFILEIO_VALID(zone->kfio));
	isc_mutex_lock(&zone->kfio->lock);
}

void
dns_zone_unlock_keyfiles(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	if (zone->kasp == NULL) {
		/* No need to lock, nothing is writing key files. */
		return;
	}

	REQUIRE(DNS_KEYFILEIO_VALID(zone->kfio));
	isc_mutex_unlock(&zone->kfio->lock);
}

isc_result_t
dns_zone_checknames(dns_zone_t *zone, const dns_name_t *name,
		    dns_rdata_t *rdata) {
	bool ok = true;
	bool fail = false;
	char namebuf[DNS_NAME_FORMATSIZE];
	char namebuf2[DNS_NAME_FORMATSIZE];
	char typebuf[DNS_RDATATYPE_FORMATSIZE];
	int level = ISC_LOG_WARNING;
	dns_name_t bad;

	REQUIRE(DNS_ZONE_VALID(zone));

	if (!DNS_ZONE_OPTION(zone, DNS_ZONEOPT_CHECKNAMES) &&
	    rdata->type != dns_rdatatype_nsec3)
	{
		return ISC_R_SUCCESS;
	}

	if (DNS_ZONE_OPTION(zone, DNS_ZONEOPT_CHECKNAMESFAIL) ||
	    rdata->type == dns_rdatatype_nsec3)
	{
		level = ISC_LOG_ERROR;
		fail = true;
	}

	ok = dns_rdata_checkowner(name, rdata->rdclass, rdata->type, true);
	if (!ok) {
		dns_name_format(name, namebuf, sizeof(namebuf));
		dns_rdatatype_format(rdata->type, typebuf, sizeof(typebuf));
		dns_zone_log(zone, level, "%s/%s: %s", namebuf, typebuf,
			     isc_result_totext(DNS_R_BADOWNERNAME));
		if (fail) {
			return DNS_R_BADOWNERNAME;
		}
	}

	dns_name_init(&bad, NULL);
	ok = dns_rdata_checknames(rdata, name, &bad);
	if (!ok) {
		dns_name_format(name, namebuf, sizeof(namebuf));
		dns_name_format(&bad, namebuf2, sizeof(namebuf2));
		dns_rdatatype_format(rdata->type, typebuf, sizeof(typebuf));
		dns_zone_log(zone, level, "%s/%s: %s: %s ", namebuf, typebuf,
			     namebuf2, isc_result_totext(DNS_R_BADNAME));
		if (fail) {
			return DNS_R_BADNAME;
		}
	}

	return ISC_R_SUCCESS;
}

void
dns_zone_setcheckmx(dns_zone_t *zone, dns_checkmxfunc_t checkmx) {
	REQUIRE(DNS_ZONE_VALID(zone));
	zone->checkmx = checkmx;
}

void
dns_zone_setchecksrv(dns_zone_t *zone, dns_checksrvfunc_t checksrv) {
	REQUIRE(DNS_ZONE_VALID(zone));
	zone->checksrv = checksrv;
}

void
dns_zone_setcheckns(dns_zone_t *zone, dns_checknsfunc_t checkns) {
	REQUIRE(DNS_ZONE_VALID(zone));
	zone->checkns = checkns;
}

void
dns_zone_setisself(dns_zone_t *zone, dns_isselffunc_t isself, void *arg) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	zone->isself = isself;
	zone->isselfarg = arg;
	UNLOCK_ZONE(zone);
}

void
dns_zone_setnotifydefer(dns_zone_t *zone, uint32_t defer) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	zone->notifydefer = defer;
	UNLOCK_ZONE(zone);
}

void
dns_zone_setnotifydelay(dns_zone_t *zone, uint32_t delay) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	zone->notifydelay = delay;
	UNLOCK_ZONE(zone);
}

isc_result_t
dns_zone_signwithkey(dns_zone_t *zone, dns_secalg_t algorithm, uint16_t keyid,
		     bool deleteit) {
	isc_result_t result;
	REQUIRE(DNS_ZONE_VALID(zone));

	dnssec_log(zone, ISC_LOG_NOTICE,
		   "dns_zone_signwithkey(algorithm=%u, keyid=%u)", algorithm,
		   keyid);
	LOCK_ZONE(zone);
	result = zone_signwithkey(zone, algorithm, keyid, deleteit);
	UNLOCK_ZONE(zone);

	return result;
}

/*
 * Called when a dynamic update for an NSEC3PARAM record is received.
 *
 * If set, transform the NSEC3 salt into human-readable form so that it can be
 * logged.  Then call zone_addnsec3chain(), passing NSEC3PARAM RDATA to it.
 */
isc_result_t
dns_zone_addnsec3chain(dns_zone_t *zone, dns_rdata_nsec3param_t *nsec3param) {
	isc_result_t result;
	char salt[255 * 2 + 1];

	REQUIRE(DNS_ZONE_VALID(zone));

	result = dns_nsec3param_salttotext(nsec3param, salt, sizeof(salt));
	RUNTIME_CHECK(result == ISC_R_SUCCESS);
	dnssec_log(zone, ISC_LOG_NOTICE,
		   "dns_zone_addnsec3chain(hash=%u, iterations=%u, salt=%s)",
		   nsec3param->hash, nsec3param->iterations, salt);
	LOCK_ZONE(zone);
	result = zone_addnsec3chain(zone, nsec3param);
	UNLOCK_ZONE(zone);

	return result;
}

void
dns_zone_setnodes(dns_zone_t *zone, uint32_t nodes) {
	REQUIRE(DNS_ZONE_VALID(zone));

	if (nodes == 0) {
		nodes = 1;
	}
	zone->nodes = nodes;
}

void
dns_zone_setsignatures(dns_zone_t *zone, uint32_t signatures) {
	REQUIRE(DNS_ZONE_VALID(zone));

	/*
	 * We treat signatures as a signed value so explicitly
	 * limit its range here.
	 */
	if (signatures > INT32_MAX) {
		signatures = INT32_MAX;
	} else if (signatures == 0) {
		signatures = 1;
	}
	zone->signatures = signatures;
}

uint32_t
dns_zone_getsignatures(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));
	return zone->signatures;
}

void
dns_zone_setprivatetype(dns_zone_t *zone, dns_rdatatype_t type) {
	REQUIRE(DNS_ZONE_VALID(zone));
	zone->privatetype = type;
}

dns_rdatatype_t
dns_zone_getprivatetype(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));
	return zone->privatetype;
}

static isc_result_t
zone_signwithkey(dns_zone_t *zone, dns_secalg_t algorithm, uint16_t keyid,
		 bool deleteit) {
	dns_signing_t *signing;
	dns_signing_t *current;
	isc_result_t result = ISC_R_SUCCESS;
	isc_time_t now;
	dns_db_t *db = NULL;

	signing = isc_mem_get(zone->mctx, sizeof *signing);

	signing->magic = 0;
	signing->db = NULL;
	signing->dbiterator = NULL;
	signing->algorithm = algorithm;
	signing->keyid = keyid;
	signing->deleteit = deleteit;
	signing->done = false;

	now = isc_time_now();

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	if (zone->db != NULL) {
		dns_db_attach(zone->db, &db);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);

	if (db == NULL) {
		result = ISC_R_NOTFOUND;
		goto cleanup;
	}

	dns_db_attach(db, &signing->db);

	for (current = ISC_LIST_HEAD(zone->signing); current != NULL;
	     current = ISC_LIST_NEXT(current, link))
	{
		if (current->db == signing->db &&
		    current->algorithm == signing->algorithm &&
		    current->keyid == signing->keyid)
		{
			if (current->deleteit != signing->deleteit) {
				current->done = true;
			} else {
				goto cleanup;
			}
		}
	}

	result = dns_db_createiterator(signing->db, 0, &signing->dbiterator);

	if (result == ISC_R_SUCCESS) {
		result = dns_dbiterator_first(signing->dbiterator);
	}
	if (result == ISC_R_SUCCESS) {
		dns_dbiterator_pause(signing->dbiterator);
		ISC_LIST_INITANDAPPEND(zone->signing, signing, link);
		signing = NULL;
		if (isc_time_isepoch(&zone->signingtime)) {
			zone->signingtime = now;
			if (zone->loop != NULL) {
				zone_settimer(zone, &now);
			}
		}
	}

cleanup:
	if (signing != NULL) {
		if (signing->db != NULL) {
			dns_db_detach(&signing->db);
		}
		if (signing->dbiterator != NULL) {
			dns_dbiterator_destroy(&signing->dbiterator);
		}
		isc_mem_put(zone->mctx, signing, sizeof *signing);
	}
	if (db != NULL) {
		dns_db_detach(&db);
	}
	return result;
}

/* Called once; *timep should be set to the current time. */
static isc_result_t
next_keyevent(dst_key_t *key, isc_stdtime_t *timep) {
	isc_result_t result;
	isc_stdtime_t now, then = 0, event;
	int i;

	now = *timep;

	for (i = 0; i <= DST_MAX_TIMES; i++) {
		result = dst_key_gettime(key, i, &event);
		if (result == ISC_R_SUCCESS && event > now &&
		    (then == 0 || event < then))
		{
			then = event;
		}
	}

	if (then != 0) {
		*timep = then;
		return ISC_R_SUCCESS;
	}

	return ISC_R_NOTFOUND;
}

static isc_result_t
rr_exists(dns_db_t *db, dns_dbversion_t *ver, dns_name_t *name,
	  const dns_rdata_t *rdata, bool *flag) {
	dns_rdataset_t rdataset;
	dns_dbnode_t *node = NULL;
	isc_result_t result;

	dns_rdataset_init(&rdataset);
	if (rdata->type == dns_rdatatype_nsec3) {
		CHECK(dns_db_findnsec3node(db, name, false, &node));
	} else {
		CHECK(dns_db_findnode(db, name, false, &node));
	}
	result = dns_db_findrdataset(db, node, ver, rdata->type, 0,
				     (isc_stdtime_t)0, &rdataset, NULL);
	if (result == ISC_R_NOTFOUND) {
		*flag = false;
		result = ISC_R_SUCCESS;
		goto failure;
	}

	for (result = dns_rdataset_first(&rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&rdataset))
	{
		dns_rdata_t myrdata = DNS_RDATA_INIT;
		dns_rdataset_current(&rdataset, &myrdata);
		if (!dns_rdata_compare(&myrdata, rdata)) {
			break;
		}
	}
	dns_rdataset_disassociate(&rdataset);
	if (result == ISC_R_SUCCESS) {
		*flag = true;
	} else if (result == ISC_R_NOMORE) {
		*flag = false;
		result = ISC_R_SUCCESS;
	}

failure:
	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}
	return result;
}

/*
 * Add records to signal the state of signing or of key removal.
 */
static isc_result_t
add_signing_records(dns_db_t *db, dns_rdatatype_t privatetype,
		    dns_dbversion_t *ver, dns_diff_t *diff, bool sign_all) {
	dns_difftuple_t *tuple = NULL, *newtuple = NULL, *next = NULL;
	dns_difftuple_t *addtuple = NULL, *deltuple = NULL;
	dns_rdata_dnskey_t dnskey;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	bool flag;
	isc_region_t r;
	isc_result_t result = ISC_R_SUCCESS;
	uint16_t keyid;
	unsigned char buf[5];
	dns_name_t *name = dns_db_origin(db);
	dns_difftuplelist_t add = ISC_LIST_INITIALIZER;
	dns_difftuplelist_t del = ISC_LIST_INITIALIZER;
	dns_difftuplelist_t tuples = ISC_LIST_INITIALIZER;

	/*
	 * Move non DNSKEY and not DNSSEC DNSKEY records to tuples
	 * and sort the remaining DNSKEY records to add and del.
	 */
	for (tuple = ISC_LIST_HEAD(diff->tuples); tuple != NULL;
	     tuple = ISC_LIST_HEAD(diff->tuples))
	{
		if (tuple->rdata.type != dns_rdatatype_dnskey) {
			ISC_LIST_UNLINK(diff->tuples, tuple, link);
			ISC_LIST_APPEND(tuples, tuple, link);
			continue;
		}

		result = dns_rdata_tostruct(&tuple->rdata, &dnskey, NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
		if ((dnskey.flags & DNS_KEYFLAG_OWNERMASK) != DNS_KEYOWNER_ZONE)
		{
			ISC_LIST_UNLINK(diff->tuples, tuple, link);
			ISC_LIST_APPEND(tuples, tuple, link);
			continue;
		}

		ISC_LIST_UNLINK(diff->tuples, tuple, link);
		switch (tuple->op) {
		case DNS_DIFFOP_DEL:
		case DNS_DIFFOP_DELRESIGN:
			ISC_LIST_APPEND(del, tuple, link);
			break;
		case DNS_DIFFOP_ADD:
		case DNS_DIFFOP_ADDRESIGN:
			ISC_LIST_APPEND(add, tuple, link);
			break;
		default:
			UNREACHABLE();
		}
	}

	/*
	 * Put the tuples that don't need more processing back onto
	 * diff->tuples.
	 */
	ISC_LIST_APPENDLIST(diff->tuples, tuples, link);

	/*
	 * Filter out DNSKEY TTL changes and put them back onto diff->tuples.
	 */
	for (deltuple = ISC_LIST_HEAD(del); deltuple != NULL; deltuple = next) {
		next = ISC_LIST_NEXT(deltuple, link);
		for (addtuple = ISC_LIST_HEAD(add); addtuple != NULL;
		     addtuple = ISC_LIST_NEXT(addtuple, link))
		{
			int n = dns_rdata_compare(&deltuple->rdata,
						  &addtuple->rdata);
			if (n == 0) {
				ISC_LIST_UNLINK(del, deltuple, link);
				ISC_LIST_APPEND(diff->tuples, deltuple, link);
				ISC_LIST_UNLINK(add, addtuple, link);
				ISC_LIST_APPEND(diff->tuples, addtuple, link);
				break;
			}
		}
	}

	/*
	 * Combine any remaining DNSKEY changes together.
	 */
	ISC_LIST_APPENDLIST(tuples, add, link);
	ISC_LIST_APPENDLIST(tuples, del, link);

	/*
	 * Add private records for keys that have been removed
	 * or added.
	 */
	for (tuple = ISC_LIST_HEAD(tuples); tuple != NULL;
	     tuple = ISC_LIST_NEXT(tuple, link))
	{
		dns_rdata_toregion(&tuple->rdata, &r);

		keyid = dst_region_computeid(&r);

		buf[0] = dnskey.algorithm;
		buf[1] = (keyid & 0xff00) >> 8;
		buf[2] = (keyid & 0xff);
		buf[3] = (tuple->op == DNS_DIFFOP_ADD) ? 0 : 1;
		buf[4] = 0;
		rdata.data = buf;
		rdata.length = sizeof(buf);
		rdata.type = privatetype;
		rdata.rdclass = tuple->rdata.rdclass;

		if (sign_all || tuple->op == DNS_DIFFOP_DEL) {
			CHECK(rr_exists(db, ver, name, &rdata, &flag));
			if (flag) {
				continue;
			}

			CHECK(dns_difftuple_create(diff->mctx, DNS_DIFFOP_ADD,
						   name, 0, &rdata, &newtuple));
			CHECK(do_one_tuple(&newtuple, db, ver, diff));
			INSIST(newtuple == NULL);
		}

		/*
		 * Remove any record which says this operation has already
		 * completed.
		 */
		buf[4] = 1;
		CHECK(rr_exists(db, ver, name, &rdata, &flag));
		if (flag) {
			CHECK(dns_difftuple_create(diff->mctx, DNS_DIFFOP_DEL,
						   name, 0, &rdata, &newtuple));
			CHECK(do_one_tuple(&newtuple, db, ver, diff));
			INSIST(newtuple == NULL);
		}
	}

failure:
	/*
	 * Put the DNSKEY changes we cared about back on diff->tuples.
	 */
	ISC_LIST_APPENDLIST(diff->tuples, tuples, link);
	INSIST(ISC_LIST_EMPTY(add));
	INSIST(ISC_LIST_EMPTY(del));
	INSIST(ISC_LIST_EMPTY(tuples));
	return result;
}

/*
 * See if dns__zone_updatesigs() will update signature for RRset 'rrtype' at
 * the apex, and if not tickle them and cause to sign so that newly activated
 * keys are used.
 */
static isc_result_t
tickle_apex_rrset(dns_rdatatype_t rrtype, dns_zone_t *zone, dns_db_t *db,
		  dns_dbversion_t *ver, isc_stdtime_t now, dns_diff_t *diff,
		  dns__zonediff_t *zonediff, dst_key_t **keys,
		  unsigned int nkeys, isc_stdtime_t inception,
		  isc_stdtime_t keyexpire) {
	dns_difftuple_t *tuple;
	isc_result_t result;

	for (tuple = ISC_LIST_HEAD(diff->tuples); tuple != NULL;
	     tuple = ISC_LIST_NEXT(tuple, link))
	{
		if (tuple->rdata.type == rrtype &&
		    dns_name_equal(&tuple->name, &zone->origin))
		{
			break;
		}
	}

	if (tuple == NULL) {
		result = del_sigs(zone, db, ver, &zone->origin, rrtype,
				  zonediff, keys, nkeys, now, false);
		if (result != ISC_R_SUCCESS) {
			dnssec_log(zone, ISC_LOG_ERROR,
				   "sign_apex:del_sigs -> %s",
				   isc_result_totext(result));
			return result;
		}
		result = add_sigs(db, ver, &zone->origin, zone, rrtype,
				  zonediff->diff, keys, nkeys, zone->mctx, now,
				  inception, keyexpire);
		if (result != ISC_R_SUCCESS) {
			dnssec_log(zone, ISC_LOG_ERROR,
				   "sign_apex:add_sigs -> %s",
				   isc_result_totext(result));
			return result;
		}
	}

	return ISC_R_SUCCESS;
}

static isc_result_t
sign_apex(dns_zone_t *zone, dns_db_t *db, dns_dbversion_t *ver,
	  isc_stdtime_t now, dns_diff_t *diff, dns__zonediff_t *zonediff) {
	isc_result_t result;
	isc_stdtime_t inception, soaexpire, keyexpire;
	dst_key_t *zone_keys[DNS_MAXZONEKEYS];
	unsigned int nkeys = 0, i;

	result = dns_zone_findkeys(zone, db, ver, now, zone->mctx,
				   DNS_MAXZONEKEYS, zone_keys, &nkeys);
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR,
			   "sign_apex:dns_zone_findkeys -> %s",
			   isc_result_totext(result));
		return result;
	}

	inception = now - 3600; /* Allow for clock skew. */
	soaexpire = now + dns_zone_getsigvalidityinterval(zone);

	keyexpire = dns_zone_getkeyvalidityinterval(zone);
	if (keyexpire == 0) {
		keyexpire = soaexpire - 1;
	} else {
		keyexpire += now;
	}

	/*
	 * See if dns__zone_updatesigs() will update DNSKEY/CDS/CDNSKEY
	 * signature and if not cause them to sign so that newly activated
	 * keys are used.
	 */
	result = tickle_apex_rrset(dns_rdatatype_dnskey, zone, db, ver, now,
				   diff, zonediff, zone_keys, nkeys, inception,
				   keyexpire);
	if (result != ISC_R_SUCCESS) {
		goto failure;
	}
	result = tickle_apex_rrset(dns_rdatatype_cds, zone, db, ver, now, diff,
				   zonediff, zone_keys, nkeys, inception,
				   keyexpire);
	if (result != ISC_R_SUCCESS) {
		goto failure;
	}
	result = tickle_apex_rrset(dns_rdatatype_cdnskey, zone, db, ver, now,
				   diff, zonediff, zone_keys, nkeys, inception,
				   keyexpire);
	if (result != ISC_R_SUCCESS) {
		goto failure;
	}

	result = dns__zone_updatesigs(diff, db, ver, zone_keys, nkeys, zone,
				      inception, soaexpire, keyexpire, now,
				      zonediff);

	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR,
			   "sign_apex:dns__zone_updatesigs -> %s",
			   isc_result_totext(result));
		goto failure;
	}

failure:
	for (i = 0; i < nkeys; i++) {
		dst_key_free(&zone_keys[i]);
	}
	return result;
}

static isc_result_t
clean_nsec3param(dns_zone_t *zone, dns_db_t *db, dns_dbversion_t *ver,
		 dns_diff_t *diff) {
	isc_result_t result;
	dns_dbnode_t *node = NULL;
	dns_rdataset_t rdataset;

	dns_rdataset_init(&rdataset);
	CHECK(dns_db_getoriginnode(db, &node));

	result = dns_db_findrdataset(db, node, ver, dns_rdatatype_dnskey,
				     dns_rdatatype_none, 0, &rdataset, NULL);
	if (dns_rdataset_isassociated(&rdataset)) {
		dns_rdataset_disassociate(&rdataset);
	}
	if (result != ISC_R_NOTFOUND) {
		goto failure;
	}

	result = dns_nsec3param_deletechains(db, ver, zone, true, diff);

failure:
	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}
	return result;
}

/*
 * Given an RRSIG rdataset and an algorithm, determine whether there
 * are any signatures using that algorithm.
 */
static bool
signed_with_alg(dns_rdataset_t *rdataset, dns_secalg_t alg) {
	dns_rdata_t rdata = DNS_RDATA_INIT;
	dns_rdata_rrsig_t rrsig;
	isc_result_t result;

	REQUIRE(rdataset == NULL || rdataset->type == dns_rdatatype_rrsig);
	if (rdataset == NULL || !dns_rdataset_isassociated(rdataset)) {
		return false;
	}

	for (result = dns_rdataset_first(rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(rdataset))
	{
		dns_rdataset_current(rdataset, &rdata);
		result = dns_rdata_tostruct(&rdata, &rrsig, NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
		dns_rdata_reset(&rdata);
		if (rrsig.algorithm == alg) {
			return true;
		}
	}

	return false;
}

static isc_result_t
add_chains(dns_zone_t *zone, dns_db_t *db, dns_dbversion_t *ver,
	   dns_diff_t *diff) {
	dns_name_t *origin;
	bool build_nsec3;
	isc_result_t result;

	origin = dns_db_origin(db);
	CHECK(dns_private_chains(db, ver, zone->privatetype, NULL,
				 &build_nsec3));
	if (build_nsec3) {
		CHECK(dns_nsec3_addnsec3sx(db, ver, origin, zone_nsecttl(zone),
					   false, zone->privatetype, diff));
	}
	CHECK(updatesecure(db, ver, origin, zone_nsecttl(zone), true, diff));

failure:
	return result;
}

static void
dnssec_report(const char *format, ...) {
	va_list args;
	va_start(args, format);
	isc_log_vwrite(dns_lctx, DNS_LOGCATEGORY_DNSSEC, DNS_LOGMODULE_ZONE,
		       ISC_LOG_INFO, format, args);
	va_end(args);
}

static void
checkds_destroy(dns_checkds_t *checkds, bool locked) {
	REQUIRE(DNS_CHECKDS_VALID(checkds));

	dns_zone_log(checkds->zone, ISC_LOG_DEBUG(3),
		     "checkds: destroy DS query");

	if (checkds->zone != NULL) {
		if (!locked) {
			LOCK_ZONE(checkds->zone);
		}
		REQUIRE(LOCKED_ZONE(checkds->zone));
		if (ISC_LINK_LINKED(checkds, link)) {
			ISC_LIST_UNLINK(checkds->zone->checkds_requests,
					checkds, link);
		}
		if (!locked) {
			UNLOCK_ZONE(checkds->zone);
		}
		if (locked) {
			zone_idetach(&checkds->zone);
		} else {
			dns_zone_idetach(&checkds->zone);
		}
	}
	if (checkds->find != NULL) {
		dns_adb_destroyfind(&checkds->find);
	}
	if (checkds->request != NULL) {
		dns_request_destroy(&checkds->request);
	}
	if (dns_name_dynamic(&checkds->ns)) {
		dns_name_free(&checkds->ns, checkds->mctx);
	}
	if (checkds->key != NULL) {
		dns_tsigkey_detach(&checkds->key);
	}
	if (checkds->transport != NULL) {
		dns_transport_detach(&checkds->transport);
	}
	INSIST(checkds->rlevent == NULL);
	isc_mem_putanddetach(&checkds->mctx, checkds, sizeof(*checkds));
}

static isc_result_t
make_dnskey(dst_key_t *key, unsigned char *buf, int bufsize,
	    dns_rdata_t *target) {
	isc_result_t result;
	isc_buffer_t b;
	isc_region_t r;

	isc_buffer_init(&b, buf, bufsize);
	result = dst_key_todns(key, &b);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	dns_rdata_reset(target);
	isc_buffer_usedregion(&b, &r);
	dns_rdata_fromregion(target, dst_key_class(key), dns_rdatatype_dnskey,
			     &r);
	return ISC_R_SUCCESS;
}

static bool
do_checkds(dns_zone_t *zone, dst_key_t *key, isc_stdtime_t now,
	   bool dspublish) {
	dns_kasp_t *kasp = zone->kasp;
	isc_result_t result;
	uint32_t count = 0;
	uint32_t num;

	switch (zone->checkdstype) {
	case dns_checkdstype_yes:
		num = zone->parent_nscount;
		break;
	case dns_checkdstype_explicit:
		num = dns_remote_count(&zone->parentals);
		break;
	case dns_checkdstype_no:
	default:
		dns_zone_log(zone, ISC_LOG_WARNING,
			     "checkds: option is disabled");
		return false;
	}

	if (dspublish) {
		(void)dst_key_getnum(key, DST_NUM_DSPUBCOUNT, &count);
		count += 1;
		dst_key_setnum(key, DST_NUM_DSPUBCOUNT, count);
		dns_zone_log(zone, ISC_LOG_DEBUG(3),
			     "checkds: %u DS published "
			     "for key %u",
			     count, dst_key_id(key));

		if (count != num) {
			return false;
		}
	} else {
		(void)dst_key_getnum(key, DST_NUM_DSDELCOUNT, &count);
		count += 1;
		dst_key_setnum(key, DST_NUM_DSDELCOUNT, count);
		dns_zone_log(zone, ISC_LOG_DEBUG(3),
			     "checkds: %u DS withdrawn "
			     "for key %u",
			     count, dst_key_id(key));

		if (count != num) {
			return false;
		}
	}

	dns_zone_log(zone, ISC_LOG_DEBUG(3),
		     "checkds: checkds %s for key "
		     "%u",
		     dspublish ? "published" : "withdrawn", dst_key_id(key));

	dns_zone_lock_keyfiles(zone);
	result = dns_keymgr_checkds_id(kasp, &zone->checkds_ok, now, now,
				       dspublish, dst_key_id(key),
				       dst_key_alg(key));
	dns_zone_unlock_keyfiles(zone);

	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_WARNING,
			     "checkds: checkds for key %u failed: %s",
			     dst_key_id(key), isc_result_totext(result));
		return false;
	}

	return true;
}

static isc_result_t
validate_ds(dns_zone_t *zone, dns_message_t *message) {
	UNUSED(zone);
	UNUSED(message);

	/* Get closest trust anchor */

	/* Check that trust anchor is (grand)parent of zone. */

	/* Find the DNSKEY signing the message. */

	/* Check that DNSKEY is in chain of trust. */

	/* Validate DS RRset. */

	return ISC_R_SUCCESS;
}

static void
checkds_done(void *arg) {
	dns_request_t *request = (dns_request_t *)arg;
	dns_checkds_t *checkds = dns_request_getarg(request);
	char addrbuf[ISC_SOCKADDR_FORMATSIZE];
	char rcode[128];
	dns_zone_t *zone = NULL;
	dns_db_t *db = NULL;
	dns_dbversion_t *version = NULL;
	dns_dnsseckey_t *key = NULL;
	dns_dnsseckeylist_t keys;
	dns_kasp_t *kasp = NULL;
	dns_message_t *message = NULL;
	dns_rdataset_t *ds_rrset = NULL;
	isc_buffer_t buf;
	isc_result_t result;
	isc_stdtime_t now;
	isc_time_t timenow;
	bool rekey = false;
	bool empty = false;

	REQUIRE(DNS_CHECKDS_VALID(checkds));

	zone = checkds->zone;

	ISC_LIST_INIT(keys);

	kasp = zone->kasp;
	INSIST(kasp != NULL);

	isc_buffer_init(&buf, rcode, sizeof(rcode));
	isc_sockaddr_format(&checkds->dst, addrbuf, sizeof(addrbuf));

	dns_zone_log(zone, ISC_LOG_DEBUG(1), "checkds: DS query to %s: done",
		     addrbuf);

	dns_message_create(zone->mctx, NULL, NULL, DNS_MESSAGE_INTENTPARSE,
			   &message);
	INSIST(message != NULL);

	CHECK(dns_request_getresult(request));
	CHECK(dns_request_getresponse(request, message,
				      DNS_MESSAGEPARSE_PRESERVEORDER));
	CHECK(dns_rcode_totext(message->rcode, &buf));

	dns_zone_log(zone, ISC_LOG_DEBUG(3),
		     "checkds: DS response from %s: %.*s", addrbuf,
		     (int)buf.used, rcode);

	/* Validate response. */
	CHECK(validate_ds(zone, message));

	/* Check RCODE. */
	if (message->rcode != dns_rcode_noerror) {
		dns_zone_log(zone, ISC_LOG_NOTICE,
			     "checkds: bad DS response from %s: %.*s", addrbuf,
			     (int)buf.used, rcode);
		goto failure;
	}

	/* Make sure that either AA or RA bit is set. */
	if ((message->flags & DNS_MESSAGEFLAG_AA) == 0 &&
	    (message->flags & DNS_MESSAGEFLAG_RA) == 0)
	{
		dns_zone_log(zone, ISC_LOG_NOTICE,
			     "checkds: bad DS response from %s: expected AA or "
			     "RA bit set",
			     addrbuf);
		goto failure;
	}

	/* Lookup DS RRset. */
	result = dns_message_firstname(message, DNS_SECTION_ANSWER);
	while (result == ISC_R_SUCCESS) {
		dns_name_t *name = NULL;
		dns_rdataset_t *rdataset;

		dns_message_currentname(message, DNS_SECTION_ANSWER, &name);
		if (dns_name_compare(&zone->origin, name) != 0) {
			goto next;
		}

		for (rdataset = ISC_LIST_HEAD(name->list); rdataset != NULL;
		     rdataset = ISC_LIST_NEXT(rdataset, link))
		{
			if (rdataset->type != dns_rdatatype_ds) {
				goto next;
			}

			ds_rrset = rdataset;
			break;
		}

		if (ds_rrset != NULL) {
			break;
		}

	next:
		result = dns_message_nextname(message, DNS_SECTION_ANSWER);
	}

	if (ds_rrset == NULL) {
		empty = true;
		dns_zone_log(zone, ISC_LOG_NOTICE,
			     "checkds: empty DS response from %s", addrbuf);
	}

	timenow = isc_time_now();
	now = isc_time_seconds(&timenow);

	CHECK(dns_zone_getdb(zone, &db));
	dns_db_currentversion(db, &version);

	KASP_LOCK(kasp);
	LOCK_ZONE(zone);
	for (key = ISC_LIST_HEAD(zone->checkds_ok); key != NULL;
	     key = ISC_LIST_NEXT(key, link))
	{
		bool alldone = false, found = false;
		bool checkdspub = false, checkdsdel = false, ksk = false;
		dst_key_state_t ds_state = DST_KEY_STATE_NA;
		isc_stdtime_t published = 0, withdrawn = 0;
		isc_result_t ret = ISC_R_SUCCESS;

		/* Is this key have the KSK role? */
		(void)dst_key_role(key->key, &ksk, NULL);
		if (!ksk) {
			continue;
		}

		/* Do we need to check the DS RRset for this key? */
		(void)dst_key_getstate(key->key, DST_KEY_DS, &ds_state);
		(void)dst_key_gettime(key->key, DST_TIME_DSPUBLISH, &published);
		(void)dst_key_gettime(key->key, DST_TIME_DSDELETE, &withdrawn);

		if (ds_state == DST_KEY_STATE_RUMOURED && published == 0) {
			checkdspub = true;
		} else if (ds_state == DST_KEY_STATE_UNRETENTIVE &&
			   withdrawn == 0)
		{
			checkdsdel = true;
		}
		if (!checkdspub && !checkdsdel) {
			continue;
		}

		if (empty) {
			goto dswithdrawn;
		}

		/* Find the appropriate DS record. */
		ret = dns_rdataset_first(ds_rrset);
		while (ret == ISC_R_SUCCESS) {
			dns_rdata_ds_t ds;
			dns_rdata_t dnskey = DNS_RDATA_INIT;
			dns_rdata_t dsrdata = DNS_RDATA_INIT;
			dns_rdata_t rdata = DNS_RDATA_INIT;
			isc_result_t r;
			unsigned char dsbuf[DNS_DS_BUFFERSIZE];
			unsigned char keybuf[DST_KEY_MAXSIZE];

			dns_rdataset_current(ds_rrset, &rdata);
			r = dns_rdata_tostruct(&rdata, &ds, NULL);
			if (r != ISC_R_SUCCESS) {
				goto nextds;
			}
			/* Check key tag and algorithm. */
			if (dst_key_id(key->key) != ds.key_tag) {
				goto nextds;
			}
			if (dst_key_alg(key->key) != ds.algorithm) {
				goto nextds;
			}
			/* Derive DS from DNSKEY, see if the rdata is equal. */
			make_dnskey(key->key, keybuf, sizeof(keybuf), &dnskey);
			r = dns_ds_buildrdata(&zone->origin, &dnskey,
					      ds.digest_type, dsbuf, &dsrdata);
			if (r != ISC_R_SUCCESS) {
				goto nextds;
			}
			if (dns_rdata_compare(&rdata, &dsrdata) == 0) {
				found = true;
				if (checkdspub) {
					/* DS Published. */
					alldone = do_checkds(zone, key->key,
							     now, true);
					if (alldone) {
						rekey = true;
					}
				}
			}

		nextds:
			ret = dns_rdataset_next(ds_rrset);
		}

	dswithdrawn:
		/* DS withdrawn. */
		if (checkdsdel && !found) {
			alldone = do_checkds(zone, key->key, now, false);
			if (alldone) {
				rekey = true;
			}
		}
	}
	UNLOCK_ZONE(zone);
	KASP_UNLOCK(kasp);

	/* Rekey after checkds. */
	if (rekey) {
		dns_zone_rekey(zone, false);
	}

failure:
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_DEBUG(3),
			     "checkds: DS request failed: %s",
			     isc_result_totext(result));
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

	checkds_destroy(checkds, false);
	dns_message_detach(&message);
}

static bool
checkds_isqueued(dns_zone_t *zone, dns_name_t *name, isc_sockaddr_t *addr,
		 dns_tsigkey_t *key, dns_transport_t *transport) {
	dns_checkds_t *checkds;

	for (checkds = ISC_LIST_HEAD(zone->checkds_requests); checkds != NULL;
	     checkds = ISC_LIST_NEXT(checkds, link))
	{
		if (checkds->request != NULL) {
			continue;
		}
		if (name != NULL && dns_name_equal(name, &checkds->ns)) {
			return true;
		}
		if (addr != NULL && isc_sockaddr_equal(addr, &checkds->dst) &&
		    checkds->key == key && checkds->transport == transport)
		{
			return true;
		}
	}
	return false;
}

static isc_result_t
checkds_create(isc_mem_t *mctx, unsigned int flags, dns_checkds_t **checkdsp) {
	dns_checkds_t *checkds;

	REQUIRE(checkdsp != NULL && *checkdsp == NULL);

	checkds = isc_mem_get(mctx, sizeof(*checkds));
	*checkds = (dns_checkds_t){
		.flags = flags,
	};

	isc_mem_attach(mctx, &checkds->mctx);
	isc_sockaddr_any(&checkds->dst);
	dns_name_init(&checkds->ns, NULL);
	ISC_LINK_INIT(checkds, link);
	checkds->magic = CHECKDS_MAGIC;
	*checkdsp = checkds;
	return ISC_R_SUCCESS;
}

static void
checkds_createmessage(dns_zone_t *zone, dns_message_t **messagep) {
	dns_message_t *message = NULL;

	dns_name_t *tempname = NULL;
	dns_rdataset_t *temprdataset = NULL;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(messagep != NULL && *messagep == NULL);

	dns_message_create(zone->mctx, NULL, NULL, DNS_MESSAGE_INTENTRENDER,
			   &message);

	message->opcode = dns_opcode_query;
	message->rdclass = zone->rdclass;
	message->flags |= DNS_MESSAGEFLAG_RD;

	dns_message_gettempname(message, &tempname);

	dns_message_gettemprdataset(message, &temprdataset);

	/*
	 * Make question.
	 */
	dns_name_init(tempname, NULL);
	dns_name_clone(&zone->origin, tempname);
	dns_rdataset_makequestion(temprdataset, zone->rdclass,
				  dns_rdatatype_ds);
	ISC_LIST_APPEND(tempname->list, temprdataset, link);
	dns_message_addname(message, tempname, DNS_SECTION_QUESTION);
	tempname = NULL;
	temprdataset = NULL;

	*messagep = message;
}

/*
 * XXXAG should check for DNS_ZONEFLG_EXITING
 */
static void
process_checkds_adb_event(void *arg) {
	dns_adbfind_t *find = (dns_adbfind_t *)arg;
	dns_checkds_t *checkds = (dns_checkds_t *)find->cbarg;
	dns_adbstatus_t astat = find->status;

	REQUIRE(DNS_CHECKDS_VALID(checkds));
	REQUIRE(find == checkds->find);

	switch (astat) {
	case DNS_ADB_MOREADDRESSES:
		dns_adb_destroyfind(&checkds->find);
		checkds_find_address(checkds);
		return;

	case DNS_ADB_NOMOREADDRESSES:
		LOCK_ZONE(checkds->zone);
		checkds_send_tons(checkds);
		UNLOCK_ZONE(checkds->zone);
		break;

	default:
		break;
	}

	checkds_destroy(checkds, false);
}

static void
checkds_find_address(dns_checkds_t *checkds) {
	isc_result_t result;
	unsigned int options;
	dns_adb_t *adb = NULL;

	REQUIRE(DNS_CHECKDS_VALID(checkds));

	options = DNS_ADBFIND_WANTEVENT;
	if (isc_net_probeipv4() != ISC_R_DISABLED) {
		options |= DNS_ADBFIND_INET;
	}
	if (isc_net_probeipv6() != ISC_R_DISABLED) {
		options |= DNS_ADBFIND_INET6;
	}

	dns_view_getadb(checkds->zone->view, &adb);
	if (adb == NULL) {
		goto destroy;
	}

	result = dns_adb_createfind(
		adb, checkds->zone->loop, process_checkds_adb_event, checkds,
		&checkds->ns, dns_rootname, 0, options, 0, NULL,
		checkds->zone->view->dstport, 0, NULL, NULL, &checkds->find);
	dns_adb_detach(&adb);

	/* Something failed? */
	if (result != ISC_R_SUCCESS) {
		goto destroy;
	}

	/* More addresses pending? */
	if ((checkds->find->options & DNS_ADBFIND_WANTEVENT) != 0) {
		return;
	}

	/* We have as many addresses as we can get. */
	LOCK_ZONE(checkds->zone);
	checkds_send_tons(checkds);
	UNLOCK_ZONE(checkds->zone);

destroy:
	checkds_destroy(checkds, false);
}

static void
checkds_send_toaddr(void *arg) {
	dns_checkds_t *checkds = (dns_checkds_t *)arg;
	isc_result_t result;
	dns_message_t *message = NULL;
	isc_netaddr_t dstip;
	dns_tsigkey_t *key = NULL;
	char addrbuf[ISC_SOCKADDR_FORMATSIZE];
	isc_sockaddr_t src;
	unsigned int options, timeout;
	bool have_checkdssource = false;
	bool canceled = checkds->rlevent->canceled;

	REQUIRE(DNS_CHECKDS_VALID(checkds));

	isc_rlevent_free(&checkds->rlevent);

	LOCK_ZONE(checkds->zone);

	if (DNS_ZONE_FLAG(checkds->zone, DNS_ZONEFLG_LOADED) == 0 || canceled ||
	    DNS_ZONE_FLAG(checkds->zone, DNS_ZONEFLG_EXITING) ||
	    checkds->zone->view->requestmgr == NULL ||
	    checkds->zone->db == NULL)
	{
		result = ISC_R_CANCELED;
		goto cleanup;
	}

	/*
	 * The raw IPv4 address should also exist.  Don't send to the
	 * mapped form.
	 */
	if (isc_sockaddr_pf(&checkds->dst) == PF_INET6 &&
	    IN6_IS_ADDR_V4MAPPED(&checkds->dst.type.sin6.sin6_addr))
	{
		isc_sockaddr_format(&checkds->dst, addrbuf, sizeof(addrbuf));
		dns_zone_log(checkds->zone, ISC_LOG_DEBUG(3),
			     "checkds: ignoring IPv6 mapped IPV4 address: %s",
			     addrbuf);
		result = ISC_R_CANCELED;
		goto cleanup;
	}

	checkds_createmessage(checkds->zone, &message);

	isc_sockaddr_format(&checkds->dst, addrbuf, sizeof(addrbuf));
	if (checkds->key != NULL) {
		/* Transfer ownership of key */
		key = checkds->key;
		checkds->key = NULL;
	} else {
		isc_netaddr_fromsockaddr(&dstip, &checkds->dst);
		result = dns_view_getpeertsig(checkds->zone->view, &dstip,
					      &key);
		if (result != ISC_R_SUCCESS && result != ISC_R_NOTFOUND) {
			dns_zone_log(checkds->zone, ISC_LOG_ERROR,
				     "checkds: DS query to %s not sent. "
				     "Peer TSIG key lookup failure.",
				     addrbuf);
			goto cleanup_message;
		}
	}

	if (key != NULL) {
		char namebuf[DNS_NAME_FORMATSIZE];

		dns_name_format(key->name, namebuf, sizeof(namebuf));
		dns_zone_log(checkds->zone, ISC_LOG_DEBUG(3),
			     "checkds: sending DS query to %s : TSIG (%s)",
			     addrbuf, namebuf);
	} else {
		dns_zone_log(checkds->zone, ISC_LOG_DEBUG(3),
			     "checkds: sending DS query to %s", addrbuf);
	}
	options = 0;
	if (checkds->zone->view->peers != NULL) {
		dns_peer_t *peer = NULL;
		bool usetcp = false;
		result = dns_peerlist_peerbyaddr(checkds->zone->view->peers,
						 &dstip, &peer);
		if (result == ISC_R_SUCCESS) {
			result = dns_peer_getquerysource(peer, &src);
			if (result == ISC_R_SUCCESS) {
				have_checkdssource = true;
			}
			result = dns_peer_getforcetcp(peer, &usetcp);
			if (result == ISC_R_SUCCESS && usetcp) {
				options |= DNS_FETCHOPT_TCP;
			}
		}
	}
	switch (isc_sockaddr_pf(&checkds->dst)) {
	case PF_INET:
		if (!have_checkdssource) {
			isc_sockaddr_t any;
			isc_sockaddr_any(&any);

			src = checkds->src;
			if (isc_sockaddr_equal(&src, &any)) {
				src = checkds->zone->parentalsrc4;
			}
		}
		break;
	case PF_INET6:
		if (!have_checkdssource) {
			isc_sockaddr_t any;
			isc_sockaddr_any6(&any);

			src = checkds->src;
			if (isc_sockaddr_equal(&src, &any)) {
				src = checkds->zone->parentalsrc6;
			}
		}
		break;
	default:
		result = ISC_R_NOTIMPLEMENTED;
		goto cleanup_key;
	}

	dns_zone_log(checkds->zone, ISC_LOG_DEBUG(3),
		     "checkds: create request for DS query to %s", addrbuf);

	timeout = 5;
	options |= DNS_REQUESTOPT_TCP;
	result = dns_request_create(
		checkds->zone->view->requestmgr, message, &src, &checkds->dst,
		NULL, NULL, options, key, timeout * 3 + 1, timeout, 2,
		checkds->zone->loop, checkds_done, checkds, &checkds->request);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(checkds->zone, ISC_LOG_DEBUG(3),
			     "checkds: dns_request_create() to %s failed: %s",
			     addrbuf, isc_result_totext(result));
	}

cleanup_key:
	if (key != NULL) {
		dns_tsigkey_detach(&key);
	}
cleanup_message:
	dns_message_detach(&message);
cleanup:
	UNLOCK_ZONE(checkds->zone);
	if (result != ISC_R_SUCCESS) {
		checkds_destroy(checkds, false);
	}
}

static void
checkds_send_tons(dns_checkds_t *checkds) {
	dns_adbaddrinfo_t *ai;
	isc_sockaddr_t dst;
	isc_result_t result;
	dns_checkds_t *newcheckds = NULL;
	dns_zone_t *zone = NULL;

	/*
	 * Zone lock held by caller.
	 */
	REQUIRE(DNS_CHECKDS_VALID(checkds));
	REQUIRE(LOCKED_ZONE(checkds->zone));

	zone = checkds->zone;

	if (DNS_ZONE_FLAG(checkds->zone, DNS_ZONEFLG_EXITING)) {
		return;
	}

	for (ai = ISC_LIST_HEAD(checkds->find->list); ai != NULL;
	     ai = ISC_LIST_NEXT(ai, publink))
	{
		dst = ai->sockaddr;
		if (checkds_isqueued(zone, NULL, &dst, NULL, NULL)) {
			continue;
		}

		newcheckds = NULL;
		result = checkds_create(checkds->mctx, 0, &newcheckds);
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}
		zone_iattach(zone, &newcheckds->zone);
		ISC_LIST_APPEND(newcheckds->zone->checkds_requests, newcheckds,
				link);
		newcheckds->dst = dst;
		dns_name_dup(&checkds->ns, checkds->mctx, &newcheckds->ns);
		switch (isc_sockaddr_pf(&newcheckds->dst)) {
		case PF_INET:
			isc_sockaddr_any(&newcheckds->src);
			break;
		case PF_INET6:
			isc_sockaddr_any6(&newcheckds->src);
			break;
		default:
			UNREACHABLE();
		}
		/*
		 * XXXWMM: Should we attach key and transport here?
		 * Probably not, because we expect the name servers to be
		 * publicly available on the default transport protocol.
		 */

		result = isc_ratelimiter_enqueue(
			newcheckds->zone->zmgr->checkdsrl,
			newcheckds->zone->loop, checkds_send_toaddr, newcheckds,
			&newcheckds->rlevent);
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}
		newcheckds = NULL;
	}

cleanup:
	if (newcheckds != NULL) {
		checkds_destroy(newcheckds, true);
	}
}

static void
checkds_send(dns_zone_t *zone) {
	dns_view_t *view = dns_zone_getview(zone);
	isc_result_t result;
	unsigned int flags = 0;
	unsigned int i = 0;

	/*
	 * Zone lock held by caller.
	 */
	REQUIRE(LOCKED_ZONE(zone));

	dns_zone_log(zone, ISC_LOG_DEBUG(3),
		     "checkds: start sending DS queries to %u parentals",
		     dns_remote_count(&zone->parentals));

	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING)) {
		dns_zone_log(zone, ISC_LOG_DEBUG(3),
			     "checkds: abort, named exiting");
		return;
	}

	dns_remote_reset(&zone->parentals, false);
	while (!dns_remote_done(&zone->parentals)) {
		dns_tsigkey_t *key = NULL;
		dns_transport_t *transport = NULL;
		isc_sockaddr_t src, dst;
		dns_checkds_t *checkds = NULL;

		i++;

		if (dns_remote_keyname(&zone->parentals) != NULL) {
			dns_name_t *keyname =
				dns_remote_keyname(&zone->parentals);
			(void)dns_view_gettsig(view, keyname, &key);
		}

		if (dns_remote_tlsname(&zone->parentals) != NULL) {
			dns_name_t *tlsname =
				dns_remote_tlsname(&zone->parentals);
			(void)dns_view_gettransport(view, DNS_TRANSPORT_TLS,
						    tlsname, &transport);
			dns_zone_logc(
				zone, DNS_LOGCATEGORY_XFER_IN, ISC_LOG_INFO,
				"got TLS configuration for zone transfer");
		}

		dst = dns_remote_curraddr(&zone->parentals);
		src = dns_remote_sourceaddr(&zone->parentals);
		INSIST(isc_sockaddr_pf(&src) == isc_sockaddr_pf(&dst));

		if (isc_sockaddr_disabled(&dst)) {
			goto next;
		}

		/* TODO: glue the transport to the checkds request */

		if (checkds_isqueued(zone, NULL, &dst, key, transport)) {
			dns_zone_log(zone, ISC_LOG_DEBUG(3),
				     "checkds: DS query to parent "
				     "%d is queued",
				     i);
			if (key != NULL) {
				dns_tsigkey_detach(&key);
			}
			if (transport != NULL) {
				dns_transport_detach(&transport);
			}
			goto next;
		}

		dns_zone_log(zone, ISC_LOG_DEBUG(3),
			     "checkds: create DS query for "
			     "parent %d",
			     i);

		result = checkds_create(zone->mctx, flags, &checkds);
		if (result != ISC_R_SUCCESS) {
			dns_zone_log(zone, ISC_LOG_DEBUG(3),
				     "checkds: create DS query for "
				     "parent %d failed",
				     i);
			goto next;
		}
		zone_iattach(zone, &checkds->zone);
		dns_name_dup(dns_rootname, checkds->mctx, &checkds->ns);
		checkds->src = src;
		checkds->dst = dst;

		INSIST(checkds->key == NULL);
		if (key != NULL) {
			checkds->key = key;
			key = NULL;
		}

		INSIST(checkds->transport == NULL);
		if (transport != NULL) {
			checkds->transport = transport;
			transport = NULL;
		}

		ISC_LIST_APPEND(zone->checkds_requests, checkds, link);
		result = isc_ratelimiter_enqueue(
			checkds->zone->zmgr->checkdsrl, checkds->zone->loop,
			checkds_send_toaddr, checkds, &checkds->rlevent);
		if (result != ISC_R_SUCCESS) {
			dns_zone_log(zone, ISC_LOG_DEBUG(3),
				     "checkds: send DS query to "
				     "parent %d failed",
				     i);
			checkds_destroy(checkds, true);
		}

	next:
		dns_remote_next(&zone->parentals, false);
	}
}

/*
 * An NS RRset has been fetched from the parent of a zone whose DS RRset needs
 * to be checked; scan the RRset and start sending queries to the parental
 * agents.
 */
static void
nsfetch_done(void *arg) {
	dns_fetchresponse_t *resp = (dns_fetchresponse_t *)arg;
	isc_result_t result, eresult;
	dns_nsfetch_t *nsfetch = NULL;
	dns_zone_t *zone = NULL;
	isc_mem_t *mctx = NULL;
	dns_name_t *zname = NULL;
	dns_name_t *pname = NULL;
	char pnamebuf[DNS_NAME_FORMATSIZE];
	bool free_needed, levelup = false;
	dns_rdataset_t *nsrrset = NULL;
	dns_rdataset_t *nssigset = NULL;

	INSIST(resp != NULL);

	nsfetch = resp->arg;

	INSIST(nsfetch != NULL);

	zone = nsfetch->zone;
	mctx = nsfetch->mctx;
	zname = dns_fixedname_name(&nsfetch->name);
	pname = &nsfetch->pname;
	nsrrset = &nsfetch->nsrrset;
	nssigset = &nsfetch->nssigset;
	eresult = resp->result;

	/* Free resources which are not of interest */
	if (resp->node != NULL) {
		dns_db_detachnode(resp->db, &resp->node);
	}
	if (resp->db != NULL) {
		dns_db_detach(&resp->db);
	}
	dns_resolver_destroyfetch(&nsfetch->fetch);

	LOCK_ZONE(zone);
	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING) || zone->view == NULL) {
		goto cleanup;
	}

	zone->nsfetchcount--;

	dns_name_format(pname, pnamebuf, sizeof(pnamebuf));
	dnssec_log(zone, ISC_LOG_DEBUG(3),
		   "Returned from '%s' NS fetch in nsfetch_done(): %s",
		   pnamebuf, isc_result_totext(eresult));

	if (eresult == DNS_R_NCACHENXRRSET || eresult == DNS_R_NXRRSET) {
		dnssec_log(zone, ISC_LOG_DEBUG(3),
			   "NODATA response for NS '%s', level up", pnamebuf);
		levelup = true;
		goto cleanup;

	} else if (eresult != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_WARNING,
			   "Unable to fetch NS set '%s': %s", pnamebuf,
			   isc_result_totext(eresult));
		result = eresult;
		goto done;
	}

	/* No NS records found */
	if (!dns_rdataset_isassociated(nsrrset)) {
		dnssec_log(zone, ISC_LOG_WARNING,
			   "No NS records found for '%s'", pnamebuf);
		result = ISC_R_NOTFOUND;
		goto done;
	}

	/* No RRSIGs found */
	if (!dns_rdataset_isassociated(nssigset)) {
		dnssec_log(zone, ISC_LOG_WARNING, "No NS RRSIGs found for '%s'",
			   pnamebuf);
		result = DNS_R_MUSTBESECURE;
		goto done;
	}

	/* Check trust level */
	if (nsrrset->trust < dns_trust_secure) {
		dnssec_log(zone, ISC_LOG_WARNING,
			   "Invalid NS RRset for '%s' trust level %u", pnamebuf,
			   nsrrset->trust);
		result = DNS_R_MUSTBESECURE;
		goto done;
	}

	/* Record the number of NS records we found. */
	zone->parent_nscount = dns_rdataset_count(nsrrset);

	UNLOCK_ZONE(zone);

	/* Look up the addresses for the found parental name servers. */
	for (result = dns_rdataset_first(nsrrset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(nsrrset))
	{
		dns_checkds_t *checkds = NULL;
		dns_rdata_t rdata = DNS_RDATA_INIT;
		dns_rdata_ns_t ns;
		bool isqueued;

		dns_rdataset_current(nsrrset, &rdata);
		result = dns_rdata_tostruct(&rdata, &ns, NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);

		dns_rdata_reset(&rdata);

		LOCK_ZONE(zone);
		isqueued = checkds_isqueued(zone, &ns.name, NULL, NULL, NULL);
		UNLOCK_ZONE(zone);
		if (isqueued) {
			continue;
		}
		result = checkds_create(zone->mctx, 0, &checkds);
		if (result != ISC_R_SUCCESS) {
			dns_zone_log(zone, ISC_LOG_DEBUG(3),
				     "checkds: checkds_create() failed: %s",
				     isc_result_totext(result));
			break;
		}

		if (isc_log_wouldlog(dns_lctx, ISC_LOG_DEBUG(3))) {
			char nsnamebuf[DNS_NAME_FORMATSIZE];
			dns_name_format(&ns.name, nsnamebuf, sizeof(nsnamebuf));
			dns_zone_log(zone, ISC_LOG_DEBUG(3),
				     "checkds: send DS query to NS %s",
				     nsnamebuf);
		}

		LOCK_ZONE(zone);
		zone_iattach(zone, &checkds->zone);
		dns_name_dup(&ns.name, zone->mctx, &checkds->ns);
		ISC_LIST_APPEND(zone->checkds_requests, checkds, link);
		UNLOCK_ZONE(zone);

		checkds_find_address(checkds);
	}
	if (result == ISC_R_NOMORE) {
		result = ISC_R_SUCCESS;
	}

	LOCK_ZONE(zone);

done:
	if (result != ISC_R_SUCCESS) {
		dnssec_log(
			zone, ISC_LOG_ERROR,
			"checkds: error during parental-agents processing: %s",
			isc_result_totext(result));
	}

cleanup:
	isc_refcount_decrement(&zone->irefs);

	if (dns_rdataset_isassociated(nsrrset)) {
		dns_rdataset_disassociate(nsrrset);
	}
	if (dns_rdataset_isassociated(nssigset)) {
		dns_rdataset_disassociate(nssigset);
	}

	dns_resolver_freefresp(&resp);

	if (levelup) {
		UNLOCK_ZONE(zone);
		nsfetch_levelup(nsfetch);
		return;
	}

	dns_name_free(zname, mctx);
	isc_mem_putanddetach(&nsfetch->mctx, nsfetch, sizeof(dns_nsfetch_t));

	free_needed = exit_check(zone);
	UNLOCK_ZONE(zone);

	if (free_needed) {
		zone_free(zone);
	}
}

static void
do_nsfetch(void *arg) {
	dns_nsfetch_t *nsfetch = (dns_nsfetch_t *)arg;
	isc_result_t result;
	unsigned int nlabels = 1;
	dns_resolver_t *resolver = NULL;
	dns_zone_t *zone = nsfetch->zone;
	unsigned int options = DNS_FETCHOPT_UNSHARED | DNS_FETCHOPT_NOCACHED;

	if (DNS_ZONE_FLAG(zone, DNS_ZONEFLG_EXITING)) {
		result = ISC_R_SHUTTINGDOWN;
		goto cleanup;
	}

	result = dns_view_getresolver(zone->view, &resolver);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	if (isc_log_wouldlog(dns_lctx, ISC_LOG_DEBUG(3))) {
		char namebuf[DNS_NAME_FORMATSIZE];
		dns_name_format(&nsfetch->pname, namebuf, sizeof(namebuf));
		dnssec_log(zone, ISC_LOG_DEBUG(3),
			   "Create fetch for '%s' NS request", namebuf);
	}

	/* Derive parent domain. XXXWMM: Check for root domain */
	dns_name_split(&nsfetch->pname,
		       dns_name_countlabels(&nsfetch->pname) - nlabels, NULL,
		       &nsfetch->pname);

	/*
	 * Use of DNS_FETCHOPT_NOCACHED is essential here.  If it is not
	 * set and the cache still holds a non-expired, validated version
	 * of the RRset being queried for by the time the response is
	 * received, the cached RRset will be passed to nsfetch_done()
	 * instead of the one received in the response as the latter will
	 * have a lower trust level due to not being validated until
	 * nsfetch_done() is called.
	 */
	result = dns_resolver_createfetch(
		resolver, &nsfetch->pname, dns_rdatatype_ns, NULL, NULL, NULL,
		NULL, 0, options, 0, NULL, NULL, zone->loop, nsfetch_done,
		nsfetch, NULL, &nsfetch->nsrrset, &nsfetch->nssigset,
		&nsfetch->fetch);

	dns_resolver_detach(&resolver);

cleanup:
	if (result != ISC_R_SUCCESS) {
		dns_name_t *zname = dns_fixedname_name(&nsfetch->name);
		bool free_needed;
		char namebuf[DNS_NAME_FORMATSIZE];
		dns_name_format(&nsfetch->pname, namebuf, sizeof(namebuf));
		dnssec_log(zone, ISC_LOG_WARNING,
			   "Failed to create fetch for '%s' NS request",
			   namebuf);
		LOCK_ZONE(zone);
		zone->nsfetchcount--;
		isc_refcount_decrement(&zone->irefs);

		dns_name_free(zname, zone->mctx);
		isc_mem_putanddetach(&nsfetch->mctx, nsfetch, sizeof(*nsfetch));

		free_needed = exit_check(zone);
		UNLOCK_ZONE(zone);
		if (free_needed) {
			zone_free(zone);
		}
	}
}

/*
 * Retry an NS RRset lookup, one level up. In other words, this function should
 * be called on an dns_nsfetch structure where the response yielded in a NODATA
 * response. This must be because there is an empty non-terminal inbetween the
 * child and parent zone.
 */
static void
nsfetch_levelup(dns_nsfetch_t *nsfetch) {
	dns_zone_t *zone = nsfetch->zone;

#ifdef ENABLE_AFL
	if (!dns_fuzzing_resolver) {
#endif /* ifdef ENABLE_AFL */
		LOCK_ZONE(zone);
		zone->nsfetchcount++;
		isc_refcount_increment0(&zone->irefs);

		dns_rdataset_init(&nsfetch->nsrrset);
		dns_rdataset_init(&nsfetch->nssigset);
		if (isc_log_wouldlog(dns_lctx, ISC_LOG_DEBUG(3))) {
			dnssec_log(zone, ISC_LOG_DEBUG(3),
				   "Creating parent NS fetch in "
				   "nsfetch_levelup()");
		}
		isc_async_run(zone->loop, do_nsfetch, nsfetch);
		UNLOCK_ZONE(zone);
#ifdef ENABLE_AFL
	}
#endif /* ifdef ENABLE_AFL */
}

static void
zone_checkds(dns_zone_t *zone) {
	bool cdscheck = false;
	dns_checkdstype_t checkdstype = zone->checkdstype;

	if (checkdstype == dns_checkdstype_no) {
		return;
	}

	for (dns_dnsseckey_t *key = ISC_LIST_HEAD(zone->checkds_ok);
	     key != NULL; key = ISC_LIST_NEXT(key, link))
	{
		dst_key_state_t ds_state = DST_KEY_STATE_NA;
		bool ksk = false;
		isc_stdtime_t published = 0, withdrawn = 0;

		/* Is this key have the KSK role? */
		(void)dst_key_role(key->key, &ksk, NULL);
		if (!ksk) {
			continue;
		}

		/* Do we need to check the DS RRset? */
		(void)dst_key_getstate(key->key, DST_KEY_DS, &ds_state);
		(void)dst_key_gettime(key->key, DST_TIME_DSPUBLISH, &published);
		(void)dst_key_gettime(key->key, DST_TIME_DSDELETE, &withdrawn);

		if (ds_state == DST_KEY_STATE_RUMOURED && published == 0) {
			dst_key_setnum(key->key, DST_NUM_DSPUBCOUNT, 0);
			cdscheck = true;
		} else if (ds_state == DST_KEY_STATE_UNRETENTIVE &&
			   withdrawn == 0)
		{
			dst_key_setnum(key->key, DST_NUM_DSDELCOUNT, 0);
			cdscheck = true;
		}
	}

	if (!cdscheck) {
		return;
	}

	if (checkdstype == dns_checkdstype_explicit) {
		/* Request the DS RRset. */
		LOCK_ZONE(zone);
		checkds_send(zone);
		UNLOCK_ZONE(zone);
		return;
	}

	INSIST(checkdstype == dns_checkdstype_yes);

#ifdef ENABLE_AFL
	if (!dns_fuzzing_resolver) {
#endif /* ifdef ENABLE_AFL */
		dns_nsfetch_t *nsfetch;
		dns_name_t *name = NULL;

		nsfetch = isc_mem_get(zone->mctx, sizeof(dns_nsfetch_t));
		*nsfetch = (dns_nsfetch_t){ .zone = zone };
		isc_mem_attach(zone->mctx, &nsfetch->mctx);
		LOCK_ZONE(zone);
		zone->nsfetchcount++;
		isc_refcount_increment0(&zone->irefs);
		name = dns_fixedname_initname(&nsfetch->name);
		dns_name_init(&nsfetch->pname, NULL);
		dns_name_clone(&zone->origin, &nsfetch->pname);
		dns_name_dup(&zone->origin, zone->mctx, name);
		dns_rdataset_init(&nsfetch->nsrrset);
		dns_rdataset_init(&nsfetch->nssigset);
		if (isc_log_wouldlog(dns_lctx, ISC_LOG_DEBUG(3))) {
			dnssec_log(
				zone, ISC_LOG_DEBUG(3),
				"Creating parent NS fetch in zone_checkds()");
		}
		isc_async_run(zone->loop, do_nsfetch, nsfetch);
		UNLOCK_ZONE(zone);
#ifdef ENABLE_AFL
	}
#endif /* ifdef ENABLE_AFL */
}

static isc_result_t
update_ttl(dns_rdataset_t *rdataset, dns_name_t *name, dns_ttl_t ttl,
	   dns_diff_t *diff) {
	isc_result_t result;

	/*
	 * Delete everything using the existing TTL.
	 */
	for (result = dns_rdataset_first(rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(rdataset))
	{
		dns_difftuple_t *tuple = NULL;
		dns_rdata_t rdata = DNS_RDATA_INIT;

		dns_rdataset_current(rdataset, &rdata);
		result = dns_difftuple_create(diff->mctx, DNS_DIFFOP_DEL, name,
					      rdataset->ttl, &rdata, &tuple);
		if (result != ISC_R_SUCCESS) {
			return result;
		}
		dns_diff_appendminimal(diff, &tuple);
	}
	if (result != ISC_R_NOMORE) {
		return result;
	}

	/*
	 * Add everything using the new TTL.
	 */
	for (result = dns_rdataset_first(rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(rdataset))
	{
		dns_difftuple_t *tuple = NULL;
		dns_rdata_t rdata = DNS_RDATA_INIT;

		dns_rdataset_current(rdataset, &rdata);
		result = dns_difftuple_create(diff->mctx, DNS_DIFFOP_ADD, name,
					      ttl, &rdata, &tuple);
		if (result != ISC_R_SUCCESS) {
			return result;
		}
		dns_diff_appendminimal(diff, &tuple);
	}
	if (result != ISC_R_NOMORE) {
		return result;
	}
	return ISC_R_SUCCESS;
}

static isc_result_t
zone_verifykeys(dns_zone_t *zone, dns_dnsseckeylist_t *newkeys,
		uint32_t purgeval, isc_stdtime_t now) {
	dns_dnsseckey_t *key1, *key2, *next;

	/*
	 * Make sure that the existing keys are also present in the new keylist.
	 */
	for (key1 = ISC_LIST_HEAD(zone->keyring); key1 != NULL; key1 = next) {
		bool found = false;
		next = ISC_LIST_NEXT(key1, link);

		if (dst_key_is_unused(key1->key)) {
			continue;
		}
		if (dns_keymgr_key_may_be_purged(key1->key, purgeval, now)) {
			continue;
		}
		if (key1->purge) {
			continue;
		}

		for (key2 = ISC_LIST_HEAD(*newkeys); key2 != NULL;
		     key2 = ISC_LIST_NEXT(key2, link))
		{
			if (dst_key_compare(key1->key, key2->key)) {
				found = true;
				break;
			}
		}

		if (!found) {
			char keystr[DST_KEY_FORMATSIZE];
			dst_key_format(key1->key, keystr, sizeof(keystr));
			dnssec_log(zone, ISC_LOG_DEBUG(1),
				   "verifykeys: key %s - not available",
				   keystr);
			return ISC_R_NOTFOUND;
		}
	}

	/* All good. */
	return ISC_R_SUCCESS;
}

static void
remove_rdataset(dns_zone_t *zone, dns_diff_t *diff, dns_rdataset_t *rdataset) {
	if (!dns_rdataset_isassociated(rdataset)) {
		return;
	}

	for (isc_result_t result = dns_rdataset_first(rdataset);
	     result == ISC_R_SUCCESS; result = dns_rdataset_next(rdataset))
	{
		dns_rdata_t rdata = DNS_RDATA_INIT;
		dns_difftuple_t *tuple = NULL;

		dns_rdataset_current(rdataset, &rdata);
		dns_difftuple_create(zone->mctx, DNS_DIFFOP_DEL, &zone->origin,
				     rdataset->ttl, &rdata, &tuple);
		dns_diff_append(diff, &tuple);
	}
	return;
}

static void
add_tuple(dns_diff_t *diff, dns_difftuple_t *tuple) {
	dns_difftuple_t *copy = NULL;

	dns_difftuple_copy(tuple, &copy);
	dns_diff_appendminimal(diff, &copy);
}

static void
zone_apply_skrbundle(dns_zone_t *zone, dns_skrbundle_t *bundle,
		     dns_rdataset_t *dnskeyset, dns_rdataset_t *cdsset,
		     dns_rdataset_t *cdnskeyset, dns_diff_t *diff) {
	dns_kasp_t *kasp = zone->kasp;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(DNS_KASP_VALID(kasp));
	REQUIRE(DNS_SKRBUNDLE_VALID(bundle));

	/* Remove existing DNSKEY, CDS, and CDNSKEY records. */
	remove_rdataset(zone, diff, dnskeyset);
	remove_rdataset(zone, diff, cdsset);
	remove_rdataset(zone, diff, cdnskeyset);

	/* Add the records from the bundle. */
	dns_difftuple_t *tuple = ISC_LIST_HEAD(bundle->diff.tuples);
	while (tuple != NULL) {
		switch (tuple->rdata.type) {
		case dns_rdatatype_dnskey:
			add_tuple(diff, tuple);
			break;
		case dns_rdatatype_cdnskey:
		case dns_rdatatype_cds:
			add_tuple(diff, tuple);
			break;
		case dns_rdatatype_rrsig:
			/* Not interested in right now */
			break;
		default:
			INSIST(0);
		}

		tuple = ISC_LIST_NEXT(tuple, link);
	}
}

static void
zone_rekey(dns_zone_t *zone) {
	isc_result_t result;
	dns_db_t *db = NULL;
	dns_dbnode_t *node = NULL;
	dns_dbversion_t *ver = NULL;
	dns_rdataset_t cdsset, soaset, soasigs, keyset, keysigs, cdnskeyset;
	dns_dnsseckeylist_t dnskeys, keys, rmkeys;
	dns_dnsseckey_t *key = NULL;
	dns_diff_t diff, _sig_diff;
	dns_kasp_t *kasp;
	dns_skrbundle_t *bundle = NULL;
	dns__zonediff_t zonediff;
	bool commit = false, newactive = false;
	bool newalg = false;
	bool fullsign;
	bool offlineksk = false;
	uint32_t sigval = 0;
	dns_ttl_t ttl = 3600;
	const char *dir = NULL;
	isc_mem_t *mctx = NULL;
	isc_stdtime_t now, nexttime = 0;
	isc_time_t timenow;
	isc_interval_t ival;
	char timebuf[80];

	REQUIRE(DNS_ZONE_VALID(zone));

	ISC_LIST_INIT(dnskeys);
	ISC_LIST_INIT(keys);
	ISC_LIST_INIT(rmkeys);
	dns_rdataset_init(&soaset);
	dns_rdataset_init(&soasigs);
	dns_rdataset_init(&keyset);
	dns_rdataset_init(&keysigs);
	dns_rdataset_init(&cdsset);
	dns_rdataset_init(&cdnskeyset);
	mctx = zone->mctx;
	dns_diff_init(mctx, &diff);
	dns_diff_init(mctx, &_sig_diff);
	zonediff_init(&zonediff, &_sig_diff);

	CHECK(dns_zone_getdb(zone, &db));
	CHECK(dns_db_newversion(db, &ver));
	CHECK(dns_db_getoriginnode(db, &node));

	timenow = isc_time_now();
	now = isc_time_seconds(&timenow);

	kasp = zone->kasp;
	dir = dns_zone_getkeydirectory(zone);

	dnssec_log(zone, ISC_LOG_INFO, "reconfiguring zone keys");

	/* Get the SOA record's TTL */
	CHECK(dns_db_findrdataset(db, node, ver, dns_rdatatype_soa,
				  dns_rdatatype_none, 0, &soaset, &soasigs));
	ttl = soaset.ttl;
	dns_rdataset_disassociate(&soaset);

	if (kasp != NULL) {
		ttl = dns_kasp_dnskeyttl(kasp);
		offlineksk = dns_kasp_offlineksk(kasp);
		sigval = dns_kasp_sigvalidity_dnskey(kasp);
	}

	/* Get the current DNSKEY rdataset */
	result = dns_db_findrdataset(db, node, ver, dns_rdatatype_dnskey,
				     dns_rdatatype_none, 0, &keyset, &keysigs);
	if (result == ISC_R_SUCCESS) {
		/*
		 * If we don't have a policy then use the DNSKEY ttl
		 * if it exists.  Otherwise update the DNSKEY ttl if
		 * needed.
		 */
		if (kasp == NULL) {
			ttl = keyset.ttl;
		} else if (ttl != keyset.ttl && !offlineksk) {
			result = update_ttl(&keyset, &zone->origin, ttl, &diff);
			if (result != ISC_R_SUCCESS) {
				dnssec_log(zone, ISC_LOG_ERROR,
					   "Updating DNSKEY TTL from %u to %u "
					   "failed: %s",
					   keyset.ttl, ttl,
					   isc_result_totext(result));
				goto failure;
			}
			dnssec_log(zone, ISC_LOG_INFO,
				   "Updating DNSKEY TTL from %u to %u",
				   keyset.ttl, ttl);
			keyset.ttl = ttl;
		}

		dns_zone_lock_keyfiles(zone);

		result = dns_dnssec_keylistfromrdataset(
			&zone->origin, kasp, dir, mctx, &keyset, &keysigs,
			&soasigs, false, false, &dnskeys);

		dns_zone_unlock_keyfiles(zone);

		if (result != ISC_R_SUCCESS) {
			goto failure;
		}
	} else if (result != ISC_R_NOTFOUND) {
		goto failure;
	}

	/* Get the current CDS rdataset */
	result = dns_db_findrdataset(db, node, ver, dns_rdatatype_cds,
				     dns_rdatatype_none, 0, &cdsset, NULL);
	if (result != ISC_R_SUCCESS && dns_rdataset_isassociated(&cdsset)) {
		dns_rdataset_disassociate(&cdsset);
	} else if (result == ISC_R_SUCCESS && kasp != NULL &&
		   ttl != cdsset.ttl && !offlineksk)
	{
		result = update_ttl(&cdsset, &zone->origin, ttl, &diff);
		if (result != ISC_R_SUCCESS) {
			dnssec_log(zone, ISC_LOG_ERROR,
				   "Updating CDS TTL from %u to %u failed: %s",
				   cdsset.ttl, ttl, isc_result_totext(result));
			goto failure;
		}
		dnssec_log(zone, ISC_LOG_INFO, "Updating CDS TTL from %u to %u",
			   cdsset.ttl, ttl);
		cdsset.ttl = ttl;
	}

	/* Get the current CDNSKEY rdataset */
	result = dns_db_findrdataset(db, node, ver, dns_rdatatype_cdnskey,
				     dns_rdatatype_none, 0, &cdnskeyset, NULL);
	if (result != ISC_R_SUCCESS && dns_rdataset_isassociated(&cdnskeyset)) {
		dns_rdataset_disassociate(&cdnskeyset);
	} else if (result == ISC_R_SUCCESS && kasp != NULL &&
		   ttl != cdnskeyset.ttl && !offlineksk)
	{
		result = update_ttl(&cdnskeyset, &zone->origin, ttl, &diff);
		if (result != ISC_R_SUCCESS) {
			dnssec_log(
				zone, ISC_LOG_ERROR,
				"Updating CDNSKEY TTL from %u to %u failed: %s",
				cdnskeyset.ttl, ttl, isc_result_totext(result));
			goto failure;
		}
		dnssec_log(zone, ISC_LOG_INFO,
			   "Updating CDNSKEY TTL from %u to %u", cdnskeyset.ttl,
			   ttl);
		cdnskeyset.ttl = ttl;
	}

	/*
	 * True when called from "rndc sign".  Indicates the zone should be
	 * fully signed now.
	 */
	fullsign = DNS_ZONEKEY_OPTION(zone, DNS_ZONEKEY_FULLSIGN);

	if (offlineksk) {
		/* Lookup the correct bundle in the SKR. */
		LOCK_ZONE(zone);
		if (zone->skr == NULL) {
			UNLOCK_ZONE(zone);
			dnssec_log(zone, ISC_LOG_DEBUG(1),
				   "zone_rekey:dns_skr_lookup failed: "
				   "no SKR available");
			result = DNS_R_NOSKRFILE;
			goto failure;
		}
		bundle = dns_skr_lookup(zone->skr, now, sigval);
		zone->skrbundle = bundle;
		UNLOCK_ZONE(zone);

		if (bundle == NULL) {
			char nowstr[26]; /* Minimal buf per ctime_r() spec. */
			char utc[sizeof("YYYYMMDDHHSSMM")];
			isc_buffer_t b;
			isc_region_t r;
			isc_buffer_init(&b, utc, sizeof(utc));

			isc_stdtime_tostring(now, nowstr, sizeof(nowstr));
			(void)dns_time32_totext(now, &b);
			isc_buffer_usedregion(&b, &r);
			dnssec_log(zone, ISC_LOG_DEBUG(1),
				   "zone_rekey:dns_skr_lookup failed: "
				   "no available SKR bundle for time "
				   "%.*s (%s)",
				   (int)r.length, r.base, nowstr);
			result = DNS_R_NOSKRBUNDLE;
			goto failure;
		}

		zone_apply_skrbundle(zone, bundle, &keyset, &cdsset,
				     &cdnskeyset, &diff);

		dns_skrbundle_t *next = ISC_LIST_NEXT(bundle, link);
		if (next != NULL) {
			if (nexttime == 0) {
				nexttime = next->inception;
			}
		} else {
			dnssec_log(zone, ISC_LOG_WARNING,
				   "zone_rekey: last bundle in skr, please "
				   "import new skr file");
		}
	}

	/*
	 * DNSSEC Key and Signing Policy
	 */

	KASP_LOCK(kasp);

	dns_zone_lock_keyfiles(zone);
	result = dns_dnssec_findmatchingkeys(&zone->origin, kasp, dir,
					     zone->keystores, now, mctx, &keys);
	dns_zone_unlock_keyfiles(zone);

	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_DEBUG(1),
			   "zone_rekey:dns_dnssec_findmatchingkeys failed: %s",
			   isc_result_totext(result));
	}

	if (kasp != NULL && !offlineksk) {
		/* Verify new keys. */
		isc_result_t ret = zone_verifykeys(
			zone, &keys, dns_kasp_purgekeys(kasp), now);
		if (ret != ISC_R_SUCCESS) {
			dnssec_log(zone, ISC_LOG_ERROR,
				   "zone_rekey:zone_verifykeys failed: "
				   "some key files are missing");
			KASP_UNLOCK(kasp);
			goto failure;
		}

		/*
		 * Check DS at parental agents. Clear ongoing checks.
		 */
		LOCK_ZONE(zone);
		checkds_cancel(zone);
		clear_keylist(&zone->checkds_ok, zone->mctx);
		ISC_LIST_INIT(zone->checkds_ok);
		UNLOCK_ZONE(zone);

		ret = dns_zone_getdnsseckeys(zone, db, ver, now,
					     &zone->checkds_ok);
		if (ret == ISC_R_SUCCESS) {
			zone_checkds(zone);
		} else {
			dnssec_log(zone,
				   (ret == ISC_R_NOTFOUND) ? ISC_LOG_DEBUG(1)
							   : ISC_LOG_ERROR,
				   "zone_rekey:dns_zone_getdnsseckeys failed: "
				   "%s",
				   isc_result_totext(ret));
		}

		/* Run keymgr. */
		if (result == ISC_R_SUCCESS || result == ISC_R_NOTFOUND) {
			dns_zone_lock_keyfiles(zone);
			result = dns_keymgr_run(&zone->origin, zone->rdclass,
						mctx, &keys, &dnskeys, dir,
						kasp, now, &nexttime);
			dns_zone_unlock_keyfiles(zone);

			if (result != ISC_R_SUCCESS) {
				dnssec_log(zone, ISC_LOG_ERROR,
					   "zone_rekey:dns_keymgr_run "
					   "failed: %s",
					   isc_result_totext(result));
				KASP_UNLOCK(kasp);
				goto failure;
			}
		}
	} else if (offlineksk) {
		/*
		 * With offline-ksk enabled we don't run the keymgr.
		 * Instead we derive the states from the timing metadata.
		 */
		dns_zone_lock_keyfiles(zone);
		result = dns_keymgr_offline(&zone->origin, &keys, kasp, now,
					    &nexttime);
		dns_zone_unlock_keyfiles(zone);

		if (result != ISC_R_SUCCESS) {
			dnssec_log(zone, ISC_LOG_ERROR,
				   "zone_rekey:dns_keymgr_offline "
				   "failed: %s",
				   isc_result_totext(result));
		}
	}

	KASP_UNLOCK(kasp);

	/*
	 * Update CDS, CDNSKEY and DNSKEY record sets if the keymgr ran
	 * successfully (dns_keymgr_run returned ISC_R_SUCCESS), or in
	 * case of DNSSEC management without dnssec-policy if we have keys
	 * (dns_dnssec_findmatchingkeys returned ISC_R_SUCCESS).
	 */
	if (result == ISC_R_SUCCESS) {
		dns_kasp_digestlist_t digests;
		bool cdsdel = false;
		bool cdnskeydel = false;
		bool cdnskeypub = true;
		bool sane_diff, sane_dnskey;
		isc_stdtime_t when;

		result = dns_dnssec_updatekeys(&dnskeys, &keys, &rmkeys,
					       &zone->origin, ttl, &diff, mctx,
					       dnssec_report);
		/*
		 * Keys couldn't be updated for some reason;
		 * try again later.
		 */
		if (result != ISC_R_SUCCESS) {
			dnssec_log(zone, ISC_LOG_ERROR,
				   "zone_rekey:couldn't update zone keys: %s",
				   isc_result_totext(result));
			goto failure;
		}

		if (offlineksk) {
			/* We can skip a lot of things */
			goto post_sync;
		}

		/*
		 * Publish CDS/CDNSKEY DELETE records if the zone is
		 * transitioning from secure to insecure.
		 */
		if (kasp != NULL) {
			if (strcmp(dns_kasp_getname(kasp), "insecure") == 0) {
				cdsdel = true;
				cdnskeydel = true;
			}
			digests = dns_kasp_digests(kasp);
			cdnskeypub = dns_kasp_cdnskey(kasp);
		} else {
			/* Check if there is a CDS DELETE record. */
			if (dns_rdataset_isassociated(&cdsset)) {
				for (result = dns_rdataset_first(&cdsset);
				     result == ISC_R_SUCCESS;
				     result = dns_rdataset_next(&cdsset))
				{
					dns_rdata_t crdata = DNS_RDATA_INIT;
					dns_rdataset_current(&cdsset, &crdata);
					/*
					 * CDS deletion record has this form
					 * "0 0 0 00" which is 5 zero octets.
					 */
					if (crdata.length == 5U &&
					    memcmp(crdata.data,
						   (unsigned char[5]){ 0, 0, 0,
								       0, 0 },
						   5) == 0)
					{
						cdsdel = true;
						break;
					}
				}
			}

			/* Check if there is a CDNSKEY DELETE record. */
			if (dns_rdataset_isassociated(&cdnskeyset)) {
				for (result = dns_rdataset_first(&cdnskeyset);
				     result == ISC_R_SUCCESS;
				     result = dns_rdataset_next(&cdnskeyset))
				{
					dns_rdata_t crdata = DNS_RDATA_INIT;
					dns_rdataset_current(&cdnskeyset,
							     &crdata);
					/*
					 * CDNSKEY deletion record has this form
					 * "0 3 0 AA==" which is 2 zero octets,
					 * a 3, and 2 zero octets.
					 */
					if (crdata.length == 5U &&
					    memcmp(crdata.data,
						   (unsigned char[5]){ 0, 0, 3,
								       0, 0 },
						   5) == 0)
					{
						cdnskeydel = true;
						break;
					}
				}
			}

			digests = dns_kasp_digests(zone->defaultkasp);
		}

		/*
		 * Update CDS / CDNSKEY records.
		 */
		result = dns_dnssec_syncupdate(&dnskeys, &rmkeys, &cdsset,
					       &cdnskeyset, now, &digests,
					       cdnskeypub, ttl, &diff, mctx);
		if (result != ISC_R_SUCCESS) {
			dnssec_log(zone, ISC_LOG_ERROR,
				   "zone_rekey:couldn't update CDS/CDNSKEY: %s",
				   isc_result_totext(result));
			goto failure;
		}

		if (cdsdel || cdnskeydel) {
			/*
			 * Only publish CDS/CDNSKEY DELETE records if there is
			 * a KSK that can be used to verify the RRset. This
			 * means there must be a key with the KSK role that is
			 * published and is used for signing.
			 */
			bool allow = false;
			for (key = ISC_LIST_HEAD(dnskeys); key != NULL;
			     key = ISC_LIST_NEXT(key, link))
			{
				dst_key_t *dstk = key->key;

				if (dst_key_is_published(dstk, now, &when) &&
				    dst_key_is_signing(dstk, DST_BOOL_KSK, now,
						       &when))
				{
					allow = true;
					break;
				}
			}
			if (cdsdel) {
				cdsdel = allow;
			}
			if (cdnskeydel) {
				cdnskeydel = allow;
			}
		}
		result = dns_dnssec_syncdelete(
			&cdsset, &cdnskeyset, &zone->origin, zone->rdclass, ttl,
			&diff, mctx, cdsdel, cdnskeydel);
		if (result != ISC_R_SUCCESS) {
			dnssec_log(zone, ISC_LOG_ERROR,
				   "zone_rekey:couldn't update CDS/CDNSKEY "
				   "DELETE records: %s",
				   isc_result_totext(result));
			goto failure;
		}

	post_sync:
		/*
		 * See if any pre-existing keys have newly become active;
		 * also, see if any new key is for a new algorithm, as in that
		 * event, we need to sign the zone fully.  (If there's a new
		 * key, but it's for an already-existing algorithm, then
		 * the zone signing can be handled incrementally.)
		 */
		for (key = ISC_LIST_HEAD(dnskeys); key != NULL;
		     key = ISC_LIST_NEXT(key, link))
		{
			if (!key->first_sign) {
				continue;
			}

			newactive = true;

			if (!dns_rdataset_isassociated(&keysigs)) {
				newalg = true;
				break;
			}

			if (signed_with_alg(&keysigs, dst_key_alg(key->key))) {
				/*
				 * This isn't a new algorithm; clear
				 * first_sign so we won't sign the
				 * whole zone with this key later.
				 */
				key->first_sign = false;
			} else {
				newalg = true;
				break;
			}
		}

		/*
		 * A sane diff is one that is not empty, and that does not
		 * introduce a zone with NSEC only DNSKEYs along with NSEC3
		 * chains.
		 */
		sane_dnskey = dns_zone_check_dnskey_nsec3(zone, db, ver, &diff,
							  NULL, 0);
		sane_diff = !ISC_LIST_EMPTY(diff.tuples) && sane_dnskey;
		if (!sane_dnskey) {
			dnssec_log(zone, ISC_LOG_ERROR,
				   "NSEC only DNSKEYs and NSEC3 chains not "
				   "allowed");
		}

		if (newactive || fullsign || sane_diff) {
			CHECK(dns_diff_apply(&diff, db, ver));
			CHECK(clean_nsec3param(zone, db, ver, &diff));
			CHECK(add_signing_records(db, zone->privatetype, ver,
						  &diff, newalg || fullsign));
			CHECK(update_soa_serial(zone, db, ver, &diff, mctx,
						zone->updatemethod));
			CHECK(add_chains(zone, db, ver, &diff));
			CHECK(sign_apex(zone, db, ver, now, &diff, &zonediff));
			CHECK(zone_journal(zone, zonediff.diff, NULL,
					   "zone_rekey"));
			commit = true;
		}
	}

	dns_db_closeversion(db, &ver, true);

	LOCK_ZONE(zone);

	if (commit) {
		dns_difftuple_t *tuple;
		dns_stats_t *dnssecsignstats =
			dns_zone_getdnssecsignstats(zone);

		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_NEEDNOTIFY);

		zone_needdump(zone, DNS_DUMP_DELAY);

		zone_settimer(zone, &timenow);

		/* Remove any signatures from removed keys.  */
		if (!ISC_LIST_EMPTY(rmkeys)) {
			for (key = ISC_LIST_HEAD(rmkeys); key != NULL;
			     key = ISC_LIST_NEXT(key, link))
			{
				result = zone_signwithkey(
					zone, dst_key_alg(key->key),
					dst_key_id(key->key), true);
				if (result != ISC_R_SUCCESS) {
					dnssec_log(zone, ISC_LOG_ERROR,
						   "zone_signwithkey failed: "
						   "%s",
						   isc_result_totext(result));
				}

				/* Clear DNSSEC sign statistics. */
				if (dnssecsignstats != NULL) {
					dns_dnssecsignstats_clear(
						dnssecsignstats,
						dst_key_id(key->key),
						dst_key_alg(key->key));
					/*
					 * Also clear the dnssec-sign
					 * statistics of the revoked key id.
					 */
					dns_dnssecsignstats_clear(
						dnssecsignstats,
						dst_key_rid(key->key),
						dst_key_alg(key->key));
				}
			}
		}

		if (fullsign) {
			/*
			 * "rndc sign" was called, so we now sign the zone
			 * with all active keys, whether they're new or not.
			 */
			for (key = ISC_LIST_HEAD(dnskeys); key != NULL;
			     key = ISC_LIST_NEXT(key, link))
			{
				if (!key->force_sign && !key->hint_sign) {
					continue;
				}

				result = zone_signwithkey(
					zone, dst_key_alg(key->key),
					dst_key_id(key->key), false);
				if (result != ISC_R_SUCCESS) {
					dnssec_log(zone, ISC_LOG_ERROR,
						   "zone_signwithkey failed: "
						   "%s",
						   isc_result_totext(result));
				}
			}
		} else if (newalg) {
			/*
			 * We haven't been told to sign fully, but a new
			 * algorithm was added to the DNSKEY.  We sign
			 * the full zone, but only with newly active
			 * keys.
			 */
			for (key = ISC_LIST_HEAD(dnskeys); key != NULL;
			     key = ISC_LIST_NEXT(key, link))
			{
				if (!key->first_sign) {
					continue;
				}

				result = zone_signwithkey(
					zone, dst_key_alg(key->key),
					dst_key_id(key->key), false);
				if (result != ISC_R_SUCCESS) {
					dnssec_log(zone, ISC_LOG_ERROR,
						   "zone_signwithkey failed: "
						   "%s",
						   isc_result_totext(result));
				}
			}
		}

		/*
		 * Clear fullsign flag, if it was set, so we don't do
		 * another full signing next time.
		 */
		DNS_ZONEKEY_CLROPTION(zone, DNS_ZONEKEY_FULLSIGN);

		/*
		 * Cause the zone to add/delete NSEC3 chains for the
		 * deferred NSEC3PARAM changes.
		 */
		for (tuple = ISC_LIST_HEAD(zonediff.diff->tuples);
		     tuple != NULL; tuple = ISC_LIST_NEXT(tuple, link))
		{
			unsigned char buf[DNS_NSEC3PARAM_BUFFERSIZE];
			dns_rdata_t rdata = DNS_RDATA_INIT;
			dns_rdata_nsec3param_t nsec3param;

			if (tuple->rdata.type != zone->privatetype ||
			    tuple->op != DNS_DIFFOP_ADD)
			{
				continue;
			}

			if (!dns_nsec3param_fromprivate(&tuple->rdata, &rdata,
							buf, sizeof(buf)))
			{
				continue;
			}

			result = dns_rdata_tostruct(&rdata, &nsec3param, NULL);
			RUNTIME_CHECK(result == ISC_R_SUCCESS);
			if (nsec3param.flags == 0) {
				continue;
			}

			result = zone_addnsec3chain(zone, &nsec3param);
			if (result != ISC_R_SUCCESS) {
				dnssec_log(zone, ISC_LOG_ERROR,
					   "zone_addnsec3chain failed: %s",
					   isc_result_totext(result));
			}
		}

		/*
		 * Activate any NSEC3 chain updates that may have
		 * been scheduled before this rekey.
		 */
		if (fullsign || newalg) {
			resume_addnsec3chain(zone);
		}

		/*
		 * Schedule the next resigning event
		 */
		set_resigntime(zone);
	}

	isc_time_settoepoch(&zone->refreshkeytime);

	/*
	 * If keymgr provided a next time, use the calculated next rekey time.
	 */
	if (kasp != NULL) {
		isc_time_t timenext;
		uint32_t nexttime_seconds;

		/*
		 * Set the key refresh timer to the next scheduled key event
		 * or to 'dnssec-loadkeys-interval' seconds in the future
		 * if no next key event is scheduled (nexttime == 0).
		 */
		if (nexttime > 0) {
			nexttime_seconds = nexttime - now;
		} else {
			nexttime_seconds = zone->refreshkeyinterval;
		}

		DNS_ZONE_TIME_ADD(&timenow, nexttime_seconds, &timenext);
		zone->refreshkeytime = timenext;
		zone_settimer(zone, &timenow);
		isc_time_formattimestamp(&zone->refreshkeytime, timebuf, 80);

		dnssec_log(zone, ISC_LOG_DEBUG(3),
			   "next key event in %u seconds", nexttime_seconds);
		dnssec_log(zone, ISC_LOG_INFO, "next key event: %s", timebuf);
	}
	/*
	 * If we're doing key maintenance, set the key refresh timer to
	 * the next scheduled key event or to 'dnssec-loadkeys-interval'
	 * seconds in the future, whichever is sooner.
	 */
	else if (DNS_ZONEKEY_OPTION(zone, DNS_ZONEKEY_MAINTAIN))
	{
		isc_time_t timethen;
		isc_stdtime_t then;

		DNS_ZONE_TIME_ADD(&timenow, zone->refreshkeyinterval,
				  &timethen);
		zone->refreshkeytime = timethen;

		for (key = ISC_LIST_HEAD(dnskeys); key != NULL;
		     key = ISC_LIST_NEXT(key, link))
		{
			then = now;
			result = next_keyevent(key->key, &then);
			if (result != ISC_R_SUCCESS) {
				continue;
			}

			DNS_ZONE_TIME_ADD(&timenow, then - now, &timethen);
			if (isc_time_compare(&timethen, &zone->refreshkeytime) <
			    0)
			{
				zone->refreshkeytime = timethen;
			}
		}

		zone_settimer(zone, &timenow);

		isc_time_formattimestamp(&zone->refreshkeytime, timebuf, 80);
		dnssec_log(zone, ISC_LOG_INFO, "next key event: %s", timebuf);
	}
	UNLOCK_ZONE(zone);

	/*
	 * Remember which keys have been used.
	 */
	if (!ISC_LIST_EMPTY(zone->keyring)) {
		clear_keylist(&zone->keyring, zone->mctx);
	}
	while ((key = ISC_LIST_HEAD(dnskeys)) != NULL) {
		if (isc_log_wouldlog(dns_lctx, ISC_LOG_DEBUG(3))) {
			/* This debug log is used in the kasp system test */
			char algbuf[DNS_SECALG_FORMATSIZE];
			dns_secalg_format(dst_key_alg(key->key), algbuf,
					  sizeof(algbuf));
			dnssec_log(zone, ISC_LOG_DEBUG(3),
				   "zone_rekey done: key %d/%s",
				   dst_key_id(key->key), algbuf);
		}
		ISC_LIST_UNLINK(dnskeys, key, link);
		ISC_LIST_APPEND(zone->keyring, key, link);
	}

	result = ISC_R_SUCCESS;

failure:
	LOCK_ZONE(zone);
	if (result != ISC_R_SUCCESS) {
		/*
		 * Something went wrong; try again in ten minutes or
		 * after a key refresh interval, whichever is shorter.
		 */
		int loglevel = ISC_LOG_DEBUG(3);
		if (result != DNS_R_NOTLOADED) {
			loglevel = ISC_LOG_ERROR;
		}
		dnssec_log(zone, loglevel,
			   "zone_rekey failure: %s (retry in %u seconds)",
			   isc_result_totext(result),
			   ISC_MIN(zone->refreshkeyinterval, 600));
		isc_interval_set(&ival, ISC_MIN(zone->refreshkeyinterval, 600),
				 0);
		isc_time_nowplusinterval(&zone->refreshkeytime, &ival);
	}
	UNLOCK_ZONE(zone);

	dns_diff_clear(&diff);
	dns_diff_clear(&_sig_diff);

	clear_keylist(&dnskeys, mctx);
	clear_keylist(&keys, mctx);
	clear_keylist(&rmkeys, mctx);

	if (ver != NULL) {
		dns_db_closeversion(db, &ver, false);
	}
	if (dns_rdataset_isassociated(&cdsset)) {
		dns_rdataset_disassociate(&cdsset);
	}
	if (dns_rdataset_isassociated(&keyset)) {
		dns_rdataset_disassociate(&keyset);
	}
	if (dns_rdataset_isassociated(&keysigs)) {
		dns_rdataset_disassociate(&keysigs);
	}
	if (dns_rdataset_isassociated(&soasigs)) {
		dns_rdataset_disassociate(&soasigs);
	}
	if (dns_rdataset_isassociated(&cdnskeyset)) {
		dns_rdataset_disassociate(&cdnskeyset);
	}
	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}
	if (db != NULL) {
		dns_db_detach(&db);
	}

	INSIST(ver == NULL);
}

void
dns_zone_rekey(dns_zone_t *zone, bool fullsign) {
	isc_time_t now;

	if (zone->type == dns_zone_primary && zone->loop != NULL) {
		LOCK_ZONE(zone);

		if (fullsign) {
			DNS_ZONEKEY_SETOPTION(zone, DNS_ZONEKEY_FULLSIGN);
		}

		now = isc_time_now();
		zone->refreshkeytime = now;
		zone_settimer(zone, &now);

		UNLOCK_ZONE(zone);
	}
}

isc_result_t
dns_zone_nscheck(dns_zone_t *zone, dns_db_t *db, dns_dbversion_t *version,
		 unsigned int *errors) {
	isc_result_t result;
	dns_dbnode_t *node = NULL;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(errors != NULL);

	result = dns_db_getoriginnode(db, &node);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	result = zone_count_ns_rr(zone, db, node, version, NULL, errors, false);
	dns_db_detachnode(db, &node);
	return result;
}

isc_result_t
dns_zone_cdscheck(dns_zone_t *zone, dns_db_t *db, dns_dbversion_t *version) {
	isc_result_t result;
	dns_dbnode_t *node = NULL;
	dns_rdataset_t dnskey, cds, cdnskey;
	unsigned char algorithms[256];
	unsigned int i;
	bool empty = false;

	enum { notexpected = 0, expected = 1, found = 2 };

	REQUIRE(DNS_ZONE_VALID(zone));

	result = dns_db_getoriginnode(db, &node);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	dns_rdataset_init(&cds);
	dns_rdataset_init(&dnskey);
	dns_rdataset_init(&cdnskey);

	result = dns_db_findrdataset(db, node, version, dns_rdatatype_cds,
				     dns_rdatatype_none, 0, &cds, NULL);
	if (result != ISC_R_SUCCESS && result != ISC_R_NOTFOUND) {
		goto failure;
	}

	result = dns_db_findrdataset(db, node, version, dns_rdatatype_cdnskey,
				     dns_rdatatype_none, 0, &cdnskey, NULL);
	if (result != ISC_R_SUCCESS && result != ISC_R_NOTFOUND) {
		goto failure;
	}

	if (!dns_rdataset_isassociated(&cds) &&
	    !dns_rdataset_isassociated(&cdnskey))
	{
		result = ISC_R_SUCCESS;
		goto failure;
	}

	result = dns_db_findrdataset(db, node, version, dns_rdatatype_dnskey,
				     dns_rdatatype_none, 0, &dnskey, NULL);
	if (result == ISC_R_NOTFOUND) {
		empty = true;
	} else if (result != ISC_R_SUCCESS) {
		goto failure;
	}

	/*
	 * For each DNSSEC algorithm in the CDS RRset there must be
	 * a matching DNSKEY record with the exception of a CDS deletion
	 * record which must be by itself.
	 */
	if (dns_rdataset_isassociated(&cds)) {
		bool delete = false;
		memset(algorithms, notexpected, sizeof(algorithms));
		for (result = dns_rdataset_first(&cds); result == ISC_R_SUCCESS;
		     result = dns_rdataset_next(&cds))
		{
			dns_rdata_t crdata = DNS_RDATA_INIT;
			dns_rdata_cds_t structcds;

			dns_rdataset_current(&cds, &crdata);
			/*
			 * CDS deletion record has this form "0 0 0 00" which
			 * is 5 zero octets.
			 */
			if (crdata.length == 5U &&
			    memcmp(crdata.data,
				   (unsigned char[5]){ 0, 0, 0, 0, 0 }, 5) == 0)
			{
				delete = true;
				continue;
			}

			if (empty) {
				result = DNS_R_BADCDS;
				goto failure;
			}

			CHECK(dns_rdata_tostruct(&crdata, &structcds, NULL));
			if (algorithms[structcds.algorithm] == 0) {
				algorithms[structcds.algorithm] = expected;
			}
			for (result = dns_rdataset_first(&dnskey);
			     result == ISC_R_SUCCESS;
			     result = dns_rdataset_next(&dnskey))
			{
				dns_rdata_t rdata = DNS_RDATA_INIT;
				dns_rdata_dnskey_t structdnskey;

				dns_rdataset_current(&dnskey, &rdata);
				CHECK(dns_rdata_tostruct(&rdata, &structdnskey,
							 NULL));

				if (structdnskey.algorithm ==
				    structcds.algorithm)
				{
					algorithms[structcds.algorithm] = found;
				}
			}
			if (result != ISC_R_NOMORE) {
				goto failure;
			}
		}
		for (i = 0; i < sizeof(algorithms); i++) {
			if (delete) {
				if (algorithms[i] != notexpected) {
					result = DNS_R_BADCDS;
					goto failure;
				}
			} else if (algorithms[i] == expected) {
				result = DNS_R_BADCDS;
				goto failure;
			}
		}
	}

	/*
	 * For each DNSSEC algorithm in the CDNSKEY RRset there must be
	 * a matching DNSKEY record with the exception of a CDNSKEY deletion
	 * record which must be by itself.
	 */
	if (dns_rdataset_isassociated(&cdnskey)) {
		bool delete = false;
		memset(algorithms, notexpected, sizeof(algorithms));
		for (result = dns_rdataset_first(&cdnskey);
		     result == ISC_R_SUCCESS;
		     result = dns_rdataset_next(&cdnskey))
		{
			dns_rdata_t crdata = DNS_RDATA_INIT;
			dns_rdata_cdnskey_t structcdnskey;

			dns_rdataset_current(&cdnskey, &crdata);
			/*
			 * CDNSKEY deletion record has this form
			 * "0 3 0 AA==" which is 2 zero octets, a 3,
			 * and 2 zero octets.
			 */
			if (crdata.length == 5U &&
			    memcmp(crdata.data,
				   (unsigned char[5]){ 0, 0, 3, 0, 0 }, 5) == 0)
			{
				delete = true;
				continue;
			}

			if (empty) {
				result = DNS_R_BADCDNSKEY;
				goto failure;
			}

			CHECK(dns_rdata_tostruct(&crdata, &structcdnskey,
						 NULL));
			if (algorithms[structcdnskey.algorithm] == 0) {
				algorithms[structcdnskey.algorithm] = expected;
			}
			for (result = dns_rdataset_first(&dnskey);
			     result == ISC_R_SUCCESS;
			     result = dns_rdataset_next(&dnskey))
			{
				dns_rdata_t rdata = DNS_RDATA_INIT;
				dns_rdata_dnskey_t structdnskey;

				dns_rdataset_current(&dnskey, &rdata);
				CHECK(dns_rdata_tostruct(&rdata, &structdnskey,
							 NULL));

				if (structdnskey.algorithm ==
				    structcdnskey.algorithm)
				{
					algorithms[structcdnskey.algorithm] =
						found;
				}
			}
			if (result != ISC_R_NOMORE) {
				goto failure;
			}
		}
		for (i = 0; i < sizeof(algorithms); i++) {
			if (delete) {
				if (algorithms[i] != notexpected) {
					result = DNS_R_BADCDNSKEY;
					goto failure;
				}
			} else if (algorithms[i] == expected) {
				result = DNS_R_BADCDNSKEY;
				goto failure;
			}
		}
	}
	result = ISC_R_SUCCESS;

failure:
	if (dns_rdataset_isassociated(&cds)) {
		dns_rdataset_disassociate(&cds);
	}
	if (dns_rdataset_isassociated(&dnskey)) {
		dns_rdataset_disassociate(&dnskey);
	}
	if (dns_rdataset_isassociated(&cdnskey)) {
		dns_rdataset_disassociate(&cdnskey);
	}
	dns_db_detachnode(db, &node);
	return result;
}

void
dns_zone_setautomatic(dns_zone_t *zone, bool automatic) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	zone->automatic = automatic;
	UNLOCK_ZONE(zone);
}

bool
dns_zone_getautomatic(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));
	return zone->automatic;
}

void
dns_zone_setadded(dns_zone_t *zone, bool added) {
	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);
	zone->added = added;
	UNLOCK_ZONE(zone);
}

bool
dns_zone_getadded(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));
	return zone->added;
}

isc_result_t
dns_zone_dlzpostload(dns_zone_t *zone, dns_db_t *db) {
	isc_time_t loadtime;
	isc_result_t result;
	dns_zone_t *secure = NULL;

	loadtime = isc_time_now();

	/*
	 * Lock hierarchy: zmgr, zone, raw.
	 */
again:
	LOCK_ZONE(zone);
	INSIST(zone != zone->raw);
	if (inline_secure(zone)) {
		LOCK_ZONE(zone->raw);
	} else if (inline_raw(zone)) {
		secure = zone->secure;
		TRYLOCK_ZONE(result, secure);
		if (result != ISC_R_SUCCESS) {
			UNLOCK_ZONE(zone);
			secure = NULL;
			isc_thread_yield();
			goto again;
		}
	}
	result = zone_postload(zone, db, loadtime, ISC_R_SUCCESS);
	if (inline_secure(zone)) {
		UNLOCK_ZONE(zone->raw);
	} else if (secure != NULL) {
		UNLOCK_ZONE(secure);
	}
	UNLOCK_ZONE(zone);
	return result;
}

isc_result_t
dns_zone_setrefreshkeyinterval(dns_zone_t *zone, uint32_t interval) {
	REQUIRE(DNS_ZONE_VALID(zone));
	if (interval == 0) {
		return ISC_R_RANGE;
	}
	/* Maximum value: 24 hours (3600 minutes) */
	if (interval > (24 * 60)) {
		interval = (24 * 60);
	}
	/* Multiply by 60 for seconds */
	zone->refreshkeyinterval = interval * 60;
	return ISC_R_SUCCESS;
}

void
dns_zone_setrequestixfr(dns_zone_t *zone, bool flag) {
	REQUIRE(DNS_ZONE_VALID(zone));
	zone->requestixfr = flag;
}

bool
dns_zone_getrequestixfr(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));
	return zone->requestixfr;
}

void
dns_zone_setixfrratio(dns_zone_t *zone, uint32_t ratio) {
	REQUIRE(DNS_ZONE_VALID(zone));
	zone->ixfr_ratio = ratio;
}

uint32_t
dns_zone_getixfrratio(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));
	return zone->ixfr_ratio;
}

void
dns_zone_setrequestexpire(dns_zone_t *zone, bool flag) {
	REQUIRE(DNS_ZONE_VALID(zone));
	zone->requestexpire = flag;
}

bool
dns_zone_getrequestexpire(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));
	return zone->requestexpire;
}

void
dns_zone_setserialupdatemethod(dns_zone_t *zone, dns_updatemethod_t method) {
	REQUIRE(DNS_ZONE_VALID(zone));
	zone->updatemethod = method;
}

dns_updatemethod_t
dns_zone_getserialupdatemethod(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));
	return zone->updatemethod;
}

/*
 * Lock hierarchy: zmgr, zone, raw.
 */
isc_result_t
dns_zone_link(dns_zone_t *zone, dns_zone_t *raw) {
	dns_zonemgr_t *zmgr;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(zone->zmgr != NULL);
	REQUIRE(zone->loop != NULL);
	REQUIRE(zone->raw == NULL);

	REQUIRE(DNS_ZONE_VALID(raw));
	REQUIRE(raw->zmgr == NULL);
	REQUIRE(raw->loop == NULL);
	REQUIRE(raw->secure == NULL);

	REQUIRE(zone != raw);

	/*
	 * Lock hierarchy: zmgr, zone, raw.
	 */
	zmgr = zone->zmgr;
	RWLOCK(&zmgr->rwlock, isc_rwlocktype_write);
	LOCK_ZONE(zone);
	LOCK_ZONE(raw);

	isc_loop_attach(zone->loop, &raw->loop);

	/* dns_zone_attach(raw, &zone->raw); */
	isc_refcount_increment(&raw->references);
	zone->raw = raw;

	/* dns_zone_iattach(zone, &raw->secure); */
	zone_iattach(zone, &raw->secure);

	ISC_LIST_APPEND(zmgr->zones, raw, link);
	raw->zmgr = zmgr;
	isc_refcount_increment(&zmgr->refs);

	UNLOCK_ZONE(raw);
	UNLOCK_ZONE(zone);
	RWUNLOCK(&zmgr->rwlock, isc_rwlocktype_write);
	return ISC_R_SUCCESS;
}

void
dns_zone_getraw(dns_zone_t *zone, dns_zone_t **raw) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(raw != NULL && *raw == NULL);

	LOCK(&zone->lock);
	INSIST(zone != zone->raw);
	if (zone->raw != NULL) {
		dns_zone_attach(zone->raw, raw);
	}
	UNLOCK(&zone->lock);
}

bool
dns_zone_israw(dns_zone_t *zone) {
	bool israw;
	REQUIRE(DNS_ZONE_VALID(zone));
	LOCK(&zone->lock);
	israw = zone->secure != NULL;
	UNLOCK(&zone->lock);
	return israw;
}

bool
dns_zone_issecure(dns_zone_t *zone) {
	bool issecure;
	REQUIRE(DNS_ZONE_VALID(zone));
	LOCK(&zone->lock);
	issecure = zone->raw != NULL;
	UNLOCK(&zone->lock);
	return issecure;
}

struct keydone {
	bool all;
	unsigned char data[5];
	dns_zone_t *zone;
};

#define PENDINGFLAGS (DNS_NSEC3FLAG_CREATE | DNS_NSEC3FLAG_INITIAL)

static void
keydone(void *arg) {
	bool commit = false;
	isc_result_t result;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	dns_dbversion_t *oldver = NULL, *newver = NULL;
	dns_db_t *db = NULL;
	dns_dbnode_t *node = NULL;
	dns_rdataset_t rdataset;
	dns_diff_t diff;
	struct keydone *kd = (struct keydone *)arg;
	dns_zone_t *zone = kd->zone;
	dns_update_log_t log = { update_log_cb, NULL };
	bool clear_pending = false;

	INSIST(DNS_ZONE_VALID(zone));

	ENTER;

	dns_rdataset_init(&rdataset);
	dns_diff_init(zone->mctx, &diff);

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	if (zone->db != NULL) {
		dns_db_attach(zone->db, &db);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
	if (db == NULL) {
		goto failure;
	}

	dns_db_currentversion(db, &oldver);
	result = dns_db_newversion(db, &newver);
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR,
			   "keydone:dns_db_newversion -> %s",
			   isc_result_totext(result));
		goto failure;
	}

	result = dns_db_getoriginnode(db, &node);
	if (result != ISC_R_SUCCESS) {
		goto failure;
	}

	result = dns_db_findrdataset(db, node, newver, zone->privatetype,
				     dns_rdatatype_none, 0, &rdataset, NULL);
	if (result == ISC_R_NOTFOUND) {
		INSIST(!dns_rdataset_isassociated(&rdataset));
		goto failure;
	}
	if (result != ISC_R_SUCCESS) {
		INSIST(!dns_rdataset_isassociated(&rdataset));
		goto failure;
	}

	for (result = dns_rdataset_first(&rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&rdataset))
	{
		bool found = false;

		dns_rdataset_current(&rdataset, &rdata);

		if (kd->all) {
			if (rdata.length == 5 && rdata.data[0] != 0 &&
			    rdata.data[3] == 0 && rdata.data[4] == 1)
			{
				found = true;
			} else if (rdata.data[0] == 0 &&
				   (rdata.data[2] & PENDINGFLAGS) != 0)
			{
				found = true;
				clear_pending = true;
			}
		} else if (rdata.length == 5 &&
			   memcmp(rdata.data, kd->data, 5) == 0)
		{
			found = true;
		}

		if (found) {
			CHECK(update_one_rr(db, newver, &diff, DNS_DIFFOP_DEL,
					    &zone->origin, rdataset.ttl,
					    &rdata));
		}
		dns_rdata_reset(&rdata);
	}

	if (!ISC_LIST_EMPTY(diff.tuples)) {
		/* Write changes to journal file. */
		CHECK(update_soa_serial(zone, db, newver, &diff, zone->mctx,
					zone->updatemethod));

		result = dns_update_signatures(&log, zone, db, oldver, newver,
					       &diff,
					       zone->sigvalidityinterval);
		if (!clear_pending) {
			CHECK(result);
		}

		CHECK(zone_journal(zone, &diff, NULL, "keydone"));
		commit = true;

		LOCK_ZONE(zone);
		DNS_ZONE_SETFLAG(zone,
				 DNS_ZONEFLG_LOADED | DNS_ZONEFLG_NEEDNOTIFY);
		zone_needdump(zone, 30);
		UNLOCK_ZONE(zone);
	}

failure:
	if (dns_rdataset_isassociated(&rdataset)) {
		dns_rdataset_disassociate(&rdataset);
	}
	if (db != NULL) {
		if (node != NULL) {
			dns_db_detachnode(db, &node);
		}
		if (oldver != NULL) {
			dns_db_closeversion(db, &oldver, false);
		}
		if (newver != NULL) {
			dns_db_closeversion(db, &newver, commit);
		}
		dns_db_detach(&db);
	}
	dns_diff_clear(&diff);
	isc_mem_put(zone->mctx, kd, sizeof(*kd));
	dns_zone_idetach(&zone);

	INSIST(oldver == NULL);
	INSIST(newver == NULL);
}

isc_result_t
dns_zone_keydone(dns_zone_t *zone, const char *keystr) {
	isc_result_t result = ISC_R_SUCCESS;
	struct keydone *kd = NULL;
	isc_buffer_t b;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);

	kd = isc_mem_get(zone->mctx, sizeof(*kd));
	*kd = (struct keydone){ .all = false };

	if (strcasecmp(keystr, "all") == 0) {
		kd->all = true;
	} else {
		isc_textregion_t r;
		const char *algstr = NULL;
		dns_keytag_t keyid;
		dns_secalg_t alg;
		size_t n;

		n = sscanf(keystr, "%hu/", &keyid);
		if (n == 0U) {
			CHECK(ISC_R_FAILURE);
		}

		algstr = strchr(keystr, '/');
		if (algstr != NULL) {
			algstr++;
		} else {
			CHECK(ISC_R_FAILURE);
		}

		n = sscanf(algstr, "%hhu", &alg);
		if (n == 0U) {
			r.base = UNCONST(algstr);
			r.length = strlen(algstr);
			CHECK(dns_secalg_fromtext(&alg, &r));
		}

		/* construct a private-type rdata */
		isc_buffer_init(&b, kd->data, sizeof(kd->data));
		isc_buffer_putuint8(&b, alg);
		isc_buffer_putuint8(&b, (keyid & 0xff00) >> 8);
		isc_buffer_putuint8(&b, keyid & 0xff);
		isc_buffer_putuint8(&b, 0);
		isc_buffer_putuint8(&b, 1);
	}

	zone_iattach(zone, &kd->zone);
	isc_async_run(zone->loop, keydone, kd);
	kd = NULL;

failure:
	if (kd != NULL) {
		isc_mem_put(zone->mctx, kd, sizeof(*kd));
	}
	UNLOCK_ZONE(zone);
	return result;
}

/*
 * Called from the zone loop's queue after the relevant event is posted by
 * dns_zone_setnsec3param().
 */
static void
setnsec3param(void *arg) {
	struct np3 *npe = (struct np3 *)arg;
	dns_zone_t *zone = npe->zone;
	bool loadpending;

	INSIST(DNS_ZONE_VALID(zone));

	ENTER;

	LOCK_ZONE(zone);
	loadpending = DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADPENDING);
	UNLOCK_ZONE(zone);

	/*
	 * The receive_secure_serial() and setnsec3param() calls are
	 * loop-serialized for the zone. Make sure there's no processing
	 * currently running.
	 */
	INSIST(zone->rss_newver == NULL);

	bool rescheduled = false;
	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	/*
	 * The zone is not yet fully loaded. Reschedule the event to
	 * be picked up later. This turns this function into a busy
	 * wait, but it only happens at startup.
	 */
	if (zone->db == NULL && loadpending) {
		rescheduled = true;
		isc_async_run(zone->loop, setnsec3param, npe);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
	if (rescheduled) {
		return;
	}

	rss_post(npe);

	dns_zone_idetach(&zone);
}

static void
salt2text(unsigned char *salt, uint8_t saltlen, unsigned char *text,
	  unsigned int textlen) {
	isc_region_t r;
	isc_buffer_t buf;
	isc_result_t result;

	r.base = salt;
	r.length = (unsigned int)saltlen;

	isc_buffer_init(&buf, text, textlen);
	result = isc_hex_totext(&r, 2, "", &buf);
	if (result == ISC_R_SUCCESS) {
		text[saltlen * 2] = 0;
	} else {
		text[0] = 0;
	}
}

/*
 * Check whether NSEC3 chain addition or removal specified by the private-type
 * record passed with the event was already queued (or even fully performed).
 * If not, modify the relevant private-type records at the zone apex and call
 * resume_addnsec3chain().
 */
static void
rss_post(void *arg) {
	struct np3 *npe = (struct np3 *)arg;
	dns_zone_t *zone = npe->zone;
	nsec3param_t *np = &npe->params;
	bool commit = false;
	isc_result_t result;
	dns_dbversion_t *oldver = NULL, *newver = NULL;
	dns_db_t *db = NULL;
	dns_dbnode_t *node = NULL;
	dns_rdataset_t prdataset, nrdataset;
	dns_diff_t diff;
	dns_update_log_t log = { update_log_cb, NULL };
	dns_rdata_t rdata;
	bool nseconly;
	bool exists = false;

	ENTER;

	dns_rdataset_init(&prdataset);
	dns_rdataset_init(&nrdataset);
	dns_diff_init(zone->mctx, &diff);

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	if (zone->db != NULL) {
		dns_db_attach(zone->db, &db);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
	if (db == NULL) {
		goto failure;
	}

	dns_db_currentversion(db, &oldver);
	result = dns_db_newversion(db, &newver);
	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR,
			   "setnsec3param:dns_db_newversion -> %s",
			   isc_result_totext(result));
		goto failure;
	}

	CHECK(dns_db_getoriginnode(db, &node));

	/*
	 * Do we need to look up the NSEC3 parameters?
	 */
	if (np->lookup) {
		dns_rdata_nsec3param_t param;
		dns_rdata_t nrdata = DNS_RDATA_INIT;
		dns_rdata_t prdata = DNS_RDATA_INIT;
		unsigned char nbuf[DNS_NSEC3PARAM_BUFFERSIZE];
		unsigned char saltbuf[255];
		isc_buffer_t b;

		param.salt = NULL;
		result = dns__zone_lookup_nsec3param(zone, &np->rdata, &param,
						     saltbuf, np->resalt);
		if (result == ISC_R_SUCCESS) {
			/*
			 * Success because the NSEC3PARAM already exists, but
			 * function returns void, so goto failure to clean up.
			 */
			goto failure;
		}
		if (result != DNS_R_NSEC3RESALT && result != ISC_R_NOTFOUND) {
			dnssec_log(zone, ISC_LOG_DEBUG(3),
				   "setnsec3param:lookup nsec3param -> %s",
				   isc_result_totext(result));
			goto failure;
		}

		INSIST(param.salt != NULL);

		/* Update NSEC3 parameters. */
		np->rdata.hash = param.hash;
		np->rdata.flags = param.flags;
		np->rdata.iterations = param.iterations;
		np->rdata.salt_length = param.salt_length;
		np->rdata.salt = param.salt;

		isc_buffer_init(&b, nbuf, sizeof(nbuf));
		CHECK(dns_rdata_fromstruct(&nrdata, zone->rdclass,
					   dns_rdatatype_nsec3param, &np->rdata,
					   &b));
		dns_nsec3param_toprivate(&nrdata, &prdata, zone->privatetype,
					 np->data, sizeof(np->data));
		np->length = prdata.length;
		np->nsec = false;
	}

	/*
	 * Does a private-type record already exist for this chain?
	 */
	result = dns_db_findrdataset(db, node, newver, zone->privatetype,
				     dns_rdatatype_none, 0, &prdataset, NULL);
	if (result == ISC_R_SUCCESS) {
		for (result = dns_rdataset_first(&prdataset);
		     result == ISC_R_SUCCESS;
		     result = dns_rdataset_next(&prdataset))
		{
			dns_rdata_init(&rdata);
			dns_rdataset_current(&prdataset, &rdata);

			if (np->length == rdata.length &&
			    memcmp(rdata.data, np->data, np->length) == 0)
			{
				exists = true;
				break;
			}
		}
	} else if (result != ISC_R_NOTFOUND) {
		INSIST(!dns_rdataset_isassociated(&prdataset));
		goto failure;
	}

	/*
	 * Does the chain already exist?
	 */
	result = dns_db_findrdataset(db, node, newver, dns_rdatatype_nsec3param,
				     dns_rdatatype_none, 0, &nrdataset, NULL);
	if (result == ISC_R_SUCCESS) {
		for (result = dns_rdataset_first(&nrdataset);
		     result == ISC_R_SUCCESS;
		     result = dns_rdataset_next(&nrdataset))
		{
			dns_rdata_init(&rdata);
			dns_rdataset_current(&nrdataset, &rdata);

			if (np->length == ((unsigned int)rdata.length + 1) &&
			    memcmp(rdata.data, np->data + 1, np->length - 1) ==
				    0)
			{
				exists = true;
				break;
			}
		}
	} else if (result != ISC_R_NOTFOUND) {
		INSIST(!dns_rdataset_isassociated(&nrdataset));
		goto failure;
	}

	/*
	 * We need to remove any existing NSEC3 chains if the supplied NSEC3
	 * parameters are supposed to replace the current ones or if we are
	 * switching to NSEC.
	 */
	if (!exists && np->replace && (np->length != 0 || np->nsec)) {
		CHECK(dns_nsec3param_deletechains(db, newver, zone, !np->nsec,
						  &diff));
	}

	if (!exists && np->length != 0) {
		/*
		 * We're creating an NSEC3 chain.  Add the private-type record
		 * passed in the event handler's argument to the zone apex.
		 *
		 * If the zone is not currently capable of supporting an NSEC3
		 * chain (due to the DNSKEY RRset at the zone apex not existing
		 * or containing at least one key using an NSEC-only
		 * algorithm), add the INITIAL flag, so these parameters can be
		 * used later when NSEC3 becomes available.
		 */
		dns_rdata_init(&rdata);

		np->data[2] |= DNS_NSEC3FLAG_CREATE;
		result = dns_nsec_nseconly(db, newver, NULL, &nseconly);
		if (result == ISC_R_NOTFOUND || nseconly) {
			np->data[2] |= DNS_NSEC3FLAG_INITIAL;
		}

		rdata.length = np->length;
		rdata.data = np->data;
		rdata.type = zone->privatetype;
		rdata.rdclass = zone->rdclass;
		CHECK(update_one_rr(db, newver, &diff, DNS_DIFFOP_ADD,
				    &zone->origin, 0, &rdata));
	}

	/*
	 * If we changed anything in the zone, write changes to journal file
	 * and set commit to true so that resume_addnsec3chain() will be
	 * called below in order to kick off adding/removing relevant NSEC3
	 * records.
	 */
	if (!ISC_LIST_EMPTY(diff.tuples)) {
		CHECK(update_soa_serial(zone, db, newver, &diff, zone->mctx,
					zone->updatemethod));
		result = dns_update_signatures(&log, zone, db, oldver, newver,
					       &diff,
					       zone->sigvalidityinterval);
		if (result != ISC_R_NOTFOUND) {
			CHECK(result);
		}
		CHECK(zone_journal(zone, &diff, NULL, "setnsec3param"));
		commit = true;

		LOCK_ZONE(zone);
		DNS_ZONE_SETFLAG(zone, DNS_ZONEFLG_LOADED);
		zone_needdump(zone, 30);
		UNLOCK_ZONE(zone);
	}

failure:
	if (dns_rdataset_isassociated(&prdataset)) {
		dns_rdataset_disassociate(&prdataset);
	}
	if (dns_rdataset_isassociated(&nrdataset)) {
		dns_rdataset_disassociate(&nrdataset);
	}
	if (node != NULL) {
		dns_db_detachnode(db, &node);
	}
	if (oldver != NULL) {
		dns_db_closeversion(db, &oldver, false);
	}
	if (newver != NULL) {
		dns_db_closeversion(db, &newver, commit);
	}
	if (db != NULL) {
		dns_db_detach(&db);
	}
	if (commit) {
		LOCK_ZONE(zone);
		resume_addnsec3chain(zone);
		UNLOCK_ZONE(zone);
	}
	dns_diff_clear(&diff);
	isc_mem_put(zone->mctx, npe, sizeof(*npe));

	INSIST(oldver == NULL);
	INSIST(newver == NULL);
}

/*
 * Check if zone has NSEC3PARAM (and thus a chain) with the right parameters.
 *
 * If 'salt' is NULL, a match is found if the salt has the requested length,
 * otherwise the NSEC3 salt must match the requested salt value too.
 *
 * Returns  ISC_R_SUCCESS, if a match is found, or an error if no match is
 * found, or if the db lookup failed.
 */
isc_result_t
dns__zone_lookup_nsec3param(dns_zone_t *zone, dns_rdata_nsec3param_t *lookup,
			    dns_rdata_nsec3param_t *param,
			    unsigned char saltbuf[255], bool resalt) {
	isc_result_t result = ISC_R_UNEXPECTED;
	dns_dbnode_t *node = NULL;
	dns_db_t *db = NULL;
	dns_dbversion_t *version = NULL;
	dns_rdataset_t rdataset;
	dns_rdata_nsec3param_t nsec3param;
	dns_rdata_t rdata = DNS_RDATA_INIT;

	REQUIRE(DNS_ZONE_VALID(zone));

	dns_rdataset_init(&rdataset);

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	if (zone->db != NULL) {
		dns_db_attach(zone->db, &db);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
	if (db == NULL) {
		result = ISC_R_FAILURE;
		goto setparam;
	}

	result = dns_db_findnode(db, &zone->origin, false, &node);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "dns__zone_lookup_nsec3param:"
			     "dns_db_findnode -> %s",
			     isc_result_totext(result));
		result = ISC_R_FAILURE;
		goto setparam;
	}
	dns_db_currentversion(db, &version);

	result = dns_db_findrdataset(db, node, version,
				     dns_rdatatype_nsec3param,
				     dns_rdatatype_none, 0, &rdataset, NULL);
	if (result != ISC_R_SUCCESS) {
		INSIST(!dns_rdataset_isassociated(&rdataset));
		if (result != ISC_R_NOTFOUND) {
			dns_zone_log(zone, ISC_LOG_ERROR,
				     "dns__zone_lookup_nsec3param:"
				     "dns_db_findrdataset -> %s",
				     isc_result_totext(result));
		}
		goto setparam;
	}

	for (result = dns_rdataset_first(&rdataset); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(&rdataset))
	{
		dns_rdataset_current(&rdataset, &rdata);
		result = dns_rdata_tostruct(&rdata, &nsec3param, NULL);
		INSIST(result == ISC_R_SUCCESS);
		dns_rdata_reset(&rdata);

		/* Check parameters. */
		if (nsec3param.hash != lookup->hash) {
			continue;
		}
		if (nsec3param.iterations != lookup->iterations) {
			continue;
		}
		if (nsec3param.salt_length != lookup->salt_length) {
			continue;
		}
		if (lookup->salt != NULL) {
			if (memcmp(nsec3param.salt, lookup->salt,
				   lookup->salt_length) != 0)
			{
				continue;
			}
		}
		/* Found a match. */
		result = ISC_R_SUCCESS;
		param->hash = nsec3param.hash;
		param->flags = nsec3param.flags;
		param->iterations = nsec3param.iterations;
		param->salt_length = nsec3param.salt_length;
		param->salt = nsec3param.salt;
		break;
	}

	if (result == ISC_R_NOMORE) {
		result = ISC_R_NOTFOUND;
	}

setparam:
	if (result != ISC_R_SUCCESS) {
		/* Found no match. */
		param->hash = lookup->hash;
		param->flags = lookup->flags;
		param->iterations = lookup->iterations;
		param->salt_length = lookup->salt_length;
		param->salt = lookup->salt;
	}

	if (result != ISC_R_NOTFOUND && result != ISC_R_SUCCESS) {
		goto failure;
	}

	if (param->salt_length == 0) {
		param->salt = (unsigned char *)"-";
	} else if (resalt || param->salt == NULL) {
		unsigned char *newsalt;
		unsigned char salttext[255 * 2 + 1];
		do {
			/* Generate a new salt. */
			result = dns_nsec3_generate_salt(saltbuf,
							 param->salt_length);
			if (result != ISC_R_SUCCESS) {
				break;
			}
			newsalt = saltbuf;
			salt2text(newsalt, param->salt_length, salttext,
				  sizeof(salttext));
			dnssec_log(zone, ISC_LOG_INFO, "generated salt: %s",
				   salttext);
			/* Check for salt conflict. */
			if (param->salt != NULL &&
			    memcmp(newsalt, param->salt, param->salt_length) ==
				    0)
			{
				result = ISC_R_SUCCESS;
			} else {
				param->salt = newsalt;
				result = DNS_R_NSEC3RESALT;
			}
		} while (result == ISC_R_SUCCESS);

		INSIST(result != ISC_R_SUCCESS);
	}

failure:
	if (dns_rdataset_isassociated(&rdataset)) {
		dns_rdataset_disassociate(&rdataset);
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

	return result;
}

/*
 * Called when an "rndc signing -nsec3param ..." command is received, or the
 * 'dnssec-policy' has changed.
 *
 * Allocate and prepare an nsec3param_t structure which holds information about
 * the NSEC3 changes requested for the zone:
 *
 *   - if NSEC3 is to be disabled ("-nsec3param none"), only set the "nsec"
 *     field of the structure to true and the "replace" field to the value
 *     of the "replace" argument, leaving other fields initialized to zeros, to
 *     signal that the zone should be signed using NSEC instead of NSEC3,
 *
 *   - otherwise, prepare NSEC3PARAM RDATA that will eventually be inserted at
 *     the zone apex, convert it to a private-type record and store the latter
 *     in the "data" field of the nsec3param_t structure.
 *
 * Once the nsec3param_t structure is prepared, post an event to the zone's
 * loop which will cause setnsec3param() to be called with the prepared
 * structure passed as an argument.
 */
isc_result_t
dns_zone_setnsec3param(dns_zone_t *zone, uint8_t hash, uint8_t flags,
		       uint16_t iter, uint8_t saltlen, unsigned char *salt,
		       bool replace, bool resalt) {
	isc_result_t result = ISC_R_SUCCESS;
	dns_rdata_nsec3param_t param, lookup;
	dns_rdata_t nrdata = DNS_RDATA_INIT;
	dns_rdata_t prdata = DNS_RDATA_INIT;
	unsigned char nbuf[DNS_NSEC3PARAM_BUFFERSIZE];
	unsigned char saltbuf[255];
	struct np3 *npe = NULL;
	nsec3param_t *np = NULL;
	isc_buffer_t b;
	bool do_lookup = false;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);

	/*
	 * First check if the requested NSEC3 parameters are already
	 * set, if so, no need to set again.
	 */
	if (hash != 0) {
		lookup.hash = hash;
		lookup.flags = flags;
		lookup.iterations = iter;
		lookup.salt_length = saltlen;
		lookup.salt = salt;
		param.salt = NULL;
		result = dns__zone_lookup_nsec3param(zone, &lookup, &param,
						     saltbuf, resalt);
		if (result == ISC_R_SUCCESS) {
			UNLOCK_ZONE(zone);
			return ISC_R_SUCCESS;
		}
		/*
		 * Schedule lookup if lookup above failed (may happen if
		 * zone db is NULL for example).
		 */
		do_lookup = (param.salt == NULL) ? true : false;
	}

	npe = isc_mem_get(zone->mctx, sizeof(*npe));
	*npe = (struct np3){
		.link = ISC_LINK_INITIALIZER,
	};

	np = &npe->params;
	*np = (struct nsec3param){
		.replace = replace,
		.resalt = resalt,
		.lookup = do_lookup,
	};

	if (hash == 0) {
		np->nsec = true;
		dnssec_log(zone, ISC_LOG_DEBUG(3), "setnsec3param:nsec");
	} else {
		param.common.rdclass = zone->rdclass;
		param.common.rdtype = dns_rdatatype_nsec3param;
		ISC_LINK_INIT(&param.common, link);
		param.mctx = NULL;
		/*
		 * nsec3 specific param set in
		 * dns__zone_lookup_nsec3param()
		 */
		isc_buffer_init(&b, nbuf, sizeof(nbuf));

		if (param.salt != NULL) {
			CHECK(dns_rdata_fromstruct(&nrdata, zone->rdclass,
						   dns_rdatatype_nsec3param,
						   &param, &b));
			dns_nsec3param_toprivate(&nrdata, &prdata,
						 zone->privatetype, np->data,
						 sizeof(np->data));
			np->length = prdata.length;
		}

		np->rdata = param;

		if (isc_log_wouldlog(dns_lctx, ISC_LOG_DEBUG(3))) {
			unsigned char salttext[255 * 2 + 1];
			if (param.salt != NULL) {
				salt2text(param.salt, param.salt_length,
					  salttext, sizeof(salttext));
			}
			dnssec_log(zone, ISC_LOG_DEBUG(3),
				   "setnsec3param:nsec3 %u %u %u %u:%s",
				   param.hash, param.flags, param.iterations,
				   param.salt_length,
				   param.salt == NULL ? "unknown"
						      : (char *)salttext);
		}
	}

	/*
	 * setnsec3param() will silently return early if the zone does
	 * not yet have a database.  Prevent that by queueing the event
	 * up if zone->db is NULL.  All events queued here are
	 * subsequently processed by receive_secure_db() if it ever gets
	 * called or simply freed by zone_free() otherwise.
	 */

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	if (zone->db != NULL) {
		zone_iattach(zone, &npe->zone);
		isc_async_run(zone->loop, setnsec3param, npe);
	} else {
		ISC_LIST_APPEND(zone->setnsec3param_queue, npe, link);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);

	result = ISC_R_SUCCESS;

failure:
	UNLOCK_ZONE(zone);
	return result;
}

isc_result_t
dns_zone_getloadtime(dns_zone_t *zone, isc_time_t *loadtime) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(loadtime != NULL);

	LOCK_ZONE(zone);
	*loadtime = zone->loadtime;
	UNLOCK_ZONE(zone);
	return ISC_R_SUCCESS;
}

isc_result_t
dns_zone_getexpiretime(dns_zone_t *zone, isc_time_t *expiretime) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(expiretime != NULL);

	LOCK_ZONE(zone);
	*expiretime = zone->expiretime;
	UNLOCK_ZONE(zone);
	return ISC_R_SUCCESS;
}

isc_result_t
dns_zone_getrefreshtime(dns_zone_t *zone, isc_time_t *refreshtime) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(refreshtime != NULL);

	LOCK_ZONE(zone);
	*refreshtime = zone->refreshtime;
	UNLOCK_ZONE(zone);
	return ISC_R_SUCCESS;
}

isc_result_t
dns_zone_getrefreshkeytime(dns_zone_t *zone, isc_time_t *refreshkeytime) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(refreshkeytime != NULL);

	LOCK_ZONE(zone);
	*refreshkeytime = zone->refreshkeytime;
	UNLOCK_ZONE(zone);
	return ISC_R_SUCCESS;
}

unsigned int
dns_zone_getincludes(dns_zone_t *zone, char ***includesp) {
	dns_include_t *include;
	char **array = NULL;
	unsigned int n = 0;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(includesp != NULL && *includesp == NULL);

	LOCK_ZONE(zone);
	if (zone->nincludes == 0) {
		goto done;
	}

	array = isc_mem_allocate(zone->mctx, sizeof(char *) * zone->nincludes);
	for (include = ISC_LIST_HEAD(zone->includes); include != NULL;
	     include = ISC_LIST_NEXT(include, link))
	{
		INSIST(n < zone->nincludes);
		array[n++] = isc_mem_strdup(zone->mctx, include->name);
	}
	INSIST(n == zone->nincludes);
	*includesp = array;

done:
	UNLOCK_ZONE(zone);
	return n;
}

void
dns_zone_setstatlevel(dns_zone_t *zone, dns_zonestat_level_t level) {
	REQUIRE(DNS_ZONE_VALID(zone));

	zone->statlevel = level;
}

dns_zonestat_level_t
dns_zone_getstatlevel(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->statlevel;
}

static void
setserial(void *arg) {
	uint32_t oldserial, desired;
	bool commit = false;
	isc_result_t result;
	dns_dbversion_t *oldver = NULL, *newver = NULL;
	dns_db_t *db = NULL;
	dns_diff_t diff;
	struct setserial *sse = (struct setserial *)arg;
	dns_zone_t *zone = sse->zone;
	dns_update_log_t log = { update_log_cb, NULL };
	dns_difftuple_t *oldtuple = NULL, *newtuple = NULL;

	INSIST(DNS_ZONE_VALID(zone));

	ENTER;

	if (zone->update_disabled) {
		goto disabled;
	}

	desired = sse->serial;

	dns_diff_init(zone->mctx, &diff);

	ZONEDB_LOCK(&zone->dblock, isc_rwlocktype_read);
	if (zone->db != NULL) {
		dns_db_attach(zone->db, &db);
	}
	ZONEDB_UNLOCK(&zone->dblock, isc_rwlocktype_read);
	if (db == NULL) {
		goto failure;
	}

	dns_db_currentversion(db, &oldver);
	result = dns_db_newversion(db, &newver);
	if (result != ISC_R_SUCCESS) {
		dns_zone_log(zone, ISC_LOG_ERROR,
			     "setserial:dns_db_newversion -> %s",
			     isc_result_totext(result));
		goto failure;
	}

	CHECK(dns_db_createsoatuple(db, oldver, diff.mctx, DNS_DIFFOP_DEL,
				    &oldtuple));
	CHECK(dns_difftuple_copy(oldtuple, &newtuple));
	newtuple->op = DNS_DIFFOP_ADD;

	oldserial = dns_soa_getserial(&oldtuple->rdata);
	if (desired == 0U) {
		desired = 1;
	}
	if (!isc_serial_gt(desired, oldserial)) {
		if (desired != oldserial) {
			dns_zone_log(zone, ISC_LOG_INFO,
				     "setserial: desired serial (%u) "
				     "out of range (%u-%u)",
				     desired, oldserial + 1,
				     oldserial + 0x7fffffff);
		}
		goto failure;
	}

	dns_soa_setserial(desired, &newtuple->rdata);
	CHECK(do_one_tuple(&oldtuple, db, newver, &diff));
	CHECK(do_one_tuple(&newtuple, db, newver, &diff));
	result = dns_update_signatures(&log, zone, db, oldver, newver, &diff,
				       zone->sigvalidityinterval);
	if (result != ISC_R_NOTFOUND) {
		CHECK(result);
	}

	/* Write changes to journal file. */
	CHECK(zone_journal(zone, &diff, NULL, "setserial"));
	commit = true;

	LOCK_ZONE(zone);
	zone_needdump(zone, 30);
	UNLOCK_ZONE(zone);

failure:
	if (oldtuple != NULL) {
		dns_difftuple_free(&oldtuple);
	}
	if (newtuple != NULL) {
		dns_difftuple_free(&newtuple);
	}
	if (oldver != NULL) {
		dns_db_closeversion(db, &oldver, false);
	}
	if (newver != NULL) {
		dns_db_closeversion(db, &newver, commit);
	}
	if (db != NULL) {
		dns_db_detach(&db);
	}
	dns_diff_clear(&diff);

disabled:
	isc_mem_put(zone->mctx, sse, sizeof(*sse));
	dns_zone_idetach(&zone);

	INSIST(oldver == NULL);
	INSIST(newver == NULL);
}

isc_result_t
dns_zone_setserial(dns_zone_t *zone, uint32_t serial) {
	isc_result_t result = ISC_R_SUCCESS;
	struct setserial *sse = NULL;

	REQUIRE(DNS_ZONE_VALID(zone));

	LOCK_ZONE(zone);

	if (!inline_secure(zone)) {
		if (!dns_zone_isdynamic(zone, true)) {
			result = DNS_R_NOTDYNAMIC;
			goto failure;
		}
	}

	if (zone->update_disabled) {
		result = DNS_R_FROZEN;
		goto failure;
	}

	sse = isc_mem_get(zone->mctx, sizeof(*sse));
	*sse = (struct setserial){ .serial = serial };
	zone_iattach(zone, &sse->zone);
	isc_async_run(zone->loop, setserial, sse);

failure:
	UNLOCK_ZONE(zone);
	return result;
}

isc_stats_t *
dns_zone_getgluecachestats(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return zone->gluecachestats;
}

bool
dns_zone_isloaded(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return DNS_ZONE_FLAG(zone, DNS_ZONEFLG_LOADED);
}

isc_result_t
dns_zone_verifydb(dns_zone_t *zone, dns_db_t *db, dns_dbversion_t *ver) {
	dns_dbversion_t *version = NULL;
	dns_keytable_t *secroots = NULL;
	isc_result_t result;
	dns_name_t *origin;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(db != NULL);

	ENTER;

	if (dns_zone_gettype(zone) != dns_zone_mirror) {
		return ISC_R_SUCCESS;
	}

	if (ver == NULL) {
		dns_db_currentversion(db, &version);
	} else {
		version = ver;
	}

	if (zone->view != NULL) {
		result = dns_view_getsecroots(zone->view, &secroots);
		if (result != ISC_R_SUCCESS) {
			goto done;
		}
	}

	origin = dns_db_origin(db);
	result = dns_zoneverify_dnssec(zone, db, version, origin, secroots,
				       zone->mctx, true, false, dnssec_report);

done:
	if (secroots != NULL) {
		dns_keytable_detach(&secroots);
	}

	if (ver == NULL) {
		dns_db_closeversion(db, &version, false);
	}

	if (result != ISC_R_SUCCESS) {
		dnssec_log(zone, ISC_LOG_ERROR, "zone verification failed: %s",
			   isc_result_totext(result));
		result = DNS_R_VERIFYFAILURE;
	}

	return result;
}

static dns_ttl_t
zone_nsecttl(dns_zone_t *zone) {
	REQUIRE(DNS_ZONE_VALID(zone));

	return ISC_MIN(zone->minimum, zone->soattl);
}

void
dns_zonemgr_set_tlsctx_cache(dns_zonemgr_t *zmgr,
			     isc_tlsctx_cache_t *tlsctx_cache) {
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));
	REQUIRE(tlsctx_cache != NULL);

	RWLOCK(&zmgr->tlsctx_cache_rwlock, isc_rwlocktype_write);

	if (zmgr->tlsctx_cache != NULL) {
		isc_tlsctx_cache_detach(&zmgr->tlsctx_cache);
	}

	isc_tlsctx_cache_attach(tlsctx_cache, &zmgr->tlsctx_cache);

	RWUNLOCK(&zmgr->tlsctx_cache_rwlock, isc_rwlocktype_write);
}

static void
zmgr_tlsctx_attach(dns_zonemgr_t *zmgr, isc_tlsctx_cache_t **ptlsctx_cache) {
	REQUIRE(DNS_ZONEMGR_VALID(zmgr));
	REQUIRE(ptlsctx_cache != NULL && *ptlsctx_cache == NULL);

	RWLOCK(&zmgr->tlsctx_cache_rwlock, isc_rwlocktype_read);

	INSIST(zmgr->tlsctx_cache != NULL);
	isc_tlsctx_cache_attach(zmgr->tlsctx_cache, ptlsctx_cache);

	RWUNLOCK(&zmgr->tlsctx_cache_rwlock, isc_rwlocktype_read);
}

isc_mem_t *
dns_zone_getmem(dns_zone_t *zone) {
	return zone->mctx;
}

unsigned int
dns_zone_gettid(dns_zone_t *zone) {
	return zone->tid;
}

isc_loop_t *
dns_zone_getloop(dns_zone_t *zone) {
	return zone->loop;
}

isc_result_t
dns_zone_makedb(dns_zone_t *zone, dns_db_t **dbp) {
	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(dbp != NULL && *dbp == NULL);

	dns_db_t *db = NULL;

	isc_result_t result = dns_db_create(
		zone->mctx, zone->db_argv[0], &zone->origin,
		(zone->type == dns_zone_stub) ? dns_dbtype_stub
					      : dns_dbtype_zone,
		zone->rdclass, zone->db_argc - 1, zone->db_argv + 1, &db);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	switch (zone->type) {
	case dns_zone_primary:
	case dns_zone_secondary:
	case dns_zone_mirror:
		result = dns_db_setgluecachestats(db, zone->gluecachestats);
		if (result == ISC_R_NOTIMPLEMENTED) {
			result = ISC_R_SUCCESS;
		}
		if (result != ISC_R_SUCCESS) {
			dns_db_detach(&db);
			return result;
		}
		break;
	default:
		break;
	}

	dns_db_setloop(db, zone->loop);
	dns_db_setmaxrrperset(db, zone->maxrrperset);
	dns_db_setmaxtypepername(db, zone->maxtypepername);

	*dbp = db;

	return ISC_R_SUCCESS;
}

isc_result_t
dns_zone_import_skr(dns_zone_t *zone, const char *file) {
	dns_skr_t *skr = NULL;
	isc_result_t result;

	REQUIRE(DNS_ZONE_VALID(zone));
	REQUIRE(zone->kasp != NULL);
	REQUIRE(file != NULL);

	dns_skr_create(zone->mctx, file, &zone->origin, zone->rdclass, &skr);

	CHECK(dns_skr_read(zone->mctx, file, &zone->origin, zone->rdclass,
			   dns_kasp_dnskeyttl(zone->kasp), &skr));

	dns_zone_setskr(zone, skr);
	dnssec_log(zone, ISC_LOG_DEBUG(1), "imported skr file %s", file);

failure:
	dns_skr_detach(&skr);

	return result;
}
