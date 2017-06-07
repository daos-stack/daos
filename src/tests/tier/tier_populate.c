#define DD_SUBSYS	DD_FAC(tests)

#include <getopt.h>
#include "daos_test.h"
#include <stdio.h>
#include "tier_test.h"
#include "daos_iotest.h"

char USAGE[] = "ds_tier_populate <cold_tier_group>";

char *g_group_id;

daos_epoch_t g_epoch;
FILE	    *fp;


daos_obj_id_t
test_oid_gen(uint16_t oclass, unsigned seed)
{
	daos_obj_id_t oid = dts_oid_gen(oclass, seed);

	fprintf(fp, "OID:"DF_OID"\n", DP_OID(oid));
	return oid;
}

void
ioreq_init(struct ioreq *req, daos_handle_t coh, daos_obj_id_t oid,
	   daos_iod_type_t iod_type, test_arg_t *arg)
{
	int rc;
	int i;
	bool ev_flag;

	memset(req, 0, sizeof(*req));

	req->iod_type = iod_type;
	req->arg = arg;
	if (arg->async) {
		rc = daos_event_init(&req->ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	arg->expect_result = 0;
	daos_fail_loc_set(arg->fail_loc);
	daos_fail_value_set(arg->fail_value);

	/* init sgl */
	for (i = 0; i < IOREQ_SG_IOD_NR; i++) {
		req->sgl[i].sg_nr.num = IOREQ_SG_NR;
		req->sgl[i].sg_iovs = req->val_iov[i];
	}

	/* init csum */
	daos_csum_set(&req->csum, &req->csum_buf[0], UPDATE_CSUM_SIZE);

	/* init record extent */
	for (i = 0; i < IOREQ_SG_IOD_NR; i++) {
		int j;

		for (j = 0; j < IOREQ_IOD_NR; j++) {
			req->rex[i][j].rx_nr = 1;
			req->rex[i][j].rx_idx = 0;

			/** epoch range: required by the wire format */
			req->erange[i][j].epr_lo = 0;
			req->erange[i][j].epr_hi = DAOS_EPOCH_MAX;
		}

		/* I/O descriptor */
		req->iod[i].iod_recxs = req->rex[i];
		req->iod[i].iod_nr = IOREQ_IOD_NR;

		/* epoch descriptor */
		req->iod[i].iod_eprs = req->erange[i];

		req->iod[i].iod_kcsum.cs_csum = NULL;
		req->iod[i].iod_kcsum.cs_buf_len = 0;
		req->iod[i].iod_kcsum.cs_len = 0;
		req->iod[i].iod_type = iod_type;

	}
	D_DEBUG(DF_MISC, "open oid="DF_OID"\n", DP_OID(oid));

	/** open the object */
	rc = daos_obj_open(coh, oid, 0, 0, &req->oh,
			   req->arg->async ? &req->ev : NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		rc = daos_event_test(&req->ev, DAOS_EQ_WAIT, &ev_flag);
		assert_int_equal(rc, 0);
		assert_int_equal(ev_flag, true);
		assert_int_equal(req->ev.ev_error, 0);
	}
}

void
ioreq_fini(struct ioreq *req)
{
	int rc;

	rc = daos_obj_close(req->oh, NULL);
	assert_int_equal(rc, 0);

	req->arg->fail_loc = 0;
	req->arg->fail_value = 0;
	daos_fail_loc_set(0);
	if (req->arg->async) {
		rc = daos_event_fini(&req->ev);
		assert_int_equal(rc, 0);
	}
}

static void
insert_internal(daos_key_t *dkey, int nr, daos_sg_list_t *sgls,
		daos_iod_t *iods, daos_epoch_t epoch, struct ioreq *req)
{
	bool ev_flag;
	int rc;

	/** execute update operation */
	D_DEBUG(DF_MISC, "OBJ_UPDATE - %d records\n", nr);
	rc = daos_obj_update(req->oh, epoch, dkey, nr, iods, sgls,
			     req->arg->async ? &req->ev : NULL);
	if (!req->arg->async) {
		assert_int_equal(rc, req->arg->expect_result);
		return;
	}

	rc = daos_event_test(&req->ev, DAOS_EQ_WAIT, &ev_flag);
	assert_int_equal(rc, 0);
	assert_int_equal(ev_flag, true);
	assert_int_equal(req->ev.ev_error, req->arg->expect_result);
}

static void
ioreq_dkey_set(struct ioreq *req, const char *dkey)
{
	daos_iov_set(&req->dkey, (void *)dkey, strlen(dkey));
}

static void
ioreq_akey_set(struct ioreq *req, const char **akey, int nr)
{
	int i;

	assert_in_range(nr, 1, IOREQ_SG_IOD_NR);
	/** akey */
	for (i = 0; i < nr; i++)
		daos_iov_set(&req->iod[i].iod_name, (void *)akey[i],
			     strlen(akey[i]));
}

static void
ioreq_sgl_simple_set(struct ioreq *req, void **value,
		     daos_size_t *size, int nr)
{
	daos_sg_list_t *sgl = req->sgl;
	int i;

	assert_in_range(nr, 1, IOREQ_SG_IOD_NR);
	for (i = 0; i < nr; i++) {
		sgl[i].sg_nr.num = 1;
		sgl[i].sg_nr.num_out = 0;
		daos_iov_set(&sgl[i].sg_iovs[0], value[i], size[i]);
	}
}

static void
ioreq_iod_simple_set(struct ioreq *req, daos_size_t *size,
		     uint64_t *idx, daos_epoch_t *epoch, int nr)
{
	daos_iod_t *iod = req->iod;
	int i;

	assert_in_range(nr, 1, IOREQ_SG_IOD_NR);
	for (i = 0; i < nr; i++) {
		/** record extent */
		iod[i].iod_type = req->iod_type;
		iod[i].iod_size = size[i];
		if (req->iod_type == DAOS_IOD_ARRAY) {
			iod[i].iod_recxs[0].rx_idx = idx[i] + i * SEGMENT_SIZE;
			iod[i].iod_recxs[0].rx_nr = 1;
		}

		/** XXX: to be fixed */
		iod[i].iod_eprs[0].epr_lo = *epoch;
		iod[i].iod_nr = 1;

		D_DEBUG(DF_TIERS,
			"%d: typ:%d sz:%lu idx:"DF_U64" nr:"DF_U64"\n",
			i, iod[i].iod_type, iod[i].iod_size,
			iod[i].iod_recxs[0].rx_idx,
			iod[i].iod_recxs[0].rx_nr);
	}
}

void
insert(const char *dkey, int nr, const char **akey, uint64_t *idx,
       void **val, daos_size_t *size, daos_epoch_t *epoch, struct ioreq *req)
{
	assert_in_range(nr, 1, IOREQ_SG_IOD_NR);

	/* dkey */
	ioreq_dkey_set(req, dkey);

	/* akey */
	ioreq_akey_set(req, akey, nr);

	/* set sgl */
	if (val != NULL)
		ioreq_sgl_simple_set(req, val, size, nr);

	/* set iod */
	ioreq_iod_simple_set(req, size, idx, epoch, nr);

	insert_internal(&req->dkey, nr, val == NULL ? NULL : req->sgl,
			req->iod, *epoch, req);
}

void
insert_single(const char *dkey, const char *akey, uint64_t idx,
	      void *value, daos_size_t size, daos_epoch_t epoch,
	      struct ioreq *req)
{
	insert(dkey, 1, &akey, &idx, &value, &size, &epoch, req);
}

static void
lookup_internal(daos_key_t *dkey, int nr, daos_sg_list_t *sgls,
		daos_iod_t *iods, daos_epoch_t epoch, struct ioreq *req)
{
	bool ev_flag;
	int rc;

	/** execute fetch operation */
	rc = daos_obj_fetch(req->oh, epoch, dkey, nr, iods, sgls,
			    NULL, req->arg->async ? &req->ev : NULL);
	if (!req->arg->async) {
		assert_int_equal(rc, req->arg->expect_result);
		return;
	}

	/** wait for fetch completion */
	rc = daos_event_test(&req->ev, DAOS_EQ_WAIT, &ev_flag);
	assert_int_equal(rc, 0);
	assert_int_equal(ev_flag, true);
	assert_int_equal(req->ev.ev_error, req->arg->expect_result);
	/* Only single iov for each sgls during the test */
	assert_int_equal(sgls->sg_nr.num_out, 1);
}

void
lookup(const char *dkey, int nr, const char **akey, uint64_t *idx,
       daos_size_t *read_size, void **val, daos_size_t *size,
       daos_epoch_t *epoch, struct ioreq *req)
{
	assert_in_range(nr, 1, IOREQ_SG_IOD_NR);

	/* dkey */
	ioreq_dkey_set(req, dkey);

	/* akey */
	ioreq_akey_set(req, akey, nr);

	/* set sgl */
	ioreq_sgl_simple_set(req, val, size, nr);

	/* set iod */
	ioreq_iod_simple_set(req, read_size, idx, epoch, nr);

	lookup_internal(&req->dkey, nr, req->sgl, req->iod, *epoch, req);
}

void
lookup_single(const char *dkey, const char *akey, uint64_t idx,
	      void *val, daos_size_t size, daos_epoch_t epoch,
	      struct ioreq *req)
{
	daos_size_t read_size = DAOS_REC_ANY;

	lookup(dkey, 1, &akey, &idx, &read_size, &val, &size, &epoch, req);
}
#if 0
/** test overwrite in different epoch */
static void
io_epoch_overwrite(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	daos_size_t	 size;
	char		 ubuf[] = "DAOS";
	char		 fbuf[] = "DAOS";
	int		 i;
	daos_epoch_t	 e = 0;

	/** choose random object */
	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	size = strlen(ubuf);

	for (i = 0; i < size; i++)
		insert_single("d", "a", i, &ubuf[i], 1, e, &req);

	for (i = 0; i < size; i++) {
		e++;
		ubuf[i] += 32;
		insert_single("d", "a", i, &ubuf[i], 1, e, &req);
	}

	memset(fbuf, 0, sizeof(fbuf));
	for (;;) {
		for (i = 0; i < size; i++)
			lookup_single("d", "a", i, &fbuf[i], 1, e, &req);
		print_message("e = %lu, fbuf = %s\n", e, fbuf);
		assert_string_equal(fbuf, ubuf);
		if (e == 0)
			break;
		e--;
		ubuf[e] -= 32;
	}

	ioreq_fini(&req);
}
#endif

/** i/o to variable idx offset */
static void
io_var_idx_offset(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	daos_off_t	 offset;
	daos_epoch_t	 epoch = g_epoch;

	oid = test_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	for (offset = UINT64_MAX; offset > 0; offset >>= 8) {
		char buf[10];

		print_message("idx offset: %lu\n", offset);

		/** Insert */
		insert_single("var_idx_off_d", "var_idx_off_a", offset, "data",
		       strlen("data") + 1, epoch, &req);

		/** Lookup */
		memset(buf, 0, 10);
		lookup_single("var_idx_off_d", "var_idx_off_a", offset,
			      buf, 10, epoch, &req);
		assert_int_equal(req.iod[0].iod_size, strlen(buf) + 1);

		/** Verify data consistency */
		assert_string_equal(buf, "data");
	}

	ioreq_fini(&req);

}

/** i/o to variable akey size */
static void
io_var_akey_size(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	daos_size_t	 size;
	const int	 max_size = 1 << 10;
	char		*key;

	/** akey not supported yet */
	oid = test_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	key = malloc(max_size + 1);
	assert_non_null(key);
	memset(key, 'a', max_size);

	for (size = 1; size <= max_size; size <<= 1) {
		char buf[10];

		print_message("akey size: %lu\n", size);

		/** Insert */
		key[size] = '\0';
		insert_single("var_akey_size_d", key, 0, "data",
			      strlen("data") + 1, g_epoch, &req);

		/** Lookup */
		memset(buf, 0, 10);
		lookup_single("var_akey_size_d", key, 0, buf,
			      10, g_epoch, &req);

		assert_int_equal(req.iod[0].iod_size, strlen("data") + 1);

		/** Verify data consistency */
		assert_string_equal(buf, "data");
		key[size] = 'b';
	}

	free(key);
	ioreq_fini(&req);
}

/** i/o to variable dkey size */
static void
io_var_dkey_size(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	daos_size_t	 size;
	const int	 max_size = 1 << 10;
	char		*key;

	oid = test_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	key = malloc(max_size + 1);
	assert_non_null(key);
	memset(key, 'a', max_size);

	for (size = 1; size <= max_size; size <<= 1) {
		char buf[10];

		print_message("dkey size: %lu\n", size);

		/** Insert */
		key[size] = '\0';
		insert_single(key, "var_dkey_size_a", 0, "data",
			      strlen("data") + 1, g_epoch, &req);

		/** Lookup */
		memset(buf, 0, 10);
		lookup_single(key, "var_dkey_size_a", 0, buf, 10,
			      g_epoch, &req);
		assert_int_equal(req.iod[0].iod_size, strlen("data") + 1);

		/** Verify data consistency */
		assert_string_equal(buf, "data");
		key[size] = 'b';
	}

	free(key);
	ioreq_fini(&req);
}

/** i/o to variable aligned record size */
static void
io_var_rec_size(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_epoch_t	 epoch;
	struct ioreq	 req;
	daos_size_t	 size;
	const int	 max_size = 1 << 22;
	char		*fetch_buf;
	char		*update_buf;

	oid = test_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	/** random epoch as well */
	epoch = g_epoch;

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	fetch_buf = malloc(max_size);
	assert_non_null(fetch_buf);

	update_buf = malloc(max_size);
	assert_non_null(update_buf);

	dts_buf_render(update_buf, max_size);

	for (size = 1; size <= max_size; size <<= 1, epoch++) {
		char dkey[30];

		print_message("Record size: %lu val: \'%c\' epoch: %lu\n",
			      size, update_buf[0], epoch);

		/** Insert */
		sprintf(dkey, DF_U64, epoch);
		insert_single(dkey, "var_rec_size_a", 0, update_buf,
			      size, g_epoch, &req);

		/** Lookup */
		memset(fetch_buf, 0, max_size);
		lookup_single(dkey, "var_rec_size_a", 0, fetch_buf,
			      max_size, g_epoch, &req);
		assert_int_equal(req.iod[0].iod_size, size);

		/** Verify data consistency */
		assert_memory_equal(update_buf, fetch_buf, size);
	}

	free(update_buf);
	free(fetch_buf);
	ioreq_fini(&req);
}

static void
io_simple_internal(void **state, daos_obj_id_t oid)
{
	test_arg_t	*arg = *state;
	struct ioreq	 req;
	const char	 dkey[] = "test_update dkey";
	const char	 akey[] = "test_update akey";
	const char	 rec[]  = "test_update record";
	char		*buf;

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert */
	print_message("Insert(e=0)/lookup(e=0)/verify simple kv record\n");

	insert_single(dkey, akey, 0, (void *)rec, strlen(rec), g_epoch, &req);

	/** Lookup */
	buf = calloc(64, 1);
	assert_non_null(buf);
	lookup_single(dkey, akey, 0, buf, 64, g_epoch, &req);

	/** Verify data consistency */
	print_message("size = %lu\n", req.iod[0].iod_size);
	assert_int_equal(req.iod[0].iod_size, strlen(rec));
	assert_memory_equal(buf, rec, strlen(rec));
	free(buf);
	ioreq_fini(&req);
}

/** very basic update/fetch with data verification */
static void
io_simple(void **state)
{
	daos_obj_id_t	 oid;

	oid = test_oid_gen(DAOS_OC_REPL_MAX_RW, ((test_arg_t *)state)->myrank);
	io_simple_internal(state, oid);
}

static void
io_named(void **state)
{
	test_arg_t   *arg = *state;
	daos_obj_id_t oid;
	struct ioreq  req;
	const char    dkey[] = "dkey-bob";
	const char    akey[] = "akey-bob";
	const char    recd[] = "yabba-dabba-dooooo";

	oid.hi  = 55551111;
	oid.mid = 1;
	oid.lo  = 0;
	daos_obj_id_generate(&oid, DAOS_OC_REPL_MAX_RW);
	fprintf(fp, "TGT:"DF_OID"\n", DP_OID(oid));

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	insert_single(dkey, akey, 0, (void *)recd, strlen(recd), g_epoch, &req);
	ioreq_fini(&req);
}

static void
io_complex(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	const char	 dkey[] = "test_update dkey";
	char		*akey[5];
	char		*rec[5];
	daos_size_t	rec_size[5];
	daos_off_t	offset[5];
	char		*val[5];
	daos_size_t	val_size[5];
	daos_epoch_t	epoch = g_epoch;
	int		i;

	oid = test_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	print_message("Insert(e=0)/lookup(e=0)/verify complex kv record\n");
	for (i = 0; i < 5; i++) {
		akey[i] = calloc(20, 1);
		assert_non_null(akey[i]);
		sprintf(akey[i], "test_update akey%d", i);
		rec[i] = calloc(20, 1);
		assert_non_null(rec[i]);
		sprintf(rec[i], "test_update val%d", i);
		rec_size[i] = strlen(rec[i]);
		offset[i] = i * 20;
		val[i] = calloc(64, 1);
		assert_non_null(val[i]);
		val_size[i] = 64;
	}

	/** Insert */
	insert(dkey, 5, (const char **)akey, offset, (void **)rec,
	       rec_size, &epoch, &req);

	/** Lookup */
	lookup(dkey, 5, (const char **)akey, offset, rec_size,
	       (void **)val, val_size, &epoch, &req);

	/** Verify data consistency */
	for (i = 0; i < 5; i++) {
		print_message("size = %lu\n", req.iod[i].iod_size);
		assert_int_equal(req.iod[i].iod_size, strlen(rec[i]));
		assert_memory_equal(val[i], rec[i], strlen(rec[i]));
		free(val[i]);
		free(akey[i]);
		free(rec[i]);
	}
	ioreq_fini(&req);
}

#define STACK_BUF_LEN	24

static void
basic_byte_array(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	daos_epoch_t	 epoch = g_epoch;
	daos_iov_t	 dkey;
	daos_sg_list_t	 sgl;
	daos_iov_t	 sg_iov;
	daos_iod_t	 iod;
	daos_recx_t	 recx;
	char		 buf_out[STACK_BUF_LEN];
	char		 buf[STACK_BUF_LEN];
	int		 rc;

	dts_buf_render(buf, STACK_BUF_LEN);

	/** open object */
	oid = test_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	D_DEBUG(DF_MISC, "BASIC_BYTE_ARRAY open oid="DF_OID"\n", DP_OID(oid));
	rc = daos_obj_open(arg->coh, oid, 0, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	/** init dkey */
	daos_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	daos_iov_set(&sg_iov, buf, sizeof(buf));
	sgl.sg_nr.num		= 1;
	sgl.sg_nr.num_out	= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	daos_iov_set(&iod.iod_name, "akey", strlen("akey"));
	daos_csum_set(&iod.iod_kcsum, NULL, 0);
	recx.rx_idx = 0;
	recx.rx_nr  = STACK_BUF_LEN;

	iod.iod_size	= 1;
	iod.iod_nr	= 1;
	iod.iod_recxs	= &recx;
	iod.iod_eprs	= NULL;
	iod.iod_csums	= NULL;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** update record */
	print_message("writing %d bytes with one recx per byte\n",
		      STACK_BUF_LEN);
	D_DEBUG(DF_MISC, "BYTE_ARRAY_UPDATE - %d recxs\n", STACK_BUF_LEN);
	rc = daos_obj_update(oh, epoch, &dkey, 1, &iod, &sgl, NULL);
	assert_int_equal(rc, 0);

	/** fetch */
	print_message("reading data back ...\n");
	memset(buf_out, 0, sizeof(buf_out));
	daos_iov_set(&sg_iov, buf_out, sizeof(buf_out));
	rc = daos_obj_fetch(oh, epoch, &dkey, 1, &iod, &sgl, NULL, NULL);
	assert_int_equal(rc, 0);
	/** Verify data consistency */
	print_message("validating data ...\n");
	assert_memory_equal(buf, buf_out, sizeof(buf));

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_int_equal(rc, 0);
	print_message("all good\n");
}


int
arg_setup(test_arg_t *arg, unsigned int step, bool multi_rank)
{
	int		 rc;

	memset(arg, 0, sizeof(*arg));

	MPI_Comm_rank(MPI_COMM_WORLD, &arg->myrank);
	MPI_Comm_size(MPI_COMM_WORLD, &arg->rank_size);
	arg->multi_rank = multi_rank;

	arg->svc.rl_nr.num = 1;
	arg->svc.rl_nr.num_out = 0;
	arg->svc.rl_ranks = arg->ranks;

	arg->mode = 0731;
	arg->uid = geteuid();
	arg->gid = getegid();

	arg->group = g_group_id;
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
		rc = daos_pool_create(0731, geteuid(), getegid(), arg->group,
				      NULL, "pmem", 1024*1024*1024, &arg->svc,
				      arg->pool_uuid, NULL);
		if (rc)
			print_message("daos_pool_create failed, rc: %d\n", rc);
	}
	/** broadcast pool create result */
	if (multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** broadcast pool UUID */
	if (multi_rank)
		MPI_Bcast(arg->pool_uuid, 16, MPI_CHAR, 0, MPI_COMM_WORLD);

	if (step == SETUP_POOL_CREATE)
		goto out;

	/** connect to pool */
	if (arg->myrank == 0) {
		rc = daos_pool_connect(arg->pool_uuid, arg->group, &arg->svc,
				       DAOS_PC_RW, &arg->poh, &arg->pool_info,
				       NULL /* ev */);
		if (rc)
			print_message("daos_pool_connect failed, rc: %d\n", rc);
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
	return 0;
}

void
next_epoch(daos_handle_t coh, daos_epoch_t *epoch)
{
	daos_epoch_state_t estate;
	daos_epoch_t n = *epoch;
	int rc;

	D_DEBUG(DF_MISC, "************* DS POPULATE - EP COMMIT **********\n");
	rc = daos_epoch_commit(coh, n, &estate, NULL);
	if (rc != 0) {
		print_message("daos_epoch_commit returned %d\n", rc);
		return;
	}
	print_message("epoch commit: epoch:"DF_U64"\n", n);
#if 0
	print_message("  hce: "DF_U64"\n", estate.es_hce);
	print_message("  lre: "DF_U64"\n", estate.es_lre);
	print_message("  lhe: "DF_U64"\n", estate.es_lhe);
	print_message("  ghce: "DF_U64"\n", estate.es_ghce);
	print_message("  glre: "DF_U64"\n", estate.es_glre);
	print_message("  ghpce: "DF_U64"\n", estate.es_ghpce);
#endif
	n = 0;
	rc = daos_epoch_hold(coh, &n, &estate, NULL);
	if (rc != 0) {
		print_message("daos_epoch_hold for "DF_U64" returned %d\n",
			      n, rc);
		return;
	}
	print_message("epoch hold: epoch:"DF_U64"\n", n);
#if 0
	print_message("  hce: "DF_U64"\n", estate.es_hce);
	print_message("  lre: "DF_U64"\n", estate.es_lre);
	print_message("  lhe: "DF_U64"\n", estate.es_lhe);
	print_message("  ghce: "DF_U64"\n", estate.es_ghce);
	print_message("  glre: "DF_U64"\n", estate.es_glre);
	print_message("  ghpce: "DF_U64"\n", estate.es_ghpce);
#endif
	*epoch = n;
	g_epoch = n;
}


int
main(int argc, char **argv)
{
	int		   rank;
	int		   size;
	int		   rc;
	test_arg_t	   arg;
	test_arg_t         *argp = &arg;
	daos_epoch_t       epoch;
	daos_epoch_state_t estate;


	if (argc != 2) {
		print_message("Incorrect number of args. %s\n", USAGE);
		return -1;
	}
	fp = fopen("cold_tier.info", "w");
	if (!fp) {
		print_message("could not open cold_tier.info\n");
		return -1;
	}
	g_group_id = malloc(strlen(argv[1] + 1));
	strcpy(g_group_id, argv[1]);
	fprintf(fp, "Group Name:%s\n", g_group_id);

	print_message("Cold-Tier Group: %s\n", g_group_id);

	MPI_Init(&argc, &argv);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Barrier(MPI_COMM_WORLD);

	cmocka_set_message_output(CM_OUTPUT_STDOUT);
	rc = daos_init();

	if (rc) {
		print_message("daos_init() failed with %d\n", rc);
		return -1;
	}
	D_DEBUG(DF_MISC, "************* DS POPULATE - STARTING **********\n");
	rc = arg_setup(&arg, SETUP_CONT_CONNECT, false);
	if (rc)
		goto cont_cl;

	fprintf(fp, "Pool UUID:"DF_UUIDF"\n", DP_UUID(arg.pool_uuid));
	fprintf(fp, "Cont UUID:"DF_UUIDF"\n", DP_UUID(arg.co_uuid));
	print_message("Opened container\n");
	print_message("container info:\n");
	print_message("  hce: "DF_U64"\n", arg.co_info.ci_epoch_state.es_hce);
	print_message("  lre: "DF_U64"\n", arg.co_info.ci_epoch_state.es_lre);
	print_message("  lhe: "DF_U64"\n", arg.co_info.ci_epoch_state.es_lhe);
	print_message("  ghce: "DF_U64"\n",
		      arg.co_info.ci_epoch_state.es_ghce);
	print_message("  glre: "DF_U64"\n",
		      arg.co_info.ci_epoch_state.es_glre);
	print_message("  ghpce: "DF_U64"\n",
		      arg.co_info.ci_epoch_state.es_ghpce);
	arg.async = false;

	g_epoch = 0;

	D_DEBUG(DF_MISC, "************* DS POPULATE - EP HOLD **********\n");
	epoch = g_epoch;
	rc = daos_epoch_hold(arg.coh, &epoch, &estate, NULL);
	if (rc != 0) {
		print_message("daos_epoch_hold for "DF_U64" returned %d\n",
			      g_epoch, rc);
		goto cont_cl;
	}
	print_message("epoch hold: epoch:"DF_U64"\n", epoch);
	print_message("  hce: "DF_U64"\n", estate.es_hce);
	print_message("  lre: "DF_U64"\n", estate.es_lre);
	print_message("  lhe: "DF_U64"\n", estate.es_lhe);
	print_message("  ghce: "DF_U64"\n", estate.es_ghce);
	print_message("  glre: "DF_U64"\n", estate.es_glre);
	print_message("  ghpce: "DF_U64"\n", estate.es_ghpce);
	g_epoch = epoch;

	print_message("io_simple #1\n");

	D_DEBUG(DF_MISC, "************* DS POPULATE - T1/9 **********\n");
	io_simple((void **)&argp);

	io_named((void **)&argp);

	next_epoch(arg.coh, &epoch);

	D_DEBUG(DF_MISC, "************* DS POPULATE - T2/9 **********\n");
	print_message("io_simple #2\n");
	arg.async = true;
	io_simple((void **)&argp);

	next_epoch(arg.coh, &epoch);

	D_DEBUG(DF_MISC, "************* DS POPULATE - T3/9 **********\n");
	print_message("io_var_rec_size #1\n");
	arg.async = false;
	io_var_rec_size((void **)&argp);

	next_epoch(arg.coh, &epoch);

	D_DEBUG(DF_MISC, "************* DS POPULATE - T4/9 **********\n");
	print_message("io_var_rec_size #2\n");
	arg.async = true;
	io_var_rec_size((void **)&argp);

	next_epoch(arg.coh, &epoch);

	D_DEBUG(DF_MISC, "************* DS POPULATE - T5/9 **********\n");
	print_message("io_var_dkey_size\n");
	io_var_dkey_size((void **)&argp);

	next_epoch(arg.coh, &epoch);

	D_DEBUG(DF_MISC, "************* DS POPULATE - T6/9 **********\n");
	print_message("io_var_akey_size\n");
	arg.async = false;
	io_var_akey_size((void **)&argp);

	next_epoch(arg.coh, &epoch);

	D_DEBUG(DF_MISC, "************* DS POPULATE - T7/9 **********\n");
	print_message("io_var_idx_offset\n");
	arg.async = true;
	io_var_idx_offset((void **)&argp);

	next_epoch(arg.coh, &epoch);

	D_DEBUG(DF_MISC, "************* DS POPULATE - T8/9 **********\n");
	arg.async = false;
	io_complex((void **)&argp);

	next_epoch(arg.coh, &epoch);

	D_DEBUG(DF_MISC, "************* DS POPULATE - T9/9 **********\n");
	print_message("basic_byte_array\n");
	basic_byte_array((void **)&argp);

	print_message("flushing epoch\n");
	rc = daos_epoch_flush(arg.coh, epoch, NULL, NULL);
	print_message("committing epoch "DF_U64"\n", epoch);
	D_DEBUG(DF_MISC, "************* DS POPULATE - EP COMMIT **********\n");
	rc = daos_epoch_commit(arg.coh, epoch, &estate, NULL);
	if (rc != 0)
		print_message("daos_epoch_commit returned %d\n", rc);

	print_message("epoch commit: epoch:"DF_U64"\n", epoch);
	print_message("\nCOLD TIER POPULATED, disconnecting from pool\n\n");

cont_cl:
	D_DEBUG(DF_MISC, "************* DS POPULATE - CT CLOSE **********\n");
	rc = daos_cont_close(arg.coh, NULL);
	if (rc)
		print_message("Container close failed: %d\n", rc);

	D_DEBUG(DF_MISC, "************* DS POPULATE - POOL DISC **********\n");
	rc = daos_pool_disconnect(arg.poh, NULL);
	if (rc)
		print_message("Pool disconnect failed: %d\n", rc);


	rc = daos_fini();
	if (rc)
		print_message("daos_fini() failed with %d\n", rc);

	MPI_Finalize();
	free(g_group_id);

	return 0;
}
