/*	$NetBSD: lex.h,v 1.8 2025/01/26 16:25:41 christos Exp $	*/

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

/*! \file isc/lex.h
 * \brief The "lex" module provides a lightweight tokenizer.  It can operate
 * on files or buffers, and can handle "include".  It is designed for
 * parsing of DNS master files and the BIND configuration file, but
 * should be general enough to tokenize other things, e.g. HTTP.
 *
 * \li MP:
 *	No synchronization is provided.  Clients must ensure exclusive
 *	access.
 *
 * \li Reliability:
 *	No anticipated impact.
 *
 * \li Resources:
 *	TBS
 *
 * \li Security:
 *	No anticipated impact.
 *
 * \li Standards:
 * 	None.
 */

/***
 *** Imports
 ***/

#include <stdbool.h>
#include <stdio.h>

#include <isc/lang.h>
#include <isc/region.h>
#include <isc/types.h>

ISC_LANG_BEGINDECLS

/***
 *** Options
 ***/

/*@{*/
/*!
 * Various options for isc_lex_gettoken().
 */

#define ISC_LEXOPT_EOL	     0x0001 /*%< Want end-of-line token. */
#define ISC_LEXOPT_EOF	     0x0002 /*%< Want end-of-file token. */
#define ISC_LEXOPT_INITIALWS 0x0004 /*%< Want initial whitespace. */
#define ISC_LEXOPT_NUMBER    0x0008 /*%< Recognize numbers. */
#define ISC_LEXOPT_QSTRING   0x0010 /*%< Recognize qstrings. */
/*@}*/

/*@{*/
/*!
 * The ISC_LEXOPT_DNSMULTILINE option handles the processing of '(' and ')' in
 * the DNS master file format.  If this option is set, then the
 * ISC_LEXOPT_INITIALWS and ISC_LEXOPT_EOL options will be ignored when
 * the paren count is > 0.  To use this option, '(' and ')' must be special
 * characters.
 */
#define ISC_LEXOPT_DNSMULTILINE 0x0020 /*%< Handle '(' and ')'. */
#define ISC_LEXOPT_NOMORE	0x0040 /*%< Want "no more" token. */

#define ISC_LEXOPT_CNUMBER	    0x0080 /*%< Recognize octal and hex. */
#define ISC_LEXOPT_ESCAPE	    0x0100 /*%< Recognize escapes. */
#define ISC_LEXOPT_QSTRINGMULTILINE 0x0200 /*%< Allow multiline "" strings */
#define ISC_LEXOPT_OCTAL	    0x0400 /*%< Expect a octal number. */
#define ISC_LEXOPT_BTEXT	    0x0800 /*%< Bracketed text. */
#define ISC_LEXOPT_VPAIR	    0x1000 /*%< Recognize value pair. */
#define ISC_LEXOPT_QVPAIR	    0x2000 /*%< Recognize quoted value pair. */
/*@}*/
/*@{*/
/*!
 * Various commenting styles, which may be changed at any time with
 * isc_lex_setcomments().
 */

#define ISC_LEXCOMMENT_C	     0x01
#define ISC_LEXCOMMENT_CPLUSPLUS     0x02
#define ISC_LEXCOMMENT_SHELL	     0x04
#define ISC_LEXCOMMENT_DNSMASTERFILE 0x08
/*@}*/

/***
 *** Types
 ***/

/*! Lex */

typedef char isc_lexspecials_t[256];

/* Tokens */

typedef enum {
	isc_tokentype_unknown = 0,
	isc_tokentype_string = 1,
	isc_tokentype_number = 2,
	isc_tokentype_qstring = 3,
	isc_tokentype_eol = 4,
	isc_tokentype_eof = 5,
	isc_tokentype_initialws = 6,
	isc_tokentype_special = 7,
	isc_tokentype_nomore = 8,
	isc_tokentype_btext = 9,
	isc_tokentype_vpair = 10,
	isc_tokentype_qvpair = 11,
} isc_tokentype_t;

