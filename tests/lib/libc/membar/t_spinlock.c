/*	$NetBSD: t_spinlock.c,v 1.5 2025/05/02 22:30:44 riastradh Exp $	*/

/*-
 * Copyright (c) 2022 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_spinlock.c,v 1.5 2025/05/02 22:30:44 riastradh Exp $");

#include <sys/types.h>

#include <sys/atomic.h>
#include <sys/param.h>
#include <sys/sysctl.h>

#include <assert.h>
#include <atf-c.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#ifdef	BROKEN_ACQUIRE
#undef	membar_acquire
#define	membar_acquire()	asm volatile("" ::: "memory")
#endif	/* BROKEN_ACQUIRE */

#ifdef	BROKEN_RELEASE
#undef	membar_release
#define	membar_release()	asm volatile("" ::: "memory")
#endif	/* BROKEN_RELEASE */

volatile sig_atomic_t times_up;

volatile unsigned lockbit __aligned(COHERENCY_UNIT);

volatile struct {
	uint64_t v;
} __aligned(COHERENCY_UNIT) C[8];
uint64_t TC[2];

static void
lock(void)
{

	while (atomic_swap_uint(&lockbit, 1))
		continue;
	membar_acquire();
}

static void
unlock(void)
{

	membar_release();
#ifdef __HAVE_HASHLOCKED_ATOMICS
	(void)atomic_cas_uint(&lockbit, 1, 0);
#else
	lockbit = 0;
#endif
}

static void *
thread(void *cookie)
{
	unsigned me = (unsigned)(uintptr_t)cookie;
	uint64_t C_local = 0, C0[__arraycount(C)];
	unsigned i;

	while (!times_up) {
		C_local++;
		lock();
		for (i = 0; i < __arraycount(C); i++)
			C0[i] = C[i].v;
		__insn_barrier();
		for (i = __arraycount(C); i --> 0;)
			C[i].v = C0[i] + 1;
		unlock();
	}

	TC[me] = C_local;

	return NULL;
}

ATF_TC(spinlock);
ATF_TC_HEAD(spinlock, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify membar_acquire/release work for spin locks");
}
ATF_TC_BODY(spinlock, tc)
{
	pthread_t t[2];
	unsigned i;
	int ncpu;
	size_t ncpulen = sizeof(ncpu);
	int error;

	alarm(10);

	if (sysctlbyname("hw.ncpu", &ncpu, &ncpulen, NULL, 0) == -1)
		atf_tc_fail("hw.ncpu: (%d) %s", errno, strerror(errno));
	assert(ncpulen == sizeof(ncpu));
	if (ncpu == 1)
		atf_tc_skip("membar tests are only for multicore systems");

#ifdef __powerpc__
	atf_tc_expect_fail("port-powerpc/59386: t_spinlock test is failing");
#endif

	for (i = 0; i < 2; i++) {
		error = pthread_create(&t[i], NULL, &thread,
		    (void *)(uintptr_t)i);
		if (error)
			errc(1, error, "pthread_create");
	}
	sleep(5);
	times_up = 1;
	for (i = 0; i < 2; i++) {
		error = pthread_join(t[i], NULL);
		if (error)
			errc(1, error, "pthread_join");
	}
	for (i = 0; i < __arraycount(C); i++) {
		ATF_CHECK_MSG(C[i].v == TC[0] + TC[1], "%d: "
		    "%"PRIu64" != %"PRIu64" + %"PRIu64" (off by %"PRIdMAX")",
		    i, C[i].v, TC[0], TC[1], TC[0] + TC[1] - C[i].v);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, spinlock);
	return atf_no_error();
}
