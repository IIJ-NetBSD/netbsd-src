#!/usr/bin/awk -f

#	$NetBSD: tables.awk,v 1.3 2025/02/06 19:35:28 christos Exp $

# Copyright (c) 2013 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Taylor R. Campbell.
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

BEGIN {
	n_afs = 0
	n_socktypes = 0
}

!(($1 == "#define") && ($3 ~ /^[0-9]*$/)) {
	next
}

($2 ~ /^AF_[A-Z0-9_]*$/) && ($2 != "AF_MAX") {
	afs[n_afs++] = substr($2, 4)
}

$2 ~ /^SOCK_[A-Z0-9_]*$/ {
	socktypes[n_socktypes++] = substr($2, 6)
}

END {
	printf("/* Do not edit!  This file was generated automagically! */\n");
	printf("#include <sys/socket.h>\n");

	printf("\nstatic const char *const address_families[] = {\n");
	for (i = 0; i < n_afs; i++)
		printf("\t[AF_%s] = \"%s\",\n", afs[i], tolower(afs[i]));
	printf("};\n");

	printf("\nstatic const char *const socket_types[] = {\n");
	for (i = 0; i < n_socktypes; i++)
		printf("\t[SOCK_%s] = \"%s\",\n", socktypes[i],
		    tolower(socktypes[i]));
	printf("};\n");
}
