/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It implements CaRT init and finalize related
 * APIs/handling.
 */
#include <malloc.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "crt_internal.h"

struct crt_gdata	crt_gdata;
static volatile int	gdata_init_flag;
struct crt_plugin_gdata crt_plugin_gdata;
static bool		g_prov_settings_applied[CRT_PROV_COUNT];

static void
crt_lib_init(void) __attribute__((__constructor__));

static void
crt_lib_fini(void) __attribute__((__destructor__));

/* Library initialization/constructor */
static void
crt_lib_init(void)
{
	int		rc;
	uint64_t	start_rpcid;

	rc = D_RWLOCK_INIT(&crt_gdata.cg_rwlock, NULL);
	D_ASSERT(rc == 0);

	/*
	 * avoid size mis-matching between client/server side
	 * /see crt_proc_uuid_t().
	 */
	D_CASSERT(sizeof(uuid_t) == 16);

	crt_gdata.cg_refcount = 0;
	crt_gdata.cg_inited = 0;
	crt_gdata.cg_primary_prov = CRT_PROV_OFI_SOCKETS;

	d_srand(d_timeus_secdiff(0) + getpid());
	start_rpcid = ((uint64_t)d_rand()) << 32;

	crt_gdata.cg_rpcid = start_rpcid;
	crt_gdata.cg_num_cores = sysconf(_SC_NPROCESSORS_ONLN);
}

/* Library deinit */
static void
crt_lib_fini(void)
{
	D_RWLOCK_DESTROY(&crt_gdata.cg_rwlock);
}

static void
dump_envariables(void)
{
	int	i;
	char	*val;
	char	*envars[] = {"D_PROVIDER", "D_INTERFACE", "D_DOMAIN", "D_PORT",
		"CRT_PHY_ADDR_STR", "D_LOG_STDERR_IN_LOG", "D_LOG_SIZE",
		"D_LOG_FILE", "D_LOG_FILE_APPEND_PID", "D_LOG_MASK", "DD_MASK",
		"DD_STDERR", "DD_SUBSYS", "CRT_TIMEOUT", "CRT_ATTACH_INFO_PATH",
		"OFI_PORT", "OFI_INTERFACE", "OFI_DOMAIN", "CRT_CREDIT_EP_CTX",
		"CRT_CTX_SHARE_ADDR", "CRT_CTX_NUM", "D_FI_CONFIG",
		"FI_UNIVERSE_SIZE", "CRT_ENABLE_MEM_PIN",
		"FI_OFI_RXM_USE_SRX", "D_LOG_FLUSH", "CRT_MRC_ENABLE",
		"CRT_SECONDARY_PROVIDER", "D_PROVIDER_AUTH_KEY", "D_PORT_AUTO_ADJUST",
		"D_POLL_TIMEOUT"};

	D_INFO("-- ENVARS: --\n");
	for (i = 0; i < ARRAY_SIZE(envars); i++) {
		val = getenv(envars[i]);
		if (strcmp(envars[i], "D_PROVIDER_AUTH_KEY") == 0 && val)
			D_INFO("%s = %s\n", envars[i], "********");
		else
			D_INFO("%s = %s\n", envars[i], val);
	}
}

static void
dump_opt(crt_init_options_t *opt)
{
	D_INFO("options:\n");
	D_INFO("crt_timeout = %d\n", opt->cio_crt_timeout);
	D_INFO("max_ctx_num = %d\n", opt->cio_ctx_max_num);
	D_INFO("swim_idx = %d\n", opt->cio_swim_crt_idx);
	D_INFO("provider = %s\n", opt->cio_provider);
	D_INFO("interface = %s\n", opt->cio_interface);
	D_INFO("domain = %s\n", opt->cio_domain);
}

static int
crt_na_config_init(bool primary, crt_provider_t provider,
		   char *interface, char *domain, char *port,
		   char *auth_key, bool port_auto_adjust);

/* Workaround for CART-890 */
static void
mem_pin_workaround(void)
{
	struct rlimit	rlim;
	int		rc = 0;

	/* Note: mallopt() returns 1 on success */
	/* Prevent malloc from releasing memory via sbrk syscall */
	rc = mallopt(M_TRIM_THRESHOLD, -1);
	if (rc != 1)
		D_WARN("Failed to disable malloc trim: %d\n", errno);

	/* Disable fastbins; this option is not available on all systems */
	rc = mallopt(M_MXFAST, 0);
	if (rc != 1)
		D_WARN("Failed to disable malloc fastbins: %d (%s)\n", errno, strerror(errno));

	rc = getrlimit(RLIMIT_MEMLOCK, &rlim);
	if (rc != 0) {
		D_WARN("getrlimit() failed; errno=%d (%s)\n",
		       errno, strerror(errno));
		goto exit;
	}

	if (rlim.rlim_cur == RLIM_INFINITY &&
	    rlim.rlim_max == RLIM_INFINITY) {
		D_INFO("Infinite rlimit detected; performing mlockall()\n");

		/* Lock all pages */
		rc = mlockall(MCL_CURRENT | MCL_FUTURE);
		if (rc)
			D_WARN("Failed to mlockall(); errno=%d (%s)\n",
			       errno, strerror(errno));

	} else {
		D_INFO("mlockall() skipped\n");
	}

	D_DEBUG(DB_ALL, "Memory pinning workaround enabled\n");
exit:
	return;
}

