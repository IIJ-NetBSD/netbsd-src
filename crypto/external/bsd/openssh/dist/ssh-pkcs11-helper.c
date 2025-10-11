/*	$NetBSD: ssh-pkcs11-helper.c,v 1.24 2025/10/11 15:45:08 christos Exp $	*/
/* $OpenBSD: ssh-pkcs11-helper.c,v 1.29 2025/07/30 04:27:42 djm Exp $ */

/*
 * Copyright (c) 2010 Markus Friedl.  All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "includes.h"
__RCSID("$NetBSD: ssh-pkcs11-helper.c,v 1.24 2025/10/11 15:45:08 christos Exp $");

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <sys/param.h>

#include <stdlib.h>
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "xmalloc.h"
#include "sshbuf.h"
#include "log.h"
#include "misc.h"
#include "sshkey.h"
#include "authfd.h"
#include "ssh-pkcs11.h"
#include "ssherr.h"

/* borrows code from sftp-server and ssh-agent */

static char *providername; /* Provider for this helper */

#define MAX_MSG_LENGTH		10240 /*XXX*/

/* input and output queue */
struct sshbuf *iqueue;
struct sshbuf *oqueue;

static void
send_msg(struct sshbuf *m)
{
	int r;

	if ((r = sshbuf_put_stringb(oqueue, m)) != 0)
		fatal_fr(r, "enqueue");
}

static void
process_add(void)
{
	char *pin;
	struct sshkey **keys = NULL;
	int r, i, nkeys;
	struct sshbuf *msg;
	char **labels = NULL;

	if (providername != NULL)
		fatal_f("provider already set");
	if ((msg = sshbuf_new()) == NULL)
		fatal_f("sshbuf_new failed");
	if ((r = sshbuf_get_cstring(iqueue, &providername, NULL)) != 0 ||
	    (r = sshbuf_get_cstring(iqueue, &pin, NULL)) != 0)
		fatal_fr(r, "parse");
	debug3_f("add %s", providername);
	if ((nkeys = pkcs11_add_provider(providername, pin,
	    &keys, &labels)) > 0) {
		if ((r = sshbuf_put_u8(msg,
		    SSH2_AGENT_IDENTITIES_ANSWER)) != 0 ||
		    (r = sshbuf_put_u32(msg, nkeys)) != 0)
			fatal_fr(r, "compose");
		for (i = 0; i < nkeys; i++) {
			if ((r = sshkey_puts(keys[i], msg)) != 0 ||
			    (r = sshbuf_put_cstring(msg, labels[i])) != 0)
				fatal_fr(r, "compose key");
			debug3_f("%s: %s \"%s\"", providername,
			    sshkey_type(keys[i]), labels[i]);
			free(labels[i]);
		}
	} else if ((r = sshbuf_put_u8(msg, SSH_AGENT_FAILURE)) != 0 ||
	    (r = sshbuf_put_u32(msg, -nkeys)) != 0)
		fatal_fr(r, "compose");
	free(labels);
	free(keys); /* keys themselves are transferred to pkcs11_keylist */
	free(pin);
	send_msg(msg);
	sshbuf_free(msg);
}

static void
process_sign(void)
{
	const u_char *data;
	u_char *signature = NULL;
	size_t dlen, slen = 0;
	u_int compat;
	int r, ok = -1;
	struct sshkey *key = NULL;
	struct sshbuf *msg;
	char *alg = NULL;

	if ((r = sshkey_froms(iqueue, &key)) != 0 ||
	    (r = sshbuf_get_string_direct(iqueue, &data, &dlen)) != 0 ||
	    (r = sshbuf_get_cstring(iqueue, &alg, NULL)) != 0 ||
	    (r = sshbuf_get_u32(iqueue, &compat)) != 0)
		fatal_fr(r, "parse");

	if (*alg == '\0') {
		free(alg);
		alg = NULL;
	}

	if ((r = pkcs11_sign(key, &signature, &slen, data, dlen,
	    alg, NULL, NULL, compat)) != 0) {
		error_fr(r, "sign %s", sshkey_type(key));
		goto reply;
	}

	/* success */
	ok = 0;
 reply:
	if ((msg = sshbuf_new()) == NULL)
		fatal_f("sshbuf_new failed");
	if (ok == 0) {
		if ((r = sshbuf_put_u8(msg, SSH2_AGENT_SIGN_RESPONSE)) != 0 ||
		    (r = sshbuf_put_string(msg, signature, slen)) != 0)
			fatal_fr(r, "compose response");
	} else {
		if ((r = sshbuf_put_u8(msg, SSH2_AGENT_FAILURE)) != 0)
			fatal_fr(r, "compose failure response");
	}
	sshkey_free(key);
	free(alg);
	free(signature);
	send_msg(msg);
	sshbuf_free(msg);
}

