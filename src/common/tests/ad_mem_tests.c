/*
 * (C) Copyright 2022-2023 Intel Corporation.
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

#define ADT_STORE_SIZE	(384 << 20)

static struct ad_blob_handle adt_bh;
static char	*adt_store;
static int	 adt_arena_type = ARENA_TYPE_BASE + 0;

static void
addr_swap(void *array, int a, int b)
{
	daos_off_t *addrs = array;
	daos_off_t  tmp;

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
adt_store_wal_submit(struct umem_store *store, struct umem_wal_tx *wal_tx, void *data_iod)
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
	struct umem_store	store = {0};
	struct ad_blob_handle	bh;
	int	rc;

	printf("prep create ad_blob\n");
	store.stor_size	= ADT_STORE_SIZE;
	store.stor_ops	= &adt_store_ops;
	rc = ad_blob_create(DUMMY_BLOB, 0, &store, &bh);
	assert_rc_equal(rc, 0);

	printf("close ad_blob\n");
	rc = ad_blob_close(bh);
	assert_rc_equal(rc, 0);
}

#define UD_BUF_SIZE	64

struct undo_data {
	uint8_t		set_8[2];
	uint16_t	set_16[2];
	uint32_t	set_32[2];
	uint64_t	set_64[2];
	uint32_t	sbt_32[2];
	uint32_t	cbt_32[2];
	int32_t		inc_32[2];
	int32_t		dec_32[2];
	char		snap_buf[UD_BUF_SIZE];
};

static void
adt_undo_1(void **state)
{
	struct undo_data     *ud;
	struct ad_reserv_act  act;
	daos_off_t	      addr;
	struct ad_tx	      tx;
	int		      rc;
	uint32_t	      arena = AD_ARENA_ANY;

	/* NB: redo & undo can only work on memory managed by allocator */
	addr = ad_reserve(adt_bh, 0, sizeof(*ud), &arena, &act);
	D_ASSERT(addr != 0);

	ud = ad_addr2ptr(adt_bh, addr);
	ud->set_8[0]  = ud->set_8[1]  = 0xbe;
	ud->set_16[0] = ud->set_16[1] = 0xcafe;
	ud->set_32[0] = ud->set_32[1] = 0xbabecafe;
	ud->set_64[0] = ud->set_64[1] = 0xbeef0010babecafe;
	ud->inc_32[0] = ud->inc_32[1] = 2;
	ud->dec_32[0] = ud->dec_32[1] = 2;
	ud->sbt_32[0] = ud->sbt_32[1] = 0x0;
	ud->cbt_32[0] = ud->cbt_32[1] = 0x3;
	memset(ud->snap_buf, 0x5a, UD_BUF_SIZE); /* initialize buffer */

	rc = ad_tx_begin(adt_bh, &tx);
	assert_rc_equal(rc, 0);

	rc = ad_tx_set(&tx, &ud->set_8[0], 0, 1, AD_TX_UNDO);
	assert_rc_equal(rc, 0);
	assert_int_equal(ud->set_8[0], 0);

	rc = ad_tx_set(&tx, &ud->set_16[0], 0, 2, AD_TX_UNDO);
	assert_rc_equal(rc, 0);
	assert_int_equal(ud->set_16[0], 0);

	rc = ad_tx_set(&tx, &ud->set_32[0], 0, 4, AD_TX_UNDO);
	assert_rc_equal(rc, 0);
	assert_int_equal(ud->set_32[0], 0);

	rc = ad_tx_set(&tx, &ud->set_64[0], 0, 8, AD_TX_UNDO);
	assert_rc_equal(rc, 0);
	assert_int_equal(ud->set_64[0], 0);

	rc = ad_tx_increase(&tx, &ud->inc_32[0], AD_TX_UNDO);
	assert_rc_equal(rc, 0);
	assert_int_equal(ud->inc_32[0], (ud->inc_32[1] + 1));

	rc = ad_tx_decrease(&tx, &ud->dec_32[0], AD_TX_UNDO);
	assert_rc_equal(rc, 0);
	assert_int_equal(ud->dec_32[0], (ud->dec_32[1] - 1));

	rc = ad_tx_setbits(&tx, &ud->sbt_32[0], 0, 2);
	assert_rc_equal(rc, 0);
	assert_int_equal(ud->sbt_32[0], 3);

	rc = ad_tx_clrbits(&tx, &ud->cbt_32[0], 0, 2);
	assert_rc_equal(rc, 0);
	assert_int_equal(ud->cbt_32[0], 0);

	rc = ad_tx_snap(&tx, ud->snap_buf, UD_BUF_SIZE, AD_TX_UNDO);
	assert_rc_equal(rc, 0);
	memset(ud->snap_buf, 0, UD_BUF_SIZE);

	rc = ad_tx_end(&tx, -37); /* abort all changes */
	assert_rc_equal(rc, -37);

	/* all the old values should be restored */
	printf("check undo results of set_value\n");
	assert_int_equal(ud->set_8[0], ud->set_8[1]);
	assert_int_equal(ud->set_16[0], ud->set_16[1]);
	assert_int_equal(ud->set_32[0], ud->set_32[1]);
	assert_int_equal(ud->set_64[0], ud->set_64[1]);

	printf("check undo results of increase and decrease\n");
	assert_int_equal(ud->inc_32[0], ud->inc_32[1]);
	assert_int_equal(ud->dec_32[0], ud->dec_32[1]);

	printf("check undo results of setbits and clrbits\n");
	assert_int_equal(ud->sbt_32[0], ud->sbt_32[1]);
	assert_int_equal(ud->cbt_32[0], ud->cbt_32[1]);

	printf("check undo results of snapped memory\n");
	assert_int_equal(ud->snap_buf[0], 0x5a);
	assert_int_equal(ud->snap_buf[UD_BUF_SIZE - 1], 0x5a);

	ad_cancel(&act, 1);
}

