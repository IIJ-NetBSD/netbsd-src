#	$NetBSD: Makefile,v 1.1 2025/07/17 14:25:28 christos Exp $

.include "bsd.own.mk"

CRYPTODIST=${NETBSDSRCDIR}/crypto
.include "${NETBSDSRCDIR}/crypto/Makefile.openssl"

regen:
	for i in $$(find ${OPENSSLSRC} -name \*alpha\*.pl); do \
		case $$i in \
		(*) perl -I${OPENSSLSRC}/crypto/perlasm \
		-I${OPENSSLSRC}/crypto/bn/asm $$i linux /dev/stdout \
		> $$(basename $$i .pl).S;; \
		esac; \
	done
