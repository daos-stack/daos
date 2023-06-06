/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(tests)

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>
#include <getopt.h>

#include <daos/tests_lib.h>
#include <daos/common.h>
#include <daos/btree_class.h>
#include <daos_srv/vea.h>
#include "../vea_internal.h"

char		pool_file[PATH_MAX];

#define IO_STREAM_CNT	(3)

struct vea_ut_args {
	struct umem_instance		 vua_umm;
	struct umem_tx_stage_data	 vua_txd;
	struct vea_space_df		*vua_md;
	struct vea_hint_df		*vua_hint[IO_STREAM_CNT];
	struct vea_space_info		*vua_vsi;
	struct vea_hint_context		*vua_hint_ctxt[IO_STREAM_CNT];
	d_list_t			 vua_resrvd_list[IO_STREAM_CNT];
	d_list_t			 vua_alloc_list;
};

static struct vea_ut_args	ut_args;

static void
print_usage(void)
{
	fprintf(stdout, "vea_ut [-f <pool_file_name>]\n");
}

#define	UT_TOTAL_BLKS	(((VEA_LARGE_EXT_MB * 2) + 1) << 20) /* 129MB */

static void
ut_format(void **state)
{
	struct vea_ut_args *args = *state;
	uint32_t blk_sz = 0; /* use the default size */
	uint32_t hdr_blks = 1;
	uint64_t capacity = UT_TOTAL_BLKS;
	int rc;

	/* format */
	print_message("format\n");
	rc = vea_format(&args->vua_umm, &args->vua_txd, args->vua_md, blk_sz,
			hdr_blks, capacity, NULL, NULL, false);
	assert_rc_equal(rc, 0);

	/* reformat without setting 'force' */
	print_message("reformat without setting 'force'\n");
	rc = vea_format(&args->vua_umm, &args->vua_txd, args->vua_md, blk_sz,
			hdr_blks, capacity, NULL, NULL, false);
	assert_rc_equal(rc, -DER_EXIST);

	/* reformat with 'force' */
	print_message("reformat with 'force'\n");
	rc = vea_format(&args->vua_umm, &args->vua_txd, args->vua_md, blk_sz,
			hdr_blks, capacity, NULL, NULL, true);
	assert_rc_equal(rc, 0);
}

static void
ut_load(void **state)
{
	struct vea_ut_args *args = *state;
	struct vea_unmap_context unmap_ctxt = { 0 };
	int rc;

	rc = vea_load(&args->vua_umm, &args->vua_txd, args->vua_md, &unmap_ctxt,
		      NULL, &args->vua_vsi);
	assert_rc_equal(rc, 0);
}

static void
ut_query(void **state)
{
	struct vea_ut_args	*args = *state;
	struct vea_attr		 attr;
	struct vea_stat		 stat;
	uint32_t		 blk_sz, hdr_blks, tot_blks;
	int			 rc;

	rc = vea_query(args->vua_vsi, &attr, &stat);
	assert_rc_equal(rc, 0);

	/* the values from ut_format() */
	blk_sz = (1 << 12);
	hdr_blks = 1;
	tot_blks = UT_TOTAL_BLKS / blk_sz - hdr_blks;

	/* verify the attributes */
	assert_int_equal(attr.va_blk_sz, blk_sz);
	assert_int_equal(attr.va_hdr_blks, hdr_blks);
	assert_int_equal(attr.va_large_thresh,
			 (VEA_LARGE_EXT_MB << 20) / blk_sz);
	assert_int_equal(attr.va_tot_blks, tot_blks);

	/* verify the statistics */
	assert_int_equal(stat.vs_free_persistent, tot_blks);
	assert_int_equal(stat.vs_free_transient, tot_blks);
	assert_int_equal(stat.vs_frags_large, 1);
	assert_int_equal(stat.vs_frags_small, 0);
	assert_int_equal(stat.vs_frags_aging, 0);
	assert_int_equal(stat.vs_resrv_hint, 0);
	assert_int_equal(stat.vs_resrv_large, 0);
	assert_int_equal(stat.vs_resrv_small, 0);
}

static void
ut_hint_load(void **state)
{
	struct vea_ut_args *args = *state;
	int i, rc;

	for (i = 0; i < IO_STREAM_CNT; i++) {
		print_message("load hint of I/O stream:%d\n", i);
		rc = vea_hint_load(args->vua_hint[i], &args->vua_hint_ctxt[i]);
		assert_rc_equal(rc, 0);
	}
}

