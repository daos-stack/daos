/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * src/tests/suite/daos_pipeline.c
 */
#include <daos.h>
#if BUILD_PIPELINE
#include <daos_pipeline.h>
#endif
#include "daos_test.h"

#if BUILD_PIPELINE
#define NUM_AKEYS 4
#define VALUE_MAX_SIZE 10

daos_pipeline_t      pipe0;
daos_filter_t        comp_eq;
daos_filter_t        aggr_sum;
daos_filter_part_t   eqfunc_fp;
daos_filter_part_t   akey_fp;
daos_filter_part_t   const_fp;
daos_filter_part_t   andfunc_fp;
d_iov_t              const_iov0;
char                 const0[]          = "Benny";
char                 const1[]          = "Benny";
char                 const3[14];
char                 cond_type[]       = "DAOS_FILTER_CONDITION";
char                 aggr_type[]       = "DAOS_FILTER_AGGREGATION";
char                 eqfunc_type[]     = "DAOS_FILTER_FUNC_EQ";
char                 andfunc_type[]    = "DAOS_FILTER_FUNC_AND";
char                 akey_type[]       = "DAOS_FILTER_AKEY";
char                 const_type[]      = "DAOS_FILTER_CONST";
char                 str_type[]        = "DAOS_FILTER_TYPE_CSTRING";
char                 int_type[]        = "DAOS_FILTER_TYPE_INTEGER4";
char                 wrong_type[]      = "WRONG_TYPE";
char                *akeys[NUM_AKEYS]  = {"Owner", "Species", "Sex", "Age"};

static daos_pipeline_t *
build_incor_chained(void)
{
	daos_filter_t	*aux;
	int		rc;

	daos_pipeline_init(&pipe0);
	daos_filter_init(&comp_eq);
	daos_filter_init(&aggr_sum);

	d_iov_set(&comp_eq.filter_type, cond_type, strlen(cond_type));
	d_iov_set(&aggr_sum.filter_type, aggr_type, strlen(aggr_type));

	rc = daos_pipeline_add(&pipe0, &comp_eq);
	assert_rc_equal(rc, 0);
	rc = daos_pipeline_add(&pipe0, &aggr_sum);
	assert_rc_equal(rc, 0);

	/** switching filters to create and incorrect pipeline */
	aux                   = pipe0.filters[0];
	pipe0.filters[0]      = pipe0.aggr_filters[0];
	pipe0.aggr_filters[0] = aux;

	return &pipe0;
}

static daos_pipeline_t *
build_cor_0(void)
{
	int rc;

	daos_pipeline_init(&pipe0);
	daos_filter_init(&comp_eq);

	d_iov_set(&comp_eq.filter_type, cond_type, strlen(cond_type));

	rc = daos_pipeline_add(&pipe0, &comp_eq);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(&comp_eq, &eqfunc_fp);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(&comp_eq, &akey_fp);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(&comp_eq, &const_fp);
	assert_rc_equal(rc, 0);

	eqfunc_fp = (daos_filter_part_t){ 0 };
	d_iov_set(&eqfunc_fp.part_type, eqfunc_type, strlen(eqfunc_type));
	eqfunc_fp.num_operands = 2;

	akey_fp   = (daos_filter_part_t){ 0 };
	d_iov_set(&akey_fp.part_type, akey_type, strlen(akey_type));
	d_iov_set(&akey_fp.data_type, str_type, strlen(str_type));
	d_iov_set(&akey_fp.akey, akeys[0], strlen(akeys[0]));
	akey_fp.data_len       = VALUE_MAX_SIZE;

	const_fp  = (daos_filter_part_t){ 0 };
	d_iov_set(&const_fp.part_type, const_type, strlen(const_type));
	d_iov_set(&const_fp.data_type, str_type, strlen(str_type));
	const_fp.num_constants = 1;
	const_fp.constant      = &const_iov0;
	d_iov_set(&const_iov0, const0, strlen(const0) + 1);

	return &pipe0;
}

static daos_pipeline_t *
build_cor_1(void)
{
	int rc;

	daos_pipeline_init(&pipe0);
	daos_filter_init(&comp_eq);

	d_iov_set(&comp_eq.filter_type, cond_type, strlen(cond_type));

	rc = daos_pipeline_add(&pipe0, &comp_eq);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(&comp_eq, &andfunc_fp);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(&comp_eq, &eqfunc_fp);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(&comp_eq, &eqfunc_fp);
	assert_rc_equal(rc, 0);

	andfunc_fp = (daos_filter_part_t){ 0 };
	d_iov_set(&andfunc_fp.part_type, andfunc_type, strlen(andfunc_type));
	andfunc_fp.num_operands = 2;

	eqfunc_fp = (daos_filter_part_t){ 0 };
	d_iov_set(&eqfunc_fp.part_type, eqfunc_type, strlen(eqfunc_type));
	eqfunc_fp.num_operands = 2;

	return &pipe0;
}

static daos_pipeline_t *
build_incor_parttype(void)
{
	daos_pipeline_t *p;

	p = build_cor_0();
	/** changing the constant to an incorrect type */
	d_iov_set(&p->filters[0]->parts[2]->part_type, wrong_type, strlen(wrong_type));

	return p;
}

static daos_pipeline_t *
build_incor_numops(void)
{
	daos_pipeline_t *p;

	p = build_cor_0();
	/** changing the number of operands so they are incorrect */
	p->filters[0]->parts[0]->num_operands = 3;

	return p;
}

static daos_pipeline_t *
build_incor_numparts(void)
{
	daos_pipeline_t *p;

	p = build_cor_1();
	/** changing the number of parts so they are incorrect */
	p->filters[0]->parts[0]->num_operands = 100;

	return p;
}

static daos_pipeline_t *
build_with_notype(void)
{
	daos_pipeline_t *p;

	p = build_cor_0();
	/** removing type of constant so it is incorrectly configured */
	p->filters[0]->parts[2]->data_type = (d_iov_t){ 0 };

	return p;
}

static daos_pipeline_t *
build_with_cstring_nonullchar(void)
{
	daos_pipeline_t *p;

	p = build_cor_0();
	/** making sure there are no null chars in cstring */
	const1[strlen(const1)] = 'x';
	p->filters[0]->parts[2]->constant->iov_buf = const1;

	return p;
}

static daos_pipeline_t *
build_with_string_insane_size(void)
{
	size_t		*size_ptr;
	daos_pipeline_t	*p;
	char		*const3_strpart;

	p = build_cor_0();
	/** making sure the size of the string is not sane */
	size_ptr = (size_t *)const3;
	*size_ptr = (size_t)1000;
	const3_strpart = &const3[sizeof(size_t)];
	strncpy(const3_strpart, const0, 6);
	d_iov_set(p->filters[0]->parts[2]->constant, const3, 13);
	d_iov_set(&p->filters[0]->parts[2]->data_type, "DAOS_FILTER_TYPE_STRING",
		  strlen("DAOS_FILTER_TYPE_STRING"));

	return p;
}

