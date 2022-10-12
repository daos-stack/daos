/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2015-2022, Intel Corporation */

/*
 * tx.c -- transactions implementation
 */

#include <inttypes.h>
#include <wchar.h>
#include <errno.h>

#include "queue.h"
#include "ravl.h"
#include "obj.h"
#include "out.h"
#include "tx.h"
#include "valgrind_internal.h"
#include "memops.h"
#include "dav_internal.h"

struct tx_data {
	PMDK_SLIST_ENTRY(tx_data) tx_entry;
	jmp_buf env;
	enum dav_tx_failure_behavior failure_behavior;
};

struct tx {
	dav_obj_t *pop;
	enum dav_tx_stage stage;
	int last_errnum;

	PMDK_SLIST_HEAD(txd, tx_data) tx_entries;

	struct ravl *ranges;

	VEC(, struct dav_action) actions;

	dav_tx_callback stage_callback;
	void *stage_callback_arg;

	int first_snapshot;

};

/*
 * get_tx -- (internal) returns current transaction
 *
 * This function should be used only in high-level functions.
 */
static struct tx *
get_tx()
{
	static __thread struct tx tx;

	return &tx;
}

struct tx_alloc_args {
	uint64_t flags;
	const void *copy_ptr;
	size_t copy_size;
};

#define ALLOC_ARGS(flags)\
(struct tx_alloc_args){flags, NULL, 0}

struct tx_range_def {
	uint64_t offset;
	uint64_t size;
	uint64_t flags;
};

/*
 * tx_range_def_cmp -- compares two snapshot ranges
 */
static int
tx_range_def_cmp(const void *lhs, const void *rhs)
{
	const struct tx_range_def *l = lhs;
	const struct tx_range_def *r = rhs;

	if (l->offset > r->offset)
		return 1;
	else if (l->offset < r->offset)
		return -1;

	return 0;
}

static void
obj_tx_abort(int errnum, int user);

/*
 * obj_tx_fail_err -- (internal) dav_tx_abort variant that returns
 * error code
 */
static inline int
obj_tx_fail_err(int errnum, uint64_t flags)
{
	if ((flags & DAV_FLAG_TX_NO_ABORT) == 0)
		obj_tx_abort(errnum, 0);
	errno = errnum;
	return errnum;
}

/*
 * obj_tx_fail_null -- (internal) dav_tx_abort variant that returns
 * null PMEMoid
 */
static inline uint64_t
obj_tx_fail_null(int errnum, uint64_t flags)
{
	if ((flags & DAV_FLAG_TX_NO_ABORT) == 0)
		obj_tx_abort(errnum, 0);
	errno = errnum;
	return 0;
}

/* ASSERT_IN_TX -- checks whether there's open transaction */
#define ASSERT_IN_TX(tx) do {\
	if ((tx)->stage == DAV_TX_STAGE_NONE)\
		FATAL("%s called outside of transaction", __func__);\
} while (0)

/* ASSERT_TX_STAGE_WORK -- checks whether current transaction stage is WORK */
#define ASSERT_TX_STAGE_WORK(tx) do {\
	if ((tx)->stage != DAV_TX_STAGE_WORK)\
		FATAL("%s called in invalid stage %d", __func__, (tx)->stage);\
} while (0)

/*
 * tx_action_reserve -- (internal) reserve space for the given number of actions
 */
static int
tx_action_reserve(struct tx *tx, size_t n)
{
	size_t entries_size = (VEC_SIZE(&tx->actions) + n) *
		sizeof(struct ulog_entry_val);

	if (operation_reserve(tx->pop->external, entries_size) != 0)
		return -1;

	return 0;
}

/*
 * tx_action_add -- (internal) reserve space and add a new tx action
 */
static struct dav_action *
tx_action_add(struct tx *tx)
{
	if (tx_action_reserve(tx, 1) != 0)
		return NULL;

	VEC_INC_BACK(&tx->actions);

	return &VEC_BACK(&tx->actions);
}

/*
 * tx_action_remove -- (internal) remove last tx action
 */
static void
tx_action_remove(struct tx *tx)
{
	VEC_POP_BACK(&tx->actions);
}

/*
 * constructor_tx_alloc -- (internal) constructor for normal alloc
 */
static int
constructor_tx_alloc(void *ctx, void *ptr, size_t usable_size, void *arg)
{
	/* suppress unused-parameter errors */
	SUPPRESS_UNUSED(ctx);

	ASSERTne(ptr, NULL);
	ASSERTne(arg, NULL);

	struct tx_alloc_args *args = arg;

	/* do not report changes to the new object */
	VALGRIND_ADD_TO_TX(ptr, usable_size);

	if (args->flags & DAV_FLAG_ZERO)
		memset(ptr, 0, usable_size);

	if (args->copy_ptr && args->copy_size != 0)
		memcpy(ptr, args->copy_ptr, args->copy_size);

	return 0;
}

struct tx_range_data {
	void *begin;
	void *end;

	PMDK_SLIST_ENTRY(tx_range_data) tx_range;
};

PMDK_SLIST_HEAD(txr, tx_range_data);

/*
 * tx_restore_range -- (internal) restore a single range from undo log
 */
static void
tx_restore_range(dav_obj_t *pop, struct tx *tx, struct ulog_entry_buf *range)
{
	struct txr tx_ranges;
	struct tx_range_data *txr;

	PMDK_SLIST_INIT(&tx_ranges);

	ASSERT(tx->pop == pop);

	D_ALLOC_PTR_NZ(txr);
	if (txr == NULL) {
		/* we can't do it any other way */
		FATAL("!Malloc");
	}

	uint64_t range_offset = ulog_entry_offset(&range->base);

	txr->begin = OBJ_OFF_TO_PTR(pop, range_offset);
	txr->end = (char *)txr->begin + range->size;
	PMDK_SLIST_INSERT_HEAD(&tx_ranges, txr, tx_range);


	ASSERT(!PMDK_SLIST_EMPTY(&tx_ranges));

	void *dst_ptr = OBJ_OFF_TO_PTR(pop, range_offset);

	while (!PMDK_SLIST_EMPTY(&tx_ranges)) {
		txr = PMDK_SLIST_FIRST(&tx_ranges);
		PMDK_SLIST_REMOVE_HEAD(&tx_ranges, tx_range);
		/* restore partial range data from snapshot */
		ASSERT((char *)txr->begin >= (char *)dst_ptr);
		uint8_t *src = &range->data[
				(char *)txr->begin - (char *)dst_ptr];

		ASSERT((char *)txr->end >= (char *)txr->begin);
		size_t size = (size_t)((char *)txr->end - (char *)txr->begin);

		pmemops_memcpy(&pop->p_ops, txr->begin, src, size, 0);
		D_FREE(txr);
	}
}

