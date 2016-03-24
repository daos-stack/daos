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

#include "dss_internal.h"

/* Loaded module instance */
struct loaded_mod {
	/* library handle grabbed with dlopen(3) */
	void			*lm_hdl;
	/* module interface looked up via dlsym(3) */
	struct dss_module	*lm_dss_mod;
	/* linked list of loaded module */
	daos_list_t		 lm_lk;
};

/* Track list of loaded modules */
DAOS_LIST_HEAD(loaded_mod_list);

static struct loaded_mod *
dss_module_search(const char *modname, bool unlink)
{
	daos_list_t	*tmp;
	daos_list_t	*pos;

	/* search for the module in the loaded module list */
	daos_list_for_each_safe(pos, tmp, &loaded_mod_list) {
		struct loaded_mod	*mod;

		mod = daos_list_entry(pos, struct loaded_mod, lm_lk);
		if (strcmp(mod->lm_dss_mod->sm_name, modname) == 0) {
			if (unlink)
				daos_list_del_init(pos);
			return mod;
		}
	}

	/* not found */
	return NULL;
}

#define DSS_MODNAME_MAX_LEN	32

int
dss_module_load(const char *modname)
{
	struct loaded_mod	*lmod;
	struct dss_module	*smod;
	char			 name[DSS_MODNAME_MAX_LEN + 8];
	void			*handle;
	char			*err;
	int			 rc;

	if (strlen(modname) > DSS_MODNAME_MAX_LEN) {
		D_ERROR("modname %s is too long > %d\n",
			modname, DSS_MODNAME_MAX_LEN);
		return -DER_INVAL;
	}

	/* load the dynamic library */
	sprintf(name, "lib%s.so", modname);
	handle = dlopen(name, RTLD_LAZY);
	if (handle == NULL) {
		D_ERROR("cannot load %s\n", modname);
		return -DER_INVAL;
	}

	/* allocate data structure to track this module instance */
	D_ALLOC_PTR(lmod);
	if (!lmod) {
		rc = -DER_NOMEM;
		goto err_hdl;
	}

	lmod->lm_hdl = handle;

	/* clear existing errors, if any */
	dlerror();

	/* lookup the dss_module structure defining the module interface */
	sprintf(name, "%s_module", modname);
	smod = (struct dss_module *)dlsym(handle, name);

	/* check for errors */
	err = dlerror();
	if (err != NULL) {
		D_ERROR("failed to load %s: %s\n", modname, err);
		rc = -DER_INVAL;
		goto err_lmod;
	}
	lmod->lm_dss_mod = smod;

	/* check module name is consistent */
	if (strcmp(smod->sm_name, modname) != 0) {
		D_ERROR("inconsistent module name %s != %s\n", modname,
			smod->sm_name);
		rc = -DER_INVAL;
		goto err_hdl;
	}

	/* initialize the module */
	rc = smod->sm_init();
	if (rc) {
		D_ERROR("failed to init %s: %d\n", modname, rc);
		rc = -DER_INVAL;
		goto err_lmod;
	}

	/* register client RPC handlers */
	rc = dss_rpc_register(smod->sm_cl_hdlrs, smod->sm_mod_id, 0);
	if (rc) {
		D_ERROR("failed to register client RPC for %s: %d\n",
			modname, rc);
		goto err_mod_init;
	}

	/* register server RPC handlers */
	rc = dss_rpc_register(smod->sm_srv_hdlrs, smod->sm_mod_id, 1);
	if (rc) {
		D_ERROR("failed to register srv RPC for %s: %d\n",
			modname, rc);
		goto err_cl_rpc;
	}

	/* module successfully loaded, add it to the tracking list */
	daos_list_add_tail(&lmod->lm_lk, &loaded_mod_list);
	return 0;

err_cl_rpc:
	dss_rpc_unregister(smod->sm_cl_hdlrs);
err_mod_init:
	smod->sm_fini();
err_lmod:
	D_FREE_PTR(lmod);
err_hdl:
	dlclose(handle);
	return rc;
}

int
dss_module_unload(const char *modname)
{
	struct loaded_mod	*lmod;
	struct dss_module	*smod;
	int			 rc;

	/* lookup the module from the loaded module list */
	lmod = dss_module_search(modname, true);

	if (lmod == NULL)
		/* module not found ... */
		return -DER_ENOENT;

	smod = lmod->lm_dss_mod;

	/* unregister client RPC handlers */
	rc = dss_rpc_unregister(smod->sm_cl_hdlrs);
	if (rc) {
		D_ERROR("failed to unregister client RPC for %s: %d\n",
			modname, rc);
		return rc;
	}

	/* unregister server RPC handlers */
	rc = dss_rpc_unregister(smod->sm_srv_hdlrs);
	if (rc) {
		D_ERROR("failed to register srv RPC for %s: %d\n",
			modname, rc);
		return rc;
	}

	/* finalize the module */
	rc = smod->sm_fini();
	if (rc) {
		D_ERROR("module finalization failed for %s: %d\n",
			modname, rc);
		return rc;

	}

	/* close the library handle */
	dlclose(lmod->lm_hdl);

	/* free memory used to track this module instance */
	D_FREE_PTR(lmod);

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
