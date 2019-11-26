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
#define D_LOGFAC        DD_FAC(tests)


#include <string.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h> /** For cmocka.h */
#include <stdint.h>
#include <cmocka.h>
#include <gurt/types.h>
#include <daos/common.h>
#include <daos/checksum.h>
#include <daos/tests_lib.h>

static bool verbose;

/**
 * -----------------------------------------------------------------------------
 * Setup some fake functions and veriables to track how the functions are
 * called. Will be used for checksummer testing.
 * -----------------------------------------------------------------------------
 */
#define FAKE_CSUM_TYPE 999
static int fake_init_called;
static int
fake_init(struct daos_csummer *obj)
{
	fake_init_called++;
	return 0;
}

static int fake_fini_called;
static void
fake_fini(struct daos_csummer *obj)
{
	fake_fini_called++;
}

#define FAKE_UPDATE_BUF_LEN 512
static char fake_update_buf_copy[FAKE_UPDATE_BUF_LEN];
static char *fake_update_buf = fake_update_buf_copy;
static int fake_update_bytes_seen;
static int
fake_update(struct daos_csummer *obj, uint8_t *buf, size_t buf_len)
{
	obj->dcs_csum_buf[0]++; /** Just increment the first byte */
	fake_update_bytes_seen += buf_len;
	strncpy(fake_update_buf, (char *)buf, buf_len);
	fake_update_buf += buf_len;
	return 0;
}

static uint16_t fake_get_size_result;
static uint16_t
fake_get_size(struct daos_csummer *obj)
{
	return fake_get_size_result;
}

void
fake_reset(struct daos_csummer *obj)
{
	obj->dcs_csum_buf[0] = 0;
}

static struct csum_ft fake_algo = {
		.cf_init = fake_init,
		.cf_destroy = fake_fini,
		.cf_update = fake_update,
		.cf_reset = fake_reset,
		.cf_csum_len = 0,
		.cf_get_size = NULL,
		.cf_type = FAKE_CSUM_TYPE,
		.cf_name = "fake"
	};

/**
 * -----------------------------------------------------------------------------
 * Test the CSUMMER initialize, destroy, and some other basic functions
 * -----------------------------------------------------------------------------
 */
static void
test_init_and_destroy(void **state)
{
	fake_init_called = 0;

	struct daos_csummer *csummer;
	int rc = daos_csummer_init(&csummer, &fake_algo, 0);

	assert_int_equal(0, rc);
	assert_int_equal(1, fake_init_called);
	assert_int_equal(FAKE_CSUM_TYPE, daos_csummer_get_type(csummer));

	/** get size should use static size or get size function if set*/
	fake_algo.cf_csum_len = 4;
	assert_int_equal(4, daos_csummer_get_csum_len(csummer));
	fake_algo.cf_csum_len = 0;
	fake_algo.cf_get_size = fake_get_size;
	fake_get_size_result = 5;
	assert_int_equal(5, daos_csummer_get_csum_len(csummer));
	assert_true(daos_csummer_initialized(csummer));
	assert_string_equal("fake", daos_csummer_get_name(csummer));

	daos_csummer_destroy(&csummer);
	assert_int_equal(1, fake_fini_called);
	assert_null(csummer);
}

static void
test_update_reset(void **state)
{
	struct daos_csummer *csummer;
	uint32_t csum = 0;

	fake_get_size_result = sizeof(csum);
	daos_csummer_init(&csummer, &fake_algo, 0);
	daos_csummer_set_buffer(csummer, (uint8_t *) &csum, sizeof(csum));

	size_t len = 32;
	uint8_t buf[len];

	memset(buf, 0, len);

	/** before an update, the csum should be 0 */
	assert_int_equal(0, csum);

	/** The fake csummer simply increments the csum each time */
	daos_csummer_update(csummer, buf, len);
	assert_int_equal(1, csum);


	daos_csummer_update(csummer, buf, len);
	assert_int_equal(2, csum);


	/** reset */
	daos_csummer_reset(csummer);
	assert_int_equal(0, csum);

	daos_csummer_destroy(&csummer);
}

