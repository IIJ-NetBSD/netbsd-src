#! /usr/bin/env sh
#	$NetBSD: build.sh,v 1.399 2025/07/08 12:29:40 hgutch Exp $
#
# Copyright (c) 2001-2023 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Todd Vierling and Luke Mewburn.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
#
# Top level build wrapper, to build or cross-build NetBSD.
#

#
# {{{ Begin shell feature tests.
#
# We try to determine whether or not this script is being run under
# a shell that supports the features that we use.  If not, we try to
# re-exec the script under another shell.  If we can't find another
# suitable shell, then we show a message and exit.
#

errmsg=			# error message, if not empty
shelltest=false		# if true, exit after testing the shell
re_exec_allowed=true	# if true, we may exec under another shell

# Parse special command line options in $1.  These special options are
# for internal use only, are not documented, and are not valid anywhere
# other than $1.
case "$1" in
--shelltest)
    shelltest=true
    re_exec_allowed=false
    shift
    ;;
--no-re-exec)
    re_exec_allowed=false
    shift
    ;;
esac

# Solaris /bin/sh, and other SVR4 shells, do not support "!".
# This is the first feature that we test, because subsequent
# tests use "!".
#
# Unfortunately, if the shell doesn't support ! most of the tests
# following which use '!' are likely to simply abort with a syntax error.
# Not executing the code containing ! does not avoid compiling it.
#
if test -z "$errmsg"
then
    if ( eval '! false' ) >/dev/null 2>&1
    then
	:
    else
	errmsg='Shell does not support "!".'
    fi
fi

# Does the shell support functions?
#
if test -z "$errmsg"
then
    if ! (
	eval 'somefunction() { : ; }'
	) >/dev/null 2>&1
    then
	errmsg='Shell does not support functions.'
    fi
fi

# Does the shell support the "local" keyword for variables in functions?
#
# Local variables are not required by SUSv3, but some scripts run during
# the NetBSD build use them.
#
# ksh93 fails this test; it uses an incompatible syntax involving the
# keywords 'function' and 'typeset'.
#
if test -z "$errmsg"
then
    if ! (
	eval 'f() { local v=2; }; v=1; f && test x"$v" = x1'
	) >/dev/null 2>&1
    then
	errmsg='Shell does not support the "local" keyword in functions.'
    fi
fi

# Does the shell support ${var%suffix}, ${var#prefix}, and their variants?
#
# We don't bother testing for ${var+value}, ${var-value}, or their variants,
# since shells without those (unlikely) are sure to fail other tests too.
#
if test -z "$errmsg"
then
    if ! (
	eval 'var=a/b/c ;
	      test x"${var#*/};${var##*/};${var%/*};${var%%/*}" = \
		   x"b/c;c;a/b;a" ;'
	) >/dev/null 2>&1
    then
	errmsg='Shell does not support "${var%suffix}" or "${var#prefix}".'
    fi
fi

# Does the shell support IFS?
#
# zsh in normal mode (as opposed to "emulate sh" mode) fails this test.
#
if test -z "$errmsg"
then
    if ! (
	eval 'IFS=: ; v=":a b::c" ; set -- $v ; IFS=+ ;
		test x"$#;$1,$2,$3,$4;$*" = x"4;,a b,,c;+a b++c"'
	) >/dev/null 2>&1
    then
	errmsg='Shell does not support IFS word splitting.'
    fi
fi

# Does the shell support ${1+"$@"}?
#
# Some versions of zsh fail this test, even in "emulate sh" mode.
#
if test -z "$errmsg"
then
    if ! (
	eval 'set -- "a a a" "b b b"
	      set -- ${1+"$@"}
	      test x"$#;$1;$2" = x"2;a a a;b b b" '
	) >/dev/null 2>&1
    then
	errmsg='Shell does not support ${1+"$@"}.'
    fi
fi

# Does the shell support $(...) command substitution?
#
if test -z "$errmsg"
then
    if ! (
	eval 'var=$(echo abc); test x"$var" = xabc'
	) >/dev/null 2>&1
    then
	errmsg='Shell does not support "$(...)" command substitution.'
    fi
fi

# Does the shell support $(...) command substitution with
# unbalanced parentheses?
#
# Some shells known to fail this test are:  NetBSD /bin/ksh (as of 2009-12),
# bash-3.1, pdksh-5.2.14, zsh-4.2.7 in "emulate sh" mode.
#
if test -z "$errmsg"
then
    if ! (
	eval 'var=$(case x in x) echo abc;; esac); test x"$var" = x"abc"'
	) >/dev/null 2>&1
    then
	# XXX: This test is ignored because so many shells fail it; instead,
	#      the NetBSD build avoids using the problematic construct.
	: ignore 'Shell does not support "$(...)" with unbalanced ")".'
    fi
fi

# Does the shell support getopts or getopt?
#  (XXX: Q: why the need for the eval here, looks unncessary)
#
if test -z "$errmsg"
then
    if ! (
	eval 'type getopts || type getopt'
	) >/dev/null 2>&1
    then
	errmsg='Shell does not support getopts or getopt.'
    fi
fi

#
# If shelltest is true, exit now, reporting whether or not the shell is good.
#
if "$shelltest"
then
    if test -n "$errmsg"
    then
	echo >&2 "$0: $errmsg"
	exit 1
    else
	exit 0
    fi
fi

#
# If the shell was bad, try to exec a better shell, or report an error.
#
# Loops are broken by passing an extra "--no-re-exec" flag to the new
# instance of this script.
#
if test -n "$errmsg"
then
    if "$re_exec_allowed"
    then
	for othershell in \
	    "${HOST_SH}" /usr/xpg4/bin/sh ksh ksh88 mksh pdksh dash bash
	    # NOTE: some shells known not to work are:
	    # any shell using csh syntax;
	    # Solaris /bin/sh (missing many modern features);
	    # ksh93 (incompatible syntax for local variables);
	    # zsh (many differences, unless run in compatibility mode).
	do
	    test -n "$othershell" || continue
	    if eval 'type "$othershell"' >/dev/null 2>&1 \
		&& $othershell "$0" --shelltest >/dev/null 2>&1
	    then
		cat <<EOF
$0: $errmsg
$0: Retrying under $othershell
EOF
		HOST_SH="$othershell"
		export HOST_SH
		exec $othershell "$0" --no-re-exec "$@" # avoid ${1+"$@"}
	    fi
	    # If HOST_SH was set, but failed the test above,
	    # then give up without trying any other shells.
	    test x"${othershell}" = x"${HOST_SH}" && break
	done
    fi

    #
    # If we get here, then the shell is bad, and we either could not
    # find a replacement, or were not allowed to try a replacement.
    #
    cat <<EOF
$0: $errmsg

The NetBSD build system requires a shell that supports modern POSIX
features, as well as the "local" keyword in functions (which is a
widely-implemented but non-standardised feature).

Please re-run this script under a suitable shell.  For example:

	/path/to/suitable/shell $0 ...

The above command will usually enable build.sh to automatically set
HOST_SH=/path/to/suitable/shell, but if that fails, then you may also
need to explicitly set the HOST_SH environment variable, as follows:

	HOST_SH=/path/to/suitable/shell
	export HOST_SH
	\${HOST_SH} $0 ...
EOF
    exit 1
fi

#
# }}} End shell feature tests.
#

progname=${0##*/}
toppid=$$
results=/dev/null
tab='	'
nl='
'
trap "exit 1" 1 2 3 15

bomb()
{
	cat >&2 <<ERRORMESSAGE

ERROR: $*

*** BUILD ABORTED ***
ERRORMESSAGE
	kill ${toppid}		# in case we were invoked from a subshell
	exit 1
}

# Quote args to make them safe in the shell.
# Usage: quotedlist="$(shell_quote args...)"
#
# After building up a quoted list, use it by evaling it inside
# double quotes, like this:
#    eval "set -- $quotedlist"
# or like this:
#    eval "\$command $quotedlist \$filename"
#
shell_quote()
(
	local result=
	local arg qarg
	LC_COLLATE=C ; export LC_COLLATE # so [a-zA-Z0-9] works in ASCII
	for arg in "$@"
	do
		case "${arg}" in
		'')
			qarg="''"
			;;
		*[!-./a-zA-Z0-9]*)
			# Convert each embedded ' to '\'',
			# then insert ' at the beginning of the first line,
			# and append ' at the end of the last line.
			# Finally, elide unnecessary '' pairs at the
			# beginning and end of the result and as part of
			# '\'''\'' sequences that result from multiple
			# adjacent quotes in he input.
			qarg=$(printf "%s\n" "$arg" |
			    ${SED:-sed} -e "s/'/'\\\\''/g" \
				-e "1s/^/'/" -e "\$s/\$/'/" \
				-e "1s/^''//" -e "\$s/''\$//" \
				-e "s/'''/'/g"
			    )
			;;
		*)
			# Arg is not the empty string, and does not contain
			# any unsafe characters.  Leave it unchanged for
			# readability.
			qarg="${arg}"
			;;
		esac
		result="${result}${result:+ }${qarg}"
	done
	printf "%s\n" "$result"
)

statusmsg()
{
	${runcmd} echo "===> $*" | tee -a "${results}"
}

statusmsg2()
{
	local msg

	msg=${1}
	shift

	case "${msg}" in
	????????????????*)	;;
	??????????*)		msg="${msg}      ";;
	?????*)			msg="${msg}           ";;
	*)			msg="${msg}                ";;
	esac
	case "${msg}" in
	?????????????????????*)	;;
	????????????????????)	msg="${msg} ";;
	???????????????????)	msg="${msg}  ";;
	??????????????????)	msg="${msg}   ";;
	?????????????????)	msg="${msg}    ";;
	????????????????)	msg="${msg}     ";;
	esac
	statusmsg "${msg}$*"
}

warning()
{
	statusmsg "Warning: $*"
}

# Find a program in the PATH, and show the result.  If not found,
# show a default.  If $2 is defined (even if it is an empty string),
# then that is the default; otherwise, $1 is used as the default.
#
find_in_PATH()
{
	local prog="$1"
	local result="${2-$1}"
	local dir
	local IFS=:

	for dir in ${PATH}
	do
		if [ -x "${dir}/${prog}" ]
		then
			result=${dir}/${prog}
			break
		fi
	done
	echo "${result}"
}

