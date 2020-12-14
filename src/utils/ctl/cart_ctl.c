/*
 * (C) Copyright 2018-2020 Intel Corporation.
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
 * This file is part of CaRT. It implements client side of the cart_ctl command
 * line utility.
 */
#define D_LOGFAC	DD_FAC(ctl)

#include <stdio.h>
#include <pthread.h>
#include <getopt.h>
#include <semaphore.h>

#include "tests_common.h"

/* max number of ranks that can be queried at once */
#define CRT_CTL_MAX		1024
#define CRT_CTL_MAX_ARG_STR_LEN (1 << 16)

int crt_ctl_logfac;

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
	DEF_CMD(CMD_LIST_CTX, CRT_OPC_CTL_LS),
	DEF_CMD(CMD_GET_URI_CACHE, CRT_OPC_CTL_GET_URI_CACHE),
	DEF_CMD(CMD_GET_HOSTNAME, CRT_OPC_CTL_GET_HOSTNAME),
	DEF_CMD(CMD_GET_PID, CRT_OPC_CTL_GET_PID),
	DEF_CMD(CMD_ENABLE_FI, CRT_OPC_CTL_FI_TOGGLE),
	DEF_CMD(CMD_DISABLE_FI, CRT_OPC_CTL_FI_TOGGLE),
	DEF_CMD(CMD_SET_FI_ATTR, CRT_OPC_CTL_FI_SET_ATTR),
	DEF_CMD(CMD_LOG_SET, CRT_OPC_CTL_LOG_SET),
	DEF_CMD(CMD_LOG_ADD_MSG, CRT_OPC_CTL_LOG_ADD_MSG),
};

static char *cmd2str(enum cmd_t cmd)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cmds); i++) {
		if (cmd == cmds[i].cmd)
			return cmds[i].cmd_str;
	}

	return "Unknown cmd";
}

static int cmd2opcode(enum cmd_t cmd)
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
	if (strnlen(arg_str, CRT_CTL_MAX_ARG_STR_LEN) >=
		    CRT_CTL_MAX_ARG_STR_LEN) {
		D_ERROR("arg string too long.\n");
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
			if (num_ranks_l > CRT_CTL_MAX) {
				D_ERROR("Too many target ranks.\n");
				return;
			}
			ranks[index] = atoi(token);
			index++;
			token = strtok_r(NULL, ",", &saveptr);
			continue;
		}
		if (ptr == token || ptr == token + strlen(token)) {
			D_ERROR("Invalid rank range.\n");
			return;
		}
		rstart = atoi(token);
		rend = atoi(ptr + 1);
		num_ranks_l += (rend - rstart + 1);
		if (num_ranks_l > CRT_CTL_MAX) {
			D_ERROR("Too many target ranks.\n");
			return;
		}
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
	if (strnlen(arg_str, CRT_CTL_MAX_ARG_STR_LEN) >=
		    CRT_CTL_MAX_ARG_STR_LEN) {
		D_ERROR("arg string too long.\n");
		return;
	}

	D_DEBUG(DB_TRACE, "arg_str %s\n", arg_str);
	fprintf(stderr, "arg_str %s\n", arg_str);
	token = strtok_r(arg_str, ",", &saveptr);
	if (token == NULL)
		D_GOTO(error_out, 0);
	fi_attr_in->fa_fault_id = strtoull(token, &endptr, 10);
	fprintf(stderr, "fault_id %d\n", fi_attr_in->fa_fault_id);

	/* get max_faults */
	token = strtok_r(NULL, ",", &saveptr);
	if (token == NULL)
		D_GOTO(error_out, 0);
	fi_attr_in->fa_max_faults = strtoull(token, &endptr, 10);
	fprintf(stderr, "max_faults %lu\n", fi_attr_in->fa_max_faults);

	token = strtok_r(NULL, ",", &saveptr);
	if (token == NULL)
		D_GOTO(error_out, 0);
	fi_attr_in->fa_probability_x = strtoull(token, &endptr, 10);

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
	fprintf(stderr, "Error: --attr has wrong number of arguments, should "
		"be \t--attr fault_id,max_faults,probability,err_code\n");
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
	printf("--cfg_path\n");
	printf("\tPath to group config file\n");
	printf("--rank start-end,start-end,rank,rank\n");
	printf("\tspecify target ranks; 'all' specifies every known rank\n");
	printf("-l log_mask\n");
	printf("\tSpecify log_mask to be set remotely\n");
	printf("-n\n");
	printf("\tdon't perform 'wait for ranks' sync\n");
	printf("-m 'log_message'\n");
	printf("\tSpecify log message to be sent to remote server\n");
}

