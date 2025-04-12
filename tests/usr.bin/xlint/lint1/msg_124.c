/*	$NetBSD: msg_124.c,v 1.16 2025/04/12 15:49:50 rillig Exp $	*/
# 3 "msg_124.c"

// Test for message: invalid combination of '%s' and '%s', op '%s' [124]

/* lint1-extra-flags: -s -X 191,351 */

typedef void(*signal_handler)(int);

typedef signal_handler(*sys_signal)(signal_handler);

typedef int(*printflike)(const char *, ...)
    __attribute__((format(printf, 1, 2)));

void
example(int *ptr)
{
	/* expect+1: warning: invalid combination of 'pointer to function(int) returning void' and 'pointer to int', op 'init' [124] */
	signal_handler handler = ptr;
	/* expect+1: warning: invalid combination of 'pointer to function(pointer to function(int) returning void) returning pointer to function(int) returning void' and 'pointer to int', op 'init' [124] */
	sys_signal signal = ptr;
	/* expect+1: warning: invalid combination of 'pointer to function(pointer to const char, ...) returning int' and 'pointer to int', op 'init' [124] */
	printflike printf = ptr;
}

void ok(_Bool);
void not_ok(_Bool);

void
compare_pointers(const void *vp, const char *cp, const int *ip,
		 signal_handler fp)
{
	ok(vp == cp);
	ok(vp == ip);
	/* expect+1: warning: C90 or later forbid comparison of 'void *' with function pointer [274] */
	ok(vp == fp);
	/* expect+1: warning: invalid combination of 'pointer to const char' and 'pointer to const int', op '==' [124] */
	not_ok(cp == ip);
	/* expect+1: warning: invalid combination of 'pointer to const char' and 'pointer to function(int) returning void', op '==' [124] */
	not_ok(cp == fp);
	ok(vp == (void *)0);
	ok(cp == (void *)0);
	ok(ip == (void *)0);
	ok(fp == (void *)0);	/* wrong 274 before 2021-01-25 */
	ok((void *)0 == vp);
	ok((void *)0 == cp);
	ok((void *)0 == ip);
	ok((void *)0 == fp);	/* wrong 274 before 2021-01-25 */
	ok(vp == 0);
	ok(cp == 0);
	ok(ip == 0);
	ok(fp == 0);
	ok(vp == 0L);
	ok(cp == 0L);
	ok(ip == 0L);
	ok(fp == 0L);
}
