/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of vos/tests/
 *
 * vos/tests/vts_ilog.c
 *
 * Author: Jeffrey Olivier <jeffrey.v.olivier@intel.com>
 */
#define D_LOGFAC	DD_FAC(tests)

#include <stdarg.h>
#include "vts_io.h"
#include <vos_internal.h>

#define LOG_FAIL(rc, expected_value, format, ...)			\
	do {								\
		if ((rc) == (expected_value))				\
			break;						\
		fail_msg("ERROR: rc="DF_RC" != %d: " format, DP_RC(rc),	\
			 expected_value, ##__VA_ARGS__);		\
	} while (0)

static bool verbose;

static struct ilog_df *
ilog_alloc_root(struct umem_instance *umm)
{
	int		 rc = 0;
	umem_off_t	 ilog_off = UMOFF_NULL;

	rc = umem_tx_begin(umm, NULL);
	if (rc != 0) {
		print_message("Tx begin failed\n");
		goto done;
	}

	ilog_off = umem_zalloc(umm, sizeof(struct ilog_df));
	if (ilog_off == UMOFF_NULL) {
		print_message("Allocation failed\n");
		rc = -DER_NOSPACE;
	}

	rc = umem_tx_end(umm, rc);
done:
	assert_rc_equal(rc, 0);

	return umem_off2ptr(umm, ilog_off);
}

static void
ilog_free_root(struct umem_instance *umm, struct ilog_df *ilog)
{
	int		 rc = 0;

	rc = umem_tx_begin(umm, NULL);
	if (rc != 0) {
		print_message("Tx begin failed\n");
		goto done;
	}

	rc = umem_free(umm, umem_ptr2off(umm, ilog));

	rc = umem_tx_end(umm, rc);
done:
	assert_rc_equal(rc, 0);
}

enum {
	COMMITTED = 0,
	COMMITTABLE,
	PREPARED,
};

static int		current_status;
static struct ilog_id	current_tx_id;

struct fake_tx_entry {
	umem_off_t	root_off;
	d_list_t	link;
	daos_epoch_t	epoch;
	int		status;
	uint32_t	tx_id;
};

static D_LIST_HEAD(fake_tx_list);

static int
fake_tx_status_get(struct umem_instance *umm, uint32_t tx_id,
		   daos_epoch_t epoch, uint32_t intent, void *args)
{
	struct lru_array	*array = args;
	struct fake_tx_entry	*entry;
	bool			 found;

	if (tx_id == 0)
		return ILOG_COMMITTED;

	assert_true(tx_id >= DTX_LID_RESERVED);

	found = lrua_lookupx(array, tx_id - DTX_LID_RESERVED, epoch, &entry);

	if (found == false)
		return ILOG_COMMITTED;

	assert_non_null(entry);

	switch (entry->status) {
	case COMMITTED:
	case COMMITTABLE:
		return ILOG_COMMITTED;
	case PREPARED:
		if (intent == DAOS_INTENT_PURGE)
			return ILOG_REMOVED;
		return ILOG_UNCOMMITTED;
	}
	D_ASSERT(0);
	return 0;
}

void
fake_tx_reset(void)
{
	/** Just set it so it doesn't match anything */
	current_tx_id.id_tx_id = 0xbeef;
	current_tx_id.id_epoch = 0;
}

static void
fake_tx_evict(void *payload, uint32_t idx, void *arg)
{
	struct fake_tx_entry	*entry = payload;

	d_list_del(&entry->link);
}

static int
fake_tx_is_same_tx(struct umem_instance *umm, uint32_t tx_id,
		   daos_epoch_t epoch, bool *same, void *args)
{
	if (tx_id == current_tx_id.id_tx_id)
		*same = true;
	else
		*same = false;

	return 0;
}

static int
fake_tx_log_add(struct umem_instance *umm, umem_off_t offset, uint32_t *tx_id,
		daos_epoch_t epoch, void *args)
{
	struct lru_array	*array = args;
	struct fake_tx_entry	*entry;
	uint32_t		 idx;
	int			 rc;

	rc = lrua_allocx(array, &idx, epoch, &entry);
	assert_rc_equal(rc, 0);
	assert_non_null(entry);

	entry->tx_id = idx;
	entry->root_off = offset;
	entry->status = current_status;
	d_list_add_tail(&entry->link, &fake_tx_list);

	entry->tx_id = current_tx_id.id_tx_id = *tx_id = idx + DTX_LID_RESERVED;
	entry->epoch = current_tx_id.id_epoch = epoch;

	return 0;
}

static int
fake_tx_log_del(struct umem_instance *umm, umem_off_t offset, uint32_t tx_id,
		daos_epoch_t epoch, bool deregister, void *args)
{
	struct lru_array	*array = args;
	struct fake_tx_entry	*entry;
	bool			 found;

	if (tx_id < DTX_LID_RESERVED)
		return 0;

	found = lrua_lookupx(array, tx_id - DTX_LID_RESERVED, epoch, &entry);
	assert_true(found);
	if (entry->root_off != offset) {
		print_message("Mismatched ilog root "DF_U64"!="
			      DF_U64"\n", entry->root_off,
			      offset);
		return -DER_INVAL;
	}
	lrua_evictx(array, tx_id - DTX_LID_RESERVED, epoch);
	return 0;
}

static void
commit_all(void)
{
	struct fake_tx_entry	*entry;

	d_list_for_each_entry(entry, &fake_tx_list, link) {
		/** Mark the entry as committed */
		entry->status = COMMITTED;
	}
}

static struct ilog_desc_cbs ilog_callbacks = {
	.dc_log_status_cb	= fake_tx_status_get,
	.dc_is_same_tx_cb	= fake_tx_is_same_tx,
	.dc_log_add_cb		= fake_tx_log_add,
	.dc_log_del_cb		= fake_tx_log_del,
};

struct desc {
	daos_epoch_t	epoch;
	bool		punch;
	int32_t		pad;
};

struct entries {
	struct lru_array	*array;
	struct desc		*entries;
	int			 entry_count;
	int			 alloc_count;
};

#define MAX_ILOG_LEN 2000
static int
entries_init(struct entries *entries)
{
	struct lru_callbacks	cbs = {
		.lru_on_evict = fake_tx_evict,
	};
	int			rc;

	D_ALLOC_ARRAY(entries->entries, MAX_ILOG_LEN);
	if (entries->entries == NULL)
		return -DER_NOMEM;

	entries->entry_count = 0;
	entries->alloc_count = MAX_ILOG_LEN;

	rc = lrua_array_alloc(&entries->array, DTX_ARRAY_LEN, 1,
			      sizeof(struct fake_tx_entry), 0, &cbs, NULL);
	if (rc != 0)
		D_FREE(entries->entries);

	ilog_callbacks.dc_log_status_args = entries->array;
	ilog_callbacks.dc_is_same_tx_args = entries->array;
	ilog_callbacks.dc_log_add_args = entries->array;
	ilog_callbacks.dc_log_del_args = entries->array;

	return rc;
}

static void
entries_fini(struct entries *entries)
{
	lrua_array_free(entries->array);
	D_FREE(entries->entries);
}

#define ENTRIES_END ((daos_epoch_t)0)

enum entries_op {
	ENTRY_NEW,
	ENTRY_APPEND,
	ENTRY_REPLACE,
};

static int
entries_set(struct entries *entries, enum entries_op op, ...)
{
	struct desc	*desc;
	uint32_t	 epoch;
	va_list		 ap;

	switch (op) {
	case ENTRY_NEW:
		entries->entry_count = 0;
		if (verbose)
			print_message("New entries\n");
		break;
	case ENTRY_APPEND:
		if (verbose)
			print_message("Append entries\n");
		break;
	case ENTRY_REPLACE:
		if (entries->entry_count == 0) {
			print_message("Can't replace non-existent entry\n");
			return -DER_MISC;
		}
		if (verbose)
			print_message("Replace entry\n");
		entries->entry_count--;
	}

	va_start(ap, op);
	for (;;) {
		epoch = va_arg(ap, uint32_t);
		if (epoch == ENTRIES_END)
			break;

		if (entries->entry_count == entries->alloc_count) {
			va_end(ap);
			return -DER_NOMEM;
		}

		desc = &entries->entries[entries->entry_count];
		desc->epoch = epoch;
		desc->punch = va_arg(ap, int);
		if (verbose)
			print_message("Append entry %d epoch=" DF_U64 " punch="
				      DF_BOOL "\n", entries->entry_count,
				      desc->epoch, DP_BOOL(desc->punch));

		entries->entry_count++;
	}
	va_end(ap);

	return 0;
}

static int
entries_check(struct umem_instance *umm, struct ilog_df *root,
	      const struct ilog_desc_cbs *cbs, const daos_epoch_range_t *epr,
	      int expected_rc, struct entries *entries)
{
	struct desc		*desc;
	struct ilog_entry	 entry;
	struct ilog_entries	 ilog_entries;
	int			 idx;
	int			 rc = 0;
	int			 wrong_epoch = 0;
	int			 wrong_punch = 0;

	ilog_fetch_init(&ilog_entries);

	rc = ilog_fetch(umm, root, cbs, 0, &ilog_entries);
	if (rc != expected_rc) {
		print_message("Unexpected fetch rc: %s\n", d_errstr(rc));
		if (rc == 0)
			rc = -DER_MISC;
		goto finish;
	}

	desc = entries->entries;
	idx = 0;
	if (verbose)
		print_message("Checking log\n");
	ilog_foreach_entry(&ilog_entries, &entry) {
		if (idx == entries->entry_count) {
			print_message("Too many entries in ilog\n");
			rc = -DER_MISC;
			goto finish;
		}

		if (verbose) {
			print_message("epoch="DF_U64" tx_id=%d punch="
				      DF_BOOL "\n",
				      entry.ie_id.id_epoch,
				      entry.ie_id.id_tx_id,
				      DP_BOOL(ilog_is_punch(&entry)));
			print_message("expected epoch="DF_U64" punch="
				      DF_BOOL "\n", desc->epoch,
				      DP_BOOL(desc->punch));
		}

		if (desc->epoch != entry.ie_id.id_epoch) {
			print_message("Epoch mismatch "DF_U64" != "DF_U64"\n",
				      desc->epoch, entry.ie_id.id_epoch);
			wrong_epoch++;
		}
		if (desc->punch != ilog_is_punch(&entry)) {
			print_message("Punch mismatch " DF_BOOL " != " DF_BOOL
				      "\n", DP_BOOL(desc->punch),
				      DP_BOOL(ilog_is_punch(&entry)));
			wrong_punch++;
		}

		desc++;
		idx++;
	}
	if (verbose)
		print_message("Done\n");
	if (idx < entries->entry_count) {
		print_message("Not enough entries returned %d < %d\n", idx,
			      entries->entry_count);
		rc = -DER_MISC;
		goto finish;
	}
	if (wrong_punch || wrong_epoch) {
		rc = -DER_MISC;
		goto finish;
	}

	rc = 0;

finish:
	ilog_fetch_finish(&ilog_entries);
	return rc;
}

static int
do_update(daos_handle_t loh, daos_epoch_t epoch,
	  bool punch, bool *prior_punch,
	  int *prior_status, struct entries *entries)
{
	int		rc;

	rc = ilog_update(loh, NULL, epoch, 1, punch);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n",
			      d_errstr(rc));
		return rc;
	}

	if (punch || *prior_punch || *prior_status == PREPARED) {
		rc = entries_set(entries, ENTRY_APPEND, epoch,
				 punch, ENTRIES_END);
		if (rc != 0) {
			print_message("Failure appending entry\n");
			return rc;
		}
		*prior_punch = punch;
		*prior_status = current_status;
	} else if (verbose) {
		print_message("Skipping "DF_U64" status=%d\n",
			      epoch, current_status);
	}

	return 0;
}

