/* Register support routines for the remote server for GDB.
   Copyright (C) 2001-2024 Free Software Foundation, Inc.

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

#include "regdef.h"
#include "gdbthread.h"
#include "tdesc.h"
#include "gdbsupport/rsp-low.h"
#include "gdbsupport/gdb-checked-static-cast.h"

#ifndef IN_PROCESS_AGENT

struct regcache *
get_thread_regcache (thread_info *thread, bool fetch)
{
  regcache *regcache = thread->regcache ();

  /* Threads' regcaches are created lazily, because biarch targets add
     the main thread/lwp before seeing it stop for the first time, and
     it is only after the target sees the thread stop for the first
     time that the target has a chance of determining the process's
     architecture.  IOW, when we first add the process's main thread
     we don't know which architecture/tdesc its regcache should
     have.  */
  if (regcache == NULL)
    {
      process_info *proc = thread->process ();

      gdb_assert (proc->tdesc != NULL);

      regcache = new_register_cache (proc->tdesc);
      thread->set_regcache (regcache);
    }

  if (fetch && !regcache->registers_fetched)
    {
      scoped_restore_current_thread restore_thread;

      switch_to_thread (thread);
      /* Invalidate all registers, to prevent stale left-overs.  */
      memset (regcache->register_status, REG_UNAVAILABLE,
	      regcache->tdesc->reg_defs.size ());
      fetch_inferior_registers (regcache, -1);
      regcache->registers_fetched = true;
    }

  return regcache;
}

/* See gdbsupport/common-regcache.h.  */

reg_buffer_common *
get_thread_regcache_for_ptid (ptid_t ptid)
{
  return get_thread_regcache (find_thread_ptid (ptid));
}

void
regcache_invalidate_thread (thread_info *thread)
{
  regcache *regcache = thread->regcache ();

  if (regcache == NULL)
    return;

  if (regcache->registers_fetched)
    {
      scoped_restore_current_thread restore_thread;

      switch_to_thread (thread);
      store_inferior_registers (regcache, -1);
    }

  regcache->registers_fetched = false;
}

/* See regcache.h.  */

void
regcache_invalidate_pid (int pid)
{
  process_info *process = find_process_pid (pid);

  if (process != nullptr)
    process->for_each_thread (regcache_invalidate_thread);
}

/* See regcache.h.  */

void
regcache_invalidate (void)
{
  /* Only update the threads of the current process.  */
  int pid = current_thread->id.pid ();

  regcache_invalidate_pid (pid);
}

#endif

struct regcache *
init_register_cache (struct regcache *regcache,
		     const struct target_desc *tdesc,
		     unsigned char *regbuf)
{
  if (regbuf == NULL)
    {
#ifndef IN_PROCESS_AGENT
      /* Make sure to zero-initialize the register cache when it is
	 created, in case there are registers the target never
	 fetches.  This way they'll read as zero instead of
	 garbage.  */
      regcache->tdesc = tdesc;
      regcache->registers
	= (unsigned char *) xcalloc (1, tdesc->registers_size);
      regcache->registers_owned = true;
      regcache->register_status
	= (unsigned char *) xmalloc (tdesc->reg_defs.size ());
      memset ((void *) regcache->register_status, REG_UNAVAILABLE,
	      tdesc->reg_defs.size ());
#else
      gdb_assert_not_reached ("can't allocate memory from the heap");
#endif
    }
  else
    {
      regcache->tdesc = tdesc;
      regcache->registers = regbuf;
      regcache->registers_owned = false;
#ifndef IN_PROCESS_AGENT
      regcache->register_status = NULL;
#endif
    }

  regcache->registers_fetched = false;

  return regcache;
}

#ifndef IN_PROCESS_AGENT

struct regcache *
new_register_cache (const struct target_desc *tdesc)
{
  struct regcache *regcache = new struct regcache;

  gdb_assert (tdesc->registers_size != 0);

  return init_register_cache (regcache, tdesc, NULL);
}

void
free_register_cache (struct regcache *regcache)
{
  if (regcache)
    {
      if (regcache->registers_owned)
	free (regcache->registers);
      free (regcache->register_status);
      delete regcache;
    }
}

#endif

void
regcache::copy_from (regcache *src)
{
  gdb_assert (src != nullptr);
  gdb_assert (src->tdesc == this->tdesc);
  gdb_assert (src != this);

  memcpy (this->registers, src->registers, src->tdesc->registers_size);
#ifndef IN_PROCESS_AGENT
  if (this->register_status != nullptr && src->register_status != nullptr)
    memcpy (this->register_status, src->register_status,
	    src->tdesc->reg_defs.size ());
#endif
  this->registers_fetched = src->registers_fetched;
}