# Try to find a working POSIX shell, and set HOST_SH to refer to it.
# Assumes that uname_s, uname_m, and PWD have been set.
#
set_HOST_SH()
{
	# Even if ${HOST_SH} is already defined, we still do the
	# sanity checks at the end.

	# Solaris has /usr/xpg4/bin/sh.
	#
	[ -z "${HOST_SH}" ] &&
		[ "${uname_s}" = SunOS ] &&
		[ -x /usr/xpg4/bin/sh ] &&
			HOST_SH=/usr/xpg4/bin/sh

	# Try to get the name of the shell that's running this script,
	# by parsing the output from "ps".  We assume that, if the host
	# system's ps command supports -o comm at all, it will do so
	# in the usual way: a one-line header followed by a one-line
	# result, possibly including trailing white space.  And if the
	# host system's ps command doesn't support -o comm, we assume
	# that we'll get an error message on stderr and nothing on
	# stdout.  (We don't try to use ps -o 'comm=' to suppress the
	# header line, because that is less widely supported.)
	#
	# If we get the wrong result here, the user can override it by
	# specifying HOST_SH in the environment.
	#
	[ -z "${HOST_SH}" ] && HOST_SH=$(
		(
			ps -p $$ -o comm |
			    sed -ne "2s/[ ${tab}]*\$//p"
		) 2>/dev/null
	)

	# If nothing above worked, use "sh".  We will later find the
	# first directory in the PATH that has a "sh" program.
	#
	[ -z "${HOST_SH}" ] && HOST_SH="sh"

	# If the result so far is not an absolute path, try to prepend
	# PWD or search the PATH.
	#
	case "${HOST_SH}" in
	/*)	:
		;;
	*/*)	HOST_SH="${PWD}/${HOST_SH}"
		;;
	*)	HOST_SH="$(find_in_PATH "${HOST_SH}")"
		;;
	esac

	# If we don't have an absolute path by now, bomb.
	#
	case "${HOST_SH}" in
	/*)	:
		;;
	*)	bomb "HOST_SH=\"${HOST_SH}\" is not an absolute path"
		;;
	esac

	# If HOST_SH is not executable, bomb.
	#
	[ -x "${HOST_SH}" ] ||
	    bomb "HOST_SH=\"${HOST_SH}\" is not executable"

	# If HOST_SH fails tests, bomb.
	# ("$0" may be a path that is no longer valid, because we have
	# performed "cd $(dirname $0)", so don't use $0 here.)
	#
	"${HOST_SH}" build.sh --shelltest ||
	    bomb "HOST_SH=\"${HOST_SH}\" failed functionality tests"
}

# initdefaults --
# Set defaults before parsing command line options.
#
initdefaults()
{
	makeenv=
	makewrapper=
	makewrappermachine=
	runcmd=
	operations=
	removedirs=

	[ -d usr.bin/make ] || cd "$(dirname $0)"
	[ -d usr.bin/make ] ||
	    bomb "usr.bin/make not found; build.sh must be run from" \
	         "the top level of source directory"
	[ -f share/mk/bsd.own.mk ] ||
	    bomb "src/share/mk is missing; please re-fetch the source tree"

	# Set various environment variables to known defaults,
	# to minimize (cross-)build problems observed "in the field".
	#
	# LC_ALL=C must be set before we try to parse the output from
	# any command.  Other variables are set (or unset) here, before
	# we parse command line arguments.
	#
	# These variables can be overridden via "-V var=value" if
	# you know what you are doing.
	#
	unsetmakeenv C_INCLUDE_PATH
	unsetmakeenv CPLUS_INCLUDE_PATH
	unsetmakeenv INFODIR
	unsetmakeenv LESSCHARSET
	unsetmakeenv MAKEFLAGS
	unsetmakeenv TERMINFO
	setmakeenv LC_ALL C

	# Find information about the build platform.  This should be
	# kept in sync with _HOST_OSNAME, _HOST_OSREL, and _HOST_ARCH
	# variables in share/mk/bsd.sys.mk.
	#
	# Note that "uname -p" is not part of POSIX, but we want uname_p
	# to be set to the host MACHINE_ARCH, if possible.  On systems
	# where "uname -p" fails, shows "unknown", or shows a string
	# that does not look like an identifier, fall back to using the
	# output from "uname -m" instead.
	#
	uname_s=$(uname -s 2>/dev/null)
	uname_r=$(uname -r 2>/dev/null)
	uname_m=$(uname -m 2>/dev/null)
	uname_p=$(uname -p 2>/dev/null || echo "unknown")
	case "${uname_p}" in
	''|unknown|*[!-_A-Za-z0-9]*) uname_p="${uname_m}" ;;
	esac

	id_u=$(id -u 2>/dev/null || /usr/xpg4/bin/id -u 2>/dev/null)

	# If $PWD is a valid name of the current directory, POSIX mandates
	# that pwd return it by default which causes problems in the
	# presence of symlinks.  Unsetting PWD is simpler than changing
	# every occurrence of pwd to use -P.
	#
	# XXX Except that doesn't work on Solaris. Or many Linuces.
	#     And the standard says altering PWD produces unspecified results
	# So instead just cd -P to $PWD which should make PWD be symlink free
	#
	cd -P "${PWD}" || bomb "Cannot cd to \$PWD ($PWD)"
	# At this point TOP=$PWD should just work, but let's be ultra safe.
	TOP=$( (exec pwd -P 2>/dev/null) || (exec pwd 2>/dev/null) )

	# The user can set HOST_SH in the environment, or we try to
	# guess an appropriate value.  Then we set several other
	# variables from HOST_SH.
	#
	set_HOST_SH
	setmakeenv HOST_SH "${HOST_SH}"
	setmakeenv BSHELL "${HOST_SH}"
	setmakeenv CONFIG_SHELL "${HOST_SH}"

	# Set defaults.
	#
	toolprefix=nb

	# Some systems have a small ARG_MAX.  -X prevents make(1) from
	# exporting variables in the environment redundantly.
	#
	case "${uname_s}" in
	Darwin | FreeBSD | CYGWIN*)
		MAKEFLAGS="-X ${MAKEFLAGS}"
		;;
	esac

	# do_{operation}=true if given operation is requested.
	#
	do_expertmode=false
	do_rebuildmake=false
	do_removedirs=false
	do_tools=false
	do_libs=false
	do_cleandir=false
	do_obj=false
	do_build=false
	do_distribution=false
	do_release=false
	do_kernel=false
	do_releasekernel=false
	do_kernels=false
	do_modules=false
	do_installmodules=false
	do_install=false
	do_sets=false
	do_sourcesets=false
	do_syspkgs=false
	do_pkg=false
	do_iso_image=false
	do_iso_image_source=false
	do_live_image=false
	do_install_image=false
	do_disk_image=false
	do_params=false
	do_show_params=false
	do_rump=false
	do_dtb=false

	# done_{operation}=true if given operation has been done.
	#
	done_rebuildmake=false

	# Create scratch directory
	#
	tmpdir="${TMPDIR-/tmp}/nbbuild$$"
	mkdir "${tmpdir}" || bomb "Cannot mkdir: ${tmpdir}"
	trap "cd /; rm -r -f \"${tmpdir}\"" 0
	results="${tmpdir}/build.sh.results"

	# Set source directories
	#
	setmakeenv NETBSDSRCDIR "${TOP}"

	# Make sure KERNOBJDIR is an absolute path if defined
	#
	case "${KERNOBJDIR}" in
	''|/*)	;;
	*)	KERNOBJDIR="${TOP}/${KERNOBJDIR}"
		setmakeenv KERNOBJDIR "${KERNOBJDIR}"
		;;
	esac

	# Find the version of NetBSD
	#
	DISTRIBVER="$(${HOST_SH} ${TOP}/sys/conf/osrelease.sh)"

	# Set the BUILDSEED to NetBSD-"N"
	#
	setmakeenv BUILDSEED \
		"NetBSD-$(${HOST_SH} ${TOP}/sys/conf/osrelease.sh -m)"

	# Set MKARZERO to "yes"
	#
	setmakeenv MKARZERO yes

}

# valid_MACHINE_ARCH -- A multi-line string, listing all valid
# MACHINE/MACHINE_ARCH pairs.
#
# Each line contains a MACHINE and MACHINE_ARCH value, an optional ALIAS
# which may be used to refer to the MACHINE/MACHINE_ARCH pair, and an
# optional DEFAULT or NO_DEFAULT keyword.
#
# When a MACHINE corresponds to multiple possible values of
# MACHINE_ARCH, then this table should list all allowed combinations.
# If the MACHINE is associated with a default MACHINE_ARCH (to be
# used when the user specifies the MACHINE but fails to specify the
# MACHINE_ARCH), then one of the lines should have the "DEFAULT"
# keyword.  If there is no default MACHINE_ARCH for a particular
# MACHINE, then there should be a line with the "NO_DEFAULT" keyword,
# and with a blank MACHINE_ARCH.
#
valid_MACHINE_ARCH='
MACHINE=acorn32		MACHINE_ARCH=earmv4	ALIAS=eacorn32 DEFAULT
MACHINE=algor		MACHINE_ARCH=mips64el	ALIAS=algor64
MACHINE=algor		MACHINE_ARCH=mipsel	DEFAULT
MACHINE=alpha		MACHINE_ARCH=alpha
MACHINE=amd64		MACHINE_ARCH=x86_64
MACHINE=amiga		MACHINE_ARCH=m68k
MACHINE=amigappc	MACHINE_ARCH=powerpc
MACHINE=arc		MACHINE_ARCH=mips64el	ALIAS=arc64
MACHINE=arc		MACHINE_ARCH=mipsel	DEFAULT
MACHINE=atari		MACHINE_ARCH=m68k
MACHINE=bebox		MACHINE_ARCH=powerpc
MACHINE=cats		MACHINE_ARCH=earmv4	ALIAS=ecats DEFAULT
MACHINE=cesfic		MACHINE_ARCH=m68k
MACHINE=cobalt		MACHINE_ARCH=mips64el	ALIAS=cobalt64
MACHINE=cobalt		MACHINE_ARCH=mipsel	DEFAULT
MACHINE=dreamcast	MACHINE_ARCH=sh3el
MACHINE=emips		MACHINE_ARCH=mipseb
MACHINE=epoc32		MACHINE_ARCH=earmv4	ALIAS=eepoc32 DEFAULT
MACHINE=evbarm		MACHINE_ARCH=		NO_DEFAULT
MACHINE=evbarm		MACHINE_ARCH=earmv4	ALIAS=evbearmv4-el	ALIAS=evbarmv4-el
MACHINE=evbarm		MACHINE_ARCH=earmv4eb	ALIAS=evbearmv4-eb	ALIAS=evbarmv4-eb
MACHINE=evbarm		MACHINE_ARCH=earmv5	ALIAS=evbearmv5-el	ALIAS=evbarmv5-el
MACHINE=evbarm		MACHINE_ARCH=earmv5hf	ALIAS=evbearmv5hf-el	ALIAS=evbarmv5hf-el
MACHINE=evbarm		MACHINE_ARCH=earmv5eb	ALIAS=evbearmv5-eb	ALIAS=evbarmv5-eb
MACHINE=evbarm		MACHINE_ARCH=earmv5hfeb	ALIAS=evbearmv5hf-eb	ALIAS=evbarmv5hf-eb
MACHINE=evbarm		MACHINE_ARCH=earmv6	ALIAS=evbearmv6-el	ALIAS=evbarmv6-el
MACHINE=evbarm		MACHINE_ARCH=earmv6hf	ALIAS=evbearmv6hf-el	ALIAS=evbarmv6hf-el
MACHINE=evbarm		MACHINE_ARCH=earmv6eb	ALIAS=evbearmv6-eb	ALIAS=evbarmv6-eb
MACHINE=evbarm		MACHINE_ARCH=earmv6hfeb	ALIAS=evbearmv6hf-eb	ALIAS=evbarmv6hf-eb
MACHINE=evbarm		MACHINE_ARCH=earmv7	ALIAS=evbearmv7-el	ALIAS=evbarmv7-el
MACHINE=evbarm		MACHINE_ARCH=earmv7eb	ALIAS=evbearmv7-eb	ALIAS=evbarmv7-eb
MACHINE=evbarm		MACHINE_ARCH=earmv7hf	ALIAS=evbearmv7hf-el	ALIAS=evbarmv7hf-el
MACHINE=evbarm		MACHINE_ARCH=earmv7hfeb	ALIAS=evbearmv7hf-eb	ALIAS=evbarmv7hf-eb
MACHINE=evbarm		MACHINE_ARCH=aarch64	ALIAS=evbarm64-el	ALIAS=evbarm64
MACHINE=evbarm		MACHINE_ARCH=aarch64eb	ALIAS=evbarm64-eb
MACHINE=evbcf		MACHINE_ARCH=coldfire
MACHINE=evbmips		MACHINE_ARCH=		NO_DEFAULT
MACHINE=evbmips		MACHINE_ARCH=mips64eb	ALIAS=evbmips64-eb
MACHINE=evbmips		MACHINE_ARCH=mips64el	ALIAS=evbmips64-el
MACHINE=evbmips		MACHINE_ARCH=mipseb	ALIAS=evbmips-eb
MACHINE=evbmips		MACHINE_ARCH=mipsel	ALIAS=evbmips-el
MACHINE=evbmips		MACHINE_ARCH=mipsn64eb	ALIAS=evbmipsn64-eb
MACHINE=evbmips		MACHINE_ARCH=mipsn64el	ALIAS=evbmipsn64-el
MACHINE=evbppc		MACHINE_ARCH=powerpc	DEFAULT
MACHINE=evbppc		MACHINE_ARCH=powerpc64	ALIAS=evbppc64
MACHINE=evbsh3		MACHINE_ARCH=		NO_DEFAULT
MACHINE=evbsh3		MACHINE_ARCH=sh3eb	ALIAS=evbsh3-eb
MACHINE=evbsh3		MACHINE_ARCH=sh3el	ALIAS=evbsh3-el
MACHINE=ews4800mips	MACHINE_ARCH=mipseb
MACHINE=hp300		MACHINE_ARCH=m68k
MACHINE=hppa		MACHINE_ARCH=hppa
MACHINE=hpcarm		MACHINE_ARCH=earmv4	ALIAS=hpcearm DEFAULT
MACHINE=hpcmips		MACHINE_ARCH=mipsel
MACHINE=hpcsh		MACHINE_ARCH=sh3el
MACHINE=i386		MACHINE_ARCH=i386
MACHINE=ia64		MACHINE_ARCH=ia64
MACHINE=ibmnws		MACHINE_ARCH=powerpc
MACHINE=iyonix		MACHINE_ARCH=earm	ALIAS=eiyonix DEFAULT
MACHINE=landisk		MACHINE_ARCH=sh3el
MACHINE=luna68k		MACHINE_ARCH=m68k
MACHINE=mac68k		MACHINE_ARCH=m68k
MACHINE=macppc		MACHINE_ARCH=powerpc	DEFAULT
MACHINE=macppc		MACHINE_ARCH=powerpc64	ALIAS=macppc64
MACHINE=mipsco		MACHINE_ARCH=mipseb
MACHINE=mmeye		MACHINE_ARCH=sh3eb
MACHINE=mvme68k		MACHINE_ARCH=m68k
MACHINE=mvmeppc		MACHINE_ARCH=powerpc
MACHINE=netwinder	MACHINE_ARCH=earmv4	ALIAS=enetwinder DEFAULT
MACHINE=news68k		MACHINE_ARCH=m68k
MACHINE=newsmips	MACHINE_ARCH=mipseb
MACHINE=next68k		MACHINE_ARCH=m68k
MACHINE=ofppc		MACHINE_ARCH=powerpc	DEFAULT
MACHINE=ofppc		MACHINE_ARCH=powerpc64	ALIAS=ofppc64
MACHINE=or1k		MACHINE_ARCH=or1k
MACHINE=playstation2	MACHINE_ARCH=mipsel
MACHINE=pmax		MACHINE_ARCH=mips64el	ALIAS=pmax64
MACHINE=pmax		MACHINE_ARCH=mipsel	DEFAULT
MACHINE=prep		MACHINE_ARCH=powerpc
MACHINE=riscv		MACHINE_ARCH=riscv64	ALIAS=riscv64 DEFAULT
MACHINE=riscv		MACHINE_ARCH=riscv32	ALIAS=riscv32
MACHINE=rs6000		MACHINE_ARCH=powerpc
MACHINE=sandpoint	MACHINE_ARCH=powerpc
MACHINE=sbmips		MACHINE_ARCH=		NO_DEFAULT
MACHINE=sbmips		MACHINE_ARCH=mips64eb	ALIAS=sbmips64-eb
MACHINE=sbmips		MACHINE_ARCH=mips64el	ALIAS=sbmips64-el
MACHINE=sbmips		MACHINE_ARCH=mipseb	ALIAS=sbmips-eb
MACHINE=sbmips		MACHINE_ARCH=mipsel	ALIAS=sbmips-el
MACHINE=sgimips		MACHINE_ARCH=mips64eb	ALIAS=sgimips64
MACHINE=sgimips		MACHINE_ARCH=mipseb	DEFAULT
MACHINE=shark		MACHINE_ARCH=earmv4	ALIAS=eshark DEFAULT
MACHINE=sparc		MACHINE_ARCH=sparc
MACHINE=sparc64		MACHINE_ARCH=sparc64
MACHINE=sun2		MACHINE_ARCH=m68000
MACHINE=sun3		MACHINE_ARCH=m68k
MACHINE=vax		MACHINE_ARCH=vax
MACHINE=virt68k		MACHINE_ARCH=m68k
MACHINE=x68k		MACHINE_ARCH=m68k
MACHINE=zaurus		MACHINE_ARCH=earm	ALIAS=ezaurus DEFAULT
'

# getarch -- find the default MACHINE_ARCH for a MACHINE,
# or convert an alias to a MACHINE/MACHINE_ARCH pair.
#
# Saves the original value of MACHINE in makewrappermachine before
# alias processing.
#
# Sets MACHINE and MACHINE_ARCH if the input MACHINE value is
# recognised as an alias, or recognised as a machine that has a default
# MACHINE_ARCH (or that has only one possible MACHINE_ARCH).
#
# Leaves MACHINE and MACHINE_ARCH unchanged if MACHINE is recognised
# as being associated with multiple MACHINE_ARCH values with no default.
#
# Bombs if MACHINE is not recognised.
#
getarch()
{
	local IFS
	local found=
	local line

	IFS="${nl}"
	makewrappermachine="${MACHINE}"
	for line in ${valid_MACHINE_ARCH}
	do
		line="${line%%#*}" # ignore comments
		line="$( IFS=" ${tab}" ; echo $line )" # normalise white space
		case "${line} " in
		' ')
			# skip blank lines or comment lines
			continue
			;;
		*" ALIAS=${MACHINE} "*)
			# Found a line with a matching ALIAS=<alias>.
			found="$line"
			break
			;;
		"MACHINE=${MACHINE} "*" NO_DEFAULT"*)
			# Found an explicit "NO_DEFAULT" for this MACHINE.
			found="$line"
			break
			;;
		"MACHINE=${MACHINE} "*" DEFAULT"*)
			# Found an explicit "DEFAULT" for this MACHINE.
			found="$line"
			break
			;;
		"MACHINE=${MACHINE} "*)
			# Found a line for this MACHINE.  If it's the
			# first such line, then tentatively accept it.
			# If it's not the first matching line, then
			# remember that there was more than one match.
			case "$found" in
			'')	found="$line" ;;
			*)	found="MULTIPLE_MATCHES" ;;
			esac
			;;
		esac
	done

	case "$found" in
	*NO_DEFAULT*|*MULTIPLE_MATCHES*)
		# MACHINE is OK, but MACHINE_ARCH is still unknown
		return
		;;
	"MACHINE="*" MACHINE_ARCH="*)
		# Obey the MACHINE= and MACHINE_ARCH= parts of the line.
		IFS=' '
		for frag in ${found}
		do
			case "$frag" in
			MACHINE=*|MACHINE_ARCH=*)
				eval "$frag"
				;;
			esac
		done
		;;
	*)
		bomb "Unknown target MACHINE: ${MACHINE}"
		;;
	esac
}

# validatearch -- check that the MACHINE/MACHINE_ARCH pair is supported.
#
# Bombs if the pair is not supported.
#
validatearch()
{
	local IFS
	local line
	local foundpair=false foundmachine=false foundarch=false

	case "${MACHINE_ARCH}" in
	"")
		bomb "No MACHINE_ARCH provided." \
		     "Use 'build.sh -m ${MACHINE} list-arch' to show options"
		;;
	esac

	IFS="${nl}"
	for line in ${valid_MACHINE_ARCH}
	do
		line="${line%%#*}" # ignore comments
		line="$( IFS=" ${tab}" ; echo $line )" # normalise white space
		case "${line} " in
		' ')
			# skip blank lines or comment lines
			continue
			;;
		"MACHINE=${MACHINE} MACHINE_ARCH=${MACHINE_ARCH} "*)
			foundpair=true
			;;
		"MACHINE=${MACHINE} "*)
			foundmachine=true
			;;
		*"MACHINE_ARCH=${MACHINE_ARCH} "*)
			foundarch=true
			;;
		esac
	done

	case "${foundpair}:${foundmachine}:${foundarch}" in
	true:*)
		: OK
		;;
	*:false:*)
		bomb "Unknown target MACHINE: ${MACHINE}"
		;;
	*:*:false)
		bomb "Unknown target MACHINE_ARCH: ${MACHINE_ARCH}"
		;;
	*)
		bomb "MACHINE_ARCH '${MACHINE_ARCH}' does not support MACHINE '${MACHINE}'"
		;;
	esac
}

# listarch -- list valid MACHINE/MACHINE_ARCH/ALIAS values,
# optionally restricted to those where the MACHINE and/or MACHINE_ARCH
# match specified glob patterns.
#
listarch()
{
	local machglob="$1" archglob="$2"
	local IFS
	local wildcard='*'
	local line xline frag
	local line_matches_machine line_matches_arch
	local found=false

	# Empty machglob or archglob should match anything
	: "${machglob:=${wildcard}}"
	: "${archglob:=${wildcard}}"

	IFS="${nl}"
	for line in ${valid_MACHINE_ARCH}
	do
		line="${line%%#*}" # ignore comments
		xline="$( IFS=" ${tab}" ; echo $line )" # normalise white space
		[ -z "${xline}" ] && continue # skip blank or comment lines

		line_matches_machine=false
		line_matches_arch=false

		IFS=' '
		for frag in ${xline}
		do
			case "${frag}" in
			MACHINE=${machglob})
				line_matches_machine=true ;;
			ALIAS=${machglob})
				line_matches_machine=true ;;
			MACHINE_ARCH=${archglob})
				line_matches_arch=true ;;
			esac
		done

		if $line_matches_machine && $line_matches_arch
		then
			found=true
			echo "$line"
		fi
	done
	if ! $found
	then
		echo >&2 "No match for" \
		    "MACHINE=${machglob} MACHINE_ARCH=${archglob}"
		return 1
	fi
	return 0
}

# nobomb_getmakevar --
# Given the name of a make variable in $1, show make's idea of the
# value of that variable, or return 1 if there's an error.
#
nobomb_getmakevar()
{
	[ -x "${make}" ] || return 1
	"${make}" -m ${TOP}/share/mk -s -B -f- _x_ <<EOF || return 1
_x_:
	echo \${$1}
.include <bsd.prog.mk>
.include <bsd.kernobj.mk>
EOF
}

# bomb_getmakevar --
# Given the name of a make variable in $1, show make's idea of the
# value of that variable, or bomb if there's an error.
#
bomb_getmakevar()
{
	[ -x "${make}" ] || bomb "bomb_getmakevar $1: ${make} is not executable"
	nobomb_getmakevar "$1" || bomb "bomb_getmakevar $1: ${make} failed"
}

# getmakevar --
# Given the name of a make variable in $1, show make's idea of the
# value of that variable, or show a literal '$' followed by the
# variable name if ${make} is not executable.  This is intended for use in
# messages that need to be readable even if $make hasn't been built,
# such as when build.sh is run with the "-n" option.
#
getmakevar()
{
	if [ -x "${make}" ]
	then
		bomb_getmakevar "$1"
	else
		echo "\$$1"
	fi
}

setmakeenv()
{
	eval "$1='$2'; export $1"
	makeenv="${makeenv} $1"
}

safe_setmakeenv()
{
	case "$1" in

	#	Look for any vars we want to prohibit here, like:
	# Bad | Dangerous)	usage "Cannot override $1 with -V";;

	# That first char is OK has already been verified.
	*[!A-Za-z0-9_]*)	usage "Bad variable name (-V): '$1'";;
	esac
	setmakeenv "$@"
}

unsetmakeenv()
{
	eval "unset $1"
	makeenv="${makeenv} $1"
}

safe_unsetmakeenv()
{
	case "$1" in

	#	Look for any vars user should not be able to unset
	# Needed | Must_Have)	usage "Variable $1 cannot be unset";;

	[!A-Za-z_]* | *[!A-Za-z0-9_]*)	usage "Bad variable name (-Z): '$1'";;
	esac
	unsetmakeenv "$1"
}

# Clear all variables defined in makeenv.  Used to run a subprocess
# outside the usual NetBSD build's make environment.
#
clearmakeenv()
{
	local var

	for var in ${makeenv}
	do
		unset ${var}
	done
}

# Given a variable name in $1, modify the variable in place as follows:
# For each space-separated word in the variable, call resolvepath.
#
resolvepaths()
{
	local var="$1"
	local val
	eval val=\"\${${var}}\"
	local newval=
	local word

	for word in ${val}
	do
		resolvepath word
		newval="${newval}${newval:+ }${word}"
	done
	eval ${var}=\"\${newval}\"
}

# Given a variable name in $1, modify the variable in place as follows:
# Convert possibly-relative path to absolute path by prepending
# ${TOP} if necessary.  Also delete trailing "/", if any.
#
resolvepath()
{
	local var="$1"
	local val
	eval val=\"\${${var}}\"
	case "${val}" in
	/)
		;;
	/*)
		val="${val%/}"
		;;
	*)
		val="${TOP}/${val%/}"
		;;
	esac
	eval ${var}=\"\${val}\"
}

# Show synopsis to stdout.
#
synopsis()
{
	cat <<_usage_

Usage: ${progname} [-EnoPRrUux] [-a ARCH] [-B BID] [-C EXTRAS]
                [-c COMPILER] [-D DEST] [-j NJOB] [-M MOBJ] [-m MACH]
                [-N NOISY] [-O OOBJ] [-R RELEASE] [-S SEED] [-T TOOLS]
                [-V VAR=[VALUE]] [-w WRAPPER] [-X X11SRC]
                [-Z VAR]
                OPERATION ...
       ${progname} ( -h | -? )

_usage_
}

# Show help to stdout.
#
help()
{
	synopsis
	cat <<_usage_
 Build OPERATIONs (all imply "obj" and "tools"):
    build               Run "make build".
    distribution        Run "make distribution" (includes DESTDIR/etc/ files).
    release             Run "make release" (includes kernels & distrib media).

 Other OPERATIONs:
    help                Show this help message, and exit.
    makewrapper         Create ${toolprefix}make-\${MACHINE} wrapper
                        and ${toolprefix}make.
                        Always performed.
    cleandir            Run "make cleandir".  [Default unless -u is used]
    dtb                 Build devicetree blobs.
    obj                 Run "make obj".  [Default unless -o is used]
    tools               Build and install tools.
    install=IDIR        Run "make installworld" to IDIR to install all sets
                        except 'etc'.  Useful after "distribution" or "release".
    kernel=CONF         Build kernel with config file CONF.
    kernel.gdb=CONF     Build kernel (including netbsd.gdb) with config
                        file CONF.
    releasekernel=CONF  Install kernel built by kernel=CONF to RELEASEDIR.
    kernels             Build all kernels.
    installmodules=IDIR Run "make installmodules" to IDIR to install all
                        kernel modules.
    modules             Build kernel modules.
    rumptest            Do a linktest for rump (for developers).
    sets                Create binary sets in
                        RELEASEDIR/RELEASEMACHINEDIR/binary/sets.
                        DESTDIR should be populated beforehand.
    distsets            Same as "distribution sets".
    sourcesets          Create source sets in RELEASEDIR/source/sets.
    syspkgs             Create syspkgs in
                        RELEASEDIR/RELEASEMACHINEDIR/binary/syspkgs.
    pkg=CATEGORY/PKG    (EXPERIMENT) Build a package CATEGORY/PKG from pkgsrc.
    iso-image           Create CD-ROM image in RELEASEDIR/images.
    iso-image-source    Create CD-ROM image with source in RELEASEDIR/images.
    live-image          Create bootable live image in
                        RELEASEDIR/RELEASEMACHINEDIR/installation/liveimage.
    install-image       Create bootable installation image in
                        RELEASEDIR/RELEASEMACHINEDIR/installation/installimage.
    disk-image=TARGET   Create bootable disk image in
                        RELEASEDIR/RELEASEMACHINEDIR/binary/gzimg/TARGET.img.gz.
    params              Create params file with various make(1) parameters.
    show-params         Show various make(1) parameters.
    list-arch           Show a list of valid MACHINE/MACHINE_ARCH values,
                        and exit.  The list may be narrowed by passing glob
                        patterns or exact values in MACHINE or MACHINE_ARCH.
    mkrepro-timestamp   Show the latest source timestamp used for reproducible
                        builds and exit.  Requires -P or -V MKREPRO=yes.
    show-revisionid	Show the revision ID of the current directory
			(in SCM-dependent format) and exit.
			Requires -P or -V MKREPRO=yes.

 Options:
    -a ARCH        Set MACHINE_ARCH=ARCH.  [Default: deduced from MACHINE]
    -B BID         Set BUILDID=BID.
    -C EXTRAS      Append EXTRAS to CDEXTRA for inclusion on CD-ROM.
    -c COMPILER    Select compiler from COMPILER:
                       clang
                       gcc
                   [Default: gcc]
    -D DEST        Set DESTDIR=DEST.  [Default: destdir.\${MACHINE}]
    -E             Set "expert" mode; disables various safety checks.
                   Should not be used without expert knowledge of the build
                   system.
    -h             Show this help message, and exit.
    -j NJOB        Run up to NJOB jobs in parallel; see make(1) -j.
    -M MOBJ        Set obj root directory to MOBJ; sets MAKEOBJDIRPREFIX=MOBJ,
                   unsets MAKEOBJDIR.
    -m MACH        Set MACHINE=MACH.  Some MACH values are actually
                   aliases that set MACHINE/MACHINE_ARCH pairs.
                   [Default: deduced from the host system if the host
                   OS is NetBSD]
    -N NOISY       Set the noisiness (MAKEVERBOSE) level of the build to NOISY:
                       0   Minimal output ("quiet").
                       1   Describe what is occurring.
                       2   Describe what is occurring and echo the actual
                           command.
                       3   Ignore the effect of the "@" prefix in make
                           commands.
                       4   Trace shell commands using the shell's -x flag.
                   [Default: 2]
    -n             Show commands that would be executed, but do not execute
                   them.
    -O OOBJ        Set obj root directory to OOBJ; sets a MAKEOBJDIR pattern
                   using OOBJ, unsets MAKEOBJDIRPREFIX.
    -o             Set MKOBJDIRS=no; do not create objdirs at start of build.
    -P             Set MKREPRO and MKREPRO_TIMESTAMP to the latest source
                   CVS timestamp for reproducible builds.
    -R RELEASE     Set RELEASEDIR=RELEASE.  [Default: releasedir]
    -r             Remove contents of TOOLDIR and DESTDIR before building.
    -S SEED        Set BUILDSEED=SEED.  [Default: NetBSD-majorversion]
    -T TOOLS       Set TOOLDIR=TOOLS.  If unset, and TOOLDIR is not set
                   in the environment, ${toolprefix}make will be (re)built
                   unconditionally.
    -U             Set MKUNPRIVED=yes; build without requiring root privileges,
                   install from an unprivileged build with proper file
                   permissions.
    -u             Set MKUPDATE=yes; do not run "make cleandir" first.
                   Without this, everything is rebuilt, including the tools.
    -V VAR=[VALUE] Set variable VAR=VALUE.
    -w WRAPPER     Create ${toolprefix}make script as WRAPPER.
                   [Default: \${TOOLDIR}/bin/${toolprefix}make-\${MACHINE}]
    -X X11SRC      Set X11SRCDIR=X11SRC.  [Default: /usr/xsrc]
    -x             Set MKX11=yes; build X11 from X11SRCDIR.
    -Z VAR         Unset ("zap") variable VAR.
    -?             Show this help message, and exit.

_usage_
}

# Show optional error message, help to stderr, and exit 1.
#
usage()
{
	if [ -n "$*" ]
	then
		echo 1>&2 ""
		echo 1>&2 "${progname}: $*"
	fi
	synopsis 1>&2
	exit 1
}

parseoptions()
{
	opts=a:B:C:c:D:Ehj:M:m:N:nO:oPR:rS:T:UuV:w:X:xZ:
	opt_a=false
	opt_m=false
	local did_show_info=false

	if type getopts >/dev/null 2>&1
	then
		# Use POSIX getopts.
		#
		getoptcmd='getopts :${opts} opt && opt=-${opt}'
		optargcmd=':'
		optremcmd='shift $((${OPTIND} -1))'
	else
		type getopt >/dev/null 2>&1 ||
		    bomb "Shell does not support getopts or getopt"

		# Use old-style getopt(1) (doesn't handle whitespace in args).
		#
		args="$(getopt ${opts} $*)"
		[ $? = 0 ] || usage
		set -- ${args}

		getoptcmd='[ $# -gt 0 ] && opt="$1" && shift'
		optargcmd='OPTARG="$1"; shift'
		optremcmd=':'
	fi

	# Parse command line options.
	#
	while eval ${getoptcmd}
	do
		case ${opt} in

		-a)
			eval ${optargcmd}
			MACHINE_ARCH=${OPTARG}
			opt_a=true
			;;

		-B)
			eval ${optargcmd}
			BUILDID=${OPTARG}
			;;

		-C)
			eval ${optargcmd}
			resolvepaths OPTARG
			CDEXTRA="${CDEXTRA}${CDEXTRA:+ }${OPTARG}"
			;;

		-c)
			eval ${optargcmd}
			case "${OPTARG}" in
			gcc)	# default, no variables needed
				;;
			clang)	setmakeenv HAVE_LLVM yes
				setmakeenv MKLLVM yes
				setmakeenv MKGCC no
				;;
			#pcc)	...
			#	;;
			*)	bomb "Unknown compiler: ${OPTARG}"
			esac
			;;

		-D)
			eval ${optargcmd}
			resolvepath OPTARG
			setmakeenv DESTDIR "${OPTARG}"
			;;

		-E)
			do_expertmode=true
			;;

		-j)
			eval ${optargcmd}
			parallel="-j ${OPTARG}"
			;;

		-M)
			eval ${optargcmd}
			resolvepath OPTARG
			case "${OPTARG}" in
			\$*)	usage "-M argument must not begin with '\$'"
				;;
			*\$*)	# can use resolvepath, but can't set TOP_objdir
				resolvepath OPTARG
				;;
			*)	resolvepath OPTARG
				TOP_objdir="${OPTARG}${TOP}"
				;;
			esac
			unsetmakeenv MAKEOBJDIR
			setmakeenv MAKEOBJDIRPREFIX "${OPTARG}"
			;;

			# -m overrides MACHINE_ARCH unless "-a" is specified
		-m)
			eval ${optargcmd}
			MACHINE="${OPTARG}"
			opt_m=true
			;;

		-N)
			eval ${optargcmd}
			case "${OPTARG}" in
			0|1|2|3|4)
				setmakeenv MAKEVERBOSE "${OPTARG}"
				;;
			*)
				usage "'${OPTARG}' is not a valid value for -N"
				;;
			esac
			;;

		-n)
			runcmd=echo
			;;

		-O)
			eval ${optargcmd}
			case "${OPTARG}" in
			*\$*)	usage "-O argument must not contain '\$'"
				;;
			*)	resolvepath OPTARG
				TOP_objdir="${OPTARG}"
				;;
			esac
			unsetmakeenv MAKEOBJDIRPREFIX
			setmakeenv MAKEOBJDIR "\${.CURDIR:C,^$TOP,$OPTARG,}"
			;;

		-o)
			MKOBJDIRS=no
			;;

		-P)
			MKREPRO=yes
			export MKREPRO
			;;

		-R)
			eval ${optargcmd}
			resolvepath OPTARG
			setmakeenv RELEASEDIR "${OPTARG}"
			;;

		-r)
			do_removedirs=true
			do_rebuildmake=true
			;;

		-S)
			eval ${optargcmd}
			setmakeenv BUILDSEED "${OPTARG}"
			;;

		-T)
			eval ${optargcmd}
			resolvepath OPTARG
			TOOLDIR="${OPTARG}"
			export TOOLDIR
			;;

		-U)
			setmakeenv MKUNPRIVED yes
			;;

		-u)
			setmakeenv MKUPDATE yes
			;;

		-V)
			eval ${optargcmd}
			case "${OPTARG}" in
		    # XXX: consider restricting which variables can be changed?
			[a-zA-Z_]*=*)
				safe_setmakeenv "${OPTARG%%=*}" "${OPTARG#*=}"
				;;
			[a-zA-Z_]*)
				safe_setmakeenv "${OPTARG}" ""
				;;
			*)
				usage "-V argument must be of the form 'VAR[=VALUE]'"
				;;
			esac
			;;

		-w)
			eval ${optargcmd}
			resolvepath OPTARG
			makewrapper="${OPTARG}"
			;;

		-X)
			eval ${optargcmd}
			resolvepath OPTARG
			setmakeenv X11SRCDIR "${OPTARG}"
			;;

		-x)
			setmakeenv MKX11 yes
			;;

		-Z)
			eval ${optargcmd}
		    # XXX: consider restricting which variables can be unset?
			safe_unsetmakeenv "${OPTARG}"
			;;

		--)
			break
			;;

		-h)
			help
			exit 0
			;;

		'-?')
			if [ "${OPTARG}" = '?' ]
			then
				help
				exit 0
			fi
			usage "Unknown option -${OPTARG}"
			;;

		-:)
			usage "Missing argument for option -${OPTARG}"
			;;

		*)
			usage "Unimplemented option ${opt}"
			;;

		esac
	done

	# Validate operations.
	#
	eval ${optremcmd}
	while [ $# -gt 0 ]
	do
		op=$1; shift
		operations="${operations} ${op}"

		case "${op}" in

		help)
			help
			exit 0
			;;

		list-arch)
			listarch "${MACHINE}" "${MACHINE_ARCH}"
			exit
			;;
		mkrepro-timestamp)
			setup_mkrepro quiet
			echo "${MKREPRO_TIMESTAMP:-0}"
			did_show_info=true
			;;

		show-revisionid)
			setup_mkrepro quiet
			echo "${NETBSD_REVISIONID}"
			did_show_info=true
			;;

		kernel=*|releasekernel=*|kernel.gdb=*)
			arg=${op#*=}
			op=${op%%=*}
			[ -n "${arg}" ] ||
			    bomb "Must supply a kernel name with '${op}=...'"
			;;

		disk-image=*)
			arg=${op#*=}
			op=disk_image
			[ -n "${arg}" ] ||
			    bomb "Must supply a target name with '${op}=...'"

			;;

		pkg=*)
			arg=${op#*=}
			op=${op%%=*}
			[ -n "${arg}" ] ||
			    bomb "Must supply category/package with 'pkg=...'"
			;;

		install=*|installmodules=*)
			arg=${op#*=}
			op=${op%%=*}
			[ -n "${arg}" ] ||
			    bomb "Must supply a directory with 'install=...'"
			;;

		distsets)
			operations="$(echo "$operations" | sed 's/distsets/distribution sets/')"
			do_sets=true
			op=distribution
			;;

		build|\
		cleandir|\
		distribution|\
		dtb|\
		install-image|\
		iso-image-source|\
		iso-image|\
		kernels|\
		libs|\
		live-image|\
		makewrapper|\
		modules|\
		obj|\
		params|\
		release|\
		rump|\
		rumptest|\
		sets|\
		show-params|\
		sourcesets|\
		syspkgs|\
		tools)
			;;

		*)
			usage "Unknown OPERATION '${op}'"
			;;

		esac
		# ${op} may contain chars that are not allowed in variable
		# names.  Replace them with '_' before setting do_${op}.
		op="$( echo "$op" | tr -s '.-' '__')"
		eval do_${op}=true
	done

	"$did_show_info" && [ "${MKREPRO_TIMESTAMP:-0}" -ne 0 ] && exit

	[ -n "${operations}" ] || usage "Missing OPERATION to perform"

	# Set up MACHINE*.  On a NetBSD host, these are allowed to be unset.
	#
	if [ -z "${MACHINE}" ]
	then
		[ "${uname_s}" = NetBSD ] || {
		    bomb "MACHINE must be set, or -m must be used," \
		         "for cross builds"
		}
		MACHINE=${uname_m}
		MACHINE_ARCH=${uname_p}
	fi
	if $opt_m && ! $opt_a
	then
		# Settings implied by the command line -m option
		# override MACHINE_ARCH from the environment (if any).
		getarch
	fi
	[ -n "${MACHINE_ARCH}" ] || getarch
	validatearch

	# Set up default make(1) environment.
	#
	makeenv="${makeenv} TOOLDIR MACHINE MACHINE_ARCH MAKEFLAGS"
	[ -z "${BUILDID}" ] || makeenv="${makeenv} BUILDID"
	[ -z "${BUILDINFO}" ] || makeenv="${makeenv} BUILDINFO"
	MAKEFLAGS="-de -m ${TOP}/share/mk ${MAKEFLAGS}"
	MAKEFLAGS="${MAKEFLAGS} MKOBJDIRS=${MKOBJDIRS-yes}"
	export MAKEFLAGS MACHINE MACHINE_ARCH
	if [ -z "${USETOOLS}" ]; then
		setmakeenv USETOOLS yes
	else
		setmakeenv USETOOLS "${USETOOLS}"
	fi
	setmakeenv MAKEWRAPPERMACHINE "${makewrappermachine:-${MACHINE}}"
	setmakeenv MAKE_OBJDIR_CHECK_WRITABLE no
}

# sanitycheck --
# Sanity check after parsing command line options, before rebuildmake.
#
sanitycheck()
{
	# Install as non-root is a bad idea.
	#
	if ${do_install} && [ "$id_u" -ne 0 ]
	then
		if ${do_expertmode}
		then
		    warning "Will install as an unprivileged user"
		else
		    bomb "-E must be set for install as an unprivileged user"
		fi
	fi

	# If the PATH contains any non-absolute components (including,
	# but not limited to, "." or ""), then complain.  As an exception,
	# allow "" or "." as the last component of the PATH.  This is fatal
	# if expert mode is not in effect.
	#
	local path="${PATH}"
	path="${path%:}"	# delete trailing ":"
	path="${path%:.}"	# delete trailing ":."
	case ":${path}:/" in
	*:[!/~]*)
		if ${do_expertmode}
		then
			warning "PATH contains non-absolute components"
		else
			bomb "PATH environment variable must not" \
			     "contain non-absolute components"
		fi
		;;
	esac

	while [ "${MKX11-no}" = yes ]		# not really a loop
	do
		test -n "${X11SRCDIR}" && {
		    test -d "${X11SRCDIR}/external" ||
			bomb "X11SRCDIR (${X11SRCDIR}) does not exist (with -x)"
		    break
		}
		for _xd in \
		    "${NETBSDSRCDIR%/*}/xsrc" \
		    "${NETBSDSRCDIR}/xsrc" \
		    /usr/xsrc
		do
		    test -f "${_xd}/Makefile" &&
			setmakeenv X11SRCDIR "${_xd}" &&
			    break 2
		done
		bomb "Asked to build X11 but no xsrc"
	done

	while $do_pkg				# not really a loop
	do
		test -n "${PKGSRCDIR}" && {
		    test -f "${PKGSRCDIR}/mk/bsd.pkg.mk" ||
			bomb "PKGSRCDIR (${PKGSRCDIR}) does not exist"
		    break
		}
		for _pd in \
		    "${NETBSDSRCDIR%/*}/pkgsrc" \
		    "${NETBSDSRCDIR}/pkgsrc" \
		    /usr/pkgsrc
		do
		    test -f "${_pd}/mk/bsd.pkg.mk" &&
			setmakeenv PKGSRCDIR "${_pd}" &&
			    break 2
		done
		bomb "Asked to build package but no pkgsrc"
	done
	if $do_pkg && [ "${MKX11-no}" = yes ]
	then
		# See comment below about X11_TYPE in pkgsrc mk.conf.
		# (Feel free to remove this, and set X11_TYPE to
		# native/modular according to MKX11=yes/no, if you want
		# to do the work to make X11_TYPE=native cross-builds
		# work.)
		bomb "Experimental \`build.sh pkg=...'" \
		     "does not support -x/MKX11=yes"
	fi
}

# find a command in PATH and print the full filename
print_path_of()
{
	set -- $( type "$1" )
	printf "${3}\n"
}


# print_tooldir_program --
# Try to find and show a path to an existing
# ${TOOLDIR}/bin/${toolprefix}program
#
print_tooldir_program()
{
	local possible_TOP_OBJ
	local possible_TOOLDIR
	local possible_program
	local tooldir_program
	local program="${1}"

	if [ "${USETOOLS-yes}" != "yes" ]; then
		print_path_of "${program}"
		return
	fi

	if [ -n "${TOOLDIR}" ]
	then
		echo "${TOOLDIR}/bin/${toolprefix}${program}"
		return
	fi

	# Set host_ostype to something like "NetBSD-4.5.6-i386".  This
	# is intended to match the HOST_OSTYPE variable in <bsd.own.mk>.
	#
	local host_ostype="${uname_s}-$(
			echo "${uname_r}" | sed -e 's/([^)]*)//g' -e 's/ /_/g'
		)-$(
			echo "${uname_p}" | sed -e 's/([^)]*)//g' -e 's/ /_/g'
		)"

	# Look in a few potential locations for
	# ${possible_TOOLDIR}/bin/${toolprefix}${program}.
	# If we find it, then set possible_program.
	#
	# In the usual case (without interference from environment
	# variables or /etc/mk.conf), <bsd.own.mk> should set TOOLDIR to
	# "${_SRC_TOP_OBJ_}/tooldir.${host_ostype}".
	#
	# In practice it's difficult to figure out the correct value
	# for _SRC_TOP_OBJ_.  In the easiest case, when the -M or -O
	# options were passed to build.sh, then ${TOP_objdir} will be
	# the correct value.  We also try a few other possibilities, but
	# we do not replicate all the logic of <bsd.obj.mk>.
	#
	for possible_TOP_OBJ in \
		"${TOP_objdir}" \
		"${MAKEOBJDIRPREFIX:+${MAKEOBJDIRPREFIX}${TOP}}" \
		"${TOP}" \
		"${TOP}/obj" \
		"${TOP}/obj.${MACHINE}"
	do
	    [ -n "${possible_TOP_OBJ}" ] || continue
	    possible_TOOLDIR="${possible_TOP_OBJ}/tooldir.${host_ostype}"
	    possible_program="${possible_TOOLDIR}/bin/${toolprefix}${program}"
	    if [ -x "${possible_program}" ]
	    then
		echo ${possible_program}
		return
	    fi
	done
	echo ''
}

# print_tooldir_make --
# Try to find and show a path to an existing
# ${TOOLDIR}/bin/${toolprefix}make, for use by rebuildmake() before a
# new version of ${toolprefix}make has been built.
#
# * If TOOLDIR was set in the environment or on the command line, use
#   that value.
# * Otherwise try to guess what TOOLDIR would be if not overridden by
#   /etc/mk.conf, and check whether the resulting directory contains
#   a copy of ${toolprefix}make (this should work for everybody who
#   doesn't override TOOLDIR via /etc/mk.conf);
# * Failing that, search for ${toolprefix}make, nbmake, bmake, or make,
#   in the PATH (this might accidentally find a version of make that
#   does not understand the syntax used by NetBSD make, and that will
#   lead to failure in the next step);
# * If a copy of make was found above, try to use it with
#   nobomb_getmakevar to find the correct value for TOOLDIR, and believe the
#   result only if it's a directory that already exists;
# * If a value of TOOLDIR was found above, and if
#   ${TOOLDIR}/bin/${toolprefix}make exists, show that value.
#
print_tooldir_make()
{
	local possible_make
	local possible_TOOLDIR
	local tooldir_make

	possible_make=$(print_tooldir_program make)
	# If the above didn't work, search the PATH for a suitable
	# ${toolprefix}make, nbmake, bmake, or make.
	#
	: ${possible_make:=$(find_in_PATH ${toolprefix}make '')}
	: ${possible_make:=$(find_in_PATH nbmake '')}
	: ${possible_make:=$(find_in_PATH bmake '')}
	: ${possible_make:=$(find_in_PATH make '')}

	# At this point, we don't care whether possible_make is in the
	# correct TOOLDIR or not; we simply want it to be usable by
	# getmakevar to help us find the correct TOOLDIR.
	#
	# Use ${possible_make} with nobomb_getmakevar to try to find
	# the value of TOOLDIR.  Believe the result only if it's
	# a directory that already exists and contains bin/${toolprefix}make.
	#
	if [ -x "${possible_make}" ]
	then
		possible_TOOLDIR=$(
			make="${possible_make}" \
			    nobomb_getmakevar TOOLDIR 2>/dev/null
			)
		if [ $? = 0 ] &&
		   [ -n "${possible_TOOLDIR}" ] &&
		   [ -d "${possible_TOOLDIR}" ]
		then
			tooldir_make="${possible_TOOLDIR}/bin/${toolprefix}make"
			if [ -x "${tooldir_make}" ]
			then
				echo "${tooldir_make}"
				return 0
			fi
		fi
	fi
	return 1
}

# rebuildmake --
# Rebuild nbmake in a temporary directory if necessary.  Sets $make
# to a path to the nbmake executable.  Sets done_rebuildmake=true
# if nbmake was rebuilt.
#
# There is a cyclic dependency between building nbmake and choosing
# TOOLDIR: TOOLDIR may be affected by settings in /etc/mk.conf, so we
# would like to use getmakevar to get the value of TOOLDIR; but we can't
# use getmakevar before we have an up to date version of nbmake; we
# might already have an up to date version of nbmake in TOOLDIR, but we
# don't yet know where TOOLDIR is.
#
# The default value of TOOLDIR also depends on the location of the top
# level object directory, so $(getmakevar TOOLDIR) invoked before or
# after making the top level object directory may produce different
# results.
#
# Strictly speaking, we should do the following:
#
#    1. build a new version of nbmake in a temporary directory;
#    2. use the temporary nbmake to create the top level obj directory;
#    3. use $(getmakevar TOOLDIR) with the temporary nbmake to
#       get the correct value of TOOLDIR;
#    4. move the temporary nbmake to ${TOOLDIR}/bin/nbmake.
#
# However, people don't like building nbmake unnecessarily if their
# TOOLDIR has not changed since an earlier build.  We try to avoid
# rebuilding a temporary version of nbmake by taking some shortcuts to
# guess a value for TOOLDIR, looking for an existing version of nbmake
# in that TOOLDIR, and checking whether that nbmake is newer than the
# sources used to build it.
#
rebuildmake()
{
	make="$(print_tooldir_make)"
	if [ -n "${make}" ] && [ -x "${make}" ]
	then
		for f in usr.bin/make/*.[ch]
		do
			if [ "${f}" -nt "${make}" ]
			then
				statusmsg "${make} outdated" \
					"(older than ${f}), needs building."
				do_rebuildmake=true
				break
			fi
		done
	else
		statusmsg "No \$TOOLDIR/bin/${toolprefix}make, needs building."
		do_rebuildmake=true
	fi

	# Build bootstrap ${toolprefix}make if needed.
	if ! ${do_rebuildmake}
	then
		return
	fi

	# Silent configure with MAKEVERBOSE==0
	if [ ${MAKEVERBOSE:-2} -eq 0 ]
	then
		configure_args=--silent
	fi

	statusmsg "Bootstrapping ${toolprefix}make"
	${runcmd} cd "${tmpdir}"
	${runcmd} env CC="${HOST_CC-cc}" CPPFLAGS="${HOST_CPPFLAGS}" \
		CFLAGS="${HOST_CFLAGS--O}" LDFLAGS="${HOST_LDFLAGS}" \
	    ${HOST_SH} "${TOP}/tools/make/configure" ${configure_args} ||
	( cp ${tmpdir}/config.log ${tmpdir}-config.log
	      bomb "Configure of ${toolprefix}make failed," \
		   "see ${tmpdir}-config.log for details" )
	${runcmd} ${HOST_SH} buildmake.sh ||
	    bomb "Build of ${toolprefix}make failed"
	make="${tmpdir}/${toolprefix}make"
	${runcmd} cd "${TOP}"
	${runcmd} rm -f usr.bin/make/*.o
	done_rebuildmake=true
}

# validatemakeparams --
# Perform some late sanity checks, after rebuildmake,
# but before createmakewrapper or any real work.
#
# Creates the top-level obj directory, because that
# is needed by some of the sanity checks.
#
# Shows status messages reporting the values of several variables.
#
validatemakeparams()
{
	# Determine MAKECONF first, and set in the makewrapper.
	# If set in the environment, then use that.
	# else if ./mk.conf exists, then set MAKECONF to that,
	# else use the default from share/mk/bsd.own.mk (/etc/mk.conf).
	#
	if [ -n "${MAKECONF+1}" ]
	then
		setmakeenv MAKECONF "${MAKECONF}"
		statusmsg2 "getenv MAKECONF:" "${MAKECONF}"
	elif [ -f "${TOP}/mk.conf" ]
	then
		setmakeenv MAKECONF "${TOP}/mk.conf"
		statusmsg2 "mk.conf MAKECONF:" "${MAKECONF}"
	else
		MAKECONF=$(getmakevar MAKECONF)
		setmakeenv MAKECONF "${MAKECONF}"
		statusmsg2 "share/mk MAKECONF:" "${MAKECONF}"
	fi
	if [ -z "${MAKECONF}" ]
	then
		bomb "MAKECONF must not be empty"
	elif [ -e "${MAKECONF}" ]
	then
		statusmsg2 "MAKECONF file:" "${MAKECONF}"
	else
		statusmsg2 "MAKECONF file:" "${MAKECONF} (File not found)"
	fi

	# Normalise MKOBJDIRS, MKUNPRIVED, and MKUPDATE.
	# These may be set as build.sh options or in "mk.conf".
	# Don't export them as they're only used for tests in build.sh.
	#
	MKOBJDIRS=$(getmakevar MKOBJDIRS)
	MKUNPRIVED=$(getmakevar MKUNPRIVED)
	MKUPDATE=$(getmakevar MKUPDATE)

	# Non-root should always use either the -U or -E flag.
	#
	if ! ${do_expertmode} && [ "$id_u" -ne 0 ] && [ "${MKUNPRIVED}" = no ]
	then
		bomb "-U or -E must be set for build as an unprivileged user"
	fi

	if [ "${runcmd}" = echo ]
	then
		TOOLCHAIN_MISSING=no
		EXTERNAL_TOOLCHAIN=
	else
		TOOLCHAIN_MISSING=$(bomb_getmakevar TOOLCHAIN_MISSING)
		EXTERNAL_TOOLCHAIN=$(bomb_getmakevar EXTERNAL_TOOLCHAIN)
	fi
	if [ "${TOOLCHAIN_MISSING}" = yes ] && [ -z "${EXTERNAL_TOOLCHAIN}" ]
	then
		${runcmd} echo "ERROR: build.sh (in-tree cross-toolchain)" \
						"is not yet available for"
		${runcmd} echo "	MACHINE:      ${MACHINE}"
		${runcmd} echo "	MACHINE_ARCH: ${MACHINE_ARCH}"
		${runcmd} echo ""
		${runcmd} echo "All builds for this platform should be done" \
						"via a traditional make"
		${runcmd} echo "If you wish to use an external" \
						"cross-toolchain, set"
		${runcmd} echo "	EXTERNAL_TOOLCHAIN=<path to" \
						"toolchain root>"
		${runcmd} echo "in either the environment or mk.conf and rerun"
		${runcmd} echo "	${progname} $*"
		exit 1
	fi

	if [ "${MKOBJDIRS}" != no ]
	then
		# Create the top-level object directory.
		#
		# "make obj NOSUBDIR=" can handle most cases, but it
		# can't handle the case where MAKEOBJDIRPREFIX is set
		# while the corresponding directory does not exist
		# (rules in <bsd.obj.mk> would abort the build).  We
		# therefore have to handle the MAKEOBJDIRPREFIX case
		# without invoking "make obj".  The MAKEOBJDIR case
		# could be handled either way, but we choose to handle
		# it similarly to MAKEOBJDIRPREFIX.
		#
		if [ -n "${TOP_obj}" ]
		then
			# It must have been set by the "-M" or "-O"
			# command line options, so there's no need to
			# use getmakevar
			:
		elif [ -n "$MAKEOBJDIRPREFIX" ]
		then
			TOP_obj="$(getmakevar MAKEOBJDIRPREFIX)${TOP}"
		elif [ -n "$MAKEOBJDIR" ]
		then
			TOP_obj="$(getmakevar MAKEOBJDIR)"
		fi
		if [ -n "$TOP_obj" ]
		then
		    ${runcmd} mkdir -p "${TOP_obj}" ||
			bomb "Can't create top level object directory" \
			     "${TOP_obj}"
		else
		    ${runcmd} "${make}" -m "${TOP}/share/mk" obj NOSUBDIR= ||
			    bomb "Can't create top level object directory" \
				 "using make obj"
		fi

		# make obj in tools to ensure that the objdir for "tools"
		# is available.
		#
		${runcmd} cd tools
		${runcmd} "${make}" -m "${TOP}/share/mk" obj NOSUBDIR= ||
		    bomb "Failed to make obj in tools"
		${runcmd} cd "${TOP}"
	fi

	# Find TOOLDIR, DESTDIR, and RELEASEDIR, according to getmakevar,
	# and bomb if they have changed from the values we had from the
	# command line or environment.
	#
	# This must be done after creating the top-level object directory.
	#
	for var in TOOLDIR DESTDIR RELEASEDIR
	do
		eval oldval=\"\$${var}\"
		newval="$(getmakevar $var)"
		if ! $do_expertmode
		then
			: ${_SRC_TOP_OBJ_:=$(getmakevar _SRC_TOP_OBJ_)}
			case "$var" in
			DESTDIR)
				: ${newval:=${_SRC_TOP_OBJ_}/destdir.${MACHINE}}
				makeenv="${makeenv} DESTDIR"
				;;
			RELEASEDIR)
				: ${newval:=${_SRC_TOP_OBJ_}/releasedir}
				makeenv="${makeenv} RELEASEDIR"
				;;
			esac
		fi
		if [ -n "$oldval" ] && [ "$oldval" != "$newval" ]
		then
			bomb "Value of ${var} has changed" \
			     "(was \"${oldval}\", now \"${newval}\")"
		fi
		eval ${var}=\"\${newval}\"
		eval export ${var}
		statusmsg2 "${var} path:" "${newval}"
	done

	# RELEASEMACHINEDIR is just a subdir name, e.g. "i386".
	RELEASEMACHINEDIR=$(getmakevar RELEASEMACHINEDIR)

	# Check validity of TOOLDIR and DESTDIR.
	#
	if [ -z "${TOOLDIR}" ] || [ "${TOOLDIR}" = / ]
	then
		bomb "TOOLDIR '${TOOLDIR}' invalid"
	fi
	removedirs="${TOOLDIR}"

	if [ -z "${DESTDIR}" ] || [ "${DESTDIR}" = / ]
	then
		if ${do_distribution} || ${do_release} ||
		   [ "${uname_s}" != NetBSD ] ||
		   [ "${uname_m}" != "${MACHINE}" ]
		then
			bomb "DESTDIR must != / for cross builds," \
			     "or ${progname} 'distribution' or 'release'"
		fi
		if ! ${do_expertmode}
		then
			bomb "DESTDIR must != / for non -E (expert) builds"
		fi
		statusmsg "WARNING: Building to /, in expert mode."
		statusmsg "         This may cause your system to break!"
		statusmsg "         Reasons include:"
		statusmsg "           - your kernel is not up to date"
		statusmsg "           - the libraries or toolchain have changed"
		statusmsg "         YOU HAVE BEEN WARNED!"
	else
		removedirs="${removedirs} ${DESTDIR}"
	fi
	if ${do_releasekernel} && [ -z "${RELEASEDIR}" ]
	then
		bomb "Must set RELEASEDIR with 'releasekernel=...'"
	fi

	# If a previous build.sh run used -U (and therefore created a
	# METALOG file), then most subsequent build.sh runs must also
	# use -U.  If DESTDIR is about to be removed, then don't perform
	# this check.
	#
	case "${do_removedirs} ${removedirs} " in
	true*" ${DESTDIR} "*)
		# DESTDIR is about to be removed
		;;
	*)
		if [ -e "${DESTDIR}/METALOG" ] &&
		   [ "${MKUNPRIVED}" = no ]
		then
			if $do_expertmode
			then
				warning "A previous build.sh run specified -U"
			else
				bomb "A previous build.sh run specified -U;" \
				     "you must specify it again now"
			fi
		fi
		;;
	esac

	# live-image and install-image targets require binary sets
	# (actually DESTDIR/etc/mtree/set.* files) built with MKUNPRIVED.
	# If release operation is specified with live-image or install-image,
	# the release op should be performed with -U for later image ops.
	#
	if ${do_release} &&
	    { ${do_live_image} || ${do_install_image} ; } &&
	    [ "${MKUNPRIVED}" = no ]
	then
		bomb "-U must be specified on building release" \
		     "to create images later"
	fi
}


createmakewrapper()
{
	# Remove the target directories.
	#
	if ${do_removedirs}
	then
		for f in ${removedirs}
		do
			statusmsg "Removing ${f}"
			${runcmd} rm -r -f "${f}"
		done
	fi

	# Recreate $TOOLDIR.
	#
	${runcmd} mkdir -p "${TOOLDIR}/bin" ||
	    bomb "mkdir of '${TOOLDIR}/bin' failed"

	# If we did not previously rebuild ${toolprefix}make, then
	# check whether $make is still valid and the same as the output
	# from print_tooldir_make.  If not, then rebuild make now.  A
	# possible reason for this being necessary is that the actual
	# value of TOOLDIR might be different from the value guessed
	# before the top level obj dir was created.
	#
	if ! ${done_rebuildmake} &&
	   { ! [ -x "$make" ] || [ "$make" != "$(print_tooldir_make)" ] ; }
	then
		rebuildmake
	fi

	# Install ${toolprefix}make if it was built.
	#
	if ${done_rebuildmake}
	then
		${runcmd} rm -f "${TOOLDIR}/bin/${toolprefix}make"
		${runcmd} cp "${make}" "${TOOLDIR}/bin/${toolprefix}make" ||
		    bomb "Failed to install \$TOOLDIR/bin/${toolprefix}make"
		make="${TOOLDIR}/bin/${toolprefix}make"
		statusmsg "Created ${make}"
	fi

	# Build a ${toolprefix}make wrapper script, usable by hand as
	# well as by build.sh.
	#
	if [ -z "${makewrapper}" ]
	then
		makewrapper="${TOOLDIR}/bin/${toolprefix}make"
		makewrapper="${makewrapper}-${makewrappermachine:-${MACHINE}}"
		[ -z "${BUILDID}" ] || makewrapper="${makewrapper}-${BUILDID}"
	fi

	${runcmd} rm -f "${makewrapper}"
	if [ "${runcmd}" = echo ]
	then
		echo 'cat <<EOF >'${makewrapper}
		makewrapout=
	else
		makewrapout=">>\${makewrapper}"
	fi

	case "${KSH_VERSION:-${SH_VERSION}}" in
	*PD\ KSH*|*MIRBSD\ KSH*)
		set +o braceexpand
		;;
	esac

	eval cat <<EOF ${makewrapout}
#! ${HOST_SH}
# Set proper variables to allow easy "make" building of a NetBSD subtree.
# Generated from:  \$NetBSD: build.sh,v 1.399 2025/07/08 12:29:40 hgutch Exp $
# with these arguments: ${_args}
#

EOF
	{
		sorted_vars=$(
			for var in ${makeenv}
			do
				echo "${var}"
			done |
				sort -u
		)
		for var in ${sorted_vars}
		do
			eval val=\"\${${var}}\"
			eval is_set=\"\${${var}+set}\"
			if [ -z "${is_set}" ]
			then
				echo "unset ${var}"
			else
				qval="$(shell_quote "${val}")"
				echo "${var}=${qval}; export ${var}"
			fi
		done

		cat <<-EOF

			exec "\${TOOLDIR}/bin/${toolprefix}make" \${1+"\$@"}
		EOF
	} | eval cat "${makewrapout}"
	[ "${runcmd}" = echo ] && echo EOF
	${runcmd} chmod +x "${makewrapper}"
	statusmsg2 "Updated makewrapper:" "${makewrapper}"
}

make_in_dir()
{
	local dir="$1"
	local op="$2"
	${runcmd} cd "${dir}" ||
	    bomb "Failed to cd to \"${dir}\""
	${runcmd} "${makewrapper}" ${parallel} ${op} ||
	    bomb "Failed to make ${op} in \"${dir}\""
	${runcmd} cd "${TOP}" ||
	    bomb "Failed to cd back to \"${TOP}\""
}

buildtools()
{
	if [ "${MKOBJDIRS}" != no ]
	then
		${runcmd} "${makewrapper}" ${parallel} obj-tools ||
		    bomb "Failed to make obj-tools"
	fi
	if [ "${MKUPDATE}" = no ]
	then
		make_in_dir tools cleandir
	fi
	make_in_dir tools build_install
	statusmsg "Tools built to ${TOOLDIR}"
}

buildlibs()
{
	if [ "${MKOBJDIRS}" != no ]
	then
		${runcmd} "${makewrapper}" ${parallel} obj ||
		    bomb "Failed to make obj"
	fi
	if [ "${MKUPDATE}" = no ]
	then
		make_in_dir lib cleandir
	fi
	make_in_dir . do-distrib-dirs
	make_in_dir . includes
	make_in_dir . do-lib
	statusmsg "libs built"
}

getkernelconf()
{
	kernelconf=$1
	if [ "${MKOBJDIRS}" != no ]
	then
		# The correct value of KERNOBJDIR might
		# depend on a prior "make obj" in
		# ${KERNSRCDIR}/${KERNARCHDIR}/compile.
		#
		KERNSRCDIR="$(getmakevar KERNSRCDIR)"
		KERNARCHDIR="$(getmakevar KERNARCHDIR)"
		make_in_dir "${KERNSRCDIR}/${KERNARCHDIR}/compile" obj
	fi
	KERNCONFDIR=$(getmakevar KERNCONFDIR)
	KERNOBJDIR=$(getmakevar KERNOBJDIR)
	case "${kernelconf}" in
	*/*)
		kernelconfpath=${kernelconf}
		kernelconfname=${kernelconf##*/}
		;;
	*)
		kernelconfpath=${KERNCONFDIR}/${kernelconf}
		kernelconfname=${kernelconf}
		;;
	esac
	kernelbuildpath=${KERNOBJDIR}/${kernelconfname}
}