struct version_cache {
	uint32_t	vc_ver[2];
	int		vc_idx;
};

static void
version_cache_init(struct version_cache *vcache)
{
	memset(vcache, 0, sizeof(*vcache));
	vcache->vc_ver[1] = 0;
}

static bool
version_cache_fetch_helper(struct version_cache *vcache, daos_handle_t loh,
			   bool expect_change)
{
	vcache->vc_ver[vcache->vc_idx] = ilog_version_get(loh);
	if (expect_change) {
		if (vcache->vc_ver[vcache->vc_idx] <=
		    vcache->vc_ver[1 - vcache->vc_idx]) {
			print_message("version %d should be greater than %d\n",
				      vcache->vc_ver[vcache->vc_idx],
				      vcache->vc_ver[1 - vcache->vc_idx]);
			return false;
		}
	} else if (vcache->vc_ver[0] != vcache->vc_ver[1]) {
		print_message("version unexpected mismatch: %d != %d\n",
			      vcache->vc_ver[0], vcache->vc_ver[1]);
		return false;
	}
	/* toggle to other entry */
	vcache->vc_idx = 1 - vcache->vc_idx;
	return true;
}

#define version_cache_fetch(vcache, loh, expect_change)			\
	assert_true(version_cache_fetch_helper(vcache, loh, expect_change))

