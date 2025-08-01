/*	$NetBSD: boot.c,v 1.33 2025/07/31 02:59:13 pgoyette Exp $	*/

/*-
 * Copyright (c) 2016 Kimihiro Nonaka <nonaka@netbsd.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "efiboot.h"

#include <sys/bootblock.h>
#include <sys/boot_flag.h>
#include <machine/limits.h>

#include "bootcfg.h"
#include "bootmod.h"
#include "bootmenu.h"
#include "biosdisk.h"
#include "devopen.h"

#ifdef _STANDALONE
#include <bootinfo.h>
#endif

int errno;
int boot_biosdev;
daddr_t boot_biossector;

extern const char bootprog_name[], bootprog_rev[], bootprog_kernrev[];

extern struct x86_boot_params boot_params;
extern char twiddle_toggle;

static const char * const names[][2] = {
	{ "netbsd", "netbsd.gz" },
	{ "onetbsd", "onetbsd.gz" },
	{ "netbsd.old", "netbsd.old.gz" },
	{ "netbsd/kernel", "netbsd/kernel.gz" },
	{ "onetbsd/kernel", "onetbsd/kernel.gz" },
	{ "netbsd.old/kernel", "netbsd.old/kernel.gz" },
};

#define NUMNAMES	__arraycount(names)
#define DEFFILENAME	names[0][0]

#ifndef	EFIBOOTCFG_FILENAME
#define	EFIBOOTCFG_FILENAME	"esp:/EFI/NetBSD/boot.cfg"
#endif

void	command_help(char *);
void	command_quit(char *);
void	command_boot(char *);
void	command_pkboot(char *);
void	command_consdev(char *);
void	command_root(char *);
void	command_dev(char *);
void	command_devpath(char *);
void	command_efivar(char *);
void	command_gop(char *);
#if LIBSA_ENABLE_LS_OP
void	command_ls(char *);
#endif
void	command_memmap(char *);
#ifndef SMALL
void	command_menu(char *);
#endif
void	command_modules(char *);
void	command_multiboot(char *);
void	command_reloc(char *);
void	command_text(char *);
void	command_version(char *);

const struct bootblk_command commands[] = {
	{ "help",	command_help },
	{ "?",		command_help },
	{ "quit",	command_quit },
	{ "boot",	command_boot },
	{ "pkboot",	command_pkboot },
	{ "consdev",	command_consdev },
	{ "root",	command_root },
	{ "dev",	command_dev },
	{ "devpath",	command_devpath },
	{ "efivar",	command_efivar },
	{ "fs",		fs_add },
	{ "gop",	command_gop },
	{ "load",	module_add },
#if LIBSA_ENABLE_LS_OP
	{ "ls",		command_ls },
#endif
	{ "memmap",	command_memmap },
#ifndef SMALL
	{ "menu",	command_menu },
#endif
	{ "modules",	command_modules },
	{ "multiboot",	command_multiboot },
	{ "reloc",	command_reloc },
	{ "rndseed",	rnd_add },
	{ "splash",	splash_add },
	{ "text",	command_text },
	{ "userconf",	userconf_add },
	{ "version",	command_version },
	{ NULL,		NULL },
};

static char *default_fsname;
static char *default_devname;
static int default_unit, default_partition;
static const char *default_filename;
static const char *default_part_name;

static char *sprint_bootsel(const char *);
static void bootit(const char *, int);
static void bootit2(char *, size_t, int);

int
parsebootfile(const char *fname, char **fsname, char **devname, int *unit,
    int *partition, const char **file)
{
	const char *col;
	static char savedevname[MAXDEVNAME+1];
#if defined(SUPPORT_NFS) || defined(SUPPORT_TFTP)
	const struct netboot_fstab *nf;
#endif

	*fsname = default_fsname;
	if (default_part_name == NULL) {
		*devname = default_devname;
	} else {
		snprintf(savedevname, sizeof(savedevname),
		    "NAME=%s", default_part_name);
		*devname = savedevname;
	}
	*unit = default_unit;
	*partition = default_partition;
	*file = default_filename;

	if (fname == NULL)
		return 0;

	if ((col = strchr(fname, ':')) != NULL) {	/* device given */
		int devlen;
		int u = 0, p = 0;
		int i = 0;

		devlen = col - fname;
		if (devlen > MAXDEVNAME)
			return EINVAL;

		if (strstr(fname, "NAME=") == fname) {
			strlcpy(savedevname, fname, devlen + 1);
			*fsname = "ufs";
			*devname = savedevname;
			*unit = -1;
			*partition = -1;
			fname = col + 1;
			goto out;
		}

