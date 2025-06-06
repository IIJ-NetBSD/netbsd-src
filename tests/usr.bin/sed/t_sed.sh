# $NetBSD: t_sed.sh,v 1.15 2025/06/03 19:03:23 martin Exp $
#
# Copyright (c) 2012 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Jukka Ruohonen and David A. Holland.
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

atf_test_case c2048
c2048_head() {
	atf_set "descr" "Test that sed(1) does not fail when the " \
			"2048th character is a backslash (PR bin/25899)"
}

c2048_body() {

	atf_check -s exit:0 -o inline:'foo\n' -e empty \
		-x "echo foo | sed -f $(atf_get_srcdir)/d_c2048.in"
}

atf_test_case emptybackref
emptybackref_head() {
	atf_set "descr" "Test that sed(1) handles empty back references"
}

emptybackref_body() {

	atf_check -o inline:"foo1bar1\n" \
		-x "echo foo1bar1 | sed -ne '/foo\(.*\)bar\1/p'"

	atf_check -o inline:"foobar\n" \
		-x "echo foobar | sed -ne '/foo\(.*\)bar\1/p'"
}

atf_test_case longlines
longlines_head() {
	atf_set "descr" "Test that sed(1) handles " \
			"long lines correctly (PR bin/42261)"
}

longlines_body() {

	str=$(awk 'BEGIN {while(x<2043){printf "x";x++}}')
	echo $str > input

	atf_check -o save:output -x "echo x | sed s,x,${str},g"
	atf_check -s exit:0 -o empty -e empty -x "diff input output"
}

atf_test_case rangeselection
rangeselection_head() {
	atf_set "descr" "Test that sed(1) handles " \
			"range selection correctly"
}

rangeselection_body() {
	# basic cases
	atf_check -o inline:"D\n" \
		-x "printf 'A\nB\nC\nD\n' | sed '1,3d'"
	atf_check -o inline:"A\n" \
		-x "printf 'A\nB\nC\nD\n' | sed '2,4d'"
	# two non-overlapping ranges
	atf_check -o inline:"C\n" \
		-x "printf 'A\nB\nC\nD\nE\n' | sed '1,2d;4,5d'"
	# overlapping ranges; the first prevents the second from being entered
	atf_check -o inline:"D\nE\n" \
		-x "printf 'A\nB\nC\nD\nE\n' | sed '1,3d;3,5d'"
	# the 'n' command can also prevent ranges from being entered
	atf_check -o inline:"B\nB\nC\nD\n" \
		-x "printf 'A\nB\nC\nD\n' | sed '1,3s/A/B/;1,3n;1,3s/B/C/'"
	atf_check -o inline:"B\nC\nC\nD\n" \
		-x "printf 'A\nB\nC\nD\n' | sed '1,3s/A/B/;1,3n;2,3s/B/C/'"

	# basic cases using regexps
	atf_check -o inline:"D\n" \
		-x "printf 'A\nB\nC\nD\n' | sed '/A/,/C/d'"
	atf_check -o inline:"A\n" \
		-x "printf 'A\nB\nC\nD\n' | sed '/B/,/D/d'"
	# two non-overlapping ranges
	atf_check -o inline:"C\n" \
		-x "printf 'A\nB\nC\nD\nE\n' | sed '/A/,/B/d;/D/,/E/d'"
	# two overlapping ranges; the first blocks the second as above
	atf_check -o inline:"D\nE\n" \
		-x "printf 'A\nB\nC\nD\nE\n' | sed '/A/,/C/d;/C/,/E/d'"
	# the 'n' command makes some lines invisible to downstreap regexps
	atf_check -o inline:"B\nC\nC\nD\n" \
		-x "printf 'A\nB\nC\nD\n' | sed '/A/,/C/s/A/B/;1,3n;/B/,/C/s/B/C/'"

	# a range ends at the *first* matching end line
	atf_check -o inline:"D\nC\n" \
		-x "printf 'A\nB\nC\nD\nC\n' | sed '/A/,/C/d'"
	# another matching start line within the range has no effect
	atf_check -o inline:"D\nC\n" \
		-x "printf 'A\nB\nA\nC\nD\nC\n' | sed '/A/,/C/d'"
}