/*
 * tx_undo_entry_apply -- applies modifications of a single ulog entry
 */
static int
tx_undo_entry_apply(struct ulog_entry_base *e, void *arg,
	const struct pmem_ops *p_ops)
{
	/* suppress unused-parameter errors */
	SUPPRESS_UNUSED(arg);

	struct ulog_entry_buf *eb;

	switch (ulog_entry_type(e)) {
	case ULOG_OPERATION_BUF_CPY:
		eb = (struct ulog_entry_buf *)e;

		tx_restore_range(p_ops->base, get_tx(), eb);
		break;
	case ULOG_OPERATION_AND:
	case ULOG_OPERATION_OR:
	case ULOG_OPERATION_SET:
	case ULOG_OPERATION_BUF_SET:
	default:
		ASSERT(0);
	}

	return 0;
}

/*
 * tx_abort_set -- (internal) abort all set operations
 */
static void
tx_abort_set(dav_obj_t *pop)
{
	ulog_foreach_entry((struct ulog *)&pop->clogs.undo,
		tx_undo_entry_apply, NULL, &pop->p_ops);
	pmemops_drain(&pop->p_ops);
	operation_finish(pop->undo, ULOG_INC_FIRST_GEN_NUM);
}

/*
 * tx_flush_range -- (internal) flush one range
 */
static void
tx_flush_range(void *data, void *ctx)
{
	dav_obj_t *pop = ctx;
	struct tx_range_def *range = data;

	if (!(range->flags & DAV_FLAG_NO_FLUSH)) {
		pmemops_xflush(&pop->p_ops, OBJ_OFF_TO_PTR(pop, range->offset),
				range->size, 0);
	}
	VALGRIND_REMOVE_FROM_TX(OBJ_OFF_TO_PTR(pop, range->offset),
		range->size);
}

/*
 * tx_clean_range -- (internal) clean one range
 */
static void
tx_clean_range(void *data, void *ctx)
{
	dav_obj_t *pop = ctx;
	struct tx_range_def *range = data;

	VALGRIND_REMOVE_FROM_TX(OBJ_OFF_TO_PTR(pop, range->offset),
		range->size);
	VALGRIND_SET_CLEAN(OBJ_OFF_TO_PTR(pop, range->offset), range->size);
}

/*
 * tx_pre_commit -- (internal) do pre-commit operations
 */
static void
tx_pre_commit(struct tx *tx)
{
	/* Flush all regions and destroy the whole tree. */
	ravl_delete_cb(tx->ranges, tx_flush_range, tx->pop);
	tx->ranges = NULL;
}

/*
 * tx_abort -- (internal) abort all allocated objects
 */
static void
tx_abort(dav_obj_t *pop)
{
	struct tx *tx = get_tx();

	tx_abort_set(pop);

	ravl_delete_cb(tx->ranges, tx_clean_range, pop);
	palloc_cancel(pop->do_heap,
		VEC_ARR(&tx->actions), VEC_SIZE(&tx->actions));
	tx->ranges = NULL;
}

/*
 * tx_get_pop -- returns the current transaction's pool handle, NULL if not
 * within a transaction.
 */
dav_obj_t *
tx_get_pop(void)
{
	return get_tx()->pop;
}

/*
 * tx_ranges_insert_def -- (internal) allocates and inserts a new range
 *	definition into the ranges tree
 */
static int
tx_ranges_insert_def(dav_obj_t *pop, struct tx *tx,
	const struct tx_range_def *rdef)
{
	/* suppress unused-parameter errors */
	SUPPRESS_UNUSED(pop);

	DAV_DEBUG("rdef->offset %"PRIu64" rdef->size %"PRIu64,
		rdef->offset, rdef->size);

	int ret = ravl_emplace_copy(tx->ranges, rdef);

	if (ret && errno == EEXIST)
		FATAL("invalid state of ranges tree");
	return ret;
}

/*
 * tx_alloc_common -- (internal) common function for alloc and zalloc
 */
static uint64_t
tx_alloc_common(struct tx *tx, size_t size, type_num_t type_num,
		palloc_constr constructor, struct tx_alloc_args args)
{
	uint64_t ret;

	if (size > DAV_MAX_ALLOC_SIZE) {
		ERR("requested size too large");
		return obj_tx_fail_null(ENOMEM, args.flags);
	}

	dav_obj_t *pop = tx->pop;

	struct dav_action *action = tx_action_add(tx);

	if (action == NULL)
		return obj_tx_fail_null(ENOMEM, args.flags);

	if (palloc_reserve(pop->do_heap, size, constructor, &args, type_num, 0,
		CLASS_ID_FROM_FLAG(args.flags),
		ARENA_ID_FROM_FLAG(args.flags), action) != 0)
		goto err_oom;

	ret = action->heap.offset;
	size = action->heap.usable_size;

	const struct tx_range_def r = {ret, size, args.flags};

	if (tx_ranges_insert_def(pop, tx, &r) != 0)
		goto err_oom;

	return ret;

err_oom:
	tx_action_remove(tx);
	D_CRIT("out of memory\n");
	return obj_tx_fail_null(ENOMEM, args.flags);
}

/*
 * dav_tx_begin -- initializes new transaction
 */
