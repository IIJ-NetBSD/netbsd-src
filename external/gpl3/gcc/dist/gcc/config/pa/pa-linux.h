/* Definitions for PA_RISC with ELF format
   Copyright (C) 1999-2022 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */


#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
	GNU_USER_TARGET_OS_CPP_BUILTINS();	\
	builtin_assert ("machine=bigendian");	\
    }						\
  while (0)

#undef CPP_SPEC
#define CPP_SPEC "%{posix:-D_POSIX_SOURCE} %{pthread:-D_REENTRANT}"

#undef ASM_SPEC
#define ASM_SPEC \
  ""

/* Define this for shared library support because it isn't in the main
   linux.h file.  */

#define GLIBC_DYNAMIC_LINKER "/lib/ld.so.1"

#undef LINK_SPEC
#define LINK_SPEC "\
  %{shared:-shared} \
  %{!shared: \
    %{!static: \
      %{rdynamic:-export-dynamic} \
      -dynamic-linker " GNU_USER_DYNAMIC_LINKER "} \
      %{static:-static}}"

/* glibc's profiling functions don't need gcc to allocate counters.  */
#define NO_DEFERRED_PROFILE_COUNTERS 1

/* Define the strings used for the special svr4 .type and .size directives.
   These strings generally do not vary from one system running svr4 to
   another, but if a given system (e.g. m88k running svr) needs to use
   different pseudo-op names for these, they may be overridden in the
   file which includes this one.  */

#undef STRING_ASM_OP
#define STRING_ASM_OP   "\t.stringz\t"

#define TEXT_SECTION_ASM_OP "\t.text"
#define DATA_SECTION_ASM_OP "\t.data"
#define BSS_SECTION_ASM_OP "\t.section\t.bss"

#define TARGET_ASM_FILE_START pa_linux_file_start

/* We want local labels to start with period if made with asm_fprintf.  */
#undef LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX "."

/* Define these to generate the Linux/ELF/SysV style of internal
   labels all the time - i.e. to be compatible with
   ASM_GENERATE_INTERNAL_LABEL in <elfos.h>.  Compare these with the
   ones in pa.h and note the lack of dollar signs in these.  FIXME:
   shouldn't we fix pa.h to use ASM_GENERATE_INTERNAL_LABEL instead? */

#undef ASM_OUTPUT_ADDR_VEC_ELT
#define ASM_OUTPUT_ADDR_VEC_ELT(FILE, VALUE) \
  fprintf (FILE, "\t.word .L%d\n", VALUE)

#undef ASM_OUTPUT_ADDR_DIFF_ELT
#define ASM_OUTPUT_ADDR_DIFF_ELT(FILE, BODY, VALUE, REL) \
  fprintf (FILE, "\t.word .L%d-.L%d\n", VALUE, REL)

/* Use the default.  */
#undef ASM_OUTPUT_LABEL

/* NOTE: (*targetm.asm_out.internal_label)() is defined for us by elfos.h, and
   does what we want (i.e. uses colons).  It must be compatible with
   ASM_GENERATE_INTERNAL_LABEL(), so do not define it here.  */

/* Use the default.  */
#undef ASM_OUTPUT_INTERNAL_LABEL

/* Use the default.  */
#undef TARGET_ASM_GLOBALIZE_LABEL
/* Globalizing directive for a label.  */
#define GLOBAL_ASM_OP ".globl "

/* FIXME: Hacked from the <elfos.h> one so that we avoid multiple
   labels in a function declaration (since pa.cc seems determined to do
   it differently).  */

#undef ASM_DECLARE_FUNCTION_NAME
#define ASM_DECLARE_FUNCTION_NAME(FILE, NAME, DECL)		\
  do								\
    {								\
      ASM_OUTPUT_TYPE_DIRECTIVE (FILE, NAME, "function");	\
      ASM_DECLARE_RESULT (FILE, DECL_RESULT (DECL));		\
      pa_output_function_label (FILE);				\
    }								\
  while (0)

/* Output function prologue for linux.  */
#undef TARGET_ASM_FUNCTION_PROLOGUE
#define TARGET_ASM_FUNCTION_PROLOGUE pa_linux_output_function_prologue

/* As well as globalizing the label, we need to encode the label
   to ensure a plabel is generated in an indirect call.  */

#undef ASM_OUTPUT_EXTERNAL_LIBCALL
#define ASM_OUTPUT_EXTERNAL_LIBCALL(FILE, FUN)  		\
  do								\
    {								\
      if (!FUNCTION_NAME_P (XSTR (FUN, 0)))			\
	pa_encode_label (FUN);					\
      (*targetm.asm_out.globalize_label) (FILE, XSTR (FUN, 0));	\
    }								\
  while (0)

#undef TARGET_GAS
#define TARGET_GAS 1

#undef TARGET_SYNC_LIBCALL
#define TARGET_SYNC_LIBCALL 1

/* The SYNC operations are implemented as library functions, not
   INSN patterns.  As a result, the HAVE defines for the patterns are
   not defined.  We need to define them to generate the corresponding
   __GCC_HAVE_SYNC_COMPARE_AND_SWAP_* and __GCC_ATOMIC_*_LOCK_FREE
   defines.  */

#define HAVE_sync_compare_and_swapqi 1
#define HAVE_sync_compare_and_swaphi 1
#define HAVE_sync_compare_and_swapsi 1
#define HAVE_sync_compare_and_swapdi 1

/* Enable GNU stack notes.  */

#undef NEED_INDICATE_EXEC_STACK
#define NEED_INDICATE_EXEC_STACK 1
