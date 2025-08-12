/*-
 * Copyright (c) 2011-2025 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This material is based upon work partially supported by The
 * NetBSD Foundation under a contract with Mindaugas Rasiukevicius.
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

/*
 * npfctl(8) building of the configuration.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: npf_build.c,v 1.60 2025/08/12 10:47:14 joe Exp $");

#include <sys/types.h>
#define	__FAVOR_BSD
#include <netinet/tcp.h>

#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>

#include <pcap/pcap.h>

#include "npfctl.h"

#define	MAX_RULE_NESTING	16

static nl_config_t *		npf_conf = NULL;
static bool			npf_debug = false;
static nl_rule_t *		the_rule = NULL;
static bool			npf_conf_built = false;

static bool			l2_group = false;
static nl_rule_t *		defgroup_l3 = NULL;
static nl_rule_t *		defgroup_l2 = NULL;
static nl_rule_t *		current_group[MAX_RULE_NESTING];
static unsigned			rule_nesting_level = 0;
static unsigned			npfctl_tid_counter = 0;

static void			npfctl_dump_bpf(struct bpf_program *);

void
npfctl_config_init(bool debug)
{
	npf_conf = npf_config_create();
	if (npf_conf == NULL) {
		errx(EXIT_FAILURE, "npf_config_create() failed");
	}
	memset(current_group, 0, sizeof(current_group));
	npf_debug = debug;
	npf_conf_built = false;
}

nl_config_t *
npfctl_config_ref(void)
{
	return npf_conf;
}

nl_rule_t *
npfctl_rule_ref(void)
{
	return the_rule;
}

void
npfctl_config_build(void)
{
	/* Run-once. */
	if (npf_conf_built) {
		return;
	}

	/*
	 * The layer 3 default group is mandatory.  Note: npfctl_build_group_end()
	 * skipped the default rule, since it must be the last one.
	 * if you set a layer 2 rule, layer 2 default also becomes mandatory.
	 * if you don't set layer 2 rules, only layer 3 default is mandatory
	 */
	if (!defgroup_l3) {
		errx(EXIT_FAILURE, "layer 3 default group was not defined");
	}

	if (l2_group & !defgroup_l2) {
		errx(EXIT_FAILURE, "layer 2 default group not defined");
	}
	assert(rule_nesting_level == 0);
	npf_rule_insert(npf_conf, NULL, defgroup_l3);

	if (defgroup_l2)
		npf_rule_insert(npf_conf, NULL, defgroup_l2);

	npf_config_build(npf_conf);
	npf_conf_built = true;
}

int
npfctl_config_send(int fd)
{
	npf_error_t errinfo;
	int error = 0;

	npfctl_config_build();
	error = npf_config_submit(npf_conf, fd, &errinfo);
	if (error) {
		npfctl_print_error(&errinfo);
	}
	npf_config_destroy(npf_conf);
	return error;
}

void
npfctl_config_save(nl_config_t *ncf, const char *outfile)
{
	void *blob;
	size_t len;
	int fd;

	blob = npf_config_export(ncf, &len);
	if (!blob) {
		err(EXIT_FAILURE, "npf_config_export");
	}
	if ((fd = open(outfile, O_CREAT | O_TRUNC | O_WRONLY, 0644)) == -1) {
		err(EXIT_FAILURE, "could not open %s", outfile);
	}
	if (write(fd, blob, len) != (ssize_t)len) {
		err(EXIT_FAILURE, "write to %s failed", outfile);
	}
	free(blob);
	close(fd);
}

bool
npfctl_debug_addif(const char *ifname)
{
	const char tname[] = "npftest";
	const size_t tnamelen = sizeof(tname) - 1;

	if (npf_debug) {
		_npf_debug_addif(npf_conf, ifname);
		return strncmp(ifname, tname, tnamelen) == 0;
	}
	return 0;
}

nl_table_t *
npfctl_table_getbyname(nl_config_t *ncf, const char *name)
{
	nl_iter_t i = NPF_ITER_BEGIN;
	nl_table_t *tl;

	/* XXX dynamic ruleset */
	if (!ncf) {
		return NULL;
	}
	while ((tl = npf_table_iterate(ncf, &i)) != NULL) {
		const char *tname = npf_table_getname(tl);
		if (strcmp(tname, name) == 0) {
			break;
		}
	}
	return tl;
}

unsigned
npfctl_table_getid(const char *name)
{
	nl_table_t *tl;

	tl = npfctl_table_getbyname(npf_conf, name);
	return tl ? npf_table_getid(tl) : (unsigned)-1;
}

