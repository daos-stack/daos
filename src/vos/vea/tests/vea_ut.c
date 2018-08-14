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

#define D_LOGFAC	DD_FAC(tests)

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>
#include <getopt.h>

#include <daos/common.h>
#include <daos/btree_class.h>
#include <daos_srv/vea.h>
#include <../vea_internal.h>

char		pool_file[PATH_MAX];

#define IO_STREAM_CNT	(3)

struct vea_ut_args {
	struct umem_instance	 vua_umm;
	struct vea_space_df	*vua_md;
	struct vea_hint_df	*vua_hint[IO_STREAM_CNT];
	struct vea_space_info	*vua_vsi;
	struct vea_hint_context *vua_hint_ctxt[IO_STREAM_CNT];
	d_list_t		 vua_resrvd_list[IO_STREAM_CNT];
	d_list_t		 vua_alloc_list;
};

static struct vea_ut_args	ut_args;

static void
print_usage(void)
{
	fprintf(stdout, "vea_ut [-f <pool_file_name>]\n");
}

static void
ut_format(void **state)
{
	struct vea_ut_args *args = *state;
	uint32_t blk_sz = 0; /* use the default size */
	uint32_t hdr_blks = 1;
	uint64_t capacity = ((VEA_LARGE_EXT_MB * 2) << 20); /* 128MB */
	int rc;

	/* format */
	print_message("format\n");
	rc = vea_format(&args->vua_umm, args->vua_md, blk_sz,
			hdr_blks, capacity, NULL, NULL, false);
	assert_int_equal(rc, 0);

	/* reformat without setting 'force' */
	print_message("reformat without setting 'force'\n");
	rc = vea_format(&args->vua_umm, args->vua_md, blk_sz,
			hdr_blks, capacity, NULL, NULL, false);
	assert_int_equal(rc, -DER_EXIST);

	/* reformat with 'force' */
	print_message("reformat with 'force'\n");
	rc = vea_format(&args->vua_umm, args->vua_md, blk_sz,
			hdr_blks, capacity, NULL, NULL, true);
	assert_int_equal(rc, 0);

	/* TODO: Test VOS format callback */
}

static void
ut_load(void **state)
{
	struct vea_ut_args *args = *state;
	struct vea_unmap_context unmap_ctxt;
	int rc;

	unmap_ctxt.vnc_unmap = NULL;
	unmap_ctxt.vnc_data = NULL;
	rc = vea_load(&args->vua_umm, args->vua_md, &unmap_ctxt,
		      &args->vua_vsi);
	assert_int_equal(rc, 0);

	/* TODO: Test VOS unmap callback */
}

static void
ut_hint_load(void **state)
{
	struct vea_ut_args *args = *state;
	int i, rc;

	for (i = 0; i < IO_STREAM_CNT; i++) {
		print_message("load hint of I/O stream:%d\n", i);
		rc = vea_hint_load(args->vua_hint[i], &args->vua_hint_ctxt[i]);
		assert_int_equal(rc, 0);
	}
}

