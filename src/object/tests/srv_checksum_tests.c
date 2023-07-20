/*
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <daos/test_utils.h>
#include <daos/checksum.h>
#include <daos_srv/evtree.h>
#include <daos_srv/srv_csum.h>
#include <daos/tests_lib.h>
#include <daos/test_perf.h>

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
fake_reset(void *daos_mhash_ctx)
{
	fake_update_buf[0] = '>';
	fake_update_buf++;
	fake_update_bytes++;
	return 0;
}

static int
fake_update(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	fake_update_called++;

	if (fake_update_bytes + buf_len < FAKE_UPDATE_BUF_LEN) {
		strncpy(fake_update_buf, (char *)buf, buf_len);
		fake_update_buf += buf_len;
		fake_update_bytes += buf_len;
		fake_update_buf[0] = '|';
		fake_update_buf++;
		fake_update_bytes++;
	}

	return 0;
}

static int
fake_finish(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	int i;

	/** Fill checksum with 'N' to indicate creating new checksum */
	for (i = 0; i < buf_len; i++)
		buf[i] = 'N';

	return 0;
}

static int fake_compare_called;
static bool
fake_compare(void *daos_mhash_ctx,
	     uint8_t *buf1, uint8_t *buf2,
	     size_t buf_len)
{
	fake_compare_called++;
	return true;
}

static struct hash_ft fake_algo = {
	.cf_reset	= fake_reset,
	.cf_update	= fake_update,
	.cf_finish	= fake_finish,
	.cf_compare	= fake_compare,
	.cf_hash_len	= sizeof(uint32_t),
	.cf_type	= 999,
	.cf_name	= "fake"
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
 *
 * These tests verify that the server logic for creating new checksums
 * or copying stored checksums works properly. Each test should have a layout
 * diagram of sorts in the comment header (if it fits easily). Gennerally a '_'
 * represents a hole that will exist in the request.
 *
 * There is a setup section that defines the request, chunk size,
 * record size and the layout of what extents are "stored". The setup
 * will create a bsgl with biov for each layout as though it were
 * coming from VOS. It should take into
 * account the prefix/suffix needed to represent raw vs req extents (.sel = req,
 * .ful = raw).
 * To verify correctness, a fake csum algo structure is used that
 * remembers what data it sees while "updating" and then can verify that the
 * correct data was used for "calculating" the new checksums.
 * - In the checksum (ASSERT_CSUM) "SSSS" means that the stored checksum was
 *   copied. "CCCC" means that a new checksum was created.
 * - In the observed data for a checksum update (FAKE_UPDATE_SAW), a '>' means
 *   that a new checksum was started,  and '|' separates calls to checksum
 *   update.
 * -------------------------------------------------------------------------
 */
struct vos_fetch_test_context {
	size_t			 nr; /** Num of bsgl.bio_iov/biov_csums pairs */
	struct bio_sglist	 bsgl;
	struct dcs_ci_list	 biov_csums;
	daos_iod_t		 iod;
	struct daos_csummer	*csummer;
	struct dcs_iod_csums	*iod_csum;
};

struct extent_info {
	char *data;
	struct evt_extent sel;
	struct evt_extent ful;
	bool is_hole;
};

struct array_test_case_args {
	int request_idx;
	int request_len;
	uint64_t chunksize;
	uint64_t rec_size;
	struct extent_info layout[24];
};

#define	ARRAY_TEST_CASE_CREATE(ctx, ...) array_test_case_create(ctx, \
	&(struct array_test_case_args)__VA_ARGS__)

static void
array_test_case_create(struct vos_fetch_test_context *ctx,
		 struct array_test_case_args *setup)
{
	uint32_t	 csum_len;
	uint64_t	 rec_size;
	uint32_t	 cs;
	size_t		 i = 0;
	size_t		 j;
	size_t		 nr;
	uint8_t		*dummy_csums;

	daos_csummer_init(&ctx->csummer, &fake_algo, setup->chunksize, 0);

	csum_len = daos_csummer_get_csum_len(ctx->csummer);
	cs = daos_csummer_get_chunksize(ctx->csummer);
	assert_true(cs != 0);
	dummy_csums = (uint8_t *)"SSSSSSSSSSSSSSSSSSSSSSSSSS";
	rec_size = setup->rec_size;

	/** count number of layouts */
	while (setup->layout[i].data != NULL)
		i++;
	nr = i;

	ctx->nr = nr;
	bio_sgl_init(&ctx->bsgl, nr);
	ctx->bsgl.bs_nr_out = nr;
	assert_success(dcs_csum_info_list_init(&ctx->biov_csums, 10));

	for (i = 0; i < nr; i++) {
		struct extent_info	*l;
		char			*data;
		struct bio_iov		*biov;
		struct dcs_csum_info	 info;
		uint8_t			 csum_buf[128];
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

		if (l->is_hole) {
			BIO_ADDR_SET_HOLE(&biov->bi_addr);
			biov->bi_buf = NULL;
		} else {
			D_ALLOC(biov->bi_buf, data_len);
			memcpy(bio_iov2raw_buf(biov), data, data_len);
			/** Just a rough count */
			num_of_csum = data_len / cs + 1;

			assert_true(csum_len * num_of_csum <= ARRAY_SIZE(csum_buf));
			info.cs_csum = csum_buf;
			info.cs_buf_len = csum_len * num_of_csum;
			info.cs_nr = num_of_csum;
			info.cs_len = csum_len;
			info.cs_chunksize = cs;
			for (j = 0; j < num_of_csum; j++)
				ci_insert(&info, j, dummy_csums, csum_len);
			dcs_csum_info_save(&ctx->biov_csums, &info);
		}
	}

