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
#include "csum_extent_tests.h"
#include "vts_io.h"

#define SUCCESS(exp) (0 == (exp))

/*
 * Structure which uniquely identifies a extent in a key value pair.
 */
struct extent_key {
	daos_handle_t	container_hdl;
	daos_unit_oid_t	object_id;
	daos_key_t		dkey;
	daos_key_t		akey;
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
	uint32_t csum_bytes;
	uint32_t total_records;
	uint32_t record_bytes;
	uint32_t csum_chunk_records;
	uint8_t  use_rand_csum;
};

/*
 * test properties for extents that expand an entire range of memory
 */
struct csum_test {
	struct  extent_key extent_key;
	uint32_t csum_bytes;
	uint32_t total_records;
	uint32_t record_bytes;
	uint32_t csum_chunk_records;
	uint32_t csum_buf_len;
	uint32_t buf_len;
	uint8_t *update_csum_buf;
	uint8_t *fetch_csum_buf;
	uint8_t *fetch_buf;
	uint8_t *update_buf;
	uint8_t  use_rand_csum;
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
	uint64_t records_per_extent = get_records_per_extent(test, extent_nr);
	int i;

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
	uint64_t csum_buf_len =
		get_csum_buf_len_per_extent(test, extent_nr);

	int i;

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
	uint32_t *csum_count_total, uint32_t epoch)
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
			       1, &iod, &sgl);

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
	int rc;
	int i;

	/* Setup Test */
	struct csum_test_params params;

	params.total_records = 1024 * 1024 * 64;
	params.record_bytes = 1;
	params.csum_bytes = 8; /* CRC64? */
	params.csum_chunk_records = 1024 * 16; /* 16K */
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
			   &csums_count_total, i + 1);
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
			       1, &iod, &sgl);

	assert_int_equal(0, iod.iod_csums[0].cs_nr);

	iod_free(&iod);
	sgl_free(&sgl);


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

