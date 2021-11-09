/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC       DD_FAC(server)

#include <abt.h>
#include <daos/common.h>
#include <daos_errno.h>
#include "srv_internal.h"

/* ============== Thread collective functions ============================ */

struct aggregator_arg_type {
	struct dss_stream_arg_type	at_args;
	void				(*at_reduce)(void *a_args,
						     void *s_args);
	int				at_rc;
	int				at_xs_nr;
};

/**
 * Collective operations among all server xstreams
 */
struct dss_future_arg {
	ABT_future	dfa_future;
	int		(*dfa_func)(void *);
	void		*dfa_arg;
	/** User callback for asynchronous mode */
	void		(*dfa_comp_cb)(void *);
	/** Argument for the user callback */
	void		*dfa_comp_arg;
	int		dfa_status;
	bool		dfa_async;
};

struct collective_arg {
	struct dss_future_arg		ca_future;
};

static void
collective_func(void *varg)
{
	struct dss_stream_arg_type	*a_args	= varg;
	struct collective_arg		*carg	= a_args->st_coll_args;
	struct dss_future_arg		*f_arg	= &carg->ca_future;

	/** Update just the rc value */
	a_args->st_rc = f_arg->dfa_func(f_arg->dfa_arg);

	DABT_FUTURE_SET(f_arg->dfa_future, (void *)a_args);
}

/* Reduce the return codes into the first element. */
static void
collective_reduce(void **arg)
{
	struct aggregator_arg_type	*aggregator;
	struct dss_stream_arg_type	*stream;
	int				*nfailed;
	int				 i;

	aggregator = (struct aggregator_arg_type *)arg[0];
	nfailed = &aggregator->at_args.st_rc;

	for (i = 1; i < aggregator->at_xs_nr + 1; i++) {
		stream = (struct dss_stream_arg_type *)arg[i];
		if (stream->st_rc != 0) {
			if (aggregator->at_rc == 0)
				aggregator->at_rc = stream->st_rc;
			(*nfailed)++;
		}

		/** optional custom aggregator call provided across streams */
		if (aggregator->at_reduce)
			aggregator->at_reduce(aggregator->at_args.st_arg,
					      stream->st_arg);
	}
}

static int
dss_collective_reduce_internal(struct dss_coll_ops *ops,
			       struct dss_coll_args *args, bool create_ult,
			       unsigned int flags)
{
	struct collective_arg		carg;
	struct dss_coll_stream_args	*stream_args;
	struct dss_stream_arg_type	*stream;
	struct aggregator_arg_type	aggregator;
	struct dss_xstream		*dx;
	ABT_future			future;
	int				xs_nr;
	int				rc;
	int				tid;

	if (ops == NULL || args == NULL || ops->co_func == NULL) {
		D_DEBUG(DB_MD, "mandatory args missing dss_collective_reduce");
		return -DER_INVAL;
	}

	if (ops->co_reduce_arg_alloc != NULL &&
	    ops->co_reduce_arg_free == NULL) {
		D_DEBUG(DB_MD, "Free callback missing for reduce args\n");
		return -DER_INVAL;
	}

	if (dss_tgt_nr == 0) {
		/* May happen when the server is shutting down. */
		D_DEBUG(DB_TRACE, "no xstreams\n");
		return -DER_CANCELED;
	}

	xs_nr = dss_tgt_nr;
	stream_args = &args->ca_stream_args;
	D_ALLOC_ARRAY(stream_args->csa_streams, xs_nr);
	if (stream_args->csa_streams == NULL)
		return -DER_NOMEM;

	/*
	 * Use the first, extra element of the value array to store the number
	 * of failed tasks.
	 */
	rc = ABT_future_create(xs_nr + 1, collective_reduce, &future);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_streams, rc = dss_abterr2der(rc));

	carg.ca_future.dfa_future = future;
	carg.ca_future.dfa_func	= ops->co_func;
	carg.ca_future.dfa_arg	= args->ca_func_args;
	carg.ca_future.dfa_status = 0;

	memset(&aggregator, 0, sizeof(aggregator));
	aggregator.at_xs_nr = xs_nr;
	if (ops->co_reduce) {
		aggregator.at_args.st_arg = args->ca_aggregator;
		aggregator.at_reduce	  = ops->co_reduce;
	}

	if (ops->co_reduce_arg_alloc)
		for (tid = 0; tid < xs_nr; tid++) {
			stream = &stream_args->csa_streams[tid];
			rc = ops->co_reduce_arg_alloc(stream,
						     aggregator.at_args.st_arg);
			if (rc)
				D_GOTO(out_future, rc);
		}

	DABT_FUTURE_SET(future, (void *)&aggregator);
	for (tid = 0; tid < xs_nr; tid++) {
		stream			= &stream_args->csa_streams[tid];
		stream->st_coll_args	= &carg;

		if (args->ca_exclude_tgts_cnt) {
			int i;

			for (i = 0; i < args->ca_exclude_tgts_cnt; i++)
				if (args->ca_exclude_tgts[i] == tid)
					break;

			if (i < args->ca_exclude_tgts_cnt) {
				D_DEBUG(DB_TRACE, "Skip tgt %d\n", tid);
				DABT_FUTURE_SET(future, (void *)stream);
				continue;
			}
		}

		dx = dss_get_xstream(DSS_MAIN_XS_ID(tid));
		if (create_ult) {
			ABT_thread_attr		attr;
			int			rc1;

			if (flags & DSS_ULT_DEEP_STACK) {
				rc1 = ABT_thread_attr_create(&attr);
				if (rc1 != ABT_SUCCESS)
					D_GOTO(next, rc = dss_abterr2der(rc1));

				rc1 = ABT_thread_attr_set_stacksize(attr, DSS_DEEP_STACK_SZ);
				D_ASSERT(rc1 == ABT_SUCCESS);

				D_DEBUG(DB_TRACE, "Create collective ult with stacksize %d\n",
					DSS_DEEP_STACK_SZ);

			} else {
				attr = ABT_THREAD_ATTR_NULL;
			}

			rc = sched_create_thread(dx, collective_func, stream, attr, NULL, flags);
			if (attr != ABT_THREAD_ATTR_NULL) {
				rc1 = ABT_thread_attr_free(&attr);
				D_ASSERT(rc1 == ABT_SUCCESS);
			}
		} else {
			rc = sched_create_task(dx, collective_func, stream,
					       NULL, flags);
		}

		if (rc != 0) {
next:
			stream->st_rc = rc;
			DABT_FUTURE_SET(future, (void *)stream);
		}
	}

	ABT_future_wait(future);

	rc = aggregator.at_rc;

