/**
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#include <daos_types.h>
#include <daos/checksum.h>
#include <evt_priv.h>
#include <vos_io_checksum.h>
#include "vts_io.h"

static void
print_chars(const uint8_t *buf, const size_t len, const uint32_t max)
{
	int i;

	if (buf == NULL)
		return;

	for (i = 0; i <  len && (i < max || max == 0); i++)
		print_error("%c", buf[i]);
}


#define SUCCESS(exp) (0 == (exp))

#define ASSERT_SUCCESS(exp) assert_int_equal(0, (exp))

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

/*
 * Parameters used to setup the test
 */
struct csum_test_params {
	uint32_t	csum_bytes;
	uint32_t	total_records;
	uint32_t	record_bytes;
	uint32_t	csum_chunk_records;
	uint8_t		use_rand_csum;
};

/*
 * test properties for extents that expand an entire range of memory
 */
struct csum_test {
	struct  extent_key	 extent_key;
	uint32_t		 csum_bytes;
	uint32_t		 total_records;
	uint32_t		 record_bytes;
	uint32_t		 csum_chunk_records;
	uint32_t		 csum_buf_len;
	uint32_t		 buf_len;
	uint8_t			*update_csum_buf;
	uint8_t			*fetch_csum_buf;
	uint8_t			*fetch_buf;
	uint8_t			*update_buf;
	uint8_t			 use_rand_csum;
};

/*
 * Calculated fields based on number of extents to segment the data
 */
static uint64_t
get_records_per_extent(struct csum_test *test, const uint64_t extents)
{
	return test->total_records / extents;
}

static uint64_t
get_csums_per_extent(struct csum_test *test, const uint64_t extents)
{
	return get_records_per_extent(test, extents) / test->csum_chunk_records;
}

static uint64_t
get_csum_buf_len_per_extent(struct csum_test *test, const uint64_t extents)
{
	return get_csums_per_extent(test, extents) * test->csum_bytes;
}

static uint64_t
get_csum_total(struct csum_test *test, const uint64_t extents)
{
	return test->total_records / test->csum_chunk_records;
}

/*
 * Setup the tests
 */
static void
csum_test_setup(struct csum_test *test, void **state,
		struct csum_test_params *params)
{
	test->total_records = params->total_records;
	test->record_bytes = params->record_bytes;
	test->csum_bytes = params->csum_bytes;
	test->csum_chunk_records = params->csum_chunk_records;
	test->use_rand_csum = params->use_rand_csum;

	/* Calculated fields */
	test->buf_len = test->total_records * test->record_bytes;
	test->csum_buf_len = (test->total_records /
			      test->csum_chunk_records) *
			     test->csum_bytes;

	/* Allocate memory for the csums and buffers used for data */
	test->fetch_buf = malloc(test->buf_len);
	test->update_buf = malloc(test->buf_len);
	test->update_csum_buf = malloc(test->csum_buf_len);
	test->fetch_csum_buf = malloc(test->csum_buf_len);

	/* Generate some random data */
	dts_buf_render((char *) test->update_buf, test->buf_len);

	extent_key_from_test_args(&test->extent_key,
				  (struct io_test_args *) *state);
}

/*
 * Cleanup from test
 */
static void
csum_test_teardown(struct csum_test *test_test)
{
	free(test_test->update_csum_buf);
	free(test_test->fetch_csum_buf);
	free(test_test->fetch_buf);
	free(test_test->update_buf);
}

/*
 * Initialize an I/O Descriptor based on current test and number of extents
 */
static void
iod_init(daos_iod_t *iod, const uint32_t extent_nr,
	 struct csum_test *test)
{
	memset(iod, 0, sizeof(*iod));
	iod->iod_type = DAOS_IOD_ARRAY;
	iod->iod_size = 1;
	iod->iod_name = test->extent_key.akey;
	iod->iod_recxs = calloc(extent_nr, sizeof(daos_recx_t));
	iod->iod_csums = calloc(extent_nr, sizeof(daos_csum_buf_t));
	iod->iod_nr = extent_nr;
}

/*
 * Free resources allocated for the I/O Descriptor
 */
static void
iod_free(daos_iod_t *iod)
{
	free(iod->iod_recxs);
	free(iod->iod_csums);
}