	ctx->iod.iod_nr = 1;
	ctx->iod.iod_size = rec_size;
	ctx->iod.iod_type = DAOS_IOD_ARRAY;
	D_ALLOC_PTR(ctx->iod.iod_recxs);
	ctx->iod.iod_recxs->rx_idx = setup->request_idx;
	ctx->iod.iod_recxs->rx_nr = setup->request_len;

	daos_csummer_alloc_iods_csums(ctx->csummer, &ctx->iod, 1, false,
				      NULL, &ctx->iod_csum);
}

static void
test_case_destroy(struct vos_fetch_test_context *ctx)
{
	int i;

	daos_csummer_free_ic(ctx->csummer, &ctx->iod_csum);

	for (i = 0; i < ctx->nr; i++) {
		void *bio_buf = bio_iov2raw_buf(&ctx->bsgl.bs_iovs[i]);

		if (bio_buf)
			D_FREE(bio_buf);
	}
	dcs_csum_info_list_fini(&ctx->biov_csums);

	if (ctx->iod.iod_recxs)
		D_FREE(ctx->iod.iod_recxs);

	bio_sgl_fini(&ctx->bsgl);
	daos_csummer_destroy(&ctx->csummer);
}

static int
fetch_csum_verify_bsgl_with_args(struct vos_fetch_test_context *ctx)
{
	return ds_csum_add2iod(
		&ctx->iod, ctx->csummer, &ctx->bsgl, &ctx->biov_csums, NULL,
		ctx->iod_csum);
}

#define ASSERT_CSUM(ctx, csum) \
	assert_memory_equal(csum, ctx.iod_csum->ic_data->cs_csum, \
		sizeof(csum) - 1)
#define ASSERT_CSUM_EMPTY(ctx, idx)                                                                \
	assert_int_equal(                                                                          \
	    0, *(ctx.iod_csum->ic_data->cs_csum + (idx * ctx.iod_csum->ic_data->cs_len)))
#define ASSERT_CSUM_IDX(ctx, csum, idx) \
	assert_memory_equal(csum, ctx.iod_csum->ic_data->cs_csum + \
	(idx * ctx.iod_csum->ic_data->cs_len), \
		sizeof(csum) - 1)

/**
 * Single extent that is a single chunk. Request matches extent
 *
 * Should look like this:
 * Fetch extent:	1  2  3  \0 |
 * epoch 1 extent:	1  2  3  \0 |
 * index:		0  1  2  3  |
 */
static void
request_that_matches_single_extent(void **state)
{
	struct vos_fetch_test_context ctx;

	/** Setup */
	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 4,
		.chunksize = 4,
		.rec_size = 1,
		.layout = {
			{.data = "123", .sel = {0, 3}, .ful = {0, 3},},
			{.data = NULL}
		}
	});

	memset(ctx.iod_csum->ic_data->cs_csum, 0,
	       ctx.iod_csum->ic_data->cs_buf_len);

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */

	ASSERT_CSUM(ctx, "SSSS");
	FAKE_UPDATE_SAW("");

	test_case_destroy(&ctx);
}

/**
 * Single extent that is smaller than a single chunk. Request matches extent
 *
 * Should look like this:
 * Fetch extent:	A  B  C
 * epoch 1 extent:	A  B  C
 * index:		0  1  2
 */