atf_test_case preserve_leading_ws_ia
preserve_leading_ws_ia_head() {
	atf_set "descr" "Test that sed(1) preserves leading whitespace " \
			"in insert and append (PR bin/49872)"
}

preserve_leading_ws_ia_body() {
	atf_check -o inline:"    1 2 3\n4 5 6\n    7 8 9\n\n" \
		-x 'echo | sed -e "/^$/i\\
    1 2 3\\
4 5 6\\
    7 8 9"'
}

atf_test_case escapes_in_subst
escapes_in_subst_head() {
	atf_set "descr" "Test that sed(1) expands \x \d \o escapes " \
		"in substitution strings"
}

escapes_in_subst_body() {
	# basic tests
	atf_check -o inline:"fooXbar\n" \
		-x 'echo "foo bar" | sed -e "s/ /\x58/"'
	atf_check -o inline:"fooXbar\n" \
		-x 'echo "foo bar" | sed -e "s/ /\o130/"'
	atf_check -o inline:"fooXbar\n" \
		-x 'echo "foo bar" | sed -e "s/ /\d88/"'

	# overflowing the escaped char value
	# atf_expect_fail "PR bin/59453: sed misparses \[dox]number escapes"
	atf_check -o inline:"#8ball\n" \
		  -x "echo | sed -e 's/^/\d358ball/'"

	atf_check -o inline:"#3duh\n" \
		  -x "echo | sed -e 's/^/\o0433duh/'"

	atf_check -o inline:"#duh\n" \
		  -x "echo | sed -e 's/^/\x23duh/'"

	# check that escapes end after 2 or 3 chars
	atf_check -o inline:"00000000  09 38 62 61 6c 6c 0a                              |.8ball.|\n" \
		  -x "echo | sed -e 's/^/\d0098ball/' | hexdump -C | head -1"

	atf_check -o inline:"00000000  07 37 62 61 6c 6c 0a                              |.7ball.|\n" \
		  -x "echo | sed -e 's/^/\o0077ball/' | hexdump -C | head -1"

	atf_check -o inline:"00000000  01 38 62 61 6c 6c 0a                              |.8ball.|\n" \
		  -x "echo | sed -e 's/^/\x018ball/' | hexdump -C | head -1"
}

atf_test_case escapes_in_re
escapes_in_re_head() {
	atf_set "descr" "Test that sed(1) expands \x \d \o escapes " \
		"in regex strings"
}

escapes_in_re_body() {
	atf_check -o inline:"foo bar\n" \
		-x 'echo "fooXbar" | sed -e "s/\x58/ /"'
	atf_check -o inline:"foo bar\n" \
		-x 'echo "fooXbar" | sed -e "s/\o130/ /"'
	atf_check -o inline:"foo bar\n" \
		-x 'echo "fooXbar" | sed -e "s/\d88/ /"'
}

atf_test_case escapes_in_re_bracket
escapes_in_re_bracket_head() {
	atf_set "descr" "Test that sed(1) does not expand \x \d \o escapes " \
		"in regex strings inside braces"
}

escapes_in_re_bracket_body() {
	atf_check -o inline:"foo    bar\n" \
		-x 'echo "foo\\x58bar" | sed -e "s/[\x58]/ /g"'
	atf_check -o inline:"f       bar\n" \
		-x 'echo "fooo\\130bar" | sed -e "s/[\o130]/ /g"'
	atf_check -o inline:"foo    bar\n" \
		-x 'echo "foo\\d88bar" | sed -e "s/[\d88]/ /g"'
}

atf_test_case relative_addressing
relative_addressing_head() {
	atf_set "descr" "Test that sed(1) handles relative addressing " \
		"properly (PR bin/49109)"
}

relative_addressing_body() {
	atf_check -o match:"3" -x 'seq 1 4 | sed -n "1,+2p" | wc -l'
}

atf_init_test_cases() {
	atf_add_test_case c2048
	atf_add_test_case emptybackref
	atf_add_test_case longlines
	atf_add_test_case rangeselection
	atf_add_test_case preserve_leading_ws_ia
	atf_add_test_case escapes_in_subst
	atf_add_test_case escapes_in_re
	atf_add_test_case escapes_in_re_bracket
	atf_add_test_case relative_addressing
}
