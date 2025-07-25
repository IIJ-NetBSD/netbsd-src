/*	$NetBSD: msg_207.c,v 1.6 2025/07/07 19:57:17 rillig Exp $	*/
# 3 "msg_207.c"

// Test for message: loop not entered at top [207]
// This message is not used.
// Its purpose is unclear, and the number of false positives is too high.

static void
/* expect+1: warning: static function 'for_loop' unused [236] */
for_loop(void)
{
	for (int i = 0; i < 10; i++)
		if (0 == 1)
			for (i = 0;
			    i < 5;
				/* was+2: warning: loop not entered at top [207] */
				/* expect+1: warning: end-of-loop code not reached [223] */
			    i += 4)
				return;

	// XXX: Why is this different from the snippet above?
	for (int i = 0; i < 10; i++)
		if (0 == 1)
			/* expect+1: warning: 'init' statement not reached [193] */
			for (int j = 0;
			    j < 5;
			    /* expect+1: warning: end-of-loop code not reached [223] */
			    j += 4)
				return;
}

static void
/* expect+1: warning: static function 'while_loop' unused [236] */
while_loop(void)
{
	for (int i = 0; i < 10; i++)
		if (0 == 1)
			/* was+1: warning: loop not entered at top [207] */
			while (i < 5)
				i += 4;
}

static void
/* expect+1: warning: static function 'do_loop' unused [236] */
do_loop(void)
{
	for (int i = 0; i < 10; i++)
		if (0 == 1)
			/* was+1: warning: loop not entered at top [207] */
			do {
				i += 4;
			} while (i < 5);
}
