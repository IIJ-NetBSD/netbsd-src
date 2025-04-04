/* tc-vax.h -- Header file for tc-vax.c.
   Copyright (C) 1987-2024 Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#define TC_VAX 1

#define TARGET_BYTES_BIG_ENDIAN 0

#ifdef OBJ_AOUT
#ifdef TE_NetBSD
#define TARGET_FORMAT "a.out-vax-netbsd"
#endif
#ifndef TARGET_FORMAT
#define TARGET_FORMAT "a.out-vax-bsd"
#endif
#endif

#ifdef OBJ_VMS
#define TARGET_FORMAT "vms-vax"
#endif

#ifdef OBJ_ELF
#define TARGET_FORMAT "elf32-vax"
#define DIFF_EXPR_OK 1
#define CFI_DIFF_EXPR_OK 0
#endif

#define TARGET_ARCH	bfd_arch_vax

#define NO_RELOC	BFD_RELOC_NONE
#define NOP_OPCODE	0x01

#define md_operand(x)

#ifdef OBJ_ELF
#define TC_PARSE_CONS_EXPRESSION(EXP, NBYTES) vax_cons (EXP, NBYTES)
#define TC_CONS_FIX_NEW vax_cons_fix_new
bfd_reloc_code_real_type vax_cons (expressionS *, int);
void vax_cons_fix_new (struct frag *, int, unsigned int, struct expressionS *,
		       bfd_reloc_code_real_type);
#endif

extern const struct relax_type md_relax_table[];
#define TC_GENERIC_RELAX_TABLE md_relax_table

/* Values passed to md_apply_fix don't include symbol values.  */
#define MD_APPLY_SYM_VALUE(FIX) 0

#define tc_fix_adjustable(FIX)					\
	((FIX)->fx_r_type != BFD_RELOC_VTABLE_INHERIT		\
	 && (FIX)->fx_r_type != BFD_RELOC_32_PLT_PCREL		\
	 && (FIX)->fx_r_type != BFD_RELOC_32_GOT_PCREL		\
	 && (FIX)->fx_r_type != BFD_RELOC_VTABLE_ENTRY		\
	 && ((FIX)->fx_pcrel					\
	     || ((FIX)->fx_subsy != NULL			\
		 && (S_GET_SEGMENT ((FIX)->fx_subsy)		\
		     == S_GET_SEGMENT ((FIX)->fx_addsy)))	\
	     || S_IS_LOCAL ((FIX)->fx_addsy)))

#define TARGET_USE_CFIPOP 1

#define tc_cfi_frame_initial_instructions vax_cfi_frame_initial_instructions
extern void vax_cfi_frame_initial_instructions (void);

#define tc_regname_to_dw2regnum tc_vax_regname_to_dw2regnum
extern int tc_vax_regname_to_dw2regnum (char *);

#define tc_cfi_emit_pcrel_expr vax_cfi_emit_pcrel_expr
extern void vax_cfi_emit_pcrel_expr (expressionS *, unsigned int);

#define DWARF2_LINE_MIN_INSN_LENGTH     1
#define DWARF2_DEFAULT_RETURN_COLUMN    15
#define DWARF2_CIE_DATA_ALIGNMENT       -4
