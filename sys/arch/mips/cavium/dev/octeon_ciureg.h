/*	$NetBSD: octeon_ciureg.h,v 1.11 2020/07/20 17:56:13 jmcneill Exp $	*/

/*
 * Copyright (c) 2007 Internet Initiative Japan, Inc.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * CIU Registers
 */

#ifndef _OCTEON_CIUREG_H_
#define _OCTEON_CIUREG_H_

/* ---- register addresses */

#define	CIU_INT0_SUM0				UINT64_C(0x0001070000000000)
#define	CIU_INT1_SUM0				UINT64_C(0x0001070000000008)
#define	CIU_INT2_SUM0				UINT64_C(0x0001070000000010)
#define	CIU_INT3_SUM0				UINT64_C(0x0001070000000018)
#define	CIU_IP2_SUM0(n)				(CIU_INT0_SUM0 + 0x10 * (n))
#define	CIU_IP3_SUM0(n)				(CIU_INT1_SUM0 + 0x10 * (n))
#define	CIU_INT32_SUM0				UINT64_C(0x0001070000000100)
#define	CIU_INT_SUM1				UINT64_C(0x0001070000000108)
#define	CIU_INT0_EN0				UINT64_C(0x0001070000000200)
#define	CIU_INT1_EN0				UINT64_C(0x0001070000000210)
#define	CIU_INT2_EN0				UINT64_C(0x0001070000000220)
#define	CIU_INT3_EN0				UINT64_C(0x0001070000000230)
#define	CIU_IP2_EN0(n)				(CIU_INT0_EN0 + 0x20 * (n))
#define	CIU_IP3_EN0(n)				(CIU_INT1_EN0 + 0x20 * (n))
#define	CIU_INT32_EN0				UINT64_C(0x0001070000000400)
#define	CIU_INT0_EN1				UINT64_C(0x0001070000000208)
#define	CIU_INT1_EN1				UINT64_C(0x0001070000000218)
#define	CIU_INT2_EN1				UINT64_C(0x0001070000000228)
#define	CIU_INT3_EN1				UINT64_C(0x0001070000000238)
#define	CIU_IP2_EN1(n)				(CIU_INT0_EN1 + 0x20 * (n))
#define	CIU_IP3_EN1(n)				(CIU_INT1_EN1 + 0x20 * (n))
#define	CIU_INT32_EN1				UINT64_C(0x0001070000000408)
#define	CIU_TIM0				UINT64_C(0x0001070000000480)
#define	CIU_TIM1				UINT64_C(0x0001070000000488)
#define	CIU_TIM2				UINT64_C(0x0001070000000490)
#define	CIU_TIM3				UINT64_C(0x0001070000000498)
#define	CIU_WDOG0				UINT64_C(0x0001070000000500)
#define	CIU_WDOG(n)				(CIU_WDOG0 + (n) * 8)
#define	CIU_PP_POKE0				UINT64_C(0x0001070000000580)
#define	CIU_PP_POKE1				UINT64_C(0x0001070000000588)
#define	CIU_PP_POKE(n)				(CIU_PP_POKE0 + (n) * 8)
#define	CIU_MBOX_SET0				UINT64_C(0x0001070000000600)
#define	CIU_MBOX_SET1				UINT64_C(0x0001070000000608)
#define	CIU_MBOX_SET(n)				(CIU_MBOX_SET0 + (n) * 8)
#define	CIU_MBOX_CLR0				UINT64_C(0x0001070000000680)
#define	CIU_MBOX_CLR1				UINT64_C(0x0001070000000688)
#define	CIU_MBOX_CLR(n)				(CIU_MBOX_CLR0 + (n) * 8)
#define	CIU_PP_RST				UINT64_C(0x0001070000000700)
#define	CIU_PP_DBG				UINT64_C(0x0001070000000708)
#define	CIU_GSTOP				UINT64_C(0x0001070000000710)
#define	CIU_NMI					UINT64_C(0x0001070000000718)
#define	CIU_DINT				UINT64_C(0x0001070000000720)
#define	CIU_FUSE				UINT64_C(0x0001070000000728)
#define	CIU_BIST				UINT64_C(0x0001070000000730)
#define	CIU_SOFT_BIST				UINT64_C(0x0001070000000738)
#define	CIU_SOFT_RST				UINT64_C(0x0001070000000740)
#define	CIU_SOFT_PRST				UINT64_C(0x0001070000000748)
#define	CIU_PCI_INTA				UINT64_C(0x0001070000000750)
#define	CIU_INT4_SUM0				UINT64_C(0x0001070000000c00)
#define	CIU_INT4_SUM1				UINT64_C(0x0001070000000c08)
#define	CIU_IP4_SUM0(n)				(CIU_INT4_SUM0 + 0x8 * (n))
#define	CIU_INT4_EN00				UINT64_C(0x0001070000000c80)
#define	CIU_INT4_EN01				UINT64_C(0x0001070000000c88)
#define	CIU_INT4_EN10				UINT64_C(0x0001070000000c90)
#define	CIU_INT4_EN11				UINT64_C(0x0001070000000c98)
#define	CIU_IP4_EN0(n)				(CIU_INT4_EN00 + 0x10 * (n))
#define	CIU_IP4_EN1(n)				(CIU_INT4_EN01 + 0x10 * (n))

