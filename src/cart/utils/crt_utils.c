/*
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Common functions to be shared among tests
 */
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cart/api.h>
#include <unistd.h>
#include <daos/mgmt.h>
#include <daos/event.h>

#include "../crt_internal.h"
#include "crt_utils.h"

/* Global structures */
struct test_options opts = { .is_initialized = false };

/* Local structure definitions */
struct wfr_status {
	sem_t	sem;
	int	rc;
	int	num_ctx;
};

/* Functions */
struct test_options *crtu_get_opts()
{
	return &opts;
}

void
crtu_test_init(d_rank_t rank, int num_attach_retries, bool is_server,
	       bool assert_on_error)
{
	opts.is_initialized	= true;
	opts.self_rank		= rank;
	opts.mypid		= getpid();
	opts.is_server		= is_server;
	opts.num_attach_retries	= num_attach_retries;
	opts.assert_on_error	= assert_on_error;
	opts.shutdown		= 0;
	opts.is_swim_enabled	= false;
	opts.use_daos_agent_env	= false;

	/* Use 2 second delay as a default for all tests for now */
	opts.delay_shutdown_sec	= 2;
}

static inline int
crtu_drain_queue(crt_context_t ctx)
{
	int	rc;
	int	i;

	/* TODO: Need better mechanism for tests to drain all queues */
	for (i = 0; i < 1000; i++)
		crt_progress(ctx, 1000);

	/* Drain the queue. Progress until 1 second timeout.  We need
	 * a more robust method
	 */
	do {
		rc = crt_progress(ctx, 1000000);
		if (rc != 0 && rc != -DER_TIMEDOUT) {
			D_ERROR("crt_progress failed rc: %d.\n", rc);
			return rc;
		}

		if (rc == -DER_TIMEDOUT)
			break;
	} while (1);

	D_DEBUG(DB_TEST, "Done draining queue\n");
	return 0;
}

void
crtu_set_shutdown_delay(int delay_sec)
{
	opts.delay_shutdown_sec = delay_sec;
}

void
crtu_progress_stop(void)
{
	opts.shutdown = 1;
}

/* Write a completion file to signal graceful server shutdown */
void
write_completion_file(void)
{
	FILE	*fptr;
	char	*dir;
	char	*completion_file = NULL;

	dir = getenv("DAOS_TEST_SHARED_DIR");
	D_ASSERTF(dir != NULL,
		"DAOS_TEST_SHARED_DIR must be set for --write_completion_file "
		"option.\n");
	D_ASPRINTF(completion_file, "%s/test-servers-completed.txt.%d", dir, getpid());
	D_ASSERTF(completion_file != NULL, "Error allocating completion_file string\n");

	unlink(completion_file);
	fptr = fopen(completion_file, "w");
	D_ASSERTF(fptr != NULL, "Error opening completion file for writing.\n");
	DBG_PRINT("Wrote completion file: %s.\n", completion_file);
	fclose(fptr);
	D_FREE(completion_file);
}

void *
crtu_progress_fn(void *data)
{
	int		rc;
	int		idx = -1;
	crt_context_t	*p_ctx = (crt_context_t *)data;

	D_ASSERTF(opts.is_initialized == true, "crtu_test_init not called.\n");

	rc = crt_context_idx(*p_ctx, &idx);
	if (rc != 0) {
		D_ERROR("crt_context_idx() failed; rc=%d\n", rc);
		assert(0);
	}

	while (opts.shutdown == 0)
		crt_progress(*p_ctx, 1000);

	if (opts.is_server) {
		if (opts.is_swim_enabled && idx == 0)
			crt_swim_disable_all();

		rc = crtu_drain_queue(*p_ctx);
		D_ASSERTF(rc == 0, "crtu_drain_queue() failed with rc=%d\n", rc);

		if (opts.delay_shutdown_sec > 0)
			sleep(opts.delay_shutdown_sec);
	}

	rc = crt_context_destroy(*p_ctx, 1);
	D_ASSERTF(rc == 0, "Failed to destroy context %p rc=%d\n", p_ctx, rc);

	pthread_exit(rc ? *p_ctx : NULL);

	return NULL;
}