/* Value based on default daos runs with 16 targets + 2 service contexts */
#define CRT_SRV_CONTEXT_NUM_MIN (16 + 2)

static int
prov_data_init(struct crt_prov_gdata *prov_data, crt_provider_t provider,
	       bool primary, crt_init_options_t *opt)

{
	bool		share_addr = false;
	bool		set_sep = false;
	uint32_t	ctx_num = 0;
	uint32_t	max_expect_size = 0;
	uint32_t	max_unexpect_size = 0;
	uint32_t	max_num_ctx = CRT_SRV_CONTEXT_NUM;
	int		i;
	int		rc;

	rc = D_MUTEX_INIT(&prov_data->cpg_mutex, NULL);
	if (rc != 0)
		return rc;

	/* Set max number of contexts. Defaults to the number of cores */
	ctx_num = 0;
	d_getenv_int("CRT_CTX_NUM", &ctx_num);
	if (opt)
		max_num_ctx = ctx_num ? ctx_num : max(crt_gdata.cg_num_cores, opt->cio_ctx_max_num);
	else
		max_num_ctx = ctx_num ? ctx_num : crt_gdata.cg_num_cores;

	if (max_num_ctx > CRT_SRV_CONTEXT_NUM)
		max_num_ctx = CRT_SRV_CONTEXT_NUM;

	/* To be able to run on VMs */
	if (max_num_ctx < CRT_SRV_CONTEXT_NUM_MIN)
		max_num_ctx = CRT_SRV_CONTEXT_NUM_MIN;

	D_DEBUG(DB_ALL, "Max number of contexts set to %d\n", max_num_ctx);

	/* Assume for now this option is only available for a primary provider */
	if (primary) {
		if (opt && opt->cio_sep_override) {
			if (opt->cio_use_sep) {
				set_sep = true;
				max_num_ctx = opt->cio_ctx_max_num;
			}
		} else {
			share_addr = false;

			d_getenv_bool("CRT_CTX_SHARE_ADDR", &share_addr);
			if (share_addr) {
				set_sep = true;
				ctx_num = 0;
				d_getenv_int("CRT_CTX_NUM", &ctx_num);
				max_num_ctx = ctx_num;
			}
		}
	}

	if (opt && opt->cio_use_expected_size)
		max_expect_size = opt->cio_max_expected_size;

	if (opt && opt->cio_use_unexpected_size)
		max_unexpect_size = opt->cio_max_unexpected_size;

	prov_data->cpg_inited = true;
	prov_data->cpg_provider = provider;
	prov_data->cpg_ctx_num = 0;
	prov_data->cpg_sep_mode = set_sep;
	prov_data->cpg_contig_ports = true;
	prov_data->cpg_ctx_max_num = max_num_ctx;
	prov_data->cpg_max_exp_size = max_expect_size;
	prov_data->cpg_max_unexp_size = max_unexpect_size;
	prov_data->cpg_primary = primary;

	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++)
		prov_data->cpg_used_idx[i] = false;

	/* By default set number of secondary remote tags to 1 */
	prov_data->cpg_num_remote_tags = 1;
	prov_data->cpg_last_remote_tag = 0;

	D_DEBUG(DB_ALL, "prov_idx: %d primary: %d sep_mode: %d sizes: (%d/%d) max_ctx: %d\n",
		provider, primary, set_sep, max_expect_size, max_unexpect_size, max_num_ctx);

	D_INIT_LIST_HEAD(&prov_data->cpg_ctx_list);

	return DER_SUCCESS;
}

