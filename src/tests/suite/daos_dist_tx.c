/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * tests/suite/daos_dist_tx.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_test.h"
#include "daos_iotest.h"
#include <daos/dtx.h>

#define MUST(rc)		assert_int_equal(rc, 0)
#define	DTX_TEST_SUB_REQS	32
#define DTX_IO_SMALL		32
#define DTX_NC_CNT		10

D_CASSERT(DTX_NC_CNT % IOREQ_SG_IOD_NR == 0);

static const char *dts_dtx_dkey	= "dtx_dkey";
static const char *dts_dtx_akey	= "dtx_akey";

static void
dtx_1(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		 write_bufs[DTX_TEST_SUB_REQS][DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 i;

	print_message("DTX1: multiple SV update against the same obj\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	arg->async = 0;
	oid = daos_test_oid_gen(arg->coh, OC_RP_XSF, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	/* Repeatedly insert different SV for the same obj, overwrite. */
	for (i = 0; i < DTX_TEST_SUB_REQS; i++) {
		dts_buf_render(write_bufs[i], DTX_IO_SMALL);
		insert_single(dkey, akey, 0, write_bufs[i], DTX_IO_SMALL,
			      th, &req);
	}

	MUST(daos_tx_commit(th, NULL));

	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL,
		      DAOS_TX_NONE, &req);
	/* The last value will be fetched. */
	assert_memory_equal(write_bufs[DTX_TEST_SUB_REQS - 1], fetch_buf,
			    DTX_IO_SMALL);

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_2(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		 write_bufs[DTX_TEST_SUB_REQS][DTX_IO_SMALL * 2];
	char		 fetch_buf[DTX_IO_SMALL * (DTX_TEST_SUB_REQS + 1)];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 i;

	print_message("DTX2: multiple EV update against the same obj\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	arg->async = 0;
	oid = daos_test_oid_gen(arg->coh, OC_RP_XSF, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/* Repeatedly insert different SV for the same obj, some overlap. */
	for (i = 0; i < DTX_TEST_SUB_REQS; i++) {
		dts_buf_render(write_bufs[i], DTX_IO_SMALL * 2);
		insert_single_with_rxnr(dkey, akey, i, write_bufs[i],
					DTX_IO_SMALL, 2, th, &req);
	}

	MUST(daos_tx_commit(th, NULL));

	lookup_single_with_rxnr(dkey, akey, 0, fetch_buf, DTX_IO_SMALL,
				DTX_IO_SMALL * (DTX_TEST_SUB_REQS + 1),
				DAOS_TX_NONE, &req);

	for (i = 0; i < DTX_TEST_SUB_REQS - 1; i++)
		assert_memory_equal(&write_bufs[i],
				    &fetch_buf[i * DTX_IO_SMALL], DTX_IO_SMALL);

	assert_memory_equal(&write_bufs[DTX_TEST_SUB_REQS - 1],
			    &fetch_buf[DTX_IO_SMALL * (DTX_TEST_SUB_REQS - 1)],
			    DTX_IO_SMALL * 2);

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_update_multiple_objs(test_arg_t *arg, daos_iod_type_t i_type,
			 uint32_t size, uint16_t oclass)
{
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		*write_bufs[DTX_TEST_SUB_REQS];
	char		*fetch_buf;
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oids[DTX_TEST_SUB_REQS];
	struct ioreq	 reqs[DTX_TEST_SUB_REQS];
	int		 i;

	MUST(daos_tx_open(arg->coh, &th, 0, NULL));
	arg->async = 0;

	for (i = 0; i < DTX_TEST_SUB_REQS; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, oclass, 0, 0,
					    arg->myrank);
		ioreq_init(&reqs[i], arg->coh, oids[i], i_type, arg);

		D_ALLOC(write_bufs[i], size);
		assert_non_null(write_bufs[i]);

		dts_buf_render(write_bufs[i], size);
		insert_single(dkey, akey, 0, write_bufs[i], size, th, &reqs[i]);
	}

	MUST(daos_tx_commit(th, NULL));

	D_ALLOC(fetch_buf, size);
	assert_non_null(fetch_buf);

	for (i = 0; i < DTX_TEST_SUB_REQS; i++) {
		lookup_single(dkey, akey, 0, fetch_buf, size, DAOS_TX_NONE,
			      &reqs[i]);
		assert_memory_equal(write_bufs[i], fetch_buf, size);
	}

	for (i = 0; i < DTX_TEST_SUB_REQS; i++) {
		D_FREE(write_bufs[i]);
		ioreq_fini(&reqs[i]);
	}
	D_FREE(fetch_buf);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_3(void **state)
{
	print_message("DTX3: Multiple small SV update against multiple objs\n");
	dtx_update_multiple_objs(*state, DAOS_IOD_SINGLE, 1 << 6, OC_S1);
}

static void
dtx_4(void **state)
{
	print_message("DTX4: Multiple large EV update against multiple objs\n");
	dtx_update_multiple_objs(*state, DAOS_IOD_ARRAY, 1 << 12, OC_S1);
}

static void
dtx_5(void **state)
{
	test_arg_t	*arg = *state;

	print_message("DTX5: Multiple small SV update on multiple EC objs\n");

	if (!test_runable(arg, 3))
		skip();

	dtx_update_multiple_objs(arg, DAOS_IOD_SINGLE, 1 << 8, OC_EC_2P1G1);
}

static void
dtx_6(void **state)
{
	test_arg_t	*arg = *state;

	print_message("DTX6: Multiple large EV update on multiple EC objs\n");

	if (!test_runable(arg, 3))
		skip();

	dtx_update_multiple_objs(arg, DAOS_IOD_ARRAY, 1 << 16, OC_EC_2P1G1);
}

static void
dtx_7(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		 write_buf[DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	print_message("DTX7: SV update plus punch\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	arg->async = 0;
	oid = daos_test_oid_gen(arg->coh, OC_RP_XSF, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	dts_buf_render(write_buf, DTX_IO_SMALL);
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	punch_akey(dkey, akey, th, &req);

	MUST(daos_tx_commit(th, NULL));

	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL,
		      DAOS_TX_NONE, &req);
	assert_int_equal(req.iod[0].iod_size, 0);

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_8(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		 write_bufs[2][DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL * 2];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	print_message("DTX8: EV update plus punch\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	arg->async = 0;
	oid = daos_test_oid_gen(arg->coh, OC_RP_XSF, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	dts_buf_render(write_bufs[0], DTX_IO_SMALL);
	insert_single(dkey, akey, 0, write_bufs[0], DTX_IO_SMALL, th, &req);

	punch_akey(dkey, akey, th, &req);

	dts_buf_render(write_bufs[1], DTX_IO_SMALL);
	insert_single(dkey, akey, 1, write_bufs[1], DTX_IO_SMALL, th, &req);

	MUST(daos_tx_commit(th, NULL));

	memset(fetch_buf, 0, DTX_IO_SMALL);
	memset(write_bufs[0], 0, DTX_IO_SMALL);

	lookup_single_with_rxnr(dkey, akey, 0, fetch_buf, DTX_IO_SMALL,
				DTX_IO_SMALL * 2, DAOS_TX_NONE, &req);
	assert_memory_equal(write_bufs[0], fetch_buf, DTX_IO_SMALL);
	assert_memory_equal(write_bufs[1], &fetch_buf[DTX_IO_SMALL], DTX_IO_SMALL);

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_9(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		 write_bufs[2][DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL * 2];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	print_message("DTX9: conditional insert/update\n");

	arg->async = 0;
	oid = daos_test_oid_gen(arg->coh, OC_RP_XSF, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	MPI_Barrier(MPI_COMM_WORLD);

	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	dts_buf_render(write_bufs[0], DTX_IO_SMALL);
	arg->expect_result = -DER_NONEXIST;
	insert_single_with_flags(dkey, akey, 0, write_bufs[0], DTX_IO_SMALL, th,
				 &req, DAOS_COND_DKEY_UPDATE);

	arg->expect_result = 0;
	insert_single_with_flags(dkey, akey, 0, write_bufs[0], DTX_IO_SMALL, th,
				 &req, DAOS_COND_DKEY_INSERT);

	MUST(daos_tx_commit(th, NULL));
	MUST(daos_tx_close(th, NULL));
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	dts_buf_render(write_bufs[1], DTX_IO_SMALL);
	arg->expect_result = -DER_EXIST;
	insert_single_with_flags(dkey, akey, 1, write_bufs[1], DTX_IO_SMALL, th,
				 &req, DAOS_COND_AKEY_INSERT);

	arg->expect_result = 0;
	insert_single_with_flags(dkey, akey, 1, write_bufs[1], DTX_IO_SMALL, th,
				 &req, DAOS_COND_AKEY_UPDATE);

	MUST(daos_tx_commit(th, NULL));
	MUST(daos_tx_close(th, NULL));

	lookup_single_with_rxnr(dkey, akey, 0, fetch_buf, DTX_IO_SMALL,
				DTX_IO_SMALL * 2, DAOS_TX_NONE, &req);
	assert_memory_equal(write_bufs[0], fetch_buf, DTX_IO_SMALL);
	assert_memory_equal(write_bufs[1], &fetch_buf[DTX_IO_SMALL], DTX_IO_SMALL);

	MPI_Barrier(MPI_COMM_WORLD);

	ioreq_fini(&req);
}

static void
dtx_10(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	const char	*dkey2 = "tmp_dkey";
	const char	*akey2 = "tmp_akey";
	char		 write_buf[DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	print_message("DTX10: conditional punch\n");

	arg->async = 0;
	oid = daos_test_oid_gen(arg->coh, OC_RP_XSF, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	dts_buf_render(write_buf, DTX_IO_SMALL);
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, DAOS_TX_NONE,
		      &req);

	MPI_Barrier(MPI_COMM_WORLD);

	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	arg->expect_result = -DER_NONEXIST;
	punch_akey_with_flags(dkey2, akey2, th, &req, DAOS_COND_PUNCH);

	arg->expect_result = 0;
	punch_akey_with_flags(dkey, akey, th, &req, DAOS_COND_PUNCH);

	arg->expect_result = -DER_NONEXIST;
	punch_dkey_with_flags(dkey2, th, &req, DAOS_COND_PUNCH);

	/** Remove the test for the dkey because it can't work with client
	 *  side caching and punch propagation.   The dkey will have been
	 *  removed by the akey punch above.  The problem is the server
	 *  doesn't know that due to caching so there is no way to make it
	 *  work.
	 */
	MUST(daos_tx_commit(th, NULL));

	arg->expect_result = 0;
	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL,
		      DAOS_TX_NONE, &req);
	assert_int_equal(req.iod[0].iod_size, 0);

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));

	MPI_Barrier(MPI_COMM_WORLD);
}

static void
dtx_11(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	const char	*dkey2 = "tmp_dkey";
	const char	*akey2 = "tmp_akey";
	char		 write_buf[DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	print_message("DTX11: read only transaction\n");

	arg->async = 0;
	oid = daos_test_oid_gen(arg->coh, OC_RP_XSF, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	dts_buf_render(write_buf, DTX_IO_SMALL);
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, DAOS_TX_NONE,
		      &req);

	insert_single(dkey2, akey, 0, write_buf, DTX_IO_SMALL, DAOS_TX_NONE,
		      &req);

	MUST(daos_tx_open(arg->coh, &th, DAOS_TF_RDONLY, NULL));

	arg->expect_result = -DER_NO_PERM;

	insert_single(dkey, akey2, 0, write_buf, DTX_IO_SMALL, th, &req);
	punch_akey(dkey, akey, th, &req);

	arg->expect_result = 0;
	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL, th, &req);
	assert_int_equal(req.iod[0].iod_size, DTX_IO_SMALL);

	lookup_single(dkey, akey2, 0, fetch_buf, DTX_IO_SMALL, th, &req);
	assert_int_equal(req.iod[0].iod_size, 0);

	MUST(daos_tx_commit(th, NULL));

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_12(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	const char	*akey2 = "tmp_akey";
	char		 write_buf[DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 i;

	print_message("DTX12: zero copy flag\n");

	MPI_Barrier(MPI_COMM_WORLD);

	MUST(daos_tx_open(arg->coh, &th, DAOS_TF_ZERO_COPY, NULL));

	arg->async = 0;
	oid = daos_test_oid_gen(arg->coh, OC_RP_XSF, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	dts_buf_render(write_buf, DTX_IO_SMALL);
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	/* Reuse 'write_buf' */
	for (i = 0; i < DTX_IO_SMALL; i++)
		write_buf[i] += 1;

	insert_single(dkey, akey2, 0, write_buf, DTX_IO_SMALL, th, &req);

	MUST(daos_tx_commit(th, NULL));

	lookup_single(dkey, akey2, 0, fetch_buf, DTX_IO_SMALL,
		      DAOS_TX_NONE, &req);
	assert_memory_equal(write_buf, fetch_buf, DTX_IO_SMALL);

	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL,
		      DAOS_TX_NONE, &req);

	/* 'write_buf' has been overwritten, so it is the same as 2nd update.
	 *
	 * XXX: It is just for test purpose, but not the promised hehavior to
	 *	application for the case of reuse buffer with ZERO_COPY flag.
	 */
	assert_memory_equal(write_buf, fetch_buf, DTX_IO_SMALL);

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));

	MPI_Barrier(MPI_COMM_WORLD);
}

static void
dtx_13(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		 write_buf[DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 rc;

	print_message("DTX13: DTX status machnie\n");

	arg->async = 0;
	oid = daos_test_oid_gen(arg->coh, OC_RP_XSF, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	dts_buf_render(write_buf, DTX_IO_SMALL);

	print_message("Open the TX1...\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	print_message("Commit the empty TX1...\n");
	MUST(daos_tx_commit(th, NULL));

	print_message("Commit the committed TX1...\n");
	rc = daos_tx_commit(th, NULL);
	assert_rc_equal(rc, -DER_ALREADY);

	print_message("Abort the committed TX1, expect DER_NO_PERM\n");
	rc = daos_tx_abort(th, NULL);
	assert_rc_equal(rc, -DER_NO_PERM);

	print_message("Restart the committed TX1, expect DER_NO_PERM\n");
	rc = daos_tx_restart(th, NULL);
	assert_rc_equal(rc, -DER_NO_PERM);

	print_message("Update against the committed TX1, expect DER_NO_PERM\n");
	arg->expect_result = -DER_NO_PERM;
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	print_message("Fetch against the committed TX1, expect DER_NO_PERM\n");
	arg->expect_result = -DER_NO_PERM;
	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL, th, &req);

	print_message("Close the TX1...\n");
	MUST(daos_tx_close(th, NULL));

	print_message("Open the TX2...\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	print_message("Update via the TX2...\n");
	arg->expect_result = 0;
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	print_message("Restart the TX2, expect DER_NO_PERM\n");
	rc = daos_tx_restart(th, NULL);
	assert_rc_equal(rc, -DER_NO_PERM);

	print_message("Abort the TX2...\n");
	MUST(daos_tx_abort(th, NULL));

	print_message("Abort the TX2 again...\n");
	rc = daos_tx_abort(th, NULL);
	assert_rc_equal(rc, -DER_ALREADY);

	print_message("Commit the aborted TX2, expect DER_NO_PERM\n");
	rc = daos_tx_commit(th, NULL);
	assert_rc_equal(rc, -DER_NO_PERM);

	print_message("Update against the aborted TX2, expect DER_NO_PERM\n");
	arg->expect_result = -DER_NO_PERM;
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	print_message("Fetch against the aborted TX2, expect DER_NO_PERM\n");
	arg->expect_result = -DER_NO_PERM;
	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL, th, &req);

	print_message("Close the TX2...\n");
	MUST(daos_tx_close(th, NULL));

	ioreq_fini(&req);
}

static void
dtx_14(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	const char	*akey2 = "tmp_akey";
	char		 write_buf[DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 nrestarts = 13;
	int		 rc;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX14: restart because of conflict with others\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	arg->async = 0;
	oid = daos_test_oid_gen(arg->coh, OC_RP_XSF, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	dts_buf_render(write_buf, DTX_IO_SMALL);
again:
	arg->expect_result = 0;
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	MPI_Barrier(MPI_COMM_WORLD);
	/* Simulate the conflict with other DTX. */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     DAOS_DTX_RESTART | DAOS_FAIL_ALWAYS,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	rc = daos_tx_commit(th, NULL);
	assert_rc_equal(rc, -DER_TX_RESTART);

	/* Not allow new I/O before restart the TX. */
	arg->expect_result = -DER_NO_PERM;
	insert_single(dkey, akey2, 0, write_buf, DTX_IO_SMALL, th, &req);

	MUST(daos_tx_restart(th, NULL));

	/* Reset the fail_loc */
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	nrestarts--;
	if (nrestarts > 0) {
		print_message("Simulate another conflict/restart...\n");
		goto again;
	}

	arg->expect_result = 0;
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	MUST(daos_tx_commit(th, NULL));

	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL,
		      DAOS_TX_NONE, &req);
	assert_memory_equal(write_buf, fetch_buf, DTX_IO_SMALL);

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_15(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		 write_buf[DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX15: restart because of stale pool map\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	arg->async = 0;
	oid = daos_test_oid_gen(arg->coh, OC_RP_XSF, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(DAOS_DTX_STALE_PM | DAOS_FAIL_ALWAYS);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     DAOS_DTX_STALE_PM | DAOS_FAIL_ALWAYS,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	arg->expect_result = -DER_TX_RESTART;
	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL, th, &req);

	/* Reset the fail_loc */
	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(0);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	dts_buf_render(write_buf, DTX_IO_SMALL);
	/* Not allow new I/O before restart the TX. */
	arg->expect_result = -DER_NO_PERM;
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	MUST(daos_tx_restart(th, NULL));

	arg->expect_result = 0;
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	MUST(daos_tx_commit(th, NULL));

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_handle_resent(test_arg_t *arg, uint64_t fail_loc)
{
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		 write_buf[DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	print_message("Resend commit because of lost CPD request\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	arg->async = 0;
	oid = daos_test_oid_gen(arg->coh, OC_RP_XSF, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	dts_buf_render(write_buf, DTX_IO_SMALL);
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     fail_loc | DAOS_FAIL_ALWAYS, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	MUST(daos_tx_commit(th, NULL));

	/* Reset the fail_loc */
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL,
		      DAOS_TX_NONE, &req);
	assert_memory_equal(write_buf, fetch_buf, DTX_IO_SMALL);

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_16(void **state)
{
	print_message("DTX16: resend commit because of lost CPD request\n");

	/* DAOS_DTX_LOST_RPC_REQUEST will simulate the case of CPD RPC
	 * request lost before being executed on the leader. Then the
	 * client will resend the CPD RPC after timeout.
	 */

	dtx_handle_resent(*state, DAOS_DTX_LOST_RPC_REQUEST);
}

static void
dtx_17(void **state)
{
	print_message("DTX17: resend commit because of lost CPD reply\n");

	/* DAOS_DTX_LOST_RPC_REPLY will simulate the case of CPD RPC
	 * reply lost after being executed on the leader. Then the
	 * client will resend the CPD RPC after timeout.
	 */
	dtx_handle_resent(*state, DAOS_DTX_LOST_RPC_REPLY);
}

static void
dtx_18(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		 write_buf[DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_epoch_t	 epoch = 0;
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 rc;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX18: Spread read time-stamp when commit\n");

	if (!test_runable(arg, 3))
		skip();

	arg->async = 0;
	oid = daos_test_oid_gen(arg->coh, OC_RP_3G1, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	dts_buf_render(write_buf, DTX_IO_SMALL);
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL,
		      DAOS_TX_NONE, &req);

	/* Start read only transaction. */
	MUST(daos_tx_open(arg->coh, &th, DAOS_TF_RDONLY, NULL));

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		/* DAOS_DTX_NO_READ_TS will skip the initial read TS. */
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
			DAOS_DTX_NO_READ_TS | DAOS_FAIL_ALWAYS, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL, th, &req);
	assert_memory_equal(write_buf, fetch_buf, DTX_IO_SMALL);

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	MUST(daos_tx_commit(th, NULL));

	MUST(daos_tx_hdl2epoch(th, &epoch));
	assert_int_not_equal(epoch, 0);

	MUST(daos_tx_close(th, NULL));

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(DAOS_DTX_SPEC_EPOCH | DAOS_FAIL_ALWAYS);
	daos_fail_value_set(epoch - 1);
	MPI_Barrier(MPI_COMM_WORLD);

	/* Start another RW transaction. */
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	dts_buf_render(write_buf, DTX_IO_SMALL);
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	/* Expect to hit conflict with the read TS on other shards. */
	rc = daos_tx_commit(th, NULL);
	assert_rc_equal(rc, -DER_TX_RESTART);

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_value_set(0);
	daos_fail_loc_set(0);
	MPI_Barrier(MPI_COMM_WORLD);

	MUST(daos_tx_close(th, NULL));

	ioreq_fini(&req);
}

static void
dtx_19(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		*write_bufs[DTX_TEST_SUB_REQS];
	char		*fetch_buf;
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oids[DTX_TEST_SUB_REQS];
	struct ioreq	 reqs[DTX_TEST_SUB_REQS];
	uint32_t	 nr[DTX_TEST_SUB_REQS];
	size_t		 size[DTX_TEST_SUB_REQS];
	size_t		 max_size = 0;
	daos_iod_type_t	 i_type;
	int		 i;
	uint16_t	 oclass;

	print_message("DTX19: misc rep and EC object update in same TX.\n");

	if (!test_runable(arg, 4))
		skip();

	MUST(daos_tx_open(arg->coh, &th, DAOS_TF_ZERO_COPY, NULL));
	arg->async = 0;

	for (i = 0; i < DTX_TEST_SUB_REQS; i++) {
		switch (i % 4) {
		case 0:
			oclass = OC_EC_2P1G1;
			i_type = DAOS_IOD_SINGLE;
			nr[i] = 1;
			size[i] = 8 * (1 << (i % 21));
			break;
		case 1:
			oclass = OC_EC_2P2G1;
			i_type = DAOS_IOD_ARRAY;
			nr[i] = 1 << (i % 21);
			size[i] = 8;
			break;
		case 2:
			oclass = OC_S1;
			i_type = DAOS_IOD_SINGLE;
			nr[i] = 1;
			size[i] = 8 * (1 << (i % 21));
			break;
		default:
			oclass = OC_S2;
			i_type = DAOS_IOD_ARRAY;
			nr[i] = 1 << (i % 21);
			size[i] = 8;
			break;
		}

		if (max_size < size[i] * nr[i])
			max_size = size[i] * nr[i];

		oids[i] = daos_test_oid_gen(arg->coh, oclass, 0, 0,
					    arg->myrank);
		ioreq_init(&reqs[i], arg->coh, oids[i], i_type, arg);

		D_ALLOC(write_bufs[i], size[i] * nr[i]);
		assert_non_null(write_bufs[i]);

		dts_buf_render(write_bufs[i], size[i] * nr[i]);
		insert_single_with_rxnr(dkey, akey, i, write_bufs[i],
					size[i], nr[i], th, &reqs[i]);
	}

	MUST(daos_tx_commit(th, NULL));

	D_ALLOC(fetch_buf, max_size);
	assert_non_null(fetch_buf);

	for (i = 0; i < DTX_TEST_SUB_REQS; i++) {
		memset(fetch_buf, 0, max_size);
		lookup_single_with_rxnr(dkey, akey, i, fetch_buf, size[i],
					size[i] * nr[i], DAOS_TX_NONE,
					&reqs[i]);
		assert_memory_equal(write_bufs[i], fetch_buf, size[i] * nr[i]);
	}

	for (i = 0; i < DTX_TEST_SUB_REQS; i++) {
		D_FREE(write_bufs[i]);
		ioreq_fini(&reqs[i]);
	}
	D_FREE(fetch_buf);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_init_oid_req_akey(test_arg_t *arg, daos_obj_id_t *oids, struct ioreq *reqs,
		      uint16_t *ocs, daos_iod_type_t *types, char *akeys[],
		      int oid_req_cnt, int akey_cnt, uint8_t ofeats)
{
	int	i;

	for (i = 0; i < oid_req_cnt; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, ocs[i], ofeats, 0,
					    arg->myrank);
		ioreq_init(&reqs[i], arg->coh, oids[i], types[i], arg);
	}

	for (i = 0; i < akey_cnt; i++) {
		D_ALLOC(akeys[i], 16);
		assert_non_null(akeys[i]);
		dts_buf_render(akeys[i], 16);
	}
}

static void
dtx_fini_req_akey(struct ioreq *reqs, char *akeys[],
		  int req_cnt, int akey_cnt)
{
	int	i;

	for (i = 0; i < req_cnt; i++)
		ioreq_fini(&reqs[i]);

	for (i = 0; i < akey_cnt; i++)
		D_FREE(akeys[i]);
}

static void
dtx_20(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		*write_bufs[2];
	char		*fetch_buf;
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oids[2];
	struct ioreq	 reqs[2];
	daos_iod_type_t	 types[2] = { DAOS_IOD_SINGLE, DAOS_IOD_ARRAY };
	uint16_t	 ocs[2] = { OC_EC_2P1G1, OC_RP_2G1 };
	size_t		 size = (1 << 20) + 3;
	int		 i;
	int		 rc;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX20: atomicity - either all done or none done\n");

	if (!test_runable(arg, 3))
		skip();

	dtx_init_oid_req_akey(arg, oids, reqs, ocs, types, NULL, 2, 0, 0);

	print_message("Successful transactional update\n");

	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	for (i = 0; i < 2; i++) {
		D_ALLOC(write_bufs[i], size);
		assert_non_null(write_bufs[i]);

		dts_buf_render(write_bufs[i], size);
		insert_single(dkey, akey, 0, write_bufs[i], size, th, &reqs[i]);
	}

	MUST(daos_tx_commit(th, NULL));
	MUST(daos_tx_close(th, NULL));

	D_ALLOC(fetch_buf, size);
	assert_non_null(fetch_buf);

	print_message("Verify succeeful update result\n");

	for (i = 0; i < 2; i++) {
		lookup_single(dkey, akey, 0, fetch_buf, size, DAOS_TX_NONE,
			      &reqs[i]);
		/* Both the two object should have been updated successfully. */
		assert_memory_equal(write_bufs[i], fetch_buf, size);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	/* Simulate the case of TX IO error on the shard_1. */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_DTX_FAIL_IO | DAOS_FAIL_ALWAYS,
				      0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("Failed transactional update\n");

	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	for (i = 0; i < 2; i++)
		/* Exchange the buffers of the two object via new update. */
		insert_single(dkey, akey, 0, write_bufs[1 - i], size, th,
			      &reqs[i]);

	rc = daos_tx_commit(th, NULL);
	assert_rc_equal(rc, -DER_IO);

	MUST(daos_tx_close(th, NULL));

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				      0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("Verify failed update result\n");

	for (i = 0; i < 2; i++) {
		lookup_single(dkey, akey, 0, fetch_buf, size, DAOS_TX_NONE,
			      &reqs[i]);
		/* The 2nd update failed for one object, then none of the
		 * objects are updated, so the data should be old value.
		 */
		assert_memory_equal(write_bufs[i], fetch_buf, size);

		D_FREE(write_bufs[i]);
		ioreq_fini(&reqs[i]);
	}

	D_FREE(fetch_buf);
}

static void
dtx_21(void **state)
{
	test_arg_t	*arg = *state;
	const char	*akey = dts_dtx_akey;
	char		*dkeys[DTX_TEST_SUB_REQS];
	char		*write_bufs[DTX_TEST_SUB_REQS];
	char		*fetch_buf;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	daos_iod_type_t	 type = DAOS_IOD_ARRAY;
	uint16_t	 oc = OC_RP_2G2;
	size_t		 size = 32;
	int		 i;
	int		 rc;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX21: TX atomicity - internal transaction.\n");

	if (!test_runable(arg, 4))
		skip();

	dtx_init_oid_req_akey(arg, &oid, &req, &oc, &type, NULL, 1, 0, 0);

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(DAOS_DTX_COMMIT_SYNC | DAOS_FAIL_ALWAYS);

	for (i = 0; i < DTX_TEST_SUB_REQS; i++) {
		D_ALLOC(dkeys[i], 16);
		assert_non_null(dkeys[i]);
		dts_buf_render(dkeys[i], 16);

		D_ALLOC(write_bufs[i], size);
		assert_non_null(write_bufs[i]);
		dts_buf_render(write_bufs[i], size);

		insert_single(dkeys[i], akey, 0, write_bufs[i],
			      size, DAOS_TX_NONE, &req);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(0);
	/* Simulate the case of TX IO error on the shard_1. */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_DTX_FAIL_IO | DAOS_FAIL_ALWAYS,
				      0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("Failed punch firstly\n");

	rc = daos_obj_punch(req.oh, DAOS_TX_NONE, 0, NULL);
	assert_rc_equal(rc, -DER_IO);

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				      0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	D_ALLOC(fetch_buf, size);
	assert_non_null(fetch_buf);

	print_message("Verify failed punch result\n");

	for (i = 0; i < DTX_TEST_SUB_REQS; i++) {
		lookup_single(dkeys[i], akey, 0, fetch_buf, size,
			      DAOS_TX_NONE, &req);
		/* Punch failed, all shards should be there. */
		assert_memory_equal(write_bufs[i], fetch_buf, size);
	}

	print_message("Successful punch object\n");

	MUST(daos_obj_punch(req.oh, DAOS_TX_NONE, 0, NULL));

	print_message("Verify successful punch result\n");

	arg->expect_result = -DER_NONEXIST;
	for (i = 0; i < DTX_TEST_SUB_REQS; i++)
		/* Punch succeed, all shards should have been punched. */
		lookup_empty_single(dkeys[i], akey, 0, fetch_buf, size,
				    DAOS_TX_NONE, &req);

	for (i = 0; i < DTX_TEST_SUB_REQS; i++) {
		D_FREE(dkeys[i]);
		D_FREE(write_bufs[i]);
	}

	D_FREE(fetch_buf);
	ioreq_fini(&req);
}

static void
dtx_share_oid(daos_obj_id_t *oid)
{
	int	rc;

	rc = MPI_Bcast(&oid->lo, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
	assert_int_equal(rc, MPI_SUCCESS);

	rc = MPI_Bcast(&oid->hi, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
	assert_int_equal(rc, MPI_SUCCESS);

	MPI_Barrier(MPI_COMM_WORLD);
}

static void
dtx_22(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oids[2];
	struct ioreq	 reqs[2];
	uint64_t	 vals[2] = { 0 };
	daos_iod_type_t	 types[2] = { DAOS_IOD_SINGLE, DAOS_IOD_ARRAY };
	uint16_t	 ocs[2] = { OC_EC_2P1G1, OC_RP_2G1 };
	int		 i, j;
	int		 rc;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX22: TX isolation - invisible partial modification\n");

	if (!test_runable(arg, 3))
		skip();

	if (arg->myrank == 0)
		dtx_init_oid_req_akey(arg, oids, reqs, ocs, types,
				      NULL, 2, 0, 0);

	/* All ranks share the same two objects. */
	for (i = 0; i < 2; i++)
		dtx_share_oid(&oids[i]);

	if (arg->myrank != 0) {
		ioreq_init(&reqs[0], arg->coh, oids[0], types[0], arg);
		ioreq_init(&reqs[1], arg->coh, oids[1], types[1], arg);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	/* Generate the base objects and values via rank0. */
	if (arg->myrank == 0) {
		daos_fail_loc_set(DAOS_DTX_COMMIT_SYNC | DAOS_FAIL_ALWAYS);
		for (i = 0; i < 2; i++)
			insert_single(dkey, akey, 0, &vals[0], sizeof(vals[0]),
				      DAOS_TX_NONE, &reqs[i]);
		daos_fail_loc_set(0);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	for (j = 0; j < 200; j++) {
		MUST(daos_tx_open(arg->coh, &th, 0, NULL));

restart:
		for (i = 0; i < 2; i++) {
			reqs[i].arg->not_check_result = 1;
			lookup_empty_single(dkey, akey, 0, &vals[i],
					    sizeof(vals[i]), th, &reqs[i]);
			reqs[i].arg->not_check_result = 0;

			if (reqs[i].result == -DER_TX_RESTART) {
				print_message("Handle TX restart (1) %d:%d\n",
					      arg->myrank, j);
				MUST(daos_tx_restart(th, NULL));
				goto restart;
			}

			assert_rc_equal(reqs[i].result, 0);
		}

		/* If "vals[0] > vals[1]", then vals[0]'s TX internal update
		 * status is visible to current TX.
		 *
		 * If "vals[0] < vals[1]", then MVCC is broken because current
		 * TX's epoch does not prevent vals[1]'s TX commit which epoch
		 * is older than current TX's epoch (for read).
		 */
		assert_true(vals[0] == vals[1]);

		MUST(daos_tx_hdl2epoch(th, &vals[0]));

		insert_single(dkey, akey, 0, &vals[0], sizeof(vals[0]), th,
			      &reqs[0]);
		insert_single(dkey, akey, 0, &vals[0], sizeof(vals[0]), th,
			      &reqs[1]);

		rc = daos_tx_commit(th, NULL);
		if (rc == -DER_TX_RESTART) {
			print_message("Handle TX restart (2) %d:%d\n",
				      arg->myrank, j);
			MUST(daos_tx_restart(th, NULL));
			goto restart;
		}

		assert_rc_equal(rc, 0);
		MUST(daos_tx_close(th, NULL));
	}

	dtx_fini_req_akey(reqs, NULL, 2, 0);
}

static void
dtx_23(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oids[2];
	struct ioreq	 reqs[2];
	daos_iod_type_t	 types[2] = { DAOS_IOD_ARRAY, DAOS_IOD_SINGLE };
	uint16_t	 ocs[2] = { OC_EC_2P1G1, OC_RP_2G1 };
	uint32_t	 vals[2];
	int		 rc;
	bool		 once = false;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX23: server start epoch - refuse TX with old epoch\n");

	if (!test_runable(arg, 3))
		skip();

	dtx_init_oid_req_akey(arg, oids, reqs, ocs, types, NULL, 2, 0, 0);

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(DAOS_DTX_COMMIT_SYNC | DAOS_FAIL_ALWAYS);
	insert_single(dkey, akey, 0, &vals[0], sizeof(vals[0]),
		      DAOS_TX_NONE, &reqs[0]);
	insert_single(dkey, akey, 0, &vals[0], sizeof(vals[0]),
		      DAOS_TX_NONE, &reqs[1]);
	daos_fail_loc_set(0);
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_DTX_START_EPOCH | DAOS_FAIL_ALWAYS,
				      0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

restart:
	/* It will get a stale epoch from server if set DAOS_DTX_START_EPOCH. */
	lookup_single(dkey, akey, 0, &vals[1], sizeof(vals[1]), th, &reqs[0]);

	insert_single(dkey, akey, 0, &vals[1], sizeof(vals[1]), th, &reqs[1]);

	rc = daos_tx_commit(th, NULL);
	if (once) {
		assert_rc_equal(rc, 0);
	} else {
		once = true;
		assert_rc_equal(rc, -DER_TX_RESTART);

		MPI_Barrier(MPI_COMM_WORLD);
		if (arg->myrank == 0)
			daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
					      0, 0, NULL);
		MPI_Barrier(MPI_COMM_WORLD);

		print_message("Handle TX restart %d\n", arg->myrank);

		MUST(daos_tx_restart(th, NULL));

		goto restart;
	}

	MUST(daos_tx_close(th, NULL));

	dtx_fini_req_akey(reqs, NULL, 2, 0);
}

static void
dtx_24(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey1 = "a_dkey_1";
	const char	*dkey2 = "b_dkey_2";
	const char	*akey = dts_dtx_akey;
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oids[10];
	struct ioreq	 reqs[10];
	uint32_t	 val;
	int		 i;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX24: async batched commit\n");

	if (!test_runable(arg, 4))
		skip();

	print_message("Transactional update something\n");

	for (i = 0, val = 0; i < 10; i++, val++) {
		oids[i] = daos_test_oid_gen(arg->coh, OC_RP_2G2, 0, 0,
					    arg->myrank);
		ioreq_init(&reqs[i], arg->coh, oids[i], DAOS_IOD_ARRAY, arg);

		MUST(daos_tx_open(arg->coh, &th, 0, NULL));

		insert_single(dkey1, akey, 0, &val, sizeof(val), th, &reqs[i]);
		insert_single(dkey2, akey, 0, &val, sizeof(val), th, &reqs[i]);

		MUST(daos_tx_commit(th, NULL));
		MUST(daos_tx_close(th, NULL));
	}

	print_message("Sleep %d seconds for the batched commit...\n",
		      DTX_COMMIT_THRESHOLD_AGE + 3);

	/* Sleep one batched commit interval to guarantee that all async TXs
	 * have been committed.
	 */
	sleep(DTX_COMMIT_THRESHOLD_AGE + 3);

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(DAOS_DTX_NO_RETRY | DAOS_FAIL_ALWAYS);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_DTX_NO_RETRY | DAOS_FAIL_ALWAYS,
				      0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	for (i = 0; i < 10; i++) {
		lookup_single(dkey1, akey, 0, &val, sizeof(val), DAOS_TX_NONE,
			      &reqs[i]);
		assert_int_equal(val, i);

		lookup_single(dkey2, akey, 0, &val, sizeof(val), DAOS_TX_NONE,
			      &reqs[i]);
		assert_int_equal(val, i);

		ioreq_fini(&reqs[i]);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(0);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      0, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);
}

static void
dtx_25(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey1 = "a_dkey_1";
	const char	*dkey2 = "b_dkey_2";
	const char	*akey = dts_dtx_akey;
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oids[DTX_NC_CNT];
	struct ioreq	 reqs[DTX_NC_CNT];
	uint32_t	 val;
	int		 i;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX25: uncertain status check - committable\n");

	if (!test_runable(arg, 4))
		skip();

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_DTX_NO_BATCHED_CMT |
				      DAOS_FAIL_ALWAYS, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("Transactional update without batched commit\n");

	for (i = 0, val = 1; i < DTX_NC_CNT; i++, val++) {
		oids[i] = daos_test_oid_gen(arg->coh, OC_RP_2G2, 0, 0,
					    arg->myrank);
		ioreq_init(&reqs[i], arg->coh, oids[i], DAOS_IOD_ARRAY, arg);

		MUST(daos_tx_open(arg->coh, &th, 0, NULL));

		/* Base value: i + 1 */
		insert_single(dkey1, akey, 0, &val, sizeof(val), th, &reqs[i]);
		insert_single(dkey2, akey, 0, &val, sizeof(val), th, &reqs[i]);

		MUST(daos_tx_commit(th, NULL));
		MUST(daos_tx_close(th, NULL));
	}

	print_message("Verify update result without batched commit\n");

	for (i = 0; i < DTX_NC_CNT; i++) {
		/* Async batched commit is disabled, so if fetch hit
		 * 'prepared' DTX on non-leader, it needs to resolve
		 * the uncertainty via dtx_refresh with leader.
		 */
		lookup_single(dkey1, akey, 0, &val, sizeof(val), DAOS_TX_NONE,
			      &reqs[i]);
		assert_int_equal(val, i + 1);

		lookup_single(dkey2, akey, 0, &val, sizeof(val), DAOS_TX_NONE,
			      &reqs[i]);
		assert_int_equal(val, i + 1);

		ioreq_fini(&reqs[i]);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      0, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);
}

static void
dtx_26(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey1 = "a_dkey_1";
	const char	*dkey2 = "b_dkey_2";
	const char	*akey = dts_dtx_akey;
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oids[DTX_NC_CNT];
	struct ioreq	 reqs[DTX_NC_CNT];
	uint32_t	 val;
	int		 i;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX26: uncertain status check - non-committable\n");

	if (!test_runable(arg, 4))
		skip();

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(DAOS_DTX_COMMIT_SYNC | DAOS_FAIL_ALWAYS);
	MPI_Barrier(MPI_COMM_WORLD);

	for (i = 0, val = 1; i < DTX_NC_CNT; i++, val++) {
		oids[i] = daos_test_oid_gen(arg->coh, OC_RP_2G2, 0, 0,
					    arg->myrank);
		ioreq_init(&reqs[i], arg->coh, oids[i], DAOS_IOD_ARRAY, arg);

		/* Base value: i + 1 */
		insert_single(dkey1, akey, 0, &val, sizeof(val), DAOS_TX_NONE,
			      &reqs[i]);
		insert_single(dkey2, akey, 0, &val, sizeof(val), DAOS_TX_NONE,
			      &reqs[i]);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(0);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_DTX_NO_COMMITTABLE |
				      DAOS_FAIL_ALWAYS, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("More transactional update without mark committable\n");

	for (i = 0, val = 21; i < DTX_NC_CNT; i++, val++) {
		MUST(daos_tx_open(arg->coh, &th, 0, NULL));

		/* New value: i + 21 */
		insert_single(dkey1, akey, 0, &val, sizeof(val), th, &reqs[i]);
		insert_single(dkey2, akey, 0, &val, sizeof(val), th, &reqs[i]);

		MUST(daos_tx_commit(th, NULL));
		MUST(daos_tx_close(th, NULL));
	}

	print_message("Verify update result without mark committable\n");

	for (i = 0; i < DTX_NC_CNT; i++) {
		 /* Inject fail_loc to simulate the case of non-committable.
		  * So the DTX with 'prepared' status will not be committed,
		  * and will be regarded as not ready, then become invisible
		  * for other fetch operation. Then fetch will get old value.
		  */
		lookup_single(dkey1, akey, 0, &val, sizeof(val), DAOS_TX_NONE,
			      &reqs[i]);
		assert_int_equal(val, i + 1);

		lookup_single(dkey2, akey, 0, &val, sizeof(val), DAOS_TX_NONE,
			      &reqs[i]);
		assert_int_equal(val, i + 1);

		ioreq_fini(&reqs[i]);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      0, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);
}

static void
dtx_uncertainty_miss_request(test_arg_t *arg, uint64_t loc, bool abort,
			     bool delay)
{
	const char	*dkey1 = "a_dkey_1";
	const char	*dkey2 = "b_dkey_2";
	const char	*akey = dts_dtx_akey;
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oids[DTX_NC_CNT];
	struct ioreq	 reqs[DTX_NC_CNT];
	uint32_t	 val;
	int		 i;
	int		 rc;

	if (!test_runable(arg, 4))
		skip();

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(DAOS_DTX_COMMIT_SYNC | DAOS_FAIL_ALWAYS);
	MPI_Barrier(MPI_COMM_WORLD);

	for (i = 0, val = 1; i < DTX_NC_CNT; i++, val++) {
		oids[i] = daos_test_oid_gen(arg->coh, OC_RP_2G2, 0, 0,
					    arg->myrank);
		ioreq_init(&reqs[i], arg->coh, oids[i], DAOS_IOD_ARRAY, arg);

		/* Base value: i + 1 */
		insert_single(dkey1, akey, 0, &val, sizeof(val), DAOS_TX_NONE,
			      &reqs[i]);
		insert_single(dkey2, akey, 0, &val, sizeof(val), DAOS_TX_NONE,
			      &reqs[i]);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(0);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      loc | DAOS_FAIL_ALWAYS, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("Transactional update with loc %lx\n", loc);

	for (i = 0, val = 21; i < DTX_NC_CNT; i++, val++) {
		MUST(daos_tx_open(arg->coh, &th, 0, NULL));

		/* New value: i + 21 */
		insert_single(dkey1, akey, 0, &val, sizeof(val), th, &reqs[i]);
		insert_single(dkey2, akey, 0, &val, sizeof(val), th, &reqs[i]);

		rc = daos_tx_commit(th, NULL);
		if (abort)
			assert_rc_equal(rc, -DER_IO);
		else
			assert_rc_equal(rc, 0);

		MUST(daos_tx_close(th, NULL));
	}

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, delay ?
				      (DAOS_DTX_UNCERTAIN | DAOS_FAIL_ALWAYS) :
				      0, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("Verify transactional update result with loc %lx\n", loc);

	if (delay) {
		arg->not_check_result = 1;
		for (i = 0; i < DTX_NC_CNT; i++) {
			lookup_single(dkey1, akey, 0, &val, sizeof(val),
				      DAOS_TX_NONE, &reqs[i]);
			rc = reqs[i].result;
			lookup_single(dkey2, akey, 0, &val, sizeof(val),
				      DAOS_TX_NONE, &reqs[i]);

			/* Either the 1st result or the 2nd one must be
			 * -DER_TX_UNCERTAIN, and only one can be zero,
			 *  the other is -DER_TX_UNCERTAIN.
			 */
			if (rc == 0) {
				assert_int_equal(reqs[i].result,
						 -DER_TX_UNCERTAIN);
			} else {
				assert_int_equal(rc, -DER_TX_UNCERTAIN);
				assert_int_equal(reqs[i].result, 0);
			}

			ioreq_fini(&reqs[i]);
		}
		arg->not_check_result = 0;

		MPI_Barrier(MPI_COMM_WORLD);
		if (arg->myrank == 0)
			daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
					      0, 0, NULL);
		MPI_Barrier(MPI_COMM_WORLD);
	} else {
		for (i = 0; i < DTX_NC_CNT; i++) {
			lookup_single(dkey1, akey, 0, &val, sizeof(val),
				      DAOS_TX_NONE, &reqs[i]);
			if (abort)
				assert_int_equal(val, i + 1);
			else
				assert_int_equal(val, i + 21);

			lookup_single(dkey2, akey, 0, &val, sizeof(val),
				      DAOS_TX_NONE, &reqs[i]);
			if (abort)
				assert_int_equal(val, i + 1);
			else
				assert_int_equal(val, i + 21);

			ioreq_fini(&reqs[i]);
		}
	}
}

static void
dtx_27(void **state)
{
	FAULT_INJECTION_REQUIRED();

	print_message("DTX27: uncertain status check - miss commit\n");

	dtx_uncertainty_miss_request(*state, DAOS_DTX_MISS_COMMIT,
				     false, false);
}

static void
dtx_28(void **state)
{
	FAULT_INJECTION_REQUIRED();

	print_message("DTX28: uncertain status check - miss abort\n");

	dtx_uncertainty_miss_request(*state, DAOS_DTX_MISS_ABORT, true, false);
}

static void
dtx_inject_commit_fail(test_arg_t *arg, int idx)
{
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		if (idx % 2 == 1)
			daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
					      DAOS_DTX_MISS_ABORT |
					      DAOS_FAIL_ALWAYS, 0, NULL);
		else
			daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
					      DAOS_DTX_MISS_COMMIT |
					      DAOS_FAIL_ALWAYS, 0, NULL);
	}
	MPI_Barrier(MPI_COMM_WORLD);
}

static void
dtx_generate_layout(test_arg_t *arg, const char *dkey1, const char *dkey2,
		    char **akeys, struct ioreq *reqs, int count,
		    bool base_only, bool inject_fail)
{
	daos_handle_t	 th = { 0 };
	uint64_t	 val;
	int		 i, j;
	int		 rc;

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(DAOS_DTX_COMMIT_SYNC | DAOS_FAIL_ALWAYS);
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("Non-transactional update for base layout\n");

	for (i = 0, val = 1; i < count; i++, val++) {
		/* Base value: i + 1 */
		insert_single(dkey1, akeys[i], 0, &val, sizeof(val),
			      DAOS_TX_NONE, &reqs[0]);
		insert_single(dkey2, akeys[i], 0, &val, sizeof(val),
			      DAOS_TX_NONE, &reqs[0]);
		insert_single(dkey1, akeys[i], 0, &val, sizeof(val),
			      DAOS_TX_NONE, &reqs[1]);
		insert_single(dkey2, akeys[i], 0, &val, sizeof(val),
			      DAOS_TX_NONE, &reqs[1]);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(0);
	MPI_Barrier(MPI_COMM_WORLD);

	if (base_only)
		return;

	print_message("More transactional %s fail loc\n",
		      inject_fail ? "with" : "without");

	for (j = 0; j < 2; j++) {
		for (i = 0, val = 21; i < count; i++, val++) {
			if (inject_fail)
				dtx_inject_commit_fail(arg, i);

			MUST(daos_tx_open(arg->coh, &th, 0, NULL));

			/* New value: i + 21 */
			insert_single(dkey1, akeys[i], 0, &val, sizeof(val), th,
				      &reqs[j]);
			insert_single(dkey2, akeys[i], 0, &val, sizeof(val), th,
				      &reqs[1 - j]);

			rc = daos_tx_commit(th, NULL);
			if (i % 2 == 1 && inject_fail)
				assert_rc_equal(rc, -DER_IO);
			else
				assert_rc_equal(rc, 0);

			MUST(daos_tx_close(th, NULL));
		}
	}

	if (inject_fail) {
		MPI_Barrier(MPI_COMM_WORLD);
		if (arg->myrank == 0)
			daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
					      0, 0, NULL);
		MPI_Barrier(MPI_COMM_WORLD);
	}
}

static void
dtx_29(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey1 = "a_dkey_1";
	const char	*dkey2 = "b_dkey_2";
	char		*akeys[DTX_NC_CNT];
	daos_obj_id_t	 oids[2];
	struct ioreq	 reqs[2];
	daos_iod_type_t	 types[2] = { DAOS_IOD_ARRAY, DAOS_IOD_SINGLE };
	uint16_t	 ocs[2] = { OC_EC_2P1G1, OC_RP_2G2 };
	uint64_t	 data[DTX_NC_CNT] = { 0 };
	daos_off_t	 offsets[DTX_NC_CNT];
	daos_size_t	 rec_sizes[DTX_NC_CNT];
	daos_size_t	 data_sizes[DTX_NC_CNT];
	uint64_t	*data_addrs[DTX_NC_CNT];
	int		 i;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX29: uncertain status check - fetch re-entry\n");

	if (!test_runable(arg, 4))
		skip();

	dtx_init_oid_req_akey(arg, oids, reqs, ocs, types, akeys, 2,
			      DTX_NC_CNT, 0);

	for (i = 0; i < DTX_NC_CNT; i++) {
		offsets[i] = 0;
		rec_sizes[i] = sizeof(uint64_t);
		data_sizes[i] = sizeof(uint64_t);
		data_addrs[i] = &data[i];
	}

	dtx_generate_layout(arg, dkey1, dkey2, akeys, reqs,
			    DTX_NC_CNT, false, true);

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(DAOS_DTX_NO_RETRY | DAOS_FAIL_ALWAYS);
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("Triggering fetch re-entry...\n");

	for (i = 0; i < DTX_NC_CNT; i += IOREQ_SG_IOD_NR) {
		lookup(dkey1, IOREQ_SG_IOD_NR, (const char **)(akeys + i),
		       offsets + i, rec_sizes + i, (void **)&data_addrs[i],
		       data_sizes + i, DAOS_TX_NONE, &reqs[0], false);
		lookup(dkey2, IOREQ_SG_IOD_NR, (const char **)(akeys + i),
		       offsets + i, rec_sizes + i, (void **)&data_addrs[i],
		       data_sizes + i, DAOS_TX_NONE, &reqs[1], false);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(0);
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("Verifying fetch results...\n");

	for (i = 0; i < DTX_NC_CNT; i += 2)
		assert_int_equal(data[i], i + 21);
	for (i = 1; i < DTX_NC_CNT; i += 2)
		assert_int_equal(data[i], i + 1);

	dtx_fini_req_akey(reqs, akeys, 2, DTX_NC_CNT);
}

static int
dtx_enum_parse_akey(char *str, int len, int base)
{
	int	val = 0;
	int	i;

	for (i = 0; i < len; i++) {
		if (str[i] < '0' || str[i] > '9')
			return -1;

		val = val * 10 + str[i] - '0';
	}

	return val - base;
}

static int
dtx_enum_verify_akeys(char *buf, daos_key_desc_t *kds, int num, int base)
{
	int	trace[DTX_NC_CNT * 2];
	int	idx;
	int	i;

	/* "trace[i] == 1" means that related akey should exist.
	 * "trace[i] == 0" means that related akey should not exist.
	 */
	for (i = 0; i < DTX_NC_CNT; i++)
		trace[i] = 1;
	for (i = DTX_NC_CNT;
	     i < DTX_NC_CNT * 2;
	     i += 2)
		trace[i] = 1;
	for (i = DTX_NC_CNT + 1;
	     i < DTX_NC_CNT * 2;
	     i += 2)
		trace[i] = 0;

	for (i = 0; i < num; buf += kds[i++].kd_key_len) {
		idx = dtx_enum_parse_akey(buf, kds[i].kd_key_len, base);
		if (idx < 0 || idx >= DTX_NC_CNT * 2) {
			fprintf(stderr, "Enumeration got invalid akey %.*s\n",
				(int)kds[i].kd_key_len, buf);
			return -1;
		}

		if (trace[idx] == 0) {
			fprintf(stderr, "Akey %.*s should not exist\n",
				(int)kds[i].kd_key_len, buf);
			return -1;
		}

		if (trace[idx] > 1) {
			fprintf(stderr, "Akey %.*s is packed repeatedly\n",
				(int)kds[i].kd_key_len, buf);
			return -1;
		}

		trace[idx]++;
	}

	return 0;
}

static void
dtx_30(void **state)
{
	test_arg_t	*arg = *state;
	char		*dkey1 = "a_dkey_1";
	char		*dkey2 = "b_dkey_2";
	char		*akeys[DTX_NC_CNT * 2];
	daos_obj_id_t	 oids[2];
	struct ioreq	 reqs[2];
	daos_iod_type_t	 types[2] = { DAOS_IOD_ARRAY, DAOS_IOD_SINGLE };
	uint16_t	 ocs[2] = { OC_EC_2P1G1, OC_RP_2G2 };
	int		 base = 10000;
	int		 akey_size = 32;
	daos_size_t	 buf_len = DTX_NC_CNT * 2 * akey_size;
	char		 buf[buf_len];
	daos_key_desc_t  kds[DTX_NC_CNT * 2];
	daos_anchor_t	 anchor;
	daos_handle_t	 th = { 0 };
	uint64_t	 val;
	uint32_t	 num;
	int		 rc;
	int		 i;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX30: uncertain status check - enumeration re-entry\n");

	if (!test_runable(arg, 4))
		skip();

	dtx_init_oid_req_akey(arg, oids, reqs, ocs, types, NULL, 2, 0, 0);

	for (i = 0; i < DTX_NC_CNT * 2; i++) {
		D_ALLOC(akeys[i], akey_size);
		assert_non_null(akeys[i]);
		snprintf(akeys[i], akey_size - 1, "%d", i + base);
	}

	dtx_generate_layout(arg, dkey1, dkey2, akeys, reqs,
			    DTX_NC_CNT, false, false);

	for (i = DTX_NC_CNT, val = 31; i < DTX_NC_CNT * 2; i++, val++) {
		dtx_inject_commit_fail(arg, i);

		MUST(daos_tx_open(arg->coh, &th, 0, NULL));

		/* New value: i + 31 */
		insert_single(dkey1, akeys[i], 0, &val, sizeof(val), th,
			      &reqs[0]);
		insert_single(dkey2, akeys[i], 0, &val, sizeof(val), th,
			      &reqs[0]);
		insert_single(dkey1, akeys[i], 0, &val, sizeof(val), th,
			      &reqs[1]);
		insert_single(dkey2, akeys[i], 0, &val, sizeof(val), th,
			      &reqs[1]);

		rc = daos_tx_commit(th, NULL);
		if (i % 2 == 1)
			assert_rc_equal(rc, -DER_IO);
		else
			assert_rc_equal(rc, 0);

		MUST(daos_tx_close(th, NULL));
	}

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      0, 0, NULL);
	daos_fail_loc_set(DAOS_DTX_NO_RETRY | DAOS_FAIL_ALWAYS);
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("Transactional enumerate to verify update result\n");

	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	num = DTX_NC_CNT * 2;
	memset(&anchor, 0, sizeof(anchor));
	memset(buf, 0, buf_len);

	MUST(enumerate_akey(th, dkey1, &num, kds, &anchor, buf, buf_len,
			    &reqs[0]));
	assert_int_equal(num, DTX_NC_CNT + DTX_NC_CNT / 2);

	MUST(daos_tx_commit(th, NULL));
	MUST(daos_tx_close(th, NULL));

	MUST(dtx_enum_verify_akeys(buf, kds, num, base));

	print_message("Non-transactional enumerate to verify update result\n");

	num = DTX_NC_CNT * 2;
	memset(&anchor, 0, sizeof(anchor));
	memset(buf, 0, buf_len);

	MUST(enumerate_akey(DAOS_TX_NONE, dkey2, &num, kds, &anchor, buf,
			    buf_len, &reqs[1]));
	assert_int_equal(num, DTX_NC_CNT + DTX_NC_CNT / 2);

	MUST(dtx_enum_verify_akeys(buf, kds, num, base));

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(0);
	MPI_Barrier(MPI_COMM_WORLD);

	dtx_fini_req_akey(reqs, akeys, 2, DTX_NC_CNT * 2);
}

static void
dtx_31(void **state)
{
	test_arg_t	*arg = *state;
	char		*dkey1 = "a_dkey_1";
	char		*dkey2 = "b_dkey_2";
	char		*akeys[DTX_NC_CNT];
	daos_key_t	 api_dkey1;
	daos_key_t	 api_dkey2;
	daos_key_t	 api_akeys[DTX_NC_CNT];
	daos_obj_id_t	 oids[2];
	struct ioreq	 reqs[2];
	daos_iod_type_t	 types[2] = { DAOS_IOD_ARRAY, DAOS_IOD_SINGLE };
	uint16_t	 ocs[2] = { OC_EC_2P1G1, OC_RP_2G2 };
	uint64_t	 val;
	int		 i;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX31: uncertain status check - punch re-entry\n");

	if (!test_runable(arg, 4))
		skip();

	dtx_init_oid_req_akey(arg, oids, reqs, ocs, types, akeys, 2,
			      DTX_NC_CNT, 0);

	d_iov_set(&api_dkey1, dkey1, strlen(dkey1));
	d_iov_set(&api_dkey2, dkey2, strlen(dkey2));

	for (i = 0; i < DTX_NC_CNT; i++)
		d_iov_set(&api_akeys[i], akeys[i], strlen(akeys[i]));

	dtx_generate_layout(arg, dkey1, dkey2, akeys, reqs,
			    DTX_NC_CNT, false, true);

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(DAOS_DTX_NO_RETRY | DAOS_FAIL_ALWAYS);
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("Triggering punch re-entry...\n");

	MUST(daos_obj_punch_akeys(reqs[0].oh, DAOS_TX_NONE, 0, &api_dkey1,
				  DTX_NC_CNT, api_akeys, NULL));
	MUST(daos_obj_punch_akeys(reqs[1].oh, DAOS_TX_NONE, 0, &api_dkey2,
				  DTX_NC_CNT, api_akeys, NULL));

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(0);
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("Verifying punch re-entry results...\n");

	arg->expect_result = -DER_NONEXIST;
	for (i = 0; i < DTX_NC_CNT; i++) {
		lookup_empty_single(dkey1, akeys[i], 0, &val, sizeof(val),
				    DAOS_TX_NONE, &reqs[0]);

		lookup_empty_single(dkey2, akeys[i], 0, &val, sizeof(val),
				    DAOS_TX_NONE, &reqs[1]);
	}

	dtx_fini_req_akey(reqs, akeys, 2, DTX_NC_CNT);
}

static void
dtx_32(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey1 = "a_dkey_1";
	const char	*dkey2 = "b_dkey_2";
	char		*akeys[IOREQ_SG_IOD_NR];
	daos_obj_id_t	 oids[2];
	struct ioreq	 reqs[2];
	daos_iod_type_t	 types[2] = { DAOS_IOD_ARRAY, DAOS_IOD_SINGLE };
	uint16_t	 ocs[2] = { OC_EC_2P1G1, OC_RP_2G2 };
	uint64_t	 data[IOREQ_SG_IOD_NR] = { 0 };
	int		 rx_nr[IOREQ_SG_IOD_NR];
	daos_off_t	 offsets[IOREQ_SG_IOD_NR];
	daos_size_t	 rec_sizes[IOREQ_SG_IOD_NR];
	uint64_t	*data_addrs[IOREQ_SG_IOD_NR];
	uint64_t	 val;
	int		 i;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX32: uncertain status check - update re-entry\n");

	if (!test_runable(arg, 4))
		skip();

	dtx_init_oid_req_akey(arg, oids, reqs, ocs, types, akeys, 2,
			      IOREQ_SG_IOD_NR, 0);

	for (i = 0; i < IOREQ_SG_IOD_NR; i++) {
		rx_nr[i] = 1;
		offsets[i] = 0;
		rec_sizes[i] = sizeof(uint64_t);
		data_addrs[i] = &data[i];
	}

	dtx_generate_layout(arg, dkey1, dkey2, akeys, reqs,
			    IOREQ_SG_IOD_NR, false, true);

	for (i = 0, val = 31; i < IOREQ_SG_IOD_NR; i++, val++)
		data[i] = val;

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(DAOS_DTX_COMMIT_SYNC | DAOS_FAIL_ALWAYS);
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("Triggering update re-entry...\n");

	arg->idx_no_jump = 1;
	insert(dkey1, IOREQ_SG_IOD_NR, (const char **)akeys, rec_sizes, rx_nr,
	       offsets, (void **)data_addrs, DAOS_TX_NONE, &reqs[0], 0);
	insert(dkey2, IOREQ_SG_IOD_NR, (const char **)akeys, rec_sizes, rx_nr,
	       offsets, (void **)data_addrs, DAOS_TX_NONE, &reqs[1], 0);
	arg->idx_no_jump = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(0);
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("Verifying update re-entry results...\n");

	for (i = 0; i < IOREQ_SG_IOD_NR; i++) {
		lookup_single(dkey1, akeys[i], 0, &val, sizeof(val),
			      DAOS_TX_NONE, &reqs[0]);
		assert_int_equal(val, i + 31);

		lookup_single(dkey2, akeys[i], 0, &val, sizeof(val),
			      DAOS_TX_NONE, &reqs[1]);
		assert_int_equal(val, i + 31);
	}

	dtx_fini_req_akey(reqs, akeys, 2, IOREQ_SG_IOD_NR);
}

static void
dtx_33(void **state)
{
	test_arg_t	*arg = *state;
	uint64_t	 dkeys[10];
	uint64_t	 akeys[10];
	daos_key_t	 api_dkeys[10];
	daos_key_t	 api_akey;
	daos_obj_id_t	 oids[2];
	struct ioreq	 reqs[2];
	daos_iod_type_t	 types[2] = { DAOS_IOD_ARRAY, DAOS_IOD_ARRAY };
	uint16_t	 ocs[2] = { OC_EC_2P1G1, OC_RP_2G2 };
	daos_handle_t	 th = { 0 };
	daos_iod_t	 iod = { 0 };
	d_sg_list_t	 sgl = { 0 };
	daos_recx_t	 recx = { 0 };
	d_iov_t		 val_iov;
	uint64_t	 val;
	int		 i, j;
	int		 rc;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX33: uncertain status check - query key re-entry\n");

	if (!test_runable(arg, 4))
		skip();

	dtx_init_oid_req_akey(arg, oids, reqs, ocs, types, NULL, 2, 0,
			      DAOS_OF_DKEY_UINT64 | DAOS_OF_AKEY_UINT64);

	for (i = 0; i < 10; i++) {
		dkeys[i] = 3 + i * 10;
		d_iov_set(&api_dkeys[i], &dkeys[i], sizeof(uint64_t));
	}

	for (i = 0; i < 10; i++)
		akeys[i] = 5 + i * 100;

	d_iov_set(&val_iov, &val, sizeof(val));
	recx.rx_nr = 1;
	sgl.sg_iovs = &val_iov;
	sgl.sg_nr = 1;
	iod.iod_size = sizeof(uint64_t);
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	iod.iod_type = DAOS_IOD_ARRAY;

	for (j = 0; j < 10; j++) {
		for (i = 0, val = 100000; i < 10; i++, val++) {
			dtx_inject_commit_fail(arg, i);

			MUST(daos_tx_open(arg->coh, &th, 0, NULL));

			recx.rx_idx = 7 + i * 1000 + j * 10000;
			d_iov_set(&iod.iod_name, &akeys[i], sizeof(uint64_t));

			MUST(daos_obj_update(reqs[j % 2].oh, th, 0,
					     &api_dkeys[j], 1, &iod,
					     &sgl, NULL));
			MUST(daos_obj_update(reqs[1 - j % 2].oh, th, 0,
					     &api_dkeys[10 - j - 1],
					     1, &iod, &sgl, NULL));

			rc = daos_tx_commit(th, NULL);
			if (i % 2 == 1)
				assert_rc_equal(rc, -DER_IO);
			else
				assert_rc_equal(rc, 0);

			MUST(daos_tx_close(th, NULL));
		}
	}

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      0, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	d_iov_set(&api_akey, &akeys[0], sizeof(uint64_t));

	print_message("Query the max recx on obj1\n");

	MUST(daos_obj_query_key(reqs[0].oh, DAOS_TX_NONE, DAOS_GET_DKEY |
				DAOS_GET_AKEY | DAOS_GET_RECX | DAOS_GET_MAX,
				&api_dkeys[0], &api_akey, &recx, NULL));
#if 0
	/* MAX: obj1::dkeys[j(9)], akeys[i(8)] */
	assert_int_equal(dkeys[0], 93); /* 3 + 9 * 10 */
	assert_int_equal(recx.rx_idx, 98007); /* 7 + 8 * 1000 + 9 * 10000 */
	assert_int_equal(recx.rx_nr, 1);
#endif
	assert_int_equal(akeys[0], 805); /* 5 + 8 * 100 */

	print_message("Query the min recx on obj2\n");

	MUST(daos_obj_query_key(reqs[1].oh, DAOS_TX_NONE, DAOS_GET_DKEY |
				DAOS_GET_AKEY | DAOS_GET_RECX | DAOS_GET_MIN,
				&api_dkeys[0], &api_akey, &recx, NULL));
#if 0
	/* MIX: obj2::dkeys[10 - j(0) - 1], akeys[i(0)] */
	assert_int_equal(dkeys[0], 93); /* 3 + (10 - 0 - 1) * 10 */
	assert_int_equal(recx.rx_idx, 7); /* 7 + 0 * 1000 + 0 * 10000 */
	assert_int_equal(recx.rx_nr, 1);
#endif
	assert_int_equal(akeys[0], 5); /* 5 + 0 * 100 */

	dtx_fini_req_akey(reqs, NULL, 2, 0);
}

static void
dtx_34(void **state)
{
	test_arg_t	*arg = *state;
	char		*dkey1 = "a_dkey_1";
	char		*dkey2 = "b_dkey_2";
	char		*akeys[DTX_NC_CNT];
	daos_obj_id_t	 oids[2];
	struct ioreq	 reqs[2];
	daos_iod_type_t	 types[2] = { DAOS_IOD_ARRAY, DAOS_IOD_SINGLE };
	uint16_t	 ocs[2] = { OC_EC_2P1G1, OC_RP_2G2 };
	daos_key_t	 api_dkey1;
	daos_key_t	 api_dkey2;
	daos_key_t	 api_akeys[DTX_NC_CNT];
	uint64_t	 data[DTX_NC_CNT] = { 0 };
	int		 rx_nr[DTX_NC_CNT];
	daos_off_t	 offsets[DTX_NC_CNT];
	daos_size_t	 rec_sizes[DTX_NC_CNT];
	uint64_t	*data_addrs[DTX_NC_CNT];
	daos_handle_t	 th = { 0 };
	uint64_t	 val;
	int		 rc;
	int		 i;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX34: uncertain status check - CPD RPC re-entry\n");

	if (!test_runable(arg, 4))
		skip();

	dtx_init_oid_req_akey(arg, oids, reqs, ocs, types, akeys, 2,
			      DTX_NC_CNT, 0);

	d_iov_set(&api_dkey1, dkey1, strlen(dkey1));
	d_iov_set(&api_dkey2, dkey2, strlen(dkey2));

	for (i = 0; i < DTX_NC_CNT; i++) {
		d_iov_set(&api_akeys[i], akeys[i], strlen(akeys[i]));

		rx_nr[i] = 1;
		offsets[i] = 0;
		rec_sizes[i] = sizeof(uint64_t);
		data_addrs[i] = &data[i];
	}

	dtx_generate_layout(arg, dkey1, dkey2, akeys, reqs,
			    DTX_NC_CNT, false, true);

	for (i = 0, val = 31; i < DTX_NC_CNT; i++, val++)
		data[i] = val;

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(DAOS_DTX_COMMIT_SYNC | DAOS_FAIL_ALWAYS);
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("Triggering CPD RPC handler re-entry...\n");

	arg->idx_no_jump = 1;
	for (i = 0; i < DTX_NC_CNT; i += IOREQ_SG_IOD_NR) {
		MUST(daos_tx_open(arg->coh, &th, 0, NULL));

		insert(dkey1, IOREQ_SG_IOD_NR, (const char **)(akeys + i),
		       rec_sizes + i, rx_nr + i, offsets + i,
		       (void **)&data_addrs[i], th, &reqs[0], 0);

		rc = daos_obj_punch_akeys(reqs[0].oh, th, 0, &api_dkey2,
					  IOREQ_SG_IOD_NR, api_akeys + i, NULL);
		assert_rc_equal(rc, 0);

		insert(dkey1, IOREQ_SG_IOD_NR, (const char **)(akeys + i),
		       rec_sizes + i, rx_nr + i, offsets + i,
		       (void **)&data_addrs[i], th, &reqs[1], 0);

		rc = daos_obj_punch_akeys(reqs[1].oh, th, 0, &api_dkey2,
					  IOREQ_SG_IOD_NR, api_akeys + i, NULL);
		assert_rc_equal(rc, 0);

		MUST(daos_tx_commit(th, NULL));
		MUST(daos_tx_close(th, NULL));
	}

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(0);
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("Verifying CPD RPC handler re-entry results...\n");

	for (i = 0; i < DTX_NC_CNT; i++) {
		arg->expect_result = 0;
		lookup_single(dkey1, akeys[i], 0, &val, sizeof(val),
			      DAOS_TX_NONE, &reqs[0]);
		assert_int_equal(val, i + 31);

		arg->expect_result = -DER_NONEXIST;
		lookup_empty_single(dkey2, akeys[i], 0, &val, sizeof(val),
				    DAOS_TX_NONE, &reqs[1]);
	}

	dtx_fini_req_akey(reqs, akeys, 2, DTX_NC_CNT);
}

static void
dtx_35(void **state)
{
	test_arg_t	*arg = *state;
	char		*dkey1 = "a_dkey_1";
	char		*dkey2 = "b_dkey_2";
	char		*akeys[DTX_NC_CNT];
	daos_obj_id_t	 oids[2];
	struct ioreq	 reqs[2];
	daos_iod_type_t	 types[2] = { DAOS_IOD_ARRAY, DAOS_IOD_SINGLE };
	uint16_t	 ocs[2] = { OC_EC_2P1G1, OC_RP_2G2 };
	uint64_t	 val;
	int		 i;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX35: resync during reopen container\n");

	if (!test_runable(arg, 4))
		skip();

	dtx_init_oid_req_akey(arg, oids, reqs, ocs, types, akeys, 2,
			      DTX_NC_CNT, 0);

	dtx_generate_layout(arg, dkey1, dkey2, akeys, reqs,
			    DTX_NC_CNT, false, false);

	MPI_Barrier(MPI_COMM_WORLD);

	print_message("closing object\n");
	MUST(daos_obj_close(reqs[0].oh, NULL));
	MUST(daos_obj_close(reqs[1].oh, NULL));

	print_message("closing container\n");
	MUST(daos_cont_close(arg->coh, NULL));

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		print_message("reopening container to trigger DTX resync\n");
		MUST(daos_cont_open(arg->pool.poh, arg->co_uuid, DAOS_COO_RW,
				    &arg->coh, &arg->co_info, NULL));
	}
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("share container\n");
	handle_share(&arg->coh, HANDLE_CO, arg->myrank, arg->pool.poh, 1);

	print_message("reopening object\n");
	MUST(daos_obj_open(arg->coh, oids[0], 0, &reqs[0].oh, NULL));
	MUST(daos_obj_open(arg->coh, oids[1], 0, &reqs[1].oh, NULL));

	daos_fail_loc_set(DAOS_DTX_NO_RETRY | DAOS_FAIL_ALWAYS);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_DTX_NO_RETRY | DAOS_FAIL_ALWAYS,
				      0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	/* Sleep 3 seconds, all possible DTX resync should have been done. */
	sleep(3);

	for (i = 0; i < DTX_NC_CNT; i++) {
		lookup_single(dkey1, akeys[i], 0, &val, sizeof(val),
			      DAOS_TX_NONE, &reqs[0]);
		assert_int_equal(val, i + 21);

		lookup_single(dkey2, akeys[i], 0, &val, sizeof(val),
			      DAOS_TX_NONE, &reqs[1]);
		assert_int_equal(val, i + 21);
	}

	dtx_fini_req_akey(reqs, akeys, 2, DTX_NC_CNT);

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(0);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      0, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);
}

static d_rank_t
dtx_get_restart_rank(d_rank_t *w_ranks, d_rank_t *r_ranks, int wcnt, int rcnt)
{
	int	i, j;

	for (i = 0; i < rcnt; i++) {
		bool	same = false;

		for (j = 0; j < wcnt; j++) {
			if (r_ranks[i] == w_ranks[j]) {
				same = true;
				break;
			}
		}

		if (!same)
			return r_ranks[i];
	}

	return CRT_NO_RANK;
}

static void
dtx_36(void **state)
{
	test_arg_t	*arg = *state;
	char		*dkey1 = "a_dkey_1";
	char		*dkey2 = "b_dkey_2";
	char		*akeys[DTX_NC_CNT];
	daos_obj_id_t	 oids[2];
	struct ioreq	 reqs[2];
	uint64_t	 vals[2];
	daos_iod_type_t	 types[2] = { DAOS_IOD_ARRAY, DAOS_IOD_SINGLE };
	uint16_t	 ocs[2] = { OC_RP_3G2, OC_RP_3G1 };
	daos_handle_t	 th = { 0 };
	d_rank_t	 w_ranks[3];
	d_rank_t	 r_ranks[6];
	d_rank_t	 kill_rank = CRT_NO_RANK;
	d_rank_t	 restart_rank;
	int		 i;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX36: resync - DTX entry for read only ops\n");

	if (!test_runable(arg, 7))
		skip();

	/* Obj1 has more redundancy groups than obj2. If the TX reads
	 * from multiple redundancy groups of obj1 and only writes to
	 * obj2, then there must be some server(s) that only contains
	 * read only operation.
	 */
	dtx_init_oid_req_akey(arg, oids, reqs, ocs, types, akeys, 2,
			      DTX_NC_CNT, 0);

	dtx_generate_layout(arg, dkey1, dkey2, akeys, reqs,
			    DTX_NC_CNT, true, false);

	/* Different MPI ranks will have different redundancy groups.
	 * If we kill one redundancy group for each MPI rank, then it
	 * cause too much servers to be killed as to the test can NOT
	 * go ahead. So only check on the MPI rank_0.
	 */

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		daos_fail_loc_set(DAOS_DTX_SPEC_LEADER | DAOS_FAIL_ALWAYS);
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_DTX_SPEC_LEADER | DAOS_FAIL_ALWAYS,
				      0, NULL);
		/* "DAOS_DTX_SPEC_LEADER" may affect the dispatch of
		 * sub-request on the leader, set "fail_val" as very
		 * large value can avoid such trouble.
		 */
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE,
				      (1 << 20), 0, NULL);

		print_message("Generating TXs with read only ops on server\n");

		for (i = 0, vals[1] = 31; i < DTX_NC_CNT; i++, vals[1]++) {
			MUST(daos_tx_open(arg->coh, &th, 0, NULL));

			lookup_single(dkey1, akeys[i], 0, &vals[0],
				      sizeof(vals[0]), th, &reqs[0]);
			insert_single(dkey1, akeys[i], 0, &vals[1],
				      sizeof(vals[1]), th, &reqs[1]);
			lookup_single(dkey2, akeys[i], 0, &vals[0],
				      sizeof(vals[0]), th, &reqs[0]);

			MUST(daos_tx_commit(th, NULL));
			MUST(daos_tx_close(th, NULL));
		}

		for (i = 0; i < 3; i++)
			w_ranks[i] = get_rank_by_oid_shard(arg, oids[1], i);

		for (i = 0; i < 6; i++)
			r_ranks[i] = get_rank_by_oid_shard(arg, oids[0], i);

		restart_rank = dtx_get_restart_rank(w_ranks, r_ranks, 3, 6);
		print_message("Restart rank %d when rebuild\n", restart_rank);

		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_DTX_SRV_RESTART | DAOS_FAIL_ONCE,
				      0, NULL);
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE,
				      restart_rank, 0, NULL);

		kill_rank = get_rank_by_oid_shard(arg, oids[1], 0);
		print_message("Exclude rank %d to trigger rebuild\n",
			      kill_rank);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	rebuild_single_pool_rank(arg, kill_rank, false);

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      0, 0, NULL);
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE,
				      0, 0, NULL);
		daos_fail_loc_set(0);

		print_message("Verifying data after rebuild...\n");

		for (i = 0; i < DTX_NC_CNT; i++) {
			lookup_single(dkey2, akeys[i], 0, &vals[0],
				      sizeof(vals[0]), DAOS_TX_NONE, &reqs[0]);
			assert_int_equal(vals[0], i + 1);

			lookup_single(dkey1, akeys[i], 0, &vals[0],
				      sizeof(vals[0]), DAOS_TX_NONE, &reqs[1]);
			assert_int_equal(vals[0], i + 31);
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);

	reintegrate_single_pool_rank(arg, kill_rank);

	dtx_fini_req_akey(reqs, akeys, 2, DTX_NC_CNT);
}

static void
dtx_37(void **state)
{
	test_arg_t	*arg = *state;
	char		*dkey1 = "a_dkey_1";
	char		*dkey2 = "b_dkey_2";
	char		*akeys[DTX_NC_CNT];
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	uint64_t	 val;
	daos_iod_type_t	 type = DAOS_IOD_SINGLE;
	uint16_t	 oc = OC_RP_3G2;
	daos_handle_t	 th = { 0 };
	d_rank_t	 kill_rank = CRT_NO_RANK;
	int		 i;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX37: resync - leader failed during prepare\n");

	if (!test_runable(arg, 7))
		skip();

	dtx_init_oid_req_akey(arg, &oid, &req, &oc, &type, akeys, 1,
			      DTX_NC_CNT, 0);

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(DAOS_DTX_COMMIT_SYNC | DAOS_FAIL_ALWAYS);

	print_message("Non-transactional update for base layout\n");

	for (i = 0, val = 1; i < DTX_NC_CNT; i++, val++) {
		/* Base value: i + 1 */
		insert_single(dkey1, akeys[i], 0, &val, sizeof(val),
			      DAOS_TX_NONE, &req);
		insert_single(dkey2, akeys[i], 0, &val, sizeof(val),
			      DAOS_TX_NONE, &req);
	}

	/* Different MPI ranks will generate different object layout.
	 * It is not easy to control multiple MPI ranks for specified
	 * leader and some non-leader. So only check on the MPI rank_0.
	 */
	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(0);
	if (arg->myrank == 0) {
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_DTX_NO_BATCHED_CMT |
				      DAOS_FAIL_ALWAYS, 0, NULL);

		print_message("Generating some TXs to be committed...\n");

		for (i = 0, val = 31; i < DTX_NC_CNT; i += 2, val += 2) {
			MUST(daos_tx_open(arg->coh, &th, 0, NULL));

			insert_single(dkey1, akeys[i], 0, &val, sizeof(val),
				      th, &req);
			insert_single(dkey2, akeys[i], 0, &val, sizeof(val),
				      th, &req);

			MUST(daos_tx_commit(th, NULL));
			MUST(daos_tx_close(th, NULL));
		}

		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_DTX_SKIP_PREPARE | DAOS_FAIL_ALWAYS,
				      0, NULL);
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE,
				      4, 0, NULL); /* Skip shard 4 */
		daos_fail_loc_set(DAOS_DTX_SPEC_LEADER | DAOS_FAIL_ALWAYS);

		print_message("Generating some TXs to be aborted...\n");

		for (i = 1, val = 101; i < DTX_NC_CNT; i += 2, val += 2) {
			MUST(daos_tx_open(arg->coh, &th, 0, NULL));

			insert_single(dkey1, akeys[i], 0, &val, sizeof(val),
				      th, &req);
			insert_single(dkey2, akeys[i], 0, &val, sizeof(val),
				      th, &req);

			MUST(daos_tx_commit(th, NULL));
			MUST(daos_tx_close(th, NULL));
		}

		kill_rank = get_rank_by_oid_shard(arg, oid, 0);
		print_message("Exclude rank %d to trigger rebuild\n",
			      kill_rank);

		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      0, 0, NULL);
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE,
				      0, 0, NULL);
		daos_fail_loc_set(0);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	rebuild_single_pool_rank(arg, kill_rank, false);

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		print_message("Verifying data after rebuild...\n");

		for (i = 0, val = 0; i < DTX_NC_CNT; i++, val = 0) {
			/* Full prepared TXs (i % 2 == 0) should has been
			 * committed by DTX resync, then the value should
			 * be new one.
			 * Partially prepared TXs should has been aborted
			 * during DTX resync, so the value should old one.
			 */
			lookup_single(dkey1, akeys[i], 0, &val, sizeof(val),
				      DAOS_TX_NONE, &req);
			if (i % 2 == 0)
				assert_int_equal(val, i + 31);
			else
				assert_int_equal(val, i + 1);

			lookup_single(dkey2, akeys[i], 0, &val, sizeof(val),
				      DAOS_TX_NONE, &req);
			if (i % 2 == 0)
				assert_int_equal(val, i + 31);
			else
				assert_int_equal(val, i + 1);
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);

	reintegrate_single_pool_rank(arg, kill_rank);

	dtx_fini_req_akey(&req, akeys, 1, DTX_NC_CNT);
}

static void
dtx_38(void **state)
{
	test_arg_t	*arg = *state;
	char		*dkey1 = "a_dkey_1";
	char		*dkey2 = "b_dkey_2";
	char		*akeys[DTX_NC_CNT];
	daos_obj_id_t	 oids[2];
	struct ioreq	 reqs[2];
	daos_iod_type_t	 types[2] = { DAOS_IOD_ARRAY, DAOS_IOD_SINGLE };
	uint16_t	 ocs[2] = { OC_RP_3G2, OC_S1 };
	uint64_t	 val;
	daos_handle_t	 th = { 0 };
	d_rank_t	 kill_ranks[2];
	int		 i;

	FAULT_INJECTION_REQUIRED();

	print_message("DTX38: resync - lost whole redundancy groups\n");

	if (!test_runable(arg, 7))
		skip();

	if (arg->myrank == 0) {
		oids[0] = daos_test_oid_gen(arg->coh, ocs[0], 0, 0,
					    arg->myrank);
		kill_ranks[0] = get_rank_by_oid_shard(arg, oids[0], 0);
		do {
			oids[1] = daos_test_oid_gen(arg->coh, ocs[1], 0, 0,
						    arg->myrank);
			kill_ranks[1] = get_rank_by_oid_shard(arg, oids[1], 0);
		} while (kill_ranks[0] != kill_ranks[1]);

		for (i = 0; i < DTX_NC_CNT; i++) {
			D_ALLOC(akeys[i], 16);
			assert_non_null(akeys[i]);
			dts_buf_render(akeys[i], 16);
		}

		ioreq_init(&reqs[0], arg->coh, oids[0], types[0], arg);
		ioreq_init(&reqs[1], arg->coh, oids[1], types[1], arg);
	} else {
		dtx_init_oid_req_akey(arg, oids, reqs, ocs, types, akeys, 2,
				      DTX_NC_CNT, 0);
		kill_ranks[0] = CRT_NO_RANK;
	}

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(DAOS_DTX_COMMIT_SYNC | DAOS_FAIL_ALWAYS);

	print_message("Non-transactional update for base layout\n");

	for (i = 0, val = 1; i < DTX_NC_CNT; i++, val++) {
		/* Base value: i + 1 */
		insert_single(dkey1, akeys[i], 0, &val, sizeof(val),
			      DAOS_TX_NONE, &reqs[0]);
		insert_single(dkey2, akeys[i], 0, &val, sizeof(val),
			      DAOS_TX_NONE, &reqs[0]);
		insert_single(dkey2, akeys[i], 0, &val, sizeof(val),
			      DAOS_TX_NONE, &reqs[1]);
	}

	/* Different MPI ranks will have different redundancy groups.
	 * If we kill one redundancy group for each MPI rank, then it
	 * cause too much servers to be killed as to the test can NOT
	 * go ahead. So only check on the MPI rank_0.
	 */

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(0);
	if (arg->myrank == 0) {
		daos_fail_loc_set(DAOS_DTX_SPEC_LEADER | DAOS_FAIL_ALWAYS);
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_DTX_SPEC_LEADER | DAOS_FAIL_ALWAYS,
				      0, NULL);
		/* "DAOS_DTX_SPEC_LEADER" may affect the dispatch of
		 * sub-requests on the leader, set "fail_val" as very
		 * large value can avoid such trouble.
		 */
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE,
				      (1 << 20), 0, NULL);

		print_message("Generating TXs with specified leader...\n");

		for (i = 0, val = 31; i < DTX_NC_CNT; i++, val++) {
			MUST(daos_tx_open(arg->coh, &th, 0, NULL));

			insert_single(dkey1, akeys[i], 0, &val, sizeof(val),
				      th, &reqs[0]);
			insert_single(dkey2, akeys[i], 0, &val, sizeof(val),
				      th, &reqs[1]);
			insert_single(dkey2, akeys[i], 0, &val, sizeof(val),
				      th, &reqs[0]);

			MUST(daos_tx_commit(th, NULL));
			MUST(daos_tx_close(th, NULL));
		}

		print_message("Exclude rank %d to trigger rebuild\n",
			      kill_ranks[0]);

		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      0, 0, NULL);
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE,
				      0, 0, NULL);
		daos_fail_loc_set(DAOS_DTX_NO_RETRY | DAOS_FAIL_ALWAYS);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	rebuild_single_pool_rank(arg, kill_ranks[0], false);

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		print_message("Verifying data after rebuild...\n");

		reqs[0].arg->not_check_result = 1;
		for (i = 0; i < DTX_NC_CNT; i++) {
			lookup_single(dkey1, akeys[i], 0, &val, sizeof(val),
				      DAOS_TX_NONE, &reqs[0]);
			if (reqs[0].result == 0) {
				/* Fetch from the new rebuilt target,
				 * should be the old value: i + 1
				 */
				assert_int_equal(val, i + 1);
			} else {
				assert_rc_equal(reqs[0].result,
						 -DER_DATA_LOSS);
			}

			lookup_single(dkey2, akeys[i], 0, &val, sizeof(val),
				      DAOS_TX_NONE, &reqs[0]);
			if (reqs[0].result == 0) {
				/* Fetch from the new rebuilt target,
				 * should be the old value: i + 1
				 */
				assert_int_equal(val, i + 1);
			} else {
				assert_rc_equal(reqs[0].result,
						 -DER_DATA_LOSS);
			}
		}
		reqs[0].arg->not_check_result = 0;

		print_message("Update against corrupted object...\n");

		for (i = 0, val = 101; i < DTX_NC_CNT; i++, val++) {
			insert_single(dkey1, akeys[i], 0, &val, sizeof(val),
				      DAOS_TX_NONE, &reqs[0]);
			insert_single(dkey2, akeys[i], 0, &val, sizeof(val),
				      DAOS_TX_NONE, &reqs[0]);
		}

		print_message("Verify new update...\n");

		for (i = 0; i < DTX_NC_CNT; i++) {
			lookup_single(dkey1, akeys[i], 0, &val, sizeof(val),
				      DAOS_TX_NONE, &reqs[0]);
			assert_int_equal(val, i + 101);

			lookup_single(dkey2, akeys[i], 0, &val, sizeof(val),
				      DAOS_TX_NONE, &reqs[0]);
			assert_int_equal(val, i + 101);
		}

		daos_fail_loc_set(0);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	reintegrate_single_pool_rank(arg, kill_ranks[0]);

	dtx_fini_req_akey(reqs, akeys, 2, DTX_NC_CNT);
}

static void
dtx_39(void **state)
{
	test_arg_t	*arg = *state;
	char		*akey1 = "akey_1";
	char		*akey2 = "akey_2";
	char		*akey3 = "akey_3";
	struct ioreq	 req;
	uint64_t	 val[2];
	daos_handle_t	 th = { 0 };
	d_rank_t	 kill_rank = CRT_NO_RANK;

	print_message("DTX39: not restar the transaction with fixed epoch\n");

	if (!test_runable(arg, 3))
		skip();

	if (arg->myrank == 0) {
		daos_obj_id_t	oid;
		daos_epoch_t	epoch;

		oid = daos_test_oid_gen(arg->coh, OC_RP_2G1, 0, 0, arg->myrank);
		kill_rank = get_rank_by_oid_shard(arg, oid, 0);
		ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

		val[0] = 1;
		insert_single(dts_dtx_dkey, akey1, 0, &val[0], sizeof(val[0]),
			      DAOS_TX_NONE, &req);
		insert_single(dts_dtx_dkey, akey2, 0, &val[0], sizeof(val[0]),
			      DAOS_TX_NONE, &req);

		MUST(daos_tx_open(arg->coh, &th, 0, NULL));
		insert_single(dts_dtx_dkey, akey3, 0, &val[0], sizeof(val[0]),
			      th, &req);
		MUST(daos_tx_commit(th, NULL));
		MUST(daos_tx_hdl2epoch(th, &epoch));
		MUST(daos_tx_close(th, NULL));

		MUST(daos_tx_open_snap(arg->coh, epoch << 1, &th, NULL));

		val[1] = 0;
		lookup_single(dts_dtx_dkey, akey1, 0, &val[1], sizeof(val[1]),
			      th, &req);
		assert_int_equal(val[0], val[1]);

		print_message("Exclude rank %d to trigger rebuild\n",
			      kill_rank);
	}

	rebuild_single_pool_rank(arg, kill_rank, false);

	if (arg->myrank == 0) {
		print_message("Verifying data after rebuild...\n");

		val[1] = 0;
		/* This fetch will refresh the client side pool map,
		 * then the TX's pm_ver will become stale.
		 */
		lookup_single(dts_dtx_dkey, akey2, 0, &val[1], sizeof(val[1]),
			      DAOS_TX_NONE, &req);
		assert_int_equal(val[0], val[1]);

		val[1] = 0;
		/* NOT restart the TX even if its pm_ver is stale. */
		lookup_single(dts_dtx_dkey, akey3, 0, &val[1], sizeof(val[1]),
			      th, &req);
		assert_int_equal(val[0], val[1]);

		MUST(daos_tx_close(th, NULL));
		ioreq_fini(&req);
	}

	reintegrate_single_pool_rank(arg, kill_rank);
}

static void
dtx_40(void **state)
{
	FAULT_INJECTION_REQUIRED();

	print_message("DTX40: uncertain check - miss commit with delay\n");

	dtx_uncertainty_miss_request(*state, DAOS_DTX_MISS_COMMIT, false, true);
}

static void
dtx_41(void **state)
{
	FAULT_INJECTION_REQUIRED();

	print_message("DTX41: uncertain check - miss abort with delay\n");

	dtx_uncertainty_miss_request(*state, DAOS_DTX_MISS_ABORT, true, true);
}

static test_arg_t *saved_dtx_arg;

static int
dtx_sub_setup(void **state)
{
	int	rc;

	saved_dtx_arg = *state;
	*state = NULL;
	rc = test_setup(state, SETUP_CONT_CONNECT, true, SMALL_POOL_SIZE,
			0, NULL);
	return rc;
}

static int
dtx_sub_teardown(void **state)
{
	int	rc;

	rc = test_teardown(state);
	*state = saved_dtx_arg;
	saved_dtx_arg = NULL;

	return rc;
}

static const struct CMUnitTest dtx_tests[] = {
	{"DTX1: multiple SV update against the same obj",
	 dtx_1, NULL, test_case_teardown},
	{"DTX2: multiple EV update against the same obj",
	 dtx_2, NULL, test_case_teardown},
	{"DTX3: Multiple small SV update against multiple objs",
	 dtx_3, NULL, test_case_teardown},
	{"DTX4: Multiple large EV update against multiple objs",
	 dtx_4, NULL, test_case_teardown},
	{"DTX5: Multiple small SV update on multiple EC objs",
	 dtx_5, NULL, test_case_teardown},
	{"DTX6: Multiple large EV update on multiple EC objs",
	 dtx_6, NULL, test_case_teardown},
	{"DTX7: SV update plus punch",
	 dtx_7, NULL, test_case_teardown},
	{"DTX8: EV update plus punch",
	 dtx_8, NULL, test_case_teardown},
	{"DTX9: conditional insert/update",
	 dtx_9, NULL, test_case_teardown},
	{"DTX10: conditional punch",
	 dtx_10, NULL, test_case_teardown},
	{"DTX11: read only transaction",
	 dtx_11, NULL, test_case_teardown},
	{"DTX12: zero copy flag",
	 dtx_12, NULL, test_case_teardown},
	{"DTX13: DTX status machnie",
	 dtx_13, NULL, test_case_teardown},
	{"DTX14: restart because of conflict with others",
	 dtx_14, NULL, test_case_teardown},
	{"DTX15: restart because of stale pool map",
	 dtx_15, NULL, test_case_teardown},
	{"DTX16: resend commit because of lost CPD request",
	 dtx_16, NULL, test_case_teardown},
	{"DTX17: resend commit because of lost CPD reply",
	 dtx_17, NULL, test_case_teardown},
	{"DTX18: spread read time-stamp when commit",
	 dtx_18, NULL, test_case_teardown},
	{"DTX19: Misc rep and EC object update in same TX",
	 dtx_19, NULL, test_case_teardown},

	{"DTX20: atomicity - either all done or none done",
	 dtx_20, NULL, test_case_teardown},
	{"DTX21: atomicity - internal transaction",
	 dtx_21, NULL, test_case_teardown},
	{"DTX22: TX isolation - invisible partial modification",
	 dtx_22, NULL, test_case_teardown},
	{"DTX23: server start epoch - refuse TX with old epoch",
	 dtx_23, NULL, test_case_teardown},
	{"DTX24: async batched commit",
	 dtx_24, NULL, test_case_teardown},

	{"DTX25: uncertain status check - committable",
	 dtx_25, NULL, test_case_teardown},
	{"DTX26: uncertain status check - non-committable",
	 dtx_26, NULL, test_case_teardown},
	{"DTX27: uncertain status check - miss commit",
	 dtx_27, NULL, test_case_teardown},
	{"DTX28: uncertain status check - miss abort",
	 dtx_28, NULL, test_case_teardown},

	{"DTX29: uncertain status check - fetch re-entry",
	 dtx_29, NULL, test_case_teardown},
	{"DTX30: uncertain status check - enumeration re-entry",
	 dtx_30, NULL, test_case_teardown},
	{"DTX31: uncertain status check - punch re-entry",
	 dtx_31, NULL, test_case_teardown},
	{"DTX32: uncertain status check - update re-entry",
	 dtx_32, NULL, test_case_teardown},
	{"DTX33: uncertain status check - query key re-entry",
	 dtx_33, NULL, test_case_teardown},
	{"DTX34: uncertain status check - CPD RPC re-entry",
	 dtx_34, NULL, test_case_teardown},

	{"DTX35: resync during reopen container",
	 dtx_35, NULL, test_case_teardown},
	{"DTX36: resync - DTX entry for read only ops",
	 dtx_36, dtx_sub_setup, dtx_sub_teardown},
	{"DTX37: resync - leader failed during prepare",
	 dtx_37, dtx_sub_setup, dtx_sub_teardown},
	{"DTX38: resync - lost whole redundancy groups",
	 dtx_38, dtx_sub_setup, dtx_sub_teardown},
	{"DTX39: not restart the transaction with fixed epoch",
	 dtx_39, dtx_sub_setup, dtx_sub_teardown},

	{"DTX40: uncertain check - miss commit with delay",
	 dtx_40, NULL, test_case_teardown},
	{"DTX41: uncertain check - miss abort with delay",
	 dtx_41, NULL, test_case_teardown},
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
run_daos_dist_tx_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(dtx_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests("DAOS_Distributed_TX", dtx_tests,
				ARRAY_SIZE(dtx_tests), sub_tests,
				sub_tests_size, dtx_test_setup, test_teardown);

	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}
