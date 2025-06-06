/*	$NetBSD: name.h,v 1.14 2025/05/21 14:48:04 christos Exp $	*/

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

/*****
***** Module Info
*****/

/*! \file dns/name.h
 * \brief
 * Provides facilities for manipulating DNS names and labels, including
 * conversions to and from wire format and text format.
 *
 * Given the large number of names possible in a nameserver, and because
 * names occur in rdata, it was important to come up with a very efficient
 * way of storing name data, but at the same time allow names to be
 * manipulated.  The decision was to store names in uncompressed wire format,
 * and not to make them fully abstracted objects; i.e. certain parts of the
 * server know names are stored that way.  This saves a lot of memory, and
 * makes adding names to messages easy.  Having much of the server know
 * the representation would be perilous, and we certainly don't want each
 * user of names to be manipulating such a low-level structure.  This is
 * where the Names and Labels module comes in. The module allows name
 * handles to be created and attached to uncompressed wire format
 * regions. All name operations and conversions are done through these
 * handles.
 *
 * MP:
 *\li	Clients of this module must impose any required synchronization.
 *
 * Reliability:
 *\li	This module deals with low-level byte streams.  Errors in any of
 *	the functions are likely to crash the server or corrupt memory.
 *
 * Resources:
 *\li	None.
 *
 * Security:
 *
 *\li	*** WARNING ***
 *
 *\li	dns_name_fromwire() deals with raw network data.  An error in
 *	this routine could result in the failure or hijacking of the server.
 *
 * Standards:
 *\li	RFC1035
 *\li	Draft EDNS0 (0)
 *
 */

/***
 *** Imports
 ***/

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include <isc/buffer.h>
#include <isc/hashmap.h>
#include <isc/lang.h>
#include <isc/magic.h>
#include <isc/region.h> /* Required for storage size of dns_label_t. */

#include <dns/types.h>

ISC_LANG_BEGINDECLS

/*****
***** Names
*****
***** A 'name' is a handle to a binary region.  It contains a sequence of one
***** or more DNS wire format labels.
***** Note that all names are not required to end with the root label,
***** as they are in the actual DNS wire protocol.
*****/

/***
 *** Types
 ***/

/*%
 * Clients are strongly discouraged from using this type directly,  with
 * the exception of the 'link' and 'list' fields which may be used directly
 * for whatever purpose the client desires.
 */
struct dns_name {
	unsigned int magic;
	uint8_t	     length;
	uint8_t	     labels;
	struct dns_name_attrs {
		bool absolute	  : 1; /*%< Used by name.c */
		bool readonly	  : 1; /*%< Used by name.c */
		bool dynamic	  : 1; /*%< Used by name.c */
		bool dynoffsets	  : 1; /*%< Used by name.c */
		bool nocompress	  : 1; /*%< Used by name.c */
		bool cache	  : 1; /*%< Used by resolver. */
		bool answer	  : 1; /*%< Used by resolver. */
		bool ncache	  : 1; /*%< Used by resolver. */
		bool chaining	  : 1; /*%< Used by resolver. */
		bool chase	  : 1; /*%< Used by resolver. */
		bool wildcard	  : 1; /*%< Used by server. */
		bool prerequisite : 1; /*%< Used by client. */
		bool update	  : 1; /*%< Used by client. */
		bool hasupdaterec : 1; /*%< Used by client. */
	} attributes;
	unsigned char *ndata;
	unsigned char *offsets;
	isc_buffer_t  *buffer;
	ISC_LINK(dns_name_t) link;
	ISC_LIST(dns_rdataset_t) list;
	isc_hashmap_t *hashmap;
};

#define DNS_NAME_MAGIC	  ISC_MAGIC('D', 'N', 'S', 'n')
#define DNS_NAME_VALID(n) ISC_MAGIC_VALID(n, DNS_NAME_MAGIC)

/*%
 * A name is "bindable" if it can be set to point to a new value, i.e.
 * name->ndata and name->length may be changed.
 */
#define DNS_NAME_BINDABLE(name) \
	(!name->attributes.readonly && !name->attributes.dynamic)

/*
 * Various flags.
 */
#define DNS_NAME_DOWNCASE	0x0001
#define DNS_NAME_CHECKNAMES	0x0002 /*%< Used by rdata. */
#define DNS_NAME_CHECKNAMESFAIL 0x0004 /*%< Used by rdata. */
#define DNS_NAME_CHECKREVERSE	0x0008 /*%< Used by rdata. */
#define DNS_NAME_CHECKMX	0x0010 /*%< Used by rdata. */
#define DNS_NAME_CHECKMXFAIL	0x0020 /*%< Used by rdata. */

extern const dns_name_t *dns_rootname;
extern const dns_name_t *dns_wildcardname;

