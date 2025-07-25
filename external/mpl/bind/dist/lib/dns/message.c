/*	$NetBSD: message.c,v 1.21 2025/07/17 19:01:45 christos Exp $	*/

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

/***
 *** Imports
 ***/

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>

#include <isc/async.h>
#include <isc/buffer.h>
#include <isc/hash.h>
#include <isc/hashmap.h>
#include <isc/helper.h>
#include <isc/log.h>
#include <isc/mem.h>
#include <isc/result.h>
#include <isc/string.h>
#include <isc/utf8.h>
#include <isc/util.h>
#include <isc/work.h>

#include <dns/dnssec.h>
#include <dns/keyvalues.h>
#include <dns/log.h>
#include <dns/masterdump.h>
#include <dns/message.h>
#include <dns/opcode.h>
#include <dns/rcode.h>
#include <dns/rdata.h>
#include <dns/rdatalist.h>
#include <dns/rdataset.h>
#include <dns/rdatastruct.h>
#include <dns/soa.h>
#include <dns/tsig.h>
#include <dns/ttl.h>
#include <dns/view.h>

#ifdef SKAN_MSG_DEBUG
static void
hexdump(const char *msg, const char *msg2, void *base, size_t len) {
	unsigned char *p;
	unsigned int cnt;

	p = base;
	cnt = 0;

	printf("*** %s [%s] (%u bytes @ %p)\n", msg, msg2, (unsigned int)len,
	       base);

	while (cnt < len) {
		if (cnt % 16 == 0) {
			printf("%p: ", p);
		} else if (cnt % 8 == 0) {
			printf(" |");
		}
		printf(" %02x %c", *p, isprint(*p) ? *p : ' ');
		p++;
		cnt++;

		if (cnt % 16 == 0) {
			printf("\n");
		}
	}

	if (cnt % 16 != 0) {
		printf("\n");
	}
}
#endif /* ifdef SKAN_MSG_DEBUG */

#define DNS_MESSAGE_OPCODE_MASK	      0x7800U
#define DNS_MESSAGE_OPCODE_SHIFT      11
#define DNS_MESSAGE_RCODE_MASK	      0x000fU
#define DNS_MESSAGE_FLAG_MASK	      0x8ff0U
#define DNS_MESSAGE_EDNSRCODE_MASK    0xff000000U
#define DNS_MESSAGE_EDNSRCODE_SHIFT   24
#define DNS_MESSAGE_EDNSVERSION_MASK  0x00ff0000U
#define DNS_MESSAGE_EDNSVERSION_SHIFT 16

#define VALID_NAMED_SECTION(s) \
	(((s) > DNS_SECTION_ANY) && ((s) < DNS_SECTION_MAX))
#define VALID_SECTION(s) (((s) >= DNS_SECTION_ANY) && ((s) < DNS_SECTION_MAX))
#define ADD_STRING(b, s)                                          \
	{                                                         \
		if (strlen(s) >= isc_buffer_availablelength(b)) { \
			result = ISC_R_NOSPACE;                   \
			goto cleanup;                             \
		} else                                            \
			isc_buffer_putstr(b, s);                  \
	}
#define PUT_YAMLSTR(target, namebuf, len, utfok)                   \
	{                                                          \
		result = put_yamlstr(target, namebuf, len, utfok); \
		if (result != ISC_R_SUCCESS) {                     \
			goto cleanup;                              \
		}                                                  \
	}
#define VALID_NAMED_PSEUDOSECTION(s) \
	(((s) > DNS_PSEUDOSECTION_ANY) && ((s) < DNS_PSEUDOSECTION_MAX))
#define VALID_PSEUDOSECTION(s) \
	(((s) >= DNS_PSEUDOSECTION_ANY) && ((s) < DNS_PSEUDOSECTION_MAX))

#define OPTOUT(x) (((x)->attributes & DNS_RDATASETATTR_OPTOUT) != 0)

/*%
 * This is the size of each individual scratchpad buffer, and the numbers
 * of various block allocations used within the server.
 * XXXMLG These should come from a config setting.
 */
#define SCRATCHPAD_SIZE	   1232
#define NAME_FILLCOUNT	   1024
#define NAME_FREEMAX	   8 * NAME_FILLCOUNT
#define OFFSET_COUNT	   4
#define RDATA_COUNT	   8
#define RDATALIST_COUNT	   8
#define RDATASET_FILLCOUNT 1024
#define RDATASET_FREEMAX   8 * RDATASET_FILLCOUNT

/*%
 * Text representation of the different items, for message_totext
 * functions.
 */
static const char *sectiontext[] = { "QUESTION", "ANSWER", "AUTHORITY",
				     "ADDITIONAL" };

static const char *updsectiontext[] = { "ZONE", "PREREQUISITE", "UPDATE",
					"ADDITIONAL" };

static const char *opcodetext[] = { "QUERY",	  "IQUERY",	"STATUS",
				    "RESERVED3",  "NOTIFY",	"UPDATE",
				    "RESERVED6",  "RESERVED7",	"RESERVED8",
				    "RESERVED9",  "RESERVED10", "RESERVED11",
				    "RESERVED12", "RESERVED13", "RESERVED14",
				    "RESERVED15" };

static const char *edetext[] = { "Other",
				 "Unsupported DNSKEY Algorithm",
				 "Unsupported DS Digest Type",
				 "Stale Answer",
				 "Forged Answer",
				 "DNSSEC Indeterminate",
				 "DNSSEC Bogus",
				 "Signature Expired",
				 "Signature Not Yet Valid",
				 "DNSKEY Missing",
				 "RRSIGs Missing",
				 "No Zone Key Bit Set",
				 "NSEC Missing",
				 "Cached Error",
				 "Not Ready",
				 "Blocked",
				 "Censored",
				 "Filtered",
				 "Prohibited",
				 "Stale NXDOMAIN Answer",
				 "Not Authoritative",
				 "Not Supported",
				 "No Reachable Authority",
				 "Network Error",
				 "Invalid Data" };

/*%
 * "helper" type, which consists of a block of some type, and is linkable.
 * For it to work, sizeof(dns_msgblock_t) must be a multiple of the pointer
 * size, or the allocated elements will not be aligned correctly.
 */
struct dns_msgblock {
	unsigned int count;
	unsigned int remaining;
	ISC_LINK(dns_msgblock_t) link;
}; /* dynamically sized */

static dns_msgblock_t *
msgblock_allocate(isc_mem_t *, unsigned int, unsigned int);

#define msgblock_get(block, type) \
	((type *)msgblock_internalget(block, sizeof(type)))

/*
 * A context type to pass information when checking a message signature
 * asynchronously.
 */
typedef struct checksig_ctx {
	isc_loop_t *loop;
	dns_message_t *msg;
	dns_view_t *view;
	dns_message_cb_t cb;
	void *cbarg;
	isc_result_t result;
} checksig_ctx_t;

/*
 * This function differs from public dns_message_puttemprdataset() that it
 * requires the *rdatasetp to be associated, and it will disassociate and
 * put it back to the memory pool.
 */
static void
dns__message_putassociatedrdataset(dns_message_t *msg,
				   dns_rdataset_t **rdatasetp);

static void *
msgblock_internalget(dns_msgblock_t *, unsigned int);

static void
msgblock_reset(dns_msgblock_t *);

static void
msgblock_free(isc_mem_t *, dns_msgblock_t *, unsigned int);

static void
logfmtpacket(dns_message_t *message, const char *description,
	     const isc_sockaddr_t *address, isc_logcategory_t *category,
	     isc_logmodule_t *module, const dns_master_style_t *style,
	     int level, isc_mem_t *mctx);

/*
 * Allocate a new dns_msgblock_t, and return a pointer to it.  If no memory
 * is free, return NULL.
 */
static dns_msgblock_t *
msgblock_allocate(isc_mem_t *mctx, unsigned int sizeof_type,
		  unsigned int count) {
	dns_msgblock_t *block;
	unsigned int length;

	length = sizeof(dns_msgblock_t) + (sizeof_type * count);

	block = isc_mem_get(mctx, length);

	block->count = count;
	block->remaining = count;

	ISC_LINK_INIT(block, link);

	return block;
}

/*
 * Return an element from the msgblock.  If no more are available, return
 * NULL.
 */
static void *
msgblock_internalget(dns_msgblock_t *block, unsigned int sizeof_type) {
	void *ptr;

	if (block == NULL || block->remaining == 0) {
		return NULL;
	}

	block->remaining--;

	ptr = (((unsigned char *)block) + sizeof(dns_msgblock_t) +
	       (sizeof_type * block->remaining));

	return ptr;
}

static void
msgblock_reset(dns_msgblock_t *block) {
	block->remaining = block->count;
}

/*
 * Release memory associated with a message block.
 */
static void
msgblock_free(isc_mem_t *mctx, dns_msgblock_t *block,
	      unsigned int sizeof_type) {
	unsigned int length;

	length = sizeof(dns_msgblock_t) + (sizeof_type * block->count);

	isc_mem_put(mctx, block, length);
}

/*
 * Allocate a new dynamic buffer, and attach it to this message as the
 * "current" buffer.  (which is always the last on the list, for our
 * uses)
 */
static isc_result_t
newbuffer(dns_message_t *msg, unsigned int size) {
	isc_buffer_t *dynbuf;

	dynbuf = NULL;
	isc_buffer_allocate(msg->mctx, &dynbuf, size);

	ISC_LIST_APPEND(msg->scratchpad, dynbuf, link);
	return ISC_R_SUCCESS;
}

static isc_buffer_t *
currentbuffer(dns_message_t *msg) {
	isc_buffer_t *dynbuf;

	dynbuf = ISC_LIST_TAIL(msg->scratchpad);
	INSIST(dynbuf != NULL);

	return dynbuf;
}

static void
releaserdata(dns_message_t *msg, dns_rdata_t *rdata) {
	ISC_LIST_PREPEND(msg->freerdata, rdata, link);
}

static dns_rdata_t *
newrdata(dns_message_t *msg) {
	dns_msgblock_t *msgblock;
	dns_rdata_t *rdata;

	rdata = ISC_LIST_HEAD(msg->freerdata);
	if (rdata != NULL) {
		ISC_LIST_UNLINK(msg->freerdata, rdata, link);
		return rdata;
	}

	msgblock = ISC_LIST_TAIL(msg->rdatas);
	rdata = msgblock_get(msgblock, dns_rdata_t);
	if (rdata == NULL) {
		msgblock = msgblock_allocate(msg->mctx, sizeof(dns_rdata_t),
					     RDATA_COUNT);
		ISC_LIST_APPEND(msg->rdatas, msgblock, link);

		rdata = msgblock_get(msgblock, dns_rdata_t);
	}

	dns_rdata_init(rdata);
	return rdata;
}

static void
releaserdatalist(dns_message_t *msg, dns_rdatalist_t *rdatalist) {
	ISC_LIST_PREPEND(msg->freerdatalist, rdatalist, link);
}

static dns_rdatalist_t *
newrdatalist(dns_message_t *msg) {
	dns_msgblock_t *msgblock;
	dns_rdatalist_t *rdatalist;

	rdatalist = ISC_LIST_HEAD(msg->freerdatalist);
	if (rdatalist != NULL) {
		ISC_LIST_UNLINK(msg->freerdatalist, rdatalist, link);
		goto out;
	}

	msgblock = ISC_LIST_TAIL(msg->rdatalists);
	rdatalist = msgblock_get(msgblock, dns_rdatalist_t);
	if (rdatalist == NULL) {
		msgblock = msgblock_allocate(msg->mctx, sizeof(dns_rdatalist_t),
					     RDATALIST_COUNT);
		ISC_LIST_APPEND(msg->rdatalists, msgblock, link);

		rdatalist = msgblock_get(msgblock, dns_rdatalist_t);
	}
out:
	dns_rdatalist_init(rdatalist);
	return rdatalist;
}

static dns_offsets_t *
newoffsets(dns_message_t *msg) {
	dns_msgblock_t *msgblock;
	dns_offsets_t *offsets;

	msgblock = ISC_LIST_TAIL(msg->offsets);
	offsets = msgblock_get(msgblock, dns_offsets_t);
	if (offsets == NULL) {
		msgblock = msgblock_allocate(msg->mctx, sizeof(dns_offsets_t),
					     OFFSET_COUNT);
		ISC_LIST_APPEND(msg->offsets, msgblock, link);

		offsets = msgblock_get(msgblock, dns_offsets_t);
	}

	return offsets;
}

static void
msginitheader(dns_message_t *m) {
	m->id = 0;
	m->flags = 0;
	m->rcode = 0;
	m->opcode = 0;
	m->rdclass = 0;
}

static void
msginitprivate(dns_message_t *m) {
	unsigned int i;

	for (i = 0; i < DNS_SECTION_MAX; i++) {
		m->cursors[i] = NULL;
		m->counts[i] = 0;
	}
	m->opt = NULL;
	m->sig0 = NULL;
	m->sig0name = NULL;
	m->tsig = NULL;
	m->tsigname = NULL;
	m->state = DNS_SECTION_ANY; /* indicate nothing parsed or rendered */
	m->opt_reserved = 0;
	m->sig_reserved = 0;
	m->reserved = 0;
	m->padding = 0;
	m->padding_off = 0;
	m->buffer = NULL;
}

static void
msginittsig(dns_message_t *m) {
	m->tsigstatus = dns_rcode_noerror;
	m->querytsigstatus = dns_rcode_noerror;
	m->tsigkey = NULL;
	m->tsigctx = NULL;
	m->sigstart = -1;
	m->sig0key = NULL;
	m->sig0status = dns_rcode_noerror;
	m->timeadjust = 0;
}

/*
 * Init elements to default state.  Used both when allocating a new element
 * and when resetting one.
 */
static void
msginit(dns_message_t *m) {
	msginitheader(m);
	msginitprivate(m);
	msginittsig(m);
	m->header_ok = 0;
	m->question_ok = 0;
	m->tcp_continuation = 0;
	m->verified_sig = 0;
	m->verify_attempted = 0;
	m->order = NULL;
	m->order_arg.env = NULL;
	m->order_arg.acl = NULL;
	m->order_arg.element = NULL;
	m->query.base = NULL;
	m->query.length = 0;
	m->free_query = 0;
	m->saved.base = NULL;
	m->saved.length = 0;
	m->free_saved = 0;
	m->cc_ok = 0;
	m->cc_bad = 0;
	m->tkey = 0;
	m->rdclass_set = 0;
	m->querytsig = NULL;
	m->indent.string = "\t";
	m->indent.count = 0;
}

static void
msgresetname(dns_message_t *msg, dns_name_t *name) {
	dns_rdataset_t *rds = NULL, *next_rds = NULL;

	ISC_LIST_FOREACH_SAFE (name->list, rds, link, next_rds) {
		ISC_LIST_UNLINK(name->list, rds, link);

		dns__message_putassociatedrdataset(msg, &rds);
	}
}

static void
msgresetnames(dns_message_t *msg, unsigned int first_section) {
	/* Clean up name lists. */
	for (size_t i = first_section; i < DNS_SECTION_MAX; i++) {
		dns_name_t *name = NULL, *next_name = NULL;

		ISC_LIST_FOREACH_SAFE (msg->sections[i], name, link, next_name)
		{
			ISC_LIST_UNLINK(msg->sections[i], name, link);

			msgresetname(msg, name);

			dns_message_puttempname(msg, &name);
		}
	}
}

static void
msgresetopt(dns_message_t *msg) {
	if (msg->opt != NULL) {
		if (msg->opt_reserved > 0) {
			dns_message_renderrelease(msg, msg->opt_reserved);
			msg->opt_reserved = 0;
		}
		dns__message_putassociatedrdataset(msg, &msg->opt);
		msg->opt = NULL;
		msg->cc_ok = 0;
		msg->cc_bad = 0;
	}
}

static void
msgresetsigs(dns_message_t *msg, bool replying) {
	if (msg->sig_reserved > 0) {
		dns_message_renderrelease(msg, msg->sig_reserved);
		msg->sig_reserved = 0;
	}
	if (msg->tsig != NULL) {
		INSIST(dns_rdataset_isassociated(msg->tsig));
		INSIST(msg->namepool != NULL);
		if (replying) {
			INSIST(msg->querytsig == NULL);
			msg->querytsig = msg->tsig;
		} else {
			dns__message_putassociatedrdataset(msg, &msg->tsig);
			if (msg->querytsig != NULL) {
				dns__message_putassociatedrdataset(
					msg, &msg->querytsig);
			}
		}
		dns_message_puttempname(msg, &msg->tsigname);
		msg->tsig = NULL;
	} else if (msg->querytsig != NULL && !replying) {
		dns__message_putassociatedrdataset(msg, &msg->querytsig);
		msg->querytsig = NULL;
	}
	if (msg->sig0 != NULL) {
		dns__message_putassociatedrdataset(msg, &msg->sig0);
		msg->sig0 = NULL;
	}
	if (msg->sig0name != NULL) {
		dns_message_puttempname(msg, &msg->sig0name);
	}
}

/*
 * Free all but one (or everything) for this message.  This is used by
 * both dns_message_reset() and dns__message_destroy().
 */
static void
msgreset(dns_message_t *msg, bool everything) {
	dns_msgblock_t *msgblock = NULL, *next_msgblock = NULL;
	isc_buffer_t *dynbuf = NULL, *next_dynbuf = NULL;
	dns_rdata_t *rdata = NULL;
	dns_rdatalist_t *rdatalist = NULL;

	msgresetnames(msg, 0);
	msgresetopt(msg);
	msgresetsigs(msg, false);

	/*
	 * Clean up linked lists.
	 */

	/*
	 * Run through the free lists, and just unlink anything found there.
	 * The memory isn't lost since these are part of message blocks we
	 * have allocated.
	 */
	rdata = ISC_LIST_HEAD(msg->freerdata);
	while (rdata != NULL) {
		ISC_LIST_UNLINK(msg->freerdata, rdata, link);
		rdata = ISC_LIST_HEAD(msg->freerdata);
	}
	rdatalist = ISC_LIST_HEAD(msg->freerdatalist);
	while (rdatalist != NULL) {
		ISC_LIST_UNLINK(msg->freerdatalist, rdatalist, link);
		rdatalist = ISC_LIST_HEAD(msg->freerdatalist);
	}

	dynbuf = ISC_LIST_HEAD(msg->scratchpad);
	INSIST(dynbuf != NULL);
	if (!everything) {
		isc_buffer_clear(dynbuf);
		dynbuf = ISC_LIST_NEXT(dynbuf, link);
	}
	while (dynbuf != NULL) {
		next_dynbuf = ISC_LIST_NEXT(dynbuf, link);
		ISC_LIST_UNLINK(msg->scratchpad, dynbuf, link);
		isc_buffer_free(&dynbuf);
		dynbuf = next_dynbuf;
	}

	msgblock = ISC_LIST_HEAD(msg->rdatas);
	if (!everything && msgblock != NULL) {
		msgblock_reset(msgblock);
		msgblock = ISC_LIST_NEXT(msgblock, link);
	}
	while (msgblock != NULL) {
		next_msgblock = ISC_LIST_NEXT(msgblock, link);
		ISC_LIST_UNLINK(msg->rdatas, msgblock, link);
		msgblock_free(msg->mctx, msgblock, sizeof(dns_rdata_t));
		msgblock = next_msgblock;
	}

	/*
	 * rdatalists could be empty.
	 */

	msgblock = ISC_LIST_HEAD(msg->rdatalists);
	if (!everything && msgblock != NULL) {
		msgblock_reset(msgblock);
		msgblock = ISC_LIST_NEXT(msgblock, link);
	}
	while (msgblock != NULL) {
		next_msgblock = ISC_LIST_NEXT(msgblock, link);
		ISC_LIST_UNLINK(msg->rdatalists, msgblock, link);
		msgblock_free(msg->mctx, msgblock, sizeof(dns_rdatalist_t));
		msgblock = next_msgblock;
	}

	msgblock = ISC_LIST_HEAD(msg->offsets);
	if (!everything && msgblock != NULL) {
		msgblock_reset(msgblock);
		msgblock = ISC_LIST_NEXT(msgblock, link);
	}
	while (msgblock != NULL) {
		next_msgblock = ISC_LIST_NEXT(msgblock, link);
		ISC_LIST_UNLINK(msg->offsets, msgblock, link);
		msgblock_free(msg->mctx, msgblock, sizeof(dns_offsets_t));
		msgblock = next_msgblock;
	}

	if (msg->tsigkey != NULL) {
		dns_tsigkey_detach(&msg->tsigkey);
		msg->tsigkey = NULL;
	}

	if (msg->tsigctx != NULL) {
		dst_context_destroy(&msg->tsigctx);
	}

	if (msg->query.base != NULL) {
		if (msg->free_query != 0) {
			isc_mem_put(msg->mctx, msg->query.base,
				    msg->query.length);
		}
		msg->query.base = NULL;
		msg->query.length = 0;
	}

	if (msg->saved.base != NULL) {
		if (msg->free_saved != 0) {
			isc_mem_put(msg->mctx, msg->saved.base,
				    msg->saved.length);
		}
		msg->saved.base = NULL;
		msg->saved.length = 0;
	}

	/*
	 * cleanup the buffer cleanup list
	 */
	dynbuf = ISC_LIST_HEAD(msg->cleanup);
	while (dynbuf != NULL) {
		next_dynbuf = ISC_LIST_NEXT(dynbuf, link);
		ISC_LIST_UNLINK(msg->cleanup, dynbuf, link);
		isc_buffer_free(&dynbuf);
		dynbuf = next_dynbuf;
	}

	if (msg->order_arg.env != NULL) {
		dns_aclenv_detach(&msg->order_arg.env);
	}
	if (msg->order_arg.acl != NULL) {
		dns_acl_detach(&msg->order_arg.acl);
	}

	/*
	 * Set other bits to normal default values.
	 */
	if (!everything) {
		msginit(msg);
	}
}

