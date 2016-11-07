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

#include <crt_util/list.h>
#include <daos.h>
#include <errno.h>
#include "suite/daos_test.h"

#define UPDATE_CSUM_SIZE	32
#define DKEY_SIZE		64
#define AKEY_SIZE		64
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

/* Per-process global variables */
uuid_t				pool_uuid;
daos_pool_info_t		pool_info;
daos_handle_t			poh = DAOS_HDL_INVAL;
daos_handle_t			coh = DAOS_HDL_INVAL;
daos_handle_t			oh = DAOS_HDL_INVAL;
uuid_t				cont_uuid;
daos_cont_info_t		cont_info;
daos_obj_id_t			oid;
daos_epoch_t			ghce;
crt_rank_t			svc;
crt_rank_list_t		svcl;
void				*buffers;
void				*dkbuf;
void				*akbuf;
static daos_handle_t		eq;
unsigned int			naios;
static daos_event_t		**events;
static daos_oclass_id_t		obj_class = DAOS_OC_LARGE_RW;
bool				t_validate;
bool				t_pretty_print;

struct test {
	/* Test type */
	struct test_type	*t_type;
	/* Pool Name */
	char			*t_pname;
	/* Size of dkey */
	uint64_t		t_dkey_size;
	/* Size of dkey */
	uint64_t		t_akey_size;
	/* Size of value buffer */
	uint64_t		t_val_bufsize;
	/**
	 * Number of keys (a-keys/d-keys)
	 * Test dependent
	 */
	int			t_nkeys;
	/* Number of indexes */
	int			t_nindexes;
	/* Number of concurrent IO Reqs */
	int			t_naios;
	/* Test ID */
	int			t_id;
	/* Current epoch */
	daos_epoch_t		t_epoch;
};

struct a_ioreq {
	crt_list_t		list;
	daos_event_t		ev;
	daos_dkey_t		dkey;
	daos_akey_t		akey;
	crt_iov_t		val_iov;
	daos_vec_iod_t		vio;
	daos_recx_t		rex;
	daos_epoch_range_t	erange;
	crt_sg_list_t		sgl;
	daos_csum_buf_t		csum;
	char			csum_buf[UPDATE_CSUM_SIZE];
	char			*dkey_buf;
	char			*akey_buf;
	/* daosbench specific (aio-retrieval) */
	int			r_index;
};

/** List to limit AIO operations */
static	CRT_LIST_HEAD(aios);

/**
 * Global initializations
 */
static int		comm_world_rank = -1;
static int		comm_world_size = -1;
static struct chrono	chronograph;
static int		verbose;

/* Debug macros print only fron rank 0 */
#define DBENCH_PRINT(format, ...) do {					\
	if (!comm_world_rank && t_pretty_print) {			\
		printf(""format"", ##__VA_ARGS__);			\
		fflush(stdout);						\
	}								\
} while (0)

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

static void
alloc_buffers(struct test *test, int nios)
{
	int rc;

	rc = posix_memalign((void **) &buffers, sysconf(_SC_PAGESIZE),
			    test->t_val_bufsize * nios);
	DBENCH_CHECK(rc, "Failed to allocate buffer array");

	rc = posix_memalign((void **) &dkbuf, sysconf(_SC_PAGESIZE),
			    test->t_dkey_size * nios);
	DBENCH_CHECK(rc, "Failed to allocate dkey_buf array");

	rc = posix_memalign((void **) &akbuf, sysconf(_SC_PAGESIZE),
			    test->t_akey_size * nios);
	DBENCH_CHECK(rc, "Failed to allocated akey_buf array");
}


