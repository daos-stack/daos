/**
 * (C) Copyright 2018-2019 Intel Corporation.
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
#include <daos/common.h>
#include <daos/tests_lib.h>
#include <daos_srv/vos.h>
#include <daos_test.h>
#include "dts_common.h"

/* unused object class to identify VOS (storage only) test mode */
#define DAOS_OC_RAW	(0xBEEF)
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
/* value type: single or array */
bool			 ts_single	= true;
/* always overwrite value of an akey */
bool			 ts_overwrite;
/* use zero-copy API for VOS, ignored for "echo" or "daos" */
bool			 ts_zero_copy;
/* verify the output of fetch */
bool			 ts_verify_fetch;

daos_handle_t		*ts_ohs;		/* all opened objects */
daos_obj_id_t		 ts_oid;		/* object ID */
daos_unit_oid_t		 ts_uoid;		/* object shard ID (for VOS) */

struct dts_context	 ts_ctx;
bool			 ts_nest_iterator;

/* rebuild only with iteration */
bool			ts_rebuild_only_iteration = false;
/* rebuild without update */
bool			ts_rebuild_no_update = false;

static int
vos_update_or_fetch(enum ts_op_type op_type, struct dts_io_credit *cred,
		    daos_epoch_t epoch)
{
	int	rc = 0;

	if (!ts_zero_copy) {
		if (op_type == TS_DO_UPDATE)
			rc = vos_obj_update(ts_ctx.tsc_coh, ts_uoid, epoch,
				0, &cred->tc_dkey, 1, &cred->tc_iod,
				&cred->tc_sgl);
		else
			rc = vos_obj_fetch(ts_ctx.tsc_coh, ts_uoid, epoch,
				&cred->tc_dkey, 1, &cred->tc_iod,
				&cred->tc_sgl);
	} else { /* zero-copy */
		struct bio_sglist	*bsgl;
		daos_handle_t		 ioh;

		if (op_type == TS_DO_UPDATE)
			rc = vos_update_begin(ts_ctx.tsc_coh, ts_uoid, epoch,
					      &cred->tc_dkey, 1, &cred->tc_iod,
					      &ioh);
		else
			rc = vos_fetch_begin(ts_ctx.tsc_coh, ts_uoid, epoch,
					     &cred->tc_dkey, 1, &cred->tc_iod,
					     false, &ioh);
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
			       bsgl->bs_iovs[0].bi_buf,
			       bsgl->bs_iovs[0].bi_data_len);
		} else {
			memcpy(bsgl->bs_iovs[0].bi_buf,
			       cred->tc_sgl.sg_iovs[0].iov_buf,
			       cred->tc_sgl.sg_iovs[0].iov_len);
		}

		rc = bio_iod_post(vos_ioh2desc(ioh));
end:
		if (op_type == TS_DO_UPDATE)
			rc = vos_update_end(ioh, 0, &cred->tc_dkey, rc);
		else
			rc = vos_fetch_end(ioh, rc);
	}

	return rc;
}

static int
daos_update_or_fetch(daos_handle_t oh, enum ts_op_type op_type,
		     struct dts_io_credit *cred, daos_epoch_t epoch)
{
	int	rc;

	if (op_type == TS_DO_UPDATE) {
		rc = daos_obj_update(oh, DAOS_TX_NONE, &cred->tc_dkey, 1,
				     &cred->tc_iod, &cred->tc_sgl,
				     cred->tc_evp);
	} else {
		rc = daos_obj_fetch(oh, DAOS_TX_NONE, &cred->tc_dkey, 1,
				    &cred->tc_iod, &cred->tc_sgl, NULL,
				    cred->tc_evp);
	}
	return rc;
}

static void
set_value_buffer(char *buffer, int idx)
{
	/* Sets a pattern of Aa, Bb, ..., Yy, Zz, Aa, ... */
	buffer[0] = 'A' + idx % 26;
	buffer[1] = 'a' + idx % 26;
	buffer[TEST_VAL_SIZE - 1] = 0;
}

