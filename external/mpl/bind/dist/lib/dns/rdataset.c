/*	$NetBSD: rdataset.c,v 1.11 2025/07/17 19:01:45 christos Exp $	*/

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
#include <stdbool.h>
#include <stdlib.h>

#include <isc/buffer.h>
#include <isc/mem.h>
#include <isc/random.h>
#include <isc/serial.h>
#include <isc/util.h>

#include <dns/compress.h>
#include <dns/fixedname.h>
#include <dns/name.h>
#include <dns/ncache.h>
#include <dns/rdata.h>
#include <dns/rdataset.h>

static const char *trustnames[] = {
	"none",		  "pending-additional",
	"pending-answer", "additional",
	"glue",		  "answer",
	"authauthority",  "authanswer",
	"secure",	  "local" /* aka ultimate */
};

const char *
dns_trust_totext(dns_trust_t trust) {
	if (trust >= sizeof(trustnames) / sizeof(*trustnames)) {
		return "bad";
	}
	return trustnames[trust];
}

void
dns_rdataset_init(dns_rdataset_t *rdataset) {
	/*
	 * Make 'rdataset' a valid, disassociated rdataset.
	 */

	REQUIRE(rdataset != NULL);

	*rdataset = (dns_rdataset_t){
		.magic = DNS_RDATASET_MAGIC,
		.link = ISC_LINK_INITIALIZER,
		.count = DNS_RDATASET_COUNT_UNDEFINED,
	};
}

void
dns_rdataset_invalidate(dns_rdataset_t *rdataset) {
	/*
	 * Invalidate 'rdataset'.
	 */

	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE(rdataset->methods == NULL);

	*rdataset = (dns_rdataset_t){
		.magic = 0,
		.link = ISC_LINK_INITIALIZER,
		.count = DNS_RDATASET_COUNT_UNDEFINED,
	};
}

void
dns__rdataset_disassociate(dns_rdataset_t *rdataset DNS__DB_FLARG) {
	/*
	 * Disassociate 'rdataset' from its rdata, allowing it to be reused.
	 */

	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE(rdataset->methods != NULL);

	if (rdataset->methods->disassociate != NULL) {
		(rdataset->methods->disassociate)(rdataset DNS__DB_FLARG_PASS);
	}
	*rdataset = (dns_rdataset_t){
		.magic = DNS_RDATASET_MAGIC,
		.link = ISC_LINK_INITIALIZER,
		.count = DNS_RDATASET_COUNT_UNDEFINED,
	};
}

bool
dns_rdataset_isassociated(dns_rdataset_t *rdataset) {
	/*
	 * Is 'rdataset' associated?
	 */

	REQUIRE(DNS_RDATASET_VALID(rdataset));

	if (rdataset->methods != NULL) {
		return true;
	}

	return false;
}

static isc_result_t
question_cursor(dns_rdataset_t *rdataset ISC_ATTR_UNUSED) {
	return ISC_R_NOMORE;
}

static void
question_clone(dns_rdataset_t *source, dns_rdataset_t *target DNS__DB_FLARG) {
	*target = *source;
}

static dns_rdatasetmethods_t question_methods = {
	.first = question_cursor,
	.next = question_cursor,
	.clone = question_clone,
};

void
dns_rdataset_makequestion(dns_rdataset_t *rdataset, dns_rdataclass_t rdclass,
			  dns_rdatatype_t type) {
	/*
	 * Make 'rdataset' a valid, associated, question rdataset, with a
	 * question class of 'rdclass' and type 'type'.
	 */

	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE(rdataset->methods == NULL);

	rdataset->methods = &question_methods;
	rdataset->rdclass = rdclass;
	rdataset->type = type;
	rdataset->attributes |= DNS_RDATASETATTR_QUESTION;
}

unsigned int
dns_rdataset_count(dns_rdataset_t *rdataset) {
	/*
	 * Return the number of records in 'rdataset'.
	 */

	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE(rdataset->methods != NULL);
	REQUIRE(rdataset->methods->count != NULL);

	return (rdataset->methods->count)(rdataset);
}

