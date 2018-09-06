/**
 * (C) Copyright 2018 Intel Corporation.
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
#define WITHOUT_FETCH	(false)
#define WITH_FETCH	(true)
#define TEST_VAL_SIZE	(3)

enum ts_op_type_t {
	TS_DO_UPDATE = 0,
	TS_DO_FETCH
};

/* Test class, can be:
 *	vos  : pure storage
 *	ehco : pure network
 *	daos : full stack
 */
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
char			*ts_verification_buf;
uint64_t		 ts_ver_buf_idx;

uuid_t			 ts_cookie;		/* update cookie for VOS */
daos_handle_t		 ts_oh;			/* object open handle */
daos_obj_id_t		 ts_oid;		/* object ID */
daos_unit_oid_t		 ts_uoid;		/* object shard ID (for VOS) */

struct dts_context	 ts_ctx;

/* rebuild only with iteration */
bool			ts_rebuild_only_iteration = false;
/* rebuild without update */
bool			ts_rebuild_no_update = false;

static int
ts_vos_update_or_fetch(struct dts_io_credit *cred, daos_epoch_t epoch,
		       enum ts_op_type_t update_or_fetch)
{
	int	rc = 0;

	if (!ts_zero_copy) {
		if (update_or_fetch == TS_DO_UPDATE)
			rc = vos_obj_update(ts_ctx.tsc_coh, ts_uoid, epoch,
				ts_cookie, 0, &cred->tc_dkey, 1,
				&cred->tc_iod, &cred->tc_sgl);
		else
			rc = vos_obj_fetch(ts_ctx.tsc_coh, ts_uoid, epoch,
				&cred->tc_dkey, 1, &cred->tc_iod,
				&cred->tc_sgl);
	} else { /* zero-copy */
		struct eio_sglist	*esgl;
		daos_handle_t		 ioh;

		if (update_or_fetch == TS_DO_UPDATE)
			rc = vos_update_begin(ts_ctx.tsc_coh, ts_uoid, epoch,
					      &cred->tc_dkey, 1, &cred->tc_iod,
					      &ioh);
		else
			rc = vos_fetch_begin(ts_ctx.tsc_coh, ts_uoid, epoch,
					     &cred->tc_dkey, 1, &cred->tc_iod,
					     false, &ioh);
		if (rc)
			return rc;

		rc = eio_iod_prep(vos_ioh2desc(ioh));
		if (rc)
			goto end;

		esgl = vos_iod_sgl_at(ioh, 0);
		D_ASSERT(esgl != NULL);
		D_ASSERT(esgl->es_nr_out == 1);
		D_ASSERT(cred->tc_sgl.sg_nr == 1);

		if (update_or_fetch == TS_DO_FETCH) {
			memcpy(cred->tc_sgl.sg_iovs[0].iov_buf,
			       esgl->es_iovs[0].ei_buf,
			       esgl->es_iovs[0].ei_data_len);
		} else {
			memcpy(esgl->es_iovs[0].ei_buf,
			       cred->tc_sgl.sg_iovs[0].iov_buf,
			       cred->tc_sgl.sg_iovs[0].iov_len);
		}

		rc = eio_iod_post(vos_ioh2desc(ioh));
end:
		if (update_or_fetch == TS_DO_UPDATE)
			rc = vos_update_end(ioh, ts_cookie, 0, &cred->tc_dkey,
					    rc);
		else
			rc = vos_fetch_end(ioh, rc);
	}

	return rc;
}

static int
ts_daos_update(struct dts_io_credit *cred, daos_epoch_t epoch)
{
	int	rc;

	rc = daos_obj_update(ts_oh, epoch, &cred->tc_dkey, 1,
			     &cred->tc_iod, &cred->tc_sgl, cred->tc_evp);
	return rc;
}