static int
akey_update_or_fetch(daos_handle_t oh, enum ts_op_type op_type,
		     char *dkey, char *akey, daos_epoch_t *epoch,
		     int *indices, int idx, char *verify_buff)
{
	struct dts_io_credit *cred;
	daos_iod_t	     *iod;
	daos_sg_list_t	     *sgl;
	daos_recx_t	     *recx;
	int		      vsize = ts_ctx.tsc_cred_vsize;
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
	daos_iov_set(&cred->tc_dkey, cred->tc_dbuf,
			strlen(cred->tc_dbuf));

	/* setup I/O descriptor */
	memcpy(cred->tc_abuf, akey, DTS_KEY_LEN);
	daos_iov_set(&iod->iod_name, cred->tc_abuf,
			strlen(cred->tc_abuf));
	iod->iod_size = vsize;
	recx->rx_nr  = 1;
	if (ts_single) {
		iod->iod_type = DAOS_IOD_SINGLE;
	} else {
		iod->iod_type = DAOS_IOD_ARRAY;
		iod->iod_size = 1;
		recx->rx_nr  = vsize;
		recx->rx_idx = ts_overwrite ? 0 : indices[idx] * vsize;
	}

	iod->iod_nr    = 1;
	iod->iod_recxs = recx;

	if (op_type == TS_DO_UPDATE) {
		/* initialize value buffer and setup sgl */
		set_value_buffer(cred->tc_vbuf, idx);
	} else {
		/* Clear the buffer for fetch */
		memset(cred->tc_vbuf, 0, vsize);
	}

	daos_iov_set(&cred->tc_val, cred->tc_vbuf, vsize);
	sgl->sg_iovs = &cred->tc_val;
	sgl->sg_nr = 1;

	if (ts_mode == TS_MODE_VOS)
		rc = vos_update_or_fetch(op_type, cred, *epoch);
	else
		rc = daos_update_or_fetch(oh, op_type, cred, *epoch);

	if (rc != 0) {
		fprintf(stderr, "%s failed. rc=%d, epoch=%"PRIu64"\n",
			op_type == TS_DO_FETCH ? "Fetch" : "Update",
			rc, *epoch);
		return rc;
	}

	/* overwrite can replace orignal data and reduce space
	 * consumption.
	 */
	if (!ts_overwrite)
		(*epoch)++;

	if (verify_buff != NULL)
		memcpy(verify_buff, cred->tc_vbuf, TEST_VAL_SIZE);

	return rc;
}

static int
dkey_update_or_fetch(daos_handle_t oh, enum ts_op_type op_type, char *dkey,
		     daos_epoch_t *epoch)
{
	int		*indices;
	char		 akey[DTS_KEY_LEN];
	int		 i;
	int		 j;
	int		 rc = 0;

	indices = dts_rand_iarr_alloc(ts_recx_p_akey, 0);
	D_ASSERT(indices != NULL);

	for (i = 0; i < ts_akey_p_dkey; i++) {
		dts_key_gen(akey, DTS_KEY_LEN, "walker");
		for (j = 0; j < ts_recx_p_akey; j++) {
			rc = akey_update_or_fetch(oh, op_type, dkey, akey,
						  epoch, indices, j, NULL);
			if (rc)
				goto failed;
		}
	}

failed:
	D_FREE(indices);
	return rc;
}

static int
objects_update(d_rank_t rank)
{
	int		i;
	int		j;
	int		rc;
	daos_epoch_t	epoch = 0;

	dts_reset_key();

	if (!ts_overwrite)
		++epoch;

	for (i = 0; i < ts_obj_p_cont; i++) {
		ts_oid = dts_oid_gen(ts_class, 0, ts_ctx.tsc_mpi_rank);
		if (ts_class == DAOS_OC_R2S_SPEC_RANK)
			ts_oid = dts_oid_set_rank(ts_oid, rank);

		if (ts_mode == TS_MODE_DAOS) {
			rc = daos_obj_open(ts_ctx.tsc_coh, ts_oid,
					   DAOS_OO_RW, &ts_ohs[i], NULL);
			if (rc) {
				fprintf(stderr, "object open failed\n");
				return -1;
			}
		} else {
			memset(&ts_uoid, 0, sizeof(ts_uoid));
			ts_uoid.id_pub = ts_oid;
		}

		for (j = 0; j < ts_dkey_p_obj; j++) {
			char	 dkey[DTS_KEY_LEN];

			dts_key_gen(dkey, DTS_KEY_LEN, "blade");
			rc = dkey_update_or_fetch(ts_ohs[i], TS_DO_UPDATE, dkey,
						  &epoch);
			if (rc)
				return rc;
		}
	}
	rc = dts_credit_drain(&ts_ctx);

	return rc;
}

