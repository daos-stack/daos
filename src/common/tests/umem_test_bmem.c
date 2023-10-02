/**
 * (C) Copyright 2019-2023 Intel Corporation.
 * (C) Copyright 2023 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <uuid/uuid.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include <daos/mem.h>
#include <daos/tests_lib.h>
#include "utest_common.h"

#define POOL_SIZE ((1024 * 1024  * 1024ULL))

struct test_arg {
	struct utest_context	*ta_utx;
	uint64_t		*ta_root;
	char			*ta_pool_name;
};

uint64_t persist_reserv_cnt;
uint64_t persist_submit_cnt;

uint64_t persist_reserv_snap;
uint64_t persist_submit_snap;

static inline void
snap_persist_activity()
{
	persist_reserv_snap = persist_reserv_cnt;
	persist_submit_snap = persist_submit_cnt;
}

static inline void
validate_persist_activity(uint64_t persist_reserv_incr, uint64_t persist_submit_incr)
{
	assert_true(persist_reserv_cnt == (persist_reserv_snap + persist_reserv_incr));
	assert_true(persist_submit_cnt == (persist_submit_snap + persist_submit_incr));
}

static int _persist_reserv(struct umem_store *store, uint64_t *id)
{
	persist_reserv_cnt++;
	return 0;
}

static int _persist_submit(struct umem_store *store, struct umem_wal_tx *wal_tx, void *data_iod)
{
	persist_submit_cnt++;
	return 0;
}

struct umem_store_ops _store_ops = {
	.so_wal_reserv = _persist_reserv,
	.so_wal_submit = _persist_submit,
};

struct umem_store ustore = { .stor_size = POOL_SIZE, .stor_ops = &_store_ops,
			     .store_type = DAOS_MD_BMEM };

int
teardown_pmem(void **state)
{
	struct test_arg *arg = *state;
	int		 rc;

	if (arg == NULL) {
		print_message("state not set, likely due to group-setup issue\n");
		return 0;
	}

	rc = utest_utx_destroy(arg->ta_utx);

	D_FREE(arg->ta_pool_name);

	return rc;
}

int
setup_pmem(void **state)
{
	struct test_arg		*arg = *state;
	static int		 tnum;
	int			 rc = 0;

	D_ASPRINTF(arg->ta_pool_name, "/mnt/daos/umem-test-%d", tnum++);
	if (arg->ta_pool_name == NULL) {
		print_message("Failed to allocate test struct\n");
		return 1;
	}

	rc = utest_pmem_create(arg->ta_pool_name, POOL_SIZE,
			       sizeof(*arg->ta_root), &ustore, &arg->ta_utx);
	if (rc != 0) {
		perror("Could not create pmem context");
		rc = 1;
		goto failed;
	}

	arg->ta_root = utest_utx2root(arg->ta_utx);

	return 0;
failed:
	D_FREE(arg->ta_pool_name);
	return rc;
}

static int
global_setup(void **state)
{
	struct test_arg	*arg;

	if (umempobj_settings_init(true) != 0) {
		print_message("Failed to set the md_on_ssd tunable\n");
		return 1;
	}

	D_ALLOC_PTR(arg);
	if (arg == NULL) {
		print_message("Failed to allocate test struct\n");
		return 1;
	}

	*state = arg;

	return 0;
}

static int
global_teardown(void **state)
{
	struct test_arg	*arg = *state;

	D_FREE(arg);

	return 0;
}

static void
test_atomic_alloc(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	uint64_t		 off, size, off_arr[16];
	int			 i, rc;
	uint64_t		 initial_mem_used, cur_mem_used;
	uint64_t		 total_size = 0;

	utest_get_scm_used_space(arg->ta_utx, &initial_mem_used);
	snap_persist_activity();
	off = umem_atomic_alloc(umm, 1024, UMEM_TYPE_ANY);
	assert_false(UMOFF_IS_NULL(off));
	validate_persist_activity(1, 1);

	rc = umem_atomic_free(umm, off);
	assert_int_equal(rc, 0);
	validate_persist_activity(2, 2);
	utest_get_scm_used_space(arg->ta_utx, &cur_mem_used);
	assert_true(cur_mem_used == initial_mem_used);

	/* Negative test: Incorrect size test */
	snap_persist_activity();
	off = umem_atomic_alloc(umm, 0, UMEM_TYPE_ANY);
	assert_true(UMOFF_IS_NULL(off));
	validate_persist_activity(0, 0);

	/* Validate allocation of various sizes */
	snap_persist_activity();
	for (i = 1; i < 16; i++) {
		size = (1ul<<i) - 1;
		total_size += size;
		off_arr[i] = umem_atomic_alloc(umm, size, UMEM_TYPE_ANY);
		assert_false(UMOFF_IS_NULL(off_arr[i]));
	}
	validate_persist_activity(15, 15);
	utest_get_scm_used_space(arg->ta_utx, &cur_mem_used);
	assert_true(cur_mem_used >= initial_mem_used+total_size);

	snap_persist_activity();
	for (i = 15; i > 0; i--) {
		rc = umem_atomic_free(umm, off_arr[i]);
		assert_int_equal(rc, 0);
	}
	validate_persist_activity(15, 15);
	utest_get_scm_used_space(arg->ta_utx, &cur_mem_used);
	assert_true(cur_mem_used == initial_mem_used);
}

