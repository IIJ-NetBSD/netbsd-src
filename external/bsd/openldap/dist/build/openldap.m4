dnl OpenLDAP Autoconf Macros
dnl $OpenLDAP$
dnl This work is part of OpenLDAP Software <http://www.openldap.org/>.
dnl
dnl Copyright 1998-2024 The OpenLDAP Foundation.
dnl All rights reserved.
dnl
dnl Redistribution and use in source and binary forms, with or without
dnl modification, are permitted only as authorized by the OpenLDAP
dnl Public License.
dnl
dnl A copy of this license is available in the file LICENSE in the
dnl top-level directory of the distribution or, alternatively, at
dnl <http://www.OpenLDAP.org/license.html>.
dnl
dnl --------------------------------------------------------------------
dnl Restricted form of AC_ARG_ENABLE that limits user options
dnl
dnl $1 = option name
dnl $2 = help-string
dnl $3 = default value	(auto).  "--" means do not set it by default
dnl $4 = allowed values (auto yes no)
dnl $5 = overridden default
AC_DEFUN([OL_ARG_ENABLE], [# OpenLDAP --enable-$1
	pushdef([ol_DefVal],ifelse($3,,auto,$3))
	AC_ARG_ENABLE($1,ifelse($4,,[$2],[$2] translit([$4],[ ],[|])) ifelse($3,--,,@<:@ol_DefVal@:>@),[
	ol_arg=invalid
	for ol_val in ifelse($4,,[auto yes no],[$4]) ; do
		if test "$enableval" = "$ol_val" ; then
			ol_arg="$ol_val"
		fi
	done
	if test "$ol_arg" = "invalid" ; then
		AC_MSG_ERROR(bad value $enableval for --enable-$1)
	fi
	ol_enable_$1="$ol_arg"
]ifelse($3,--,,[,
[	ol_enable_$1=ifelse($5,,ol_DefVal,[${]$5[:-]ol_DefVal[}])]]))dnl
dnl AC_MSG_RESULT([OpenLDAP -enable-$1 $ol_enable_$1])
	popdef([ol_DefVal])
# end --enable-$1
])dnl
dnl
dnl --------------------------------------------------------------------
dnl Restricted form of AC_ARG_WITH that limits user options
dnl
dnl $1 = option name
dnl $2 = help-string
dnl $3 = default value (no)
dnl $4 = allowed values (yes or no)
AC_DEFUN([OL_ARG_WITH], [# OpenLDAP --with-$1
	AC_ARG_WITH($1,[$2 @<:@]ifelse($3,,yes,$3)@:>@,[
	ol_arg=invalid
	for ol_val in ifelse($4,,[yes no],[$4]) ; do
		if test "$withval" = "$ol_val" ; then
			ol_arg="$ol_val"
		fi
	done
	if test "$ol_arg" = "invalid" ; then
		AC_MSG_ERROR(bad value $withval for --with-$1)
	fi
	ol_with_$1="$ol_arg"
],
[	ol_with_$1=ifelse($3,,"no","$3")])dnl
dnl AC_MSG_RESULT([OpenLDAP --with-$1 $ol_with_$1])
# end --with-$1
])dnl
dnl ====================================================================
dnl Check for dependency generation flag
AC_DEFUN([OL_MKDEPEND], [# test for make depend flag
OL_MKDEP=
OL_MKDEP_FLAGS=
if test -z "${MKDEP}"; then
	OL_MKDEP="${CC-cc}"
	if test -z "${MKDEP_FLAGS}"; then
		AC_CACHE_CHECK([for ${OL_MKDEP} depend flag], ol_cv_mkdep, [
			ol_cv_mkdep=no
			for flag in "-M" "-xM"; do
				cat > conftest.c <<EOF
 noCode;
EOF
				if AC_TRY_COMMAND($OL_MKDEP $flag conftest.c) \
					| grep '^conftest\.'"${ac_objext}" >/dev/null 2>&1
				then
					if test ! -f conftest."${ac_object}" ; then
						ol_cv_mkdep=$flag
						OL_MKDEP_FLAGS="$flag"
						break
					fi
				fi
			done
			rm -f conftest*
		])
		test "$ol_cv_mkdep" = no && OL_MKDEP=":"
	else
		cc_cv_mkdep=yes
		OL_MKDEP_FLAGS="${MKDEP_FLAGS}"
	fi
else
	cc_cv_mkdep=yes
	OL_MKDEP="${MKDEP}"
	OL_MKDEP_FLAGS="${MKDEP_FLAGS}"
fi
AC_SUBST(OL_MKDEP)
AC_SUBST(OL_MKDEP_FLAGS)
])
dnl
dnl ====================================================================
dnl Check if system uses EBCDIC instead of ASCII
AC_DEFUN([OL_CPP_EBCDIC], [# test for EBCDIC
AC_CACHE_CHECK([for EBCDIC],ol_cv_cpp_ebcdic,[
	AC_PREPROC_IFELSE([AC_LANG_SOURCE([[
#if !('M' == 0xd4)
#include <__ASCII__/generate_error.h>
#endif
]])],[ol_cv_cpp_ebcdic=yes],[ol_cv_cpp_ebcdic=no])])
if test $ol_cv_cpp_ebcdic = yes ; then
	AC_DEFINE(HAVE_EBCDIC,1, [define if system uses EBCDIC instead of ASCII])
fi
])
dnl
dnl --------------------------------------------------------------------
dnl Check for MSVC
AC_DEFUN([OL_MSVC],
[AC_REQUIRE_CPP()dnl
AC_CACHE_CHECK([whether we are using MS Visual C++], ol_cv_msvc,
[AC_PREPROC_IFELSE([AC_LANG_SOURCE([[
#ifndef _MSC_VER
#include <__FOO__/generate_error.h>
#endif
]])],[ol_cv_msvc=yes],[ol_cv_msvc=no])])])

