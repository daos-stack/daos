/**
 * (C) Copyright 2019-2020 Intel Corporation.
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

#include <daos_types.h>
#include <daos/checksum.h>
#include <evt_priv.h>
#include "vts_io.h"

/*
 * Structure which uniquely identifies a extent in a key value pair.
 */
struct extent_key {
	daos_handle_t	container_hdl;
	daos_unit_oid_t	object_id;
	daos_key_t	dkey;
	daos_key_t	akey;
	char		dkey_buf[UPDATE_DKEY_SIZE];
	char		akey_buf[UPDATE_AKEY_SIZE];
};

/*
 * Initialize the extent key from the io_test_args
 */
void
extent_key_from_test_args(struct extent_key *k,
			       struct io_test_args *args)
{
	/* Set up dkey and akey */
	dts_key_gen(&k->dkey_buf[0], args->dkey_size, args->dkey);
	dts_key_gen(&k->akey_buf[0], args->akey_size, args->akey);
	set_iov(&k->dkey, &k->dkey_buf[0], args->ofeat & DAOS_OF_DKEY_UINT64);
	set_iov(&k->akey, &k->akey_buf[0], args->ofeat & DAOS_OF_AKEY_UINT64);

	k->container_hdl = args->ctx.tc_co_hdl;
	k->object_id = args->oid;
}

struct recx_config {
	uint64_t idx;
	uint64_t nr;
	uint32_t csum_count;
};

struct expected_biov {
	uint64_t prefix;
	uint64_t suffix;
};

#define MAX_RECX 10
struct test_case_args {
	uint32_t		rec_size;
	uint32_t		chunksize;
	struct recx_config	update_recxs[MAX_RECX + 1];
	struct recx_config	fetch_recxs[MAX_RECX + 1];
	uint32_t		biovs_nr;
	struct expected_biov	biovs[MAX_RECX + 1];
	uint32_t		holes_nr;
};

#define	CSUM_FOR_ARRAYS_TEST_CASE(state, ...) \
	csum_for_arrays_test_case(state, (struct test_case_args)__VA_ARGS__)
/** index to a specific csum within a csum info array */
struct cia_idx {
	uint32_t ci_idx;
	uint32_t csum_idx;
};

static bool
cia_idx_next(struct cia_idx *idx, struct dcs_csum_info *infos,
	     uint64_t infos_nr)
{
	idx->csum_idx++;
	if (infos[idx->ci_idx].cs_nr >= idx->csum_idx) {
		idx->ci_idx++;
		idx->csum_idx = 0;
	}
	return idx->ci_idx < infos_nr;
}

static uint8_t *
cia_idx_get_csum(struct cia_idx *idx, struct dcs_csum_info *infos)
{
	return ci_idx2csum(&infos[idx->ci_idx], idx->csum_idx);
}

