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
#define D_LOGFAC       DD_FAC(tests)

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <mpi.h>
#include <abt.h>
#include <daos/common.h>
#include <daos/tests_lib.h>
#include <daos_srv/vos.h>
#include <daos_test.h>
#include <daos/dts.h>

/* unused object class to identify VOS (storage only) test mode */
#define DAOS_OC_RAW	(0xBEE)
#define RANK_ZERO	(0)
#define TEST_VAL_SIZE	(3)

enum ts_op_type {
	TS_DO_UPDATE = 0,
	TS_DO_FETCH
};

enum {
	TS_MODE_VOS,  /* pure storage */
	TS_MODE_ECHO, /* pure network */
	TS_MODE_DAOS, /* full stack */
};

int			 ts_mode = TS_MODE_VOS;
int			 ts_class = DAOS_OC_RAW;

char			 ts_pmem_file[PATH_MAX];

unsigned int		 ts_obj_p_cont	= 1;	/* # objects per container */
unsigned int		 ts_dkey_p_obj	= 1;	/* # dkeys per object */
unsigned int		 ts_akey_p_dkey	= 100;	/* # akeys per dkey */
unsigned int		 ts_recx_p_akey	= 1000;	/* # recxs per akey */
unsigned int		 ts_vsize	= 64;	/* default value size */
unsigned int		 ts_seed;
/* value type: single or array */
bool			 ts_single	= true;
/* always overwrite value of an akey */
bool			 ts_overwrite;
/* use zero-copy API for VOS, ignored for "echo" or "daos" */
bool			 ts_zero_copy;
/* shuffle the offsets of the array */
bool			 ts_shuffle	= false;
bool			 ts_pause	= false;

bool			 ts_oid_init	= false;

daos_handle_t		*ts_ohs;		/* all opened objects */
daos_obj_id_t		*ts_oids;		/* object IDs */
daos_unit_oid_t		*ts_uoids;		/* object shard IDs (for VOS) */

struct dts_context	 ts_ctx;
bool			 ts_nest_iterator;

/* rebuild only with iteration */
bool			ts_rebuild_only_iteration = false;
/* rebuild without update */
bool			ts_rebuild_no_update = false;
/* test inside ULT */
bool			ts_in_ult;
bool			ts_profile_vos;
char			*ts_profile_vos_path = ".";
int			ts_profile_vos_avg = 100;
static ABT_xstream	abt_xstream;

#define PF_DKEY_PREF	"blade"
#define PF_AKEY_PREF	"walker"

struct pf_param {
	/* output performance */
	bool		pa_perf;
	/* no key reset */
	bool		pa_no_reset;
	/* output parameter */
	double		pa_duration;
	union {
		/* private paramter for rebuild */
		struct {
			bool scan;
			bool pull;
		} pa_rebuild;
		/* private parameter for iteration */
		struct {
			bool nested;
		} pa_iter;
	};
};

struct pf_test {
	/* identifier of test */
	char	  ts_code;
	char	 *ts_name;
	int	(*ts_parse)(char *str, struct pf_param *param, char **strpp);
	int	(*ts_func)(struct pf_test *ts, struct pf_param *param);
};

typedef void (*pf_parse_cb_t)(char *, struct pf_param *, char **);

static void ts_print_usage(void);
static void show_result(double duration, uint64_t start, uint64_t end,
			char *test_name);

int
ts_abt_init(void)
{
	int cpuid;
	int num_cpus;
	int rc;

	rc = ABT_init(0, NULL);
	if (rc != ABT_SUCCESS) {
		fprintf(stderr, "ABT init failed: %d\n", rc);
		return -1;
	}

	rc = ABT_xstream_self(&abt_xstream);
	if (rc != ABT_SUCCESS) {
		printf("ABT get self xstream failed: %d\n", rc);
		return -1;
	}

	rc = ABT_xstream_get_cpubind(abt_xstream, &cpuid);
	if (rc != ABT_SUCCESS) {
		fprintf(stderr, "get cpubind failed: %d\n", rc);
		fprintf(stderr, "No CPU affinity for this test.\n");
		fprintf(stderr, "Build ABT by --enable-affinity if"
			" you want to try CPU affinity.\n");
		return 0;
	}

	rc = ABT_xstream_get_affinity(abt_xstream, 0, NULL,
				      &num_cpus);
	if (rc != ABT_SUCCESS) {
		fprintf(stderr, "get num_cpus: %d\n", rc);
		fprintf(stderr, "No CPU affinity for this test.\n");
		fprintf(stderr, "Build ABT by --enable-affinity if"
			" you want to try CPU affinity.\n");
		return 0;
	}

	cpuid = (cpuid + 1) % num_cpus;
	rc = ABT_xstream_set_cpubind(abt_xstream, cpuid);
	if (rc != ABT_SUCCESS) {
		fprintf(stderr, "set affinity: %d\n", rc);
		fprintf(stderr, "No CPU affinity for this test.\n");
		fprintf(stderr, "Build ABT by --enable-affinity if"
			" you want to try CPU affinity.\n");
		return 0;
	}

	return 0;
}

void
ts_abt_fini(void)
{
	ABT_xstream_join(abt_xstream);
	ABT_xstream_free(&abt_xstream);
	ABT_finalize();
}

#define TS_TIME_START(time, start)		\
do {						\
	if (time == NULL)			\
		break;				\
	start = daos_get_ntime();		\
} while (0)

#define TS_TIME_END(time, start)		\
do {						\
	if ((time) == NULL)			\
		break;				\
	*time += (daos_get_ntime() - start)/1000;\
} while (0)

