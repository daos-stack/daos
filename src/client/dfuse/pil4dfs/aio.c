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

/* the number of EQs for aio contexts. (d_aio_eq_count_g + d_eq_count) <= d_eq_count_max */
uint16_t                 d_aio_eq_count_g;

extern pthread_mutex_t   d_lock_aio_eqs_g;
extern uint16_t          d_eq_count_max;
extern uint16_t          d_eq_count;
extern bool              d_hook_enabled;
extern struct file_obj  *d_file_list[MAX_OPENED_FILE];

struct d_aio_eq {
	daos_handle_t    eq;
	pthread_mutex_t  lock;
	_Atomic uint64_t n_op_queued;
	_Atomic uint64_t n_op_done;
};

/* The max number of event queues dedicated to AIO */
#define MAX_NUM_EQ_AIO  (16)
/* list of EQs dedicated for aio contexts. */
static struct d_aio_eq aio_eq_list[MAX_NUM_EQ_AIO];
/* The accumulated sum of iodepth for all created aio contexts. It is introduced to distribute IO
 * requests from aio context evenly over available EQs and minimize the chances EQs are used by
 * multiple aio contexts.
 */
static long int        io_depth_total_g;

struct d_aio_ev {
	daos_event_t      ev;
	struct iocb      *piocb;
	struct d_aio_ctx *ctx;
};

struct d_aio_ctx {
	/* the real io_context_t used in libaio */
	io_context_t      ctx;
	/* the depth of context set by io_setup */
	int               depth;
	int               first_eq;
	bool              inited;
	/* DFS is involved or not for current context */
	bool              on_dfs;
	_Atomic uint64_t  num_op_submitted;
	_Atomic uint64_t  num_op_done;
	pthread_mutex_t   lock;
	/* The array of finished ev. ev is removed from the array by io_getevents(). */
	struct d_aio_ev **ev_done_array;
	int               ev_done_h;
	int               ev_done_t;
	int               ev_queue_len;
};

typedef struct d_aio_ctx d_aio_ctx_t;

static int (*next_io_setup)(int maxevents, io_context_t *ctxp);
static int (*next_io_destroy)(io_context_t ctx);
static int (*next_io_submit)(io_context_t ctx, long nr, struct iocb *ios[]);
static int (*next_io_cancel)(io_context_t ctx, struct iocb *iocb, struct io_event *evt);
static int (*next_io_getevents)(io_context_t ctx, long min_nr, long nr, struct io_event *events,
				struct timespec *timeout);

/* free all EQs allocated for aio */

void
d_free_aio_ctx(void)
{
	int i;
	int rc;

	for (i = 0; i < d_aio_eq_count_g; i++) {
		rc = daos_eq_destroy(aio_eq_list[i].eq, 0);
		if (rc)
			DL_ERROR(rc, "daos_eq_destroy() failed");
		D_MUTEX_DESTROY(&aio_eq_list[i].lock);
	}
	d_aio_eq_count_g = 0;
}

/* aio functions return negative errno in case of failure */
static int
create_ev_eq_for_aio(d_aio_ctx_t *aio_ctx);

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
	aio_ctx_obj->ctx   = *ctxp;
	aio_ctx_obj->depth = maxevents;
	atomic_init(&aio_ctx_obj->num_op_submitted, 0);
	atomic_init(&aio_ctx_obj->num_op_done, 0);
	aio_ctx_obj->inited        = false;
	aio_ctx_obj->on_dfs        = false;
	aio_ctx_obj->ev_done_array = NULL;
	aio_ctx_obj->ev_done_h     = 0;
	aio_ctx_obj->ev_done_t     = 0;
	aio_ctx_obj->ev_queue_len  = 0;
	*ctxp                      = (io_context_t)aio_ctx_obj;

	rc = D_MUTEX_INIT(&aio_ctx_obj->lock, NULL);
	if (rc) {
		io_destroy(*ctxp);
		D_FREE(aio_ctx_obj);
		return -daos_der2errno(rc);
	}

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
	d_aio_ctx_t *aio_ctx_obj = (d_aio_ctx_t *)ctx;
	io_context_t ctx_save    = aio_ctx_obj->ctx;

	if (next_io_destroy == NULL) {
		next_io_destroy = dlsym(RTLD_NEXT, "io_destroy");
		D_ASSERT(next_io_destroy != NULL);
	}
	D_MUTEX_DESTROY(&aio_ctx_obj->lock);
	D_FREE(aio_ctx_obj->ev_done_array);
	D_FREE(aio_ctx_obj);
	return next_io_destroy(ctx_save);
}

