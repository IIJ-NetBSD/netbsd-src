/*	$NetBSD: err.c,v 1.274 2025/07/08 17:43:54 rillig Exp $	*/

/*
 * Copyright (c) 1994, 1995 Jochen Pohl
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Jochen Pohl for
 *	The NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#include <sys/cdefs.h>
#if defined(__RCSID)
__RCSID("$NetBSD: err.c,v 1.274 2025/07/08 17:43:54 rillig Exp $");
#endif

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "lint1.h"

bool seen_error;
bool seen_warning;

/* number of syntax errors */
int sytxerr;


static const char *const msgs[] = {
	"empty declaration",						// 0
	"old-style declaration; add 'int'",				// 1
	"empty declaration",						// 2
	"'%s' declared in parameter declaration list",			// 3
	"invalid type combination",					// 4
	"modifying typedef with '%s'; only qualifiers allowed",		// 5
	"use 'double' instead of 'long float'",				// 6
	"only one storage class allowed",				// 7
	"invalid storage class",					// 8
	"only 'register' is valid as storage class in parameter",	// 9
	"duplicate '%s'",						// 10
	"bit-field initializer out of range",				// 11
	"compiler takes size of function",				// 12
	"incomplete enum type '%s'",					// 13
	"",								// 14
	"function returns invalid type '%s'",				// 15
	"array of function is invalid",					// 16
	"null dimension",						// 17
	"invalid use of 'void'",					// 18
	"void type for '%s'",						// 19
	"negative array dimension (%d)",				// 20
	"redeclaration of formal parameter '%s'",			// 21
	"incomplete or misplaced function definition",			// 22
	"undefined label '%s'",						// 23
	"cannot initialize function '%s'",				// 24
	"cannot initialize typedef '%s'",				// 25
	"cannot initialize extern declaration '%s'",			// 26
	"redeclaration of '%s'",					// 27
	"redefinition of '%s'",						// 28
	"'%s' was previously declared extern, becomes static",		// 29
	"redeclaration of '%s'; C90 or later require static",		// 30
	"'%s' has incomplete type '%s'",				// 31
	"type of parameter '%s' defaults to 'int'",			// 32
	"duplicate member name '%s'",					// 33
	"nonportable bit-field type '%s'",				// 34
	"invalid bit-field type '%s'",					// 35
	"invalid bit-field size: %d",					// 36
	"zero size bit-field",						// 37
	"function invalid in structure or union",			// 38
	"zero-sized array '%s' in struct requires C99 or later",	// 39
	"",			/* never used */			// 40
	"bit-field in union is very unusual",				// 41
	"forward reference to enum type",				// 42
	"redefinition of '%s' hides earlier one",			// 43
	"declaration of '%s %s' introduces new type in C90 or later",	// 44
	"base type is really '%s %s'",					// 45
	"%s tag '%s' redeclared as %s",					// 46
	"zero sized %s is a C99 feature",				// 47
	"enumeration value '%s' overflows",				// 48
	"anonymous struct/union members is a C11 feature",		// 49
	"parameter '%s' has function type, should be pointer",		// 50
	"parameter mismatch: %d declared, %d defined",			// 51
	"cannot initialize parameter '%s'",				// 52
	"declared parameter '%s' is missing",				// 53
	"trailing ',' in enum declaration requires C99 or later",	// 54
	"integral constant expression expected",			// 55
	"constant %s too large for 'int'",				// 56
	"enumeration constant '%s' hides parameter",			// 57
	"type of '%s' does not match prototype",			// 58
	"formal parameter #%d lacks name",				// 59
	"void must be sole parameter",					// 60
	"void parameter '%s' cannot have name",				// 61
	"function prototype parameters must have types",		// 62
	"prototype does not match old-style definition",		// 63
	"()-less function definition",					// 64
	"'%s' has no named members",					// 65
	"",								// 66
	"cannot return incomplete type",				// 67
	"typedef already qualified with '%s'",				// 68
	"inappropriate qualifiers with 'void'",				// 69
	"",			/* unused */				// 70
	"too many characters in character constant",			// 71
	"typedef declares no type name",				// 72
	"empty character constant",					// 73
	"no hex digits follow \\x",					// 74
	"overflow in hex escape",					// 75
	"character escape does not fit in character",			// 76
	"bad octal digit '%c'",						// 77
	"",			/* unused */				// 78
	"dubious escape \\%c",						// 79
	"dubious escape \\%o",						// 80
	"\\a requires C90 or later",					// 81
	"\\x requires C90 or later",					// 82
	"storage class after type is obsolescent",			// 83
	"C90 to C17 require formal parameter before '...'",		// 84
	"dubious tag declaration '%s %s'",				// 85
	"automatic '%s' hides external declaration with type '%s'",	// 86
	"static '%s' hides external declaration with type '%s'",	// 87
	"typedef '%s' hides external declaration with type '%s'",	// 88
	"typedef '%s' redeclared",					// 89
	"inconsistent redeclaration of extern '%s'",			// 90
	"declaration of '%s' hides parameter",				// 91
	"inconsistent redeclaration of static '%s'",			// 92
	"dubious static function '%s' at block level",			// 93
	"function '%s' has invalid storage class",			// 94
	"declaration of '%s' hides earlier one",			// 95
	"cannot dereference non-pointer type '%s'",			// 96
	"suffix 'U' requires C90 or later",				// 97
	"suffixes 'F' or 'L' require C90 or later",			// 98
	"'%s' undefined",						// 99
	"unary '+' requires C90 or later",				// 100
	"type '%s' does not have member '%s'",				// 101
	"invalid use of member '%s'",					// 102
	"left operand of '.' must be struct or union, not '%s'",	// 103
	"left operand of '->' must be pointer to struct or union, not '%s'", // 104
	"non-unique member requires struct/union %s",			// 105
	"left operand of '->' must be pointer",				// 106
	"operands of '%s' have incompatible types '%s' and '%s'",	// 107
	"operand of '%s' has invalid type '%s'",			// 108
	"void type invalid in expression",				// 109
	"pointer to function is not allowed here",			// 110
	"unacceptable operand of '%s'",					// 111
	"cannot take address of bit-field",				// 112
	"cannot take address of register '%s'",				// 113
	"%soperand of '%s' must be lvalue",				// 114
	"%soperand of '%s' must be modifiable lvalue",			// 115
	"invalid pointer subtraction",					// 116
	"bitwise '%s' on signed value possibly nonportable",		// 117
	"semantics of '%s' change in C90; use explicit cast",		// 118
	"conversion of '%s' to '%s' is out of range",			// 119
	"bitwise '%s' on signed value nonportable",			// 120
	"negative shift",						// 121
	"shift amount %llu is greater than bit-size %llu of '%s'",	// 122
	"invalid combination of %s '%s' and %s '%s', op '%s'",		// 123
	"invalid combination of '%s' and '%s', op '%s'",		// 124
	"pointers to functions can only be compared for equality",	// 125
	"incompatible types '%s' and '%s' in conditional",		// 126
	"",			/* no longer used */			// 127
	"operator '%s' discards '%s' from '%s'",			// 128
	"expression has null effect",					// 129
	"enum type mismatch: '%s' '%s' '%s'",				// 130
	"conversion to '%s' may sign-extend incorrectly",		// 131
	"conversion from '%s' to '%s' may lose accuracy",		// 132
	"conversion of pointer to '%s' loses bits",			// 133
	"conversion of pointer to '%s' may lose bits",			// 134
	"converting '%s' to '%s' increases alignment from %u to %u",	// 135
	"cannot do pointer arithmetic on operand of unknown size",	// 136
	"",			/* unused */				// 137
	"unknown operand size, op '%s'",				// 138
	"division by 0",						// 139
	"modulus by 0",							// 140
	"'%s' overflows '%s'",						// 141
	"operator '%s' produces floating point overflow",		// 142
	"cannot take size/alignment of incomplete type",		// 143
	"cannot take size/alignment of function type '%s'",		// 144
	"cannot take size/alignment of bit-field",			// 145
	"cannot take size/alignment of void",				// 146
	"invalid cast from '%s' to '%s'",				// 147
	"improper cast of void expression",				// 148
	"cannot call '%s', must be a function",				// 149
	"argument mismatch: %d %s passed, %d expected",			// 150
	"void expressions may not be arguments, arg #%d",		// 151
	"argument cannot have unknown size, arg #%d",			// 152
	"converting '%s' to incompatible '%s' for argument %d",		// 153
	"invalid combination of %s '%s' and %s '%s', arg #%d",		// 154
	"passing '%s' to incompatible '%s', arg #%d",			// 155
	"function expects '%s', passing '%s' for arg #%d",		// 156
	"C90 treats constant as unsigned",				// 157
	"'%s' may be used before set",					// 158
	"assignment in conditional context",				// 159
	"operator '==' found where '=' was expected",			// 160
	"",			/* no longer used */			// 161
	"operator '%s' compares '%s' with '%s'",			// 162
	"a cast does not yield an lvalue",				// 163
	"assignment of negative constant %lld to unsigned type '%s'",	// 164
	"constant truncated by assignment",				// 165
	"precision lost in bit-field assignment",			// 166
	"array subscript %jd cannot be negative",			// 167
	"array subscript %ju cannot be > %d",				// 168
	"possible precedence confusion between '%s' and '%s'",		// 169
	"first operand of '?' must have scalar type",			// 170
	"cannot assign to '%s' from '%s'",				// 171
	"too many struct/union initializers",				// 172
	"too many array initializers, expected %d",			// 173
	"too many initializers for '%s'",				// 174
	"initialization of incomplete type '%s'",			// 175
	"",			/* no longer used */			// 176
	"non-constant initializer",					// 177
	"initializer does not fit",					// 178
	"cannot initialize struct/union with no named member",		// 179
	"bit-field initializer does not fit",				// 180
	"{}-enclosed or constant initializer of type '%s' required",	// 181
	"'%s' discards '%s' from '%s'",					// 182
	"invalid combination of %s '%s' and %s '%s' for '%s'",		// 183
	"invalid combination of '%s' and '%s'",				// 184
	"cannot initialize '%s' from '%s'",				// 185
	"bit-field initializer must be an integer in traditional C",	// 186
	"string literal too long (%ju) for target array (%ju)",		// 187
	"automatic aggregate initialization requires C90 or later",	// 188
	"",			/* no longer used */			// 189
	"empty array declaration for '%s'",				// 190
	"'%s' set but not used in function '%s'",			// 191
	"'%s' unused in function '%s'",					// 192
	"'%s' statement not reached",					// 193
	"label '%s' redefined",						// 194
	"case not in switch",						// 195
	"case label is converted from '%s' to '%s'",			// 196
	"non-constant case expression",					// 197
	"non-integral case expression",					// 198
	"duplicate case '%jd' in switch",				// 199
	"duplicate case '%ju' in switch",				// 200
	"default outside switch",					// 201
	"duplicate default in switch",					// 202
	"case label must be of type 'int' in traditional C",		// 203
	"controlling expressions must have scalar type",		// 204
	"switch expression must have integral type",			// 205
	"enumeration value(s) not handled in switch",			// 206
	"",			/* no longer used */			// 207
	"break outside loop or switch",					// 208
	"continue outside loop",					// 209
	"enum type mismatch between '%s' and '%s' in initialization",	// 210
	"function has return type '%s' but returns '%s'",		// 211
	"cannot return incomplete type",				// 212
	"void function '%s' cannot return value",			// 213
	"function '%s' expects to return value",			// 214
	"function '%s' implicitly declared to return int",		// 215
	"function '%s' has 'return expr' and 'return'",			// 216
	"function '%s' falls off bottom without returning value",	// 217
	"C90 treats constant as unsigned, op '%s'",			// 218
	"concatenated strings require C90 or later",			// 219
	"fallthrough on case statement",				// 220
	"initialization of unsigned type '%s' with negative constant %lld", // 221
	"conversion of negative constant %lld to unsigned type '%s'",	// 222
	"end-of-loop code not reached",					// 223
	"cannot recover from previous errors",				// 224
	"static function '%s' called but not defined",			// 225
	"static variable '%s' unused",					// 226
	"const object '%s' should have initializer",			// 227
	"function cannot return const or volatile object",		// 228
	"converting '%s' to '%s' is questionable",			// 229
	"nonportable character comparison '%s'",			// 230
	"parameter '%s' unused in function '%s'",			// 231
	"label '%s' unused in function '%s'",				// 232
	"struct '%s' never defined",					// 233
	"union '%s' never defined",					// 234
	"enum '%s' never defined",					// 235
	"static function '%s' unused",					// 236
	"redeclaration of formal parameter '%s'",			// 237
	"initialization of union requires C90 or later",		// 238
	"",			/* no longer used */			// 239
	"",			/* unused */				// 240
	"dubious operation '%s' on enum",				// 241
	"combination of '%s' and '%s', op '%s'",			// 242
	"operator '%s' assumes that '%s' is ordered",			// 243
	"invalid structure pointer combination",			// 244
	"incompatible structure pointers: '%s' '%s' '%s'",		// 245
	"dubious conversion of enum to '%s'",				// 246
	"pointer cast from '%s' to unrelated '%s'",			// 247
	"floating-point constant out of range",				// 248
	"syntax error '%s'",						// 249
	"unknown character \\%o",					// 250
	"malformed integer constant",					// 251
	"integer constant out of range",				// 252
	"unterminated character constant",				// 253
	"newline in string or char constant",				// 254
	"undefined or invalid '#' directive",				// 255
	"unterminated comment",						// 256
	"extra characters in lint comment",				// 257
	"unterminated string constant",					// 258
	"argument %d is converted from '%s' to '%s' due to prototype",	// 259
	"previous declaration of '%s'",					// 260
	"previous definition of '%s'",					// 261
	"\\\" inside a character constant requires C90 or later",	// 262
	"\\? requires C90 or later",					// 263
	"\\v requires C90 or later",					// 264
	"%s does not support 'long long'",				// 265
	"'long double' requires C90 or later",				// 266
	"shift amount %u equals bit-size of '%s'",			// 267
	"variable '%s' declared inline",				// 268
	"parameter '%s' declared inline",				// 269
	"function prototypes require C90 or later",			// 270
	"switch expression must be of type 'int' in traditional C",	// 271
	"empty translation unit",					// 272
	"bit-field type '%s' invalid in C90 or later",			// 273
	"C90 or later forbid comparison of %s with %s",			// 274
	"cast discards 'const' from type '%s'",				// 275
	"'__%s__' is invalid for type '%s'",				// 276
	"initialization of '%s' with '%s'",				// 277
	"combination of '%s' and '%s', arg #%d",			// 278
	"combination of '%s' and '%s' in return",			// 279
	"comment /* %s */ must be outside function",			// 280
	"duplicate comment /* %s */",					// 281
	"comment /* %s */ must precede function definition",		// 282
	"parameter number mismatch in comment /* %s */",		// 283
	"fallthrough on default statement",				// 284
	"prototype declaration",					// 285
	"function definition is not a prototype",			// 286
	"function declaration is not a prototype",			// 287
	"dubious use of /* VARARGS */ with /* %s */",			// 288
	"/* PRINTFLIKE */ and /* SCANFLIKE */ cannot be combined",	// 289
	"static function '%s' declared but not defined",		// 290
	"invalid multibyte character",					// 291
	"cannot concatenate wide and regular string literals",		// 292
	"parameter %d must be 'char *' for PRINTFLIKE/SCANFLIKE",	// 293
	"multi-character character constant",				// 294
	"conversion of '%s' to '%s' is out of range, arg #%d",		// 295
	"conversion of negative constant %lld to unsigned type '%s', arg #%d", // 296
	"conversion to '%s' may sign-extend incorrectly, arg #%d",	// 297
	"conversion from '%s' to '%s' may lose accuracy, arg #%d",	// 298
	"prototype does not match old-style definition, arg #%d",	// 299
	"old-style definition",						// 300
	"array of incomplete type",					// 301
	"'%s' returns pointer to automatic object",			// 302
	"conversion of %s to %s requires a cast",			// 303
	"conversion of %s to %s requires a cast, arg #%d",		// 304
	"conversion of %s to %s requires a cast, op %s",		// 305
	"constant %s truncated by conversion, op '%s'",			// 306
	"static variable '%s' set but not used",			// 307
	"invalid type for _Complex",					// 308
	"'%s' converts '%s' with its most significant bit being set to '%s'", // 309
	"symbol renaming can't be used on function parameters",		// 310
	"symbol renaming can't be used on automatic variables",		// 311
	"%s does not support '//' comments",				// 312
	"struct or union member name in initializer is a C99 feature",	// 313
	"",		/* never used */				// 314
	"GCC style struct or union member name in initializer",		// 315
	"__FUNCTION__/__PRETTY_FUNCTION__ is a GCC extension",		// 316
	"__func__ is a C99 feature",					// 317
	"variable array dimension is a C99/GCC extension",		// 318
	"compound literals are a C99/GCC extension",			// 319
	"'({ ... })' is a GCC extension",				// 320
	"array initializer with designators is a C99 feature",		// 321
	"zero sized array requires C99 or later",			// 322
	"continue in 'do ... while (0)' loop",				// 323
	"suggest cast from '%s' to '%s' on op '%s' to avoid overflow",	// 324
	"variable declaration in for loop",				// 325
	"attribute '%s' ignored for '%s'",				// 326
	"declarations after statements is a C99 feature",		// 327
	"union cast is a GCC extension",				// 328
	"type '%s' is not a member of '%s'",				// 329
	"operand of '%s' must be bool, not '%s'",			// 330
	"left operand of '%s' must be bool, not '%s'",			// 331
	"right operand of '%s' must be bool, not '%s'",			// 332
	"controlling expression must be bool, not '%s'",		// 333
	"parameter %d expects '%s', gets passed '%s'",			// 334
	"operand of '%s' must not be bool",				// 335
	"left operand of '%s' must not be bool",			// 336
	"right operand of '%s' must not be bool",			// 337
	"option '%c' should be handled in the switch",			// 338
	"option '%c' should be listed in the options string",		// 339
	"initialization with '[a...b]' is a GCC extension",		// 340
	"argument to '%s' must be 'unsigned char' or EOF, not '%s'",	// 341
	"argument to '%s' must be cast to 'unsigned char', not to '%s'", // 342
	"static array size requires C11 or later",			// 343
	"bit-field of type plain 'int' has implementation-defined signedness", // 344
	"generic selection requires C11 or later",			// 345
	"call to '%s' effectively discards 'const' from argument",	// 346
	"redeclaration of '%s' with type '%s', expected '%s'",		// 347
	"maximum value %d for '%s' of type '%s' does not match maximum array index %d", // 348
	"non type argument to alignof is a GCC extension",		// 349
	"'_Atomic' requires C11 or later",				// 350
	"missing%s header declaration for '%s'",			// 351
	"nested 'extern' declaration of '%s'",				// 352
	"empty initializer braces require C23 or later",		// 353
	"'_Static_assert' requires C11 or later",			// 354
	"'_Static_assert' without message requires C23 or later",	// 355
	"short octal escape '%.*s' followed by digit '%c'",		// 356
	"hex escape '%.*s' mixes uppercase and lowercase digits",	// 357
	"hex escape '%.*s' has more than 2 digits",			// 358
	"missing new-style '\\177' or old-style number base",		// 359
	"missing new-style number base after '\\177'",			// 360
	"number base '%.*s' is %ju, must be 8, 10 or 16",		// 361
	"conversion '%.*s' should not be escaped",			// 362
	"escaped character '%.*s' in description of conversion '%.*s'", // 363
	"missing bit position after '%.*s'",				// 364
	"missing field width after '%.*s'",				// 365
	"missing '\\0' at the end of '%.*s'",				// 366
	"empty description in '%.*s'",					// 367
	"missing comparison value after conversion '%.*s'",		// 368
	"bit position '%.*s' in '%.*s' should be escaped as octal or hex", // 369
	"field width '%.*s' in '%.*s' should be escaped as octal or hex", // 370
	"bit position '%.*s' (%ju) in '%.*s' out of range %u..%u",	// 371
	"field width '%.*s' (%ju) in '%.*s' out of range 0..64",	// 372
	"bit field end %ju in '%.*s' out of range 0..64",		// 373
	"unknown conversion '%.*s', must be one of 'bfF=:*'",		// 374
	"comparison value '%.*s' (%ju) exceeds maximum field value %ju", // 375
	"'%.*s' overlaps earlier '%.*s' on bit %u",			// 376
	"redundant '\\0' at the end of the format",			// 377
	"conversion '%.*s' is unreachable by input value",		// 378
	"comparing integer '%s' to floating point constant %Lg",	// 379
	"lossy conversion of %Lg to '%s', arg #%d",			// 380
	"lossy conversion of %Lg to '%s'",				// 381
	"constant assignment of type '%s' in operand of '%s' always evaluates to '%s'", // 382
	"passing '%s' as argument %d to '%s' discards '%s'",		// 383
	"function definition for '%s' with identifier list is obsolete in C23", // 384
	"do-while macro '%.*s' ends with semicolon",			// 385
};

