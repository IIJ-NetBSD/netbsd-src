/*	$NetBSD: message.h,v 1.16 2025/07/17 19:01:46 christos Exp $	*/

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

/***
 ***	Imports
 ***/

#include <inttypes.h>
#include <stdbool.h>

#include <isc/lang.h>
#include <isc/magic.h>
#include <isc/refcount.h>

#include <dns/compress.h>
#include <dns/ede.h>
#include <dns/masterdump.h>
#include <dns/types.h>

#include <dst/dst.h>

/* Add -DDNS_MESSAGE_TRACE=1 to CFLAGS for detailed reference tracing */

/*! \file dns/message.h
 * \brief Message Handling Module
 *
 * How this beast works:
 *
 * When a dns message is received in a buffer, dns_message_parse() is called
 * on the memory region.  Various items are checked including the format
 * of the message (if counts are right, if counts consume the entire sections,
 * and if sections consume the entire message) and known pseudo-RRs in the
 * additional data section are analyzed and removed.
 *
 * TSIG checking is also done at this layer, and any DNSSEC transaction
 * signatures should also be checked here.
 *
 * Notes on using the gettemp*() and puttemp*() functions:
 *
 * These functions return items (names, rdatasets, etc) allocated from some
 * internal state of the dns_message_t.
 *
 * Names and rdatasets must be put back into the dns_message_t in
 * one of two ways.  Assume a name was allocated via
 * dns_message_gettempname():
 *
 *\li	(1) insert it into a section, using dns_message_addname().
 *
 *\li	(2) return it to the message using dns_message_puttempname().
 *
 * The same applies to rdatasets.
 *
 * On the other hand, offsets, rdatalists and rdatas allocated using
 * dns_message_gettemp*() will always be freed automatically
 * when the message is reset or destroyed; calling dns_message_puttemp*()
 * on rdatalists and rdatas is optional and serves only to enable the item
 * to be reused multiple times during the lifetime of the message; offsets
 * cannot be reused.
 *
 * Buffers allocated using isc_buffer_allocate() can be automatically freed
 * as well by giving the buffer to the message using dns_message_takebuffer().
 * Doing this will cause the buffer to be freed using isc_buffer_free()
 * when the section lists are cleared, such as in a reset or in a destroy.
 * Since the buffer itself exists until the message is destroyed, this sort
 * of code can be written:
 *
 * \code
 *	buffer = isc_buffer_allocate(mctx, 512);
 *	name = NULL;
 *	result = dns_message_gettempname(message, &name);
 *	result = dns_name_fromtext(name, &source, dns_rootname, 0, buffer);
 *	dns_message_takebuffer(message, &buffer);
 * \endcode
 *
 *
 * TODO:
 *
 * XXX Needed:  ways to set and retrieve EDNS information, add rdata to a
 * section, move rdata from one section to another, remove rdata, etc.
 */

#define DNS_MESSAGEFLAG_QR 0x8000U
#define DNS_MESSAGEFLAG_AA 0x0400U
#define DNS_MESSAGEFLAG_TC 0x0200U
#define DNS_MESSAGEFLAG_RD 0x0100U
#define DNS_MESSAGEFLAG_RA 0x0080U
#define DNS_MESSAGEFLAG_AD 0x0020U
#define DNS_MESSAGEFLAG_CD 0x0010U

/*%< EDNS0 extended message flags */
#define DNS_MESSAGEEXTFLAG_DO 0x8000U /* DNSSEC OK */
#define DNS_MESSAGEEXTFLAG_CO 0x4000U /* Compact denial of existence OK */

/*%< EDNS0 extended OPT codes */

#define DNS_OPT_LLQ	       1  /*%< LLQ opt code */
#define DNS_OPT_UL	       2  /*%< UL opt code */
#define DNS_OPT_NSID	       3  /*%< NSID opt code */
#define DNS_OPT_DAU	       5  /*%< DNSSEC algorithm understood */
#define DNS_OPT_DHU	       6  /*%< DNSSEC hash understood */
#define DNS_OPT_N3U	       7  /*%< NSEC3 hash understood */
#define DNS_OPT_CLIENT_SUBNET  8  /*%< client subnet opt code */
#define DNS_OPT_EXPIRE	       9  /*%< EXPIRE opt code */
#define DNS_OPT_COOKIE	       10 /*%< COOKIE opt code */
#define DNS_OPT_TCP_KEEPALIVE  11 /*%< TCP keepalive opt code */
#define DNS_OPT_PAD	       12 /*%< PAD opt code */
#define DNS_OPT_CHAIN	       13 /*%< CHAIN opt code */
#define DNS_OPT_KEY_TAG	       14 /*%< Key tag opt code */
#define DNS_OPT_EDE	       15 /*%< Extended DNS Error opt code */
#define DNS_OPT_CLIENT_TAG     16 /*%< Client tag opt code */
#define DNS_OPT_SERVER_TAG     17 /*%< Server tag opt code */
#define DNS_OPT_REPORT_CHANNEL 18 /*%< DNS Reporting Channel */
#define DNS_OPT_ZONEVERSION    19 /*%< Zoneversion opt code */

/*%< Experimental options [65001...65534] as per RFC6891 */

/*%<
 * The maximum number of EDNS options we allow to set. Reserve space for the
 * options we know about. Extended DNS Errors may occur multiple times, see
 * DNS_EDE_MAX_ERRORS.
 */
#define DNS_EDNSOPTIONS 7 + DNS_EDE_MAX_ERRORS

#define DNS_MESSAGE_REPLYPRESERVE	 (DNS_MESSAGEFLAG_RD | DNS_MESSAGEFLAG_CD)
#define DNS_MESSAGEEXTFLAG_REPLYPRESERVE (DNS_MESSAGEEXTFLAG_DO)

#define DNS_MESSAGE_HEADERLEN 12 /*%< 6 uint16_t's */

#define DNS_MESSAGE_MAGIC      ISC_MAGIC('M', 'S', 'G', '@')
#define DNS_MESSAGE_VALID(msg) ISC_MAGIC_VALID(msg, DNS_MESSAGE_MAGIC)

