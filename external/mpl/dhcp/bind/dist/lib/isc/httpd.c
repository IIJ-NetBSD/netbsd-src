/*	$NetBSD: httpd.c,v 1.1 2024/02/18 20:57:49 christos Exp $	*/

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

/*! \file */

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include <isc/buffer.h>
#include <isc/httpd.h>
#include <isc/mem.h>
#include <isc/print.h>
#include <isc/refcount.h>
#include <isc/socket.h>
#include <isc/string.h>
#include <isc/task.h>
#include <isc/time.h>
#include <isc/util.h>

#ifdef HAVE_ZLIB
#include <zlib.h>
#endif /* ifdef HAVE_ZLIB */

/*%
 * TODO:
 *
 *  o  Make the URL processing external functions which will fill-in a buffer
 *     structure we provide, or return an error and we will render a generic
 *     page and close the client.
 */

#define MSHUTTINGDOWN(cm)    ((cm->flags & ISC_HTTPDMGR_FLAGSHUTTINGDOWN) != 0)
#define MSETSHUTTINGDOWN(cm) (cm->flags |= ISC_HTTPDMGR_FLAGSHUTTINGDOWN)

#define HTTP_RECVLEN	 1024
#define HTTP_SENDGROW	 1024
#define HTTP_SEND_MAXLEN 10240

#define HTTPD_CLOSE	     0x0001 /* Got a Connection: close header */
#define HTTPD_FOUNDHOST	     0x0002 /* Got a Host: header */
#define HTTPD_KEEPALIVE	     0x0004 /* Got a Connection: Keep-Alive */
#define HTTPD_ACCEPT_DEFLATE 0x0008

#define HTTPD_MAGIC    ISC_MAGIC('H', 't', 'p', 'd')
#define VALID_HTTPD(m) ISC_MAGIC_VALID(m, HTTPD_MAGIC)

#define HTTPDMGR_MAGIC	  ISC_MAGIC('H', 'p', 'd', 'm')
#define VALID_HTTPDMGR(m) ISC_MAGIC_VALID(m, HTTPDMGR_MAGIC)

/*% http client */
struct isc_httpd {
	unsigned int magic; /* HTTPD_MAGIC */
	isc_refcount_t references;
	isc_httpdmgr_t *mgr; /*%< our parent */
	ISC_LINK(isc_httpd_t) link;
	unsigned int state;
	isc_socket_t *sock;

	/*%
	 * Received data state.
	 */
	char recvbuf[HTTP_RECVLEN]; /*%< receive buffer */
	uint32_t recvlen;	    /*%< length recv'd */
	char *headers;		    /*%< set in process_request() */
	unsigned int method;
	char *url;
	char *querystring;
	char *protocol;

	/*
	 * Flags on the httpd client.
	 */
	int flags;

	/*%
	 * Transmit data state.
	 *
	 * This is the data buffer we will transmit.
	 *
	 * This free function pointer is filled in by the rendering function
	 * we call.  The free function is called after the data is transmitted
	 * to the client.
	 *
	 * The bufflist is the list of buffers we are currently transmitting.
	 * The headerbuffer is where we render our headers to.  If we run out
	 * of space when rendering a header, we will change the size of our
	 * buffer.  We will not free it until we are finished, and will
	 * allocate an additional HTTP_SENDGROW bytes per header space grow.
	 *
	 * We currently use three buffers total, one for the headers (which
	 * we manage), another for the client to fill in (which it manages,
	 * it provides the space for it, etc) -- we will pass that buffer
	 * structure back to the caller, who is responsible for managing the
	 * space it may have allocated as backing store for it.  This second
	 * buffer is bodybuffer, and we only allocate the buffer itself, not
	 * the backing store.
	 * The third buffer is compbuffer, managed by us, that contains the
	 * compressed HTTP data, if compression is used.
	 *
	 */
	isc_buffer_t headerbuffer;
	isc_buffer_t compbuffer;
	isc_buffer_t *sendbuffer;

	const char *mimetype;
	unsigned int retcode;
	const char *retmsg;
	isc_buffer_t bodybuffer;
	isc_httpdfree_t *freecb;
	void *freecb_arg;
};

/*% lightweight socket manager for httpd output */
struct isc_httpdmgr {
	unsigned int magic; /* HTTPDMGR_MAGIC */
	isc_refcount_t references;
	isc_mem_t *mctx;
	isc_socket_t *sock; /*%< listening socket */
	isc_task_t *task;   /*%< owning task */
	isc_timermgr_t *timermgr;

	isc_httpdclientok_t *client_ok;	 /*%< client validator */
	isc_httpdondestroy_t *ondestroy; /*%< cleanup callback */
	void *cb_arg;			 /*%< argument for the above */

	unsigned int flags;
	ISC_LIST(isc_httpd_t) running; /*%< running clients */

	isc_mutex_t lock;

	ISC_LIST(isc_httpdurl_t) urls; /*%< urls we manage */
	isc_httpdaction_t *render_404;
	isc_httpdaction_t *render_500;
};

/*%
 * HTTP methods.
 */
#define ISC_HTTPD_METHODUNKNOWN 0
#define ISC_HTTPD_METHODGET	1
#define ISC_HTTPD_METHODPOST	2

/*%
 * Client states.
 *
 * _IDLE	The client is not doing anything at all.  This state should
 *		only occur just after creation, and just before being
 *		destroyed.
 *
 * _RECV	The client is waiting for data after issuing a socket recv().
 *
 * _RECVDONE	Data has been received, and is being processed.
 *
 * _SEND	All data for a response has completed, and a reply was
 *		sent via a socket send() call.
 *
 * _SENDDONE	Send is completed.
 *
 * Badly formatted state table:
 *
 *	IDLE -> RECV when client has a recv() queued.
 *
 *	RECV -> RECVDONE when recvdone event received.
 *
 *	RECVDONE -> SEND if the data for a reply is at hand.
 *
 *	SEND -> RECV when a senddone event was received.
 *
 *	At any time -> RECV on error.  If RECV fails, the client will
 *	self-destroy, closing the socket and freeing memory.
 */
