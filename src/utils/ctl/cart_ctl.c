/*
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It implements client side of the cart_ctl command
 * line utility.
 */
#define D_LOGFAC	DD_FAC(ctl)

#include <stdio.h>
#include <pthread.h>
#include <getopt.h>
#include <semaphore.h>

/* for crt_register_proto_fi() */
#include "crt_internal.h"
#include "crt_utils.h"
#include <daos/agent.h>
#include <daos/mgmt.h>
#include "svc.pb-c.h"


/* max number of ranks that can be queried at once */
#define CRT_CTL_MAX		1024
#define MAX_ARG_LEN		(1 << 16)

#define error_exit(x...)	\
do {				\
	fprintf(stderr, x);	\
	exit(-1);		\
} while (0)

#define error_warn(x...)	fprintf(stderr, x)
#define msg(x...)		fprintf(stdout, x)

enum cmd_t {
	CMD_GET_URI_CACHE,
	CMD_LIST_CTX,
	CMD_GET_HOSTNAME,
	CMD_GET_PID,
	CMD_ENABLE_FI,
	CMD_DISABLE_FI,
	CMD_SET_FI_ATTR,
	CMD_LOG_SET,
	CMD_LOG_ADD_MSG,
};

struct cmd_info {
	enum cmd_t	cmd;
	int		opcode;
	char		*cmd_str;
};

/* Helper macro to fill out cmd_info struct entry */
#define DEF_CMD(cmd, opc)	{cmd, opc, #cmd}

struct cmd_info cmds[] = {
	DEF_CMD(CMD_LIST_CTX,		CRT_OPC_CTL_LS),
	DEF_CMD(CMD_GET_URI_CACHE,	CRT_OPC_CTL_GET_URI_CACHE),
	DEF_CMD(CMD_GET_HOSTNAME,	CRT_OPC_CTL_GET_HOSTNAME),
	DEF_CMD(CMD_GET_PID,		CRT_OPC_CTL_GET_PID),
	DEF_CMD(CMD_ENABLE_FI,		CRT_OPC_CTL_FI_TOGGLE),
	DEF_CMD(CMD_DISABLE_FI,		CRT_OPC_CTL_FI_TOGGLE),
	DEF_CMD(CMD_SET_FI_ATTR,	CRT_OPC_CTL_FI_SET_ATTR),
	DEF_CMD(CMD_LOG_SET,		CRT_OPC_CTL_LOG_SET),
	DEF_CMD(CMD_LOG_ADD_MSG,	CRT_OPC_CTL_LOG_ADD_MSG),
};

static char*
cmd2str(enum cmd_t cmd)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cmds); i++) {
		if (cmd == cmds[i].cmd)
			return cmds[i].cmd_str;
	}

	return "Unknown cmd";
}

static int
cmd2opcode(enum cmd_t cmd)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cmds); i++) {
		if (cmd == cmds[i].cmd)
			return cmds[i].opcode;
	}

	return -1;
}

struct cb_info {
	enum cmd_t cmd;
};

struct ctl_g {
	enum cmd_t			 cg_cmd_code;
	char				*cg_group_name;
	crt_group_t			*cg_target_group;
	int				 cg_num_ranks;
	d_rank_t			 cg_ranks[CRT_CTL_MAX];
	crt_context_t			 cg_crt_ctx;
	pthread_t			 cg_tid;
	int				 cg_complete;
	bool				 cg_save_cfg;
	char				*cg_cfg_path;
	sem_t				 cg_num_reply;
	struct crt_ctl_fi_attr_set_in	 cg_fi_attr;
	int				 cg_fi_attr_inited;
	char				*cg_log_mask;
	int				 cg_log_mask_set;
	bool				 cg_no_wait_for_ranks;
	char				*cg_log_msg;
	bool				 cg_log_msg_set;
	bool				 cg_use_daos_agent_env;
};

static struct ctl_g ctl_gdata;

