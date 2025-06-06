/*	$NetBSD: kern_module.c,v 1.173 2025/05/05 00:31:48 pgoyette Exp $	*/

/*-
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software developed for The NetBSD Foundation
 * by Andrew Doran.
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

/*
 * Kernel module support.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: kern_module.c,v 1.173 2025/05/05 00:31:48 pgoyette Exp $");

#define _MODULE_INTERNAL

#ifdef _KERNEL_OPT
#include "opt_ddb.h"
#include "opt_modular.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/lwp.h>
#include <sys/kauth.h>
#include <sys/kobj.h>
#include <sys/kmem.h>
#include <sys/module.h>
#include <sys/module_hook.h>
#include <sys/kthread.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/evcnt.h>

#include <uvm/uvm_extern.h>

struct vm_map *module_map;
const char *module_machine;
char	module_base[MODULE_BASE_SIZE];

struct modlist        module_list = TAILQ_HEAD_INITIALIZER(module_list);
struct modlist        module_builtins = TAILQ_HEAD_INITIALIZER(module_builtins);
static struct modlist module_bootlist = TAILQ_HEAD_INITIALIZER(module_bootlist);

struct module_callbacks {
	TAILQ_ENTRY(module_callbacks) modcb_list;
	void (*modcb_load)(struct module *);
	void (*modcb_unload)(struct module *);
};
TAILQ_HEAD(modcblist, module_callbacks);
static struct modcblist modcblist;

static module_t *module_netbsd;
static const modinfo_t module_netbsd_modinfo = {
	.mi_version = __NetBSD_Version__,
	.mi_class = MODULE_CLASS_MISC,
	.mi_name = "netbsd"
};

static module_t	*module_active;

__read_mostly
#ifdef MODULAR_DEFAULT_VERBOSE
bool		module_verbose_on = true;
#else
bool		module_verbose_on = false;
#endif

__read_mostly
#ifdef MODULAR_DEFAULT_AUTOLOAD
bool		module_autoload_on = true;
#else
bool		module_autoload_on = false;
#endif

__read_mostly
#ifdef MODULAR_DEFAULT_AUTOUNLOAD_UNSAFE
bool		module_autounload_unsafe = true;
#else
bool		module_autounload_unsafe = false;
#endif
u_int		module_count;
u_int		module_builtinlist;
u_int		module_autotime = 10;
u_int		module_gen = 1;
static kcondvar_t module_thread_cv;
static kmutex_t module_thread_lock;
static int	module_thread_ticks;
int (*module_load_vfs_vec)(const char *, int, bool, module_t *,
			   prop_dictionary_t *) = (void *)eopnotsupp;

static kauth_listener_t	module_listener;

static specificdata_domain_t module_specificdata_domain;

/* Ensure that the kernel's link set isn't empty. */
static modinfo_t module_dummy;
__link_set_add_rodata(modules, module_dummy);

static module_t	*module_newmodule(modsrc_t);
static void	module_free(module_t *);
static void	module_require_force(module_t *);
static int	module_do_load(const char *, bool, int, prop_dictionary_t,
		    module_t **, modclass_t modclass, bool);
static int	module_do_unload(const char *, bool);
static int	module_do_builtin(const module_t *, const char *, module_t **,
    prop_dictionary_t);
static int	module_fetch_info(module_t *);
static void	module_thread(void *);

static module_t	*module_lookup(const char *);
static void	module_enqueue(module_t *);

static bool	module_merge_dicts(prop_dictionary_t, const prop_dictionary_t);

static void	sysctl_module_setup(void);
static int	sysctl_module_autotime(SYSCTLFN_PROTO);

static void	module_callback_load(struct module *);
static void	module_callback_unload(struct module *);

#define MODULE_CLASS_MATCH(mi, modclass) \
	((modclass) == MODULE_CLASS_ANY || (modclass) == (mi)->mi_class)

static void
module_incompat(const modinfo_t *mi, int modclass)
{
	module_error("Incompatible module class %d for `%s' (wanted %d)",
	    mi->mi_class, mi->mi_name, modclass);
}

struct module *
module_kernel(void)
{

	return module_netbsd;
}

/*
 * module_error:
 *
 *	Utility function: log an error.
 */
void
module_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	printf("WARNING: module error: ");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
}

/*
 * module_print:
 *
 *	Utility function: log verbose output.
 */
void
module_print(const char *fmt, ...)
{
	va_list ap;

	if (module_verbose_on) {
		va_start(ap, fmt);
		printf("DEBUG: module: ");
		vprintf(fmt, ap);
		printf("\n");
		va_end(ap);
	}
}

/*
 * module_name:
 *
 *	Utility function: return the module's name.
 */
const char *
module_name(struct module *mod)
{

	return mod->mod_info->mi_name;
}

/*
 * module_source:
 *
 *	Utility function: return the module's source.
 */
modsrc_t
module_source(struct module *mod)
{

	return mod->mod_source;
}

static int
module_listener_cb(kauth_cred_t cred, kauth_action_t action, void *cookie,
    void *arg0, void *arg1, void *arg2, void *arg3)
{
	int result;

	result = KAUTH_RESULT_DEFER;

	if (action != KAUTH_SYSTEM_MODULE)
		return result;

	if ((uintptr_t)arg2 != 0)	/* autoload */
		result = KAUTH_RESULT_ALLOW;

	return result;
}

/*
 * Allocate a new module_t
 */
static module_t *
module_newmodule(modsrc_t source)
{
	module_t *mod;

	mod = kmem_zalloc(sizeof(*mod), KM_SLEEP);
	mod->mod_source = source;
	specificdata_init(module_specificdata_domain, &mod->mod_sdref);
	return mod;
}

/*
 * Free a module_t
 */
static void
module_free(module_t *mod)
{

	specificdata_fini(module_specificdata_domain, &mod->mod_sdref);
	if (mod->mod_required)
		kmem_free(mod->mod_required, mod->mod_arequired *
		    sizeof(module_t *));
	kmem_free(mod, sizeof(*mod));
}

/*
 * Require the -f (force) flag to load a module
 */
static void
module_require_force(struct module *mod)
{
	SET(mod->mod_flags, MODFLG_MUST_FORCE);
}

/*
 * Add modules to the builtin list.  This can done at boottime or
 * at runtime if the module is linked into the kernel with an
 * external linker.  All or none of the input will be handled.
 * Optionally, the modules can be initialized.  If they are not
 * initialized, module_init_class() or module_load() can be used
 * later, but these are not guaranteed to give atomic results.
 */