#define ISC_HTTPD_STATEIDLE	0
#define ISC_HTTPD_STATERECV	1
#define ISC_HTTPD_STATERECVDONE 2
#define ISC_HTTPD_STATESEND	3
#define ISC_HTTPD_STATESENDDONE 4

#define ISC_HTTPD_ISRECV(c)	((c)->state == ISC_HTTPD_STATERECV)
#define ISC_HTTPD_ISRECVDONE(c) ((c)->state == ISC_HTTPD_STATERECVDONE)
#define ISC_HTTPD_ISSEND(c)	((c)->state == ISC_HTTPD_STATESEND)
#define ISC_HTTPD_ISSENDDONE(c) ((c)->state == ISC_HTTPD_STATESENDDONE)

/*%
 * Overall magic test that means we're not idle.
 */
#define ISC_HTTPD_SETRECV(c)	 ((c)->state = ISC_HTTPD_STATERECV)
#define ISC_HTTPD_SETRECVDONE(c) ((c)->state = ISC_HTTPD_STATERECVDONE)
#define ISC_HTTPD_SETSEND(c)	 ((c)->state = ISC_HTTPD_STATESEND)
#define ISC_HTTPD_SETSENDDONE(c) ((c)->state = ISC_HTTPD_STATESENDDONE)

static void
isc_httpd_accept(isc_task_t *, isc_event_t *);
static void
isc_httpd_recvdone(isc_task_t *, isc_event_t *);
static void
isc_httpd_senddone(isc_task_t *, isc_event_t *);
static isc_result_t
process_request(isc_httpd_t *, int);
static isc_result_t
grow_headerspace(isc_httpd_t *);
static void
reset_client(isc_httpd_t *httpd);

static isc_httpdaction_t render_404;
static isc_httpdaction_t render_500;

#if ENABLE_AFL
static void (*finishhook)(void) = NULL;
#endif /* ENABLE_AFL */

static void
maybe_destroy_httpd(isc_httpd_t *);
static void
destroy_httpd(isc_httpd_t *);
static void
maybe_destroy_httpdmgr(isc_httpdmgr_t *);
static void
destroy_httpdmgr(isc_httpdmgr_t *);

static void
isc_httpdmgr_attach(isc_httpdmgr_t *, isc_httpdmgr_t **);
static void
isc_httpdmgr_detach(isc_httpdmgr_t **);

static void
maybe_destroy_httpd(isc_httpd_t *httpd) {
	if (isc_refcount_decrement(&httpd->references) == 1) {
		destroy_httpd(httpd);
	}
}

static void
free_buffer(isc_mem_t *mctx, isc_buffer_t *buffer) {
	isc_region_t r;

	isc_buffer_region(buffer, &r);
	if (r.length > 0) {
		isc_mem_put(mctx, r.base, r.length);
	}

	isc_buffer_initnull(buffer);
}

static void
destroy_httpd(isc_httpd_t *httpd) {
	isc_httpdmgr_t *httpdmgr;

	REQUIRE(VALID_HTTPD(httpd));

	httpdmgr = httpd->mgr;
	REQUIRE(VALID_HTTPDMGR(httpdmgr));

	/*
	 * Unlink before calling isc_socket_detach so
	 * isc_httpdmgr_shutdown does not dereference a NULL pointer
	 * when calling isc_socket_cancel().
	 */
	LOCK(&httpdmgr->lock);
	ISC_LIST_UNLINK(httpdmgr->running, httpd, link);
	UNLOCK(&httpdmgr->lock);

	httpd->magic = 0;
	isc_refcount_destroy(&httpd->references);
	isc_socket_detach(&httpd->sock);

	free_buffer(httpdmgr->mctx, &httpd->headerbuffer);
	free_buffer(httpdmgr->mctx, &httpd->compbuffer);

	isc_mem_put(httpdmgr->mctx, httpd, sizeof(isc_httpd_t));

#if ENABLE_AFL
	if (finishhook != NULL) {
		finishhook();
	}
#endif /* ENABLE_AFL */

	isc_httpdmgr_detach(&httpdmgr);
}

static isc_result_t
httpdmgr_socket_accept(isc_task_t *task, isc_httpdmgr_t *httpdmgr) {
	isc_result_t result = ISC_R_SUCCESS;

	/* decremented in isc_httpd_accept */
	isc_refcount_increment(&httpdmgr->references);
	result = isc_socket_accept(httpdmgr->sock, task, isc_httpd_accept,
				   httpdmgr);
	if (result != ISC_R_SUCCESS) {
		INSIST(isc_refcount_decrement(&httpdmgr->references) > 1);
	}
	return (result);
}

static void
httpd_socket_recv(isc_httpd_t *httpd, isc_region_t *region, isc_task_t *task) {
	isc_result_t result = ISC_R_SUCCESS;

	/* decremented in isc_httpd_recvdone */
	(void)isc_refcount_increment(&httpd->references);
	result = isc_socket_recv(httpd->sock, region, 1, task,
				 isc_httpd_recvdone, httpd);
	if (result != ISC_R_SUCCESS) {
		INSIST(isc_refcount_decrement(&httpd->references) > 1);
	}
}

