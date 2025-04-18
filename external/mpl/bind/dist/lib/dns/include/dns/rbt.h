/*	$NetBSD: rbt.h,v 1.10 2025/01/26 16:25:28 christos Exp $	*/

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

#pragma once

/*! \file dns/rbt.h */

#include <inttypes.h>
#include <stdbool.h>

#include <isc/assertions.h>
#include <isc/crc64.h>
#include <isc/lang.h>
#include <isc/magic.h>
#include <isc/refcount.h>

#include <dns/types.h>

ISC_LANG_BEGINDECLS

/*@{*/
/*%
 * Option values for dns_rbt_findnode().
 * These are used to form a bitmask.
 */
#define DNS_RBTFIND_NOOPTIONS	  0x00
#define DNS_RBTFIND_EMPTYDATA	  0x01
#define DNS_RBTFIND_NOEXACT	  0x02
#define DNS_RBTFIND_NOPREDECESSOR 0x04
/*@}*/

#define DNS_RBT_USEMAGIC 1

#define DNS_RBT_LOCKLENGTH (sizeof(((dns_rbtnode_t *)0)->locknum) * 8)

#define DNS_RBTNODE_MAGIC ISC_MAGIC('R', 'B', 'N', 'O')
#if DNS_RBT_USEMAGIC
#define DNS_RBTNODE_VALID(n) ISC_MAGIC_VALID(n, DNS_RBTNODE_MAGIC)
#else /* if DNS_RBT_USEMAGIC */
#define DNS_RBTNODE_VALID(n) true
#endif /* if DNS_RBT_USEMAGIC */

/*%
 * This is the structure that is used for each node in the red/black
 * tree of trees.  NOTE WELL:  the implementation manages this as a variable
 * length structure, with the actual wire-format name and other data
 * appended to this structure.  Allocating a contiguous block of memory for
 * multiple dns_rbtnode structures will not work.
 */
struct dns_rbtnode {
#if DNS_RBT_USEMAGIC
	unsigned int magic;
#endif /* if DNS_RBT_USEMAGIC */
	/*@{*/
	/*!
	 * The following bitfields add up to a total bitwidth of 32.
	 * The range of values necessary for each item is indicated.
	 *
	 * In each case below the "range" indicated is what's _necessary_ for
	 * the bitfield to hold, not what it actually _can_ hold.
	 *
	 * Note: Tree lock must be held before modifying these
	 * bit-fields.
	 *
	 * Note: The two "unsigned int :0;" unnamed bitfields on either
	 * side of the bitfields below are scaffolding that border the
	 * set of bitfields which are accessed after acquiring the tree
	 * lock. Please don't insert any other bitfield members between
	 * the unnamed bitfields unless they should also be accessed
	 * after acquiring the tree lock.
	 */
	unsigned int		   : 0; /* start of bitfields c/o tree lock */
	unsigned int is_root	   : 1; /*%< range is 0..1 */
	unsigned int color	   : 1; /*%< range is 0..1 */
	unsigned int find_callback : 1; /*%< range is 0..1 */
	bool	     absolute	   : 1; /*%< node with absolute DNS name */
	unsigned int nsec	   : 2; /*%< range is 0..3 */
	unsigned int namelen	   : 8; /*%< range is 1..255 */
	unsigned int offsetlen	   : 8; /*%< range is 1..128 */
	unsigned int oldnamelen	   : 8; /*%< range is 1..255 */
	unsigned int		   : 0; /* end of bitfields c/o tree lock */
	/*@}*/

	/*%
	 * These are needed for hashing. The 'uppernode' points to the
	 * node's superdomain node in the parent subtree, so that it can
	 * be reached from a child that was found by a hash lookup.
	 */
	unsigned int   hashval;
	dns_rbtnode_t *uppernode;
	dns_rbtnode_t *hashnext;

	dns_rbtnode_t *parent;
	dns_rbtnode_t *left;
	dns_rbtnode_t *right;
	dns_rbtnode_t *down;

