/**
 * (C) Copyright 2018 Intel Corporation.
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
 * tests/suite/daos_test_common
 */
#define D_LOGFAC	DD_FAC(tests)
#include "daos_test.h"

/** Server crt group ID */
const char *server_group;

/** Pool service replicas */
unsigned int svc_nreplicas = 1;

static int
test_setup_pool_create(void **state, struct test_pool *pool, daos_prop_t *prop)
{
	test_arg_t *arg = *state;
	int rc;

	if (pool != NULL) {
		/* copy the info from passed in pool */
		assert_int_equal(pool->slave, 0);
		arg->pool.pool_size = pool->pool_size;
		uuid_copy(arg->pool.pool_uuid, pool->pool_uuid);
		arg->pool.svc.rl_nr = pool->svc.rl_nr;
		memcpy(arg->pool.ranks, pool->ranks,
		       sizeof(arg->pool.ranks[0]) * TEST_RANKS_MAX_NUM);
		arg->pool.slave = 1;
		if (arg->multi_rank)
			MPI_Barrier(MPI_COMM_WORLD);
		return 0;
	}

	if (arg->myrank == 0) {
		char		*env;
		int		 size_gb;
		daos_size_t	 nvme_size;

		env = getenv("POOL_SCM_SIZE");
		if (env) {
			size_gb = atoi(env);
			if (size_gb != 0)
				arg->pool.pool_size =
					(daos_size_t)size_gb << 30;
		}

		/*
		 * Set the default NVMe partition size to "2 * scm_size", so
		 * that we need to specify SCM size only for each test case.
		 *
		 * Set env POOL_NVME_SIZE to overwrite the default NVMe size.
		 */
		nvme_size = arg->pool.pool_size * 2;
		env = getenv("POOL_NVME_SIZE");
		if (env) {
			size_gb = atoi(env);
			nvme_size = (daos_size_t)size_gb << 30;
		}

		print_message("setup: creating pool, SCM size="DF_U64" GB, "
			      "NVMe size="DF_U64" GB\n",
			      (arg->pool.pool_size >> 30), nvme_size >> 30);
		rc = daos_pool_create(arg->mode, arg->uid, arg->gid, arg->group,
				      NULL, "pmem", arg->pool.pool_size,
				      nvme_size, prop, &arg->pool.svc,
				      arg->pool.pool_uuid, NULL);
		if (rc)
			print_message("daos_pool_create failed, rc: %d\n", rc);
		else
			print_message("setup: created pool "DF_UUIDF"\n",
				       DP_UUID(arg->pool.pool_uuid));
	}
	/** broadcast pool create result */
	if (arg->multi_rank) {
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		/** broadcast pool UUID and svc addresses */
		if (!rc) {
			MPI_Bcast(arg->pool.pool_uuid, 16,
				  MPI_CHAR, 0, MPI_COMM_WORLD);
			MPI_Bcast(&arg->pool.svc.rl_nr,
				  sizeof(arg->pool.svc.rl_nr),
				  MPI_CHAR, 0, MPI_COMM_WORLD);
			MPI_Bcast(arg->pool.ranks,
				  sizeof(arg->pool.ranks[0]) *
					 arg->pool.svc.rl_nr,
				  MPI_CHAR, 0, MPI_COMM_WORLD);
		}
	}
	return rc;
}