static int
create_ev_eq_for_aio(d_aio_ctx_t *aio_ctx)
{
	int rc, rc2, i, j, idx;
	int num_aio_eq_free, num_aio_eq_create;

	D_MUTEX_LOCK(&d_lock_aio_eqs_g);
	num_aio_eq_free   = (int)d_eq_count_max - (int)d_eq_count - (int)d_aio_eq_count_g;
	num_aio_eq_create = min(aio_ctx->depth, num_aio_eq_free);
	num_aio_eq_create = min(num_aio_eq_create, MAX_NUM_EQ_AIO - d_aio_eq_count_g);

	if (num_aio_eq_create > 0) {
		/* allocate EQs for aio context*/
		for (i = 0; i < num_aio_eq_create; i++) {
			idx = d_aio_eq_count_g;
			rc  = daos_eq_create(&aio_eq_list[idx + i].eq);
			if (rc)
				goto free_eq;
			D_MUTEX_INIT(&aio_eq_list[idx + i].lock, NULL);
			atomic_store_relaxed(&aio_eq_list[idx + i].n_op_queued, 0);
			atomic_store_relaxed(&aio_eq_list[idx + i].n_op_done, 0);
		}
		d_aio_eq_count_g += num_aio_eq_create;
	}
	aio_ctx->first_eq = io_depth_total_g % d_aio_eq_count_g;
	io_depth_total_g += aio_ctx->depth;
	D_MUTEX_UNLOCK(&d_lock_aio_eqs_g);

	if (d_aio_eq_count_g == 0) {
		DS_ERROR(EBUSY, "no EQs created for AIO contexts");
		return EBUSY;
	}

	D_MUTEX_LOCK(&aio_ctx->lock);
	if (aio_ctx->inited == false) {
		D_ALLOC_ARRAY(aio_ctx->ev_done_array, aio_ctx->depth);
		if (aio_ctx->ev_done_array == NULL) {
			D_MUTEX_UNLOCK(&aio_ctx->lock);
			return ENOMEM;
		}
		/* empty queue */
		aio_ctx->ev_done_h        = 0;
		aio_ctx->ev_done_t        = 0;
		aio_ctx->ev_done_array[0] = NULL;
		aio_ctx->ev_queue_len     = 0;
		aio_ctx->inited           = true;
	}
	aio_ctx->on_dfs = true;
	D_MUTEX_UNLOCK(&aio_ctx->lock);

	return 0;

free_eq:
	for (j = 0; j < i; j++) {
		rc2 = daos_eq_destroy(aio_eq_list[idx + j].eq, 0);
		if (rc2)
			DL_ERROR(rc2, "daos_eq_destroy() failed");
	}
	D_MUTEX_UNLOCK(&d_lock_aio_eqs_g);
	return daos_der2errno(rc);
}

