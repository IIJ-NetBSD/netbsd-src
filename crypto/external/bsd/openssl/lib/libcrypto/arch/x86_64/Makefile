#	$NetBSD: Makefile,v 1.14 2025/04/23 20:14:59 christos Exp $

.include "bsd.own.mk"

CRYPTODIST=${NETBSDSRCDIR}/crypto
.include "${NETBSDSRCDIR}/crypto/Makefile.openssl"

.if make(regen) && ${HAVE_LLVM:U} == "yes"
CC+= -fno-integrated-as
.endif

regen:
	for i in $$(find ${OPENSSLSRC} -name \*${MACHINE_ARCH}.pl) \
		$$(find ${OPENSSLSRC}/crypto/bn/asm -name ${MACHINE_ARCH}-\*.pl) \
		${OPENSSLSRC}/crypto/bn/asm/rsaz-avx2.pl \
		${OPENSSLSRC}/crypto/bn/asm/rsaz-avx512.pl \
		${OPENSSLSRC}/crypto/${MACHINE_ARCH}cpuid.pl ; do \
		j=$$(basename $$i .pl).S; \
		case $$j in \
		(sha512-x86_64.S) \
		    perl $$i sha256-x86_64.S; perl $$i sha512-x86_64.S;; \
		(*) \
		    (echo "#include <machine/asm.h>"; CC=${CC:Q} perl $$i elf | sed \
			-e 's/.hidden	OPENSSL_cpuid_setup/.globl	OPENSSL_cpuid_setup/' \
			-e 's/call	OPENSSL_cpuid_setup/call	PIC_PLT(OPENSSL_cpuid_setup)/' \
			-e 's/rdseedq/rdseed/') > $$j;; \
		esac; \
	done
