/* $NetBSD: dm_ioctl.c,v 1.57 2024/01/14 07:56:53 mlelstv Exp $      */

/*
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Adam Hamsik.
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
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: dm_ioctl.c,v 1.57 2024/01/14 07:56:53 mlelstv Exp $");

/*
 * Locking is used to synchronise between ioctl calls and between dm_table's
 * users.
 *
 * ioctl locking:
 * Simple reference counting, to count users of device will be used routines
 * dm_dev_busy/dm_dev_unbusy are used for that.
 * dm_dev_lookup/dm_dev_rem call dm_dev_busy before return(caller is therefore
 * holder of reference_counter last).
 *
 * ioctl routines which change/remove dm_dev parameters must wait on
 * dm_dev::dev_cv and when last user will call dm_dev_unbusy they will wake
 * up them.
 *
 * table_head locking:
 * To access table entries dm_table_* routines must be used.
 *
 * dm_table_get_entry will increment table users reference
 * counter. It will return active or inactive table depends
 * on uint8_t argument.
 *
 * dm_table_release must be called for every table_entry from
 * dm_table_get_entry. Between these two calls tables can't be switched
 * or destroyed.
 *
 * dm_table_head_init initialize table_entries SLISTS and io_cv.
 *
 * dm_table_head_destroy destroy cv.
 *
 * There are two types of users for dm_table_head first type will
 * only read list and try to do anything with it e.g. dmstrategy,
 * dm_table_size etc. There is another user for table_head which wants
 * to change table lists e.g. dm_dev_resume_ioctl, dm_dev_remove_ioctl,
 * dm_table_clear_ioctl.
 *
 * NOTE: It is not allowed to call dm_table_destroy, dm_table_switch_tables
 *       with hold table reference counter. Table reference counter is hold
 *       after calling dm_table_get_entry routine. After calling this
 *       function user must call dm_table_release before any writer table
 *       operation.
 *
 * Example: dm_table_get_entry
 *          dm_table_destroy/dm_table_switch_tables
 * This example will lead to deadlock situation because after dm_table_get_entry
 * table reference counter is != 0 and dm_table_destroy have to wait on cv until
 * reference counter is 0.
 *
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/device.h>
#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/kmem.h>
#include <sys/malloc.h>

#include <machine/int_fmtio.h>

#include "netbsd-dm.h"
#include "dm.h"
#include "ioconf.h"

extern struct cfattach dm_ca;
static uint32_t sc_minor_num;
uint32_t dm_dev_counter;

#define DM_REMOVE_FLAG(flag, name) do {					\
	prop_dictionary_get_uint32(dm_dict,DM_IOCTL_FLAGS,&flag);	\
	flag &= ~name;							\
	prop_dictionary_set_uint32(dm_dict,DM_IOCTL_FLAGS,flag);	\
} while (/*CONSTCOND*/0)

#define DM_ADD_FLAG(flag, name) do {					\
	prop_dictionary_get_uint32(dm_dict,DM_IOCTL_FLAGS,&flag);	\
	flag |= name;							\
	prop_dictionary_set_uint32(dm_dict,DM_IOCTL_FLAGS,flag);	\
} while (/*CONSTCOND*/0)

static int dm_table_deps(dm_table_entry_t *, prop_array_t);
static int dm_table_init(dm_target_t *, dm_table_entry_t *, char *);

/*
 * Print flags sent to the kernel from libdevmapper.
 */
static int
dm_dbg_print_flags(uint32_t flags)
{

	aprint_debug("dbg_print --- %d\n", flags);

	if (flags & DM_READONLY_FLAG)
		aprint_debug("dbg_flags: DM_READONLY_FLAG set In/Out\n");

	if (flags & DM_SUSPEND_FLAG)
		aprint_debug("dbg_flags: DM_SUSPEND_FLAG set In/Out\n");

	if (flags & DM_PERSISTENT_DEV_FLAG)
		aprint_debug("dbg_flags: DM_PERSISTENT_DEV_FLAG set In\n");

	if (flags & DM_STATUS_TABLE_FLAG)
		aprint_debug("dbg_flags: DM_STATUS_TABLE_FLAG set In\n");

	if (flags & DM_ACTIVE_PRESENT_FLAG)
		aprint_debug("dbg_flags: DM_ACTIVE_PRESENT_FLAG set Out\n");

	if (flags & DM_INACTIVE_PRESENT_FLAG)
		aprint_debug("dbg_flags: DM_INACTIVE_PRESENT_FLAG set Out\n");

	if (flags & DM_BUFFER_FULL_FLAG)
		aprint_debug("dbg_flags: DM_BUFFER_FULL_FLAG set Out\n");

	if (flags & DM_SKIP_BDGET_FLAG)
		aprint_debug("dbg_flags: DM_SKIP_BDGET_FLAG set In\n");

	if (flags & DM_SKIP_LOCKFS_FLAG)
		aprint_debug("dbg_flags: DM_SKIP_LOCKFS_FLAG set In\n");

	if (flags & DM_NOFLUSH_FLAG)
		aprint_debug("dbg_flags: DM_NOFLUSH_FLAG set In\n");

	return 0;
}

