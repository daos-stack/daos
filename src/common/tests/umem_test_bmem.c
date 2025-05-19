/**
 * (C) Copyright 2019-2024 Intel Corporation.
 * (C) Copyright 2023-2025 Hewlett Packard Enterprise Development LP.
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

#define POOL_SIZE  ((256 * 1024 * 1024ULL))
#define NEMB_RATIO (0.8)
#define MB_SIZE    (16 * 1024 * 1024)
#define MIN_SOEMB_CNT 3

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
	*id = persist_reserv_cnt++;
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

static int
waitqueue_create(void **wq)
{
	*wq = (void *)(UINT64_MAX);
	return 0;
}

static void
waitqueue_destroy(void *wq)
{
}

static void
waitqueue_wait(void *wq, bool yield_only)
{
}

static void
waitqueue_wakeup(void *wq, bool wakeup_all)
{
}

static int
store_load(struct umem_store *store, char *start_addr, daos_off_t offset, daos_size_t len)
{
	memset(start_addr, 0, len);
	D_ASSERTF(0, "Test is not suppose to do a store_load");
}

char store_buf[4096];

static int
store_read(struct umem_store *store, struct umem_store_iod *iod, d_sg_list_t *sgl)
{
	/* Fake Heap header read write */
	D_ASSERT(sgl->sg_iovs->iov_len <= 4096);
	memcpy(sgl->sg_iovs->iov_buf, store_buf, sgl->sg_iovs->iov_len);
	return 0;
}

static int
store_write(struct umem_store *store, struct umem_store_iod *iod, d_sg_list_t *sgl)
{
	/* Fake Heap header read write */
	D_ASSERT(sgl->sg_iovs->iov_len <= 4096);
	memcpy(store_buf, sgl->sg_iovs->iov_buf, sgl->sg_iovs->iov_len);
	return 0;
}

static int
store_flush_prep(struct umem_store *store, struct umem_store_iod *iod, daos_handle_t *fh)
{
	D_ASSERTF(0, "Test is not suppose to do a store_flush_prep");
	return 0;
}

static int
store_flush_copy(daos_handle_t fh, d_sg_list_t *sgl)
{
	D_ASSERTF(0, "Test is not suppose to do a store_flush_copy");
	return 0;
}

static int
store_flush_post(daos_handle_t fh, int err)
{
	D_ASSERTF(0, "Test is not suppose to do a store_flush_post");
	return 0;
}

static int
wal_id_cmp(struct umem_store *store, uint64_t id1, uint64_t id2)
{
	if (id1 > id2)
		return 1;
	if (id1 < id2)
		return -1;
	return 0;
}

static int
wal_replay(struct umem_store *store,
	   int (*replay_cb)(uint64_t tx_id, struct umem_action *act, void *arg), void *arg)
{
	D_ASSERTF(0, "Test is not suppose to do a store_flush_post");
	return 0;
}

struct umem_store_ops _store_ops_v2 = {
    .so_waitqueue_create  = waitqueue_create,
    .so_waitqueue_destroy = waitqueue_destroy,
    .so_waitqueue_wait    = waitqueue_wait,
    .so_waitqueue_wakeup  = waitqueue_wakeup,
    .so_load              = store_load,
    .so_read              = store_read,
    .so_write             = store_write,
    .so_flush_prep        = store_flush_prep,
    .so_flush_copy        = store_flush_copy,
    .so_flush_post        = store_flush_post,
    .so_wal_reserv        = _persist_reserv,
    .so_wal_submit        = _persist_submit,
    .so_wal_replay        = wal_replay,
    .so_wal_id_cmp        = wal_id_cmp,
};

struct umem_store ustore_v2 = {.stor_size  = POOL_SIZE * 3,
			       .stor_ops   = &_store_ops_v2,
			       .store_type = DAOS_MD_BMEM_V2,
			       .stor_priv  = (void *)(UINT64_MAX)};

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