#define NUM_REC 20
static void
ilog_test_update(void **state)
{
	struct io_test_args	*args = *state;
	struct vos_pool		*pool;
	struct umem_instance	*umm;
	struct entries		*entries = args->custom;
	struct ilog_df		*ilog;
	struct version_cache	 version_cache;
	daos_epoch_t		 epoch;
	daos_handle_t		 loh;
	int			 prior_status;
	bool			 prior_punch;
	int			 rc;
	int			 idx;

	version_cache_init(&version_cache);

	assert_non_null(entries);

	pool = vos_hdl2pool(args->ctx.tc_po_hdl);
	assert_non_null(pool);
	umm = vos_pool2umm(pool);

	ilog = ilog_alloc_root(umm);

	rc = ilog_create(umm, ilog);
	LOG_FAIL(rc, 0, "Failed to create a new incarnation log\n");

	rc = ilog_open(umm, ilog, &ilog_callbacks, &loh);
	LOG_FAIL(rc, 0, "Failed to open incarnation log\n");

	version_cache_fetch(&version_cache, loh, true);

	epoch = 1;
	current_status = COMMITTABLE;
	rc = ilog_update(loh, NULL, epoch, 1, false);
	LOG_FAIL(rc, 0, "Failed to insert ilog entry\n");

	rc = entries_set(entries, ENTRY_NEW, 1, false, ENTRIES_END);
	assert_rc_equal(rc, 0);
	rc = entries_check(umm, ilog, &ilog_callbacks, NULL, 0, entries);
	assert_rc_equal(rc, 0);

	/* Test upgrade to punch in root */
	rc = ilog_update(loh, NULL, epoch, 2, true);
	LOG_FAIL(rc, 0, "Failed to insert ilog entry\n");

	version_cache_fetch(&version_cache, loh, true);

	rc = entries_set(entries, ENTRY_REPLACE, 1, true, ENTRIES_END);
	assert_rc_equal(rc, 0);
	rc = entries_check(umm, ilog, &ilog_callbacks, NULL, 0, entries);
	assert_rc_equal(rc, 0);

	/** Same epoch, different transaction, same operation.  In other
	 *  words, both the existing entry and this one are punches so
	 *  we get back -DER_ALREADY because the existing entry covers
	 *  this punch.  This only happens if both entries are not using
	 *  DTX which is the case here.
	 */
	fake_tx_reset();
	rc = ilog_update(loh, NULL, epoch, 3, true);
	LOG_FAIL(rc, -DER_ALREADY, "Epoch entry already exists. "
		 "Replacing with different DTX should get "
		 "-DER_ALREADY\n");

	/** Same epoch, different DTX, different operation operation.
	 *  Trying to replace a punch with an update at the same
	 *  epoch requires a restart with later epoch
	 */
	fake_tx_reset();
	rc = ilog_update(loh, NULL, epoch, 3, false);
	LOG_FAIL(rc, -DER_TX_RESTART, "Epoch entry already exists. "
		 "Replacing with different DTX should get "
		 "-DER_TX_RESTART\n");

	version_cache_fetch(&version_cache, loh, false);

	/** no change */
	rc = entries_check(umm, ilog, &ilog_callbacks, NULL, 0, entries);
	assert_rc_equal(rc, 0);

	/** New epoch, creation */
	epoch = 2;
	rc = ilog_update(loh, NULL, epoch, 1, false);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	rc = entries_set(entries, ENTRY_APPEND, 2, false, ENTRIES_END);
	assert_rc_equal(rc, 0);
	rc = entries_check(umm, ilog, &ilog_callbacks, NULL, 0, entries);
	assert_rc_equal(rc, 0);

	/** New epoch, upgrade to punch */
	rc = ilog_update(loh, NULL, epoch, 2, true);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	rc = entries_set(entries, ENTRY_REPLACE, 2, true, ENTRIES_END);
	assert_rc_equal(rc, 0);
	rc = entries_check(umm, ilog, &ilog_callbacks, NULL, 0, entries);
	assert_rc_equal(rc, 0);


	epoch = 3;
	prior_punch = true;
	prior_status = PREPARED;
	for (idx = 0; idx < NUM_REC; idx++) {
		current_status = 1 + idx % 2;
		rc = do_update(loh, epoch,
			       (((idx + 1) % 10) == 0) ? true : false,
			       &prior_punch, &prior_status, entries);
		LOG_FAIL(rc, 0, "Failed to insert log entry\n");
		rc = entries_check(umm, ilog, &ilog_callbacks, NULL, 0,
				   entries);
		LOG_FAIL(rc, 0, "Punch or epoch mismatch\n");
		epoch++;
	}
	/** NB: It's a bit of a hack to insert aborted entries.   Since fetch
	 * will happily return everything, insert one more punch that will
	 * guarantee we don't have any aborted entries in the log.
	 */
	current_status = PREPARED;
	rc = do_update(loh, epoch, true, &prior_punch, &prior_status,
		       entries);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");

	rc = entries_check(umm, ilog, &ilog_callbacks, NULL, 0, entries);
	assert_rc_equal(rc, 0);
	ilog_close(loh);
	rc = ilog_destroy(umm, &ilog_callbacks, ilog);
	assert_rc_equal(rc, 0);

	assert_true(d_list_empty(&fake_tx_list));

	ilog_free_root(umm, ilog);
}