static void
parse_rank_string(char *arg_str, d_rank_t *ranks, int *num_ranks)
{
	char		*token;
	char		*saveptr;
	char		*ptr;
	uint32_t	 num_ranks_l = 0;
	uint32_t	 index = 0;
	int		 rstart;
	int		 rend;
	int		 i;

	D_ASSERT(ranks != NULL);
	D_ASSERT(num_ranks != NULL);
	D_ASSERT(arg_str != NULL);

	if (strnlen(arg_str, MAX_ARG_LEN) >= MAX_ARG_LEN) {
		error_exit("arg string too long.\n");
		return;
	}

	if (strcmp(arg_str, "all") == 0) {
		*num_ranks = -1;
		return;
	}

	D_DEBUG(DB_TRACE, "arg_str %s\n", arg_str);

	token = strtok_r(arg_str, ",", &saveptr);
	while (token != NULL) {

		ptr = strchr(token, '-');

		if (ptr == NULL) {
			num_ranks_l++;
			if (num_ranks_l > CRT_CTL_MAX)
				error_exit("Too many target ranks.\n");

			ranks[index] = atoi(token);
			index++;
			token = strtok_r(NULL, ",", &saveptr);
			continue;
		}

		if (ptr == token || ptr == token + strlen(token))
			error_exit("Invalid rank range.\n");

		rstart = atoi(token);
		rend = atoi(ptr + 1);
		num_ranks_l += (rend - rstart + 1);

		if (num_ranks_l > CRT_CTL_MAX)
			error_exit("Too many target ranks.\n");

		for (i = rstart; i < rend + 1; i++) {
			ranks[index] = i;
			index++;
		}

		token = strtok_r(NULL, ",", &saveptr);
	}

	*num_ranks = num_ranks_l;
}

static void
ctl_parse_fi_attr(char *arg_str, struct crt_ctl_fi_attr_set_in *fi_attr_in)
{
	char *token;
	char *endptr;
	char *saveptr;

	D_ASSERTF(arg_str != NULL, "arg_str is NULL.\n");
	D_ASSERTF(fi_attr_in != NULL, "fi_attr_in is NULL.\n");

	if (strnlen(arg_str, MAX_ARG_LEN) >= MAX_ARG_LEN)
		error_exit("attribute string too long (max=%d)\n", MAX_ARG_LEN);

	D_DEBUG(DB_TRACE, "arg_str %s\n", arg_str);

	token = strtok_r(arg_str, ",", &saveptr);
	if (token == NULL)
		D_GOTO(error_out, 0);

	fi_attr_in->fa_fault_id = strtoull(token, &endptr, 10);

	/* get max_faults */
	token = strtok_r(NULL, ",", &saveptr);
	if (token == NULL)
		D_GOTO(error_out, 0);

	fi_attr_in->fa_max_faults = strtoull(token, &endptr, 10);

	token = strtok_r(NULL, ",", &saveptr);
	if (token == NULL)
		D_GOTO(error_out, 0);

	fi_attr_in->fa_probability_x = strtoull(token, &endptr, 10);

	/* Workaround for DAOS-13900, make probability be a percentage */
	if (fi_attr_in->fa_probability_x != 0)
		fi_attr_in->fa_probability_y = 1000;

	token = strtok_r(NULL, ",", &saveptr);
	if (token == NULL)
		D_GOTO(error_out, 0);

	fi_attr_in->fa_err_code = strtoull(token, &endptr, 10);

	token = strtok_r(NULL, ",", &saveptr);
	if (token == NULL)
		D_GOTO(error_out, 0);

	fi_attr_in->fa_interval = strtoull(token, &endptr, 10);

	token = strtok_r(NULL, ",", &saveptr);
	if (token == NULL)
		return;

	fi_attr_in->fa_argument = token;
	return;

error_out:
	error_exit("Error: --attr has wrong number of arguments, should "
		   "be \t--attr fault_id,max_faults,probability,err_code\n");
	return;
}

static void
print_usage_msg(const char *msg)
{
	if (msg)
		printf("\nERROR: %s\n", msg);

	printf("Usage: cart_ctl <cmd> --group-name name --rank "
	       "start-end,start-end,rank,rank\n");
	printf("\ncmds: get_uri_cache, list_ctx, get_hostname, get_pid, ");
	printf("set_log, set_fi_attr, add_log_msg\n");
	printf("\nset_log:\n");
	printf("\tSet log to mask passed via -l <mask> argument\n");
	printf("\nget_uri_cache:\n");
	printf("\tPrint rank, tag and uri from uri cache\n");
	printf("\nlist_ctx:\n");
	printf("\tPrint # of contexts on each rank and uri for each context\n");
	printf("\nget_hostname:\n");
	printf("\tPrint hostnames of specified ranks\n");
	printf("\nget_pid:\n");
	printf("\tReturn pids of the specified ranks\n");
	printf("\nset_fi_attr\n");
	printf("\tset fault injection attributes for a fault ID. This command\n"
	       "\tmust be acompanied by the option\n"
	       "\t--attr fault_id,max_faults,probability,err_code"
	       "[,argument]\n");
	printf("\noptions:\n");
	printf("--group-name name\n");
	printf("\tspecify the name of the remote group\n");
	printf("--cfg_path path\n");
	printf("\tPath to group config file\n");
	printf("--rank start-end,start-end,rank,rank\n");
	printf("\tspecify target ranks; 'all' specifies every known rank\n");
	printf("-l log_mask\n");
	printf("\tSpecify log_mask to be set remotely\n");
	printf("-n\n");
	printf("\tdon't perform 'wait for ranks' sync\n");
	printf("-m 'log_message'\n");
	printf("\tSpecify log message to be sent to remote server\n");
	printf("--use_daos_agent_env\n");
	printf("\tSet OFI and CRT_* vars through daos_agent\n\n");
}

