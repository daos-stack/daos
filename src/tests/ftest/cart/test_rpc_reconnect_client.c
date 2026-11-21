/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * Exercise concurrent RPC submission on one client context while an external
 * controller interrupts and restarts the server. Keep the client context and
 * attached group alive throughout so recovery exercises cached transport state,
 * rather than establishing a fresh client connection.
 *
 * The controller must restart rank 0 with the same group name and listening
 * address/port. It waits for PRE_RESTART_READY, interrupts the server, waits for
 * SERVER_OUTAGE_OBSERVED, and then restarts it. POST_RESTART_COMPLETE reports the
 * required number of successful replies after the observed failure.
 *
 * --threads selects concurrent senders; --pre_restart_successes and
 * --post_restart_successes are aggregate targets across all senders.
 * --status_file optionally appends one marker per line for orchestration.
 * Without an interruption or successful recovery the client keeps trying;
 * the controller is responsible for enforcing an overall test timeout.
 */
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <semaphore.h>

#include "test_rpc_reconnect_common.h"

#define RPC_RECONNECT_MAX_THREADS 32

struct client_state {
	/* All senders use this context; lock protects test state, not RPC submission. */
	crt_context_t   context;
	crt_endpoint_t  server_ep;
	pthread_mutex_t lock;
	unsigned int    pre_restart_successes;
	unsigned int    post_restart_successes;
	unsigned int    pre_restart_target;
	unsigned int    post_restart_target;
	bool            pre_restart_complete;
	bool            outage_seen;
	bool            failed;
	bool            done;
	const char     *status_file;
};

struct worker_state {
	struct client_state *client;
	/* One outstanding RPC per worker; its callback publishes rc via this semaphore. */
	sem_t                completion;
	int                  rc;
	uint64_t             sequence;
	bool                 fatal;
};

struct shutdown_state {
	sem_t completion;
	int   rc;
};

static int
parse_uint(const char *value, unsigned int *result)
{
	char         *end;
	unsigned long parsed;

	errno  = 0;
	parsed = strtoul(value, &end, 10);
	if (errno != 0 || end == value || *end != '\0' || parsed > UINT_MAX)
		return -DER_INVAL;

	*result = parsed;
	return 0;
}

static void
write_status_marker(struct client_state *client, const char *marker)
{
	FILE *file;

	if (client->status_file == NULL)
		return;

	/* Closing each append makes the marker visible to the polling controller. */
	file = fopen(client->status_file, "a");
	D_ASSERTF(file != NULL, "Failed to open status file %s\n", client->status_file);
	D_ASSERTF(fprintf(file, "%s\n", marker) > 0, "Failed to write status marker %s\n", marker);
	D_ASSERTF(fclose(file) == 0, "Failed to close status file %s\n", client->status_file);
}

static void
rpc_complete_cb(const struct crt_cb_info *info)
{
	struct worker_state *worker = info->cci_arg;

	worker->rc = info->cci_rc;
	if (worker->rc == 0) {
		struct RPC_RECONNECT_PING_out *output = crt_reply_get(info->cci_rpc);

		/* Transport failures are retried, but an incorrect reply must fail the test. */
		if (output->sequence != worker->sequence) {
			worker->rc    = -DER_MISMATCH;
			worker->fatal = true;
		}
	}
	sem_post(&worker->completion);
}

static bool
client_is_done(struct client_state *client)
{
	bool done;

	pthread_mutex_lock(&client->lock);
	done = client->done;
	pthread_mutex_unlock(&client->lock);
	return done;
}

static void
record_rpc_result(struct worker_state *worker)
{
	struct client_state *client    = worker->client;
	bool                 pre_ready = false;
	bool                 outage    = false;
	bool                 complete  = false;
	bool                 failed    = false;

	/*
	 * Only failures after the initial success target count as an outage.
	 * Successes between readiness and that failure do not count toward recovery.
	 * The controller must verify a real restart; these counters alone only
	 * demonstrate successful RPCs following an observed RPC failure.
	 */
	pthread_mutex_lock(&client->lock);
	if (worker->fatal) {
		client->failed = true;
		client->done   = true;
		failed         = true;
	} else if (client->done) {
		/* Another worker has already completed or failed the test. */
	} else if (worker->rc == 0) {
		if (!client->pre_restart_complete) {
			client->pre_restart_successes++;
			if (client->pre_restart_successes >= client->pre_restart_target) {
				client->pre_restart_complete = true;
				pre_ready                    = true;
			}
		} else if (client->outage_seen) {
			client->post_restart_successes++;
			if (client->post_restart_successes >= client->post_restart_target) {
				client->done = true;
				complete     = true;
			}
		}
	} else if (client->pre_restart_complete && !client->outage_seen) {
		client->outage_seen = true;
		outage              = true;
	}
	pthread_mutex_unlock(&client->lock);

	if (failed)
		D_ERROR("Fatal RPC test error; rc=%d\n", worker->rc);
	if (pre_ready) {
		DBG_PRINT("PRE_RESTART_READY successes=%u\n", client->pre_restart_target);
		write_status_marker(client, "PRE_RESTART_READY");
	}
	if (outage) {
		DBG_PRINT("SERVER_OUTAGE_OBSERVED rc=%d\n", worker->rc);
		write_status_marker(client, "SERVER_OUTAGE_OBSERVED");
	}
	if (complete) {
		DBG_PRINT("POST_RESTART_COMPLETE successes=%u\n", client->post_restart_target);
		write_status_marker(client, "POST_RESTART_COMPLETE");
	}
}