int
module_builtin_add(modinfo_t *const *mip, size_t nmodinfo, bool init)
{
	struct module **modp = NULL, *mod_iter;
	int rv = 0, i, mipskip;

	if (init) {
		rv = kauth_authorize_system(kauth_cred_get(),
		    KAUTH_SYSTEM_MODULE, 0, (void *)(uintptr_t)MODCTL_LOAD,
		    (void *)(uintptr_t)1, NULL);
		if (rv) {
			return rv;
		}
	}

	for (i = 0, mipskip = 0; i < nmodinfo; i++) {
		if (mip[i] == &module_dummy) {
			KASSERT(nmodinfo > 0);
			nmodinfo--;
		}
	}
	if (nmodinfo == 0)
		return 0;

	modp = kmem_zalloc(sizeof(*modp) * nmodinfo, KM_SLEEP);
	for (i = 0, mipskip = 0; i < nmodinfo; i++) {
		if (mip[i+mipskip] == &module_dummy) {
			mipskip++;
			continue;
		}
		modp[i] = module_newmodule(MODULE_SOURCE_KERNEL);
		modp[i]->mod_info = mip[i+mipskip];
	}
	kernconfig_lock();

	/* do this in three stages for error recovery and atomicity */

	/* first check for presence */
	for (i = 0; i < nmodinfo; i++) {
		TAILQ_FOREACH(mod_iter, &module_builtins, mod_chain) {
			if (strcmp(mod_iter->mod_info->mi_name,
			    modp[i]->mod_info->mi_name) == 0)
				break;
		}
		if (mod_iter) {
			rv = EEXIST;
			goto out;
		}

		if (module_lookup(modp[i]->mod_info->mi_name) != NULL) {
			rv = EEXIST;
			goto out;
		}
	}

	/* then add to list */
	for (i = 0; i < nmodinfo; i++) {
		TAILQ_INSERT_TAIL(&module_builtins, modp[i], mod_chain);
		module_builtinlist++;
	}

	/* finally, init (if required) */
	if (init) {
		for (i = 0; i < nmodinfo; i++) {
			rv = module_do_builtin(modp[i],
			    modp[i]->mod_info->mi_name, NULL, NULL);
			/* throw in the towel, recovery hard & not worth it */
			if (rv)
				panic("%s: builtin module \"%s\" init failed:"
				    " %d", __func__,
				    modp[i]->mod_info->mi_name, rv);
		}
	}

 out:
	kernconfig_unlock();
	if (rv != 0) {
		for (i = 0; i < nmodinfo; i++) {
			if (modp[i])
				module_free(modp[i]);
		}
	}
	kmem_free(modp, sizeof(*modp) * nmodinfo);
	return rv;
}

/*
 * Optionally fini and remove builtin module from the kernel.
 * Note: the module will now be unreachable except via mi && builtin_add.
 */
int
module_builtin_remove(modinfo_t *mi, bool fini)
{
	struct module *mod;
	int rv = 0;

	if (fini) {
		rv = kauth_authorize_system(kauth_cred_get(),
		    KAUTH_SYSTEM_MODULE, 0, (void *)(uintptr_t)MODCTL_UNLOAD,
		    NULL, NULL);
		if (rv)
			return rv;

		kernconfig_lock();
		rv = module_do_unload(mi->mi_name, true);
		if (rv) {
			goto out;
		}
	} else {
		kernconfig_lock();
	}
	TAILQ_FOREACH(mod, &module_builtins, mod_chain) {
		if (strcmp(mod->mod_info->mi_name, mi->mi_name) == 0)
			break;
	}
	if (mod) {
		TAILQ_REMOVE(&module_builtins, mod, mod_chain);
		module_builtinlist--;
	} else {
		KASSERT(fini == false);
		rv = ENOENT;
	}

 out:
	kernconfig_unlock();
	return rv;
}

#if __NetBSD_Version__ / 1000000 % 100 == 99	/* -current */
#define LEGACY_MODULE_PATH					\
	snprintf(module_base, sizeof(module_base),		\
	    "/stand/%s/%s/modules", module_machine, osrelease);
#else						/* release */
#define LEGACY_MODULE_PATH					\
	snprintf(module_base, sizeof(module_base),		\
	    "/stand/%s/%d.%d/modules", module_machine,		\
	    __NetBSD_Version__ / 100000000,			\
	    __NetBSD_Version__ / 1000000 % 100);
#endif	/* if __NetBSD_Version__ */

/*
 * module_init:
 *
 *	Initialize the module subsystem.
 */
void
module_init(void)
{
	__link_set_decl(modules, modinfo_t);
	modinfo_t *const *mip;
	int rv;

	if (module_map == NULL) {
		module_map = kernel_map;
	}
	cv_init(&module_thread_cv, "mod_unld");
	mutex_init(&module_thread_lock, MUTEX_DEFAULT, IPL_NONE);
	TAILQ_INIT(&modcblist);

#ifdef MODULAR	/* XXX */
	module_init_md();
#endif

#ifdef KERNEL_DIR
	const char *booted_kernel = get_booted_kernel();
	if (booted_kernel) {
		while (*booted_kernel == '/')	/* ignore leading slashes */
			booted_kernel++;	/* boot lookup always at root */
		char *ptr = strrchr(booted_kernel, '/');
		if (ptr == NULL) {
			/* no dir name, use legacy module path */
			if (!module_machine)
				module_machine = machine;
			LEGACY_MODULE_PATH;
		} else {
			snprintf(module_base, sizeof(module_base),
			     "/%.*s/modules",
			    (int)(ptr - booted_kernel), booted_kernel);
		}
	} else {
		strlcpy(module_base, "/netbsd/modules", sizeof(module_base));
		printf("Cannot find kernel name, loading modules from \"%s\"\n",
		    module_base);
	}
#else	/* ifdef KERNEL_DIR */
	if (!module_machine)
		module_machine = machine;
	LEGACY_MODULE_PATH;
#endif

	module_listener = kauth_listen_scope(KAUTH_SCOPE_SYSTEM,
	    module_listener_cb, NULL);

	__link_set_foreach(mip, modules) {
		if ((rv = module_builtin_add(mip, 1, false)) != 0)
			module_error("Built-in `%s' failed: %d\n",
			    (*mip)->mi_name, rv);
	}

	sysctl_module_setup();
	module_specificdata_domain = specificdata_domain_create();

	module_netbsd = module_newmodule(MODULE_SOURCE_KERNEL);
	module_netbsd->mod_refcnt = 1;
	module_netbsd->mod_info = &module_netbsd_modinfo;
}

/*
 * module_start_unload_thread:
 *
 *	Start the auto unload kthread.
 */
void
module_start_unload_thread(void)
{
	int error;

	error = kthread_create(PRI_VM, KTHREAD_MPSAFE, NULL, module_thread,
	    NULL, NULL, "modunload");
	if (error != 0)
		panic("%s: %d", __func__, error);
}

/*
 * module_builtin_require_force
 *
 * Require MODCTL_MUST_FORCE to load any built-in modules that have 
 * not yet been initialized
 */
void
module_builtin_require_force(void)
{
	module_t *mod;

	kernconfig_lock();
	TAILQ_FOREACH(mod, &module_builtins, mod_chain) {
		module_require_force(mod);
	}
	kernconfig_unlock();
}

static struct sysctllog *module_sysctllog;

static int
sysctl_module_autotime(SYSCTLFN_ARGS)
{
	struct sysctlnode node;
	int t, error;

	t = *(int *)rnode->sysctl_data;

	node = *rnode;
	node.sysctl_data = &t;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error || newp == NULL)
		return (error);

	if (t < 0)
		return (EINVAL);

	*(int *)rnode->sysctl_data = t;
	return (0);
}