static void
ut_reserve(void **state)
{
	struct vea_ut_args	*args = *state;
	uint64_t		 off_a, off_b;
	uint32_t		 blk_cnt;
	struct vea_resrvd_ext	*ext;
	struct vea_hint_context	*h_ctxt;
	d_list_t		*r_list;
	struct vea_stat		 stat;
	int			 rc, ext_cnt;

	/*
	 * Reserve two extents from I/O stream 0 and I/O stream 1 in
	 * interleaved order, the reservation from I/O stream 0 will be
	 * canceled later, and the reservation from I/O stream 1 will
	 * be published.
	 */
	off_a = off_b = VEA_HINT_OFF_INVAL;
	for (ext_cnt = 0; ext_cnt < 2; ext_cnt++) {
		print_message("reserve extent %d from I/O stream 0\n", ext_cnt);

		r_list = &args->vua_resrvd_list[0];
		h_ctxt = args->vua_hint_ctxt[0];

		blk_cnt = (ext_cnt == 0) ? 10 : 1;
		rc = vea_reserve(args->vua_vsi, blk_cnt, h_ctxt, r_list);
		assert_rc_equal(rc, 0);

		/* correctness check */
		ext = d_list_entry(r_list->prev, struct vea_resrvd_ext,
				   vre_link);
		assert_int_equal(ext->vre_hint_off, off_a);
		assert_int_equal(ext->vre_blk_cnt, blk_cnt);
		if (ext_cnt == 0)
			off_a = ext->vre_blk_off;
		else
			assert_int_equal(ext->vre_blk_off, off_a);

		rc = vea_verify_alloc(args->vua_vsi, true, off_a, blk_cnt);
		assert_rc_equal(rc, 0);
		rc = vea_verify_alloc(args->vua_vsi, false, off_a, blk_cnt);
		assert_rc_equal(rc, 1);

		/* update hint offset */
		off_a += blk_cnt;

		print_message("reserve extent %d from I/O stream 1\n", ext_cnt);

		r_list = &args->vua_resrvd_list[1];
		h_ctxt = args->vua_hint_ctxt[1];

		blk_cnt = (ext_cnt == 0) ? 256 : 4;
		rc = vea_reserve(args->vua_vsi, blk_cnt, h_ctxt, r_list);
		assert_rc_equal(rc, 0);

		/* correctness check */
		ext = d_list_entry(r_list->prev, struct vea_resrvd_ext,
				   vre_link);
		assert_int_equal(ext->vre_hint_off, off_b);
		assert_int_equal(ext->vre_blk_cnt, blk_cnt);
		if (ext_cnt == 0)
			off_b = ext->vre_blk_off;
		else
			assert_int_equal(ext->vre_blk_off, off_b);

		rc = vea_verify_alloc(args->vua_vsi, true, off_b, blk_cnt);
		assert_rc_equal(rc, 0);
		rc = vea_verify_alloc(args->vua_vsi, false, off_b, blk_cnt);
		assert_rc_equal(rc, 1);

		/* update hint offset */
		off_b += blk_cnt;
	}

	/* Reserve from I/O stream 2, it will reserve from small free extent */
	print_message("reserve extent from I/O stream 2\n");

	r_list = &args->vua_resrvd_list[2];
	h_ctxt = args->vua_hint_ctxt[2];

	blk_cnt = 1024;
	rc = vea_reserve(args->vua_vsi, blk_cnt, h_ctxt, r_list);
	assert_rc_equal(rc, 0);

	/* correctness check */
	ext = d_list_entry(r_list->prev, struct vea_resrvd_ext, vre_link);
	assert_int_equal(ext->vre_hint_off, VEA_HINT_OFF_INVAL);
	assert_int_equal(ext->vre_blk_cnt, blk_cnt);
	/* start from the end of the stream 1 */
	assert_int_equal(ext->vre_blk_off, off_b);

	/* Verify transient is allocated */
	rc = vea_verify_alloc(args->vua_vsi, true, off_b, blk_cnt);
	assert_rc_equal(rc, 0);
	/* Verify persistent is not allocated */
	rc = vea_verify_alloc(args->vua_vsi, false, off_b, blk_cnt);
	assert_rc_equal(rc, 1);

	/* Verify statistics */
	rc = vea_query(args->vua_vsi, NULL, &stat);
	assert_rc_equal(rc, 0);

	assert_int_equal(stat.vs_frags_large, 1);
	assert_int_equal(stat.vs_frags_small, 1);
	/* 2 hint from the second reserve for io stream 0 & 1 */
	assert_int_equal(stat.vs_resrv_hint, 2);
	/* 2 large from the first reserve for io stream 0 & 1 */
	assert_int_equal(stat.vs_resrv_large, 2);
	/* 1 small from the reserve for io stream 2 */
	assert_int_equal(stat.vs_resrv_small, 1);
}

static void
ut_cancel(void **state)
{
	struct vea_ut_args *args = *state;
	struct vea_hint_context *h_ctxt;
	struct vea_resrvd_ext *ext;
	d_list_t *r_list;
	uint64_t blk_off = VEA_HINT_OFF_INVAL;
	uint32_t blk_cnt = 0;
	int rc;

	r_list = &args->vua_resrvd_list[0];
	h_ctxt = args->vua_hint_ctxt[0];

	d_list_for_each_entry(ext, r_list, vre_link) {
		blk_off = (blk_off == VEA_HINT_OFF_INVAL) ?
			  ext->vre_blk_off : blk_off;
		blk_cnt += ext->vre_blk_cnt;
	}

	print_message("cancel reservation from I/O stream 0\n");
	rc = vea_cancel(args->vua_vsi, h_ctxt, r_list);
	assert_int_equal(rc, 0);
	rc = vea_verify_alloc(args->vua_vsi, true, blk_off, blk_cnt);
	assert_rc_equal(rc, 1);
	assert_int_equal(h_ctxt->vhc_off, VEA_HINT_OFF_INVAL);
}

static void
ut_tx_publish(void **state)
{
	struct vea_ut_args *args = *state;
	struct vea_hint_context *h_ctxt;
	struct vea_resrvd_ext *ext, *copy;
	d_list_t *r_list;
	uint64_t blk_off;
	uint32_t blk_cnt;
	int rc, i;

	rc = umem_tx_begin(&args->vua_umm, &args->vua_txd);
	assert_int_equal(rc, 0);

	for (i = 1; i < IO_STREAM_CNT; i++) {
		r_list = &args->vua_resrvd_list[i];
		h_ctxt = args->vua_hint_ctxt[i];

		/*
		 * the reserved list will be freed on publish, save the
		 * allocated extents for later verification.
		 */
		d_list_for_each_entry(ext, r_list, vre_link) {
			D_ALLOC_PTR(copy);
			assert_ptr_not_equal(copy, NULL);

			D_INIT_LIST_HEAD(&copy->vre_link);
			copy->vre_blk_off = ext->vre_blk_off;
			copy->vre_blk_cnt = ext->vre_blk_cnt;
			d_list_add(&copy->vre_link, &args->vua_alloc_list);
		}

		print_message("publish reservation from I/O stream %d\n", i);
		rc = vea_tx_publish(args->vua_vsi, h_ctxt, r_list);
		assert_int_equal(rc, 0);
	}

	rc = umem_tx_commit(&args->vua_umm);
	assert_int_equal(rc, 0);

	r_list = &args->vua_alloc_list;
	d_list_for_each_entry(copy, r_list, vre_link) {
		blk_off = copy->vre_blk_off;
		blk_cnt = copy->vre_blk_cnt;

		rc = vea_verify_alloc(args->vua_vsi, true, blk_off, blk_cnt);
		assert_rc_equal(rc, 0);

		rc = vea_verify_alloc(args->vua_vsi, false, blk_off, blk_cnt);
		assert_rc_equal(rc, 0);
	}
}