static int
_vos_update_or_fetch(int obj_idx, enum ts_op_type op_type,
		     struct dts_io_credit *cred, daos_epoch_t epoch,
		     double *duration)
{
	uint64_t	start = 0;
	int		rc = 0;

	TS_TIME_START(duration, start);
	if (!ts_zero_copy) {
		if (op_type == TS_DO_UPDATE)
			rc = vos_obj_update(ts_ctx.tsc_coh, ts_uoids[obj_idx],
					    epoch, 0, 0, &cred->tc_dkey, 1,
					    &cred->tc_iod, NULL, &cred->tc_sgl);
		else
			rc = vos_obj_fetch(ts_ctx.tsc_coh, ts_uoids[obj_idx],
					   epoch, 0, &cred->tc_dkey, 1,
					   &cred->tc_iod, &cred->tc_sgl);
	} else { /* zero-copy */
		struct bio_sglist	*bsgl;
		daos_handle_t		 ioh;

		if (op_type == TS_DO_UPDATE)
			rc = vos_update_begin(ts_ctx.tsc_coh, ts_uoids[obj_idx],
					      epoch, 0, &cred->tc_dkey, 1,
					      &cred->tc_iod, NULL, false, 0,
					      &ioh, NULL);
		else
			rc = vos_fetch_begin(ts_ctx.tsc_coh, ts_uoids[obj_idx],
					     epoch, &cred->tc_dkey, 1,
					     &cred->tc_iod, 0, NULL, &ioh,
					     NULL);
		if (rc)
			return rc;

		rc = bio_iod_prep(vos_ioh2desc(ioh));
		if (rc)
			goto end;

		bsgl = vos_iod_sgl_at(ioh, 0);
		D_ASSERT(bsgl != NULL);
		D_ASSERT(bsgl->bs_nr_out == 1);
		D_ASSERT(cred->tc_sgl.sg_nr == 1);

		if (op_type == TS_DO_FETCH) {
			memcpy(cred->tc_sgl.sg_iovs[0].iov_buf,
			       bio_iov2raw_buf(&bsgl->bs_iovs[0]),
			       bio_iov2raw_len(&bsgl->bs_iovs[0]));
		} else {
			memcpy(bio_iov2req_buf(&bsgl->bs_iovs[0]),
			       cred->tc_sgl.sg_iovs[0].iov_buf,
			       cred->tc_sgl.sg_iovs[0].iov_len);
		}

		rc = bio_iod_post(vos_ioh2desc(ioh));
end:
		if (op_type == TS_DO_UPDATE)
			rc = vos_update_end(ioh, 0, &cred->tc_dkey, rc, NULL);
		else
			rc = vos_fetch_end(ioh, rc);
	}

	TS_TIME_END(duration, start);
	return rc;
}

struct vos_ult_arg {
	struct dts_io_credit	*cred;
	double			*duration;
	daos_epoch_t		 epoch;
	enum ts_op_type		 op_type;
	int			 obj_idx;
	int			 status;
};

static void
vos_update_or_fetch_ult(void *arg)
{
	struct vos_ult_arg *ult_arg = arg;

	ult_arg->status = _vos_update_or_fetch(ult_arg->obj_idx,
				ult_arg->op_type, ult_arg->cred,
				ult_arg->epoch, ult_arg->duration);
}

static int
vos_update_or_fetch(int obj_idx, enum ts_op_type op_type,
		    struct dts_io_credit *cred, daos_epoch_t epoch,
		    double *duration)
{
	ABT_thread		thread;
	struct vos_ult_arg	ult_arg;
	int			rc;

	if (!ts_in_ult)
		return _vos_update_or_fetch(obj_idx, op_type, cred, epoch,
					    duration);

	ult_arg.op_type = op_type;
	ult_arg.cred = cred;
	ult_arg.epoch = epoch;
	ult_arg.duration = duration;
	ult_arg.obj_idx = obj_idx;
	rc = ABT_thread_create_on_xstream(abt_xstream, vos_update_or_fetch_ult,
					  &ult_arg, ABT_THREAD_ATTR_NULL,
					  &thread);
	if (rc != ABT_SUCCESS)
		return rc;

	rc = ABT_thread_join(thread);
	if (rc != ABT_SUCCESS)
		return rc;

	ABT_thread_free(&thread);
	rc = ult_arg.status;
	return rc;
}

static int
daos_update_or_fetch(int obj_idx, enum ts_op_type op_type,
		     struct dts_io_credit *cred, daos_epoch_t epoch,
		     bool verify, double *duration)
{
	int	rc;
	uint64_t start = 0;

	if (!dts_is_async(&ts_ctx))
		TS_TIME_START(duration, start);
	if (op_type == TS_DO_UPDATE) {
		rc = daos_obj_update(ts_ohs[obj_idx], DAOS_TX_NONE, 0,
				     &cred->tc_dkey, 1, &cred->tc_iod,
				     &cred->tc_sgl, cred->tc_evp);
	} else {
		rc = daos_obj_fetch(ts_ohs[obj_idx], DAOS_TX_NONE, 0,
				    &cred->tc_dkey, 1, &cred->tc_iod,
				    &cred->tc_sgl, NULL,
				    verify ? NULL : cred->tc_evp);
	}

	if (!dts_is_async(&ts_ctx))
		TS_TIME_END(duration, start);

	return rc;
}

static int
set_check_buffer(char *buf, int size, bool check)
{
	int	i;
	int	j;

	for (i = 0, j = 1; j < size; j = (1 << ++i)) {
		char	val = 'A' + i % 26;

		if (check) {
			if (buf[j] == val)
				continue;

			fprintf(stderr, "buf[%d] %c != %c\n", j, buf[j], val);
			return -1;
		} else {
			buf[j] = val;
		}
	}
	return 0;
}

static int
akey_update_or_fetch(int obj_idx, enum ts_op_type op_type,
		     char *dkey, char *akey, daos_epoch_t *epoch,
		     uint64_t *indices, int idx, bool verify,
		     double *duration)
{
	struct dts_io_credit *cred;
	daos_iod_t	     *iod;
	d_sg_list_t	     *sgl;
	daos_recx_t	     *recx;
	int		      rc = 0;

	cred = dts_credit_take(&ts_ctx);
	if (!cred) {
		fprintf(stderr, "credit cannot be NULL for IO\n");
		rc = -1;
		return rc;
	}

	iod  = &cred->tc_iod;
	sgl  = &cred->tc_sgl;
	recx = &cred->tc_recx;

	memset(iod, 0, sizeof(*iod));
	memset(sgl, 0, sizeof(*sgl));
	memset(recx, 0, sizeof(*recx));

	/* setup dkey */
	memcpy(cred->tc_dbuf, dkey, DTS_KEY_LEN);
	d_iov_set(&cred->tc_dkey, cred->tc_dbuf,
			strlen(cred->tc_dbuf));

	/* setup I/O descriptor */
	memcpy(cred->tc_abuf, akey, DTS_KEY_LEN);
	d_iov_set(&iod->iod_name, cred->tc_abuf,
			strlen(cred->tc_abuf));
	iod->iod_size = ts_vsize;
	recx->rx_nr  = 1;
	if (ts_single) {
		iod->iod_type = DAOS_IOD_SINGLE;
	} else {
		iod->iod_type = DAOS_IOD_ARRAY;
		iod->iod_size = 1;
		recx->rx_nr  = ts_vsize;
		recx->rx_idx = ts_overwrite ? 0 : indices[idx] * ts_vsize;
	}

	iod->iod_nr    = 1;
	iod->iod_recxs = recx;

	if (op_type == TS_DO_UPDATE) {
		/* initialize value buffer and setup sgl */
		set_check_buffer(cred->tc_vbuf, ts_vsize, false);
		verify = false;
	} else {
		if (verify) /* Clear the buffer for verify */
			memset(cred->tc_vbuf, 0, ts_vsize);
	}

	d_iov_set(&cred->tc_val, cred->tc_vbuf, ts_vsize);
	sgl->sg_iovs = &cred->tc_val;
	sgl->sg_nr = 1;

	if (ts_mode == TS_MODE_VOS)
		rc = vos_update_or_fetch(obj_idx, op_type, cred, *epoch,
					 duration);
	else
		rc = daos_update_or_fetch(obj_idx, op_type, cred, *epoch,
					  verify, duration);

	if (rc != 0) {
		fprintf(stderr, "%s failed. rc=%d, epoch=%"PRIu64"\n",
			op_type == TS_DO_FETCH ? "Fetch" : "Update",
			rc, *epoch);
		return rc;
	}

	/* overwrite can replace original data and reduce space
	 * consumption.
	 */
	if (!ts_overwrite)
		(*epoch)++;

	if (verify) {
		rc = set_check_buffer(cred->tc_vbuf, ts_vsize, true);
		dts_credit_return(&ts_ctx, cred);
		return rc;
	}
	return 0;
}