static void
sysctl_module_setup(void)
{
	const struct sysctlnode *node = NULL;

	sysctl_createv(&module_sysctllog, 0, NULL, &node,
		CTLFLAG_PERMANENT,
		CTLTYPE_NODE, "module",
		SYSCTL_DESCR("Module options"),
		NULL, 0, NULL, 0,
		CTL_KERN, CTL_CREATE, CTL_EOL);

	if (node == NULL)
		return;

	sysctl_createv(&module_sysctllog, 0, &node, NULL,
		CTLFLAG_PERMANENT | CTLFLAG_READWRITE,
		CTLTYPE_BOOL, "autoload",
		SYSCTL_DESCR("Enable automatic load of modules"),
		NULL, 0, &module_autoload_on, 0,
		CTL_CREATE, CTL_EOL);
	sysctl_createv(&module_sysctllog, 0, &node, NULL,
		CTLFLAG_PERMANENT | CTLFLAG_READWRITE,
		CTLTYPE_BOOL, "autounload_unsafe",
		SYSCTL_DESCR("Enable automatic unload of unaudited modules"),
		NULL, 0, &module_autounload_unsafe, 0,
		CTL_CREATE, CTL_EOL);
	sysctl_createv(&module_sysctllog, 0, &node, NULL,
		CTLFLAG_PERMANENT | CTLFLAG_READWRITE,
		CTLTYPE_BOOL, "verbose",
		SYSCTL_DESCR("Enable verbose output"),
		NULL, 0, &module_verbose_on, 0,
		CTL_CREATE, CTL_EOL);
	sysctl_createv(&module_sysctllog, 0, &node, NULL,
		CTLFLAG_PERMANENT | CTLFLAG_READONLY,
		CTLTYPE_STRING, "path",
		SYSCTL_DESCR("Default module load path"),
		NULL, 0, module_base, 0,
		CTL_CREATE, CTL_EOL);
	sysctl_createv(&module_sysctllog, 0, &node, NULL,
		CTLFLAG_PERMANENT | CTLFLAG_READWRITE,
		CTLTYPE_INT, "autotime",
		SYSCTL_DESCR("Auto-unload delay"),
		sysctl_module_autotime, 0, &module_autotime, 0,
		CTL_CREATE, CTL_EOL);
}

/*
 * module_init_class:
 *
 *	Initialize all built-in and pre-loaded modules of the
 *	specified class.
 */
void
module_init_class(modclass_t modclass)
{
	TAILQ_HEAD(, module) bi_fail = TAILQ_HEAD_INITIALIZER(bi_fail);
	module_t *mod;
	modinfo_t *mi;

	kernconfig_lock();
	/*
	 * Builtins first.  These will not depend on pre-loaded modules
	 * (because the kernel would not link).
	 */
	do {
		TAILQ_FOREACH(mod, &module_builtins, mod_chain) {
			mi = mod->mod_info;
			if (!MODULE_CLASS_MATCH(mi, modclass))
				continue;
			/*
			 * If initializing a builtin module fails, don't try
			 * to load it again.  But keep it around and queue it
			 * on the builtins list after we're done with module
			 * init.  Don't set it to MODFLG_MUST_FORCE in case a
			 * future attempt to initialize can be successful.
			 * (If the module has previously been set to
			 * MODFLG_MUST_FORCE, don't try to override that!)
			 */
			if (ISSET(mod->mod_flags, MODFLG_MUST_FORCE) ||
			    module_do_builtin(mod, mi->mi_name, NULL,
			    NULL) != 0) {
				TAILQ_REMOVE(&module_builtins, mod, mod_chain);
				TAILQ_INSERT_TAIL(&bi_fail, mod, mod_chain);
			}
			break;
		}
	} while (mod != NULL);

	/*
	 * Now preloaded modules.  These will be pulled off the
	 * list as we call module_do_load();
	 */
	do {
		TAILQ_FOREACH(mod, &module_bootlist, mod_chain) {
			mi = mod->mod_info;
			if (!MODULE_CLASS_MATCH(mi, modclass))
				continue;
			module_do_load(mi->mi_name, false, 0, NULL, NULL,
			    modclass, false);
			break;
		}
	} while (mod != NULL);

	/* return failed builtin modules to builtin list */
	while ((mod = TAILQ_FIRST(&bi_fail)) != NULL) {
		TAILQ_REMOVE(&bi_fail, mod, mod_chain);
		TAILQ_INSERT_TAIL(&module_builtins, mod, mod_chain);
	}

	kernconfig_unlock();
}

/*
 * module_compatible:
 *
 *	Return true if the two supplied kernel versions are said to
 *	have the same binary interface for kernel code.  The entire
 *	version is significant for the development tree (-current),
 *	major and minor versions are significant for official
 *	releases of the system.
 */
bool
module_compatible(int v1, int v2)
{

#if __NetBSD_Version__ / 1000000 % 100 == 99	/* -current */
	return v1 == v2;
#else						/* release */
	return abs(v1 - v2) < 10000;
#endif
}

/*
 * module_load:
 *
 *	Load a single module from the file system.
 */
int
module_load(const char *filename, int flags, prop_dictionary_t props,
	    modclass_t modclass)
{
	module_t *mod;
	int error;

	/* Test if we already have the module loaded before
	 * authorizing so we have the opportunity to return EEXIST. */
	kernconfig_lock();
	mod = module_lookup(filename);
	if (mod != NULL) {
		module_print("%s module `%s' already loaded",
		    "Requested", filename);
		error = EEXIST;
		goto out;
	}

	/* Authorize. */
	error = kauth_authorize_system(kauth_cred_get(), KAUTH_SYSTEM_MODULE,
	    0, (void *)(uintptr_t)MODCTL_LOAD, NULL, NULL);
	if (error != 0)
		goto out;

	error = module_do_load(filename, false, flags, props, NULL, modclass,
	    false);

out:
	kernconfig_unlock();
	return error;
}

/*
 * module_autoload:
 *
 *	Load a single module from the file system, system initiated.
 */
int
module_autoload(const char *filename, modclass_t modclass)
{
	int error;
	struct proc *p = curlwp->l_proc;

	kernconfig_lock();

	module_print("Autoload for `%s' requested by pid %d (%s)",
	    filename, p->p_pid, p->p_comm);

	/* Nothing if the user has disabled it. */
	if (!module_autoload_on) {
		module_print("Autoload disabled for `%s' ", filename);
		kernconfig_unlock();
		return EPERM;
	}

        /* Disallow path separators and magic symlinks. */
        if (strchr(filename, '/') != NULL || strchr(filename, '@') != NULL ||
            strchr(filename, '.') != NULL) {
		module_print("Autoload illegal path for `%s' ", filename);
		kernconfig_unlock();
        	return EPERM;
	}

	/* Authorize. */
	error = kauth_authorize_system(kauth_cred_get(), KAUTH_SYSTEM_MODULE,
	    0, (void *)(uintptr_t)MODCTL_LOAD, (void *)(uintptr_t)1, NULL);

	if (error != 0) {
		module_print("Autoload  not authorized for `%s' ", filename);
		kernconfig_unlock();
		return error;
	}
	error = module_do_load(filename, false, 0, NULL, NULL, modclass, true);

	module_print("Autoload for `%s' status %d", filename, error);
	kernconfig_unlock();
	return error;
}