#define isvalidname(c) ((c) >= 'a' && (c) <= 'z')
		if (!isvalidname(fname[i]))
			return EINVAL;
		do {
			savedevname[i] = fname[i];
			i++;
		} while (isvalidname(fname[i]));
		savedevname[i] = '\0';

#define isnum(c) ((c) >= '0' && (c) <= '9')
		if (i < devlen) {
			if (!isnum(fname[i]))
				return EUNIT;
			do {
				u *= 10;
				u += fname[i++] - '0';
			} while (isnum(fname[i]));
		}

#define isvalidpart(c) ((c) >= 'a' && (c) <= 'z')
		if (i < devlen) {
			if (!isvalidpart(fname[i]))
				return EPART;
			p = fname[i++] - 'a';
		}

		if (i != devlen)
			return ENXIO;

#if defined(SUPPORT_NFS) || defined(SUPPORT_TFTP)
		nf = netboot_fstab_find(savedevname);
		if (nf != NULL)
			*fsname = (char *)nf->name;
		else
#endif
		*fsname = "ufs";
		*devname = savedevname;
		*unit = u;
		*partition = p;
		fname = col + 1;
	}

out:
	if (*fname)
		*file = fname;

	return 0;
}

static char *
snprint_bootdev(char *buf, size_t bufsize, const char *devname, int unit,
    int partition)
{
	static const char *no_partition_devs[] = { "esp", "net", "nfs", "tftp" };
	int i;

	for (i = 0; i < __arraycount(no_partition_devs); i++)
		if (strcmp(devname, no_partition_devs[i]) == 0)
			break;
	if (strstr(devname, "NAME=") == devname)
		strlcpy(buf, devname, bufsize);
	else
		snprintf(buf, bufsize, "%s%d%c", devname, unit,
		  i < __arraycount(no_partition_devs) ? '\0' : 'a' + partition);
	return buf;
}

static char *
sprint_bootsel(const char *filename)
{
	char *fsname, *devname;
	int unit, partition;
	const char *file;
	static char buf[80];

	if (parsebootfile(filename, &fsname, &devname, &unit,
			  &partition, &file) == 0) {
		snprintf(buf, sizeof(buf), "%s:%s", snprint_bootdev(buf,
		    sizeof(buf), devname, unit, partition), file);
		return buf;
	}
	return "(invalid)";
}

void
clearit(void)
{

	if (bootcfg_info.clear)
		clear_pc_screen();
}

static void
bootit(const char *filename, int howto)
{

	if (howto & AB_VERBOSE)
		printf("booting %s (howto 0x%x)\n", sprint_bootsel(filename),
		    howto);

	if (exec_netbsd(filename, efi_loadaddr, howto, 0, efi_cleanup) < 0)
		printf("boot: %s: %s\n", sprint_bootsel(filename),
		       strerror(errno));
	else
		printf("boot returned\n");
}