static int
test_setup_pool_connect(void **state, struct test_pool *pool)
{
	test_arg_t *arg = *state;
	int rc;

	if (pool != NULL) {
		assert_int_equal(arg->pool.slave, 1);
		assert_int_equal(pool->slave, 0);
		memcpy(&arg->pool.pool_info, &pool->pool_info,
		       sizeof(pool->pool_info));
		arg->pool.poh = pool->poh;
		if (arg->multi_rank)
			MPI_Barrier(MPI_COMM_WORLD);
		return 0;
	}

	if (arg->myrank == 0) {
		daos_pool_info_t	info;

		print_message("setup: connecting to pool\n");
		rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
				       &arg->pool.svc, DAOS_PC_RW,
				       &arg->pool.poh, &arg->pool.pool_info,
				       NULL /* ev */);
		if (rc)
			print_message("daos_pool_connect failed, rc: %d\n", rc);
		else
			print_message("connected to pool, ntarget=%d\n",
				      arg->pool.pool_info.pi_ntargets);

		if (rc == 0) {
			rc = daos_pool_query(arg->pool.poh, NULL, &info, NULL,
					     NULL);

			if (rc == 0) {
				arg->srv_ntgts = info.pi_ntargets;
				arg->srv_nnodes = info.pi_nnodes;
				arg->srv_disabled_ntgts = info.pi_ndisabled;
			}
		}
	}

	/** broadcast pool connect result */
	if (arg->multi_rank) {
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		if (!rc) {
			/** broadcast pool info */
			MPI_Bcast(&arg->pool.pool_info,
				  sizeof(arg->pool.pool_info),
				  MPI_CHAR, 0, MPI_COMM_WORLD);
			/** l2g and g2l the pool handle */
			handle_share(&arg->pool.poh, HANDLE_POOL,
				     arg->myrank, arg->pool.poh, 0);
		}
	}
	return rc;
}

static int
test_setup_cont_create(void **state, daos_prop_t *co_prop)
{
	test_arg_t *arg = *state;
	int rc;

	if (arg->myrank == 0) {
		uuid_generate(arg->co_uuid);
		print_message("setup: creating container "DF_UUIDF"\n",
			      DP_UUID(arg->co_uuid));
		rc = daos_cont_create(arg->pool.poh, arg->co_uuid, co_prop,
				      NULL);
		if (rc)
			print_message("daos_cont_create failed, rc: %d\n", rc);
	}
	/** broadcast container create result */
	if (arg->multi_rank) {
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		/** broadcast container UUID */
		if (!rc)
			MPI_Bcast(arg->co_uuid, 16,
				  MPI_CHAR, 0, MPI_COMM_WORLD);
	}
	return rc;
}


static int
test_setup_cont_open(void **state)
{
	test_arg_t *arg = *state;
	int rc;

	if (arg->myrank == 0) {
		print_message("setup: opening container\n");
		rc = daos_cont_open(arg->pool.poh, arg->co_uuid, DAOS_COO_RW,
				    &arg->coh, &arg->co_info, NULL);
		if (rc)
			print_message("daos_cont_open failed, rc: %d\n", rc);
	}
	/** broadcast container open result */
	if (arg->multi_rank) {
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		/** l2g and g2l the container handle */
		if (!rc)
			handle_share(&arg->coh, HANDLE_CO,
				     arg->myrank, arg->pool.poh, 0);
	}
	return rc;
}

int
test_setup_next_step(void **state, struct test_pool *pool, daos_prop_t *po_prop,
		     daos_prop_t *co_prop)
{
	test_arg_t *arg = *state;

	switch (arg->setup_state) {
	default:
		arg->setup_state = SETUP_EQ;
		return daos_eq_create(&arg->eq);
	case SETUP_EQ:
		arg->setup_state = SETUP_POOL_CREATE;
		return test_setup_pool_create(state, pool, po_prop);
	case SETUP_POOL_CREATE:
		arg->setup_state = SETUP_POOL_CONNECT;
		return test_setup_pool_connect(state, pool);
	case SETUP_POOL_CONNECT:
		arg->setup_state = SETUP_CONT_CREATE;
		return test_setup_cont_create(state, co_prop);
	case SETUP_CONT_CREATE:
		arg->setup_state = SETUP_CONT_CONNECT;
		return test_setup_cont_open(state);
	}
}

