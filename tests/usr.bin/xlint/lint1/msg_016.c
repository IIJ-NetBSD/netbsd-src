/*	$NetBSD: msg_016.c,v 1.6 2025/02/20 20:59:34 rillig Exp $	*/
# 3 "msg_016.c"

// Test for message: array of function is illegal [16]

/* lint1-extra-flags: -X 351 */

typedef void function(void);

/* expect+1: error: array of function is illegal [16] */
function functions[] = {
	/*
	 * XXX: The below warning should not assume that function is an
	 * integer type.
	 */
	/* expect+1: warning: illegal combination of integer 'int' and pointer 'pointer to void' for 'init' [183] */
	(void *)0,
};