out_future:
	ABT_future_free(&future);

	if (ops->co_reduce_arg_free)
		for (tid = 0; tid < xs_nr; tid++)
			ops->co_reduce_arg_free(&stream_args->csa_streams[tid]);

out_streams:
	D_FREE(args->ca_stream_args.csa_streams);

	return rc;
}

/**
 * General case:
 * Execute \a task(\a arg) collectively on all server xstreams. Can only be
 * called by ULTs. Can only execute tasklet-compatible functions. User specified
 * reduction functions for aggregation after collective
 *
 * \param[in] ops		All dss_collective ops to work on streams
 *				include \a func(\a arg) for collective on all
 *				server xstreams.
 * \param[in] args		All arguments required for dss_collective
 *				including func args.
 * \param[in] flags		Flags of dss_ult_flags
 *
 * \return			number of failed xstreams or error code
 */
int
dss_task_collective_reduce(struct dss_coll_ops *ops,
			   struct dss_coll_args *args, unsigned int flags)
{
	return dss_collective_reduce_internal(ops, args, false, flags);
}

/**
 * General case:
 * Execute \a ULT(\a arg) collectively on all server xstreams. Can only be
 * called by ULTs. Can only execute tasklet-compatible functions. User specified
 * reduction functions for aggregation after collective
 *
 * \param[in] ops		All dss_collective ops to work on streams
 *				include \a func(\a arg) for collective on all
 *				server xstreams.
 * \param[in] args		All arguments required for dss_collective
 *				including func args.
 * \param[in] flags		Flags from dss_ult_flags
 *
 * \return			number of failed xstreams or error code
 */
int
dss_thread_collective_reduce(struct dss_coll_ops *ops,
			     struct dss_coll_args *args, unsigned int flags)
{
	return dss_collective_reduce_internal(ops, args, true, flags);
}

static int
dss_collective_internal(int (*func)(void *), void *arg, bool thread,
			unsigned int flags)
{
	int				rc;
	struct dss_coll_ops		coll_ops = { 0 };
	struct dss_coll_args		coll_args = { 0 };

	coll_ops.co_func	= func;
	coll_args.ca_func_args	= arg;

	if (thread)
		rc = dss_thread_collective_reduce(&coll_ops, &coll_args, flags);
	else
		rc = dss_task_collective_reduce(&coll_ops, &coll_args, flags);

	return rc;
}

/**
 * Execute \a func(\a arg) collectively on all server xstreams. Can only be
 * called by ULTs. Can only execute tasklet-compatible functions.
 *
 * \param[in] func	function to be executed
 * \param[in] arg	argument to be passed to \a func
 * \param[in] flags	Flags from dss_ult_flags
 *
 * \return		number of failed xstreams or error code
 */
int
dss_task_collective(int (*func)(void *), void *arg, unsigned int flags)
{
	return dss_collective_internal(func, arg, false, flags);
}