/*
 * Get list of all available targets from global
 * target list and sent them back to libdevmapper.
 */
int
dm_list_versions_ioctl(prop_dictionary_t dm_dict)
{
	prop_array_t target_list;
	uint32_t flags;

	flags = 0;

	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_FLAGS, &flags);

	dm_dbg_print_flags(flags);
	target_list = dm_target_prop_list();

	prop_dictionary_set(dm_dict, DM_IOCTL_CMD_DATA, target_list);
	prop_object_release(target_list);

	return 0;
}

/*
 * Create in-kernel entry for device. Device attributes such as name, uuid are
 * taken from proplib dictionary.
 */
int
dm_dev_create_ioctl(prop_dictionary_t dm_dict)
{
	dm_dev_t *dmv;
	const char *name, *uuid;
	int r;
	uint32_t flags;
	device_t devt;
	cfdata_t cf;

	flags = 0;
	name = NULL;
	uuid = NULL;

	/* Get needed values from dictionary. */
	prop_dictionary_get_string(dm_dict, DM_IOCTL_NAME, &name);
	prop_dictionary_get_string(dm_dict, DM_IOCTL_UUID, &uuid);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_FLAGS, &flags);

	dm_dbg_print_flags(flags);

	/* Lookup name and uuid if device already exist quit. */
	if ((dmv = dm_dev_lookup(name, uuid, -1)) != NULL) {
		DM_ADD_FLAG(flags, DM_EXISTS_FLAG);	/* Device already exists */
		dm_dev_unbusy(dmv);
		return EEXIST;
	}

	if ((dmv = dm_dev_alloc()) == NULL)
		return ENOMEM;

	cf = kmem_alloc(sizeof(*cf), KM_SLEEP);
	cf->cf_name = dm_cd.cd_name;
	cf->cf_atname = dm_ca.ca_name;
	cf->cf_unit = atomic_inc_32_nv(&sc_minor_num);
	cf->cf_fstate = FSTATE_NOTFOUND;
	if ((devt = config_attach_pseudo(cf)) == NULL) {
		dm_dev_free(dmv);
		kmem_free(cf, sizeof(*cf));
		aprint_error("Unable to attach pseudo device dm/%s\n", name);
		return (ENOMEM);
	}

	if (uuid)
		strncpy(dmv->uuid, uuid, DM_UUID_LEN);
	else
		dmv->uuid[0] = '\0';

	if (name)
		strlcpy(dmv->name, name, DM_NAME_LEN);

	dmv->minor = cf->cf_unit;
	dmv->flags = 0;		/* device flags are set when needed */
	dmv->ref_cnt = 0;
	dmv->event_nr = 0;
	dmv->devt = devt;

	dm_table_head_init(&dmv->table_head);

	mutex_init(&dmv->dev_mtx, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&dmv->diskp_mtx, MUTEX_DEFAULT, IPL_NONE);
	cv_init(&dmv->dev_cv, "dm_dev");

	if (flags & DM_READONLY_FLAG)
		dmv->flags |= DM_READONLY_FLAG;

	prop_dictionary_set_uint32(dm_dict, DM_IOCTL_MINOR, dmv->minor);

	disk_init(dmv->diskp, device_xname(devt), &dmdkdriver);
	disk_attach(dmv->diskp);

	dmv->diskp->dk_info = NULL;

	if ((r = dm_dev_insert(dmv)) != 0)
		dm_dev_free(dmv);

	DM_ADD_FLAG(flags, DM_EXISTS_FLAG);
	DM_REMOVE_FLAG(flags, DM_INACTIVE_PRESENT_FLAG);

	/* Increment device counter After creating device */
	atomic_inc_32(&dm_dev_counter);

	return r;
}