/*
 * module_unload:
 *
 *	Find and unload a module by name.
 */
int
module_unload(const char *name)
{
	int error;

	/* Authorize. */
	error = kauth_authorize_system(kauth_cred_get(), KAUTH_SYSTEM_MODULE,
	    0, (void *)(uintptr_t)MODCTL_UNLOAD, NULL, NULL);
	if (error != 0) {
		return error;
	}

	kernconfig_lock();
	error = module_do_unload(name, true);
	kernconfig_unlock();

	return error;
}

/*
 * module_lookup:
 *
 *	Look up a module by name.
 */
module_t *
module_lookup(const char *name)
{
	module_t *mod;

	KASSERT(kernconfig_is_held());

	TAILQ_FOREACH(mod, &module_list, mod_chain) {
		if (strcmp(mod->mod_info->mi_name, name) == 0)
			break;
	}

	return mod;
}

/*
 * module_hold:
 *
 *	Add a single reference to a module.  It's the caller's
 *	responsibility to ensure that the reference is dropped
 *	later.
 */
void
module_hold(module_t *mod)
{

	kernconfig_lock();
	mod->mod_refcnt++;
	kernconfig_unlock();
}

/*
 * module_rele:
 *
 *	Release a reference acquired with module_hold().
 */
void
module_rele(module_t *mod)
{

	kernconfig_lock();
	KASSERT(mod->mod_refcnt > 0);
	mod->mod_refcnt--;
	kernconfig_unlock();
}

/*
 * module_enqueue:
 *
 *	Put a module onto the global list and update counters.
 */
void
module_enqueue(module_t *mod)
{
	int i;

	KASSERT(kernconfig_is_held());

	/*
	 * Put new entry at the head of the queue so autounload can unload
	 * requisite modules with only one pass through the queue.
	 */
	TAILQ_INSERT_HEAD(&module_list, mod, mod_chain);
	if (mod->mod_nrequired) {

		/* Add references to the requisite modules. */
		for (i = 0; i < mod->mod_nrequired; i++) {
			KASSERT((*mod->mod_required)[i] != NULL);
			(*mod->mod_required)[i]->mod_refcnt++;
		}
	}
	module_count++;
	module_gen++;
}

/*
 * Our array of required module pointers starts with zero entries.  If we
 * need to add a new entry, and the list is already full, we reallocate a
 * larger array, adding MAXMODDEPS entries.
 */
static void
alloc_required(module_t *mod)
{
	module_t *(*new)[], *(*old)[];
	int areq;
	int i;

	if (mod->mod_nrequired >= mod->mod_arequired) {
		areq = mod->mod_arequired + MAXMODDEPS;
		old = mod->mod_required;
		new = kmem_zalloc(areq * sizeof(module_t *), KM_SLEEP);
		for (i = 0; i < mod->mod_arequired; i++)
			(*new)[i] = (*old)[i];
		mod->mod_required = new;
		if (old)
			kmem_free(old, mod->mod_arequired * sizeof(module_t *));
		mod->mod_arequired = areq;
	}
}

/*
 * module_do_builtin:
 *
 *	Initialize a module from the list of modules that are
 *	already linked into the kernel.
 */
static int
module_do_builtin(const module_t *pmod, const char *name, module_t **modp,
    prop_dictionary_t props)
{
	const char *p, *s;
	char buf[MAXMODNAME];
	modinfo_t *mi = NULL;
	module_t *mod, *mod2, *mod_loaded, *prev_active;
	size_t len;
	int error;

	KASSERT(kernconfig_is_held());

	/*
	 * Search the list to see if we have a module by this name.
	 */
	TAILQ_FOREACH(mod, &module_builtins, mod_chain) {
		if (strcmp(mod->mod_info->mi_name, name) == 0) {
			mi = mod->mod_info;
			break;
		}
	}

	/*
	 * Check to see if already loaded.  This might happen if we
	 * were already loaded as a dependency.
	 */
	if ((mod_loaded = module_lookup(name)) != NULL) {
		KASSERT(mod == NULL);
		if (modp)
			*modp = mod_loaded;
		return 0;
	}

	/* Note! This is from TAILQ, not immediate above */
	if (mi == NULL) {
		/*
		 * XXX: We'd like to panic here, but currently in some
		 * cases (such as nfsserver + nfs), the dependee can be
		 * successfully linked without the dependencies.
		 */
		module_error("Built-in module `%s' can't find built-in "
		    "dependency `%s'", pmod->mod_info->mi_name, name);
		return ENOENT;
	}

	/*
	 * Initialize pre-requisites.
	 */
	KASSERT(mod->mod_required == NULL);
	KASSERT(mod->mod_arequired == 0);
	KASSERT(mod->mod_nrequired == 0);
	if (mi->mi_required != NULL) {
		for (s = mi->mi_required; *s != '\0'; s = p) {
			if (*s == ',')
				s++;
			p = s;
			while (*p != '\0' && *p != ',')
				p++;
			len = uimin(p - s + 1, sizeof(buf));
			strlcpy(buf, s, len);
			if (buf[0] == '\0')
				break;
			alloc_required(mod);
			error = module_do_builtin(mod, buf, &mod2, NULL);
			if (error != 0) {
				module_error("Built-in module `%s' prerequisite "
				    "`%s' failed, error %d", name, buf, error);
				goto fail;
			}
			(*mod->mod_required)[mod->mod_nrequired++] = mod2;
		}
	}

	/*
	 * Try to initialize the module.
	 */
	prev_active = module_active;
	module_active = mod;
	error = (*mi->mi_modcmd)(MODULE_CMD_INIT, props);
	module_active = prev_active;
	if (error != 0) {
		module_error("Built-in module `%s' failed its MODULE_CMD_INIT, "
		    "error %d", mi->mi_name, error);
		goto fail;
	}

	/* load always succeeds after this point */

	TAILQ_REMOVE(&module_builtins, mod, mod_chain);
	module_builtinlist--;
	if (modp != NULL) {
		*modp = mod;
	}
	module_enqueue(mod);
	return 0;

 fail:
	if (mod->mod_required)
		kmem_free(mod->mod_required, mod->mod_arequired *
		    sizeof(module_t *));
	mod->mod_arequired = 0;
	mod->mod_nrequired = 0;
	mod->mod_required = NULL;
	return error;
}

/*
 * module_load_sysctl
 *
 * Check to see if a non-builtin module has any SYSCTL_SETUP() routine(s)
 * registered.  If so, call it (them).
 */

static void
module_load_sysctl(module_t *mod)
{
	void (**ls_funcp)(struct sysctllog **);
	void *ls_start;
	size_t ls_size, count;
	int error;

	/*
	 * Built-in modules don't have a mod_kobj so we cannot search
	 * for their link_set_sysctl_funcs
	 */
	if (mod->mod_source == MODULE_SOURCE_KERNEL)
		return;

	error = kobj_find_section(mod->mod_kobj, "link_set_sysctl_funcs",
	    &ls_start, &ls_size);
	if (error == 0) {
		count = ls_size / sizeof(ls_start);
		ls_funcp = ls_start;
		while (count--) {
			(**ls_funcp)(&mod->mod_sysctllog);
			ls_funcp++;
		}
	}
}

