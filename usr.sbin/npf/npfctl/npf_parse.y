/*-
 * Copyright (c) 2011-2025 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Martin Husemann, Christos Zoulas and Mindaugas Rasiukevicius.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

%{

#include <err.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __NetBSD__
#include <vis.h>
#endif

#include "npfctl.h"

#define	YYSTACKSIZE	4096

int			yystarttoken;
const char *		yyfilename;

extern int		yylineno, yycolumn;
extern int		yylex(int);

void
yyerror(const char *fmt, ...)
{
	extern int yyleng;
	extern char *yytext;

	char *msg, *context = estrndup(yytext, yyleng);
	bool eol = (*context == '\n');
	va_list ap;

	va_start(ap, fmt);
	vasprintf(&msg, fmt, ap);
	va_end(ap);

	fprintf(stderr, "%s:%d:%d: %s", yyfilename,
	    yylineno - (int)eol, yycolumn, msg);
	if (!eol) {
#ifdef __NetBSD__
		size_t len = strlen(context);
		char *dst = ecalloc(1, len * 4 + 1);

		strvisx(dst, context, len, VIS_WHITE|VIS_CSTYLE);
		context = dst;
#endif
		fprintf(stderr, " near '%s'", context);
	}
	fprintf(stderr, "\n");
	exit(EXIT_FAILURE);
}

%}

/*
 * No conflicts allowed.  Keep it this way.
 */
%expect 0
%expect-rr 0

/*
 * Depending on the mode of operation, set a different start symbol.
 * Workaround yacc limitation by passing the start token.
 */
%start input
%token RULE_ENTRY_TOKEN MAP_ENTRY_TOKEN
%lex-param { int yystarttoken }

/*
 * General tokens.
 */
%token			ALG
%token			ALGO
%token			ALL
%token			ANY
%token			APPLY
%token			ARROWBOTH
%token			ARROWLEFT
%token			ARROWRIGHT
%token			BLOCK
%token			CDB
%token			CONST
%token			CURLY_CLOSE
%token			CURLY_OPEN
%token			CODE
%token			COLON
%token			COMMA
%token			DEFAULT
%token			TDYNAMIC
%token			TSTATIC
%token			EQ
%token			ETHER
%token			EXCL_MARK
%token			TFILE
%token			FLAGS
%token			FROM
%token			GROUP
%token			GT
%token			HASH
%token			ICMPTYPE
%token			ID
%token			IFADDRS
%token			IN
%token			INET4
%token			INET6
%token			INTERFACE
%token			INVALID
%token			IPHASH
%token			IPSET
%token			IRG
%token			LPM
%token			L2
%token			LT
%token			MAP
%token			NEWLINE
%token			NO_PORTS
%token			MINUS
%token			NAME
%token			NETMAP
%token			NPT66
%token			ON
%token			OFF
%token			OUT
%token			PAR_CLOSE
%token			PAR_OPEN
%token			PASS
%token			PCAP_FILTER
%token			PORT
%token			PROCEDURE
%token			PROTO
%token			FAMILY
%token			FINAL
%token			FORW
%token			RETURN
%token			RETURNICMP
%token			RETURNRST
%token			ROUNDROBIN
%token			RULESET
%token			SEMICOLON
%token			SET
%token			SLASH
%token			STATEFUL
%token			STATEFUL_ALL
%token			TABLE
%token			TCP
%token			TO
%token			TREE
%token			TYPE
%token			USER
%token			XRG
%token	<num>		ICMP
%token	<num>		ICMP6

%token	<num>		HEX
%token	<str>		ETHERHEX
%token	<str>		IDENTIFIER
%token	<str>		IPV4ADDR
%token	<str>		IPV6ADDR
%token	<str>		MACADDR
%token	<num>		NUM
%token	<fpnum>		FPNUM
%token	<str>		STRING
%token	<str>		PARAM
%token	<str>		TABLE_ID
%token	<str>		VAR_ID