/* first step init - for initializing crt_gdata */
static int data_init(int server, crt_init_options_t *opt)
{
	uint32_t	timeout;
	uint32_t	credits;
	uint32_t	fi_univ_size = 0;
	uint32_t	mem_pin_enable = 0;
	uint32_t	is_secondary;
	char		ucx_ib_fork_init = 0;
	int		rc = 0;

	D_DEBUG(DB_ALL, "initializing crt_gdata...\n");

	dump_envariables();

	D_DEBUG(DB_ALL, "Starting RPCID %#lx. Num cores: %ld\n",
		crt_gdata.cg_rpcid, crt_gdata.cg_num_cores);

	is_secondary = 0;
	/* Apply CART-890 workaround for server side only */
	if (server) {
		d_getenv_int("CRT_ENABLE_MEM_PIN", &mem_pin_enable);
		if (mem_pin_enable == 1)
			mem_pin_workaround();
	} else {
		/*
		 * Client-side envariable to indicate that the cluster
		 * is running using a secondary provider
		 */
		d_getenv_int("CRT_SECONDARY_PROVIDER", &is_secondary);

	}

	crt_gdata.cg_provider_is_primary = (is_secondary) ? 0 : 1;

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

	crt_gdata.cg_swim_crt_idx = CRT_DEFAULT_PROGRESS_CTX_IDX;

	D_DEBUG(DB_ALL, "SWIM context idx=%d\n", crt_gdata.cg_swim_crt_idx);

	/* Override defaults and environment if option is set */
	if (opt && opt->cio_use_credits) {
		credits = opt->cio_ep_credits;
	} else {
		credits = CRT_DEFAULT_CREDITS_PER_EP_CTX;
		d_getenv_int("CRT_CREDIT_EP_CTX", &credits);
	}

	/* Must be set on the server when using UCX, will not affect OFI */
	d_getenv_char("UCX_IB_FORK_INIT", &ucx_ib_fork_init);
	if (ucx_ib_fork_init) {
		if (server) {
			D_INFO("UCX_IB_FORK_INIT was set to %c, setting to n\n", ucx_ib_fork_init);
		} else {
			D_INFO("UCX_IB_FORK_INIT was set to %c on client\n", ucx_ib_fork_init);
		}
	}
	if (server)
		setenv("UCX_IB_FORK_INIT", "n", 1);

	/* This is a workaround for CART-871 if universe size is not set */
	d_getenv_int("FI_UNIVERSE_SIZE", &fi_univ_size);
	if (fi_univ_size == 0) {
		D_INFO("FI_UNIVERSE_SIZE was not set; setting to 2048\n");
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

	/** Enable statistics only for the server side and if requested */
	if (opt && opt->cio_use_sensors && server) {
		int	ret;

		/** enable sensors */
		crt_gdata.cg_use_sensors = true;

		/** set up the global sensors */
		ret = d_tm_add_metric(&crt_gdata.cg_uri_self, D_TM_COUNTER,
				      "total number of URI requests for self",
				      "", "net/uri/lookup_self");
		if (ret)
			D_WARN("Failed to create uri self sensor: "DF_RC"\n",
			       DP_RC(ret));

		ret = d_tm_add_metric(&crt_gdata.cg_uri_other, D_TM_COUNTER,
				      "total number of URI requests for other "
				      "ranks", "", "net/uri/lookup_other");
		if (ret)
			D_WARN("Failed to create uri other sensor: "DF_RC"\n",
			       DP_RC(ret));
	}

	gdata_init_flag = 1;
	return rc;
}

static int
crt_plugin_init(void)
{
	struct crt_prog_cb_priv *cbs_prog;
	struct crt_event_cb_priv *cbs_event;
	size_t cbs_size = CRT_CALLBACKS_NUM;
	int i, rc;

	D_ASSERT(crt_plugin_gdata.cpg_inited == 0);

	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		crt_plugin_gdata.cpg_prog_cbs_old[i] = NULL;
		D_ALLOC_ARRAY(cbs_prog, cbs_size);
		if (cbs_prog == NULL) {
			for (i--; i >= 0; i--)
				D_FREE(crt_plugin_gdata.cpg_prog_cbs[i]);
			D_GOTO(out, rc = -DER_NOMEM);
		}
		crt_plugin_gdata.cpg_prog_size[i] = cbs_size;
		crt_plugin_gdata.cpg_prog_cbs[i]  = cbs_prog;
	}

	crt_plugin_gdata.cpg_event_cbs_old = NULL;
	D_ALLOC_ARRAY(cbs_event, cbs_size);
	if (cbs_event == NULL) {
		D_GOTO(out_destroy_prog, rc = -DER_NOMEM);
	}
	crt_plugin_gdata.cpg_event_size = cbs_size;
	crt_plugin_gdata.cpg_event_cbs  = cbs_event;

	rc = D_MUTEX_INIT(&crt_plugin_gdata.cpg_mutex, NULL);
	if (rc)
		D_GOTO(out_destroy_event, rc);

	crt_plugin_gdata.cpg_inited = 1;
	D_GOTO(out, rc = 0);

out_destroy_event:
	D_FREE(crt_plugin_gdata.cpg_event_cbs);
out_destroy_prog:
	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++)
		D_FREE(crt_plugin_gdata.cpg_prog_cbs[i]);
out:
	return rc;
}

