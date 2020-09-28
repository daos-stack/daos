/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
	int				rc;

	/** Update just the rc value */
	a_args->st_rc = f_arg->dfa_func(f_arg->dfa_arg);

	rc = ABT_future_set(f_arg->dfa_future, (void *)a_args);
	if (rc != ABT_SUCCESS)
		D_ERROR("future set failure %d\n", rc);
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

static inline int
sched_ult2pool(int ult_type)
{
	switch (ult_type) {
	case DSS_ULT_DTX_RESYNC:
	case DSS_ULT_IOFW:
	case DSS_ULT_EC:
	case DSS_ULT_CHECKSUM:
	case DSS_ULT_COMPRESS:
	case DSS_ULT_POOL_SRV:
	case DSS_ULT_DRPC_LISTENER:
	case DSS_ULT_RDB:
	case DSS_ULT_DRPC_HANDLER:
	case DSS_ULT_MISC:
	case DSS_ULT_IO:
		return DSS_POOL_IO;
	case DSS_ULT_REBUILD:
		return DSS_POOL_REBUILD;
	case DSS_ULT_GC:
		return DSS_POOL_GC;
	default:
		D_ASSERTF(0, "Invalid ULT type %d.\n", ult_type);
		return -DER_INVAL;
	}
}

static int
dss_collective_reduce_internal(struct dss_coll_ops *ops,
			       struct dss_coll_args *args, bool create_ult,
			       int flag, int ult_type)
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

	rc = ABT_future_set(future, (void *)&aggregator);
	D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);
	for (tid = 0; tid < xs_nr; tid++) {
		ABT_pool pool;
		stream			= &stream_args->csa_streams[tid];
		stream->st_coll_args	= &carg;

		if (args->ca_exclude_tgts_cnt) {
			int i;

			for (i = 0; i < args->ca_exclude_tgts_cnt; i++)
				if (args->ca_exclude_tgts[i] == tid)
					break;

			if (i < args->ca_exclude_tgts_cnt) {
				D_DEBUG(DB_TRACE, "Skip tgt %d\n", tid);
				rc = ABT_future_set(future, (void *)stream);
				D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);
				continue;
			}
		}

		dx = dss_get_xstream(DSS_MAIN_XS_ID(tid));
		pool = dx->dx_pools[sched_ult2pool(ult_type)];
		if (create_ult)
			rc = ABT_thread_create(pool, collective_func, stream,
					       ABT_THREAD_ATTR_NULL, NULL);
		else
			rc = ABT_task_create(pool, collective_func, stream,
					     NULL);

		if (rc != ABT_SUCCESS) {
			stream->st_rc = dss_abterr2der(rc);
			rc = ABT_future_set(future, (void *)stream);
			D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);
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
 * \param[in] flag		collective flag, reserved for future usage.
 * \param[in] ult_type		type of the collective task/ult
 *
 * \return			number of failed xstreams or error code
 */