%type	<str>		addr some_name table_store dynamic_ifaddrs
%type	<str>		proc_param_val opt_apply ifname on_ifname ifref
%type	<num>		port opt_final number afamily opt_family
%type	<num>		block_or_pass rule_dir group_dir block_opts
%type	<num>		maybe_not opt_stateful icmp_type table_type
%type	<num>		map_sd map_algo map_flags map_type layer
%type	<num>		param_val op_unary op_binary
%type	<etype>		ether_type
%type	<rid>		uid gid
%type	<var>		static_ifaddrs filt_addr_element
%type	<var>		filt_port filt_port_list port_range icmp_type_and_code
%type	<var>		filt_addr addr_and_mask tcp_flags tcp_flags_and_mask
%type	<var>		procs proc_call proc_param_list proc_param mac_addr
%type	<var>		element list_elems list_trail list value filt_addr_list
%type	<var>		opt_proto proto proto_elems
%type	<addrport>	mapseg
%type	<uid>		uids uid_item user_id
%type	<gid>		gids gid_item group_id
%type	<filtopts>	filt_opts all_or_filt_opts l2_filt_opts l2_all_of_filt_opts
%type	<optproto>	rawproto
%type	<rulegroup>	group_opts

%union {
	char *		str;
	uint16_t	etype;
	unsigned long	num;
	uint32_t	rid;
	double		fpnum;
	npfvar_t *	var;
	addr_port_t	addrport;
	filt_opts_t	filtopts;
	opt_proto_t	optproto;
	rule_group_t	rulegroup;
	struct r_id	uid;
	struct r_id	gid;
}

%%

input
	: lines
	| RULE_ENTRY_TOKEN	rule
	| MAP_ENTRY_TOKEN	map
	;

lines
	: lines sepline line
	| line
	;

line
	: vardef
	| table
	| map
	| group
	| rproc
	| alg
	| set
	|
	;

alg
	: ALG STRING
	{
		npfctl_build_alg($2);
	}
	;

sepline
	: NEWLINE
	| SEMICOLON
	;

param_val
	: number	{ $$ = $1; }
	| ON		{ $$ = true; }
	| OFF		{ $$ = false; }
	;

set
	: SET PARAM param_val {
		npfctl_setparam($2, $3);
	}
	;

/*
 * A value - an element or a list of elements.
 * Can be assigned to a variable or used inline.
 */

vardef
	: VAR_ID EQ value
	{
		npfvar_add($3, $1);
	}
	;

value
	: element
	| list
	;

list
	: CURLY_OPEN opt_nl list_elems CURLY_CLOSE
	{
		$$ = $3;
	}
	;

list_elems
	: element list_trail
	{
		$$ = npfvar_add_elements($1, $2);
	}
	;

element
	: IDENTIFIER
	{
		$$ = npfvar_create_from_string(NPFVAR_IDENTIFIER, $1);
	}
	| STRING
	{
		$$ = npfvar_create_from_string(NPFVAR_STRING, $1);
	}
	| number MINUS number
	{
		$$ = npfctl_parse_port_range($1, $3);
	}
	| number
	{
		uint32_t val = $1;
		$$ = npfvar_create_element(NPFVAR_NUM, &val, sizeof(val));
	}
	| VAR_ID
	{
		$$ = npfvar_create_from_string(NPFVAR_VAR_ID, $1);
	}
	| TABLE_ID		{ $$ = npfctl_parse_table_id($1); }
	| dynamic_ifaddrs	{ $$ = npfctl_ifnet_table($1); }
	| static_ifaddrs	{ $$ = $1; }
	| addr_and_mask		{ $$ = $1; }
	| mac_addr		{ $$ = $1; }
	;

list_trail
	: element_sep element list_trail
	{
		$$ = npfvar_add_elements($2, $3);
	}
	| opt_nl 		{ $$ = NULL; }
	| element_sep 		{ $$ = NULL; }
	;

element_sep
	: opt_nl COMMA opt_nl
	;

opt_nl
	: opt_nl NEWLINE
	|
	;

/*
 * Table definition.
 */

table
	: TABLE TABLE_ID TYPE table_type table_store
	{
		npfctl_build_table($2, $4, $5);
	}
	;

table_type
	: IPSET		{ $$ = NPF_TABLE_IPSET; }
	| HASH
	{
		warnx("warning - table type \"hash\" is deprecated and may be "
		    "deleted in\nthe future; please use the \"ipset\" type "
		    "instead.");
		$$ = NPF_TABLE_IPSET;
	}
	| LPM		{ $$ = NPF_TABLE_LPM; }
	| TREE
	{
		warnx("warning - table type \"tree\" is deprecated and may be "
		    "deleted in\nthe future; please use the \"lpm\" type "
		    "instead.");
		$$ = NPF_TABLE_LPM;
	}
	| CONST		{ $$ = NPF_TABLE_CONST; }
	| CDB
	{
		warnx("warning -- table type \"cdb\" is deprecated and may be "
		    "deleted in\nthe future; please use the \"const\" type "
		    "instead.");
		$$ = NPF_TABLE_CONST;
	}
	;