static void
test_atomic_copy(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	umem_off_t		 off;
	int			 rc;
	char			*ptr;
	char			 local_buf[2048];
	char			*dest;

	off = umem_atomic_alloc(umm, 2048, UMEM_TYPE_ANY);
	assert_false(UMOFF_IS_NULL(off));

	memset(local_buf, 'a', 2048);
	ptr = umem_off2ptr(umm, off);
	snap_persist_activity();
	dest = umem_atomic_copy(umm, ptr, local_buf, 2048, UMEM_COMMIT_IMMEDIATE);
	assert_true(dest == ptr);
	validate_persist_activity(1, 1);

	memset(local_buf+256, 'b', 256);
	ptr = umem_off2ptr(umm, off+256);
	snap_persist_activity();
	dest = umem_atomic_copy(umm, ptr, local_buf+256, 256, UMEM_RESERVED_MEM);
	assert_true(dest == ptr);
	validate_persist_activity(0, 0);

	memset(local_buf+512, 'c', 256);
	ptr = umem_off2ptr(umm, off+512);
	snap_persist_activity();
	dest = umem_atomic_copy(umm, ptr, local_buf+512, 256, UMEM_COMMIT_IMMEDIATE);
	assert_true(dest == ptr);
	validate_persist_activity(1, 1);

	memset(local_buf+768, 'd', 256);
	ptr = umem_off2ptr(umm, off+768);
	snap_persist_activity();
	dest = umem_atomic_copy(umm, ptr, local_buf+768, 256, UMEM_COMMIT_DEFER);
	assert_true(dest == ptr);
	validate_persist_activity(0, 0);

	memset(local_buf+1024, 'e', 256);
	ptr = umem_off2ptr(umm, off+1024);
	snap_persist_activity();
	dest = umem_atomic_copy(umm, ptr, local_buf+1024, 256, UMEM_COMMIT_IMMEDIATE);
	assert_true(dest == ptr);
	validate_persist_activity(1, 1);

	ptr = umem_off2ptr(umm, off);
	rc = memcmp(local_buf, ptr, 2048);
	assert_int_equal(rc, 0);

	/* atomic copy with defer commit should persist even if outer tx fails */
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	memset(local_buf+1280, 'f', 256);
	ptr = umem_off2ptr(umm, off+1280);
	dest = umem_atomic_copy(umm, ptr, local_buf+1280, 256, UMEM_COMMIT_DEFER);
	assert_true(dest == ptr);
	rc = umem_tx_abort(umm, 1);
	assert_int_not_equal(rc, 0);
	validate_persist_activity(1, 1);

	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	memset(local_buf+1280, 'f', 256);
	ptr = umem_off2ptr(umm, off+1280);
	rc = umem_tx_xadd_ptr(umm, ptr, 256, UMEM_XADD_NO_SNAPSHOT);
	assert_int_equal(rc, 0);
	dest = umem_atomic_copy(umm, ptr, local_buf+1280, 256, UMEM_COMMIT_DEFER);
	assert_true(dest == ptr);
	rc = umem_tx_abort(umm, 1);
	assert_int_not_equal(rc, 0);
	validate_persist_activity(1, 1);

	ptr = umem_off2ptr(umm, off);
	rc = memcmp(local_buf, ptr, 2048);
	assert_int_equal(rc, 0);

	rc = umem_atomic_free(umm, off);
	assert_int_equal(rc, 0);
}

struct cbdata {
	int abort_cb_noop;
	int abort_cb;
	int commit_cb_noop;
	int commit_cb;
	int end_cb_noop;
	int end_cb;
} gdata;

static void abort_cb(void *data, bool noop)
{
	struct cbdata *dptr = (struct cbdata *)data;

	noop ? dptr->abort_cb_noop++ : dptr->abort_cb++;
}

static void commit_cb(void *data, bool noop)
{
	struct cbdata *dptr = (struct cbdata *)data;

	noop ? dptr->commit_cb_noop++ : dptr->commit_cb++;
}

static void end_cb(void *data, bool noop)
{
	struct cbdata *dptr = (struct cbdata *)data;

	noop ? dptr->end_cb_noop++ : dptr->end_cb++;
}

static void
test_simple_commit_tx(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	int			 rc;
	umem_off_t		 off;

	struct umem_tx_stage_data txd;

	umem_init_txd(&txd);
	memset(&gdata, 0, sizeof(gdata));

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_NONE);
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_return_code(rc, 0);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	off = umem_alloc(umm, 128);
	assert_false(UMOFF_IS_NULL(off));

	rc = umem_tx_commit(umm);
	assert_return_code(rc, 0);
	validate_persist_activity(1, 1);
	assert_true(umem_tx_stage(umm) == UMEM_STAGE_NONE);

	snap_persist_activity();
	rc = umem_tx_begin(umm, &txd);
	assert_return_code(rc, 0);

	umem_tx_add_callback(umm, &txd, UMEM_STAGE_ONCOMMIT, commit_cb, &gdata);
	umem_tx_add_callback(umm, &txd, UMEM_STAGE_ONABORT, abort_cb, &gdata);
	umem_tx_add_callback(umm, &txd, UMEM_STAGE_NONE, end_cb, &gdata);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	rc = umem_free(umm, off);
	assert_return_code(rc, 0);

	rc = umem_tx_commit(umm);
	assert_return_code(rc, 0);
	validate_persist_activity(1, 1);
	assert_true(umem_tx_stage(umm) == UMEM_STAGE_NONE);

	assert_true((gdata.abort_cb == 0) && (gdata.abort_cb_noop == 1));
	assert_true((gdata.commit_cb == 1) && (gdata.commit_cb_noop == 0));
	assert_true((gdata.end_cb == 1) && (gdata.end_cb_noop == 0));

	umem_fini_txd(&txd);
}

static void
test_simple_abort_tx(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	int			 rc;
	umem_off_t		 off;
	char			*ptr;

	struct umem_tx_stage_data txd;

	umem_init_txd(&txd);
	memset(&gdata, 0, sizeof(gdata));

	rc = umem_tx_begin(umm, NULL);
	assert_return_code(rc, 0);
	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	off = umem_zalloc(umm, 128);
	assert_false(UMOFF_IS_NULL(off));
	ptr = umem_off2ptr(umm, off);
	strcpy(ptr, "0123456789");
	rc = umem_tx_commit(umm);
	assert_return_code(rc, 0);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_NONE);
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_return_code(rc, 0);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	rc = umem_tx_add(umm, off, 128);
	assert_return_code(rc, 0);
	ptr = umem_off2ptr(umm, off);
	memset(ptr, 'a', 128);

	rc = umem_tx_abort(umm, 1);
	assert_true(rc == umem_tx_errno(1));
	validate_persist_activity(1, 0);
	assert_true(umem_tx_stage(umm) == UMEM_STAGE_NONE);

	ptr = umem_off2ptr(umm, off);
	assert_true(strcmp(ptr, "0123456789") == 0);

	snap_persist_activity();
	rc = umem_tx_begin(umm, &txd);
	assert_return_code(rc, 0);

	umem_tx_add_callback(umm, &txd, UMEM_STAGE_ONCOMMIT, commit_cb, &gdata);
	umem_tx_add_callback(umm, &txd, UMEM_STAGE_ONABORT, abort_cb, &gdata);
	umem_tx_add_callback(umm, &txd, UMEM_STAGE_NONE, end_cb, &gdata);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	rc = umem_tx_add(umm, off, 128);
	assert_return_code(rc, 0);
	ptr = umem_off2ptr(umm, off);
	memset(ptr, 'a', 128);

	rc = umem_tx_abort(umm, 2);
	assert_true(rc == umem_tx_errno(2));
	validate_persist_activity(1, 0);
	assert_true(umem_tx_stage(umm) == UMEM_STAGE_NONE);

	assert_true((gdata.abort_cb == 1) && (gdata.abort_cb_noop == 0));
	assert_true((gdata.commit_cb == 0) && (gdata.commit_cb_noop == 1));
	assert_true((gdata.end_cb == 1) && (gdata.end_cb_noop == 0));

	ptr = umem_off2ptr(umm, off);
	assert_true(strcmp(ptr, "0123456789") == 0);
	umem_fini_txd(&txd);
}