int
dav_tx_begin(dav_obj_t *pop, jmp_buf env, ...)
{
	DAV_DEBUG("");

	int err = 0;
	struct tx *tx = get_tx();

	enum dav_tx_failure_behavior failure_behavior = DAV_TX_FAILURE_ABORT;

	if (tx->stage == DAV_TX_STAGE_WORK) {
		if (tx->pop != pop) {
			ERR("nested transaction for different pool");
			return obj_tx_fail_err(EINVAL, 0);
		}

		/* inherits this value from the parent transaction */
		struct tx_data *txd = PMDK_SLIST_FIRST(&tx->tx_entries);

		failure_behavior = txd->failure_behavior;

		VALGRIND_START_TX;
	} else if (tx->stage == DAV_TX_STAGE_NONE) {
		VALGRIND_START_TX;

		dav_hold_clogs(pop);
		operation_start(pop->undo);

		VEC_INIT(&tx->actions);
		PMDK_SLIST_INIT(&tx->tx_entries);

		tx->ranges = ravl_new_sized(tx_range_def_cmp,
			sizeof(struct tx_range_def));

		tx->pop = pop;

		tx->first_snapshot = 1;

	} else {
		FATAL("Invalid stage %d to begin new transaction", tx->stage);
	}

	struct tx_data *txd;

	D_ALLOC_PTR_NZ(txd);
	if (txd == NULL) {
		err = errno;
		D_CRIT("Malloc!\n");
		goto err_abort;
	}

	tx->last_errnum = 0;
	ASSERT(env == NULL);
	if (env != NULL)
		memcpy(txd->env, env, sizeof(jmp_buf));
	else
		memset(txd->env, 0, sizeof(jmp_buf));

	txd->failure_behavior = failure_behavior;

	PMDK_SLIST_INSERT_HEAD(&tx->tx_entries, txd, tx_entry);

	tx->stage = DAV_TX_STAGE_WORK;

	/* handle locks */
	va_list argp;

	va_start(argp, env);

	enum dav_tx_param param_type;

	while ((param_type = va_arg(argp, enum dav_tx_param)) !=
			DAV_TX_PARAM_NONE) {
		if (param_type == DAV_TX_PARAM_CB) {
			dav_tx_callback cb =
					va_arg(argp, dav_tx_callback);
			void *arg = va_arg(argp, void *);

			if (tx->stage_callback &&
					(tx->stage_callback != cb ||
					tx->stage_callback_arg != arg)) {
				FATAL(
			 "transaction callback is already set, old %p new %p old_arg %p new_arg %p",
					tx->stage_callback, cb,
					tx->stage_callback_arg, arg);
			}

			tx->stage_callback = cb;
			tx->stage_callback_arg = arg;
		} else {
			ASSERT(param_type == DAV_TX_PARAM_CB);
		}
	}
	va_end(argp);

	ASSERT(err == 0);
	return 0;

err_abort:
	if (tx->stage == DAV_TX_STAGE_WORK)
		obj_tx_abort(err, 0);
	else
		tx->stage = DAV_TX_STAGE_ONABORT;
	return err;
}

/*
 * tx_abort_on_failure_flag -- (internal) return 0 or DAV_FLAG_TX_NO_ABORT
 * based on transaction setting
 */
static uint64_t
tx_abort_on_failure_flag(struct tx *tx)
{
	struct tx_data *txd = PMDK_SLIST_FIRST(&tx->tx_entries);

	if (txd->failure_behavior == DAV_TX_FAILURE_RETURN)
		return DAV_FLAG_TX_NO_ABORT;
	return 0;
}

/*
 * obj_tx_callback -- (internal) executes callback associated with current stage
 */
static void
obj_tx_callback(struct tx *tx)
{
	if (!tx->stage_callback)
		return;

	struct tx_data *txd = PMDK_SLIST_FIRST(&tx->tx_entries);

	/* is this the outermost transaction? */
	if (PMDK_SLIST_NEXT(txd, tx_entry) == NULL)
		tx->stage_callback(tx->pop, tx->stage, tx->stage_callback_arg);
}

/*
 * dav_tx_stage -- returns current transaction stage
 */
enum dav_tx_stage
dav_tx_stage(void)
{
	return get_tx()->stage;
}

/*
 * obj_tx_abort -- aborts current transaction
 */
static void
obj_tx_abort(int errnum, int user)
{
	struct tx *tx = get_tx();

	ASSERT_IN_TX(tx);
	ASSERT_TX_STAGE_WORK(tx);

	ASSERT(tx->pop != NULL);

	if (errnum == 0)
		errnum = ECANCELED;

	tx->stage = DAV_TX_STAGE_ONABORT;
	struct tx_data *txd = PMDK_SLIST_FIRST(&tx->tx_entries);

	if (PMDK_SLIST_NEXT(txd, tx_entry) == NULL) {
		/* this is the outermost transaction */

		/* process the undo log */
		tx_abort(tx->pop);

		dav_release_clogs(tx->pop);
	}

	tx->last_errnum = errnum;
	errno = errnum;
	if (user)
		DAV_DEBUG("!explicit transaction abort");

	/* ONABORT */
	obj_tx_callback(tx);

	if (!util_is_zeroed(txd->env, sizeof(jmp_buf)))
		longjmp(txd->env, errnum);
}

/*
 * dav_tx_abort -- aborts current transaction
 *
 * Note: this function should not be called from inside of dav.
 */
void
dav_tx_abort(int errnum)
{
	PMEMOBJ_API_START();
	obj_tx_abort(errnum, 1);
	PMEMOBJ_API_END();
}

/*
 * dav_tx_errno -- returns last transaction error code
 */
int
dav_tx_errno(void)
{
	DAV_DEBUG("err:%d", get_tx()->last_errnum);

	return get_tx()->last_errnum;
}

static void
tx_post_commit(struct tx *tx)
{
	operation_finish(tx->pop->undo, 0);
}

/*
 * dav_tx_commit -- commits current transaction
 */
void
dav_tx_commit(void)
{
	PMEMOBJ_API_START();
	struct tx *tx = get_tx();

	ASSERT_IN_TX(tx);
	ASSERT_TX_STAGE_WORK(tx);

	/* WORK */
	obj_tx_callback(tx);

	ASSERT(tx->pop != NULL);

	struct tx_data *txd = PMDK_SLIST_FIRST(&tx->tx_entries);

	if (PMDK_SLIST_NEXT(txd, tx_entry) == NULL) {
		/* this is the outermost transaction */

		dav_obj_t *pop = tx->pop;

		/* pre-commit phase */
		tx_pre_commit(tx);

		pmemops_drain(&pop->p_ops);

		operation_start(tx->pop->external);

		palloc_publish(pop->do_heap, VEC_ARR(&tx->actions),
			VEC_SIZE(&tx->actions), tx->pop->external);

		tx_post_commit(tx);

		dav_release_clogs(pop);
	}

	tx->stage = DAV_TX_STAGE_ONCOMMIT;

	/* ONCOMMIT */
	obj_tx_callback(tx);
	PMEMOBJ_API_END();
}

