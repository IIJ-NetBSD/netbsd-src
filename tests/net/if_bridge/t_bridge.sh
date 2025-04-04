#	$NetBSD: t_bridge.sh,v 1.21 2024/09/03 08:01:38 ozaki-r Exp $
#
# Copyright (c) 2014 The NetBSD Foundation, Inc.
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

SOCK1=unix://commsock1
SOCK2=unix://commsock2
SOCK3=unix://commsock3
IP1=10.0.0.1
IP2=10.0.0.2
IP61=fc00::1
IP62=fc00::2
IPBR1=10.0.0.11
IPBR2=10.0.0.12
IP6BR1=fc00::11
IP6BR2=fc00::12

DEBUG=${DEBUG:-false}
TIMEOUT=5

setup_endpoint()
{
	sock=${1}
	addr=${2}
	bus=${3}
	mode=${4}

	rump_server_add_iface $sock shmif0 $bus
	export RUMP_SERVER=${sock}
	if [ $mode = "ipv6" ]; then
		atf_check -s exit:0 rump.ifconfig shmif0 inet6 ${addr}
	else
		atf_check -s exit:0 rump.ifconfig shmif0 inet ${addr} netmask 0xffffff00
	fi

	atf_check -s exit:0 rump.ifconfig shmif0 up
	$DEBUG && rump.ifconfig shmif0
}

test_endpoint()
{
	sock=${1}
	addr=${2}
	bus=${3}
	mode=${4}

	export RUMP_SERVER=${sock}
	atf_check -s exit:0 -o match:shmif0 rump.ifconfig
	if [ $mode = "ipv6" ]; then
		atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT ${addr}
	else
		atf_check -s exit:0 -o ignore rump.ping -n -w $TIMEOUT -c 1 ${addr}
	fi
}

test_setup()
{
	test_endpoint $SOCK1 $IP1 bus1 ipv4
	test_endpoint $SOCK3 $IP2 bus2 ipv4

	export RUMP_SERVER=$SOCK2
	atf_check -s exit:0 -o match:shmif0 rump.ifconfig
	atf_check -s exit:0 -o match:shmif1 rump.ifconfig
}

test_setup6()
{
	test_endpoint $SOCK1 $IP61 bus1 ipv6
	test_endpoint $SOCK3 $IP62 bus2 ipv6

	export RUMP_SERVER=$SOCK2
	atf_check -s exit:0 -o match:shmif0 rump.ifconfig
	atf_check -s exit:0 -o match:shmif1 rump.ifconfig
}

setup_bridge_server()
{

	rump_server_add_iface $SOCK2 shmif0 bus1
	rump_server_add_iface $SOCK2 shmif1 bus2
	export RUMP_SERVER=$SOCK2
	atf_check -s exit:0 rump.ifconfig shmif0 up
	atf_check -s exit:0 rump.ifconfig shmif1 up
}

setup()
{

	rump_server_start $SOCK1 bridge
	rump_server_start $SOCK2 bridge
	rump_server_start $SOCK3 bridge

	setup_endpoint $SOCK1 $IP1 bus1 ipv4
	setup_endpoint $SOCK3 $IP2 bus2 ipv4
	setup_bridge_server
}

setup6()
{

	rump_server_start $SOCK1 netinet6 bridge
	rump_server_start $SOCK2 netinet6 bridge
	rump_server_start $SOCK3 netinet6 bridge

	setup_endpoint $SOCK1 $IP61 bus1 ipv6
	setup_endpoint $SOCK3 $IP62 bus2 ipv6
	setup_bridge_server
}

setup_bridge()
{
	export RUMP_SERVER=$SOCK2
	rump_server_add_iface $SOCK2 bridge0
	atf_check -s exit:0 rump.ifconfig bridge0 up

	export LD_PRELOAD=/usr/lib/librumphijack.so
	atf_check -s exit:0 /sbin/brconfig bridge0 add shmif0
	atf_check -s exit:0 /sbin/brconfig bridge0 add shmif1
	/sbin/brconfig bridge0
	unset LD_PRELOAD
	rump.ifconfig shmif0
	rump.ifconfig shmif1
}

setup_member_ip()
{
	export RUMP_SERVER=$SOCK2
	export LD_PRELOAD=/usr/lib/librumphijack.so
	atf_check -s exit:0 rump.ifconfig shmif0 $IPBR1/24
	atf_check -s exit:0 rump.ifconfig shmif1 $IPBR2/24
	atf_check -s exit:0 rump.ifconfig -w 10
	/sbin/brconfig bridge0
	unset LD_PRELOAD
	rump.ifconfig shmif0
	rump.ifconfig shmif1
}