typedef union {
	char		 as_char;
	unsigned long	 as_ulong;
	isc_region_t	 as_region;
	isc_textregion_t as_textregion;
	void		*as_pointer;
} isc_tokenvalue_t;

typedef struct isc_token {
	isc_tokentype_t	 type;
	isc_tokenvalue_t value;
} isc_token_t;

/***
 *** Functions
 ***/

void
isc_lex_create(isc_mem_t *mctx, size_t max_token, isc_lex_t **lexp);
/*%<
 * Create a lexer.
 *
 * 'max_token' is a hint of the number of bytes in the largest token.
 *
 * Requires:
 *\li	'*lexp' is a valid lexer.
 *
 * Ensures:
 *\li	On success, *lexp is attached to the newly created lexer.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_NOMEMORY
 */

void
isc_lex_destroy(isc_lex_t **lexp);
/*%<
 * Destroy the lexer.
 *
 * Requires:
 *\li	'*lexp' is a valid lexer.
 *
 * Ensures:
 *\li	*lexp == NULL
 */

unsigned int
isc_lex_getcomments(isc_lex_t *lex);
/*%<
 * Return the current lexer commenting styles.
 *
 * Requires:
 *\li	'lex' is a valid lexer.
 *
 * Returns:
 *\li	The commenting styles which are currently allowed.
 */

void
isc_lex_setcomments(isc_lex_t *lex, unsigned int comments);
/*%<
 * Set allowed lexer commenting styles.
 *
 * Requires:
 *\li	'lex' is a valid lexer.
 *
 *\li	'comments' has meaningful values.
 */

void
isc_lex_getspecials(isc_lex_t *lex, isc_lexspecials_t specials);
/*%<
 * Put the current list of specials into 'specials'.
 *
 * Requires:
 *\li	'lex' is a valid lexer.
 */

void
isc_lex_setspecials(isc_lex_t *lex, isc_lexspecials_t specials);
/*!<
 * The characters in 'specials' are returned as tokens.  Along with
 * whitespace, they delimit strings and numbers.
 *
 * Note:
 *\li	Comment processing takes precedence over special character
 *	recognition.
 *
 * Requires:
 *\li	'lex' is a valid lexer.
 */

isc_result_t
isc_lex_openfile(isc_lex_t *lex, const char *filename);
/*%<
 * Open 'filename' and make it the current input source for 'lex'.
 *
 * Requires:
 *\li	'lex' is a valid lexer.
 *
 *\li	filename is a valid C string.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_NOMEMORY			Out of memory
 *\li	#ISC_R_NOTFOUND			File not found
 *\li	#ISC_R_NOPERM			No permission to open file
 *\li	#ISC_R_FAILURE			Couldn't open file, not sure why
 *\li	#ISC_R_UNEXPECTED
 */

isc_result_t
isc_lex_openstream(isc_lex_t *lex, FILE *stream);
/*%<
 * Make 'stream' the current input source for 'lex'.
 *
 * Requires:
 *\li	'lex' is a valid lexer.
 *
 *\li	'stream' is a valid C stream.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_NOMEMORY			Out of memory
 */

isc_result_t
isc_lex_openbuffer(isc_lex_t *lex, isc_buffer_t *buffer);
/*%<
 * Make 'buffer' the current input source for 'lex'.
 *
 * Requires:
 *\li	'lex' is a valid lexer.
 *
 *\li	'buffer' is a valid buffer.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_NOMEMORY			Out of memory
 */

isc_result_t
isc_lex_close(isc_lex_t *lex);
/*%<
 * Close the most recently opened object (i.e. file or buffer).
 *
 * Returns:
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_NOMORE			No more input sources
 */

isc_result_t
isc_lex_gettoken(isc_lex_t *lex, unsigned int options, isc_token_t *tokenp);
/*%<
 * Get the next token.
 *
 * Requires:
 *\li	'lex' is a valid lexer.
 *
 *\li	'lex' has an input source.
 *
 *\li	'options' contains valid options.
 *
 *\li	'*tokenp' is a valid pointer.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS
 *\li	#ISC_R_UNEXPECTEDEND
 *\li	#ISC_R_NOMEMORY
 *
 *	These two results are returned only if their corresponding lexer
 *	options are not set.
 *
 *\li	#ISC_R_EOF			End of input source
 *\li	#ISC_R_NOMORE			No more input sources
 */

