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
/*
 * This is an MPI-based DAOS benchmarking tool.
 */
#define D_LOGFAC	DD_FAC(tests)

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

#include <daos/common.h>
#include <daos/container.h>
#include <daos.h>
#include <errno.h>
#include "suite/daos_test.h"

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
d_rank_list_t			*svcl;
void				*buffers;
void				*dkbuf;
void				*akbuf;
static daos_handle_t		eq;
unsigned int			naios;
static daos_event_t		**events;
bool				t_validate;
bool				t_pretty_print;
bool				t_kill_update;
bool				t_kill_fetch;
bool				t_kill_enum;
bool				t_kill_server;
uint64_t			t_wait;
uint64_t			t_pause;
bool				t_update_for_fetch;
bool				t_keep_container;
daos_oclass_id_t		obj_class = OC_SX;

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
	/* Number of steps for kv-simul test */
	int			t_steps;
	/* Container UUID */
	char			*t_container;
	/* Server crt group ID */
	char			*t_group;
};

struct a_ioreq {
	d_list_t		list;
	daos_event_t		ev;
	daos_key_t		dkey;
	d_iov_t		val_iov;
	daos_iod_t		iod;
	daos_recx_t		rex;
	daos_epoch_range_t	erange;
	d_sg_list_t		sgl;
	char			*dkey_buf;
	char			*akey_buf;
	/* daosbench specific (aio-retrieval) */
	int			r_index;
};

struct value_entry {
	uint32_t	rank;
	uint32_t	index;
	uint64_t	value;
};

/** List to limit AIO operations */
static	D_LIST_HEAD(aios);

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

/*
 * Callers may want to initialize the following "ioreq" fields after calling
 * this function:
 *
 *   - dkey_buf and dkey (via ioreq_init_dkey())
 *   - akey_buf and iod.iod_name (via ioreq_init_akey())
 *   - val_iov (via ioreq_init_value())
 *   - rex.rsize and rex.rx_idx
 *   - r_index
 */
static void
ioreq_init_basic(struct a_ioreq *ioreq, daos_iod_type_t iod_type)
{
	int rc;

	memset(ioreq, 0, sizeof(*ioreq));

	ioreq->iod.iod_type = iod_type;
	ioreq->rex.rx_nr = 1;
	ioreq->erange.epr_hi = DAOS_EPOCH_MAX;

	ioreq->iod.iod_nr = 1;
	ioreq->iod.iod_recxs = &ioreq->rex;
	ioreq->iod.iod_eprs = &ioreq->erange;

	ioreq->sgl.sg_nr = 1;
	ioreq->sgl.sg_iovs = &ioreq->val_iov;

	rc = daos_event_init(&ioreq->ev, eq, NULL);
	DBENCH_CHECK(rc, "Failed to initialize event for aio %p", ioreq);
}

static void
ioreq_fini_basic(struct a_ioreq *ioreq)
{
	daos_event_fini(&ioreq->ev);
}

static void
ioreq_init_dkey(struct a_ioreq *ioreq, void *buf, size_t size)
{
	ioreq->dkey_buf = buf;
	d_iov_set(&ioreq->dkey, buf, size);
}

static void
ioreq_init_akey(struct a_ioreq *ioreq, void *buf, size_t size)
{
	ioreq->akey_buf = buf;
	d_iov_set(&ioreq->iod.iod_name, buf, size);
}

static void
ioreq_init_value(struct a_ioreq *ioreq, void *buf, size_t size)
{
	d_iov_set(&ioreq->val_iov, buf, size);
}

static void
ioreq_init(struct a_ioreq *ioreq, struct test *test, int counter)
{
	int iod_type;

	if (!strcmp(test->t_type->tt_name, "kv-idx-update"))
		iod_type = DAOS_IOD_ARRAY;
	else
		iod_type = DAOS_IOD_SINGLE;

	ioreq_init_basic(ioreq, iod_type);

	ioreq_init_dkey(ioreq, dkbuf + test->t_dkey_size * counter,
			test->t_dkey_size);
	ioreq_init_akey(ioreq, akbuf + test->t_akey_size * counter,
			test->t_akey_size);
	ioreq_init_value(ioreq, buffers + test->t_val_bufsize * counter,
		       test->t_val_bufsize);
}

static void
ioreq_fini(struct a_ioreq *ioreq)
{
	ioreq_fini_basic(ioreq);
}

static void
free_buffers()
{
	D_FREE(buffers);
	D_FREE(dkbuf);
	D_FREE(akbuf);
}