static void
test_nested_commit_tx(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	int			 rc;
	umem_off_t		 off1, off2;

	struct umem_tx_stage_data txd;

	umem_init_txd(&txd);
	memset(&gdata, 0, sizeof(gdata));

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_NONE);
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_return_code(rc, 0);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	off1 = umem_alloc(umm, 128);
	assert_false(UMOFF_IS_NULL(off1));

	/* Inner Tx start */
	rc = umem_tx_begin(umm, NULL);
	assert_return_code(rc, 0);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	off2 = umem_alloc(umm, 256);
	assert_false(UMOFF_IS_NULL(off2));

	rc = umem_tx_commit(umm);
	assert_return_code(rc, 0);
	validate_persist_activity(1, 0);
	/* Inner Tx end */

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);

	rc = umem_tx_commit(umm);
	assert_return_code(rc, 0);
	validate_persist_activity(1, 1);
	assert_true(umem_tx_stage(umm) == UMEM_STAGE_NONE);

	snap_persist_activity();
	rc = umem_tx_begin(umm, &txd);
	assert_return_code(rc, 0);

	umem_tx_add_callback(umm, &txd, UMEM_STAGE_ONCOMMIT, commit_cb, &gdata);
	umem_tx_add_callback(umm, &txd, UMEM_STAGE_ONABORT, abort_cb, &gdata);
	umem_tx_add_callback(umm, &txd, UMEM_STAGE_NONE, end_cb, &gdata);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	rc = umem_free(umm, off1);
	assert_return_code(rc, 0);

	/* Inner Tx start */
	rc = umem_tx_begin(umm, NULL);
	assert_return_code(rc, 0);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	rc = umem_free(umm, off2);
	assert_return_code(rc, 0);

	rc = umem_tx_commit(umm);
	assert_return_code(rc, 0);
	validate_persist_activity(1, 0);
	/* Inner Tx end */

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);

	rc = umem_tx_commit(umm);
	assert_return_code(rc, 0);
	validate_persist_activity(1, 1);
	assert_true(umem_tx_stage(umm) == UMEM_STAGE_NONE);

	assert_true((gdata.abort_cb == 0) && (gdata.abort_cb_noop == 1));
	assert_true((gdata.commit_cb == 1) && (gdata.commit_cb_noop == 0));
	assert_true((gdata.end_cb == 1) && (gdata.end_cb_noop == 0));
	umem_fini_txd(&txd);
}

static void
test_nested_outer_abort_tx(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	int			 rc;
	umem_off_t		 off1, off2;
	char			*ptr1, *ptr2;

	struct umem_tx_stage_data txd;

	umem_init_txd(&txd);
	memset(&gdata, 0, sizeof(gdata));

	rc = umem_tx_begin(umm, NULL);
	assert_return_code(rc, 0);
	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	off1 = umem_zalloc(umm, 128);
	assert_false(UMOFF_IS_NULL(off1));
	ptr1 = umem_off2ptr(umm, off1);
	strcpy(ptr1, "0123456789");
	off2 = umem_zalloc(umm, 256);
	assert_false(UMOFF_IS_NULL(off2));
	ptr2 = umem_off2ptr(umm, off2);
	strcpy(ptr2, "ABCDEFGHIJ");
	rc = umem_tx_commit(umm);
	assert_return_code(rc, 0);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_NONE);
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_return_code(rc, 0);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	rc = umem_tx_add(umm, off1, 128);
	assert_return_code(rc, 0);
	ptr1 = umem_off2ptr(umm, off1);
	memset(ptr1, 'a', 128);

	/* Inner Tx Start */
	rc = umem_tx_begin(umm, NULL);
	assert_return_code(rc, 0);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	rc = umem_tx_add(umm, off2, 128);
	assert_return_code(rc, 0);
	ptr2 = umem_off2ptr(umm, off2);
	memset(ptr2, '0', 128);

	rc = umem_tx_commit(umm);
	assert_return_code(rc, 0);
	/* Inner Tx End */

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	rc = umem_tx_abort(umm, 1);
	assert_true(rc == umem_tx_errno(1));
	validate_persist_activity(1, 0);
	assert_true(umem_tx_stage(umm) == UMEM_STAGE_NONE);

	ptr1 = umem_off2ptr(umm, off1);
	assert_true(strcmp(ptr1, "0123456789") == 0);
	ptr2 = umem_off2ptr(umm, off2);
	assert_true(strcmp(ptr2, "ABCDEFGHIJ") == 0);

	snap_persist_activity();
	rc = umem_tx_begin(umm, &txd);
	assert_return_code(rc, 0);

	umem_tx_add_callback(umm, &txd, UMEM_STAGE_ONCOMMIT, commit_cb, &gdata);
	umem_tx_add_callback(umm, &txd, UMEM_STAGE_ONABORT, abort_cb, &gdata);
	umem_tx_add_callback(umm, &txd, UMEM_STAGE_NONE, end_cb, &gdata);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	rc = umem_tx_add(umm, off1, 128);
	assert_return_code(rc, 0);
	ptr1 = umem_off2ptr(umm, off1);
	memset(ptr1, 'a', 128);

	/* Inner Tx Start */
	rc = umem_tx_begin(umm, NULL);
	assert_return_code(rc, 0);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	rc = umem_tx_add(umm, off2, 128);
	assert_return_code(rc, 0);
	ptr2 = umem_off2ptr(umm, off2);
	memset(ptr2, '0', 128);

	rc = umem_tx_commit(umm);
	assert_return_code(rc, 0);
	/* Inner Tx End */

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	rc = umem_tx_abort(umm, 2);
	assert_true(rc == umem_tx_errno(2));
	validate_persist_activity(1, 0);
	assert_true(umem_tx_stage(umm) == UMEM_STAGE_NONE);

	assert_true((gdata.abort_cb == 1) && (gdata.abort_cb_noop == 0));
	assert_true((gdata.commit_cb == 0) && (gdata.commit_cb_noop == 1));
	assert_true((gdata.end_cb == 1) && (gdata.end_cb_noop == 0));

	ptr1 = umem_off2ptr(umm, off1);
	assert_true(strcmp(ptr1, "0123456789") == 0);
	ptr2 = umem_off2ptr(umm, off2);
	assert_true(strcmp(ptr2, "ABCDEFGHIJ") == 0);
	umem_fini_txd(&txd);
}

