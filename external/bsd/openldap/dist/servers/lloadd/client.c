/*	$NetBSD: client.c,v 1.3 2025/09/05 21:16:24 christos Exp $	*/

/* $OpenLDAP$ */
/* This work is part of OpenLDAP Software <http://www.openldap.org/>.
 *
 * Copyright 1998-2024 The OpenLDAP Foundation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted only as authorized by the OpenLDAP
 * Public License.
 *
 * A copy of this license is available in the file LICENSE in the
 * top-level directory of the distribution or, alternatively, at
 * <http://www.OpenLDAP.org/license.html>.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: client.c,v 1.3 2025/09/05 21:16:24 christos Exp $");

#include "portable.h"

#include <ac/socket.h>
#include <ac/errno.h>
#include <ac/string.h>
#include <ac/time.h>
#include <ac/unistd.h>

#include "lutil.h"
#include "lload.h"

long lload_client_max_pending = 0;

lload_c_head clients = LDAP_CIRCLEQ_HEAD_INITIALIZER( clients );

ldap_pvt_thread_mutex_t clients_mutex;

static void client_unlink( LloadConnection *upstream );

int
request_abandon( LloadConnection *c, LloadOperation *op )
{
    LloadOperation *request, needle = { .o_client_connid = c->c_connid };
    int rc = LDAP_SUCCESS;

    op->o_res = LLOAD_OP_COMPLETED;

    if ( ber_decode_int( &op->o_request, &needle.o_client_msgid ) ) {
        Debug( LDAP_DEBUG_STATS, "request_abandon: "
                "connid=%lu msgid=%d invalid integer sent in abandon request\n",
                c->c_connid, op->o_client_msgid );

        OPERATION_UNLINK(op);
        CONNECTION_LOCK_DESTROY(c);
        return -1;
    }

    CONNECTION_LOCK(c);
    request = ldap_tavl_find( c->c_ops, &needle, operation_client_cmp );
    if ( !request ) {
        Debug( LDAP_DEBUG_STATS, "request_abandon: "
                "connid=%lu msgid=%d requests abandon of an operation "
                "msgid=%d not being processed anymore\n",
                c->c_connid, op->o_client_msgid, needle.o_client_msgid );
        CONNECTION_UNLOCK(c);
        goto done;
    } else if ( request->o_tag == LDAP_REQ_BIND ) {
        /* RFC 4511 states we must not allow Abandon on Binds */
        Debug( LDAP_DEBUG_STATS, "request_abandon: "
                "connid=%lu msgid=%d requests abandon of a bind operation "
                "msgid=%d\n",
                c->c_connid, op->o_client_msgid, needle.o_client_msgid );
        CONNECTION_UNLOCK(c);
        goto done;
    }
    Debug( LDAP_DEBUG_STATS, "request_abandon: "
            "connid=%lu msgid=%d abandoning %s msgid=%d\n",
            c->c_connid, op->o_client_msgid,
            lload_msgtype2str( request->o_tag ), needle.o_client_msgid );

    if ( c->c_state == LLOAD_C_BINDING ) {
        assert(0);
    }

    CONNECTION_UNLOCK(c);
    operation_abandon( request );

done:
    OPERATION_UNLINK(op);
    return rc;
}

