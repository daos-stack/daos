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
 * This file is part of daos
 *
 * tests/suite/daos_base_tx.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_test.h"
#include "daos_iotest.h"
#include <daos/dtx.h>

static int dts_dtx_class	= OC_RP_3G1;
static int dts_dtx_replica_cnt	= 3;
static int dts_dtx_iosize	= 64;
static const char *dts_dtx_dkey	= "dtx_io dkey";
static const char *dts_dtx_akey	= "dtx_io akey";

static void
dtx_set_fail_loc(test_arg_t *arg, uint64_t fail_loc)
{
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     fail_loc, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);
}

static void
dtx_check_replicas(const char *dkey, const char *akey, const char *msg,
		   const char *update_buf, daos_size_t size, struct ioreq *req)
{
	char	*fetch_buf = NULL;
	int	 i;

	if (size != 0) {
		assert_non_null(update_buf);
		D_ALLOC(fetch_buf, size);
		assert_non_null(fetch_buf);
	}

	/* Require to fetch from specified replica. */
	daos_fail_loc_set(DAOS_OBJ_SPECIAL_SHARD | DAOS_FAIL_ALWAYS);

	for (i = 0; i < dts_dtx_replica_cnt; i++) {
		memset(fetch_buf, 0, size);

		/* Fetch from the replica_i. */
		daos_fail_value_set(i);
		lookup_single(dkey, akey, 0, fetch_buf, size,
			      DAOS_TX_NONE, req);
		print_message("%s: rep %d, result %d, size %lu/%lu\n",
			      msg, i, req->result, size, req->iod[0].iod_size);

		assert_int_equal(req->iod[0].iod_size, size);
		if (fetch_buf != NULL)
			assert_memory_equal(update_buf, fetch_buf, size);
	}

	daos_fail_loc_set(0);

	if (fetch_buf != NULL)
		D_FREE(fetch_buf);
}

static void
dtx_io_test_succ(void **state, daos_iod_type_t iod_type)
{
	test_arg_t	*arg = *state;
	char		*update_buf;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	if (!test_runable(arg, dts_dtx_replica_cnt))
		return;

	D_ALLOC(update_buf, dts_dtx_iosize);
	assert_non_null(update_buf);
	dts_buf_render(update_buf, dts_dtx_iosize);

	oid = dts_oid_gen(dts_dtx_class, 0, arg->myrank);

	/* Synchronously commit the update. */
	arg->fail_loc = DAOS_DTX_COMMIT_SYNC | DAOS_FAIL_ALWAYS;
	arg->async = 0;
	ioreq_init(&req, arg->coh, oid, iod_type, arg);

	/** Insert */
	insert_single(dkey, akey, 0, update_buf, dts_dtx_iosize,
		      DAOS_TX_NONE, &req);
	dtx_check_replicas(dkey, akey, "update_succ", update_buf,
			   dts_dtx_iosize, &req);

	/* Synchronously commit the punch. */
	daos_fail_loc_set(DAOS_DTX_COMMIT_SYNC | DAOS_FAIL_ALWAYS);

	punch_dkey(dkey, DAOS_TX_NONE, &req);
	dtx_check_replicas(dkey, akey, "punch_succ", NULL, 0, &req);

	D_FREE(update_buf);
	ioreq_fini(&req);
}

static void
dtx_1(void **state)
{
	print_message("update/punch single value successfully\n");
	dtx_io_test_succ(state, DAOS_IOD_SINGLE);
}

static void
dtx_2(void **state)
{
	print_message("update/punch array value successfully\n");
	dtx_io_test_succ(state, DAOS_IOD_ARRAY);
}