static void
crt_plugin_fini(void)
{
	int i;

	D_ASSERT(crt_plugin_gdata.cpg_inited == 1);

	crt_plugin_gdata.cpg_inited = 0;

	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		D_FREE(crt_plugin_gdata.cpg_prog_cbs[i]);
		D_FREE(crt_plugin_gdata.cpg_prog_cbs_old[i]);
	}

	D_FREE(crt_plugin_gdata.cpg_event_cbs);
	D_FREE(crt_plugin_gdata.cpg_event_cbs_old);

	D_MUTEX_DESTROY(&crt_plugin_gdata.cpg_mutex);
}

static int
__split_arg(char *s_arg_to_split, char **first_arg, char **second_arg)
{
	char	*save_ptr = NULL;
	char	*arg_to_split;

	D_ASSERT(first_arg != NULL);
	D_ASSERT(second_arg != NULL);

	/* no-op, not an error case */
	if (s_arg_to_split == NULL) {
		*first_arg = NULL;
		*second_arg = NULL;
		return DER_SUCCESS;
	}

	D_STRNDUP(arg_to_split, s_arg_to_split, 255);
	if (!arg_to_split) {
		*first_arg = NULL;
		*second_arg = NULL;
		return -DER_NOMEM;
	}

	*first_arg = 0;
	*second_arg = 0;

	*first_arg = strtok_r(arg_to_split, ",", &save_ptr);
	*second_arg = save_ptr;

	return DER_SUCCESS;
}


int
crt_str_to_provider(const char *str_provider)
{
	int	provider_idx = CRT_PROV_UNKNOWN;
	int	i;

	if (str_provider == NULL)
		return provider_idx;

	for (i = 0; crt_na_dict[i].nad_str != NULL; i++) {

		if (!strncmp(str_provider, crt_na_dict[i].nad_str,
			     strlen(crt_na_dict[i].nad_str) + 1) ||
		    (crt_na_dict[i].nad_alt_str &&
		     !strncmp(str_provider, crt_na_dict[i].nad_alt_str,
			      strlen(crt_na_dict[i].nad_alt_str) + 1))) {
			provider_idx = crt_na_dict[i].nad_type;
			break;
		}
	}

	return provider_idx;
}

static int
check_grpid(crt_group_id_t grpid)
{
	int rc = 0;

	if (grpid == NULL)
		return rc;

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
out:
	return rc;
}

static void
apply_if_not_set(const char *env_name, const char *new_value)
{
	char *old_val;

	old_val = getenv(env_name);

	if (old_val == NULL) {
		D_INFO("%s not set, setting to %s\n", env_name, new_value);
		setenv(env_name, new_value, true);
	}
}

static void
prov_settings_apply(bool primary, crt_provider_t prov, crt_init_options_t *opt)
{
	uint32_t mrc_enable = 0;

	/* Avoid applying same settings multiple times */
	if (g_prov_settings_applied[prov] == true)
		return;

	/* rxm and verbs providers only works with regular EP */
	if (prov != CRT_PROV_OFI_SOCKETS &&
	    crt_provider_is_sep(primary, prov)) {
		D_WARN("set CRT_CTX_SHARE_ADDR as 1 is invalid "
		       "for current provider, ignoring it.\n");
		crt_provider_set_sep(prov, primary, false);
	}

	if (prov == CRT_PROV_OFI_VERBS_RXM ||
	    prov == CRT_PROV_OFI_TCP_RXM) {
		/* Use shared receive queues to avoid large mem consumption */
		apply_if_not_set("FI_OFI_RXM_USE_SRX", "1");

		/* Only apply on the server side */
		if (prov == CRT_PROV_OFI_TCP_RXM && crt_is_service())
			apply_if_not_set("FI_OFI_RXM_DEF_TCP_WAIT_OBJ", "pollfd");

	}

	if (prov == CRT_PROV_OFI_CXI)
		mrc_enable = 1;

	d_getenv_int("CRT_MRC_ENABLE", &mrc_enable);
	if (mrc_enable == 0) {
		D_INFO("Disabling MR CACHE (FI_MR_CACHE_MAX_COUNT=0)\n");
		setenv("FI_MR_CACHE_MAX_COUNT", "0", 1);
	}

	/* Use tagged messages for other providers, disable multi-recv */
	if (prov != CRT_PROV_OFI_CXI && prov != CRT_PROV_OFI_TCP)
		apply_if_not_set("NA_OFI_UNEXPECTED_TAG_MSG", "1");

	g_prov_settings_applied[prov] = true;
}

