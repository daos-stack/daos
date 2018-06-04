/* Copyright (C) 2017-2018 Intel Corporation
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
 * This file is a utility to simulate the RAS event notification mechanism.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <poll.h>

#include <cart/types.h>
#include <gurt/errno.h>
#include <gurt/common.h>
#include "crt_fake_events.h"

static pthread_t	fake_event_tid;
static int		fake_event_thread_done;
bool dead;


static void *
fake_event_thread(void *arg)
{
	char		*pipe_name;
	int		 event_code;
	d_rank_t	 rank;
	FILE		*fifo_pipe = NULL;
	int		 fd;
	struct pollfd	 pollfds[1];
	int		 rc;

	pipe_name = (char *) arg;

	while (fake_event_thread_done == 0) {
		fd = open(pipe_name, O_RDONLY | O_NONBLOCK);
		if (fd == -1) {
			D_ERROR("open() on fle %s failed. error: %s\n",
				pipe_name, strerror(errno));
			break;
		}
		fifo_pipe = fdopen(fd, "r");

		if (fifo_pipe == NULL) {
			D_ERROR("fdopen() failed. error: %s\n",
				strerror(errno));
			close(fd);
			break;
		}

		pollfds[0].fd = fd;
		pollfds[0].events = POLLIN;
		pollfds[0].revents = 0;
		rc = poll(pollfds, 1, 100);
		if (rc == 1 && pollfds[0].revents & POLLIN)
			do {
				rc = fscanf(fifo_pipe, "%d%d", &event_code,
					    &rank);
				if (rc == EOF) {
					/* reached EOF */
					D_DEBUG(DB_TEST,
						"fscanf reached end of file\n");
					break;
				}
				D_DEBUG(DB_TEST, "fscanf return code %d\n",
					rc);
				fprintf(stderr, "event code: %d rank: %d\n",
					event_code, rank);
				if (event_code == 0)
					crt_lm_fake_event_notify_fn(rank,
								    &dead);
			} while (rc != EOF);
		fclose(fifo_pipe);
	}

	free(pipe_name);

	return NULL;
}

int
crt_fake_event_init(d_rank_t rank)
{
	char		*pipe_name;
	int		 length;
	int		 rc = 0;

	length = snprintf(NULL, 0, "/tmp/fake_event_pipe_%02d", 0);
	pipe_name = malloc(length + 1);
	if (pipe_name == NULL) {
		D_ERROR("malloc failed, rc: %d\n", rc);
		D_GOTO(out, rc = -DER_NOMEM);
	}
	snprintf(pipe_name, length + 1, "/tmp/fake_event_pipe_%02d", 0);
	mkfifo(pipe_name, 0666);
	D_DEBUG(DB_TEST, "Rank: %d, named pipe created: %s\n",
		rank, pipe_name);
	rc = pthread_create(&fake_event_tid, NULL, fake_event_thread, (void *)
			    pipe_name);
	if (rc != 0)
		D_ERROR("fake_event_thread creation failed, return code: %d\n",
			rc);

out:
	return rc;
}

int
crt_fake_event_fini(int rank)
{
	char		*pipe_name;
	int		 length;
	void		*status;
	int		 rc = 0;

	fake_event_thread_done = 1;
	rc = pthread_join(fake_event_tid, &status);
	if (rc != 0) {
		D_ERROR("Couldn't join fake_event_thread, return code: %d\n",
			rc);
		D_GOTO(out, 0);
	}

	length = snprintf(NULL, 0, "/tmp/fake_event_pipe_%02d", 0);
	pipe_name = malloc(length + 1);
	if (pipe_name == NULL) {
		D_ERROR("malloc failed, rc: %d\n", rc);
		D_GOTO(out, rc = -DER_NOMEM);
	}
	snprintf(pipe_name, length + 1, "/tmp/fake_event_pipe_%02d", rank);
	remove(pipe_name);
	free(pipe_name);

out:
	return rc;
}
