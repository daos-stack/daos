/**
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of the DAOS server. It implements the modular interface
 * to load server-side code on demand. DAOS modules are effectively dynamic
 * libraries loaded on-the-fly in the DAOS server via dlopen(3).
 */
#define D_LOGFAC       DD_FAC(server)

#include <dlfcn.h>

#include <daos_errno.h>
#include <daos/common.h>
#include <gurt/list.h>
#include <daos/rpc.h>
#include "drpc_handler.h"
#include "srv_internal.h"

/* Loaded module instance */
struct loaded_mod {
	/* library handle grabbed with dlopen(3) */
	void			*lm_hdl;
	/* module interface looked up via dlsym(3) */
	struct dss_module	*lm_dss_mod;
	/* linked list of loaded module */
	d_list_t		 lm_lk;
	/** Module is initialized */
	bool			 lm_init;
};

/* Track list of loaded modules */
D_LIST_HEAD(loaded_mod_list);
pthread_mutex_t loaded_mod_list_lock = PTHREAD_MUTEX_INITIALIZER;

/* Define an array for faster accessing the module by mod_id */
static struct dss_module	*dss_modules[DAOS_MAX_MODULE];

struct dss_module *
dss_module_get(int mod_id)
{
	/* If the mod_id comes from CART initialized RPC,
	 * let's return NULL for now.
	 */
	if (mod_id >= DAOS_MAX_MODULE)
		return NULL;

	return dss_modules[mod_id];
}

static struct loaded_mod *
dss_module_search(const char *modname)
{
	struct loaded_mod *mod;

	/* search for the module in the loaded module list */
	d_list_for_each_entry(mod, &loaded_mod_list, lm_lk) {
		if (strcmp(mod->lm_dss_mod->sm_name, modname) == 0)
			return mod;
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
	handle = dlopen(name, RTLD_LAZY | RTLD_GLOBAL);
	if (handle == NULL) {
		D_ERROR("cannot load %s: %s\n", name, dlerror());
		return -DER_INVAL;
	}

	/* allocate data structure to track this module instance */
	D_ALLOC_PTR(lmod);
	if (!lmod)
		D_GOTO(err_hdl, rc = -DER_NOMEM);

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
		D_GOTO(err_lmod, rc = -DER_INVAL);
	}
	lmod->lm_dss_mod = smod;

	/* check module name is consistent */
	if (strcmp(smod->sm_name, modname) != 0) {
		D_ERROR("inconsistent module name %s != %s\n", modname,
			smod->sm_name);
		D_GOTO(err_lmod, rc = -DER_INVAL);
	}

	/* module successfully loaded (not yet initialized), add it to the
	 * tracking list
	 */
	D_MUTEX_LOCK(&loaded_mod_list_lock);
	d_list_add_tail(&lmod->lm_lk, &loaded_mod_list);
	dss_modules[smod->sm_mod_id] = smod;
	D_MUTEX_UNLOCK(&loaded_mod_list_lock);

	return 0;
err_lmod:
	D_FREE(lmod);
err_hdl:
	dlclose(handle);
	return rc;
}

static int
dss_module_init_one(struct loaded_mod *lmod, uint64_t *mod_facs)
{
	struct dss_module	*smod;
	int			rc = 0;

	smod = lmod->lm_dss_mod;
	/* initialize the module */
	rc = smod->sm_init();
	if (rc) {
		D_ERROR("failed to init %s: "DF_RC"\n", smod->sm_name,
			DP_RC(rc));
		D_GOTO(err_lmod, rc = -DER_INVAL);
	}

	if (smod->sm_key != NULL)
		dss_register_key(smod->sm_key);

	/* register RPC handlers */
	rc = daos_rpc_register(smod->sm_proto_fmt, smod->sm_cli_count,
			       smod->sm_handlers, smod->sm_mod_id);
	if (rc) {
		D_ERROR("failed to register RPC for %s: "DF_RC"\n",
			smod->sm_name, DP_RC(rc));
		D_GOTO(err_mod_init, rc);
	}

	/* register dRPC handlers */
	rc = drpc_hdlr_register_all(smod->sm_drpc_handlers);
	if (rc) {
		D_ERROR("failed to register dRPC for %s: "DF_RC"\n",
			smod->sm_name, DP_RC(rc));
		D_GOTO(err_rpc, rc);
	}

	if (mod_facs != NULL)
		*mod_facs = smod->sm_facs;

	lmod->lm_init = true;

	return 0;

err_rpc:
	daos_rpc_unregister(smod->sm_proto_fmt);
err_mod_init:
	dss_unregister_key(smod->sm_key);
	smod->sm_fini();
err_lmod:
	d_list_del_init(&lmod->lm_lk);
	dlclose(lmod->lm_hdl);
	D_FREE(lmod);
	return rc;
}