	/*%
	 * Used for LRU cache.  This linked list is used to mark nodes which
	 * have no data any longer, but we cannot unlink at that exact moment
	 * because we did not or could not obtain a write lock on the tree.
	 */
	ISC_LINK(dns_rbtnode_t) deadlink;

	/*@{*/
	/*!
	 * These values are used in the RBT DB implementation.  The appropriate
	 * node lock must be held before accessing them.
	 *
	 * Note: The two "unsigned int :0;" unnamed bitfields on either
	 * side of the bitfields below are scaffolding that border the
	 * set of bitfields which are accessed after acquiring the node
	 * lock. Please don't insert any other bitfield members between
	 * the unnamed bitfields unless they should also be accessed
	 * after acquiring the node lock.
	 *
	 * NOTE: Do not merge these fields into bitfields above, as
	 * they'll all be put in the same qword that could be accessed
	 * without the node lock as it shares the qword with other
	 * members. Leave these members here so that they occupy a
	 * separate region of memory.
	 */
	void *data;
	uint8_t	      : 0; /* start of bitfields c/o node lock */
	uint8_t dirty : 1;
	uint8_t wild  : 1;
	uint8_t	      : 0;	/* end of bitfields c/o node lock */
	uint16_t       locknum; /* note that this is not in the bitfield */
	isc_refcount_t references;
	/*@}*/
};

typedef isc_result_t (*dns_rbtfindcallback_t)(dns_rbtnode_t	*node,
					      dns_name_t	*name,
					      void *callback_arg DNS__DB_FLARG);

typedef void (*dns_rbtdeleter_t)(void *, void *);

/*****
*****  Chain Info
*****/

/*!
 * A chain is used to keep track of the sequence of nodes to reach any given
 * node from the root of the tree.  Originally nodes did not have parent
 * pointers in them (for memory usage reasons) so there was no way to find
 * the path back to the root from any given node.  Now that nodes have parent
 * pointers, chains might be going away in a future release, though the
 * movement functionality would remain.
 *
 * Chains may be used to iterate over a tree of trees.  After setting up the
 * chain's structure using dns_rbtnodechain_init(), it needs to be initialized
 * to point to the lexically first or lexically last node in the tree of trees
 * using dns_rbtnodechain_first() or dns_rbtnodechain_last(), respectively.
 * Calling dns_rbtnodechain_next() or dns_rbtnodechain_prev() then moves the
 * chain over to the next or previous node, respectively.
 *
 * In any event, parent information, whether via parent pointers or chains, is
 * necessary information for iterating through the tree or for basic internal
 * tree maintenance issues (ie, the rotations that are done to rebalance the
 * tree when a node is added).  The obvious implication of this is that for a
 * chain to remain valid, the tree has to be locked down against writes for the
 * duration of the useful life of the chain, because additions or removals can
 * change the path from the root to the node the chain has targeted.
 *
 * The dns_rbtnodechain_ functions _first, _last, _prev and _next all take
 * dns_name_t parameters for the name and the origin, which can be NULL.  If
 * non-NULL, 'name' will end up pointing to the name data and offsets that are
 * stored at the node (and thus it will be read-only), so it should be a
 * regular dns_name_t that has been initialized with dns_name_init.  When
 * 'origin' is non-NULL, it will get the name of the origin stored in it, so it
 * needs to have its own buffer space and offsets, which is most easily
 * accomplished with a dns_fixedname_t.  It is _not_ necessary to reinitialize
 * either 'name' or 'origin' between calls to the chain functions.
 *
 * NOTE WELL: even though the name data at the root of the tree of trees will
 * be absolute (typically just "."), it will will be made into a relative name
 * with an origin of "." -- an empty name when the node is ".".  This is
 * because a common on operation on 'name' and 'origin' is to use
 * dns_name_concatenate() on them to generate the complete name.  An empty name
 * can be detected when dns_name_countlabels == 0, and is printed by
 * dns_name_totext()/dns_name_format() as "@", consistent with RFC1035's
 * definition of "@" as the current origin.
 *
 * dns_rbtnodechain_current is similar to the _first, _last, _prev and _next
 * functions but additionally can provide the node to which the chain points.
 */