static bool is_suppressed[sizeof(msgs) / sizeof(msgs[0])];

static struct include_level {
	const char *filename;
	int lineno;
	struct include_level *by;
} *includes;

void
suppress_messages(const char *p)
{
	char *end;

	for (; ch_isdigit(*p); p = end + 1) {
		unsigned long id = strtoul(p, &end, 10);
		if ((*end != '\0' && *end != ',') ||
		    id >= sizeof(msgs) / sizeof(msgs[0]) ||
		    msgs[id][0] == '\0')
			break;

		is_suppressed[id] = true;

		if (*end == '\0')
			return;
	}
	errx(1, "invalid message ID '%.*s'", (int)strcspn(p, ","), p);
}

void
update_location(const char *filename, int lineno, bool is_begin, bool is_end)
{
	struct include_level *top;

	top = includes;
	if (is_begin && top != NULL)
		top->lineno = curr_pos.p_line;

	if (top == NULL || is_begin) {
		top = xmalloc(sizeof(*top));
		top->filename = filename;
		top->lineno = lineno;
		top->by = includes;
		includes = top;
	} else {
		if (is_end) {
			includes = top->by;
			free(top);
			top = includes;
		}
		if (top != NULL) {
			top->filename = filename;
			top->lineno = lineno;
		}
	}
}

