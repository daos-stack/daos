/*
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos/common.h>
#include <daos/tests_lib.h>
#include "../ad_mem.h"

#define ADT_STORE_SIZE	(256 << 20)
#define HDR_SIZE	(32 << 10)

static struct ad_blob_handle adt_bh;
static char	*adt_store;

static void
addr_swap(void *array, int a, int b)
{
        daos_off_t	*addrs = array;
        daos_off_t	 tmp;

        tmp = addrs[a];
        addrs[a] = addrs[b];
        addrs[b] = tmp;
}

static daos_sort_ops_t addr_shuffle_ops = {
	.so_swap	= addr_swap,
};

static void
adt_addrs_shuffle(daos_off_t *addrs, int nr)
{
	daos_array_shuffle((void *)addrs, nr, &addr_shuffle_ops);
}

static int
adt_store_read(struct umem_store *store, struct umem_store_iod *iod, d_sg_list_t *sgl)
{
	struct umem_store_region *region;

	D_ASSERT(iod->io_nr == 1);
	D_ASSERT(sgl->sg_nr == 1);

	region = &iod->io_regions[0];
	memcpy(sgl->sg_iovs[0].iov_buf, &adt_store[region->sr_addr], region->sr_size);
	printf("Read %d bytes from store address %lu\n",
	       (int)region->sr_size, (unsigned long)region->sr_addr);

	return 0;
}

static int
adt_store_write(struct umem_store *store, struct umem_store_iod *iod, d_sg_list_t *sgl)
{
	struct umem_store_region *region;

	D_ASSERT(iod->io_nr == 1);
	D_ASSERT(sgl->sg_nr == 1);

	region = &iod->io_regions[0];
	memcpy(&adt_store[region->sr_addr], sgl->sg_iovs[0].iov_buf, region->sr_size);
	printf("Write %d bytes to store address %lu\n",
	       (int)region->sr_size, (unsigned long)region->sr_addr);

	return 0;
}

static uint64_t  wal_id;

static int
adt_store_wal_rsv(struct umem_store *store, uint64_t *id)
{
	*id = wal_id++;
	return 0;
}

static int
adt_store_wal_submit(struct umem_store *store, uint64_t id, d_list_t *actions)
{
	return 0;
}

struct umem_store_ops adt_store_ops = {
	.so_read	= adt_store_read,
	.so_write	= adt_store_write,
	.so_wal_reserv	= adt_store_wal_rsv,
	.so_wal_submit	= adt_store_wal_submit,
};

static void
adt_blob_create(void **state)
{
	struct umem_store	*store;
	struct ad_blob_handle	 bh;
	int	rc;

	printf("prep create ad_blob\n");
	rc = ad_blob_prep_create(DUMMY_BLOB, ADT_STORE_SIZE, &bh);
	assert_rc_equal(rc, 0);

	store = ad_blob_hdl2store(bh);
	store->stor_ops = &adt_store_ops;

	printf("post create ad_blob\n");
	rc = ad_blob_post_create(bh);
	assert_rc_equal(rc, 0);

	printf("close ad_blob\n");
	rc = ad_blob_close(bh);
	assert_rc_equal(rc, 0);
}

static void adt_reserve_cancel_1(void **state)
{
	const int	     alloc_size = 128;
	struct ad_reserv_act act;
	daos_off_t	     addr;
	daos_off_t	     addr_saved;
	uint32_t	     arena = AD_ARENA_ANY;

	printf("reserve and cancel\n");
	addr = ad_reserve(adt_bh, 0, alloc_size, &arena, &act);
	if (addr == 0) {
		fprintf(stderr, "failed allocate\n");
		return;
	}
	addr_saved = addr;
	ad_cancel(&act, 1);

	printf("another reserve should have the same address\n");
	addr = ad_reserve(adt_bh, 0, alloc_size, &arena, &act);
	if (addr == 0) {
		fprintf(stderr, "failed allocate\n");
		return;
	}
	assert_int_equal(addr, addr_saved);
	ad_cancel(&act, 1);
}

static void
adt_reserve_cancel_2(void **state)
{
	const int	     alloc_size = 128;
	const int	     rsv_count = 16;
	struct ad_reserv_act acts[rsv_count];
	daos_off_t	     addrs[rsv_count];
	int		     i;
	uint32_t	     arena = AD_ARENA_ANY;

	printf("multiple reserve and cancel\n");
	for (i = 0; i < rsv_count; i++) {
		addrs[i] = ad_reserve(adt_bh, 0, alloc_size, &arena, &acts[i]);
		if (addrs[i] == 0) {
			fprintf(stderr, "failed allocate\n");
			return;
		}
		printf("reserved address=%lx\n", (unsigned long)addrs[i]);
	}
	ad_cancel(acts, rsv_count);
}

static void
adt_reserve_pub_1(void **state)
{
	const int	     alloc_size = 48;
	struct ad_tx	     tx;
	struct ad_reserv_act act;
	daos_off_t	     addr;
	daos_off_t	     addr_saved;
	int		     rc;
	int		     i;
	uint32_t	     arena = AD_ARENA_ANY;

	printf("Reserve and publish in a loop\n");
	for (i = 0; i < 1024; i++) {
		addr = ad_reserve(adt_bh, 0, alloc_size, &arena, &act);
		if (addr == 0) {
			fprintf(stderr, "failed allocate\n");
			return;
		}
		rc = ad_tx_begin(adt_bh, &tx);
		assert_rc_equal(rc, 0);

		rc = ad_tx_publish(&tx, &act, 1);
		assert_rc_equal(rc, 0);

		rc = ad_tx_end(&tx, 0);
		assert_rc_equal(rc, 0);

		addr_saved = addr;

		/* Another reserve should have different address */
		addr = ad_reserve(adt_bh, 0, alloc_size, &arena, &act);
		if (addr == 0) {
			fprintf(stderr, "failed allocate\n");
			return;
		}
		assert_int_not_equal(addr, addr_saved);
		ad_cancel(&act, 1);
	}
}