/* Return a reference to the description of register N.  */

static const struct gdb::reg &
find_register_by_number (const struct target_desc *tdesc, int n)
{
  gdb_assert (n >= 0);
  gdb_assert (n < tdesc->reg_defs.size ());

  return tdesc->reg_defs[n];
}

#ifndef IN_PROCESS_AGENT

void
registers_to_string (struct regcache *regcache, char *buf)
{
  unsigned char *registers = regcache->registers;
  const struct target_desc *tdesc = regcache->tdesc;

  for (int i = 0; i < tdesc->reg_defs.size (); ++i)
    {
      if (regcache->register_status[i] == REG_VALID)
	{
	  bin2hex (registers, buf, register_size (tdesc, i));
	  buf += register_size (tdesc, i) * 2;
	}
      else
	{
	  memset (buf, 'x', register_size (tdesc, i) * 2);
	  buf += register_size (tdesc, i) * 2;
	}
      registers += register_size (tdesc, i);
    }
  *buf = '\0';
}

void
registers_from_string (struct regcache *regcache, char *buf)
{
  int len = strlen (buf);
  unsigned char *registers = regcache->registers;
  const struct target_desc *tdesc = regcache->tdesc;

  if (len != tdesc->registers_size * 2)
    {
      warning ("Wrong sized register packet (expected %d bytes, got %d)",
	       2 * tdesc->registers_size, len);
      if (len > tdesc->registers_size * 2)
	len = tdesc->registers_size * 2;
    }
  hex2bin (buf, registers, len / 2);
}

/* See regcache.h */

std::optional<int>
find_regno_no_throw (const struct target_desc *tdesc, const char *name)
{
  for (int i = 0; i < tdesc->reg_defs.size (); ++i)
    {
      if (strcmp (name, find_register_by_number (tdesc, i).name) == 0)
	return i;
    }
  return {};
}

int
find_regno (const struct target_desc *tdesc, const char *name)
{
  std::optional<int> regnum = find_regno_no_throw (tdesc, name);

  if (regnum.has_value ())
    return *regnum;

  internal_error ("Unknown register %s requested", name);
}

static void
free_register_cache_thread (thread_info *thread)
{
  regcache *regcache = thread->regcache ();

  if (regcache != NULL)
    {
      regcache_invalidate_thread (thread);
      free_register_cache (regcache);
      thread->set_regcache (nullptr);
    }
}

void
regcache_release (void)
{
  /* Flush and release all pre-existing register caches.  */
  for_each_thread (free_register_cache_thread);
}
#endif

int
register_cache_size (const struct target_desc *tdesc)
{
  return tdesc->registers_size;
}

int
register_size (const struct target_desc *tdesc, int n)
{
  return find_register_by_number (tdesc, n).size / 8;
}

/* See gdbsupport/common-regcache.h.  */

int
regcache_register_size (const reg_buffer_common *regcache, int n)
{
  return register_size
    (gdb::checked_static_cast<const struct regcache *> (regcache)->tdesc, n);
}

static gdb::array_view<gdb_byte>
register_data (const struct regcache *regcache, int n)
{
  const gdb::reg &reg = find_register_by_number (regcache->tdesc, n);
  return gdb::make_array_view (regcache->registers + reg.offset / 8,
			       reg.size / 8);
}

void
supply_register (struct regcache *regcache, int n, const void *vbuf)
{
  const gdb::reg &reg = find_register_by_number (regcache->tdesc, n);
  const gdb_byte *buf = static_cast<const gdb_byte *> (vbuf);
  return regcache->raw_supply (n, gdb::make_array_view (buf, reg.size / 8));
}

/* See gdbsupport/common-regcache.h.  */

void
regcache::raw_supply (int n, gdb::array_view<const gdb_byte> src)
{
  auto dst = register_data (this, n);

  if (src.data () != nullptr)
    {
      copy (src, dst);
#ifndef IN_PROCESS_AGENT
      if (register_status != NULL)
	register_status[n] = REG_VALID;
#endif
    }
  else
    {
      memset (dst.data (), 0, dst.size ());
#ifndef IN_PROCESS_AGENT
      if (register_status != NULL)
	register_status[n] = REG_UNAVAILABLE;
#endif
    }
}

/* Supply register N with value zero to REGCACHE.  */

void
supply_register_zeroed (struct regcache *regcache, int n)
{
  auto dst = register_data (regcache, n);
  memset (dst.data (), 0, dst.size ());
#ifndef IN_PROCESS_AGENT
  if (regcache->register_status != NULL)
    regcache->register_status[n] = REG_VALID;
#endif
}