static int
setup_pmem_internal(void **state, struct umem_store *store)
{
	struct test_arg		*arg = *state;
	static int		 tnum;
	int			 rc = 0;
	uint64_t                 pool_sz = POOL_SIZE;

	D_ASPRINTF(arg->ta_pool_name, "/mnt/daos/umem-test-%d", tnum++);
	if (arg->ta_pool_name == NULL) {
		print_message("Failed to allocate test struct\n");
		return 1;
	}

	if (store->store_type == DAOS_MD_BMEM_V2)
		pool_sz = POOL_SIZE * 2;
	rc = utest_pmem_create(arg->ta_pool_name, pool_sz, sizeof(*arg->ta_root), store,
			       &arg->ta_utx);
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
setup_pmem(void **state)
{
	return setup_pmem_internal(state, &ustore);
}

static int
setup_pmem_v2(void **state)
{
	struct test_arg      *arg;
	struct umem_instance *umm;
	int                   rc, i;

	rc = setup_pmem_internal(state, &ustore_v2);

	arg = *state;
	umm = utest_utx2umm(arg->ta_utx);
	/*
	 * Do soemb reservations before the test begins.
	 */
	if (!rc) {
		for (i = 0; i < MIN_SOEMB_CNT; i++) {
			umem_tx_begin(umm, NULL);
			umem_tx_commit(umm);
		}
	}
	return rc;
}

static int
global_setup(void **state)
{
	struct test_arg	*arg;

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
test_atomic_alloc_from_bucket(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	uint64_t		 off, size, off_arr[16];
	int			 i, rc;
	uint64_t		 initial_mem_used, cur_mem_used;
	uint64_t		 total_size = 0;

	utest_get_scm_used_space(arg->ta_utx, &initial_mem_used);
	snap_persist_activity();
	off = umem_atomic_alloc_from_bucket(umm, 1024, UMEM_TYPE_ANY, UMEM_DEFAULT_MBKT_ID);
	assert_false(UMOFF_IS_NULL(off));
	validate_persist_activity(1, 1);

	rc = umem_atomic_free(umm, off);
	assert_int_equal(rc, 0);
	validate_persist_activity(2, 2);
	utest_get_scm_used_space(arg->ta_utx, &cur_mem_used);
	assert_true(cur_mem_used == initial_mem_used);

	/* Negative test: Incorrect size test */
	snap_persist_activity();
	off = umem_atomic_alloc_from_bucket(umm, 0, UMEM_TYPE_ANY, UMEM_DEFAULT_MBKT_ID);
	assert_true(UMOFF_IS_NULL(off));
	validate_persist_activity(0, 0);

	/* Validate allocation of various sizes */
	snap_persist_activity();
	for (i = 1; i < 16; i++) {
		size = (1ul<<i) - 1;
		total_size += size;
		off_arr[i] = umem_atomic_alloc_from_bucket(umm, size, UMEM_TYPE_ANY,
							   UMEM_DEFAULT_MBKT_ID);
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
	dest = umem_atomic_copy(umm, ptr, local_buf+768, 256, UMEM_COMMIT_IMMEDIATE);
	assert_true(dest == ptr);
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
	assert_int_equal(rc, 0);

	umoff = umem_zalloc(umm, 4);
	assert_false(UMOFF_IS_NULL(umoff));

	value1 = umem_off2ptr(umm, umoff);
	assert_true(*value1 == 0);

	rc = umem_free(umm, umoff);
	assert_int_equal(rc, 0);
	utest_tx_end(arg->ta_utx, rc);
}

static void
test_alloc_from_bucket(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	int			*value1;
	umem_off_t		 umoff = 0;
	int			 rc;

	rc = utest_tx_begin(arg->ta_utx);
	assert_int_equal(rc, 0);

	umoff = umem_zalloc_from_bucket(umm, 4, UMEM_DEFAULT_MBKT_ID);
	assert_false(UMOFF_IS_NULL(umoff));

	value1 = umem_off2ptr(umm, umoff);

	assert_true(*value1 == 0);

	rc = umem_free(umm, umoff);
	assert_int_equal(rc, 0);
	utest_tx_end(arg->ta_utx, rc);
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
test_tx_alloc_from_bucket(void **state)
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

	umoff1 = umem_zalloc_from_bucket(umm, 4, UMEM_DEFAULT_MBKT_ID);
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

	umoff2 = umem_alloc_from_bucket(umm, 4, UMEM_DEFAULT_MBKT_ID);
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
	expect_assert_failure(umem_alloc_from_bucket(umm, 100, UMEM_DEFAULT_MBKT_ID));
	expect_assert_failure(umem_zalloc_from_bucket(umm, 100, UMEM_DEFAULT_MBKT_ID));

	/* alloc of size zero */
	snap_persist_activity();
	utest_get_scm_used_space(arg->ta_utx, &initial_mem_used);
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	umoff1 = umem_alloc_from_bucket(umm, 0, UMEM_DEFAULT_MBKT_ID);
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
	umoff1 = umem_zalloc_from_bucket(umm, 0, UMEM_DEFAULT_MBKT_ID);
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
	umoff1 = umem_zalloc_from_bucket(umm, 4, UMEM_DEFAULT_MBKT_ID);
	assert_false(UMOFF_IS_NULL(umoff1));
	rc = umem_tx_end(umm, 0);
	assert_int_equal(rc, 0);
	expect_assert_failure(umem_free(umm, umoff1));

	/* abort after alloc and used memory should not increase */
	snap_persist_activity();
	utest_get_scm_used_space(arg->ta_utx, &initial_mem_used);
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	umoff1 = umem_alloc_from_bucket(umm, 16, UMEM_DEFAULT_MBKT_ID);
	assert_false(UMOFF_IS_NULL(umoff1));
	umoff1 = umem_zalloc_from_bucket(umm, 32, UMEM_DEFAULT_MBKT_ID);
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
	rc = umem_tx_add(umm, umm->umm_pool->up_store.stor_size + 4096, 128);
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
test_tx_bucket_reserve_publish_cancel(void **state)
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
	umoff = umem_reserve_from_bucket(umm, rsrvd_act, 980, UMEM_DEFAULT_MBKT_ID);
	assert_false(UMOFF_IS_NULL(umoff));
	rsrv_ptr1 = umem_off2ptr(umm, umoff);
	memset(rsrv_ptr1, 0, 980);
	memset(local_buf, 0, 980);
	memcpy(rsrv_ptr1+128, data, strlen(data));
	memcpy(local_buf+128, data, strlen(data));

	umoff = umem_reserve_from_bucket(umm, rsrvd_act, 128, UMEM_DEFAULT_MBKT_ID);
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
	umoff = umem_reserve_from_bucket(umm, rsrvd_act, 980, UMEM_DEFAULT_MBKT_ID);
	assert_false(UMOFF_IS_NULL(umoff));
	rsrv_ptr1 = umem_off2ptr(umm, umoff);
	memset(rsrv_ptr1, 1, 980);
	memset(local_buf, 1, 980);

	umoff = umem_reserve_from_bucket(umm, rsrvd_act, 128, UMEM_DEFAULT_MBKT_ID);
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
	umoff = umem_atomic_alloc_from_bucket(umm, 980, UMEM_TYPE_ANY, UMEM_DEFAULT_MBKT_ID);
	assert_false(UMOFF_IS_NULL(umoff));
	rsrv_ptr3 = umem_off2ptr(umm, umoff);
	assert_ptr_equal(rsrv_ptr1, rsrv_ptr3);
	umoff = umem_atomic_alloc_from_bucket(umm, 128, UMEM_TYPE_ANY, UMEM_DEFAULT_MBKT_ID);
	assert_false(UMOFF_IS_NULL(umoff));
	rsrv_ptr4 = umem_off2ptr(umm, umoff);
	assert_ptr_equal(rsrv_ptr2, rsrv_ptr4);
	umem_rsrvd_act_free(&rsrvd_act);

	/* reserve - atomic_copy - cancel */
	rc = umem_rsrvd_act_alloc(umm, &rsrvd_act, 2);
	assert_int_equal(rc, 0);
	umoff = umem_reserve_from_bucket(umm, rsrvd_act, 980, UMEM_DEFAULT_MBKT_ID);
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

static void
test_tx_bucket_dfree_publish_cancel(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	int			 rc;
	struct umem_rsrvd_act	*rsrvd_act;
	umem_off_t		 umoff1, umoff2;
	uint64_t		 freed_mem = 0;
	uint64_t		 initial_mem_used, cur_mem_used;

	/* Defer Free/Publish */
	umoff1 = umem_atomic_alloc_from_bucket(umm, 2048, UMEM_TYPE_ANY, UMEM_DEFAULT_MBKT_ID);
	assert_false(UMOFF_IS_NULL(umoff1));
	umoff2 = umem_atomic_alloc_from_bucket(umm, 1024, UMEM_TYPE_ANY, UMEM_DEFAULT_MBKT_ID);
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
	umoff1 = umem_atomic_alloc_from_bucket(umm, 2048, UMEM_TYPE_ANY, UMEM_DEFAULT_MBKT_ID);
	assert_false(UMOFF_IS_NULL(umoff1));
	umoff2 = umem_atomic_alloc_from_bucket(umm, 1024, UMEM_TYPE_ANY, UMEM_DEFAULT_MBKT_ID);
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

static void
test_atomic_alloc_mb(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	umem_off_t               umoff, umoff1, umoff2, umoff3, umoff4;
	uint32_t                 mb_id;
	int                      found = 0, i;

	mb_id = umem_allot_mb_evictable(umm, 0);
	assert_int_not_equal(mb_id, 0); /* zero maps to non-evictable memory bucket */

	/* Allocate objects from the memory bucket */
	umoff1 = umem_atomic_alloc_from_bucket(umm, 2048, UMEM_TYPE_ANY, mb_id);
	assert_false(UMOFF_IS_NULL(umoff1));
	assert_true(umem_get_mb_from_offset(umm, umoff1) == mb_id);
	umoff2 = umem_atomic_alloc_from_bucket(umm, 1024, UMEM_TYPE_ANY, mb_id);
	assert_false(UMOFF_IS_NULL(umoff2));
	assert_true(umem_get_mb_from_offset(umm, umoff2) == mb_id);

	/* Allocate from non-evictable memory bucket */
	umoff3 = umem_atomic_alloc_from_bucket(umm, 2048, UMEM_TYPE_ANY, UMEM_DEFAULT_MBKT_ID);
	assert_false(UMOFF_IS_NULL(umoff3));
	assert_true(umem_get_mb_from_offset(umm, umoff3) == UMEM_DEFAULT_MBKT_ID);
	umoff4 = umem_atomic_alloc_from_bucket(umm, 1024, UMEM_TYPE_ANY, UMEM_DEFAULT_MBKT_ID);
	assert_false(UMOFF_IS_NULL(umoff4));
	assert_true(umem_get_mb_from_offset(umm, umoff4) == UMEM_DEFAULT_MBKT_ID);

	/* Free allocated objects */
	umem_atomic_free(umm, umoff1);
	umem_atomic_free(umm, umoff2);
	umem_atomic_free(umm, umoff3);
	umem_atomic_free(umm, umoff4);

	/*
	 * Validate whether those freed objects are in the free list of respective
	 * Memory buckets. We do many allocations and free to ensure that the objects
	 * in recycler bin are moved back for reallocation.
	 */

	found = 0;
	for (i = 0; i < 16 * 1024; i++) {
		umoff = umem_atomic_alloc_from_bucket(umm, 2048, UMEM_TYPE_ANY, mb_id);
		assert_false(UMOFF_IS_NULL(umoff));
		assert_true(umem_get_mb_from_offset(umm, umoff) == mb_id);
		umem_atomic_free(umm, umoff);
		if (umoff == umoff1) {
			found = 1;
			break;
		}
	}
	assert_int_equal(found, 1);

	found = 0;
	for (i = 0; i < 16 * 1024; i++) {
		umoff =
		    umem_atomic_alloc_from_bucket(umm, 2048, UMEM_TYPE_ANY, UMEM_DEFAULT_MBKT_ID);
		assert_false(UMOFF_IS_NULL(umoff));
		assert_true(umem_get_mb_from_offset(umm, umoff) == UMEM_DEFAULT_MBKT_ID);
		umem_atomic_free(umm, umoff);
		if (umoff == umoff3) {
			found = 1;
			break;
		}
	}
	assert_int_equal(found, 1);

	found = 0;
	for (i = 0; i < 16 * 1024; i++) {
		umoff = umem_atomic_alloc_from_bucket(umm, 1024, UMEM_TYPE_ANY, mb_id);
		assert_false(UMOFF_IS_NULL(umoff));
		assert_true(umem_get_mb_from_offset(umm, umoff) == mb_id);
		umem_atomic_free(umm, umoff);
		if (umoff == umoff2) {
			found = 1;
			break;
		}
	}
	assert_int_equal(found, 1);

	found = 0;
	for (i = 0; i < 16 * 1024; i++) {
		umoff =
		    umem_atomic_alloc_from_bucket(umm, 1024, UMEM_TYPE_ANY, UMEM_DEFAULT_MBKT_ID);
		assert_false(UMOFF_IS_NULL(umoff));
		assert_true(umem_get_mb_from_offset(umm, umoff) == UMEM_DEFAULT_MBKT_ID);
		umem_atomic_free(umm, umoff);
		if (umoff == umoff4) {
			found = 1;
			break;
		}
	}
	assert_int_equal(found, 1);
}

static void
test_atomic_alloc_overflow_mb(void **state)
{
	struct test_arg      *arg = *state;
	struct umem_instance *umm = utest_utx2umm(arg->ta_utx);
	umem_off_t            umoff, umoff_prev;
	umem_off_t            umoff1 = UMOFF_NULL, umoff2 = UMOFF_NULL, umoff3 = UMOFF_NULL;
	uint32_t              mb_id, ret_id;
	int                   hit            = 0;
	uint64_t              allocated_size = 0;

	mb_id = umem_allot_mb_evictable(umm, 0);
	assert_int_not_equal(mb_id, 0); /* zero maps to non-evictable memory bucket */

	do {
		hit = 0;
		/* Allocate objects from the memory bucket */
		umoff_prev = umoff1;
		umoff1     = umem_atomic_alloc_from_bucket(umm, 2048, UMEM_TYPE_ANY, mb_id);
		assert_false(UMOFF_IS_NULL(umoff1));
		ret_id = umem_get_mb_from_offset(umm, umoff1);
		if (ret_id == mb_id)
			allocated_size += 2048;
		else if (ret_id == 0) {
			umem_atomic_free(umm, umoff1);
			umoff1 = umoff_prev;
			hit++;
		} else
			assert_true(ret_id == mb_id);
		umoff_prev = umoff2;
		umoff2     = umem_atomic_alloc_from_bucket(umm, 1024, UMEM_TYPE_ANY, mb_id);
		assert_false(UMOFF_IS_NULL(umoff2));
		ret_id = umem_get_mb_from_offset(umm, umoff2);
		if (ret_id == mb_id)
			allocated_size += 1024;
		else if (ret_id == 0) {
			umem_atomic_free(umm, umoff2);
			umoff2 = umoff_prev;
			hit++;
		} else
			assert_true(ret_id == mb_id);
		umoff_prev = umoff3;
		umoff3     = umem_atomic_alloc_from_bucket(umm, 128, UMEM_TYPE_ANY, mb_id);
		assert_false(UMOFF_IS_NULL(umoff3));
		ret_id = umem_get_mb_from_offset(umm, umoff3);
		if (ret_id == mb_id)
			allocated_size += 128;
		else if (ret_id == 0) {
			umem_atomic_free(umm, umoff3);
			umoff3 = umoff_prev;
			hit++;
		} else
			assert_true(ret_id == mb_id);
	} while (hit != 3);
	print_message("Total allocated size from mb %lu\n", allocated_size);

	umem_atomic_free(umm, umoff1);
	umem_atomic_free(umm, umoff2);
	umem_atomic_free(umm, umoff3);

	/*
	 * The only free memory in the MB is that of the offsets freed above.
	 * Subsequent allocation from the same MB should return the same offsets.
	 */
	umoff = umem_atomic_alloc_from_bucket(umm, 2048, UMEM_TYPE_ANY, mb_id);
	assert_false(UMOFF_IS_NULL(umoff));
	assert_true(umem_get_mb_from_offset(umm, umoff) == mb_id);
	assert_true(umoff == umoff1);
	umoff = umem_atomic_alloc_from_bucket(umm, 1024, UMEM_TYPE_ANY, mb_id);
	assert_false(UMOFF_IS_NULL(umoff));
	assert_true(umem_get_mb_from_offset(umm, umoff) == mb_id);
	assert_true(umoff == umoff2);
	umoff = umem_atomic_alloc_from_bucket(umm, 128, UMEM_TYPE_ANY, mb_id);
	assert_false(UMOFF_IS_NULL(umoff));
	assert_true(umem_get_mb_from_offset(umm, umoff) == mb_id);
	assert_true(umoff == umoff3);
}

static void
test_reserve_from_mb(void **state)
{
	struct test_arg       *arg = *state;
	struct umem_instance  *umm = utest_utx2umm(arg->ta_utx);
	umem_off_t             umoff, umoff1;
	uint32_t               mb_id;
	struct umem_rsrvd_act *rsrvd_act;
	size_t                 rsrv_size = 1032;
	int                    found     = 0, i, rc;

	mb_id = umem_allot_mb_evictable(umm, 0);
	assert_int_not_equal(mb_id, 0); /* zero maps to non-evictable memory bucket */

	/* Reserve an object and then cancel the allocation */
	rc = umem_rsrvd_act_alloc(umm, &rsrvd_act, 1);
	assert_int_equal(rc, 0);
	umoff = umem_reserve_from_bucket(umm, rsrvd_act, rsrv_size, mb_id);
	assert_false(UMOFF_IS_NULL(umoff));
	/* Validate that the object is from the memory bucket of interest. */
	assert_true(umem_get_mb_from_offset(umm, umoff) == mb_id);
	umem_cancel(umm, rsrvd_act);
	umem_rsrvd_act_free(&rsrvd_act);
	/* Validate that the object is really freed */
	rc = umem_rsrvd_act_alloc(umm, &rsrvd_act, 1);
	assert_int_equal(rc, 0);
	umoff1 = umem_reserve_from_bucket(umm, rsrvd_act, rsrv_size, mb_id);
	assert_false(UMOFF_IS_NULL(umoff));
	assert_true(umoff1 == umoff);
	umem_cancel(umm, rsrvd_act);
	umem_rsrvd_act_free(&rsrvd_act);

	/* Reserve an object and publish it within a transaction. */
	rc = umem_rsrvd_act_alloc(umm, &rsrvd_act, 1);
	assert_int_equal(rc, 0);
	umoff = umem_reserve_from_bucket(umm, rsrvd_act, rsrv_size, mb_id);
	assert_false(UMOFF_IS_NULL(umoff));
	/* Validate that the object is from the memory bucket of interest. */
	assert_true(umem_get_mb_from_offset(umm, umoff) == mb_id);
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	rc = umem_tx_publish(umm, rsrvd_act);
	assert_int_equal(rc, 0);
	rc = umem_tx_commit(umm);
	assert_int_equal(rc, 0);
	umem_rsrvd_act_free(&rsrvd_act);
	/*
	 * Make sure that the above allocated object is never returned by
	 * subsequent allocation.
	 */
	for (i = 0; i < 32 * 1024; i++) {
		umoff1 = umem_atomic_alloc_from_bucket(umm, rsrv_size, UMEM_TYPE_ANY, mb_id);
		assert_false(UMOFF_IS_NULL(umoff1));
		assert_true(umem_get_mb_from_offset(umm, umoff1) == mb_id);
		umem_atomic_free(umm, umoff1);
		assert_false(umoff == umoff1);
	}

	/* Defer free an object and cancel it subsequently */
	rc = umem_rsrvd_act_alloc(umm, &rsrvd_act, 1);
	assert_int_equal(rc, 0);
	umem_defer_free(umm, umoff, rsrvd_act);
	assert_int_equal(rc, 0);
	umem_cancel(umm, rsrvd_act);
	umem_rsrvd_act_free(&rsrvd_act);
	/* Validate that the object is not really freed */
	for (i = 0; i < 32 * 1024; i++) {
		umoff1 = umem_atomic_alloc_from_bucket(umm, rsrv_size, UMEM_TYPE_ANY, mb_id);
		assert_false(UMOFF_IS_NULL(umoff1));
		assert_true(umem_get_mb_from_offset(umm, umoff1) == mb_id);
		umem_atomic_free(umm, umoff1);
		assert_false(umoff == umoff1);
	}

	/* Defer free an object and publish it within a transaction. */
	rc = umem_rsrvd_act_alloc(umm, &rsrvd_act, 1);
	assert_int_equal(rc, 0);
	umem_defer_free(umm, umoff, rsrvd_act);
	assert_int_equal(rc, 0);
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	rc = umem_tx_publish(umm, rsrvd_act);
	assert_int_equal(rc, 0);
	rc = umem_tx_commit(umm);
	assert_int_equal(rc, 0);
	umem_rsrvd_act_free(&rsrvd_act);
	/* Validate that the object is returned in subsequent allocation */
	found = 0;
	for (i = 0; i < 32 * 1024; i++) {
		umoff1 = umem_atomic_alloc_from_bucket(umm, rsrv_size, UMEM_TYPE_ANY, mb_id);
		assert_false(UMOFF_IS_NULL(umoff1));
		assert_true(umem_get_mb_from_offset(umm, umoff1) == mb_id);
		umem_atomic_free(umm, umoff1);
		if (umoff == umoff1) {
			found = 1;
			break;
		}
	}
	assert_int_equal(found, 1);
}

static void
test_tx_alloc_from_mb(void **state)
{
	struct test_arg      *arg   = *state;
	struct umem_instance *umm   = utest_utx2umm(arg->ta_utx);
	umem_off_t            umoff = UINT64_MAX, umoff1 = UINT64_MAX;
	uint32_t              mb_id;
	size_t                alloc_size = 1024;
	int                   found      = 0, i, rc;

	mb_id = umem_allot_mb_evictable(umm, 0);
	assert_int_not_equal(mb_id, 0); /* zero maps to non-evictable memory bucket */

	/* Do a tx alloc and fail the transaction. */
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	umoff = umem_alloc_from_bucket(umm, alloc_size, mb_id);
	assert_false(UMOFF_IS_NULL(umoff));
	assert_true(umem_get_mb_from_offset(umm, umoff) == mb_id);
	rc = umem_tx_end(umm, 1);
	assert_true(rc == umem_tx_errno(1));
	found = 0;
	for (i = 0; i < 32 * 1024; i++) {
		umoff1 = umem_atomic_alloc_from_bucket(umm, alloc_size, UMEM_TYPE_ANY, mb_id);
		assert_false(UMOFF_IS_NULL(umoff1));
		assert_true(umem_get_mb_from_offset(umm, umoff1) == mb_id);
		umem_atomic_free(umm, umoff1);
		if (umoff == umoff1) {
			found = 1;
			break;
		}
	}
	assert_int_equal(found, 1);

	/* Do a tx alloc and pass the transaction. */
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	umoff = umem_alloc_from_bucket(umm, alloc_size, mb_id);
	assert_false(UMOFF_IS_NULL(umoff));
	assert_true(umem_get_mb_from_offset(umm, umoff) == mb_id);
	rc = umem_tx_end(umm, 0);
	assert_int_equal(rc, 0);
	for (i = 0; i < 32 * 1024; i++) {
		umoff1 = umem_atomic_alloc_from_bucket(umm, alloc_size, UMEM_TYPE_ANY, mb_id);
		assert_false(UMOFF_IS_NULL(umoff1));
		assert_true(umem_get_mb_from_offset(umm, umoff1) == mb_id);
		umem_atomic_free(umm, umoff1);
		assert_false(umoff == umoff1);
	}

	/* Do a tx free and fail the transaction. */
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	umem_free(umm, umoff);
	rc = umem_tx_end(umm, 1);
	assert_true(rc == umem_tx_errno(1));
	for (i = 0; i < 32 * 1024; i++) {
		umoff1 = umem_atomic_alloc_from_bucket(umm, alloc_size, UMEM_TYPE_ANY, mb_id);
		assert_false(UMOFF_IS_NULL(umoff1));
		assert_true(umem_get_mb_from_offset(umm, umoff1) == mb_id);
		umem_atomic_free(umm, umoff1);
		assert_false(umoff == umoff1);
	}

	/* Do a tx free and pass the transaction. */
	rc = umem_tx_begin(umm, NULL);
	assert_int_equal(rc, 0);
	umem_free(umm, umoff);
	rc = umem_tx_end(umm, 0);
	assert_int_equal(rc, 0);
	found = 0;
	for (i = 0; i < 32 * 1024; i++) {
		umoff1 = umem_atomic_alloc_from_bucket(umm, alloc_size, UMEM_TYPE_ANY, mb_id);
		assert_false(UMOFF_IS_NULL(umoff1));
		assert_true(umem_get_mb_from_offset(umm, umoff1) == mb_id);
		umem_atomic_free(umm, umoff1);
		if (umoff == umoff1) {
			found = 1;
			break;
		}
	}
	assert_int_equal(found, 1);
}

struct bucket_alloc_info {
	umem_off_t start_umoff;
	uint32_t   num_allocs;
	uint32_t   mb_id;
};

void
alloc_bucket_to_full(struct umem_instance *umm, struct bucket_alloc_info *ainfo)
{
	umem_off_t              umoff, prev_umoff;
	size_t                  alloc_size = 128;
	umem_off_t             *ptr;
	struct umem_cache_range rg = {0};
	struct umem_pin_handle *p_hdl;
	uint32_t                id = ainfo->mb_id;

	if (UMOFF_IS_NULL(ainfo->start_umoff)) {
		ainfo->start_umoff =
		    umem_atomic_alloc_from_bucket(umm, alloc_size, UMEM_TYPE_ANY, id);
		assert_false(UMOFF_IS_NULL(ainfo->start_umoff));
		ainfo->num_allocs++;
		assert_true(umem_get_mb_from_offset(umm, ainfo->start_umoff) == id);
	}
	prev_umoff = ainfo->start_umoff;
	rg.cr_off  = umem_get_mb_base_offset(umm, id);
	rg.cr_size = 1;
	assert_true(umem_cache_pin(&umm->umm_pool->up_store, &rg, 1, 0, &p_hdl) == 0);

	while (1) {
		umoff = umem_atomic_alloc_from_bucket(umm, alloc_size, UMEM_TYPE_ANY, id);
		assert_false(UMOFF_IS_NULL(umoff));
		if (umem_get_mb_from_offset(umm, umoff) != id) {
			umem_atomic_free(umm, umoff);
			break;
		}
		ptr        = (umem_off_t *)umem_off2ptr(umm, prev_umoff);
		*ptr       = umoff;
		ptr        = (umem_off_t *)umem_off2ptr(umm, umoff);
		*ptr       = UMOFF_NULL;
		prev_umoff = umoff;
		ainfo->num_allocs++;
	}
	umem_cache_unpin(&umm->umm_pool->up_store, p_hdl);
	print_message("Bulk Alloc: Bucket %d, start off %lu num_allocation %d\n", ainfo->mb_id,
		      ainfo->start_umoff, ainfo->num_allocs);
}

void
free_bucket_by_pct(struct umem_instance *umm, struct bucket_alloc_info *ainfo, int pct)
{
	int                     num_free = (ainfo->num_allocs * pct) / 100;
	umem_off_t              umoff, *ptr, next_umoff;
	struct umem_pin_handle *p_hdl;
	struct umem_cache_range rg = {0};
	int                     i, rc;

	assert_true((pct >= 0) && (pct <= 100));

	if (UMOFF_IS_NULL(ainfo->start_umoff))
		return;
	print_message("Bulk Free BEFORE: Bucket %d, start off %lu num_allocation %d\n",
		      ainfo->mb_id, ainfo->start_umoff, ainfo->num_allocs);

	rg.cr_off  = umem_get_mb_base_offset(umm, ainfo->mb_id);
	rg.cr_size = 1;
	rc         = umem_cache_pin(&umm->umm_pool->up_store, &rg, 1, 0, &p_hdl);
	assert_true(rc == 0);

	umoff = ainfo->start_umoff;
	for (i = 0; i < num_free; i++) {
		ptr        = (umem_off_t *)umem_off2ptr(umm, umoff);
		next_umoff = *ptr;
		umem_atomic_free(umm, umoff);
		umoff = next_umoff;
		ainfo->num_allocs--;
		if (UMOFF_IS_NULL(umoff))
			break;
	}
	ainfo->start_umoff = umoff;
	umem_cache_unpin(&umm->umm_pool->up_store, p_hdl);
	print_message("Bulk Free AFTER: Bucket %d, start off %lu num_allocation %d\n", ainfo->mb_id,
		      ainfo->start_umoff, ainfo->num_allocs);
}

static void
test_tx_alloc_from_multimb(void **state)
{
	struct test_arg         *arg = *state;
	struct umem_instance    *umm = utest_utx2umm(arg->ta_utx);
	struct bucket_alloc_info ainfo[10];
	uint32_t                 id;
	int                      i;

	for (i = 0; i < 8; i++) {
		/* Create an MB and fill it with allocs */
		ainfo[i].mb_id       = umem_allot_mb_evictable(umm, 0);
		ainfo[i].num_allocs  = 0;
		ainfo[i].start_umoff = UMOFF_NULL;
		assert_true(ainfo[i].mb_id != 0);
		alloc_bucket_to_full(umm, &ainfo[i]);
	}

	/* Free 5% of space for MB 2 */
	free_bucket_by_pct(umm, &ainfo[2], 5); /* 90+ */
	/* Free 30% of space for MB 3 */
	free_bucket_by_pct(umm, &ainfo[3], 30); /* 30-75 */
	/* Free 80% of space for MB 4 */
	free_bucket_by_pct(umm, &ainfo[4], 80); /* 0-30 */
	/* Free 15% of space for MB 5 */
	free_bucket_by_pct(umm, &ainfo[5], 20); /* 75-90 */
	/* Free 10% of space for MB 6 */
	free_bucket_by_pct(umm, &ainfo[6], 50); /* 30-75 */
	/* Free 90% of space for MB 8 */
	free_bucket_by_pct(umm, &ainfo[7], 90); /* 0-30 */

	/* Allocator should return mb with utilization 30%-75% */
	id = umem_allot_mb_evictable(umm, 0);
	print_message("obtained id %d, expected is %d\n", id, ainfo[3].mb_id);
	assert_true(id == ainfo[3].mb_id);
	alloc_bucket_to_full(umm, &ainfo[3]);
	id = umem_allot_mb_evictable(umm, 0);
	print_message("obtained id %d, expected is %d\n", id, ainfo[6].mb_id);
	assert_true(id == ainfo[6].mb_id);
	alloc_bucket_to_full(umm, &ainfo[6]);

	/* Next preference should be 0%-30% */
	id = umem_allot_mb_evictable(umm, 0);
	print_message("obtained id %d, expected is %d\n", id, ainfo[4].mb_id);
	assert_true(id == ainfo[4].mb_id);
	alloc_bucket_to_full(umm, &ainfo[4]);
	id = umem_allot_mb_evictable(umm, 0);
	print_message("obtained id %d, expected is %d\n", id, ainfo[7].mb_id);
	assert_true(id == ainfo[7].mb_id);
	alloc_bucket_to_full(umm, &ainfo[7]);

	/* Next is to create a new memory bucket. */
	id = umem_allot_mb_evictable(umm, 0);
	for (i = 0; i < 8; i++)
		assert_true(id != ainfo[i].mb_id);
	print_message("obtained id %d\n", id);

	/* Without eviction support 75-90% and 90% and above cannot be tested.
	 * TBD: as this requires supporting eviction within this test environment.
	 */
}

static void
test_umempobj_create_smallsize(void **state)
{
	int                  num = 0;
	char                *name;
	uint32_t             id;
	struct umem_store    ustore_tmp = {.stor_size  = POOL_SIZE,
					   .stor_ops   = &_store_ops_v2,
					   .store_type = DAOS_MD_BMEM_V2,
					   .stor_priv  = (void *)(UINT64_MAX)};
	struct umem_attr     uma;
	struct umem_instance umm;

	uma.uma_id = umempobj_backend_type2class_id(ustore_tmp.store_type);

	/* umempobj_create with zero scm size */
	D_ASPRINTF(name, "/mnt/daos/umem-test-tmp-%d", num++);
	assert_true(name != NULL);
	uma.uma_pool =
	    umempobj_create(name, "invalid_pool", UMEMPOBJ_ENABLE_STATS, 0, 0666, &ustore_tmp);
	assert_ptr_equal(uma.uma_pool, NULL);
	unlink(name);
	D_FREE(name);

	/* umempobj_create with zero metablob size */
	D_ASPRINTF(name, "/mnt/daos/umem-test-tmp-%d", num++);
	assert_true(name != NULL);
	ustore_tmp.stor_size = 0;
	uma.uma_pool = umempobj_create(name, "invalid_pool", UMEMPOBJ_ENABLE_STATS, POOL_SIZE, 0666,
				       &ustore_tmp);
	assert_ptr_equal(uma.uma_pool, NULL);
	ustore_tmp.stor_size = POOL_SIZE;
	unlink(name);
	D_FREE(name);

	/* umempobj_create with scm size less than 32MB */
	D_ASPRINTF(name, "/mnt/daos/umem-test-tmp-%d", num++);
	assert_true(name != NULL);
	uma.uma_pool = umempobj_create(name, "invalid_pool", UMEMPOBJ_ENABLE_STATS,
				       24 * 1024 * 1024, 0666, &ustore_tmp);
	assert_ptr_equal(uma.uma_pool, NULL);
	unlink(name);
	D_FREE(name);

	/* umempobj_create with scm size set to 112MB */
	D_ASPRINTF(name, "/mnt/daos/umem-test-tmp-%d", num++);
	assert_true(name != NULL);
	uma.uma_pool = umempobj_create(name, "invalid_pool", UMEMPOBJ_ENABLE_STATS,
				       112 * 1024 * 1024, 0666, &ustore_tmp);
	assert_ptr_not_equal(uma.uma_pool, NULL);
	umempobj_close(uma.uma_pool);
	unlink(name);
	D_FREE(name);

	/* umempobj_create with scm and metablob size set to 112MB */
	D_ASPRINTF(name, "/mnt/daos/umem-test-tmp-%d", num++);
	assert_true(name != NULL);
	ustore_tmp.stor_size = 112 * 1024 * 1024;
	uma.uma_pool         = umempobj_create(name, "invalid_pool", UMEMPOBJ_ENABLE_STATS,
					       112 * 1024 * 1024, 0666, &ustore_tmp);
	umem_class_init(&uma, &umm);
	id = umem_allot_mb_evictable(&umm, 0);
	print_message("with scm == metablob, evictable id returned is %d\n", id);
	assert_true(id == 0);
	ustore_tmp.stor_size = POOL_SIZE;
	assert_ptr_not_equal(uma.uma_pool, NULL);
	umempobj_close(uma.uma_pool);
	unlink(name);
	D_FREE(name);

	/* umempobj_create with scm size greater than metablob size*/
	D_ASPRINTF(name, "/mnt/daos/umem-test-tmp-%d", num++);
	assert_true(name != NULL);
	ustore_tmp.stor_size = 224 * 1024 * 1024;
	uma.uma_pool         = umempobj_create(name, "invalid_pool", UMEMPOBJ_ENABLE_STATS,
					       112 * 1024 * 1024, 0666, &ustore_tmp);
	umem_class_init(&uma, &umm);
	id = umem_allot_mb_evictable(&umm, 0);
	print_message("with metablob > scm, evictable id returned is %d\n", id);
	assert_true(id != 0);
	ustore_tmp.stor_size = POOL_SIZE;
	assert_ptr_not_equal(uma.uma_pool, NULL);
	umempobj_close(uma.uma_pool);
	unlink(name);
	D_FREE(name);
}

static void
test_umempobj_nemb_usage(void **state)
{
	int                  num = 0, i;
	char                *name;
	struct umem_store    ustore_tmp = {.stor_size  = 256 * 1024 * 1024,
					   .stor_ops   = &_store_ops_v2,
					   .store_type = DAOS_MD_BMEM_V2,
					   .stor_priv  = (void *)(UINT64_MAX)};
	struct umem_attr     uma;
	struct umem_instance umm;
	umem_off_t           umoff, *ptr = NULL, prev_umoff = UMOFF_NULL;
	size_t               alloc_size = (10 * 1024 * 1024);

	uma.uma_id = umempobj_backend_type2class_id(ustore_tmp.store_type);
	/* Create a heap and cache of size 256MB and 240MB (16 & 15 zones) respectively */
	D_ASPRINTF(name, "/mnt/daos/umem-test-tmp-%d", 0);
	assert_true(name != NULL);
	uma.uma_pool = umempobj_create(name, "valid_pool", UMEMPOBJ_ENABLE_STATS, 240 * 1024 * 1024,
				       0666, &ustore_tmp);
	assert_ptr_not_equal(uma.uma_pool, NULL);

	umem_class_init(&uma, &umm);

	/* Do the SOEMB reservation before the actual test. */
	for (i = 0; i < MIN_SOEMB_CNT; i++) {
		umem_tx_begin(&umm, NULL);
		umem_tx_commit(&umm);
	}

	/* Do allocation and verify that only 80% of 15 zones - MIN_SOEMB_CNT are allotted
	 * to non evictable MBs
	 */
	for (num = 0;; num++) {
		/* do an allocation that takes more than half the zone size */
		umoff = umem_atomic_alloc(&umm, alloc_size, UMEM_TYPE_ANY);
		if (UMOFF_IS_NULL(umoff))
			break;
		ptr        = (umem_off_t *)umem_off2ptr(&umm, umoff);
		*ptr       = prev_umoff;
		prev_umoff = umoff;
	}
	/* 80% nemb when heap size greater than cache size */
	assert_int_equal(num, 12 - MIN_SOEMB_CNT);
	print_message("Number of allocations is %d\n", num);

	for (--num;; num--) {
		umoff = *ptr;
		umem_atomic_free(&umm, prev_umoff);
		if (UMOFF_IS_NULL(umoff))
			break;
		ptr        = (umem_off_t *)umem_off2ptr(&umm, umoff);
		prev_umoff = umoff;
	}
	assert_int_equal(num, 0);
	umempobj_close(uma.uma_pool);
	unlink(name);
	D_FREE(name);

	prev_umoff = UMOFF_NULL;
	/* Create a heap and cache of size 256MB (16 zones) each */
	D_ASPRINTF(name, "/mnt/daos/umem-test-tmp-%d", 1);
	assert_true(name != NULL);
	uma.uma_pool = umempobj_create(name, "valid_pool", UMEMPOBJ_ENABLE_STATS, 256 * 1024 * 1024,
				       0666, &ustore_tmp);
	assert_ptr_not_equal(uma.uma_pool, NULL);

	umem_class_init(&uma, &umm);

	/* Do the SOEMB reservation before the actual test. */
	for (i = 0; i < MIN_SOEMB_CNT; i++) {
		umem_tx_begin(&umm, NULL);
		umem_tx_commit(&umm);
	}

	/* Do allocation and verify that all 16 zones are allotted to non evictable MBs */
	for (num = 0;; num++) {
		/* do an allocation that takes more than half the zone size */
		umoff = umem_atomic_alloc(&umm, alloc_size, UMEM_TYPE_ANY);
		if (UMOFF_IS_NULL(umoff))
			break;
		ptr        = (umem_off_t *)umem_off2ptr(&umm, umoff);
		*ptr       = prev_umoff;
		prev_umoff = umoff;
	}
	assert_int_equal(num, 16);
	print_message("Number of allocations is %d\n", num);

	for (--num;; num--) {
		umoff = *ptr;
		umem_atomic_free(&umm, prev_umoff);
		if (UMOFF_IS_NULL(umoff))
			break;
		ptr        = (umem_off_t *)umem_off2ptr(&umm, umoff);
		prev_umoff = umoff;
	}
	assert_int_equal(num, 0);
	umempobj_close(uma.uma_pool);
	unlink(name);
	D_FREE(name);
}

static void
test_umempobj_heap_mb_stats(void **state)
{
	int                  num = 0, count, rc, i;
	char                *name;
	uint64_t             scm_size   = 128 * 1024 * 1024;
	uint64_t             meta_size  = 256 * 1024 * 1024;
	struct umem_store    ustore_tmp = {.stor_size  = meta_size,
					   .stor_ops   = &_store_ops_v2,
					   .store_type = DAOS_MD_BMEM_V2,
					   .stor_priv  = (void *)(UINT64_MAX)};
	struct umem_attr     uma;
	struct umem_instance umm;
	umem_off_t           umoff, *ptr = NULL, prev_umoff = UMOFF_NULL;
	size_t               alloc_size = 128;
	uint64_t             allocated, allocated0, allocated1;
	uint64_t             maxsz, maxsz_exp, maxsz_alloc;
	uint32_t             mb_id;

	uma.uma_id = umempobj_backend_type2class_id(ustore_tmp.store_type);
	/* Create a heap and cache of size 256MB and 128MB (16 & 8 zones) respectively */
	D_ASPRINTF(name, "/mnt/daos/umem-test-tmp-%d", 0);
	assert_true(name != NULL);
	uma.uma_pool = umempobj_create(name, "invalid_pool", UMEMPOBJ_ENABLE_STATS, scm_size, 0666,
				       &ustore_tmp);
	assert_ptr_not_equal(uma.uma_pool, NULL);
	maxsz_exp   = (uint64_t)(scm_size / MB_SIZE * NEMB_RATIO) * MB_SIZE;
	maxsz_alloc = ((uint64_t)(((scm_size / MB_SIZE) * NEMB_RATIO)) - MIN_SOEMB_CNT) * MB_SIZE;

	umem_class_init(&uma, &umm);

	rc = umempobj_get_mbusage(umm.umm_pool, 0, &allocated0, &maxsz);
	print_message("NE usage max_size = %lu exp_max_size = %lu allocated = %lu\n", maxsz,
		      maxsz_exp, allocated0);
	assert_int_equal(rc, 0);
	assert_int_equal(maxsz, maxsz_exp);

	/* Do the SOEMB reservation before the actual test. */
	for (i = 0; i < MIN_SOEMB_CNT; i++) {
		umem_tx_begin(&umm, NULL);
		umem_tx_commit(&umm);
	}

	/* allocate and consume all of the space */
	for (num = 0;; num++) {
		umoff = umem_atomic_alloc(&umm, alloc_size, UMEM_TYPE_ANY);
		if (UMOFF_IS_NULL(umoff))
			break;
		ptr        = (umem_off_t *)umem_off2ptr(&umm, umoff);
		*ptr       = prev_umoff;
		prev_umoff = umoff;
	}
	rc = umempobj_get_mbusage(umm.umm_pool, 0, &allocated1, &maxsz);
	print_message("NE usage max_size = %lu allocated = %lu\n", maxsz, allocated1);
	assert_int_equal(rc, 0);
	assert_true(allocated1 * 100 / maxsz_alloc >= 99);
	assert_int_equal(maxsz, maxsz_exp);

	for (count = num; count > num / 2; count--) {
		umoff = *ptr;
		umem_atomic_free(&umm, prev_umoff);
		if (UMOFF_IS_NULL(umoff))
			break;
		ptr        = (umem_off_t *)umem_off2ptr(&umm, umoff);
		prev_umoff = umoff;
	}
	rc = umempobj_get_mbusage(umm.umm_pool, 0, &allocated, &maxsz);
	print_message("NE usage max_size = %lu allocated = %lu\n", maxsz, allocated);
	assert_int_equal(rc, 0);
	assert_true(allocated < ((allocated1 / 2) + alloc_size));
	assert_true((allocated + alloc_size) > (allocated1 / 2));
	assert_int_equal(maxsz, maxsz_exp);
	for (;;) {
		umoff = *ptr;
		umem_atomic_free(&umm, prev_umoff);
		if (UMOFF_IS_NULL(umoff))
			break;
		ptr        = (umem_off_t *)umem_off2ptr(&umm, umoff);
		prev_umoff = umoff;
	}
	rc = umempobj_get_mbusage(umm.umm_pool, 0, &allocated, &maxsz);
	print_message("NE usage max_size = %lu allocated = %lu\n", maxsz, allocated);
	assert_int_equal(rc, 0);
	assert_int_equal(allocated, allocated0);
	assert_int_equal(maxsz, maxsz_exp);

	/* Now Test an evictable MB */
	mb_id = umem_allot_mb_evictable(&umm, 0);
	assert_true(mb_id > 0);
	maxsz_exp = MB_SIZE;

	rc = umempobj_get_mbusage(umm.umm_pool, mb_id, &allocated0, &maxsz);
	print_message("E usage max_size = %lu exp_max_size = %lu allocated = %lu\n", maxsz,
		      maxsz_exp, allocated0);
	assert_int_equal(rc, 0);
	assert_int_equal(maxsz, maxsz_exp);

	prev_umoff = UMOFF_NULL;
	ptr        = NULL;
	/* allocate and consume all of the space */
	for (num = 0;; num++) {
		umoff = umem_atomic_alloc_from_bucket(&umm, alloc_size, UMEM_TYPE_ANY, mb_id);
		if (umem_get_mb_from_offset(&umm, umoff) != mb_id) {
			umem_atomic_free(&umm, umoff);
			break;
		}
		ptr        = (umem_off_t *)umem_off2ptr(&umm, umoff);
		*ptr       = prev_umoff;
		prev_umoff = umoff;
	}
	rc = umempobj_get_mbusage(umm.umm_pool, mb_id, &allocated1, &maxsz);
	print_message("E usage max_size = %lu allocated = %lu\n", maxsz, allocated1);
	assert_int_equal(rc, 0);
	assert_true(allocated1 * 100 / maxsz >= 99);
	assert_int_equal(maxsz, maxsz_exp);

	for (count = num; count > num / 2; count--) {
		umoff = *ptr;
		umem_atomic_free(&umm, prev_umoff);
		if (UMOFF_IS_NULL(umoff))
			break;
		ptr        = (umem_off_t *)umem_off2ptr(&umm, umoff);
		prev_umoff = umoff;
	}
	rc = umempobj_get_mbusage(umm.umm_pool, mb_id, &allocated, &maxsz);
	print_message("E usage max_size = %lu allocated = %lu\n", maxsz, allocated);
	assert_int_equal(rc, 0);
	assert_true(allocated < allocated1 / 2);
	assert_int_equal(maxsz, maxsz_exp);
	for (;;) {
		umoff = *ptr;
		umem_atomic_free(&umm, prev_umoff);
		if (UMOFF_IS_NULL(umoff))
			break;
		ptr        = (umem_off_t *)umem_off2ptr(&umm, umoff);
		prev_umoff = umoff;
	}
	rc = umempobj_get_mbusage(umm.umm_pool, mb_id, &allocated, &maxsz);
	print_message("E usage max_size = %lu allocated = %lu\n", maxsz, allocated);
	assert_int_equal(rc, 0);
	assert_int_equal(allocated, allocated0);
	assert_int_equal(maxsz, maxsz_exp);

	/* Testing invalid mb_ids */
	rc = umempobj_get_mbusage(umm.umm_pool, mb_id - 1, &allocated, &maxsz);
	assert_int_equal(rc, -DER_INVAL);
	rc = umempobj_get_mbusage(umm.umm_pool, mb_id + 1, &allocated, &maxsz);
	assert_int_equal(rc, -DER_INVAL);
	rc = umempobj_get_mbusage(umm.umm_pool, 50, &allocated, &maxsz);
	assert_int_equal(rc, -DER_INVAL);

	umempobj_close(uma.uma_pool);
	unlink(name);
	D_FREE(name);
}

int
main(int argc, char **argv)
{
	int                            rc = 0;

	static const struct CMUnitTest v1_tests[] = {
	    {"BMEM001: Test atomic alloc/free", test_atomic_alloc, setup_pmem, teardown_pmem},
	    {"BMEM001a: Test atomic alloc/free", test_atomic_alloc_from_bucket, setup_pmem,
	     teardown_pmem},
	    {"BMEM002: Test null flags pmem", test_invalid_flags, setup_pmem, teardown_pmem},
	    {"BMEM003: Test alloc pmem", test_alloc, setup_pmem, teardown_pmem},
	    {"BMEM003a: Test alloc pmem", test_alloc_from_bucket, setup_pmem, teardown_pmem},
	    {"BMEM004a: Test atomic copy", test_atomic_copy, setup_pmem, teardown_pmem},
	    {"BMEM005: Test simple commit tx", test_simple_commit_tx, setup_pmem, teardown_pmem},
	    {"BMEM006: Test simple abort tx", test_simple_abort_tx, setup_pmem, teardown_pmem},
	    {"BMEM007: Test nested commit tx", test_nested_commit_tx, setup_pmem, teardown_pmem},
	    {"BMEM008: Test nested outer abort tx", test_nested_outer_abort_tx, setup_pmem,
	     teardown_pmem},
	    {"BMEM009: Test nested inner abort tx", test_nested_inner_abort_tx, setup_pmem,
	     teardown_pmem},
	    {"BMEM010: Test tx alloc/free", test_tx_alloc, setup_pmem, teardown_pmem},
	    {"BMEM010a: Test tx alloc/free", test_tx_alloc_from_bucket, setup_pmem, teardown_pmem},
	    {"BMEM011: Test tx add range", test_tx_add, setup_pmem, teardown_pmem},
	    {"BMEM012: Test tx add ptr", test_tx_add_ptr, setup_pmem, teardown_pmem},
	    {"BMEM013: Test tx xadd ptr", test_tx_xadd_ptr, setup_pmem, teardown_pmem},
	    {"BMEM014: Test tx reserve publish/cancel", test_tx_reserve_publish_cancel, setup_pmem,
	     teardown_pmem},
	    {"BMEM014a: Test tx reserve publish/cancel", test_tx_bucket_reserve_publish_cancel,
	     setup_pmem, teardown_pmem},
	    {"BMEM015: Test tx defer free publish/cancel", test_tx_dfree_publish_cancel, setup_pmem,
	     teardown_pmem},
	    {"BMEM015a: Test tx defer free publish/cancel", test_tx_bucket_dfree_publish_cancel,
	     setup_pmem, teardown_pmem},
	    {NULL, NULL, NULL, NULL}};

	static const struct CMUnitTest v2_tests[] = {
	    {"BMEM001: Test atomic alloc/free", test_atomic_alloc, setup_pmem_v2, teardown_pmem},
	    {"BMEM001a: Test atomic alloc/free", test_atomic_alloc_from_bucket, setup_pmem_v2,
	     teardown_pmem},
	    {"BMEM002: Test null flags pmem", test_invalid_flags, setup_pmem_v2, teardown_pmem},
	    {"BMEM003: Test alloc pmem", test_alloc, setup_pmem_v2, teardown_pmem},
	    {"BMEM003a: Test alloc pmem", test_alloc_from_bucket, setup_pmem_v2, teardown_pmem},
	    {"BMEM004a: Test atomic copy", test_atomic_copy, setup_pmem_v2, teardown_pmem},
	    {"BMEM005: Test simple commit tx", test_simple_commit_tx, setup_pmem_v2, teardown_pmem},
	    {"BMEM006: Test simple abort tx", test_simple_abort_tx, setup_pmem_v2, teardown_pmem},
	    {"BMEM007: Test nested commit tx", test_nested_commit_tx, setup_pmem_v2, teardown_pmem},
	    {"BMEM008: Test nested outer abort tx", test_nested_outer_abort_tx, setup_pmem_v2,
	     teardown_pmem},
	    {"BMEM009: Test nested inner abort tx", test_nested_inner_abort_tx, setup_pmem_v2,
	     teardown_pmem},
	    {"BMEM010: Test tx alloc/free", test_tx_alloc, setup_pmem_v2, teardown_pmem},
	    {"BMEM010a: Test tx alloc/free", test_tx_alloc_from_bucket, setup_pmem_v2,
	     teardown_pmem},
	    {"BMEM011: Test tx add range", test_tx_add, setup_pmem_v2, teardown_pmem},
	    {"BMEM012: Test tx add ptr", test_tx_add_ptr, setup_pmem_v2, teardown_pmem},
	    {"BMEM013: Test tx xadd ptr", test_tx_xadd_ptr, setup_pmem_v2, teardown_pmem},
	    {"BMEM014: Test tx reserve publish/cancel", test_tx_reserve_publish_cancel,
	     setup_pmem_v2, teardown_pmem},
	    {"BMEM014a: Test tx reserve publish/cancel", test_tx_bucket_reserve_publish_cancel,
	     setup_pmem_v2, teardown_pmem},
	    {"BMEM015: Test tx defer free publish/cancel", test_tx_dfree_publish_cancel,
	     setup_pmem_v2, teardown_pmem},
	    {"BMEM015a: Test tx defer free publish/cancel", test_tx_bucket_dfree_publish_cancel,
	     setup_pmem_v2, teardown_pmem},
	    {"BMEM016: Test atomic allocs within a memory bucket", test_atomic_alloc_mb,
	     setup_pmem_v2, teardown_pmem},
	    {"BMEM017: Test atomic allocs overflow a memory bucket", test_atomic_alloc_overflow_mb,
	     setup_pmem_v2, teardown_pmem},
	    {"BMEM018: Test reserve/defer_free from a memory bucket", test_reserve_from_mb,
	     setup_pmem_v2, teardown_pmem},
	    {"BMEM019: Test tx alloc/free from a memory bucket", test_tx_alloc_from_mb,
	     setup_pmem_v2, teardown_pmem},
	    {"BMEM020: Test tx alloc/free from multiple memory buckets", test_tx_alloc_from_multimb,
	     setup_pmem_v2, teardown_pmem},
	    {"BMEM021: Test umempobj create small size", test_umempobj_create_smallsize, NULL,
	     NULL},
	    {"BMEM022: Test umempobj non_evictable MB usage", test_umempobj_nemb_usage, NULL, NULL},
	    {"BMEM023: Test umempobj get MB stats", test_umempobj_heap_mb_stats, NULL, NULL},
	    {NULL, NULL, NULL, NULL}};

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0)
		return rc;

	d_register_alt_assert(mock_assert);

	rc = cmocka_run_group_tests_name("bmem v1 tests", v1_tests, global_setup, global_teardown);

	rc += cmocka_run_group_tests_name("bmem v2 tests", v2_tests, global_setup, global_teardown);

	daos_debug_fini();
	return rc;
}
