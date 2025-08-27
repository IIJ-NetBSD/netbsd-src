/* Native-dependent code for GNU/Linux on LoongArch processors.

   Copyright (C) 2024 Free Software Foundation, Inc.
   Contributed by Loongson Ltd.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef GDB_NAT_LOONGARCH_LINUX_HW_POINT_H
#define GDB_NAT_LOONGARCH_LINUX_HW_POINT_H

#include "gdbsupport/break-common.h" /* For enum target_hw_bp_type.  */

#include "nat/loongarch-hw-point.h"

struct loongarch_user_watch_state {
	uint64_t dbg_info;
	struct {
		uint64_t    addr;
		uint64_t    mask;
		uint32_t    ctrl;
		uint32_t    pad;
	} dbg_regs[8];
};


/* Macros to extract fields from the hardware debug information word.  */
#define LOONGARCH_DEBUG_NUM_SLOTS(x) ((x) & 0xffff)

/* Each bit of a variable of this type is used to indicate whether a
   hardware breakpoint or watchpoint setting has been changed since
   the last update.

   Bit N corresponds to the Nth hardware breakpoint or watchpoint
   setting which is managed in loongarch_debug_reg_state, where N is
   valid between 0 and the total number of the hardware breakpoint or
   watchpoint debug registers minus 1.

   When bit N is 1, the corresponding breakpoint or watchpoint setting
   has changed, and therefore the corresponding hardware debug
   register needs to be updated via the ptrace interface.

   In the per-thread arch-specific data area, we define two such
   variables for per-thread hardware breakpoint and watchpoint
   settings respectively.

   This type is part of the mechanism which helps reduce the number of
   ptrace calls to the kernel, i.e. avoid asking the kernel to write
   to the debug registers with unchanged values.  */

typedef ULONGEST dr_changed_t;

/* Set each of the lower M bits of X to 1; assert X is wide enough.  */

#define DR_MARK_ALL_CHANGED(x, m)					\
  do									\
    {									\
      gdb_assert (sizeof ((x)) * 8 >= (m));				\
      (x) = (((dr_changed_t)1 << (m)) - 1);				\
    } while (0)

#define DR_MARK_N_CHANGED(x, n)						\
  do									\
    {									\
      (x) |= ((dr_changed_t)1 << (n));					\
    } while (0)

#define DR_CLEAR_CHANGED(x)						\
  do									\
    {									\
      (x) = 0;								\
    } while (0)

#define DR_HAS_CHANGED(x) ((x) != 0)
#define DR_N_HAS_CHANGED(x, n) ((x) & ((dr_changed_t)1 << (n)))

/* Per-thread arch-specific data we want to keep.  */

struct arch_lwp_info
{
  /* When bit N is 1, it indicates the Nth hardware breakpoint or
     watchpoint register pair needs to be updated when the thread is
     resumed; see loongarch_linux_prepare_to_resume.  */
  dr_changed_t dr_changed_bp;
  dr_changed_t dr_changed_wp;
};

/* Call ptrace to set the thread TID's hardware breakpoint/watchpoint
   registers with data from *STATE.  */

void loongarch_linux_set_debug_regs (struct loongarch_debug_reg_state *state,
				     int tid, int watchpoint);

/* Get the hardware debug register capacity information from the
   process represented by TID.  */

void loongarch_linux_get_debug_reg_capacity (int tid);

/* Return the debug register state for process PID.  If no existing
   state is found for this process, return nullptr.  */

struct loongarch_debug_reg_state *loongarch_lookup_debug_reg_state (pid_t pid);

/* Return the debug register state for process PID.  If no existing
   state is found for this process, create new state.  */

struct loongarch_debug_reg_state *loongarch_get_debug_reg_state (pid_t pid);

/* Remove any existing per-process debug state for process PID.  */

void loongarch_remove_debug_reg_state (pid_t pid);


#endif /* GDB_NAT_LOONGARCH_LINUX_HW_POINT_H */
