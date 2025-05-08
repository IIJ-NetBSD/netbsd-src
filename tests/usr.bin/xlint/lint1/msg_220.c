/*	$NetBSD: msg_220.c,v 1.11 2025/05/08 20:51:41 rillig Exp $	*/
# 3 "msg_220.c"

// Test for message: fallthrough on case statement [220]

/* lint1-extra-flags: -h -X 351 */

extern void
println(const char *);

void
example(int n)
{
	switch (n) {
	case 1:
	case 3:
	case 5:
		println("odd");
		/* expect+1: warning: fallthrough on case statement [220] */
	case 2:
	case 7:
		println("prime");
		/* expect+1: warning: fallthrough on default statement [284] */
	default:
		println("number");
	}
}

void
example1(int n)
{
	switch (n) {
	case 1:
	case 3:
	case 5:
		println("odd");
		__attribute__((__fallthrough__));
	case 2:
	case 7:
		println("prime");
		__attribute__((__fallthrough__));
	default:
		println("number");
	}
}

/* https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html#index-Wimplicit-fallthrough */
void
annotation_comment_variations(int n)
{
	switch (n) {
	case 0:
		println("0");
		/* FALLTHROUGH */
	case 1:
		println("1");
		/* Seen in libarchive/archive_string.c, macro WRITE_UC. */
		/* FALL THROUGH */
		/* Lint warned before lex.c 1.79 from 2021-08-29. */
	case 2:
		println("2");
		/* FALLS THROUGH */
		/* expect+1: warning: fallthrough on case statement [220] */
	case 3:
		println("3");
		/* intentionally falls through */
		/* expect+1: warning: fallthrough on case statement [220] */
	case 4:
		println("4");
		/* This is the Splint variant, which is seldom used. */
		/* @fallthrough@ */
		/* expect+1: warning: fallthrough on case statement [220] */
	case 5:
		println("5");
		/* Seen in unbound/lookup3.c, function hashlittle. */
		/* Lint warned before lex.c 1.80 from 2021-08-29. */
		/* fallthrough */
	case 6:
		println("6");
	}
}


void do_return(void);
void noreturn(void)
    __attribute__((__noreturn__));
void noreturn_attr_comma(void)
    __attribute__((__unused__, __noreturn__));
void noreturn_attr_attr(void)
    __attribute__((__unused__))
    __attribute__((__noreturn__));

void
call_noreturn(void)
{
	switch (0) {
	case 0:
		do_return();
		/* expect+1: warning: fallthrough on case statement [220] */
	case 1:
		noreturn();
	case 2:
		noreturn_attr_comma();
	case 3:
		noreturn_attr_attr();
	case 4:
		return;
	}
}