dnl --------------------------------------------------------------------
dnl OpenLDAP version of STDC header check w/ EBCDIC support
AC_DEFUN([OL_HEADER_STDC],
[AC_REQUIRE_CPP()dnl
AC_REQUIRE([OL_CPP_EBCDIC])dnl
AC_CACHE_CHECK([for ANSI C header files], ol_cv_header_stdc,
[AC_PREPROC_IFELSE([AC_LANG_SOURCE([[#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <float.h>]])],[ol_cv_header_stdc=yes],[ol_cv_header_stdc=no])

if test $ol_cv_header_stdc = yes; then
  # SunOS 4.x string.h does not declare mem*, contrary to ANSI.
AC_EGREP_HEADER(memchr, string.h, , ol_cv_header_stdc=no)
fi

if test $ol_cv_header_stdc = yes; then
  # ISC 2.0.2 stdlib.h does not declare free, contrary to ANSI.
AC_EGREP_HEADER(free, stdlib.h, , ol_cv_header_stdc=no)
fi

if test $ol_cv_header_stdc = yes; then
  # /bin/cc in Irix-4.0.5 gets non-ANSI ctype macros unless using -ansi.
AC_RUN_IFELSE([AC_LANG_SOURCE([[#include <ctype.h>
#include <stdlib.h>
#ifndef HAVE_EBCDIC
#	define ISLOWER(c) ('a' <= (c) && (c) <= 'z')
#	define TOUPPER(c) (ISLOWER(c) ? 'A' + ((c) - 'a') : (c))
#else
#	define ISLOWER(c) (('a' <= (c) && (c) <= 'i') \
		|| ('j' <= (c) && (c) <= 'r') \
		|| ('s' <= (c) && (c) <= 'z'))
#	define TOUPPER(c)	(ISLOWER(c) ? ((c) | 0x40) : (c))
#endif
#define XOR(e, f) (((e) && !(f)) || (!(e) && (f)))
int main () { int i; for (i = 0; i < 256; i++)
if (XOR (islower (i), ISLOWER (i)) || toupper (i) != TOUPPER (i)) exit(2);
exit (0); }
]])],[],[ol_cv_header_stdc=no],[:])
fi])
if test $ol_cv_header_stdc = yes; then
  AC_DEFINE(STDC_HEADERS)
fi
ac_cv_header_stdc=disable
])
dnl
dnl ====================================================================
dnl DNS resolver macros
AC_DEFUN([OL_RESOLVER_TRY],
[if test $ol_cv_lib_resolver = no ; then
	AC_CACHE_CHECK([for resolver link (]ifelse($2,,default,$2)[)],[$1],
[
	ol_RESOLVER_LIB=ifelse($2,,,$2)
	ol_LIBS=$LIBS
	LIBS="$ol_RESOLVER_LIB $LIBS"

	AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#ifdef HAVE_SYS_TYPES_H
#	include <sys/types.h>
#endif
#include <netinet/in.h>
#ifdef HAVE_ARPA_NAMESER_H
#	include <arpa/nameser.h>
#endif
#ifdef HAVE_RESOLV_H
#	include <resolv.h>
#endif
]], [[{
	int len, status;
	char *request = NULL;
	unsigned char reply[64*1024];
	unsigned char host[64*1024];
	unsigned char *p;

#ifdef NS_HFIXEDSZ
	/* Bind 8/9 interface */
	len = res_query(request, ns_c_in, ns_t_srv, reply, sizeof(reply));
#else
	/* Bind 4 interface */
# ifndef T_SRV
#  define T_SRV 33
# endif
	len = res_query(request, C_IN, T_SRV, reply, sizeof(reply));
#endif
	p = reply;
#ifdef NS_HFIXEDSZ
	/* Bind 8/9 interface */
	p += NS_HFIXEDSZ;
#elif defined(HFIXEDSZ)
	/* Bind 4 interface w/ HFIXEDSZ */
	p += HFIXEDSZ;
#else
	/* Bind 4 interface w/o HFIXEDSZ */
	p += sizeof(HEADER);
#endif
	status = dn_expand( reply, reply+len, p, host, sizeof(host));
}]])],[$1=yes],[$1=no])

	LIBS="$ol_LIBS"
])

	if test $$1 = yes ; then
		ol_cv_lib_resolver=ifelse($2,,yes,$2)
	fi
fi
])
dnl --------------------------------------------------------------------
dnl Try to locate appropriate library
AC_DEFUN([OL_RESOLVER_LINK],
[ol_cv_lib_resolver=no
OL_RESOLVER_TRY(ol_cv_resolver_none)
OL_RESOLVER_TRY(ol_cv_resolver_resolv,[-lresolv])
OL_RESOLVER_TRY(ol_cv_resolver_bind,[-lbind])
])
dnl
dnl ====================================================================
dnl Check POSIX Thread version 
dnl
dnl defines ol_cv_pthread_version to 4, 5, 6, 7, 8, 10, depending on the
dnl	version of the POSIX.4a Draft that is implemented.
dnl	10 == POSIX.4a Final == POSIX.1c-1996 for our purposes.
dnl	Existence of pthread.h should be tested separately.
dnl
dnl tests:
dnl	pthread_detach() was dropped in Draft 8, it is present
dnl		in every other version
dnl	PTHREAD_CREATE_UNDETACHED is only in Draft 7, it was called
dnl		PTHREAD_CREATE_JOINABLE after that
dnl	pthread_attr_create was renamed to pthread_attr_init in Draft 6.
dnl		Draft 6-10 has _init, Draft 4-5 has _create.
dnl	pthread_attr_default was dropped in Draft 6, only 4 and 5 have it
dnl	PTHREAD_MUTEX_INITIALIZER was introduced in Draft 5. It's not
dnl		interesting to us because we don't try to statically
dnl		initialize mutexes. 5-10 has it.
dnl
dnl Draft 9 and 10 are equivalent for our purposes.
dnl
AC_DEFUN([OL_POSIX_THREAD_VERSION],
[AC_CACHE_CHECK([POSIX thread version],[ol_cv_pthread_version],[
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#		include <pthread.h>
	]], [[
		int i = PTHREAD_CREATE_JOINABLE;
	]])],[
	AC_EGREP_HEADER(pthread_detach,pthread.h,
	ol_cv_pthread_version=10, ol_cv_pthread_version=8)],[
	AC_EGREP_CPP(draft7,[
#		include <pthread.h>
#		ifdef PTHREAD_CREATE_UNDETACHED
		draft7
#		endif
	], ol_cv_pthread_version=7, [
	AC_EGREP_HEADER(pthread_attr_init,pthread.h,
	ol_cv_pthread_version=6, [
	AC_EGREP_CPP(draft5,[
#		include <pthread.h>
#ifdef		PTHREAD_MUTEX_INITIALIZER
		draft5
#endif
	], ol_cv_pthread_version=5, ol_cv_pthread_version=4) ]) ]) ])
])
])dnl
dnl
dnl --------------------------------------------------------------------
AC_DEFUN([OL_PTHREAD_TEST_INCLUDES], [[
/* pthread test headers */
#include <pthread.h>
#if HAVE_PTHREADS < 7
#include <errno.h>
#endif
#ifndef NULL
#define NULL (void*)0
#endif

#ifdef __STDC__
static void *task(void *p)
#else
static void *task(p)
	void *p;
#endif
{
	return (void *) (p == NULL);
}
]])
AC_DEFUN([OL_PTHREAD_TEST_FUNCTION],[[
	/* pthread test function */
#ifndef PTHREAD_CREATE_DETACHED
#define	PTHREAD_CREATE_DETACHED	1
#endif
	pthread_t t;
	int status;
	int detach = PTHREAD_CREATE_DETACHED;

#if HAVE_PTHREADS > 4
	/* Final pthreads */
	pthread_attr_t attr;

	status = pthread_attr_init(&attr);
	if( status ) return status;

#if HAVE_PTHREADS < 7
	status = pthread_attr_setdetachstate(&attr, &detach);
	if( status < 0 ) status = errno;
#else
	status = pthread_attr_setdetachstate(&attr, detach);
#endif
	if( status ) return status;
	status = pthread_create( &t, &attr, task, NULL );
#if HAVE_PTHREADS < 7
	if( status < 0 ) status = errno;
#endif
	if( status ) return status;
#else
	/* Draft 4 pthreads */
	status = pthread_create( &t, pthread_attr_default, task, NULL );
	if( status ) return errno;

	/* give thread a chance to complete */
	/* it should remain joinable and hence detachable */
	sleep( 1 );

	status = pthread_detach( &t );
	if( status ) return errno;
#endif

#ifdef HAVE_LINUX_THREADS
	pthread_kill_other_threads_np();
#endif

	return 0;
]])