static int
parse_args(int argc, char **argv)
{
	int		option_index = 0;
	int		opt;
	int		rc = 0;

	if (argc <= 2) {
		print_usage_msg("Wrong number of args\n");
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
		{0, 0, 0, 0},
	};

	while (1) {
		opt = getopt_long(argc, argv, "g:r:a:p:l:m:n", long_options,
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
		default:
			break;
		}
	}

	if (ctl_gdata.cg_cmd_code == CMD_LOG_ADD_MSG &&
	    ctl_gdata.cg_log_msg_set == false) {
		D_ERROR("log msg (-m 'message') missing for add_log_msg\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (ctl_gdata.cg_cmd_code == CMD_LOG_SET &&
	    ctl_gdata.cg_log_mask_set == 0) {
		D_ERROR("log mask (-l mask) missing for set_log\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (ctl_gdata.cg_cmd_code == CMD_SET_FI_ATTR &&
	    ctl_gdata.cg_fi_attr_inited == 0) {
		D_ERROR("fault attributes missing for set_fi_attr.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

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
		fprintf(stdout, "  rank = %d, ", grp_cache[i].gc_rank);
		fprintf(stdout, "tag = %d, ", grp_cache[i].gc_tag);
		fprintf(stdout, "uri = %s\n", grp_cache[i].gc_uri);
	}
}

static void
ctl_cli_cb(const struct crt_cb_info *cb_info)
{
	struct crt_ctl_ep_ls_in			*in_args;
	struct crt_ctl_get_uri_cache_out	*out_uri_cache_args;
	struct crt_ctl_ep_ls_out		*out_ls_args;
	struct crt_ctl_get_host_out		*out_get_host_args;
	struct crt_ctl_get_pid_out		*out_get_pid_args;
	struct crt_ctl_fi_attr_set_out		*out_set_fi_attr_args;
	struct crt_ctl_fi_toggle_out		*out_fi_toggle_args;
	struct crt_ctl_log_set_out		*out_log_set_args;
	char					*addr_str;
	struct cb_info				*info;
	int					 i;

	info = cb_info->cci_arg;

	in_args = crt_req_get(cb_info->cci_rpc);

	fprintf(stdout, "COMMAND: %s\n", cmd2str(info->cmd));

	if (info->cmd == CMD_ENABLE_FI) {
		out_fi_toggle_args = crt_reply_get(cb_info->cci_rpc);
		fprintf(stdout, "CMD_ENABLE_FI finished. rc %d\n",
			out_fi_toggle_args->rc);
	} else if (info->cmd == CMD_DISABLE_FI) {
		out_fi_toggle_args = crt_reply_get(cb_info->cci_rpc);
		fprintf(stdout, "CMD_DISABLE_FI finished. rc %d\n",
			out_fi_toggle_args->rc);
	} else if (info->cmd == CMD_SET_FI_ATTR) {
		out_set_fi_attr_args = crt_reply_get(cb_info->cci_rpc);
		fprintf(stdout, "rc: %d (%s)\n", out_set_fi_attr_args->fa_ret,
			d_errstr(out_set_fi_attr_args->fa_ret));
	} else if (info->cmd == CMD_LOG_SET) {
		out_log_set_args = crt_reply_get(cb_info->cci_rpc);
		fprintf(stdout, "rc: %d (%s)\n", out_log_set_args->rc,
				d_errstr(out_log_set_args->rc));
	} else if (info->cmd == CMD_LOG_ADD_MSG) {
	} else if (cb_info->cci_rc == 0) {
		fprintf(stdout, "group: %s, rank: %d\n",
			in_args->cel_grp_id, in_args->cel_rank);

		if (info->cmd == CMD_GET_URI_CACHE) {
			out_uri_cache_args = crt_reply_get(cb_info->cci_rpc);
			if (out_uri_cache_args->cguc_rc != 0)
				fprintf(stdout, "CMD_GET_URI_CACHE "
					"returned error, rc = %d\n",
					out_uri_cache_args->cguc_rc);
			else
				print_uri_cache(out_uri_cache_args);
		} else if (info->cmd == CMD_LIST_CTX) {
			out_ls_args = crt_reply_get(cb_info->cci_rpc);
			fprintf(stdout, "ctx_num: %d\n",
				out_ls_args->cel_ctx_num);
			addr_str = out_ls_args->cel_addr_str.iov_buf;
			for (i = 0; i < out_ls_args->cel_ctx_num; i++) {
				fprintf(stdout, "    %s\n", addr_str);
				addr_str += (strlen(addr_str) + 1);
			}
		} else if (info->cmd == CMD_GET_HOSTNAME) {
			out_get_host_args = crt_reply_get(cb_info->cci_rpc);

			fprintf(stdout, "hostname: %s\n",
			    (char *)out_get_host_args->cgh_hostname.iov_buf);
		} else if (info->cmd == CMD_GET_PID) {

			out_get_pid_args = crt_reply_get(cb_info->cci_rpc);

			fprintf(stdout, "pid: %d\n",
				out_get_pid_args->cgp_pid);
		}

	} else {
		fprintf(stdout, "ERROR: group: %s, rank %d, rc %d\n",
			in_args->cel_grp_id, in_args->cel_rank,
			cb_info->cci_rc);
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
	struct crt_ctl_fi_attr_set_in	*in_args_fi_attr;

	in_args_fi_attr = crt_req_get(rpc_req);

	in_args_fi_attr->fa_fault_id = ctl_gdata.cg_fi_attr.fa_fault_id;
	in_args_fi_attr->fa_max_faults = ctl_gdata.cg_fi_attr.fa_max_faults;
	in_args_fi_attr->fa_probability_x =
		ctl_gdata.cg_fi_attr.fa_probability_x;
	in_args_fi_attr->fa_probability_y =
		ctl_gdata.cg_fi_attr.fa_probability_y;
	in_args_fi_attr->fa_err_code = ctl_gdata.cg_fi_attr.fa_err_code;
	in_args_fi_attr->fa_interval = ctl_gdata.cg_fi_attr.fa_interval;
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
	int			 rc = 0;

	if (ctl_gdata.cg_save_cfg) {
		rc = crt_group_config_path_set(ctl_gdata.cg_cfg_path);
		D_ASSERTF(rc == 0, "crt_group_config_path_set failed %d\n", rc);
	}

	tc_cli_start_basic("crt_ctl", ctl_gdata.cg_group_name, &grp,
			    &rank_list, &ctl_gdata.cg_crt_ctx,
			    &ctl_gdata.cg_tid, 1, true, NULL);

	rc = sem_init(&ctl_gdata.cg_num_reply, 0, 0);
	D_ASSERTF(rc == 0, "Could not initialize semaphore. rc %d\n", rc);

	/* waiting to sync with the following parameters
	 * 0 - tag 0
	 * 1 - total ctx
	 * 5 - ping timeout
	 * 150 - total timeout
	 */
	if (ctl_gdata.cg_no_wait_for_ranks == false) {
		rc = tc_wait_for_ranks(ctl_gdata.cg_crt_ctx, grp, rank_list,
				       0, 1, 5, 150);
		if (rc != 0) {
			D_ERROR("wait_for_ranks() failed; rc=%d\n", rc);
			D_GOTO(out, rc);
		}
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

	for (i = 0; i < num_ranks; i++) {
		ep.ep_grp = grp;
		ep.ep_rank = ranks_to_send[i];
		ep.ep_tag = 0;
		rc = crt_req_create(ctl_gdata.cg_crt_ctx, &ep,
				    cmd2opcode(info.cmd), &rpc_req);
		if (rc != 0) {
			D_ERROR("crt_req_create() failed. rc %d.\n", rc);
			D_GOTO(out, rc);
		}

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
		if (rc != 0) {
			D_ERROR("crt_req_send() failed. rpc_req %p rank %d "
				"tag %d rc %d.\n", rpc_req, ep.ep_rank,
				 ep.ep_tag, rc);
			D_GOTO(out, rc);
		}

		rc = tc_sem_timedwait(&ctl_gdata.cg_num_reply, 61, __LINE__);
		if (rc != 0) {
			D_ERROR("tc_sem_timedwait failed, rc = %d\n", rc);
			D_GOTO(out, rc);
		}
	}

	d_rank_list_free(rank_list);

	if (ctl_gdata.cg_save_cfg) {
		rc = crt_group_detach(grp);
		D_ASSERTF(rc == 0, "crt_group_detach failed, rc: %d\n", rc);
	} else {
		rc = crt_group_view_destroy(grp);
		D_ASSERTF(rc == 0,
			  "crt_group_view_destroy() failed; rc=%d\n", rc);
	}

	g_shutdown = 1;

	rc = pthread_join(ctl_gdata.cg_tid, NULL);
	D_ASSERTF(rc == 0, "pthread_join failed. rc: %d\n", rc);

	rc = sem_destroy(&ctl_gdata.cg_num_reply);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);

	d_log_fini();

out:
	return rc;
}

int
main(int argc, char **argv)
{
	int		rc = 0;

	rc = parse_args(argc, argv);
	D_ASSERTF(rc == 0, "parse_args() failed. rc %d\n", rc);

	/* rank, num_attach_retries, is_server, assert_on_error */
	tc_test_init(0, 40, false, false);

	rc = ctl_init();
	D_ASSERTF(rc == 0, "ctl_init() failed, rc %d\n", rc);

	return rc;
}