/*
 * Ordering here matters.  DNS_SECTION_ANY must be the lowest and negative,
 * and DNS_SECTION_MAX must be one greater than the last used section.
 */
typedef int dns_section_t;
#define DNS_SECTION_ANY	       (-1)
#define DNS_SECTION_QUESTION   0
#define DNS_SECTION_ANSWER     1
#define DNS_SECTION_AUTHORITY  2
#define DNS_SECTION_ADDITIONAL 3
#define DNS_SECTION_MAX	       4

typedef int dns_pseudosection_t;
#define DNS_PSEUDOSECTION_ANY  (-1)
#define DNS_PSEUDOSECTION_OPT  0
#define DNS_PSEUDOSECTION_TSIG 1
#define DNS_PSEUDOSECTION_SIG0 2
#define DNS_PSEUDOSECTION_MAX  3

typedef int dns_messagetextflag_t;
#define DNS_MESSAGETEXTFLAG_NOCOMMENTS 0x0001
#define DNS_MESSAGETEXTFLAG_NOHEADERS  0x0002
#define DNS_MESSAGETEXTFLAG_ONESOA     0x0004
#define DNS_MESSAGETEXTFLAG_OMITSOA    0x0008

/*
 * Dynamic update names for these sections.
 */
#define DNS_SECTION_ZONE	 DNS_SECTION_QUESTION
#define DNS_SECTION_PREREQUISITE DNS_SECTION_ANSWER
#define DNS_SECTION_UPDATE	 DNS_SECTION_AUTHORITY

/*
 * These tell the message library how the created dns_message_t will be used.
 */
typedef enum dns_message_intent {
	DNS_MESSAGE_INTENTUNKNOWN = 0, /*%< internal use only */
	DNS_MESSAGE_INTENTPARSE = 1,   /*%< parsing messages */
	DNS_MESSAGE_INTENTRENDER = 2,  /*%< rendering */
} dns_message_intent_t;

/*
 * Control behavior of parsing
 */
#define DNS_MESSAGEPARSE_PRESERVEORDER 0x0001 /*%< preserve rdata order */
#define DNS_MESSAGEPARSE_BESTEFFORT       \
	0x0002 /*%< return a message if a \
		* recoverable parse error \
		* occurs */
#define DNS_MESSAGEPARSE_CLONEBUFFER   \
	0x0004 /*%< save a copy of the \
		* source buffer */
#define DNS_MESSAGEPARSE_IGNORETRUNCATION \
	0x0008 /*%< truncation errors are \
		* not fatal. */

/*
 * Control behavior of rendering
 */
#define DNS_MESSAGERENDER_ORDERED    0x0001 /*%< don't change order */
#define DNS_MESSAGERENDER_PARTIAL    0x0002 /*%< allow a partial rdataset */
#define DNS_MESSAGERENDER_OMITDNSSEC 0x0004 /*%< omit DNSSEC records */
#define DNS_MESSAGERENDER_PREFER_A      \
	0x0008 /*%< prefer A records in \
		*   additional section. */
#define DNS_MESSAGERENDER_PREFER_AAAA      \
	0x0010 /*%< prefer AAAA records in \
		*   additional section. */
/* Obsolete: DNS_MESSAGERENDER_FILTER_AAAA	0x0020	*/

typedef struct dns_msgblock dns_msgblock_t;

struct dns_sortlist_arg {
	dns_aclenv_t	       *env;
	dns_acl_t	       *acl;
	const dns_aclelement_t *element;
};

typedef struct dns_minttl {
	bool	  is_set;
	dns_ttl_t ttl;
} dns_minttl_t;

struct dns_message {
	/* public from here down */
	unsigned int   magic;
	isc_refcount_t references;

	dns_messageid_t	 id;
	unsigned int	 flags;
	dns_rcode_t	 rcode;
	dns_opcode_t	 opcode;
	dns_rdataclass_t rdclass;

	/* 4 real, 1 pseudo */
	unsigned int counts[DNS_SECTION_MAX];

	/* private from here down */
	dns_namelist_t	sections[DNS_SECTION_MAX];
	dns_name_t     *cursors[DNS_SECTION_MAX];
	dns_rdataset_t *opt;
	dns_rdataset_t *sig0;
	dns_rdataset_t *tsig;

	int state;
	unsigned int			      : 0; /* bits */
	dns_message_intent_t from_to_wire     : 2; /* 2 */
	unsigned int	     header_ok	      : 1; /* 3 */
	unsigned int	     question_ok      : 1; /* 4 */
	unsigned int	     tcp_continuation : 1; /* 5 */
	unsigned int	     verified_sig     : 1; /* 6 */
	unsigned int	     verify_attempted : 1; /* 7 */
	unsigned int	     free_query	      : 1; /* 8 */
	unsigned int	     free_saved	      : 1; /* 9 */
	unsigned int	     cc_ok	      : 1; /* 10 */
	unsigned int	     cc_bad	      : 1; /* 11 */
	unsigned int	     cc_echoed	      : 1; /* 12 */
	unsigned int	     tkey	      : 1; /* 13 */
	unsigned int	     rdclass_set      : 1; /* 14 */
	unsigned int	     fuzzing	      : 1; /* 15 */
	unsigned int	     free_pools	      : 1; /* 16 */
	unsigned int			      : 0;

	unsigned int opt_reserved;
	unsigned int sig_reserved;
	unsigned int reserved; /* reserved space (render) */

	uint16_t     padding;
	unsigned int padding_off;

	isc_buffer_t   *buffer;
	dns_compress_t *cctx;

	isc_mem_t     *mctx;
	isc_mempool_t *namepool;
	isc_mempool_t *rdspool;

	isc_bufferlist_t scratchpad;
	isc_bufferlist_t cleanup;

	ISC_LIST(dns_msgblock_t) rdatas;
	ISC_LIST(dns_msgblock_t) rdatalists;
	ISC_LIST(dns_msgblock_t) offsets;

