/*	$NetBSD: op_colon.c,v 1.8 2024/01/28 08:17:27 rillig Exp $	*/
# 3 "op_colon.c"

/*
 * Test handling of the operator ':', as part of the '?:'.
 */

/* lint1-extra-flags: -p -X 351 */

struct canary {
	int member;
};

void
sink(struct canary);

void
test_merge_qualifiers(_Bool cond, int *p, const int *c, volatile int *v,
		      const volatile int *cv)
{
	/* expect+1: ... 'pointer to int' ... */
	sink(cond ? p : p);
	/* expect+1: ... 'pointer to const int' ... */
	sink(cond ? p : c);
	/* expect+1: ... 'pointer to volatile int' ... */
	sink(cond ? p : v);
	/* expect+1: ... 'pointer to const volatile int' ... */
	sink(cond ? p : cv);

	/* expect+1: ... 'pointer to const int' ... */
	sink(cond ? c : p);
	/* expect+1: ... 'pointer to const int' ... */
	sink(cond ? c : c);
	/* expect+1: ... 'pointer to const volatile int' ... */
	sink(cond ? c : v);
	/* expect+1: ... 'pointer to const volatile int' ... */
	sink(cond ? c : cv);

	/* expect+1: ... 'pointer to volatile int' ... */
	sink(cond ? v : p);
	/* expect+1: ... 'pointer to const volatile int' ... */
	sink(cond ? v : c);
	/* expect+1: ... 'pointer to volatile int' ... */
	sink(cond ? v : v);
	/* expect+1: ... 'pointer to const volatile int' ... */
	sink(cond ? v : cv);

	/* expect+1: ... 'pointer to const volatile int' ... */
	sink(cond ? cv : p);
	/* expect+1: ... 'pointer to const volatile int' ... */
	sink(cond ? cv : c);
	/* expect+1: ... 'pointer to const volatile int' ... */
	sink(cond ? cv : v);
	/* expect+1: ... 'pointer to const volatile int' ... */
	sink(cond ? cv : cv);
}

/* null pointer constant + other pointer */
void
c99_6_5_15_p6(_Bool cond, const volatile double *cv_dp)
{
	/* expect+1: ... 'pointer to const volatile double' ... */
	sink(cond ? cv_dp : (void *)0);
	/* expect+1: ... 'pointer to const volatile double' ... */
	sink(cond ? (void *)0 : cv_dp);
}

void
c99_6_5_15_p8(
    _Bool cond,
    const void *c_vp,
    void *vp,
    const int *c_ip,
    volatile int *v_ip,
    int *ip,
    const char *c_cp
)
{
	/* expect+1: ... 'pointer to const void' ... */
	sink(cond ? c_vp : c_ip);
	/* expect+1: ... 'pointer to const void' ... */
	sink(cond ? c_ip : c_vp);

	/* expect+1: ... 'pointer to volatile int' ... */
	sink(cond ? v_ip : 0);
	/* expect+1: ... 'pointer to volatile int' ... */
	sink(cond ? 0 : v_ip);

	/* expect+1: ... 'pointer to const volatile int' ... */
	sink(cond ? c_ip : v_ip);
	/* expect+1: ... 'pointer to const volatile int' ... */
	sink(cond ? v_ip : c_ip);

	/* expect+1: ... 'pointer to const void' ... */
	sink(cond ? vp : c_cp);
	/* expect+1: ... 'pointer to const void' ... */
	sink(cond ? c_cp : vp);

	/* expect+1: ... 'pointer to const int' ... */
	sink(cond ? ip : c_ip);
	/* expect+1: ... 'pointer to const int' ... */
	sink(cond ? c_ip : ip);

	/* expect+1: ... 'pointer to void' ... */
	sink(cond ? vp : ip);
	/* expect+1: ... 'pointer to void' ... */
	sink(cond ? ip : vp);
}
