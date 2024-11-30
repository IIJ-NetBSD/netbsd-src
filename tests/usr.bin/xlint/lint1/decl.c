/*	$NetBSD: decl.c,v 1.33 2024/11/30 11:27:20 rillig Exp $	*/
# 3 "decl.c"

/*
 * Tests for declarations, especially the distinction between the
 * declaration-specifiers and the declarators.
 */

/* lint1-extra-flags: -X 191,351 */

/*
 * Even though 'const' comes after 'char' and is therefore quite close to the
 * first identifier, it applies to both identifiers.
 */
void
specifier_qualifier(void)
{
	char const a = 1, b = 2;

	/* expect+1: warning: left operand of '=' must be modifiable lvalue [115] */
	a = 1;
	/* expect+1: warning: left operand of '=' must be modifiable lvalue [115] */
	b = 2;
}

/*
 * Since 'const' comes before 'char', there is no ambiguity whether the
 * 'const' applies to all variables or just to the first.
 */
void
qualifier_specifier(void)
{
	const char a = 1, b = 2;

	/* expect+1: warning: left operand of '=' must be modifiable lvalue [115] */
	a = 3;
	/* expect+1: warning: left operand of '=' must be modifiable lvalue [115] */
	b = 5;
}

void
declarator_with_prefix_qualifier(void)
{
	/* expect+1: error: syntax error 'const' [249] */
	char a = 1, const b = 2;

	a = 1;
	/* expect+1: error: 'b' undefined [99] */
	b = 2;
}

void
declarator_with_postfix_qualifier(void)
{
	/* expect+1: error: syntax error 'const' [249] */
	char a = 1, b const = 2;

	a = 1;
	b = 2;
}

void sink(double *);

void
declarators(void)
{
	char *pc = 0, c = 0, **ppc = 0;

	/* expect+1: warning: converting 'pointer to char' to incompatible 'pointer to double' for argument 1 [153] */
	sink(pc);
	/* expect+1: warning: illegal combination of pointer 'pointer to double' and integer 'char', arg #1 [154] */
	sink(c);
	/* expect+1: warning: converting 'pointer to pointer to char' to incompatible 'pointer to double' for argument 1 [153] */
	sink(ppc);
}

_Bool
enum_error_handling(void)
{
	enum {
		/* expect+1: error: syntax error '"' [249] */
		"error 1"
		:		/* still the same error */
		,		/* back on track */
		A,
		B
	} x = A;

	return x == B;
}

/*
 * An __attribute__ at the beginning of a declaration may become ambiguous
 * since a GCC fallthrough statement starts with __attribute__ as well.
 */
void
unused_local_variable(void)
{
	__attribute__((unused)) _Bool unused_var;

	__attribute__((unused))
	__attribute__((unused)) _Bool unused_twice;
}

int
declaration_without_type_specifier(void)
{
	const i = 3;
	/* expect-1: error: old-style declaration; add 'int' [1] */
	return i;
}


/* expect+2: warning: static function 'unused' unused [236] */
static void
unused(void)
{
}

/*
 * The attribute 'used' does not influence static functions, it only
 * applies to function parameters.
 */
/* LINTED */
static void
unused_linted(void)
{
}

/* covers 'type_qualifier_list: type_qualifier_list type_qualifier' */
int *const volatile cover_type_qualifier_list;

_Bool bool;
char plain_char;
signed char signed_char;
unsigned char unsigned_char;
short signed_short;
unsigned short unsigned_short;
int signed_int;
unsigned int unsigned_int;
long signed_long;
unsigned long unsigned_long;
struct {
	int member;
} unnamed_struct;

/*
 * Before decl.c 1.201 from 2021-07-15, lint crashed with an internal error
 * in dcs_end_type (named end_type back then).
 */
unsigned long sizes =
    sizeof(const typeof(bool)) +
    sizeof(const typeof(plain_char)) +
    sizeof(const typeof(signed_char)) +
    sizeof(const typeof(unsigned_char)) +
    sizeof(const typeof(signed_short)) +
    sizeof(const typeof(unsigned_short)) +
    sizeof(const typeof(signed_int)) +
    sizeof(const typeof(unsigned_int)) +
    sizeof(const typeof(signed_long)) +
    sizeof(const typeof(unsigned_long)) +
    sizeof(const typeof(unnamed_struct));

/* expect+2: error: old-style declaration; add 'int' [1] */
/* expect+1: error: syntax error 'int' [249] */
thread int thread_int;
__thread int thread_int;
/* expect+2: error: old-style declaration; add 'int' [1] */
/* expect+1: error: syntax error 'int' [249] */
__thread__ int thread_int;

static
/* expect+1: warning: static function 'cover_func_declarator' unused [236] */
cover_func_declarator(void)
/* expect+1: error: old-style declaration; add 'int' [1] */
{
}

