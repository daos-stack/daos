/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * tests/suite/daos_rebuild.c
 *
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"
#include "dfs_test.h"
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>

static test_arg_t *save_arg;

enum REBUILD_TEST_OP_TYPE {
	RB_OP_TYPE_FAIL,
	RB_OP_TYPE_DRAIN,
	RB_OP_TYPE_REINT,
	RB_OP_TYPE_ADD,
	RB_OP_TYPE_RECLAIM,
};

static void
rebuild_exclude_tgt(test_arg_t **args, int arg_cnt, d_rank_t rank,
		    int tgt_idx, bool kill)
{
	int i;

	if (kill) {
		daos_kill_server(args[0], args[0]->pool.pool_uuid,
				 args[0]->group, args[0]->pool.alive_svc,
				 rank);
		/* If one rank is killed, then it has to exclude all
		 * targets on this rank.
		 **/
		D_ASSERT(tgt_idx == -1);
		return;
	}

	for (i = 0; i < arg_cnt; i++) {
		daos_exclude_target(args[i]->pool.pool_uuid,
				    args[i]->group, args[i]->dmg_config,
				    rank, tgt_idx);
	}
}

static void
rebuild_reint_tgt(test_arg_t **args, int args_cnt, d_rank_t rank,
		int tgt_idx)
{
	int i;

	for (i = 0; i < args_cnt; i++) {
		if (!args[i]->pool.destroyed)
			daos_reint_target(args[i]->pool.pool_uuid,
					  args[i]->group,
					  args[i]->dmg_config,
					  rank, tgt_idx);
		sleep(2);
	}
}

static void
rebuild_extend_tgt(test_arg_t **args, int args_cnt, d_rank_t rank,
		   int tgt_idx, daos_size_t nvme_size)
{
	int i;

	for (i = 0; i < args_cnt; i++) {
		if (!args[i]->pool.destroyed)
			daos_extend_target(args[i]->pool.pool_uuid,
					   args[i]->group,
					   args[i]->dmg_config,
					   rank, tgt_idx, nvme_size);
		sleep(2);
	}
}

static void
rebuild_drain_tgt(test_arg_t **args, int args_cnt, d_rank_t rank,
		int tgt_idx)
{
	int i;

	for (i = 0; i < args_cnt; i++) {
		if (!args[i]->pool.destroyed)
			daos_drain_target(args[i]->pool.pool_uuid,
					args[i]->group,
					args[i]->dmg_config,
					rank, tgt_idx);
		sleep(2);
	}
}

static void
rebuild_targets(test_arg_t **args, int args_cnt, d_rank_t *ranks,
		int *tgts, int rank_nr, bool kill,
		enum REBUILD_TEST_OP_TYPE op_type)
{
	int i;

	for (i = 0; i < args_cnt; i++)
		if (args[i]->rebuild_pre_cb)
			args[i]->rebuild_pre_cb(args[i]);