static void
print_stack_trace(void)
{
	const struct include_level *top;

	if ((top = includes) == NULL)
		return;
	/*
	 * Skip the innermost include level since it is already listed in the
	 * diagnostic itself.  Furthermore, its lineno is the line number of
	 * the last '#' line, not the current line.
	 */
	for (top = top->by; top != NULL; top = top->by)
		printf("\tincluded from %s(%d)\n", top->filename, top->lineno);
}

/*
 * If Fflag is not set, lbasename() returns a pointer to the last
 * component of the path, otherwise it returns the argument.
 */
static const char *
lbasename(const char *path)
{

	if (Fflag)
		return path;

	const char *base = path;
	for (const char *p = path; *p != '\0'; p++)
		if (*p == '/')
			base = p + 1;
	return base;
}

static FILE *
output_channel(void)
{
	return yflag ? stderr : stdout;
}

static void
verror_at(int msgid, const pos_t *pos, va_list ap)
{

	if (is_suppressed[msgid])
		return;

	FILE *out = output_channel();
	(void)fprintf(out, "%s(%d): error: ",
	    lbasename(pos->p_file), pos->p_line);
	(void)vfprintf(out, msgs[msgid], ap);
	(void)fprintf(out, " [%d]\n", msgid);
	seen_error = true;
	print_stack_trace();
}

