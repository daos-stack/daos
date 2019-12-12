/*
 * (C) Copyright 2019 Intel Corporation.
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <daos/test_utils.h>
#include <daos/checksum.h>
#include <daos_srv/evtree.h>
#include <daos_srv/srv_csum.h>

#define ASSERT_SUCCESS(exp) assert_int_equal(0, (exp))

static void
print_chars(const uint8_t *buf, const size_t len, const uint32_t max)
{
	int i;

	if (buf == NULL)
		return;

	for (i = 0; i <  len && (i < max || max == 0); i++)
		print_error("%c", buf[i]);
}

#define FAKE_UPDATE_BUF_LEN (1024*1024)
static char fake_update_buf_copy[FAKE_UPDATE_BUF_LEN];
static char *fake_update_buf = fake_update_buf_copy;

static int fake_update_bytes;
static int fake_update_called;
static int
fake_update(struct daos_csummer *obj, uint8_t *buf, size_t buf_len)
{
	int	i;
	size_t	bytes_len;

	fake_update_called++;

	if (fake_update_bytes + buf_len < FAKE_UPDATE_BUF_LEN) {
		strncpy(fake_update_buf, (char *)buf, buf_len);
		fake_update_buf += buf_len;
		fake_update_bytes += buf_len;
		strncpy(fake_update_buf, "|", 1);
		fake_update_buf++;
		fake_update_bytes++;
	}

	bytes_len = min(sizeof(uint32_t), buf_len);

	for (i = 0; i < bytes_len; i++)
		*((uint32_t *)obj->dcs_csum_buf) |= buf[i];

	return 0;
}

static int fake_compare_called;
static bool
fake_compare(struct daos_csummer *obj,
	     uint8_t *buf1, uint8_t *buf2,
	     size_t buf_len)
{
	fake_compare_called++;
	return true;
}

static struct csum_ft fake_algo = {
	.cf_update = fake_update,
	.cf_compare = fake_compare,
	.cf_csum_len = sizeof(uint32_t),
	.cf_type = 999,
	.cf_name = "fake"
};

static void
reset_fake_algo()
{
	memset(fake_update_buf_copy, 0, FAKE_UPDATE_BUF_LEN);
	fake_update_buf = fake_update_buf_copy;
	fake_update_called = 0;
	fake_update_bytes = 0;
	fake_compare_called = 0;
}

#define	FAKE_UPDATE_SAW(buf) fake_update_saw(__FILE__, __LINE__, \
				buf, sizeof(buf)-1)

void fake_update_saw(char *file, int line, char *buf, size_t len)
{
	if (len != fake_update_bytes ||
	    memcmp(fake_update_buf_copy, buf, len) != 0) {
		print_error("%s:%dExpected to see '", file, line);
		print_chars((uint8_t *) buf, len, 0);
		print_error("' in '");
		print_chars((uint8_t *) fake_update_buf_copy, fake_update_bytes,
			    FAKE_UPDATE_BUF_LEN);
		print_error("'\n");
		fail();
	}
}

/**
 * -------------------------------------------------------------------------
 * Testing fetch of aligned and unaligned extents
 * -------------------------------------------------------------------------
 */
struct vos_fetch_test_context {
	size_t		 nr; /** Number of bsgl.bio_iov/biov_dcb pairs */
	struct bio_sglist	 bsgl;
	daos_csum_buf_t	*biov_dcbs;
	daos_iod_t		 iod;
	struct daos_csummer	*csummer;
};

struct extent_info {
	char *data;
	struct evt_extent sel;
	struct evt_extent ful;
};

struct test_setup {
	int request_idx;
	int request_len;
	uint64_t chunksize;
	uint64_t rec_size;
	struct extent_info layout[24];
};

#define	TEST_CASE_CREATE(ctx, ...) test_case_create(ctx, \
	&(struct test_setup)__VA_ARGS__)

