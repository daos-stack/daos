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
/*
 * This is an MPI-based DAOS benchmarking tool.
 */
#include <time.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <libgen.h>
#include <assert.h>
#include <mpi.h>

#include <daos_types.h>
#include <daos/list.h>
#include <daos_sr.h>
#include <errno.h>

#define UPDATE_CSUM_SIZE	32
#define DKEY_SIZE		64
#define	VAL_BUF_SIZE		64
#define DBENCH_TEST_NKEYS	100


/**
 * Chronograph---an array of string-named time records
 */
struct chrono {
	char   *c_keys[16];
	double	c_values[16];
	int	c_n_records;
};

struct test;
typedef void (*cb_run_t)(struct test *);

struct test_type {
	char	       *tt_name;
	cb_run_t	tt_run;
};

uuid_t				pool_uuid;
daos_pool_info_t		pool_info;
daos_handle_t			poh = DAOS_HDL_INVAL;
daos_handle_t			coh = DAOS_HDL_INVAL;
daos_handle_t			oh = DAOS_HDL_INVAL;
uuid_t				co_uuid;
daos_co_info_t			co_info;
daos_obj_id_t			oid;
daos_epoch_t			ghce;
daos_rank_t			svc;
daos_rank_list_t		svcl;
void				*buffers;
static daos_handle_t		eq;
unsigned int			naios;
static daos_event_t		**events;
static daos_oclass_id_t		obj_class = DSR_OC_LARGE_RW;


struct test {
	/* Test type */
	struct test_type	*t_type;
	/* Pool Name */
	char			*t_pname;
	/* Size of dkey */
	uint64_t		t_dkey_size;
	/* Size of value buffer */
	uint64_t		t_val_bufsize;
	/* Number of keys */
	int			t_nkeys;
	/* Number of indexes */
	int			t_nindexes;
	/* Number of concurrent IO Reqs */
	int			t_naios;
	/* Current epoch */
	daos_epoch_t		t_epoch;
};

struct a_ioreq {
	daos_list_t		list;
	daos_event_t		ev;
	daos_dkey_t		dkey;
	daos_iov_t		val_iov;
	daos_vec_iod_t		vio;
	daos_recx_t		rex;
	daos_epoch_range_t	erange;
	daos_sg_list_t		sgl;
	daos_csum_buf_t		csum;
	char			csum_buf[UPDATE_CSUM_SIZE];
	char			dkey_buf[DKEY_SIZE];

};

static	DAOS_LIST_HEAD(aios);

/**
 * Global initializations
 */
static int		comm_world_rank = -1;
static int		comm_world_size = -1;
static struct chrono	chronograph;
static int		verbose;


/**
 * TODO: Move these to DAOS DEBUG macros
 */

#define DBENCH_INFO(format, ...) do {					\
	if (verbose)							\
		printf("daosbench:%d:%s:%d: "format"\n",		\
		       comm_world_rank,	__FILE__, __LINE__,		\
		       ##__VA_ARGS__);					\
} while (0)

#define DBENCH_ERR(rc, format, ...) do {				\
	fprintf(stderr, "daosbench:%d:%s:%d: %s: "format"\n",		\
		comm_world_rank, __FILE__, __LINE__, strerror(rc),	\
		##__VA_ARGS__);						\
	MPI_Abort(MPI_COMM_WORLD, -1);					\
} while (0)

#define DBENCH_CHECK(rc, format, ...) do {				\
	int	_rc = (rc);						\
									\
	if (_rc < 0) {							\
		fprintf(stderr, "daosbench:%d:%s:%d: %s: "format"\n",	\
			comm_world_rank, __FILE__, __LINE__,		\
			strerror(-_rc), ##__VA_ARGS__);			\
		MPI_Abort(MPI_COMM_WORLD, -1);				\
	}								\
} while (0)

enum {
	HANDLE_POOL,
	HANDLE_CO
};