static void
test_nested_inner_abort_tx(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	int			 rc;
	umem_off_t		 off1, off2;
	char			*ptr1, *ptr2;

	struct umem_tx_stage_data txd;

	umem_init_txd(&txd);
	memset(&gdata, 0, sizeof(gdata));

	rc = umem_tx_begin(umm, NULL);
	assert_return_code(rc, 0);
	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	off1 = umem_zalloc(umm, 128);
	assert_false(UMOFF_IS_NULL(off1));
	ptr1 = umem_off2ptr(umm, off1);
	strcpy(ptr1, "0123456789");
	off2 = umem_zalloc(umm, 256);
	assert_false(UMOFF_IS_NULL(off2));
	ptr2 = umem_off2ptr(umm, off2);
	strcpy(ptr2, "ABCDEFGHIJ");
	rc = umem_tx_commit(umm);
	assert_return_code(rc, 0);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_NONE);
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_return_code(rc, 0);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	rc = umem_tx_add(umm, off1, 128);
	assert_return_code(rc, 0);
	ptr1 = umem_off2ptr(umm, off1);
	memset(ptr1, 'a', 128);

	/* Inner Tx Start */
	rc = umem_tx_begin(umm, NULL);
	assert_return_code(rc, 0);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	rc = umem_tx_add(umm, off2, 128);
	assert_return_code(rc, 0);
	ptr2 = umem_off2ptr(umm, off2);
	memset(ptr2, '0', 128);

	rc = umem_tx_abort(umm, 1);
	assert_true(rc == umem_tx_errno(1));
	/* Inner Tx End */

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_ONABORT);
	expect_assert_failure(umem_tx_begin(umm, NULL));
	expect_assert_failure(umem_tx_commit(umm));

	rc = umem_tx_end(umm, 1);
	assert_true(rc == umem_tx_errno(1));
	validate_persist_activity(1, 0);
	assert_true(umem_tx_stage(umm) == UMEM_STAGE_NONE);

	ptr1 = umem_off2ptr(umm, off1);
	assert_true(strcmp(ptr1, "0123456789") == 0);
	ptr2 = umem_off2ptr(umm, off2);
	assert_true(strcmp(ptr2, "ABCDEFGHIJ") == 0);

	snap_persist_activity();
	rc = umem_tx_begin(umm, &txd);
	assert_return_code(rc, 0);

	umem_tx_add_callback(umm, &txd, UMEM_STAGE_ONCOMMIT, commit_cb, &gdata);
	umem_tx_add_callback(umm, &txd, UMEM_STAGE_ONABORT, abort_cb, &gdata);
	umem_tx_add_callback(umm, &txd, UMEM_STAGE_NONE, end_cb, &gdata);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	rc = umem_tx_add(umm, off1, 128);
	assert_return_code(rc, 0);
	ptr1 = umem_off2ptr(umm, off1);
	memset(ptr1, 'a', 128);

	/* Inner Tx Start */
	rc = umem_tx_begin(umm, NULL);
	assert_return_code(rc, 0);

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_WORK);
	rc = umem_tx_add(umm, off2, 128);
	assert_return_code(rc, 0);
	ptr2 = umem_off2ptr(umm, off2);
	memset(ptr2, '0', 128);

	rc = umem_tx_abort(umm, 2);
	assert_true(rc == umem_tx_errno(2));
	/* Inner Tx End */

	assert_true(umem_tx_stage(umm) == UMEM_STAGE_ONABORT);
	rc = umem_tx_end(umm, 2);
	assert_true(rc == umem_tx_errno(2));
	validate_persist_activity(1, 0);
	assert_true(umem_tx_stage(umm) == UMEM_STAGE_NONE);

	assert_true((gdata.abort_cb == 1) && (gdata.abort_cb_noop == 0));
	assert_true((gdata.commit_cb == 0) && (gdata.commit_cb_noop == 1));
	assert_true((gdata.end_cb == 1) && (gdata.end_cb_noop == 0));

	ptr1 = umem_off2ptr(umm, off1);
	assert_true(strcmp(ptr1, "0123456789") == 0);
	ptr2 = umem_off2ptr(umm, off2);
	assert_true(strcmp(ptr2, "ABCDEFGHIJ") == 0);
	umem_fini_txd(&txd);
}

static void
test_invalid_flags(void **state)
{
	struct test_arg		*arg = *state;
	uint32_t		*value1;
	uint32_t		*value2;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	umem_off_t		 umoff = UMOFF_NULL;
	uint64_t		 offset;
	int			 i;

	assert_true(UMOFF_IS_NULL(umoff));

	assert_int_equal(umem_off2flags(umoff), 0);

	for (i = 0; i < UMOFF_MAX_FLAG; i++) {
		umem_off_set_null_flags(&umoff, i);
		assert_int_equal(umem_off2flags(umoff), i);
		assert_true(UMOFF_IS_NULL(umoff));
	}

	umoff = UMOFF_NULL;
	assert_int_equal(umem_off2flags(umoff), 0);

	assert_int_equal(utest_alloc(arg->ta_utx, &umoff, sizeof(*value1), NULL,
				     NULL), 0);
	assert_int_equal(umem_off2flags(umoff), 0);

	offset = umem_off2offset(umoff);
	value1 = umem_off2ptr(umm, umoff);
	assert_ptr_not_equal(value1, NULL);

	*value1 = 0xdeadbeef;
	assert_int_equal(*value1, 0xdeadbeef);

	for (i = 0; i < UMOFF_MAX_FLAG; i++) {
		umem_off_set_flags(&umoff, i);
		assert_int_equal(umem_off2flags(umoff), i);
		assert_false(UMOFF_IS_NULL(umoff));
		assert_int_equal(umem_off2offset(umoff), offset);
	}
	assert_int_equal(*value1, 0xdeadbeef);

	value2 = umem_off2ptr(umm, umoff);
	assert_ptr_equal(value1, value2);

	assert_int_equal(*value2, 0xdeadbeef);

	/* Even with flags set, the offset should be valid */
	assert_int_equal(utest_free(arg->ta_utx, umoff), 0);
}