/*%
 * The number of level blocks to allocate at a time, same as the maximum
 * number of labels. Allocating space for 128 levels when the tree is
 * almost never that deep is wasteful, but it's not clear that it matters,
 * since the waste is only 1MB for 1000 concurrently active chains on a
 * system with 64-bit pointers.
 */
#define DNS_RBT_LEVELBLOCK 127

typedef struct dns_rbtnodechain {
	unsigned int magic;
	/*%
	 * The terminal node of the chain.  It is not in levels[].
	 * This is ostensibly private ... but in a pinch it could be
	 * used tell that the chain points nowhere without needing to
	 * call dns_rbtnodechain_current().
	 */
	dns_rbtnode_t *end;
	/*%
	 * Currently the maximum number of levels is allocated directly in
	 * the structure, but future revisions of this code might have a
	 * static initial block with dynamic growth.
	 */
	dns_rbtnode_t *levels[DNS_RBT_LEVELBLOCK];
	/*%
	 * level_count indicates how deep the chain points into the
	 * tree of trees, and is the index into the levels[] array.
	 * Thus, levels[level_count - 1] is the last level node stored.
	 * A chain that points to the top level of the tree of trees has
	 * a level_count of 0, the first level has a level_count of 1, and
	 * so on.
	 */
	unsigned int level_count;
	/*%
	 * level_matches tells how many levels matched above the node
	 * returned by dns_rbt_findnode().  A match (partial or exact) found
	 * in the first level thus results in level_matches being set to 1.
	 * This is used by the rbtdb to set the start point for a recursive
	 * search of superdomains until the RR it is looking for is found.
	 */
	unsigned int level_matches;
} dns_rbtnodechain_t;

/*****
***** Public interfaces.
*****/
isc_result_t
dns_rbt_create(isc_mem_t *mctx, dns_rbtdeleter_t deleter, void *deleter_arg,
	       dns_rbt_t **rbtp);
/*%<
 * Initialize a red-black tree of trees.
 *
 * Notes:
 *\li   The deleter argument, if non-null, points to a function that is
 *      responsible for cleaning up any memory associated with the data
 *      pointer of a node when the node is deleted.  It is passed the
 *      deleted node's data pointer as its first argument and deleter_arg
 *      as its second argument.
 *
 * Requires:
 * \li  mctx is a pointer to a valid memory context.
 *\li   rbtp != NULL && *rbtp == NULL
 *\li   arg == NULL iff deleter == NULL
 *
 * Ensures:
 *\li   If result is ISC_R_SUCCESS:
 *              *rbtp points to a valid red-black tree manager
 *
 *\li   If result is failure:
 *              *rbtp does not point to a valid red-black tree manager.
 *
 * Returns:
 *\li   #ISC_R_SUCCESS  Success
 */

isc_result_t
dns_rbt_addnode(dns_rbt_t *rbt, const dns_name_t *name, dns_rbtnode_t **nodep);

/*%<
 * Add 'name' to the tree of trees. On success, return the address of
 * the newly added node. If 'name' already existed, return ISC_R_EXISTS
 * and the address of the pre-existing node.
 *
 * Requires:
 *\li   rbt is a valid rbt structure.
 *\li   dns_name_isabsolute(name) == TRUE
 *\li   nodep != NULL && *nodep == NULL
 *
 * Ensures:
 *\li   'name' is not altered in any way.
 *
 *\li   Any external references to nodes in the tree are unaffected by
 *      node splits that are necessary to insert the new name.
 *
 *\li   If result is ISC_R_SUCCESS:
 *              'name' is findable in the red/black tree of trees in O(log N).
 *              *nodep is the node that was added for 'name'.
 *
 *\li   If result is ISC_R_EXISTS:
 *              The tree of trees is unaltered.
 *              *nodep is the existing node for 'name'.
 *
 * Returns:
 *\li   #ISC_R_SUCCESS  Success
 *\li   #ISC_R_EXISTS   The name already exists, possibly without data.
 *\li   #ISC_R_NOSPACE  The name had more logical labels than are allowed.
 */

