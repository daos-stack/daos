/**
 * (C) Copyright 2020 Intel Corporation.
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

/*
 * This provides a simple example for how to access different DAOS objects.
 *
 * For more information on the DAOS object model, please visit this
 * page: https://daos-stack.github.io/overview/storage/#daos-object
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <mpi.h>
#include <daos.h>

/** local task information */
static char		node[128] = "unknown";
static daos_handle_t	poh;
static daos_handle_t	coh;
static int		rank, rankn;
MPI_Comm		test_comm;

#define FAIL(fmt, ...)						\
do {								\
	fprintf(stderr, "Process (%s): " fmt " aborting\n",	\
		node, ## __VA_ARGS__);				\
	exit(1);						\
} while (0)

#define	ASSERT(cond, ...)					\
do {								\
	if (!(cond))						\
		FAIL(__VA_ARGS__);				\
} while (0)

enum handleType {
	HANDLE_POOL,
	HANDLE_CO,
};

#define ENUM_DESC_BUF	512
#define ENUM_DESC_NR	5

enum {
	OBJ_DKEY,
	OBJ_AKEY
};

static inline int
rand_rank() {
	return rand() % rankn;
}

static void
dts_buf_render(char *buf, unsigned int buf_len)
{
	int	nr = 'z' - 'a' + 1;
	int	i;

	for (i = 0; i < buf_len - 1; i++) {
		int randv = rand() % (2 * nr);

		if (randv < nr)
			buf[i] = 'a' + randv;
		else
			buf[i] = 'A' + (randv - nr);
	}
	buf[i] = '\0';
}

static inline void
handle_share(daos_handle_t *hdl, int type)
{
	d_iov_t	ghdl = { NULL, 0, 0 };
	int	rc;

	if (rank == 0) {
		/** fetch size of global handle */
		if (type == HANDLE_POOL)
			rc = daos_pool_local2global(*hdl, &ghdl);
		else
			rc = daos_cont_local2global(*hdl, &ghdl);
		ASSERT(rc == 0, "local2global failed with %d", rc);
	}

	/** broadcast size of global handle to all peers */
	MPI_Bcast(&ghdl.iov_buf_len, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);

	/** allocate buffer for global pool handle */
	ghdl.iov_buf = malloc(ghdl.iov_buf_len);
	ghdl.iov_len = ghdl.iov_buf_len;

	if (rank == 0) {
		/** generate actual global handle to share with peer tasks */
		if (type == HANDLE_POOL)
			rc = daos_pool_local2global(*hdl, &ghdl);
		else
			rc = daos_cont_local2global(*hdl, &ghdl);
		ASSERT(rc == 0, "local2global failed with %d", rc);
	}

	/** broadcast global handle to all peers */
	MPI_Bcast(ghdl.iov_buf, ghdl.iov_len, MPI_BYTE, 0, MPI_COMM_WORLD);

	if (rank != 0) {
		/** unpack global handle */
		if (type == HANDLE_POOL) {
			/* NB: Only pool_global2local are different */
			rc = daos_pool_global2local(ghdl, hdl);
		} else {
			rc = daos_cont_global2local(poh, ghdl, hdl);
		}
		ASSERT(rc == 0, "global2local failed with %d", rc);
	}

	free(ghdl.iov_buf);

	MPI_Barrier(MPI_COMM_WORLD);
}

static void
enumerate_key(daos_handle_t oh, daos_handle_t th, int *total_nr,
	      daos_key_t *dkey, int key_type)
{
	char		*buf;
	daos_key_desc_t  kds[ENUM_DESC_NR];
	daos_anchor_t	 anchor = {0};
	d_sg_list_t	 sgl;
	d_iov_t		 sg_iov;
	int		 key_nr = 0;
	int		 rc;

	buf = malloc(ENUM_DESC_BUF);
	d_iov_set(&sg_iov, buf, ENUM_DESC_BUF);
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t nr = ENUM_DESC_NR;

		memset(buf, 0, ENUM_DESC_BUF);
		if (key_type == OBJ_DKEY)
			rc = daos_obj_list_dkey(oh, th, &nr, kds,
						&sgl, &anchor, NULL);
		else
			rc = daos_obj_list_akey(oh, th, dkey, &nr,
						kds, &sgl, &anchor, NULL);
		ASSERT(rc == 0, "object list failed with %d", rc);
		if (nr == 0)
			continue;
		key_nr += nr;
	}

	*total_nr = key_nr;
}

#define KEYS 10
#define BUFLEN 1024

