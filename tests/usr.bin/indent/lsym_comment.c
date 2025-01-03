/* $NetBSD: lsym_comment.c,v 1.25 2025/01/03 23:37:18 rillig Exp $ */

/*
 * Tests for the token lsym_comment, which starts a comment.
 *
 * C11 distinguishes block comments and end-of-line comments.  Indent further
 * distinguishes box comments that are a special kind of block comments.
 *
 * See also:
 *	opt_fc1.c
 *	lsym_comment.c
 */

/*-
 * TODO: systematically test comments
 *
 * - starting in column 1, with opt.format_col1_comments (-fc1)
 * - starting in column 1, without opt.format_col1_comments (-fc1)
 * - starting in column 9, independent of opt.format_col1_comments (-fc1)
 * - starting in column 33, the default
 * - starting in column 65, which is already close to the default right margin
 * - starting in column 81, spilling into the right margin
 *
 * - block comment starting with '/' '*' '-'
 * - block comment starting with '/' '*' '*'
 * - block comment starting with '/' '*' '\n'
 * - end-of-line comment starting with '//'
 * - end-of-line comment starting with '//x', so without leading space
 * - block comment starting with '/' '*' 'x', so without leading space
 *
 * - block/end-of-line comment to the right of a label
 * - block/end-of-line comment to the right of code
 * - block/end-of-line comment to the right of label with code
 *
 * - with/without opt.comment_delimiter_on_blank_line (-cdb)
 * - with/without opt.star_comment_cont (-sc)
 * - with/without opt.format_block_comments (-fbc)
 * - with varying opt.max_line_length (32, 64, 80, 140)
 * - with varying opt.unindent_displace (-d0, -d2, -d-5)
 * - with varying opt.indent_size (3, 4, 8)
 * - with varying opt.tabsize (3, 4, 8, 16)
 * - with varying opt.block_comment_max_line_length (-lc60, -lc78, -lc90)
 * - with varying opt.comment_column (-c0, -c1, -c33, -c80)
 * - with varying opt.decl_comment_column (-cd0, -cd1, -cd20, -cd33, -cd80)
 * - with/without ps.line_has_decl
 *
 * - very long comments that overflow the buffer 'com'
 * - comments that come from save_com
 * - very long word that already spills over the right margin
 * - wrap/nowrap comment containing '\n'
 * - wrap/nowrap comment containing '\f'
 * - wrap/nowrap comment containing '\t'
 */

//indent input
typedef enum x {
	aaaaaaaaaaaaaaaaaaaaaa = 1 << 0,	/* test a */
	bbbbbbbbbbbbbbbbb = 1 << 1,	/* test b */
	cccccccccccccc = 1 << 1,	/* test c */
	dddddddddddddddddddddddddddddd = 1 << 2	/* test d */
} x;
//indent end

//indent run-equals-input -bbb


//indent input
/* See FreeBSD r303597, r303598, r309219, and r309343 */
void
t(void) {
	/*
	 * Old indent wrapped the URL near where this sentence ends.
	 *
	 * https://www.freebsd.org/cgi/man.cgi?query=indent&apropos=0&sektion=0&manpath=FreeBSD+12-current&arch=default&format=html
	 */

	/*
	 * The default maximum line length for comments is 78, and the 'kk' at
	 * the end makes the line exactly 78 bytes long.
	 *
	 * aaaaaa bbbbbb cccccc dddddd eeeeee ffffff ggggg hhhhh iiiii jjjj kk
	 */

	/*
	 * Old indent unnecessarily removed the star comment continuation on the next line.
	 *
	 * *test*
	 */

	/* r309219 Go through linked list, freeing from the malloced (t[-1]) address. */

	/* r309343	*/
}
//indent end

//indent run -bbb
/* See FreeBSD r303597, r303598, r309219, and r309343 */
void
t(void)
{

	/*
	 * Old indent wrapped the URL near where this sentence ends.
	 *
	 * https://www.freebsd.org/cgi/man.cgi?query=indent&apropos=0&sektion=0&manpath=FreeBSD+12-current&arch=default&format=html
	 */

	/*
	 * The default maximum line length for comments is 78, and the 'kk' at
	 * the end makes the line exactly 78 bytes long.
	 *
	 * aaaaaa bbbbbb cccccc dddddd eeeeee ffffff ggggg hhhhh iiiii jjjj kk
	 */

	/*
	 * Old indent unnecessarily removed the star comment continuation on
	 * the next line.
	 *
	 * *test*
	 */

	/*
	 * r309219 Go through linked list, freeing from the malloced (t[-1])
	 * address.
	 */

	/* r309343	*/
}
//indent end


