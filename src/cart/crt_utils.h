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
		struct test_options *__opts;				\
		__opts = crtu_get_opts();					\
		D_INFO(x);						\
		if (__opts->is_server)					\
			fprintf(stderr, "SRV [rank=%d pid=%d]\t",       \
			__opts->self_rank,				\
			__opts->mypid);					\
		else							\
			fprintf(stderr, "CLI [rank=%d pid=%d]\t",       \
			__opts->self_rank,				\
			__opts->mypid);					\
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

void
write_completion_file(void);

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

void
crtu_test_swim_enable(bool is_swim_enabled);

int
crtu_sem_timedwait(sem_t *sem, int sec, int line_number);

#endif /* __CART_UTILS_H__ */