static void
ut_reserve(void **state)
{
	struct vea_ut_args *args = *state;
	uint64_t off_a, off_b;
	uint32_t blk_cnt;
	struct vea_resrvd_ext *ext;
	struct vea_hint_context *h_ctxt;
	d_list_t *r_list;
	int rc, ext_cnt;

	/*
	 * Reserve two extents from I/O stream 0 and I/O stream 1 in
	 * interleaved order, the reservation from I/O stream 0 will be
	 * cancelled later, and the reservation from I/O stream 1 will
	 * be published.
	 */
	off_a = off_b = VEA_HINT_OFF_INVAL;
	for (ext_cnt = 0; ext_cnt < 2; ext_cnt++) {
		print_message("reserve extent %d from I/O stream 0\n", ext_cnt);

		r_list = &args->vua_resrvd_list[0];
		h_ctxt = args->vua_hint_ctxt[0];

		blk_cnt = (ext_cnt == 0) ? 10 : 1;
		rc = vea_reserve(args->vua_vsi, blk_cnt, h_ctxt, r_list);
		assert_int_equal(rc, 0);

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
		assert_int_equal(rc, 0);
		rc = vea_verify_alloc(args->vua_vsi, false, off_a, blk_cnt);
		assert_int_equal(rc, 1);

		/* update hint offset */
		off_a += blk_cnt;

		print_message("reserve extent %d from I/O stream 1\n", ext_cnt);

		r_list = &args->vua_resrvd_list[1];
		h_ctxt = args->vua_hint_ctxt[1];

		blk_cnt = (ext_cnt == 0) ? 256 : 4;
		rc = vea_reserve(args->vua_vsi, blk_cnt, h_ctxt, r_list);
		assert_int_equal(rc, 0);

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
		assert_int_equal(rc, 0);
		rc = vea_verify_alloc(args->vua_vsi, false, off_b, blk_cnt);
		assert_int_equal(rc, 1);

		/* update hint offset */
		off_b += blk_cnt;
	}

	/* Reserve from I/O stream 2, it will reserve from small free extent */
	print_message("reserve extent from I/O stream 2\n");

	r_list = &args->vua_resrvd_list[2];
	h_ctxt = args->vua_hint_ctxt[2];

	blk_cnt = 1024;
	rc = vea_reserve(args->vua_vsi, blk_cnt, h_ctxt, r_list);
	assert_int_equal(rc, 0);

	/* correctness check */
	ext = d_list_entry(r_list->prev, struct vea_resrvd_ext, vre_link);
	assert_int_equal(ext->vre_hint_off, VEA_HINT_OFF_INVAL);
	assert_int_equal(ext->vre_blk_cnt, blk_cnt);
	/* start from the end of the stream 0 */
	assert_int_equal(ext->vre_blk_off, off_a);

	/* Verify transient is allocated */
	rc = vea_verify_alloc(args->vua_vsi, true, off_a, blk_cnt);
	assert_int_equal(rc, 0);
	/* Verify persistent is not allocated */
	rc = vea_verify_alloc(args->vua_vsi, false, off_a, blk_cnt);
	assert_int_equal(rc, 1);
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
	assert_int_equal(rc, 1);
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

	rc = umem_tx_begin(&args->vua_umm);
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
		assert_int_equal(rc, 0);

		rc = vea_verify_alloc(args->vua_vsi, false, blk_off, blk_cnt);
		assert_int_equal(rc, 0);
	}
}