/*
 * Get list of created device-mapper devices from global list and
 * send it to kernel.
 *
 * Output dictionary:
 *
 * <key>cmd_data</key>
 *  <array>
 *   <dict>
 *    <key>name<key>
 *    <string>...</string>
 *
 *    <key>dev</key>
 *    <integer>...</integer>
 *   </dict>
 *  </array>
 */
int
dm_dev_list_ioctl(prop_dictionary_t dm_dict)
{
	prop_array_t dev_list;
	uint32_t flags;

	flags = 0;

	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_FLAGS, &flags);

	dm_dbg_print_flags(flags);

	dev_list = dm_dev_prop_list();

	prop_dictionary_set(dm_dict, DM_IOCTL_CMD_DATA, dev_list);
	prop_object_release(dev_list);

	return 0;
}

/*
 * Rename selected devices old name is in struct dm_ioctl.
 * newname is taken from dictionary
 *
 * <key>cmd_data</key>
 *  <array>
 *   <string>...</string>
 *  </array>
 */
int
dm_dev_rename_ioctl(prop_dictionary_t dm_dict)
{
	prop_array_t cmd_array;
	dm_dev_t *dmv;

	const char *name, *uuid, *n_name;
	uint32_t flags, minor;

	name = NULL;
	uuid = NULL;
	minor = 0;

	/* Get needed values from dictionary. */
	prop_dictionary_get_string(dm_dict, DM_IOCTL_NAME, &name);
	prop_dictionary_get_string(dm_dict, DM_IOCTL_UUID, &uuid);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_FLAGS, &flags);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_MINOR, &minor);

	dm_dbg_print_flags(flags);

	cmd_array = prop_dictionary_get(dm_dict, DM_IOCTL_CMD_DATA);

	prop_array_get_string(cmd_array, 0, &n_name);

	if (strlen(n_name) + 1 > DM_NAME_LEN)
		return EINVAL;

	if ((dmv = dm_dev_rem(name, uuid, minor)) == NULL) {
		DM_REMOVE_FLAG(flags, DM_EXISTS_FLAG);
		return ENOENT;
	}
	/* change device name */
	/*
	 * XXX How to deal with this change, name only used in
	 * dm_dev_routines, should I add dm_dev_change_name which will run
	 * under the dm_dev_list mutex ?
	 */
	strlcpy(dmv->name, n_name, DM_NAME_LEN);

	prop_dictionary_set_uint32(dm_dict, DM_IOCTL_OPEN, dmv->table_head.io_cnt);
	prop_dictionary_set_uint32(dm_dict, DM_IOCTL_MINOR, dmv->minor);
	prop_dictionary_set_string(dm_dict, DM_IOCTL_UUID, dmv->uuid);

	dm_dev_insert(dmv);

	return 0;
}

/*
 * Remove device from global list I have to remove active
 * and inactive tables first.
 */
int
dm_dev_remove_ioctl(prop_dictionary_t dm_dict)
{
	int error;
	cfdata_t cf;
	dm_dev_t *dmv;
	const char *name, *uuid;
	uint32_t flags, minor;
	device_t devt;

	flags = 0;
	name = NULL;
	uuid = NULL;

	/* Get needed values from dictionary. */
	prop_dictionary_get_string(dm_dict, DM_IOCTL_NAME, &name);
	prop_dictionary_get_string(dm_dict, DM_IOCTL_UUID, &uuid);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_FLAGS, &flags);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_MINOR, &minor);

	dm_dbg_print_flags(flags);

	/*
	 * This seems as hack to me, probably use routine dm_dev_get_devt to
	 * atomically get devt from device.
	 */
	if ((dmv = dm_dev_lookup(name, uuid, minor)) == NULL) {
		DM_REMOVE_FLAG(flags, DM_EXISTS_FLAG);
		return ENOENT;
	}
	devt = dmv->devt;

	dm_dev_unbusy(dmv);

	/*
	 * This will call dm_detach routine which will actually removes
	 * device.
	 */
	cf = device_cfdata(devt);
	error = config_detach(devt, DETACH_QUIET);
	if (error == 0)
		kmem_free(cf, sizeof(*cf));
	return error;
}

/*
 * Return actual state of device to libdevmapper.
 */