/*
 * Initialize the record extents based on current test and number of extents
 */
static void
iod_recx_init(daos_iod_t *iod, const uint32_t extent_nr,
	      struct csum_test *test)
{
	uint64_t	records_per_extent;
	int		i;

	records_per_extent = get_records_per_extent(test, extent_nr);

	for (i = 0; i < extent_nr; i++) {
		daos_recx_t *recx = &iod->iod_recxs[i];

		recx->rx_nr = records_per_extent;
		recx->rx_idx = i * records_per_extent;
	}
}

/*
 * Setup the Scatter/Gather List and allocate resources required
 */
static void
sgl_init(d_sg_list_t *sgl, uint8_t *buf, uint32_t buf_len)
{
	memset(sgl, 0, sizeof((*sgl)));
	sgl->sg_nr = 1;
	sgl->sg_iovs = calloc(sgl->sg_nr, sizeof(d_iov_t));

	d_iov_set(&sgl->sg_iovs[0], buf, buf_len);
}

/*
 * Free resources allocated for the Scatter/Gather List
 */
static void
sgl_free(d_sg_list_t *sgl)
{
	free(sgl->sg_iovs);
}

/*
 * Setup the csum buffer structure and create dummy csums.
 */
static void
iod_csum_calculate(struct csum_test *test, uint32_t extent_nr, daos_iod_t *iod)
{
	uint64_t	csum_buf_len;
	int		i;

	csum_buf_len = get_csum_buf_len_per_extent(test, extent_nr);

	for (i = 0; i < extent_nr; i++) {
		daos_csum_buf_t *csum = &iod->iod_csums[i];
		/*
		 * "Calculating" csums for extents (ignoring "chunks" for now)
		 */
		uint8_t *buf = &test->update_csum_buf[i * csum_buf_len];
		char *extent_csum_buf = (char *) buf;

		if (test->use_rand_csum)
			dts_buf_render(extent_csum_buf, csum_buf_len);
		else
			memset(extent_csum_buf, i + 1, csum_buf_len);

		dcb_set(csum, buf, csum_buf_len,
			test->csum_bytes,
			get_csums_per_extent(test, extent_nr),
			test->csum_chunk_records *
			test->record_bytes);
	}
}

/*
 * Send the test's update buffer and csum to VOS to update the object.
 */
static int
update(struct csum_test *test, const uint32_t extent_nr,
	uint32_t epoch)
{
	d_sg_list_t		sgl;
	daos_iod_t		iod;

	iod_init(&iod, extent_nr, test);
	iod_recx_init(&iod, extent_nr, test);
	sgl_init(&sgl, test->update_buf, test->buf_len);

	iod_csum_calculate(test, extent_nr, &iod);

	int rc = vos_obj_update(test->extent_key.container_hdl,
				test->extent_key.object_id, epoch,
				0, &test->extent_key.dkey,
				1, &iod, &sgl);
	iod_free(&iod);
	sgl_free(&sgl);
	return rc;
}

/*
 * Fetch the extent and csum into the test's fetch buffers. Count the total
 * number of csums that were fetched and return to be verified.
 */
static int
fetch(struct csum_test *test, int extent_nr, uint32_t *csum_count_per_extent,
	uint32_t *csum_count_total, uint32_t epoch,
	struct daos_csummer *csummer)
{
	d_sg_list_t		sgl;
	daos_iod_t		iod;
	int			i;

	iod_init(&iod, extent_nr, test);
	iod_recx_init(&iod, extent_nr, test);
	sgl_init(&sgl, test->update_buf, test->buf_len);

	daos_csum_buf_t *csums = iod.iod_csums;

	uint64_t len_per_extent = get_csum_buf_len_per_extent(test, extent_nr);

	for (i = 0; i < extent_nr; i++) {
		csums[i].cs_buf_len = len_per_extent;
		csums[i].cs_csum = &test->fetch_csum_buf[i * len_per_extent];
		csums[i].cs_chunksize = test->csum_chunk_records *
			test->record_bytes;
		csums[i].cs_len = test->csum_bytes;
		csums[i].cs_nr = get_csums_per_extent(test, extent_nr);
	}

