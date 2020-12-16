/**
 * (C) Copyright 2019-2020 Intel Corporation.
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
 * tests/suite/daos_verify_consistency.c
 *
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_test.h"
#include "daos_iotest.h"

static int dts_vc_class		= OC_RP_3GX;
static int dts_vc_replica_cnt	= 3;

#define VCT_KEY_SIZE		64
#define VCT_SMALL_IO_SIZE	256
#define VCT_LARGE_IO_SIZE	8192

static inline uint32_t
vc_random_size(uint32_t size)
{
	uint32_t	tmp;

	if (size < 4)
		return size;

	if (size < 10)
		tmp = size >> 1;
	else
		tmp = size >> 3;

	return rand() % (size - tmp) + tmp;
}

static inline void
vc_set_fail_loc(test_arg_t *arg, uint64_t fail_loc, int total, int cur)
{
	if (fail_loc == 0 || cur > total || cur < total - 1)
		return;

	if (cur == total) {
		MPI_Barrier(MPI_COMM_WORLD);
		if (arg->myrank == 0)
			daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
					     0, 0, NULL);
	} else {
		if (arg->myrank == 0)
			daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
					     fail_loc, 0, NULL);
		MPI_Barrier(MPI_COMM_WORLD);
	}
}

static void
vc_gen_modifications(test_arg_t *arg, struct ioreq *req, daos_obj_id_t oid,
		     int dkey_count, int akey_count, int rec_count,
		     uint64_t dkey_fail_loc, uint64_t akey_fail_loc,
		     uint64_t rec_fail_loc)
{
	char		 dkey[VCT_KEY_SIZE];
	char		 akey[VCT_KEY_SIZE];
	char		*buf;
	int		 buf_size;
	int		 i;
	int		 j;
	int		 k;

	print_message("Generating load for obj "DF_OID": dkeys %d (%lx), "
		      "akeys %d (%lx), recs %d (%lx)\n",
		      DP_OID(oid), dkey_count, dkey_fail_loc, akey_count,
		      akey_fail_loc, rec_count, rec_fail_loc);

	D_ALLOC(buf, VCT_LARGE_IO_SIZE);
	assert_non_null(buf);

	for (i = 0; i < dkey_count; i++) {
		vc_set_fail_loc(arg, dkey_fail_loc, dkey_count, i);
		dts_key_gen(dkey, VCT_KEY_SIZE, "dkey");

		for (j = 0; j < akey_count; j++) {
			if (i == dkey_count / 2 && j == akey_count / 2)
				punch_dkey(dkey, DAOS_TX_NONE, req);

			vc_set_fail_loc(arg, akey_fail_loc, akey_count, j);
			dts_key_gen(akey, VCT_KEY_SIZE, "akey");
			buf_size = vc_random_size(j % 2 ?
					VCT_LARGE_IO_SIZE : VCT_SMALL_IO_SIZE);

			for (k = 0; k < rec_count; k++) {
				if (j == akey_count / 2 && k == rec_count / 2)
					punch_akey(dkey, akey, DAOS_TX_NONE,
						   req);

				vc_set_fail_loc(arg, rec_fail_loc,
						rec_count, k);

				if (k == rec_count / 2 + 1) {
					punch_single(dkey, akey, k - 1,
						     DAOS_TX_NONE, req);
				} else {
					dts_buf_render(buf, buf_size);
					insert_single(dkey, akey, k, buf,
						      buf_size, DAOS_TX_NONE,
						      req);
				}
			}

			vc_set_fail_loc(arg, rec_fail_loc, rec_count, k);
		}

		vc_set_fail_loc(arg, akey_fail_loc, akey_count, j);
	}

	vc_set_fail_loc(arg, dkey_fail_loc, dkey_count, i);

	D_FREE(buf);
}

static inline int
vc_obj_verify(test_arg_t *arg, daos_obj_id_t oid)
{
	print_message("Verifying obj "DF_OID"...\n", DP_OID(oid));

	return daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
}

static void
vc_without_inconsistency(void **state, daos_iod_type_t iod_type)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 rc;

	if (!test_runable(arg, dts_vc_replica_cnt))
		return;

	oid = dts_oid_gen(dts_vc_class, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, iod_type, arg);

	vc_gen_modifications(arg, &req, oid, 7, 7, 7, 0, 0, 0);

	rc = vc_obj_verify(arg, oid);
	assert_int_equal(rc, 0);

	ioreq_fini(&req);
}

static void
vc_1(void **state)
{
	print_message("verify single value without inconsistency\n");
	vc_without_inconsistency(state, DAOS_IOD_SINGLE);
}

static void
vc_2(void **state)
{
	print_message("verify array value without inconsistency\n");
	vc_without_inconsistency(state, DAOS_IOD_ARRAY);
}

static void
vc_3(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 rc;

	print_message("misc single and array value without inconsistency\n");

	if (!test_runable(arg, dts_vc_replica_cnt))
		return;

	oid = dts_oid_gen(dts_vc_class, 0, arg->myrank);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);
	vc_gen_modifications(arg, &req, oid, 7, 7, 7, 0, 0, 0);
	ioreq_fini(&req);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	vc_gen_modifications(arg, &req, oid, 7, 7, 7, 0, 0, 0);

	rc = vc_obj_verify(arg, oid);
	assert_int_equal(rc, 0);

	ioreq_fini(&req);
}

static void
vc_4(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 rc;

	FAULT_INJECTION_REQUIRED();

	print_message("verify with different rec\n");

	if (!test_runable(arg, dts_vc_replica_cnt))
		return;

	oid = dts_oid_gen(dts_vc_class, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	vc_gen_modifications(arg, &req, oid, 7, 7, 7, 0, 0,
			     DAOS_VC_DIFF_REC | DAOS_FAIL_ALWAYS);

	rc = vc_obj_verify(arg, oid);
	assert_int_equal(rc, -DER_MISMATCH);

	ioreq_fini(&req);
}

enum vc_test_lost_type {
	VTLT_REC	= 1,
	VTLT_AKEY	= 2,
	VTLT_DKEY	= 3,
};

static void
vc_test_lost_data(void **state, int type)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 rc;

	if (!test_runable(arg, dts_vc_replica_cnt))
		return;

	oid = dts_oid_gen(dts_vc_class, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	switch (type) {
	case VTLT_REC:
		vc_gen_modifications(arg, &req, oid, 7, 7, 7, 0, 0,
				     DAOS_VC_LOST_DATA | DAOS_FAIL_ALWAYS);
		break;
	case VTLT_AKEY:
		vc_gen_modifications(arg, &req, oid, 7, 7, 7, 0,
				     DAOS_VC_LOST_DATA | DAOS_FAIL_ALWAYS, 0);
		break;
	case VTLT_DKEY:
		vc_gen_modifications(arg, &req, oid, 7, 7, 7,
				     DAOS_VC_LOST_DATA | DAOS_FAIL_ALWAYS,
				     0, 0);
		break;
	default:
		return;
	}

	rc = vc_obj_verify(arg, oid);
	assert_int_equal(rc, -DER_MISMATCH);

	ioreq_fini(&req);
}

static void
vc_5(void **state)
{

	FAULT_INJECTION_REQUIRED();

	print_message("verify with lost rec\n");
	vc_test_lost_data(state, VTLT_REC);
}

static void
vc_6(void **state)
{

	FAULT_INJECTION_REQUIRED();

	print_message("verify with lost akey\n");
	vc_test_lost_data(state, VTLT_AKEY);
}

static void
vc_7(void **state)
{

	FAULT_INJECTION_REQUIRED();

	print_message("verify with lost dkey\n");
	vc_test_lost_data(state, VTLT_DKEY);
}

static void
vc_8(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 rc;

	FAULT_INJECTION_REQUIRED();

	print_message("verify with lost replica\n");

	if (!test_runable(arg, dts_vc_replica_cnt))
		return;

	oid = dts_oid_gen(dts_vc_class, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	vc_gen_modifications(arg, &req, oid, 7, 7, 7, 0, 0, 0);

	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_VC_LOST_REPLICA | DAOS_FAIL_ALWAYS,
				      0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	rc = vc_obj_verify(arg, oid);
	assert_int_equal(rc, -DER_MISMATCH);

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     0, 0, NULL);

	ioreq_fini(&req);
}

static void
vc_9(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 rc;

	FAULT_INJECTION_REQUIRED();

	print_message("verify with different dkey\n");

	if (!test_runable(arg, dts_vc_replica_cnt))
		return;

	oid = dts_oid_gen(dts_vc_class, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	vc_gen_modifications(arg, &req, oid, 7, 7, 7,
			     DAOS_VC_DIFF_DKEY, 0, 0);

	rc = vc_obj_verify(arg, oid);
	assert_int_equal(rc, -DER_MISMATCH);

	ioreq_fini(&req);
}

static const struct CMUnitTest vc_tests[] = {
	{"VC1: verify single value without inconsistency",
	 vc_1, NULL, test_case_teardown},
	{"VC2: verify array value without inconsistency",
	 vc_2, NULL, test_case_teardown},
	{"VC3: misc single and array value without inconsistency",
	 vc_3, NULL, test_case_teardown},
	{"VC4: verify with different rec",
	 vc_4, NULL, test_case_teardown},
	{"VC5: verify with lost rec",
	 vc_5, NULL, test_case_teardown},
	{"VC6: verify with lost akey",
	 vc_6, NULL, test_case_teardown},
	{"VC7: verify with lost dkey",
	 vc_7, NULL, test_case_teardown},
	{"VC8: verify with lost replica",
	 vc_8, NULL, test_case_teardown},
	{"VC9: verify with different dkey",
	 vc_9, NULL, test_case_teardown},
};

static int
vc_test_setup(void **state)
{
	int     rc;

	rc = test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE,
			0, NULL);

	return rc;
}

int
run_daos_vc_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(vc_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests("DAOS vc tests", vc_tests, ARRAY_SIZE(vc_tests),
				sub_tests, sub_tests_size, vc_test_setup,
				test_teardown);

	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}
