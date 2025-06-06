/*	$NetBSD: reloc.c,v 1.120 2025/05/02 23:03:16 riastradh Exp $	 */

/*
 * Copyright 1996 John D. Polstra.
 * Copyright 1996 Matt Thomas <matt@3am-software.com>
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
__RCSID("$NetBSD: reloc.c,v 1.120 2025/05/02 23:03:16 riastradh Exp $");
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
#include "hash.h"
#include "rtld.h"

#ifndef RTLD_INHIBIT_COPY_RELOCS
static int _rtld_do_copy_relocation(const Obj_Entry *, const Elf_Rela *);

static int
_rtld_do_copy_relocation(const Obj_Entry *dstobj, const Elf_Rela *rela)
{
	void           *dstaddr = (void *)(dstobj->relocbase + rela->r_offset);
	const Elf_Sym  *dstsym = dstobj->symtab + ELF_R_SYM(rela->r_info);
	const char     *name = dstobj->strtab + dstsym->st_name;
	Elf_Hash        hash;
	size_t          size = dstsym->st_size;
	const void     *srcaddr;
	const Elf_Sym  *srcsym = NULL;
	Obj_Entry      *srcobj;

	hash.sysv = _rtld_sysv_hash(name);
	hash.gnu = _rtld_gnu_hash(name);

	if (__predict_false(size == 0)) {
#if defined(__powerpc__) && !defined(__LP64) /* PR port-macppc/47464 */
		if (strcmp(name, "_SDA_BASE_") == 0
		    || strcmp(name, "_SDA2_BASE_") == 0)
		{
			rdbg(("COPY %s %s --> ignoring old binutils bug",
			      dstobj->path, name));
			return 0;
		}
#endif
#if 0 /* shall we warn? */
		xwarnx("%s: zero size COPY relocation for \"%s\"",
		       dstobj->path, name);
#endif
	}

	for (srcobj = dstobj->next; srcobj != NULL; srcobj = srcobj->next) {
		srcsym = _rtld_symlook_obj(name, &hash, srcobj, 0,
		    _rtld_fetch_ventry(dstobj, ELF_R_SYM(rela->r_info)));
		if (srcsym != NULL)
			break;
	}

	if (srcobj == NULL) {
		_rtld_error("Undefined symbol \"%s\" referenced from COPY"
		    " relocation in %s", name, dstobj->path);
		return (-1);
	}
	srcaddr = (const void *)(srcobj->relocbase + srcsym->st_value);
	rdbg(("COPY %s %s %s --> src=%p dst=%p size %ld",
	    dstobj->path, srcobj->path, name, srcaddr,
	    (void *)dstaddr, (long)size));
	(void)memcpy(dstaddr, srcaddr, size);
	return (0);
}
#endif /* RTLD_INHIBIT_COPY_RELOCS */


/*
 * Process the special R_xxx_COPY relocations in the main program.  These
 * copy data from a shared object into a region in the main program's BSS
 * segment.
 *
 * Returns 0 on success, -1 on failure.
 */
int
_rtld_do_copy_relocations(const Obj_Entry *dstobj)
{
#ifndef RTLD_INHIBIT_COPY_RELOCS

	/* COPY relocations are invalid elsewhere */
	assert(!dstobj->isdynamic);

	if (dstobj->rel != NULL) {
		const Elf_Rel  *rel;
		for (rel = dstobj->rel; rel < dstobj->rellim; ++rel) {
			if (ELF_R_TYPE(rel->r_info) == R_TYPE(COPY)) {
				Elf_Rela        ourrela;
				ourrela.r_info = rel->r_info;
				ourrela.r_offset = rel->r_offset;
				ourrela.r_addend = 0;
				if (_rtld_do_copy_relocation(dstobj,
				    &ourrela) < 0)
					return (-1);
			}
		}
	}
	if (dstobj->rela != NULL) {
		const Elf_Rela *rela;
		for (rela = dstobj->rela; rela < dstobj->relalim; ++rela) {
			if (ELF_R_TYPE(rela->r_info) == R_TYPE(COPY)) {
				if (_rtld_do_copy_relocation(dstobj, rela) < 0)
					return (-1);
			}
		}
	}
#ifdef GNU_RELRO
	/*
	 * If the main program is lazily bound (default -- whether or
	 * not LD_BINDNOW is set in the calling environment), we are
	 * now done writing to anything covered by RELRO and we can
	 * safely make it read-only.  There may still be ifunc
	 * resolution to do later; it will happen in a read/write
	 * segment and will not be made read-only.
	 *
	 * But if the main program is eagerly bound (i.e., the object
	 * has DF_1_NOW set in DT_FLAGS_1, whether or not LD_BIND_NOW
	 * is set in the calling environment), we delay protecting the
	 * RELRO region as read-only until we have resolved ifuncs --
	 * at which point we will make the ifunc resolution read-only
	 * too.
	 */
	if (!dstobj->z_now && _rtld_relro(dstobj, true) == -1)
		return -1;
#endif
#endif /* RTLD_INHIBIT_COPY_RELOCS */

	return (0);
}