int
io_submit(io_context_t ctx, long nr, struct iocb *ios[])
{
	d_aio_ctx_t     *aio_ctx_obj = (d_aio_ctx_t *)ctx;
	io_context_t     ctx_real    = aio_ctx_obj->ctx;
	daos_size_t      read_size   = 0;
	d_iov_t          iov         = {};
	d_sg_list_t      sgl         = {};
	struct d_aio_ev *ctx_ev      = NULL;
	int              i, n_op_dfs, fd, io_depth;
	int              idx_aio_eq;
	int              rc, rc2;
	short            op;
	long int         aio_eq_count_local;

	if (next_io_submit == NULL) {
		next_io_submit = dlsym(RTLD_NEXT, "io_submit");
		D_ASSERT(next_io_submit != NULL);
	}
	if (!d_hook_enabled)
		return next_io_submit(ctx_real, nr, ios);
	io_depth = aio_ctx_obj->depth;
	if (io_depth == 0)
		return next_io_submit(ctx_real, nr, ios);
	if (nr > io_depth)
		nr = io_depth;

	n_op_dfs = 0;
	for (i = 0; i < nr; i++) {
		if (ios[i]->aio_fildes >= FD_FILE_BASE)
			n_op_dfs++;

		op = ios[i]->aio_lio_opcode;
		/* only support IO_CMD_PREAD and IO_CMD_PWRITE */
		if (op != IO_CMD_PREAD && op != IO_CMD_PWRITE) {
			DS_ERROR(EINVAL, "io_submit only supports PREAD and PWRITE for now");
			return (-EINVAL);
		}
	}
	if (n_op_dfs == 0)
		return next_io_submit(ctx_real, nr, ios);

	if (n_op_dfs != nr) {
		DS_ERROR(EINVAL, "io_submit() does not support mixed non-dfs and dfs files yet");
		return (-EINVAL);
	}

	aio_eq_count_local = d_aio_eq_count_g;
	for (i = 0; i < nr; i++) {
		op         = ios[i]->aio_lio_opcode;
		fd         = ios[i]->aio_fildes - FD_FILE_BASE;
		idx_aio_eq = (aio_ctx_obj->first_eq + i) % aio_eq_count_local;

		D_ALLOC_PTR(ctx_ev);
		if (ctx_ev == NULL)
			return i ? i : (-ENOMEM);

		rc = daos_event_init(&ctx_ev->ev, aio_eq_list[idx_aio_eq].eq, NULL);
		if (rc) {
			DL_ERROR(rc, "daos_event_init() failed");
			D_GOTO(err, rc = daos_der2errno(rc));
		}
		d_iov_set(&iov, (void *)ios[i]->u.c.buf, ios[i]->u.c.nbytes);
		sgl.sg_nr     = 1;
		sgl.sg_iovs   = &iov;
		ctx_ev->piocb = ios[i];
		/* EQs are shared by contexts. Need to save ctx when polling EQs. */
		ctx_ev->ctx = aio_ctx_obj;
		if (op == IO_CMD_PREAD) {
			rc = dfs_read(d_file_list[fd]->dfs_mt->dfs, d_file_list[fd]->file, &sgl,
				      ios[i]->u.c.offset, &read_size, &ctx_ev->ev);
			if (rc) {
				rc2 = daos_event_fini(&ctx_ev->ev);
				if (rc2)
					DL_ERROR(rc2, "daos_event_fini() failed");
				D_GOTO(err, rc);
			}
		}
		if (op == IO_CMD_PWRITE) {
			rc = dfs_write(d_file_list[fd]->dfs_mt->dfs, d_file_list[fd]->file, &sgl,
				       ios[i]->u.c.offset, &ctx_ev->ev);
			if (rc) {
				rc2 = daos_event_fini(&ctx_ev->ev);
				if (rc2)
					DL_ERROR(rc2, "daos_event_fini() failed");
				D_GOTO(err, rc);
			}
		}
		atomic_fetch_add_relaxed(&aio_ctx_obj->num_op_submitted, 1);
		atomic_fetch_add_relaxed(&aio_eq_list[idx_aio_eq].n_op_queued, 1);
	}

	return nr;

err:
	D_FREE(ctx_ev);

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

static int
ev_enqueue(struct d_aio_ctx *ctx, struct d_aio_ev *ev)
{
	D_MUTEX_LOCK(&ctx->lock);
        D_ASSERT(ctx->depth > ctx->ev_queue_len);
	ctx->ev_done_array[ctx->ev_done_t] = ev;
	ctx->ev_done_t++;
	if (ctx->ev_done_t >= ctx->depth)
		ctx->ev_done_t = 0;
	ctx->ev_queue_len++;
	D_MUTEX_UNLOCK(&ctx->lock);
	return 0;
}

static void
ev_dequeue_batch(struct d_aio_ctx *ctx, long min_nr, long nr, struct io_event *events, int *num_ev)
{
	int              rc;
	struct d_aio_ev *ev;

	D_MUTEX_LOCK(&ctx->lock);
	while (ctx->ev_queue_len > 0 && *num_ev < min_nr) {
		/* dequeue one ev record */
		ev = ctx->ev_done_array[ctx->ev_done_h];
		ctx->ev_done_h++;
		if (ctx->ev_done_h >= ctx->depth)
			ctx->ev_done_h = 0;
		ctx->ev_queue_len--;

		events[*num_ev].obj = ev->piocb;
		events[*num_ev].res = ev->piocb->u.c.nbytes;

		rc = daos_event_fini(&ev->ev);
		if (rc)
			DL_ERROR(rc, "daos_event_fini() failed");
		D_FREE(ev);
		(*num_ev)++;
	}
	D_MUTEX_UNLOCK(&ctx->lock);
}

#define AIO_EQ_DEPTH MAX_EQ

/* poll all EQs in our list */
static void
aio_poll_eqs(struct d_aio_ctx *ctx, long min_nr, long nr, struct io_event *events, int *num_ev)
{
	int                i, j;
	int                rc, rc2;
	struct daos_event *eps[AIO_EQ_DEPTH + 1] = {0};
	struct d_aio_ev   *p_aio_ev;

	ev_dequeue_batch(ctx, min_nr, nr, events, num_ev);
	if (*num_ev >= min_nr)
		return;

	/* loop over all EQs */
	for (i = 0; i < d_aio_eq_count_g; i++) {
		if (atomic_load_relaxed(&aio_eq_list[i].n_op_queued) == 0)
			continue;

		rc = daos_eq_poll(aio_eq_list[i].eq, 0, DAOS_EQ_NOWAIT, AIO_EQ_DEPTH, eps);
		if (rc < 0)
			DL_ERROR(rc, "daos_eq_poll() failed");

		for (j = 0; j < rc; j++) {
			if (eps[j]->ev_error) {
				DS_ERROR(eps[j]->ev_error, "daos_eq_poll() error");
			} else {
				atomic_fetch_add_relaxed(&aio_eq_list[i].n_op_queued, -1);
				atomic_fetch_add_relaxed(&aio_eq_list[i].n_op_done, 1);
				p_aio_ev = container_of(eps[j], struct d_aio_ev, ev);
				if (p_aio_ev->ctx == ctx) {
					/* append to event list */
					D_MUTEX_LOCK(&ctx->lock);
					events[*num_ev].obj = p_aio_ev->piocb;
					events[*num_ev].res = p_aio_ev->piocb->u.c.nbytes;

					rc2 = daos_event_fini(&p_aio_ev->ev);
					if (rc2)
						DL_ERROR(rc, "daos_event_fini() failed");
					(*num_ev)++;
					D_MUTEX_UNLOCK(&ctx->lock);
					D_FREE(p_aio_ev);
				} else {
					/* need to append context's finished queue */
					ev_enqueue(p_aio_ev->ctx, p_aio_ev);
				}
			}
		}
	}

	/* try again after polling */
	ev_dequeue_batch(ctx, min_nr, nr, events, num_ev);
	if (*num_ev >= min_nr)
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
		aio_poll_eqs(aio_ctx_obj, min_nr, nr, events, &op_done);
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
