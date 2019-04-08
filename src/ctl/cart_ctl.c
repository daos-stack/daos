/* Copyright (C) 2018-2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * This file is part of CaRT. It implements client side of the cart_ctl command
 * line utility.
 */
#define D_LOGFAC	DD_FAC(ctl)

#include "crt_internal.h"
#include <gurt/common.h>
#include <cart/api.h>

#include <stdio.h>
#include <pthread.h>
#include <getopt.h>
#include <semaphore.h>

/* max number of ranks that can be queried at once */
#define CRT_CTL_MAX 1024
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
	sem_t				 cg_num_reply;
	struct crt_ctl_fi_attr_set_in	 cg_fi_attr;
	int				 cg_fi_attr_inited;
};

static struct ctl_g ctl_gdata;

static void *
progress_thread(void *arg)
{
	int			rc;
	crt_context_t		crt_ctx;

	crt_ctx = (crt_context_t) arg;
	/* progress loop */
	do {
		rc = crt_progress(crt_ctx, 1, NULL, NULL);
		if (rc != 0 && rc != -DER_TIMEDOUT) {
			D_ERROR("crt_progress failed rc: %d.\n", rc);
			break;
		}


		if (ctl_gdata.cg_complete == 1) {
			D_DEBUG(DB_TRACE, "ctl_gdata.cg_complete %d.\n",
				ctl_gdata.cg_complete);
			break;
		}
	} while (1);

	D_DEBUG(DB_TRACE, "progress_thread: progress thread exit ...\n");

	pthread_exit(NULL);
}

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
	       "start-end,start-end,rank,rank\n"
	       "--path path-to-attach-info\n");
	printf("\ncmds: get_uri_cache, list_ctx, get_hostname, get_pid\n");
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
	printf("--rank start-end,start-end,rank,rank\n");
	printf("\tspecify target ranks\n");
	printf("--path path-to-attach-info\n");
	printf("\tspecify the location of the attach info file\n");
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
	else {
		print_usage_msg("Invalid command\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	optind = 2;
	while (1) {
		static struct option long_options[] = {
			{"group-name", required_argument, 0, 'g'},
			{"rank", required_argument, 0, 'r'},
			{"attr", required_argument, 0, 'a'},
			{"path", required_argument, 0, 'p'},
			{0, 0, 0, 0},
		};

		opt = getopt_long(argc, argv, "g:r:a:p:", long_options, NULL);
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
		case 'p':
			rc = crt_group_config_path_set(optarg);
			if (rc != 0) {
				printf("Bad attach prefix: %s\n", optarg);
				exit(-1);
			}
			break;
		default:
			break;
		}
	}

	if (ctl_gdata.cg_cmd_code == CMD_SET_FI_ATTR &&
	    ctl_gdata.cg_fi_attr_inited == 0) {
		D_ERROR("fault attributes missing for set_fi_attr.\n");
		rc = -DER_INVAL;
	}

out:
	return rc;
}