static void
vwarning_at(int msgid, const pos_t *pos, va_list ap)
{

	if (is_suppressed[msgid])
		return;

	debug_step("%s: lwarn=%d msgid=%d", __func__, lwarn, msgid);
	if (lwarn == LWARN_NONE || lwarn == msgid)
		/* this warning is suppressed by a LINTED comment */
		return;

	FILE *out = output_channel();
	(void)fprintf(out, "%s(%d): warning: ",
	    lbasename(pos->p_file), pos->p_line);
	(void)vfprintf(out, msgs[msgid], ap);
	(void)fprintf(out, " [%d]\n", msgid);
	seen_warning = true;
	print_stack_trace();
}

static void
vmessage_at(int msgid, const pos_t *pos, va_list ap)
{

	if (is_suppressed[msgid])
		return;

	FILE *out = output_channel();
	(void)fprintf(out, "%s(%d): ",
	    lbasename(pos->p_file), pos->p_line);
	(void)vfprintf(out, msgs[msgid], ap);
	(void)fprintf(out, " [%d]\n", msgid);
	print_stack_trace();
}

void
(error_at)(int msgid, const pos_t *pos, ...)
{
	va_list ap;

	va_start(ap, pos);
	verror_at(msgid, pos, ap);
	va_end(ap);
}

