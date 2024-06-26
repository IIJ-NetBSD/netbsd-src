/*	$NetBSD: servent.h,v 1.5 2024/01/20 14:52:48 christos Exp $	*/

/*-
 * Copyright (c) 2004 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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

#include <stdio.h>
#ifdef _REENTRANT
#include "reentrant.h"
#endif

struct servent_data {
	FILE *plainfile;
	struct cdbr *cdb;
	struct servent serv;
	char **aliases;
	size_t maxaliases;
	int flags;
#define	_SV_STAYOPEN	1
#define	_SV_CDB		2
#define	_SV_PLAINFILE	4
#define	_SV_FIRST	8
	uint32_t cdb_index;
	uint8_t *cdb_buf;
	size_t cdb_buf_len;
	char *line;
	void *dummy;
};

#ifdef _REENTRANT
extern mutex_t _servent_mutex;
#endif
extern struct servent_data _servent_data;

struct servent	*getservent_r(struct servent *, struct servent_data *);
struct servent	*getservbyname_r(const char *, const char *,
    struct servent *, struct servent_data *);
struct servent	*getservbyport_r(int, const char *,
    struct servent *, struct servent_data *);
void setservent_r(int, struct servent_data *);
void endservent_r(struct servent_data *);

int _servent_open(struct servent_data *);
void _servent_close(struct servent_data *);
int _servent_getline(struct servent_data *);
struct servent *_servent_parseline(struct servent_data *, struct servent *);
struct servent *_servent_parsedb(struct servent_data *, struct servent *,
    const uint8_t *, size_t);
