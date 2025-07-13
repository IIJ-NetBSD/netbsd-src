/*	$NetBSD: netgroup_mkdb.c,v 1.19 2025/07/13 12:34:10 rillig Exp $	*/

/*
 * Copyright (c) 1994 Christos Zoulas
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/cdefs.h>
#ifndef lint
__RCSID("$NetBSD: netgroup_mkdb.c,v 1.19 2025/07/13 12:34:10 rillig Exp $");
#endif

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <db.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stringlist.h>
#define _NETGROUP_PRIVATE
#include <netgroup.h>
#include <assert.h>

#include "str.h"
#include "util.h"

#define DEBUG_NG

#define NEW(a)	(a *) emalloc(sizeof(a))

struct nentry {
	int             n_type;
	size_t          n_size;	/* Buffer size required for printing */
	union {
		char            *_name;
		struct netgroup *_group;
	} _n;
#define n_name	_n._name
#define n_group _n._group
	struct nentry  *n_next;
};


static	void	 cleanup(void);
static	DB      *ng_insert(DB *, const char *);
static	void	 ng_reventry(DB *, DB *, struct nentry *, char *,
    size_t, StringList *);
static	void	 ng_print(struct nentry *, struct string *);
static	void	 ng_rprint(DB *, struct string *);
static	DB	*ng_reverse(DB *, size_t);
static	DB	*ng_load(const char *);
static	void	 ng_write(DB *, DB *, int);
static	void	 ng_rwrite(DB *, DB *, int);
static	void	 usage(void) __dead;

#ifdef DEBUG_NG
static	int 	 debug = 0;
static	void	 ng_dump(DB *);
static	void	 ng_rdump(DB *);
#endif /* DEBUG_NG */
static	int	 dups = 0;


static const char ng_empty[] = "";
#define NG_EMPTY(a)	((a) ? (a) : ng_empty)

static const char *dbname = _PATH_NETGROUP_DB;

int
main(int argc, char **argv)
{
	DB		 *db, *ndb, *hdb, *udb;
	int               ch;
	char		  buf[MAXPATHLEN];
	const char	 *fname = _PATH_NETGROUP;


	while ((ch = getopt(argc, argv, "dDo:")) != -1)
		switch (ch) {
#ifdef DEBUG_NG
		case 'd':
			debug++;
			break;
#endif
		case 'o':
			dbname = optarg;
			break;

		case 'D':
			dups++;
			break;

		case '?':
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc == 1)
		fname = *argv;
	else if (argc > 1)
		usage();

	if (atexit(cleanup))
		err(1, "Cannot install exit handler");

	/* Read and parse the netgroup file */
	ndb = ng_load(fname);
#ifdef DEBUG_NG
	if (debug) {
		(void)fprintf(stderr, "#### Database\n");
		ng_dump(ndb);
	}
#endif

	/* Reverse the database by host */
	hdb = ng_reverse(ndb, offsetof(struct netgroup, ng_host));
#ifdef DEBUG_NG
	if (debug) {
		(void)fprintf(stderr, "#### Reverse by host\n");
		ng_rdump(hdb);
	}
#endif

	/* Reverse the database by user */
	udb = ng_reverse(ndb, offsetof(struct netgroup, ng_user));
#ifdef DEBUG_NG
	if (debug) {
		(void)fprintf(stderr, "#### Reverse by user\n");
		ng_rdump(udb);
	}
#endif

	(void)snprintf(buf, sizeof(buf), "%s.tmp", dbname);

	db = dbopen(buf, O_RDWR | O_CREAT | O_EXCL,
	    (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH), DB_HASH, NULL);
	if (!db)
		err(1, "%s", buf);

	ng_write(db, ndb, _NG_KEYBYNAME);
	ng_rwrite(db, udb, _NG_KEYBYUSER);
	ng_rwrite(db, hdb, _NG_KEYBYHOST);

	if ((db->close)(db))
		err(1, "Error closing database");

	if (rename(buf, dbname) == -1)
		err(1, "Cannot rename `%s' to `%s'", buf, dbname);

	return 0;
}