static void
httpd_socket_send(isc_httpd_t *httpd, isc_region_t *region, isc_task_t *task) {
	isc_result_t result = ISC_R_SUCCESS;

	/* decremented in isc_httpd_senddone */
	(void)isc_refcount_increment(&httpd->references);
	result = isc_socket_send(httpd->sock, region, task, isc_httpd_senddone,
				 httpd);
	if (result != ISC_R_SUCCESS) {
		INSIST(isc_refcount_decrement(&httpd->references) > 1);
	}
}

isc_result_t
isc_httpdmgr_create(isc_mem_t *mctx, isc_socket_t *sock, isc_task_t *task,
		    isc_httpdclientok_t *client_ok,
		    isc_httpdondestroy_t *ondestroy, void *cb_arg,
		    isc_timermgr_t *tmgr, isc_httpdmgr_t **httpdmgrp) {
	isc_result_t result;
	isc_httpdmgr_t *httpdmgr;

	REQUIRE(mctx != NULL);
	REQUIRE(sock != NULL);
	REQUIRE(task != NULL);
	REQUIRE(tmgr != NULL);
	REQUIRE(httpdmgrp != NULL && *httpdmgrp == NULL);

	httpdmgr = isc_mem_get(mctx, sizeof(isc_httpdmgr_t));

	*httpdmgr = (isc_httpdmgr_t){ .timermgr = tmgr, /* XXXMLG no attach
							 * function? */
				      .client_ok = client_ok,
				      .ondestroy = ondestroy,
				      .cb_arg = cb_arg,
				      .render_404 = render_404,
				      .render_500 = render_500 };

	isc_mutex_init(&httpdmgr->lock);
	isc_mem_attach(mctx, &httpdmgr->mctx);
	isc_socket_attach(sock, &httpdmgr->sock);
	isc_task_attach(task, &httpdmgr->task);

	ISC_LIST_INIT(httpdmgr->running);
	ISC_LIST_INIT(httpdmgr->urls);

	isc_refcount_init(&httpdmgr->references, 1);

	/* XXXMLG ignore errors on isc_socket_listen() */
	result = isc_socket_listen(sock, SOMAXCONN);
	if (result != ISC_R_SUCCESS) {
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "isc_socket_listen() failed: %s",
				 isc_result_totext(result));
		goto cleanup;
	}

	(void)isc_socket_filter(sock, "httpready");

	httpdmgr->magic = HTTPDMGR_MAGIC;

	result = httpdmgr_socket_accept(task, httpdmgr);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	*httpdmgrp = httpdmgr;
	return (ISC_R_SUCCESS);

cleanup:
	httpdmgr->magic = 0;
	isc_refcount_decrementz(&httpdmgr->references);
	isc_refcount_destroy(&httpdmgr->references);
	isc_task_detach(&httpdmgr->task);
	isc_socket_detach(&httpdmgr->sock);
	isc_mem_detach(&httpdmgr->mctx);
	isc_mutex_destroy(&httpdmgr->lock);
	isc_mem_put(mctx, httpdmgr, sizeof(isc_httpdmgr_t));
	return (result);
}

static void
isc_httpdmgr_attach(isc_httpdmgr_t *source, isc_httpdmgr_t **targetp) {
	REQUIRE(VALID_HTTPDMGR(source));
	REQUIRE(targetp != NULL && *targetp == NULL);

	isc_refcount_increment(&source->references);

	*targetp = source;
}

static void
isc_httpdmgr_detach(isc_httpdmgr_t **httpdmgrp) {
	REQUIRE(httpdmgrp != NULL && VALID_HTTPDMGR(*httpdmgrp));
	isc_httpdmgr_t *httpdmgr = *httpdmgrp;
	*httpdmgrp = NULL;

	maybe_destroy_httpdmgr(httpdmgr);
}

static void
maybe_destroy_httpdmgr(isc_httpdmgr_t *httpdmgr) {
	if (isc_refcount_decrement(&httpdmgr->references) == 1) {
		destroy_httpdmgr(httpdmgr);
	}
}

static void
destroy_httpdmgr(isc_httpdmgr_t *httpdmgr) {
	isc_httpdurl_t *url;

	isc_refcount_destroy(&httpdmgr->references);

	LOCK(&httpdmgr->lock);

	httpdmgr->magic = 0;

	INSIST(MSHUTTINGDOWN(httpdmgr));
	INSIST(ISC_LIST_EMPTY(httpdmgr->running));

	isc_socket_detach(&httpdmgr->sock);
	isc_task_detach(&httpdmgr->task);
	httpdmgr->timermgr = NULL;

	/*
	 * Clear out the list of all actions we know about.  Just free the
	 * memory.
	 */
	url = ISC_LIST_HEAD(httpdmgr->urls);
	while (url != NULL) {
		isc_mem_free(httpdmgr->mctx, url->url);
		ISC_LIST_UNLINK(httpdmgr->urls, url, link);
		isc_mem_put(httpdmgr->mctx, url, sizeof(isc_httpdurl_t));
		url = ISC_LIST_HEAD(httpdmgr->urls);
	}

	UNLOCK(&httpdmgr->lock);
	isc_mutex_destroy(&httpdmgr->lock);

	if (httpdmgr->ondestroy != NULL) {
		(httpdmgr->ondestroy)(httpdmgr->cb_arg);
	}
	isc_mem_putanddetach(&httpdmgr->mctx, httpdmgr, sizeof(isc_httpdmgr_t));
}

#define LENGTHOK(s) (httpd->recvbuf - (s) < (int)httpd->recvlen)
#define BUFLENOK(s) (httpd->recvbuf - (s) < HTTP_RECVLEN)

/*
 * Look for the given header in headers.
 * If value is specified look for it terminated with a character in eov.
 */