/*
 * The first Christmas tree is to the right of the code, therefore the comment
 * is moved to the code comment column; the follow-up lines of that comment
 * are moved by the same distance, to preserve the internal layout.
 *
 * The other Christmas tree is a standalone block comment, therefore the
 * comment starts in the code column.
 */
//indent input
{
	if (1) /*- a Christmas tree  *
				    ***
				   ***** */
		    /*- another one *
				   ***
				  ***** */
		1;
}
//indent end

//indent run -bbb
{
	if (1)			/*- a Christmas tree  *
						     ***
						    ***** */
		/*- another one *
			       ***
			      ***** */
		1;
}
//indent end


/*
 * The first Christmas tree is to the right of the code, therefore the comment
 * is moved to the code comment column; the follow-up lines of that comment
 * are moved by the same distance, to preserve the internal layout.
 *
 * The other Christmas tree is a standalone block comment, therefore the
 * comment starts in the code column.
 */
//indent input
{
	if (7) { /*- a Christmas tree  *
				      ***
				     ***** */
		    /*- another one *
				   ***
				  ***** */
		stmt();
	}
}
//indent end

//indent run -bbb
{
	if (7) {		/*- a Christmas tree  *
					             ***
					            ***** */
		/*- another one *
			       ***
			      ***** */
		stmt();
	}
}
//indent end


//indent input
int decl;/*-fixed comment
	    fixed comment*/
//indent end

//indent run -di0
int decl;			/*-fixed comment
			           fixed comment*/
//indent end
/*
 * XXX: The second line of the above comment contains 11 spaces in a row,
 * instead of using as many tabs as possible.
 */


//indent input
{
	if (0)/*-first line            |
	   second line                 |*/
		;
}
//indent end

//indent run -di0
{
	if (0)			/*-first line            |
			     second line                 |*/
		;
}
//indent end


/*
 * Ensure that all text of the comment is preserved when the comment is moved
 * to the right.
 */
//indent input
int decl;/*-fixed comment
123456789ab fixed comment*/
//indent end

//indent run -di0
int decl;			/*-fixed comment
		       123456789ab fixed comment*/
//indent end


/*
 * Ensure that all text of the comment is preserved when the comment is moved
 * to the right.
 */
//indent input
{
	if(0)/*-first line
123456789ab second line           |*/
	    ;
}
//indent end

//indent run -di0
{
	if (0)			/*-first line
		   123456789ab second line           |*/
		;
}
//indent end


/*
 * Ensure that all text of the comment is preserved when the comment is moved
 * to the left. In this case, the internal layout of the comment cannot be
 * preserved since the second line already starts in column 1.
 */
//indent input
int decl;					    /*-|fixed comment
					| minus 12     |
		| tabs inside		|
	    |---|
|-----------|
tab1+++	tab2---	tab3+++	tab4---	tab5+++	tab6---	tab7+++fixed comment*/
//indent end

//indent run -di0
int decl;			/*-|fixed comment
		    | minus 12     |
| tabs inside		|
|---|
|-----------|
tab1+++	tab2---	tab3+++	tab4---	tab5+++	tab6---	tab7+++fixed comment*/
//indent end


/*
 * Ensure that all text of the comment is preserved when the comment is moved
 * to the left. In this case, the internal layout of the comment cannot be
 * preserved since the second line already starts in column 1.
 */
//indent input
{
	if(0)					    /*-|first line
					| minus 12     |
		| tabs inside		|
	    |---|
|-----------|
tab1+++	tab2---	tab3+++	tab4---	tab5+++	tab6---	tab7+++fixed comment*/
		;
}
//indent end

//indent run -di0
{
	if (0)			/*-|first line
		    | minus 12     |
| tabs inside		|
|---|
|-----------|
tab1+++	tab2---	tab3+++	tab4---	tab5+++	tab6---	tab7+++fixed comment*/
		;
}
//indent end


/*
 * Ensure that '{' after a comment is preserved.
 */