/*%<
 * DNS_NAME_INITNONABSOLUTE and DNS_NAME_INITABSOLUTE are macros for
 * initializing dns_name_t structures.
 *
 * Note[1]: 'length' is set to (sizeof(A) - 1) in DNS_NAME_INITNONABSOLUTE
 * and sizeof(A) in DNS_NAME_INITABSOLUTE to allow C strings to be used
 * to initialize 'ndata'.
 *
 * Note[2]: The final value of offsets for DNS_NAME_INITABSOLUTE should
 * match (sizeof(A) - 1) which is the offset of the root label.
 *
 * Typical usage:
 *	unsigned char data[] = "\005value";
 *	unsigned char offsets[] = { 0 };
 *	dns_name_t value = DNS_NAME_INITNONABSOLUTE(data, offsets);
 *
 *	unsigned char data[] = "\005value";
 *	unsigned char offsets[] = { 0, 6 };
 *	dns_name_t value = DNS_NAME_INITABSOLUTE(data, offsets);
 */
#define DNS_NAME_INITNONABSOLUTE(A, B)              \
	{                                           \
		.magic = DNS_NAME_MAGIC,            \
		.ndata = A,                         \
		.length = (sizeof(A) - 1),          \
		.labels = sizeof(B),                \
		.attributes = { .readonly = true }, \
		.offsets = B,                       \
		.link = ISC_LINK_INITIALIZER,       \
		.list = ISC_LIST_INITIALIZER,       \
	}

#define DNS_NAME_INITABSOLUTE(A, B)                                   \
	{                                                             \
		.magic = DNS_NAME_MAGIC,                              \
		.ndata = A,                                           \
		.length = sizeof(A),                                  \
		.labels = sizeof(B),                                  \
		.attributes = { .readonly = true, .absolute = true }, \
		.offsets = B,                                         \
		.link = ISC_LINK_INITIALIZER,                         \
		.list = ISC_LIST_INITIALIZER,                         \
	}

#define DNS_NAME_INITEMPTY              \
	{ .magic = DNS_NAME_MAGIC,      \
	  .link = ISC_LINK_INITIALIZER, \
	  .list = ISC_LIST_INITIALIZER }

/*%
 * Standard sizes of a wire format name
 */
#define DNS_NAME_MAXWIRE   255
#define DNS_NAME_MAXLABELS 128
#define DNS_NAME_LABELLEN  63

typedef unsigned char dns_offsets_t[DNS_NAME_MAXLABELS];

/*
 * Text output filter procedure.
 * 'target' is the buffer to be converted.  The region to be converted
 * is from 'buffer'->base + 'used_org' to the end of the used region.
 */
typedef isc_result_t(dns_name_totextfilter_t)(isc_buffer_t *target,
					      unsigned int  used_org);

/***
 *** Initialization
 ***/

static inline void
dns_name_init(dns_name_t *name, unsigned char *offsets) {
	*name = (dns_name_t){
		.magic = DNS_NAME_MAGIC,
		.offsets = (offsets),
		.link = ISC_LINK_INITIALIZER,
		.list = ISC_LIST_INITIALIZER,
	};
}
/*%<
 * Initialize 'name'.
 *
 * Notes:
 * \li	'offsets' is never required to be non-NULL, but specifying a
 *	dns_offsets_t for 'offsets' will improve the performance of most
 *	name operations if the name is used more than once.
 *
 * Requires:
 * \li	'name' is not NULL and points to a struct dns_name.
 *
 * \li	offsets == NULL or offsets is a dns_offsets_t.
 *
 * Ensures:
 * \li	'name' is a valid name.
 * \li	dns_name_countlabels(name) == 0
 * \li	dns_name_isabsolute(name) == false
 */

static inline void
dns_name_reset(dns_name_t *name) {
	REQUIRE(DNS_NAME_VALID(name));
	REQUIRE(DNS_NAME_BINDABLE(name));

	name->ndata = NULL;
	name->length = 0;
	name->labels = 0;
	name->attributes.absolute = false;
	if (name->buffer != NULL) {
		isc_buffer_clear(name->buffer);
	}
}
/*%<
 * Reinitialize 'name'.
 *
 * Notes:
 * \li	This function distinguishes itself from dns_name_init() in two
 *	key ways:
 *
 * \li	+ If any buffer is associated with 'name' (via dns_name_setbuffer()
 *	  or by being part of a dns_fixedname_t) the link to the buffer
 *	  is retained but the buffer itself is cleared.
 *
 * \li	+ Of the attributes associated with 'name', all are retained except
 *	  the absolute flag.
 *
 * Requires:
 * \li	'name' is a valid name.
 *
 * Ensures:
 * \li	'name' is a valid name.
 * \li	dns_name_countlabels(name) == 0
 * \li	dns_name_isabsolute(name) == false
 */

static inline void
dns_name_invalidate(dns_name_t *name) {
	REQUIRE(DNS_NAME_VALID(name));

	name->magic = 0;
	name->ndata = NULL;
	name->length = 0;
	name->labels = 0;
	name->attributes = (struct dns_name_attrs){};
	name->offsets = NULL;
	name->buffer = NULL;
	ISC_LINK_INIT(name, link);
}
/*%<
 * Make 'name' invalid.
 *
 * Requires:
 * \li	'name' is a valid name.
 *
 * Ensures:
 * \li	If assertion checking is enabled, future attempts to use 'name'
 *	without initializing it will cause an assertion failure.
 *
 * \li	If the name had a dedicated buffer, that association is ended.
 */