setup_member_ip6()
{
	export RUMP_SERVER=$SOCK2
	export LD_PRELOAD=/usr/lib/librumphijack.so
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 $IP6BR1
	atf_check -s exit:0 rump.ifconfig shmif1 inet6 $IP6BR2
	atf_check -s exit:0 rump.ifconfig -w 10
	/sbin/brconfig bridge0
	unset LD_PRELOAD
	rump.ifconfig shmif0
	rump.ifconfig shmif1
}

teardown_bridge()
{
	export RUMP_SERVER=$SOCK2
	export LD_PRELOAD=/usr/lib/librumphijack.so
	/sbin/brconfig bridge0
	atf_check -s exit:0 /sbin/brconfig bridge0 delete shmif0
	atf_check -s exit:0 /sbin/brconfig bridge0 delete shmif1
	/sbin/brconfig bridge0
	unset LD_PRELOAD
	rump.ifconfig shmif0
	rump.ifconfig shmif1
}

test_setup_bridge()
{
	export RUMP_SERVER=$SOCK2
	export LD_PRELOAD=/usr/lib/librumphijack.so
	atf_check -s exit:0 -o match:shmif0 /sbin/brconfig bridge0
	atf_check -s exit:0 -o match:shmif1 /sbin/brconfig bridge0
	/sbin/brconfig bridge0
	unset LD_PRELOAD
}

down_up_interfaces()
{
	export RUMP_SERVER=$SOCK1
	rump.ifconfig shmif0 down
	rump.ifconfig shmif0 up
	export RUMP_SERVER=$SOCK3
	rump.ifconfig shmif0 down
	rump.ifconfig shmif0 up
}

test_ping_failure()
{
	export RUMP_SERVER=$SOCK1
	atf_check -s not-exit:0 -o ignore rump.ping -q -n -w $TIMEOUT -c 1 $IP2
	export RUMP_SERVER=$SOCK3
	atf_check -s not-exit:0 -o ignore rump.ping -q -n -w $TIMEOUT -c 1 $IP1
}

test_ping_success()
{
	export RUMP_SERVER=$SOCK1
	rump.ifconfig -v shmif0
	atf_check -s exit:0 -o ignore rump.ping -q -n -w $TIMEOUT -c 1 $IP2
	rump.ifconfig -v shmif0

	export RUMP_SERVER=$SOCK3
	rump.ifconfig -v shmif0
	atf_check -s exit:0 -o ignore rump.ping -q -n -w $TIMEOUT -c 1 $IP1
	rump.ifconfig -v shmif0
}

test_ping6_failure()
{
	export RUMP_SERVER=$SOCK1
	atf_check -s not-exit:0 -o ignore rump.ping6 -q -n -c 1 -X $TIMEOUT $IP62
	export RUMP_SERVER=$SOCK3
	atf_check -s not-exit:0 -o ignore rump.ping6 -q -n -c 1 -X $TIMEOUT $IP61
}

test_ping6_success()
{
	export RUMP_SERVER=$SOCK1
	rump.ifconfig -v shmif0
	atf_check -s exit:0 -o ignore rump.ping6 -q -n -c 1 -X $TIMEOUT $IP62
	rump.ifconfig -v shmif0

	export RUMP_SERVER=$SOCK3
	rump.ifconfig -v shmif0
	atf_check -s exit:0 -o ignore rump.ping6 -q -n -c 1 -X $TIMEOUT $IP61
	rump.ifconfig -v shmif0
}

test_ping_member()
{
	export RUMP_SERVER=$SOCK1
	rump.ifconfig -v shmif0
	atf_check -s exit:0 -o ignore rump.ping -q -n -w $TIMEOUT -c 1 $IPBR1
	rump.ifconfig -v shmif0
	# Test for PR#48104
	atf_check -s exit:0 -o ignore rump.ping -q -n -w $TIMEOUT -c 1 $IPBR2
	rump.ifconfig -v shmif0

	export RUMP_SERVER=$SOCK3
	rump.ifconfig -v shmif0
	# Test for PR#48104
	atf_check -s exit:0 -o ignore rump.ping -q -n -w $TIMEOUT -c 1 $IPBR1
	rump.ifconfig -v shmif0
	atf_check -s exit:0 -o ignore rump.ping -q -n -w $TIMEOUT -c 1 $IPBR2
	rump.ifconfig -v shmif0
}