static void
ut_free(void **state)
{
	struct vea_ut_args *args = *state;
	struct vea_hint_context *h_ctxt;
	struct vea_resrvd_ext *ext;
	d_list_t *r_list;
	uint64_t blk_off;
	uint32_t blk_cnt;
	uint32_t blk_tot;
	int rc;

	r_list = &args->vua_alloc_list;
	d_list_for_each_entry(ext, r_list, vre_link) {
		blk_off = ext->vre_blk_off;
		blk_cnt = ext->vre_blk_cnt;

		rc = vea_free(args->vua_vsi, blk_off, blk_cnt);
		assert_int_equal(rc, 0);

		/* not immediately visual for allocation */
		rc = vea_verify_alloc(args->vua_vsi, true, blk_off, blk_cnt);
		assert_int_equal(rc, 0);

		rc = vea_verify_alloc(args->vua_vsi, false, blk_off, blk_cnt);
		assert_int_equal(rc, 1);
	}

	print_message("transient free extents:\n");
	vea_dump(args->vua_vsi, true);
	print_message("persistent free extents:\n");
	vea_dump(args->vua_vsi, false);

	/* wait for free extents expire */
	print_message("wait for %d seconds ...\n", VEA_MIGRATE_INTVL);
	sleep(VEA_MIGRATE_INTVL);
	/* call reserve to trigger free extents migration */
	r_list = &args->vua_resrvd_list[0];
	h_ctxt = args->vua_hint_ctxt[0];
	rc = vea_reserve(args->vua_vsi, 1, h_ctxt, r_list);
	assert_int_equal(rc, 0);

	rc = vea_cancel(args->vua_vsi, h_ctxt, r_list);
	assert_int_equal(rc, 0);

	r_list = &args->vua_alloc_list;
	blk_tot = 0;
	d_list_for_each_entry(ext, r_list, vre_link) {
		blk_off = ext->vre_blk_off;
		blk_cnt = ext->vre_blk_cnt;
		blk_tot += blk_cnt;

		rc = vea_verify_alloc(args->vua_vsi, true, blk_off, blk_cnt);
		assert_int_equal(rc, 1);
	}

	/* Verify free space has been merged and is not allocated*/
	ext = d_list_entry(r_list->prev, struct vea_resrvd_ext, vre_link);
	blk_off = ext->vre_blk_off;
	rc = vea_verify_alloc(args->vua_vsi, true, blk_off, blk_tot);
	assert_int_equal(rc, 1); /* 1 means it is not allocated */
	print_message("transient free extents after migration:\n");
	vea_dump(args->vua_vsi, true);
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
	struct umem_attr uma;
	PMEMoid root;
	void *root_addr;
	int rc, i;

	memset(test_args, 0, sizeof(struct vea_ut_args));
	D_INIT_LIST_HEAD(&test_args->vua_alloc_list);

	unlink(pool_file);

	uma.uma_id = UMEM_CLASS_PMEM;
	uma.uma_u.pmem_pool = pmemobj_create(pool_file, "vea_ut",
					     pool_size, 0666);
	if (uma.uma_u.pmem_pool == NULL) {
		fprintf(stderr, "create pmemobj pool error\n");
		return -1;
	}

	rc = umem_class_init(&uma, &test_args->vua_umm);
	if (rc) {
		fprintf(stderr, "initialize umm error %d\n", rc);
		goto error;
	}

	root = pmemobj_root(test_args->vua_umm.umm_u.pmem_pool,
			    sizeof(struct vea_space_df) +
			    sizeof(struct vea_hint_df) * IO_STREAM_CNT);
	if (OID_IS_NULL(root)) {
		fprintf(stderr, "get root error\n");
		rc = -1;
		goto error;
	}

	root_addr = pmemobj_direct(root);
	test_args->vua_md = root_addr;
	root_addr += sizeof(struct vea_space_df);

	for (i = 0; i < IO_STREAM_CNT; i++) {
		test_args->vua_hint[i] = root_addr;

		test_args->vua_hint[i]->vhd_off = 0;
		test_args->vua_hint[i]->vhd_seq = 0;
		D_INIT_LIST_HEAD(&test_args->vua_resrvd_list[i]);

		root_addr += sizeof(struct vea_hint_df);
	}
	return 0;
error:
	pmemobj_close(uma.uma_u.pmem_pool);
	test_args->vua_umm.umm_u.pmem_pool = NULL;

	return rc;
}