int
request_process( LloadConnection *client, LloadOperation *op )
{
    BerElement *output;
    LloadConnection *upstream = NULL;
    LloadBackend *b = NULL;
    ber_int_t msgid;
    int res = LDAP_UNAVAILABLE, rc = LDAP_SUCCESS;
    char *message = "no connections available";
    enum op_restriction client_restricted;

    if ( lload_control_actions && !BER_BVISNULL( &op->o_ctrls ) ) {
        BerElementBuffer copy_berbuf;
        BerElement *copy = (BerElement *)&copy_berbuf;
        struct berval control;

        ber_init2( copy, &op->o_ctrls, 0 );

        while ( ber_skip_element( copy, &control ) == LBER_SEQUENCE ) {
            struct restriction_entry *entry, needle = {};
            BerElementBuffer control_berbuf;
            BerElement *control_ber = (BerElement *)&control_berbuf;

            ber_init2( control_ber, &control, 0 );

            if ( ber_skip_element( control_ber, &needle.oid ) == LBER_ERROR ) {
                res = LDAP_PROTOCOL_ERROR;
                message = "invalid control";

                operation_send_reject( op, res, message, 1 );
                goto fail;
            }

            entry = ldap_tavl_find(
                    lload_control_actions, &needle, lload_restriction_cmp );
            if ( entry && op->o_restricted < entry->action ) {
                op->o_restricted = entry->action;
            }
        }
    }
    if ( op->o_restricted < LLOAD_OP_RESTRICTED_WRITE &&
            lload_write_coherence &&
            op->o_tag != LDAP_REQ_SEARCH &&
            op->o_tag != LDAP_REQ_COMPARE ) {
        op->o_restricted = LLOAD_OP_RESTRICTED_WRITE;
    }

    if ( op->o_restricted == LLOAD_OP_RESTRICTED_REJECT ) {
        res = LDAP_UNWILLING_TO_PERFORM;
        message = "extended operation or control disallowed";

        operation_send_reject( op, res, message, 1 );
        goto fail;
    }

    CONNECTION_LOCK(client);
    client_restricted = client->c_restricted;
    if ( client_restricted ) {
        if ( client_restricted == LLOAD_OP_RESTRICTED_WRITE &&
                client->c_restricted_inflight == 0 &&
                client->c_restricted_at >= 0 &&
                client->c_restricted_at + lload_write_coherence <
                    op->o_start.tv_sec ) {
            Debug( LDAP_DEBUG_TRACE, "request_process: "
                    "connid=%lu write coherence to backend '%s' expired\n",
                    client->c_connid, client->c_backend->b_name.bv_val );
            client->c_backend = NULL;
            client_restricted = client->c_restricted = LLOAD_OP_NOT_RESTRICTED;
        }
        switch ( client_restricted ) {
            case LLOAD_OP_NOT_RESTRICTED:
                break;
            case LLOAD_OP_RESTRICTED_WRITE:
            case LLOAD_OP_RESTRICTED_BACKEND:
                b = client->c_backend;
                assert( b );
                break;
            case LLOAD_OP_RESTRICTED_UPSTREAM:
            case LLOAD_OP_RESTRICTED_ISOLATE:
                upstream = client->c_linked_upstream;
                assert( upstream );
                break;
            default:
                assert(0);
                break;
        }
    }
    if ( op->o_restricted < client_restricted ) {
        op->o_restricted = client_restricted;
    }
    CONNECTION_UNLOCK(client);

    if ( upstream ) {
        b = upstream->c_backend;
        checked_lock( &b->b_mutex );
        if ( !try_upstream( b, NULL, op, upstream, &res, &message ) ) {
            upstream = NULL;
        }
        checked_unlock( &b->b_mutex );
    } else if ( b ) {
        backend_select( b, op, &upstream, &res, &message );
    } else {
        upstream_select( op, &upstream, &res, &message );
    }

    if ( !upstream ) {
        Debug( LDAP_DEBUG_STATS, "request_process: "
                "connid=%lu, msgid=%d no available connection found\n",
                op->o_client_connid, op->o_client_msgid );

        operation_send_reject( op, res, message, 1 );
        goto fail;
    }
    CONNECTION_ASSERT_LOCKED(upstream);
    assert_locked( &upstream->c_io_mutex );
    op->o_upstream = upstream;
    op->o_upstream_connid = upstream->c_connid;
    op->o_res = LLOAD_OP_FAILED;

    /* Was it unlinked in the meantime? No need to send a response since the
     * client is dead */
    if ( !IS_ALIVE( op, o_refcnt ) ) {
        LloadBackend *b = upstream->c_backend;

        upstream->c_n_ops_executing--;
        checked_unlock( &upstream->c_io_mutex );
        CONNECTION_UNLOCK(upstream);

        checked_lock( &b->b_mutex );
        b->b_n_ops_executing--;
        checked_unlock( &b->b_mutex );

        assert( !IS_ALIVE( client, c_live ) );
        checked_lock( &op->o_link_mutex );
        if ( op->o_upstream ) {
            op->o_upstream = NULL;
        }
        checked_unlock( &op->o_link_mutex );
        return -1;
    }

    output = upstream->c_pendingber;
    if ( output == NULL && (output = ber_alloc()) == NULL ) {
        LloadBackend *b = upstream->c_backend;

        upstream->c_n_ops_executing--;
        CONNECTION_UNLOCK(upstream);
        checked_unlock( &upstream->c_io_mutex );

        checked_lock( &b->b_mutex );
        b->b_n_ops_executing--;
        operation_update_backend_counters( op, b );
        checked_unlock( &b->b_mutex );

        Debug( LDAP_DEBUG_ANY, "request_process: "
                "ber_alloc failed\n" );

        rc = -1;
        goto fail;
    }
    upstream->c_pendingber = output;

    if ( client_restricted < LLOAD_OP_RESTRICTED_UPSTREAM &&
            op->o_restricted >= LLOAD_OP_RESTRICTED_UPSTREAM ) {
        rc = ldap_tavl_insert(
                &upstream->c_linked, client, lload_upstream_entry_cmp,
                ldap_avl_dup_error );
        assert( rc == LDAP_SUCCESS );
    }

    op->o_upstream_msgid = msgid = upstream->c_next_msgid++;
    rc = ldap_tavl_insert(
            &upstream->c_ops, op, operation_upstream_cmp, ldap_avl_dup_error );

    CONNECTION_UNLOCK(upstream);

    Debug( LDAP_DEBUG_TRACE, "request_process: "
            "client connid=%lu added %s msgid=%d to upstream connid=%lu as "
            "msgid=%d\n",
            op->o_client_connid, lload_msgtype2str( op->o_tag ),
            op->o_client_msgid, op->o_upstream_connid, op->o_upstream_msgid );
    assert( rc == LDAP_SUCCESS );

    lload_stats.counters[LLOAD_STATS_OPS_OTHER].lc_ops_forwarded++;

    if ( op->o_restricted > client_restricted ||
            client_restricted == LLOAD_OP_RESTRICTED_WRITE ) {
        CONNECTION_LOCK(client);
        if ( op->o_restricted > client_restricted ) {
            client->c_restricted = op->o_restricted;
        }
        if ( op->o_restricted == LLOAD_OP_RESTRICTED_WRITE ) {
            client->c_restricted_inflight++;
        }
        if ( op->o_restricted >= LLOAD_OP_RESTRICTED_UPSTREAM ) {
            if ( client_restricted < LLOAD_OP_RESTRICTED_UPSTREAM ) {
                client->c_linked_upstream = upstream;
            }
            assert( client->c_linked_upstream == upstream );
            client->c_backend = NULL;
        } else if ( op->o_restricted >= LLOAD_OP_RESTRICTED_WRITE ) {
            if ( client_restricted < LLOAD_OP_RESTRICTED_WRITE ) {
                client->c_backend = upstream->c_backend;
            }
            assert( client->c_backend == upstream->c_backend );
        }
        CONNECTION_UNLOCK(client);
    }

    if ( (lload_features & LLOAD_FEATURE_PROXYAUTHZ) &&
            client->c_type != LLOAD_C_PRIVILEGED ) {
        CONNECTION_LOCK(client);
        Debug( LDAP_DEBUG_TRACE, "request_process: "
                "proxying identity %s to upstream\n",
                client->c_auth.bv_val );
        ber_printf( output, "t{titOt{{sbO}" /* "}}" */, LDAP_TAG_MESSAGE,
                LDAP_TAG_MSGID, msgid,
                op->o_tag, &op->o_request,
                LDAP_TAG_CONTROLS,
                LDAP_CONTROL_PROXY_AUTHZ, 1, &client->c_auth );
        CONNECTION_UNLOCK(client);

        if ( !BER_BVISNULL( &op->o_ctrls ) ) {
            ber_write( output, op->o_ctrls.bv_val, op->o_ctrls.bv_len, 0 );
        }

        ber_printf( output, /* "{{" */ "}}" );
    } else {
        ber_printf( output, "t{titOtO}", LDAP_TAG_MESSAGE,
                LDAP_TAG_MSGID, msgid,
                op->o_tag, &op->o_request,
                LDAP_TAG_CONTROLS, BER_BV_OPTIONAL( &op->o_ctrls ) );
    }
    checked_unlock( &upstream->c_io_mutex );

    connection_write_cb( -1, 0, upstream );
    return rc;

fail:
    if ( upstream ) {
        CONNECTION_LOCK_DESTROY(upstream);

        /* We have not committed any restrictions in the end */
        op->o_restricted = LLOAD_OP_NOT_RESTRICTED;
        operation_send_reject( op, LDAP_OTHER, "internal error", 0 );
    }

    OPERATION_UNLINK(op);
    if ( rc ) {
        CONNECTION_LOCK_DESTROY(client);
    }
    return rc;
}