static void
kill_daos_server(const char *grp)
{
	daos_pool_info_t	info = {0};
	d_rank_t		rank;
	int			tgt = -1;
	struct d_tgt_list	targets;
	int			rc;

	rc = daos_pool_query(poh, NULL, &info, NULL, NULL);
	DBENCH_CHECK(rc, "Error in querying pool\n");

	if (info.pi_ntargets - info.pi_ndisabled <= 1)
		return;
	/* choose the last alive one */
	rank = info.pi_ntargets - 1 - info.pi_ndisabled;

	printf("\nKilling target %d (total of %d of %d already disabled)\n",
	       rank, info.pi_ndisabled, info.pi_ntargets);
	fflush(stdout);

	rc  = daos_mgmt_svc_rip(grp, rank, true, NULL);
	DBENCH_CHECK(rc, "Error in killing server\n");

	targets.tl_nr		= 1;
	targets.tl_ranks	= &rank;
	targets.tl_tgts		= &tgt;

	rc = daos_pool_tgt_exclude(pool_uuid, grp, svcl, &targets, NULL);
	DBENCH_CHECK(rc, "Error in excluding pool from poolmap\n");

	memset(&info, 0, sizeof(daos_pool_info_t));
	rc = daos_pool_query(poh, NULL, &info, NULL, NULL);
	DBENCH_CHECK(rc, "Error in query pool\n");

	printf("Target Rank: %d Killed successfully, (%d targets disabled)\n\n",
	       rank, info.pi_ndisabled);
	fflush(stdout);
}

static inline void
kill_and_sync(const char *grp)
{
	double start, end;

	start = MPI_Wtime();
	if (comm_world_rank == 0)
		kill_daos_server(grp);
	MPI_Barrier(MPI_COMM_WORLD);
	end = MPI_Wtime();
	/**
	 *  This is time spent on killing and
	 *  syncing, this will be deducted from
	 *  the total time, such that the time spent
	 *  reported is not squeued
	 */
	t_wait += (end - start);
}

static inline void
sleep_and_sync()
{
	if (comm_world_rank == 0) {
		printf("Pausing "DF_U64" seconds\n",
		       t_wait);
		sleep(t_wait);
	}
	MPI_Barrier(MPI_COMM_WORLD);
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
}

static inline void
kv_set_value(struct test *test, void *buf, int rank, int index, uint64_t value)
{
	struct value_entry	*v = buf;
	int			i;

	for (i = 0; i < test->t_val_bufsize / sizeof(*v); i++) {
		v[i].rank = rank;
		v[i].index = index;
		v[i].value = value;
	}
}

static void
kv_print_value(struct test *test, void *buf)
{
	struct value_entry	*v = buf;
	int			i;

	fprintf(stderr, "value %p\n", v);
	for (i = 0; i < test->t_val_bufsize / sizeof(*v); i++) {
		fprintf(stderr, "  rank: %u\n", v->rank);
		fprintf(stderr, "  index: %u\n", v->index);
		fprintf(stderr, "  value: %lx\n", v->value);
	}
}

static void
aio_req_init(struct test *test)
{
	struct a_ioreq	*ioreq;
	int		i;

	alloc_buffers(test, test->t_naios);
	for (i = 0; i < test->t_naios; i++) {
		D_ALLOC(ioreq, sizeof(*ioreq));
		if (ioreq == NULL)
			DBENCH_ERR(ENOMEM,
				   "Failed to allocate ioreq array");

		memset(ioreq, 0, sizeof(*ioreq));
		ioreq_init(ioreq, test, i);
		d_list_add(&ioreq->list, &aios);

		DBENCH_INFO("Allocated AIO %p: buffer %p", ioreq,
			    ioreq->val_iov.iov_buf);
	}

	naios = test->t_naios;
	D_ALLOC_ARRAY(events, test->t_naios);
	DBENCH_CHECK(events == NULL, "Failed in allocating events array\n");
}

static void
aio_req_fini(struct test *test)
{
	struct a_ioreq *ioreq;
	struct a_ioreq *tmp;

	D_FREE(events);

	d_list_for_each_entry_safe(ioreq, tmp, &aios, list) {
		DBENCH_INFO("Freeing AIO %p: buffer %p", ioreq,
			     ioreq->val_iov.iov_buf);

		d_list_del_init(&ioreq->list);
		ioreq_fini(ioreq);
		D_FREE(ioreq);
	}
	free_buffers();
}

static void
aio_req_wait(struct test *test, int fetch_flag, uint64_t value)
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
		D_ALLOC(valbuf, test->t_val_bufsize);
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
			     ioreq->iod.iod_recxs->rx_idx,
			     ioreq->iod.iod_recxs->rx_nr);

		d_list_move(&ioreq->list, &aios);
		naios++;
		DBENCH_INFO("Completed AIO %p: buffer %p",
			    ioreq, ioreq->val_iov.iov_buf);

		if (fetch_flag && t_validate) {
			kv_set_value(test, valbuf, comm_world_rank,
				     ioreq->r_index, value);
			if (ioreq->val_iov.iov_len != test->t_val_bufsize ||
			    memcmp(ioreq->val_iov.iov_buf, valbuf,
				   test->t_val_bufsize)) {
				kv_print_value(test, ioreq->val_iov.iov_buf);
				kv_print_value(test, valbuf);
				DBENCH_ERR(EIO, "lookup dkey: %s akey: %s "
					   "idx: %d len: %lu (expect %lu)",
					   ioreq->dkey_buf, ioreq->akey_buf,
					   ioreq->r_index,
					   ioreq->val_iov.iov_len,
					   test->t_val_bufsize);
			}
		}
	}
	D_FREE(valbuf);
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


