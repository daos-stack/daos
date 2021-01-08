/**
 * (C) Copyright 2019-2021 Intel Corporation.
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
/**
 * This file is part of vos/tests/
 *
 * vos/tests/vts_gc.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include "vts_io.h"
#include <daos_api.h>

#define NO_FLAGS	    (0)

enum {
	STAT_CONT	= (1 << 0),
	STAT_OBJ	= (1 << 1),
	STAT_DKEY	= (1 << 2),
	STAT_AKEY	= (1 << 3),
	STAT_SINGV	= (1 << 4),
	STAT_RECX	= (1 << 5),
};

struct gc_test_args {
	struct dts_context	 gc_ctx;
	bool			 gc_array;
};

static struct gc_test_args	gc_args;

static const int cont_nr	= 4;
#define OBJ_PER_CONT	64
#define DKEY_PER_OBJ	64
static const int akey_per_dkey	= 16;
static const int recx_size	= 4096;
static const int singv_size	= 16;

static int obj_per_cont = OBJ_PER_CONT;
static int dkey_per_obj = DKEY_PER_OBJ;

static struct vos_gc_stat	gc_stat;

void
gc_add_stat(unsigned int bits)
{
	if (bits & STAT_CONT)
		gc_stat.gs_conts++;
	if (bits & STAT_OBJ)
		gc_stat.gs_objs++;
	if (bits & STAT_DKEY)
		gc_stat.gs_dkeys++;
	if (bits & STAT_AKEY)
		gc_stat.gs_akeys++;
	if (bits & STAT_SINGV)
		gc_stat.gs_singvs++;
	if (bits & STAT_RECX)
		gc_stat.gs_recxs++;
}

void
gc_print_stat(void)
{
	print_message("GC stats:\n"
		      "containers : "DF_U64"\n"
		      "objects	  : "DF_U64"\n"
		      "dkeys	  : "DF_U64"\n"
		      "akeys	  : "DF_U64"\n"
		      "singvs	  : "DF_U64"\n"
		      "recxs	  : "DF_U64"\n",
		      gc_stat.gs_conts, gc_stat.gs_objs, gc_stat.gs_dkeys,
		      gc_stat.gs_akeys, gc_stat.gs_singvs, gc_stat.gs_recxs);
}

int
gc_obj_update(struct gc_test_args *args, daos_handle_t coh, daos_unit_oid_t oid,
	      daos_epoch_t epoch, struct dts_io_credit *cred)
{
	daos_iod_t	*iod = &cred->tc_iod;
	d_sg_list_t	*sgl = &cred->tc_sgl;
	int		 rc;

	iod->iod_nr = 1;
	dts_key_gen(cred->tc_abuf, DTS_KEY_LEN, NULL);
	sgl->sg_iovs = &cred->tc_val;
	sgl->sg_nr = 1;

	if (!args->gc_array) {
		d_iov_set(&cred->tc_val, cred->tc_vbuf, singv_size);
		iod->iod_type = DAOS_IOD_SINGLE;
		iod->iod_size = singv_size;

		gc_add_stat(STAT_SINGV);
		rc = vos_obj_update(coh, oid, epoch, 0, 0, &cred->tc_dkey, 1,
				    &cred->tc_iod, NULL, sgl);
		if (rc != 0) {
			print_error("Failed to update\n");
			return rc;
		}
	} else {
		daos_handle_t	ioh;

		d_iov_set(&cred->tc_val, cred->tc_vbuf, recx_size);
		iod->iod_type = DAOS_IOD_ARRAY;
		iod->iod_size = 1;
		iod->iod_recxs = &cred->tc_recx;
		cred->tc_recx.rx_nr = recx_size;

		gc_add_stat(STAT_RECX);
		rc = vos_update_begin(coh, oid, epoch, 0, &cred->tc_dkey, 1,
				      &cred->tc_iod, NULL, false, 0, &ioh,
				      NULL);
		if (rc != 0) {
			print_error("Failed to prepare ZC update\n");
			return rc;
		}

		rc = bio_iod_prep(vos_ioh2desc(ioh));
		if (rc) {
			print_error("Failed to prepare bio desc\n");
			return rc;
		}

		/* write garbage, we don't care */
		rc = bio_iod_post(vos_ioh2desc(ioh));
		if (rc) {
			print_error("Failed to post bio request\n");
			return rc;
		}

		rc = vos_update_end(ioh, 0, &cred->tc_dkey, rc, NULL);
		if (rc != 0) {
			print_error("Failed to submit ZC update\n");
			return rc;
		}
	}
	return 0;
}

