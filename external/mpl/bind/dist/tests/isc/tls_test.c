/*	$NetBSD: tls_test.c,v 1.2 2025/01/26 16:25:50 christos Exp $	*/

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

#include <inttypes.h>
#include <sched.h> /* IWYU pragma: keep */
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * As a workaround, include an OpenSSL header file before including cmocka.h,
 * because OpenSSL 3.1.0 uses __attribute__(malloc), conflicting with a
 * redefined malloc in cmocka.h.
 */
#include <openssl/err.h>

#define UNIT_TESTING
#include <cmocka.h>

#include <isc/loop.h>
#include <isc/nonce.h>
#include <isc/os.h>
#include <isc/quota.h>
#include <isc/refcount.h>
#include <isc/sockaddr.h>
#include <isc/thread.h>
#include <isc/util.h>
#include <isc/uv.h>

#include "uv_wrap.h"
#define KEEP_BEFORE

#include "netmgr_common.h"

#include <tests/isc.h>

ISC_LOOP_TEST_IMPL(tls_noop) {
	stream_noop(arg);
	return;
}

ISC_LOOP_TEST_IMPL(tls_noresponse) {
	stream_noresponse(arg);
	return;
}

ISC_LOOP_TEST_IMPL(tls_shutdownconnect) {
	stream_shutdownconnect(arg);
	return;
}

ISC_LOOP_TEST_IMPL(tls_shutdownread) {
	stream_shutdownread(arg);
	return;
}

ISC_LOOP_TEST_IMPL(tls_timeout_recovery) {
	stream_timeout_recovery(arg);
	return;
}

ISC_LOOP_TEST_IMPL(tls_recv_one) {
	stream_recv_one(arg);
	return;
}

ISC_LOOP_TEST_IMPL(tls_recv_two) {
	stream_recv_two(arg);
	return;
}

ISC_LOOP_TEST_IMPL(tls_recv_send) {
	stream_recv_send(arg);
	return;
}

ISC_LOOP_TEST_IMPL(tls_recv_send_sendback) {
	allow_send_back = true;
	stream_recv_send(arg);
}

/* TLS quota */

ISC_LOOP_TEST_IMPL(tls_recv_one_quota) {
	atomic_store(&check_listener_quota, true);
	stream_recv_one(arg);
}

ISC_LOOP_TEST_IMPL(tls_recv_two_quota) {
	atomic_store(&check_listener_quota, true);
	stream_recv_two(arg);
}

ISC_LOOP_TEST_IMPL(tls_recv_send_quota) {
	atomic_store(&check_listener_quota, true);
	stream_recv_send(arg);
}

ISC_LOOP_TEST_IMPL(tls_recv_send_quota_sendback) {
	allow_send_back = true;
	atomic_store(&check_listener_quota, true);
	stream_recv_send(arg);
}

/* PROXY tests */

ISC_LOOP_TEST_IMPL(proxy_tls_noop) { loop_test_tls_noop(arg); }

ISC_LOOP_TEST_IMPL(proxy_tls_noresponse) { loop_test_tls_noresponse(arg); }

ISC_LOOP_TEST_IMPL(proxy_tls_shutdownconnect) {
	loop_test_tls_shutdownconnect(arg);
}

ISC_LOOP_TEST_IMPL(proxy_tls_shutdownread) { loop_test_tls_shutdownread(arg); }

ISC_LOOP_TEST_IMPL(proxy_tls_timeout_recovery) {
	loop_test_tls_timeout_recovery(arg);
}

ISC_LOOP_TEST_IMPL(proxy_tls_recv_one) { loop_test_tls_recv_one(arg); }

ISC_LOOP_TEST_IMPL(proxy_tls_recv_two) { loop_test_tls_recv_two(arg); }

ISC_LOOP_TEST_IMPL(proxy_tls_recv_send) { loop_test_tls_recv_send(arg); }

ISC_LOOP_TEST_IMPL(proxy_tls_recv_send_sendback) {
	loop_test_tls_recv_send(arg);
}

/* TLS quota */

ISC_LOOP_TEST_IMPL(proxy_tls_recv_one_quota) { loop_test_tls_recv_one(arg); }

ISC_LOOP_TEST_IMPL(proxy_tls_recv_two_quota) { loop_test_tls_recv_two(arg); }

ISC_LOOP_TEST_IMPL(proxy_tls_recv_send_quota) { loop_test_tls_recv_send(arg); }

ISC_LOOP_TEST_IMPL(proxy_tls_recv_send_quota_sendback) {
	loop_test_tls_recv_send(arg);
}

