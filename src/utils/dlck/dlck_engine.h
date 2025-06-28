/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_ENGINE__
#define __DLCK_ENGINE__

#include <abt.h>

#include "dlck_args.h"
struct dlck_ult {
	ABT_thread thread;
};

struct dlck_xstream {
	ABT_xstream     xstream;
	ABT_pool        pool;

	int             tgt_id;
	struct dlck_ult nvme_poll;
	ABT_eventual    nvme_poll_done;
};

struct dlck_engine {
	unsigned             targets;
	struct dlck_xstream *xss;
};

typedef void (*dlck_ult_func)(void *arg);

/**
 * XXX doc missing
 */
int
dlck_engine_start(struct dlck_args *args, struct dlck_engine **engine_ptr);
int
dlck_engine_stop(struct dlck_engine *engine);
int
dlck_engine_xstream_init(struct dlck_xstream *xs);
int
dlck_engine_xstream_fini(struct dlck_xstream *xs);

int
dlck_abt_attr_default_create(ABT_thread_attr *attr);
int
dlck_abt_init(struct dlck_engine *engine);

int
dlck_ult_create(ABT_pool pool, dlck_ult_func func, void *arg, struct dlck_ult *ult);
int
dlck_ult_create_on_xstream(struct dlck_xstream *xs, dlck_ult_func func, void *arg,
			   struct dlck_ult *ult);

int
dlck_xstream_create(struct dlck_xstream *xs);

#endif /** __DLCK_ENGINE__ */
