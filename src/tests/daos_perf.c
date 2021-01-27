/**
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#define STRIDE_MIN	(4) /* Should be changed with updating NB places */

enum ts_op_type {
	TS_DO_UPDATE = 0,
	TS_DO_FETCH
};

enum {
	TS_MODE_VOS,  /* pure storage */
	TS_MODE_ECHO, /* pure network */
	TS_MODE_DAOS, /* full stack */
};

int			 ts_mode = TS_MODE_DAOS;
int			 ts_class = OC_SX;

char			 ts_pmem_file[PATH_MAX];

unsigned int		 ts_obj_p_cont	= 1;	/* # objects per container */
unsigned int		 ts_dkey_p_obj	= 256;	/* # dkeys per object */
unsigned int		 ts_akey_p_dkey	= 16;	/* # akeys per dkey */
unsigned int		 ts_recx_p_akey	= 16;	/* # recxs per akey */
unsigned int		 ts_stride	= 64;	/* default extent size */
unsigned int		 ts_seed;
/* value type: single or array */
bool			 ts_single	= true;
/* use zero-copy API for VOS, ignored for "echo" or "daos" */
bool			 ts_zero_copy;
/* random write (array value only) */
bool			 ts_random;
bool			 ts_pause;

bool			 ts_oid_init;

daos_handle_t		*ts_ohs;		/* all opened objects */
daos_obj_id_t		*ts_oids;		/* object IDs */
daos_unit_oid_t		*ts_uoids;		/* object shard IDs (for VOS) */
uint64_t		*ts_indices;

struct dts_context	 ts_ctx;
bool			 ts_nest_iterator;

/* test inside ULT */
bool			ts_in_ult;
bool			ts_profile_vos;
char			*ts_profile_vos_path = ".";
int			ts_profile_vos_avg = 100;
static ABT_xstream	abt_xstream;

#define PF_DKEY_PREF	"blade"
#define PF_AKEY_PREF	"apple"

struct pf_param {
	/* output performance */
	bool		pa_perf;
	/* no key reset, verification cannot work after enabling it */
	bool		pa_no_reset;
	/* # iterations of the test */
	int		pa_iteration;
	/* output parameter */
	double		pa_duration;
	union {
		/* private parameter for rebuild */
		struct {
			/* only run rebuild scan */
			bool	scan;
			/* run scan + pull, no local write */
			bool	pull;
		} pa_rebuild;
		/* private parameter for iteration */
		struct {
			/* nested iterator */
			bool	nested;
		} pa_iter;
		/* private parameter for OIT */
		struct {
			bool	verbose;
		} pa_oit;
		/* private parameter for update, fetch and verify */
		struct {
			/* offset within stride */
			int	offset;
			/* size of the I/O */
			int	size;
			/* verify the read */
			bool	verify;
		} pa_rw;
	};
};

struct pf_test {
	/* identifier of test */
	char	  ts_code;
	/* name of the test */
	char	 *ts_name;
	/* parse test parameters */
	int	(*ts_parse)(char *str, struct pf_param *param, char **strpp);
	/* main test function */
	int	(*ts_func)(struct pf_test *ts, struct pf_param *param);
};

typedef int (*pf_parse_cb_t)(char *, struct pf_param *, char **);

static void ts_print_usage(void);
static void show_result(struct pf_param *param, uint64_t start, uint64_t end,
			char *test_name);
static bool val_has_unit(char c);
static uint64_t val_unit(uint64_t val, char unit);

/* buffer for data verification */
struct pf_stride_buf {
	char		*sb_buf;
	char		 sb_mark;
	unsigned int	 sb_size;
};

struct pf_stride_buf	stride_buf;

/* mark 16 bytes within each 4K for verification */
static int stride_marks[] = {
	0,	3,	7,	13,
	23,	56,	105,	158,
	231,	400,	712,	1291,
	1788,	2371,	3116,	3968,
};

#define STRIDE_PAGE	(1 << 12)

enum {
	/* set a few some bytes in stride_buf */
	STRIDE_BUF_SET,
	/* load marked bytes from stride_buf for write */
	STRIDE_BUF_LOAD,
	/* check if read buffer can match with stride buffer */
	STRIDE_BUF_VERIFY,
};