void
csum_for_arrays_test_case(void *const *state, struct test_case_args test)
{
	struct dcs_csum_info	*csum_infos;
	struct dcs_iod_csums	 iod_csums = {0};
	daos_iod_t		 iod = {0};
	daos_recx_t		 recx[MAX_RECX];
	d_sg_list_t		 sgl;
	int			 update_recx_nr = 0;
	uint32_t		 data_size = 0;
	int			 fetch_recx_nr = 0;
	uint32_t		 csum_size;
	daos_handle_t		 ioh = DAOS_HDL_INVAL;
	uint32_t		 f_csums_nr;
	struct dcs_csum_info	*f_csums;
	struct bio_desc		*biod;
	struct bio_sglist	*bsgl;
	int			 expected_csums_nr;
	int			 i;
	struct extent_key	 k;
	int			 rc;
	struct cia_idx		 csum_idx = {0};
	struct cia_idx		 f_csums_idx = {0};

	extent_key_from_test_args(&k, *state);
	csum_size = 8;

	while (test.update_recxs[update_recx_nr].nr > 0) {
		data_size += test.update_recxs[update_recx_nr].nr *
			     test.rec_size;
		update_recx_nr++;
	}

	while (test.fetch_recxs[fetch_recx_nr].nr > 0)
		fetch_recx_nr++;

	for (i = 0; i < update_recx_nr; i++) {
		recx[i].rx_nr = test.update_recxs[i].nr;
		recx[i].rx_idx = test.update_recxs[i].idx;
	}

	D_ALLOC_ARRAY(csum_infos, update_recx_nr);
	iod_csums.ic_data = csum_infos;
	iod_csums.ic_nr = update_recx_nr;

	for (i = 0; i < update_recx_nr; i++) {
		uint64_t csum_buf_len;

		csum_buf_len = (uint64_t)csum_size *
				test.update_recxs[i].csum_count;
		csum_infos[i].cs_type = 1;
		csum_infos[i].cs_nr = test.update_recxs[i].csum_count;
		csum_infos[i].cs_chunksize = test.chunksize;
		D_ALLOC(csum_infos[i].cs_csum, csum_buf_len);
		memset(csum_infos[i].cs_csum, i + 1, csum_buf_len);

		csum_infos[i].cs_buf_len = csum_buf_len;
		csum_infos[i].cs_len = csum_size;
	}

	iod.iod_name = k.akey;
	iod.iod_size = test.rec_size;
	iod.iod_nr = update_recx_nr;
	iod.iod_recxs = recx;
	iod.iod_type = DAOS_IOD_ARRAY;

	dts_sgl_init_with_strings_repeat(&sgl, (data_size / 16) + 1, 1,
					 "0123456789ABCDEF");

	/** update with a checksum */
	rc = vos_obj_update(k.container_hdl, k.object_id, 1, 0, 0, &k.dkey, 1,
			    &iod, &iod_csums, &sgl);
	if (rc != 0)
		fail_msg("vos_obj_update failed with error code %d", rc);

	iod.iod_nr = fetch_recx_nr;
	for (i = 0; i < fetch_recx_nr; i++) {
		recx[i].rx_nr = test.fetch_recxs[i].nr;
		recx[i].rx_idx = test.fetch_recxs[i].idx;
	}

	/**
	 * fetch with a checksum. Can't use vos_obj_fetch because need to
	 * have access to the vos io handler to get the checksums (this is
	 * how the server object layer already interfaces with VOS)
	 */
	vos_fetch_begin(k.container_hdl, k.object_id, 1, 0, &k.dkey, 1, &iod,
			0, NULL, &ioh, NULL);

	biod = vos_ioh2desc(ioh);
	bsgl = bio_iod_sgl(biod, 0);
	f_csums_nr = vos_ioh2ci_nr(ioh);
	f_csums = vos_ioh2ci(ioh);

	assert_int_equal(test.biovs_nr, bsgl->bs_nr_out);

	for (i = 0; i < bsgl->bs_nr_out; i++) {
		struct bio_iov *biov = &bsgl->bs_iovs[i];
		struct expected_biov *expected_biov = &test.biovs[i];

		assert_int_equal(expected_biov->prefix, biov->bi_prefix_len);
		assert_int_equal(expected_biov->suffix, biov->bi_suffix_len);
	}

	/** should be 1 csum info per biov (minus holes) */
	expected_csums_nr = test.biovs_nr - test.holes_nr;
	assert_int_equal(expected_csums_nr, f_csums_nr);
	assert_non_null(f_csums);

	do {
		assert_memory_equal(cia_idx_get_csum(&csum_idx, csum_infos),
				    cia_idx_get_csum(&f_csums_idx, f_csums),
				    csum_size);
	} while (cia_idx_next(&csum_idx, csum_infos, update_recx_nr) &&
		cia_idx_next(&f_csums_idx, f_csums, f_csums_nr));

	/** Clean up */
	vos_fetch_end(ioh, rc);
	d_sgl_fini(&sgl, true);

	for (i = 0; i < update_recx_nr; i++)
		D_FREE(csum_infos[i].cs_csum);

	D_FREE(csum_infos);
}

