/*	$NetBSD: showq_json.c,v 1.5 2025/02/25 19:15:48 christos Exp $	*/

/*++
/* NAME
/*	showq_json 8
/* SUMMARY
/*	JSON queue status formatter
/* SYNOPSIS
/*	void	showq_json(
/*	VSTREAM	*showq)
/* DESCRIPTION
/*	This function converts showq(8) daemon output to JSON format.
/* DIAGNOSTICS
/*	Fatal errors: out of memory, malformed showq(8) daemon output.
/* LICENSE
/* .ad
/* .fi
/*	The Secure Mailer license must be distributed with this software.
/* AUTHOR(S)
/*	Wietse Venema
/*	IBM T.J. Watson Research
/*	P.O. Box 704
/*	Yorktown Heights, NY 10598, USA
/*
/*	Wietse Venema
/*	Google, Inc.
/*	111 8th Avenue
/*	New York, NY 10011, USA
/*--*/

/* System library. */

#include <sys_defs.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sysexits.h>
#include <ctype.h>
#include <errno.h>

/* Utility library. */

#include <vstring.h>
#include <vstream.h>
#include <stringops.h>
#include <mymalloc.h>
#include <msg.h>

/* Global library. */

#include <mail_proto.h>
#include <mail_queue.h>
#include <mail_date.h>
#include <mail_params.h>

/* Application-specific. */

#include <postqueue.h>

#define STR(x)	vstring_str(x)
#define LEN(x)	VSTRING_LEN(x)

/* json_message - report status for one message */

static void format_json(VSTREAM *showq_stream)
{
    static VSTRING *queue_name = 0;
    static VSTRING *queue_id = 0;
    static VSTRING *addr = 0;
    static VSTRING *why = 0;
    static VSTRING *quote_buf = 0;
    long    arrival_time;
    long    message_size;
    int     showq_status;
    int     rcpt_count = 0;
    int     forced_expire;

    /*
     * One-time initialization.
     */
    if (queue_name == 0) {
	queue_name = vstring_alloc(100);
	queue_id = vstring_alloc(100);
	addr = vstring_alloc(100);
	why = vstring_alloc(100);
	quote_buf = vstring_alloc(100);
    }

    /*
     * Force JSON values to UTF-8 (with SMTPUTF8 enabled) or ASCII (with
     * SMTPUTF8 disabled).
     */
#define QUOTE_JSON(res, src) printable(quote_for_json((res), (src), -1), '?')

    /*
     * Read the message properties and sender address.
     */
    if (attr_scan(showq_stream, ATTR_FLAG_MORE | ATTR_FLAG_STRICT
		  | ATTR_FLAG_PRINTABLE,
		  RECV_ATTR_STR(MAIL_ATTR_QUEUE, queue_name),
		  RECV_ATTR_STR(MAIL_ATTR_QUEUEID, queue_id),
		  RECV_ATTR_LONG(MAIL_ATTR_TIME, &arrival_time),
		  RECV_ATTR_LONG(MAIL_ATTR_SIZE, &message_size),
		  RECV_ATTR_INT(MAIL_ATTR_FORCED_EXPIRE, &forced_expire),
		  RECV_ATTR_STR(MAIL_ATTR_SENDER, addr),
		  ATTR_TYPE_END) != 6)
	msg_fatal_status(EX_SOFTWARE, "malformed showq server response");
    vstream_printf("{");
    vstream_printf("\"queue_name\": \"%s\", ",
		   QUOTE_JSON(quote_buf, STR(queue_name)));
    vstream_printf("\"queue_id\": \"%s\", ",
		   QUOTE_JSON(quote_buf, STR(queue_id)));
    vstream_printf("\"arrival_time\": %ld, ", arrival_time);
    vstream_printf("\"message_size\": %ld, ", message_size);
    vstream_printf("\"forced_expire\": %s, ", forced_expire ? "true" : "false");
    vstream_printf("\"sender\": \"%s\", ",
		   QUOTE_JSON(quote_buf, STR(addr)));

    /*
     * Read zero or more (recipient, reason) pair(s) until attr_scan_more()
     * consumes a terminator. If the showq daemon messes up, don't try to
     * resynchronize.
     */
    vstream_printf("\"recipients\": [");
    for (rcpt_count = 0; (showq_status = attr_scan_more(showq_stream)) > 0; rcpt_count++) {
	if (rcpt_count > 0)
	    vstream_printf(", ");
	vstream_printf("{");
	if (attr_scan(showq_stream, ATTR_FLAG_MORE | ATTR_FLAG_STRICT
		      | ATTR_FLAG_PRINTABLE,
		      RECV_ATTR_STR(MAIL_ATTR_RECIP, addr),
		      RECV_ATTR_STR(MAIL_ATTR_WHY, why),
		      ATTR_TYPE_END) != 2)
	    msg_fatal_status(EX_SOFTWARE, "malformed showq server response");
	vstream_printf("\"address\": \"%s\"",
		       QUOTE_JSON(quote_buf, STR(addr)));
	if (LEN(why) > 0)
	    vstream_printf(", \"delay_reason\": \"%s\"",
			   QUOTE_JSON(quote_buf, STR(why)));
	vstream_printf("}");
    }
    vstream_printf("]");
    if (showq_status < 0)
	msg_fatal_status(EX_SOFTWARE, "malformed showq server response");
    vstream_printf("}\n");
    if (vstream_fflush(VSTREAM_OUT) && errno != EPIPE)
	msg_fatal_status(EX_IOERR, "output write error: %m");
}

/* showq_json - streaming JSON-format output adapter */

void    showq_json(VSTREAM *showq_stream)
{
    int     showq_status;

    /*
     * Emit zero or more queue file objects until attr_scan_more() consumes a
     * terminator.
     */
    while ((showq_status = attr_scan_more(showq_stream)) > 0
	   && vstream_ferror(VSTREAM_OUT) == 0) {
	format_json(showq_stream);
    }
    if (showq_status < 0)
	msg_fatal_status(EX_SOFTWARE, "malformed showq server response");
}