static void
ilog_test_abort(void **state)
{
	struct fake_tx_entry	*entry;
	struct fake_tx_entry	*tmp;
	struct io_test_args	*args = *state;
	struct vos_pool		*pool;
	struct umem_instance	*umm;
	struct ilog_df		*ilog;
	struct entries		*entries = args->custom;
	struct version_cache	 version_cache;
	struct ilog_id		 id;
	daos_handle_t		 loh;
	bool			 first;
	int			 idx;
	int			 iter;
	int			 rc;

	version_cache_init(&version_cache);
	assert_non_null(entries);

	pool = vos_hdl2pool(args->ctx.tc_po_hdl);
	assert_non_null(pool);
	umm = vos_pool2umm(pool);

	ilog = ilog_alloc_root(umm);

	rc = ilog_create(umm, ilog);
	LOG_FAIL(rc, 0, "Failed to create a new incarnation log\n");

	rc = ilog_open(umm, ilog, &ilog_callbacks, &loh);
	LOG_FAIL(rc, 0, "Failed to open new incarnation log\n");
	version_cache_fetch(&version_cache, loh, true);

	id.id_epoch = 1;
	current_status = PREPARED;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, false);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	rc = entries_set(entries, ENTRY_NEW, 1, false, ENTRIES_END);
	assert_rc_equal(rc, 0);
	rc = entries_check(umm, ilog, &ilog_callbacks, NULL, 0, entries);
	assert_rc_equal(rc, 0);

	id = current_tx_id;
	rc = ilog_abort(loh, &id);
	LOG_FAIL(rc, 0, "Failed to abort log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	rc = entries_set(entries, ENTRY_NEW, ENTRIES_END);
	assert_rc_equal(rc, 0);
	rc = entries_check(umm, ilog, &ilog_callbacks, NULL, -DER_NONEXIST,
			   entries);
	assert_rc_equal(rc, 0);

	rc = ilog_update(loh, NULL, id.id_epoch, 2, false);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	for (iter = 0; iter < 5; iter++) {
		rc = entries_set(entries, ENTRY_NEW, 1, false, ENTRIES_END);
		assert_rc_equal(rc, 0);
		rc = entries_check(umm, ilog, &ilog_callbacks, NULL, 0,
				   entries);
		assert_rc_equal(rc, 0);

		id.id_epoch = 2 + NUM_REC * iter;
		/* Insert a bunch then delete them */
		for (idx = 2; idx < NUM_REC; idx++) {
			bool	punch = idx & 1 ? true : false;

			rc = ilog_update(loh, NULL, id.id_epoch, idx - 1,
					 punch);
			LOG_FAIL(rc, 0, "Failed to insert log entry\n");
			version_cache_fetch(&version_cache, loh, true);
			rc = entries_set(entries, ENTRY_APPEND, id.id_epoch,
					 punch, ENTRIES_END);
			LOG_FAIL(rc, 0, "Failed to set entries\n");
			id.id_epoch++;
		}

		rc = entries_check(umm, ilog, &ilog_callbacks, NULL, 0,
				   entries);
		assert_rc_equal(rc, 0);

		/* delete the same entries, leaving the one entry in the tree */
		id.id_epoch = 2 + NUM_REC * iter;
		first = true;
		d_list_for_each_entry_safe(entry, tmp, &fake_tx_list, link) {
			if (first) {
				first = false; /* skip first */
				continue;
			}
			id.id_tx_id = entry->tx_id;
			id.id_epoch = entry->epoch;
			rc = ilog_abort(loh, &id);
			id.id_epoch++;
			LOG_FAIL(rc, 0, "Failed to abort log entry\n");
			version_cache_fetch(&version_cache, loh, true);
		}
	}

	rc = entries_set(entries, ENTRY_NEW, 1, false, ENTRIES_END);
	assert_rc_equal(rc, 0);
	rc = entries_check(umm, ilog, &ilog_callbacks, NULL, 0, entries);
	assert_rc_equal(rc, 0);

	ilog_close(loh);
	rc = ilog_destroy(umm, &ilog_callbacks, ilog);
	assert_rc_equal(rc, 0);

	assert_true(d_list_empty(&fake_tx_list));
	ilog_free_root(umm, ilog);
}