static int
ts_daos_fetch(struct dts_io_credit *cred, daos_epoch_t epoch)
{
	int	rc;

	rc = daos_obj_fetch(ts_oh, epoch, &cred->tc_dkey, 1, &cred->tc_iod,
			    &cred->tc_sgl, NULL, cred->tc_evp);

	return rc;
}

static void
ts_verification_buf_append(char *the_string)
{
	memcpy(&ts_verification_buf[ts_ver_buf_idx], the_string,
		TEST_VAL_SIZE);
	ts_ver_buf_idx += TEST_VAL_SIZE;
}

static void
ts_set_value_buffer(char *buffer, int idx)
{
	/* Sets a pattern of Aa, Bb, ..., Yy, Zz, Aa, ... */
	buffer[0] = 'A' + idx % 26;
	buffer[1] = 'a' + idx % 26;
	buffer[TEST_VAL_SIZE - 1] = 0;
}

static int
ts_hold_epoch(daos_epoch_t *epoch)
{
	if (ts_ctx.tsc_mpi_rank == 0)
		return daos_epoch_hold(ts_ctx.tsc_coh, epoch, NULL, NULL);
	return 0;
}

static int
ts_key_update_or_fetch(enum ts_op_type_t update_or_fetch, bool with_fetch)
{
	int		*indices;
	char		 dkey_buf[DTS_KEY_LEN];
	char		 akey_buf[DTS_KEY_LEN];
	int		 vsize = ts_ctx.tsc_cred_vsize;
	int		 i;
	int		 j;
	int		 rc = 0;
	daos_epoch_t	 epoch = 0;

	indices = dts_rand_iarr_alloc(ts_recx_p_akey, 0);
	D_ASSERT(indices != NULL);

	dts_key_gen(dkey_buf, DTS_KEY_LEN, "blade");

	for (i = 0; i < ts_akey_p_dkey; i++) {

		dts_key_gen(akey_buf, DTS_KEY_LEN, "walker");

		for (j = 0; j < ts_recx_p_akey; j++) {
			struct dts_io_credit *cred;
			daos_iod_t	     *iod;
			daos_sg_list_t	     *sgl;
			daos_recx_t	     *recx;

			cred = dts_credit_take(&ts_ctx);
			if (!cred) {
				fprintf(stderr,
					"credit cannot be NULL for IO\n");
				rc = -1;
				D_GOTO(failed, rc);
			}

			iod  = &cred->tc_iod;
			sgl  = &cred->tc_sgl;
			recx = &cred->tc_recx;

			memset(iod, 0, sizeof(*iod));
			memset(sgl, 0, sizeof(*sgl));
			memset(recx, 0, sizeof(*recx));

			/* setup dkey */
			memcpy(cred->tc_dbuf, dkey_buf, DTS_KEY_LEN);
			daos_iov_set(&cred->tc_dkey, cred->tc_dbuf,
				     strlen(cred->tc_dbuf));

			/* setup I/O descriptor */
			memcpy(cred->tc_abuf, akey_buf, DTS_KEY_LEN);
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
				recx->rx_idx = ts_overwrite ? 0 : indices[j];
			}

			iod->iod_nr    = 1;
			iod->iod_recxs = recx;

			if (update_or_fetch == TS_DO_UPDATE) {
				/* initialize value buffer and setup sgl */
				ts_set_value_buffer(cred->tc_vbuf, j);
			} else {
				/* Clear the buffer for fetch */
				memset(cred->tc_vbuf, 0, vsize);
			}

			daos_iov_set(&cred->tc_val, cred->tc_vbuf, vsize);
			sgl->sg_iovs = &cred->tc_val;
			sgl->sg_nr = 1;

			/* overwrite can replace orignal data and reduce space
			 * consumption.
			 */
			if (!ts_overwrite)
				epoch++;

			if (ts_class == DAOS_OC_RAW) {
				rc = ts_vos_update_or_fetch(cred, epoch,
					update_or_fetch);
			} else {
				if (update_or_fetch == TS_DO_UPDATE) {
					if (with_fetch == WITH_FETCH) {
						rc = ts_hold_epoch(&epoch);
						if (rc)
							D_GOTO(failed, rc);
					}
					rc = ts_daos_update(cred, epoch);
				}
				else
					rc = ts_daos_fetch(cred, epoch);
			}

			if (rc)
				D_GOTO(failed, rc);

			if (ts_verify_fetch && update_or_fetch == TS_DO_FETCH)
				ts_verification_buf_append(cred->tc_vbuf);

			/* Flush and commit, if needed */
			if (update_or_fetch == TS_DO_UPDATE &&
			    with_fetch == WITH_FETCH) {
				if (ts_class != DAOS_OC_RAW &&
				    ts_ctx.tsc_mpi_rank == 0) {
					rc = daos_epoch_flush(ts_ctx.tsc_coh,
						epoch, NULL, NULL);
					if (rc)
						D_GOTO(failed, rc);

					rc = daos_epoch_commit(ts_ctx.tsc_coh,
						epoch, NULL, NULL);
					if (rc)
						D_GOTO(failed, rc);
				}
			}

			if (rc != 0) {
				fprintf(stderr, "%s failed: %d\n",
					update_or_fetch ? "Fetch" : "Update",
					rc);
				D_GOTO(failed, rc);
			}
		}
	}