static int
gc_obj_prepare(struct gc_test_args *args, daos_handle_t coh,
	       daos_unit_oid_t *oids)
{
	struct dts_io_credit	*cred;
	daos_iod_t		*iod;
	int		         i;
	int			 j;
	int			 k;
	int			 rc;

	cred = dts_credit_take(&args->gc_ctx);
	D_ASSERT(cred);
	iod = &cred->tc_iod;

	d_iov_set(&cred->tc_dkey, cred->tc_dbuf, DTS_KEY_LEN);
	d_iov_set(&iod->iod_name, cred->tc_abuf, DTS_KEY_LEN);

	for (i = 0; i < obj_per_cont; i++) {
		daos_unit_oid_t	oid;

		gc_add_stat(STAT_OBJ);
		oid = dts_unit_oid_gen(0, 0, 0);
		if (oids)
			oids[i] = oid;

		for (j = 0; j < dkey_per_obj; j++) {
			gc_add_stat(STAT_DKEY);
			dts_key_gen(cred->tc_dbuf, DTS_KEY_LEN, NULL);

			for (k = 0; k < akey_per_dkey; k++) {
				gc_add_stat(STAT_AKEY);
				dts_key_gen(cred->tc_abuf, DTS_KEY_LEN, NULL);

				rc = gc_obj_update(args, coh, oid, 1, cred);
				if (rc)
					return rc;
			}
		}
	}
	return 0;
}

static int
gc_wait_check(struct gc_test_args *args, bool cont_delete)
{
	struct vos_gc_stat *stat;
	vos_pool_info_t	    pinfo;
	int		    rc;

	print_message("wait for VOS GC\n");
	stat = &pinfo.pif_gc_stat;
	while (1) {
		int	creds = 64;

		rc = vos_gc_pool(args->gc_ctx.tsc_poh, &creds);
		if (rc) {
			print_error("gc pool failed: %s\n", d_errstr(rc));
			return rc;
		}
		if (creds != 0)
			break;
	}

	print_message("query GC result\n");
	rc = vos_pool_query(args->gc_ctx.tsc_poh, &pinfo);
	if (rc) {
		print_error("Failed to query pool: %s\n", d_errstr(rc));
		return rc;
	}
	print_message("GC stats:\n"
		      "containers : "DF_U64"/"DF_U64"\n"
		      "objects	  : "DF_U64"/"DF_U64"\n"
		      "dkeys	  : "DF_U64"/"DF_U64"\n"
		      "akeys	  : "DF_U64"/"DF_U64"\n"
		      "singvs	  : "DF_U64"/"DF_U64"\n"
		      "recxs	  : "DF_U64"/"DF_U64"\n",
		      stat->gs_conts,  gc_stat.gs_conts,
		      stat->gs_objs,   gc_stat.gs_objs,
		      stat->gs_dkeys,  gc_stat.gs_dkeys,
		      stat->gs_akeys,  gc_stat.gs_akeys,
		      stat->gs_singvs, gc_stat.gs_singvs,
		      stat->gs_recxs,  gc_stat.gs_recxs);

	if (!cont_delete)
		gc_stat.gs_conts = 0;

	if (memcmp(&gc_stat, stat, sizeof(gc_stat)) != 0) {
		print_error("unmatched GC results\n");
		return -DER_IO;
	}
	print_message("Test successfully completed\n");
	return 0;
}

static int
gc_obj_run(struct gc_test_args *args)
{
	daos_unit_oid_t	*oids;
	int		 i;
	int		 rc;

	D_ALLOC_ARRAY(oids, obj_per_cont);
	if (!oids) {
		print_error("failed to allocate oids\n");
		return -DER_NOMEM;
	}

	rc = gc_obj_prepare(args, args->gc_ctx.tsc_coh, oids);
	if (rc)
		goto out;

	gc_print_stat();

	for (i = 0; i < obj_per_cont; i++) {
		rc = vos_obj_delete(args->gc_ctx.tsc_coh, oids[i]);
		if (rc) {
			print_error("failed to delete objects: %s\n",
				    d_errstr(rc));
			goto out;
		}
	}

	rc = gc_wait_check(args, false);
out:
	D_FREE(oids);
	return rc;
}

