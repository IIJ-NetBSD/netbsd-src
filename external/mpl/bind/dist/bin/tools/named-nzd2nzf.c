/*	$NetBSD: named-nzd2nzf.c,v 1.11 2025/01/26 16:25:10 christos Exp $	*/

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

#ifndef HAVE_LMDB
#error This program requires the LMDB library.
#endif /* ifndef HAVE_LMDB */

#include <lmdb.h>
#include <stdio.h>
#include <stdlib.h>

#include <dns/view.h>

int
main(int argc, char *argv[]) {
	int status;
	const char *path;
	MDB_env *env = NULL;
	MDB_txn *txn = NULL;
	MDB_cursor *cursor = NULL;
	MDB_dbi dbi;
	MDB_val key, data;

	if (argc != 2) {
		fprintf(stderr, "Usage: named-nzd2nzf <nzd-path>\n");
		exit(EXIT_FAILURE);
	}

	path = argv[1];

	status = mdb_env_create(&env);
	if (status != MDB_SUCCESS) {
		fprintf(stderr, "named-nzd2nzf: mdb_env_create: %s",
			mdb_strerror(status));
		exit(EXIT_FAILURE);
	}

	status = mdb_env_open(env, path, DNS_LMDB_FLAGS, 0600);
	if (status != MDB_SUCCESS) {
		fprintf(stderr, "named-nzd2nzf: mdb_env_open: %s",
			mdb_strerror(status));
		exit(EXIT_FAILURE);
	}

	status = mdb_txn_begin(env, 0, MDB_RDONLY, &txn);
	if (status != MDB_SUCCESS) {
		fprintf(stderr, "named-nzd2nzf: mdb_txn_begin: %s",
			mdb_strerror(status));
		exit(EXIT_FAILURE);
	}

	status = mdb_dbi_open(txn, NULL, 0, &dbi);
	if (status != MDB_SUCCESS) {
		fprintf(stderr, "named-nzd2nzf: mdb_dbi_open: %s",
			mdb_strerror(status));
		exit(EXIT_FAILURE);
	}

	status = mdb_cursor_open(txn, dbi, &cursor);
	if (status != MDB_SUCCESS) {
		fprintf(stderr, "named-nzd2nzf: mdb_cursor_open: %s",
			mdb_strerror(status));
		exit(EXIT_FAILURE);
	}

	for (status = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
	     status == MDB_SUCCESS;
	     status = mdb_cursor_get(cursor, &key, &data, MDB_NEXT))
	{
		if (key.mv_data == NULL || key.mv_size == 0 ||
		    data.mv_data == NULL || data.mv_size == 0)
		{
			fprintf(stderr,
				"named-nzd2nzf: empty column found in "
				"database '%s'",
				path);
			exit(EXIT_FAILURE);
		}

		/* zone zonename { config; }; */
		printf("zone \"%.*s\" %.*s;\n", (int)key.mv_size,
		       (char *)key.mv_data, (int)data.mv_size,
		       (char *)data.mv_data);
	}

	mdb_cursor_close(cursor);
	mdb_txn_abort(txn);
	mdb_env_close(env);

	return 0;
}