void
(error)(int msgid, ...)
{
	va_list ap;

	va_start(ap, msgid);
	verror_at(msgid, &curr_pos, ap);
	va_end(ap);
}

void
assert_failed(const char *file, int line, const char *func, const char *cond)
{

#if LINT_FUZZING
	/*
	 * After encountering a parse error in the grammar, lint often does not
	 * properly clean up its data structures, especially in 'dcs', the
	 * stack of declaration levels.  This often leads to assertion
	 * failures.  These cases are not interesting though, as the purpose of
	 * lint is to check syntactically valid code.  In such a case, exit
	 * gracefully.  This allows a fuzzer like afl to focus on more
	 * interesting cases instead of reporting nonsense translation units
	 * like 'f=({e:;}' or 'v(const(char););e(v){'.
	 */
	if (sytxerr > 0)
		norecover();
#endif

	(void)fflush(stdout);
	(void)fprintf(stderr,
	    "lint: assertion \"%s\" failed in %s at %s:%d near %s:%d\n",
	    cond, func, file, line,
	    lbasename(curr_pos.p_file), curr_pos.p_line);
	print_stack_trace();
	(void)fflush(stdout);
	abort();
}

void
(warning_at)(int msgid, const pos_t *pos, ...)
{
	va_list ap;

	va_start(ap, pos);
	vwarning_at(msgid, pos, ap);
	va_end(ap);
}