static void
test_update_with_multiple_buffers(void **state)
{
	uint32_t		 csum = 0; /** buffer */
	uint32_t		 csum2 = 0; /** buffer */
	size_t			 len = 64;
	uint8_t			 buf[len];
	struct daos_csummer	*csummer;

	fake_get_size_result = sizeof(uint32_t); /** setup fake checksum */
	daos_csummer_init(&csummer, &fake_algo, 0);

	memset(buf, 0xa, len);

	daos_csummer_set_buffer(csummer, (uint8_t *) &csum, sizeof(uint32_t));
	daos_csummer_update(csummer, buf, len);
	assert_int_equal(1, csum);

	daos_csummer_set_buffer(csummer, (uint8_t *) &csum2, sizeof(uint32_t));
	daos_csummer_update(csummer, buf, len);
	assert_int_equal(1, csum2);

	daos_csummer_destroy(&csummer);
}

/**
 * -----------------------------------------------------------------------------
 * A series of tests for allocating and calculating csums
 * -----------------------------------------------------------------------------
 */

static void
test_checksummer_allocates_csum_buf(void **state)
{
	struct daos_csummer	*csummer;
	daos_csum_buf_t		*actual;
	daos_recx_t		 recx;
	daos_iod_t		 iod = {0};
	uint32_t		 csums_nr;

	daos_csummer_init(&csummer, &fake_algo, 16);
	recx.rx_idx = 0;
	recx.rx_nr = 32;

	fake_get_size_result = 2;
	iod.iod_size = 1;
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;

	daos_csummer_alloc_dcbs(csummer, &iod, 1, &actual, &csums_nr);

	assert_non_null(actual);
	assert_non_null(actual->cs_csum);
	assert_int_equal(4, actual->cs_buf_len); /** 2 csums * 2 bytes */
	assert_int_equal(1, csums_nr); /** 1 IOD with 1 extent */

	daos_csummer_free_dcbs(csummer, &actual);
	assert_null(actual);

	daos_csummer_destroy(&csummer);
}

static void
test_daos_checksummer_with_single_iov_single_chunk(void **state)
{
	struct daos_csummer	*csummer;
	d_sg_list_t		 sgl;
	daos_recx_t		 recx;
	daos_csum_buf_t		*actual;
	daos_iod_t		 iod = {0};
	int			 rc = 0;

	fake_get_size_result = 4;
	daos_csummer_init(&csummer, &fake_algo, 16);
	fake_algo.cf_get_size = fake_get_size;

	dts_sgl_init_with_strings(&sgl, 1, "abcdef");

	recx.rx_idx = 0;
	recx.rx_nr = daos_sgl_buf_size(&sgl);
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	iod.iod_size = 1;

	rc = daos_csummer_calc(csummer, &sgl, &iod, &actual);

	assert_int_equal(0, rc);

	assert_int_equal(fake_get_size_result, actual->cs_buf_len);
	assert_int_equal(1, actual->cs_nr);
	assert_int_equal(fake_get_size_result, actual->cs_len);

	assert_int_equal(1, *dcb_idx2csum(actual, 0));

	daos_csummer_free_dcbs(csummer, &actual);
	daos_sgl_fini(&sgl, true);
	daos_csummer_destroy(&csummer);
}