failed:
	free(indices);
	return rc;
}

static int
ts_write_records_internal(d_rank_t rank, bool with_fetch)
{
	int i;
	int j;
	int rc;

	dts_reset_key();
	for (i = 0; i < ts_obj_p_cont; i++) {
		ts_oid = dts_oid_gen(ts_class, 0, ts_ctx.tsc_mpi_rank);
		if (ts_class == DAOS_OC_R2S_SPEC_RANK)
			ts_oid = dts_oid_set_rank(ts_oid, rank);
		for (j = 0; j < ts_dkey_p_obj; j++) {
			if (ts_class != DAOS_OC_RAW) {
				rc = daos_obj_open(ts_ctx.tsc_coh, ts_oid, 1,
						   DAOS_OO_RW, &ts_oh, NULL);
				if (rc) {
					fprintf(stderr, "object open failed\n");
					return -1;
				}
			} else {
				memset(&ts_uoid, 0, sizeof(ts_uoid));
				ts_uoid.id_pub = ts_oid;
			}

			rc = ts_key_update_or_fetch(TS_DO_UPDATE, with_fetch);
			if (rc)
				return rc;

			if (ts_class != DAOS_OC_RAW &&
				with_fetch == WITHOUT_FETCH) {
				rc = daos_obj_close(ts_oh, NULL);
				if (rc)
					return rc;
			}

		}
	}

	rc = dts_credit_drain(&ts_ctx);

	return rc;
}

static int
ts_verify_recx_p_akey()
{
	int	i;
	char	ground_truth[TEST_VAL_SIZE];

	for (i = 0; i < ts_recx_p_akey; i++) {
		ts_set_value_buffer(ground_truth, i);

		if (memcmp(&ts_verification_buf[ts_ver_buf_idx],
			   ground_truth, TEST_VAL_SIZE) != 0) {
			return -1;
		}
		ts_ver_buf_idx += TEST_VAL_SIZE;
	}
	return 0;
}

static int
ts_verify_all_fetches(void)
{
	int	i;
	int	j;
	int	k;

	ts_ver_buf_idx = 0;
	for (i = 0; i < ts_obj_p_cont; i++) {
		for (j = 0; j < ts_dkey_p_obj; j++) {
			for (k = 0; k < ts_akey_p_dkey; k++) {
				if (ts_verify_recx_p_akey() != 0)
					return -1;
			}
		}
	}
	return 0;
}

