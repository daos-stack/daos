/**
 * (C) Copyright 2019-2021 Intel Corporation.
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
#define MAX_PAGES  10
#define MAX_CHUNKS 10

struct test_arg {
	struct utest_context	*ta_utx;
	uint64_t		*ta_root;
	char			*ta_pool_name;
	struct umem_store        ta_store;
	uint64_t                 ta_offsets[MAX_CHUNKS];
	uint64_t                 ta_sizes[MAX_CHUNKS];
	int                      ta_nr[MAX_PAGES];
	int                      ta_current_page;
	int                      ta_current_idx;
};

int
teardown_vmem(void **state)
{
	struct test_arg *arg = *state;
	int		 rc;

	if (arg == NULL) {
		print_message("state not set, likely due to group-setup"
			      " issue\n");
		return 0;
	}

	rc = utest_utx_destroy(arg->ta_utx);

	return rc;
}
int
setup_vmem(void **state)
{
	struct test_arg		*arg = *state;
	int			 rc = 0;

	rc = utest_vmem_create(sizeof(*arg->ta_root), &arg->ta_utx);
	if (rc != 0) {
		perror("Could not create vmem context");
		rc = 1;
		goto failed;
	}

	arg->ta_root = utest_utx2root(arg->ta_utx);

	return 0;
failed:
	return rc;
}


int
teardown_pmem(void **state)
{
	struct test_arg *arg = *state;
	int		 rc;

	if (arg == NULL) {
		print_message("state not set, likely due to group-setup"
			      " issue\n");
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
			       sizeof(*arg->ta_root), &arg->ta_utx);
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

	umem_cache_free(&arg->ta_store);

	D_FREE(arg);

	return 0;
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

static int
flush_prep(struct umem_store *store, struct umem_store_iod *iod, daos_handle_t *fh)
{
	struct test_arg *arg;
	int              i;
	int              idx;

	arg = container_of(store, struct test_arg, ta_store);

	assert_int_equal(iod->io_nr, arg->ta_nr[arg->ta_current_page]);
	for (i = arg->ta_current_idx; i < arg->ta_nr[arg->ta_current_page] + arg->ta_current_idx;
	     i++) {
		idx = i - arg->ta_current_idx;
		assert_int_equal(iod->io_regions[idx].sr_addr, arg->ta_offsets[i]);
		assert_int_equal(iod->io_regions[idx].sr_size, arg->ta_sizes[i]);
	}

	fh->cookie = (uint64_t)arg;

	return 0;
}

static int
flush_copy(daos_handle_t fh, d_sg_list_t *sgl)
{
	struct test_arg *arg = (struct test_arg *)fh.cookie;
	int              i;
	int              idx;

	assert_int_equal(sgl->sg_nr, arg->ta_nr[arg->ta_current_page]);
	for (i = arg->ta_current_idx; i < arg->ta_nr[arg->ta_current_page] + arg->ta_current_idx;
	     i++) {
		idx = i - arg->ta_current_idx;
		assert_int_equal(sgl->sg_iovs[idx].iov_buf,
				 (void *)(arg->ta_offsets[i] + UMEM_CACHE_PAGE_SZ));
		assert_int_equal(sgl->sg_iovs[idx].iov_len, arg->ta_sizes[i]);
	}

	arg->ta_current_idx += arg->ta_nr[arg->ta_current_page];
	arg->ta_current_page++;

	return 0;
}

static int
flush_post(daos_handle_t fh, int err)
{
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

static struct umem_store_ops stor_ops = {
    .so_flush_prep = flush_prep,
    .so_flush_copy = flush_copy,
    .so_flush_post = flush_post,
    .so_wal_id_cmp = wal_id_cmp,
};

static void
wait_cb(struct umem_store *store, uint64_t chkpt_tx, uint64_t *committed_tx, void *arg)
{
	*committed_tx = chkpt_tx;
}

static void
test_page_cache(void **state)
{
	struct test_arg   *arg = *state;
	struct umem_cache *cache;
	uint64_t           id = 0;
	int                rc;

	arg->ta_store.stor_size = 46 * 1024 * 1024;
	arg->ta_store.stor_ops  = &stor_ops;

	rc = umem_cache_alloc(&arg->ta_store, 0);
	assert_rc_equal(rc, 0);

	cache = arg->ta_store.cache;
	assert_non_null(cache);
	assert_int_equal(cache->ca_num_pages, 3);
	assert_int_equal(cache->ca_max_mapped, 3);

	rc = umem_cache_map_range(&arg->ta_store, 0, (void *)(UMEM_CACHE_PAGE_SZ), 3);
	assert_rc_equal(rc, 0);

	/** touch multiple chunks */
	rc = umem_cache_touch(&arg->ta_store, 1, 0, UMEM_CACHE_CHUNK_SZ + 1);
	assert_rc_equal(rc, 0);

	/** Span page boundary */
	rc = umem_cache_touch(&arg->ta_store, 2, UMEM_CACHE_PAGE_SZ - 1, UMEM_CACHE_CHUNK_SZ);
	assert_rc_equal(rc, 0);

	/** Touch the last page, different tx id */
	rc = umem_cache_touch(&arg->ta_store, 3, 2 * UMEM_CACHE_PAGE_SZ + 1, 10);
	assert_rc_equal(rc, 0);

	/** Touch many chunks on last page */
	rc = umem_cache_touch(&arg->ta_store, 3,
			      2 * UMEM_CACHE_PAGE_SZ + (UMEM_CACHE_CHUNK_SZ * 2) + 1,
			      UMEM_CACHE_CHUNK_SZ * 80);
	assert_rc_equal(rc, 0);

	arg->ta_current_page = 0;
	/** page 0 */
	arg->ta_nr[0]      = 2;
	arg->ta_offsets[0] = 0;
	arg->ta_sizes[0]   = UMEM_CACHE_CHUNK_SZ * 2;
	arg->ta_offsets[1] = UMEM_CACHE_PAGE_SZ - UMEM_CACHE_CHUNK_SZ;
	arg->ta_sizes[1]   = UMEM_CACHE_CHUNK_SZ;

	/** page 1 */
	arg->ta_nr[1]      = 1;
	arg->ta_offsets[2] = UMEM_CACHE_PAGE_SZ;
	arg->ta_sizes[2]   = UMEM_CACHE_CHUNK_SZ;

	/** page 2 */
	arg->ta_nr[2]      = 3;
	arg->ta_offsets[3] = 2 * UMEM_CACHE_PAGE_SZ;
	arg->ta_sizes[3]   = UMEM_CACHE_CHUNK_SZ;
	/** Size won't span more than one 64-bit mask worth of chunks */
	arg->ta_offsets[4] = 2 * UMEM_CACHE_PAGE_SZ + UMEM_CACHE_CHUNK_SZ * 2;
	arg->ta_sizes[4]   = UMEM_CACHE_CHUNK_SZ * 62;
	arg->ta_offsets[5] = 2 * UMEM_CACHE_PAGE_SZ + UMEM_CACHE_CHUNK_SZ * 64;
	arg->ta_sizes[5]   = UMEM_CACHE_CHUNK_SZ * 19;

	rc = umem_cache_checkpoint(&arg->ta_store, wait_cb, NULL, &id);
	assert_rc_equal(rc, 0);
	assert_int_equal(id, 3);

	/** This should be a noop so set ta_nr to ridiculous value that will assert */
	arg->ta_nr[0]        = 1000;
	arg->ta_current_page = 0;
	arg->ta_current_idx  = 0;
	rc                   = umem_cache_checkpoint(&arg->ta_store, wait_cb, NULL, &id);
	assert_rc_equal(rc, 0);
	assert_int_equal(id, 3);

	/* Ok, do another round */
	rc = umem_cache_touch(&arg->ta_store, 4, 10, 40);
	assert_rc_equal(rc, 0);

	rc = umem_cache_touch(&arg->ta_store, 5, 80, 40);
	assert_rc_equal(rc, 0);

	arg->ta_nr[0]      = 1;
	arg->ta_offsets[0] = 0;
	arg->ta_sizes[0]   = UMEM_CACHE_CHUNK_SZ;

	rc = umem_cache_checkpoint(&arg->ta_store, wait_cb, NULL, &id);
	assert_rc_equal(rc, 0);
	assert_int_equal(id, 5);

	umem_cache_free(&arg->ta_store);
}

int
main(int argc, char **argv)
{
	static const struct CMUnitTest umem_tests[] = {
	    {"UMEM001: Test null flags pmem", test_invalid_flags, setup_pmem, teardown_pmem},
	    {"UMEM002: Test null flags vmem", test_invalid_flags, setup_vmem, teardown_vmem},
	    {"UMEM003: Test alloc pmem", test_alloc, setup_pmem, teardown_pmem},
	    {"UMEM004: Test alloc vmem", test_alloc, setup_vmem, teardown_vmem},
	    {"UMEM005: Test page cache", test_page_cache, NULL, NULL},
	    {NULL, NULL, NULL, NULL}};

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("umem tests", umem_tests,
					   global_setup, global_teardown);
}