//indent input
{
	if(0)/*comment*/{
	}
}
//indent end

/* Before 2023-05-11, the comment and the '{' swapped places. */
//indent run
{
	if (0) /* comment */ {
	}
}
//indent end


/*
 * The following comments test line breaking when the comment ends with a
 * space.
 */
//indent input
/* 456789 123456789 123456789 12345 */
/* 456789 123456789 123456789 123456 */
/* 456789 123456789 123456789 1234567 */
/* 456789 123456789 123456789 12345678 */
/* 456789 123456789 123456789 123456789 */
//indent end

//indent run -l38
/* 456789 123456789 123456789 12345 */
/*
 * 456789 123456789 123456789 123456
 */
/*
 * 456789 123456789 123456789 1234567
 */
/*
 * 456789 123456789 123456789 12345678
 */
/*
 * 456789 123456789 123456789
 * 123456789
 */
//indent end


/*
 * When determining whether the comment fits in a single line, only the first
 * trailing space or tab is kept, the others are removed.
 */
//indent input
/* tab: */
/* 456789 123456789 123456789 12345		*/
/* 456789 123456789 123456789 123456		*/
/* space: */
/* 456789 123456789 123456789 12345             */
/* 456789 123456789 123456789 123456            */
//indent end

//indent run -l38
/* tab: */
/*
 * 456789 123456789 123456789 12345
 */
/*
 * 456789 123456789 123456789 123456
 */
/* space: */
/* 456789 123456789 123456789 12345 */
/*
 * 456789 123456789 123456789 123456
 */
//indent end


/*
 * The following comments test line breaking when the comment does not end
 * with a space. Since indent adds a trailing space to a single-line comment,
 * this space has to be taken into account when computing the line length.
 */
//indent input
/* x		. line length 35*/
/* x		.. line length 36*/
/* x		... line length 37*/
/* x		.... line length 38*/
/* x		..... line length 39*/
/* x		...... line length 40*/
/* x		....... line length 41*/
/* x		........ line length 42*/
//indent end

//indent run -l38
/* x		. line length 35 */
/* x		.. line length 36 */
/* x		... line length 37 */
/* x		.... line length 38 */
/*
 * x		..... line length 39
 */
/*
 * x		...... line length 40
 */
/*
 * x		....... line length 41
 */
/*
 * x		........ line length 42
 */
//indent end


/*
 * The different types of comments that indent distinguishes, starting in
 * column 1 (see options '-fc1' and '-nfc1').
 */
//indent input
/* This is a traditional C block comment. */

// This is a C99 line comment.

/*
 * This is a box comment since its first line (the one above this line) is
 * empty.
 *
 *
 *
 * Its text gets wrapped.
 * Empty lines serve as paragraphs.
 */

/**
 * This is a box comment
 * that is not re-wrapped.
 */

/*-
 * This is a box comment
 * that is not re-wrapped.
 * It is often used for copyright declarations.
 */
//indent end

//indent run
/* This is a traditional C block comment. */

// This is a C99 line comment.

/*
 * This is a box comment since its first line (the one above this line) is
 * empty.
 *
 *
 *
 * Its text gets wrapped. Empty lines serve as paragraphs.
 */

/**
 * This is a box comment
 * that is not re-wrapped.
 */

/*-
 * This is a box comment
 * that is not re-wrapped.
 * It is often used for copyright declarations.
 */
//indent end


/*
 * The different types of comments that indent distinguishes, starting in
 * column 9, so they are independent of the option '-fc1'.
 */
//indent input
void
function(void)
{
	/* This is a traditional C block comment. */

	/*
	 * This is a box comment.
	 *
	 * It starts in column 9, not 1,
	 * therefore it gets re-wrapped.
	 */

	/**
	 * This is a box comment
	 * that is not re-wrapped, even though it starts in column 9, not 1.
	 */

	/*-
	 * This is a box comment
	 * that is not re-wrapped.
	 */
}
//indent end

//indent run
void
function(void)
{
	/* This is a traditional C block comment. */

	/*
	 * This is a box comment.
	 *
	 * It starts in column 9, not 1, therefore it gets re-wrapped.
	 */

	/**
	 * This is a box comment
	 * that is not re-wrapped, even though it starts in column 9, not 1.
	 */

	/*-
	 * This is a box comment
	 * that is not re-wrapped.
	 */
}
//indent end


