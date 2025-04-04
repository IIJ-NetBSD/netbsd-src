/*	$NetBSD: regex_test.c,v 1.3 2025/01/26 16:25:50 christos Exp $	*/

/*
 * Copyright (C) Internet Systems Consortium, Inc. ("ISC")
 *
 * SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * See the COPYRIGHT file distributed with this work for additional
 * information regarding copyright ownership.
 */

#include <inttypes.h>
#include <sched.h> /* IWYU pragma: keep */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_REGEX_H
#include <regex.h>
#endif /* ifdef HAVE_REGEX_H */

#define UNIT_TESTING
#include <cmocka.h>

#include <isc/commandline.h>
#include <isc/regex.h>
#include <isc/util.h>

#include <tests/isc.h>

/* test isc_regex_validate() */
ISC_RUN_TEST_IMPL(regex_validate) {
	/*
	 *  test regex were generated using http://code.google.com/p/regfuzz/
	 *  modified to use only printable characters
	 */
	struct {
		const char *expression;
		int expect;
		int exception; /* regcomp accepts but is
				* disallowed. */
	} tests[] = {
		{ "", -1, 0 },
		{ "*", -1, 0 },
		{ ".*", 0, 0 },
		{ ".**", -1, 0 },
		{ ".*\?", -1, 0 },
		{ ".*+", -1, 0 },
		{ "+", -1, 0 },
		{ ".+", 0, 0 },
		{ ".++", -1, 0 },
		{ ".+\?", -1, 0 },
		{ ".+*", -1, 0 },
		{ "\?", -1, 0 },
		{ ".\?", 0, 0 },
		{ ".\?\?", -1, 0 },
		{ ".\?*", -1, 0 },
		{ ".\?+", -1, 0 },
		{ "(", -1, 0 },
		{ "()", 1, 0 },
		{ "(|)", -1, 0 },
		{ "(a|)", -1, 0 },
		{ "(|b)", -1, 0 },
		{ ".{", 0, 0 },
		{ ".{1", -1, 0 },
		{ ".\\{1", 0, 0 },
		{ ".{1}", 0, 0 },
		{ ".\\{1}", 0, 0 },
		{ ".{,", 0, 0 },
		{ ".{,}", 0, 0 },
		{ ".{1,}", 0, 0 },
		{ ".\\{1,}", 0, 0 },
		{ ".{1,\\}", -1, 0 },
		{ ".{1,", -1, 0 },
		{ ".\\{1,", 0, 0 },
		{ ".{1,2}", 0, 0 },
		{ ".{1,2}*", -1, 0 },
		{ ".{1,2}+", -1, 0 },
		{ ".{1,2}\?", -1, 0 },
		{ ".{1,2", -1, 0 },
		{ ".{2,1}", -1, 0 },
		{ "[", -1, 0 },
		{ "[]", -1, 0 },
		{ "[]]", 0, 0 },
		{ "[[]", 0, 0 },
		{ "[^]", -1, 0 },
		{ "[1-2-3]", -1, 0 },
		{ "[1-22-3]", 0, 0 },
		{ "[+--23]", 0, 0 },
		{ "[+--]", 0, 0 },
		{ "[-1]", 0, 0 },
		{ "[1-]", 0, 0 },
		{ "[[.^.]]", 0, 0 },
		{ "[^]]", 0, 0 },
		{ "[^^]", 0, 0 },
		{ "[]]\?", 0, 0 },
		{ "[[]\?", 0, 0 },
		{ "[[..]]", -1, 0 },
		{ "[[...]]", 0, 0 },
		{ "[[..5.]--]", -1, 0 },
		{ "[[.+.]--]", 0, 0 },
		{ "[[..+.]--]", -1, 0 },
		{ "[[.5.]--]", -1, 0 },
		{ "[1-[=x=]]", -1, 0 },
		{ "[[:alpha:]]", 0, 0 },
		{ "[[:alpha:]", -1, 0 },
		{ "[[:alnum:]]", 0, 0 },
		{ "[[:alnum:]", -1, 0 },
		{ "[[:digit:]]", 0, 0 },
		{ "[[:digit:]", -1, 0 },
		{ "[[:punct:]]", 0, 0 },
		{ "[[:punct:]", -1, 0 },
		{ "[[:graph:]]", 0, 0 },
		{ "[[:graph:]", -1, 0 },
		{ "[[:space:]]", 0, 0 },
		{ "[[:space:]", -1, 0 },
		{ "[[:blank:]]", 0, 0 },
		{ "[[:blank:]", -1, 0 },
		{ "[[:upper:]]", 0, 0 },
		{ "[[:upper:]", -1, 0 },
		{ "[[:cntrl:]]", 0, 0 },
		{ "[[:cntrl:]", -1, 0 },
		{ "[[:print:]]", 0, 0 },
		{ "[[:print:]", -1, 0 },
		{ "[[:xdigit:]]", 0, 0 },
		{ "[[:xdigit:]", -1, 0 },
		{ "[[:unknown:]]", -1, 0 },
		{ "\\[", 0, 0 },
		{ "(a)\\1", 1, 0 },
		{ "(a)\\2", -1, 1 },
		{ "\\0", 0, 0 },
		{ "[[][:g(\?(raph:][:alnu)(\?{m:][:space:]h]<Z3})AAA)S[:space:]"
		  "{176,}",
		  0, 0 },
		{ "(()IIIIIIII(III[[[[[[[[[[[[[[[[[[^[[[[[[[[              [^  "
		  "     "
		  "fX][:ascii:].)N[:a(\?<!lpha:])][:punct:]e*y+)a{-124,223}",
		  3, 0 },
		{ "(pP\\\\\\(\?<!"
		  "\\\\\\\\\\\\\\\\\\\\\\lRRRRRRRRRRRRRRRRBBBBBBBBBBBBBBBB))"
		  "kkkkkkkkkkkkkkkkkkkkk|^",
		  1, 0 },
		{ "[^[^[{111}(\?=(\?:(\?>/"
		  "r(\?<(\?=!(\?(\?!<!Q(\?:=0_{Meqipm`(\?((\?{x|N)))))|))+]+]Z)"
		  "O{,-215}])}))___________________{}",
		  0, 0 },
		{ "[C{,-218(\?=}E^< ]PP-Ga)t``````````````````````````{138}", 0,
		  0 },
		{ "[^h(\?<!(\?>Nn(\?#])))", 0, 0 },
		{ "[(\?!(\?<=[^{,37}AAAA(AAAAAAAAAAAAA])", 0, 0 },
		{ "[^((\?(\?:ms(\?<!xims:A{}(\?{*</H(\?=xL "
		  "$(\?<!,[})))*)qqqqqqqqqqqqqqqqqq)]"
		  "33333333333333333333333333333{[:graph:]p)-+( "
		  "oqD]){-10,}-{247}_______________________X-e[:alpha:][:"
		  "upperword:]_(______wwwwwwwww "
		  "/c[:upperword:][:alnum:][:alnum:][:pun(\?{ct:])[:blankcntrl:"
		  "]})*_*",
		  2, 0 },
		{ "[(\?<!:lowerprin(\?{t:]{}}){113,})[:punct:]"
		  "IIIIIIIIIIIIIIIIIIIIIIII",
		  0, 0 },
		{ "PP)", 0, 0 },
		{ "(([^(\?<!((\?>\?=[])p.]}8X[:blankcntrl:],{-119,94})XmF1.{)-)"
		  "[:upperword:])[:digit:]{zg-q",
		  2, 0 },
		{ "[^[({(\?#254}))Z[l][x50]=444444444444(4444444444u[:punct:]"
		  "\?[:punct:(\?!])])",
		  1, 0 },
		{ "[^[^[^([^((*4[(^((\?<=])Ec)", 0, 0 },
		{ "(0)Y:8biiiiiiiiiiiiiiiiiii", 1, 0 },
		{ "[^w(\?!)P::::::::::::::(\?#::(\?<=:::::::::]\"\"{}["
		  "3333333333333333(\?<=33333(\?!)9Xja][:alph(\?<=a:])xB1)("
		  "PX8Cf\?4444)qq[:digit:])",
		  1, 0 },
		{ "([U[^[^].]^m]/306KS7JJJJJJJJ{})", 1, 0 },
		{ "[^[^([^[(\?!(\?>8j`Wg2(\?{,(\?>!#N++++(\?<![++++++)+"
		  "44444444bA:K(\?<!O3([:digit:]3]}}}}}}}}}}}}}}}}}}}}}}}}LP})"
		  "S",
		  0, 0 },
		{ "[({(\?{,(\?(=213}*))})]WWWWWWWWWWWWWWW[:alnum:])", 0, 0 },
		{ "[:(\?<=ascii:])", 0, 0 },
		{ "[U(\?#)(\?<=+HzE])[:punct:]{-207,170}\?s.!", 0, 0 },
		{ "{}z=jU75~n#soD\"&\?UL`X{xxxxxxxxxxxxxxxxxxxx(xxxxxx${-246,"
		  "27}[:graph:]g\"{_bX)[:alnum:][:punct:]{-79,}-",
		  1, 0 },
		{ "[^{,-186}@@@@[^(\?{@@(\?>@+(\?>l.]}))*\\BCYX]^W{52,123}("
		  "lXislccccccccccccccccc)-*)",
		  1, 0 },
		{ "(x42+,)7=]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]", 1, 0 },
		{ "[^(*[:graph:]q/TH\?B(\?{P)]})uZn[:digit:]+2", 0, 0 },
		{ "([XXXXXXXXXXXXXXXXXXXXX[(:alnum:][:space:]i%[:upperw(\?=o("
		  "\?#rd:])) ",
		  1, 0 },
		{ "(@@@@)", 1, 0 },
		{ "{-18,}[:as[(\?>^[cii:]]{}>+{-46,}{,95}[:punct:]{}"
		  "99999999999999])-{-134}'sK$"
		  "wCKjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjj",
		  0, 0 },
		{ "(l[:alpha:(\?!]))", 1, 0 },
		{ "[[^(\?{]|JJ[:alph(a:]X{})B^][:lowerprint:]n-219{-32}{19,105}"
		  "k4P}){,-144}",
		  0, 0 },
		{ "[[^]P[:punct:][:alpha:][:xdigit:]syh]|W#JS*(m<2,P-RK)cA@", 1,
		  0 },
		{ "([^((\?({\?<=)}){[^}^]{}])^P4[:punct:[]$)]", 1, 0 },
		{ "([(\?#:(\?{space:]}):{}{-242,}n)F[:alpha:]3$)d4H3up6qS[:"
		  "blankcntrl:]B:C{}[:upperword:]r",
		  1, 0 },
		{ "([(\?:]))[:digit:]mLV.{}", 1, 0 },
		{ "[^PPP-[]{[,50}{128,}]111111111111111]p", 0, 0 },
		{ "[^([^([^([[^[([^[^[[2[[[[[[[[[[[[[^[[[[(\?(\?{:[[[[[[(\?([-["
		  ":ascii:]--*)",
		  -1, 0 },
		{ ")!F^DA/ZZZZZZZZZZZZZZZZZZ", 0, 0 },
		{ "[[[[[[[((\?=\?(\?>([[[[[[[^[[[[(\?()[[[K(\?#))])))]7Y[:"
		  "space:]{,-96}pP)[:ascii:]u{-88}:N{-251}uo",
		  0, 0 },
		{ "t[:x(\?<=digit:])eYYYYYYYYYYYYYYYYYY{,-220}A", 0, 0 },
		{ "[[({10,}[:graph:]Pdddddd(\?#X)])[:alnum:(]]L-C){,50}[:"
		  "blankcntrl:]p[:gra(ph:]){66,}",
		  0, 0 },
		{ "[^[^]*4br]w[:digit(\?::]n99999999999999999)P[:punct:]pP", 0,
		  0 },
		{ "[:digit:]{67,247}!N{122})VrXe", 0, 0 },
		{ "[:xdigit:]^[:xdigit:]Z[:alnum:]^^^^1[:upperword:(\?=])[:"
		  "lowerprint:]*JJ-",
		  0, 0 },
		{ "[[(\?imsximsx:^*e(){,3[6}](V~\?^[:asc(\?!ii:]I.dZ))]$^"
		  "AAAAAAAAAAAAAAAAAAAAAAAA[:space:]k)]",
		  1, 0 },
		{ "W{,112}[:lowerp(\?<!rint:]$#GT>R7~t'"
		  "\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"9,O).",
		  0, 0 },
		{ "[^{6((\?>\?:4}(\?<=G))f)"
		  "KKKKKKKKKKKKKKKKKKKKKKKKKKKKKpppppppp(\?=ppppp]{,-101}|[:"
		  "blankcntrl:]Z{-182})",
		  0, 0 },
		{ "([:punct:]@^,,,,,,,,,,,,,,,,,,,,,,,,,,0\?:-o8NPIIIIIIIII)"
		  "pPKKKKKKKKKKKKKKKKKKKK",
		  1, 0 },
		{ "([^[[^[^]]]])", 1, 0 },
		{ "[([^[(333\"(\?#\\\\[)(\?isx-x:\"Tx]')", 0, 0 },
		{ "[[n>^>T%.zzzzzzzzzzzzzzzzz$&|Fk.1o7^o, "
		  "^8{202,-12}$[:alnum:]]G[:upperword:]V[:xdigit:]L|[:"
		  "upperword:]KKKKKKKKKKKKYX\"\")xJ "
		  "~B@[{,-68}/][:upperword:]QI.",
		  0, 0 },
		{ "[^[]tN^hy3\"d@v T[GE\?^~{124,10(\?{2}]})\?[:upperword:]O", 0,
		  0 },
		{ "d.``````````````````````````[:up(\?=perword:]"
		  "RRRRRRRRRRRRRRR)",
		  0, 0 },
		{ "[Z{{{{{{{{{{{{{(\?={(\?<!{{{{{{{{{(\?>{{J6N:H[tA+mN3Zmf:p\?]"
		  "\?){-181,82}S4n.b[:lowerpri(\?{nt:]|"
		  "ggggggggggggggggggggggggggggggg}))4)",
		  0, 0 },
		{ "[^((/////[^////[^/////////[(^/////]fI{240}{-120}+]R]GA)", 0,
		  0 },
		{ "[-(\?#.)(\?())[:alpha:](\?={(\?#}r)[:space:]PPW]o)", 0, 0 },
		{ "[:lowerp(\?{rint:]})201{46,}[:a[^scii:]0Q{37,}][:blankcntrl:"
		  "]1331",
		  0, 0 },
		{ "[^(\?!(\?#)\\GIwxKKKKKKKKKK'$KKKKKKKK]l)bbb^&\?", 0, 0 },
		{ "[:ascii:]*[:sp(\?<=ace:])", 0, 0 },
		{ "({-66,}Z{})0I{-111,}[:punct(\?():])", 1, 0 },
		{ "[[^(\?!()%%%%%%%%%%%%%(\?:%%%%%%%%%%%%%%%%)t(\?{VX>B#6sUU("
		  "\?<!UUUUUU(\?=UUU[^UUUUUUUUUUUU(\?((\?:UPPPPPPPPPPP)"
		  "PPPPPPPPPPPPPPP]ffffffffffffffffffffffff)^[:space:]"
		  "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww{243}9[:lowerprint:]Dv[:"
		  "graph:])][:blankcntrl:]V%E[:graph:]})[:space:]{-83,}cQZ{}4{-"
		  "23,135}",
		  0, 0 },
		{ "({,-76[}]O[:xdi(\?<!git:])\?5))))))))\?d[:lowerprint:]"
		  "b666666[:graph:]c",
		  1, 0 },
		{ "{}{-145,}[:(\?(spa)ce:])f", 0, 0 },
		{ "[([^].{116,243}]T*[[^:punct(\?[{[^:(\?<!]]8()])[:alnum:])})]"
		  "N{}{,243}*[n]][:graph:]",
		  1, 0 },
		{ "[^w]8888888888888888_________(__________[:ascii:]BdqTE$^0|"
		  "MNto*i#############[^#################])",
		  1, 0 },
		{ "[^[[[<[()\?]GGG{,26[}[:alnum:]SSSSS.gggggggg[:graph:]"
		  "CCCCCCCCCCC{79,}{138,191}][:di(git:]u]@]"
		  "JJJJJJJJJJJJJJJJJJJJJJJ[:graph:(\?:][:alnum:]])[:alnum:])]",
		  0, 0 },
		{ "[^(((BBBBBBBBBB(\?>BBBZvvvvvvvvvv(\?m(sximsx:vvv)iiiiiiii)))"
		  "j>Rs:Sm]0MMMMMMMMMMM|@F)Y]*^#EEEEEEE)*",
		  0, 0 },
		{ "([^([(U(\?!)<<<<<<<<<<(\?#<<<<(\?<!<<<)(\?=L.{73,})+]n9U}fk%"
		  "Jn}'b Na<%yyyyyyyyyyyy)){-198,}]))[:space:].pP361U]3s@u_9AU "
		  "Te/{s`6=IMZdL1|.ySRo",
		  1, 0 },
		{ "[[((\?<=\?>(\?#){}]{}`){1,82}){-143[,}]^G", 0, 0 },
		{ "[:digit:]W|[:up(\?<!perword:]{,-101}llllllllllllllllll[:"
		  "upperword:])mmYYYYYYYYYYYYYYYYYYYYYYY*",
		  0, 0 },
		{ "@NHy)", 0, 0 },
		{ "([^[^]][:alnum:]222[^22222222(\?{2222222222222222][:lo(\?:"
		  "werprint:][:xdigit:]^[:blankcntrl:]s+N)[:alpha:]-"
		  "NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNWxxxxxxxxxxxxxxxxxxxxxxxxxxD["
		  ":space:]U)TTTTTTTTTTfffffffffffzzzzzzzzzzzzzzzzzzzzzzzzz})",
		  1, 0 },
		{ "[^[^[[^[][^[]pP([^\?[^<=(\?=]){158,})]]]][:digit:]]"
		  "K22222222222p^dUKJ`\">@]",
		  1, 0 },
		{ "[^[^[(\?imsximsx::p(\?{unct:][(\?>:ascii:]5w)]{159}\\Q\?@C]"
		  "4(44444444}[^)|)[:graph:]]C:b)",
		  1, 0 },
		{ "[^[[(tYri[W<8%1(\?='yt][:lowerprint:[]))1r]][:alnum:][:"
		  "digit:]{48}{-52,-183}+][:alpha:]r][:upperword:]\?{-105,155}{"
		  "-55,-87}pPN#############################{63,232}]",
		  0, 0 },
		{ "[*(\?>L(\?<(\?>=))]&&&&&&&(&&&&&&&&&&&&&&&&&&))[|WIX]{-62,-"
		  "114}S  K=HW60XE<2+W",
		  1, 0 },
		{ "(00000000000)z\\\\*t{}R{88}[:alnum:]*", 1, 0 },
		{ "(([^(\?=\?gggggg[gLw)]{-250,}[:xdigit:]yZ[:g(raph:]8QNr[:"
		  "space:][:blankcntrl:]A)][:digit:]D)[:xdigit:])",
		  2, 0 },
		{ "[^([^,(\?<!]*))]", 0, 0 },
		{ "[^(\?{[:alnum:]]}}}}}}}}}}}}}}}}}}}}}}}){-83}", 0, 0 },
		{ "WWWWWWWW[:alnum(\?<=(\?#:]{,-1})@OSSS)[:digit:]", 0, 0 },
		{ "[^(\?!*]+G)", 0, 0 },
		{ "[LLLLLLLLLLLLLLLLLLLLLLLLLLLLLL>s8.>[^{}$(\?(]]XXXXXXX)"
		  "XXXXXXXXXXXXXX[:alpha:]Whii\?p[:xdigit:])+",
		  0, 0 },
		{ "(7777[:blankcntrl:])", 1, 0 },
		{ "[^C[:digit:]]{}YYYY(YYYYYYYYYYYYYYYY)", 1, 0 },
		{ "on|,#tve%F(w-::::::::::::::::::::::::::::*=->)", 1, 0 },
		{ "([((\?=(\?!((\?=')))27(<{})S-vvvvvvvvvv(\?="
		  "vvvvvvvvvvvvvvvvv[:punct:][:alnum:]}}}}}}}}}}}}}}}}}}}}}}}"
		  "PPPPPPPPPPPPPPPPPPPPPPPPPPPPPgggggggggggggggggggggggggg(\?#("
		  "\?#gggggg<X){}]{-164,61})>+))uQ)W>[:punct:][:xdigit:][:"
		  "digit:][:punct:]{}[:digit:][:space:]){,-105}=xiAyf}o[:alpha:"
		  "]akZSYK+sl{",
		  1, 0 },
		{ "[^[^]/S:Hq<[:upperword:(\?<=]W[:alnum:]X])1973", 0, 0 },
		{ "[[^[[^([^VVVV(\?!(VVVVVVVVVVVVVVVVVVVVV[VVVVX][^]2))"
		  "98ppppppppppppppppppppppppppppppp/////////////////////"
		  "b.]G{-101,}[:[ascii:]P].=~])AAAAAAAAAAAAA2{-153,}]]]]]]]]]]]"
		  "]]]]]]]]]]]]]]]]]]]]][:alnum:][:lowerprint:]WN/"
		  "D!rD]|4444{180}]V_@3lW#lat]",
		  0, 0 },
		{ "[^[^([^TTTTT(\?:T(\?:T7777{,59}])[:graph:][:ascii(\?<=:]))f]"
		  "AD{,-43}%%%%%%%%%%%%%%%%)S|[:digit:]FZm<[:blankcntrl:]QT&xj*"
		  "{-114,}$[:xdigit:]042][:xdig[it:]{-180}027[:alpha:][:ascii:]"
		  "[:lowerprint:][:xdigit:]^|[:alnum:][^Mi]z!suQ{-44,-32}[:"
		  "digit:]]",
		  0, 0 },
		{ ")", 0, 0 },
		{ "''''''''''[:a(\?imsxisx:lnum:])P", 0, 0 },
		{ "(([{20(\?<=8}[:alnum:]pP$`(\?#N)wRH[:graph:]aaaaaaaaaaaaaa("
		  "\?=aaaaaaaaaaaaaaaaP]a)))[:punct:]-\?)A^",
		  2, 0 },
		{ "[^(.//"
		  "[:punct:]&-333333333333333333333333333(\?<!33)"
		  "LLLLLLLLLLLLLLLLL[:alnum:]$1]~8]|^\"A[:xdigit:]\?[:ascii:]{"
		  "128,}{,-74}[:graph:]{157}3N){-196,184}D",
		  0, 0 },
		{ "[^($(\?{(\?<=)[#)]})[:space:]]nWML0D{}", 0, 0 },
		{ ",,,,,,,,,,,,,,,,,,,,,,,,,,,,,[^]x{213,-93}(\?{A7]V{}})", 0,
		  0 },
		{ "[k(\?=*)+^[f(])r_H6", 0, 0 },
		{ "[(\?#(\?{)]q})", 0, 0 },
		{ "([GLLLLLLLLLL(\?!((\?:LLLLLLLL]))C#T$Y))^|>W90DDDDDDDDDDD[^"
		  "DDDDDDDDDDDDDDDDDDDD]B[:punct:]c/",
		  1, 0 },
		{ "[^(\?<!)(\?{b}){,199}A[:space:]+++++++(\?!++++++++{36}Tn])",
		  0, 0 },
		{ "()[:alpha:]a", 1, 0 },
		{ "[(\?(:blan)kcntrl:])lUUUUUUUUUUUUUUUUUUUUUUU", 0, 0 },
		{ "[^[^(s[[[[[[[[[[[[[[(\?#[[[[[[[)\?`````][:blankcntrl:(\?>]|)"
		  "p1EmmmmmmmmmmmmmmmmmmmmmmmmmmmmL{-241}666666666666666666666)"
		  "]^bLDDDDDDDDDDDDD]",
		  0, 0 },
		{ "[nn(\?<!nnnnn(\?#n8)=````````````````````{41,}]U,cb*%Y[:"
		  "graph:]).[:alnum:]\\\\\\\\\\gt",
		  0, 0 },
		{ "()\?5{,-195}lm*Ga[:space:]Y", 1, 0 },
		{ "[(\?:].di)c", 0, 0 },
		{ "([([^([\?{})Za,$S(\?!p(\?{++(\?##V(\?<!Evuil.2(\?<![^[h|[^']"
		  "C)*\"]5]",
		  1, 0 },
		{ "[((^24(\?#4[^Kkj{}))]]{232}47)077[:alpha:]zzzzzzzz{}", 0,
		  0 },
		{ "[^(\?:[^F]o$h)-iV%]", 0, 0 },
		{ "[[^[([((([^(\?{[^((\?=)kaSx(\?imsximsx:w3A[`%+A$I{,62}ns&Y!#"
		  "ay "
		  "o9YAo{Y>1((\?>\?#45)Z{,108}{}11111111111111111111111111qqqq)"
		  "\?][:lowerprint:]mbo#)@",
		  0, 0 },
		{ "[^iii8(888888(\?<!8^]))s", 0, 0 },
		{ "([[(\?(\?:({^]}[)[(r)])G]{,-87}", 1, 0 },
		{ "([[^{249,}(\?>(\?=)]]T()[:bl(\?!ankcntrl:]=jjjjjjjjjjjjjjjj-"
		  ")))t{}[:alpha:]-\":i! Gn[A4Ym7<<<<<<<<<<<<<<<<]",
		  2, 0 },
		{ "^{}{[^,241(\?#}(\?m(\?ixim:sximsx:]t))+oD)", 0, 0 },
		{ "5[(\?#:xdigit:])", 0, 0 },
		{ "[^f{(\?>,22(9}[^[^])6KKKKKKKKKKKKK)]RRRRRRRRfuK99999999C}"
		  "osnNR]BgCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC[:blankcntrl:]",
		  0, 0 },
		{ "[^(\?=U){24,}W-{,17(\?:3[^}]q.nQ#PU_|i$$$$$$$$$$$$$$+)[:dig("
		  "\?<!it:]){-98}\?[:upperword:]]",
		  -1, 0 },
		{ "[(\?<=[0(\?!72])euE.]{,-159}[:alnum:]t-:l\?)$"
		  "yyyyyyyyyyyyyyyyyyyyyyyyyyfffffffffffffffffffffffffff",
		  0, 0 },
		{ "[^[^]q[:asc(\?imsxmsx:ii:]JJJJJJJJJJJJJJJJJJJJ[:graph:]]$)`#"
		  "DdY^qqqqqqqqqqqqqqqqqqqqqqqqqqqu>4^4ta[:alpha:]",
		  0, 0 },
		{ "(((b0HN)q))p5<T())`7JJv{'cv'#L8BNz", 4, 0 },
		{ "[pFp2VttBg(\?<=7777777777777|TTTTTTTTTTTTTTT[:space:]Z]^p\"["
		  ":blankcntrl:])",
		  0, 0 },
		{ ")aM@@@@@@@@@@@@@", 0, 0 },
		{ "([^[(\?<![^])", 1, 0 },
		{ "()Z[:ascii:]", 1, 0 },
		{ "(fuPPo)..........................[:xdigit:]{}{,4}*kkkkkkkCx#"
		  ",_=&~)|.2x",
		  1, 0 },
		{ "[+(\?<=){}++++++[:alnum:](\?=+]s)[:alnum:]~~~~~~"
		  "XXXXXXXXXXXXXXX.[:digit:]",
		  0, 0 },
		{ "[{}[^^(\?(]))CCCCCCCCCCCCCCCCCCCCEg2cF]{}3", 0, 0 },
		{ "([[[^[^[^([[^[^([(\?<=G[[)=(\?!===(\?isximsx:==(\?#==[^====="
		  "(\?{==================$T[[^^u_TiC.Fo.02>X)uH]$})354b[:alnum:"
		  "]]]EVVVVVVVVVVVVVVVVVVVVVVVVVVVVVz[:digi(\?(t:][:upperword:]"
		  ")",
		  1, 0 },
		{ "([:blankcntrl:]t-){121,}[:ascii:]444444{}[:graph:]E040", 1,
		  0 },
		{ "[^{134,}]DzQ\?{-30,191})z,\?1Vfq!z}cgv)ERK)1T/=f\?>'", 0,
		  0 },
		{ "@v)<yN]'l-/"
		  "KKKKKKKBBBBBBBBBBBBBMa2eLA[:digit(\?<!:])\"\"e|l$&m`_yn[:"
		  "blankcntrl:]uuuuuuuuuuuuuuuuuuu[:punct:]",
		  0, 0 },
		{ "[[999999999999999(\?<=(\?:(\?ixmx:(\?>))])Y]|){,10}\?{}", 0,
		  0 },
		{ "([[[(\?!^]P-AA[AAAAAA[A[^A)r]+B]])", 1, 0 },
		{ "3}|[:ascii:][:punct:]()", 1, 0 },
		{ "()dw", 1, 0 },
		{ "[N]{})))))))))))))))))))))))", 0, 0 },
		{ "[[[^([[(\?()(\?#)++([^\?{+++[^+++++++++++(\?!+(\?=+++++++r9/"
		  "n]N7{-219}{-91}pP[:punct:]T]mROm+~[:digit:][:digit:])Y:",
		  0, 0 },
		{ "[^'Pu[(\?<!D&]_a[:alnum:]E<,F%4&[:xdigit:])][:lowerprint:]",
		  0, 0 },
		{ "tttt(tttttttttt*uKKUUUUU)", 1, 0 },
		{ "([:ascii:]GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG)+kX______________{"
		  "}GGGGG\?TUH3,{67,77}|[:graph:]C{,-136}{}[:upperword:[]{,-6}&"
		  "]T84]n={C",
		  1, 0 },
		{ "[:upperword:]DC[:u(\?<=pperword:]*d`H0\?m>~\?N|z#Ar--SO{,-"
		  "141}076)G\?{,-110}M+-[:alpha:]",
		  0, 0 },
		{ "{,-214}{,10(9})", 1, 0 },
		{ "([^xxxxxxxxxxxxxxxxxMMMMMMMMMMMMMMXW])].[:punct:]Q`{-63,63}"
		  "Uua[:alnum:]\?OQssb#L@@@@@@@@(@@@)[:graph:]",
		  2, 0 },
		{ "[[^(\?!```[^``````````````(\?<=``(\?>````````M/////(\?!/////"
		  "///////////////"
		  "[^GD!|#li]~)*.$]))Tq!]C[:lowerprint:]Qk[{}]]"
		  "JJJJJJJJJJJJJJJJJJJJJJJ{e])c",
		  0, 0 },
		{ "$[5(7ES])[:xdigit:]%{MRMtYD&aS&g6jp&ghJ@:!I~4%{"
		  "P\?0vvvvvvvvvvvvvvvvvvvv\\\\\\\\\\\\\\\\\\\\\\\\x54[:"
		  "lowerprint:][:upperword:]",
		  0, 0 },
		{ "[((([(\?((\?>[:alnum:][):as(\?<!cii:(\?:]Re))K|)|^){-28,89}"
		  "l<H.<H:N)QKuuuuuuuuw8E136P)^)[:ascii:]][:xdigit:]-",
		  0, 0 },
		{ "(pjvA'x]=D\"qUby\\+'R)r\?C22[:ascii:]", 1, 0 },
		{ "[]*b~y C=#P\"6(gD%#-[^FBt{}]${-244}", 0, 0 },
		{ "[:up(\?!pe(\?=rword:])lA-'yb\"Xk|K_V\"/"
		  "@}:&zUA-)W#{-178,-142}(){-202,}",
		  1, 0 },
		{ "()1.WldRA-!!!!!!!!!!!!!!!!!", 1, 0 },
		{ "lZZZZZZZZZZZZZZZ(Z[:al(\?:num:])"
		  "ttttttttttttttttttttttttttttttg.)6$yyy",
		  1, 0 },
		{ "[([^([^[^(([([^[^(([[$(\?{P(\?=(\?<(\?!=(\?#P[^Y])<GA[:"
		  "ascii:][(\?#(\?<!:alpha:](B{100,})]}))\?)XU=",
		  1, 0 },
		{ "[[dVw{6(\?{9,}2222kkkkkkkkkkkkkkkkkkkkkkkkkk|{}*E]]{}SB{35}-"
		  "w%{eh})<{-178,}",
		  0, 0 },
		{ "(D(~))", 2, 0 },
		{ "[(:alpha:]{,90}Z|)[:ascii:]Du\?[:grap[^h:]^w+|{}][:ascii:]",
		  0, 0 },
		{ "[:p(\?<=unct:]kkkkkkkkkkkkkkkkkkkk)", 0, 0 },
		{ "{}[:((\?<!dig((\?#it(\?#:]())p))ZZZZZZZZZZ[:blankcntrl:]){}{"
		  "-124,})[:ascii:]",
		  1, 0 },
		{ "[[:graph:]{168}lRRRRRRRRRRRRR(\?#RRRRRRRRRRRRRRRRR)rrrr(\?("
		  "rrrrrr)rrrrrrrS[(\?<!@f)6>{,-49})q${98,}J\?]){",
		  0, 0 },
		{ "([:pu(\?(nc)t:]F{-32,-102}+)\?cpP[:lowerprint:].^)", 1, 0 },
		{ "([{}{210,-238}]1:h)", 1, 0 },
		{ "([]QQQQ[QQQQQQQQQQQQQQQQQQ][:digit:]Z{-20,}Slllllll[:space:]"
		  "C^(@{-174,-156}fx{cf2c}{-242,}rBBBBBBBBBBBBBBBBBBc[:alpha:]"
		  "N\?))$[:graph:][:ascii:]P+nnnnnnnnnnnnnnnnnnnnnnn1N$r>>>>>>>"
		  ">>>>>>>>>>>>>>>>>(>>{,88}{,-234}__________)[:upperword:]R.[:"
		  "alnum:][:lowerprint:]^}\"",
		  3, 0 },
		{ "([^(\?=]-))$", 1, 0 },
		{ "([:ascii:]\?,D[:upperword:][:xdigit:]tttttttttttt[^tt(\?<!"
		  "ttttttttt21f|.(pP[:punct:])])rrrrrrrr)",
		  1, 0 },
		{ "([{1(\?=16}iiiiiiiiii((\?<=iiiiiiiiiiiiiiiiii|ZZZZZZZZZZZ("
		  "\?(\?#{ZZZZZZZ))c}))<<<<<(\?#<<<<<<<<<<<d7CVq8]w{-148,-168}"
		  "\\Gp){-230,}D3",
		  1, 0 },
		{ "[^8888(88888888888EX].[:alnum:]){}", 0, 0 },
		{ "([^][^)2]-[:lower(\?=print:]{,79}[:graph:]n)", 1, 0 },
		{ "[bSi\?x_mp(C)0{64}[:space:]hhh(\?(hhh)hhL){5,130}'w\"$l&[:"
		  "xdigit:][:alpha:]IIIIIIIIIIIIIIIIIIIIIII+-SOOOOOOOOOOOO     "
		  "          (\?( )              ]f)ed",
		  0, 0 },
		{ "[[^[(^(C.Jl[^X&Rb64a+Sd])'m[:alpha:])]]]{134,}", 0, 0 },
		{ "()L", 1, 0 },
		{ "[[(({224,(\?#88})@======(\?!=========(\?{=)PPP)i^@p(\?([:"
		  "punct:]})^^[^^^^^^^^^^^^^^^^^^^^^@)m]|{CS{,-3}168)-[:xdigit:"
		  "][:upperword:]hnD=Bns)z)AAAAAAAAAAAAAAAAAAAAAAA[^A{}"
		  "ccccccccccc)SZ]Q-p.sD]]+P",
		  0, 0 },
		{ "[[^[^]{135,}66666666666666666666[6(666i2M9.!uhmT\?JMm.*(\?!+"
		  ")[:alpha:]eeeeeeeeeeeeeeeeeeeeeeeeeee]]])ZZ[:blankcntrl:][:"
		  "ascii:]",
		  0, 0 },
		{ "(13[3Ux>{,10}[(\?<=:xdigit:]))PL9{-89,-181}F'''''''''", 1,
		  0 },
		{ "[^.|(\?{af]})^$XE!$", 0, 0 },
		{ "(WWWWWWWWWWWWWWWWWWWWWWWWWWWW#J)", 1, 0 },
		{ "({}}M7we-216)L[:digit:][:upperword:]", 1, 0 },
		{ "([:aln[^u(\?=m:]))].z", 1, 0 },
		{ "([:alpha:]{(92})%6{41,136})Vij@[:alnum:][:lowerprint:]", 2,
		  0 },
		{ "[[[++(\?{+++{}})n{{137,}{51,-177}Z[]M*[:ascii:]{(-29,-47}2)$"
		  "e^{,-195}{-156,}^]{}{-225,69}A]{-222,}{,20}m[:blankcntrl:]",
		  1, 0 },
		{ ")l)[:alnum:][:graph:]g8TTTTTTTTTTTTTTTTLLLLLLLLLLLLLLLLL", 0,
		  0 },
		{ "[([(\?<=.(\?{)/})mmmmmmmm(\?(mmmmm]{-154,-176}*S)I]", 0, 0 },
		{ "[(([{(\?(\?<!im(\?imsix:sim(sx:,141}])D)l{,42}ttttt[(\?::"
		  "punct:])){-162,-141}{-26,})dU@@@@@@@@@@@@@@@ "
		  "S)\\A\?w|VVVVVVVVV)X.kN{,21}{-208,-52}>[:lowerprint:][:"
		  "ascii:]e-]]]]]]]]]]]]]]]]]]]]]",
		  0, 0 },
		{ "[^({}(){(66(\?=,}[^]'''''QQQQQQQQQ).P#>^){86,168}Z[(\?<!:"
		  "lowerprint:]{-166,-70}<k",
		  0, 0 },
		{ "APP[:alpha:][:alnum:]nd[:upperword:(\?(]^"
		  "xxxxxxxxxxxxxxxxxxx)xxxxxxxxx{-70}[:punct:]l)U-",
		  0, 0 },
		{ "[^(.\"od~(6({[^(\?<!228}\?)\?)######(\?:#########z "
		  ")c(\?<!aQ`(\?{UKSwu[})][^-17]{11,}}][:ascii:]))^RiH+WyspP["
		  "qi&)=p6])[:space:]{-221,}]6p",
		  0, 0 },
		{ "{-78}()[:xdigit:]{155}{,-92}", 1, 0 },
		{ "[(\?>Q{,147}_____________(\?!______uuuuuuuuuuuuuTr]){74,179}"
		  "{}){,103}{-209,16}*RRRRRRRRRRRRRRRRw{,87}9{144}[:ascii:]'<"
		  "Ab",
		  0, 0 },
		{ "([666c] {-171}yc,8-k_)EEEEEEEEEEEEEEEEEEEEE<", 1, 0 },
		{ "[^(\?>(\?<!)2(\?imim:)6HwN)^|fc!(\?(d]75))065)G", 0, 0 },
		{ "[[^xDB[:alnum:][:xdigit:]][:digit:]jW]([:alpha:])", 1, 0 },
		{ "[ds~T+[x55[:digit:]X[JJJJJJJ.[(\?::upperword:]){,-14}][:"
		  "xdigit:]bbbbbbbbbbb",
		  0, 0 },
		{ "[qqqqq(\?<=qqqq(\?(qqq)^G[):ascii:]])W", 0, 0 },
		{ "[:space:]JJJJJJ[:alph(\?<!a:]|[:ascii:(\?(])[:x)digit:]- "
		  "XSstG[:g(\?>raph:])^)Ny6RF_ndoU9@*rxW{4,41}4{}",
		  0, 0 },
		{ "[:punct:]{162,}j[:aln(um:].....................[^...]\?>z[:"
		  "l[owerprint:]){55,222}]",
		  0, 0 },
		{ "(>vWa)OXcccccccccccccccccccccccc[:alpha:]C{,-10}81|m1D^T)[:"
		  "lowerprint:]''''[:alpha:]l",
		  1, 0 },
		{ "(XZcgM/UI-/"
		  "mZq-222){-85,-196}[:alpha:]{114}rrrrrrrrrrrrrrrrrrrrrrrr{,"
		  "157}ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZLkD-&&&&&&&&&&&&&&&-][:"
		  "alnum:]{}{,111}[:digit:]",
		  1, 0 },
		{ "[^(\?:]MMMMMMMMMMMMMMMMMMMMMMMMMMM)cK["
		  "KKKKKKKKKKKKKKKKKKKKKKKK]P{146}",
		  0, 0 },
		{ "([^[^wqesa)n\?L(\?<=FH+G[^rCGmfD]w)m1D\"%}]])", 1, 0 },
		{ "[((\?:[^.HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH|S)xd)*[:space:](])["
		  ":xdigit:]ngr'G#/B]-----------------------------",
		  0, 0 },
		{ ")[:lowerprint(\?<=(:]l))G p", 0, 0 },
		{ "[^[^(\?<(\?<(=(\?imsximx:![(((\?<!\?(^))\?]^)[:xdigit:][:"
		  "graph:]{-104,})Gf+GD*qc)c]f))])",
		  0, 0 },
		{ "[^([\?())P[:alnum:]w]{-186,-139}-[:space:]RN3w[Fmvpl[:space:"
		  "][:digit:]&&&&&&&&&&&&}(\?#}}}}}}}}}}}}}}}}}}}])z",
		  0, 0 },
		{ "([[^^*C[()f][(\?=:punct([\?#:]o)]V)]%%%%%%%%%%%%%%%%%%%%%%%%"
		  "%%%%%%[^x{1f948})]]",
		  1, 0 },
		{ "[(:xdigit:])zE", 0, 0 },
		{ "[:pu(\?(nc)t:])(a*){-51}", 1, 0 },
		{ "[^(.NKKKKKKKKKKKKKKKKKKKKKKKK-[:upperword:][:space:]`MPi>",
		  -1, 0 },
		{ "Nvvv[vv.][:alnu[^m:]+|Crrrrrrrrrrrrrrrrrrrrr[:xdigit:]j1n)v#"
		  "]",
		  0, 0 },
		{ "[^#}[(\?>:alnum:]).QQQQ[^QQQQQQ!!![!!!!!!!-s.n]se]{-238,}Tf]"
		  "p4721",
		  0, 0 },
		{ "([((\?#\?<=)+)Hr:-H]z[:graph:].{}oooooo(ooooooooo][:punct:]"
		  "k<gXG@@@@@@@@@@@@@@@@@@@{,-176}){}L`)$",
		  2, 0 },
		{ "({,249}{-73,}Z&&&&&&&&Ds35MB<v)qqqqqqqqqqqqqqqqqqqqqqqqq", 1,
		  0 },
		{ "[^.N][:blankcntrl:]))))))))))))))))))))))))))))))", 0, 0 },
		{ "(()*){198,}", 2, 0 },
		{ "{-237,}220{}[:ascii:]```````(`````````````\?{-115,185}){,-"
		  "18}[:punct:]'|Kk",
		  1, 0 },
		{ "[(\?()])", 0, 0 },
		{ "([(\?#[:alnum:]CQ)}}}}}}}}(\?>}}}}}}}(}}}}}\?310[|))xA5r][[^"
		  ":ascii:]^{,-156}{])CCCCCCCCCCC-145]FzwOD_u\?",
		  1, 0 },
		{ "[^[^[]{-163}{(-203}[(\?!:upperword:]PPGjZ[:xdi(\?=git(\?#:]{"
		  "-73}s)qqqq(qqqqqqqqqqqqqqqqqq{173,210}[:xdigit:(\?<(\?>=]WW["
		  "^WWWWWWW\?*O)))Q){}08)[(\?(\?<=#:blankcntrl:]{90,}]U)])L)"
		  "ooooooooooooooooooooooooooox--^c[:ascii:]])s)",
		  2, 0 },
		{ "[(\?!:punc(\?imximx:t[^:]4F<}!)]'M-)tKKKa4904", 0, 0 },
		{ "[^^{}\\(\?<!\\\\\\\\\\\\\\\\\\(\?#\\\\\\\\[:punct:](\?>)"
		  "T000000000(\?(000)00000))+])",
		  0, 0 },
		{ "L[:p(\?#unct:])", 0, 0 },
		{ "[:upperw(\?<!ord:])", 0, 0 },
		{ "@$\"\"\"\"\"\"\"[\"\"\"\"\"\"\"\"\"\"[^(\"\"\"\"\"(\"\"][]))"
		  "*U{223,138}*o```````````````(\?=[```````````````]{238}"
		  "mmmPPPPPPPPPPPPPPP&&&&&&&&&&&&&&&&&&)sF$[:digit:[]]",
		  0, 0 },
		{ "[^#Txx[xxxlPB(\?><[^U/)]]{}X3333333333(3333333f*])", 1, 0 },
		{ "<<<<<<<<<<<<<<<[^<<<<<<<<<.][(\?#:ascii:])[:xdigit:]|^", 0,
		  0 },
		{ "([:punct:]{}){-167,}{-59,}Pd\"", 1, 0 },
		{ "[((\?#{,214})t$)VVV[:xdigit:]{104(\?<=}D][:graph:])|H){1,}{-"
		  "176,}",
		  0, 0 },
		{ "[[([[^N,,,,,(\?=,,(\?#(\?:,,,,,,,,,,,[^,,,,,,,,,,]<,~4::_.A]"
		  "){-52,}-[:alnum:]Pnnnnnnnnnnnnnnnnnn)d",
		  0, 0 },
		{ "{-18(3,})uT{4,}", 1, 0 },
		{ "[^[^[(p+c(\?<!b$))(\?:EU(\?(.][^{}]3[:xdigi[^t):][:punct(\?>"
		  ":])[])][:s[^pace:]][:alnum:][:alpha:]]kw06E",
		  0, 0 },
		{ "[^^^^^^JJJJJJJJ(JJ(\?=JJ(.6[:space:]H]{231,}A^eqqq)[:ascii:("
		  "\?>(])[(\?>:spa(\?:ce:]xxxxxxxxx)@_t-))"
		  "138GNNNNNNNNNNNNNNNNNNNNNNNNNN[:digit:]no!`#E\?&[:"
		  "lowerprint:].)[:graph:]{86,}[:digit:][:alnum:]",
		  0, 0 },
		{ "[:g(\?<=raph:]a{114,146}(){}0Y[:bl(ankcntrl:])D)\?", 1, 0 },
		{ "[^[^]*H{-192,96}S|]G)6B-kLB", 0, 0 },
		{ "[[^[^][/"
		  "NS8`um(\?{82&{((\?{\?<!-[110,-88}]m)})kkkkkkkk$$$$$$$$$$$$[^"
		  "$$$$$@n%BuK@X!P)y0v!^]YY[YYY[YYYYYYYYYYYYYYYYYY///////"
		  "{}{{{{{{{{{{{{{oiiii})]8{-2[53}w{82,}]{,245}]{-134}]"
		  "fffffffffffffffffff]\"I>DW>9tN%{113}{unE",
		  0, 0 },
		{ "[:(\?(alpha:]`))Y2sCqWQ104", 0, 0 },
		{ "(([^()Wcccccccc(\?{cccccccccccccccccc(\?<!c(ccccc[:space:]$)"
		  "(\?>)FZ{}{}`|||||||||||||*````````````````````````````'="
		  "dLQmx/"
		  "Y.A7j'o}jn{}:})][:punct:]$|,-)!&Y:Ys#"
		  "ykL7JJJJJJJJJJJJJJJJJJJJJJJJJ8yex>#mv[:punct:](x@)$[:uppe("
		  "\?<!rword:])_)",
		  3, 0 },
		{ "[[(^HHHHHHHHHHHH(\?imsximx:HH(HHHHHH(\?{HH[HH])qjR>9))i})]a!"
		  "lBW3p{A=or)ShE%[:punct:]{}]5r",
		  0, 0 },
		{ "[:pu[nc[^t:]]]}}}}}}}[}}}}}}}(\?#}])@@@@@@@@@@@@@@@@@@"
		  "DDDDDDDDDDDDDDDDDDD\?]xA2\?",
		  0, 0 },
		{ "(.[:alpha:]xB7[:alnu(\?{m:]})RRRRRRRRRRRRRRRRRRRRRRRRRRRL)[:"
		  "space:]G\?",
		  1, 0 },
		{ "[:blan(\?<!(\?=kcntrl:]){71,})!ooooooooooooN", 0, 0 },
		{ "()e$$$$$$$$$$$$$$$$$$$$iiiiiiii", 1, 0 },
		{ "(b[:ascii:]67777777777777777777777777)({-106}kkk^F----------"
		  "---------------------{13}A)f00000000sBAddddd{-66}kd!D'",
		  2, 0 },
		{ "(Q                        ^])[^lf][:space:][:lowerprint:]\?",
		  1, 0 },
		{ "[[^]\\S{152}W![:digit:][[^:space:(\?(]=pEhwY][:alnum:][:"
		  "digit):][:graph:]])QQIC9h-oowf[:xdigit:]{-52}{,190}"
		  "1111111111111111111fX{-189,226}W",
		  0, 0 },
		{ "[^(\?!(\?<=)]).h[:as(\?>cii:])[:alnum:]$$$$$[:space:]3$$$$$$"
		  "$$$$$$$$$$$$$$$$$$$$$$$$$1",
		  0, 0 },
		{ "[[$zQ================(\?<!=(\?>=========(\?====D[^))|i{}"
		  "\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?)][:s(pace:]])"
		  ")]",
		  0, 0 },
		{ "[^{,-[15(\?#6}]Vwjjjjjjjj[jjjjjjjjjjjjjjjjjjS9999)]q]"
		  "rWWWWWWWWWWWWWWWW[:punct:]@@@@@@@@@@@@@@@@@@@@@@@@gO[:"
		  "blankcntrl:]>L[:ascii:]:::::::::::::::::::"
		  "x11uuuuuuuuuuuuuuuuuuuuuuuuuuuuu{-124,114}[:graph:]C#{tcg[:"
		  "xdigit:]gZZZZ[:lowerprint:]nA(_{{{{{{{{{{{{{{{{{{{{SS)\\D[:"
		  "alpha:]",
		  1, 0 },
		{ "[^(\?())]!T\?[:asc[^ii:]E:4},,]I[^b(\?:n4(njj~+{\?'k{7}{189,"
		  "-194}{ig.[[[[[[(\?#[[[_bs6,JD`1(\?<!WBo]F+{d*VO22z2K1][:"
		  "xdigit:]))Suuuuuuuuuuu[^u{,117}\?YYYYYYYYYYYYYYYYYYYYYYYYB^]"
		  "|q]:eY1GGGGGGGGGGGGGGGGGGGGGGGGGGGGe\?)bU[:punct:]",
		  0, 0 },
		{ "[\?UA(\?:]\?)[:xdigit:]A^mmmmmmmmmmmmmm>>>>>>>>>>>>>>>>>>>>>"
		  ">>>>>>>[^>>>(\?(>)){,-165}]",
		  0, 0 },
		{ "([^[][^n(\?{[[p]#})|][^]L|66666666666[:graph:]][:graph:]2[:"
		  "xdigit:][:space:]9b})[:digit(\?imsximsx::]+PZ):{}|E)[:"
		  "xdigit[^:]|>]^[:alpha:]::::::::[:ascii:]````[:ascii:]:",
		  1, 0 },
		{ "[:lowerprint(\?<!:])", 0, 0 },
		{ "[[^[]{-47}[:lowerprint:][:punct:]L[(\?::g(raph:]lY[:alnum:])"
		  "qWYU)}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}[c%$dp5[:alnum:]DDDDDDDD^"
		  "^%&{,-94}E]{-8,175}[:alpha:]-.^[:digi(t:]CCCC(CCCCCCCCC])."
		  "ax72)",
		  1, 0 },
		{ "[[^($$$$$$$$$$$$$$$$$$[^$I((\?{\?(u)\"YuK "
		  "ZpOHq[!(\?>t|LQT(|)L[(:ascii:])",
		  0, 0 },
		{ "[^[^([:graph:](QpPdyDQ`[:alpha:](.X[:digit:]wwwwwwwwwwwwww("
		  "\?imxims:wwwwwwwe(\?<!z)ONNN(\?#)[^])[:space:](KKKKKKKKK{"
		  "113,}327[:xdigit:]k)]CeeeeeeeeeeeeeeeeeMMMMMMMMMMMMMMMMM)[:"
		  "lowerprint:]]HHHHHHHHHHHHHHHHHHH]]]]]]]]]]]]]",
		  1, 0 },
		{ "[Q(r(\?=)v]dm[:alnum:][:b(\?{lankcntrl:][:xdigit(\?=:])})P[:"
		  "graph:]bd/Rx){50}{-150,-172}",
		  0, 0 },
		{ "[(\?(im(\?:sxims:))9]))L", 0, 0 },
		{ "[[^[(\?{^Z][^0[:alpha:]]\\XB*{-151}t})][:alnum:]]", 0, 0 },
		{ "[([(D\?/////////////////////.'yvYysU&5AU-]kV)*){,123}z]", 0,
		  0 },
		{ "[:alnu(\?{m:][:a(\?=lpha:][:alpha:])n}))7[:ascii:][:xdigit:]"
		  "[:punct:]-",
		  0, 0 },
		{ "[^[:graph:]IIIIIIIIIIIIIIIIIIIIIII][:sp(\?<!ace:])", 0, 0 },
		{ "[[[(\?=[[[cDD(\?<!D(\?:DDDDDDDDDDDD(\?<=DDD(DDDDDD(\?:"
		  "DDDDDDD(\?<=D(\?()])rvp{243,}D$<[:space:]([:lowerpr)int:])])"
		  "Ea{}U[:upperword:][:xdigit(\?#:]or}Z+34gD{/P NJ",
		  1, 0 },
		{ "[^(,H>)*d2K0DNX5)T(].)[:digit:].", 0, 0 },
		{ "([:punct:(\?#])})JJJJJJJJ[:xdigit:]"
		  "PPUUUUUUUUUUUUUUUUUUUUUUUUUUUUUU.......................0hSk{"
		  ",89}[:xdigit:].[:xdigit:]Z",
		  1, 0 },
		{ "(LGTTTTTTTTTTTTTTTTTTTTTTTTTT[:alpha:]){-106,113}[:punct:]d|"
		  "[:digit:]kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk\?wP",
		  1, 0 },
		{ "([^[^<N_-k\?{(\?#18}]i]::::::::::::::::::::::::::)1+LLLLn{}/"
		  "){-198}",
		  1, 0 },
		{ "([[^(AAAAAAAAAA(\?(AAAAA)AAAAf).LzHHHHHHHHHHHHHHHHHHHHH(\?#"
		  "HHHHH|)[ZEEEEE(\?#EEEEEEEEE(\?<!EEEEEEEEsG)q[:punct:]{}][:"
		  "upperword:]D)[:space:][:digit:]+e[:ascii:]].i|JJJJJJJJ+n][:"
		  "xdigit:]Se)P[:lowerprint:]_______________________________.[:"
		  "punct:]pP{-172,86}iiiiiiiiiiiiiiiiiiiiiiiii){,-178}",
		  1, 0 },
		{ "([\?=[[^,BDRRPZ{129}*D-[:punct:]]])([:upperword:]ud)\?][:"
		  "punct:]A",
		  -1, 0 },
		{ "(([(\?#((\?{\?=^])c-)C[:lowerprint:]xvkR}k\")"
		  "ccccccccccccccccccccNNNNNNN[:alp[ha:]{,93}vhlX:|A]2})nSw)]"
		  "N.",
		  2, 0 },
		{ "()g/qzyiV(x3d|A0wllllll){162}[:space:]", 2, 0 },
		{ "qqqqqqqqqqqqqqqqqqqqvvvvvvvvvvvv8[:x(\?imsxmsx:digit:][:"
		  "alpha:]''''''''''''''''''''''''''')",
		  0, 0 },
		{ "({,226}nf^W=vs$xK^=A=M#b,)V", 1, 0 },
		{ "(_T 2BC9N'cccccccccc-87EF#&^eQfDDDn._,m&c`tjAwR "
		  "#~A)[:(\?imsimx:alpha:])/yHYL6|{-40,47}",
		  1, 0 },
		{ "[[^]{-8(4,138})z[:xdigit:]{180,}]", 1, 0 },
		{ "[([^T____________________(\?:__C(\?<=]-)])+[:ascii:])r[:"
		  "graph:].----------",
		  0, 0 },
		{ "[f{}LLLL(LLp((((\?<!((((((((((((((({,56}]BR`{,52}){-22,}\?[:"
		  "space:]h>Sow",
		  0, 0 },
		{ "{-179}^[:alpha:(\?!].a'5wacA3\\\\\\\\AAAAAAAA)~^]wC", 0, 0 },
		{ ">[:digit:]{,-212}+(`)LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL[:ascii:"
		  "][:digit:][:space:]",
		  1, 0 },
		{ "[[^[[^RBW{,255(}(\?(\?>=(W)_]uu][:blankcntrl:])O)]]", 0, 0 },
		{ "(C_______________________________)2", 1, 0 },
		{ "([/ntf_a3].)", 1, 0 },
		{ "[:space:]+[(:upperword:],c7[:asci(\?<=i:]ggggggggggg)[:"
		  "ascii:]/1$$$$$$$$$$$$$$$$$$$$$$$$$$)",
		  0, 0 },
		{ "Xq{109}~EEEEEEEE[:upper[^word:]lgB:X(h[:alpha:]B[:space:]].)"
		  "IkaH@3}}H'yK~\?[:upperw(\?#ord:(\?:]){=================[:"
		  "blankcntrl:])",
		  1, 0 },
		{ "(([[^]]$3Xr^$%%%%%%%%%%%%%%%%%%%%%================U[:ascii:]"
		  ")X).FFFFFFFFFFgO[:punct:]oooooooooooooooooooBC[:blankcntrl:]"
		  "mmmmmmmmmmmmmmmmmmmm[:lowerprint:]rBM~<HAc#Sb&&&&&&&&&&&&&&&"
		  "&&&&&&&&&&&&&&Cy",
		  2, 0 },
		{ "([([([^(\?:)D]-{M#H "
		  ">rERRRRRRR[^RRRRR(\?>RRRRR])[(\?=^)X]{207,}U])))Z[:"
		  "blankcntrl:]]yyyyyyyyyyyyyyyy\?",
		  1, 0 },
		{ "[Q(\?{*[^(\?(\?!!])[:graph:]]})[:alnum:]iE)dGGGGGGG[^"
		  "GGGGGGGGGG[:xdigit:]w]",
		  0, 0 },
		{ "[^Z(\?!6(\?(\?><=)[:graph:])]BBBBBBBBBBBBBBBB^)", 0, 0 },
		{ "[[^([^[^][[[[[[[(\?({[[(\?(\?imsxmsx(\?imsi[ms:::[[[[[[[[[})"
		  ")]$)){12,})|:::::::::::::::::::[:lowerprint:]{}{-96,-147}){"
		  "13,}`[:digit:]]\"^Ca%%%%%%%%%%%%%%%%%%%%%%%%%%"
		  "UUUUUUUUUUUUUUUUUU]]9",
		  0, 0 },
		{ "[^(\?(\?(\?#!<=))JLBS\"zi)'''''''''''['''''''''''''"
		  "piiiiiiiiiiiii(\?<=iiii]])ZZZZZZZZZZZZZZZZZZ[:space:]",
		  0, 0 },
		{ "({})[:punct:]", 1, 0 },
		{ "E9[:blankc(\?{ntrl:]})N", 0, 0 },
		{ "[:alph(\?#a:]){198,}sq\?X0B7", 0, 0 },
		{ "[^\\\\\\\\(\\\\\\[\\\\\\\\\\\\[(\?<(\?isximsx:={11(\?(9,}"
		  "\?0])]]))\?FN3M\?{-128,}Z444444)444fbLiVN8)",
		  0, 0 },
		{ "[[^[^([[[[[[[[[(\?>[[[[[[[[[[[[[[[[[[[[[{53(\?<=,-175(\?>}"
		  "ggggggggggggggggg%))[:alnum:])[:punct:]"
		  "kkkkkkkkkkkkkkkkkkkkkkkkk)+"
		  "Soooooooooooooooooooooooooooooooo](WR+--)x36+llllllllllll{,"
		  "35}]Fqb^=F]KKKKKKaaaaa{,131}",
		  1, 0 },
		{ "(g\"Ssqw<&{Cl{82,}Mdf|9cIlmCW{}[:digit:]4C{}[:alnum:]PP)", 1,
		  0 },
		{ "OOOOOOOU[*evVIIIIIIIIIIIIIIIII(\?#(\?#IIII)]PP[:xdigit:]"
		  "2222222222222222[:xdigit:]Kx)p[:digit:]",
		  0, 0 },
		{ "([[{248,16(\?=5(\?#}][:alpha:])|[:p(\?!unct:(\?(]", 1, 0 },
		{ "[pP((\?=S)(\?#)]$[:aln(\?(um:)]2\?)$GGGGGGGGGGGGGGGGG({-U:c)"
		  "{-61,}[:ascii:]{-202}G",
		  1, 0 },
		{ "()$D[:alnum:]", 1, 0 },
		{ "[(\?#^]){}[:ascii:]", 0, 0 },
		{ "[uuuuuuuuuuuuuuuuuuuuuuuuuuuuuu]FFFFFFFFFFFFFFFFFFFFFF&2e\?)"
		  "%oP'mc@z2b}n{<b4_Laz^0LLLLLLLLLLLLLLLLLLLLLLL,,,d",
		  0, 0 },
		{ "{}(^________________''|$)RRRRRRRRRRRRRRRRRRR", 1, 0 },
		{ "(H)####################bbbbbbbbbbbbbbbbVSSSSSSSSSSS|"
		  "tdU\"goeAbPP{-248,81}",
		  1, 0 },
		{ "[^[(\?ims(\?>xisx:)UHpP*n{}]{}fx14<7OEpE>n2150)"
		  "8888888888888888]^GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGS",
		  0, 0 },
		{ "(d)+", 1, 0 },
		{ "[^.(\?(>)(\?=e)])al[:space:]x", 0, 0 },
		{ "[^256c(\?!]){-19,}", 0, 0 },
		{ "Q)", 0, 0 },
		{ "[^s\?\?(\?{\?\?\?(\?#\?(\?<!\?\?\?\?\?\?\?\?\?\?\?("
		  "\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?{}]F\?j(jjjjjjjjjjjjjjjjjjn)"
		  "kTI1f[{1|(\?<=^[^+[:digit:]{}^s^))})))T]{-17}{CCCCCCCCCCa{-"
		  "21,}{,-146}^uZQB]YuLu-|tUGRMz^^",
		  1, 0 },
		{ "([^.{}.EE[EEEEEEEE(\?<=EEEEEEEEEEEEEEEU]]-@s))$", 1, 0 },
		{ "[^([((\?#[#])|a)])[cccccccccccccccc][:digit:]LLLLLLL[:alnum:"
		  "]}[P%vzl{}^]&",
		  0, 0 },
		{ "({}[:space:]E)101+A{-35,11}", 1, 0 },
		{ "(va:7)u[:alpha:]", 1, 0 },
		{ "([^[[rrrrrrrrrr(\?:rrrrrrrrrr(\?<!rrrrrrrrry|D'*AH@a{}\?[:"
		  "space:][:alpha:]^]$ "
		  "{-225}[(\?(:as)(\?(>cii:])){-107,-139}6/"
		  "{^[:upperw(\?imsxmsx:ord:]{,-47} "
		  "]wuH#nAn)GGGGGGGGGGGGGGGGGr[)]T{91}lJ))[:lowerprint:][:"
		  "xdigit:][:lowerprint:])]*",
		  1, 0 },
		{ "()[:space:]~!$[:alnum:]JJJJ[:ascii:]", 1, 0 },
		{ "[^(\?<=)-]()k", 1, 0 },
		{ "(()W){,8}ea", 2, 0 },
		{ "({,-56}5G&&&&rrrrrrrrrrrrrrrrrrrrrrrrrrk.8) hWJ,TM)0Yd-", 1,
		  0 },
		{ "(Z-fddddddddddddddddddddddd)-{9}", 1, 0 },
		{ "[^<[(\?!:asc(\?:i(\?<!i:])F])[:alp(ha:]b))-}Wwx8B", 0, 0 },
		{ "[^[^[^([(\?{}(\?=)(\?())-CCCCCCCCCCC(\?=CCCCCCCC(CCCCC(\?:"
		  "CCCCCCCC(\?{l[(\?!:space:]})[:upperwor(\?:d:]{-27}[:al[^pha:"
		  "][:xdigit:]^f",
		  0, 0 },
		{ "[[^]G@>2!+[:punct:(\?<!]{,189}6ZF[:blankcntrl:][:digit:]{,"
		  "214}){-115,-14}l[:upperword:]{101,}Z[:ascii:]Ld&02|c]<0~<bc",
		  0, 0 },
		{ "(Q)[:digit:]x", 1, 0 },
		{ "hT[[:alnum:]\?]O[OOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOxFF%^(\?(_"
		  "LN "
		  "8uXQT\"*/"
		  "L)+l)>qQ[^]e[:ascii:]PP()[:digit:]NQ8%6d=&2I{-62,-142}w]].e{"
		  "}*",
		  1, 0 },
		{ "{,-219}xxxtEEEEEEEEEEEEEEEE[:pun(\?(ct:])qqq)"
		  "nnnnnnnnnnnnnnnnnnnnnnnnnnn",
		  0, 0 },
		{ "[:di(\?>git:])W4", 0, 0 },
		{ "([^y])Fkvto$", 1, 0 },
		{ "[^($$$$$$(\?!$$$$$(\?{$$$$$$(\?<=$$$$$$$$$$$+===)[:alnum:]"
		  "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM)Z]{}^[:blankcntrl:]--"
		  "xxxxxxxxxxxxxxx[^xxxxxxx)\?tVG\?{232,81}{121,}xn{,-226}})"
		  "tttttttttttttttttttttttmu(\?<!&&&&&&&&&&&&&&&&&&&&&&0b]z)$"
		  "87{,-192}{}{-242,}",
		  0, 0 },
		{ "l[:dig(\?(it:]|s*)aA[:digit(\?<=:].^.))x[:digit:]", 0, 0 },
		{ "[:grap[^(\?#h:]').]Z", 0, 0 },
		{ "[:gra[^ph:]t[:digit:]222222222222(22222222222222222H "
		  "qM]pWZr[:ascii:]-hRb_.)Q{-228,-204}{}",
		  1, 0 },
		{ "AAAAAAAAAAAAAAA(AA)YeX", 1, 0 },
		{ "(!dqqqF*^){(,-79}s!!!!!!!!!!!!)", 2, 0 },
		{ "[^(\?msxm(\?#sx:]|)ZHYup)j{95}0L:vXB#')d'DX\?m."
		  "T034\\\\\\\\\\\\\\\\\\\\\\y5rV{}S",
		  0, 0 },
		{ "(W*O+yl([\?!P(\?:)I]${}{-195,-14}[:upperword:]{}[:xdi[^git:]"
		  "[:space:]X[:grap[^h:]~]zzzzzzzzzzzzzzzzzzzzzzzL)+)Y "
		  "b.-=jf{-216,}${/!}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}|]",
		  2, 0 },
		{ "[^\\\\\\\\\\(\?<=\\\\\\\\\\\\\\\\m]{-48,234}[:alpha:]s)", 0,
		  0 },
		{ "[(\?{U}(\?<!)])LLLLLLLLLLLLLLsssssssssssssssssssssssssss[:"
		  "ascii:][:blankcntrl:]---------b",
		  0, 0 },
		{ "[^[^[(\?#)(\?imsxims[x:)<<<<[<<<<<<<<<<(\?<!<<<<<<<([^\?(<<<"
		  "<<<<<<<z(\?(zu(\?<=~83}aZpIE)[:alnum:](\?imsximsx:(\?!jrE6("
		  "\?<!\?V(SzDU)000[000000000((\?=\?)=0])L|lOYuWXk",
		  0, 0 },
		{ "$o[:dig(it:]nnnnnnnnnnnnnnn{-94}|G)[:alpha(\?!:] "
		  "{,-108}D=\?>[:digit:]S[:space:]t",
		  0, 0 },
		{ "()n", 1, 0 },
		{ "[:upp(erword:]$)<}.vZM<lEY5Y*", 0, 0 },
		{ "[^([^\?>)rCD&{5(\?msxisx:7,}qqqqqqqqqqqqqqqqqq{31,}@w#W:(@("
		  "\?:zp$YYYYA[:alpha:]{1}A)*dZJ\"5OG|\?(\?#a])]|){-150}[:"
		  "xdigit:]",
		  0, 0 },
		{ "[($)gwo{`\"]{-160,}"
		  "\\\\\\\\\\\\\\\\\\\\\\\\\\66666666666666888888888888",
		  -1, 1 },
		{ "((}DA+Rc000000000000000000)%vvvvvvvvvvvvvvvvvvvvv%C&emZ*[:"
		  "alnum:]#m/"
		  "D[:graph:][:blank[^cntrl:]E{,168})"
		  "kkkkkkkkkk000000000000000]",
		  2, 0 },
		{ "[^[u*(\?#x01234)oxGGGGG(\?([GGGG)GGGGGGGGG]^U)!!CCCCBM`4QB^"
		  "XEN]{,-60}[:upperword:]G]",
		  0, 0 },
		{ "(%)~t{S,K^MI3PMo)=b", 1, 0 },
		{ "[[[^]{}eU([:xdigit:]&&&&&&&&&&&&&&&&&)\"W|43[:alpha:][:"
		  "graph:]J8b[:blankcntrl:]gggggQ{,183}{,-254}\?[:ascii(:]{,"
		  "134}",
		  1, 0 },
		{ "[[([^[^([^(\?=)1RRRRRRRRRRRRRRRRRRRRRR(\?:(\?(\?(\?!=#RRRRR("
		  "\?=RRRR(\?<[^!Ru)])]o[:[graph:[^]{,7})[:digit(\?::]{-215,}e["
		  ":space:]]",
		  0, 0 },
		{ "({{{{{{{{{{{{{{{{{{KKKKKKKKKKKKKKKKKKKKKKKKKKKKBBBBBBBBBBBB)"
		  "[:space:]0[:alnum:]HcctQA",
		  1, 0 },
		{ "[^(pP7(HsN[^g{186,-87}\?\?]EQ%u:-Y)+>>>>>>>>>>>>>>>>>>>>>pP]"
		  "[:alpha:]",
		  0, 0 },
		{ "[(.{141}h|)((\?:\?=@Q} "
		  "ghcC{+*(R)D+][:lo(\?#werprint:]"
		  "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzz))",
		  0, 0 },
		{ "[^({}S)PPFl(])-216", 0, 0 },
		{ "[([[^(((([(\?#^[^[^\?4[(:[dig[^it(\?(:]{122,})y\?", 0, 0 },
		{ "[[2${188}u{1(4(\?(1,1(\?{98}e{&tbaoI]q)[:punct:])d}))"
		  "Nqffffffffffffffffffffffffffff[:ascii:]+]",
		  0, 0 },
		{ "()K-", 1, 0 },
		{ "[[{2(2((\?(\?!()2})])[:alpha:]fVVVVVVVVV{-47}):::::::::::)"
		  "\?vwyyyyyyyyyyyyyyyyyyyyyyyyy-]{}",
		  0, 0 },
		{ "ivcs)g", 0, 0 },
		{ "(hhhh[^hhhh(\?{h\?]})%%%%%%%%%%%%%%%)\"+38mbY:s9{/d# "
		  "zaNnbQb)b:*zpKI{-26,-189}",
		  1, 0 },
		{ "S*(#)[:graph:]lllllllll&G)t", 1, 0 },
		{ "([^[(([\?=\?<!)]]___{-63,})]nt", 1, 0 },
		{ "[:b(lankcntrl:][:alpha:]*[:pu[^[nct:][:alpha:]A]$"
		  "aaaaaaaaaaaa*)A[:digit:]U][:alnum:]",
		  0, 0 },
		{ "[^f[^p000{68(\?isxmx:,}(\?!vvvvvv)$)]PP#*{(})[:punct:]&&&&&&"
		  "&&&&&&&[:punct:]\?][:blankcntrl:]",
		  1, 0 },
		{ "[^(((\?(\?(()))GGGGGGGGG{(\?!($)))((\?!)V^{228,145}))]{-229}"
		  "Qjjjjj[:punct:]R)",
		  0, 0 },
		{ "[(Q[^((\?{(\?:]~z)})gE(.<){}|)Kuuuuu$*"
		  "222222222222222222222D]",
		  -1, 0 },
		{ "([^`(\?<=`````[^`````````M]\?)=L74A[:upperword:]]P", 1, 0 },
		{ "(({}[:space:]qv-T){,-192}{-45}{65}9\?X).d", 2, 0 },
		{ "_[(:upperword:]mU(P}qX>\?%)$Lwq[:alpha:]{-115,}============="
		  "==================={127,}",
		  1, 0 },
		{ "e)", 0, 0 },
		{ "[{,2[5}Klen+D0'YX(\?<=|_H]I,Y\"*/<3sM[:digit:]])#.", 0, 0 },
		{ "[:(xdigit:]){[:digit(\?mxmsx::][:as(\?<=cii:]d!{135})#)pP[:"
		  "space:]Syyyyyyyyyyyyyyyyyyyy\"Gg8",
		  0, 0 },
		{ "[(\?()])", 0, 0 },
		{ "[^([^[^[[^[:alpha:]SIus[^f<f]}}}}}}}}}}][:xdigit(\?=:]Z{-13}"
		  "*]_[]LLLL)]E[:alnum:]b$)]]]]]]]]]]]]]]]]]]]]]]]]][:"
		  "lowerprint:][:ascii:]{,40}{86,}"
		  "333333333999999999999999999999999999*"
		  "fffffffffffffffffffffffff99999999U9|[:digit:][:upperword:]"
		  "oowwwwwwww[wwwwwwwwww{195}[:xdigit:]]H{-73,153}R+zAz{}r/////"
		  "////////"
		  "{232,}kAoffffffffff[:blankcntrl:]xxxxxxxxxxxxxxx]KKKKKl0,[:"
		  "alpha:]|{,-165}Qc{96}CCCCCCCCCCCCCCCCCCCC/",
		  0, 0 },
		{ "{}:V(7O-)[:ascii:][:graph:]PPPPPPPPPPPPPPPPPPPPPPPPPPPPPPP#",
		  1, 0 },
		{ "[^(\?<[^=CC(CC$)]*        c)BBBBBBBBBBBBBBBBBBBBBBB]z{-18,}",
		  0, 0 },
		{ "[[qqqqqqqqqqq(\?(qq235|ttttttttttttttttttttttttttttt[[ttt<<<"
		  "<(\?{<<<<<<<<<<<<)<<<<<<<<p)/"
		  "S9(\?{OOOOOOO(\?<!OOOk)})]nIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIb]"
		  "Z})",
		  0, 0 },
		{ "[^[^(\?>][^((\?<!C(\?!+(\?=)]^8)6nx).)){,-13}[:blankcntrl:]"
		  "\"(L{}){,29}nnnnn{-83}]l[:upperword:])",
		  1, 0 },
		{ "[(ZZ\"#(\?#Nb(\?<!:U)oRRRR])Zei${Ec/)s", 0, 0 },
		{ "[^[^[(\?(t(\?:3```````)`````)|#CB)//////////////////////////"
		  "///"
		  "*!liB#|CCCCCCCCCCCCCC(\?=CCCCCCa7N]weTTTTTTTTTTTTTTTT1{}o\?{"
		  "}BBBBBBBBBBBBBBBBBBBBBBBB.])u{-218,126}.,[:space:]]",
		  0, 0 },
		{ "[[([:alnum:])yyy(\?!yyyyyyyyyy(\?!yyyyyyyyyyyyyyyyyyy[:"
		  "graph:]I])Uw*X.^[:ascii:]{,-63}[:digit:]{-88})&&&&&&&&&&&&&&"
		  "]*",
		  0, 0 },
		{ "[[[^K(\?=KKKKKKKKKKKK(\?:KKKKKKKKK[KKKKKK]]U[:digit:])]dd)({"
		  ",16})xy+Pu)JJJJJJJJJJJJJJJ[:space:][:ascii:][:upperword:]ql_"
		  "jywmt4B+]{-30,}^555555555Xza[:punct:]",
		  1, 0 },
		{ "[[^^XXX(\?:XXX((XXXXXXXXXXXXXXXXXXXX)v)$N9$"
		  "r\"\"\"\"\"\"\"\"\"\"\"\"\"].{,239}$[:punct:]\"9999][:alpha:"
		  "]{}c){,55}s[:upperword:][:xdigit:]310",
		  0, 0 },
		{ "[@([^I8oNl)]-{-203,-224}{-78,}KKKKKKKKc{-66}[:xdi(\?=git:]=="
		  "========){}f{-124,}[:upperword:][:lowerprint:]]{}--------l+",
		  0, 0 },
		{ "[^]ozp+0(\?#\"[(\?()X]))[:blankcntrl:][^e{99,222}"
		  "JJJJJJJJJJJJJJJ3F]\?[:blankcntrl:]l$ot",
		  0, 0 },
		{ "[[^[[((\?isximx:)2222222222(\?=22222[:graph:])+U)((\?{\?<=("
		  "\?()iYv8qc@#y)G])+}))FvnP\"7OZ-b273[:ascii:]Ak6*`S[:digit:]["
		  ":graph:]]{2}^G{79,}DDDDDbbbbbbbbbbbbbbbbbbbbbbbb(bbbbbbb)|"
		  "tP48y{wNJ_S hJbY]]dc",
		  1, 0 },
		{ "[:alph(\?{a:]p1[:lowerprint:]}){163,}", 0, 0 },
		{ "W()", 1, 0 },
		{ "()``````````````````````````[:ascii:][:alnum:]{,26}[:graph:"
		  "]",
		  1, 0 },
		{ "[:al(\?<!num:]|byyy,*)U5%u${190}-{-221,-33}"
		  "k7777777777777777777777777777777+eXXXXXXXXXXXXXXXXX[X(\?(XX)"
		  "XX)S'vEAa]*e",
		  -1, 0 },
		{ "[^(([R_AC[lE'{2(\?{28(]8LTt[]b[:punct:]]O)|2[:graph:][:"
		  "space:]})                               "
		  "x3C[:alpha:])uI+dddddddddddddddddddddddd{-165,}"
		  "FFFFFFFFFFFFFFFFFFFFFFF)cccc*[:upperword:]]G{,-38}{24,}"
		  "555555555555555555555555555VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVZ["
		  ":blankcntrl:][:ascii:]",
		  0, 0 },
		{ "[^QQQQQQQ(\?#QQ(QQQQQQ[:punct:][:space:]){(\?(\?:!}[:graph:]"
		  "t}}[^}}(}}}}}444444[^444444444444444444444]\?]G)E)L{,-103}{"
		  "84,}r$ii]-[:alp(\?<=ha:]S5G~9>n*)P<"
		  "3tttttttttttttttttttttttttt)n{}[:graph:]"
		  "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeee{,83}[:digit:])"
		  "0BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB[:alpha:]{-155,}{151,}",
		  0, 0 },
		{ "Ue{,254}+f[:lowerp(\?<=rint:]U.fff)", 0, 0 },
		{ "QQQQQQQQQQQQQQQQQQQQQQQQAY<J)'MPi_u%#2doopqU7/"
		  "{103}[:graph:]e!7{GOr",
		  0, 0 },
		{ "[^({,[^233}[^d)BBBBBBBBBBBBBBB=======(\?>===========[^=S|[^["
		  ":alpha:]G/]qqqqqqqqq{}[:xdigit:])..k",
		  0, 0 },
		{ "[([^[[:space:]ffffff(\?=ff]M]))[:xdigit:]UbCI,CzalLU*y5I[:"
		  "digit:]r{-30,180}{-209,-45}Paf]",
		  0, 0 },
		{ "[^[h(\?{hhhhhhhhhhhhhhhhhhhhh})]{,143}[:lowerprint:][:ascii:"
		  "((\?(\?=])[:asc)ii:])zp]",
		  0, 0 },
		{ "[[(\?{]})]", 0, 0 },
		{ "[[1\"3m^,(\?<!2((\?!\?#t```````````````````````````)\?)|c^)"
		  "A^~]{61}W\\\\\\vvvvrrrrrrrrrrr[:digit(\?#:])]F[:upperword:]"
		  "dX\\\\",
		  0, 0 },
		{ "([${144,}(\?<!)-RAk_F(\?imsxisx:=9]z/))", 1, 0 },
		{ "[[^[[[^([[^[^[^([[^([[Uiiiii#####(\?(\?{(\?<!#########(\?=##"
		  "###).^)(.|>2m[M/"
		  "2222222222222222222222222222(\?:22222222222(\?#22(\?:(\?="
		  "22222{,243}]x68+I/"
		  "K)11111111111]\\pP[:graph:]$[:space:]^{}A)[:xdigit:]-={>",
		  0, 0 },
		{ "[(\?>[(^()Vty2vvvvvvvvvvvvvvvvz^])ZZZZZZZZZZZZZZZZZZZ-------"
		  "---------5\\dVLSp8UE2m+z3X/Sd",
		  0, 0 },
		{ "[}}}}}}}}}}}}}}}}}}}(\?#}}(\?<=)|*C "
		  "]*29JW7O9mEB]pE_OoxN)[:alpha:]",
		  0, 0 },
		{ "([^((\?<=\?)D{,200}.[(\?#:ascii:])[:space:].)[:alpha:]D|[:"
		  "graph:]{,-41}*LLUUUUUUUUUUUUU{-189,-131}]qHR<k2@P{27}<^e,ub%"
		  "\?/4){-243}+[:digit:]%*x9lA^",
		  1, 0 },
		{ "([:alpha:]bT&+_)$Z{,212}x26`", 1, 0 },
		{ "[^([^(A{[^}g(\?()A9p#54b]-------------------------------)."
		  "wzD#=f\\)A)8a]]DNNNNNNNNNNNNNNNNNNNNNNNNNN",
		  0, 0 },
		{ "(W000000000000000000000000000000)", 1, 0 },
		{ "www(wwwwwwwwwwwww)", 1, 0 },
		{ "()555555555555{18}i+[:alnum:]E  {}U", 1, 0 },
		{ "SqbHoooooooooooo[^oooooo([^ooooooo])\\N[:xdigit:]]oooo`", 0,
		  0 },
		{ "[999999999999999999uE{193,0}lx{7917}[:punct:]4&d]{221,}[:"
		  "digit:]{49,156}[:lowe(\?<=rprint:])[:space:]{-33}w+",
		  0, 0 },
		{ "[^(\?{})<{220,-193}[(\?=:xdigit:]UUUUUUUUUUUUUUUUUUU'{-18}]"
		  ")",
		  0, 0 },
		{ "b[(\?<=:upperw(\?{ord:][:digit:]})EEEEEEEEEEEEEEEEEEEEE/////"
		  "/////////////){177}C",
		  0, 0 },
		{ "(^).[:alnum:][^[(\?=[(\?{[})DA5{)[[I~y&O\?9>])]][:"
		  "blankcntrl:]M[:alpha:]x9[:upperword:]|[:xdigit:]b",
		  1, 0 },
		{ "()[:digit:][^[U}-]]{,206}V*WJ@R]\?", 1, 0 },
		{ "[^](\?#{}(\?[<=)yv)]r", 0, 0 },
		{ "({,-192}//////////////////////7!eW_0eoL){}", 1, 0 },
		{ "^[:punct:(]+)IIIIIIIII[:punct:]P$pP", 0, 0 },
		{ "[(\?=|U)^-]{-52,-72}[:digit:]*6666666666\?{{{", 0, 0 },
		{ "([^f(\?:+{1((\?=34,}]))^)s0bux7\?5`Bwr[:upperword:])Dy+", 1,
		  0 },
		{ "AL{}:::::::::::::::::::::::::::::::{,(104}~@,Ysey@h).", 1,
		  0 },
		{ "[^((.)))(\?()))))))))))))))))))))(\?msxims:))))))))))[)][:"
		  "upperword:][:alpha:])",
		  0, 0 },
		{ "[^(()f])G^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^T{}N*nK[G]{,61}^^^^^"
		  "^^^]",
		  0, 0 },
		{ "[(N::(\?<=[:digit:][:graph:][:space:]xB5[(:xdigit:]|Yv{}"
		  "HHHHHHHHHHHHHHHHHHHHHHHHd).[:g(\?<=raph:])[:digit:]<<)[:"
		  "digit:])[:space:]Q[:punct:]x7C]",
		  0, 0 },
		{ "[^((\?(\?(())a)(\?!){})W)pP3333333333("
		  "33333333333333333333hhh]{})",
		  0, 0 },
		{ "[^                [               "
		  "a*FFFFF[^FFFFFFFFFFF(\?<[^!FFFF(\?=FF])])L1]{,-52}{B-bxsPKg{"
		  ",8}[:digit:][:punct:][:upperword:]DD${,-131}",
		  0, 0 },
		{ "($$$$$$$$$$$$$$$$$$$$$$$$$$$$$^pP),,,,,,,,,,,,,(,,,,,,,,,,,,"
		  ")QQQQQQQQQQQQQQQQQQQQQQQQ",
		  2, 0 },
		{ "[:lowerprint:]|l{(,-54}C{}*-)IIIIIIIIIIIIIIIII", 1, 0 },
		{ "()+", 1, 0 },
		{ "[(([(\?{[:punct:]]|))[[[[[[[[[[})]WWWWWWWW&$$$$$$$[:graph:]",
		  0, 0 },
		{ "[^(\?{}){(107[(^,}][:space:[]))^w,&aPPPPPP[^PPPPP{117,-213}"
		  "s\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?]]]222222[:d(\?("
		  "igit:]NNNNNN)NNNNNNNNNNNNN8)I",
		  0, 0 },
		{ "[^(\?<!$)|TTTTTTTTTTTTTTTTTTTTTT(TTTT]a8)2<", 0, 0 },
		{ "([^[]%[^[^]-][:alpha:]37*:[:space:]]lQvu)[:xdigit:][:"
		  "blankcntrl:]",
		  1, 0 },
		{ "[[Bl_>9C^:\?X_KK]2sw@hHZT!],uuuuuuut|lFW()''''''''''''''''''"
		  "'''[:graph:]<~v{-251}0[:digit:]C[{222,}]{,41}{}*g^UuS/"
		  "{-114}",
		  1, 0 },
		{ "(D{,-79}[:gra(ph:(\?(]C[:ascii:]))I[tC.%tkllll[^"
		  "llllllllllllllll]&&&&&&)&&&&&&&&&&&&&&&&&&&&&&)]10435",
		  1, 0 },
		{ "[:al(\?{[^num:]]})}x'[:(\?#xdigit:])"
		  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxKKKKKKKKKKKKKKKKKKKKKKKKKKKKTT"
		  "Tr*%{~f",
		  0, 0 },
		{ "[ZQKEEEEEEEEEEEEEEEEE(\?<!]3|.~~~~~~~~~~~~~~303)"
		  "33333333333333333",
		  0, 0 },
		{ "(-62([:ascii:]5555){-230,}<<<<<<SM[:punct:]{72}|E{160,})"
		  "Pfqba!{,-188}DS{ +2tRu\"0JG$",
		  2, 0 },
		{ "([^(\?:(Ea00000000000000[:punct:][:graph:]{}]))[:xdigit:]{-"
		  "65}t){164,}",
		  1, 0 },
		{ "[\?$$$$$$$$$$$$$$$$$$$$$$$$$F......(\?(.).q#R:j6%TTLCdtuM|8*"
		  "54<GHoqEh9FBW0:W]L0)o][:upperword:]",
		  0, 0 },
		{ "[(\?>[:alnum:]W[:space:]]D)|L", 0, 0 },
		{ "(M(MM)[:alnum:]|[:lowerprint:]4)", 2, 0 },
		{ "[[^(\?:{}{2[2(\?>0,})]]]Etu)-)", 0, 0 },
		{ "([^[^^z[:graph:]]#{-144,96}[:punct:]!4LY//////////////////"
		  "SSSSSSSSSSSSSSSSSSSSSSSSS[[^:xdigit:]\?`-!L#p0{52}]%{-121,}["
		  ":graph:]]WqJ>$6UBg{,7}[:blankcntrl:])[:upperword:]y2wW!A[:"
		  "blankcntrl:]0CN\?",
		  1, 0 },
		{ "[[^(\?:|+bII(IIIIIII(\?(\?>!)275SIIIIIIIIII(IIIIIII(\?="
		  "IIIIII[:graph:]|)`]S\?.}A)[:alnum:]Jgggggggggg{-150,}{-89,})"
		  "[:alpha:]Q)|07be5:j)]",
		  0, 0 },
		{ "([(\?i(ms(\?=x-x(\?>:))C)]){})>eIqm~lFb[:upperword:][:"
		  "blankcntrl:]w=[:digit:][:graph:]",
		  1, 0 },
		{ "([HHHHHHHHHHHHHHHHHHHHHHHHHH[^HHH("
		  "\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?!!!!!!!!!!!!!!!!!!"
		  "!!{23}]~J=[:ascii:]tttttttttttttttt])-216",
		  1, 0 },
		{ "B{[^-32,246}{13(\?!0}q>GVQw*[:digit:][:punct:]."
		  "77777777777777777777`T(-t01odD]\?${}{-247}+gV{131})+[:"
		  "lowerprint:]m/z~d",
		  0, 0 },
		{ "[t[$FV+(\?=E=[^])]-$U{-22[5,}{253,}08g]$[{}][:xdigit:][:"
		  "punct:]{-18}{-173,}]{,-191}V_|90",
		  0, 0 },
		{ "()$", 1, 0 },
		{ "[^[^((((((((((((((W[(\?::blankcntrl:]&-JH]J){93}LLLLLLL|r{,"
		  "221}tY/172]-AS",
		  0, 0 },
		{ "[^()(\?{qqqq(\?msimsx:qqqqqqqqqq3999999999999GGGGG|S*W%{,"
		  "128}][:xdigit:]AJt]}\"Zf!lRpr{>){,36}})",
		  0, 0 },
		{ "[([]^]^)", 0, 0 },
		{ "([.(\?#){}[:alpha:]\?S{2}P%Gw]"
		  "nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnYiq5)>i*r<",
		  1, 0 },
		{ "[ggggggggggg$PPP:S "
		  "(:]N{239,}|A[:lowerprint:]vvvvvvvvvv[:lower(print:]{-184}({-"
		  "133,}+)[:punct:]P/Q.OOOOOOOOOOOOOOOOOOOOOOOOOOOOOO",
		  1, 0 },
		{ "(RRRRRRR[^RRRRR[RRRRRRRRR])]", 1, 0 },
		{ "[(\?:^])D%", 0, 0 },
		{ "()[]#C[+[j]{,29}-]", 1, 0 },
		{ "(([(\?(((\?{\?!(\?=\?=#[Es*){02$r'}(\?:3pz)"
		  "uPPPPPPPPPPPPPPPP(\?(\?>:PPPP][:graph:][:ascii:]`.)[:punct:]"
		  "[:a(\?mxi:lnum:])r)$)[:xdigit:]$[:(\?=digit:])aa[^]a)\?])"
		  "sQQQQQQQQQQQQQQQQQQQQQQQQQQ^|$)-}))",
		  2, 0 },
		{ "z@@@@@@@y${}[:(\?:upperword:]l\?{,144}-)", 0, 0 },
		{ "[:aln(\?:(\?>um:(\?imximsx:]){})FGGGGGGGGGG|-p){,105}", 0,
		  0 },
		{ "[[{17}llllllllllllllll(\?:lllllllll{,(\?#-94}OUUUUUUU(\?#"
		  "UUUUUUUUUUUUUAA]p[:digit:]{-1(57,}5yyyyyyyyyyyyyyyyyyyyy[:"
		  "alnum:]v{-185}^^^^^^^^^^^^^)d[[[p)]))",
		  1, 0 },
		{ "()|[:digit:].E2o", 1, 0 },
		{ "()3[:lowerprint:]", 1, 0 },
		{ "[(\?{(\?#(\?>SN}[^)z+r^t[:digit:]seP[:alnum:]$b1ZY[U(\?<!"
		  "U4IIIIIIIIIIIII(\?<=IIIIIIIIII]m)]))]4)",
		  0, 0 },
		{ "{,74}      qkk[^p]kbi6>{}000000000000000000000000000000$|)",
		  0, 0 },
		{ "[:(\?=digit:])v{164}", 0, 0 },
		{ "[:graph:]h[:upper(\?(wo(\?{rd:)])00000[^000000000000})."
		  "4OEVf{,-46}]A",
		  0, 0 },
		{ "[](((((((((((((((N{{{{{{{{{{{{{{{{,-1}e]a{-166,-44}", 0, 0 },
		{ "([[^[^[(^[]]YYYYYYYYYYY]D.cQ{}[:alpha:]ttttttt000000[^0000("
		  "\?<!0000000000000000N::::::::].][:alpha:]#5\?{}{-253,-193}]"
		  "\\[:ascii:]tS{,35}B)ffffffffffffffffffffffff))/",
		  1, 0 },
		{ "(G)[:alpha:(\?#])W{-197,-220}w8", 1, 0 },
		{ "{-2[^00,(\?#-([84}ig+)]]l[:graph:][:graph:][:space:])"
		  "aaaaaaaaaaaaaaaaaaa{-208,}ea{,224}",
		  0, 0 },
		{ "[^[W(\?<=[B[:xdigit:]{255,}FAAAAAAAAAAAAAAAAAAAPP])[:xdigit:"
		  "]+][:lowerprint:]${-195}",
		  0, 0 },
		{ "[v{104,}BB].HHHHHHHHHHHH[:ascii:]"
		  "bbbbbbbbbbbbbbbbbbbbbbbbbbbb(btttttttttttttttttttttttttt){"
		  "180}",
		  1, 0 },
		{ "[^(i[^iiiiiiiiiiiiiiiiii(ii)n])#######################]", 0,
		  0 },
		{ "(([:space:])[:g(\?>raph:])[:punct:][:upperword:]LV\"t+t!)[:"
		  "ascii:][:lowerprint:]q",
		  2, 0 },
		{ "[[[^([7(\?[<!)\\PP~D7L        (\?imsimsx:(\?=  "
		  "$GS26L3-J(\?()!)]]{-178}%$[:p(\?!unct:]))yyyyyyyyyyyyyy@w,["
		  "11!R86:)G*[(\?(:blankcntrl:]267$~L\?{-108}k[:alnum:]So\?Y/"
		  "eq]-|[:xdigit:]555555555555555555555555555)55555........W*O)"
		  ")][:alnum:]]I{,-126}[:lowerprint:]8\?[:xdigit:]u%wHc6\?:Pc.."
		  ".........................,,,,,,,,,,,,,,,,,,,,,,,,,,,]",
		  0, 0 },
		{ "((3pPp))QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ", 2, 0 },
		{ "[[^]{-244[}(\?([^|W0E4]UUUUUUUUUUUUUU[:upper)word:][:space:]"
		  "{-57,})+L>R]]$PeFuufcBA`qr!!!!!!!!!!!!!!!!!!!!!!!!!",
		  0, 0 },
		{ "[[(\?#F^(\?<!)|)fff(\?!fffffffffffffffff(\?{ffffff(\?:"
		  "ffffff[:alnum:])]]c.\?-}))",
		  0, 0 },
		{ "[^[^((\?:)ww[wwww(\?>wwwww)3z/57z){34}]/(/////////////[^////"
		  "//////////////)]E%)L{-133}]*$]",
		  1, 0 },
		{ "(!)GS[:ascii:][:punct:]{235}T'&-_h\"", 1, 0 },
		{ "(){}", 1, 0 },
		{ "[[^((\?!(\?<=)*QF[:alpha:])([^[^\?<!x60t(\?<!"
		  "UUUUUUUUUUUUUUUUUUUU)K&d{118}z7nM.G)````````````````````````"
		  "```E:(\?(){31,}){}]k]){,109}[:space:]]ZZ[:xdigit:]]{-68,}`{}"
		  "{}e\?[:alnum:]",
		  0, 0 },
		{ "[^{223}.^,-qqqqqqqqq((\?!\?>qqqqqqqqqqqqqqqqqqqqqqqP6W0_'O)"
		  "Bur*'6&*t)]{65})+",
		  0, 0 },
		{ "([(\?=)]wr$7f5ru){100,}[:xdigit:]y{}[:digit:]{}2n@P|9#mru~"
		  "97{-189,73}$a",
		  1, 0 },
		{ "({-113,213}){-172,221}B[:ascii:]{,-48}", 1, 0 },
		{ "[^[[Xf`````((\?{(\?<=\?imsmsx:`````````(\?!`````````[```("
		  "\?mximsx:``(\?(&|o{xIaO][:)space:]3))\?])+)*<|@@@@@@@@@@@@@@"
		  "@@@@@@@@){-251,}{}]*[:graph:]1!azE\?|-120u*][:lowerprint:]}"
		  ")",
		  0, 0 },
		{ "[[[^##(\?################(\?>(\?(##t)][:punct:])b))<<<<<<<<<"
		  "<<<<<<<<<<<<<<<<<[:alnum:]y "
		  ">u=l:rp8i3Ci#]46%NIO-W[:space:]IIIIIIIIIIIIIIIIII]W[:space:]"
		  "f]l{-253}",
		  0, 0 },
		{ "[:graph:]L{-136,175}{[^}h(\?=t)Q]ooooooooo("
		  "ooooooooooooooooo_)[:space:]q\?",
		  1, 0 },
		{ "()$.", 1, 0 },
		{ "[(\?<!^$.\?{197}B]$)", 0, 0 },
		{ "[:di(git:])[:low(erprint:])qqqqqqqqqqqqqqqq[:digit:]", 0,
		  0 },
		{ "((zzzzzzzzzzzzAUUUU)l$]VD                 z~)n", 2, 0 },
		{ "([^[(\?<=^[]{}][.WWWW)044444444444(\?=44(\?{444(\?{("
		  "444444444444e{(\?=}}))..t]+[:(\?<!xdigit:]P]-N}))))|)",
		  1, 0 },
		{ "\\ce[:(\?#asc(\?{ii:])})[:upperword:]`^", 0, 0 },
		{ "[:graph:(\?<=])[:alpha:]", 0, 0 },
		{ "([:upp(\?=erword:])pC)lp\?", 1, 0 },
		{ "(oooooooooooooo\?fN)-[:alpha:]{-213}[:alnum:]qHEu", 1, 0 },
		{ "[:punct:]TTTTTTTTTTTTTTTTTTT[:d(\?#igit:])[:alpha:]", 0, 0 },
		{ "([^[^[^J4(+++++++++++++++++++++SgDE(\?>\"y8].]::::::::::::::"
		  ":)pP5-]p)O{,199}xxxxxxxxxxxxxxxxxxxxxx[:ascii:]%",
		  1, 0 },
		{ "([:alpha:]Fs)Z", 1, 0 },
		{ "[()]{209}[:alpha:]hhhhhhhhh(hhhhhhhhhhhhhhhhhhhhh)pP<<<<<<<<"
		  "<<<<<<<<<<<<<<<<<<<<<",
		  1, 0 },
		{ "-{-8,}.[:(\?imsxx:ascii(\?<!:]{-231}aa*{}K^UQL\?)d\?[:"
		  "lowerprint:]W)q>D9'",
		  0, 0 },
		{ "[#(\?msximsx:#########################-IIIIIIIIIIIIII(IIII("
		  "\?#IIIII((\?#[^III{})N.[(\?=:lowerprint:]))CwT,,,,,,,,,,,,,,"
		  ",,,,,,Sq]$CCCCCCCCCCCCCCCCCCCCCCCuuuuuuuu])))",
		  0, 0 },
		{ "[:xdigit:][(\?#]){13}{,75}lllllllll", 0, 0 },
		{ "[c]QQQQQQQQ1+{-252[(\?#}33333])[:upperword:]", 0, 0 },
		{ "P@i #>>PF!@8G<[(\?:^P]-)D", 0, 0 },
		{ "uZZZZZZZZZZZZZZ[^ZZZZZl*-211{199}(\?!p])"
		  "EEEEEEEEEEEEEEEEEEEEEEEEEEED[:lowerp(\?msximsx:rint:])",
		  0, 0 },
		{ "[(\?!^])021[:graph:]'", 0, 0 },
		{ "\\(\?>[(\?<=:ascii:]{}[:alpha:]d8}G))", 0, 0 },
		{ "[^[((\?!1)[^,a|]\?{,242}[:alnum:])X\"a", 0, 0 },
		{ "pP[((\?simx::a(\?!lnum:]vvvvvvvvvvvvvvvvvvvvvvvvv)|O0)[:"
		  "digit:]ooooooooooooooooooooo)"
		  "\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"",
		  0, 0 },
		{ "_ L:8J-~ Y$[:uppe(rword:]{,-184}]{}6.A)", 0, 0 },
		{ "{,105}.(9]]{-12})N@0nOOE", 1, 0 },
		{ "HHHHHHHHHH[:xdigit:]uuuuuuuuuuuu{}E^X\\\\\\12601", -1, 1 },
		{ "( o)=\"OU7h{V>", 1, 0 },
		{ "[[:xdigit:])))))$[:xdigit:]+{152}{,-50}(c),,,,,,!!!!!!!!!!!!"
		  "!!(\?>!!!!!!!!!!!!!.[:digit:]i>\"O'i9])-175d_",
		  0, 0 },
		{ "[([^[^[^([[Eeee[^eeeeeee(\?(\?<!(eeeeeeeeeeeeeeeeeef|]][:"
		  "alph()\?>(\?!(\?>a:]a{,166})/////////////////////"
		  "[:gr[^aph:])Gpu",
		  0, 0 },
		{ "(7)NNNNNNNNNNN132", 1, 0 },
		{ "[([\?#^[]{QKm$v])][:alp[^ha:]]", 0, 0 },
		{ "(:{86})7{K|[:alpha:]{O", 1, 0 },
		{ "([Y(\?{[[^:alnum:][:alnum:][:digit:][:a(\?(lpha(\?(:].})", 1,
		  0 },
		{ "[[({29,-30}([[^:digit:])Y]]J=~{,220}[:blankcntrl:])"
		  "0ooooooooooooooooooooooooooooooo[:punct:]&]",
		  0, 0 },
		{ "[^1Dx32[:alnum:]]{[(\?::punct:]MMMMMMMMMM)12759", 0, 0 },
		{ "([[[]]*|(_])[:u(\?{pperword:]})", 2, 0 },
		{ "[:upper(\?(wo)rd:]){-16,250}", 0, 0 },
		{ "([^{194}i(\?({161)}PP\\S{}{,-14}]))z{208,225}BpPEt", 1, 0 },
		{ "[(\?m-ms:)}&!@29k0sUqzt9}<-x|A$!+G>>>>>>>>>>>>>>>>>>>>>>>>>>"
		  ">>>>>>CCCCCCCCCC-][[:space:]][:space:]El",
		  0, 0 },
		{ "()[:digit:(\?isx(\?>ix:]K^WQQQQQQQQQQQQQQs)[:lowerprint:])",
		  1, 0 },
		{ "[a|(\?imix:S(\?(SSSS)SS(\?>S)]W)8t[:ascii:]f$)[:alnum:]"
		  "111111111111111111111[^[:space:]x{12729}+'''''''''''''''']",
		  0, 0 },
		{ "[^(\?!(\?(\?#=)a)[:punct:]=2)(){}$$$$$$$$$(\?ims(\?#-isx:$$$"
		  "$$$$$$$$$$$$$(\?#$$s)x{294b}##############################"
		  "slllll)]){,209}333333333333333333G:v2/K",
		  0, 0 },
		{ "[^]ub(\?<=)vQ6(\?#Z\"3.)[:space:]u[[:digit:]]"
		  "7777777777777777U'{}sssssssssss",
		  0, 0 },
		{ "(([(])`[:ascii:]b)", 2, 0 },
		{ "[[[^[^([^[^(\?=(\?imxisx:[[^w])", 0, 0 },
		{ "pppp(pppppppppp-{-175}Nb>k&)sssss{-190,-54}", 1, 0 },
		{ "()OJ@`'%[:(as(\?!cii(\?#:]))+pffffffffffffffffffffffffffff{,"
		  "162}[:ascii:]5)s-[:graph:]",
		  1, 0 },
		{ "[(M{}Ux5{jaW/"
		  "{}[^u[:alpha:]s^{84,}PPb@Wt$(\?>nha<Yf41a)]{}[:lowerprint:])"
		  "*[:lowerprint:]][:upperword:]^1gS.^=pp{}"
		  "FFFFFFFFFFFFFFFFFFFFFFFFFFF33333333333{}",
		  0, 0 },
		{ ")\?L9~h4BQnNp F\\Q{}", 0, 0 },
		{ "($)[:upperwor(\?:d:])N[:alnum:]"
		  "bcccccccc5555555555555555555555555.N[:blankcntrl:]",
		  1, 0 },
		{ "2222222222222222222ppppppppppppppppp[:lowerprint:]))[^B\\e{{"
		  "{{{f]6#+{,-104}{{{{{{{{{{{{{",
		  0, 0 },
		{ "<[(\?>:al[^pha:]])\"O\"vN", 0, 0 },
		{ "[(\?>d8E@b.{(\?<=,-250}(\?=mx48[:punct:]^&)]nAeYY)W)-13272",
		  0, 0 },
		{ "22222222222222222222222222///////////////////"
		  "[:digi(\?#t:]eM)[:lowerprint:][:alpha:][:alpha:]EEEEEEEEEEE",
		  0, 0 },
		{ "[(\?={38,223})^\\\\\\\\\\\\\\\\L(\?:{,-50}3|)}r]aW\\x70U{-"
		  "110,}8LUf)w]4+oav",
		  0, 0 },
		{ "G[:upperword:]v[:lowerprint:]-tu)j8CK", 0, 0 },
		{ "[([([^().(\?(\?><=c)'(\?<(='(\?<!''''''''(\?(\?<!!''''''''''"
		  "'(\?=''''''/"
		  "(|dHj(P>L\?q!G))|)(\?=n(\?(^tk)T-z$q!D|2<rc[^{,53})]jZy))))"
		  "6)[:bla)nkcntrl:])010])7pE`l[:space:]([:lowerprint:]"
		  "eXXXXXXXXXXXXXXXXXXXTTTrrrrrrrrrrrrrrrrrrrrrrrrrrrrrr]+[:"
		  "alph(\?!a:]7)444444444444444444444444l{34,}]J{}"
		  "yyyyyyyyyyyyyyyyyyyyyyyyyyy)\?'z9~9s.mA",
		  1, 0 },
		{ "().", 1, 0 },
		{ "{-205(,}[:al(ph(\?>[^a:]W,[4DLR[^^8THMtVv~KKw(\?>)pPF)].{-"
		  "245,}]))fffffffffd[:alpha:]zzzzzzzzzzzzzzzzzzzzzzzzzzzzz",
		  1, 0 },
		{ "[^[[^]{-[1(\?imximx:83,}{,182}][:graph:]]^])-bTO X0P", 0,
		  0 },
		{ "[11111111111(\?#11111111]U[:asc([\?!ii:]{,37}+{-89}){-170,"
		  "218}{-21,})f[:xdigit:]]P.[:xdig(\?:it:]145)YYYYYY$S@:@@@@@@@"
		  "@@{-150,-109}",
		  0, 0 },
		{ "{-40}<o][^D[(:graph:]]d).Q", 0, 0 },
		{ "()APPLn[:xdigit:]", 1, 0 },
		{ "[([^\?+++++++++++      [       (\?>  (\?(     (\?{  "
		  "(\?!]E{-29})pP)})ZpP",
		  0, 0 },
		{ "(t|{}c[^z^\?(@YLD]bSSSSSSSSSSSSSSS)+{{{{{{{{{{{{{{{[:xdigit:"
		  "]n>1)WkF}7",
		  1, 0 },
		{ "W22[0Q[^d-d{}PPPPPPPPPPPPPPP<^FZ(\?<=\"[U]Yo}9H'cYy]S[:"
		  "alnum:]^8wTDH)^u",
		  0, 0 },
		{ "([^[(\?:(\?>((\?#$)(\?{^(\?>))///////////(\?>/"
		  "ggggggggggggggggg{1(\?!90,-13}\\D)Dyyyyyyyyyyyy(\?!y(\?<!"
		  "yyyyyyy)})]]$)[:xdigit:]|{}-)#a))nPpP[:lowerprint:]AA)V+q^[:"
		  "blankcntrl:]",
		  1, 0 },
		{ "([^(\?!]))D{,97}", 1, 0 },
		{ "(c){,141}", 1, 0 },
		{ "nn[:s(\?<=pace:])[:upperword:]ooooooooooooooooooo*^[:space:]"
		  "`{-188,129}mmmmmmmmmmmmm^.",
		  0, 0 },
		{ "[[G{(\?imsximsx:2(49}{,-46}r(\?(\?=#Gw]u))[:bl(\?>ankcntrl:]"
		  "))(^m+)zSiZ "
		  "F4[!]VV$E{-9,-100}''''('''''''''\?DEOOOOOOOOOOOO############"
		  "###[:space:])HHHH)[:digit:]'////////////",
		  2, 0 },
		{ "[^*}(\?>)(\?:7Q=#+]KKKKKKKKKKKKKKKKKKKKKKKKKKKG)]]]]]]]]]]]]"
		  "]]]]]]]]]]]]]][:alpha:]-{}",
		  0, 0 },
		{ "[n(\?<(\?#!nnnnnn55555{205,}!)[:alnum:]^]!!!!!!!!!!!!!!!!!!!"
		  "!!!![:punct:])[:x(\?(digit:]vr)|'n6W5 D&jk[:punct:]5)",
		  0, 0 },
		{ "[^P(P{(\?i(msxisx:235,}))***])[:alpha:]^", 0, 0 },
		{ "[([t(\?<!(\?<!4])[:u(\?=pperword:]))-])}}}}}}}}}}}}}}}}}c{-"
		  "39,}[:digit:]$-",
		  0, 0 },
		{ "([^)]{241}[:xdigit:][:upp(\?=erwo(\?(rd:]-xF5b{})q[:ascii:])"
		  "T4U{185}9999999999)()X&Ny[:alpha:]@@@@@@@@@@@@@@@@@@@@@@@@@@"
		  "@@@@@@{69,}[:alnum:]x{d7f8}p-[:digit:]",
		  2, 0 },
		{ "(f)(${,111}{25,}!\\d{,94}[:blankcntrl:]@[:space:][:ascii:])-"
		  "237{,232}DQVVVVVVVVVVVVVV)-",
		  2, 0 },
		{ "PP[:g(\?!raph:]){}", 0, 0 },
		{ "([[^-][^4[:digit:]NNNNNNNNNNN]TVU:])[:ascii:]", 1, 0 },
		{ "(([^(\?[[^<=)][:graph:]+iiiiiiiiiiiiiiiiiiiiiiiiii0INFX[:"
		  "xdigi(\?(t:][:blankcntrl:]][:graph:]qM6A[:alpha:][:graph:])"
		  "1*]eFvvvvvvvvvv)v-)U))t{89}",
		  2, 0 },
		{ "[^ZZZZZZZZZZZZZZZiiiiiiiiiiiii(iiiiiiiiiiiiiii{}))))))))))))"
		  "))))))]))))))))))))))))))))))))[:digit:]-",
		  0, 0 },
		{ "ddddddddd+zzzzzzzzzzzz[:graph(:])ssssssM{-223}[:graph:]", 0,
		  0 },
		{ "[:alph(\?>a:])x11{-144,45}.", 0, 0 },
		{ "[]{#y.^(\?{{}&&&&(\?:[^&&&&&&&&)[:punct:]n{190}OylBQ{(\?!-"
		  "73})2u',x(\?#Ds(\?#{})j(\?{-})})u0(((((((\?{(((([:alnum:])"
		  "MC})b=71TncyE>[:xdigit:]*\\f]{}]\"p#!8twZT\")[:punct:][:"
		  "space:]",
		  0, 0 },
		{ "[^(Z6]8)|'@p8{}[:upperword:]MMMMMMMMMMMMMMMMMMMMMMMMMMMM{}"
		  "7c",
		  0, 0 },
		{ "$0)@#vp,VcJ.Bdh", 0, 0 },
		{ "[[^(-])nnnn+s`[:alpha:][:blankcnt[^rl:][:upperword:]{-15,}]["
		  ":g(raph:]c]){,-177}6[:upperword:]##################{,-14}",
		  0, 0 },
		{ "[[(5C{86(,}PPrrrrrrrrrrrrrrrrrrrrr{150,182})N{}LSC|)-[:"
		  "alnum:]{}KKKKKKKKKKKKKKKK<4=~7K3PPPPPPPPPPPPPPPPPPPPPPP[:"
		  "lowerprint:]]]",
		  -1, 0 },
		{ "([^(x{145b[5}^hfc.0)+]z@_&lA{-34,}])X\?", 1, 0 },
		{ "([(\?<=)(\?!])l)L", 1, 0 },
		{ "({-104,}DrPPDF4444444444444[:space:])[:space:]", 1, 0 },
		{ "())))", 1, 0 },
		{ "[[^((\?>\?(\?[{})q5v}r7t(P)xtffffffffffff))]{,-66}kdExX&-"
		  "SCeCzzzzzzzzzEc)E,\"^I]x{e629}|{}]",
		  0, 0 },
		{ "[h[:punct:]p\\[\\\\(\?:\\\\[^\\\\)Eo#:C$u[^T/"
		  "ysA[*%nM:f]{,221}[:lowerprin[^t:]{]bx{f285}E]E[:alnum:]+]"
		  "1oe3B][:alp(ha:]]fh7}M$l)D{17}",
		  0, 0 },
		{ "IIIIIIII[^IIIIIIX]-_S[:digit(\?#:])"
		  "33333333333333333333333333[:punct:]iiiiiiiiiiiiiiiiii",
		  0, 0 },
		{ "[^[[:punct:](\?((\?:^ "
		  "#Q_po(\?=[:alpha:]{}z()(\?!======'wq$Q2)LLLLLLLLLLLLLLLe("
		  "C9gggggggggggggggggg[(\?<=:alnum:]()\?<!{-85,}W[[[[[[[[[[[[["
		  "[[[(\?{[[[[[[^)(]\?])|uuu[uuuuuuuuuuuuuuuuuu{,-20}p${}]MHI&"
		  "7s:\?$[:digit:]-:)_V`*{-52,}{250}$:ME9izF/"
		  "uP[:blankcntrl:]})''''''''''''''''''''''''''''')"
		  "CCCCCCCCCCCCCCCCCCCCCCCCdd[:ascii:][:lowerprint:]."
		  "Mcccccccccc2B{-230,}$[:digit:]",
		  1, 0 },
		{ "()|mOAuK~P144[:space:]^9dddddddddddddddddddddddddddddd[:"
		  "blankcntrl:]",
		  1, 0 },
		{ "[^[^[^.L[^-vEUl(\?>(\?=a!Ib1P]])])~~~~~~~]xE9", 0, 0 },
		{ "X()", 1, 0 },
		{ "[^()(\?#G(\?<!)(\?=^r])*,XXXXXXXXXXXXXXXXX@)444444444", 0,
		  0 },
		{ "([[((\?<=({,-70})-[:xd(\?=igit:]{,138})", -1, 0 },
		{ "[(^]{62,67})", 0, 0 },
		{ "([((])[:space:]))", 1, 0 },
		{ "(a{(109,})[:alpha:]{,-121}{})]RRRRRRRRRRRRRRRRRRRRRRRR{}{"
		  "125,}ttttttttt{46,}`[:space:]",
		  2, 0 },
		{ "[^[^([q[8]~.IPmiBSspP)]QpX[pT==8@lulANS]]{,-98}]", 0, 0 },
		{ "[^77777777777777777777777(\?>777777])", 0, 0 },
		{ "(),e<^X~{[:alpha:]{}G{70}", 1, 0 },
		{ "({-211,}'){}", 1, 0 },
		{ "[^(\?imsxsx:{}[*])cccccccccccccccccccccccccccccccc<z0W8]$",
		  0, 0 },
		{ "(){2,89}$z", 1, 0 },
		{ "((050[^\"\"\"\"\"\"\"\"z]8|j{}{,-112}$).pP)qq1~hW}L", 2, 0 },
		{ "[[^[(+xx(\?<!xxxxxxxx(\?!xxxxxxxxxx(\?#(\?>[x))(\?:]r.]]]))["
		  ":graph(\?<=:])))",
		  0, 0 },
		{ "[^([(\?#)(\?(\?(<=)l|\?(\?!])kkkkkkkkkkkkkkkkkkkkkkkkkk", 0,
		  0 },
		{ "[:xdigit:]K(KKKKKKK)^3c.OOO{-240,-10}2{-97,-139}*{-34,}[:"
		  "xdigit:]",
		  1, 0 },
		{ "[([^66666666F(\?>FFFFFFFFFFwpP)LLLLLDeDA&Am$l[:xdigit:]!T5#]"
		  "n[:alpha:]U*)))))))))))))PP]",
		  0, 0 },
		{ "[[[:punct:]u^[:xdigit:]L(\?:[:xdigit:][[:graph:]PP{21}A[:"
		  "alpha:]8%I(M%b<eE~#C@r=uG~~~~~~~~~~~~~~~~~~~~~~~~~~~~+w]pP)"
		  "T]]$$$$$$$$$$$$$$${-121,}|l",
		  0, 0 },
		{ "([(107{,-4(\?=}~[^D)])f]{,46}+ri<)", 1, 0 },
		{ "[(\?<=]{,208}+~)", 0, 0 },
		{ "[^444(\?<=4444444[:alnum:]&[,i]0)[:alpha:][:upperword:]", 0,
		  0 },
		{ "[^([^(\?()*+)SS(\?>SSSSSSSSSSSSSSSSSSSSSS]]]]]]]]]]]]]]]]]]]"
		  "]]]]]]]]]]]{,-1}])[:blankcntrl:]============================"
		  "===[:punct:][:blankcntrl:]Z[:space:][:ascii:]$|$[:"
		  "blankcntrl:] JR.{,133}[:alpha:]$\?)<]",
		  -1, 0 },
		{ "(OL[:u[pperword(:][:s[^pace:].[:spac(e:],,,,]*])$)\?)", 1,
		  0 },
		{ "(VI[:digit:][:alpha:]6)EG", 1, 0 },
		{ "({}){-2,-40}rrrrrrrrrrrrrrrrrrrrrrr[:punct:]", 1, 0 },
		{ "()q", 1, 0 },
		{ "[^([^[([^C|])]{,-56}[:xdigit:]{-144,}V])fYv{-[40,-58}$@@@@@@"
		  "@@@@@@@]|Y(-]-.]h-[:dig(it:])>>>dddddddddddddddddddddddddd{"
		  "101,}",
		  1, 0 },
		{ "([P,{1(\?(\?(<=28,-218[^)}LoZX)])!!!!!!!!!!!!!!*[:blank(\?!"
		  "cntrl:]ed)\\\\\\\\\\\\\\\\\\\\[\\L\?][:graph:]:*Y{-108,120}"
		  "xCC)]",
		  1, 0 },
		{ "(A[:space:]PP{185}a^!!!!!!lllllll)*db\?$Pfr", 1, 0 },
		{ "{-21,-118}kG[(\?{:xdigit:]})[:punct:]{69}"
		  "Qyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy5{}TTTTTTTTTTTTTTTTTTTTT",
		  0, 0 },
		{ "[[^[P(\?<=P$X>0^d.[:punct:](\?#ccccccccccccccccccccccccc{}"
		  "3N000(\?>00000000000000000000000000000]f[:punct:]5)).R======"
		  "=========={,222}^wwwwwwww$)]-{}              "
		  "]{,-22}CjP{242,}",
		  0, 0 },
		{ "[(\?#^]{})", 0, 0 },
		{ "[^([[([([[([^[^(\?:(\?(\?(!)]\"))h>\"RRRRRRRRRRRRRRRR[^"
		  "RRRRR{68,-65}7Q(\?{]",
		  0, 0 },
		{ "(P{}){175,}PP{}rttttttttttt", 1, 0 },
		{ "[:bla(\?{nkcntrl(\?#:]})))))))))))))))))))))))!!!!sR{})", 0,
		  0 },
		{ " [:digit:]dAAAAAAAAAAAAA^[:ascii(:]55)^", 0, 0 },
		{ "($*)dZY", -1, 0 },
		{ "[:graph:][:lowerprint:]S[:gr(\?=aph:]{-128,}"
		  "666666666666666666666{}[:upperword:]|"
		  "nnnnnnnnnnnnnnnnnnnnnnnnnnB)c[:xdigit:]{-225,}{-4,}{-192,}"
		  "QQQQQQQQQQQQQQQ@@@@@@@@@@@@@@@@@@@@@@.",
		  0, 0 },
		{ "([:digit:]s{44,}{}{-31,}c{,-130}pP){-241,}UeN", 1, 0 },
		{ "([^)((\?>\?#{}hK\"V2\?d][KKK(\?imsxim:KKKKKKKKKKKKKKKKKKKK[^"
		  "KKKKKKKKKWWWW[WWWWWWWWWWWWWWWWW)B])_l_3",
		  1, 0 },
		{ "[(^[(\?!*){[^,91}].j]*]L)*c|[:alpha:]&", 0, 0 },
		{ "[^[[[^[777GGG(\?:W_U(\?imsxms:[:punct:]A]-)[:digit:][:"
		  "blankcntrl(\?(:]][:alnum:)])]WRRRRRRRRRRRRRRRRRRRRRRRRRRR]{"
		  "31,}[:xdigit:]][:xdigit:]))))))))))))))))))))))$[:xdigit:]",
		  0, 0 },
		{ "[:ascii:]m*[:punct:]#[(\?<!:punct:][:alpha:]-,"
		  "7vyXeeeeeeeeeeeeeeeeeeeeeeeee^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^"
		  "^%%%%%%%%%%%%%%%%%%%%%%%%%%%%[:digit:]''''''''''''''''')",
		  0, 0 },
		{ "([^*[(:punct:]9999999999999999999{147,}]j{,193}{171}Z-)){"
		  "208}0[:graph:]yDt",
		  1, 0 },
		{ "(dw[[:alpha:]U]ttt[tttttttttttttttttttt]Q^171e)[:xdigit:]/",
		  1, 0 },
		{ "[[^((\?#)Tqqqqqqqqqqqqqqqqqqqqqqqqq105++++++++++++++++++++++"
		  "++++b7V+7dit]])|D",
		  0, 0 },
		{ "{}P7.Ajh[:xdigit:]^[:blankc((\?(\?<=nt[rl:]FFF)-]){}o|a[:"
		  "grap(\?!h:]))PsssssssssssssssssssssssssssssssN^{-60,}Kb",
		  0, 0 },
		{ "[:alpha(\?(:]$!_+777777777777777777777777O)666)lll[^llllll[^"
		  "l{{{{{{{{{{{{{{{{{{{{{{|]{-217,}MoEl`7)^)LlU[:alph[a:]({-"
		  "241,27})]]{-212}{,249}n)X",
		  1, 0 },
		{ "[U|ajP[:alnum:]n[(:digit:]]W)[:graph:]b[:xdigit:].P", 0, 0 },
		{ "(([:low(\?-imsx:erprint:]|{}[:ascii:][:gr(\?:aph:])>>>>>>>>>"
		  ">>>>{,-129}))\?{-226,}^P)R",
		  2, 0 },
		{ "[^[[nnnnnnnnnn(\?=nnnn(\?!nnnnnnnnnnnn(\?#nnnnnn{,-38}N){"
		  "202,}]$[:alnum:])]t][:alnum:[]^=w){237}][:alpha:]-[:alpha:]+"
		  "e",
		  0, 0 },
		{ "()[(\?(:digit):]+qc)O88888888{,151}aJ", 1, 0 },
		{ "([^([(\?!sv(\?=)d]{-200,})N))]Z{-73,15}", 1, 0 },
		{ "([\?\?\?\?|||||||||||(\?{||(\?=||||||||-}[))Ehhhhhhhhhhhhh{,"
		  "202}&TcfL((\?:>)((\?!\?>$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$8[:"
		  "alpha:]\\d])]C[:graph:]h*,\"\?u{|mU,a)[:blankcntrl:][:"
		  "lowerp(\?>rint:])PPnP+9.[:xdigit:]*PPjjjjjjjjjj~y<#*scf_\"^"
		  "e[:xdig(\?(i)t(:])~$y)^){-131,77}^L%",
		  1, 0 },
		{ "[^[(((\?>)$}h9$B5+yhU/"
		  "Nqh$YYYYYYYYYYYYYYYYYYYYYShK)3WHw1vMMMMMMMMMMMMM(\?="
		  "MMMMMMMMMMMM[:alnum:]/"
		  ")dddddddddddd(dddddd\"e5zLW)+![:space:]+BHGHfAS]"
		  "\?IIIIIIIIIIIIIIII*&&&&&&&&&&&&&&&&&&)NNvwDteepjdm<<<<<<<<<<"
		  "<<<<<<<<<<<<<<<<<<<<${61,219}D][:digit:]0",
		  -1, 0 },
		{ "[:punct:][{177,(\?=234}]ix9*)", 0, 0 },
		{ "([^K{,3(\?<=4}]I)\?U)", 1, 0 },
		{ "[([^[[[^([([^[^(\?=])X", 0, 0 },
		{ "[:blankcntrl:(])qd_R\?{\?r[=\"[^[^6]vX8)a+{C%H84CK6Uy#E]sE{"
		  "208}",
		  0, 0 },
		{ "PPPPPPPPPPPPPPPPPPPPPPPPPPnnnnnnnnnn()[:upperword:]us", 1,
		  0 },
		{ "x{,46}[:graph:]LU{}CU)", 0, 0 },
		{ "()-t|[^W{}][:lo[^werprint:]{}]\?b5", 1, 0 },
		{ "()x5A", 1, 0 },
		{ "[([^]-217)]s{-47,135}0000000000000000000000000000000{,-108}",
		  0, 0 },
		{ "[^((\?{[^L\?u]})f", 0, 0 },
		{ "()[[^^(\?{y(\?=VF_(\?<=]D}))]-= {46,})^5bIEQ{,-96}Z", 1, 0 },
		{ "([^{}f[:punct:]\"X%%%%%%%%%%%%%%%%%%%%]5{-194}A[:punct:]"
		  "mnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn+AAAAAAAAAA-)",
		  1, 0 },
		{ "(CCCCCCCCCCCCCCCCCCCC{-230}352{-182,-68}O4{})", 1, 0 },
		{ "([^[^\?[:space:]$TTTTTTTTTTTTLLLLL[^LLLLLLLLLL[^({}{4,-179}]"
		  "]J] C]){}C{}{-224,})QQQQQQQQQQQQQQQQQ^",
		  1, 0 },
		{ "([[:alnum:]].){-155,-82}dzI{55,}^", 1, 0 },
		{ "([[:alnum:](\?#{88,-178})[:graph:]NC\"pI[:punct:]rmWd5y^p+"
		  "gUP]YYYYYYYYYYYYYYYYYYYY~{,-62}{,200}{-109}{}+"
		  "333333333333333333333333333333{}p)^.hhhhhhhhhhhhhhh",
		  1, 0 },
		{ "[000000(\?mmsx:00000000000000000000000)M]]]]2*`[^]QQQQQQQQ("
		  "\?<=QQQQQQQQQQQQQQQQQQQQQQQ])\"<h\?",
		  0, 0 },
		{ "[^((<g(\?>5j[bbbbbbb(\?{bb)o{}3(\?imxisx:E]g})YYYYY[:"
		  "blankcntr(\?#l:].(()w264[:ascii:]^)[:ascii:]G)&(n "
		  "{^PGn[:xdigit:])nv_e|]{-103,30}",
		  3, 0 },
		{ "[^(([(\?!{}@[^HCO[[^^D[|]{,-49}][:xdigit:]]c`4[:ascii(\?<!:]"
		  ")$66666666666)*)]PP$Z[:alpha:]{,-235}UK],(aT/"
		  "+6rbMqs60EloA)[:g(\?isx:raph:]!)]z$o{-24,}x1E[:blankcntrl:]"
		  "ZDFvk",
		  1, 0 },
		{ "[:blank(\?=cntrl:]US@.!\"[:digit:]*E)$16182", 0, 0 },
		{ "[-{}x{3772[}][:(\?<=xdigit:][:u(\?#pperword:].W)aD)<pfN<b=C|"
		  "-{-38}EZdOP|!>ggggggggggggggg\\\\\\\\\\\\\\\\\\\\\\\\\\Ef[:"
		  "space:]\?][:ascii:]{21,}",
		  0, 0 },
		{ "([:xdigit:]W[:u(pperword(\?::]jS "
		  "[:upperword:]*)[:alpha:]nnnnnnnnnnn))-148}SSu",
		  1, 0 },
		{ "([^(\?!\?)[(:upperword:])Bx^x$~lCr6*)6", 1, 0 },
		{ "[{,-78}Y[:xdigit:][^s(\?>]P[:space:])]YYYYYYYYY[:punct:][:"
		  "alnum:][:blankcntrl:]",
		  0, 0 },
		{ "([MMMMMM(\?(MMM)M(\?<=MMMMMMMMMMMMMMM[^M)]en][:punct:]-[:"
		  "alpha:]))Nr[:space:]",
		  1, 0 },
		{ "~=1([^(\?=(\?:l){}])j{-44}{-18}[^u[:graph:]]{-187,}[:xdigit:"
		  "]w[:alpha:])",
		  1, 0 },
		{ "[ccccc(\?>c(\?{cccc[ccccwetoCei+)w&-+{,-142}[:alpha:]"
		  "PP66io4(|zkA=],,,,,,,,,,,,,,,,,,,,,Lx5Cx{d2bb}]{188}U~~~~~~~"
		  "~~~~~~~~~~~~~~~~})",
		  0, 0 },
		{ "Q|0\"[:d(\?:igit:]^{,-174})", 0, 0 },
		{ "[^[(\?>rh])]", 0, 0 },
		{ "[ees{{{{{{{{{{{{{{{{{bbbbbbb4`ml******(\?=****+])", 0, 0 },
		{ "((hdG[((\?<=:dig(it:])[^[:alpha:]$(\?sxi:)x{11390}[(\?{:"
		  "upperword:]~)i 8[:blankcn[trl:(])]+{,-183}Zqp",
		  2, 0 },
		{ "Dd{D8`+DW={-[53,1(\?<=71}])", 0, 0 },
		{ "[:(\?(alpha:][:punct:])", 0, 0 },
		{ ".LLLLLLLLLLLLLLLLLLLLLLLLLLLL{}pP[:punct:]x0CZ{30,}!!!(!!!!!"
		  "!!!!!!!!!!!!!!!!!!!!==@77.%[:graph:]D)",
		  1, 0 },
		{ "[^[^[[r(\?#]){-237,}RRRRRRRRRRRRRRRRRRRRRRRR[^Rll(\?!(\?{"
		  "lllll]",
		  0, 0 },
		{ "()*ooooooooooooooooooooyyyyyyyyyyyyyyy", 1, 0 },
		{ "{,4(}D)JJJJJJJJJJJJJJJJJJJJJJJJJ", 1, 0 },
		{ "((b.D{}[:al[pha:]{64}]{})==========================[:alnum:]"
		  "h>77b)!Ab",
		  2, 0 },
		{ "([^[^[^oooooooooooooooooooooo][:space:][:punct:]PeniKe*~$"
		  "g\?${>[:lowerprint:]w))))))))))))))){}yyyyyyyyyyyyyyyyyy]pP."
		  "|QhZ]{,190})sssssssssssssr+=[:blankcntrl:]"
		  "WWWWWWWWWWWWWWWWWWWWW",
		  1, 0 },
		{ "([*(\?{})hhhhhhhhhhhhhhhh]G{,-170}QdErrrrrrrc-"
		  "jjjjjjjjjjjjjjjjjjjjn+{-130,-10})PpDS@Bee",
		  1, 0 },
		{ "([:b(\?=lankcntrl:]))T[:alnum:]{-224}ywt", 1, 0 },
		{ "([633(\?<=333(\?<=3333333333(333333)^\?]aGA)[:digi(\?>(\?{t:"
		  "])$[[:space:][:xdigit:])|8T\?',_{171}{}{113}b\?5kAv0/"
		  "7{})`huh>xM]C8pYRz]s$Eu08)",
		  1, 0 },
		{ "-(pP)[:alnum:]$^", 1, 0 },
		{ "[^x(\?{{17681}]P*)U(_t/8E_\"iN})3333333", 1, 0 },
		{ "(([^([[r(\?=[[^^*kx$][:alpha:]:::[:::::[^[^::::::::((\?{\?{:"
		  ":]).^p[:space:]}){52}{}]W{}fn",
		  2, 0 },
		{ "[:(\?>punct:]Ef[:xdigit:]x{c07b}{-50}Z{129,}YL1T`\\A)x[:"
		  "punc(\?=t:]e[:xdigit:]2c6E46Y)+n               ",
		  0, 0 },
		{ "[^(\?!{,-79}[:punct:]'|}>,)][:blankcntrl:]{-118,-231}{-119,-"
		  "50}:XXXXXXXXXXXXXXXXX-~{}$txlB)3KFL",
		  0, 0 },
		{ "[^(([^fccccccccccccccccccc(\?<!ccccgQeKMfKzz]X$$$$$$$$$$$$$$"
		  "$$$$$$$$$$$$$$$$$[:l(\?<=(\?<=owerprint:]))s{-97}{}))EUi${,-"
		  "132}'{79}---------{,-93}77777777777777777[:lowerprint:].:H)["
		  ":punct:]nnnnnncP\?s1:dGed{186}N@pppppppppppppppppppppP{-212,"
		  "-110}[:space:][:lowerprint:]$S}7{-112,164}-*.{-184,}"
		  "OOOOOOOOOO]f\?",
		  0, 0 },
		{ "(([\?#(\?>)])qcU$Q7|82\?{})", 2, 0 },
		{ "[^yyyyyyyyyyyyyyyyyyyy(\?#yyyyyyyyyyya][:ascii:]\?)", 0, 0 },
		{ "(([((\?{)EEEE(\?<!EEEEE(\?:EEEEEE~)}){244,}"
		  "QQQQQQQQQQQQQQQQQQQ(\?>QQQQQQ(\?!QQQQQ][:digit:]\?))"
		  "99999999999999)[:digit:][:upperword:]b))PP{}{}",
		  2, 0 },
		{ "(K(c=B))", 2, 0 },
		{ "(G`*s\?b[:g(raph:]))", 1, 0 },
		{ "[^[([[[*QQQQQQQQQQQQQQQQ(\?=(\?=QQQQQQ(\?<!"
		  "QQQQQQQQZddddddddd((\?{\?>ddddddddddc{22,}iiiiiiiii("
		  "iiiiiiiiiiiiiii(\?#iiiiiii[^i))\?\?\?\?\?\?]WWW)[:"
		  "lowerprint:])]{-60,202}+[:upperword:]f[:xdigit:][:alnum:]{,-"
		  "214})1~~~~~~~MMMMMMMMMMMMMMMMMM.",
		  0, 0 },
		{ "({-102,})A.", 1, 0 },
		{ "[((((\?<!(\?[^>(\?#\?()))p\"JD.{}(\?>)))((\?{l(\?<=).'053][:"
		  "xdigit:]N+)})]WWWW%[:asc(\?{ii:]}))B[:alnum:]X){}s[:digit:]",
		  0, 0 },
		{ "x7&{139}WWWWWWWWWWWWWW[:blankcntr[^(\?<!l:]-71]\"{-167}cqkI)"
		  "[:dig[^it:]{}{}[:digit:]*[:punct:]-[l11111111111111111(\?("
		  "111111111{175,-216}~[:alnum:]`+X1F)vCpWSp(\?>~[^n@f`````````"
		  "````)````````P])Y,N{}{}]{}pXF@)",
		  0, 0 },
		{ "G[([(\?(^)$])P]^[:alnum:]){,-48}[:blankcntrl:]{}", 0, 0 },
		{ "[[^[^f(\?=f(\?<=fffffff[^fffffffff[^fffffffff(\?<=fff]){-"
		  "194,150}fx{e5a4}V",
		  0, 0 },
		{ "9[:xdigit(\?{:]})", 0, 0 },
		{ "[^([[(\?>()$xxxxxxxxxxxxxx[xxxxxxxxxxxxxxxx((\?=aA)s13]])pp["
		  "(\?>pppppppppppppppp|{}){20,}]b)]{-179,183}{-204,}[:ascii:])"
		  "]-11111111{}{,132}qooooooooooooooooooo{}${}|9t",
		  0, 0 },
		{ "([^[{}]\"[^6]*-{,-106}{}u]BR~8WG,U-)[:blankcntrl:]", 1, 0 },
		{ "[''''''''(''''''''''z])c", 0, 0 },
		{ "[^[(\?>])[:alnum:]r[:alnum:]+{,215}D]", 0, 0 },
		{ "([({,127}7Qr(\?:z)pPNev%}(\?msximsx:4(\?<!){}&.D5555(\?<="
		  "55555555555555555555i$[:xdigit:]){,-157}[:graph:]U[:punct:]"
		  "nn(\?=nnnnnnnnnnnn(\?>nn(\?:nnnnnnnn_U{}]E)):^"
		  "oooooooooooooooooooooooooooo)",
		  1, 0 },
		{ "[^(\?#)(\?<!k2z]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]BW[:alnum:][:"
		  "graph:]{157}Y]s$C)[:graph:]{,-189}",
		  0, 0 },
		{ "$+CCCCCCCCC[^CCCCCC(\?<=Ca=]r{-81}[:alpha:][:alpha:])E=", -1,
		  0 },
		{ "[(((\?=\?{([^(\?<=)])>!(([:alnum:]{252}{}})ffffffffffffl){}"
		  "A2r\?~ImE\"[:punct:]){}[:digit:]",
		  2, 0 },
		{ "([:blank[cntrl:]].t^P)", 1, 0 },
		{ "[^[(\?:X])|rrrrrrrrrrrrrrrrrrrrrrrrrr*P]Q", 0, 0 },
		{ "[[[^(\?{((\?<!))s})(\?<!A){14}(\?:L*+TTTTTTT]U{[^-12([\?!,}"
		  "\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?)Y`Y)L]|]]|"
		  "]",
		  0, 0 },
		{ "hkXzf',]yP$+[:u(pperword:])", -1, 0 },
		{ "(#[:blankcnt(\?iximsx:rl:])$QQQQQQ{}[:digit:])\?A", 1, 0 },
		{ "(B{-34,})*{,106}", 1, 0 },
		{ "[(\?{:graph:]})", 0, 0 },
		{ "((){}{,63}[:punct:]^t[:space:])^17737", 2, 0 },
		{ "([^[SSSSSSSSS[SSSSSSSSSSSSSSS[([[[{38,}]Jn][:alpha:]])])$'",
		  1, 0 },
		{ "[^({}{95})B{1(\?>15}]x{f779}ZZ,Wo)O[:alpha:][:lowerprint:]{"
		  "81,228}Q[:upperword:]",
		  0, 0 },
		{ "[[^[^()n[[[[[[[[[[[[^[[[[[[[[[[(\?: G)(\?{K![^m) "
		  "j(\?:C|((\?:n*Xlaa908:n$m,))[:xdigit:]x(\?{{1a5cd}"
		  "pppppppppppppp(\?(pppp)p(pQ)))"
		  "ddddddddddddddddddddddddddddddd]q[:alnum:(\?{]Ga})\?})@[:"
		  "lowerprint:]{,169}[:blankcntrl:][:graph:]]n{-76,}|U\"{,-54}"
		  "t]I{}{-64,-232}]\?].\?{-111,227}) "
		  "@hFp\?j=H$Wbu<{,209}De{,145}{206}-})[:blankcntrl:]",
		  0, 0 },
		{ "[^[^(LLLLLLLLLLLLLL[^L[L[:alpha:]3{,189}(\?#(\?>n){}^"
		  "EXXXXXXXXXXXXXXXXXXXXXXXXX]c*)^r=$WWWWWWWWWWWWW",
		  0, 0 },
		{ ")w###################", 0, 0 },
		{ "{,121}[:d(\?(i)git:])E\?[:punct:]LLLLLLLLL[:ascii:]+", 0,
		  0 },
		{ "([]]]]]]]]]]]]][:space:]Jrt3o.]b)pwwwwwwwwwwwQfm~", 1, 0 },
		{ "[+-{,-120}*(\?!()t*(\?(\?{>G)F)yd]V{}f<\?}){245}"
		  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx[:upperword:]",
		  0, 0 },
		{ "(DDDDDDDDDDDDDDDDDDDDDDDDDDDDDc[:space:][:pu[^nct:]{-11,12}["
		  ":ascii:][:alpha:]{,155}P])",
		  1, 0 },
		{ "()ggggggg{-136,-21}", 1, 0 },
		{ "([^((\?<=U\?)(\?=^^^^^^^^^^^[^^^^^^^^^^^^^///(\?#//[////////"
		  "////////////"
		  "(\?()#######b+]$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$^[:digit:])"
		  "\\U]Q8@}4d)\\U",
		  1, 0 },
		{ "A[:graph(\?::])-mo=U[:upperword:]"
		  "ttttttttttttttttttttttttttt",
		  0, 0 },
		{ "[^(((\?=\?im-m(sx:)c~~[^~~~~~~~~~~~~~(\?>~~~~~~~~~~~~~"
		  "SSSSSSSSSSSSSSSSSSSS]{51,}[:digit:]{,-179}N))kk["
		  "kkkkkkkkkkkkkkg$)[(\?::punct:]zWl)]|)*",
		  0, 0 },
		{ "[((\?=()+A)][:graph:]x0B)[:graph:]", 0, 0 },
		{ "(nR%B[:blankcntrl:]C=|en-[:digit:]n[:graph:]HHHH[HH]D\?%[:"
		  "digit:]MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM.z(oF9zW8A7cfff(f))-["
		  ":blankcntrl:][:blankcntrl:]A[:digit:])D{,-243}",
		  3, 0 },
		{ "([[()]]{,-251(})\?L)uw@", 2, 0 },
		{ "\"|{(,-144})A.ooooooooo(ooooooFFFFFFFFFFFFF\?)n{,-18}", 2,
		  0 },
		{ "([^([(([[^([000000[0(0(\?!0(\?=0000000])45|E]", 1, 0 },
		{ "[B[[[[[[[[[[[|{}*oKqv%(\?<=wsQ{1pMeK1^6%nLNqi<@ge][:punct:]="
		  " M@* "
		  "D|NwL\\-"
		  "117\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"~"
		  "Qnd]h.O\"01x:[:alpha:]^){}D}\"",
		  0, 0 },
		{ "([[RRRRRRRRRRRRRRRRRRRRRRRRRRRRxpSrx{7d79}*oJ2`Ft{n1,3g:1H@"
		  "bT$D "
		  "&[n/"
		  "Cg)=ld@Ir{Fk>*4*`(\?>````````````````````(\?:`````.........."
		  "...........]]{,246})7 \"F4[^F|/g)]+e`rw@{,-69}H)",
		  1, 0 },
		{ "([(\?<=)X[:digit:]PP.[(\?#:((\?#\?#graph:])[:digit:][Q+)(N]["
		  ":alpha:]]f)[:graph:])+Elllllllllllllllll[:digit:]=)pP{uU-"
		  "20bzY|ZKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKt<c",
		  1, 0 },
		{ "[^(([^$(\?:(\?#w)[(\?::punct:]]d{-149,}[:ascii:])[:"
		  "blankcntrl:]@@@@@[@@@@@@@@@@@@@@[:graph:][:xdigit:]O[:alpha:"
		  "]2$-[:graph:])[:lowerprint:]-\?#S[:blankcntrl:][:alnum:]){-"
		  "77,}]d[:digit:]N5v+Sqqqqqqq^% "
		  "-I4]*.)^[:alnum:]"
		  "JDfjMRU7ttttttttttttjjjjjjjjjjjjjjjjjjjjjjCCCCCCCCCCCCCCCCCC"
		  "CD{,21}{0,67}[:graph:]{,208}B",
		  -1, 0 },
		{ "(%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%[:ascii:])i{}[:lowerprint:]"
		  "epxxxxxxxxxxxxxx[:lowerprint:]r-",
		  1, 0 },
		{ "([(^w(\?!)()])-s", 1, 0 },
		{ "[aIIIIIIIIIIIII(\?imsxims(\?=x:IIIIIIIm^NXXXXX(\?!("
		  "\?isximsx:XXXXXXXXXXXXXS0]F)z))+"
		  "rrrrrrrrrrrrrrrrrrrrrrrrrrrrrrr{,-237})"
		  "ZZZZZZZZZZZZZZZZZZZZZZ",
		  0, 0 },
		{ "(Z)[:alpha:]", 1, 0 },
		{ "U#Z(=)", 1, 0 },
		{ "([:lowerprint:][:punct:])1cVb*[:xdigit:]&&&&&&&&&&&&&&&&&&&&"
		  "&&&&O",
		  1, 0 },
		{ "()~K`3/[^*h[]G6[:upperw(\?()ord:]w)[:punct:]]{}", 1, 0 },
		{ "[[[]V[:digit(\?>:])|l*KKKKKKKKKKKKKKKKK,,,,,,,[,,,s.{148,}"
		  "P33333][:lo(\?<!werprin(\?!t:]ZZZZZZZZZZZZZZZZZZZZZZZ]{,-"
		  "229}{-160,}){,-211}XPPP].{}z[:alnum:][:alpha:(\?=]t{166,}"
		  "uuuuu6]i*p(m))[:space:]E|S",
		  1, 0 },
		{ "[^(h(\?(\?({#2})(\?(\?#>Q){,57}%[:digit:]"
		  "\?\?\?\?\?\?\?\?\?\?.[)]]d{)-49,}f)^O{,68})\?C",
		  0, 0 },
		{ "(}u])18621", 1, 0 },
		{ "[:as(\?=cii:][^(\?=)(S-{.F-[:punct:]3-105^[:lowerprint:]"
		  "111111111111111111111111---)][:alnum:][:ascii:]JJJJJwHSk",
		  -1, 0 },
		{ "[^3>>>>>>-sZ^^^^(\?>]Y[:di(\?(\?imxim:#git:]{-158,-102}[:"
		  "punct:]{}{87,})))[:upperword:]",
		  0, 0 },
		{ "[(\?<!^r]$W){}*[:alpha:].[:digit:]", 0, 0 },
		{ "[:ascii(\?::[^])X]-", 0, 0 },
		{ "[([^]Z)[:upperword:]N{}*[:graph:]*^", 0, 0 },
		{ "([[(\?#^[(:graph:]]){205,}[:gr(aph:]T%]^"
		  "MMMMMMMMMMMMMMMMMMMM){) <v\\[:digit:])",
		  1, 0 },
		{ "[^Y.h~b(\?<=~P{(\?=169,65}\?[^\?\?\?\?\?\?\?\?\?["
		  "\?\?\?\?\?\?\?\?\?K\"s`[yT7oP[:alpha:]{})]zrrrrrrrrrrrrrr)]"
		  "KKKKKKKKKKKKKKK[:digit:]S][:lowerprint:][:digit:]",
		  0, 0 },
		{ "(s)", 1, 0 },
		{ "[u(\?!uuuuuuuuuuuuuuuuuuuu[:digit:]{,48}[:graph:]WL[:alnum:]"
		  "]v=_)VN>{AjBBBBBBBBBBBBBBBBBBBBBBB[:upperword:]`'W)",
		  0, 0 },
		{ "[^([[()DN1[^][|]\?]{-104,}])[:space:]][:lowerprint:]r[:"
		  "alpha:].DU",
		  0, 0 },
		{ "[^((33333333333333333333333(\?<=3333333D))"
		  "kkkkkkkkkkkkkkkkkkkkkkk[k[:alpha:]])]X+",
		  0, 0 },
		{ "[({,-17})[@e{220,(\?#41}])]]{-213,-225}", 0, 0 },
		{ "[[^(\?#[(\?:^[[(\?(^]))]])]vvvvvvvvvvvvvvvvvvvvv{,96}|m]{-"
		  "79,248}[:alpha:])",
		  0, 0 },
		{ "([[(\?imsisx:^}$,-[:al(\?>num:]Xqqqqqqqqqqqq{-185,154}]b#+T)"
		  "{-241,})A{-27}[(\?<!:lowerprint:]X)[:punct:]ME-]+"
		  "BBBBBBBBBBBBBBBBa|{-40}M8mhgD 0HU]{16})",
		  -1, 0 },
		{ "[^(\?>([\?()(\?#))]--R1rk^UnP.[(\?!:digit:]])^)[:upperword:]"
		  "{}0000000000000000000000000000000~U{-139,-19}z<L-228",
		  0, 0 },
		{ "()-:=3uE$[:alnum:]bP%{-210,}", 1, 0 },
		{ "(U)7777]]]]]]]]]]]]]]]]]]]]]]]]]]]]]c::AA[:alpha:]{,3}f1{"
		  "NzH@3lTf{}{",
		  1, 0 },
		{ "[C{(\?>})RR(\?=R<]p'N~&.-})6]", 0, 0 },
		{ "[^\?[^(\?(lFt]).[^7Q-])kkkkkkkkkkkk]XTFy\"1Deiv!,'xVK", 0,
		  0 },
		{ "[^$[^[:xdigit:](\?{*{245,99}h8v(\?!)]]u)Z[:punct:]})[:alnum:"
		  "]+|[:blankcntrl:]u{}[:lowerprint:]+bBJ4+k-v{-116}",
		  0, 0 },
		{ "S)f{,180}[:graph:]&{12,244}", 0, 0 },
		{ "(([[(.()[^^{80(\?>(\?<=,235})ddddddddd[^ddddddddd(\?<=d.__B{"
		  "36}````````````````(\?:```(\?>```````,,,,,,,(\?:,,)P$U,[:"
		  "xdigit:])zzzzzzzzzzzzz]UUUU[uB]n<&[(:ascii:].][:alnum:])\?S]"
		  "{})d{138,}s9========[:lowerprint:]]OOOOOOOOOOOOOOO|"
		  "yyyyyyyyyyyyyyy$LZ[:lowerprint:]EEEEEEE[:ascii:][:punct:]"
		  "VpP^{-48}D){,46}x))2P))a[:lowerprint:]r",
		  2, 0 },
		{ "[^(((\?<!):())PPPPPPPPPPPPPPP(\?=[PPPPPPP(\?{PPPPPPPP$)})"
		  "77777777777777777]{,-57}::::::::::::(::::::::::::::::)]g{89}"
		  "__________________[:xdigit:]l[:punct:])N",
		  1, 0 },
		{ ":02-k\?p3I7aEhJ\\265-[:space:]pP[:space:]x0F[:alnum:]aM4[:"
		  "lowerprint:]sA@@@@@@@@@@@@@@@@@@@@@@@@@@@@",
		  -1, 1 },
		{ "a[:upper(\?{word:]})X{-173,}-2F[:lowerprint:]", 0, 0 },
		{ "u,w<g*Q002S{,130}{239}[:lower(print:]cr{-165,}#$k<L/"
		  "&)[:blankcntrl:]aaaaaaaaaaaaaaaaaaaaaa[:ascii:]",
		  0, 0 },
		{ "(xFA^{-161,93})U[:xdigit:]", 1, 0 },
		{ "[^(\?=]{})mE`", 0, 0 },
		{ "[[((\?(\?#:alnum:]])x6CS[:digit:]{-197,}.)N", 0, 0 },
		{ "[^(\?![])C*[:upp(erword:])-176]", 0, 0 },
		{ "[[^[[^[55555555555555555555555555(\?>555(\?<!555)S][]]A[:l("
		  "\?>owerp(rint:]])]*",
		  0, 0 },
		{ "Au)khgzAfXIZoZ=g[:digit:]){,186}Upvf=x<]Tbd5Rq\?.", 0, 0 },
		{ "b{-176,}B^[:bla(\?(<!nkcntrl:]{-6,133}#B "
		  ":)<<<<<<<<<<<<<<<<<<<)[:alnum:]$}}}}}}}}}}}}}}}}}}}}}}}[:"
		  "xdigit:]tw",
		  0, 0 },
		{ "(4IIIII(IIIIIIIIIIIIIIIII{})W{-152,-238}){,-56}^{-142,}", 2,
		  0 },
		{ "[^([[(\?(\?(!)>>>>>>>>>>>>>(>>>>>>>>D)Ix{(1(\?imxmsx:762)c})"
		  ")A)[[[[[[[[[[[[[[5Rp]DDDDDDDDDDDDDDDDDDDD]Us+\\w[:digit:]{-"
		  "47}[:xdigit:][:blankcntrl:])ddddddddddddddd[^ddddddddddddd[:"
		  "digit:]|]]*{-165,-230}{-212}{53,}]\?",
		  0, 0 },
		{ "[^[^]]|[:(\?:alnum:])}}}}}}}}}}}}}}}}}}}}", 0, 0 },
		{ "VVVVVVVVVVVVVVVVVVVVVVVVVVVV[:d(i(\?#git:])){{{{{{[:digit:]"
		  "ZfQ55555555{}Z",
		  0, 0 },
		{ "[L][:blankcnt(\?((\?=rl:(\?=]){-35,[^}){)eJb>>>>>>>>>>>>>>>>"
		  ">>>>>>$ [:xdigit:]l0Tv2Tw2@C[:space:]Zc/{*)>]N3j~.dMBBBB",
		  0, 0 },
		{ "[[^(\?>(([]))])[:graph:]]{65,}as#Q:lQ", 0, 0 },
		{ "[^[fPPUUUUUUUUUUU(\?#UUU[^UUUUUU(\?<="
		  "UUUUUUUUUGGGGGGGGGGGGGGGGGGG((\?{\?=GGGGGG.MK))+]+)&UxFW)"
		  "rwv\?@D.",
		  0, 0 },
		{ "{-(60,})m", 1, 0 },
		{ "b[(])^w", 0, 0 },
		{ "[][^qVs(\?:(p])X)\?'", 0, 0 },
		{ "()8", 1, 0 },
		{ "(t[:punc[^t:(\?{][:blankcntrl:])})[^8\?]z*]", 1, 0 },
		{ "[:lowerprint:])[:graph:]lppppppppppppppppppppppppppppf", 0,
		  0 },
		{ "[:alph(a:])[:ascii:]g +z-Bc-U{,%Gk", 0, 0 },
		{ "u[:graph:(\?=]*)W:::", 0, 0 },
		{ "([:alnum(:])l)", 1, 0 },
		{ "[[[}}}}}}}}(\?<!}}}}}}}+(\?{),,,,,,,,,,,,,,(\?!,,,,,,,,]"
		  "99999999999&R[:ascii:]ZZZZ-{-10,}{96}Ed*][:graph:])]}){}{}G{"
		  "-9,}",
		  0, 0 },
		{ "([^[{}]]Z[[^:graph:]{-47}55555555555555555555555555555[:"
		  "ascii:]s]6,$:3qAew1Y)+)[:punct:]",
		  1, 0 },
		{ "[[[[[([[[[[[[[[[[[[[[[[[[[[[[[8!1i]')", 0, 0 },
		{ "([((\?(\?#>)(\?{,)At]%M9FSq5)EB", 1, 0 },
		{ "(}````````````````(``{210,})[:(\?#space:]P[:digit:])PP.{-"
		  "227,}$pK~mm ImR|{,51}[:alnum:]<)[:alpha:]",
		  2, 0 },
		{ "[^(\?<=])[:digit:]", 0, 0 },
		{ "[^'''''''{(\?:178,}e{,16}$QQQQQQQQQQQQQQQQQQQQQQQ$])", 0,
		  0 },
		{ "[^(\?>@K*)(\?#d18]{78,}B)[:digit:]{-193,}=wg{,59}", 0, 0 },
		{ "[^.{156,}!(\?<=!!!!!!!!!!!!!!(\?{!(\?(!!!!!!!!!!!!!)})"
		  "TTTTTTTTTTTTTTTTTTTTTTTTTTTTT[^}}}}}}}}}}}})}}}}}}}}}}}}}]])"
		  "{}^L#%-{}FC",
		  0, 0 },
		{ "(eeeee{-169,-100}-fa[:upperword:]N)$Nellllllllllllll", 1,
		  0 },
		{ "[[(\?!())\?[(\?!:alnum:]e{,28}M])[:punct:]"
		  "CCCCCCCCCCCCCCCCCCCC]{-150,}{-167}",
		  0, 0 },
		{ "[[@[@(\?#@[@]P]Z{')]{-186,117}]+)7f-", 0, 0 },
		{ "\\Q+kD}]AEM)u ", 0, 0 },
		{ "([(\?{(\?=:::::::::::::&){,210}]^})P{-31,}8[:space:]C[:"
		  "alnum:][:a(scii:]z|[:upperword:])[:alnum:][:graph:])zr~Zk",
		  1, 0 },
		{ ".[:space:]e[:g(\?{(\?{raph:]})})@@@@@@@@@@@@@wb|~k", 0, 0 },
		{ "()ooooooooo\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"[:graph:]", 1,
		  0 },
		{ "[^64h(\?(@Eyw][:xdi[git:]pP%%%%%u(uuu[:up[perword:]`8Utdh{)}"
		  "]]))lW[:punct:]W.hhhhhhhhhhhhhhhhhhhhhhhh'm<<}O8`ZXtG.$",
		  1, 0 },
		{ "BPP[:digit:]bbbbbbbbbbb(bb)S+[:alnum:]", 1, 0 },
		{ "um.[:ascii((\?#\?!:])*)+KKKKKKKKKKKKKKKKKKKKKKKKKS.=<Bf", 0,
		  0 },
		{ "", -1, 0 },
		{ "(()$[:lowerprint:][:s[pace:]2]bbbbbbbbbyoooooooooooooooooo*{"
		  "39,}$')qV`AcH>,eDl",
		  -1, 0 },
		{ "(()[^])e{-241,}", -1, 0 },
		{ "()[:alpha:]rliiiiiiii[:alnum:]Mb*QW9N.>\?{115,}&u*j", -1,
		  0 },
		{ "()[]p", -1, 0 },
		{ "(I[^]pfL)$[:punct:]", -1, 0 },
		{ "([])>>>>>>>>>>[:alnum:]", -1, 0 },
		{ "([])O\\\\\\\\\\\\\\fffffffffffffffffffffff=s6jCZy/"
		  "b+ir2'*{151,}",
		  -1, 0 },
		{ "([])nnnnnnnnnnnnnnnnnnnnnnnnnn[:xdigit:]^N$f", -1, 0 },
		{ "([]M)[:lowerprint:]a(pg$Z[:punct:])77777777777.", -1, 0 },
		{ "([]XXXXXXXXXXXXXXXXXXXXXX-===========)", -1, 0 },
		{ "([]lkX{-224}[:blankcntrl:]$gPKIZlSC#F@XX "
		  "I'^}{234}yZm)uuuuuuuuuuuuuuuuuuuuuurS",
		  -1, 0 },
		{ "([^0kYkg9])IIIIIIIIIIIIIIIIIIIIII/"
		  "{(192,-118}l+FoSD6\?A)c[:xdigit:]`````````````````e-{-4,-"
		  "170}x{4620}Z[:upperword:]",
		  -1, 0 },
		{ "([^[^[^()(\?>){}B]XYF+#[:alpha:]{-85((,-55[^}t]n).{,-33}]]("
		  "bQJ!|O+{175,})RFh)Z+^.{137,}:VpP[:alpha:]-MceqVVkkkk("
		  "kkkkkkkkkkkkkkkkkk)"
		  "\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?{-"
		  "115,-67})``````````````````````````````",
		  -1, 0 },
		{ "([^]EzU[:alnum:]+^^^^^^^^^^^^^^^^^^^)[:xdigit:]HHHHHHHH$"
		  "66666666666666666666666666666666UUUUUUUUUUUUUUUUUUUUL{}iiii{"
		  "-76}X",
		  -1, 0 },
		{ "([^]~~~~~~~~~~{240,})]NOp", -1, 0 },
		{ "(sb)[:digit:]VVVVVVVVx{9569}52,|]", -1, 0 },
		{ "(x{19762}){}", -1, 0 },
		{ "-[:xdigit:][]", -1, 0 },
		{ "121|", -1, 0 },
		{ "141[:xdigit:][:lowerprint:]{24}{59,191}[:digit:]/", -1, 0 },
		{ "G[^],,,,,,,,,,,,,+\"DiX", -1, 0 },
		{ "Gm(ho9:\"8{-188,-200}Z[:blankcntrl:]{,171}"
		  "\?\?\?\?\?\?\?\?\?\?\?[:blankcntrl:]LLLLLLLLLLLLLLLLLLLLLLL{"
		  "}^[:graph:][:blankc(\?#ntrl:])w",
		  -1, 0 },
		{ "N\"\"\"\"\"\"\"-------------------------|[:alnum:]"
		  "AAAAAAAAAAAAAAAAAAAAf\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?",
		  -1, 0 },
		{ "U{-30,}^\?\?\?", -1, 0 },
		{ "W^*04rAY(Ee*>[^o3[]]_)", -1, 0 },
		{ "X[^]}*C[:alnum:]", -1, 0 },
		{ "[${,-3}]+^\?[|x8A|][:space:]'''''['''''"
		  "JJJJJJJJJJJJJJJJJJJJJJJJJJJJJyl}.Y7G]",
		  -1, 0 },
		{ "[()&[&&&]\?\?["
		  "\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?"
		  "pg%k8ug`Wqk4|NR{h[CK5Ez=]jHpQw&`{:]{,91}D",
		  -1, 0 },
		{ "[(\?#(\?:)[)([\?>)(\?>(\?:[:alnum:])]G]{85}[^)w]N]gYrUs|",
		  -1, 0 },
		{ "[(\?<=)[:digit:]\?]{152,}VR|", -1, 0 },
		{ "[****(\?>**********(\?<!*******Q)Vr){[^25,}*:"
		  "FFFFFFFFFFFFFFFFFFFFFFFF(\?{FFFF(({}D]|",
		  -1, 0 },
		{ "[:ascii:]+{124,}:*]\?$-{92}D[:lowerprint:]``````````````````"
		  "```",
		  -1, 0 },
		{ "[:ascii:]\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?A<", -1, 0 },
		{ "[:blankcntrl:]p\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?$"
		  "\?TTTTTTTTTTTTTTTTT[:ascii:][:upperword:]",
		  -1, 0 },
		{ "[:punct:]{254}DDDDDDDDDDDDDDD@[:alpha:]Z\?\?-----R", -1, 0 },
		{ "[:upperword:]J\?\?nqCAdfyW5", -1, 0 },
		{ "[:upperword:]{-39}|", -1, 0 },
		{ "[:xdigit:]^\?", -1, 0 },
		{ "[Z*e ]NdmP\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?", -1, 0 },
		{ "[[:punct:]q]ex{15625}-", -1, 0 },
		{ "[[[^([^L((\?{b(\?=C\?]-134{,-207}[:ascii:]Hz}XIz}|", -1, 0 },
		{ "[[^V(\?:(\?<!(\?>))TTTTTTTTTTTTTTTTTTTTTTT)[:punct:][:digit:"
		  "]]GGGGGGGGGGGGGGGGGGGGG,]|.{-224}{96}{239,}1",
		  -1, 0 },
		{ "[[^^PP]{,-222}{182}{141}]zFD}-.", -1, 0 },
		{ "[] Hn&[:xdigit:][:upperword:]f", -1, 0 },
		{ "[]$.B", -1, 0 },
		{ "[]&&&&&&&&&&&&&&&&&&&&&&&", -1, 0 },
		{ "[]()[:xdigit:]er063{132,140}$", -1, 0 },
		{ "[]+1434", -1, 0 },
		{ "[]-", -1, 0 },
		{ "[]-#yyK", -1, 0 },
		{ "[]-(S$5)AxbdTKO[:alnum:]", -1, 0 },
		{ "[]2883", -1, 0 },
		{ "[]2dhd-[:alpha:]"
		  "sssssssssssssssss55555555555555555555555555555555Z[:punct:]",
		  -1, 0 },
		{ "[]4", -1, 0 },
		{ "[]44444444444444444G", -1, 0 },
		{ "[]\?", -1, 0 },
		{ "[]A", -1, 0 },
		{ "[]Gap8bc", -1, 0 },
		{ "[]OOOO", -1, 0 },
		{ "[]PP", -1, 0 },
		{ "[]QQ", -1, 0 },
		{ "[]WaFaGO,o", -1, 0 },
		{ "[]Z", -1, 0 },
		{ "[][:alpha:]|[:digit:]Ls$I-Ff~+xA3e", -1, 0 },
		{ "[][:ascii:]-218", -1, 0 },
		{ "[][:ascii:]N}}}}}}}}}}}}}}}-{137,}8682", -1, 0 },
		{ "[][:lowerprint:]Ur", -1, 0 },
		{ "[][:space:]15097", -1, 0 },
		{ "[][:xdigit:]", -1, 0 },
		{ "[]dpSSSSSSSS", -1, 0 },
		{ "[]e13768", -1, 0 },
		{ "[]gT", -1, 0 },
		{ "[]h", -1, 0 },
		{ "[]n", -1, 0 },
		{ "[]vvvvvvvvvvvvvvvvvvvvvvvvvv*[:xdigit:]", -1, 0 },
		{ "[]{,-212}1111111111111111111C3821", -1, 0 },
		{ "[]{-128,}hc", -1, 0 },
		{ "[]{-181,}&[:xdigit:].\?}}}}}}}}}}}}}}}}}}}}}}", -1, 0 },
		{ "[]{}F&}i`7|ZAH", -1, 0 },
		{ "[^(\?())u{196,}pP][r^ndddddddddddddddddddddd]{31,246}\?J",
		  -1, 0 },
		{ "[^.ii.1-S]lwwwwwwwwwwwwwwwwww[^wwwwwwwwwwwwww[:alnum:]DOpP+<"
		  "N][^]44{179}{-194,56}",
		  -1, 0 },
		{ "[^2[:alnum:]]\?t\?\?", -1, 0 },
		{ "[^[((\?{[^^<<<<(\?(\?<!{)})(\?<!]{,184}{-213}|", -1, 0 },
		{ "[^[^[]\?{89,}PPsvf{[:space:]]]vd{161,}", -1, 0 },
		{ "[^[^].]+{0}s", -1, 0 },
		{ "[^]${}", -1, 0 },
		{ "[^]([:punct:]),%[:xdigit:]w^0\?{-233}", -1, 0 },
		{ "[^]-", -1, 0 },
		{ "[^].^", -1, 0 },
		{ "[^]6743", -1, 0 },
		{ "[^]JD", -1, 0 },
		{ "[^]N=[:upperword:]zzzzzzzzzzzzzzzzz.", -1, 0 },
		{ "[^]OLz_6", -1, 0 },
		{ "[^]PP[:digit:]0eBEx=", -1, 0 },
		{ "[^]SHzuKp", -1, 0 },
		{ "[^][:upperword:]{111}-TpmXw", -1, 0 },
		{ "[^]^''''''''z{-73,}", -1, 0 },
		{ "[^]^{,141}e", -1, 0 },
		{ "[^]aaaaaaaaaaaaaaaaaaa{-98,43}", -1, 0 },
		{ "[^]f", -1, 0 },
		{ "[^]l", -1, 0 },
		{ "[^]n\"Wt", -1, 0 },
		{ "[^]pPZ\?q+m0LJ+", -1, 0 },
		{ "[^]p[:upperword:]L:", -1, 0 },
		{ "[^]q\?{,-18}-", -1, 0 },
		{ "[^]s[:space:(\?<=]$", -1, 0 },
		{ "[^]{,58}t", -1, 0 },
		{ "[^]{255,}JJJJJJJJJJJJJJJJJJJJJJJJJJ", -1, 0 },
		{ "[^]{45}", -1, 0 },
		{ "[^]{W", -1, 0 },
		{ "[^]{}{-22}", -1, 0 },
		{ "[^]{}{}{}[:xdigit:]+", -1, 0 },
		{ "[^]|9{,-108}{}.LVIJJJJJJJJJJJJJJJPP", -1, 0 },
		{ "[^{,-254}]|", -1, 0 },
		{ "[o(\?{(\?<=}[))f++++++++++++++++"
		  "777777777777777777777777yzPPs]"
		  "\?\?dRRRRRRRRRRRRRRRRRRRRRRRRRRRR&]>%fffffffffff",
		  -1, 0 },
		{ "aW|", -1, 0 },
		{ "cT{}[]C^r2``tm", -1, 0 },
		{ "kkkkkkkkkkkkkkkkkkkkkkk[:blankcntrl:]|{}3{26,}{151,}[:punct:"
		  "]JJJlH$gP%(2WUE%%%%%%%%%%%%%%%%%%%%a){ibf{}\?",
		  -1, 0 },
		{ "lZ\?\?\?\?\?\?\?\?\?\?\?-P2eZt[:punct:]", -1, 0 },
		{ "vF3qn[^]N.", -1, 0 },
		{ "wwwwwwwwwwwwww{-176,}275[^]>."
		  "UUUUUUUUUUUUUUUUUUUUeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee2$Yd",
		  -1, 0 },
		{ "{-197,223}bf]]]]]]]]]]\?&}/s\?\?~c", -1, 0 },
		{ "{-37,}EpP|", -1, 0 },
		{ "{}@]a[][:xdigit:]z{a", -1, 0 },
		{ "}02|", -1, 0 },
		{ "}}}}}}}}}(}}){}[llll]^N|", -1, 0 },
	};
	unsigned int i;
	int r;

	UNUSED(state);