static void
ilog_test_persist(void **state)
{
	struct io_test_args	*args = *state;
	struct vos_pool		*pool;
	struct umem_instance	*umm;
	struct ilog_df		*ilog;
	struct entries		*entries = args->custom;
	struct version_cache	 version_cache;
	struct ilog_id		 id;
	daos_handle_t		 loh;
	struct ilog_id		 saved_tx_id1, saved_tx_id2;
	int			 rc;

	assert_non_null(entries);
	pool = vos_hdl2pool(args->ctx.tc_po_hdl);
	assert_non_null(pool);
	umm = vos_pool2umm(pool);

	version_cache_init(&version_cache);

	ilog = ilog_alloc_root(umm);

	rc = ilog_create(umm, ilog);
	LOG_FAIL(rc, 0, "Failed to create a new incarnation log\n");

	rc = ilog_open(umm, ilog, &ilog_callbacks, &loh);
	LOG_FAIL(rc, 0, "Failed to open incarnation log\n");
	version_cache_fetch(&version_cache, loh, true);

	id.id_epoch = 1;
	current_status = PREPARED;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, false);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	saved_tx_id1 = current_tx_id;
	version_cache_fetch(&version_cache, loh, true);

	id.id_epoch = 2;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, false);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	saved_tx_id2 = current_tx_id;
	version_cache_fetch(&version_cache, loh, true);

	id.id_epoch = 3;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, false);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	id.id_epoch = 4;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, true);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	id = saved_tx_id2;
	rc = ilog_persist(loh, &id);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	rc = entries_set(entries, ENTRY_NEW, 1, false, 2, false, 3, false,
			 4, true, ENTRIES_END);
	assert_rc_equal(rc, 0);
	rc = entries_check(umm, ilog, &ilog_callbacks, NULL, 0, entries);
	assert_rc_equal(rc, 0);

	id = saved_tx_id1;
	rc = ilog_persist(loh, &id);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	rc = entries_set(entries, ENTRY_NEW, 1, false, 2, false, 3, false, 4,
			 true, ENTRIES_END);
	assert_rc_equal(rc, 0);
	rc = entries_check(umm, ilog, &ilog_callbacks, NULL, 0, entries);
	assert_rc_equal(rc, 0);

	ilog_close(loh);
	rc = ilog_destroy(umm, &ilog_callbacks, ilog);
	assert_rc_equal(rc, 0);
	assert_true(d_list_empty(&fake_tx_list));
	ilog_free_root(umm, ilog);
}

