/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * This file is part of the DAOS server. It implements the modular interface
 * to load server-side code on demand. DAOS modules are effectively dynamic
 * libraries loaded on-the-fly in the DAOS server via dlopen(3).
 */

#include <dlfcn.h>

#include <daos/daos_errno.h>
#include <daos/daos_common.h>
#include <daos/daos_list.h>

#include <daos_srv/daos_server.h>

#include "dss_internal.h"

/* Loaded module instance */
struct srv_mod {
	/* library handle grabbed with dlopen(3) */
	void			*sm_hdl;
	/* module interface looked up via dlsym(3) */
	struct dss_module	*sm_mod;
	/* linked list of loaded module */
	daos_list_t		 sm_lk;
};

/* Track list of loaded modules */
DAOS_LIST_HEAD(srv_mod_list);

static struct srv_mod *
dss_module_search(std::string modname, bool unlink)
{
	daos_list_t	*tmp;
	daos_list_t	*pos;

	/* search for the module in the loaded module list */
	daos_list_for_each_safe(pos, tmp, &srv_mod_list) {
		struct srv_mod	*mod;

		mod = daos_list_entry(pos, struct srv_mod, sm_lk);
		if (strcmp(mod->sm_mod->sm_name, modname.c_str()) == 0) {
			if (unlink)
				daos_list_del_init(pos);
			return mod;
		}
	}

	/* not found */
	return NULL;
}

int
dss_module_load(std::string modname)
{
	struct srv_mod	*mod;
	std::string	 name;
	void		*handle;
	char		*err;
	int		 rc;

	/* load the dynamic library */
	name = "lib" + modname + ".so";
	handle = dlopen(name.c_str(), RTLD_LAZY);
	if (handle == NULL) {
		D_ERROR("cannot load %s\n", modname.c_str());
		return -DER_INVAL;
	}

	/* allocate data structure to track this module instance */
	D_ALLOC_PTR(mod);
	if (!mod) {
		rc = -DER_NOMEM;
		goto err_hdl;
	}

	mod->sm_hdl = handle;

	/* clear existing errors, if any */
	dlerror();

	/* lookup the dss_module structure defining the module interface */
	name = modname + "_module";
	mod->sm_mod = (struct dss_module *)dlsym(handle, name.c_str());

	/* check for errors */
	err = dlerror();
	if (err != NULL) {
		D_ERROR("failed to load %s: %s\n", modname.c_str(), err);
		rc = -DER_INVAL;
		goto err_mod;
	}

	/* check module name is consistent */
	if (strcmp(mod->sm_mod->sm_name, modname.c_str()) != 0) {
		D_ERROR("inconsistent module name %s != %s\n", modname.c_str(),
			mod->sm_mod->sm_name);
		rc = -DER_INVAL;
		goto err_hdl;
	}

	/* initialize the module */
	rc = mod->sm_mod->sm_init();
	if (rc) {
		D_ERROR("failed to init %s: %d\n", modname.c_str(), rc);
		rc = -DER_INVAL;
		goto err_mod;
	}

	/* module successfully loaded, add it to the tracking list */
	daos_list_add_tail(&mod->sm_lk, &srv_mod_list);
	return 0;

err_mod:
	D_FREE_PTR(mod);
err_hdl:
	dlclose(handle);
	return rc;
}

int
dss_module_unload(std::string modname)
{
	struct srv_mod	*mod;
	int		 rc;

	/* lookup the module from the loaded module list */
	mod = dss_module_search(modname, true);

	if (mod == NULL)
		/* module not found ... */
		return -DER_ENOENT;

	/* finalize the module */
	rc = mod->sm_mod->sm_fini();

	/* close the library handle */
	dlclose(mod->sm_hdl);

	/* free memory used to track this module instance */
	D_FREE_PTR(mod);

	return rc;
}

int
dss_module_init(void)
{
	return 0;
}

int
dss_module_fini(bool force)
{
	return 0;
}