static bool
have_header(isc_httpd_t *httpd, const char *header, const char *value,
	    const char *eov) {
	char *cr, *nl, *h;
	size_t hlen, vlen = 0;

	h = httpd->headers;
	hlen = strlen(header);
	if (value != NULL) {
		INSIST(eov != NULL);
		vlen = strlen(value);
	}

	for (;;) {
		if (strncasecmp(h, header, hlen) != 0) {
			/*
			 * Skip to next line;
			 */
			cr = strchr(h, '\r');
			if (cr != NULL && cr[1] == '\n') {
				cr++;
			}
			nl = strchr(h, '\n');

			/* last header? */
			h = cr;
			if (h == NULL || (nl != NULL && nl < h)) {
				h = nl;
			}
			if (h == NULL) {
				return (false);
			}
			h++;
			continue;
		}

		if (value == NULL) {
			return (true);
		}

		/*
		 * Skip optional leading white space.
		 */
		h += hlen;
		while (*h == ' ' || *h == '\t') {
			h++;
		}
		/*
		 * Terminate token search on NULL or EOL.
		 */
		while (*h != 0 && *h != '\r' && *h != '\n') {
			if (strncasecmp(h, value, vlen) == 0) {
				if (strchr(eov, h[vlen]) != NULL) {
					return (true);
					/*
					 * Skip to next token.
					 */
				}
			}
			/*
			 * Skip to next token.
			 */
			h += strcspn(h, eov);
			if (h[0] == '\r' && h[1] == '\n') {
				h++;
			}
			if (h[0] != 0) {
				h++;
			}
		}
		return (false);
	}
}

static isc_result_t
process_request(isc_httpd_t *httpd, int length) {
	char *s;
	char *p;
	int delim;

	httpd->recvlen += length;

	httpd->recvbuf[httpd->recvlen] = 0;
	httpd->headers = NULL;

	/*
	 * If we don't find a blank line in our buffer, return that we need
	 * more data.
	 */
	s = strstr(httpd->recvbuf, "\r\n\r\n");
	delim = 2;
	if (s == NULL) {
		s = strstr(httpd->recvbuf, "\n\n");
		delim = 1;
	}
	if (s == NULL) {
		return (ISC_R_NOTFOUND);
	}

	/*
	 * NUL terminate request at the blank line.
	 */
	s[delim] = 0;

	/*
	 * Determine if this is a POST or GET method.  Any other values will
	 * cause an error to be returned.
	 */
	if (strncmp(httpd->recvbuf, "GET ", 4) == 0) {
		httpd->method = ISC_HTTPD_METHODGET;
		p = httpd->recvbuf + 4;
	} else if (strncmp(httpd->recvbuf, "POST ", 5) == 0) {
		httpd->method = ISC_HTTPD_METHODPOST;
		p = httpd->recvbuf + 5;
	} else {
		return (ISC_R_RANGE);
	}

	/*
	 * From now on, p is the start of our buffer.
	 */

	/*
	 * Extract the URL.
	 */
	s = p;
	while (LENGTHOK(s) && BUFLENOK(s) &&
	       (*s != '\n' && *s != '\r' && *s != '\0' && *s != ' '))
	{
		s++;
	}
	if (!LENGTHOK(s)) {
		return (ISC_R_NOTFOUND);
	}
	if (!BUFLENOK(s)) {
		return (ISC_R_NOMEMORY);
	}
	*s = 0;

	/*
	 * Make the URL relative.
	 */
	if ((strncmp(p, "http:/", 6) == 0) || (strncmp(p, "https:/", 7) == 0)) {
		/* Skip first / */
		while (*p != '/' && *p != 0) {
			p++;
		}
		if (*p == 0) {
			return (ISC_R_RANGE);
		}
		p++;
		/* Skip second / */
		while (*p != '/' && *p != 0) {
			p++;
		}
		if (*p == 0) {
			return (ISC_R_RANGE);
		}
		p++;
		/* Find third / */
		while (*p != '/' && *p != 0) {
			p++;
		}
		if (*p == 0) {
			p--;
			*p = '/';
		}
	}

	httpd->url = p;
	p = s + 1;
	s = p;

	/*
	 * Now, see if there is a ? mark in the URL.  If so, this is
	 * part of the query string, and we will split it from the URL.
	 */
	httpd->querystring = strchr(httpd->url, '?');
	if (httpd->querystring != NULL) {
		*(httpd->querystring) = 0;
		httpd->querystring++;
	}

	/*
	 * Extract the HTTP/1.X protocol.  We will bounce on anything but
	 * HTTP/1.0 or HTTP/1.1 for now.
	 */
	while (LENGTHOK(s) && BUFLENOK(s) &&
	       (*s != '\n' && *s != '\r' && *s != '\0'))
	{
		s++;
	}
	if (!LENGTHOK(s)) {
		return (ISC_R_NOTFOUND);
	}
	if (!BUFLENOK(s)) {
		return (ISC_R_NOMEMORY);
	}
	/*
	 * Check that we have the expected eol delimiter.
	 */
	if (strncmp(s, delim == 1 ? "\n" : "\r\n", delim) != 0) {
		return (ISC_R_RANGE);
	}
	*s = 0;
	if ((strncmp(p, "HTTP/1.0", 8) != 0) &&
	    (strncmp(p, "HTTP/1.1", 8) != 0))
	{
		return (ISC_R_RANGE);
	}
	httpd->protocol = p;
	p = s + delim; /* skip past eol */
	s = p;

	httpd->headers = s;

	if (have_header(httpd, "Connection:", "close", ", \t\r\n")) {
		httpd->flags |= HTTPD_CLOSE;
	}

	if (have_header(httpd, "Host:", NULL, NULL)) {
		httpd->flags |= HTTPD_FOUNDHOST;
	}

	if (strncmp(httpd->protocol, "HTTP/1.0", 8) == 0) {
		if (have_header(httpd, "Connection:", "Keep-Alive", ", \t\r\n"))
		{
			httpd->flags |= HTTPD_KEEPALIVE;
		} else {
			httpd->flags |= HTTPD_CLOSE;
		}
	}

	/*
	 * Check for Accept-Encoding:
	 */
#ifdef HAVE_ZLIB
	if (have_header(httpd, "Accept-Encoding:", "deflate", ";, \t\r\n")) {
		httpd->flags |= HTTPD_ACCEPT_DEFLATE;
	}
#endif /* ifdef HAVE_ZLIB */

	/*
	 * Standards compliance hooks here.
	 */
	if (strcmp(httpd->protocol, "HTTP/1.1") == 0 &&
	    ((httpd->flags & HTTPD_FOUNDHOST) == 0))
	{
		return (ISC_R_RANGE);
	}

	return (ISC_R_SUCCESS);
}