static void
ilog_test_aggregate(void **state)
{
	struct io_test_args	*args = *state;
	struct vos_pool		*pool;
	struct umem_instance	*umm;
	struct ilog_df		*ilog;
	struct version_cache	 version_cache;
	struct ilog_entries	 ilents;
	struct entries		*entries = args->custom;
	struct ilog_id		 id;
	daos_epoch_range_t	 epr = {0, DAOS_EPOCH_MAX};
	daos_handle_t		 loh;
	int			 rc;

	version_cache_init(&version_cache);
	assert_non_null(entries);
	pool = vos_hdl2pool(args->ctx.tc_po_hdl);
	assert_non_null(pool);
	umm = vos_pool2umm(pool);

	ilog_fetch_init(&ilents);

	ilog = ilog_alloc_root(umm);

	rc = ilog_create(umm, ilog);
	LOG_FAIL(rc, 0, "Failed to create a new incarnation log\n");

	rc = ilog_open(umm, ilog, &ilog_callbacks, &loh);
	LOG_FAIL(rc, 0, "Failed to open incarnation log\n");

	id.id_epoch = 1;
	current_status = PREPARED;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, false);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");

	version_cache_fetch(&version_cache, loh, true);

	id.id_epoch = 2;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, false);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	id.id_epoch = 3;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, false);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	id.id_epoch = 4;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, true);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	commit_all();
	epr.epr_lo = 2;
	epr.epr_hi = 4;
	rc = ilog_aggregate(umm, ilog, &ilog_callbacks, &epr, false, 0, 0,
			    &ilents);
	LOG_FAIL(rc, 0, "Failed to aggregate ilog\n");
	version_cache_fetch(&version_cache, loh, true);

	rc = entries_set(entries, ENTRY_NEW, 1, false, 4, true, ENTRIES_END);
	assert_rc_equal(rc, 0);
	rc = entries_check(umm, ilog, &ilog_callbacks, NULL, 0, entries);
	assert_rc_equal(rc, 0);

	id.id_epoch = 5;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, true);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	id.id_epoch = 6;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, false);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	commit_all();

	epr.epr_lo = 0;
	epr.epr_hi = 6;
	rc = ilog_aggregate(umm, ilog, &ilog_callbacks, &epr, false, 0, 0,
			    &ilents);
	LOG_FAIL(rc, 0, "Failed to aggregate ilog\n");
	version_cache_fetch(&version_cache, loh, true);
	rc = entries_set(entries, ENTRY_NEW, 6, false, ENTRIES_END);
	assert_rc_equal(rc, 0);
	rc = entries_check(umm, ilog, &ilog_callbacks, NULL, 0, entries);
	assert_rc_equal(rc, 0);

	id.id_epoch = 7;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, true);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);
	commit_all();
	epr.epr_hi = 7;
	rc = ilog_aggregate(umm, ilog, &ilog_callbacks, &epr, false, 0, 0,
			    &ilents);
	/* 1 means empty */
	LOG_FAIL(rc, 1, "Failed to aggregate log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	rc = entries_set(entries, ENTRY_NEW, ENTRIES_END);
	assert_rc_equal(rc, 0);
	rc = entries_check(umm, ilog, &ilog_callbacks, NULL, -DER_NONEXIST,
			   entries);
	assert_rc_equal(rc, 0);
	assert_true(d_list_empty(&fake_tx_list));

	ilog_close(loh);
	rc = ilog_destroy(umm, &ilog_callbacks, ilog);
	assert_rc_equal(rc, 0);

	ilog_free_root(umm, ilog);
	ilog_fetch_finish(&ilents);
}