	int rc = vos_obj_fetch(test->extent_key.container_hdl,
			       test->extent_key.object_id,
			       epoch, &test->extent_key.dkey,
			       1, csummer, &iod, &sgl);

	*csum_count_total = 0;
	*csum_count_per_extent = 0;
	for (i = 0; i < extent_nr; i++) {
		/*
		 * Count total checksums fetched
		 */
		*csum_count_total += csums[i].cs_nr;

		/*
		 * Each extent's csum values should be the same ... so just save
		 * the first
		 */
		if (i == 0)
			*csum_count_per_extent = csums[i].cs_nr;
		else
			assert_int_equal(*csum_count_per_extent,
					 csums[i].cs_nr);
	}

	iod_free(&iod);
	sgl_free(&sgl);

	return rc;
}

/*
 * ------------------------------------------------------------------------
 * Actual Test Functions
 * - These are called from vts_io and included in csum_extent_tests.h
 * ------------------------------------------------------------------------
 */

/*
 * Using a single data source, this test will repeatedly update and fetch
 * a key value (vos_object_update/vos_object_fetch) with different number
 * of extents spanning the data source. It does expect that the updates and
 * fetched extents are "chunk" aligned.
 *
 */
void
csum_multiple_extents_tests(void **state)
{
	int			 rc;
	int			 i;
	struct daos_csummer	*csummer;

	daos_csummer_init(&csummer, &fake_algo,  1024 * 16 /* 16K */);

	/* Setup Test */
	struct csum_test_params params;

	params.total_records = 1024 * 1024 * 64;
	params.record_bytes = 1;
	params.csum_bytes =  daos_csummer_get_csum_len(csummer);
	params.csum_chunk_records = daos_csummer_get_chunksize(csummer);
	params.use_rand_csum = true;
	struct csum_test test;

	csum_test_setup(&test, state, &params);

	/* extent counts for update and fetch. The extents will span the same
	 * amount of data, the extents themselves will be of different lengths
	 */
	const int END = 0;
	const int table[][2] = {
	/*	update, fetch  */
		{1,	1},
		{1,	4},
		{4,	4},
		{4,	1},
		{END,	END}
	};

	for (i = 0; table[i][0] != END; i++) {
		int update_extents = table[i][0];
		int fetch_extents = table[i][1];

		printf("Update Extents: %d, Fetch Extents: %d\n",
		       update_extents, fetch_extents);

		uint32_t csums_count_total, csum_count_per_extent;

		rc = update(&test, update_extents, i + 1);
		if (!SUCCESS(rc))
			fail_msg("Error updating extent with csum: %s\n",
				 d_errstr(rc));

		rc = fetch(&test, fetch_extents, &csum_count_per_extent,
			   &csums_count_total, i + 1, csummer);
		if (!SUCCESS(rc))
			fail_msg("Error fetching extent with csum: %s\n",
				 d_errstr(rc));

		/* Verify */
		assert_int_equal(get_csums_per_extent(&test, fetch_extents),
				 csum_count_per_extent);
		assert_int_equal(get_csum_total(&test, fetch_extents),
				 csums_count_total);
		assert_memory_equal(test.update_csum_buf, test.fetch_csum_buf,
				    test.csum_buf_len);
	}
	csum_test_teardown(&test);
	daos_csummer_destroy(&csummer);
}

/*
 * This test verifies that csums aren't copied into a zero length buffer.
 */