static void
test_alloc(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	int			*value1;
	umem_off_t		 umoff = 0;
	int			 rc;

	rc = utest_tx_begin(arg->ta_utx);
	if (rc != 0)
		goto done;

	umoff = umem_zalloc(umm, 4);
	if (UMOFF_IS_NULL(umoff)) {
		print_message("umoff unexpectedly NULL\n");
		rc = 1;
		goto end;
	}

	value1 = umem_off2ptr(umm, umoff);

	if (*value1 != 0) {
		print_message("Bad value for allocated umoff\n");
		rc = 1;
		goto end;
	}

	rc = umem_free(umm, umoff);
end:
	rc = utest_tx_end(arg->ta_utx, rc);
done:
	assert_int_equal(rc, 0);
}

static void
test_tx_alloc(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	int			 rc;
	daos_size_t		 allotted_size = 0;
	uint64_t		 initial_mem_used, cur_mem_used;
	int			*value1, *value2;
	umem_off_t		 umoff1 = 0, umoff2 = 0;

	/* Test umem_zalloc */
	snap_persist_activity();
	utest_get_scm_used_space(arg->ta_utx, &initial_mem_used);
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);

	umoff1 = umem_zalloc(umm, 4);
	assert_false(UMOFF_IS_NULL(umoff1));
	allotted_size += 4;

	value1 = umem_off2ptr(umm, umoff1);

	assert_true(*value1 == 0);

	rc = umem_tx_commit(umm);
	assert_int_equal(rc, 0);
	validate_persist_activity(1, 1);
	utest_get_scm_used_space(arg->ta_utx, &cur_mem_used);
	assert_true(cur_mem_used >= (initial_mem_used + allotted_size));

	/* Test umem_alloc */
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);

	umoff2 = umem_alloc(umm, 4);
	allotted_size += 4;
	assert_false(UMOFF_IS_NULL(umoff2));

	value2 = umem_off2ptr(umm, umoff2);
	*value2 = 100;

	rc = umem_tx_commit(umm);
	assert_int_equal(rc, 0);

	validate_persist_activity(1, 1);
	utest_get_scm_used_space(arg->ta_utx, &cur_mem_used);
	assert_true(cur_mem_used >= (initial_mem_used + allotted_size));

	/* Test umem_free */
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);

	rc = umem_free(umm, umoff2);
	assert_int_equal(rc, 0);
	allotted_size -= 4;

	rc = umem_free(umm, umoff1);
	assert_int_equal(rc, 0);
	allotted_size -= 4;

	rc = umem_tx_commit(umm);
	assert_int_equal(rc, 0);
	validate_persist_activity(1, 1);
	utest_get_scm_used_space(arg->ta_utx, &cur_mem_used);
	assert_true(allotted_size == 0);
	assert_true(cur_mem_used == initial_mem_used);

	/* Negative Tests */
	/* Outside of TX */
	expect_assert_failure(umem_alloc(umm, 100));
	expect_assert_failure(umem_zalloc(umm, 100));

	/* alloc of size zero */
	snap_persist_activity();
	utest_get_scm_used_space(arg->ta_utx, &initial_mem_used);
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	umoff1 = umem_alloc(umm, 0);
	assert_true(UMOFF_IS_NULL(umoff1));
	assert_true(umem_tx_stage(umm) == UMEM_STAGE_ONABORT);
	rc = umem_tx_end(umm, 1);
	assert_false(rc == 0);
	validate_persist_activity(1, 0);
	utest_get_scm_used_space(arg->ta_utx, &cur_mem_used);
	assert_true(initial_mem_used == cur_mem_used);

	snap_persist_activity();
	utest_get_scm_used_space(arg->ta_utx, &initial_mem_used);
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	umoff1 = umem_zalloc(umm, 0);
	assert_true(UMOFF_IS_NULL(umoff1));
	assert_true(umem_tx_stage(umm) == UMEM_STAGE_ONABORT);
	rc = umem_tx_end(umm, 1);
	assert_false(rc == 0);
	validate_persist_activity(1, 0);
	utest_get_scm_used_space(arg->ta_utx, &cur_mem_used);
	assert_true(initial_mem_used == cur_mem_used);

	/* free outside of tx */
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	umoff1 = umem_zalloc(umm, 4);
	assert_false(UMOFF_IS_NULL(umoff1));
	rc = umem_tx_end(umm, 0);
	assert_int_equal(rc, 0);
	expect_assert_failure(umem_free(umm, umoff1));

	/* abort after alloc and used memory should not increase */
	snap_persist_activity();
	utest_get_scm_used_space(arg->ta_utx, &initial_mem_used);
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	umoff1 = umem_alloc(umm, 16);
	assert_false(UMOFF_IS_NULL(umoff1));
	umoff1 = umem_zalloc(umm, 32);
	assert_false(UMOFF_IS_NULL(umoff2));
	rc = umem_tx_abort(umm, 1);
	assert_false(rc == 0);
	validate_persist_activity(1, 0);
	utest_get_scm_used_space(arg->ta_utx, &cur_mem_used);
	assert_true(initial_mem_used == cur_mem_used);

}

static void
test_tx_add(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	int			 rc;
	umem_off_t		 umoff;
	char			*start_ptr, *tmp_ptr;
	char			 local_buf[2048];

	/* Setup */
	umoff = umem_atomic_alloc(umm, 2048, UMEM_TYPE_ANY);
	assert_false(UMOFF_IS_NULL(umoff));
	start_ptr = umem_off2ptr(umm, umoff);
	memset(local_buf, 0, 2048);
	tmp_ptr = umem_atomic_copy(umm, start_ptr, local_buf, 2048, UMEM_COMMIT_IMMEDIATE);
	assert_true(tmp_ptr == start_ptr);

	/* Negative tests */
	expect_assert_failure(umem_tx_add(umm, umoff, 128));

	/* Normal operation */
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	rc = umem_tx_add(umm, umoff, 128);
	assert_int_equal(rc, 0);
	start_ptr = umem_off2ptr(umm, umoff);
	memset(start_ptr, 'a', 128);
	memset(local_buf, 'a', 128);
	rc = umem_tx_end(umm, 0);
	assert_int_equal(rc, 0);
	validate_persist_activity(1, 1);
	assert_false(strncmp(local_buf, start_ptr, 128));

	/* Abort a transaction after tx add */
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	rc = umem_tx_add(umm, umoff+128, 128);
	assert_int_equal(rc, 0);
	tmp_ptr = umem_off2ptr(umm, umoff+128);
	memset(tmp_ptr, 'b', 128);
	rc = umem_tx_abort(umm, 1);
	assert_true(rc != 0);
	validate_persist_activity(1, 0);
	assert_false(strncmp(local_buf, start_ptr, 256));

	/* Invalid offset */
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	rc = umem_tx_add(umm, POOL_SIZE+4096, 128);
	assert_true(rc != 0);
	assert_true(umem_tx_stage(umm) == UMEM_STAGE_ONABORT);
	rc = umem_tx_end(umm, rc);
	assert_true(rc != 0);
	validate_persist_activity(1, 0);
}