	ISC_LIST(dns_rdata_t) freerdata;
	ISC_LIST(dns_rdatalist_t) freerdatalist;

	dns_rcode_t	tsigstatus;
	dns_rcode_t	querytsigstatus;
	dns_name_t     *tsigname; /* Owner name of TSIG, if any */
	dns_rdataset_t *querytsig;
	dns_tsigkey_t  *tsigkey;
	dst_context_t  *tsigctx;
	int		sigstart;
	int		timeadjust;

	dns_name_t  *sig0name; /* Owner name of SIG0, if any */
	dst_key_t   *sig0key;
	dns_rcode_t  sig0status;
	isc_region_t query;
	isc_region_t saved;

	/*
	 * Time to be used when fuzzing.
	 */
	isc_stdtime_t fuzztime;

	dns_rdatasetorderfunc_t order;
	dns_sortlist_arg_t	order_arg;

	dns_indent_t indent;

	dns_minttl_t minttl[DNS_SECTION_MAX];
};

struct dns_ednsopt {
	uint16_t code;
	uint16_t length;
	uint8_t *value;
};

typedef void (*dns_message_cb_t)(void *arg, isc_result_t result);

/***
 *** Functions
 ***/

ISC_LANG_BEGINDECLS

void
dns_message_create(isc_mem_t *mctx, isc_mempool_t *namepool,
		   isc_mempool_t *rdspool, dns_message_intent_t intent,
		   dns_message_t **msgp);
/*%<
 * Create msg structure.
 *
 * This function will allocate some internal blocks of memory that are
 * expected to be needed for parsing or rendering nearly any type of message.
 *
 * Requires:
 *\li	'mctx' be a valid memory context.
 *
 *\li	'msgp' be non-null and '*msg' be NULL.
 *
 *\li	'namepool' and 'rdspool' must be either both NULL or both valid
 *	isc_mempool_t
 *
 *\li	'intent' must be one of DNS_MESSAGE_INTENTPARSE or
 *	#DNS_MESSAGE_INTENTRENDER.
 *
 * Ensures:
 *\li	The data in "*msg" is set to indicate an unused and empty msg
 *	structure.
 */

void
dns_message_reset(dns_message_t *msg, dns_message_intent_t intent);
/*%<
 * Reset a message structure to default state.  All internal lists are freed
 * or reset to a default state as well.  This is simply a more efficient
 * way to call dns_message_detach() (assuming last reference is hold),
 * followed by dns_message_create(), since it avoid many memory allocations.
 *
 * If any data loanouts (buffers, names, rdatas, etc) were requested,
 * the caller must no longer use them after this call.
 *
 * The intended next use of the message will be 'intent'.
 *
 * Requires:
 *
 *\li	'msg' be valid.
 *
 *\li	'intent' is DNS_MESSAGE_INTENTPARSE or DNS_MESSAGE_INTENTRENDER
 */

#if DNS_MESSAGE_TRACE
#define dns_message_ref(ptr) dns_message__ref(ptr, __func__, __FILE__, __LINE__)
#define dns_message_unref(ptr) \
	dns_message__unref(ptr, __func__, __FILE__, __LINE__)
#define dns_message_attach(ptr, ptrp) \
	dns_message__attach(ptr, ptrp, __func__, __FILE__, __LINE__)
#define dns_message_detach(ptrp) \
	dns_message__detach(ptrp, __func__, __FILE__, __LINE__)
ISC_REFCOUNT_TRACE_DECL(dns_message);
#else
ISC_REFCOUNT_DECL(dns_message);
#endif
/*
 * Reference counting for dns_message
 */

isc_result_t
dns_message_sectiontotext(dns_message_t *msg, dns_section_t section,
			  const dns_master_style_t *style,
			  dns_messagetextflag_t flags, isc_buffer_t *target);

isc_result_t
dns_message_pseudosectiontotext(dns_message_t *msg, dns_pseudosection_t section,
				const dns_master_style_t *style,
				dns_messagetextflag_t	  flags,
				isc_buffer_t		 *target);
/*%<
 * Convert section 'section' or 'pseudosection' of message 'msg' to
 * a cleartext representation
 *
 * Notes:
 *     \li See dns_message_totext for meanings of flags.
 *
 * Requires:
 *
 *\li	'msg' is a valid message.
 *
 *\li	'style' is a valid master dump style.
 *
 *\li	'target' is a valid buffer.
 *
 *\li	'section' is a named section label.
 *
 * Ensures:
 *
 *\li	If the result is success:
 *		The used space in 'target' is updated.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_NOSPACE
 *\li	#ISC_R_NOMORE
 *
 *\li	Note: On error return, *target may be partially filled with data.
 */

isc_result_t
dns_message_headertotext(dns_message_t *msg, const dns_master_style_t *style,
			 dns_messagetextflag_t flags, isc_buffer_t *target);
/*%<
 * Convert the header section of message 'msg' to a cleartext
 * representation. This is called from dns_message_totext().
 *
 * Notes on flags:
 *\li	If #DNS_MESSAGETEXTFLAG_NOHEADERS is set, header lines will be
 * 	suppressed and this function is a no-op.
 *
 * Requires:
 *
 *\li	'msg' is a valid message.
 *
 *\li	'target' is a valid buffer.
 *
 * Ensures:
 *
 *\li	If the result is success:
 *		The used space in 'target' is updated.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_NOSPACE
 *\li	#ISC_R_NOMORE
 *
 *\li	Note: On error return, *target may be partially filled with data.
 */

isc_result_t
dns_message_totext(dns_message_t *msg, const dns_master_style_t *style,
		   dns_messagetextflag_t flags, isc_buffer_t *target);