/** Single chunk extent updated and fetched */
static void
update_fetch_csum_for_array_1(void **state)
{
	CSUM_FOR_ARRAYS_TEST_CASE(state, {
		.chunksize = 8,
		.rec_size = 1,
		.update_recxs =  {
			{.idx = 0, .nr = 8, .csum_count = 1},
		},
		.biovs_nr = 1,
		.fetch_recxs = {
			{.idx = 0, .nr = 8, .csum_count = 1},
		}
	});
}

/** Two single chunk extents updated and fetched */
static void
update_fetch_csum_for_array_2(void **state)
{
	CSUM_FOR_ARRAYS_TEST_CASE(state, {
		.chunksize = 8,
		.rec_size = 1,
		.update_recxs =  {
			{.idx = 0, .nr = 8, .csum_count = 1},
			{.idx = 8, .nr = 8, .csum_count = 1},
		},
		.biovs_nr = 2,
		.fetch_recxs = {
			{.idx = 0, .nr = 8, .csum_count = 1},
			{.idx = 8, .nr = 8, .csum_count = 1},
		}
	});
}

/** Two chunk extent */
static void
update_fetch_csum_for_array_3(void **state)
{
	CSUM_FOR_ARRAYS_TEST_CASE(state, {
		.chunksize = 8,
		.rec_size = 1,
		.update_recxs =  {
			{.idx = 0, .nr = 16, .csum_count = 2},
		},
		.biovs_nr = 1,
		.fetch_recxs = {
			{.idx = 0, .nr = 16, .csum_count = 2},

		}
	});
}

/** Update with two single chunk extents, fetch two chunks */
static void
update_fetch_csum_for_array_4(void **state)
{
	CSUM_FOR_ARRAYS_TEST_CASE(state, {
		.chunksize = 8,
		.rec_size = 1,
		.update_recxs =  {
			{.idx = 0, .nr = 8, .csum_count = 1},
			{.idx = 8, .nr = 8, .csum_count = 1},
		},
		.biovs_nr = 2,
		.fetch_recxs = {
			{.idx = 0, .nr = 16, .csum_count = 2},

		}
	});
}

/** Update with single two chunk extent, fetch two single chunk extents */
static void
update_fetch_csum_for_array_5(void **state)
{
	CSUM_FOR_ARRAYS_TEST_CASE(state, {
		.chunksize = 8,
		.rec_size = 1,
		.update_recxs =  {
			{.idx = 0, .nr = 16, .csum_count = 2},

		},
		.biovs_nr = 2,
		.fetch_recxs = {
			{.idx = 0, .nr = 8, .csum_count = 1},
			{.idx = 8, .nr = 8, .csum_count = 1},
		}
	});
}

/** Update with single chunk extent, fetch part of the extent */
static void
update_fetch_csum_for_array_6(void **state)
{
	CSUM_FOR_ARRAYS_TEST_CASE(state, {
		.chunksize = 8,
		.rec_size = 1,
		.update_recxs =  {
			{.idx = 0, .nr = 8, .csum_count = 1},
		},
		.biovs_nr = 1,
		.fetch_recxs = {
			{.idx = 1, .nr = 5, .csum_count = 1},
		},
		.biovs = { {.prefix = 1, .suffix = 2} }
	});
}

/** Update with partial chunk, fetch same partial chunk */
static void
update_fetch_csum_for_array_7(void **state)
{
	CSUM_FOR_ARRAYS_TEST_CASE(state, {
		.chunksize = 8,
		.rec_size = 4,
		.update_recxs =  {
			{.idx = 2, .nr = 4, .csum_count = 2},
		},
		.biovs_nr = 1,
		.fetch_recxs = {
			{.idx = 2, .nr = 4, .csum_count = 2},
		}
	});
}

/** Update with partial chunk, fetch extent smaller than that */
static void
update_fetch_csum_for_array_8(void **state)
{
	CSUM_FOR_ARRAYS_TEST_CASE(state, {
		.chunksize = 8,
		.rec_size = 4,
		.update_recxs =  {
			{.idx = 2, .nr = 4, .csum_count = 2},
		},
		.fetch_recxs = {
			{.idx = 3, .nr = 2, .csum_count = 1},
		},
		.biovs_nr = 1,
		.biovs = { {.prefix = 4, .suffix = 4} }
	});
}