static void
ioreq_init(struct a_ioreq *ioreq, struct test *test, int counter,
	   int l_naios)
{
	int rc;

	if (l_naios > 0) {
		rc = posix_memalign((void **) &buffers, sysconf(_SC_PAGESIZE),
				    test->t_val_bufsize * l_naios);
		DBENCH_CHECK(rc, "Failed to allocate buffer array");
	}

	ioreq->dkey.iov_buf = ioreq->dkey_buf;
	ioreq->dkey.iov_buf_len = DKEY_SIZE;

	ioreq->csum.cs_csum = &ioreq->csum_buf;
	ioreq->csum.cs_buf_len = UPDATE_CSUM_SIZE;
	ioreq->csum.cs_len = UPDATE_CSUM_SIZE;

	ioreq->rex.rx_nr = 1;
	ioreq->rex.rx_idx = 0;

	ioreq->erange.epr_lo = 0;
	ioreq->erange.epr_hi = DAOS_EPOCH_MAX;

	ioreq->vio.vd_name.iov_buf = "data";
	ioreq->vio.vd_name.iov_buf_len =
	strlen(ioreq->vio.vd_name.iov_buf) + 1;

	ioreq->vio.vd_kcsum.cs_csum = NULL;
	ioreq->vio.vd_kcsum.cs_buf_len = 0;
	ioreq->vio.vd_kcsum.cs_len = 0;

	ioreq->vio.vd_nr = 1;
	ioreq->vio.vd_recxs = &ioreq->rex;
	ioreq->vio.vd_csums = &ioreq->csum;
	ioreq->vio.vd_eprs  = &ioreq->erange;

	ioreq->val_iov.iov_buf = buffers + test->t_val_bufsize * counter;
	ioreq->val_iov.iov_buf_len = test->t_val_bufsize;
	ioreq->val_iov.iov_len = ioreq->val_iov.iov_buf_len;
	ioreq->sgl.sg_nr.num = 1;
	ioreq->sgl.sg_iovs = &ioreq->val_iov;

	rc = daos_event_init(&ioreq->ev, eq, NULL);
	DBENCH_CHECK(rc, "Failed to initialize event for aio[%d]", counter);

}

static void
ioreq_fini()
{
	free(buffers);
}

static void
aio_req_init(struct test *test)
{
	struct a_ioreq	*ioreq;
	int		i;

	for (i = 0; i < test->t_naios; i++) {
		ioreq = malloc(sizeof(*ioreq));
		if (ioreq == NULL)
			DBENCH_ERR(ENOMEM,
				   "Failed to allocate ioreq array");

		memset(ioreq, 0, sizeof(*ioreq));
		ioreq_init(ioreq, test, i, test->t_naios);
		daos_list_add(&ioreq->list, &aios);

		DBENCH_INFO("Allocated AIO %p: buffer %p", ioreq,
			    ioreq->val_iov.iov_buf);
	}

	naios = test->t_naios;
	events = malloc((sizeof(*events) * test->t_naios));
	DBENCH_CHECK(events == NULL, "Failed in allocating events array\n");
}

static void
aio_req_fini(struct test *test)
{
	struct a_ioreq *ioreq;
	struct a_ioreq *tmp;

	free(events);

	daos_list_for_each_entry_safe(ioreq, tmp, &aios, list) {
		DBENCH_INFO("Freeing AIO %p: buffer %p", ioreq,
			     ioreq->val_iov.iov_buf);

		daos_list_del_init(&ioreq->list);
		daos_event_fini(&ioreq->ev);
		free(ioreq);
	}
	ioreq_fini();
}

static void
aio_req_wait(struct test *test)
{
	struct a_ioreq		*ioreq;
	int			i;
	int			rc;

	rc = daos_eq_poll(eq, 0, DAOS_EQ_WAIT, test->t_naios,
			  events);
	DBENCH_CHECK(rc, "Failed to poll event queue");
	assert(rc <= test->t_naios - naios);

	for (i = 0; i < rc; i++) {
		ioreq = (struct a_ioreq *)
		      ((char *) events[i] -
		       (char *) (&((struct a_ioreq *) 0)->ev));

		DBENCH_CHECK(ioreq->ev.ev_error,
			     "Failed to transfer (%lu, %lu)",
			     ioreq->vio.vd_recxs->rx_idx,
			     ioreq->vio.vd_recxs->rx_nr);

		daos_list_move(&ioreq->list, &aios);
		naios++;
		DBENCH_INFO("Completed AIO %p: buffer %p",
			    ioreq, ioreq->val_iov.iov_buf);
	}

	DBENCH_INFO("Found %d completed AIOs (%d free %d busy)",
		    rc, naios, test->t_naios - naios);
}