static int
parse_args(int argc, char **argv)
{
	int	option_index = 0;
	int	opt;
	int	rc = 0;

	ctl_gdata.cg_use_daos_agent_env = false;

	if (argc <= 2) {
		print_usage_msg(NULL);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (strcmp(argv[1], "get_uri_cache") == 0)
		ctl_gdata.cg_cmd_code = CMD_GET_URI_CACHE;
	else if (strcmp(argv[1], "list_ctx") == 0)
		ctl_gdata.cg_cmd_code = CMD_LIST_CTX;
	else if (strcmp(argv[1], "get_hostname") == 0)
		ctl_gdata.cg_cmd_code = CMD_GET_HOSTNAME;
	else if (strcmp(argv[1], "get_pid") == 0)
		ctl_gdata.cg_cmd_code = CMD_GET_PID;
	else if (strcmp(argv[1], "enable_fi") == 0)
		ctl_gdata.cg_cmd_code = CMD_ENABLE_FI;
	else if (strcmp(argv[1], "disable_fi") == 0)
		ctl_gdata.cg_cmd_code = CMD_DISABLE_FI;
	else if (strcmp(argv[1], "set_fi_attr") == 0)
		ctl_gdata.cg_cmd_code = CMD_SET_FI_ATTR;
	else if (strcmp(argv[1], "set_log") == 0)
		ctl_gdata.cg_cmd_code = CMD_LOG_SET;
	else if (strcmp(argv[1], "add_log_msg") == 0)
		ctl_gdata.cg_cmd_code = CMD_LOG_ADD_MSG;
	else if (strcmp(argv[1], "use_daos_agent_env") == 0)
		ctl_gdata.cg_use_daos_agent_env = true;
	else {
		print_usage_msg("Invalid command\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	optind = 2;

	static struct option long_options[] = {
		{"group-name", required_argument, 0, 'g'},
		{"rank", required_argument, 0, 'r'},
		{"attr", required_argument, 0, 'a'},
		{"cfg_path", required_argument, 0, 's'},
		{"log_mask", required_argument, 0, 'l'},
		{"no_sync", optional_argument, 0, 'n'},
		{"message", required_argument, 0, 'm'},
		{"use_daos_agent_env", no_argument, 0, 'u'},
		{0, 0, 0, 0},
	};

	while (1) {
		opt = getopt_long(argc, argv, "g:r:a:p:l:m:nu", long_options,
				  &option_index);
		if (opt == -1)
			break;
		switch (opt) {
		case 0:
			if (long_options[option_index].flag != 0)
				break;
		case 'g':
			ctl_gdata.cg_group_name = optarg;
			break;
		case 'r':
			parse_rank_string(optarg, ctl_gdata.cg_ranks,
					  &ctl_gdata.cg_num_ranks);
			break;
		case 'a':
			ctl_parse_fi_attr(optarg, &ctl_gdata.cg_fi_attr);
			ctl_gdata.cg_fi_attr_inited = 1;
			break;
		case 's':
			ctl_gdata.cg_save_cfg = true;
			ctl_gdata.cg_cfg_path = optarg;
			break;
		case 'l':
			ctl_gdata.cg_log_mask = optarg;
			ctl_gdata.cg_log_mask_set = 1;
			break;
		case 'n':
			ctl_gdata.cg_no_wait_for_ranks = true;
			break;
		case 'm':
			ctl_gdata.cg_log_msg = optarg;
			ctl_gdata.cg_log_msg_set = true;
			break;
		case 'u':
			ctl_gdata.cg_use_daos_agent_env = true;
			break;
		default:
			break;
		}
	}

	if (ctl_gdata.cg_cmd_code == CMD_LOG_ADD_MSG && !ctl_gdata.cg_log_msg_set)
		error_exit("log msg (-m 'message') missing for add_log_msg\n");

	if (ctl_gdata.cg_cmd_code == CMD_LOG_SET && !ctl_gdata.cg_log_mask_set)
		error_exit("log mask (-l mask) missing for set_log\n");

	if (ctl_gdata.cg_cmd_code == CMD_SET_FI_ATTR && !ctl_gdata.cg_fi_attr_inited)
		error_exit("fault attributes missing for set_fi_attr.\n");

out:
	return rc;
}

static void
print_uri_cache(struct crt_ctl_get_uri_cache_out *out_uri_cache_args)
{
	struct crt_grp_cache	*grp_cache;
	int			 count;
	int			 i;

	count = out_uri_cache_args->cguc_grp_cache.ca_count;

	grp_cache = out_uri_cache_args->cguc_grp_cache.ca_arrays;

	for (i = 0; i < count; i++) {
		fprintf(stdout, "rank = %d, ", grp_cache[i].gc_rank);
		fprintf(stdout, "tag  = %d, ", grp_cache[i].gc_tag);
		fprintf(stdout, "uri  = %s\n", grp_cache[i].gc_uri);
	}
}

static void
ctl_cli_cb(const struct crt_cb_info *cb_info)
{
	struct cb_info		*info;
	int			cmd_rc = 0;
	char			*cmd_str = "";
	int			i = 0;

	info = cb_info->cci_arg;

	cmd_str = cmd2str(info->cmd);

	if (cb_info->cci_rc != 0)
		error_exit("command %s failed with rc=%d\n", cmd_str, cb_info->cci_rc);

	switch (info->cmd) {
	case CMD_ENABLE_FI:
	case CMD_DISABLE_FI:
		cmd_rc = ((struct crt_ctl_fi_toggle_out *)
				crt_reply_get(cb_info->cci_rpc))->rc;
		if (cmd_rc != 0)
			error_exit("%s failed with rc=%d\n", cmd_str, cmd_rc);

		msg("%s completed successfully", cmd_str);
		break;

	case CMD_SET_FI_ATTR:
		cmd_rc = ((struct crt_ctl_fi_attr_set_out *)
				crt_reply_get(cb_info->cci_rpc))->fa_ret;
		if (cmd_rc != 0)
			error_exit("%s failed with rc=%d\n", cmd_str, cmd_rc);

		msg("%s completed successfully", cmd_str);
		break;

	case CMD_LOG_SET:
		cmd_rc = ((struct crt_ctl_log_set_out *)
				crt_reply_get(cb_info->cci_rpc))->rc;
		if (cmd_rc != 0)
			error_exit("%s failed with rc=%d\n", cmd_str, cmd_rc);

		msg("%s completed successfully", cmd_str);
		break;

	case CMD_LOG_ADD_MSG:
		cmd_rc = ((struct crt_ctl_log_add_msg_out *)
				crt_reply_get(cb_info->cci_rpc))->rc;
		if (cmd_rc != 0)
			error_exit("%s failed with rc=%d\n", cmd_str, cmd_rc);

		msg("%s completed successfully", cmd_str);
		break;

	case CMD_GET_URI_CACHE:
		{
			struct crt_ctl_get_uri_cache_out *out;

			out = crt_reply_get(cb_info->cci_rpc);

			if (out->cguc_rc != 0 || cb_info->cci_rc != 0)
				error_exit("get_uri_cache failed\n");

			print_uri_cache(out);
		}
		break;

	case CMD_LIST_CTX:
		{
			char				*addr_str;
			struct crt_ctl_ep_ls_out	*out;

			out = crt_reply_get(cb_info->cci_rpc);
			msg("Number of remote contexts (endpoints): %d\n", out->cel_ctx_num);

			addr_str = out->cel_addr_str.iov_buf;

			for (i = 0; i < out->cel_ctx_num; i++) {
				msg("    %s\n", addr_str);
				addr_str += (strlen(addr_str) + 1);
			}
		}
		break;

	case CMD_GET_HOSTNAME:
		{
			struct crt_ctl_get_host_out *out;

			out = crt_reply_get(cb_info->cci_rpc);
			msg("hostname: %s\n", (char *)out->cgh_hostname.iov_buf);
		}
		break;

	case CMD_GET_PID:
		{
			struct crt_ctl_get_pid_out *out;

			out = crt_reply_get(cb_info->cci_rpc);
			msg("pid: %d\n", out->cgp_pid);
		}
		break;

	default:
		break;
	}

	sem_post(&ctl_gdata.cg_num_reply);
}

/**
 * Fill in RPC arguments to turn on / turn off fault injection on target
 * \param[in/out] rpc_req        pointer to the RPC
 * \param[in] op                 0 means the RPC will disable fault injection on
 *                               the target, 1 means the RPc will enable fault
 *                               injection on the target.
 */

static void
ctl_fill_fi_toggle_rpc_args(crt_rpc_t *rpc_req, int op)
{
	struct crt_ctl_fi_toggle_in	*in_args;

	D_ASSERTF(op == 0 || op == 1, "op should be 0 or 1.\n");

	in_args = crt_req_get(rpc_req);
	in_args->op = op;
}

static void
crt_fill_log_add_msg(crt_rpc_t *rpc_req)
{
	struct crt_ctl_log_add_msg_in	*in_args;

	in_args = crt_req_get(rpc_req);
	in_args->log_msg = ctl_gdata.cg_log_msg;
}

static void
crt_fill_set_log(crt_rpc_t *rpc_req)
{
	struct crt_ctl_log_set_in	*in_args;

	in_args = crt_req_get(rpc_req);

	in_args->log_mask = ctl_gdata.cg_log_mask;
}

static void
ctl_fill_fi_set_attr_rpc_args(crt_rpc_t *rpc_req)
{
	struct crt_ctl_fi_attr_set_in	*in_args;

	in_args = crt_req_get(rpc_req);

	in_args->fa_fault_id = ctl_gdata.cg_fi_attr.fa_fault_id;
	in_args->fa_max_faults = ctl_gdata.cg_fi_attr.fa_max_faults;
	in_args->fa_probability_x = ctl_gdata.cg_fi_attr.fa_probability_x;
	in_args->fa_probability_y = ctl_gdata.cg_fi_attr.fa_probability_y;
	in_args->fa_err_code = ctl_gdata.cg_fi_attr.fa_err_code;
	in_args->fa_interval = ctl_gdata.cg_fi_attr.fa_interval;
}

static void
ctl_fill_rpc_args(crt_rpc_t *rpc_req, int index)
{
	struct crt_ctl_ep_ls_in		*in_args;

	in_args = crt_req_get(rpc_req);

	in_args->cel_grp_id = ctl_gdata.cg_target_group->cg_grpid;
	in_args->cel_rank = ctl_gdata.cg_ranks[index];
}

static int
ctl_register_fi(crt_endpoint_t *ep)
{
	return crt_register_proto_fi(ep);
}

static int
ctl_register_ctl(crt_endpoint_t *ep)
{
	return crt_register_proto_ctl(ep);
}

static int
ctl_init()
{
	int			 i;
	crt_rpc_t		*rpc_req;
	crt_endpoint_t		 ep;
	struct cb_info		 info;
	crt_group_t		*grp = NULL;
	d_rank_t		*ranks_to_send = NULL;
	d_rank_list_t		*rank_list = NULL;
	int			 num_ranks;
	int			 wait_time = 60;
	int			 total_wait = 150;
	int			 rc = 0;

	if (D_ON_VALGRIND) {
		wait_time *= 3;
		total_wait *= 3;
	}

	if (ctl_gdata.cg_save_cfg) {
		rc = crt_group_config_path_set(ctl_gdata.cg_cfg_path);
		if (rc != 0)
			error_exit("Failed to set config path; rc=%d\n", rc);
	}

	rc = crtu_cli_start_basic("crt_ctl", ctl_gdata.cg_group_name, &grp,
				  &rank_list, &ctl_gdata.cg_crt_ctx,
				  &ctl_gdata.cg_tid, 1, true, NULL,
				  ctl_gdata.cg_use_daos_agent_env);
	if (rc != 0)
		error_exit("Failed to start client; rc=%d\n", rc);

	rc = sem_init(&ctl_gdata.cg_num_reply, 0, 0);
	if (rc != 0)
		error_exit("Semaphore init failed; rc=%d\n", rc);

	if (!ctl_gdata.cg_no_wait_for_ranks) {
		rc = crtu_wait_for_ranks(ctl_gdata.cg_crt_ctx, grp, rank_list,
					 0 /* tag */, 1 /* num contexts to query */,
					 wait_time, total_wait);
		if (rc != 0)
			error_exit("Connection timeout; rc=%d\n", rc);
	}

	ctl_gdata.cg_target_group = grp;
	info.cmd = ctl_gdata.cg_cmd_code;

	if (ctl_gdata.cg_num_ranks == -1) {
		num_ranks = rank_list->rl_nr;
		ranks_to_send = rank_list->rl_ranks;
	} else {
		num_ranks = ctl_gdata.cg_num_ranks;
		ranks_to_send = ctl_gdata.cg_ranks;
	}

	ep.ep_grp = grp;
	ep.ep_rank = ranks_to_send[0];
	ep.ep_tag = 0;

	if (ctl_gdata.cg_cmd_code == CMD_SET_FI_ATTR ||
	    ctl_gdata.cg_cmd_code == CMD_DISABLE_FI ||
	    ctl_gdata.cg_cmd_code == CMD_ENABLE_FI) {
		rc = ctl_register_fi(&ep);
		if (rc != -DER_SUCCESS)
			return rc;
	} else {
		rc = ctl_register_ctl(&ep);
		if (rc != -DER_SUCCESS)
			return rc;
	}

	for (i = 0; i < num_ranks; i++) {
		ep.ep_grp = grp;
		ep.ep_rank = ranks_to_send[i];
		ep.ep_tag = 0;
		rc = crt_req_create(ctl_gdata.cg_crt_ctx, &ep,
				    cmd2opcode(info.cmd), &rpc_req);
		if (rc != 0)
			error_exit("Failed to create RPC; rc=%d\n", rc);

		switch (info.cmd) {
		case (CMD_ENABLE_FI):
			ctl_fill_fi_toggle_rpc_args(rpc_req, 1);
			break;
		case (CMD_DISABLE_FI):
			ctl_fill_fi_toggle_rpc_args(rpc_req, 0);
			break;
		case (CMD_SET_FI_ATTR):
			ctl_fill_fi_set_attr_rpc_args(rpc_req);
			break;
		case (CMD_LOG_SET):
			crt_fill_set_log(rpc_req);
			break;
		case (CMD_LOG_ADD_MSG):
			crt_fill_log_add_msg(rpc_req);
			break;
		default:
			ctl_fill_rpc_args(rpc_req, i);
		}

		D_DEBUG(DB_NET, "rpc_req %p rank %d tag %d seq %d\n",
			rpc_req, ep.ep_rank, ep.ep_tag, i);

		rc = crt_req_send(rpc_req, ctl_cli_cb, &info);
		if (rc != 0)
			error_exit("Failed to send RPC; rc=%d\n", rc);

		rc = crtu_sem_timedwait(&ctl_gdata.cg_num_reply, wait_time, __LINE__);
		if (rc != 0)
			error_exit("No response from the server after %d sec; rc=%d\n",
				   wait_time, rc);
	}

	d_rank_list_free(rank_list);

	if (ctl_gdata.cg_save_cfg) {
		rc = crt_group_detach(grp);
		if (rc != 0)
			error_warn("Failed to destroy the group; rc=%d\n", rc);
	} else {
		rc = crt_group_view_destroy(grp);
		if (rc != 0)
			error_warn("Failed to destroy the view; rc=%d\n", rc);
	}

	crtu_progress_stop();

	rc = pthread_join(ctl_gdata.cg_tid, NULL);
	if (rc != 0)
		error_warn("Failed to join the threads; rc=%d\n", rc);

	rc = sem_destroy(&ctl_gdata.cg_num_reply);
	if (rc != 0)
		error_warn("Failed to destroy a semaphore; rc=%d\n", rc);

	rc = crt_finalize();
	if (rc != 0)
		error_warn("Failed to finalize; rc=%d\n", rc);

	if (ctl_gdata.cg_use_daos_agent_env) {
		dc_mgmt_fini();
	}
	d_log_fini();

	return rc;
}

int
main(int argc, char **argv)
{
	int rc = 0;

	rc = d_log_init();
	if (rc != 0)
		error_exit("Failed to init log; rc=%d\n", rc);

	rc = parse_args(argc, argv);
	if (rc != 0)
		error_exit("Failed to parse some arguments\n");

	/* rank, num_attach_retries, is_server, assert_on_error */
	crtu_test_init(0, 40, false, false);

	if (ctl_gdata.cg_use_daos_agent_env) {
		rc = dc_agent_init();
		if (rc != 0)
			error_exit("Failed talking to DAOS Agent; rc=%d\n", rc);
	}

	rc = ctl_init();
	if (rc != 0)
		error_exit("Init failed; rc=%d\n", rc);

	d_log_fini();
	if (rc != 0)
		return 1;
	return 0;
}