/** Update with several, sequential extents, fetch most of the array */
static void
update_fetch_csum_for_array_9(void **state)
{
	CSUM_FOR_ARRAYS_TEST_CASE(state, {
		.chunksize = 8,
		.rec_size = 1,
		.update_recxs =  {
			{.idx = 0, .nr = 16, .csum_count = 2},
			{.idx = 16, .nr = 4, .csum_count = 1},
			{.idx = 20, .nr = 10, .csum_count = 2},
			{.idx = 30, .nr = 6, .csum_count = 2},
		},
		.fetch_recxs = {
			{.idx = 0, .nr = 33, .csum_count = 5},
		},
		.biovs_nr = 4,
		.biovs = {
			{.prefix = 0, .suffix = 0},
			{.prefix = 0, .suffix = 0},
			{.prefix = 0, .suffix = 0},
			{.prefix = 0, .suffix = 3},
		}
	});
}

/** Fetch with holes */
static void
update_fetch_csum_for_array_10(void **state)
{
	CSUM_FOR_ARRAYS_TEST_CASE(state, {
		.chunksize = 8,
		.rec_size = 1,
		.update_recxs =  {
			{.idx = 0, .nr = 8, .csum_count = 1},
			{.idx = 16, .nr = 8, .csum_count = 1},
		},
		.fetch_recxs = {
			{.idx = 0, .nr = 24, .csum_count = 1},
		},
		.biovs_nr = 3, /** 1 for the hole */
		.holes_nr = 1
	});
}

/**
 * -------------------------------------
 * Helper function tests
 * -------------------------------------
 */

struct evt_csum_test_args {
	uint32_t lo;
	uint32_t hi;
	uint32_t inob;
	uint32_t chunksize;
	uint16_t csum_size;
};

struct evt_csum_test_structures {
	struct evt_root		root;
	struct evt_context	tcx;
	struct evt_extent	extent;
};

static void
evt_csum_test_setup(struct evt_csum_test_structures *structs,
	struct evt_csum_test_args *args)
{
	memset(structs, 0, sizeof(*structs));
	structs->tcx.tc_root = &structs->root;
	structs->extent.ex_lo = args->lo;
	structs->extent.ex_hi = args->hi;
	structs->root.tr_inob = args->inob;
	structs->root.tr_csum_chunk_size = args->chunksize;
	structs->root.tr_csum_len = args->csum_size;
}

#define layout_is_csum_count(expected, ...)\
	evt_csum_count_test(expected, (struct evt_csum_test_args)__VA_ARGS__)

static void
evt_csum_count_test(uint32_t expected, struct evt_csum_test_args args)
{
	struct evt_csum_test_structures test;

	evt_csum_test_setup(&test, &args);
	daos_size_t csum_count = evt_csum_count(&test.tcx, &test.extent);

	if (expected != csum_count) {
		fail_msg("expected (%d) != csum_count (%"PRIu64")\n\tFrom "
			"lo: %d, hi: %d, inob: %d, chunk size: %d",
			 expected,
			 csum_count,
			 args.lo,
			 args.hi,
			 args.inob,
			 args.chunksize
		);
	}
}

#define layout_has_csum_buf_len(expected, ...)\
	evt_csum_buf_len_test(expected, (struct evt_csum_test_args)__VA_ARGS__)

static void
evt_csum_buf_len_test(uint32_t expected, struct evt_csum_test_args args)
{
	struct evt_csum_test_structures test;

	evt_csum_test_setup(&test, &args);

	daos_size_t csum_buf_len = evt_csum_buf_len(&test.tcx, &test.extent);

	if (expected != csum_buf_len) {
		fail_msg("expected (%d) != csum_buf_len (%"PRIu64")\n\tFrom "
			"lo: %d, hi: %d, inob: %d, chunk size: %d",
			 expected,
			 csum_buf_len,
			 args.lo,
			 args.hi,
			 args.inob,
			 args.chunksize
		);
	}
}

