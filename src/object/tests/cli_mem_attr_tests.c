/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos/tests_lib.h>
#include <daos/task.h>
#include "../cli_mem_attr.h"
#include "../../cart/crt_mem.h"

static void
mem_attrs_valid(void **state)
{
	daos_mem_attr_t attrs[2] = {0};

	assert_rc_equal(dc_obj_mem_attrs_valid(NULL, NULL, 1), 0);

	attrs[0].ma_mem_type  = DAOS_MEM_TYPE_HOST;
	attrs[1].ma_mem_type  = DAOS_MEM_TYPE_CUDA;
	attrs[1].ma_device_id = 3;
	assert_rc_equal(dc_obj_mem_attrs_valid(attrs, NULL, 2), 0);

	/* host memory has no device */
	attrs[0].ma_device_id = 1;
	assert_rc_equal(dc_obj_mem_attrs_valid(attrs, NULL, 2), -DER_INVAL);
	attrs[0].ma_device_id = 0;

	attrs[1].ma_mem_type = (daos_mem_type_t)0x7fff;
	assert_rc_equal(dc_obj_mem_attrs_valid(attrs, NULL, 2), -DER_INVAL);
	attrs[1].ma_mem_type = DAOS_MEM_TYPE_CUDA;

	/* ma_csums is reserved */
	d_iov_set(&attrs[1].ma_csums, &attrs[0], sizeof(attrs[0]));
	assert_rc_equal(dc_obj_mem_attrs_valid(attrs, NULL, 2), -DER_INVAL);
}

static void
mem_attrs_flags(void **state)
{
	daos_mem_attr_t attrs[2] = {0};

	assert_int_equal(dc_obj_mem_attrs_flags(NULL, 2, 0), 0);

	attrs[0].ma_mem_type = DAOS_MEM_TYPE_HOST;
	attrs[1].ma_mem_type = DAOS_MEM_TYPE_HOST;
	assert_int_equal(dc_obj_mem_attrs_flags(attrs, 2, 0), 0);

	attrs[1].ma_mem_type = DAOS_MEM_TYPE_CUDA;
	assert_int_equal(dc_obj_mem_attrs_flags(attrs, 2, 1), 1 | DAOS_OBJ_IO_GPU_DIRECT);
}

static void
mem_attrs_dup(void **state)
{
	daos_mem_attr_t  src[2] = {0};
	daos_mem_attr_t *dup    = NULL;

	src[0].ma_mem_type  = DAOS_MEM_TYPE_CUDA;
	src[0].ma_device_id = 2;
	src[1].ma_mem_type  = DAOS_MEM_TYPE_HOST;

	assert_rc_equal(dc_obj_mem_attrs_dup(src, 2, &dup), 0);
	assert_non_null(dup);

	assert_int_equal(dup[0].ma_mem_type, DAOS_MEM_TYPE_CUDA);
	assert_int_equal(dup[0].ma_device_id, 2);
	assert_null(dup[0].ma_handle);

	assert_int_equal(dup[1].ma_mem_type, DAOS_MEM_TYPE_HOST);

	/* the copy must not depend on the caller's array */
	memset(src, 0, sizeof(src));
	assert_int_equal(dup[0].ma_mem_type, DAOS_MEM_TYPE_CUDA);

	dc_obj_mem_attrs_free(dup, 2);
}

static void
mem_attrs_handle(void **state)
{
	char                   backing[4096 + 64];
	char                  *pool = backing + 32;
	struct crt_mem_handle  mh   = {.cmh_buf = pool,
				       .cmh_len = 4096,
				       .cmh_dev = {.md_type = D_MEM_TYPE_CUDA, .md_device_id = 1}};
	daos_mem_attr_t        attr = {.ma_mem_type  = DAOS_MEM_TYPE_CUDA,
				       .ma_device_id = 1,
				       .ma_handle    = (daos_mem_handle_t)&mh};
	daos_mem_attr_t *dup = NULL;
	d_iov_t          iovs[2];
	d_sg_list_t      sgl = {.sg_nr = 2, .sg_iovs = iovs};

	atomic_init(&mh.cmh_refs, 1);

	/* scattered blocks inside the region, the last one touching its end */
	d_iov_set(&iovs[0], pool + 128, 256);
	d_iov_set(&iovs[1], pool + 4096 - 64, 64);
	assert_rc_equal(dc_obj_mem_attrs_valid(&attr, &sgl, 1), 0);

	/* outside, past the end, and a fetch buffer larger than its length */
	d_iov_set(&iovs[1], pool + 4096 - 64, 65);
	assert_rc_equal(dc_obj_mem_attrs_valid(&attr, &sgl, 1), -DER_INVAL);
	d_iov_set(&iovs[1], pool - 1, 8);
	assert_rc_equal(dc_obj_mem_attrs_valid(&attr, &sgl, 1), -DER_INVAL);
	d_iov_set(&iovs[1], pool + 100, 8);
	iovs[1].iov_len     = 0;
	iovs[1].iov_buf_len = 4096;
	assert_rc_equal(dc_obj_mem_attrs_valid(&attr, &sgl, 1), -DER_INVAL);
	d_iov_set(&iovs[1], pool + 100, 8);

	/* memory type and device must not conflict with the handle */
	attr.ma_device_id = 2;
	assert_rc_equal(dc_obj_mem_attrs_valid(&attr, &sgl, 1), -DER_INVAL);
	attr.ma_device_id = 1;

	/* in-flight copies hold a reference */
	assert_rc_equal(dc_obj_mem_attrs_dup(&attr, 1, &dup), 0);
	assert_ptr_equal(dup[0].ma_handle, (daos_mem_handle_t)&mh);
	assert_int_equal(atomic_load(&mh.cmh_refs), 2);
	dc_obj_mem_attrs_free(dup, 1);
	assert_int_equal(atomic_load(&mh.cmh_refs), 1);
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
	    cmocka_unit_test(mem_attrs_valid),
	    cmocka_unit_test(mem_attrs_flags),
	    cmocka_unit_test(mem_attrs_dup),
	    cmocka_unit_test(mem_attrs_handle),
	};
	int rc;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0)
		return rc;

	rc = cmocka_run_group_tests_name("DAOS client mem attr tests", tests, NULL, NULL);
	daos_debug_fini();
	return rc;
}