static void
stride_buf_init(int size)
{
	stride_buf.sb_mark	= 'A';
	stride_buf.sb_size	= size;
	stride_buf.sb_buf	= calloc(1, size);
	D_ASSERT(stride_buf.sb_buf);
}

static void
stride_buf_fini(void)
{
	if (stride_buf.sb_buf)
		free(stride_buf.sb_buf);
}

static int
stride_buf_op(int opc, char *buf, unsigned offset, int size)
{
	unsigned int	i;
	unsigned int	j;
	char		mark = stride_buf.sb_mark;

	if (opc == STRIDE_BUF_SET) {
		stride_buf.sb_mark++;
		if (stride_buf.sb_mark > 'Z')
			stride_buf.sb_mark = 'A';
	}

	for (i = (offset & ~(STRIDE_PAGE - 1));
	     i < stride_buf.sb_size; i += STRIDE_PAGE) {
		for (j = 0; j < ARRAY_SIZE(stride_marks); j++) {
			int	pos;

			pos = i + stride_marks[j];
			if (pos < offset)
				continue;
			/* possible for the last page */
			if (pos >= stride_buf.sb_size)
				break;

			if (pos >= offset + size) {
				/* NB: for single value, unset marks because
				 * old version will be fully overwritten
				 */
				if (ts_single && opc == STRIDE_BUF_SET) {
					stride_buf.sb_buf[pos] = 0;
					continue;
				}
				return 0;
			}

			switch (opc) {
			case STRIDE_BUF_SET:
				stride_buf.sb_buf[pos] = mark;
				break;
			case STRIDE_BUF_VERIFY:
				D_ASSERT(buf);
				if (stride_buf.sb_buf[pos] != buf[pos - offset])
					return -1; /* mismatch */
				break;
			case STRIDE_BUF_LOAD:
				D_ASSERT(buf);
				buf[pos - offset] = stride_buf.sb_buf[pos];
				break;
			}
		}
	}
	return 0;
}

static void
stride_buf_set(unsigned offset, int size)
{
	stride_buf_op(STRIDE_BUF_SET, NULL, offset, size);
}

static void
stride_buf_load(char *buf, unsigned offset, int size)
{
	stride_buf_op(STRIDE_BUF_LOAD, buf, offset, size);
}

static int
stride_buf_verify(char *buf, unsigned offset, int size)
{
	return stride_buf_op(STRIDE_BUF_VERIFY, buf, offset, size);
}

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

	rc = ABT_xstream_get_affinity(abt_xstream, 0, NULL, &num_cpus);
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
					       ult_arg->op_type,
					       ult_arg->cred,
					       ult_arg->epoch,
					       ult_arg->duration);
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
		     bool sync, double *duration)
{
	daos_event_t *evp = sync ? NULL : cred->tc_evp;
	uint64_t      start = 0;
	int	      rc;

	if (!dts_is_async(&ts_ctx))
		TS_TIME_START(duration, start);
	if (op_type == TS_DO_UPDATE) {
		rc = daos_obj_update(ts_ohs[obj_idx], DAOS_TX_NONE, 0,
				     &cred->tc_dkey, 1, &cred->tc_iod,
				     &cred->tc_sgl, evp);
	} else {
		rc = daos_obj_fetch(ts_ohs[obj_idx], DAOS_TX_NONE, 0,
				    &cred->tc_dkey, 1, &cred->tc_iod,
				    &cred->tc_sgl, NULL, evp);
	}

	if (!dts_is_async(&ts_ctx))
		TS_TIME_END(duration, start);

	return rc;
}

