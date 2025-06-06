/*	$NetBSD: prop_data.c,v 1.23 2025/05/14 03:25:45 thorpej Exp $	*/

/*-
 * Copyright (c) 2006, 2020, 2025 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "prop_object_impl.h"
#include <prop/prop_data.h>

#if defined(_KERNEL)
#include <sys/systm.h>
#elif defined(_STANDALONE)
#include <sys/param.h>
#include <lib/libkern/libkern.h>
#else
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#endif

struct _prop_data {
	struct _prop_object	pd_obj;
	union {
		void *		pdu_mutable;
		const void *	pdu_immutable;
	} pd_un;
#define	pd_mutable		pd_un.pdu_mutable
#define	pd_immutable		pd_un.pdu_immutable
	size_t			pd_size;
	int			pd_flags;
};

#define	PD_F_NOCOPY		0x01
#define	PD_F_MUTABLE		0x02

_PROP_POOL_INIT(_prop_data_pool, sizeof(struct _prop_data), "propdata")
_PROP_MALLOC_DEFINE(M_PROP_DATA, "prop data",
		    "property data container object")

static const struct _prop_object_type_tags _prop_data_type_tags = {
	.xml_tag		=	"data",
};

static _prop_object_free_rv_t
		_prop_data_free(prop_stack_t, prop_object_t *);
static bool	_prop_data_externalize(
				struct _prop_object_externalize_context *,
				void *);
static _prop_object_equals_rv_t
		_prop_data_equals(prop_object_t, prop_object_t,
				  void **, void **,
				  prop_object_t *, prop_object_t *);

static const struct _prop_object_type _prop_object_type_data = {
	.pot_type	=	PROP_TYPE_DATA,
	.pot_free	=	_prop_data_free,
	.pot_extern	=	_prop_data_externalize,
	.pot_equals	=	_prop_data_equals,
};

#define	prop_object_is_data(x)		\
	((x) != NULL && (x)->pd_obj.po_type == &_prop_object_type_data)

/* ARGSUSED */
static _prop_object_free_rv_t
_prop_data_free(prop_stack_t stack, prop_object_t *obj)
{
	prop_data_t pd = *obj;

	if ((pd->pd_flags & PD_F_NOCOPY) == 0 && pd->pd_mutable != NULL)
	    	_PROP_FREE(pd->pd_mutable, M_PROP_DATA);
	_PROP_POOL_PUT(_prop_data_pool, pd);

	return (_PROP_OBJECT_FREE_DONE);
}

static const char _prop_data_base64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char _prop_data_pad64 = '=';

