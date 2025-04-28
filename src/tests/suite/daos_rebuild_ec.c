/**
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is for simple tests of rebuild, which does not need to kill the
 * rank, and only used to verify the consistency after different data model
 * rebuild.
 *
 * tests/suite/daos_rebuild_simple.c
 *
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"
#include "dfs_test.h"
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>

static void
rebuild_ec_internal(void **state, daos_oclass_id_t oclass, int kill_data_nr,
		    int kill_parity_nr, int write_type)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	d_rank_t		kill_ranks[4] = { -1 };
	int			kill_ranks_num = 0;
	d_rank_t		extra_kill_ranks[4] = { -1 };
	int			rc;

	if (oclass == OC_EC_2P1G1 && !test_runable(arg, 4))
		return;
	if (oclass == OC_EC_4P2G1 && !test_runable(arg, 8))
		return;

	if (svc_nreplicas < 5)
		return;

	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "disabled");
	oid = daos_test_oid_gen(arg->coh, oclass, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	if (write_type == PARTIAL_UPDATE)
		write_ec_partial(&req, arg->index, 0);
	else if (write_type == FULL_UPDATE)
		write_ec_full(&req, arg->index, 0);
	else if (write_type == FULL_PARTIAL_UPDATE)
		write_ec_full_partial(&req, arg->index, 0);
	else if (write_type == PARTIAL_FULL_UPDATE)
		write_ec_partial_full(&req, arg->index, 0);

	get_killing_rank_by_oid(arg, oid, kill_data_nr, kill_parity_nr,
				kill_ranks, &kill_ranks_num);

	rebuild_pools_ranks(&arg, 1, kill_ranks, kill_ranks_num, true);

	arg->rebuild_cb = reintegrate_inflight_io;
	arg->rebuild_cb_arg = &oid;
	reintegrate_pools_ranks(&arg, 1, kill_ranks, kill_ranks_num, true);

	arg->rebuild_cb = NULL;
	arg->rebuild_cb_arg = NULL;
	ioreq_fini(&req);

	/*
	 * let's kill extra data node to verify parity is correct.
	 */
	if (oclass == OC_EC_2P1G1) {
		get_killing_rank_by_oid(arg, oid, 1, 0, extra_kill_ranks, NULL);
		rebuild_pools_ranks(&arg, 1, &extra_kill_ranks[0], 1, true);
	} else { /* oclass OC_EC_4P2G1 */
		get_killing_rank_by_oid(arg, oid, 2, 0, extra_kill_ranks, NULL);
		rebuild_pools_ranks(&arg, 1, &extra_kill_ranks[0], 2, true);
	}

	if (oclass == OC_EC_2P1G1)
		reintegrate_pools_ranks(&arg, 1, &extra_kill_ranks[0], 1, true);
	else /* oclass OC_EC_4P2G1 */
		reintegrate_pools_ranks(&arg, 1, &extra_kill_ranks[0], 2, true);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	if (write_type == PARTIAL_UPDATE)
		verify_ec_partial(&req, arg->index, 0);
	else if (write_type == FULL_UPDATE)
		verify_ec_full(&req, arg->index, 0);
	else if (write_type == FULL_PARTIAL_UPDATE)
		verify_ec_full_partial(&req, arg->index, 0);
	else if (write_type == PARTIAL_FULL_UPDATE)
		verify_ec_full(&req, arg->index, 0);

	ioreq_fini(&req);

	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	assert_success(rc);
}

#define CELL_SIZE	DAOS_EC_CELL_DEF

static void
rebuild_mixed_stripes(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	char		*data;
	char		*verify_data;
	daos_recx_t	recxs[5];
	d_rank_t	rank = 0;
	int		size = 8 * CELL_SIZE + 10000;

	print_message("BEGIN %s\n", __FUNCTION__);

	if (!test_runable(arg, 7))
		return;

	oid = daos_test_oid_gen(arg->coh, OC_EC_4P2G1, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	recxs[0].rx_idx = 0;		/* full stripe */
	recxs[0].rx_nr = 4 * CELL_SIZE;

	recxs[1].rx_idx = 5 * CELL_SIZE; /* partial stripe */
	recxs[1].rx_nr = 2000;

	recxs[2].rx_idx = 8 * CELL_SIZE;	/* full stripe */
	recxs[2].rx_nr = 4 * CELL_SIZE;

	recxs[3].rx_idx = 12 * CELL_SIZE;	/* partial stripe */
	recxs[3].rx_nr = 5000;

	recxs[4].rx_idx = 16 * CELL_SIZE - 3000;	/* partial stripe */
	recxs[4].rx_nr = 3000;

	data = (char *)malloc(size);
	verify_data = (char *)malloc(size);
	make_buffer(data, 'a', size);
	make_buffer(verify_data, 'a', size);

	req.iod_type = DAOS_IOD_ARRAY;
	insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, recxs, 5,
		     data, size, &req);

	rank = get_rank_by_oid_shard(arg, oid, 0);
	rebuild_pools_ranks(&arg, 1, &rank, 1, false);

	memset(data, 0, size);
	lookup_recxs("d_key", "a_key", 1, DAOS_TX_NONE, recxs, 5,
		     data, size, &req);
	assert_memory_equal(data, verify_data, size);

	free(data);
	free(verify_data);
	ioreq_fini(&req);

	reintegrate_pools_ranks(&arg, 1, &rank, 1, false);
}

