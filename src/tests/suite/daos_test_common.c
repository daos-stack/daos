/**
 * (C) Copyright 2018-2020 Intel Corporation.
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

#include <daos.h>
#include <daos_prop.h>
#include <daos_mgmt.h>
#include "daos_test.h"

/** Server crt group ID */
const char *server_group;
const char *dmg_config_file;

/** Pool service replicas */
unsigned int svc_nreplicas = 1;

/** Checksum Config */
unsigned int	dt_csum_type;
unsigned int	dt_csum_chunksize;
bool		dt_csum_server_verify;
int		dt_obj_class;


/* Create or import a single pool with option to store info in arg->pool
 * or an alternate caller-specified test_pool structure.
 * ipool (optional): import pool: store info for an existing pool to arg->pool.
 * opool (optional): export pool: create new pool, store info in opool.
 *                   Caller set opool->pool_size, svc.rl_nr before calling.
 *
 * ipool!=NULL: import a pool, store in arg.pool (or opool if opool!=NULL).
 * ipool==NULL: create a pool, store in arg.pool (or opool if opool!=NULL).
 */
int
test_setup_pool_create(void **state, struct test_pool *ipool,
		       struct test_pool *opool, daos_prop_t *prop)
{
	test_arg_t		*arg = *state;
	struct test_pool	*outpool;
	int			 rc = 0;

	outpool = opool ? opool : &arg->pool;

	if (ipool != NULL) {
		/* copy the info from passed in pool */
		assert_int_equal(outpool->slave, 0);
		outpool->pool_size = ipool->pool_size;
		uuid_copy(outpool->pool_uuid, ipool->pool_uuid);
		d_rank_list_dup(&outpool->alive_svc, ipool->alive_svc);
		d_rank_list_dup(&outpool->svc, ipool->svc);
		outpool->slave = 1;
		if (arg->multi_rank)
			MPI_Barrier(MPI_COMM_WORLD);
		return 0;
	}