void
dns__rdataset_clone(dns_rdataset_t *source,
		    dns_rdataset_t *target DNS__DB_FLARG) {
	/*
	 * Make 'target' refer to the same rdataset as 'source'.
	 */

	REQUIRE(DNS_RDATASET_VALID(source));
	REQUIRE(source->methods != NULL);
	REQUIRE(DNS_RDATASET_VALID(target));
	REQUIRE(target->methods == NULL);

	(source->methods->clone)(source, target DNS__DB_FLARG_PASS);
}

isc_result_t
dns_rdataset_first(dns_rdataset_t *rdataset) {
	/*
	 * Move the rdata cursor to the first rdata in the rdataset (if any).
	 */

	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE(rdataset->methods != NULL);
	REQUIRE(rdataset->methods->first != NULL);

	return (rdataset->methods->first)(rdataset);
}

isc_result_t
dns_rdataset_next(dns_rdataset_t *rdataset) {
	/*
	 * Move the rdata cursor to the next rdata in the rdataset (if any).
	 */

	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE(rdataset->methods != NULL);
	REQUIRE(rdataset->methods->next != NULL);

	return (rdataset->methods->next)(rdataset);
}

void
dns_rdataset_current(dns_rdataset_t *rdataset, dns_rdata_t *rdata) {
	/*
	 * Make 'rdata' refer to the current rdata.
	 */

	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE(rdataset->methods != NULL);
	REQUIRE(rdataset->methods->current != NULL);

	(rdataset->methods->current)(rdataset, rdata);
}

#define MAX_SHUFFLE    32
#define WANT_FIXED(r)  (((r)->attributes & DNS_RDATASETATTR_FIXEDORDER) != 0)
#define WANT_RANDOM(r) (((r)->attributes & DNS_RDATASETATTR_RANDOMIZE) != 0)
#define WANT_CYCLIC(r) (((r)->attributes & DNS_RDATASETATTR_CYCLIC) != 0)

struct towire_sort {
	int key;
	dns_rdata_t *rdata;
};

static int
towire_compare(const void *av, const void *bv) {
	const struct towire_sort *a = (const struct towire_sort *)av;
	const struct towire_sort *b = (const struct towire_sort *)bv;
	return a->key - b->key;
}

static void
swap_rdata(dns_rdata_t *in, unsigned int a, unsigned int b) {
	dns_rdata_t rdata = in[a];
	in[a] = in[b];
	in[b] = rdata;
}