static void
adt_rsv_cancel_1(void **state)
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
adt_rsv_cancel_2(void **state)
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
adt_rsv_pub_abort_1(void **state)
{
	const int	     alloc_size = 512;
	struct ad_tx	     tx;
	struct ad_reserv_act act;
	daos_off_t	     addr;
	daos_off_t	     addr_saved;
	int		     rc;
	uint32_t	     arena = AD_ARENA_ANY;

	printf("Reserve, publish and abort transaction\n");
	addr = ad_reserve(adt_bh, 0, alloc_size, &arena, &act);
	if (addr == 0) {
		fprintf(stderr, "failed allocate\n");
		return;
	}
	rc = ad_tx_begin(adt_bh, &tx);
	assert_rc_equal(rc, 0);

	rc = ad_tx_publish(&tx, &act, 1);
	assert_rc_equal(rc, 0);

	rc = ad_tx_end(&tx, -37); /* abort transaction */
	assert_rc_equal(rc, -37);

	addr_saved = addr;

	/* Another reserve should have the same address */
	addr = ad_reserve(adt_bh, 0, alloc_size, &arena, &act);
	if (addr == 0) {
		fprintf(stderr, "failed allocate\n");
		return;
	}
	assert_int_equal(addr, addr_saved);
	ad_cancel(&act, 1);
}

static void
adt_rsv_pub_abort_2(void **state)
{
	const int	     alloc_size = 4096;
	const int	     rsv_count = 4100; /* cross arena boundary */
	struct ad_tx	     tx;
	struct ad_reserv_act acts[rsv_count];
	daos_off_t	     addrs[rsv_count];
	int		     rc;
	int		     i;
	uint32_t	     arena = AD_ARENA_ANY;
	uint32_t	     arena_old = arena;

	printf("Reserve many, publish and abort transaction\n");
	for (i = 0; i < rsv_count; i++) {
		addrs[i] = ad_reserve(adt_bh, 0, alloc_size, &arena, &acts[i]);
		if (addrs[i]== 0) {
			fprintf(stderr, "failed allocate\n");
			return;
		}
		if (arena_old != arena) {
			printf("Switch from arena %d to arena %d\n", arena_old, arena);
			arena_old = arena;
		}
	}

	rc = ad_tx_begin(adt_bh, &tx);
	assert_rc_equal(rc, 0);

	rc = ad_tx_publish(&tx, acts, rsv_count);
	assert_rc_equal(rc, 0);

	rc = ad_tx_end(&tx, -37);
	assert_rc_equal(rc, -37);
}