static void
print_uri_cache(struct crt_ctl_get_uri_cache_out *out_uri_cache_args)
{
	struct crt_grp_cache    *grp_cache;
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
ctl_client_cb(const struct crt_cb_info *cb_info)
{
	struct crt_ctl_ep_ls_in			*in_args;
	struct crt_ctl_get_uri_cache_out	*out_uri_cache_args;
	struct crt_ctl_ep_ls_out		*out_ls_args;
	struct crt_ctl_get_host_out		*out_get_host_args;
	struct crt_ctl_get_pid_out		*out_get_pid_args;
	struct crt_ctl_fi_attr_set_out		*out_set_fi_attr_args;
	struct crt_ctl_fi_toggle_out		*out_fi_toggle_args;
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
ctl_issue_cmd(void)
{
	int				 i;
	crt_rpc_t			*rpc_req;
	crt_endpoint_t			 ep;
	struct cb_info			 info;
	int				 rc = 0;

	D_DEBUG(DB_TRACE, "num requested ranks %d\n", ctl_gdata.cg_num_ranks);

	info.cmd = ctl_gdata.cg_cmd_code;

	for (i = 0; i < ctl_gdata.cg_num_ranks; i++) {
		ep.ep_grp = ctl_gdata.cg_target_group;
		ep.ep_rank = ctl_gdata.cg_ranks[i];
		ep.ep_tag = 0;
		rc = crt_req_create(ctl_gdata.cg_crt_ctx, &ep,
				    cmd2opcode(info.cmd), &rpc_req);
		if (rc != 0) {
			D_ERROR("crt_req_create() failed. rc %d.\n", rc);
			D_GOTO(out, rc);
		}

		/* fill RPC arguments depending on the opcode */
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
		default:
			ctl_fill_rpc_args(rpc_req, i);
		}

		D_DEBUG(DB_NET, "rpc_req %p rank %d tag %d seq %d\n",
			rpc_req, ep.ep_rank, ep.ep_tag, i);

		rc = crt_req_send(rpc_req, ctl_client_cb, &info);
		if (rc != 0) {
			D_ERROR("crt_req_send() failed. rpc_req %p rank %d tag "
				"%d rc %d.\n",
				rpc_req, ep.ep_rank, ep.ep_tag, rc);
			D_GOTO(out, rc);
		}
	}
	for (i = 0; i < ctl_gdata.cg_num_ranks; i++)
		sem_wait(&ctl_gdata.cg_num_reply);

out:
	return rc;
}


#define NUM_ATTACH_RETRIES 20

static int
ctl_init()
{
	int rc;
	int attach_retries = NUM_ATTACH_RETRIES;

	rc = crt_init("crt_ctl", CRT_FLAG_BIT_SINGLETON);
	D_ASSERTF(rc == 0, "crt_init() failed, rc: %d\n", rc);

	rc = d_log_init();
	D_ASSERTF(rc == 0, "d_log_init() failed. rc: %d\n", rc);

	rc = crt_context_create(&ctl_gdata.cg_crt_ctx);
	D_ASSERTF(rc == 0, "crt_context_create() failed. rc: %d\n", rc);

	ctl_gdata.cg_complete = 0;
	rc = sem_init(&ctl_gdata.cg_num_reply, 0, 0);
	D_ASSERTF(rc == 0, "Could not initialize semaphore. rc %d\n", rc);
	rc = pthread_create(&ctl_gdata.cg_tid, NULL, progress_thread,
			    ctl_gdata.cg_crt_ctx);
	D_ASSERTF(rc == 0, "pthread_create() failed. rc: %d\n", rc);

	/* Attempt to attach up to NUM_ATTACH_RETRIES in case servers
	 * have not started up yet
	 */
	while (attach_retries-- > 0) {
		rc = crt_group_attach(ctl_gdata.cg_group_name,
			      &ctl_gdata.cg_target_group);
		if (rc == 0)
			break;

		D_DEBUG(DB_TEST, "Attach failed, retries left=%d\n",
			attach_retries);
		sleep(1);
	}

	D_ASSERTF(rc == 0, "crt_group_attach failed, tgt_group: %s rc: %d\n",
		  ctl_gdata.cg_group_name, rc);
	D_ASSERTF(ctl_gdata.cg_target_group != NULL,
		  "NULL attached target_group\n");

	return rc;
}

static int
ctl_finalize()
{
	int		rc;

	rc = crt_group_detach(ctl_gdata.cg_target_group);
	D_ASSERTF(rc == 0, "crt_group_detach failed, rc: %d\n", rc);
	ctl_gdata.cg_complete = 1;
	rc = pthread_join(ctl_gdata.cg_tid, NULL);
	D_ASSERTF(rc == 0, "pthread_join failed. rc: %d\n", rc);
	rc = crt_context_destroy(ctl_gdata.cg_crt_ctx, 0);
	D_ASSERTF(rc == 0, "crt_context_destroy() failed. rc: %d\n", rc);
	d_log_fini();
	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);

	return rc;
}

int
main(int argc, char **argv)
{
	int		rc = 0;

	rc = parse_args(argc, argv);
	if (rc != 0) {
		D_ERROR("parse_args() failed. rc %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = ctl_init();
	if (rc != 0) {
		D_ERROR("ctl_init() failed, rc %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = ctl_issue_cmd();

	if (rc != 0) {
		D_ERROR("Command '%s' failed with rc=%d\n",
			cmd2str(ctl_gdata.cg_cmd_code), rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_TRACE, "cart_ctl exiting\n");
	rc = ctl_finalize();
	if (rc != 0) {
		D_ERROR("ctl_finalize() failed, rc %d\n", rc);
		D_GOTO(out, rc);
	}

out:
	return rc;
}