static isc_result_t
towiresorted(dns_rdataset_t *rdataset, const dns_name_t *owner_name,
	     dns_compress_t *cctx, isc_buffer_t *target,
	     dns_rdatasetorderfunc_t order, const void *order_arg, bool partial,
	     unsigned int options, unsigned int *countp,
	     void **state ISC_ATTR_UNUSED) {
	isc_region_t r;
	isc_result_t result;
	unsigned int i, count = 0, added;
	isc_buffer_t savedbuffer, rdlen, rrbuffer;
	unsigned int headlen;
	bool question = false;
	bool shuffle = false, sort = false;
	bool want_random, want_cyclic;
	dns_rdata_t in_fixed[MAX_SHUFFLE];
	dns_rdata_t *in = in_fixed;
	struct towire_sort out_fixed[MAX_SHUFFLE];
	struct towire_sort *out = out_fixed;
	dns_fixedname_t fixed;
	dns_name_t *name = NULL;
	uint16_t offset;

	/*
	 * Convert 'rdataset' to wire format, compressing names as specified
	 * in cctx, and storing the result in 'target'.
	 */

	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE(rdataset->methods != NULL);
	REQUIRE(countp != NULL);
	REQUIRE(cctx != NULL && cctx->mctx != NULL);

	want_random = WANT_RANDOM(rdataset);
	want_cyclic = WANT_CYCLIC(rdataset);

	if ((rdataset->attributes & DNS_RDATASETATTR_QUESTION) != 0) {
		question = true;
		count = 1;
		result = dns_rdataset_first(rdataset);
		INSIST(result == ISC_R_NOMORE);
	} else if ((rdataset->attributes & DNS_RDATASETATTR_NEGATIVE) != 0) {
		/*
		 * This is a negative caching rdataset.
		 */
		unsigned int ncache_opts = 0;
		if ((options & DNS_RDATASETTOWIRE_OMITDNSSEC) != 0) {
			ncache_opts |= DNS_NCACHETOWIRE_OMITDNSSEC;
		}
		return dns_ncache_towire(rdataset, cctx, target, ncache_opts,
					 countp);
	} else {
		count = dns_rdataset_count(rdataset);
		result = dns_rdataset_first(rdataset);
		if (result == ISC_R_NOMORE) {
			return ISC_R_SUCCESS;
		}
		if (result != ISC_R_SUCCESS) {
			return result;
		}
	}

	/*
	 * Do we want to sort and/or shuffle this answer?
	 */
	if (!question && count > 1 && rdataset->type != dns_rdatatype_rrsig) {
		if (order != NULL) {
			sort = true;
		}
		if (want_random || want_cyclic) {
			shuffle = true;
		}
	}

	if (shuffle || sort) {
		if (count > MAX_SHUFFLE) {
			in = isc_mem_cget(cctx->mctx, count, sizeof(*in));
			out = isc_mem_cget(cctx->mctx, count, sizeof(*out));
			if (in == NULL || out == NULL) {
				shuffle = sort = false;
			}
		}
	}

	if (shuffle || sort) {
		uint32_t seed = 0;
		unsigned int j = 0;

		/*
		 * First we get handles to all of the rdata.
		 */
		i = 0;
		do {
			INSIST(i < count);
			dns_rdata_init(&in[i]);
			dns_rdataset_current(rdataset, &in[i]);
			i++;
			result = dns_rdataset_next(rdataset);
		} while (result == ISC_R_SUCCESS);
		if (result != ISC_R_NOMORE) {
			goto cleanup;
		}
		INSIST(i == count);

		if (want_random) {
			seed = isc_random32();
		}

		if (want_cyclic &&
		    (rdataset->count != DNS_RDATASET_COUNT_UNDEFINED))
		{
			j = rdataset->count % count;
		}

		for (i = 0; i < count; i++) {
			if (want_random) {
				swap_rdata(in, j, j + seed % (count - j));
			}

			out[i].key = (sort) ? (*order)(&in[j], order_arg) : 0;
			out[i].rdata = &in[j];
			if (++j == count) {
				j = 0;
			}
		}
		/*
		 * Sortlist order.
		 */
		if (sort) {
			qsort(out, count, sizeof(out[0]), towire_compare);
		}
	}

	savedbuffer = *target;
	i = 0;
	added = 0;

	name = dns_fixedname_initname(&fixed);
	dns_name_copy(owner_name, name);
	dns_rdataset_getownercase(rdataset, name);
	offset = 0xffff;

	name->attributes.nocompress |= owner_name->attributes.nocompress;

	do {
		/*
		 * Copy out the name, type, class, ttl.
		 */

		rrbuffer = *target;
		dns_compress_setpermitted(cctx, true);
		result = dns_name_towire(name, cctx, target, &offset);
		if (result != ISC_R_SUCCESS) {
			goto rollback;
		}
		headlen = sizeof(dns_rdataclass_t) + sizeof(dns_rdatatype_t);
		if (!question) {
			headlen += sizeof(dns_ttl_t) + 2;
		} /* XXX 2 for rdata len
		   */
		isc_buffer_availableregion(target, &r);
		if (r.length < headlen) {
			result = ISC_R_NOSPACE;
			goto rollback;
		}
		isc_buffer_putuint16(target, rdataset->type);
		isc_buffer_putuint16(target, rdataset->rdclass);
		if (!question) {
			dns_rdata_t rdata = DNS_RDATA_INIT;

			isc_buffer_putuint32(target, rdataset->ttl);

			/*
			 * Save space for rdlen.
			 */
			rdlen = *target;
			isc_buffer_add(target, 2);

			/*
			 * Copy out the rdata
			 */
			if (shuffle || sort) {
				rdata = *(out[i].rdata);
			} else {
				dns_rdata_reset(&rdata);
				dns_rdataset_current(rdataset, &rdata);
			}
			result = dns_rdata_towire(&rdata, cctx, target);
			if (result != ISC_R_SUCCESS) {
				goto rollback;
			}
			INSIST((target->used >= rdlen.used + 2) &&
			       (target->used - rdlen.used - 2 < 65536));
			isc_buffer_putuint16(
				&rdlen,
				(uint16_t)(target->used - rdlen.used - 2));
			added++;
		}

		if (shuffle || sort) {
			i++;
			if (i == count) {
				result = ISC_R_NOMORE;
			} else {
				result = ISC_R_SUCCESS;
			}
		} else {
			result = dns_rdataset_next(rdataset);
		}
	} while (result == ISC_R_SUCCESS);

	if (result != ISC_R_NOMORE) {
		goto rollback;
	}

	*countp += count;

	result = ISC_R_SUCCESS;
	goto cleanup;

rollback:
	if (partial && result == ISC_R_NOSPACE) {
		dns_compress_rollback(cctx, rrbuffer.used);
		*countp += added;
		*target = rrbuffer;
		goto cleanup;
	}
	dns_compress_rollback(cctx, savedbuffer.used);
	*countp = 0;
	*target = savedbuffer;

cleanup:
	if (out != NULL && out != out_fixed) {
		isc_mem_cput(cctx->mctx, out, count, sizeof(*out));
	}
	if (in != NULL && in != in_fixed) {
		isc_mem_cput(cctx->mctx, in, count, sizeof(*in));
	}
	return result;
}