static void
rebuild_ec_multi_stripes(void **state)
{
#define TEST_STRIPE_NR	(2)
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	char		*whole_data;
	char		*data[TEST_STRIPE_NR];
	char		*verify_data[TEST_STRIPE_NR];
	daos_recx_t	recxs[2 * TEST_STRIPE_NR];
	d_rank_t	rank = 0;
	uint64_t	start;
	uint16_t	fail_shards[2];
	int		i, size = 8 * CELL_SIZE;

	print_message("BEGIN %s\n", __FUNCTION__);

	if (!test_runable(arg, 7))
		return;

	oid = daos_test_oid_gen(arg->coh, OC_EC_4P2G1, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	for (i = 0; i < TEST_STRIPE_NR; i++) {
		start = i * 4 * CELL_SIZE;
		recxs[0].rx_idx = start;
		recxs[0].rx_nr = 4 * CELL_SIZE;
		recxs[1].rx_idx = start + 80 * CELL_SIZE;
		recxs[1].rx_nr = 4 * CELL_SIZE;

		data[i] = (char *)malloc(size);
		verify_data[i] = (char *)malloc(size);
		make_buffer(data[i], 'a' + i, size);
		make_buffer(verify_data[i], 'a' + i, size);

		req.iod_type = DAOS_IOD_ARRAY;
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, recxs, 2, data[i], size, &req);
		free(data[i]);
	}

	ioreq_fini(&req);

	/* test EC degraded fetch and verify data */
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	fail_shards[0] = 1;
	fail_shards[1] = 0;
	daos_fail_value_set(daos_shard_fail_value(fail_shards, 2));
	daos_fail_loc_set(DAOS_FAIL_SHARD_OPEN | DAOS_FAIL_ALWAYS);
	for (i = 0; i < TEST_STRIPE_NR; i++) {
		start = i * 4 * CELL_SIZE;
		recxs[i * 2].rx_idx = start;
		recxs[i * 2].rx_nr = 4 * CELL_SIZE;
		recxs[i * 2 + 1].rx_idx = start + 80 * CELL_SIZE;
		recxs[i * 2 + 1].rx_nr = 4 * CELL_SIZE;
	}

	whole_data  = (char *)malloc(size * TEST_STRIPE_NR);
	memset(whole_data, 0, size * TEST_STRIPE_NR);
	lookup_recxs("d_key", "a_key", 1, DAOS_TX_NONE, recxs, 2 * TEST_STRIPE_NR,
		     whole_data, size * TEST_STRIPE_NR, &req);

	for (i = 0; i < TEST_STRIPE_NR; i++) {
		data[i] = whole_data + size * i;
		assert_memory_equal(data[i], verify_data[i], size);
	}
	free(whole_data);
	daos_fail_value_set(0);
	daos_fail_loc_set(0);
	ioreq_fini(&req);

	/* test fetch after EC rebuild and verify data */
	rank = get_rank_by_oid_shard(arg, oid, 0);
	rebuild_pools_ranks(&arg, 1, &rank, 1, false);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < TEST_STRIPE_NR; i++) {
		start = i * 4 * CELL_SIZE;
		recxs[0].rx_idx = start;
		recxs[0].rx_nr = 4 * CELL_SIZE;
		recxs[1].rx_idx = start + 80 * CELL_SIZE;
		recxs[1].rx_nr = 4 * CELL_SIZE;

		data[i] = (char *)malloc(size);
		memset(data[i], 0, size);
		lookup_recxs("d_key", "a_key", 1, DAOS_TX_NONE, recxs, 2, data[i], size, &req);
		assert_memory_equal(data[i], verify_data[i], size);
		free(data[i]);
		free(verify_data[i]);
	}

	ioreq_fini(&req);
	reintegrate_pools_ranks(&arg, 1, &rank, 1, false);
}

static int
rebuild_ec_setup(void  **state, int number, uint32_t rf)
{
	test_arg_t	*arg;
	daos_prop_t	*props = NULL;
	int		rc;

	save_group_state(state);
	rc = test_setup(state, SETUP_POOL_CONNECT, true,
			REBUILD_POOL_SIZE, number, NULL);
	if (rc) {
		print_message("It can not create the pool with %d ranks"
			      " probably due to not enough ranks %d\n",
			      number, rc);
		return rc;
	}

	arg = *state;
	/* sustain 2 failure here */
	props = daos_prop_alloc(3);
	props->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	props->dpp_entries[0].dpe_val = rf;
	props->dpp_entries[1].dpe_type = DAOS_PROP_CO_CSUM;
	props->dpp_entries[1].dpe_val = DAOS_PROP_CO_CSUM_CRC32;
	props->dpp_entries[2].dpe_type = DAOS_PROP_CO_CSUM_SERVER_VERIFY;
	props->dpp_entries[2].dpe_val = DAOS_PROP_CO_CSUM_SV_ON;

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, props);
	assert_success(rc);
	daos_prop_free(props);

	if (dt_obj_class != DAOS_OC_UNKNOWN)
		arg->obj_class = dt_obj_class;
	else
		arg->obj_class = DAOS_OC_R3S_SPEC_RANK;

	return rc;
}

static int
rebuild_ec_8nodes_rf1_setup(void **state)
{
	return rebuild_ec_setup(state, 8, DAOS_PROP_CO_REDUN_RF1);
}

static int
rebuild_ec_8nodes_setup(void **state)
{
	return rebuild_ec_setup(state, 8, DAOS_PROP_CO_REDUN_RF2);
}

static int
rebuild_ec_6nodes_setup(void **state)
{
	return rebuild_ec_setup(state, 6, DAOS_PROP_CO_REDUN_RF2);
}

static void
rebuild_partial_fail_data(void **state)
{
	print_message("BEGIN %s\n", __FUNCTION__);
	rebuild_ec_internal(state, OC_EC_2P1G1, 1, 0, PARTIAL_UPDATE);
	print_message("END %s\n", __FUNCTION__);
}

static void
rebuild_partial_fail_parity(void **state)
{
	print_message("BEGIN %s\n", __FUNCTION__);
	rebuild_ec_internal(state, OC_EC_2P1G1, 0, 1, PARTIAL_UPDATE);
}

static void
rebuild_full_fail_data(void **state)
{
	print_message("BEGIN %s\n", __FUNCTION__);
	rebuild_ec_internal(state, OC_EC_2P1G1, 1, 0, FULL_UPDATE);
}

static void
rebuild_full_fail_parity(void **state)
{
	print_message("BEGIN %s\n", __FUNCTION__);
	rebuild_ec_internal(state, OC_EC_2P1G1, 0, 1, FULL_UPDATE);
}

static void
rebuild_full_partial_fail_data(void **state)
{
	print_message("BEGIN %s\n", __FUNCTION__);
	rebuild_ec_internal(state, OC_EC_2P1G1, 1, 0, FULL_PARTIAL_UPDATE);
}

static void
rebuild_full_partial_fail_parity(void **state)
{
	print_message("BEGIN %s\n", __FUNCTION__);
	rebuild_ec_internal(state, OC_EC_2P1G1, 0, 1, FULL_PARTIAL_UPDATE);
}

