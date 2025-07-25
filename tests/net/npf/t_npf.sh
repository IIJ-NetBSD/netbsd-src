# $NetBSD: t_npf.sh,v 1.9 2025/07/20 12:25:54 joe Exp $
#
# Copyright (c) 2008, 2010 The NetBSD Foundation, Inc.
# All rights reserved.
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

run_test()
{
	local name="${1}"

	if [ "$name" = "l2defpass" ]; then
		atf_check -o ignore -e ignore npfctl debug -c "$(atf_get_srcdir)/npftestl3.conf" -o ./npf.plist
	else
		atf_check -o ignore -e ignore npfctl debug -c "$(atf_get_srcdir)/npftest.conf" -o ./npf.plist
	fi

	atf_check -o ignore npftest -c npf.plist -T "${name}"
}

cleanup()
{

	if [ -f npftest.core ]; then
		gdb -batch -ex bt npftest npftest.core
		# Extract kernel logs including a panic message
		strings npftest.core |grep -E '^\[.+\] '
	fi
}

add_test()
{
	local name="${1}"; shift
	local desc="${*}";

	atf_test_case "npf_${name}" cleanup
	eval "npf_${name}_head() {
			atf_set descr \"${desc}\"
			atf_set require.progs npfctl npftest
		}
	    npf_${name}_body() {
			run_test ${name}
		}
	    npf_${name}_cleanup() {
			cleanup
		}"
	atf_add_test_case "npf_${name}"
}

atf_init_test_cases()
{
	LIST=/tmp/t_npf.$$
	trap "rm -f $LIST" EXIT

	sh -ec 'npftest -L || printf "dummy\tnone\n"' > $LIST 2>/dev/null

	while read tag desc
	do
		add_test "${tag}" "${desc}"
	done < $LIST
}