static void
test_case_create(struct vos_fetch_test_context *ctx, struct test_setup *setup)
{
	uint32_t	 csum_len;
	uint64_t	 rec_size;
	uint32_t	 cs;
	size_t		 i = 0;
	size_t		 j;
	size_t		 nr;
	uint8_t		*dummy_csums;

	daos_csummer_init(&ctx->csummer, &fake_algo, setup->chunksize);

	csum_len = daos_csummer_get_csum_len(ctx->csummer);
	cs = daos_csummer_get_chunksize(ctx->csummer);
	dummy_csums = (uint8_t *) "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	rec_size = setup->rec_size;

	/** count number of layouts */
	while (setup->layout[i].data != NULL)
		i++;
	nr = i;

	ctx->nr = nr;
	bio_sgl_init(&ctx->bsgl, nr);
	ctx->bsgl.bs_nr_out = nr;
	D_ALLOC_ARRAY(ctx->biov_dcbs, nr);

	for (i = 0; i < nr; i++) {
		struct extent_info	*l;
		char			*data;
		struct bio_iov		*biov;
		daos_csum_buf_t		*dcb;
		size_t			 data_len;
		size_t			 num_of_csum;
		bio_addr_t		 addr = {0};

		l = &setup->layout[i];
		data = l->data;

		data_len = (l->ful.ex_hi - l->ful.ex_lo + 1) * rec_size;
		biov = &ctx->bsgl.bs_iovs[i];

		bio_iov_set(biov, addr,
			    evt_extent_width(&l->sel) * rec_size);
		bio_iov_set_extra(biov,
				  (l->sel.ex_lo - l->ful.ex_lo) *
				  rec_size,
				  (l->ful.ex_hi - l->sel.ex_hi) *
				  rec_size);

		D_ALLOC(biov->bi_buf, data_len);
		memcpy(bio_iov2raw_buf(biov), data, data_len);

		/** Just a rough count */
		num_of_csum = data_len / cs + 1;

		dcb = &ctx->biov_dcbs[i];
		D_ALLOC(dcb->cs_csum, csum_len * num_of_csum);
		dcb->cs_buf_len = csum_len * num_of_csum;
		dcb->cs_nr = num_of_csum;
		dcb->cs_len = csum_len;
		dcb->cs_chunksize = cs;

		for (j = 0; j < num_of_csum; j++) {
			/** All csums will be the same so verify correctly */
			dcb_insert(dcb, j, dummy_csums, csum_len);
		}
	}

	ctx->iod.iod_nr = 1;
	ctx->iod.iod_size = rec_size;
	ctx->iod.iod_type = DAOS_IOD_ARRAY;
	D_ALLOC_PTR(ctx->iod.iod_recxs);
	ctx->iod.iod_recxs->rx_idx = setup->request_idx;
	ctx->iod.iod_recxs->rx_nr = setup->request_len;

	daos_csummer_alloc_dcbs(ctx->csummer, &ctx->iod, 1,
				&ctx->iod.iod_csums, NULL);
}

static void
test_case_destroy(struct vos_fetch_test_context *ctx)
{
	int i;

	daos_csummer_free_dcbs(ctx->csummer, &ctx->iod.iod_csums);

	for (i = 0; i < ctx->nr; i++) {
		void *bio_buf = bio_iov2raw_buf(&ctx->bsgl.bs_iovs[i]);

		if (bio_buf)
			D_FREE(bio_buf);

		if (ctx->biov_dcbs[i].cs_csum)
			D_FREE(ctx->biov_dcbs[i].cs_csum);
	}

	if (ctx->iod.iod_recxs)
		D_FREE(ctx->iod.iod_recxs);

	bio_sgl_fini(&ctx->bsgl);
	daos_csummer_destroy(&ctx->csummer);
}

static int
vos_fetch_csum_verify_bsgl_with_args(struct vos_fetch_test_context *ctx)
{
	return ds_csum_add2iod(
		&ctx->iod, ctx->csummer, &ctx->bsgl,
		ctx->biov_dcbs, NULL);
}

/**
 * -------------------------------------------------------------------------
 * Testing the logic to determine if a new checksum needs to be calculated
 * for a chunk of an extent ... or can the stored checksum be used
 * -------------------------------------------------------------------------
 */

