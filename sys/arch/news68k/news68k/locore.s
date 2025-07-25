/*	$NetBSD: locore.s,v 1.87 2025/07/08 11:45:26 thorpej Exp $	*/

/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1980, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *
 * from: Utah $Hdr: locore.s 1.66 92/12/22$
 *
 *	@(#)locore.s	8.6 (Berkeley) 5/27/94
 */

/*
 * locore.s for news68k - based on mvme68k and hp300 version
 */

#include "opt_compat_netbsd.h"
#include "opt_compat_sunos.h"
#include "opt_fpsp.h"
#include "opt_ddb.h"
#include "opt_kgdb.h"
#include "opt_lockdebug.h"
#include "opt_fpu_emulate.h"
#include "opt_m68k_arch.h"

#include "assym.h"
#include <machine/asm.h>
#include <machine/trap.h>

#include "ksyms.h"

/*
 * Temporary stack for a variety of purposes.
 * Try and make this the first thing is the data segment so it
 * is page aligned.  Note that if we overflow here, we run into
 * our text segment.
 */
	.data
	.space	PAGE_SIZE
ASLOCAL(tmpstk)

ASLOCAL(monitor)
	.long	0

/*
 * Macro to relocate a symbol, used before MMU is enabled.
 */
#define	_RELOC(var, ar)		\
	lea	var,ar;		\
	addl	%a5,ar

#define	RELOC(var, ar)		_RELOC(_C_LABEL(var), ar)
#define	ASRELOC(var, ar)	_RELOC(_ASM_LABEL(var), ar)

/*
 * LED control for DEBUG.
 */
#define	IMMEDIATE	#
#define	SETLED(func)	\
	movl	IMMEDIATE func,%d0; \
	jmp	debug_led

#define	SETLED2(func)	\
	movl	IMMEDIATE func,%d0; \
	jmp	debug_led2

#define	TOMONITOR	\
	moveal	_ASM_LABEL(monitor), %a0; \
	jmp	%a0@
/*
 * Initialization
 *
 * The bootstrap loader loads us in starting at 0, and VBR is non-zero.
 * On entry, args on stack are boot device, boot filename, console unit,
 * boot flags (howto), boot device name, filesystem type name.
 */
BSS(lowram,4)
BSS(esym,4)

/*
 * This is for kvm_mkdb, and should be the address of the beginning
 * of the kernel text segment (not necessarily the same as kernbase).
 */
	.text
GLOBAL(kernel_text)

/*
 * start of kernel and .text!
 */
ASENTRY_NOPROFILE(start)
	movw	#PSL_HIGHIPL,%sr	| no interrupts

	movl	#0x0, %a5		| RAM starts at 0 (%a5)
	movl	#0x0, %a6		| clear %fp to terminate debug trace

	RELOC(bootdev,%a0)
	movl	%d6, %a0@		| save bootdev
	RELOC(boothowto,%a0)
	movl	%d7, %a0@		| save boothowto

	ASRELOC(tmpstk,%a0)
	movl	%a0,%sp			| give ourselves a temporary stack

	movc %vbr,%a0
	movl %a0@(188),_ASM_LABEL(monitor)| save trap #15 to return PROM monitor
	movl %a0@(128),_ASM_LABEL(romcallvec)| save trap #0 to use PROM calls

	RELOC(esym, %a0)
#if NKSYMS || defined(DDB) || defined(MODULAR)
	movl	%d2,%a0@		| store end of symbol table
#else
	clrl	%a0@
#endif
	/* %d2 now free */
	RELOC(lowram, %a0)
	movl	%a5,%a0			| store start of physical memory
	movl	#CACHE_OFF,%d0
	movc	%d0,%cacr		| clear and disable on-chip cache(s)

	movl	#DC_FREEZE,%d0		| data freeze bit
	movc	%d0,%cacr		|  only exists on 68030
	movc	%cacr,%d0		| read it back
	tstl	%d0			| zero?
	jeq	Lnot68030		| yes, we have 68020/68040

	movl	#CACHE_OFF,%d0
	movc	%d0,%cacr		| clear data freeze bit

	RELOC(mmutype,%a0)
	movl	#MMU_68030,%a0@
	RELOC(cputype,%a0)
	movl	#CPU_68030,%a0@
	RELOC(fputype,%a0)
	movl	#FPU_68882,%a0@

	movl	%d6,%d0			| check bootdev
	andl	#0x00007000,%d0		| BOOTDEV_HOST
	cmpl	#0x00007000,%d0		| news1200?
	jne	Lnot1200		| no, then skip

	/* news1200 */
	/* XXX Are these needed?*/
	sf	0xe1100000		| AST disable (?)
	sf	0xe10c0000		| level2 interrupt disable (?)
	moveb	#0x03,0xe1140002	| timer set (?)
	moveb	#0xd0,0xe1140003	| timer set (?)
	sf	0xe1140000		| timer interrupt disable (?)
	/* XXX */

	RELOC(systype,%a0)
	movl	#NEWS1200,%a0@
	RELOC(ectype, %a0)		|
	movl	#EC_NONE,%a0@		| news1200 have no L2 cache

	/*
	 * Fix up the physical addresses of the news1200's onboard
	 * I/O registers.
	 */
	RELOC(intiobase_phys, %a0);
	movl	#INTIOBASE1200,%a0@
	RELOC(intiotop_phys, %a0);
	movl	#INTIOTOP1200,%a0@

	RELOC(extiobase_phys, %a0);
	movl	#EXTIOBASE1200,%a0@
	RELOC(extiotop_phys, %a0);
	movl	#EXTIOTOP1200,%a0@

	RELOC(ctrl_power, %a0);
	movl	#CTRL_POWER1200,%a0@	| CTRL_POWER port for news1200
	RELOC(ctrl_led_phys, %a0);
	movl	#CTRL_LED1200,%a0@	| CTRL_LED port for news1200
	jra	Lcom030

Lnot1200:
	tstl	%d0			| news1400/1500/1600/1700?
	jne	Lnotyet			| no, then skip

	/* news1400/1500/1600/1700 */
	/* XXX Are these needed?*/
	sf	0xe1280000		| AST disable (?)
	sf	0xe1180000		| level2 interrupt disable (?)
	st	0xe1300000		| L2 cache enable (?)
	st	0xe1900000		| L2 cache clear (?)
	sf	0xe1000000		| timer interrupt disable (?)
	moveb	#0x36,0xe0c80000	| XXX reset FDC for PWS-1560
	/* XXX */

	/* news1400/1500/1600/1700 - 68030 CPU/MMU, 68882 FPU */
	RELOC(systype,%a0)
	movl	#NEWS1700,%a0@

	cmpb	#0xf2,0xe1c00000	| read model id from idrom
	jle	1f			|  news1600/1700 ?

	RELOC(ectype, %a0)		| no, we are news1400/1500
	movl	#EC_NONE,%a0		|  and do not have L2 cache
	jra	2f
1:
	RELOC(ectype, %a0)		| yes, we are news1600/1700
	movl	#EC_PHYS,%a0@		|  and have a physical address cache
2:
	/*
	 * Fix up the physical addresses of the news1700's onboard
	 * I/O registers.
	 */
	RELOC(intiobase_phys, %a0);
	movl	#INTIOBASE1700,%a0@
	RELOC(intiotop_phys, %a0);
	movl	#INTIOTOP1700,%a0@

	RELOC(extiobase_phys, %a0);
	movl	#EXTIOBASE1700,%a0@
	RELOC(extiotop_phys, %a0);
	movl	#EXTIOTOP1700,%a0@

	RELOC(ctrl_power, %a0);
	movl	#CTRL_POWER1700,%a0@	| CTRL_POWER port for news1700
	RELOC(ctrl_led_phys, %a0);
	movl	#CTRL_LED1700,%a0@	| CTRL_LED port for news1700
Lcom030:

	movl	%d4,%d1
	addl	%a5,%d1
	moveq	#PGSHIFT,%d2
	lsrl	%d2,%d1			| convert to page (click) number
	RELOC(maxmem, %a0)
	movl	%d1,%a0@		| save as maxmem

	movl	%d4,%d1			|
	moveq	#PGSHIFT,%d2
	lsrl	%d2,%d1			| convert to page (click) number
	RELOC(physmem, %a0)
	movl	%d1,%a0@		| save as physmem

	jra	Lstart1
Lnot68030:

#ifdef news700 /* XXX eventually... */
	RELOC(mmutype,%a0)
	movl	#MMU_68851,%a0@
	RELOC(cputype,%a0)
	movl	#CPU_68020,%a0@
	RELOC(fputype,%a0)
	movl	#FPU_68881,%a0@
	RELOC(ectype, %a0)
	movl	#EC_NONE,%a0@
#if 1	/* XXX */
	jra	Lnotyet
#else
	/* XXX more XXX */
	jra	Lstart1
#endif
Lnot700:
#endif

	/*
	 * If we fall to here, the board is not supported.
	 * Just drop out to the monitor.
	 */

	TOMONITOR
	/* NOTREACHED */

Lnotyet:
	/*
	 * If we get here, it means a particular model
	 * doesn't have the necessary support code in the
	 * kernel.  Just drop out to the monitor.
	 */
	TOMONITOR
	/* NOTREACHED */

Lstart1:
/* initialize source/destination control registers for movs */
	moveq	#FC_USERD,%d0		| user space
	movc	%d0,%sfc		|   as source
	movc	%d0,%dfc		|   and destination of transfers
/*
 * configure kernel and lwp0 VA space so we can get going
 */
	.globl	_Sysseg_pa, _pmap_bootstrap, _avail_start

#if NKSYMS || defined(DDB) || defined(MODULAR)
	RELOC(esym,%a0)			| end of static kernel test/data/syms
	movl	%a0@,%d2
	jne	Lstart2
#endif
	RELOC(end,%a0)
	movl	%a0,%d2			| end of static kernel text/data
Lstart2:
	addl	#PAGE_SIZE-1,%d2
	andl	#PG_FRAME,%d2		| round to a page
	movl	%d2,%a4
	addl	%a5,%a4			| convert to PA
	pea	%a5@			| firstpa
	pea	%a4@			| nextpa
	RELOC(pmap_bootstrap,%a0)
	jbsr	%a0@			| pmap_bootstrap(firstpa, nextpa)
	addql	#8,%sp
/*
 * Enable the MMU.
 * Since the kernel is mapped logical == physical, we just turn it on.
 */
	RELOC(Sysseg_pa, %a0)		| system segment table addr
	movl	%a0@,%d1		| read value (a PA)
	RELOC(mmutype, %a0)
	cmpl	#MMU_68040,%a0@		| 68040?
	jne	Lmotommu1		| no, skip
	.long	0x4e7b1807		| movc %d1,%srp
	jra	Lstploaddone
Lmotommu1:
#ifdef M68030
	RELOC(protorp, %a0)
	movl	%d1,%a0@(4)		| segtable address
	pmove	%a0@,%srp		| load the supervisor root pointer
#endif /* M68030 */
Lstploaddone:
#ifdef M68040
	RELOC(mmutype, %a0)
	cmpl	#MMU_68040,%a0@		| 68040?
	jne	Lmotommu2		| no, skip

	RELOC(mmu_tt40, %a0)		| pointer to TT reg values
	movl	%a0,%sp@-
	RELOC(mmu_load_tt40,%a0)	| pass it to mmu_load_tt40()
	jbsr	%a0@
	addql	#4,%sp

	.word	0xf4d8			| cinva bc
	.word	0xf518			| pflusha
	movl	#MMU40_TCR_BITS,%d0
	.long	0x4e7b0003		| movc %d0,%tc
	movl	#CACHE40_ON,%d0
	movc	%d0,%cacr		| turn on both caches
	jmp	Lenab1
Lmotommu2:
#endif /* M68040 */
	RELOC(mmu_tt30, %a0)		| pointer to TT reg values
	movl	%a0,%sp@-
	RELOC(mmu_load_tt30,%a0)	| pass it to mmu_load_tt30()
	jbsr	%a0@
	addql	#4,%sp

	pflusha
	RELOC(prototc, %a2)
	pmove	%a2@,%tc		| load it

/*
 * Should be running mapped from this point on
 */
Lenab1:
	lea	_ASM_LABEL(tmpstk),%sp	| re-load temporary stack
	jbsr	_C_LABEL(vec_init)	| initialize vector table
/* call final pmap setup */
	jbsr	_C_LABEL(pmap_bootstrap_finalize)
/* set kernel stack, user SP */
	movl	_C_LABEL(lwp0uarea),%a1	| get lwp0 uarea
	lea	%a1@(USPACE-4),%sp	|   set kernel stack to end of area
	movl	#USRSTACK-4,%a2
	movl	%a2,%usp		| init user SP

	tstl	_C_LABEL(fputype)	| Have an FPU?
	jeq	Lenab2			| No, skip.
	clrl	%a1@(PCB_FPCTX)		| ensure null FP context
	movl	%a1,%sp@-
	jbsr	_C_LABEL(m68881_restore) | restore it (does not kill a1)
	addql	#4,%sp
Lenab2:
	jbsr	_C_LABEL(_TBIA)		| invalidate TLB
	cmpl	#MMU_68040,_C_LABEL(mmutype)	| 68040?
	jeq	Ltbia040		| yes, cache already on
	pflusha
	tstl	_C_LABEL(mmutype)
	jpl	Lenab3			| 68851 implies no d-cache
	movl	#CACHE_ON,%d0
	tstl	_C_LABEL(ectype)	| have external cache?
	jne	1f			| Yes, skip
	orl	#CACHE_BE,%d0		| set cache burst enable
1:
	movc	%d0,%cacr		| clear cache
	tstl	_C_LABEL(ectype)	| have external cache?
	jeq	Lenab3			| No, skip
	movl	_C_LABEL(cache_clr),%a0
	st	%a0@			| flush external cache
	jra	Lenab3
Ltbia040:
	.word	0xf518
Lenab3:
/* final setup for C code */
	jbsr	_C_LABEL(news68k_init)	| additional pre-main initialization

/*
 * Create a fake exception frame so that cpu_lwp_fork() can copy it.
 * main() nevers returns; we exit to user mode from a forked process
 * later on.
 */
	clrw	%sp@-			| vector offset/frame type
	clrl	%sp@-			| PC - filled in by "execve"
	movw	#PSL_USER,%sp@-		| in user mode
	clrl	%sp@-			| stack adjust count and padding
	lea	%sp@(-64),%sp		| construct space for D0-D7/A0-A7
	lea	_C_LABEL(lwp0),%a0	| save pointer to frame
	movl	%sp,%a0@(L_MD_REGS)	|   in lwp0.l_md.md_regs

	jra	_C_LABEL(main)		| main()

	SETLED2(3);			| main returned?

/*
 * Trap/interrupt vector routines
 */
#include <m68k/m68k/trap_subr.s>

/*
 * Use common m68k bus error and address error handlers.
 */
#include <m68k/m68k/busaddrerr.s>

/*
 * FP exceptions.
 */
ENTRY_NOPROFILE(fpfline)
#ifdef FPU_EMULATE
	clrl	%sp@-			| stack adjust count
	moveml	#0xFFFF,%sp@-		| save registers
	moveq	#T_FPEMULI,%d0		| denote as FP emulation trap
	jra	_ASM_LABEL(fault)	| do it
#else
	jra	_C_LABEL(illinst)
#endif

ENTRY_NOPROFILE(fpunsupp)
#ifdef FPU_EMULATE
	clrl	%sp@-			| stack adjust count
	moveml	#0xFFFF,%sp@-		| save registers
	moveq	#T_FPEMULD,%d0		| denote as FP emulation trap
	jra	_ASM_LABEL(fault)	| do it
#else
	jra	_C_LABEL(illinst)
#endif

/*
 * Handles all other FP coprocessor exceptions.
 * Note that since some FP exceptions generate mid-instruction frames
 * and may cause signal delivery, we need to test for stack adjustment
 * after the trap call.
 */
ENTRY_NOPROFILE(fpfault)
	clrl	%sp@-		| stack adjust count
	moveml	#0xFFFF,%sp@-	| save user registers
	movl	%usp,%a0	| and save
	movl	%a0,%sp@(FR_SP)	|   the user stack pointer
	clrl	%sp@-		| no VA arg
	movl	_C_LABEL(curpcb),%a0 | current pcb
	lea	%a0@(PCB_FPCTX),%a0 | address of FP savearea
	fsave	%a0@		| save state
	tstb	%a0@		| null state frame?
	jeq	Lfptnull	| yes, safe
	clrw	%d0		| no, need to tweak BIU
	movb	%a0@(1),%d0	| get frame size
	bset	#3,%a0@(0,%d0:w)| set exc_pend bit of BIU
Lfptnull:
	fmovem	%fpsr,%sp@-	| push fpsr as code argument
	frestore %a0@		| restore state
	movl	#T_FPERR,%sp@-	| push type arg
	jra	_ASM_LABEL(faultstkadj) | call trap and deal with stack cleanup


/*
 * Other exceptions only cause four and six word stack frame and require
 * no post-trap stack adjustment.
 */

ENTRY_NOPROFILE(badtrap)
	moveml	#0xC0C0,%sp@-		| save scratch regs
	movw	%sp@(22),%sp@-		| push exception vector info
	clrw	%sp@-
	movl	%sp@(22),%sp@-		| and PC
	jbsr	_C_LABEL(straytrap)	| report
	addql	#8,%sp			| pop args
	moveml	%sp@+,#0x0303		| restore regs
	jra	_ASM_LABEL(rei)		| all done

ENTRY_NOPROFILE(trap0)
	clrl	%sp@-			| stack adjust count
	moveml	#0xFFFF,%sp@-		| save user registers
	movl	%usp,%a0		| save the user SP
	movl	%a0,%sp@(FR_SP)		|   in the savearea
	movl	%d0,%sp@-		| push syscall number
	jbsr	_C_LABEL(syscall)	| handle it
	addql	#4,%sp			| pop syscall arg
	tstl	_C_LABEL(astpending)	| AST pending?
	jne	Lrei			| yes, handle it via trap
	movl	%sp@(FR_SP),%a0		| grab and restore
	movl	%a0,%usp		|   user SP
	moveml	%sp@+,#0x7FFF		| restore most registers
	addql	#8,%sp			| pop SP and stack adjust
	rte

/*
 * Trap 12 is the entry point for the cachectl "syscall" (both HPUX & BSD)
 *	cachectl(command, addr, length)
 * command in %d0, addr in %a1, length in %d1
 */
ENTRY_NOPROFILE(trap12)
	movl	_C_LABEL(curlwp),%a0
	movl	%a0@(L_PROC),%sp@-	| push curproc pointer
	movl	%d1,%sp@-		| push length
	movl	%a1,%sp@-		| push addr
	movl	%d0,%sp@-		| push command
	jbsr	_C_LABEL(cachectl1)	| do it
	lea	%sp@(16),%sp		| pop args
	jra	_ASM_LABEL(rei)		| all done

/*
 * Trace (single-step) trap.  Kernel-mode is special.
 * User mode traps are simply passed on to trap().
 */
ENTRY_NOPROFILE(trace)
	clrl	%sp@-			| stack adjust count
	moveml	#0xFFFF,%sp@-
	moveq	#T_TRACE,%d0

	| Check PSW and see what happen.
	|   T=0 S=0	(should not happen)
	|   T=1 S=0	trace trap from user mode
	|   T=0 S=1	trace trap on a trap instruction
	|   T=1 S=1	trace trap from system mode (kernel breakpoint)

	movw	%sp@(FR_HW),%d1		| get PSW
	notw	%d1			| XXX no support for T0 on 680[234]0
	andw	#PSL_TS,%d1		| from system mode (T=1, S=1)?
	jeq	Lkbrkpt			| yes, kernel breakpoint
	jra	_ASM_LABEL(fault)	| no, user-mode fault

/*
 * Trap 15 is used for:
 *	- GDB breakpoints (in user programs)
 *	- KGDB breakpoints (in the kernel)
 *	- trace traps for SUN binaries (not fully supported yet)
 * User mode traps are simply passed to trap().
 */
ENTRY_NOPROFILE(trap15)
	clrl	%sp@-			| stack adjust count
	moveml	#0xFFFF,%sp@-
	moveq	#T_TRAP15,%d0
	movw	%sp@(FR_HW),%d1		| get PSW
	andw	#PSL_S,%d1		| from system mode?
	jne	Lkbrkpt			| yes, kernel breakpoint
	jra	_ASM_LABEL(fault)	| no, user-mode fault

Lkbrkpt: | Kernel-mode breakpoint or trace trap. (d0=trap_type)
	| Save the system sp rather than the user sp.
	movw	#PSL_HIGHIPL,%sr	| lock out interrupts
	lea	%sp@(FR_SIZE),%a6	| Save stack pointer
	movl	%a6,%sp@(FR_SP)		|  from before trap

	| If were are not on tmpstk switch to it.
	| (so debugger can change the stack pointer)
	movl	%a6,%d1
	cmpl	#_ASM_LABEL(tmpstk),%d1
	jls	Lbrkpt2			| already on tmpstk
	| Copy frame to the temporary stack
	movl	%sp,%a0			| a0=src
	lea	_ASM_LABEL(tmpstk)-96,%a1 | a1=dst
	movl	%a1,%sp			| sp=new frame
	movql	#FR_SIZE,%d1
Lbrkpt1:
	movl	%a0@+,%a1@+
	subql	#4,%d1
	bgt	Lbrkpt1

Lbrkpt2:
	| Call the trap handler for the kernel debugger.
	| Do not call trap() to do it, so that we can
	| set breakpoints in trap() if we want.  We know
	| the trap type is either T_TRACE or T_BREAKPOINT.
	| If we have both DDB and KGDB, let KGDB see it first,
	| because KGDB will just return 0 if not connected.
	| Save args in %d2, %a2
	movl	%d0,%d2			| trap type
	movl	%sp,%a2			| frame ptr
#ifdef KGDB
	| Let KGDB handle it (if connected)
	movl	%a2,%sp@-		| push frame ptr
	movl	%d2,%sp@-		| push trap type
	jbsr	_C_LABEL(kgdb_trap)	| handle the trap
	addql	#8,%sp			| pop args
	cmpl	#0,%d0			| did kgdb handle it?
	jne	Lbrkpt3			| yes, done
#endif
#ifdef DDB
	| Let DDB handle it
	movl	%a2,%sp@-		| push frame ptr
	movl	%d2,%sp@-		| push trap type
	jbsr	_C_LABEL(kdb_trap)	| handle the trap
	addql	#8,%sp			| pop args
#if 0	/* not needed on hp300 */
	cmpl	#0,%d0			| did ddb handle it?
	jne	Lbrkpt3			| yes, done
#endif
#endif
	/* Sun 3 drops into PROM here. */
Lbrkpt3:
	| The stack pointer may have been modified, or
	| data below it modified (by kgdb push call),
	| so push the hardware frame at the current sp
	| before restoring registers and returning.

	movl	%sp@(FR_SP),%a0		| modified %sp
	lea	%sp@(FR_SIZE),%a1	| end of our frame
	movl	%a1@-,%a0@-		| copy 2 longs with
	movl	%a1@-,%a0@-		| ... predecrement
	movl	%a0,%sp@(FR_SP)		| %sp = h/w frame
	moveml	%sp@+,#0x7FFF		| restore all but %sp
	movl	%sp@,%sp		| ... and %sp
	rte				| all done

/*
 * Interrupt handlers.
 */

ENTRY_NOPROFILE(lev1intr)		/* Level 1: AST interrupt */
	addql	#1,_C_LABEL(intr_depth)
	INTERRUPT_SAVEREG
	CPUINFO_INCREMENT(CI_NINTR)
	addql	#1,_C_LABEL(m68k_intr_evcnt)+AST_INTRCNT
	movl	_C_LABEL(ctrl_ast),%a0
	clrb	%a0@			| disable AST interrupt
	INTERRUPT_RESTOREREG
	subql	#1,_C_LABEL(intr_depth)
	jra	_ASM_LABEL(rei)		| handle AST

#ifdef notyet
ENTRY_NOPROFILE(_softintr)		/* Level 2: software interrupt */
	INTERRUPT_SAVEREG
	jbsr	_C_LABEL(softintr_dispatch)
	INTERRUPT_RESTOREREG
	rte
#endif

ENTRY_NOPROFILE(lev3intr)		/* Level 3: fd, lpt, vme etc. */
	addql	#1,_C_LABEL(intr_depth)
	INTERRUPT_SAVEREG
	jbsr	_C_LABEL(intrhand_lev3)
	INTERRUPT_RESTOREREG
	subql	#1,_C_LABEL(intr_depth)
	rte

ENTRY_NOPROFILE(lev4intr)		/* Level 4: scsi, le, vme etc. */
	addql	#1,_C_LABEL(intr_depth)
	INTERRUPT_SAVEREG
	jbsr	_C_LABEL(intrhand_lev4)
	INTERRUPT_RESTOREREG
	subql	#1,_C_LABEL(intr_depth)
	rte

ENTRY_NOPROFILE(_isr_clock)		/* Level 6: clock (see clock_hb.c) */
	addql	#1,_C_LABEL(intr_depth)
	INTERRUPT_SAVEREG
	jbsr	_C_LABEL(clock_intr)
	INTERRUPT_RESTOREREG
	subql	#1,_C_LABEL(intr_depth)
	rte

#if 0
ENTRY_NOPROFILE(lev7intr)		/* Level 7: NMI */
	addql	#1,_C_LABEL(intrcnt)+NMI_INTRCNT
	clrl	%sp@-
	moveml	#0xFFFF,%sp@-		| save registers
	movl	%usp,%a0		| and save
	movl	%a0,%sp@(FR_SP)		|   the user stack pointer
	jbsr	_C_LABEL(nmintr)	| call handler: XXX wrapper
	movl	%sp@(FR_SP),%a0		| restore
	movl	%a0,%usp		|   user SP
	moveml	%sp@+,#0x7FFF		| and remaining registers
	addql	#8,%sp			| pop SP and stack adjust
	rte
#endif

/*
 * Emulation of VAX REI instruction.
 *
 * This code deals with checking for and servicing
 * ASTs (profiling, scheduling).
 * After identifying that we need an AST we drop the IPL
 * to allow device interrupts.
 *
 * This code is complicated by the fact that sendsig may have been called
 * necessitating a stack cleanup.
 */
/*
 * news68k has hardware support for AST,
 * so only traps (including system call) and
 * the AST interrupt use this REI function.
 */

ASENTRY_NOPROFILE(rei)
	tstl	_C_LABEL(astpending)	| AST pending?
	jne	1f			| no, done
	rte
1:
	btst	#5,%sp@			| yes, are we returning to user mode?
	jeq	2f			| no, done
	rte
2:
	movw	#PSL_LOWIPL,%sr		| lower SPL
	clrl	%sp@-			| stack adjust
	moveml	#0xFFFF,%sp@-		| save all registers
	movl	%usp,%a1		| including
	movl	%a1,%sp@(FR_SP)		|    the users SP
Lrei:
	clrl	%sp@-			| VA == none
	clrl	%sp@-			| code == none
	movl	#T_ASTFLT,%sp@-		| type == async system trap
	pea	%sp@(12)		| fp == address of trap frame
	jbsr	_C_LABEL(trap)		| go handle it
	lea	%sp@(16),%sp		| pop value args
	movl	%sp@(FR_SP),%a0		| restore user SP
	movl	%a0,%usp		|   from save area
	movw	%sp@(FR_ADJ),%d0	| need to adjust stack?
	jne	Laststkadj		| yes, go to it
	moveml	%sp@+,#0x7FFF		| no, restore most user regs
	addql	#8,%sp			| toss SP and stack adjust
	rte				| and do real RTE
Laststkadj:
	lea	%sp@(FR_HW),%a1		| pointer to HW frame
	addql	#8,%a1			| source pointer
	movl	%a1,%a0			| source
	addw	%d0,%a0			|  + hole size = dest pointer
	movl	%a1@-,%a0@-		| copy
	movl	%a1@-,%a0@-		|  8 bytes
	movl	%a0,%sp@(FR_SP)		| new SSP
	moveml	%sp@+,#0x7FFF		| restore user registers
	movl	%sp@,%sp		| and our SP
	rte				| real return

/*
 * Primitives
 */

/*
 * Use common m68k process/lwp switch and context save subroutines.
 */
#define FPCOPROC	/* XXX: Temp. Reqd. */
#include <m68k/m68k/switch_subr.s>


ENTRY(ecacheon)
	tstl	_C_LABEL(ectype)
	jeq	Lnocache7
	movl	_C_LABEL(cache_ctl),%a0
	st	%a0@			| NEWS-OS does `st 0xe1300000'
Lnocache7:
	rts

ENTRY(ecacheoff)
	tstl	_C_LABEL(ectype)
	jeq	Lnocache8
	movl	_C_LABEL(cache_ctl),%a0
	sf	%a0@			| NEWS-OS does `sf 0xe1300000'
Lnocache8:
	rts

/*
 * _delay(unsigned N)
 *
 * Delay for at least (N/256) microseconds.
 * This routine depends on the variable:  delay_divisor
 * which should be set based on the CPU clock rate.
 */
ENTRY_NOPROFILE(_delay)
	| %d0 = arg = (usecs << 8)
	movl	%sp@(4),%d0
	| %d1 = delay_divisor
	movl	_C_LABEL(delay_divisor),%d1
	jra	L_delay			/* Jump into the loop! */

	/*
	 * Align the branch target of the loop to a half-line (8-byte)
	 * boundary to minimize cache effects.  This guarantees both
	 * that there will be no prefetch stalls due to cache line burst
	 * operations and that the loop will run from a single cache
	 * half-line.
	 */
	.align  8
L_delay:
	subl	%d1,%d0
	jgt	L_delay
	rts

/*
 * Handle the nitty-gritty of rebooting the machine.
 * Basically we just turn off the MMU, restore the PROM's initial VBR
 * and jump through the PROM halt vector with argument via %d7
 * depending on how the system was halted.
 */
ENTRY_NOPROFILE(doboot)
#if defined(M68040)
	cmpl	#MMU_68040,_C_LABEL(mmutype)	| 68040?
	jeq	Lnocache5		| yes, skip
#endif
	movl	#CACHE_OFF,%d0
	movc	%d0,%cacr		| disable on-chip cache(s)
Lnocache5:
	movl	_C_LABEL(boothowto),%d7	| load howto
	movl	_C_LABEL(bootdev),%d6	| load bootdev
	movl	%sp@(4),%d2		| arg
	movl	_C_LABEL(ctrl_power),%a0| CTRL_POWER port
	movl	_C_LABEL(saved_vbr),%d3	| Fetch original VBR value
	lea	_ASM_LABEL(tmpstk),%sp	| physical SP in case of NMI
	movl	#0,%a7@-		| value for pmove to TC (turn off MMU)
	pmove	%a7@,%tc		| disable MMU
	movc	%d3,%vbr		| Restore monitor's VBR
	movl	%d2,%d0			|
	andl	#0x800,%d0		| mask off
	tstl	%d0			| power down?
	beq	1f			|
	clrb	%a0@			| clear CTRL_POWER port
1:
	tstl	%d2			| autoboot?
	beq	2f			| yes!
	movl	%d2,%d7			|
2:
	trap	#15
	/* NOTREACHED */

/*
 * debug_led():
 * control LED routines for debugging
 * used as break point before printf enabled
 */
ASENTRY_NOPROFILE(debug_led)
	RELOC(ctrl_led_phys,%a0)	| assume %a5 still has base address
	movl	%d0,%a0@

1:	nop
	jmp	1b
	rts

/*
 * debug_led2():
 * similar to debug_led(), but used after MMU enabled
 */
ASENTRY_NOPROFILE(debug_led2)
	movl	_C_LABEL(ctrl_led_phys),%d1
	subl	_C_LABEL(intiobase_phys),%d1
	addl	_C_LABEL(intiobase),%d1
	movl    %d1,%a0
	movl	%d0,%a0@

1:	nop
	jmp	1b
	rts


/*
 * Misc. global variables.
 */
	.data

GLOBAL(systype)
	.long	NEWS1700	| default to NEWS1700

GLOBAL(mmutype)
	.long	MMU_68030	| default to MMU_68030

GLOBAL(cputype)
	.long	CPU_68030	| default to CPU_68030

GLOBAL(fputype)
	.long	FPU_68882	| default to FPU_68882

GLOBAL(ectype)
	.long	EC_NONE		| external cache type, default to none

GLOBAL(prototc)
	.long	MMU51_TCR_BITS	| prototype translation control

/*
 * Information from first stage boot program
 */
GLOBAL(bootpart)
	.long	0
GLOBAL(bootdevlun)
	.long	0
GLOBAL(bootctrllun)
	.long	0
GLOBAL(bootaddr)
	.long	0

GLOBAL(intiobase)
	.long	0		| KVA of base of internal IO space

GLOBAL(extiobase)
	.long	0		| KVA of base of internal IO space

GLOBAL(intiolimit)
	.long	0		| KVA of end of internal IO space

GLOBAL(intiobase_phys)
	.long	0		| PA of board's I/O registers

GLOBAL(intiotop_phys)
	.long	0		| PA of top of board's I/O registers

GLOBAL(extiobase_phys)
	.long	0		| PA of external I/O registers

GLOBAL(extiotop_phys)
	.long	0		| PA of top of external I/O registers

GLOBAL(ctrl_power)
	.long	0		| PA of power control port

GLOBAL(ctrl_led_phys)
	.long	0		| PA of LED control port

GLOBAL(cache_ctl)
	.long	0		| KVA of external cache control port

GLOBAL(cache_clr)
	.long	0		| KVA of external cache clear port

GLOBAL(romcallvec)
	.long	0