const char *
npfctl_table_getname(nl_config_t *ncf, unsigned tid, bool *ifaddr)
{
	const char *name = NULL;
	nl_iter_t i = NPF_ITER_BEGIN;
	nl_table_t *tl;

	while ((tl = npf_table_iterate(ncf, &i)) != NULL) {
		if (npf_table_getid(tl) == tid) {
			name = npf_table_getname(tl);
			break;
		}
	}
	if (!name) {
		return NULL;
	}
	if (!strncmp(name, NPF_IFNET_TABLE_PREF, NPF_IFNET_TABLE_PREFLEN)) {
		name += NPF_IFNET_TABLE_PREFLEN;
		*ifaddr = true;
	} else {
		*ifaddr = false;
	}
	return name;
}

static in_port_t
npfctl_get_singleport(const npfvar_t *vp)
{
	port_range_t *pr;
	in_port_t *port;

	if (npfvar_get_count(vp) > 1) {
		yyerror("multiple ports are not valid");
	}
	pr = npfvar_get_data(vp, NPFVAR_PORT_RANGE, 0);
	if (pr->pr_start != pr->pr_end) {
		yyerror("port range is not valid");
	}
	port = &pr->pr_start;
	return *port;
}

static fam_addr_mask_t *
npfctl_get_singlefam(const npfvar_t *vp)
{
	fam_addr_mask_t *am;

	if (npfvar_get_type(vp, 0) != NPFVAR_FAM) {
		yyerror("map segment must be an address or network");
	}
	if (npfvar_get_count(vp) > 1) {
		yyerror("map segment cannot have multiple static addresses");
	}
	am = npfvar_get_data(vp, NPFVAR_FAM, 0);
	if (am == NULL) {
		yyerror("invalid map segment");
	}
	return am;
}

static unsigned
npfctl_get_singletable(const npfvar_t *vp)
{
	unsigned *tid;

	if (npfvar_get_count(vp) > 1) {
		yyerror("invalid use of multiple tables");
	}
	tid = npfvar_get_data(vp, NPFVAR_TABLE, 0);
	assert(tid != NULL);
	return *tid;
}

static bool
npfctl_build_fam(npf_bpf_t *ctx, sa_family_t family,
    fam_addr_mask_t *fam, unsigned opts)
{
	/*
	 * If family is specified, address does not match it and the
	 * address is extracted from the interface, then simply ignore.
	 * Otherwise, address of invalid family was passed manually.
	 */
	if (family != AF_UNSPEC && family != fam->fam_family) {
		if (!fam->fam_ifindex) {
			yyerror("specified address is not of the required "
			    "family %d", family);
		}
		return false;
	}

	family = fam->fam_family;
	if (family != AF_INET && family != AF_INET6) {
		yyerror("family %d is not supported", family);
	}

	/*
	 * Optimise 0.0.0.0/0 case to be NOP.  Otherwise, address with
	 * zero mask would never match and therefore is not valid.
	 */
	if (fam->fam_mask == 0) {
		if (!npfctl_addr_iszero(&fam->fam_addr)) {
			yyerror("filter criterion would never match");
		}
		return false;
	}

	npfctl_bpf_cidr(ctx, opts, family, &fam->fam_addr, fam->fam_mask);
	return true;
}

static void
npfctl_build_vars(npf_bpf_t *ctx, sa_family_t family, npfvar_t *vars, int opts)
{
	npfctl_bpf_group_enter(ctx, (opts & MATCH_INVERT) != 0);
	for (unsigned i = 0; i < npfvar_get_count(vars); i++) {
		const unsigned type = npfvar_get_type(vars, i);
		void *data = npfvar_get_data(vars, type, i);

		assert(data != NULL);

		switch (type) {
		case NPFVAR_FAM: {
			fam_addr_mask_t *fam = data;
			npfctl_build_fam(ctx, family, fam, opts);
			break;
		}
		case NPFVAR_PORT_RANGE: {
			port_range_t *pr = data;
			npfctl_bpf_ports(ctx, opts, pr->pr_start, pr->pr_end);
			break;
		}
		case NPFVAR_TABLE: {
			unsigned tid;
			memcpy(&tid, data, sizeof(unsigned));
			npfctl_bpf_table(ctx, opts, tid);
			break;
		}
		case NPFVAR_MAC: {
			struct ether_addr *eth = data;
			npfctl_bpf_ether(ctx, opts, eth);
			break;
		}
		default:
			yyerror("unexpected %s", npfvar_type(type));
		}
	}
	npfctl_bpf_group_exit(ctx);
}

