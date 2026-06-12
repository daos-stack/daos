/*
 * (C) Copyright 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Server that accepts ping RPCs with an option of specifying response delay.
 *
 * Delayed RPCs are processed by context[0] today as simple_client only
 * sends to tag=0.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <time.h>
#include <cart/api.h>
#include <gurt/common.h>
#include <gurt/list.h>

#include "simple_serv_cli_common.h"

#define MY_RANK 0
#define GRP_VER 1

static int do_shutdown;

/* TODO: needs to be per-context. For assumes this is context0 list */
static D_LIST_HEAD(delayed_rpcs_list);
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;

struct list_entry {
	crt_rpc_t      *rpc;
	d_list_t        link;
	struct timespec when;
};

void
process_delayed_rpcs(void)
{
	struct list_entry *entry, *tmp;
	struct timespec    now;
	int                rc;
	int                num_replied = 0;

	rc = clock_gettime(CLOCK_REALTIME, &now);
	D_ASSERTF(rc == 0, "clock_gettime() failed; rc=%d\n", rc);

	D_MUTEX_LOCK(&list_lock);
	d_list_for_each_entry_safe(entry, tmp, &delayed_rpcs_list, link) {
		if (now.tv_sec > entry->when.tv_sec) {
			crt_reply_send(entry->rpc);

			/* addref was done in handler_pin */
			crt_req_decref(entry->rpc);
			d_list_del(&entry->link);
			num_replied++;
		}
	}
	D_MUTEX_UNLOCK(&list_lock);

	if (num_replied > 0)
		DBG_PRINT("Replied to %d delayed rpc%s\n", num_replied,
			  num_replied == 1 ? "" : "s");
}

int
handler_shutdown(crt_rpc_t *rpc)
{
	DBG_PRINT("Shutdown handler called!\n");
	crt_reply_send(rpc);
	do_shutdown = 1;
	return 0;
}

/*
 * Ping handler.
 * Replies right away if input->delay_sec == 0, else queues it for
 * context0 to process at a later time.
 * Assumes today that delayed rpcs only arrive to context0
 **/
int
handler_ping(crt_rpc_t *rpc)
{
	struct RPC_PING_in  *input;
	struct RPC_PING_out *output;
	int                  rc;

	input  = crt_req_get(rpc);
	output = crt_reply_get(rpc);
	output->seq = input->seq;

	if (input->delay_sec == 0) {
		crt_reply_send(rpc);
	} else {
		struct list_entry *entry;

		D_ALLOC_PTR(entry);
		D_ASSERTF(entry != 0, "failed to allocate entry\n");

		rc = clock_gettime(CLOCK_REALTIME, &entry->when);
		D_ASSERTF(rc == 0, "clock_gettime() failed; rc=%d\n", rc);

		entry->when.tv_sec += input->delay_sec;
		entry->rpc = rpc;

		/* decref in process_delayed_rpcs() */
		crt_req_addref(rpc);

		D_MUTEX_LOCK(&list_lock);
		d_list_add_tail(&entry->link, &delayed_rpcs_list);
		D_MUTEX_UNLOCK(&list_lock);
	}

	return 0;
}

static void *
progress_fn(void *data)
{
	int            rc;
	int            idx   = -1;
	char          *uri   = NULL;
	crt_context_t *p_ctx = (crt_context_t *)data;

	rc = crt_context_idx(*p_ctx, &idx);
	D_ASSERTF(rc == 0, "crt_context_idx() failed; rc=%d\n", rc);

	rc = crt_context_uri_get(*p_ctx, &uri);
	D_ASSERTF(rc == 0, "crt_context_uri_get() failed; rc=%d\n", rc);

	DBG_PRINT("started context[%d] listening on address %s\n", idx, uri);

	/* main loop */
	while (do_shutdown == 0) {
		crt_progress(*p_ctx, 1000);

		if (idx == 0) {
			process_delayed_rpcs();
		}
	}

	rc = crt_context_destroy(*p_ctx, 1);
	D_ASSERTF(rc == 0, "Failed to destroy context %p rc=%d\n", p_ctx, rc);

	DBG_PRINT("context[%d] terminated\n", idx);
	pthread_exit(rc ? *p_ctx : NULL);

	return NULL;
}

int
main(int argc, char **argv)
{
	crt_context_t crt_ctx[NUM_SERVER_CTX];
	pthread_t     progress_thread[NUM_SERVER_CTX];
	int           i;
	int           rc;

	rc = d_log_init();
	assert(rc == 0);

	DBG_PRINT("Server starting up\n");

	if (getenv("D_PROVIDER") == NULL) {
		DBG_PRINT("Warning: D_PROVIDER was not set, assuming 'ofi+tcp'\n");
		setenv("D_PROVIDER", "ofi+tcp", 1);
	}

	if (getenv("D_INTERFACE") == NULL) {
		DBG_PRINT("Warning: D_INTERFACE was not set, assuming 'eth0'\n");
		setenv("D_INTERFACE", "eth0", 1);
	}

	if (getenv("D_PORT") == NULL) {
		DBG_PRINT("Warning: D_PORT was not set, setting to 31420\n");
		setenv("D_PORT", "31420", 1);
	}

	rc = crt_init("simple_server", CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	D_ASSERTF(rc == 0, "crt_init() failed; rc=%d\n", rc);

	rc = crt_rank_self_set(MY_RANK, GRP_VER);
	D_ASSERTF(rc == 0, "crt_rank_self_set(%d) failed; rc=%d\n", MY_RANK, rc);

	rc = crt_proto_register(&my_proto_fmt);
	if (rc != 0) {
		D_ERROR("crt_proto_register() failed; rc=%d\n", rc);
		assert(0);
	}

	for (i = 0; i < NUM_SERVER_CTX; i++) {
		rc = crt_context_create(&crt_ctx[i]);
		D_ASSERTF(rc == 0, "crt_context_create() failed; rc=%d\n", rc);

		rc = pthread_create(&progress_thread[i], 0, progress_fn, &crt_ctx[i]);
		D_ASSERTF(rc == 0, "pthread_create() failed; rc=%d\n", rc);
	}

	rc = crt_group_config_save(NULL, true);
	D_ASSERTF(rc == 0, "crt_group_config_save() failed\n");

	/* Wait until shutdown is issued and progress threads exit */
	for (i = 0; i < NUM_SERVER_CTX; i++)
		pthread_join(progress_thread[i], NULL);

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed with rc=%d\n", rc);

	d_log_fini();
	return 0;
}
