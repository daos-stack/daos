/**
 * (C) Copyright 2021-2022 Intel Corporation.
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
#define NR_IODS        4
#define STRING_MAX_LEN 10

static char *fields[NR_IODS] = {"Owner", "Species", "Sex", "Age"};
int          nr_aggr;

void
insert_example_records(void)
{
	int         rc;
	d_iov_t     dkey;
	d_sg_list_t sgls[NR_IODS];
	d_iov_t     iovs[NR_IODS];
	daos_iod_t  iods[NR_IODS];
	uint32_t    i, j;

	char       *name[] = {"Slim",   "Buffy",   "Claws", "Whistler",
			      "Chirpy", "Browser", "Fang",  "Fluffy"};
	char    *owner[] = {"Benny", "Harold", "GWen", "Gwen", "Gwen", "Diane", "Benny", "Harold"};
	char    *species[] = {"snake", "dog", "cat", "bird", "bird", "dog", "dog", "cat"};
	char    *sex[]     = {"m", "f", "m", "m", "f", "m", "m", "f"};
	uint64_t age[]     = {1, 10, 4, 2, 3, 2, 7, 9};

	void    *data[NR_IODS];

	data[0] = (void *)owner;
	data[1] = (void *)species;
	data[2] = (void *)sex;
	data[3] = (void *)age;

	printf("records:\n");
	for (i = 0; i < 8; i++) { /** records */
		printf("\tname(dkey)=%s%*c", name[i], (int)(STRING_MAX_LEN - strlen(name[i])), ' ');
		/** set dkey for record */
		d_iov_set(&dkey, name[i], strlen(name[i]));

		for (j = 0; j < NR_IODS - 1; j++) { /** str fields */
			char **strdata = (char **)data[j];

			printf("%s(akey)=%s%*c", fields[j], strdata[i],
			       (int)(STRING_MAX_LEN - strlen(strdata[i])), ' ');
			/** akeys */
			sgls[j].sg_nr     = 1;
			sgls[j].sg_nr_out = 0;
			sgls[j].sg_iovs   = &iovs[j];
			d_iov_set(&iovs[j], strdata[i], strlen(strdata[i]) + 1);

			d_iov_set(&iods[j].iod_name, (void *)fields[j], strlen(fields[j]));
			iods[j].iod_nr    = 1;
			iods[j].iod_size  = strlen(strdata[i]) + 1;
			iods[j].iod_recxs = NULL;
			iods[j].iod_type  = DAOS_IOD_SINGLE;
		}
		uint64_t *intdata = (uint64_t *)data[NR_IODS - 1];

		printf("%s(akey)=%lu\n", fields[NR_IODS - 1], intdata[i]);
		/** akeys */
		sgls[NR_IODS - 1].sg_nr     = 1;
		sgls[NR_IODS - 1].sg_nr_out = 0;
		sgls[NR_IODS - 1].sg_iovs   = &iovs[NR_IODS - 1];
		d_iov_set(&iovs[NR_IODS - 1], &intdata[i], sizeof(uint64_t));

		d_iov_set(&iods[NR_IODS - 1].iod_name, (void *)fields[NR_IODS - 1],
			  strlen(fields[NR_IODS - 1]));
		iods[NR_IODS - 1].iod_nr    = 1;
		iods[NR_IODS - 1].iod_size  = sizeof(uint64_t);
		iods[NR_IODS - 1].iod_recxs = NULL;
		iods[NR_IODS - 1].iod_type  = DAOS_IOD_SINGLE;

		rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, NR_IODS, iods, sgls, NULL);
		ASSERT(rc == 0, "Obj update failed with %d", rc);
	}
	printf("\n");
}

/**
 * Build pipeline filtering by "Owner == Benny"
 */