void
demo_daos_key_conflict(int use_dtx)
{
	daos_handle_t	oh, th = DAOS_TX_NONE;
	char		buf[BUFLEN];
	daos_obj_id_t	oid;
	d_iov_t		dkey;
	int		total_nr = 0;
	char		dkey_str[10];
	char		akey_str[10];
	d_sg_list_t	sgl;
	d_iov_t		sg_iov;
	daos_iod_t	iod;
	int		conflictor;
	int		rc;

	if (rank == 0)
		printf("Testing simple DKEY IO conflict detection:\n");

	oid.hi = 0;
	oid.lo = 2;
	daos_obj_generate_id(&oid, 0, OC_SX, 0);

	srand(time(0));

	rc = daos_obj_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	ASSERT(rc == 0, "object open failed with %d", rc);

	dts_buf_render(buf, BUFLEN);

	sprintf(dkey_str, "dkey_%d", 0);
	d_iov_set(&dkey, dkey_str, strlen(dkey_str));
	d_iov_set(&sg_iov, buf, BUFLEN);
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;
	sprintf(akey_str, "akey_%d", 0);
	d_iov_set(&iod.iod_name, akey_str, strlen(akey_str));

	iod.iod_nr	= 1; /** has to be 1 for single value */
	iod.iod_size	= BUFLEN; /** size of the single value */
	iod.iod_recxs	= NULL; /** recx is ignored for single value */
	iod.iod_type	= DAOS_IOD_SINGLE; /** value type of the akey */

	if (rank == 0) {
		rc = daos_obj_update(oh, th, 0, &dkey, 1, &iod, &sgl, NULL);
		ASSERT(rc == 0, "object update failed with %d", rc);
	}

	if (rank == 0)
		conflictor = rand_rank();
	MPI_Bcast(&conflictor, 1, MPI_INT, 0, MPI_COMM_WORLD);

	if (use_dtx) {
		rc = daos_tx_open(coh, &th, 0, NULL);
		ASSERT(rc == 0, "daos_tx_open() failed with %d\n", rc);
	}

	if (rank == conflictor) {
		rc = daos_obj_fetch(oh, th, 0, &dkey, 1, &iod, &sgl,
				    NULL, NULL);
		ASSERT(rc == 0, "object fetch failed with %d", rc);
	}

	MPI_Barrier(MPI_COMM_WORLD);

	if (rank != conflictor) {
		rc = daos_obj_fetch(oh, th, 0, &dkey, 1, &iod, &sgl,
				    NULL, NULL);
		ASSERT(rc == 0, "object fetch failed with %d", rc);
	}

	MPI_Barrier(MPI_COMM_WORLD);

	if (rank == conflictor) {
		printf("Rank %d updating dkey\n", rank);
		//rc = daos_obj_punch_dkeys(oh, th, 0, 1, &dkey, NULL);
		//ASSERT(rc == 0, "object punch failed with %d", rc);
		rc = daos_obj_update(oh, th, 0, &dkey, 1, &iod, &sgl, NULL);
		ASSERT(rc == 0, "object update failed with %d", rc);
	}

	if (use_dtx) {
		rc = daos_tx_commit(th, NULL);
		if (rc) {
			printf("Commit on rank %d failed with %s\n", rank, d_errstr(rc));
			ASSERT(rc == -DER_TX_RESTART, "invalid error from commit");
		}

		rc = daos_tx_close(th,  NULL);
		ASSERT(rc == 0, "daos_tx_close() failed with %d\n", rc);	
	}

	enumerate_key(oh, DAOS_TX_NONE, &total_nr, NULL, OBJ_DKEY);
	ASSERT(total_nr == 1, "wrong number of dkeys listed");

	daos_obj_close(oh, NULL);

	MPI_Barrier(MPI_COMM_WORLD);
	if (rank == 0)
		printf("---------------------- DONE\n");
}