void
regcache::raw_supply_part_zeroed (int regnum, int offset, size_t size)
{
  auto dst = register_data (this, regnum).slice (offset, size);
  memset (dst.data (), 0, dst.size ());
#ifndef IN_PROCESS_AGENT
  if (register_status != NULL)
    register_status[regnum] = REG_VALID;
#endif
}

#ifndef IN_PROCESS_AGENT

/* Supply register called NAME with value zero to REGCACHE.  */

void
supply_register_by_name_zeroed (struct regcache *regcache,
				const char *name)
{
  supply_register_zeroed (regcache, find_regno (regcache->tdesc, name));
}

#endif

/* Supply the whole register set whose contents are stored in BUF, to
   REGCACHE.  If BUF is NULL, all the registers' values are recorded
   as unavailable.  */

void
supply_regblock (struct regcache *regcache, const void *buf)
{
  if (buf)
    {
      const struct target_desc *tdesc = regcache->tdesc;

      memcpy (regcache->registers, buf, tdesc->registers_size);
#ifndef IN_PROCESS_AGENT
      {
	int i;

	for (i = 0; i < tdesc->reg_defs.size (); i++)
	  regcache->register_status[i] = REG_VALID;
      }
#endif
    }
  else
    {
      const struct target_desc *tdesc = regcache->tdesc;

      memset (regcache->registers, 0, tdesc->registers_size);
#ifndef IN_PROCESS_AGENT
      {
	int i;

	for (i = 0; i < tdesc->reg_defs.size (); i++)
	  regcache->register_status[i] = REG_UNAVAILABLE;
      }
#endif
    }
}

#ifndef IN_PROCESS_AGENT

void
supply_register_by_name (struct regcache *regcache,
			 const char *name, const void *buf)
{
  supply_register (regcache, find_regno (regcache->tdesc, name), buf);
}

#endif

void
collect_register (struct regcache *regcache, int n, void *vbuf)
{
  const gdb::reg &reg = find_register_by_number (regcache->tdesc, n);
  gdb_byte *buf = static_cast<gdb_byte *> (vbuf);
  regcache->raw_collect (n, gdb::make_array_view (buf, reg.size / 8));
}

/* See gdbsupport/common-regcache.h.  */

void
regcache::raw_collect (int n, gdb::array_view<gdb_byte> dst) const
{
  auto src = register_data (this, n);
  copy (src, dst);
}

enum register_status
regcache_raw_read_unsigned (reg_buffer_common *reg_buf, int regnum,
			    ULONGEST *val)
{
  int size;
  regcache *regcache = gdb::checked_static_cast<struct regcache *> (reg_buf);

  gdb_assert (regcache != NULL);

  size = register_size (regcache->tdesc, regnum);

  if (size > (int) sizeof (ULONGEST))
    error (_("That operation is not available on integers of more than"
	    "%d bytes."),
	  (int) sizeof (ULONGEST));

  *val = 0;
  collect_register (regcache, regnum, val);

  return regcache->get_register_status (regnum);
}

#ifndef IN_PROCESS_AGENT

/* See regcache.h.  */

ULONGEST
regcache_raw_get_unsigned_by_name (struct regcache *regcache,
				   const char *name)
{
  return regcache_raw_get_unsigned (regcache,
				    find_regno (regcache->tdesc, name));
}

void
collect_register_as_string (struct regcache *regcache, int n, char *buf)
{
  bin2hex (register_data (regcache, n), buf);
}

void
collect_register_by_name (struct regcache *regcache,
			  const char *name, void *buf)
{
  collect_register (regcache, find_regno (regcache->tdesc, name), buf);
}

/* Special handling for register PC.  */

CORE_ADDR
regcache_read_pc (reg_buffer_common *regcache)
{
  return the_target->read_pc
    (gdb::checked_static_cast<struct regcache *> (regcache));
}

void
regcache_write_pc (struct regcache *regcache, CORE_ADDR pc)
{
  the_target->write_pc (regcache, pc);
}

#endif

/* See gdbsupport/common-regcache.h.  */

enum register_status
regcache::get_register_status (int regnum) const
{
#ifndef IN_PROCESS_AGENT
  gdb_assert (regnum >= 0 && regnum < tdesc->reg_defs.size ());
  if (register_status != nullptr)
    return (enum register_status) (register_status[regnum]);
  else
    return REG_VALID;
#else
  return REG_VALID;
#endif
}

/* See gdbsupport/common-regcache.h.  */

bool
regcache::raw_compare (int regnum, const void *buf, int offset) const
{
  gdb_assert (buf != NULL);

  gdb::array_view<const gdb_byte> regbuf = register_data (this, regnum);
  gdb_assert (offset <= regbuf.size ());
  regbuf = regbuf.slice (offset);

  return memcmp (buf, regbuf.data (), regbuf.size ()) == 0;
}
