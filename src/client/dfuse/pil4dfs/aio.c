/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(il)

#include <libaio.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/syscall.h>

#include <daos.h>
#include <daos_fs.h>
#include <daos/debug.h>
#include <daos/common.h>

#include "pil4dfs_int.h"

extern _Atomic bool      d_daos_inited;
extern bool              d_compatible_mode;
extern bool              d_hook_enabled;
extern struct file_obj  *d_file_list[MAX_OPENED_FILE];

extern int
d_get_fd_redirected(int fd);

typedef struct d_aio_req_args {
	d_iov_t	    iov;
	d_sg_list_t sgl;
}d_aio_req_args_t;

struct d_aio_ev {
	daos_event_t      ev;
	struct iocb      *piocb;
	struct d_aio_ctx *ctx;
};

struct d_aio_ctx {
	/* the real io_context_t used in libaio */
	io_context_t  ctx;
	/* the depth of context set by io_setup */
	int           depth;
	daos_handle_t eq;
	bool          inited;
	/* DFS is involved or not for current context */
	bool          on_dfs;
	uint64_t      n_op_queued;
	uint64_t      n_op_done;
};

typedef struct d_aio_ctx d_aio_ctx_t;

static int (*next_io_setup)(int maxevents, io_context_t *ctxp);
static int (*next_io_destroy)(io_context_t ctx);
static int (*next_io_submit)(io_context_t ctx, long nr, struct iocb *ios[]);
static int (*next_io_cancel)(io_context_t ctx, struct iocb *iocb, struct io_event *evt);
static int (*next_io_getevents)(io_context_t ctx, long min_nr, long nr, struct io_event *events,
				struct timespec *timeout);

static int (*next_io_queue_init)(int maxevents, io_context_t *ctxp);
/* aio functions return negative errno in case of failure */

static int
create_ev_eq_for_aio(d_aio_ctx_t *aio_ctx);

int
io_setup(int maxevents, io_context_t *ctxp);

int
io_queue_init(int maxevents, io_context_t *ctxp)
{
	if (next_io_queue_init == NULL) {
		next_io_queue_init = dlsym(RTLD_NEXT, "io_queue_init");
		D_ASSERT(next_io_queue_init != NULL);
	}

	if (maxevents > 0) {
		return io_setup(maxevents, ctxp);
	}

	return -EINVAL;
}

int
io_setup(int maxevents, io_context_t *ctxp)
{
	int          rc;
	d_aio_ctx_t *aio_ctx_obj;

	if (next_io_setup == NULL) {
		next_io_setup = dlsym(RTLD_NEXT, "io_setup");
		D_ASSERT(next_io_setup != NULL);
	}

	rc = next_io_setup(maxevents, ctxp);
	if (rc < 0)
		return rc;

	D_ALLOC_PTR(aio_ctx_obj);
	if (aio_ctx_obj == NULL) {
		io_destroy(*ctxp);
		return -ENOMEM;
	}
	aio_ctx_obj->ctx         = *ctxp;
	aio_ctx_obj->depth       = maxevents;
	aio_ctx_obj->eq.cookie   = 0;
	aio_ctx_obj->n_op_queued = 0;
	aio_ctx_obj->n_op_done   = 0;
	aio_ctx_obj->inited      = false;
	aio_ctx_obj->on_dfs      = false;
	*ctxp                    = (io_context_t)aio_ctx_obj;

	if (!d_daos_inited)
		/* daos_init() is not called yet. Call create_ev_eq_for_aio() inside io_submit() */
		return 0;

	/* assume all io requests are over DFS for now. */
	rc = create_ev_eq_for_aio(aio_ctx_obj);
	if (rc) {
		return (-rc);
	}

	return 0;
}

int
io_destroy(io_context_t ctx)
{
	int          rc;
	d_aio_ctx_t *aio_ctx_obj = (d_aio_ctx_t *)ctx;
	io_context_t ctx_save    = aio_ctx_obj->ctx;

	if (next_io_destroy == NULL) {
		next_io_destroy = dlsym(RTLD_NEXT, "io_destroy");
		D_ASSERT(next_io_destroy != NULL);
	}
	rc = daos_eq_destroy(aio_ctx_obj->eq, 0);
	D_FREE(aio_ctx_obj);
	if (rc)
		return (-daos_der2errno(rc));

	return next_io_destroy(ctx_save);
}

static int
create_ev_eq_for_aio(d_aio_ctx_t *aio_ctx)
{
	int          rc;

	if (aio_ctx->inited)
		return 0;

	/* allocate EQs for aio context*/
	rc  = daos_eq_create(&aio_ctx->eq);
	if (rc)
		goto err;
	aio_ctx->on_dfs = true;
	aio_ctx->inited = true;

	return 0;

err:
	return daos_der2errno(rc);
}

static int
aio_req_cb(void *args, daos_event_t *ev, int ret)
{
	D_FREE(args);
	return 0;
}