ISC_TEST_LIST_START

/* TLS */
ISC_TEST_ENTRY_CUSTOM(tls_noop, stream_noop_setup, stream_noop_teardown)
ISC_TEST_ENTRY_CUSTOM(tls_noresponse, stream_noresponse_setup,
		      stream_noresponse_teardown)
ISC_TEST_ENTRY_CUSTOM(tls_shutdownconnect, stream_shutdownconnect_setup,
		      stream_shutdownconnect_teardown)
ISC_TEST_ENTRY_CUSTOM(tls_shutdownread, stream_shutdownread_setup,
		      stream_shutdownread_teardown)
ISC_TEST_ENTRY_CUSTOM(tls_timeout_recovery, stream_timeout_recovery_setup,
		      stream_timeout_recovery_teardown)
ISC_TEST_ENTRY_CUSTOM(tls_recv_one, stream_recv_one_setup,
		      stream_recv_one_teardown)
ISC_TEST_ENTRY_CUSTOM(tls_recv_two, stream_recv_two_setup,
		      stream_recv_two_teardown)
ISC_TEST_ENTRY_CUSTOM(tls_recv_send, stream_recv_send_setup,
		      stream_recv_send_teardown)
ISC_TEST_ENTRY_CUSTOM(tls_recv_send_sendback, stream_recv_send_setup,
		      stream_recv_send_teardown)

/* TLS quota */
ISC_TEST_ENTRY_CUSTOM(tls_recv_one_quota, stream_recv_one_setup,
		      stream_recv_one_teardown)
ISC_TEST_ENTRY_CUSTOM(tls_recv_two_quota, stream_recv_two_setup,
		      stream_recv_two_teardown)
ISC_TEST_ENTRY_CUSTOM(tls_recv_send_quota, stream_recv_send_setup,
		      stream_recv_send_teardown)
ISC_TEST_ENTRY_CUSTOM(tls_recv_send_quota_sendback, stream_recv_send_setup,
		      stream_recv_send_teardown)

/* PROXY */

/* TLS */
ISC_TEST_ENTRY_CUSTOM(proxy_tls_noop, proxystream_noop_setup,
		      proxystream_noop_teardown)
ISC_TEST_ENTRY_CUSTOM(proxy_tls_noresponse, proxystream_noresponse_setup,
		      proxystream_noresponse_teardown)
ISC_TEST_ENTRY_CUSTOM(proxy_tls_shutdownconnect,
		      proxystream_shutdownconnect_setup,
		      proxystream_shutdownconnect_teardown)
ISC_TEST_ENTRY_CUSTOM(proxy_tls_shutdownread, proxystream_shutdownread_setup,
		      proxystream_shutdownread_teardown)
ISC_TEST_ENTRY_CUSTOM(proxy_tls_timeout_recovery,
		      proxystream_timeout_recovery_setup,
		      proxystream_timeout_recovery_teardown)
ISC_TEST_ENTRY_CUSTOM(proxy_tls_recv_one, proxystream_recv_one_setup,
		      proxystream_recv_one_teardown)
ISC_TEST_ENTRY_CUSTOM(proxy_tls_recv_two, proxystream_recv_two_setup,
		      proxystream_recv_two_teardown)
ISC_TEST_ENTRY_CUSTOM(proxy_tls_recv_send, proxystream_recv_send_setup,
		      proxystream_recv_send_teardown)
ISC_TEST_ENTRY_CUSTOM(proxy_tls_recv_send_sendback, proxystream_recv_send_setup,
		      proxystream_recv_send_teardown)

/* TLS quota */
ISC_TEST_ENTRY_CUSTOM(proxy_tls_recv_one_quota, proxystream_recv_one_setup,
		      proxystream_recv_one_teardown)
ISC_TEST_ENTRY_CUSTOM(proxy_tls_recv_two_quota, proxystream_recv_two_setup,
		      proxystream_recv_two_teardown)
ISC_TEST_ENTRY_CUSTOM(proxy_tls_recv_send_quota, proxystream_recv_send_setup,
		      proxystream_recv_send_teardown)
ISC_TEST_ENTRY_CUSTOM(proxy_tls_recv_send_quota_sendback,
		      proxystream_recv_send_setup,
		      proxystream_recv_send_teardown)

ISC_TEST_LIST_END

static int
tls_setup(void **state ISC_ATTR_UNUSED) {
	stream_port = TLS_TEST_PORT;
	stream_use_TLS = true;
	stream = true;

	return 0;
}

ISC_TEST_MAIN_CUSTOM(tls_setup, NULL)
