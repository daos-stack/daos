/**
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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

#define DF_BOOL "%s"
#define DP_BOOL(punch) ((punch) ? "true" : "false")

static bool verbose;

static struct ilog_df *
ilog_alloc_root(struct umem_instance *umm)
{
	int		 rc = 0;
	umem_off_t	 ilog_off = UMOFF_NULL;

	rc = vos_tx_begin(umm);
	if (rc != 0) {
		print_message("Tx begin failed\n");
		goto done;
	}

	ilog_off = umem_zalloc(umm, sizeof(struct ilog_df));
	if (ilog_off == UMOFF_NULL) {
		print_message("Allocation failed\n");
		rc = -DER_NOSPACE;
	}

	rc = vos_tx_end(umm, rc);
done:
	assert_int_equal(rc, 0);

	return umem_off2ptr(umm, ilog_off);
}

static void
ilog_free_root(struct umem_instance *umm, struct ilog_df *ilog)
{
	int		 rc = 0;

	rc = vos_tx_begin(umm);
	if (rc != 0) {
		print_message("Tx begin failed\n");
		goto done;
	}

	rc = umem_free(umm, umem_ptr2off(umm, ilog));

	rc = vos_tx_end(umm, rc);
done:
	assert_int_equal(rc, 0);
}

enum {
	COMMITTED = 0,
	COMMITTABLE,
	PREPARED,
};

static int		current_status;
static umem_off_t	current_tx_id;

struct fake_tx_entry {
	umem_off_t	root_off;
	d_list_t	link;
	int		status;
};

static D_LIST_HEAD(fake_tx_list);

static enum ilog_status
fake_tx_status_get(struct umem_instance *umm, umem_off_t tx_id, uint32_t intent,
		   void *args)
{
	struct fake_tx_entry	*entry = (struct fake_tx_entry *)tx_id;

	if (entry == NULL)
		return ILOG_VISIBLE;

	switch (entry->status) {
	case COMMITTED:
	case COMMITTABLE:
		return ILOG_VISIBLE;
	case PREPARED:
		return ILOG_INVISIBLE;
	}
	D_ASSERT(0);
	return 0;
}

void
fake_tx_reset(void)
{
	/** Just set it so it doesn't match anything */
	current_tx_id = 0xbaadbeef;
}

void
fake_tx_remove(void)
{
	struct fake_tx_entry	*entry = (struct fake_tx_entry *)current_tx_id;

	d_list_del(&entry->link);
	D_FREE(entry);
}

static int
fake_tx_is_same_tx(struct umem_instance *umm, umem_off_t tx_id, bool *same,
		   void *args)
{
	if (tx_id == current_tx_id)
		*same = true;
	else
		*same = false;

	return 0;
}

static int
fake_tx_log_add(struct umem_instance *umm, umem_off_t offset, umem_off_t *tx_id,
		void *args)
{
	struct fake_tx_entry	*entry;

	D_ALLOC_PTR(entry);
	if (entry == NULL)
		return -DER_NOMEM;

	entry->root_off = offset;
	entry->status = current_status;
	d_list_add_tail(&entry->link, &fake_tx_list);

	current_tx_id = *tx_id = (umem_off_t)entry;

	return 0;
}

static int
fake_tx_log_del(struct umem_instance *umm, umem_off_t offset, umem_off_t tx_id,
		void *args)
{
	struct fake_tx_entry	*entry;

	d_list_for_each_entry(entry, &fake_tx_list, link) {
		if (entry == (struct fake_tx_entry *)tx_id) {
			if (entry->root_off != offset) {
				print_message("Mismatched ilog root "DF_U64"!="
					      DF_U64"\n", entry->root_off,
					      offset);
				return -DER_INVAL;
			}
			d_list_del(&entry->link);
			D_FREE(entry);
			return 0;
		}
	}

	return 0;
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
	struct desc	*entries;
	int		 entry_count;
	int		 alloc_count;
};

#define MAX_ILOG_LEN 2000
static int
entries_init(struct entries *entries)
{
	D_ALLOC_ARRAY(entries->entries, MAX_ILOG_LEN);
	if (entries->entries == NULL)
		return -DER_NOMEM;

	entries->entry_count = 0;
	entries->alloc_count = MAX_ILOG_LEN;

	return 0;
}