static int
dkey_update_or_fetch(enum ts_op_type op_type, char *dkey, daos_epoch_t *epoch,
		     bool verify, double *duration)
{
	uint64_t	*indices;
	char		 akey[DTS_KEY_LEN];
	int		 i;
	int		 j;
	int		 k;
	int		 rc = 0;

	indices = dts_rand_iarr_alloc_set(ts_recx_p_akey, 0, ts_shuffle);
	D_ASSERT(indices != NULL);

	for (i = 0; i < ts_akey_p_dkey; i++) {
		dts_key_gen(akey, DTS_KEY_LEN, PF_AKEY_PREF);
		for (j = 0; j < ts_recx_p_akey; j++) {
			for (k = 0; k < ts_obj_p_cont; k++) {
				rc = akey_update_or_fetch(k, op_type, dkey,
						akey, epoch, indices, j, verify,
						duration);
				if (rc)
					goto failed;
			}
		}
	}

failed:
	D_FREE(indices);
	return rc;
}

static int
objects_open(void)
{
	int	i;
	int	rc;

	for (i = 0; i < ts_obj_p_cont; i++) {
		if (!ts_oid_init) {
			ts_oids[i] = dts_oid_gen(ts_class, 0,
						 ts_ctx.tsc_mpi_rank);
			if (ts_class == DAOS_OC_R2S_SPEC_RANK)
				ts_oids[i] = dts_oid_set_rank(ts_oids[i],
							      RANK_ZERO);
		}

		if (ts_mode == TS_MODE_DAOS || ts_mode == TS_MODE_ECHO) {
			rc = daos_obj_open(ts_ctx.tsc_coh, ts_oids[i],
					   DAOS_OO_RW, &ts_ohs[i], NULL);
			if (rc) {
				fprintf(stderr, "object open failed\n");
				return -1;
			}
		} else {
			memset(&ts_uoids[i], 0, sizeof(*ts_uoids));
			ts_uoids[i].id_pub = ts_oids[i];
		}
	}
	ts_oid_init = true;
	return 0;
}

static int
objects_close(void)
{
	int i;
	int rc = 0;

	if (ts_mode == TS_MODE_VOS || !ts_oid_init)
		return 0; /* nothing to do */

	for (i = 0; i < ts_obj_p_cont; i++) {
		rc = daos_obj_close(ts_ohs[i], NULL);
		D_ASSERT(rc == 0);
	}
	return 0;
}

static int
objects_update(double *duration)
{
	int		i;
	int		rc;
	uint64_t	start = 0;
	daos_epoch_t	epoch = 1;

	if (!ts_overwrite)
		++epoch;

	if (dts_is_async(&ts_ctx))
		TS_TIME_START(duration, start);

	for (i = 0; i < ts_dkey_p_obj; i++) {
		char	 dkey[DTS_KEY_LEN];

		dts_key_gen(dkey, DTS_KEY_LEN, PF_DKEY_PREF);
		rc = dkey_update_or_fetch(TS_DO_UPDATE, dkey, &epoch,
					  false, duration);
		if (rc)
			return rc;
	}

	rc = dts_credit_drain(&ts_ctx);
	if (dts_is_async(&ts_ctx))
		TS_TIME_END(duration, start);

	return rc;
}

static int
objects_fetch(double *duration, bool verify)
{
	int		i;
	int		rc = 0;
	uint64_t	start = 0;
	daos_epoch_t	epoch = crt_hlc_get();

	if (!ts_overwrite)
		epoch = crt_hlc_get();

	if (dts_is_async(&ts_ctx))
		TS_TIME_START(duration, start);

	for (i = 0; i < ts_dkey_p_obj; i++) {
		char	 dkey[DTS_KEY_LEN];

		dts_key_gen(dkey, DTS_KEY_LEN, PF_DKEY_PREF);
		rc = dkey_update_or_fetch(TS_DO_FETCH, dkey, &epoch,
					  verify, duration);
		if (rc != 0)
			return rc;
	}
	rc = dts_credit_drain(&ts_ctx);

	if (dts_is_async(&ts_ctx))
		TS_TIME_END(duration, start);
	return rc;
}

typedef int (*iterate_cb_t)(daos_handle_t ih, vos_iter_entry_t *key_ent,
			    vos_iter_param_t *param);