isc_result_t
dns_rdataset_towiresorted(dns_rdataset_t *rdataset,
			  const dns_name_t *owner_name, dns_compress_t *cctx,
			  isc_buffer_t *target, dns_rdatasetorderfunc_t order,
			  const void *order_arg, unsigned int options,
			  unsigned int *countp) {
	return towiresorted(rdataset, owner_name, cctx, target, order,
			    order_arg, false, options, countp, NULL);
}

isc_result_t
dns_rdataset_towirepartial(dns_rdataset_t *rdataset,
			   const dns_name_t *owner_name, dns_compress_t *cctx,
			   isc_buffer_t *target, dns_rdatasetorderfunc_t order,
			   const void *order_arg, unsigned int options,
			   unsigned int *countp, void **state) {
	REQUIRE(state == NULL); /* XXX remove when implemented */
	return towiresorted(rdataset, owner_name, cctx, target, order,
			    order_arg, true, options, countp, state);
}

isc_result_t
dns_rdataset_towire(dns_rdataset_t *rdataset, const dns_name_t *owner_name,
		    dns_compress_t *cctx, isc_buffer_t *target,
		    unsigned int options, unsigned int *countp) {
	return towiresorted(rdataset, owner_name, cctx, target, NULL, NULL,
			    false, options, countp, NULL);
}

isc_result_t
dns_rdataset_additionaldata(dns_rdataset_t *rdataset,
			    const dns_name_t *owner_name,
			    dns_additionaldatafunc_t add, void *arg,
			    size_t limit) {
	dns_rdata_t rdata = DNS_RDATA_INIT;
	isc_result_t result;

	/*
	 * For each rdata in rdataset, call 'add' for each name and type in the
	 * rdata which is subject to additional section processing.
	 */

	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE((rdataset->attributes & DNS_RDATASETATTR_QUESTION) == 0);

	if (limit != 0 && dns_rdataset_count(rdataset) > limit) {
		return DNS_R_TOOMANYRECORDS;
	}

	result = dns_rdataset_first(rdataset);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	do {
		dns_rdataset_current(rdataset, &rdata);
		result = dns_rdata_additionaldata(&rdata, owner_name, add, arg);
		if (result == ISC_R_SUCCESS) {
			result = dns_rdataset_next(rdataset);
		}
		dns_rdata_reset(&rdata);
	} while (result == ISC_R_SUCCESS);

	if (result != ISC_R_NOMORE) {
		return result;
	}

	return ISC_R_SUCCESS;
}

isc_result_t
dns_rdataset_addnoqname(dns_rdataset_t *rdataset, dns_name_t *name) {
	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE(rdataset->methods != NULL);
	if (rdataset->methods->addnoqname == NULL) {
		return ISC_R_NOTIMPLEMENTED;
	}
	return (rdataset->methods->addnoqname)(rdataset, name);
}

isc_result_t
dns__rdataset_getnoqname(dns_rdataset_t *rdataset, dns_name_t *name,
			 dns_rdataset_t *neg,
			 dns_rdataset_t *negsig DNS__DB_FLARG) {
	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE(rdataset->methods != NULL);

	if (rdataset->methods->getnoqname == NULL) {
		return ISC_R_NOTIMPLEMENTED;
	}
	return (rdataset->methods->getnoqname)(rdataset, name, neg,
					       negsig DNS__DB_FLARG_PASS);
}