/*
 * _rtld_relocate_relr(obj)
 *
 *	Relocate the RELR entries of obj.  The RELR table is encoded as
 *	a sequence of alternating addresses and bitmaps.  Each address
 *	entry has the low-order bit clear, and each bitmap has the
 *	low-order bit set:
 *
 *		AAAAAAA0
 *		BBBBBBB1
 *		BBBBBBB1
 *		BBBBBBB1
 *		AAAAAAA0
 *		BBBBBBB1
 *		...
 *
 *	Each address A is taken relative to obj->relocbase, and has
 *	obj->relocbase added to the Elf_Addr it points at.  For each
 *	bit i in the following bitmaps concatenated starting at 1,
 *	excluding the low-order bit used to distinguish bitmaps from
 *	addresses, the Elf_Addr at the address
 *
 *		A + sizeof(Elf_Addr)*i
 *
 *	(again, relative to obj->relocbase) has obj->relocbase added
 *	too.
 *
 *	DT_RELR relocations are processed before any DT_REL or DT_RELA
 *	relocations.
 *
 *	References:
 *
 *	Rahul Chaudhry, `Re: Proposal for a new section type SHT_RELR',
 *	generic-abi mailing list, 2018-02-07.
 *
 *	https://groups.google.com/g/generic-abi/c/bX460iggiKg/m/Jnz1lgLJAgAJ
 *	https://web.archive.org/web/20241213012330/https://groups.google.com/g/generic-abi/c/bX460iggiKg/m/Jnz1lgLJAgAJ
 */
static void
_rtld_relocate_relr(Obj_Entry *obj)
{
	const Elf_Relr *relr;

	if (obj->relr == obj->relrlim)
		return;

	for (relr = obj->relr; relr < obj->relrlim;) {
		Elf_Addr *where;

		/*
		 * At an address entry.  Relocate the address.
		 */
		assert((*relr & 1) == 0);
		where = (Elf_Addr *)(obj->relocbase + *relr);
		*where++ += (Elf_Addr)obj->relocbase;

		/*
		 * Process every bitmap entry after the address.
		 */
		while (++relr < obj->relrlim && *relr & 1) {
			unsigned i;

			/*
			 * Process every set bit in the bitmap.  Note
			 * that the first bit (i=0) is not processed
			 * here -- it's just metadata to mark a bitmap
			 * entry.
			 */
			for (i = 1; i < CHAR_BIT*sizeof(*relr); i++, where++) {
				if (*relr & ((Elf_Relr)1 << i))
					*where += (Elf_Addr)obj->relocbase;
			}
		}
	}
}

/*
 * Relocate newly-loaded shared objects.  The argument is a pointer to
 * the Obj_Entry for the first such object.  All objects from the first
 * to the end of the list of objects are relocated.  Returns 0 on success,
 * or -1 on failure.
 */
int
_rtld_relocate_objects(Obj_Entry *first, bool bind_now)
{
	Obj_Entry *obj;
	int ok = 1;

	for (obj = first; obj != NULL; obj = obj->next) {
		if ((!obj->sysv_hash && !obj->gnu_hash) ||
		    obj->symtab == NULL || obj->strtab == NULL) {
			_rtld_error("%s: Shared object has no run-time"
			    " symbol table", obj->path);
			return -1;
		}
		if (obj->nbuckets == UINT32_MAX) {
			_rtld_error("%s: Symbol table too large", obj->path);
			return -1;
		}
		rdbg((" relocating %s (%ld/%ld rel/rela, %ld/%ld plt rel/rela)",
		    obj->path,
		    (long)(obj->rellim - obj->rel),
		    (long)(obj->relalim - obj->rela),
		    (long)(obj->pltrellim - obj->pltrel),
		    (long)(obj->pltrelalim - obj->pltrela)));

		if (obj->textrel) {
			xwarnx("%s: text relocations", obj->path);
			/*
			 * There are relocations to the write-protected text
			 * segment.
			 */
			if (mprotect(obj->mapbase, obj->textsize,
				PROT_READ | PROT_WRITE) == -1) {
				_rtld_error("%s: Cannot write-enable text "
				    "segment: %s", obj->path, xstrerror(errno));
				return -1;
			}
		}
		dbg(("doing relative relocations"));
		_rtld_relocate_relr(obj);
		dbg(("doing non-PLT relocations"));
		if (_rtld_relocate_nonplt_objects(obj) < 0)
			ok = 0;
		if (obj->textrel) {	/* Re-protected the text segment. */
			if (mprotect(obj->mapbase, obj->textsize,
				     PROT_READ | PROT_EXEC) == -1) {
				_rtld_error("%s: Cannot write-protect text "
				    "segment: %s", obj->path, xstrerror(errno));
				return -1;
			}
		}
		dbg(("doing lazy PLT binding"));
		if (_rtld_relocate_plt_lazy(obj) < 0)
			ok = 0;
		if (obj->z_now || bind_now) {
			dbg(("doing immediate PLT binding"));
			if (_rtld_relocate_plt_objects(obj) < 0)
				ok = 0;
		}
		if (!ok)
			return -1;

		dbg(("fixing up PLTGOT"));
		/* Set the special PLTGOT entries. */
		if (obj->pltgot != NULL)
			_rtld_setup_pltgot(obj);
#ifdef GNU_RELRO
		if (_rtld_relro(obj, false) == -1)
			return -1;
#endif
	}
	return 0;
}

