/*	$NetBSD: redir.h,v 1.29 2024/11/11 22:57:42 kre Exp $	*/

/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
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
 *
 *	@(#)redir.h	8.2 (Berkeley) 5/4/95
 */

/* flags passed to redirect */
#define REDIR_PUSH  0x01	/* save previous values of file descriptors */
#define REDIR_BACKQ 0x02	/* save the command output in memory */
#define REDIR_VFORK 0x04	/* running under vfork(2), be careful */
#define REDIR_KEEP  0x08	/* don't close-on-exec */

/* flags passed to popredir and free_rl */
#define POPREDIR_DISCARD	0	/* just abandon everything */
#define POPREDIR_UNDO		1	/* undo saved redirects */
#define POPREDIR_PERMANENT	2	/* keep renamed fd, close saving fd */

union node;
void redirect(union node *, int);
void popredir(int);
int fd0_redirected_p(void);
void clearredir(int);
int movefd(int, int);
int to_upper_fd(int);
void register_sh_fd(int, void (*)(int, int));
void sh_close(int);
struct output;
int outredir(struct output *, union node *, int);

extern int max_user_fd;		/* highest fd used by user */
extern long user_fd_limit;	/* highest possible user fd */