bool
dns_name_isvalid(const dns_name_t *name);
/*%<
 * Check whether 'name' points to a valid dns_name
 */

/***
 *** Dedicated Buffers
 ***/

static inline void
dns_name_setbuffer(dns_name_t *name, isc_buffer_t *buffer) {
	REQUIRE(DNS_NAME_VALID(name));
	REQUIRE((buffer != NULL && name->buffer == NULL) || (buffer == NULL));

	name->buffer = buffer;
}
/*%<
 * Dedicate a buffer for use with 'name'.
 *
 * Notes:
 * \li	Specification of a target buffer in dns_name_fromwire(),
 *	dns_name_fromtext(), and dns_name_concatenate() is optional if
 *	'name' has a dedicated buffer.
 *
 * \li	The caller must not write to buffer until the name has been
 *	invalidated or is otherwise known not to be in use.
 *
 * \li	If buffer is NULL and the name previously had a dedicated buffer,
 *	than that buffer is no longer dedicated to use with this name.
 *	The caller is responsible for ensuring that the storage used by
 *	the name remains valid.
 *
 * Requires:
 * \li	'name' is a valid name.
 *
 * \li	'buffer' is a valid binary buffer and 'name' doesn't have a
 *	dedicated buffer already, or 'buffer' is NULL.
 */

bool
dns_name_hasbuffer(const dns_name_t *name);
/*%<
 * Does 'name' have a dedicated buffer?
 *
 * Requires:
 * \li	'name' is a valid name.
 *
 * Returns:
 * \li	true	'name' has a dedicated buffer.
 * \li	false	'name' does not have a dedicated buffer.
 */

/***
 *** Properties
 ***/

bool
dns_name_isabsolute(const dns_name_t *name);
/*%<
 * Does 'name' end in the root label?
 *
 * Requires:
 * \li	'name' is a valid name
 *
 * Returns:
 * \li	TRUE		The last label in 'name' is the root label.
 * \li	FALSE		The last label in 'name' is not the root label.
 */

bool
dns_name_iswildcard(const dns_name_t *name);
/*%<
 * Is 'name' a wildcard name?
 *
 * Requires:
 * \li	'name' is a valid name
 *
 * \li	dns_name_countlabels(name) > 0
 *
 * Returns:
 * \li	TRUE		The least significant label of 'name' is '*'.
 * \li	FALSE		The least significant label of 'name' is not '*'.
 */

uint32_t
dns_name_hash(const dns_name_t *name);
/*%<
 * Provide a hash value for 'name'.
 *
 * Note: This function always takes into account of the entire name to calculate
 * the hash value. The names which differ only in case will have the same hash
 * value.
 *
 * Requires:
 *\li	'name' is a valid name
 *
 * Returns:
 *\li	A hash value
 */

/*
 *** Comparisons
 ***/

dns_namereln_t
dns_name_fullcompare(const dns_name_t *name1, const dns_name_t *name2,
		     int *orderp, unsigned int *nlabelsp);
/*%<
 * Determine the relative ordering under the DNSSEC order relation of
 * 'name1' and 'name2', and also determine the hierarchical
 * relationship of the names.
 *
 * Note: It makes no sense for one of the names to be relative and the
 * other absolute.  If both names are relative, then to be meaningfully
 * compared the caller must ensure that they are both relative to the
 * same domain.
 *
 * Requires:
 *\li	'name1' is a valid name
 *
 *\li	dns_name_countlabels(name1) > 0
 *
 *\li	'name2' is a valid name
 *
 *\li	dns_name_countlabels(name2) > 0
 *
 *\li	orderp and nlabelsp are valid pointers.
 *
 *\li	Either name1 is absolute and name2 is absolute, or neither is.
 *
 * Ensures:
 *
 *\li	*orderp is < 0 if name1 < name2, 0 if name1 = name2, > 0 if
 *	name1 > name2.
 *
 *\li	*nlabelsp is the number of common significant labels.
 *
 * Returns:
 *\li	dns_namereln_none		There's no hierarchical relationship
 *					between name1 and name2.
 *\li	dns_namereln_contains		name1 properly contains name2; i.e.
 *					name2 is a proper subdomain of name1.
 *\li	dns_namereln_subdomain		name1 is a proper subdomain of name2.
 *\li	dns_namereln_equal		name1 and name2 are equal.
 *\li	dns_namereln_commonancestor	name1 and name2 share a common
 *					ancestor.
 */

int
dns_name_compare(const dns_name_t *name1, const dns_name_t *name2);
/*%<
 * Determine the relative ordering under the DNSSEC order relation of
 * 'name1' and 'name2'.
 *
 * Note: It makes no sense for one of the names to be relative and the
 * other absolute.  If both names are relative, then to be meaningfully
 * compared the caller must ensure that they are both relative to the
 * same domain.
 *
 * Requires:
 * \li	'name1' is a valid name
 *
 * \li	'name2' is a valid name
 *
 * \li	Either name1 is absolute and name2 is absolute, or neither is.
 *
 * Returns:
 * \li	< 0		'name1' is less than 'name2'
 * \li	0		'name1' is equal to 'name2'
 * \li	> 0		'name1' is greater than 'name2'
 */