static void
entries_fini(struct entries *entries)
{
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

		if (entries->entry_count == entries->alloc_count)
			return -DER_NOMEM;

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
entries_check(daos_handle_t loh, const daos_epoch_range_t *epr,
	      int expected_rc, struct entries *entries)
{
	struct ilog_entry	*entry;
	struct desc		*desc;
	struct ilog_entries	 ilog_entries;
	int			 idx;
	int			 rc = 0;
	int			 wrong_epoch = 0;
	int			 wrong_punch = 0;

	ilog_fetch_init(&ilog_entries);

	rc = ilog_fetch(loh, DAOS_INTENT_DEFAULT, epr, &ilog_entries);
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
	ilog_foreach_entry(&ilog_entries, entry) {
		if (idx == entries->entry_count) {
			print_message("Too many entries in ilog\n");
			rc = -DER_MISC;
			goto finish;
		}

		if (verbose) {
			print_message("epoch="DF_U64" tx_id="DF_U64" punch="
				      DF_BOOL "\n",
				      entry->ie_id.id_epoch,
				      entry->ie_id.id_tx_id,
				      DP_BOOL(entry->ie_punch));
			print_message("expected epoch="DF_U64" punch="
				      DF_BOOL "\n", desc->epoch,
				      DP_BOOL(desc->punch));
		}

		if (desc->epoch != entry->ie_id.id_epoch) {
			print_message("Epoch mismatch "DF_U64" != "DF_U64"\n",
				      desc->epoch, entry->ie_id.id_epoch);
			wrong_epoch++;
		}
		if (desc->punch != entry->ie_punch) {
			print_message("Punch mismatch " DF_BOOL " != " DF_BOOL
				      "\n", DP_BOOL(desc->punch),
				      DP_BOOL(entry->ie_punch));
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

	rc = ilog_update(loh, epoch, punch);
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
		print_message("Skipping "DF_U64" punch=%s status=%d\n",
			      epoch, punch ? "true" : "false", current_status);
	}


	return 0;
}

#define NUM_REC 20
static void
ilog_test_update(void **state)
{
	struct io_test_args	*args = *state;
	struct vos_pool		*pool;
	struct umem_instance	*umm;
	struct entries		*entries = args->custom;
	struct ilog_df		*ilog;
	daos_epoch_t		 epoch;
	daos_handle_t		 loh;
	int			 prior_status;
	bool			 prior_punch;
	int			 rc;
	int			 idx;

	assert_non_null(entries);

	pool = vos_hdl2pool(args->ctx.tc_po_hdl);
	assert_non_null(pool);
	umm = vos_pool2umm(pool);

	ilog = ilog_alloc_root(umm);

	rc = ilog_create(umm, ilog);
	if (rc != 0) {
		print_message("Failed to create a new incarnation log: %s\n",
			      d_errstr(rc));
		assert(0);
	}

	rc = ilog_open(umm, ilog, &ilog_callbacks, &loh);
	if (rc != 0) {
		print_message("Failed to open incarnation log: %s\n",
			      d_errstr(rc));
		assert(0);
	}

	epoch = 1;
	current_status = COMMITTABLE;
	rc = ilog_update(loh, epoch, false);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	rc = entries_set(entries, ENTRY_NEW, 1, false, ENTRIES_END);
	assert_int_equal(rc, 0);
	rc = entries_check(loh, NULL, 0, entries);
	assert_int_equal(rc, 0);

	/* Test upgrade to punch in root */
	rc = ilog_update(loh, epoch, true);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	rc = entries_set(entries, ENTRY_REPLACE, 1, true, ENTRIES_END);
	assert_int_equal(rc, 0);
	rc = entries_check(loh, NULL, 0, entries);
	assert_int_equal(rc, 0);

	/** Same epoch, different DTX */
	fake_tx_reset();
	rc = ilog_update(loh, epoch, true);
	if (rc != -DER_AGAIN) {
		print_message("Epoch entry already exists.  Replacing with"
			      " different DTX should get -DER_AGAIN: rc=%s\n",
			      d_errstr(rc));
		assert(0);
	}

	/** no change */
	rc = entries_check(loh, NULL, 0, entries);
	assert_int_equal(rc, 0);

	/** New epoch, creation */
	epoch = 2;
	rc = ilog_update(loh, epoch, false);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	rc = entries_set(entries, ENTRY_APPEND, 2, false, ENTRIES_END);
	assert_int_equal(rc, 0);
	rc = entries_check(loh, NULL, 0, entries);
	assert_int_equal(rc, 0);

	/** New epoch, upgrade to punch */
	rc = ilog_update(loh, epoch, true);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	rc = entries_set(entries, ENTRY_REPLACE, 2, true, ENTRIES_END);
	assert_int_equal(rc, 0);
	rc = entries_check(loh, NULL, 0, entries);
	assert_int_equal(rc, 0);


	epoch = 3;
	prior_punch = true;
	prior_status = PREPARED;
	for (idx = 0; idx < NUM_REC; idx++) {
		current_status = 1 + idx % 2;
		rc = do_update(loh, epoch,
			       (((idx + 1) % 10) == 0) ? true : false,
			       &prior_punch, &prior_status, entries);
		if (rc != 0) {
			print_message("failed update: %s\n", d_errstr(rc));
			assert(0);
		}
		epoch++;
	}
	/** NB: It's a bit of a hack to insert aborted entries.   Since fetch
	 * will happily return everything, insert one more punch that will
	 * guarantee we don't have any aborted entries in the log.
	 */
	current_status = PREPARED;
	rc = do_update(loh, epoch, true, &prior_punch, &prior_status,
		       entries);
	if (rc != 0) {
		print_message("failed update: %s\n", d_errstr(rc));
		assert(0);
	}

	rc = entries_check(loh, NULL, 0, entries);
	assert_int_equal(rc, 0);
	ilog_close(loh);
	rc = ilog_destroy(umm, &ilog_callbacks, ilog);
	assert_int_equal(rc, 0);

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
	struct ilog_id		 id;
	daos_handle_t		 loh;
	bool			 first;
	int			 idx;
	int			 iter;
	int			 rc;

	assert_non_null(entries);

	pool = vos_hdl2pool(args->ctx.tc_po_hdl);
	assert_non_null(pool);
	umm = vos_pool2umm(pool);

	ilog = ilog_alloc_root(umm);

	rc = ilog_create(umm, ilog);
	if (rc != 0) {
		print_message("Failed to create a new incarnation log: %s\n",
			      d_errstr(rc));
		assert(0);
	}

	rc = ilog_open(umm, ilog, &ilog_callbacks, &loh);
	if (rc != 0) {
		print_message("Failed to open incarnation log: %s\n",
			      d_errstr(rc));
		assert(0);
	}

	id.id_epoch = 1;
	current_status = PREPARED;
	rc = ilog_update(loh, id.id_epoch, false);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	rc = entries_set(entries, ENTRY_NEW, 1, false, ENTRIES_END);
	assert_int_equal(rc, 0);
	rc = entries_check(loh, NULL, 0, entries);
	assert_int_equal(rc, 0);

	id.id_tx_id = current_tx_id;
	rc = ilog_abort(loh, &id);
	if (rc != 0) {
		print_message("Failed to delete log entry: %s\n", d_errstr(rc));
		assert(0);
	}
	fake_tx_remove();

	rc = entries_set(entries, ENTRY_NEW, ENTRIES_END);
	assert_int_equal(rc, 0);
	rc = entries_check(loh, NULL, -DER_NONEXIST, entries);
	assert_int_equal(rc, 0);

	rc = ilog_update(loh, id.id_epoch, false);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	for (iter = 0; iter < 5; iter++) {
		rc = entries_set(entries, ENTRY_NEW, 1, false, ENTRIES_END);
		assert_int_equal(rc, 0);
		rc = entries_check(loh, NULL, 0, entries);
		assert_int_equal(rc, 0);

		id.id_epoch = 2 + NUM_REC * iter;
		/* Insert a bunch then delete them */
		for (idx = 2; idx < NUM_REC; idx++) {
			bool	punch = idx & 1 ? true : false;

			rc = ilog_update(loh, id.id_epoch,
					 punch);
			if (rc != 0) {
				print_message("Failed to insert ilog: %s\n",
					      d_errstr(rc));
				assert(0);
			}
			rc = entries_set(entries, ENTRY_APPEND, id.id_epoch,
					 punch, ENTRIES_END);
			if (rc != 0) {
				print_message("Failed to set entries\n");
				assert(0);
			}
			id.id_epoch++;
		}

		rc = entries_check(loh, NULL, 0, entries);
		assert_int_equal(rc, 0);

		/* delete the same entries, leaving the one entry in the tree */
		id.id_epoch = 2 + NUM_REC * iter;
		first = true;
		d_list_for_each_entry_safe(entry, tmp, &fake_tx_list, link) {
			if (first) {
				first = false; /* skip first */
				continue;
			}
			id.id_tx_id = (umem_off_t)entry;
			rc = ilog_abort(loh, &id);
			id.id_epoch++;
			if (rc != 0) {
				print_message("Failed to delete ilog: %s\n",
					      d_errstr(rc));
				assert(0);
			}
			current_tx_id = id.id_tx_id;
			fake_tx_remove();
		}
	}

	rc = entries_set(entries, ENTRY_NEW, 1, false, ENTRIES_END);
	assert_int_equal(rc, 0);
	rc = entries_check(loh, NULL, 0, entries);
	assert_int_equal(rc, 0);

	ilog_close(loh);
	rc = ilog_destroy(umm, &ilog_callbacks, ilog);
	assert_int_equal(rc, 0);

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
	struct ilog_id		 id;
	daos_handle_t		 loh;
	umem_off_t		 saved_tx_id1, saved_tx_id2;
	int			 rc;

	assert_non_null(entries);
	pool = vos_hdl2pool(args->ctx.tc_po_hdl);
	assert_non_null(pool);
	umm = vos_pool2umm(pool);

	ilog = ilog_alloc_root(umm);

	rc = ilog_create(umm, ilog);
	if (rc != 0) {
		print_message("Failed to create a new incarnation log: %s\n",
			      d_errstr(rc));
		assert(0);
	}

	rc = ilog_open(umm, ilog, &ilog_callbacks, &loh);
	if (rc != 0) {
		print_message("Failed to open incarnation log: %s\n",
			      d_errstr(rc));
		assert(0);
	}

	id.id_epoch = 1;
	current_status = PREPARED;
	rc = ilog_update(loh, id.id_epoch, false);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}
	saved_tx_id1 = current_tx_id;

	id.id_epoch = 2;
	rc = ilog_update(loh, id.id_epoch, false);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}
	saved_tx_id2 = current_tx_id;

	id.id_epoch = 3;
	rc = ilog_update(loh, id.id_epoch, false);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	id.id_epoch = 4;
	rc = ilog_update(loh, id.id_epoch, true);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	id.id_epoch = 2;
	id.id_tx_id = saved_tx_id2;
	rc = ilog_persist(loh, &id);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}
	current_tx_id = saved_tx_id2;
	fake_tx_remove();

	rc = entries_set(entries, ENTRY_NEW, 1, false, 2, false, 4, true,
			 ENTRIES_END);
	assert_int_equal(rc, 0);
	rc = entries_check(loh, NULL, 0, entries);
	assert_int_equal(rc, 0);

	id.id_epoch = 1;
	id.id_tx_id = saved_tx_id1;
	rc = ilog_persist(loh, &id);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}
	current_tx_id = saved_tx_id1;
	fake_tx_remove();

	rc = entries_set(entries, ENTRY_NEW, 1, false, 4, true, ENTRIES_END);
	assert_int_equal(rc, 0);
	rc = entries_check(loh, NULL, 0, entries);
	assert_int_equal(rc, 0);

	ilog_close(loh);
	rc = ilog_destroy(umm, &ilog_callbacks, ilog);
	assert_int_equal(rc, 0);
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
	struct entries		*entries = args->custom;
	struct ilog_id		 id;
	daos_epoch_range_t	 epr = {0, DAOS_EPOCH_MAX};
	daos_handle_t		 loh;
	int			 rc;

	assert_non_null(entries);
	pool = vos_hdl2pool(args->ctx.tc_po_hdl);
	assert_non_null(pool);
	umm = vos_pool2umm(pool);

	ilog = ilog_alloc_root(umm);

	rc = ilog_create(umm, ilog);
	if (rc != 0) {
		print_message("Failed to create a new incarnation log: %s\n",
			      d_errstr(rc));
		assert(0);
	}

	rc = ilog_open(umm, ilog, &ilog_callbacks, &loh);
	if (rc != 0) {
		print_message("Failed to open incarnation log: %s\n",
			      d_errstr(rc));
		assert(0);
	}

	id.id_epoch = 1;
	current_status = PREPARED;
	rc = ilog_update(loh, id.id_epoch, false);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	id.id_epoch = 2;
	rc = ilog_update(loh, id.id_epoch, false);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	current_status = COMMITTED;
	id.id_epoch = 3;
	rc = ilog_update(loh, id.id_epoch, false);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	id.id_epoch = 4;
	rc = ilog_update(loh, id.id_epoch, true);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	epr.epr_lo = 2;
	epr.epr_hi = 4;
	rc = ilog_aggregate(loh, &epr);
	if (rc != 0) {
		print_message("Failed to aggregate log entry: "DF_RC"\n",
			      DP_RC(rc));
		assert(0);
	}

	rc = entries_set(entries, ENTRY_NEW, 1, false, ENTRIES_END);
	assert_int_equal(rc, 0);
	rc = entries_check(loh, NULL, 0, entries);
	assert_int_equal(rc, 0);

	id.id_epoch = 5;
	rc = ilog_update(loh, id.id_epoch, true);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	id.id_epoch = 6;
	rc = ilog_update(loh, id.id_epoch, false);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	epr.epr_lo = 0;
	epr.epr_hi = 6;
	rc = ilog_aggregate(loh, &epr);
	if (rc != 0) {
		print_message("Failed to aggregate log entry: "DF_RC"\n",
			      DP_RC(rc));
		assert(0);
	}
	rc = entries_set(entries, ENTRY_NEW, 6, false, ENTRIES_END);
	assert_int_equal(rc, 0);
	rc = entries_check(loh, NULL, 0, entries);
	assert_int_equal(rc, 0);

	id.id_epoch = 7;
	rc = ilog_update(loh, id.id_epoch, true);
	if (rc != 0) {
		print_message("Failed to insert log entry: %s\n", d_errstr(rc));
		assert(0);
	}

	epr.epr_hi = 7;
	rc = ilog_aggregate(loh, &epr);
	if (rc != 1) { /* 1 means empty */
		print_message("Failed to aggregate log entry: "DF_RC"\n",
			      DP_RC(rc));
		assert(0);
	}

	rc = entries_set(entries, ENTRY_NEW, ENTRIES_END);
	assert_int_equal(rc, 0);
	rc = entries_check(loh, NULL, -DER_NONEXIST, entries);
	assert_int_equal(rc, 0);
	assert_true(d_list_empty(&fake_tx_list));

	ilog_close(loh);
	rc = ilog_destroy(umm, &ilog_callbacks, ilog);
	assert_int_equal(rc, 0);

	ilog_free_root(umm, ilog);
}

static const struct CMUnitTest inc_tests[] = {
	{ "VOS500.1: VOS incarnation log UPDATE", ilog_test_update, NULL,
		NULL},
	{ "VOS500.2: VOS incarnation log ABORT test", ilog_test_abort, NULL,
		NULL},
	{ "VOS500.3: VOS incarnation log PERSIST test", ilog_test_persist, NULL,
		NULL},
	{ "VOS500.3: VOS incarnation log AGGREGATE test", ilog_test_aggregate,
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
run_ilog_tests(void)
{
	return cmocka_run_group_tests_name("VOS Incarnation log tests",
					   inc_tests, setup_ilog,
					   teardown_ilog);
}