static void
ilog_test_discard(void **state)
{
	struct io_test_args	*args = *state;
	struct vos_pool		*pool;
	struct umem_instance	*umm;
	struct ilog_df		*ilog;
	struct version_cache	 version_cache;
	struct ilog_entries	 ilents;
	struct entries		*entries = args->custom;
	struct ilog_id		 id;
	daos_epoch_range_t	 epr = {0, DAOS_EPOCH_MAX};
	daos_handle_t		 loh;
	int			 rc;

	version_cache_init(&version_cache);
	assert_non_null(entries);
	pool = vos_hdl2pool(args->ctx.tc_po_hdl);
	assert_non_null(pool);
	umm = vos_pool2umm(pool);

	ilog_fetch_init(&ilents);

	ilog = ilog_alloc_root(umm);

	rc = ilog_create(umm, ilog);
	LOG_FAIL(rc, 0, "Failed to create a new incarnation log\n");

	rc = ilog_open(umm, ilog, &ilog_callbacks, &loh);
	LOG_FAIL(rc, 0, "Failed to open incarnation log\n");

	id.id_epoch = 1;
	current_status = PREPARED;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, false);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");

	version_cache_fetch(&version_cache, loh, true);

	id.id_epoch = 2;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, false);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	id.id_epoch = 3;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, false);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	id.id_epoch = 4;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, true);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	commit_all();
	epr.epr_lo = 2;
	epr.epr_hi = 4;
	rc = ilog_aggregate(umm, ilog, &ilog_callbacks, &epr, true, 0, 0,
			    &ilents);
	LOG_FAIL(rc, 0, "Failed to aggregate ilog\n");
	version_cache_fetch(&version_cache, loh, true);

	rc = entries_set(entries, ENTRY_NEW, 1, false, ENTRIES_END);
	assert_rc_equal(rc, 0);
	rc = entries_check(umm, ilog, &ilog_callbacks, NULL, 0, entries);
	assert_rc_equal(rc, 0);

	id.id_epoch = 5;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, true);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	id.id_epoch = 6;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, false);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);

	commit_all();
	epr.epr_lo = 0;
	epr.epr_hi = 6;
	rc = ilog_aggregate(umm, ilog, &ilog_callbacks, &epr, true, 0, 0,
			    &ilents);
	/* 1 means empty */
	LOG_FAIL(rc, 1, "Failed to aggregate ilog\n");
	version_cache_fetch(&version_cache, loh, true);
	rc = entries_set(entries, ENTRY_NEW, ENTRIES_END);
	assert_rc_equal(rc, 0);
	rc = entries_check(umm, ilog, &ilog_callbacks, NULL, -DER_NONEXIST,
			   entries);
	assert_rc_equal(rc, 0);

	id.id_epoch = 7;
	rc = ilog_update(loh, NULL, id.id_epoch, 1, false);
	LOG_FAIL(rc, 0, "Failed to insert log entry\n");
	version_cache_fetch(&version_cache, loh, true);
	commit_all();

	epr.epr_hi = 7;
	rc = ilog_aggregate(umm, ilog, &ilog_callbacks, &epr, true, 0, 0,
			    &ilents);
	/* 1 means empty */
	LOG_FAIL(rc, 1, "Failed to aggregate ilog\n");
	version_cache_fetch(&version_cache, loh, true);

	rc = entries_set(entries, ENTRY_NEW, ENTRIES_END);
	assert_rc_equal(rc, 0);
	rc = entries_check(umm, ilog, &ilog_callbacks, NULL, -DER_NONEXIST,
			   entries);
	assert_rc_equal(rc, 0);
	assert_true(d_list_empty(&fake_tx_list));

	ilog_close(loh);
	rc = ilog_destroy(umm, &ilog_callbacks, ilog);
	assert_rc_equal(rc, 0);

	ilog_free_root(umm, ilog);
	ilog_fetch_finish(&ilents);
}

