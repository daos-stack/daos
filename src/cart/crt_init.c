/*
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of CaRT. It implements CaRT init and finalize related
 * APIs/handling.
 */

#include <malloc.h>
#include <sys/mman.h>
#include "crt_internal.h"

struct crt_gdata crt_gdata;
static volatile int   gdata_init_flag;
struct crt_plugin_gdata crt_plugin_gdata;

static void
dump_envariables(void)
{
	int	i;
	char	*val;
	char	*envars[] = {"CRT_PHY_ADDR_STR", "D_LOG_STDERR_IN_LOG",
		"D_LOG_FILE", "D_LOG_FILE_APPEND_PID", "D_LOG_MASK", "DD_MASK",
		"DD_STDERR", "DD_SUBSYS", "CRT_TIMEOUT", "CRT_ATTACH_INFO_PATH",
		"OFI_PORT", "OFI_INTERFACE", "OFI_DOMAIN", "CRT_CREDIT_EP_CTX",
		"CRT_CTX_SHARE_ADDR", "CRT_CTX_NUM", "D_FI_CONFIG",
		"FI_UNIVERSE_SIZE", "CRT_DISABLE_MEM_PIN",
		"FI_OFI_RXM_USE_SRX" };

	D_DEBUG(DB_ALL, "-- ENVARS: --\n");
	for (i = 0; i < ARRAY_SIZE(envars); i++) {
		val = getenv(envars[i]);
		D_DEBUG(DB_ALL, "%s = %s\n", envars[i], val);
	}
}

/* Workaround for CART-890 */
static int
mem_pin_workaround(void)
{
	int crt_rc = 0;
	int rc = 0;

	/* Prevent malloc from releasing memory via sbrk syscall */
	rc = mallopt(M_TRIM_THRESHOLD, -1);
	if (rc != 1) {
		D_ERROR("Failed to disable malloc trim: %d\n", errno);
		D_GOTO(exit, crt_rc = -DER_MISC);
	}

	/* Disable fastbins; this option is not available on all systems */
	rc = mallopt(M_MXFAST, 0);
	if (rc != 1)
		D_WARN("Failed to disable malloc fastbins: %d (%s)\n",
			errno, strerror(errno));

	D_DEBUG(DB_ALL, "Memory pinning workaround enabled\n");
exit:
	return crt_rc;
}