isc_result_t
isc_lex_getmastertoken(isc_lex_t *lex, isc_token_t *token,
		       isc_tokentype_t expect, bool eol);
/*%<
 * Get the next token from a DNS master file type stream.  This is a
 * convenience function that sets appropriate options and handles quoted
 * strings and end of line correctly for master files.  It also ungets
 * unexpected tokens.  If `eol` is set then expect end-of-line otherwise
 * eol is a error.
 *
 * Requires:
 *\li	'lex' is a valid lexer.
 *
 *\li	'token' is a valid pointer
 *
 * Returns:
 *
 * \li	any return code from isc_lex_gettoken().
 */

isc_result_t
isc_lex_getoctaltoken(isc_lex_t *lex, isc_token_t *token, bool eol);
/*%<
 * Get the next token from a DNS master file type stream.  This is a
 * convenience function that sets appropriate options and handles end
 * of line correctly for master files.  It also ungets unexpected tokens.
 * If `eol` is set then expect end-of-line otherwise eol is a error.
 *
 * Requires:
 *\li	'lex' is a valid lexer.
 *
 *\li	'token' is a valid pointer
 *
 * Returns:
 *
 * \li	any return code from isc_lex_gettoken().
 */

void
isc_lex_ungettoken(isc_lex_t *lex, isc_token_t *tokenp);
/*%<
 * Unget the current token.
 *
 * Requires:
 *\li	'lex' is a valid lexer.
 *
 *\li	'lex' has an input source.
 *
 *\li	'tokenp' points to a valid token.
 *
 *\li	There is no ungotten token already.
 */

void
isc_lex_getlasttokentext(isc_lex_t *lex, isc_token_t *tokenp, isc_region_t *r);
/*%<
 * Returns a region containing the text of the last token returned.
 *
 * Requires:
 *\li	'lex' is a valid lexer.
 *
 *\li	'lex' has an input source.
 *
 *\li	'tokenp' points to a valid token.
 *
 *\li	A token has been gotten and not ungotten.
 */

char *
isc_lex_getsourcename(isc_lex_t *lex);
/*%<
 * Return the input source name.
 *
 * Requires:
 *\li	'lex' is a valid lexer.
 *
 * Returns:
 * \li	source name or NULL if no current source.
 *\li	result valid while current input source exists.
 */

unsigned long
isc_lex_getsourceline(isc_lex_t *lex);
/*%<
 * Return the input source line number.
 *
 * Requires:
 *\li	'lex' is a valid lexer.
 *
 * Returns:
 *\li 	Current line number or 0 if no current source.
 */

isc_result_t
isc_lex_setsourcename(isc_lex_t *lex, const char *name);
/*%<
 * Assigns a new name to the input source.
 *
 * Requires:
 *
 * \li	'lex' is a valid lexer.
 *
 * Returns:
 * \li	#ISC_R_SUCCESS
 * \li	#ISC_R_NOMEMORY
 * \li	#ISC_R_NOTFOUND - there are no sources.
 */

isc_result_t
isc_lex_setsourceline(isc_lex_t *lex, unsigned long line);
/*%<
 * Assigns a new line number to the input source. This can be used
 * when parsing a buffer that's been excerpted from the middle a file,
 * allowing logged messages to display the correct line number,
 * rather than the line number within the buffer.
 *
 * Requires:
 *
 * \li	'lex' is a valid lexer.
 *
 * Returns:
 * \li	#ISC_R_SUCCESS
 * \li	#ISC_R_NOTFOUND - there are no sources.
 */

bool
isc_lex_isfile(isc_lex_t *lex);
/*%<
 * Return whether the current input source is a file.
 *
 * Requires:
 *\li	'lex' is a valid lexer.
 *
 * Returns:
 * \li	#true if the current input is a file,
 *\li	#false otherwise.
 */

ISC_LANG_ENDDECLS