/*
 * dav_tx_end -- ends current transaction
 */
int
dav_tx_end(void)
{
	struct tx *tx = get_tx();

	if (tx->stage == DAV_TX_STAGE_WORK)
		FATAL("dav_tx_end called without dav_tx_commit");

	if (tx->pop == NULL)
		FATAL("dav_tx_end called without dav_tx_begin");

	if (tx->stage_callback &&
			(tx->stage == DAV_TX_STAGE_ONCOMMIT ||
			tx->stage == DAV_TX_STAGE_ONABORT)) {
		tx->stage = DAV_TX_STAGE_FINALLY;
		obj_tx_callback(tx);
	}

	struct tx_data *txd = PMDK_SLIST_FIRST(&tx->tx_entries);

	PMDK_SLIST_REMOVE_HEAD(&tx->tx_entries, tx_entry);

	D_FREE(txd);

	VALGRIND_END_TX;
	int ret = tx->last_errnum;

	if (PMDK_SLIST_EMPTY(&tx->tx_entries)) {
		tx->pop = NULL;
		tx->stage = DAV_TX_STAGE_NONE;
		VEC_DELETE(&tx->actions);

		if (tx->stage_callback) {
			dav_tx_callback cb = tx->stage_callback;
			void *arg = tx->stage_callback_arg;

			tx->stage_callback = NULL;
			tx->stage_callback_arg = NULL;

			cb(tx->pop, DAV_TX_STAGE_NONE, arg);
			/* tx should not be accessed after this callback */
		}
	} else {
		/* resume the next transaction */
		tx->stage = DAV_TX_STAGE_WORK;

		/* abort called within inner transaction, waterfall the error */
		if (tx->last_errnum)
			obj_tx_abort(tx->last_errnum, 0);
	}

	return ret;
}

#if	0	/* REVISIT */
/*
 * dav_tx_process -- processes current transaction stage
 */
void
dav_tx_process(void)
{
	DAV_DEBUG("");
	struct tx *tx = get_tx();

	ASSERT_IN_TX(tx);

	switch (tx->stage) {
	case DAV_TX_STAGE_NONE:
		break;
	case DAV_TX_STAGE_WORK:
		dav_tx_commit();
		break;
	case DAV_TX_STAGE_ONABORT:
	case DAV_TX_STAGE_ONCOMMIT:
		tx->stage = DAV_TX_STAGE_FINALLY;
		obj_tx_callback(tx);
		break;
	case DAV_TX_STAGE_FINALLY:
		tx->stage = DAV_TX_STAGE_NONE;
		break;
	default:
		ASSERT(0);
	}
}
#endif

/*
 * vg_verify_initialized -- when executed under Valgrind verifies that
 *   the buffer has been initialized; explicit check at snapshotting time,
 *   because Valgrind may find it much later when it's impossible to tell
 *   for which snapshot it triggered
 */
static void
vg_verify_initialized(dav_obj_t *pop, const struct tx_range_def *def)
{
	/* suppress unused-parameter errors */
	SUPPRESS_UNUSED(pop, def);
#if VG_MEMCHECK_ENABLED
	if (!On_memcheck)
		return;

	VALGRIND_DO_DISABLE_ERROR_REPORTING;
	char *start = (char *)pop + def->offset;
	char *uninit = (char *)VALGRIND_CHECK_MEM_IS_DEFINED(start, def->size);

	if (uninit) {
		VALGRIND_PRINTF(
			"Snapshotting uninitialized data in range <%p,%p> (<offset:0x%lx,size:0x%lx>)\n",
			start, start + def->size, def->offset, def->size);

		if (uninit != start)
			VALGRIND_PRINTF("Uninitialized data starts at: %p\n",
					uninit);

		VALGRIND_DO_ENABLE_ERROR_REPORTING;
		VALGRIND_CHECK_MEM_IS_DEFINED(start, def->size);
	} else {
		VALGRIND_DO_ENABLE_ERROR_REPORTING;
	}
#endif
}

/*
 * dav_tx_add_snapshot -- (internal) creates a variably sized snapshot
 */
static int
dav_tx_add_snapshot(struct tx *tx, struct tx_range_def *snapshot)
{
	/*
	 * Depending on the size of the block, either allocate an
	 * entire new object or use cache.
	 */
	void *ptr = OBJ_OFF_TO_PTR(tx->pop, snapshot->offset);

	VALGRIND_ADD_TO_TX(ptr, snapshot->size);

	/* do nothing */
	if (snapshot->flags & DAV_XADD_NO_SNAPSHOT)
		return 0;

	if (!(snapshot->flags & DAV_XADD_ASSUME_INITIALIZED))
		vg_verify_initialized(tx->pop, snapshot);

	/*
	 * If we are creating the first snapshot, setup a redo log action to
	 * increment counter in the undo log, so that the log becomes
	 * invalid once the redo log is processed.
	 */
	if (tx->first_snapshot) {
		struct dav_action *action = tx_action_add(tx);

		if (action == NULL)
			return -1;

		uint64_t *n = &tx->pop->clogs.undo.gen_num;

		palloc_set_value(tx->pop->do_heap, action,
			n, *n + 1);

		tx->first_snapshot = 0;
	}

	return operation_add_buffer(tx->pop->undo, ptr, ptr, snapshot->size,
		ULOG_OPERATION_BUF_CPY);
}

/*
 * dav_tx_merge_flags -- (internal) common code for merging flags between
 * two ranges to ensure resultant behavior is correct
 */
static void
dav_tx_merge_flags(struct tx_range_def *dest, struct tx_range_def *merged)
{
	/*
	 * DAV_XADD_NO_FLUSH should only be set in merged range if set in
	 * both ranges
	 */
	if ((dest->flags & DAV_XADD_NO_FLUSH) &&
				!(merged->flags & DAV_XADD_NO_FLUSH)) {
		dest->flags = dest->flags & (~DAV_XADD_NO_FLUSH);
	}
}