int
test_setup(void **state, unsigned int step, bool multi_rank,
	   daos_size_t pool_size, struct test_pool *pool)
{
	test_arg_t	*arg = *state;
	struct timeval	 now;
	unsigned int	 seed;
	int		 rc = 0;

	/* feed a seed for pseudo-random number generator */
	gettimeofday(&now, NULL);
	seed = (unsigned int)(now.tv_sec * 1000000 + now.tv_usec);
	srandom(seed);

	if (arg == NULL) {
		D_ALLOC(arg, sizeof(test_arg_t));
		if (arg == NULL)
			return -1;
		*state = arg;
		memset(arg, 0, sizeof(*arg));

		MPI_Comm_rank(MPI_COMM_WORLD, &arg->myrank);
		MPI_Comm_size(MPI_COMM_WORLD, &arg->rank_size);
		arg->multi_rank = multi_rank;
		arg->pool.pool_size = pool_size;
		arg->setup_state = -1;

		arg->pool.svc.rl_nr = svc_nreplicas;
		arg->pool.svc.rl_ranks = arg->pool.ranks;
		arg->pool.slave = false;

		arg->mode = 0731;
		arg->uid = geteuid();
		arg->gid = getegid();

		arg->group = server_group;
		uuid_clear(arg->pool.pool_uuid);
		uuid_clear(arg->co_uuid);

		arg->hdl_share = false;
		arg->pool.poh = DAOS_HDL_INVAL;
		arg->coh = DAOS_HDL_INVAL;

		arg->pool.destroyed = false;
	}

	while (!rc && step != arg->setup_state)
		rc = test_setup_next_step(state, pool, NULL, NULL);

	 if (rc) {
		D_FREE(arg);
		*state = NULL;
	}
	return rc;
}

static int
pool_destroy_safe(test_arg_t *arg)
{
	daos_pool_info_t		 pinfo;
	daos_handle_t			 poh = arg->pool.poh;
	int				 rc;

	if (daos_handle_is_inval(poh)) {
		rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
				       &arg->pool.svc, DAOS_PC_RW,
				       &poh, &arg->pool.pool_info,
				       NULL /* ev */);
		if (rc != 0) { /* destory straightaway */
			print_message("failed to connect pool: %d\n", rc);
			poh = DAOS_HDL_INVAL;
		}
	}

	while (!daos_handle_is_inval(poh)) {
		struct daos_rebuild_status *rstat = &pinfo.pi_rebuild_st;

		memset(&pinfo, 0, sizeof(pinfo));
		pinfo.pi_bits = DPI_REBUILD_STATUS;
		rc = daos_pool_query(poh, NULL, &pinfo, NULL, NULL);
		if (rc != 0) {
			fprintf(stderr, "pool query failed: %d\n", rc);
			return rc;
		}

		if (rstat->rs_done == 0) {
			print_message("waiting for rebuild\n");
			sleep(1);
			continue;
		}

		/* no rebuild */
		break;
	}

	daos_pool_disconnect(poh, NULL);

	rc = daos_pool_destroy(arg->pool.pool_uuid, arg->group, 1, NULL);
	if (rc && rc != -DER_TIMEDOUT)
		print_message("daos_pool_destroy failed, rc: %d\n", rc);
	return rc;
}