static void
test_tx_add_ptr(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	int			 rc;
	umem_off_t		 umoff;
	char			*start_ptr, *tmp_ptr;
	char			 local_buf[2048];

	/* Setup */
	umoff = umem_atomic_alloc(umm, 2048, UMEM_TYPE_ANY);
	assert_false(UMOFF_IS_NULL(umoff));
	start_ptr = umem_off2ptr(umm, umoff);
	memset(local_buf, 0, 2048);
	tmp_ptr = umem_atomic_copy(umm, start_ptr, local_buf, 2048, UMEM_COMMIT_IMMEDIATE);
	assert_true(tmp_ptr == start_ptr);

	/* Negative tests */
	expect_assert_failure(umem_tx_add_ptr(umm, start_ptr, 128));

	/* Normal operation */
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	start_ptr = umem_off2ptr(umm, umoff);
	rc = umem_tx_add_ptr(umm, start_ptr, 128);
	assert_int_equal(rc, 0);
	memset(start_ptr, 'a', 128);
	memset(local_buf, 'a', 128);
	rc = umem_tx_end(umm, 0);
	assert_int_equal(rc, 0);
	validate_persist_activity(1, 1);
	assert_false(strncmp(local_buf, start_ptr, 128));

	/* Abort a transaction after tx add */
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	tmp_ptr = umem_off2ptr(umm, umoff+128);
	rc = umem_tx_add_ptr(umm, tmp_ptr, 128);
	assert_int_equal(rc, 0);
	memset(tmp_ptr, 'b', 128);
	rc = umem_tx_abort(umm, 1);
	assert_true(rc != 0);
	validate_persist_activity(1, 0);
	assert_false(strncmp(local_buf, start_ptr, 256));

	/* Invalid pointer */
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	rc = umem_tx_add_ptr(umm, local_buf, 128);
	assert_true(rc != 0);
	assert_true(umem_tx_stage(umm) == UMEM_STAGE_ONABORT);
	rc = umem_tx_end(umm, rc);
	assert_true(rc != 0);
	validate_persist_activity(1, 0);
}

static void
test_tx_xadd_ptr(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	int			 rc;
	umem_off_t		 umoff;
	char			*start_ptr, *tmp_ptr;
	char			 local_buf[2048];

	/* Setup */
	umoff = umem_atomic_alloc(umm, 2048, UMEM_TYPE_ANY);
	assert_false(UMOFF_IS_NULL(umoff));
	start_ptr = umem_off2ptr(umm, umoff);
	memset(local_buf, 0, 2048);
	tmp_ptr = umem_atomic_copy(umm, start_ptr, local_buf, 2048, UMEM_COMMIT_IMMEDIATE);
	assert_true(tmp_ptr == start_ptr);

	/* Negative tests */
	expect_assert_failure(umem_tx_xadd_ptr(umm, start_ptr, 128, UMEM_XADD_NO_SNAPSHOT));

	/* Normal operation */
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	start_ptr = umem_off2ptr(umm, umoff);
	rc = umem_tx_xadd_ptr(umm, start_ptr, 128, UMEM_XADD_NO_SNAPSHOT);
	assert_int_equal(rc, 0);
	memset(start_ptr, 'a', 128);
	memset(local_buf, 'a', 128);
	rc = umem_tx_end(umm, 0);
	assert_int_equal(rc, 0);
	validate_persist_activity(1, 1);
	assert_false(strncmp(local_buf, start_ptr, 128));

	/* Abort a transaction after tx add */
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	tmp_ptr = umem_off2ptr(umm, umoff+128);
	rc = umem_tx_xadd_ptr(umm, tmp_ptr, 128, UMEM_XADD_NO_SNAPSHOT);
	assert_int_equal(rc, 0);
	memset(tmp_ptr, 'b', 128);
	memset(local_buf+128, 'b', 128);	/* No UNDO */
	tmp_ptr = umem_off2ptr(umm, umoff+256);
	rc = umem_tx_xadd_ptr(umm, tmp_ptr, 256, 0);	/* UNDO */
	assert_int_equal(rc, 0);
	memset(tmp_ptr, 'b', 256);
	rc = umem_tx_abort(umm, 1);
	assert_true(rc != 0);
	validate_persist_activity(1, 0);
	assert_false(strncmp(local_buf, start_ptr, 512));

	/* Invalid pointer */
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	rc = umem_tx_xadd_ptr(umm, local_buf, 128, UMEM_XADD_NO_SNAPSHOT);
	assert_true(rc != 0);
	assert_true(umem_tx_stage(umm) == UMEM_STAGE_ONABORT);
	rc = umem_tx_end(umm, rc);
	assert_true(rc != 0);
	validate_persist_activity(1, 0);
}