static unsigned int
spacefortsig(dns_tsigkey_t *key, int otherlen) {
	isc_region_t r1 = { 0 }, r2 = { 0 };
	unsigned int x = 0;

	/*
	 * The space required for a TSIG record is:
	 *
	 *	n1 bytes for the name
	 *	2 bytes for the type
	 *	2 bytes for the class
	 *	4 bytes for the ttl
	 *	2 bytes for the rdlength
	 *	n2 bytes for the algorithm name
	 *	6 bytes for the time signed
	 *	2 bytes for the fudge
	 *	2 bytes for the MAC size
	 *	x bytes for the MAC
	 *	2 bytes for the original id
	 *	2 bytes for the error
	 *	2 bytes for the other data length
	 *	y bytes for the other data (at most)
	 * ---------------------------------
	 *     26 + n1 + n2 + x + y bytes
	 */

	dns_name_toregion(key->name, &r1);
	if (key->alg != DST_ALG_UNKNOWN) {
		dns_name_toregion(dns_tsigkey_algorithm(key), &r2);
	}
	if (key->key != NULL) {
		isc_result_t result = dst_key_sigsize(key->key, &x);
		if (result != ISC_R_SUCCESS) {
			x = 0;
		}
	}
	return 26 + r1.length + r2.length + x + otherlen;
}

void
dns_message_create(isc_mem_t *mctx, isc_mempool_t *namepool,
		   isc_mempool_t *rdspool, dns_message_intent_t intent,
		   dns_message_t **msgp) {
	REQUIRE(mctx != NULL);
	REQUIRE(msgp != NULL);
	REQUIRE(*msgp == NULL);
	REQUIRE(intent == DNS_MESSAGE_INTENTPARSE ||
		intent == DNS_MESSAGE_INTENTRENDER);
	REQUIRE((namepool != NULL && rdspool != NULL) ||
		(namepool == NULL && rdspool == NULL));

	dns_message_t *msg = isc_mem_get(mctx, sizeof(dns_message_t));
	*msg = (dns_message_t){
		.from_to_wire = intent,
		.references = ISC_REFCOUNT_INITIALIZER(1),
		.scratchpad = ISC_LIST_INITIALIZER,
		.cleanup = ISC_LIST_INITIALIZER,
		.rdatas = ISC_LIST_INITIALIZER,
		.rdatalists = ISC_LIST_INITIALIZER,
		.offsets = ISC_LIST_INITIALIZER,
		.freerdata = ISC_LIST_INITIALIZER,
		.freerdatalist = ISC_LIST_INITIALIZER,
		.magic = DNS_MESSAGE_MAGIC,
		.namepool = namepool,
		.rdspool = rdspool,
		.free_pools = (namepool == NULL && rdspool == NULL),
	};

	isc_mem_attach(mctx, &msg->mctx);

	if (msg->free_pools) {
		dns_message_createpools(mctx, &msg->namepool, &msg->rdspool);
	}

	msginit(msg);

	for (size_t i = 0; i < DNS_SECTION_MAX; i++) {
		ISC_LIST_INIT(msg->sections[i]);
	}

	isc_buffer_t *dynbuf = NULL;
	isc_buffer_allocate(mctx, &dynbuf, SCRATCHPAD_SIZE);
	ISC_LIST_APPEND(msg->scratchpad, dynbuf, link);

	*msgp = msg;
}

void
dns_message_reset(dns_message_t *msg, dns_message_intent_t intent) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(intent == DNS_MESSAGE_INTENTPARSE ||
		intent == DNS_MESSAGE_INTENTRENDER);

	msgreset(msg, false);
	msg->from_to_wire = intent;
}

static void
dns__message_destroy(dns_message_t *msg) {
	REQUIRE(msg != NULL);
	REQUIRE(DNS_MESSAGE_VALID(msg));

	msgreset(msg, true);

	msg->magic = 0;

	if (msg->free_pools) {
		dns_message_destroypools(&msg->namepool, &msg->rdspool);
	}

	isc_mem_putanddetach(&msg->mctx, msg, sizeof(dns_message_t));
}

#if DNS_MESSAGE_TRACE
ISC_REFCOUNT_TRACE_IMPL(dns_message, dns__message_destroy);
#else
ISC_REFCOUNT_IMPL(dns_message, dns__message_destroy);
#endif

static bool
name_match(void *node, const void *key) {
	return dns_name_equal(node, key);
}

static isc_result_t
findname(dns_name_t **foundname, const dns_name_t *target,
	 dns_namelist_t *section) {
	dns_name_t *name = NULL;

	ISC_LIST_FOREACH_REV (*section, name, link) {
		if (dns_name_equal(name, target)) {
			if (foundname != NULL) {
				*foundname = name;
			}
			return ISC_R_SUCCESS;
		}
	}

	return ISC_R_NOTFOUND;
}

static uint32_t
rds_hash(dns_rdataset_t *rds) {
	isc_hash32_t state;

	isc_hash32_init(&state);
	isc_hash32_hash(&state, &rds->rdclass, sizeof(rds->rdclass), true);
	isc_hash32_hash(&state, &rds->type, sizeof(rds->type), true);
	isc_hash32_hash(&state, &rds->covers, sizeof(rds->covers), true);

	return isc_hash32_finalize(&state);
}

static bool
rds_match(void *node, const void *key0) {
	const dns_rdataset_t *rds = node;
	const dns_rdataset_t *key = key0;

	return rds->rdclass == key->rdclass && rds->type == key->type &&
	       rds->covers == key->covers;
}

isc_result_t
dns_message_findtype(const dns_name_t *name, dns_rdatatype_t type,
		     dns_rdatatype_t covers, dns_rdataset_t **rdatasetp) {
	dns_rdataset_t *rds = NULL;

	REQUIRE(name != NULL);
	REQUIRE(rdatasetp == NULL || *rdatasetp == NULL);

	ISC_LIST_FOREACH_REV (name->list, rds, link) {
		if (rds->type == type && rds->covers == covers) {
			SET_IF_NOT_NULL(rdatasetp, rds);

			return ISC_R_SUCCESS;
		}
	}

	return ISC_R_NOTFOUND;
}

/*
 * Read a name from buffer "source".
 */
static isc_result_t
getname(dns_name_t *name, isc_buffer_t *source, dns_message_t *msg,
	dns_decompress_t dctx) {
	isc_buffer_t *scratch;
	isc_result_t result;
	unsigned int tries;

	scratch = currentbuffer(msg);

	/*
	 * First try:  use current buffer.
	 * Second try:  allocate a new buffer and use that.
	 */
	tries = 0;
	while (tries < 2) {
		result = dns_name_fromwire(name, source, dctx, scratch);

		if (result == ISC_R_NOSPACE) {
			tries++;

			result = newbuffer(msg, SCRATCHPAD_SIZE);
			if (result != ISC_R_SUCCESS) {
				return result;
			}

			scratch = currentbuffer(msg);
			dns_name_reset(name);
		} else {
			return result;
		}
	}

	UNREACHABLE();
}

static isc_result_t
getrdata(isc_buffer_t *source, dns_message_t *msg, dns_decompress_t dctx,
	 dns_rdataclass_t rdclass, dns_rdatatype_t rdtype,
	 unsigned int rdatalen, dns_rdata_t *rdata) {
	isc_buffer_t *scratch;
	isc_result_t result;
	unsigned int tries;
	unsigned int trysize;

	scratch = currentbuffer(msg);

	isc_buffer_setactive(source, rdatalen);

	/*
	 * First try:  use current buffer.
	 * Second try:  allocate a new buffer of size
	 *     max(SCRATCHPAD_SIZE, 2 * compressed_rdatalen)
	 *     (the data will fit if it was not more than 50% compressed)
	 * Subsequent tries: double buffer size on each try.
	 */
	tries = 0;
	trysize = 0;
	/* XXX possibly change this to a while (tries < 2) loop */
	for (;;) {
		result = dns_rdata_fromwire(rdata, rdclass, rdtype, source,
					    dctx, scratch);

		if (result == ISC_R_NOSPACE) {
			if (tries == 0) {
				trysize = 2 * rdatalen;
				if (trysize < SCRATCHPAD_SIZE) {
					trysize = SCRATCHPAD_SIZE;
				}
			} else {
				INSIST(trysize != 0);
				if (trysize >= 65535) {
					return ISC_R_NOSPACE;
				}
				/* XXX DNS_R_RRTOOLONG? */
				trysize *= 2;
			}
			tries++;
			result = newbuffer(msg, trysize);
			if (result != ISC_R_SUCCESS) {
				return result;
			}

			scratch = currentbuffer(msg);
		} else {
			return result;
		}
	}
}

#define DO_ERROR(r)                          \
	do {                                 \
		if (best_effort) {           \
			seen_problem = true; \
		} else {                     \
			result = r;          \
			goto cleanup;        \
		}                            \
	} while (0)

static void
cleanup_name_hashmaps(dns_namelist_t *section) {
	dns_name_t *name = NULL;
	ISC_LIST_FOREACH (*section, name, link) {
		if (name->hashmap != NULL) {
			isc_hashmap_destroy(&name->hashmap);
		}
	}
}

static isc_result_t
getquestions(isc_buffer_t *source, dns_message_t *msg, dns_decompress_t dctx,
	     unsigned int options) {
	isc_region_t r;
	unsigned int count;
	dns_name_t *name = NULL;
	dns_name_t *found_name = NULL;
	dns_rdataset_t *rdataset = NULL;
	dns_rdatalist_t *rdatalist = NULL;
	isc_result_t result = ISC_R_SUCCESS;
	dns_rdatatype_t rdtype;
	dns_rdataclass_t rdclass;
	dns_namelist_t *section = &msg->sections[DNS_SECTION_QUESTION];
	bool best_effort = ((options & DNS_MESSAGEPARSE_BESTEFFORT) != 0);
	bool seen_problem = false;
	bool free_name = false;
	bool free_hashmaps = false;
	isc_hashmap_t *name_map = NULL;

	if (msg->counts[DNS_SECTION_QUESTION] > 1) {
		isc_hashmap_create(msg->mctx, 1, &name_map);
	}

	for (count = 0; count < msg->counts[DNS_SECTION_QUESTION]; count++) {
		name = NULL;
		dns_message_gettempname(msg, &name);
		name->offsets = (unsigned char *)newoffsets(msg);
		free_name = true;

		/*
		 * Parse the name out of this packet.
		 */
		isc_buffer_remainingregion(source, &r);
		isc_buffer_setactive(source, r.length);
		result = getname(name, source, msg, dctx);
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}

		/* If there is only one QNAME, skip the duplicity checks */
		if (name_map == NULL) {
			result = ISC_R_SUCCESS;
			goto skip_name_check;
		}

		/*
		 * Run through the section, looking to see if this name
		 * is already there.  If it is found, put back the allocated
		 * name since we no longer need it, and set our name pointer
		 * to point to the name we found.
		 */
		result = isc_hashmap_add(name_map, dns_name_hash(name),
					 name_match, name, name,
					 (void **)&found_name);

		/*
		 * If it is the first name in the section, accept it.
		 *
		 * If it is not, but is not the same as the name already
		 * in the question section, append to the section.  Note that
		 * here in the question section this is illegal, so return
		 * FORMERR.  In the future, check the opcode to see if
		 * this should be legal or not.  In either case we no longer
		 * need this name pointer.
		 */
	skip_name_check:
		switch (result) {
		case ISC_R_SUCCESS:
			if (!ISC_LIST_EMPTY(*section)) {
				DO_ERROR(DNS_R_FORMERR);
			}
			ISC_LIST_APPEND(*section, name, link);
			break;
		case ISC_R_EXISTS:
			dns_message_puttempname(msg, &name);
			name = found_name;
			found_name = NULL;
			break;
		default:
			UNREACHABLE();
		}

		free_name = false;

		/*
		 * Get type and class.
		 */
		isc_buffer_remainingregion(source, &r);
		if (r.length < 4) {
			result = ISC_R_UNEXPECTEDEND;
			goto cleanup;
		}
		rdtype = isc_buffer_getuint16(source);
		rdclass = isc_buffer_getuint16(source);

		/*
		 * If this class is different than the one we already read,
		 * this is an error.
		 */
		if (msg->rdclass_set == 0) {
			msg->rdclass = rdclass;
			msg->rdclass_set = 1;
		} else if (msg->rdclass != rdclass) {
			DO_ERROR(DNS_R_FORMERR);
		}

		/*
		 * Is this a TKEY query?
		 */
		if (rdtype == dns_rdatatype_tkey) {
			msg->tkey = 1;
		}

		/*
		 * Allocate a new rdatalist.
		 */
		rdatalist = newrdatalist(msg);
		rdatalist->type = rdtype;
		rdatalist->rdclass = rdclass;
		rdatalist->covers = 0;

		/*
		 * Convert rdatalist to rdataset, and attach the latter to
		 * the name.
		 */
		dns_message_gettemprdataset(msg, &rdataset);
		dns_rdatalist_tordataset(rdatalist, rdataset);

		rdataset->attributes |= DNS_RDATASETATTR_QUESTION;

		/*
		 * Skip the duplicity check for first rdataset
		 */
		if (ISC_LIST_EMPTY(name->list)) {
			result = ISC_R_SUCCESS;
			goto skip_rds_check;
		}

		/*
		 * Can't ask the same question twice.
		 */
		if (name->hashmap == NULL) {
			isc_hashmap_create(msg->mctx, 1, &name->hashmap);
			free_hashmaps = true;

			INSIST(ISC_LIST_HEAD(name->list) ==
			       ISC_LIST_TAIL(name->list));

			dns_rdataset_t *old_rdataset =
				ISC_LIST_HEAD(name->list);

			result = isc_hashmap_add(
				name->hashmap, rds_hash(old_rdataset),
				rds_match, old_rdataset, old_rdataset, NULL);

			INSIST(result == ISC_R_SUCCESS);
		}
		result = isc_hashmap_add(name->hashmap, rds_hash(rdataset),
					 rds_match, rdataset, rdataset, NULL);
		if (result == ISC_R_EXISTS) {
			DO_ERROR(DNS_R_FORMERR);
		}

	skip_rds_check:
		ISC_LIST_APPEND(name->list, rdataset, link);

		rdataset = NULL;
	}

	if (seen_problem) {
		/* XXX test coverage */
		result = DNS_R_RECOVERABLE;
	}

cleanup:
	if (rdataset != NULL) {
		if (dns_rdataset_isassociated(rdataset)) {
			dns_rdataset_disassociate(rdataset);
		}
		dns_message_puttemprdataset(msg, &rdataset);
	}

	if (free_name) {
		dns_message_puttempname(msg, &name);
	}

	if (free_hashmaps) {
		cleanup_name_hashmaps(section);
	}

	if (name_map != NULL) {
		isc_hashmap_destroy(&name_map);
	}

	return result;
}

static bool
update(dns_section_t section, dns_rdataclass_t rdclass) {
	if (section == DNS_SECTION_PREREQUISITE) {
		return rdclass == dns_rdataclass_any ||
		       rdclass == dns_rdataclass_none;
	}
	if (section == DNS_SECTION_UPDATE) {
		return rdclass == dns_rdataclass_any;
	}
	return false;
}