int
test_teardown(void **state)
{
	test_arg_t	*arg = *state;
	int		 rc = 0;
	int              rc_reduce = 0;

	if (arg == NULL) {
		print_message("state not set, likely due to group-setup"
			      " issue\n");
		return 0;
	}

	if (arg->multi_rank)
		MPI_Barrier(MPI_COMM_WORLD);

	if (!daos_handle_is_inval(arg->coh)) {
		rc = daos_cont_close(arg->coh, NULL);
		if (arg->multi_rank) {
			MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN,
				      MPI_COMM_WORLD);
			rc = rc_reduce;
		}
		arg->coh = DAOS_HDL_INVAL;
		if (rc) {
			print_message("failed to close container "DF_UUIDF
				      ": %d\n", DP_UUID(arg->co_uuid), rc);
			return rc;
		}
	}

	if (!uuid_is_null(arg->co_uuid)) {
		while (arg->myrank == 0) {
			rc = daos_cont_destroy(arg->pool.poh, arg->co_uuid, 1,
					       NULL);
			if (rc == -DER_BUSY) {
				print_message("Container is busy, wait\n");
				sleep(1);
				continue;
			}
			break;
		}
		if (arg->multi_rank)
			MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		if (rc) {
			/* The container might be left some reference count
			 * during rebuild test due to "hacky"exclude triggering
			 * rebuild mechanism(REBUILD24/25), so even the
			 * container is not closed, then delete will fail
			 * here, but if we do not free the arg, then next
			 * subtest might fail, expecially for rebuild test.
			 * so let's destory the arg anyway. Though some pool
			 * might be left here. XXX
			 */
			print_message("failed to destroy container "DF_UUIDF
				      ": %d\n", DP_UUID(arg->co_uuid), rc);
			goto free;
		}
	}

	if (!uuid_is_null(arg->pool.pool_uuid) && !arg->pool.slave) {
		if (arg->myrank == 0)
			pool_destroy_safe(arg);

		if (arg->multi_rank)
			MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		if (rc)
			return rc;
	}

	if (!daos_handle_is_inval(arg->eq)) {
		rc = daos_eq_destroy(arg->eq, 0);
		if (rc) {
			print_message("failed to destroy eq: %d\n", rc);
			return rc;
		}
	}

free:
	D_FREE(arg);
	*state = NULL;
	return 0;
}

int test_make_dirs(char *dir, mode_t mode)
{
	char	*p;
	mode_t	 stored_mode;
	char	 parent_dir[PATH_MAX] = { 0 };

	if (dir == NULL || *dir == '\0')
		return daos_errno2der(errno);

	stored_mode = umask(0);
	p = strrchr(dir, '/');
	if (p != NULL) {
		strncpy(parent_dir, dir, p - dir);
		if (access(parent_dir, F_OK) != 0)
			test_make_dirs(parent_dir, mode);

		if (access(dir, F_OK) != 0) {
			if (mkdir(dir, mode) != 0) {
				print_message("mkdir %s failed %d.\n",
					      dir, errno);
				return daos_errno2der(errno);
			}
		}
	}
	umask(stored_mode);

	return 0;
}

d_rank_t ranks_to_kill[MAX_KILLS];

bool
test_runable(test_arg_t *arg, unsigned int required_nodes)
{
	int		 i;
	static bool	 runable = true;

	if (arg->myrank == 0) {
		int			tgts_per_node;
		int			disable_nodes;

		tgts_per_node = arg->srv_ntgts / arg->srv_nnodes;
		disable_nodes = (arg->srv_disabled_ntgts + tgts_per_node - 1) /
				tgts_per_node;
		if (arg->srv_nnodes - disable_nodes < required_nodes) {
			if (arg->myrank == 0)
				print_message("No enough targets, skipping "
					      "(%d/%d)\n",
					      arg->srv_ntgts,
					      arg->srv_disabled_ntgts);
			runable = false;
		}

		for (i = 0; i < MAX_KILLS; i++)
			ranks_to_kill[i] = arg->srv_nnodes -
					   disable_nodes - i - 1;

		arg->hce = daos_ts2epoch();
	}

	MPI_Bcast(&runable, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Barrier(MPI_COMM_WORLD);
	return runable;
}

int
test_pool_get_info(test_arg_t *arg, daos_pool_info_t *pinfo)
{
	bool	   connect_pool = false;
	int	   rc;

	if (daos_handle_is_inval(arg->pool.poh)) {
		rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
				       &arg->pool.svc, DAOS_PC_RW,
				       &arg->pool.poh, pinfo,
				       NULL /* ev */);
		if (rc) {
			print_message("pool_connect "DF_UUIDF
				      " failed, rc: %d\n",
				      DP_UUID(arg->pool.pool_uuid), rc);
			return rc;
		}
		connect_pool = true;
	}

	rc = daos_pool_query(arg->pool.poh, NULL, pinfo, NULL, NULL);
	if (rc != 0)
		print_message("pool query failed %d\n", rc);

	if (connect_pool) {
		rc = daos_pool_disconnect(arg->pool.poh, NULL);
		if (rc)
			print_message("disconnect failed: %d\n",
				      rc);
		arg->pool.poh = DAOS_HDL_INVAL;
	}

	return rc;
}