void
evt_csum_helper_functions_tests(void **state)
{
	/**
	 * Testing evt_csum_count
	 */
	layout_is_csum_count(0, {.lo = 0, .hi = 0, .inob = 0, .chunksize = 0});
	layout_is_csum_count(1, {.lo = 0, .hi = 3, .inob = 1, .chunksize = 4});
	layout_is_csum_count(2, {.lo = 0, .hi = 3, .inob = 2, .chunksize = 4});
	layout_is_csum_count(2, {.lo = 0, .hi = 3, .inob = 1, .chunksize = 2});

	/** Cross chunk size alignment */
	layout_is_csum_count(2, {.lo = 1, .hi = 7, .inob = 1, .chunksize = 4});
	layout_is_csum_count(2, {.lo = 1, .hi = 5, .inob = 1, .chunksize = 4});
	layout_is_csum_count(3, {.lo = 1, .hi = 9, .inob = 1, .chunksize = 4});

	/** Some larger ... more realistic values */
	const uint32_t val64K = 1024 * 64;
	const uint32_t val256K = 1024 * 256;
	const uint64_t val1G = 1024 * 1024 * 1024;

	layout_is_csum_count(val256K, {.lo = 0, .hi = val1G - 1, .inob = 16,
		.chunksize = val64K});

	/**
	 * Testing evt_csum_buf_len
	 */
	layout_has_csum_buf_len(0, {.lo = 0, .hi = 0, .inob = 0, .chunksize = 0,
		.csum_size = 8});
	layout_has_csum_buf_len(8, {.lo = 0, .hi = 3, .inob = 1, .chunksize = 4,
		.csum_size = 8});
	layout_has_csum_buf_len(16, {.lo = 0, .hi = 3, .inob = 2,
		.chunksize = 4, .csum_size = 8});
	layout_has_csum_buf_len(16, {.lo = 0, .hi = 3, .inob = 1,
		.chunksize = 2, .csum_size = 8});

	layout_has_csum_buf_len(val256K * 64, {.lo = 0, .hi = val1G - 1,
		.inob = 16, .chunksize = val64K, .csum_size = 64});
}

/**
 * ------------------------------
 * Testing evt entry alignment
 * ------------------------------
 */
struct test_evt_entry_aligned_args {
	uint64_t		rb;
	uint64_t		chunksize;
	struct evt_extent	sel;
	struct evt_extent	ext;
};

#define EVT_ENTRY_ALIGNED_TESTCASE(lo, hi, ...) \
	evt_entry_aligned_testcase(__FILE__, __LINE__, lo, hi, \
	(struct test_evt_entry_aligned_args) __VA_ARGS__)

static void evt_entry_aligned_testcase(char *file, int line,
				       daos_off_t expected_lo,
				       daos_off_t expected_hi,
				       struct test_evt_entry_aligned_args args)
{
	struct evt_entry entry = {0};
	struct evt_extent result;

	entry.en_sel_ext = args.sel;
	entry.en_ext = args.ext;
	entry.en_csum.cs_chunksize = args.chunksize;
	result = evt_entry_align_to_csum_chunk(&entry, args.rb);

	if (expected_lo != result.ex_lo)
		fail_msg("%s:%d lo - expected %lu but found %lu\n",
			 file, line, expected_lo, result.ex_lo);
	if (expected_hi != result.ex_hi)
		fail_msg("%s:%d hi - expected %lu but found %lu\n",
			 file, line, expected_hi, result.ex_hi);
}