static void
npfctl_build_proto_block(npf_bpf_t *ctx, const opt_proto_t *op, bool multiple)
{
	const unsigned proto = op->op_proto;
	npfvar_t *popts = op->op_opts;

	if (multiple && popts) {
		yyerror("multiple protocol options with protocol filters "
		    "are not yet supported");
	}

	/* Build the protocol filter. */
	npfctl_bpf_proto(ctx, proto);

	switch (proto) {
	case IPPROTO_TCP:
		/* Build TCP flags matching (optional). */
		if (popts) {
			uint8_t *tf, *tf_mask;

			assert(npfvar_get_count(popts) == 2);
			tf = npfvar_get_data(popts, NPFVAR_TCPFLAG, 0);
			tf_mask = npfvar_get_data(popts, NPFVAR_TCPFLAG, 1);
			npfctl_bpf_tcpfl(ctx, *tf, *tf_mask);
		}
		break;
	case IPPROTO_ICMP:
	case IPPROTO_ICMPV6:
		/* Build ICMP/ICMPv6 type and/or code matching. */
		if (popts) {
			int *icmp_type, *icmp_code;

			assert(npfvar_get_count(popts) == 2);
			icmp_type = npfvar_get_data(popts, NPFVAR_ICMP, 0);
			icmp_code = npfvar_get_data(popts, NPFVAR_ICMP, 1);
			npfctl_bpf_icmp(ctx, *icmp_type, *icmp_code);
		}
		break;
	default:
		/* No options for other protocols. */
		break;
	}
}

static void
npfctl_build_proto(npf_bpf_t *ctx, const npfvar_t *vars)
{
	const unsigned count = npfvar_get_count(vars);

	/*
	 * XXX: For now, just do not support multiple protocol
	 * blocks with options; this is because npfctl_bpf_tcpfl()
	 * and npfctl_bpf_icmp() will not work correctly in a group.
	 */
	if (count == 1) {
		const opt_proto_t *op = npfvar_get_data(vars, NPFVAR_PROTO, 0);
		npfctl_build_proto_block(ctx, op, false);
		return;
	}

	npfctl_bpf_group_enter(ctx, false);
	for (unsigned i = 0; i < count; i++) {
		const opt_proto_t *op = npfvar_get_data(vars, NPFVAR_PROTO, i);
		npfctl_build_proto_block(ctx, op, true);
	}
	npfctl_bpf_group_exit(ctx);
}

static bool
npfctl_check_proto(const npfvar_t *vars, bool *non_tcpudp, bool *tcp_with_nofl)
{
	unsigned count;

	*non_tcpudp = false;
	*tcp_with_nofl = false;

	if (vars == NULL) {
		return false;
	}

	count = npfvar_get_count(vars);
	for (unsigned i = 0; i < count; i++) {
		const opt_proto_t *op = npfvar_get_data(vars, NPFVAR_PROTO, i);

		switch (op->op_proto) {
		case IPPROTO_TCP:
			*tcp_with_nofl = op->op_opts == NULL;
			break;
		case IPPROTO_UDP:
		case -1:
			break;
		default:
			*non_tcpudp = true;
			break;
		}
	}
	return count != 0;
}

static bool
build_l3_code(npf_bpf_t *bc, nl_rule_t *rl, sa_family_t family, const npfvar_t *popts,
    const filt_opts_t *fopts)
{
	unsigned opts;
	const addr_port_t *apfrom = &fopts->filt.opt3.fo_from;
	const addr_port_t *apto = &fopts->filt.opt3.fo_to;
	bool any_proto, any_addrs, any_ports, stateful;
	bool any_l4proto, non_tcpudp, tcp_with_nofl;

	/*
	 * Gather some information about the protocol options, if any.
	 * Check the filter criteria in general -- if none specified,
	 * then no byte-code.
	 */
	any_l4proto = npfctl_check_proto(popts, &non_tcpudp, &tcp_with_nofl);
	any_proto = (family != AF_UNSPEC) || any_l4proto;
	any_addrs = apfrom->ap_netaddr || apto->ap_netaddr;
	any_ports = apfrom->ap_portrange || apto->ap_portrange;
	stateful = (npf_rule_getattr(rl) & NPF_RULE_STATEFUL) != 0;
	if (!any_proto && !any_addrs && !any_ports && !stateful) {
		return false;
	}

	/*
	 * Sanity check: ports can only be used with TCP or UDP protocol.
	 */
	if (any_ports && non_tcpudp) {
		yyerror("invalid filter options for given the protocol(s)");
	}

	/* Build layer 3 and 4 protocol blocks. */
	if (family != AF_UNSPEC) {
		npfctl_bpf_ipver(bc, family);
	}
	if (any_l4proto) {
		npfctl_build_proto(bc, popts);
	}

	/*
	 * If this is a stateful rule and TCP flags are not specified,
	 * then add "flags S/SAFR" filter for TCP protocol case.
	 */
	if (stateful && (!any_l4proto || tcp_with_nofl)) {
		npfctl_bpf_tcpfl(bc, TH_SYN, TH_SYN | TH_ACK | TH_FIN | TH_RST);
	}

	/* Build IP address blocks. */
	opts = MATCH_SRC | (fopts->fo_finvert ? MATCH_INVERT : 0);
	npfctl_build_vars(bc, family, apfrom->ap_netaddr, opts);
	opts = MATCH_DST | (fopts->fo_tinvert ? MATCH_INVERT : 0);
	npfctl_build_vars(bc, family, apto->ap_netaddr, opts);

	/*
	 * Build the port-range blocks.  If no protocol is specified,
	 * then we implicitly filter for the TCP / UDP protocols.
	 */
	if (any_ports && !any_l4proto) {
		npfctl_bpf_group_enter(bc, false);
		npfctl_bpf_proto(bc, IPPROTO_TCP);
		npfctl_bpf_proto(bc, IPPROTO_UDP);
		npfctl_bpf_group_exit(bc);
	}

	npfctl_build_vars(bc, family, apfrom->ap_portrange, MATCH_SRC);
	npfctl_build_vars(bc, family, apto->ap_portrange, MATCH_DST);

	return true;
}