static void
ut_free(void **state)
{
	struct vea_ut_args *args = *state;
	struct vea_resrvd_ext *ext;
	d_list_t *r_list;
	uint64_t blk_off;
	uint32_t blk_cnt, nr_flushed;
	int rc;

	r_list = &args->vua_alloc_list;
	d_list_for_each_entry(ext, r_list, vre_link) {
		blk_off = ext->vre_blk_off;
		blk_cnt = ext->vre_blk_cnt;

		rc = vea_free(args->vua_vsi, blk_off, blk_cnt);
		assert_rc_equal(rc, 0);

		/* not immediately visual for allocation */
		rc = vea_verify_alloc(args->vua_vsi, true, blk_off, blk_cnt);
		assert_rc_equal(rc, 0);

		rc = vea_verify_alloc(args->vua_vsi, false, blk_off, blk_cnt);
		assert_rc_equal(rc, 1);
	}

	print_message("transient free extents:\n");
	vea_dump(args->vua_vsi, true);
	print_message("persistent free extents:\n");
	vea_dump(args->vua_vsi, false);

	/* call vea_flush to trigger free extents migration */
	rc = vea_flush(args->vua_vsi, true, UINT32_MAX, &nr_flushed);
	assert_rc_equal(rc, 0);
	assert_true(nr_flushed > 0);

	r_list = &args->vua_alloc_list;
	d_list_for_each_entry(ext, r_list, vre_link) {
		blk_off = ext->vre_blk_off;
		blk_cnt = ext->vre_blk_cnt;

		rc = vea_verify_alloc(args->vua_vsi, true, blk_off, blk_cnt);
		assert_rc_equal(rc, 1);
	}

	print_message("transient free extents after migration:\n");
	vea_dump(args->vua_vsi, true);
	print_message("persistent free extents after migration:\n");
	vea_dump(args->vua_vsi, false);
}

static void
ut_hint_unload(void **state)
{
	struct vea_ut_args *args = *state;
	int i;

	for (i = 0; i < IO_STREAM_CNT; i++) {
		print_message("unload hint of I/O stream:%d\n", i);
		vea_hint_unload(args->vua_hint_ctxt[i]);
		args->vua_hint_ctxt[i] = NULL;
	}
}

static void
ut_unload(void **state)
{
	struct vea_ut_args *args = *state;

	vea_unload(args->vua_vsi);
	args->vua_vsi = NULL;
}

static int
ut_setup(struct vea_ut_args *test_args)
{
	daos_size_t pool_size = (50 << 20); /* 50MB */
	struct umem_attr uma = {0};
	void *root_addr;
	int rc, i;

	memset(test_args, 0, sizeof(struct vea_ut_args));
	D_INIT_LIST_HEAD(&test_args->vua_alloc_list);

	unlink(pool_file);

	uma.uma_id = UMEM_CLASS_PMEM;
	uma.uma_pool = umempobj_create(pool_file, "vea_ut", 0,
					     pool_size, 0666, NULL);
	if (uma.uma_pool == NULL) {
		fprintf(stderr, "create pmemobj pool error\n");
		return -1;
	}

	root_addr = umempobj_get_rootptr(uma.uma_pool,
			    sizeof(struct vea_space_df) +
			    sizeof(struct vea_hint_df) * IO_STREAM_CNT);
	if (root_addr == NULL) {
		fprintf(stderr, "get root error\n");
		rc = -1;
		goto error;
	}

	rc = umem_class_init(&uma, &test_args->vua_umm);
	if (rc) {
		fprintf(stderr, "initialize umm error %d\n", rc);
		goto error;
	}


	test_args->vua_md = root_addr;
	root_addr += sizeof(struct vea_space_df);

	for (i = 0; i < IO_STREAM_CNT; i++) {
		test_args->vua_hint[i] = root_addr;

		test_args->vua_hint[i]->vhd_off = 0;
		test_args->vua_hint[i]->vhd_seq = 0;
		D_INIT_LIST_HEAD(&test_args->vua_resrvd_list[i]);

		root_addr += sizeof(struct vea_hint_df);
	}

	umem_init_txd(&test_args->vua_txd);
	return 0;
error:
	umempobj_close(uma.uma_pool);
	test_args->vua_umm.umm_pool = NULL;

	return rc;
}

static int
vea_ut_setup(void **state)
{
	int rc;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0)
		return rc;

	rc = dbtree_class_register(DBTREE_CLASS_IV,
				   BTR_FEAT_UINT_KEY | BTR_FEAT_DIRECT_KEY,
				   &dbtree_iv_ops);
	if (rc != 0 && rc != -DER_EXIST) {
		fprintf(stderr, "register DBTREE_CLASS_IV error %d\n", rc);
		return rc;
	}

	rc = dbtree_class_register(DBTREE_CLASS_IFV, BTR_FEAT_UINT_KEY | BTR_FEAT_DIRECT_KEY,
				   &dbtree_ifv_ops);
	if (rc != 0 && rc != -DER_EXIST) {
		fprintf(stderr, "register DBTREE_CLASS_IFV error %d\n", rc);
		return rc;
	}

	rc = ut_setup(&ut_args);
	if (rc == 0)
		*state = &ut_args;
	return rc;

}

static void
ut_teardown(struct vea_ut_args *test_args)
{
	struct vea_resrvd_ext *ext, *tmp;
	d_list_t *r_list;
	int cur_stream;

	r_list = &test_args->vua_alloc_list;
	d_list_for_each_entry_safe(ext, tmp, r_list, vre_link) {
		d_list_del_init(&ext->vre_link);
		D_FREE(ext);
	}

	for (cur_stream = 0; cur_stream < IO_STREAM_CNT; cur_stream++) {
		r_list = &test_args->vua_resrvd_list[cur_stream];
		d_list_for_each_entry_safe(ext, tmp, r_list, vre_link) {
			d_list_del_init(&ext->vre_link);
			D_FREE(ext);
		}
	}

	if (test_args->vua_umm.umm_pool != NULL) {
		umempobj_close(test_args->vua_umm.umm_pool);
		test_args->vua_umm.umm_pool = NULL;
	}
	umem_fini_txd(&test_args->vua_txd);
}

