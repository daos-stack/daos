/* Copyright (C) 2017-2019 Intel Corporation
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
 *
 * This file is a simple test of the crt_barrier API
 */
#include <pthread.h>
#include <cart/api.h>

#include "tests_common.h"

#define NUM_BARRIERS 20

static int g_barrier_count;

struct proc_info {
	d_rank_t	rank;
	d_rank_t	grp_rank;
	int		barrier_num;
	int		complete;
};

static void
barrier_complete_cb(struct crt_barrier_cb_info *cb_info)
{
	struct proc_info	*info;

	info = (struct proc_info *)cb_info->bci_arg;

	D_ASSERTF(cb_info->bci_rc == 0, "Barrier failed %d\n", cb_info->bci_rc);

	D_ASSERTF(info->barrier_num == g_barrier_count,
		  "Out of order barrier completion, %d != %d\n",
		  info->barrier_num, g_barrier_count);
	g_barrier_count++;
	info->complete = 1;
	printf("Hello from rank %d (%d), num %d\n", info->rank,
	       info->grp_rank, info->barrier_num);
	fflush(stdout);
}

int main(int argc, char **argv)
{
	struct proc_info	*info;
	void			*check_ret;
	crt_context_t		 crt_ctx;
	d_rank_t		 my_rank;
	char			*env_self_rank;
	int			 i;
	pthread_t		 tid;
	crt_group_t		*grp = NULL;
	uint32_t		 grp_size;
	int			 rc = 0;

	env_self_rank = getenv("CRT_L_RANK");
	my_rank = atoi(env_self_rank);

	/* rank, num_attach_retries, is_server, assert_on_error */
	tc_test_init(my_rank, 20, true, true);

	tc_srv_start_basic("server_grp", &crt_ctx, &tid, grp, &grp_size);

	info = (struct proc_info *)malloc(sizeof(struct proc_info) *
					  NUM_BARRIERS);
	D_ASSERTF(info != NULL,
		  "Could not allocate space for test");

	crt_group_rank(NULL, &my_rank);

	for (i = 0; i < NUM_BARRIERS; i++) {
		info[i].rank = my_rank;
		info[i].grp_rank = my_rank;
		info[i].barrier_num = i;
		info[i].complete = 0;
		for (;;) {
			rc = crt_barrier(NULL, barrier_complete_cb, &info[i]);
			if (rc != -DER_BUSY)
				break;
			sched_yield();
		}
		D_ASSERTF(rc == 0, "crt_barrier_create rank=%d, barrier = %d,"
			  " rc = %d\n", my_rank, i, rc);
	}
	for (i = 0; i < NUM_BARRIERS; i++)
		while (info[i].complete == 0)
			sched_yield();

	D_ASSERTF(g_barrier_count == NUM_BARRIERS,
		  "Not all barriers completed\n");

	g_barrier_count = 0;

	g_shutdown = 1;

	pthread_join(tid, &check_ret);
	D_ASSERTF(check_ret == NULL, "Progress thread failed\n");

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "Failed in crt_finalize, rc = %d\n", rc);

	d_log_fini();

	free(info);

	return rc;
}