/*%<
 * Convert all sections of message 'msg' to a cleartext representation
 *
 * Notes on flags:
 *\li	If #DNS_MESSAGETEXTFLAG_NOCOMMENTS is cleared, lines beginning with
 * 	";;" will be emitted indicating section name.
 *\li	If #DNS_MESSAGETEXTFLAG_NOHEADERS is cleared, header lines will be
 * 	emitted.
 *\li	If #DNS_MESSAGETEXTFLAG_ONESOA is set then only print the first
 *	SOA record in the answer section.
 *\li	If *#DNS_MESSAGETEXTFLAG_OMITSOA is set don't print any SOA records
 *	in the answer section.
 *
 * The SOA flags are useful for suppressing the display of the second
 * SOA record in an AXFR by setting #DNS_MESSAGETEXTFLAG_ONESOA on the
 * first message in an AXFR stream and #DNS_MESSAGETEXTFLAG_OMITSOA on
 * subsequent messages.
 *
 * Requires:
 *
 *\li	'msg' is a valid message.
 *
 *\li	'style' is a valid master dump style.
 *
 *\li	'target' is a valid buffer.
 *
 * Ensures:
 *
 *\li	If the result is success:
 *		The used space in 'target' is updated.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_NOSPACE
 *\li	#ISC_R_NOMORE
 *
 *\li	Note: On error return, *target may be partially filled with data.
 */

isc_result_t
dns_message_parse(dns_message_t *msg, isc_buffer_t *source,
		  unsigned int options);
/*%<
 * Parse raw wire data in 'source' as a DNS message.
 *
 * OPT records are detected and stored in the pseudo-section "opt".
 * TSIGs are detected and stored in the pseudo-section "tsig".
 *
 * If #DNS_MESSAGEPARSE_PRESERVEORDER is set, or if the opcode of the message
 * is UPDATE, a separate dns_name_t object will be created for each RR in the
 * message.  Each such dns_name_t will have a single rdataset containing the
 * single RR, and the order of the RRs in the message is preserved.
 * Otherwise, only one dns_name_t object will be created for each unique
 * owner name in the section, and each such dns_name_t will have a list
 * of rdatasets.  To access the names and their data, use
 * dns_message_firstname() and dns_message_nextname().
 *
 * If #DNS_MESSAGEPARSE_BESTEFFORT is set, errors in message content will
 * not be considered FORMERRs.  If the entire message can be parsed, it
 * will be returned and DNS_R_RECOVERABLE will be returned.
 *
 * If #DNS_MESSAGEPARSE_IGNORETRUNCATION is set then return as many complete
 * RR's as possible, DNS_R_RECOVERABLE will be returned.
 *
 * OPT and TSIG records are always handled specially, regardless of the
 * 'preserve_order' setting.
 *
 * Requires:
 *\li	"msg" be a valid message with parsing intent.
 *
 *\li	"source" be a wire format buffer.
 *
 * Ensures:
 *\li	The buffer's data format is correct.
 *
 *\li	The buffer's contents verify as correct regarding header bits, buffer
 * 	and rdata sizes, etc.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS		-- all is well
 *\li	#ISC_R_NOMEMORY		-- no memory
 *\li	#DNS_R_RECOVERABLE	-- the message parsed properly, but contained
 *				   errors.
 *\li	Many other errors possible XXXMLG
 */

isc_result_t
dns_message_renderbegin(dns_message_t *msg, dns_compress_t *cctx,
			isc_buffer_t *buffer);
/*%<
 * Begin rendering on a message.  Only one call can be made to this function
 * per message.
 *
 * The compression context is "owned" by the message library until
 * dns_message_renderend() is called.  It must be invalidated by the caller.
 *
 * The buffer is "owned" by the message library until dns_message_renderend()
 * is called.
 *
 * Requires:
 *
 *\li	'msg' be a valid message with rendering intent.
 *
 *\li	dns_message_renderbegin() has not previously been called.
 *
 *\li	'cctx' be valid.
 *
 *\li	'buffer' is a valid buffer with length less than 65536.
 *
 * Side Effects:
 *
 *\li	The buffer is cleared before it is used.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS		-- all is well
 *\li	#ISC_R_NOSPACE		-- output buffer is too small
 */

isc_result_t
dns_message_renderchangebuffer(dns_message_t *msg, isc_buffer_t *buffer);
/*%<
 * Reset the buffer.  This can be used after growing the old buffer
 * on a ISC_R_NOSPACE return from most of the render functions.
 *
 * On successful completion, the old buffer is no longer used by the
 * library.  The new buffer is owned by the library until
 * dns_message_renderend() is called.
 *
 * Requires:
 *
 *\li	'msg' be valid.
 *
 *\li	dns_message_renderbegin() was called.
 *
 *\li	buffer != NULL.
 *
 * Returns:
 *\li	#ISC_R_NOSPACE		-- new buffer is too small
 *\li	#ISC_R_SUCCESS		-- all is well.
 */

isc_result_t
dns_message_renderreserve(dns_message_t *msg, unsigned int space);
/*%<
 * XXXMLG should use size_t rather than unsigned int once the buffer
 * API is cleaned up
 *
 * Reserve "space" bytes in the given buffer.
 *
 * Requires:
 *
 *\li	'msg' be valid.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS		-- all is well.
 *\li	#ISC_R_NOSPACE		-- not enough free space in the buffer.
 */

void
dns_message_renderrelease(dns_message_t *msg, unsigned int space);
/*%<
 * XXXMLG should use size_t rather than unsigned int once the buffer
 * API is cleaned up
 *
 * Release "space" bytes in the given buffer that was previously reserved.
 *
 * Requires:
 *
 *\li	'msg' be valid.
 *
 *\li	'space' is less than or equal to the total amount of space reserved
 *	via prior calls to dns_message_renderreserve().
 */

isc_result_t
dns_message_rendersection(dns_message_t *msg, dns_section_t section,
			  unsigned int options);
/*%<
 * Render all names, rdatalists, etc from the given section at the
 * specified priority or higher.
 *
 * Requires:
 *\li	'msg' be valid.
 *
 *\li	'section' be a valid section.
 *
 *\li	dns_message_renderbegin() was called.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS		-- all records were written, and there are
 *				   no more records for this section.
 *\li	#ISC_R_NOSPACE		-- Not enough room in the buffer to write
 *				   all records requested.
 *\li	#DNS_R_MOREDATA		-- All requested records written, and there
 *				   are records remaining for this section.
 */