void
(warning)(int msgid, ...)
{
	va_list ap;

	va_start(ap, msgid);
	vwarning_at(msgid, &curr_pos, ap);
	va_end(ap);
}

void
(message_at)(int msgid, const pos_t *pos, ...)
{
	va_list ap;

	va_start(ap, pos);
	vmessage_at(msgid, pos, ap);
	va_end(ap);
}

void
(c99ism)(int msgid, ...)
{
	va_list ap;

	if (allow_c99)
		return;

	va_start(ap, msgid);
	int severity = (!allow_gcc ? 1 : 0) + (!allow_trad ? 1 : 0);
	if (severity == 2)
		verror_at(msgid, &curr_pos, ap);
	if (severity == 1)
		vwarning_at(msgid, &curr_pos, ap);
	va_end(ap);
}

void
(c11ism)(int msgid, ...)
{
	va_list ap;

	/* FIXME: C11 mode has nothing to do with GCC mode. */
	if (allow_c11 || allow_gcc)
		return;
	va_start(ap, msgid);
	verror_at(msgid, &curr_pos, ap);
	va_end(ap);
}

void
(c23ism)(int msgid, ...)
{
	va_list ap;

	if (allow_c23)
		return;
	va_start(ap, msgid);
	verror_at(msgid, &curr_pos, ap);
	va_end(ap);
}