static bool
_prop_data_externalize(struct _prop_object_externalize_context *ctx, void *v)
{
	prop_data_t pd = v;
	size_t i, srclen;
	const uint8_t *src;
	uint8_t output[4];
	uint8_t input[3];

	_PROP_ASSERT(ctx->poec_format == PROP_FORMAT_XML ||
		     ctx->poec_format == PROP_FORMAT_JSON);

	/*
	 * JSON does not have a syntax for serialized binary data.
	 */
	if (ctx->poec_format == PROP_FORMAT_JSON) {
		return false;
	}

	if (pd->pd_size == 0)
		return (_prop_extern_append_empty_tag(ctx,
		    &_prop_data_type_tags));

	if (_prop_extern_append_start_tag(ctx,
				&_prop_data_type_tags, NULL) == false)
		return (false);

	for (src = pd->pd_immutable, srclen = pd->pd_size;
	     srclen > 2; srclen -= 3) {
		input[0] = *src++;
		input[1] = *src++;
		input[2] = *src++;

		output[0] = (uint32_t)input[0] >> 2;
		output[1] = ((uint32_t)(input[0] & 0x03) << 4) +
		    ((uint32_t)input[1] >> 4);
		output[2] = ((uint32_t)(input[1] & 0x0f) << 2) +
		    ((uint32_t)input[2] >> 6);
		output[3] = input[2] & 0x3f;
		_PROP_ASSERT(output[0] < 64);
		_PROP_ASSERT(output[1] < 64);
		_PROP_ASSERT(output[2] < 64);
		_PROP_ASSERT(output[3] < 64);

		if (_prop_extern_append_char(ctx,
				_prop_data_base64[output[0]]) == false ||
		    _prop_extern_append_char(ctx,
		    		_prop_data_base64[output[1]]) == false ||
		    _prop_extern_append_char(ctx,
		    		_prop_data_base64[output[2]]) == false ||
		    _prop_extern_append_char(ctx,
		    		_prop_data_base64[output[3]]) == false)
			return (false);
	}

	if (srclen != 0) {
		input[0] = input[1] = input[2] = '\0';
		for (i = 0; i < srclen; i++)
			input[i] = *src++;

		output[0] = (uint32_t)input[0] >> 2;
		output[1] = ((uint32_t)(input[0] & 0x03) << 4) +
		    ((uint32_t)input[1] >> 4);
		output[2] = ((uint32_t)(input[1] & 0x0f) << 2) +
		    ((uint32_t)input[2] >> 6);
		_PROP_ASSERT(output[0] < 64);
		_PROP_ASSERT(output[1] < 64);
		_PROP_ASSERT(output[2] < 64);

		if (_prop_extern_append_char(ctx,
				_prop_data_base64[output[0]]) == false ||
		    _prop_extern_append_char(ctx,
		    		_prop_data_base64[output[1]]) == false ||
		    _prop_extern_append_char(ctx,
		    		srclen == 1 ? _prop_data_pad64
				: _prop_data_base64[output[2]]) == false ||
		    _prop_extern_append_char(ctx,
		    		_prop_data_pad64) == false)
			return (false);
	}

	if (_prop_extern_append_end_tag(ctx,
					&_prop_data_type_tags) == false)
		return (false);

	return (true);
}

/* ARGSUSED */
static _prop_object_equals_rv_t
_prop_data_equals(prop_object_t v1, prop_object_t v2,
    void **stored_pointer1, void **stored_pointer2,
    prop_object_t *next_obj1, prop_object_t *next_obj2)
{
	prop_data_t pd1 = v1;
	prop_data_t pd2 = v2;

	if (pd1 == pd2)
		return (_PROP_OBJECT_EQUALS_TRUE);
	if (pd1->pd_size != pd2->pd_size)
		return (_PROP_OBJECT_EQUALS_FALSE);
	if (pd1->pd_size == 0) {
		_PROP_ASSERT(pd1->pd_immutable == NULL);
		_PROP_ASSERT(pd2->pd_immutable == NULL);
		return (_PROP_OBJECT_EQUALS_TRUE);
	}
	if (memcmp(pd1->pd_immutable, pd2->pd_immutable, pd1->pd_size) == 0)
		return _PROP_OBJECT_EQUALS_TRUE;
	else
		return _PROP_OBJECT_EQUALS_FALSE;
}

static prop_data_t
_prop_data_alloc(int const flags)
{
	prop_data_t pd;

	pd = _PROP_POOL_GET(_prop_data_pool);
	if (pd != NULL) {
		_prop_object_init(&pd->pd_obj, &_prop_object_type_data);

		pd->pd_mutable = NULL;
		pd->pd_size = 0;
		pd->pd_flags = flags;
	}

	return (pd);
}

static prop_data_t
_prop_data_instantiate(int const flags, const void * const data,
    size_t const len)
{
	prop_data_t pd;

	pd = _prop_data_alloc(flags);
	if (pd != NULL) {
		pd->pd_immutable = data;
		pd->pd_size = len;
	}

	return (pd);
}

_PROP_DEPRECATED(prop_data_create_data,
    "this program uses prop_data_create_data(); all functions "
    "supporting mutable prop_data objects are deprecated.")
_PROP_EXPORT prop_data_t
prop_data_create_data(const void *v, size_t size)
{
	prop_data_t pd;
	void *nv;

	pd = _prop_data_alloc(PD_F_MUTABLE);
	if (pd != NULL && size != 0) {
		nv = _PROP_MALLOC(size, M_PROP_DATA);
		if (nv == NULL) {
			prop_object_release(pd);
			return (NULL);
		}
		memcpy(nv, v, size);
		pd->pd_mutable = nv;
		pd->pd_size = size;
	}
	return (pd);
}