static void
evt_entry_aligned_tests(void **state)
{
	/** Testing lower bound alignment */
	EVT_ENTRY_ALIGNED_TESTCASE(0, 1,
	{
		.chunksize = 2,
		.rb = 1,
		.sel = {.ex_lo = 1, .ex_hi = 1},
		.ext = {.ex_lo = 0, .ex_hi = 1}
	});
	EVT_ENTRY_ALIGNED_TESTCASE(2, 5,
	{
		.chunksize = 2,
		.rb = 1,
		.sel = {.ex_lo = 3, .ex_hi = 5},
		.ext = {.ex_lo = 0, .ex_hi = 5}
	});
	EVT_ENTRY_ALIGNED_TESTCASE(0, 7,
	{
		.chunksize = 4,
		.rb = 1,
		.sel = {.ex_lo = 3, .ex_hi = 7},
		.ext = {.ex_lo = 0, .ex_hi = 7}
	});

	/** Testing upper bound alignment */
	EVT_ENTRY_ALIGNED_TESTCASE(0, 1,
	{
		.chunksize = 2,
		.rb = 1,
		.sel = {.ex_lo = 0, .ex_hi = 1},
		.ext = {.ex_lo = 0, .ex_hi = 1}
	});
	EVT_ENTRY_ALIGNED_TESTCASE(0, 3,
	{
		.chunksize = 2,
		.rb = 1,
		.sel = {.ex_lo = 0, .ex_hi = 2},
		.ext = {.ex_lo = 0, .ex_hi = 4}
	});
	EVT_ENTRY_ALIGNED_TESTCASE(0, 7,
	{
		.chunksize = 4,
		.rb = 1,
		.sel = {.ex_lo = 0, .ex_hi = 5},
		.ext = {.ex_lo = 0, .ex_hi = 10}
	});

	/** Bounded by the actual extent */
	EVT_ENTRY_ALIGNED_TESTCASE(1, 10,
	{
		.chunksize = 4,
		.rb = 1,
		.sel = {.ex_lo = 2, .ex_hi = 9},
		.ext = {.ex_lo = 1, .ex_hi = 10}
	});

	/** Testing different record and chunk sizes */
	EVT_ENTRY_ALIGNED_TESTCASE(0, 7,
	{
		.chunksize = 16,
		.rb = 4,
		.sel = {.ex_lo = 0, .ex_hi = 5},
		.ext = {.ex_lo = 0, .ex_hi = 10}
	});
	EVT_ENTRY_ALIGNED_TESTCASE(4, 7,
	{
		.chunksize = 16,
		.rb = 4,
		.sel = {.ex_lo = 5, .ex_hi = 5},
		.ext = {.ex_lo = 0, .ex_hi = 10}
	});
	EVT_ENTRY_ALIGNED_TESTCASE(500, 1024*128 - 1,
	{
		.chunksize = 1024*32, /** 32K */
		.rb = 1,
		.sel = {.ex_lo = 1000, .ex_hi = 1024*100},
		.ext = {.ex_lo = 500, .ex_hi = 1024*1000}
	});
	EVT_ENTRY_ALIGNED_TESTCASE(UINT64_MAX, UINT64_MAX,
	{
		.chunksize = 1024*32, /** 32K */
		.rb = 5,
		.sel = {.ex_lo = UINT64_MAX, .ex_hi = UINT64_MAX},
		.ext = {.ex_lo = UINT64_MAX, .ex_hi = UINT64_MAX}
	});
}