table_store
	: TFILE STRING	{ $$ = $2; }
	| TDYNAMIC
	{
		warnx("warning - the \"dynamic\" keyword for tables is obsolete");
		$$ = NULL;
	}
	|		{ $$ = NULL; }
	;

/*
 * Map definition.
 */

map_sd
	: TSTATIC	{ $$ = NPFCTL_NAT_STATIC; }
	| TDYNAMIC	{ $$ = NPFCTL_NAT_DYNAMIC; }
	|		{ $$ = NPFCTL_NAT_DYNAMIC; }
	;

map_algo
	: ALGO NETMAP		{ $$ = NPF_ALGO_NETMAP; }
	| ALGO IPHASH		{ $$ = NPF_ALGO_IPHASH; }
	| ALGO ROUNDROBIN	{ $$ = NPF_ALGO_RR; }
	| ALGO NPT66		{ $$ = NPF_ALGO_NPT66; }
	|			{ $$ = 0; }
	;

map_flags
	: NO_PORTS	{ $$ = NPF_NAT_PORTS; }
	|		{ $$ = 0; }
	;

map_type
	: ARROWBOTH	{ $$ = NPF_NATIN | NPF_NATOUT; }
	| ARROWLEFT	{ $$ = NPF_NATIN; }
	| ARROWRIGHT	{ $$ = NPF_NATOUT; }
	;

mapseg
	: filt_addr filt_port
	{
		$$.ap_netaddr = $1;
		$$.ap_portrange = $2;
	}
	;

map
	: MAP ifref map_sd map_algo map_flags mapseg map_type mapseg
	  PASS opt_family opt_proto all_or_filt_opts
	{
		npfctl_build_natseg($3, $7, $5, $2, &$6, &$8, $11, &$12, $4);
	}
	| MAP ifref map_sd map_algo map_flags mapseg map_type mapseg
	{
		npfctl_build_natseg($3, $7, $5, $2, &$6, &$8, NULL, NULL, $4);
	}
	| MAP ifref map_sd map_algo map_flags proto mapseg map_type mapseg
	{
		npfctl_build_natseg($3, $8, $5, $2, &$7, &$9, $6, NULL, $4);
	}
	| MAP RULESET group_opts
	{
		npfctl_build_maprset($3.rg_name, $3.rg_attr, $3.rg_ifname);
	}
	;

/*
 * Rule procedure definition and its parameters.
 */

rproc
	: PROCEDURE STRING CURLY_OPEN procs CURLY_CLOSE
	{
		npfctl_build_rproc($2, $4);
	}
	;

procs
	: procs sepline proc_call
	{
		$$ = npfvar_add_elements($1, $3);
	}
	| proc_call	{ $$ = $1; }
	;

proc_call
	: IDENTIFIER COLON proc_param_list
	{
		proc_call_t pc;

		pc.pc_name = estrdup($1);
		pc.pc_opts = $3;

		$$ = npfvar_create_element(NPFVAR_PROC, &pc, sizeof(pc));
	}
	|		{ $$ = NULL; }
	;

proc_param_list
	: proc_param_list COMMA proc_param
	{
		$$ = npfvar_add_elements($1, $3);
	}
	| proc_param	{ $$ = $1; }
	|		{ $$ = NULL; }
	;

proc_param
	: some_name proc_param_val
	{
		proc_param_t pp;

		pp.pp_param = estrdup($1);
		pp.pp_value = $2 ? estrdup($2) : NULL;

		$$ = npfvar_create_element(NPFVAR_PROC_PARAM, &pp, sizeof(pp));
	}
	;

proc_param_val
	: some_name	{ $$ = $1; }
	| number	{ (void)asprintf(&$$, "%ld", $1); }
	| FPNUM		{ (void)asprintf(&$$, "%lf", $1); }
	|		{ $$ = NULL; }
	;

/*
 * Group and dynamic ruleset definition.
 */

group
	: GROUP group_opts
	{
		/* Build a group.  Increase the nesting level. */
		npfctl_build_group($2.rg_name, $2.rg_attr,
		    $2.rg_ifname, $2.rg_default);
	}
	  ruleset_block
	{
		/* Decrease the nesting level. */
		npfctl_build_group_end();
	}
	;