	MPI_Barrier(MPI_COMM_WORLD);
	/** include or exclude the target from the pool */
	if (args[0]->myrank == 0) {
		for (i = 0; i < rank_nr; i++) {
			switch (op_type) {
			case RB_OP_TYPE_FAIL:
				rebuild_exclude_tgt(args, args_cnt,
						ranks[i], tgts ? tgts[i] : -1,
						kill);
				break;
			case RB_OP_TYPE_REINT:
				rebuild_reint_tgt(args, args_cnt, ranks[i],
						tgts ? tgts[i] : -1);
				break;
			case RB_OP_TYPE_ADD:
				rebuild_extend_tgt(args, args_cnt, ranks[i],
						   tgts ? tgts[i] : -1,
						   args[i]->pool.pool_size);
				break;
			case RB_OP_TYPE_DRAIN:
				rebuild_drain_tgt(args, args_cnt, ranks[i],
						tgts ? tgts[i] : -1);
				break;
			case RB_OP_TYPE_RECLAIM:
				/*
				 * There is no externally accessible operation
				 * that triggers reclaim. It is automatically
				 * scheduled after reintegration or addition
				 */
				D_ASSERT(op_type != RB_OP_TYPE_RECLAIM);
				break;
			}
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);

	for (i = 0; i < args_cnt; i++)
		if (args[i]->rebuild_cb)
			args[i]->rebuild_cb(args[i]);

	sleep(10); /* make sure the rebuild happens after exclude/add/kill */
	if (args[0]->myrank == 0 && !args[0]->no_rebuild)
		test_rebuild_wait(args, args_cnt);

	MPI_Barrier(MPI_COMM_WORLD);
	for (i = 0; i < args_cnt; i++) {
		daos_cont_status_clear(args[i]->coh, NULL);

		if (args[i]->rebuild_post_cb)
			args[i]->rebuild_post_cb(args[i]);
	}
}


void
rebuild_single_pool_rank(test_arg_t *arg, d_rank_t failed_rank, bool kill)
{
	rebuild_targets(&arg, 1, &failed_rank, NULL, 1, kill, RB_OP_TYPE_FAIL);
}

void
reintegrate_single_pool_rank_no_disconnect(test_arg_t *arg, d_rank_t failed_rank)
{
	rebuild_targets(&arg, 1, &failed_rank, NULL, 1, false, RB_OP_TYPE_REINT);
}

void
rebuild_pools_ranks(test_arg_t **args, int args_cnt, d_rank_t *failed_ranks,
		    int ranks_nr, bool kill)
{
	rebuild_targets(args, args_cnt, failed_ranks, NULL, ranks_nr,
			kill, RB_OP_TYPE_FAIL);
}

void
rebuild_single_pool_target(test_arg_t *arg, d_rank_t failed_rank,
			   int failed_tgt, bool kill)
{
	rebuild_targets(&arg, 1, &failed_rank, &failed_tgt, 1, kill,
			RB_OP_TYPE_FAIL);
}

void
drain_single_pool_target(test_arg_t *arg, d_rank_t failed_rank,
			   int failed_tgt, bool kill)
{
	rebuild_targets(&arg, 1, &failed_rank, &failed_tgt, 1, kill,
			RB_OP_TYPE_DRAIN);
}

void
drain_single_pool_rank(test_arg_t *arg, d_rank_t failed_rank, bool kill)
{
	rebuild_targets(&arg, 1, &failed_rank, NULL, 1, kill, RB_OP_TYPE_DRAIN);
}

void
extend_single_pool_rank(test_arg_t *arg, d_rank_t failed_rank)
{
	rebuild_targets(&arg, 1, &failed_rank, NULL, 1, false, RB_OP_TYPE_ADD);
}

void
drain_pools_ranks(test_arg_t **args, int args_cnt, d_rank_t *failed_ranks,
		    int ranks_nr, bool kill)
{
	rebuild_targets(args, args_cnt, failed_ranks, NULL, ranks_nr, kill,
			RB_OP_TYPE_DRAIN);
}

int
rebuild_pool_disconnect_internal(void *data)
{
	test_arg_t      *arg = data;
	int             rc = 0;
	int             rc_reduce = 0;

	/* Close cont and disconnect pool */
	rc = daos_cont_close(arg->coh, NULL);
	if (arg->multi_rank) {
		MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN,
		MPI_COMM_WORLD);
		rc = rc_reduce;
	}
	print_message("container close "DF_UUIDF"\n",
	DP_UUID(arg->co_uuid));
	if (rc) {
		print_message("failed to close container "DF_UUIDF
		": %d\n", DP_UUID(arg->co_uuid), rc);
		return rc;
	}

	arg->coh = DAOS_HDL_INVAL;
	rc = daos_pool_disconnect(arg->pool.poh, NULL /* ev */);
	if (rc)
		print_message("failed to disconnect pool "DF_UUIDF
			": %d\n", DP_UUID(arg->pool.pool_uuid), rc);

	print_message("pool disconnect "DF_UUIDF"\n",
	DP_UUID(arg->pool.pool_uuid));

	arg->pool.poh = DAOS_HDL_INVAL;
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}

