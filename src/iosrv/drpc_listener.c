/*
 * (C) Copyright 2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/**
 * drpc_listener: dRPC Listener ULT
 *
 * This file contains the logic to initialize, run, and tear down the ULT that
 * will run the dRPC listener.
 */

#define D_LOGFAC		DD_FAC(drpc)

#include <abt.h>
#include <stddef.h>
#include <daos_srv/daos_server.h>
#include "drpc_handler.h"
#include "drpc_internal.h"

/*
 * Internal state of the dRPC listener thread.
 */
static struct drpc_listener_status {
	bool		running;	/* is the thread running? */
	ABT_mutex	running_mutex;	/* multiple threads touch it */
	ABT_thread	thread;		/* so we can cleanup when we're done */
} status;

static const int	SLEEP_TIME_MS = 1000; /* time to yield between cycles */
/* TODO: Make unique sock name from pid when 2+ io server instances */
static const char	*listener_socket_name = "daos_io_server.sock";

char			*drpc_listener_socket_path;

static bool
is_listener_running(void)
{
	bool result;

	ABT_mutex_lock(status.running_mutex);
	result = status.running;
	ABT_mutex_unlock(status.running_mutex);

	return result;
}

static void
set_listener_running(bool enable)
{
	ABT_mutex_lock(status.running_mutex);
	status.running = enable;
	ABT_mutex_unlock(status.running_mutex);
}

/*
 * The dRPC listener thread.
 */
static void
drpc_listener_run(void *arg)
{
	struct drpc_progress_context *ctx;

	D_ASSERT(arg != NULL);
	ctx = (struct drpc_progress_context *)arg;

	D_INFO("Starting dRPC listener\n");
	set_listener_running(true);
	while (is_listener_running()) {
		int rc;

		/* instant timeout - don't hog the xstream */
		rc = drpc_progress(ctx, 0);
		if (rc != DER_SUCCESS && rc != -DER_TIMEDOUT) {
			D_ERROR("dRPC listener progress error: %d\n", rc);
		}

		dss_sleep(SLEEP_TIME_MS); /* yield for a bit */
	}

	D_INFO("Closing down dRPC listener\n");
	drpc_progress_context_close(ctx);
}

/*
 * Stands up a drpc listener socket and creates a corresponding progress
 * context.
 */
static int
setup_listener_ctx(struct drpc_progress_context **new_ctx)
{
	struct drpc	*listener;
	char		*sockpath = drpc_listener_socket_path;

	/* If there's something already in the socket path, it will fail */
	unlink(sockpath);
	listener = drpc_listen(sockpath, drpc_hdlr_process_msg);
	if (listener == NULL) {
		D_ERROR("Failed to create listener socket at '%s'\n",
				sockpath);
		return -DER_UNKNOWN;
	}

	*new_ctx = drpc_progress_context_create(listener);
	if (*new_ctx == NULL) {
		D_ERROR("Failed to create drpc_progress_context\n");
		drpc_close(listener);
		return -DER_UNKNOWN;
	}

	return DER_SUCCESS;
}

/*
 * Sets up the listener socket and kicks off a ULT to listen on it.
 */
static int
drpc_listener_start_ult(ABT_thread *thread)
{
	int				rc;
	struct drpc_progress_context	*ctx = NULL;

	rc = setup_listener_ctx(&ctx);
	if (rc != 0) {
		D_ERROR("Listener setup failed, aborting ULT creation\n");
		return rc;
	}

	/* Create a ULT to start the drpc listener */
	rc = dss_ult_create(drpc_listener_run, (void *)ctx,
			    DSS_ULT_DRPC, 0, 0, thread);
	if (rc != 0) {
		D_ERROR("Failed to create drpc listener ULT: %d\n", rc);
		drpc_progress_context_close(ctx);
		return rc;
	}

	return DER_SUCCESS;
}

static int
generate_socket_path(void)
{
	int rc;

	rc = asprintf(&drpc_listener_socket_path, "%s/%s",
			dss_socket_dir, listener_socket_name);
	if (rc < 0) {
		D_ERROR("Failed to allocate socket path\n");
		return -DER_NOMEM;
	}

	return DER_SUCCESS;
}

int
drpc_listener_init(void)
{
	int rc;

	rc = generate_socket_path();
	if (rc != DER_SUCCESS) {
		return rc;
	}

	memset(&status, 0, sizeof(status));
	rc = ABT_mutex_create(&status.running_mutex);
	if (rc != ABT_SUCCESS) {
		D_ERROR("Failed to create mutex\n");
		return dss_abterr2der(rc);
	}

	return drpc_listener_start_ult(&status.thread);
}

/*
 * Updates the state to stop the thread, and waits for it to exit.
 */
static int
drpc_listener_stop(void)
{
	int rc;

	set_listener_running(false);

	rc = ABT_thread_join(status.thread);
	if (rc != ABT_SUCCESS) {
		D_ERROR("ABT error re-joining thread: %d\n", rc);
		return dss_abterr2der(rc);
	}

	return DER_SUCCESS;
}

int
drpc_listener_fini(void)
{
	int	rc;
	int	tmp_rc;

	rc = drpc_listener_stop();

	tmp_rc = ABT_thread_free(&status.thread);
	if (tmp_rc != ABT_SUCCESS) {
		D_ERROR("ABT error freeing thread: %d\n", tmp_rc);
		rc = dss_abterr2der(tmp_rc);
	}

	tmp_rc = ABT_mutex_free(&status.running_mutex);
	if (tmp_rc != ABT_SUCCESS) {
		D_ERROR("ABT error freeing mutex: %d\n", tmp_rc);
		rc = dss_abterr2der(tmp_rc);
	}

	D_FREE(drpc_listener_socket_path);

	return rc;
}