static void
process(void)
{
	u_int msg_len;
	u_int buf_len;
	u_int consumed;
	u_char type;
	const u_char *cp;
	int r;

	buf_len = sshbuf_len(iqueue);
	if (buf_len < 5)
		return;		/* Incomplete message. */
	cp = sshbuf_ptr(iqueue);
	msg_len = get_u32(cp);
	if (msg_len > MAX_MSG_LENGTH) {
		error("bad message len %d", msg_len);
		cleanup_exit(11);
	}
	if (buf_len < msg_len + 4)
		return;
	if ((r = sshbuf_consume(iqueue, 4)) != 0 ||
	    (r = sshbuf_get_u8(iqueue, &type)) != 0)
		fatal_fr(r, "parse type/len");
	buf_len -= 4;
	switch (type) {
	case SSH_AGENTC_ADD_SMARTCARD_KEY:
		debug("process_add");
		process_add();
		break;
	case SSH2_AGENTC_SIGN_REQUEST:
		debug("process_sign");
		process_sign();
		break;
	default:
		error("Unknown message %d", type);
		break;
	}
	/* discard the remaining bytes from the current packet */
	if (buf_len < sshbuf_len(iqueue)) {
		error("iqueue grew unexpectedly");
		cleanup_exit(255);
	}
	consumed = buf_len - sshbuf_len(iqueue);
	if (msg_len < consumed) {
		error("msg_len %d < consumed %d", msg_len, consumed);
		cleanup_exit(255);
	}
	if (msg_len > consumed) {
		if ((r = sshbuf_consume(iqueue, msg_len - consumed)) != 0)
			fatal_fr(r, "consume");
	}
}

void
cleanup_exit(int i)
{
	/* XXX */
	_exit(i);
}

int
main(int argc, char **argv)
{
	int r, ch, in, out, log_stderr = 0;
	ssize_t len;
	SyslogFacility log_facility = SYSLOG_FACILITY_AUTH;
	LogLevel log_level = SYSLOG_LEVEL_ERROR;
	char buf[4*4096];
	extern char *__progname;
	struct pollfd pfd[2];

	log_init(__progname, log_level, log_facility, log_stderr);

	while ((ch = getopt(argc, argv, "v")) != -1) {
		switch (ch) {
		case 'v':
			log_stderr = 1;
			if (log_level == SYSLOG_LEVEL_ERROR)
				log_level = SYSLOG_LEVEL_DEBUG1;
			else if (log_level < SYSLOG_LEVEL_DEBUG3)
				log_level++;
			break;
		default:
			fprintf(stderr, "usage: %s [-v]\n", __progname);
			exit(1);
		}
	}

	log_init(__progname, log_level, log_facility, log_stderr);

	pkcs11_init(0);
	in = STDIN_FILENO;
	out = STDOUT_FILENO;

	if ((iqueue = sshbuf_new()) == NULL)
		fatal_f("sshbuf_new failed");
	if ((oqueue = sshbuf_new()) == NULL)
		fatal_f("sshbuf_new failed");

	while (1) {
		memset(pfd, 0, sizeof(pfd));
		pfd[0].fd = in;
		pfd[1].fd = out;

		/*
		 * Ensure that we can read a full buffer and handle
		 * the worst-case length packet it can generate,
		 * otherwise apply backpressure by stopping reads.
		 */
		if ((r = sshbuf_check_reserve(iqueue, sizeof(buf))) == 0 &&
		    (r = sshbuf_check_reserve(oqueue, MAX_MSG_LENGTH)) == 0)
			pfd[0].events = POLLIN;
		else if (r != SSH_ERR_NO_BUFFER_SPACE)
			fatal_fr(r, "reserve");

		if (sshbuf_len(oqueue) > 0)
			pfd[1].events = POLLOUT;

		if ((r = poll(pfd, 2, -1 /* INFTIM */)) <= 0) {
			if (r == 0 || errno == EINTR)
				continue;
			fatal("poll: %s", strerror(errno));
		}

		/* copy stdin to iqueue */
		if ((pfd[0].revents & (POLLIN|POLLHUP|POLLERR)) != 0) {
			len = read(in, buf, sizeof buf);
			if (len == 0) {
				debug("read eof");
				cleanup_exit(0);
			} else if (len < 0) {
				error("read: %s", strerror(errno));
				cleanup_exit(1);
			} else if ((r = sshbuf_put(iqueue, buf, len)) != 0)
				fatal_fr(r, "sshbuf_put");
		}
		/* send oqueue to stdout */
		if ((pfd[1].revents & (POLLOUT|POLLHUP)) != 0) {
			len = write(out, sshbuf_ptr(oqueue),
			    sshbuf_len(oqueue));
			if (len < 0) {
				error("write: %s", strerror(errno));
				cleanup_exit(1);
			} else if ((r = sshbuf_consume(oqueue, len)) != 0)
				fatal_fr(r, "consume");
		}

		/*
		 * Process requests from client if we can fit the results
		 * into the output buffer, otherwise stop processing input
		 * and let the output queue drain.
		 */
		if ((r = sshbuf_check_reserve(oqueue, MAX_MSG_LENGTH)) == 0)
			process();
		else if (r != SSH_ERR_NO_BUFFER_SPACE)
			fatal_fr(r, "reserve");
	}
}