/*
 * dav_tx_add_common -- (internal) common code for adding persistent memory
 * into the transaction
 */
static int
dav_tx_add_common(struct tx *tx, struct tx_range_def *args)
{
	DAV_DEBUG("");

	if (args->size > DAV_MAX_ALLOC_SIZE) {
		ERR("snapshot size too large");
		return obj_tx_fail_err(EINVAL, args->flags);
	}

	if (!OBJ_OFFRANGE_FROM_HEAP(tx->pop, args->offset, (args->offset + args->size))) {
		ERR("object outside of heap");
		return obj_tx_fail_err(EINVAL, args->flags);
	}

	int ret = 0;

	/*
	 * Search existing ranges backwards starting from the end of the
	 * snapshot.
	 */
	struct tx_range_def r = *args;
	struct tx_range_def search = {0, 0, 0};
	/*
	 * If the range is directly adjacent to an existing one,
	 * they can be merged, so search for less or equal elements.
	 */
	enum ravl_predicate p = RAVL_PREDICATE_LESS_EQUAL;
	struct ravl_node *nprev = NULL;

	while (r.size != 0) {
		search.offset = r.offset + r.size;
		struct ravl_node *n = ravl_find(tx->ranges, &search, p);
		/*
		 * We have to skip searching for LESS_EQUAL because
		 * the snapshot we would find is the one that was just
		 * created.
		 */
		p = RAVL_PREDICATE_LESS;

		struct tx_range_def *f = n ? ravl_data(n) : NULL;

		size_t fend = f == NULL ? 0 : f->offset + f->size;
		size_t rend = r.offset + r.size;

		if (fend == 0 || fend < r.offset) {
			/*
			 * If found no range or the found range is not
			 * overlapping or adjacent on the left side, we can just
			 * create the entire r.offset + r.size snapshot.
			 *
			 * Snapshot:
			 *	--+-
			 * Existing ranges:
			 *	---- (no ranges)
			 * or	+--- (no overlap)
			 * or	---+ (adjacent on on right side)
			 */
			if (nprev != NULL) {
				/*
				 * But, if we have an existing adjacent snapshot
				 * on the right side, we can just extend it to
				 * include the desired range.
				 */
				struct tx_range_def *fprev = ravl_data(nprev);

				ASSERTeq(rend, fprev->offset);
				fprev->offset -= r.size;
				fprev->size += r.size;
			} else {
				/*
				 * If we don't have anything adjacent, create
				 * a new range in the tree.
				 */
				ret = tx_ranges_insert_def(tx->pop,
					tx, &r);
				if (ret != 0)
					break;
			}
			ret = dav_tx_add_snapshot(tx, &r);
			break;
		} else if (fend <= rend) {
			/*
			 * If found range has its end inside of the desired
			 * snapshot range, we can extend the found range by the
			 * size leftover on the left side.
			 *
			 * Snapshot:
			 *	--+++--
			 * Existing ranges:
			 *	+++---- (overlap on left)
			 * or	---+--- (found snapshot is inside)
			 * or	---+-++ (inside, and adjacent on the right)
			 * or	+++++-- (desired snapshot is inside)
			 *
			 */
			struct tx_range_def snapshot = *args;

			snapshot.offset = fend;
			/* the side not yet covered by an existing snapshot */
			snapshot.size = rend - fend;

			/* the number of bytes intersecting in both ranges */
			size_t intersection = fend - MAX(f->offset, r.offset);

			r.size -= intersection + snapshot.size;
			f->size += snapshot.size;
			dav_tx_merge_flags(f, args);

			if (snapshot.size != 0) {
				ret = dav_tx_add_snapshot(tx, &snapshot);
				if (ret != 0)
					break;
			}

			/*
			 * If there's a snapshot adjacent on right side, merge
			 * the two ranges together.
			 */
			if (nprev != NULL) {
				struct tx_range_def *fprev = ravl_data(nprev);

				ASSERTeq(rend, fprev->offset);
				f->size += fprev->size;
				dav_tx_merge_flags(f, fprev);
				ravl_remove(tx->ranges, nprev);
			}
		} else if (fend >= r.offset) {
			/*
			 * If found range has its end extending beyond the
			 * desired snapshot.
			 *
			 * Snapshot:
			 *	--+++--
			 * Existing ranges:
			 *	-----++ (adjacent on the right)
			 * or	----++- (overlapping on the right)
			 * or	----+++ (overlapping and adjacent on the right)
			 * or	--+++++ (desired snapshot is inside)
			 *
			 * Notice that we cannot create a snapshot based solely
			 * on this information without risking overwriting an
			 * existing one. We have to continue iterating, but we
			 * keep the information about adjacent snapshots in the
			 * nprev variable.
			 */
			size_t overlap = rend - MAX(f->offset, r.offset);

			r.size -= overlap;
			dav_tx_merge_flags(f, args);
		} else {
			ASSERT(0);
		}

		nprev = n;
	}

	if (ret != 0) {
		DAV_DEBUG("out of memory\n");
		return obj_tx_fail_err(ENOMEM, args->flags);
	}

	return 0;
}

/*
 * dav_tx_add_range_direct -- adds persistent memory range into the
 *					transaction
 */
int
dav_tx_add_range_direct(const void *ptr, size_t size)
{
	DAV_DEBUG("");

	PMEMOBJ_API_START();
	struct tx *tx = get_tx();

	ASSERT_IN_TX(tx);
	ASSERT_TX_STAGE_WORK(tx);

	int ret;

	uint64_t flags = tx_abort_on_failure_flag(tx);

	if (!OBJ_PTR_FROM_POOL(tx->pop, ptr)) {
		ERR("object outside of pool");
		ret = obj_tx_fail_err(EINVAL, flags);
		PMEMOBJ_API_END();
		return ret;
	}

	struct tx_range_def args = {
		.offset = OBJ_PTR_TO_OFF(tx->pop, ptr),
		.size = size,
		.flags = flags,
	};

	ret = dav_tx_add_common(tx, &args);

	PMEMOBJ_API_END();
	return ret;
}