void csum_test_csum_buffer_of_0_during_fetch(void **state)
{
	struct csum_test_params params;
	struct csum_test test;

	params.total_records = 1024 * 1024 * 64;
	params.record_bytes = 1;
	params.csum_bytes = 64;
	params.csum_chunk_records = 1024 * 16;
	params.use_rand_csum = true;


	csum_test_setup(&test, state, &params);

	daos_epoch_t epoch = 1;

	update(&test, 1, epoch);

	/* Fetch ... */
	d_sg_list_t	 sgl;
	daos_iod_t	 iod;
	uint32_t	 extent_nr = 1;

	iod_init(&iod, extent_nr, &test);
	iod_recx_init(&iod, extent_nr, &test);
	sgl_init(&sgl, test.update_buf, test.buf_len);

	iod.iod_csums[0].cs_buf_len = 0;
	iod.iod_csums[0].cs_csum = NULL;
	vos_obj_fetch(test.extent_key.container_hdl,
		      test.extent_key.object_id,
		      epoch, &test.extent_key.dkey,
		      1, NULL, &iod, &sgl);

	assert_int_equal(0, iod.iod_csums[0].cs_nr);

	iod_free(&iod);
	sgl_free(&sgl);

	csum_test_teardown(&test);
}

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
write_to_extent(struct extent_key *extent_key, daos_epoch_t epoch,
		daos_recx_t *extent, uint8_t *data_buf,
		const uint64_t buf_len, daos_csum_buf_t *csum)
{
	daos_iod_t	iod;
	d_iov_t		sgl_iov;
	d_sg_list_t	sgl;

	if (epoch == 0)
		epoch = 1;

	memset(&iod, 0, sizeof(iod));
	iod.iod_name = extent_key->akey;
	iod.iod_nr = 1;
	iod.iod_csums = csum;
	iod.iod_recxs = extent;

	iod.iod_size = 1;
	iod.iod_type = DAOS_IOD_ARRAY;

	sgl_iov.iov_len = sgl_iov.iov_buf_len = buf_len;
	sgl_iov.iov_buf = data_buf;

	memset(&sgl, 0, sizeof(sgl));
	sgl.sg_nr = 1;
	sgl.sg_iovs = &sgl_iov;

	if (!SUCCESS(vos_obj_update(extent_key->container_hdl,
				    extent_key->object_id, epoch, 0,
				    &extent_key->dkey, 1, &iod, &sgl)))
		fail_msg("Failed to update");
}

static uint8_t *
allocate_random(size_t len)
{
	uint8_t *result;

	D_ALLOC(result, len);
	if (result)
		dts_buf_render((char *) result, (unsigned int) len);
	return result;
}

#define	ALLOC_RAND(ptr, len) \
	do { \
		(ptr) = allocate_random(len); \
		assert_non_null(ptr); \
	} while (0)

static void
read_from_extent(struct extent_key *extent_key, daos_epoch_t epoch,
		 daos_recx_t *extent, uint8_t *buf,
		 uint64_t buf_len, daos_csum_buf_t *csum,
		 struct daos_csummer *csummer)
{
	daos_iod_t	iod;
	d_iov_t		sgl_iov;
	d_sg_list_t	sgl;

	if (epoch == 0)
		epoch = 1;

	memset(&iod, 0, sizeof(iod));

	iod.iod_name = extent_key->akey;
	iod.iod_nr = 1;
	iod.iod_csums = csum;
	iod.iod_recxs = extent;

	iod.iod_size = 1;
	iod.iod_type = DAOS_IOD_ARRAY;


	sgl_iov.iov_len = sgl_iov.iov_buf_len = buf_len;
	sgl_iov.iov_buf = buf;

	memset(&sgl, 0, sizeof(sgl));
	sgl.sg_nr = 1;
	sgl.sg_iovs = &sgl_iov;

	if (!SUCCESS(vos_obj_fetch(extent_key->container_hdl,
				   extent_key->object_id, epoch,
				   &extent_key->dkey,
				   1, csummer, &iod, &sgl)))
		fail_msg("Failed to fetch");
}

/*
 * This test verifies that csums aren't copied for holes
 */