int
rebuild_pool_connect_internal(void *data)
{
	test_arg_t      *arg = data;
	int             rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
				       DAOS_PC_RW,
				       &arg->pool.poh, &arg->pool.pool_info,
				       NULL /* ev */);
		if (rc)
			print_message("daos_pool_connect failed, rc: %d\n", rc);

		print_message("pool connect "DF_UUIDF"\n",
		DP_UUID(arg->pool.pool_uuid));
	}
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** broadcast pool info */
	if (arg->multi_rank) {
		MPI_Bcast(&arg->pool.pool_info, sizeof(arg->pool.pool_info),
		MPI_CHAR, 0, MPI_COMM_WORLD);
		handle_share(&arg->pool.poh, HANDLE_POOL, arg->myrank,
		arg->pool.poh, 0);
	}

	/** open container */
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = daos_cont_open(arg->pool.poh, arg->co_uuid,
				    DAOS_COO_RW | DAOS_COO_FORCE,
				    &arg->coh, &arg->co_info, NULL);
		if (rc)
			print_message("daos_cont_open failed, rc: %d\n", rc);

		print_message("container open "DF_UUIDF"\n",
		DP_UUID(arg->co_uuid));
	}
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** broadcast container info */
	if (arg->multi_rank) {
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		handle_share(&arg->coh, HANDLE_CO, arg->myrank, arg->pool.poh,
		0);
	}

	return 0;
}


void
reintegrate_single_pool_target(test_arg_t *arg, d_rank_t failed_rank,
			       int failed_tgt)
{
	/* XXX: Disconnecting and reconnecting is necessary for the time being
	 * while reintegration only supports "offline" mode and without
	 * incremental reintegration. Disconnecting from the pool allows the
	 * containers to be deleted before reintegration occurs
	 *
	 * Once incremental reintegration support is added, this should be
	 * removed
	 */
	rebuild_pool_disconnect_internal(arg);
	rebuild_targets(&arg, 1, &failed_rank, &failed_tgt, 1, false,
			RB_OP_TYPE_REINT);
	rebuild_pool_connect_internal(arg);
}

void
reintegrate_single_pool_rank(test_arg_t *arg, d_rank_t failed_rank)
{

	/* XXX: Disconnecting and reconnecting is necessary for the time being
	 * while reintegration only supports "offline" mode and without
	 * incremental reintegration. Disconnecting from the pool allows the
	 * containers to be deleted before reintegration occurs
	 *
	 * Once incremental reintegration support is added, this should be
	 * removed
	 */
	rebuild_pool_disconnect_internal(arg);
	rebuild_targets(&arg, 1, &failed_rank, NULL, 1, false,
			RB_OP_TYPE_REINT);
	rebuild_pool_connect_internal(arg);
}

void
reintegrate_pools_ranks(test_arg_t **args, int args_cnt, d_rank_t *failed_ranks,
			int ranks_nr)
{
	int i;

	/* XXX: Disconnecting and reconnecting is necessary for the time being
	 * while reintegration only supports "offline" mode and without
	 * incremental reintegration. Disconnecting from the pool allows the
	 * containers to be deleted before reintegration occurs
	 *
	 * Once incremental reintegration support is added, this should be
	 * removed
	 */
	for (i = 0; i < args_cnt; i++)
		rebuild_pool_disconnect_internal(args[i]);
	rebuild_targets(args, args_cnt, failed_ranks, NULL, ranks_nr,
			false, RB_OP_TYPE_REINT);
	for (i = 0; i < args_cnt; i++)
		rebuild_pool_connect_internal(args[i]);
}


void
rebuild_add_back_tgts(test_arg_t *arg, d_rank_t failed_rank, int *failed_tgts,
		      int nr)
{
	MPI_Barrier(MPI_COMM_WORLD);
	/* Add back the target if it is not being killed */
	if (arg->myrank == 0 && !arg->pool.destroyed) {
		int i;

		for (i = 0; i < nr; i++)
			daos_reint_target(arg->pool.pool_uuid, arg->group,
					  arg->dmg_config,
					  failed_rank,
					  failed_tgts ? failed_tgts[i] : -1);
	}
	MPI_Barrier(MPI_COMM_WORLD);
}

