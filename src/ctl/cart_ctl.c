/* Copyright (C) 2018 Intel Corporation
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
	CMD_LIST_CTX,
	CMD_GET_HOSTNAME,
	CMD_GET_PID,
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
	DEF_CMD(CMD_GET_HOSTNAME, CRT_OPC_CTL_GET_HOSTNAME),
	DEF_CMD(CMD_GET_PID, CRT_OPC_CTL_GET_PID),
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
	enum cmd_t	 cg_cmd_code;
	char		*cg_group_name;
	crt_group_t	*cg_target_group;
	int		 cg_num_ranks;
	d_rank_t	 cg_ranks[CRT_CTL_MAX];
	crt_context_t	 cg_crt_ctx;
	pthread_t	 cg_tid;
	int		 cg_complete;
	sem_t		 cg_num_reply;
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
	token = strtok(arg_str, ",");
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
			token = strtok(NULL, ",");
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
		token = strtok(NULL, ",");
	}
	*num_ranks = num_ranks_l;
}

static void
print_usage_msg(const char *msg)
{
	if (msg)
		printf("\nERROR: %s\n", msg);
	printf("Usage: cart_ctl <cmd> --group-name name --rank "
	       "start-end,start-end,rank,rank\n");
	printf("cmds: list_ctx, get_hostname, get_pid\n");
	printf("\nlist_ctx:\n");
	printf("\tPrint # of contexts on each rank and uri for each context\n");
	printf("\nget_hostname:\n");
	printf("\tPrint hostnames of specified ranks\n");
	printf("\nget_pid:\n");
	printf("\tReturn pids of the specified ranks\n");
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

	if (strcmp(argv[1], "list_ctx") == 0)
		ctl_gdata.cg_cmd_code = CMD_LIST_CTX;
	else if (strcmp(argv[1], "get_hostname") == 0)
		ctl_gdata.cg_cmd_code = CMD_GET_HOSTNAME;
	else if (strcmp(argv[1], "get_pid") == 0)
		ctl_gdata.cg_cmd_code = CMD_GET_PID;
	else {
		print_usage_msg("Invalid command\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	optind = 2;
	while (1) {
		static struct option long_options[] = {
			{"group-name", required_argument, 0, 'g'},
			{"rank", required_argument, 0, 'r'},
			{0, 0, 0, 0},
		};

		opt = getopt_long(argc, argv, "g:r:", long_options, NULL);
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
		}
	}

out:
	return rc;
}

static void
ctl_client_cb(const struct crt_cb_info *cb_info)
{
	struct crt_ctl_ep_ls_in		*in_args;
	struct crt_ctl_ep_ls_out	*out_ls_args;
	struct crt_ctl_get_host_out	*out_get_host_args;
	struct crt_ctl_get_pid_out	*out_get_pid_args;
	char				*addr_str;
	int				 i;
	struct cb_info			*info;

	info = cb_info->cci_arg;

	in_args = crt_req_get(cb_info->cci_rpc);

	fprintf(stdout, "COMMAND: %s\n", cmd2str(info->cmd));

	if (cb_info->cci_rc == 0) {
		fprintf(stdout, "group: %s, rank: %d\n",
			in_args->cel_grp_id, in_args->cel_rank);

		if (info->cmd == CMD_LIST_CTX) {
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

static int
ctl_issue_cmd(void)
{
	int				 i;
	crt_rpc_t			*rpc_req;
	struct crt_ctl_ep_ls_in		*in_args;
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

		in_args = crt_req_get(rpc_req);
		in_args->cel_grp_id = ctl_gdata.cg_target_group->cg_grpid;
		in_args->cel_rank = ctl_gdata.cg_ranks[i];

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


#define NUM_ATTACH_RETRIES 10

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
