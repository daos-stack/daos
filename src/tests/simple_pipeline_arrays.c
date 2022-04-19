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
#include <daos.h>
#include "pipeline_common.h"

/** daos info */
static daos_handle_t poh; /** pool */
static daos_handle_t coh; /** container */
static daos_handle_t oh;  /** object */

/** DB info */
#define NR_RECXS         4
#define NR_IODS_PER_DKEY 1

static char field[] = "Array";
int         nr_aggr;

void
insert_example_records(void)
{
	int         rc;
	d_iov_t     dkey;
	d_sg_list_t sgl;
	d_iov_t     iov;
	char        record_data[18];
	daos_iod_t  iod;
	void       *data[NR_RECXS];
	daos_recx_t recxs[NR_RECXS];
	uint32_t    i, j;

	uint64_t    ID[]             = {1, 2, 3, 4, 5, 6, 7, 8};
	uint64_t    DATA_RX_IDX_0[]  = {10, 20, 11, 21, 50, 51, 52, 3};
	uint32_t    DATA_RX_IDX_10[] = {100, 200, 110, 210, 500, 510, 520, 30};
	uint32_t    DATA_RX_IDX_14[] = {10, 9, 8, 7, 6, 5, 4, 3};
	uint16_t    DATA_RX_IDX_31[] = {2, 4, 6, 8, 16, 32, 64, 128};

	data[0]         = (void *)DATA_RX_IDX_0;
	data[1]         = (void *)DATA_RX_IDX_10;
	data[2]         = (void *)DATA_RX_IDX_14;
	data[3]         = (void *)DATA_RX_IDX_31;

	/**
	 * record size will be 1 byte so we can store objects of different
	 * size in the array
	 */
	recxs[0].rx_idx = 0;
	recxs[0].rx_nr  = 8; /** 8 bytes long unsigned integer */
	recxs[1].rx_idx = 10;
	recxs[1].rx_nr  = 4; /** 4 bytes long unsigned integer */
	recxs[2].rx_idx = 14;
	recxs[2].rx_nr  = 4; /** 4 bytes long unsigned integer */
	recxs[3].rx_idx = 31;
	recxs[3].rx_nr  = 2; /** 2 bytes long unsigned integer */

	printf("records:\n");
	for (i = 0; i < 8; i++) {
		size_t offset;

		printf("\tid(dkey)=%lu\t", ID[i]);
		/** set dkey for record */
		d_iov_set(&dkey, &ID[i], 8);

		/** set akey */
		offset = 0;
		printf("%s(akey) -->> ", field);
		for (j = 0; j < NR_RECXS; j++) {
			char *data_j = (char *)data[j];

			printf("rx[%lu:%lu]=", recxs[j].rx_idx, recxs[j].rx_nr);
			if (recxs[j].rx_nr == 8) {
				printf("%lu\t", ((uint64_t *)data_j)[i]);
				memcpy(&record_data[offset], &data_j[i * 8], 8);
				offset += 8;
			} else if (recxs[j].rx_nr == 4) {
				printf("%u\t", ((uint32_t *)data_j)[i]);
				memcpy(&record_data[offset], &data_j[i * 4], 4);
				offset += 4;
			} else {
				printf("%hu\t", ((uint16_t *)data_j)[i]);
				memcpy(&record_data[offset], &data_j[i * 2], 2);
				offset += 2;
			}
		}
		printf("\n");

		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs   = &iov;
		d_iov_set(&iov, record_data, 18);

		d_iov_set(&iod.iod_name, (void *)field, strlen(field));
		iod.iod_nr    = NR_RECXS;
		iod.iod_size  = 1;
		iod.iod_recxs = recxs;
		iod.iod_type  = DAOS_IOD_ARRAY;

		rc            = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
		ASSERT(rc == 0, "Obj update failed with %d", rc);
	}
	printf("\n");
}

/**
 * Build pipeline filtering by "Array[0:8] < 50"
 */