static int
rebuild_io_obj_internal(struct ioreq *req, bool validate, int index)
{
#define BULK_SIZE	5000
#define REC_SIZE	64
#define LARGE_KEY_SIZE	(512 * 1024)
#define DKEY_LOOP	3
#define AKEY_LOOP	3
#define REC_LOOP	10
	char	dkey[32];
	char	akey[32];
	char	data[REC_SIZE];
	char	data_verify[REC_SIZE];
	char	*large_key;
	int	akey_punch_idx = 1;
	int	dkey_punch_idx = 1;
	int	rec_punch_idx = 2;
	int	large_key_idx = 7;
	int	j;
	int	k;
	int	l;

	D_ALLOC(large_key, LARGE_KEY_SIZE);
	if (large_key == NULL)
		return -DER_NOMEM;
	memset(large_key, 'L', LARGE_KEY_SIZE - 1);
	sprintf(data, "data");
	sprintf(data_verify, "data");
	for (j = 0; j < DKEY_LOOP; j++) {
		req->iod_type = DAOS_IOD_ARRAY;
		/* small records */
		sprintf(dkey, "dkey_%d_%d", index, j);
		for (k = 0; k < AKEY_LOOP; k++) {
			sprintf(akey, "akey_%d_%d", index, k);
			for (l = 0; l < REC_LOOP; l++) {
				if (validate) {
					/* How to verify punch? XXX */
					if (k == akey_punch_idx ||
					    j == dkey_punch_idx ||
					    l == rec_punch_idx)
						continue;
					memset(data, 0, REC_SIZE);
					if (l == large_key_idx)
						lookup_single(large_key, akey,
							      l, data, REC_SIZE,
							      DAOS_TX_NONE,
							      req);
					else
						lookup_single(dkey, akey, l,
							      data, REC_SIZE,
							      DAOS_TX_NONE,
							      req);
					assert_memory_equal(data, data_verify,
						    strlen(data_verify));
				} else {
					if (l == large_key_idx)
						insert_single(large_key, akey,
							l, data,
							strlen(data) + 1,
							DAOS_TX_NONE, req);
					else if (l == rec_punch_idx)
						punch_single(dkey, akey, l,
							     DAOS_TX_NONE,
							     req);
					else
						insert_single(dkey, akey, l,
							data, strlen(data) + 1,
							DAOS_TX_NONE, req);
				}
			}

			/* Punch akey */
			if (k == akey_punch_idx && !validate)
				punch_akey(dkey, akey, DAOS_TX_NONE, req);
		}

		/* large records */
		for (k = 0; k < 2; k++) {
			char bulk[BULK_SIZE+10];
			char compare[BULK_SIZE];

			sprintf(akey, "akey_bulk_%d_%d", index, k);
			memset(compare, 'a', BULK_SIZE);
			for (l = 0; l < 5; l++) {
				if (validate) {
					/* How to verify punch? XXX */
					if (k == akey_punch_idx ||
					    j == dkey_punch_idx)
						continue;
					memset(bulk, 0, BULK_SIZE);
					lookup_single(dkey, akey, l,
						      bulk, BULK_SIZE + 10,
						      DAOS_TX_NONE, req);
					assert_memory_equal(bulk, compare,
							    BULK_SIZE);
				} else {
					memset(bulk, 'a', BULK_SIZE);
					insert_single(dkey, akey, l,
						      bulk, BULK_SIZE,
						      DAOS_TX_NONE, req);
				}
			}

			/* Punch akey */
			if (k == akey_punch_idx && !validate)
				punch_akey(dkey, akey, DAOS_TX_NONE, req);
		}

		/* Punch dkey */
		if (j == dkey_punch_idx && !validate)
			punch_dkey(dkey, DAOS_TX_NONE, req);

		/* single record */
		req->iod_type = DAOS_IOD_SINGLE;
		sprintf(dkey, "dkey_single_%d_%d", index, j);
		if (validate) {
			memset(data, 0, REC_SIZE);
			lookup_single(dkey, "akey_single", 0, data, REC_SIZE,
				      DAOS_TX_NONE, req);
			assert_memory_equal(data, data_verify,
					    strlen(data_verify));
		} else {
			insert_single(dkey, "akey_single", 0, data,
				      strlen(data) + 1, DAOS_TX_NONE, req);
		}
	}

	D_FREE(large_key);
	return 0;
}

void
rebuild_io(test_arg_t *arg, daos_obj_id_t *oids, int oids_nr)
{
	struct ioreq	req;
	int		i;
	int		punch_idx = 1;

	print_message("rebuild io obj %d\n", oids_nr);
	for (i = 0; i < oids_nr; i++) {
		ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);
		if (i == punch_idx) {
			punch_obj(DAOS_TX_NONE, &req);
		} else {
			rebuild_io_obj_internal((&req), false, arg->index);
		}
		ioreq_fini(&req);
	}
}