AC_DEFUN([OL_PTHREAD_TEST_PROGRAM],
[AC_LANG_SOURCE([OL_PTHREAD_TEST_INCLUDES

#ifdef __STDC__
int main(int argc, char **argv)
#else
int main(argc, argv)
	int argc;
	char **argv;
#endif
{
OL_PTHREAD_TEST_FUNCTION
}
])])
dnl --------------------------------------------------------------------
AC_DEFUN([OL_PTHREAD_TRY], [# Pthread try link: $1 ($2)
if test "$ol_link_threads" = no ; then
	# try $1
	AC_CACHE_CHECK([for pthread link with $1], [$2], [
		# save the flags
		ol_LIBS="$LIBS"
		LIBS="$1 $LIBS"

		AC_RUN_IFELSE([OL_PTHREAD_TEST_PROGRAM],
			[$2=yes],
			[$2=no],
			[AC_LINK_IFELSE([AC_LANG_PROGRAM(OL_PTHREAD_TEST_INCLUDES,
				OL_PTHREAD_TEST_FUNCTION)],
				[$2=yes], [$2=no])])

		# restore the LIBS
		LIBS="$ol_LIBS"
	])

	if test $$2 = yes ; then
		ol_link_pthreads="$1"
		ol_link_threads=posix
	fi
fi
])
dnl
dnl ====================================================================
dnl Check GNU Pth pthread Header
dnl
dnl defines ol_cv_header linux_threads to 'yes' or 'no'
dnl		'no' implies pthreads.h is not LinuxThreads or pthreads.h
dnl		doesn't exist.  Existence of pthread.h should separately
dnl		checked.
dnl 
AC_DEFUN([OL_HEADER_GNU_PTH_PTHREAD_H], [
	AC_CACHE_CHECK([for GNU Pth pthread.h],
		[ol_cv_header_gnu_pth_pthread_h],
		[AC_EGREP_CPP(__gnu_pth__,
			[#include <pthread.h>
#ifdef _POSIX_THREAD_IS_GNU_PTH
	__gnu_pth__;
#endif
],
			[ol_cv_header_gnu_pth_pthread_h=yes],
			[ol_cv_header_gnu_pth_pthread_h=no])
		])
])dnl
dnl ====================================================================
dnl Check for NT Threads
AC_DEFUN([OL_NT_THREADS], [
	AC_CHECK_FUNC(_beginthread)

	if test $ac_cv_func__beginthread = yes ; then
		AC_DEFINE(HAVE_NT_THREADS,1,[if you have NT Threads])
		ol_cv_nt_threads=yes
	fi
])
dnl ====================================================================
dnl Check LinuxThreads Header
dnl
dnl defines ol_cv_header linux_threads to 'yes' or 'no'
dnl		'no' implies pthreads.h is not LinuxThreads or pthreads.h
dnl		doesn't exist.  Existence of pthread.h should separately
dnl		checked.
dnl 
AC_DEFUN([OL_HEADER_LINUX_THREADS], [
	AC_CACHE_CHECK([for LinuxThreads pthread.h],
		[ol_cv_header_linux_threads],
		[AC_EGREP_CPP(pthread_kill_other_threads_np,
			[#include <pthread.h>],
			[ol_cv_header_linux_threads=yes],
			[ol_cv_header_linux_threads=no])
		])
	if test $ol_cv_header_linux_threads = yes; then
		AC_DEFINE(HAVE_LINUX_THREADS,1,[if you have LinuxThreads])
	fi
])dnl
dnl --------------------------------------------------------------------
dnl	Check LinuxThreads Implementation
dnl
dnl	defines ol_cv_sys_linux_threads to 'yes' or 'no'
dnl	'no' implies pthreads implementation is not LinuxThreads.
dnl 
AC_DEFUN([OL_SYS_LINUX_THREADS], [
	AC_CHECK_FUNCS(pthread_kill_other_threads_np)
	AC_CACHE_CHECK([for LinuxThreads implementation],
		[ol_cv_sys_linux_threads],
		[ol_cv_sys_linux_threads=$ac_cv_func_pthread_kill_other_threads_np])
])dnl
dnl
dnl --------------------------------------------------------------------
dnl Check LinuxThreads consistency
AC_DEFUN([OL_LINUX_THREADS], [
	AC_REQUIRE([OL_HEADER_LINUX_THREADS])
	AC_REQUIRE([OL_SYS_LINUX_THREADS])
	AC_CACHE_CHECK([for LinuxThreads consistency], [ol_cv_linux_threads], [
		if test $ol_cv_header_linux_threads = yes &&
		   test $ol_cv_sys_linux_threads = yes; then
			ol_cv_linux_threads=yes
		elif test $ol_cv_header_linux_threads = no &&
		     test $ol_cv_sys_linux_threads = no; then
			ol_cv_linux_threads=no
		else
			ol_cv_linux_threads=error
		fi
	])
])dnl
dnl
dnl ====================================================================
dnl Check for POSIX Regex
AC_DEFUN([OL_POSIX_REGEX], [
AC_CACHE_CHECK([for compatible POSIX regex],ol_cv_c_posix_regex,[
	AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <sys/types.h>
#include <regex.h>
static char *pattern, *string;
int main(void)
{
	int rc;
	regex_t re;

	pattern = "^A";

	if(regcomp(&re, pattern, 0)) {
		return -1;
	}
	
	string = "ALL MATCH";
	
	rc = regexec(&re, string, 0, (void*)0, 0);

	regfree(&re);

	return rc;
}]])],[ol_cv_c_posix_regex=yes],[ol_cv_c_posix_regex=no],[ol_cv_c_posix_regex=cross])])
])
dnl
dnl ====================================================================
dnl Check if toupper() requires islower() to be called first
AC_DEFUN([OL_C_UPPER_LOWER],
[AC_CACHE_CHECK([if toupper() requires islower()],ol_cv_c_upper_lower,[
	AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <ctype.h>
#include <stdlib.h>
int main(void)
{
	if ('C' == toupper('C'))
		exit(0);
	else
		exit(1);
}]])],[ol_cv_c_upper_lower=no],[ol_cv_c_upper_lower=yes],[ol_cv_c_upper_lower=safe])])
if test $ol_cv_c_upper_lower != no ; then
	AC_DEFINE(C_UPPER_LOWER,1, [define if toupper() requires islower()])