int
dm_dev_status_ioctl(prop_dictionary_t dm_dict)
{
	dm_dev_t *dmv;
	const char *name, *uuid;
	uint32_t flags, j, minor;

	name = NULL;
	uuid = NULL;
	flags = 0;

	prop_dictionary_get_string(dm_dict, DM_IOCTL_NAME, &name);
	prop_dictionary_get_string(dm_dict, DM_IOCTL_UUID, &uuid);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_FLAGS, &flags);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_MINOR, &minor);

	if ((dmv = dm_dev_lookup(name, uuid, minor)) == NULL) {
		DM_REMOVE_FLAG(flags, DM_EXISTS_FLAG);
		return ENOENT;
	}
	dm_dbg_print_flags(dmv->flags);

	prop_dictionary_set_uint32(dm_dict, DM_IOCTL_OPEN, dmv->table_head.io_cnt);
	prop_dictionary_set_uint32(dm_dict, DM_IOCTL_MINOR, dmv->minor);
	prop_dictionary_set_string(dm_dict, DM_IOCTL_UUID, dmv->uuid);

	if (dmv->flags & DM_SUSPEND_FLAG)
		DM_ADD_FLAG(flags, DM_SUSPEND_FLAG);

	/*
	 * Add status flags for tables I have to check both active and
	 * inactive tables.
	 */
	if ((j = dm_table_get_target_count(&dmv->table_head, DM_TABLE_ACTIVE)))
		DM_ADD_FLAG(flags, DM_ACTIVE_PRESENT_FLAG);
	else
		DM_REMOVE_FLAG(flags, DM_ACTIVE_PRESENT_FLAG);

	prop_dictionary_set_uint32(dm_dict, DM_IOCTL_TARGET_COUNT, j);

	if (dm_table_get_target_count(&dmv->table_head, DM_TABLE_INACTIVE))
		DM_ADD_FLAG(flags, DM_INACTIVE_PRESENT_FLAG);
	else
		DM_REMOVE_FLAG(flags, DM_INACTIVE_PRESENT_FLAG);

	dm_dev_unbusy(dmv);

	return 0;
}

/*
 * Set only flag to suggest that device is suspended. This call is
 * not supported in NetBSD.
 */
int
dm_dev_suspend_ioctl(prop_dictionary_t dm_dict)
{
	dm_dev_t *dmv;
	const char *name, *uuid;
	uint32_t flags, minor;

	name = NULL;
	uuid = NULL;
	flags = 0;

	prop_dictionary_get_string(dm_dict, DM_IOCTL_NAME, &name);
	prop_dictionary_get_string(dm_dict, DM_IOCTL_UUID, &uuid);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_FLAGS, &flags);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_MINOR, &minor);

	if ((dmv = dm_dev_lookup(name, uuid, minor)) == NULL) {
		DM_REMOVE_FLAG(flags, DM_EXISTS_FLAG);
		return ENOENT;
	}
	atomic_or_32(&dmv->flags, DM_SUSPEND_FLAG);

	dm_dbg_print_flags(dmv->flags);

	prop_dictionary_set_uint32(dm_dict, DM_IOCTL_OPEN, dmv->table_head.io_cnt);
	prop_dictionary_set_uint32(dm_dict, DM_IOCTL_FLAGS, dmv->flags);
	prop_dictionary_set_uint32(dm_dict, DM_IOCTL_MINOR, dmv->minor);

	dm_dev_unbusy(dmv);

	/* Add flags to dictionary flag after dmv -> dict copy */
	DM_ADD_FLAG(flags, DM_EXISTS_FLAG);

	return 0;
}

/*
 * Simulate Linux behaviour better and switch tables here and not in
 * dm_table_load_ioctl.
 */
int
dm_dev_resume_ioctl(prop_dictionary_t dm_dict)
{
	dm_dev_t *dmv;
	const char *name, *uuid;
	uint32_t flags, minor;

	name = NULL;
	uuid = NULL;
	flags = 0;

	/*
	 * char *xml; xml = prop_dictionary_externalize(dm_dict);
	 * printf("%s\n",xml);
	 */

	prop_dictionary_get_string(dm_dict, DM_IOCTL_NAME, &name);
	prop_dictionary_get_string(dm_dict, DM_IOCTL_UUID, &uuid);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_FLAGS, &flags);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_MINOR, &minor);

	/* Remove device from global device list */
	if ((dmv = dm_dev_lookup(name, uuid, minor)) == NULL) {
		DM_REMOVE_FLAG(flags, DM_EXISTS_FLAG);
		return ENOENT;
	}

	/* Make inactive table active, if it exists */
	if (dmv->flags & DM_INACTIVE_PRESENT_FLAG)
		dm_table_switch_tables(&dmv->table_head);

	atomic_and_32(&dmv->flags, ~(DM_SUSPEND_FLAG | DM_INACTIVE_PRESENT_FLAG));
	atomic_or_32(&dmv->flags, DM_ACTIVE_PRESENT_FLAG);

	DM_ADD_FLAG(flags, DM_EXISTS_FLAG);

	dmgetproperties(dmv->diskp, &dmv->table_head);

	prop_dictionary_set_uint32(dm_dict, DM_IOCTL_OPEN, dmv->table_head.io_cnt);
	prop_dictionary_set_uint32(dm_dict, DM_IOCTL_FLAGS, flags);
	prop_dictionary_set_uint32(dm_dict, DM_IOCTL_MINOR, dmv->minor);

	dm_dev_unbusy(dmv);

	/* Destroy inactive table after resume. */
	dm_table_destroy(&dmv->table_head, DM_TABLE_INACTIVE);

	return 0;
}