static void
test_daos_checksummer_with_mult_iov_single_chunk(void **state)
{
	struct daos_csummer	*csummer;
	d_sg_list_t		 sgl;
	daos_recx_t		 recx;
	daos_csum_buf_t		*actual;
	daos_iod_t		 iod = {0};
	int			 rc = 0;

	fake_get_size_result = 4;
	daos_csummer_init(&csummer, &fake_algo, 16);

	dts_sgl_init_with_strings(&sgl, 3, "ab", "cdef", "gh");

	recx.rx_idx = 0;
	recx.rx_nr = daos_sgl_buf_size(&sgl);
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	iod.iod_size = 1;
	fake_update_bytes_seen = 0;

	rc = daos_csummer_calc(csummer, &sgl, &iod, &actual);

	assert_int_equal(0, rc);
	assert_int_equal(11, fake_update_bytes_seen);
	assert_int_equal(fake_get_size_result, actual->cs_buf_len);
	assert_int_equal(1, actual->cs_nr);
	assert_int_equal(fake_get_size_result, actual->cs_len);

	/** fake checksum calc should have been called 3 times (1 for each
	 * iov in sgl)
	 */
	assert_int_equal(3, *dcb_idx2csum(actual, 0));

	daos_sgl_fini(&sgl, true);
	daos_csummer_free_dcbs(csummer, &actual);
	daos_csummer_destroy(&csummer);

}
static void
test_daos_checksummer_with_multi_iov_multi_extents(void **state)
{
	struct daos_csummer	*csummer;
	d_sg_list_t		 sgl;
	daos_recx_t		 recx[2];
	daos_iod_t		 iod = {0};
	daos_csum_buf_t		*actual;
	int			 rc = 0;

	fake_get_size_result = 4;

	daos_csummer_init(&csummer, &fake_algo, 16);

	dts_sgl_init_with_strings(&sgl, 2, "abcdefghijklmnopqrstufwxyz",
				  "1234");

	assert_int_equal(32, daos_sgl_buf_size(&sgl)); /** Check my math */
	recx[0].rx_idx = 0;
	recx[0].rx_nr = 16;
	recx[1].rx_idx = 16;
	recx[1].rx_nr = 16;

	fake_update_bytes_seen = 0;

	iod.iod_nr = 2;
	iod.iod_recxs = recx;
	iod.iod_size = 1;

	rc = daos_csummer_calc(csummer, &sgl, &iod, &actual);

	assert_int_equal(0, rc);
	/** fake checksum calc should have been called once for the first one,
	 * all the bytes for recx[0] are in the first iov.
	 * fake checksum calc should have been called twice for recx[1]. It
	 * would need the rest of the bytes in iov 1 and all of
	 * iov 2 in the sgl.
	 */
	assert_int_equal(1, *dcb_idx2csum(&actual[0], 0));
	assert_int_equal(2, *dcb_idx2csum(&actual[1], 0));

	daos_sgl_fini(&sgl, true);
	daos_csummer_free_dcbs(csummer, &actual);
	daos_csummer_destroy(&csummer);

}


static void
test_daos_checksummer_with_multiple_chunks(void **state)
{
	struct daos_csummer	*csummer;
	d_sg_list_t		 sgl;
	daos_recx_t		 recx;
	daos_iod_t		 iod = {0};
	daos_csum_buf_t		*actual;
	int			 rc = 0;

	fake_update_buf = fake_update_buf_copy;

	fake_get_size_result = 4;

	daos_csummer_init(&csummer, &fake_algo, 4);

	dts_sgl_init_with_strings(&sgl, 1, "0123456789");

	recx.rx_idx = 0;
	recx.rx_nr = daos_sgl_buf_size(&sgl);
	fake_update_bytes_seen = 0;

	recx.rx_idx = 0;
	recx.rx_nr = daos_sgl_buf_size(&sgl);
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	iod.iod_size = 1;

	rc = daos_csummer_calc(csummer, &sgl, &iod, &actual);

	assert_int_equal(0, rc);
	int csum_expected_count = 3; /** 11/4=3 */

	assert_int_equal(fake_get_size_result * csum_expected_count,
			 actual->cs_buf_len);
	assert_int_equal(csum_expected_count, actual->cs_nr);

	assert_int_equal(1, *dcb_idx2csum(actual, 0));
	assert_int_equal(1, *dcb_idx2csum(actual, 1));
	assert_int_equal(1, *dcb_idx2csum(actual, 2));
	assert_int_equal(11, fake_update_bytes_seen);
	assert_string_equal("0123456789", fake_update_buf_copy);

	daos_sgl_fini(&sgl, true);
	daos_csummer_free_dcbs(csummer, &actual);
	daos_csummer_destroy(&csummer);
}

/**
 * -----------------------------------------------------------------------------
 * Test checksum comparison function
 * -----------------------------------------------------------------------------
 */

#define	setup_buf_for_test(csum, csum_buf) \
	dcb_set(&(csum), csum_buf, sizeof(csum_buf), 1, \
		sizeof(csum_buf), 1024)

static void
simple_test_compare_checksums(void **state)
{
	struct daos_csummer	*csummer;
	uint8_t			 csum_buf[] = "checksum";
	uint8_t			 csum_buf_same[] = "checksum";
	/** same len, different value */
	uint8_t			 csum_buf_dif[] = "corruptd";
	/** mostly the same, dif len */
	uint8_t			 csum_buf_dif_len[] = "checksumm";
	uint8_t			 csum_buf_dif_len2[] = "checksu";
	daos_csum_buf_t		 one;
	daos_csum_buf_t		 two;

	daos_csummer_init(&csummer, &fake_algo, 1024);
	one.cs_type = FAKE_CSUM_TYPE;
	two.cs_type = FAKE_CSUM_TYPE;

	setup_buf_for_test(one, csum_buf);
	setup_buf_for_test(two, csum_buf_same);
	assert_true(daos_csummer_compare(csummer, &one, &two));

	setup_buf_for_test(two, csum_buf_dif);
	assert_false(daos_csummer_compare(csummer, &one, &two));

	setup_buf_for_test(two, csum_buf_dif_len);
	assert_false(daos_csummer_compare(csummer, &one, &two));

	setup_buf_for_test(two, csum_buf_dif_len2);
	assert_false(daos_csummer_compare(csummer, &one, &two));

	daos_csummer_destroy(&csummer);
}