static bool
build_l2_code(npf_bpf_t *bc, const filt_opts_t *fopts)
{
	unsigned opts;
	npfvar_t *ap_from = fopts->filt.opt2.from_mac;
	npfvar_t *ap_to = fopts->filt.opt2.to_mac;
	const uint16_t ether_type = fopts->filt.opt2.ether_type;
	bool addr_or_ether;

	addr_or_ether = ap_from || ap_to || ether_type;
	if(!addr_or_ether)
		return false;

	if (ether_type != 0) {
		fetch_ether_type(bc, ether_type);
	}

	/* Build ether address blocks. */
	opts = MATCH_DST | (fopts->fo_tinvert ? MATCH_INVERT : 0);
	npfctl_build_vars(bc, 0, ap_to, opts);
	opts = MATCH_SRC | (fopts->fo_finvert ? MATCH_INVERT : 0);
	npfctl_build_vars(bc, 0, ap_from, opts);

	return true;
}

static bool
npfctl_build_code(nl_rule_t *rl, sa_family_t family, const npfvar_t *popts,
    const filt_opts_t *fopts)
{
	npf_bpf_t *bc;
	size_t len;
	uint32_t layer = fopts->layer;

	bc = npfctl_bpf_create();
	if (layer == NPF_RULE_LAYER_3) {
		if (!build_l3_code(bc, rl, family, popts, fopts))
			return false;
	} else if (layer == NPF_RULE_LAYER_2) {
		if (!build_l2_code(bc, fopts))
			return false;
	} else {
		yyerror("%s: layer not supported", __func__);
	}

	/* Set the byte-code marks, if any. */
	const void *bmarks = npfctl_bpf_bmarks(bc, &len);
	if (bmarks && npf_rule_setinfo(rl, bmarks, len) != 0) {
		errx(EXIT_FAILURE, "npf_rule_setinfo");
	}

	/* Complete BPF byte-code and pass to the rule. */
	struct bpf_program *bf = npfctl_bpf_complete(bc);
	if (bf == NULL) {
		npfctl_bpf_destroy(bc);
		return true;
	}
	len = bf->bf_len * sizeof(struct bpf_insn);

	if (npf_rule_setcode(rl, NPF_CODE_BPF, bf->bf_insns, len) != 0) {
		errx(EXIT_FAILURE, "npf_rule_setcode");
	}
	npfctl_dump_bpf(bf);
	npfctl_bpf_destroy(bc);

	return true;
}

static void
npfctl_build_pcap(nl_rule_t *rl, const char *filter)
{
	const size_t maxsnaplen = 64 * 1024;
	struct bpf_program bf;
	size_t len;
	pcap_t *pd;

	pd = pcap_open_dead(DLT_RAW, maxsnaplen);
	if (pd == NULL) {
		err(EXIT_FAILURE, "pcap_open_dead");
	}

	if (pcap_compile(pd, &bf,
	    filter, 1, PCAP_NETMASK_UNKNOWN) == -1) {
		yyerror("invalid pcap-filter(7) syntax");
	}
	len = bf.bf_len * sizeof(struct bpf_insn);

	if (npf_rule_setcode(rl, NPF_CODE_BPF, bf.bf_insns, len) != 0) {
		errx(EXIT_FAILURE, "npf_rule_setcode failed");
	}
	npfctl_dump_bpf(&bf);
	pcap_freecode(&bf);
	pcap_close(pd);
}

static void
npfctl_build_rpcall(nl_rproc_t *rp, const char *name, npfvar_t *args)
{
	npf_extmod_t *extmod;
	nl_ext_t *extcall;
	int error;

	extmod = npf_extmod_get(name, &extcall);
	if (extmod == NULL) {
		yyerror("unknown rule procedure '%s'", name);
	}

	for (size_t i = 0; i < npfvar_get_count(args); i++) {
		const char *param, *value;
		proc_param_t *p;

		p = npfvar_get_data(args, NPFVAR_PROC_PARAM, i);
		param = p->pp_param;
		value = p->pp_value;

		error = npf_extmod_param(extmod, extcall, param, value);
		switch (error) {
		case EINVAL:
			yyerror("invalid parameter '%s'", param);
		default:
			break;
		}
	}
	error = npf_rproc_extcall(rp, extcall);
	if (error) {
		yyerror(error == EEXIST ?
		    "duplicate procedure call" : "unexpected error");
	}
}