_PROP_DEPRECATED(prop_data_create_data_nocopy,
    "this program uses prop_data_create_data_nocopy(), "
    "which is deprecated; use prop_data_create_nocopy() instead.")
_PROP_EXPORT prop_data_t
prop_data_create_data_nocopy(const void *v, size_t size)
{
	return prop_data_create_nocopy(v, size);
}

/*
 * prop_data_create_copy --
 *	Create a data object with a copy of the provided data.
 */
_PROP_EXPORT prop_data_t
prop_data_create_copy(const void *v, size_t size)
{
	prop_data_t pd;
	void *nv;

	/* Tolerate the creation of empty data objects. */
	if (v != NULL && size != 0) {
		nv = _PROP_MALLOC(size, M_PROP_DATA);
		if (nv == NULL)
			return (NULL);

		memcpy(nv, v, size);
	} else {
		nv = NULL;
		size = 0;
	}

	pd = _prop_data_instantiate(0, nv, size);
	if (pd == NULL && nv == NULL)
		_PROP_FREE(nv, M_PROP_DATA);

	return (pd);
}

/*
 * prop_data_create_nocopy --
 *	Create a data object using the provided external data reference.
 */
_PROP_EXPORT prop_data_t
prop_data_create_nocopy(const void *v, size_t size)
{

	/* Tolerate the creation of empty data objects. */
	if (v == NULL || size == 0) {
		v = NULL;
		size = 0;
	}

	return _prop_data_instantiate(PD_F_NOCOPY, v, size);
}

/*
 * prop_data_copy --
 *	Copy a data container.  If the original data is external, then
 *	the copy is also references the same external data.
 */
_PROP_EXPORT prop_data_t
prop_data_copy(prop_data_t opd)
{
	prop_data_t pd;

	if (! prop_object_is_data(opd))
		return (NULL);

	if ((opd->pd_flags & PD_F_NOCOPY) != 0 ||
	    (opd->pd_flags & PD_F_MUTABLE) == 0) {
		/* Just retain and return the original. */
		prop_object_retain(opd);
		return (opd);
	}

	pd = prop_data_create_copy(opd->pd_immutable, opd->pd_size);
	if (pd != NULL) {
		/* Preserve deprecated mutability semantics. */
		pd->pd_flags |= PD_F_MUTABLE;
	}

	return (pd);
}

/*
 * prop_data_size --
 *	Return the size of the data.
 */
_PROP_EXPORT size_t
prop_data_size(prop_data_t pd)
{

	if (! prop_object_is_data(pd))
		return (0);

	return (pd->pd_size);
}

/*
 * prop_data_value --
 *	Returns a pointer to the data object's value.  This pointer
 *	remains valid only as long as the data object.
 */
_PROP_EXPORT const void *
prop_data_value(prop_data_t pd)
{

	if (! prop_object_is_data(pd))
		return (0);

	return (pd->pd_immutable);
}

/*
 * prop_data_copy_value --
 *	Copy the data object's value into the supplied buffer.
 */
_PROP_EXPORT bool
prop_data_copy_value(prop_data_t pd, void *buf, size_t buflen)
{

	if (! prop_object_is_data(pd))
		return (false);

	if (buf == NULL || buflen < pd->pd_size)
		return (false);

	/* Tolerate empty data objects. */
	if (pd->pd_immutable == NULL || pd->pd_size == 0)
		return (false);

	memcpy(buf, pd->pd_immutable, pd->pd_size);

	return (true);
}

_PROP_DEPRECATED(prop_data_data,
    "this program uses prop_data_data(), "
    "which is deprecated; use prop_data_copy_value() instead.")
_PROP_EXPORT void *
prop_data_data(prop_data_t pd)
{
	void *v;

	if (! prop_object_is_data(pd))
		return (NULL);

	if (pd->pd_size == 0) {
		_PROP_ASSERT(pd->pd_immutable == NULL);
		return (NULL);
	}

	_PROP_ASSERT(pd->pd_immutable != NULL);

	v = _PROP_MALLOC(pd->pd_size, M_TEMP);
	if (v != NULL)
		memcpy(v, pd->pd_immutable, pd->pd_size);

	return (v);
}