void
boot(void)
{
	int currname;
	int c;
#if defined(SUPPORT_NFS) || defined(SUPPORT_TFTP)
	const struct netboot_fstab *nf;
#endif

	boot_modules_enabled = !(boot_params.bp_flags & X86_BP_FLAGS_NOMODULES);

	/* try to set default device to what BIOS tells us */
	bios2dev(boot_biosdev, boot_biossector, &default_devname, &default_unit,
	    &default_partition, &default_part_name);

	/* if the user types "boot" without filename */
	default_filename = DEFFILENAME;

#if defined(SUPPORT_NFS) || defined(SUPPORT_TFTP)
	nf = netboot_fstab_find(default_devname);
	if (nf != NULL)
		default_fsname = (char *)nf->name;
	else
#endif
	default_fsname = "ufs";

	if (!(boot_params.bp_flags & X86_BP_FLAGS_NOBOOTCONF)) {
#ifdef EFIBOOTCFG_FILENAME
		int rv = EINVAL;
		if (efi_bootdp_type != BOOT_DEVICE_TYPE_NET)
			rv = parsebootconf(EFIBOOTCFG_FILENAME);
		if (rv)
#endif
		parsebootconf(BOOTCFG_FILENAME);
	} else {
		bootcfg_info.timeout = boot_params.bp_timeout;
	}

	/*
	 * If console set in boot.cfg, switch to it.
	 * This will print the banner, so we don't need to explicitly do it
	 */
	if (bootcfg_info.consdev) {
		command_consdev(bootcfg_info.consdev);
	} else {
		clearit();
		print_bootcfg_banner(bootprog_name, bootprog_rev);
	}

	/* Display the menu, if applicable */
	twiddle_toggle = 0;
	if (bootcfg_info.nummenu > 0) {
		/* Does not return */
		doboottypemenu();
	}

	printf("Press return to boot now, any other key for boot menu\n");
	for (currname = 0; currname < NUMNAMES; currname++) {
		printf("booting %s - starting in ",
		       sprint_bootsel(names[currname][0]));

		c = awaitkey((bootcfg_info.timeout < 0) ? 0
		    : bootcfg_info.timeout, 1);
		if ((c != '\r') && (c != '\n') && (c != '\0')) {
		    if ((boot_params.bp_flags & X86_BP_FLAGS_PASSWORD) == 0) {
			/* do NOT ask for password */
			bootmenu(); /* does not return */
		    } else {
			/* DO ask for password */
			if (check_password((char *)boot_params.bp_password)) {
			    /* password ok */
			    printf("type \"?\" or \"help\" for help.\n");
			    bootmenu(); /* does not return */
			} else {
			    /* bad password */
			    printf("Wrong password.\n");
			    currname = 0;
			    continue;
			}
		    }
		}

		/*
		 * try pairs of names[] entries, foo and foo.gz
		 */
		/* don't print "booting..." again */
		bootit(names[currname][0], 0);
		/* since it failed, try compressed bootfile. */
		bootit(names[currname][1], AB_VERBOSE);
	}

	bootmenu();	/* does not return */
}

/* ARGSUSED */
void
command_help(char *arg)
{

	printf("commands are:\n"
	       "boot [dev:][filename] [-12acdqsvxz]\n"
#ifndef NO_RAIDFRAME
	       "     dev syntax is (hd|fd|cd|raid)[N[x]]\n"
#else
	       "     dev syntax is (hd|fd|cd)[N[x]]\n"
#endif
#ifndef NO_GPT
	       "                or NAME=gpt_label\n"
#endif
	       "     (ex. \"hd0a:netbsd.old -s\")\n"
	       "pkboot [dev:][filename] [-12acdqsvxz]\n"
	       "dev [dev:]\n"
	       "consdev {pc|com[0123][,{speed}]|com,{ioport}[,{speed}]}\n"
	       "root    {spec}\n"
	       "     spec can be disk, e.g. wd0, sd0\n"
	       "     or string like wedge:name\n"
	       "devpath\n"
	       "efivar\n"
	       "gop [{modenum|list}]\n"
	       "load {path_to_module}\n"
#if LIBSA_ENABLE_LS_OP
	       "ls [dev:][path]\n"
#endif
	       "memmap [{sorted|unsorted|compact}]\n"
#ifndef SMALL
	       "menu (reenters boot menu, if defined in boot.cfg)\n"
#endif
	       "modules {on|off|enabled|disabled}\n"
	       "multiboot [dev:][filename] [<args>]\n"
	       "reloc {address|none|default}\n"
	       "rndseed {path_to_rndseed_file}\n"
	       "splash {path_to_image_file}\n"
	       "text [{modenum|list}]\n"
	       "userconf {command}\n"
	       "version\n"
	       "help|?\n"
	       "quit\n");
}

#if LIBSA_ENABLE_LS_OP
void
command_ls(char *arg)
{
	const char *save = default_filename;

	default_filename = "/";
	ls(arg);
	default_filename = save;
}
#endif

/* ARGSUSED */
void
command_quit(char *arg)
{

	printf("Exiting...\n");
	delay(1 * 1000 * 1000);
	reboot();
	/* Note: we shouldn't get to this point! */
	panic("Could not reboot!");
}

static void
bootit2(char *path, size_t plen, int howto)
{
	bootit(path, howto);
	snprintf(path, plen, "%s.gz", path);
	bootit(path, howto | AB_VERBOSE);
}

