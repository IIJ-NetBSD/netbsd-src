#! /bin/sh
# $OpenLDAP$
## This work is part of OpenLDAP Software <http://www.openldap.org/>.
##
## Copyright 1998-2024 The OpenLDAP Foundation.
## All rights reserved.
##
## Redistribution and use in source and binary forms, with or without
## modification, are permitted only as authorized by the OpenLDAP
## Public License.
##
## A copy of this license is available in the file LICENSE in the
## top-level directory of the distribution or, alternatively, at
## <http://www.OpenLDAP.org/license.html>.
if [ x"$WITH_SASL" = x"yes" -a x"$USE_SASL" != x"no" ] ; then
	SASL="sasl"
	if [ x"$USE_SASL" = x"yes" ] ; then
		USE_SASL=SCRAM-SHA-256
	fi
	SASL_MECH="\"saslmech=$USE_SASL\""
else
	SASL="nosasl"
	SASL_MECH=
fi
sed -e "s/@BACKEND@/${BACKEND}/"			\
	-e "s/^#${BACKEND}#//"				\
	-e "/^#~/s/^#[^#]*~${BACKEND}~[^#]*#/#omit: /"	\
		-e "s/^#~[^#]*~#//"			\
	-e "s/@RELAY@/${RELAY}/"			\
	-e "s/^#relay-${RELAY}#//"			\
	-e "s/^#${BACKENDTYPE}#//"			\
	-e "s/^#${AC_TLS_TYPE}#//"			\
	-e "s/^#${AC_ldap}#//"				\
	-e "s/^#${AC_meta}#//"				\
	-e "s/^#${AC_asyncmeta}#//"			\
	-e "s/^#${AC_relay}#//"				\
	-e "s/^#${AC_sql}#//"				\
	-e "s/^#${RDBMS}#//"			\
	-e "s/^#${AC_accesslog}#//"			\
	-e "s/^#${AC_dds}#//"				\
	-e "s/^#${AC_deref}#//"				\
	-e "s/^#${AC_dynlist}#//"			\
	-e "s/^#${AC_homedir}#//"			\
	-e "s/^#${AC_memberof}#//"			\
	-e "s/^#${AC_nestgroup}#//"			\
	-e "s/^#${AC_pcache}#//"			\
	-e "s/^#${AC_ppolicy}#//"			\
	-e "s/^#${AC_refint}#//"			\
	-e "s/^#${AC_retcode}#//"			\
	-e "s/^#${AC_remoteauth}#//"			\
	-e "s/^#${AC_rwm}#//"				\
	-e "s/^#${AC_syncprov}#//"			\
	-e "s/^#${AC_translucent}#//"			\
	-e "s/^#${AC_unique}#//"			\
	-e "s/^#${AC_valsort}#//"			\
	-e "s/^#${INDEXDB}#//"				\
	-e "s/^#${MAINDB}#//"				\
	-e "s/^#${SASL}#//"				\
	-e "s/^#${ACI}#//"				\
	-e "s;@URI1@;${URI1};"				\
	-e "s;@URI2@;${URI2};"				\
	-e "s;@URI3@;${URI3};"				\
	-e "s;@URI4@;${URI4};"				\
	-e "s;@URI5@;${URI5};"				\
	-e "s;@URI6@;${URI6};"				\
	-e "s;@PORT1@;${PORT1};"			\
	-e "s;@PORT2@;${PORT2};"			\
	-e "s;@PORT3@;${PORT3};"			\
	-e "s;@PORT4@;${PORT4};"			\
	-e "s;@PORT5@;${PORT5};"			\
	-e "s;@PORT6@;${PORT6};"			\
	-e "s;@SURI1@;${SURI1};"			\
	-e "s;@SURI2@;${SURI2};"			\
	-e "s;@SURI3@;${SURI3};"			\
	-e "s;@SURI4@;${SURI4};"			\
	-e "s;@SURI5@;${SURI5};"			\
	-e "s;@SURI6@;${SURI6};"			\
	-e "s;@URIP1@;${URIP1};"			\
	-e "s;@URIP2@;${URIP2};"			\
	-e "s;@URIP3@;${URIP3};"			\
	-e "s;@URIP4@;${URIP4};"			\
	-e "s;@URIP5@;${URIP5};"			\
	-e "s;@URIP6@;${URIP6};"			\
	-e "s;@SURIP1@;${SURIP1};"			\
	-e "s;@SURIP2@;${SURIP2};"			\
	-e "s;@SURIP3@;${SURIP3};"			\
	-e "s;@SURIP4@;${SURIP4};"			\
	-e "s;@SURIP5@;${SURIP5};"			\
	-e "s;@SURIP6@;${SURIP6};"			\
	-e "s/@SASL_MECH@/${SASL_MECH}/"		\
	-e "s;@TESTDIR@;${TESTDIR};"			\
	-e "s;@TESTWD@;${TESTWD};"			\
	-e "s;@DATADIR@;${DATADIR};"			\
	-e "s;@SCHEMADIR@;${SCHEMADIR};"		\
	-e "s;@KRB5REALM@;${KRB5REALM};"		\
	-e "s;@KDCHOST@;${KDCHOST};"			\
	-e "s;@KDCPORT@;${KDCPORT};"			\
	-e "s;@TIMEOUT@;${TIMEOUT};"			\
	-e "/^#/d"