/*
 * module_load_evcnt
 *
 * Check to see if a non-builtin module has any static evcnt's defined;
 * if so, attach them.
 */

static void
module_load_evcnt(module_t *mod)
{
	struct evcnt * const *ls_evp;
	void *ls_start;
	size_t ls_size, count;
	int error;

	/*
	 * Built-in modules' static evcnt stuff will be handled
	 * automatically as part of general kernel initialization
	 */
	if (mod->mod_source == MODULE_SOURCE_KERNEL)
		return;

	error = kobj_find_section(mod->mod_kobj, "link_set_evcnts",
	    &ls_start, &ls_size);
	if (error == 0) {
		count = ls_size / sizeof(*ls_evp);
		ls_evp = ls_start;
		while (count--) {
			evcnt_attach_static(*ls_evp++);
		}
	}
}

/*
 * module_unload_evcnt
 *
 * Check to see if a non-builtin module has any static evcnt's defined;
 * if so, detach them.
 */

static void
module_unload_evcnt(module_t *mod)
{
	struct evcnt * const *ls_evp;
	void *ls_start;
	size_t ls_size, count;
	int error;

	/*
	 * Built-in modules' static evcnt stuff will be handled
	 * automatically as part of general kernel initialization
	 */
	if (mod->mod_source == MODULE_SOURCE_KERNEL)
		return;

	error = kobj_find_section(mod->mod_kobj, "link_set_evcnts",
	    &ls_start, &ls_size);
	if (error == 0) {
		count = ls_size / sizeof(*ls_evp);
		ls_evp = (void *)((char *)ls_start + ls_size);
		while (count--) {
			evcnt_detach(*--ls_evp);
		}
	}
}

/*
 * module_do_load:
 *
 *	Helper routine: load a module from the file system, or one
 *	pushed by the boot loader.
 */