/*
 * npfctl_build_rproc: create and insert a rule procedure.
 */
void
npfctl_build_rproc(const char *name, npfvar_t *procs)
{
	nl_rproc_t *rp;
	size_t i;

	rp = npf_rproc_create(name);
	if (rp == NULL) {
		errx(EXIT_FAILURE, "%s failed", __func__);
	}

	for (i = 0; i < npfvar_get_count(procs); i++) {
		proc_call_t *pc = npfvar_get_data(procs, NPFVAR_PROC, i);
		npfctl_build_rpcall(rp, pc->pc_name, pc->pc_opts);
	}
	npf_rproc_insert(npf_conf, rp);
}

/*
 * npfctl_build_maprset: create and insert a NAT ruleset.
 */
void
npfctl_build_maprset(const char *name, int attr, const char *ifname)
{
	const int attr_di = (NPF_RULE_IN | NPF_RULE_OUT);
	nl_rule_t *rl;
	bool natset;
	int err;

	/* Validate the prefix. */
	err = npfctl_nat_ruleset_p(name, &natset);
	if (!natset) {
		yyerror("NAT ruleset names must be prefixed with `"
		    NPF_RULESET_MAP_PREF "`");
	}
	if (err) {
		yyerror("NAT ruleset is missing a name (only prefix found)");
	}

	/* If no direction is not specified, then both. */
	if ((attr & attr_di) == 0) {
		attr |= attr_di;
	}

	/* Allow only "in/out" attributes. */
	attr = NPF_RULE_GROUP | NPF_RULE_DYNAMIC | (attr & attr_di);
	rl = npf_rule_create(name, attr, ifname);
	npf_rule_setprio(rl, NPF_PRI_LAST);
	npf_nat_insert(npf_conf, rl);
}

static void
npf_check_layer(const char **lstr, uint32_t lattr, const char *func)
{
	if (lattr & NPF_RULE_LAYER_2)
		*lstr = "layer 2";
	else if (lattr & NPF_RULE_LAYER_3)
		*lstr = "layer 3";
	else
		yyerror("%s: layer not yet supported", func);
}

static nl_rule_t *
set_defgroup(nl_rule_t *rl, nl_rule_t *def_group, int attr)
{
	if (def_group) {
		const char *str;
		npf_check_layer(&str, attr, __func__);
		yyerror("multiple %s default groups are not valid", str);
	}
	if (rule_nesting_level) {
		yyerror("default group can only be at the top level");
	}

	return rl;
}

/*
 * npfctl_build_group: create a group, update the current group pointer
 * and increase the nesting level.
 */
void
npfctl_build_group(const char *name, int attr, const char *ifname, bool def)
{
	const int attr_di = (NPF_RULE_IN | NPF_RULE_OUT);
	nl_rule_t *rl;

	if (def || (attr & attr_di) == 0) {
		attr |= attr_di;
	}

	rl = npf_rule_create(name, attr | NPF_RULE_GROUP, ifname);
	npf_rule_setprio(rl, NPF_PRI_LAST);
	if (def) {
		if (attr & NPF_RULE_LAYER_3) {
			defgroup_l3 = set_defgroup(rl, defgroup_l3, attr);
		}
		else if (attr & NPF_RULE_LAYER_2) {
			defgroup_l2 = set_defgroup(rl, defgroup_l2, attr);
		}
		else {
			yyerror("%s: layer not supported", __func__);
		}
	} else {
		if (attr & NPF_RULE_LAYER_2)
			l2_group = true;
	}

	/* Set the current group and increase the nesting level. */
	if (rule_nesting_level >= MAX_RULE_NESTING) {
		yyerror("rule nesting limit reached");
	}
	current_group[++rule_nesting_level] = rl;
}

void
npfctl_build_group_end(void)
{
	nl_rule_t *parent, *group;

	assert(rule_nesting_level > 0);
	parent = current_group[rule_nesting_level - 1];
	group = current_group[rule_nesting_level];
	current_group[rule_nesting_level--] = NULL;

	/*
	 * Note:
	 * - If the parent is NULL, then it is a global rule.
	 * - The default rule must be the last, so it is inserted later.
	 */
	if (group == defgroup_l3 || group == defgroup_l2) {
		assert(parent == NULL);
		return;
	}
	npf_rule_insert(npf_conf, parent, group);
}

/*
 * this function is here to ensure that layer 2 rules are
 * rightfully embedded in layer2 groups
 * and vice versa. layer3 group => layer 3 rules
 * does not allow setting layer 2 rules in layer 3 groups
 */
static uint32_t
npf_rule_layer_compat(nl_rule_t *cg, uint32_t layer)
{
	uint32_t attr = attr = npf_rule_getattr(cg);

	if ((attr & layer) == 0) {
		/* only set the layer strings when you need them */
		const char *str;
		npf_check_layer(&str, layer, __func__);

		yyerror("cannot insert %s rules in this group"
			" make sure to insert same layer rules in the same group ", str);
	}
	return layer;
}