fi
])
dnl
dnl ====================================================================
dnl Error string checks
dnl
dnl Check for declaration of sys_errlist in one of stdio.h and errno.h.
dnl Declaration of sys_errlist on BSD4.4 interferes with our declaration.
dnl Reported by Keith Bostic.
AC_DEFUN([OL_SYS_ERRLIST],
[AC_CACHE_CHECK([existence of sys_errlist],ol_cv_have_sys_errlist,[
	AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <errno.h>]], [[char *c = (char *) *sys_errlist]])],[ol_cv_have_sys_errlist=yes],[ol_cv_have_sys_errlist=no])])
if test $ol_cv_have_sys_errlist = yes ; then
	AC_DEFINE(HAVE_SYS_ERRLIST,1,
		[define if you actually have sys_errlist in your libs])
	AC_CACHE_CHECK([declaration of sys_errlist],ol_cv_dcl_sys_errlist,[
		AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#ifdef _WIN32
#include <stdlib.h>
#endif ]], [[char *c = (char *) *sys_errlist]])],[ol_cv_dcl_sys_errlist=yes],
	[ol_cv_dcl_sys_errlist=no])])
#
	# It's possible (for near-UNIX clones) that sys_errlist doesn't exist
	if test $ol_cv_dcl_sys_errlist = no ; then
		AC_DEFINE(DECL_SYS_ERRLIST,1,
			[define if sys_errlist is not declared in stdio.h or errno.h])
	fi