static void
extent_smaller_than_chunk(void **state)
{
	struct vos_fetch_test_context ctx;

	/** Setup */
	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 3,
		.chunksize = 8,
		.rec_size = 1,
		.layout = {
			{.data = "ABC", .sel = {0, 2}, .ful = {0, 2},},
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	ASSERT_CSUM(ctx, "SSSS");
	FAKE_UPDATE_SAW("");

	/** Never have to create a new csum because there's only 1 extent */
	assert_int_equal(0, fake_update_called);
	assert_int_equal(0, fake_compare_called);

	/** clean up */
	test_case_destroy(&ctx);
}

/**
 * Single extent that is multiple chunks. Request matches extent
 *
 * Should look like this:
 * Fetch extent:	1  2 | 3  4 | 5  6 | 7  \0 |
 * epoch 1 extent:	1  2 | 3  4 | 5  6 | 7  \0 |
 * index:		0  1 | 2  3 | 4  5 | 6  7  |
 */
static void
request_that_matches_single_extent_multiple_chunks(void **state)
{
	struct vos_fetch_test_context ctx;

	/** Setup */
	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 8,
		.chunksize = 2,
		.rec_size = 1,
		.layout = {
			{.data = "1234567", .sel = {0, 7}, .ful = {0, 7},},
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	ASSERT_CSUM(ctx, "SSSS");
	FAKE_UPDATE_SAW("");

	test_case_destroy(&ctx);
}

/**
 * Single extent that isn't chunk aligned at beginning or end,  but request
 * matches extent so still don't need new checksum
 *
 * Should look like this:
 * Fetch extent:	   2 | 3  4 | 5  6 | \0    |
 * epoch 1 extent:	   2 | 3  4 | 5  6 | \0    |
 * index:		0  1 | 2  3 | 4  5 | 6  7  |
 */
static void
request_that_matches_single_extent_multiple_chunks_not_aligned(void **state)
{
	struct vos_fetch_test_context ctx;

	/** Setup */
	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 1,
		.request_len = 6,
		.chunksize = 2,
		.rec_size = 1,
		.layout = {
			{.data = "23456", .sel = {1, 6}, .ful = {1, 6},},
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	ASSERT_CSUM(ctx, "SSSSSSSSSSSSSSSS");
	FAKE_UPDATE_SAW("");

	test_case_destroy(&ctx);
}

/**
 * Two extents that are chunk aligned and request is chunk aligned. Stored
 * checksums are copied.
 *
 * Should look like this:
 * Fetch extent:	Z  Y  X  W | V  U  T  S
 * epoch 2 extent:	           | V  U  T  S
 * epoch 1 extent:	Z  Y  X  W |
 * index:		0  1  2  3 | 4  5  6  7
 */
static void
request_that_matches_multiple_aligned_extents(void **state)
{
	struct vos_fetch_test_context ctx;

	/** Setup */
	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 8,
		.chunksize = 4,
		.rec_size = 1,
		.layout = {
			{.data = "ZYXW", .sel = {0, 3}, .ful = {0, 3},},
			{.data = "VUTS", .sel = {4, 7}, .ful = {4, 7},},
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	ASSERT_CSUM(ctx, "SSSSSSSS");
	FAKE_UPDATE_SAW("");

	test_case_destroy(&ctx);
}

/**
 * Two extents that are chunk aligned and request is chunk aligned. Stored
 * checksums are copied. Same as previous but extents are  in reverse order
 *
 * Should look like this:
 * Fetch extent:	Z  Y  X  W | V  U  T  S
 * epoch 2 extent:	Z  Y  X  W |
 * epoch 1 extent:	           | V  U  T  S
 * index:		0  1  2  3 | 4  5  6  7
 */
static void
request_that_matches_multiple_aligned_extents2(void **state)
{
	struct vos_fetch_test_context ctx;

	/** Setup */
	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 8,
		.chunksize = 4,
		.rec_size = 1,
		.layout = {
			{.data = "VUTS", .sel = {4, 7}, .ful = {4, 7},},
			{.data = "ZYXW", .sel = {0, 3}, .ful = {0, 3},},
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	ASSERT_CSUM(ctx, "SSSSSSSS");
	FAKE_UPDATE_SAW("");

	test_case_destroy(&ctx);
}

/**
 * One extent. Requesting larger (at end) extent than what exists. Will still
 * copy stored checksum because only stored extent is returned.
 *
 * Should look like this:
 * Fetch extent:	Z  Y  X  W | V  U  _  _
 * epoch 1 extent:	Z  Y  X  W | V  U
 * index:		0  1  2  3 | 4  5  6  7
 */
static void
request_that_is_more_than_extents(void **state)
{
	struct vos_fetch_test_context ctx;

	/** Setup */
	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 8,
		.chunksize = 4,
		.rec_size = 1,
		.layout = {
			{.data = "ZYXWVU", .sel = {0, 5}, .ful = {0, 5},},
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	ASSERT_CSUM(ctx, "SSSS");

	FAKE_UPDATE_SAW("");
	assert_int_equal(0, fake_update_called);
	assert_int_equal(0, fake_compare_called);

	test_case_destroy(&ctx);
}

/**
 * One single chunk length extent, but only first half is requested. Will need
 * to create a new checksum and verify whole original extent.
 *
 * Should look like this:
 * Fetch extent:	Z  Y  X  W
 * epoch 1 extent:	Z  Y  X  W  V  U  T  S  |
 * index:		0  1  2  3  4  5  6  7  |
 */
static void
partial_chunk_request0(void **state)
{
	struct vos_fetch_test_context ctx;

	/** Setup */
	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 4,
		.chunksize = 8,
		.rec_size = 1,
		.layout = {
			{.data = "ZYXWVUTS", .sel = {0, 3}, .ful = {0, 7},},
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW(">ZYXW|>ZYXWVUTS|");
	ASSERT_CSUM(ctx, "NNNN");
	assert_int_equal(2, fake_update_called);
	assert_int_equal(1, fake_compare_called);

	test_case_destroy(&ctx);
}

/**
 * One single chunk length extent, but only last half is requested. Will need
 * to create a new checksum and verify whole original extent.
 *
 * Should look like this:
 * Fetch extent:	            V  U  T  S  |
 * epoch 1 extent:	Z  Y  X  W  V  U  T  S  |
 * index:		0  1  2  3  4  5  6  7  |
 */
static void
partial_chunk_request1(void **state)
{
	struct vos_fetch_test_context ctx;

	/** Setup */
	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 4,
		.request_len = 4,
		.chunksize = 8,
		.rec_size = 1,
		.layout = {
			{.data = "ZYXWVUTS", .sel = {4, 7}, .ful = {0, 7},},
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW(">VUTS|>ZYXWVUTS|");
	ASSERT_CSUM(ctx, "NNNN");
	assert_int_equal(2, fake_update_called);
	assert_int_equal(1, fake_compare_called);

	test_case_destroy(&ctx);
}

/**
 * One single chunk length extent, but only middle part is requested. Will need
 * to create a new checksum and verify whole original extent.
 *
 * Should look like this:
 * Fetch extent:	      X  W  V  U        |
 * epoch 1 extent:	Z  Y  X  W  V  U  T  S  |
 * index:		0  1  2  3  4  5  6  7  |
 */
static void
partial_chunk_request2(void **state)
{
	struct vos_fetch_test_context ctx;

	/** Setup */
	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 2,
		.request_len = 4,
		.chunksize = 8,
		.rec_size = 1,
		.layout = {
			{.data = "ZYXWVUTS", .sel = {2, 5}, .ful = {0, 7},},
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW(">XWVU|>ZYXWVUTS|");
	ASSERT_CSUM(ctx, "NNNN");
	assert_int_equal(2, fake_update_called);
	assert_int_equal(1, fake_compare_called);

	test_case_destroy(&ctx);
}

/**
 * Single extent that spans multiple chunks. Request is only part of first
 * and last chunk so those should have new checksums, while only the
 * beginning/ending chunks are verified. The two middle chunks' checksums
 * should be copied.
 *
 * Should look like this:
 * Fetch extent:	      X  W | V  U  T  S | Z  Y  X  W | V  U       |
 * epoch 1 extent:	Z  Y  X  W | V  U  T  S | Z  Y  X  W | V  U  T  S |
 * index:		0  1  2  3 | 4  5  6  7 | 8  9 10 11 |12 13 14 15 |
 * Should create a new csum for the first chunk, copy the middle 2 chunks, then
 * create a new csum for the last chunk
 */
static void
request_needs_new_and_copy(void **state)
{
	struct vos_fetch_test_context ctx;

	/** Setup */
	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 2,
		.request_len = 12,
		.chunksize = 4,
		.rec_size = 1,
		.layout = {
			{.data = "ZYXWVUTSZYXWVUTS", .sel = {2, 13},
				.ful = {0, 15},},
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW(">XW|>ZYXW|>VU|>VUTS|");
	assert_int_equal(4, fake_update_called);
	assert_int_equal(2, fake_compare_called);

	ASSERT_CSUM(ctx, "NNNNSSSSSSSSNNNN");

	test_case_destroy(&ctx);
}

/**
 * Two extents, second overlaps the first partially in first chunk and
 * completely in second chunk. First chunk will need new checksum, second will
 * be copied from second extent. The first chunk of both first and second
 * extents will need to be verified.
 *
 *
 * Should look like this:
 * Fetch extent:	1  A | B \0
 * epoch 2 extent:	   A | B  \0
 * epoch 1 extent:	1  2 | 3  \0
 * index:		0  1 | 2  3
 */
static void
unaligned_chunks_csums_new_csum_is_created(void **state)
{
	struct vos_fetch_test_context ctx;

	/** Setup */
	ARRAY_TEST_CASE_CREATE(&ctx, {
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
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW(">1|A|>12|>A|");
	ASSERT_CSUM(ctx, "NNNNSSSS");

	assert_int_equal(4, fake_update_called);
	assert_int_equal(2, fake_compare_called);

	test_case_destroy(&ctx);
}

/**
 * Make sure not verifying chunks that are not part of fetch, even
 * though parts of the extent are. Request is first part of two extents, but
 * not part of the second chunk of extent two. When verifying, should not need
 * second chunk.
 *
 * Should look like this:
 * Fetch extent:	5  A  B  C
 * epoch 2 extent:	   A  B  C  D  E  F  G | H  I  J
 * epoch 1 extent:	5  6  7
 * index:		0  1  2  3  4  5  6  7 | 8  9  10
 */
static void
extent_larger_than_request(void **state)
{
	struct vos_fetch_test_context ctx;

	/**
	 * Fetching a whole single chunk that's made up of two extents.
	 * Only the first 2 bytes of first are visible, but will need to verify
	 * the whole chunk from the first.
	 *
	 */
	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 4,
		.chunksize = 8,
		.rec_size = 1,
		.layout = {
			{.data = "567",
				.sel = {0, 0}, .ful = {0, 2} },
			{.data = "ABCDEFGHI",
				.sel = {1, 3}, .ful = {1, 10} },
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW(">5|ABC|>567|>ABCDEFG|");
	assert_int_equal(4, fake_update_called);
	assert_int_equal(2, fake_compare_called);
	ASSERT_CSUM(ctx, "NNNN");

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
unaligned_first_chunk(void **state)
{
	struct vos_fetch_test_context ctx;

	/** Setup */
	ARRAY_TEST_CASE_CREATE(&ctx, {
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
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW("");
	ASSERT_CSUM(ctx, "SSSSSSSS");

	assert_int_equal(0, fake_update_called);
	assert_int_equal(0, fake_compare_called);

	test_case_destroy(&ctx);
}

/**
 * Two extents that don't overlap, but don't align (meet i nthe middle of a
 * chunk). Will copy stored checksums for first and last chunk, but create a
 * new one for the middle chunk.
 *
 * Should look like this:
 * Fetch extent:	A  B  C | D  E  F | G  H  I |
 * epoch 2 extent:	        |    E  F | G  H  I |
 * epoch 1 extent:	A  B  C | D       |         |
 * index:		0  1  2 | 3  4  5 | 6  7  8 |
 */
static void
fetch_multiple_unaligned_extents(void **state)
{
	struct vos_fetch_test_context ctx;

	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 9,
		.chunksize = 3,
		.rec_size = 1,
		.layout = {
			{.data = "ABCD", .sel = {0, 3}, .ful = {0, 3} },
			{.data = "EFGHI", .sel = {4, 8}, .ful = {4, 8} },
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW(">D|EF|>D|>EF|");
	assert_int_equal(4, fake_update_called);
	assert_int_equal(2, fake_compare_called);
	ASSERT_CSUM(ctx, "SSSSNNNNSSSS");


	/** clean up */
	test_case_destroy(&ctx);
}


/**
 *
 * Many extents without overlapping
 *
 * Should look like this:
 * Fetch extent:	A  B  C  D | E  F
 * epoch 6 extent:	           |    F
 * epoch 5 extent:	           | E
 * epoch 4 extent:	         D |
 * epoch 3 extent:	      C    |
 * epoch 2 extent:	   B       |
 * epoch 1 extent:	A          |
 * index:		0  1  2  3 | 4  5
 */
static void
many_extents(void **state)
{
	struct vos_fetch_test_context ctx;

	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 6,
		.chunksize = 4,
		.rec_size = 1,
		.layout = {
			{.data = "A", .sel = {0, 0}, .ful = {0, 0} },
			{.data = "B", .sel = {1, 1}, .ful = {1, 1} },
			{.data = "C", .sel = {2, 2}, .ful = {2, 2} },
			{.data = "D", .sel = {3, 3}, .ful = {3, 3} },
			{.data = "E", .sel = {4, 4}, .ful = {4, 4} },
			{.data = "F", .sel = {5, 5}, .ful = {5, 5} },
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW(">A|B|C|D|>A|>B|>C|>D|>E|F|>E|>F|");
	ASSERT_CSUM(ctx, "NNNN");

	/** clean up */
	test_case_destroy(&ctx);
}



/**
 * Request begins before extent. This will be represented by VOS as an extent
 * that is a hole first.
 *
 * Should look like this:
 * Fetch extent:	_  _  X  W | V  U  T  S
 * epoch 1 extent:	      X  W | V  U  T  S
 * index:		0  1  2  3 | 4  5  6  7
 */
static void
request_that_begins_before_extent(void **state)
{
	struct vos_fetch_test_context ctx;

	/** Setup */
	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 8,
		.chunksize = 4,
		.rec_size = 1,
		.layout = {
			{.data = "", .sel = {0, 1}, .ful = {0, 1},
				.is_hole = true},
			{.data = "XWVUTS", .sel = {2, 7}, .ful = {2, 7},},
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	ASSERT_CSUM(ctx, "SSSS");

	FAKE_UPDATE_SAW("");
	assert_int_equal(0, fake_update_called);
	assert_int_equal(0, fake_compare_called);

	test_case_destroy(&ctx);
}

/**
 * Two extents with a gap in the middle. Requesting all. The hole will be
 * represented by VOS as a third extent that is a hole. Even though the first
 * extent's hi isn't aligned, the stored checksum will still work.
 *
 * Should look like this:
 * Fetch extent:	A  B  C | D  _  _ | G  H  I |
 * epoch 2 extent:	        |         | G  H  I |
 * epoch 1 extent:	A  B  C | D       |         |
 * index:		0  1  2 | 3  4  5 | 6  7  8 |
 */
static void
fetch_with_hole(void **state)
{
	struct vos_fetch_test_context ctx;

	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 9,
		.chunksize = 3,
		.rec_size = 1,
		.layout = {
			{.data = "ABCD", .sel = {0, 3}, .ful = {0, 3} },
			{.data = "", .sel = {4, 5}, .ful = {4, 5},
				.is_hole = true},
			{.data = "GHI", .sel = {6, 8}, .ful = {6, 8} },
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW("");
	ASSERT_CSUM(ctx, "SSSSSSSSSSSS");

	/** clean up */
	test_case_destroy(&ctx);
}

/**
 * Hole within a single chunk
 *
 * Should look like this:
 * Fetch extent:	A  B  C  _  _  _  G  H |
 * epoch 2 extent:	                  G  H |
 * epoch 1 extent:	A  B  C                |
 * index:		0  1  2  3  4  5  6  7 |
 */
static void
fetch_with_hole2(void **state)
{
	struct vos_fetch_test_context ctx;

	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 8,
		.chunksize = 8,
		.rec_size = 1,
		.layout = {
			{.data = "ABC", .sel = {0, 2}, .ful = {0, 2} },
			{.data = "", .sel = {3, 5}, .ful = {3, 5},
				.is_hole = true},
			{.data = "GHI", .sel = {6, 7}, .ful = {6, 7} },
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW(">ABC|GH|>ABC|>GH|");
	ASSERT_CSUM(ctx, "NNNN");

	/** clean up */
	test_case_destroy(&ctx);
}

/**
 *
 * Many holes in a single chunk
 *
 * Should look like this:
 * Fetch extent:	A  _  B  _  C  _  D  _  E  _  F
 * epoch 6 extent:	                              F
 * epoch 5 extent:	                        E
 * epoch 4 extent:	                  D
 * epoch 3 extent:	            C
 * epoch 2 extent:	      B
 * epoch 1 extent:	A
 * index:		0  1  2  3  4  5  6  7  8  9  10
 */
static void
fetch_with_hole3(void **state)
{
	struct vos_fetch_test_context ctx;

	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 20,
		.chunksize = 16,
		.rec_size = 1,
		.layout = {
			{.data = "A", .sel = {0, 0}, .ful = {0, 0} },
			{.data = "", .sel = {1, 1}, .ful = {1, 1},
				.is_hole = true},
			{.data = "B", .sel = {2, 2}, .ful = {2, 2} },
			{.data = "", .sel = {3, 3}, .ful = {3, 3},
				.is_hole = true},
			{.data = "C", .sel = {4, 4}, .ful = {4, 4} },
			{.data = "", .sel = {5, 5}, .ful = {5, 5},
				.is_hole = true},
			{.data = "D", .sel = {6, 6}, .ful = {6, 6} },
			{.data = "", .sel = {7, 7}, .ful = {7, 7},
				.is_hole = true},
			{.data = "E", .sel = {8, 8}, .ful = {8, 8} },
			{.data = "", .sel = {9, 9}, .ful = {9, 9},
				.is_hole = true},
			{.data = "F", .sel = {10, 10}, .ful = {10, 10} },
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW(">A|B|C|D|E|F|>A|>B|>C|>D|>E|>F|");
	ASSERT_CSUM(ctx, "NNNN");

	/** clean up */
	test_case_destroy(&ctx);
}

/**
 *
 * 2 holes, first spans a whole chunk, second starts in middle of a chunk and
 * ends in middle of next chunk
 *
 * Should look like this:
 * Fetch extent:	_  _  _  _  _  _  _  _ | A  B  C  D  E  F  _  _ | _  _  G  H  I  J  K  L
 * epoch 2 extent:	                       |                        |       G  H  I  J  K  L
 * epoch 1 extent:	                       | A  B  C  D  E  F       |
 * index:		0  1  2  3  4  5  6  7 | 8  9 10 11 12 13 14 15 |16 17 18 19 20 21 22 23
 */
static void
fetch_with_hole4(void **state)
{
	struct vos_fetch_test_context ctx;

	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 23,
		.chunksize = 8,
		.rec_size = 1,
		.layout = {
			{.data = "", .sel = {0, 7}, .ful = {0, 7},
				.is_hole = true},
			{.data = "ABCDEF", .sel = {8, 13}, .ful = {8, 13} },
			{.data = "", .sel = {14, 17}, .ful = {14, 17},
				.is_hole = true},
			{.data = "GHIJKL", .sel = {18, 23}, .ful = {18, 23} },
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW("");
	ASSERT_CSUM_EMPTY(ctx, 0);
	ASSERT_CSUM_IDX(ctx, "SSSS", 1);

	/** clean up */
	test_case_destroy(&ctx);
}

/**
 * Will create a new checksum for the first chunk, but there's a hole that
 * continues into the next chunk.
 *
 * Should look like this:
 * Fetch extent:	A  B  C  _ | _  _  _  _ | _  G  H      |
 * epoch 3 extent:	           |            |    G  H      |
 * epoch 2 extent:	   B  C    |            |              |
 * epoch 1 extent:	A          |            |              |
 * index:		0  1  2  3 | 4  5  6  7 | 8  9  10  11 |
 */
static void
fetch_with_hole5(void **state)
{
	struct vos_fetch_test_context ctx;

	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 12,
		.chunksize = 4,
		.rec_size = 1,
		.layout = {
			{.data = "ABC", .sel = {0, 0}, .ful = {0, 0} },
			{.data = "BC", .sel = {1, 2}, .ful = {1, 2} },
			{.data = "", .sel = {3, 8}, .ful = {3, 8},
				.is_hole = true},
			{.data = "GH", .sel = {9, 10}, .ful = {9, 10} },
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW(">A|BC|>A|>BC|");
	ASSERT_CSUM(ctx, "NNNN");
	ASSERT_CSUM_EMPTY(ctx, 1);
	ASSERT_CSUM_IDX(ctx, "SSSS", 2);

	/** clean up */
	test_case_destroy(&ctx);
}

/**
 * Will skip the first chunk of the request, then create a checksum for the A
 * in the second chunk. Request has two chunks even though it is only 1 chunk
 * in length.
 *
 * Should look like this:
 * Fetch extent:	   _  _  _ | A          |              |
 * epoch 1 extent:	           | A  B  C  D |              |
 * index:		0  1  2  3 | 4  5  6  7 | 8  9  10  11 |
 */
static void
fetch_with_hole6(void **state)
{
	struct vos_fetch_test_context ctx;

	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 1,
		.request_len = 4,
		.chunksize = 4,
		.rec_size = 1,
		.layout = {
			{.data = "", .sel = {1, 3}, .ful = {1, 3},
				.is_hole = true},
			{.data = "ABCD", .sel = {4, 4}, .ful = {4, 7} },
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW(">A|>ABCD|");
	ASSERT_CSUM_EMPTY(ctx, 0);
	ASSERT_CSUM_IDX(ctx, "NNNN", 1);

	/** clean up */
	test_case_destroy(&ctx);
}


/**
 * Hole within a single chunk
 *
 * Should look like this:
 * Fetch extent:	   A | B  _ | _  _ |      | H  I |  J  K |  L  M
 * epoch 3 punch:            |    _ | _  _ | _  _ |
 * epoch 1 extent:	   A | B  C | D  E | F  G | H  I |  J  K |  L  M
 * index:		0  1 | 2  3 | 4  5 | 6  7 | 8  9 | 10 11 | 12 13
 */
static void
fetch_with_hole7(void **state)
{
	struct vos_fetch_test_context ctx;

	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 1,
		.request_len = 13,
		.chunksize = 2,
		.rec_size = 1,
		.layout = {
			{.data = "ABCDEFGHIJKLM", .sel = {1, 2}, .ful = {1, 13} },
			{.data = "", .sel = {3, 7}, .ful = {3, 7}, .is_hole = true},
			{.data = "HIJKLM", .sel = {8, 13}, .ful = {1, 13} },
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW(">B|>BC|");
	ASSERT_CSUM(ctx, "SSSS");

	/** clean up */
	test_case_destroy(&ctx);
}

/**
 * If multiple recx are part of an iod, there will be more biov's than needed
 * for a single recx.
 *
 * Fetch extent:	A  B  C  D | E
 * epoch 2 extent:	           |    F  G  H | I  J  K
 * epoch 1 extent:	A  B  C  D | E          |
 * index:		0  1  2  3 | 4  5  6  7 | 8  9  10
*/
static void
request_is_only_part_of_biovs(void **state)
{
	struct vos_fetch_test_context ctx;

	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 5,
		.chunksize = 4,
		.rec_size = 1,
		.layout = {
			{.data = "ABCDE",
				.sel = {0, 4}, .ful = {0, 4} },
			{.data = "FGHIJK",
				.sel = {5, 10}, .ful = {5, 10} },
			{.data = NULL}
		}
	});

	/** Act */
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	FAKE_UPDATE_SAW("");
	ASSERT_CSUM(ctx, "SSSSSSSS");

	/** clean up */
	test_case_destroy(&ctx);
}

static void
larger_records(void **state)
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

	ARRAY_TEST_CASE_CREATE(&ctx, {
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
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	/** 1 record from 1st extent (mnop) and 2 records from 2nd extent
	 * (ABCDEFGH)
	 */
	FAKE_UPDATE_SAW(">mnop|ABCDEFGH|>mnop|>ABCDEFGH|");
	ASSERT_CSUM(ctx, "SSSSNNNN");

	/** clean up */
	test_case_destroy(&ctx);
}

static void
larger_records2(void **state)
{
	struct vos_fetch_test_context	ctx;
	char				*large_data01;
	char				*large_data02;

	D_ALLOC(large_data01, 1024 * 16);
	D_ALLOC(large_data02, 1024 * 16);

	memset(large_data01, 'A', 1024 * 16);
	memset(large_data02, 'B', 1024 * 16);

	ARRAY_TEST_CASE_CREATE(&ctx, {
		.request_idx = 0,
		.request_len = 12,
		.chunksize = 1024 * 32,
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
	assert_success(fetch_csum_verify_bsgl_with_args(&ctx));

	/** Verify */
	assert_int_equal(4, fake_update_called);
	assert_int_equal(2, fake_compare_called);

	/** clean up */
	test_case_destroy(&ctx);
	D_FREE(large_data01);
	D_FREE(large_data02);
}

/**
 * ----------------------------------------------------------------------------
 * Single Value Test
 * ----------------------------------------------------------------------------
 */
static void
update_fetch_sv(void **state)
{
	struct daos_csummer	*csummer;
	struct bio_iov		 biov = {0};
	struct bio_sglist	 bsgl = {0};
	/** vos_update_begin will populate a list of csum_infos (one for each
	 * biov 'extent'
	 */
	struct dcs_csum_info	 from_vos_begin = {0};
	struct dcs_ci_list	 from_vos_begin_list = {0};
	struct dcs_csum_info	 csum_info = {0};
	struct dcs_iod_csums	 iod_csums = {0};

	uint32_t		 iod_csum_value = 0;
	daos_iod_t		 iod = {0};
	char			*data = "abcd";
	uint32_t		 csum = 0x12345678;

	daos_csummer_init(&csummer, &fake_algo, 4, 0);

	iod.iod_type = DAOS_IOD_SINGLE;
	iod.iod_size = strlen(data);
	iod.iod_nr = 1;

	ci_set(&csum_info, &iod_csum_value, sizeof(uint32_t), sizeof(uint32_t),
	       1, CSUM_NO_CHUNK, 1);
	iod_csums.ic_data = &csum_info;

	biov.bi_buf = data;
	biov.bi_data_len = strlen(data);

	bsgl.bs_iovs = &biov;
	bsgl.bs_nr_out = 1;
	bsgl.bs_nr = 1;

	ci_set(&from_vos_begin, &csum, sizeof(uint32_t), sizeof(uint32_t), 1,
	       CSUM_NO_CHUNK, 1);
	assert_success(dcs_csum_info_list_init(&from_vos_begin_list, 1));
	dcs_csum_info_save(&from_vos_begin_list, &from_vos_begin);

	ds_csum_add2iod(&iod, csummer, &bsgl, &from_vos_begin_list, NULL,
			&iod_csums);

	assert_memory_equal(csum_info.cs_csum, from_vos_begin.cs_csum,
			    from_vos_begin.cs_len);

	daos_csummer_destroy(&csummer);
	dcs_csum_info_list_fini(&from_vos_begin_list);
}

#define assert_csum_err(fn) assert_rc_equal(-DER_CSUM, (fn))

static void
key_verify(void **state)
{
	struct daos_csummer  *csummer;
	daos_key_t            dkey = {0};
	char                  dkey_buf[32] = {0};
	struct dcs_csum_info *dkey_csum = NULL;
	daos_iod_t            iods  = {0};
	struct dcs_iod_csums *iod_csums = NULL;
	char                  akey_buf[32] = {0};

	daos_csummer_init_with_type(&csummer, HASH_TYPE_CRC32, 4, 0);

	d_iov_set(&dkey, dkey_buf, ARRAY_SIZE(dkey_buf));
	sprintf(dkey_buf, "dkey");

	d_iov_set(&iods.iod_name, akey_buf, ARRAY_SIZE(akey_buf));
	sprintf(akey_buf, "akey");

	assert_success(daos_csummer_calc_key(csummer, &dkey, &dkey_csum));
	assert_success(daos_csummer_calc_iods(csummer, NULL, &iods, NULL, 1, true, NULL, 0,
					      &iod_csums));

	assert_success(ds_csum_verify_keys(csummer, &dkey, dkey_csum, &iods, iod_csums, 1, NULL));

	MEASURE_TIME(ds_csum_verify_keys(csummer, &dkey, dkey_csum, &iods, iod_csums, 1, NULL),
		     noop(), noop());

	sprintf(dkey_buf, "corrupted");
	assert_csum_err(ds_csum_verify_keys(csummer, &dkey, dkey_csum, &iods, iod_csums, 1, NULL));

	daos_csummer_free_ci(csummer, &dkey_csum);
	daos_csummer_free_ic(csummer, &iod_csums);
	daos_csummer_destroy(&csummer);
}

static int
sct_setup(void **state)
{
	return 0;
}

static int
sct_teardown(void **state)
{
	reset_fake_algo();
	return 0;
}

/* Convenience macro for unit tests */
#define	TA(desc, test_fn) \
	{ desc, test_fn, sct_setup, sct_teardown }

static const struct CMUnitTest srv_csum_tests[] = {
	TA("SRV_CSUM_ARRAY01: Whole extent requested",
	   request_that_matches_single_extent),
	TA("SRV_CSUM_ARRAY02: Whole extent requested, broken into multiple "
	   "chunks", request_that_matches_single_extent_multiple_chunks),
	TA("SRV_CSUM_ARRAY03: Whole extent requested, broken into multiple "
	   "chunks, not aligned to chunk",
	   request_that_matches_single_extent_multiple_chunks_not_aligned),
	TA("SRV_CSUM_ARRAY04: Multiple aligned extents requested",
	   request_that_matches_multiple_aligned_extents),
	TA("SRV_CSUM_ARRAY05: Multiple aligned extents requested",
	   request_that_matches_multiple_aligned_extents2),
	TA("SRV_CSUM_ARRAY06: Request more than exists",
	   request_that_is_more_than_extents),
	TA("SRV_CSUM_ARRAY07: Partial Extents, first part of chunk",
	   partial_chunk_request0),
	TA("SRV_CSUM_ARRAY08: Partial Extents, last part of chunk",
	   partial_chunk_request1),
	TA("SRV_CSUM_ARRAY09: Partial Extents, middle part of chunk",
	   partial_chunk_request2),
	TA("SRV_CSUM_ARRAY10: Partial chunks and full chunks",
	   request_needs_new_and_copy),
	TA("SRV_CSUM_ARRAY11: Partial Extents, chunks don't align",
	   unaligned_chunks_csums_new_csum_is_created),
	TA("SRV_CSUM_ARRAY12: Partial Extents, first extent isn't aligned",
	   unaligned_first_chunk),
	TA("SRV_CSUM_ARRAY13: Partial Extents, extent smaller than chunk",
	   extent_smaller_than_chunk),
	TA("SRV_CSUM_ARRAY14: Extent is larger than chunk",
	   extent_larger_than_request),
	TA("SRV_CSUM_ARRAY15: Full and partial chunks",
	   fetch_multiple_unaligned_extents),
	TA("SRV_CSUM_ARRAY16: Many sequential extents", many_extents),
	TA("SRV_CSUM_ARRAY17: Begins with hole", request_that_begins_before_extent),
	TA("SRV_CSUM_ARRAY18: Hole in middle", fetch_with_hole),
	TA("SRV_CSUM_ARRAY19: Hole in middle", fetch_with_hole2),
	TA("SRV_CSUM_ARRAY20: Many holes in middle", fetch_with_hole3),
	TA("SRV_CSUM_ARRAY21: First chunk is hole", fetch_with_hole4),
	TA("SRV_CSUM_ARRAY22: Handle holes while creating csums", fetch_with_hole5),
	TA("SRV_CSUM_ARRAY22: Hole spans past first chunk", fetch_with_hole6),
	TA("SRV_CSUM_ARRAY13: Hole in middle spans multiple chunks", fetch_with_hole7),
	TA("SRV_CSUM_ARRAY24: First recx request of multiple", request_is_only_part_of_biovs),
	TA("SRV_CSUM_ARRAY25: Fetch with larger records1", larger_records),
	TA("SRV_CSUM_ARRAY26: Fetch with larger records2", larger_records2),
	TA("SRV_CSUM_SV01: Various scenarios for update/fetch with fault injection",
	   update_fetch_sv),
	TA("SRV_CSUM_01: Server side key verification", key_verify),
};

int
main(int argc, char **argv)
{
	int	rc = 0;
#if CMOCKA_FILTER_SUPPORTED == 1 /** for cmocka filter(requires cmocka 1.1.5) */
	char	 filter[1024];

	if (argc > 1) {
		snprintf(filter, 1024, "*%s*", argv[1]);
		cmocka_set_test_filter(filter);
	}
#endif

	rc += cmocka_run_group_tests_name(
		"Storage and retrieval of checksums for Array Type",
		srv_csum_tests, NULL, NULL);


	return rc;
}