struct need_new_checksum_tests_args {
	/**  bytes needed for the chunk, can be smaller than chunk size */
	size_t		chunksize; /** chunk size */
	/** If a new checksum calculation has already started */
	bool		csum_started;
	bool		has_next_biov;
	/** byte the biov starts (not known by biov, but must is
	 * calculated based on evt_entry/biov combo. For testing,
	 * just hard code it.
	 */
	daos_off_t	req_start;
	daos_off_t	req_len;
	daos_off_t	raw_len;
};

#define	NEED_NEW_CHECKSUM_TESTS_TESTCASE(expected, ...) \
	need_new_checksum_tests_testcase(__FILE__, __LINE__, expected, \
		(struct need_new_checksum_tests_args)__VA_ARGS__)

void
need_new_checksum_tests_testcase(char *file, int line, bool expected,
				 struct need_new_checksum_tests_args args)
{
	bool		 result;

	bool has_next = args.has_next_biov;
	bool started = args.csum_started;
	struct daos_csum_range chunk;
	struct daos_csum_range req;
	struct daos_csum_range raw;

	dcr_set_idx_nr(&chunk, 0, args.chunksize);
	dcr_set_idx_nr(&req, args.req_start, args.req_len);
	dcr_set_idx_nr(&raw, args.req_start, args.raw_len);

	result = ext_needs_new_csum(&raw, &req, &chunk, started, has_next);
	if (result != expected) {
		print_error("%s:%d expected %d but found %d\n", file, line,
			    expected, result);
		fail();
	}
}

static void
need_new_checksum_tests(void **state)
{
	/** Whenever a csum calculation has already been started (csum_started),
	 * it must continue (until the next chunk at least).
	 */
	NEED_NEW_CHECKSUM_TESTS_TESTCASE(true, {
		.csum_started = true,
		.has_next_biov = false,
		.chunksize = 10,
		.req_len = 10,
		.raw_len = 10,
	});

	/**
	 *  Everything lines up so this 'chunk' csum can be used as is.
	 */
	NEED_NEW_CHECKSUM_TESTS_TESTCASE(false, {
		.csum_started = false,
		.has_next_biov = false,
		.chunksize = 8,
		.req_len = 8,
		.raw_len = 8,
	});

	/**
	 * Extent is larger than chunksize and is only extent in chunk so new
	 * checksum is not needed.
	 */
	NEED_NEW_CHECKSUM_TESTS_TESTCASE(false, {
		.has_next_biov = false,
		.chunksize = 8,
		.csum_started = false,
		.req_len = 20,
		.raw_len = 20,
	});

	/**
	 * Extent is smaller than chunksize and is only extent in chunk
	 */
	NEED_NEW_CHECKSUM_TESTS_TESTCASE(false, {
		.has_next_biov = false,
		.chunksize = 16,
		.csum_started = false,
		.req_len = 6,
		.raw_len = 6,
	});

	/**
	 * Extent is smaller than chunksize and another extent is after, but
	 * it is after the next chunk starts
	 */
	NEED_NEW_CHECKSUM_TESTS_TESTCASE(false, {
		.has_next_biov = true,
		.chunksize = 8,
		.csum_started = false,
		.req_len = 6,
		.raw_len = 6,
		.req_start = 4 /** starts so next biov is after chunk end */

	});

	/**
	 * Extent is smaller than chunksize and another extent is after in
	 * same chunk ... will need to calc new csum
	 */
	NEED_NEW_CHECKSUM_TESTS_TESTCASE(true, {
		.has_next_biov = true,
		.chunksize = 8,
		.csum_started = false,
		.req_len = 3,
		.raw_len = 3,
		.req_start = 4 /** starts so next biov is after chunk end */
	});

	/**
	 * Extent is larger than bytes needed for chunk (fetch is smaller than
	 * chunk), but still smaller than chunk.
	 */
	NEED_NEW_CHECKSUM_TESTS_TESTCASE(true, {
		.has_next_biov = false,
		.chunksize = 8,
		.csum_started = false,
		.req_len = 6,
		.raw_len = 8,
		.req_start = 1
	});
	/**
	 * Same as previous, but using biov end instead of begin
	 */
	NEED_NEW_CHECKSUM_TESTS_TESTCASE(true, {
		.has_next_biov = false,
		.chunksize = 8,
		.csum_started = false,
		.req_len = 6,
		.raw_len = 8,
		.req_start = 0
	});
}