fi
])dnl
dnl
dnl ====================================================================
dnl glibc supplies a non-standard strerror_r if _GNU_SOURCE is defined.
dnl It's actually preferable to the POSIX version, if available.
AC_DEFUN([OL_NONPOSIX_STRERROR_R],
[AC_CACHE_CHECK([non-posix strerror_r],ol_cv_nonposix_strerror_r,[
	AC_EGREP_CPP(strerror_r,[#include <string.h>],
		ol_decl_strerror_r=yes, ol_decl_strerror_r=no)dnl

	if test $ol_decl_strerror_r = yes ; then
		AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <string.h>]], [[   /* from autoconf 2.59 */
				char buf[100];
				char x = *strerror_r (0, buf, sizeof buf);
				char *p = strerror_r (0, buf, sizeof buf);
			]])],[ol_cv_nonposix_strerror_r=yes],[ol_cv_nonposix_strerror_r=no])
	else
		AC_RUN_IFELSE([AC_LANG_SOURCE([[
			int main(void) {
				char buf[100];
				buf[0] = 0;
				strerror_r( 1, buf, sizeof buf );
				exit( buf[0] == 0 );
			}
			]])],[ol_cv_nonposix_strerror_r=yes],[ol_cv_nonposix_strerror_r=no],[ol_cv_nonposix_strerror_r=no])
	fi
	])