diskimage()
{
	ARG="$(echo "$1" | tr '[:lower:]' '[:upper:]')"
	[ -f "${DESTDIR}/etc/mtree/set.base" ] ||
	    bomb "The release binaries must be built first"
	kerneldir="${RELEASEDIR}/${RELEASEMACHINEDIR}/binary/kernel"
	kernel="${kerneldir}/netbsd-${ARG}.gz"
	[ -f "${kernel}" ] ||
	    bomb "The kernel ${kernel} must be built first"
	make_in_dir "${NETBSDSRCDIR}/etc" "smp_${1}"
}

buildkernel()
{
	if ! ${do_tools} && ! ${buildkernelwarned:-false}
	then
		# Building tools every time we build a kernel is clearly
		# unnecessary.  We could try to figure out whether rebuilding
		# the tools is necessary this time, but it doesn't seem worth
		# the trouble.  Instead, we say it's the user's responsibility
		# to rebuild the tools if necessary.
		#
		statusmsg "Building kernel without building new tools"
		buildkernelwarned=true
	fi
	getkernelconf $1
	statusmsg2 "Building kernel:" "${kernelconf}"
	statusmsg2 "Build directory:" "${kernelbuildpath}"
	${runcmd} mkdir -p "${kernelbuildpath}" ||
	    bomb "Cannot mkdir: ${kernelbuildpath}"
	if [ "${MKUPDATE}" = no ]
	then
		make_in_dir "${kernelbuildpath}" cleandir
	fi
	[ -x "${TOOLDIR}/bin/${toolprefix}config" ] ||
		bomb "${TOOLDIR}/bin/${toolprefix}config does not exist." \
		     "You need to \"$0 tools\" first"
	CONFIGOPTS=$(getmakevar CONFIGOPTS)
	${runcmd} "${TOOLDIR}/bin/${toolprefix}config" ${CONFIGOPTS} \
		-b "${kernelbuildpath}" -s "${TOP}/sys" ${configopts} \
		"${kernelconfpath}" ||
	    bomb "${toolprefix}config failed for ${kernelconf}"
	make_in_dir "${kernelbuildpath}" depend
	make_in_dir "${kernelbuildpath}" all

	if [ "${runcmd}" != echo ]
	then
		statusmsg "Kernels built from ${kernelconf}:"
		kernlist=$(awk '$1 == "config" { print $2 }' ${kernelconfpath})
		for kern in ${kernlist:-netbsd}
		do
			[ -f "${kernelbuildpath}/${kern}" ] &&
			    echo "  ${kernelbuildpath}/${kern}"
		done | tee -a "${results}"
	fi
}