#define	CIU_BASE				UINT64_C(0x0001070000000000)

#define	CIU_INT0_SUM0_OFFSET			0x0000
#define	CIU_INT1_SUM0_OFFSET			0x0008
#define	CIU_INT2_SUM0_OFFSET			0x0010
#define	CIU_INT3_SUM0_OFFSET			0x0018
#define	CIU_INT32_SUM0_OFFSET			0x0100
#define	CIU_INT_SUM1_OFFSET			0x0108
#define	CIU_INT0_EN0_OFFSET			0x0200
#define	CIU_INT1_EN0_OFFSET			0x0210
#define	CIU_INT2_EN0_OFFSET			0x0220
#define	CIU_INT3_EN0_OFFSET			0x0230
#define	CIU_INT32_EN0_OFFSET			0x0400
#define	CIU_INT0_EN1_OFFSET			0x0208
#define	CIU_INT1_EN1_OFFSET			0x0218
#define	CIU_INT2_EN1_OFFSET			0x0228
#define	CIU_INT3_EN1_OFFSET			0x0238
#define	CIU_INT32_EN1_OFFSET			0x0408
#define	CIU_TIM0_OFFSET				0x0480
#define	CIU_TIM1_OFFSET				0x0488
#define	CIU_TIM2_OFFSET				0x0490
#define	CIU_TIM3_OFFSET				0x0498
#define	CIU_WDOG0_OFFSET			0x0500
#define	CIU_WDOG1_OFFSET			0x0508
#define	CIU_PP_POKE0_OFFSET			0x0580
#define	CIU_PP_POKE1_OFFSET			0x0588
#define	CIU_MBOX_SET0_OFFSET			0x0600
#define	CIU_MBOX_SET1_OFFSET			0x0608
#define	CIU_MBOX_CLR0_OFFSET			0x0680
#define	CIU_MBOX_CLR1_OFFSET			0x0688
#define	CIU_PP_RST_OFFSET			0x0700
#define	CIU_PP_DBG_OFFSET			0x0708
#define	CIU_GSTOP_OFFSET			0x0710
#define	CIU_NMI_OFFSET				0x0718
#define	CIU_DINT_OFFSET				0x0720
#define	CIU_FUSE_OFFSET				0x0728
#define	CIU_BIST_OFFSET				0x0730
#define	CIU_SOFT_BIST_OFFSET			0x0738
#define	CIU_SOFT_RST_OFFSET			0x0740
#define	CIU_SOFT_PRST_OFFSET			0x0748
#define	CIU_PCI_INTA_OFFSET			0x0750