if test $ol_cv_nonposix_strerror_r = yes ; then
	AC_DEFINE(HAVE_NONPOSIX_STRERROR_R,1,
		[define if strerror_r returns char* instead of int])
fi
])dnl
dnl
AC_DEFUN([OL_STRERROR],
[AC_CHECK_FUNCS(strerror strerror_r)
ol_cv_func_strerror_r=no
if test "${ac_cv_func_strerror_r}" = yes ; then
	OL_NONPOSIX_STRERROR_R
elif test "${ac_cv_func_strerror}" = no ; then
	OL_SYS_ERRLIST
fi
])dnl
dnl ====================================================================
dnl Early MIPS compilers (used in Ultrix 4.2) don't like
dnl "int x; int *volatile a = &x; *a = 0;"
dnl 	-- borrowed from PDKSH
AC_DEFUN([OL_C_VOLATILE],
 [AC_CACHE_CHECK(if compiler understands volatile, ol_cv_c_volatile,
    [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[int x, y, z;]], [[volatile int a; int * volatile b = x ? &y : &z;
      /* Older MIPS compilers (eg., in Ultrix 4.2) don't like *b = 0 */
      *b = 0;]])],[ol_cv_c_volatile=yes],[ol_cv_c_volatile=no])])
  if test $ol_cv_c_volatile = yes; then
    : 
  else
    AC_DEFINE(volatile,,[define as empty if volatile is not supported])
  fi
 ])dnl