#define dns_rbt_findnode(rbt, name, foundname, node, chain, options, callback, \
			 callback_arg)                                         \
	dns__rbt_findnode(rbt, name, foundname, node, chain, options,          \
			  callback, callback_arg DNS__DB_FILELINE)
isc_result_t
dns__rbt_findnode(dns_rbt_t *rbt, const dns_name_t *name, dns_name_t *foundname,
		  dns_rbtnode_t **node, dns_rbtnodechain_t *chain,
		  unsigned int options, dns_rbtfindcallback_t callback,
		  void *callback_arg DNS__DB_FLARG);
/*%<
 * Find the node for 'name'.
 *
 * Notes:
 *\li   A node that has no data is considered not to exist for this function,
 *      unless the DNS_RBTFIND_EMPTYDATA option is set.  This applies to both
 *      exact matches and partial matches.
 *
 *\li   If the chain parameter is non-NULL, then the path through the tree
 *      to the DNSSEC predecessor of the searched for name is maintained,
 *      unless the DNS_RBTFIND_NOPREDECESSOR or DNS_RBTFIND_NOEXACT option
 *      is used. (For more details on those options, see below.)
 *
 *\li   If there is no predecessor, then the chain will point to nowhere, as
 *      indicated by chain->end being NULL or dns_rbtnodechain_current
 *      returning ISC_R_NOTFOUND.  Note that in a normal Internet DNS RBT
 *      there will always be a predecessor for all names except the root
 *      name, because '.' will exist and '.' is the predecessor of
 *      everything.  But you can certainly construct a trivial tree and a
 *      search for it that has no predecessor.
 *
 *\li   Within the chain structure, the 'levels' member of the structure holds
 *      the root node of each level except the first.
 *
 *\li   The 'level_count' of the chain indicates how deep the chain to the
 *      predecessor name is, as an index into the 'levels[]' array.  It does
 *      not count name elements, per se, but only levels of the tree of trees,
 *      the distinction arising because multiple labels from a name can be
 *      stored on only one level.  It is also does not include the level
 *      that has the node, since that level is not stored in levels[].
 *
 *\li   The chain's 'level_matches' is not directly related to the predecessor.
 *      It is the number of levels above the level of the found 'node',
 *      regardless of whether it was a partial match or exact match.  When
 *      the node is found in the top level tree, or no node is found at all,
 *      level_matches is 0.
 *
 *\li   When DNS_RBTFIND_NOEXACT is set, the closest matching superdomain is
 *      returned (also subject to DNS_RBTFIND_EMPTYDATA), even when
 *      there is an exact match in the tree.  In this case, the chain
 *      will not point to the DNSSEC predecessor, but will instead point
 *      to the exact match, if there was any.  Thus the preceding paragraphs
 *      should have "exact match" substituted for "predecessor" to describe
 *      how the various elements of the chain are set.  This was done to
 *      ensure that the chain's state was sane, and to prevent problems that
 *      occurred when running the predecessor location code under conditions
 *      it was not designed for.  It is not clear *where* the chain should
 *      point when DNS_RBTFIND_NOEXACT is set, so if you end up using a chain
 *      with this option because you want a particular node, let us know
 *      where you want the chain pointed, so this can be made more firm.
 *
 * Requires:
 *\li   rbt is a valid rbt manager.
 *\li   dns_name_isabsolute(name) == TRUE.
 *\li   node != NULL && *node == NULL.
 *\li   #DNS_RBTFIND_NOEXACT and DNS_RBTFIND_NOPREDECESSOR are mutually
 *              exclusive.
 *
 * Ensures:
 *\li   'name' and the tree are not altered in any way.
 *
 *\li   If result is ISC_R_SUCCESS:
 *\verbatim
 *              *node is the terminal node for 'name'.
 *
 *              'foundname' and 'name' represent the same name (though not
 *              the same memory).
 *
 *              'chain' points to the DNSSEC predecessor, if any, of 'name'.
 *
 *              chain->level_matches and chain->level_count are equal.
 *\endverbatim
 *
 *      If result is DNS_R_PARTIALMATCH:
 *\verbatim
 *              *node is the data associated with the deepest superdomain
 *              of 'name' which has data.
 *
 *              'foundname' is the name of deepest superdomain (which has
 *              data, unless the DNS_RBTFIND_EMPTYDATA option is set).
 *
 *              'chain' points to the DNSSEC predecessor, if any, of 'name'.
 *\endverbatim
 *
 *\li   If result is ISC_R_NOTFOUND:
 *\verbatim
 *              Neither the name nor a superdomain was found.  *node is NULL.
 *
 *              'chain' points to the DNSSEC predecessor, if any, of 'name'.
 *
 *              chain->level_matches is 0.
 *\endverbatim
 *
 * Returns:
 *\li   #ISC_R_SUCCESS          Success
 *\li   #DNS_R_PARTIALMATCH     Superdomain found with data
 *\li   #ISC_R_NOTFOUND         No match, or superdomain with no data
 *\li   #ISC_R_NOSPACE Concatenating nodes to form foundname failed
 */