int
handle_one_request( LloadConnection *c )
{
    BerElement *ber;
    LloadOperation *op = NULL;
    RequestHandler handler = NULL;
    int over_limit = 0;
    enum sc_state state;
    enum sc_io_state io_state;

    ber = c->c_currentber;
    c->c_currentber = NULL;

    CONNECTION_LOCK(c);
    op = operation_init( c, ber );
    if ( !op ) {
        Debug( LDAP_DEBUG_ANY, "handle_one_request: "
                "connid=%lu, operation_init failed\n",
                c->c_connid );
        CONNECTION_DESTROY(c);
        ber_free( ber, 1 );
        return -1;
    }
    if ( lload_client_max_pending &&
            c->c_n_ops_executing >= lload_client_max_pending ) {
        over_limit = 1;
    }

    /*
     * Remember the current state so we don't have to lock again,
     * we're only screening whether we can keep going, e.g. noone can change
     * state to LLOAD_C_BINDING from under us (would imply a new operation was
     * received but that's us), but the opposite is possible - a Bind response
     * could be received and processed in the meantime.
     */
    state = c->c_state;
    CONNECTION_UNLOCK(c);

    switch ( op->o_tag ) {
        case LDAP_REQ_UNBIND:
            /* There is never a response for this operation */
            op->o_res = LLOAD_OP_COMPLETED;
            OPERATION_UNLINK(op);

            Debug( LDAP_DEBUG_STATS, "handle_one_request: "
                    "received unbind, closing client connid=%lu\n",
                    c->c_connid );
            CONNECTION_LOCK_DESTROY(c);
            return -1;
        case LDAP_REQ_BIND:
            handler = request_bind;
            break;
        case LDAP_REQ_ABANDON:
            /* We can't send a response to abandon requests even if a bind is
             * currently in progress */
            return request_abandon( c, op );
        case LDAP_REQ_EXTENDED:
        default:
            if ( state == LLOAD_C_BINDING ) {
                operation_send_reject(
                        op, LDAP_PROTOCOL_ERROR, "bind in progress", 0 );
                return LDAP_SUCCESS;
            }
            if ( over_limit ) {
                operation_send_reject( op, LDAP_BUSY,
                        "pending operation limit reached on this connection",
                        0 );
                return LDAP_SUCCESS;
            }

            checked_lock( &c->c_io_mutex );
            io_state = c->c_io_state;
            checked_unlock( &c->c_io_mutex );
            if ( io_state & LLOAD_C_READ_PAUSE ) {
                operation_send_reject( op, LDAP_BUSY,
                        "writing side backlogged, please keep reading", 0 );
                return LDAP_SUCCESS;
            }

            if ( op->o_tag == LDAP_REQ_EXTENDED ) {
                handler = request_extended;
            } else {
                handler = request_process;
            }
            break;
    }

    if ( state == LLOAD_C_CLOSING ) {
        operation_send_reject(
                op, LDAP_UNAVAILABLE, "connection is shutting down", 0 );
        return LDAP_SUCCESS;
    }

    return handler( c, op );
}