/*
 * Comments to the right of declarations.
 */
//indent input
void
function(void)
{
	int decl;	/* declaration comment */

	int decl;	/* short
			 * multi-line
			 * declaration
			 * comment */

	int decl;	/* long single-line declaration comment that is longer than the allowed line width */

	int decl;	/* long multi-line declaration comment
 * that is longer than
 * the allowed line width */

	int decl;	// C99 declaration comment

	{
		int decl;	/* indented declaration */
		{
			int decl;	/* indented declaration */
			{
				int decl;	/* indented declaration */
				{
					int decl;	/* indented declaration */
				}
			}
		}
	}
}
//indent end

//indent run -ldi0
void
function(void)
{
	int decl;		/* declaration comment */

	int decl;		/* short multi-line declaration comment */

	int decl;		/* long single-line declaration comment that
				 * is longer than the allowed line width */

	int decl;		/* long multi-line declaration comment that is
				 * longer than the allowed line width */

	int decl;		// C99 declaration comment

	{
		int decl;	/* indented declaration */
		{
			int decl;	/* indented declaration */
			{
				int decl;	/* indented declaration */
				{
// $ This comment is indented so far to the right that it may overshoot the
// $ right margin.  The allowed line length is increased to the starting
// $ indentation of 56 plus a fixed amount of 25 columns, resulting in 81.
// $ The trailing '*' would fit, but the trailing '/' is too much.
					int decl;	/* indented declaration
							 */
				}
			}
		}
	}
}
//indent end


/*
 * Comments to the right of code.
 */
//indent input
void
function(void)
{
	code();			/* code comment */
	code();			/* code comment _________ to line length 78 */
	code();			/* code comment __________ to line length 79 */
	code();			/* code comment ___________ to line length 80 */
	code();			/* code comment ____________ to line length 81 */
	code();			/* code comment _____________ to line length 82 */

/* $ In the following comments, the line length is measured after formatting. */
	code();			/* code comment _________ to line length 78*/
	code();			/* code comment __________ to line length 79*/
	code();			/* code comment ___________ to line length 80*/
	code();			/* code comment ____________ to line length 81*/
	code();			/* code comment _____________ to line length 82*/

	code();			/* short
				 * multi-line
				 * code
				 * comment */

	code();			/* long single-line code comment that is longer than the allowed line width */

	code();			/* long multi-line code comment
 * that is longer than
 * the allowed line width */

	code();			// C99 code comment
	code();			// C99 code comment ________ to line length 78
	code();			// C99 code comment _________ to line length 79
	code();			// C99 code comment __________ to line length 80
	code();			// C99 code comment ___________ to line length 81
	code();			// C99 code comment ____________ to line length 82

	if (cond) /* comment */
		if (cond) /* comment */
			if (cond) /* comment */
				if (cond) /* comment */
					if (cond) /* comment */
						code(); /* comment */
}
//indent end

//indent run -l78
void
function(void)
{
	code();			/* code comment */
	code();			/* code comment _________ to line length 78 */
	code();			/* code comment __________ to line length 79
				 */
	code();			/* code comment ___________ to line length 80
				 */
	code();			/* code comment ____________ to line length 81
				 */
	code();			/* code comment _____________ to line length
				 * 82 */

/* $ In the following comments, the line length is measured after formatting. */
	code();			/* code comment _________ to line length 78 */
	code();			/* code comment __________ to line length 79
				 */
	code();			/* code comment ___________ to line length 80
				 */
	code();			/* code comment ____________ to line length 81
				 */
	code();			/* code comment _____________ to line length
				 * 82 */

	code();			/* short multi-line code comment */

	code();			/* long single-line code comment that is
				 * longer than the allowed line width */

	code();			/* long multi-line code comment that is longer
				 * than the allowed line width */

/* $ Trailing C99 comments are not wrapped, as indent would not correctly */
/* $ recognize the continuation lines as continued comments. For block */
/* $ comments this works since the comment has not ended yet. */
	code();			// C99 code comment
	code();			// C99 code comment ________ to line length 78
	code();			// C99 code comment _________ to line length 79
	code();			// C99 code comment __________ to line length 80
	code();			// C99 code comment ___________ to line length 81
	code();			// C99 code comment ____________ to line length 82

	if (cond)		/* comment */
		if (cond)	/* comment */
			if (cond)	/* comment */
				if (cond)	/* comment */
					if (cond)	/* comment */
						code();	/* comment */
}
//indent end