static void
rebuild_partial_full_fail_data(void **state)
{
	print_message("BEGIN %s\n", __FUNCTION__);
	rebuild_ec_internal(state, OC_EC_2P1G1, 1, 0, PARTIAL_FULL_UPDATE);
}

static void
rebuild_partial_full_fail_parity(void **state)
{
	print_message("BEGIN %s\n", __FUNCTION__);
	rebuild_ec_internal(state, OC_EC_2P1G1, 0, 1, PARTIAL_FULL_UPDATE);
}

static void
rebuild2p_partial_fail_data(void **state)
{
	print_message("BEGIN %s\n", __FUNCTION__);
	rebuild_ec_internal(state, OC_EC_4P2G1, 1, 0, FULL_UPDATE);
}

static void
rebuild2p_partial_fail_2data(void **state)
{
	print_message("BEGIN %s\n", __FUNCTION__);
	rebuild_ec_internal(state, OC_EC_4P2G1, 2, 0, FULL_UPDATE);
}

static void
rebuild2p_partial_fail_data_parity(void **state)
{
	print_message("BEGIN %s\n", __FUNCTION__);
	rebuild_ec_internal(state, OC_EC_4P2G1, 1, 1, FULL_UPDATE);
}

static void
rebuild2p_partial_fail_parity(void **state)
{
	print_message("BEGIN %s\n", __FUNCTION__);
	rebuild_ec_internal(state, OC_EC_4P2G1, 0, 1, FULL_UPDATE);
}

static void
rebuild2p_partial_fail_2parity(void **state)
{
	print_message("BEGIN %s\n", __FUNCTION__);
	rebuild_ec_internal(state, OC_EC_4P2G1, 0, 2, FULL_UPDATE);
}

static void
rebuild_dfs_fail_data_s0(void **state)
{
	int shard = 0;

	print_message("BEGIN %s\n", __FUNCTION__);
	dfs_ec_rebuild_io(state, &shard, 1);
}

static void
rebuild_dfs_fail_data_s1(void **state)
{
	int shard = 1;

	print_message("BEGIN %s\n", __FUNCTION__);
	dfs_ec_rebuild_io(state, &shard, 1);
}

static void
rebuild_dfs_fail_data_s3(void **state)
{
	int shard = 3;

	print_message("BEGIN %s\n", __FUNCTION__);
	dfs_ec_rebuild_io(state, &shard, 1);
}

