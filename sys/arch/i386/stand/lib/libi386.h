/*	$NetBSD: libi386.h,v 1.55 2025/05/06 18:16:12 pgoyette Exp $	*/

/*
 * Copyright (c) 1996
 *	Matthias Drochner.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef	__I386_STAND_LIBI386_H__
#define	__I386_STAND_LIBI386_H__

typedef unsigned long physaddr_t;

/* this is in startup code */
void vpbcopy(const void *, void *, size_t);
void pvbcopy(const void *, void *, size_t);
void pbzero(void *, size_t);
physaddr_t vtophys(void *);

ssize_t pread(int, void *, size_t);
void startprog(physaddr_t, uint32_t, uint32_t *, physaddr_t);
void multiboot(physaddr_t, physaddr_t, physaddr_t, uint32_t);

int exec_netbsd(const char *, physaddr_t, int, int, void (*)(void));
int exec_multiboot(const char *, char *);

void delay(int);
int getbasemem(void);
int getextmemx(void);
int getextmem1(void);
int biosvideomode(void);
#ifdef CONSERVATIVE_MEMDETECT
#define getextmem() getextmem1()
#else
#define getextmem() getextmemx()
#endif
void reboot(void);
void gateA20(void);

void clear_pc_screen(void);
void initio(int);
#define CONSDEV_PC 0
#define CONSDEV_COM0 1
#define CONSDEV_COM1 2
#define CONSDEV_COM2 3
#define CONSDEV_COM3 4
#define CONSDEV_COM0KBD 5
#define CONSDEV_COM1KBD 6
#define CONSDEV_COM2KBD 7
#define CONSDEV_COM3KBD 8
#define CONSDEV_AUTO (-1)
int iskey(int);
char awaitkey(int, int);
void wait_sec(int);

/* multiboot */
struct multiboot_package;
struct multiboot_package_priv;

struct multiboot_package {
	int			 mbp_version;
	struct multiboot_header	*mbp_header;
	const char		*mbp_file;
	char			*mbp_args;
	u_long			 mbp_basemem;
	u_long			 mbp_extmem;
	u_long			 mbp_loadaddr;
	u_long			*mbp_marks;
	struct multiboot_package_priv*mbp_priv;
	struct multiboot_package*(*mbp_probe)(const char *);
	int			(*mbp_exec)(struct multiboot_package *);
	void			(*mbp_cleanup)(struct multiboot_package *);
};

struct multiboot_package *probe_multiboot1(const char *);
struct multiboot_package *probe_multiboot2(const char *);

/* this is in "user code"! */
int parsebootfile(const char *, char **, char **, int *, int *, const char **);

/* parseutils.c */
char *gettrailer(char *);
int parseopts(const char *, int *);
int parseboot(char *, char **, int *);

/* menuutils.c */
struct bootblk_command {
	const char *c_name;
	void (*c_fn)(char *);
};
void bootmenu(void);
void docommand(char *);

/* in "user code": */
void command_help(char *);
extern const struct bootblk_command commands[];

/* asm bios/dos calls */
__compactcall int biosdisk_extread(int, void *);
int biosdisk_read(int, int, int, int, int, void *);
__compactcall int biosdisk_reset(int);

__compactcall int biosgetrtc(u_long *);
int biosgetsystime(void);
int comgetc(int);
void cominit(int);
__compactcall int computc(int, int);
int comstatus(int);
int congetc(void);
int conisshift(void);
int coniskey(void);
__compactcall void conputc(int);
void conclr(void);

int getextmem2(int *);
__compactcall int getextmemps2(void *);
int getmementry(int *, int *);

__compactcall int biosdisk_int13ext(int);
__compactcall int biosdisk_getinfo(int);
struct biosdisk_extinfo;
__compactcall int biosdisk_getextinfo(int, struct biosdisk_extinfo *);
int get_harddrives(void);
void biosdisk_probe(void);

void module_add(char *);
void splash_add(char *);
void rnd_add(char *);
void fs_add(char *);
void userconf_add(char *);
void module_add_split(const char *, uint8_t);

/* Note: implementations differ in boot2, dosboot & efiboot */
void command_dev(char *);

struct btinfo_framebuffer;
void framebuffer_configure(struct btinfo_framebuffer *);

void ksyms_addr_set(void *, void *, void *);

#endif	/* __I386_STAND_LIBI386_H__ */