static void
isc_httpd_create(isc_httpdmgr_t *httpdmgr, isc_socket_t *sock,
		 isc_httpd_t **httpdp) {
	isc_httpd_t *httpd;
	char *headerdata;

	REQUIRE(VALID_HTTPDMGR(httpdmgr));
	REQUIRE(httpdp != NULL && *httpdp == NULL);

	httpd = isc_mem_get(httpdmgr->mctx, sizeof(isc_httpd_t));

	*httpd = (isc_httpd_t){ .sock = sock };

	isc_httpdmgr_attach(httpdmgr, &httpd->mgr);

	isc_refcount_init(&httpd->references, 1);
	ISC_HTTPD_SETRECV(httpd);
	isc_socket_setname(httpd->sock, "httpd", NULL);

	/*
	 * Initialize the buffer for our headers.
	 */
	headerdata = isc_mem_get(httpdmgr->mctx, HTTP_SENDGROW);
	isc_buffer_init(&httpd->headerbuffer, headerdata, HTTP_SENDGROW);

	isc_buffer_initnull(&httpd->compbuffer);
	isc_buffer_initnull(&httpd->bodybuffer);
	reset_client(httpd);

	ISC_LINK_INIT(httpd, link);
	ISC_LIST_APPEND(httpdmgr->running, httpd, link);

	httpd->magic = HTTPD_MAGIC;

	*httpdp = httpd;
}

static void
isc_httpd_accept(isc_task_t *task, isc_event_t *ev) {
	isc_httpdmgr_t *httpdmgr = ev->ev_arg;
	isc_httpd_t *httpd = NULL;
	isc_region_t r;
	isc_socket_newconnev_t *nev = (isc_socket_newconnev_t *)ev;
	isc_sockaddr_t peeraddr;

	REQUIRE(VALID_HTTPDMGR(httpdmgr));

	LOCK(&httpdmgr->lock);
	if (MSHUTTINGDOWN(httpdmgr)) {
		goto out;
	}

	if (nev->result == ISC_R_CANCELED) {
		goto out;
	}

	if (nev->result != ISC_R_SUCCESS) {
		/* XXXMLG log failure */
		goto requeue;
	}

	(void)isc_socket_getpeername(nev->newsocket, &peeraddr);
	if (httpdmgr->client_ok != NULL &&
	    !(httpdmgr->client_ok)(&peeraddr, httpdmgr->cb_arg))
	{
		isc_socket_detach(&nev->newsocket);
		goto requeue;
	}

	isc_httpd_create(httpdmgr, nev->newsocket, &httpd);

	r.base = (unsigned char *)httpd->recvbuf;
	r.length = HTTP_RECVLEN - 1;

	httpd_socket_recv(httpd, &r, task);

requeue:
	(void)httpdmgr_socket_accept(task, httpdmgr);

out:
	UNLOCK(&httpdmgr->lock);

	if (httpd != NULL) {
		maybe_destroy_httpd(httpd);
	}
	maybe_destroy_httpdmgr(httpdmgr);

	isc_event_free(&ev);
}

static isc_result_t
render_404(const char *url, isc_httpdurl_t *urlinfo, const char *querystring,
	   const char *headers, void *arg, unsigned int *retcode,
	   const char **retmsg, const char **mimetype, isc_buffer_t *b,
	   isc_httpdfree_t **freecb, void **freecb_args) {
	static char msg[] = "No such URL.\r\n";

	UNUSED(url);
	UNUSED(urlinfo);
	UNUSED(querystring);
	UNUSED(headers);
	UNUSED(arg);

	*retcode = 404;
	*retmsg = "No such URL";
	*mimetype = "text/plain";
	isc_buffer_reinit(b, msg, strlen(msg));
	isc_buffer_add(b, strlen(msg));
	*freecb = NULL;
	*freecb_args = NULL;

	return (ISC_R_SUCCESS);
}

static isc_result_t
render_500(const char *url, isc_httpdurl_t *urlinfo, const char *querystring,
	   const char *headers, void *arg, unsigned int *retcode,
	   const char **retmsg, const char **mimetype, isc_buffer_t *b,
	   isc_httpdfree_t **freecb, void **freecb_args) {
	static char msg[] = "Internal server failure.\r\n";

	UNUSED(url);
	UNUSED(urlinfo);
	UNUSED(querystring);
	UNUSED(headers);
	UNUSED(arg);

	*retcode = 500;
	*retmsg = "Internal server failure";
	*mimetype = "text/plain";
	isc_buffer_reinit(b, msg, strlen(msg));
	isc_buffer_add(b, strlen(msg));
	*freecb = NULL;
	*freecb_args = NULL;

	return (ISC_R_SUCCESS);
}