static bool
rebuild_pool_wait(test_arg_t *arg)
{
	daos_pool_info_t	   pinfo = { 0 };
	struct daos_rebuild_status *rst;
	int			   rc;
	bool			   done = false;

	pinfo.pi_bits = DPI_REBUILD_STATUS;
	rc = test_pool_get_info(arg, &pinfo);
	rst = &pinfo.pi_rebuild_st;
	if (rst->rs_done || rc != 0) {
		print_message("Rebuild "DF_UUIDF" (ver=%d) is done %d/%d, "
			      "obj="DF_U64", rec="DF_U64".\n",
			       DP_UUID(arg->pool.pool_uuid), rst->rs_version,
			       rc, rst->rs_errno, rst->rs_obj_nr,
			       rst->rs_rec_nr);
		done = true;
	} else {
		print_message("wait for rebuild pool "DF_UUIDF"(ver=%u), "
			      "to-be-rebuilt obj="DF_U64", already rebuilt obj="
			      DF_U64", rec="DF_U64"\n",
			      DP_UUID(arg->pool.pool_uuid), rst->rs_version,
			      rst->rs_toberb_obj_nr, rst->rs_obj_nr,
			      rst->rs_rec_nr);
	}

	return done;
}

int
test_get_leader(test_arg_t *arg, d_rank_t *rank)
{
	daos_pool_info_t	pinfo = { 0 };
	int			rc;

	rc = test_pool_get_info(arg, &pinfo);
	if (rc)
		return rc;

	*rank = pinfo.pi_leader;
	return 0;
}

d_rank_t
test_get_last_svr_rank(test_arg_t *arg)
{
	unsigned int tgts_per_node;
	unsigned int disable_nodes;

	if (arg->srv_ntgts == 0 || arg->srv_nnodes == 0) {
		print_message("not connected yet?\n");
		return -1;
	}

	/* If rank == -1, it means kill the last node */
	tgts_per_node = arg->srv_ntgts / arg->srv_nnodes;
	disable_nodes = (arg->srv_disabled_ntgts +
			 tgts_per_node - 1) / tgts_per_node;

	return arg->srv_nnodes - disable_nodes - 1;
}

bool
test_rebuild_query(test_arg_t **args, int args_cnt)
{
	bool all_done = true;
	int i;

	for (i = 0; i < args_cnt; i++) {
		bool done;

		done = rebuild_pool_wait(args[i]);
		if (!done)
			all_done = false;
	}
	return all_done;
}

void
test_rebuild_wait(test_arg_t **args, int args_cnt)
{
	while (!test_rebuild_query(args, args_cnt))
		sleep(2);
}

int
run_daos_sub_tests(const struct CMUnitTest *tests, int tests_size,
		   daos_size_t pool_size, int *sub_tests,
		   int sub_tests_size, test_setup_cb_t setup_cb,
		   test_teardown_cb_t teardown_cb)
{
	void *state = NULL;
	int i;
	int rc;

	D_ASSERT(pool_size > 0);
	rc = test_setup(&state, SETUP_CONT_CONNECT, true, pool_size, NULL);
	if (rc)
		return rc;

	if (setup_cb != NULL) {
		rc = setup_cb(&state);
		if (rc)
			return rc;
	}

	for (i = 0; i < sub_tests_size; i++) {
		int idx = sub_tests ? sub_tests[i] : i;
		test_arg_t	*arg;

		if (idx >= tests_size) {
			print_message("No test %d\n", idx);
			continue;
		}

		print_message("%s\n", tests[idx].name);
		if (tests[idx].setup_func)
			tests[idx].setup_func(&state);

		arg = state;
		arg->index = idx;

		tests[idx].test_func(&state);
		if (tests[idx].teardown_func)
			tests[idx].teardown_func(&state);
	}

	if (teardown_cb != NULL) {
		rc = teardown_cb(&state);
		if (rc)
			return rc;
	} else {
		test_teardown(&state);
	}

	return 0;
}