#ifdef HAVE_REGEX_H
	/*
	 * Check if we get the expected response.
	 */
	for (i = 0; i < sizeof(tests) / sizeof(*tests); i++) {
		regex_t preg;

		memset(&preg, 0, sizeof(preg));
		r = regcomp(&preg, tests[i].expression, REG_EXTENDED);
		if (((r != 0 && tests[i].expect != -1) ||
		     (r == 0 && tests[i].expect == -1)) &&
		    !tests[i].exception)
		{
		} else if (r == 0 &&
			   preg.re_nsub != (unsigned int)tests[i].expect &&
			   !tests[i].exception)
		{
			tests[i].expect = preg.re_nsub;
		}
		if (r == 0) {
			regfree(&preg);
		}
	}
#endif /* ifdef HAVE_REGEX_H */

	/*
	 * Check if we get the expected response.
	 */
	for (i = 0; i < sizeof(tests) / sizeof(*tests); i++) {
		r = isc_regex_validate(tests[i].expression);
		if (r != tests[i].expect) {
			print_error("# %s -> %d expected %d\n",
				    tests[i].expression, r, tests[i].expect);
		}
		assert_int_equal(r, tests[i].expect);
	}
}

ISC_TEST_LIST_START

ISC_TEST_ENTRY(regex_validate)

ISC_TEST_LIST_END

ISC_TEST_MAIN
