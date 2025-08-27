/* DWARF 2 abbrev table cache

   Copyright (C) 2022-2024 Free Software Foundation, Inc.

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

#include "dwarf2/abbrev-table-cache.h"

void
abbrev_table_cache::add (abbrev_table_up table)
{
  /* We allow this as a convenience to the caller.  */
  if (table == nullptr)
    return;

  bool inserted = m_tables.emplace (std::move (table)).second;

  /* If this one already existed, then it should have been reused.  */
  gdb_assert (inserted);
}
