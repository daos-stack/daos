/**
 * (C) Copyright 2020-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

#define VOS_TS_SIZE (8 * 1024 * 1024)

#define NUM_EXTRA	4
struct ts_test_arg {
	uint32_t		 ta_records[VOS_TS_TYPE_COUNT][VOS_TS_SIZE];
	void			*old_table;
	struct vos_ts_set	*ta_ts_set;
	uint32_t		 ta_counts[VOS_TS_TYPE_COUNT];
	uint32_t		 ta_extra_records[NUM_EXTRA];
};

static void
run_negative_entry_test(struct ts_test_arg *ts_arg, uint32_t type)
{
	struct vos_ts_entry	*entry;
	uint32_t		*idx_ptr;
	uint32_t		 idx;
	bool			 found;
	bool			 reset;

	if (type == 0) {
		vos_ts_set_reset(ts_arg->ta_ts_set, type, 0);
		entry = vos_ts_alloc(ts_arg->ta_ts_set,
				     &ts_arg->ta_records[type][0], 0);
		/** Entry should be alloca0ted */
		assert_non_null(entry);
		assert_int_equal(entry->te_info->ti_type, type);
		return;
	}

	reset = false;
	for (idx = 0; idx <= VOS_TS_SIZE; idx++) {
		entry = vos_ts_get_negative(ts_arg->ta_ts_set, idx,
					    reset);
		reset = true;
		assert_non_null(entry);
	}

	for (idx = 0; idx < ts_arg->ta_counts[type];
	     idx++) {
		idx_ptr = &ts_arg->ta_records[type][idx];
		found = vos_ts_lookup(ts_arg->ta_ts_set, idx_ptr, false,
				      &entry);
		assert_false(found);
		assert_null(entry);
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
			vos_ts_set_reset(ts_arg->ta_ts_set, type - 1, 0);
			/** ignore the entries that were evicted */
			parent_idx = idx / children_per_parent + NUM_EXTRA + 1;
			idx_ptr = &ts_arg->ta_records[type - 1][parent_idx];
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
	assert_int_equal(ts_arg->ta_ts_set->ts_init_count, 1 + type);

	/** Lookup an entry entry */
	found = vos_ts_lookup(ts_arg->ta_ts_set,
			      &ts_arg->ta_records[type][NUM_EXTRA - 2], true,
			      &entry);
	assert_true(found);
	assert_non_null(entry);

	assert_int_equal(ts_arg->ta_ts_set->ts_init_count, 1 + type);
	/** Now evict a few entries */
	for (idx = 0; idx < NUM_EXTRA; idx++) {
		vos_ts_set_reset(ts_arg->ta_ts_set, type, 0);
		entry = vos_ts_alloc(ts_arg->ta_ts_set,
				     &ts_arg->ta_extra_records[idx], idx);
		assert_non_null(entry);
		assert_int_equal(entry->te_info->ti_type, type);
	}
	assert_int_equal(ts_arg->ta_ts_set->ts_init_count, 1 + type);

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
		vos_ts_evict(&ts_arg->ta_extra_records[idx], type, true);

	/** evicting an entry should move it to lru */
	vos_ts_set_reset(ts_arg->ta_ts_set, type, 0);
	found = vos_ts_lookup(ts_arg->ta_ts_set, &ts_arg->ta_records[type][20],
			      false, &same);
	assert_true(found);
	assert_int_equal(same->te_info->ti_type, type);
	vos_ts_evict(&ts_arg->ta_records[type][20], type, true);
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
	uint32_t		 idx;
	bool			 found;

	for (type = 0; type < VOS_TS_TYPE_COUNT; type++)
		run_positive_entry_test(ts_arg, type);

	for (type = VOS_TS_TYPE_AKEY;; type--) {
		for (idx = 0; idx < ts_arg->ta_counts[type]; idx++) {
			vos_ts_evict(&ts_arg->ta_records[type][idx], type, true);
			found = vos_ts_lookup(ts_arg->ta_ts_set,
					      &ts_arg->ta_records[type][idx],
					      true, &entry);
			assert_false(found);
		}

		if (type == VOS_TS_TYPE_CONT)
			break;
	}

	for (type = 0; type < VOS_TS_TYPE_COUNT; type++)
		run_negative_entry_test(ts_arg, type);

	for (type = VOS_TS_TYPE_AKEY;; type--) {
		for (idx = 0; idx < ts_arg->ta_counts[type]; idx++) {
			found = vos_ts_lookup(ts_arg->ta_ts_set,
					      &ts_arg->ta_records[type][idx],
					      true, &entry);
			assert_false(found);
		}

		if (type == VOS_TS_TYPE_CONT)
			break;
	}

}