static daos_pipeline_t *
build_incor_datatype(void)
{
	daos_pipeline_t *p;

	p = build_cor_0();
	/** changing the constant to an incorrect type */
	d_iov_set(&p->filters[0]->parts[2]->data_type, wrong_type, strlen(wrong_type));

	return p;
}

static daos_pipeline_t *
build_incor_ops_datatypes(void)
{
	daos_pipeline_t *p;

	p = build_cor_0();
	/** changing the constant to an integer type (instead of string) */
	d_iov_set(&p->filters[0]->parts[2]->data_type, int_type, strlen(int_type));

	return p;
}

static void
cleanup_pipe(daos_pipeline_t *p)
{
	int rc;

	if (!p)
		return;
	rc = daos_pipeline_free(p);
	assert_rc_equal(rc, 0);
}

static void
check_pipelines(void **state)
{
	daos_pipeline_t	*p0 = NULL;
	int		rc = 0;

	print_message(" A. Check that NULL pipelines get detected.\n");
	rc = daos_pipeline_check(p0);
	assert_rc_equal(rc, -DER_INVAL);

	print_message(" B. Check that incorrectly chained pipelines get detected.\n");
	p0 = build_incor_chained();
	rc = daos_pipeline_check(p0);
	assert_rc_equal(rc, -DER_INVAL);
	cleanup_pipe(p0);

	print_message(" C. Simple correct pipeline should pass.\n");
	p0 = build_cor_0();
	rc = daos_pipeline_check(p0);
	assert_rc_equal(rc, 0);
	cleanup_pipe(p0);

	print_message(" D. Check that incorrect part types get detected.\n");
	p0 = build_incor_parttype();
	rc = daos_pipeline_check(p0);
	assert_rc_equal(rc, -DER_NOSYS);
	cleanup_pipe(p0);

	print_message(" E. Check that incorrect num of operands get detected.\n");
	p0 = build_incor_numops();
	rc = daos_pipeline_check(p0);
	assert_rc_equal(rc, -DER_INVAL);
	cleanup_pipe(p0);

	print_message(" F. Check that incorrect num of parts get detected.\n");
	p0 = build_incor_numparts();
	rc = daos_pipeline_check(p0);
	assert_rc_equal(rc, -DER_INVAL);
	cleanup_pipe(p0);

	print_message(" G. Check that parts that are not functions without a type get detected.\n");
	p0 = build_with_notype();
	rc = daos_pipeline_check(p0);
	assert_rc_equal(rc, -DER_INVAL);
	cleanup_pipe(p0);

	print_message(" H. Check that CSTRING constants without ending in \\0 get detected.\n");
	p0 = build_with_cstring_nonullchar();
	rc = daos_pipeline_check(p0);
	assert_rc_equal(rc, -DER_INVAL);
	cleanup_pipe(p0);
	strcpy(const1, const0);

	print_message(" I. Check that STRING constants with an 'insane' size get detected.\n");
	p0 = build_with_string_insane_size();
	rc = daos_pipeline_check(p0);
	assert_rc_equal(rc, -DER_INVAL);
	cleanup_pipe(p0);

	print_message(" J. Check that incorrect data types get detected.\n");
	p0 = build_incor_datatype();
	rc = daos_pipeline_check(p0);
	assert_rc_equal(rc, -DER_NOSYS);
	cleanup_pipe(p0);

	print_message(" K. Check that different data types for same function get detected.\n");
	p0 = build_incor_ops_datatypes();
	rc = daos_pipeline_check(p0);
	assert_rc_equal(rc, -DER_INVAL);
	cleanup_pipe(p0);

	print_message("all good\n");
}

#define NR_IODS		4
#define STRING_MAX_LEN	10

static void
free_filter_data(daos_filter_t **filters, uint32_t nfilters)
{
	uint32_t i, j, k;

	for (i = 0; i < nfilters; i++) {
		for (j = 0; j < filters[i]->num_parts; j++) {
			free(filters[i]->parts[j]->part_type.iov_buf);
			if (filters[i]->parts[j]->data_type.iov_buf_len > 0)
				free(filters[i]->parts[j]->data_type.iov_buf);
			if (filters[i]->parts[j]->akey.iov_buf_len > 0)
				free(filters[i]->parts[j]->akey.iov_buf);
			for (k = 0; k < filters[i]->parts[j]->num_constants; k++) {
				free(filters[i]->parts[j]->constant[k].iov_buf);
			}
			if (filters[i]->parts[j]->num_constants > 0)
				free(filters[i]->parts[j]->constant);
			free(filters[i]->parts[j]);
		}
		if (filters[i]->filter_type.iov_len > 0)
			free(filters[i]->filter_type.iov_buf);
	}
}

static int
free_pipeline(daos_pipeline_t *pipe)
{
	daos_filter_t	**filters_to_free;
	uint32_t	total_filters;
	uint32_t	i;
	int		rc;

	/** saving filter ptrs so we can free them later */
	total_filters   = pipe->num_filters + pipe->num_aggr_filters;
	filters_to_free = malloc(sizeof(*filters_to_free) * total_filters);

	memcpy(filters_to_free, pipe->filters, sizeof(*filters_to_free) * pipe->num_filters);
	memcpy(&filters_to_free[pipe->num_filters], pipe->aggr_filters,
	       sizeof(*filters_to_free) * pipe->num_aggr_filters);

	/** freeing objects allocated by client */
	free_filter_data(pipe->filters, pipe->num_filters);
	free_filter_data(pipe->aggr_filters, pipe->num_aggr_filters);

	/** freeing objects allocated by DAOS */
	rc = daos_pipeline_free(pipe);

	/** freeing filters */
	for (i = 0; i < total_filters; i++)
		free(filters_to_free[i]);
	free(filters_to_free);

	return rc;
}