static int
dss_module_unload_internal(struct loaded_mod *lmod)
{
	struct dss_module	*smod = lmod->lm_dss_mod;
	int			 rc = 0;

	if (lmod->lm_init == false)
		goto close_mod;
	/* unregister RPC handlers */
	rc = daos_rpc_unregister(smod->sm_proto_fmt);
	if (rc) {
		D_ERROR("failed to unregister RPC "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	rc = drpc_hdlr_unregister_all(smod->sm_drpc_handlers);
	if (rc != 0) {
		D_ERROR("Failed to unregister dRPC "DF_RC"\n", DP_RC(rc));
	}

	dss_unregister_key(smod->sm_key);

	dss_modules[smod->sm_mod_id] = NULL;
	/* finalize the module */
	rc = smod->sm_fini();
	if (rc) {
		D_ERROR("module finalization failed for: "DF_RC"\n", DP_RC(rc));
		return rc;

	}

close_mod:
	/* close the library handle */
	dlclose(lmod->lm_hdl);

	return rc;
}

int
dss_module_init_all(uint64_t *mod_facs)
{
	struct loaded_mod	*lmod;
	struct loaded_mod	*tmp;
	uint64_t		 fac;
	int			 rc = 0;

	/* lookup the module from the loaded module list */
	D_MUTEX_LOCK(&loaded_mod_list_lock);
	d_list_for_each_entry_safe(lmod, tmp, &loaded_mod_list, lm_lk) {
		if (rc != 0) {
			dss_module_unload_internal(lmod);
			d_list_del_init(&lmod->lm_lk);
			D_FREE(lmod);
			continue;
		}
		fac = 0;
		rc = dss_module_init_one(lmod, &fac);
		*mod_facs |= fac;
	}
	D_MUTEX_UNLOCK(&loaded_mod_list_lock);

	return rc;
}


int
dss_module_unload(const char *modname)
{
	struct loaded_mod	*lmod;

	/* lookup the module from the loaded module list */
	D_MUTEX_LOCK(&loaded_mod_list_lock);
	lmod = dss_module_search(modname);
	if (lmod == NULL) {
		D_MUTEX_UNLOCK(&loaded_mod_list_lock);
		/* module not found ... */
		return -DER_ENOENT;
	}
	d_list_del_init(&lmod->lm_lk);
	D_MUTEX_UNLOCK(&loaded_mod_list_lock);

	dss_module_unload_internal(lmod);

	/* free memory used to track this module instance */
	D_FREE(lmod);

	return 0;
}

int
dss_module_setup_all(void)
{
	struct loaded_mod      *mod;
	int			rc = 0;

	D_MUTEX_LOCK(&loaded_mod_list_lock);
	d_list_for_each_entry(mod, &loaded_mod_list, lm_lk) {
		struct dss_module *m = mod->lm_dss_mod;

		if (m->sm_setup == NULL)
			continue;
		rc = m->sm_setup();
		if (rc != 0) {
			D_ERROR("failed to set up module %s: %d\n", m->sm_name,
				rc);
			break;
		}
	}
	D_MUTEX_UNLOCK(&loaded_mod_list_lock);
	return rc;
}

int
dss_module_cleanup_all(void)
{
	struct loaded_mod      *mod;
	int			rc = 0;

	D_INFO("Cleaning up all loaded modules\n");
	D_MUTEX_LOCK(&loaded_mod_list_lock);
	D_INFO("Iterating through loaded modules list\n");
	d_list_for_each_entry_reverse(mod, &loaded_mod_list, lm_lk) {
		struct dss_module *m = mod->lm_dss_mod;

		if (m->sm_cleanup == NULL) {
			D_INFO("Module %s: no sm_cleanup func\n", m->sm_name);
			continue;
		}
		D_INFO("Module %s: invoke sm_cleanup func\n", m->sm_name);
		rc = m->sm_cleanup();
		if (rc != 0) {
			D_ERROR("failed to clean up module %s: "DF_RC"\n",
				m->sm_name, DP_RC(rc));
			break;
		}
		D_INFO("Module %s: cleaned up\n", m->sm_name);
	}
	D_INFO("Done iterating through loaded modules list\n");
	D_MUTEX_UNLOCK(&loaded_mod_list_lock);
	D_INFO("Done cleaning up all loaded modules\n");
	return rc;
}

int
dss_module_init(void)
{
	return drpc_hdlr_init();
}

int
dss_module_fini(bool force)
{
	return drpc_hdlr_fini();
}

void
dss_module_unload_all(void)
{
	struct loaded_mod	*mod;
	struct loaded_mod	*tmp;
	struct d_list_head	destroy_list;

	D_INIT_LIST_HEAD(&destroy_list);
	D_MUTEX_LOCK(&loaded_mod_list_lock);
	d_list_for_each_entry_safe(mod, tmp, &loaded_mod_list, lm_lk) {
		d_list_del_init(&mod->lm_lk);
		d_list_add(&mod->lm_lk, &destroy_list);
	}
	D_MUTEX_UNLOCK(&loaded_mod_list_lock);

	d_list_for_each_entry_safe(mod, tmp, &destroy_list, lm_lk) {
		d_list_del_init(&mod->lm_lk);

		D_FREE(mod);
	}
}