/**
 * Execute \a func(\a arg) collectively on all server xstreams. Can only be
 * called by ULTs. Can only execute tasklet-compatible functions.
 *
 * \param[in] func	function to be executed
 * \param[in] arg	argument to be passed to \a func
 * \param[in] flags	Flags from dss_ult_flags
 *
 * \return		number of failed xstreams or error code
 */

int
dss_thread_collective(int (*func)(void *), void *arg, unsigned int flags)
{
	return dss_collective_internal(func, arg, true, flags);
}

/* ============== ULT create functions =================================== */

static inline int
sched_ult2xs(int xs_type, int tgt_id)
{
	uint32_t	xs_id;

	D_ASSERT(tgt_id >= 0 && tgt_id < dss_tgt_nr);
	switch (xs_type) {
	case DSS_XS_SELF:
		return DSS_XS_SELF;
	case DSS_XS_SYS:
		return 0;
	case DSS_XS_SWIM:
		return 1;
	case DSS_XS_DRPC:
		return 2;
	case DSS_XS_IOFW:
		if (!dss_helper_pool) {
			if (dss_tgt_offload_xs_nr > 0)
				xs_id = DSS_MAIN_XS_ID(tgt_id) + 1;
			else
				xs_id = DSS_MAIN_XS_ID((tgt_id + 1) % dss_tgt_nr);
			break;
		}

		/*
		 * Comment from @liuxuezhao:
		 *
		 * This is the case that no helper XS, so for IOFW,
		 * we either use itself, or use neighbor XS.
		 *
		 * Why original code select neighbor XS rather than itself
		 * is because, when the code is called, I know myself is on
		 * processing IO request and need IO forwarding, now I am
		 * processing IO, so likely there is not only one IO (possibly
		 * more than one IO for specific dkey), I am busy so likely my
		 * neighbor is not busy (both busy seems only in some special
		 * multiple dkeys used at same time) can help me do the IO
		 * forwarding?
		 *
		 * But this is just original intention, you guys think it is
		 * not reasonable? prefer another way that I am processing IO
		 * and need IO forwarding, OK, just let myself do it ...
		 *
		 * Note that we first do IO forwarding and then serve local IO,
		 * ask neighbor to do IO forwarding seems is helpful to make
		 * them concurrent, right?
		 */
		if (dss_tgt_offload_xs_nr >= dss_tgt_nr)
			xs_id = dss_sys_xs_nr + dss_tgt_nr + tgt_id;
		else if (dss_tgt_offload_xs_nr > 0)
			xs_id = dss_sys_xs_nr + dss_tgt_nr + tgt_id % dss_tgt_offload_xs_nr;
		else
			xs_id = (DSS_MAIN_XS_ID(tgt_id) + 1) % dss_tgt_nr;
		break;
	case DSS_XS_OFFLOAD:
		if (!dss_helper_pool) {
			if (dss_tgt_offload_xs_nr > 0)
				xs_id = DSS_MAIN_XS_ID(tgt_id) + dss_tgt_offload_xs_nr / dss_tgt_nr;
			else
				xs_id = DSS_MAIN_XS_ID((tgt_id + 1) % dss_tgt_nr);
			break;
		}

		if (dss_tgt_offload_xs_nr > dss_tgt_nr)
			xs_id = dss_sys_xs_nr + 2 * dss_tgt_nr +
				(tgt_id % (dss_tgt_offload_xs_nr - dss_tgt_nr));
		else if (dss_tgt_offload_xs_nr > 0)
			xs_id = dss_sys_xs_nr + dss_tgt_nr + tgt_id % dss_tgt_offload_xs_nr;
		else
			xs_id = (DSS_MAIN_XS_ID(tgt_id) + 1) % dss_tgt_nr;
		break;
	case DSS_XS_VOS:
		xs_id = DSS_MAIN_XS_ID(tgt_id);
		break;
	default:
		D_ASSERTF(0, "Invalid xstream type %d.\n", xs_type);
		return -DER_INVAL;
	}
	D_ASSERT(xs_id < DSS_XS_NR_TOTAL && xs_id >= dss_sys_xs_nr);
	return xs_id;
}

static int
ult_create_internal(void (*func)(void *), void *arg, int xs_type, int tgt_idx,
		    size_t stack_size, ABT_thread *ult, unsigned int flags)
{
	ABT_thread_attr		 attr;
	struct dss_xstream	*dx;
	int			 rc, rc1;

	dx = dss_get_xstream(sched_ult2xs(xs_type, tgt_idx));
	if (dx == NULL)
		return -DER_NONEXIST;

	if (stack_size > 0) {
		rc = ABT_thread_attr_create(&attr);
		if (rc != ABT_SUCCESS)
			return dss_abterr2der(rc);

		rc = ABT_thread_attr_set_stacksize(attr, stack_size);
		D_ASSERT(rc == ABT_SUCCESS);

		D_DEBUG(DB_TRACE, "Create ult stacksize is %zd\n", stack_size);
	} else {
		attr = ABT_THREAD_ATTR_NULL;
	}

	rc = sched_create_thread(dx, func, arg, attr, ult, flags);
	if (attr != ABT_THREAD_ATTR_NULL) {
		rc1 = ABT_thread_attr_free(&attr);
		D_ASSERT(rc1 == ABT_SUCCESS);
	}

	return rc;
}