Elf_Addr
_rtld_resolve_ifunc(const Obj_Entry *obj, const Elf_Sym *def)
{
	Elf_Addr target;

	_rtld_shared_exit();
	target = _rtld_resolve_ifunc2(obj,
	    (Elf_Addr)obj->relocbase + def->st_value);
	_rtld_shared_enter();
	return target;
}

Elf_Addr
_rtld_resolve_ifunc2(const Obj_Entry *obj, Elf_Addr addr)
{
	Elf_Addr target;

	target = _rtld_call_function_addr(obj, addr);

	return target;
}

#if \
    !defined(RTLD_COMMON_CALL_IFUNC_RELA) && \
    !defined(RTLD_COMMON_CALL_IFUNC_REL) && \
    !defined(RTLD_ARCH_CALL_IFUNC)
void
_rtld_call_ifunc(Obj_Entry *obj, sigset_t *mask, u_int cur_objgen)
{
}
#endif

#ifdef RTLD_COMMON_CALL_IFUNC_RELA
#  ifdef __sparc__
#  include <machine/elf_support.h>
#  endif

void
_rtld_call_ifunc(Obj_Entry *obj, sigset_t *mask, u_int cur_objgen)
{
	const Elf_Rela *rela;
	Elf_Addr *where;
#ifdef __sparc__
	Elf_Word *where2;
#endif
	Elf_Addr target;

	while (obj->ifunc_remaining > 0 && _rtld_objgen == cur_objgen) {
		rela = obj->pltrelalim - obj->ifunc_remaining--;
#ifdef __sparc__
#define PLT_IRELATIVE R_TYPE(JMP_IREL)
#else
#define PLT_IRELATIVE R_TYPE(IRELATIVE)
#endif
		if (ELF_R_TYPE(rela->r_info) != PLT_IRELATIVE)
			continue;
#ifdef __sparc__
		where2 = (Elf_Word *)(obj->relocbase + rela->r_offset);
#else
		where = (Elf_Addr *)(obj->relocbase + rela->r_offset);
#endif
		target = (Elf_Addr)(obj->relocbase + rela->r_addend);
		_rtld_exclusive_exit(mask);
		target = _rtld_resolve_ifunc2(obj, target);
		_rtld_exclusive_enter(mask);
#ifdef __sparc__
		sparc_write_branch(where2 + 1, (void *)target);
#else
		if (*where != target)
			*where = target;
#endif
	}

	while (obj->ifunc_remaining_nonplt > 0 && _rtld_objgen == cur_objgen) {
		rela = obj->relalim - obj->ifunc_remaining_nonplt--;
		if (ELF_R_TYPE(rela->r_info) != R_TYPE(IRELATIVE))
			continue;
		where = (Elf_Addr *)(obj->relocbase + rela->r_offset);
		target = (Elf_Addr)(obj->relocbase + rela->r_addend);
		_rtld_exclusive_exit(mask);
		target = _rtld_resolve_ifunc2(obj, target);
		_rtld_exclusive_enter(mask);
		if (*where != target)
			*where = target;
	}
}
#endif

#ifdef RTLD_COMMON_CALL_IFUNC_REL
void
_rtld_call_ifunc(Obj_Entry *obj, sigset_t *mask, u_int cur_objgen)
{
	const Elf_Rel *rel;
	Elf_Addr *where, target;

	while (obj->ifunc_remaining > 0 && _rtld_objgen == cur_objgen) {
		rel = obj->pltrellim - obj->ifunc_remaining;
		--obj->ifunc_remaining;
		if (ELF_R_TYPE(rel->r_info) == R_TYPE(IRELATIVE)) {
			where = (Elf_Addr *)(obj->relocbase + rel->r_offset);
			_rtld_exclusive_exit(mask);
			target = _rtld_resolve_ifunc2(obj, *where);
			_rtld_exclusive_enter(mask);
			if (*where != target)
				*where = target;
		}
	}

	while (obj->ifunc_remaining_nonplt > 0 && _rtld_objgen == cur_objgen) {
		rel = obj->rellim - obj->ifunc_remaining_nonplt--;
		if (ELF_R_TYPE(rel->r_info) == R_TYPE(IRELATIVE)) {
			where = (Elf_Addr *)(obj->relocbase + rel->r_offset);
			_rtld_exclusive_exit(mask);
			target = _rtld_resolve_ifunc2(obj, *where);
			_rtld_exclusive_enter(mask);
			if (*where != target)
				*where = target;
		}
	}
}
#endif