static void
test_compare_checksums(void **state)
{
	struct daos_csummer	*csummer;
	daos_csum_buf_t		*one;
	daos_csum_buf_t		*two;
	d_sg_list_t		 sgl;
	daos_recx_t		 recx;
	daos_iod_t		 iod = {0};
	int			 rc = 0;

	fake_get_size_result = 4;

	daos_csummer_init(&csummer, &fake_algo, 4);
	dts_sgl_init_with_strings(&sgl, 1, "0123456789");

	recx.rx_idx = 0;
	recx.rx_nr = daos_sgl_buf_size(&sgl);

	fake_update_bytes_seen = 0;

	recx.rx_idx = 0;
	recx.rx_nr = daos_sgl_buf_size(&sgl);
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	iod.iod_size = 1;

	rc = daos_csummer_calc(csummer, &sgl, &iod, &one);
	assert_int_equal(0, rc);
	rc = daos_csummer_calc(csummer, &sgl, &iod, &two);
	assert_int_equal(0, rc);

	assert_true(daos_csummer_compare(csummer, one, two));

	daos_sgl_fini(&sgl, true);
	daos_csummer_free_dcbs(csummer, &one);
	daos_csummer_free_dcbs(csummer, &two);
	daos_csummer_destroy(&csummer);
}

static void
test_verify_data(void **state)
{
	struct daos_csummer	*csummer;
	daos_iod_t		 iod = {0};
	d_sg_list_t		 sgl = {0};
	daos_size_t		 sgl_buf_half;
	daos_recx_t		 recxs[2] = {0};
	int			 rc;

	dts_sgl_init_with_strings(&sgl, 1, "0123456789");

	recxs[0].rx_idx = 0;
	sgl_buf_half = daos_sgl_buf_size(&sgl) / 2;
	recxs[0].rx_nr = sgl_buf_half;
	recxs[1].rx_idx = sgl_buf_half;
	recxs[1].rx_nr = sgl_buf_half;

	iod.iod_size = 1;
	iod.iod_nr = 2;
	iod.iod_recxs = recxs;

	/** Checksum not set in iod should pass verify */
	rc = daos_csum_check_sgl(&iod, &sgl);
	assert_int_equal(0, rc);

	daos_csummer_type_init(&csummer, CSUM_TYPE_ISAL_CRC64_REFL, 1024*1024);

	rc = daos_csummer_calc(csummer, &sgl, &iod, &iod.iod_csums);
	assert_int_equal(0, rc);

	rc = daos_csum_check_sgl(&iod, &sgl);
	assert_int_equal(0, rc);

	((char *)sgl.sg_iovs[0].iov_buf)[0]++; /** Corrupt the data */
	rc = daos_csum_check_sgl(&iod, &sgl);
	assert_int_equal(-DER_IO, rc);

	((char *)sgl.sg_iovs[0].iov_buf)[0]--; /** Un-corrupt the data */
	/** Corrupt data elsewhere*/
	((char *)sgl.sg_iovs[0].iov_buf)[sgl_buf_half + 1]++;
	rc = daos_csum_check_sgl(&iod, &sgl);
	assert_int_equal(-DER_IO, rc);

	/** Clean up */
	daos_csummer_free_dcbs(csummer, &iod.iod_csums);
	daos_csummer_destroy(&csummer);
}