int
crt_init_opt(crt_group_id_t grpid, uint32_t flags, crt_init_options_t *opt)
{
	char		*provider_env;
	char		*interface_env;
	char		*domain_env;
	char		*auth_key_env;
	char		*tmp;
	struct timeval	now;
	unsigned int	seed;
	const char	*path;
	bool		server;
	int		rc = 0;
	char		*provider_str0 = NULL;
	char		*provider_str1 = NULL;
	crt_provider_t	primary_provider;
	crt_provider_t	secondary_provider;
	crt_provider_t	tmp_prov;
	char		*port_str, *port0, *port1;
	char		*iface0, *iface1, *domain0, *domain1;
	char		*auth_key0, *auth_key1;
	int		num_secondaries = 0;
	bool		port_auto_adjust = false;
	int		i;

	server = flags & CRT_FLAG_BIT_SERVER;
	port_str = NULL;
	port0 = NULL;
	port1 = NULL;
	iface0 = NULL;
	iface1 = NULL;
	domain0 = NULL;
	domain1 = NULL;
	auth_key0 = NULL;
	auth_key1 = NULL;

	/* d_log_init is reference counted */
	rc = d_log_init();
	if (rc != 0) {
		D_PRINT_ERR("d_log_init failed, rc: %d.\n", rc);
		return rc;
	}

	crt_setup_log_fac();

	D_INFO("libcart version %s initializing\n", CART_VERSION);

	if (opt)
		dump_opt(opt);

	/* d_fault_inject_init() is reference counted */
	rc = d_fault_inject_init();
	if (rc != DER_SUCCESS && rc != -DER_NOSYS) {
		D_ERROR("d_fault_inject_init() failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}

	/* check the group name */
	rc = check_grpid(grpid);
	if (rc != DER_SUCCESS)
		D_GOTO(out, rc);

	if (gdata_init_flag == 0) {
		rc = data_init(server, opt);
		if (rc != 0) {
			D_ERROR("data_init failed "DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}
	}
	D_ASSERT(gdata_init_flag == 1);

	D_RWLOCK_WRLOCK(&crt_gdata.cg_rwlock);
	if (crt_gdata.cg_inited == 0) {
		/* feed a seed for pseudo-random number generator */
		gettimeofday(&now, NULL);
		seed = (unsigned int)(now.tv_sec * 1000000 + now.tv_usec);
		d_srand(seed);

		crt_gdata.cg_server = server;
		crt_gdata.cg_auto_swim_disable =
			(flags & CRT_FLAG_BIT_AUTO_SWIM_DISABLE) ? 1 : 0;

		path = getenv("CRT_ATTACH_INFO_PATH");
		if (path != NULL && strlen(path) > 0) {
			rc = crt_group_config_path_set(path);
			if (rc != 0)
				D_ERROR("Got %s from ENV CRT_ATTACH_INFO_PATH, "
					"but crt_group_config_path_set failed "
					"rc: %d, ignore the ENV.\n", path, rc);
			else
				D_DEBUG(DB_ALL, "set group_config_path as %s.\n", path);
		}

		if (opt && opt->cio_auth_key)
			auth_key_env = opt->cio_auth_key;
		else
			auth_key_env = getenv("D_PROVIDER_AUTH_KEY");

		if (opt && opt->cio_provider)
			provider_env = opt->cio_provider;
		else {
			provider_env = getenv(CRT_PHY_ADDR_ENV);

			tmp = getenv("D_PROVIDER");
			if (tmp)
				provider_env = tmp;
		}

		if (opt && opt->cio_interface)
			interface_env = opt->cio_interface;
		else {
			interface_env = getenv("OFI_INTERFACE");

			tmp = getenv("D_INTERFACE");
			if (tmp)
				interface_env = tmp;
		}

		if (opt && opt->cio_domain)
			domain_env = opt->cio_domain;
		else {
			domain_env = getenv("OFI_DOMAIN");

			tmp = getenv("D_DOMAIN");
			if (tmp)
				domain_env = tmp;
		}

		if (opt && opt->cio_port)
			port_str = opt->cio_port;
		else {
			port_str = getenv("OFI_PORT");

			tmp = getenv("D_PORT");
			if (tmp)
				port_str = tmp;
		}

		d_getenv_bool("D_PORT_AUTO_ADJUST", &port_auto_adjust);

		rc = __split_arg(provider_env, &provider_str0, &provider_str1);
		if (rc != 0)
			D_GOTO(unlock, rc);

		primary_provider = crt_str_to_provider(provider_str0);
		secondary_provider = crt_str_to_provider(provider_str1);

		if (primary_provider == CRT_PROV_UNKNOWN) {
			D_ERROR("Requested provider %s not found\n", provider_env);
			D_GOTO(unlock, rc = -DER_NONEXIST);
		}

		rc = __split_arg(interface_env, &iface0, &iface1);
		if (rc != 0)
			D_GOTO(unlock, rc);
		rc = __split_arg(domain_env, &domain0, &domain1);
		if (rc != 0)
			D_GOTO(unlock, rc);
		rc = __split_arg(port_str, &port0, &port1);
		if (rc != 0)
			D_GOTO(unlock, rc);
		rc = __split_arg(auth_key_env, &auth_key0, &auth_key1);
		if (rc != 0)
			D_GOTO(unlock, rc);

		if (iface0 == NULL) {
			D_ERROR("Empty interface specified\n");
			D_GOTO(unlock, rc = -DER_INVAL);
		}

		rc = prov_data_init(&crt_gdata.cg_prov_gdata_primary,
				    primary_provider, true, opt);
		if (rc != 0)
			D_GOTO(unlock, rc);

		prov_settings_apply(true, primary_provider, opt);
		crt_gdata.cg_primary_prov = primary_provider;

		rc = crt_na_config_init(true, primary_provider, iface0, domain0,
					port0, auth_key0, port_auto_adjust);
		if (rc != 0) {
			D_ERROR("crt_na_config_init() failed, "DF_RC"\n", DP_RC(rc));
			D_GOTO(unlock, rc);
		}

		if (secondary_provider != CRT_PROV_UNKNOWN) {
			num_secondaries = 1;
			crt_gdata.cg_num_secondary_provs = num_secondaries;

			if (port1 == NULL || port1[0] == '\0') {
				port1 = port0;
			}

			D_ALLOC_ARRAY(crt_gdata.cg_secondary_provs, num_secondaries);
			if (crt_gdata.cg_secondary_provs == NULL)
				D_GOTO(cleanup, rc = -DER_NOMEM);

			D_ALLOC_ARRAY(crt_gdata.cg_prov_gdata_secondary, num_secondaries);
			if (crt_gdata.cg_prov_gdata_secondary == NULL)
				D_GOTO(cleanup, rc = -DER_NOMEM);

			crt_gdata.cg_secondary_provs[0] = secondary_provider;
		}

		for (i = 0;  i < num_secondaries; i++) {
			tmp_prov = crt_gdata.cg_secondary_provs[i];

			rc = prov_data_init(&crt_gdata.cg_prov_gdata_secondary[i],
					    tmp_prov, false, opt);
			if (rc != 0)
				D_GOTO(cleanup, rc);

			prov_settings_apply(false, tmp_prov, opt);

			rc = crt_na_config_init(false, tmp_prov, iface1, domain1,
						port1, auth_key1, port_auto_adjust);
			if (rc != 0) {
				D_ERROR("crt_na_config_init() failed, "DF_RC"\n", DP_RC(rc));
				D_GOTO(cleanup, rc);
			}
		}

		rc = crt_hg_init();
		if (rc != 0) {
			D_ERROR("crt_hg_init() failed, "DF_RC"\n", DP_RC(rc));
			D_GOTO(cleanup, rc);
		}

		rc = crt_grp_init(grpid);
		if (rc != 0) {
			D_ERROR("crt_grp_init() failed, "DF_RC"\n", DP_RC(rc));
			D_GOTO(cleanup, rc);
		}

		if (crt_plugin_gdata.cpg_inited == 0) {
			rc = crt_plugin_init();
			if (rc != 0) {
				D_ERROR("crt_plugin_init() failed, "DF_RC"\n", DP_RC(rc));
				D_GOTO(cleanup, rc);
			}
		}

		crt_self_test_init();

		rc = crt_opc_map_create();
		if (rc != 0) {
			D_ERROR("crt_opc_map_create() failed, "DF_RC"\n", DP_RC(rc));
			D_GOTO(self_test, rc);
		}

		rc = crt_internal_rpc_register(server);
		if (rc != 0) {
			D_ERROR("crt_internal_rpc_register() failed, "DF_RC"\n", DP_RC(rc));
			D_GOTO(self_test, rc);
		}

		D_ASSERT(crt_gdata.cg_opc_map != NULL);

		crt_gdata.cg_inited = 1;
	} else {
		if (crt_gdata.cg_server == false && server == true) {
			D_ERROR("CRT initialized as client, cannot set as server again.\n");
			D_GOTO(unlock, rc = -DER_INVAL);
		}
	}

	crt_gdata.cg_refcount++;

	D_GOTO(unlock, rc);

self_test:
	crt_self_test_fini();

cleanup:
	crt_gdata.cg_inited = 0;
	if (crt_plugin_gdata.cpg_inited == 1)
		crt_plugin_fini();
	if (crt_gdata.cg_grp_inited == 1)
		crt_grp_fini();
	if (crt_gdata.cg_opc_map != NULL)
		crt_opc_map_destroy(crt_gdata.cg_opc_map);

	crt_na_config_fini(true, crt_gdata.cg_primary_prov);

	D_FREE(crt_gdata.cg_secondary_provs);
	D_FREE(crt_gdata.cg_prov_gdata_secondary);

unlock:
	D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);

out:
	/*
	 * We don't need to free port1, iface1 and domain1 as
	 * they occupy the same original string as port0, iface0 and domain0
	 */
	D_FREE(port0);
	D_FREE(iface0);
	D_FREE(domain0);
	D_FREE(provider_str0);
	D_FREE(auth_key0);

	if (rc != 0) {
		D_ERROR("failed, "DF_RC"\n", DP_RC(rc));
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

int
crt_finalize(void)
{
	int local_rc;
	int rc = 0;
	int i;
	struct crt_prov_gdata *prov_data;

	D_RWLOCK_WRLOCK(&crt_gdata.cg_rwlock);

	if (!crt_initialized()) {
		D_ERROR("cannot finalize before initializing.\n");
		D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);
		D_GOTO(direct_out, rc = -DER_UNINIT);
	}

	crt_gdata.cg_refcount--;
	if (crt_gdata.cg_refcount == 0) {
		crt_self_test_fini();

		/* TODO: Needs to happen for every initialized provider */
		prov_data = &crt_gdata.cg_prov_gdata_primary;

		if (prov_data->cpg_ctx_num > 0) {
			D_ASSERT(!crt_context_empty(crt_gdata.cg_primary_prov,
				 CRT_LOCKED));
			D_ERROR("cannot finalize, current ctx_num(%d).\n",
				prov_data->cpg_ctx_num);
			crt_gdata.cg_refcount++;
			D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);
			D_GOTO(out, rc = -DER_BUSY);
		} else {
			D_ASSERT(crt_context_empty(crt_gdata.cg_primary_prov,
				 CRT_LOCKED));
		}

		if (crt_plugin_gdata.cpg_inited == 1)
			crt_plugin_fini();

		if (crt_is_service() && crt_gdata.cg_swim_inited)
			crt_swim_fini();

		crt_grp_fini();

		rc = crt_hg_fini();
		if (rc != 0) {
			D_ERROR("crt_hg_fini failed rc: %d.\n", rc);
			crt_gdata.cg_refcount++;
			D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);
			D_GOTO(out, rc);
		}

		crt_opc_map_destroy(crt_gdata.cg_opc_map);

		D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);

		/* allow the same program to re-initialize */
		crt_gdata.cg_refcount = 0;
		crt_gdata.cg_inited = 0;
		gdata_init_flag = 0;

		crt_na_config_fini(true, crt_gdata.cg_primary_prov);

		if (crt_gdata.cg_secondary_provs != NULL) {
			for (i = 0; i < crt_gdata.cg_num_secondary_provs; i++)
				crt_na_config_fini(false, crt_gdata.cg_secondary_provs[i]);
		}

		D_FREE(crt_gdata.cg_secondary_provs);
		D_FREE(crt_gdata.cg_prov_gdata_secondary);
	} else {
		D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);
	}