void
command_boot(char *arg)
{
	char *filename;
	char path[512];
	int howto;

	if (!parseboot(arg, &filename, &howto))
		return;

	if (filename != NULL && filename[0] != '\0') {
		/* try old locations first to appease atf test beds */
		snprintf(path, sizeof(path) - 4, "%s", filename);
		bootit2(path, sizeof(path), howto);

		/*
		 * now treat given filename as a directory unless there
		 * is already an embedded path-name separator '/' present
		 */
		if (strchr(filename + 1, '/') == NULL) {
			snprintf(path, sizeof(path) - 4, "%s/kernel",
			    filename);
			bootit2(path, sizeof(path), howto);
		}
	} else {
		int i;

		for (i = 0; i < NUMNAMES; i++) {
			bootit(names[i][0], howto);
			bootit(names[i][1], howto);
		}
	}
}

void
command_pkboot(char *arg)
{
	extern int has_prekern;
	has_prekern = 1;
	command_boot(arg);
	has_prekern = 0;
}

void
command_dev(char *arg)
{
	static char savedevname[MAXDEVNAME + 1];
	char buf[80];
	char *devname;
	const char *file; /* dummy */

	if (*arg == '\0') {
		efi_disk_show();
		efi_net_show();

		if (default_part_name != NULL)
			printf("default NAME=%s\n", default_part_name);
		else
			printf("default %s\n",
			       snprint_bootdev(buf, sizeof(buf),
					       default_devname, default_unit,
					       default_partition));
		return;
	}

	if (strchr(arg, ':') == NULL ||
	    parsebootfile(arg, &default_fsname, &devname, &default_unit,
	      &default_partition, &file)) {
		command_help(NULL);
		return;
	}

	/* put to own static storage */
	strncpy(savedevname, devname, MAXDEVNAME + 1);
	default_devname = savedevname;

	/* +5 to skip leading NAME= */
	if (strstr(devname, "NAME=") == devname)
		default_part_name = default_devname + 5;
}

static const struct cons_devs {
	const char	*name;
	u_int		tag;
	int		ioport;
} cons_devs[] = {
	{ "pc",		CONSDEV_PC,   0 },
	{ "com0",	CONSDEV_COM0, 0 },
	{ "com1",	CONSDEV_COM1, 0 },
	{ "com2",	CONSDEV_COM2, 0 },
	{ "com3",	CONSDEV_COM3, 0 },
	{ "com0kbd",	CONSDEV_COM0KBD, 0 },
	{ "com1kbd",	CONSDEV_COM1KBD, 0 },
	{ "com2kbd",	CONSDEV_COM2KBD, 0 },
	{ "com3kbd",	CONSDEV_COM3KBD, 0 },
	{ "com",	CONSDEV_COM0, -1 },
	{ "auto",	CONSDEV_AUTO, 0 },
	{ NULL,		0 }
};

void
command_consdev(char *arg)
{
	const struct cons_devs *cdp;
	char *sep, *sep2 = NULL;
	int ioport, speed = 0;

	if (*arg == '\0') {
		efi_cons_show();
		return;
	}

	sep = strchr(arg, ',');
	if (sep != NULL) {
		*sep++ = '\0';
		sep2 = strchr(sep, ',');
		if (sep2 != NULL)
			*sep2++ = '\0';
	}

	for (cdp = cons_devs; cdp->name; cdp++) {
		if (strcmp(arg, cdp->name) == 0) {
			ioport = cdp->ioport;
			if (cdp->tag == CONSDEV_PC || cdp->tag == CONSDEV_AUTO) {
				if (sep != NULL || sep2 != NULL)
					goto error;
			} else {
				/* com? */
				if (ioport == -1) {
					if (sep != NULL) {
						u_long t = strtoul(sep, NULL, 0);
						if (t > INT_MAX)
							goto error;
						ioport = (int)t;
					}
					if (sep2 != NULL) {
						speed = atoi(sep2);
						if (speed < 0)
							goto error;
					}
				} else {
					if (sep != NULL) {
						speed = atoi(sep);
						if (speed < 0)
							goto error;
					}
					if (sep2 != NULL)
						goto error;
				}
			}
			efi_consinit(cdp->tag, ioport, speed);
			clearit();
			print_bootcfg_banner(bootprog_name, bootprog_rev);
			return;
		}
	}
error:
	printf("invalid console device.\n");
}

void
command_root(char *arg)
{
	struct btinfo_rootdevice *biv = &bi_root;

	strncpy(biv->devname, arg, sizeof(biv->devname));
	if (biv->devname[sizeof(biv->devname)-1] != '\0') {
		biv->devname[sizeof(biv->devname)-1] = '\0';
		printf("truncated to %s\n",biv->devname);
	}
}