releasekernel()
{
	getkernelconf $1
	kernelreldir="${RELEASEDIR}/${RELEASEMACHINEDIR}/binary/kernel"
	${runcmd} mkdir -p "${kernelreldir}"
	kernlist=$(awk '$1 == "config" { print $2 }' ${kernelconfpath})
	for kern in ${kernlist:-netbsd}
	do
		builtkern="${kernelbuildpath}/${kern}"
		[ -f "${builtkern}" ] || continue
		releasekern="${kernelreldir}/${kern}-${kernelconfname}.gz"
		statusmsg2 "Kernel copy:" "${releasekern}"
		if [ "${runcmd}" = echo ]
		then
			echo "gzip -c -9 < ${builtkern} > ${releasekern}"
		else
			gzip -c -9 < "${builtkern}" > "${releasekern}"
		fi
	done
}

buildkernels()
{
	allkernels=$( runcmd= make_in_dir etc '-V ${ALL_KERNELS}' )
	for k in $allkernels
	do
		buildkernel "${k}"
	done
}

buildmodules()
{
	setmakeenv MKBINUTILS no
	if ! ${do_tools} && ! ${buildmoduleswarned:-false}
	then
		# Building tools every time we build modules is clearly
		# unnecessary as well as a kernel.
		#
		statusmsg "Building modules without building new tools"
		buildmoduleswarned=true
	fi

	statusmsg "Building kernel modules for NetBSD/${MACHINE} ${DISTRIBVER}"
	if [ "${MKOBJDIRS}" != no ]
	then
		make_in_dir sys/modules obj
	fi
	if [ "${MKUPDATE}" = no ]
	then
		make_in_dir sys/modules cleandir
	fi
	make_in_dir sys/modules dependall
	make_in_dir sys/modules install

	statusmsg "Successful build of kernel modules for" \
		  "NetBSD/${MACHINE} ${DISTRIBVER}"
}

