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

#define NR_RECXS	4
#define FSIZE		15

static daos_handle_t	poh; /** pool */
static daos_handle_t	coh; /** container */
static daos_handle_t	oh;  /** object */
static char		field[]	= "Array";

static void
insert_example_records(void)
{
	int		rc;
	d_iov_t		dkey;
	d_sg_list_t	sgl;
	d_iov_t         sg_iovs[NR_RECXS];
	daos_iod_t	iod;
	daos_recx_t	recx;
	uint32_t	i;
	char		fname[FSIZE];
	time_t		atime, ctime, mtime;
	mode_t		mode = S_IWUSR | S_IRUSR;

	for (i = 0; i < 100; i++) {
		int j = 0;

		if (i < 10)
			sprintf(fname, "file.0%d", i);
		else 
			sprintf(fname, "file.%d", i);

		printf("insert DKEY = %s\n", fname);

		/** set dkey for record */
		d_iov_set(&dkey, &fname, strlen(fname));

		if (i % 10 == 0)
			mode |= S_IFDIR;
		else
			mode |= S_IFREG;

		atime = mtime = ctime = time(NULL);

		d_iov_set(&sg_iovs[j++], &mode, sizeof(mode_t));
		d_iov_set(&sg_iovs[j++], &atime, sizeof(time_t));
		d_iov_set(&sg_iovs[j++], &mtime, sizeof(time_t));          
		d_iov_set(&sg_iovs[j++], &ctime, sizeof(time_t));

		sgl.sg_nr	= NR_RECXS;
		sgl.sg_nr_out	= 0;
		sgl.sg_iovs	= sg_iovs;

		d_iov_set(&iod.iod_name, (void *)field, strlen(field));
		iod.iod_nr	= 1;
		iod.iod_size	= 1;
		recx.rx_idx	= 0;
		recx.rx_nr	= sizeof(time_t) * 3 + sizeof(mode_t);
		iod.iod_recxs	= &recx;
		iod.iod_type	= DAOS_IOD_ARRAY;

		rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
		ASSERT(rc == 0, "Obj update failed with %d", rc);
	}
	return;
}

/**
 *
 */