/*
 * dav_tx_xadd_range_direct -- adds persistent memory range into the
 *					transaction
 */
int
dav_tx_xadd_range_direct(const void *ptr, size_t size, uint64_t flags)
{

	PMEMOBJ_API_START();
	struct tx *tx = get_tx();

	ASSERT_IN_TX(tx);
	ASSERT_TX_STAGE_WORK(tx);

	int ret;
	uint64_t off;

	flags |= tx_abort_on_failure_flag(tx);

	if (flags & ~DAV_XADD_VALID_FLAGS) {
		ERR("unknown flags 0x%" PRIx64, flags
			& ~DAV_XADD_VALID_FLAGS);
		ret = obj_tx_fail_err(EINVAL, flags);
		PMEMOBJ_API_END();
		return ret;
	}

	if (!OBJ_PTR_FROM_POOL(tx->pop, ptr)) {
		ERR("object outside of pool");
		ret = obj_tx_fail_err(EINVAL, flags);
		PMEMOBJ_API_END();
		return ret;
	}

	off = OBJ_PTR_TO_OFF(tx->pop, ptr);
	struct tx_range_def args = {
		.offset = off,
		.size = size,
		.flags = flags,
	};

	ret = dav_tx_add_common(tx, &args);

	PMEMOBJ_API_END();
	return ret;
}

/*
 * dav_tx_add_range -- adds persistent memory range into the transaction
 */
int
dav_tx_add_range(uint64_t hoff, size_t size)
{
	PMEMOBJ_API_START();
	struct tx *tx = get_tx();

	ASSERT_IN_TX(tx);
	ASSERT_TX_STAGE_WORK(tx);

	int ret;

	uint64_t flags = tx_abort_on_failure_flag(tx);

	ASSERT(OBJ_OFF_IS_VALID(tx->pop, hoff));

	struct tx_range_def args = {
		.offset = hoff,
		.size = size,
		.flags = flags,
	};

	ret = dav_tx_add_common(tx, &args);

	PMEMOBJ_API_END();
	return ret;
}

/*
 * dav_tx_xadd_range -- adds persistent memory range into the transaction
 */
int
dav_tx_xadd_range(uint64_t hoff, size_t size, uint64_t flags)
{
	PMEMOBJ_API_START();
	struct tx *tx = get_tx();

	ASSERT_IN_TX(tx);
	ASSERT_TX_STAGE_WORK(tx);

	int ret;

	flags |= tx_abort_on_failure_flag(tx);

	if (flags & ~DAV_XADD_VALID_FLAGS) {
		ERR("unknown flags 0x%" PRIx64, flags
			& ~DAV_XADD_VALID_FLAGS);
		ret = obj_tx_fail_err(EINVAL, flags);
		PMEMOBJ_API_END();
		return ret;
	}

	ASSERT(OBJ_OFF_IS_VALID(tx->pop, hoff));

	struct tx_range_def args = {
		.offset = hoff,
		.size = size,
		.flags = flags,
	};

	ret = dav_tx_add_common(tx, &args);

	PMEMOBJ_API_END();
	return ret;
}

/*
 * dav_tx_alloc -- allocates a new object
 */
uint64_t
dav_tx_alloc(size_t size, uint64_t type_num)
{
	uint64_t off;

	PMEMOBJ_API_START();
	struct tx *tx = get_tx();

	ASSERT_IN_TX(tx);
	ASSERT_TX_STAGE_WORK(tx);

	uint64_t flags = tx_abort_on_failure_flag(tx);

	if (size == 0) {
		ERR("allocation with size 0");
		off = obj_tx_fail_null(EINVAL, flags);
		PMEMOBJ_API_END();
		return off;
	}

	off = tx_alloc_common(tx, size, (type_num_t)type_num,
			constructor_tx_alloc, ALLOC_ARGS(flags));

	PMEMOBJ_API_END();
	return off;
}

/*
 * dav_tx_zalloc -- allocates a new zeroed object
 */
uint64_t
dav_tx_zalloc(size_t size, uint64_t type_num)
{
	uint64_t off;
	struct tx *tx = get_tx();

	ASSERT_IN_TX(tx);
	ASSERT_TX_STAGE_WORK(tx);

	uint64_t flags = DAV_FLAG_ZERO;

	flags |= tx_abort_on_failure_flag(tx);

	PMEMOBJ_API_START();
	if (size == 0) {
		ERR("allocation with size 0");
		off = obj_tx_fail_null(EINVAL, flags);
		PMEMOBJ_API_END();
		return off;
	}

	off = tx_alloc_common(tx, size, (type_num_t)type_num,
			constructor_tx_alloc, ALLOC_ARGS(flags));

	PMEMOBJ_API_END();
	return off;
}

/*
 * dav_tx_xalloc -- allocates a new object
 */
uint64_t
dav_tx_xalloc(size_t size, uint64_t type_num, uint64_t flags)
{
	uint64_t off;
	struct tx *tx = get_tx();

	ASSERT_IN_TX(tx);
	ASSERT_TX_STAGE_WORK(tx);

	flags |= tx_abort_on_failure_flag(tx);

	PMEMOBJ_API_START();

	if (size == 0) {
		ERR("allocation with size 0");
		off = obj_tx_fail_null(EINVAL, flags);
		PMEMOBJ_API_END();
		return off;
	}

	if (flags & ~DAV_TX_XALLOC_VALID_FLAGS) {
		ERR("unknown flags 0x%" PRIx64, flags
			& ~(DAV_TX_XALLOC_VALID_FLAGS));
		off = obj_tx_fail_null(EINVAL, flags);
		PMEMOBJ_API_END();
		return off;
	}

	off = tx_alloc_common(tx, size, (type_num_t)type_num,
			constructor_tx_alloc, ALLOC_ARGS(flags));

	PMEMOBJ_API_END();
	return off;
}

/*
 * dav_tx_xfree -- frees an existing object, with no_abort option
 */