ruleset
	: RULESET group_opts
	{
		/* Ruleset is a dynamic group. */
		npfctl_build_group($2.rg_name, $2.rg_attr | NPF_RULE_DYNAMIC,
		    $2.rg_ifname, $2.rg_default);
		npfctl_build_group_end();
	}
	;

group_dir
	: FORW		{ $$ = NPF_RULE_FORW; }
	| rule_dir
	;

group_opts
	: DEFAULT layer
	{
		memset(&$$, 0, sizeof(rule_group_t));
		$$.rg_default = true;
		$$.rg_attr |= $2;
	}
	| STRING group_dir on_ifname layer
	{
		memset(&$$, 0, sizeof(rule_group_t));
		$$.rg_name = $1;
		$$.rg_attr = $2 | $4;
		$$.rg_ifname = $3;
	}
	;

layer
	: L2 { $$ = NPF_RULE_LAYER_2; }
	| 	{ $$ =  NPF_RULE_LAYER_3; } /* ret layer3 by defualt */
	;

ruleset_block
	: CURLY_OPEN ruleset_def CURLY_CLOSE
	;

ruleset_def
	: ruleset_def sepline rule_group
	| rule_group
	;

rule_group
	: rule
	| group
	| ruleset
	|
	;

/*
 * Rule and misc.
 */

rule
	: block_or_pass opt_stateful rule_dir opt_final on_ifname
	  opt_family opt_proto all_or_filt_opts opt_apply
	{
		npfctl_build_rule($1 | $2 | $3 | $4, $5,
		    $6, $7, &$8, NULL, $9);
	}
	| block_or_pass opt_stateful rule_dir opt_final on_ifname
	  PCAP_FILTER STRING opt_apply
	{
		npfctl_build_rule($1 | $2 | $3 | $4, $5,
		    AF_UNSPEC, NULL, NULL, $7, $8);
	}
	| block_or_pass ETHER rule_dir opt_final on_ifname
		l2_all_of_filt_opts
	{
		npfctl_build_rule($1 | $3 | $4, $5, 0, NULL, &$6, NULL, NULL);
	}
	;

block_or_pass
	: BLOCK block_opts	{ $$ = $2; }
	| PASS			{ $$ = NPF_RULE_PASS; }
	;

rule_dir
	: IN			{ $$ = NPF_RULE_IN; }
	| OUT			{ $$ = NPF_RULE_OUT; }
	|			{ $$ = NPF_RULE_IN | NPF_RULE_OUT; }
	;

opt_final
	: FINAL			{ $$ = NPF_RULE_FINAL; }
	|			{ $$ = 0; }
	;

on_ifname
	: ON ifref		{ $$ = $2; }
	|			{ $$ = NULL; }
	;

afamily
	: INET4			{ $$ = AF_INET; }
	| INET6			{ $$ = AF_INET6; }
	;

maybe_not
	: EXCL_MARK		{ $$ = true; }
	|			{ $$ = false; }
	;

opt_family
	: FAMILY afamily	{ $$ = $2; }
	|			{ $$ = AF_UNSPEC; }
	;

rawproto
	: TCP tcp_flags_and_mask
	{
		$$.op_proto = IPPROTO_TCP;
		$$.op_opts = $2;
	}
	| ICMP icmp_type_and_code
	{
		$$.op_proto = IPPROTO_ICMP;
		$$.op_opts = $2;
	}
	| ICMP6 icmp_type_and_code
	{
		$$.op_proto = IPPROTO_ICMPV6;
		$$.op_opts = $2;
	}
	| some_name
	{
		$$.op_proto = npfctl_protono($1);
		$$.op_opts = NULL;
	}
	| number
	{
		$$.op_proto = $1;
		$$.op_opts = NULL;
	}
	;

proto_elems
	: proto_elems COMMA rawproto
	{
		npfvar_t *pvar = npfvar_create_element(
		    NPFVAR_PROTO, &$3, sizeof($3));
		$$ = npfvar_add_elements($1, pvar);
	}
	| rawproto
	{
		$$ = npfvar_create_element(NPFVAR_PROTO, &$1, sizeof($1));
	}
	;

proto
	: PROTO rawproto
	{
		$$ = npfvar_create_element(NPFVAR_PROTO, &$2, sizeof($2));
	}
	| PROTO CURLY_OPEN proto_elems CURLY_CLOSE
	{
		$$ = $3;
	}
	;