isc_result_t
dns_rbt_deletenode(dns_rbt_t *rbt, dns_rbtnode_t *node, bool recurse);
/*%<
 * Delete 'node' from the tree of trees.
 *
 * Notes:
 *\li   When 'node' is removed, if recurse is true then all nodes
 *      in levels down from it are removed too.
 *
 * Requires:
 *\li   rbt is a valid rbt manager.
 *\li   node != NULL.
 *
 * Ensures:
 *\li   Does NOT ensure that any external references to nodes in the tree
 *      are unaffected by node joins.
 *
 *\li   If result is ISC_R_SUCCESS:
 *              'node' does not appear in the tree with data; however,
 *              the node might still exist if it serves as a pointer to
 *              a lower tree level as long as 'recurse' was false, hence
 *              the node could can be found with dns_rbt_findnode when
 *              that function's empty_data_ok parameter is true.
 *
 *\li   If result is ISC_R_NOSPACE:
 *              The node was deleted, but the tree structure was not
 *              optimized.
 *
 * Returns:
 *\li   #ISC_R_SUCCESS  Success
 *\li   #ISC_R_NOSPACE  dns_name_concatenate failed when joining nodes.
 */

void
dns_rbt_namefromnode(dns_rbtnode_t *node, dns_name_t *name);
/*%<
 * Convert the sequence of labels stored at 'node' into a 'name'.
 *
 * Notes:
 *\li   This function does not return the full name, from the root, but
 *      just the labels at the indicated node.
 *
 *\li   The name data pointed to by 'name' is the information stored
 *      in the node, not a copy.  Altering the data at this pointer
 *      will likely cause grief.
 *
 * Requires:
 * \li  name->offsets == NULL
 *
 * Ensures:
 * \li  'name' is readonly.
 *
 * \li  'name' will point directly to the labels stored after the
 *      dns_rbtnode_t struct.
 *
 * \li  'name' will have offsets that also point to the information stored
 *      as part of the node.
 */

isc_result_t
dns_rbt_fullnamefromnode(dns_rbtnode_t *node, dns_name_t *name);
/*%<
 * Like dns_rbt_namefromnode, but returns the full name from the root.
 *
 * Notes:
 * \li  Unlike dns_rbt_namefromnode, the name will not point directly
 *      to node data.  Rather, dns_name_concatenate will be used to copy
 *      the name data from each node into the 'name' argument.
 *
 * Requires:
 * \li  name != NULL
 * \li  name has a dedicated buffer.
 *
 * Returns:
 * \li  ISC_R_SUCCESS
 * \li  ISC_R_NOSPACE           (possible via dns_name_concatenate)
 * \li  DNS_R_NAMETOOLONG       (possible via dns_name_concatenate)
 */

char *
dns_rbt_formatnodename(dns_rbtnode_t *node, char *printname, unsigned int size);
/*%<
 * Format the full name of a node for printing, using dns_name_format().
 *
 * Notes:
 * \li  'size' is the length of the printname buffer.  This should be
 *      DNS_NAME_FORMATSIZE or larger.
 *
 * Requires:
 * \li  node and printname are not NULL.
 *
 * Returns:
 * \li  The 'printname' pointer.
 */