static int
ts_iterate_internal(uint32_t type, vos_iter_param_t *param,
		    iterate_cb_t iter_cb)
{
	daos_anchor_t		*probe_hash = NULL;
	vos_iter_entry_t	key_ent;
	daos_handle_t		ih;
	int			rc;

	rc = vos_iter_prepare(type, param, &ih, NULL);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			rc = 0;
		else
			D_ERROR("Failed to prepare d-key iterator: "DF_RC"\n",
				DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = vos_iter_probe(ih, probe_hash);
	if (rc != 0) {
		if (rc == -DER_NONEXIST || rc == -DER_AGAIN)
			rc = 0;
		D_GOTO(out_iter_fini, rc);
	}

	while (1) {
		rc = vos_iter_fetch(ih, &key_ent, NULL);
		if (rc != 0)
			break;

		/* fill the key to iov if there are enough space */
		if (iter_cb) {
			rc = iter_cb(ih, &key_ent, param);
			if (rc != 0)
				break;
		}

		rc = vos_iter_next(ih);
		if (rc)
			break;
	}

	if (rc == -DER_NONEXIST)
		rc = 0;

out_iter_fini:
	vos_iter_finish(ih);
out:
	return rc;
}

static int
iter_akey_cb(daos_handle_t ih, vos_iter_entry_t *key_ent,
	     vos_iter_param_t *param)
{
	int	rc;

	param->ip_akey = key_ent->ie_key;
	/* iterate array record */
	if (ts_nest_iterator)
		param->ip_ih = ih;
	rc = ts_iterate_internal(VOS_ITER_RECX, param, NULL);

	ts_iterate_internal(VOS_ITER_SINGLE, param, NULL);

	return rc;
}

static int
iter_dkey_cb(daos_handle_t ih, vos_iter_entry_t *key_ent,
	     vos_iter_param_t *param)
{
	int	rc;

	param->ip_dkey = key_ent->ie_key;
	if (ts_nest_iterator)
		param->ip_ih = ih;
	/* iterate akey */
	rc = ts_iterate_internal(VOS_ITER_AKEY, param, iter_akey_cb);

	return rc;
}

/* Iterate all of dkey/akey/record */
static int
iterate_records(double *duration)
{
	vos_iter_param_t	param = {};
	int			rc = 0;
	uint64_t		start = 0;

	assert_int_equal(ts_class, DAOS_OC_RAW);

	/* prepare iterate parameters */
	param.ip_hdl = ts_ctx.tsc_coh;
	param.ip_oid = ts_uoids[0];

	param.ip_epr.epr_lo = 0;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	param.ip_epc_expr = VOS_IT_EPC_RE;

	TS_TIME_START(duration, start);
	rc = ts_iterate_internal(VOS_ITER_DKEY, &param, iter_dkey_cb);
	TS_TIME_END(duration, start);
	return rc;
}

static int
pf_update(struct pf_test *ts, struct pf_param *param)
{
	int	rc;

	rc = objects_open();
	if (rc)
		return rc;

	rc = objects_update(&param->pa_duration);
	if (rc)
		return rc;

	rc = objects_close();
	return rc;
}

static int
pf_fetch(struct pf_test *ts, struct pf_param *param)
{
	int	rc;

	rc = objects_open();
	if (rc)
		return rc;

	rc = objects_fetch(&param->pa_duration, false);
	if (rc)
		return rc;

	rc = objects_close();
	return rc;
}

static int
pf_verify(struct pf_test *ts, struct pf_param *param)
{
	int	rc;

	if (ts_overwrite || (ts_single && ts_recx_p_akey > 1)) {
		fprintf(stdout, "Verification is unsupported\n");
		return 0;
	}

	rc = objects_open();
	if (rc)
		return rc;

	rc = objects_fetch(&param->pa_duration, true);
	if (rc)
		return rc;

	rc = objects_close();
	return rc;
}


static int
pf_iterate(struct pf_test *pf, struct pf_param *param)
{
	if (ts_mode != TS_MODE_VOS) {
		fprintf(stderr, "iterator can only run with -T \"vos\"\n");
		if (ts_ctx.tsc_mpi_rank == 0)
			ts_print_usage();
		return -1;
	}
	return iterate_records(&param->pa_duration);
}

static int
exclude_server(d_rank_t rank)
{
	struct d_tgt_list	targets;
	int			tgt = -1;
	int			rc;

	/** exclude from the pool */
	targets.tl_nr = 1;
	targets.tl_ranks = &rank;
	targets.tl_tgts = &tgt;
	rc = daos_pool_tgt_exclude(ts_ctx.tsc_pool_uuid, NULL, NULL /* svc */,
				   &targets, NULL);

	return rc;
}

static int
reint_server(d_rank_t rank)
{
	struct d_tgt_list	targets;
	int			tgt = -1;
	int			rc;

	/** exclude from the pool */
	targets.tl_nr = 1;
	targets.tl_ranks = &rank;
	targets.tl_tgts = &tgt;
	rc = daos_pool_reint_tgt(ts_ctx.tsc_pool_uuid, NULL, NULL /* svc */,
				 &targets, NULL);
	return rc;
}

static void
wait_rebuild(double *duration)
{
	daos_pool_info_t	   pinfo;
	struct daos_rebuild_status *rst = &pinfo.pi_rebuild_st;
	int			   rc = 0;
	uint64_t		   start = 0;

	TS_TIME_START(duration, start);
	while (1) {
		memset(&pinfo, 0, sizeof(pinfo));
		pinfo.pi_bits = DPI_REBUILD_STATUS;
		rc = daos_pool_query(ts_ctx.tsc_poh, NULL, &pinfo, NULL, NULL);
		if (rst->rs_done || rc != 0) {
			fprintf(stderr, "Rebuild (ver=%d) is done %d/%d\n",
				rst->rs_version, rc, rst->rs_errno);
			break;
		}
		sleep(2);
	}
	TS_TIME_END(duration, start);
}

static int
pf_rebuild(struct pf_test *ts, struct pf_param *param)
{
	int rc;

	if (ts_mode != TS_MODE_DAOS) {
		fprintf(stderr, "Can only run in DAOS full stack mode\n");
		return -1;
	}

	if (ts_class != DAOS_OC_R2S_SPEC_RANK) {
		fprintf(stderr, "Please choose R2S_SPEC_RANK\n");
		return -1;
	}

	if (param->pa_rebuild.scan) {
		daos_mgmt_set_params(NULL, -1, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_NO_REBUILD,
				     0, NULL);
	} else if (param->pa_rebuild.pull) {
		daos_mgmt_set_params(NULL, -1, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_NO_UPDATE,
				     0, NULL);
	}

	rc = exclude_server(RANK_ZERO);
	if (rc)
		return rc;

	wait_rebuild(&param->pa_duration);

	rc = reint_server(RANK_ZERO);
	if (rc)
		return rc;

	daos_mgmt_set_params(NULL, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
	return rc;
}

/* Test command Format: "C;p=x;q D;a;b"
 *
 * The upper-case charactor is command, e.g. U=update, F=fetch, anything after
 * semicolon is parameter of the command. Space or tab is the separator between
 * commands.
 */

#define PARAM_SEP	';'
#define PARAM_ASSIGN	'='

static int
pf_parse_common(char *str, struct pf_param *param, pf_parse_cb_t parse_cb,
	        char **strp)
{
	bool skip = false;

	/* parse parameters and execute the function. */
	while (1) {
		if (isspace(*str) || *str == 0)
			break; /* end of a test command + parameters */

		if (*str == PARAM_SEP) { /* test command has parameters */
			skip = false;
			str++;
			continue;
		}
		if (skip) { /* skip the current test command */
			str++;
			continue;
		}

		switch (*str) {
		default:
			if (parse_cb)
				parse_cb(str, param, &str);
			else
				str++;
			skip = true;
			break;
		case 'k':
			param->pa_no_reset = true;
			str++;
			break;
		case 'p':
			param->pa_perf = true;
			str++;
			break;
		}
	}
	*strp = str;
	return 0;
}

static int
pf_parse(char *str, struct pf_param *param, char **strp)
{
	return pf_parse_common(str, param, NULL, strp);
}

/**
 * Example: "U;p R;p;o=p"
 * 'U' is update test
 *	'p': parameter of update and it means outputing performance result
 *
 * 'R' is rebuild test
 * 	'p' is parameter of rebuild and it means outputing performance result
 * 	'o=p' means only run pull (no write) for rebuild.
 */
static void
pf_parse_rebuild_cb(char *str, struct pf_param *param, char **strp)
{
	switch (*str) {
	default:
		str++;
		break;
	case 'o':
		str++;
		if (*str != PARAM_ASSIGN)
			return;

		str++;
		if (*str == 's') {
			/* scan objects only */
			param->pa_rebuild.scan = true;
		} else if (*str == 'p') {
			/* scan objects, read data but no write */
			param->pa_rebuild.pull = true;
		}
		str++;
		break;
	}
	*strp = str;
	return;
}

static int
pf_parse_rebuild(char *str, struct pf_param *pa, char **strp)
{
	return pf_parse_common(str, pa, pf_parse_rebuild_cb, strp);
}

static void
pf_parse_iterate_cb(char *str, struct pf_param *pa, char **strp)
{
	switch (*str) {
	default:
		str++;
		break;
	case 'n':
		pa->pa_iter.nested = true;
		str++;
		break;
	}
	*strp = str;
	return;
}

static int
pf_parse_iterate(char *str, struct pf_param *pa, char **strp)
{
	return pf_parse_common(str, pa, pf_parse_iterate_cb, strp);
}

struct pf_test pf_tests[] = {
	{
		.ts_code	= 'U',
		.ts_name	= "UPDATE",
		.ts_parse	= pf_parse,
		.ts_func	= pf_update,
	},
	{
		.ts_code	= 'F',
		.ts_name	= "FETCH",
		.ts_parse	= pf_parse,
		.ts_func	= pf_fetch,
	},
	{
		.ts_code	= 'V',
		.ts_name	= "VERIFY",
		.ts_parse	= pf_parse,
		.ts_func	= pf_verify,
	},
	{
		.ts_code	= 'I',
		.ts_name	= "ITERATE",
		.ts_parse	= pf_parse_iterate,
		.ts_func	= pf_iterate,
	},
	{
		.ts_code	= 'R',
		.ts_name	= "REBUILD",
		.ts_parse	= pf_parse_rebuild,
		.ts_func	= pf_rebuild,
	},
	{
		.ts_code	= 0,
	},
};

struct pf_test *
find_test(char code)
{
	struct pf_test	*ts;
	int		 i;

	for (i = 0;; i++) {
		ts = &pf_tests[i];
		if (ts->ts_code == 0)
			break;

		if (ts->ts_code == code)
			return ts;
	}
	fprintf(stderr, "uknown test code %c\n", code);
	return NULL;
}

void
pause_test(char *name)
{
	int	c;

	while (ts_ctx.tsc_mpi_rank == 0) {
		D_PRINT("Type 'y|Y' to run test=%s: ", name);
		c = getc(stdin);
		if (c == 'y' || c == 'Y')
			break;
	}
	if (ts_ctx.tsc_mpi_size > 1)
		MPI_Barrier(MPI_COMM_WORLD);
}

int
run_one(struct pf_test *ts, struct pf_param *param)
{
	double	start;
	double	end;
	int	rc;

	/* guarantee the each test can generate the same OIDs/keys */
	srand(ts_seed);

	start = daos_get_ntime();
	if (!param->pa_no_reset)
		dts_reset_key();

	fprintf(stdout, "Running %s test\n", ts->ts_name);
	rc = ts->ts_func(ts, param);
	end = daos_get_ntime();
	if (ts_ctx.tsc_mpi_size > 1) {
		int	rc_g = 0;

		MPI_Allreduce(&rc, &rc_g, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
		rc = rc_g;
	}

	if (rc != 0) {
		fprintf(stderr, "Failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (param->pa_perf)
		show_result(param->pa_duration, start, end, ts->ts_name);

	return 0;
}

int
run_commands(char *cmds)
{
	struct pf_test	*ts = NULL;
	bool		 skip = false;
	int		 rc;

	while (1) {
		struct pf_param param;
		char		code;

		if (ts) {
			if (ts_pause)
				pause_test(ts->ts_name);
			else
				D_PRINT("Running test=%s\n", ts->ts_name);

			memset(&param, 0, sizeof(param));
			/* parse private parameters for the test */
			ts->ts_parse(cmds, &param, &cmds);

			/* run the test */
			rc = run_one(ts, &param);
			if (rc) {
				D_PRINT("%s failed\n", ts->ts_name);
				return rc;
			}
			D_PRINT("Completed test=%s\n", ts->ts_name);
			ts = NULL; /* reset */
			continue;
		}

		code = *cmds;
		cmds++;
		if (code == 0)
			return 0;

		if (isspace(code)) { /* move to a new command */
			skip = false;
			continue;
		}

		if (skip) /* unknown test code, skip all parameters */
			continue;

		ts = find_test(code);
		if (!ts) {
			fprintf(stdout, "Skip unknown test code=%c\n", code);
			skip = true;
			continue;
		}
	}
}

static uint64_t
ts_val_factor(uint64_t val, char factor)
{
	switch (factor) {
	default:
		return val;
	case 'k':
		val *= 1000;
		return val;
	case 'm':
		val *= 1000 * 1000;
		return val;
	case 'g':
		val *= 1000 * 1000 * 1000;
		return val;
	case 'K':
		val *= 1024;
		return val;
	case 'M':
		val *= 1024 * 1024;
		return val;
	case 'G':
		val *= 1024 * 1024 * 1024;
		return val;
	}
}

static const char *
ts_class_name(void)
{
	switch (ts_class) {
	default:
		return "unknown";
	case DAOS_OC_RAW:
		if (ts_in_ult)
			return "VOS (storage only running in ABT ULT)";
		else
			return "VOS (storage only)";
	case DAOS_OC_ECHO_TINY_RW:
		return "ECHO TINY (network only, non-replica)";
	case DAOS_OC_ECHO_R2S_RW:
		return "ECHO R2S (network only, 2-replica)";
	case DAOS_OC_ECHO_R3S_RW:
		return "ECHO R3S (network only, 3-replica)";
	case DAOS_OC_ECHO_R4S_RW:
		return "ECHO R4S (network only, 4-replica)";
	case OC_S1:
		return "DAOS TINY (full stack, non-replica)";
	case OC_SX:
		return "DAOS LARGE (full stack, non-replica)";
	case OC_RP_2G1:
		return "DAOS R2S (full stack, 2 replica)";
	case OC_RP_3G1:
		return "DAOS R3S (full stack, 3 replica)";
	case OC_RP_4G1:
		return "DAOS R4S (full stack, 4 replics)";
	case OC_EC_2P2G1:
		return "DAOS OC_EC_2P2G1 (full stack 2+2 EC)";
	case OC_EC_4P2G1:
		return "DAOS OC_EC_4P2G1 (full stack 4+2 EC)";
	case OC_EC_8P2G1:
		return "DAOS OC_EC_8P2G1 (full stack 8+2 EC)";
	}
}

static const char *
ts_val_type(void)
{
	return ts_single ? "single" : "array";
}

static const char *
ts_yes_or_no(bool value)
{
	return value ? "yes" : "no";
}

static void
ts_print_usage(void)
{
	printf("daos_perf -- performance benchmark tool for DAOS\n\
\n\
Description:\n\
	The daos_perf utility benchmarks point-to-point I/O performance of\n\
	different layers of the DAOS stack.\n\
\n\
The options are as follows:\n\
-h	Print this help message.\n\
\n\
-P number\n\
	Pool SCM partition size, which can have M(megatbytes) or \n\
	G(gigabytes) as postfix of number. E.g. -P 512M, -P 8G.\n\
\n\
-N number\n\
	Pool NVMe partition size.\n\
\n\
-T vos|echo|daos\n\
	Type of test, it can be 'vos' and 'daos'.\n\
	vos  : run directly on top of Versioning Object Store (VOS).\n\
	echo : I/O traffic generated by the utility only goes through the\n\
	       network stack and never lands to storage.\n\
	daos : I/O traffic goes through the full DAOS stack, including both\n\
	       network and storage.\n\
	The default value is 'vos'\n\
\n\
-C number\n\
	Credits for concurrently asynchronous I/O. It can be value between 1\n\
	and 64. The utility runs in synchronous mode if credits is set to 0.\n\
	This option is ignored for mode 'vos'.\n\
\n\
-c TINY|LARGE|R2S|R3S|R4S|EC2P1|EC2P2|EC4P2|EC8P2\n\
	Object class for DAOS full stack test.\n\
\n\
-o number\n\
	Number of objects are used by the utility.\n\
\n\
-d number\n\
	Number of dkeys per object. The number can have 'k' or 'm' as postfix\n\
	which stands for kilo or million.\n\
\n\
-a number\n\
	Number of akeys per dkey. The number can have 'k' or 'm' as postfix\n\
	which stands for kilo or million.\n\
\n\
-r number\n\
	Number of records per akey. The number can have 'k' or 'm' as postfix\n\
	which stands for kilo or million.\n\
\n\
-A	Use array value of akey, single value is selected by default.\n\
\n\
-s number\n\
	Size of single value, or extent size of array value. The number can\n\
	have 'K' or 'M' as postfix which stands for kilobyte or megabytes.\n\
\n\
-z	Use zero copy API, this option is only valid for 'vos'\n\
\n\
-t	Instead of using different indices and epochs, all I/Os land to the\n\
	same extent in the same epoch. This option can reduce usage of\n\
	storage space.\n\
\n\
-B	Profile performance of both update and fetch.\n\
\n\
-n	Only run iterate performance test but with nesting iterator\n\
	enable.  This can only run in vos mode.\n\
\n\
-f pathname\n\
	Full path name of the VOS file.\n\
\n\
-w	Pause after initialization for attaching debugger or analysis\n\
	tool.\n\
\n\
-x	run vos perf test in a ABT ult mode.\n\
\n\
-p	run vos perf with profile.\n");
}

static struct option ts_ops[] = {
	{ "pool_scm",	required_argument,	NULL,	'P' },
	{ "pool_nvme",	required_argument,	NULL,	'N' },
	{ "type",	required_argument,	NULL,	'T' },
	{ "credits",	required_argument,	NULL,	'C' },
	{ "obj",	required_argument,	NULL,	'o' },
	{ "dkey",	required_argument,	NULL,	'd' },
	{ "akey",	required_argument,	NULL,	'a' },
	{ "recx",	required_argument,	NULL,	'r' },
	{ "array",	no_argument,		NULL,	'A' },
	{ "size",	required_argument,	NULL,	's' },
	{ "zcopy",	no_argument,		NULL,	'z' },
	{ "overwrite",	no_argument,		NULL,	't' },
	{ "run",	required_argument,	NULL,	'R' },
	{ "file",	required_argument,	NULL,	'f' },
	{ "help",	no_argument,		NULL,	'h' },
	{ "wait",	no_argument,		NULL,	'w' },
	{ NULL,		0,			NULL,	0   },
};

static void
show_result(double duration, uint64_t start, uint64_t end, char *test_name)
{
	double		agg_duration;
	uint64_t	first_start;
	uint64_t	last_end;
	double		duration_max;
	double		duration_min;
	double		duration_sum;

	if (ts_ctx.tsc_mpi_size > 1) {
		MPI_Reduce(&start, &first_start, 1, MPI_UINT64_T,
			   MPI_MIN, 0, MPI_COMM_WORLD);
		MPI_Reduce(&end, &last_end, 1, MPI_UINT64_T,
			   MPI_MAX, 0, MPI_COMM_WORLD);
		agg_duration = (last_end - first_start) /
			       (1000.0 * 1000 * 1000);
	} else {
		agg_duration = duration / (1000.0 * 1000);
	}

	/* nano sec to sec */

	if (ts_ctx.tsc_mpi_size > 1) {
		MPI_Reduce(&duration, &duration_max, 1, MPI_DOUBLE,
			   MPI_MAX, 0, MPI_COMM_WORLD);
		MPI_Reduce(&duration, &duration_min, 1, MPI_DOUBLE,
			   MPI_MIN, 0, MPI_COMM_WORLD);
		MPI_Reduce(&duration, &duration_sum, 1, MPI_DOUBLE,
			   MPI_SUM, 0, MPI_COMM_WORLD);
	} else {
		duration_max = duration_min = duration_sum = duration;
	}

	if (ts_ctx.tsc_mpi_rank == 0) {
		unsigned long	total;
		double		bandwidth;
		double		latency;
		double		rate;

		total = ts_ctx.tsc_mpi_size *
			ts_obj_p_cont * ts_dkey_p_obj *
			ts_akey_p_dkey * ts_recx_p_akey;

		rate = total / agg_duration;
		latency = duration_max / total;
		bandwidth = (rate * ts_vsize) / (1024 * 1024);

		fprintf(stdout, "%s successfully completed:\n"
			"\tduration : %-10.6f sec\n"
			"\tbandwith : %-10.3f MB/sec\n"
			"\trate     : %-10.2f IO/sec\n"
			"\tlatency  : %-10.3f us "
			"(nonsense if credits > 1)\n",
			test_name, agg_duration, bandwidth, rate, latency);

		fprintf(stdout, "Duration across processes:\n");
		fprintf(stdout, "\tMAX duration : %-10.6f sec\n",
			duration_max/(1000 * 1000));
		fprintf(stdout, "\tMIN duration : %-10.6f sec\n",
			duration_min/(1000 * 1000));
		fprintf(stdout, "\tAverage duration : %-10.6f sec\n",
			duration_sum / ((ts_ctx.tsc_mpi_size) * 1000 * 1000));
	}
}

int
main(int argc, char **argv)
{
	char		*cmds	  = NULL;
	char		*dmg_conf = NULL;
	char		uuid_buf[256];
	daos_size_t	scm_size  = (2ULL << 30); /* default pool SCM size */
	daos_size_t	nvme_size = 0;	/* default pool NVMe size */
	int		credits   = -1;	/* sync mode */
	int		ec_vsize  = 0;
	d_rank_t	svc_rank  = 0;	/* pool service rank */
	int		rc;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &ts_ctx.tsc_mpi_rank);
	MPI_Comm_size(MPI_COMM_WORLD, &ts_ctx.tsc_mpi_size);

	memset(ts_pmem_file, 0, sizeof(ts_pmem_file));
	while ((rc = getopt_long(argc, argv,
				 "P:N:T:C:c:o:d:a:r:R:ASg:G:s:ztf:hBwxp",
				 ts_ops, NULL)) != -1) {
		char	*endp;

		switch (rc) {
		default:
			fprintf(stderr, "Unknown option %c\n", rc);
			return -1;
		case 'w':
			ts_pause = true;
			break;
		case 'T':
			if (!strcasecmp(optarg, "echo")) {
				/* just network, no storage */
				ts_mode = TS_MODE_ECHO;

			} else if (!strcasecmp(optarg, "daos")) {
				/* full stack: network + storage */
				ts_mode = TS_MODE_DAOS;

			} else if (!strcasecmp(optarg, "vos")) {
				/* pure storage */
				ts_mode = TS_MODE_VOS;

			} else {
				if (ts_ctx.tsc_mpi_rank == 0)
					ts_print_usage();
				return -1;
			}

			if (ts_mode == TS_MODE_VOS) { /* RAW only for VOS */
				if (ts_class != DAOS_OC_RAW)
					ts_class = DAOS_OC_RAW;
			} else { /* no RAW for other modes */
				if (ts_class == DAOS_OC_RAW)
					ts_class = OC_SX;
			}
			break;
		case 'C':
			credits = strtoul(optarg, &endp, 0);
			break;
		case 'c':
			if (!strcasecmp(optarg, "R4S")) {
				ts_class = OC_RP_4G1;
			} else if (!strcasecmp(optarg, "R3S")) {
				ts_class = OC_RP_3G1;
			} else if (!strcasecmp(optarg, "R2S")) {
				ts_class = OC_RP_2G1;
			} else if (!strcasecmp(optarg, "TINY")) {
				ts_class = OC_S1;
			} else if (!strcasecmp(optarg, "LARGE")) {
				ts_class = OC_SX;
			} else if (!strcasecmp(optarg, "EC2P1")) {
				ts_class = OC_EC_2P1G1;
			} else if (!strcasecmp(optarg, "EC2P")) {
				ts_class = OC_EC_2P2G1;
			} else if (!strcasecmp(optarg, "EC4P2")) {
				ts_class = OC_EC_4P2G1;
			} else if (!strcasecmp(optarg, "EC8P2")) {
				ts_class = OC_EC_8P2G1;
			} else {
				if (ts_ctx.tsc_mpi_rank == 0)
					ts_print_usage();
				return -1;
			}
			break;
		case 'P':
			scm_size = strtoul(optarg, &endp, 0);
			scm_size = ts_val_factor(scm_size, *endp);
			break;
		case 'N':
			nvme_size = strtoul(optarg, &endp, 0);
			nvme_size = ts_val_factor(nvme_size, *endp);
			break;
		case 'o':
			ts_obj_p_cont = strtoul(optarg, &endp, 0);
			ts_obj_p_cont = ts_val_factor(ts_obj_p_cont, *endp);
			break;
		case 'd':
			ts_dkey_p_obj = strtoul(optarg, &endp, 0);
			ts_dkey_p_obj = ts_val_factor(ts_dkey_p_obj, *endp);
			break;
		case 'a':
			ts_akey_p_dkey = strtoul(optarg, &endp, 0);
			ts_akey_p_dkey = ts_val_factor(ts_akey_p_dkey, *endp);
			break;
		case 'r':
			ts_recx_p_akey = strtoul(optarg, &endp, 0);
			ts_recx_p_akey = ts_val_factor(ts_recx_p_akey, *endp);
			break;
		case 'A':
			ts_single = false;
			break;
		case 'S':
			ts_shuffle = true;
			break;
		case 'g':
			dmg_conf = optarg;
			break;
		case 'G':
			ts_seed = atoi(optarg);
			break;
		case 'R':
			cmds = optarg;
			break;
		case 's':
			ts_vsize = strtoul(optarg, &endp, 0);
			ts_vsize = ts_val_factor(ts_vsize, *endp);
			if (ts_vsize < TEST_VAL_SIZE) {
				fprintf(stderr, "ERROR: value size must be >= "
					"%d\n", TEST_VAL_SIZE);
				return -1;
			}
			break;
		case 't':
			ts_overwrite = true;
			break;
		case 'z':
			ts_zero_copy = true;
			break;
		case 'f':
			strncpy(ts_pmem_file, optarg, PATH_MAX - 1);
			break;
		case 'x':
			ts_in_ult = true;
			break;
		case 'p':
			ts_profile_vos = true;
			break;
		case 'h':
			if (ts_ctx.tsc_mpi_rank == 0)
				ts_print_usage();
			return 0;
		}
	}

	if (!cmds) {
		ts_print_usage();
		return -1;
	}

	if (ts_seed == 0) {
		struct timeval	tv;

		gettimeofday(&tv, NULL);
		ts_seed = tv.tv_usec;
	}

	/* Convert object classes for echo mode.
	 * NB: we can also run in echo mode for arbitrary object class by
	 * setting DAOS_IO_BYPASS="target" while starting server.
	 */
	if (ts_mode == TS_MODE_ECHO) {
		if (ts_class == OC_RP_4G1)
			ts_class = DAOS_OC_ECHO_R4S_RW;
		else if (ts_class == OC_RP_3G1)
			ts_class = DAOS_OC_ECHO_R3S_RW;
		else if (ts_class == OC_RP_2G1)
			ts_class = DAOS_OC_ECHO_R2S_RW;
		else
			ts_class = DAOS_OC_ECHO_TINY_RW;
	}

	if (ts_dkey_p_obj == 0 || ts_akey_p_dkey == 0 ||
	    ts_recx_p_akey == 0) {
		fprintf(stderr, "Invalid arguments %d/%d/%d/\n",
			ts_akey_p_dkey, ts_recx_p_akey,
			ts_recx_p_akey);
		if (ts_ctx.tsc_mpi_rank == 0)
			ts_print_usage();
		return -1;
	}

	if (ts_mode == TS_MODE_VOS) {
		ts_ctx.tsc_cred_nr = -1; /* VOS can only support sync mode */
		if (strlen(ts_pmem_file) == 0)
			strcpy(ts_pmem_file, "/mnt/daos/vos_perf.pmem");

		ts_ctx.tsc_pmem_file = ts_pmem_file;
		if (ts_in_ult) {
			rc = ts_abt_init();
			if (rc)
				return rc;
		}
	} else {
		if (ts_in_ult || ts_profile_vos) {
			fprintf(stderr, "ULT and profiling is only supported"
				" in VOS mode.\n");
			if (ts_ctx.tsc_mpi_rank == 0)
				ts_print_usage();
			return -1;
		}

		ts_ctx.tsc_cred_nr = credits;
		ts_ctx.tsc_svc.rl_nr = 1;
		ts_ctx.tsc_svc.rl_ranks  = &svc_rank;
	}

	if (ts_class != DAOS_OC_RAW) {
		daos_obj_id_t	tmp_oid;
		struct daos_oclass_attr	*oca;

		tmp_oid = dts_oid_gen(ts_class, 0, 0);
		oca = daos_oclass_attr_find(tmp_oid);
		D_ASSERT(oca != NULL);
		if (DAOS_OC_IS_EC(oca))
			ec_vsize = oca->u.ec.e_len * oca->u.ec.e_k;
		if (ec_vsize != 0 && ts_vsize % ec_vsize != 0 &&
		    ts_ctx.tsc_mpi_rank == 0)
			fprintf(stdout, "for EC obj perf test, vsize (-s) %d "
				"should be multiple of %d (full-stripe size) "
				"to get better performance.\n",
				ts_vsize, ec_vsize);
	}

	ts_ctx.tsc_cred_vsize	= ts_vsize;
	ts_ctx.tsc_scm_size	= scm_size;
	ts_ctx.tsc_nvme_size	= nvme_size;
	ts_ctx.tsc_dmg_conf	= dmg_conf;

	if (ts_ctx.tsc_mpi_rank == 0 || ts_mode == TS_MODE_VOS) {
		uuid_generate(ts_ctx.tsc_cont_uuid);
		uuid_generate(ts_ctx.tsc_pool_uuid);
	}

	rc = dts_ctx_init(&ts_ctx);
	if (rc)
		return -1;

	/* For daos mode test, tsc_pool_uuid is a output parameter of
	 * dmg_pool_create; for vos mode test, tsc_pool_uuid is a input
	 * parameter of vos_pool_create.
	 * So uuid_unparse() the pool_uuid after dts_ctx_init.
	 */
	memset(uuid_buf, 0, sizeof(uuid_buf));
	if (ts_ctx.tsc_mpi_rank == 0 || ts_mode == TS_MODE_VOS)
		uuid_unparse(ts_ctx.tsc_pool_uuid, uuid_buf);

	if (ts_ctx.tsc_mpi_rank == 0) {
		fprintf(stdout,
			"Test :\n\t%s\n"
			"Pool :\n\t%s\n"
			"Parameters :\n"
			"\tpool size     : SCM: %u MB, NVMe: %u MB\n"
			"\tcredits       : %d (sync I/O for -ve)\n"
			"\tobj_per_cont  : %u x %d (procs)\n"
			"\tdkey_per_obj  : %u\n"
			"\takey_per_dkey : %u\n"
			"\trecx_per_akey : %u\n"
			"\tvalue type    : %s\n"
			"\tvalue size    : %u\n"
			"\tzero copy     : %s\n"
			"\toverwrite     : %s\n"
			"\tVOS file      : %s\n",
			ts_class_name(), uuid_buf,
			(unsigned int)(scm_size >> 20),
			(unsigned int)(nvme_size >> 20),
			credits,
			ts_obj_p_cont,
			ts_ctx.tsc_mpi_size,
			ts_dkey_p_obj,
			ts_akey_p_dkey,
			ts_recx_p_akey,
			ts_val_type(),
			ts_vsize,
			ts_yes_or_no(ts_zero_copy),
			ts_yes_or_no(ts_overwrite),
			ts_mode == TS_MODE_VOS ? ts_pmem_file : "<NULL>");
	}

	ts_ohs = calloc(ts_obj_p_cont, sizeof(*ts_ohs));
	ts_oids = calloc(ts_obj_p_cont, sizeof(*ts_oids));
	ts_uoids = calloc(ts_obj_p_cont, sizeof(*ts_uoids));
	if (!ts_ohs || !ts_oids || !ts_uoids) {
		fprintf(stderr, "failed to allocate %u open handles\n",
			ts_obj_p_cont);
		return -1;
	}

	if (ts_profile_vos)
		vos_profile_start(ts_profile_vos_path, ts_profile_vos_avg);
	MPI_Barrier(MPI_COMM_WORLD);

	rc = run_commands(cmds);

	if (ts_in_ult)
		ts_abt_fini();

	if (ts_profile_vos)
		vos_profile_stop();

	dts_ctx_fini(&ts_ctx);
	MPI_Finalize();
	free(ts_ohs);

	return 0;
}