static int
module_do_load(const char *name, bool isdep, int flags,
	       prop_dictionary_t props, module_t **modp, modclass_t modclass,
	       bool autoload)
{
	/* The pending list for this level of recursion */
	TAILQ_HEAD(pending_t, module);
	struct pending_t *pending;
	struct pending_t new_pending = TAILQ_HEAD_INITIALIZER(new_pending);

	/* The stack of pending lists */
	static SLIST_HEAD(pend_head, pend_entry) pend_stack =
		SLIST_HEAD_INITIALIZER(pend_stack);
	struct pend_entry {
		SLIST_ENTRY(pend_entry) pe_entry;
		struct pending_t *pe_pending;
	} my_pend_entry;

	modinfo_t *mi;
	module_t *mod, *mod2, *prev_active;
	prop_dictionary_t filedict;
	char buf[MAXMODNAME];
	const char *s, *p;
	int error;
	size_t len;

	KASSERT(kernconfig_is_held());

	filedict = NULL;
	error = 0;

	/*
	 * Set up the pending list for this entry.  If this is an
	 * internal entry (for a dependency), then use the same list
	 * as for the outer call;  otherwise, it's an external entry
	 * (possibly recursive, ie a module's xxx_modcmd(init, ...)
	 * routine called us), so use the locally allocated list.  In
	 * either case, add it to our stack.
	 */
	if (isdep) {
		KASSERT(SLIST_FIRST(&pend_stack) != NULL);
		pending = SLIST_FIRST(&pend_stack)->pe_pending;
	} else
		pending = &new_pending;
	my_pend_entry.pe_pending = pending;
	SLIST_INSERT_HEAD(&pend_stack, &my_pend_entry, pe_entry);

	/*
	 * Search the list of disabled builtins first.
	 */
	TAILQ_FOREACH(mod, &module_builtins, mod_chain) {
		if (strcmp(mod->mod_info->mi_name, name) == 0) {
			break;
		}
	}
	if (mod) {
		if (ISSET(mod->mod_flags, MODFLG_MUST_FORCE) &&
		    !ISSET(flags, MODCTL_LOAD_FORCE)) {
			if (!autoload) {
				module_error("Use -f to reinstate "
				    "builtin module `%s'", name);
			}
			SLIST_REMOVE_HEAD(&pend_stack, pe_entry);
			return EPERM;
		} else {
			SLIST_REMOVE_HEAD(&pend_stack, pe_entry);
			error = module_do_builtin(mod, name, modp, props);
			module_print("module_do_builtin() returned %d", error);
			return error;
		}
	}

	/*
	 * Load the module and link.  Before going to the file system,
	 * scan the list of modules loaded by the boot loader.
	 */
	TAILQ_FOREACH(mod, &module_bootlist, mod_chain) {
		if (strcmp(mod->mod_info->mi_name, name) == 0) {
			TAILQ_REMOVE(&module_bootlist, mod, mod_chain);
			break;
		}
	}
	if (mod != NULL) {
		TAILQ_INSERT_TAIL(pending, mod, mod_chain);
	} else {
		/*
		 * Check to see if module is already present.
		 */
		mod = module_lookup(name);
		if (mod != NULL) {
			if (modp != NULL) {
				*modp = mod;
			}
			module_print("%s module `%s' already loaded",
			    isdep ? "Dependent" : "Requested", name);
			SLIST_REMOVE_HEAD(&pend_stack, pe_entry);
			return EEXIST;
		}

		mod = module_newmodule(MODULE_SOURCE_FILESYS);
		if (mod == NULL) {
			module_error("Out of memory for `%s'", name);
			SLIST_REMOVE_HEAD(&pend_stack, pe_entry);
			return ENOMEM;
		}

		error = module_load_vfs_vec(name, flags, autoload, mod,
					    &filedict);
		if (error != 0) {
#ifdef DEBUG
			/*
			 * The exec class of modules contains a list of
			 * modules that is the union of all the modules
			 * available for each architecture, so we don't
			 * print an error if they are missing.
			 */
			if ((modclass != MODULE_CLASS_EXEC || error != ENOENT)
			    && root_device != NULL)
				module_error("module_load_vfs_vec() failed "
				    "for `%s', error %d", name, error);
			else
#endif
				module_print("module_load_vfs_vec() failed "
				    "for `%s', error %d", name, error);
			SLIST_REMOVE_HEAD(&pend_stack, pe_entry);
			module_free(mod);
			return error;
		}
		TAILQ_INSERT_TAIL(pending, mod, mod_chain);

		error = module_fetch_info(mod);
		if (error != 0) {
			module_error("Cannot fetch info for `%s', error %d",
			    name, error);
			goto fail;
		}
	}

	/*
	 * Check compatibility.
	 */
	mi = mod->mod_info;
	if (strnlen(mi->mi_name, MAXMODNAME) >= MAXMODNAME) {
		error = EINVAL;
		module_error("Module name `%s' longer than %d", mi->mi_name,
		    MAXMODNAME);
		goto fail;
	}
	if (mi->mi_class <= MODULE_CLASS_ANY ||
	    mi->mi_class >= MODULE_CLASS_MAX) {
		error = EINVAL;
		module_error("Module `%s' has invalid class %d",
		    mi->mi_name, mi->mi_class);
		    goto fail;
	}
	if (!module_compatible(mi->mi_version, __NetBSD_Version__)) {
		module_error("Module `%s' built for `%d', system `%d'",
		    mi->mi_name, mi->mi_version, __NetBSD_Version__);
		if (ISSET(flags, MODCTL_LOAD_FORCE)) {
			module_error("Forced load, system may be unstable");
		} else {
			error = EPROGMISMATCH;
			goto fail;
		}
	}

	/*
	 * If a specific kind of module was requested, ensure that we have
	 * a match.
	 */
	if (!MODULE_CLASS_MATCH(mi, modclass)) {
		module_incompat(mi, modclass);
		error = ENOENT;
		goto fail;
	}

	/*
	 * If loading a dependency, `name' is a plain module name.
	 * The name must match.
	 */
	if (isdep && strcmp(mi->mi_name, name) != 0) {
		module_error("Dependency name mismatch (`%s' != `%s')",
		    name, mi->mi_name);
		error = ENOENT;
		goto fail;
	}

	/*
	 * If we loaded a module from the filesystem, check the actual
	 * module name (from the modinfo_t) to ensure another module
	 * with the same name doesn't already exist.  (There's no
	 * guarantee the filename will match the module name, and the
	 * dup-symbols check may not be sufficient.)
	 */
	if (mod->mod_source == MODULE_SOURCE_FILESYS) {
		mod2 = module_lookup(mod->mod_info->mi_name);
		if ( mod2 && mod2 != mod) {
			module_error("Module with name `%s' already loaded",
			    mod2->mod_info->mi_name);
			error = EEXIST;
			if (modp != NULL)
				*modp = mod2;
			goto fail;
		}
	}

	/*
	 * Block circular dependencies.
	 */
	TAILQ_FOREACH(mod2, pending, mod_chain) {
		if (mod == mod2) {
			continue;
		}
		if (strcmp(mod2->mod_info->mi_name, mi->mi_name) == 0) {
			error = EDEADLK;
			module_error("Circular dependency detected for `%s'",
			    mi->mi_name);
			goto fail;
		}
	}

	/*
	 * Now try to load any requisite modules.
	 */
	if (mi->mi_required != NULL) {
		mod->mod_arequired = 0;
		for (s = mi->mi_required; *s != '\0'; s = p) {
			if (*s == ',')
				s++;
			p = s;
			while (*p != '\0' && *p != ',')
				p++;
			len = p - s + 1;
			if (len >= MAXMODNAME) {
				error = EINVAL;
				module_error("Required module name `%s' "
				    "longer than %d", mi->mi_required,
				    MAXMODNAME);
				goto fail;
			}
			strlcpy(buf, s, len);
			if (buf[0] == '\0')
				break;
			alloc_required(mod);
			if (strcmp(buf, mi->mi_name) == 0) {
				error = EDEADLK;
				module_error("Self-dependency detected for "
				   "`%s'", mi->mi_name);
				goto fail;
			}
			error = module_do_load(buf, true, flags, NULL,
			    &mod2, MODULE_CLASS_ANY, true);
			if (error != 0 && error != EEXIST) {
				module_error("Recursive load failed for `%s' "
				    "(`%s' required), error %d", mi->mi_name,
				    buf, error);
				goto fail;
			}
			(*mod->mod_required)[mod->mod_nrequired++] = mod2;
		}
	}

	/*
	 * We loaded all needed modules successfully: perform global
	 * relocations and initialize.
	 */
	{
		char xname[MAXMODNAME];

		/*
		 * In case of error the entire module is gone, so we
		 * need to save its name for possible error report.
		 */

		strlcpy(xname, mi->mi_name, MAXMODNAME);
		error = kobj_affix(mod->mod_kobj, mi->mi_name);
		if (error != 0) {
			module_error("Unable to affix module `%s', error %d",
			    xname, error);
			goto fail2;
		}
	}

	if (filedict) {
		if (!module_merge_dicts(filedict, props)) {
			module_error("Module properties failed for %s", name);
			error = EINVAL;
			goto fail;
		}
	}

	prev_active = module_active;
	module_active = mod;

	/*
	 * Note that we handle sysctl and evcnt setup _before_ we
	 * initialize the module itself.  This maintains a consistent
	 * order between built-in and run-time-loaded modules.  If
	 * initialization then fails, we'll need to undo these, too.
	 */
	module_load_sysctl(mod);	/* Set-up module's sysctl if any */
	module_load_evcnt(mod);		/* Attach any static evcnt needed */


	error = (*mi->mi_modcmd)(MODULE_CMD_INIT, filedict ? filedict : props);
	module_active = prev_active;
	if (filedict) {
		prop_object_release(filedict);
		filedict = NULL;
	}
	if (error != 0) {
		module_error("modcmd(CMD_INIT) failed for `%s', error %d",
		    mi->mi_name, error);
		goto fail3;
	}

	/*
	 * If a recursive load already added a module with the same
	 * name, abort.
	 */
	mod2 = module_lookup(mi->mi_name);
	if (mod2 && mod2 != mod) {
		module_error("Recursive load causes duplicate module `%s'",
		    mi->mi_name);
		error = EEXIST;
		goto fail1;
	}

	/*
	 * Good, the module loaded successfully.  Put it onto the
	 * list and add references to its requisite modules.
	 */
	TAILQ_REMOVE(pending, mod, mod_chain);
	module_enqueue(mod);
	if (modp != NULL) {
		*modp = mod;
	}
	if (autoload && module_autotime > 0) {
		/*
		 * Arrange to try unloading the module after
		 * a short delay unless auto-unload is disabled.
		 */
		mod->mod_autotime = time_second + module_autotime;
		SET(mod->mod_flags, MODFLG_AUTO_LOADED);
		module_thread_kick();
	}
	SLIST_REMOVE_HEAD(&pend_stack, pe_entry);
	module_print("Module `%s' loaded successfully", mi->mi_name);
	module_callback_load(mod);
	return 0;

 fail1:
	(*mi->mi_modcmd)(MODULE_CMD_FINI, NULL);
 fail3:
	/*
	 * If there were any registered SYSCTL_SETUP funcs, make sure
	 * we release the sysctl entries
	 */
	if (mod->mod_sysctllog) {
		sysctl_teardown(&mod->mod_sysctllog);
	}
	/* Also detach any static evcnt's */
	module_unload_evcnt(mod);
 fail:
	kobj_unload(mod->mod_kobj);
 fail2:
	if (filedict != NULL) {
		prop_object_release(filedict);
		filedict = NULL;
	}
	TAILQ_REMOVE(pending, mod, mod_chain);
	SLIST_REMOVE_HEAD(&pend_stack, pe_entry);
	module_free(mod);
	module_print("Load failed, error %d", error);
	return error;
}

/*
 * module_do_unload:
 *
 *	Helper routine: do the dirty work of unloading a module.
 */
