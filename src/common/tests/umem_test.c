/**
 * (C) Copyright 2019-2024 Intel Corporation.
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
#define MAX_CHUNKS 8192

struct chunk {
	uint64_t ch_off;
	uint64_t ch_size;
	d_list_t ch_link;
};

struct test_arg {
	struct utest_context	*ta_utx;
	uint64_t		*ta_root;
	char			*ta_pool_name;
	struct umem_store        ta_store;
	struct chunk             ta_chunks[MAX_CHUNKS];
	int                      ta_chunk_nr;
	d_list_t                 ta_prep_list;
	d_list_t                 ta_flush_list;
};

static void
reset_arg(struct test_arg *arg)
{
	arg->ta_chunk_nr = 0;
	D_INIT_LIST_HEAD(&arg->ta_prep_list);
	D_INIT_LIST_HEAD(&arg->ta_flush_list);
}

static void
touch_mem(struct test_arg *arg, uint64_t tx_id, uint64_t offset, uint64_t size)
{
	struct umem_cache *cache = arg->ta_store.cache;
	struct chunk *prep       = &arg->ta_chunks[arg->ta_chunk_nr++];
	struct chunk *flush      = &arg->ta_chunks[arg->ta_chunk_nr++];
	d_list_t     *prep_list  = &arg->ta_prep_list;
	d_list_t     *flush_list = &arg->ta_flush_list;
	int           rc;

	rc = umem_cache_touch(&arg->ta_store, tx_id, offset + cache->ca_base_off, size);
	assert_int_equal(rc, 0);

	prep->ch_off  = offset + cache->ca_base_off;
	prep->ch_size = size;
	d_list_add_tail(&prep->ch_link, prep_list);

	flush->ch_off  = offset + UMEM_CACHE_PAGE_SZ;
	flush->ch_size = size;
	d_list_add_tail(&flush->ch_link, flush_list);
}

static void
find_expected(struct test_arg *arg, const char *type, d_list_t *list, uint64_t start_region,
	      uint64_t end_region)
{
	struct chunk *chunk;
	struct chunk *new_chunk;
	struct chunk *temp;
	uint64_t      end_chunk;
	bool          found = false;

	d_list_for_each_entry_safe(chunk, temp, list, ch_link) {
		end_chunk = chunk->ch_off + chunk->ch_size;

		if (end_region <= chunk->ch_off || start_region >= end_chunk)
			continue;

		found = true;
		if (start_region <= chunk->ch_off && end_region >= end_chunk) {
			d_list_del(&chunk->ch_link);
			continue;
		}

		/** 3 possible cases
		 *  1. Chunk is covered at start
		 *  2. Chunk is covered at end
		 *  3. Chunk is covered in middle
		 */
		if (start_region > chunk->ch_off) {
			if (end_region < end_chunk) {
				/** Case 3 */
				new_chunk          = &arg->ta_chunks[arg->ta_chunk_nr++];
				new_chunk->ch_off  = end_region;
				new_chunk->ch_size = end_chunk - new_chunk->ch_off;
				d_list_add(&chunk->ch_link, list);
				/** Fall through to case 1 */
			}
			/** Case 1 */
			chunk->ch_size = start_region - chunk->ch_off;
		} else if (end_region < end_chunk) {
			/** Case 2 */
			chunk->ch_off  = end_region;
			chunk->ch_size = end_chunk - chunk->ch_off;
		} else {
			assert_true(0);
		}
	}

	if (!found) {
		fail_msg("Unexpected %s: off=" DF_U64 ", size=" DF_U64 "\n", type, start_region,
			 end_region);
		assert_true(0);
	}
}

static void
check_io_region(struct test_arg *arg, struct umem_store_region *region)
{
	find_expected(arg, "io_region", &arg->ta_prep_list, region->sr_addr,
		      region->sr_addr + region->sr_size);
}

static void
check_iov(struct test_arg *arg, d_iov_t *iov)
{
	find_expected(arg, "io_iov", &arg->ta_flush_list, (uint64_t)iov->iov_buf,
		      (uint64_t)iov->iov_buf + iov->iov_len);
}

static void
check_lists_empty(struct test_arg *arg)
{
	assert_true(d_list_empty(&arg->ta_flush_list));
	assert_true(d_list_empty(&arg->ta_prep_list));
}

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
			       sizeof(*arg->ta_root), NULL, &arg->ta_utx);
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
	int rc;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc) {
		print_message("Failed to init debug\n");
		return 1;
	}

	D_ALLOC_PTR(arg);
	if (arg == NULL) {
		print_message("Failed to allocate test struct\n");
		daos_debug_fini();
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
	daos_debug_fini();

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

	arg = container_of(store, struct test_arg, ta_store);

	for (i = 0; i < iod->io_nr; i++)
		check_io_region(arg, &iod->io_regions[i]);

	fh->cookie = (uint64_t)arg;

	return 0;
}

