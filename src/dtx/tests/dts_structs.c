/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * DTX-related struct validation
 */
#define D_LOGFAC DD_FAC(tests)

#include <stddef.h>
#include <stdbool.h>
#include <uuid/uuid.h>
#include <daos_types.h>
#include <daos/object.h>

#include "vts_io.h"

#define SET_STRUCT_COMMON(a, c)          memset((void *)(&(a)), c, sizeof(a))

#define RESET_STRUCT(a)                  SET_STRUCT_COMMON(a, 0)

#define SET_STRUCT(a)                    SET_STRUCT_COMMON(a, 0xff)

#define SET_FIELD(a, field)              memset((void *)(&a.field), 0xff, sizeof(a.field))

#define SET_BITFIELD(a, bitfield, width) a.bitfield = ((1 << (width)) - 1)

#define SET_BITFIELD_1(a, bitfield)      SET_BITFIELD(a, bitfield, 1)

/*
 * Make sure the `struct dtx_handle` is well packed and all necessry paddings
 * are explicit.
 */
static void
struct_dtx_handle(void **state)
{
	struct dtx_handle dummy;

	/* Zero the whole structure. */
	RESET_STRUCT(dummy);

	/* Fill up all existing fields with a pattern. */
	SET_FIELD(dummy, dth_dte);
	SET_FIELD(dummy, dth_xid);
	SET_FIELD(dummy, dth_ver);
	SET_FIELD(dummy, dth_refs);
	SET_FIELD(dummy, dth_mbs);
	SET_FIELD(dummy, dth_coh);
	SET_FIELD(dummy, dth_poh);
	SET_FIELD(dummy, dth_epoch);
	SET_FIELD(dummy, dth_epoch_bound);
	SET_FIELD(dummy, dth_leader_oid);

	SET_BITFIELD_1(dummy, dth_sync);
	SET_BITFIELD_1(dummy, dth_pinned);
	SET_BITFIELD_1(dummy, dth_cos_done);
	SET_BITFIELD_1(dummy, dth_solo);
	SET_BITFIELD_1(dummy, dth_drop_cmt);
	SET_BITFIELD_1(dummy, dth_modify_shared);
	SET_BITFIELD_1(dummy, dth_active);
	SET_BITFIELD_1(dummy, dth_touched_leader_oid);
	SET_BITFIELD_1(dummy, dth_local_tx_started);
	SET_BITFIELD_1(dummy, dth_shares_inited);
	SET_BITFIELD_1(dummy, dth_dist);
	SET_BITFIELD_1(dummy, dth_for_migration);
	SET_BITFIELD_1(dummy, dth_prepared);
	SET_BITFIELD_1(dummy, dth_aborted);
	SET_BITFIELD_1(dummy, dth_already);
	SET_BITFIELD_1(dummy, dth_need_validation);
	SET_BITFIELD_1(dummy, dth_ignore_uncommitted);
	SET_BITFIELD_1(dummy, dth_local);
	SET_BITFIELD_1(dummy, dth_local_complete);
	SET_BITFIELD(dummy, padding1, 13);

	SET_FIELD(dummy, dth_dti_cos_count);
	SET_FIELD(dummy, dth_dti_cos);
	SET_FIELD(dummy, dth_ent);
	SET_FIELD(dummy, dth_flags);
	SET_FIELD(dummy, dth_rsrvd_cnt);
	SET_FIELD(dummy, dth_deferred_cnt);
	SET_FIELD(dummy, dth_modification_cnt);
	SET_FIELD(dummy, dth_op_seq);
	SET_FIELD(dummy, dth_deferred_used_cnt);
	SET_FIELD(dummy, padding2);
	SET_FIELD(dummy, dth_oid_cnt);
	SET_FIELD(dummy, dth_oid_cap);
	SET_FIELD(dummy, padding3);
	SET_FIELD(dummy, dth_oid_array);
	SET_FIELD(dummy, dth_local_oid_cnt);
	SET_FIELD(dummy, dth_local_oid_cap);
	SET_FIELD(dummy, padding4);
	SET_FIELD(dummy, dth_local_oid_array);
	SET_FIELD(dummy, dth_dkey_hash);
	SET_FIELD(dummy, dth_rsrvd_inline);
	SET_FIELD(dummy, dth_rsrvds);
	SET_FIELD(dummy, dth_deferred);
	SET_FIELD(dummy, dth_local_stub);
	SET_FIELD(dummy, dth_deferred_nvme);
	SET_FIELD(dummy, dth_share_cmt_list);
	SET_FIELD(dummy, dth_share_abt_list);
	SET_FIELD(dummy, dth_share_act_list);
	SET_FIELD(dummy, dth_share_tbd_list);
	SET_FIELD(dummy, dth_share_tbd_count);
	SET_FIELD(dummy, padding5);

	/* Set a whole structure for comparison. */
	struct dtx_handle dummy_set;
	SET_STRUCT(dummy_set);

	/* Detect unset parts of the structure. */
	assert_memory_equal(&dummy, &dummy_set, sizeof(dummy));

	/*
	 * Note: The following expression allows inspecting potential "holes" in the structure
	 * definition. Hope you find it useful. Best of luck.
	 *
	 * char (*test)[offsetof(struct dtx_handle, padding5)] = 1;
	 *
	 * Will produce an error on compile time e.g.
	 * initialization of ‘char (*)[284]’ from ‘int’ makes pointer from integer without a cast
	 */
}

static const struct CMUnitTest structs_tests_all[] = {
    {"DTX300: struct dtx_handle checks", struct_dtx_handle, NULL, NULL},
};

int
run_structs_tests(const char *cfg)
{
	const char *test_name = "DTX structs checks";

	return cmocka_run_group_tests_name(test_name, structs_tests_all, NULL, NULL);
}