/*
 * Before decl.c 1.268 from 2022-04-03, lint ran into an assertion failure for
 * "elsz > 0" in 'length'.
 */
/* expect+2: error: syntax error 'goto' [249] */
/* expect+1: warning: empty array declaration for 'void_array_error' [190] */
void void_array_error[] goto;

const volatile int
/* expect+1: warning: duplicate 'const' [10] */
    *const volatile const
/* expect+1: warning: duplicate 'volatile' [10] */
    *volatile const volatile
    *duplicate_ptr;


/*
 * Since tree.c 1.573 from 2023-07-15 and before decl.c 1.370 from 2023-07-31,
 * lint crashed due to a failed assertion in find_member.  The assertion states
 * that every member of a struct or union must link back to its containing
 * type, which had not been the case for unnamed bit-fields.
 */
struct bit_and_data {
	unsigned int :0;
	unsigned int bit:1;
	unsigned int :0;

	void *data;
};

static inline void *
bit_and_data(struct bit_and_data *node)
{
	return node->data;
}


// See cgram.y, rule 'notype_member_declarator'.
void
symbol_type_in_unnamed_bit_field_member(void)
{
	enum {
		bits = 4,
	};

	struct s {
		// Since there is no name in the declarator, the next symbol
		// after the ':' must not be interpreted as a member name, but
		// instead as a variable, type or function (SK_VCFT).
		unsigned int :bits;
		int named_member;
	};
}

// Symbols that are defined in the parameter list of a function definition can
// be accessed in the body of the function, even if they are nested.
int
get_x(struct point3d { struct point3d_number { int v; } x, y, z; } arg)
{
/* expect-1: warning: dubious tag declaration 'struct point3d' [85] */
/* expect-2: warning: dubious tag declaration 'struct point3d_number' [85] */
	static struct point3d local;
	static struct point3d_number z;
	return arg.x.v + local.x.v + z.v;
}

// Expressions of the form '(size_t)&null_ptr->member' are used by several
// C implementations to implement the offsetof macro.
void
offsetof_on_array_member(void)
{
	typedef struct {
		int padding, plain, arr[2];
	} s1;

	// Bit-fields must have a constant number of bits.
	struct s2 {
		unsigned int off_plain:(unsigned long)&((s1 *)0)->plain;
		unsigned int off_arr:(unsigned long)&((s1 *)0)->arr;
		unsigned int off_arr_0:(unsigned long)&((s1 *)0)->arr[0];
		unsigned int off_arr_3:(unsigned long)&((s1 *)0)->arr[3];
	};

	// Arrays may be variable-width, but the diagnostic reveals the size.
	/* expect+1: error: negative array dimension (-4) [20] */
	typedef int off_plain[-(int)(unsigned long)&((s1 *)0)->plain];
	/* expect+1: error: negative array dimension (-8) [20] */
	typedef int off_arr[-(int)(unsigned long)&((s1 *)0)->arr];
	/* expect+1: error: negative array dimension (-8) [20] */
	typedef int off_arr_0[-(int)(unsigned long)&((s1 *)0)->arr[0]];
	/* expect+1: error: negative array dimension (-20) [20] */
	typedef int off_arr_3[-(int)(unsigned long)&((s1 *)0)->arr[3]];
}

/* PR bin/39639: writing "long double" gave "long int" */
int
long_double_vs_long_int(long double *a, long int *b)
{
	/* expect+1: warning: illegal combination of 'pointer to long double' and 'pointer to long', op '==' [124] */
	return a == b;
}

struct zero_sized_array {
	int member[0];
};

void
type_name_as_member_name(void)
{
	typedef char h[10];

	typedef struct {
		int i;
		char *c;
	} fh;

	struct foo {
		fh h;
		struct {
			int x;
			int y;
		} fl;
	};
}


// When query 16 is not enabled, don't produce a 'previous declaration' message
// without a preceding main diagnostic.
static void static_function(void) __attribute__((__used__));

// The definition is without 'static'.
void
static_function(void)
{
}


typedef void (*fprint_function)(int, const char *, ...);
typedef fprint_function (*change_logger)
    (fprint_function, fprint_function, fprint_function, fprint_function);

// Provoke a long type name to test reallocation in type_name.
/* expect+1: error: redeclaration of 'static_function' with type 'function(pointer to function(pointer to function(int, pointer to const char, ...) returning void, pointer to function(int, pointer to const char, ...) returning void, pointer to function(int, pointer to const char, ...) returning void, pointer to function(int, pointer to const char, ...) returning void) returning pointer to function(int, pointer to const char, ...) returning void) returning void', expected 'function(void) returning void' [347] */
void static_function(change_logger);