static void
build_pipeline_one(daos_pipeline_t *pipeline)
{
	int			rc;

	/** build condition for dkey containing ".0" */

	daos_size_t		str_type_s;
	char			*str_type1, *str_type2;

	str_type_s   = strlen("DAOS_FILTER_TYPE_BINARY");
	str_type1     = (char *) malloc(str_type_s);
	strncpy(str_type1, "DAOS_FILTER_TYPE_BINARY", str_type_s);
	str_type2     = (char *) malloc(str_type_s);
	strncpy(str_type2, "DAOS_FILTER_TYPE_BINARY", str_type_s);

	daos_size_t		dkey_ftype_s;
	char			*dkey_ftype;

	dkey_ftype_s = strlen("DAOS_FILTER_DKEY");
	dkey_ftype   = (char *) malloc(dkey_ftype_s);
	strncpy(dkey_ftype, "DAOS_FILTER_DKEY", dkey_ftype_s);

	daos_size_t		const_ftype_s;
	char			*const0_ftype;

	const_ftype_s = strlen("DAOS_FILTER_CONST");
	const0_ftype   = (char *) malloc(const_ftype_s);
	strncpy(const0_ftype, "DAOS_FILTER_CONST", const_ftype_s);

	daos_size_t		const_dkey_s;
	char			*const_dkey;

	const_dkey_s = strlen("*.0*");
	const_dkey   = (char *) malloc(const_dkey_s);
	strncpy(const_dkey, "*.0*", const_dkey_s);

	daos_filter_part_t	*dkey_ft;

	dkey_ft = (daos_filter_part_t *) calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&dkey_ft->part_type, dkey_ftype, dkey_ftype_s);
	d_iov_set(&dkey_ft->data_type, str_type1, str_type_s);
	dkey_ft->data_len = FSIZE;

	daos_filter_part_t      *const0_ft;

	const0_ft = (daos_filter_part_t *) calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&const0_ft->part_type, const0_ftype, const_ftype_s);
	d_iov_set(&const0_ft->data_type, str_type2, str_type_s);
	const0_ft->num_constants = 1;
	const0_ft->constant = malloc(sizeof(d_iov_t));
	d_iov_set(const0_ft->constant, const_dkey, const_dkey_s);

	daos_size_t		like_func_s;
	char			*like_func;
	daos_filter_part_t	*like_ft;

	like_func_s = strlen("DAOS_FILTER_FUNC_LIKE");
	like_func   = (char *) malloc(like_func_s);
	strncpy(like_func, "DAOS_FILTER_FUNC_LIKE", like_func_s);
	like_ft = (daos_filter_part_t *) calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&like_ft->part_type, like_func, like_func_s);
	like_ft->num_operands = 2;

	/** build condition for uint32_t integer in array bytes 0-4 & S_IFMT == S_IFDIR */

	daos_size_t		int_type_s;
	char			*int_type0;
	char			*int_type1;
	char			*int_type2;

	int_type_s = strlen("DAOS_FILTER_TYPE_UINTEGER4");
	int_type0  = (char *) malloc(int_type_s);
	int_type1  = (char *) malloc(int_type_s);
	int_type2  = (char *) malloc(int_type_s);
	strncpy(int_type0, "DAOS_FILTER_TYPE_UINTEGER4", int_type_s);
	strncpy(int_type1, "DAOS_FILTER_TYPE_UINTEGER4", int_type_s);
	strncpy(int_type2, "DAOS_FILTER_TYPE_UINTEGER4", int_type_s);

	daos_size_t		akey_ftype_s;
	char			*akey_ftype;

	akey_ftype_s = strlen("DAOS_FILTER_AKEY");
	akey_ftype   = (char *) malloc(akey_ftype_s);
	strncpy(akey_ftype, "DAOS_FILTER_AKEY", akey_ftype_s);

	daos_size_t		akey_s;
	char			*akey;
	daos_filter_part_t	*akey_ft;

	akey_s = strlen(field);
	akey   = (char *) malloc(akey_s);
	strncpy(akey, field, akey_s);
	akey_ft = (daos_filter_part_t *) calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&akey_ft->part_type, akey_ftype, akey_ftype_s);
	d_iov_set(&akey_ft->data_type, int_type0, int_type_s);
	d_iov_set(&akey_ft->akey, akey, akey_s);
	akey_ft->data_len    = 4;

	char			*const1_ftype;
	char			*const2_ftype;
	mode_t			*constant1, *constant2;
	daos_filter_part_t	*const1_ft, *const2_ft;

	const1_ftype   = (char *) malloc(const_ftype_s);
	strncpy(const1_ftype, "DAOS_FILTER_CONST", const_ftype_s);
	constant1 = (mode_t *) malloc(sizeof(mode_t));
	*constant1 = S_IFMT;
	const2_ftype   = (char *) malloc(const_ftype_s);
	strncpy(const2_ftype, "DAOS_FILTER_CONST", const_ftype_s);
	constant2 = (mode_t *) malloc(sizeof(mode_t));
	*constant2 = S_IFDIR;
	const1_ft = (daos_filter_part_t *) calloc(1, sizeof(daos_filter_part_t));
	const2_ft = (daos_filter_part_t *) calloc(1, sizeof(daos_filter_part_t));

	d_iov_set(&const1_ft->part_type, const1_ftype, const_ftype_s);
	d_iov_set(&const1_ft->data_type, int_type1, int_type_s);
	const1_ft->num_constants = 1;
	const1_ft->constant = (d_iov_t *) malloc(sizeof(d_iov_t));
	d_iov_set(const1_ft->constant, constant1, sizeof(mode_t));

	d_iov_set(&const2_ft->part_type, const2_ftype, const_ftype_s);
	d_iov_set(&const2_ft->data_type, int_type2, int_type_s);
	const2_ft->num_constants = 1;
	const2_ft->constant = (d_iov_t *) malloc(sizeof(d_iov_t));
	d_iov_set(const2_ft->constant, constant2, sizeof(mode_t));

	daos_size_t		ba_func_s;
	char			*ba_func;
	daos_filter_part_t	*ba_ft;

	ba_func_s = strlen("DAOS_FILTER_FUNC_BITAND");
	ba_func   = (char *) malloc(ba_func_s);
	strncpy(ba_func, "DAOS_FILTER_FUNC_BITAND", ba_func_s);
	ba_ft = (daos_filter_part_t *) calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&ba_ft->part_type, ba_func, ba_func_s);
	ba_ft->num_operands = 2;

	daos_size_t		eq_func_s;
	char			*eq_func;
	daos_filter_part_t	*eq_ft;

	eq_func_s = strlen("DAOS_FILTER_FUNC_EQ");
	eq_func   = (char *) malloc(eq_func_s);
	strncpy(eq_func, "DAOS_FILTER_FUNC_EQ", eq_func_s);
	eq_ft = (daos_filter_part_t *) calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&eq_ft->part_type, eq_func, eq_func_s);
	eq_ft->num_operands = 2;


	/** build final condition where result should be the dkey condition || the array condition */

	daos_size_t		or_func_s;
	char			*or_func;
	daos_filter_part_t	*or_ft;

	or_func_s = strlen("DAOS_FILTER_FUNC_OR");
	or_func   = (char *) malloc(or_func_s);
	strncpy(or_func, "DAOS_FILTER_FUNC_OR", or_func_s);
	or_ft = (daos_filter_part_t *) calloc(1, sizeof(daos_filter_part_t));
	d_iov_set(&or_ft->part_type, or_func, or_func_s);
	or_ft->num_operands = 2;

	daos_size_t		pipe_cond_s;
	char			*pipe_cond;
	daos_filter_t		*pipef;

	pipe_cond_s = strlen("DAOS_FILTER_CONDITION");
	pipe_cond   = (char *) malloc(pipe_cond_s);
	strncpy(pipe_cond, "DAOS_FILTER_CONDITION", pipe_cond_s);
	pipef = (daos_filter_t *) calloc(1, sizeof(daos_filter_t));
	daos_filter_init(pipef);
	d_iov_set(&pipef->filter_type, pipe_cond, pipe_cond_s);

	/** OR -> LIKE -> DKEY -> CONST DKEY -> EQ -> BIT AND -> AKEY -> CONST1 -> CONST2 */

	rc = daos_filter_add(pipef, or_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);

	rc = daos_filter_add(pipef, like_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
	rc = daos_filter_add(pipef, dkey_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
	rc = daos_filter_add(pipef, const0_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);

	rc = daos_filter_add(pipef, eq_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
	rc = daos_filter_add(pipef, ba_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
	rc = daos_filter_add(pipef, akey_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
	rc = daos_filter_add(pipef, const1_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
	rc = daos_filter_add(pipef, const2_ft);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);

	rc = daos_pipeline_add(pipeline, pipef);
	ASSERT(rc == 0, "Pipeline add failed with %d", rc);
}

static void
run_pipeline(daos_pipeline_t *pipeline)
{
	daos_iod_t		iod;
	daos_anchor_t		anchor;
	uint32_t		nr_iods, nr_kds;
	daos_key_desc_t		*kds;
	d_sg_list_t		*sgl_keys;
	d_iov_t			*iovs_keys;
	char			*buf_keys;
	d_sg_list_t		*sgl_recs;
	d_iov_t			*iovs_recs;
	char			*buf_recs;
	daos_recx_t		recx;
	uint32_t		i;
	int			rc;

	/* iod for akey's metadata */
	iod.iod_nr	= 1;
	iod.iod_size	= 1;
	recx.rx_idx	= 0;
	recx.rx_nr	= sizeof(mode_t);
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;
	d_iov_set(&iod.iod_name, (char *) field, strlen(field));
	nr_iods		= 1;

	/* reading chunks of 16 keys (at most) at a time */
	nr_kds = 16;

	/* to store retrieved dkeys */
	kds		= malloc(sizeof(daos_key_desc_t) * nr_kds);
	sgl_keys	= malloc(sizeof(d_sg_list_t) * nr_kds);
	iovs_keys	= malloc(sizeof(d_iov_t) * nr_kds);
	buf_keys	= malloc(FSIZE * nr_kds);

	/* to store retrieved data */
	sgl_recs	= malloc(sizeof(d_sg_list_t) * nr_kds);
	iovs_recs	= malloc(sizeof(d_iov_t) * nr_kds);
	buf_recs	= malloc(sizeof(mode_t) * nr_kds);

	for (i = 0; i < nr_kds; i++) {
		sgl_keys[i].sg_nr	= 1;
		sgl_keys[i].sg_nr_out	= 0;
		sgl_keys[i].sg_iovs	= &iovs_keys[i];
		d_iov_set(&iovs_keys[i], &buf_keys[i * FSIZE], FSIZE);

		sgl_recs[i].sg_nr	= 1;
		sgl_recs[i].sg_nr_out	= 0;
		sgl_recs[i].sg_iovs	= &iovs_recs[i];
		d_iov_set(&iovs_recs[i], &buf_recs[i * sizeof(mode_t)], sizeof(mode_t));
	}

	/** reset anchor */
	memset(&anchor, 0, sizeof(daos_anchor_t));

	/** reading 16 records at a time */
	while (!daos_anchor_is_eof(&anchor)) {
		nr_kds = 16; /** trying to read 16 in each iteration */

		rc = daos_pipeline_run(coh, oh, *pipeline, DAOS_TX_NONE, 0,
				       NULL, &nr_iods, &iod, &anchor, &nr_kds,
				       kds, sgl_keys, sgl_recs ,NULL, NULL);
		ASSERT(rc == 0, "Pipeline run failed with %d", rc);

		/** processing nr_kds records */
		for (i = 0; i < nr_kds; i++) {
			char *dkey = (char *) sgl_keys[i].sg_iovs->iov_buf;
			daos_size_t dkeylen = kds[i].kd_key_len;

			printf("\t(dkey)=%.*s\t", (int) dkeylen, dkey);
			printf("%s(akey) -->> ", field);

			char *ptr = &buf_recs[i * sizeof(mode_t)];
			mode_t cur_mode = *((mode_t *) ptr);

			if (S_ISDIR(cur_mode))
				printf("MODE type = S_IFDIR\n");
			else if (S_ISREG(cur_mode))
				printf("MODE type = S_IFREG\n");
			else
				ASSERT(0, "ERROR: invalid mode_t retrieved\n");
		}
	}

	free(kds);
	free(sgl_keys);
	free(iovs_keys);
	free(sgl_recs);
	free(iovs_recs);
	free(buf_recs);
}

int
main(int argc, char **argv)
{
	daos_obj_id_t		oid;
	int			rc;
	daos_pipeline_t		pipeline1;

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
	/** Running pipeline */

	printf("run_pipeline\n");
	fflush(stdout);

	run_pipeline(&pipeline1);
	/** Freeing used memory */

	printf("free_pipeline\n");
	fflush(stdout);

	free_pipeline(&pipeline1);

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