void
handle_share(daos_handle_t *hdl, int rank,
	     int type, daos_handle_t handle)
{
	daos_iov_t	ghdl = { NULL, 0, 0 };
	int		rc = 0;

	if (type != HANDLE_POOL || type != HANDLE_CO)
		rc = EINVAL;
	DBENCH_CHECK(rc, "Unkown handle type\n");

	if (rank == 0) {
		if (type == HANDLE_POOL)
			dsr_pool_local2global(*hdl, &ghdl);
		else
			dsr_co_local2global(*hdl, &ghdl);
	}

	/** broadcast size of global handle to all peers */
	rc = MPI_Bcast(&ghdl.iov_buf_len, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
	DBENCH_CHECK(rc,
		     "Global hdl sz broadcast failed with %d", rc);

	/** allocate buffer for global pool handle */
	ghdl.iov_buf = malloc(ghdl.iov_buf_len);
	ghdl.iov_len = ghdl.iov_buf_len;

	if (rank == 0) {
		/** generate actual global handle to share with peer tasks */
		if (type == HANDLE_POOL)
			rc = dsr_pool_local2global(*hdl, &ghdl);
		else
			rc = dsr_co_local2global(*hdl, &ghdl);
		DBENCH_CHECK(rc, "local2global failed with %d", rc);
	}

	/** broadcast global handle to all peers */
	rc = MPI_Bcast(ghdl.iov_buf, ghdl.iov_len, MPI_BYTE, 0,
		       MPI_COMM_WORLD);
	DBENCH_CHECK(rc,
		     "Global hdl broadcast failed with %d", rc);

	if (rank != 0) { /** unpack global handle */
		if (type == HANDLE_POOL)
			rc = dsr_pool_global2local(ghdl, hdl);
		else
			rc = dsr_co_global2local(poh, ghdl, hdl);

		DBENCH_CHECK(rc, "global2local failed with %d", rc);
	}

	free(ghdl.iov_buf);
}

static void
chrono_record(char *key)
{
	struct chrono	*chrono = &chronograph;

	chrono->c_keys[chrono->c_n_records] = key;
	chrono->c_values[chrono->c_n_records] = MPI_Wtime();
	chrono->c_n_records++;
}

static double
chrono_read(char *key)
{
	struct chrono	*chrono = &chronograph;
	int		i;

	for (i = 0; i < chrono->c_n_records; i++)
		if (strcmp(chrono->c_keys[i], key) == 0)
			return chrono->c_values[i];
	DBENCH_ERR(ENOENT, "Failed to find '%s' time", key);
	return 0;
}


static void
object_open(int rank, daos_epoch_t epoch, daos_handle_t *object)
{
	unsigned int	flags;
	int		rc;
	int		rand_obj_id;

	if (rank == 0)
		rand_obj_id = rand();

	rc = MPI_Bcast(&rand_obj_id, 1, MPI_INT, 0, MPI_COMM_WORLD);
	DBENCH_CHECK(rc, "Error in broadcasting random object id");

	oid.hi = rand_obj_id + 2;
	oid.mid = rand_obj_id + 1;
	oid.lo = rand_obj_id;
	dsr_obj_id_generate(&oid, obj_class);

	if (rank == 0) {
		rc = dsr_obj_declare(coh, oid, epoch, NULL,
				     NULL);
		DBENCH_CHECK(rc, "Failed to declare object");
	}

	MPI_Barrier(MPI_COMM_WORLD);

	flags = DAOS_OO_RW;
	rc = dsr_obj_open(coh, oid, epoch, flags, object, NULL);
	DBENCH_CHECK(rc, "Failed to open object");
}

static void
object_close(daos_handle_t object)
{
	int rc;

	rc = dsr_obj_close(object, NULL /* ev */);
	DBENCH_CHECK(rc, "Failed to close object");
}

static void
insert(const char *dkey, const char *akey, uint64_t idx,
	void *val, daos_size_t size, daos_epoch_t epoch,
	struct a_ioreq *req)
{

	int rc;

	/** dkey */
	daos_iov_set(&req->dkey, (void *)dkey, strlen(dkey));

	/** akey */
	daos_iov_set(&req->vio.vd_name, (void *)akey, strlen(akey));

	/** val */
	daos_iov_set(&req->val_iov, val, size);

	/** record extent */
	req->rex.rx_rsize = size;
	req->rex.rx_idx = idx;

	/** XXX: to be fixed */
	req->erange.epr_lo = epoch;

	/** execute update operation */
	rc = dsr_obj_update(oh, epoch, &req->dkey, 1, &req->vio,
			    &req->sgl, &req->ev);
	DBENCH_CHECK(rc, "dsr fetch failed\n");
}

static void
enumerate(daos_epoch_t epoch, uint32_t *number, daos_key_desc_t *kds,
	  daos_hash_out_t *anchor, char *buf, int len, struct a_ioreq *req)
{
	int		rc;
	daos_event_t	*evp;

	daos_iov_set(&req->val_iov, buf, len);

	/** execute fetch operation */
	rc = dsr_obj_list_dkey(oh, epoch, number, kds,
			       &req->sgl, anchor,
			       &req->ev);
	DBENCH_CHECK(rc, "Failed to list dkey\n");
	rc = daos_eq_poll(eq, 1, DAOS_EQ_WAIT, 1, &evp);
	DBENCH_CHECK(rc, "Failed to poll event queue");
	assert(rc == 1);
}

static void
lookup(const char *dkey, const char *akey, uint64_t idx, void *val,
	daos_size_t size, daos_epoch_t epoch, struct a_ioreq *req,
	struct test *test, int verify)
{
	int rc;

	/** dkey */
	daos_iov_set(&req->dkey, (void *)dkey, strlen(dkey));

	/** akey */
	daos_iov_set(&req->vio.vd_name, (void *)akey, strlen(akey));

	/** val */
	daos_iov_set(&req->val_iov, val, size);

	/** record extent */
	req->rex.rx_rsize = size;
	req->rex.rx_idx = idx;

	/** XXX: to be fixed */
	req->erange.epr_lo = epoch;

	if (!verify)
		DBENCH_INFO("Starting lookup %p (%d free %d busy): "
			    "dkey '%s' iod <%llu, %llu> sgl <%p, %llu>", req,
			    naios, test->t_naios - naios,
			    (char *)(req->dkey.iov_buf),
			    (unsigned long long) req->vio.vd_recxs->rx_idx,
			    (unsigned long long) req->vio.vd_recxs->rx_nr,
			    req->sgl.sg_iovs->iov_buf,
			    (unsigned long long)
			    req->sgl.sg_iovs->iov_buf_len);

	/** execute fetch operation */
	rc = dsr_obj_fetch(oh, epoch, &req->dkey, 1, &req->vio, &req->sgl,
			   NULL, verify ? NULL : &req->ev);
	DBENCH_CHECK(rc, "dsr fetch failed\n");
}

static void
daos_init(void)
{
	int	rc;

	rc = dsr_init();
	DBENCH_CHECK(rc, "Failed to initialize DAOS-SR\n");
}

static void
daos_fini(void)
{
	int	rc;

	rc = dsr_fini();
	DBENCH_CHECK(rc, "dsr_fini failed with\n");
}

static void
pool_disconnect(int rank)
{
	int	rc = 0;

	rc = dsr_pool_disconnect(poh, NULL);
	DBENCH_CHECK(rc, "pool disconnect failed\n");
}

static void
container_create(int rank)
{
	if (!rank) {
		int	rc;

		uuid_generate(co_uuid);
		rc = dsr_co_create(poh, co_uuid, NULL);
		DBENCH_CHECK(rc, "Container create failed\n");

		rc = dsr_co_open(poh, co_uuid, DAOS_COO_RW, NULL,
				 &coh, &co_info, NULL);
		DBENCH_CHECK(rc, "Container open failed\n");
	}
}

static void
container_close(int rank)
{
	int	rc;

	if (rank != 0) {
		rc  = dsr_co_close(coh, NULL);
		DBENCH_CHECK(rc, "Failed to close container\n");
	}

	rc = MPI_Barrier(MPI_COMM_WORLD);
	DBENCH_CHECK(rc, "Failed at barrier in container close\n");

	if (!rank) {
		rc = dsr_co_close(coh, NULL);
		DBENCH_CHECK(rc, "Failed to close container\n");
	}

}

static void
container_destroy(int rank)
{
	container_close(rank);

	if (!rank) {
		int	rc;

		rc = dsr_co_destroy(poh, co_uuid, 1, NULL);
		DBENCH_CHECK(rc, "Container destroy failed\n");
	}
}



static void
kv_test_report(struct test *test, int idx_flag)
{
	if (!comm_world_rank) {
		double d = chrono_read("end") - chrono_read("begin");
		uint32_t count = idx_flag ? test->t_nindexes : test->t_nkeys;

		printf("%s\n", test->t_type->tt_name);
		printf("Time: %f seconds (%f ops per second)\n",
		       d, (count * comm_world_size)/d);
	}
}


static void
kv_test_describe(struct test *test, int idx_flag)
{

	if (!idx_flag)
		test->t_nindexes = 1;
	else
		test->t_nkeys = 1;

	if (comm_world_rank == 0) {
		printf("===============================\n");
		printf("Test Setup\n");
		printf("---------------\n");
		printf("Test: %s", test->t_type->tt_name);
		printf("\n");
		printf("DAOS pool :%s", test->t_pname);
		printf("\n");
		printf("Value buffer size: %"PRIu64,
		       test->t_val_bufsize);
		printf("\n");
		printf("Number of processes: %d",
		       comm_world_size);
		printf("\n");
		if (!idx_flag)
			printf("Number of keys/process: %d",
			       test->t_nkeys);
		else
			printf("Number of indexes/process: %d",
			       test->t_nindexes);
		printf("\n");
		printf("Number of asynchronous I/O: %d",
		       test->t_naios);
		printf("\n");
		printf("===============================\n");
	}
}

static struct a_ioreq*
get_next_ioreq(struct test *test)
{
	struct a_ioreq	*ioreq;

	while (naios == 0)
		aio_req_wait(test);
	ioreq = daos_list_entry(aios.next, struct a_ioreq, list);
	daos_list_move_tail(&ioreq->list, &aios);
	naios--;

	return ioreq;
}

static void
kv_update_async(struct test *test, int idx_flag)
{
	int		rc = 0, i;
	struct a_ioreq	*ioreq;
	int		counter;
	char		**key = NULL;

	counter = idx_flag ? test->t_nindexes : test->t_nkeys;

	if (!idx_flag) {
		key = (char **)malloc(counter * sizeof(char *));
		if (key == NULL)
			DBENCH_ERR(ENOMEM, "Error in allocating Key\n");
		for (i = 0; i < counter; i++) {
			key[i] = malloc(DKEY_SIZE);
			if (key[i] == NULL)
				DBENCH_ERR(ENOMEM,
					   "Error in allocating key[%d]\n",
					   i);
		}
	}

	ghce = co_info.ci_epoch_state.es_glb_hce;
	DBENCH_INFO("ghce: %"PRIu64, ghce);

	ghce++;
	test->t_epoch = ghce;

	if (comm_world_rank == 0) {
		rc = dsr_epoch_hold(coh, &test->t_epoch, NULL, NULL);
		DBENCH_CHECK(rc, "Failed to hold epoch\n");
	}

	object_open(comm_world_rank, test->t_epoch, &oh);
	aio_req_init(test);

	for (i = 0; i < counter; i++) {
		ioreq = get_next_ioreq(test);
		if (!idx_flag) {
			memset(key[i], 0, DKEY_SIZE);
			snprintf(key[i], DKEY_SIZE, "%d",
				(comm_world_rank*test->t_nkeys) + i);

			DBENCH_INFO("%d: Key for insert: %s",
				    comm_world_rank, key[i]);
		}

		/* FIXME: memset() can only take one byte. */
		memset(ioreq->val_iov.iov_buf,
		       ((comm_world_rank*counter) + i),
		       test->t_val_bufsize);

		insert(idx_flag ? "var_dkey_d" : key[i],
		       "var_dkey_a",
		       idx_flag ?
		       ((comm_world_rank * test->t_nindexes) + i) : 0,
		       ioreq->val_iov.iov_buf,
		       test->t_val_bufsize, test->t_epoch, ioreq);
	}

	while (test->t_naios - naios > 0)
		aio_req_wait(test);
	aio_req_fini(test);

	if (!idx_flag) {
		for (i = 0; i < counter; i++)
			free(key[i]);
		free(key);
	}
}

static void
kv_update_verify(struct test *test, int idx_flag)
{
	char		*valbuf = NULL;
	char		*lookup_buf = NULL;
	struct a_ioreq	ioreq;
	int		counter, i;

	/**
	 * Verification can happen synchronously!
	 */

	counter = idx_flag ? test->t_nindexes : test->t_nkeys;
	test->t_naios = 1;

	lookup_buf = malloc(test->t_val_bufsize);
	if (lookup_buf == NULL)
		DBENCH_ERR(ENOMEM,
			   "Error in allocating lookup buf\n");

	valbuf = malloc(test->t_val_bufsize);
	if (valbuf == NULL)
		DBENCH_ERR(ENOMEM,
			   "Error in allocating lookup buf\n");

	for (i = 0; i < counter; i++) {
		char	key[DKEY_SIZE];

		ioreq_init(&ioreq, test, i, 0);
		if (!idx_flag) {
			memset(key, 0, DKEY_SIZE);
			snprintf(key, DKEY_SIZE, "%d",
				(comm_world_rank * counter) + i);

			DBENCH_INFO("%d: Key for insert: %s",
				    comm_world_rank, key);
		}

		memset(lookup_buf, 0, test->t_val_bufsize);
		memset(valbuf, ((comm_world_rank * counter) + i),
		       test->t_val_bufsize);

		lookup(idx_flag ? "var_dkey_d" : key,
		       "var_dkey_a",
		       idx_flag ?
		       ((comm_world_rank * test->t_nindexes) + i) : 0,
		       lookup_buf,
		       test->t_val_bufsize, test->t_epoch,
		       &ioreq, test, 1);
		DBENCH_INFO("lookup_buf: %s\n valbuf: %s",
			    (char *)(lookup_buf), (char *)valbuf);

		if (memcmp(lookup_buf, valbuf, test->t_val_bufsize))
			DBENCH_ERR(EIO, "Lookup buffers differ for key :%d",
				   i);
	}
	DBENCH_INFO("Verification complete!\n");

	free(lookup_buf);
	free(valbuf);

}

static void
kv_flush_and_commit(struct test *test)
{
	int	rc = 0;

	if (comm_world_rank == 0) {
		DBENCH_INFO("Flushing Epoch %lu", test->t_epoch);

		rc = dsr_epoch_flush(coh, test->t_epoch, NULL, NULL);
		DBENCH_CHECK(rc, "Failed to flush epoch");

		DBENCH_INFO("Committing Epoch :%lu", test->t_epoch);
		rc = dsr_epoch_commit(coh, test->t_epoch, NULL, NULL);
		DBENCH_CHECK(rc, "Failed to commit object write\n");
	}

}

static void
kv_multikey_update_run(struct test *test)
{

	kv_test_describe(test, 0);
	MPI_Barrier(MPI_COMM_WORLD);

	chrono_record("begin");

	kv_update_async(test, 0);
	MPI_Barrier(MPI_COMM_WORLD);

	DBENCH_INFO("completed %d inserts\n", test->t_nkeys);
	kv_flush_and_commit(test);
	chrono_record("end");

	/**
	 * Done with benchmarking
	 * Lets verify the test
	 */
	kv_update_verify(test, 0);
	object_close(oh);

	kv_test_report(test, 0);
}

static void
kv_multikey_fetch_run(struct test *test)
{
	char		*valbuf = NULL;
	char		**lookup_buf = NULL;
	int		i;
	struct a_ioreq	*ioreq;
	char		**key = NULL;

	kv_test_describe(test, 0);

	MPI_Barrier(MPI_COMM_WORLD);
	kv_update_async(test, 0);
	MPI_Barrier(MPI_COMM_WORLD);
	kv_flush_and_commit(test);

	/**
	 * We need this buffer to collect all async
	 * lookup results for verification
	 */
	lookup_buf = (char **)malloc(test->t_nkeys * sizeof(char *));
	if (lookup_buf  == NULL)
		DBENCH_ERR(ENOMEM, "Error in allocating lookup_buf");

	key = (char **)malloc(test->t_nkeys * sizeof(char *));
	if (key == NULL)
		DBENCH_ERR(ENOMEM, "Error in allocating key array");

	for (i = 0; i < test->t_nkeys; i++) {
		lookup_buf[i] = malloc(test->t_val_bufsize);
		if (lookup_buf[i] == NULL)
			DBENCH_ERR(ENOMEM, "Error in allocating lookup_buf[%d]",
				   i);
		key[i] = malloc(test->t_val_bufsize);
		if (key[i] == NULL)
			DBENCH_ERR(ENOMEM, "Error in allocating key[%d]",
				   i);
	}

	MPI_Barrier(MPI_COMM_WORLD);

	chrono_record("begin");
	aio_req_init(test);

	for (i = 0; i < test->t_nkeys; i++) {
		ioreq = get_next_ioreq(test);
		memset(key[i], 0, DKEY_SIZE);
		snprintf(key[i], DKEY_SIZE, "%d",
			(comm_world_rank * test->t_nkeys) + i);
		memset(lookup_buf[i], 0, test->t_val_bufsize);
		lookup(key[i], "var_dkey_a", 0, lookup_buf[i],
		       test->t_val_bufsize, test->t_epoch, ioreq, test, 0);
	}

	while (test->t_naios - naios > 0)
		aio_req_wait(test);
	aio_req_fini(test);

	chrono_record("end");
	MPI_Barrier(MPI_COMM_WORLD);
	/**
	 * Done with benchmarking
	 * Lets verify the test
	 */

	valbuf = malloc(test->t_val_bufsize);
	if (valbuf == NULL)
		DBENCH_ERR(ENOMEM,
			   "Error in allocating lookup buf\n");

	for (i = 0; i < test->t_nkeys; i++) {
		memset(valbuf, ((comm_world_rank*test->t_nkeys) + i),
		       test->t_val_bufsize);
		if (memcmp(lookup_buf[i], valbuf, test->t_val_bufsize))
			DBENCH_ERR(EIO, "Lookup buffers differ for key :%d",
				   i);
		free(lookup_buf[i]);
		free(key[i]);
	}

	DBENCH_INFO("Verification complete!\n");

	object_close(oh);
	free(lookup_buf);
	free(key);
	free(valbuf);

	kv_test_report(test, 0);
}

static void
kv_enumerate_test(struct test *test)
{
	uint32_t		number = 5;
	daos_hash_out_t		hash_out;
	daos_key_desc_t		kds[5];
	char			*buf, *ptr;
	int			total_keys = 0;
	int			i;
	struct a_ioreq		e_ioreq;
	int			done = 0;
	int			key_start, key_end;


	kv_test_describe(test, 0);

	key_start = (comm_world_rank * test->t_nkeys);
	key_end = (comm_world_rank * test->t_nkeys) + test->t_nkeys;
	DBENCH_INFO("Key Range %d -> %d", key_start, key_end);

	MPI_Barrier(MPI_COMM_WORLD);
	kv_update_async(test, 0);
	MPI_Barrier(MPI_COMM_WORLD);

	kv_flush_and_commit(test);
	MPI_Barrier(MPI_COMM_WORLD);

	chrono_record("begin");

	/* All updates completed. Starting to enumerate */
	memset(&hash_out, 0, sizeof(hash_out));
	buf = calloc(5 * DKEY_SIZE, 1);
	ioreq_init(&e_ioreq, test, 1, 1);

	/** enumerate records */
	while (!daos_hash_is_eof(&hash_out)) {

		enumerate(test->t_epoch,
			  &number, kds, &hash_out, buf, 5*DKEY_SIZE,
			  &e_ioreq);
		if (number == 0)
			goto next;

		ptr = buf;
		total_keys += number;
		for (i = 0; i < number; i++) {
			char key[DKEY_SIZE];

			snprintf(key, kds[i].kd_key_len + 1, ptr);
			DBENCH_INFO("i %d key %s len %d", i, key,
				    (int)kds[i].kd_key_len);
			if (atoi(key) >= key_start &&
			    atoi(key) < key_end)
				done++;
			else
				DBENCH_INFO("out of range? Test will fail!");
			ptr += kds[i].kd_key_len;
		}
next:
		if (daos_hash_is_eof(&hash_out))
			break;
		memset(buf, 0, 5 * DKEY_SIZE);
		number = 5;
	}
	ioreq_fini();
	object_close(oh);

	chrono_record("end");
	DBENCH_INFO("Verifying the test");
	DBENCH_INFO("total_keys: %d, Done : %d, nkeys: %d", total_keys,
		    done, test->t_nkeys);
	assert(done == test->t_nkeys);
	DBENCH_INFO("Test Complete");


	/* Cleanup */
	free(buf);

	kv_test_report(test, 0);
}

static void
kv_multi_idx_update_run(struct test *test)
{

	kv_test_describe(test, 1);

	MPI_Barrier(MPI_COMM_WORLD);
	chrono_record("begin");

	kv_update_async(test, 1);
	DBENCH_INFO("completed %d inserts\n", test->t_nindexes);

	kv_flush_and_commit(test);
	chrono_record("end");

	MPI_Barrier(MPI_COMM_WORLD);
	kv_update_verify(test, 1);
	object_close(oh);

	kv_test_report(test, 1);
}

static struct test_type	test_type_mkvar = {
	"kv-multi-key",
	kv_multikey_update_run
};

static struct test_type	test_type_mivar = {
	"kv-multi-idx",
	kv_multi_idx_update_run
};

static struct test_type test_type_fetch = {
	"kv-fetch-test",
	kv_multikey_fetch_run
};


static struct test_type test_type_enum = {
	"kv-enum-test",
	kv_enumerate_test
};

static struct test_type	*test_types_available[] = {
	&test_type_mkvar,
	&test_type_mivar,
	&test_type_enum,
	&test_type_fetch,
	NULL
};


static struct test_type *
test_type_search(char *name)
{
	struct test_type  **test = test_types_available;

	while (*test != NULL && strcmp((*test)->tt_name, name) != 0)
		test++;
	return *test;
}

static void
usage(void)
{
	printf("\
Usage: daosbench --test=TEST [OPTIONS]\n\
	Options:\n\
	--test=TEST		Run TEST.\n\
	--aios=N		Submit N in-flight I/O requests.\n\
	--dpool=pool		DAOS pool through dmg tool.\n\
	--keys=N		Number of keys to be created in the test. \n\
	--nidxs=N		Number of key indexes.\n\
	--value_buf_size=N	value buffere size for this test\n\
	--verbose		verbose flag. \n\
	--help			Print this message and exit.\n\
	Tests:\n\
		kv-multi-idx	Each mpi rank makes 'n' idx updates\n\
		kv-multi-key	Each mpi rank makes 'n' key updates\n\
		kv-enum-test    Each mpi rank enumerates 'n' keys\n");
}

static int
test_init(struct test *test, int argc, char *argv[])
{
	struct option	options[] = {
		{"aios",		1,	NULL,	'a'},
		{"help",		0,	NULL,	'h'},
		{"keys",		1,	NULL,	'k'},
		{"indexes",		1,	NULL,	'i'},
		{"value_buf_size",	1,	NULL,	'b'},
		{"verbose",		0,	NULL,	'v'},
		{"test",		1,	NULL,	't'},
		{"dpool",		1,	NULL,	'p'},
		{NULL,			0,	NULL,	0}
	};
	int	rc;
	int	first = 1;

	/**
	 * Initializing and setting some default
	 * values
	 */
	memset(test, 0, sizeof(struct test));
	test->t_nkeys = DBENCH_TEST_NKEYS;
	test->t_dkey_size = DKEY_SIZE;
	test->t_val_bufsize = VAL_BUF_SIZE;
	test->t_naios = 16;
	test->t_nindexes = 1;
	test->t_pname = NULL;

	if (comm_world_rank != 0)
		opterr = 0;

	while ((rc = getopt_long(argc, argv, "a:k:i:b:t:p:hv",
				 options, NULL)) != -1) {
		switch (rc) {
		case 'a':
			test->t_naios = atoi(optarg);
			break;
		case 'k':
			test->t_nkeys = atoi(optarg);
			break;
		case 'i':
			test->t_nindexes = atoi(optarg);
			break;
		case 'b':
			test->t_val_bufsize = atoi(optarg);
			break;
		case 'p':
			test->t_pname = strdup(optarg);
			break;
		case 'h':
			if (comm_world_rank == 0)
				usage();
			return 1;
		case 't':
			if (!first) {
				/*
				 * This allows per-test-type test
				 * initialization, which is probably needed by
				 * future test types.
				 */
				if (comm_world_rank == 0)
					fprintf(stderr,
						"Use <exec> '--test' first\n");
				return 2;
			}
			test->t_type = test_type_search(optarg);
			if (test->t_type == NULL) {
				if (comm_world_rank == 0)
					fprintf(stderr,
						"DB: '%s':unkown test\n",
						optarg);
				return 2;
			}
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			return 2;
		}

		if (first)
			first = 0;
	}

	if (test->t_type == NULL) {
		if (comm_world_rank == 0)
			fprintf(stderr,
				"daosbench: '--test' must be specified\n");
		return 2;
	}

	if (!test->t_pname) {
		if (comm_world_rank == 0) {
			fprintf(stderr,
				"daosbench: --dpool must be specified\n");
			fprintf(stderr,
				"daosbench: Use the dmg too create pool\n");
		}
		return 2;
	}

	if (test->t_naios > 32) {
		if (comm_world_rank == 0)
			fprintf(stderr,
				"daosbench: inflight aios>32 not allowed\n");
	}


	if (comm_world_rank == 0) {
		time_t	t = time(NULL);

		printf("================================\n");
		printf("DAOSBENCH (KV)\nStarted at\n%s", ctime(&t));
		printf("=================================\n");
	}
	srand(time(NULL));

	return 0;
}

static void
test_fini(struct test *test)
{
	if (comm_world_rank == 0) {
		time_t	t = time(NULL);

		printf("\n");
		printf("Ended at %s", ctime(&t));
	}
}

int main(int argc, char *argv[])
{
	struct test	arg;
	int		rc = 0;


	MPI_Init(&argc, &argv);
	/**
	 * TODO: Test init for all processes
	 */

	MPI_Comm_rank(MPI_COMM_WORLD, &comm_world_rank);
	MPI_Comm_size(MPI_COMM_WORLD, &comm_world_size);
	rc = test_init(&arg, argc, argv);
	if (rc != 0) {
		if (rc == 1 /* help */)
			rc = 0;
		goto exit;
	}

	daos_init();

	rc = daos_eq_create(&eq);
	DBENCH_CHECK(rc, "Event queue creation failed\n");

	if (comm_world_rank == 0) {
		daos_rank_t		rank  = 0;

		if (strlen(arg.t_pname) == 0)
			DBENCH_ERR(EINVAL, "'daosPool' must be specified");
		DBENCH_INFO("Connecting to Pool: %s",
			    arg.t_pname);
		rc = uuid_parse(arg.t_pname, pool_uuid);
		DBENCH_CHECK(rc, "Failed to parsr 'daosPool': %s",
			     arg.t_pname);
		svcl.rl_nr.num = 1;
		svcl.rl_nr.num_out = 0;
		svcl.rl_ranks = &rank;

		rc = dsr_pool_connect(pool_uuid, NULL, &svcl,
				      DAOS_PC_RW, NULL, &poh,
				      &pool_info, NULL);
		DBENCH_CHECK(rc, "Pool %s connect failed\n",
			     arg.t_pname);
	}

	handle_share(&poh, comm_world_rank, HANDLE_POOL, poh);
	rc = MPI_Bcast(&pool_info, sizeof(pool_info), MPI_BYTE, 0,
		       MPI_COMM_WORLD);
	DBENCH_CHECK(rc, "broadcast pool_info error\n");

	container_create(comm_world_rank);
	handle_share(&coh, comm_world_rank, HANDLE_CO, poh);

	/** Invoke test **/
	arg.t_type->tt_run(&arg);

	container_destroy(comm_world_rank);
	pool_disconnect(comm_world_rank);
	test_fini(&arg);

	rc = daos_eq_destroy(eq, 0);
	DBENCH_CHECK(rc, "Event queue destroy failed\n");

	daos_fini();

exit:
	/**
	 * TODO: Enable this once PMIx segfault is fixed
	 * Currently its just ugly otherwise.
	 * MPI_Finalize();
	 */
	return rc;
}