/*
 * Table management routines
 * lvm2tools doesn't send name/uuid to kernel with table
 * for lookup I have to use minor number.
 */

/*
 * Remove inactive table from device. Routines which work's with inactive tables
 * doesn't need to synchronise with dmstrategy. They can synchronise themselves with mutex?.
 */
int
dm_table_clear_ioctl(prop_dictionary_t dm_dict)
{
	dm_dev_t *dmv;
	const char *name, *uuid;
	uint32_t flags, minor;

	name = NULL;
	uuid = NULL;
	flags = 0;
	minor = 0;

	prop_dictionary_get_string(dm_dict, DM_IOCTL_NAME, &name);
	prop_dictionary_get_string(dm_dict, DM_IOCTL_UUID, &uuid);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_FLAGS, &flags);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_MINOR, &minor);

	aprint_debug("Clearing inactive table from device: %s--%s\n",
	    name, uuid);

	if ((dmv = dm_dev_lookup(name, uuid, minor)) == NULL) {
		DM_REMOVE_FLAG(flags, DM_EXISTS_FLAG);
		return ENOENT;
	}
	/* Select unused table */
	dm_table_destroy(&dmv->table_head, DM_TABLE_INACTIVE);

	atomic_and_32(&dmv->flags, ~DM_INACTIVE_PRESENT_FLAG);

	dm_dev_unbusy(dmv);

	return 0;
}

/*
 * Get list of physical devices for active table.
 * Get dev_t from pdev vnode and insert it into cmd_array.
 *
 * XXX. This function is called from lvm2tools to get information
 *      about physical devices, too e.g. during vgcreate.
 */
int
dm_table_deps_ioctl(prop_dictionary_t dm_dict)
{
	dm_dev_t *dmv;
	dm_table_t *tbl;
	dm_table_entry_t *table_en;

	prop_array_t cmd_array;
	const char *name, *uuid;
	uint32_t flags, minor;
	int table_type;

	name = NULL;
	uuid = NULL;
	flags = 0;

	prop_dictionary_get_string(dm_dict, DM_IOCTL_NAME, &name);
	prop_dictionary_get_string(dm_dict, DM_IOCTL_UUID, &uuid);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_FLAGS, &flags);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_MINOR, &minor);

	/* create array for dev_t's */
	cmd_array = prop_array_create();

	if ((dmv = dm_dev_lookup(name, uuid, minor)) == NULL) {
		DM_REMOVE_FLAG(flags, DM_EXISTS_FLAG);
		return ENOENT;
	}
	prop_dictionary_set_uint32(dm_dict, DM_IOCTL_MINOR, dmv->minor);
	prop_dictionary_set_string(dm_dict, DM_IOCTL_NAME, dmv->name);
	prop_dictionary_set_string(dm_dict, DM_IOCTL_UUID, dmv->uuid);

	aprint_debug("Getting table deps for device: %s\n", dmv->name);

	/*
	 * if DM_QUERY_INACTIVE_TABLE_FLAG is passed we need to query
	 * INACTIVE TABLE
	 */
	if (flags & DM_QUERY_INACTIVE_TABLE_FLAG)
		table_type = DM_TABLE_INACTIVE;
	else
		table_type = DM_TABLE_ACTIVE;

	tbl = dm_table_get_entry(&dmv->table_head, table_type);

	SLIST_FOREACH(table_en, tbl, next)
		dm_table_deps(table_en, cmd_array);

	dm_table_release(&dmv->table_head, table_type);
	dm_dev_unbusy(dmv);

	prop_dictionary_set(dm_dict, DM_IOCTL_CMD_DATA, cmd_array);
	prop_object_release(cmd_array);

	return 0;
}