	if (arg->myrank == 0) {
		char		*env;
		int		 size_gb;
		daos_size_t	 nvme_size;
		d_rank_list_t	 *rank_list = NULL;

		env = getenv("POOL_SCM_SIZE");
		if (env) {
			size_gb = atoi(env);
			if (size_gb != 0)
				outpool->pool_size =
					(daos_size_t)size_gb << 30;
		}

		/*
		 * Set the default NVMe partition size to "4 * scm_size", so
		 * that we need to specify SCM size only for each test case.
		 *
		 * Set env POOL_NVME_SIZE to overwrite the default NVMe size.
		 */
		nvme_size = outpool->pool_size * 4;
		env = getenv("POOL_NVME_SIZE");
		if (env) {
			size_gb = atoi(env);
			nvme_size = (daos_size_t)size_gb << 30;
		}

		if (arg->pool_node_size > 0) {
			rank_list = d_rank_list_alloc(arg->pool_node_size);
			if (rank_list == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}
		print_message("setup: creating pool, SCM size="DF_U64" GB, "
			      "NVMe size="DF_U64" GB\n",
			      (outpool->pool_size >> 30), nvme_size >> 30);
		rc = dmg_pool_create(dmg_config_file,
				     arg->uid, arg->gid, arg->group,
				     rank_list, outpool->pool_size, nvme_size,
				     prop, outpool->svc, outpool->pool_uuid);
		if (rc)
			print_message("dmg_pool_create failed, rc: %d\n", rc);
		else
			print_message("setup: created pool "DF_UUIDF"\n",
				       DP_UUID(outpool->pool_uuid));
		if (rank_list)
			d_rank_list_free(rank_list);
	}
out:
	/** broadcast pool create result */
	if (arg->multi_rank) {
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		/** broadcast pool UUID and svc addresses */
		if (!rc) {
			MPI_Bcast(outpool->pool_uuid, 16,
				  MPI_CHAR, 0, MPI_COMM_WORLD);
			MPI_Bcast(&outpool->svc->rl_nr,
				  sizeof(outpool->svc->rl_nr),
				  MPI_CHAR, 0, MPI_COMM_WORLD);
			MPI_Bcast(outpool->svc->rl_ranks,
				  sizeof(outpool->svc->rl_ranks[0]) *
					 outpool->svc->rl_nr,
				  MPI_CHAR, 0, MPI_COMM_WORLD);
		}
	}
	return rc;
}

static int
test_setup_pool_connect(void **state, struct test_pool *pool)
{
	test_arg_t *arg = *state;
	int rc = -DER_INVAL;

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
		daos_pool_info_t info = {0};

		print_message("setup: connecting to pool\n");
		rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
				       arg->pool.svc,
				       arg->pool.pool_connect_flags,
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
	int rc = 0;

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
	int rc = 0;

	if (arg->myrank == 0) {
		print_message("setup: opening container\n");
		rc = daos_cont_open(arg->pool.poh, arg->co_uuid,
				    arg->cont_open_flags,
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
		return test_setup_pool_create(state, pool,
					      NULL /*opool */, po_prop);
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
	   daos_size_t pool_size, int node_size, struct test_pool *pool)
{
	test_arg_t		*arg = *state;
	struct timeval		 now;
	unsigned int		 seed;
	int			 rc = 0;
	daos_prop_t		 co_props = {0};
	struct daos_prop_entry	 csum_entry[3] = {0};
	struct daos_prop_entry	*entry;

	/* feed a seed for pseudo-random number generator */
	gettimeofday(&now, NULL);
	seed = (unsigned int)(now.tv_sec * 1000000 + now.tv_usec);
	srandom(seed);

	if (arg == NULL) {
		d_rank_list_t	tmp_list;

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

		tmp_list.rl_nr = svc_nreplicas;
		tmp_list.rl_ranks = arg->pool.ranks;

		d_rank_list_dup(&arg->pool.alive_svc, &tmp_list);
		d_rank_list_dup(&arg->pool.svc, &tmp_list);
		arg->pool.slave = false;

		arg->uid = geteuid();
		arg->gid = getegid();

		arg->pool_node_size = node_size;
		arg->group = server_group;
		arg->dmg_config = dmg_config_file;
		uuid_clear(arg->pool.pool_uuid);
		uuid_clear(arg->co_uuid);

		arg->hdl_share = false;
		arg->pool.poh = DAOS_HDL_INVAL;
		arg->pool.pool_connect_flags = DAOS_PC_RW;
		arg->coh = DAOS_HDL_INVAL;
		arg->cont_open_flags = DAOS_COO_RW;
		arg->obj_class = dt_obj_class;
		arg->pool.destroyed = false;
	}

	/** Look at variables set by test arguments and setup container props */
	if (dt_csum_type) {
		print_message("\n-------\n"
			      "Checksum enabled in test!"
			      "\n-------\n");
		entry = &csum_entry[co_props.dpp_nr];
		entry->dpe_type = DAOS_PROP_CO_CSUM;
		entry->dpe_val = dt_csum_type;

		co_props.dpp_nr++;
	}

	if (dt_csum_chunksize) {
		entry = &csum_entry[co_props.dpp_nr];
		entry->dpe_type = DAOS_PROP_CO_CSUM_CHUNK_SIZE;
		entry->dpe_val = dt_csum_chunksize;
		co_props.dpp_nr++;
	}

	if (dt_csum_server_verify) {
		entry = &csum_entry[co_props.dpp_nr];
		entry->dpe_type = DAOS_PROP_CO_CSUM_SERVER_VERIFY;
		entry->dpe_val = dt_csum_server_verify ?
			DAOS_PROP_CO_CSUM_SV_ON :
			DAOS_PROP_CO_CSUM_SERVER_VERIFY;

		co_props.dpp_nr++;
	}

	if (co_props.dpp_nr > 0)
		co_props.dpp_entries = csum_entry;

	while (!rc && step != arg->setup_state)
		rc = test_setup_next_step(state, pool, NULL, &co_props);

	if (rc) {
		D_FREE(arg);
		*state = NULL;
	}
	return rc;
}

/* Destroy arg->pool or the pool specified by extpool (optional argument) */
int
pool_destroy_safe(test_arg_t *arg, struct test_pool *extpool)
{
	daos_pool_info_t	 pinfo = {0};
	struct test_pool	*pool;
	daos_handle_t		 poh;
	int			 rc;

	pool = extpool ? extpool : &arg->pool;
	poh = pool->poh;

	if (daos_handle_is_inval(poh)) {
		rc = daos_pool_connect(pool->pool_uuid, arg->group,
				       pool->svc, DAOS_PC_RW,
				       &poh, &pool->pool_info,
				       NULL /* ev */);
		if (rc != 0) { /* destroy straight away */
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

	rc = dmg_pool_destroy(dmg_config_file,
			      pool->pool_uuid, arg->group, 1);
	if (rc && rc != -DER_TIMEDOUT)
		print_message("dmg_pool_destroy failed, rc: %d\n", rc);
	if (rc == 0)
		print_message("teardown: destroyed pool "DF_UUIDF"\n",
			      DP_UUID(pool->pool_uuid));
	return rc;
}

int
test_teardown_cont_hdl(test_arg_t *arg)
{
	int	rc = 0;
	int	rc_reduce = 0;

	rc = daos_cont_close(arg->coh, NULL);
	if (arg->multi_rank) {
		MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN,
			      MPI_COMM_WORLD);
		rc = rc_reduce;
	}
	arg->coh = DAOS_HDL_INVAL;
	arg->setup_state = SETUP_CONT_CREATE;
	if (rc) {
		print_message("failed to close container "DF_UUIDF
			      ": %d\n", DP_UUID(arg->co_uuid), rc);
		return rc;
	}

	return rc;
}

int
test_teardown_cont(test_arg_t *arg)
{
	int	rc = 0;

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
	if (rc)
		print_message("failed to destroy container "DF_UUIDF
			      ": %d\n", DP_UUID(arg->co_uuid), rc);

	uuid_clear(arg->co_uuid);
	arg->setup_state = SETUP_POOL_CONNECT;
	return rc;
}

int
test_teardown(void **state)
{
	test_arg_t	*arg = *state;
	int		 rc = 0;

	if (arg == NULL) {
		print_message("state not set, likely due to group-setup"
			      " issue\n");
		return 0;
	}

	if (arg->multi_rank)
		MPI_Barrier(MPI_COMM_WORLD);

	if (!daos_handle_is_inval(arg->coh)) {
		rc = test_teardown_cont_hdl(arg);
		if (rc)
			return rc;
	}

	if (!uuid_is_null(arg->co_uuid)) {
		rc = test_teardown_cont(arg);
		if (rc) {
			/* The container might be left some reference count
			 * during rebuild test due to "hacky"exclude triggering
			 * rebuild mechanism(REBUILD24/25), so even the
			 * container is not closed, then delete will fail
			 * here, but if we do not free the arg, then next
			 * subtest might fail, especially for rebuild test.
			 * so let's destroy the arg anyway. Though some pool
			 * might be left here. XXX
			 */
			goto free;
		}
	}

	if (!uuid_is_null(arg->pool.pool_uuid) && !arg->pool.slave &&
	    !arg->pool.destroyed) {
		if (arg->myrank != 0) {
			if (!daos_handle_is_inval(arg->pool.poh))
				rc = daos_pool_disconnect(arg->pool.poh, NULL);
		}
		if (arg->multi_rank)
			MPI_Barrier(MPI_COMM_WORLD);
		if (arg->myrank == 0)
			rc = pool_destroy_safe(arg, NULL);

		if (arg->multi_rank)
			MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		if (rc) {
			print_message("failed to destroy pool "DF_UUIDF
				      " rc: %d\n",
				      DP_UUID(arg->pool.pool_uuid), rc);
			return rc;
		}
	}

	if (!daos_handle_is_inval(arg->eq)) {
		rc = daos_eq_destroy(arg->eq, 0);
		if (rc) {
			print_message("failed to destroy eq: %d\n", rc);
			return rc;
		}
	}

free:
	if (arg->pool.svc)
		d_rank_list_free(arg->pool.svc);
	if (arg->pool.alive_svc)
		d_rank_list_free(arg->pool.alive_svc);
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
				print_message("Not enough targets(need %d),"
					      " skipping (%d/%d)\n",
					      required_nodes,
					      arg->srv_ntgts,
					      arg->srv_disabled_ntgts);
			runable = false;
		}

		for (i = 0; i < MAX_KILLS; i++)
			ranks_to_kill[i] = arg->srv_nnodes -
					   disable_nodes - i - 1;

		arg->hce = crt_hlc_get();
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
				       arg->pool.svc, DAOS_PC_RW,
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
	daos_pool_info_t	   pinfo = {0};
	struct daos_rebuild_status *rst;
	int			   rc;
	bool			   done = false;

	pinfo.pi_bits = DPI_REBUILD_STATUS;
	rc = test_pool_get_info(arg, &pinfo);
	rst = &pinfo.pi_rebuild_st;
	if ((rst->rs_done || rc != 0) && rst->rs_version != 0) {
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
	daos_pool_info_t	pinfo = {0};
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
		bool done = true;

		if (!args[i]->pool.destroyed)
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
run_daos_sub_tests_only(char *test_name, const struct CMUnitTest *tests,
			int tests_size, int *sub_tests, int sub_tests_size)
{
	int i;
	int rc = 0;

	if (sub_tests != NULL) {
		struct CMUnitTest *subtests;
		int subtestsnb = 0;

		D_ALLOC_ARRAY(subtests, sub_tests_size);
		if (subtests == NULL) {
			print_message("failed allocating subtests array\n");
			return -DER_NOMEM;
		}

		for (i = 0; i < sub_tests_size; i++) {
			if (sub_tests[i] >= tests_size || sub_tests[i] < 0) {
				print_message("No subtest %d\n", sub_tests[i]);
				continue;
			}
			subtests[i] = tests[sub_tests[i]];
			subtestsnb++;
		}

		/* run the sub-tests */
		if (subtestsnb > 0)
			rc = _cmocka_run_group_tests(test_name, subtests,
						     subtestsnb, NULL, NULL);
		D_FREE(subtests);
	} else {
		/* run the full suite */
		rc = _cmocka_run_group_tests(test_name, tests, tests_size,
					     NULL, NULL);
	}

	return rc;
}

int
run_daos_sub_tests(char *test_name, const struct CMUnitTest *tests,
		   int tests_size, int *sub_tests, int sub_tests_size,
		   test_setup_cb_t setup_cb, test_teardown_cb_t teardown_cb)
{
	int i;
	int rc = 0;

	if (sub_tests != NULL) {
		struct CMUnitTest *subtests;
		int subtestsnb = 0;

		D_ALLOC_ARRAY(subtests, sub_tests_size);
		if (subtests == NULL) {
			print_message("failed allocating subtests array\n");
			return -DER_NOMEM;
		}

		for (i = 0; i < sub_tests_size; i++) {
			if (sub_tests[i] > tests_size || sub_tests[i] < 1) {
				print_message("No subtest %d\n", sub_tests[i]);
				continue;
			}
			subtests[i] = tests[sub_tests[i] - 1];
			subtestsnb++;
		}

		/* run the sub-tests */
		if (subtestsnb > 0)
			rc = _cmocka_run_group_tests(test_name, subtests,
						     subtestsnb, setup_cb,
						     teardown_cb);
		D_FREE(subtests);
	} else {
		/* run the full suite */
		rc = _cmocka_run_group_tests(test_name, tests, tests_size,
					     setup_cb, teardown_cb);
	}

	return rc;
}

static void
daos_dmg_pool_target(const char *sub_cmd, const uuid_t pool_uuid,
		     const char *grp, const char *dmg_config,
		     const d_rank_list_t *svc, d_rank_t rank, int tgt_idx)
{
	char		dmg_cmd[DTS_CFG_MAX];
	int		rc;

	/* build and invoke dmg cmd */
	dts_create_config(dmg_cmd, "dmg pool %s -i --pool=%s --rank=%d",
			  sub_cmd, DP_UUID(pool_uuid), rank);

	if (tgt_idx != -1)
		dts_append_config(dmg_cmd, " --target-idx=%d", tgt_idx);
	if (dmg_config != NULL)
		dts_append_config(dmg_cmd, " -o %s", dmg_config);

	rc = system(dmg_cmd);
	print_message("%s rc %#x\n", dmg_cmd, rc);
	assert_int_equal(rc, 0);
}

void
daos_exclude_target(const uuid_t pool_uuid, const char *grp,
		    const char *dmg_config, const d_rank_list_t *svc,
		    d_rank_t rank, int tgt_idx)
{
	daos_dmg_pool_target("exclude", pool_uuid, grp, dmg_config, svc,
			     rank, tgt_idx);
}

void
daos_reint_target(const uuid_t pool_uuid, const char *grp,
		  const char *dmg_config, const d_rank_list_t *svc,
		  d_rank_t rank, int tgt_idx)
{
	daos_dmg_pool_target("reintegrate", pool_uuid, grp, dmg_config, svc,
			     rank, tgt_idx);
}

void
daos_drain_target(const uuid_t pool_uuid, const char *grp,
		  const char *dmg_config, const d_rank_list_t *svc,
		  d_rank_t rank, int tgt_idx)
{

	daos_dmg_pool_target("drain", pool_uuid, grp, dmg_config, svc,
			     rank, tgt_idx);
}

void
daos_exclude_server(const uuid_t pool_uuid, const char *grp,
		    const char *dmg_config, const d_rank_list_t *svc,
		    d_rank_t rank)
{
	daos_exclude_target(pool_uuid, grp, dmg_config, svc, rank, -1);
}

void
daos_reint_server(const uuid_t pool_uuid, const char *grp,
		  const char *dmg_config, const d_rank_list_t *svc,
		  d_rank_t rank)
{
	daos_reint_target(pool_uuid, grp, dmg_config, svc, rank, -1);
}

void
daos_kill_server(test_arg_t *arg, const uuid_t pool_uuid,
		 const char *grp, d_rank_list_t *svc, d_rank_t rank)
{
	int		tgts_per_node;
	int		disable_nodes;
	int		failures = 0;
	int		max_failure;
	int		i;
	int		rc;
	char		dmg_cmd[DTS_CFG_MAX];

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

	if ((int)rank == -1)
		rank = arg->srv_nnodes - disable_nodes - 1;

	arg->srv_disabled_ntgts += tgts_per_node;
	if (d_rank_in_rank_list(svc, rank))
		svc->rl_nr--;
	print_message("\tKilling rank %d (total of %d with %d already "
		      "disabled, svc->rl_nr %d)!\n", rank, arg->srv_ntgts,
		       arg->srv_disabled_ntgts - 1, svc->rl_nr);

	/* build and invoke dmg cmd to stop the server */
	dts_create_config(dmg_cmd, "dmg system stop -i -r %d --force", rank);
	if (arg->dmg_config != NULL)
		dts_append_config(dmg_cmd, " -o %s", arg->dmg_config);

	rc = system(dmg_cmd);
	print_message(" %s rc %#x\n", dmg_cmd, rc);
	assert_int_equal(rc, 0);
}

struct daos_acl *
get_daos_acl_with_owner_perms(uint64_t perms)
{
	struct daos_acl	*acl;
	struct daos_ace	*owner_ace;

	owner_ace = daos_ace_create(DAOS_ACL_OWNER, NULL);
	owner_ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	owner_ace->dae_allow_perms = perms;
	assert_true(daos_ace_is_valid(owner_ace));

	acl = daos_acl_create(&owner_ace, 1);
	assert_non_null(acl);

	daos_ace_free(owner_ace);
	return acl;
}

daos_prop_t *
get_daos_prop_with_owner_and_acl(char *owner, uint32_t owner_type,
				 struct daos_acl *acl, uint32_t acl_type)
{
	daos_prop_t	*prop;

	prop = daos_prop_alloc(2);
	assert_non_null(prop);

	prop->dpp_entries[0].dpe_type = acl_type;
	prop->dpp_entries[0].dpe_val_ptr = daos_acl_dup(acl);
	assert_non_null(prop->dpp_entries[0].dpe_val_ptr);

	prop->dpp_entries[1].dpe_type = owner_type;
	D_STRNDUP(prop->dpp_entries[1].dpe_str, owner,
		  DAOS_ACL_MAX_PRINCIPAL_LEN);
	assert_non_null(prop->dpp_entries[1].dpe_str);

	return prop;
}

daos_prop_t *
get_daos_prop_with_owner_acl_perms(uint64_t perms, uint32_t type)
{
	daos_prop_t	*prop;
	struct daos_acl	*acl;

	acl = get_daos_acl_with_owner_perms(perms);

	prop = daos_prop_alloc(1);
	assert_non_null(prop);

	prop->dpp_entries[0].dpe_type = type;
	prop->dpp_entries[0].dpe_val_ptr = acl;

	/* No need to free ACL - belongs to prop now */
	return prop;
}

daos_prop_t *
get_daos_prop_with_user_acl_perms(uint64_t perms)
{
	daos_prop_t	*prop;
	struct daos_acl	*acl;
	struct daos_ace	*ace;
	char		*user = NULL;

	assert_int_equal(daos_acl_uid_to_principal(geteuid(), &user), 0);

	acl = get_daos_acl_with_owner_perms(0);

	ace = daos_ace_create(DAOS_ACL_USER, user);
	assert_non_null(ace);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = perms;
	assert_true(daos_ace_is_valid(ace));

	assert_int_equal(daos_acl_add_ace(&acl, ace), 0);

	/* Set effective user up as non-owner */
	prop = get_daos_prop_with_owner_and_acl("nobody@", DAOS_PROP_CO_OWNER,
						acl, DAOS_PROP_CO_ACL);

	daos_ace_free(ace);
	daos_acl_free(acl);
	D_FREE(user);
	return prop;
}

int
get_pid_of_process(char *host, char *dpid, char *proc)
{
	char    command[256];
	size_t  len = 0;
	size_t  read;
	char    *line = NULL;

	snprintf(command, sizeof(command),
		 "ssh %s pgrep %s", host, proc);
	FILE *fp1 = popen(command, "r");

	print_message("Command= %s\n", command);
	if (fp1 == NULL)
		return -DER_INVAL;

	while ((read = getline(&line, &len, fp1)) != -1) {
		print_message("%s pid = %s", proc, line);
		strcat(dpid, line);
	}

	pclose(fp1);
	return 0;
}

int
get_server_config(char *host, char *server_config_file)
{
	char	command[256];
	size_t	len = 0;
	size_t	read;
	char	*line = NULL;
	char	*pch;
	char    *dpid;
	int	rc;
	char    daos_proc[16] = "daos_server";

	D_ALLOC(dpid, 16);
	rc = get_pid_of_process(host, dpid, daos_proc);
	assert_int_equal(rc, 0);

	snprintf(command, sizeof(command),
		 "ssh %s ps ux -A | grep %s", host, dpid);
	FILE *fp = popen(command, "r");

	print_message("Command %s", command);
	if (fp == NULL)
		return -DER_INVAL;

	while ((read = getline(&line, &len, fp)) != -1) {
		print_message("line %s", line);
		if (strstr(line, "--config") != NULL ||
		    strstr(line, "-o") != NULL)
			break;
	}

	pch = strtok(line, " ");
	while (pch != NULL) {
		if (strstr(pch, "--config") != NULL) {
			if (strchr(pch, '=') != NULL)
				strcpy(server_config_file,
				       strchr(pch, '=') + 1);
			else {
				pch = strtok(NULL, " ");
				strcpy(server_config_file, pch);
			}
			break;
		}

		if (strstr(pch, "-o") != NULL) {
			pch = strtok(NULL, " ");
			strcpy(server_config_file, pch);
			break;
		}
		pch = strtok(NULL, " ");
	}

	pclose(fp);

	D_FREE(dpid);
	free(line);
	return 0;
}

int verify_server_log_mask(char *host, char *server_config_file,
			   char *log_mask){
	char	command[256];
	size_t	len = 0;
	size_t	read;
	char	*line = NULL;

	snprintf(command, sizeof(command),
		 "ssh %s cat %s", host, server_config_file);

	FILE *fp = popen(command, "r");

	if (fp == NULL)
		return -DER_INVAL;

	while ((read = getline(&line, &len, fp)) != -1) {
		if (strstr(line, " log_mask") != NULL) {
			if (strstr(line, log_mask) == NULL) {
				print_message(
					"Expected log_mask = %s, Found %s\n ",
					log_mask, line);
				return -DER_INVAL;
			}
		}
	}

	pclose(fp);
	free(line);
	return 0;
}

int get_log_file(char *host, char *server_config_file,
		 char *key_name, char *log_file)
{
	char	command[256];
	size_t	len = 0;
	size_t	read;
	char	*line = NULL;

	snprintf(command, sizeof(command),
		 "ssh %s cat %s", host, server_config_file);

	FILE *fp = popen(command, "r");

	if (fp == NULL)
		return -DER_INVAL;

	while ((read = getline(&line, &len, fp)) != -1) {
		if (strstr(line, key_name) != NULL)
			strcat(log_file, strrchr(line, ':') + 1);
	}

	pclose(fp);
	D_FREE(line);
	return 0;
}

int verify_state_in_log(char *host, char *log_file, char *state)
{
	char	command[1024];
	size_t	len = 0;
	size_t	read;
	char	*line = NULL;
	char	*pch = NULL;
	int		length;
	char	*tmp = NULL;
	FILE	*fp;

	D_ALLOC(tmp, 1024);
	strcpy(tmp, log_file);

	pch = strtok(tmp, "\n");
	while (pch != NULL) {
		length = strlen(pch);
		if (pch[length - 1] == '\n')
			pch[length - 1]  = '\0';

		snprintf(command, sizeof(command),
			 "ssh %s cat %s | grep \"%s\"", host, pch, state);
		fp = popen(command, "r");
		while ((read = getline(&line, &len, fp)) != -1) {
			if (strstr(line, state) != NULL) {
				print_message("Found state %s in Log file %s\n",
					      state, pch);
				goto out;
			}
		}
		pch = strtok(NULL, " ");
		pclose(fp);
		free(line);
	}

	D_FREE(tmp);
	return -DER_INVAL;
out:
	D_FREE(tmp);
	return 0;
}

#define MAX_BS_STATE_WAIT	20 /* 20sec sleep between bs state queries */
#define MAX_BS_STATE_RETRY	15 /* max timeout of 15 * 20sec= 5min */

int wait_and_verify_blobstore_state(uuid_t bs_uuid, char *expected_state,
				    const char *group)
{
	int	bs_state;
	int	retry_cnt;
	int	rc;

	retry_cnt = 0;
	while (retry_cnt <= MAX_BS_STATE_RETRY) {
		rc = daos_mgmt_get_bs_state(group, bs_uuid, &bs_state,
					    NULL /*ev*/);
		if (rc)
			return rc;

		if (verify_blobstore_state(bs_state, expected_state) == 0)
			return 0;

		sleep(MAX_BS_STATE_WAIT);
		retry_cnt++;
	};

	return -DER_TIMEDOUT;
}

#define MAX_POOL_TGT_STATE_WAIT	   5 /* 5sec sleep between tgt state queries */
#define MAX_POOL_TGT_STATE_RETRY   24 /* max timeout of 24 * 5sec= 2min */

int wait_and_verify_pool_tgt_state(daos_handle_t poh, int tgtidx, int rank,
				   char *expected_state)
{
	daos_target_info_t	tgt_info = { 0 };
	int			retry_cnt;
	int			rc;

	retry_cnt = 0;
	while (retry_cnt <= MAX_POOL_TGT_STATE_RETRY) {
		char *expected_state_dup = strdup(expected_state);
		char *state = strtok(expected_state_dup, "|");

		rc = daos_pool_query_target(poh, tgtidx, rank, &tgt_info, NULL);
			if (rc)
				return rc;

		/* multiple states not present in expected_state str */
		if (state == NULL) {
			if (strcmp(daos_target_state_enum_to_str(tgt_info.ta_state),
				   expected_state) == 0)
				return 0;
		/* multiple states separated by a '|' in expected_state str */
		} else {
			while (state != NULL) {
				if (strcmp(daos_target_state_enum_to_str(tgt_info.ta_state),
					   state) == 0)
					return 0;
				state = strtok(NULL, "|");
			};
		}

		sleep(MAX_POOL_TGT_STATE_WAIT);
		retry_cnt++;
	};

	return -DER_TIMEDOUT;
}