/*
 * npfctl_build_rule: create a rule, build byte-code from filter options,
 * if any, and insert into the ruleset of current group, or set the rule.
 */
void
npfctl_build_rule(uint32_t attr, const char *ifname, sa_family_t family,
    const npfvar_t *popts, const filt_opts_t *fopts,
    const char *pcap_filter, const char *rproc)
{
	nl_rule_t *rl, *cg;

	attr |= (npf_conf ? 0 : NPF_RULE_DYNAMIC);

	/*
	 * quickly check for group-rule layer compat
	 * if the filter layer matches group layer,
	 * set the layer bit in rule attribute for kernel
	 */
	if (npf_conf) {
		cg = current_group[rule_nesting_level];
		attr |= npf_rule_layer_compat(cg, fopts->layer);
	} else {
		/* set the layer bit directly for dynamic rules */
		attr |= fopts->layer;
	}

	if (attr & NPF_RULE_LAYER_2 && attr & (NPF_RULE_RETRST | NPF_RULE_RETICMP))
		yyerror("return blocks not yet supported in layer 2");

	rl = npf_rule_create(NULL, attr, ifname);
	if (pcap_filter) {
		npfctl_build_pcap(rl, pcap_filter);
	} else {
		npfctl_build_code(rl, family, popts, fopts);
	}

	if (fopts->uid.op != NPF_OP_NONE) {
		npf_rule_setrid(rl, fopts->uid, "r_user");
	}

	if (fopts->gid.op != NPF_OP_NONE) {
		npf_rule_setrid(rl, fopts->gid, "r_group");
	}

	if (rproc) {
		npf_rule_setproc(rl, rproc);
	}

	if (npf_conf) {
		cg = current_group[rule_nesting_level];

		if (rproc && !npf_rproc_exists_p(npf_conf, rproc)) {
			yyerror("rule procedure '%s' is not defined", rproc);
		}
		assert(cg != NULL);
		npf_rule_setprio(rl, NPF_PRI_LAST);
		npf_rule_insert(npf_conf, cg, rl);
	} else {
		/* We have parsed a single rule - set it. */
		the_rule = rl;
	}
}

/*
 * npfctl_build_nat: create a single NAT policy of a specified
 * type with a given filter options.
 */
static nl_nat_t *
npfctl_build_nat(int type, const char *ifname, const addr_port_t *ap,
    const npfvar_t *popts, const filt_opts_t *fopts, unsigned flags)
{
	fam_addr_mask_t *am;
	sa_family_t family;
	in_port_t port;
	nl_nat_t *nat;
	unsigned tid;

	if (ap->ap_portrange) {
		/*
		 * The port forwarding case.  In such case, there has to
		 * be a single port used for translation; we keep the port
		 * translation on, but disable the port map.
		 */
		port = npfctl_get_singleport(ap->ap_portrange);
		flags = (flags & ~NPF_NAT_PORTMAP) | NPF_NAT_PORTS;
	} else {
		port = 0;
	}

	nat = npf_nat_create(type, flags, ifname);

	switch (npfvar_get_type(ap->ap_netaddr, 0)) {
	case NPFVAR_FAM:
		/* Translation address. */
		am = npfctl_get_singlefam(ap->ap_netaddr);
		family = am->fam_family;
		npf_nat_setaddr(nat, family, &am->fam_addr, am->fam_mask);
		break;
	case NPFVAR_TABLE:
		/* Translation table. */
		family = AF_UNSPEC;
		tid = npfctl_get_singletable(ap->ap_netaddr);
		npf_nat_settable(nat, tid);
		break;
	default:
		yyerror("map must have a valid translation address");
		abort();
	}
	npf_nat_setport(nat, port);
	npfctl_build_code(nat, family, popts, fopts);
	return nat;
}

static void
npfctl_dnat_check(const addr_port_t *ap, const unsigned algo)
{
	const unsigned type = npfvar_get_type(ap->ap_netaddr, 0);
	fam_addr_mask_t *am;

	switch (algo) {
	case NPF_ALGO_NETMAP:
		if (type == NPFVAR_FAM) {
			break;
		}
		yyerror("translation address using NETMAP must be "
		    "a network and not a dynamic pool");
		break;
	case NPF_ALGO_IPHASH:
	case NPF_ALGO_RR:
	case NPF_ALGO_NONE:
		if (type != NPFVAR_FAM) {
			break;
		}
		am = npfctl_get_singlefam(ap->ap_netaddr);
		if (am->fam_mask == NPF_NO_NETMASK) {
			break;
		}
		yyerror("translation address, given the specified algorithm, "
		    "must be a pool or a single address");
		break;
	default:
		yyerror("invalid algorithm specified for dynamic NAT");
	}
}

/*
 * npfctl_build_natseg: validate and create NAT policies.
 */