#ifdef HAVE_TLS
/*
 * The connection has a token assigned to it when the callback is set up.
 */
void
client_tls_handshake_cb( evutil_socket_t s, short what, void *arg )
{
    LloadConnection *c = arg;
    epoch_t epoch;
    int rc = 0;

    if ( what & EV_TIMEOUT ) {
        Debug( LDAP_DEBUG_CONNS, "client_tls_handshake_cb: "
                "connid=%lu, timeout reached, destroying\n",
                c->c_connid );
        goto fail;
    }

    /*
     * In case of StartTLS, make sure we flush the response first.
     * Also before we try to read anything from the connection, it isn't
     * permitted to Abandon a StartTLS exop per RFC4511 anyway.
     */
    checked_lock( &c->c_io_mutex );
    if ( c->c_pendingber ) {
        checked_unlock( &c->c_io_mutex );
        connection_write_cb( s, what, arg );

        if ( !IS_ALIVE( c, c_live ) ) {
            goto fail;
        }

        /* Do we still have data pending? If so, connection_write_cb would
         * already have arranged the write callback to trigger again */
        checked_lock( &c->c_io_mutex );
        if ( c->c_pendingber ) {
            checked_unlock( &c->c_io_mutex );
            return;
        }
    }

    rc = ldap_pvt_tls_accept( c->c_sb, LLOAD_TLS_CTX );
    checked_unlock( &c->c_io_mutex );
    if ( rc < 0 ) {
        goto fail;
    }

    if ( rc == 0 ) {
        struct event_base *base = event_get_base( c->c_read_event );

        /*
         * We're finished, replace the callbacks
         *
         * This is deadlock-safe, since both share the same base - the one
         * that's just running us.
         */
        CONNECTION_LOCK(c);
        event_del( c->c_read_event );
        event_del( c->c_write_event );

        c->c_read_timeout = NULL;
        event_assign( c->c_read_event, base, c->c_fd, EV_READ|EV_PERSIST,
                connection_read_cb, c );
        if ( IS_ALIVE( c, c_live ) ) {
            event_add( c->c_read_event, c->c_read_timeout );
        }

        event_assign( c->c_write_event, base, c->c_fd, EV_WRITE,
                connection_write_cb, c );
        Debug( LDAP_DEBUG_CONNS, "client_tls_handshake_cb: "
                "connid=%lu finished\n",
                c->c_connid );

        c->c_is_tls = LLOAD_TLS_ESTABLISHED;
        CONNECTION_UNLOCK(c);
        return;
    } else if ( ber_sockbuf_ctrl( c->c_sb, LBER_SB_OPT_NEEDS_WRITE, NULL ) ) {
        if ( IS_ALIVE( c, c_live ) ) {
            CONNECTION_LOCK(c);
            event_add( c->c_write_event, lload_write_timeout );
            CONNECTION_UNLOCK(c);
        }
        Debug( LDAP_DEBUG_CONNS, "client_tls_handshake_cb: "
                "connid=%lu need write rc=%d\n",
                c->c_connid, rc );
    }
    return;

fail:
    Debug( LDAP_DEBUG_CONNS, "client_tls_handshake_cb: "
            "connid=%lu failed rc=%d\n",
            c->c_connid, rc );

    assert( c->c_ops == NULL );
    epoch = epoch_join();
    CONNECTION_LOCK_DESTROY(c);
    epoch_leave( epoch );
}
#endif /* HAVE_TLS */