void
rebuild_io_validate(test_arg_t *arg, daos_obj_id_t *oids, int oids_nr)
{
	struct ioreq	req;
	int		i;

	print_message("rebuild io validate obj %d\n", oids_nr);
	for (i = 0; i < oids_nr; i++) {
		/* XXX: skip punch object. */
		if (i == 1)
			continue;
		ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);
		rebuild_io_obj_internal((&req), true, arg->index);
		ioreq_fini(&req);
	}
}

void
rebuild_io_verify(test_arg_t *arg, daos_obj_id_t *oids, int oids_nr)
{
	int	rc;
	int	i;

	rc = daos_cont_status_clear(arg->coh, NULL);
	assert_rc_equal(rc, 0);

	print_message("rebuild io verify obj %d\n", oids_nr);
	for (i = 0; i < oids_nr; i++) {
		/* XXX: skip punch object. */
		if (i == 1)
			continue;

		rc = daos_obj_verify(arg->coh, oids[i], DAOS_EPOCH_MAX);
		assert_rc_equal(rc, 0);
	}
}

/* using some un-aligned size */
#define DATA_SIZE			(1048576 * 4 + 347)
#define PARTIAL_DATA_SIZE		(933)
#define IOD3_DATA_SIZE			(311)
#define LARGE_SINGLE_VALUE_SIZE		(8569)
#define SMALL_SINGLE_VALUE_SIZE		(37)

#define KEY_NR 5
void make_buffer(char *buffer, char start, int total)
{
	int i = 0;

	while (total > 0) {
		char tmp;
		int size = min(total, 1000);

		tmp = start + (i++) % 25;
		memset(buffer, tmp, size);
		buffer += size;
		total -= size;
	}
}