static int
vea_ut_teardown(void **state)
{
	struct vea_ut_args *args = *state;

	if (args == NULL) {
		print_message("state not set, likely due to group-setup"
			      " issue\n");
		return 0;
	}
	ut_teardown(args);
	daos_debug_fini();
	return 0;
}

static void
ut_reserve_special(void **state)
{
	/* Use a temporary device instead of the main one the other tests use */
	struct vea_ut_args args;
	uint32_t blk_cnt = 0;
	d_list_t *r_list;
	uint32_t hdr_blks = 1;
	uint64_t capacity = 2UL << 30; /* 2GB, 0.5M 4k blocks in total */
	struct vea_unmap_context unmap_ctxt = { 0 };
	uint32_t blk_sz = 0; /* use the default size */
	int rc;

	ut_setup(&args);

	rc = vea_format(&args.vua_umm, &args.vua_txd, args.vua_md, blk_sz,
			hdr_blks, capacity, NULL, NULL, false);
	assert_rc_equal(rc, 0);

	rc = vea_load(&args.vua_umm, &args.vua_txd, args.vua_md, &unmap_ctxt,
		      NULL, &args.vua_vsi);
	assert_rc_equal(rc, 0);

	print_message("Try to reserve extent larger than available space\n");

	r_list = &args.vua_resrvd_list[0];

	/* reserve too big should fail */
	blk_cnt = (1 << 20); /* 1M blocks */
	rc = vea_reserve(args.vua_vsi, blk_cnt, NULL, r_list);
	/* expect -DER_NOSPACE */
	assert_rc_equal(rc, -DER_NOSPACE);
	print_message("correctly failed to reserve extent\n");

	/* allocation should success */
	blk_cnt = (500 * 1024); /* a bit less than 0.5M blocks */
	rc = vea_reserve(args.vua_vsi, blk_cnt, NULL, r_list);
	assert_rc_equal(rc, 0);

	rc = umem_tx_begin(&args.vua_umm, &args.vua_txd);
	assert_int_equal(rc, 0);

	rc = vea_tx_publish(args.vua_vsi, NULL, r_list);
	assert_int_equal(rc, 0);

	rc = umem_tx_commit(&args.vua_umm);
	assert_int_equal(rc, 0);

	/* free the allocated space */
	rc = vea_free(args.vua_vsi, hdr_blks, blk_cnt);
	assert_rc_equal(rc, 0);

	/*
	 * immediate reserve after free, the free extents should be made
	 * visible for allocation immediately, reserve should succeed.
	 */
	rc = vea_reserve(args.vua_vsi, blk_cnt, NULL, r_list);
	assert_rc_equal(rc, 0);

	vea_unload(args.vua_vsi);
	ut_teardown(&args);
}

