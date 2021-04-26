/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Common functions to be shared among tests
 */
#ifndef __CART_UTILS_H__
#define __CART_UTILS_H__
#include <semaphore.h>
#include <cart/api.h>

#include "crt_internal.h"

#define DBG_PRINT(x...)							\
	do {								\
		struct test_options *opts;				\
		opts = crtu_get_opts();					\
		D_INFO(x);						\
		if (opts->is_server)					\
			fprintf(stderr, "SRV [rank=%d pid=%d]\t",       \
			opts->self_rank,				\
			opts->mypid);					\
		else							\
			fprintf(stderr, "CLI [rank=%d pid=%d]\t",       \
			opts->self_rank,				\
			opts->mypid);					\
		fprintf(stderr, x);					\
	} while (0)

struct test_options {
	bool		is_initialized;
	d_rank_t	self_rank;
	int		mypid;
	int		num_attach_retries;
	bool		is_server;
	bool		assert_on_error;
	volatile int	shutdown;
	int		delay_shutdown_sec;
	bool		is_swim_enabled;
};

struct test_options *crtu_get_opts();

void
crtu_test_init(d_rank_t rank, int num_attach_retries, bool is_server,
	       bool assert_on_error);

void
crtu_set_shutdown_delay(int delay_sec);

void
crtu_progress_stop(void);

void *
crtu_progress_fn(void *data);

int
crtu_wait_for_ranks(crt_context_t ctx, crt_group_t *grp,
		    d_rank_list_t *rank_list,
		    int tag, int total_ctx, double ping_timeout,
		    double total_timeout);
int
crtu_load_group_from_file(const char *grp_cfg_file, crt_context_t ctx,
			  crt_group_t *grp, d_rank_t my_rank,
			  bool delete_file);

void
crtu_cli_start_basic(char *local_group_name, char *srv_group_name,
		     crt_group_t **grp, d_rank_list_t **rank_list,
		     crt_context_t *crt_ctx, pthread_t *progress_thread,
		     unsigned int total_srv_ctx, bool use_cfg,
		     crt_init_options_t *init_opt);
void
crtu_srv_start_basic(char *srv_group_name, crt_context_t *crt_ctx,
		     pthread_t *progress_thread, crt_group_t **grp,
		     uint32_t *grp_size, crt_init_options_t *init_opt);
int
crtu_log_msg(crt_context_t ctx, crt_group_t *grp, d_rank_t rank, char *msg);

static inline void
crtu_test_swim_enable(bool is_swim_enabled)
{
	struct test_options	*opts;

	opts = crtu_get_opts();
	opts->is_swim_enabled = is_swim_enabled;
}

static inline int
crtu_sem_timedwait(sem_t *sem, int sec, int line_number)
{
	struct timespec		deadline;
	int			rc;
	struct test_options	*opts;

	opts = crtu_get_opts();
	rc = clock_gettime(CLOCK_REALTIME, &deadline);
	if (rc != 0) {
		if (opts->assert_on_error)
			D_ASSERTF(rc == 0, "clock_gettime() failed at "
				  "line %d rc: %d\n", line_number, rc);
		D_ERROR("clock_gettime() failed, rc = %d\n", rc);
		D_GOTO(out, rc);
	}

	deadline.tv_sec += sec;
	rc = sem_timedwait(sem, &deadline);
	if (rc != 0) {
		if (opts->assert_on_error)
			D_ASSERTF(rc == 0, "sem_timedwait() failed at "
				  "line %d rc: %d\n", line_number, rc);
		D_ERROR("sem_timedwait() failed, rc = %d\n", rc);
		D_GOTO(out, rc);
	}
out:
	return rc;
}

#endif /* __CART_UTILS_H__ */
