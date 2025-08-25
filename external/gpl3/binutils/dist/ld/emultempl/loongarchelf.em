# This shell script emits a C file. -*- C -*-
#   Copyright (C) 2021-2025 Free Software Foundation, Inc.
#   Contributed by Loongson Ltd.
#
# This file is part of the GNU Binutils.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING3. If not,
# see <http://www.gnu.org/licenses/>.

fragment <<EOF

#include "ldmain.h"
#include "ldctor.h"
#include "elf/loongarch.h"
#include "elfxx-loongarch.h"

EOF

# Disable linker relaxation if set address of section or segment.
PARSE_AND_LIST_ARGS_CASES=${PARSE_AND_LIST_ARGS_CASES}'
    case OPTION_SECTION_START:
    case OPTION_TTEXT:
    case OPTION_TBSS:
    case OPTION_TDATA:
    case OPTION_TTEXT_SEGMENT:
    case OPTION_TRODATA_SEGMENT:
    case OPTION_TLDATA_SEGMENT:
      link_info.disable_target_specific_optimizations = 2;
      return false;
'

fragment <<EOF
static void
larch_elf_before_allocation (void)
{
  gld${EMULATION_NAME}_before_allocation ();

  if (link_info.discard == discard_sec_merge)
    link_info.discard = discard_l;

  if (!bfd_link_relocatable (&link_info))
    {
      /* We always need at least some relaxation to handle code alignment.  */
      if (RELAXATION_DISABLED_BY_USER)
	TARGET_ENABLE_RELAXATION;
      else
	ENABLE_RELAXATION;
    }

  link_info.relax_pass = 3;
}

static void
gld${EMULATION_NAME}_after_allocation (void)
{
  int need_layout = 0;

  /* Don't attempt to discard unused .eh_frame sections until the final link,
     as we can't reliably tell if they're used until after relaxation.  */
  if (!bfd_link_relocatable (&link_info))
    {
      need_layout = bfd_elf_discard_info (link_info.output_bfd, &link_info);
      if (need_layout < 0)
	{
	  einfo (_("%X%P: .eh_frame/.stab edit: %E\n"));
	  return;
	}
    }

  /* The program header size of executable file may increase.  */
  if (bfd_get_flavour (link_info.output_bfd) == bfd_target_elf_flavour
      && !bfd_link_relocatable (&link_info))
    {
      if (lang_phdr_list == NULL)
	elf_seg_map (link_info.output_bfd) = NULL;
      if (!_bfd_elf_map_sections_to_segments (link_info.output_bfd,
					      &link_info,
					      NULL))
	fatal (_("%P: map sections to segments failed: %E\n"));
    }

  /* Adjust program header size and .eh_frame_hdr size before
     lang_relax_sections. Without it, the vma of data segment may increase.  */
  lang_do_assignments (lang_allocating_phase_enum);
  lang_reset_memory_regions ();
  lang_size_sections (NULL, true);

  enum phase_enum *phase = &(expld.dataseg.phase);
  bfd_elf${ELFSIZE}_loongarch_set_data_segment_info (&link_info, (int *) phase);
  /* gld${EMULATION_NAME}_map_segments (need_layout); */
  ldelf_map_segments (need_layout);
}

EOF

LDEMUL_BEFORE_ALLOCATION=larch_elf_before_allocation
LDEMUL_AFTER_ALLOCATION=gld${EMULATION_NAME}_after_allocation