static void
adt_rsv_pub_1(void **state)
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
adt_rsv_pub_2(void **state)
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
adt_rsv_pub_3(void **state)
{
	const int	     alloc_size = 64;
	const int	     rsv_count = 1024;
	struct ad_tx	     tx;
	struct ad_reserv_act acts[rsv_count];
	daos_off_t	     addrs[rsv_count];
	int		     i;
	int		     c;
	int		     rc;
	uint32_t	     arena = AD_ARENA_ANY;

	printf("Mixed reserved and cancel\n");
	for (i = c = 0; i < rsv_count; i++) {
		addrs[i] = ad_reserve(adt_bh, 0, alloc_size, &arena, &acts[c]);
		if (addrs[i] == 0) {
			fprintf(stderr, "failed allocate\n");
			return;
		}
		if (i % 3 == 0)
			ad_cancel(&acts[c], 1);
		else
			c++;
	}

	rc = ad_tx_begin(adt_bh, &tx);
	assert_rc_equal(rc, 0);

	rc = ad_tx_publish(&tx, acts, c);
	assert_rc_equal(rc, 0);

	rc = ad_tx_end(&tx, 0);
	assert_rc_equal(rc, 0);
}

static void
adt_rsv_pub_4(void **state)
{
	const int	     alloc_size = 4096;
	const int	     rsv_count = 1024;
	const int	     loop = 6; /* (6 * 1024 * 4096) cross arena boundary */
	struct ad_tx	     tx;
	struct ad_reserv_act acts[rsv_count];
	daos_off_t	     addrs[rsv_count];
	int		     rc;
	int		     i;
	int		     j;
	uint32_t	     arena = AD_ARENA_ANY;

	printf("Crossing arena boundary allocation\n");
	for (i = 0; i < loop; i++) {
		for (j = 0; j < rsv_count; j++) {
			addrs[j] = ad_reserve(adt_bh, 0, alloc_size, &arena, &acts[j]);
			if (addrs[j] == 0) {
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
		printf("Published allocation: size = %d KB, arena = %u\n",
		       ((i + 1) * alloc_size * rsv_count) >> 10, arena);
	}
}

static void
adt_rsv_pub_5(void **state)
{
	const int	     alloc_large1 = (8 << 10);
	const int	     alloc_large2 = (1 << 20);
	const int	     rsv_count = 16;
	struct ad_tx	     tx;
	struct ad_reserv_act acts[rsv_count * 2];
	daos_off_t	     addrs[rsv_count * 2];
	int		     rc;
	int		     i;
	uint32_t	     arena = AD_ARENA_ANY;

	printf("Reserve large space and publish\n");
	for (i = 0; i < rsv_count * 2;) {
		addrs[i] = ad_reserve(adt_bh, ARENA_TYPE_LARGE, alloc_large1, &arena, &acts[i]);
		if (addrs[i] == 0) {
			fprintf(stderr, "failed allocate size=%d\n", alloc_large1);
			return;
		}
		i++;
		addrs[i] = ad_reserve(adt_bh, ARENA_TYPE_LARGE, alloc_large2, &arena, &acts[i]);
		if (addrs[i]== 0) {
			fprintf(stderr, "failed allocate size=%d\n", alloc_large2);
			return;
		}
		i++;
	}

	printf("Publish reserved addresses\n");
	rc = ad_tx_begin(adt_bh, &tx);
	assert_rc_equal(rc, 0);

	rc = ad_tx_publish(&tx, acts, rsv_count * 2);
	assert_rc_equal(rc, 0);

	rc = ad_tx_end(&tx, 0);
	assert_rc_equal(rc, 0);
}

static void
adt_rsv_inval(void **state)
{
	const int	     alloc_size = 8192; /* unsupported size */
	struct ad_reserv_act act;
	daos_off_t	     addr;
	uint32_t	     arena = AD_ARENA_ANY;

	printf("reserve invalid size should fail\n");
	addr = ad_reserve(adt_bh, 0, alloc_size, &arena, &act);
	assert_int_equal(addr, 0);
}

static struct ad_group_spec adt_gsp[] = {
	{
		.gs_unit	= 512,
		.gs_count	= 256,
	},
	{
		.gs_unit	= 768,
		.gs_count	= 256,
	},
	{
		.gs_unit	= 1024,
		.gs_count	= 256,
	},
	{
		.gs_unit	= 2048,
		.gs_count	= 256,
	},
	{
		.gs_unit	= 4096,
		.gs_count	= 256,
	},
	{
		.gs_unit	= 8192,
		.gs_count	= 256,
	},
};

static void
adt_reg_arena(void **state)
{
	const int	     alloc_sz1 = 768;
	const int	     alloc_sz2 = 8192;
	const int	     loop = 300; /* > 256 */
	struct ad_tx	     tx;
	struct ad_reserv_act acts[2];
	daos_off_t	     addrs[2];
	int		     i;
	int		     rc;
	uint32_t	     arena = AD_ARENA_ANY;
	uint32_t	     arena_old = 0;

	printf("register new arena and allocate from it\n");
	rc = ad_arena_register(adt_bh, adt_arena_type, adt_gsp, ARRAY_SIZE(adt_gsp));
	assert_rc_equal(rc, 0);

	printf("registered new type=%d\n", adt_arena_type);

	for (i = 0; i < loop; i++) {
		addrs[0] = ad_reserve(adt_bh, adt_arena_type, alloc_sz1, &arena, &acts[0]);
		assert_int_not_equal(addrs[0], 0);

		addrs[1] = ad_reserve(adt_bh, adt_arena_type, alloc_sz2, &arena, &acts[1]);
		assert_int_not_equal(addrs[1], 0);

		if (arena == AD_ARENA_ANY || arena != arena_old) {
			printf("allocate from arena = %d\n", arena);
			arena_old = arena;
		}

		rc = ad_tx_begin(adt_bh, &tx);
		assert_rc_equal(rc, 0);

		rc = ad_tx_publish(&tx, acts, 2);
		assert_rc_equal(rc, 0);

		rc = ad_tx_end(&tx, 0);
		assert_rc_equal(rc, 0);
	}
}

static void
adt_rsv_free_1(void **state)
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
adt_rsv_free_2(void **state)
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

static void
adt_rsv_write_free(void **state)
{
	const int	     alloc_size = 1536; /* non-pow2 group */
	const int	     loop = 200; /* cross group boundary (256K) */
	struct ad_tx	     tx;
	struct ad_reserv_act acts[loop];
	daos_off_t	     addrs[loop];
	int		     rc;
	int		     i;
	uint32_t	     arena = AD_ARENA_ANY;

	printf("Non-pow2 alloc, write and free\n");
	for (i = 0; i < loop; i++) {
		char *ptr;

		addrs[i] = ad_reserve(adt_bh, 0, alloc_size, &arena, &acts[i]);
		if (addrs[i] == 0) {
			fprintf(stderr, "failed %d allocate\n", i);
			return;
		}

		ptr = ad_addr2ptr(adt_bh, addrs[i]);
		memset(ptr, 0xca, alloc_size);
	}

	rc = ad_tx_begin(adt_bh, &tx);
	assert_rc_equal(rc, 0);

	rc = ad_tx_publish(&tx, acts, loop);
	assert_rc_equal(rc, 0);

	rc = ad_tx_end(&tx, 0);
	assert_rc_equal(rc, 0);

	adt_addrs_shuffle(addrs, loop);

	rc = ad_tx_begin(adt_bh, &tx);
	assert_rc_equal(rc, 0);
	for (i = 0; i < loop; i++) {
		rc = ad_tx_free(&tx, addrs[i]);
		assert_rc_equal(rc, 0);
	}
	rc = ad_tx_end(&tx, 0);
	assert_rc_equal(rc, 0);
}

static void
adt_delayed_free_1(void **state)
{
	const int	     alloc_size = 256;
	struct ad_tx	     tx;
	struct ad_reserv_act act;
	daos_off_t	     addr;
	daos_off_t	     addr2;
	int		     rc;
	uint32_t	     arena = AD_ARENA_ANY;

	printf("Delayed free\n");
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
	assert_rc_equal(rc, 0);

	addr2 = ad_reserve(adt_bh, 0, alloc_size, &arena, &act);
	assert_int_not_equal(addr, addr2);

	rc = ad_tx_publish(&tx, &act, 1);
	assert_rc_equal(rc, 0);

	rc = ad_tx_end(&tx, 0);
	assert_rc_equal(rc, 0);
}

static void
adt_tx_perf_1(void **state)
{
	const int	     alloc_sizes[2] = {64, 128};
	const int	     op_per_tx = 2;
	const int	     loop = 400000;
	struct ad_tx	     tx;
	struct ad_reserv_act acts[op_per_tx];
	struct timespec	     now;
	struct timespec	     then;
	daos_off_t	     addrs[op_per_tx];
	int64_t		     tdiff;
	int		     ops;
	int		     rc;
	int		     i;
	int		     j;
	uint32_t	     arena = AD_ARENA_ANY;

	printf("transaction performance test: 2 x alloc per tx\n");
	d_gettime(&then);
	for (i = 0; i < loop; i++) {
		/* NB: two reservations per transaction */
		for (j = 0; j < op_per_tx; j++) {
			addrs[j] = ad_reserve(adt_bh, 0, alloc_sizes[j], &arena, &acts[j]);
			if (addrs[j] == 0) {
				fprintf(stderr, "failed allocate\n");
				return;
			}
		}

		rc = ad_tx_begin(adt_bh, &tx);
		assert_rc_equal(rc, 0);

		rc = ad_tx_publish(&tx, acts, op_per_tx);
		assert_rc_equal(rc, 0);

		rc = ad_tx_end(&tx, 0);
		assert_rc_equal(rc, 0);
	}
	d_gettime(&now);
	tdiff = d_timediff_ns(&then, &now);

	ops = (int)((double)loop / ((double)tdiff / NSEC_PER_SEC));
	printf("TX rate = %d/sec\n", ops);
}

static void
adt_tx_perf_2(void **state)
{
	const int	     op_per_tx = 3;
	const int	     alloc_sizes[3] = {64, 128, 256};
	const int	     loop = 200000;
	struct ad_tx	     tx;
	struct ad_reserv_act acts[op_per_tx];
	struct timespec	     now;
	struct timespec	     then;
	daos_off_t	     *addrs;
	int64_t		     tdiff;
	int		     ops;
	int		     rc;
	int		     i;
	int		     count;
	uint32_t	     arena = AD_ARENA_ANY;

	printf("transaction performance test: 3 x alloc + 1 x free per tx\n");
	addrs = calloc(op_per_tx * loop, sizeof(*addrs));
	if (!addrs) {
		fprintf(stderr, "failed allocate\n");
		return;
	}

	d_gettime(&then);
	for (i = count = 0; i < loop; i++) {
		int	j;
		int	k;

		/* NB: 3 allocations and 1 free per transaction */
		for (j = 0; j < op_per_tx; j++) {
			k = count + j;
			addrs[k] = ad_reserve(adt_bh, 0, alloc_sizes[j], &arena, &acts[j]);
			if (addrs[k] == 0) {
				fprintf(stderr, "failed allocate\n");
				return;
			}
		}

		rc = ad_tx_begin(adt_bh, &tx);
		assert_rc_equal(rc, 0);

		rc = ad_tx_publish(&tx, acts, op_per_tx);
		assert_rc_equal(rc, 0);

		count += op_per_tx;
		if (i > 0) {
			k = d_rand() % count;
			rc = ad_tx_free(&tx, addrs[k]);
			assert_rc_equal(rc, 0);
			addrs[k] = addrs[--count];
		}
		rc = ad_tx_end(&tx, 0);
		assert_rc_equal(rc, 0);
	}
	d_gettime(&now);
	tdiff = d_timediff_ns(&then, &now);

	ops = (int)((double)loop / ((double)tdiff / NSEC_PER_SEC));
	printf("TX rate = %d/sec\n", ops);
	free(addrs);
}

static void
adt_no_space_1(void **state)
{
	const int	     alloc_size = 4096;
	const int	     alloc_size1 = 512;
	struct ad_tx	     tx;
	struct ad_reserv_act act;
	daos_off_t	     addr;
	int		     rc;
	int		     i;
	uint32_t	     arena = AD_ARENA_ANY;
	daos_off_t	    *addr_array;
	int		     array_size;

	D_ALLOC(addr_array, (ADT_STORE_SIZE / alloc_size) * sizeof(daos_off_t));
	if (addr_array == NULL)
		return;

	printf("Consume all space\n");
	for (i = 0;; i++) {
		addr = ad_reserve(adt_bh, 0, alloc_size, &arena, &act);
		if (addr == 0) {
			printf("Run out of space, allocated %d MB space, last used arena=%d\n",
			       (int)((alloc_size * i) >> 20), arena);
			break;
		}

		addr_array[i] = addr;
		rc = ad_tx_begin(adt_bh, &tx);
		assert_rc_equal(rc, 0);

		rc = ad_tx_publish(&tx, &act, 1);
		assert_rc_equal(rc, 0);

		rc = ad_tx_end(&tx, 0);
		assert_rc_equal(rc, 0);
	}
	array_size = i;

	adt_addrs_shuffle(addr_array, array_size);
	rc = ad_tx_begin(adt_bh, &tx);
	assert_rc_equal(rc, 0);

	printf("Freeing all space: %d\n", array_size);
	for (i = 0; i < array_size; i++) {
		rc = ad_tx_free(&tx, addr_array[i]);
		assert_rc_equal(rc, 0);
	}
	rc = ad_tx_end(&tx, 0);
	assert_rc_equal(rc, 0);

	printf("Consume all space again\n");
	for (i = 0;; i++) {
		addr = ad_reserve(adt_bh, 0, alloc_size1, &arena, &act);
		if (addr == 0) {
			printf("Run out of space, allocated %d MB space, last used arena=%d\n",
			       (int)((alloc_size1 * i) >> 20), arena);
			break;
		}
		rc = ad_tx_begin(adt_bh, &tx);
		assert_rc_equal(rc, 0);

		rc = ad_tx_publish(&tx, &act, 1);
		assert_rc_equal(rc, 0);

		rc = ad_tx_end(&tx, 0);
		assert_rc_equal(rc, 0);
	}
	/* D_ASSERT(i >= array_size * (alloc_size / alloc_size1)); */
	D_FREE(addr_array);
}

static int
adt_setup(void **state)
{
	struct umem_store store = {0};
	int		  rc;

	adt_blob_create(state);

	printf("open ad_blob\n");
	store.stor_ops = &adt_store_ops;
	rc = ad_blob_open(DUMMY_BLOB, 0, &store, &adt_bh);
	assert_rc_equal(rc, 0);

	assert_int_equal(store.stor_size, ADT_STORE_SIZE);
	return 0;
}

static int
adt_teardown(void **state)
{
	int	rc;

	printf("close ad_blob\n");
	rc = ad_blob_destroy(adt_bh);
	assert_rc_equal(rc, 0);
	return 0;
}

int
main(void)
{
	const struct CMUnitTest ad_mem_tests[] = {
		cmocka_unit_test(adt_undo_1),
		cmocka_unit_test(adt_rsv_cancel_1),
		cmocka_unit_test(adt_rsv_cancel_2),
		cmocka_unit_test(adt_rsv_pub_1),
		cmocka_unit_test(adt_rsv_pub_2),
		cmocka_unit_test(adt_rsv_pub_3),
		cmocka_unit_test(adt_rsv_pub_4),
		cmocka_unit_test(adt_rsv_pub_5),
		cmocka_unit_test(adt_rsv_pub_abort_1),
		cmocka_unit_test(adt_rsv_pub_abort_2),
		cmocka_unit_test(adt_rsv_inval),
		cmocka_unit_test(adt_reg_arena),
		cmocka_unit_test(adt_rsv_free_1),
		cmocka_unit_test(adt_rsv_free_2),
		cmocka_unit_test(adt_rsv_write_free),
		cmocka_unit_test(adt_delayed_free_1),
		cmocka_unit_test(adt_tx_perf_1),
		cmocka_unit_test(adt_tx_perf_2),
		/* Must be the last test */
		cmocka_unit_test(adt_no_space_1),
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