static int
akey_update_or_fetch(int obj_idx, enum ts_op_type op_type,
		     char *dkey, char *akey, daos_epoch_t *epoch,
		     int idx, struct pf_param *param)
{
	struct dts_io_credit *cred;
	daos_iod_t	     *iod;
	d_sg_list_t	     *sgl;
	daos_recx_t	     *recx;
	size_t		      len;
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

	/* setup dkey */
	len = min(strlen(dkey), DTS_KEY_LEN);
	memcpy(cred->tc_dbuf, dkey, len);
	d_iov_set(&cred->tc_dkey, cred->tc_dbuf, len);

	/* setup I/O descriptor */
	len = min(strlen(akey), DTS_KEY_LEN);
	memcpy(cred->tc_abuf, akey, len);
	d_iov_set(&iod->iod_name, cred->tc_abuf, len);
	if (ts_single) {
		iod->iod_type = DAOS_IOD_SINGLE;
		iod->iod_size = param->pa_rw.size;
		recx->rx_nr   = 1;
		recx->rx_idx  = 0;
	} else {
		iod->iod_type = DAOS_IOD_ARRAY;
		iod->iod_size = 1;
		recx->rx_nr   = param->pa_rw.size;
		recx->rx_idx  = ts_indices[idx] * ts_stride +
				param->pa_rw.offset;
	}

	iod->iod_nr    = 1;
	iod->iod_recxs = recx;
	iod->iod_flags = 0;

	if (op_type == TS_DO_UPDATE) {
		/* initialize value buffer and setup sgl */
		stride_buf_load(cred->tc_vbuf, param->pa_rw.offset,
				param->pa_rw.size);
	} else {
		if (param->pa_rw.verify) /* Clear the buffer for verify */
			memset(cred->tc_vbuf, 0, param->pa_rw.size);
	}

	d_iov_set(&cred->tc_val, cred->tc_vbuf, param->pa_rw.size);
	sgl->sg_iovs = &cred->tc_val;
	sgl->sg_nr = 1;
	sgl->sg_nr_out = 0;

	if (ts_mode == TS_MODE_VOS)
		rc = vos_update_or_fetch(obj_idx, op_type, cred, *epoch,
					 &param->pa_duration);
	else
		rc = daos_update_or_fetch(obj_idx, op_type, cred, *epoch,
					  !!param->pa_rw.verify,
					  &param->pa_duration);

	if (rc != 0) {
		fprintf(stderr, "%s failed. rc=%d, epoch=%"PRIu64"\n",
			op_type == TS_DO_FETCH ? "Fetch" : "Update",
			rc, *epoch);
		return rc;
	}

	(*epoch)++;
	if (param->pa_rw.verify) {
		rc = stride_buf_verify(cred->tc_vbuf, param->pa_rw.offset,
				       param->pa_rw.size);
		dts_credit_return(&ts_ctx, cred);
		return rc;
	}
	return 0;
}

static int
dkey_update_or_fetch(enum ts_op_type op_type, char *dkey, daos_epoch_t *epoch,
		     struct pf_param *param)
{
	char		 akey[DTS_KEY_LEN];
	int		 i;
	int		 j;
	int		 k;
	int		 rc = 0;

	if (!ts_indices) {
		ts_indices = dts_rand_iarr_alloc_set(ts_recx_p_akey, 0,
						     ts_random);
		D_ASSERT(ts_indices != NULL);
	}