builddtb()
{
	statusmsg "Building devicetree blobs for" \
		  "NetBSD/${MACHINE} ${DISTRIBVER}"
	if [ "${MKOBJDIRS}" != no ]
	then
		make_in_dir sys/dtb obj
	fi
	if [ "${MKUPDATE}" = no ]
	then
		make_in_dir sys/dtb cleandir
	fi
	make_in_dir sys/dtb dependall
	make_in_dir sys/dtb install

	statusmsg "Successful build of devicetree blobs for" \
		  "NetBSD/${MACHINE} ${DISTRIBVER}"
}

buildpkg()
{
	local catpkg
	local pkgroot
	local makejobsarg
	local makejobsvar
	local quiet
	local opsys_version

	catpkg="$1"

	pkgroot="${TOP_objdir:-${TOP}}/pkgroot"
	${runcmd} mkdir -p "${pkgroot}" ||
	    bomb "Can't create package root" "${pkgroot}"

	# Get a symlink-free absolute path to pkg -- pkgsrc wants this.
	#
	# XXX See TOP= above regarding pwd -P.
	pkgroot=$(unset PWD; cd "${pkgroot}" &&
		((exec pwd -P 2>/dev/null) || (exec pwd 2>/dev/null)))

	case $parallel in
	"-j "*)
		makejobsarg="--make-jobs ${parallel#-j }"
		makejobsvar="MAKE_JOBS=${parallel#-j }"
		;;
	*)	makejobsarg=
		makejobsvar=
		;;
	esac

	if [ "${MAKEVERBOSE}" -eq 0 ]
	then
		quiet="--quiet"
	else
		quiet=
	fi

	# Derived from pkgsrc/mk/bsd.prefs.mk rev. 1.451.
	opsys_version=$(echo "${DISTRIBVER}" |
		awk -F. '{
				major=int($1)
				minor=int($2)
				if (minor>=100) minor=99
				patch=int($3)
				if (patch>=100) patch=99
				printf "%02d%02d%02d", major, minor, patch
			}'
	)

	# Bootstrap pkgsrc if needed.
	#
	# XXX Redo this if it's out-of-date, not just if it's missing.
	if ! [ -x "${pkgroot}/pkg/bin/bmake" ]
	then
		statusmsg "Bootstrapping pkgsrc"

		cat >"${pkgroot}/mk.conf-fragment" <<EOF