test_ping6_member()
{
	export RUMP_SERVER=$SOCK1
	rump.ifconfig -v shmif0
	atf_check -s exit:0 -o ignore rump.ping6 -q -n -X $TIMEOUT -c 1 $IP6BR1
	rump.ifconfig -v shmif0
	# Test for PR#48104
	atf_check -s exit:0 -o ignore rump.ping6 -q -n -X $TIMEOUT -c 1 $IP6BR2
	rump.ifconfig -v shmif0

	export RUMP_SERVER=$SOCK3
	rump.ifconfig -v shmif0
	# Test for PR#48104
	atf_check -s exit:0 -o ignore rump.ping6 -q -n -X $TIMEOUT -c 1 $IP6BR1
	rump.ifconfig -v shmif0
	atf_check -s exit:0 -o ignore rump.ping6 -q -n -X $TIMEOUT -c 1 $IP6BR2
	rump.ifconfig -v shmif0
}

test_create_destroy()
{

	rump_server_start $SOCK1 bridge

	test_create_destroy_common $SOCK1 bridge0
}

test_ipv4()
{
	setup
	test_setup

	# Enable once PR kern/49219 is fixed
	#test_ping_failure

	setup_bridge
	sleep 1
	test_setup_bridge
	test_ping_success

	teardown_bridge
	test_ping_failure

	rump_server_destroy_ifaces
}

test_ipv6()
{
	setup6
	test_setup6

	test_ping6_failure

	setup_bridge
	sleep 1
	test_setup_bridge
	test_ping6_success

	teardown_bridge
	test_ping6_failure

	rump_server_destroy_ifaces
}

test_member_ipv4()
{
	setup
	test_setup

	# Enable once PR kern/49219 is fixed
	#test_ping_failure

	setup_bridge
	sleep 1
	test_setup_bridge
	test_ping_success

	setup_member_ip
	test_ping_member

	teardown_bridge
	test_ping_failure

	rump_server_destroy_ifaces
}

test_member_ipv6()
{
	setup6
	test_setup6

	test_ping6_failure

	setup_bridge
	sleep 1
	test_setup_bridge
	test_ping6_success

	setup_member_ip6
	test_ping6_member

	teardown_bridge
	test_ping6_failure

	rump_server_destroy_ifaces
}

BUS_SHMIF0=./bus0
BUS_SHMIF1=./bus1
BUS_SHMIF2=./bus2

unpack_file()
{

	atf_check -s exit:0 uudecode $(atf_get_srcdir)/${1}.uue
}

reset_if_stats()
{

	for ifname in shmif0 shmif1 shmif2
	do
		atf_check -s exit:0 -o ignore rump.ifconfig -z $ifname
	done
}