static int
ts_read_records_internal(d_rank_t rank)
{
	int i;
	int j;
	int rc = 0;

	dts_reset_key();
	for (i = 0; i < ts_obj_p_cont; i++) {
		for (j = 0; j < ts_dkey_p_obj; j++)
			rc = ts_key_update_or_fetch(TS_DO_FETCH, WITH_FETCH);
	}
	if (ts_class != DAOS_OC_RAW)
		rc = daos_obj_close(ts_oh, NULL);

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
	/* iterate akey */
	rc = ts_iterate_internal(VOS_ITER_AKEY, param, iter_akey_cb);

	return rc;
}

/* Iterate all of dkey/akey/record */
static int
ts_iterate_records_internal(d_rank_t rank)
{
	vos_iter_param_t	param;
	int			rc = 0;

	assert_int_equal(ts_class, DAOS_OC_RAW);

	/* prepare iterate parameters */
	memset(&param, 0, sizeof(param));
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
	rc = ts_write_records_internal(RANK_ZERO, WITHOUT_FETCH);
	*end_time = dts_time_now();
	return rc;
}

static int
ts_fetch_perf(double *start_time, double *end_time)
{
	int	rc;

	rc = ts_write_records_internal(RANK_ZERO, WITH_FETCH);
	if (rc)
		return rc;
	*start_time = dts_time_now();
	rc = ts_read_records_internal(RANK_ZERO);
	*end_time = dts_time_now();
	if (rc)
		return rc;
	if (ts_verify_fetch) {
		rc = ts_verify_all_fetches();
		fprintf(stdout, "Fetch verification: %s\n", rc ? "Failed" :
			"Success");
	}
	return rc;
}

static int
ts_iterate_perf(double *start_time, double *end_time)
{
	int	rc;

	rc = ts_write_records_internal(RANK_ZERO, WITH_FETCH);
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
	rc = ts_write_records_internal(RANK_ZERO, WITH_FETCH);
	if (rc)
		return rc;
	rc = ts_read_records_internal(RANK_ZERO);
	*end_time = dts_time_now();
	return rc;
}

static int
ts_exclude_server(d_rank_t rank)
{
	d_rank_list_t	targets;
	int		rc;

	/** exclude from the pool */
	targets.rl_nr = 1;
	targets.rl_ranks = &rank;
	rc = daos_pool_exclude(ts_ctx.tsc_pool_uuid, NULL, &ts_ctx.tsc_svc,
			       &targets, NULL);

	return rc;
}

