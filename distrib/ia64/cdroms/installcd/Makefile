#	$NetBSD: Makefile,v 1.5 2025/05/07 11:47:26 martin Exp $
CDBASE=		ia64cd		# gives ${CDBASE}.iso

#CDINSTKERNEL=	../../instkernel/instkernel
CDKERNELS=	${KERNELCOMPDIR}/netbsd netbsd
CDRELEASE=	true			# include $RELEASEDIR/$RELEASEMACHINEDIR
CDRELEASE_NODEBUG=	true

# don't make the CD bootable, there is no installboot support for ia64
# image_md_post:
#	${TOOL_INSTALLBOOT} -m${MACHINE} ${CDIMAGE} bootxx.${MACHINE}

.include <bsd.own.mk>
KERNELCOMPDIR!= cd ${.CURDIR}/../../../../sys/arch/${MACHINE}/compile/GENERIC && ${PRINTOBJDIR}

.include "${.CURDIR}/../../../common/Makefile.bootcd"