static int
object_open(int t_id, int enum_flag, daos_handle_t *object)
{
	unsigned int	flags;

	if (enum_flag) {
		oid.hi = t_id + comm_world_rank + 1;
		oid.lo = t_id + comm_world_rank;
	} else {
		oid.hi = t_id + 1;
		oid.lo = t_id;
	}
	daos_obj_generate_id(&oid, 0, obj_class, 0);

	MPI_Barrier(MPI_COMM_WORLD);

	flags = DAOS_OO_RW;
	return daos_obj_open(coh, oid, flags, object, NULL);
}

static void
object_close(daos_handle_t object)
{
	int rc;

	rc = daos_obj_close(object, NULL /* ev */);
	DBENCH_CHECK(rc, "Failed to close object");
}

static void
insert(uint64_t idx, daos_handle_t th, struct a_ioreq *req, int sync)
{

	int	rc;

	/** record extent */
	req->iod.iod_size = req->val_iov.iov_len;
	req->rex.rx_idx = idx;

	req->erange.epr_lo = 0;

	DBENCH_INFO("Starting update %p (%d free): dkey '%s' akey '%s' "
		    "iod <%lu, %lu> sgl <%p, %lu> %s",
		    req, naios, (char *)req->dkey.iov_buf,
		    (char *)req->iod.iod_name.iov_buf,
		    req->iod.iod_recxs->rx_idx, req->iod.iod_recxs->rx_nr,
		    req->sgl.sg_iovs->iov_buf,
		    req->sgl.sg_iovs->iov_buf_len,
		    sync ? "sync" : "async");

	/** execute update operation */
	rc = daos_obj_update(oh, th, 0, &req->dkey, 1, &req->iod, &req->sgl,
			     sync ? NULL : &req->ev);
	DBENCH_CHECK(rc, "object update failed\n");
}

static void
enumerate(daos_handle_t th, uint32_t *number, daos_key_desc_t *kds,
	  daos_anchor_t *anchor, char *buf, int len, struct a_ioreq *req)
{
	int	rc;

	d_iov_set(&req->val_iov, buf, len);

	/** execute fetch operation */
	rc = daos_obj_list_dkey(oh, th, number, kds, &req->sgl, anchor, NULL);

	DBENCH_CHECK(rc, "daos_obj_list_dkey failed\n");
}

static void
lookup(uint64_t idx, daos_handle_t th, struct a_ioreq *req,
       struct test *test, int verify)
{
	int rc;

	/** record extent */
	req->iod.iod_size = req->val_iov.iov_len;
	req->rex.rx_idx = idx;

	/** XXX: to be fixed */
	req->erange.epr_lo = 0;

	DBENCH_INFO("Starting lookup %p (%d free %d busy): dkey '%s' akey '%s' "
		    "iod <%lu, %lu> sgl <%p, %lu> %s", req, naios,
		    test->t_naios - naios, (char *)(req->dkey.iov_buf),
		    (char *)req->iod.iod_name.iov_buf,
		    req->iod.iod_recxs->rx_idx,
		    req->iod.iod_recxs->rx_nr, req->sgl.sg_iovs->iov_buf,
		    req->sgl.sg_iovs->iov_buf_len,
		    verify ? "sync" : "async");

