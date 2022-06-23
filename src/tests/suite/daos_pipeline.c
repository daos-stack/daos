/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * src/tests/suite/daos_pipeline.c
 */
#include <daos.h>
#include <daos_pipeline.h>
#include "daos_test.h"

#define NUM_AKEYS       4
#define VALUE_MAX_SIZE  10

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
	int             rc;
	daos_filter_t  *aux;

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
	size_t           *size_ptr;
	daos_pipeline_t  *p;
	char             *const3_strpart;

	p = build_cor_0();
	/** making sure the size of the string is not sane */
	size_ptr       = (size_t *)const3;
	*size_ptr      = (size_t)1000; /* strlen(const0) is 5 */
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
	if (!p)
		return;
	daos_pipeline_free(p);
}

static void
check_pipelines(void **state)
{
	daos_pipeline_t  *p0 = NULL;
	int               rc = 0;

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

static const struct CMUnitTest pipeline_tests[] = {
	{"PIPELINE: Testing daos_pipeline_check",
	 check_pipelines, async_disable, NULL},
};

int
pipeline_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE, 0, NULL);
}

int
run_daos_pipeline_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("DAOS_PIPELINE_API", pipeline_tests, pipeline_setup,
					 test_teardown);
	par_barrier(PAR_COMM_WORLD);
	return rc;
}