void
dns_message_renderheader(dns_message_t *msg, isc_buffer_t *target);
/*%<
 * Render the message header.  This is implicitly called by
 * dns_message_renderend().
 *
 * Requires:
 *
 *\li	'msg' be a valid message.
 *
 *\li	dns_message_renderbegin() was called.
 *
 *\li	'target' is a valid buffer with enough space to hold a message header
 */

isc_result_t
dns_message_renderend(dns_message_t *msg);
/*%<
 * Finish rendering to the buffer.  Note that more data can be in the
 * 'msg' structure.  Destroying the structure will free this, or in a multi-
 * part EDNS1 message this data can be rendered to another buffer later.
 *
 * Requires:
 *
 *\li	'msg' be a valid message.
 *
 *\li	dns_message_renderbegin() was called.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS		-- all is well.
 */

void
dns_message_renderreset(dns_message_t *msg);
/*%<
 * Reset the message so that it may be rendered again.
 *
 * Notes:
 *
 *\li	If dns_message_renderbegin() has been called, dns_message_renderend()
 *	must be called before calling this function.
 *
 * Requires:
 *
 *\li	'msg' be a valid message with rendering intent.
 */

isc_result_t
dns_message_firstname(dns_message_t *msg, dns_section_t section);
/*%<
 * Set internal per-section name pointer to the beginning of the section.
 *
 * The functions dns_message_firstname() and dns_message_nextname() may
 * be used for iterating over the owner names in a section.
 *
 * Requires:
 *
 *\li	'msg' be valid.
 *
 *\li	'section' be a valid section.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS		-- All is well.
 *\li	#ISC_R_NOMORE		-- No names on given section.
 */

isc_result_t
dns_message_nextname(dns_message_t *msg, dns_section_t section);
/*%<
 * Sets the internal per-section name pointer to point to the next name
 * in that section.
 *
 * Requires:
 *
 *\li	'msg' be valid.
 *
 *\li	'section' be a valid section.
 *
 *\li	dns_message_firstname() must have been called on this section,
 *	and the result was ISC_R_SUCCESS.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS		-- All is well.
 *\li	#ISC_R_NOMORE		-- No more names in given section.
 */

void
dns_message_currentname(dns_message_t *msg, dns_section_t section,
			dns_name_t **name);
/*%<
 * Sets 'name' to point to the name where the per-section internal name
 * pointer is currently set.
 *
 * This function returns the name in the database, so any data associated
 * with it (via the name's "list" member) contains the actual rdatasets.
 *
 * Requires:
 *
 *\li	'msg' be valid.
 *
 *\li	'name' be non-NULL, and *name be NULL.
 *
 *\li	'section' be a valid section.
 *
 *\li	dns_message_firstname() must have been called on this section,
 *	and the result of it and any dns_message_nextname() calls was
 *	#ISC_R_SUCCESS.
 */

isc_result_t
dns_message_findname(dns_message_t *msg, dns_section_t section,
		     const dns_name_t *target, dns_rdatatype_t type,
		     dns_rdatatype_t covers, dns_name_t **foundname,
		     dns_rdataset_t **rdataset);
/*%<
 * Search for a name in the specified section.  If it is found, *name is
 * set to point to the name, and *rdataset is set to point to the found
 * rdataset (if type is specified as other than dns_rdatatype_any).
 *
 * Requires:
 *\li	'msg' be valid.
 *
 *\li	'section' be a named section.
 *
 *\li	If a pointer to the name is desired, 'foundname' should be non-NULL.
 *	If it is non-NULL, '*foundname' MUST be NULL.
 *
 *\li	If a type other than dns_datatype_any is searched for, 'rdataset'
 *	may be non-NULL, '*rdataset' be NULL, and will point at the found
 *	rdataset.  If the type is dns_datatype_any, 'rdataset' must be NULL.
 *
 *\li	'target' be a valid name.
 *
 *\li	'type' be a valid type.
 *
 *\li	If 'type' is dns_rdatatype_rrsig, 'covers' must be a valid type.
 *	Otherwise it should be 0.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS		-- all is well.
 *\li	#DNS_R_NXDOMAIN		-- name does not exist in that section.
 *\li	#DNS_R_NXRRSET		-- The name does exist, but the desired
 *				   type does not.
 */

isc_result_t
dns_message_findtype(const dns_name_t *name, dns_rdatatype_t type,
		     dns_rdatatype_t covers, dns_rdataset_t **rdataset);
/*%<
 * Search the name for the specified type.  If it is found, *rdataset is
 * filled in with a pointer to that rdataset.
 *
 * Requires:
 *\li	if '**rdataset' is non-NULL, *rdataset needs to be NULL.
 *
 *\li	'type' be a valid type, and NOT dns_rdatatype_any.
 *
 *\li	If 'type' is dns_rdatatype_rrsig, 'covers' must be a valid type.
 *	Otherwise it should be 0.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS		-- all is well.
 *\li	#ISC_R_NOTFOUND		-- the desired type does not exist.
 */

void
dns_message_addname(dns_message_t *msg, dns_name_t *name,
		    dns_section_t section);
/*%<
 * Adds the name to the given section.
 *
 * It is the caller's responsibility to enforce any unique name requirements
 * in a section.
 *
 * Requires:
 *
 *\li	'msg' be valid, and be a renderable message.
 *
 *\li	'name' be a valid absolute name.
 *
 *\li	'section' be a named section.
 */

void
dns_message_removename(dns_message_t *msg, dns_name_t *name,
		       dns_section_t section);
/*%<
 * Remove a existing name from a given section.
 *
 * It is the caller's responsibility to ensure the name is part of the
 * given section.
 *
 * Requires:
 *
 *\li	'msg' be a valid message with rendering intent.
 *
 *\li	'name' be a valid absolute name.
 *
 *\li	'section' be a named section.
 */