#ifdef HAVE_ZLIB
/*%<
 * Reallocates compbuffer to size, does nothing if compbuffer is already
 * larger than size.
 *
 * Requires:
 *\li	httpd a valid isc_httpd_t object
 *
 * Returns:
 *\li	#ISC_R_SUCCESS		-- all is well.
 *\li	#ISC_R_NOMEMORY		-- not enough memory to extend buffer
 */
static isc_result_t
alloc_compspace(isc_httpd_t *httpd, unsigned int size) {
	char *newspace;
	isc_region_t r;

	isc_buffer_region(&httpd->compbuffer, &r);
	if (size < r.length) {
		return (ISC_R_SUCCESS);
	}

	newspace = isc_mem_get(httpd->mgr->mctx, size);
	isc_buffer_reinit(&httpd->compbuffer, newspace, size);

	if (r.base != NULL) {
		isc_mem_put(httpd->mgr->mctx, r.base, r.length);
	}

	return (ISC_R_SUCCESS);
}

/*%<
 * Tries to compress httpd->bodybuffer to httpd->compbuffer, extending it
 * if necessary.
 *
 * Requires:
 *\li	httpd a valid isc_httpd_t object
 *
 * Returns:
 *\li	#ISC_R_SUCCESS	  -- all is well.
 *\li	#ISC_R_NOMEMORY	  -- not enough memory to compress data
 *\li	#ISC_R_FAILURE	  -- error during compression or compressed
 *			     data would be larger than input data
 */
static isc_result_t
isc_httpd_compress(isc_httpd_t *httpd) {
	z_stream zstr;
	isc_region_t r;
	isc_result_t result;
	int ret;
	int inputlen;

	inputlen = isc_buffer_usedlength(&httpd->bodybuffer);
	result = alloc_compspace(httpd, inputlen);
	if (result != ISC_R_SUCCESS) {
		return (result);
	}
	isc_buffer_clear(&httpd->compbuffer);
	isc_buffer_region(&httpd->compbuffer, &r);

	/*
	 * We're setting output buffer size to input size so it fails if the
	 * compressed data size would be bigger than the input size.
	 */
	memset(&zstr, 0, sizeof(zstr));
	zstr.total_in = zstr.avail_in = zstr.total_out = zstr.avail_out =
		inputlen;

	zstr.next_in = isc_buffer_base(&httpd->bodybuffer);
	zstr.next_out = r.base;

	ret = deflateInit(&zstr, Z_DEFAULT_COMPRESSION);
	if (ret == Z_OK) {
		ret = deflate(&zstr, Z_FINISH);
	}
	deflateEnd(&zstr);
	if (ret == Z_STREAM_END) {
		isc_buffer_add(&httpd->compbuffer, inputlen - zstr.avail_out);
		return (ISC_R_SUCCESS);
	} else {
		return (ISC_R_FAILURE);
	}
}
#endif /* ifdef HAVE_ZLIB */