static void
test_evt_entry_csum_update(void **state)
{
	const uint32_t			csum_buf_len = 32;
	uint8_t				buf[csum_buf_len];
	struct dcs_csum_info		actual;
	const struct dcs_csum_info	expected  = {
		.cs_buf_len = csum_buf_len,
		.cs_nr = 4,
		.cs_csum = buf,
		.cs_len = 8,
		.cs_chunksize = 4,
		.cs_type = 3
	};
	struct evt_extent		ext = {.ex_lo = 0, .ex_hi = 31};
	struct evt_extent		sel = {.ex_lo = 0, .ex_hi = 7};

	actual = expected;

	/** Don't update unnecessarily */
	evt_entry_csum_update(&ext, &sel, &actual, 1);
	assert_int_equal(expected.cs_nr, actual.cs_nr);
	assert_int_equal(expected.cs_buf_len, actual.cs_buf_len);
	assert_ptr_equal(expected.cs_csum, actual.cs_csum);

	/** Will still need the first checksum to verify the first chunk */
	actual = expected;
	sel.ex_lo = 3;
	evt_entry_csum_update(&ext, &sel, &actual, 1);
	assert_int_equal(expected.cs_nr, actual.cs_nr);
	assert_int_equal(expected.cs_buf_len, actual.cs_buf_len);
	assert_ptr_equal(expected.cs_csum, actual.cs_csum);

	/**
	 * Because the selected extent doesn't include the first chunk, the
	 * first checksum should be removed
	 */
	actual = expected;
	sel.ex_lo = 4;
	evt_entry_csum_update(&ext, &sel, &actual, 1);
	assert_int_equal(expected.cs_nr - 1, actual.cs_nr);
	assert_int_equal(expected.cs_buf_len - expected.cs_len,
		actual.cs_buf_len);
	assert_ptr_equal(expected.cs_csum + expected.cs_len, actual.cs_csum);

	/**
	 * Only 1 byte of second chunk, but still keep second checksum
	 */
	actual = expected;
	sel.ex_lo = 7;
	evt_entry_csum_update(&ext, &sel, &actual, 1);
	assert_int_equal(expected.cs_nr - 1, actual.cs_nr);
	assert_int_equal(expected.cs_buf_len - expected.cs_len,
		actual.cs_buf_len);
	assert_ptr_equal(expected.cs_csum + expected.cs_len, actual.cs_csum);

	/**
	 * Only 1 byte of second chunk, but still keep second checksum
	 */
	actual = expected;
	sel.ex_lo = 8;
	sel.ex_hi = 16;
	evt_entry_csum_update(&ext, &sel, &actual, 1);
	assert_int_equal(expected.cs_nr - 2, actual.cs_nr);
	assert_int_equal(expected.cs_buf_len - expected.cs_len * 2,
		actual.cs_buf_len);
	assert_ptr_equal(expected.cs_csum + expected.cs_len * 2,
			 actual.cs_csum);
}

int setup(void **state)
{
	return 0;
}

int teardown(void **state)
{
	return 0;
}

#define	VOS(desc, test_fn) \
	{ "VOS_CSUM" desc, test_fn, setup, teardown}

static const struct CMUnitTest update_fetch_checksums_for_array_types[] = {
	VOS("01: Single chunk", update_fetch_csum_for_array_1),
	VOS("02: Two extents", update_fetch_csum_for_array_2),
	VOS("03: Two chunks", update_fetch_csum_for_array_3),
	VOS("04: Two extents -> one extent", update_fetch_csum_for_array_4),
	VOS("05: One extent -> two extents", update_fetch_csum_for_array_5),
	VOS("06: One chunk -> partial", update_fetch_csum_for_array_6),
	VOS("07: Partial -> partial", update_fetch_csum_for_array_7),
	VOS("08: Partial -> more partial", update_fetch_csum_for_array_8),
	VOS("09: Many sequential extents", update_fetch_csum_for_array_9),
	VOS("10: Holes", update_fetch_csum_for_array_10),
};

#define	EVT(desc, test_fn) \
	{ "EVT_CSUM" desc, test_fn, setup, teardown}
static const struct CMUnitTest evt_checksums_tests[] = {
	EVT("01: Some EVT Checksum Helper Functions",
		evt_csum_helper_functions_tests),
	EVT("02: Test the alignment of entries", evt_entry_aligned_tests),
	EVT("03: Test the alignment of entries", test_evt_entry_csum_update),
};

int run_csum_extent_tests(const char *cfg)
{
	int rc = 0;
	char	test_name[DTS_CFG_MAX];

	dts_create_config(test_name,
		"Storage and retrieval of checksums for Array Type %s", cfg);

	rc = cmocka_run_group_tests_name(
		test_name,
		update_fetch_checksums_for_array_types, setup_io, teardown_io);

	dts_create_config(test_name,
		"evtreen helper functions for alignment, counting, etc for csum  %s",
		cfg);

	rc += cmocka_run_group_tests_name(
		test_name,
		evt_checksums_tests, setup_io, teardown_io);

	return rc;
}