int
io_submit(io_context_t ctx, long nr, struct iocb *ios[])
{
	d_aio_ctx_t      *aio_ctx_obj = (d_aio_ctx_t *)ctx;
	io_context_t      ctx_real    = aio_ctx_obj->ctx;
	daos_size_t       read_size   = 0;
	struct d_aio_ev  *ctx_ev      = NULL;
	d_aio_req_args_t *req_args;
	int               i, n_op_dfs, fd, io_depth;
	int               rc, rc2;
	short             op;
	int              *fd_directed = NULL;

	if (next_io_submit == NULL) {
		next_io_submit = dlsym(RTLD_NEXT, "io_submit");
		D_ASSERT(next_io_submit != NULL);
	}
	if (!d_hook_enabled)
		goto org;
	io_depth = aio_ctx_obj->depth;
	if (io_depth == 0)
		goto org;
	if (nr > io_depth)
		nr = io_depth;

	D_ALLOC_ARRAY(fd_directed, nr);
	if (fd_directed == NULL)
		D_GOTO(err, rc = ENOMEM);

	n_op_dfs = 0;
	for (i = 0; i < nr; i++) {
		fd_directed[i] = d_get_fd_redirected(ios[i]->aio_fildes);
		if (fd_directed[i] >= FD_FILE_BASE)
			n_op_dfs++;

		op = ios[i]->aio_lio_opcode;
		/* only support IO_CMD_PREAD and IO_CMD_PWRITE */
		if (op != IO_CMD_PREAD && op != IO_CMD_PWRITE) {
			DS_ERROR(EINVAL, "io_submit only supports PREAD and PWRITE for now");
			D_GOTO(err, rc = EINVAL);
		}
	}
	if (n_op_dfs == 0)
		goto org;

	if (n_op_dfs != nr) {
		if (d_compatible_mode)
			goto org;
		DS_ERROR(EINVAL, "io_submit() does not support mixed non-dfs and dfs files yet in"
			 " regular mode");
		D_GOTO(err, rc = EINVAL);
	}

	if (!aio_ctx_obj->inited) {
		rc = create_ev_eq_for_aio(aio_ctx_obj);
		if (rc)
			D_GOTO(err, rc);
	}

	for (i = 0; i < nr; i++) {
		op = ios[i]->aio_lio_opcode;
		fd = fd_directed[i] - FD_FILE_BASE;

		D_ALLOC_PTR(ctx_ev);
		if (ctx_ev == NULL)
			D_GOTO(err_loop, rc = ENOMEM);

		rc = daos_event_init(&ctx_ev->ev, aio_ctx_obj->eq, NULL);
		if (rc) {
			DL_ERROR(rc, "daos_event_init() failed");
			D_GOTO(err_loop, rc = daos_der2errno(rc));
		}
		ctx_ev->piocb = ios[i];
		/* EQs are shared by contexts. Need to save ctx when polling EQs. */
		ctx_ev->ctx = aio_ctx_obj;
		D_ALLOC_PTR(req_args);
		if (req_args == NULL)
			D_GOTO(err_loop, rc = ENOMEM);
		d_iov_set(&req_args->iov, (void *)ios[i]->u.c.buf, ios[i]->u.c.nbytes);
		req_args->sgl.sg_nr     = 1;
		req_args->sgl.sg_iovs   = &req_args->iov;

		if (op == IO_CMD_PREAD) {
			rc = daos_event_register_comp_cb(&ctx_ev->ev, aio_req_cb, req_args);
			if (rc) {
				DL_ERROR(rc, "daos_event_register_comp_cb() failed");
				D_GOTO(err_loop, rc);
			}
			rc = dfs_read(d_file_list[fd]->dfs_mt->dfs, d_file_list[fd]->file,
				      &req_args->sgl, ios[i]->u.c.offset, &read_size, &ctx_ev->ev);
			if (rc) {
				rc2 = daos_event_fini(&ctx_ev->ev);
				if (rc2)
					DL_ERROR(rc2, "daos_event_fini() failed");
				D_GOTO(err_loop, rc);
			}
		}
		if (op == IO_CMD_PWRITE) {
			rc = daos_event_register_comp_cb(&ctx_ev->ev, aio_req_cb, req_args);
			if (rc) {
				DL_ERROR(rc, "daos_event_register_comp_cb() failed");
				D_GOTO(err_loop, rc);
			}
			rc = dfs_write(d_file_list[fd]->dfs_mt->dfs, d_file_list[fd]->file,
				       &req_args->sgl, ios[i]->u.c.offset, &ctx_ev->ev);
			if (rc) {
				rc2 = daos_event_fini(&ctx_ev->ev);
				if (rc2)
					DL_ERROR(rc2, "daos_event_fini() failed");
				D_GOTO(err_loop, rc);
			}
		}
		aio_ctx_obj->n_op_queued++;
	}

	D_FREE(fd_directed);
	return nr;

org:
	D_FREE(fd_directed);
	return next_io_submit(ctx_real, nr, ios);

err:
	D_FREE(fd_directed);
	return (-rc);

err_loop:
	D_FREE(ctx_ev);
	D_FREE(fd_directed);

	return i ? i : (-rc);
}