static void
isc_httpd_recvdone(isc_task_t *task, isc_event_t *ev) {
	isc_result_t result;
	isc_httpd_t *httpd = ev->ev_arg;
	isc_socketevent_t *sev = (isc_socketevent_t *)ev;
	isc_buffer_t *databuffer;
	isc_httpdurl_t *url;
	isc_time_t now;
	isc_region_t r;
	bool is_compressed = false;
	char datebuf[ISC_FORMATHTTPTIMESTAMP_SIZE];

	REQUIRE(VALID_HTTPD(httpd));

	INSIST(ISC_HTTPD_ISRECV(httpd));

	if (sev->result != ISC_R_SUCCESS) {
		goto out;
	}

	result = process_request(httpd, sev->n);
	if (result == ISC_R_NOTFOUND) {
		if (httpd->recvlen >= HTTP_RECVLEN - 1) {
			goto out;
		}
		r.base = (unsigned char *)httpd->recvbuf + httpd->recvlen;
		r.length = HTTP_RECVLEN - httpd->recvlen - 1;

		httpd_socket_recv(httpd, &r, task);
		goto out;
	} else if (result != ISC_R_SUCCESS) {
		goto out;
	}

	ISC_HTTPD_SETSEND(httpd);

	/*
	 * XXXMLG Call function here.  Provide an add-header function
	 * which will append the common headers to a response we generate.
	 */
	isc_buffer_initnull(&httpd->bodybuffer);
	isc_time_now(&now);
	isc_time_formathttptimestamp(&now, datebuf, sizeof(datebuf));
	LOCK(&httpd->mgr->lock);
	url = ISC_LIST_HEAD(httpd->mgr->urls);
	while (url != NULL) {
		if (strcmp(httpd->url, url->url) == 0) {
			break;
		}
		url = ISC_LIST_NEXT(url, link);
	}
	UNLOCK(&httpd->mgr->lock);

	if (url == NULL) {
		result = httpd->mgr->render_404(
			httpd->url, NULL, httpd->querystring, NULL, NULL,
			&httpd->retcode, &httpd->retmsg, &httpd->mimetype,
			&httpd->bodybuffer, &httpd->freecb, &httpd->freecb_arg);
	} else {
		result = url->action(httpd->url, url, httpd->querystring,
				     httpd->headers, url->action_arg,
				     &httpd->retcode, &httpd->retmsg,
				     &httpd->mimetype, &httpd->bodybuffer,
				     &httpd->freecb, &httpd->freecb_arg);
	}
	if (result != ISC_R_SUCCESS) {
		result = httpd->mgr->render_500(
			httpd->url, url, httpd->querystring, NULL, NULL,
			&httpd->retcode, &httpd->retmsg, &httpd->mimetype,
			&httpd->bodybuffer, &httpd->freecb, &httpd->freecb_arg);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
	}

#ifdef HAVE_ZLIB
	if ((httpd->flags & HTTPD_ACCEPT_DEFLATE) != 0) {
		result = isc_httpd_compress(httpd);
		if (result == ISC_R_SUCCESS) {
			is_compressed = true;
		}
	}
#endif /* ifdef HAVE_ZLIB */

	isc_httpd_response(httpd);
	if ((httpd->flags & HTTPD_KEEPALIVE) != 0) {
		isc_httpd_addheader(httpd, "Connection", "Keep-Alive");
	}
	isc_httpd_addheader(httpd, "Content-Type", httpd->mimetype);
	isc_httpd_addheader(httpd, "Date", datebuf);
	isc_httpd_addheader(httpd, "Expires", datebuf);

	if (url != NULL && url->isstatic) {
		char loadbuf[ISC_FORMATHTTPTIMESTAMP_SIZE];
		isc_time_formathttptimestamp(&url->loadtime, loadbuf,
					     sizeof(loadbuf));
		isc_httpd_addheader(httpd, "Last-Modified", loadbuf);
		isc_httpd_addheader(httpd, "Cache-Control: public", NULL);
	} else {
		isc_httpd_addheader(httpd, "Last-Modified", datebuf);
		isc_httpd_addheader(httpd, "Pragma: no-cache", NULL);
		isc_httpd_addheader(httpd, "Cache-Control: no-cache", NULL);
	}

	isc_httpd_addheader(httpd, "Server: libisc", NULL);

	if (is_compressed) {
		isc_httpd_addheader(httpd, "Content-Encoding", "deflate");
		isc_httpd_addheaderuint(
			httpd, "Content-Length",
			isc_buffer_usedlength(&httpd->compbuffer));
	} else {
		isc_httpd_addheaderuint(
			httpd, "Content-Length",
			isc_buffer_usedlength(&httpd->bodybuffer));
	}

	isc_httpd_endheaders(httpd); /* done */

	/*
	 * Append either the compressed or the non-compressed response body to
	 * the response headers and store the result in httpd->sendbuffer.
	 */
	isc_buffer_dup(httpd->mgr->mctx, &httpd->sendbuffer,
		       &httpd->headerbuffer);
	isc_buffer_setautorealloc(httpd->sendbuffer, true);
	databuffer = (is_compressed ? &httpd->compbuffer : &httpd->bodybuffer);
	isc_buffer_usedregion(databuffer, &r);
	result = isc_buffer_copyregion(httpd->sendbuffer, &r);
	RUNTIME_CHECK(result == ISC_R_SUCCESS);
	/*
	 * Determine total response size.
	 */
	isc_buffer_usedregion(httpd->sendbuffer, &r);

	httpd_socket_send(httpd, &r, task);

out:
	maybe_destroy_httpd(httpd);
	isc_event_free(&ev);
}

void
isc_httpdmgr_shutdown(isc_httpdmgr_t **httpdmgrp) {
	isc_httpdmgr_t *httpdmgr;
	isc_httpd_t *httpd;

	REQUIRE(httpdmgrp != NULL);
	httpdmgr = *httpdmgrp;
	*httpdmgrp = NULL;
	REQUIRE(VALID_HTTPDMGR(httpdmgr));

	LOCK(&httpdmgr->lock);

	MSETSHUTTINGDOWN(httpdmgr);

	isc_socket_cancel(httpdmgr->sock, httpdmgr->task, ISC_SOCKCANCEL_ALL);

	httpd = ISC_LIST_HEAD(httpdmgr->running);
	while (httpd != NULL) {
		isc_socket_cancel(httpd->sock, httpdmgr->task,
				  ISC_SOCKCANCEL_ALL);
		httpd = ISC_LIST_NEXT(httpd, link);
	}

	UNLOCK(&httpdmgr->lock);

	maybe_destroy_httpdmgr(httpdmgr);
}