bool
(gnuism)(int msgid, ...)
{
	va_list ap;
	int severity = (!allow_gcc ? 1 : 0) +
	    (!allow_trad && !allow_c99 ? 1 : 0);

	va_start(ap, msgid);
	if (severity == 2)
		verror_at(msgid, &curr_pos, ap);
	if (severity == 1)
		vwarning_at(msgid, &curr_pos, ap);
	va_end(ap);
	return severity > 0;
}


static const char *queries[] = {
	"",			/* unused, to make queries 1-based */
	"implicit conversion from floating point '%s' to integer '%s'",	// Q1
	"cast from floating point '%s' to integer '%s'",		// Q2
	"implicit conversion changes sign from '%s' to '%s'",		// Q3
	"usual arithmetic conversion for '%s' from '%s' to '%s'",	// Q4
	"pointer addition has integer on the left-hand side",		// Q5
	"no-op cast from '%s' to '%s'",					// Q6
	"redundant cast from '%s' to '%s' before assignment",		// Q7
	"octal number '%.*s'",						// Q8
	"parenthesized return value",					// Q9
	"chained assignment with '%s' and '%s'",			// Q10
	"static variable '%s' in function",				// Q11
	"comma operator with types '%s' and '%s'",			// Q12
	"redundant 'extern' in function declaration of '%s'",		// Q13
	"comparison '%s' of 'char' with plain integer %d",		// Q14
	"implicit conversion from integer 0 to pointer '%s'",		// Q15
	"'%s' was declared 'static', now non-'static'",			// Q16
	"invisible character U+%04X in %s",				// Q17
	"const automatic variable '%s'",				// Q18
	"implicit conversion from integer '%s' to floating point '%s'",	// Q19
	"implicit narrowing conversion from void pointer to '%s'",	// Q20
	"typedef '%s' of struct type '%s'",				// Q21
	"typedef '%s' of union type '%s'",				// Q22
	"typedef '%s' of pointer to struct type '%s'",			// Q23
	"typedef '%s' of pointer to union type '%s'",			// Q24
};