static void
dtx_io_test_fail(void **state, uint64_t fail_loc)
{
	test_arg_t	*arg = *state;
	char		*update_buf1;
	char		*update_buf2;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	if (!test_runable(arg, dts_dtx_replica_cnt))
		return;

	D_ALLOC(update_buf1, dts_dtx_iosize);
	assert_non_null(update_buf1);
	dts_buf_render(update_buf1, dts_dtx_iosize);

	D_ALLOC(update_buf2, dts_dtx_iosize / 2);
	assert_non_null(update_buf2);
	dts_buf_render(update_buf2, dts_dtx_iosize / 2);

	oid = dts_oid_gen(dts_dtx_class, 0, arg->myrank);

	/* Synchronously commit the update_1. */
	arg->fail_loc = DAOS_DTX_COMMIT_SYNC | DAOS_FAIL_ALWAYS;
	arg->async = 0;
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	insert_single(dkey, akey, 0, update_buf1, dts_dtx_iosize,
		      DAOS_TX_NONE, &req);

	/* Fail the following update_2 on some replica(s). */
	dtx_set_fail_loc(arg, fail_loc);

	arg->expect_result = -DER_IO;
	insert_single(dkey, akey, 0, update_buf2, dts_dtx_iosize / 2,
		      DAOS_TX_NONE, &req);
	arg->expect_result = 0;
	dtx_check_replicas(dkey, akey, "update_fail", update_buf1,
			   dts_dtx_iosize, &req);

	arg->expect_result = -DER_IO;
	/* Fail the following punch on some replica(s). */
	punch_dkey(dkey, DAOS_TX_NONE, &req);
	arg->expect_result = 0;
	dtx_check_replicas(dkey, akey, "punch_fail", update_buf1,
			   dts_dtx_iosize, &req);

	dtx_set_fail_loc(arg, 0);

	D_FREE(update_buf1);
	D_FREE(update_buf2);
	ioreq_fini(&req);
}

static void
dtx_3(void **state)
{
	print_message("failed to update/punch on leader\n");
	dtx_io_test_fail(state, DAOS_DTX_LEADER_ERROR | DAOS_FAIL_ALWAYS);
}

static void
dtx_4(void **state)
{
	print_message("failed to update/punch on non-leader\n");
	dtx_io_test_fail(state, DAOS_DTX_NONLEADER_ERROR | DAOS_FAIL_ALWAYS);
}

static int
dtx_check_replicas_v2(const char *dkey, const char *akey, const char *msg,
		      const char *update_buf, daos_size_t size,
		      bool punch, struct ioreq *req)
{
	char	*fetch_buf = NULL;
	int	 count = 0;
	int	 i;

	assert_non_null(update_buf);
	D_ALLOC(fetch_buf, size);
	assert_non_null(fetch_buf);

	for (i = 0; i < dts_dtx_replica_cnt; i++) {
		memset(fetch_buf, 0, size);

		/* Fetch from the replica_i. */
		daos_fail_value_set(i);
		lookup_single(dkey, akey, 0, fetch_buf, size,
			      DAOS_TX_NONE, req);
		print_message("%s: rep %d, %d, size %lu/%lu\n",
			      msg, i, req->result, size, req->iod[0].iod_size);

		/* Leader replica will return the latest data. Non-leader may
		 * return -DER_INPROGRESS if not committed, or the latest data
		 * if related DTX has been committed asynchronously for retry.
		 */
		if (req->result == 0) {
			count++;
			assert_true(req->iod[0].iod_size == punch ? 0 : size);
			assert_memory_equal(update_buf, fetch_buf, size);
		} else {
			assert_int_equal(req->result, -DER_INPROGRESS);
		}
	}

	daos_fail_value_set(0);

	if (fetch_buf != NULL)
		D_FREE(fetch_buf);

	return count;
}

static void
dtx_fetch_committable(void **state, bool punch)
{
	test_arg_t	*arg = *state;
	char		*update_buf1;
	char		*zero_buf;
	char		*update_buf2;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 rc;

	if (!test_runable(arg, dts_dtx_replica_cnt))
		return;

	D_ALLOC(update_buf1, dts_dtx_iosize);
	assert_non_null(update_buf1);
	dts_buf_render(update_buf1, dts_dtx_iosize);

	D_ALLOC(update_buf2, dts_dtx_iosize / 2);
	assert_non_null(update_buf2);
	dts_buf_render(update_buf2, dts_dtx_iosize / 2);

	D_ALLOC(zero_buf, dts_dtx_iosize);
	assert_non_null(zero_buf);

	oid = dts_oid_gen(dts_dtx_class, 0, arg->myrank);

	/* Synchronously commit the 1st update. */
	arg->fail_loc = DAOS_DTX_COMMIT_SYNC | DAOS_FAIL_ALWAYS;
	arg->async = 0;
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	insert_single(dkey, akey, 0, update_buf1, dts_dtx_iosize,
		      DAOS_TX_NONE, &req);

	/* Asynchronously commit the 2nd modification. */
	daos_fail_loc_set(0);

	if (punch)
		punch_dkey(dkey, DAOS_TX_NONE, &req);
	else
		insert_single(dkey, akey, 0, update_buf2, dts_dtx_iosize / 2,
			      DAOS_TX_NONE, &req);

	/* Require to fetch from specified replica. */
	daos_fail_loc_set(DAOS_OBJ_SPECIAL_SHARD | DAOS_FAIL_ALWAYS);

	rc = dtx_check_replicas_v2(dkey, akey, "fetch_committable_1",
				   punch ? zero_buf : update_buf2,
				   dts_dtx_iosize / 2, punch, &req);
	/* At least leader will return the latest data. */
	assert_true(rc >= 1);

	/* Reset fail_loc, repeat fetch from any replica. If without specifying
	 * the replica, and if fetch from non-leader hits non-committed DTX, it
	 * will retry with leader, finially, the expected data will be returned
	 * from the leader replica.
	 */
	daos_fail_loc_set(0);

	rc = dtx_check_replicas_v2(dkey, akey, "fetch_committable_2",
				   punch ? zero_buf : update_buf2,
				   dts_dtx_iosize / 2, punch, &req);
	assert_int_equal(rc, dts_dtx_replica_cnt);

	D_FREE(zero_buf);
	D_FREE(update_buf1);
	D_FREE(update_buf2);
	ioreq_fini(&req);
}