static void
ut_inval_params_format(void **state)
{
	struct vea_ut_args args;
	uint32_t block_size = 0; /* use the default size */
	uint32_t header_blocks = 1;
	uint64_t capacity = ((VEA_LARGE_EXT_MB * 2) << 20); /* 128 MB */
	int rc;

	ut_setup(&args);
	print_message("Testing invalid parameters to vea_format\n");

	/* vea_format: Test null umem */
	expect_assert_failure(vea_format(NULL, &args.vua_txd, args.vua_md,
					 block_size, header_blocks, capacity,
					 NULL, NULL, false));

	/* vea_format: Test null md */
	expect_assert_failure(vea_format(&args.vua_umm, &args.vua_txd, NULL,
					 block_size, header_blocks, capacity,
					 NULL, NULL, false));

	/* vea_format: Test large block_size */
	block_size = UINT32_MAX;
	rc = vea_format(&args.vua_umm, &args.vua_txd, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_rc_equal(rc, -DER_INVAL);

	/* vea_format: Test non-4k aligned block_size */
	block_size = 4095;
	rc = vea_format(&args.vua_umm, &args.vua_txd, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_rc_equal(rc, -DER_INVAL);

	/* vea_format: Test no header blocks */
	block_size = 0; /* Set to 0 to use the default 4K block size */
	header_blocks = 0;
	rc = vea_format(&args.vua_umm, &args.vua_txd, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_rc_equal(rc, -DER_INVAL);

	/* vea_format: Test large value for header_blocks */
	header_blocks = UINT32_MAX;
	rc = vea_format(&args.vua_umm, &args.vua_txd, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_rc_equal(rc, -DER_NOSPACE);

	/* vea_format: Test small value for capacity */
	header_blocks = 1;
	capacity = 0;
	rc = vea_format(&args.vua_umm, &args.vua_txd, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_rc_equal(rc, -DER_NOSPACE);

	/* vea_format: Make capacity and block_size equal */
	capacity = 4096;
	rc = vea_format(&args.vua_umm, &args.vua_txd, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_rc_equal(rc, -DER_NOSPACE);

	/* vea_format: Test upper bound of largest extent < UINT32_MAX */
	capacity = (16ULL << 40) + 4096; /* 16TB + 1 4k header block */
	rc = vea_format(&args.vua_umm, &args.vua_txd, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_rc_equal(rc, -DER_INVAL);

	ut_teardown(&args);
}

static void
ut_inval_params_load(void **state)
{
	struct vea_ut_args args;
	uint32_t block_size = 0; /* use the default size */
	uint32_t header_blocks = 1;
	uint64_t capacity = ((VEA_LARGE_EXT_MB * 2) << 20); /* 128 MB */
	struct vea_unmap_context unmap_ctxt = { 0 };
	int rc;

	ut_setup(&args);
	print_message("Testing invalid parameters to vea_load\n");

	/* vea_load: Test unformatted blob */
	rc = vea_load(&args.vua_umm, &args.vua_txd, args.vua_md, &unmap_ctxt,
		      NULL, &args.vua_vsi);
	assert_rc_equal(rc, -DER_UNINIT);

	/* vea_load: Test umem is NULL */
	/* First correctly format the blob */
	capacity = ((VEA_LARGE_EXT_MB * 2) << 20); /* 128 MB */
	rc = vea_format(&args.vua_umm, &args.vua_txd, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_rc_equal(rc, 0);
	expect_assert_failure(vea_load(NULL, &args.vua_txd, args.vua_md,
				       &unmap_ctxt, NULL, &args.vua_vsi));

	/* vea_load: Test md is NULL */
	expect_assert_failure(vea_load(&args.vua_umm, &args.vua_txd, NULL,
				       &unmap_ctxt, NULL, &args.vua_vsi));

	/* vea_load: Test unmap_ctxt is NULL */
	expect_assert_failure(vea_load(&args.vua_umm, &args.vua_txd,
				       args.vua_md, NULL, NULL, &args.vua_vsi));

	/* vea_load: Test vsip is NULL */
	expect_assert_failure(vea_load(&args.vua_umm, &args.vua_txd,
				       args.vua_md, &unmap_ctxt, NULL, NULL));

	/* vea_unload: Test is vsi NULL */
	expect_assert_failure(vea_unload(args.vua_vsi));

	ut_teardown(&args);
}

static void
ut_inval_params_reserve(void **state)
{
	struct vea_ut_args args;
	uint32_t block_count = 1;
	uint32_t block_size = 0; /* use the default size */
	uint32_t header_blocks = 1;
	uint64_t capacity = ((VEA_LARGE_EXT_MB * 2) << 20); /* 128 MB */
	struct vea_unmap_context unmap_ctxt = { 0 };
	d_list_t *r_list;
	int rc;

	ut_setup(&args);
	print_message("Testing invalid parameters to vea_reserve\n");
	rc = vea_format(&args.vua_umm, &args.vua_txd, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_rc_equal(rc, 0);

	rc = vea_load(&args.vua_umm, &args.vua_txd, args.vua_md, &unmap_ctxt,
		      NULL, &args.vua_vsi);
	assert_rc_equal(rc, 0);

	r_list = &args.vua_resrvd_list[0];

	/* vea_reserve: Test vsi is NULL */
	expect_assert_failure(vea_reserve(NULL, block_count, NULL, r_list));

	/* vea_reserve: Test resrvd_list is NULL */
	expect_assert_failure(vea_reserve(args.vua_vsi, block_count, NULL,
					  NULL));
	vea_unload(args.vua_vsi);
	ut_teardown(&args);
}

static void
ut_inval_params_cancel(void **state)
{
	struct vea_ut_args args;
	uint32_t block_count = 1;
	uint32_t block_size = 0; /* use the default size */
	uint32_t header_blocks = 1;
	uint64_t capacity = ((VEA_LARGE_EXT_MB * 2) << 20); /* 128 MB */
	struct vea_unmap_context unmap_ctxt = { 0 };
	d_list_t *r_list;
	int rc;

	print_message("Testing invalid parameters to vea_cancel\n");
	ut_setup(&args);
	rc = vea_format(&args.vua_umm, &args.vua_txd, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_rc_equal(rc, 0);

	rc = vea_load(&args.vua_umm, &args.vua_txd, args.vua_md, &unmap_ctxt,
		      NULL, &args.vua_vsi);
	assert_rc_equal(rc, 0);
	r_list = &args.vua_resrvd_list[0];

	rc = vea_reserve(args.vua_vsi, block_count, NULL, r_list);
	expect_assert_failure(vea_cancel(NULL, NULL, r_list));
	expect_assert_failure(vea_cancel(args.vua_vsi, NULL, NULL));
	vea_unload(args.vua_vsi);
	ut_teardown(&args);
}

static void
ut_inval_params_tx_publish(void **state)
{
	struct vea_ut_args args;
	uint32_t block_count = 1;
	uint32_t block_size = 0; /* use the default size */
	uint32_t header_blocks = 1;
	uint64_t capacity = ((VEA_LARGE_EXT_MB * 2) << 20); /* 128 MB */
	struct vea_unmap_context unmap_ctxt = { 0 };
	d_list_t *r_list;
	int rc;

	print_message("Testing invalid parameters to vea_tx_publish\n");
	ut_setup(&args);
	rc = vea_format(&args.vua_umm, &args.vua_txd, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_rc_equal(rc, 0);

	rc = vea_load(&args.vua_umm, &args.vua_txd, args.vua_md, &unmap_ctxt,
		      NULL, &args.vua_vsi);
	assert_rc_equal(rc, 0);
	r_list = &args.vua_resrvd_list[0];

	rc = vea_reserve(args.vua_vsi, block_count, NULL, r_list);
	assert_rc_equal(rc, 0);

	rc = umem_tx_begin(&args.vua_umm, &args.vua_txd);
	assert_int_equal(rc, 0);

	expect_assert_failure(vea_tx_publish(NULL, NULL, r_list));
	expect_assert_failure(vea_tx_publish(args.vua_vsi, NULL, NULL));

	rc = umem_tx_commit(&args.vua_umm); /* Why is this needed? */
	assert_int_equal(rc, 0);

	vea_unload(args.vua_vsi);
	ut_teardown(&args);
}

static void
ut_inval_params_free(void **state)
{
	struct vea_ut_args args;
	uint32_t block_count = 1;
	uint32_t block_size = 0; /* use the default size */
	uint32_t header_blocks = 1;
	uint64_t block_offset = 0;
	uint64_t capacity = ((VEA_LARGE_EXT_MB * 2) << 20); /* 128 MB */
	struct vea_unmap_context unmap_ctxt = { 0 };
	d_list_t *r_list;
	int rc;

	print_message("Testing invalid parameters to vea_free\n");
	ut_setup(&args);
	rc = vea_format(&args.vua_umm, &args.vua_txd, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_rc_equal(rc, 0);

	rc = vea_load(&args.vua_umm, &args.vua_txd, args.vua_md, &unmap_ctxt,
		      NULL, &args.vua_vsi);
	assert_rc_equal(rc, 0);
	r_list = &args.vua_resrvd_list[0];

	rc = vea_reserve(args.vua_vsi, block_count, NULL, r_list);
	assert_rc_equal(rc, 0);

	rc = vea_cancel(args.vua_vsi, NULL, r_list);
	assert_int_equal(rc, 0);

	expect_assert_failure(vea_free(NULL, block_offset, block_count));

	rc = vea_free(args.vua_vsi, block_offset, block_count);
	assert_rc_equal(rc, -DER_INVAL);

	/* Try block_count = 0 */
	block_count = 0;
	block_offset = 1;
	rc = vea_free(args.vua_vsi, block_offset, block_count);
	assert_rc_equal(rc, -DER_INVAL);

	vea_unload(args.vua_vsi);
	ut_teardown(&args);
}

static void
ut_inval_params_hint_load(void **state)
{
	struct vea_ut_args args;

	print_message("Testing invalid parameters to vea_hint_load\n");
	ut_setup(&args);

	expect_assert_failure(vea_hint_load(NULL, &args.vua_hint_ctxt[0]));
	expect_assert_failure(vea_hint_load(args.vua_hint[0], NULL));

	ut_teardown(&args);
}

static void
ut_inval_params_set_ext_age(void **state)
{
	struct vea_ut_args args;
	uint64_t block_offset = 0;
	uint64_t age = 0;

	print_message("Testing invalid parameters to vea_set_ext_age\n");
	ut_setup(&args);
	expect_assert_failure(vea_set_ext_age(NULL, block_offset, age));
	ut_teardown(&args);
}

static void
ut_inval_params_get_ext_vector(void **state)
{
	struct vea_ut_args args;
	uint64_t block_offset = 0;
	uint64_t block_count = 1;
	struct vea_ext_vector ext_vector;

	print_message("Testing invalid parameters to vea_get_ext_vector\n");
	ut_setup(&args);
	expect_assert_failure(vea_get_ext_vector(NULL, block_offset,
						 block_count, &ext_vector));
	expect_assert_failure(vea_get_ext_vector(args.vua_vsi, block_offset,
						 block_count, NULL));
	ut_teardown(&args);
}

static void
ut_free_invalid_space(void **state)
{
	struct vea_ut_args args;
	struct vea_unmap_context unmap_ctxt = { 0 };
	struct vea_hint_context *h_ctxt;
	struct vea_resrvd_ext *fake_ext;
	d_list_t *r_list;
	uint32_t block_count = 16;
	uint32_t block_size = 0; /* use the default size */
	uint32_t header_blocks = 1;
	uint64_t capacity = ((VEA_LARGE_EXT_MB * 2) << 20); /* 128 MB */
	int rc;

	print_message("Try to free space that's not valid\n");
	ut_setup(&args);
	rc = vea_format(&args.vua_umm, &args.vua_txd, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_int_equal(rc, 0);

	rc = vea_load(&args.vua_umm, &args.vua_txd, args.vua_md, &unmap_ctxt,
		      NULL, &args.vua_vsi);
	assert_int_equal(rc, 0);

	/* Reserve from I/O Stream 0 */
	r_list = &args.vua_resrvd_list[0];
	h_ctxt = args.vua_hint_ctxt[0];
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list);
	assert_int_equal(rc, 0);

	/* Try to free from I/O Stream 1, which hasn't been reserved */
	r_list = &args.vua_resrvd_list[1];
	h_ctxt = args.vua_hint_ctxt[1];
	D_ALLOC_PTR(fake_ext);
	assert_ptr_not_equal(fake_ext, NULL);
	fake_ext->vre_blk_cnt = 32;
	fake_ext->vre_blk_off = 64;
	d_list_add(&fake_ext->vre_link, r_list);
	rc = vea_cancel(args.vua_vsi, h_ctxt, r_list);
	assert_true(rc < 0);
	print_message("vea_cancel returned %d\n", rc);

	vea_unload(args.vua_vsi);
	ut_teardown(&args);
}

static void
print_stats(struct vea_ut_args *args, bool verbose)
{
	struct vea_stat	stat;
	int		rc;

	rc = vea_query(args->vua_vsi, NULL, &stat);
	assert_int_equal(rc, 0);
	print_message("free_blks:"DF_U64"/"DF_U64", frags_large:"DF_U64", "
		      "frags_small:"DF_U64", frags_aging:"DF_U64"\n"
		      "resrv_hint:"DF_U64"\nresrv_large:"DF_U64"\n"
		      "resrv_small:"DF_U64"\n",
		      stat.vs_free_persistent, stat.vs_free_transient,
		      stat.vs_frags_large, stat.vs_frags_small, stat.vs_frags_aging,
		      stat.vs_resrv_hint, stat.vs_resrv_large, stat.vs_resrv_small);

	if (verbose)
		vea_dump(args->vua_vsi, true);
}

static void
ut_interleaved_ops(void **state)
{
	struct vea_ut_args args;
	struct vea_unmap_context unmap_ctxt = { 0 };
	struct vea_hint_context *h_ctxt;
	d_list_t *r_list_a;
	d_list_t *r_list_b;
	uint32_t block_size = 0; /* use the default size */
	uint32_t header_blocks = 1;
	uint64_t capacity = ((VEA_LARGE_EXT_MB * 2) << 20); /* 128 MB */
	uint32_t block_count;
	int rc;

	print_message("Test interleaved operations\n");
	ut_setup(&args);
	rc = vea_format(&args.vua_umm, &args.vua_txd, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_int_equal(rc, 0);

	rc = vea_load(&args.vua_umm, &args.vua_txd, args.vua_md, &unmap_ctxt,
		      NULL, &args.vua_vsi);
	assert_int_equal(rc, 0);

	rc = umem_tx_begin(&args.vua_umm, &args.vua_txd);
	assert_int_equal(rc, 0);

	/*
	 * Do the following interleaved operations:
	 * 1. reserve A, reserve B, publish A, publish B
	 * 2. reserve A, reserve B, publish B, publish A
	 * 3. reserve A, reserve B, cancel B, publish A
	 * 4. reserve A, reserve B, publish A, cancel B
	 * 5. reserve A, reserve B, cancel A, publish B
	 * 6. reserve A, reserve B, publish B, cancel A
	 * 7. reserve A, reserve B, cancel A, cancel B
	 * 8. reserve A, reserve B, cancel B, cancel A
	 * 9. reserve A, reserve B, reserve C, publish B, publish A & C
	 **/
	block_count = 2;
	r_list_a = &args.vua_resrvd_list[0];
	r_list_b = &args.vua_resrvd_list[1];
	rc = vea_hint_load(args.vua_hint[0], &args.vua_hint_ctxt[0]);
	h_ctxt = args.vua_hint_ctxt[0];
	assert_rc_equal(rc, 0);

	/* Case 1 */
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_a);
	assert_rc_equal(rc, 0);
	block_count += 2;
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_b);
	assert_rc_equal(rc, 0);
	rc = vea_tx_publish(args.vua_vsi, h_ctxt, r_list_a);
	assert_int_equal(rc, 0);
	rc = vea_tx_publish(args.vua_vsi, h_ctxt, r_list_b);
	assert_int_equal(rc, 0);

	/* Case 2 */
	block_count += 2;
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_a);
	assert_rc_equal(rc, 0);
	block_count += 2;
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_b);
	assert_rc_equal(rc, 0);
	rc = vea_tx_publish(args.vua_vsi, h_ctxt, r_list_b);
	assert_int_equal(rc, 0);
	rc = vea_tx_publish(args.vua_vsi, h_ctxt, r_list_a);
	assert_int_equal(rc, 0);

	/* Case 3 */
	block_count += 2;
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_a);
	assert_rc_equal(rc, 0);
	block_count += 2;
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_b);
	assert_rc_equal(rc, 0);
	rc = vea_cancel(args.vua_vsi, h_ctxt, r_list_b);
	assert_int_equal(rc, 0);
	rc = vea_tx_publish(args.vua_vsi, h_ctxt, r_list_a);
	assert_int_equal(rc, 0);

	/* Case 4 */
	block_count += 2;
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_a);
	assert_rc_equal(rc, 0);
	block_count += 2;
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_b);
	assert_rc_equal(rc, 0);
	rc = vea_tx_publish(args.vua_vsi, h_ctxt, r_list_a);
	assert_int_equal(rc, 0);
	rc = vea_cancel(args.vua_vsi, h_ctxt, r_list_b);
	assert_int_equal(rc, 0);

	/* Case 5 */
	block_count += 2;
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_a);
	assert_rc_equal(rc, 0);
	block_count += 2;
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_b);
	assert_rc_equal(rc, 0);
	rc = vea_cancel(args.vua_vsi, h_ctxt, r_list_a);
	assert_int_equal(rc, 0);
	rc = vea_tx_publish(args.vua_vsi, h_ctxt, r_list_b);
	assert_int_equal(rc, 0);

	/* Case 6 */
	block_count += 2;
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_a);
	assert_rc_equal(rc, 0);
	block_count += 2;
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_b);
	assert_rc_equal(rc, 0);
	rc = vea_tx_publish(args.vua_vsi, h_ctxt, r_list_b);
	assert_int_equal(rc, 0);
	rc = vea_cancel(args.vua_vsi, h_ctxt, r_list_a);
	assert_int_equal(rc, 0);

	/* Case 7 */
	block_count += 2;
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_a);
	assert_rc_equal(rc, 0);
	block_count += 2;
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_b);
	assert_rc_equal(rc, 0);
	rc = vea_cancel(args.vua_vsi, h_ctxt, r_list_a);
	assert_int_equal(rc, 0);
	rc = vea_cancel(args.vua_vsi, h_ctxt, r_list_b);
	assert_int_equal(rc, 0);

	/* Case 8 */
	block_count += 2;
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_a);
	assert_rc_equal(rc, 0);
	block_count += 2;
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_b);
	assert_rc_equal(rc, 0);
	rc = vea_cancel(args.vua_vsi, h_ctxt, r_list_b);
	assert_int_equal(rc, 0);
	rc = vea_cancel(args.vua_vsi, h_ctxt, r_list_a);
	assert_int_equal(rc, 0);

	/* Case 9 */
	block_count = 2;
	/* Reserve A */
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_a);
	assert_rc_equal(rc, 0);
	/* Reserve B */
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_b);
	assert_rc_equal(rc, 0);
	/* Reserve C */
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list_a);
	assert_rc_equal(rc, 0);
	/* Publish B */
	rc = vea_tx_publish(args.vua_vsi, h_ctxt, r_list_b);
	assert_rc_equal(rc, 0);
	/* Publish A & C */
	rc = vea_tx_publish(args.vua_vsi, h_ctxt, r_list_a);
	assert_rc_equal(rc, 0);

	rc = umem_tx_commit(&args.vua_umm);
	assert_int_equal(rc, 0);
	print_stats(&args, true);

	vea_hint_unload(args.vua_hint_ctxt[0]);
	vea_unload(args.vua_vsi);
	ut_teardown(&args);
}