static const struct CMUnitTest inc_tests[] = {
	{ "VOS500.1: VOS incarnation log UPDATE", ilog_test_update, NULL,
		NULL},
	{ "VOS500.2: VOS incarnation log ABORT test", ilog_test_abort, NULL,
		NULL},
	{ "VOS500.3: VOS incarnation log PERSIST test", ilog_test_persist, NULL,
		NULL},
	{ "VOS500.4: VOS incarnation log AGGREGATE test", ilog_test_aggregate,
		NULL, NULL},
	{ "VOS500.5: VOS incarnation log DISCARD test", ilog_test_discard,
		NULL, NULL},
};

int
setup_ilog(void **state)
{
	struct entries		*entries;
	struct io_test_args	*arg;

	setup_io(state);

	arg = *state;

	D_ALLOC_PTR(entries);
	if (entries == NULL)
		return -DER_NOMEM;

	arg->custom = entries;

	return entries_init(entries);
}

int
teardown_ilog(void **state)
{
	struct io_test_args *arg = *state;

	entries_fini(arg->custom);
	D_FREE(arg->custom);

	teardown_io(state);

	return 0;
}

int
run_ilog_tests(const char *cfg)
{
	char	test_name[DTS_CFG_MAX];

	dts_create_config(test_name, "VOS Incarnation log tests %s", cfg);
	return cmocka_run_group_tests_name(test_name,
					   inc_tests, setup_ilog,
					   teardown_ilog);
}