static void
ctl_client_cb(const struct crt_cb_info *info)
{
	struct wfr_status		*wfrs;
	struct crt_ctl_ep_ls_out	*out_ls_args;
	char				*addr_str;
	int				 i;

	wfrs = (struct wfr_status *)info->cci_arg;

	if (info->cci_rc == 0) {
		out_ls_args = crt_reply_get(info->cci_rpc);
		wfrs->num_ctx = out_ls_args->cel_ctx_num;
		wfrs->rc = out_ls_args->cel_rc;

		D_DEBUG(DB_TEST, "ctx_num: %d\n",
			out_ls_args->cel_ctx_num);
		addr_str = out_ls_args->cel_addr_str.iov_buf;
		for (i = 0; i < out_ls_args->cel_ctx_num; i++) {
			D_DEBUG(DB_TEST, "    %s\n", addr_str);
				addr_str += (strlen(addr_str) + 1);
		}
	} else {
		wfrs->rc = info->cci_rc;
	}

	sem_post(&wfrs->sem);
}

static inline void
crtu_sync_timedwait(struct wfr_status *wfrs, int sec, int line_number)
{
	struct timespec	deadline;
	int		rc;

	rc = clock_gettime(CLOCK_REALTIME, &deadline);
	if (opts.assert_on_error) {
		D_ASSERTF(rc == 0, "clock_gettime() failed at line %d "
			  "rc: %d\n",
			  line_number, rc);
	} else {
		wfrs->rc = rc;
	}

	deadline.tv_sec += sec;

	rc = sem_timedwait(&wfrs->sem, &deadline);
	if (opts.assert_on_error) {
		D_ASSERTF(rc == 0, "Sync timed out at line %d rc: %d\n",
			  line_number, rc);
	} else {
		wfrs->rc = rc;
	}
}