static int
dm_table_deps(dm_table_entry_t *table_en, prop_array_t array)
{
	dm_mapping_t *map;
	int i, size;
	uint64_t rdev, tmp;

	size = prop_array_count(array);

	TAILQ_FOREACH(map, &table_en->pdev_maps, next) {
		rdev = map->data.pdev->pdev_vnode->v_rdev;
		for (i = 0; i < size; i++) {
			if (prop_array_get_uint64(array, i, &tmp) == true)
				if (rdev == tmp)
					break; /* exists */
		}
		/*
		 * Ignore if the device has already been added by
		 * other tables.
		 */
		if (i == size) {
			prop_array_add_uint64(array, rdev);
			aprint_debug("%s: %d:%d\n", __func__, major(rdev),
			    minor(rdev));
		}
	}

	return 0;
}

/*
 * Load new table/tables to device.
 * Call appropriate target init routine to open all physical pdev's and
 * link them to device. For other targets mirror, strip, snapshot
 * etc. also add dependency devices to upcalls list.
 *
 * Load table to inactive slot table are switched in dm_device_resume_ioctl.
 * This simulates Linux behaviour better there should not be any difference.
 */
int
dm_table_load_ioctl(prop_dictionary_t dm_dict)
{
	dm_dev_t *dmv;
	dm_table_entry_t *table_en, *last_table;
	dm_table_t *tbl;
	dm_target_t *target;

	prop_object_iterator_t iter;
	prop_array_t cmd_array;
	prop_dictionary_t target_dict;

	const char *name, *uuid, *type;
	uint32_t flags, minor;

	flags = 0;
	name = NULL;
	uuid = NULL;
	last_table = NULL;

	/*
	 * char *xml; xml = prop_dictionary_externalize(dm_dict);
	 * printf("%s\n",xml);
	 */

	prop_dictionary_get_string(dm_dict, DM_IOCTL_NAME, &name);
	prop_dictionary_get_string(dm_dict, DM_IOCTL_UUID, &uuid);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_FLAGS, &flags);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_MINOR, &minor);

	cmd_array = prop_dictionary_get(dm_dict, DM_IOCTL_CMD_DATA);
	iter = prop_array_iterator(cmd_array);
	dm_dbg_print_flags(flags);

	if ((dmv = dm_dev_lookup(name, uuid, minor)) == NULL) {
		DM_REMOVE_FLAG(flags, DM_EXISTS_FLAG);
		prop_object_iterator_release(iter);
		return ENOENT;
	}
	aprint_debug("Loading table to device: %s--%d\n", name,
	    dmv->table_head.cur_active_table);

	/*
	 * I have to check if this table slot is not used by another table list.
	 * if it is used I should free them.
	 */
	if (dmv->flags & DM_INACTIVE_PRESENT_FLAG)
		dm_table_destroy(&dmv->table_head, DM_TABLE_INACTIVE);

	dm_dbg_print_flags(dmv->flags);
	tbl = dm_table_get_entry(&dmv->table_head, DM_TABLE_INACTIVE);

	aprint_debug("dmv->name = %s\n", dmv->name);

	prop_dictionary_set_uint32(dm_dict, DM_IOCTL_MINOR, dmv->minor);

	while ((target_dict = prop_object_iterator_next(iter)) != NULL) {
		int ret;
		const char *cp;
		char *str;
		size_t strsz;

		prop_dictionary_get_string(target_dict,
		    DM_TABLE_TYPE, &type);
		/*
		 * If we want to deny table with 2 or more different
		 * target we should do it here
		 */
		if (((target = dm_target_lookup(type)) == NULL) &&
		    ((target = dm_target_autoload(type)) == NULL)) {
			dm_table_release(&dmv->table_head, DM_TABLE_INACTIVE);
			dm_dev_unbusy(dmv);
			prop_object_iterator_release(iter);
			return ENOENT;
		}
		table_en = kmem_alloc(sizeof(dm_table_entry_t), KM_SLEEP);
		prop_dictionary_get_uint64(target_dict, DM_TABLE_START,
		    &table_en->start);
		prop_dictionary_get_uint64(target_dict, DM_TABLE_LENGTH,
		    &table_en->length);

		table_en->target = target;
		table_en->dm_dev = dmv;
		table_en->target_config = NULL;
		TAILQ_INIT(&table_en->pdev_maps);

		/*
		 * There is a parameter string after dm_target_spec
		 * structure which  points to /dev/wd0a 284 part of
		 * table. String str points to this text. This can be
		 * null and therefore it should be checked before we try to
		 * use it.
		 */
		cp = NULL;
		prop_dictionary_get_string(target_dict,
		    DM_TABLE_PARAMS, &cp);
		if (cp == NULL)
			str = NULL;
		else
			str = kmem_strdupsize(cp, &strsz, KM_SLEEP);

		if (SLIST_EMPTY(tbl) || last_table == NULL)
			/* insert this table to head */
			SLIST_INSERT_HEAD(tbl, table_en, next);
		else
			SLIST_INSERT_AFTER(last_table, table_en, next);

		if ((ret = dm_table_init(target, table_en, str)) != 0) {
			dm_table_release(&dmv->table_head, DM_TABLE_INACTIVE);
			dm_table_destroy(&dmv->table_head, DM_TABLE_INACTIVE);

			if (str != NULL)
				kmem_free(str, strsz);

			dm_dev_unbusy(dmv);
			prop_object_iterator_release(iter);
			return ret;
		}
		last_table = table_en;
		if (str != NULL)
			kmem_free(str, strsz);
	}
	prop_object_iterator_release(iter);

	DM_ADD_FLAG(flags, DM_INACTIVE_PRESENT_FLAG);
	atomic_or_32(&dmv->flags, DM_INACTIVE_PRESENT_FLAG);

	dm_table_release(&dmv->table_head, DM_TABLE_INACTIVE);
	dm_dev_unbusy(dmv);
	return 0;
}

