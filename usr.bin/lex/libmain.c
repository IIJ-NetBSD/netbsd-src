/* libmain - flex run-time support library "main" function */

/* $Header: /usr/tmp/cvs2git/cvsroot-netbsd/src/usr.bin/lex/Attic/libmain.c,v 1.4 1996/12/10 07:18:47 mikel Exp $ */

#include <sys/cdefs.h>

int yylex __P((void));
int main __P((int, char **, char **));

int
main( argc, argv, envp )
int argc;
char *argv[];
char *envp[];
	{
	while ( yylex() != 0 )
		;

	return 0;
	}