static int
dkey_verify(daos_handle_t oh, char *dkey, daos_epoch_t *epoch)
{
	int	 i;
	int	*indices;
	char	 ground_truth[TEST_VAL_SIZE];
	char	 test_string[TEST_VAL_SIZE];
	char	 akey[DTS_KEY_LEN];
	int	 rc = 0;

	indices = dts_rand_iarr_alloc(ts_recx_p_akey, 0);
	D_ASSERT(indices != NULL);
	dts_key_gen(akey, DTS_KEY_LEN, "walker");

	for (i = 0; i < ts_recx_p_akey; i++) {
		set_value_buffer(ground_truth, i);
		rc = akey_update_or_fetch(oh, TS_DO_FETCH, dkey, akey, epoch,
					  indices, i, test_string);
		if (rc)
			goto failed;
		if (memcmp(test_string, ground_truth, TEST_VAL_SIZE) != 0) {
			D_PRINT("MISMATCH! ground_truth=%s, test_string=%s\n",
				ground_truth, test_string);
			rc = -1;
			goto failed;
		}
	}
failed:
	D_FREE(indices);
	return rc;
}

static int
objects_verify(void)
{
	int		i;
	int		j;
	int		k;
	int		rc = 0;
	char		dkey[DTS_KEY_LEN];
	daos_epoch_t	epoch = 0;

	dts_reset_key();
	if (!ts_overwrite)
		++epoch;
	for (i = 0; i < ts_obj_p_cont; i++) {
		for (j = 0; j < ts_dkey_p_obj; j++) {
			dts_key_gen(dkey, DTS_KEY_LEN, "blade");
			for (k = 0; k < ts_akey_p_dkey; k++) {
				rc = dkey_verify(ts_ohs[i], dkey, &epoch);
				if (rc != 0)
					return rc;
			}
		}
	}
	return rc;
}

static int
objects_verify_close(void)
{
	int i;
	int rc = 0;

	if (ts_verify_fetch) {
		rc = objects_verify();
		fprintf(stdout, "Fetch verification: %s\n", rc ? "Failed" :
			"Success");
	}

	for (i = 0; ts_mode == TS_MODE_DAOS && i < ts_obj_p_cont; i++) {
		rc = daos_obj_close(ts_ohs[i], NULL);
		D_ASSERT(rc == 0);
	}
	return 0;
}