static isc_result_t
getsection(isc_buffer_t *source, dns_message_t *msg, dns_decompress_t dctx,
	   dns_section_t sectionid, unsigned int options) {
	isc_region_t r;
	unsigned int count, rdatalen;
	dns_name_t *name = NULL;
	dns_name_t *found_name = NULL;
	dns_rdataset_t *rdataset = NULL;
	dns_rdataset_t *found_rdataset = NULL;
	dns_rdatalist_t *rdatalist = NULL;
	isc_result_t result = ISC_R_SUCCESS;
	dns_rdatatype_t rdtype, covers;
	dns_rdataclass_t rdclass;
	dns_rdata_t *rdata = NULL;
	dns_ttl_t ttl;
	dns_namelist_t *section = &msg->sections[sectionid];
	bool free_name = false, seen_problem = false;
	bool free_hashmaps = false;
	bool preserve_order = ((options & DNS_MESSAGEPARSE_PRESERVEORDER) != 0);
	bool best_effort = ((options & DNS_MESSAGEPARSE_BESTEFFORT) != 0);
	bool isedns, issigzero, istsig;
	isc_hashmap_t *name_map = NULL;

	if (msg->counts[sectionid] > 1) {
		isc_hashmap_create(msg->mctx, 1, &name_map);
	}

	for (count = 0; count < msg->counts[sectionid]; count++) {
		int recstart = source->current;
		bool skip_name_search, skip_type_search;

		skip_name_search = false;
		skip_type_search = false;
		isedns = false;
		issigzero = false;
		istsig = false;
		found_rdataset = NULL;

		name = NULL;
		dns_message_gettempname(msg, &name);
		name->offsets = (unsigned char *)newoffsets(msg);
		free_name = true;

		/*
		 * Parse the name out of this packet.
		 */
		isc_buffer_remainingregion(source, &r);
		isc_buffer_setactive(source, r.length);
		result = getname(name, source, msg, dctx);
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}

		/*
		 * Get type, class, ttl, and rdatalen.  Verify that at least
		 * rdatalen bytes remain.  (Some of this is deferred to
		 * later.)
		 */
		isc_buffer_remainingregion(source, &r);
		if (r.length < 2 + 2 + 4 + 2) {
			result = ISC_R_UNEXPECTEDEND;
			goto cleanup;
		}
		rdtype = isc_buffer_getuint16(source);
		rdclass = isc_buffer_getuint16(source);

		/*
		 * If there was no question section, we may not yet have
		 * established a class.  Do so now.
		 */
		if (msg->rdclass_set == 0 &&
		    rdtype != dns_rdatatype_opt &&  /* class is UDP SIZE */
		    rdtype != dns_rdatatype_tsig && /* class is ANY */
		    rdtype != dns_rdatatype_tkey)   /* class is undefined */
		{
			msg->rdclass = rdclass;
			msg->rdclass_set = 1;
		}

		/*
		 * If this class is different than the one in the question
		 * section, bail.
		 */
		if (msg->opcode != dns_opcode_update &&
		    rdtype != dns_rdatatype_tsig &&
		    rdtype != dns_rdatatype_opt &&
		    rdtype != dns_rdatatype_key &&  /* in a TKEY query */
		    rdtype != dns_rdatatype_sig &&  /* SIG(0) */
		    rdtype != dns_rdatatype_tkey && /* Win2000 TKEY */
		    msg->rdclass != dns_rdataclass_any &&
		    msg->rdclass != rdclass)
		{
			DO_ERROR(DNS_R_FORMERR);
		}

		/*
		 * If this is not a TKEY query/response then the KEY
		 * record's class needs to match.
		 */
		if (msg->opcode != dns_opcode_update && !msg->tkey &&
		    rdtype == dns_rdatatype_key &&
		    msg->rdclass != dns_rdataclass_any &&
		    msg->rdclass != rdclass)
		{
			DO_ERROR(DNS_R_FORMERR);
		}

		/*
		 * Special type handling for TSIG, OPT, and TKEY.
		 */
		if (rdtype == dns_rdatatype_tsig) {
			/*
			 * If it is a tsig, verify that it is in the
			 * additional data section.
			 */
			if (sectionid != DNS_SECTION_ADDITIONAL ||
			    rdclass != dns_rdataclass_any ||
			    count != msg->counts[sectionid] - 1)
			{
				DO_ERROR(DNS_R_BADTSIG);
			} else {
				skip_name_search = true;
				skip_type_search = true;
				istsig = true;
			}
		} else if (rdtype == dns_rdatatype_opt) {
			/*
			 * The name of an OPT record must be ".", it
			 * must be in the additional data section, and
			 * it must be the first OPT we've seen.
			 */
			if (!dns_name_equal(dns_rootname, name) ||
			    sectionid != DNS_SECTION_ADDITIONAL ||
			    msg->opt != NULL)
			{
				DO_ERROR(DNS_R_FORMERR);
			} else {
				skip_name_search = true;
				skip_type_search = true;
				isedns = true;
			}
		} else if (rdtype == dns_rdatatype_tkey) {
			/*
			 * A TKEY must be in the additional section if this
			 * is a query, and the answer section if this is a
			 * response.  Unless it's a Win2000 client.
			 *
			 * Its class is ignored.
			 */
			dns_section_t tkeysection;

			if ((msg->flags & DNS_MESSAGEFLAG_QR) == 0) {
				tkeysection = DNS_SECTION_ADDITIONAL;
			} else {
				tkeysection = DNS_SECTION_ANSWER;
			}
			if (sectionid != tkeysection &&
			    sectionid != DNS_SECTION_ANSWER)
			{
				DO_ERROR(DNS_R_FORMERR);
			}
		}

		/*
		 * ... now get ttl and rdatalen, and check buffer.
		 */
		ttl = isc_buffer_getuint32(source);
		rdatalen = isc_buffer_getuint16(source);
		r.length -= (2 + 2 + 4 + 2);
		if (r.length < rdatalen) {
			result = ISC_R_UNEXPECTEDEND;
			goto cleanup;
		}

		/*
		 * Read the rdata from the wire format.  Interpret the
		 * rdata according to its actual class, even if it had a
		 * DynDNS meta-class in the packet (unless this is a TSIG).
		 * Then put the meta-class back into the finished rdata.
		 */
		rdata = newrdata(msg);
		if (msg->opcode == dns_opcode_update &&
		    update(sectionid, rdclass))
		{
			if (rdatalen != 0) {
				result = DNS_R_FORMERR;
				goto cleanup;
			}
			/*
			 * When the rdata is empty, the data pointer is
			 * never dereferenced, but it must still be non-NULL.
			 * Casting 1 rather than "" avoids warnings about
			 * discarding the const attribute of a string,
			 * for compilers that would warn about such things.
			 */
			rdata->data = (unsigned char *)1;
			rdata->length = 0;
			rdata->rdclass = rdclass;
			rdata->type = rdtype;
			rdata->flags = DNS_RDATA_UPDATE;
			result = ISC_R_SUCCESS;
		} else if (rdclass == dns_rdataclass_none &&
			   msg->opcode == dns_opcode_update &&
			   sectionid == DNS_SECTION_UPDATE)
		{
			result = getrdata(source, msg, dctx, msg->rdclass,
					  rdtype, rdatalen, rdata);
		} else {
			result = getrdata(source, msg, dctx, rdclass, rdtype,
					  rdatalen, rdata);
		}
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}
		rdata->rdclass = rdclass;
		if (rdtype == dns_rdatatype_rrsig && rdata->flags == 0) {
			covers = dns_rdata_covers(rdata);
			if (covers == 0) {
				DO_ERROR(DNS_R_FORMERR);
			}
		} else if (rdtype == dns_rdatatype_sig /* SIG(0) */ &&
			   rdata->flags == 0)
		{
			covers = dns_rdata_covers(rdata);
			if (covers == 0) {
				if (sectionid != DNS_SECTION_ADDITIONAL ||
				    count != msg->counts[sectionid] - 1 ||
				    !dns_name_equal(name, dns_rootname))
				{
					DO_ERROR(DNS_R_BADSIG0);
				} else {
					skip_name_search = true;
					skip_type_search = true;
					issigzero = true;
				}
			} else {
				if (msg->rdclass != dns_rdataclass_any &&
				    msg->rdclass != rdclass)
				{
					/* XXX test coverage */
					DO_ERROR(DNS_R_FORMERR);
				}
			}
		} else {
			covers = 0;
		}

		/*
		 * Check the ownername of NSEC3 records
		 */
		if (rdtype == dns_rdatatype_nsec3 &&
		    !dns_rdata_checkowner(name, msg->rdclass, rdtype, false))
		{
			result = DNS_R_BADOWNERNAME;
			goto cleanup;
		}

		/*
		 * If we are doing a dynamic update or this is a meta-type,
		 * don't bother searching for a name, just append this one
		 * to the end of the message.
		 */
		if (preserve_order || msg->opcode == dns_opcode_update ||
		    skip_name_search)
		{
			if (!isedns && !istsig && !issigzero) {
				ISC_LIST_APPEND(*section, name, link);
				free_name = false;
			}
		} else {
			if (name_map == NULL) {
				result = ISC_R_SUCCESS;
				goto skip_name_check;
			}

			/*
			 * Run through the section, looking to see if this name
			 * is already there.  If it is found, put back the
			 * allocated name since we no longer need it, and set
			 * our name pointer to point to the name we found.
			 */
			result = isc_hashmap_add(name_map, dns_name_hash(name),
						 name_match, name, name,
						 (void **)&found_name);

			/*
			 * If it is a new name, append to the section.
			 */
		skip_name_check:
			switch (result) {
			case ISC_R_SUCCESS:
				ISC_LIST_APPEND(*section, name, link);
				break;
			case ISC_R_EXISTS:
				dns_message_puttempname(msg, &name);
				name = found_name;
				found_name = NULL;
				break;
			default:
				UNREACHABLE();
			}
			free_name = false;
		}

		rdatalist = newrdatalist(msg);
		rdatalist->type = rdtype;
		rdatalist->covers = covers;
		rdatalist->rdclass = rdclass;
		rdatalist->ttl = ttl;

		dns_message_gettemprdataset(msg, &rdataset);
		dns_rdatalist_tordataset(rdatalist, rdataset);
		dns_rdataset_setownercase(rdataset, name);
		rdatalist = NULL;

		/*
		 * Search name for the particular type and class.
		 * Skip this stage if in update mode or this is a meta-type.
		 */
		if (isedns || istsig || issigzero) {
			/* Skip adding the rdataset to the tables */
		} else if (preserve_order || msg->opcode == dns_opcode_update ||
			   skip_type_search)
		{
			result = ISC_R_SUCCESS;

			ISC_LIST_APPEND(name->list, rdataset, link);
		} else {
			/*
			 * If this is a type that can only occur in
			 * the question section, fail.
			 */
			if (dns_rdatatype_questiononly(rdtype)) {
				DO_ERROR(DNS_R_FORMERR);
			}

			if (ISC_LIST_EMPTY(name->list)) {
				result = ISC_R_SUCCESS;
				goto skip_rds_check;
			}

			if (name->hashmap == NULL) {
				isc_hashmap_create(msg->mctx, 1,
						   &name->hashmap);
				free_hashmaps = true;

				INSIST(ISC_LIST_HEAD(name->list) ==
				       ISC_LIST_TAIL(name->list));

				dns_rdataset_t *old_rdataset =
					ISC_LIST_HEAD(name->list);

				result = isc_hashmap_add(
					name->hashmap, rds_hash(old_rdataset),
					rds_match, old_rdataset, old_rdataset,
					NULL);

				INSIST(result == ISC_R_SUCCESS);
			}

			result = isc_hashmap_add(
				name->hashmap, rds_hash(rdataset), rds_match,
				rdataset, rdataset, (void **)&found_rdataset);

			/*
			 * If we found an rdataset that matches, we need to
			 * append this rdata to that set.  If we did not, we
			 * need to create a new rdatalist, store the important
			 * bits there, convert it to an rdataset, and link the
			 * latter to the name. Yuck.  When appending, make
			 * certain that the type isn't a singleton type, such as
			 * SOA or CNAME.
			 *
			 * Note that this check will be bypassed when preserving
			 * order, the opcode is an update, or the type search is
			 * skipped.
			 */
		skip_rds_check:
			switch (result) {
			case ISC_R_EXISTS:
				/* Free the rdataset we used as the key */
				dns__message_putassociatedrdataset(msg,
								   &rdataset);
				result = ISC_R_SUCCESS;
				rdataset = found_rdataset;

				if (!dns_rdatatype_issingleton(rdtype)) {
					break;
				}

				dns_rdatalist_fromrdataset(rdataset,
							   &rdatalist);
				dns_rdata_t *first =
					ISC_LIST_HEAD(rdatalist->rdata);
				INSIST(first != NULL);
				if (dns_rdata_compare(rdata, first) != 0) {
					DO_ERROR(DNS_R_FORMERR);
				}
				break;
			case ISC_R_SUCCESS:
				ISC_LIST_APPEND(name->list, rdataset, link);
				break;
			default:
				UNREACHABLE();
			}
		}

		/*
		 * Minimize TTLs.
		 *
		 * Section 5.2 of RFC2181 says we should drop
		 * nonauthoritative rrsets where the TTLs differ, but we
		 * currently treat them the as if they were authoritative and
		 * minimize them.
		 */
		if (ttl != rdataset->ttl) {
			rdataset->attributes |= DNS_RDATASETATTR_TTLADJUSTED;
			if (ttl < rdataset->ttl) {
				rdataset->ttl = ttl;
			}
		}

		/* Append this rdata to the rdataset. */
		dns_rdatalist_fromrdataset(rdataset, &rdatalist);
		ISC_LIST_APPEND(rdatalist->rdata, rdata, link);

		/*
		 * If this is an OPT, SIG(0) or TSIG record, remember it.
		 * Also, set the extended rcode for TSIG.
		 *
		 * Note msg->opt, msg->sig0 and msg->tsig will only be
		 * already set if best-effort parsing is enabled otherwise
		 * there will only be at most one of each.
		 */
		if (isedns) {
			dns_rcode_t ercode;

			msg->opt = rdataset;
			ercode = (dns_rcode_t)((msg->opt->ttl &
						DNS_MESSAGE_EDNSRCODE_MASK) >>
					       20);
			msg->rcode |= ercode;
			dns_message_puttempname(msg, &name);
			free_name = false;
		} else if (issigzero) {
			msg->sig0 = rdataset;
			msg->sig0name = name;
			msg->sigstart = recstart;
			free_name = false;
		} else if (istsig) {
			msg->tsig = rdataset;
			msg->tsigname = name;
			msg->sigstart = recstart;
			/*
			 * Windows doesn't like TSIG names to be compressed.
			 */
			msg->tsigname->attributes.nocompress = true;
			free_name = false;
		}
		rdataset = NULL;

		if (seen_problem) {
			if (free_name) {
				/* XXX test coverage */
				dns_message_puttempname(msg, &name);
			}
			free_name = false;
		}
		INSIST(!free_name);
	}

	if (seen_problem) {
		result = DNS_R_RECOVERABLE;
	}

cleanup:
	if (rdataset != NULL && rdataset != found_rdataset) {
		dns__message_putassociatedrdataset(msg, &rdataset);
	}
	if (free_name) {
		dns_message_puttempname(msg, &name);
	}

	if (free_hashmaps) {
		cleanup_name_hashmaps(section);
	}

	if (name_map != NULL) {
		isc_hashmap_destroy(&name_map);
	}

	return result;
}

isc_result_t
dns_message_parse(dns_message_t *msg, isc_buffer_t *source,
		  unsigned int options) {
	isc_region_t r;
	dns_decompress_t dctx;
	isc_result_t ret;
	uint16_t tmpflags;
	isc_buffer_t origsource;
	bool seen_problem;
	bool ignore_tc;

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(source != NULL);
	REQUIRE(msg->from_to_wire == DNS_MESSAGE_INTENTPARSE);

	seen_problem = false;
	ignore_tc = ((options & DNS_MESSAGEPARSE_IGNORETRUNCATION) != 0);

	origsource = *source;

	msg->header_ok = 0;
	msg->question_ok = 0;

	if ((options & DNS_MESSAGEPARSE_CLONEBUFFER) == 0) {
		isc_buffer_usedregion(&origsource, &msg->saved);
	} else {
		msg->saved.length = isc_buffer_usedlength(&origsource);
		msg->saved.base = isc_mem_get(msg->mctx, msg->saved.length);
		memmove(msg->saved.base, isc_buffer_base(&origsource),
			msg->saved.length);
		msg->free_saved = 1;
	}

	isc_buffer_remainingregion(source, &r);
	if (r.length < DNS_MESSAGE_HEADERLEN) {
		return ISC_R_UNEXPECTEDEND;
	}

	msg->id = isc_buffer_getuint16(source);
	tmpflags = isc_buffer_getuint16(source);
	msg->opcode = ((tmpflags & DNS_MESSAGE_OPCODE_MASK) >>
		       DNS_MESSAGE_OPCODE_SHIFT);
	msg->rcode = (dns_rcode_t)(tmpflags & DNS_MESSAGE_RCODE_MASK);
	msg->flags = (tmpflags & DNS_MESSAGE_FLAG_MASK);
	msg->counts[DNS_SECTION_QUESTION] = isc_buffer_getuint16(source);
	msg->counts[DNS_SECTION_ANSWER] = isc_buffer_getuint16(source);
	msg->counts[DNS_SECTION_AUTHORITY] = isc_buffer_getuint16(source);
	msg->counts[DNS_SECTION_ADDITIONAL] = isc_buffer_getuint16(source);

	msg->header_ok = 1;
	msg->state = DNS_SECTION_QUESTION;

	dctx = DNS_DECOMPRESS_ALWAYS;

	ret = getquestions(source, msg, dctx, options);

	if (ret == ISC_R_UNEXPECTEDEND && ignore_tc) {
		goto truncated;
	}
	if (ret == DNS_R_RECOVERABLE) {
		seen_problem = true;
		ret = ISC_R_SUCCESS;
	}
	if (ret != ISC_R_SUCCESS) {
		return ret;
	}
	msg->question_ok = 1;

	ret = getsection(source, msg, dctx, DNS_SECTION_ANSWER, options);
	if (ret == ISC_R_UNEXPECTEDEND && ignore_tc) {
		goto truncated;
	}
	if (ret == DNS_R_RECOVERABLE) {
		seen_problem = true;
		ret = ISC_R_SUCCESS;
	}
	if (ret != ISC_R_SUCCESS) {
		return ret;
	}

	ret = getsection(source, msg, dctx, DNS_SECTION_AUTHORITY, options);
	if (ret == ISC_R_UNEXPECTEDEND && ignore_tc) {
		goto truncated;
	}
	if (ret == DNS_R_RECOVERABLE) {
		seen_problem = true;
		ret = ISC_R_SUCCESS;
	}
	if (ret != ISC_R_SUCCESS) {
		return ret;
	}

	ret = getsection(source, msg, dctx, DNS_SECTION_ADDITIONAL, options);
	if (ret == ISC_R_UNEXPECTEDEND && ignore_tc) {
		goto truncated;
	}
	if (ret == DNS_R_RECOVERABLE) {
		seen_problem = true;
		ret = ISC_R_SUCCESS;
	}
	if (ret != ISC_R_SUCCESS) {
		return ret;
	}

	isc_buffer_remainingregion(source, &r);
	if (r.length != 0) {
		isc_log_write(dns_lctx, ISC_LOGCATEGORY_GENERAL,
			      DNS_LOGMODULE_MESSAGE, ISC_LOG_DEBUG(3),
			      "message has %u byte(s) of trailing garbage",
			      r.length);
	}

truncated:

	if (ret == ISC_R_UNEXPECTEDEND && ignore_tc) {
		return DNS_R_RECOVERABLE;
	}
	if (seen_problem) {
		return DNS_R_RECOVERABLE;
	}
	return ISC_R_SUCCESS;
}

isc_result_t
dns_message_renderbegin(dns_message_t *msg, dns_compress_t *cctx,
			isc_buffer_t *buffer) {
	isc_region_t r;

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(buffer != NULL);
	REQUIRE(isc_buffer_length(buffer) < 65536);
	REQUIRE(msg->buffer == NULL);
	REQUIRE(msg->from_to_wire == DNS_MESSAGE_INTENTRENDER);

	msg->cctx = cctx;

	/*
	 * Erase the contents of this buffer.
	 */
	isc_buffer_clear(buffer);

	/*
	 * Make certain there is enough for at least the header in this
	 * buffer.
	 */
	isc_buffer_availableregion(buffer, &r);
	if (r.length < DNS_MESSAGE_HEADERLEN) {
		return ISC_R_NOSPACE;
	}

	if (r.length - DNS_MESSAGE_HEADERLEN < msg->reserved) {
		return ISC_R_NOSPACE;
	}

	/*
	 * Reserve enough space for the header in this buffer.
	 */
	isc_buffer_add(buffer, DNS_MESSAGE_HEADERLEN);

	msg->buffer = buffer;

	return ISC_R_SUCCESS;
}

isc_result_t
dns_message_renderchangebuffer(dns_message_t *msg, isc_buffer_t *buffer) {
	isc_region_t r, rn;

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(buffer != NULL);
	REQUIRE(msg->buffer != NULL);

	/*
	 * Ensure that the new buffer is empty, and has enough space to
	 * hold the current contents.
	 */
	isc_buffer_clear(buffer);

	isc_buffer_availableregion(buffer, &rn);
	isc_buffer_usedregion(msg->buffer, &r);
	REQUIRE(rn.length > r.length);

	/*
	 * Copy the contents from the old to the new buffer.
	 */
	isc_buffer_add(buffer, r.length);
	memmove(rn.base, r.base, r.length);

	msg->buffer = buffer;

	return ISC_R_SUCCESS;
}

void
dns_message_renderrelease(dns_message_t *msg, unsigned int space) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(space <= msg->reserved);

	msg->reserved -= space;
}

isc_result_t
dns_message_renderreserve(dns_message_t *msg, unsigned int space) {
	isc_region_t r;

	REQUIRE(DNS_MESSAGE_VALID(msg));

	if (msg->buffer != NULL) {
		isc_buffer_availableregion(msg->buffer, &r);
		if (r.length < (space + msg->reserved)) {
			return ISC_R_NOSPACE;
		}
	}

	msg->reserved += space;

	return ISC_R_SUCCESS;
}

static bool
wrong_priority(dns_rdataset_t *rds, int pass, dns_rdatatype_t preferred_glue) {
	int pass_needed;

	/*
	 * If we are not rendering class IN, this ordering is bogus.
	 */
	if (rds->rdclass != dns_rdataclass_in) {
		return false;
	}

	switch (rds->type) {
	case dns_rdatatype_a:
	case dns_rdatatype_aaaa:
		if (preferred_glue == rds->type) {
			pass_needed = 4;
		} else {
			pass_needed = 3;
		}
		break;
	case dns_rdatatype_rrsig:
	case dns_rdatatype_dnskey:
		pass_needed = 2;
		break;
	default:
		pass_needed = 1;
	}

	if (pass_needed >= pass) {
		return false;
	}

	return true;
}

static isc_result_t
renderset(dns_rdataset_t *rdataset, const dns_name_t *owner_name,
	  dns_compress_t *cctx, isc_buffer_t *target, unsigned int reserved,
	  unsigned int options, unsigned int *countp) {
	isc_result_t result;

	/*
	 * Shrink the space in the buffer by the reserved amount.
	 */
	if (target->length - target->used < reserved) {
		return ISC_R_NOSPACE;
	}

	target->length -= reserved;
	result = dns_rdataset_towire(rdataset, owner_name, cctx, target,
				     options, countp);
	target->length += reserved;

	return result;
}

static void
maybe_clear_ad(dns_message_t *msg, dns_section_t sectionid) {
	if (msg->counts[sectionid] == 0 &&
	    (sectionid == DNS_SECTION_ANSWER ||
	     (sectionid == DNS_SECTION_AUTHORITY &&
	      msg->counts[DNS_SECTION_ANSWER] == 0)))
	{
		msg->flags &= ~DNS_MESSAGEFLAG_AD;
	}
}