struct csum_idx {
	int dcb_idx;
	int csum_idx;
};

struct biov_iod_csum_compare {
	struct csum_idx biov_csum;
	struct csum_idx iod_csum;
};

#define	IOD_BIOV_CSUM_SAME(args, ...) \
	iod_biov_csum_same(args, (struct biov_iod_csum_compare)__VA_ARGS__)

void
iod_biov_csum_same(struct vos_fetch_test_context *ctx,
		   struct biov_iod_csum_compare idxs)
{
	daos_csum_buf_t *biov = &ctx->biov_dcbs[idxs.biov_csum.dcb_idx];
	daos_csum_buf_t *iod = &ctx->iod.iod_csums[idxs.iod_csum.dcb_idx];
	uint32_t csum_len = biov->cs_len;

	assert_memory_equal(
		dcb_idx2csum(biov, idxs.biov_csum.csum_idx),
		dcb_idx2csum(iod, idxs.iod_csum.csum_idx), csum_len);
}

/** Test cases */
static void
with_extent_smaller_than_chunk(void **state)
{
	struct vos_fetch_test_context ctx;

	/** Setup */
	TEST_CASE_CREATE(&ctx, {
		.request_idx = 1,
		.request_len = 3,
		.chunksize = 8,
		.rec_size = 1,
		.layout = {
			{.data = "AB", .sel = {0, 2}, .ful = {0, 2},},
			{.data = NULL}
		}
	});

	/** Act */
	ASSERT_SUCCESS(vos_fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	IOD_BIOV_CSUM_SAME(&ctx, { .biov_csum = {0, 0}, .iod_csum = {0, 0} });

	/** Never have to create a new csum because there's only 1 extent */
	assert_int_equal(0, fake_update_called);
	assert_int_equal(0, fake_compare_called);

	/** clean up */
	test_case_destroy(&ctx);
}

/**
 * Should look like this:
 * Fetch extent:	1  2 | 3 \0 | 4  \0
 * epoch 2 extent:	              4  \0
 * epoch 1 extent:	1  2 | 3  \0
 * index:		0  1 | 2  3 | 4  5
 */
static void
with_aligned_chunks_csums_are_copied(void **state)
{
	struct vos_fetch_test_context ctx;

	/** Setup */
	TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 6,
		.chunksize = 2,
		.rec_size = 1,
		.layout = {
			{.data = "123", .sel = {0, 3}, .ful = {0, 3},},
			{.data = "4", .sel = {4, 5}, .ful = {4, 5},},
			{.data = NULL}
		}
	});

	/** Act */
	ASSERT_SUCCESS(vos_fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	IOD_BIOV_CSUM_SAME(&ctx, { .biov_csum = {0, 0}, .iod_csum = {0, 0} });
	IOD_BIOV_CSUM_SAME(&ctx, { .biov_csum = {0, 1}, .iod_csum = {0, 1} });
	IOD_BIOV_CSUM_SAME(&ctx, { .biov_csum = {1, 0}, .iod_csum = {0, 2} });

	FAKE_UPDATE_SAW("");
	assert_int_equal(0, fake_update_called);
	assert_int_equal(0, fake_compare_called);


	test_case_destroy(&ctx);
}

/**
 * Should look like this:
 * Fetch extent:	1  A | B \0
 * epoch 2 extent:	   A | B  \0
 * epoch 1 extent:	1  2 | 3  \0
 * index:		0  1 | 2  3
 */
static void
with_unaligned_chunks_csums_new_csum_is_created(void **state)
{
	struct vos_fetch_test_context ctx;

	/** Setup */
	TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 4,
		.chunksize = 2,
		.rec_size = 1,
		.layout = {
			{.data = "123", .sel = {0, 0}, .ful = {0, 3},},
			{.data = "AB", .sel = {1, 3}, .ful = {1, 3},},
			{.data = NULL}
		}
	});

	/** Act */
	ASSERT_SUCCESS(vos_fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW("1|A|12|A|");
	assert_int_equal(4, fake_update_called);
	assert_int_equal(2, fake_compare_called);

	test_case_destroy(&ctx);
}