void
npfctl_build_natseg(int sd, int type, unsigned mflags, const char *ifname,
    const addr_port_t *ap1, const addr_port_t *ap2, const npfvar_t *popts,
    const filt_opts_t *fopts, unsigned algo)
{
	fam_addr_mask_t *am1 = NULL, *am2 = NULL;
	nl_nat_t *nt1 = NULL, *nt2 = NULL;
	filt_opts_t imfopts;
	uint16_t adj = 0;
	unsigned flags;
	bool binat;

	assert(ifname != NULL);

	/*
	 * Validate that mapping has the translation address(es) set.
	 */
	if ((type & NPF_NATIN) != 0 && ap1->ap_netaddr == NULL) {
		yyerror("inbound network segment is not specified");
	}
	if ((type & NPF_NATOUT) != 0 && ap2->ap_netaddr == NULL) {
		yyerror("outbound network segment is not specified");
	}

	/*
	 * Bi-directional NAT is a combination of inbound NAT and outbound
	 * NAT policies with the translation segments inverted respectively.
	 */
	binat = (NPF_NATIN | NPF_NATOUT) == type;

	switch (sd) {
	case NPFCTL_NAT_DYNAMIC:
		/*
		 * Dynamic NAT: stateful translation -- traditional NAPT
		 * is expected.  Unless it is bi-directional NAT, perform
		 * the port mapping.
		 */
		flags = !binat ? (NPF_NAT_PORTS | NPF_NAT_PORTMAP) : 0;
		if (type & NPF_NATIN) {
			npfctl_dnat_check(ap1, algo);
		}
		if (type & NPF_NATOUT) {
			npfctl_dnat_check(ap2, algo);
		}
		break;
	case NPFCTL_NAT_STATIC:
		/*
		 * Static NAT: stateless translation.
		 */
		flags = NPF_NAT_STATIC;

		/* Note: translation address/network cannot be a table. */
		if (type & NPF_NATIN) {
			am1 = npfctl_get_singlefam(ap1->ap_netaddr);
		}
		if (type & NPF_NATOUT) {
			am2 = npfctl_get_singlefam(ap2->ap_netaddr);
		}

		/* Validate the algorithm. */
		switch (algo) {
		case NPF_ALGO_NPT66:
			if (!binat || am1->fam_mask != am2->fam_mask) {
				yyerror("asymmetric NPTv6 is not supported");
			}
			adj = npfctl_npt66_calcadj(am1->fam_mask,
			    &am1->fam_addr, &am2->fam_addr);
			break;
		case NPF_ALGO_NETMAP:
			if (binat && am1->fam_mask != am2->fam_mask) {
				yyerror("net-to-net mapping using the "
				    "NETMAP algorithm must be 1:1");
			}
			break;
		case NPF_ALGO_NONE:
			if ((am1 && am1->fam_mask != NPF_NO_NETMASK) ||
			    (am2 && am2->fam_mask != NPF_NO_NETMASK)) {
				yyerror("static net-to-net translation "
				    "must have an algorithm specified");
			}
			break;
		default:
			yyerror("invalid algorithm specified for static NAT");
		}
		break;
	default:
		abort();
	}

	/*
	 * Apply the flag modifications.
	 */
	if (mflags & NPF_NAT_PORTS) {
		flags &= ~(NPF_NAT_PORTS | NPF_NAT_PORTMAP);
	}

	/*
	 * If the filter criteria is not specified explicitly, apply implicit
	 * filtering according to the given network segments.
	 *
	 * Note: filled below, depending on the type.
	 */
	if (__predict_true(!fopts)) {
		fopts = &imfopts;
	}

	if (type & NPF_NATIN) {
		memset(&imfopts, 0, sizeof(imfopts));
		imfopts.layer = NPF_RULE_LAYER_3;
		memcpy(&imfopts.filt.opt3.fo_to, ap2, sizeof(imfopts.filt.opt3.fo_to));
		nt1 = npfctl_build_nat(NPF_NATIN, ifname,
		    ap1, popts, fopts, flags);
	}
	if (type & NPF_NATOUT) {
		memset(&imfopts, 0, sizeof(imfopts));
		imfopts.layer = NPF_RULE_LAYER_3;
		memcpy(&imfopts.filt.opt3.fo_from, ap1, sizeof(imfopts.filt.opt3.fo_from));
		nt2 = npfctl_build_nat(NPF_NATOUT, ifname,
		    ap2, popts, fopts, flags);
	}

	switch (algo) {
	case NPF_ALGO_NONE:
		break;
	case NPF_ALGO_NPT66:
		/*
		 * NPTv6 is a special case using special adjustment value.
		 * It is always bidirectional NAT.
		 */
		assert(nt1 && nt2);
		npf_nat_setnpt66(nt1, ~adj);
		npf_nat_setnpt66(nt2, adj);
		break;
	default:
		/*
		 * Set the algorithm.
		 */
		if (nt1) {
			npf_nat_setalgo(nt1, algo);
		}
		if (nt2) {
			npf_nat_setalgo(nt2, algo);
		}
	}

	if (npf_conf) {
		if (nt1) {
			npf_rule_setprio(nt1, NPF_PRI_LAST);
			npf_nat_insert(npf_conf, nt1);
		}
		if (nt2) {
			npf_rule_setprio(nt2, NPF_PRI_LAST);
			npf_nat_insert(npf_conf, nt2);
		}
	} else {
		// XXX/TODO: need to refactor a bit to enable this..
		if (nt1 && nt2) {
			errx(EXIT_FAILURE, "bidirectional NAT is currently "
			    "not yet supported in the dynamic rules");
		}
		the_rule = nt1 ? nt1 : nt2;
	}
}