USE_CROSS_COMPILE?=	no
TOOLDIR=		${TOOLDIR}
CROSS_DESTDIR=		${DESTDIR}
CROSS_MACHINE_ARCH=	${MACHINE_ARCH}
CROSS_OPSYS=		NetBSD
CROSS_OS_VERSION=	${DISTRIBVER}
CROSS_OPSYS_VERSION=	${opsys_version}
CROSS_LOWER_OPSYS=	netbsd
CROSS_LOWER_OPSYS_VERSUFFIX=	# empty
CROSS_LOWER_OS_VARIANT=		# empty
CROSS_LOWER_VARIANT_VERSION=	# empty
CROSS_LOWER_VENDOR=		# empty
CROSS_OBJECT_FMT=	ELF

ALLOW_VULNERABLE_PACKAGES=	yes
BINPKG_SITES=			# empty
FAILOVER_FETCH=			yes
FETCH_TIMEOUT=			1800
PASSIVE_FETCH=			yes

DISTDIR=		${pkgroot}/distfiles
PACKAGES=		${pkgroot}/packages
WRKOBJDIR=		${pkgroot}/work

# pkgsrc cross-builds are not set up to support native X, but also part
# of the point of pkgsrc cross-build infrastructure is to not need
# native X any more.
#
# (If you fix this, remove the bomb in build.sh pkg=... on MKX11=yes.)
X11_TYPE=		modular