/* ---- register bits */

/* interrupt numbers */

#define	CIU_INT_BOOTDMA				63
#define	CIU_INT_MII				62
#define	CIU_INT_IPDPPTHR			61
#define	CIU_INT_POWIQ				60
#define	CIU_INT_TWSI2				59
#define	CIU_INT_MPI				58
#define	CIU_INT_PCM				57
#define	CIU_INT_USB				56
#define	CIU_INT_TIMER_3				55
#define	CIU_INT_TIMER_2				54
#define	CIU_INT_TIMER_1				53
#define	CIU_INT_TIMER_0				52
#define	CIU_INT_KEY_ZERO			51
#define	CIU_INT_IPD_DRP				50
#define	CIU_INT_GMX_DRP2			49
#define	CIU_INT_GMX_DRP				48
#define	CIU_INT_TRACE				47
#define	CIU_INT_RML				46
#define	CIU_INT_TWSI				45
#define	CIU_INT_WDOG_SUM			44
#define	CIU_INT_PCI_MSI_63_48			43
#define	CIU_INT_PCI_MSI_47_32			42
#define	CIU_INT_PCI_MSI_31_16			41
#define	CIU_INT_PCI_MSI_15_0			40
#define	CIU_INT_PCI_INT_D			39
#define	CIU_INT_PCI_INT_C			38
#define	CIU_INT_PCI_INT_B			37
#define	CIU_INT_PCI_INT_A			36
#define	CIU_INT_UART_1				35
#define	CIU_INT_UART_0				34
#define	CIU_INT_MBOX_31_16			33
#define	CIU_INT_MBOX_15_0			32
#define	CIU_INT_GPIO_15				31
#define	CIU_INT_GPIO_14				30
#define	CIU_INT_GPIO_13				29
#define	CIU_INT_GPIO_12				28
#define	CIU_INT_GPIO_11				27
#define	CIU_INT_GPIO_10				26
#define	CIU_INT_GPIO_9				25
#define	CIU_INT_GPIO_8				24
#define	CIU_INT_GPIO_7				23
#define	CIU_INT_GPIO_6				22
#define	CIU_INT_GPIO_5				21
#define	CIU_INT_GPIO_4				20
#define	CIU_INT_GPIO_3				19
#define	CIU_INT_GPIO_2				18
#define	CIU_INT_GPIO_1				17
#define	CIU_INT_GPIO_0				16
#define	CIU_INT_WORKQ_15			15
#define	CIU_INT_WORKQ_14			14
#define	CIU_INT_WORKQ_13			13
#define	CIU_INT_WORKQ_12			12
#define	CIU_INT_WORKQ_11			11
#define	CIU_INT_WORKQ_10			10
#define	CIU_INT_WORKQ_9				 9
#define	CIU_INT_WORKQ_8				 8
#define	CIU_INT_WORKQ_7				 7
#define	CIU_INT_WORKQ_6				 6
#define	CIU_INT_WORKQ_5				 5
#define	CIU_INT_WORKQ_4				 4
#define	CIU_INT_WORKQ_3				 3
#define	CIU_INT_WORKQ_2				 2
#define	CIU_INT_WORKQ_1				 1
#define	CIU_INT_WORKQ_0				 0

#define	CUI_INT_WDOG_15				 15
#define	CUI_INT_WDOG_14				 14
#define	CUI_INT_WDOG_13				 13
#define	CUI_INT_WDOG_12				 12
#define	CUI_INT_WDOG_11				 11
#define	CUI_INT_WDOG_10				 10
#define	CUI_INT_WDOG_9				  9
#define	CUI_INT_WDOG_8				  8
#define	CUI_INT_WDOG_7				  7
#define	CUI_INT_WDOG_6				  6
#define	CUI_INT_WDOG_5				  5
#define	CUI_INT_WDOG_4				  4
#define	CUI_INT_WDOG_3				  3
#define	CUI_INT_WDOG_2				  2
#define	CUI_INT_WDOG_1				  1
#define	CUI_INT_WDOG_0				  0