/**
 * Create a ULT to execute \a func(\a arg). If \a ult is not NULL, the caller
 * is responsible for freeing the ULT handle with ABT_thread_free().
 *
 * \param[in]	func		function to execute
 * \param[in]	arg		argument for \a func
 * \param[in]	xs_type		xstream type
 * \param[in]	tgt_idx		VOS target index
 * \param[in]	stack_size	stacksize of the ULT, if it is 0, then create
 *				default size of ULT.
 * \param[out]	ult		ULT handle if not NULL
 */
int
dss_ult_create(void (*func)(void *), void *arg, int xs_type, int tgt_idx,
	       size_t stack_size, ABT_thread *ult)
{
	return ult_create_internal(func, arg, xs_type, tgt_idx, stack_size,
				   ult, 0);
}

int
dss_ult_periodic(void (*func)(void *), void *arg, int xs_type, int tgt_idx,
		 size_t stack_size, ABT_thread *ult)
{
	return ult_create_internal(func, arg, xs_type, tgt_idx, stack_size,
				   ult, DSS_ULT_FL_PERIODIC);
}

static void
ult_execute_cb(void *data)
{
	struct dss_future_arg	*arg = data;
	int			rc;

	rc = arg->dfa_func(arg->dfa_arg);
	arg->dfa_status = rc;

	if (!arg->dfa_async)
		DABT_FUTURE_SET(arg->dfa_future, (void *)(intptr_t)rc);
	else
		arg->dfa_comp_cb(arg->dfa_comp_arg);
}

/**
 * Execute a function in a separate ULT synchornously or asynchronously.
 *
 * Sync: wait until it has been executed.
 * Async: return and call user callback from ULT.
 * Note: This is normally used when it needs to create an ULT on other
 * xstream.
 *
 * \param[in]	func		function to execute
 * \param[in]	arg		argument for \a func
 * \param[in]	user_cb		user call back (mandatory for async mode)
 * \param[in]	arg		argument for \a user callback
 * \param[in]	xs_type		xstream type
 * \param[in]	tgt_id		target index
 * \param[in]	stack_size	stacksize of the ULT, if it is 0, then create
 *				default size of ULT.
 */
int
dss_ult_execute(int (*func)(void *), void *arg, void (*user_cb)(void *),
		void *cb_args, int xs_type, int tgt_id, size_t stack_size)
{
	struct dss_future_arg	future_arg;
	ABT_future		future;
	int			rc;

	memset(&future_arg, 0, sizeof(future_arg));
	future_arg.dfa_func = func;
	future_arg.dfa_arg = arg;
	future_arg.dfa_status = 0;

	if (user_cb == NULL) {
		rc = ABT_future_create(1, NULL, &future);
		if (rc != ABT_SUCCESS)
			return dss_abterr2der(rc);
		future_arg.dfa_future = future;
		future_arg.dfa_async  = false;
	} else {
		future_arg.dfa_comp_cb	= user_cb;
		future_arg.dfa_comp_arg = cb_args;
		future_arg.dfa_async	= true;
	}

	rc = dss_ult_create(ult_execute_cb, &future_arg, xs_type, tgt_id,
			    stack_size, NULL);
	if (rc)
		D_GOTO(free, rc);

	if (!future_arg.dfa_async)
		ABT_future_wait(future);
free:
	if (rc == 0)
		rc = future_arg.dfa_status;

	if (!future_arg.dfa_async)
		ABT_future_free(&future);

	return rc;
}

/**
 * Create an ULT on each server xstream to execute a \a func(\a arg)
 *
 * \param[in] func	function to be executed
 * \param[in] arg	argument to be passed to \a func
 * \param[in] main	only create ULT on main XS or not.
 *
 * \return		Success or negative error code
 *			0
 *			-DER_NOMEM
 *			-DER_INVAL
 */
int
dss_ult_create_all(void (*func)(void *), void *arg, bool main)
{
	struct dss_xstream      *dx;
	int			 i, rc = 0;

	for (i = 0; i < dss_xstream_cnt(); i++) {
		dx = dss_get_xstream(i);
		if (main && !dx->dx_main_xs)
			continue;

		rc = sched_create_thread(dx, func, arg, ABT_THREAD_ATTR_NULL,
					 NULL, 0);
		if (rc != 0)
			break;
	}

	return rc;
}