bool
dns_name_equal(const dns_name_t *name1, const dns_name_t *name2);
/*%<
 * Are 'name1' and 'name2' equal?
 *
 * Notes:
 * \li	Because it only needs to test for equality, dns_name_equal() can be
 *	significantly faster than dns_name_fullcompare() or dns_name_compare().
 *
 * \li	Offsets tables are not used in the comparison.
 *
 * \li	It makes no sense for one of the names to be relative and the
 *	other absolute.  If both names are relative, then to be meaningfully
 * 	compared the caller must ensure that they are both relative to the
 * 	same domain.
 *
 * Requires:
 * \li	'name1' is a valid name
 *
 * \li	'name2' is a valid name
 *
 * \li	Either name1 is absolute and name2 is absolute, or neither is.
 *
 * Returns:
 * \li	true	'name1' and 'name2' are equal
 * \li	false	'name1' and 'name2' are not equal
 */

bool
dns_name_caseequal(const dns_name_t *name1, const dns_name_t *name2);
/*%<
 * Case sensitive version of dns_name_equal().
 */

int
dns_name_rdatacompare(const dns_name_t *name1, const dns_name_t *name2);
/*%<
 * Compare two names as if they are part of rdata in DNSSEC canonical
 * form.
 *
 * Requires:
 * \li	'name1' is a valid absolute name
 *
 * \li	dns_name_countlabels(name1) > 0
 *
 * \li	'name2' is a valid absolute name
 *
 * \li	dns_name_countlabels(name2) > 0
 *
 * Returns:
 * \li	< 0		'name1' is less than 'name2'
 * \li	0		'name1' is equal to 'name2'
 * \li	> 0		'name1' is greater than 'name2'
 */

bool
dns_name_issubdomain(const dns_name_t *name1, const dns_name_t *name2);
/*%<
 * Is 'name1' a subdomain of 'name2'?
 *
 * Notes:
 * \li	name1 is a subdomain of name2 if name1 is contained in name2, or
 *	name1 equals name2.
 *
 * \li	It makes no sense for one of the names to be relative and the
 *	other absolute.  If both names are relative, then to be meaningfully
 *	compared the caller must ensure that they are both relative to the
 *	same domain.
 *
 * Requires:
 * \li	'name1' is a valid name
 *
 * \li	'name2' is a valid name
 *
 * \li	Either name1 is absolute and name2 is absolute, or neither is.
 *
 * Returns:
 * \li	TRUE		'name1' is a subdomain of 'name2'
 * \li	FALSE		'name1' is not a subdomain of 'name2'
 */

bool
dns_name_matcheswildcard(const dns_name_t *name, const dns_name_t *wname);
/*%<
 * Does 'name' match the wildcard specified in 'wname'?
 *
 * Notes:
 * \li	name matches the wildcard specified in wname if all labels
 *	following the wildcard in wname are identical to the same number
 *	of labels at the end of name.
 *
 * \li	It makes no sense for one of the names to be relative and the
 *	other absolute.  If both names are relative, then to be meaningfully
 *	compared the caller must ensure that they are both relative to the
 *	same domain.
 *
 * Requires:
 * \li	'name' is a valid name
 *
 * \li	dns_name_countlabels(name) > 0
 *
 * \li	'wname' is a valid name
 *
 * \li	dns_name_countlabels(wname) > 0
 *
 * \li	dns_name_iswildcard(wname) is true
 *
 * \li	Either name is absolute and wname is absolute, or neither is.
 *
 * Returns:
 * \li	TRUE		'name' matches the wildcard specified in 'wname'
 * \li	FALSE		'name' does not match the wildcard specified in 'wname'
 */

/***
 *** Labels
 ***/

static inline unsigned int
dns_name_countlabels(const dns_name_t *name) {
	REQUIRE(DNS_NAME_VALID(name));
	REQUIRE(name->labels <= DNS_NAME_MAXLABELS);

	return name->labels;
}
/*%<
 * How many labels does 'name' have?
 *
 * Notes:
 * \li	In this case, as in other places, a 'label' is an ordinary label.
 *
 * Requires:
 * \li	'name' is a valid name
 *
 * Ensures:
 * \li	The result is <= 128.
 *
 * Returns:
 * \li	The number of labels in 'name'.
 */

void
dns_name_getlabel(const dns_name_t *name, unsigned int n, dns_label_t *label);
/*%<
 * Make 'label' refer to the 'n'th least significant label of 'name'.
 *
 * Notes:
 * \li	Numbering starts at 0.
 *
 * \li	Given "rc.vix.com.", the label 0 is "rc", and label 3 is the
 *	root label.
 *
 * \li	'label' refers to the same memory as 'name', so 'name' must not
 *	be changed while 'label' is still in use.
 *
 * Requires:
 * \li	n < dns_name_countlabels(name)
 */

void
dns_name_getlabelsequence(const dns_name_t *source, unsigned int first,
			  unsigned int n, dns_name_t *target);