static int
dav_tx_xfree(uint64_t off, uint64_t flags)
{
	struct tx *tx = get_tx();

	ASSERT_IN_TX(tx);
	ASSERT_TX_STAGE_WORK(tx);

	flags |= tx_abort_on_failure_flag(tx);

	if (flags & ~DAV_XFREE_VALID_FLAGS) {
		ERR("unknown flags 0x%" PRIx64,
				flags & ~DAV_XFREE_VALID_FLAGS);
		return obj_tx_fail_err(EINVAL, flags);
	}

	if (off == 0)
		return 0;

	dav_obj_t *pop = tx->pop;

	/* REVISIT */
#if 0
	if (pop->do_phdr->dp_uuid_lo != oid.pool_uuid_lo) {
		ERR("invalid pool uuid");
		return obj_tx_fail_err(EINVAL, flags);
	}
#endif

	ASSERT(OBJ_OFF_IS_VALID(pop, off));

	PMEMOBJ_API_START();

	struct dav_action *action;

	struct tx_range_def range = {off, 0, 0};
	struct ravl_node *n = ravl_find(tx->ranges, &range,
		RAVL_PREDICATE_EQUAL);

	/*
	 * If attempting to free an object allocated within the same
	 * transaction, simply cancel the alloc and remove it from the actions.
	 */
	if (n != NULL) {
		VEC_FOREACH_BY_PTR(action, &tx->actions) {
			if (action->type == DAV_ACTION_TYPE_HEAP &&
				action->heap.offset == off) {
				struct tx_range_def *r = ravl_data(n);
				void *ptr = OBJ_OFF_TO_PTR(pop, r->offset);

				VALGRIND_SET_CLEAN(ptr, r->size);
				VALGRIND_REMOVE_FROM_TX(ptr, r->size);
				ravl_remove(tx->ranges, n);
				palloc_cancel(pop->do_heap, action, 1);
				VEC_ERASE_BY_PTR(&tx->actions, action);
				PMEMOBJ_API_END();
				return 0;
			}
		}
	}

	action = tx_action_add(tx);
	if (action == NULL) {
		int ret = obj_tx_fail_err(errno, flags);

		PMEMOBJ_API_END();
		return ret;
	}

	palloc_defer_free(pop->do_heap, off, action);

	PMEMOBJ_API_END();
	return 0;
}

/*
 * dav_tx_free -- frees an existing object
 */
int
dav_tx_free(uint64_t off)
{
	return dav_tx_xfree(off, 0);
}

void*
dav_tx_off2ptr(uint64_t off)
{
	struct tx *tx = get_tx();

	ASSERT_IN_TX(tx);
	ASSERT_TX_STAGE_WORK(tx);

	ASSERT(OBJ_OFF_IS_VALID(tx->pop, off));
	return (void *)OBJ_OFF_TO_PTR(tx->pop, off);
}

/*
 * dav_reserve -- reserves a single object
 */
uint64_t
dav_reserve(dav_obj_t *pop, struct dav_action *act,
	size_t size, uint64_t type_num)
{
	DAV_DEBUG("pop %p act %p size %zu type_num %llx",
		  pop, act, size,
		  (unsigned long long)type_num);

	PMEMOBJ_API_START();

	if (palloc_reserve(pop->do_heap, size, NULL, NULL, type_num,
		0, 0, 0, act) != 0) {
		PMEMOBJ_API_END();
		return 0;
	}

	PMEMOBJ_API_END();
	return act->heap.offset;
}

/*
 * dav_defer_free -- creates a deferred free action
 */
void
dav_defer_free(dav_obj_t *pop, uint64_t off, struct dav_action *act)
{
	ASSERT(off != 0);
	ASSERT(OBJ_OFF_IS_VALID(pop, off));
	palloc_defer_free(pop->do_heap, off, act);
}

#if	0
/*
 * dav_publish -- publishes a collection of actions
 */
int
dav_publish(dav_obj_t *pop, struct dav_action *actv, size_t actvcnt)
{
	PMEMOBJ_API_START();
	struct operation_context *ctx = pmalloc_operation_hold(pop);

	size_t entries_size = actvcnt * sizeof(struct ulog_entry_val);

	if (operation_reserve(ctx, entries_size) != 0) {
		PMEMOBJ_API_END();
		return -1;
	}

	palloc_publish(&pop->do_heap, actv, actvcnt, ctx);

	pmalloc_operation_release(pop);

	PMEMOBJ_API_END();
	return 0;
}
#endif

/*
 * dav_cancel -- cancels collection of actions
 */
void
dav_cancel(dav_obj_t *pop, struct dav_action *actv, size_t actvcnt)
{
	PMEMOBJ_API_START();
	palloc_cancel(pop->do_heap, actv, actvcnt);
	PMEMOBJ_API_END();
}


/*
 * dav_tx_xpublish -- publishes actions inside of a transaction,
 * with no_abort option
 */
int
dav_tx_publish(struct dav_action *actv, size_t actvcnt)
{
	struct tx *tx = get_tx();
	uint64_t flags = 0;

	ASSERT_IN_TX(tx);
	ASSERT_TX_STAGE_WORK(tx);

	flags |= tx_abort_on_failure_flag(tx);

	PMEMOBJ_API_START();

	if (tx_action_reserve(tx, actvcnt) != 0) {
		int ret = obj_tx_fail_err(ENOMEM, flags);

		PMEMOBJ_API_END();
		return ret;
	}

	for (size_t i = 0; i < actvcnt; ++i)
		VEC_PUSH_BACK(&tx->actions, actv[i]);

	PMEMOBJ_API_END();
	return 0;
}

/* arguments for constructor_alloc */
struct constr_args {
	int zero_init;
	dav_constr constructor;
	void *arg;
};


/* arguments for constructor_alloc_root */
struct carg_root {
	size_t size;
	dav_constr constructor;
	void *arg;
};

/* arguments for constructor_realloc and constructor_zrealloc */
struct carg_realloc {
	void *ptr;
	size_t old_size;
	size_t new_size;
	int zero_init;
	type_num_t user_type;
	dav_constr constructor;
	void *arg;
};

/*
 * constructor_zrealloc_root -- (internal) constructor for dav_root
 */