static void
ut_fragmentation(void **state)
{
	struct vea_ut_args args;
	struct vea_unmap_context unmap_ctxt = { 0 };
	struct vea_resrvd_ext *ext, *copy;
	struct vea_resrvd_ext *tmp_ext;
	d_list_t *r_list;
	d_list_t persist_list;
	uint64_t capacity = 32llu << 30; /* 32 GB */
	uint32_t block_size = 4096; /* use the default size */
	uint32_t header_blocks = 1;
	uint32_t block_count;
	uint32_t cur_stream;
	uint32_t max_blocks;
	int rc;

	print_message("Test allocation on fragmented device\n");
	ut_setup(&args);
	rc = vea_format(&args.vua_umm, &args.vua_txd, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_rc_equal(rc, 0);

	rc = vea_load(&args.vua_umm, &args.vua_txd, args.vua_md, &unmap_ctxt,
		      NULL, &args.vua_vsi);
	assert_rc_equal(rc, 0);

	/* Generate random fragments on the same I/O stream */
	srand(time(0));
	cur_stream = 0;
	r_list = &args.vua_resrvd_list[cur_stream];
	max_blocks = args.vua_vsi->vsi_class.vfc_large_thresh;
	/* Keep reserving until we run out of space */
	while (rc == 0) {
		/* Get a random number greater than 2 */
		block_count = (rand() % max_blocks - 1) + 2;
		rc = vea_reserve(args.vua_vsi, block_count, NULL, r_list);
	}

	/* Free some of the fragments. The canceled ones remain in r_list */
	D_INIT_LIST_HEAD(&persist_list);

	d_list_for_each_entry_safe(ext, tmp_ext, r_list, vre_link) {
		/* Copy the extents to keep to persist_list */
		/* Remove about every other fragment */
		if (rand() % 2 == 0) {
			d_list_move_tail(&ext->vre_link, &persist_list);

			D_ALLOC_PTR(copy);
			assert_ptr_not_equal(copy, NULL);

			D_INIT_LIST_HEAD(&copy->vre_link);
			copy->vre_blk_off = ext->vre_blk_off;
			copy->vre_blk_cnt = ext->vre_blk_cnt;
			d_list_add(&copy->vre_link, &args.vua_alloc_list);
		}
	}

	/* Publish the ones to persist */
	rc = umem_tx_begin(&args.vua_umm, &args.vua_txd);
	assert_int_equal(rc, 0);
	rc = vea_tx_publish(args.vua_vsi, NULL, &persist_list);
	assert_int_equal(rc, 0);
	rc = umem_tx_commit(&args.vua_umm);
	assert_int_equal(rc, 0);

	/* Cancel the reservations */
	rc = vea_cancel(args.vua_vsi, NULL, r_list);
	if (rc != 0) {
		print_message("vea_cancel() returned %d\n", rc);
		assert_int_equal(rc, 0);
	}

	print_message("Fragments:\n");
	print_stats(&args, false);

	/* Try to allocate on multiple I/O streams until no space available*/
	while (rc == 0) {
		for (cur_stream = 0; cur_stream < IO_STREAM_CNT; cur_stream++) {
			r_list = &args.vua_resrvd_list[cur_stream];
			/* Get a random number greater than 2 */
			block_count = (rand() % max_blocks - 1) + 2;
			rc = vea_reserve(args.vua_vsi, block_count, NULL,
					 r_list);
			if (rc != 0) {
				assert_true(rc == -DER_NOSPACE);
				break;
			}
		}
	}
	print_message("Fragments after more reservations:\n");
	print_stats(&args, false);

	/* Free allocated extents */
	r_list = &args.vua_alloc_list;
	d_list_for_each_entry(ext, r_list, vre_link) {
		uint64_t blk_off = ext->vre_blk_off;
		uint32_t blk_cnt = ext->vre_blk_cnt;

		rc = vea_free(args.vua_vsi, blk_off, blk_cnt);
		assert_rc_equal(rc, 0);

		/* not immediately visual for allocation */
		rc = vea_verify_alloc(args.vua_vsi, true, blk_off, blk_cnt);
		assert_rc_equal(rc, 0);

		rc = vea_verify_alloc(args.vua_vsi, false, blk_off, blk_cnt);
		assert_rc_equal(rc, 1);
	}

	vea_unload(args.vua_vsi);
	ut_teardown(&args);
}

static const struct CMUnitTest vea_uts[] = {
	{ "vea_format", ut_format, NULL, NULL},
	{ "vea_load", ut_load, NULL, NULL},
	{ "vea_query", ut_query, NULL, NULL},
	{ "vea_hint_load", ut_hint_load, NULL, NULL},
	{ "vea_reserve", ut_reserve, NULL, NULL},
	{ "vea_cancel", ut_cancel, NULL, NULL},
	{ "vea_tx_publish", ut_tx_publish, NULL, NULL},
	{ "vea_free", ut_free, NULL, NULL},
	{ "vea_hint_unload", ut_hint_unload, NULL, NULL},
	{ "vea_unload", ut_unload, NULL, NULL},
	{ "vea_reserve_special", ut_reserve_special, NULL, NULL},
	{ "vea_inval_params_format", ut_inval_params_format, NULL, NULL},
	{ "vea_inval_params_load", ut_inval_params_load, NULL, NULL},
	{ "vea_inval_param_reserve", ut_inval_params_reserve, NULL, NULL},
	{ "vea_inval_param_cancel", ut_inval_params_cancel, NULL, NULL},
	{ "vea_inval_param_tx_publish", ut_inval_params_tx_publish, NULL, NULL},
	{ "vea_inval_param_free", ut_inval_params_free, NULL, NULL},
	{ "vea_inval_param_hint_load", ut_inval_params_hint_load, NULL, NULL},
	{ "vea_inval_param_set_ext_age", ut_inval_params_set_ext_age, NULL,
	  NULL},
	{ "vea_inval_param_get_ext_vector", ut_inval_params_get_ext_vector,
	  NULL, NULL},
	{ "vea_free_invalid_space", ut_free_invalid_space, NULL, NULL},
	{ "vea_interleaved_ops", ut_interleaved_ops, NULL, NULL},
	{ "vea_fragmentation", ut_fragmentation, NULL, NULL}
};

int main(int argc, char **argv)
{
	static struct option long_ops[] = {
		{ "file",	required_argument,	NULL,	'f' },
		{ "help",	no_argument,		NULL,	'h' },
		{ NULL,		0,			NULL,	0   },
	};
	int rc;

	d_register_alt_assert(mock_assert);

	memset(pool_file, 0, sizeof(pool_file));
	while ((rc = getopt_long(argc, argv, "f:h", long_ops, NULL)) != -1) {
		switch (rc) {
		case 'f':
			strncpy(pool_file, optarg, PATH_MAX - 1);
			break;
		case 'h':
			print_usage();
			return 0;
		default:
			fprintf(stderr, "unknown option %c\n", rc);
			print_usage();
			return -1;
		}
	}

	if (strlen(pool_file) == 0)
		strncpy(pool_file, "/mnt/daos/vea_ut_pool", sizeof(pool_file));

	return cmocka_run_group_tests_name("VEA unit tests", vea_uts,
					   vea_ut_setup, vea_ut_teardown);
}