static int
objects_fetch(d_rank_t rank)
{
	int		i;
	int		j;
	int		rc = 0;
	daos_epoch_t	epoch = 0;

	dts_reset_key();
	if (!ts_overwrite)
		++epoch;

	for (i = 0; i < ts_obj_p_cont; i++) {
		for (j = 0; j < ts_dkey_p_obj; j++) {
			char	 dkey[DTS_KEY_LEN];

			dts_key_gen(dkey, DTS_KEY_LEN, "blade");
			rc = dkey_update_or_fetch(ts_ohs[i], TS_DO_FETCH, dkey,
						  &epoch);
			if (rc != 0)
				return rc;
		}
	}
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

	rc = vos_iter_prepare(type, param, &ih);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			rc = 0;
		else
			D_ERROR("Failed to prepare d-key iterator: %d\n", rc);
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
ts_iterate_records_internal(d_rank_t rank)
{
	vos_iter_param_t	param = {};
	int			rc = 0;

	assert_int_equal(ts_class, DAOS_OC_RAW);

	/* prepare iterate parameters */
	param.ip_hdl = ts_ctx.tsc_coh;
	param.ip_oid = ts_uoid;

	param.ip_epr.epr_lo = 0;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	param.ip_epc_expr = VOS_IT_EPC_RE;

	rc = ts_iterate_internal(VOS_ITER_DKEY, &param, iter_dkey_cb);
	return rc;
}

static int
ts_write_perf(double *start_time, double *end_time)
{
	int	rc;

	*start_time = dts_time_now();
	rc = objects_update(RANK_ZERO);
	*end_time = dts_time_now();
	if (rc)
		return rc;

	rc = objects_verify_close();
	return rc;
}

static int
ts_fetch_perf(double *start_time, double *end_time)
{
	int	rc;

	rc = objects_update(RANK_ZERO);
	if (rc)
		return rc;
	*start_time = dts_time_now();
	rc = objects_fetch(RANK_ZERO);
	*end_time = dts_time_now();
	if (rc)
		return rc;

	rc = objects_verify_close();
	return rc;
}

static int
ts_iterate_perf(double *start_time, double *end_time)
{
	int	rc;

	rc = objects_update(RANK_ZERO);
	if (rc)
		return rc;
	*start_time = dts_time_now();
	rc = ts_iterate_records_internal(RANK_ZERO);
	*end_time = dts_time_now();
	return rc;
}

static int
ts_update_fetch_perf(double *start_time, double *end_time)
{
	int	rc;

	*start_time = dts_time_now();
	rc = objects_update(RANK_ZERO);
	if (rc)
		return rc;
	rc = objects_fetch(RANK_ZERO);
	*end_time = dts_time_now();
	if (rc)
		return rc;

	rc = objects_verify_close();
	return rc;
}

static int
ts_exclude_server(d_rank_t rank)
{
	struct d_tgt_list	targets;
	int			tgt = -1;
	int			rc;

	/** exclude from the pool */
	targets.tl_nr = 1;
	targets.tl_ranks = &rank;
	targets.tl_tgts = &tgt;
	rc = daos_pool_tgt_exclude(ts_ctx.tsc_pool_uuid, NULL, &ts_ctx.tsc_svc,
				   &targets, NULL);

	return rc;
}

static int
ts_add_server(d_rank_t rank)
{
	struct d_tgt_list	targets;
	int			tgt = -1;
	int			rc;

	/** exclude from the pool */
	targets.tl_nr = 1;
	targets.tl_ranks = &rank;
	targets.tl_tgts = &tgt;
	rc = daos_pool_add_tgt(ts_ctx.tsc_pool_uuid, NULL, &ts_ctx.tsc_svc,
			       &targets, NULL);
	return rc;
}

static void
ts_rebuild_wait()
{
	daos_pool_info_t	   pinfo;
	struct daos_rebuild_status *rst = &pinfo.pi_rebuild_st;
	int			   rc = 0;

	while (1) {
		memset(&pinfo, 0, sizeof(pinfo));
		rc = daos_pool_query(ts_ctx.tsc_poh, NULL, &pinfo, NULL, NULL);
		if (rst->rs_done || rc != 0) {
			fprintf(stderr, "Rebuild (ver=%d) is done %d/%d\n",
				rst->rs_version, rc, rst->rs_errno);
			break;
		}
		sleep(2);
	}
}

static int
ts_rebuild_perf(double *start_time, double *end_time)
{
	int rc;

	/* prepare the record */
	ts_class = DAOS_OC_R2S_SPEC_RANK;
	rc = objects_update(RANK_ZERO);
	if (rc)
		return rc;

	if (ts_rebuild_only_iteration)
		daos_mgmt_set_params(NULL, -1, DSS_KEY_FAIL_LOC,
				     DAOS_REBUILD_NO_REBUILD,
				     0, NULL);
	else if (ts_rebuild_no_update)
		daos_mgmt_set_params(NULL, -1, DSS_KEY_FAIL_LOC,
				     DAOS_REBUILD_NO_UPDATE,
				     0, NULL);

	rc = ts_exclude_server(RANK_ZERO);
	if (rc)
		return rc;

	*start_time = dts_time_now();
	ts_rebuild_wait();
	*end_time = dts_time_now();

	rc = ts_add_server(RANK_ZERO);

	daos_mgmt_set_params(NULL, -1, DSS_KEY_FAIL_LOC, 0, 0, NULL);

	return rc;
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
		return "VOS (storage only)";
	case DAOS_OC_ECHO_TINY_RW:
		return "ECHO TINY (network only, non-replica)";
	case DAOS_OC_ECHO_R2S_RW:
		return "ECHO R2S (network only, 2-replica)";
	case DAOS_OC_ECHO_R3S_RW:
		return "ECHO R3S (network only, 3-replica)";
	case DAOS_OC_ECHO_R4S_RW:
		return "ECHO R4S (network only, 4-replica)";
	case DAOS_OC_TINY_RW:
		return "DAOS TINY (full stack, non-replica)";
	case DAOS_OC_R2S_RW:
		return "DAOS R2S (full stack, 2 replica)";
	case DAOS_OC_R3S_RW:
		return "DAOS R3S (full stack, 3 replica)";
	case DAOS_OC_R4S_RW:
		return "DAOS R4S (full stack, 4 replics)";
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
	Tyes of test, it can be 'vos' and 'daos'.\n\
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
-c TINY|R2S|R3S|R4S\n\
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
-U	Only run update performance test.\n\
\n\
-F	Only run fetch performance test. This does an update first, but only\n\
	measures the time for the fetch portion.\n\
\n\
-v	Verify fetch. Checks that what was read from the filesystem is what\n\
	was written to it. This verifcation is not part of timed\n\
	performance measurement. This is turned off by default.\n\
\n\
-R	Only run rebuild performance test.\n\
\n\
-B	Profile performance of both update and fetch.\n\
\n\
-I	Only run iterate performance test. Only runs in vos mode.\n\
\n\
-n	Only run iterate performance test but with nesting iterator\n\
	enable.  This can only run in vos mode.\n\
\n\
-f pathname\n\
	Full path name of the VOS file.\n\
\n\
-w	Pause after initialization for attaching debugger or analysis\n\
	tool.\n");
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
	{ "nest_iter",	no_argument,		NULL,	'n' },
	{ "file",	required_argument,	NULL,	'f' },
	{ "help",	no_argument,		NULL,	'h' },
	{ "verify",	no_argument,		NULL,	'v' },
	{ "wait",	no_argument,		NULL,	'w' },
	{ NULL,		0,			NULL,	0   },
};

void show_result(double now, double then, int vsize, char *test_name)
{
	double		duration, agg_duration;
	double		first_start;
	double		last_end;
	double		duration_max;
	double		duration_min;
	double		duration_sum;

	duration = now - then;

	if (ts_ctx.tsc_mpi_size > 1) {
		MPI_Reduce(&then, &first_start, 1, MPI_DOUBLE,
			   MPI_MIN, 0, MPI_COMM_WORLD);
		MPI_Reduce(&now, &last_end, 1, MPI_DOUBLE,
			   MPI_MAX, 0, MPI_COMM_WORLD);
	} else {
		first_start = then;
		last_end = now;
	}

	agg_duration = last_end - first_start;

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
		latency = (agg_duration * 1000 * 1000) / total;
		bandwidth = (rate * vsize) / (1024 * 1024);

		fprintf(stdout, "%s successfully completed:\n"
			"\tduration : %-10.6f sec\n"
			"\tbandwith : %-10.3f MB/sec\n"
			"\trate     : %-10.2f IO/sec\n"
			"\tlatency  : %-10.3f us "
			"(nonsense if credits > 1)\n",
			test_name, agg_duration, bandwidth, rate, latency);

		fprintf(stdout, "Duration across processes:\n");
		fprintf(stdout, "\tMAX duration : %-10.6f sec\n",
			duration_max);
		fprintf(stdout, "\tMIN duration : %-10.6f sec\n",
			duration_min);
		fprintf(stdout, "\tAverage duration : %-10.6f sec\n",
			duration_sum / ts_ctx.tsc_mpi_size);
	}
}
enum {
	UPDATE_TEST = 0,
	FETCH_TEST,
	ITERATE_TEST,
	REBUILD_TEST,
	UPDATE_FETCH_TEST,
	TEST_SIZE,
};