void
daos_exclude_target(const uuid_t pool_uuid, const char *grp,
		    const d_rank_list_t *svc, d_rank_t rank,
		    int tgt_idx)
{
	struct d_tgt_list	targets;
	int			rc;

	/** exclude from the pool */
	targets.tl_nr = 1;
	targets.tl_ranks = &rank;
	targets.tl_tgts = &tgt_idx;
	rc = daos_pool_tgt_exclude(pool_uuid, grp, svc, &targets, NULL);
	if (rc)
		print_message("exclude pool failed rc %d\n", rc);
	assert_int_equal(rc, 0);
}

void
daos_add_target(const uuid_t pool_uuid, const char *grp,
		const d_rank_list_t *svc, d_rank_t rank, int tgt_idx)
{
	struct d_tgt_list	targets;
	int			rc;

	/** add tgt to the pool */
	targets.tl_nr = 1;
	targets.tl_ranks = &rank;
	targets.tl_tgts = &tgt_idx;
	rc = daos_pool_add_tgt(pool_uuid, grp, svc, &targets, NULL);
	if (rc)
		print_message("add pool failed rc %d\n", rc);
	assert_int_equal(rc, 0);
}

void
daos_exclude_server(const uuid_t pool_uuid, const char *grp,
		    const d_rank_list_t *svc, d_rank_t rank)
{
	daos_exclude_target(pool_uuid, grp, svc, rank, -1);
}

void
daos_add_server(const uuid_t pool_uuid, const char *grp,
		const d_rank_list_t *svc, d_rank_t rank)
{
	daos_add_target(pool_uuid, grp, svc, rank, -1);
}

void
daos_kill_server(test_arg_t *arg, const uuid_t pool_uuid, const char *grp,
		 d_rank_list_t *svc, d_rank_t rank)
{
	int tgts_per_node = arg->srv_ntgts / arg->srv_nnodes;
	int rc;

	arg->srv_disabled_ntgts += tgts_per_node;
	if (d_rank_in_rank_list(svc, rank))
		svc->rl_nr--;
	print_message("\tKilling target %d (total of %d with %d already "
		      "disabled, svc->rl_nr %d)!\n", rank, arg->srv_ntgts,
		       arg->srv_disabled_ntgts - 1, svc->rl_nr);

	/** kill server */
	rc = daos_mgmt_svc_rip(grp, rank, true, NULL);
	assert_int_equal(rc, 0);
}

void
daos_kill_exclude_server(test_arg_t *arg, const uuid_t pool_uuid,
			 const char *grp, d_rank_list_t *svc)
{
	int		tgts_per_node;
	int		disable_nodes;
	int		failures = 0;
	int		max_failure;
	int		i;
	d_rank_t	rank;

	tgts_per_node = arg->srv_ntgts / arg->srv_nnodes;
	disable_nodes = (arg->srv_disabled_ntgts + tgts_per_node - 1) /
			tgts_per_node;
	max_failure = (svc->rl_nr - 1) / 2;
	for (i = 0; i < svc->rl_nr; i++) {
		if (svc->rl_ranks[i] >=
		    arg->srv_nnodes - disable_nodes - 1)
			failures++;
	}

	if (failures > max_failure) {
		print_message("Already kill %d targets with %d replica,"
			      " (max_kill %d) can not kill anymore\n",
			      arg->srv_disabled_ntgts, svc->rl_nr, max_failure);
		return;
	}

	rank = arg->srv_nnodes - disable_nodes - 1;

	daos_kill_server(arg, pool_uuid, grp, svc, rank);
	daos_exclude_server(pool_uuid, grp, svc, rank);
}