/*%<
 * Make 'target' refer to the 'n' labels including and following 'first'
 * in 'source'.
 *
 * Notes:
 * \li	Numbering starts at 0.
 *
 * \li	Given "rc.vix.com.", the label 0 is "rc", and label 3 is the
 *	root label.
 *
 * \li	'target' refers to the same memory as 'source', so 'source'
 *	must not be changed while 'target' is still in use.
 *
 * Requires:
 * \li	'source' and 'target' are valid names.
 *
 * \li	first < dns_name_countlabels(name)
 *
 * \li	first + n <= dns_name_countlabels(name)
 */

void
dns_name_clone(const dns_name_t *source, dns_name_t *target);
/*%<
 * Make 'target' refer to the same name as 'source'.
 *
 * Notes:
 *
 * \li	'target' refers to the same memory as 'source', so 'source'
 *	must not be changed or freed while 'target' is still in use.
 *
 * \li	This call is functionally equivalent to:
 *
 * \code
 *		dns_name_getlabelsequence(source, 0,
 *					  dns_name_countlabels(source),
 *					  target);
 * \endcode
 *
 *	but is more efficient.  Also, dns_name_clone() works even if 'source'
 *	is empty.
 *
 * Requires:
 *
 * \li	'source' is a valid name.
 *
 * \li	'target' is a valid name that is not read-only.
 */

/***
 *** Conversions
 ***/

void
dns_name_fromregion(dns_name_t *name, const isc_region_t *r);
/*%<
 * Make 'name' refer to region 'r'.
 *
 * Note:
 * \li	If the conversion encounters a root label before the end of the
 *	region the conversion stops and the length is set to the length
 *	so far converted.  A maximum of 255 bytes is converted.
 *
 * Requires:
 * \li	The data in 'r' is a sequence of one or more type 00 labels.
 */

static inline void
dns_name_toregion(const dns_name_t *name, isc_region_t *r) {
	REQUIRE(DNS_NAME_VALID(name));
	REQUIRE(r != NULL);

	r->base = name->ndata;
	r->length = name->length;
}
/*%<
 * Make 'r' refer to 'name'.
 *
 * Requires:
 *
 * \li	'name' is a valid name.
 *
 * \li	'r' is a valid region.
 */

isc_result_t
dns_name_fromwire(dns_name_t *name, isc_buffer_t *source, dns_decompress_t dctx,
		  isc_buffer_t *target);
/*%<
 * Copy the possibly-compressed name at source (active region) into target,
 * decompressing it.
 *
 * Notes:
 * \li	Decompression policy is controlled by 'dctx'.
 *
 * Security:
 *
 * \li	*** WARNING ***
 *
 * \li	This routine will often be used when 'source' contains raw network
 *	data.  A programming error in this routine could result in a denial
 *	of service, or in the hijacking of the server.
 *
 * Requires:
 *
 * \li	'name' is a valid name.
 *
 * \li	'source' is a valid buffer and the first byte of the active
 *	region should be the first byte of a DNS wire format domain name.
 *
 * \li	'target' is a valid buffer or 'target' is NULL and 'name' has
 *	a dedicated buffer.
 *
 * \li	'dctx' is a valid decompression context.
 *
 * \li	DNS_NAME_DOWNCASE is not set.
 *
 * Ensures:
 *
 *	If result is success:
 * \li		If 'target' is not NULL, 'name' is attached to it.
 *
 * \li		The current location in source is advanced, and the used space
 *		in target is updated.
 *
 * Result:
 * \li	Success
 * \li	Bad Form: Label Length
 * \li	Bad Form: Unknown Label Type
 * \li	Bad Form: Name Length
 * \li	Bad Form: Compression type not allowed
 * \li	Bad Form: Bad compression pointer
 * \li	Bad Form: Input too short
 * \li	Resource Limit: Not enough space in buffer
 */

isc_result_t
dns_name_towire(const dns_name_t *name, dns_compress_t *cctx,
		isc_buffer_t *target, uint16_t *comp_offsetp);
/*%<
 * Convert 'name' into wire format, compressing it as specified by the
 * compression context 'cctx', and storing the result in 'target'.
 *
 * Notes:
 * \li	If compression is permitted, then the cctx table may be updated.
 *
 * Requires:
 * \li	'name' is a valid name
 *
 * \li	dns_name_countlabels(name) > 0
 *
 * \li	dns_name_isabsolute(name) == TRUE
 *
 * \li	target is a valid buffer.
 *
 * \li	Any offsets in the compression table are valid for buffer.
 *
 * Ensures:
 *
 *	If the result is success:
 *
 * \li		The used space in target is updated.
 *
 * Returns:
 * \li	Success
 * \li	Resource Limit: Not enough space in buffer
 */

isc_result_t
dns_name_fromtext(dns_name_t *name, isc_buffer_t *source,
		  const dns_name_t *origin, unsigned int options,
		  isc_buffer_t *target);