void
build_pipeline_one(daos_pipeline_t *pipeline)
{
	char	       *int_type1, *int_type2, *akey_ftype;
	size_t              int_type_s, akey_ftype_s;
	char	       *const_ftype, *akey, *ltfunc_ftype;
	size_t              const_ftype_s, akey_s, ltfunc_ftype_s;
	char	       *pipe_cond_type;
	size_t              pipe_cond_type_s;
	uint64_t           *constant;
	daos_filter_part_t *akey_ft, *ltfunc_ft, *const_ft;
	daos_filter_t      *comp_lt;
	int                 rc;

	/** mem allocation */
	int_type_s = strlen("DAOS_FILTER_TYPE_UINTEGER8");
	int_type1  = (char *)malloc(int_type_s + 1);
	int_type2  = (char *)malloc(int_type_s + 1);
	strcpy(int_type1, "DAOS_FILTER_TYPE_UINTEGER8");
	strcpy(int_type2, "DAOS_FILTER_TYPE_UINTEGER8");

	akey_ftype_s = strlen("DAOS_FILTER_AKEY");
	akey_ftype   = (char *)malloc(akey_ftype_s + 1);
	strcpy(akey_ftype, "DAOS_FILTER_AKEY");

	akey_s = strlen("Array");
	akey   = (char *)malloc(akey_s + 1);
	strcpy(akey, "Array");

	const_ftype_s = strlen("DAOS_FILTER_CONST");
	const_ftype   = (char *)malloc(const_ftype_s + 1);
	strcpy(const_ftype, "DAOS_FILTER_CONST");

	constant       = (uint64_t *)malloc(sizeof(uint64_t));
	*constant      = 50;

	ltfunc_ftype_s = strlen("DAOS_FILTER_FUNC_LT");
	ltfunc_ftype   = (char *)malloc(ltfunc_ftype_s + 1);
	strcpy(ltfunc_ftype, "DAOS_FILTER_FUNC_LT");

	pipe_cond_type_s = strlen("DAOS_FILTER_CONDITION");
	pipe_cond_type   = (char *)malloc(pipe_cond_type_s + 1);
	strcpy(pipe_cond_type, "DAOS_FILTER_CONDITION");

	akey_ft            = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	const_ft           = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	ltfunc_ft          = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	comp_lt            = (daos_filter_t *)calloc(1, sizeof(daos_filter_t));

	const_ft->constant = (d_iov_t *)malloc(sizeof(d_iov_t));

	/** akey for filter */
	d_iov_set(&akey_ft->part_type, akey_ftype, akey_ftype_s);
	d_iov_set(&akey_ft->data_type, int_type1, int_type_s);
	d_iov_set(&akey_ft->akey, akey, akey_s);
	akey_ft->data_len = 8;

	/** constant for filter */
	d_iov_set(&const_ft->part_type, const_ftype, const_ftype_s);
	d_iov_set(&const_ft->data_type, int_type2, int_type_s);
	const_ft->num_constants = 1;
	d_iov_set(const_ft->constant, (void *)constant, 8);

	/** function for filter */
	d_iov_set(&ltfunc_ft->part_type, ltfunc_ftype, ltfunc_ftype_s);
	ltfunc_ft->num_operands = 2;

	/** building a pipeline condition filter:
	 *    the order of operands is prefix:
	 *         "Array[0:8] < 50" -> |(func=lt)|(akey=Array)|(const=50)|
	 */
	daos_filter_init(comp_lt);
	d_iov_set(&comp_lt->filter_type, pipe_cond_type, pipe_cond_type_s);

	rc = daos_filter_add(comp_lt, ltfunc_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(comp_lt, akey_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(comp_lt, const_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);

	/** adding the filter to the pipeline. This pipeline has only one filter */
	rc = daos_pipeline_add(pipeline, comp_lt);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
}

/**
 * Build pipeline filtering by "Array[10:4] > 500"
 */
void
build_pipeline_two(daos_pipeline_t *pipeline)
{
	char	       *int_type1, *int_type2, *akey_ftype;
	size_t              int_type_s, akey_ftype_s;
	char	       *const_ftype, *akey, *gtfunc_ftype;
	size_t              const_ftype_s, akey_s, gtfunc_ftype_s;
	char	       *pipe_cond_type;
	size_t              pipe_cond_type_s;
	uint32_t           *constant;
	daos_filter_part_t *akey_ft, *gtfunc_ft, *const_ft;
	daos_filter_t      *comp_gt;
	int                 rc;

	/** mem allocation */
	int_type_s = strlen("DAOS_FILTER_TYPE_UINTEGER4");
	int_type1  = (char *)malloc(int_type_s + 1);
	int_type2  = (char *)malloc(int_type_s + 1);
	strcpy(int_type1, "DAOS_FILTER_TYPE_UINTEGER4");
	strcpy(int_type2, "DAOS_FILTER_TYPE_UINTEGER4");

	akey_ftype_s = strlen("DAOS_FILTER_AKEY");
	akey_ftype   = (char *)malloc(akey_ftype_s + 1);
	strcpy(akey_ftype, "DAOS_FILTER_AKEY");

	akey_s = strlen("Array");
	akey   = (char *)malloc(akey_s + 1);
	strcpy(akey, "Array");

	const_ftype_s = strlen("DAOS_FILTER_CONST");
	const_ftype   = (char *)malloc(const_ftype_s + 1);
	strcpy(const_ftype, "DAOS_FILTER_CONST");

	constant       = (uint32_t *)malloc(sizeof(uint32_t));
	*constant      = 500;

	gtfunc_ftype_s = strlen("DAOS_FILTER_FUNC_GT");
	gtfunc_ftype   = (char *)malloc(gtfunc_ftype_s + 1);
	strcpy(gtfunc_ftype, "DAOS_FILTER_FUNC_GT");

	pipe_cond_type_s = strlen("DAOS_FILTER_CONDITION");
	pipe_cond_type   = (char *)malloc(pipe_cond_type_s + 1);
	strcpy(pipe_cond_type, "DAOS_FILTER_CONDITION");

	akey_ft            = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	const_ft           = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	gtfunc_ft          = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	comp_gt            = (daos_filter_t *)calloc(1, sizeof(daos_filter_t));

	const_ft->constant = (d_iov_t *)malloc(sizeof(d_iov_t));

	/** akey for filter */
	d_iov_set(&akey_ft->part_type, akey_ftype, akey_ftype_s);
	d_iov_set(&akey_ft->data_type, int_type1, int_type_s);
	d_iov_set(&akey_ft->akey, akey, akey_s);
	akey_ft->data_offset = 10;
	akey_ft->data_len    = 4;

	/** constant for filter */
	d_iov_set(&const_ft->part_type, const_ftype, const_ftype_s);
	d_iov_set(&const_ft->data_type, int_type2, int_type_s);
	const_ft->num_constants = 1;
	d_iov_set(const_ft->constant, (void *)constant, 4);

	/** function for filter */
	d_iov_set(&gtfunc_ft->part_type, gtfunc_ftype, gtfunc_ftype_s);
	gtfunc_ft->num_operands = 2;

	/** building a pipeline condition filter:
	 *    the order of operands is prefix:
	 *         "Array[10:4] > 500" -> |(func=gt)|(akey=Array)|(const=500)|
	 */
	daos_filter_init(comp_gt);
	d_iov_set(&comp_gt->filter_type, pipe_cond_type, pipe_cond_type_s);

	rc = daos_filter_add(comp_gt, gtfunc_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(comp_gt, akey_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(comp_gt, const_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);

	/** adding the filter to the pipeline. This pipeline has only one filter */
	rc = daos_pipeline_add(pipeline, comp_gt);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
}

void
run_pipeline(daos_pipeline_t *pipeline)
{
	daos_iod_t             *iods;
	daos_anchor_t          anchor;
	uint32_t               nr_iods, nr_kds;
	daos_key_desc_t       *kds;
	d_sg_list_t            sgl_keys;
	d_iov_t               *iovs_keys;
	char                  *buf_keys;
	d_sg_list_t            sgl_recs;
	d_iov_t               *iovs_recs;
	char                  *buf_recs;
	daos_recx_t            recxs[NR_RECXS];
	daos_pipeline_stats_t  stats = {0};
	uint32_t               i, j;
	int                    rc;

	/* record extensions for akey's array */
	recxs[0].rx_idx = 0;
	recxs[0].rx_nr  = 8;
	recxs[1].rx_idx = 10;
	recxs[1].rx_nr  = 4;
	recxs[2].rx_idx = 14;
	recxs[2].rx_nr  = 4;
	recxs[3].rx_idx = 31;
	recxs[3].rx_nr  = 2;

	/* reading chunks of 64 keys (at most) at a time */
	nr_kds          = 64;
	nr_iods         = NR_IODS_PER_DKEY * nr_kds;

	/* to store retrieved dkeys */
	kds                   = malloc(sizeof(*kds) * nr_kds);
	iovs_keys             = malloc(sizeof(*iovs_keys) * nr_kds);
	sgl_keys.sg_nr        = nr_kds;
	sgl_keys.sg_nr_out    = 0;
	sgl_keys.sg_iovs      = iovs_keys;
	buf_keys              = malloc(nr_kds * 8);
	/* to store retrieved data */
	iods                  = calloc(nr_iods, sizeof(*iods));
	iovs_recs             = malloc(sizeof(*iovs_recs) * nr_iods);
	sgl_recs.sg_nr        = nr_iods;
	sgl_recs.sg_nr_out    = 0;
	sgl_recs.sg_iovs      = iovs_recs;
	buf_recs              = malloc(18 * nr_iods);

	for (i = 0; i < nr_kds; i++) {
		d_iov_set(&iovs_keys[i], &buf_keys[i * 8], 8);
	}
	for (i = 0; i < nr_iods; i++) {
		d_iov_set(&iovs_recs[i], &buf_recs[i * 18], 18);

		/**
		 * iods:
		 *  -- 0 <= i < NR_IODS_PER_DKEY: for akey's metadata
		 *  -- 0 <= i < nr_iods         : output information about akeys retrieved
		 */
		iods[i].iod_nr    = NR_RECXS;
		iods[i].iod_size  = 1; /* we interpret it as an array of bytes */
		iods[i].iod_recxs = recxs;
		iods[i].iod_type  = DAOS_IOD_ARRAY;
		if (i < NR_IODS_PER_DKEY)
			d_iov_set(&iods[i].iod_name, (char *)field, strlen(field));
	}

	/** reset anchor */
	memset(&anchor, 0, sizeof(daos_anchor_t));

	/** until anchor is EOF we call pipeline run */
	while (!daos_anchor_is_eof(&anchor)) {
		/** restorin value for in/out parametes */
		nr_kds  = 64; /** trying to read 64 in each iteration */
		nr_iods = NR_IODS_PER_DKEY * nr_kds;

		/** pipeline run */
		rc     = daos_pipeline_run(coh, oh, pipeline, DAOS_TX_NONE, 0, NULL,
					   NR_IODS_PER_DKEY, &nr_iods, iods, &anchor, &nr_kds, kds,
					   &sgl_keys, &sgl_recs, NULL, &stats, NULL);
		ASSERT(rc == 0, "Pipeline run failed with %d", rc);

		/** processing nr_kds records */
		for (i = 0; i < nr_kds; i++) {
			uint64_t dkey = *((uint64_t *)sgl_keys.sg_iovs[i].iov_buf);
			char    *akey_data;
			size_t   offset;

			printf("\tid(dkey)=%lu\t", dkey);
			printf("%s(akey) -->> ", field);

			offset    = 0;
			akey_data = (char *)sgl_recs.sg_iovs[i].iov_buf;

			for (j = 0; j < NR_RECXS; j++) {
				char *data_j = &akey_data[offset];

				printf("rx[%lu:%lu]=", recxs[j].rx_idx, recxs[j].rx_nr);
				if (recxs[j].rx_nr == 8) {
					printf("%lu\t", *((uint64_t *)data_j));
					offset += 8;
				} else if (recxs[j].rx_nr == 4) {
					printf("%u\t", *((uint32_t *)data_j));
					offset += 4;
				} else {
					printf("%hu\t", *((uint16_t *)data_j));
					offset += 2;
				}
			}
			printf("\n");
		}
	}
	printf("\t(scanned %lu dkeys)\n\n", stats.nr_dkeys);

	free(iods);
	free(kds);
	free(iovs_keys);
	free(buf_keys);
	free(iovs_recs);
	free(buf_recs);
}

int
main(int argc, char **argv)
{
	daos_obj_id_t   oid;
	int             rc;
	daos_pipeline_t pipeline1, pipeline2;

	if (argc != 2) {
		fprintf(stderr, "args: pool_uuid/pool_label\n");
		exit(1);
	}

	/** Initialize the local DAOS stack */
	rc = daos_init();
	ASSERT(rc == 0, "daos_init failed with %d", rc);

	/** Call connect */
	rc = daos_pool_connect(argv[1], NULL, DAOS_PC_RW, &poh, NULL, NULL);
	ASSERT(rc == 0, "pool connect failed with %d", rc);

	rc = daos_cont_create_with_label(poh, "simple_pipeline_cont1", NULL, NULL, NULL);
	ASSERT(rc == 0, "container create failed with %d", rc);

	rc = daos_cont_open(poh, "simple_pipeline_cont1", DAOS_COO_RW, &coh, NULL, NULL);
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
	/** FILTER "Array[0:8] < 50" */
	build_pipeline_one(&pipeline1);
	/** checking that the pipe is well constructed */
	rc = daos_pipeline_check(&pipeline1);
	ASSERT(rc == 0, "Pipeline check failed with %d", rc);
	printf("filtering by (Array[0:8] < 50):\n");
	/** Running pipeline */
	run_pipeline(&pipeline1);

	/** init pipeline2 object */
	daos_pipeline_init(&pipeline2);
	/** FILTER "Array[10:4] > 500" */
	build_pipeline_two(&pipeline2);
	/** checking that the pipe is well constructed */
	rc = daos_pipeline_check(&pipeline2);
	ASSERT(rc == 0, "Pipeline check failed with %d", rc);
	printf("filtering by (Array[10:4] > 500):\n");
	/** Running pipeline */
	run_pipeline(&pipeline2);

	/** Freeing used memory */
	free_pipeline(&pipeline1);
	free_pipeline(&pipeline2);

	/** destroying container */
	rc = daos_cont_destroy(poh, "simple_pipeline_cont1", 1, NULL);
	ASSERT(rc == 0, "Container destroy failed with %d", rc);

	return 0;
}