void
csum_test_holes(void **state)
{
	const uint64_t		 data_size = 1024 * 64; /** 64K */
	const uint64_t		 chunk_size = 1024 * 16; /** 16K */
	struct extent_key	 extent_key;
	daos_recx_t		 extent = {0, data_size};
	daos_csum_buf_t		 csum;
	daos_csum_buf_t		 read_csum;
	uint8_t			*data_buf_1;
	uint8_t			*data_buf_2;
	uint8_t			*read_data_buf = calloc(data_size * 3, 1);
	uint8_t			*csum_buf_1;
	uint8_t			*csum_buf_2;
	uint8_t			*csum_read_buf;
	struct daos_csummer	*csummer;

	daos_csummer_init(&csummer, &fake_algo, chunk_size);

	extent_key_from_test_args(&extent_key, (struct io_test_args *) *state);

	csum.cs_len = daos_csummer_get_csum_len(csummer);
	csum.cs_type = 1;
	csum.cs_chunksize = chunk_size;
	csum.cs_nr = daos_recx_calc_chunks(extent, 1, chunk_size);
	csum.cs_buf_len = csum.cs_len * csum.cs_nr;

	csum_buf_1 = allocate_random(csum.cs_buf_len);
	data_buf_1 = allocate_random(data_size);
	sleep(1); /** Sleep so random seed is different */
	data_buf_2 = allocate_random(data_size);
	csum_buf_2 = allocate_random(csum.cs_buf_len);

	assert_memory_not_equal(data_buf_1, data_buf_2, data_size);
	assert_memory_not_equal(csum_buf_1, csum_buf_2, csum.cs_buf_len);
	csum_read_buf = calloc(csum.cs_buf_len * 3, 1);

	/** Write first 64K */
	csum.cs_csum = csum_buf_1;
	write_to_extent(&extent_key, 1, &extent, data_buf_1, data_size, &csum);

	/** Leave a 64K hole and write the following 64K */
	extent.rx_idx = data_size * 2;
	csum.cs_csum = csum_buf_2;
	write_to_extent(&extent_key, 1, &extent, data_buf_2, data_size, &csum);

	/** Read from first to last written */
	extent.rx_idx = 0;
	extent.rx_nr *= 3;

	memset(&read_csum, 0, sizeof(read_csum));
	read_csum.cs_len = csum.cs_len;
	read_csum.cs_chunksize = chunk_size;
	read_csum.cs_nr = daos_recx_calc_chunks(extent, 1, chunk_size);
	read_csum.cs_buf_len = read_csum.cs_len * read_csum.cs_nr;
	read_csum.cs_csum = csum_read_buf;
	read_csum.cs_type = 1;


	read_from_extent(&extent_key, 0, &extent,
			 read_data_buf, data_size * 3, &read_csum, csummer);

	assert_memory_equal(data_buf_1, read_data_buf, data_size);
	assert_memory_equal(data_buf_2, read_data_buf + data_size * 2,
			    data_size);

	assert_memory_equal(csum_buf_1, csum_read_buf, csum.cs_buf_len);
	assert_memory_equal(csum_buf_2, csum_read_buf + csum.cs_buf_len * 2,
			    csum.cs_buf_len);

	free(data_buf_1);
	free(data_buf_2);
	free(read_data_buf);
	free(csum_buf_1);
	free(csum_buf_2);
	free(csum_read_buf);
	daos_csummer_destroy(&csummer);
}

/*
 * Verifies can handle not starting at 0
 */
void
csum_extent_not_starting_at_0(void **state)
{
	const uint64_t		 data_size = 1024 * 64; /** 64K */
	const uint64_t		 chunk_size = 1024 * 16; /** 16K */
	struct extent_key	 extent_key;
	daos_csum_buf_t		 csum;
	daos_csum_buf_t		 read_csum;
	uint8_t			*data_buf_1 = allocate_random(data_size);
	uint8_t			*read_data_buf = calloc(data_size, 1);
	uint8_t			*csum_buf_1;
	uint8_t			*csum_read_buf;
	struct daos_csummer	*csummer;

	daos_csummer_init(&csummer, &fake_algo, chunk_size);

	extent_key_from_test_args(&extent_key, (struct io_test_args *) *state);


	daos_recx_t extent = {1024 * 64, data_size};

	csum.cs_len = daos_csummer_get_csum_len(csummer);
	csum.cs_type = 1;
	csum.cs_chunksize = chunk_size;
	csum.cs_nr = daos_recx_calc_chunks(extent, 1, chunk_size);
	csum.cs_buf_len = csum.cs_len * csum.cs_nr;

	csum_buf_1 = allocate_random(csum.cs_buf_len);
	csum_read_buf = calloc(csum.cs_buf_len, 1);

	/** Write first 64K at offset 64K */
	csum.cs_csum = csum_buf_1;
	write_to_extent(&extent_key, 1, &extent, data_buf_1, data_size, &csum);

	memset(&read_csum, 0, sizeof(read_csum));
	read_csum.cs_len = csum.cs_len;
	read_csum.cs_type = csum.cs_type;
	read_csum.cs_chunksize = chunk_size;
	read_csum.cs_nr = daos_recx_calc_chunks(extent, 1, chunk_size);
	read_csum.cs_buf_len = read_csum.cs_len * read_csum.cs_nr;
	read_csum.cs_csum = csum_read_buf;

	read_from_extent(&extent_key, 0, &extent, read_data_buf, data_size,
			 &read_csum, csummer);

	assert_memory_equal(data_buf_1, read_data_buf, data_size);
	assert_memory_equal(csum_buf_1, csum_read_buf, csum.cs_buf_len);

	free(data_buf_1);
	free(read_data_buf);
	free(csum_buf_1);
	free(csum_read_buf);
	daos_csummer_destroy(&csummer);
}