isc_result_t
dns_rdataset_addclosest(dns_rdataset_t *rdataset, const dns_name_t *name) {
	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE(rdataset->methods != NULL);
	if (rdataset->methods->addclosest == NULL) {
		return ISC_R_NOTIMPLEMENTED;
	}
	return (rdataset->methods->addclosest)(rdataset, name);
}

isc_result_t
dns__rdataset_getclosest(dns_rdataset_t *rdataset, dns_name_t *name,
			 dns_rdataset_t *neg,
			 dns_rdataset_t *negsig DNS__DB_FLARG) {
	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE(rdataset->methods != NULL);

	if (rdataset->methods->getclosest == NULL) {
		return ISC_R_NOTIMPLEMENTED;
	}
	return (rdataset->methods->getclosest)(rdataset, name, neg,
					       negsig DNS__DB_FLARG_PASS);
}

void
dns_rdataset_settrust(dns_rdataset_t *rdataset, dns_trust_t trust) {
	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE(rdataset->methods != NULL);

	if (rdataset->methods->settrust != NULL) {
		(rdataset->methods->settrust)(rdataset, trust);
	} else {
		rdataset->trust = trust;
	}
}

void
dns__rdataset_expire(dns_rdataset_t *rdataset DNS__DB_FLARG) {
	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE(rdataset->methods != NULL);

	if (rdataset->methods->expire != NULL) {
		(rdataset->methods->expire)(rdataset DNS__DB_FLARG_PASS);
	}
}

void
dns_rdataset_clearprefetch(dns_rdataset_t *rdataset) {
	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE(rdataset->methods != NULL);

	if (rdataset->methods->clearprefetch != NULL) {
		(rdataset->methods->clearprefetch)(rdataset);
	}
}

void
dns_rdataset_setownercase(dns_rdataset_t *rdataset, const dns_name_t *name) {
	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE(rdataset->methods != NULL);

	if (rdataset->methods->setownercase != NULL &&
	    (rdataset->attributes & DNS_RDATASETATTR_KEEPCASE) == 0)
	{
		(rdataset->methods->setownercase)(rdataset, name);
	}
}

void
dns_rdataset_getownercase(const dns_rdataset_t *rdataset, dns_name_t *name) {
	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE(rdataset->methods != NULL);

	if (rdataset->methods->getownercase != NULL &&
	    (rdataset->attributes & DNS_RDATASETATTR_KEEPCASE) == 0)
	{
		(rdataset->methods->getownercase)(rdataset, name);
	}
}

void
dns_rdataset_trimttl(dns_rdataset_t *rdataset, dns_rdataset_t *sigrdataset,
		     dns_rdata_rrsig_t *rrsig, isc_stdtime_t now,
		     bool acceptexpired) {
	uint32_t ttl = 0;

	REQUIRE(DNS_RDATASET_VALID(rdataset));
	REQUIRE(DNS_RDATASET_VALID(sigrdataset));
	REQUIRE(rrsig != NULL);

	/*
	 * If we accept expired RRsets keep them for no more than 120 seconds.
	 */
	if (acceptexpired &&
	    (isc_serial_le(rrsig->timeexpire, (now + 120) & 0xffffffff) ||
	     isc_serial_le(rrsig->timeexpire, now)))
	{
		ttl = 120;
	} else if (isc_serial_ge(rrsig->timeexpire, now)) {
		ttl = rrsig->timeexpire - now;
	}

	ttl = ISC_MIN(ISC_MIN(rdataset->ttl, sigrdataset->ttl),
		      ISC_MIN(rrsig->originalttl, ttl));
	rdataset->ttl = ttl;
	sigrdataset->ttl = ttl;
}

bool
dns_rdataset_equals(const dns_rdataset_t *rdataset1,
		    const dns_rdataset_t *rdataset2) {
	REQUIRE(DNS_RDATASET_VALID(rdataset1));
	REQUIRE(DNS_RDATASET_VALID(rdataset2));

	if (rdataset1->methods->equals != NULL &&
	    rdataset1->methods->equals == rdataset2->methods->equals)
	{
		return (rdataset1->methods->equals)(rdataset1, rdataset2);
	}

	return false;
}