/*%<
 * Convert the textual representation of a DNS name at source
 * into uncompressed wire form stored in target.
 *
 * Notes:
 * \li	Relative domain names will have 'origin' appended to them
 *	unless 'origin' is NULL, in which case relative domain names
 *	will remain relative.
 *
 * \li	If DNS_NAME_DOWNCASE is set in 'options', any uppercase letters
 *	in 'source' will be downcased when they are copied into 'target'.
 *
 * Requires:
 *
 * \li	'name' is a valid name.
 *
 * \li	'source' is a valid buffer.
 *
 * \li	'target' is a valid buffer or 'target' is NULL and 'name' has
 *	a dedicated buffer.
 *
 * Ensures:
 *
 *	If result is success:
 * \li	 	If 'target' is not NULL, 'name' is attached to it.
 *
 * \li		Uppercase letters are downcased in the copy iff
 *		DNS_NAME_DOWNCASE is set in 'options'.
 *
 * \li		The current location in source is advanced, and the used space
 *		in target is updated.
 *
 * Result:
 *\li	#ISC_R_SUCCESS
 *\li	#DNS_R_EMPTYLABEL
 *\li	#DNS_R_LABELTOOLONG
 *\li	#DNS_R_BADESCAPE
 *\li	#DNS_R_BADDOTTEDQUAD
 *\li	#ISC_R_NOSPACE
 *\li	#ISC_R_UNEXPECTEDEND
 */

#define DNS_NAME_OMITFINALDOT 0x01U
#define DNS_NAME_PRINCIPAL    0x02U /* do not escape $ and @ */

isc_result_t
dns_name_totext(const dns_name_t *name, unsigned int options,
		isc_buffer_t *target);
/*%<
 * Convert 'name' into text format, storing the result in 'target'.
 *
 * Notes:
 *\li	If DNS_NAME_OMITFINALDOT is set in options, then the final '.'
 *	in absolute names other than the root name will be omitted.
 *
 *\li	If DNS_NAME_PRINCIPAL is set in options, '$' and '@' will *not*
 *	be escaped; otherwise they will, along with other characters that
 *	are special in zone files ('"', '(', ')', '.', ';', and '\'),
 *	which are always escaped.
 *
 *\li	If dns_name_countlabels == 0, the name will be "@", representing the
 *	current origin as described by RFC1035.
 *
 *\li	The name is not NUL terminated.
 *
 * Requires:
 *
 *\li	'name' is a valid name
 *
 *\li	'target' is a valid buffer
 *
 *\li	if dns_name_isabsolute is false, then omit_final_dot is false
 *
 * Ensures:
 *
 *\li	If the result is success:
 *		the used space in target is updated.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_NOSPACE
 */

#define DNS_NAME_MAXTEXT 1023
/*%<
 * The maximum length of the text representation of a domain
 * name as generated by dns_name_totext().  This does not
 * include space for a terminating NULL.
 *
 * This definition is conservative - the actual maximum
 * is 1004, derived as follows:
 *
 *   A backslash-decimal escaped character takes 4 bytes.
 *   A wire-encoded name can be up to 255 bytes and each
 *   label is one length byte + at most 63 bytes of data.
 *   Maximizing the label lengths gives us a name of
 *   three 63-octet labels, one 61-octet label, and the
 *   root label:
 *
 *      1 + 63 + 1 + 63 + 1 + 63 + 1 + 61 + 1 = 255
 *
 *   When printed, this is (3 * 63 + 61) * 4
 *   bytes for the escaped label data + 4 bytes for the
 *   dot terminating each label = 1004 bytes total.
 */

isc_result_t
dns_name_tofilenametext(const dns_name_t *name, bool omit_final_dot,
			isc_buffer_t *target);
/*%<
 * Convert 'name' into an alternate text format appropriate for filenames,
 * storing the result in 'target'.  The name data is downcased, guaranteeing
 * that the filename does not depend on the case of the converted name.
 *
 * Notes:
 *\li	If 'omit_final_dot' is true, then the final '.' in absolute
 *	names other than the root name will be omitted.
 *
 *\li	The name is not NUL terminated.
 *
 * Requires:
 *
 *\li	'name' is a valid absolute name
 *
 *\li	'target' is a valid buffer.
 *
 * Ensures:
 *
 *\li	If the result is success:
 *		the used space in target is updated.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_NOSPACE
 */

isc_result_t
dns_name_downcase(const dns_name_t *source, dns_name_t *name,
		  isc_buffer_t *target);
/*%<
 * Downcase 'source'.
 *
 * Requires:
 *
 *\li	'source' and 'name' are valid names.
 *
 *\li	If source == name, then
 *		'source' must not be read-only
 *
 *\li	Otherwise,
 *		'target' is a valid buffer or 'target' is NULL and
 *		'name' has a dedicated buffer.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_NOSPACE
 *
 * Note: if source == name, then the result will always be ISC_R_SUCCESS.
 */

isc_result_t
dns_name_concatenate(const dns_name_t *prefix, const dns_name_t *suffix,
		     dns_name_t *name, isc_buffer_t *target);
/*%<
 *	Concatenate 'prefix' and 'suffix'.
 *
 * Requires:
 *
 *\li	'prefix' is a valid name or NULL.
 *
 *\li	'suffix' is a valid name or NULL.
 *
 *\li	'name' is a valid name or NULL.
 *
 *\li	'target' is a valid buffer or 'target' is NULL and 'name' has
 *	a dedicated buffer.
 *
 *\li	If 'prefix' is absolute, 'suffix' must be NULL or the empty name.
 *
 * Ensures:
 *
 *\li	On success,
 *	 	If 'target' is not NULL and 'name' is not NULL, then 'name'
 *		is attached to it.
 *		The used space in target is updated.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_NOSPACE
 *\li	#DNS_R_NAMETOOLONG
 */

