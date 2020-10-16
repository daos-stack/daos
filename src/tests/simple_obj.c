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
#include <mpi.h>
#include <daos.h>

/** local task information */
static char		node[128] = "unknown";
static daos_handle_t	poh;
static daos_handle_t	coh;
static int		rank, rankn;
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
enumerate_key(daos_handle_t oh, int *total_nr, daos_key_t *dkey, int key_type)
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
			rc = daos_obj_list_dkey(oh, DAOS_TX_NONE, &nr, kds,
						&sgl, &anchor, NULL);
		else
			rc = daos_obj_list_akey(oh, DAOS_TX_NONE, dkey, &nr,
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
example_daos_key_array()
{
	daos_handle_t	oh;
	char		buf[BUFLEN], rbuf[BUFLEN];
	daos_obj_id_t	oid;
	d_iov_t		dkey;
	int		total_nr = 0;
	char		dkey_str[10];
	int		i, rc;

	if (rank == 0)
		printf("Example of DAOS Key array:\n");

	/*
	 * Set an object ID. This is chosen by the user.
	 *
	 * DAOS provides a unique 64 bit integer oid allocator that can be used
	 * for the oid.lo to allocate 1 or more unique oids in the
	 * container. Please see: daos_cont_alloc_oids();
	 */
	oid.hi = 0;
	oid.lo = 1;

	/*
	 * generate objid to encode feature flags and object class to the
	 * OID. The object class controls the sharding and redundancy of the
	 * object (replication, Erasure coding, no protection). In this case, we
	 * choose max striping with no data prorection - OC_SX.
	 */
	daos_obj_generate_id(&oid, 0, OC_SX, 0);

	/** open DAOS object */
	rc = daos_obj_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	ASSERT(rc == 0, "object open failed with %d", rc);

	/*
	 * In this example, we will create an object with 10 dkeys, where each
	 * dkey has 1 akey, and and array with a 1k extent. A user can create as
	 * many dkeys as they like under a single object. All akeys and values
	 * under the same dkey are guaranteed to be colocated on the same
	 * storage target. There is no limitation on how many akeys that can be
	 * created under a single dkey or how many records can be stored under a
	 * single akey, other than the space available on a single target.
	 */

	/*
	 * init buffer (for this example, we reuse the same buffer for all the
	 * updates just for simplicity.
	 */
	dts_buf_render(buf, BUFLEN);

	for (i = 0; i < KEYS; i++) {
		d_sg_list_t	sgl;
		d_iov_t		sg_iov;
		daos_iod_t	iod;
		daos_recx_t	recx;

		/** init dkey */
		sprintf(dkey_str, "dkey_%d", i);
		d_iov_set(&dkey, dkey_str, strlen(dkey_str));

		/*
		 * init scatter/gather. this describes data in your memory. in
		 * this case it's a single contiguous buffer, but this gives the
		 * ability to provide an iovec for segmented buffer in memory.
		 */
		d_iov_set(&sg_iov, buf, BUFLEN);
		sgl.sg_nr		= 1;
		sgl.sg_nr_out		= 0;
		sgl.sg_iovs		= &sg_iov;

		/** init I/O descriptor */
		d_iov_set(&iod.iod_name, "akey", strlen("akey")); /** akey */

		/*
		 * number of extents in recx array, 1 means we are accessing a
		 * single contiguous extent. we can have segmented/partial
		 * access similar to an iovec for file offsets. Each process
		 * writes 1k extent contiguously: 0: 0, 1:1024, 2:2048, etc.
		 */
		iod.iod_nr	= 1;
		iod.iod_size	= 1; /** record size (1 byte array here) */
		recx.rx_nr	= BUFLEN; /** extent size */
		recx.rx_idx	= rank * BUFLEN; /** extent offset */
		iod.iod_recxs	= &recx;
		iod.iod_type	= DAOS_IOD_ARRAY; /** value type of the akey */

		/*
		 * Update a dkey. In this case we have 1 akey under this dkey,
		 * hence 1 iod and 1 sgl. for multiple akey access, this
		 * function is used with an array of iods and sgls and the
		 * number of akeys passed as the nr (5th argument to this
		 * function.
		 */
		rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
				     NULL);
		ASSERT(rc == 0, "object update failed with %d", rc);
	}

	for (i = 0; i < KEYS; i++) {
		d_sg_list_t	sgl;
		d_iov_t		sg_iov;
		daos_iod_t	iod;
		daos_recx_t	recx;

		/** init dkey */
		sprintf(dkey_str, "dkey_%d", i);
		d_iov_set(&dkey, dkey_str, strlen(dkey_str));

		/** init scatter/gather */
		d_iov_set(&sg_iov, rbuf, BUFLEN);
		sgl.sg_nr		= 1;
		sgl.sg_nr_out		= 0;
		sgl.sg_iovs		= &sg_iov;

		/** init I/O descriptor */
		d_iov_set(&iod.iod_name, "akey", strlen("akey")); /** akey */
		iod.iod_nr	= 1; /** number of extents in recx array */
		iod.iod_size	= 1; /** record size (1 byte array here) */
		recx.rx_nr	= BUFLEN; /** extent size */
		recx.rx_idx	= rank * BUFLEN; /** extent offset */
		iod.iod_recxs	= &recx;
		iod.iod_type	= DAOS_IOD_ARRAY; /** value type of the akey */

		/** fetch a dkey */
		rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
				    NULL, NULL);
		ASSERT(rc == 0, "object update failed with %d", rc);

		if (memcmp(buf, rbuf, BUFLEN) != 0)
			ASSERT(0, "Data verification");
		memset(rbuf, 0, BUFLEN);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	/** list all dkeys */
	enumerate_key(oh, &total_nr, NULL, OBJ_DKEY);
	ASSERT(total_nr == KEYS, "key enumeration failed");

	MPI_Barrier(MPI_COMM_WORLD);
	if (rank == 0) {
		/** punch/remove 1 akey */
		sprintf(dkey_str, "dkey_%d", 2);
		d_iov_set(&dkey, dkey_str, strlen(dkey_str));
		rc = daos_obj_punch_dkeys(oh, DAOS_TX_NONE, 0, 1, &dkey, NULL);
		ASSERT(rc == 0, "object punch failed with %d", rc);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	/** list all dkeys again (should have 1 less) */
	enumerate_key(oh, &total_nr, NULL, OBJ_DKEY);
	ASSERT(total_nr == KEYS - 1, "key enumeration failed");

	daos_obj_close(oh, NULL);

	MPI_Barrier(MPI_COMM_WORLD);
	if (rank == 0)
		printf("SUCCESS\n");
}

void
example_daos_key_sv()
{
	daos_handle_t	oh;
	char		buf[BUFLEN], rbuf[BUFLEN];
	daos_obj_id_t	oid;
	d_iov_t		dkey;
	int		total_nr = 0;
	char		dkey_str[10];
	char		akey_str[10];
	int		i, rc;

	MPI_Barrier(MPI_COMM_WORLD);
	if (rank == 0)
		printf("Example of DAOS Key Single Value type:\n");

	/*
	 * Most of this example is the same as the key_array one, except the
	 * type of the value under the akey will be a single value of size
	 * 1024. This is a single value that is atomically updated / read, and
	 * no partial access is allowed.
	 */

	oid.hi = 0;
	oid.lo = 2;
	daos_obj_generate_id(&oid, 0, OC_SX, 0);

	rc = daos_obj_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	ASSERT(rc == 0, "object open failed with %d", rc);

	/*
	 * In this example, we will create an object with 10 dkeys, where each
	 * dkey has 1 akey, and a Single Value of size 1k. A user can create as
	 * many dkeys as they like under a single object. All akeys and values
	 * under the same dkey are guaranteed to be colocated on the same
	 * storage target. A user can update the akey after it was first
	 * inserted with a new single value of a different size, but the old
	 * value is atomically removed and updated to the new value.
	 */

	dts_buf_render(buf, BUFLEN);

	for (i = 0; i < KEYS; i++) {
		d_sg_list_t	sgl;
		d_iov_t		sg_iov;
		daos_iod_t	iod;

		sprintf(dkey_str, "dkey_%d", i);
		d_iov_set(&dkey, dkey_str, strlen(dkey_str));

		d_iov_set(&sg_iov, buf, BUFLEN);
		sgl.sg_nr		= 1;
		sgl.sg_nr_out		= 0;
		sgl.sg_iovs		= &sg_iov;

		/*
		 * Unlike the dkey_array case, where all ranks can update
		 * different extents of the value in the same akey, with a
		 * single value, the last update to the akey wins. in this case,
		 * each rank will create a separate akey under the same dkey
		 * with it's rank attached to akey name.
		 */
		sprintf(akey_str, "akey_%d", rank);
		d_iov_set(&iod.iod_name, akey_str, strlen(akey_str));

		iod.iod_nr	= 1; /** has to be 1 for single value */
		iod.iod_size	= BUFLEN; /** size of the single value */
		iod.iod_recxs	= NULL; /** recx is ignored for single value */
		iod.iod_type	= DAOS_IOD_SINGLE; /** value type of the akey */

		rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
				     NULL);
		ASSERT(rc == 0, "object update failed with %d", rc);
	}

	for (i = 0; i < KEYS; i++) {
		d_sg_list_t	sgl;
		d_iov_t		sg_iov;
		daos_iod_t	iod;

		/** init dkey */
		sprintf(dkey_str, "dkey_%d", i);
		d_iov_set(&dkey, dkey_str, strlen(dkey_str));

		/** init scatter/gather */
		d_iov_set(&sg_iov, rbuf, BUFLEN);
		sgl.sg_nr		= 1;
		sgl.sg_nr_out		= 0;
		sgl.sg_iovs		= &sg_iov;

		/** init I/O descriptor */
		sprintf(akey_str, "akey_%d", rank);
		d_iov_set(&iod.iod_name, akey_str, strlen(akey_str));
		iod.iod_nr	= 1;
		/*
		 * Size of the single value. if user doesn't know the length,
		 * they can set this qto DAOS_REC_ANY (0) and pass a NULL
		 * sgl. after the fetch, DAOS reports the actual size of the
		 * value.
		 */
		iod.iod_size	= BUFLEN;
		iod.iod_recxs	= NULL;
		iod.iod_type	= DAOS_IOD_SINGLE;

		/** fetch a dkey */
		rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
				    NULL, NULL);
		ASSERT(rc == 0, "object update failed with %d", rc);

		if (memcmp(buf, rbuf, BUFLEN) != 0)
			ASSERT(0, "Data verification");
		memset(rbuf, 0, BUFLEN);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	/** list all dkeys */
	enumerate_key(oh, &total_nr, NULL, OBJ_DKEY);
	ASSERT(total_nr == KEYS, "key enumeration failed");

	MPI_Barrier(MPI_COMM_WORLD);
	if (rank == 0) {
		/** punch/remove 1 akey */
		sprintf(dkey_str, "dkey_%d", 2);
		d_iov_set(&dkey, dkey_str, strlen(dkey_str));
		rc = daos_obj_punch_dkeys(oh, DAOS_TX_NONE, 0, 1, &dkey, NULL);
		ASSERT(rc == 0, "object punch failed with %d", rc);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	/** list all dkeys again (should have 1 less) */
	enumerate_key(oh, &total_nr, NULL, OBJ_DKEY);
	ASSERT(total_nr == KEYS - 1, "key enumeration failed");

	daos_obj_close(oh, NULL);

	MPI_Barrier(MPI_COMM_WORLD);
	if (rank == 0)
		printf("SUCCESS\n");
}

void
example_daos_array()
{
	daos_handle_t	oh;
	char		buf[BUFLEN], rbuf[BUFLEN];
	daos_obj_id_t	oid;
	int		rc;

	if (rank == 0)
		printf("Example of DAOS Array:\n");

	/*
	 * Set an object ID. This is chosen by the user.
	 *
	 * DAOS provides a unique 64 bit integer oid allocator that can be used
	 * for the oid.lo to allocate 1 or more unique oids in the
	 * container. Please see: daos_cont_alloc_oids();
	 */
	oid.hi = 0;
	oid.lo = 3;

	/*
	 * generate an array objid to encode feature flags and object class to
	 * the OID. This is a convenience function over the
	 * daos_obj_generate_id() that adds the required feature flags for an
	 * array: DAOS_OF_DKEY_UINT64 | DAOS_OF_KV_FLAT | DAOS_OF_ARRAY
	 */
	daos_array_generate_id(&oid, OC_SX, true, 0);

	/*
	 * Create the array object with cell size 1 (byte array) and 1m chunk
	 * size (similar to stripe size in Lustre). Both are configurable by the
	 * user of course.
	 */
	if (rank == 0) {
		rc = daos_array_create(coh, oid, DAOS_TX_NONE, 1, 1048576, &oh,
				       NULL);
		ASSERT(rc == 0, "array create failed with %d", rc);
	}

	MPI_Barrier(MPI_COMM_WORLD);

	if (rank != 0) {
		size_t cell_size, csize;

		rc = daos_array_open(coh, oid, DAOS_TX_NONE, DAOS_OO_RW,
				     &cell_size, &csize, &oh, NULL);
		ASSERT(rc == 0, "array open failed with %d", rc);
		ASSERT(cell_size == 1, "array open failed");
		ASSERT(csize == 1048576, "array open failed");
	}

	daos_array_iod_t iod;
	d_sg_list_t	sgl;
	daos_range_t	rg;
	d_iov_t		iov;
	daos_size_t	array_size;

	/** set array location */
	iod.arr_nr = 1; /** number of ranges / array iovec */
	rg.rg_len = BUFLEN; /** length */
	rg.rg_idx = rank * BUFLEN; /** offset */
	iod.arr_rgs = &rg;

	/** set memory location, each rank writing BUFLEN */
	sgl.sg_nr = 1;
	d_iov_set(&iov, buf, BUFLEN);
	sgl.sg_iovs = &iov;

	/** Write */
	rc = daos_array_write(oh, DAOS_TX_NONE, &iod, &sgl, NULL);
	ASSERT(rc == 0, "array write failed with %d", rc);

	MPI_Barrier(MPI_COMM_WORLD);

	/** check size */
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
	ASSERT(rc == 0, "array get_size failed with %d", rc);
	ASSERT(array_size == BUFLEN * rankn, "key enumeration failed");

	d_iov_set(&iov, rbuf, BUFLEN);
	sgl.sg_iovs = &iov;

	/** read & verify */
	rc = daos_array_read(oh, DAOS_TX_NONE, &iod, &sgl, NULL);
	ASSERT(rc == 0, "array read failed with %d", rc);

	if (memcmp(buf, rbuf, BUFLEN) != 0)
		ASSERT(0, "Data verification");

	daos_array_close(oh, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	if (rank == 0)
		printf("SUCCESS\n");
}

static void
list_keys(daos_handle_t oh, int *num_keys)
{
	char		*buf;
	daos_key_desc_t kds[ENUM_DESC_NR];
	daos_anchor_t	anchor = {0};
	int		key_nr = 0;
	d_sg_list_t	sgl;
	d_iov_t		sg_iov;

	buf = malloc(ENUM_DESC_BUF);
	d_iov_set(&sg_iov, buf, ENUM_DESC_BUF);
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t	nr = ENUM_DESC_NR;
		int		rc;

		memset(buf, 0, ENUM_DESC_BUF);
		rc = daos_kv_list(oh, DAOS_TX_NONE, &nr, kds, &sgl, &anchor,
				  NULL);
		ASSERT(rc == 0, "KV list failed with %d", rc);

		if (nr == 0)
			continue;
		key_nr += nr;
	}
	*num_keys = key_nr;
}

void
example_daos_kv()
{
	daos_handle_t	oh;
	char		buf[BUFLEN], rbuf[BUFLEN];
	daos_obj_id_t	oid;
	char		key[10];
	int		i, rc;

	MPI_Barrier(MPI_COMM_WORLD);
	if (rank == 0)
		printf("Example of DAOS High level KV type:\n");

	/*
	 * This is an example if the high level KV API which abstracts out the
	 * 2-level keys and exposes a single Key and atomic single value to
	 * represent a more traditional KV API. In this example we insert 10
	 * keys each with value BUFLEN (note that the value under each key need
	 * not to be of the same size.
	 */

	oid.hi = 0;
	oid.lo = 4;
	/** the KV API requires the flat feature flag be set in the oid */
	daos_obj_generate_id(&oid, DAOS_OF_KV_FLAT, OC_SX, 0);

	rc = daos_kv_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	ASSERT(rc == 0, "KV open failed with %d", rc);

	dts_buf_render(buf, BUFLEN);

	/** each rank puts 10 keys */
	for (i = 0; i < KEYS; i++) {
		sprintf(key, "key_%d_%d", i, rank);
		rc = daos_kv_put(oh, DAOS_TX_NONE, 0, key, BUFLEN, buf, NULL);
		ASSERT(rc == 0, "KV put failed with %d", rc);
	}

	/** each rank gets 10 keys */
	for (i = 0; i < KEYS; i++) {
		daos_size_t size;

		sprintf(key, "key_%d_%d", i, rank);

		/** first query the size */
		rc = daos_kv_get(oh, DAOS_TX_NONE, 0, key, &size, NULL, NULL);
		ASSERT(rc == 0, "KV get failed with %d", rc);
		ASSERT(size == BUFLEN, "Invalid read size");

		/** get the data */
		rc = daos_kv_get(oh, DAOS_TX_NONE, 0, key, &size, rbuf, NULL);
		ASSERT(rc == 0, "KV get failed with %d", rc);
		ASSERT(size == BUFLEN, "Invalid read size");

		if (memcmp(buf, rbuf, BUFLEN) != 0)
			ASSERT(0, "Data verification");
		memset(rbuf, 0, BUFLEN);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	int num_keys = 0;

	/** enumerate all keys */
	list_keys(oh, &num_keys);
	ASSERT(num_keys == KEYS * rankn, "KV enumerate failed");

	MPI_Barrier(MPI_COMM_WORLD);
	/** each rank removes a key */
	sprintf(key, "key_%d_%d", 1, rank);
	rc = daos_kv_remove(oh, DAOS_TX_NONE, 0, key, NULL);
	ASSERT(rc == 0, "KV remove failed with %d", rc);
	MPI_Barrier(MPI_COMM_WORLD);

	/** enumerate all keys */
	list_keys(oh, &num_keys);
	ASSERT(num_keys == (KEYS - 1) * rankn,
	       "KV enumerate after remove failed");

	daos_kv_close(oh, NULL);

	MPI_Barrier(MPI_COMM_WORLD);
	if (rank == 0)
		printf("SUCCESS\n");
}

int
main(int argc, char **argv)
{
	uuid_t		pool_uuid, co_uuid;
	d_rank_list_t	*svcl = NULL;
	int		rc;

	rc = MPI_Init(&argc, &argv);
	ASSERT(rc == MPI_SUCCESS, "MPI_Init failed with %d", rc);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &rankn);

	rc = gethostname(node, sizeof(node));
	ASSERT(rc == 0, "buffer for hostname too small");

	if (argc != 3) {
		fprintf(stderr, "args: pool svcl\n");
		exit(1);
	}

	/** initialize the local DAOS stack */
	rc = daos_init();
	ASSERT(rc == 0, "daos_init failed with %d", rc);

	/** parse the pool information and connect to the pool */
	rc = uuid_parse(argv[1], pool_uuid);
	ASSERT(rc == 0, "Failed to parse 'Pool uuid': %s", argv[1]);

	svcl = daos_rank_list_parse(argv[2], ":");
	if (svcl == NULL)
		ASSERT(svcl != NULL, "Failed to allocate svcl");

	/** Call connect on rank 0 only and broadcast handle to others */
	if (rank == 0) {
		rc = daos_pool_connect(pool_uuid, NULL, svcl, DAOS_PC_RW, &poh,
				       NULL, NULL);
		ASSERT(rc == 0, "pool connect failed with %d", rc);
	}
	/** share pool handle with peer tasks */
	handle_share(&poh, HANDLE_POOL);

	/*
	 * Create and open container on rank 0 and share the handle.
	 *
	 * Alternatively, one could create the container outside of this program
	 * using the daos utility: daos cont create --pool=puuid --svc=svclist
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

	/** Example of DAOS key_Array object */
	example_daos_key_array();

	/** Example of DAOS key_SV object */
	example_daos_key_sv();

	/** Example of DAOS Array object */
	example_daos_array();

	/** Example of DAOS KV object */
	example_daos_kv();

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