.-include "${MAKECONF}"

MKDEBUG=		no	# interferes with pkgsrc builds
EOF

		# XXX Set --abi for mips and whatever else needs it?
		# XXX Unprivileged native tools, privileged cross.
		(
			cd "${PKGSRCDIR}" &&
			clearmakeenv &&
			./bootstrap/bootstrap \
				${makejobsarg} \
				--mk-fragment "${pkgroot}/mk.conf-fragment" \
				--prefix "${pkgroot}/pkg" \
				${quiet} \
				--unprivileged \
				--workdir "${pkgroot}/bootwork"
		) ||
			 bomb "Failed to bootstrap pkgsrc"
	fi

	# Build the package.
	(
		cd "${PKGSRCDIR}/${catpkg}" &&
		clearmakeenv &&
		"${pkgroot}/pkg/bin/bmake" package \
			USE_CROSS_COMPILE=yes \
			${makejobsvar}
	) ||
		bomb "Failed to build ${catpkg}"
}

installmodules()
{
	dir="$1"
	${runcmd} "${makewrapper}" INSTALLMODULESDIR="${dir}" installmodules ||
	    bomb "Failed to make installmodules to ${dir}"
	statusmsg "Successful installmodules to ${dir}"
}

installworld()
{
	dir="$1"
	${runcmd} "${makewrapper}" INSTALLWORLDDIR="${dir}" installworld ||
	    bomb "Failed to make installworld to ${dir}"
	statusmsg "Successful installworld to ${dir}"
}

# Run rump build&link tests.
#
# To make this feasible for running without having to install includes and
# libraries into destdir (i.e. quick), we only run ld.  This is possible
# since the rump kernel is a closed namespace apart from calls to rumpuser.
# Therefore, if ld complains only about rumpuser symbols, rump kernel
# linking was successful.
#
# We test that rump links with a number of component configurations.
# These attempt to mimic what is encountered in the full build.
# See list below.  The list should probably be either autogenerated
# or managed elsewhere; keep it here until a better idea arises.
#
# Above all, note that THIS IS NOT A SUBSTITUTE FOR A FULL BUILD.
#

# XXX: uwe: kern/56599 - while riastradh addressed librump problems,
# there are still unwanted dependencies:
#    net -> net_net
#    vfs -> fifo