static void
rebuild_dfs_fail_2data_s0s1(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 0;
	shards[1] = 1;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
rebuild_dfs_fail_2data_s0s2(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 0;
	shards[1] = 2;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
rebuild_dfs_fail_2data_s0s3(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 0;
	shards[1] = 3;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
rebuild_dfs_fail_2data_s1s2(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 1;
	shards[1] = 2;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
rebuild_dfs_fail_2data_s1s3(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 1;
	shards[1] = 3;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
rebuild_dfs_fail_2data_s2s3(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 2;
	shards[1] = 3;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
rebuild_dfs_fail_data_parity_s0p1(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 0;
	shards[1] = 5;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
rebuild_dfs_fail_data_parity_s3p1(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 3;
	shards[1] = 5;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
rebuild_dfs_fail_data_parity_s2p1(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 2;
	shards[1] = 5;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
rebuild_dfs_fail_data_parity_s0p0(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 0;
	shards[1] = 4;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
rebuild_dfs_fail_data_parity_s2p0(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 2;
	shards[1] = 4;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
rebuild_dfs_fail_data_parity_s3p0(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 3;
	shards[1] = 4;
	dfs_ec_rebuild_io(state, shards, 2);
}

void
dfs_ec_seq_fail(void **state, int *shards, int shards_nr)
{
	dfs_t		*dfs_mt;
	daos_handle_t	co_hdl;
	uuid_t		co_uuid;
	char		str[37];
	test_arg_t	*arg = *state;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	dfs_obj_t	*obj;
	daos_size_t	buf_size = 16 * CELL_SIZE;
	daos_size_t	chunk_size = 16 * CELL_SIZE;
	char		filename[32];
	d_rank_t	ranks[4] = { -1 };
	int		idx = 0;
	d_sg_list_t	small_sgl;
	d_iov_t		small_iov;
	char		*small_buf;
	char		*small_vbuf;
	int		small_buf_size = 32;
	daos_obj_id_t	oid;
	char		*buf;
	char		*vbuf;
	int		i;
	int		rc;

	dfs_attr_t attr = {};

	attr.da_props = daos_prop_alloc(2);
	assert_non_null(attr.da_props);
	attr.da_props->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_LVL;
	attr.da_props->dpp_entries[0].dpe_val = DAOS_PROP_CO_REDUN_RANK;
	attr.da_props->dpp_entries[1].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	attr.da_props->dpp_entries[1].dpe_val = DAOS_PROP_CO_REDUN_RF2;

	rc = dfs_cont_create(arg->pool.poh, &co_uuid, &attr, &co_hdl, &dfs_mt);
	daos_prop_free(attr.da_props);
	assert_success(rc);
	printf("Created DFS Container "DF_UUIDF"\n", DP_UUID(co_uuid));

	D_ALLOC(buf, buf_size);
	assert_true(buf != NULL);
	D_ALLOC(vbuf, buf_size);
	assert_true(vbuf != NULL);

	d_iov_set(&iov, buf, buf_size);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs = &iov;

	dts_buf_render(buf, buf_size);
	memcpy(vbuf, buf, buf_size);

	/* Full stripe update */
	sprintf(filename, "rebuild_file");
	rc = dfs_open(dfs_mt, NULL, filename, S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT, OC_EC_4P2G1, chunk_size,
		      NULL, &obj);
	assert_success(rc);
	rc = dfs_write(dfs_mt, obj, &sgl, 0, NULL);
	assert_success(rc);

	/* partial stripe update */
	D_ALLOC(small_buf, small_buf_size);
	assert_true(small_buf != NULL);
	D_ALLOC(small_vbuf, small_buf_size);
	assert_true(small_vbuf != NULL);
	d_iov_set(&small_iov, small_buf, small_buf_size);
	small_sgl.sg_nr = 1;
	small_sgl.sg_nr_out = 1;
	small_sgl.sg_iovs = &small_iov;
	dts_buf_render(small_buf, small_buf_size);
	memcpy(small_vbuf, small_buf, small_buf_size);
	for (i = 0; i < 30; i++) {
		daos_off_t	offset;

		offset = (i + 20) * 4 * CELL_SIZE;
		rc = dfs_write(dfs_mt, obj, &small_sgl, offset, NULL);
		assert_success(rc);
		offset += CELL_SIZE - 10;
		rc = dfs_write(dfs_mt, obj, &small_sgl, offset, NULL);
		assert_success(rc);
	}

	dfs_obj2id(obj, &oid);
	while (shards_nr-- > 0) {
		daos_size_t fetch_size = 0;

		ranks[idx] = get_rank_by_oid_shard(arg, oid, shards[idx]);
		rebuild_pools_ranks(&arg, 1, &ranks[idx], 1, false);
		idx++;

		daos_cont_status_clear(co_hdl, NULL);
		/* Verify full stripe */
		d_iov_set(&iov, buf, buf_size);
		memset(buf, 0, buf_size);
		rc = dfs_read(dfs_mt, obj, &sgl, 0, &fetch_size, NULL);
		assert_success(rc);
		assert_int_equal(fetch_size, buf_size);
		assert_memory_equal(buf, vbuf, buf_size);
		for (i = 0; i < 30; i++) {
			daos_off_t	offset;

			memset(small_buf, 0, small_buf_size);
			offset = (i + 20) * 4 * CELL_SIZE;
			rc = dfs_read(dfs_mt, obj, &small_sgl, offset,
				      &fetch_size, NULL);
			assert_success(rc);
			assert_int_equal(fetch_size, small_buf_size);
			assert_memory_equal(small_buf, small_vbuf,
					    small_buf_size);
			offset += CELL_SIZE - 10;
			memset(small_buf, 0, small_buf_size);
			rc = dfs_read(dfs_mt, obj, &small_sgl, offset,
				      &fetch_size, NULL);
			assert_int_equal(fetch_size, small_buf_size);
			assert_success(rc);
			assert_memory_equal(small_buf, small_vbuf,
					    small_buf_size);
		}
	}

	rc = dfs_release(obj);
	assert_success(rc);

	D_FREE(buf);
	rc = dfs_umount(dfs_mt);
	assert_success(rc);

	rc = daos_cont_close(co_hdl, NULL);
	assert_rc_equal(rc, 0);

	uuid_unparse(co_uuid, str);
	rc = daos_cont_destroy(arg->pool.poh, str, 1, NULL);
	assert_rc_equal(rc, 0);

#if 0
	while (idx > 0)
		rebuild_add_back_tgts(arg, ranks[--idx], NULL, 1);
#endif
}

static void
rebuild_dfs_fail_seq_s0s1(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 0;
	shards[1] = 1;
	dfs_ec_seq_fail(state, shards, 2);
}

static void
rebuild_dfs_fail_seq_s1s2(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 1;
	shards[1] = 2;
	dfs_ec_seq_fail(state, shards, 2);
}

static void
rebuild_dfs_fail_seq_s2s3(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 2;
	shards[1] = 3;
	dfs_ec_seq_fail(state, shards, 2);
}

static void
rebuild_dfs_fail_seq_s0s3(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 0;
	shards[1] = 3;
	dfs_ec_seq_fail(state, shards, 2);
}

static void
rebuild_dfs_fail_seq_s0p0(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 0;
	shards[1] = 4;
	dfs_ec_seq_fail(state, shards, 2);
}

static void
rebuild_dfs_fail_seq_s3p1(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 3;
	shards[1] = 5;
	dfs_ec_seq_fail(state, shards, 2);
}

static void
rebuild_dfs_fail_seq_p0p1(void **state)
{
	int shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	shards[0] = 4;
	shards[1] = 5;
	dfs_ec_seq_fail(state, shards, 2);
}

static void
rebuild_multiple_group_ec_object(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	char		*data;
	char		*verify_data;
	int		i;
	char		dkey[32];
	daos_recx_t	recx;
	d_rank_t	rank = 0;
	uint32_t	tgt_idx;
	daos_iod_t	iod;
	d_iov_t		sg_iov;
	d_sg_list_t	sgl;
	int		size = 4 * CELL_SIZE;
	int		rc;

	print_message("BEGIN %s\n", __FUNCTION__);

	if (!test_runable(arg, 8))
		return;

	oid = daos_test_oid_gen(arg->coh, OC_EC_4P1G8, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	data = (char *)malloc(size);
	verify_data = (char *)malloc(size);
	make_buffer(data, 'a', size);
	make_buffer(verify_data, 'a', size);

	d_iov_set(&sg_iov, data, size);
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= &sg_iov;
	d_iov_set(&iod.iod_name, "a_key_single", strlen("a_key_single"));
	iod.iod_nr	= 1;
	iod.iod_size	= size;
	iod.iod_recxs	= NULL;
	iod.iod_type	= DAOS_IOD_SINGLE;

	for (i = 0; i < 30; i++) {
		daos_key_t	dkey_iov;

		sprintf(dkey, "d_key_%d", i);

		recx.rx_idx = 0;	/* full stripe */
		recx.rx_nr = size;
		insert_recxs(dkey, "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, size, &req);

		d_iov_set(&dkey_iov, dkey, strlen(dkey));
		rc = daos_obj_update(req.oh, DAOS_TX_NONE, 0, &dkey_iov, 1, &iod, &sgl, NULL);
		assert_rc_equal(rc, 0);
	}

	rank = get_rank_by_oid_shard(arg, oid, 17);
	tgt_idx = get_tgt_idx_by_oid_shard(arg, oid, 17);
	rebuild_single_pool_target(arg, rank, tgt_idx, false);

	for (i = 0; i < 30; i++) {
		daos_key_t	dkey_iov;

		sprintf(dkey, "d_key_%d", i);

		recx.rx_idx = 0;	/* full stripe */
		recx.rx_nr = size;
		memset(data, 0, size);
		lookup_recxs(dkey, "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, size, &req);
		assert_memory_equal(data, verify_data, size);

		d_iov_set(&dkey_iov, dkey, strlen(dkey));
		d_iov_set(&sg_iov, data, size);
		sgl.sg_nr	= 1;
		sgl.sg_nr_out	= 0;
		sgl.sg_iovs	= &sg_iov;
		d_iov_set(&iod.iod_name, "a_key_single", strlen("a_key_single"));
		iod.iod_nr	= 1;
		iod.iod_size	= size;
		iod.iod_recxs	= NULL;
		iod.iod_type	= DAOS_IOD_SINGLE;

		rc = daos_obj_fetch(req.oh, DAOS_TX_NONE, 0, &dkey_iov, 1, &iod, &sgl, NULL, NULL);
		assert_rc_equal(rc, 0);
		assert_memory_equal(data, verify_data, size);
	}

	ioreq_fini(&req);

	free(data);
	free(verify_data);
}

static int
enumerate_cb(void *data)
{
	test_arg_t	*arg = data;
	struct ioreq	*req = arg->rebuild_cb_arg;
	daos_anchor_t	anchor = { 0 };
	int		total = 0;
	char		buf[512];
	daos_size_t	buf_len = 512;
	int		rc;

	while (!daos_anchor_is_eof(&anchor)) {
		daos_key_desc_t kds[10];
		uint32_t number = 10;

		memset(buf, 0, buf_len);
		rc = enumerate_dkey(DAOS_TX_NONE, &number, kds, &anchor, buf,
				    buf_len, req);
		assert_rc_equal(rc, 0);
		total += number;
		print_message("total %d  number %d\n", total, number);
	}

	assert_int_equal(total, 100);
	return 0;
}

static void
rebuild_ec_dkey_enumeration(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	d_rank_t	rank;
	int		i;

	print_message("BEGIN %s\n", __FUNCTION__);

	if (!test_runable(arg, 8))
		return;

	oid = daos_test_oid_gen(arg->coh, OC_EC_4P1G1, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 100; i++) {
		char dkey[32];

		/* Make dkey on different shards */
		req.iod_type = DAOS_IOD_ARRAY;
		sprintf(dkey, "dkey_%d", i);
		insert_single(dkey, "a_key", 0, "data", strlen("data") + 1,
			      DAOS_TX_NONE, &req);
	}

	rank = get_rank_by_oid_shard(arg, oid, 4);
	arg->rebuild_cb = enumerate_cb;
	arg->rebuild_cb_arg = &req;
	rebuild_single_pool_rank(arg, rank, false);
	ioreq_fini(&req);
}

static void
rebuild_ec_parity_multi_group(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	d_rank_t	rank;
	int		i;

	print_message("BEGIN %s\n", __FUNCTION__);

	if (!test_runable(arg, 8))
		return;

	oid = daos_test_oid_gen(arg->coh, OC_EC_4P1G8, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 100; i++) {
		char dkey[32];

		/* Make dkey on different shards */
		req.iod_type = DAOS_IOD_ARRAY;
		sprintf(dkey, "dkey_%d", i);
		insert_single(dkey, "a_key", 0, "data", strlen("data") + 1,
			      DAOS_TX_NONE, &req);
	}

	rank = get_rank_by_oid_shard(arg, oid, 9);
	rebuild_single_pool_rank(arg, rank, false);

	reintegrate_single_pool_rank(arg, rank, false);
	ioreq_fini(&req);
}

#define SNAP_CNT	20
static void
rebuild_ec_snapshot(void **state, daos_oclass_id_t oclass, int shard)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	daos_epoch_t	snap_epoch[SNAP_CNT];
	daos_size_t	data_size;
	d_rank_t	rank;
	char		*data;
	char		*verify_data;
	int		rc;
	int		i;

	if (!test_runable(arg, 6))
		return;

	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "time");
	oid = daos_test_oid_gen(arg->coh, oclass, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	data_size = ec_data_nr_get(oid) * (uint64_t)CELL_SIZE + 1000;
	data = (char *)malloc(data_size);
	assert_true(data != NULL);
	verify_data = (char *)malloc(data_size);
	assert_true(verify_data != NULL);

	for (i = 0; i < SNAP_CNT; i++) {
		daos_recx_t recx;

		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = data_size;
		recx.rx_idx = 0;
		memset(data, 'a' + i, data_size);
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, data_size, &req);
		daos_cont_create_snap(arg->coh, &snap_epoch[i], NULL, NULL);
	}

	rank = get_rank_by_oid_shard(arg, oid, shard);
	rebuild_single_pool_rank(arg, rank, false);

	for (i = 0; i < SNAP_CNT; i++) {
		daos_handle_t		th_open;
		daos_epoch_range_t	epr;

		daos_tx_open_snap(arg->coh, snap_epoch[i], &th_open, NULL);
		memset(verify_data, 'a' + i, data_size);
		ec_verify_parity_data(&req, "d_key", "a_key", 0, data_size,
				      verify_data, th_open, false);
		daos_tx_close(th_open, NULL);

		epr.epr_hi = epr.epr_lo = snap_epoch[i];
		rc = daos_cont_destroy_snap(arg->coh, epr, NULL);
		assert_success(rc);
	}

	reintegrate_single_pool_rank(arg, rank, false);
	free(data);
	free(verify_data);
	ioreq_fini(&req);
}

static void
rebuild_ec_snapshot_data_shard(void **state)
{
	print_message("BEGIN %s\n", __FUNCTION__);
	rebuild_ec_snapshot(state, OC_EC_4P2G1, 0);
}

static void
rebuild_ec_snapshot_parity_shard(void **state)
{
	print_message("BEGIN %s\n", __FUNCTION__);
	rebuild_ec_snapshot(state, OC_EC_4P2G1, 5);
}

static void
rebuild_ec_parity_overwrite(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	char		*data;
	daos_recx_t	recx;
	d_rank_t	rank = 0;
	int		i;
	int		stripe_size = 2 * CELL_SIZE;

	print_message("BEGIN %s\n", __FUNCTION__);

	if (!test_runable(arg, 8))
		return;

	oid = daos_test_oid_gen(arg->coh, OC_EC_2P2G1, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	data = (char *)malloc(stripe_size);
	make_buffer(data, 'a', stripe_size);

	for (i = 0; i < 5; i++) {
		recx.rx_idx = i * stripe_size;	/* full stripe */
		recx.rx_nr = stripe_size;
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, stripe_size, &req);

		recx.rx_idx = i * stripe_size + 1000;
		recx.rx_nr = 1000;
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, 1000, &req);

		recx.rx_idx = i * stripe_size + CELL_SIZE + 1000;
		recx.rx_nr = 1000;
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, 1000, &req);
	}

	rank = get_rank_by_oid_shard(arg, oid, 2);
	rebuild_single_pool_rank(arg, rank, false);

	ioreq_fini(&req);

	free(data);
}

static void
rebuild_ec_then_aggregation(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	d_rank_t	rank;
	int		i;
	char		*data;
	char		*verify_data;

	print_message("BEGIN %s\n", __FUNCTION__);

	if (!test_runable(arg, 8))
		return;

	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "disabled");
	data = (char *)malloc(CELL_SIZE);
	assert_true(data != NULL);
	verify_data = (char *)malloc(CELL_SIZE);
	assert_true(verify_data != NULL);
	oid = daos_test_oid_gen(arg->coh, OC_EC_4P2G1, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 11; i++) {
		daos_recx_t recx;

		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = CELL_SIZE;
		recx.rx_idx = i * CELL_SIZE;
		memset(data, 'a' + i, CELL_SIZE);
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, CELL_SIZE, &req);
	}

	/* rebuild one data shard */
	rank = get_rank_by_oid_shard(arg, oid, 2);
	rebuild_pools_ranks(&arg, 1, &rank, 1, false);
	print_message("sleep 30 seconds");
	sleep(30);

	/* Trigger EC aggregation */
	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "time");
	trigger_and_wait_ec_aggreation(arg, &oid, 1, NULL, NULL, 0, 0,
				       DAOS_FORCE_EC_AGG);

	for (i = 0; i < 11; i++) {
		daos_off_t offset = i * CELL_SIZE;

		memset(verify_data, 'a' + i, CELL_SIZE);
		ec_verify_parity_data(&req, "d_key", "a_key", offset,
				      (daos_size_t)CELL_SIZE, verify_data,
				      DAOS_TX_NONE, true);
	}
	free(data);
	free(verify_data);
}

static void
rebuild_ec_multiple_shards(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oids[20];
	d_rank_t	rank = 2;
	int		i, j, k;
	char		*data;
	char		*verify_data;
	uint64_t	stripe_size = 4 * CELL_SIZE;
	daos_recx_t	recx;

	print_message("BEGIN %s\n", __FUNCTION__);

	if (!test_runable(arg, 6))
		return;

	data = (char *)malloc(stripe_size);
	verify_data = (char *)malloc(stripe_size);
	assert_true(data != NULL);
	assert_true(verify_data != NULL);
	for (i = 0; i < 20; i++)
		oids[i] = daos_test_oid_gen(arg->coh, OC_EC_4P2GX, 0, 0, arg->myrank);

	for (k = 0; k < 3; k++) {
		for (i = 0; i < 20; i++) {
			ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);
			memset(data, 'a' + i, stripe_size);
			for (j = 5 * k; j < 5 * (k + 1); j++) {
				req.iod_type = DAOS_IOD_ARRAY;
				recx.rx_nr = stripe_size;
				recx.rx_idx = j * stripe_size;
				insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
					     data, stripe_size, &req);
			}
			ioreq_fini(&req);
		}

		rebuild_pools_ranks(&arg, 1, &rank, 1, false);
		daos_cont_status_clear(arg->coh, NULL);
		print_message("exclude rank %u\n", rank);
		rank++;

		for (i = 0; i < 20; i++) {
			ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);
			memset(verify_data, 'a' + i, stripe_size);
			for (j = 5 * k; j < 5 * (k + 1); j++) {
				req.iod_type = DAOS_IOD_ARRAY;
				recx.rx_nr = stripe_size;
				recx.rx_idx = j * stripe_size;
				memset(data, 0, stripe_size);
				lookup_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
					     data, stripe_size, &req);
				assert_memory_equal(verify_data, data, stripe_size);
			}
			ioreq_fini(&req);
		}
	}

	rank = 2;
	for (k = 0; k < 3; k++) {
		reintegrate_pools_ranks(&arg, 1, &rank, 1, false);
		rank++;

		for (i = 0; i < 20; i++) {
			ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);
			memset(verify_data, 'a' + i, stripe_size);
			for (j = 5 * k; j < 5 * (k + 1); j++) {
				req.iod_type = DAOS_IOD_ARRAY;
				recx.rx_nr = stripe_size;
				recx.rx_idx = j * stripe_size;
				memset(data, 0, stripe_size);
				lookup_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
					     data, stripe_size, &req);
				assert_memory_equal(verify_data, data, stripe_size);
			}
			ioreq_fini(&req);
		}
	}

	free(verify_data);
	free(data);
}

static void
rebuild_ec_multiple_failure_tgts(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	d_rank_t	rank;
	int		i;
	daos_recx_t	recx;
	char		dkey[32];
	char		data[20];
	char		v_data[20];

	print_message("BEGIN %s\n", __FUNCTION__);

	if (!test_runable(arg, 6))
		return;

	oid = daos_test_oid_gen(arg->coh, OC_EC_4P2GX, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 20; i++) {
		sprintf(dkey, "d_key_%d", i);
		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = 20;
		recx.rx_idx = 0;
		memset(data, 'a', 20);
		insert_recxs(dkey, "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, 20, &req);
	}
	ioreq_fini(&req);

	arg->no_rebuild = 1;
	rebuild_single_pool_target(arg, 3, 3, false);
	arg->no_rebuild = 0;
	rank = 7;
	rebuild_pools_ranks(&arg, 1, &rank, 1, true);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 20; i++) {
		sprintf(dkey, "d_key_%d", i);
		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = 20;
		recx.rx_idx = 0;
		memset(data, 0, 20);
		memset(v_data, 'a', 20);
		lookup_recxs(dkey, "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, 20, &req);
		assert_memory_equal(data, v_data, 20);
	}
	ioreq_fini(&req);

	rank = 7;
	reintegrate_pools_ranks(&arg, 1, &rank, 1, true);
	reintegrate_single_pool_target(arg, 3, 3);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 20; i++) {
		sprintf(dkey, "d_key_%d", i);
		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = 20;
		recx.rx_idx = 0;
		memset(data, 0, 20);
		memset(v_data, 'a', 20);
		lookup_recxs(dkey, "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, 20, &req);
		assert_memory_equal(data, v_data, 20);
	}
	ioreq_fini(&req);
}

static void
rebuild_ec_parity_overwrite_fail_parity_internal(void **state, int *kill_shards, int nr,
						 bool aggregation)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	char		*data;
	char		*verify_data;
	daos_recx_t	recx;
	d_rank_t	data_ranks[4];
	d_rank_t	parity_rank;
	uint64_t	dkey_hash;
	int		shard_idx;
	int		stripe_size = 4 * CELL_SIZE;
	int		i;

	if (!test_runable(arg, 8))
		return;

	if (svc_nreplicas < 5)
		return;

	oid = daos_test_oid_gen(arg->coh, OC_EC_4P2G1, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	data = (char *)malloc(stripe_size);
	verify_data = (char *)malloc(stripe_size);
	make_buffer(data, 'a', stripe_size);

	recx.rx_idx = 0;	/* full stripe */
	recx.rx_nr = stripe_size;
	insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
		     data, stripe_size, &req);

	if (aggregation)
		trigger_and_wait_ec_aggreation(arg, &oid, 1, "d_key", "a_key", 0,
					       0, DAOS_FORCE_EC_AGG);
	make_buffer(data, 'b', 1000);
	memcpy(verify_data, data, stripe_size);
	recx.rx_idx = 0;
	recx.rx_nr = 1000;
	insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
		     data, 1000, &req);

	dkey_hash = d_hash_murmur64((const unsigned char *)"d_key", strlen("d_key"), 5731);

	/* fail parity epoch */
	shard_idx = (dkey_hash % 6 + 5) % 6;
	parity_rank = get_rank_by_oid_shard(arg, oid, shard_idx);
	rebuild_single_pool_rank(arg, parity_rank, true);

	print_message("sleep 60 seconds for aggregation\n");
	sleep(60);

	/* fail data shard */
	for (i = 0; i < nr; i++) {
		shard_idx = (dkey_hash % 6 + kill_shards[i]) % 6;
		data_ranks[i] = get_rank_by_oid_shard(arg, oid, shard_idx);
	}

	rebuild_pools_ranks(&arg, 1, data_ranks, nr, true);
	recx.rx_idx = 0;	/* full stripe */
	recx.rx_nr = stripe_size;
	lookup_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
		     data, stripe_size, &req);

	assert_memory_equal(data, verify_data, stripe_size);

	reintegrate_pools_ranks(&arg, 1, data_ranks, nr, true);
	reintegrate_pools_ranks(&arg, 1, &parity_rank, 1, true);

	lookup_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
		     data, stripe_size, &req);

	assert_memory_equal(data, verify_data, stripe_size);


	ioreq_fini(&req);
	free(data);
	free(verify_data);
}