void
build_pipeline_one(daos_pipeline_t *pipeline)
{
	daos_filter_part_t *akey_ft, *eqfunc_ft, *const_ft;
	char	       *akey_ftype, *const_ftype, *eqfunc_ftype;
	size_t              akey_ftype_s, const_ftype_s, eqfunc_ftype_s;
	char	       *str_type1, *str_type2;
	char	       *pipe_cond_type;
	size_t              str_type_s, pipe_cond_type_s;
	char	       *constant, *akey;
	size_t              akey_s;
	daos_filter_t      *comp_eq;
	int                 rc;

	/** mem allocation */
	str_type_s = strlen("DAOS_FILTER_TYPE_CSTRING");
	str_type1  = (char *)malloc(str_type_s + 1);
	str_type2  = (char *)malloc(str_type_s + 1);
	strcpy(str_type1, "DAOS_FILTER_TYPE_CSTRING");
	strcpy(str_type2, "DAOS_FILTER_TYPE_CSTRING");

	akey_ftype_s = strlen("DAOS_FILTER_AKEY");
	akey_ftype   = (char *)malloc(akey_ftype_s + 1);
	strcpy(akey_ftype, "DAOS_FILTER_AKEY");

	akey_s = strlen("Owner");
	akey   = (char *)malloc(akey_s + 1);
	strcpy(akey, "Owner");

	const_ftype_s = strlen("DAOS_FILTER_CONST");
	const_ftype   = (char *)malloc(const_ftype_s + 1);
	strcpy(const_ftype, "DAOS_FILTER_CONST");

	constant = (char *)malloc(strlen("Benny") + 1);
	strcpy(constant, "Benny");

	eqfunc_ftype_s = strlen("DAOS_FILTER_FUNC_EQ");
	eqfunc_ftype   = (char *)malloc(eqfunc_ftype_s + 1);
	strcpy(eqfunc_ftype, "DAOS_FILTER_FUNC_EQ");

	pipe_cond_type_s = strlen("DAOS_FILTER_CONDITION");
	pipe_cond_type   = (char *)malloc(pipe_cond_type_s + 1);
	strcpy(pipe_cond_type, "DAOS_FILTER_CONDITION");

	akey_ft            = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	const_ft           = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	eqfunc_ft          = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	comp_eq            = (daos_filter_t *)calloc(1, sizeof(daos_filter_t));

	const_ft->constant = (d_iov_t *)malloc(sizeof(d_iov_t));

	/** akey for filter */
	d_iov_set(&akey_ft->part_type, akey_ftype, akey_ftype_s);
	d_iov_set(&akey_ft->data_type, str_type1, str_type_s);
	d_iov_set(&akey_ft->akey, akey, akey_s);
	akey_ft->data_len = STRING_MAX_LEN;

	/** constant for filter */
	d_iov_set(&const_ft->part_type, const_ftype, const_ftype_s);
	d_iov_set(&const_ft->data_type, str_type2, str_type_s);
	const_ft->num_constants = 1;
	d_iov_set(const_ft->constant, constant, strlen(constant) + 1);

	/** function for filter */
	d_iov_set(&eqfunc_ft->part_type, eqfunc_ftype, eqfunc_ftype_s);
	eqfunc_ft->num_operands = 2;

	/**
	 * building a pipeline condition filter:
	 *    the order of operands is prefix:
	 *         "Owner == Benny"  ->  |(func=eq)|(akey=Owner)|(const=Benny)|
	 */
	daos_filter_init(comp_eq);
	d_iov_set(&comp_eq->filter_type, pipe_cond_type, pipe_cond_type_s);

	rc = daos_filter_add(comp_eq, eqfunc_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(comp_eq, akey_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(comp_eq, const_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);

	/** adding the filter to the pipeline. This pipeline has only one filter */
	rc = daos_pipeline_add(pipeline, comp_eq);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
}

/**
 * Build pipeline filtering by "Owner == Benny AND Species == dog"
 */
void
build_pipeline_two(daos_pipeline_t *pipeline)
{
	daos_filter_part_t *akey1_ft, *eqfunc1_ft, *const1_ft;
	daos_filter_part_t *akey2_ft, *eqfunc2_ft, *const2_ft;
	daos_filter_part_t *andfunc_ft;
	char	       *akey1_ftype, *akey2_ftype;
	char	       *const1_ftype, *const2_ftype;
	size_t              akey_ftype_s, const_ftype_s;
	char	       *eqfunc1_ftype, *eqfunc2_ftype, *andfunc_ftype;
	size_t              eqfunc_ftype_s, andfunc_ftype_s;
	char	       *str_type1, *str_type2, *str_type3, *str_type4;
	char	       *pipe_cond_type;
	size_t              str_type_s, pipe_cond_type_s;
	char	       *constant1, *akey1, *constant2, *akey2;
	size_t              akey1_s, akey2_s;
	daos_filter_t      *comp_and;
	int                 rc;

	/** mem allocation */
	akey_ftype_s = strlen("DAOS_FILTER_AKEY");
	akey1_ftype  = (char *)malloc(akey_ftype_s + 1);
	akey2_ftype  = (char *)malloc(akey_ftype_s + 1);
	strcpy(akey1_ftype, "DAOS_FILTER_AKEY");
	strcpy(akey2_ftype, "DAOS_FILTER_AKEY");

	akey1_s = strlen("Owner");
	akey2_s = strlen("Species");
	akey1   = (char *)malloc(akey1_s + 1);
	akey2   = (char *)malloc(akey2_s + 1);
	strcpy(akey1, "Owner");
	strcpy(akey2, "Species");

	str_type_s = strlen("DAOS_FILTER_TYPE_CSTRING");
	str_type1  = (char *)malloc(str_type_s + 1);
	str_type2  = (char *)malloc(str_type_s + 1);
	str_type3  = (char *)malloc(str_type_s + 1);
	str_type4  = (char *)malloc(str_type_s + 1);
	strcpy(str_type1, "DAOS_FILTER_TYPE_CSTRING");
	strcpy(str_type2, "DAOS_FILTER_TYPE_CSTRING");
	strcpy(str_type3, "DAOS_FILTER_TYPE_CSTRING");
	strcpy(str_type4, "DAOS_FILTER_TYPE_CSTRING");

	const_ftype_s = strlen("DAOS_FILTER_CONST");
	const1_ftype  = (char *)malloc(const_ftype_s + 1);
	const2_ftype  = (char *)malloc(const_ftype_s + 1);
	strcpy(const1_ftype, "DAOS_FILTER_CONST");
	strcpy(const2_ftype, "DAOS_FILTER_CONST");

	constant1 = (char *)malloc(strlen("Benny") + 1);
	constant2 = (char *)malloc(strlen("dog") + 1);
	strcpy(constant1, "Benny");
	strcpy(constant2, "dog");

	eqfunc_ftype_s = strlen("DAOS_FILTER_FUNC_EQ");
	eqfunc1_ftype  = (char *)malloc(eqfunc_ftype_s + 1);
	eqfunc2_ftype  = (char *)malloc(eqfunc_ftype_s + 1);
	strcpy(eqfunc1_ftype, "DAOS_FILTER_FUNC_EQ");
	strcpy(eqfunc2_ftype, "DAOS_FILTER_FUNC_EQ");

	andfunc_ftype_s = strlen("DAOS_FILTER_FUNC_AND");
	andfunc_ftype   = (char *)malloc(andfunc_ftype_s + 1);
	strcpy(andfunc_ftype, "DAOS_FILTER_FUNC_AND");

	pipe_cond_type_s = strlen("DAOS_FILTER_CONDITION");
	pipe_cond_type   = (char *)malloc(pipe_cond_type_s + 1);
	strcpy(pipe_cond_type, "DAOS_FILTER_CONDITION");

	/** akey1 for filter */
	akey1_ft = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&akey1_ft->part_type, akey1_ftype, akey_ftype_s);
	d_iov_set(&akey1_ft->data_type, str_type1, str_type_s);
	d_iov_set(&akey1_ft->akey, akey1, akey1_s);
	akey1_ft->data_len = STRING_MAX_LEN;

	/** akey2 for filter */
	akey2_ft           = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&akey2_ft->part_type, akey2_ftype, akey_ftype_s);
	d_iov_set(&akey2_ft->data_type, str_type2, str_type_s);
	d_iov_set(&akey2_ft->akey, akey2, akey2_s);
	akey2_ft->data_len = STRING_MAX_LEN;

	/** constant1 for filter */
	const1_ft          = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&const1_ft->part_type, const1_ftype, const_ftype_s);
	d_iov_set(&const1_ft->data_type, str_type3, str_type_s);
	const1_ft->num_constants = 1;
	const1_ft->constant      = (d_iov_t *)malloc(sizeof(d_iov_t));
	d_iov_set(const1_ft->constant, constant1, strlen(constant1) + 1);

	/** constant2 for filter */
	const2_ft = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&const2_ft->part_type, const2_ftype, const_ftype_s);
	d_iov_set(&const2_ft->data_type, str_type4, str_type_s);
	const2_ft->num_constants = 1;
	const2_ft->constant      = (d_iov_t *)malloc(sizeof(d_iov_t));
	d_iov_set(const2_ft->constant, constant2, strlen(constant2) + 1);

	/** function1 for filter (==) */
	eqfunc1_ft = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&eqfunc1_ft->part_type, eqfunc1_ftype, eqfunc_ftype_s);
	eqfunc1_ft->num_operands = 2;

	/** function2 for filter (==) */
	eqfunc2_ft               = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&eqfunc2_ft->part_type, eqfunc2_ftype, eqfunc_ftype_s);
	eqfunc2_ft->num_operands = 2;

	/** function3 for filter (and) */
	andfunc_ft               = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&andfunc_ft->part_type, andfunc_ftype, andfunc_ftype_s);
	andfunc_ft->num_operands = 2;

	/**
	 * building a pipeline condition filter:
	 *    the order of operands is prefix:
	 *         "Owner == Benny AND Species == dog"  ->
	 * |(func=and)|(func=eq)|(akey=Owner)|(const=Benny)|(func=eq)|(akey=Species)|(const=dog)|
	 */
	comp_and                 = (daos_filter_t *)calloc(1, sizeof(daos_filter_t));
	daos_filter_init(comp_and);
	d_iov_set(&comp_and->filter_type, pipe_cond_type, pipe_cond_type_s);

	rc = daos_filter_add(comp_and, andfunc_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(comp_and, eqfunc1_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(comp_and, akey1_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(comp_and, const1_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(comp_and, eqfunc2_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(comp_and, akey2_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(comp_and, const2_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);

	/** adding the filter to the pipeline. This pipeline has only one filter */
	rc = daos_pipeline_add(pipeline, comp_and);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
}

/**
 * Build pipeline filtering by "Owner == Benny", aggregate by "SUM(age)"
 */
void
build_pipeline_three(daos_pipeline_t *pipeline)
{
	daos_filter_part_t *akey1_ft, *eqfunc_ft, *const_ft;
	daos_filter_part_t *akey2_ft;
	daos_filter_part_t *sumfunc_ft;
	char	       *akey1_ftype, *akey2_ftype, *const_ftype;
	size_t              akey_ftype_s, const_ftype_s;
	char	       *eqfunc_ftype, *sumfunc_ftype;
	size_t              eqfunc_ftype_s, sumfunc_ftype_s;
	char	       *str_type1, *str_type2;
	char	       *uint_type1;
	size_t              str_type_s, uint_type_s;
	char	       *pipe_cond_type, *pipe_aggr_type;
	size_t              pipe_cond_type_s, pipe_aggr_type_s;
	char	       *constant, *akey1, *akey2;
	size_t              akey1_s, akey2_s;
	daos_filter_t      *comp_eq, *aggr_sum;
	int                 rc;

	/** mem allocation */
	akey_ftype_s = strlen("DAOS_FILTER_AKEY");
	akey1_ftype  = (char *)malloc(akey_ftype_s + 1);
	akey2_ftype  = (char *)malloc(akey_ftype_s + 1);
	strcpy(akey1_ftype, "DAOS_FILTER_AKEY");
	strcpy(akey2_ftype, "DAOS_FILTER_AKEY");

	akey1_s = strlen("Owner");
	akey2_s = strlen("Age");
	akey1   = (char *)malloc(akey1_s + 1);
	akey2   = (char *)malloc(akey2_s + 1);
	strcpy(akey1, "Owner");
	strcpy(akey2, "Age");

	str_type_s = strlen("DAOS_FILTER_TYPE_CSTRING");
	str_type1  = (char *)malloc(str_type_s + 1);
	str_type2  = (char *)malloc(str_type_s + 1);
	strcpy(str_type1, "DAOS_FILTER_TYPE_CSTRING");
	strcpy(str_type2, "DAOS_FILTER_TYPE_CSTRING");
	uint_type_s = strlen("DAOS_FILTER_TYPE_UINTEGER8");
	uint_type1  = (char *)malloc(uint_type_s + 1);
	strcpy(uint_type1, "DAOS_FILTER_TYPE_UINTEGER8");

	const_ftype_s = strlen("DAOS_FILTER_CONST");
	const_ftype   = (char *)malloc(const_ftype_s + 1);
	strcpy(const_ftype, "DAOS_FILTER_CONST");
	constant = (char *)malloc(strlen("Benny") + 1);
	strcpy(constant, "Benny");

	eqfunc_ftype_s = strlen("DAOS_FILTER_FUNC_EQ");
	eqfunc_ftype   = (char *)malloc(eqfunc_ftype_s + 1);
	strcpy(eqfunc_ftype, "DAOS_FILTER_FUNC_EQ");

	sumfunc_ftype_s = strlen("DAOS_FILTER_FUNC_SUM");
	sumfunc_ftype   = (char *)malloc(sumfunc_ftype_s + 1);
	strcpy(sumfunc_ftype, "DAOS_FILTER_FUNC_SUM");

	pipe_cond_type_s = strlen("DAOS_FILTER_CONDITION");
	pipe_cond_type   = (char *)malloc(pipe_cond_type_s + 1);
	strcpy(pipe_cond_type, "DAOS_FILTER_CONDITION");

	pipe_aggr_type_s = strlen("DAOS_FILTER_AGGREGATION");
	pipe_aggr_type   = (char *)malloc(pipe_aggr_type_s + 1);
	strcpy(pipe_aggr_type, "DAOS_FILTER_AGGREGATION");

	/** akey1 for filter */
	akey1_ft = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&akey1_ft->part_type, akey1_ftype, akey_ftype_s);
	d_iov_set(&akey1_ft->data_type, str_type1, str_type_s);
	d_iov_set(&akey1_ft->akey, akey1, akey1_s);
	akey1_ft->data_len = STRING_MAX_LEN;

	/** akey2 for filter */
	akey2_ft           = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&akey2_ft->part_type, akey2_ftype, akey_ftype_s);
	d_iov_set(&akey2_ft->data_type, uint_type1, uint_type_s);
	d_iov_set(&akey2_ft->akey, akey2, akey2_s);
	akey2_ft->data_len = sizeof(uint64_t);

	/** constant for filter */
	const_ft           = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&const_ft->part_type, const_ftype, const_ftype_s);
	d_iov_set(&const_ft->data_type, str_type2, str_type_s);
	const_ft->num_constants = 1;
	const_ft->constant      = (d_iov_t *)malloc(sizeof(d_iov_t));
	d_iov_set(const_ft->constant, constant, strlen(constant) + 1);

	/** function1 for filter (==) */
	eqfunc_ft = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&eqfunc_ft->part_type, eqfunc_ftype, eqfunc_ftype_s);
	eqfunc_ft->num_operands = 2;

	/** function2 for filter (SUM()) */
	sumfunc_ft              = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&sumfunc_ft->part_type, sumfunc_ftype, sumfunc_ftype_s);
	sumfunc_ft->num_operands = 1;

	/**
	 * building a pipeline with a condition filter and an aggregation filter:
	 *    the order of operands is prefix:
	 *         "Owner == Benny" -> |(func=eq) |(akey=Owner)|(const=Benny)|
	 *         SUM(age)         -> |(func=sum)|(akey=Age)|
	 */
	comp_eq                  = (daos_filter_t *)calloc(1, sizeof(daos_filter_t));
	daos_filter_init(comp_eq);
	aggr_sum = (daos_filter_t *)calloc(1, sizeof(daos_filter_t));
	daos_filter_init(aggr_sum);

	d_iov_set(&comp_eq->filter_type, pipe_cond_type, pipe_cond_type_s);
	d_iov_set(&aggr_sum->filter_type, pipe_aggr_type, pipe_aggr_type_s);

	rc = daos_filter_add(comp_eq, eqfunc_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(comp_eq, akey1_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(comp_eq, const_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);

	rc = daos_filter_add(aggr_sum, sumfunc_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(aggr_sum, akey2_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);

	/** adding the filters to the pipeline. This pipeline has two filters */
	rc = daos_pipeline_add(pipeline, comp_eq);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
	rc = daos_pipeline_add(pipeline, aggr_sum);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
}

/**
 * Build pipeline filtering by "Age & 1 > 0"
 */
void
build_pipeline_four(daos_pipeline_t *pipeline)
{
	daos_filter_part_t *akey_ft, *bitandfunc_ft, *const1_ft, *const2_ft;
	daos_filter_part_t *gtfunc_ft;
	char	       *akey_ftype, *const1_ftype, *const2_ftype;
	char	       *bitandfunc_ftype, *gtfunc_ftype;
	size_t              akey_ftype_s, const_ftype_s, bitandfunc_ftype_s;
	size_t              gtfunc_ftype_s;
	char	       *uint_type1, *uint_type2, *uint_type3;
	char	       *pipe_cond_type;
	size_t              uint_type_s, pipe_cond_type_s;
	char	       *akey;
	size_t              akey_s;
	uint64_t           *constant1, *constant2;
	daos_filter_t      *func_bitand;
	int                 rc;

	/** mem allocation */
	uint_type_s = strlen("DAOS_FILTER_TYPE_UINTEGER8");
	uint_type1  = (char *)malloc(uint_type_s + 1);
	uint_type2  = (char *)malloc(uint_type_s + 1);
	uint_type3  = (char *)malloc(uint_type_s + 1);
	strcpy(uint_type1, "DAOS_FILTER_TYPE_UINTEGER8");
	strcpy(uint_type2, "DAOS_FILTER_TYPE_UINTEGER8");
	strcpy(uint_type3, "DAOS_FILTER_TYPE_UINTEGER8");

	akey_ftype_s = strlen("DAOS_FILTER_AKEY");
	akey_ftype   = (char *)malloc(akey_ftype_s + 1);
	strcpy(akey_ftype, "DAOS_FILTER_AKEY");

	akey_s = strlen("Age");
	akey   = (char *)malloc(akey_s + 1);
	strcpy(akey, "Age");

	const_ftype_s = strlen("DAOS_FILTER_CONST");
	const1_ftype  = (char *)malloc(const_ftype_s + 1);
	const2_ftype  = (char *)malloc(const_ftype_s + 1);
	strcpy(const1_ftype, "DAOS_FILTER_CONST");
	strcpy(const2_ftype, "DAOS_FILTER_CONST");

	constant1          = (uint64_t *)malloc(sizeof(uint64_t));
	constant2          = (uint64_t *)malloc(sizeof(uint64_t));
	*constant1         = 1;
	*constant2         = 0;

	bitandfunc_ftype_s = strlen("DAOS_FILTER_FUNC_BITAND");
	bitandfunc_ftype   = (char *)malloc(bitandfunc_ftype_s + 1);
	strcpy(bitandfunc_ftype, "DAOS_FILTER_FUNC_BITAND");

	gtfunc_ftype_s = strlen("DAOS_FILTER_FUNC_GT");
	gtfunc_ftype   = (char *)malloc(gtfunc_ftype_s + 1);
	strcpy(gtfunc_ftype, "DAOS_FILTER_FUNC_GT");

	pipe_cond_type_s = strlen("DAOS_FILTER_CONDITION");
	pipe_cond_type   = (char *)malloc(pipe_cond_type_s + 1);
	strcpy(pipe_cond_type, "DAOS_FILTER_CONDITION");

	/** akey for filter */
	akey_ft = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&akey_ft->part_type, akey_ftype, akey_ftype_s);
	d_iov_set(&akey_ft->data_type, uint_type1, uint_type_s);
	d_iov_set(&akey_ft->akey, akey, akey_s);
	akey_ft->data_len = sizeof(uint64_t);

	/** constant1 for filter */
	const1_ft         = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&const1_ft->part_type, const1_ftype, const_ftype_s);
	d_iov_set(&const1_ft->data_type, uint_type2, uint_type_s);
	const1_ft->num_constants = 1;
	const1_ft->constant      = (d_iov_t *)malloc(sizeof(d_iov_t));
	d_iov_set(const1_ft->constant, (void *)constant1, sizeof(uint64_t));

	/** constant2 for filter */
	const2_ft = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&const2_ft->part_type, const2_ftype, const_ftype_s);
	d_iov_set(&const2_ft->data_type, uint_type3, uint_type_s);
	const2_ft->num_constants = 1;
	const2_ft->constant      = (d_iov_t *)malloc(sizeof(d_iov_t));
	d_iov_set(const2_ft->constant, (void *)constant2, sizeof(uint64_t));

	/** bitand function for filter */
	bitandfunc_ft = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&bitandfunc_ft->part_type, bitandfunc_ftype, bitandfunc_ftype_s);
	bitandfunc_ft->num_operands = 2;

	/** greater than function for filter */
	gtfunc_ft                   = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&gtfunc_ft->part_type, gtfunc_ftype, gtfunc_ftype_s);
	gtfunc_ft->num_operands = 2;

	/**
	 * building a pipeline condition filter:
	 *    the order of operands is prefix:
	 *     "Age & 1 > 0" -> |(func=gt)|(func=bitand)|(akey=Age)|(const=1)|(const=0)|
	 */
	func_bitand             = (daos_filter_t *)calloc(1, sizeof(daos_filter_t));
	daos_filter_init(func_bitand);
	d_iov_set(&func_bitand->filter_type, pipe_cond_type, pipe_cond_type_s);

	rc = daos_filter_add(func_bitand, gtfunc_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(func_bitand, bitandfunc_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(func_bitand, akey_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(func_bitand, const1_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);
	rc = daos_filter_add(func_bitand, const2_ft);
	ASSERT(rc == 0, "Filter add failed with %d", rc);

	/** adding the filter to the pipeline. This pipeline has only one filter  */
	rc = daos_pipeline_add(pipeline, func_bitand);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
}

void
run_pipeline(daos_pipeline_t *pipeline)
{
	daos_iod_t             iods[NR_IODS];
	daos_anchor_t          anchor;
	uint32_t               nr_iods;
	uint32_t               nr_kds;
	daos_key_desc_t       *kds;
	d_sg_list_t            sgl_keys;
	d_iov_t               *iovs_keys;
	char                  *buf_keys;
	d_sg_list_t            sgl_recx;
	d_sg_list_t            sgl_aggr;
	d_iov_t               *iovs_recx;
	d_iov_t               *iovs_aggr;
	char                  *buf_recx;
	char                  *buf_aggr;
	daos_pipeline_stats_t  stats = {0};
	uint32_t               i, j, l;
	int                    rc;

	/** iods: information about what akeys to retrieve */
	for (i = 0; i < NR_IODS; i++) {
		iods[i].iod_nr    = 1;
		iods[i].iod_size  = STRING_MAX_LEN;
		iods[i].iod_recxs = NULL;
		iods[i].iod_type  = DAOS_IOD_SINGLE;
		d_iov_set(&iods[i].iod_name, (void *)fields[i], strlen(fields[i]));
	}

	/**
	 * reading in chunks of 64 keys (at most) at a time
	 */
	nr_kds    = 64;
	nr_iods   = NR_IODS;

	kds       = malloc(sizeof(daos_key_desc_t) * nr_kds);

	/** sgl_keys: to store the retrieved dkeys */
	sgl_keys.sg_nr     = nr_kds;
	sgl_keys.sg_nr_out = 0;
	iovs_keys          = malloc(sizeof(d_iov_t) * nr_kds);
	sgl_keys.sg_iovs   = iovs_keys;
	buf_keys           = malloc(nr_kds * STRING_MAX_LEN);
	for (i = 0; i < nr_kds; i++) {
		d_iov_set(&iovs_keys[i], &buf_keys[i * STRING_MAX_LEN], STRING_MAX_LEN);
	}

	/** sgl_recx: to store the retrieved data for the akeys of each dkey */
	sgl_recx.sg_nr     = nr_kds * nr_iods;
	sgl_recx.sg_nr_out = 0;
	iovs_recx          = malloc(sizeof(d_iov_t) * nr_kds * nr_iods);
	sgl_recx.sg_iovs   = iovs_recx;
	buf_recx           = malloc(nr_kds * nr_iods * STRING_MAX_LEN);
	for (i = 0; i < nr_kds * nr_iods; i++) {
		d_iov_set(&iovs_recx[i], &buf_recx[i * STRING_MAX_LEN], STRING_MAX_LEN);
	}
	/** sgl_aggr: for aggregation of data */
	sgl_aggr.sg_nr     = nr_aggr;
	sgl_aggr.sg_nr_out = 0;
	iovs_aggr          = malloc(sizeof(d_iov_t) * nr_aggr);
	sgl_aggr.sg_iovs   = iovs_aggr;
	buf_aggr           = malloc(sizeof(double) * nr_aggr);
	for (i = 0; i < nr_aggr; i++) {
		d_iov_set(&iovs_aggr[i], (void *)&buf_aggr[i], sizeof(double));
	}

	/** reset anchor */
	memset(&anchor, 0, sizeof(daos_anchor_t));

	/** reading 64 records at a time */
	while (!daos_anchor_is_eof(&anchor)) {
		nr_kds = 64; /** trying to read 64 at a time */

		rc = daos_pipeline_run(coh, oh, pipeline, DAOS_TX_NONE, 0, NULL, &nr_iods, iods,
				       &anchor, &nr_kds, kds, &sgl_keys, &sgl_recx, &sgl_aggr,
				       &stats, NULL);

		ASSERT(rc == 0, "Pipeline run failed with %d", rc);
		/** process nr_kds fetched records */
		for (i = 0; i < nr_kds; i++) {
			char  *dkey   = (char *)sgl_keys.sg_iovs[i].iov_buf;
			size_t dkey_s = sgl_keys.sg_iovs[i].iov_len;

			printf("\tname(dkey)=%.*s%*c", (int)dkey_s, dkey,
			       (int)(STRING_MAX_LEN - dkey_s), ' ');
			for (j = 0; j < nr_iods - 1; j++) {
				char  *akey;
				size_t akey_s;

				l      = i * nr_iods + j;
				akey   = (char *)sgl_recx.sg_iovs[j].iov_buf;
				akey_s = sgl_recx.sg_iovs[j].iov_len;
				printf("%.*s(akey)=%.*s%*c", (int)iods[j].iod_name.iov_len,
				       (char *)iods[j].iod_name.iov_buf, (int)akey_s, akey,
				       (int)(STRING_MAX_LEN - akey_s), ' ');
			}
			uint64_t *akey;

			l    = i * nr_iods + (nr_iods - 1);
			akey = (uint64_t *)sgl_recx.sg_iovs[l].iov_buf;
			printf("%.*s(akey)=%lu\n", (int)iods[nr_iods - 1].iod_name.iov_len,
			       (char *)iods[nr_iods - 1].iod_name.iov_buf, *akey);
		}
	}
	printf("\t(scanned %lu dkeys)\n", stats.nr_dkeys);
	for (i = 0; i < nr_aggr; i++) {
		double *res = (double *)sgl_aggr.sg_iovs[i].iov_buf;

		printf("  ---agg result[%u]=%f---\n", i, *res);
	}
	printf("\n");

	free(kds);
	free(iovs_keys);
	free(buf_keys);
	free(iovs_recx);
	free(buf_recx);
	free(iovs_aggr);
	free(buf_aggr);
}

int
main(int argc, char **argv)
{
	daos_obj_id_t   oid;
	int             rc;
	daos_pipeline_t pipeline1, pipeline2, pipeline3, pipeline4;

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

	rc = daos_cont_create_with_label(poh, "simple_pipeline_cont", NULL, NULL, NULL);
	ASSERT(rc == 0, "container create failed with %d", rc);

	rc = daos_cont_open(poh, "simple_pipeline_cont", DAOS_COO_RW, &coh, NULL, NULL);
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
	/** FILTER "Owner == Benny" */
	build_pipeline_one(&pipeline1);
	/** checking that the pipe is well constructed */
	rc = daos_pipeline_check(&pipeline1);
	ASSERT(rc == 0, "Pipeline check failed with %d", rc);
	printf("filtering by (Owner=Benny):\n");
	/** Running pipeline */
	nr_aggr = 0;
	run_pipeline(&pipeline1);

	/** init pipeline2 object */
	daos_pipeline_init(&pipeline2);
	/** FILTER "Owner == Benny AND Species == dog" */
	build_pipeline_two(&pipeline2);
	/** checking that the pipe is well constructed */
	rc = daos_pipeline_check(&pipeline2);
	ASSERT(rc == 0, "Pipeline check failed with %d", rc);
	printf("filtering by (Owner=Benny AND Species=dog):\n");
	/** Running pipeline */
	run_pipeline(&pipeline2);

	/** init pipeline3 object */
	daos_pipeline_init(&pipeline3);
	/** FILTER "Owner == Benny", AGGREGATE "SUM(age)" */
	build_pipeline_three(&pipeline3);
	/** checking that the pipe is well constructed */
	rc = daos_pipeline_check(&pipeline3);
	ASSERT(rc == 0, "Pipeline check failed with %d", rc);
	printf("filtering by (Owner=Benny), aggregating by SUM(age):\n");
	/** Running pipeline */
	nr_aggr = 1;
	run_pipeline(&pipeline3);
	nr_aggr = 0;

	/** init pipeline4 object */
	daos_pipeline_init(&pipeline4);
	/** FILTER "Age & 1" */
	build_pipeline_four(&pipeline4);
	/** checking that the pipe is well constructed */
	rc = daos_pipeline_check(&pipeline4);
	ASSERT(rc == 0, "Pipeline check failed with %d", rc);
	printf("filtering by (Age & 1) > 0:\n");
	/** Running pipeline */
	run_pipeline(&pipeline4);

	/** Freeing used memory */
	free_pipeline(&pipeline1);
	free_pipeline(&pipeline2);
	free_pipeline(&pipeline3);
	free_pipeline(&pipeline4);

	/** destroying container */
	rc = daos_cont_destroy(poh, "simple_pipeline_cont", 1, NULL);
	ASSERT(rc == 0, "Container destroy failed with %d", rc);

	return 0;
}