/* first step init - for initializing crt_gdata */
static int data_init(int server, crt_init_options_t *opt)
{
	uint32_t	timeout;
	uint32_t	credits;
	bool		share_addr = false;
	uint32_t	ctx_num = 1;
	uint32_t	fi_univ_size = 0;
	uint32_t	mem_pin_disable = 0;
	uint64_t	start_rpcid;
	int		rc = 0;

	D_DEBUG(DB_ALL, "initializing crt_gdata...\n");

	dump_envariables();

	/*
	 * avoid size mis-matching between client/server side
	 * /see crt_proc_uuid_t().
	 */
	D_CASSERT(sizeof(uuid_t) == 16);

	D_INIT_LIST_HEAD(&crt_gdata.cg_ctx_list);

	rc = D_RWLOCK_INIT(&crt_gdata.cg_rwlock, NULL);
	if (rc != 0) {
		D_ERROR("Failed to init cg_rwlock\n");
		D_GOTO(exit, rc);
	}

	crt_gdata.cg_ctx_num = 0;
	crt_gdata.cg_refcount = 0;
	crt_gdata.cg_inited = 0;
	crt_gdata.cg_na_plugin = CRT_NA_OFI_SOCKETS;
	crt_gdata.cg_sep_mode = false;

	srand(d_timeus_secdiff(0) + getpid());
	start_rpcid = ((uint64_t)rand()) << 32;

	crt_gdata.cg_rpcid = start_rpcid;

	D_DEBUG(DB_ALL, "Starting RPCID 0x%lx\n", start_rpcid);

	/* Apply CART-890 workaround for server side only */
	if (server) {
		d_getenv_int("CRT_DISABLE_MEM_PIN", &mem_pin_disable);
		if (mem_pin_disable == 0) {
			rc = mem_pin_workaround();
			if (rc != 0)
				D_GOTO(exit, rc);
		}
	}

	timeout = 0;

	if (opt && opt->cio_crt_timeout != 0)
		timeout = opt->cio_crt_timeout;
	else
		d_getenv_int("CRT_TIMEOUT", &timeout);

	if (timeout == 0 || timeout > 3600)
		crt_gdata.cg_timeout = CRT_DEFAULT_TIMEOUT_S;
	else
		crt_gdata.cg_timeout = timeout;

	D_DEBUG(DB_ALL, "set the global timeout value as %d second.\n",
		crt_gdata.cg_timeout);

	/* Override defaults and environment if option is set */
	if (opt && opt->cio_use_credits) {
		credits = opt->cio_ep_credits;
	} else {
		credits = CRT_DEFAULT_CREDITS_PER_EP_CTX;
		d_getenv_int("CRT_CREDIT_EP_CTX", &credits);
	}

	/* This is a workaround for CART-871 if universe size is not set */
	d_getenv_int("FI_UNIVERSE_SIZE", &fi_univ_size);
	if (fi_univ_size == 0) {
		D_WARN("FI_UNIVERSE_SIZE was not set; setting to 2048\n");
		setenv("FI_UNIVERSE_SIZE", "2048", 1);
	}

	if (credits == 0) {
		D_DEBUG(DB_ALL, "CRT_CREDIT_EP_CTX set as 0, flow control "
			"disabled.\n");
	} else if (credits > CRT_MAX_CREDITS_PER_EP_CTX) {
		D_DEBUG(DB_ALL, "ENV CRT_CREDIT_EP_CTX's value %d exceed max "
			"allowed value, use %d for flow control.\n",
			credits, CRT_MAX_CREDITS_PER_EP_CTX);
		credits = CRT_MAX_CREDITS_PER_EP_CTX;
	} else {
		D_DEBUG(DB_ALL, "CRT_CREDIT_EP_CTX set as %d for flow "
			"control.\n", credits);
	}
	crt_gdata.cg_credit_ep_ctx = credits;
	D_ASSERT(crt_gdata.cg_credit_ep_ctx <= CRT_MAX_CREDITS_PER_EP_CTX);

	if (opt && opt->cio_sep_override) {
		if (opt->cio_use_sep) {
			crt_gdata.cg_sep_mode = true;
			D_DEBUG(DB_ALL, "crt_gdata.cg_sep_mode turned on.\n");
		}
		crt_gdata.cg_ctx_max_num = opt->cio_ctx_max_num;
	} else {
		d_getenv_bool("CRT_CTX_SHARE_ADDR", &share_addr);
		if (share_addr) {
			crt_gdata.cg_sep_mode = true;
			D_DEBUG(DB_ALL, "crt_gdata.cg_sep_mode turned on.\n");
		}

		d_getenv_int("CRT_CTX_NUM", &ctx_num);
		crt_gdata.cg_ctx_max_num = ctx_num;
	}
	D_DEBUG(DB_ALL, "set cg_sep_mode %d, cg_ctx_max_num %d.\n",
		crt_gdata.cg_sep_mode, crt_gdata.cg_ctx_max_num);
	if (crt_gdata.cg_sep_mode == false && crt_gdata.cg_ctx_max_num > 1)
		D_WARN("CRT_CTX_NUM has no effect because CRT_CTX_SHARE_ADDR "
		       "is not set or set to 0\n");

	gdata_init_flag = 1;
exit:
	return rc;
}

static int
crt_plugin_init(void)
{
	int i, rc;

	D_ASSERT(crt_plugin_gdata.cpg_inited == 0);

	/** init the lists */
	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		D_INIT_LIST_HEAD(&crt_plugin_gdata.cpg_prog_cbs[i]);
		rc = D_RWLOCK_INIT(&crt_plugin_gdata.cpg_prog_rwlock[i], NULL);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	D_INIT_LIST_HEAD(&crt_plugin_gdata.cpg_timeout_cbs);
	rc = D_RWLOCK_INIT(&crt_plugin_gdata.cpg_timeout_rwlock, NULL);
	if (rc != 0)
		D_GOTO(out_destroy_prog, rc);

	D_INIT_LIST_HEAD(&crt_plugin_gdata.cpg_event_cbs);
	rc = D_RWLOCK_INIT(&crt_plugin_gdata.cpg_event_rwlock, NULL);
	if (rc != 0)
		D_GOTO(out_destroy_timeout, rc);


	crt_plugin_gdata.cpg_inited = 1;
	D_GOTO(out, rc = 0);

	D_RWLOCK_DESTROY(&crt_plugin_gdata.cpg_event_rwlock);
out_destroy_timeout:
	D_RWLOCK_DESTROY(&crt_plugin_gdata.cpg_timeout_rwlock);
out_destroy_prog:
	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++)
		D_RWLOCK_DESTROY(&crt_plugin_gdata.cpg_prog_rwlock[i]);