#define	CIU_TIMX_XXX_63_37			UINT64_C(0xffffffe000000000)
#define	CIU_TIMX_ONE_SHOT			UINT64_C(0x0000001000000000)
#define	CIU_TIMX_LEN				UINT64_C(0x0000000fffffffff)

#define	CIU_WDOGX_XXX_63_46			UINT64_C(0xffffc00000000000)
#define	CIU_WDOGX_GSTOPEN			UINT64_C(0x0000200000000000)
#define	CIU_WDOGX_DSTOP				UINT64_C(0x0000100000000000)
#define	CIU_WDOGX_CNT				UINT64_C(0x00000ffffff00000)
#define	CIU_WDOGX_LEN				UINT64_C(0x00000000000ffff0)
#define	CIU_WDOGX_STATE				UINT64_C(0x000000000000000c)
#define	CIU_WDOGX_MODE				UINT64_C(0x0000000000000003)
#define	  CIU_WDOGX_MODE_OFF			  0
#define	  CIU_WDOGX_MODE_INTR			  1
#define	  CIU_WDOGX_MODE_INTR_NMI		  2
#define	  CIU_WDOGX_MODE_INTR_NMI_SOFT		  3

#define	CIU_PP_POKEX_XXX_63_0			UINT64_C(0xffffffffffffffff)

#define	CIU_MBOX_SETX_XXX_63_32			UINT64_C(0xffffffff00000000)
#define	CIU_MBOX_SETX_SET			UINT64_C(0x00000000ffffffff)

#define	CIU_MBOX_CLRX_XXX_63_32			UINT64_C(0xffffffff00000000)
#define	CIU_MBOX_CLRX_CLR			UINT64_C(0x00000000ffffffff)

#define	CIU_PP_RST_RST				UINT64_C(0x0000ffffffffffff)
#define	CIU_PP_RST_RST0				UINT64_C(0x0000000000000001)

#define	CIU_PP_DBG_PPDBG			UINT64_C(0x0000ffffffffffff)

#define	CIU_GSTOP_XXX_63_1			UINT64_C(0xfffffffffffffffe)
#define	CIU_GSTOP_GSTOP				UINT64_C(0x0000000000000001)

#define	CIU_NMI_NMI				UINT64_C(0x0000ffffffffffff)

#define	CIU_DINT_DINT				UINT64_C(0x0000ffffffffffff)

#define	CIU_FUSE_FUSE				UINT64_C(0x0000ffffffffffff)

#define	CIU_BIST_BIST				UINT64_C(0x0000ffffffffffff)

#define	CIU_SOFT_BIST_XXX_63_1			UINT64_C(0xfffffffffffffffe)
#define	CIU_SOFT_BIST_SOFT_BIST			UINT64_C(0x0000000000000001)

#define	CIU_SOFT_RST_XXX_63_1			UINT64_C(0xfffffffffffffffe)
#define	CIU_SOFT_RST_SOFT_RST			UINT64_C(0x0000000000000001)

#define	CIU_SOFT_PRST_XXX_63_4			UINT64_C(0xfffffffffffffff8)
#define	CIU_SOFT_PRST_HOST64			UINT64_C(0x0000000000000004)
#define	CIU_SOFT_PRST_NPI			UINT64_C(0x0000000000000002)
#define	CIU_SOFT_PRST_SOFT_PRST			UINT64_C(0x0000000000000001)

#define	CIU_PCI_INTA_XXX_63_2			UINT64_C(0xfffffffffffffffc)
#define	CIU_PCI_INTA_INT			UINT64_C(0x0000000000000003)

#endif /* _OCTEON_CIUREG_H_ */