static void
ioreq_init(struct a_ioreq *ioreq, struct test *test, int counter)
{
	int rc;

	/** dkey */
	ioreq->dkey_buf = dkbuf + test->t_dkey_size * counter;
	crt_iov_set(&ioreq->dkey, ioreq->dkey_buf,
		     test->t_dkey_size);
	/** akey */
	ioreq->akey_buf = akbuf + test->t_akey_size * counter;
	crt_iov_set(&ioreq->vio.vd_name, ioreq->akey_buf,
		     test->t_akey_size);

	ioreq->csum.cs_csum = &ioreq->csum_buf;
	ioreq->csum.cs_buf_len = UPDATE_CSUM_SIZE;
	ioreq->csum.cs_len = UPDATE_CSUM_SIZE;

	ioreq->rex.rx_nr = 1;
	ioreq->rex.rx_idx = 0;

	ioreq->erange.epr_lo = 0;
	ioreq->erange.epr_hi = DAOS_EPOCH_MAX;

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
free_buffers()
{
	free(buffers);
	free(dkbuf);
	free(akbuf);
}

static inline void
kv_set_dkey(struct test *test, struct a_ioreq *ioreq, int key_type,
	    int index)
{
	memset(ioreq->dkey_buf, 0, test->t_dkey_size);
	if (!key_type) {
		/** multiple dkey */
		snprintf(ioreq->dkey_buf,
			 test->t_dkey_size, "%d",
			(comm_world_rank*test->t_nkeys) + index);
	} else {
		snprintf(ioreq->dkey_buf, test->t_dkey_size,
			 "var_key_d%d", comm_world_rank);
	}
	DBENCH_INFO("%d: dKey : %s, len: %"PRIu64"\n",
		    comm_world_rank, ioreq->dkey_buf,
		    ioreq->dkey.iov_len);
}

static inline void
kv_set_akey(struct test *test, struct a_ioreq *ioreq, int key_type,
	    int index)
{
	memset(ioreq->akey_buf, 0, test->t_akey_size);
	if (key_type == 1) {
		/** Multiple akey */
		snprintf(ioreq->akey_buf,
			 test->t_akey_size, "%d",
			 (comm_world_rank * test->t_nkeys) + index);
	} else {
		snprintf(ioreq->akey_buf, test->t_akey_size,
			 "var_key_a%d", comm_world_rank);
	}
	DBENCH_INFO("%d: akey: %s, len: "DF_U64"\n",
		    comm_world_rank, ioreq->akey_buf,
		    ioreq->akey.iov_len);
}

static inline void
kv_set_value(struct test *test, void *buf, int counter, int index)
{
	memset(buf, (((comm_world_rank*counter) + index) % 94) + 33,
	       test->t_val_bufsize);
}

static void
aio_req_init(struct test *test)
{
	struct a_ioreq	*ioreq;
	int		i;

	alloc_buffers(test, test->t_naios);
	for (i = 0; i < test->t_naios; i++) {
		ioreq = malloc(sizeof(*ioreq));
		if (ioreq == NULL)
			DBENCH_ERR(ENOMEM,
				   "Failed to allocate ioreq array");

		memset(ioreq, 0, sizeof(*ioreq));
		ioreq_init(ioreq, test, i);
		crt_list_add(&ioreq->list, &aios);

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

	crt_list_for_each_entry_safe(ioreq, tmp, &aios, list) {
		DBENCH_INFO("Freeing AIO %p: buffer %p", ioreq,
			     ioreq->val_iov.iov_buf);

		crt_list_del_init(&ioreq->list);
		daos_event_fini(&ioreq->ev);
		free(ioreq);
	}
	free_buffers();
}

static void
aio_req_wait(struct test *test, int fetch_flag)
{
	struct a_ioreq		*ioreq;
	int			i;
	int			rc;
	char			*valbuf = NULL;


	rc = daos_eq_poll(eq, 0, DAOS_EQ_WAIT, test->t_naios,
			  events);
	DBENCH_CHECK(rc, "Failed to poll event queue");
	assert(rc <= test->t_naios - naios);

	if (fetch_flag && t_validate) {
		valbuf = malloc(test->t_val_bufsize);
		if (valbuf == NULL)
			DBENCH_ERR(ENOMEM,
				   "Valbuf allocation error\n");
	}

	for (i = 0; i < rc; i++) {
		ioreq = (struct a_ioreq *)
		      ((char *) events[i] -
		       (char *) (&((struct a_ioreq *) 0)->ev));

		DBENCH_CHECK(ioreq->ev.ev_error,
			     "Failed to transfer (%lu, %lu)",
			     ioreq->vio.vd_recxs->rx_idx,
			     ioreq->vio.vd_recxs->rx_nr);

		crt_list_move(&ioreq->list, &aios);
		naios++;
		DBENCH_INFO("Completed AIO %p: buffer %p",
			    ioreq, ioreq->val_iov.iov_buf);

		if (fetch_flag && t_validate) {
			kv_set_value(test, valbuf, test->t_nkeys,
				     ioreq->r_index);
			if (memcmp(ioreq->val_iov.iov_buf, valbuf,
				   test->t_val_bufsize))
				DBENCH_ERR(EIO, "lookup dkey: %s \
					   akey: %s idx :%d", ioreq->dkey_buf,
					   ioreq->akey_buf, ioreq->r_index);
		}
	}
	DBENCH_INFO("Found %d completed AIOs (%d free %d busy)",
		    rc, naios, test->t_naios - naios);
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
object_open(int t_id, daos_epoch_t epoch,
	    int enum_flag, daos_handle_t *object)
{
	unsigned int	flags;
	int		rc;

	if (enum_flag) {
		oid.hi = t_id + comm_world_rank + 2;
		oid.mid = t_id + comm_world_rank + 1;
		oid.lo = t_id + comm_world_rank;
	} else {
		oid.hi = t_id + 2;
		oid.mid = t_id + 1;
		oid.lo = t_id;
	}
	daos_obj_id_generate(&oid, obj_class);

	if (enum_flag) {
		rc = daos_obj_declare(coh, oid, epoch, NULL, NULL);
		DBENCH_CHECK(rc, "Failed to declare object");
	} else {
		if (comm_world_rank == 0) {
			rc = daos_obj_declare(coh, oid, epoch, NULL, NULL);
			DBENCH_CHECK(rc, "Failed to declare object");
		}
	}

	MPI_Barrier(MPI_COMM_WORLD);

	flags = DAOS_OO_RW;
	rc = daos_obj_open(coh, oid, epoch, flags, object, NULL);
	DBENCH_CHECK(rc, "Failed to open object");
}

static void
object_close(daos_handle_t object)
{
	int rc;

	rc = daos_obj_close(object, NULL /* ev */);
	DBENCH_CHECK(rc, "Failed to close object");
}

static void
insert(uint64_t idx, daos_epoch_t epoch, struct a_ioreq *req)
{

	int rc;

	/** record extent */
	req->rex.rx_rsize = req->val_iov.iov_len;
	req->rex.rx_idx = idx;

	req->erange.epr_lo = epoch;

	/** execute update operation */
	rc = daos_obj_update(oh, epoch, &req->dkey, 1, &req->vio,
			     &req->sgl, &req->ev);
	DBENCH_CHECK(rc, "dsr fetch failed\n");
}

static void
enumerate(daos_epoch_t epoch, uint32_t *number, daos_key_desc_t *kds,
	  daos_hash_out_t *anchor, char *buf, int len, struct a_ioreq *req)
{
	int	rc;

	crt_iov_set(&req->val_iov, buf, len);

	/** execute fetch operation */
	rc = daos_obj_list_dkey(oh, epoch, number, kds, &req->sgl, anchor,
				NULL);

	DBENCH_CHECK(rc, "daos_obj_list_dkey failed\n");
}

static void
lookup(uint64_t idx, daos_epoch_t epoch, struct a_ioreq *req,
       struct test *test, int verify)
{
	int rc;

	/** record extent */
	req->rex.rx_rsize = req->val_iov.iov_len;
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
	rc = daos_obj_fetch(oh, epoch, &req->dkey, 1, &req->vio, &req->sgl,
			    NULL, verify ? NULL : &req->ev);
	DBENCH_CHECK(rc, "dsr fetch failed\n");
}

static void
init(void)
{
	int	rc;

	rc = daos_init();
	DBENCH_CHECK(rc, "Failed to initialize DAOS\n");
}

static void
fini(void)
{
	int	rc;

	rc = daos_fini();
	DBENCH_CHECK(rc, "daos_fini failed with\n");
}

static void
pool_disconnect(int rank)
{
	int	rc = 0;

	rc = daos_pool_disconnect(poh, NULL);
	DBENCH_CHECK(rc, "pool disconnect failed\n");
}

static void
container_create(int rank)
{
	if (!rank) {
		int	rc;

		uuid_generate(cont_uuid);
		rc = daos_cont_create(poh, cont_uuid, NULL);
		DBENCH_CHECK(rc, "Container create failed\n");

		rc = daos_cont_open(poh, cont_uuid, DAOS_COO_RW, &coh,
				    &cont_info, NULL);
		DBENCH_CHECK(rc, "Container open failed\n");
	}
}

static void
container_close(int rank)
{
	int	rc;

	if (rank != 0) {
		rc  = daos_cont_close(coh, NULL);
		DBENCH_CHECK(rc, "Failed to close container\n");
	}

	rc = MPI_Barrier(MPI_COMM_WORLD);
	DBENCH_CHECK(rc, "Failed at barrier in container close\n");

	if (!rank) {
		rc = daos_cont_close(coh, NULL);
		DBENCH_CHECK(rc, "Failed to close container\n");
	}

}

static void
container_destroy(int rank)
{
	container_close(rank);

	if (!rank) {
		int	rc;

		rc = daos_cont_destroy(poh, cont_uuid, 1, NULL);
		DBENCH_CHECK(rc, "Container destroy failed\n");
	}
}



static void
kv_test_report(struct test *test, int key_type)
{
	if (!comm_world_rank) {
		double d = chrono_read("end") - chrono_read("begin");
		uint32_t count = (key_type == 2) ? test->t_nindexes :
				test->t_nkeys;

		printf("%s\n", test->t_type->tt_name);
		printf("Time: %f seconds (%f ops per second)\n",
		       d, (count * comm_world_size)/d);
	}
}


static void
kv_test_describe(struct test *test, int key_type)
{

	if (key_type != 2)
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
		if (key_type != 2)
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
		aio_req_wait(test, 0);
	ioreq = crt_list_entry(aios.next, struct a_ioreq, list);
	crt_list_move_tail(&ioreq->list, &aios);
	naios--;

	return ioreq;
}

static void
kv_update_async(struct test *test, int key_type,
		int enum_flag)
{
	int		rc = 0, i;
	struct a_ioreq	*ioreq;
	int		counter;

	counter = (key_type == 2) ? test->t_nindexes : test->t_nkeys;
	ghce = cont_info.ci_epoch_state.es_ghce;
	DBENCH_INFO("ghce: %"PRIu64, ghce);

	ghce++;
	test->t_epoch = ghce;

	if (comm_world_rank == 0) {
		rc = daos_epoch_hold(coh, &test->t_epoch, NULL, NULL);
		DBENCH_CHECK(rc, "Failed to hold epoch\n");
	}

	object_open(test->t_id, test->t_epoch, enum_flag, &oh);

	aio_req_init(test);
	for (i = 0; i < counter; i++) {
		ioreq = get_next_ioreq(test);
		kv_set_dkey(test, ioreq, key_type, i);
		kv_set_akey(test, ioreq, key_type, i);
		kv_set_value(test, ioreq->val_iov.iov_buf,
			     counter, i);
		insert((key_type == 2) ?
		       ((comm_world_rank * test->t_nindexes) + i) : 0,
		       test->t_epoch, ioreq);
	}

	while (test->t_naios - naios > 0)
		aio_req_wait(test, 0);
	aio_req_fini(test);
}

static void
kv_update_verify(struct test *test, int key_type)
{
	int		counter, i;
	char		*valbuf = NULL;
	struct a_ioreq	ioreq;

	/**
	 * Verification can happen synchronously!
	 */

	counter = (key_type == 2) ? test->t_nindexes : test->t_nkeys;
	test->t_naios = 1;

	valbuf = malloc(test->t_val_bufsize);
	if (valbuf == NULL)
		DBENCH_ERR(ENOMEM,
			   "Error in allocating lookup buf\n");

	alloc_buffers(test, 1);
	ioreq_init(&ioreq, test, 0);

	for (i = 0; i < counter; i++) {
		kv_set_dkey(test, &ioreq, key_type, i);
		kv_set_akey(test, &ioreq, key_type, i);
		kv_set_value(test, valbuf, counter, i);

		lookup((key_type == 2) ?
		       ((comm_world_rank * test->t_nindexes) + i) : 0,
		       test->t_epoch, &ioreq, test, 1);
		DBENCH_INFO("lookup_buf: %s\n valbuf: %s",
			    (char *)(ioreq.val_iov.iov_buf),
			    (char *)valbuf);

		if (memcmp(ioreq.val_iov.iov_buf, valbuf, test->t_val_bufsize))
			DBENCH_ERR(EIO, "Lookup buffers differ for key :%d",
				   i);
	}
	DBENCH_INFO("Verification complete!\n");
	free_buffers();
	free(valbuf);
}

static void
kv_flush_and_commit(struct test *test)
{
	int	rc = 0;

	if (comm_world_rank == 0) {
		DBENCH_INFO("Flushing Epoch %lu", test->t_epoch);

		rc = daos_epoch_flush(coh, test->t_epoch, NULL, NULL);
		DBENCH_CHECK(rc, "Failed to flush epoch");

		DBENCH_INFO("Committing Epoch :%lu", test->t_epoch);
		rc = daos_epoch_commit(coh, test->t_epoch, NULL, NULL);
		DBENCH_CHECK(rc, "Failed to commit object write\n");
	}

}

static void
kv_multi_dkey_update_run(struct test *test)
{

	kv_test_describe(test, 0);
	MPI_Barrier(MPI_COMM_WORLD);

	DBENCH_PRINT("%s: Inserting %d keys....",
	       test->t_type->tt_name,
	       comm_world_size * test->t_nkeys);
	chrono_record("begin");

	kv_update_async(test, 0, 0);
	MPI_Barrier(MPI_COMM_WORLD);
	DBENCH_INFO("completed %d inserts\n", test->t_nkeys);
	kv_flush_and_commit(test);
	chrono_record("end");
	DBENCH_PRINT("Done!\n");

	/**
	 * Done with benchmarking
	 * Lets verify the test
	 */
	if (t_validate) {
		DBENCH_PRINT("%s: Validating....",
		       test->t_type->tt_name);
		kv_update_verify(test, 0);
		DBENCH_PRINT("Done!\n");
	}
	object_close(oh);

	kv_test_report(test, 0);
}

static void
kv_multi_akey_update_run(struct test *test)
{

	kv_test_describe(test, 0);
	MPI_Barrier(MPI_COMM_WORLD);

	DBENCH_PRINT("%s: Inserting %d keys....",
	       test->t_type->tt_name,
	       comm_world_size * test->t_nkeys);
	chrono_record("begin");

	kv_update_async(test, 1, 0);
	MPI_Barrier(MPI_COMM_WORLD);
	DBENCH_INFO("completed %d inserts\n", test->t_nkeys);
	kv_flush_and_commit(test);
	chrono_record("end");
	DBENCH_PRINT("Done!\n");

	/**
	 * Done with benchmarking
	 * Lets verify the test
	 */
	if (t_validate) {
		DBENCH_PRINT("%s: Validating....",
		       test->t_type->tt_name);
		kv_update_verify(test, 1);
		DBENCH_PRINT("Done!\n");
	}
	object_close(oh);

	kv_test_report(test, 0);
}

static void
kv_multi_dkey_fetch_run(struct test *test)
{
	int		i;
	struct a_ioreq	*ioreq;

	kv_test_describe(test, 0);

	DBENCH_PRINT("%s: Setup by inserting %d keys....",
	       test->t_type->tt_name,
	       comm_world_size * test->t_nkeys);
	MPI_Barrier(MPI_COMM_WORLD);
	kv_update_async(test, 0, 0);
	MPI_Barrier(MPI_COMM_WORLD);
	kv_flush_and_commit(test);
	DBENCH_PRINT("Done!\n");

	/**
	 * We need this buffer to collect all async
	 * lookup results for verification
	 */
	MPI_Barrier(MPI_COMM_WORLD);
	DBENCH_PRINT("%s: Begin by fetching %d keys....",
	       test->t_type->tt_name,
	       comm_world_size * test->t_nkeys);

	chrono_record("begin");
	aio_req_init(test);
	for (i = 0; i < test->t_nkeys; i++) {
		ioreq = get_next_ioreq(test);
		ioreq->r_index = i;
		kv_set_dkey(test, ioreq, 0, i);
		kv_set_akey(test, ioreq, 0, i);
		lookup(/* idx */ 0, test->t_epoch, ioreq, test, 0);
	}

	while (test->t_naios - naios > 0)
		aio_req_wait(test, 1);
	aio_req_fini(test);

	chrono_record("end");
	DBENCH_PRINT("Done!\n");
	MPI_Barrier(MPI_COMM_WORLD);

	object_close(oh);

	kv_test_report(test, 0);
}

static void
kv_multi_akey_fetch_run(struct test *test)
{
	int		i;
	struct a_ioreq	*ioreq;

	kv_test_describe(test, 1);

	DBENCH_PRINT("%s: Setup by inserting %d keys....",
	       test->t_type->tt_name,
	       comm_world_size * test->t_nkeys);
	MPI_Barrier(MPI_COMM_WORLD);
	kv_update_async(test, 1, 0);
	MPI_Barrier(MPI_COMM_WORLD);
	kv_flush_and_commit(test);
	DBENCH_PRINT("Done!\n");

	/**
	 * We need this buffer to collect all async
	 * lookup results for verification
	 */
	MPI_Barrier(MPI_COMM_WORLD);
	DBENCH_PRINT("%s: Begin by fetching %d keys....",
	       test->t_type->tt_name,
	       comm_world_size * test->t_nkeys);

	chrono_record("begin");
	aio_req_init(test);
	for (i = 0; i < test->t_nkeys; i++) {
		ioreq = get_next_ioreq(test);
		ioreq->r_index = i;
		kv_set_dkey(test, ioreq, 1, i);
		kv_set_akey(test, ioreq, 1, i);
		lookup(/* idx */ 0, test->t_epoch, ioreq, test, 0);
	}

	while (test->t_naios - naios > 0)
		aio_req_wait(test, 1);
	aio_req_fini(test);

	chrono_record("end");
	DBENCH_PRINT("Done!\n");
	MPI_Barrier(MPI_COMM_WORLD);

	object_close(oh);

	kv_test_report(test, 0);
}


static void
kv_dkey_enumerate(struct test *test)
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

	DBENCH_PRINT("%s: Setup by inserting %d keys....",
		     test->t_type->tt_name,
		     test->t_nkeys * comm_world_size);
	key_start = (comm_world_rank * test->t_nkeys);
	key_end = (comm_world_rank * test->t_nkeys) + test->t_nkeys;
	DBENCH_INFO("Key Range %d -> %d", key_start, key_end);

	MPI_Barrier(MPI_COMM_WORLD);
	kv_update_async(test, 0, 1);
	MPI_Barrier(MPI_COMM_WORLD);

	kv_flush_and_commit(test);
	DBENCH_PRINT("Done!\n");

	MPI_Barrier(MPI_COMM_WORLD);

	DBENCH_PRINT("%s: Beginning enumerating %d keys....",
		     test->t_type->tt_name,
		     comm_world_size * test->t_nkeys);

	alloc_buffers(test, 1);

	/* All updates completed. Starting to enumerate */
	memset(&hash_out, 0, sizeof(hash_out));
	buf = calloc(5 * test->t_dkey_size, 1);
	ioreq_init(&e_ioreq, test, 0);

	chrono_record("begin");

	/** enumerate records */
	while (!daos_hash_is_eof(&hash_out)) {

		enumerate(test->t_epoch,
			  &number, kds, &hash_out, buf, 5*test->t_dkey_size,
			  &e_ioreq);
		if (number == 0)
			goto next;
		total_keys += number;

		if (t_validate) {
			ptr = buf;
			for (i = 0; i < number; i++) {
				char key[DKEY_SIZE];

				snprintf(key, kds[i].kd_key_len + 1, ptr);
				DBENCH_INFO("i %d key %s len %d", i, key,
						    (int)kds[i].kd_key_len);
					if (atoi(key) >= key_start &&
					    atoi(key) < key_end)
						done++;
					else
						DBENCH_INFO("out of range!!");
					ptr += kds[i].kd_key_len;
			}
		}
next:
		if (daos_hash_is_eof(&hash_out))
			break;
		memset(buf, 0, 5 * test->t_dkey_size);
		number = 5;
	}
	chrono_record("end");
	DBENCH_PRINT("Done\n");
	object_close(oh);
	free_buffers();
	if (t_validate) {
		DBENCH_PRINT("%s: Validating ...",
			     test->t_type->tt_name);

		DBENCH_INFO("Verifying the test");
		DBENCH_INFO("total_keys: %d, Done : %d, nkeys: %d", total_keys,
			    done, test->t_nkeys);
		assert(done == test->t_nkeys);
		DBENCH_INFO("Test Complete");
		DBENCH_PRINT("Done!\n");
	}

	/* Cleanup */
	free(buf);
	kv_test_report(test, 0);
}

static void
kv_multi_idx_update_run(struct test *test)
{

	kv_test_describe(test, 2);

	MPI_Barrier(MPI_COMM_WORLD);
	DBENCH_PRINT("%s: Inserting %d indexes....",
		      test->t_type->tt_name,
		      comm_world_size * test->t_nkeys);

	chrono_record("begin");
	kv_update_async(test, 2, 0);
	DBENCH_INFO("completed %d inserts\n", test->t_nindexes);

	kv_flush_and_commit(test);
	chrono_record("end");
	DBENCH_PRINT("Done!\n");

	MPI_Barrier(MPI_COMM_WORLD);

	if (t_validate) {
		DBENCH_PRINT("%s: Validating....",
			      test->t_type->tt_name);
		kv_update_verify(test, 2);
		DBENCH_PRINT("Done!\n");
	}
	object_close(oh);
	kv_test_report(test, 2);
}

static struct test_type	test_type_mdkvar = {
	"kv-dkey-update",
	kv_multi_dkey_update_run
};

static struct test_type	test_type_makvar = {
	"kv-akey-update",
	kv_multi_akey_update_run
};

static struct test_type	test_type_mivar = {
	"kv-idx-update",
	kv_multi_idx_update_run
};

static struct test_type test_type_dkfetch = {
	"kv-dkey-fetch",
	kv_multi_dkey_fetch_run
};

static struct test_type test_type_akfetch = {
	"kv-akey-fetch",
	kv_multi_akey_fetch_run
};

static struct test_type test_type_dkenum = {
	"kv-dkey-enum",
	kv_dkey_enumerate
};

static struct test_type	*test_types_available[] = {
	&test_type_mdkvar,
	&test_type_makvar,
	&test_type_mivar,
	&test_type_dkfetch,
	&test_type_akfetch,
	&test_type_dkenum,
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
Usage: daosbench -t TEST -p $UUID [OPTIONS]\n\
	Options:\n\
	--test=TEST | -t	Run TEST.\n\
	--testid=id | -o	Test ID(unique for objectID) \n\
	--aios=N | -a		Submit N in-flight I/O requests.\n\
	--dpool=pool | -p	DAOS pool through dmg tool.\n\
	--keys=N | -k		Number of keys to be created in the test. \n\
	--indexes=N | -i	Number of key indexes.\n\
	--value-buf-size=N | -b	value buffer size for this test\n\
	--dkey-size=N | -s	buffer size of dkey for this test\n\
	--pretty-print | -d	pretty-print-flag. \n\
	--check-tests | -c	do data verifications. \n\
	--verbose | -v		verbose flag. \n\
	--help | -h		Print this message and exit.\n\
	Tests Available:\n\
		kv-idx-update	Each mpi rank makes 'n' idx updates\n\
		kv-dkey-update	Each mpi rank makes 'n' dkey updates\n\
		kv-akey-update	Each mpi rank makes 'n' akey updates\n\
		kv-dkey-fetch	Each mpi rank makes 'n' dkey fetches\n\
		kv-akey-fetch	Each mpi rank makes 'n' akey fetches\n\
		kv-dkey-enum    Each mpi rank enumerates 'n' dkeys\n");
}

static int
test_init(struct test *test, int argc, char *argv[])
{
	struct option	options[] = {
		{"aios",		1,	NULL,	'a'},
		{"help",		0,	NULL,	'h'},
		{"keys",		1,	NULL,	'k'},
		{"indexes",		1,	NULL,	'i'},
		{"value-buf-size",	1,	NULL,	'b'},
		{"dkey-size",		1,	NULL,	's'},
		{"akey-size",		1,	NULL,	'y'},
		{"verbose",		0,	NULL,	'v'},
		{"test",		1,	NULL,	't'},
		{"testid",		1,	NULL,	'o'},
		{"check-tests",		0,	NULL,	'c'},
		{"test",		1,	NULL,	't'},
		{"dpool",		1,	NULL,	'p'},
		{"pretty-print",	0,	NULL,	'd'},
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
	test->t_akey_size = AKEY_SIZE;
	test->t_val_bufsize = VAL_BUF_SIZE;
	test->t_naios = 16;
	test->t_nindexes = 1;
	test->t_pname = NULL;
	test->t_id = -1;
	t_validate = false;
	t_pretty_print = false;


	if (comm_world_rank != 0)
		opterr = 0;

	while ((rc = getopt_long(argc, argv, "a:k:i:b:t:o:p:s:hvcd",
				 options, NULL)) != -1) {
		switch (rc) {
		case 'a':
			test->t_naios = atoi(optarg);
			break;
		case 'k':
			test->t_nkeys = atoi(optarg);
			break;
		case 's':
			test->t_dkey_size = atoi(optarg);
			break;
		case 'y':
			test->t_akey_size = atoi(optarg);
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
		case 'o':
			test->t_id = atoi(optarg);
			break;
		case 'c':
			t_validate = true;
			break;
		case 'd':
			t_pretty_print = true;
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

	if (test->t_id < 0) {
		if (comm_world_rank == 0)
			fprintf(stderr,
				"daosbench: '--testid' must be specified\n");
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

	init();

	rc = daos_eq_create(&eq);
	DBENCH_CHECK(rc, "Event queue creation failed\n");

	if (comm_world_rank == 0) {
		crt_rank_t		rank  = 0;

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

		rc = daos_pool_connect(pool_uuid, NULL, &svcl, DAOS_PC_RW,
				       &poh, &pool_info, NULL);
		DBENCH_CHECK(rc, "Pool %s connect failed\n",
			     arg.t_pname);
	}

	handle_share(&poh, HANDLE_POOL, comm_world_rank, poh,
		     verbose ? 1 : 0);
	rc = MPI_Bcast(&pool_info, sizeof(pool_info), MPI_BYTE, 0,
		       MPI_COMM_WORLD);
	DBENCH_CHECK(rc, "broadcast pool_info error\n");

	container_create(comm_world_rank);
	handle_share(&coh, HANDLE_CO, comm_world_rank, poh,
		     verbose ? 1 : 0);

	/** Invoke test **/
	arg.t_type->tt_run(&arg);

	container_destroy(comm_world_rank);
	pool_disconnect(comm_world_rank);
	test_fini(&arg);

	rc = daos_eq_destroy(eq, 0);
	DBENCH_CHECK(rc, "Event queue destroy failed\n");

	fini();

exit:
	MPI_Finalize();

	return rc;
}