unsigned int
dns_rbt_nodecount(dns_rbt_t *rbt);
/*%<
 * Obtain the number of nodes in the tree of trees.
 *
 * Requires:
 * \li  rbt is a valid rbt manager.
 */

size_t
dns_rbt_hashsize(dns_rbt_t *rbt);
/*%<
 * Obtain the current number of buckets in the 'rbt' hash table.
 *
 * Requires:
 * \li  rbt is a valid rbt manager.
 */

isc_result_t
dns_rbt_destroy(dns_rbt_t **rbtp, unsigned int quantum);
/*%<
 * Stop working with a red-black tree of trees.
 * If 'quantum' is zero then the entire tree will be destroyed.
 * If 'quantum' is non zero then up to 'quantum' nodes will be destroyed
 * allowing the rbt to be incrementally destroyed by repeated calls to
 * dns_rbt_destroy2().  Once dns_rbt_destroy2() has been called no other
 * operations than dns_rbt_destroy()/dns_rbt_destroy2() should be
 * performed on the tree of trees.
 *
 * Requires:
 * \li  *rbt is a valid rbt manager.
 *
 * Ensures on ISC_R_SUCCESS:
 * \li  All space allocated by the RBT library has been returned.
 *
 * \li  *rbt is invalidated as an rbt manager.
 *
 * Returns:
 * \li  ISC_R_SUCCESS
 * \li  ISC_R_QUOTA if 'quantum' nodes have been destroyed.
 */

void
dns_rbt_printtext(dns_rbt_t *rbt, void (*data_printer)(FILE *, void *),
		  FILE	    *f);
/*%<
 * Print an ASCII representation of the internal structure of the red-black
 * tree of trees to the passed stream.
 *
 * data_printer is a callback function that is called to print the data
 * in a node. It should print it to the passed FILE stream.
 *
 * Notes:
 * \li  The name stored at each node, along with the node's color, is printed.
 *      Then the down pointer, left and right pointers are displayed
 *      recursively in turn.  NULL down pointers are silently omitted;
 *      NULL left and right pointers are printed.
 */

void
dns_rbt_printdot(dns_rbt_t *rbt, bool show_pointers, FILE *f);
/*%<
 * Print a GraphViz dot representation of the internal structure of the
 * red-black tree of trees to the passed stream.
 *
 * If show_pointers is TRUE, pointers are also included in the generated
 * graph.
 *
 * Notes:
 * \li	The name stored at each node, along with the node's color is displayed.
 *	Then the down pointer, left and right pointers are displayed
 *	recursively in turn.  NULL left, right and down pointers are
 *	silently omitted.
 */

void
dns_rbt_printnodeinfo(dns_rbtnode_t *n, FILE *f);
/*%<
 * Print out various information about a node
 *
 * Requires:
 *\li	'n' is a valid pointer.
 *
 *\li	'f' points to a valid open FILE structure that allows writing.
 */

size_t
dns__rbt_getheight(dns_rbt_t *rbt);
/*%<
 * Return the maximum height of sub-root nodes found in the red-black
 * forest.
 *
 * The height of a node is defined as the number of nodes in the longest
 * path from the node to a leaf. For each subtree in the forest, this
 * function determines the height of its root node. Then it returns the
 * maximum such height in the forest.
 *
 * Note: This function exists for testing purposes. Non-test code must
 * not use it.
 *
 * Requires:
 * \li  rbt is a valid rbt manager.
 */

bool
dns__rbt_checkproperties(dns_rbt_t *rbt);
/*%<
 * Check red-black properties of the forest.
 *
 * Note: This function exists for testing purposes. Non-test code must
 * not use it.
 *
 * Requires:
 * \li  rbt is a valid rbt manager.
 */

size_t
dns__rbtnode_getdistance(dns_rbtnode_t *node);
/*%<
 * Return the distance (in nodes) from the node to its upper node of its
 * subtree. The root node has a distance of 1. A child of the root node
 * has a distance of 2.
 */