out:
	/* d_fault_inject_fini() is reference counted */
	local_rc = d_fault_inject_fini();
	if (local_rc != 0 && local_rc != -DER_NOSYS)
		D_ERROR("d_fault_inject_fini() failed, rc: %d\n", local_rc);

direct_out:
	if (rc == 0)
		d_log_fini(); /* d_log_fini is reference counted */
	else
		D_ERROR("failed, rc: "DF_RC"\n", DP_RC(rc));

	return rc;
}

static inline bool is_integer_str(char *str)
{
	const char *p;

	p = str;
	if (p == NULL || strlen(p) == 0)
		return false;

	while (*p != '\0') {
		if (*p <= '9' && *p >= '0') {
			p++;
			continue;
		} else {
			return false;
		}
	}

	return true;
}

static inline int
crt_get_port_opx(int *port)
{
	int     rc = 0;
	uint16_t    pid;

	pid = getpid();
	*port = pid;
	D_DEBUG(DB_ALL, "got a port: %d.\n", *port);

	return rc;
}

#define PORT_RANGE_STR_SIZE 32

static void
crt_port_range_verify(int port)
{
	char	proc[] = "/proc/sys/net/ipv4/ip_local_port_range";
	FILE	*f;
	char	buff[PORT_RANGE_STR_SIZE];
	int	start_port = -1;
	int	end_port = -1;
	char	*p;
	int	rc;

	f = fopen(proc, "r");
	if (!f) {
		D_ERROR("Failed to open %s for reading\n", proc);
		return;
	}

	memset(buff, 0x0, PORT_RANGE_STR_SIZE);

	rc = fread(buff, 1, PORT_RANGE_STR_SIZE - 1, f);
	if (rc <= 0) {
		D_ERROR("Failed to read from file %s\n", proc);
		fclose(f);
		return;
	}

	fclose(f);

	p = buff;
	/* Data is in the format of <start_port><whitespaces><end_port>*/
	while (*p != '\0') {
		if (*p == ' ' || *p == '\t') {
			*p = '\0';
			start_port = atoi(buff);

			p++;
			while (*p == ' ' || *p == '\t')
				p++;

			end_port = atoi(p);
			break;
		}
		p++;
	}

	if (start_port == -1)
		return;

	if (port >= start_port && port <= end_port) {
		D_WARN("Requested port %d is inside of the local port range "
		       "as specified by file '%s'\n", port, proc);
		D_WARN("In order to avoid port conflicts pick a different "
		       "value outside of the %d-%d range\n",
		       start_port, end_port);
	}
}