out:
	return rc;
}

int
crt_init_opt(crt_group_id_t grpid, uint32_t flags, crt_init_options_t *opt)
{
	char		*addr_env;
	struct timeval	now;
	unsigned int	seed;
	const char	*path;
	bool		server;
	bool		provider_found = false;
	int		plugin_idx;
	int		rc = 0;

	server = flags & CRT_FLAG_BIT_SERVER;

	/* d_log_init is reference counted */
	rc = d_log_init();
	if (rc != 0) {
		D_PRINT_ERR("d_log_init failed, rc: %d.\n", rc);
		return rc;
	}

	crt_setup_log_fac();

	D_INFO("libcart version %s initializing\n", CART_VERSION);

	/* d_fault_inject_init() is reference counted */
	rc = d_fault_inject_init();
	if (rc != DER_SUCCESS) {
		D_ERROR("d_fault_inject_init() failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}

	if (grpid != NULL) {
		if (crt_validate_grpid(grpid) != 0) {
			D_ERROR("grpid contains invalid characters "
				"or is too long\n");
			D_GOTO(out, rc = -DER_INVAL);
		}

		if (strcmp(grpid, CRT_DEFAULT_GRPID) == 0) {
			D_ERROR("invalid client grpid (same as "
				"CRT_DEFAULT_GRPID).\n");
			D_GOTO(out, rc = -DER_INVAL);
		}
	}

	if (gdata_init_flag == 0) {
		rc = data_init(server, opt);
		if (rc != 0) {
			D_ERROR("data_init failed, rc(%d) - %s.\n",
				rc, strerror(rc));
			D_GOTO(out, rc = -rc);
		}
	}
	D_ASSERT(gdata_init_flag == 1);

	D_RWLOCK_WRLOCK(&crt_gdata.cg_rwlock);
	if (crt_gdata.cg_inited == 0) {
		/* feed a seed for pseudo-random number generator */
		gettimeofday(&now, NULL);
		seed = (unsigned int)(now.tv_sec * 1000000 + now.tv_usec);
		srandom(seed);

		crt_gdata.cg_server = server;
		crt_gdata.cg_auto_swim_disable =
			(flags & CRT_FLAG_BIT_AUTO_SWIM_DISABLE) ? 1 : 0;

		D_DEBUG(DB_ALL, "Server bit set to %d\n", server);
		D_DEBUG(DB_ALL, "Swim auto disable set to %d\n",
				crt_gdata.cg_auto_swim_disable);

		path = getenv("CRT_ATTACH_INFO_PATH");
		if (path != NULL && strlen(path) > 0) {
			rc = crt_group_config_path_set(path);
			if (rc != 0)
				D_ERROR("Got %s from ENV CRT_ATTACH_INFO_PATH, "
					"but crt_group_config_path_set failed "
					"rc: %d, ignore the ENV.\n", path, rc);
			else
				D_DEBUG(DB_ALL, "set group_config_path as "
					"%s.\n", path);
		}

		addr_env = (crt_phy_addr_t)getenv(CRT_PHY_ADDR_ENV);
		if (addr_env == NULL) {
			D_DEBUG(DB_ALL, "ENV %s not found.\n",
				CRT_PHY_ADDR_ENV);
			goto do_init;
		} else{
			D_DEBUG(DB_ALL, "EVN %s: %s.\n", CRT_PHY_ADDR_ENV,
				addr_env);
		}

		provider_found = false;
		for (plugin_idx = 0; crt_na_dict[plugin_idx].nad_str != NULL;
		     plugin_idx++) {
			if (!strncmp(addr_env, crt_na_dict[plugin_idx].nad_str,
				strlen(crt_na_dict[plugin_idx].nad_str) + 1)) {
				crt_gdata.cg_na_plugin =
					crt_na_dict[plugin_idx].nad_type;
				provider_found = true;
				break;
			}
		}

		if (!provider_found) {
			D_ERROR("Requested provider %s not found\n", addr_env);
			D_GOTO(out, rc = -DER_NONEXIST);
		}
do_init:
		/* Print notice that "ofi+verbs" is legacy */
		if (crt_gdata.cg_na_plugin == CRT_NA_OFI_VERBS) {
			D_ERROR("\"ofi+verbs\" is no longer supported. "
				"Use \"ofi+verbs;ofi_rxm\" instead for %s env",
				CRT_PHY_ADDR_ENV);
			D_GOTO(out, rc = -DER_INVAL);
		}

		/* the verbs provider only works with regular EP */
		if ((crt_gdata.cg_na_plugin == CRT_NA_OFI_VERBS_RXM ||
		     crt_gdata.cg_na_plugin == CRT_NA_OFI_VERBS ||
		     crt_gdata.cg_na_plugin == CRT_NA_OFI_TCP_RXM) &&
		    crt_gdata.cg_sep_mode) {
			D_WARN("set CRT_CTX_SHARE_ADDR as 1 is invalid "
			       "for current provider, ignore it.\n");
			crt_gdata.cg_sep_mode = false;
		}

		if (crt_gdata.cg_na_plugin == CRT_NA_OFI_VERBS_RXM ||
		    crt_gdata.cg_na_plugin == CRT_NA_OFI_TCP_RXM) {
			char *srx_env;

			srx_env = getenv("FI_OFI_RXM_USE_SRX");
			if (srx_env == NULL) {
				D_WARN("FI_OFI_RXM_USE_SRX not set, set=1\n");
				setenv("FI_OFI_RXM_USE_SRX", "1", true);
			}
		}

		if (crt_gdata.cg_na_plugin == CRT_NA_OFI_PSM2) {
			setenv("FI_PSM2_NAME_SERVER", "1", true);
			D_DEBUG(DB_ALL, "Setting FI_PSM2_NAME_SERVER to 1\n");
		}
		if (crt_na_type_is_ofi(crt_gdata.cg_na_plugin)) {
			rc = crt_na_ofi_config_init();
			if (rc != 0) {
				D_ERROR("crt_na_ofi_config_init failed, "
					"rc: %d.\n", rc);
				D_GOTO(out, rc);
			}
		}

		rc = crt_hg_init();
		if (rc != 0) {
			D_ERROR("crt_hg_init failed rc: %d.\n", rc);
			D_GOTO(cleanup, rc);
		}

		rc = crt_grp_init(grpid);
		if (rc != 0) {
			D_ERROR("crt_grp_init failed, rc: %d.\n", rc);
			D_GOTO(cleanup, rc);
		}

		if (crt_plugin_gdata.cpg_inited == 0) {
			rc = crt_plugin_init();
			if (rc != 0) {
				D_ERROR("crt_plugin_init rc: %d.\n", rc);
				D_GOTO(cleanup, rc);
			}
		}

		crt_self_test_init();

		rc = crt_opc_map_create(CRT_OPC_MAP_BITS);
		if (rc != 0) {
			D_ERROR("crt_opc_map_create failed rc: %d.\n", rc);
			D_GOTO(cleanup, rc);
		}
		D_ASSERT(crt_gdata.cg_opc_map != NULL);

		crt_gdata.cg_inited = 1;

	} else {
		if (crt_gdata.cg_server == false && server == true) {
			D_ERROR("CRT initialized as client, cannot set as "
				"server again.\n");
			D_GOTO(unlock, rc = -DER_INVAL);
		}
	}

	crt_gdata.cg_refcount++;

	D_GOTO(unlock, rc);

cleanup:
	crt_gdata.cg_inited = 0;
	if (crt_gdata.cg_grp_inited == 1)
		crt_grp_fini();
	if (crt_gdata.cg_opc_map != NULL)
		crt_opc_map_destroy(crt_gdata.cg_opc_map);

	crt_na_ofi_config_fini();

unlock:
	D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);

out:
	if (rc != 0) {
		D_ERROR("crt_init failed, rc: %d.\n", rc);
		d_fault_inject_fini();
		d_log_fini();
	}
	return rc;
}

bool
crt_initialized()
{
	return (gdata_init_flag == 1) && (crt_gdata.cg_inited == 1);
}

void
crt_plugin_fini(void)
{
	struct crt_prog_cb_priv		*prog_cb_priv;
	struct crt_timeout_cb_priv	*timeout_cb_priv;
	struct crt_event_cb_priv	*event_cb_priv;
	int				 i;

	D_ASSERT(crt_plugin_gdata.cpg_inited == 1);

	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		while ((prog_cb_priv = d_list_pop_entry(
					&crt_plugin_gdata.cpg_prog_cbs[i],
					struct crt_prog_cb_priv,
					cpcp_link))) {
			D_FREE(prog_cb_priv);
		}
	}

	while ((timeout_cb_priv = d_list_pop_entry(&crt_plugin_gdata.cpg_timeout_cbs,
						   struct crt_timeout_cb_priv,
						   ctcp_link))) {
		D_FREE(timeout_cb_priv);
	}
	while ((event_cb_priv = d_list_pop_entry(&crt_plugin_gdata.cpg_event_cbs,
						 struct crt_event_cb_priv,
						 cecp_link))) {
		D_FREE(event_cb_priv);
	}

	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++)
		D_RWLOCK_DESTROY(&crt_plugin_gdata.cpg_prog_rwlock[i]);
	D_RWLOCK_DESTROY(&crt_plugin_gdata.cpg_timeout_rwlock);
	D_RWLOCK_DESTROY(&crt_plugin_gdata.cpg_event_rwlock);
}