int
crtu_wait_for_ranks(crt_context_t ctx, crt_group_t *grp,
		    d_rank_list_t *rank_list, int tag, int total_ctx,
		    double ping_timeout, double total_timeout)
{
	struct wfr_status		ws;
	struct timespec			t1, t2;
	struct crt_ctl_ep_ls_in		*in_args;
	d_rank_t			rank;
	crt_rpc_t			*rpc = NULL;
	crt_endpoint_t			server_ep;
	double				time_s = 0;
	int				i = 0;
	int				rc = 0;

	D_ASSERTF(opts.is_initialized == true, "crtu_test_init not called.\n");

	rc = d_gettime(&t1);
	D_ASSERTF(rc == 0, "d_gettime() failed; rc=%d\n", rc);

	rc = sem_init(&ws.sem, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed; rc=%d\n", rc);

	server_ep.ep_tag = tag;
	server_ep.ep_grp = grp;

	for (i = 0; i < rank_list->rl_nr; i++) {
		rank = rank_list->rl_ranks[i];
		server_ep.ep_rank = rank;

		rc = crt_req_create(ctx, &server_ep, CRT_OPC_CTL_LS, &rpc);
		D_ASSERTF(rc == 0, "crt_req_create failed; rc=%d\n", rc);

		in_args = crt_req_get(rpc);
		in_args->cel_grp_id = grp->cg_grpid;
		in_args->cel_rank = rank;

		rc = crt_req_set_timeout(rpc, ping_timeout);
		D_ASSERTF(rc == 0, "crt_req_set_timeout failed; rc=%d\n", rc);

		ws.rc = 0;
		ws.num_ctx = 0;

		rc = crt_req_send(rpc, ctl_client_cb, &ws);

		if (rc == 0)
			crtu_sync_timedwait(&ws, 120, __LINE__);
		else
			ws.rc = rc;

		while (ws.rc != 0 && time_s < total_timeout) {
			rc = crt_req_create(ctx, &server_ep,
					    CRT_OPC_CTL_LS, &rpc);
			D_ASSERTF(rc == 0,
				  "crt_req_create failed; rc=%d\n", rc);

			in_args = crt_req_get(rpc);
			in_args->cel_grp_id = grp->cg_grpid;
			in_args->cel_rank = rank;

			rc = crt_req_set_timeout(rpc, ping_timeout);
			D_ASSERTF(rc == 0,
				  "crt_req_set_timeout failed; rc=%d\n", rc);

			ws.rc = 0;
			ws.num_ctx = 0;

			rc = crt_req_send(rpc, ctl_client_cb, &ws);

			if (rc == 0)
				crtu_sync_timedwait(&ws, 120, __LINE__);
			else
				ws.rc = rc;

			rc = d_gettime(&t2);
			D_ASSERTF(rc == 0, "d_gettime() failed; rc=%d\n", rc);
			time_s = d_time2s(d_timediff(t1, t2));

			if (ws.rc != 0 && time_s < total_timeout)
				sleep(1);
		}

		if (ws.rc != 0) {
			rc = ws.rc;
			break;
		}

		if (ws.num_ctx < total_ctx) {
			rc = -1;
			break;
		}
	}

	sem_destroy(&ws.sem);

	return rc;
}

int
crtu_load_group_from_file(const char *grp_cfg_file, crt_context_t ctx,
			  crt_group_t *grp, d_rank_t my_rank,
			  bool delete_file)
{
	FILE		*f;
	int		parsed_rank;
	char		parsed_addr[255];
	int		rc = 0;

	D_ASSERTF(opts.is_initialized == true, "crtu_test_init not called.\n");

	if (grp_cfg_file == NULL) {
		D_ERROR("No config filename was passed\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	f = fopen(grp_cfg_file, "r");
	if (!f) {
		D_ERROR("Failed to open %s for reading\n", grp_cfg_file);
		D_GOTO(out, rc = -DER_NONEXIST);
	}

	while (1) {
		rc = fscanf(f, "%8d %254s", &parsed_rank, parsed_addr);
		if (rc == EOF) {
			rc = 0;
			break;
		}

		if (parsed_rank == my_rank)
			continue;

		rc = crt_group_primary_rank_add(ctx, grp,
						parsed_rank, parsed_addr);

		if (rc != 0) {
			D_ERROR("Failed to add %d %s; rc=%d\n",
				parsed_rank, parsed_addr, rc);
			break;
		}
	}

	fclose(f);

	if (delete_file)
		unlink(grp_cfg_file);

out:
	return rc;
}

#define SYS_INFO_BUF_SIZE 16

int
crtu_dc_mgmt_net_cfg_rank_add(const char *name, crt_group_t *group,
		    crt_context_t *context)
{
	int				  i;
	int				  rc = 0;
	struct dc_mgmt_sys_info		  crt_net_cfg_info = {0};
	Mgmt__GetAttachInfoResp		 *crt_net_cfg_resp = NULL;
	Mgmt__GetAttachInfoResp__RankUri *rank_uri;

	/* Query the agent for the CaRT network configuration parameters */
	rc = dc_get_attach_info(name,
				true /* all_ranks */,
				&crt_net_cfg_info,
				&crt_net_cfg_resp);
	if (opts.assert_on_error)
		D_ASSERTF(rc == 0, "dc_get_attach_info() failed, rc=%d\n", rc);

	if (rc != 0) {
		D_ERROR("dc_get_attach_info() failed, rc=%d\n", rc);
		D_GOTO(err_group, rc);
	}

	for (i = 0; i < crt_net_cfg_resp->n_rank_uris; i++) {
		rank_uri = crt_net_cfg_resp->rank_uris[i];

		rc = crt_group_primary_rank_add(context, group,
						rank_uri->rank,
						rank_uri->uri);

		if (rc != 0) {
			D_ERROR("failed to add rank %u URI %s to group %s: "
				DF_RC"\n",
				rank_uri->rank,
				rank_uri->uri,
				name,
				DP_RC(rc));
			goto err_group;
		}

		D_INFO("rank: %d uri: %s\n", rank_uri->rank, rank_uri->uri);
	}

err_group:
	dc_put_attach_info(&crt_net_cfg_info, crt_net_cfg_resp);

	return rc;
}

int
crtu_dc_mgmt_net_cfg_setenv(const char *name)
{
	int			 rc;
	char			 buf[SYS_INFO_BUF_SIZE];
	char			*crt_timeout;
	char			*ofi_interface;
	char			*ofi_domain;
	char			*cli_srx_set;
	struct dc_mgmt_sys_info  crt_net_cfg_info = {0};
	Mgmt__GetAttachInfoResp *crt_net_cfg_resp = NULL;

	/* Query the agent for the CaRT network configuration parameters */
	rc = dc_get_attach_info(name, true /* all_ranks */,
				&crt_net_cfg_info, &crt_net_cfg_resp);
	if (opts.assert_on_error)
		D_ASSERTF(rc == 0, "dc_get_attach_info() failed, rc=%d\n", rc);

	if (rc != 0) {
		D_ERROR("dc_get_attach_info() failed, rc=%d\n", rc);
		D_GOTO(cleanup, rc);
	}

	/* These two are always set */
	D_INFO("setenv CRT_PHY_ADDR_STR=%s\n", crt_net_cfg_info.provider);
	rc = setenv("CRT_PHY_ADDR_STR", crt_net_cfg_info.provider, 1);
	if (rc != 0)
		D_GOTO(cleanup, rc = d_errno2der(errno));

	sprintf(buf, "%d", crt_net_cfg_info.crt_ctx_share_addr);
	D_INFO("setenv CRT_CTX_SHARE_ADDR=%d\n", crt_net_cfg_info.crt_ctx_share_addr);
	rc = setenv("CRT_CTX_SHARE_ADDR", buf, 1);
	if (rc != 0)
		D_GOTO(cleanup, rc = d_errno2der(errno));

	/* If the server has set this, the client must use the same value. */
	if (crt_net_cfg_info.srv_srx_set != -1) {
		sprintf(buf, "%d", crt_net_cfg_info.srv_srx_set);
		rc = setenv("FI_OFI_RXM_USE_SRX", buf, 1);
		D_INFO("setenv FI_OFI_RXM_USE_SRX=%d\n", crt_net_cfg_info.srv_srx_set);
		if (rc != 0)
			D_GOTO(cleanup, rc = d_errno2der(errno));

		D_DEBUG(DB_MGMT, "Using server's value for FI_OFI_RXM_USE_SRX: %s\n", buf);
	} else {
		/* Client may not set it if the server hasn't. */
		cli_srx_set = getenv("FI_OFI_RXM_USE_SRX");
		if (cli_srx_set) {
			D_ERROR("Client set FI_OFI_RXM_USE_SRX to %s, "
				"but server is unset!\n", cli_srx_set);
			D_GOTO(cleanup, rc = -DER_INVAL);
		}
	}

	/* Allow client env overrides for these three */
	crt_timeout = getenv("CRT_TIMEOUT");
	if (!crt_timeout) {
		sprintf(buf, "%d", crt_net_cfg_info.crt_timeout);
		rc = setenv("CRT_TIMEOUT", buf, 1);
		D_INFO("setenv CRT_TIMEOUT=%d\n", crt_net_cfg_info.crt_timeout);
		if (rc != 0)
			D_GOTO(cleanup, rc = d_errno2der(errno));
	} else {
		D_DEBUG(DB_MGMT, "Using client provided CRT_TIMEOUT: %s\n", crt_timeout);
	}

	ofi_interface = getenv("OFI_INTERFACE");
	if (!ofi_interface) {
		rc = setenv("OFI_INTERFACE", crt_net_cfg_info.interface, 1);
		D_INFO("Setting OFI_INTERFACE=%s\n", crt_net_cfg_info.interface);
		if (rc != 0)
			D_GOTO(cleanup, rc = d_errno2der(errno));
	} else {
		D_DEBUG(DB_MGMT,
			"Using client provided OFI_INTERFACE: %s\n",
			ofi_interface);
	}

	ofi_domain = getenv("OFI_DOMAIN");
	if (!ofi_domain) {
		rc = setenv("OFI_DOMAIN", crt_net_cfg_info.domain, 1);
		D_INFO("Setting OFI_DOMAIN=%s\n", crt_net_cfg_info.domain);
		if (rc != 0)
			D_GOTO(cleanup, rc = d_errno2der(errno));
	} else {
		D_DEBUG(DB_MGMT, "Using client provided OFI_DOMAIN: %s\n", ofi_domain);
	}

	D_INFO("CaRT env setup with:\n"
		"\tOFI_INTERFACE=%s, OFI_DOMAIN: %s, CRT_PHY_ADDR_STR: %s, "
		"CRT_CTX_SHARE_ADDR: %s, CRT_TIMEOUT: %s\n",
		getenv("OFI_INTERFACE"), getenv("OFI_DOMAIN"),
		getenv("CRT_PHY_ADDR_STR"),
		getenv("CRT_CTX_SHARE_ADDR"), getenv("CRT_TIMEOUT"));

cleanup:
	dc_put_attach_info(&crt_net_cfg_info, crt_net_cfg_resp);

	return rc;
}

int
crtu_cli_start_basic(char *local_group_name, char *srv_group_name,
		     crt_group_t **grp, d_rank_list_t **rank_list,
		     crt_context_t *crt_ctx, pthread_t *progress_thread,
		     unsigned int total_srv_ctx, bool use_cfg,
		     crt_init_options_t *init_opt, bool use_daos_agent_env)
{
	char		*grp_cfg_file;
	uint32_t	 grp_size;
	int		 rc = 0;

	if (opts.assert_on_error)
		D_ASSERTF(opts.is_initialized == true, "crtu_test_init not called.\n");

	rc = d_log_init();
	if (rc != 0)
		D_GOTO(out, rc);

	if (use_daos_agent_env) {
		rc = crtu_dc_mgmt_net_cfg_setenv(srv_group_name);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	if (init_opt)
		rc = crt_init_opt(local_group_name, 0, init_opt);
	else
		rc = crt_init(local_group_name, 0);

	if (rc != 0)
		D_GOTO(out, rc);

	rc = crt_context_create(crt_ctx);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = pthread_create(progress_thread, NULL, crtu_progress_fn, crt_ctx);
	if (rc != 0)
		D_GOTO(out, rc);

	if (!use_daos_agent_env) {
		if (use_cfg) {
			/*
			 * DAOS-8839: change retries to infinite to allow valgrind
			 * enough time to start servers up. Instead rely on test
			 * timeout for cases when attach file is not there due to
			 * server bug/issue.
			 */
			while (1) {
				rc = crt_group_attach(srv_group_name, grp);
				if (rc == 0)
					break;
				sleep(1);
			}
			if (grp == NULL)
				D_GOTO(out, rc = -DER_INVAL);

		} else {
			rc = crt_group_view_create(srv_group_name, grp);
			if (rc != 0)
				D_GOTO(out, rc);

			if (*grp == NULL)
				D_GOTO(out, rc = -DER_INVAL);

			grp_cfg_file = getenv("CRT_L_GRP_CFG");

			/* load group info from a config file and
			 * delete file upon return
			 */
			rc = crtu_load_group_from_file(grp_cfg_file,
						       *crt_ctx, *grp,
						       -1, true);
			if (rc != 0)
				D_GOTO(out, rc);
		}
	} else {
		rc = crt_group_view_create(srv_group_name, grp);
		if (rc != 0)
			D_GOTO(out, rc);

		if (*grp == NULL)
			D_GOTO(out, rc = -DER_INVAL);

		rc = crtu_dc_mgmt_net_cfg_rank_add(srv_group_name,
						   *grp, *crt_ctx);
		if (rc != 0) {
			crt_group_view_destroy(*grp);
			D_GOTO(out, rc);
		}
	}

	rc = crt_group_size(*grp, &grp_size);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = crt_group_ranks_get(*grp, rank_list);
	if (rc != 0)
		D_GOTO(out, rc);

	if (!*rank_list) {
		D_ERROR("Rank list is NULL\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if ((*rank_list)->rl_nr != grp_size) {
		D_ERROR("rank_list differs in size. expected %d got %d\n",
			grp_size, (*rank_list)->rl_nr);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if ((*rank_list)->rl_nr == 0) {
		D_ERROR("Rank list is empty\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = crt_group_psr_set(*grp, (*rank_list)->rl_ranks[0]);
	if (rc != 0)
		D_GOTO(out, rc);

out:
	if (rc != 0 && opts.assert_on_error) {
		D_ERROR("Asserting due to an error\n");
		assert(0);
	}

	return rc;
}

int
crtu_srv_start_basic(char *srv_group_name, crt_context_t *crt_ctx,
		     pthread_t *progress_thread, crt_group_t **grp,
		     uint32_t *grp_size, crt_init_options_t *init_opt)
{
	char		*env_self_rank;
	char		*grp_cfg_file;
	char		*my_uri;
	d_rank_t	 my_rank;
	int		 rc = 0;

	if (opts.assert_on_error)
		D_ASSERTF(opts.is_initialized == true, "crtu_test_init not called.\n");

	env_self_rank = getenv("CRT_L_RANK");
	my_rank = atoi(env_self_rank);

	rc = d_log_init();
	if (rc != 0)
		D_GOTO(out, rc);

	if (init_opt) {
		rc = crt_init_opt(srv_group_name, CRT_FLAG_BIT_SERVER |
				  CRT_FLAG_BIT_AUTO_SWIM_DISABLE, init_opt);
	} else {
		rc = crt_init(srv_group_name, CRT_FLAG_BIT_SERVER |
			      CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	}

	if (rc != 0)
		D_GOTO(out, rc);

	*grp = crt_group_lookup(NULL);
	if (!(*grp)) {
		D_ERROR("Group lookup failed\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = crt_rank_self_set(my_rank, 1 /* group_version_min */);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = crt_context_create(crt_ctx);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = pthread_create(progress_thread, NULL, crtu_progress_fn, crt_ctx);
	if (rc != 0)
		D_GOTO(out, rc);

	if (opts.is_swim_enabled) {
		rc = crt_swim_init(0);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	grp_cfg_file = getenv("CRT_L_GRP_CFG");

	rc = crt_rank_uri_get(*grp, my_rank, 0, &my_uri);
	if (rc != 0)
		D_GOTO(out, rc);

	/* load group info from a config file and delete file upon return */
	rc = crtu_load_group_from_file(grp_cfg_file, crt_ctx[0], *grp, my_rank, true);
	if (rc != 0)
		D_GOTO(out, rc);

	D_FREE(my_uri);

	rc = crt_group_size(NULL, grp_size);
	if (rc != 0)
		D_GOTO(out, rc);

out:
	if (opts.assert_on_error && rc != 0) {
		D_ERROR("Failed to start server. Asserting\n");
		assert(0);
	}

	return rc;
}

struct crtu_log_msg_cb_resp {
	sem_t	sem;
};

static void
crtu_log_msg_cb(const struct crt_cb_info *info)
{
	struct crtu_log_msg_cb_resp	*resp;

	if (info->cci_rc != 0) {
		D_WARN("Add Log message CB failed\n");
		D_ASSERTF(info->cci_rc == 0,
			  "Send Log RPC did not respond\n");
	}
	resp = (struct crtu_log_msg_cb_resp *)info->cci_arg;
	sem_post(&resp->sem);
}

int
crtu_log_msg(crt_context_t ctx, crt_group_t *grp, d_rank_t rank, char *msg)
{
	int32_t				 rc = 0;
	struct crt_ctl_log_add_msg_in	*send_args;
	crt_rpc_t			*rpc_req = NULL;
	crt_endpoint_t			 ep;
	crt_opcode_t			 opcode = CRT_OPC_CTL_LOG_ADD_MSG;
	struct crtu_log_msg_cb_resp	 resp;

	/* Initialize response structure */
	rc = sem_init(&resp.sem, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed\n");

	/* Fill in the endpoint info */
	ep.ep_grp = grp;
	ep.ep_rank = rank;
	ep.ep_tag = 0;

	rc = crt_req_create(ctx, &ep, opcode, &rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create() failed. rc %d.\n", rc);
		D_GOTO(exit, rc);
	}

	crt_req_addref(rpc_req);
	send_args =  crt_req_get(rpc_req);
	send_args->log_msg = msg;

	/* send the request */
	rc = crt_req_send(rpc_req, crtu_log_msg_cb, &resp);
	if (rc < 0) {
		D_WARN("rpc failed, message: \"%s \"not sent\n", msg);
		goto cleanup;
	}

	/* Wait for response */
	rc = crtu_sem_timedwait(&resp.sem, 30, __LINE__);
	if (rc < 0) {
		D_WARN("Messaage logged timed out: %s\n", msg);
		crt_req_abort(rpc_req);
		goto cleanup;
	}

	/* Decrement reference */
cleanup:
	crt_req_decref(rpc_req);
exit:
	D_INFO("Return code %d\n", rc);
	return rc;
}

void
crtu_test_swim_enable(bool is_swim_enabled)
{
	opts.is_swim_enabled = is_swim_enabled;
}

int
crtu_sem_timedwait(sem_t *sem, int sec, int line_number)
{
	struct timespec		deadline;
	int			rc;

	rc = clock_gettime(CLOCK_REALTIME, &deadline);
	if (rc != 0) {
		if (opts.assert_on_error)
			D_ASSERTF(rc == 0, "clock_gettime() failed at "
				  "line %d rc: %d\n", line_number, rc);
		D_ERROR("clock_gettime() failed, rc = %d\n", rc);
		D_GOTO(out, rc);
	}

	deadline.tv_sec += sec;
	rc = sem_timedwait(sem, &deadline);
	if (rc != 0) {
		if (opts.assert_on_error)
			D_ASSERTF(rc == 0, "sem_timedwait() failed at "
				  "line %d rc: %d\n", line_number, rc);
		D_ERROR("sem_timedwait() failed, rc = %d\n", rc);
		D_GOTO(out, rc);
	}
out:
	return rc;
}
