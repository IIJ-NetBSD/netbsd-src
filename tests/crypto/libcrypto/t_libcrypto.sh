# $NetBSD: t_libcrypto.sh,v 1.10 2024/04/28 07:27:40 rillig Exp $
#
# Copyright (c) 2008, 2009, 2010 The NetBSD Foundation, Inc.
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

atf_test_case engine
engine_head()
{
	atf_set "descr" "Checks ENGINE framework"
}
engine_body()
{
	atf_check -o ignore -e ignore "$(atf_get_srcdir)/h_enginetest" \
	    "$(atf_get_srcdir)/d_server.pem"
}

atf_test_case bn
bn_head()
{
	atf_set "descr" "Checks BIGNUM library"
	atf_set "timeout" "7200"
}
bn_body()
{
	atf_check -o ignore -e ignore "$(atf_get_srcdir)/h_bntest"
	atf_check -o ignore -e ignore "$(atf_get_srcdir)/h_divtest"
	atf_check -o ignore -e ignore "$(atf_get_srcdir)/h_exptest"
}

atf_test_case conf
conf_head()
{
	atf_set "descr" "Checks configuration modules"
}
conf_body()
{
	cp $(atf_get_srcdir)/d_conf_ssleay.cnf ssleay.cnf

	atf_check -o file:$(atf_get_srcdir)/d_conf.out \
		$(atf_get_srcdir)/h_conftest
}

atf_test_case threads
threads_head()
{
	atf_set "descr" "Checks threading"
}
threads_body()
{
	local s=$(atf_get_srcdir)
	if [ -f "$s/rsakey.pem" ]; then
	    atf_check -o ignore -e ignore "$s/h_threadstest" \
		-config "$s/default.cnf" "$s"
	else
		"$s/h_threadstest" \
		    -cert "$s/d_server.pem" \
		    -ccert "$s/d_client.pem" \
		2>&1 | tee out
		atf_check -s exit:1 -o empty -e empty grep :error: out
	fi
}

atf_init_test_cases()
{
	atf_add_test_case engine
	atf_add_test_case bn
	atf_add_test_case conf
	atf_add_test_case threads
}