static int
module_do_unload(const char *name, bool load_requires_force)
{
	module_t *mod, *prev_active;
	int error;
	u_int i;

	KASSERT(kernconfig_is_held());
	KASSERT(name != NULL);

	module_print("Unload requested for `%s' (requires_force %s)", name,
	    load_requires_force ? "TRUE" : "FALSE");
	mod = module_lookup(name);
	if (mod == NULL) {
		module_error("Module `%s' not found", name);
		return ENOENT;
	}
	if (mod->mod_refcnt != 0) {
		module_print("Module `%s' busy (%d refs)", name,
		    mod->mod_refcnt);
		return EBUSY;
	}

	/*
	 * Builtin secmodels are there to stay.
	 */
	if (mod->mod_source == MODULE_SOURCE_KERNEL &&
	    mod->mod_info->mi_class == MODULE_CLASS_SECMODEL) {
		module_print("Cannot unload built-in secmodel module `%s'",
		    name);
		return EPERM;
	}

	prev_active = module_active;
	module_active = mod;
	module_callback_unload(mod);

	/* let the module clean up after itself */
	error = (*mod->mod_info->mi_modcmd)(MODULE_CMD_FINI, NULL);

	/*
	 * If there were any registered SYSCTL_SETUP funcs, make sure
	 * we release the sysctl entries.  Same for static evcnt.
	 */
	if (error == 0) {
		if (mod->mod_sysctllog) {
			sysctl_teardown(&mod->mod_sysctllog);
		}
		module_unload_evcnt(mod);
	}
	module_active = prev_active;
	if (error != 0) {
		module_print("Could not unload module `%s' error=%d", name,
		    error);
		return error;
	}
	module_count--;
	TAILQ_REMOVE(&module_list, mod, mod_chain);
	for (i = 0; i < mod->mod_nrequired; i++) {
		(*mod->mod_required)[i]->mod_refcnt--;
	}
	module_print("Unloaded module `%s'", name);
	if (mod->mod_kobj != NULL) {
		kobj_unload(mod->mod_kobj);
	}
	if (mod->mod_source == MODULE_SOURCE_KERNEL) {
		if (mod->mod_required != NULL) {
			/*
			 * release "required" resources - will be re-parsed
			 * if the module is re-enabled
			 */
			kmem_free(mod->mod_required,
			    mod->mod_arequired * sizeof(module_t *));
			mod->mod_nrequired = 0;
			mod->mod_arequired = 0;
			mod->mod_required = NULL;
		}
		if (load_requires_force)
			module_require_force(mod);
		TAILQ_INSERT_TAIL(&module_builtins, mod, mod_chain);
		module_builtinlist++;
	} else {
		module_free(mod);
	}
	module_gen++;

	return 0;
}

/*
 * module_prime:
 *
 *	Push a module loaded by the bootloader onto our internal
 *	list.
 */
int
module_prime(const char *name, void *base, size_t size)
{
	__link_set_decl(modules, modinfo_t);
	modinfo_t *const *mip;
	module_t *mod;
	int error;

	/* Check for module name same as a built-in module */

	__link_set_foreach(mip, modules) {
		if (*mip == &module_dummy)
			continue;
		if (strcmp((*mip)->mi_name, name) == 0) {
			module_error("Module `%s' pushed by boot loader "
			    "already exists", name);
			return EEXIST;
		}
	}

	/* Also eliminate duplicate boolist entries */

	TAILQ_FOREACH(mod, &module_bootlist, mod_chain) {
		if (strcmp(mod->mod_info->mi_name, name) == 0) {
			module_error("Duplicate bootlist entry for module "
			    "`%s'", name);
			return EEXIST;
		}
	}

	mod = module_newmodule(MODULE_SOURCE_BOOT);
	if (mod == NULL) {
		return ENOMEM;
	}

	error = kobj_load_mem(&mod->mod_kobj, name, base, size);
	if (error != 0) {
		module_free(mod);
		module_error("Unable to load `%s' pushed by boot loader, "
		    "error %d", name, error);
		return error;
	}
	error = module_fetch_info(mod);
	if (error != 0) {
		kobj_unload(mod->mod_kobj);
		module_free(mod);
		module_error("Unable to fetch_info for `%s' pushed by boot "
		    "loader, error %d", name, error);
		return error;
	}

	TAILQ_INSERT_TAIL(&module_bootlist, mod, mod_chain);

	return 0;
}

/*
 * module_fetch_into:
 *
 *	Fetch modinfo record from a loaded module.
 */
static int
module_fetch_info(module_t *mod)
{
	int error;
	void *addr;
	size_t size;

	/*
	 * Find module info record and check compatibility.
	 */
	error = kobj_find_section(mod->mod_kobj, "link_set_modules",
	    &addr, &size);
	if (error != 0) {
		module_error("`link_set_modules' section not present, "
		    "error %d", error);
		return error;
	}
	if (size != sizeof(modinfo_t **)) {
		if (size > sizeof(modinfo_t **) &&
		    (size % sizeof(modinfo_t **)) == 0) {
			module_error("`link_set_modules' section wrong size "
			    "(%zu different MODULE declarations?)",
			    size / sizeof(modinfo_t **));
		} else {
			module_error("`link_set_modules' section wrong size "
			    "(got %zu, wanted %zu)",
			    size, sizeof(modinfo_t **));
		}
		return ENOEXEC;
	}
	mod->mod_info = *(modinfo_t **)addr;

	return 0;
}

/*
 * module_find_section:
 *
 *	Allows a module that is being initialized to look up a section
 *	within its ELF object.
 */
int
module_find_section(const char *name, void **addr, size_t *size)
{

	KASSERT(kernconfig_is_held());
	KASSERT(module_active != NULL);

	return kobj_find_section(module_active->mod_kobj, name, addr, size);
}

/*
 * module_thread:
 *
 *	Automatically unload modules.  We try once to unload autoloaded
 *	modules after module_autotime seconds.  If the system is under
 *	severe memory pressure, we'll try unloading all modules, else if
 *	module_autotime is zero, we don't try to unload, even if the
 *	module was previously scheduled for unload.
 */
static void
module_thread(void *cookie)
{
	module_t *mod, *next;
	modinfo_t *mi;
	int error;

	for (;;) {
		kernconfig_lock();
		for (mod = TAILQ_FIRST(&module_list); mod != NULL; mod = next) {
			next = TAILQ_NEXT(mod, mod_chain);

			/* skip built-in modules */
			if (mod->mod_source == MODULE_SOURCE_KERNEL)
				continue;
			/* skip modules that weren't auto-loaded */
			if (!ISSET(mod->mod_flags, MODFLG_AUTO_LOADED))
				continue;

			if (uvm_availmem(false) < uvmexp.freemin) {
				module_thread_ticks = hz;
			} else if (module_autotime == 0 ||
				   mod->mod_autotime == 0) {
				continue;
			} else if (time_second < mod->mod_autotime) {
				module_thread_ticks = hz;
			    	continue;
			} else {
				mod->mod_autotime = 0;
			}

			/*
			 * Ask the module if it can be safely unloaded.
			 *
			 * - Modules which have been audited to be OK
			 *   with that will return 0.
			 *
			 * - Modules which have not been audited for
			 *   safe autounload will return ENOTTY.
			 *
			 *   => With kern.module.autounload_unsafe=1,
			 *      we treat ENOTTY as acceptance.
			 *
			 * - Some modules would ping-ping in and out
			 *   because their use is transient but often.
			 *   Example: exec_script.  Other modules may
			 *   still be in use.  These modules can
			 *   prevent autounload in all cases by
			 *   returning EBUSY or some other error code.
			 */
			mi = mod->mod_info;
			error = (*mi->mi_modcmd)(MODULE_CMD_AUTOUNLOAD, NULL);
			if (error == 0 ||
			    (error == ENOTTY && module_autounload_unsafe)) {
				module_print("Requesting autounload for"
				    "`%s'", mi->mi_name);
				(void)module_do_unload(mi->mi_name, false);
			} else
				module_print("Module `%s' declined to be "
				    "auto-unloaded error=%d", mi->mi_name,
				    error);
		}
		kernconfig_unlock();

		mutex_enter(&module_thread_lock);
		(void)cv_timedwait(&module_thread_cv, &module_thread_lock,
		    module_thread_ticks);
		module_thread_ticks = 0;
		mutex_exit(&module_thread_lock);
	}
}