static int
dm_table_init(dm_target_t *target, dm_table_entry_t *table_en, char *params)
{
	int i, n, ret, argc;
	char **ap, **argv;

	if (params == NULL)
		return EINVAL;

	n = target->max_argc;
	if (n)
		aprint_debug("Max argc %d for %s target\n", n, target->name);
	else
		n = 32; /* large enough for most targets */

	argv = kmem_alloc(sizeof(*argv) * n, KM_SLEEP);

	for (ap = argv;
	     ap < &argv[n] && (*ap = strsep(&params, " \t")) != NULL;) {
		if (**ap != '\0')
			ap++;
	}
	argc = ap - argv;

	for (i = 0; i < argc; i++)
		aprint_debug("DM: argv[%d] = \"%s\"\n", i, argv[i]);

	KASSERT(target->init);
	ret = target->init(table_en, argc, argv);

	kmem_free(argv, sizeof(*argv) * n);

	return ret;
}

/*
 * Get description of all tables loaded to device from kernel
 * and send it to libdevmapper.
 *
 * Output dictionary for every table:
 *
 * <key>cmd_data</key>
 * <array>
 *   <dict>
 *    <key>type<key>
 *    <string>...</string>
 *
 *    <key>start</key>
 *    <integer>...</integer>
 *
 *    <key>length</key>
 *    <integer>...</integer>
 *
 *    <key>params</key>
 *    <string>...</string>
 *   </dict>
 * </array>
 */