static int
crt_na_fill_ip_addr(struct crt_na_config *na_cfg)
{
	struct ifaddrs	*if_addrs = NULL;
	struct ifaddrs	*ifa = NULL;
	void		*tmp_ptr;
	const char	*ip_str = NULL;
	int		rc = 0;

	rc = getifaddrs(&if_addrs);
	if (rc != 0) {
		D_ERROR("cannot getifaddrs, errno: %d(%s).\n", errno, strerror(errno));
		D_GOTO(out, rc = -DER_PROTO);
	}

	for (ifa = if_addrs; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL)
			continue;
		if (strcmp(ifa->ifa_name, na_cfg->noc_interface))
			continue;

		memset(na_cfg->noc_ip_str, 0, INET_ADDRSTRLEN);

		if (ifa->ifa_addr->sa_family == AF_INET) {
			/* check it is a valid IPv4 Address */
			tmp_ptr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
			ip_str = inet_ntop(AF_INET, tmp_ptr, na_cfg->noc_ip_str, INET_ADDRSTRLEN);
			if (ip_str == NULL) {
				D_ERROR("inet_ntop errno: %d(%s).\n", errno, strerror(errno));
				freeifaddrs(if_addrs);
				D_GOTO(out, rc = -DER_PROTO);
			}
			break;
		} else if (ifa->ifa_addr->sa_family == AF_INET6) {
			/* check it is a valid IPv6 Address */
			/*
			 * tmp_ptr =
			 * &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
			 * inet_ntop(AF_INET6, tmp_ptr, na_conf.noc_ip_str,
			 *           INET6_ADDRSTRLEN);
			 * D_DEBUG("Get %s IPv6 Address %s\n",
			 *         ifa->ifa_name, na_conf.noc_ip_str);
			 */
		}
	}
	freeifaddrs(if_addrs);
	if (ip_str == NULL) {
		D_ERROR("no IP addr found on interface %s\n", na_cfg->noc_interface);
		D_GOTO(out, rc = -DER_PROTO);
	}