	for (i = 0; i < ts_akey_p_dkey; i++) {
		dts_key_gen(akey, DTS_KEY_LEN, PF_AKEY_PREF);
		for (j = 0; j < ts_recx_p_akey; j++) {
			for (k = 0; k < ts_obj_p_cont; k++) {
				rc = akey_update_or_fetch(k, op_type, dkey,
							  akey, epoch, j,
							  param);
				if (rc)
					break;
			}
		}
	}
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
			ts_uoids[i].id_pub = ts_oids[i];
			ts_uoids[i].id_shard = 0;
			ts_uoids[i].id_pad_32 = 0;
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
objects_update(struct pf_param *param)
{
	static daos_epoch_t epoch = 1;
	int		i;
	int		rc = 0;
	int		rc_drain;
	uint64_t	start = 0;

	stride_buf_set(param->pa_rw.offset, param->pa_rw.size);
	++epoch;

	if (dts_is_async(&ts_ctx))
		TS_TIME_START(&param->pa_duration, start);

	for (i = 0; i < ts_dkey_p_obj; i++) {
		char	 dkey[DTS_KEY_LEN];

		dts_key_gen(dkey, DTS_KEY_LEN, PF_DKEY_PREF);
		rc = dkey_update_or_fetch(TS_DO_UPDATE, dkey, &epoch,
					  param);
		if (rc)
			break;
	}
	rc_drain = dts_credit_drain(&ts_ctx);
	if (rc == 0)
		rc = rc_drain;

	if (dts_is_async(&ts_ctx))
		TS_TIME_END(&param->pa_duration, start);

	return rc;
}

static int
objects_fetch(struct pf_param *param)
{
	int		i;
	int		rc = 0;
	int		rc_drain;
	uint64_t	start = 0;
	daos_epoch_t	epoch = crt_hlc_get();

	if (dts_is_async(&ts_ctx))
		TS_TIME_START(&param->pa_duration, start);

	for (i = 0; i < ts_dkey_p_obj; i++) {
		char	 dkey[DTS_KEY_LEN];

		dts_key_gen(dkey, DTS_KEY_LEN, PF_DKEY_PREF);
		rc = dkey_update_or_fetch(TS_DO_FETCH, dkey, &epoch,
					  param);
		if (rc != 0)
			break;
	}
	rc_drain = dts_credit_drain(&ts_ctx);
	if (rc == 0)
		rc = rc_drain;

	if (dts_is_async(&ts_ctx))
		TS_TIME_END(&param->pa_duration, start);
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
	if (rc)
		return rc;

	rc = ts_iterate_internal(VOS_ITER_SINGLE, param, NULL);
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
obj_iter_records(daos_unit_oid_t oid, struct pf_param *ppa)
{
	vos_iter_param_t	param = {};
	int			rc = 0;
	uint64_t		start = 0;

	assert_int_equal(ts_class, DAOS_OC_RAW);

	/* prepare iterate parameters */
	param.ip_hdl = ts_ctx.tsc_coh;
	param.ip_oid = oid;

	param.ip_epr.epr_lo = 0;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	param.ip_epc_expr = VOS_IT_EPC_RE;

	TS_TIME_START(&ppa->pa_duration, start);
	rc = ts_iterate_internal(VOS_ITER_DKEY, &param, iter_dkey_cb);
	TS_TIME_END(&ppa->pa_duration, start);
	return rc;
}

static int
pf_update(struct pf_test *ts, struct pf_param *param)
{
	int	rc;

	rc = objects_open();
	if (rc)
		return rc;

	rc = objects_update(param);
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

	param->pa_rw.verify = false;
	rc = objects_fetch(param);
	if (rc)
		return rc;

	rc = objects_close();
	return rc;
}

static int
pf_verify(struct pf_test *ts, struct pf_param *param)
{
	int	rc;

	if (ts_single && ts_recx_p_akey > 1) {
		fprintf(stdout, "Verification is unsupported\n");
		return 0;
	}

	rc = objects_open();
	if (rc)
		return rc;

	param->pa_rw.verify = true;
	rc = objects_fetch(param);
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
	ts_nest_iterator = param->pa_iter.nested;
	return obj_iter_records(ts_uoids[0], param);
}

static int
pf_oit(struct pf_test *pf, struct pf_param *param)
{
	static const int OID_ARR_SIZE	= 8;
	daos_obj_id_t	oids[OID_ARR_SIZE];
	daos_anchor_t	anchor;
	daos_handle_t	toh;
	daos_epoch_t	epoch;
	uint32_t	oids_nr;
	int		total;
	int		i;
	int		rc;

	if (ts_mode != TS_MODE_DAOS)
		return 0; /* cannot support */

	rc = daos_cont_create_snap_opt(ts_ctx.tsc_coh, &epoch, NULL,
				       DAOS_SNAP_OPT_CR | DAOS_SNAP_OPT_OIT,
				       NULL);
	if (rc)
		fprintf(stderr, "failed to create snapshot\n");

	rc = daos_oit_open(ts_ctx.tsc_coh, epoch, &toh, NULL);
	D_ASSERT(rc == 0);

	memset(&anchor, 0, sizeof(anchor));
	for (total = 0; true; ) {
		oids_nr = OID_ARR_SIZE;
		rc = daos_oit_list(toh, oids, &oids_nr, &anchor, NULL);
		D_ASSERTF(rc == 0, "%d\n", rc);

		D_PRINT("returned %d oids\n", oids_nr);
		for (i = 0; i < oids_nr; i++) {
			if (param->pa_oit.verbose) {
				D_PRINT("oid[%d] ="DF_OID"\n",
					total, DP_OID(oids[i]));
			}
			total++;
		}
		if (daos_anchor_is_eof(&anchor)) {
			D_PRINT("listed %d objects\n", total);
			break;
		}
	}
	rc = daos_oit_close(toh, NULL);
	D_ASSERT(rc == 0);
	return rc;
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
	rc = daos_pool_tgt_exclude(ts_ctx.tsc_pool_uuid, NULL,
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
	rc = daos_pool_reint_tgt(ts_ctx.tsc_pool_uuid, NULL,
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
		daos_debug_set_params(NULL, -1, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_NO_REBUILD,
				     0, NULL);
	} else if (param->pa_rebuild.pull) {
		daos_debug_set_params(NULL, -1, DMG_KEY_FAIL_LOC,
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

	daos_debug_set_params(NULL, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
	return rc;
}

/* Test command Format: "C;p=x;q D;a;b"
 *
 * The upper-case character is command, e.g. U=update, F=fetch, anything after
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
	int  rc;

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
			if (parse_cb) {
				rc = parse_cb(str, param, &str);
				if (rc)
					return rc;
			} else {
				str++;
			}
			break;
		case 'k':
			param->pa_no_reset = true;
			str++;
			break;
		case 'p':
			param->pa_perf = true;
			str++;
			break;
		case 'i':
			str++;
			if (*str != PARAM_ASSIGN)
				return -1;

			param->pa_iteration = strtol(&str[1], &str, 0);
			break;
		}
		skip = *str != PARAM_SEP;
	}
	*strp = str;
	return 0;
}

static int
pf_parse_rw_cb(char *str, struct pf_param *param, char **strp)
{
	char	c = *str;
	int	val;

	switch (c) {
	default:
		str++;
		break;
	case 'o':
	case 's':
		str++;
		if (*str != PARAM_ASSIGN)
			return -1;

		val = strtol(&str[1], &str, 0);
		if (val_has_unit(*str)) {
			val = val_unit(val, *str);
			str++;
		}
		if (c == 'o')
			param->pa_rw.offset = val;
		else
			param->pa_rw.size = val;
		break;
	}
	*strp = str;
	return 0;
}

static int
pf_parse_rw(char *str, struct pf_param *param, char **strp)
{
	int	rc;

	rc = pf_parse_common(str, param, pf_parse_rw_cb, strp);
	if (rc)
		return rc;

	if (param->pa_rw.size == 0) /* full stride write */
		param->pa_rw.size = ts_stride;

	if (ts_single)
		param->pa_rw.offset = 0;

	if (param->pa_rw.offset + param->pa_rw.size > ts_stride) {
		D_PRINT("offset + size crossed the stride boundary: %d/%d/%d\n",
			param->pa_rw.offset, param->pa_rw.size, ts_stride);
		return -1;
	}
	return 0;
}

/**
 * Example: "U;p R;p;o=p"
 * 'U' is update test
 *	'p': parameter of update and it means outputting performance result
 *
 * 'R' is rebuild test
 *	'p' is parameter of rebuild and it means outputting performance result
 *	'o=p' means only run pull (no write) for rebuild.
 */
static int
pf_parse_rebuild_cb(char *str, struct pf_param *param, char **strp)
{
	switch (*str) {
	default:
		str++;
		break;
	case 'o':
		str++;
		if (*str != PARAM_ASSIGN)
			return -1;

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
	return 0;
}

static int
pf_parse_rebuild(char *str, struct pf_param *pa, char **strp)
{
	return pf_parse_common(str, pa, pf_parse_rebuild_cb, strp);
}

static int
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
	return 0;
}

static int
pf_parse_iterate(char *str, struct pf_param *pa, char **strp)
{
	return pf_parse_common(str, pa, pf_parse_iterate_cb, strp);
}

static int
pf_parse_oit_cb(char *str, struct pf_param *pa, char **strp)
{
	switch (*str) {
	default:
		str++;
		break;
	case 'v':
		pa->pa_oit.verbose = true;
		str++;
		break;
	}
	*strp = str;
	return 0;
}

static int
pf_parse_oit(char *str, struct pf_param *pa, char **strp)
{
	return pf_parse_common(str, pa, pf_parse_oit_cb, strp);
}

/* predefined test cases */
struct pf_test pf_tests[] = {
	{
		.ts_code	= 'U',
		.ts_name	= "UPDATE",
		.ts_parse	= pf_parse_rw,
		.ts_func	= pf_update,
	},
	{
		.ts_code	= 'F',
		.ts_name	= "FETCH",
		.ts_parse	= pf_parse_rw,
		.ts_func	= pf_fetch,
	},
	{
		.ts_code	= 'V',
		.ts_name	= "VERIFY",
		.ts_parse	= pf_parse_rw,
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
		.ts_code	= 'O',
		.ts_name	= "OIT",
		.ts_parse	= pf_parse_oit,
		.ts_func	= pf_oit,
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
	fprintf(stderr, "unknown test code %c\n", code);
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
	int	i;
	int	rc;

	/* guarantee the each test can generate the same OIDs/keys */
	srand(ts_seed);
	if (param->pa_iteration == 0)
		param->pa_iteration = 1;

	fprintf(stdout, "Running %s test (iteration=%d)\n",
		ts->ts_name, param->pa_iteration);

	start = daos_get_ntime();

	for (i = 0; i < param->pa_iteration; i++) {
		if (!param->pa_no_reset)
			dts_reset_key();

		rc = ts->ts_func(ts, param);
		if (rc)
			break;
	}

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
		show_result(param, start, end, ts->ts_name);

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
			char	*tmp = cmds;

			if (ts_pause)
				pause_test(ts->ts_name);
			else
				D_PRINT("Running test=%s\n", ts->ts_name);

			memset(&param, 0, sizeof(param));
			/* parse private parameters of the test */
			rc = ts->ts_parse(cmds, &param, &cmds);
			if (rc) {
				D_PRINT("Invalid test parameters: %s\n", tmp);
				return rc;
			}

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
		if (code == 0) /* finished all the tests */
			return 0;

		if (isspace(code)) { /* move to a new command */
			skip = false;
			continue;
		}

		if (skip) /* unknown test code, skip all parameters */
			continue;

		ts = find_test(code);
		if (!ts) {
			fprintf(stdout, "Unknown test code=%c\n", code);
			skip = true;
			continue;
		}
	}
}

static bool
val_has_unit(char c)
{
	return c == 'k' || c == 'K' || c == 'm' ||
	       c == 'M' || c == 'g' || c == 'G';

}

static uint64_t
val_unit(uint64_t val, char unit)
{
	switch (unit) {
	default:
		return val;
	case 'k':
	case 'K':
		val *= 1024;
		return val;
	case 'm':
	case 'M':
		val *= 1024 * 1024;
		return val;
	case 'g':
	case 'G':
		val *= 1024 * 1024 * 1024;
		return val;
	}
}

static const char *
pf_class2name(void)
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

static int
pf_name2class(char *name)
{
	if (!strcasecmp(name, "R4S")) {
		ts_class = OC_RP_4G1;
	} else if (!strcasecmp(name, "R3S")) {
		ts_class = OC_RP_3G1;
	} else if (!strcasecmp(name, "R2S")) {
		ts_class = OC_RP_2G1;
	} else if (!strcasecmp(name, "TINY")) {
		ts_class = OC_S1;
	} else if (!strcasecmp(name, "LARGE")) {
		ts_class = OC_SX;
	} else if (!strcasecmp(name, "EC2P1")) {
		ts_class = OC_EC_2P1G1;
	} else if (!strcasecmp(name, "EC2P")) {
		ts_class = OC_EC_2P2G1;
	} else if (!strcasecmp(name, "EC4P2")) {
		ts_class = OC_EC_4P2G1;
	} else if (!strcasecmp(name, "EC8P2")) {
		ts_class = OC_EC_8P2G1;
	} else {
		if (ts_ctx.tsc_mpi_rank == 0)
			ts_print_usage();
		return -1;
	}
	return 0;
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
-T daos|echo|vos\n\
	Type of test, it can be 'daos', 'echo' and 'vos'.\n\
	daos : I/O traffic goes through the full DAOS stack, including both\n\
	       network and storage.\n\
	echo : I/O traffic generated by the utility only goes through the\n\
	       network stack and never lands to storage.\n\
	vos  : run directly on top of Versioning Object Store (VOS).\n\
	The default value is 'daos'\n\
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
-n number\n\
	Number of strides per akey. The number can have 'k' or 'm' as postfix\n\
	which stands for kilo or million.\n\
\n\
-A [R]\n\
	Use array value of akey, single value is selected by default.\n\
	optional parameter 'R' indicates random writes\n\
\n\
-s number\n\
	Stride size, it is the offset distance between two array writes,\n\
	it is also the default size for write if 'U' has no size parameter\n\
	The number can have 'K' or 'M' as postfix which stands for kilobyte\n\
	or megabytes.\n\
\n\
-z	Use zero copy API, this option is only valid for 'vos'\n\
\n\
-t	Instead of using different indices and epochs, all I/Os land to the\n\
	same extent in the same epoch. This option can reduce usage of\n\
	storage space.\n\
\n\
-B	Profile performance of both update and fetch.\n\
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
	{ "oit",	no_argument,		NULL,	'O' },
	{ "obj",	required_argument,	NULL,	'o' },
	{ "dkey",	required_argument,	NULL,	'd' },
	{ "akey",	required_argument,	NULL,	'a' },
	{ "num",	required_argument,	NULL,	'n' },
	{ "stride",	required_argument,	NULL,	's' },
	{ "array",	optional_argument,	NULL,	'A' },
	{ "size",	required_argument,	NULL,	's' },
	{ "zcopy",	no_argument,		NULL,	'z' },
	{ "run",	required_argument,	NULL,	'R' },
	{ "file",	required_argument,	NULL,	'f' },
	{ "dmg_conf",	required_argument,	NULL,	'g' },
	{ "help",	no_argument,		NULL,	'h' },
	{ "wait",	no_argument,		NULL,	'w' },
	{ NULL,		0,			NULL,	0   },
};

static void
show_result(struct pf_param *param, uint64_t start, uint64_t end,
	    char *test_name)
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
		agg_duration = param->pa_duration / (1000.0 * 1000);
	}

	/* nano sec to sec */

	if (ts_ctx.tsc_mpi_size > 1) {
		MPI_Reduce(&param->pa_duration, &duration_max, 1, MPI_DOUBLE,
			   MPI_MAX, 0, MPI_COMM_WORLD);
		MPI_Reduce(&param->pa_duration, &duration_min, 1, MPI_DOUBLE,
			   MPI_MIN, 0, MPI_COMM_WORLD);
		MPI_Reduce(&param->pa_duration, &duration_sum, 1, MPI_DOUBLE,
			   MPI_SUM, 0, MPI_COMM_WORLD);
	} else {
		duration_max = duration_min =
		duration_sum = param->pa_duration;
	}

	if (ts_ctx.tsc_mpi_rank == 0) {
		unsigned long	total;
		double		bandwidth;
		double		latency;
		double		rate;

		total = ts_ctx.tsc_mpi_size * param->pa_iteration *
			ts_obj_p_cont * ts_dkey_p_obj *
			ts_akey_p_dkey * ts_recx_p_akey;

		rate = total / agg_duration;
		latency = duration_max / total;
		bandwidth = (rate * param->pa_rw.size) / (1024 * 1024);

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
	d_rank_t	svc_rank  = 0;	/* pool service rank */
	int		rc;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &ts_ctx.tsc_mpi_rank);
	MPI_Comm_size(MPI_COMM_WORLD, &ts_ctx.tsc_mpi_size);

	memset(ts_pmem_file, 0, sizeof(ts_pmem_file));
	while ((rc = getopt_long(argc, argv,
				 "P:N:T:C:c:o:d:a:n:s:R:g:G:zf:hwxpA::",
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

			break;
		case 'C':
			credits = strtoul(optarg, &endp, 0);
			break;
		case 'c':
			rc = pf_name2class(optarg);
			if (rc)
				return rc;
			break;
		case 'P':
			scm_size = strtoul(optarg, &endp, 0);
			scm_size = val_unit(scm_size, *endp);
			break;
		case 'N':
			nvme_size = strtoul(optarg, &endp, 0);
			nvme_size = val_unit(nvme_size, *endp);
			break;
		case 'o':
			ts_obj_p_cont = strtoul(optarg, &endp, 0);
			ts_obj_p_cont = val_unit(ts_obj_p_cont, *endp);
			break;
		case 'd':
			ts_dkey_p_obj = strtoul(optarg, &endp, 0);
			ts_dkey_p_obj = val_unit(ts_dkey_p_obj, *endp);
			break;
		case 'a':
			ts_akey_p_dkey = strtoul(optarg, &endp, 0);
			ts_akey_p_dkey = val_unit(ts_akey_p_dkey, *endp);
			break;
		case 'n':
			ts_recx_p_akey = strtoul(optarg, &endp, 0);
			ts_recx_p_akey = val_unit(ts_recx_p_akey, *endp);
			break;
		case 's':
			ts_stride = strtoul(optarg, &endp, 0);
			ts_stride = val_unit(ts_stride, *endp);
			break;
		case 'A':
			ts_single = false;
			if (optarg  && (optarg[0] == 'r' || optarg[0] == 'R'))
				ts_random = true;
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
		case 'z':
			ts_zero_copy = true;
			break;
		case 'f':
			if (strnlen(optarg, PATH_MAX) >= (PATH_MAX - 5)) {
				fprintf(stderr, "filename size must be < %d\n",
					PATH_MAX - 5);
				return -1;
			}
			strncpy(ts_pmem_file, optarg, PATH_MAX - 5);
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
		D_PRINT("Please provide command string\n");
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
	if (ts_mode == TS_MODE_VOS) { /* RAW only for VOS */
		ts_class = DAOS_OC_RAW;

	} else { /* no RAW for other modes */
		if (ts_class == DAOS_OC_RAW)
			ts_class = OC_S1;
	}

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

	if (ts_dkey_p_obj == 0 || ts_akey_p_dkey == 0 || ts_recx_p_akey == 0) {
		fprintf(stderr, "Invalid arguments %d/%d/%d/\n",
			ts_dkey_p_obj, ts_akey_p_dkey, ts_recx_p_akey);
		if (ts_ctx.tsc_mpi_rank == 0)
			ts_print_usage();
		return -1;
	}

	if (ts_mode == TS_MODE_VOS) {
		if (ts_ctx.tsc_mpi_size > 1 &&
		    (access("/etc/daos_nvme.conf", F_OK) != -1)) {
			fprintf(stderr,
				"no support: multi-proc vos_perf with NVMe\n");
			return -1;
		}
		ts_ctx.tsc_cred_nr = -1; /* VOS can only support sync mode */
		if (strlen(ts_pmem_file) == 0) {
			snprintf(ts_pmem_file, sizeof(ts_pmem_file),
				 "/mnt/daos/vos_perf%d.pmem",
				 ts_ctx.tsc_mpi_rank);
		} else {
			char id[16];


			snprintf(id, sizeof(id), "%d", ts_ctx.tsc_mpi_rank);
			strncat(ts_pmem_file, id,
				(sizeof(ts_pmem_file) - strlen(ts_pmem_file)));
		}
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

	if (ts_stride < STRIDE_MIN)
		ts_stride = STRIDE_MIN;

	stride_buf_init(ts_stride);

	ts_ctx.tsc_cred_vsize	= ts_stride;
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
			"\tstride size   : %u\n"
			"\tzero copy     : %s\n"
			"\tVOS file      : %s\n",
			pf_class2name(), uuid_buf,
			(unsigned int)(scm_size >> 20),
			(unsigned int)(nvme_size >> 20),
			credits,
			ts_obj_p_cont,
			ts_ctx.tsc_mpi_size,
			ts_dkey_p_obj,
			ts_akey_p_dkey,
			ts_recx_p_akey,
			ts_val_type(),
			ts_stride,
			ts_yes_or_no(ts_zero_copy),
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
	if (ts_indices)
		free(ts_indices);
	stride_buf_fini();
	dts_ctx_fini(&ts_ctx);

	MPI_Finalize();

	if (ts_uoids)
		free(ts_uoids);
	if (ts_oids)
		free(ts_oids);
	if (ts_ohs)
		free(ts_ohs);

	return 0;
}
