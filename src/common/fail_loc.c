/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * common/fail_loc.c to inject failure scenario
 */
#define D_LOGFAC	DD_FAC(common)

#include <daos/common.h>
#include <gurt/fault_inject.h>

uint64_t daos_fail_loc;
uint64_t daos_fail_value;
uint64_t daos_fail_num;

void
daos_fail_loc_reset()
{
	daos_fail_loc_set(0);
	D_DEBUG(DB_ANY, "*** fail_loc="DF_X64"\n", daos_fail_loc);
}

int
daos_fail_check(uint64_t fail_loc)
{
	struct d_fault_attr_t	*attr = NULL;
	int			grp = 0;

	if ((daos_fail_loc == 0 ||
	    (daos_fail_loc & DAOS_FAIL_MASK_LOC) !=
	     (fail_loc & DAOS_FAIL_MASK_LOC)) &&
	     !d_fault_inject_is_enabled())
		return 0;

	/**
	 * TODO reset fail_loc to save some cycles once the
	 * current fail_loc finish the job.
	 */
	/* Search the attr in inject yml first */
	if (d_fault_inject_is_enabled()) {
		uint32_t id = DAOS_FAIL_ID_GET(fail_loc);

		attr = d_fault_attr_lookup(id);
	}

	if (attr == NULL) {
		grp = DAOS_FAIL_GROUP_GET(fail_loc);
		attr = d_fault_attr_lookup(grp);
	}

	if (attr == NULL) {
		D_DEBUG(DB_ANY, "No attr fail_loc="DF_X64" value="DF_U64
			", input_loc ="DF_X64" idx %d\n", daos_fail_loc,
			daos_fail_value, fail_loc, grp);
		return 0;
	}

	if (d_should_fail(attr)) {
		D_DEBUG(DB_ANY, "*** fail_loc="DF_X64" value="DF_U64
			", input_loc ="DF_X64" idx %d\n", daos_fail_loc,
			daos_fail_value, fail_loc, grp);
		return 1;
	}

	return 0;
}

void
daos_fail_loc_set(uint64_t fail_loc)
{
	struct d_fault_attr_t	attr_in = { 0 };
	bool			attr_set = false;

	/* If fail_loc is 0, let's assume it will reset unit test fail loc */
	if (fail_loc == 0)
		attr_in.fa_id = DAOS_FAIL_UNIT_TEST_GROUP;
	else
		attr_in.fa_id = DAOS_FAIL_GROUP_GET(fail_loc);

	D_ASSERT(attr_in.fa_id > 0);
	D_ASSERT(attr_in.fa_id < DAOS_FAIL_MAX_GROUP);

	attr_in.fa_probability_x = 1;
	attr_in.fa_probability_y = 1;
	if (fail_loc & DAOS_FAIL_ONCE) {
		attr_in.fa_max_faults = 1;
		attr_set = true;
	} else if (fail_loc & DAOS_FAIL_SOME) {
		D_ASSERT(daos_fail_num > 0);
		attr_in.fa_max_faults = daos_fail_num;
		attr_set = true;
	} else if (fail_loc & DAOS_FAIL_ALWAYS) {
		attr_in.fa_max_faults = 0;
		attr_set = true;
	}

	if (attr_set)
		d_fault_attr_set(attr_in.fa_id, attr_in);

	daos_fail_loc = fail_loc;
	D_DEBUG(DB_ANY, "*** fail_loc="DF_X64"\n", daos_fail_loc);
}

/* set #nr shards as failure (nr within [1, 4]) */
uint64_t
daos_shard_fail_value(uint16_t *shards, int nr)
{
	int		i;
	uint64_t	fail_val = 0;

	if (nr == 0 || nr > 4) {
		D_ERROR("ignore nr %d, should within [1, 4].\n", nr);
		return fail_val;
	}
	for (i = 0; i < nr; i++)
		fail_val |= ((uint64_t)shards[i] << (16 * i));
	return fail_val;
}

bool
daos_shard_in_fail_value(uint16_t shard)
{
	int		i;
	uint64_t	mask = 0xFFFF;
	uint64_t	fail_val = daos_fail_value_get();

	for (i = 0; i < 4; i++) {
		if (shard == ((fail_val & (mask << (i * 16))) >> (i * 16)))
			return true;
	}

	return false;
}

void
daos_fail_num_set(uint64_t value)
{
	daos_fail_num = value;
}

void
daos_fail_value_set(uint64_t value)
{
	daos_fail_value = value;
}

uint64_t
daos_fail_value_get(void)
{
	return daos_fail_value;
}

int
daos_fail_init(void)
{
	struct d_fault_attr_t	attr = { 0 };
	int rc;

	rc = d_fault_inject_init();
	if (rc != 0 && rc != -DER_NOSYS)
		return rc;

	rc = d_fault_attr_set(DAOS_FAIL_UNIT_TEST_GROUP, attr);
	if (rc)
		d_fault_inject_fini();

	return rc;
}

void
daos_fail_fini(void)
{
	d_fault_inject_fini();
}