static int
constructor_zrealloc_root(void *ctx, void *ptr, size_t usable_size, void *arg)
{
	dav_obj_t *pop = ctx;

	DAV_DEBUG("pop %p ptr %p arg %p", pop, ptr, arg);

	ASSERTne(ptr, NULL);
	ASSERTne(arg, NULL);

	VALGRIND_ADD_TO_TX(ptr, usable_size);

	struct carg_realloc *carg = arg;

	if (usable_size > carg->old_size) {
		size_t grow_len = usable_size - carg->old_size;
		void *new_data_ptr = (void *)((uintptr_t)ptr + carg->old_size);

		pmemops_memset(&pop->p_ops, new_data_ptr, 0, grow_len, 0);
	}
	int ret = 0;

	if (carg->constructor)
		ret = carg->constructor(pop, ptr, carg->arg);

	VALGRIND_REMOVE_FROM_TX(ptr, usable_size);

	return ret;
}

/*
 * obj_realloc_root -- (internal) reallocate root object
 */
static int
obj_alloc_root(dav_obj_t *pop, size_t size)
{
	DAV_DEBUG("pop %p size %zu", pop, size);

	struct carg_realloc carg;

	carg.ptr = OBJ_OFF_TO_PTR(pop, pop->do_phdr->dp_root_offset);
	carg.old_size = pop->do_phdr->dp_root_size;
	carg.new_size = size;
	carg.user_type = 0;
	carg.constructor = NULL;
	carg.zero_init = 1;
	carg.arg = NULL;

	struct operation_context *ctx = pop->external;

	operation_start(ctx);

	operation_add_entry(ctx, &pop->do_phdr->dp_root_size, size, ULOG_OPERATION_SET);

	int ret = palloc_operation(pop->do_heap, pop->do_phdr->dp_root_offset,
			&pop->do_phdr->dp_root_offset, size,
			constructor_zrealloc_root, &carg,
			0, 0, 0, 0, ctx); /* REVISIT: object_flags and type num ignored*/

	return ret;
}

/*
 * dav_root_construct -- returns root object
 */
uint64_t
dav_root(dav_obj_t *pop, size_t size)
{
	DAV_DEBUG("pop %p size %zu", pop, size);

	PMEMOBJ_API_START();
	if (size > DAV_MAX_ALLOC_SIZE) {
		ERR("requested size too large");
		errno = ENOMEM;
		PMEMOBJ_API_END();
		return 0;
	}

	if (size == 0 && pop->do_phdr->dp_root_offset == 0) {
		ERR("requested size cannot equals zero");
		errno = EINVAL;
		PMEMOBJ_API_END();
		return 0;
	}

	/* REVISIT START
	 * For thread safety the below block has to be protected by lock
	 */
	if (size > pop->do_phdr->dp_root_size &&
			obj_alloc_root(pop, size)) {
		ERR("dav_root failed");
		PMEMOBJ_API_END();
		return 0;
	}

	/* REVISIT END */

	PMEMOBJ_API_END();
	return pop->do_phdr->dp_root_offset;
}

/*
 * constructor_alloc -- (internal) constructor for obj_alloc_construct
 */
static int
constructor_alloc(void *ctx, void *ptr, size_t usable_size, void *arg)
{
	dav_obj_t *pop = ctx;

	struct pmem_ops *p_ops = &pop->p_ops;

	DAV_DEBUG("pop %p ptr %p arg %p", pop, ptr, arg);

	ASSERTne(ptr, NULL);
	ASSERTne(arg, NULL);

	struct constr_args *carg = arg;

	if (carg->zero_init)
		pmemops_memset(p_ops, ptr, 0, usable_size, 0);

	int ret = 0;

	if (carg->constructor)
		ret = carg->constructor(pop, ptr, carg->arg);

	return ret;
}

/*
 * obj_alloc_construct -- (internal) allocates a new object with constructor
 */
static int
obj_alloc_construct(dav_obj_t *pop, uint64_t *offp, size_t size,
	type_num_t type_num, uint64_t flags,
	dav_constr constructor, void *arg)
{
	if (size > DAV_MAX_ALLOC_SIZE) {
		ERR("requested size too large");
		errno = ENOMEM;
		return -1;
	}

	struct constr_args carg;

	carg.zero_init = flags & DAV_FLAG_ZERO;
	carg.constructor = constructor;
	carg.arg = arg;

	struct operation_context *ctx = pop->external;

	operation_start(ctx);

	int ret = palloc_operation(pop->do_heap, 0, offp, size,
			constructor_alloc, &carg, type_num, 0,
			CLASS_ID_FROM_FLAG(flags), ARENA_ID_FROM_FLAG(flags),
			ctx);

	return ret;
}

/*
 * dav_alloc -- allocates a new object
 */
int
dav_alloc(dav_obj_t *pop, uint64_t *offp, size_t size,
	uint64_t type_num, dav_constr constructor, void *arg)
{
	DAV_DEBUG("pop %p offp %p size %zu type_num %llx constructor %p arg %p",
		  pop, offp, size, (unsigned long long)type_num,
		  constructor, arg);

	if (size == 0) {
		ERR("allocation with size 0");
		errno = EINVAL;
		return -1;
	}

	if (offp == NULL) {
		ERR("allocation offp is NULL");
		errno = EINVAL;
		return -1;
	}

	PMEMOBJ_API_START();
	int ret = obj_alloc_construct(pop, offp, size, type_num,
			0, constructor, arg);

	PMEMOBJ_API_END();
	return ret;
}

/*
 * dav_free -- frees an existing object
 */
void
dav_free(dav_obj_t *pop, uint64_t off)
{
	struct operation_context *ctx = pop->external;

	DAV_DEBUG("oid.off 0x%016" PRIx64, off);

	if (off == 0)
		return;

	PMEMOBJ_API_START();

	ASSERTne(pop, NULL);
	ASSERT(OBJ_OFF_IS_VALID(pop, off));

	palloc_operation(pop->do_heap, off, &off, 0, NULL, NULL,
			0, 0, 0, 0, ctx);
	PMEMOBJ_API_END();
}

/*
 * dav_memcpy_persist -- dav version of memcpy
 */
void *
dav_memcpy_persist(dav_obj_t *pop, void *dest, const void *src,
	size_t len)
{
	DAV_DEBUG("pop %p dest %p src %p len %zu", pop, dest, src, len);
	PMEMOBJ_API_START();

	void *ptr = pmemops_memcpy(&pop->p_ops, dest, src, len, 0);

	PMEMOBJ_API_END();
	return ptr;
}
