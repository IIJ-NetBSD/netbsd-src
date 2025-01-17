// SPDX-FileCopyrightText: 2013 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
//
// SPDX-License-Identifier: MIT

/*
 * This example shows how to use cds_lfht_destroy() to clear memory used
 * by a a RCU lock-free hash table. This hash table requires using a
 * RCU scheme.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <urcu/urcu-memb.h>	/* RCU flavor */
#include <urcu/rculfhash.h>	/* RCU Lock-free hash table */
#include <urcu/compiler.h>	/* For CAA_ARRAY_SIZE */
#include "jhash.h"		/* Example hash function */

/*
 * Nodes populated into the hash table.
 */
struct mynode {
	int value;			/* Node content */
	struct cds_lfht_node node;	/* Chaining in hash table */
	struct rcu_head rcu_head;	/* For call_rcu() */
};

static
void free_node(struct rcu_head *head)
{
	struct mynode *node = caa_container_of(head, struct mynode, rcu_head);

	free(node);
}

int main(void)
{
	int values[] = { -5, 42, 42, 36, 24, };	/* 42 is duplicated */
	struct cds_lfht *ht;	/* Hash table */
	unsigned int i;
	int ret = 0;
	uint32_t seed;
	struct cds_lfht_iter iter;	/* For iteration on hash table */
	struct cds_lfht_node *ht_node;
	struct mynode *node;

	/*
	 * Each thread need using RCU read-side need to be explicitly
	 * registered.
	 */
	urcu_memb_register_thread();

	/* Use time as seed for hash table hashing. */
	seed = (uint32_t) time(NULL);

	/*
	 * Allocate hash table.
	 */
	ht = cds_lfht_new_flavor(1, 1, 0,
		CDS_LFHT_AUTO_RESIZE | CDS_LFHT_ACCOUNTING,
		&urcu_memb_flavor, NULL);
	if (!ht) {
		printf("Error allocating hash table\n");
		ret = -1;
		goto end;
	}

	/*
	 * Add nodes to hash table.
	 */
	for (i = 0; i < CAA_ARRAY_SIZE(values); i++) {
		unsigned long hash;
		int value;

		node = malloc(sizeof(*node));
		if (!node) {
			ret = -1;
			goto end;
		}

		cds_lfht_node_init(&node->node);
		value = values[i];
		node->value = value;
		hash = jhash(&value, sizeof(value), seed);

		/*
		 * cds_lfht_add() needs to be called from RCU read-side
		 * critical section.
		 */
		urcu_memb_read_lock();
		cds_lfht_add(ht, hash, &node->node);
		urcu_memb_read_unlock();
	}

	/*
	 * Iterate over each hash table node. Those will appear in
	 * random order, depending on the hash seed. Iteration needs to
	 * be performed within RCU read-side critical section.
	 */
	printf("hash table content (random order):");
	urcu_memb_read_lock();
	cds_lfht_for_each_entry(ht, &iter, node, node) {
		printf(" %d", node->value);
	}
	urcu_memb_read_unlock();
	printf("\n");


	/*
	 * Make sure all hash table nodes are removed before destroying.
	 */
	printf("removing all nodes:");
	urcu_memb_read_lock();
	cds_lfht_for_each_entry(ht, &iter, node, node) {
		ht_node = cds_lfht_iter_get_node(&iter);
		ret = cds_lfht_del(ht, ht_node);
		printf(" %d", node->value);
		if (ret) {
			printf(" (concurrently deleted)");
		} else {
			urcu_memb_call_rcu(&node->rcu_head, free_node);
		}
	}
	urcu_memb_read_unlock();
	printf("\n");

	/*
	 * cds_lfht_destroy() must be called from a very specific
	 * context: it needs to be called from a registered RCU reader
	 * thread. However, this thread should _not_ be within a RCU
	 * read-side critical section. Also, it should _not_ be called
	 * from a call_rcu thread.
	 */
	ret = cds_lfht_destroy(ht, NULL);
	if (ret) {
		printf("Destroying hash table failed\n");
	}
end:
	urcu_memb_unregister_thread();
	return ret;
}