LloadConnection *
client_init(
        ber_socket_t s,
        const char *peername,
        struct event_base *base,
        int flags )
{
    LloadConnection *c;
    struct event *event;
    event_callback_fn read_cb = connection_read_cb,
                      write_cb = connection_write_cb;

    if ( (c = lload_connection_init( s, peername, flags) ) == NULL ) {
        return NULL;
    }

    {
        ber_len_t max = sockbuf_max_incoming_client;
        ber_sockbuf_ctrl( c->c_sb, LBER_SB_OPT_SET_MAX_INCOMING, &max );
    }

    c->c_state = LLOAD_C_READY;

    if ( flags & CONN_IS_TLS ) {
#ifdef HAVE_TLS
        int rc;

        c->c_is_tls = LLOAD_LDAPS;

        rc = ldap_pvt_tls_accept( c->c_sb, LLOAD_TLS_CTX );
        if ( rc < 0 ) {
            Debug( LDAP_DEBUG_CONNS, "client_init: "
                    "connid=%lu failed initial TLS accept rc=%d\n",
                    c->c_connid, rc );
            CONNECTION_LOCK(c);
            goto fail;
        }

        if ( rc ) {
            c->c_read_timeout = lload_timeout_net;
            read_cb = write_cb = client_tls_handshake_cb;
        }
#else /* ! HAVE_TLS */
        assert(0);
#endif /* ! HAVE_TLS */
    }

    event = event_new( base, s, EV_READ|EV_PERSIST, read_cb, c );
    if ( !event ) {
        Debug( LDAP_DEBUG_ANY, "client_init: "
                "Read event could not be allocated\n" );
        CONNECTION_LOCK(c);
        goto fail;
    }
    c->c_read_event = event;

    event = event_new( base, s, EV_WRITE, write_cb, c );
    if ( !event ) {
        Debug( LDAP_DEBUG_ANY, "client_init: "
                "Write event could not be allocated\n" );
        CONNECTION_LOCK(c);
        goto fail;
    }
    c->c_write_event = event;

    CONNECTION_LOCK(c);
#ifdef BALANCER_MODULE
    if ( lload_monitor_client_subsys ) {
        acquire_ref( &c->c_refcnt );
        CONNECTION_UNLOCK(c);
        if ( lload_monitor_conn_entry_create(
                    c, lload_monitor_client_subsys ) ) {
            CONNECTION_LOCK(c);
            RELEASE_REF( c, c_refcnt, c->c_destroy );
            goto fail;
        }
        CONNECTION_LOCK(c);
        RELEASE_REF( c, c_refcnt, c->c_destroy );
    }
#endif /* BALANCER_MODULE */

    c->c_destroy = client_destroy;
    c->c_unlink = client_unlink;
    c->c_pdu_cb = handle_one_request;

    /* We only register the write event when we have data pending */
    event_add( c->c_read_event, c->c_read_timeout );

    checked_lock( &clients_mutex );
    LDAP_CIRCLEQ_INSERT_TAIL( &clients, c, c_next );
    checked_unlock( &clients_mutex );
    CONNECTION_UNLOCK(c);

    return c;
fail:
    if ( !IS_ALIVE( c, c_live ) ) {
        /*
         * Released while we were unlocked, it's scheduled for destruction
         * already
         */
        return NULL;
    }

    if ( c->c_write_event ) {
        event_free( c->c_write_event );
        c->c_write_event = NULL;
    }
    if ( c->c_read_event ) {
        event_free( c->c_read_event );
        c->c_read_event = NULL;
    }

    c->c_state = LLOAD_C_INVALID;
    c->c_live--;
    c->c_refcnt--;
    connection_destroy( c );
    return NULL;
}

