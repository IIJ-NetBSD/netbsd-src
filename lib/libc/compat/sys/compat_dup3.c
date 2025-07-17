/*	$NetBSD: compat_dup3.c,v 1.3 2025/07/17 18:28:58 kre Exp $ */

/*-
 * Copyright (c) 2024 The NetBSD Foundation, Inc.
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
#include <sys/cdefs.h>
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: compat_dup3.c,v 1.3 2025/07/17 18:28:58 kre Exp $");
#endif /* LIBC_SCCS and not lint */

#include "namespace.h"
#define __LIBC12_SOURCE__
#include <fcntl.h>
#include <compat/include/unistd.h>

__warn_references(dup3,
    "warning: reference to compatibility dup3(); include <unistd.h> to generate correct reference")

#ifndef	FD_CLOFORK			/*XXX temporary*/
#define	FD_CLOFORK	0		/*XXX temporary*/
#endif					/*XXX temporary*/
#ifndef	O_CLOFORK			/*XXX temporary*/
#define	O_CLOFORK	0x08000000	/*XXX temporary*/
#endif					/*XXX temporary*/
int
dup3(int oldfd, int newfd, int flags)
{
	int fdflags;

	if (oldfd != newfd) {
		return __dup3100(oldfd, newfd, flags);
	}
	if (flags & (O_NONBLOCK|O_NOSIGPIPE)) {
		int e = fcntl(newfd, F_GETFL, 0);
		if (e == -1)
			return -1;
		e |= flags & (O_NONBLOCK|O_NOSIGPIPE);
		e = fcntl(newfd, F_SETFL, e);
		if (e == -1)
			return -1;
	}

	switch (flags & (O_CLOEXEC|O_CLOFORK)) {
	case O_CLOEXEC|O_CLOFORK:
		fdflags = FD_CLOEXEC|FD_CLOFORK;
		break;
	case O_CLOEXEC:
		fdflags = FD_CLOEXEC;
		break;
	case O_CLOFORK:
		fdflags = FD_CLOFORK;
		break;
	default:
		return 0;
	}
	return fcntl(newfd, F_SETFD, fdflags);
}