/*
 * cleanup(): Remove temporary files upon exit
 */
static void
cleanup(void)
{
	char buf[MAXPATHLEN];
	(void)snprintf(buf, sizeof(buf), "%s.tmp", dbname);
	(void)unlink(buf);
}



/*
 * ng_load(): Load the netgroup database from a file
 */
static DB *
ng_load(const char *fname)
{
	FILE           *fp;
	DB             *db;
	char           *buf;
	size_t          size;
	struct nentry  *tail, *head, *e;
	char           *p, *name;
	struct netgroup *ng;
	DBT             data, key;

	/* Open the netgroup file */
	if ((fp = fopen(fname, "r")) == NULL)
		err(1, "%s", fname);

	db = dbopen(NULL, O_RDWR | O_CREAT | O_EXCL, 0, DB_HASH, NULL);

	if (db == NULL)
		err(1, "dbopen");

	while ((buf = fparseln(fp, &size, NULL, NULL, 0)) != NULL) {
		tail = head = NULL;
		p = buf;

		while (p != NULL) {
			switch (_ng_parse(&p, &name, &ng)) {
			case _NG_NONE:
				/* done with this one */
				p = NULL;
				free(buf);
				if (head == NULL)
					break;

				key.data = (u_char *)head->n_name;
				key.size = strlen(head->n_name) + 1;
				data.data = (u_char *)&head;
				data.size = sizeof(head);
				switch ((db->put)(db, &key, &data,
				    R_NOOVERWRITE)) {
				case 0:
					break;

				case 1:
					warnx("Duplicate entry netgroup `%s'",
					    head->n_name);
					break;

				case -1:
					err(1, "put");

				default:
					abort();
				}
				break;

			case _NG_NAME:
				e = NEW(struct nentry);
				e->n_type = _NG_NAME;
				e->n_name = name;
				e->n_next = NULL;
				e->n_size = size;
				if (tail == NULL)
					head = tail = e;
				else {
					tail->n_next = e;
					tail = e;
				}
				break;

			case _NG_GROUP:
				if (tail == NULL) {
					char fmt[BUFSIZ];
					_ng_print(fmt, sizeof(fmt), ng);
					errx(1, "no netgroup key for %s", fmt);
				}
				else {
					e = NEW(struct nentry);
					e->n_type = _NG_GROUP;
					e->n_group = ng;
					e->n_next = NULL;
					e->n_size = size;
					tail->n_next = e;
					tail = e;
				}
				break;

			case _NG_ERROR:
				errx(1, "Fatal error at `%s'", p);

			default:
				abort();
			}
		}
	}
	(void)fclose(fp);
	return db;
}


/*
 * ng_insert(): Insert named key into the database, and return its associated
 * string database
 */
static DB *
ng_insert(DB *db, const char *name)
{
	DB             *xdb = NULL;
	DBT             key, data;

	key.data = __UNCONST(name);
	key.size = strlen(name) + 1;

	switch ((db->get)(db, &key, &data, 0)) {
	case 0:
		(void)memcpy(&xdb, data.data, sizeof(xdb));
		break;

	case 1:
		xdb = dbopen(NULL, O_RDWR | O_CREAT | O_EXCL, 0, DB_HASH, NULL);
		if (xdb == NULL)
			err(1, "dbopen");

		data.data = (u_char *)&xdb;
		data.size = sizeof(xdb);
		switch ((db->put)(db, &key, &data, R_NOOVERWRITE)) {
		case 0:
			break;

		case -1:
			err(1, "db put `%s'", name);

		case 1:
		default:
			abort();
		}
		break;

	case -1:
		err(1, "db get `%s'", name);

	default:
		abort();
	}

	return xdb;
}


/*
 * ng_reventry(): Recursively add all the netgroups to the group entry.
 */