// Omit any expensive computations in the default mode where none of the
// queries are enabled.  Function calls in message details don't need to be
// guarded by this flag, as that happens in the query_message macro already.
bool any_query_enabled;
bool is_query_enabled[sizeof(queries) / sizeof(queries[0])];

void
(query_message)(int query_id, ...)
{

	if (!is_query_enabled[query_id])
		return;

	va_list ap;
	FILE *out = output_channel();
	(void)fprintf(out, "%s(%d): ",
	    lbasename(curr_pos.p_file), curr_pos.p_line);
	va_start(ap, query_id);
	(void)vfprintf(out, queries[query_id], ap);
	va_end(ap);
	(void)fprintf(out, " [Q%d]\n", query_id);
	print_stack_trace();
}

void
enable_queries(const char *p)
{
	char *end;

	for (; ch_isdigit(*p); p = end + 1) {
		unsigned long id = strtoul(p, &end, 10);
		if ((*end != '\0' && *end != ',') ||
		    id >= sizeof(queries) / sizeof(queries[0]) ||
		    queries[id][0] == '\0')
			break;

		any_query_enabled = true;
		is_query_enabled[id] = true;

		if (*end == '\0')
			return;
	}
	errx(1, "invalid query ID '%.*s'", (int)strcspn(p, ","), p);
}