/*
 * LOANOUT FUNCTIONS
 *
 * Each of these functions loan a particular type of data to the caller.
 * The storage for these will vanish when the message is destroyed or
 * reset, and must NOT be used after these operations.
 */

void
dns_message_gettempname(dns_message_t *msg, dns_name_t **item);
/*%<
 * Return a name that can be used for any temporary purpose, including
 * inserting into the message's linked lists.  The name must be returned
 * to the message code using dns_message_puttempname() or inserted into
 * one of the message's sections before the message is destroyed.
 *
 * The name will be associated with a dns_fixedname object, and will
 * be initialized.
 *
 * Requires:
 *\li	msg be a valid message
 *
 *\li	item != NULL && *item == NULL
 */

void
dns_message_gettemprdata(dns_message_t *msg, dns_rdata_t **item);
/*%<
 * Return a rdata that can be used for any temporary purpose, including
 * inserting into the message's linked lists.  The rdata will be freed
 * when the message is destroyed or reset.
 *
 * Requires:
 *\li	msg be a valid message
 *
 *\li	item != NULL && *item == NULL
 */

void
dns_message_gettemprdataset(dns_message_t *msg, dns_rdataset_t **item);
/*%<
 * Return a rdataset that can be used for any temporary purpose, including
 * inserting into the message's linked lists. The name must be returned
 * to the message code using dns_message_puttempname() or inserted into
 * one of the message's sections before the message is destroyed.
 *
 * Requires:
 *\li	msg be a valid message
 *
 *\li	item != NULL && *item == NULL
 */

void
dns_message_gettemprdatalist(dns_message_t *msg, dns_rdatalist_t **item);
/*%<
 * Return a rdatalist that can be used for any temporary purpose, including
 * inserting into the message's linked lists.  The rdatalist will be
 * destroyed when the message is destroyed or reset.
 *
 * Requires:
 *\li	msg be a valid message
 *
 *\li	item != NULL && *item == NULL
 */

void
dns_message_puttempname(dns_message_t *msg, dns_name_t **item);
/*%<
 * Return a borrowed name to the message's name free list.
 *
 * Requires:
 *\li	msg be a valid message
 *
 *\li	item != NULL && *item point to a name returned by
 *	dns_message_gettempname()
 *
 * Ensures:
 *\li	*item == NULL
 */

void
dns_message_puttemprdata(dns_message_t *msg, dns_rdata_t **item);
/*%<
 * Return a borrowed rdata to the message's rdata free list.
 *
 * Requires:
 *\li	msg be a valid message
 *
 *\li	item != NULL && *item point to a rdata returned by
 *	dns_message_gettemprdata()
 *
 * Ensures:
 *\li	*item == NULL
 */

void
dns_message_puttemprdataset(dns_message_t *msg, dns_rdataset_t **item);
/*%<
 * Return a borrowed rdataset to the message's rdataset free list.
 *
 * Requires:
 *\li	msg be a valid message
 *
 *\li	item != NULL && *item point to a rdataset returned by
 *	dns_message_gettemprdataset()
 *
 * Ensures:
 *\li	*item == NULL
 */

void
dns_message_puttemprdatalist(dns_message_t *msg, dns_rdatalist_t **item);
/*%<
 * Return a borrowed rdatalist to the message's rdatalist free list.
 *
 * Requires:
 *\li	msg be a valid message
 *
 *\li	item != NULL && *item point to a rdatalist returned by
 *	dns_message_gettemprdatalist()
 *
 * Ensures:
 *\li	*item == NULL
 */

isc_result_t
dns_message_peekheader(isc_buffer_t *source, dns_messageid_t *idp,
		       unsigned int *flagsp);
/*%<
 * Assume the remaining region of "source" is a DNS message.  Peek into
 * it and fill in "*idp" with the message id, and "*flagsp" with the flags.
 *
 * Requires:
 *
 *\li	source != NULL
 *
 * Ensures:
 *
 *\li	if (idp != NULL) *idp == message id.
 *
 *\li	if (flagsp != NULL) *flagsp == message flags.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS		-- all is well.
 *
 *\li	#ISC_R_UNEXPECTEDEND	-- buffer doesn't contain enough for a header.
 */

isc_result_t
dns_message_reply(dns_message_t *msg, bool want_question_section);
/*%<
 * Start formatting a reply to the query in 'msg'.
 *
 * Requires:
 *
 *\li	'msg' is a valid message which contains a query.
 *
 * Ensures:
 *
 *\li	The message will have a rendering intent.  If 'want_question_section'
 *	is true, the message opcode is query or notify, and the question
 *	section is present and properly formatted, then the question section
 *	will be included in the reply.  All other sections will be cleared.
 *	The QR flag will be set, the RD flag will be preserved, and all other
 *	flags will be cleared.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS		-- all is well.
 *
 *\li	#DNS_R_FORMERR		-- the header or question section of the
 *				   message is invalid, replying is impossible.
 *				   If DNS_R_FORMERR is returned when
 *				   want_question_section is false, then
 *				   it's the header section that's bad;
 *				   otherwise either of the header or question
 *				   sections may be bad.
 */

dns_rdataset_t *
dns_message_getopt(dns_message_t *msg);
/*%<
 * Get the OPT record for 'msg'.
 *
 * Requires:
 *
 *\li	'msg' is a valid message.
 *
 * Returns:
 *
 *\li	The OPT rdataset of 'msg', or NULL if there isn't one.
 */

isc_result_t
dns_message_setopt(dns_message_t *msg, dns_rdataset_t *opt);
/*%<
 * Set/clear the OPT record for 'msg'.
 *
 * Requires:
 *
 *\li	'msg' is a valid message with rendering intent
 *	and no sections have been rendered.
 *
 *\li	'opt' is a valid OPT rdataset or NULL.
 *
 * Ensures:
 *
 *\li	The OPT record has either been freed or ownership of it has
 *	been transferred to the message.
 *
 *\li	If ISC_R_SUCCESS was returned, the OPT record will be rendered
 *	when dns_message_renderend() is called.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS		-- all is well.
 *
 *\li	#ISC_R_NOSPACE		-- there is no space for the OPT record.
 */