//indent input
/*
	 * this
		 * is a boxed
			 * staircase.
*
* Its paragraphs get wrapped.

There may also be
		lines without asterisks.

 */
//indent end

//indent run
/*
 * this is a boxed staircase.
 *
 * Its paragraphs get wrapped.
 *
 * There may also be lines without asterisks.
 *
 */
//indent end


//indent input
void loop(void)
{
while(cond)/*comment*/;

	while(cond)
	/*comment*/;
}
//indent end

//indent run
void
loop(void)
{
	while (cond)		/* comment */
		;

	while (cond)
		/* comment */;
}
//indent end


/*
 * The following comment starts really far to the right. To avoid that each
 * line only contains a single word, the maximum allowed line width is
 * extended such that each comment line may contain 22 characters.
 */
//indent input
int		global_variable_with_really_long_name_that_reaches_up_to_column_83;	/* 1234567890123456789 1 1234567890123456789 12 1234567890123456789 123 1234567890123456789 1234 1234567890123456789 12345 1234567890123456789 123456 */
//indent end

//indent run
int		global_variable_with_really_long_name_that_reaches_up_to_column_83;	/* 1234567890123456789 1
											 * 1234567890123456789 12
											 * 1234567890123456789
											 * 123
											 * 1234567890123456789
											 * 1234
											 * 1234567890123456789
											 * 12345
											 * 1234567890123456789
											 * 123456 */
//indent end


/*
 * Demonstrates handling of line-end '//' comments.
 *
 * Even though this type of comments had been added in C99, indent didn't
 * support these comments until 2021 and instead messed up the code in
 * seemingly unpredictable ways. It treated any sequence of '/' as a binary
 * operator, no matter whether it was '/' or '//' or '/////'.
 */
//indent input
int dummy // comment
    = // eq
    1		// one
    + // plus
    2;// two

/////separator/////

void function(void){}

// Note: removing one of these line-end comments affected the formatting
// of the main function below, before indent supported '//' comments.

int
main(void)
{
}
//indent end

//indent run
int		dummy		// comment
=				// eq
1				// one
+				// plus
2;				// two

/////separator/////

void
function(void)
{
}

// Note: removing one of these line-end comments affected the formatting
// of the main function below, before indent supported '//' comments.

int
main(void)
{
}
//indent end


/*
 * Between March 2021 and October 2021, indent supported C99 comments only
 * very basically. It messed up the following code, repeating the identifier
 * 'bar' twice in a row.
 */
//indent input
void c99_comment(void)
{
foo(); // C99 comment
bar();
}
//indent end

//indent run
void
c99_comment(void)
{
	foo();			// C99 comment
	bar();
}
//indent end


//indent input
void
comment_at_end_of_function(void)
{
	if (cond)
		statement();
	// comment
}
//indent end

//indent run-equals-input


//indent input
int		decl;
// end-of-line comment at the end of the file
//indent end

//indent run-equals-input


/* A form feed in the middle of a comment is an ordinary character. */
//indent input
/*
 * AE
 */
/*-AE*/
//indent end

//indent run-equals-input


/*
 * Form feeds are seldom used, especially in comments, so treat them as an
 * ordinary character.
 */
//indent input
/* comment*/
/*text* comment*/
//indent end

//indent run
/*  comment */
/* text* comment */
//indent end

//indent run-equals-prev-output -nsc

//indent run-equals-input -nfc1


/*
 * A completely empty line in a box comment must be copied unmodified to the
 * output. This is done in process_comment by adding a space to the end of an
 * otherwise empty comment. This space forces output_line to add some output,
 * but the trailing space is discarded, resulting in an empty line.
 */
//indent input
/*- comment


end */
//indent end

//indent run-equals-input -nfc1


//indent input
/* comment comment comment comment Ümläute */
//indent end

//indent run -l40
/*
 * comment comment comment comment
 * Ümläute
 */
//indent end


//indent input
int f(void)
{
	if (0)
		/* 12 1234 123 123456 1234 1234567 123 1234.  */;
}
//indent end