static void
dtx_5(void **state)
{
	print_message("fetch with committable update\n");
	dtx_fetch_committable(state, false);
}

static void
dtx_6(void **state)
{
	print_message("fetch with committable punch\n");
	dtx_fetch_committable(state, true);
}

static void
dtx_modify_committable(void **state, bool committable_punch, bool sync_update)
{
	test_arg_t	*arg = *state;
	char		*update_buf1;
	char		*update_buf2;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	if (!test_runable(arg, dts_dtx_replica_cnt))
		return;

	D_ALLOC(update_buf1, dts_dtx_iosize);
	assert_non_null(update_buf1);
	dts_buf_render(update_buf1, dts_dtx_iosize);

	D_ALLOC(update_buf2, dts_dtx_iosize / 2);
	assert_non_null(update_buf2);
	dts_buf_render(update_buf2, dts_dtx_iosize / 2);

	oid = dts_oid_gen(dts_dtx_class, 0, arg->myrank);

	/* Synchronously commit the 1st update. */
	arg->fail_loc = DAOS_DTX_COMMIT_SYNC | DAOS_FAIL_ALWAYS;
	arg->async = 0;
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	insert_single(dkey, akey, 0, update_buf1, dts_dtx_iosize,
		      DAOS_TX_NONE, &req);

	/* Asynchronously commit the 2nd modification. */
	daos_fail_loc_set(0);

	if (committable_punch)
		punch_dkey(dkey, DAOS_TX_NONE, &req);
	else
		insert_single(dkey, akey, 0, update_buf2, dts_dtx_iosize / 2,
			      DAOS_TX_NONE, &req);

	/* Synchronously commit the 3rd modification. */
	daos_fail_loc_set(DAOS_DTX_COMMIT_SYNC | DAOS_FAIL_ALWAYS);

	if (sync_update) {
		dts_buf_render(update_buf1, dts_dtx_iosize / 4);
		insert_single(dkey, akey, 0, update_buf1, dts_dtx_iosize / 4,
			      DAOS_TX_NONE, &req);
		dtx_check_replicas(dkey, akey, "update_committable",
				   update_buf1, dts_dtx_iosize / 4, &req);
	} else {
		punch_dkey(dkey, DAOS_TX_NONE, &req);
		dtx_check_replicas(dkey, akey, "punch_committable", NULL,
				   0, &req);
	}

	D_FREE(update_buf1);
	D_FREE(update_buf2);
	ioreq_fini(&req);
}

static void
dtx_7(void **state)
{
	print_message("update with committable update DTX\n");
	dtx_modify_committable(state, false, true);
}

static void
dtx_8(void **state)
{
	print_message("punch with committable update DTX\n");
	dtx_modify_committable(state, false, false);
}

static void
dtx_9(void **state)
{
	print_message("update with committable punch DTX\n");
	dtx_modify_committable(state, true, true);
}