static inline void
dns_name_split(const dns_name_t *name, unsigned int suffixlabels,
	       dns_name_t *prefix, dns_name_t *suffix) {
	REQUIRE(DNS_NAME_VALID(name));
	REQUIRE(suffixlabels > 0);
	REQUIRE(suffixlabels <= name->labels);
	REQUIRE(prefix != NULL || suffix != NULL);
	REQUIRE(prefix == NULL ||
		(DNS_NAME_VALID(prefix) && DNS_NAME_BINDABLE(prefix)));
	REQUIRE(suffix == NULL ||
		(DNS_NAME_VALID(suffix) && DNS_NAME_BINDABLE(suffix)));

	if (prefix != NULL) {
		dns_name_getlabelsequence(name, 0, name->labels - suffixlabels,
					  prefix);
	}
	if (suffix != NULL) {
		dns_name_getlabelsequence(name, name->labels - suffixlabels,
					  suffixlabels, suffix);
	}
}
/*%<
 *
 * Split 'name' into two pieces on a label boundary.
 *
 * Notes:
 * \li     'name' is split such that 'suffix' holds the most significant
 *      'suffixlabels' labels.  All other labels are stored in 'prefix'.
 *
 *\li	Copying name data is avoided as much as possible, so 'prefix'
 *	and 'suffix' will end up pointing at the data for 'name'.
 *
 *\li	It is legitimate to pass a 'prefix' or 'suffix' that has
 *	its name data stored someplace other than the dedicated buffer.
 *	This is useful to avoid name copying in the calling function.
 *
 *\li	It is also legitimate to pass a 'prefix' or 'suffix' that is
 *	the same dns_name_t as 'name'.
 *
 * Requires:
 *\li	'name' is a valid name.
 *
 *\li	'suffixlabels' cannot exceed the number of labels in 'name'.
 *
 * \li	'prefix' is a valid name or NULL, and cannot be read-only.
 *
 *\li	'suffix' is a valid name or NULL, and cannot be read-only.
 *
 * Ensures:
 *
 *\li	On success:
 *		If 'prefix' is not NULL it will contain the least significant
 *		labels.
 *		If 'suffix' is not NULL it will contain the most significant
 *		labels.  dns_name_countlabels(suffix) will be equal to
 *		suffixlabels.
 *
 *\li	On failure:
 *		Either 'prefix' or 'suffix' is invalidated (depending
 *		on which one the problem was encountered with).
 *
 * Returns:
 *\li	#ISC_R_SUCCESS	No worries.  (This function should always success).
 */

void
dns_name_dup(const dns_name_t *source, isc_mem_t *mctx, dns_name_t *target);
/*%<
 * Make 'target' a dynamically allocated copy of 'source'.
 *
 * Requires:
 *
 *\li	'source' is a valid non-empty name.
 *
 *\li	'target' is a valid name that is not read-only.
 *
 *\li	'mctx' is a valid memory context.
 */

void
dns_name_dupwithoffsets(const dns_name_t *source, isc_mem_t *mctx,
			dns_name_t *target);
/*%<
 * Make 'target' a read-only dynamically allocated copy of 'source'.
 * 'target' will also have a dynamically allocated offsets table.
 *
 * Requires:
 *
 *\li	'source' is a valid non-empty name.
 *
 *\li	'target' is a valid name that is not read-only.
 *
 *\li	'target' has no offsets table.
 *
 *\li	'mctx' is a valid memory context.
 */

void
dns_name_free(dns_name_t *name, isc_mem_t *mctx);
/*%<
 * Free 'name'.
 *
 * Requires:
 *
 *\li	'name' is a valid name created previously in 'mctx' by dns_name_dup().
 *
 *\li	'mctx' is a valid memory context.
 *
 * Ensures:
 *
 *\li	All dynamic resources used by 'name' are freed and the name is
 *	invalidated.
 */

isc_result_t
dns_name_digest(const dns_name_t *name, dns_digestfunc_t digest, void *arg);
/*%<
 * Send 'name' in DNSSEC canonical form to 'digest'.
 *
 * Requires:
 *
 *\li	'name' is a valid name.
 *
 *\li	'digest' is a valid dns_digestfunc_t.
 *
 * Ensures:
 *
 *\li	If successful, the DNSSEC canonical form of 'name' will have been
 *	sent to 'digest'.
 *
 *\li	If digest() returns something other than ISC_R_SUCCESS, that result
 *	will be returned as the result of dns_name_digest().
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS
 *
 *\li	Many other results are possible if not successful.
 *
 */

bool
dns_name_dynamic(const dns_name_t *name);
/*%<
 * Returns whether there is dynamic memory associated with this name.
 *
 * Requires:
 *
 *\li	'name' is a valid name.
 *
 * Returns:
 *
 *\li	'true' if the name is dynamic otherwise 'false'.
 */