/* The comment is too long to fit in a single line. */
//indent run -l54
int
f(void)
{
	if (0)
		/*
		 * 12 1234 123 123456 1234 1234567 123
		 * 1234.
		 */;
}
//indent end

/* The comment fits in a single line. */
//indent run
int
f(void)
{
	if (0)
		/* 12 1234 123 123456 1234 1234567 123 1234. */;
}
//indent end


/*
 * Test for an edge cases in comment handling, having a block comment inside
 * a line comment. Before NetBSD pr_comment.c 1.96 from 2021-11-04, indent
 * wrongly assumed that the comment would end at the '*' '/', tokenizing the
 * second word 'still' as a type_outside_parentheses.
 */
//indent input
/* block comment */
// line comment /* still a line comment */ still a line comment
//indent end

//indent run-equals-input


/*
 * Tests for comments that are not wrapped.
 */
//indent input
/*-	tab space	tab space */
/*-	very-long-word-that-cannot-be-broken very-long-word-that-cannot-be-broken */
/*-	very-long-word-that-cannot-be-broken very-long-word-that-cannot-be-broken */
//indent end

//indent run-equals-input -l5

//indent run-equals-input -l32


/*
 * Test for form feeds in nowrap comments.
 */
//indent input
/*-*/
/*-<>*/
//indent end

//indent run-equals-input


/*
 * In a comment that is wrapped, one or more empty lines separate paragraphs.
 * All of these empty lines are preserved.
 */
//indent input
/* line 1


line 4 */
//indent end

//indent run
/*
 * line 1
 *
 *
 * line 4
 */
//indent end

//indent run-equals-input -nfc1

//indent run-equals-input -nfc1 -nsc

//indent run -nsc
/*
line 1


line 4
 */
//indent end

//indent run-equals-input -nsc -ncdb


/*
 * Since 2019-04-04 and before pr_comment.c 1.123 from 2021-11-25, the
 * function analyze_comment wrongly joined the two comments.
 */
//indent input
/*
 *//*
join*/
//indent end

//indent run -nfc1
/*
 */
 /*
  * join
  */
//indent end


/*
 * Since 2019-04-04 and before pr_comment.c 1.123 from 2021-11-25, the
 * function analyze_comment generated malformed output by terminating the
 * first comment but omitting the start of the second comment.
 */
//indent input
/*
*//*
error*/
//indent end

//indent run -nfc1
/*
*/
 /*
  * error
  */
//indent end


/*
 * Ensure that there is exactly one space between the comment and the
 * following binary operator.
 */
//indent input
{
a /* */ > b;
a>b;
}
//indent end

//indent run
{
	a /* */ > b;
	a > b;
}
//indent end


/*
 * Line comments are only related to a code snippet if they are on the same
 * line; they cannot be continued in the next lines.
 */
//indent input
int line;	// comment line 1
		// comment line 2
int block;	/* comment line 1
		 * comment line 2
		 */
//indent end

//indent run -di0
int line;			// comment line 1
// $ XXX: This comment was probably intended to continue 'comment line 1'.
// comment line 2
int block;			/* comment line 1 comment line 2 */
//indent end


// Ensure that '/*/' is not interpreted as a complete comment.
//indent input
/*/ comment? or:not; /* */
//indent end

//indent run
/* / comment? or:not; /* */
//indent end

//indent run-equals-input -nfc1


/*
 * The tokens '/' and '*' do not form a comment when they are separated by a
 * space.
 */
//indent input
int a = b / *c;
// $ Indent can be tricked into treating '/' as a unary operator, thus turning
// $ some operators into the start of a comment. This only works in
// $ syntactically invalid text.
int a = b + / * c;
//indent end

//indent run -di0
int a = b / *c;
// $ FIXME: Don't merge the two operators; there are enough situations where
// $ indent has to guess whether an operator is unary or binary, and these
// $ heuristics can go wrong.
int a = b + /*c;
//indent end


/*
 * Ensure that tab characters that are broken into separate lines are replaced
 * with spaces; other tabs are preserved.
 */
//indent input
/* word	word	word	word	word	word	word	word	word */
//indent end

//indent run -l38
/*
 * word	word	word	word	word
 * word	word	word	word
 */
//indent end


/* In no-wrap comments, every single newline is preserved. */
//indent input
/*-
paragraph 1



paragraph 2
 */
//indent end

//indent run-equals-input