_PROP_DEPRECATED(prop_data_data_nocopy,
    "this program uses prop_data_data_nocopy(), "
    "which is deprecated; use prop_data_value() instead.")
_PROP_EXPORT const void *
prop_data_data_nocopy(prop_data_t pd)
{
	return prop_data_value(pd);
}

/*
 * prop_data_equals --
 *	Return true if two data objects are equivalent.
 */
_PROP_EXPORT bool
prop_data_equals(prop_data_t pd1, prop_data_t pd2)
{
	if (!prop_object_is_data(pd1) || !prop_object_is_data(pd2))
		return (false);

	return (prop_object_equals(pd1, pd2));
}

/*
 * prop_data_equals_data --
 *	Return true if the contained data is equivalent to the specified
 *	external data.
 */
_PROP_EXPORT bool
prop_data_equals_data(prop_data_t pd, const void *v, size_t size)
{

	if (! prop_object_is_data(pd))
		return (false);

	if (pd->pd_size != size || v == NULL)
		return (false);

	return (memcmp(pd->pd_immutable, v, size) == 0);
}

static bool
_prop_data_internalize_decode(struct _prop_object_internalize_context *ctx,
			     uint8_t *target, size_t targsize, size_t *sizep,
			     const char **cpp)
{
	const char *src;
	size_t tarindex;
	int state, ch;
	const char *pos;

	state = 0;
	tarindex = 0;
	src = ctx->poic_cp;

	for (;;) {
		ch = (unsigned char) *src++;
		if (_PROP_EOF(ch))
			return (false);
		if (_PROP_ISSPACE(ch))
			continue;
		if (ch == '<') {
			src--;
			break;
		}
		if (ch == _prop_data_pad64)
			break;

		pos = strchr(_prop_data_base64, ch);
		if (pos == NULL)
			return (false);

		switch (state) {
		case 0:
			if (target) {
				if (tarindex >= targsize)
					return (false);
				target[tarindex] =
				    (uint8_t)((pos - _prop_data_base64) << 2);
			}
			state = 1;
			break;

		case 1:
			if (target) {
				if (tarindex + 1 >= targsize)
					return (false);
				target[tarindex] |=
				    (uint32_t)(pos - _prop_data_base64) >> 4;
				target[tarindex + 1] =
				    (uint8_t)(((pos - _prop_data_base64) & 0xf)
				        << 4);
			}
			tarindex++;
			state = 2;
			break;

		case 2:
			if (target) {
				if (tarindex + 1 >= targsize)
					return (false);
				target[tarindex] |=
				    (uint32_t)(pos - _prop_data_base64) >> 2;
				target[tarindex + 1] =
				    (uint8_t)(((pos - _prop_data_base64)
				        & 0x3) << 6);
			}
			tarindex++;
			state = 3;
			break;

		case 3:
			if (target) {
				if (tarindex >= targsize)
					return (false);
				target[tarindex] |= (uint8_t)
				    (pos - _prop_data_base64);
			}
			tarindex++;
			state = 0;
			break;

		default:
			_PROP_ASSERT(/*CONSTCOND*/0);
		}
	}

	/*
	 * We are done decoding the Base64 characters.  Let's see if we
	 * ended up on a byte boundary and/or with unrecognized trailing
	 * characters.
	 */
	if (ch == _prop_data_pad64) {
		ch = (unsigned char) *src;	/* src already advanced */
		if (_PROP_EOF(ch))
			return (false);
		switch (state) {
		case 0:		/* Invalid = in first position */
		case 1:		/* Invalid = in second position */
			return (false);

		case 2:		/* Valid, one byte of info */
			/* Skip whitespace */
			for (ch = (unsigned char) *src++;
			     ch != '<'; ch = (unsigned char) *src++) {
				if (_PROP_EOF(ch))
					return (false);
				if (!_PROP_ISSPACE(ch))
					break;
			}
			/* Make sure there is another trailing = */
			if (ch != _prop_data_pad64)
				return (false);
			ch = (unsigned char) *src;
			/* FALLTHROUGH */

		case 3:		/* Valid, two bytes of info */
			/*
			 * We know this char is a =.  Is there anything but
			 * whitespace after it?
			 */
			for (ch = (unsigned char) *src++;
			     ch != '<'; ch = (unsigned char) *src++) {
				if (_PROP_EOF(ch))
					return (false);
				if (!_PROP_ISSPACE(ch))
					return (false);
			}
			/* back up to '<' */
			src--;
		}
	} else {
		/*
		 * We ended by seeing the end of the Base64 string.  Make
		 * sure there are no partial bytes lying around.
		 */
		if (state != 0)
			return (false);
	}

	_PROP_ASSERT(*src == '<');
	if (sizep != NULL)
		*sizep = tarindex;
	if (cpp != NULL)
		*cpp = src;

	return (true);
}

