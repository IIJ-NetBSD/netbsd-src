/* $NetBSD: prom.h,v 1.17 2024/03/02 20:15:33 thorpej Exp $ */

/*
 * Copyright (c) 1994, 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Keith Bostic, Chris G. Demetriou
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#ifndef	ASSEMBLER
struct prom_vec {
	uint64_t	routine;
	void		*routine_arg;
};

/* The return value from a prom call. */
typedef union {
	struct {
		uint64_t
			retval	: 32,		/* return value. */
			unit	: 8,
			mbz	: 8,
			error	: 13,
			status	: 3;
	} u;
	uint64_t bits;
} prom_return_t;

/* Linux kernel parameter block filled out by Qemu. */
struct linux_kernel_params {
	char		kernel_cmdline[256];
	uint64_t	initrd_base;
	uint64_t	initrd_size;
};

#ifdef _STANDALONE
int	getchar(void);
void	putchar(int);
#endif

void	prom_halt(int) __attribute__((__noreturn__));
int	prom_getenv(int, char *, int);
bool	prom_qemu_getenv(const char *, char *, size_t);

void	hwrpb_primary_init(void);
void	hwrpb_restart_setup(void);
#endif

/* Prom operation values. */
#define	PROM_R_CLOSE		0x11
#define	PROM_R_GETC		0x01
#define	PROM_R_GETENV		0x22
#define	PROM_R_OPEN		0x10
#define	PROM_R_PUTS		0x02
#define	PROM_R_READ		0x13
#define	PROM_R_WRITE		0x14
#define	PROM_R_IOCTL		0x12

/* Prom IOCTL operation subcodes */
#define	PROM_I_SKIP2IRG		1
#define	PROM_I_SKIP2MARK	2
#define	PROM_I_REWIND		3
#define	PROM_I_WRITEMARK	4

/* Environment variable values. */
#define	PROM_E_BOOTED_DEV	0x4
#define	PROM_E_BOOTED_FILE	0x6
#define	PROM_E_BOOTED_OSFLAGS	0x8
#define	PROM_E_TTY_DEV		0xf
#define	PROM_E_SCSIID		0x42
#define	PROM_E_SCSIFAST		0x43

#if defined(_STANDALONE) || defined(ENABLEPROM)
/*
 * These can't be called from the kernel without great care.
 *
 * There have to be stub routines to do the copying that ensures that the
 * PROM doesn't get called with an address larger than 32 bits.  Calls that
 * either don't need to copy anything, or don't need the copy because it's
 * already being done elsewhere, are defined here.
 */
#define	prom_open(dev, len)						\
	prom_dispatch(PROM_R_OPEN, (dev), (len), 0, 0)
#define	prom_close(chan)						\
	prom_dispatch(PROM_R_CLOSE, chan, 0, 0, 0)
#define	prom_read(chan, len, buf, blkno)				\
	prom_dispatch(PROM_R_READ, chan, len, (uint64_t)buf, blkno)
#define	prom_write(chan, len, buf, blkno)				\
	prom_dispatch(PROM_R_WRITE, chan, len, (uint64_t)buf, blkno)
#define	prom_ioctl(chan, op, count)					\
	prom_dispatch(PROM_R_IOCTL, chan, op, (int64_t)count, 0, 0)
#define	prom_putstr(chan, str, len)					\
	prom_dispatch(PROM_R_PUTS, chan, (uint64_t)str, len, 0)
#define	prom_getc(chan)							\
	prom_dispatch(PROM_R_GETC, chan, 0, 0, 0)
#define prom_getenv_disp(id, buf, len)					\
	prom_dispatch(PROM_R_GETENV, id, (uint64_t)buf, len, 0)
#endif

#ifndef ASSEMBLER
#ifdef _KERNEL

#ifdef _KERNEL_OPT
#include "opt_dec_kn7aa.h"
#include "opt_dec_kn8ae.h"

#if defined(DEC_KN7AA) || defined(DEC_KN8AE)
#define _PROM_MAY_USE_PROM_CONSOLE	/* XXX */
#endif /* DEC_KN8AE */
#endif /* _KERNEL_OPT */

extern bool prom_interface_initialized;

bool	prom_uses_prom_console(void);

void	prom_enter(void);
void	prom_leave(void);

void	promcnputc(dev_t, int);
int	promcngetc(dev_t);
int	promcnlookc(dev_t, char *);

uint64_t	prom_dispatch(uint64_t, uint64_t, uint64_t, uint64_t,
		    uint64_t);
#endif /* _KERNEL */
#endif /* ASSEMBLER */