static void
gc_obj_test(void **state)
{
	struct gc_test_args *args = *state;
	int		     rc;

	rc = gc_obj_run(args);
	assert_int_equal(rc, 0);
}

static void
gc_obj_bio_test(void **state)
{
	struct gc_test_args *args = *state;
	int		     rc;

	args->gc_array = true;
	rc = gc_obj_run(args);
	assert_int_equal(rc, 0);
}

static int
gc_cont_run(struct gc_test_args *args)
{
	uuid_t		*cont_ids;
	daos_handle_t	 poh;
	int		 i;
	int		 rc;

	D_ALLOC_ARRAY(cont_ids, cont_nr);
	if (!cont_ids) {
		print_error("failed to allocate container ids\n");
		return -DER_NOMEM;
	}

	poh = args->gc_ctx.tsc_poh;
	for (i = 0; i < cont_nr; i++) {
		daos_handle_t coh;

		uuid_generate(cont_ids[i]);

		rc = vos_cont_create(poh, cont_ids[i]);
		if (rc) {
			print_error("failed to create container: %s\n",
				    d_errstr(rc));
			return rc;
		}

		gc_add_stat(STAT_CONT);
		rc = vos_cont_open(poh, cont_ids[i], &coh);
		if (rc) {
			print_error("failed to open container: %s\n",
				    d_errstr(rc));
			return rc;
		}

		rc = gc_obj_prepare(args, coh, NULL);
		if (rc)
			return rc;

		rc = vos_cont_close(coh);
		if (rc) {
			print_error("failed to close container: %s\n",
				    d_errstr(rc));
			return rc;
		}

		rc = vos_cont_destroy(poh, cont_ids[i]);
		if (rc) {
			print_error("failed to destroy container: %s\n",
				    d_errstr(rc));
			return rc;
		}
	}
	rc = gc_wait_check(args, true);
	return rc;
}

static void
gc_cont_test(void **state)
{
	struct gc_test_args *args = *state;
	int		     rc;

	rc = gc_cont_run(args);
	assert_int_equal(rc, 0);
}

static int
gc_setup(void **state)
{
	struct dts_context	*tc = &gc_args.gc_ctx;

	memset(&gc_stat, 0, sizeof(gc_stat));
	memset(&gc_args, 0, sizeof(gc_args));

	tc->tsc_scm_size	= (2ULL << 30);
	tc->tsc_nvme_size	= (4ULL << 30);
	tc->tsc_cred_vsize	= max(recx_size, singv_size);
	tc->tsc_cred_nr		= -1; /* sync mode */
	tc->tsc_mpi_rank	= 0;
	tc->tsc_mpi_size	= 1;
	uuid_generate(tc->tsc_pool_uuid);
	uuid_generate(tc->tsc_cont_uuid);
	vts_pool_fallocate(&tc->tsc_pmem_file);

	dts_ctx_init(&gc_args.gc_ctx);

	gc_args.gc_array = false;
	*state = &gc_args;
	return 0;
}

static int
gc_teardown(void **state)
{
	struct gc_test_args *args = *state;

	assert_ptr_equal(args, &gc_args);

	dts_ctx_fini(&args->gc_ctx);
	free(args->gc_ctx.tsc_pmem_file);

	memset(&gc_stat, 0, sizeof(gc_stat));
	memset(args, 0, sizeof(*args));
	return 0;
}

static int
gc_prepare(void **state)
{
	struct gc_test_args *args = *state;

	vos_pool_ctl(args->gc_ctx.tsc_poh, VOS_PO_CTL_RESET_GC);
	memset(&gc_stat, 0, sizeof(gc_stat));
	return 0;
}

static const struct CMUnitTest gc_tests[] = {
	{ "GC01: object garbage collecting",
	  gc_obj_test, gc_prepare, NULL},
	{ "GC02: object garbage collecting (array)",
	  gc_obj_bio_test, gc_prepare, NULL},
	{ "GC03: container garbage collecting",
	  gc_cont_test, gc_prepare, NULL},
};

int
run_gc_tests(const char *cfg)
{
	char	test_name[DTS_CFG_MAX];

	if (DAOS_ON_VALGRIND) {
		obj_per_cont = 2;
		dkey_per_obj = 3;
	}

	dts_create_config(test_name, "Garbage collector %s", cfg);
	return cmocka_run_group_tests_name(test_name,
					   gc_tests, gc_setup, gc_teardown);
}