static void
update_min_section_ttl(dns_message_t *restrict msg,
		       const dns_section_t sectionid,
		       dns_rdataset_t *restrict rdataset) {
	if (!msg->minttl[sectionid].is_set ||
	    rdataset->ttl < msg->minttl[sectionid].ttl)
	{
		msg->minttl[sectionid].is_set = true;
		msg->minttl[sectionid].ttl = rdataset->ttl;
	}
}

isc_result_t
dns_message_rendersection(dns_message_t *msg, dns_section_t sectionid,
			  unsigned int options) {
	dns_namelist_t *section;
	dns_name_t *name, *next_name;
	dns_rdataset_t *rdataset, *next_rdataset;
	unsigned int count, total;
	isc_result_t result;
	isc_buffer_t st; /* for rollbacks */
	int pass;
	bool partial = false;
	unsigned int rd_options;
	dns_rdatatype_t preferred_glue = 0;

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(msg->buffer != NULL);
	REQUIRE(VALID_NAMED_SECTION(sectionid));

	section = &msg->sections[sectionid];

	if ((sectionid == DNS_SECTION_ADDITIONAL) &&
	    (options & DNS_MESSAGERENDER_ORDERED) == 0)
	{
		if ((options & DNS_MESSAGERENDER_PREFER_A) != 0) {
			preferred_glue = dns_rdatatype_a;
			pass = 4;
		} else if ((options & DNS_MESSAGERENDER_PREFER_AAAA) != 0) {
			preferred_glue = dns_rdatatype_aaaa;
			pass = 4;
		} else {
			pass = 3;
		}
	} else {
		pass = 1;
	}

	if ((options & DNS_MESSAGERENDER_OMITDNSSEC) == 0) {
		rd_options = 0;
	} else {
		rd_options = DNS_RDATASETTOWIRE_OMITDNSSEC;
	}

	/*
	 * Shrink the space in the buffer by the reserved amount.
	 */
	if (msg->buffer->length - msg->buffer->used < msg->reserved) {
		return ISC_R_NOSPACE;
	}
	msg->buffer->length -= msg->reserved;

	total = 0;
	if (msg->reserved == 0 && (options & DNS_MESSAGERENDER_PARTIAL) != 0) {
		partial = true;
	}

	/*
	 * Render required glue first.  Set TC if it won't fit.
	 */
	name = ISC_LIST_HEAD(*section);
	if (name != NULL) {
		rdataset = ISC_LIST_HEAD(name->list);
		if (rdataset != NULL &&
		    (rdataset->attributes & DNS_RDATASETATTR_REQUIREDGLUE) !=
			    0 &&
		    (rdataset->attributes & DNS_RDATASETATTR_RENDERED) == 0)
		{
			const void *order_arg = &msg->order_arg;
			st = *(msg->buffer);
			count = 0;
			if (partial) {
				result = dns_rdataset_towirepartial(
					rdataset, name, msg->cctx, msg->buffer,
					msg->order, order_arg, rd_options,
					&count, NULL);
			} else {
				result = dns_rdataset_towiresorted(
					rdataset, name, msg->cctx, msg->buffer,
					msg->order, order_arg, rd_options,
					&count);
			}
			total += count;
			if (partial && result == ISC_R_NOSPACE) {
				msg->flags |= DNS_MESSAGEFLAG_TC;
				msg->buffer->length += msg->reserved;
				msg->counts[sectionid] += total;
				return result;
			}
			if (result == ISC_R_NOSPACE) {
				msg->flags |= DNS_MESSAGEFLAG_TC;
			}
			if (result != ISC_R_SUCCESS) {
				dns_compress_rollback(msg->cctx, st.used);
				*(msg->buffer) = st; /* rollback */
				msg->buffer->length += msg->reserved;
				msg->counts[sectionid] += total;
				return result;
			}

			update_min_section_ttl(msg, sectionid, rdataset);

			rdataset->attributes |= DNS_RDATASETATTR_RENDERED;
		}
	}

	do {
		name = ISC_LIST_HEAD(*section);
		if (name == NULL) {
			msg->buffer->length += msg->reserved;
			msg->counts[sectionid] += total;
			return ISC_R_SUCCESS;
		}

		while (name != NULL) {
			next_name = ISC_LIST_NEXT(name, link);

			rdataset = ISC_LIST_HEAD(name->list);
			while (rdataset != NULL) {
				next_rdataset = ISC_LIST_NEXT(rdataset, link);

				if ((rdataset->attributes &
				     DNS_RDATASETATTR_RENDERED) != 0)
				{
					goto next;
				}

				if (((options & DNS_MESSAGERENDER_ORDERED) ==
				     0) &&
				    (sectionid == DNS_SECTION_ADDITIONAL) &&
				    wrong_priority(rdataset, pass,
						   preferred_glue))
				{
					goto next;
				}

				st = *(msg->buffer);

				count = 0;
				if (partial) {
					result = dns_rdataset_towirepartial(
						rdataset, name, msg->cctx,
						msg->buffer, msg->order,
						&msg->order_arg, rd_options,
						&count, NULL);
				} else {
					result = dns_rdataset_towiresorted(
						rdataset, name, msg->cctx,
						msg->buffer, msg->order,
						&msg->order_arg, rd_options,
						&count);
				}

				total += count;

				/*
				 * If out of space, record stats on what we
				 * rendered so far, and return that status.
				 *
				 * XXXMLG Need to change this when
				 * dns_rdataset_towire() can render partial
				 * sets starting at some arbitrary point in the
				 * set.  This will include setting a bit in the
				 * rdataset to indicate that a partial
				 * rendering was done, and some state saved
				 * somewhere (probably in the message struct)
				 * to indicate where to continue from.
				 */
				if (partial && result == ISC_R_NOSPACE) {
					msg->buffer->length += msg->reserved;
					msg->counts[sectionid] += total;
					return result;
				}
				if (result != ISC_R_SUCCESS) {
					INSIST(st.used < 65536);
					dns_compress_rollback(
						msg->cctx, (uint16_t)st.used);
					*(msg->buffer) = st; /* rollback */
					msg->buffer->length += msg->reserved;
					msg->counts[sectionid] += total;
					maybe_clear_ad(msg, sectionid);
					return result;
				}

				/*
				 * If we have rendered non-validated data,
				 * ensure that the AD bit is not set.
				 */
				if (rdataset->trust != dns_trust_secure &&
				    (sectionid == DNS_SECTION_ANSWER ||
				     sectionid == DNS_SECTION_AUTHORITY))
				{
					msg->flags &= ~DNS_MESSAGEFLAG_AD;
				}
				if (OPTOUT(rdataset)) {
					msg->flags &= ~DNS_MESSAGEFLAG_AD;
				}

				update_min_section_ttl(msg, sectionid,
						       rdataset);

				rdataset->attributes |=
					DNS_RDATASETATTR_RENDERED;

			next:
				rdataset = next_rdataset;
			}

			name = next_name;
		}
	} while (--pass != 0);

	msg->buffer->length += msg->reserved;
	msg->counts[sectionid] += total;

	return ISC_R_SUCCESS;
}

void
dns_message_renderheader(dns_message_t *msg, isc_buffer_t *target) {
	uint16_t tmp;
	isc_region_t r;

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(msg->buffer != NULL);
	REQUIRE(target != NULL);

	isc_buffer_availableregion(target, &r);
	REQUIRE(r.length >= DNS_MESSAGE_HEADERLEN);

	isc_buffer_putuint16(target, msg->id);

	tmp = ((msg->opcode << DNS_MESSAGE_OPCODE_SHIFT) &
	       DNS_MESSAGE_OPCODE_MASK);
	tmp |= (msg->rcode & DNS_MESSAGE_RCODE_MASK);
	tmp |= (msg->flags & DNS_MESSAGE_FLAG_MASK);

	INSIST(msg->counts[DNS_SECTION_QUESTION] < 65536 &&
	       msg->counts[DNS_SECTION_ANSWER] < 65536 &&
	       msg->counts[DNS_SECTION_AUTHORITY] < 65536 &&
	       msg->counts[DNS_SECTION_ADDITIONAL] < 65536);

	isc_buffer_putuint16(target, tmp);
	isc_buffer_putuint16(target,
			     (uint16_t)msg->counts[DNS_SECTION_QUESTION]);
	isc_buffer_putuint16(target, (uint16_t)msg->counts[DNS_SECTION_ANSWER]);
	isc_buffer_putuint16(target,
			     (uint16_t)msg->counts[DNS_SECTION_AUTHORITY]);
	isc_buffer_putuint16(target,
			     (uint16_t)msg->counts[DNS_SECTION_ADDITIONAL]);
}

isc_result_t
dns_message_renderend(dns_message_t *msg) {
	isc_buffer_t tmpbuf;
	isc_region_t r;
	int result;
	unsigned int count;

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(msg->buffer != NULL);

	if ((msg->rcode & ~DNS_MESSAGE_RCODE_MASK) != 0 && msg->opt == NULL) {
		/*
		 * We have an extended rcode but are not using EDNS.
		 */
		return DNS_R_FORMERR;
	}

	/*
	 * If we're adding a OPT, TSIG or SIG(0) to a truncated message,
	 * clear all rdatasets from the message except for the question
	 * before adding the OPT, TSIG or SIG(0).  If the question doesn't
	 * fit, don't include it.
	 */
	if ((msg->tsigkey != NULL || msg->sig0key != NULL || msg->opt) &&
	    (msg->flags & DNS_MESSAGEFLAG_TC) != 0)
	{
		isc_buffer_t *buf;

		msgresetnames(msg, DNS_SECTION_ANSWER);
		buf = msg->buffer;
		dns_message_renderreset(msg);
		msg->buffer = buf;
		isc_buffer_clear(msg->buffer);
		isc_buffer_add(msg->buffer, DNS_MESSAGE_HEADERLEN);
		dns_compress_rollback(msg->cctx, 0);
		result = dns_message_rendersection(msg, DNS_SECTION_QUESTION,
						   0);
		if (result != ISC_R_SUCCESS && result != ISC_R_NOSPACE) {
			return result;
		}
	}

	/*
	 * If we've got an OPT record, render it.
	 */
	if (msg->opt != NULL) {
		dns_message_renderrelease(msg, msg->opt_reserved);
		msg->opt_reserved = 0;
		/*
		 * Set the extended rcode.  Cast msg->rcode to dns_ttl_t
		 * so that we do a unsigned shift.
		 */
		msg->opt->ttl &= ~DNS_MESSAGE_EDNSRCODE_MASK;
		msg->opt->ttl |= (((dns_ttl_t)(msg->rcode) << 20) &
				  DNS_MESSAGE_EDNSRCODE_MASK);
		/*
		 * Render.
		 */
		count = 0;
		result = renderset(msg->opt, dns_rootname, msg->cctx,
				   msg->buffer, msg->reserved, 0, &count);
		msg->counts[DNS_SECTION_ADDITIONAL] += count;
		if (result != ISC_R_SUCCESS) {
			return result;
		}
	}

	/*
	 * Deal with EDNS padding.
	 *
	 * padding_off is the length of the OPT with the 0-length PAD
	 * at the end.
	 */
	if (msg->padding_off > 0) {
		unsigned char *cp = isc_buffer_used(msg->buffer);
		unsigned int used, remaining;
		uint16_t len, padsize = 0;

		/* Check PAD */
		if ((cp[-4] != 0) || (cp[-3] != DNS_OPT_PAD) || (cp[-2] != 0) ||
		    (cp[-1] != 0))
		{
			return ISC_R_UNEXPECTED;
		}

		/*
		 * Zero-fill the PAD to the computed size;
		 * patch PAD length and OPT rdlength
		 */

		/* Aligned used length + reserved to padding block */
		used = isc_buffer_usedlength(msg->buffer);
		if (msg->padding != 0) {
			padsize = ((uint16_t)used + msg->reserved) %
				  msg->padding;
		}
		if (padsize != 0) {
			padsize = msg->padding - padsize;
		}
		/* Stay below the available length */
		remaining = isc_buffer_availablelength(msg->buffer);
		if (padsize > remaining) {
			padsize = remaining;
		}

		isc_buffer_add(msg->buffer, padsize);
		memset(cp, 0, padsize);
		cp[-2] = (unsigned char)((padsize & 0xff00U) >> 8);
		cp[-1] = (unsigned char)(padsize & 0x00ffU);
		cp -= msg->padding_off;
		len = ((uint16_t)(cp[-2])) << 8;
		len |= ((uint16_t)(cp[-1]));
		len += padsize;
		cp[-2] = (unsigned char)((len & 0xff00U) >> 8);
		cp[-1] = (unsigned char)(len & 0x00ffU);
	}

	/*
	 * If we're adding a TSIG record, generate and render it.
	 */
	if (msg->tsigkey != NULL) {
		dns_message_renderrelease(msg, msg->sig_reserved);
		msg->sig_reserved = 0;
		result = dns_tsig_sign(msg);
		if (result != ISC_R_SUCCESS) {
			return result;
		}
		count = 0;
		result = renderset(msg->tsig, msg->tsigname, msg->cctx,
				   msg->buffer, msg->reserved, 0, &count);
		msg->counts[DNS_SECTION_ADDITIONAL] += count;
		if (result != ISC_R_SUCCESS) {
			return result;
		}
	}

	/*
	 * If we're adding a SIG(0) record, generate and render it.
	 */
	if (msg->sig0key != NULL) {
		dns_message_renderrelease(msg, msg->sig_reserved);
		msg->sig_reserved = 0;
		result = dns_dnssec_signmessage(msg, msg->sig0key);
		if (result != ISC_R_SUCCESS) {
			return result;
		}
		count = 0;
		/*
		 * Note: dns_rootname is used here, not msg->sig0name, since
		 * the owner name of a SIG(0) is irrelevant, and will not
		 * be set in a message being rendered.
		 */
		result = renderset(msg->sig0, dns_rootname, msg->cctx,
				   msg->buffer, msg->reserved, 0, &count);
		msg->counts[DNS_SECTION_ADDITIONAL] += count;
		if (result != ISC_R_SUCCESS) {
			return result;
		}
	}

	isc_buffer_usedregion(msg->buffer, &r);
	isc_buffer_init(&tmpbuf, r.base, r.length);

	dns_message_renderheader(msg, &tmpbuf);

	msg->buffer = NULL; /* forget about this buffer only on success XXX */

	return ISC_R_SUCCESS;
}

void
dns_message_renderreset(dns_message_t *msg) {
	/*
	 * Reset the message so that it may be rendered again.
	 */

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(msg->from_to_wire == DNS_MESSAGE_INTENTRENDER);

	msg->buffer = NULL;

	for (size_t i = 0; i < DNS_SECTION_MAX; i++) {
		dns_name_t *name = NULL;

		msg->cursors[i] = NULL;
		msg->counts[i] = 0;
		ISC_LIST_FOREACH (msg->sections[i], name, link) {
			dns_rdataset_t *rds = NULL;
			ISC_LIST_FOREACH (name->list, rds, link) {
				rds->attributes &= ~DNS_RDATASETATTR_RENDERED;
			}
		}
	}
	if (msg->tsigname != NULL) {
		dns_message_puttempname(msg, &msg->tsigname);
	}
	if (msg->tsig != NULL) {
		dns__message_putassociatedrdataset(msg, &msg->tsig);
	}
	if (msg->sig0name != NULL) {
		dns_message_puttempname(msg, &msg->sig0name);
	}
	if (msg->sig0 != NULL) {
		dns__message_putassociatedrdataset(msg, &msg->sig0);
	}
}

isc_result_t
dns_message_firstname(dns_message_t *msg, dns_section_t section) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(VALID_NAMED_SECTION(section));

	msg->cursors[section] = ISC_LIST_HEAD(msg->sections[section]);

	if (msg->cursors[section] == NULL) {
		return ISC_R_NOMORE;
	}

	return ISC_R_SUCCESS;
}

isc_result_t
dns_message_nextname(dns_message_t *msg, dns_section_t section) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(VALID_NAMED_SECTION(section));
	REQUIRE(msg->cursors[section] != NULL);

	msg->cursors[section] = ISC_LIST_NEXT(msg->cursors[section], link);

	if (msg->cursors[section] == NULL) {
		return ISC_R_NOMORE;
	}

	return ISC_R_SUCCESS;
}

void
dns_message_currentname(dns_message_t *msg, dns_section_t section,
			dns_name_t **name) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(VALID_NAMED_SECTION(section));
	REQUIRE(name != NULL && *name == NULL);
	REQUIRE(msg->cursors[section] != NULL);

	*name = msg->cursors[section];
}

isc_result_t
dns_message_findname(dns_message_t *msg, dns_section_t section,
		     const dns_name_t *target, dns_rdatatype_t type,
		     dns_rdatatype_t covers, dns_name_t **name,
		     dns_rdataset_t **rdataset) {
	dns_name_t *foundname = NULL;
	isc_result_t result;

	/*
	 * XXX These requirements are probably too intensive, especially
	 * where things can be NULL, but as they are they ensure that if
	 * something is NON-NULL, indicating that the caller expects it
	 * to be filled in, that we can in fact fill it in.
	 */
	REQUIRE(msg != NULL);
	REQUIRE(VALID_NAMED_SECTION(section));
	REQUIRE(target != NULL);
	REQUIRE(name == NULL || *name == NULL);

	if (type == dns_rdatatype_any) {
		REQUIRE(rdataset == NULL);
	} else {
		REQUIRE(rdataset == NULL || *rdataset == NULL);
	}

	result = findname(&foundname, target, &msg->sections[section]);

	if (result == ISC_R_NOTFOUND) {
		return DNS_R_NXDOMAIN;
	} else if (result != ISC_R_SUCCESS) {
		return result;
	}

	SET_IF_NOT_NULL(name, foundname);

	/*
	 * And now look for the type.
	 */
	if (type == dns_rdatatype_any) {
		return ISC_R_SUCCESS;
	}

	result = dns_message_findtype(foundname, type, covers, rdataset);
	if (result == ISC_R_NOTFOUND) {
		return DNS_R_NXRRSET;
	}

	return result;
}

void
dns_message_addname(dns_message_t *msg, dns_name_t *name,
		    dns_section_t section) {
	REQUIRE(msg != NULL);
	REQUIRE(msg->from_to_wire == DNS_MESSAGE_INTENTRENDER);
	REQUIRE(dns_name_isabsolute(name));
	REQUIRE(VALID_NAMED_SECTION(section));

	ISC_LIST_APPEND(msg->sections[section], name, link);
}

void
dns_message_removename(dns_message_t *msg, dns_name_t *name,
		       dns_section_t section) {
	REQUIRE(msg != NULL);
	REQUIRE(msg->from_to_wire == DNS_MESSAGE_INTENTRENDER);
	REQUIRE(dns_name_isabsolute(name));
	REQUIRE(VALID_NAMED_SECTION(section));

	ISC_LIST_UNLINK(msg->sections[section], name, link);
}

void
dns_message_gettempname(dns_message_t *msg, dns_name_t **item) {
	dns_fixedname_t *fn = NULL;

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(item != NULL && *item == NULL);

	fn = isc_mempool_get(msg->namepool);
	*item = dns_fixedname_initname(fn);
}

void
dns_message_gettemprdata(dns_message_t *msg, dns_rdata_t **item) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(item != NULL && *item == NULL);

	*item = newrdata(msg);
}

void
dns_message_gettemprdataset(dns_message_t *msg, dns_rdataset_t **item) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(item != NULL && *item == NULL);

	*item = isc_mempool_get(msg->rdspool);
	dns_rdataset_init(*item);
}

void
dns_message_gettemprdatalist(dns_message_t *msg, dns_rdatalist_t **item) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(item != NULL && *item == NULL);

	*item = newrdatalist(msg);
}

void
dns_message_puttempname(dns_message_t *msg, dns_name_t **itemp) {
	dns_name_t *item = NULL;

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(itemp != NULL && *itemp != NULL);

	item = *itemp;
	*itemp = NULL;

	REQUIRE(!ISC_LINK_LINKED(item, link));
	REQUIRE(ISC_LIST_HEAD(item->list) == NULL);

	if (item->hashmap != NULL) {
		isc_hashmap_destroy(&item->hashmap);
	}

	/*
	 * we need to check this in case dns_name_dup() was used.
	 */
	if (dns_name_dynamic(item)) {
		dns_name_free(item, msg->mctx);
	}

	/*
	 * 'name' is the first field in dns_fixedname_t, so putting
	 * back the address of name is the same as putting back
	 * the fixedname.
	 */
	isc_mempool_put(msg->namepool, item);
}

