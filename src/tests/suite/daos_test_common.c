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
test_setup_pool_create(void **state, struct test_pool *pool)
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
		char	*env;

		env = getenv("POOL_SIZE");
		if (env) {
			int size_gb;

			size_gb = atoi(env);
			if (size_gb != 0)
				arg->pool.pool_size =
					(daos_size_t)size_gb << 30;
		}

		print_message("setup: creating pool size="DF_U64" GB\n",
			      (arg->pool.pool_size >> 30));
		rc = daos_pool_create(arg->mode, arg->uid, arg->gid, arg->group,
				      NULL, "pmem", arg->pool.pool_size,
				      &arg->pool.svc, arg->pool.pool_uuid,
				      NULL);
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
			rc = daos_pool_query(arg->pool.poh, NULL, &info, NULL);

			if (rc == 0) {
				arg->srv_ntgts = info.pi_ntargets;
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
test_setup_cont_create(void **state)
{
	test_arg_t *arg = *state;
	int rc;

	if (arg->myrank == 0) {
		uuid_generate(arg->co_uuid);
		print_message("setup: creating container "DF_UUIDF"\n",
			      DP_UUID(arg->co_uuid));
		rc = daos_cont_create(arg->pool.poh, arg->co_uuid, NULL);
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
test_setup_next_step(void **state, struct test_pool *pool)
{
	test_arg_t *arg = *state;

	switch (arg->setup_state) {
	default:
		arg->setup_state = SETUP_EQ;
		return daos_eq_create(&arg->eq);
	case SETUP_EQ:
		arg->setup_state = SETUP_POOL_CREATE;
		return test_setup_pool_create(state, pool);
	case SETUP_POOL_CREATE:
		arg->setup_state = SETUP_POOL_CONNECT;
		return test_setup_pool_connect(state, pool);
	case SETUP_POOL_CONNECT:
		arg->setup_state = SETUP_CONT_CREATE;
		return test_setup_cont_create(state);
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
		arg = malloc(sizeof(test_arg_t));
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
		rc = test_setup_next_step(state, pool);

	 if (rc) {
		free(arg);
		*state = NULL;
	}
	return rc;
}

static int
pool_destroy_safe(test_arg_t *arg)
{
	daos_pool_info_t		 pinfo;
	daos_handle_t			 poh = arg->pool.poh;
	bool				 connected = false;
	int				 rc;

	if (daos_handle_is_inval(poh)) {
		rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
				       &arg->pool.svc, DAOS_PC_RW,
				       &poh, &arg->pool.pool_info,
				       NULL /* ev */);
		if (rc != 0) { /* destory straightaway */
			print_message("failed to connect pool: %d\n", rc);
			poh = DAOS_HDL_INVAL;
		} else {
			connected = true;
		}
	}

	while (!daos_handle_is_inval(poh)) {
		struct daos_rebuild_status *rstat = &pinfo.pi_rebuild_st;

		memset(&pinfo, 0, sizeof(pinfo));
		rc = daos_pool_query(poh, NULL, &pinfo, NULL);
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
		if (connected)
			daos_pool_disconnect(poh, NULL);
		break;
	}

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
			print_message("failed to destroy container "DF_UUIDF
				      ": %d\n", DP_UUID(arg->co_uuid), rc);
			return rc;
		}
	}

	if (!daos_handle_is_inval(arg->pool.poh) && !arg->pool.slave) {
		rc = daos_pool_disconnect(arg->pool.poh, NULL /* ev */);
		arg->pool.poh = DAOS_HDL_INVAL;
		if (arg->multi_rank) {
			MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN,
				      MPI_COMM_WORLD);
			rc = rc_reduce;
		}
		if (rc) {
			print_message("failed to disconnect pool "DF_UUIDF
				      ": %d\n", DP_UUID(arg->pool.pool_uuid),
				      rc);
			return rc;
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

	D_FREE_PTR(arg);
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