int
dm_table_status_ioctl(prop_dictionary_t dm_dict)
{
	dm_dev_t *dmv;
	dm_table_t *tbl;
	dm_table_entry_t *table_en;

	prop_array_t cmd_array;
	prop_dictionary_t target_dict;

	uint32_t minor, flags;
	const char *name, *uuid;
	int table_type;

	uuid = NULL;
	name = NULL;
	flags = 0;

	prop_dictionary_get_string(dm_dict, DM_IOCTL_NAME, &name);
	prop_dictionary_get_string(dm_dict, DM_IOCTL_UUID, &uuid);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_FLAGS, &flags);
	prop_dictionary_get_uint32(dm_dict, DM_IOCTL_MINOR, &minor);

	cmd_array = prop_array_create();

	if ((dmv = dm_dev_lookup(name, uuid, minor)) == NULL) {
		DM_REMOVE_FLAG(flags, DM_EXISTS_FLAG);
		return ENOENT;
	}
	/*
	 * if DM_QUERY_INACTIVE_TABLE_FLAG is passed we need to query
	 * INACTIVE TABLE
	 */
	if (flags & DM_QUERY_INACTIVE_TABLE_FLAG)
		table_type = DM_TABLE_INACTIVE;
	else
		table_type = DM_TABLE_ACTIVE;

	if (dm_table_get_target_count(&dmv->table_head, DM_TABLE_ACTIVE)) {
		DM_ADD_FLAG(flags, DM_ACTIVE_PRESENT_FLAG);
	} else {
		DM_REMOVE_FLAG(flags, DM_ACTIVE_PRESENT_FLAG);

		if (dm_table_get_target_count(&dmv->table_head, DM_TABLE_INACTIVE))
			DM_ADD_FLAG(flags, DM_INACTIVE_PRESENT_FLAG);
		else
			DM_REMOVE_FLAG(flags, DM_INACTIVE_PRESENT_FLAG);
	}

	if (dmv->flags & DM_SUSPEND_FLAG)
		DM_ADD_FLAG(flags, DM_SUSPEND_FLAG);

	prop_dictionary_set_uint32(dm_dict, DM_IOCTL_MINOR, dmv->minor);

	aprint_debug("Status of device tables: %s--%d\n",
	    name, dmv->table_head.cur_active_table);

	tbl = dm_table_get_entry(&dmv->table_head, table_type);

	SLIST_FOREACH(table_en, tbl, next) {
		char *params;
		int is_table;

		target_dict = prop_dictionary_create();
		aprint_debug("%016" PRIu64 ", length %016" PRIu64
		    ", target %s\n", table_en->start, table_en->length,
		    table_en->target->name);

		prop_dictionary_set_uint64(target_dict, DM_TABLE_START,
		    table_en->start);
		prop_dictionary_set_uint64(target_dict, DM_TABLE_LENGTH,
		    table_en->length);

		prop_dictionary_set_string(target_dict, DM_TABLE_TYPE,
		    table_en->target->name);

		/* dm_table_get_cur_actv.table ?? */
		prop_dictionary_set_int32(target_dict, DM_TABLE_STAT,
		    dmv->table_head.cur_active_table);

		/*
		 * Explicitly clear DM_TABLE_PARAMS to prevent dmsetup(8) from
		 * printing junk when DM_TABLE_PARAMS was never initialized.
		 */
		prop_dictionary_set_string(target_dict, DM_TABLE_PARAMS, "");

		is_table = (flags & DM_STATUS_TABLE_FLAG) ? 1 : 0;
		if (is_table && table_en->target->table)
			params = table_en->target->table(
			    table_en->target_config);
		else if (!is_table && table_en->target->info)
			params = table_en->target->info(
			    table_en->target_config);
		else
			params = NULL;

		if (params != NULL) {
			prop_dictionary_set_string(target_dict,
			    DM_TABLE_PARAMS, params);
			kmem_free(params, DM_MAX_PARAMS_SIZE);
		}

		prop_array_add(cmd_array, target_dict);
		prop_object_release(target_dict);
	}

	dm_table_release(&dmv->table_head, table_type);
	dm_dev_unbusy(dmv);

	prop_dictionary_set_uint32(dm_dict, DM_IOCTL_FLAGS, flags);
	prop_dictionary_set(dm_dict, DM_IOCTL_CMD_DATA, cmd_array);
	prop_object_release(cmd_array);

	return 0;
}

/*
 * For every call I have to set kernel driver version.
 * Because I can have commands supported only in other
 * newer/later version. This routine is called for every
 * ioctl command.
 */
int
dm_check_version(prop_dictionary_t dm_dict)
{
	int i;
	uint32_t dm_version[3];
	prop_array_t ver;

	ver = prop_dictionary_get(dm_dict, DM_IOCTL_VERSION);

	for (i = 0; i < 3; i++)
		prop_array_get_uint32(ver, i, &dm_version[i]);

	if (DM_VERSION_MAJOR != dm_version[0] || DM_VERSION_MINOR < dm_version[1]) {
		aprint_debug("libdevmapper/kernel version mismatch "
		    "kernel: %d.%d.%d libdevmapper: %d.%d.%d\n",
		    DM_VERSION_MAJOR, DM_VERSION_MINOR, DM_VERSION_PATCHLEVEL,
		    dm_version[0], dm_version[1], dm_version[2]);

		return EIO;
	}
	prop_array_set_uint32(ver, 0, DM_VERSION_MAJOR);
	prop_array_set_uint32(ver, 1, DM_VERSION_MINOR);
	prop_array_set_uint32(ver, 2, DM_VERSION_PATCHLEVEL);

	prop_dictionary_set(dm_dict, DM_IOCTL_VERSION, ver);

	return 0;
}