out:
	return rc;
}

static int
crt_na_config_init(bool primary, crt_provider_t provider,
		   char *interface, char *domain, char *port_str,
		   char *auth_key, bool port_auto_adjust)
{
	struct crt_na_config		*na_cfg;
	int				rc = 0;
	int				port = -1;

	if (provider == CRT_PROV_SM)
		return 0;

	na_cfg = crt_provider_get_na_config(primary, provider);
	D_STRNDUP(na_cfg->noc_interface, interface, 64);
	if (!na_cfg->noc_interface)
		D_GOTO(out, rc = -DER_NOMEM);

	if (domain) {
		D_STRNDUP(na_cfg->noc_domain, domain, 64);
		if (!na_cfg->noc_domain)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	if (auth_key) {
		D_STRNDUP(na_cfg->noc_auth_key, auth_key, 255);
		if (!na_cfg->noc_auth_key)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	crt_na_fill_ip_addr(na_cfg);
	if (crt_is_service() && port_str != NULL && strlen(port_str) > 0) {
		if (!is_integer_str(port_str)) {
			D_DEBUG(DB_ALL, "ignoring invalid OFI_PORT %s.", port_str);
		} else {
			port = atoi(port_str);

			if (provider == CRT_PROV_OFI_SOCKETS ||
			    provider == CRT_PROV_OFI_VERBS_RXM ||
			    provider == CRT_PROV_OFI_TCP_RXM)
				crt_port_range_verify(port);

			if (provider == CRT_PROV_OFI_CXI && port_auto_adjust) {
				if (port > 511) {
					D_WARN("Port=%d outside of valid range 0-511, "
					       "converting it to %d\n",
					       port, port % 512);
					port = port % 512;
				}
			}

			D_DEBUG(DB_ALL, "OFI_PORT %d, using it as service port.\n", port);
		}
	} else if (provider == CRT_PROV_OFI_OPX) {
		rc = crt_get_port_opx(&port);
		if (rc != 0) {
			D_ERROR("crt_get_port failed, rc: %d.\n", rc);
			D_GOTO(out, rc);
		}
	}

	na_cfg->noc_port = port;

out:
	if (rc != -DER_SUCCESS) {
		D_FREE(na_cfg->noc_interface);
		D_FREE(na_cfg->noc_domain);
		D_FREE(na_cfg->noc_auth_key);
	}
	return rc;
}

void crt_na_config_fini(bool primary, crt_provider_t provider)
{
	struct crt_na_config *na_cfg;

	na_cfg = crt_provider_get_na_config(primary, provider);
	D_FREE(na_cfg->noc_interface);
	D_FREE(na_cfg->noc_domain);
	D_FREE(na_cfg->noc_auth_key);
	na_cfg->noc_port = 0;
}