#ifndef SMALL
/* ARGSUSED */
void
command_menu(char *arg)
{

	if (bootcfg_info.nummenu > 0) {
		/* Does not return */
		doboottypemenu();
	} else
		printf("No menu defined in boot.cfg\n");
}
#endif /* !SMALL */

void
command_modules(char *arg)
{

	if (strcmp(arg, "enabled") == 0 ||
	    strcmp(arg, "on") == 0)
		boot_modules_enabled = true;
	else if (strcmp(arg, "disabled") == 0 ||
	    strcmp(arg, "off") == 0)
		boot_modules_enabled = false;
	else
		printf("invalid flag, must be 'enabled' or 'disabled'.\n");
}

void
command_multiboot(char *arg)
{
	char *filename;

	filename = arg;
	if (exec_multiboot(filename, gettrailer(arg)) < 0)
		printf("multiboot: %s: %s\n", sprint_bootsel(filename),
		       strerror(errno));
	else
		printf("boot returned\n");
}

void
command_reloc(char *arg)
{
	char *ep;

	if (*arg == '\0') {
		switch (efi_reloc_type) {
		case RELOC_NONE:
			printf("reloc: none\n");
			break;
		case RELOC_ADDR:
			printf("reloc: %p\n", (void *)efi_kernel_reloc);
			break;
		case RELOC_DEFAULT:
		default:
			printf("reloc: default\n");
			break;
		}
		goto out;
	}

	if (strcmp(arg, "default") == 0) {
		efi_reloc_type = RELOC_DEFAULT;
		goto out;
	}

	if (strcmp(arg, "none") == 0) {
		efi_reloc_type = RELOC_NONE;
		goto out;
	}

	errno = 0;
	efi_kernel_reloc = strtoul(arg, &ep, 0);
	if (ep == arg || *ep != '\0' || errno)
		printf("could not parse address \"%s\"\n", arg);
	else
		efi_reloc_type = RELOC_ADDR;
out:
	return;

}

void
command_version(char *arg)
{
	CHAR16 *path;
	char *upath, *ufirmware;
	int rv;

	if (strcmp(arg, "full") == 0) {
		printf("ImageBase: 0x%" PRIxPTR "\n",
		    (uintptr_t)efi_li->ImageBase);
		printf("Stack: 0x%" PRIxPTR "\n", efi_main_sp);
		printf("EFI version: %d.%02d\n",
		    ST->Hdr.Revision >> 16, ST->Hdr.Revision & 0xffff);
		ufirmware = NULL;
		rv = ucs2_to_utf8(ST->FirmwareVendor, &ufirmware);
		if (rv == 0) {
			printf("EFI Firmware: %s (rev %d.%02d)\n", ufirmware,
			    ST->FirmwareRevision >> 16,
			    ST->FirmwareRevision & 0xffff);
			FreePool(ufirmware);
		}
		path = DevicePathToStr(efi_bootdp);
		upath = NULL;
		rv = ucs2_to_utf8(path, &upath);
		FreePool(path);
		if (rv == 0) {
			printf("Boot DevicePath: %d:%d:%s\n",
			    DevicePathType(efi_bootdp),
			    DevicePathSubType(efi_bootdp), upath);
			FreePool(upath);
		}
	}

	printf("\n"
	    ">> %s, Revision %s (from NetBSD %s)\n"
	    ">> Memory: %d/%d k\n",
	    bootprog_name, bootprog_rev, bootprog_kernrev,
	    getbasemem(), getextmem());
}

void
command_memmap(char *arg)
{
	bool sorted = true;
	bool compact = false;

	if (*arg == '\0' || strcmp(arg, "sorted") == 0)
		/* Already sorted is true. */;
	else if (strcmp(arg, "unsorted") == 0)
		sorted = false;
	else if (strcmp(arg, "compact") == 0)
		compact = true;
	else {
		printf("invalid flag, "
		    "must be 'sorted', 'unsorted' or 'compact'.\n");
		return;
	}

	efi_memory_show_map(sorted, compact);
}