void
dns_message_puttemprdata(dns_message_t *msg, dns_rdata_t **item) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(item != NULL && *item != NULL);

	releaserdata(msg, *item);
	*item = NULL;
}

static void
dns__message_putassociatedrdataset(dns_message_t *msg, dns_rdataset_t **item) {
	dns_rdataset_disassociate(*item);
	dns_message_puttemprdataset(msg, item);
}

void
dns_message_puttemprdataset(dns_message_t *msg, dns_rdataset_t **item) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(item != NULL && *item != NULL);

	REQUIRE(!dns_rdataset_isassociated(*item));
	isc_mempool_put(msg->rdspool, *item);
	*item = NULL;
}

void
dns_message_puttemprdatalist(dns_message_t *msg, dns_rdatalist_t **item) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(item != NULL && *item != NULL);

	releaserdatalist(msg, *item);
	*item = NULL;
}

isc_result_t
dns_message_peekheader(isc_buffer_t *source, dns_messageid_t *idp,
		       unsigned int *flagsp) {
	isc_region_t r;
	isc_buffer_t buffer;
	dns_messageid_t id;
	unsigned int flags;

	REQUIRE(source != NULL);

	buffer = *source;

	isc_buffer_remainingregion(&buffer, &r);
	if (r.length < DNS_MESSAGE_HEADERLEN) {
		return ISC_R_UNEXPECTEDEND;
	}

	id = isc_buffer_getuint16(&buffer);
	flags = isc_buffer_getuint16(&buffer);
	flags &= DNS_MESSAGE_FLAG_MASK;

	SET_IF_NOT_NULL(flagsp, flags);
	SET_IF_NOT_NULL(idp, id);

	return ISC_R_SUCCESS;
}

isc_result_t
dns_message_reply(dns_message_t *msg, bool want_question_section) {
	unsigned int clear_from;
	isc_result_t result;

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE((msg->flags & DNS_MESSAGEFLAG_QR) == 0);

	if (!msg->header_ok) {
		return DNS_R_FORMERR;
	}
	if (msg->opcode != dns_opcode_query && msg->opcode != dns_opcode_notify)
	{
		want_question_section = false;
	}
	if (msg->opcode == dns_opcode_update) {
		clear_from = DNS_SECTION_PREREQUISITE;
	} else if (want_question_section) {
		if (!msg->question_ok) {
			return DNS_R_FORMERR;
		}
		clear_from = DNS_SECTION_ANSWER;
	} else {
		clear_from = DNS_SECTION_QUESTION;
	}
	msg->from_to_wire = DNS_MESSAGE_INTENTRENDER;
	msgresetnames(msg, clear_from);
	msgresetopt(msg);
	msgresetsigs(msg, true);
	msginitprivate(msg);
	/*
	 * We now clear most flags and then set QR, ensuring that the
	 * reply's flags will be in a reasonable state.
	 */
	if (msg->opcode == dns_opcode_query) {
		msg->flags &= DNS_MESSAGE_REPLYPRESERVE;
	} else {
		msg->flags = 0;
	}
	msg->flags |= DNS_MESSAGEFLAG_QR;

	/*
	 * This saves the query TSIG status, if the query was signed, and
	 * reserves space in the reply for the TSIG.
	 */
	if (msg->tsigkey != NULL) {
		unsigned int otherlen = 0;
		msg->querytsigstatus = msg->tsigstatus;
		msg->tsigstatus = dns_rcode_noerror;
		if (msg->querytsigstatus == dns_tsigerror_badtime) {
			otherlen = 6;
		}
		msg->sig_reserved = spacefortsig(msg->tsigkey, otherlen);
		result = dns_message_renderreserve(msg, msg->sig_reserved);
		if (result != ISC_R_SUCCESS) {
			msg->sig_reserved = 0;
			return result;
		}
	}
	if (msg->saved.base != NULL) {
		msg->query.base = msg->saved.base;
		msg->query.length = msg->saved.length;
		msg->free_query = msg->free_saved;
		msg->saved.base = NULL;
		msg->saved.length = 0;
		msg->free_saved = 0;
	}

	return ISC_R_SUCCESS;
}

dns_rdataset_t *
dns_message_getopt(dns_message_t *msg) {
	/*
	 * Get the OPT record for 'msg'.
	 */

	REQUIRE(DNS_MESSAGE_VALID(msg));

	return msg->opt;
}

isc_result_t
dns_message_setopt(dns_message_t *msg, dns_rdataset_t *opt) {
	isc_result_t result;
	dns_rdata_t rdata = DNS_RDATA_INIT;

	/*
	 * Set the OPT record for 'msg'.
	 */

	/*
	 * The space required for an OPT record is:
	 *
	 *	1 byte for the name
	 *	2 bytes for the type
	 *	2 bytes for the class
	 *	4 bytes for the ttl
	 *	2 bytes for the rdata length
	 * ---------------------------------
	 *     11 bytes
	 *
	 * plus the length of the rdata.
	 */

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(opt == NULL || DNS_RDATASET_VALID(opt));
	REQUIRE(opt == NULL || opt->type == dns_rdatatype_opt);
	REQUIRE(msg->from_to_wire == DNS_MESSAGE_INTENTRENDER);
	REQUIRE(msg->state == DNS_SECTION_ANY);

	msgresetopt(msg);

	if (opt == NULL) {
		return ISC_R_SUCCESS;
	}

	result = dns_rdataset_first(opt);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}
	dns_rdataset_current(opt, &rdata);
	msg->opt_reserved = 11 + rdata.length;
	result = dns_message_renderreserve(msg, msg->opt_reserved);
	if (result != ISC_R_SUCCESS) {
		msg->opt_reserved = 0;
		goto cleanup;
	}

	msg->opt = opt;

	return ISC_R_SUCCESS;

cleanup:
	dns__message_putassociatedrdataset(msg, &opt);
	return result;
}

dns_rdataset_t *
dns_message_gettsig(dns_message_t *msg, const dns_name_t **owner) {
	/*
	 * Get the TSIG record and owner for 'msg'.
	 */

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(owner == NULL || *owner == NULL);

	SET_IF_NOT_NULL(owner, msg->tsigname);
	return msg->tsig;
}

isc_result_t
dns_message_settsigkey(dns_message_t *msg, dns_tsigkey_t *key) {
	isc_result_t result;

	/*
	 * Set the TSIG key for 'msg'
	 */

	REQUIRE(DNS_MESSAGE_VALID(msg));

	if (key == NULL && msg->tsigkey != NULL) {
		if (msg->sig_reserved != 0) {
			dns_message_renderrelease(msg, msg->sig_reserved);
			msg->sig_reserved = 0;
		}
		dns_tsigkey_detach(&msg->tsigkey);
	}
	if (key != NULL) {
		REQUIRE(msg->tsigkey == NULL && msg->sig0key == NULL);
		dns_tsigkey_attach(key, &msg->tsigkey);
		if (msg->from_to_wire == DNS_MESSAGE_INTENTRENDER) {
			msg->sig_reserved = spacefortsig(msg->tsigkey, 0);
			result = dns_message_renderreserve(msg,
							   msg->sig_reserved);
			if (result != ISC_R_SUCCESS) {
				dns_tsigkey_detach(&msg->tsigkey);
				msg->sig_reserved = 0;
				return result;
			}
		}
	}
	return ISC_R_SUCCESS;
}

dns_tsigkey_t *
dns_message_gettsigkey(dns_message_t *msg) {
	/*
	 * Get the TSIG key for 'msg'
	 */

	REQUIRE(DNS_MESSAGE_VALID(msg));

	return msg->tsigkey;
}

void
dns_message_setquerytsig(dns_message_t *msg, isc_buffer_t *querytsig) {
	dns_rdata_t *rdata = NULL;
	dns_rdatalist_t *list = NULL;
	dns_rdataset_t *set = NULL;
	isc_buffer_t *buf = NULL;
	isc_region_t r;

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(msg->querytsig == NULL);

	if (querytsig == NULL) {
		return;
	}

	dns_message_gettemprdata(msg, &rdata);

	dns_message_gettemprdatalist(msg, &list);
	dns_message_gettemprdataset(msg, &set);

	isc_buffer_usedregion(querytsig, &r);
	isc_buffer_allocate(msg->mctx, &buf, r.length);
	isc_buffer_putmem(buf, r.base, r.length);
	isc_buffer_usedregion(buf, &r);
	dns_rdata_init(rdata);
	dns_rdata_fromregion(rdata, dns_rdataclass_any, dns_rdatatype_tsig, &r);
	dns_message_takebuffer(msg, &buf);
	ISC_LIST_APPEND(list->rdata, rdata, link);
	dns_rdatalist_tordataset(list, set);

	msg->querytsig = set;
}

isc_result_t
dns_message_getquerytsig(dns_message_t *msg, isc_mem_t *mctx,
			 isc_buffer_t **querytsig) {
	isc_result_t result;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	isc_region_t r;

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(mctx != NULL);
	REQUIRE(querytsig != NULL && *querytsig == NULL);

	if (msg->tsig == NULL) {
		return ISC_R_SUCCESS;
	}

	result = dns_rdataset_first(msg->tsig);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	dns_rdataset_current(msg->tsig, &rdata);
	dns_rdata_toregion(&rdata, &r);

	isc_buffer_allocate(mctx, querytsig, r.length);
	isc_buffer_putmem(*querytsig, r.base, r.length);
	return ISC_R_SUCCESS;
}

dns_rdataset_t *
dns_message_getsig0(dns_message_t *msg, const dns_name_t **owner) {
	/*
	 * Get the SIG(0) record for 'msg'.
	 */

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(owner == NULL || *owner == NULL);

	if (msg->sig0 != NULL && owner != NULL) {
		/* If dns_message_getsig0 is called on a rendered message
		 * after the SIG(0) has been applied, we need to return the
		 * root name, not NULL.
		 */
		if (msg->sig0name == NULL) {
			*owner = dns_rootname;
		} else {
			*owner = msg->sig0name;
		}
	}
	return msg->sig0;
}

isc_result_t
dns_message_setsig0key(dns_message_t *msg, dst_key_t *key) {
	isc_region_t r;
	unsigned int x;
	isc_result_t result;

	/*
	 * Set the SIG(0) key for 'msg'
	 */

	/*
	 * The space required for an SIG(0) record is:
	 *
	 *	1 byte for the name
	 *	2 bytes for the type
	 *	2 bytes for the class
	 *	4 bytes for the ttl
	 *	2 bytes for the type covered
	 *	1 byte for the algorithm
	 *	1 bytes for the labels
	 *	4 bytes for the original ttl
	 *	4 bytes for the signature expiration
	 *	4 bytes for the signature inception
	 *	2 bytes for the key tag
	 *	n bytes for the signer's name
	 *	x bytes for the signature
	 * ---------------------------------
	 *     27 + n + x bytes
	 */
	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(msg->from_to_wire == DNS_MESSAGE_INTENTRENDER);
	REQUIRE(msg->state == DNS_SECTION_ANY);

	if (key != NULL) {
		REQUIRE(msg->sig0key == NULL && msg->tsigkey == NULL);
		dns_name_toregion(dst_key_name(key), &r);
		result = dst_key_sigsize(key, &x);
		if (result != ISC_R_SUCCESS) {
			msg->sig_reserved = 0;
			return result;
		}
		msg->sig_reserved = 27 + r.length + x;
		result = dns_message_renderreserve(msg, msg->sig_reserved);
		if (result != ISC_R_SUCCESS) {
			msg->sig_reserved = 0;
			return result;
		}
		msg->sig0key = key;
	}
	return ISC_R_SUCCESS;
}

dst_key_t *
dns_message_getsig0key(dns_message_t *msg) {
	/*
	 * Get the SIG(0) key for 'msg'
	 */

	REQUIRE(DNS_MESSAGE_VALID(msg));

	return msg->sig0key;
}

void
dns_message_takebuffer(dns_message_t *msg, isc_buffer_t **buffer) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(buffer != NULL);
	REQUIRE(ISC_BUFFER_VALID(*buffer));

	ISC_LIST_APPEND(msg->cleanup, *buffer, link);
	*buffer = NULL;
}

isc_result_t
dns_message_signer(dns_message_t *msg, dns_name_t *signer) {
	isc_result_t result = ISC_R_SUCCESS;
	dns_rdata_t rdata = DNS_RDATA_INIT;

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(signer != NULL);
	REQUIRE(msg->from_to_wire == DNS_MESSAGE_INTENTPARSE);

	if (msg->tsig == NULL && msg->sig0 == NULL) {
		return ISC_R_NOTFOUND;
	}

	if (msg->verify_attempted == 0) {
		return DNS_R_NOTVERIFIEDYET;
	}

	if (!dns_name_hasbuffer(signer)) {
		isc_buffer_t *dynbuf = NULL;
		isc_buffer_allocate(msg->mctx, &dynbuf, 512);
		dns_name_setbuffer(signer, dynbuf);
		dns_message_takebuffer(msg, &dynbuf);
	}

	if (msg->sig0 != NULL) {
		dns_rdata_sig_t sig;

		result = dns_rdataset_first(msg->sig0);
		INSIST(result == ISC_R_SUCCESS);
		dns_rdataset_current(msg->sig0, &rdata);

		result = dns_rdata_tostruct(&rdata, &sig, NULL);
		if (result != ISC_R_SUCCESS) {
			return result;
		}

		if (msg->verified_sig && msg->sig0status == dns_rcode_noerror) {
			result = ISC_R_SUCCESS;
		} else {
			result = DNS_R_SIGINVALID;
		}
		dns_name_clone(&sig.signer, signer);
		dns_rdata_freestruct(&sig);
	} else {
		const dns_name_t *identity;
		dns_rdata_any_tsig_t tsig;

		result = dns_rdataset_first(msg->tsig);
		INSIST(result == ISC_R_SUCCESS);
		dns_rdataset_current(msg->tsig, &rdata);

		result = dns_rdata_tostruct(&rdata, &tsig, NULL);
		INSIST(result == ISC_R_SUCCESS);
		if (msg->verified_sig && msg->tsigstatus == dns_rcode_noerror &&
		    tsig.error == dns_rcode_noerror)
		{
			result = ISC_R_SUCCESS;
		} else if ((!msg->verified_sig) ||
			   (msg->tsigstatus != dns_rcode_noerror))
		{
			result = DNS_R_TSIGVERIFYFAILURE;
		} else {
			INSIST(tsig.error != dns_rcode_noerror);
			result = DNS_R_TSIGERRORSET;
		}
		dns_rdata_freestruct(&tsig);

		if (msg->tsigkey == NULL) {
			/*
			 * If msg->tsigstatus & tsig.error are both
			 * dns_rcode_noerror, the message must have been
			 * verified, which means msg->tsigkey will be
			 * non-NULL.
			 */
			INSIST(result != ISC_R_SUCCESS);
		} else {
			identity = dns_tsigkey_identity(msg->tsigkey);
			if (identity == NULL) {
				if (result == ISC_R_SUCCESS) {
					result = DNS_R_NOIDENTITY;
				}
				identity = msg->tsigkey->name;
			}
			dns_name_clone(identity, signer);
		}
	}

	return result;
}

void
dns_message_resetsig(dns_message_t *msg) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	msg->verified_sig = 0;
	msg->verify_attempted = 0;
	msg->tsigstatus = dns_rcode_noerror;
	msg->sig0status = dns_rcode_noerror;
	msg->timeadjust = 0;
	if (msg->tsigkey != NULL) {
		dns_tsigkey_detach(&msg->tsigkey);
		msg->tsigkey = NULL;
	}
}

#ifdef SKAN_MSG_DEBUG
void
dns_message_dumpsig(dns_message_t *msg, char *txt1) {
	dns_rdata_t querytsigrdata = DNS_RDATA_INIT;
	dns_rdata_any_tsig_t querytsig;
	isc_result_t result;

	if (msg->tsig != NULL) {
		result = dns_rdataset_first(msg->tsig);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
		dns_rdataset_current(msg->tsig, &querytsigrdata);
		result = dns_rdata_tostruct(&querytsigrdata, &querytsig, NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
		hexdump(txt1, "TSIG", querytsig.signature, querytsig.siglen);
	}

	if (msg->querytsig != NULL) {
		result = dns_rdataset_first(msg->querytsig);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
		dns_rdataset_current(msg->querytsig, &querytsigrdata);
		result = dns_rdata_tostruct(&querytsigrdata, &querytsig, NULL);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
		hexdump(txt1, "QUERYTSIG", querytsig.signature,
			querytsig.siglen);
	}
}
#endif /* ifdef SKAN_MSG_DEBUG */

static void
checksig_done(void *arg);

static void
checksig_run(void *arg) {
	checksig_ctx_t *chsigctx = arg;

	chsigctx->result = dns_message_checksig(chsigctx->msg, chsigctx->view);

	isc_async_run(chsigctx->loop, checksig_done, chsigctx);
}

static void
checksig_done(void *arg) {
	checksig_ctx_t *chsigctx = arg;
	dns_message_t *msg = chsigctx->msg;

	chsigctx->cb(chsigctx->cbarg, chsigctx->result);

	dns_view_detach(&chsigctx->view);
	isc_loop_detach(&chsigctx->loop);
	isc_mem_put(msg->mctx, chsigctx, sizeof(*chsigctx));
	dns_message_detach(&msg);
}

isc_result_t
dns_message_checksig_async(dns_message_t *msg, dns_view_t *view,
			   isc_loop_t *loop, dns_message_cb_t cb, void *cbarg) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(view != NULL);
	REQUIRE(loop != NULL);
	REQUIRE(cb != NULL);

	checksig_ctx_t *chsigctx = isc_mem_get(msg->mctx, sizeof(*chsigctx));
	*chsigctx = (checksig_ctx_t){
		.cb = cb,
		.cbarg = cbarg,
		.result = ISC_R_UNSET,
		.loop = isc_loop_ref(loop),
	};
	dns_message_attach(msg, &chsigctx->msg);
	dns_view_attach(view, &chsigctx->view);

	dns_message_clonebuffer(msg);
	isc_helper_run(loop, checksig_run, chsigctx);

	return DNS_R_WAIT;
}