static void
test_daos_iod_csum_alloc(void **state)
{
	struct daos_csummer	*csummer;

	daos_iod_t		 iods[2] = {0};
	daos_recx_t		 recx[3] = {0};
	daos_csum_buf_t		*csum_out;
	uint32_t		 csum_nr;

	daos_csummer_init(&csummer, &fake_algo, 4);

	recx[0].rx_nr = 4;
	recx[1].rx_nr = 4;
	recx[2].rx_nr = 4;

	iods[0].iod_nr = 2;
	iods[0].iod_recxs = &recx[0];
	iods[1].iod_nr = 1;
	iods[1].iod_recxs = &recx[2];
	assert_null(iods[0].iod_csums);
	daos_csummer_alloc_dcbs(csummer, iods, 1, &csum_out, &csum_nr);
	assert_int_equal(2, csum_nr);
	/** shouldn't touch the iod csum, only use it for info */
	assert_null(iods[0].iod_csums);
	daos_csummer_free_dcbs(csummer, &csum_out);

	daos_csummer_alloc_dcbs(csummer, iods, 2, &csum_out, &csum_nr);
	assert_int_equal(3, csum_nr);
	daos_csummer_free_dcbs(csummer, &csum_out);
}

static void
print_checksum(struct daos_csummer *csummer, daos_csum_buf_t *csum)
{
	uint32_t i, c;

	D_PRINT("Type: %d\n", csum->cs_type);
	D_PRINT("Name: %s\n", daos_csummer_get_name(csummer));
	D_PRINT("Count: %d\n", csum->cs_nr);
	D_PRINT("Len: %d\n", csum->cs_len);
	D_PRINT("Buf Len: %d\n", csum->cs_buf_len);
	D_PRINT("Chunk: %d\n", csum->cs_chunksize);
	for (c = 0; c < csum->cs_nr; c++) {
		uint8_t *csum_bytes = dcb_idx2csum(csum, c);

		D_PRINT("Checksum[%02d]: 0x", c);
		for (i = 0; i < csum->cs_len; i++)
			D_PRINT("%02x", csum_bytes[i]);
		D_PRINT("\n");
	}
	D_PRINT("\n");
}

/**
 * -----------------------------------------------------------------------------
 * Loop through and verify all the different checksum algorithms supporting
 * -----------------------------------------------------------------------------
 */
static void
test_all_checksum_types(void **state)
{
	d_sg_list_t		 sgl;
	daos_recx_t		 recxs;
	enum DAOS_CSUM_TYPE	 type;
	struct daos_csummer	*csummer = NULL;
	daos_csum_buf_t		*csums = NULL;
	int			 csum_lens[CSUM_TYPE_END];
	daos_iod_t		 iod = {0};
	int			 rc;

	/** expected checksum lengths */
	csum_lens[CSUM_TYPE_ISAL_CRC16_T10DIF]	= 2;
	csum_lens[CSUM_TYPE_ISAL_CRC32_ISCSI]	= 4;
	csum_lens[CSUM_TYPE_ISAL_CRC64_REFL]	= 8;

	dts_sgl_init_with_strings(&sgl, 1, "Lorem ipsum dolor sit amet, "
"consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et "
"dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation "
"ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure "
"dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla "
"pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui "
"officia deserunt mollit anim id est laborum.");

	recxs.rx_idx = 0;
	recxs.rx_nr = daos_sgl_buf_size(&sgl);

	for (type = CSUM_TYPE_UNKNOWN + 1; type < CSUM_TYPE_END; type++) {
		rc = daos_csummer_init(&csummer,
			daos_csum_type2algo(type), 128);
		assert_int_equal(0, rc);

		iod.iod_nr = 1;
		iod.iod_recxs = &recxs;
		iod.iod_size = 1;

		rc = daos_csummer_calc(csummer, &sgl, &iod, &csums);

		assert_int_equal(0, rc);
		assert_int_equal(csum_lens[type],
				 daos_csummer_get_csum_len(csummer));

		if (verbose)
			print_checksum(csummer, &csums[0]);

		daos_csummer_free_dcbs(csummer, &csums);
		daos_csummer_destroy(&csummer);
	}

	daos_sgl_fini(&sgl, true);
}

/**
 * -----------------------------------------------------------------------------
 * Test some helper functions for indexing checksums within a daos_csum_buf_t
 * -----------------------------------------------------------------------------
 */