/*
 * Verifies can handle non-chunk aligned extents
 */
void
csum_extent_not_chunk_aligned(void **state)
{
	const uint64_t		data_size = 20;
	const uint64_t		chunk_size = 8;

	struct extent_key	 extent_key;
	daos_csum_buf_t		 csum;
	daos_csum_buf_t		 read_csum = {0};
	uint8_t			*csum_buf_1;
	uint8_t			*csum_read_buf;
	uint8_t			*data_buf_1;
	uint8_t			*read_data_buf;
	daos_recx_t		 extent = {10, data_size};
	struct daos_csummer	*csummer;

	daos_csummer_init(&csummer, &fake_algo, chunk_size);

	extent_key_from_test_args(&extent_key, (struct io_test_args *) *state);

	csum.cs_len = daos_csummer_get_csum_len(csummer);
	csum.cs_type = 1;
	csum.cs_chunksize = chunk_size;
	csum.cs_nr = daos_recx_calc_chunks(extent, 1, chunk_size);
	csum.cs_buf_len = csum.cs_len * csum.cs_nr;

	ALLOC_RAND(csum_buf_1, csum.cs_buf_len);
	D_ALLOC(csum_read_buf, csum.cs_buf_len);

	ALLOC_RAND(data_buf_1, data_size);
	D_ALLOC(read_data_buf, data_size);

	csum.cs_csum = csum_buf_1;
	write_to_extent(&extent_key, 1, &extent, data_buf_1, data_size, &csum);

	read_csum.cs_len = csum.cs_len;
	read_csum.cs_chunksize = chunk_size;
	read_csum.cs_buf_len = csum.cs_buf_len;
	read_csum.cs_csum = csum_read_buf;
	read_csum.cs_type = csum.cs_type;
	read_csum.cs_nr = csum.cs_nr;

	read_from_extent(&extent_key, 0, &extent, read_data_buf, data_size,
			 &read_csum, csummer);

	assert_memory_equal(data_buf_1, read_data_buf, data_size);
	assert_memory_equal(csum_buf_1, csum_read_buf, csum.cs_buf_len);

	free(data_buf_1);
	free(read_data_buf);
	free(csum_buf_1);
	free(csum_read_buf);
	daos_csummer_destroy(&csummer);
}

void csum_invalid_input_tests(void **state)
{
	const uint64_t		data_size = 20;
	const uint64_t		chunk_size = 8;

	struct extent_key	 extent_key;
	daos_csum_buf_t		 csum;
	daos_csum_buf_t		 read_csum;
	uint8_t			*csum_buf_1;
	uint8_t			*csum_zero_buf;
	uint8_t			*csum_read_buf;
	uint8_t			*data_buf_1;
	uint8_t			*read_data_buf;
	daos_recx_t		 extent = {10, data_size};
	struct daos_csummer	*csummer;

	daos_csummer_init(&csummer, &fake_algo, chunk_size);

	extent_key_from_test_args(&extent_key, (struct io_test_args *) *state);

	csum.cs_len = daos_csummer_get_csum_len(csummer);
	csum.cs_type = 1;
	csum.cs_chunksize = 0; /** Invalid */
	csum.cs_nr = daos_recx_calc_chunks(extent, 1, chunk_size);
	csum.cs_buf_len = csum.cs_len * csum.cs_nr;

	ALLOC_RAND(data_buf_1, data_size);
	D_ALLOC(read_data_buf, data_size);

	ALLOC_RAND(csum_buf_1, csum.cs_buf_len);
	D_ALLOC(csum_zero_buf, csum.cs_buf_len);
	D_ALLOC(csum_read_buf, csum.cs_buf_len);

	csum.cs_csum = csum_buf_1;
	write_to_extent(&extent_key, 1, &extent, data_buf_1, data_size, &csum);

	memset(&read_csum, 0, sizeof(read_csum));
	read_csum.cs_len = 0; /** invalid */
	read_csum.cs_chunksize = chunk_size;
	read_csum.cs_buf_len = csum.cs_buf_len;
	read_csum.cs_csum = csum_read_buf;

	read_from_extent(&extent_key, 0, &extent, read_data_buf, data_size,
			 &read_csum, csummer);

	assert_memory_equal(data_buf_1, read_data_buf, data_size);

	/** Verify the csum was not set to what was sent earlier */
	assert_memory_equal(csum_zero_buf, csum_read_buf, csum.cs_buf_len);

	free(data_buf_1);
	free(read_data_buf);
	free(csum_buf_1);
	daos_csummer_destroy(&csummer);
}

