/*
 * Copyright (c) 2017-2019 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <stdlib.h>
#include <stdbool.h>

#include <rdma/fabric.h>
#include <rdma/fi_trigger.h>
#include <sys/uio.h>
#include <sys/socket.h>

#include "pattern.h"

#define PM_DEFAULT_OOB_PORT (8228)

enum multi_xfer{
	multi_msg,
	multi_rma,
};

enum multi_pattern {
	PATTERN_MESH,
	PATTERN_RING,
	PATTERN_GATHER,
	PATTERN_BROADCAST,
};

struct multi_xfer_method {
	char* name;
	int (*send)(void);
	int (*recv)(void);
	int (*wait)(void);
};

struct pm_job_info {
	size_t		my_rank;
	size_t		num_ranks;
	int		sock;
	int		*clients; //only valid for server
	struct fi_rma_iov 	*multi_iovs;

	struct sockaddr_storage oob_server_addr;
	size_t 		server_addr_len;
	void		*names;
	size_t		name_len;
	fi_addr_t	*fi_addrs;
	enum multi_xfer transfer_method;
	enum multi_pattern pattern;
};

struct multinode_xfer_state {
	int 			iter;
	size_t			recvs_posted;
	size_t			sends_posted;

	size_t			tx_window;
	size_t			rx_window;

	/* pattern iterator state */
	int			cur_source;
	int			cur_target;

	bool			all_recvs_posted;
	bool			all_sends_posted;
	bool			all_completions_done;

	uint64_t		tx_flags;
	uint64_t		rx_flags;
};

extern struct pm_job_info pm_job;

static inline int timer_index(int iter, int dest_rank)
{
    return iter * pm_job.num_ranks + dest_rank;
}

int multinode_run_tests(int argc, char **argv);
int pm_allgather(void *my_item, void *items, int item_size);
ssize_t socket_send(int sock, void *buf, size_t len, int flags);
int socket_recv(int sock, void *buf, size_t len, int flags);
void pm_barrier(void);

int multi_msg_send(void);
int multi_msg_recv(void);
int multi_msg_wait(void);
int multi_rma_write(void);
int multi_rma_recv(void);
int multi_rma_wait(void);