static int
ts_add_server(d_rank_t rank)
{
	d_rank_list_t	targets;
	int		rc;

	/** exclude from the pool */
	targets.rl_nr = 1;
	targets.rl_ranks = &rank;
	rc = daos_pool_tgt_add(ts_ctx.tsc_pool_uuid, NULL, &ts_ctx.tsc_svc,
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
		rc = daos_pool_query(ts_ctx.tsc_poh, NULL, &pinfo, NULL);
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
	rc = ts_write_records_internal(RANK_ZERO, WITHOUT_FETCH);
	if (rc)
		return rc;

	if (ts_rebuild_only_iteration)
		daos_mgmt_params_set(NULL, -1, DSS_KEY_FAIL_LOC,
				     DAOS_REBUILD_NO_REBUILD | DAOS_FAIL_VALUE,
				     NULL);
	else if (ts_rebuild_no_update)
		daos_mgmt_params_set(NULL, -1, DSS_KEY_FAIL_LOC,
				     DAOS_REBUILD_NO_UPDATE | DAOS_FAIL_VALUE,
				     NULL);

	rc = ts_exclude_server(RANK_ZERO);
	if (rc)
		return rc;

	*start_time = dts_time_now();
	ts_rebuild_wait();
	*end_time = dts_time_now();

	rc = ts_add_server(RANK_ZERO);

	daos_mgmt_params_set(NULL, -1, DSS_KEY_FAIL_LOC, 0, NULL); 

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
	case DAOS_OC_ECHO_RW:
		return "ECHO (network only)";
	case DAOS_OC_TINY_RW:
		return "DAOS (full stack)";
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
	Pool size, which can have M (megatbytes)or G (gigabytes) as postfix\n\
	of number. E.g. -P 512M, -P 8G.\n\
\n\
-T vos|echo|daos\n\
	Tyes of test, it can be 'vos', 'echo' and 'daos'.\n\
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
-I	Only run iterate performance test. This can only in vos mode.\n\
\n\
-f pathname\n\
	Full path name of the VOS file.\n");
}

static struct option ts_ops[] = {
	{ "pool",	required_argument,	NULL,	'P' },
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
	{ "file",	required_argument,	NULL,	'f' },
	{ "help",	no_argument,		NULL,	'h' },
	{ "verify",	no_argument,		NULL,	'v' },
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
	daos_size_t	pool_size = (2ULL << 30); /* default pool size */
	int		credits   = -1;	/* sync mode */
	int		vsize	   = 32;	/* default value size */
	d_rank_t	svc_rank  = 0;	/* pool service rank */
	double		then;
	double		now;
	int		rc;
	int		i;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &ts_ctx.tsc_mpi_rank);
	MPI_Comm_size(MPI_COMM_WORLD, &ts_ctx.tsc_mpi_size);

	memset(ts_pmem_file, 0, sizeof(ts_pmem_file));
	while ((rc = getopt_long(argc, argv, "P:T:C:o:d:a:r:As:ztf:hUFRBvIiu",
				 ts_ops, NULL)) != -1) {
		char	*endp;

		switch (rc) {
		default:
			fprintf(stderr, "Unknown option %c\n", rc);
			return -1;
		case 'T':
			if (!strcasecmp(optarg, "echo")) {
				/* just network, no storage */
				ts_class = DAOS_OC_ECHO_RW;

			} else if (!strcasecmp(optarg, "daos")) {
				/* full stack: network + storage */
				ts_class = DAOS_OC_TINY_RW;

			} else if (!strcasecmp(optarg, "vos")) {
				/* pure storage */
				ts_class = DAOS_OC_RAW;

			} else {
				if (ts_ctx.tsc_mpi_rank == 0)
					ts_print_usage();
				return -1;
			}
			break;
		case 'C':
			credits = strtoul(optarg, &endp, 0);
			break;
		case 'P':
			pool_size = strtoul(optarg, &endp, 0);
			pool_size = ts_val_factor(pool_size, *endp);
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
		case 'I':
			perf_tests[ITERATE_TEST] = ts_iterate_perf;
			break;
		case 'h':
			if (ts_ctx.tsc_mpi_rank == 0)
				ts_print_usage();
			return 0;
		}
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

	if (ts_ctx.tsc_mpi_rank == 0 || ts_class == DAOS_OC_RAW) {
		uuid_generate(ts_ctx.tsc_pool_uuid);
		uuid_generate(ts_ctx.tsc_cont_uuid);
	}

	if (ts_class == DAOS_OC_RAW) {
		uuid_generate(ts_cookie);
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
	ts_ctx.tsc_pool_size	= pool_size;

	if (ts_ctx.tsc_mpi_rank == 0) {
		fprintf(stdout,
			"Test :\n\t%s\n"
			"Parameters :\n"
			"\tpool size     : %u MB\n"
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
			(unsigned int)(pool_size >> 20),
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
			ts_class == DAOS_OC_RAW ? ts_pmem_file : "<NULL>");
	}

	if (ts_verify_fetch) {
		/* Allocate memory for the verification buffer */
		ts_verification_buf = malloc(ts_obj_p_cont * ts_dkey_p_obj *
					ts_akey_p_dkey * ts_recx_p_akey *
					TEST_VAL_SIZE);
	}

	rc = dts_ctx_init(&ts_ctx);
	if (rc)
		return -1;

	if (ts_ctx.tsc_mpi_rank == 0)
		fprintf(stdout, "Started...\n");

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
	free(ts_verification_buf);
	dts_ctx_fini(&ts_ctx);
	MPI_Finalize();

	return 0;
}
