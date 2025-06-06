/*	$NetBSD: headers.c,v 1.75 2025/05/02 23:03:16 riastradh Exp $	 */

/*
 * Copyright 1996 John D. Polstra.
 * Copyright 1996 Matt Thomas <matt@3am-software.com>
 * Copyright 2002 Charles M. Hannum <root@ihack.net>
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by John Polstra.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */

/*
 * Dynamic linker for ELF.
 *
 * John Polstra <jdp@polstra.com>.
 */

#include <sys/cdefs.h>
#ifndef lint
__RCSID("$NetBSD: headers.c,v 1.75 2025/05/02 23:03:16 riastradh Exp $");
#endif /* not lint */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/bitops.h>
#include <dirent.h>

#include "debug.h"
#include "rtld.h"

/*
 * Process a shared object's DYNAMIC section, and save the important
 * information in its Obj_Entry structure.
 */
void
_rtld_digest_dynamic(const char *execname, Obj_Entry *obj)
{
	Elf_Dyn        *dynp;
	Needed_Entry  **needed_tail = &obj->needed;
	const Elf_Dyn  *dyn_soname = NULL;
	const Elf_Dyn  *dyn_rpath = NULL;
	bool		use_pltrel = false;
	bool		use_pltrela = false;
	Elf_Addr        relsz = 0, relasz = 0, relrsz = 0;
	Elf_Addr	pltrel = 0, pltrelsz = 0;
#ifdef RTLD_LOADER
	Elf_Addr	init = 0, fini = 0;
#endif

	dbg(("headers: digesting PT_DYNAMIC at %p", obj->dynamic));
	for (dynp = obj->dynamic; dynp->d_tag != DT_NULL; ++dynp) {
		dbg(("  d_tag %ld at %p", (long)dynp->d_tag, dynp));
		switch (dynp->d_tag) {

		case DT_REL:
			obj->rel = (const Elf_Rel *)
			    (obj->relocbase + dynp->d_un.d_ptr);
			break;

		case DT_RELSZ:
			relsz = dynp->d_un.d_val;
			break;

		case DT_RELENT:
			assert(dynp->d_un.d_val == sizeof(Elf_Rel));
			break;

		case DT_JMPREL:
			pltrel = dynp->d_un.d_ptr;
			break;

		case DT_PLTRELSZ:
			pltrelsz = dynp->d_un.d_val;
			break;

		case DT_RELA:
			obj->rela = (const Elf_Rela *)
			    (obj->relocbase + dynp->d_un.d_ptr);
			break;

		case DT_RELASZ:
			relasz = dynp->d_un.d_val;
			break;

		case DT_RELAENT:
			assert(dynp->d_un.d_val == sizeof(Elf_Rela));
			break;

		case DT_RELR:
			obj->relr = (const Elf_Relr *)(obj->relocbase +
			    dynp->d_un.d_ptr);
			break;

		case DT_RELRSZ:
			relrsz = dynp->d_un.d_val;
			break;

		case DT_RELRENT:
			assert(dynp->d_un.d_val == sizeof(Elf_Relr));
			break;

		case DT_PLTREL:
			use_pltrel = dynp->d_un.d_val == DT_REL;
			use_pltrela = dynp->d_un.d_val == DT_RELA;
			assert(use_pltrel || use_pltrela);
			break;

		case DT_SYMTAB:
			obj->symtab = (const Elf_Sym *)
				(obj->relocbase + dynp->d_un.d_ptr);
			break;

		case DT_SYMENT:
			assert(dynp->d_un.d_val == sizeof(Elf_Sym));
			break;

		case DT_STRTAB:
			obj->strtab = (const char *)
			    (obj->relocbase + dynp->d_un.d_ptr);
			break;

		case DT_STRSZ:
			obj->strsize = dynp->d_un.d_val;
			break;

		case DT_VERNEED:
			obj->verneed = (const Elf_Verneed *)
			    (obj->relocbase + dynp->d_un.d_ptr);
			break;

		case DT_VERNEEDNUM:
			obj->verneednum = dynp->d_un.d_val;
			break;

		case DT_VERDEF:
			obj->verdef = (const Elf_Verdef *)
			    (obj->relocbase + dynp->d_un.d_ptr);
			break;

		case DT_VERDEFNUM:
			obj->verdefnum = dynp->d_un.d_val;
			break;

		case DT_VERSYM:
			obj->versyms = (const Elf_Versym *)
			    (obj->relocbase + dynp->d_un.d_ptr);
			break;

		case DT_HASH:
			{
				uint32_t nbuckets, nchains;
				const Elf_Symindx *hashtab = (const Elf_Symindx *)
				    (obj->relocbase + dynp->d_un.d_ptr);

				if (hashtab[0] > UINT32_MAX)
					nbuckets = UINT32_MAX;
				else
					nbuckets = hashtab[0];
				obj->nbuckets = nbuckets;
				obj->nchains = (nchains = hashtab[1]);
				obj->buckets = hashtab + 2;
				obj->chains = obj->buckets + obj->nbuckets;

				/* Validity check */
				if (!obj->buckets || !nbuckets || !nchains)
					continue;

				obj->sysv_hash = true;

				/*
				 * Should really be in _rtld_relocate_objects,
				 * but _rtld_symlook_obj might be used before.
				 */
				fast_divide32_prepare(obj->nbuckets,
				    &obj->nbuckets_m,
				    &obj->nbuckets_s1,
				    &obj->nbuckets_s2);
			}
			break;

		case DT_GNU_HASH:
			{
				uint32_t nmaskwords;
				uint32_t nbuckets, symndx;
				int bloom_size32;
				bool nmw_power2;
				const Elf_Symindx *hashtab = (const Elf_Symindx *)
				    (obj->relocbase + dynp->d_un.d_ptr);

				if (hashtab[0] > UINT32_MAX)
					nbuckets = UINT32_MAX;
				else
					nbuckets = hashtab[0];
				obj->nbuckets_gnu = nbuckets;

				nmaskwords = hashtab[2];
				bloom_size32 = nmaskwords * (ELFSIZE / 32);

				obj->buckets_gnu = (const uint32_t *)(hashtab + 4 + bloom_size32);

				nmw_power2 = powerof2(nmaskwords);

				/* Validity check */
				if (!nmw_power2 || !nbuckets || !obj->buckets_gnu)
					continue;

				obj->gnu_hash = true;

				obj->mask_bm_gnu = nmaskwords - 1;
				obj->symndx_gnu = (symndx = hashtab[1]);
				obj->shift2_gnu = hashtab[3];
				obj->bloom_gnu = (const Elf_Addr *)(hashtab + 4);
				obj->chains_gnu = obj->buckets_gnu + nbuckets - symndx;

				/*
				 * Should really be in _rtld_relocate_objects,
				 * but _rtld_symlook_obj might be used before.
				 */
				fast_divide32_prepare(nbuckets,
				    &obj->nbuckets_m_gnu,
				    &obj->nbuckets_s1_gnu,
				    &obj->nbuckets_s2_gnu);

				dbg(("found GNU Hash: buckets=%p "
				     "nbuckets=%u chains=%p nchains=%lu "
				     "bloom=%p mask_bm=%u shift2=%u "
				     "symndx=%u",
				    obj->buckets_gnu, obj->nbuckets_gnu,
				    obj->chains_gnu, obj->nchains_gnu,
				    obj->bloom_gnu, obj->mask_bm_gnu,
				    obj->shift2_gnu, obj->symndx_gnu));
			}
			break;

		case DT_NEEDED:
			{
				Needed_Entry *nep = NEW(Needed_Entry);

				nep->name = dynp->d_un.d_val;
				nep->obj = NULL;
				nep->next = NULL;

				*needed_tail = nep;
				needed_tail = &nep->next;
			}
			break;

		case DT_PLTGOT:
			obj->pltgot = (Elf_Addr *)
			    (obj->relocbase + dynp->d_un.d_ptr);
			break;

		case DT_TEXTREL:
			obj->textrel = true;
			break;

		case DT_SYMBOLIC:
			obj->symbolic = true;
			break;

		case DT_RPATH:
		case DT_RUNPATH:
			/*
		         * We have to wait until later to process this, because
			 * we might not have gotten the address of the string
			 * table yet.
		         */
			dyn_rpath = dynp;
			break;

		case DT_SONAME:
			dyn_soname = dynp;
			break;

		case DT_INIT:
#ifdef RTLD_LOADER
			init = dynp->d_un.d_ptr;
#endif
			break;

#ifdef HAVE_INITFINI_ARRAY
		case DT_INIT_ARRAY:
			obj->init_array =
			    (fptr_t *)(obj->relocbase + dynp->d_un.d_ptr);
			dbg(("headers: DT_INIT_ARRAY at %p",
			    obj->init_array));
			break;

		case DT_INIT_ARRAYSZ:
			obj->init_arraysz = dynp->d_un.d_val / sizeof(fptr_t);
			dbg(("headers: DT_INIT_ARRAYZ %zu",
			    obj->init_arraysz));
			break;
#endif

		case DT_FINI:
#ifdef RTLD_LOADER
			fini = dynp->d_un.d_ptr;
#endif
			break;

#ifdef HAVE_INITFINI_ARRAY
		case DT_FINI_ARRAY:
			obj->fini_array =
			    (fptr_t *)(obj->relocbase + dynp->d_un.d_ptr);
			dbg(("headers: DT_FINI_ARRAY at %p",
			    obj->fini_array));
			break;

		case DT_FINI_ARRAYSZ:
			obj->fini_arraysz = dynp->d_un.d_val / sizeof(fptr_t);
			dbg(("headers: DT_FINI_ARRAYZ %zu",
			    obj->fini_arraysz));
			break;
#endif

		/*
		 * Don't process DT_DEBUG on MIPS as the dynamic
		 * section is mapped read-only.  DT_MIPS_RLD_MAP or
		 * DT_MIPS_RLD_MAP_REL is used instead.
		 *
		 * XXX: n32/n64 may use DT_DEBUG, not sure yet.
		 */
#ifndef __mips__
		case DT_DEBUG:
#ifdef RTLD_LOADER
			dynp->d_un.d_ptr = (Elf_Addr)&_rtld_debug;
#endif
			break;
#endif

#ifdef __alpha__
		case DT_ALPHA_PLTRO:
			obj->secureplt = (dynp->d_un.d_val != 0);
			break;
#endif
#ifdef __mips__
		case DT_MIPS_LOCAL_GOTNO:
			obj->local_gotno = dynp->d_un.d_val;
			break;

		case DT_MIPS_SYMTABNO:
			obj->symtabno = dynp->d_un.d_val;
			break;

		case DT_MIPS_GOTSYM:
			obj->gotsym = dynp->d_un.d_val;
			break;

		/*
		 * The .dynamic section is read-only, so the loader
		 * can't write to it; instead, the linker reserves
		 * space in a read/write .rld_map section for the
		 * loader write to, and leaves a pointer to that space
		 * in a DT_MIPS_RLD_MAP entry.
		 *
		 * Except pointers like that don't work for
		 * position-independent executables, which use
		 * DT_MIPS_RLD_MAP_REL instead.
		 */
		case DT_MIPS_RLD_MAP:
#ifdef RTLD_LOADER
			*((Elf_Addr *)dynp->d_un.d_ptr) =
			    (Elf_Addr)&_rtld_debug;
#endif
			break;

		/*
		 * The .dynamic section is read-only, so the loader
		 * can't write to it; instead, the linker reserves
		 * space in a read/write .rld_map section for the
		 * loader write to, which might be mapped anywhere in
		 * virtual address space for position-independent
		 * executables, so the linker leaves its offset
		 * relative to the .dynamic entry itself in the dynamic
		 * entry.
		 */
		case DT_MIPS_RLD_MAP_REL:
#ifdef RTLD_LOADER
			*(Elf_Addr *)((Elf_Addr)dynp + dynp->d_un.d_val) =
			    (Elf_Addr)&_rtld_debug;
#endif
			break;
#endif
#ifdef __powerpc__
#ifdef _LP64
		case DT_PPC64_GLINK:
			obj->glink = (Elf_Addr)(uintptr_t)obj->relocbase + dynp->d_un.d_ptr;
			break;
#else
		case DT_PPC_GOT:
			obj->gotptr = (Elf_Addr *)(obj->relocbase + dynp->d_un.d_ptr);
			break;
#endif
#endif
		case DT_FLAGS_1:
			obj->z_now =
			    ((dynp->d_un.d_val & DF_1_NOW) != 0);
			obj->z_nodelete =
			    ((dynp->d_un.d_val & DF_1_NODELETE) != 0);
			obj->z_initfirst =
			    ((dynp->d_un.d_val & DF_1_INITFIRST) != 0);
			obj->z_noopen =
			    ((dynp->d_un.d_val & DF_1_NOOPEN) != 0);
			break;
		}
	}

	obj->rellim = (const Elf_Rel *)((const uint8_t *)obj->rel + relsz);
	obj->relalim = (const Elf_Rela *)((const uint8_t *)obj->rela + relasz);
	obj->relrlim = (const Elf_Relr *)((const uint8_t *)obj->relr + relrsz);
	if (use_pltrel) {
		obj->pltrel = (const Elf_Rel *)(obj->relocbase + pltrel);
		obj->pltrellim = (const Elf_Rel *)(obj->relocbase + pltrel + pltrelsz);
		obj->pltrelalim = 0;
		/* On PPC and SPARC, at least, REL(A)SZ may include JMPREL.
		   Trim rel(a)lim to save time later. */
		if (obj->rellim && obj->pltrel &&
		    obj->rellim > obj->pltrel &&
		    obj->rellim <= obj->pltrellim)
			obj->rellim = obj->pltrel;
	} else if (use_pltrela) {
		obj->pltrela = (const Elf_Rela *)(obj->relocbase + pltrel);
		obj->pltrellim = 0;
		obj->pltrelalim = (const Elf_Rela *)(obj->relocbase + pltrel + pltrelsz);
		/* On PPC and SPARC, at least, REL(A)SZ may include JMPREL.
		   Trim rel(a)lim to save time later. */
		if (obj->relalim && obj->pltrela &&
		    obj->relalim > obj->pltrela &&
		    obj->relalim <= obj->pltrelalim)
			obj->relalim = obj->pltrela;
	}

	/* If the ELF Hash is present, "nchains" is the same in both hashes. */
	if (!obj->sysv_hash && obj->gnu_hash) {
		uint_fast32_t i, nbucket, symndx;

		/* Otherwise, count the entries from the GNU Hash chain. */
		nbucket = obj->nbuckets_gnu;
		symndx = obj->symndx_gnu;

		for (i = 0; i < nbucket; i++) {
			Elf_Word bkt = obj->buckets_gnu[i];
			if (bkt == 0)
				continue;
			const uint32_t *hashval = &obj->chains_gnu[bkt];
			do {
				symndx++;
			} while ((*hashval++ & 1U) == 0);
		}
		obj->nchains_gnu = (uint32_t)symndx;
	}

#ifdef RTLD_LOADER
#if defined(__HAVE_FUNCTION_DESCRIPTORS)
	if (init != 0)
		obj->init = (void (*)(void))
		    _rtld_function_descriptor_alloc(obj, NULL, init);
	if (fini != 0)
		obj->fini = (void (*)(void))
		    _rtld_function_descriptor_alloc(obj, NULL, fini);
#else
	if (init != 0)
		obj->init = (void (*)(void)) (obj->relocbase + init);
	if (fini != 0)
		obj->fini = (void (*)(void)) (obj->relocbase + fini);
#endif
#endif

	if (dyn_rpath != NULL) {
		_rtld_add_paths(execname, &obj->rpaths, obj->strtab +
		    dyn_rpath->d_un.d_val);
	}
	if (dyn_soname != NULL) {
		_rtld_object_add_name(obj, obj->strtab +
		    dyn_soname->d_un.d_val);
	}
}