/*
 * module_thread:
 *
 *	Kick the module thread into action, perhaps because the
 *	system is low on memory.
 */
void
module_thread_kick(void)
{

	mutex_enter(&module_thread_lock);
	module_thread_ticks = hz;
	cv_broadcast(&module_thread_cv);
	mutex_exit(&module_thread_lock);
}

#ifdef DDB
/*
 * module_whatis:
 *
 *	Helper routine for DDB.
 */
void
module_whatis(uintptr_t addr, void (*pr)(const char *, ...))
{
	module_t *mod;
	size_t msize;
	vaddr_t maddr;

	TAILQ_FOREACH(mod, &module_list, mod_chain) {
		if (mod->mod_kobj == NULL) {
			continue;
		}
		if (kobj_stat(mod->mod_kobj, &maddr, &msize) != 0)
			continue;
		if (addr < maddr || addr >= maddr + msize) {
			continue;
		}
		(*pr)("%p is %p+%zu, in kernel module `%s'\n",
		    (void *)addr, (void *)maddr,
		    (size_t)(addr - maddr), mod->mod_info->mi_name);
	}
}

/*
 * module_print_list:
 *
 *	Helper routine for DDB.
 */
void
module_print_list(void (*pr)(const char *, ...))
{
	const char *src;
	module_t *mod;
	size_t msize;
	vaddr_t maddr;

	(*pr)("%16s %16s %8s %8s\n", "NAME", "TEXT/DATA", "SIZE", "SOURCE");

	TAILQ_FOREACH(mod, &module_list, mod_chain) {
		switch (mod->mod_source) {
		case MODULE_SOURCE_KERNEL:
			src = "builtin";
			break;
		case MODULE_SOURCE_FILESYS:
			src = "filesys";
			break;
		case MODULE_SOURCE_BOOT:
			src = "boot";
			break;
		default:
			src = "unknown";
			break;
		}
		if (mod->mod_kobj == NULL) {
			maddr = 0;
			msize = 0;
		} else if (kobj_stat(mod->mod_kobj, &maddr, &msize) != 0)
			continue;
		(*pr)("%16s %16lx %8ld %8s\n", mod->mod_info->mi_name,
		    (long)maddr, (long)msize, src);
	}
}
#endif	/* DDB */

static bool
module_merge_dicts(prop_dictionary_t existing_dict,
		   const prop_dictionary_t new_dict)
{
	prop_dictionary_keysym_t props_keysym;
	prop_object_iterator_t props_iter;
	prop_object_t props_obj;
	const char *props_key;
	bool error;

	if (new_dict == NULL) {			/* nothing to merge */
		return true;
	}

	error = false;
	props_iter = prop_dictionary_iterator(new_dict);
	if (props_iter == NULL) {
		return false;
	}

	while ((props_obj = prop_object_iterator_next(props_iter)) != NULL) {
		props_keysym = (prop_dictionary_keysym_t)props_obj;
		props_key = prop_dictionary_keysym_value(props_keysym);
		props_obj = prop_dictionary_get_keysym(new_dict, props_keysym);
		if ((props_obj == NULL) || !prop_dictionary_set(existing_dict,
		    props_key, props_obj)) {
			error = true;
			goto out;
		}
	}
	error = false;

out:
	prop_object_iterator_release(props_iter);

	return !error;
}

/*
 * module_specific_key_create:
 *
 *	Create a key for subsystem module-specific data.
 */
specificdata_key_t
module_specific_key_create(specificdata_key_t *keyp, specificdata_dtor_t dtor)
{

	return specificdata_key_create(module_specificdata_domain, keyp, dtor);
}

/*
 * module_specific_key_delete:
 *
 *	Delete a key for subsystem module-specific data.
 */
void
module_specific_key_delete(specificdata_key_t key)
{

	return specificdata_key_delete(module_specificdata_domain, key);
}

/*
 * module_getspecific:
 *
 *	Return module-specific data corresponding to the specified key.
 */
void *
module_getspecific(module_t *mod, specificdata_key_t key)
{

	return specificdata_getspecific(module_specificdata_domain,
	    &mod->mod_sdref, key);
}

/*
 * module_setspecific:
 *
 *	Set module-specific data corresponding to the specified key.
 */
void
module_setspecific(module_t *mod, specificdata_key_t key, void *data)
{

	specificdata_setspecific(module_specificdata_domain,
	    &mod->mod_sdref, key, data);
}

/*
 * module_register_callbacks:
 *
 *	Register a new set of callbacks to be called on module load/unload.
 *	Call the load callback on each existing module.
 *	Return an opaque handle for unregistering these later.
 */
void *
module_register_callbacks(void (*load)(struct module *),
    void (*unload)(struct module *))
{
	struct module_callbacks *modcb;
	struct module *mod;

	modcb = kmem_alloc(sizeof(*modcb), KM_SLEEP);
	modcb->modcb_load = load;
	modcb->modcb_unload = unload;

	kernconfig_lock();
	TAILQ_INSERT_TAIL(&modcblist, modcb, modcb_list);
	TAILQ_FOREACH_REVERSE(mod, &module_list, modlist, mod_chain)
		load(mod);
	kernconfig_unlock();

	return modcb;
}

/*
 * module_unregister_callbacks:
 *
 *	Unregister a previously-registered set of module load/unload callbacks.
 *	Call the unload callback on each existing module.
 */
void
module_unregister_callbacks(void *opaque)
{
	struct module_callbacks *modcb;
	struct module *mod;

	modcb = opaque;
	kernconfig_lock();
	TAILQ_FOREACH(mod, &module_list, mod_chain)
		modcb->modcb_unload(mod);
	TAILQ_REMOVE(&modcblist, modcb, modcb_list);
	kernconfig_unlock();
	kmem_free(modcb, sizeof(*modcb));
}

/*
 * module_callback_load:
 *
 *	Helper routine: call all load callbacks on a module being loaded.
 */
static void
module_callback_load(struct module *mod)
{
	struct module_callbacks *modcb;

	TAILQ_FOREACH(modcb, &modcblist, modcb_list) {
		modcb->modcb_load(mod);
	}
}

/*
 * module_callback_unload:
 *
 *	Helper routine: call all unload callbacks on a module being unloaded.
 */
static void
module_callback_unload(struct module *mod)
{
	struct module_callbacks *modcb;

	TAILQ_FOREACH(modcb, &modcblist, modcb_list) {
		modcb->modcb_unload(mod);
	}
}
