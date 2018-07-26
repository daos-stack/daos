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

#define IO_STREAM_CNT	3

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
		print_message("unload hint of I/O stream:%d\n", i);
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

	rc = vea_verify_alloc(args->vua_vsi, true, off_a, blk_cnt);
	assert_int_equal(rc, 0);
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

	for (i = 1; i < 3; i++) {
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
	d_list_for_each_entry(ext, r_list, vre_link) {
		blk_off = ext->vre_blk_off;
		blk_cnt = ext->vre_blk_cnt;

		rc = vea_verify_alloc(args->vua_vsi, true, blk_off, blk_cnt);
		assert_int_equal(rc, 1);
	}
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
			hdr_blks, capacity, NULL, NULL, true);
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
	assert_true((rc == -DER_NOSPACE) | (rc == -DER_INVAL));
	print_message("correctly failed to reserve extent\n");
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
	{ "vea_reserve_too_big", ut_reserve_too_big, NULL, NULL}
};

int main(int argc, char **argv)
{
	static struct option long_ops[] = {
		{ "file",	required_argument,	NULL,	'f' },
		{ "help",	no_argument,		NULL,	'h' },
		{ NULL,		0,			NULL,	0   },
	};
	int rc;

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
