/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDK-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * This provides a simple example for how to use the data filter capabilities
 * in DAOS.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <daos.h>
#include "pipeline_common.h"

#define NR_RECXS         4
#define FSIZE            15
#define NUM_DKEYS        1024
#define NR_IODS_PER_DKEY 1

static daos_handle_t poh; /** pool */
static daos_handle_t coh; /** container */
static daos_handle_t oh;  /** object */
static char          field[] = "Array";
static time_t        ts;
static void
insert_example_records(void)
{
	int         rc;
	d_iov_t     dkey;
	d_sg_list_t sgl;
	d_iov_t     sg_iovs[NR_RECXS];
	daos_iod_t  iod;
	daos_recx_t recx;
	uint32_t    i;
	char        fname[FSIZE];
	time_t      atime, ctime, mtime;
	mode_t      mode;

	for (i = 0; i < NUM_DKEYS; i++) {
		int j = 0;

		if (i < 10)
			sprintf(fname, "file.0%d", i);
		else
			sprintf(fname, "file.%d", i);

		/** set dkey for record */
		d_iov_set(&dkey, &fname, strlen(fname));

		mode = S_IWUSR | S_IRUSR;
		if (i % 10 == 0)
			mode |= S_IFDIR;
		else
			mode |= S_IFREG;

		ctime = time(NULL);
		mtime = ctime;
		atime = ctime;

		if (i == 50) {
			sleep(1);
			ts = time(NULL);
			sleep(1);
		}

		d_iov_set(&sg_iovs[j++], &mode, sizeof(mode_t));
		d_iov_set(&sg_iovs[j++], &atime, sizeof(time_t));
		d_iov_set(&sg_iovs[j++], &mtime, sizeof(time_t));
		d_iov_set(&sg_iovs[j++], &ctime, sizeof(time_t));

		sgl.sg_nr     = NR_RECXS;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs   = sg_iovs;

		d_iov_set(&iod.iod_name, (void *)field, strlen(field));
		iod.iod_nr    = 1;
		iod.iod_size  = 1;
		recx.rx_idx   = 0;
		recx.rx_nr    = sizeof(time_t) * 3 + sizeof(mode_t);
		iod.iod_recxs = &recx;
		iod.iod_type  = DAOS_IOD_ARRAY;

		rc            = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
		ASSERT(rc == 0, "Obj update failed with %d", rc);
	}
}

#define BINARY_F "DAOS_FILTER_TYPE_BINARY"
#define DKEY_F   "DAOS_FILTER_DKEY"
#define AKEY_F   "DAOS_FILTER_AKEY"
#define CONST_F  "DAOS_FILTER_CONST"
#define INT8_F   "DAOS_FILTER_TYPE_UINTEGER8"
#define INT4_F   "DAOS_FILTER_TYPE_UINTEGER4"
#define LIKE_F   "DAOS_FILTER_FUNC_LIKE"
#define GT_F     "DAOS_FILTER_FUNC_GT"
#define EQ_F     "DAOS_FILTER_FUNC_EQ"
#define BA_F     "DAOS_FILTER_FUNC_BITAND"
#define AND_F    "DAOS_FILTER_FUNC_AND"
#define OR_F     "DAOS_FILTER_FUNC_OR"
#define COND_F   "DAOS_FILTER_CONDITION"
#define NAME_F   "%.9%"

static mode_t      constant1 = S_IFMT;
static mode_t      constant2 = S_IFDIR;

static d_iov_t     dkey_iov;
static d_iov_t     const1_iov;
static d_iov_t     const2_iov;
static d_iov_t     const3_iov;

daos_filter_part_t dkey_ft;
daos_filter_part_t akey1_ft;
daos_filter_part_t akey2_ft;
daos_filter_part_t const0_ft;
daos_filter_part_t const1_ft;
daos_filter_part_t const2_ft;
daos_filter_part_t const3_ft;
daos_filter_part_t like_ft;
daos_filter_part_t ba_ft;
daos_filter_part_t eq_ft;
daos_filter_part_t gt_ft;
daos_filter_part_t and_ft;
daos_filter_part_t or_ft;