dnl
dnl ====================================================================
dnl Look for fetch(3)
AC_DEFUN([OL_LIB_FETCH],
[ol_LIBS=$LIBS
LIBS="-lfetch $LIBS"
AC_CACHE_CHECK([fetch(3) library],ol_cv_lib_fetch,[
	AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#include <stdio.h>
#include <fetch.h>]], [[struct url *u = fetchParseURL("file:///"); ]])],[ol_cv_lib_fetch=yes],[ol_cv_lib_fetch=no])])
LIBS=$ol_LIBS
if test $ol_cv_lib_fetch != no ; then
	ol_link_fetch="-lfetch"
	AC_DEFINE(HAVE_FETCH,1,
		[define if you actually have FreeBSD fetch(3)])
fi
])dnl
dnl
dnl ====================================================================
dnl Define inet_aton is available
AC_DEFUN([OL_FUNC_INET_ATON],
 [AC_CACHE_CHECK([for inet_aton()], ol_cv_func_inet_aton,
    [AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#ifdef HAVE_SYS_TYPES_H
#	include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#	include <sys/socket.h>
#	ifdef HAVE_SYS_SELECT_H
#		include <sys/select.h>
#	endif
#	include <netinet/in.h>
#	ifdef HAVE_ARPA_INET_H
#		include <arpa/inet.h>
#	endif
#endif
]], [[struct in_addr in;
int rc = inet_aton( "255.255.255.255", &in );]])],[ol_cv_func_inet_aton=yes],[ol_cv_func_inet_aton=no])])
  if test $ol_cv_func_inet_aton != no; then
    AC_DEFINE(HAVE_INET_ATON, 1,
		[define to you inet_aton(3) is available])
  fi
 ])dnl