static isc_result_t
grow_headerspace(isc_httpd_t *httpd) {
	char *newspace;
	unsigned int newlen;
	isc_region_t r;

	isc_buffer_region(&httpd->headerbuffer, &r);
	newlen = r.length + HTTP_SENDGROW;
	if (newlen > HTTP_SEND_MAXLEN) {
		return (ISC_R_NOSPACE);
	}

	newspace = isc_mem_get(httpd->mgr->mctx, newlen);

	isc_buffer_reinit(&httpd->headerbuffer, newspace, newlen);

	isc_mem_put(httpd->mgr->mctx, r.base, r.length);

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_httpd_response(isc_httpd_t *httpd) {
	isc_result_t result;
	unsigned int needlen;

	REQUIRE(VALID_HTTPD(httpd));

	needlen = strlen(httpd->protocol) + 1; /* protocol + space */
	needlen += 3 + 1; /* room for response code, always 3 bytes */
	needlen += strlen(httpd->retmsg) + 2; /* return msg + CRLF */

	while (isc_buffer_availablelength(&httpd->headerbuffer) < needlen) {
		result = grow_headerspace(httpd);
		if (result != ISC_R_SUCCESS) {
			return (result);
		}
	}

	return (isc_buffer_printf(&httpd->headerbuffer, "%s %03u %s\r\n",
				  httpd->protocol, httpd->retcode,
				  httpd->retmsg));
}

isc_result_t
isc_httpd_addheader(isc_httpd_t *httpd, const char *name, const char *val) {
	isc_result_t result;
	unsigned int needlen;

	REQUIRE(VALID_HTTPD(httpd));

	needlen = strlen(name); /* name itself */
	if (val != NULL) {
		needlen += 2 + strlen(val); /* :<space> and val */
	}
	needlen += 2; /* CRLF */

	while (isc_buffer_availablelength(&httpd->headerbuffer) < needlen) {
		result = grow_headerspace(httpd);
		if (result != ISC_R_SUCCESS) {
			return (result);
		}
	}

	if (val != NULL) {
		return (isc_buffer_printf(&httpd->headerbuffer, "%s: %s\r\n",
					  name, val));
	} else {
		return (isc_buffer_printf(&httpd->headerbuffer, "%s\r\n",
					  name));
	}
}

isc_result_t
isc_httpd_endheaders(isc_httpd_t *httpd) {
	isc_result_t result;

	REQUIRE(VALID_HTTPD(httpd));

	while (isc_buffer_availablelength(&httpd->headerbuffer) < 2) {
		result = grow_headerspace(httpd);
		if (result != ISC_R_SUCCESS) {
			return (result);
		}
	}

	return (isc_buffer_printf(&httpd->headerbuffer, "\r\n"));
}

isc_result_t
isc_httpd_addheaderuint(isc_httpd_t *httpd, const char *name, int val) {
	isc_result_t result;
	unsigned int needlen;
	char buf[sizeof "18446744073709551616"];

	REQUIRE(VALID_HTTPD(httpd));

	snprintf(buf, sizeof(buf), "%d", val);

	needlen = strlen(name);	    /* name itself */
	needlen += 2 + strlen(buf); /* :<space> and val */
	needlen += 2;		    /* CRLF */

	while (isc_buffer_availablelength(&httpd->headerbuffer) < needlen) {
		result = grow_headerspace(httpd);
		if (result != ISC_R_SUCCESS) {
			return (result);
		}
	}

	return (isc_buffer_printf(&httpd->headerbuffer, "%s: %s\r\n", name,
				  buf));
}

static void
isc_httpd_senddone(isc_task_t *task, isc_event_t *ev) {
	isc_httpd_t *httpd = ev->ev_arg;
	isc_region_t r;
	isc_socketevent_t *sev = (isc_socketevent_t *)ev;

	REQUIRE(VALID_HTTPD(httpd));

	INSIST(ISC_HTTPD_ISSEND(httpd));

	isc_buffer_free(&httpd->sendbuffer);

	/*
	 * We will always want to clean up our receive buffer, even if we
	 * got an error on send or we are shutting down.
	 *
	 * We will pass in the buffer only if there is data in it.  If
	 * there is no data, we will pass in a NULL.
	 */
	if (httpd->freecb != NULL) {
		isc_buffer_t *b = NULL;
		if (isc_buffer_length(&httpd->bodybuffer) > 0) {
			b = &httpd->bodybuffer;
			httpd->freecb(b, httpd->freecb_arg);
		}
	}

	if (sev->result != ISC_R_SUCCESS) {
		goto out;
	}

	if ((httpd->flags & HTTPD_CLOSE) != 0) {
		goto out;
	}

	ISC_HTTPD_SETRECV(httpd);

	reset_client(httpd);

	r.base = (unsigned char *)httpd->recvbuf;
	r.length = HTTP_RECVLEN - 1;

	httpd_socket_recv(httpd, &r, task);

out:
	maybe_destroy_httpd(httpd);
	isc_event_free(&ev);
}

static void
reset_client(isc_httpd_t *httpd) {
	/*
	 * Catch errors here.  We MUST be in RECV mode, and we MUST NOT have
	 * any outstanding buffers.  If we have buffers, we have a leak.
	 */
	INSIST(ISC_HTTPD_ISRECV(httpd));
	INSIST(!ISC_LINK_LINKED(&httpd->headerbuffer, link));
	INSIST(!ISC_LINK_LINKED(&httpd->bodybuffer, link));

	httpd->recvbuf[0] = 0;
	httpd->recvlen = 0;
	httpd->headers = NULL;
	httpd->method = ISC_HTTPD_METHODUNKNOWN;
	httpd->url = NULL;
	httpd->querystring = NULL;
	httpd->protocol = NULL;
	httpd->flags = 0;

	isc_buffer_clear(&httpd->headerbuffer);
	isc_buffer_clear(&httpd->compbuffer);
	isc_buffer_invalidate(&httpd->bodybuffer);
}

isc_result_t
isc_httpdmgr_addurl(isc_httpdmgr_t *httpdmgr, const char *url,
		    isc_httpdaction_t *func, void *arg) {
	/* REQUIRE(VALID_HTTPDMGR(httpdmgr)); Dummy function */

	return (isc_httpdmgr_addurl2(httpdmgr, url, false, func, arg));
}

isc_result_t
isc_httpdmgr_addurl2(isc_httpdmgr_t *httpdmgr, const char *url, bool isstatic,
		     isc_httpdaction_t *func, void *arg) {
	isc_httpdurl_t *item;

	REQUIRE(VALID_HTTPDMGR(httpdmgr));

	if (url == NULL) {
		httpdmgr->render_404 = func;
		return (ISC_R_SUCCESS);
	}

	item = isc_mem_get(httpdmgr->mctx, sizeof(isc_httpdurl_t));

	item->url = isc_mem_strdup(httpdmgr->mctx, url);

	item->action = func;
	item->action_arg = arg;
	item->isstatic = isstatic;
	isc_time_now(&item->loadtime);

	ISC_LINK_INIT(item, link);

	LOCK(&httpdmgr->lock);
	ISC_LIST_APPEND(httpdmgr->urls, item, link);
	UNLOCK(&httpdmgr->lock);

	return (ISC_R_SUCCESS);
}

void
isc_httpd_setfinishhook(void (*fn)(void)) {
#if ENABLE_AFL
	finishhook = fn;
#else  /* ENABLE_AFL */
	UNUSED(fn);
#endif /* ENABLE_AFL */
}
