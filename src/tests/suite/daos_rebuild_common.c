/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
 * tests/suite/daos_rebuild.c
 *
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>

static test_arg_t *save_arg;

#define REBUILD_SUBTEST_POOL_SIZE (1ULL << 30)
#define REBUILD_SMALL_POOL_SIZE (1ULL << 28)

enum REBUILD_TEST_OP_TYPE {
	RB_OP_TYPE_FAIL,
	RB_OP_TYPE_DRAIN,
	RB_OP_TYPE_ADD,
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
				    args[i]->pool.svc,
				    rank, tgt_idx);
	}
}

static void
rebuild_add_tgt(test_arg_t **args, int args_cnt, d_rank_t rank,
		int tgt_idx)
{
	int i;

	for (i = 0; i < args_cnt; i++) {
		if (!args[i]->pool.destroyed)
			daos_reint_target(args[i]->pool.pool_uuid,
					  args[i]->group,
					  args[i]->dmg_config,
					  args[i]->pool.svc,
					  rank, tgt_idx);
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
					args[i]->pool.svc,
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
			case RB_OP_TYPE_ADD:
				rebuild_add_tgt(args, args_cnt, ranks[i],
						tgts ? tgts[i] : -1);
				break;
			case RB_OP_TYPE_DRAIN:
				rebuild_drain_tgt(args, args_cnt, ranks[i],
						tgts ? tgts[i] : -1);
				break;
			}
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);

	for (i = 0; i < args_cnt; i++)
		if (args[i]->rebuild_cb)
			args[i]->rebuild_cb(args[i]);

	sleep(10); /* make sure the rebuild happens after exclude/add/kill */
	if (args[0]->myrank == 0)
		test_rebuild_wait(args, args_cnt);

	MPI_Barrier(MPI_COMM_WORLD);
	for (i = 0; i < args_cnt; i++)
		if (args[i]->rebuild_post_cb)
			args[i]->rebuild_post_cb(args[i]);
}


void
rebuild_single_pool_rank(test_arg_t *arg, d_rank_t failed_rank, bool kill)
{
	rebuild_targets(&arg, 1, &failed_rank, NULL, 1, kill, RB_OP_TYPE_FAIL);
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
				       arg->pool.svc, DAOS_PC_RW,
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
		rc = daos_cont_open(arg->pool.poh, arg->co_uuid, DAOS_COO_RW,
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
			RB_OP_TYPE_ADD);
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
	rebuild_targets(&arg, 1, &failed_rank, NULL, 1, false, RB_OP_TYPE_ADD);
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
			false, RB_OP_TYPE_ADD);
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
					  arg->dmg_config, arg->pool.svc,
					  failed_rank,
					  failed_tgts ? failed_tgts[i] : -1);
	}
	MPI_Barrier(MPI_COMM_WORLD);
}

static int
rebuild_io_obj_internal(struct ioreq *req, bool validate, daos_epoch_t eph,
			daos_epoch_t validate_eph, int index)
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
	int	j;
	int	k;
	int	l;

	D_ALLOC(large_key, LARGE_KEY_SIZE);
	if (large_key == NULL)
		return -DER_NOMEM;
	memset(large_key, 'L', LARGE_KEY_SIZE - 1);

	for (j = 0; j < DKEY_LOOP; j++) {
		req->iod_type = DAOS_IOD_ARRAY;
		/* small records */
		sprintf(dkey, "dkey_%d_%d", index, j);
		sprintf(data, "%s_"DF_U64, "data", eph);
		sprintf(data_verify, "%s_"DF_U64, "data", validate_eph);
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
					if (l == 7)
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
					if (l == 7)
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
		sprintf(data, "%s_"DF_U64, "single_data", eph);
		sprintf(data_verify, "%s_"DF_U64, "single_data",
			validate_eph);
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
	daos_epoch_t	eph = arg->hce + arg->index * 2 + 1;
	int		i;
	int		punch_idx = 1;

	print_message("update obj %d eph "DF_U64" before rebuild\n", oids_nr,
		      eph);

	for (i = 0; i < oids_nr; i++) {
		ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);
		if (i == punch_idx) {
			punch_obj(DAOS_TX_NONE, &req);
		} else {
			rebuild_io_obj_internal((&req), false, eph, -1,
						 arg->index);
		}
		ioreq_fini(&req);
	}
}

void
rebuild_io_validate(test_arg_t *arg, daos_obj_id_t *oids, int oids_nr,
		    bool discard)
{
	int	rc;
	int	i;

	for (i = 0; i < oids_nr; i++) {
		/* XXX: skip punch object. */
		if (i == 1)
			continue;

		rc = daos_obj_verify(arg->coh, oids[i], DAOS_EPOCH_MAX);
		assert_int_equal(rc, 0);
	}
}

/* Create a new pool for the sub_test */
int
rebuild_pool_create(test_arg_t **new_arg, test_arg_t *old_arg, int flag,
		    struct test_pool *pool)
{
	int rc;

	/* create/connect another pool */
	rc = test_setup((void **)new_arg, flag, old_arg->multi_rank,
			REBUILD_SUBTEST_POOL_SIZE, pool);
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
	rank = layout->ol_shards[grp_idx]->os_ranks[idx];

	print_message("idx %u grp %u rank %d\n", idx, grp_idx, rank);
	daos_obj_layout_free(layout);
	return rank;
}

void
get_killing_rank_by_oid(test_arg_t *arg, daos_obj_id_t oid, int data_nr,
			int parity_nr, d_rank_t *ranks, int *ranks_num)
{
	struct daos_oclass_attr *oca;
	uint32_t		shard = 0;
	int			idx = 0;
	int			data_idx;

	oca = daos_oclass_attr_find(oid);
	if (oca->ca_resil == DAOS_RES_REPL) {
		ranks[0] = get_rank_by_oid_shard(arg, oid, 0);
		*ranks_num = 1;
		return;
	}

	/* for EC object */
	assert_true(data_nr <= oca->u.ec.e_k);
	assert_true(parity_nr <= oca->u.ec.e_p);
	while (parity_nr-- > 0) {
		shard = oca->u.ec.e_k + oca->u.ec.e_p - 1 - idx;
		ranks[idx++] = get_rank_by_oid_shard(arg, oid, shard);
	}

	data_idx = 0;
	while (data_nr-- > 0) {
		shard = data_idx++;
		ranks[idx++] = get_rank_by_oid_shard(arg, oid, shard);
	}

	*ranks_num = idx;
}

static void
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
			REBUILD_SUBTEST_POOL_SIZE, NULL);
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
			REBUILD_SMALL_POOL_SIZE, NULL);
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