isc_result_t
dns_message_checksig(dns_message_t *msg, dns_view_t *view) {
	isc_buffer_t b, msgb;

	REQUIRE(DNS_MESSAGE_VALID(msg));

	if (msg->tsigkey == NULL && msg->tsig == NULL && msg->sig0 == NULL) {
		return ISC_R_SUCCESS;
	}

	INSIST(msg->saved.base != NULL);
	isc_buffer_init(&msgb, msg->saved.base, msg->saved.length);
	isc_buffer_add(&msgb, msg->saved.length);
	if (msg->tsigkey != NULL || msg->tsig != NULL) {
#ifdef SKAN_MSG_DEBUG
		dns_message_dumpsig(msg, "dns_message_checksig#1");
#endif /* ifdef SKAN_MSG_DEBUG */
		if (view != NULL) {
			return dns_view_checksig(view, &msgb, msg);
		} else {
			return dns_tsig_verify(&msgb, msg, NULL, NULL);
		}
	} else {
		dns_rdata_t rdata = DNS_RDATA_INIT;
		dns_rdata_sig_t sig;
		dns_rdataset_t keyset;
		isc_result_t result;
		uint32_t key_checks, message_checks;

		result = dns_rdataset_first(msg->sig0);
		INSIST(result == ISC_R_SUCCESS);
		dns_rdataset_current(msg->sig0, &rdata);

		/*
		 * This can occur when the message is a dynamic update, since
		 * the rdata length checking is relaxed.  This should not
		 * happen in a well-formed message, since the SIG(0) is only
		 * looked for in the additional section, and the dynamic update
		 * meta-records are in the prerequisite and update sections.
		 */
		if (rdata.length == 0) {
			return ISC_R_UNEXPECTEDEND;
		}

		result = dns_rdata_tostruct(&rdata, &sig, NULL);
		if (result != ISC_R_SUCCESS) {
			return result;
		}

		dns_rdataset_init(&keyset);
		if (view == NULL) {
			result = DNS_R_KEYUNAUTHORIZED;
			goto freesig;
		}
		result = dns_view_simplefind(view, &sig.signer,
					     dns_rdatatype_key /* SIG(0) */, 0,
					     0, false, &keyset, NULL);

		if (result != ISC_R_SUCCESS) {
			result = DNS_R_KEYUNAUTHORIZED;
			goto freesig;
		} else if (keyset.trust < dns_trust_ultimate) {
			result = DNS_R_KEYUNAUTHORIZED;
			goto freesig;
		}
		result = dns_rdataset_first(&keyset);
		INSIST(result == ISC_R_SUCCESS);

		/*
		 * In order to protect from a possible DoS attack, this function
		 * supports limitations on how many keyid checks and how many
		 * key checks (message verifications using a matched key) are
		 * going to be allowed.
		 */
		const uint32_t max_key_checks =
			view->sig0key_checks_limit > 0
				? view->sig0key_checks_limit
				: UINT32_MAX;
		const uint32_t max_message_checks =
			view->sig0message_checks_limit > 0
				? view->sig0message_checks_limit
				: UINT32_MAX;

		for (key_checks = 0, message_checks = 0;
		     result == ISC_R_SUCCESS && key_checks < max_key_checks &&
		     message_checks < max_message_checks;
		     key_checks++, result = dns_rdataset_next(&keyset))
		{
			dst_key_t *key = NULL;

			dns_rdata_reset(&rdata);
			dns_rdataset_current(&keyset, &rdata);
			isc_buffer_init(&b, rdata.data, rdata.length);
			isc_buffer_add(&b, rdata.length);

			result = dst_key_fromdns(&sig.signer, rdata.rdclass, &b,
						 view->mctx, &key);
			if (result != ISC_R_SUCCESS) {
				continue;
			}
			if (dst_key_alg(key) != sig.algorithm ||
			    dst_key_id(key) != sig.keyid ||
			    !(dst_key_proto(key) == DNS_KEYPROTO_DNSSEC ||
			      dst_key_proto(key) == DNS_KEYPROTO_ANY))
			{
				dst_key_free(&key);
				continue;
			}
			result = dns_dnssec_verifymessage(&msgb, msg, key);
			dst_key_free(&key);
			if (result == ISC_R_SUCCESS) {
				break;
			}
			message_checks++;
		}
		if (result == ISC_R_NOMORE) {
			result = DNS_R_KEYUNAUTHORIZED;
		} else if (key_checks == max_key_checks) {
			isc_log_write(dns_lctx, ISC_LOGCATEGORY_GENERAL,
				      DNS_LOGMODULE_MESSAGE, ISC_LOG_DEBUG(3),
				      "sig0key-checks-limit reached when "
				      "trying to check a message signature");
			result = DNS_R_KEYUNAUTHORIZED;
		} else if (message_checks == max_message_checks) {
			isc_log_write(dns_lctx, ISC_LOGCATEGORY_GENERAL,
				      DNS_LOGMODULE_MESSAGE, ISC_LOG_DEBUG(3),
				      "sig0message-checks-limit reached when "
				      "trying to check a message signature");
			result = DNS_R_KEYUNAUTHORIZED;
		}

	freesig:
		if (dns_rdataset_isassociated(&keyset)) {
			dns_rdataset_disassociate(&keyset);
		}
		dns_rdata_freestruct(&sig);
		return result;
	}
}

#define INDENT(sp)                                                           \
	do {                                                                 \
		unsigned int __i;                                            \
		dns_masterstyle_flags_t __flags = dns_master_styleflags(sp); \
		if ((__flags & DNS_STYLEFLAG_INDENT) == 0ULL &&              \
		    (__flags & DNS_STYLEFLAG_YAML) == 0ULL)                  \
		{                                                            \
			break;                                               \
		}                                                            \
		for (__i = 0; __i < msg->indent.count; __i++) {              \
			ADD_STRING(target, msg->indent.string);              \
		}                                                            \
	} while (0)

isc_result_t
dns_message_sectiontotext(dns_message_t *msg, dns_section_t section,
			  const dns_master_style_t *style,
			  dns_messagetextflag_t flags, isc_buffer_t *target) {
	dns_name_t empty_name;
	isc_result_t result = ISC_R_SUCCESS;
	bool seensoa = false;
	size_t saved_count;
	dns_masterstyle_flags_t sflags;

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(target != NULL);
	REQUIRE(VALID_NAMED_SECTION(section));

	saved_count = msg->indent.count;

	if (ISC_LIST_EMPTY(msg->sections[section])) {
		goto cleanup;
	}

	sflags = dns_master_styleflags(style);

	INDENT(style);
	if ((sflags & DNS_STYLEFLAG_YAML) != 0) {
		if (msg->opcode != dns_opcode_update) {
			ADD_STRING(target, sectiontext[section]);
		} else {
			ADD_STRING(target, updsectiontext[section]);
		}
		ADD_STRING(target, "_SECTION:\n");
	} else if ((flags & DNS_MESSAGETEXTFLAG_NOCOMMENTS) == 0) {
		ADD_STRING(target, ";; ");
		if (msg->opcode != dns_opcode_update) {
			ADD_STRING(target, sectiontext[section]);
		} else {
			ADD_STRING(target, updsectiontext[section]);
		}
		ADD_STRING(target, " SECTION:\n");
	}

	dns_name_init(&empty_name, NULL);
	result = dns_message_firstname(msg, section);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}
	if ((sflags & DNS_STYLEFLAG_YAML) != 0) {
		msg->indent.count++;
	}
	do {
		dns_name_t *name = NULL;
		dns_message_currentname(msg, section, &name);

		dns_rdataset_t *rds = NULL;
		ISC_LIST_FOREACH (name->list, rds, link) {
			if (section == DNS_SECTION_ANSWER &&
			    rds->type == dns_rdatatype_soa)
			{
				if ((flags & DNS_MESSAGETEXTFLAG_OMITSOA) != 0)
				{
					continue;
				}
				if (seensoa &&
				    (flags & DNS_MESSAGETEXTFLAG_ONESOA) != 0)
				{
					continue;
				}
				seensoa = true;
			}
			if (section == DNS_SECTION_QUESTION) {
				INDENT(style);
				if ((sflags & DNS_STYLEFLAG_YAML) == 0) {
					ADD_STRING(target, ";");
				}
				result = dns_master_questiontotext(
					name, rds, style, target);
			} else {
				result = dns_master_rdatasettotext(
					name, rds, style, &msg->indent, target);
			}
			if (result != ISC_R_SUCCESS) {
				goto cleanup;
			}
		}
		result = dns_message_nextname(msg, section);
	} while (result == ISC_R_SUCCESS);
	if ((sflags & DNS_STYLEFLAG_YAML) != 0) {
		msg->indent.count--;
	}
	if ((flags & DNS_MESSAGETEXTFLAG_NOHEADERS) == 0 &&
	    (flags & DNS_MESSAGETEXTFLAG_NOCOMMENTS) == 0 &&
	    (sflags & DNS_STYLEFLAG_YAML) == 0)
	{
		INDENT(style);
		ADD_STRING(target, "\n");
	}
	if (result == ISC_R_NOMORE) {
		result = ISC_R_SUCCESS;
	}

cleanup:
	msg->indent.count = saved_count;
	return result;
}

static isc_result_t
render_ecs(isc_buffer_t *ecsbuf, isc_buffer_t *target) {
	int i;
	char addr[16] = { 0 }, addr_text[64];
	uint16_t family;
	uint8_t addrlen, addrbytes, scopelen;
	isc_result_t result;

	/*
	 * Note: This routine needs to handle malformed ECS options.
	 */

	if (isc_buffer_remaininglength(ecsbuf) < 4) {
		return DNS_R_OPTERR;
	}
	family = isc_buffer_getuint16(ecsbuf);
	addrlen = isc_buffer_getuint8(ecsbuf);
	scopelen = isc_buffer_getuint8(ecsbuf);

	addrbytes = (addrlen + 7) / 8;
	if (isc_buffer_remaininglength(ecsbuf) < addrbytes) {
		return DNS_R_OPTERR;
	}

	if (addrbytes > sizeof(addr)) {
		return DNS_R_OPTERR;
	}

	for (i = 0; i < addrbytes; i++) {
		addr[i] = isc_buffer_getuint8(ecsbuf);
	}

	switch (family) {
	case 0:
		if (addrlen != 0U || scopelen != 0U) {
			return DNS_R_OPTERR;
		}
		strlcpy(addr_text, "0", sizeof(addr_text));
		break;
	case 1:
		if (addrlen > 32 || scopelen > 32) {
			return DNS_R_OPTERR;
		}
		inet_ntop(AF_INET, addr, addr_text, sizeof(addr_text));
		break;
	case 2:
		if (addrlen > 128 || scopelen > 128) {
			return DNS_R_OPTERR;
		}
		inet_ntop(AF_INET6, addr, addr_text, sizeof(addr_text));
		break;
	default:
		return DNS_R_OPTERR;
	}

	ADD_STRING(target, " ");
	ADD_STRING(target, addr_text);
	snprintf(addr_text, sizeof(addr_text), "/%d/%d", addrlen, scopelen);
	ADD_STRING(target, addr_text);

	result = ISC_R_SUCCESS;

cleanup:
	return result;
}

static isc_result_t
render_llq(isc_buffer_t *optbuf, dns_message_t *msg,
	   const dns_master_style_t *style, isc_buffer_t *target) {
	char buf[sizeof("18446744073709551615")]; /* 2^64-1 */
	isc_result_t result = ISC_R_SUCCESS;
	uint32_t u;
	uint64_t q;
	const char *sep1 = " ", *sep2 = ", ";
	size_t count = msg->indent.count;
	bool yaml = false;

	if ((dns_master_styleflags(style) & DNS_STYLEFLAG_YAML) != 0) {
		sep1 = sep2 = "\n";
		msg->indent.count++;
		yaml = true;
	}

	u = isc_buffer_getuint16(optbuf);
	ADD_STRING(target, sep1);
	INDENT(style);
	if (yaml) {
		ADD_STRING(target, "LLQ-VERSION: ");
	} else {
		ADD_STRING(target, "Version: ");
	}
	snprintf(buf, sizeof(buf), "%u", u);
	ADD_STRING(target, buf);

	u = isc_buffer_getuint16(optbuf);
	ADD_STRING(target, sep2);
	INDENT(style);
	if (yaml) {
		ADD_STRING(target, "LLQ-OPCODE: ");
	} else {
		ADD_STRING(target, "Opcode: ");
	}
	snprintf(buf, sizeof(buf), "%u", u);
	ADD_STRING(target, buf);

	u = isc_buffer_getuint16(optbuf);
	ADD_STRING(target, sep2);
	INDENT(style);
	if (yaml) {
		ADD_STRING(target, "LLQ-ERROR: ");
	} else {
		ADD_STRING(target, "Error: ");
	}
	snprintf(buf, sizeof(buf), "%u", u);
	ADD_STRING(target, buf);

	q = isc_buffer_getuint32(optbuf);
	q <<= 32;
	q |= isc_buffer_getuint32(optbuf);
	ADD_STRING(target, sep2);
	INDENT(style);
	if (yaml) {
		ADD_STRING(target, "LLQ-ID: ");
	} else {
		ADD_STRING(target, "Identifier: ");
	}
	snprintf(buf, sizeof(buf), "%" PRIu64, q);
	ADD_STRING(target, buf);

	u = isc_buffer_getuint32(optbuf);
	ADD_STRING(target, sep2);
	INDENT(style);
	if (yaml) {
		ADD_STRING(target, "LLQ-LEASE: ");
	} else {
		ADD_STRING(target, "Lifetime: ");
	}
	snprintf(buf, sizeof(buf), "%u", u);
	ADD_STRING(target, buf);

cleanup:
	msg->indent.count = count;
	return result;
}

static isc_result_t
put_yamlstr(isc_buffer_t *target, unsigned char *namebuf, size_t len,
	    bool utfok) {
	isc_result_t result = ISC_R_SUCCESS;

	for (size_t i = 0; i < len; i++) {
		if (isprint(namebuf[i]) || (utfok && namebuf[i] > 127)) {
			if (namebuf[i] == '\\' || namebuf[i] == '"') {
				ADD_STRING(target, "\\");
			}
			if (isc_buffer_availablelength(target) < 1) {
				return ISC_R_NOSPACE;
			}
			isc_buffer_putmem(target, &namebuf[i], 1);
		} else {
			ADD_STRING(target, ".");
		}
	}
cleanup:
	return result;
}

static isc_result_t
render_nameopt(isc_buffer_t *optbuf, bool yaml, isc_buffer_t *target) {
	dns_decompress_t dctx = DNS_DECOMPRESS_NEVER;
	dns_fixedname_t fixed;
	dns_name_t *name = dns_fixedname_initname(&fixed);
	char namebuf[DNS_NAME_FORMATSIZE];
	isc_result_t result;

	result = dns_name_fromwire(name, optbuf, dctx, NULL);
	if (result == ISC_R_SUCCESS && isc_buffer_activelength(optbuf) == 0) {
		dns_name_format(name, namebuf, sizeof(namebuf));
		ADD_STRING(target, " \"");
		if (yaml) {
			PUT_YAMLSTR(target, (unsigned char *)namebuf,
				    strlen(namebuf), false);
		} else {
			ADD_STRING(target, namebuf);
		}
		ADD_STRING(target, "\"");
		return result;
	}
	result = ISC_R_FAILURE;
cleanup:
	return result;
}

static const char *option_names[] = {
	[DNS_OPT_LLQ] = "LLQ",
	[DNS_OPT_UL] = "UPDATE-LEASE",
	[DNS_OPT_NSID] = "NSID",
	[DNS_OPT_DAU] = "DAU",
	[DNS_OPT_DHU] = "DHU",
	[DNS_OPT_N3U] = "N3U",
	[DNS_OPT_CLIENT_SUBNET] = "CLIENT-SUBNET",
	[DNS_OPT_EXPIRE] = "EXPIRE",
	[DNS_OPT_COOKIE] = "COOKIE",
	[DNS_OPT_TCP_KEEPALIVE] = "TCP-KEEPALIVE",
	[DNS_OPT_PAD] = "PADDING",
	[DNS_OPT_CHAIN] = "CHAIN",
	[DNS_OPT_KEY_TAG] = "KEY-TAG",
	[DNS_OPT_EDE] = "EDE",
	[DNS_OPT_CLIENT_TAG] = "CLIENT-TAG",
	[DNS_OPT_SERVER_TAG] = "SERVER-TAG",
	[DNS_OPT_REPORT_CHANNEL] = "Report-Channel",
	[DNS_OPT_ZONEVERSION] = "ZONEVERSION",
};