test_protection()
{

	unpack_file unicast.pcap
	unpack_file broadcast.pcap

	rump_server_start $SOCK1 bridge
	rump_server_add_iface $SOCK1 shmif0 $BUS_SHMIF0
	rump_server_add_iface $SOCK1 shmif1 $BUS_SHMIF1
	rump_server_add_iface $SOCK1 shmif2 $BUS_SHMIF2

	export RUMP_SERVER=$SOCK1
	atf_check -s exit:0 rump.ifconfig shmif0 up
	atf_check -s exit:0 rump.ifconfig shmif1 up
	atf_check -s exit:0 rump.ifconfig shmif2 up

	atf_check -s exit:0 rump.ifconfig bridge0 create
	atf_check -s exit:0 rump.ifconfig bridge0 up

	atf_check -s exit:0 $HIJACKING brconfig bridge0 add shmif0 add shmif1 add shmif2
	$DEBUG && rump.ifconfig

	# Protected interfaces: -
	# Learning: -
	# Input: unicast through shmif0
	# Output: shmif1, shmif2
	reset_if_stats
	atf_check -s exit:0 -o ignore shmif_pcapin unicast.pcap ${BUS_SHMIF0}
	atf_check -s exit:0 -o match:"input: 1 packet" rump.ifconfig -v shmif0
	atf_check -s exit:0 -o match:"output: 1 packet" rump.ifconfig -v shmif1
	atf_check -s exit:0 -o match:"output: 1 packet" rump.ifconfig -v shmif2
	$DEBUG && rump.ifconfig -v bridge0

	# Protected interfaces: -
	# Learning: -
	# Input: broadcast through shmif0
	# Output: shmif1, shmif2
	reset_if_stats
	atf_check -s exit:0 -o ignore shmif_pcapin broadcast.pcap ${BUS_SHMIF0}
	atf_check -s exit:0 -o match:"input: 1 packet" rump.ifconfig -v shmif0
	atf_check -s exit:0 -o match:"output: 1 packet" rump.ifconfig -v shmif1
	atf_check -s exit:0 -o match:"output: 1 packet" rump.ifconfig -v shmif2
	$DEBUG && rump.ifconfig -v bridge0

	# Protect shmif0 and shmif2
	atf_check -s exit:0 $HIJACKING brconfig bridge0 protect shmif0
	atf_check -s exit:0 $HIJACKING brconfig bridge0 protect shmif2
	atf_check -s exit:0 \
	    -o match:"shmif0.+PROTECTED" \
	    -o match:"shmif2.+PROTECTED" \
	    -o not-match:"shmif1.+PROTECTED" \
	    $HIJACKING brconfig bridge0

	# Protected interfaces: shmif0 shmif2
	# Learning: -
	# Input: unicast through shmif0
	# Output: shmif1
	reset_if_stats
	atf_check -s exit:0 -o ignore shmif_pcapin unicast.pcap ${BUS_SHMIF0}
	atf_check -s exit:0 -o match:"input: 1 packet" rump.ifconfig -v shmif0
	atf_check -s exit:0 -o match:"output: 1 packet" rump.ifconfig -v shmif1
	atf_check -s exit:0 -o match:"output: 0 packet" rump.ifconfig -v shmif2
	$DEBUG && rump.ifconfig -v bridge0

	# Protected interfaces: shmif0 shmif2
	# Learning: -
	# Input: broadcast through shmif0
	# Output: shmif1
	reset_if_stats
	atf_check -s exit:0 -o ignore shmif_pcapin broadcast.pcap ${BUS_SHMIF0}
	atf_check -s exit:0 -o match:"input: 1 packet" rump.ifconfig -v shmif0
	atf_check -s exit:0 -o match:"output: 1 packet" rump.ifconfig -v shmif1
	atf_check -s exit:0 -o match:"output: 0 packet" rump.ifconfig -v shmif2
	$DEBUG && rump.ifconfig -v bridge0

	# Insert a route 00:aa:aa:aa:aa:aa shmif2 to test forwarding path of known-unicast-frame
	atf_check -s exit:0 $HIJACKING brconfig bridge0 static shmif2 00:aa:aa:aa:aa:aa
	atf_check -s exit:0 -o match:'00:aa:aa:aa:aa:aa shmif2 0 flags=1<STATIC>' \
	    $HIJACKING brconfig bridge0
	$DEBUG && $HIJACKING brconfig bridge0

	# Protected interfaces: shmif0 shmif2
	# Learning: 00:aa:aa:aa:aa:aa shmif2
	# Input: broadcast through shmif0
	# Output: -
	reset_if_stats
	atf_check -s exit:0 -o ignore shmif_pcapin unicast.pcap ${BUS_SHMIF0}
	atf_check -s exit:0 -o match:"input: 1 packet" rump.ifconfig -v shmif0
	atf_check -s exit:0 -o match:"output: 0 packet" rump.ifconfig -v shmif1
	atf_check -s exit:0 -o match:"output: 0 packet" rump.ifconfig -v shmif2
	$DEBUG && rump.ifconfig -v bridge0

	# Unprotect shmif2
	atf_check -s exit:0 $HIJACKING brconfig bridge0 -protect shmif2
	atf_check -s exit:0 \
	    -o match:"shmif0.+PROTECTED" \
	    -o not-match:"shmif2.+PROTECTED" \
	    -o not-match:"shmif1.+PROTECTED" \
	    $HIJACKING brconfig bridge0

	# Protected interfaces: shmif0
	# Learning: 00:aa:aa:aa:aa:aa shmif2
	# Input: broadcast through shmif0
	# Output: shmif2
	reset_if_stats
	atf_check -s exit:0 -o ignore shmif_pcapin unicast.pcap ${BUS_SHMIF0}
	atf_check -s exit:0 -o match:"input: 1 packet" rump.ifconfig -v shmif0
	atf_check -s exit:0 -o match:"output: 0 packet" rump.ifconfig -v shmif1
	atf_check -s exit:0 -o match:"output: 1 packet" rump.ifconfig -v shmif2
	$DEBUG && rump.ifconfig -v bridge0

	rump_server_destroy_ifaces
}

add_test()
{
	local name=$1
	local desc="$2"

	atf_test_case "bridge_${name}" cleanup
	eval "bridge_${name}_head() {
			atf_set descr \"${desc}\"
			atf_set require.progs rump_server
		}
	    bridge_${name}_body() {
			test_${name}
		}
	    bridge_${name}_cleanup() {
			\$DEBUG && dump
			cleanup
		}"
	atf_add_test_case "bridge_${name}"
}

atf_init_test_cases()
{

	add_test create_destroy "Tests creating/destroying bridge interfaces"
	add_test ipv4           "Does basic if_bridge tests (IPv4)"
	add_test ipv6           "Does basic if_bridge tests (IPv6)"
	add_test member_ipv4    "Tests if_bridge with members with an IP address (IPv4)"
	add_test member_ipv6    "Tests if_bridge with members with an IP address (IPv6)"
	add_test protection     "Tests interface protection"
}