void
client_reset( LloadConnection *c )
{
    TAvlnode *root;
    long freed = 0, executing;
    LloadConnection *linked_upstream = NULL;
    enum op_restriction restricted = c->c_restricted;

    CONNECTION_ASSERT_LOCKED(c);
    root = c->c_ops;
    c->c_ops = NULL;
    executing = c->c_n_ops_executing;
    c->c_n_ops_executing = 0;

    if ( !BER_BVISNULL( &c->c_auth ) ) {
        ch_free( c->c_auth.bv_val );
        BER_BVZERO( &c->c_auth );
    }
    if ( !BER_BVISNULL( &c->c_sasl_bind_mech ) ) {
        ch_free( c->c_sasl_bind_mech.bv_val );
        BER_BVZERO( &c->c_sasl_bind_mech );
    }

    if ( restricted && restricted < LLOAD_OP_RESTRICTED_ISOLATE ) {
        if ( c->c_backend ) {
            assert( c->c_restricted <= LLOAD_OP_RESTRICTED_BACKEND );
            assert( c->c_restricted_inflight == 0 );
            c->c_backend = NULL;
            c->c_restricted_at = 0;
        } else {
            assert( c->c_restricted == LLOAD_OP_RESTRICTED_UPSTREAM );
            assert( c->c_linked_upstream != NULL );
            linked_upstream = c->c_linked_upstream;
            c->c_linked_upstream = NULL;
        }
    }
    CONNECTION_UNLOCK(c);

    if ( root ) {
        freed = ldap_tavl_free( root, (AVL_FREE)operation_abandon );
        Debug( LDAP_DEBUG_TRACE, "client_reset: "
                "dropped %ld operations\n",
                freed );
    }
    assert( freed == executing );

    if ( linked_upstream && restricted == LLOAD_OP_RESTRICTED_UPSTREAM ) {
        LloadConnection *removed = ldap_tavl_delete(
                &linked_upstream->c_linked, c, lload_upstream_entry_cmp );
        assert( removed == c );
    }

    CONNECTION_LOCK(c);
    CONNECTION_ASSERT_LOCKED(c);
}