void
command_devpath(char *arg)
{
	EFI_STATUS status;
	UINTN i, nhandles;
	EFI_HANDLE *handles;
	EFI_DEVICE_PATH *dp0, *dp;
	CHAR16 *path;
	char *upath;
	UINTN cols, rows, row = 0;
	int rv;

	status = uefi_call_wrapper(ST->ConOut->QueryMode, 4, ST->ConOut,
	    ST->ConOut->Mode->Mode, &cols, &rows);
	if (EFI_ERROR(status) || rows <= 2)
		rows = 0;
	else
		rows -= 2;

	/*
	 * all devices.
	 */
	status = LibLocateHandle(ByProtocol, &DevicePathProtocol, NULL,
	    &nhandles, &handles);
	if (EFI_ERROR(status))
		return;

	for (i = 0; i < nhandles; i++) {
		status = uefi_call_wrapper(BS->HandleProtocol, 3, handles[i],
		    &DevicePathProtocol, (void **)&dp0);
		if (EFI_ERROR(status))
			break;

		printf("DevicePathType %d\n", DevicePathType(dp0));
		if (++row >= rows) {
			row = 0;
			printf("Press Any Key to continue :");
			(void) awaitkey(-1, 0);
			printf("\n");
		}
		for (dp = dp0;
		     !IsDevicePathEnd(dp);
		     dp = NextDevicePathNode(dp)) {

			path = DevicePathToStr(dp);
			upath = NULL;
			rv = ucs2_to_utf8(path, &upath);
			FreePool(path);
			if (rv) {
				printf("convert failed\n");
				break;
			}

			printf("%d:%d:%s\n", DevicePathType(dp),
			    DevicePathSubType(dp), upath);
			FreePool(upath);

			if (++row >= rows) {
				row = 0;
				printf("Press Any Key to continue :");
				(void) awaitkey(-1, 0);
				printf("\n");
			}
		}
	}
}


void
command_efivar(char *arg)
{
	static const char header[] =
	 "GUID                                 Variable Name        Value\n"
	 "==================================== ==================== ========\n";
	EFI_STATUS status;
	UINTN sz = 64, osz;
	CHAR16 *name = NULL, *tmp, *val, guid[128];
	char *uname, *uval, *uguid;
	EFI_GUID vendor;
	UINTN cols, rows, row = 0;
	int rv;

	status = uefi_call_wrapper(ST->ConOut->QueryMode, 4, ST->ConOut,
	    ST->ConOut->Mode->Mode, &cols, &rows);
	if (EFI_ERROR(status) || rows <= 2)
		rows = 0;
	else
		rows -= 2;

	name = AllocatePool(sz);
	if (name == NULL) {
		printf("memory allocation failed: %" PRIuMAX" bytes\n",
		    (uintmax_t)sz);
		return;
	}

	SetMem(name, sz, 0);
	vendor = NullGuid;

	printf("%s", header);
	for (;;) {
		osz = sz;
		status = uefi_call_wrapper(RT->GetNextVariableName, 3,
		    &sz, name, &vendor);
		if (EFI_ERROR(status)) {
			if (status == EFI_NOT_FOUND)
				break;
			if (status != EFI_BUFFER_TOO_SMALL) {
				printf("GetNextVariableName failed: %" PRIxMAX "\n",
				    (uintmax_t)status);
				break;
			}

			tmp = AllocatePool(sz);
			if (tmp == NULL) {
				printf("memory allocation failed: %" PRIuMAX
				    "bytes\n", (uintmax_t)sz);
				break;
			}
			SetMem(tmp, sz, 0);
			CopyMem(tmp, name, osz);
			FreePool(name);
			name = tmp;
			continue;
		}

		val = LibGetVariable(name, &vendor);
		if (val != NULL) {
			uval = NULL;
			rv = ucs2_to_utf8(val, &uval);
			FreePool(val);
			if (rv) {
				printf("value convert failed\n");
				break;
			}
		} else
			uval = NULL;
		uname = NULL;
		rv = ucs2_to_utf8(name, &uname);
		if (rv) {
			printf("name convert failed\n");
			FreePool(uval);
			break;
		}
		GuidToString(guid, &vendor);
		uguid = NULL;
		rv = ucs2_to_utf8(guid, &uguid);
		if (rv) {
			printf("GUID convert failed\n");
			FreePool(uval);
			FreePool(uname);
			break;
		}
		printf("%-35s %-20s %s\n", uguid, uname, uval ? uval : "(null)");
		FreePool(uguid);
		FreePool(uname);
		if (uval != NULL)
			FreePool(uval);

		if (++row >= rows) {
			row = 0;
			printf("Press Any Key to continue :");
			(void) awaitkey(-1, 0);
			printf("\n");
		}
	}

	FreePool(name);
}