int
crt_finalize(void)
{
	int local_rc;
	int rc = 0;

	D_RWLOCK_WRLOCK(&crt_gdata.cg_rwlock);

	if (!crt_initialized()) {
		D_ERROR("cannot finalize before initializing.\n");
		D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);
		D_GOTO(direct_out, rc = -DER_UNINIT);
	}

	crt_gdata.cg_refcount--;
	if (crt_gdata.cg_refcount == 0) {
		if (crt_gdata.cg_ctx_num > 0) {
			D_ASSERT(!crt_context_empty(CRT_LOCKED));
			D_ERROR("cannot finalize, current ctx_num(%d).\n",
				crt_gdata.cg_ctx_num);
			crt_gdata.cg_refcount++;
			D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);
			D_GOTO(out, rc = -DER_NO_PERM);
		} else {
			D_ASSERT(crt_context_empty(CRT_LOCKED));
		}

		if (crt_plugin_gdata.cpg_inited == 1)
			crt_plugin_fini();

		if (crt_is_service() && crt_gdata.cg_swim_inited)
			crt_swim_fini();

		rc = crt_grp_fini();
		if (rc != 0) {
			D_ERROR("crt_grp_fini failed, rc: %d.\n", rc);
			crt_gdata.cg_refcount++;
			D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);
			D_GOTO(out, rc);
		}

		rc = crt_hg_fini();
		if (rc != 0) {
			D_ERROR("crt_hg_fini failed rc: %d.\n", rc);
			crt_gdata.cg_refcount++;
			D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);
			D_GOTO(out, rc);
		}

		crt_opc_map_destroy(crt_gdata.cg_opc_map);

		D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);
		rc = D_RWLOCK_DESTROY(&crt_gdata.cg_rwlock);
		if (rc != 0) {
			D_ERROR("failed to destroy cg_rwlock, rc: %d.\n", rc);
			D_GOTO(out, rc);
		}

		/* allow the same program to re-initialize */
		crt_gdata.cg_refcount = 0;
		crt_gdata.cg_inited = 0;
		gdata_init_flag = 0;

		crt_na_ofi_config_fini();
	} else {
		D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);
	}