/*****
***** Chain Functions
*****/

void
dns_rbtnodechain_init(dns_rbtnodechain_t *chain);
/*%<
 * Initialize 'chain'.
 *
 * Requires:
 *\li   'chain' is a valid pointer.
 *
 * Ensures:
 *\li   'chain' is suitable for use.
 */

void
dns_rbtnodechain_reset(dns_rbtnodechain_t *chain);
/*%<
 * Free any dynamic storage associated with 'chain', and then reinitialize
 * 'chain'.
 *
 * Requires:
 *\li   'chain' is a valid pointer.
 *
 * Ensures:
 *\li   'chain' is suitable for use, and uses no dynamic storage.
 */

void
dns_rbtnodechain_invalidate(dns_rbtnodechain_t *chain);
/*%<
 * Free any dynamic storage associated with 'chain', and then invalidates it.
 *
 * Notes:
 *\li   Future calls to any dns_rbtnodechain_ function will need to call
 *      dns_rbtnodechain_init on the chain first (except, of course,
 *      dns_rbtnodechain_init itself).
 *
 * Requires:
 *\li   'chain' is a valid chain.
 *
 * Ensures:
 *\li   'chain' is no longer suitable for use, and uses no dynamic storage.
 */

isc_result_t
dns_rbtnodechain_current(dns_rbtnodechain_t *chain, dns_name_t *name,
			 dns_name_t *origin, dns_rbtnode_t **node);
/*%<
 * Provide the name, origin and node to which the chain is currently pointed.
 *
 * Notes:
 *\li   The tree need not have be locked against additions for the chain
 *      to remain valid, however there are no guarantees if any deletion
 *      has been made since the chain was established.
 *
 * Requires:
 *\li   'chain' is a valid chain.
 *
 * Ensures:
 *\li   'node', if non-NULL, is the node to which the chain was pointed
 *      by dns_rbt_findnode, dns_rbtnodechain_first or dns_rbtnodechain_last.
 *      If none were called for the chain since it was initialized or reset,
 *      or if the was no predecessor to the name searched for with
 *      dns_rbt_findnode, then '*node' is NULL and ISC_R_NOTFOUND is returned.
 *
 *\li   'name', if non-NULL, is the name stored at the terminal level of
 *      the chain.  This is typically a single label, like the "www" of
 *      "www.isc.org", but need not be so.  At the root of the tree of trees,
 *      if the node is "." then 'name' is ".", otherwise it is relative to ".".
 *      (Minimalist and atypical case:  if the tree has just the name
 *      "isc.org." then the root node's stored name is "isc.org." but 'name'
 *      will be "isc.org".)
 *
 *\li   'origin', if non-NULL, is the sequence of labels in the levels
 *      above the terminal level, such as "isc.org." in the above example.
 *      'origin' is always "." for the root node.
 *
 *
 * Returns:
 *\li   #ISC_R_SUCCESS          name, origin & node were successfully set.
 *\li   #ISC_R_NOTFOUND         The chain does not point to any node.
 *\li   &lt;something_else>     Any error return from dns_name_concatenate.
 */

isc_result_t
dns_rbtnodechain_first(dns_rbtnodechain_t *chain, dns_rbt_t *rbt,
		       dns_name_t *name, dns_name_t *origin);
/*%<
 * Set the chain to the lexically first node in the tree of trees.
 *
 * Notes:
 *\li   By the definition of ordering for DNS names, the root of the tree of
 *      trees is the very first node, since everything else in the megatree
 *      uses it as a common suffix.
 *
 * Requires:
 *\li   'chain' is a valid chain.
 *\li   'rbt' is a valid rbt manager.
 *
 * Ensures:
 *\li   The chain points to the very first node of the tree.
 *
 *\li   'name' and 'origin', if non-NULL, are set as described for
 *      dns_rbtnodechain_current.  Thus 'origin' will always be ".".
 *
 * Returns:
 *\li   #DNS_R_NEWORIGIN                The name & origin were successfully set.
 *\li   &lt;something_else>     Any error result from dns_rbtnodechain_current.
 */

