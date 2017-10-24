/**
 * (C) Copyright 2016 Intel Corporation.
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
 * This file is part of daos_m
 *
 * src/tests/addons/
 */

#define DDSUBSYS	DDFAC(tests)

#include <daos_test.h>
#include "daos_addons_test.h"

/** Server crt group ID */
static const char *server_group;

/** Pool service replicas */
static unsigned int svc_nreplicas = 1;

static daos_size_t pool_size = (4ULL << 30);

int
setup(void **state, unsigned int step, bool multi_rank)
{
	test_arg_t	*arg;
	int		 i;
	int		 rc;

	arg = malloc(sizeof(test_arg_t));
	if (arg == NULL)
		return -1;

	memset(arg, 0, sizeof(*arg));

	MPI_Comm_rank(MPI_COMM_WORLD, &arg->myrank);
	MPI_Comm_size(MPI_COMM_WORLD, &arg->rank_size);
	arg->multi_rank = multi_rank;

	arg->svc.rl_nr.num = svc_nreplicas;
	arg->svc.rl_nr.num_out = svc_nreplicas;
	arg->svc.rl_ranks = arg->ranks;
	for (i = 0; i < svc_nreplicas; i++)
		arg->svc.rl_ranks[i] = i;

	arg->mode = 0731;
	arg->uid = geteuid();
	arg->gid = getegid();

	arg->group = server_group;
	uuid_clear(arg->pool_uuid);
	uuid_clear(arg->co_uuid);

	arg->hdl_share = false;
	arg->poh = DAOS_HDL_INVAL;
	arg->coh = DAOS_HDL_INVAL;

	rc = daos_eq_create(&arg->eq);
	if (rc)
		return rc;

	if (step == SETUP_EQ)
		goto out;

	/** create pool */
	if (arg->myrank == 0) {
		char	*env;

		env = getenv("POOL_SIZE");
		if (env) {
			int size_gb;

			size_gb = atoi(env);
			if (size_gb != 0)
				pool_size = (daos_size_t)size_gb << 30;
		}

		print_message("setup: creating pool size="DF_U64" GB\n",
			      (pool_size >> 30));

		rc = daos_pool_create(0731, geteuid(), getegid(), arg->group,
				      NULL, "pmem", pool_size, &arg->svc,
				      arg->pool_uuid, NULL);
		if (rc)
			print_message("daos_pool_create failed, rc: %d\n", rc);
		else
			print_message("setup: created pool "DF_UUIDF"\n",
				       DP_UUID(arg->pool_uuid));
	}
	/** broadcast pool create result */
	if (multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** broadcast pool UUID and svc addresses */
	if (arg->myrank == 0)
		arg->svc.rl_nr.num = arg->svc.rl_nr.num_out;
	if (multi_rank) {
		MPI_Bcast(arg->pool_uuid, 16, MPI_CHAR, 0, MPI_COMM_WORLD);
		MPI_Bcast(&arg->svc, sizeof(arg->pool_info), MPI_CHAR, 0,
			  MPI_COMM_WORLD);
	}

	if (step == SETUP_POOL_CREATE)
		goto out;

	/** connect to pool */
	if (arg->myrank == 0) {
		print_message("setup: connecting to pool\n");
		rc = daos_pool_connect(arg->pool_uuid, arg->group, &arg->svc,
				       DAOS_PC_RW, &arg->poh, &arg->pool_info,
				       NULL /* ev */);
		if (rc)
			print_message("daos_pool_connect failed, rc: %d\n", rc);
		else
			print_message("connected to pool, ntarget=%d\n",
				      arg->pool_info.pi_ntargets);
	}
	/** broadcast pool connect result */
	if (multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** broadcast pool info */
	if (multi_rank)
		MPI_Bcast(&arg->pool_info, sizeof(arg->pool_info), MPI_CHAR, 0,
			  MPI_COMM_WORLD);

	/** l2g and g2l the pool handle */
	if (multi_rank)
		handle_share(&arg->poh, HANDLE_POOL, arg->myrank, arg->poh, 0);

	if (step == SETUP_POOL_CONNECT)
		goto out;

	/** create container */
	if (arg->myrank == 0) {
		print_message("setup: creating container\n");
		uuid_generate(arg->co_uuid);
		rc = daos_cont_create(arg->poh, arg->co_uuid, NULL);
		if (rc)
			print_message("daos_cont_create failed, rc: %d\n", rc);
	}
	/** broadcast container create result */
	if (multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** broadcast container UUID */
	if (multi_rank)
		MPI_Bcast(arg->co_uuid, 16, MPI_CHAR, 0, MPI_COMM_WORLD);

	if (step == SETUP_CONT_CREATE)
		goto out;

	/** open container */
	if (arg->myrank == 0) {
		print_message("setup: opening container\n");
		rc = daos_cont_open(arg->poh, arg->co_uuid, DAOS_COO_RW,
				    &arg->coh, &arg->co_info, NULL);
		if (rc)
			print_message("daos_cont_open failed, rc: %d\n", rc);
	}
	/** broadcast container open result */
	if (multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** l2g and g2l the container handle */
	if (multi_rank)
		handle_share(&arg->coh, HANDLE_CO, arg->myrank, arg->poh, 0);

	if (step == SETUP_CONT_CONNECT)
		goto out;
out:
	*state = arg;
	return 0;
}


static int
pool_destroy_safe(test_arg_t *arg)
{
	daos_pool_info_t		 pinfo;
	daos_handle_t			 poh = arg->poh;
	bool				 connected = false;
	int				 rc;

	if (daos_handle_is_inval(poh)) {
		rc = daos_pool_connect(arg->pool_uuid, arg->group, &arg->svc,
				       DAOS_PC_RW, &poh, &arg->pool_info,
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

		if (rstat->rs_version != 0) {
			print_message("waiting for rebuild\n");
			sleep(1);
			continue;
		}

		/* no rebuild */
		if (connected)
			daos_pool_disconnect(poh, NULL);
		break;
	}

	rc = daos_pool_destroy(arg->pool_uuid, arg->group, 1, NULL);
	if (rc && rc != -DER_TIMEDOUT)
		print_message("daos_pool_destroy failed, rc: %d\n", rc);
	return rc;
}

int
teardown(void **state)
{
	test_arg_t	*arg = *state;
	int		 rc, rc_reduce = 0;

	if (arg->multi_rank)
		MPI_Barrier(MPI_COMM_WORLD);

	if (!daos_handle_is_inval(arg->coh)) {
		rc = daos_cont_close(arg->coh, NULL);
		if (arg->multi_rank) {
			MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN,
				      MPI_COMM_WORLD);
			rc = rc_reduce;
		}
		if (rc) {
			print_message("failed to close container "DF_UUIDF
				      ": %d\n", DP_UUID(arg->co_uuid), rc);
			return rc;
		}
	}

	if (!uuid_is_null(arg->co_uuid)) {
		while (arg->myrank == 0) {
			rc = daos_cont_destroy(arg->poh, arg->co_uuid, 1, NULL);
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

	if (!daos_handle_is_inval(arg->poh)) {
		rc = daos_pool_disconnect(arg->poh, NULL /* ev */);
		arg->poh = DAOS_HDL_INVAL;
		if (arg->multi_rank) {
			MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN,
				      MPI_COMM_WORLD);
			rc = rc_reduce;
		}
		if (rc) {
			print_message("failed to disconnect pool "DF_UUIDF
				      ": %d\n", DP_UUID(arg->pool_uuid), rc);
			return rc;
		}
	}

	if (!uuid_is_null(arg->pool_uuid)) {
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

	D__FREE_PTR(arg);
	return 0;
}

int
main(int argc, char **argv)
{
	test_arg_t	*arg;
	int	nr_failed = 0;
	int	nr_total_failed = 0;
	int	rank;
	int	size;
	int	rc;

	MPI_Init(&argc, &argv);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	rc = daos_init();
	if (rc) {
		print_message("daos_init() failed with %d\n", rc);
		return -1;
	}

	if (svc_nreplicas > ARRAY_SIZE(arg->ranks) && rank == 0) {
		print_message("at most %zu service replicas allowed\n",
			      ARRAY_SIZE(arg->ranks));
		return -1;
	}

	daos_test_print(rank, "\n\n=================");
	daos_test_print(rank, "DAOS ADDONS Array tests..");
	daos_test_print(rank, "=====================");
	nr_failed = run_array_test(rank, size);

	daos_test_print(rank, "\n\n=================");
	daos_test_print(rank, "DAOS ADDONS HL tests..");
	daos_test_print(rank, "=====================");
	nr_failed += run_hl_test(rank, size);

	MPI_Allreduce(&nr_failed, &nr_total_failed, 1, MPI_INT, MPI_SUM,
		      MPI_COMM_WORLD);

	rc = daos_fini();
	if (rc)
		print_message("daos_fini() failed with %d\n", rc);

	if (!rank) {
		print_message("\n============ Summary %s\n", __FILE__);
		if (nr_total_failed == 0)
			print_message("OK - NO TEST FAILURES\n");
		else
			print_message("ERROR, %i TEST(S) FAILED\n",
				      nr_total_failed);
	}

	MPI_Finalize();

	return nr_failed;
}