opt_proto
	: proto			{ $$ = $1; }
	|			{ $$ = NULL; }
	;

all_or_filt_opts
	: ALL user_id group_id
	{
		$$ = npfctl_parse_l3filt_opt(NULL, NULL, false, NULL, NULL, false, $2, $3);
	}
	| filt_opts	{ $$ = $1; }
	;

l2_all_of_filt_opts
	: ALL
	{
		$$ = npfctl_parse_l2filt_opt(NULL, false, NULL, false, 0);
	}
	| l2_filt_opts { $$ = $1; }
	;

opt_stateful
	: STATEFUL	{ $$ = NPF_RULE_STATEFUL; }
	| STATEFUL_ALL	{ $$ = NPF_RULE_STATEFUL | NPF_RULE_GSTATEFUL; }
	|		{ $$ = 0; }
	;

opt_apply
	: APPLY STRING	{ $$ = $2; }
	|		{ $$ = NULL; }
	;

block_opts
	: RETURNRST	{ $$ = NPF_RULE_RETRST; }
	| RETURNICMP	{ $$ = NPF_RULE_RETICMP; }
	| RETURN	{ $$ = NPF_RULE_RETRST | NPF_RULE_RETICMP; }
	|		{ $$ = 0; }
	;

filt_opts
	: FROM maybe_not filt_addr filt_port TO maybe_not filt_addr filt_port
	user_id group_id
	{
		$$ = npfctl_parse_l3filt_opt($3, $4, $2, $7, $8, $6, $9, $10);
	}
	| FROM maybe_not filt_addr filt_port user_id group_id
	{
		$$ = npfctl_parse_l3filt_opt($3, $4, $2, NULL, NULL, false, $5, $6);
	}
	| TO maybe_not filt_addr filt_port user_id group_id
	{
		$$ = npfctl_parse_l3filt_opt(NULL, NULL, false, $3, $4, $2, $5, $6);
	}
	;

l2_filt_opts
	: FROM maybe_not filt_addr TO maybe_not filt_addr ether_type
	{
		$$ = npfctl_parse_l2filt_opt($3, $2, $6, $5, $7);

	}
	| FROM maybe_not filt_addr ether_type
	{
		$$ = npfctl_parse_l2filt_opt($3, $2, NULL, false, $4);
	}
	| TO maybe_not filt_addr ether_type
	{
		$$ = npfctl_parse_l2filt_opt(NULL, false, $3, $2, $4);
	}
	;

ether_type
	: TYPE ETHERHEX { $$ = npfctl_parse_ether_type($2); }
	|	{ $$ = 0; }
	;

filt_addr_list
	: filt_addr_list COMMA filt_addr_element
	{
		npfvar_add_elements($1, $3);
	}
	| filt_addr_element
	;

filt_addr
	: CURLY_OPEN filt_addr_list CURLY_CLOSE
	{
		$$ = $2;
	}
	| filt_addr_element	{ $$ = $1; }
	| ANY			{ $$ = NULL; }
	;

addr_and_mask
	: addr SLASH number
	{
		$$ = npfctl_parse_fam_addr_mask($1, NULL, &$3);
	}
	| addr SLASH addr
	{
		$$ = npfctl_parse_fam_addr_mask($1, $3, NULL);
	}
	| addr
	{
		$$ = npfctl_parse_fam_addr_mask($1, NULL, NULL);
	}
	;

mac_addr
	: MACADDR
	{
		$$ = npfctl_parse_mac_addr($1);
	}
	;