static isc_result_t
dns_message_pseudosectiontoyaml(dns_message_t *msg, dns_pseudosection_t section,
				const dns_master_style_t *style,
				dns_messagetextflag_t flags,
				isc_buffer_t *target) {
	dns_rdataset_t *ps = NULL;
	const dns_name_t *name = NULL;
	isc_result_t result = ISC_R_SUCCESS;
	char buf[sizeof("/1234567890")];
	uint32_t mbz;
	dns_rdata_t rdata;
	isc_buffer_t optbuf;
	uint16_t optcode, optlen;
	size_t saved_count;
	unsigned char *optdata = NULL;
	unsigned int indent;
	isc_buffer_t ecsbuf;

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(target != NULL);
	REQUIRE(VALID_NAMED_PSEUDOSECTION(section));

	saved_count = msg->indent.count;

	switch (section) {
	case DNS_PSEUDOSECTION_OPT:
		ps = dns_message_getopt(msg);
		if (ps == NULL) {
			goto cleanup;
		}

		INDENT(style);
		ADD_STRING(target, "OPT_PSEUDOSECTION:\n");
		msg->indent.count++;

		INDENT(style);
		ADD_STRING(target, "EDNS:\n");
		indent = ++msg->indent.count;

		INDENT(style);
		ADD_STRING(target, "version: ");
		snprintf(buf, sizeof(buf), "%u",
			 (unsigned int)((ps->ttl & 0x00ff0000) >> 16));
		ADD_STRING(target, buf);
		ADD_STRING(target, "\n");
		INDENT(style);
		ADD_STRING(target, "flags:");
		if ((ps->ttl & DNS_MESSAGEEXTFLAG_DO) != 0) {
			ADD_STRING(target, " do");
		}
		if ((ps->ttl & DNS_MESSAGEEXTFLAG_CO) != 0) {
			ADD_STRING(target, " co");
		}
		ADD_STRING(target, "\n");
		mbz = ps->ttl & 0xffff;
		/* Exclude Known Flags. */
		mbz &= ~(DNS_MESSAGEEXTFLAG_DO | DNS_MESSAGEEXTFLAG_CO);
		if (mbz != 0) {
			INDENT(style);
			ADD_STRING(target, "MBZ: ");
			snprintf(buf, sizeof(buf), "0x%.4x", mbz);
			ADD_STRING(target, buf);
			ADD_STRING(target, "\n");
		}
		INDENT(style);
		ADD_STRING(target, "udp: ");
		snprintf(buf, sizeof(buf), "%u\n", (unsigned int)ps->rdclass);
		ADD_STRING(target, buf);
		result = dns_rdataset_first(ps);
		if (result != ISC_R_SUCCESS) {
			result = ISC_R_SUCCESS;
			goto cleanup;
		}

		/*
		 * Print EDNS info, if any.
		 *
		 * WARNING: The option contents may be malformed as
		 * dig +ednsopt=value:<content> does not perform validity
		 * checking.
		 */
		dns_rdata_init(&rdata);
		dns_rdataset_current(ps, &rdata);

		isc_buffer_init(&optbuf, rdata.data, rdata.length);
		isc_buffer_add(&optbuf, rdata.length);
		while (isc_buffer_remaininglength(&optbuf) != 0) {
			bool extra_text = false;
			const char *option_name = NULL;

			msg->indent.count = indent;
			INSIST(isc_buffer_remaininglength(&optbuf) >= 4U);
			optcode = isc_buffer_getuint16(&optbuf);
			optlen = isc_buffer_getuint16(&optbuf);
			INSIST(isc_buffer_remaininglength(&optbuf) >= optlen);

			INDENT(style);
			if (optcode < ARRAY_SIZE(option_names)) {
				option_name = option_names[optcode];
			}
			if (option_name != NULL) {
				ADD_STRING(target, option_names[optcode])
			} else {
				snprintf(buf, sizeof(buf), "OPT=%u", optcode);
				ADD_STRING(target, buf);
			}
			ADD_STRING(target, ":");

			switch (optcode) {
			case DNS_OPT_LLQ:
				if (optlen == 18U) {
					result = render_llq(&optbuf, msg, style,
							    target);
					if (result != ISC_R_SUCCESS) {
						goto cleanup;
					}
					ADD_STRING(target, "\n");
					continue;
				}
				break;
			case DNS_OPT_UL:
				if (optlen == 4U || optlen == 8U) {
					uint32_t secs, key = 0;
					msg->indent.count++;

					secs = isc_buffer_getuint32(&optbuf);
					ADD_STRING(target, "\n");
					INDENT(style);
					ADD_STRING(target, "LEASE:");
					snprintf(buf, sizeof(buf), " %u", secs);
					ADD_STRING(target, buf);

					ADD_STRING(target, " # ");
					result = dns_ttl_totext(secs, true,
								true, target);
					if (result != ISC_R_SUCCESS) {
						goto cleanup;
					}
					ADD_STRING(target, "\n");

					if (optlen == 8U) {
						key = isc_buffer_getuint32(
							&optbuf);
						INDENT(style);
						ADD_STRING(target,
							   "KEY-LEASE:");
						snprintf(buf, sizeof(buf),
							 " %u", key);
						ADD_STRING(target, buf);

						ADD_STRING(target, " # ");
						result = dns_ttl_totext(
							key, true, true,
							target);
						if (result != ISC_R_SUCCESS) {
							goto cleanup;
						}
						ADD_STRING(target, "\n");
					}
					continue;
				}
				break;
			case DNS_OPT_CLIENT_SUBNET:
				isc_buffer_init(&ecsbuf,
						isc_buffer_current(&optbuf),
						optlen);
				isc_buffer_add(&ecsbuf, optlen);
				result = render_ecs(&ecsbuf, target);
				if (result == ISC_R_NOSPACE) {
					goto cleanup;
				}
				if (result == ISC_R_SUCCESS) {
					isc_buffer_forward(&optbuf, optlen);
					ADD_STRING(target, "\n");
					continue;
				}
				ADD_STRING(target, "\n");
				break;
			case DNS_OPT_EXPIRE:
				if (optlen == 4) {
					uint32_t secs;
					secs = isc_buffer_getuint32(&optbuf);
					snprintf(buf, sizeof(buf), " %u", secs);
					ADD_STRING(target, buf);
					ADD_STRING(target, " # ");
					result = dns_ttl_totext(secs, true,
								true, target);
					if (result != ISC_R_SUCCESS) {
						goto cleanup;
					}
					ADD_STRING(target, "\n");
					continue;
				}
				break;
			case DNS_OPT_TCP_KEEPALIVE:
				if (optlen == 2) {
					unsigned int dsecs;
					dsecs = isc_buffer_getuint16(&optbuf);
					snprintf(buf, sizeof(buf), " %u.%u",
						 dsecs / 10U, dsecs % 10U);
					ADD_STRING(target, buf);
					ADD_STRING(target, " secs\n");
					continue;
				}
				break;
			case DNS_OPT_CHAIN:
			case DNS_OPT_REPORT_CHANNEL:
				if (optlen > 0U) {
					isc_buffer_t sb = optbuf;
					isc_buffer_setactive(&optbuf, optlen);
					result = render_nameopt(&optbuf, true,
								target);
					if (result == ISC_R_SUCCESS) {
						ADD_STRING(target, "\n");
						continue;
					}
					optbuf = sb;
				}
				break;
			case DNS_OPT_KEY_TAG:
				if (optlen > 0U && (optlen % 2U) == 0U) {
					const char *sep = " [";
					while (optlen > 0U) {
						uint16_t id =
							isc_buffer_getuint16(
								&optbuf);
						snprintf(buf, sizeof(buf),
							 "%s %u", sep, id);
						ADD_STRING(target, buf);
						sep = ",";
						optlen -= 2;
					}
					ADD_STRING(target, " ]\n");
					continue;
				}
				break;
			case DNS_OPT_EDE:
				if (optlen >= 2U) {
					uint16_t ede;
					ADD_STRING(target, "\n");
					msg->indent.count++;
					INDENT(style);
					ADD_STRING(target, "INFO-CODE:");
					ede = isc_buffer_getuint16(&optbuf);
					snprintf(buf, sizeof(buf), " %u", ede);
					ADD_STRING(target, buf);
					if (ede < ARRAY_SIZE(edetext)) {
						ADD_STRING(target, " (");
						ADD_STRING(target,
							   edetext[ede]);
						ADD_STRING(target, ")");
					}
					ADD_STRING(target, "\n");
					optlen -= 2;
					if (optlen != 0) {
						INDENT(style);
						ADD_STRING(target,
							   "EXTRA-TEXT:");
						extra_text = true;
					}
				}
				break;
			case DNS_OPT_CLIENT_TAG:
			case DNS_OPT_SERVER_TAG:
				if (optlen == 2U) {
					uint16_t id =
						isc_buffer_getuint16(&optbuf);
					snprintf(buf, sizeof(buf), " %u\n", id);
					ADD_STRING(target, buf);
					continue;
				}
				break;
			case DNS_OPT_COOKIE:
				if (optlen == 8 ||
				    (optlen >= 16 && optlen < 40))
				{
					size_t i;

					msg->indent.count++;
					optdata = isc_buffer_current(&optbuf);

					ADD_STRING(target, "\n");
					INDENT(style);
					ADD_STRING(target, "CLIENT: ");
					for (i = 0; i < 8; i++) {
						snprintf(buf, sizeof(buf),
							 "%02x", optdata[i]);
						ADD_STRING(target, buf);
					}
					ADD_STRING(target, "\n");

					if (optlen >= 16) {
						INDENT(style);
						ADD_STRING(target, "SERVER: ");
						for (; i < optlen; i++) {
							snprintf(buf,
								 sizeof(buf),
								 "%02x",
								 optdata[i]);
							ADD_STRING(target, buf);
						}
						ADD_STRING(target, "\n");
					}

					/*
					 * Valid server cookie?
					 */
					if (msg->cc_ok && optlen >= 16) {
						INDENT(style);
						ADD_STRING(target,
							   "STATUS: good\n");
					}
					/*
					 * Server cookie is not valid but
					 * we had our cookie echoed back.
					 */
					if (msg->cc_ok && optlen < 16) {
						INDENT(style);
						ADD_STRING(target,
							   "STATUS: echoed\n");
					}
					/*
					 * We didn't get our cookie echoed
					 * back.
					 */
					if (msg->cc_bad) {
						INDENT(style);
						ADD_STRING(target,
							   "STATUS: bad\n)");
					}
					isc_buffer_forward(&optbuf, optlen);
					continue;
				}
				break;
			default:
				break;
			}

			if (optlen != 0) {
				int i;
				bool utf8ok = false;

				ADD_STRING(target, " ");

				optdata = isc_buffer_current(&optbuf);
				if (extra_text) {
					utf8ok = isc_utf8_valid(optdata,
								optlen);
				}
				if (!utf8ok) {
					for (i = 0; i < optlen; i++) {
						const char *sep;
						switch (optcode) {
						case DNS_OPT_COOKIE:
							sep = "";
							break;
						default:
							sep = " ";
							break;
						}
						snprintf(buf, sizeof(buf),
							 "%02x%s", optdata[i],
							 sep);
						ADD_STRING(target, buf);
					}
				}

				isc_buffer_forward(&optbuf, optlen);

				if (optcode == DNS_OPT_COOKIE ||
				    optcode == DNS_OPT_CLIENT_SUBNET)
				{
					ADD_STRING(target, "\n");
					continue;
				}

				/*
				 * For non-COOKIE options, add a printable
				 * version
				 */
				if (!extra_text) {
					ADD_STRING(target, "(\"");
				} else {
					ADD_STRING(target, "\"");
				}
				PUT_YAMLSTR(target, optdata, optlen, utf8ok);
				if (!extra_text) {
					ADD_STRING(target, "\")");
				} else {
					ADD_STRING(target, "\"");
				}
			}
			ADD_STRING(target, "\n");
		}
		msg->indent.count = indent;
		result = ISC_R_SUCCESS;
		goto cleanup;
	case DNS_PSEUDOSECTION_TSIG:
		ps = dns_message_gettsig(msg, &name);
		if (ps == NULL) {
			result = ISC_R_SUCCESS;
			goto cleanup;
		}
		INDENT(style);
		ADD_STRING(target, "TSIG_PSEUDOSECTION:\n");
		result = dns_master_rdatasettotext(name, ps, style,
						   &msg->indent, target);
		ADD_STRING(target, "\n");
		goto cleanup;
	case DNS_PSEUDOSECTION_SIG0:
		ps = dns_message_getsig0(msg, &name);
		if (ps == NULL) {
			result = ISC_R_SUCCESS;
			goto cleanup;
		}
		INDENT(style);
		ADD_STRING(target, "SIG0_PSEUDOSECTION:\n");
		result = dns_master_rdatasettotext(name, ps, style,
						   &msg->indent, target);
		if ((flags & DNS_MESSAGETEXTFLAG_NOHEADERS) == 0 &&
		    (flags & DNS_MESSAGETEXTFLAG_NOCOMMENTS) == 0)
		{
			ADD_STRING(target, "\n");
		}
		goto cleanup;
	}

	result = ISC_R_UNEXPECTED;

cleanup:
	msg->indent.count = saved_count;
	return result;
}

isc_result_t
dns_message_pseudosectiontotext(dns_message_t *msg, dns_pseudosection_t section,
				const dns_master_style_t *style,
				dns_messagetextflag_t flags,
				isc_buffer_t *target) {
	dns_rdataset_t *ps = NULL;
	const dns_name_t *name = NULL;
	isc_result_t result;
	char buf[sizeof(" (65000 bytes)")];
	uint32_t mbz;
	dns_rdata_t rdata;
	isc_buffer_t optbuf;
	uint16_t optcode, optlen;
	unsigned char *optdata = NULL;
	isc_buffer_t ecsbuf;

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(target != NULL);
	REQUIRE(VALID_NAMED_PSEUDOSECTION(section));

	if ((dns_master_styleflags(style) & DNS_STYLEFLAG_YAML) != 0) {
		return dns_message_pseudosectiontoyaml(msg, section, style,
						       flags, target);
	}

	switch (section) {
	case DNS_PSEUDOSECTION_OPT:
		ps = dns_message_getopt(msg);
		if (ps == NULL) {
			return ISC_R_SUCCESS;
		}
		if ((flags & DNS_MESSAGETEXTFLAG_NOCOMMENTS) == 0) {
			INDENT(style);
			ADD_STRING(target, ";; OPT PSEUDOSECTION:\n");
		}

		INDENT(style);
		ADD_STRING(target, "; EDNS: version: ");
		snprintf(buf, sizeof(buf), "%u",
			 (unsigned int)((ps->ttl & 0x00ff0000) >> 16));
		ADD_STRING(target, buf);
		ADD_STRING(target, ", flags:");
		if ((ps->ttl & DNS_MESSAGEEXTFLAG_DO) != 0) {
			ADD_STRING(target, " do");
		}
		if ((ps->ttl & DNS_MESSAGEEXTFLAG_CO) != 0) {
			ADD_STRING(target, " co");
		}
		mbz = ps->ttl & 0xffff;
		/* Exclude Known Flags. */
		mbz &= ~(DNS_MESSAGEEXTFLAG_DO | DNS_MESSAGEEXTFLAG_CO);
		if (mbz != 0) {
			ADD_STRING(target, "; MBZ: ");
			snprintf(buf, sizeof(buf), "0x%.4x", mbz);
			ADD_STRING(target, buf);
			ADD_STRING(target, ", udp: ");
		} else {
			ADD_STRING(target, "; udp: ");
		}
		snprintf(buf, sizeof(buf), "%u\n", (unsigned int)ps->rdclass);
		ADD_STRING(target, buf);

		result = dns_rdataset_first(ps);
		if (result != ISC_R_SUCCESS) {
			return ISC_R_SUCCESS;
		}

		/*
		 * Print EDNS info, if any.
		 *
		 * WARNING: The option contents may be malformed as
		 * dig +ednsopt=value:<content> does no validity
		 * checking.
		 */
		dns_rdata_init(&rdata);
		dns_rdataset_current(ps, &rdata);

		isc_buffer_init(&optbuf, rdata.data, rdata.length);
		isc_buffer_add(&optbuf, rdata.length);
		while (isc_buffer_remaininglength(&optbuf) != 0) {
			const char *option_name = NULL;

			INSIST(isc_buffer_remaininglength(&optbuf) >= 4U);
			optcode = isc_buffer_getuint16(&optbuf);
			optlen = isc_buffer_getuint16(&optbuf);

			INSIST(isc_buffer_remaininglength(&optbuf) >= optlen);

			INDENT(style);
			ADD_STRING(target, "; ");
			if (optcode < ARRAY_SIZE(option_names)) {
				option_name = option_names[optcode];
			}
			if (option_name != NULL) {
				ADD_STRING(target, option_names[optcode])
			} else {
				snprintf(buf, sizeof(buf), "OPT=%u", optcode);
				ADD_STRING(target, buf);
			}
			ADD_STRING(target, ":");

			switch (optcode) {
			case DNS_OPT_LLQ:
				if (optlen == 18U) {
					result = render_llq(&optbuf, msg, style,
							    target);
					if (result != ISC_R_SUCCESS) {
						return result;
					}
					ADD_STRING(target, "\n");
					continue;
				}
				break;
			case DNS_OPT_UL:
				if (optlen == 4U || optlen == 8U) {
					uint32_t secs, key = 0;
					secs = isc_buffer_getuint32(&optbuf);
					snprintf(buf, sizeof(buf), " %u", secs);
					ADD_STRING(target, buf);
					if (optlen == 8U) {
						key = isc_buffer_getuint32(
							&optbuf);
						snprintf(buf, sizeof(buf),
							 "/%u", key);
						ADD_STRING(target, buf);
					}
					ADD_STRING(target, " (");
					result = dns_ttl_totext(secs, true,
								true, target);
					if (result != ISC_R_SUCCESS) {
						goto cleanup;
					}
					if (optlen == 8U) {
						ADD_STRING(target, "/");
						result = dns_ttl_totext(
							key, true, true,
							target);
						if (result != ISC_R_SUCCESS) {
							goto cleanup;
						}
					}
					ADD_STRING(target, ")\n");
					continue;
				}
				break;
			case DNS_OPT_CLIENT_SUBNET:
				isc_buffer_init(&ecsbuf,
						isc_buffer_current(&optbuf),
						optlen);
				isc_buffer_add(&ecsbuf, optlen);
				result = render_ecs(&ecsbuf, target);
				if (result == ISC_R_NOSPACE) {
					return result;
				}
				if (result == ISC_R_SUCCESS) {
					isc_buffer_forward(&optbuf, optlen);
					ADD_STRING(target, "\n");
					continue;
				}
				break;
			case DNS_OPT_EXPIRE:
				if (optlen == 4) {
					uint32_t secs;
					secs = isc_buffer_getuint32(&optbuf);
					snprintf(buf, sizeof(buf), " %u", secs);
					ADD_STRING(target, buf);
					ADD_STRING(target, " (");
					result = dns_ttl_totext(secs, true,
								true, target);
					if (result != ISC_R_SUCCESS) {
						return result;
					}
					ADD_STRING(target, ")\n");
					continue;
				}
				break;
			case DNS_OPT_TCP_KEEPALIVE:
				if (optlen == 2) {
					unsigned int dsecs;
					dsecs = isc_buffer_getuint16(&optbuf);
					snprintf(buf, sizeof(buf), " %u.%u",
						 dsecs / 10U, dsecs % 10U);
					ADD_STRING(target, buf);
					ADD_STRING(target, " secs\n");
					continue;
				}
				break;
			case DNS_OPT_PAD:
				if (optlen > 0U) {
					snprintf(buf, sizeof(buf),
						 " (%u bytes)", optlen);
					ADD_STRING(target, buf);
					isc_buffer_forward(&optbuf, optlen);
				}
				ADD_STRING(target, "\n");
				continue;
			case DNS_OPT_CHAIN:
			case DNS_OPT_REPORT_CHANNEL:
				if (optlen > 0U) {
					isc_buffer_t sb = optbuf;
					isc_buffer_setactive(&optbuf, optlen);
					result = render_nameopt(&optbuf, false,
								target);
					if (result == ISC_R_SUCCESS) {
						ADD_STRING(target, "\n");
						continue;
					}
					optbuf = sb;
				}
				ADD_STRING(target, "\n");
				break;
			case DNS_OPT_KEY_TAG:
				if (optlen > 0U && (optlen % 2U) == 0U) {
					const char *sep = "";
					while (optlen > 0U) {
						uint16_t id =
							isc_buffer_getuint16(
								&optbuf);
						snprintf(buf, sizeof(buf),
							 "%s %u", sep, id);
						ADD_STRING(target, buf);
						sep = ",";
						optlen -= 2;
					}
					ADD_STRING(target, "\n");
					continue;
				}
				break;
			case DNS_OPT_EDE:
				if (optlen >= 2U) {
					uint16_t ede;
					ede = isc_buffer_getuint16(&optbuf);
					snprintf(buf, sizeof(buf), " %u", ede);
					ADD_STRING(target, buf);
					if (ede < ARRAY_SIZE(edetext)) {
						ADD_STRING(target, " (");
						ADD_STRING(target,
							   edetext[ede]);
						ADD_STRING(target, ")");
					}
					optlen -= 2;
					if (optlen != 0) {
						ADD_STRING(target, ":");
					}
				} else if (optlen == 1U) {
					/* Malformed */
					optdata = isc_buffer_current(&optbuf);
					snprintf(buf, sizeof(buf),
						 " %02x (\"%c\")\n", optdata[0],
						 isprint(optdata[0])
							 ? optdata[0]
							 : '.');
					isc_buffer_forward(&optbuf, optlen);
					ADD_STRING(target, buf);
					continue;
				}
				break;
			case DNS_OPT_CLIENT_TAG:
			case DNS_OPT_SERVER_TAG:
				if (optlen == 2U) {
					uint16_t id =
						isc_buffer_getuint16(&optbuf);
					snprintf(buf, sizeof(buf), " %u\n", id);
					ADD_STRING(target, buf);
					continue;
				}
				break;
			default:
				break;
			}

			if (optlen != 0) {
				int i;
				bool utf8ok = false;

				ADD_STRING(target, " ");

				optdata = isc_buffer_current(&optbuf);
				if (optcode == DNS_OPT_EDE) {
					utf8ok = isc_utf8_valid(optdata,
								optlen);
				}
				if (!utf8ok) {
					for (i = 0; i < optlen; i++) {
						const char *sep;
						switch (optcode) {
						case DNS_OPT_COOKIE:
							sep = "";
							break;
						default:
							sep = " ";
							break;
						}
						snprintf(buf, sizeof(buf),
							 "%02x%s", optdata[i],
							 sep);
						ADD_STRING(target, buf);
					}
				}

				isc_buffer_forward(&optbuf, optlen);

				if (optcode == DNS_OPT_COOKIE) {
					/*
					 * Valid server cookie?
					 */
					if (msg->cc_ok && optlen >= 16) {
						ADD_STRING(target, " (good)");
					}
					/*
					 * Server cookie is not valid but
					 * we had our cookie echoed back.
					 */
					if (msg->cc_ok && optlen < 16) {
						ADD_STRING(target, " (echoed)");
					}
					/*
					 * We didn't get our cookie echoed
					 * back.
					 */
					if (msg->cc_bad) {
						ADD_STRING(target, " (bad)");
					}
					ADD_STRING(target, "\n");
					continue;
				}

				if (optcode == DNS_OPT_CLIENT_SUBNET) {
					ADD_STRING(target, "\n");
					continue;
				}

				/*
				 * For non-COOKIE options, add a printable
				 * version.
				 */
				if (optcode != DNS_OPT_EDE) {
					ADD_STRING(target, "(\"");
				} else {
					ADD_STRING(target, "(");
				}
				if (isc_buffer_availablelength(target) < optlen)
				{
					return ISC_R_NOSPACE;
				}
				for (i = 0; i < optlen; i++) {
					if (isprint(optdata[i]) ||
					    (utf8ok && optdata[i] > 127))
					{
						isc_buffer_putmem(
							target, &optdata[i], 1);
					} else {
						isc_buffer_putstr(target, ".");
					}
				}
				if (optcode != DNS_OPT_EDE) {
					ADD_STRING(target, "\")");
				} else {
					ADD_STRING(target, ")");
				}
			}
			ADD_STRING(target, "\n");
		}
		return ISC_R_SUCCESS;
	case DNS_PSEUDOSECTION_TSIG:
		ps = dns_message_gettsig(msg, &name);
		if (ps == NULL) {
			return ISC_R_SUCCESS;
		}
		INDENT(style);
		if ((flags & DNS_MESSAGETEXTFLAG_NOCOMMENTS) == 0) {
			ADD_STRING(target, ";; TSIG PSEUDOSECTION:\n");
		}
		result = dns_master_rdatasettotext(name, ps, style,
						   &msg->indent, target);
		if ((flags & DNS_MESSAGETEXTFLAG_NOHEADERS) == 0 &&
		    (flags & DNS_MESSAGETEXTFLAG_NOCOMMENTS) == 0)
		{
			ADD_STRING(target, "\n");
		}
		return result;
	case DNS_PSEUDOSECTION_SIG0:
		ps = dns_message_getsig0(msg, &name);
		if (ps == NULL) {
			return ISC_R_SUCCESS;
		}
		INDENT(style);
		if ((flags & DNS_MESSAGETEXTFLAG_NOCOMMENTS) == 0) {
			ADD_STRING(target, ";; SIG0 PSEUDOSECTION:\n");
		}
		result = dns_master_rdatasettotext(name, ps, style,
						   &msg->indent, target);
		if ((flags & DNS_MESSAGETEXTFLAG_NOHEADERS) == 0 &&
		    (flags & DNS_MESSAGETEXTFLAG_NOCOMMENTS) == 0)
		{
			ADD_STRING(target, "\n");
		}
		return result;
	}
	result = ISC_R_UNEXPECTED;
cleanup:
	return result;
}