static void *
send_worker(void *arg)
{
	struct worker_state *worker = arg;
	struct client_state *client = worker->client;

	while (!client_is_done(client)) {
		struct RPC_RECONNECT_PING_in *input;
		crt_rpc_t                    *rpc;
		int                           rc;

		worker->sequence++;
		worker->fatal = false;
		/* Do not serialize create/send across workers: this is the race under test. */
		rc = crt_req_create(client->context, &client->server_ep, RPC_RECONNECT_PING_OPCODE,
				    &rpc);
		if (rc == 0) {
			input           = crt_req_get(rpc);
			input->sequence = worker->sequence;

			/* Bound outage detection per attempt while continuing with fresh RPCs. */
			rc = crt_req_set_timeout(rpc, 5);
			if (rc == 0)
				rc = crt_req_send(rpc, rpc_complete_cb, worker);
			else {
				worker->fatal = true;
				crt_req_decref(rpc);
			}
		} else {
			worker->fatal = true;
			worker->rc    = rc;
			record_rpc_result(worker);
			continue;
		}

		/* The progress thread runs callbacks, including callbacks invoked by send. */
		if (rc != 0) {
			worker->rc = rc;
		} else {
			while (sem_wait(&worker->completion) != 0) {
				if (errno != EINTR) {
					worker->rc    = -DER_MISC;
					worker->fatal = true;
					break;
				}
			}
		}
		record_rpc_result(worker);
	}

	return NULL;
}

static void
shutdown_complete_cb(const struct crt_cb_info *info)
{
	struct shutdown_state *state = info->cci_arg;

	state->rc = info->cci_rc;
	sem_post(&state->completion);
}