isc_result_t
dns_name_print(const dns_name_t *name, FILE *stream);
/*%<
 * Print 'name' on 'stream'.
 *
 * Requires:
 *
 *\li	'name' is a valid name.
 *
 *\li	'stream' is a valid stream.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS
 *
 *\li	Any error that dns_name_totext() can return.
 */

void
dns_name_format(const dns_name_t *name, char *cp, unsigned int size);
/*%<
 * Format 'name' as text appropriate for use in log messages.
 *
 * Store the formatted name at 'cp', writing no more than
 * 'size' bytes.  The resulting string is guaranteed to be
 * null terminated.
 *
 * The formatted name will have a terminating dot only if it is
 * the root.
 *
 * This function cannot fail, instead any errors are indicated
 * in the returned text.
 *
 * Requires:
 *
 *\li	'name' is a valid name.
 *
 *\li	'cp' points a valid character array of size 'size'.
 *
 *\li	'size' > 0.
 *
 */

isc_result_t
dns_name_tostring(const dns_name_t *source, char **target, isc_mem_t *mctx);
/*%<
 * Convert 'name' to string format, allocating sufficient memory to
 * hold it (free with isc_mem_free()).
 *
 * Differs from dns_name_format in that it allocates its own memory.
 *
 * Requires:
 *
 *\li	'name' is a valid name.
 *\li	'target' is not NULL.
 *\li	'*target' is NULL.
 *
 * Returns:
 *
 *\li	ISC_R_SUCCESS
 *\li	ISC_R_NOMEMORY
 *
 *\li	Any error that dns_name_totext() can return.
 */

isc_result_t
dns_name_fromstring(dns_name_t *target, const char *src,
		    const dns_name_t *origin, unsigned int options,
		    isc_mem_t *mctx);
/*%<
 * Convert a string to a name and place it in target, allocating memory
 * as necessary.  'options' has the same semantics as that of
 * dns_name_fromtext().
 *
 * If 'target' has a buffer then the name will be copied into it rather than
 * memory being allocated.
 *
 * Requires:
 *
 * \li	'target' is a valid name that is not read-only.
 * \li	'src' is not NULL.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS
 *
 *\li	Any error that dns_name_fromtext() can return.
 *
 *\li	Any error that dns_name_dup() can return.
 */

isc_result_t
dns_name_settotextfilter(dns_name_totextfilter_t *proc);
/*%<
 * Set / clear a thread specific function 'proc' to be called at the
 * end of dns_name_totext().
 *
 * Note: It's a good practice to call "dns_name_settotextfilter(NULL);"
 * prior to exiting the thread.
 *
 * Returns
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_UNEXPECTED
 */

#define DNS_NAME_FORMATSIZE (DNS_NAME_MAXTEXT + 1)
/*%<
 * Suggested size of buffer passed to dns_name_format().
 * Includes space for the terminating NULL.
 */

void
dns_name_copy(const dns_name_t *source, dns_name_t *dest);
/*%<
 * Copies the name in 'source' into 'dest'.  The name data is copied to
 * the dedicated buffer for 'dest'. (If copying to a name that doesn't
 * have a dedicated buffer, use dns_name_setbuffer() first.)
 *
 * Requires:
 * \li	'source' is a valid name.
 *
 * \li	'dest' is an initialized name with a dedicated buffer.
 */

bool
dns_name_ishostname(const dns_name_t *name, bool wildcard);
/*%<
 * Return if 'name' is a valid hostname.  RFC 952 / RFC 1123.
 * If 'wildcard' is true then allow the first label of name to
 * be a wildcard.
 * The root is also accepted.
 *
 * Requires:
 *	'name' to be valid.
 */

bool
dns_name_ismailbox(const dns_name_t *name);
/*%<
 * Return if 'name' is a valid mailbox.  RFC 821.
 *
 * Requires:
 * \li	'name' to be valid.
 */

bool
dns_name_internalwildcard(const dns_name_t *name);
/*%<
 * Return if 'name' contains a internal wildcard name.
 *
 * Requires:
 * \li	'name' to be valid.
 */

bool
dns_name_isdnssd(const dns_name_t *owner);
/*%<
 * Determine if the 'owner' is a DNS-SD prefix.
 */

bool
dns_name_isrfc1918(const dns_name_t *owner);
/*%<
 * Determine if the 'name' is in the RFC 1918 reverse namespace.
 */

bool
dns_name_isula(const dns_name_t *owner);
/*%<
 * Determine if the 'name' is in the ULA reverse namespace.
 */

bool
dns_name_istat(const dns_name_t *name);
/*%<
 * Determine if 'name' is a potential 'trust-anchor-telemetry' name.
 */

bool
dns_name_isdnssvcb(const dns_name_t *name);
/*%<
 * Determine if 'name' is a dns service name,
 * i.e. it starts with and optional _port label followed by a _dns label.
 */

size_t
dns_name_size(const dns_name_t *name);
/*%<
 * Return the amount of dynamically allocated memory associated with
 * 'name' (which is 0 if 'name' is not dynamic).
 *
 * Requires:
 * \li	'name' to be valid.
 */

ISC_LANG_ENDDECLS
