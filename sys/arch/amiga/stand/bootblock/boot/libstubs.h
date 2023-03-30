/* $NetBSD: libstubs.h,v 1.7.94.1 2023/03/30 11:54:33 martin Exp $ */

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Ignatios Souvatzis.
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

#include "amigaio.h"
#include "amigagraph.h"
#include "amigatypes.h"
#include <sys/types.h>

extern struct ExecBase *SysBase;
extern struct Library *IntuitionBase;
extern struct Library *ExpansionBase;

void *AllocMem (size_t, u_int32_t);
void FreeMem (void *, size_t);

struct Library *OpenLibrary (const char *, u_int32_t);
void CloseLibrary (struct Library *);
struct MsgPort *CreateMsgPort(void);
void *CreateIORequest(struct MsgPort *, u_int32_t);
void DeleteIORequest(void *);
void DeleteMsgPort(struct MsgPort *);

u_int8_t DoIO(struct AmigaIO *);
void SendIO(struct AmigaIO *);
struct AmigaIO *CheckIO(struct AmigaIO *);
void *WaitPort(struct MsgPort *);
void AbortIO(struct AmigaIO *);
u_int8_t WaitIO(struct AmigaIO *);

void RawIOInit(void);
int32_t RawPutChar(int32_t c);
int32_t RawMayGetChar(void);

int OpenDevice(const char *, u_int32_t, struct AmigaIO *, u_int32_t);
#ifdef _PRIMARY_BOOT
void CloseDevice(struct AmigaIO *);
#endif

void *FindResident(const char *);
void *InitResident(const char *, u_int32_t);
void *OpenResource(const char *);

u_int32_t CachePreDMA(u_int32_t, u_int32_t *, int);
#define DMAF_Continue		2
#define DMAF_NoModify		4
#define DMAF_ReadFromRAM	8

void Forbid(void);
void Permit(void);

struct Screen *OpenScreenTagList(struct NewScreen *, const u_int32_t *);
struct Screen *OpenScreenTag(struct NewScreen *, ...);
struct Window *OpenWindowTagList(struct Window *, const u_int32_t *);
struct Window *OpenWindowTag(struct Window *, ...);
#ifdef _PRIMARY_BOOT
void CloseScreen(struct Screen *);
void CloseWindow(struct Window *);
#endif

#ifdef nomore
u_int32_t mytime(void);
#endif

struct cfdev *FindConfigDev(struct cfdev *, int, int);

#ifndef DOINLINES
void CacheClearU(void);
#else
#define LibCallNone(lib, what)  \
	__asm("movl a6,sp@-; movl %0,a6; " what "; movl sp@+,a6" :: \
	    "r"(lib) : "d0", "d1", "a0", "a1")

#define CacheClearU() LibCallNone(SysBase, "jsr a6@(-0x27c)")
#endif
