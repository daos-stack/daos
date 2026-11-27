/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_XSTREAM__
#define __DLCK_XSTREAM__

#include <abt.h>

#include "dlck_abt.h"
#include "dlck_args.h"

struct dlck_engine;

struct dlck_xstream {
	ABT_xstream  xstream;
	ABT_pool     pool;
	int          tgt_id;
	ABT_thread   nvme_poll;
	ABT_eventual nvme_poll_done;
	int          ult_rc;
};

int
dlck_xstream_create(struct dlck_xstream *xs);

int
dlck_xstream_init(struct dlck_xstream *xs);

int
dlck_xstream_stop(struct dlck_xstream *xs);

void
dlck_xstream_stop_all_no_error(struct dlck_xstream *xss, unsigned targets);

int
dlck_xstream_free(struct dlck_xstream *xs);

struct xstream_arg {
	struct dlck_control  *ctrl;
	struct dlck_engine   *engine;
	struct dlck_xstream  *xs;
	struct checker_status status;
};

int
dlck_xstream_arg_alloc(struct dlck_engine *engine, int idx, void *ctrl_ptr, void **output_arg);

void
dlck_xstream_arg_free(void *ctrl_ptr, void **arg);

#endif /* __DLCK_XSTREAM__ */