static void
test_run(char *local_group_name, char *server_group_name, unsigned int thread_count,
	 unsigned int pre_restart_target, unsigned int post_restart_target, char *status_file)
{
	struct client_state               client                             = {0};
	struct worker_state               workers[RPC_RECONNECT_MAX_THREADS] = {0};
	struct shutdown_state             shutdown                           = {0};
	pthread_t                         progress_thread;
	pthread_t                         worker_threads[RPC_RECONNECT_MAX_THREADS];
	crt_group_t                      *group     = NULL;
	d_rank_list_t                    *rank_list = NULL;
	crt_rpc_t                        *rpc;
	struct RPC_RECONNECT_SHUTDOWN_in *shutdown_input;
	unsigned int                      workers_started = 0;
	unsigned int                      i;
	int                               rc;

	client.status_file = status_file;
	/* This helper creates the only client context and its dedicated progress thread. */
	rc = crtu_cli_start_basic(local_group_name, server_group_name, &group, &rank_list,
				  &client.context, &progress_thread, 1, true, NULL, false);
	D_ASSERTF(rc == 0, "crtu_cli_start_basic() failed; rc=%d\n", rc);

	rc = crt_proto_register(&rpc_reconnect_proto_fmt);
	D_ASSERTF(rc == 0, "crt_proto_register() failed; rc=%d\n", rc);

	client.server_ep.ep_grp    = group;
	client.server_ep.ep_rank   = 0;
	client.pre_restart_target  = pre_restart_target;
	client.post_restart_target = post_restart_target;

	rc = pthread_mutex_init(&client.lock, NULL);
	D_ASSERTF(rc == 0, "pthread_mutex_init() failed; rc=%d\n", rc);

	for (i = 0; i < thread_count; i++) {
		workers[i].client = &client;
		rc                = sem_init(&workers[i].completion, 0, 0);
		D_ASSERTF(rc == 0, "sem_init() failed; rc=%d\n", rc);
		rc = pthread_create(&worker_threads[i], NULL, send_worker, &workers[i]);
		D_ASSERTF(rc == 0, "pthread_create() failed; rc=%d\n", rc);
		workers_started++;
	}

	DBG_PRINT("Started %u RPC sender threads sharing one CaRT context\n", thread_count);
	for (i = 0; i < workers_started; i++) {
		rc = pthread_join(worker_threads[i], NULL);
		D_ASSERTF(rc == 0, "pthread_join() failed; rc=%d\n", rc);
	}

	DBG_PRINT("Client completed with %u successes before restart and %u after restart\n",
		  client.pre_restart_successes, client.post_restart_successes);
	D_ASSERTF(client.done && !client.failed,
		  "Client did not complete successfully; done=%d failed=%d\n", client.done,
		  client.failed);

	/* Drain all sender threads before asking the recovered server to exit normally. */
	rc = sem_init(&shutdown.completion, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() for shutdown failed; rc=%d\n", rc);
	rc = crt_req_create(client.context, &client.server_ep, RPC_RECONNECT_SHUTDOWN_OPCODE, &rpc);
	D_ASSERTF(rc == 0 && rpc != NULL, "crt_req_create() for shutdown failed; rc=%d\n", rc);
	shutdown_input        = crt_req_get(rpc);
	shutdown_input->field = 0;
	rc                    = crt_req_send(rpc, shutdown_complete_cb, &shutdown);
	D_ASSERTF(rc == 0, "crt_req_send() for shutdown failed; rc=%d\n", rc);
	while (sem_wait(&shutdown.completion) != 0)
		D_ASSERTF(errno == EINTR, "sem_wait() for shutdown failed; errno=%d\n", errno);
	D_ASSERTF(shutdown.rc == 0, "Shutdown RPC failed; rc=%d\n", shutdown.rc);

	crtu_progress_stop();
	rc = pthread_join(progress_thread, NULL);
	D_ASSERTF(rc == 0, "pthread_join() for progress thread failed; rc=%d\n", rc);

	rc = crt_group_detach(group);
	D_ASSERTF(rc == 0, "crt_group_detach() failed; rc=%d\n", rc);
	d_rank_list_free(rank_list);

	for (i = 0; i < thread_count; i++) {
		rc = sem_destroy(&workers[i].completion);
		D_ASSERTF(rc == 0, "sem_destroy() failed; rc=%d\n", rc);
	}
	rc = pthread_mutex_destroy(&client.lock);
	D_ASSERTF(rc == 0, "pthread_mutex_destroy() failed; rc=%d\n", rc);
	rc = sem_destroy(&shutdown.completion);
	D_ASSERTF(rc == 0, "sem_destroy() for shutdown failed; rc=%d\n", rc);

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed; rc=%d\n", rc);
	d_log_fini();
}

int
main(int argc, char **argv)
{
	static struct option long_options[] = {
	    {"name", required_argument, NULL, 'g'},
	    {"attach_to", required_argument, NULL, 'a'},
	    {"threads", required_argument, NULL, 'e'},
	    {"pre_restart_successes", required_argument, NULL, 'p'},
	    {"post_restart_successes", required_argument, NULL, 'n'},
	    {"status_file", required_argument, NULL, 's'},
	    {0, 0, 0, 0},
	};
	char        *local_group_name    = "rpc_reconnect_client";
	char        *server_group_name   = NULL;
	char        *status_file         = NULL;
	unsigned int thread_count        = 8;
	unsigned int pre_restart_target  = 100;
	unsigned int post_restart_target = 100;
	int          parse_rc            = 0;
	int          option;

	while ((option = getopt_long(argc, argv, "g:a:e:p:n:s:", long_options, NULL)) != -1) {
		switch (option) {
		case 'g':
			local_group_name = optarg;
			break;
		case 'a':
			server_group_name = optarg;
			break;
		case 'e':
			parse_rc = parse_uint(optarg, &thread_count);
			break;
		case 'p':
			parse_rc = parse_uint(optarg, &pre_restart_target);
			break;
		case 'n':
			parse_rc = parse_uint(optarg, &post_restart_target);
			break;
		case 's':
			status_file = optarg;
			break;
		default:
			return 1;
		}
	}
	if (parse_rc != 0 || optind != argc || server_group_name == NULL || thread_count < 2 ||
	    thread_count > RPC_RECONNECT_MAX_THREADS || pre_restart_target == 0 ||
	    post_restart_target == 0) {
		fprintf(stderr,
			"Usage: %s --attach_to <group> [--name <group>] "
			"[--threads 2-%u] [--pre_restart_successes N] "
			"[--post_restart_successes N]\n",
			argv[0], RPC_RECONNECT_MAX_THREADS);
		return 1;
	}

	crtu_test_init(0, 40, false, true);
	test_run(local_group_name, server_group_name, thread_count, pre_restart_target,
		 post_restart_target, status_file);
	return 0;
}
