/*	$NetBSD: regular.c,v 1.26 2025/01/12 06:38:21 simonb Exp $	*/

/*-
 * Copyright (c) 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)regular.c	8.3 (Berkeley) 4/2/94";
#else
__RCSID("$NetBSD: regular.c,v 1.26 2025/01/12 06:38:21 simonb Exp $");
#endif
#endif /* not lint */

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <err.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "extern.h"

void
c_regular(int fd1, const char *file1, off_t skip1, off_t len1,
    int fd2, const char *file2, off_t skip2, off_t len2)
{
	u_char ch, *p1, *p2;
	off_t byte, length, line;
	int dfound;
	size_t blk_sz, blk_cnt;

	if (skip1 > len1)
		eofmsg(file1, len1 + 1, 0);
	len1 -= skip1;
	if (skip2 > len2)
		eofmsg(file2, len2 + 1, 0);
	len2 -= skip2;

	if (sflag && len1 != len2)
		exit(DIFF_EXIT);

	byte = line = 1;
	dfound = 0;
	length = MIN(len1, len2);
	for (blk_sz = 1024 * 1024; length != 0; length -= blk_sz) {
		if ((uintmax_t)blk_sz > (uintmax_t)length)
			blk_sz = length;
		p1 = mmap(NULL, blk_sz, PROT_READ, MAP_FILE|MAP_SHARED,
		    fd1, skip1);
		if (p1 == MAP_FAILED)
			goto mmap_failed;

		p2 = mmap(NULL, blk_sz, PROT_READ, MAP_FILE|MAP_SHARED,
		    fd2, skip2);
		if (p2 == MAP_FAILED) {
			munmap(p1, blk_sz);
			goto mmap_failed;
		}

		blk_cnt = blk_sz;
		if ((lflag || sflag) && (memcmp(p1, p2, blk_sz) == 0)) {
			/*
			 * If the two blocks are the same and we are
			 * using the -l or -s flags, we don't need to
			 * count lines.  There is nothing else to do
			 * except advance the pointers for munmap()
			 * below.
			 */
			p1 += blk_sz;
			p2 += blk_sz;
		} else {
			for (; blk_cnt--; ++p1, ++p2, ++byte) {
				if ((ch = *p1) != *p2) {
					if (!lflag) {
						diffmsg(file1, file2, byte, line);
						/* NOTREACHED */
					}
					dfound = 1;
					(void)printf("%6lld %3o %3o\n",
					    (long long)byte, ch, *p2);
				}
				if (ch == '\n')
					++line;
			}
		}
		munmap(p1 - blk_sz, blk_sz);
		munmap(p2 - blk_sz, blk_sz);
		skip1 += blk_sz;
		skip2 += blk_sz;
	}

	if (len1 != len2)
		eofmsg(len1 > len2 ? file2 : file1, byte, line);
	if (dfound)
		exit(DIFF_EXIT);
	return;

mmap_failed:
	c_special(fd1, file1, skip1, fd2, file2, skip2);
}
