/**
 * (C) Copyright 2020 Intel Corporation.
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
 * vos/tests/vts_ts.c
 *
 * Author: Jeffrey Olivier <jeffrey.v.olivier@intel.com>
 */
#define D_LOGFAC	DD_FAC(tests)

#include <stdarg.h>
#include "vts_io.h"
#include <vos_internal.h>
#include <vos_ts.h>

#define NUM_EXTRA	4
struct ts_test_arg {
	uint32_t		*ta_records[VOS_TS_TYPE_COUNT];
	struct vos_ts_set	*ta_ts_set;
	uint32_t		 ta_real_records[VOS_TS_SIZE];
	uint32_t		 ta_counts[VOS_TS_TYPE_COUNT];
	uint32_t		 ta_extra_records[NUM_EXTRA];
};

static void
run_negative_entry_test(struct ts_test_arg *ts_arg, uint32_t type)
{
	struct vos_ts_info	*info;
	struct vos_ts_entry	*entry;
	struct vos_ts_entry	*parent = NULL;
	uint32_t		*idx_ptr;
	uint32_t		 idx;
	uint32_t		 saved_idx;
	uint32_t		 parent_idx;
	bool			 found;
	bool			 reset;

	for (parent_idx = 0; parent_idx < ts_arg->ta_counts[type - 1];
	     parent_idx++) {
		vos_ts_set_reset(ts_arg->ta_ts_set, type - 1, 0);
		idx_ptr = &ts_arg->ta_records[type - 1][parent_idx];
		found = vos_ts_lookup(ts_arg->ta_ts_set, idx_ptr, false,
				      &parent);
		assert_true(found);
		assert_non_null(parent);
		info = parent->te_info;

		reset = false;
		for (idx = 0; idx <= info->ti_cache_mask; idx++) {
			entry = vos_ts_get_negative(ts_arg->ta_ts_set, idx,
						    reset);
			reset = true;
			assert_non_null(entry);
			idx_ptr = &parent->te_miss_idx[entry->te_hash_idx];
			ts_arg->ta_records[type][idx] = *idx_ptr;
		}

		for (idx = info->ti_cache_mask + 1;
		     idx < (info->ti_cache_mask + 1) * 2; idx++) {
			entry = vos_ts_get_negative(ts_arg->ta_ts_set, idx,
						    true);
			assert_non_null(entry);
			idx_ptr = &parent->te_miss_idx[entry->te_hash_idx];
			saved_idx = idx & info->ti_cache_mask;
			assert_int_equal(ts_arg->ta_records[type][saved_idx],
					 *idx_ptr);
		}
	}
}

static void
run_positive_entry_test(struct ts_test_arg *ts_arg, uint32_t type)
{
	struct vos_ts_entry	*entry;
	struct vos_ts_entry	*same;
	uint32_t		*idx_ptr;
	uint32_t		 children_per_parent = 100;
	uint32_t		 parent_idx;
	uint32_t		 idx;
	bool			 reset = false;
	bool			 found;

	for (idx = 0; idx < ts_arg->ta_counts[type]; idx++) {
		found = vos_ts_lookup(ts_arg->ta_ts_set,
				      &ts_arg->ta_records[type][idx], reset,
				      &entry);
		reset = true;
		/** Index should initially be empty */
		assert_false(found);

		if (type != VOS_TS_TYPE_CONT) {
			vos_ts_set_reset(ts_arg->ta_ts_set, type - 2, 0);
			/** ignore the entries that were evicted */
			parent_idx = idx / children_per_parent + NUM_EXTRA + 1;
			idx_ptr = &ts_arg->ta_records[type - 2][parent_idx];
			found = vos_ts_lookup(ts_arg->ta_ts_set, idx_ptr,
					      false, &entry);
			assert_true(found);
			assert_non_null(entry);
		}

		entry = vos_ts_alloc(ts_arg->ta_ts_set,
				     &ts_arg->ta_records[type][idx], idx);
		/** Entry should be allocated */
		assert_non_null(entry);
		assert_int_equal(entry->te_info->ti_type, type);

		found = vos_ts_lookup(ts_arg->ta_ts_set,
				      &ts_arg->ta_records[type][idx], true,
				      &same);
		assert_true(found);
		/** New lookup should get same entry */
		assert_ptr_equal(same, entry);
	}
	assert_int_equal(ts_arg->ta_ts_set->ts_init_count, 1 + type / 2);

	/** Lookup an entry entry */
	found = vos_ts_lookup(ts_arg->ta_ts_set,
			      &ts_arg->ta_records[type][NUM_EXTRA - 2], true,
			      &entry);
	assert_true(found);
	assert_non_null(entry);

	assert_int_equal(ts_arg->ta_ts_set->ts_init_count, 1 + type / 2);
	/** Now evict a few entries */
	for (idx = 0; idx < NUM_EXTRA; idx++) {
		vos_ts_set_reset(ts_arg->ta_ts_set, type, 0);
		entry = vos_ts_alloc(ts_arg->ta_ts_set,
				     &ts_arg->ta_extra_records[idx], idx);
		assert_non_null(entry);
		assert_int_equal(entry->te_info->ti_type, type);
	}
	assert_int_equal(ts_arg->ta_ts_set->ts_init_count, 1 + type / 2);

	/** Now check original entries, only the one used above should still be
	 * there.   Others will have been evicted by LRU policy
	 */
	for (idx = 0; idx <= NUM_EXTRA; idx++) {
		vos_ts_set_reset(ts_arg->ta_ts_set, type, 0);
		found = vos_ts_lookup(ts_arg->ta_ts_set,
				      &ts_arg->ta_records[type][idx], false,
				      &entry);
		if (idx == (NUM_EXTRA - 2))
			assert_true(found);
		else
			assert_false(found);
	}

	/** Now evict the extra records to reset the array for child tests */
	for (idx = 0; idx < NUM_EXTRA; idx++)
		vos_ts_evict(&ts_arg->ta_extra_records[idx]);

	/** evicting an entry should move it to lru */
	vos_ts_set_reset(ts_arg->ta_ts_set, type, 0);
	found = vos_ts_lookup(ts_arg->ta_ts_set, &ts_arg->ta_records[type][20],
			      false, &same);
	assert_true(found);
	assert_int_equal(same->te_info->ti_type, type);
	vos_ts_evict(&ts_arg->ta_records[type][20]);
	found = vos_ts_lookup(ts_arg->ta_ts_set, &ts_arg->ta_records[type][20],
			      true, &entry);
	assert_false(found);
	entry = vos_ts_alloc(ts_arg->ta_ts_set, &ts_arg->ta_records[type][20],
			     20);
	assert_int_equal(entry->te_info->ti_type, type);
	assert_ptr_equal(entry, same);

	for (idx = 0; idx <= NUM_EXTRA; idx++) {
		if (idx == (NUM_EXTRA - 2))
			continue;
		vos_ts_set_reset(ts_arg->ta_ts_set, type, 0);
		entry = vos_ts_alloc(ts_arg->ta_ts_set,
				     &ts_arg->ta_records[type][idx], idx);
		assert_non_null(entry);
	}

	/** Final check...all of them should exist */
	for (idx = 0; idx < ts_arg->ta_counts[type]; idx++) {
		found = vos_ts_lookup(ts_arg->ta_ts_set,
				      &ts_arg->ta_records[type][idx], true,
				      &entry);
		assert_true(found);
		assert_non_null(entry);
	}
}