	/** execute fetch operation */
	rc = daos_obj_fetch(oh, th, 0, &req->dkey, 1, &req->iod, &req->sgl,
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

static int
container_open(int rank, char *uuid_str, int create)
{
	int	rc = 0;

	assert(create || uuid_str != NULL);
	if (rank == 0) {
		if (uuid_str == NULL) {
			uuid_generate(cont_uuid);
		} else {
			rc = uuid_parse(uuid_str, cont_uuid);
			DBENCH_CHECK(rc, "Failed to parse container "
				     "UUID: %s", uuid_str);
		}

		if (create) {
			rc = daos_cont_create(poh, cont_uuid, NULL, NULL);
			DBENCH_CHECK(rc, "Container create failed\n");
		}

		rc = daos_cont_open(poh, cont_uuid, DAOS_COO_RW, &coh,
				    &cont_info, NULL);
	}
	return rc;
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

	if (rank == 0) {
		rc = daos_cont_close(coh, NULL);
		DBENCH_CHECK(rc, "Failed to close container\n");
	}

}

static void
container_destroy(int rank)
{
	int	rc;

	if (rank != 0)
		return;

	DBENCH_PRINT("Destroying container\n");
	rc = daos_cont_destroy(poh, cont_uuid, 1, NULL);
	DBENCH_CHECK(rc, "Container destroy failed\n");
}


static void
kv_test_report(struct test *test, int key_type)
{
	if (!comm_world_rank) {
		double d = chrono_read("end") - chrono_read("begin");
		uint32_t count = (key_type == 2) ? test->t_nindexes :
				test->t_nkeys;

		/** Subtract pause time from total time */
		if (t_wait > 0 && (d - t_wait) > 0) {
			DBENCH_PRINT("Deducting time spent pausing\n");
			d -= t_wait;
		}

		printf("%s\n", test->t_type->tt_name);
		printf("Time: %f seconds (%f ops per second)\n",
		       d, (count * comm_world_size)/d);
	}
}

static void
metadata_test_report(struct test *test)
{
	if (!comm_world_rank) {
		double d = chrono_read("m_end") - chrono_read("m_begin");

		printf("%s\n", test->t_type->tt_name);
		printf("Time: %f seconds\n", d);
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
		printf("DAOS pool :%s\n", test->t_pname);
		printf("DAOS container :%s\n", test->t_container);
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
		aio_req_wait(test, 0, 0);
	ioreq = d_list_entry(aios.next, struct a_ioreq, list);
	d_list_move_tail(&ioreq->list, &aios);
	naios--;

	return ioreq;
}

static void
update_async(struct test *test, int key_type, uint64_t value)
{
	int		i;
	struct a_ioreq	*ioreq;
	int		counter;

	counter = (key_type == 2) ? test->t_nindexes : test->t_nkeys;

	aio_req_init(test);

	for (i = 0; i < counter; i++) {

		if (i == counter/2 && t_kill_update) {
			if (t_wait > 0)
				sleep_and_sync();
			else
				kill_and_sync(test->t_group);
		}
		ioreq = get_next_ioreq(test);
		kv_set_dkey(test, ioreq, key_type, i);
		kv_set_akey(test, ioreq, key_type, i);
		kv_set_value(test, ioreq->val_iov.iov_buf, comm_world_rank, i,
			     value);
		insert((key_type == 2) ?
		       ((comm_world_rank * test->t_nindexes) + i) : 0,
		       DAOS_TX_NONE, ioreq, 0 /* sync */);
	}

	while (test->t_naios - naios > 0)
		aio_req_wait(test, 0, 0);

	aio_req_fini(test);
}

static void
kv_update_async(struct test *test, int key_type, int enum_flag, uint64_t value,
		bool update)
{
	int rc;

	rc = object_open(test->t_id, enum_flag, &oh);
	DBENCH_CHECK(rc, "Failed to open object");

	if (update)
		update_async(test, key_type, value);
}

static void
update_verify(struct test *test, int key_type, uint64_t value)
{
	int		counter, i;
	char		*valbuf = NULL;
	struct a_ioreq	ioreq;

	/**
	 * Verification can happen synchronously!
	 */
	counter = (key_type == 2) ? test->t_nindexes : test->t_nkeys;

	D_ALLOC(valbuf, test->t_val_bufsize);
	if (valbuf == NULL)
		DBENCH_ERR(ENOMEM,
			   "Error in allocating lookup buf\n");

	alloc_buffers(test, 1);
	ioreq_init(&ioreq, test, 0);

	for (i = 0; i < counter; i++) {
		kv_set_dkey(test, &ioreq, key_type, i);
		kv_set_akey(test, &ioreq, key_type, i);
		memset(ioreq.val_iov.iov_buf, 0xda, test->t_val_bufsize);

		kv_set_value(test, valbuf, comm_world_rank, i, value);

		lookup((key_type == 2) ?
		       ((comm_world_rank * test->t_nindexes) + i) : 0,
		       DAOS_TX_NONE, &ioreq, test, 1);

		if (ioreq.val_iov.iov_len != test->t_val_bufsize ||
		    memcmp(ioreq.val_iov.iov_buf, valbuf,
			   test->t_val_bufsize)) {
			kv_print_value(test, ioreq.val_iov.iov_buf);
			kv_print_value(test, valbuf);
			DBENCH_ERR(EIO, "Lookup buffers differ for key[%d] "
				   "<%s, %s>: actual %lu bytes, expected %lu "
				   "bytes", i, ioreq.dkey_buf, ioreq.akey_buf,
				   ioreq.val_iov.iov_len, test->t_val_bufsize);
		}
	}
	ioreq_fini(&ioreq);
	free_buffers();
	D_FREE(valbuf);
}

static void
kv_snapshot(struct test *test)
{
	daos_epoch_t	snap;
	int		rc = 0;

	if (comm_world_rank == 0) {
		DBENCH_INFO("Creating Snapshot...");
		rc = daos_cont_create_snap(coh, &snap, NULL, NULL);
		DBENCH_CHECK(rc, "Failed to create snapshot.\n");
	}

}

static void
kv_multi_dkey_update_run(struct test *test)
{
	uint64_t value = 0xda05da0500000001;

	kv_test_describe(test, 0);
	MPI_Barrier(MPI_COMM_WORLD);

	DBENCH_PRINT("%s: Inserting %d keys....",
	       test->t_type->tt_name,
	       comm_world_size * test->t_nkeys);
	chrono_record("begin");

	kv_update_async(test, 0/* key_type */,
			0/* enum_flag */, value, true);
	MPI_Barrier(MPI_COMM_WORLD);
	DBENCH_INFO("completed %d inserts\n", test->t_nkeys);
	kv_snapshot(test);
	chrono_record("end");
	DBENCH_PRINT("Done!\n");

	/**
	 * Done with benchmarking
	 * Lets verify the test
	 */
	if (t_validate) {
		DBENCH_PRINT("%s: Validating....",
		       test->t_type->tt_name);
		update_verify(test, 0, value);
		DBENCH_PRINT("Done!\n");
	}
	object_close(oh);

	kv_test_report(test, 0);
}

static void
kv_multi_akey_update_run(struct test *test)
{
	uint64_t value = 0xda05da0500000002;

	kv_test_describe(test, 0);
	MPI_Barrier(MPI_COMM_WORLD);

	DBENCH_PRINT("%s: Inserting %d keys....",
	       test->t_type->tt_name,
	       comm_world_size * test->t_nkeys);
	chrono_record("begin");

	kv_update_async(test, 1, 0, value, true);
	MPI_Barrier(MPI_COMM_WORLD);
	DBENCH_INFO("completed %d inserts\n", test->t_nkeys);
	kv_snapshot(test);
	chrono_record("end");
	DBENCH_PRINT("Done!\n");

	/**
	 * Done with benchmarking
	 * Lets verify the test
	 */
	if (t_validate) {
		DBENCH_PRINT("%s: Validating....",
		       test->t_type->tt_name);
		update_verify(test, 1, value);
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
	uint64_t	value	= 0xda05da0500000004;
	bool		update	= true;

	kv_test_describe(test, 0);

	if (t_update_for_fetch) {
		update = false;
		value = 0xda05da0500000001;
	}

	if (update)
		DBENCH_PRINT("%s: Setup by inserting %d keys....",
		       test->t_type->tt_name,
		       comm_world_size * test->t_nkeys);

	MPI_Barrier(MPI_COMM_WORLD);
	kv_update_async(test, 0, 0, value, update);
	MPI_Barrier(MPI_COMM_WORLD);
	if (update) {
		kv_snapshot(test);
		DBENCH_PRINT("Done!\n");
	}

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
		if (i == test->t_nkeys/2 && t_kill_fetch) {
			if (t_wait > 0)
				sleep_and_sync();
			else
				kill_and_sync(test->t_group);
		}
		ioreq = get_next_ioreq(test);
		ioreq->r_index = i;
		kv_set_dkey(test, ioreq, 0, i);
		kv_set_akey(test, ioreq, 0, i);
		lookup(/* idx */ 0, DAOS_TX_NONE, ioreq, test, 0);
	}

	while (test->t_naios - naios > 0)
		aio_req_wait(test, 1, value);
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
	uint64_t	value = 0xda05da0500000005;
	bool		update = true;

	kv_test_describe(test, 1);

	if (t_update_for_fetch) {
		update = false;
		value = 0xda05da0500000002;
	}

	if (update)
		DBENCH_PRINT("%s: Setup by inserting %d keys....",
			     test->t_type->tt_name,
			     comm_world_size * test->t_nkeys);
	MPI_Barrier(MPI_COMM_WORLD);
	kv_update_async(test, 1, 0, value, update);
	MPI_Barrier(MPI_COMM_WORLD);
	if (update) {
		kv_snapshot(test);
		DBENCH_PRINT("Done!\n");
	}

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
		if (i == test->t_nkeys/2 && t_kill_fetch) {
			if (t_wait > 0)
				sleep_and_sync();
			else
				kill_and_sync(test->t_group);
		}
		ioreq = get_next_ioreq(test);
		ioreq->r_index = i;
		kv_set_dkey(test, ioreq, 1, i);
		kv_set_akey(test, ioreq, 1, i);
		lookup(/* idx */ 0, DAOS_TX_NONE, ioreq, test, 0);
	}

	while (test->t_naios - naios > 0)
		aio_req_wait(test, 1, value);
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
	daos_anchor_t		anchor_out;
	daos_key_desc_t		kds[5];
	char			*buf, *ptr;
	int			total_keys = 0;
	int			i;
	struct a_ioreq		e_ioreq;
	int			done = 0;
	int			key_start, key_end;
	uint64_t		value = 0xda05da0500000006;
	bool			enum_pause = false;

	kv_test_describe(test, 0);

	DBENCH_PRINT("%s: Setup by inserting %d keys....",
		     test->t_type->tt_name,
		     test->t_nkeys * comm_world_size);
	key_start = (comm_world_rank * test->t_nkeys);
	key_end = (comm_world_rank * test->t_nkeys) + test->t_nkeys;
	DBENCH_INFO("Key Range %d -> %d", key_start, key_end);

	MPI_Barrier(MPI_COMM_WORLD);
	kv_update_async(test, 0, 1, value, true);
	MPI_Barrier(MPI_COMM_WORLD);

	kv_snapshot(test);
	DBENCH_PRINT("Done!\n");

	MPI_Barrier(MPI_COMM_WORLD);

	DBENCH_PRINT("%s: Beginning enumerating %d keys....",
		     test->t_type->tt_name,
		     comm_world_size * test->t_nkeys);

	alloc_buffers(test, 1);

	/* All updates completed. Starting to enumerate */
	memset(&anchor_out, 0, sizeof(anchor_out));
	D_ALLOC_ARRAY(buf, (int)(5 * test->t_dkey_size));
	ioreq_init(&e_ioreq, test, 0);

	chrono_record("begin");

	/** enumerate records */
	while (!daos_anchor_is_eof(&anchor_out)) {

		if (!enum_pause &&
		    (number >= total_keys / 2) && t_kill_enum) {
			if (t_wait > 0)
				sleep_and_sync();
			else
				kill_and_sync(test->t_group);
			enum_pause = true;
		}

		enumerate(DAOS_TX_NONE, &number, kds, &anchor_out, buf,
			  5*test->t_dkey_size, &e_ioreq);
		if (number == 0)
			goto next;
		total_keys += number;

		if (t_validate) {
			ptr = buf;
			for (i = 0; i < number; i++) {
				char key[DKEY_SIZE];

				snprintf(key, kds[i].kd_key_len + 1, "%s", ptr);
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
		if (daos_anchor_is_eof(&anchor_out))
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
	D_FREE(buf);
	kv_test_report(test, 0);
}

static void
kv_multi_idx_update_run(struct test *test)
{
	uint64_t value = 0xda05da0500000003;

	kv_test_describe(test, 2);

	MPI_Barrier(MPI_COMM_WORLD);
	DBENCH_PRINT("%s: Inserting %d indexes....",
		      test->t_type->tt_name,
		      comm_world_size * test->t_nindexes);

	chrono_record("begin");
	kv_update_async(test, 2, 0, value, true);
	DBENCH_INFO("completed %d inserts\n", test->t_nindexes);

	kv_snapshot(test);
	chrono_record("end");
	DBENCH_PRINT("Done!\n");

	MPI_Barrier(MPI_COMM_WORLD);

	if (t_validate) {
		DBENCH_PRINT("%s: Validating....",
			      test->t_type->tt_name);
		update_verify(test, 2, value);
		DBENCH_PRINT("Done!\n");
	}
	object_close(oh);
	kv_test_report(test, 2);
}


/**
 * Creates or open container at rank 0 and
 * shares the handle across all other processes
 */
static void
daos_container_create_open(struct test *test)
{
	int rc;

	if (strcmp(test->t_type->tt_name, "daos-co-create") == 0)
		chrono_record("m_begin");

	rc = container_open(comm_world_rank, test->t_container,
			    0 /* open */);
	if (rc == -DER_NONEXIST) {
		DBENCH_PRINT("Creating container\n");
		rc = container_open(comm_world_rank, test->t_container,
				    1 /* create */);
	} else {
		DBENCH_PRINT("Found container: %s\n", test->t_container);
	}

	DBENCH_CHECK(rc, "Failed to open container\n");
	handle_share(&coh, HANDLE_CO, comm_world_rank, poh,
		     verbose ? 1 : 0);

	if (strcmp(test->t_type->tt_name, "daos-co-create") == 0) {
		chrono_record("m_end");
		metadata_test_report(test);
	}
}

static const char kv_simul_meta_dkey[] = "kv_simul";
static const char kv_simul_meta_step_akey[] = "step";

enum {
	READ,
	WRITE
};

static void
kv_simul_rw_step(struct test *test, int rw, int *step)
{
	struct a_ioreq	ioreq;

	ioreq_init_basic(&ioreq, DAOS_IOD_SINGLE);

	ioreq_init_dkey(&ioreq, (void *)kv_simul_meta_dkey,
			sizeof(kv_simul_meta_dkey));
	ioreq_init_akey(&ioreq, (void *)kv_simul_meta_step_akey,
			sizeof(kv_simul_meta_step_akey));
	ioreq_init_value(&ioreq, step, sizeof(*step));

	if (rw == READ) {
		lookup(0 /* idx */, DAOS_TX_NONE, &ioreq, test,
		       1 /* verify (sync) */);
		if (ioreq.val_iov.iov_len == 0) {
			DBENCH_PRINT("Metadata empty\n");
			*step = 0;
		} else if (ioreq.val_iov.iov_len != sizeof(*step)) {
			DBENCH_ERR(EIO, "Unexpected value size for dkey %s "
				   "akey %s: %lu",
				   (char *)ioreq.dkey.iov_buf,
				   (char *)ioreq.iod.iod_name.iov_buf,
				   ioreq.val_iov.iov_len);
		}
	} else {
		insert(0 /* idx */, DAOS_TX_NONE, &ioreq, 1 /* sync */);
	}

	ioreq_fini_basic(&ioreq);
}

/*
 *   - All ranks work on the same DAOS object.
 *   - Every rank works on its own set of (test->t_dkeys) d-keys.
 *   - Rank 0 maintains test metadata in kv_simul_meta_dkey.
 *     - A-key "last_step"
 *   - One kv_simul test involves test->t_steps steps.
 *   - Steps are numbered from 1 to test->t_steps.
 *   - Each step verifies the data written in last step and overwrites with new
 *     values.
 *   - A kv_simul test may be resumed from a preious run with exactly the same
 *     parameters that was interrupted. It resumes from last committed step.
 */
static void
kv_simul(struct test *test)
{
	int		key_type = 0 /* multiple d-keys */;
	uint64_t	value = 0xda05da0500000007;
	int		step = 0;
	int		rc;

	MPI_Barrier(MPI_COMM_WORLD);

	/* Prepare the object. */
	rc = object_open(test->t_id, 0, &oh);
	if (rc == -DER_NONEXIST)
		rc = object_open(test->t_id, 0, &oh);
	DBENCH_CHECK(rc, "Failed to open object");

	/* Determine the step number to start from. */
	DBENCH_PRINT("Reading last committed step number\n");
	if (comm_world_rank == 0)
		kv_simul_rw_step(test, READ, &step);
	MPI_Bcast(&step, 1, MPI_INT, 0, MPI_COMM_WORLD);
	step++;

	/* Verify and write new steps. */
	while (step <= test->t_steps) {
		/* If there is a previous step, then read and verify it. */
		if (step > 1) {
			MPI_Barrier(MPI_COMM_WORLD);
			DBENCH_PRINT("Step %d: reading and verifying step %d\n",
				     step, step - 1);
			update_verify(test, key_type, value + step - 1);
		}

		MPI_Barrier(MPI_COMM_WORLD);
		DBENCH_PRINT("Step %d: writing\n", step);
		update_async(test, key_type, value + step);

		DBENCH_PRINT("Step %d: writing last committed step number\n",
			     step);
		if (comm_world_rank == 0)
			kv_simul_rw_step(test, WRITE, &step);

		MPI_Barrier(MPI_COMM_WORLD);
		DBENCH_PRINT("Step %d: snapshoting\n", step);
		kv_snapshot(test);

		step++;
		DBENCH_PRINT("Sleep "DF_U64" seconds before the next step\n",
			     t_pause);
		sleep(t_pause);
		DBENCH_PRINT("Done sleeping.. continue with step: %d\n",
			     step);

	}

	/* Read and verify the last step. */
	MPI_Barrier(MPI_COMM_WORLD);
	DBENCH_PRINT("Final step: reading and verifying step %d\n", step - 1);
	update_verify(test, key_type, value + step - 1);

	object_close(oh);
}

static struct test_type test_co_create = {
	"daos-co-create",
	daos_container_create_open
};

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

static struct test_type test_type_simul = {
	"kv-simul",
	kv_simul
};

static struct test_type	*test_types_available[] = {
	&test_type_mdkvar,
	&test_type_makvar,
	&test_type_mivar,
	&test_type_dkfetch,
	&test_type_akfetch,
	&test_type_dkenum,
	&test_type_simul,
	&test_co_create,
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
	--object-class=oc | -j	Object Class options : \n\
				TINY, SMALL, LARGE, REPL_2_RW, \n\
				R3, R3S, R4, R4S, REPL_MAX \n\
				(default object class : LARGE) \n\
	--aios=N | -a		Submit N in-flight I/O requests.\n\
	--group=GROUP | -g	Server group ID\n\
	--dpool=pool | -p	DAOS pool through dmg tool.\n\
	--svc=RANKS | -S	Pool service ranks (e.g., 1:2:3:4:5)\n\
	--keys=N | -k		Number of keys to be created in the test. \n\
	--indexes=N | -i	Number of key indexes.\n\
	--value-buf-size=N | -b	value buffer size for this test\n\
	--dkey-size=N | -s	buffer size of dkey for this test\n\
	--steps=N | -e		steps for kv-simul test\n\
	--container=UUID | -n	container UUID\n\
	--kill-server	| -u	kill daos-server(default from daosbench) \n\
	--no-update-for-fetch | -f\n\
				Fetch without setting up with updates\n\
	--keep-container | -r	Keep container without destroying\n\
	--pause=seconds | -w	pause test for some time\n\
	--wait-for-kill | -l	wait tests to give time to kill\n\
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
		kv-dkey-enum    Each mpi rank enumerates 'n' dkeys\n\
		kv-simul	Each mpi rank makes 'n' steps of updates\n");
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
		{"no-update-for-fetch", 0,	NULL,	'f'},
		{"object-class",	1,	NULL,	'j'},
		{"dpool",		1,	NULL,	'p'},
		{"pretty-print",	0,	NULL,	'd'},
		{"steps",		1,	NULL,	'e'},
		{"kill-server",		0,	NULL,	'u'},
		{"keep-container",	0,	NULL,	'r'},
		{"pause",		1,	NULL,	'w'},
		{"wait-for-kill",	1,	NULL,	'l'},
		{"container",		1,	NULL,	'n'},
		{"group",		1,	NULL,	'g'},
		{"svc",			1,	NULL,	'S'},
		{NULL,			0,	NULL,	0}
	};
	const char     *svcl_str = NULL;
	int		rc;
	int		first = 1;

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
	test->t_steps = 2;
	t_validate		= false;
	t_pretty_print		= false;
	t_kill_server		= false;
	t_kill_update		= false;
	t_kill_fetch		= false;
	t_kill_enum		= false;
	t_keep_container	= false;
	t_wait			= 0;
	t_pause			= 0;

	if (comm_world_rank != 0)
		opterr = 0;

	while ((rc = getopt_long(argc, argv,
				 "a:k:i:b:t:o:p:s:hvcde:rnf:uw:j:S:",
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
		case 'j':
			if (!strcasecmp(optarg, "TINY")) {
				obj_class = OC_S1;
			} else if (!strcasecmp(optarg, "SMALL")) {
				obj_class = OC_S4;
			} else if (!strcasecmp(optarg, "LARGE")) {
				obj_class = OC_SX;
			} else if (!strcasecmp(optarg, "ECHO")) {
				obj_class = DAOS_OC_ECHO_TINY_RW;
			} else if (!strcasecmp(optarg, "R2")) {
				obj_class = OC_RP_2G2;
			} else if (!strcasecmp(optarg, "R2S")) {
				obj_class = OC_RP_2G1;
			} else if (!strcasecmp(optarg, "R3")) {
				obj_class = OC_RP_3G2;
			} else if (!strcasecmp(optarg, "R3S")) {
				obj_class = OC_RP_3G1;
			} else if (!strcasecmp(optarg, "R4")) {
				obj_class = OC_RP_4G2;
			} else if (!strcasecmp(optarg, "R4S")) {
				obj_class = OC_RP_4G1;
			} else if (!strcasecmp(optarg, "REPL_MAX")) {
				obj_class = OC_RP_XSF;
			} else {
				fprintf(stderr,
					"\ndaosbench: Unknown object class\n");
				if (comm_world_rank == 0)
					usage();
				return 1;
			}
			break;
		case 'l':
			t_wait = atoi(optarg);
			break;
		case 'w':
			t_pause = atoi(optarg);
			DBENCH_PRINT("kv-simul will pause for "DF_U64"s\n",
				     t_pause);
			break;
		case 'u':
			t_kill_server = true;
			break;
		case 'r':
			t_keep_container = true;
			break;
		case 'f':
			t_update_for_fetch = true;
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
		case 'e':
			test->t_steps = atoi(optarg);
			break;
		case 'n':
			test->t_container = optarg;
			break;
		case 'g':
			test->t_group = optarg;
			break;
		case 'S':
			svcl_str = optarg;
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

	if (test->t_container == NULL) {
		if (comm_world_rank == 0)
			fprintf(stderr,
				"daosbench: container cannot be NULL\n");
		return 2;
	}

	if (test->t_steps <= 0) {
		if (comm_world_rank == 0)
			fprintf(stderr,
				"daosbench: '--steps' must be >= 1\n");
		return 2;

	}

	if (test->t_val_bufsize % sizeof(struct value_entry) != 0) {
		if (comm_world_rank == 0)
			fprintf(stderr,
				"daosbench: '--value-buf-size' must be a "
				"multiple of %u\n",
				(int)sizeof(struct value_entry));
		return 2;
	}

	if (t_wait > 0 && t_kill_server) {
		if (comm_world_rank == 0)
			fprintf(stderr,
				"ERR: Trying to kill implicit & explicit\n");
		return 2;
	}

	if (t_kill_server && obj_class != OC_RP_XSF &&
	    obj_class != OC_RP_3G2) {
		if (comm_world_rank == 0)
			fprintf(stderr,
				"daosbench: REPL or REPL_MAX obj-class "
				"required for degraded mode\n");
		return 2;
	}

	if (t_wait > 0 || t_kill_server) {
		if (strstr(test->t_type->tt_name, "update") != NULL)
			t_kill_update	= true;
		else if (strstr(test->t_type->tt_name, "fetch") != NULL)
			t_kill_fetch	= true;
		else if (strstr(test->t_type->tt_name, "enum") != NULL)
			t_kill_enum	= true;
	}

	svcl = daos_rank_list_parse(svcl_str, ":");
	if (svcl == NULL)
		return 2;

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
	D_FREE(test->t_pname);
	if (comm_world_rank == 0) {
		time_t	t = time(NULL);

		printf("\n");
		printf("Ended at %s", ctime(&t));
	}
	d_rank_list_free(svcl);
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
		if (strlen(arg.t_pname) == 0)
			DBENCH_ERR(EINVAL, "'daosPool' must be specified");
		DBENCH_INFO("Connecting to Pool: %s",
			    arg.t_pname);
		rc = uuid_parse(arg.t_pname, pool_uuid);
		DBENCH_CHECK(rc, "Failed to parsr 'daosPool': %s",
			     arg.t_pname);
		rc = daos_pool_connect(pool_uuid, arg.t_group, svcl,
				       DAOS_PC_RW, &poh, &pool_info, NULL);
		DBENCH_CHECK(rc, "Pool %s connect failed\n",
			     arg.t_pname);
	}


	handle_share(&poh, HANDLE_POOL, comm_world_rank, poh,
		     verbose ? 1 : 0);
	rc = MPI_Bcast(&pool_info, sizeof(pool_info), MPI_BYTE, 0,
		       MPI_COMM_WORLD);
	DBENCH_CHECK(rc, "broadcast pool_info error\n");

	/** Invoke test **/
	if (strcmp(arg.t_type->tt_name, "daos-co-create") != 0)
		daos_container_create_open(&arg);

	arg.t_type->tt_run(&arg);

	container_close(comm_world_rank);
	if (!t_keep_container)
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