static void
adt_reserve_pub_2(void **state)
{
	const int	     alloc_size = 512;
	const int	     rsv_count = 16;
	struct ad_tx	     tx;
	struct ad_reserv_act acts[rsv_count];
	daos_off_t	     addrs[rsv_count];
	int		     rc;
	int		     i;
	uint32_t	     arena = AD_ARENA_ANY;

	printf("multiple reserve and one publish\n");
	for (i = 0; i < rsv_count; i++) {
		addrs[i] = ad_reserve(adt_bh, 0, alloc_size, &arena, &acts[i]);
		if (addrs[i]== 0) {
			fprintf(stderr, "failed allocate\n");
			return;
		}
		printf("reserved address=%lx\n", (unsigned long)addrs[i]);
	}

	printf("publishing reserved addresses\n");

	rc = ad_tx_begin(adt_bh, &tx);
	assert_rc_equal(rc, 0);

	rc = ad_tx_publish(&tx, acts, rsv_count);
	assert_rc_equal(rc, 0);

	rc = ad_tx_end(&tx, 0);
	assert_rc_equal(rc, 0);
}

static void
adt_reserve_free_1(void **state)
{
	const int	     alloc_size = 256;
	struct ad_tx	     tx;
	struct ad_reserv_act act;
	daos_off_t	     addr;
	int		     rc;
	uint32_t	     arena = AD_ARENA_ANY;

	printf("Reserve and publish space\n");
	addr = ad_reserve(adt_bh, 0, alloc_size, &arena, &act);
	if (addr == 0) {
		fprintf(stderr, "failed allocate\n");
		return;
	}
	rc = ad_tx_begin(adt_bh, &tx);
	assert_rc_equal(rc, 0);

	rc = ad_tx_publish(&tx, &act, 1);
	assert_rc_equal(rc, 0);

	rc = ad_tx_end(&tx, 0);
	assert_rc_equal(rc, 0);

	printf("Free space\n");
	rc = ad_tx_begin(adt_bh, &tx);
	assert_rc_equal(rc, 0);

	rc = ad_tx_free(&tx, addr);

	rc = ad_tx_end(&tx, 0);
	assert_rc_equal(rc, 0);
}

static void
adt_reserve_free_2(void **state)
{
	const int	     alloc_size = 96;
	const int	     rsv_count = 1024;
	struct ad_tx	     tx;
	struct ad_reserv_act acts[rsv_count];
	daos_off_t	     addrs[rsv_count];
	int		     i;
	int		     rc;
	uint32_t	     arena = AD_ARENA_ANY;

	printf("Multiple frees in one transaction\n");
	for (i = 0; i < rsv_count; i++) {
		addrs[i] = ad_reserve(adt_bh, 0, alloc_size, &arena, &acts[i]);
		if (addrs[i] == 0) {
			fprintf(stderr, "failed allocate\n");
			return;
		}
	}

	rc = ad_tx_begin(adt_bh, &tx);
	assert_rc_equal(rc, 0);

	rc = ad_tx_publish(&tx, acts, rsv_count);
	assert_rc_equal(rc, 0);

	rc = ad_tx_end(&tx, 0);
	assert_rc_equal(rc, 0);

	adt_addrs_shuffle(addrs, rsv_count);

	printf("Free addresses in random order\n");
	rc = ad_tx_begin(adt_bh, &tx);
	assert_rc_equal(rc, 0);

	for (i = 0; i < rsv_count; i++) {
		rc = ad_tx_free(&tx, addrs[i]);
		assert_rc_equal(rc, 0);
	}
	rc = ad_tx_end(&tx, 0);
	assert_rc_equal(rc, 0);
}

static int
adt_setup(void **state)
{
	struct umem_store *store;
	int		   rc;

	adt_blob_create(state);

	printf("prep open ad_blob\n");
	rc = ad_blob_prep_open(DUMMY_BLOB, &adt_bh);
	assert_rc_equal(rc, 0);

	store = ad_blob_hdl2store(adt_bh);
	store->stor_ops = &adt_store_ops;

	printf("post open ad_blob\n");
	rc = ad_blob_post_open(adt_bh);
	assert_rc_equal(rc, 0);
	assert_int_equal(store->stor_size, ADT_STORE_SIZE);

	return 0;
}

static int
adt_teardown(void **state)
{
	int	rc;

	printf("close ad_blob\n");
	rc = ad_blob_close(adt_bh);
	assert_rc_equal(rc, 0);
	return 0;
}

int
main(void)
{
	const struct CMUnitTest ad_mem_tests[] = {
		cmocka_unit_test(adt_reserve_cancel_1),
		cmocka_unit_test(adt_reserve_cancel_2),
		cmocka_unit_test(adt_reserve_pub_1),
		cmocka_unit_test(adt_reserve_pub_2),
		cmocka_unit_test(adt_reserve_free_1),
		cmocka_unit_test(adt_reserve_free_2),
	};
	int	rc;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	assert_rc_equal(rc, 0);

	D_ALLOC(adt_store, ADT_STORE_SIZE);
	if (!adt_store) {
		fprintf(stderr, "No memory\n");
		return -1;
	}

	rc = cmocka_run_group_tests_name("ad_mem_tests", ad_mem_tests, adt_setup, adt_teardown);

	daos_debug_fini();
	D_FREE(adt_store);
	return rc;
}