dns_rdataset_t *
dns_message_gettsig(dns_message_t *msg, const dns_name_t **owner);
/*%<
 * Get the TSIG record and owner for 'msg'.
 *
 * Requires:
 *
 *\li	'msg' is a valid message.
 *\li	'owner' is NULL or *owner is NULL.
 *
 * Returns:
 *
 *\li	The TSIG rdataset of 'msg', or NULL if there isn't one.
 *
 * Ensures:
 *
 *\li	If 'owner' is not NULL, it will point to the owner name.
 */

isc_result_t
dns_message_settsigkey(dns_message_t *msg, dns_tsigkey_t *key);
/*%<
 * Set the tsig key for 'msg'.  This is only necessary for when rendering a
 * query or parsing a response.  The key (if non-NULL) is attached to
 * to the message, and will be detached when the message is destroyed.
 *
 * Requires:
 *
 *\li	'msg' is a valid message.
 *
 *\li	'key' is a valid tsig key or NULL.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS		-- all is well.
 *
 *\li	#ISC_R_NOSPACE		-- there is no space for the TSIG record.
 */

dns_tsigkey_t *
dns_message_gettsigkey(dns_message_t *msg);
/*%<
 * Gets the tsig key for 'msg'.
 *
 * Requires:
 *
 *\li	'msg' is a valid message, and dns_message_settsigkey() has been
 *	run previously.
 */

void
dns_message_setquerytsig(dns_message_t *msg, isc_buffer_t *querytsig);
/*%<
 * Indicates that 'querytsig' is the TSIG from the signed query for which
 * 'msg' is the response.  This is also used for chained TSIGs in TCP
 * responses.
 *
 * Requires:
 *
 *\li	'querytsig' is a valid buffer as returned by dns_message_getquerytsig(),
 *	or NULL
 *
 *\li	'msg' is a valid message on which dns_message_setquerytsig() has
 *	not previously been run.
 */

isc_result_t
dns_message_getquerytsig(dns_message_t *msg, isc_mem_t *mctx,
			 isc_buffer_t **querytsig);
/*%<
 * Gets the tsig from the TSIG from the signed query 'msg'.  This is also used
 * for chained TSIGs in TCP responses.  Unlike dns_message_gettsig, this makes
 * a copy of the data, so can be used if the message is destroyed.
 *
 * Requires:
 *
 *\li	'msg' is a valid signed message
 *\li	'mctx' is a valid memory context
 *\li	querytsig != NULL && *querytsig == NULL
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_NOMEMORY
 *
 * Ensures:
 *\li 	'tsig' points to NULL or an allocated buffer which must be freed
 * 	by the caller.
 */

dns_rdataset_t *
dns_message_getsig0(dns_message_t *msg, const dns_name_t **owner);
/*%<
 * Get the SIG(0) record and owner for 'msg'.
 *
 * Requires:
 *
 *\li	'msg' is a valid message.
 *\li	'owner' is NULL or *owner is NULL.
 *
 * Returns:
 *
 *\li	The SIG(0) rdataset of 'msg', or NULL if there isn't one.
 *
 * Ensures:
 *
 *\li	If 'owner' is not NULL, it will point to the owner name.
 */

isc_result_t
dns_message_setsig0key(dns_message_t *msg, dst_key_t *key);
/*%<
 * Set the SIG(0) key for 'msg'.
 *
 * Requires:
 *
 *\li	'msg' is a valid message with rendering intent,
 *	dns_message_renderbegin() has been called, and no sections have been
 *	rendered.
 *\li	'key' is a valid sig key or NULL.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS		-- all is well.
 *
 *\li	#ISC_R_NOSPACE		-- there is no space for the SIG(0) record.
 */

dst_key_t *
dns_message_getsig0key(dns_message_t *msg);
/*%<
 * Gets the SIG(0) key for 'msg'.
 *
 * Requires:
 *
 *\li	'msg' is a valid message
 */

void
dns_message_takebuffer(dns_message_t *msg, isc_buffer_t **buffer);
/*%<
 * Give the *buffer to the message code to clean up when it is no
 * longer needed.  This is usually when the message is reset or
 * destroyed.
 *
 * Requires:
 *
 *\li	msg be a valid message.
 *
 *\li	buffer != NULL && *buffer is a valid isc_buffer_t, which was
 *	dynamically allocated via isc_buffer_allocate().
 */

isc_result_t
dns_message_signer(dns_message_t *msg, dns_name_t *signer);
/*%<
 * If this message was signed, return the identity of the signer.
 * Unless ISC_R_NOTFOUND is returned, signer will reflect the name of the
 * key that signed the message.
 *
 * Requires:
 *
 *\li	msg is a valid message with parsing intent.
 *\li	signer is a valid name
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS		- the message was signed, and *signer
 *				  contains the signing identity
 *
 *\li	#ISC_R_NOTFOUND		- no TSIG or SIG(0) record is present in the
 *				  message
 *
 *\li	#DNS_R_TSIGVERIFYFAILURE	- the message was signed by a TSIG, but
 * the signature failed to verify
 *
 *\li	#DNS_R_TSIGERRORSET	- the message was signed by a TSIG and
 *				  verified, but the query was rejected by
 *				  the server
 *
 *\li	#DNS_R_NOIDENTITY	- the message was signed by a TSIG and
 *				  verified, but the key has no identity since
 *				  it was generated by an unsigned TKEY process
 *
 *\li	#DNS_R_SIGINVALID	- the message was signed by a SIG(0), but
 *				  the signature failed to verify
 *
 *\li	#DNS_R_NOTVERIFIEDYET	- the message was signed by a TSIG or SIG(0),
 *				  but the signature has not been verified yet
 */