/*
 * npfctl_fill_table: fill NPF table with entries from a specified file.
 */
static void
npfctl_fill_table(nl_table_t *tl, unsigned type, const char *fname, FILE *fp)
{
	char *buf = NULL;
	int l = 0;
	size_t n;

	if (fp == NULL && (fp = fopen(fname, "r")) == NULL) {
		err(EXIT_FAILURE, "open '%s'", fname);
	}
	while (l++, getline(&buf, &n, fp) != -1) {
		fam_addr_mask_t fam;
		int alen;

		if (*buf == '\n' || *buf == '#') {
			continue;
		}

		if (!npfctl_parse_cidr(buf, &fam, &alen)) {
			errx(EXIT_FAILURE,
			    "%s:%d: invalid table entry", fname, l);
		}
		if (type != NPF_TABLE_LPM && fam.fam_mask != NPF_NO_NETMASK) {
			errx(EXIT_FAILURE, "%s:%d: mask used with the "
			    "table type other than \"lpm\"", fname, l);
		}

		npf_table_add_entry(tl, fam.fam_family,
		    &fam.fam_addr, fam.fam_mask);
	}
	free(buf);
}

/*
 * npfctl_load_table: create an NPF table and fill with contents from a file.
 */
nl_table_t *
npfctl_load_table(const char *tname, int tid, unsigned type,
    const char *fname, FILE *fp)
{
	nl_table_t *tl;

	tl = npf_table_create(tname, tid, type);
	if (tl && fname) {
		npfctl_fill_table(tl, type, fname, fp);
	}

	return tl;
}

/*
 * npfctl_build_table: create an NPF table, add to the configuration and,
 * if required, fill with contents from a file.
 */
void
npfctl_build_table(const char *tname, unsigned type, const char *fname)
{
	nl_table_t *tl;

	if (type == NPF_TABLE_CONST && !fname) {
		yyerror("table type 'const' must be loaded from a file");
	}

	tl = npfctl_load_table(tname, npfctl_tid_counter++, type, fname, NULL);
	assert(tl != NULL);

	if (npf_table_insert(npf_conf, tl)) {
		yyerror("table '%s' is already defined", tname);
	}
}

/*
 * npfctl_ifnet_table: get a variable with ifaddr-table; auto-create
 * the table on first reference.
 */
npfvar_t *
npfctl_ifnet_table(const char *ifname)
{
	char tname[NPF_TABLE_MAXNAMELEN];
	nl_table_t *tl;
	unsigned tid;

	snprintf(tname, sizeof(tname), NPF_IFNET_TABLE_PREF "%s", ifname);
	if (!npf_conf) {
		errx(EXIT_FAILURE, "expression `ifaddrs(%s)` is currently "
		    "not yet supported in dynamic rules", ifname);
	}

	tid = npfctl_table_getid(tname);
	if (tid == (unsigned)-1) {
		tid = npfctl_tid_counter++;
		tl = npf_table_create(tname, tid, NPF_TABLE_IFADDR);
		(void)npf_table_insert(npf_conf, tl);
	}
	return npfvar_create_element(NPFVAR_TABLE, &tid, sizeof(unsigned));
}

/*
 * npfctl_build_alg: create an NPF application level gateway and add it
 * to the configuration.
 */
void
npfctl_build_alg(const char *al_name)
{
	if (npf_alg_load(npf_conf, al_name) != 0) {
		yyerror("ALG '%s' is already loaded", al_name);
	}
}

void
npfctl_setparam(const char *name, int val)
{
	if (strcmp(name, "bpf.jit") == 0) {
		npfctl_bpfjit(val != 0);
		return;
	}
	if (npf_param_set(npf_conf, name, val) != 0) {
		yyerror("invalid parameter `%s` or its value", name);
	}
}

static void
npfctl_dump_bpf(struct bpf_program *bf)
{
	if (npf_debug) {
		extern char *yytext;
		extern int yylineno;

		int rule_line = yylineno - (int)(*yytext == '\n');
		printf("\nRULE AT LINE %d\n", rule_line);
		bpf_dump(bf, 0);
	}
}