dnl
dnl ====================================================================
dnl check no of arguments for ctime_r
AC_DEFUN([OL_FUNC_CTIME_R_NARGS],
 [AC_CACHE_CHECK(number of arguments of ctime_r, ol_cv_func_ctime_r_nargs,
   [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <time.h>]], [[time_t ti; char *buffer; ctime_r(&ti,buffer,32);]])],[ol_cv_func_ctime_r_nargs3=yes],[ol_cv_func_ctime_r_nargs3=no])

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <time.h>]], [[time_t ti; char *buffer; ctime_r(&ti,buffer);]])],[ol_cv_func_ctime_r_nargs2=yes],[ol_cv_func_ctime_r_nargs2=no])

	if test $ol_cv_func_ctime_r_nargs3 = yes &&
	   test $ol_cv_func_ctime_r_nargs2 = no ; then

		ol_cv_func_ctime_r_nargs=3

	elif test $ol_cv_func_ctime_r_nargs3 = no &&
	     test $ol_cv_func_ctime_r_nargs2 = yes ; then

		ol_cv_func_ctime_r_nargs=2

	else
		ol_cv_func_ctime_r_nargs=0
	fi
  ])

  if test $ol_cv_func_ctime_r_nargs -gt 1 ; then
 	AC_DEFINE_UNQUOTED(CTIME_R_NARGS, $ol_cv_func_ctime_r_nargs,
		[set to the number of arguments ctime_r() expects])
  fi
])dnl
dnl
dnl --------------------------------------------------------------------
dnl check return type of ctime_r()
AC_DEFUN([OL_FUNC_CTIME_R_TYPE],
 [AC_CACHE_CHECK(return type of ctime_r, ol_cv_func_ctime_r_type,
   [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <time.h>]], [[extern int (ctime_r)();]])],[ol_cv_func_ctime_r_type="int"],[ol_cv_func_ctime_r_type="charp"])
	])
  if test $ol_cv_func_ctime_r_type = "int" ; then
	AC_DEFINE(CTIME_R_RETURNS_INT,1, [define if ctime_r() returns int])
  fi
])dnl
dnl ====================================================================
dnl check no of arguments for gethostbyname_r
AC_DEFUN([OL_FUNC_GETHOSTBYNAME_R_NARGS],
 [AC_CACHE_CHECK(number of arguments of gethostbyname_r,
	ol_cv_func_gethostbyname_r_nargs,
	[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#define BUFSIZE (sizeof(struct hostent)+10)]], [[struct hostent hent; char buffer[BUFSIZE];
		int bufsize=BUFSIZE;int h_errno;
		(void)gethostbyname_r("segovia.cs.purdue.edu", &hent,
			buffer, bufsize, &h_errno);]])],[ol_cv_func_gethostbyname_r_nargs5=yes],[ol_cv_func_gethostbyname_r_nargs5=no])

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#define BUFSIZE (sizeof(struct hostent)+10)]], [[struct hostent hent;struct hostent *rhent;
		char buffer[BUFSIZE];
		int bufsize=BUFSIZE;int h_errno;
		(void)gethostbyname_r("localhost", &hent, buffer, bufsize,
			&rhent, &h_errno);]])],[ol_cv_func_gethostbyname_r_nargs6=yes],[ol_cv_func_gethostbyname_r_nargs6=no])

	if test $ol_cv_func_gethostbyname_r_nargs5 = yes &&
	   test $ol_cv_func_gethostbyname_r_nargs6 = no ; then

		ol_cv_func_gethostbyname_r_nargs=5

	elif test $ol_cv_func_gethostbyname_r_nargs5 = no &&
	     test $ol_cv_func_gethostbyname_r_nargs6 = yes ; then

		ol_cv_func_gethostbyname_r_nargs=6

	else
		ol_cv_func_gethostbyname_r_nargs=0
	fi
  ])
  if test $ol_cv_func_gethostbyname_r_nargs -gt 1 ; then
	AC_DEFINE_UNQUOTED(GETHOSTBYNAME_R_NARGS,
		$ol_cv_func_gethostbyname_r_nargs,
		[set to the number of arguments gethostbyname_r() expects])
  fi
])dnl
dnl
dnl check no of arguments for gethostbyaddr_r
AC_DEFUN([OL_FUNC_GETHOSTBYADDR_R_NARGS],
 [AC_CACHE_CHECK(number of arguments of gethostbyaddr_r,
	[ol_cv_func_gethostbyaddr_r_nargs],
	[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#define BUFSIZE (sizeof(struct hostent)+10)]], [[struct hostent hent; char buffer[BUFSIZE]; 
	    struct in_addr add;
	    size_t alen=sizeof(struct in_addr);
	    int bufsize=BUFSIZE;int h_errno;
		(void)gethostbyaddr_r( (void *)&(add.s_addr),
			alen, AF_INET, &hent, buffer, bufsize, &h_errno);]])],[ol_cv_func_gethostbyaddr_r_nargs7=yes],[ol_cv_func_gethostbyaddr_r_nargs7=no])

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#define BUFSIZE (sizeof(struct hostent)+10)]], [[struct hostent hent;
		struct hostent *rhent; char buffer[BUFSIZE]; 
		struct in_addr add;
		size_t alen=sizeof(struct in_addr);
		int bufsize=BUFSIZE;int h_errno;
		(void)gethostbyaddr_r( (void *)&(add.s_addr),
			alen, AF_INET, &hent, buffer, bufsize, 
			&rhent, &h_errno);]])],[ol_cv_func_gethostbyaddr_r_nargs8=yes],[ol_cv_func_gethostbyaddr_r_nargs8=no])

	if test $ol_cv_func_gethostbyaddr_r_nargs7 = yes &&
	   test $ol_cv_func_gethostbyaddr_r_nargs8 = no ; then

		ol_cv_func_gethostbyaddr_r_nargs=7

	elif test $ol_cv_func_gethostbyaddr_r_nargs7 = no &&
	     test $ol_cv_func_gethostbyaddr_r_nargs8 = yes ; then

		ol_cv_func_gethostbyaddr_r_nargs=8

	else
		ol_cv_func_gethostbyaddr_r_nargs=0
	fi
  ])
  if test $ol_cv_func_gethostbyaddr_r_nargs -gt 1 ; then
    AC_DEFINE_UNQUOTED(GETHOSTBYADDR_R_NARGS,
		$ol_cv_func_gethostbyaddr_r_nargs,
		[set to the number of arguments gethostbyaddr_r() expects])
  fi
])dnl
dnl
dnl --------------------------------------------------------------------
dnl Check for Cyrus SASL version compatibility
AC_DEFUN([OL_SASL_COMPAT],
[AC_CACHE_CHECK([Cyrus SASL library version], [ol_cv_sasl_compat],[
	AC_EGREP_CPP(__sasl_compat,[
#ifdef HAVE_SASL_SASL_H
#include <sasl/sasl.h>
#else
#include <sasl.h>
#endif

/* Require 2.1.15+ */
#if SASL_VERSION_MAJOR == 2  && SASL_VERSION_MINOR > 1
	char *__sasl_compat = "2.2+ or better okay (we guess)";
#elif SASL_VERSION_MAJOR == 2  && SASL_VERSION_MINOR == 1 \
	&& SASL_VERSION_STEP >=15
	char *__sasl_compat = "2.1.15+ or better okay";
#endif
	],	[ol_cv_sasl_compat=yes], [ol_cv_sasl_compat=no])])
])