filt_addr_element
	: addr_and_mask		{ assert($1 != NULL); $$ = $1; }
	| mac_addr		{ assert($1 != NULL); $$ = $1; }
	| static_ifaddrs
	{
		if (npfvar_get_count($1) != 1)
			yyerror("multiple interfaces are not supported");
		ifnet_addr_t *ifna = npfvar_get_data($1, NPFVAR_INTERFACE, 0);
		$$ = ifna->ifna_addrs;
	}
	| dynamic_ifaddrs	{ $$ = npfctl_ifnet_table($1); }
	| TABLE_ID		{ $$ = npfctl_parse_table_id($1); }
	| VAR_ID
	{
		npfvar_t *vp = npfvar_lookup($1);
		int type = npfvar_get_type(vp, 0);
		ifnet_addr_t *ifna;
again:
		switch (type) {
		case NPFVAR_IDENTIFIER:
		case NPFVAR_STRING:
			vp = npfctl_parse_ifnet(npfvar_expand_string(vp),
			    AF_UNSPEC);
			type = npfvar_get_type(vp, 0);
			goto again;
		case NPFVAR_FAM:
		case NPFVAR_MAC:
		case NPFVAR_TABLE:
			$$ = vp;
			break;
		case NPFVAR_INTERFACE:
			$$ = NULL;
			for (u_int i = 0; i < npfvar_get_count(vp); i++) {
				ifna = npfvar_get_data(vp, type, i);
				$$ = npfvar_add_elements($$, ifna->ifna_addrs);
			}
			break;
		case -1:
			yyerror("undefined variable '%s'", $1);
			break;
		default:
			yyerror("wrong variable '%s' type '%s' for address "
			    "or interface", $1, npfvar_type(type));
			break;
		}
	}
	;

addr
	: IPV4ADDR	{ $$ = $1; }
	| IPV6ADDR	{ $$ = $1; }
	;

filt_port
	: PORT CURLY_OPEN filt_port_list CURLY_CLOSE
	{
		$$ = npfctl_parse_port_range_variable(NULL, $3);
	}
	| PORT port_range	{ $$ = $2; }
	|			{ $$ = NULL; }
	;

filt_port_list
	: filt_port_list COMMA port_range
	{
		npfvar_add_elements($1, $3);
	}
	| port_range
	;

port_range
	: port		/* just port */
	{
		$$ = npfctl_parse_port_range($1, $1);
	}
	| port MINUS port	/* port from-to */
	{
		$$ = npfctl_parse_port_range($1, $3);
	}
	| VAR_ID
	{
		npfvar_t *vp;
		if ((vp = npfvar_lookup($1)) == NULL)
			yyerror("undefined port variable %s", $1);
		$$ = npfctl_parse_port_range_variable($1, vp);
	}
	;

port
	: number	{ $$ = $1; }
	| IDENTIFIER	{ $$ = npfctl_portno($1); }
	| STRING	{ $$ = npfctl_portno($1); }
	;

icmp_type_and_code
	: ICMPTYPE icmp_type
	{
		$$ = npfctl_parse_icmp($<num>0, $2, -1);
	}
	| ICMPTYPE icmp_type CODE number
	{
		$$ = npfctl_parse_icmp($<num>0, $2, $4);
	}
	| ICMPTYPE icmp_type CODE IDENTIFIER
	{
		$$ = npfctl_parse_icmp($<num>0, $2,
		    npfctl_icmpcode($<num>0, $2, $4));
	}
	| ICMPTYPE icmp_type CODE VAR_ID
	{
		char *s = npfvar_expand_string(npfvar_lookup($4));
		$$ = npfctl_parse_icmp($<num>0, $2,
		    npfctl_icmpcode($<num>0, $2, s));
	}
	|		{ $$ = NULL; }
	;

tcp_flags_and_mask
	: FLAGS tcp_flags SLASH tcp_flags
	{
		npfvar_add_elements($2, $4);
		$$ = $2;
	}
	| FLAGS tcp_flags
	{
		if (npfvar_get_count($2) != 1)
			yyerror("multiple tcpflags are not supported");
		char *s = npfvar_get_data($2, NPFVAR_TCPFLAG, 0);
		npfvar_add_elements($2, npfctl_parse_tcpflag(s));
		$$ = $2;
	}
	|		{ $$ = NULL; }
	;

tcp_flags
	: IDENTIFIER	{ $$ = npfctl_parse_tcpflag($1); }
	;

icmp_type
	: number	{ $$ = $1; }
	| IDENTIFIER	{ $$ = npfctl_icmptype($<num>-1, $1); }
	| VAR_ID
	{
		char *s = npfvar_expand_string(npfvar_lookup($1));
		$$ = npfctl_icmptype($<num>-1, s);
	}
	;