static int
alloc_ts_cache(void **state)
{
	struct vos_ts_table	*ts_table;
	struct ts_test_arg	*ts_arg = *state;
	int			 rc;

	/** Free already allocated table */
	ts_table = vos_ts_table_get(true);
	if (ts_table != NULL)
		ts_arg->old_table = ts_table;

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

struct index_record {
	uint32_t idx;
	uint32_t value;
};

#define LRU_ARRAY_SIZE	32
#define LRU_ARRAY_NR	4
#define NUM_INDEXES	128
struct lru_arg {
	struct lru_array	*array;
	struct index_record	 indexes[NUM_INDEXES];
	bool			 lookup;
};

struct lru_record {
	uint64_t		 magic1;
	struct index_record	*record;
	uint32_t		 idx;
	uint32_t		 custom;
	uint64_t		 magic2;
};

#define MAGIC1	0xdeadbeef
#define MAGIC2	0xbaadf00d

static void
on_entry_evict(void *payload, uint32_t idx, void *arg)
{
	struct lru_record	*read_record;
	struct lru_record	*record = payload;
	struct lru_arg		*ts_arg = arg;
	bool			 found;

	if (ts_arg->lookup) {
		found = lrua_lookup(ts_arg->array, &record->record->idx,
				    &read_record);
		assert_true(found);
		assert_non_null(read_record);
		assert_true(read_record == payload);
	}

	record->record->value = MAGIC1;
	record->record = NULL;
}

static void
on_entry_init(void *payload, uint32_t idx, void *arg)
{
	struct lru_record	*record = payload;

	record->idx = idx;
	record->magic1 = MAGIC1;
	record->magic2 = MAGIC2;
}

static void
on_entry_fini(void *payload, uint32_t idx, void *arg)
{
	struct lru_record	*record = payload;

	if (record->record)
		record->record->value = MAGIC1;
}

static const struct lru_callbacks lru_cbs = {
	.lru_on_evict	= on_entry_evict,
	.lru_on_init	= on_entry_init,
	.lru_on_fini	= on_entry_fini,
};

static void
lru_array_test(void **state)
{
	struct lru_arg		*ts_arg = *state;
	struct lru_record	*entry;
	int			 i;
	bool			 found;
	int			 lru_idx;
	int			 rc;


	for (i = 0; i < NUM_INDEXES; i++) {
		found = lrua_lookup(ts_arg->array, &ts_arg->indexes[i].idx,
				    &entry);
		assert_false(found);
	}

	for (i = 0; i < NUM_INDEXES; i++) {
		rc = lrua_alloc(ts_arg->array, &ts_arg->indexes[i].idx, &entry);
		assert_rc_equal(rc, 0);
		assert_non_null(entry);

		entry->record = &ts_arg->indexes[i];
		ts_arg->indexes[i].value = i;
	}

	for (i = NUM_INDEXES - 1; i >= 0; i--) {
		found = lrua_lookup(ts_arg->array, &ts_arg->indexes[i].idx,
				    &entry);
		if (found) {
			assert_true(i >= (NUM_INDEXES - LRU_ARRAY_SIZE));
			assert_non_null(entry);
			assert_true(entry->magic1 == MAGIC1);
			assert_true(entry->magic2 == MAGIC2);
			assert_true(i == ts_arg->indexes[i].value);
			assert_true(entry->idx == ts_arg->indexes[i].idx);
		} else {
			assert_false(i >= (NUM_INDEXES - LRU_ARRAY_SIZE));
			assert_null(entry);
			assert_true(ts_arg->indexes[i].value == 0xdeadbeef);
		}
	}

	lru_idx = NUM_INDEXES - 3;
	found = lrua_lookup(ts_arg->array,
			    &ts_arg->indexes[lru_idx].idx, &entry);
	assert_true(found);
	assert_non_null(entry);
	assert_true(entry->record->value == lru_idx);

	/* cache all but one new entry */
	for (i = 0; i <  LRU_ARRAY_SIZE - 1; i++) {
		found = lrua_lookup(ts_arg->array, &ts_arg->indexes[i].idx,
				    &entry);
		assert_false(found);
		rc = lrua_alloc(ts_arg->array, &ts_arg->indexes[i].idx, &entry);
		assert_rc_equal(rc, 0);
		assert_non_null(entry);

		entry->record = &ts_arg->indexes[i];
		ts_arg->indexes[i].value = i;

		found = lrua_lookup(ts_arg->array, &ts_arg->indexes[i].idx,
				    (void *)&entry);
		assert_non_null(entry);
		assert_true(entry->magic1 == MAGIC1);
		assert_true(entry->magic2 == MAGIC2);
		assert_true(i == ts_arg->indexes[i].value);
		assert_true(entry->idx == ts_arg->indexes[i].idx);
	}

	/** lru_idx should still be there */
	found = lrua_lookup(ts_arg->array,
			    &ts_arg->indexes[lru_idx].idx, (void *)&entry);
	assert_true(found);
	assert_non_null(entry);
	assert_true(entry->record->value == lru_idx);

	lrua_evict(ts_arg->array, &ts_arg->indexes[lru_idx].idx);

	found = lrua_lookup(ts_arg->array,
			    &ts_arg->indexes[lru_idx].idx, (void *)&entry);
	assert_false(found);
}

#define STRESS_ITER 500
#define BIG_TEST 50000
static void
lru_array_stress_test(void **state)
{
	struct lru_arg		*ts_arg = *state;
	struct lru_record	*entry;
	struct index_record	*stress_entries;
	int			 evicted, inserted;
	int			 i, op, j;
	bool			 found;
	int			 freq_map[] = {2, 3, 7, 13, 17};
	int			 freq_idx;
	int			 freq_idx2;
	int			 freq;
	int			 rc;

	D_ALLOC_ARRAY(stress_entries, BIG_TEST);
	assert_non_null(stress_entries);

	for (j = 0; j < STRESS_ITER * ARRAY_SIZE(freq_map); j++) {
		freq_idx = j % ARRAY_SIZE(freq_map);
		/** First evict all */
		for (i = 0; i < NUM_INDEXES; i++) {
			found = lrua_lookup(ts_arg->array,
					    &ts_arg->indexes[i].idx,
					    &entry);
			assert_false(found);
		}
		/** Now insert most */
		for (i = 0; i < NUM_INDEXES; i++) {
			if ((i % freq_map[freq_idx]) == 0)
				continue;
			rc = lrua_alloc(ts_arg->array, &stress_entries[i].idx,
					&entry);
			assert_rc_equal(rc, 0);
			assert_non_null(entry);
			entry->record = &ts_arg->indexes[i];
			ts_arg->indexes[i].value = i;
		}
		freq_idx2 = (freq_idx + 1) % ARRAY_SIZE(freq_map);
		freq = freq_map[freq_idx] * freq_map[freq_idx2];
		/** Now evict some of them */
		evicted = 0;
		for (i = NUM_INDEXES - 1; i >= 0; i--) {
			if ((i % freq) == 0)
				continue;
			found = lrua_lookup(ts_arg->array,
					    &stress_entries[i].idx, &entry);
			if (!found)
				continue;

			assert_non_null(entry);
			assert_true(entry->magic1 == MAGIC1);
			assert_true(entry->magic2 == MAGIC2);

			evicted++;

			lrua_evict(ts_arg->array, &ts_arg->indexes[i].idx);
		}

		/** The array is full at start of loop so there will be
		 *  LRU_ARRAY_SIZE entries and we evict all of them
		 */
		assert_int_equal(evicted, LRU_ARRAY_SIZE);
	}

	srand(1); /* The below test suite may fail for random seed value*/
	for (i = 0; i < BIG_TEST; i++) {
		stress_entries[i].value = MAGIC1;
		op = rand() % 10;

		if (op < 7) {
			rc = lrua_alloc(ts_arg->array, &stress_entries[i].idx,
					&entry);
			assert_rc_equal(rc, 0);
			assert_non_null(entry);

			entry->record = &stress_entries[i];
			stress_entries[i].value = i;
		} else {
			op = rand() % (i + 1);
			for (j = 0; j < i; j++) {
				if (stress_entries[op].value != MAGIC1)
					break;
				op = (op + 1) % (i + 1);
			}

			if (stress_entries[op].value != MAGIC1) {
				lrua_evict(ts_arg->array,
					   &stress_entries[op].idx);
				assert_true(stress_entries[op].value == MAGIC1);
			}
		}
	}

	inserted = 0;
	for (i = 0; i < BIG_TEST; i++) {
		if (stress_entries[i].value == MAGIC1)
			continue;

		inserted++;
		found = lrua_lookup(ts_arg->array,
				    &stress_entries[i].idx, &entry);
		assert_true(found);
		assert_non_null(entry);
		assert_true(entry->magic1 == MAGIC1);
		assert_true(entry->magic2 == MAGIC2);
		assert_true(entry->record == &stress_entries[i]);
		assert_true(stress_entries[i].value == i);

		lrua_evict(ts_arg->array, &stress_entries[i].idx);
	}

	assert_int_equal(inserted, LRU_ARRAY_SIZE);

	for (i = 0; i < LRU_ARRAY_SIZE; i++) {
		rc = lrua_alloc(ts_arg->array, &stress_entries[i].idx, &entry);
		assert_rc_equal(rc, 0);
		assert_non_null(entry);
		entry->record = &stress_entries[i];
		stress_entries[i].value = i;
	}

	/** Cause evict to lookup the entry to trigger DAOS-4548 */
	ts_arg->lookup = true;
	for (i = 0; i < LRU_ARRAY_SIZE; i++) {
		j = i + LRU_ARRAY_SIZE;
		rc = lrua_alloc(ts_arg->array, &stress_entries[j].idx, &entry);
		assert_rc_equal(rc, 0);
		assert_non_null(entry);
		entry->record = &stress_entries[j];
		stress_entries[j].value = j;
	}

	for (i = LRU_ARRAY_SIZE - 1; i >= 0; i--) {
		j = i +  2 * LRU_ARRAY_SIZE;
		rc = lrua_alloc(ts_arg->array, &stress_entries[j].idx, &entry);
		assert_rc_equal(rc, 0);
		assert_non_null(entry);
		entry->record = &stress_entries[j];
		stress_entries[j].value = j;
	}

	for (i = 0; i < LRU_ARRAY_SIZE; i++) {
		j = i + 2 * LRU_ARRAY_SIZE;
		lrua_evict(ts_arg->array, &stress_entries[j].idx);
		assert_true(stress_entries[j].value == MAGIC1);
	}

	ts_arg->lookup = false;

	D_FREE(stress_entries);
}

static void
lru_array_multi_test_iter(void **state)
{
	struct lru_arg		*ts_arg = *state;
	struct lru_record	*entry;
	int			 i;
	bool			 found;
	int			 rc;

	for (i = 0; i < NUM_INDEXES; i++) {
		found = lrua_lookup(ts_arg->array, &ts_arg->indexes[i].idx,
				    &entry);
		assert_false(found);
	}

	for (i = 0; i < NUM_INDEXES; i++) {
		rc = lrua_alloc(ts_arg->array, &ts_arg->indexes[i].idx, &entry);
		if (entry == NULL) {
			assert_true(i >= LRU_ARRAY_SIZE);
			lrua_evict(ts_arg->array,
				   &ts_arg->indexes[i - LRU_ARRAY_SIZE].idx);
			rc = lrua_alloc(ts_arg->array, &ts_arg->indexes[i].idx,
					&entry);
		}
		assert_rc_equal(rc, 0);
		assert_non_null(entry);
		entry->record = &ts_arg->indexes[i];
		ts_arg->indexes[i].value = i;
	}

	for (i = NUM_INDEXES - 1; i >= 0; i--) {
		found = lrua_lookup(ts_arg->array, &ts_arg->indexes[i].idx,
				    &entry);
		if (found) {
			assert_true(i >= (NUM_INDEXES - LRU_ARRAY_SIZE));
			assert_non_null(entry);
			assert_true(entry->magic1 == MAGIC1);
			assert_true(entry->magic2 == MAGIC2);
			assert_true(i == ts_arg->indexes[i].value);
			assert_true(entry->idx == ts_arg->indexes[i].idx);
		} else {
			assert_false(i >= (NUM_INDEXES - LRU_ARRAY_SIZE));
			assert_null(entry);
			assert_true(ts_arg->indexes[i].value == 0xdeadbeef);
		}

		/** Ok to evict entries not in the array */
		lrua_evict(ts_arg->array, &ts_arg->indexes[i].idx);
	}
}

static void
inplace_test(struct lru_arg *ts_arg, uint32_t idx, uint64_t key1, uint64_t key2)
{
	struct lru_record	*entry;
	bool			 found;
	int			 rc;

	rc = lrua_allocx_inplace(ts_arg->array, idx, key1,
				 &entry);
	assert_rc_equal(rc, 0);
	assert_non_null(entry);
	assert_true(entry->magic1 == MAGIC1);
	assert_true(entry->magic2 == MAGIC2);
	entry->magic1 = 10;
	entry->record = &ts_arg->indexes[0];
	entry = NULL;
	found = lrua_lookupx(ts_arg->array, idx + 1, key1, &entry);
	assert_false(found);
	found = lrua_lookupx(ts_arg->array, idx, key2, &entry);
	assert_false(found);
	found = lrua_lookupx(ts_arg->array, idx, key1, &entry);
	assert_true(found);
	assert_non_null(entry);
	assert_int_equal(entry->magic1, 10);
	entry->magic1 = MAGIC1;
	lrua_evictx(ts_arg->array, idx, key1);
	found = lrua_lookupx(ts_arg->array, idx, key1, &entry);
	assert_false(found);
}

static void
lru_array_multi_test(void **state)
{
	struct lru_arg		*ts_arg = *state;

	lru_array_multi_test_iter(state);
	lrua_array_aggregate(ts_arg->array);
	lru_array_multi_test_iter(state);
	lrua_array_aggregate(ts_arg->array);

	/** Try some inplace entries.   Some of these should require on-demand
	 * allocation
	 */
	inplace_test(ts_arg, LRU_ARRAY_SIZE - 2, 0xdeadbeef, 0xbaadf00d);
	inplace_test(ts_arg, 2, 0xbeefbaad, 0xf00dbaad);
	inplace_test(ts_arg, LRU_ARRAY_SIZE / 2, 0xbeef0000, 0x0000f00d);
	lrua_array_aggregate(ts_arg->array);
	lru_array_multi_test_iter(state);
}

static int
init_lru_test(void **state)
{
	struct lru_arg		*ts_arg;
	int			 rc;

	D_ALLOC_PTR(ts_arg);
	if (ts_arg == NULL)
		return 1;

	rc = lrua_array_alloc(&ts_arg->array, LRU_ARRAY_SIZE, 1,
			      sizeof(struct lru_record), 0, &lru_cbs,
			      ts_arg);

	*state = ts_arg;
	return rc;
}

static int
init_lru_multi_test(void **state)
{
	struct lru_arg		*ts_arg;
	int			 rc;

	D_ALLOC_PTR(ts_arg);
	if (ts_arg == NULL)
		return 1;

	rc = lrua_array_alloc(&ts_arg->array, LRU_ARRAY_SIZE, LRU_ARRAY_NR,
			      sizeof(struct lru_record), LRU_FLAG_REUSE_UNIQUE,
			      &lru_cbs, ts_arg);

	*state = ts_arg;
	return rc;
}

static int
finalize_lru_test(void **state)
{
	struct lru_arg		*ts_arg = *state;

	if (ts_arg == NULL)
		return 0;

	if (ts_arg->array == NULL)
		return 0;

	lrua_array_free(ts_arg->array);

	D_FREE(ts_arg);

	return 0;
}

static int
ts_test_init(void **state)
{
	int			 i;
	struct vos_ts_table	*ts_table;
	struct ts_test_arg	*ts_arg;
	int			 rc;
	struct dtx_handle	 dth = {0};

	D_ALLOC_PTR(ts_arg);
	if (ts_arg == NULL)
		return 1;

	*state = ts_arg;

	alloc_ts_cache(state);

	ts_table = vos_ts_table_get(true);

	for (i = 0; i < VOS_TS_TYPE_COUNT; i++)
		ts_arg->ta_counts[i] = ts_table->tt_type_info[i].ti_count;

	daos_dti_gen_unique(&dth.dth_xid);
	rc = vos_ts_set_allocate(&ts_arg->ta_ts_set, 0, 0, 1, &dth, true);
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
	struct vos_ts_table	*ts_table;

	vos_ts_set_free(ts_arg->ta_ts_set);
	ts_table = vos_ts_table_get(true);
	vos_ts_table_free(&ts_table);
	vos_ts_table_set(ts_arg->old_table);
	D_FREE(ts_arg);

	return 0;
}



static const struct CMUnitTest ts_tests[] = {
	{ "VOS600.1: LRU array test", lru_array_test, init_lru_test,
		finalize_lru_test},
	{ "VOS600.2: LRU array stress", lru_array_stress_test, init_lru_test,
		finalize_lru_test},
	{ "VOS600.3: LRU multi-level array", lru_array_multi_test,
		init_lru_multi_test, finalize_lru_test},
	{ "VOS600.4: VOS timestamp allocation test", ilog_test_ts_get,
		ts_test_init, ts_test_fini},
};

int
run_ts_tests(const char *cfg)
{
	char	suite[DTS_CFG_MAX];

	dts_create_config(suite, "Timestamp table tests %s", cfg);

	return cmocka_run_group_tests_name(suite, ts_tests, NULL, NULL);
}