isc_result_t
dns_rbtnodechain_last(dns_rbtnodechain_t *chain, dns_rbt_t *rbt,
		      dns_name_t *name, dns_name_t *origin);
/*%<
 * Set the chain to the lexically last node in the tree of trees.
 *
 * Requires:
 *\li   'chain' is a valid chain.
 *\li   'rbt' is a valid rbt manager.
 *
 * Ensures:
 *\li   The chain points to the very last node of the tree.
 *
 *\li   'name' and 'origin', if non-NULL, are set as described for
 *      dns_rbtnodechain_current.
 *
 * Returns:
 *\li   #DNS_R_NEWORIGIN                The name & origin were successfully set.
 *\li   &lt;something_else>     Any error result from dns_name_concatenate.
 */

isc_result_t
dns_rbtnodechain_prev(dns_rbtnodechain_t *chain, dns_name_t *name,
		      dns_name_t *origin);
/*%<
 * Adjusts chain to point the DNSSEC predecessor of the name to which it
 * is currently pointed.
 *
 * Requires:
 *\li   'chain' is a valid chain.
 *\li   'chain' has been pointed somewhere in the tree with dns_rbt_findnode,
 *      dns_rbtnodechain_first or dns_rbtnodechain_last -- and remember that
 *      dns_rbt_findnode is not guaranteed to point the chain somewhere,
 *      since there may have been no predecessor to the searched for name.
 *
 * Ensures:
 *\li   The chain is pointed to the predecessor of its current target.
 *
 *\li   'name' and 'origin', if non-NULL, are set as described for
 *      dns_rbtnodechain_current.
 *
 *\li   'origin' is only if a new origin was found.
 *
 * Returns:
 *\li   #ISC_R_SUCCESS          The predecessor was found and 'name' was set.
 *\li   #DNS_R_NEWORIGIN                The predecessor was found with a
 * different origin and 'name' and 'origin' were set. \li   #ISC_R_NOMORE There
 * was no predecessor. \li   &lt;something_else>     Any error result from
 * dns_rbtnodechain_current.
 */

isc_result_t
dns_rbtnodechain_next(dns_rbtnodechain_t *chain, dns_name_t *name,
		      dns_name_t *origin);
/*%<
 * Adjusts chain to point the DNSSEC successor of the name to which it
 * is currently pointed.
 *
 * Requires:
 *\li   'chain' is a valid chain.
 *\li   'chain' has been pointed somewhere in the tree with dns_rbt_findnode,
 *      dns_rbtnodechain_first or dns_rbtnodechain_last -- and remember that
 *      dns_rbt_findnode is not guaranteed to point the chain somewhere,
 *      since there may have been no predecessor to the searched for name.
 *
 * Ensures:
 *\li   The chain is pointed to the successor of its current target.
 *
 *\li   'name' and 'origin', if non-NULL, are set as described for
 *      dns_rbtnodechain_current.
 *
 *\li   'origin' is only if a new origin was found.
 *
 * Returns:
 *\li   #ISC_R_SUCCESS          The successor was found and 'name' was set.
 *\li   #DNS_R_NEWORIGIN                The successor was found with a different
 *                              origin and 'name' and 'origin' were set.
 *\li   #ISC_R_NOMORE           There was no successor.
 *\li   &lt;something_else>     Any error result from dns_name_concatenate.
 */

isc_result_t
dns_rbtnodechain_down(dns_rbtnodechain_t *chain, dns_name_t *name,
		      dns_name_t *origin);
/*%<
 * Descend down if possible.
 */

isc_result_t
dns_rbtnodechain_nextflat(dns_rbtnodechain_t *chain, dns_name_t *name);
/*%<
 * Find the next node at the current depth in DNSSEC order.
 */

unsigned int
dns__rbtnode_namelen(dns_rbtnode_t *node);
/*%<
 * Returns the length of the full name of the node. Used only internally
 * and in unit tests.
 */

unsigned int
dns__rbtnode_getsize(dns_rbtnode_t *node);
/*
 * Return allocated size for a node.
 */

ISC_LANG_ENDDECLS