isc_result_t
dns_message_checksig(dns_message_t *msg, dns_view_t *view);
/*%<
 * If this message was signed, verify the signature.
 *
 * Requires:
 *
 *\li	msg is a valid parsed message.
 *\li	view is a valid view or NULL
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS		- the message was unsigned, or the message
 *				  was signed correctly.
 *
 *\li	#DNS_R_EXPECTEDTSIG	- A TSIG was expected, but not seen
 *\li	#DNS_R_UNEXPECTEDTSIG	- A TSIG was seen but not expected
 *\li	#DNS_R_TSIGVERIFYFAILURE - The TSIG failed to verify
 */

isc_result_t
dns_message_checksig_async(dns_message_t *msg, dns_view_t *view,
			   isc_loop_t *loop, dns_message_cb_t cb, void *cbarg);
/*%<
 * Run dns_message_checksig() in an offloaded thread and return its result
 * using the 'cb' callback function, running on the 'loop'.
 *
 * Requires:
 *
 *\li	msg is a valid parsed message.
 *\li	view is a valid view or NULL.
 *\li	loop is a valid loop.
 *\li	cb is a valid callback function.
 *
 * Returns:
 *
 *\li	#DNS_R_WAIT
 *
 */

void
dns_message_resetsig(dns_message_t *msg);
/*%<
 * Reset the signature state.
 *
 * Requires:
 *\li	'msg' is a valid parsed message.
 */

isc_region_t *
dns_message_getrawmessage(dns_message_t *msg);
/*%<
 * Retrieve the raw message in compressed wire format.  The message must
 * have been successfully parsed for it to have been saved.
 *
 * Requires:
 *\li	msg is a valid parsed message.
 *
 * Returns:
 *\li	NULL	if there is no saved message.
 *	a pointer to a region which refers the dns message.
 */

void
dns_message_setsortorder(dns_message_t *msg, dns_rdatasetorderfunc_t order,
			 dns_aclenv_t *env, dns_acl_t *acl,
			 const dns_aclelement_t *element);
/*%<
 * Define the order in which RR sets get rendered by
 * dns_message_rendersection() to be the ascending order
 * defined by the integer value returned by 'order' when
 * given each RR and a ns_sortlist_arg_t constructed from 'env',
 * 'acl', and 'element' as arguments.
 *
 * If 'order' is NULL, a default order is used.
 *
 * Requires:
 *\li	msg be a valid message.
 *\li	If 'env' is NULL, 'order' must be NULL.
 *\li	If 'env' is not NULL, 'order' must not be NULL and at least one of
 *	'acl' and 'element' must also not be NULL.
 */

void
dns_message_settimeadjust(dns_message_t *msg, int timeadjust);
/*%<
 * Adjust the time used to sign/verify a message by timeadjust.
 * Currently only TSIG.
 *
 * Requires:
 *\li	msg be a valid message.
 */

int
dns_message_gettimeadjust(dns_message_t *msg);
/*%<
 * Return the current time adjustment.
 *
 * Requires:
 *\li	msg be a valid message.
 */

void
dns_message_logpacket(dns_message_t *message, const char *description,
		      const isc_sockaddr_t *address,
		      isc_logcategory_t *category, isc_logmodule_t *module,
		      int level, isc_mem_t *mctx);

void
dns_message_logfmtpacket(dns_message_t *message, const char *description,
			 const isc_sockaddr_t *address,
			 isc_logcategory_t *category, isc_logmodule_t *module,
			 const dns_master_style_t *style, int level,
			 isc_mem_t *mctx);
/*%<
 * Log 'message' at the specified logging parameters.
 *
 * For dns_message_logpacket and dns_message_logfmtpacket expect the
 * 'description' to end in a newline.
 *
 * Requires:
 *\li	'message' be a valid DNS message.
 *\li	'description' to be non-NULL.
 *\li	'address' to be non-NULL.
 *\li	'category' to be a valid logging category.
 *\li	'module' to be a valid logging module.
 *\li	'mctx' to be a valid memory context.
 */

isc_result_t
dns_message_buildopt(dns_message_t *msg, dns_rdataset_t **opt,
		     unsigned int version, uint16_t udpsize, unsigned int flags,
		     dns_ednsopt_t *ednsopts, size_t count);
/*%<
 * Built a opt record.
 *
 * Requires:
 *\li	msg be a valid message.
 *\li	opt to be a non NULL and *opt to be NULL.
 *
 * Returns:
 *\li	 ISC_R_SUCCESS
 *\li	 ISC_R_NOSPACE
 */

void
dns_message_setclass(dns_message_t *msg, dns_rdataclass_t rdclass);
/*%<
 * Set the expected class of records in the response.
 *
 * Requires:
 *\li	msg be a valid message with parsing intent.
 */

void
dns_message_setpadding(dns_message_t *msg, uint16_t padding);
/*%<
 * Set the padding block size in the response.
 * 0 means no padding (default).
 *
 * Requires:
 *\li	msg be a valid message.
 */

void
dns_message_clonebuffer(dns_message_t *msg);
/*%<
 * Clone the query or saved buffers if they where not cloned
 * when parsing.
 *
 * Requires:
 *\li	msg be a valid message.
 */

isc_result_t
dns_message_minttl(dns_message_t *msg, const dns_section_t sectionid,
		   dns_ttl_t *pttl);
/*%<
 * Get the smallest TTL from the 'sectionid' section of a rendered
 * message.
 *
 * Requires:
 *\li	msg be a valid rendered message;
 *\li	'pttl != NULL'.
 */

isc_result_t
dns_message_response_minttl(dns_message_t *msg, dns_ttl_t *pttl);
/*%<
 * Get the smalled TTL from the Answer section of 'msg', or if empty, try
 * the MIN(SOA TTL, SOA MINIMUM) value from an SOA record in the Authority
 * section. If neither of these are set, return ISC_R_NOTFOUND.
 *
 * Requires:
 *\li	msg be a valid rendered message;
 *\li	'pttl != NULL'.
 */

void
dns_message_createpools(isc_mem_t *mctx, isc_mempool_t **namepoolp,
			isc_mempool_t **rdspoolp);
void
dns_message_destroypools(isc_mempool_t **namepoolp, isc_mempool_t **rdspoolp);

ISC_LANG_ENDDECLS