isc_result_t
dns_message_headertotext(dns_message_t *msg, const dns_master_style_t *style,
			 dns_messagetextflag_t flags, isc_buffer_t *target) {
	char buf[sizeof("1234567890")];
	isc_result_t result;

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(target != NULL);

	if ((flags & DNS_MESSAGETEXTFLAG_NOHEADERS) != 0) {
		return ISC_R_SUCCESS;
	}

	if (dns_master_styleflags(style) & DNS_STYLEFLAG_YAML) {
		INDENT(style);
		ADD_STRING(target, "opcode: ");
		ADD_STRING(target, opcodetext[msg->opcode]);
		ADD_STRING(target, "\n");
		INDENT(style);
		ADD_STRING(target, "status: ");
		result = dns_rcode_totext(msg->rcode, target);
		if (result != ISC_R_SUCCESS) {
			return result;
		}
		ADD_STRING(target, "\n");
		INDENT(style);
		ADD_STRING(target, "id: ");
		snprintf(buf, sizeof(buf), "%u", msg->id);
		ADD_STRING(target, buf);
		ADD_STRING(target, "\n");
		INDENT(style);
		ADD_STRING(target, "flags:");
		if ((msg->flags & DNS_MESSAGEFLAG_QR) != 0) {
			ADD_STRING(target, " qr");
		}
		if ((msg->flags & DNS_MESSAGEFLAG_AA) != 0) {
			ADD_STRING(target, " aa");
		}
		if ((msg->flags & DNS_MESSAGEFLAG_TC) != 0) {
			ADD_STRING(target, " tc");
		}
		if ((msg->flags & DNS_MESSAGEFLAG_RD) != 0) {
			ADD_STRING(target, " rd");
		}
		if ((msg->flags & DNS_MESSAGEFLAG_RA) != 0) {
			ADD_STRING(target, " ra");
		}
		if ((msg->flags & DNS_MESSAGEFLAG_AD) != 0) {
			ADD_STRING(target, " ad");
		}
		if ((msg->flags & DNS_MESSAGEFLAG_CD) != 0) {
			ADD_STRING(target, " cd");
		}
		ADD_STRING(target, "\n");
		/*
		 * The final unnamed flag must be zero.
		 */
		if ((msg->flags & 0x0040U) != 0) {
			INDENT(style);
			ADD_STRING(target, "MBZ: 0x4");
			ADD_STRING(target, "\n");
		}
		if (msg->opcode != dns_opcode_update) {
			INDENT(style);
			ADD_STRING(target, "QUESTION: ");
		} else {
			INDENT(style);
			ADD_STRING(target, "ZONE: ");
		}
		snprintf(buf, sizeof(buf), "%1u",
			 msg->counts[DNS_SECTION_QUESTION]);
		ADD_STRING(target, buf);
		ADD_STRING(target, "\n");
		if (msg->opcode != dns_opcode_update) {
			INDENT(style);
			ADD_STRING(target, "ANSWER: ");
		} else {
			INDENT(style);
			ADD_STRING(target, "PREREQ: ");
		}
		snprintf(buf, sizeof(buf), "%1u",
			 msg->counts[DNS_SECTION_ANSWER]);
		ADD_STRING(target, buf);
		ADD_STRING(target, "\n");
		if (msg->opcode != dns_opcode_update) {
			INDENT(style);
			ADD_STRING(target, "AUTHORITY: ");
		} else {
			INDENT(style);
			ADD_STRING(target, "UPDATE: ");
		}
		snprintf(buf, sizeof(buf), "%1u",
			 msg->counts[DNS_SECTION_AUTHORITY]);
		ADD_STRING(target, buf);
		ADD_STRING(target, "\n");
		INDENT(style);
		ADD_STRING(target, "ADDITIONAL: ");
		snprintf(buf, sizeof(buf), "%1u",
			 msg->counts[DNS_SECTION_ADDITIONAL]);
		ADD_STRING(target, buf);
		ADD_STRING(target, "\n");
	} else {
		INDENT(style);
		ADD_STRING(target, ";; ->>HEADER<<- opcode: ");
		ADD_STRING(target, opcodetext[msg->opcode]);
		ADD_STRING(target, ", status: ");
		result = dns_rcode_totext(msg->rcode, target);
		if (result != ISC_R_SUCCESS) {
			return result;
		}
		ADD_STRING(target, ", id: ");
		snprintf(buf, sizeof(buf), "%6u", msg->id);
		ADD_STRING(target, buf);
		ADD_STRING(target, "\n");
		INDENT(style);
		ADD_STRING(target, ";; flags:");
		if ((msg->flags & DNS_MESSAGEFLAG_QR) != 0) {
			ADD_STRING(target, " qr");
		}
		if ((msg->flags & DNS_MESSAGEFLAG_AA) != 0) {
			ADD_STRING(target, " aa");
		}
		if ((msg->flags & DNS_MESSAGEFLAG_TC) != 0) {
			ADD_STRING(target, " tc");
		}
		if ((msg->flags & DNS_MESSAGEFLAG_RD) != 0) {
			ADD_STRING(target, " rd");
		}
		if ((msg->flags & DNS_MESSAGEFLAG_RA) != 0) {
			ADD_STRING(target, " ra");
		}
		if ((msg->flags & DNS_MESSAGEFLAG_AD) != 0) {
			ADD_STRING(target, " ad");
		}
		if ((msg->flags & DNS_MESSAGEFLAG_CD) != 0) {
			ADD_STRING(target, " cd");
		}
		/*
		 * The final unnamed flag must be zero.
		 */
		if ((msg->flags & 0x0040U) != 0) {
			INDENT(style);
			ADD_STRING(target, "; MBZ: 0x4");
		}
		if (msg->opcode != dns_opcode_update) {
			INDENT(style);
			ADD_STRING(target, "; QUESTION: ");
		} else {
			INDENT(style);
			ADD_STRING(target, "; ZONE: ");
		}
		snprintf(buf, sizeof(buf), "%1u",
			 msg->counts[DNS_SECTION_QUESTION]);
		ADD_STRING(target, buf);
		if (msg->opcode != dns_opcode_update) {
			ADD_STRING(target, ", ANSWER: ");
		} else {
			ADD_STRING(target, ", PREREQ: ");
		}
		snprintf(buf, sizeof(buf), "%1u",
			 msg->counts[DNS_SECTION_ANSWER]);
		ADD_STRING(target, buf);
		if (msg->opcode != dns_opcode_update) {
			ADD_STRING(target, ", AUTHORITY: ");
		} else {
			ADD_STRING(target, ", UPDATE: ");
		}
		snprintf(buf, sizeof(buf), "%1u",
			 msg->counts[DNS_SECTION_AUTHORITY]);
		ADD_STRING(target, buf);
		ADD_STRING(target, ", ADDITIONAL: ");
		snprintf(buf, sizeof(buf), "%1u",
			 msg->counts[DNS_SECTION_ADDITIONAL]);
		ADD_STRING(target, buf);
		ADD_STRING(target, "\n");
	}

cleanup:
	return result;
}

isc_result_t
dns_message_totext(dns_message_t *msg, const dns_master_style_t *style,
		   dns_messagetextflag_t flags, isc_buffer_t *target) {
	isc_result_t result;

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(target != NULL);

	result = dns_message_headertotext(msg, style, flags, target);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	result = dns_message_pseudosectiontotext(msg, DNS_PSEUDOSECTION_OPT,
						 style, flags, target);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	result = dns_message_sectiontotext(msg, DNS_SECTION_QUESTION, style,
					   flags, target);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	result = dns_message_sectiontotext(msg, DNS_SECTION_ANSWER, style,
					   flags, target);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	result = dns_message_sectiontotext(msg, DNS_SECTION_AUTHORITY, style,
					   flags, target);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	result = dns_message_sectiontotext(msg, DNS_SECTION_ADDITIONAL, style,
					   flags, target);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	result = dns_message_pseudosectiontotext(msg, DNS_PSEUDOSECTION_TSIG,
						 style, flags, target);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	result = dns_message_pseudosectiontotext(msg, DNS_PSEUDOSECTION_SIG0,
						 style, flags, target);
	return result;
}

isc_region_t *
dns_message_getrawmessage(dns_message_t *msg) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	return &msg->saved;
}

void
dns_message_setsortorder(dns_message_t *msg, dns_rdatasetorderfunc_t order,
			 dns_aclenv_t *env, dns_acl_t *acl,
			 const dns_aclelement_t *elem) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE((order == NULL) == (env == NULL));
	REQUIRE(env == NULL || (acl != NULL || elem != NULL));

	msg->order = order;
	if (env != NULL) {
		dns_aclenv_attach(env, &msg->order_arg.env);
	}
	if (acl != NULL) {
		dns_acl_attach(acl, &msg->order_arg.acl);
	}
	msg->order_arg.element = elem;
}

void
dns_message_settimeadjust(dns_message_t *msg, int timeadjust) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	msg->timeadjust = timeadjust;
}

int
dns_message_gettimeadjust(dns_message_t *msg) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	return msg->timeadjust;
}

isc_result_t
dns_opcode_totext(dns_opcode_t opcode, isc_buffer_t *target) {
	REQUIRE(opcode < 16);

	if (isc_buffer_availablelength(target) < strlen(opcodetext[opcode])) {
		return ISC_R_NOSPACE;
	}
	isc_buffer_putstr(target, opcodetext[opcode]);
	return ISC_R_SUCCESS;
}

void
dns_message_logpacket(dns_message_t *message, const char *description,
		      const isc_sockaddr_t *address,
		      isc_logcategory_t *category, isc_logmodule_t *module,
		      int level, isc_mem_t *mctx) {
	REQUIRE(address != NULL);

	logfmtpacket(message, description, address, category, module,
		     &dns_master_style_debug, level, mctx);
}

void
dns_message_logfmtpacket(dns_message_t *message, const char *description,
			 const isc_sockaddr_t *address,
			 isc_logcategory_t *category, isc_logmodule_t *module,
			 const dns_master_style_t *style, int level,
			 isc_mem_t *mctx) {
	REQUIRE(address != NULL);

	logfmtpacket(message, description, address, category, module, style,
		     level, mctx);
}

static void
logfmtpacket(dns_message_t *message, const char *description,
	     const isc_sockaddr_t *address, isc_logcategory_t *category,
	     isc_logmodule_t *module, const dns_master_style_t *style,
	     int level, isc_mem_t *mctx) {
	char addrbuf[ISC_SOCKADDR_FORMATSIZE] = { 0 };
	const char *newline = "\n";
	const char *space = " ";
	isc_buffer_t buffer;
	char *buf = NULL;
	int len = 1024;
	isc_result_t result;

	if (!isc_log_wouldlog(dns_lctx, level)) {
		return;
	}

	/*
	 * Note that these are multiline debug messages.  We want a newline
	 * to appear in the log after each message.
	 */

	if (address != NULL) {
		isc_sockaddr_format(address, addrbuf, sizeof(addrbuf));
	} else {
		newline = space = "";
	}

	do {
		buf = isc_mem_get(mctx, len);
		isc_buffer_init(&buffer, buf, len);
		result = dns_message_totext(message, style, 0, &buffer);
		if (result == ISC_R_NOSPACE) {
			isc_mem_put(mctx, buf, len);
			len += 1024;
		} else if (result == ISC_R_SUCCESS) {
			isc_log_write(dns_lctx, category, module, level,
				      "%s%s%s%s%.*s", description, space,
				      addrbuf, newline,
				      (int)isc_buffer_usedlength(&buffer), buf);
		}
	} while (result == ISC_R_NOSPACE);

	if (buf != NULL) {
		isc_mem_put(mctx, buf, len);
	}
}

isc_result_t
dns_message_buildopt(dns_message_t *message, dns_rdataset_t **rdatasetp,
		     unsigned int version, uint16_t udpsize, unsigned int flags,
		     dns_ednsopt_t *ednsopts, size_t count) {
	dns_rdataset_t *rdataset = NULL;
	dns_rdatalist_t *rdatalist = NULL;
	dns_rdata_t *rdata = NULL;
	isc_result_t result;
	unsigned int len = 0, i;

	REQUIRE(DNS_MESSAGE_VALID(message));
	REQUIRE(rdatasetp != NULL && *rdatasetp == NULL);

	dns_message_gettemprdatalist(message, &rdatalist);
	dns_message_gettemprdata(message, &rdata);
	dns_message_gettemprdataset(message, &rdataset);

	rdatalist->type = dns_rdatatype_opt;

	/*
	 * Set Maximum UDP buffer size.
	 */
	rdatalist->rdclass = udpsize;

	/*
	 * Set EXTENDED-RCODE and Z to 0.
	 */
	rdatalist->ttl = (version << 16);
	rdatalist->ttl |= (flags & 0xffff);

	/*
	 * Set EDNS options if applicable
	 */
	if (count != 0U) {
		isc_buffer_t *buf = NULL;
		bool seenpad = false;
		for (i = 0; i < count; i++) {
			len += ednsopts[i].length + 4;
		}

		if (len > 0xffffU) {
			result = ISC_R_NOSPACE;
			goto cleanup;
		}

		isc_buffer_allocate(message->mctx, &buf, len);

		for (i = 0; i < count; i++) {
			if (ednsopts[i].code == DNS_OPT_PAD &&
			    ednsopts[i].length == 0U && !seenpad)
			{
				seenpad = true;
				continue;
			}
			isc_buffer_putuint16(buf, ednsopts[i].code);
			isc_buffer_putuint16(buf, ednsopts[i].length);
			if (ednsopts[i].length != 0) {
				isc_buffer_putmem(buf, ednsopts[i].value,
						  ednsopts[i].length);
			}
		}

		/* Padding must be the final option */
		if (seenpad) {
			isc_buffer_putuint16(buf, DNS_OPT_PAD);
			isc_buffer_putuint16(buf, 0);
		}
		rdata->data = isc_buffer_base(buf);
		rdata->length = len;
		dns_message_takebuffer(message, &buf);
		if (seenpad) {
			message->padding_off = len;
		}
	} else {
		rdata->data = NULL;
		rdata->length = 0;
	}

	rdata->rdclass = rdatalist->rdclass;
	rdata->type = rdatalist->type;
	rdata->flags = 0;

	ISC_LIST_APPEND(rdatalist->rdata, rdata, link);
	dns_rdatalist_tordataset(rdatalist, rdataset);

	*rdatasetp = rdataset;
	return ISC_R_SUCCESS;

cleanup:
	dns_message_puttemprdata(message, &rdata);
	dns_message_puttemprdataset(message, &rdataset);
	dns_message_puttemprdatalist(message, &rdatalist);
	return result;
}

void
dns_message_setclass(dns_message_t *msg, dns_rdataclass_t rdclass) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(msg->from_to_wire == DNS_MESSAGE_INTENTPARSE);
	REQUIRE(msg->state == DNS_SECTION_ANY);
	REQUIRE(msg->rdclass_set == 0);

	msg->rdclass = rdclass;
	msg->rdclass_set = 1;
}

void
dns_message_setpadding(dns_message_t *msg, uint16_t padding) {
	REQUIRE(DNS_MESSAGE_VALID(msg));

	/* Avoid silly large padding */
	if (padding > 512) {
		padding = 512;
	}
	msg->padding = padding;
}

void
dns_message_clonebuffer(dns_message_t *msg) {
	REQUIRE(DNS_MESSAGE_VALID(msg));

	if (msg->free_saved == 0 && msg->saved.base != NULL) {
		msg->saved.base =
			memmove(isc_mem_get(msg->mctx, msg->saved.length),
				msg->saved.base, msg->saved.length);
		msg->free_saved = 1;
	}
	if (msg->free_query == 0 && msg->query.base != NULL) {
		msg->query.base =
			memmove(isc_mem_get(msg->mctx, msg->query.length),
				msg->query.base, msg->query.length);
		msg->free_query = 1;
	}
}

static isc_result_t
rdataset_soa_min(dns_rdataset_t *rds, dns_ttl_t *ttlp) {
	isc_result_t result;
	/* loop over the rdatas */
	for (result = dns_rdataset_first(rds); result == ISC_R_SUCCESS;
	     result = dns_rdataset_next(rds))
	{
		dns_name_t tmp;
		isc_region_t r = { 0 };
		dns_rdata_t rdata = DNS_RDATA_INIT;

		dns_rdataset_current(rds, &rdata);

		switch (rdata.type) {
		case dns_rdatatype_soa:
			/* SOA rdataset */
			break;
		case dns_rdatatype_none:
			/*
			 * Negative cache rdataset: we need
			 * to inspect the rdata to determine
			 * whether it's an SOA.
			 */
			dns_rdata_toregion(&rdata, &r);
			dns_name_init(&tmp, NULL);
			dns_name_fromregion(&tmp, &r);
			isc_region_consume(&r, tmp.length);
			if (r.length < 2) {
				continue;
			}
			rdata.type = r.base[0] << 8 | r.base[1];
			if (rdata.type != dns_rdatatype_soa) {
				continue;
			}
			break;
		default:
			continue;
		}

		if (rdata.type == dns_rdatatype_soa) {
			*ttlp = ISC_MIN(rds->ttl, dns_soa_getminimum(&rdata));
			return ISC_R_SUCCESS;
		}
	}

	return ISC_R_NOTFOUND;
}

static isc_result_t
message_authority_soa_min(dns_message_t *msg, dns_ttl_t *ttlp) {
	isc_result_t result;

	if (msg->counts[DNS_SECTION_AUTHORITY] == 0) {
		return ISC_R_NOTFOUND;
	}

	for (result = dns_message_firstname(msg, DNS_SECTION_AUTHORITY);
	     result == ISC_R_SUCCESS;
	     result = dns_message_nextname(msg, DNS_SECTION_AUTHORITY))
	{
		dns_name_t *name = NULL;
		dns_message_currentname(msg, DNS_SECTION_AUTHORITY, &name);

		dns_rdataset_t *rds = NULL;
		ISC_LIST_FOREACH (name->list, rds, link) {
			if ((rds->attributes & DNS_RDATASETATTR_RENDERED) == 0)
			{
				continue;
			}

			result = rdataset_soa_min(rds, ttlp);
			if (result == ISC_R_SUCCESS) {
				return ISC_R_SUCCESS;
			}
		}
	}

	return ISC_R_NOTFOUND;
}

isc_result_t
dns_message_minttl(dns_message_t *msg, const dns_section_t sectionid,
		   dns_ttl_t *pttl) {
	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(pttl != NULL);

	if (!msg->minttl[sectionid].is_set) {
		return ISC_R_NOTFOUND;
	}

	*pttl = msg->minttl[sectionid].ttl;
	return ISC_R_SUCCESS;
}

isc_result_t
dns_message_response_minttl(dns_message_t *msg, dns_ttl_t *pttl) {
	isc_result_t result;

	REQUIRE(DNS_MESSAGE_VALID(msg));
	REQUIRE(pttl != NULL);

	result = dns_message_minttl(msg, DNS_SECTION_ANSWER, pttl);
	if (result != ISC_R_SUCCESS) {
		return message_authority_soa_min(msg, pttl);
	}

	return ISC_R_SUCCESS;
}

void
dns_message_createpools(isc_mem_t *mctx, isc_mempool_t **namepoolp,
			isc_mempool_t **rdspoolp) {
	REQUIRE(mctx != NULL);
	REQUIRE(namepoolp != NULL && *namepoolp == NULL);
	REQUIRE(rdspoolp != NULL && *rdspoolp == NULL);

	isc_mempool_create(mctx, sizeof(dns_fixedname_t), namepoolp);
	isc_mempool_setfillcount(*namepoolp, NAME_FILLCOUNT);
	isc_mempool_setfreemax(*namepoolp, NAME_FREEMAX);
	isc_mempool_setname(*namepoolp, "dns_fixedname_pool");

	isc_mempool_create(mctx, sizeof(dns_rdataset_t), rdspoolp);
	isc_mempool_setfillcount(*rdspoolp, RDATASET_FILLCOUNT);
	isc_mempool_setfreemax(*rdspoolp, RDATASET_FREEMAX);
	isc_mempool_setname(*rdspoolp, "dns_rdataset_pool");
}

void
dns_message_destroypools(isc_mempool_t **namepoolp, isc_mempool_t **rdspoolp) {
	REQUIRE(namepoolp != NULL && *namepoolp != NULL);
	REQUIRE(rdspoolp != NULL && *rdspoolp != NULL);

	ENSURE(isc_mempool_getallocated(*namepoolp) == 0);
	ENSURE(isc_mempool_getallocated(*rdspoolp) == 0);

	isc_mempool_destroy(rdspoolp);
	isc_mempool_destroy(namepoolp);
}