/* Inject fault on the update and fetch.
* The update will initialize checksum value to zero.
* Fetch operation will return random values in checksum field.
* Test will compare the update and fetch output for pass/fail
*/
void
csum_fault_injection_multiple_extents_tests(void **state)
{
	int			 rc;
	int			 i;
	int			 update_extents;
	int			 fetch_extents;
	struct csum_test	 test;
	/* Setup Test */
	struct csum_test_params	 params;
	uint32_t		 csums_count_total, csum_count_per_extent;
	struct daos_csummer	*csummer;

	daos_csummer_init(&csummer, &fake_algo,  1024 * 16 /* 16K */);
	/* extent counts for update and fetch. The extents will span the same
	 * amount of data, the extents themselves will be of different lengths
	 */
	const int END = 0;
	const int table[][2] = {
	/*	update, fetch  */
		{1,	1},
		{1,	4},
		{2,	4},
		{4,	4},
		{4,	1},
		{4,	2},
		{END,	END}
	};

	/* Setup Test */
	params.total_records = 1024 * 1024 * 64;
	params.record_bytes = 1;
	params.use_rand_csum = true;

	params.total_records = 1024 * 1024 * 64;
	params.record_bytes = 1;
	params.csum_bytes =  daos_csummer_get_csum_len(csummer);
	params.csum_chunk_records = daos_csummer_get_chunksize(csummer);
	params.use_rand_csum = true;
	daos_csummer_init(&csummer, &fake_algo, params.csum_chunk_records);

	csum_test_setup(&test, state, &params);

	for (i = 0; table[i][0] != END; i++) {
		update_extents = table[i][0];
		fetch_extents = table[i][1];

		printf("Update Extents: %d, Fetch Extents: %d\n",
		       update_extents, fetch_extents);

		daos_fail_loc_set(DAOS_CHECKSUM_UPDATE_FAIL | DAOS_FAIL_ALWAYS);
		rc = update(&test, update_extents, i + 1);
		if (!SUCCESS(rc))
			fail_msg("Error updating extent with csum: %s\n",
				 d_errstr(rc));

		daos_fail_loc_set(DAOS_CHECKSUM_FETCH_FAIL | DAOS_FAIL_ALWAYS);
		rc = fetch(&test, fetch_extents, &csum_count_per_extent,
			   &csums_count_total, i + 1, csummer);
		if (!SUCCESS(rc))
			fail_msg("Error fetching extent with csum: %s\n",
				 d_errstr(rc));

		/* Verify */
		assert_int_equal(get_csums_per_extent(&test, fetch_extents),
				 csum_count_per_extent);
		assert_int_equal(get_csum_total(&test, fetch_extents),
				csums_count_total);
		/* It looks is there is random issues in setting
		* the fault injection flag. If last fault injection flag is not
		* set the data will be same.
		*/
		if (DAOS_FAIL_CHECK(DAOS_CHECKSUM_FETCH_FAIL))
			assert_memory_not_equal(test.update_csum_buf,
						test.fetch_csum_buf,
						test.csum_buf_len);
		else
			assert_memory_equal(test.update_csum_buf,
					test.fetch_csum_buf,
					test.csum_buf_len);
	}
	daos_fail_loc_set(0);
	csum_test_teardown(&test);
	daos_csummer_destroy(&csummer);
	daos_csummer_destroy(&csummer);
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

	result = vic_needs_new_csum(&raw, &req, &chunk, started, has_next);
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
	(struct test_setup)__VA_ARGS__)