static void
rebuild_ec_overwrite_fail_parity_data(void **state)
{
	int kill_shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	kill_shards[0] = 0;
	kill_shards[1] = 1;
	rebuild_ec_parity_overwrite_fail_parity_internal(state, kill_shards, 2, false);
}

static void
rebuild_ec_overwrite_fail_parity_data_with_parity(void **state)
{
	int kill_shards[2];

	print_message("BEGIN %s\n", __FUNCTION__);
	kill_shards[0] = 1;
	kill_shards[1] = 2;
	rebuild_ec_parity_overwrite_fail_parity_internal(state, kill_shards, 2, true);
}

static void
rebuild_ec_combined_ops(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	int		rc;

	print_message("BEGIN %s\n", __FUNCTION__);
	if (!test_runable(arg, 6))
		return;

	rc = daos_pool_set_prop(arg->pool.pool_uuid, "self_heal", "delay_rebuild");
	assert_int_equal(rc, 0);
	oid = daos_test_oid_gen(arg->coh, OC_EC_4P2GX, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	write_ec_full(&req, arg->index, 0);
	ioreq_fini(&req);

	arg->no_rebuild = 1;
	rebuild_single_pool_rank(arg, 5, true);
	print_message("sleep 30 seconds for rebuild to be scheduled/delay \n");
	sleep(30);
	extend_single_pool_rank(arg, 6);
	print_message("sleep 5 seconds for extend be scheduled/combined \n");
	sleep(5);

	arg->no_rebuild = 0;
	if (arg->myrank == 0)
		test_rebuild_wait(&arg, 1);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	verify_ec_full(&req, arg->index, 0);
	ioreq_fini(&req);

	reintegrate_single_pool_rank(arg, 5, true);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	verify_ec_full(&req, arg->index, 0);
	ioreq_fini(&req);
	print_message("END %s\n", __FUNCTION__);
}

/** create a new pool/container for each test */
static const struct CMUnitTest rebuild_tests[] = {
	{"REBUILD0: rebuild partial update with data tgt fail",
	 rebuild_partial_fail_data, rebuild_ec_8nodes_rf1_setup, test_teardown},
	{"REBUILD1: rebuild partial update with parity tgt fail",
	 rebuild_partial_fail_parity, rebuild_ec_8nodes_rf1_setup, test_teardown},
	{"REBUILD2: rebuild full stripe update with data tgt fail",
	 rebuild_full_fail_data, rebuild_ec_8nodes_rf1_setup, test_teardown},
	{"REBUILD3: rebuild full stripe update with parity tgt fail",
	 rebuild_full_fail_parity, rebuild_ec_8nodes_rf1_setup, test_teardown},
	{"REBUILD4: rebuild full then partial update with data tgt fail",
	 rebuild_full_partial_fail_data, rebuild_ec_8nodes_rf1_setup,
	 test_teardown},
	{"REBUILD5: rebuild full then partial update with parity tgt fail",
	 rebuild_full_partial_fail_parity, rebuild_ec_8nodes_rf1_setup,
	 test_teardown},
	{"REBUILD6: rebuild partial then full update with data tgt fail",
	 rebuild_partial_full_fail_data, rebuild_ec_8nodes_rf1_setup,
	 test_teardown},
	{"REBUILD7: rebuild partial then full update with parity tgt fail",
	 rebuild_partial_full_fail_parity, rebuild_ec_8nodes_rf1_setup,
	 test_teardown},
	{"REBUILD8: rebuild2p partial update with data tgt fail ",
	 rebuild2p_partial_fail_data, rebuild_ec_8nodes_setup, test_teardown},
	{"REBUILD9: rebuild2p partial update with 2 data tgt fail ",
	 rebuild2p_partial_fail_2data, rebuild_ec_8nodes_setup, test_teardown},
	{"REBUILD10: rebuild2p partial update with data/parity tgts fail ",
	 rebuild2p_partial_fail_data_parity, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD11: rebuild2p partial update with parity tgt fail",
	 rebuild2p_partial_fail_parity, rebuild_ec_8nodes_setup, test_teardown},
	{"REBUILD12: rebuild2p partial update with 2 parity tgt fail",
	 rebuild2p_partial_fail_2parity, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD13: rebuild with mixed partial/full stripe",
	 rebuild_mixed_stripes, rebuild_ec_8nodes_setup, test_teardown},
	{"REBUILD14: rebuild dfs io with data(s0) tgt fail ",
	 rebuild_dfs_fail_data_s0, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD15: rebuild dfs io with data(s1) tgt fail ",
	 rebuild_dfs_fail_data_s1, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD16: rebuild dfs io with data(s1) tgt fail ",
	 rebuild_dfs_fail_data_s3, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD17: rebuild dfs io with data(s0, s1) tgt fail ",
	 rebuild_dfs_fail_2data_s0s1, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD18: rebuild dfs io with data(s0, s2) tgt fail ",
	 rebuild_dfs_fail_2data_s0s2, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD19: rebuild dfs io with data(s0, s3) tgt fail ",
	 rebuild_dfs_fail_2data_s0s3, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD20: rebuild dfs io with data(s1, s2) tgt fail ",
	 rebuild_dfs_fail_2data_s1s2, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD21: rebuild dfs io with data(s1, s3) tgt fail ",
	 rebuild_dfs_fail_2data_s1s3, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD22: rebuild dfs io with data(s2, s3) tgt fail ",
	 rebuild_dfs_fail_2data_s2s3, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD23: rebuild dfs io with 1data 1parity(s0, p1)",
	 rebuild_dfs_fail_data_parity_s0p1, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD24: rebuild dfs io with 1data 1parity(s3, p1)",
	 rebuild_dfs_fail_data_parity_s3p1, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD25: rebuild dfs io with 1data 1parity(s2, p1)",
	 rebuild_dfs_fail_data_parity_s2p1, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD26: rebuild dfs io with 1data 1parity(s0, p0)",
	 rebuild_dfs_fail_data_parity_s0p0, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD27: rebuild dfs io with 1data 1parity(s3, p0)",
	 rebuild_dfs_fail_data_parity_s3p0, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD28: rebuild dfs io with 1data 1parity(s2, p0)",
	 rebuild_dfs_fail_data_parity_s2p0, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD29: rebuild dfs io with sequential data(s0, s1) fail",
	 rebuild_dfs_fail_seq_s0s1, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD30: rebuild dfs io with sequential data(s1, s2) fail",
	 rebuild_dfs_fail_seq_s1s2, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD31: rebuild dfs io with sequential data(s2, s3) fail",
	 rebuild_dfs_fail_seq_s2s3, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD32: rebuild dfs io with sequential data(s0, s3) fail",
	 rebuild_dfs_fail_seq_s0s3, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD33: rebuild dfs io with data and parity(s0, p0) fail",
	 rebuild_dfs_fail_seq_s0p0, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD34: rebuild dfs io with data and parity(s3, p1) fail",
	 rebuild_dfs_fail_seq_s3p1, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD35: rebuild dfs io with 2 parities(p0, p1) fail",
	 rebuild_dfs_fail_seq_p0p1, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD36: rebuild multiple group EC object",
	 rebuild_multiple_group_ec_object, rebuild_ec_8nodes_rf1_setup,
	 test_teardown},
	{"REBUILD37: rebuild EC dkey enumeration",
	 rebuild_ec_dkey_enumeration, rebuild_ec_8nodes_rf1_setup,
	 test_teardown},
	{"REBUILD38: rebuild EC multi-stripes @ different epochs",
	 rebuild_ec_multi_stripes, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD39: rebuild EC parity with multiple group",
	 rebuild_ec_parity_multi_group, rebuild_ec_8nodes_rf1_setup,
	 test_teardown},
	{"REBUILD40: rebuild EC snapshot with data shard",
	 rebuild_ec_snapshot_data_shard, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD41: rebuild EC snapshot with parity shard",
	 rebuild_ec_snapshot_parity_shard, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD42: rebuild parity over write",
	 rebuild_ec_parity_overwrite, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD43: EC rebuild then aggregation",
	 rebuild_ec_then_aggregation, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD44: multiple shards in the same target after rebuild",
	 rebuild_ec_multiple_shards, rebuild_ec_6nodes_setup,
	 test_teardown},
	{"REBUILD45: multiple shards fail tgts",
	 rebuild_ec_multiple_failure_tgts, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD46: fail parity shard and data shards after overwrite",
	 rebuild_ec_overwrite_fail_parity_data, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD47: fail parity shard and data shards after overwrite with aggregation",
	 rebuild_ec_overwrite_fail_parity_data_with_parity, rebuild_ec_8nodes_setup,
	 test_teardown},
	{"REBUILD48: combine multiple rebuild operation for EC",
	 rebuild_ec_combined_ops, rebuild_ec_6nodes_setup,
	 test_teardown},
};

int
run_daos_rebuild_simple_ec_test(int rank, int size, int *sub_tests,
				int sub_tests_size)
{
	int rc = 0;

	par_barrier(PAR_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(rebuild_tests);
		sub_tests = NULL;
	}

	run_daos_sub_tests_only("DAOS_Rebuild_EC", rebuild_tests,
				ARRAY_SIZE(rebuild_tests), sub_tests,
				sub_tests_size);

	par_barrier(PAR_COMM_WORLD);

	return rc;
}