void
demo_daos_unlink_conflict(bool use_dtx)
{
	daos_handle_t	oh, th = DAOS_TX_NONE;
	daos_obj_id_t	oid;
	size_t		size = sizeof(int);
	int		val;
	int		rc;

	if (rank == 0)
		printf("Testing insert / unlink conflict:\n");

	oid.hi = 0;
	oid.lo = 3;
	daos_obj_generate_id(&oid, DAOS_OF_KV_FLAT, OC_SX, 0);

	/** open the KV object */
	rc = daos_kv_open(coh, oid, 0, &oh, NULL);
	ASSERT(rc == 0, "failed to open kv object %d", rc);

	/** create key A with value 1 */
	/** rank 1 fetch dkey A and punch only if value == 1 */
	/** rank 1 create dkey A with value 2 */
	/** rank 2 fetch dkey A and punch only if value == 1 */

	if (rank == 0) {
		printf("insert 1 key in KV object\n");
		val = 1;
		rc = daos_kv_put(oh, th, 0, "KeyA", sizeof(int), &val, NULL);
		ASSERT(rc == 0, "daos_kv_put() failed %d", rc);
	}
	MPI_Barrier(test_comm);

	if (use_dtx) {
		rc = daos_tx_open(coh, &th, 0, NULL);
		ASSERT(rc == 0, "daos_tx_open() failed with %d\n", rc);
	}

	if (rank == 0) {
		printf("Rank 0 check key value.\n");
		rc = daos_kv_get(oh, th, 0, "KeyA", &size, &val, NULL);
		ASSERT(rc == 0, "daos_kv_get() failed %d", rc);

		if (val == 1) {
			MPI_Barrier(test_comm); /** Barrier 1 */

			printf("Rank 0 remove Key.\n");
			rc = daos_kv_remove(oh, th, 0, "KeyA", NULL);
			ASSERT(rc == 0, "daos_kv_remove() failed %d", rc);

			val = 2;
			printf("Rank 0 insert same Key with different value.\n");
			rc = daos_kv_put(oh, th, 0, "KeyA", sizeof(int), &val, NULL);
			ASSERT(rc == 0, "daos_kv_put() failed %d", rc);

			MPI_Barrier(test_comm); /** Barrier 2 */
		}
	} else if (rank == 1) {
		printf("Rank 1 check key value.\n");
		rc = daos_kv_get(oh, th, 0, "KeyA", &size, &val, NULL);
		ASSERT(rc == 0, "daos_kv_get() failed %d", rc);

		if (val == 1) {
			/** this gets delayed now for some reason (simulate with barrier) */
			MPI_Barrier(test_comm); /** Barrier 1 */
			MPI_Barrier(test_comm); /** Barrier 2 */

			printf("Rank 1 remove Key.\n");
			rc = daos_kv_remove(oh, th, 0, "KeyA", NULL);
			ASSERT(rc == 0, "daos_kv_remove() failed %d", rc);
		}
	}

	if (use_dtx) {
		rc = daos_tx_commit(th, NULL);
		if (rc) {
			printf("Commit on rank %d failed with %s\n", rank, d_errstr(rc));
			ASSERT(rc == -DER_TX_RESTART, "invalid error from commit");
		}

		rc = daos_tx_close(th,  NULL);
		ASSERT(rc == 0, "daos_tx_close() failed with %d\n", rc);	
	}

	rc = daos_kv_close(oh, NULL);
	ASSERT(rc == 0, "daos_kv_close() failed with %d\n", rc);

	MPI_Barrier(test_comm);
	if (rank == 0)
		printf("---------------------- DONE\n");
}

int
main(int argc, char **argv)
{
	uuid_t		pool_uuid, co_uuid;
	int		use_dtx;
	int		rc;

	rc = MPI_Init(&argc, &argv);
	ASSERT(rc == MPI_SUCCESS, "MPI_Init failed with %d", rc);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &rankn);

	if (rankn < 2) {
		printf("Need at least 2 MPI procs..\n");
		MPI_Finalize();
		return 0;
	}
	
	rc = gethostname(node, sizeof(node));
	ASSERT(rc == 0, "buffer for hostname too small");

	if (argc != 3) {
		fprintf(stderr, "args: pool use_dtx\n");
		exit(1);
	}

	/** initialize the local DAOS stack */
	rc = daos_init();
	ASSERT(rc == 0, "daos_init failed with %d", rc);

	/** parse the pool information and connect to the pool */
	rc = uuid_parse(argv[1], pool_uuid);
	ASSERT(rc == 0, "Failed to parse 'Pool uuid': %s", argv[1]);

	/** Call connect on rank 0 only and broadcast handle to others */
	if (rank == 0) {
		rc = daos_pool_connect(pool_uuid, NULL, NULL, DAOS_PC_RW, &poh,
				       NULL, NULL);
		ASSERT(rc == 0, "pool connect failed with %d", rc);
	}
	/** share pool handle with peer tasks */
	handle_share(&poh, HANDLE_POOL);

	/*
	 * Create and open container on rank 0 and share the handle.
	 *
	 * Alternatively, one could create the container outside of this program
	 * using the daos utility: daos cont create --pool=puuid
	 * and pass the uuid to the app.
	 */
	if (rank == 0) {
		/** generate uuid for container */
		uuid_generate(co_uuid);

		/** create container */
		rc = daos_cont_create(poh, co_uuid, NULL /* properties */,
				      NULL /* event */);
		ASSERT(rc == 0, "container create failed with %d", rc);

		/** open container */
		rc = daos_cont_open(poh, co_uuid, DAOS_COO_RW, &coh, NULL,
				    NULL);
		ASSERT(rc == 0, "container open failed with %d", rc);
	}
	/** share container handle with peer tasks */
	handle_share(&coh, HANDLE_CO);

	use_dtx = atoi(argv[2]);

	demo_daos_key_conflict(use_dtx);

	int color;

	if (rank < 2)
		color = 1;
	else
		color = 2;
	MPI_Comm_split(MPI_COMM_WORLD, color, rank, &test_comm);

	if (rank < 2)
		demo_daos_unlink_conflict(use_dtx);

	MPI_Comm_free(&test_comm);

	MPI_Barrier(MPI_COMM_WORLD);

	rc = daos_cont_close(coh, NULL);
	ASSERT(rc == 0, "cont close failed");

	rc = daos_pool_disconnect(poh, NULL);
	ASSERT(rc == 0, "disconnect failed");

	rc = daos_fini();
	ASSERT(rc == 0, "daos_fini failed with %d", rc);

	MPI_Finalize();
	return rc;
}