static void
dtx_batched_commit(void **state, int count)
{
	test_arg_t	*arg = *state;
	char		*update_buf;
	char		*ptr;
	const char	*dkey = dts_dtx_dkey;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 size = count * 8;
	int		 rc;
	int		 i;

	if (!test_runable(arg, dts_dtx_replica_cnt))
		return;

	D_ALLOC(update_buf, size);
	assert_non_null(update_buf);

	oid = dts_oid_gen(dts_dtx_class, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	for (i = 0; i < count; i++) {
		ptr = &update_buf[i * 8];
		dts_buf_render(ptr, 8);

		insert_single(dkey, ptr, 0, ptr, 8, DAOS_TX_NONE, &req);
	}

	if (count < DTX_THRESHOLD_COUNT) {
		print_message("Sleep %d seconds for DTX async batched commit\n",
			      DTX_COMMIT_THRESHOLD_AGE + 3);
		sleep(DTX_COMMIT_THRESHOLD_AGE + 3);
	}

	/* The beginning DTX_THRESHOLD_COUNT DTXs should has been committed.
	 * So fetching from any replica should get the same. Let's try some.
	 */
	for (i = 0; i < DTX_THRESHOLD_COUNT && i < count; i += 30) {
		ptr = &update_buf[i * 8];
		rc = dtx_check_replicas_v2(dkey, ptr, "batched_commit",
					   ptr, 8, false, &req);
		assert_int_equal(rc, dts_dtx_replica_cnt);
	}

	D_FREE(update_buf);
	ioreq_fini(&req);
}

static void
dtx_10(void **state)
{
	print_message("DTX batched commit with over conut threshold\n");
	dtx_batched_commit(state, DTX_THRESHOLD_COUNT + 8);
}

static void
dtx_11(void **state)
{
	print_message("DTX batched commit with over time threshold\n");
	dtx_batched_commit(state, DTX_THRESHOLD_COUNT / 8);
}

static void
dtx_handle_resend(void **state, uint64_t fail_loc, uint16_t oclass)
{
	test_arg_t	*arg = *state;
	char		*update_buf;
	char		*fetch_buf;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	D_ALLOC(update_buf, dts_dtx_iosize);
	assert_non_null(update_buf);
	dts_buf_render(update_buf, dts_dtx_iosize);

	D_ALLOC(fetch_buf, dts_dtx_iosize);
	assert_non_null(fetch_buf);

	oid = dts_oid_gen(oclass, 0, arg->myrank);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	dtx_set_fail_loc(arg, fail_loc);

	/* verify update resend */
	insert_single(dkey, akey, 0, update_buf, dts_dtx_iosize,
		      DAOS_TX_NONE, &req);

	lookup_single(dkey, akey, 0, fetch_buf, dts_dtx_iosize, DAOS_TX_NONE,
		      &req);
	assert_int_equal(req.iod[0].iod_size, dts_dtx_iosize);
	assert_memory_equal(update_buf, fetch_buf, dts_dtx_iosize);

	/* verify punch resend */
	punch_dkey(dkey, DAOS_TX_NONE, &req);

	lookup_single(dkey, akey, 0, fetch_buf, dts_dtx_iosize, DAOS_TX_NONE,
		      &req);
	assert_int_equal(req.iod[0].iod_size, 0);

	dtx_set_fail_loc(arg, 0);

	D_FREE(update_buf);
	D_FREE(fetch_buf);
	ioreq_fini(&req);
}

static void
dtx_12(void **state)
{
	print_message("Resend with lost single replicated obj request\n");
	dtx_handle_resend(state, DAOS_DTX_LOST_RPC_REQUEST | DAOS_FAIL_ALWAYS,
			  OC_S1);
}

static void
dtx_13(void **state)
{
	print_message("Resend with lost single replicated obj reply\n");
	dtx_handle_resend(state, DAOS_DTX_LOST_RPC_REPLY | DAOS_FAIL_ALWAYS,
			  OC_S1);
}

static void
dtx_14(void **state)
{
	test_arg_t	*arg = *state;

	print_message("Resend with lost multiple replicated obj request\n");

	if (!test_runable(arg, dts_dtx_replica_cnt))
		return;

	dtx_handle_resend(state, DAOS_DTX_LOST_RPC_REQUEST | DAOS_FAIL_ALWAYS,
			  dts_dtx_class);
}

static void
dtx_15(void **state)
{
	test_arg_t	*arg = *state;

	print_message("Resend with lost multiple replicated obj reply\n");

	if (!test_runable(arg, dts_dtx_replica_cnt))
		return;

	dtx_handle_resend(state, DAOS_DTX_LOST_RPC_REPLY | DAOS_FAIL_ALWAYS,
			  dts_dtx_class);
}

static void
dtx_16(void **state)
{
	test_arg_t	*arg = *state;
	char		*update_buf;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	print_message("Resend after DTX aggregation\n");

	if (!test_runable(arg, dts_dtx_replica_cnt))
		return;

	D_ALLOC(update_buf, dts_dtx_iosize);
	assert_non_null(update_buf);
	dts_buf_render(update_buf, dts_dtx_iosize);

	oid = dts_oid_gen(dts_dtx_class, 0, arg->myrank);

	/* Synchronously commit the modification. */
	arg->fail_loc = DAOS_DTX_COMMIT_SYNC | DAOS_FAIL_ALWAYS;
	arg->async = 0;
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	insert_single(dkey, akey, 0, update_buf, dts_dtx_iosize,
		      DAOS_TX_NONE, &req);

	dtx_set_fail_loc(arg, DAOS_DTX_LONG_TIME_RESEND);

	arg->expect_result = -DER_EP_OLD;
	punch_akey(dkey, akey, DAOS_TX_NONE, &req);
	arg->expect_result = 0;

	dtx_set_fail_loc(arg, 0);

	D_FREE(update_buf);
	ioreq_fini(&req);
}

static void
dtx_17(void **state)
{
	test_arg_t	*arg = *state;
	char		*update_buf;
	char		*fetch_buf;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey1 = "akey-1";
	const char	*akey2 = "akey-2";
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	print_message("DTX resync during open-close\n");

	if (!test_runable(arg, dts_dtx_replica_cnt))
		return;

	D_ALLOC(update_buf, dts_dtx_iosize);
	assert_non_null(update_buf);
	dts_buf_render(update_buf, dts_dtx_iosize);

	D_ALLOC(fetch_buf, dts_dtx_iosize);
	assert_non_null(fetch_buf);

	oid = dts_oid_gen(dts_dtx_class, 0, arg->myrank);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	insert_single(dkey, akey1, 0, update_buf, dts_dtx_iosize,
		      DAOS_TX_NONE, &req);
	insert_single(dkey, akey2, 0, update_buf, dts_dtx_iosize,
		      DAOS_TX_NONE, &req);
	punch_akey(dkey, akey1, DAOS_TX_NONE, &req);

	MPI_Barrier(MPI_COMM_WORLD);
	close_reopen_coh_oh(arg, &req, oid);

	lookup_single(dkey, akey1, 0, fetch_buf, dts_dtx_iosize, DAOS_TX_NONE,
		      &req);
	assert_int_equal(req.iod[0].iod_size, 0);

	lookup_single(dkey, akey2, 0, fetch_buf, dts_dtx_iosize, DAOS_TX_NONE,
		      &req);
	assert_int_equal(req.iod[0].iod_size, dts_dtx_iosize);
	assert_memory_equal(update_buf, fetch_buf, dts_dtx_iosize);

	D_FREE(update_buf);
	D_FREE(fetch_buf);
	ioreq_fini(&req);
}

static const struct CMUnitTest dtx_tests[] = {
	{"DTX1: update/punch single value with DTX successfully",
	 dtx_1, NULL, test_case_teardown},
	{"DTX2: update/punch array value with DTX successfully",
	 dtx_2, NULL, test_case_teardown},
	{"DTX3: update/punch with DTX failed on leader",
	 dtx_3, NULL, test_case_teardown},
	{"DTX4: update/punch with DTX failed on non-leader",
	 dtx_4, NULL, test_case_teardown},
	{"DTX5: fetch with non-committed update DTX",
	 dtx_5, NULL, test_case_teardown},
	{"DTX6: fetch with non-committed punch DTX",
	 dtx_6, NULL, test_case_teardown},
	{"DTX7: update with committable update DTX ",
	 dtx_7, NULL, test_case_teardown},
	{"DTX8: punch with committable update DTX ",
	 dtx_8, NULL, test_case_teardown},
	{"DTX9: update with committable punch DTX ",
	 dtx_9, NULL, test_case_teardown},
	{"DTX10: DTX batched commit with over conut threshold",
	 dtx_10, NULL, test_case_teardown},
	{"DTX11: DTX batched commit with over time threshold",
	 dtx_11, NULL, test_case_teardown},
	{"DTX12: Resend with lost single replicated obj request",
	 dtx_12, NULL, test_case_teardown},
	{"DTX13: Resend with lost single replicated obj reply",
	 dtx_13, NULL, test_case_teardown},
	{"DTX14: Resend with lost multiple replicated obj request",
	 dtx_14, NULL, test_case_teardown},
	{"DTX15: Resend with lost multiple replicated obj reply",
	 dtx_15, NULL, test_case_teardown},
	{"DTX16: Resend after DTX aggregation",
	 dtx_16, NULL, test_case_teardown},
	{"DTX17: DTX resync during open-close",
	 dtx_17, NULL, test_case_teardown},
};

static int
dtx_test_setup(void **state)
{
	int     rc;

	rc = test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE,
			0, NULL);

	return rc;
}

int
run_daos_base_tx_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(dtx_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests("Single RDG TX tests", dtx_tests,
				ARRAY_SIZE(dtx_tests), sub_tests,
				sub_tests_size, dtx_test_setup, test_teardown);

	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}