static void
ilog_test_ts_get(void **state)
{
	struct vos_ts_entry	*entry;
	struct ts_test_arg	*ts_arg = *state;
	uint32_t		 type;
	bool			 found;

	for (type = 0; type < VOS_TS_TYPE_COUNT; type++) {
		if (type & 1)
			run_negative_entry_test(ts_arg, type);
		else
			run_positive_entry_test(ts_arg, type);
	}

	for (type = VOS_TS_TYPE_AKEY;; type -= 2) {
		vos_ts_evict(&ts_arg->ta_records[type][0]);
		found = vos_ts_lookup(ts_arg->ta_ts_set,
				      &ts_arg->ta_records[type][0], true,
				      &entry);
		assert_false(found);

		if (type == VOS_TS_TYPE_CONT)
			break;
	}
}

static int
alloc_ts_cache(void **state)
{
	struct vos_ts_table	*ts_table;
	struct ts_test_arg	*ts_arg;
	int			 rc;

	/** Free already allocated table */
	ts_table = vos_ts_table_get();
	if (ts_table != NULL)
		vos_ts_table_free(&ts_table);

	rc = vos_ts_table_alloc(&ts_table);
	if (rc != 0) {
		print_message("Can't allocate timestamp table: "DF_RC"\n",
			      DP_RC(rc));
		return 1;
	}

	vos_ts_table_set(ts_table);
	/* No need to have a free function here because vos_tests call
	 * vos_fini which will free the table.
	 */
	ts_arg = *state;
	vos_ts_set_reset(ts_arg->ta_ts_set, 0, 0);

	return 0;
}

static const struct CMUnitTest ts_tests[] = {
	{ "VOS600.1: VOS timestamp allocation test", ilog_test_ts_get,
		alloc_ts_cache, NULL},
};

static int
ts_test_init(void **state)
{
	int			 i;
	struct vos_ts_table	*ts_table;
	struct ts_test_arg	*ts_arg;
	uint32_t		*cursor;
	int			 rc;

	D_ALLOC_PTR(ts_arg);
	if (ts_arg == NULL)
		return 1;

	*state = ts_arg;

	alloc_ts_cache(state);

	ts_table = vos_ts_table_get();

	cursor = &ts_arg->ta_real_records[0];
	for (i = 0; i < VOS_TS_TYPE_COUNT; i++) {
		ts_arg->ta_counts[i] = ts_table->tt_type_info[i].ti_count;
		ts_arg->ta_records[i] = cursor;
		cursor += ts_arg->ta_counts[i];
	}

	rc = vos_ts_set_allocate(&ts_arg->ta_ts_set, VOS_OF_USE_TIMESTAMPS, 1);
	if (rc != 0) {
		D_FREE(ts_arg);
		return rc;
	}

	return 0;
}

static int
ts_test_fini(void **state)
{
	struct ts_test_arg	*ts_arg = *state;

	vos_ts_set_free(ts_arg->ta_ts_set);
	D_FREE(ts_arg);

	return 0;
}

int
run_ts_tests(void)
{
	return cmocka_run_group_tests_name("VOS Timestamp table tests",
					   ts_tests, ts_test_init,
					   ts_test_fini);
}