static int (*perf_tests[TEST_SIZE])(double *start, double *end);

char	*perf_tests_name[] = {
	"update",
	"fetch",
	"iterate",
	"rebuild",
	"update and fetch"
};

int
main(int argc, char **argv)
{
	struct timeval	tv;
	daos_size_t	scm_size = (2ULL << 30); /* default pool SCM size */
	daos_size_t	nvme_size = (8ULL << 30); /* default pool NVMe size */
	int		credits   = -1;	/* sync mode */
	int		vsize	   = 32;	/* default value size */
	d_rank_t	svc_rank  = 0;	/* pool service rank */
	double		then;
	double		now;
	int		rc;
	int		i;
	bool		pause = false;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &ts_ctx.tsc_mpi_rank);
	MPI_Comm_size(MPI_COMM_WORLD, &ts_ctx.tsc_mpi_size);

	gettimeofday(&tv, NULL);
	srand(tv.tv_usec);

	memset(ts_pmem_file, 0, sizeof(ts_pmem_file));
	while ((rc = getopt_long(argc, argv,
				 "P:N:T:C:c:o:d:a:r:nAs:ztf:hUFRBvIiuw",
				 ts_ops, NULL)) != -1) {
		char	*endp;

		switch (rc) {
		default:
			fprintf(stderr, "Unknown option %c\n", rc);
			return -1;
		case 'w':
			pause = true;
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
					ts_class = DAOS_OC_TINY_RW;
			}
			break;
		case 'C':
			credits = strtoul(optarg, &endp, 0);
			break;
		case 'c':
			if (!strcasecmp(optarg, "R4S")) {
				ts_class = DAOS_OC_R4S_RW;
			} else if (!strcasecmp(optarg, "R3S")) {
				ts_class = DAOS_OC_R3S_RW;
			} else if (!strcasecmp(optarg, "R2S")) {
				ts_class = DAOS_OC_R2S_RW;
			} else if (!strcasecmp(optarg, "TINY")) {
				ts_class = DAOS_OC_TINY_RW;
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
		case 's':
			vsize = strtoul(optarg, &endp, 0);
			vsize = ts_val_factor(vsize, *endp);
			if (vsize < TEST_VAL_SIZE) {
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
		case 'U':
			perf_tests[UPDATE_TEST] = ts_write_perf;
			break;
		case 'F':
			perf_tests[FETCH_TEST] = ts_fetch_perf;
			break;
		case 'R':
			perf_tests[REBUILD_TEST] = ts_rebuild_perf;
			break;
		case 'i':
			ts_rebuild_only_iteration = true;
			break;
		case 'u':
			ts_rebuild_no_update = true;
			break;
		case 'B':
			perf_tests[UPDATE_FETCH_TEST] = ts_update_fetch_perf;
			break;
		case 'v':
			ts_verify_fetch = true;
			break;
		case 'n':
			ts_nest_iterator = true;
		case 'I':
			perf_tests[ITERATE_TEST] = ts_iterate_perf;
			break;
		case 'h':
			if (ts_ctx.tsc_mpi_rank == 0)
				ts_print_usage();
			return 0;
		}
	}

	/* Convert object classes for echo mode.
	 * NB: we can also run in echo mode for arbitrary object class by
	 * setting DAOS_IO_BYPASS="target" while starting server.
	 */
	if (ts_mode == TS_MODE_ECHO) {
		if (ts_class == DAOS_OC_R4S_RW)
			ts_class = DAOS_OC_ECHO_R4S_RW;
		else if (ts_class == DAOS_OC_R3S_RW)
			ts_class = DAOS_OC_ECHO_R3S_RW;
		else if (ts_class == DAOS_OC_R2S_RW)
			ts_class = DAOS_OC_ECHO_R2S_RW;
		else
			ts_class = DAOS_OC_ECHO_TINY_RW;
	}

	/* It will run write tests by default */
	if (perf_tests[REBUILD_TEST] == NULL &&
	    perf_tests[FETCH_TEST] == NULL && perf_tests[UPDATE_TEST] == NULL &&
	    perf_tests[UPDATE_FETCH_TEST] == NULL &&
	    perf_tests[ITERATE_TEST] == NULL)
		perf_tests[UPDATE_TEST] = ts_write_perf;

	if ((perf_tests[FETCH_TEST] != NULL ||
	     perf_tests[UPDATE_FETCH_TEST] != NULL) && ts_overwrite) {
		fprintf(stdout, "Note: Fetch tests are incompatible with "
			"the overwrite option (-t).\n      Remove the -t option"
			" and try again.\n");
		return -1;
	}

	if (perf_tests[REBUILD_TEST] && ts_class != DAOS_OC_TINY_RW) {
		fprintf(stderr, "rebuild can only run with -T \"daos\"\n");
		if (ts_ctx.tsc_mpi_rank == 0)
			ts_print_usage();
		return -1;
	}

	if (perf_tests[ITERATE_TEST] && ts_class != DAOS_OC_RAW) {
		fprintf(stderr, "iterate can only run with -T \"vos\"\n");
		if (ts_ctx.tsc_mpi_rank == 0)
			ts_print_usage();
		return -1;
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

	if (vsize <= sizeof(int))
		vsize = sizeof(int);

	if (ts_ctx.tsc_mpi_rank == 0 || ts_mode == TS_MODE_VOS) {
		uuid_generate(ts_ctx.tsc_pool_uuid);
		uuid_generate(ts_ctx.tsc_cont_uuid);
	}

	if (ts_mode == TS_MODE_VOS) {
		ts_ctx.tsc_cred_nr = -1; /* VOS can only support sync mode */
		if (strlen(ts_pmem_file) == 0)
			strcpy(ts_pmem_file, "/mnt/daos/vos_perf.pmem");

		ts_ctx.tsc_pmem_file = ts_pmem_file;
	} else {
		ts_ctx.tsc_cred_nr = credits;
		ts_ctx.tsc_svc.rl_nr = 1;
		ts_ctx.tsc_svc.rl_ranks  = &svc_rank;
	}
	ts_ctx.tsc_cred_vsize	= vsize;
	ts_ctx.tsc_scm_size	= scm_size;
	ts_ctx.tsc_nvme_size	= nvme_size;

	if (ts_ctx.tsc_mpi_rank == 0) {
		fprintf(stdout,
			"Test :\n\t%s\n"
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
			"\tverify fetch  : %s\n"
			"\tVOS file      : %s\n",
			ts_class_name(),
			(unsigned int)(scm_size >> 20),
			(unsigned int)(nvme_size >> 20),
			credits,
			ts_obj_p_cont,
			ts_ctx.tsc_mpi_size,
			ts_dkey_p_obj,
			ts_akey_p_dkey,
			ts_recx_p_akey,
			ts_val_type(),
			vsize,
			ts_yes_or_no(ts_zero_copy),
			ts_yes_or_no(ts_overwrite),
			ts_yes_or_no(ts_verify_fetch),
			ts_mode == TS_MODE_VOS ? ts_pmem_file : "<NULL>");
	}

	ts_ohs = calloc(ts_obj_p_cont, sizeof(*ts_ohs));
	if (!ts_ohs) {
		fprintf(stderr, "failed to allocate %u open handles\n",
			ts_obj_p_cont);
		return -1;
	}

	rc = dts_ctx_init(&ts_ctx);
	if (rc)
		return -1;

	if (ts_ctx.tsc_mpi_rank == 0) {
		if (pause) {
			fprintf(stdout, "Ready to start...If you wish to"
				" attach a tool, do so now and then hit"
				" enter.\n");
			getc(stdin);
		}
		fprintf(stdout, "Started...\n");
	}

	MPI_Barrier(MPI_COMM_WORLD);

	for (i = 0; i < TEST_SIZE; i++) {
		if (perf_tests[i] == NULL)
			continue;

		rc = perf_tests[i](&then, &now);
		if (ts_ctx.tsc_mpi_size > 1) {
			int rc_g;

			MPI_Allreduce(&rc, &rc_g, 1, MPI_INT, MPI_MIN,
				      MPI_COMM_WORLD);
			rc = rc_g;
		}

		if (rc != 0) {
			fprintf(stderr, "Failed: %d\n", rc);
			break;
		}

		show_result(now, then, vsize, perf_tests_name[i]);
	}

	dts_ctx_fini(&ts_ctx);
	MPI_Finalize();
	free(ts_ohs);

	return 0;
}