int
io_cancel(io_context_t ctx, struct iocb *iocb, struct io_event *evt)
{
	d_aio_ctx_t *aio_ctx_obj = (d_aio_ctx_t *)ctx;
	io_context_t ctx_real    = aio_ctx_obj->ctx;

	if (next_io_cancel == NULL) {
		next_io_cancel = dlsym(RTLD_NEXT, "io_cancel");
		D_ASSERT(next_io_cancel != NULL);
	}
	if (!d_hook_enabled)
		return next_io_cancel(ctx_real, iocb, evt);

	if (iocb->aio_fildes < FD_FILE_BASE)
		return next_io_cancel(ctx_real, iocb, evt);

	/* daos_event_abort() may be used to implement this feature later. */
	DS_ERROR(ENOSYS, "io_cancel() for DFS is not implemented");
	return (-ENOSYS);
}

#define AIO_EQ_DEPTH MAX_EQ

/* poll the event queue of current aio context */
static void
aio_poll_eq(struct d_aio_ctx *ctx, long min_nr, long nr, struct io_event *events, int *num_ev)
{
	int                j;
	int                rc, rc2;
	struct daos_event *eps[AIO_EQ_DEPTH + 1] = {0};
	struct d_aio_ev   *p_aio_ev;

	if (ctx->n_op_queued == 0)
		return;

	rc = daos_eq_poll(ctx->eq, 0, DAOS_EQ_NOWAIT, min(AIO_EQ_DEPTH, nr - (*num_ev)), eps);
	if (rc < 0)
		DL_ERROR(rc, "daos_eq_poll() failed");

	for (j = 0; j < rc; j++) {
		if (eps[j]->ev_error) {
			DS_ERROR(eps[j]->ev_error, "daos_eq_poll() error");
		} else {
			ctx->n_op_queued--;
			ctx->n_op_done++;
			p_aio_ev = container_of(eps[j], struct d_aio_ev, ev);
			/* append to event list */
			events[*num_ev].obj = p_aio_ev->piocb;
			events[*num_ev].res = p_aio_ev->piocb->u.c.nbytes;

			rc2 = daos_event_fini(&p_aio_ev->ev);
			if (rc2)
				DL_ERROR(rc2, "daos_event_fini() failed");
			(*num_ev)++;
			D_FREE(p_aio_ev);
		}
	}

	return;
}

static int
io_getevents_sys(io_context_t ctx, long min_nr, long max_nr, struct io_event *events,
		 struct timespec *timeout)
{
	return syscall(__NR_io_getevents, ctx, min_nr, max_nr, events, timeout);
}

int
io_getevents(io_context_t ctx, long min_nr, long nr, struct io_event *events,
	     struct timespec *timeout)
{
	d_aio_ctx_t    *aio_ctx_obj = (d_aio_ctx_t *)ctx;
	io_context_t    ctx_real    = aio_ctx_obj->ctx;
	int             op_done     = 0;
	struct timespec times_0;
	struct timespec times_1;
	struct timespec dt;

	if (next_io_getevents == NULL)
		next_io_getevents = io_getevents_sys;

	if (!d_hook_enabled)
		return next_io_getevents(ctx_real, min_nr, nr, events, timeout);

	if (aio_ctx_obj->depth == 0)
		return next_io_getevents(ctx_real, min_nr, nr, events, timeout);
	if (!aio_ctx_obj->on_dfs)
		return next_io_getevents(ctx_real, min_nr, nr, events, timeout);
	if (!aio_ctx_obj->inited) {
		DS_ERROR(EINVAL, "event queue is not initialized yet");
		return (-EINVAL);
	}
	if (timeout)
		clock_gettime(CLOCK_REALTIME, &times_0);

	if (min_nr > AIO_EQ_DEPTH)
		min_nr = AIO_EQ_DEPTH;

	while (1) {
		aio_poll_eq(aio_ctx_obj, min_nr, nr, events, &op_done);
		if (op_done >= min_nr)
			return op_done;
		if (timeout) {
			clock_gettime(CLOCK_REALTIME, &times_1);
			dt.tv_sec  = times_1.tv_sec - times_0.tv_sec;
			dt.tv_nsec = times_1.tv_nsec - times_0.tv_nsec;
			if (dt.tv_nsec < 0) {
				dt.tv_sec--;
				dt.tv_nsec += 1000000000L;
			}
			if ((dt.tv_sec > timeout->tv_sec) ||
			    ((dt.tv_sec == timeout->tv_sec) && ((dt.tv_nsec > timeout->tv_nsec)))) {
				return op_done;
			}
		}
	}
	return op_done;
}