static int
vea_ut_setup(void **state)
{
	int rc;

	rc = daos_debug_init(NULL);
	if (rc != 0)
		return rc;

	rc = dbtree_class_register(DBTREE_CLASS_IV,
				   BTR_FEAT_UINT_KEY,
				   &dbtree_iv_ops);
	if (rc != 0 && rc != -DER_EXIST) {
		fprintf(stderr, "register DBTREE_CLASS_IV error %d\n", rc);
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

	r_list = &test_args->vua_alloc_list;
	d_list_for_each_entry_safe(ext, tmp, r_list, vre_link) {
		d_list_del_init(&ext->vre_link);
		D_FREE_PTR(ext);
	}

	if (test_args->vua_umm.umm_u.pmem_pool != NULL) {
		pmemobj_close(test_args->vua_umm.umm_u.pmem_pool);
		test_args->vua_umm.umm_u.pmem_pool = NULL;
	}
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
ut_reserve_too_big(void **state)
{
	/* Use a temporary device instead of the main one the other tests use */
	struct vea_ut_args args;
	uint32_t blk_cnt = 0;
	d_list_t *r_list;
	uint32_t hdr_blks = 1;
	uint64_t capacity = 4 << 20; /* 4MB */
	struct vea_unmap_context unmap_ctxt;
	uint32_t blk_sz = 0; /* use the default size */
	int rc;

	ut_setup(&args);

	rc = vea_format(&args.vua_umm, args.vua_md, blk_sz,
			hdr_blks, capacity, NULL, NULL, false);
	assert_int_equal(rc, 0);

	unmap_ctxt.vnc_unmap = NULL;
	unmap_ctxt.vnc_data = NULL;
	rc = vea_load(&args.vua_umm, args.vua_md, &unmap_ctxt,
		      &args.vua_vsi);
	assert_int_equal(rc, 0);

	print_message("Try to reserve extent larger than available space\n");

	r_list = &args.vua_resrvd_list[0];

	/* reserve should fail */
	blk_cnt = 15000; /* 15000 * 4k >> 4MB */
	rc = vea_reserve(args.vua_vsi, blk_cnt, NULL, r_list);
	/* expect -DER_NOSPACE or -DER_INVAL (if blk_cnt > VEA_LARGE_EXT_MB) */
	assert_true((rc == -DER_NOSPACE) || (rc == -DER_INVAL));
	print_message("correctly failed to reserve extent\n");
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
	expect_assert_failure(vea_format(NULL, args.vua_md, block_size,
					 header_blocks, capacity, NULL, NULL,
					 false));

	/* vea_format: Test null md */
	expect_assert_failure(vea_format(&args.vua_umm, NULL, block_size,
					 header_blocks, capacity, NULL, NULL,
					 false));

	/* vea_format: Test large block_size */
	block_size = UINT32_MAX;
	rc = vea_format(&args.vua_umm, args.vua_md, block_size, header_blocks,
			capacity, NULL, NULL, false);
	assert_int_equal(rc, -DER_INVAL);

	/* vea_format: Test non-4k aligned block_size */
	block_size = 4095;
	rc = vea_format(&args.vua_umm, args.vua_md, block_size, header_blocks,
			capacity, NULL, NULL, false);
	assert_int_equal(rc, -DER_INVAL);

	/* vea_format: Test no header blocks */
	block_size = 0; /* Set to 0 to use the default 4K block size */
	header_blocks = 0;
	rc = vea_format(&args.vua_umm, args.vua_md, block_size, header_blocks,
			capacity, NULL, NULL, false);
	assert_int_equal(rc, -DER_INVAL);

	/* vea_format: Test large value for header_blocks */
	header_blocks = UINT32_MAX;
	rc = vea_format(&args.vua_umm, args.vua_md, block_size, header_blocks,
			capacity, NULL, NULL, false);
	assert_int_equal(rc, -DER_NOSPACE);

	/* vea_format: Test small value for capacity */
	header_blocks = 1;
	capacity = 0;
	rc = vea_format(&args.vua_umm, args.vua_md, block_size, header_blocks,
			capacity, NULL, NULL, false);
	assert_int_equal(rc, -DER_NOSPACE);

	/* vea_format: Make capacity and block_size equal */
	capacity = 4096;
	rc = vea_format(&args.vua_umm, args.vua_md, block_size, header_blocks,
			capacity, NULL, NULL, false);
	assert_int_equal(rc, -DER_NOSPACE);

	ut_teardown(&args);
}

static void
ut_inval_params_load(void **state)
{
	struct vea_ut_args args;
	uint32_t block_size = 0; /* use the default size */
	uint32_t header_blocks = 1;
	uint64_t capacity = ((VEA_LARGE_EXT_MB * 2) << 20); /* 128 MB */
	struct vea_unmap_context unmap_ctxt;
	int rc;

	ut_setup(&args);
	print_message("Testing invalid parameters to vea_load\n");

	/* vea_load: Test unformatted blob */
	rc = vea_load(&args.vua_umm, args.vua_md, &unmap_ctxt, &args.vua_vsi);
	assert_int_equal(rc, -DER_UNINIT);

	/* vea_load: Test umem is NULL */
	/* First correctly format the blob */
	capacity = ((VEA_LARGE_EXT_MB * 2) << 20); /* 128 MB */
	rc = vea_format(&args.vua_umm, args.vua_md, block_size, header_blocks,
			capacity, NULL, NULL, false);
	assert_int_equal(rc, 0);
	expect_assert_failure(vea_load(NULL, args.vua_md, &unmap_ctxt,
				       &args.vua_vsi));

	/* vea_load: Test md is NULL */
	expect_assert_failure(vea_load(&args.vua_umm, NULL, &unmap_ctxt,
				       &args.vua_vsi));

	/* vea_load: Test unmap_ctxt is NULL */
	expect_assert_failure(vea_load(&args.vua_umm, args.vua_md, NULL,
				       &args.vua_vsi));

	/* vea_load: Test vsip is NULL */
	expect_assert_failure(vea_load(&args.vua_umm, args.vua_md, &unmap_ctxt,
				       NULL));

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
	struct vea_unmap_context unmap_ctxt;
	d_list_t *r_list;
	int rc;

	ut_setup(&args);
	print_message("Testing invalid parameters to vea_reserve\n");
	rc = vea_format(&args.vua_umm, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_int_equal(rc, 0);

	unmap_ctxt.vnc_unmap = NULL;
	unmap_ctxt.vnc_data = NULL;
	rc = vea_load(&args.vua_umm, args.vua_md, &unmap_ctxt,
		      &args.vua_vsi);
	assert_int_equal(rc, 0);

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
	struct vea_unmap_context unmap_ctxt;
	d_list_t *r_list;
	int rc;

	print_message("Testing invalid parameters to vea_cancel\n");
	ut_setup(&args);
	rc = vea_format(&args.vua_umm, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_int_equal(rc, 0);

	unmap_ctxt.vnc_unmap = NULL;
	unmap_ctxt.vnc_data = NULL;
	rc = vea_load(&args.vua_umm, args.vua_md, &unmap_ctxt,
		      &args.vua_vsi);
	assert_int_equal(rc, 0);
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
	struct vea_unmap_context unmap_ctxt;
	d_list_t *r_list;
	int rc;

	print_message("Testing invalid parameters to vea_tx_publish\n");
	ut_setup(&args);
	rc = vea_format(&args.vua_umm, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_int_equal(rc, 0);

	unmap_ctxt.vnc_unmap = NULL;
	unmap_ctxt.vnc_data = NULL;
	rc = vea_load(&args.vua_umm, args.vua_md, &unmap_ctxt,
		      &args.vua_vsi);
	assert_int_equal(rc, 0);
	r_list = &args.vua_resrvd_list[0];

	rc = vea_reserve(args.vua_vsi, block_count, NULL, r_list);
	assert_int_equal(rc, 0);

	rc = umem_tx_begin(&args.vua_umm);
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
	struct vea_unmap_context unmap_ctxt;
	d_list_t *r_list;
	int rc;

	print_message("Testing invalid parameters to vea_free\n");
	ut_setup(&args);
	rc = vea_format(&args.vua_umm, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_int_equal(rc, 0);

	unmap_ctxt.vnc_unmap = NULL;
	unmap_ctxt.vnc_data = NULL;
	rc = vea_load(&args.vua_umm, args.vua_md, &unmap_ctxt,
		      &args.vua_vsi);
	assert_int_equal(rc, 0);
	r_list = &args.vua_resrvd_list[0];

	rc = vea_reserve(args.vua_vsi, block_count, NULL, r_list);
	assert_int_equal(rc, 0);

	rc = vea_cancel(args.vua_vsi, NULL, r_list);
	assert_int_equal(rc, 0);

	expect_assert_failure(vea_free(NULL, block_offset, block_count));

	rc = vea_free(args.vua_vsi, block_offset, block_count);
	assert_int_equal(rc, -DER_INVAL);

	/* Try block_count = 0 */
	block_count = 0;
	block_offset = 1;
	rc = vea_free(args.vua_vsi, block_offset, block_count);
	assert_int_equal(rc, -DER_INVAL);

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
	struct vea_unmap_context unmap_ctxt;
	struct vea_hint_context *h_ctxt;
	struct vea_resrvd_ext fake_ext;
	d_list_t *r_list;
	uint32_t block_count = 16;
	uint32_t block_size = 0; /* use the default size */
	uint32_t header_blocks = 1;
	uint64_t capacity = ((VEA_LARGE_EXT_MB * 2) << 20); /* 128 MB */
	int rc;

	/* Skip until it stops aborting the program */
	skip();

	print_message("Try to free space that's not valid\n");
	ut_setup(&args);
	rc = vea_format(&args.vua_umm, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_int_equal(rc, 0);

	unmap_ctxt.vnc_unmap = NULL;
	unmap_ctxt.vnc_data = NULL;
	rc = vea_load(&args.vua_umm, args.vua_md, &unmap_ctxt,
		      &args.vua_vsi);
	assert_int_equal(rc, 0);

	/* Reserve from I/O Stream 0 */
	r_list = &args.vua_resrvd_list[0];
	h_ctxt = args.vua_hint_ctxt[0];
	rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list);
	assert_int_equal(rc, 0);

	/* Try to free from I/O Stream 1, which hasn't been reserved */
	r_list = &args.vua_resrvd_list[1];
	h_ctxt = args.vua_hint_ctxt[1];
	fake_ext.vre_blk_cnt = 32;
	fake_ext.vre_blk_off = 64;
	d_list_add_tail(&fake_ext.vre_link, r_list);
	rc = vea_cancel(args.vua_vsi, h_ctxt, r_list); /* Causes an abort*/
	assert_true(rc < 0);

	vea_unload(args.vua_vsi);
	ut_teardown(&args);
}

#define EXTENT_COUNT (4)
static void
ut_interleaved_ops(void **state)
{
	struct vea_ut_args args;
	struct vea_unmap_context unmap_ctxt;
	struct vea_hint_context *h_ctxt;
	d_list_t *r_list;
	uint32_t block_size = 0; /* use the default size */
	uint32_t header_blocks = 1;
	uint64_t capacity = ((VEA_LARGE_EXT_MB * 2) << 20); /* 128 MB */
	uint32_t block_count;
	uint32_t cur_extent;
	uint32_t cur_stream;
	int rc;

	print_message("Test interleaved operations\n");
	ut_setup(&args);
	rc = vea_format(&args.vua_umm, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_int_equal(rc, 0);

	unmap_ctxt.vnc_unmap = NULL;
	unmap_ctxt.vnc_data = NULL;
	rc = vea_load(&args.vua_umm, args.vua_md, &unmap_ctxt,
		      &args.vua_vsi);
	assert_int_equal(rc, 0);

	rc = umem_tx_begin(&args.vua_umm);
	assert_int_equal(rc, 0);

	for (cur_extent = 0; cur_extent < EXTENT_COUNT; cur_extent++) {
		/* stream 0 will have blocks of 2 + 4 + 6 + 8   */
		/* stream 1 will have blocks of 3 + 6 + 9 + 12  */
		/* stream 2 will have blocks of 4 + 8 + 12 + 16 */
		for (cur_stream = 0; cur_stream < IO_STREAM_CNT; cur_stream++) {
			r_list = &args.vua_resrvd_list[cur_stream];
			h_ctxt = args.vua_hint_ctxt[cur_stream];
			block_count = (cur_stream + 2) * (cur_extent + 1);
			rc = vea_reserve(args.vua_vsi, block_count, h_ctxt,
					 r_list);
			assert_int_equal(rc, 0);

			/* Publish streams 1 and 2 */
			if (cur_stream != 0) {
				rc = vea_tx_publish(args.vua_vsi, h_ctxt,
						    r_list);
				assert_int_equal(rc, 0);
			}
		}
	}

	rc = umem_tx_commit(&args.vua_umm);
	assert_int_equal(rc, 0);

	/* Cancel reservations in stream 0 */
	cur_stream = 0;
	r_list = &args.vua_resrvd_list[cur_stream];
	h_ctxt = args.vua_hint_ctxt[cur_stream];
	rc = vea_cancel(args.vua_vsi, h_ctxt, r_list);
	assert_int_equal(rc, 0);

	/* Do I need to verify any memory here? Other tests already did that */

	vea_unload(args.vua_vsi);
	ut_teardown(&args);
}

static void
ut_fragmentation(void **state)
{
	struct vea_ut_args args;
	struct vea_unmap_context unmap_ctxt;
	struct vea_hint_context *h_ctxt;
	struct vea_resrvd_ext *ext;
	d_list_t *r_list;
	uint64_t capacity = ((VEA_LARGE_EXT_MB * 2) << 20); /* 128 MB */
	uint32_t block_size = 4096; /* use the default size */
	int32_t blocks_remaining = capacity / block_size;
	uint32_t header_blocks = 1;
	uint32_t block_count;
	uint32_t cur_extent;
	uint32_t cur_stream;
	int rc;

	print_message("Test allocation on fragmented device\n");
	ut_setup(&args);
	rc = vea_format(&args.vua_umm, args.vua_md, block_size,
			header_blocks, capacity, NULL, NULL, false);
	assert_int_equal(rc, 0);

	unmap_ctxt.vnc_unmap = NULL;
	unmap_ctxt.vnc_data = NULL;
	rc = vea_load(&args.vua_umm, args.vua_md, &unmap_ctxt,
		      &args.vua_vsi);
	assert_int_equal(rc, 0);

	/* Generate random fragments on the same I/O stream */
	/* Capacity=128 MB, block size=4096 bytes, so I have 32,768 blocks */
	srand(276593); /* A random prime */
	cur_stream = 0;
	r_list = &args.vua_resrvd_list[cur_stream];
	h_ctxt = args.vua_hint_ctxt[cur_stream];
	while (blocks_remaining > 0) {
		/* Get a random number between 2 and 1024 */
		block_count = rand() % (1023) + 2;
		/* Need to check if there are less than 256 blocks remaining. */
		/* Otherwise, we run out of space */
		if (blocks_remaining - (int32_t)block_count < 256) {
			block_count = (uint32_t)blocks_remaining - 256;
			blocks_remaining = 0;
		}
		rc = vea_reserve(args.vua_vsi, block_count, h_ctxt, r_list);
		assert_int_equal(rc, 0);
		blocks_remaining -= block_count;
	}

	/* Free some of the fragments */
	d_list_for_each_entry(ext, r_list, vre_link) {
		/* Remove about every other fragment */
		if (rand() % 2 == 0)
			d_list_del(&ext->vre_link);
	}
	rc = vea_cancel(args.vua_vsi, NULL, r_list);

	print_message("Fragments:\n");
	vea_dump(args.vua_vsi, true);

	/* Try to allocate on multiple I/O streams */
	for (cur_extent = 0; cur_extent < EXTENT_COUNT; cur_extent++) {
		for (cur_stream = 0; cur_stream < IO_STREAM_CNT; cur_stream++) {
			r_list = &args.vua_resrvd_list[cur_stream];
			h_ctxt = args.vua_hint_ctxt[cur_stream];
			/* Get a random number between 2 and 1024 */
			block_count = rand() % (1023) + 2;
			rc = vea_reserve(args.vua_vsi, block_count, h_ctxt,
					 r_list);
			assert_int_equal(rc, 0);
		}
	}
	print_message("Fragments after more reservations:\n");
	vea_dump(args.vua_vsi, true);

	vea_unload(args.vua_vsi);
	ut_teardown(&args);
}

static const struct CMUnitTest vea_uts[] = {
	{ "vea_format", ut_format, NULL, NULL},
	{ "vea_load", ut_load, NULL, NULL},
	{ "vea_hint_load", ut_hint_load, NULL, NULL},
	{ "vea_reserve", ut_reserve, NULL, NULL},
	{ "vea_cancel", ut_cancel, NULL, NULL},
	{ "vea_tx_publish", ut_tx_publish, NULL, NULL},
	{ "vea_free", ut_free, NULL, NULL},
	{ "vea_hint_unload", ut_hint_unload, NULL, NULL},
	{ "vea_unload", ut_unload, NULL, NULL},
	{ "vea_reserve_too_big", ut_reserve_too_big, NULL, NULL},
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