static void
test_case_create(struct vos_fetch_test_context *ctx, struct test_setup setup)
{
	uint32_t	 csum_len;
	uint64_t	 rec_size;
	uint32_t	 cs;
	size_t		 i = 0;
	size_t		 j;
	size_t		 nr;
	uint8_t		*dummy_csums;

	daos_csummer_init(&ctx->csummer, &fake_algo, setup.chunksize);

	csum_len = daos_csummer_get_csum_len(ctx->csummer);
	cs = daos_csummer_get_chunksize(ctx->csummer);
	dummy_csums = (uint8_t *) "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	rec_size = setup.rec_size;

	/** count number of layouts */
	while (setup.layout[i].data != NULL)
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

		l = &setup.layout[i];
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
	ctx->iod.iod_recxs->rx_idx = setup.request_idx;
	ctx->iod.iod_recxs->rx_nr = setup.request_len;

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
	return vic_fetch_iod(
		&ctx->iod, ctx->csummer, &ctx->bsgl,
		ctx->biov_dcbs, NULL);
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
	char				large_data01[1024 * 16];
	char				large_data02[1024 * 16] = "";

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

static const struct CMUnitTest tests[] = {
	{ "VOS_CSUM01: Extent checksums with multiple extents requested",
		csum_multiple_extents_tests, setup, teardown},
	{ "VOS_CSUM02: Extent checksums with zero len csum buffer",
		csum_test_csum_buffer_of_0_during_fetch, setup, teardown},
	{ "VOS_CSUM03: Extent checksums with holes",
		csum_test_holes, setup, teardown},
	{ "VOS_CSUM04: Test checksums when extent index doesn't start at 0",
		csum_extent_not_starting_at_0, setup, teardown},
	{ "VOS_CSUM05: Test checksums with chunk-unaligned extents",
		csum_extent_not_chunk_aligned, setup, teardown},
	{ "VOS_CSUM06: Some EVT Checksum Helper Functions",
		evt_csum_helper_functions_tests, setup, teardown},
	{ "VOS_CSUM07: Some input validation",
		csum_invalid_input_tests, setup, teardown},
	{ "VOS_CSUM08: Checksum fault injection test : Multiple extents",
		csum_fault_injection_multiple_extents_tests, setup, teardown},

	{ "VOS_CSUM_ENT01: Test the alignment of entries",
		evt_entry_aligned_tests, setup, teardown},

	{ "VOS_CSUM_FETCH01: Partial Extents, but chunks align",
		with_aligned_chunks_csums_are_copied, setup, teardown},
	{ "VOS_CSUM_FETCH02: Partial Extents, chunks don't align",
		with_unaligned_chunks_csums_new_csum_is_created,
		setup, teardown},
	{ "VOS_CSUM_FETCH03: Partial Extents, first extent isn't aligned",
		with_unaligned_first_chunk, setup, teardown},
	{ "VOS_CSUM_FETCH04: Partial Extents, extent smaller than chunk",
		with_extent_smaller_than_chunk, setup, teardown},
	{ "VOS_CSUM_FETCH05: Extent is larger than chunk",
		with_extent_larger_than_request, setup, teardown},
	{ "VOS_CSUM_FETCH06: Fetch smaller than chunk",
		with_fetch_smaller_than_chunk, setup, teardown},
	{ "VOS_CSUM_FETCH07: Partial extent/unaligned extent",
		more_partial_extent_tests, setup, teardown},
	{ "VOS_CSUM_FETCH08: Fetch with larger records",
		test_larger_records, setup, teardown},
	{ "VOS_CSUM_FETCH09: Fetch with larger records",
		test_larger_records2, setup, teardown},

	{ "VOS_CSUM_100: Determine if need new checksum",
		need_new_checksum_tests, setup, teardown},
};


int run_csum_extent_tests(void)
{
	return cmocka_run_group_tests_name("VOS Checksum tests for extents ",
		tests, setup_io, teardown_io);
}