ifname
	: some_name
	{
		npfctl_note_interface($1);
		$$ = $1;
	}
	| VAR_ID
	{
		npfvar_t *vp = npfvar_lookup($1);
		const int type = npfvar_get_type(vp, 0);
		ifnet_addr_t *ifna;
		const char *name;
		unsigned *tid;
		bool ifaddr;

		switch (type) {
		case NPFVAR_STRING:
		case NPFVAR_IDENTIFIER:
			$$ = npfvar_expand_string(vp);
			break;
		case NPFVAR_INTERFACE:
			if (npfvar_get_count(vp) != 1)
				yyerror(
				    "multiple interfaces are not supported");
			ifna = npfvar_get_data(vp, type, 0);
			$$ = ifna->ifna_name;
			break;
		case NPFVAR_TABLE:
			tid = npfvar_get_data(vp, type, 0);
			name = npfctl_table_getname(npfctl_config_ref(),
			    *tid, &ifaddr);
			if (!ifaddr) {
				yyerror("variable '%s' references a table "
				    "%s instead of an interface", $1, name);
			}
			$$ = estrdup(name);
			break;
		case -1:
			yyerror("undefined variable '%s' for interface", $1);
			break;
		default:
			yyerror("wrong variable '%s' type '%s' for interface",
			    $1, npfvar_type(type));
			break;
		}
		npfctl_note_interface($$);
	}
	;

static_ifaddrs
	: afamily PAR_OPEN ifname PAR_CLOSE
	{
		$$ = npfctl_parse_ifnet($3, $1);
	}
	;

dynamic_ifaddrs
	: IFADDRS PAR_OPEN ifname PAR_CLOSE
	{
		$$ = $3;
	}
	;

ifref
	: ifname
	| dynamic_ifaddrs
	| static_ifaddrs
	{
		ifnet_addr_t *ifna;

		if (npfvar_get_count($1) != 1) {
			yyerror("multiple interfaces are not supported");
		}
		ifna = npfvar_get_data($1, NPFVAR_INTERFACE, 0);
		npfctl_note_interface(ifna->ifna_name);
		$$ = ifna->ifna_name;
	}
	;

user_id
	: /* empty */ { $$.op = NPF_OP_NONE; }
	| USER uids	{ $$ = $2; }
	;

uids
	: uid_item	{ $$ = $1; }
	;

uid_item
	: uid
	{
		npfctl_init_rid(&$$, $1, $1, NPF_OP_EQ);
	}
	| op_unary uid
	{
		npfctl_init_rid(&$$, $2, $2, $1);
	}
	| uid op_binary uid
	{
		npfctl_init_rid(&$$, $1, $3, $2);
	}
	;

uid
	: NUM
	{
		if ($1 >= UID_MAX) {
			yyerror("illegal uid value %lu", $1);
		}
		$$ = $1;
	}
	| IDENTIFIER
	{
		if (npfctl_parse_user($1, &$$) == -1) {
			yyerror("unknown user %s", $1);
		}
	}
	| VAR_ID
	{
		npf_var_rid($1, npfctl_parse_user, &$$, "user");
	}
	;

group_id
	: /* empty */ { $$.op = NPF_OP_NONE; }
	| GROUP gids	{ $$ = $2; }
	;

gids
	: gid_item	{ $$ = $1; }
	;

gid_item
	: gid
	{
		npfctl_init_rid(&$$, $1, $1, NPF_OP_EQ);
	}
	| op_unary gid
	{
		npfctl_init_rid(&$$, $2, $2, $1);
	}
	| gid op_binary gid
	{
		npfctl_init_rid(&$$, $1, $3, $2);
	}
	;

 gid
	: NUM
	{
		if ($1 >= GID_MAX) {
			yyerror("illegal gid value %lu", $1);
		}
		$$ = $1;
	}
	| IDENTIFIER
	{
		if (npfctl_parse_group($1, &$$) == -1) {
			yyerror("unknown group %s", $1);
		}
	}
	| VAR_ID
	{
		npf_var_rid($1, npfctl_parse_group, &$$, "group");
	}
	;

op_unary
	: EQ	{ $$ = NPF_OP_EQ; }
	| EXCL_MARK EQ	{ $$ = NPF_OP_NE; }
	| LT EQ { $$ = NPF_OP_LE; }
	| LT	{ $$ = NPF_OP_LT; }
	| GT EQ	{ $$ = NPF_OP_GE; }
	| GT	{ $$ = NPF_OP_GT; }
	;

op_binary
	: XRG	{ $$ = NPF_OP_XRG; }
	| IRG	{ $$ = NPF_OP_IRG; }
	;

number
	: HEX		{ $$ = $1; }
	| NUM		{ $$ = $1; }
	;

some_name
	: IDENTIFIER	{ $$ = $1; }
	| STRING	{ $$ = $1; }
	;

%%