static void
ng_reventry(DB *db, DB *udb, struct nentry *fe, char *name, size_t s,
    StringList *ss)
{
	DBT             key, data;
	struct nentry  *e;
	struct netgroup *ng;
	char           *p;
	DB             *xdb;

	if (sl_find(ss, fe->n_name) != NULL) {
		_ng_cycle(name, ss);
		return;
	}
	sl_add(ss, fe->n_name);

	for (e = fe->n_next; e != NULL; e = e->n_next)
		switch (e->n_type) {
		case _NG_GROUP:
			if (!dups)
				sl_delete(ss, fe->n_name, 0);
			ng = e->n_group;
			p = _ng_makekey(*((char **)(((char *) ng) + s)),
					ng->ng_domain, e->n_size);
			xdb = ng_insert(udb, p);
			key.data = (u_char *)name;
			key.size = strlen(name) + 1;
			data.data = NULL;
			data.size = 0;
			switch ((xdb->put)(xdb, &key, &data, R_NOOVERWRITE)) {
			case 0:
			case 1:
				break;

			case -1:
				err(1, "db put `%s'", name);

			default:
				abort();
			}
			free(p);
			break;

		case _NG_NAME:
			key.data = (u_char *) e->n_name;
			key.size = strlen(e->n_name) + 1;
			switch ((db->get)(db, &key, &data, 0)) {
				struct nentry *rfe;
			case 0:
				(void)memcpy(&rfe, data.data, sizeof(rfe));
				ng_reventry(db, udb, rfe, name, s, ss);
				break;

			case 1:
				break;

			case -1:
				err(1, "db get `%s'", e->n_name);

			default:
				abort();
			}
			break;

		default:
			abort();
		}
}


/*
 * ng_reverse(): Reverse the database
 */
static DB *
ng_reverse(DB *db, size_t s)
{
	int             pos;
	StringList	*sl;
	DBT             key, data;
	struct nentry  *fe;
	DB             *udb = dbopen(NULL, O_RDWR | O_CREAT | O_EXCL, 0,
	    DB_HASH, NULL);

	if (udb == NULL)
		err(1, "dbopen");

	for (pos = R_FIRST;; pos = R_NEXT)
		switch ((db->seq)(db, &key, &data, pos)) {
		case 0:
			sl = sl_init();
			(void)memcpy(&fe, data.data, sizeof(fe));
			ng_reventry(db, udb, fe, (char *) key.data, s, sl);
			sl_free(sl, 0);
			break;

		case 1:
			return udb;

		case -1:
			err(1, "seq");
		}
}


/*
 * ng_print(): Pretty print a netgroup entry
 */
static void
ng_print(struct nentry *e, struct string *str)
{
	char           *ptr;

	if (e->n_next == NULL) {
		str_append(str, "", ' ');
		return;
	}

	ptr = emalloc(e->n_size);

	for (e = e->n_next; e != NULL; e = e->n_next) {
		switch (e->n_type) {
		case _NG_NAME:
			(void)snprintf(ptr, e->n_size, "%s", e->n_name);
			break;

		case _NG_GROUP:
			(void)snprintf(ptr, e->n_size, "(%s,%s,%s)",
			    NG_EMPTY(e->n_group->ng_host),
			    NG_EMPTY(e->n_group->ng_user),
			    NG_EMPTY(e->n_group->ng_domain));
			break;

		default:
			errx(1, "Internal error: Bad netgroup type");
		}
		str_append(str, ptr, ' ');
	}
	free(ptr);
}


/*
 * ng_rprint(): Pretty print all reverse netgroup mappings in the given entry
 */
static void
ng_rprint(DB *db, struct string *str)
{
	int             pos;
	DBT             key, data;

	for (pos = R_FIRST;; pos = R_NEXT)
		switch ((db->seq)(db, &key, &data, pos)) {
		case 0:
			str_append(str, (char *)key.data, ',');
			break;

		case 1:
			return;

		default:
			err(1, "seq");
		}
}


#ifdef DEBUG_NG
/*
 * ng_dump(): Pretty print all netgroups in the given database
 */