#ifdef RTLD_LOADER
/*
 * Process a shared object's program header.  This is used only for the
 * main program, when the kernel has already loaded the main program
 * into memory before calling the dynamic linker.  It creates and
 * returns an Obj_Entry structure.
 */
Obj_Entry *
_rtld_digest_phdr(const Elf_Phdr *phdr, int phnum, caddr_t entry)
{
	Obj_Entry      *obj;
	const Elf_Phdr *phlimit = phdr + phnum;
	const Elf_Phdr *ph;
	bool            first_seg = true;
	Elf_Addr        vaddr;
	size_t          size;

	obj = _rtld_obj_new();

	for (ph = phdr; ph < phlimit; ++ph) {
		if (ph->p_type != PT_PHDR)
			continue;

		obj->relocbase = (caddr_t)((uintptr_t)phdr - (uintptr_t)ph->p_vaddr);
		obj->phdr = phdr; /* Equivalent to relocbase + p_vaddr. */
		obj->phsize = ph->p_memsz;
		dbg(("headers: phdr %p (%p) phsize %zu relocbase %p",
		    obj->phdr, phdr, obj->phsize, obj->relocbase));
		break;
	}

	for (ph = phdr; ph < phlimit; ++ph) {
		vaddr = (Elf_Addr)(uintptr_t)(obj->relocbase + ph->p_vaddr);
		switch (ph->p_type) {

		case PT_INTERP:
			obj->interp = (const char *)(uintptr_t)vaddr;
			dbg(("headers: %s %p phsize %" PRImemsz,
			    "PT_INTERP", (void *)(uintptr_t)vaddr,
			     ph->p_memsz));
			break;

		case PT_LOAD:
			size = round_up(vaddr + ph->p_memsz) - obj->vaddrbase;
			if (first_seg) {	/* First load segment */
				obj->vaddrbase = round_down(vaddr);
				obj->mapbase = (caddr_t)(uintptr_t)obj->vaddrbase;
				obj->textsize = size;
				obj->mapsize = size;
				first_seg = false;
			} else {		/* Last load segment */
				obj->mapsize = MAX(obj->mapsize, size);
			}
			dbg(("headers: %s %p phsize %" PRImemsz,
			    "PT_LOAD", (void *)(uintptr_t)vaddr,
			     ph->p_memsz));
			break;

		case PT_DYNAMIC:
			obj->dynamic = (Elf_Dyn *)(uintptr_t)vaddr;
			dbg(("headers: %s %p phsize %" PRImemsz,
			    "PT_DYNAMIC", (void *)(uintptr_t)vaddr,
			     ph->p_memsz));
			break;

#ifdef GNU_RELRO
		case PT_GNU_RELRO:
			/* rounding happens later. */
			obj->relro_page = obj->relocbase + ph->p_vaddr;
			obj->relro_size = ph->p_memsz;
			dbg(("headers: %s %p phsize %" PRImemsz,
			    "PT_GNU_RELRO", (void *)(uintptr_t)vaddr,
			     ph->p_memsz));
			break;
#endif

#if defined(__HAVE_TLS_VARIANT_I) || defined(__HAVE_TLS_VARIANT_II)
		case PT_TLS:
			obj->tlsindex = 1;
			obj->tlssize = ph->p_memsz;
			obj->tlsalign = ph->p_align;
			obj->tlsinitsize = ph->p_filesz;
			obj->tlsinit = (void *)(obj->relocbase +
			    (uintptr_t)ph->p_vaddr);
			dbg(("headers: %s %p phsize %" PRImemsz,
			    "PT_TLS", (void *)(uintptr_t)vaddr,
			     ph->p_memsz));
			break;
#endif
#ifdef __ARM_EABI__
		case PT_ARM_EXIDX:
			obj->exidx_start = (void *)(uintptr_t)vaddr;
			obj->exidx_sz = ph->p_memsz;
			dbg(("headers: %s %p phsize %" PRImemsz,
			    "PT_ARM_EXIDX", (void *)(uintptr_t)vaddr,
			     ph->p_memsz));
			break;
#endif
		}
	}

	obj->entry = entry;
	return obj;
}
#endif