out:
	/* d_fault_inject_fini() is reference counted */
	local_rc = d_fault_inject_fini();
	if (local_rc != 0)
		D_ERROR("d_fault_inject_fini() failed, rc: %d\n", local_rc);

direct_out:
	if (rc == 0)
		d_log_fini(); /* d_log_fini is reference counted */
	else
		D_ERROR("crt_finalize failed, rc: %d.\n", rc);

	return rc;
}

/* global NA OFI plugin configuration */
struct na_ofi_config crt_na_ofi_conf;

static inline na_bool_t is_integer_str(char *str)
{
	char *p;

	p = str;
	if (p == NULL || strlen(p) == 0)
		return NA_FALSE;

	while (*p != '\0') {
		if (*p <= '9' && *p >= '0') {
			p++;
			continue;
		} else {
			return NA_FALSE;
		}
	}

	return NA_TRUE;
}

static inline int
crt_get_port_psm2(int *port)
{
	int		rc = 0;
	uint16_t	pid;

	pid = getpid();
	*port = (pid << 8);
	D_DEBUG(DB_ALL, "got a port: %d.\n", *port);

	return rc;
}

int crt_na_ofi_config_init(void)
{
	char		*port_str;
	char		*interface;
	int		port;
	struct ifaddrs	*if_addrs = NULL;
	struct ifaddrs	*ifa = NULL;
	void		*tmp_ptr;
	const char	*ip_str = NULL;
	char		*domain = NULL;
	int		rc = 0;

	interface = getenv("OFI_INTERFACE");
	if (interface != NULL && strlen(interface) > 0) {
		D_STRNDUP(crt_na_ofi_conf.noc_interface, interface, 64);
		if (crt_na_ofi_conf.noc_interface == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	} else {
		crt_na_ofi_conf.noc_interface = NULL;
		D_ERROR("ENV OFI_INTERFACE not set.");
		D_GOTO(out, rc = -DER_INVAL);
	}

	domain = getenv("OFI_DOMAIN");
	if (domain == NULL) {
		D_DEBUG(DB_ALL, "OFI_DOMAIN is not set. Setting it to %s\n",
			interface);
		domain = interface;
	}

	D_STRNDUP(crt_na_ofi_conf.noc_domain, domain, 64);
	if (!crt_na_ofi_conf.noc_domain) {
		D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = getifaddrs(&if_addrs);
	if (rc != 0) {
		D_ERROR("cannot getifaddrs, errno: %d(%s).\n",
			     errno, strerror(errno));
		D_GOTO(out, rc = -DER_PROTO);
	}

	for (ifa = if_addrs; ifa != NULL; ifa = ifa->ifa_next) {
		if (strcmp(ifa->ifa_name, crt_na_ofi_conf.noc_interface))
			continue;
		if (ifa->ifa_addr == NULL)
			continue;
		memset(crt_na_ofi_conf.noc_ip_str, 0, INET_ADDRSTRLEN);
		if (ifa->ifa_addr->sa_family == AF_INET) {
			/* check it is a valid IPv4 Address */
			tmp_ptr =
			&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
			ip_str = inet_ntop(AF_INET, tmp_ptr,
					   crt_na_ofi_conf.noc_ip_str,
					   INET_ADDRSTRLEN);
			if (ip_str == NULL) {
				D_ERROR("inet_ntop failed, errno: %d(%s).\n",
					errno, strerror(errno));
				freeifaddrs(if_addrs);
				D_GOTO(out, rc = -DER_PROTO);
			}
			/*
			 * D_DEBUG("Get interface %s IPv4 Address %s\n",
			 * ifa->ifa_name, na_ofi_conf.noc_ip_str);
			 */
			break;
		} else if (ifa->ifa_addr->sa_family == AF_INET6) {
			/* check it is a valid IPv6 Address */
			/*
			 * tmp_ptr =
			 * &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
			 * inet_ntop(AF_INET6, tmp_ptr, na_ofi_conf.noc_ip_str,
			 *           INET6_ADDRSTRLEN);
			 * D_DEBUG("Get %s IPv6 Address %s\n",
			 *         ifa->ifa_name, na_ofi_conf.noc_ip_str);
			 */
		}
	}
	freeifaddrs(if_addrs);
	if (ip_str == NULL) {
		D_ERROR("no IP addr found.\n");
		D_GOTO(out, rc = -DER_PROTO);
	}

	port = -1;
	port_str = getenv("OFI_PORT");
	if (crt_is_service() && port_str != NULL && strlen(port_str) > 0) {
		if (!is_integer_str(port_str)) {
			D_DEBUG(DB_ALL, "ignoring invalid OFI_PORT %s.",
				port_str);
		} else {
			port = atoi(port_str);
			if (crt_gdata.cg_na_plugin == CRT_NA_OFI_PSM2)
				port = (uint16_t) port << 8;
			D_DEBUG(DB_ALL, "OFI_PORT %d, using it as service "
					"port.\n", port);
		}
	} else if (crt_gdata.cg_na_plugin == CRT_NA_OFI_PSM2) {
		rc = crt_get_port_psm2(&port);
		if (rc != 0) {
			D_ERROR("crt_get_port failed, rc: %d.\n", rc);
			D_GOTO(out, rc);
		}
	}
	crt_na_ofi_conf.noc_port = port;

out:
	if (rc != -DER_SUCCESS) {
		D_FREE(crt_na_ofi_conf.noc_interface);
		D_FREE(crt_na_ofi_conf.noc_domain);
	}
	return rc;
}

void crt_na_ofi_config_fini(void)
{
	D_FREE(crt_na_ofi_conf.noc_interface);
	D_FREE(crt_na_ofi_conf.noc_domain);
	crt_na_ofi_conf.noc_port = 0;
}