static void
test_helper_functions(void **state)
{
	daos_csum_buf_t csum;

	csum.cs_len = 2;
	csum.cs_chunksize = 4;
	csum.cs_buf_len = 4;
	csum.cs_nr = 2;
	csum.cs_csum = calloc(csum.cs_buf_len, 1);
	csum.cs_csum[0] = 1;
	csum.cs_csum[1] = 1;
	csum.cs_csum[2] = 2;
	csum.cs_csum[3] = 2;

	assert_int_equal(0x0101, *(uint16_t *) dcb_idx2csum(&csum, 0));
	assert_int_equal(0x0202, *(uint16_t *) dcb_idx2csum(&csum, 1));
	assert_int_equal(NULL, dcb_idx2csum(&csum, 2));


	assert_int_equal(0, dcb_off2idx(&csum, 0));
	assert_int_equal(1, dcb_off2idx(&csum, 4));
	assert_int_equal(1, dcb_off2idx(&csum, 5));

	assert_int_equal(0x0101, *(uint16_t *) dcb_off2csum(
		&csum, 0));
	assert_int_equal(0x0202, *(uint16_t *) dcb_off2csum(
		&csum, 4));


	/** try some larger values */
	csum.cs_chunksize = 1024 * 16; /** 16K */
	assert_int_equal(0, dcb_off2idx(&csum, 1024 * 16 - 1));
	assert_int_equal(1, dcb_off2idx(&csum, 1024 * 16));
	assert_int_equal(1024, dcb_off2idx(&csum, 1024 * 1024 * 16));

	free(csum.cs_csum);
}

static void
test_csum_chunk_count(void **state)
{
	/** chunksize, lo_idx, hi_idx, rec_size */
	assert_int_equal(1, csum_chunk_count(1, 0, 1, 1));
	assert_int_equal(2, csum_chunk_count(2, 1, 3, 1));
	assert_int_equal(3, csum_chunk_count(2, 1, 5, 1));
	assert_int_equal(80, csum_chunk_count(4, 32, 64, 10));
}

static void
test_recx_calc_chunks(void **state)
{
	uint32_t	chunksize = 1;
	uint32_t	rec_size = 1;
	daos_recx_t	recx = {0};

	assert_int_equal(0, daos_recx_calc_chunks(recx, rec_size, chunksize));

	recx.rx_nr = 1;
	assert_int_equal(1, daos_recx_calc_chunks(recx, rec_size, chunksize));

	rec_size = 2;
	assert_int_equal(2, daos_recx_calc_chunks(recx, rec_size, chunksize));

	chunksize = 4;
	recx.rx_idx = 1;
	recx.rx_nr = 16;
	rec_size = 1;
	/** chunks = 0-3, 4-7, 8-11, 12-16, 16-20 */
	assert_int_equal(5, daos_recx_calc_chunks(recx, rec_size, chunksize));
}

static void
test_helper_iod_functions(void **state)
{
	/** Calculate total number of recxs & csums in list of iods */
	daos_iod_t iods[2] = {0};
	daos_recx_t recx[4] = {0};
	uint32_t dcb_nr, csum_nr;

	iods[0].iod_nr = 1;
	iods[0].iod_size = 1;
	iods[0].iod_recxs = &recx[0];
	iods[0].iod_recxs[0].rx_idx = 0;
	iods[0].iod_recxs[0].rx_nr = 32;
	daos_iods_count_needed_csum(iods, 1, 32, &dcb_nr, &csum_nr);
	assert_int_equal(1, dcb_nr);
	assert_int_equal(1, csum_nr);

	iods[0].iod_nr = 2;
	iods[0].iod_recxs[1].rx_idx = 32;
	iods[0].iod_recxs[1].rx_nr = 32;
	daos_iods_count_needed_csum(iods, 1, 32, &dcb_nr, &csum_nr);
	assert_int_equal(2, dcb_nr);
	assert_int_equal(2, csum_nr);

	iods[0].iod_recxs[1].rx_nr = 64;
	daos_iods_count_needed_csum(iods, 1, 32, &dcb_nr, &csum_nr);
	assert_int_equal(2, dcb_nr);
	assert_int_equal(3, csum_nr);

	iods[1].iod_nr = 2;
	iods[1].iod_size = 1;
	iods[1].iod_recxs = &recx[2];
	iods[1].iod_recxs[0].rx_idx = 0;
	iods[1].iod_recxs[0].rx_nr = 32;
	iods[1].iod_recxs[1].rx_idx = 0;
	iods[1].iod_recxs[1].rx_nr = 32;
	daos_iods_count_needed_csum(iods, 2, 32, &dcb_nr, &csum_nr);
	assert_int_equal(4, dcb_nr);
	assert_int_equal(5, csum_nr);
	iods[0].iod_recxs[1].rx_nr = 32;
}

/**
 * -----------------------------------------------------------------------------
 * Test some DAOS Container Property Knowledge
 * -----------------------------------------------------------------------------
 */