static void
test_tx_reserve_publish_cancel(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	int			 rc;
	struct umem_rsrvd_act	*rsrvd_act;
	umem_off_t		 umoff;
	char			*rsrv_ptr1, *rsrv_ptr2, *rsrv_ptr3, *rsrv_ptr4;
	char			*data = "Test Program test_tx_xadd_ptr";
	char			 local_buf[980];
	uint64_t		 initial_mem_used, cur_mem_used;
	uint64_t		 allotted_mem = 0;
	char			 addon_buf[128];

	/* Reserve/Publish */
	rc = umem_rsrvd_act_alloc(umm, &rsrvd_act, 2);
	assert_int_equal(rc, 0);
	umoff = umem_reserve(umm, rsrvd_act, 980);
	assert_false(UMOFF_IS_NULL(umoff));
	rsrv_ptr1 = umem_off2ptr(umm, umoff);
	memset(rsrv_ptr1, 0, 980);
	memset(local_buf, 0, 980);
	memcpy(rsrv_ptr1+128, data, strlen(data));
	memcpy(local_buf+128, data, strlen(data));

	umoff = umem_reserve(umm, rsrvd_act, 128);
	assert_false(UMOFF_IS_NULL(umoff));
	rsrv_ptr2 = umem_off2ptr(umm, umoff);
	memset(rsrv_ptr2, 0, 128);
	memset(addon_buf, 0, 128);
	memcpy(rsrv_ptr2, data, strlen(data));
	memcpy(addon_buf, data, strlen(data));


	utest_get_scm_used_space(arg->ta_utx, &initial_mem_used);
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	rc = umem_tx_add_ptr(umm, rsrv_ptr1, 128);
	assert_int_equal(rc, 0);
	strcpy(rsrv_ptr1, "header");
	strcpy(local_buf, "header");
	rc = umem_tx_publish(umm, rsrvd_act);
	assert_int_equal(rc, 0);
	allotted_mem = 980 + 128;
	rc = umem_tx_commit(umm);
	assert_int_equal(rc, 0);
	validate_persist_activity(1, 1);
	utest_get_scm_used_space(arg->ta_utx, &cur_mem_used);
	assert_true(cur_mem_used >= initial_mem_used + allotted_mem);
	assert_int_equal(memcmp(rsrv_ptr1, local_buf, 980), 0);
	assert_int_equal(memcmp(rsrv_ptr2, addon_buf, 128), 0);
	umem_rsrvd_act_free(&rsrvd_act);


	/* Reserve/Cancel */
	rc = umem_rsrvd_act_alloc(umm, &rsrvd_act, 2);
	assert_int_equal(rc, 0);
	umoff = umem_reserve(umm, rsrvd_act, 980);
	assert_false(UMOFF_IS_NULL(umoff));
	rsrv_ptr1 = umem_off2ptr(umm, umoff);
	memset(rsrv_ptr1, 1, 980);
	memset(local_buf, 1, 980);

	umoff = umem_reserve(umm, rsrvd_act, 128);
	assert_false(UMOFF_IS_NULL(umoff));
	rsrv_ptr2 = umem_off2ptr(umm, umoff);
	memset(rsrv_ptr2, 1, 128);


	utest_get_scm_used_space(arg->ta_utx, &initial_mem_used);
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	rc = umem_tx_add_ptr(umm, rsrv_ptr1, 128);
	assert_int_equal(rc, 0);
	strcpy(rsrv_ptr1, "header");
	rc = umem_tx_add_ptr(umm, rsrv_ptr2, 128);
	assert_int_equal(rc, 0);
	strcpy(rsrv_ptr2, "leader");
	rc = umem_tx_abort(umm, 1);
	assert_false(rc == 0);
	assert_int_equal(memcmp(rsrv_ptr1, local_buf, 980), 0);
	assert_int_equal(memcmp(rsrv_ptr2, local_buf, 128), 0);
	umem_cancel(umm, rsrvd_act);
	validate_persist_activity(1, 0);
	utest_get_scm_used_space(arg->ta_utx, &cur_mem_used);
	assert_true(cur_mem_used >= initial_mem_used);
	umoff = umem_atomic_alloc(umm, 980, UMEM_TYPE_ANY);
	assert_false(UMOFF_IS_NULL(umoff));
	rsrv_ptr3 = umem_off2ptr(umm, umoff);
	assert_ptr_equal(rsrv_ptr1, rsrv_ptr3);
	umoff = umem_atomic_alloc(umm, 128, UMEM_TYPE_ANY);
	assert_false(UMOFF_IS_NULL(umoff));
	rsrv_ptr4 = umem_off2ptr(umm, umoff);
	assert_ptr_equal(rsrv_ptr2, rsrv_ptr4);
	umem_rsrvd_act_free(&rsrvd_act);

	/* reserve - atomic_copy - cancel */
	rc = umem_rsrvd_act_alloc(umm, &rsrvd_act, 2);
	assert_int_equal(rc, 0);
	umoff = umem_reserve(umm, rsrvd_act, 980);
	assert_false(UMOFF_IS_NULL(umoff));
	rsrv_ptr1 = umem_off2ptr(umm, umoff);
	memset(local_buf, 1, 980);
	memcpy(local_buf+128, data, strlen(data));
	snap_persist_activity();
	umem_atomic_copy(umm, rsrv_ptr1, local_buf, 980, UMEM_COMMIT_IMMEDIATE);
	validate_persist_activity(1, 1);

	utest_get_scm_used_space(arg->ta_utx, &initial_mem_used);
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	rc = umem_tx_add_ptr(umm, rsrv_ptr1, 128);
	assert_int_equal(rc, 0);
	strcpy(rsrv_ptr1, "header");
	strcpy(local_buf, "header");
	rc = umem_tx_publish(umm, rsrvd_act);
	assert_int_equal(rc, 0);
	allotted_mem = 980;
	rc = umem_tx_commit(umm);
	assert_int_equal(rc, 0);
	validate_persist_activity(1, 1);
	utest_get_scm_used_space(arg->ta_utx, &cur_mem_used);
	assert_true(cur_mem_used >= initial_mem_used + allotted_mem);
	assert_int_equal(memcmp(rsrv_ptr1, local_buf, 980), 0);
	umem_rsrvd_act_free(&rsrvd_act);
}

static void
test_tx_dfree_publish_cancel(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	int			 rc;
	struct umem_rsrvd_act	*rsrvd_act;
	umem_off_t		 umoff1, umoff2;
	uint64_t		 freed_mem = 0;
	uint64_t		 initial_mem_used, cur_mem_used;

	/* Defer Free/Publish */
	umoff1 = umem_atomic_alloc(umm, 2048, UMEM_TYPE_ANY);
	assert_false(UMOFF_IS_NULL(umoff1));
	umoff2 = umem_atomic_alloc(umm, 1024, UMEM_TYPE_ANY);
	assert_false(UMOFF_IS_NULL(umoff2));

	rc = umem_rsrvd_act_alloc(umm, &rsrvd_act, 2);
	assert_int_equal(rc, 0);

	umem_defer_free(umm, umoff1, rsrvd_act);
	umem_defer_free(umm, umoff2, rsrvd_act);

	utest_get_scm_used_space(arg->ta_utx, &initial_mem_used);
	snap_persist_activity();
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	rc = umem_tx_publish(umm, rsrvd_act);
	assert_int_equal(rc, 0);
	freed_mem = 2048 + 1024;
	rc = umem_tx_commit(umm);
	assert_int_equal(rc, 0);
	validate_persist_activity(1, 1);
	utest_get_scm_used_space(arg->ta_utx, &cur_mem_used);
	assert_true(initial_mem_used >= cur_mem_used + freed_mem);
	umem_rsrvd_act_free(&rsrvd_act);


	/* Defer Free/Cancel */
	umoff1 = umem_atomic_alloc(umm, 2048, UMEM_TYPE_ANY);
	assert_false(UMOFF_IS_NULL(umoff1));
	umoff2 = umem_atomic_alloc(umm, 1024, UMEM_TYPE_ANY);
	assert_false(UMOFF_IS_NULL(umoff2));

	rc = umem_rsrvd_act_alloc(umm, &rsrvd_act, 2);
	assert_int_equal(rc, 0);

	umem_defer_free(umm, umoff1, rsrvd_act);
	umem_defer_free(umm, umoff2, rsrvd_act);

	utest_get_scm_used_space(arg->ta_utx, &initial_mem_used);
	umem_cancel(umm, rsrvd_act);
	utest_get_scm_used_space(arg->ta_utx, &cur_mem_used);
	assert_true(cur_mem_used >= initial_mem_used);
	umem_rsrvd_act_free(&rsrvd_act);
}