/**
 * Want to make sure not verifying chunks that are not part of fetch, even
 * though parts of the extent are.
 *
 * Should look like this:
 * Fetch extent:	5  A  B  C
 * epoch 2 extent:	   A  B  C  D  E  F  G | H  I  \0
 * epoch 1 extent:	5  6  \0
 * index:		0  1  2  3  4  5  6  7 | 8  9  10
 */
static void
with_extent_larger_than_request(void **state)
{
	struct vos_fetch_test_context ctx;

	/**
	 * Fetching a whole single chunk that's made up of two extents.
	 * Only the first 2 bytes of first are visible, but will need to verify
	 * the whole chunk from the first.
	 *
	 */
	TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 4,
		.chunksize = 8,
		.rec_size = 1,
		.layout = {
			{.data = "56",
				.sel = {0, 0}, .ful = {0, 2} },
			{.data = "ABCDEFGHI",
				.sel = {1, 3}, .ful = {1, 10} },
			{.data = NULL}
		}
	});

	/** Act */
	ASSERT_SUCCESS(vos_fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW("5|ABC|56\0|ABCDEFG|");
	assert_int_equal(4, fake_update_called);
	assert_int_equal(2, fake_compare_called);

	/** clean up */
	test_case_destroy(&ctx);
}

/**
 * First extent isn't aligned but everything else is. Because first chunk is
 * made up of a single extent (even though it is unaligned), a new checksum is
 * not needed and can be copied.
 *
 * Should look like this:
 * Fetch extent:	   A | C  \0
 * epoch 2 extent:	     | C  \0
 * epoch 1 extent:	   A | B  \0
 * index:		0  1 | 2  3
 */
static void
with_unaligned_first_chunk(void **state)
{
	struct vos_fetch_test_context ctx;

	/** Setup */
	TEST_CASE_CREATE(&ctx, {
		.request_idx = 1,
		.request_len = 3,
		.chunksize = 2,
		.rec_size = 1,
		.layout = {
			{.data = "AB", .sel = {1, 1}, .ful = {1, 3} },
			{.data = "C", .sel = {2, 3}, .ful = {2, 3} },
			{.data = NULL}
		}
	});

	/** Act */
	ASSERT_SUCCESS(vos_fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW("");
	IOD_BIOV_CSUM_SAME(&ctx, { .biov_csum = {0, 0}, .iod_csum = {0, 0} });
	IOD_BIOV_CSUM_SAME(&ctx, { .biov_csum = {1, 0}, .iod_csum = {0, 1} });
	assert_int_equal(0, fake_update_called);
	assert_int_equal(0, fake_compare_called);

	test_case_destroy(&ctx);
}

/**
 * When the fetch is smaller than a chunk, will need to create a new checksum
 * and verify the stored checksum
 *
 * Should look like this:
 * Fetch extent:	   B  C  D  E  F  G     |
 * epoch 1 extent:	A  B  C  D  E  F  G  H  |
 * index:		0  1  2  3  4  5  6  7  |
 */
static void
with_fetch_smaller_than_chunk(void **state)
{
	struct vos_fetch_test_context ctx;

	TEST_CASE_CREATE(&ctx, {
		.request_idx = 1,
		.request_len = 6,
		.chunksize = 8,
		.rec_size = 1,
		.layout = {
			{.data = "ABCDEFGH",
				.sel = {1, 6}, .ful = {0, 7} },
			{.data = NULL}
		}
	});

	/** Act */
	ASSERT_SUCCESS(vos_fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW("BCDEFG|ABCDEFGH|");

	assert_int_equal(2, fake_update_called);
	assert_int_equal(1, fake_compare_called);

	/** clean up */
	test_case_destroy(&ctx);
}

/**
 * Should look like this:
 * Fetch extent:	   A | C
 * epoch 2 extent:	   A | 1  \0
 * epoch 1 extent:	0  1 | \0
 * index:		0  1 | 2  3
 */
static void
more_partial_extent_tests(void **state)
{
	struct vos_fetch_test_context ctx;

	TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 3,
		.chunksize = 2,
		.rec_size = 1,
		.layout = {
			{.data = "01",
				.sel = {0, 0}, .ful = {0, 2} },
			{.data = "A",
				.sel = {1, 2}, .ful = {1, 2} },
			{.data = NULL}
		}
	});

	/** Act */
	ASSERT_SUCCESS(vos_fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW("0|A|01|A|");

	assert_int_equal(4, fake_update_called);
	assert_int_equal(2, fake_compare_called);

	/** clean up */
	test_case_destroy(&ctx);
}

static void
test_larger_records(void **state)
{
	struct vos_fetch_test_context	ctx;
	const int			buf_len = 1024;
	char				large_data01[buf_len];
	char				large_data02[buf_len];
	int				i;

	for (i = 0; i < buf_len; i++) {
		large_data01[i] = 'A' + i % ('Z' + 1 - 'A');
		large_data02[i] = 'a' + i % ('z' + 1 - 'a');
	}

	TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 8,
		.chunksize = 12,
		.rec_size = 4,
		.layout = {
			{.data = large_data02,
				.sel = {0, 3}, .ful = {0, 3} },
			{.data = large_data01,
				.sel = {4, 7}, .ful = {4, 7} },
			{.data = NULL}
		}
	});

	/** Act */
	ASSERT_SUCCESS(vos_fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	/** 1 record from 1st extent (mnop) and 2 records from 2nd extent
	 * (ABCDEFGH)
	 */
	FAKE_UPDATE_SAW("mnop|ABCDEFGH|mnop|ABCDEFGH|");

	/** clean up */
	test_case_destroy(&ctx);
}

