#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <poll.h>

#include <crt_util/common.h>
#include "crt_internal.h"

static pthread_t	fake_event_tid;
static int		fake_event_thread_done;

static void *
fake_event_thread(void *args)
{
	char		*pipe_name;
	int		 event_code;
	crt_rank_t	 rank;
	FILE		*fifo_pipe = NULL;
	int		 fd;
	struct pollfd	 pollfds[1];
	int		 rc;

	pipe_name = (char *) args;

	while (fake_event_thread_done == 0) {
		fd = open(pipe_name, O_RDONLY | O_NONBLOCK);
		if (fd == -1) {
			C_ERROR("open() on fle %s failed. error: %s\n",
				pipe_name, strerror(errno));
			return NULL;
		}
		fifo_pipe = fdopen(fd, "r");
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
					C_DEBUG("fscanf reached end of file\n");
					break;
				}
				C_DEBUG("fscanf return code %d\n", rc);
				fprintf(stderr, "event code: %d rank: %d\n",
						event_code, rank);
				if (event_code == 0)
					crt_fake_event_notify_fn(rank);
			} while (rc != EOF);
		fclose(fifo_pipe);
	}

	free(pipe_name);

	return NULL;
}

int
crt_fake_event_init(int rank)
{
	char		*pipe_name;
	int		 length;
	int		 rc = 0;

	length = snprintf(NULL, 0, "/tmp/fake_event_pipe_%02d", 0);
	pipe_name = malloc(length + 1);
	if (pipe_name == NULL) {
		C_ERROR("malloc failed, rc: %d\n", rc);
		C_GOTO(out, rc = -CER_NOMEM);
	}
	snprintf(pipe_name, length + 1, "/tmp/fake_event_pipe_%02d", 0);
	mkfifo(pipe_name, 0666);
	C_DEBUG("Rank: %d, named pipe created: %s\n", rank, pipe_name);
	rc = pthread_create(&fake_event_tid, NULL, fake_event_thread, (void *)
			pipe_name);
	if (rc != 0)
		C_ERROR("fake_event_thread creation failed, return code: %d\n",
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
		C_ERROR("Couldn't join fake_event_thread, return code: %d\n",
			rc);
		C_GOTO(out, rc);
	}

	length = snprintf(NULL, 0, "/tmp/fake_event_pipe_%02d", 0);
	pipe_name = malloc(length + 1);
	if (pipe_name == NULL) {
		C_ERROR("malloc failed, rc: %d\n", rc);
		C_GOTO(out, rc = -CER_NOMEM);
	}
	snprintf(pipe_name, length + 1, "/tmp/fake_event_pipe_%02d", rank);
	remove(pipe_name);
	free(pipe_name);

out:
	return rc;
}