int
dss_task_collective_reduce(struct dss_coll_ops *ops,
			   struct dss_coll_args *args, int flag, int ult_type)
{
	return dss_collective_reduce_internal(ops, args, false, flag, ult_type);
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
 * \param[in] flag		collective flag, reserved for future usage.
 *
 * \return			number of failed xstreams or error code
 */
int
dss_thread_collective_reduce(struct dss_coll_ops *ops,
			     struct dss_coll_args *args, int flag, int ult_type)
{
	return dss_collective_reduce_internal(ops, args, true, flag, ult_type);
}

static int
dss_collective_internal(int (*func)(void *), void *arg, bool thread, int flag,
			int ult_type)
{
	int				rc;
	struct dss_coll_ops		coll_ops = { 0 };
	struct dss_coll_args		coll_args = { 0 };

	coll_ops.co_func	= func;
	coll_args.ca_func_args	= arg;

	if (thread)
		rc = dss_thread_collective_reduce(&coll_ops, &coll_args, flag,
						  ult_type);
	else
		rc = dss_task_collective_reduce(&coll_ops, &coll_args, flag,
						ult_type);

	return rc;
}

/**
 * Execute \a func(\a arg) collectively on all server xstreams. Can only be
 * called by ULTs. Can only execute tasklet-compatible functions.
 *
 * \param[in] func	function to be executed
 * \param[in] arg	argument to be passed to \a func
 * \param[in] flag	collective flag, reserved for future usage.
 * \param[in] ult_type  the type for collective task.
 *
 * \return		number of failed xstreams or error code
 */
int
dss_task_collective(int (*func)(void *), void *arg, int flag, int ult_type)
{
	return dss_collective_internal(func, arg, false, flag, ult_type);
}

/**
 * Execute \a func(\a arg) collectively on all server xstreams. Can only be
 * called by ULTs. Can only execute tasklet-compatible functions.
 *
 * \param[in] func	function to be executed
 * \param[in] arg	argument to be passed to \a func
 * \param[in] flag	collective flag, reserved for future usage.
 * \param[in] ult_type  the type for collective ult.
 *
 * \return		number of failed xstreams or error code
 */

int
dss_thread_collective(int (*func)(void *), void *arg, int flag, int ult_type)
{
	return dss_collective_internal(func, arg, true, flag, ult_type);
}

/* ============== ULT create functions =================================== */

static inline int
sched_ult2xs(int ult_type, int tgt_id)
{
	if (tgt_id == DSS_TGT_SELF || ult_type == DSS_ULT_DTX_RESYNC)
		return DSS_XS_SELF;

	D_ASSERT(tgt_id >= 0 && tgt_id < dss_tgt_nr);
	switch (ult_type) {
	case DSS_ULT_IOFW:
	case DSS_ULT_MISC:
		if (!dss_helper_pool)
			return (DSS_MAIN_XS_ID(tgt_id) + 1) % DSS_XS_NR_TOTAL;

		if (dss_tgt_offload_xs_nr >= dss_tgt_nr)
			return (dss_sys_xs_nr + dss_tgt_nr + tgt_id);
		if (dss_tgt_offload_xs_nr > 0)
			return (dss_sys_xs_nr + dss_tgt_nr +
				tgt_id % dss_tgt_offload_xs_nr);
		else
			return ((DSS_MAIN_XS_ID(tgt_id) + 1) % dss_tgt_nr +
				dss_sys_xs_nr);
	case DSS_ULT_EC:
	case DSS_ULT_CHECKSUM:
	case DSS_ULT_COMPRESS:
	case DSS_ULT_IO:
		if (!dss_helper_pool)
			return DSS_MAIN_XS_ID(tgt_id) +
			       dss_tgt_offload_xs_nr / dss_tgt_nr;

		if (dss_tgt_offload_xs_nr > dss_tgt_nr)
			return (dss_sys_xs_nr + 2 * dss_tgt_nr +
				(tgt_id % (dss_tgt_offload_xs_nr -
					   dss_tgt_nr)));
		if (dss_tgt_offload_xs_nr > 0)
			return (dss_sys_xs_nr + dss_tgt_nr +
				tgt_id % dss_tgt_offload_xs_nr);
		else
			return (DSS_MAIN_XS_ID(tgt_id) + 1) % dss_tgt_nr +
			       dss_sys_xs_nr;
	case DSS_ULT_POOL_SRV:
	case DSS_ULT_RDB:
	case DSS_ULT_DRPC_HANDLER:
		return 0;
	case DSS_ULT_DRPC_LISTENER:
		return 1;
	case DSS_ULT_REBUILD:
	case DSS_ULT_GC:
		return DSS_MAIN_XS_ID(tgt_id);
	default:
		D_ASSERTF(0, "Invalid ULT type %d.\n", ult_type);
		return -DER_INVAL;
	}
}

/**
 * Create a ULT to execute \a func(\a arg). If \a ult is not NULL, the caller
 * is responsible for freeing the ULT handle with ABT_thread_free().
 *
 * \param[in]	func		function to execute
 * \param[in]	arg		argument for \a func
 * \param[in]	ult_type	ULT type
 * \param[in]	tgt_idx		VOS target index
 * \param[in]	stack_size	stacksize of the ULT, if it is 0, then create
 *				default size of ULT.
 * \param[out]	ult		ULT handle if not NULL
 */
int
dss_ult_create(void (*func)(void *), void *arg, int ult_type, int tgt_idx,
	       size_t stack_size, ABT_thread *ult)
{
	ABT_thread_attr		 attr;
	struct dss_xstream	*dx;
	int			 rc, rc1;

	dx = dss_get_xstream(sched_ult2xs(ult_type, tgt_idx));
	if (dx == NULL)
		return -DER_NONEXIST;

	if (stack_size > 0) {
		rc = ABT_thread_attr_create(&attr);
		if (rc != ABT_SUCCESS)
			return dss_abterr2der(rc);

		rc = ABT_thread_attr_set_stacksize(attr, stack_size);
		if (rc != ABT_SUCCESS)
			D_GOTO(free, rc = dss_abterr2der(rc));

		D_DEBUG(DB_TRACE, "Create ult stacksize is %zd\n", stack_size);
	} else {
		attr = ABT_THREAD_ATTR_NULL;
	}

	rc = ABT_thread_create(dx->dx_pools[sched_ult2pool(ult_type)], func,
			       arg, attr, ult);

free:
	if (attr != ABT_THREAD_ATTR_NULL) {
		rc1 = ABT_thread_attr_free(&attr);
		if (rc1 != ABT_SUCCESS)
			/* The child ULT has already been created,
			 * we should not return the error for the
			 * ABT_thread_attr_free() failure; otherwise,
			 * the caller will free the parameters ("arg")
			 * that is being used by the child ULT.
			 *
			 * So let's ignore the failure, the worse case
			 * is that we may leak some DRAM.
			 */
			D_ERROR("ABT_thread_attr_free failed: %d\n",
				dss_abterr2der(rc1));
	}

	return dss_abterr2der(rc);
}

static void
ult_execute_cb(void *data)
{
	struct dss_future_arg	*arg = data;
	int			rc;

	rc = arg->dfa_func(arg->dfa_arg);
	arg->dfa_status = rc;

	if (!arg->dfa_async)
		ABT_future_set(arg->dfa_future, (void *)(intptr_t)rc);
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
 * \param[in]	ult_type	type of ULT
 * \param[in]	tgt_id		target index
 * \param[out]			error code.
 */
int
dss_ult_execute(int (*func)(void *), void *arg, void (*user_cb)(void *),
		void *cb_args, int ult_type, int tgt_id, size_t stack_size)
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

	rc = dss_ult_create(ult_execute_cb, &future_arg, ult_type, tgt_id,
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
 * \param[in] ult_type	ULT type
 * \param[in] main	only create ULT on main XS or not.
 *
 * \return		Success or negative error code
 *			0
 *			-DER_NOMEM
 *			-DER_INVAL
 */
int
dss_ult_create_all(void (*func)(void *), void *arg, int ult_type, bool main)
{
	struct dss_xstream      *dx;
	int			 i, rc = 0;

	for (i = 0; i < dss_xstream_cnt(); i++) {
		dx = dss_get_xstream(i);
		if (main && !dx->dx_main_xs)
			continue;

		rc = ABT_thread_create(dx->dx_pools[sched_ult2pool(ult_type)],
				       func, arg, ABT_THREAD_ATTR_NULL, NULL);
		if (rc != ABT_SUCCESS) {
			rc = dss_abterr2der(rc);
			break;
		}
	}

	return rc;
}