static void
test_container_prop_to_csum_type(void **state)
{
	assert_int_equal(CSUM_TYPE_ISAL_CRC16_T10DIF,
			 daos_contprop2csumtype(DAOS_PROP_CO_CSUM_CRC16));
	assert_int_equal(CSUM_TYPE_ISAL_CRC32_ISCSI,
			 daos_contprop2csumtype(DAOS_PROP_CO_CSUM_CRC32));
	assert_int_equal(CSUM_TYPE_ISAL_CRC64_REFL,
			 daos_contprop2csumtype(DAOS_PROP_CO_CSUM_CRC64));
}

static void
test_is_valid_csum(void **state)
{
	assert_true(daos_cont_csum_prop_is_valid(DAOS_PROP_CO_CSUM_OFF));
	assert_true(daos_cont_csum_prop_is_valid(DAOS_PROP_CO_CSUM_CRC16));
	assert_true(daos_cont_csum_prop_is_valid(DAOS_PROP_CO_CSUM_CRC32));

	/** Not supported yet */
	assert_false(daos_cont_csum_prop_is_valid(DAOS_PROP_CO_CSUM_SHA1));
	assert_false(daos_cont_csum_prop_is_valid(99));
}

static void
test_is_csum_enabled(void **state)
{
	assert_true(daos_cont_csum_prop_is_enabled(DAOS_PROP_CO_CSUM_CRC16));
	assert_true(daos_cont_csum_prop_is_enabled(DAOS_PROP_CO_CSUM_CRC32));

	/** Not supported yet */
	assert_false(daos_cont_csum_prop_is_enabled(DAOS_PROP_CO_CSUM_SHA1));
	assert_false(daos_cont_csum_prop_is_enabled(DAOS_PROP_CO_CSUM_OFF));
	assert_false(daos_cont_csum_prop_is_enabled(9999));
}

static const struct CMUnitTest tests[] = {
	{"CSUM01: Test initialize and destroy checksummer",
		test_init_and_destroy, NULL, NULL},
	{"CSUM02: Test update and get the checksum",
		test_update_reset, NULL, NULL},
	{"CSUM03: Test update with multiple buffer",
		test_update_with_multiple_buffers, NULL, NULL},
	{"CSUM04: Allocate appropriate memory for the csum buf and csums",
		test_checksummer_allocates_csum_buf, NULL, NULL},
	{"CSUM05: Create checksum from a single iov, recx, and chunk",
		test_daos_checksummer_with_single_iov_single_chunk, NULL, NULL},
	{"CSUM06: Create checksum from a multiple iov, single recx, and chunk",
		test_daos_checksummer_with_mult_iov_single_chunk, NULL, NULL},
	{"CSUM07: Create checksum from a multiple iov, multi recx, and chunk",
		test_daos_checksummer_with_multi_iov_multi_extents, NULL, NULL},
	{"CSUM08: More complicated daos checksumming",
		test_daos_checksummer_with_multiple_chunks, NULL, NULL},
	{"CSUM09: Test the different types of checksums",
		test_all_checksum_types, NULL, NULL},
	{"CSUM10: Test map from container prop to csum type",
		test_container_prop_to_csum_type, NULL, NULL},
	{"CSUM11: Some helper function tests",
		test_helper_functions, NULL, NULL},
	{"CSUM12: Is Valid Checksum Property",
		test_is_valid_csum, NULL, NULL },
	{"CSUM13: Is Checksum Property Enabled",
		test_is_csum_enabled,          NULL, NULL },
	{"CSUM14: A simple checksum comparison test",
		simple_test_compare_checksums, NULL, NULL },
	{"CSUM15: Compare checksums after actual calculation",
		test_compare_checksums,        NULL, NULL },
	{"CSUM16: Verify data represented by IOD and SGL. ",
		test_verify_data,              NULL, NULL },
	{"CSUM17: IOD Allocate csum descriptors ",
		test_daos_iod_csum_alloc,      NULL, NULL },
	{"CSUM18: IOD helper functions ",
		test_helper_iod_functions,    NULL, NULL },
	{"CSUM19: Calculating number of chunks for range",
		test_csum_chunk_count,        NULL, NULL },
	{"CSUM20: Calculating number of chunks for an extent",
		test_recx_calc_chunks,        NULL, NULL },
};

int
daos_checksum_tests_run()
{
	verbose = false;
	return cmocka_run_group_tests_name("DAOS Checksum Tests", tests,
					   NULL, NULL);
}
