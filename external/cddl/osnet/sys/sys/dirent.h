/*	$NetBSD: dirent.h,v 1.5 2025/07/24 09:04:56 hans Exp $	*/

/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: head/sys/cddl/compat/opensolaris/sys/dirent.h 219089 2011-02-27 19:41:40Z pjd $
 */

#ifndef _OPENSOLARIS_SYS_DIRENT_H_
#define	_OPENSOLARIS_SYS_DIRENT_H_

#include <sys/types.h>

#include_next <sys/dirent.h>

#ifndef __sun
typedef	struct dirent	dirent64_t;
typedef ino_t		ino64_t;

#define	dirent64	dirent

#define	d_ino	d_fileno

#define	__DIRENT64_NAMEOFF	__builtin_offsetof(struct dirent, d_name)
#define	DIRENT64_RECLEN(len)	\
	roundup2(__DIRENT64_NAMEOFF + (len) + 1, sizeof(ino_t))

#endif	/* __sun */
#endif	/* !_OPENSOLARIS_SYS_DIRENT_H_ */