static void
write_to_extent(struct extent_key *extent_key, daos_recx_t *extent,
		     uint8_t *data_buf, const uint64_t buf_len,
		     daos_csum_buf_t *csum)
{
	daos_iod_t	iod;
	d_iov_t		sgl_iov;
	d_sg_list_t	sgl;

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
				    extent_key->object_id, 1, 0,
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
read_from_extent(struct extent_key *extent_key, daos_recx_t *extent,
		 uint8_t *buf, uint64_t buf_len, daos_csum_buf_t *csum)
{
	daos_iod_t	iod;
	d_iov_t		sgl_iov;
	d_sg_list_t	sgl;

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
			extent_key->object_id, 1, &extent_key->dkey,
				   1, &iod, &sgl)))
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

	extent_key_from_test_args(&extent_key, (struct io_test_args *) *state);

	csum.cs_len = 8; /** CRC64 maybe? */
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
	write_to_extent(&extent_key, &extent, data_buf_1, data_size, &csum);

	/** Leave a 64K hole and write the following 64K */
	extent.rx_idx = data_size * 2;
	csum.cs_csum = csum_buf_2;
	write_to_extent(&extent_key, &extent, data_buf_2, data_size, &csum);

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


	read_from_extent(&extent_key, &extent,
			 read_data_buf, data_size * 3, &read_csum);

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

	extent_key_from_test_args(&extent_key, (struct io_test_args *) *state);


	daos_recx_t extent = {1024 * 64, data_size};

	csum.cs_len = 8; /** CRC64 maybe? */
	csum.cs_type = 1;
	csum.cs_chunksize = chunk_size;
	csum.cs_nr = daos_recx_calc_chunks(extent, 1, chunk_size);
	csum.cs_buf_len = csum.cs_len * csum.cs_nr;

	csum_buf_1 = allocate_random(csum.cs_buf_len);
	csum_read_buf = calloc(csum.cs_buf_len, 1);

	/** Write first 64K at offset 64K */
	csum.cs_csum = csum_buf_1;
	write_to_extent(&extent_key, &extent, data_buf_1, data_size, &csum);

	memset(&read_csum, 0, sizeof(read_csum));
	read_csum.cs_len = csum.cs_len;
	read_csum.cs_type = csum.cs_type;
	read_csum.cs_chunksize = chunk_size;
	read_csum.cs_nr = daos_recx_calc_chunks(extent, 1, chunk_size);
	read_csum.cs_buf_len = read_csum.cs_len * read_csum.cs_nr;
	read_csum.cs_csum = csum_read_buf;

	read_from_extent(&extent_key, &extent, read_data_buf, data_size,
			 &read_csum);

	assert_memory_equal(data_buf_1, read_data_buf, data_size);
	assert_memory_equal(csum_buf_1, csum_read_buf, csum.cs_buf_len);

	free(data_buf_1);
	free(read_data_buf);
	free(csum_buf_1);
	free(csum_read_buf);
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
	daos_csum_buf_t		 read_csum;
	uint8_t			*csum_buf_1;
	uint8_t			*csum_read_buf;
	uint8_t			*data_buf_1;
	uint8_t			*read_data_buf;
	daos_recx_t		 extent = {10, data_size};

	extent_key_from_test_args(&extent_key, (struct io_test_args *) *state);

	csum.cs_len = 8; /** CRC64 maybe? */
	csum.cs_type = 1;
	csum.cs_chunksize = chunk_size;
	csum.cs_nr = daos_recx_calc_chunks(extent, 1, chunk_size);
	csum.cs_buf_len = csum.cs_len * csum.cs_nr;

	ALLOC_RAND(csum_buf_1, csum.cs_buf_len);
	D_ALLOC(csum_read_buf, csum.cs_buf_len);

	ALLOC_RAND(data_buf_1, data_size);
	D_ALLOC(read_data_buf, data_size);

	csum.cs_csum = csum_buf_1;
	write_to_extent(&extent_key, &extent, data_buf_1, data_size, &csum);


	memset(&read_csum, 0, sizeof(read_csum));
	read_csum.cs_len = csum.cs_len;
	read_csum.cs_chunksize = chunk_size;
	read_csum.cs_buf_len = csum.cs_buf_len;
	read_csum.cs_csum = csum_read_buf;
	read_csum.cs_type = csum.cs_type;
	read_csum.cs_nr = csum.cs_nr;

	read_from_extent(&extent_key, &extent, read_data_buf, data_size,
			 &read_csum);

	assert_memory_equal(data_buf_1, read_data_buf, data_size);
	assert_memory_equal(csum_buf_1, csum_read_buf, csum.cs_buf_len);

	free(data_buf_1);
	free(read_data_buf);
	free(csum_buf_1);
	free(csum_read_buf);
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

	extent_key_from_test_args(&extent_key, (struct io_test_args *) *state);

	csum.cs_len = 8; /** CRC64 maybe? */
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
	write_to_extent(&extent_key, &extent, data_buf_1, data_size, &csum);

	memset(&read_csum, 0, sizeof(read_csum));
	read_csum.cs_len = 0;
	read_csum.cs_chunksize = chunk_size;
	read_csum.cs_buf_len = csum.cs_buf_len;
	read_csum.cs_csum = csum_read_buf;

	read_from_extent(&extent_key, &extent, read_data_buf, data_size,
			 &read_csum);

	assert_memory_equal(data_buf_1, read_data_buf, data_size);

	/** Verify the csum was not set to what was sent earlier */
	assert_memory_equal(csum_zero_buf, csum_read_buf, csum.cs_buf_len);

	free(data_buf_1);
	free(read_data_buf);
	free(csum_buf_1);
}

/* Inject fault on the update and fetch.
* The update will initialize checksum value to zero.
* Fetch operation will return random values in checksum field.
* Test will compare the update and fetch output for pass/fail
*/
void
csum_fault_injection_multiple_extents_tests(void **state)
{
	int rc;
	int i;
	int update_extents;
	int fetch_extents;
	struct csum_test test;
	/* Setup Test */
	struct csum_test_params params;
	uint32_t csums_count_total, csum_count_per_extent;
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


	params.total_records = 1024 * 1024 * 64;
	params.record_bytes = 1;
	params.csum_bytes = 8; /* CRC64? */
	params.csum_chunk_records = 1024 * 16; /* 16K */
	params.use_rand_csum = true;

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
			   &csums_count_total, i + 1);
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
}