# -lrumpvfs -> $LRUMPVFS for now
LRUMPVFS="-lrumpvfs -lrumpvfs_nofifofs"

RUMP_LIBSETS="
	-lrump,
        -lrumpvfs
            --no-whole-archive -lrumpvfs_nofifofs -lrump,
	-lrumpkern_tty
            --no-whole-archive $LRUMPVFS -lrump,
	-lrumpfs_tmpfs
            --no-whole-archive $LRUMPVFS -lrump,
	-lrumpfs_ffs -lrumpfs_msdos
            --no-whole-archive $LRUMPVFS -lrumpdev_disk -lrumpdev -lrump,
	-lrumpnet_virtif -lrumpnet_netinet -lrumpnet_net -lrumpnet
	    --no-whole-archive -lrump,
	-lrumpfs_nfs
	    --no-whole-archive $LRUMPVFS
	    -lrumpnet_sockin -lrumpnet_virtif -lrumpnet_netinet
            --start-group -lrumpnet_net -lrumpnet --end-group -lrump,
	-lrumpdev_cgd -lrumpdev_raidframe -lrumpdev_rnd -lrumpdev_dm
            --no-whole-archive $LRUMPVFS -lrumpdev_disk -lrumpdev -lrumpkern_crypto -lrump
"

dorump()
{
	local doclean=
	local doobjs=

	export RUMPKERN_ONLY=1
	# create obj and distrib dirs
	if [ "${MKOBJDIRS}" != no ]
	then
		make_in_dir "${NETBSDSRCDIR}/etc/mtree" obj
		make_in_dir "${NETBSDSRCDIR}/sys/rump" obj
	fi
	${runcmd} "${makewrapper}" ${parallel} do-distrib-dirs \
	    || bomb "Could not create distrib-dirs"

	[ "${MKUPDATE}" = no ] && doclean="cleandir"
	targlist="${doclean} ${doobjs} dependall install"

	# optimize: for test we build only static libs (3x test speedup)
	if [ "${1}" = rumptest ]
	then
		setmakeenv NOPIC 1
		setmakeenv NOPROFILE 1
	fi

	for cmd in ${targlist}
	do
		make_in_dir "${NETBSDSRCDIR}/sys/rump" ${cmd}
	done

	# if we just wanted to build & install rump, we're done
	[ "${1}" != rumptest ] && return

	${runcmd} cd "${NETBSDSRCDIR}/sys/rump/librump/rumpkern" ||
		bomb "cd to rumpkern failed"

	md_quirks=`${runcmd} "${makewrapper}" -V '${_SYMQUIRK}'`
	# one little, two little, three little backslashes ...
	md_quirks="$(echo ${md_quirks} | sed 's,\\,\\\\,g'";s/'//g" )"
	${runcmd} cd "${TOP}" || bomb "cd to ${TOP} failed"
	tool_ld=`${runcmd} "${makewrapper}" -V '${LD}'`

	local oIFS="${IFS-UnSeTT}"
	IFS=","
	for set in ${RUMP_LIBSETS}
	do
		case "${oIFS}" in
		UnSeTT)	unset IFS;;
		*)	IFS="${oIFS}";;
		esac
		${runcmd} ${tool_ld} -nostdlib -L${DESTDIR}/usr/lib	\
		    -static --whole-archive ${set} --no-whole-archive   \
		    -lpthread -lc 2>&1 -o /tmp/rumptest.$$ |
		      awk -v quirks="${md_quirks}" '
			/undefined reference/ &&
			    !/more undefined references.*follow/{
				if (match($NF,
				    "`(rumpuser_|rumpcomp_|__" quirks ")") == 0)
					fails[NR] = $0
			}
			/cannot find -l/{fails[NR] = $0}
			/cannot open output file/{fails[NR] = $0}
			END{
				for (x in fails)
					print fails[x]
				exit x!=0
			}'
		[ $? -ne 0 ] && bomb "Testlink of rump failed: ${set}"
	done
	statusmsg "Rump build&link tests successful"
}

repro_date() {
	# try the bsd date fail back the linux one
	date -u -r "$1" 2> /dev/null || date -u -d "@$1"
}

setup_mkrepro()
{
	local quiet="$1"

	if [ "${MKREPRO-no}" != yes ]
	then
		return
	fi
	if [ "${MKREPRO_TIMESTAMP-0}" -ne 0 ]
	then
		return
	fi

	MKREPRO_TIMESTAMP=0
	NETBSD_REVISIONID=
	local base
	local d
	local t
	local rid
	local tag
	local vcs
	for base in src xsrc
	do
		case $base in
		src)	d=${NETBSDSRCDIR-/usr/src}/
			;;
		xsrc)	[ "${MKX11-no}" = yes ] || continue
			d=${X11SRCDIR-/usr/xsrc}/
			;;
		esac
		if [ -d "${d}CVS" ]
		then
			local cvslatest="$(print_tooldir_program cvslatest)"
			if ! [ -x "${cvslatest}" ]
			then
				buildtools
			fi
			local nbdate="$(print_tooldir_program date)"

			local cvslatestflags=
			if "${do_expertmode}"
			then
				cvslatestflags=-i
			fi

			if [ -f "${d}CVS/Tag" ]
			then
				tag=$(sed -e 's/^T//' <${d}CVS/Tag)
			else
				tag=HEAD
			fi
			t=$("${cvslatest}" ${cvslatestflags} "${d}") ||
				bomb "${cvslatest} failed"
			rid="${tag}:$(${nbdate} -u -r ${t} '+%Y%m%d%H%M%S')"
			vcs=cvs
		elif [ -d "${d}.git" ] || [ -f "${d}.git" ]
		then
			t=$(cd "${d}" && git log -1 --format=%ct) ||
				bomb "git log %ct failed"
			rid="$(
			   cd "${d}" && git log -1 --format=%H)" ||
				bomb "git log %H failed"
			vcs=git
		elif [ -d "${d}.hg" ]
		then
			t=$(hg --repo "$d" \
			    log -r . --template '{date.unixtime}\n') ||
				bomb "hg log failed"
			rid=$(hg --repo "$d" \
			    identify --template '{id}\n') ||
				bomb "hg identify failed"
			vcs=hg
		elif [ -f "${d}.hg_archival.txt" ]
		then
			local stat
			stat=$(print_tooldir_program stat) ||
				bomb "print_tooldir_program stat failed"
			if ! [ -x "${stat}" ]
			then
				buildtools
			fi

			t=$("${stat}" -t '%s' -f '%m' "${d}.hg_archival.txt") ||
				bomb "stat failed on ${d}.hg_archival.txt"
			rid=$(
			    awk '/^node:/ { print $2 }' <"${d}.hg_archival.txt"
			  ) || bomb \
			      "awk failed to find node: in ${d}.hg_archival.txt"
			vcs=hg
		else
			bomb "Cannot determine VCS for '$d'"
		fi
		NETBSD_REVISIONID="${NETBSD_REVISIONID}${NETBSD_REVISIONID:+-}${base}@${vcs}:${rid}"

		if [ -z "$t" ]
		then
			bomb "Failed to get timestamp for vcs=$vcs in '$d'"
		fi

		#echo "latest $d $vcs $t"
		if [ "$t" -gt "$MKREPRO_TIMESTAMP" ]
		then
			MKREPRO_TIMESTAMP="$t"
		fi
	done

	[ "${MKREPRO_TIMESTAMP}" -ne 0 ] || bomb "Failed to compute timestamp"
	if [ -z "${quiet}" ]
	then
		statusmsg2 "MKREPRO_TIMESTAMP" \
			"$(repro_date "${MKREPRO_TIMESTAMP}")"
	fi
	export MKREPRO MKREPRO_TIMESTAMP NETBSD_REVISIONID
}

main()
{
	initdefaults
	_args=$*
	parseoptions "$@"

	sanitycheck

	build_start=$(date)
	statusmsg2 "${progname} command:" "$0 $*"
	statusmsg2 "${progname} started:" "${build_start}"
	statusmsg2 "NetBSD version:"   "${DISTRIBVER}"
	statusmsg2 "MACHINE:"          "${MACHINE}"
	statusmsg2 "MACHINE_ARCH:"     "${MACHINE_ARCH}"
	statusmsg2 "Build platform:"   "${uname_s} ${uname_r} ${uname_m}"
	statusmsg2 "HOST_SH:"          "${HOST_SH}"
	if [ -n "${BUILDID}" ]
	then
		statusmsg2 BUILDID: "${BUILDID}"
	fi
	if [ -n "${BUILDINFO}" ]
	then
		printf "%b\n" "${BUILDINFO}" |
		while read -r line
		do
			[ -s "${line}" ] && continue
			statusmsg2 BUILDINFO: "${line}"
		done
	fi

	if [ -n "${MAKECONF+1}" ] && [ -z "${MAKECONF}" ]
	then
		bomb "MAKECONF must not be empty"
	fi

	rebuildmake
	validatemakeparams "$@"
	createmakewrapper
	setup_mkrepro

	# Perform the operations.
	#
	for op in ${operations}
	do
		case "${op}" in

		makewrapper)
			# no-op
			;;

		tools)
			buildtools
			;;
		libs)
			buildlibs
			;;

		sets)
			statusmsg "Building sets from pre-populated ${DESTDIR}"
			${runcmd} "${makewrapper}" ${parallel} ${op} ||
			    bomb "Failed to make ${op}"
			setdir=${RELEASEDIR}/${RELEASEMACHINEDIR}/binary/sets
			statusmsg "Built sets to ${setdir}"
			;;

		build|distribution|release)
			${runcmd} "${makewrapper}" ${parallel} ${op} ||
			    bomb "Failed to make ${op}"
			statusmsg "Successful make ${op}"
			;;

		cleandir|obj|sourcesets|syspkgs|params|show-params)
			${runcmd} "${makewrapper}" ${parallel} ${op} ||
			    bomb "Failed to make ${op}"
			statusmsg "Successful make ${op}"
			;;

		iso-image|iso-image-source)
			${runcmd} "${makewrapper}" ${parallel} \
			    CDEXTRA="$CDEXTRA" ${op} ||
				bomb "Failed to make ${op}"
			statusmsg "Successful make ${op}"
			;;

		live-image|install-image)
			# install-image and live-image require mtree spec files
			# built with MKUNPRIVED.  Assume MKUNPRIVED build has
			# been performed if METALOG file is created in DESTDIR.
			if [ ! -e "${DESTDIR}/METALOG" ]
			then
				bomb "The release binaries must have been" \
				     "built with -U to create images"
			fi
			${runcmd} "${makewrapper}" ${parallel} ${op} ||
			    bomb "Failed to make ${op}"
			statusmsg "Successful make ${op}"
			;;
		kernel=*)
			arg=${op#*=}
			buildkernel "${arg}"
			;;
		kernel.gdb=*)
			arg=${op#*=}
			configopts="-D DEBUG=-g"
			buildkernel "${arg}"
			;;
		releasekernel=*)
			arg=${op#*=}
			releasekernel "${arg}"
			;;

		kernels)
			buildkernels
			;;

		disk-image=*)
			arg=${op#*=}
			diskimage "${arg}"
			;;

		dtb)
			builddtb
			;;

		modules)
			buildmodules
			;;

		pkg=*)
			arg=${op#*=}
			if ! [ -d "${PKGSRCDIR}/${arg}" ]
			then
				bomb "no such package ${arg}"
			fi
			buildpkg "${arg}"
			;;

		installmodules=*)
			arg=${op#*=}
			if [ "${arg}" = / ] && {
				[ "${uname_s}" != NetBSD ] ||
				[ "${uname_m}" != "${MACHINE}" ]
			    }
			then
				bomb "'${op}' must != / for cross builds"
			fi
			installmodules "${arg}"
			;;

		install=*)
			arg=${op#*=}
			if [ "${arg}" = / ] && {
				[ "${uname_s}" != NetBSD ] ||
				[ "${uname_m}" != "${MACHINE}" ]
			   }
			then
				bomb "'${op}' must != / for cross builds"
			fi
			installworld "${arg}"
			;;

		rump)
			make_in_dir . do-distrib-dirs
			make_in_dir . includes
			make_in_dir lib/csu dependall
			make_in_dir lib/csu install
			make_in_dir external/gpl3/gcc/lib/libgcc dependall
			make_in_dir external/gpl3/gcc/lib/libgcc install
			dorump "${op}"
			;;

		rumptest)
			dorump "${op}"
			;;

		*)
			bomb "Unknown OPERATION '${op}'"
			;;

		esac
	done

	statusmsg2 "${progname} ended:" "$(date)"
	if [ -s "${results}" ]
	then
		echo "===> Summary of results:"
		sed -e 's/^===>//;s/^/	/' "${results}"
		echo "===> ."
	fi
}

main "$@"