static int
flush_copy(daos_handle_t fh, d_sg_list_t *sgl)
{
	struct test_arg *arg = (struct test_arg *)fh.cookie;
	int              i;

	for (i = 0; i < sgl->sg_nr; i++)
		check_iov(arg, &sgl->sg_iovs[i]);

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
wait_cb(void *arg, uint64_t chkpt_tx, uint64_t *committed_tx)
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
	arg->ta_store.store_type = DAOS_MD_BMEM;

	rc = umem_cache_alloc(&arg->ta_store, UMEM_CACHE_PAGE_SZ, 3, 0, 0, 0,
			      (void *)(UMEM_CACHE_PAGE_SZ), NULL, NULL, NULL);
	assert_rc_equal(rc, 0);

	cache = arg->ta_store.cache;
	assert_non_null(cache);
	assert_int_equal(cache->ca_md_pages, 3);
	assert_int_equal(cache->ca_mem_pages, 3);

	reset_arg(arg);
	/** touch multiple chunks */
	touch_mem(arg, 1, 0, UMEM_CACHE_CHUNK_SZ + 1);

	/** Span page boundary */
	touch_mem(arg, 2, UMEM_CACHE_PAGE_SZ - 1, UMEM_CACHE_CHUNK_SZ);

	/** Touch the last page, different tx id */
	touch_mem(arg, 3, 2 * UMEM_CACHE_PAGE_SZ + 1, 10);

	/** Touch many chunks on last page */
	touch_mem(arg, 3, 2 * UMEM_CACHE_PAGE_SZ + (UMEM_CACHE_CHUNK_SZ * 2) + 1,
		  UMEM_CACHE_CHUNK_SZ * 80);

	rc = umem_cache_checkpoint(&arg->ta_store, wait_cb, NULL, &id, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(id, 3);
	check_lists_empty(arg);

	/** This should be a noop so set ta_nr to ridiculous value that will assert */
	reset_arg(arg);
	rc = umem_cache_checkpoint(&arg->ta_store, wait_cb, NULL, &id, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(id, 3);

	/* Ok, do another round */
	touch_mem(arg, 4, 10, 40);

	touch_mem(arg, 5, 80, 40);

	rc = umem_cache_checkpoint(&arg->ta_store, wait_cb, NULL, &id, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(id, 5);
	check_lists_empty(arg);

	umem_cache_free(&arg->ta_store);
}

#define LARGE_NUM_PAGES  103
#define LARGE_CACHE_SIZE (LARGE_NUM_PAGES * UMEM_CACHE_PAGE_SZ)
static void
test_many_pages(void **state)
{
	struct test_arg   *arg = *state;
	struct umem_cache *cache;
	uint64_t           id = 0;
	uint64_t           offset;
	int                rc;
	uint64_t           tx_id;

	arg->ta_store.stor_size = LARGE_CACHE_SIZE;
	arg->ta_store.stor_ops  = &stor_ops;

	/** In case prior test failed */
	umem_cache_free(&arg->ta_store);

	rc = umem_cache_alloc(&arg->ta_store, UMEM_CACHE_PAGE_SZ, LARGE_NUM_PAGES, 0, 0, 0,
			      (void *)(UMEM_CACHE_PAGE_SZ), NULL, NULL, NULL);
	assert_rc_equal(rc, 0);

	cache = arg->ta_store.cache;
	assert_non_null(cache);
	assert_int_equal(cache->ca_md_pages, LARGE_NUM_PAGES);
	assert_int_equal(cache->ca_mem_pages, LARGE_NUM_PAGES);

	/** Touch all pages, more than can fit in a single set */
	reset_arg(arg);
	tx_id = 1;
	for (offset = 0; offset < LARGE_CACHE_SIZE; offset += UMEM_CACHE_PAGE_SZ) {
		touch_mem(arg, tx_id, offset, 10);

		tx_id++;

		touch_mem(arg, tx_id, offset + UMEM_CACHE_PAGE_SZ - 20, 10);
	}

	rc = umem_cache_checkpoint(&arg->ta_store, wait_cb, NULL, &id, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(id, LARGE_NUM_PAGES + 1);
	check_lists_empty(arg);

	umem_cache_free(&arg->ta_store);
}

static void
test_many_writes(void **state)
{
	struct test_arg   *arg = *state;
	struct umem_cache *cache;
	uint64_t           id = 0;
	uint64_t           offset;
	int                rc;
	uint64_t           tx_id;

	arg->ta_store.stor_size = LARGE_CACHE_SIZE;
	arg->ta_store.stor_ops  = &stor_ops;

	/** In case prior test failed */
	umem_cache_free(&arg->ta_store);

	rc = umem_cache_alloc(&arg->ta_store, UMEM_CACHE_PAGE_SZ, LARGE_NUM_PAGES, 0, 0, 0,
			      (void *)(UMEM_CACHE_PAGE_SZ), NULL, NULL, NULL);
	assert_rc_equal(rc, 0);

	cache = arg->ta_store.cache;
	assert_non_null(cache);
	assert_int_equal(cache->ca_md_pages, LARGE_NUM_PAGES);
	assert_int_equal(cache->ca_mem_pages, LARGE_NUM_PAGES);

	/** Touch all pages, more than can fit in a single set */
	reset_arg(arg);
	offset = 1;
	for (tx_id = 1; tx_id < 3800; tx_id++) {
		touch_mem(arg, tx_id, offset, 10);
		offset += UMEM_CACHE_CHUNK_SZ * 3 + 1;
	}

	rc = umem_cache_checkpoint(&arg->ta_store, wait_cb, NULL, &id, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(id, tx_id - 1);
	check_lists_empty(arg);

	umem_cache_free(&arg->ta_store);
}

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
	return 0;
}

static struct umem_store_ops p2_ops = {
	.so_waitqueue_create	= waitqueue_create,
	.so_waitqueue_destroy	= waitqueue_destroy,
	.so_waitqueue_wait	= waitqueue_wait,
	.so_waitqueue_wakeup	= waitqueue_wakeup,
	.so_load		= store_load,
	.so_flush_prep		= flush_prep,
	.so_flush_copy		= flush_copy,
	.so_flush_post		= flush_post,
	.so_wal_id_cmp		= wal_id_cmp,
};

#define PAGE_NUM_MD	20
#define PAGE_NUM_MEM	10
#define PAGE_NUM_MAX_NE	5

static bool
is_evictable_fn(void *arg, uint32_t page_id)
{
	return page_id >= PAGE_NUM_MAX_NE;
}

static int
pagevnt_fn(int event_type, void *arg, uint32_t page_id)
{
	return 0;
}

static void
test_p2_basic(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_cache	*cache;
	struct umem_cache_range	 rg = { 0 };
	struct umem_pin_handle	*pin_hdl;
	int			 rc;

	arg->ta_store.stor_size = UMEM_CACHE_PAGE_SZ * PAGE_NUM_MD;
	arg->ta_store.stor_ops  = &p2_ops;
	arg->ta_store.store_type = DAOS_MD_BMEM;

	rc = umem_cache_alloc(&arg->ta_store, UMEM_CACHE_PAGE_SZ, PAGE_NUM_MD, PAGE_NUM_MEM,
			      PAGE_NUM_MAX_NE, 4096, (void *)(UMEM_CACHE_PAGE_SZ), is_evictable_fn,
			      pagevnt_fn, NULL);
	assert_rc_equal(rc, 0);

	cache = arg->ta_store.cache;
	assert_non_null(cache);

	reset_arg(arg);

	/* Load single page */
	rg.cr_off	= cache->ca_base_off;
	rg.cr_size	= UMEM_CACHE_PAGE_SZ;
	rc = umem_cache_load(&arg->ta_store, &rg, 1, false);
	assert_rc_equal(rc, 0);
	assert_int_equal(cache->ca_pgs_stats[UMEM_PG_STATS_NONEVICTABLE], 1);
	assert_ptr_equal(umem_cache_off2ptr(&arg->ta_store, cache->ca_base_off), cache->ca_base);

	/* Map single non-evictable page */
	rg.cr_off	= cache->ca_base_off + 1 * UMEM_CACHE_PAGE_SZ;
	rg.cr_size	= UMEM_CACHE_PAGE_SZ;
	rc = umem_cache_map(&arg->ta_store, &rg, 1);
	assert_rc_equal(rc, 0);
	assert_int_equal(cache->ca_pgs_stats[UMEM_PG_STATS_NONEVICTABLE], 2);
	assert_ptr_equal(umem_cache_off2ptr(&arg->ta_store,
				cache->ca_base_off + UMEM_CACHE_PAGE_SZ),
			 cache->ca_base + UMEM_CACHE_PAGE_SZ);

	/* Load multiple pages */
	rg.cr_off	= cache->ca_base_off + (PAGE_NUM_MAX_NE - 1) * UMEM_CACHE_PAGE_SZ;
	rg.cr_size	= 3 * UMEM_CACHE_PAGE_SZ;
	rc = umem_cache_load(&arg->ta_store, &rg, 1, false);
	assert_rc_equal(rc, 0);
	assert_int_equal(cache->ca_pgs_stats[UMEM_PG_STATS_NONEVICTABLE], 3);

	/* Pin multiple pages */
	rg.cr_off	= cache->ca_base_off + (PAGE_NUM_MAX_NE - 1) * UMEM_CACHE_PAGE_SZ;
	rg.cr_size	= 2 * UMEM_CACHE_PAGE_SZ;
	rc = umem_cache_pin(&arg->ta_store, &rg, 1, false, &pin_hdl);
	assert_rc_equal(rc, 0);
	assert_non_null(pin_hdl);
	assert_int_equal(cache->ca_pgs_stats[UMEM_PG_STATS_PINNED], 1);

	/* Unpin the pinned pages */
	umem_cache_unpin(&arg->ta_store, pin_hdl);
	assert_int_equal(cache->ca_pgs_stats[UMEM_PG_STATS_PINNED], 0);

	/* Reserve free pages */
	rc = umem_cache_reserve(&arg->ta_store);
	assert_rc_equal(rc, 0);

	umem_cache_free(&arg->ta_store);
}

static void
test_p2_evict(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_cache	*cache;
	struct umem_cache_range	 rg = { 0 };
	struct umem_pin_handle	*pin_hdl;
	uint64_t		 id;
	int			 i, rc;

	arg->ta_store.stor_size = UMEM_CACHE_PAGE_SZ * PAGE_NUM_MD;
	arg->ta_store.stor_ops  = &p2_ops;
	arg->ta_store.store_type = DAOS_MD_BMEM;

	rc = umem_cache_alloc(&arg->ta_store, UMEM_CACHE_PAGE_SZ, PAGE_NUM_MD, PAGE_NUM_MEM,
			      PAGE_NUM_MAX_NE, 4096, (void *)(UMEM_CACHE_PAGE_SZ), is_evictable_fn,
			      pagevnt_fn, NULL);
	assert_rc_equal(rc, 0);

	cache = arg->ta_store.cache;
	assert_non_null(cache);

	reset_arg(arg);

	/* Load all non-evictable pages */
	rg.cr_off	= cache->ca_base_off;
	rg.cr_size	= PAGE_NUM_MAX_NE * UMEM_CACHE_PAGE_SZ;
	rc = umem_cache_load(&arg->ta_store, &rg, 1, false);
	assert_rc_equal(rc, 0);
	assert_int_equal(cache->ca_pgs_stats[UMEM_PG_STATS_NONEVICTABLE], PAGE_NUM_MAX_NE);

	/* Load more pages to fill the cache */
	rg.cr_off	= cache->ca_base_off + PAGE_NUM_MAX_NE * UMEM_CACHE_PAGE_SZ;
	rg.cr_size	= (PAGE_NUM_MEM - PAGE_NUM_MAX_NE) * UMEM_CACHE_PAGE_SZ;
	rc = umem_cache_load(&arg->ta_store, &rg, 1, false);
	assert_rc_equal(rc, 0);
	assert_int_equal(cache->ca_pgs_stats[UMEM_PG_STATS_NONEVICTABLE], PAGE_NUM_MAX_NE);

	/* Dirty all pages */
	for (i = 0; i < PAGE_NUM_MEM; i++) {
		touch_mem(arg, i + 1, i * UMEM_CACHE_PAGE_SZ, UMEM_CACHE_CHUNK_SZ);
		umem_cache_commit(&arg->ta_store, i + 1);
	}
	id = PAGE_NUM_MEM;

	/* Pin an unmapped page to trigger eviction */
	rg.cr_off	= cache->ca_base_off + PAGE_NUM_MEM * UMEM_CACHE_PAGE_SZ;
	rg.cr_size	= 100;
	rc = umem_cache_pin(&arg->ta_store, &rg, 1, false, &pin_hdl);
	assert_rc_equal(rc, 0);
	assert_int_equal(cache->ca_pgs_stats[UMEM_PG_STATS_PINNED], 1);

	umem_cache_unpin(&arg->ta_store, pin_hdl);
	assert_int_equal(cache->ca_pgs_stats[UMEM_PG_STATS_PINNED], 0);

	rc = umem_cache_checkpoint(&arg->ta_store, wait_cb, NULL, &id, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(id, PAGE_NUM_MEM);
	check_lists_empty(arg);

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
	    {"UMEM006: Test page cache many pages", test_many_pages, NULL, NULL},
	    {"UMEM007: Test page cache many writes", test_many_writes, NULL, NULL},
	    {"UMEM008: Test phase2 APIs", test_p2_basic, NULL, NULL},
	    {"UMEM009: Test phase2 eviction", test_p2_evict, NULL, NULL},
	    {NULL, NULL, NULL, NULL}};

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("umem tests", umem_tests,
					   global_setup, global_teardown);
}