static void
write_ec(struct ioreq *req, int index, char *data, daos_off_t off, int size)
{
	char		key[32];
	daos_recx_t	recx;
	int		i;
	char		single_data[LARGE_SINGLE_VALUE_SIZE];

	for (i = 0; i < KEY_NR; i++) {
		req->iod_type = DAOS_IOD_ARRAY;
		sprintf(key, "dkey_%d", index);
		recx.rx_nr = size;
		recx.rx_idx = off + i * 10485760;
		insert_recxs(key, "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, size, req);
		recx.rx_nr = IOD3_DATA_SIZE;
		insert_recxs(key, "a_key_iod3", 3, DAOS_TX_NONE, &recx, 1,
			     data, IOD3_DATA_SIZE * 3, req);

		req->iod_type = DAOS_IOD_SINGLE;
		memset(single_data, 'a' + i, LARGE_SINGLE_VALUE_SIZE);
		sprintf(key, "dkey_single_small_%d_%d", index, i);
		insert_single(key, "a_key", 0, single_data,
			      SMALL_SINGLE_VALUE_SIZE, DAOS_TX_NONE,
			      req);

		sprintf(key, "dkey_single_large_%d_%d", index, i);
		insert_single(key, "a_key", 0, single_data,
			      LARGE_SINGLE_VALUE_SIZE, DAOS_TX_NONE, req);
	}
}

static void
verify_ec(struct ioreq *req, int index, char *verify_data, daos_off_t off,
	  daos_size_t size)
{
	char		key[32];
	char		key_buf[32];
	const char	*akey = key_buf;
	void		*read_data;
	char		single_buf[LARGE_SINGLE_VALUE_SIZE];
	void		*single_data = single_buf;
	char		verify_single_data[LARGE_SINGLE_VALUE_SIZE];
	int		i;

	read_data = (char *)malloc(size);
	for (i = 0; i < KEY_NR; i++) {
		uint64_t	offset = off + i * 10485760;
		uint64_t	idx = 0;
		daos_size_t	read_size = 0;
		daos_size_t	iod_size = 1;
		daos_size_t	single_data_size;
		daos_size_t	iod3_datasize = IOD3_DATA_SIZE * 3;
		daos_size_t	datasize = size;

		req->iod_type = DAOS_IOD_ARRAY;
		sprintf(key, "dkey_%d", index);
		sprintf(key_buf, "a_key");
		memset(read_data, 0, size);
		lookup(key, 1, &akey, &offset, &iod_size,
		       &read_data, &datasize, DAOS_TX_NONE, req, false);
		assert_memory_equal(read_data, verify_data, datasize);
		assert_int_equal(iod_size, 1);

		sprintf(key_buf, "a_key_iod3");
		memset(read_data, 0, size);
		lookup(key, 1, &akey, &offset, &iod_size, &read_data,
		       &iod3_datasize, DAOS_TX_NONE, req, false);
		assert_int_equal(iod_size, 3);
		assert_memory_equal(read_data, verify_data, iod3_datasize);

		memset(verify_single_data, 'a' + i, LARGE_SINGLE_VALUE_SIZE);

		req->iod_type = DAOS_IOD_SINGLE;
		memset(single_data, 0, SMALL_SINGLE_VALUE_SIZE);
		single_data_size = SMALL_SINGLE_VALUE_SIZE;
		sprintf(key, "dkey_single_small_%d_%d", index, i);
		sprintf(key_buf, "a_key");
		lookup(key, 1, &akey, &idx, &read_size, &single_data,
		       &single_data_size, DAOS_TX_NONE, req, false);
		assert_int_equal(read_size, SMALL_SINGLE_VALUE_SIZE);
		assert_memory_equal(single_data, verify_single_data,
				    SMALL_SINGLE_VALUE_SIZE);

		idx = 0;
		read_size = 0;
		single_data_size = LARGE_SINGLE_VALUE_SIZE;
		memset(single_data, 0, LARGE_SINGLE_VALUE_SIZE);
		sprintf(key, "dkey_single_large_%d_%d", index, i);
		lookup(key, 1, &akey, &idx, &read_size, &single_data,
		       &single_data_size, DAOS_TX_NONE, req, false);
		assert_int_equal(read_size, LARGE_SINGLE_VALUE_SIZE);
		assert_memory_equal(single_data, verify_single_data,
				    LARGE_SINGLE_VALUE_SIZE);
	}
	free(read_data);
}

void
write_ec_partial(struct ioreq *req, int test_idx, daos_off_t off)
{
	char	*buffer;

	buffer = (char *)malloc(PARTIAL_DATA_SIZE);
	make_buffer(buffer, 'a', PARTIAL_DATA_SIZE);
	write_ec(req, test_idx, buffer, off, PARTIAL_DATA_SIZE);
	free(buffer);
}

void
verify_ec_partial(struct ioreq *req, int test_idx, daos_off_t off)
{
	char	*buffer;

	buffer = (char *)malloc(PARTIAL_DATA_SIZE);
	make_buffer(buffer, 'a', PARTIAL_DATA_SIZE);
	verify_ec(req, test_idx, buffer, off, PARTIAL_DATA_SIZE);
	free(buffer);
}

void
write_ec_full(struct ioreq *req, int test_idx, daos_off_t off)
{
	char	*buffer;

	buffer = (char *)malloc(DATA_SIZE);
	make_buffer(buffer, 'b', DATA_SIZE);
	write_ec(req, test_idx, buffer, off, DATA_SIZE);
	free(buffer);
}

void
verify_ec_full(struct ioreq *req, int test_idx, daos_off_t off)
{
	char	*buffer;

	buffer = (char *)malloc(DATA_SIZE);
	make_buffer(buffer, 'b', DATA_SIZE);
	verify_ec(req, test_idx, buffer, off, DATA_SIZE);
	free(buffer);
}

void
write_ec_full_partial(struct ioreq *req, int test_idx, daos_off_t off)
{
	write_ec_full(req, test_idx, off);
	write_ec_partial(req, test_idx, off);
}

void
write_ec_partial_full(struct ioreq *req, int test_idx, daos_off_t off)
{
	write_ec_partial(req, test_idx, off);
	write_ec_full(req, test_idx, off);
}

void
verify_ec_full_partial(struct ioreq *req, int test_idx, daos_off_t off)
{
	char	*buffer;

	buffer = (char *)malloc(DATA_SIZE);
	make_buffer(buffer, 'b', DATA_SIZE);
	make_buffer(buffer, 'a', PARTIAL_DATA_SIZE);
	verify_ec(req, test_idx, buffer, off, DATA_SIZE);
	free(buffer);
}

void
dfs_ec_rebuild_io(void **state, int *shards, int shards_nr)
{
	dfs_t		*dfs_mt;
	daos_handle_t	co_hdl;
	test_arg_t	*arg = *state;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	dfs_obj_t	*obj;
	daos_size_t	buf_size = 32 * 1024 * 32;
	daos_size_t	partial_size = 32 * 1024 * 2;
	daos_size_t	chunk_size = 32 * 1024 * 4;
	daos_size_t	fetch_size = 0;
	uuid_t		co_uuid;
	char		filename[32];
	d_rank_t	ranks[4] = { -1 };
	int		idx = 0;
	daos_obj_id_t	oid;
	char		*buf;
	char		*vbuf;
	int		i;
	int		rc;

	uuid_generate(co_uuid);
	rc = dfs_cont_create(arg->pool.poh, co_uuid, NULL, &co_hdl,
			     &dfs_mt);
	assert_int_equal(rc, 0);
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
	sprintf(filename, "degrade_file");
	rc = dfs_open(dfs_mt, NULL, filename, S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT, DAOS_OC_EC_K4P2_L32K, chunk_size,
		      NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_write(dfs_mt, obj, &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	/* Partial update after that */
	d_iov_set(&iov, buf, partial_size);
	for (i = 0; i < 10; i++) {
		dfs_write(dfs_mt, obj, &sgl, buf_size + i * 100 * 1024, NULL);
		assert_int_equal(rc, 0);
	}

	dfs_obj2id(obj, &oid);
	while (shards_nr-- > 0) {
		ranks[idx] = get_rank_by_oid_shard(arg, oid, shards[idx]);
		idx++;
	}
	rebuild_pools_ranks(&arg, 1, ranks, idx, false);
	daos_cont_status_clear(co_hdl, NULL);

	/* Verify full stripe */
	d_iov_set(&iov, buf, buf_size);
	fetch_size = 0;
	rc = dfs_read(dfs_mt, obj, &sgl, 0, &fetch_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(fetch_size, buf_size);
	assert_memory_equal(buf, vbuf, buf_size);

	/* Verify partial stripe */
	d_iov_set(&iov, buf, partial_size);
	for (i = 0; i < 10; i++) {
		memset(buf, 0, buf_size);
		fetch_size = 0;
		dfs_read(dfs_mt, obj, &sgl, buf_size + i * 100 * 1024,
			 &fetch_size, NULL);
		assert_int_equal(rc, 0);
		assert_int_equal(fetch_size, partial_size);
		assert_memory_equal(buf, vbuf, partial_size);
	}

	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	D_FREE(buf);
	rc = dfs_umount(dfs_mt);
	assert_int_equal(rc, 0);

	rc = daos_cont_close(co_hdl, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_cont_destroy(arg->pool.poh, co_uuid, 1, NULL);
	assert_rc_equal(rc, 0);

#if 0
	while (idx > 0)
		rebuild_add_back_tgts(arg, ranks[--idx], NULL, 1);
#endif
}

/* Create a new pool for the sub_test */
int
rebuild_pool_create(test_arg_t **new_arg, test_arg_t *old_arg, int flag,
		    struct test_pool *pool)
{
	int rc;

	/* create/connect another pool */
	rc = test_setup((void **)new_arg, flag, old_arg->multi_rank,
			REBUILD_SUBTEST_POOL_SIZE, 0, pool);
	if (rc) {
		print_message("open/connect another pool failed: rc %d\n", rc);
		return rc;
	}

	(*new_arg)->index = old_arg->index;
	return 0;
}

/* Destroy the pool for the sub test */
void
rebuild_pool_destroy(test_arg_t *arg)
{
	test_teardown((void **)&arg);
	/* make sure IV and GC release refcount on pool and free space,
	* otherwise rebuild test might run into ENOSPACE
	*/
	sleep(1);
}

d_rank_t
get_rank_by_oid_shard(test_arg_t *arg, daos_obj_id_t oid,
		      uint32_t shard)
{
	struct daos_obj_layout	*layout;
	uint32_t		grp_idx;
	uint32_t		idx;
	d_rank_t		rank;

	daos_obj_layout_get(arg->coh, oid, &layout);
	grp_idx = shard / layout->ol_shards[0]->os_replica_nr;
	idx = shard % layout->ol_shards[0]->os_replica_nr;
	rank = layout->ol_shards[grp_idx]->os_shard_loc[idx].sd_rank;

	print_message("idx %u grp %u rank %d\n", idx, grp_idx, rank);
	daos_obj_layout_free(layout);
	return rank;
}

uint32_t
get_tgt_idx_by_oid_shard(test_arg_t *arg, daos_obj_id_t oid,
			 uint32_t shard)
{
	struct daos_obj_layout	*layout;
	uint32_t		grp_idx;
	uint32_t		idx;
	uint32_t		tgt_idx;

	daos_obj_layout_get(arg->coh, oid, &layout);
	grp_idx = shard / layout->ol_shards[0]->os_replica_nr;
	idx = shard % layout->ol_shards[0]->os_replica_nr;
	tgt_idx = layout->ol_shards[grp_idx]->os_shard_loc[idx].sd_tgt_idx;

	print_message("idx %u grp %u tgt_idx %d\n", idx, grp_idx, tgt_idx);
	daos_obj_layout_free(layout);
	return tgt_idx;
}

int
ec_data_nr_get(daos_obj_id_t oid)
{
	struct daos_oclass_attr *oca;

	oca = daos_oclass_attr_find(oid, NULL);
	assert_true(oca->ca_resil == DAOS_RES_EC);
	return oca->u.ec.e_k;
}

int
ec_parity_nr_get(daos_obj_id_t oid)
{
	struct daos_oclass_attr *oca;

	oca = daos_oclass_attr_find(oid, NULL);
	assert_true(oca->ca_resil == DAOS_RES_EC);
	return oca->u.ec.e_p;
}

void
get_killing_rank_by_oid(test_arg_t *arg, daos_obj_id_t oid, int data_nr,
			int parity_nr, d_rank_t *ranks, int *ranks_num)
{
	struct daos_oclass_attr *oca;
	uint32_t		shard;
	int			idx = 0;

	oca = daos_oclass_attr_find(oid, NULL);
	if (oca->ca_resil == DAOS_RES_REPL) {
		ranks[0] = get_rank_by_oid_shard(arg, oid, 0);
		if (ranks_num)
			*ranks_num = 1;
		return;
	}

	/* for EC object */
	assert_true(data_nr <= oca->u.ec.e_k);
	assert_true(parity_nr <= oca->u.ec.e_p);
	shard = oca->u.ec.e_k + oca->u.ec.e_p - 1;
	while (parity_nr-- > 0)
		ranks[idx++] = get_rank_by_oid_shard(arg, oid, shard--);

	shard = 0;
	while (data_nr-- > 0) {
		ranks[idx++] = get_rank_by_oid_shard(arg, oid, shard);
		shard = shard + 2;
		if (shard > oca->u.ec.e_k)
			break;
	}

	if (ranks_num)
		*ranks_num = idx;
}

void
save_group_state(void **state)
{
	if (state != NULL && *state != NULL) {
		save_arg = *state;
		*state = NULL;
	}
}

static void
restore_group_state(void **state)
{
	if (state != NULL && save_arg != NULL) {
		*state = save_arg;
		save_arg = NULL;
	}
}

int
rebuild_sub_setup(void **state)
{
	test_arg_t	*arg;
	int		rc;

	save_group_state(state);
	rc = test_setup(state, SETUP_CONT_CONNECT, true,
			REBUILD_SUBTEST_POOL_SIZE, 0, NULL);
	if (rc)
		return rc;

	arg = *state;
	if (dt_obj_class != DAOS_OC_UNKNOWN)
		arg->obj_class = dt_obj_class;
	else
		arg->obj_class = DAOS_OC_R3S_SPEC_RANK;

	return 0;
}

int
rebuild_small_sub_setup(void **state)
{
	test_arg_t	*arg;
	int rc;

	save_group_state(state);
	rc = test_setup(state, SETUP_CONT_CONNECT, true,
			REBUILD_SMALL_POOL_SIZE, 0, NULL);
	if (rc)
		return rc;

	arg = *state;
	if (dt_obj_class != DAOS_OC_UNKNOWN)
		arg->obj_class = dt_obj_class;
	else
		arg->obj_class = DAOS_OC_R3S_SPEC_RANK;

	return 0;
}

int
rebuild_sub_teardown(void **state)
{
	int rc;

	rc = test_teardown(state);
	restore_group_state(state);

	return rc;
}