void
insert_simple_records(daos_handle_t oh, char *fields[])
{
	int		rc;
	d_iov_t		dkey;
	d_sg_list_t	sgls[NR_IODS];
	d_iov_t		iovs[NR_IODS];
	daos_iod_t	iods[NR_IODS];
	uint32_t	i, j;
	char		*name[] = {"Slim",   "Buffy",   "Claws", "Whistler",
				   "Chirpy", "Browser", "Fang",  "Fluffy"};
	char		*owner[] = {"Benny", "Harold", "GWen", "Gwen", "Gwen", "Diane", "Benny",
				    "Harold"};
	char		*species[] = {"snake", "dog", "cat", "bird", "bird", "dog", "dog", "cat"};
	char		*sex[]     = {"m", "f", "m", "m", "f", "m", "m", "f"};
	uint64_t	age[]     = {1, 10, 4, 2, 3, 2, 7, 9};
	void		*data[NR_IODS];

	data[0] = (void *)owner;
	data[1] = (void *)species;
	data[2] = (void *)sex;
	data[3] = (void *)age;

	print_message("records:\n");
	for (i = 0; i < 8; i++) { /** records */
		print_message("\tname(dkey)=%s%*c", name[i],
			      (int)(STRING_MAX_LEN - strlen(name[i])), ' ');
		/** set dkey for record */
		d_iov_set(&dkey, name[i], strlen(name[i]));

		for (j = 0; j < NR_IODS - 1; j++) { /** str fields */
			char **strdata = (char **)data[j];

			print_message("%s(akey)=%s%*c", fields[j], strdata[i],
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

		print_message("%s(akey)=%lu\n", fields[NR_IODS - 1], intdata[i]);
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
		assert_rc_equal(rc, 0);
	}
	print_message("\n");
}

/**
 * Build pipeline filtering by "Owner == Benny"
 */
void
build_simple_pipeline_one(daos_pipeline_t *pipeline)
{
	daos_filter_part_t	*akey_ft, *eqfunc_ft, *const_ft;
	char			*akey_ftype, *const_ftype, *eqfunc_ftype;
	size_t			akey_ftype_s, const_ftype_s, eqfunc_ftype_s;
	char			*str_type1, *str_type2;
	char			*pipe_cond_type;
	size_t			str_type_s, pipe_cond_type_s;
	char			*constant, *akey;
	size_t			akey_s;
	daos_filter_t		*comp1;
	int			rc;

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

	akey_ft = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	const_ft = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	eqfunc_ft = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	comp1 = (daos_filter_t *)calloc(1, sizeof(daos_filter_t));

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
	 *	 "Owner == Benny"  ->  |(func=eq)|(akey=Owner)|(const=Benny)|
	 */
	daos_filter_init(comp1);
	d_iov_set(&comp1->filter_type, pipe_cond_type, pipe_cond_type_s);

	rc = daos_filter_add(comp1, eqfunc_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(comp1, akey_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(comp1, const_ft);
	assert_rc_equal(rc, 0);

	/** adding the filter to the pipeline. This pipeline has only one filter */
	rc = daos_pipeline_add(pipeline, comp1);
	assert_rc_equal(rc, 0);
}

/**
 * Build pipeline filtering by "Owner == Benny AND Species == dog"
 */
void
build_simple_pipeline_two(daos_pipeline_t *pipeline)
{
	daos_filter_part_t	*akey1_ft, *eqfunc1_ft, *const1_ft;
	daos_filter_part_t	*akey2_ft, *eqfunc2_ft, *const2_ft;
	daos_filter_part_t	*andfunc_ft;
	char			*akey1_ftype, *akey2_ftype;
	char			*const1_ftype, *const2_ftype;
	size_t			akey_ftype_s, const_ftype_s;
	char			*eqfunc1_ftype, *eqfunc2_ftype, *andfunc_ftype;
	size_t			eqfunc_ftype_s, andfunc_ftype_s;
	char			*str_type1, *str_type2, *str_type3, *str_type4;
	char			*pipe_cond_type;
	size_t			str_type_s, pipe_cond_type_s;
	char			*constant1, *akey1, *constant2, *akey2;
	size_t			akey1_s, akey2_s;
	daos_filter_t		*comp_and;
	int			rc;

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
	akey2_ft   = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&akey2_ft->part_type, akey2_ftype, akey_ftype_s);
	d_iov_set(&akey2_ft->data_type, str_type2, str_type_s);
	d_iov_set(&akey2_ft->akey, akey2, akey2_s);
	akey2_ft->data_len = STRING_MAX_LEN;

	/** constant1 for filter */
	const1_ft  = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
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
	eqfunc2_ft       = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&eqfunc2_ft->part_type, eqfunc2_ftype, eqfunc_ftype_s);
	eqfunc2_ft->num_operands = 2;

	/** function3 for filter (and) */
	andfunc_ft       = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&andfunc_ft->part_type, andfunc_ftype, andfunc_ftype_s);
	andfunc_ft->num_operands = 2;

	/**
	 * building a pipeline condition filter:
	 *    the order of operands is prefix:
	 * "Owner == Benny AND Species == dog"  ->
	 * |(func=and)|(func=eq)|(akey=Owner)|(const=Benny)|(func=eq)|(akey=Species)|(const=dog)|
	 */
	comp_and = (daos_filter_t *)calloc(1, sizeof(daos_filter_t));
	daos_filter_init(comp_and);
	d_iov_set(&comp_and->filter_type, pipe_cond_type, pipe_cond_type_s);

	rc = daos_filter_add(comp_and, andfunc_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(comp_and, eqfunc1_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(comp_and, akey1_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(comp_and, const1_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(comp_and, eqfunc2_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(comp_and, akey2_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(comp_and, const2_ft);
	assert_rc_equal(rc, 0);

	/** adding the filter to the pipeline. This pipeline has only one filter */
	rc = daos_pipeline_add(pipeline, comp_and);
	assert_rc_equal(rc, 0);
}

/**
 * Build pipeline filtering by "Owner == Benny", aggregate by "SUM(age)"
 */
void
build_simple_pipeline_three(daos_pipeline_t *pipeline)
{
	daos_filter_part_t	*akey1_ft, *eqfunc_ft, *const_ft;
	daos_filter_part_t	*akey2_ft;
	daos_filter_part_t	*sumfunc_ft;
	char			*akey1_ftype, *akey2_ftype, *const_ftype;
	size_t			akey_ftype_s, const_ftype_s;
	char			*eqfunc_ftype, *sumfunc_ftype;
	size_t			eqfunc_ftype_s, sumfunc_ftype_s;
	char			*str_type1, *str_type2;
	char			*uint_type1;
	size_t			str_type_s, uint_type_s;
	char			*pipe_cond_type, *pipe_aggr_type;
	size_t			pipe_cond_type_s, pipe_aggr_type_s;
	char			*constant, *akey1, *akey2;
	size_t			akey1_s, akey2_s;
	daos_filter_t		*comp3, *aggr3;
	int			rc;

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
	akey2_ft   = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&akey2_ft->part_type, akey2_ftype, akey_ftype_s);
	d_iov_set(&akey2_ft->data_type, uint_type1, uint_type_s);
	d_iov_set(&akey2_ft->akey, akey2, akey2_s);
	akey2_ft->data_len = sizeof(uint64_t);

	/** constant for filter */
	const_ft   = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
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
	sumfunc_ft      = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&sumfunc_ft->part_type, sumfunc_ftype, sumfunc_ftype_s);
	sumfunc_ft->num_operands = 1;

	/**
	 * building a pipeline with a condition filter and an aggregation filter:
	 *    the order of operands is prefix:
	 * "Owner == Benny" -> |(func=eq) |(akey=Owner)|(const=Benny)|
	 * SUM(age) -> |(func=sum)|(akey=Age)|
	 */
	comp3  = (daos_filter_t *)calloc(1, sizeof(daos_filter_t));
	daos_filter_init(comp3);
	aggr3 = (daos_filter_t *)calloc(1, sizeof(daos_filter_t));
	daos_filter_init(aggr3);

	d_iov_set(&comp3->filter_type, pipe_cond_type, pipe_cond_type_s);
	d_iov_set(&aggr3->filter_type, pipe_aggr_type, pipe_aggr_type_s);

	rc = daos_filter_add(comp3, eqfunc_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(comp3, akey1_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(comp3, const_ft);
	assert_rc_equal(rc, 0);

	rc = daos_filter_add(aggr3, sumfunc_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(aggr3, akey2_ft);
	assert_rc_equal(rc, 0);

	/** adding the filters to the pipeline. This pipeline has two filters */
	rc = daos_pipeline_add(pipeline, comp3);
	assert_rc_equal(rc, 0);
	rc = daos_pipeline_add(pipeline, aggr3);
	assert_rc_equal(rc, 0);
}

/**
 * Build pipeline filtering by "Age & 1 > 0"
 */
void
build_simple_pipeline_four(daos_pipeline_t *pipeline)
{
	daos_filter_part_t	*akey_ft, *bitandfunc_ft, *const1_ft, *const2_ft;
	daos_filter_part_t	*gtfunc_ft;
	char			*akey_ftype, *const1_ftype, *const2_ftype;
	char			*bitandfunc_ftype, *gtfunc_ftype;
	size_t			akey_ftype_s, const_ftype_s, bitandfunc_ftype_s;
	size_t			gtfunc_ftype_s;
	char			*uint_type1, *uint_type2, *uint_type3;
	char			*pipe_cond_type;
	size_t			uint_type_s, pipe_cond_type_s;
	char			*akey;
	size_t			akey_s;
	uint64_t		*constant1, *constant2;
	daos_filter_t		*func_bitand;
	int			rc;

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

	constant1  = (uint64_t *)malloc(sizeof(uint64_t));
	constant2  = (uint64_t *)malloc(sizeof(uint64_t));
	*constant1 = 1;
	*constant2 = 0;

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
	const1_ft = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
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
	gtfunc_ft   = (daos_filter_part_t *)calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&gtfunc_ft->part_type, gtfunc_ftype, gtfunc_ftype_s);
	gtfunc_ft->num_operands = 2;

	/**
	 * building a pipeline condition filter:
	 *    the order of operands is prefix:
	 *     "Age & 1 > 0" -> |(func=gt)|(func=bitand)|(akey=Age)|(const=1)|(const=0)|
	 */
	func_bitand     = (daos_filter_t *)calloc(1, sizeof(daos_filter_t));
	daos_filter_init(func_bitand);
	d_iov_set(&func_bitand->filter_type, pipe_cond_type, pipe_cond_type_s);

	rc = daos_filter_add(func_bitand, gtfunc_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(func_bitand, bitandfunc_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(func_bitand, akey_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(func_bitand, const1_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(func_bitand, const2_ft);
	assert_rc_equal(rc, 0);

	/** adding the filter to the pipeline. This pipeline has only one filter  */
	rc = daos_pipeline_add(pipeline, func_bitand);
	assert_rc_equal(rc, 0);
}

void
run_simple_pipeline(daos_handle_t coh, daos_handle_t oh, daos_pipeline_t *pipeline, char *fields[],
		    int nr_aggr)
{
	daos_iod_t		*iods = NULL;
	daos_anchor_t		anchor;
	uint32_t		nr_iods;
	uint32_t		nr_kds;
	daos_key_desc_t		*kds = NULL;
	d_sg_list_t		sgl_keys;
	d_iov_t			*iov_keys = NULL;
	char			*buf_keys = NULL;
	d_sg_list_t		sgl_recx;
	d_iov_t			*iov_recx = NULL;
	char			*buf_recx = NULL;
	daos_size_t		*recx_size = NULL;
	d_sg_list_t		sgl_aggr;
	d_iov_t			*iovs_aggr = NULL;
	char			*buf_aggr = NULL;
	daos_pipeline_stats_t	stats = {0};
	uint32_t		i, j;
	int			rc;

	/**
	 * reading in chunks of 64 keys (at most) at a time
	 */
	nr_kds    = 64;
	nr_iods   = NR_IODS;

	kds       = malloc(sizeof(*kds) * nr_kds);
	iods      = malloc(sizeof(*iods) * nr_iods);

	/**
	 * iods: information about what akeys to retrieve
	 */
	for (i = 0; i < nr_iods; i++) {
		iods[i].iod_nr    = 1;
		iods[i].iod_size  = STRING_MAX_LEN;
		iods[i].iod_recxs = NULL;
		iods[i].iod_type  = DAOS_IOD_SINGLE;
		d_iov_set(&iods[i].iod_name, (void *)fields[i], strlen(fields[i]));
	}

	/** sgl_keys: to store the retrieved dkeys */
	sgl_keys.sg_nr        = 1;
	sgl_keys.sg_nr_out    = 0;
	iov_keys              = malloc(sizeof(*iov_keys));
	sgl_keys.sg_iovs      = iov_keys;
	buf_keys              = malloc(nr_kds * STRING_MAX_LEN);
	iov_keys->iov_buf     = buf_keys;
	iov_keys->iov_buf_len = nr_kds * STRING_MAX_LEN;
	iov_keys->iov_len     = 0;

	/** sgl_recx and recx_size: to store the retrieved data for the akeys of each dkey */
	sgl_recx.sg_nr        = 1;
	sgl_recx.sg_nr_out    = 0;
	iov_recx              = malloc(sizeof(*iov_recx));
	sgl_recx.sg_iovs      = iov_recx;
	buf_recx              = malloc(nr_iods * nr_kds * STRING_MAX_LEN);
	iov_recx->iov_buf     = buf_recx;
	iov_recx->iov_buf_len = nr_iods * nr_kds * STRING_MAX_LEN;
	iov_recx->iov_len     = 0;
	recx_size             = malloc(sizeof(*recx_size) * nr_iods * nr_kds);

	/** sgl_aggr: for aggregation of data */
	sgl_aggr.sg_nr     = nr_aggr;
	sgl_aggr.sg_nr_out = 0;
	if (nr_aggr > 0) {
		iovs_aggr          = malloc(sizeof(*iovs_aggr) * nr_aggr);
		sgl_aggr.sg_iovs   = iovs_aggr;
		buf_aggr           = malloc(sizeof(double) * nr_aggr);
		for (i = 0; i < nr_aggr; i++) {
			iovs_aggr->iov_buf     = (void *)&buf_aggr[i];
			iovs_aggr->iov_buf_len = sizeof(double);
			iovs_aggr->iov_len     = 0;
		}
	}

	/** reset anchor */
	memset(&anchor, 0, sizeof(daos_anchor_t));

	/** until anchor is EOF we call pipeline run*/
	while (!daos_anchor_is_eof(&anchor)) {
		char   *dkey, *rec;
		size_t dkey_s, rec_s;

		/** restoring value for in/out parameters */
		nr_kds  = 64; /** trying to read 64 at a time again */
		nr_iods = NR_IODS;
		/** calling pipeline run */
		rc = daos_pipeline_run(coh, oh, pipeline, DAOS_TX_NONE, 0, NULL, &nr_iods, iods,
				       &anchor, &nr_kds, kds, &sgl_keys, &sgl_recx, recx_size,
				       &sgl_aggr, &stats, NULL);
		assert_rc_equal(rc, 0);

		/** process nr_kds fetched records */
		dkey = (char *)sgl_keys.sg_iovs->iov_buf;
		rec  = (char *)sgl_recx.sg_iovs->iov_buf;
		for (i = 0; i < nr_kds; i++) {
			dkey_s  = kds[i].kd_key_len;

			print_message("\tname(dkey)=%.*s%*c", (int)dkey_s, dkey,
				      (int)(STRING_MAX_LEN - dkey_s), ' ');
			dkey += dkey_s;

			for (j = 0; j < nr_iods - 1; j++) {
				rec_s = recx_size[i * nr_iods + j];

				print_message("%.*s(akey)=%.*s%*c", (int)iods[j].iod_name.iov_len,
					      (char *)iods[j].iod_name.iov_buf, (int)rec_s, rec,
					      (int)(STRING_MAX_LEN - rec_s), ' ');
				rec += rec_s;
			}
			print_message("%.*s(akey)=%lu\n", (int)iods[nr_iods - 1].iod_name.iov_len,
				      (char *)iods[nr_iods - 1].iod_name.iov_buf,
				      *((uint64_t *)rec));
			rec += sizeof(uint64_t);
		}
	}
	print_message("\t(scanned %lu dkeys)\n", stats.nr_dkeys);
	for (i = 0; i < nr_aggr; i++) {
		double *res = (double *)sgl_aggr.sg_iovs[i].iov_buf;

		print_message("  ---agg result[%u]=%f---\n", i, *res);
	}
	print_message("\n");

	free(kds);
	free(iods);
	free(iov_keys);
	free(buf_keys);
	free(iov_recx);
	free(buf_recx);
	free(recx_size);
	if (nr_aggr > 0) {
		free(iovs_aggr);
		free(buf_aggr);
	}
}

static void
simple_pipeline(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t   oid;
	int             rc;
	daos_handle_t	coh, oh;
	daos_pipeline_t pipeline1, pipeline2, pipeline3, pipeline4;
	static char	*fields[NR_IODS] = {"Owner", "Species", "Sex", "Age"};
	int		nr_aggr;

	rc = daos_cont_create_with_label(arg->pool.poh, "simple_pipeline_cont", NULL, NULL, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_cont_open(arg->pool.poh, "simple_pipeline_cont", DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	/** create/open object */
	oid.hi = 0;
	oid.lo = 4;
	daos_obj_generate_oid(coh, &oid, DAOS_OT_MULTI_LEXICAL, OC_SX, 0, 0);

	rc = daos_obj_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** Insert some example records */
	insert_simple_records(oh, fields);

	/** init pipeline1 object */
	daos_pipeline_init(&pipeline1);
	/** FILTER "Owner == Benny" */
	build_simple_pipeline_one(&pipeline1);
	/** checking that the pipe is well constructed */
	rc = daos_pipeline_check(&pipeline1);
	assert_rc_equal(rc, 0);
	print_message("filtering by (Owner=Benny):\n");
	/** Running pipeline */
	nr_aggr = 0;
	run_simple_pipeline(coh, oh, &pipeline1, fields, nr_aggr);

	/** init pipeline2 object */
	daos_pipeline_init(&pipeline2);
	/** FILTER "Owner == Benny AND Species == dog" */
	build_simple_pipeline_two(&pipeline2);
	/** checking that the pipe is well constructed */
	rc = daos_pipeline_check(&pipeline2);
	assert_rc_equal(rc, 0);
	print_message("filtering by (Owner=Benny AND Species=dog):\n");
	/** Running pipeline */
	run_simple_pipeline(coh, oh, &pipeline2, fields, nr_aggr);

	/** init pipeline3 object */
	daos_pipeline_init(&pipeline3);
	/** FILTER "Owner == Benny", AGGREGATE "SUM(age)" */
	build_simple_pipeline_three(&pipeline3);
	/** checking that the pipe is well constructed */
	rc = daos_pipeline_check(&pipeline3);
	assert_rc_equal(rc, 0);
	print_message("filtering by (Owner=Benny), aggregating by SUM(age):\n");
	/** Running pipeline */
	nr_aggr = 1;
	run_simple_pipeline(coh, oh, &pipeline3, fields, nr_aggr);
	nr_aggr = 0;

	/** init pipeline4 object */
	daos_pipeline_init(&pipeline4);
	/** FILTER "Age & 1" */
	build_simple_pipeline_four(&pipeline4);
	/** checking that the pipe is well constructed */
	rc = daos_pipeline_check(&pipeline4);
	assert_rc_equal(rc, 0);
	print_message("filtering by (Age & 1) > 0:\n");
	/** Running pipeline */
	run_simple_pipeline(coh, oh, &pipeline4, fields, nr_aggr);

	/** Freeing used memory */
	rc = free_pipeline(&pipeline1);
	assert_rc_equal(rc, 0);
	rc = free_pipeline(&pipeline2);
	assert_rc_equal(rc, 0);
	rc = free_pipeline(&pipeline3);
	assert_rc_equal(rc, 0);
	rc = free_pipeline(&pipeline4);
	assert_rc_equal(rc, 0);

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, "simple_pipeline_cont", 0, NULL);
	assert_rc_equal(rc, 0);
}

#define NR_RECXS	4

void
insert_array_records(daos_handle_t oh, char field[])
{
	d_iov_t		dkey;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	char		record_data[18];
	daos_iod_t	iod;
	void		*data[NR_RECXS];
	daos_recx_t	recxs[NR_RECXS];
	uint32_t	i, j;
	uint64_t	ID[] = {1, 2, 3, 4, 5, 6, 7, 8};
	uint64_t	DATA_RX_IDX_0[] = {10, 20, 11, 21, 50, 51, 52, 3};
	uint32_t	DATA_RX_IDX_10[] = {100, 200, 110, 210, 500, 510, 520, 30};
	uint32_t	DATA_RX_IDX_14[] = {10, 9, 8, 7, 6, 5, 4, 3};
	uint16_t	DATA_RX_IDX_31[] = {2, 4, 6, 8, 16, 32, 64, 128};
	int		rc;

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

	print_message("records:\n");
	for (i = 0; i < 8; i++) {
		size_t offset;

		print_message("\tid(dkey)=%lu\t", ID[i]);
		/** set dkey for record */
		d_iov_set(&dkey, &ID[i], 8);

		/** set akey */
		offset = 0;
		print_message("%s(akey) -->> ", field);
		for (j = 0; j < NR_RECXS; j++) {
			char *data_j = (char *)data[j];

			print_message("rx[%lu:%lu]=", recxs[j].rx_idx, recxs[j].rx_nr);
			if (recxs[j].rx_nr == 8) {
				print_message("%lu\t", ((uint64_t *)data_j)[i]);
				memcpy(&record_data[offset], &data_j[i * 8], 8);
				offset += 8;
			} else if (recxs[j].rx_nr == 4) {
				print_message("%u\t", ((uint32_t *)data_j)[i]);
				memcpy(&record_data[offset], &data_j[i * 4], 4);
				offset += 4;
			} else {
				print_message("%hu\t", ((uint16_t *)data_j)[i]);
				memcpy(&record_data[offset], &data_j[i * 2], 2);
				offset += 2;
			}
		}
		print_message("\n");

		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs   = &iov;
		d_iov_set(&iov, record_data, 18);

		d_iov_set(&iod.iod_name, (void *)field, strlen(field));
		iod.iod_nr    = NR_RECXS;
		iod.iod_size  = 1;
		iod.iod_recxs = recxs;
		iod.iod_type  = DAOS_IOD_ARRAY;

		rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
		assert_rc_equal(rc, 0);
	}
	print_message("\n");
}

/**
 * Build pipeline filtering by "Array[0:8] < 50"
 */
void
build_array_pipeline_one(daos_pipeline_t *pipeline)
{
	char			*int_type1, *int_type2, *akey_ftype;
	size_t			int_type_s, akey_ftype_s;
	char			*const_ftype, *akey, *ltfunc_ftype;
	size_t			const_ftype_s, akey_s, ltfunc_ftype_s;
	char			*pipe_cond_type;
	size_t			pipe_cond_type_s;
	uint64_t		*constant;
	daos_filter_part_t	*akey_ft, *ltfunc_ft, *const_ft;
	daos_filter_t		*comp_lt;
	int			rc;

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
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(comp_lt, akey_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(comp_lt, const_ft);
	assert_rc_equal(rc, 0);

	/** adding the filter to the pipeline. This pipeline has only one filter */
	rc = daos_pipeline_add(pipeline, comp_lt);
	assert_rc_equal(rc, 0);
}

/**
 * Build pipeline filtering by "Array[10:4] > 500"
 */
void
build_array_pipeline_two(daos_pipeline_t *pipeline)
{
	char			*int_type1, *int_type2, *akey_ftype;
	size_t			int_type_s, akey_ftype_s;
	char			*const_ftype, *akey, *gtfunc_ftype;
	size_t			const_ftype_s, akey_s, gtfunc_ftype_s;
	char			*pipe_cond_type;
	size_t			pipe_cond_type_s;
	uint32_t		*constant;
	daos_filter_part_t	*akey_ft, *gtfunc_ft, *const_ft;
	daos_filter_t		*comp_gt;
	int			rc;

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
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(comp_gt, akey_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(comp_gt, const_ft);
	assert_rc_equal(rc, 0);

	/** adding the filter to the pipeline. This pipeline has only one filter */
	rc = daos_pipeline_add(pipeline, comp_gt);
	assert_rc_equal(rc, 0);
}

void
run_array_pipeline(daos_handle_t coh, daos_handle_t oh, daos_pipeline_t *pipeline, char field[])
{
	daos_iod_t		iod;
	daos_anchor_t		anchor;
	uint32_t		nr_iods, nr_kds;
	daos_key_desc_t		*kds;
	d_sg_list_t		sgl_keys;
	d_iov_t			*iovs_keys;
	char			*buf_keys;
	d_sg_list_t		sgl_recs;
	d_iov_t			*iovs_recs;
	char			*buf_recs;
	daos_recx_t		recxs[NR_RECXS];
	daos_pipeline_stats_t	stats = {0};
	uint32_t		i, j;
	int			rc;

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
	nr_iods         = 1;

	/* to store retrieved dkeys */
	kds                   = malloc(sizeof(*kds) * nr_kds);
	iovs_keys             = malloc(sizeof(*iovs_keys) * nr_kds);
	sgl_keys.sg_nr        = nr_kds;
	sgl_keys.sg_nr_out    = 0;
	sgl_keys.sg_iovs      = iovs_keys;
	buf_keys              = malloc(nr_kds * 8);
	/* to store retrieved data */
	iovs_recs             = malloc(sizeof(*iovs_recs) * nr_kds);
	sgl_recs.sg_nr        = nr_kds;
	sgl_recs.sg_nr_out    = 0;
	sgl_recs.sg_iovs      = iovs_recs;
	buf_recs              = malloc(18 * nr_kds);

	for (i = 0; i < nr_kds; i++) {
		d_iov_set(&iovs_keys[i], &buf_keys[i * 8], 8);
		d_iov_set(&iovs_recs[i], &buf_recs[i * 18], 18);
	}
	/**
	 * iods: information about what akeys to retrieve
	 */
	iod.iod_nr    = NR_RECXS;
	iod.iod_size  = 1; /* we interpret it as an array of bytes */
	iod.iod_recxs = recxs;
	iod.iod_type  = DAOS_IOD_ARRAY;
	d_iov_set(&iod.iod_name, (char *)field, strlen(field));

	/** reset anchor */
	memset(&anchor, 0, sizeof(daos_anchor_t));

	/** until anchor is EOF we call pipeline run */
	while (!daos_anchor_is_eof(&anchor)) {
		/** restorin value for in/out parameters */
		nr_kds  = 64; /** trying to read 64 in each iteration */
		nr_iods = 1;

		/** pipeline run */
		rc = daos_pipeline_run(coh, oh, pipeline, DAOS_TX_NONE, 0, NULL, &nr_iods, &iod,
				       &anchor, &nr_kds, kds, &sgl_keys, &sgl_recs, NULL, NULL,
				       &stats, NULL);
		assert_rc_equal(rc, 0);

		/** processing nr_kds records */
		for (i = 0; i < nr_kds; i++) {
			uint64_t	dkey = *((uint64_t *)sgl_keys.sg_iovs[i].iov_buf);
			char		*akey_data;
			size_t		offset;

			print_message("\tid(dkey)=%lu\t", dkey);
			print_message("%s(akey) -->> ", field);

			offset    = 0;
			akey_data = (char *)sgl_recs.sg_iovs[i].iov_buf;

			for (j = 0; j < NR_RECXS; j++) {
				char *data_j = &akey_data[offset];

				print_message("rx[%lu:%lu]=", recxs[j].rx_idx, recxs[j].rx_nr);
				if (recxs[j].rx_nr == 8) {
					print_message("%lu\t", *((uint64_t *)data_j));
					offset += 8;
				} else if (recxs[j].rx_nr == 4) {
					print_message("%u\t", *((uint32_t *)data_j));
					offset += 4;
				} else {
					print_message("%hu\t", *((uint16_t *)data_j));
					offset += 2;
				}
			}
			print_message("\n");
		}
	}
	print_message("\t(scanned %lu dkeys)\n\n", stats.nr_dkeys);

	free(kds);
	free(iovs_keys);
	free(buf_keys);
	free(iovs_recs);
	free(buf_recs);
}

static void
simple_pipeline_arrays(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	coh, oh;
	daos_pipeline_t pipeline1, pipeline2;
	static char	field[] = "Array";
	int		rc;

	rc = daos_cont_create_with_label(arg->pool.poh, "simple_pipeline_arrays", NULL, NULL, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_cont_open(arg->pool.poh, "simple_pipeline_arrays", DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	/** create/open object */
	oid.hi = 0;
	oid.lo = 4;
	daos_obj_generate_oid(coh, &oid, DAOS_OT_MULTI_LEXICAL, OC_SX, 0, 0);

	rc = daos_obj_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** Insert some example records */
	insert_array_records(oh, field);

	/** init pipeline1 object */
	daos_pipeline_init(&pipeline1);
	/** FILTER "Array[0:8] < 50" */
	build_array_pipeline_one(&pipeline1);
	/** checking that the pipe is well constructed */
	rc = daos_pipeline_check(&pipeline1);
	assert_rc_equal(rc, 0);
	print_message("filtering by (Array[0:8] < 50):\n");
	/** Running pipeline */
	run_array_pipeline(coh, oh, &pipeline1, field);

	/** init pipeline2 object */
	daos_pipeline_init(&pipeline2);
	/** FILTER "Array[10:4] > 500" */
	build_array_pipeline_two(&pipeline2);
	/** checking that the pipe is well constructed */
	rc = daos_pipeline_check(&pipeline2);
	assert_rc_equal(rc, 0);
	print_message("filtering by (Array[10:4] > 500):\n");
	/** Running pipeline */
	run_array_pipeline(coh, oh, &pipeline2, field);

	/** Freeing used memory */
	rc = free_pipeline(&pipeline1);
	assert_rc_equal(rc, 0);
	rc = free_pipeline(&pipeline2);
	assert_rc_equal(rc, 0);

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, "simple_pipeline_arrays", 0, NULL);
	assert_rc_equal(rc, 0);
}

#define FSIZE		15
#define NUM_DKEYS	1024
static time_t		ts;

static void
insert_dfs_records(daos_handle_t oh, char field[])
{
	int		rc;
	d_iov_t		dkey;
	d_sg_list_t	sgl;
	d_iov_t		sg_iovs[NR_RECXS];
	daos_iod_t	iod;
	daos_recx_t	recx;
	uint32_t	i;
	char		fname[FSIZE];
	time_t		atime, ctime, mtime;
	mode_t		mode;

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
		assert_rc_equal(rc, 0);
	}
}

#define BIN_F	"DAOS_FILTER_TYPE_BINARY"
#define DKEY_F	"DAOS_FILTER_DKEY"
#define AKEY_F	"DAOS_FILTER_AKEY"
#define CONST_F	"DAOS_FILTER_CONST"
#define INT8_F	"DAOS_FILTER_TYPE_UINTEGER8"
#define INT4_F	"DAOS_FILTER_TYPE_UINTEGER4"
#define LIKE_F	"DAOS_FILTER_FUNC_LIKE"
#define GT_F	"DAOS_FILTER_FUNC_GT"
#define EQ_F	"DAOS_FILTER_FUNC_EQ"
#define BA_F	"DAOS_FILTER_FUNC_BITAND"
#define AND_F	"DAOS_FILTER_FUNC_AND"
#define OR_F	"DAOS_FILTER_FUNC_OR"
#define COND_F	"DAOS_FILTER_CONDITION"
#define NAME_F	"%.9%"

static mode_t	constant1 = S_IFMT;
static mode_t	constant2 = S_IFDIR;

static d_iov_t	dkey_iov;
static d_iov_t	const1_iov;
static d_iov_t	const2_iov;
static d_iov_t	const3_iov;

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

daos_filter_t	pipef;

/**
 *
 */
static void
build_dfs_pipeline_one(daos_pipeline_t *pipeline, char field[])
{
	daos_size_t	bin_flen	= strlen(BIN_F);
	daos_size_t	dkey_flen	= strlen(DKEY_F);
	daos_size_t	akey_flen	= strlen(AKEY_F);
	daos_size_t	const_flen	= strlen(CONST_F);
	daos_size_t	int8_flen	= strlen(INT8_F);
	daos_size_t	int4_flen	= strlen(INT4_F);
	daos_size_t	like_flen	= strlen(LIKE_F);
	daos_size_t	gt_flen		= strlen(GT_F);
	daos_size_t	eq_flen		= strlen(EQ_F);
	daos_size_t	ba_flen		= strlen(BA_F);
	daos_size_t	and_flen	= strlen(AND_F);
	daos_size_t	or_flen		= strlen(OR_F);
	daos_size_t	cond_flen	= strlen(COND_F);
	daos_size_t	name_len	= strlen(NAME_F);
	daos_size_t	akey_len	= strlen(field);
	int		rc;

	/** build condition for dkey containing ".9" */

	d_iov_set(&dkey_ft.part_type, DKEY_F, dkey_flen);
	d_iov_set(&dkey_ft.data_type, BIN_F, bin_flen);
	dkey_ft.data_len = FSIZE;

	d_iov_set(&const0_ft.part_type, CONST_F, const_flen);
	d_iov_set(&const0_ft.data_type, BIN_F, bin_flen);
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
	assert_rc_equal(rc, 0);

	rc = daos_filter_add(&pipef, &eq_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(&pipef, &ba_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(&pipef, &akey1_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(&pipef, &const1_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(&pipef, &const2_ft);
	assert_rc_equal(rc, 0);

	rc = daos_filter_add(&pipef, &and_ft);
	assert_rc_equal(rc, 0);

	rc = daos_filter_add(&pipef, &like_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(&pipef, &dkey_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(&pipef, &const0_ft);
	assert_rc_equal(rc, 0);

	rc = daos_filter_add(&pipef, &gt_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(&pipef, &akey2_ft);
	assert_rc_equal(rc, 0);
	rc = daos_filter_add(&pipef, &const3_ft);
	assert_rc_equal(rc, 0);

	rc = daos_pipeline_add(pipeline, &pipef);
	assert_rc_equal(rc, 0);
}

static void
run_dfs_pipeline(daos_handle_t coh, daos_handle_t oh, daos_pipeline_t *pipeline, char field[])
{
	daos_iod_t	iod;
	daos_anchor_t	anchor;
	uint32_t	nr_iods, nr_kds;
	daos_key_desc_t	*kds;
	d_sg_list_t	sgl_keys;
	d_iov_t		*iov_keys;
	char		*buf_keys;
	d_sg_list_t	sgl_recs;
	d_iov_t		*iovs_recs;
	char		*buf_recs;
	daos_recx_t	recxs[2];
	daos_pipeline_stats_t  stats = {0};
	uint32_t	i;
	int		rc;

	/* record extensions for akey's array */
	recxs[0].rx_idx = 0;
	recxs[0].rx_nr  = sizeof(mode_t);
	recxs[1].rx_idx = 12;
	recxs[1].rx_nr  = sizeof(time_t);

	/* reading chunks of 16 keys (at most) at a time */
	nr_kds    = 16;
	nr_iods   = 1;

	/* to store retrieved dkeys */
	kds                   = malloc(sizeof(*kds) * nr_kds);
	iov_keys              = malloc(sizeof(*iov_keys));
	sgl_keys.sg_nr        = 1;
	sgl_keys.sg_nr_out    = 0;
	sgl_keys.sg_iovs      = iov_keys;
	buf_keys              = malloc(FSIZE * nr_kds);
	d_iov_set(iov_keys, buf_keys, FSIZE * nr_kds);

	/* to store retrieved data */
	iovs_recs             = malloc(sizeof(*iovs_recs) * nr_kds);
	sgl_recs.sg_nr        = nr_kds;
	sgl_recs.sg_nr_out    = 0;
	sgl_recs.sg_iovs      = iovs_recs;
	buf_recs              = malloc((sizeof(mode_t) + sizeof(time_t)) * nr_kds);

	for (i = 0; i < nr_kds; i++) {
		d_iov_set(&iovs_recs[i], &buf_recs[i * (sizeof(mode_t) + sizeof(time_t))],
			  sizeof(mode_t) + sizeof(time_t));
	}

	iod.iod_nr    = 2;
	iod.iod_size  = 1; /* we interpret it as an array of bytes */
	iod.iod_recxs = recxs;
	iod.iod_type  = DAOS_IOD_ARRAY;
	d_iov_set(&iod.iod_name, (char *)field, strlen(field));

	/** reset anchor */
	memset(&anchor, 0, sizeof(daos_anchor_t));

	/** calling pipeline run until EOF */
	while (!daos_anchor_is_eof(&anchor)) {
		nr_kds = 16; /** trying to read 16 in each iteration */
		nr_iods = 1;

		rc     = daos_pipeline_run(coh, oh, pipeline, DAOS_TX_NONE, 0, NULL,
					   &nr_iods, &iod, &anchor, &nr_kds, kds, &sgl_keys,
					   &sgl_recs, NULL, NULL, &stats, NULL);
		assert_rc_equal(rc, 0);

		/** processing nr_kds records */
		size_t off     = 0;
		char *dkey_ptr = (char *)sgl_keys.sg_iovs->iov_buf;

		for (i = 0; i < nr_kds; i++) {
			char       *dkey     = &dkey_ptr[off];
			daos_size_t dkeylen  = kds[i].kd_key_len;

			off                 += dkeylen;
			print_message("\t(dkey)=%.*s, len = %zu\t", (int)dkeylen, dkey, dkeylen);

			char  *ptr      = &buf_recs[i * (sizeof(mode_t) + sizeof(time_t))];
			mode_t cur_mode = *((mode_t *)ptr);

			if (S_ISDIR(cur_mode)) {
				print_message("MODE type = S_IFDIR\n");
			} else if (S_ISREG(cur_mode)) {
				print_message("MODE type = S_IFREG\n");
			} else {
				print_error("invalid mode_t retrieved!\n");
				assert_true(S_ISDIR(cur_mode) || S_ISREG(cur_mode));
			}
		}
	}
	print_message("\tNumber of dkeys scanned: %zu\n\n", stats.nr_dkeys);
	assert_int_equal(stats.nr_dkeys, NUM_DKEYS);

	free(kds);
	free(iov_keys);
	free(iovs_recs);
	free(buf_recs);
	free(buf_keys);
}

static void
simple_pipeline_dfs(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	coh, oh;
	daos_pipeline_t pipeline1;
	static char	field[] = "DFS_ENTRY";
	int		rc;

	rc = daos_cont_create_with_label(arg->pool.poh, "simple_pipeline_dfs", NULL, NULL, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_cont_open(arg->pool.poh, "simple_pipeline_dfs", DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	/** create/open object */
	oid.hi = 0;
	oid.lo = 4;
	daos_obj_generate_oid(coh, &oid, DAOS_OT_MULTI_LEXICAL, OC_SX, 0, 0);

	rc = daos_obj_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** Insert some example records */
	insert_dfs_records(oh, field);

	/** init pipeline1 object */
	daos_pipeline_init(&pipeline1);
	build_dfs_pipeline_one(&pipeline1, field);
	rc = daos_pipeline_check(&pipeline1);
	assert_rc_equal(rc, 0);
	run_dfs_pipeline(coh, oh, &pipeline1, field);
	/** TODO - should have API to free internally allocated filters. */
	free(pipeline1.filters);

	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, "simple_pipeline_dfs", 0, NULL);
	assert_rc_equal(rc, 0);
}

static const struct CMUnitTest pipeline_tests[] = {
	{"DAOS_PIPELINE1: Testing daos_pipeline_check",
	 check_pipelines, async_disable, NULL},
	{"DAOS_PIPELINE2: Testing simple pipeline",
	 simple_pipeline, async_disable, NULL},
	{"DAOS_PIPELINE3: Testing simple pipeline arrays",
	 simple_pipeline_arrays, async_disable, NULL},
	{"DAOS_PIPELINE4: Testing simple pipeline for DFS Entry",
	 simple_pipeline_dfs, async_disable, NULL},
};

int
pipeline_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE, 0, NULL);
}
#endif

int
run_daos_pipeline_test(int rank, int size)
{
	int rc = 0;

#if BUILD_PIPELINE
	rc = cmocka_run_group_tests_name("DAOS_Pipeline", pipeline_tests, pipeline_setup,
					 test_teardown);
#else
	print_message("DAOS PIPELINE is not enabled in release builds\n");
#endif
	par_barrier(PAR_COMM_WORLD);
	return rc;
}