/*
 * _prop_data_internalize --
 *	Parse a <data>...</data> and return the object created from the
 *	external representation.
 */

/* strtoul is used for parsing, enforce. */
typedef int PROP_DATA_ASSERT[/* CONSTCOND */sizeof(size_t) == sizeof(unsigned long) ? 1 : -1];

/* ARGSUSED */
bool
_prop_data_internalize(prop_stack_t stack, prop_object_t *obj,
    struct _prop_object_internalize_context *ctx)
{
	prop_data_t data;
	uint8_t *buf;
	size_t len, alen;

	/* No JSON binary data object representation. */
	if (ctx->poic_format == PROP_FORMAT_JSON) {
		return true;
	}

	/*
	 * We don't accept empty elements.
	 * This actually only checks for the node to be <data/>
	 * (Which actually causes another error if found.)
	 */
	if (ctx->poic_is_empty_element)
		return (true);

	/*
	 * If we got a "size" attribute, get the size of the data blob
	 * from that.  Otherwise, we have to figure it out from the base64.
	 */
	if (ctx->poic_tagattr != NULL) {
		char *cp;

		if (!_PROP_TAGATTR_MATCH(ctx, "size") ||
		    ctx->poic_tagattrval_len == 0)
			return (true);

#ifndef _KERNEL
		errno = 0;
#endif
		len = strtoul(ctx->poic_tagattrval, &cp, 0);
#ifndef _KERNEL		/* XXX can't check for ERANGE in the kernel */
		if (len == ULONG_MAX && errno == ERANGE)
			return (true);
#endif
		if (cp != ctx->poic_tagattrval + ctx->poic_tagattrval_len)
			return (true);
		_PROP_ASSERT(*cp == '\"');
	} else if (_prop_data_internalize_decode(ctx, NULL, 0, &len,
						NULL) == false)
		return (true);

	/*
	 * Always allocate one extra in case we don't land on an even byte
	 * boundary during the decode.
	 */
	buf = _PROP_MALLOC(len + 1, M_PROP_DATA);
	if (buf == NULL)
		return (true);

	if (_prop_data_internalize_decode(ctx, buf, len + 1, &alen,
					  &ctx->poic_cp) == false) {
		_PROP_FREE(buf, M_PROP_DATA);
		return (true);
	}
	if (alen != len) {
		_PROP_FREE(buf, M_PROP_DATA);
		return (true);
	}

	if (_prop_xml_intern_find_tag(ctx, "data",
				      _PROP_TAG_TYPE_END) == false) {
		_PROP_FREE(buf, M_PROP_DATA);
		return (true);
	}

	/*
	 * Handle alternate type of empty node.
	 * XML document could contain open/close tags, yet still be empty.
	 */
	if (alen == 0) {
		_PROP_FREE(buf, M_PROP_DATA);
		buf = NULL;
	}

	data = _prop_data_instantiate(0, buf, len);
	if (data == NULL && buf != NULL)
		_PROP_FREE(buf, M_PROP_DATA);

	*obj = data;
	return (true);
}