static void
ng_dump(DB *db)
{
	int             pos;
	DBT             key, data;
	struct nentry  *e;
	struct string   buf;

	for (pos = R_FIRST;; pos = R_NEXT)
		switch ((db->seq)(db, &key, &data, pos)) {
		case 0:
			(void)memcpy(&e, data.data, sizeof(e));
			str_init(&buf);
			assert(e->n_type == _NG_NAME);

			ng_print(e, &buf);
			(void)fprintf(stderr, "%s\t%s\n", e->n_name,
			    buf.s_str ? buf.s_str : "");
			str_free(&buf);
			break;

		case 1:
			return;

		default:
			err(1, "seq");
		}
}


/*
 * ng_rdump(): Pretty print all reverse mappings in the given database
 */
static void
ng_rdump(DB *db)
{
	int             pos;
	DBT             key, data;
	DB             *xdb;
	struct string   buf;

	for (pos = R_FIRST;; pos = R_NEXT)
		switch ((db->seq)(db, &key, &data, pos)) {
		case 0:
			(void)memcpy(&xdb, data.data, sizeof(xdb));
			str_init(&buf);
			ng_rprint(xdb, &buf);
			(void)fprintf(stderr, "%s\t%s\n",
			    (char *)key.data, buf.s_str ? buf.s_str : "");
			str_free(&buf);
			break;

		case 1:
			return;

		default:
			err(1, "seq");
		}
}
#endif /* DEBUG_NG */


/*
 * ng_write(): Dump the database into a file.
 */
static void
ng_write(DB *odb, DB *idb, int k)
{
	int             pos;
	DBT             key, data;
	struct nentry  *e;
	struct string   skey, sdata;

	for (pos = R_FIRST;; pos = R_NEXT)
		switch ((idb->seq)(idb, &key, &data, pos)) {
		case 0:
			memcpy(&e, data.data, sizeof(e));
			str_init(&skey);
			str_init(&sdata);
			assert(e->n_type == _NG_NAME);

			str_prepend(&skey, e->n_name, k);
			ng_print(e, &sdata);
			key.data = (u_char *) skey.s_str;
			key.size = skey.s_len + 1;
			data.data = (u_char *) sdata.s_str;
			data.size = sdata.s_len + 1;

			switch ((odb->put)(odb, &key, &data, R_NOOVERWRITE)) {
			case 0:
				break;

			case -1:
				err(1, "put");

			case 1:
			default:
				abort();
			}

			str_free(&skey);
			str_free(&sdata);
			break;

		case 1:
			return;

		default:
			err(1, "seq");
		}
}


/*
 * ng_rwrite(): Write the database
 */
static void
ng_rwrite(DB *odb, DB *idb, int k)
{
	int             pos;
	DBT             key, data;
	DB             *xdb;
	struct string   skey, sdata;

	for (pos = R_FIRST;; pos = R_NEXT)
		switch ((idb->seq)(idb, &key, &data, pos)) {
		case 0:
			memcpy(&xdb, data.data, sizeof(xdb));
			str_init(&skey);
			str_init(&sdata);

			str_prepend(&skey, (char *) key.data, k);
			ng_rprint(xdb, &sdata);
			key.data = (u_char *) skey.s_str;
			key.size = skey.s_len + 1;
			data.data = (u_char *) sdata.s_str;
			data.size = sdata.s_len + 1;

			switch ((odb->put)(odb, &key, &data, R_NOOVERWRITE)) {
			case 0:
				break;

			case -1:
				err(1, "put");

			case 1:
			default:
				abort();
			}

			str_free(&skey);
			str_free(&sdata);
			break;

		case 1:
			return;

		default:
			err(1, "seq");
		}
}


/*
 * usage(): Print usage message and exit
 */
static void
usage(void)
{

	(void)fprintf(stderr, "Usage: %s [-D] [-o db] [<file>]\n",
	    getprogname());
	exit(1);
}