static void
test_larger_records2(void **state)
{
	struct vos_fetch_test_context	ctx;
	char				*large_data01;
	char				*large_data02;

	D_ALLOC(large_data01, 1024 * 16);
	D_ALLOC(large_data02, 1024 * 16);

	memset(large_data01, 'A', 1024 * 16);
	memset(large_data02, 'B', 1024 * 16);

	TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 12,
		.chunksize = 1024*32,
		.rec_size = 1024,
		.layout = {
			{.data = large_data02,
				.sel = {0, 2}, .ful = {0, 2} },
			{.data = large_data01,
				.sel = {2, 11}, .ful = {0, 11} },
			{.data = NULL}
		}
	});

	/** Act */
	ASSERT_SUCCESS(vos_fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	assert_int_equal(4, fake_update_called);
	assert_int_equal(2, fake_compare_called);

	/** clean up */
	test_case_destroy(&ctx);
	D_FREE(large_data01);
	D_FREE(large_data02);
}

int setup(void **state)
{
	return 0;
}

int teardown(void **state)
{
	reset_fake_algo();
	return 0;
}

/* Convenience macros for unit tests */
#define	T(desc, test_fn) \
	{ "SRV_CSUM" desc, test_fn, setup, teardown}

static const struct CMUnitTest tests[] = {
	T("01: Partial Extents, but chunks align",
	  with_aligned_chunks_csums_are_copied),
	T("02: Partial Extents, chunks don't align",
	  with_unaligned_chunks_csums_new_csum_is_created),
	T("03: Partial Extents, first extent isn't aligned",
	  with_unaligned_first_chunk),
	T("04: Partial Extents, extent smaller than chunk",
	  with_extent_smaller_than_chunk),
	T("05: Extent is larger than chunk", with_extent_larger_than_request),
	T("06: Fetch smaller than chunk", with_fetch_smaller_than_chunk),
	T("07: Partial extent/unaligned extent", more_partial_extent_tests),
	T("08: Fetch with larger records", test_larger_records),
	T("09: Fetch with larger records", test_larger_records2),
	T("10: Determine if need new checksum", need_new_checksum_tests),
};

int
main(void)
{

	return cmocka_run_group_tests_name(
		"Storage and retrieval of checksums for Array Type",
		tests, NULL, NULL);

}