#if 0
/** This test is removed because the umempobj_set_slab_desc APIs are removed.  Testing the
 *  underlying dav or pmem APIs should probably be handled elsewhere.
 */
static void
test_tx_alloc_withslabs(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	struct umem_slab_desc	 slab[5];
	int			 rc, i;
	umem_off_t		 ummoff_exact1[5], ummoff_less[5], ummoff_exact2[5], ummoff_greater;
	size_t			 size_exact, size_less, size_greater;
	size_t			 initial_mem_used, cur_mem_used, total_allotted;

	/* Negative tests for allocation class */
	slab[0].unit_size = ULONG_MAX;
	slab[0].class_id = 0;
	rc = umempobj_set_slab_desc(umm->umm_pool, &slab[0]);
	assert_int_not_equal(rc, 0);
	slab[0].unit_size = 344;
	slab[0].class_id = UINT8_MAX;
	rc = umempobj_set_slab_desc(umm->umm_pool, &slab[0]);
	assert_int_not_equal(rc, 0);

	/* Valid slab creation */
	for (i = 0; i < 5; i++) {
		slab[i].unit_size = (1<<(i*2)) + 200 + i*16;
		slab[i].class_id = 0;
		rc = umempobj_set_slab_desc(umm->umm_pool, &slab[i]);
		assert_int_equal(rc, 0);
		assert_int_not_equal(slab[i].class_id, 0);

		umm->umm_slabs[i] = slab[i];
	}

	utest_get_scm_used_space(arg->ta_utx, &initial_mem_used);
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	total_allotted = 0;
	for (i = 0; i < 5; i++) {
		size_exact = (1<<(i*2)) + 200 + i*16;
		ummoff_exact1[i] = umem_alloc_verb(umm, i, UMEM_FLAG_ZERO, size_exact);
		assert_false(UMOFF_IS_NULL(ummoff_exact1[i]));
		size_less = 200;
		ummoff_less[i] = umem_alloc_verb(umm, i, UMEM_FLAG_ZERO, size_less);
		assert_false(UMOFF_IS_NULL(ummoff_less[i]));
		assert_true(ummoff_exact1[i] + size_exact == ummoff_less[i]);
		ummoff_exact2[i] = umem_alloc_verb(umm, i, UMEM_FLAG_ZERO, size_exact);
		assert_false(UMOFF_IS_NULL(ummoff_exact2[i]));
		assert_true(ummoff_less[i] + size_exact == ummoff_exact2[i]);
		total_allotted += size_exact*3;
	}
	rc = umem_tx_commit(umm);
	assert_int_equal(rc, 0);
	utest_get_scm_used_space(arg->ta_utx, &cur_mem_used);
	assert_true(initial_mem_used + total_allotted == cur_mem_used);

	for (i = 0; i < 5; i++) {
		size_greater = (1<<(i*2)) + 200 + i*16 + 100;
		rc = umem_tx_begin(umm, NULL);
		assert_int_equal(rc, 0);
		ummoff_greater = umem_alloc_verb(umm, i, UMEM_FLAG_ZERO, size_greater);
		assert_true(UMOFF_IS_NULL(ummoff_greater));
		rc = umem_tx_end(umm, 1);
		assert_int_equal(rc, umem_tx_errno(ENOMEM));
	}
}
#endif

int
main(int argc, char **argv)
{
	int rc;

	static const struct CMUnitTest umem_tests[] = {
		{ "BMEM001: Test atomic alloc/free", test_atomic_alloc,
			setup_pmem, teardown_pmem},
		{ "BMEM002: Test null flags pmem", test_invalid_flags,
			setup_pmem, teardown_pmem},
		{ "BMEM003: Test alloc pmem", test_alloc,
			setup_pmem, teardown_pmem},
		{ "BMEM004: Test atomic copy", test_atomic_copy,
			setup_pmem, teardown_pmem},
		{ "BMEM005: Test simple commit tx", test_simple_commit_tx,
			setup_pmem, teardown_pmem},
		{ "BMEM006: Test simple abort tx", test_simple_abort_tx,
			setup_pmem, teardown_pmem},
		{ "BMEM007: Test nested commit tx", test_nested_commit_tx,
			setup_pmem, teardown_pmem},
		{ "BMEM008: Test nested outer abort tx", test_nested_outer_abort_tx,
			setup_pmem, teardown_pmem},
		{ "BMEM009: Test nested inner abort tx", test_nested_inner_abort_tx,
			setup_pmem, teardown_pmem},
		{ "BMEM010: Test tx alloc/free", test_tx_alloc,
			setup_pmem, teardown_pmem},
		{ "BMEM011: Test tx add range", test_tx_add,
			setup_pmem, teardown_pmem},
		{ "BMEM012: Test tx add ptr", test_tx_add_ptr,
			setup_pmem, teardown_pmem},
		{ "BMEM013: Test tx xadd ptr", test_tx_xadd_ptr,
			setup_pmem, teardown_pmem},
		{ "BMEM014: Test tx reserve publish/cancel", test_tx_reserve_publish_cancel,
			setup_pmem, teardown_pmem},
		{ "BMEM015: Test tx defer free publish/cancel", test_tx_dfree_publish_cancel,
			setup_pmem, teardown_pmem},
		{ NULL, NULL, NULL, NULL }
	};

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0)
		return rc;


	d_register_alt_assert(mock_assert);

	rc = cmocka_run_group_tests_name("umem tests", umem_tests, global_setup, global_teardown);

	daos_debug_fini();
	return rc;
}