void
client_unlink( LloadConnection *c )
{
    enum sc_state state;
    struct event *read_event, *write_event;

    Debug( LDAP_DEBUG_CONNS, "client_unlink: "
            "removing client connid=%lu\n",
            c->c_connid );

    CONNECTION_ASSERT_LOCKED(c);
    assert( c->c_state != LLOAD_C_INVALID );
    assert( c->c_state != LLOAD_C_DYING );

    state = c->c_state;
    c->c_state = LLOAD_C_DYING;

    if ( c->c_restricted == LLOAD_OP_RESTRICTED_ISOLATE ) {
        /* Allow upstream connection to be severed in client_reset() */
        c->c_restricted = LLOAD_OP_RESTRICTED_UPSTREAM;
    }

    read_event = c->c_read_event;
    write_event = c->c_write_event;
    CONNECTION_UNLOCK(c);

    if ( read_event ) {
        event_del( read_event );
    }

    if ( write_event ) {
        event_del( write_event );
    }

    if ( state != LLOAD_C_DYING ) {
        checked_lock( &clients_mutex );
        LDAP_CIRCLEQ_REMOVE( &clients, c, c_next );
        checked_unlock( &clients_mutex );
    }

    CONNECTION_LOCK(c);
    client_reset( c );
    CONNECTION_ASSERT_LOCKED(c);
}

void
client_destroy( LloadConnection *c )
{
    Debug( LDAP_DEBUG_CONNS, "client_destroy: "
            "destroying client connid=%lu\n",
            c->c_connid );

    CONNECTION_LOCK(c);
    assert( c->c_state == LLOAD_C_DYING );

#ifdef BALANCER_MODULE
    /*
     * Can't do this in client_unlink as that could be run from cn=monitor
     * modify callback.
     */
    if ( !BER_BVISNULL( &c->c_monitor_dn ) ) {
        lload_monitor_conn_unlink( c );
    }
#endif /* BALANCER_MODULE */

    c->c_state = LLOAD_C_INVALID;

    assert( c->c_ops == NULL );

    if ( c->c_read_event ) {
        event_free( c->c_read_event );
        c->c_read_event = NULL;
    }

    if ( c->c_write_event ) {
        event_free( c->c_write_event );
        c->c_write_event = NULL;
    }

    assert( c->c_refcnt == 0 );
    connection_destroy( c );
}

void
clients_destroy( int gentle )
{
    epoch_t epoch = epoch_join();
    checked_lock( &clients_mutex );
    connections_walk(
            &clients_mutex, &clients, lload_connection_close, &gentle );
    checked_unlock( &clients_mutex );
    epoch_leave( epoch );
}