daos_filter_t      pipef;

/**
 *
 */
static void
build_pipeline_one(daos_pipeline_t *pipeline)
{
	daos_size_t bin_flen   = strlen(BINARY_F);
	daos_size_t dkey_flen  = strlen(DKEY_F);
	daos_size_t akey_flen  = strlen(AKEY_F);
	daos_size_t const_flen = strlen(CONST_F);
	daos_size_t int8_flen  = strlen(INT8_F);
	daos_size_t int4_flen  = strlen(INT4_F);
	daos_size_t like_flen  = strlen(LIKE_F);
	daos_size_t gt_flen    = strlen(GT_F);
	daos_size_t eq_flen    = strlen(EQ_F);
	daos_size_t ba_flen    = strlen(BA_F);
	daos_size_t and_flen   = strlen(AND_F);
	daos_size_t or_flen    = strlen(OR_F);
	daos_size_t cond_flen  = strlen(COND_F);
	daos_size_t name_len   = strlen(NAME_F);
	daos_size_t akey_len   = strlen(field);
	int         rc;

	/** build condition for dkey containing ".9" */

	d_iov_set(&dkey_ft.part_type, DKEY_F, dkey_flen);
	d_iov_set(&dkey_ft.data_type, BINARY_F, bin_flen);
	dkey_ft.data_len = FSIZE;

	d_iov_set(&const0_ft.part_type, CONST_F, const_flen);
	d_iov_set(&const0_ft.data_type, BINARY_F, bin_flen);
	const0_ft.num_constants = 1;
	const0_ft.constant      = &dkey_iov;
	d_iov_set(const0_ft.constant, NAME_F, name_len);

	d_iov_set(&like_ft.part_type, LIKE_F, like_flen);
	like_ft.num_operands = 2;

	/** build condition for uint32_t integer in array bytes 0-4 & S_IFMT == S_IFDIR */

	d_iov_set(&akey1_ft.part_type, AKEY_F, akey_flen);
	d_iov_set(&akey1_ft.data_type, INT4_F, int4_flen);
	d_iov_set(&akey1_ft.akey, field, akey_len);
	akey1_ft.data_len = 4;

	d_iov_set(&const1_ft.part_type, CONST_F, const_flen);
	d_iov_set(&const1_ft.data_type, INT4_F, int4_flen);
	const1_ft.num_constants = 1;
	const1_ft.constant      = &const1_iov;
	d_iov_set(const1_ft.constant, &constant1, sizeof(mode_t));

	d_iov_set(&const2_ft.part_type, CONST_F, const_flen);
	d_iov_set(&const2_ft.data_type, INT4_F, int4_flen);
	const2_ft.num_constants = 1;
	const2_ft.constant      = &const2_iov;
	d_iov_set(const2_ft.constant, &constant2, sizeof(mode_t));

	d_iov_set(&ba_ft.part_type, BA_F, ba_flen);
	ba_ft.num_operands = 2;

	d_iov_set(&eq_ft.part_type, EQ_F, eq_flen);
	eq_ft.num_operands = 2;

	/** build condition for ts integer in array bytes 12-20 > ts */

	d_iov_set(&akey2_ft.part_type, AKEY_F, akey_flen);
	d_iov_set(&akey2_ft.data_type, INT8_F, int8_flen);
	d_iov_set(&akey2_ft.akey, field, akey_len);
	akey2_ft.data_offset = 12;
	akey2_ft.data_len    = 8;

	d_iov_set(&const3_ft.part_type, CONST_F, const_flen);
	d_iov_set(&const3_ft.data_type, INT8_F, int8_flen);
	const3_ft.num_constants = 1;
	const3_ft.constant      = &const3_iov;
	d_iov_set(const3_ft.constant, &ts, sizeof(time_t));

	d_iov_set(&gt_ft.part_type, GT_F, gt_flen);
	gt_ft.num_operands = 2;

	/*
	 * build final condition where result should be the:
	 * bitwise array cond || (dkey condition && mtime array)
	 */

	d_iov_set(&and_ft.part_type, AND_F, and_flen);
	and_ft.num_operands = 2;

	d_iov_set(&or_ft.part_type, OR_F, or_flen);
	or_ft.num_operands = 2;

	daos_filter_init(&pipef);
	d_iov_set(&pipef.filter_type, COND_F, cond_flen);

	rc = daos_filter_add(&pipef, &or_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);

	rc = daos_filter_add(&pipef, &eq_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
	rc = daos_filter_add(&pipef, &ba_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
	rc = daos_filter_add(&pipef, &akey1_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
	rc = daos_filter_add(&pipef, &const1_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
	rc = daos_filter_add(&pipef, &const2_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);

	rc = daos_filter_add(&pipef, &and_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);

	rc = daos_filter_add(&pipef, &like_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
	rc = daos_filter_add(&pipef, &dkey_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
	rc = daos_filter_add(&pipef, &const0_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);

	rc = daos_filter_add(&pipef, &gt_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
	rc = daos_filter_add(&pipef, &akey2_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
	rc = daos_filter_add(&pipef, &const3_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);

	rc = daos_pipeline_add(pipeline, &pipef);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
}

static void
run_pipeline(daos_pipeline_t *pipeline)
{
	daos_iod_t            *iods;
	daos_anchor_t          anchor;
	uint32_t               nr_iods, nr_kds;
	daos_key_desc_t       *kds;
	d_sg_list_t            sgl_keys;
	d_iov_t               *iov_keys;
	char                  *buf_keys;
	d_sg_list_t            sgl_recs;
	d_iov_t               *iovs_recs;
	char                  *buf_recs;
	daos_recx_t            recxs[2];
	daos_pipeline_stats_t  stats = {0};
	uint32_t               i;
	int                    rc;

	/* record extensions for akey's array */
	recxs[0].rx_idx = 0;
	recxs[0].rx_nr  = sizeof(mode_t);
	recxs[1].rx_idx = 12;
	recxs[1].rx_nr  = sizeof(time_t);

	/* reading chunks of 16 keys (at most) at a time */
	nr_kds    = 16;
	nr_iods   = NR_IODS_PER_DKEY * nr_kds;

	/* to store retrieved dkeys */
	kds                   = malloc(sizeof(*kds) * nr_kds);
	iov_keys              = malloc(sizeof(*iov_keys));
	sgl_keys.sg_nr        = 1;
	sgl_keys.sg_nr_out    = 0;
	sgl_keys.sg_iovs      = iov_keys;
	buf_keys              = malloc(FSIZE * nr_kds);
	d_iov_set(iov_keys, buf_keys, FSIZE * nr_kds);

	/* to store retrieved data */
	iods                  = calloc(nr_iods, sizeof(*iods));
	iovs_recs             = malloc(sizeof(*iovs_recs) * nr_iods);
	sgl_recs.sg_nr        = nr_iods;
	sgl_recs.sg_nr_out    = 0;
	sgl_recs.sg_iovs      = iovs_recs;
	buf_recs              = malloc((sizeof(mode_t) + sizeof(time_t)) * nr_kds);

	for (i = 0; i < nr_iods; i++) {
		d_iov_set(&iovs_recs[i], &buf_recs[i * (sizeof(mode_t) + sizeof(time_t))],
			  sizeof(mode_t) + sizeof(time_t));

		iods[i].iod_nr    = 2;
		iods[i].iod_size  = 1; /* we interpret it as an array of bytes */
		iods[i].iod_recxs = recxs;
		iods[i].iod_type  = DAOS_IOD_ARRAY;
	}
	/** Only need to set once per akey */
	d_iov_set(&iods[0].iod_name, (char *)field, strlen(field));

	/** reset anchor */
	memset(&anchor, 0, sizeof(daos_anchor_t));

	/** calling pipeline run until EOF */
	while (!daos_anchor_is_eof(&anchor)) {
		nr_kds = 16; /** trying to read 16 in each iteration */
		nr_iods = NR_IODS_PER_DKEY * nr_kds;

		rc     = daos_pipeline_run(coh, oh, pipeline, DAOS_TX_NONE, 0, NULL,
					   NR_IODS_PER_DKEY, &nr_iods, iods, &anchor, &nr_kds, kds,
					   &sgl_keys, &sgl_recs, NULL, &stats, NULL);
		ASSERT(rc == 0, "Pipeline run failed with %d", rc);

		/** processing nr_kds records */
		size_t off     = 0;
		char *dkey_ptr = (char *)sgl_keys.sg_iovs->iov_buf;

		for (i = 0; i < nr_kds; i++) {
			char       *dkey     = &dkey_ptr[off];
			daos_size_t dkeylen  = kds[i].kd_key_len;

			off                 += dkeylen;
			printf("\t(dkey)=%.*s, len = %zu\t", (int)dkeylen, dkey, dkeylen);

			char  *ptr      = &buf_recs[i * (sizeof(mode_t) + sizeof(time_t))];
			mode_t cur_mode = *((mode_t *)ptr);

			if (S_ISDIR(cur_mode))
				printf("MODE type = S_IFDIR\n");
			else if (S_ISREG(cur_mode))
				printf("MODE type = S_IFREG\n");
			else
				ASSERT(0, "ERROR: invalid mode_t retrieved\n");
		}
	}
	printf("\tNumber of dkeys scanned: %zu\n\n", stats.nr_dkeys);
	ASSERT(stats.nr_dkeys == NUM_DKEYS, "Number of dkeys scanned != inserted number\n");

	free(iods);
	free(kds);
	free(iov_keys);
	free(iovs_recs);
	free(buf_recs);
	free(buf_keys);
}

int
main(int argc, char **argv)
{
	daos_obj_id_t   oid;
	int             rc;
	daos_pipeline_t pipeline1;

	if (argc != 3) {
		fprintf(stderr, "args: pool cont\n");
		exit(1);
	}

	/** Initialize the local DAOS stack */
	rc = daos_init();
	ASSERT(rc == 0, "daos_init failed with %d", rc);

	/** Call connect */
	rc = daos_pool_connect(argv[1], NULL, DAOS_PC_RW, &poh, NULL, NULL);
	ASSERT(rc == 0, "pool connect failed with %d", rc);

	rc = daos_cont_create_with_label(poh, argv[2], NULL, NULL, NULL);
	ASSERT(rc == 0, "container create failed with %d", rc);

	rc = daos_cont_open(poh, argv[2], DAOS_COO_RW, &coh, NULL, NULL);
	ASSERT(rc == 0, "container open failed with %d", rc);

	/** create/open object */
	oid.hi = 0;
	oid.lo = 4;
	daos_obj_generate_oid(coh, &oid, DAOS_OF_KV_FLAT, OC_SX, 0, 0);

	rc = daos_obj_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	ASSERT(rc == 0, "Obj open failed with %d", rc);

	/** Insert some example records */
	insert_example_records();

	/** init pipeline1 object */
	daos_pipeline_init(&pipeline1);
	build_pipeline_one(&pipeline1);
	rc = daos_pipeline_check(&pipeline1);
	ASSERT(rc == 0, "Pipeline check failed with %d", rc);

	run_pipeline(&pipeline1);

	/** TODO - should have API to free internally allocated filters. */
	free(pipeline1.filters);

	rc = daos_obj_close(oh, NULL);
	ASSERT(rc == 0, "Obj close failed with %d", rc);

	rc = daos_cont_close(coh, NULL);
	ASSERT(rc == 0, "cont close failed");

	rc = daos_cont_destroy(poh, argv[2], 1, NULL);
	ASSERT(rc == 0, "Container destroy failed with %d", rc);

	rc = daos_pool_disconnect(poh, NULL);
	ASSERT(rc == 0, "disconnect failed");

	rc = daos_fini();
	ASSERT(rc == 0, "daos_fini failed with %d", rc);

	return 0;
}
