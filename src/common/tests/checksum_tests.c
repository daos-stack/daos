/*
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC        DD_FAC(tests)


#include <string.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h> /* For cmocka.h */
#include <stdint.h>
#include <cmocka.h>
#include <gurt/types.h>
#include <daos/checksum.h>
#include <daos/common.h>
#include <daos/cont_props.h>
#include <daos/tests_lib.h>
#include <daos/test_perf.h>

static bool verbose;

#define assert_ci_equal(e, a) do {\
	assert_int_equal((e).cs_nr, (a).cs_nr); \
	assert_int_equal((e).cs_len, (a).cs_len); \
	assert_int_equal((e).cs_buf_len, (a).cs_buf_len); \
	assert_int_equal((e).cs_chunksize, (a).cs_chunksize); \
	assert_int_equal((e).cs_type, (a).cs_type); \
	assert_memory_equal((e).cs_csum, (a).cs_csum, (e).cs_len * (e).cs_nr); \
	} while (0)

#define assert_ic_equal(e, a) do {\
	int __i; \
	assert_int_equal((e).ic_nr, (a).ic_nr); \
	assert_ci_equal((e).ic_akey, (a).ic_akey); \
	for (__i = 0; __i < (e).ic_nr; __i++) \
		assert_ci_equal((e).ic_data[__i], (a).ic_data[__i]);\
} while (0)

/*
 * -----------------------------------------------------------------------------
 * Setup some fake functions and variables to track how the functions are
 * called. Will be used for checksummer testing.
 * -----------------------------------------------------------------------------
 */
#define FAKE_CSUM_TYPE 999
static int fake_init_called;
uint8_t fake_val;
static int
fake_init(void **daos_mhash_ctx)
{
	fake_init_called++;
	fake_val = 0;
	*daos_mhash_ctx = &fake_val;
	return 0;
}

static int fake_fini_called;
static void
fake_fini(void *daos_mhash_ctx)
{
	fake_fini_called++;
}

#define FAKE_UPDATE_BUF_LEN 512
static char fake_update_buf_copy[FAKE_UPDATE_BUF_LEN];
static char *fake_update_buf = fake_update_buf_copy;
static int fake_update_bytes_seen;
static int
fake_update(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	if (buf_len == 0)
		return 0;
	*((uint8_t *)daos_mhash_ctx) += 1; /* Just increment */
	fake_update_bytes_seen += buf_len;
	strncpy(fake_update_buf, (char *)buf, buf_len);
	fake_update_buf += buf_len;
	fake_update_buf[0] = '|';
	fake_update_buf++;
	return 0;
}

static uint16_t fake_get_size_result;
static uint16_t
fake_get_size(void *daos_mhash_ctx)
{
	return fake_get_size_result;
}

int
fake_reset(void *daos_mhash_ctx)
{
	*((uint8_t *)daos_mhash_ctx) = 0;
	return 0;
}

int
fake_finish(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	*buf = *((uint8_t *)daos_mhash_ctx);
	return 0;
}

static struct hash_ft fake_algo = {
	.cf_init	= fake_init,
	.cf_destroy	= fake_fini,
	.cf_update	= fake_update,
	.cf_reset	= fake_reset,
	.cf_finish	= fake_finish,
	.cf_hash_len	= 4,
	.cf_get_size	= NULL,
	.cf_type	= FAKE_CSUM_TYPE,
	.cf_name	= "fake"
};

void
reset_fake_algo(void)
{
	memset(fake_update_buf_copy, 0, FAKE_UPDATE_BUF_LEN);
	fake_update_buf = fake_update_buf_copy;
	fake_update_bytes_seen = 0;
	fake_init_called = 0;
	fake_fini_called = 0;
	fake_get_size_result = 0;
}

/*
 * -----------------------------------------------------------------------------
 * Test the CSUMMER initialize, destroy, and some other basic functions
 * -----------------------------------------------------------------------------
 */
static void
test_init_and_destroy(void **state)
{
	fake_init_called = 0;

	struct daos_csummer *csummer;
	int rc = daos_csummer_init(&csummer, &fake_algo, 0, 0);

	assert_rc_equal(0, rc);
	assert_int_equal(1, fake_init_called);
	assert_int_equal(FAKE_CSUM_TYPE, daos_csummer_get_type(csummer));

	/* get size should use static size or get size function if set*/
	fake_algo.cf_hash_len = 4;
	assert_int_equal(4, daos_csummer_get_csum_len(csummer));
	fake_algo.cf_hash_len = 0;
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
	daos_csummer_init(&csummer, &fake_algo, 0, 0);
	daos_csummer_set_buffer(csummer, (uint8_t *) &csum, sizeof(csum));

	size_t len = 32;
	uint8_t buf[len];

	memset(buf, 0, len);

	/* before an update, the csum should be 0 */
	assert_int_equal(0, csum);

	/* The fake csummer simply increments the csum each time */
	daos_csummer_update(csummer, buf, len);
	daos_csummer_finish(csummer);
	assert_int_equal(1, csum);


	daos_csummer_update(csummer, buf, len);
	daos_csummer_finish(csummer);
	assert_int_equal(2, csum);

	/* reset */
	daos_csummer_reset(csummer);
	daos_csummer_finish(csummer);
	assert_int_equal(0, csum);

	daos_csummer_destroy(&csummer);
}

static void
test_update_with_multiple_buffers(void **state)
{
	uint32_t		 csum = 0; /* buffer */
	uint32_t		 csum2 = 0; /* buffer */
	size_t			 len = 64;
	uint8_t			 buf[len];
	struct daos_csummer	*csummer;

	fake_get_size_result = sizeof(uint32_t); /* setup fake checksum */
	daos_csummer_init(&csummer, &fake_algo, 0, 0);

	memset(buf, 0xa, len);

	daos_csummer_set_buffer(csummer, (uint8_t *) &csum, sizeof(uint32_t));
	daos_csummer_update(csummer, buf, len);
	daos_csummer_finish(csummer);
	assert_int_equal(1, csum);

	daos_csummer_reset(csummer);
	daos_csummer_set_buffer(csummer, (uint8_t *) &csum2, sizeof(uint32_t));
	daos_csummer_update(csummer, buf, len);
	daos_csummer_finish(csummer);
	assert_int_equal(1, csum2);

	daos_csummer_destroy(&csummer);
}

/*
 * -----------------------------------------------------------------------------
 * A series of tests for calculating csums
 * -----------------------------------------------------------------------------
 */

static void
test_daos_checksummer_with_single_iov_single_chunk(void **state)
{
	struct daos_csummer	*csummer;
	d_sg_list_t		 sgl;
	daos_recx_t		 recx;
	struct dcs_iod_csums	*actual;
	daos_iod_t		 iod = {0};
	int			 rc = 0;

	fake_get_size_result = 4;
	daos_csummer_init(&csummer, &fake_algo, 16, 0);
	fake_algo.cf_get_size = fake_get_size;

	dts_sgl_init_with_strings(&sgl, 1, "abcdef");

	recx.rx_idx = 0;
	recx.rx_nr = daos_sgl_buf_size(&sgl);
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	iod.iod_size = 1;
	iod.iod_type = DAOS_IOD_ARRAY;

	rc = daos_csummer_calc_iods(csummer, &sgl, &iod, NULL, 1, 0, NULL, 0,
				    &actual);

	assert_rc_equal(0, rc);

	assert_int_equal(fake_get_size_result, actual->ic_data[0].cs_buf_len);
	assert_int_equal(1, actual->ic_data[0].cs_nr);
	assert_int_equal(fake_get_size_result, actual->ic_data[0].cs_len);

	assert_int_equal(1, *ic_idx2csum(actual, 0, 0));

	daos_csummer_free_ic(csummer, &actual);
	d_sgl_fini(&sgl, true);
	daos_csummer_destroy(&csummer);
}

static void
test_daos_checksummer_with_unaligned_recx(void **state)
{
	struct daos_csummer	*csummer;
	d_sg_list_t		 sgl;
	daos_recx_t		 recx;
	struct dcs_iod_csums	*actual;
	daos_iod_t		 iod = {0};
	int			 rc = 0;

	reset_fake_algo();

	fake_get_size_result = 4;
	daos_csummer_init(&csummer, &fake_algo, 2, 0);
	fake_algo.cf_get_size = fake_get_size;

	dts_sgl_init_with_strings(&sgl, 1, "ab");

	recx.rx_idx = 1;
	recx.rx_nr = daos_sgl_buf_size(&sgl);
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	iod.iod_size = 1;
	iod.iod_type = DAOS_IOD_ARRAY;
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));

	rc = daos_csummer_calc_iods(csummer, &sgl, &iod, NULL, 1, 0, NULL, 0,
				    &actual);

	assert_rc_equal(0, rc);
	assert_string_equal("akey|a|b", fake_update_buf_copy);

	assert_int_equal(fake_get_size_result * 2,
			 actual->ic_data[0].cs_buf_len);
	assert_int_equal(2, actual->ic_data[0].cs_nr);
	assert_int_equal(fake_get_size_result, actual->ic_data[0].cs_len);

	assert_int_equal(1, *ic_idx2csum(actual, 0, 0));
	assert_int_equal(1, *ic_idx2csum(actual, 0, 1));

	daos_csummer_free_ic(csummer, &actual);
	d_sgl_fini(&sgl, true);
	daos_csummer_destroy(&csummer);
}

static void
test_daos_checksummer_with_mult_iov_single_chunk(void **state)
{
	struct daos_csummer	*csummer;
	d_sg_list_t		 sgl;
	daos_recx_t		 recx;
	struct dcs_iod_csums	*actual;
	daos_iod_t		 iod = {0};
	int			 rc = 0;

	fake_get_size_result = 4;
	daos_csummer_init(&csummer, &fake_algo, 16, 0);

	dts_sgl_init_with_strings(&sgl, 3, "ab", "cdef", "gh");

	recx.rx_idx = 0;
	recx.rx_nr = daos_sgl_buf_size(&sgl);
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	iod.iod_size = 1;
	iod.iod_type = DAOS_IOD_ARRAY;
	fake_update_bytes_seen = 0;

	rc = daos_csummer_calc_iods(csummer, &sgl, &iod, NULL, 1, 0, NULL, 0,
				    &actual);

	assert_rc_equal(0, rc);
	assert_int_equal(11, fake_update_bytes_seen);
	assert_int_equal(fake_get_size_result, actual->ic_data[0].cs_buf_len);
	assert_int_equal(1, actual->ic_data[0].cs_nr);
	assert_int_equal(fake_get_size_result, actual->ic_data[0].cs_len);

	/* fake checksum calc should have been called 3 times (1 for each
	 * iov in sgl)
	 */
	assert_int_equal(3, *ic_idx2csum(actual, 0, 0));

	d_sgl_fini(&sgl, true);
	daos_csummer_free_ic(csummer, &actual);
	daos_csummer_destroy(&csummer);

}
static void
test_daos_checksummer_with_multi_iov_multi_extents(void **state)
{
	struct daos_csummer	*csummer;
	d_sg_list_t		 sgl;
	daos_recx_t		 recx[2];
	daos_iod_t		 iod = {0};
	struct dcs_iod_csums	*actual;
	int			 rc = 0;

	fake_get_size_result = fake_algo.cf_hash_len = 4;

	daos_csummer_init(&csummer, &fake_algo, 16, 0);

	dts_sgl_init_with_strings(&sgl, 2, "abcdefghijklmnopqrstufwxyz",
				  "1234");

	assert_int_equal(32, daos_sgl_buf_size(&sgl)); /* Check my math */
	recx[0].rx_idx = 0;
	recx[0].rx_nr = 16;
	recx[1].rx_idx = 16;
	recx[1].rx_nr = 16;

	fake_update_bytes_seen = 0;

	iod.iod_nr = 2;
	iod.iod_recxs = recx;
	iod.iod_size = 1;
	iod.iod_type = DAOS_IOD_ARRAY;

	rc = daos_csummer_calc_iods(csummer, &sgl, &iod, NULL, 1, 0, NULL, 0,
				    &actual);

	assert_rc_equal(0, rc);
	/* fake checksum calc should have been called once for the first one,
	 * all the bytes for recx[0] are in the first iov.
	 * fake checksum calc should have been called twice for recx[1]. It
	 * would need the rest of the bytes in iov 1 and all of
	 * iov 2 in the sgl.
	 */
	assert_int_equal(1, *ic_idx2csum(actual, 0, 0));
	assert_int_equal(2, *ic_idx2csum(actual, 1, 0));

	d_sgl_fini(&sgl, true);
	daos_csummer_free_ic(csummer, &actual);
	daos_csummer_destroy(&csummer);

}

static void
test_daos_checksummer_with_multiple_chunks(void **state)
{
	struct daos_csummer	*csummer;
	d_sg_list_t		 sgl;
	daos_recx_t		 recx;
	daos_iod_t		 iod = {0};
	struct dcs_iod_csums	*actual;
	int			 rc = 0;

	fake_update_buf = fake_update_buf_copy;

	fake_get_size_result = 4;

	daos_csummer_init(&csummer, &fake_algo, 4, 0);

	dts_sgl_init_with_strings(&sgl, 1, "0123456789");

	recx.rx_idx = 0;
	recx.rx_nr = daos_sgl_buf_size(&sgl);
	fake_update_bytes_seen = 0;

	recx.rx_idx = 0;
	recx.rx_nr = daos_sgl_buf_size(&sgl);
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	iod.iod_size = 1;
	iod.iod_type = DAOS_IOD_ARRAY;
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));

	rc = daos_csummer_calc_iods(csummer, &sgl, &iod, NULL, 1, 0, NULL, 0,
				    &actual);

	assert_rc_equal(0, rc);
	int csum_expected_count = 3; /* 11/4=3 */

	assert_int_equal(fake_get_size_result * csum_expected_count,
			 actual->ic_data[0].cs_buf_len);
	assert_int_equal(csum_expected_count, actual->ic_data[0].cs_nr);

	assert_int_equal(1, *ic_idx2csum(actual, 0, 0));
	assert_int_equal(1, *ic_idx2csum(actual, 0, 1));
	assert_int_equal(1, *ic_idx2csum(actual, 0, 2));
	assert_int_equal(15, fake_update_bytes_seen);
	assert_string_equal("akey|0123|4567|89", fake_update_buf_copy);

	d_sgl_fini(&sgl, true);
	daos_csummer_free_ic(csummer, &actual);
	daos_csummer_destroy(&csummer);
}

static void
get_map_test(void **state)
{
	daos_iom_t		map = {0};
	struct daos_csum_range	range;
	daos_recx_t		recxs[10];
	uint32_t		i;
	struct daos_csum_range	result;

	for (i = 0; i < ARRAY_SIZE(recxs); i++) {
		recxs[i].rx_idx = i * 10 + 1;
		recxs[i].rx_nr = 5;
	}
	map.iom_recxs = recxs;
	map.iom_nr = 1;

	/* Includes only */
	dcr_set_idx_nr(&range, 0, 10);
	result = get_maps_idx_nr_for_range(&range, &map);
	assert_int_equal(0, result.dcr_lo);
	assert_int_equal(1, result.dcr_nr);

	/* Includes second */
	map.iom_nr = 2;
	dcr_set_idx_nr(&range, 10, 10);
	result = get_maps_idx_nr_for_range(&range, &map);
	assert_int_equal(1, result.dcr_lo);
	assert_int_equal(1, result.dcr_nr);

	/* Only includes second */
	map.iom_nr = ARRAY_SIZE(recxs);
	dcr_set_idx_nr(&range, 10, 10);
	result = get_maps_idx_nr_for_range(&range, &map);
	assert_int_equal(1, result.dcr_lo);
	assert_int_equal(1, result.dcr_nr);

	/* Only includes second and third */
	dcr_set_idx_nr(&range, 10, 20);
	result = get_maps_idx_nr_for_range(&range, &map);
	assert_int_equal(1, result.dcr_lo);
	assert_int_equal(2, result.dcr_nr);

	/* includes 3, 4, 5  */
	dcr_set_idx_nr(&range, 20, 30);
	result = get_maps_idx_nr_for_range(&range, &map);
	assert_int_equal(2, result.dcr_lo);
	assert_int_equal(3, result.dcr_nr);

	/* includes all */
	dcr_set_idx_nr(&range, 0, 100);
	result = get_maps_idx_nr_for_range(&range, &map);
	assert_int_equal(0, result.dcr_lo);
	assert_int_equal(ARRAY_SIZE(recxs), result.dcr_nr);

	/* includes none */
	dcr_set_idx_nr(&range, 1000, 100);
	result = get_maps_idx_nr_for_range(&range, &map);
	assert_int_equal(0, result.dcr_lo);
	assert_int_equal(0, result.dcr_nr);

	/* Overlapping should be included */
	dcr_set_idx_nr(&range, 0, 3);
	result = get_maps_idx_nr_for_range(&range, &map);
	assert_int_equal(0, result.dcr_lo);
	assert_int_equal(1, result.dcr_nr);
	dcr_set_idx_nr(&range, 14, 3);
	result = get_maps_idx_nr_for_range(&range, &map);
	assert_int_equal(1, result.dcr_lo);
	assert_int_equal(1, result.dcr_nr);

	/* Mapped area is larger than request */
	dcr_set_idx_nr(&range, 3, 3);
	map.iom_recxs[0].rx_idx = 0;
	map.iom_recxs[0].rx_nr = 10;
	map.iom_size = 8;
	map.iom_nr = 1;
	result = get_maps_idx_nr_for_range(&range, &map);
	assert_int_equal(0, result.dcr_lo);
	assert_int_equal(1, result.dcr_nr);
}

static void
test_skip_csum_calculations_when_skip_set(void **state)
{
	struct daos_csummer	*csummer;
	d_sg_list_t		 sgl;
	daos_recx_t		 recx;
	struct dcs_iod_csums	*iod_csums;
	daos_iod_t		 iod = {0};
	int			 rc = 0;

	fake_get_size_result = 4;
	daos_csummer_init(&csummer, &fake_algo, 16, 0);
	fake_algo.cf_get_size = fake_get_size;

	dts_sgl_init_with_strings(&sgl, 1, "abcdef");
	sgl.sg_iovs->iov_len --; /* remove ending '\0' */

	recx.rx_idx = 0;
	recx.rx_nr = daos_sgl_buf_size(&sgl);
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	iod.iod_size = 1;
	iod.iod_type = DAOS_IOD_ARRAY;
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));

	/* skip key calculation */
	csummer->dcs_skip_key_calc = true;
	rc = daos_csummer_calc_iods(csummer, &sgl, &iod, NULL, 1, 0, NULL, 0,
				    &iod_csums);
	assert_rc_equal(0, rc);
	/* with key calculation would look like this ...
	 * assert_string_equal("akey|abcdef|", fake_update_buf_copy);
	 */
	assert_string_equal("abcdef|", fake_update_buf_copy);

	/*
	 * skipping the data verification means that the csummer won't try
	 * to calculate the checksum again to verify.
	 */
	/* reset */
	memset(fake_update_buf_copy, 0, FAKE_UPDATE_BUF_LEN);
	fake_update_buf = fake_update_buf_copy;

	csummer->dcs_skip_data_verify = true;
	rc = daos_csummer_verify_iod(csummer, &iod, &sgl, iod_csums, NULL,
				     0, NULL);
	assert_rc_equal(0, rc);
	assert_string_equal("", fake_update_buf_copy); /* update not called */

	daos_csummer_free_ic(csummer, &iod_csums);
	d_sgl_fini(&sgl, true);
	daos_csummer_destroy(&csummer);
}

#define assert_dcs_csum_info_list_init(list, nr)            \
	do {                                                \
	assert_success(dcs_csum_info_list_init(&list, nr)); \
	if (nr == 0)                                        \
		assert_null(list.dcl_csum_infos);           \
	if (nr > 0)                                         \
		assert_non_null(list.dcl_csum_infos);       \
	assert_int_equal(0, list.dcl_csum_infos_nr);        \
	} while (0)


static void
test_csum_info_list_handling(void **state)
{
	struct dcs_ci_list list = {0};
	int i;

	/* set to garbage so can test that all fields are set */
	memset(&list, 0xFF, sizeof(list));

	assert_dcs_csum_info_list_init(list, 0);
	dcs_csum_info_list_fini(&list);
	assert_dcs_csum_info_list_init(list, 1);

	dcs_csum_info_list_fini(&list);
	assert_dcs_csum_info_list_init(list, 2);

	uint16_t csum1 = 0xABCD;
	uint32_t csum2 = 0x4321EFAB;
	uint64_t csum3 = 0x1234567890ABCDEF;
	struct dcs_csum_info info[] = {
		{
			.cs_len = 2, .cs_nr = 1, .cs_csum = (uint8_t *) &csum1,
			.cs_chunksize = 1024, .cs_type = 99, .cs_buf_len = 2
		}, {
			.cs_len = 2, .cs_nr = 2, .cs_csum = (uint8_t *) &csum2,
			.cs_chunksize = 1024, .cs_type = 99, .cs_buf_len = 4
		}, {
			.cs_len = 2, .cs_nr = 4, .cs_csum = (uint8_t *) &csum3,
			.cs_chunksize = 1024, .cs_type = 99, .cs_buf_len = 8
		},
	};

	for (i = 0; i < ARRAY_SIZE(info); i++)
		dcs_csum_info_save(&list, &info[i]);

	assert_int_equal(ARRAY_SIZE(info), list.dcl_csum_infos_nr);
	for (i = 0; i < ARRAY_SIZE(info); i++) {
		assert_ci_equal(info[i], *dcs_csum_info_get(&list, i));
		/* shouldn't be using the same buffer */
		assert_int_not_equal(info[i].cs_csum, dcs_csum_info_get(&list, i)->cs_csum);
	}


	/* invalid index returns NULL */
	assert_null(dcs_csum_info_get(&list, 999));

	dcs_csum_info_list_fini(&list);
	assert_null(list.dcl_csum_infos);
	assert_int_equal(0, list.dcl_buf_size);
	assert_int_equal(0, list.dcl_csum_infos_nr);
	assert_int_equal(0, list.dcl_buf_used);
}

static void
test_csum_info_list_handle_many(void **state)
{
	struct dcs_ci_list	list = {0};
	struct dcs_csum_info	info[100] = {0};
	int			i;

	assert_dcs_csum_info_list_init(list, 2);

	srand(time(NULL));
	for (i = 0; i < ARRAY_SIZE(info); i++) {
		int j;

		info[i].cs_type = 99;
		info[i].cs_len = 2;
		info[i].cs_nr = rand() % 4 + 1;
		info[i].cs_buf_len = info[i].cs_len * info[i].cs_nr;
		D_ALLOC(info[i].cs_csum, info[i].cs_buf_len);
		assert_non_null(info[i].cs_csum);
		for (j = 0; j < info[i].cs_buf_len; j++)
			info[i].cs_csum[j] = rand();

		dcs_csum_info_save(&list, &info[i]);
	}

	assert_int_equal(ARRAY_SIZE(info), list.dcl_csum_infos_nr);
	for (i = 0; i < ARRAY_SIZE(info); i++) {
		assert_ci_equal(info[i], *dcs_csum_info_get(&list, i));
		/* shouldn't be using the same buffer */
		assert_int_not_equal(info[i].cs_csum, dcs_csum_info_get(&list, i)->cs_csum);
	}

	dcs_csum_info_list_fini(&list);

	for (i = 0; i < ARRAY_SIZE(info); i++)
		D_FREE(info[i].cs_csum);
}

#define MAP_MAX 10
#define	HOLES_TESTCASE(...) \
	holes_test_case(&(struct holes_test_args)__VA_ARGS__)
struct holes_test_args {
	uint32_t	 chunksize;
	uint32_t	 record_size;
	daos_recx_t	 map_recx[MAP_MAX];
	daos_recx_t	 req_recx[MAP_MAX];
	char		*expected_checksum_updates;
	char		*sgl_data;
};
static void
holes_test_case(struct holes_test_args *args)
{
	struct daos_csummer	*csummer;
	d_sg_list_t		 sgl;
	daos_iod_t		 iod = {0};
	daos_iom_t		 map = {0};
	struct dcs_iod_csums	*actual;
	uint64_t		 total_req_size = 0;
	int			 i;
	int			 rc = 0;

	int map_recx_nr = 0;
	int req_recx_nr = 0;

	while (map_recx_nr < MAP_MAX && args->map_recx[map_recx_nr].rx_nr > 0)
		map_recx_nr++;
	while (req_recx_nr < MAP_MAX && args->req_recx[req_recx_nr].rx_nr > 0)
		req_recx_nr++;

	/* Setup */
	daos_csummer_init(&csummer, &fake_algo, args->chunksize, 0);
	fake_update_buf = fake_update_buf_copy;
	memset(fake_update_buf_copy, 0, ARRAY_SIZE(fake_update_buf_copy));
	fake_get_size_result = fake_algo.cf_hash_len = 4;

	iod.iod_nr = req_recx_nr;
	iod.iod_recxs = args->req_recx;
	iod.iod_size = args->record_size;
	iod.iod_type = DAOS_IOD_ARRAY;
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));

	map.iom_recxs = args->map_recx;
	map.iom_nr = map_recx_nr;
	map.iom_size = args->record_size;
	map.iom_type = DAOS_IOD_ARRAY;

	dts_sgl_init_with_strings(&sgl, 1, args->sgl_data);
	/* sanity check */
	for (i = 0; i < req_recx_nr; i++)
		total_req_size += args->req_recx[i].rx_nr * args->record_size;
	if (total_req_size != daos_sgl_buf_size(&sgl))
		fail_msg("Test not setup correctly. total_req_size[%lu] != "
			 "daos_sgl_buf_size(&sgl)[%lu]",
			 total_req_size, daos_sgl_buf_size(&sgl));

	/* Act */
	rc = daos_csummer_calc_iods(csummer, &sgl, &iod, &map, 1, 0, NULL, 0,
				    &actual);
	assert_rc_equal(0, rc);

	/* Verify */
	assert_string_equal(args->expected_checksum_updates,
			    fake_update_buf_copy);

	d_sgl_fini(&sgl, true);
	daos_csummer_free_ic(csummer, &actual);
	daos_csummer_destroy(&csummer);
}

static void
holes_1(void **state)
{
	HOLES_TESTCASE({
		.chunksize = 1024 * 32,
		.record_size = 1,
		.map_recx = {
			{.rx_idx = 2, .rx_nr = 6},
			{.rx_idx = 10, .rx_nr = 6},
		},
		.req_recx = { {.rx_idx = 0, .rx_nr = 20} },
		/* '_' represents holes */
		.sgl_data = "__YYYYYY__ZZZZZZ___",
		.expected_checksum_updates = "akey|YYYYYY|ZZZZZZ|",
	});
}

static void
holes_2(void **state)
{
	HOLES_TESTCASE({
		.chunksize = 4,
		.record_size = 1,
		.map_recx = {
			{.rx_idx = 2, .rx_nr = 6},
			{.rx_idx = 10, .rx_nr = 6},
		},
		.req_recx = { {.rx_idx = 0, .rx_nr = 20} },
		/* '_' represents holes */
		.sgl_data = "__YYYYYY__ZZZZZZ___",
		.expected_checksum_updates = "akey|YY|YYYY|ZZ|ZZZZ|",
	});
}

static void
holes_3(void **state)
{
	HOLES_TESTCASE({
		.chunksize = 4,
		.record_size = 1,
		.map_recx = {
			{.rx_idx = 2, .rx_nr = 4},
			{.rx_idx = 10, .rx_nr = 6},
		},
		.req_recx = { {.rx_idx = 0, .rx_nr = 20} },
		/* '_' represents holes */
		.sgl_data = "__YYYY____ZZZZZZ___",
		.expected_checksum_updates = "akey|YY|YY|ZZ|ZZZZ|",
	});
}

static void
holes_4(void **state)
{
	HOLES_TESTCASE({
		.chunksize = 4,
		.record_size = 1,
		.map_recx = {
			{.rx_idx = 2, .rx_nr = 4},
			{.rx_idx = 20, .rx_nr = 6},
		},
		.req_recx = { {.rx_idx = 0, .rx_nr = 30} },
		/* '_' represents holes */
		.sgl_data = "__YYYY______________ZZZZZZ___",
		.expected_checksum_updates = "akey|YY|YY|ZZZZ|ZZ|",
	});
}
static void
holes_5(void **state)
{
	HOLES_TESTCASE({
		.chunksize = 1024,
		.record_size = 2,
		.map_recx = {
			{.rx_idx = 1, .rx_nr = 1},
			{.rx_idx = 3, .rx_nr = 1},
			{.rx_idx = 5, .rx_nr = 1},
			{.rx_idx = 7, .rx_nr = 1},
			{.rx_idx = 9, .rx_nr = 1},
			{.rx_idx = 11, .rx_nr = 1},
			{.rx_idx = 13, .rx_nr = 1},
			{.rx_idx = 15, .rx_nr = 1},
			{.rx_idx = 17, .rx_nr = 1},
			{.rx_idx = 19, .rx_nr = 1},
		},
		.req_recx = { {.rx_idx = 0, .rx_nr = 30} },
		.sgl_data =
		"__AA__AA__AA__AA__AA__AA__AA__AA__AA__AA___________________",
		.expected_checksum_updates =
			 "akey|AA|AA|AA|AA|AA|AA|AA|AA|AA|AA|",
	});
}

/*
 * -----------------------------------------------------------------------------
 * Test checksum comparison function
 * -----------------------------------------------------------------------------
 */

#define	setup_buf_for_test(csum, csum_buf) \
	ci_set(&(csum), csum_buf, sizeof(csum_buf), 1, \
		sizeof(csum_buf), 1024, FAKE_CSUM_TYPE)

static void
simple_test_compare_checksums(void **state)
{
	struct daos_csummer	*csummer;
	uint8_t			 csum_buf[] = "checksum";
	uint8_t			 csum_buf_same[] = "checksum";
	/* same len, different value */
	uint8_t			 csum_buf_dif[] = "corruptd";
	/* mostly the same, dif len */
	uint8_t			 csum_buf_dif_len[] = "checksumm";
	uint8_t			 csum_buf_dif_len2[] = "checksu";
	struct dcs_csum_info	 one;
	struct dcs_csum_info	 two;

	daos_csummer_init(&csummer, &fake_algo, 1024, 0);

	setup_buf_for_test(one, csum_buf);
	setup_buf_for_test(two, csum_buf_same);
	assert_true(daos_csummer_compare_csum_info(csummer, &one, &two));

	setup_buf_for_test(two, csum_buf_dif);
	assert_false(daos_csummer_compare_csum_info(csummer, &one, &two));

	setup_buf_for_test(two, csum_buf_dif_len);
	assert_false(daos_csummer_compare_csum_info(csummer, &one, &two));

	setup_buf_for_test(two, csum_buf_dif_len2);
	assert_false(daos_csummer_compare_csum_info(csummer, &one, &two));

	daos_csummer_destroy(&csummer);
}

static void
test_compare_checksums(void **state)
{
	struct daos_csummer	*csummer;
	struct dcs_iod_csums	*one;
	struct dcs_iod_csums	*two;
	d_sg_list_t		 sgl;
	daos_recx_t		 recx;
	daos_iod_t		 iod = {0};
	int			 rc = 0;

	fake_get_size_result = 4;

	daos_csummer_init(&csummer, &fake_algo, 4, 0);
	dts_sgl_init_with_strings(&sgl, 1, "0123456789");

	recx.rx_idx = 0;
	recx.rx_nr = daos_sgl_buf_size(&sgl);

	fake_update_bytes_seen = 0;

	recx.rx_idx = 0;
	recx.rx_nr = daos_sgl_buf_size(&sgl);
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	iod.iod_size = 1;
	iod.iod_type = DAOS_IOD_ARRAY;

	rc = daos_csummer_calc_iods(csummer, &sgl, &iod, NULL, 1, 0, NULL, 0,
				    &one);
	assert_rc_equal(0, rc);
	rc = daos_csummer_calc_iods(csummer, &sgl, &iod, NULL, 1, 0, NULL, 0,
				    &two);
	assert_rc_equal(0, rc);

	assert_true(daos_csummer_compare_csum_info(csummer, one->ic_data,
						   two->ic_data));

	d_sgl_fini(&sgl, true);
	daos_csummer_free_ic(csummer, &one);
	daos_csummer_free_ic(csummer, &two);
	daos_csummer_destroy(&csummer);
}

static void
test_get_iod_csum_allocation_size(void **state)
{
	struct daos_csummer	*csummer;
	int			 chunksize = 4;
	daos_iod_t		 iods[2] = {0};
	daos_recx_t		 recxs[2] = {0};
	uint32_t		 csum_size = 4;

	fake_algo.cf_hash_len = csum_size;
	fake_get_size_result = csum_size;
	daos_csummer_init(&csummer, &fake_algo, chunksize, 0);

	iods[0].iod_nr = 1;
	iods[0].iod_recxs = recxs;
	iods[0].iod_size = 1;
	iods[0].iod_type = DAOS_IOD_ARRAY;

	recxs[0].rx_idx = 0;
	recxs[0].rx_nr = chunksize;
	assert_int_equal(sizeof(struct dcs_iod_csums) +
			 csum_size + /* akey csum */
			 sizeof(struct dcs_csum_info) + /* 1 data csum info */
			 csum_size, /* 1 data csum */
			 daos_csummer_allocation_size(csummer, &iods[0],
						      1, 0, NULL));

	recxs[0].rx_idx = 0;
	recxs[0].rx_nr = chunksize + 1; /* two checksums now */
	assert_int_equal(sizeof(struct dcs_iod_csums) +
				 csum_size + /* akey csum */
				 sizeof(struct dcs_csum_info) +
			 csum_size * 2,
			 daos_csummer_allocation_size(csummer, &iods[0],
						      1, 0, NULL));

	iods[0].iod_nr = 2;
	recxs[1].rx_idx = 0;
	recxs[1].rx_nr = chunksize;
	assert_int_equal(sizeof(struct dcs_iod_csums) +
				 csum_size + /* akey csum */
			 sizeof(struct dcs_csum_info) * 2 +
			 csum_size * 3,
			 daos_csummer_allocation_size(csummer, &iods[0],
						      1, 0, NULL));
	iods[0].iod_nr = 1;

	iods[1].iod_nr = 1;
	iods[1].iod_recxs = recxs + 1;
	iods[1].iod_size = 1;
	iods[1].iod_type = DAOS_IOD_ARRAY;
	assert_int_equal(sizeof(struct dcs_iod_csums) * 2 +
			 csum_size * 2 + /* akey csum (1 for each iod_csum */
			 sizeof(struct dcs_csum_info) * 2 +
			 csum_size * 3,
			 daos_csummer_allocation_size(csummer, &iods[0],
						      2, 0, NULL));

	/* skip data */
	assert_int_equal(sizeof(struct dcs_iod_csums) * 2 +
			 csum_size * 2, /* akey csum (1 for each iod_csum */
			 daos_csummer_allocation_size(csummer, &iods[0], 2,
						      true, NULL));

	/* Clean up */
	daos_csummer_destroy(&csummer);
}

static void
print_checksum(struct daos_csummer *csummer, struct dcs_csum_info *csum)
{
	uint32_t i, c;

	print_message("Type: %d\n", csum->cs_type);
	print_message("Name: %s\n", daos_csummer_get_name(csummer));
	print_message("Count: %d\n", csum->cs_nr);
	print_message("Len: %d\n", csum->cs_len);
	print_message("Buf Len: %d\n", csum->cs_buf_len);
	print_message("Chunk: %d\n", csum->cs_chunksize);
	for (c = 0; c < csum->cs_nr; c++) {
		uint8_t *csum_bytes = ci_idx2csum(csum, c);

		print_message("Checksum[%02d]: 0x", c);
		for (i = 0; i < csum->cs_len; i++)
			print_message("%02x", csum_bytes[i]);
		print_message("\n");
	}
	print_message("\n");
	fflush(stdout);
}

/*
 * -----------------------------------------------------------------------------
 * Loop through and verify all the different checksum algorithms supporting
 * -----------------------------------------------------------------------------
 */
static void
test_all_algo_basic(void **state)
{
	d_sg_list_t		 sgl;
	daos_recx_t		 recxs;
	enum DAOS_HASH_TYPE	 type;
	struct daos_csummer	*csummer = NULL;
	struct dcs_iod_csums	*csums1 = NULL;
	struct dcs_iod_csums	*csums2 = NULL;
	int			 csum_lens[HASH_TYPE_END];
	daos_iod_t		 iod = {0};
	int			 rc;

	/* expected checksum lengths */
	csum_lens[HASH_TYPE_CRC16]	= 2;
	csum_lens[HASH_TYPE_CRC32]	= 4;
	csum_lens[HASH_TYPE_ADLER32]	= 4;
	csum_lens[HASH_TYPE_CRC64]	= 8;
	csum_lens[HASH_TYPE_SHA1]	= 20;
	csum_lens[HASH_TYPE_SHA256]	= 256 / 8;
	csum_lens[HASH_TYPE_SHA512]	= 512 / 8;

	dts_sgl_init_with_strings(&sgl, 1, "Data");

	recxs.rx_idx = 0;
	recxs.rx_nr = daos_sgl_buf_size(&sgl);

	for (type = HASH_TYPE_UNKNOWN + 1; type < HASH_TYPE_END; type++) {
		rc = daos_csummer_init(&csummer,
				       daos_mhash_type2algo(type), 128, 0);
		if (rc != 0)
			fail_msg("init failed for type: %d. " DF_RC,
				type, DP_RC(rc));

		d_iov_set(&iod.iod_name, "akey", sizeof("akey"));
		iod.iod_nr = 1;
		iod.iod_recxs = &recxs;
		iod.iod_size = 1;
		iod.iod_type = DAOS_IOD_ARRAY;

		rc = daos_csummer_calc_iods(csummer, &sgl, &iod, NULL, 1, 0,
					    NULL, 0, &csums1);
		assert_rc_equal(0, rc);
		assert_int_equal(csum_lens[type],
				 daos_csummer_get_csum_len(csummer));

		/* run it a second time to make sure that checksums
		 * are calculated the same and the reset, update, finish flow
		 * works
		 */
		rc = daos_csummer_calc_iods(csummer, &sgl, &iod, NULL, 1, 0,
					    NULL, 0, &csums2);

		assert_rc_equal(0, rc);
		assert_int_equal(csum_lens[type],
				 daos_csummer_get_csum_len(csummer));

		assert_memory_equal(csums1->ic_akey.cs_csum,
				    csums2->ic_akey.cs_csum,
				    csums1->ic_akey.cs_len);
		assert_memory_equal(csums1->ic_data[0].cs_csum,
				    csums2->ic_data[0].cs_csum,
				    csums1->ic_data[0].cs_len);

		if (verbose) {
			print_checksum(csummer, &csums1->ic_akey);
			print_checksum(csummer, &csums1->ic_data[0]);
		}

		daos_csummer_free_ic(csummer, &csums1);
		daos_csummer_free_ic(csummer, &csums2);
		daos_csummer_destroy(&csummer);
	}

	d_sgl_fini(&sgl, true);
}

static void
test_do_not_need_to_call(void **state)
{
	enum DAOS_HASH_TYPE	 type;
	struct daos_csummer	*csummer = NULL;
	const daos_size_t	 buffer_len = 512;
	uint8_t			 buffer[512];
	int			 i;
	int			 rc;

	for (type = HASH_TYPE_UNKNOWN + 1; type < HASH_TYPE_END; type++) {
		memset(buffer, 0, buffer_len);
		rc = daos_csummer_init(&csummer,
				       daos_mhash_type2algo(type), 128, 0);
		assert_rc_equal(0, rc);

		daos_csummer_set_buffer(csummer, buffer, buffer_len);
		rc = daos_csummer_reset(csummer);
		assert_int_equal(0, rc);

		rc = daos_csummer_finish(csummer);
		assert_int_equal(0, rc);

		/* checksum buffer should have been untouched */
		for (i = 0; i < buffer_len; i++) {
			if (buffer[i] != 0)
				fail_msg("checksum type %d, buffer[%d] (%d) "
					 "!= 0", type, i, buffer[i] != 0);
		}
		daos_csummer_destroy(&csummer);
	}
}
static void
test_repeat_updates(void **state)
{
	enum DAOS_HASH_TYPE	 type;
	struct daos_csummer	*csummer = NULL;
	const daos_size_t	 data_buf_len = 512;
	const daos_size_t	 update_chunks[] = {32, 64, 128, 256};
	uint8_t			 data_buf[data_buf_len];
	/* sha512 is largest */
	const daos_size_t	 csum_buf_len = 512 / 8;
	uint8_t			 csum_buf_1[csum_buf_len];
	uint8_t			 csum_buf_2[csum_buf_len];
	int			 i, c;
	int			 rc;

	memset(data_buf, 0xA, data_buf_len);

	for (type = HASH_TYPE_UNKNOWN + 1; type < HASH_TYPE_END; type++) {
		struct hash_ft *ft = daos_mhash_type2algo(type);

		rc = daos_csummer_init(&csummer, ft, CSUM_NO_CHUNK, 0);
		assert_rc_equal(0, rc);
		print_message("Checksum : %s\n",
			      daos_csummer_get_name(csummer));

		/* Calculate checksum for whole buffer */
		memset(csum_buf_1, 0, csum_buf_len);
		daos_csummer_set_buffer(csummer, csum_buf_1, csum_buf_len);
		rc = daos_csummer_reset(csummer);
		assert_int_equal(0, rc);
		rc = daos_csummer_update(csummer, data_buf, data_buf_len);
		assert_int_equal(0, rc);
		rc = daos_csummer_finish(csummer);
		assert_int_equal(0, rc);

		/* calculate checksum for buffer in incremental updates */
		for (c = 0; c < ARRAY_SIZE(update_chunks); c++) {
			daos_size_t chunk = update_chunks[c];

			memset(csum_buf_2, 0, csum_buf_len);

			daos_csummer_set_buffer(csummer, csum_buf_2,
						csum_buf_len);
			rc = daos_csummer_reset(csummer);
			assert_int_equal(0, rc);

			for (i = 0; i < data_buf_len / chunk; i++) {
				daos_csummer_update(csummer,
						    data_buf + i * chunk,
						    chunk);
			}

			rc = daos_csummer_finish(csummer);
			assert_int_equal(0, rc);

			for (i = 0; i < csum_buf_len; i++) {
				if (csum_buf_1[i] != csum_buf_2[i])
					fail_msg("checksum type %s, buffer[%d] "
						 "(%d) != (%d)",
						 daos_csummer_get_name(csummer),
						 i,
						 csum_buf_1[i], csum_buf_2[i]);
			}

		}
		daos_csummer_destroy(&csummer);
	}
}

/*
 * -----------------------------------------------------------------------------
 * Test some helper functions for indexing checksums within a daos_csum_info
 * -----------------------------------------------------------------------------
 */
static void
test_helper_functions(void **state)
{
	struct dcs_csum_info	dcb;
	uint16_t		csum = 0xa;
	uint32_t		two_csums = 0x12345678;

	dcb.cs_len = 2;
	dcb.cs_chunksize = 4;
	dcb.cs_buf_len = 4;
	dcb.cs_nr = 2;
	dcb.cs_csum = calloc(dcb.cs_buf_len, 1);
	dcb.cs_csum[0] = 1;
	dcb.cs_csum[1] = 1;
	dcb.cs_csum[2] = 2;
	dcb.cs_csum[3] = 2;

	assert_int_equal(0x0101, *(uint16_t *) ci_idx2csum(&dcb, 0));
	assert_int_equal(0x0202, *(uint16_t *) ci_idx2csum(&dcb, 1));
	assert_int_equal(NULL, ci_idx2csum(&dcb, 2));


	assert_int_equal(0, ci_off2idx(&dcb, 0));
	assert_int_equal(1, ci_off2idx(&dcb, 4));
	assert_int_equal(1, ci_off2idx(&dcb, 5));

	assert_int_equal(0x0101, *(uint16_t *) ci_off2csum(&dcb, 0));
	assert_int_equal(0x0202, *(uint16_t *) ci_off2csum(&dcb, 4));


	/* try some larger values */
	dcb.cs_chunksize = 1024 * 16; /* 16K */
	assert_int_equal(0, ci_off2idx(&dcb, 1024 * 16 - 1));
	assert_int_equal(1, ci_off2idx(&dcb, 1024 * 16));
	assert_int_equal(1024, ci_off2idx(&dcb, 1024 * 1024 * 16));

	/* insert csum into dcb */
	ci_insert(&dcb, 0, (uint8_t *) &csum, sizeof(csum));
	assert_int_equal(0xa, *(uint16_t *) ci_idx2csum(&dcb, 0));
	csum = 0xb;
	ci_insert(&dcb, 1, (uint8_t *) &csum, sizeof(csum));
	assert_int_equal(0xb, *(uint16_t *) ci_idx2csum(&dcb, 1));
	ci_insert(&dcb, 0, (uint8_t *) &two_csums, sizeof(two_csums));
	assert_int_equal(0x5678, *(uint16_t *) ci_idx2csum(&dcb, 0));
	assert_int_equal(0x1234, *(uint16_t *) ci_idx2csum(&dcb, 1));

	free(dcb.cs_csum);
}

static void
test_csum_chunk_count(void **state)
{
	/* chunksize, lo_idx, hi_idx, rec_size */
	assert_int_equal(1, csum_chunk_count(1, 0, 0, 1));
	assert_int_equal(1, csum_chunk_count(2, 0, 1, 1));
	assert_int_equal(2, csum_chunk_count(2, 1, 2, 1));
	assert_int_equal(2, csum_chunk_count(2, 1, 3, 1));
	assert_int_equal(3, csum_chunk_count(2, 1, 5, 1));
	assert_int_equal(0xffffffff,
			 csum_chunk_count(1024 * 32, 0, UINT64_MAX, 8));
}

static void
test_recx_calc_chunks(void **state)
{
	uint32_t	chunksize = 4;
	uint32_t	rec_size = 1;
	daos_recx_t	recx = {0};

	assert_int_equal(0, daos_recx_calc_chunks(recx, rec_size, chunksize));

	recx.rx_nr = 1;
	assert_int_equal(1, daos_recx_calc_chunks(recx, rec_size, chunksize));

	rec_size = 2;
	assert_int_equal(1, daos_recx_calc_chunks(recx, rec_size, chunksize));

	chunksize = 4;
	recx.rx_idx = 1;
	recx.rx_nr = 16;
	rec_size = 1;
	/* chunks = 0-3, 4-7, 8-11, 12-16, 16-20 */
	assert_int_equal(5, daos_recx_calc_chunks(recx, rec_size, chunksize));
}

static void
test_daos_align_to_floor_of_chunk(void **state)
{
	assert_int_equal(0, csum_chunk_align_floor(0, 16));
	assert_int_equal(0, csum_chunk_align_floor(8, 16));
	assert_int_equal(16, csum_chunk_align_floor(16, 16));
	assert_int_equal(16, csum_chunk_align_floor(17, 16));
	assert_int_equal(16, csum_chunk_align_floor(30, 16));
	assert_int_equal(16, csum_chunk_align_floor(31, 16));
	assert_int_equal(32, csum_chunk_align_floor(32, 16));
}

/*
 * ----------------------------------------------------------------------
 * Test cases for getting chunk boundaries provided a recx and chunk idx
 * ----------------------------------------------------------------------
 */
struct daos_recx_get_chunk_testcase_args {
	uint64_t cs; /* chunk size */
	uint64_t rb; /* record bytes */
	daos_recx_t recx;
};

#define DAOS_RECX_GET_CHUNK_TESTCASE(idx, start, len, ...) \
	daos_recx_get_chunk_testcase(__FILE__, __LINE__, idx, start, len, \
		(struct daos_recx_get_chunk_testcase_args)__VA_ARGS__)

void
daos_recx_get_chunk_testcase(char *filename, int line,
			     uint64_t idx, uint64_t expected_start,
			     uint64_t expected_len,
			     struct daos_recx_get_chunk_testcase_args args)
{
	struct daos_csum_range chunk;

	chunk = csum_recx_chunkidx2range(&args.recx, args.rb, args.cs, idx);

	uint64_t result_start = chunk.dcr_lo;
	uint64_t result_len = chunk.dcr_nr;

	if (expected_start != result_start)
		fail_msg("(%s:%d) Expected start %lu but found %lu. ",
			 filename, line, expected_start, result_start);
	if (expected_len != result_len)
		fail_msg("(%s:%d) Expected length %lu but found %lu. ",
			 filename, line, expected_len, result_len);
}

static void
daos_recx_get_chunk_tests(void **state)
{
	/*  extent is 0->9, with chunk = 2
	 *  - all chunks will be full chunk
	 *  - chunk 0 will start at 0
	 *  - chunk 1 will start at 2
	 *  - chunk 2 will start at 4
	 *  - and so on ...
	 *  - chunk 5 will exceed extent so will return len 0
	 */
	DAOS_RECX_GET_CHUNK_TESTCASE(0, 0, 2,
				     {.cs = 2, .rb = 1, .recx = {0, 10} });
	DAOS_RECX_GET_CHUNK_TESTCASE(1, 2, 2,
				     {.cs = 2, .rb = 1, .recx = {0, 10} });
	DAOS_RECX_GET_CHUNK_TESTCASE(2, 4, 2,
				     {.cs = 2, .rb = 1, .recx = {0, 10} });
	DAOS_RECX_GET_CHUNK_TESTCASE(4, 8, 2,
				     {.cs = 2, .rb = 1, .recx = {0, 10} });
	DAOS_RECX_GET_CHUNK_TESTCASE(5, 0, 0,
				     {.cs = 2, .rb = 1, .recx = {0, 10} });
	DAOS_RECX_GET_CHUNK_TESTCASE(0, 1, 1,
				     {.cs = 2, .rb = 1, .recx = {1, 2} });
	DAOS_RECX_GET_CHUNK_TESTCASE(1, 2, 1,
				     {.cs = 2, .rb = 1, .recx = {1, 2} });

	/* partial extent is 3-7, with chunk = 8. chunk is the whole extent */
	DAOS_RECX_GET_CHUNK_TESTCASE(0, 3, 5,
				     {.cs = 8, .rb = 1, .recx = {3, 5} });
	/* partial extent is 3-6, with chunk = 8. chunk is the whole extent */
	DAOS_RECX_GET_CHUNK_TESTCASE(0, 3, 4,
				     {.cs = 8, .rb = 1, .recx = {3, 4} });

	/* More testing ... */
	DAOS_RECX_GET_CHUNK_TESTCASE(0, 2, 6,
				     {.cs = 8, .rb = 1, .recx = {2, 50} });
	DAOS_RECX_GET_CHUNK_TESTCASE(1, 8, 8,
				     {.cs = 8, .rb = 1, .recx = {2, 50} });
	DAOS_RECX_GET_CHUNK_TESTCASE(5, 40, 8,
				     {.cs = 8, .rb = 1, .recx = {2, 50} });
	DAOS_RECX_GET_CHUNK_TESTCASE(6, 48, 4,
				     {.cs = 8, .rb = 1, .recx = {2, 50} });
	DAOS_RECX_GET_CHUNK_TESTCASE(1, 2, 2,
				     { .cs = 8, .rb = 4, .recx = {0, 10} });
	DAOS_RECX_GET_CHUNK_TESTCASE(1, 2, 1,
				     {.cs = 2, .rb = 1, .recx = {0, 3} });
	DAOS_RECX_GET_CHUNK_TESTCASE(0, 4, 4,
				     {.cs = 4, .rb = 1, .recx = {4, 4} });

	DAOS_RECX_GET_CHUNK_TESTCASE(0, 16, 16,
				     {.cs = 16, .rb = 1, .recx = {16, 16} });

	/* Max recx index  */
	DAOS_RECX_GET_CHUNK_TESTCASE(0, UINT64_MAX, 1,
				     {
					     .cs = 32 * 1024,
					     .rb = 6, .recx = {UINT64_MAX, 1}
				     });
}

static void
test_align_boundaries(void **state)
{
	struct daos_csum_range result;

	result = csum_align_boundaries(
		0, /* lo */
		0 /* hi */,
		0 /* lo boundary */,
		7 /* hi boundary */,
		1 /* rec size */,
		8 /* chunk size */);
	assert_int_equal(0, result.dcr_lo);
	assert_int_equal(7, result.dcr_hi);
	assert_int_equal(8, result.dcr_nr);

	result = csum_align_boundaries(
		1, /* lo */
		0 /* hi */,
		0 /* lo boundary */,
		7 /* hi boundary */,
		1 /* rec size */,
		8 /* chunk size */);
	assert_int_equal(0, result.dcr_lo);
	assert_int_equal(7, result.dcr_hi);

	result = csum_align_boundaries(
		1, /* lo */
		6 /* hi */,
		0 /* lo boundary */,
		7 /* hi boundary */,
		1 /* rec size */,
		8 /* chunk size */);
	assert_int_equal(0, result.dcr_lo);
	assert_int_equal(7, result.dcr_hi);

	result = csum_align_boundaries(
		1, /* lo */
		6 /* hi */,
		0 /* lo boundary */,
		8 /* hi boundary */,
		1 /* rec size */,
		8 /* chunk size */);
	assert_int_equal(0, result.dcr_lo);
	assert_int_equal(7, result.dcr_hi);

	result = csum_align_boundaries(
		1, /* lo */
		8 /* hi */,
		0 /* lo boundary */,
		10 /* hi boundary */,
		1 /* rec size */,
		8 /* chunk size */);
	assert_int_equal(0, result.dcr_lo);
	assert_int_equal(10, result.dcr_hi);

	result = csum_align_boundaries(
		16, /* lo */
		96 /* hi */,
		0 /* lo boundary */,
		100 /* hi boundary */,
		2 /* rec size */,
		8 /* chunk size */);
	assert_int_equal(16, result.dcr_lo);
	assert_int_equal(99, result.dcr_hi);

	result = csum_align_boundaries(
		UINT64_MAX, /* lo */
		UINT64_MAX /* hi */,
		0 /* lo boundary */,
		UINT64_MAX /* hi boundary */,
		8 /* rec size */,
		1024 * 32 /* chunk size */);
	assert_int_equal(UINT64_MAX - 0xFFF, result.dcr_lo);
	assert_int_equal(UINT64_MAX, result.dcr_hi);
	assert_int_equal(1024 * 4 - 1, result.dcr_nr);

	/* result is 0 if lo/hi is outside of boundary */
	result = csum_align_boundaries(
		10, /* lo */
		10 /* hi */,
		50 /* lo boundary */,
		100 /* hi boundary */,
		1 /* rec size */,
		8 /* chunk size */);
	assert_int_equal(0, result.dcr_lo);
	assert_int_equal(0, result.dcr_hi);
	assert_int_equal(0, result.dcr_nr);
	result = csum_align_boundaries(
		10, /* lo */
		1000 /* hi */,
		5 /* lo boundary */,
		100 /* hi boundary */,
		1 /* rec size */,
		8 /* chunk size */);
	assert_int_equal(0, result.dcr_lo);
	assert_int_equal(0, result.dcr_hi);
	assert_int_equal(0, result.dcr_nr);
}

static void
test_align_to_chunk(void **state)
{
	struct daos_csum_range result;

	result = csum_recidx2range(
		8 /* chunksize */,
		0 /* record index */,
		0 /* lo boundary */,
		8 /* hi boundary */,
		1 /* rec size */);
	assert_int_equal(0, result.dcr_lo);
	assert_int_equal(7, result.dcr_hi);
	assert_int_equal(8, result.dcr_nr);

	result = csum_recidx2range(
		8 /* chunksize */,
		1 /* record index */,
		0 /* lo boundary */,
		8 /* hi boundary */,
		1 /* rec size */);
	assert_int_equal(0, result.dcr_lo);
	assert_int_equal(7, result.dcr_hi);

	result = csum_recidx2range(
		8 /* chunksize */,
		7 /* record index */,
		0 /* lo boundary */,
		8 /* hi boundary */,
		1 /* rec size */);
	assert_int_equal(0, result.dcr_lo);
	assert_int_equal(7, result.dcr_hi);

	result = csum_recidx2range(
		8 /* chunksize */,
		8 /* record index */,
		0 /* lo boundary */,
		8 /* hi boundary */,
		1 /* rec size */);
	assert_int_equal(8, result.dcr_lo);
	assert_int_equal(8, result.dcr_hi);

	result = csum_recidx2range(
		8 /* chunksize */,
		8 /* record index */,
		0 /* lo boundary */,
		8 /* hi boundary */,
		1 /* rec size */);
	assert_int_equal(8, result.dcr_lo);
	assert_int_equal(8, result.dcr_hi);

	result = csum_recidx2range(
		1024 * 32 /* chunksize */,
		UINT64_MAX /* record index */,
		0 /* lo boundary */,
		UINT64_MAX /* hi boundary */,
		8 /* rec size */);
	assert_int_equal(UINT64_MAX - 0xFFF, result.dcr_lo);
	assert_int_equal(UINT64_MAX, result.dcr_hi);
	assert_int_equal(1024 * 4, result.dcr_nr);
}

/*
 * -----------------------------------------------------------------------------
 * Test some DAOS Container Property Knowledge
 * -----------------------------------------------------------------------------
 */
static void
test_container_prop_to_csum_type(void **state)
{
	assert_int_equal(HASH_TYPE_CRC16,
			 daos_contprop2hashtype(DAOS_PROP_CO_CSUM_CRC16));
	assert_int_equal(HASH_TYPE_CRC32,
			 daos_contprop2hashtype(DAOS_PROP_CO_CSUM_CRC32));
	assert_int_equal(HASH_TYPE_ADLER32,
			 daos_contprop2hashtype(DAOS_PROP_CO_CSUM_ADLER32));
	assert_int_equal(HASH_TYPE_CRC64,
			 daos_contprop2hashtype(DAOS_PROP_CO_CSUM_CRC64));
	assert_int_equal(HASH_TYPE_SHA1,
			 daos_contprop2hashtype(DAOS_PROP_CO_CSUM_SHA1));
	assert_int_equal(HASH_TYPE_SHA256,
			 daos_contprop2hashtype(DAOS_PROP_CO_CSUM_SHA256));
	assert_int_equal(HASH_TYPE_SHA512,
			 daos_contprop2hashtype(DAOS_PROP_CO_CSUM_SHA512));
}

static void
test_is_valid_csum(void **state)
{
	assert_true(daos_cont_csum_prop_is_valid(DAOS_PROP_CO_CSUM_OFF));
	assert_true(daos_cont_csum_prop_is_valid(DAOS_PROP_CO_CSUM_CRC16));
	assert_true(daos_cont_csum_prop_is_valid(DAOS_PROP_CO_CSUM_CRC32));
	assert_true(daos_cont_csum_prop_is_valid(DAOS_PROP_CO_CSUM_ADLER32));
	assert_true(daos_cont_csum_prop_is_valid(DAOS_PROP_CO_CSUM_CRC64));
	assert_true(daos_cont_csum_prop_is_valid(DAOS_PROP_CO_CSUM_SHA1));
	assert_true(daos_cont_csum_prop_is_valid(DAOS_PROP_CO_CSUM_SHA256));
	assert_true(daos_cont_csum_prop_is_valid(DAOS_PROP_CO_CSUM_SHA512));

	/* Not supported yet */
	assert_false(daos_cont_csum_prop_is_valid(99));
}

static void
test_is_csum_enabled(void **state)
{
	assert_true(daos_cont_csum_prop_is_enabled(DAOS_PROP_CO_CSUM_CRC16));
	assert_true(daos_cont_csum_prop_is_enabled(DAOS_PROP_CO_CSUM_CRC32));
	assert_true(daos_cont_csum_prop_is_enabled(DAOS_PROP_CO_CSUM_ADLER32));
	assert_true(daos_cont_csum_prop_is_enabled(DAOS_PROP_CO_CSUM_CRC64));
	assert_true(daos_cont_csum_prop_is_enabled(DAOS_PROP_CO_CSUM_SHA1));
	assert_true(daos_cont_csum_prop_is_enabled(DAOS_PROP_CO_CSUM_SHA256));
	assert_true(daos_cont_csum_prop_is_enabled(DAOS_PROP_CO_CSUM_SHA512));

	/* Not supported yet */
	assert_false(daos_cont_csum_prop_is_enabled(DAOS_PROP_CO_CSUM_OFF));
	assert_false(daos_cont_csum_prop_is_enabled(9999));
}

static void
simple_sv(void **state)
{
	struct daos_csummer	*csummer;
	d_sg_list_t		 sgl;
	struct dcs_iod_csums	*actual;
	daos_iod_t		 iod = {0};
	int			 rc;

	fake_get_size_result = 4;
	daos_csummer_init(&csummer, &fake_algo, 16, 0);
	fake_algo.cf_get_size = fake_get_size;

	dts_sgl_init_with_strings(&sgl, 1, "abcdef");

	iod.iod_nr = 1;
	iod.iod_recxs = NULL;
	iod.iod_size = daos_sgl_buf_size(&sgl);
	iod.iod_type = DAOS_IOD_SINGLE;

	rc = daos_csummer_calc_iods(csummer, &sgl, &iod, NULL, 1, 0, NULL, 0,
				    &actual);

	assert_rc_equal(0, rc);

	assert_int_equal(fake_get_size_result, actual->ic_data->cs_buf_len);
	assert_int_equal(1, actual->ic_data->cs_nr);
	assert_int_equal(fake_get_size_result, actual->ic_data->cs_len);

	assert_int_equal(1, *ci_idx2csum(actual->ic_data, 0));

	daos_csummer_free_ic(csummer, &actual);
	d_sgl_fini(&sgl, true);
	daos_csummer_destroy(&csummer);
}

static void
test_compare_sv_checksums(void **state)
{
	struct daos_csummer	*csummer;
	struct dcs_iod_csums	*one;
	struct dcs_iod_csums	*two;
	d_sg_list_t		 sgl;
	daos_iod_t		 iod = {0};
	int			 rc = 0;

	fake_get_size_result = 4;

	daos_csummer_init(&csummer, &fake_algo, 4, 0);
	dts_sgl_init_with_strings(&sgl, 1, "0123456789");

	fake_update_bytes_seen = 0;

	iod.iod_nr = 1;
	iod.iod_recxs = NULL;
	iod.iod_size = daos_sgl_buf_size(&sgl);
	iod.iod_type = DAOS_IOD_SINGLE;

	rc = daos_csummer_calc_iods(csummer, &sgl, &iod, NULL, 1, 0, NULL, 0,
				    &one);
	assert_rc_equal(0, rc);
	rc = daos_csummer_calc_iods(csummer, &sgl, &iod, NULL, 1, 0, NULL, 0,
				    &two);
	assert_rc_equal(0, rc);

	assert_true(daos_csummer_compare_csum_info(csummer, one->ic_data,
						   two->ic_data));

	d_sgl_fini(&sgl, true);
	daos_csummer_free_ic(csummer, &one);
	daos_csummer_free_ic(csummer, &two);
	daos_csummer_destroy(&csummer);
}

static void
test_verify_sv_data(void **state)
{
	struct daos_csummer	*csummer;
	daos_iod_t		 iod = {0};
	d_sg_list_t		 sgl = {0};
	daos_size_t		 sgl_buf_half;
	int			 rc;
	struct dcs_iod_csums	*iod_csums = NULL;

	daos_csummer_init_with_type(&csummer, HASH_TYPE_CRC64, 1024 * 1024, 0);
	dts_sgl_init_with_strings(&sgl, 1, "0123456789");

	iod.iod_size = daos_sgl_buf_size(&sgl);
	iod.iod_nr = 1;
	iod.iod_recxs = NULL;
	iod.iod_type = DAOS_IOD_SINGLE;

	/* Checksum not set in iod_csums but csummer is set so should error */
	rc = daos_csummer_verify_iod(csummer, &iod, &sgl, iod_csums, NULL, 0,
				     NULL);
	assert_rc_equal(-DER_INVAL, rc);

	rc = daos_csummer_calc_iods(csummer, &sgl, &iod, NULL, 1, 0, NULL, 0,
				    &iod_csums);
	assert_rc_equal(0, rc);

	rc = daos_csummer_verify_iod(csummer, &iod, &sgl, iod_csums, NULL, 0,
				     NULL);
	assert_rc_equal(0, rc);

	((char *)sgl.sg_iovs[0].iov_buf)[0]++; /* Corrupt the data */
	rc = daos_csummer_verify_iod(csummer, &iod, &sgl, iod_csums, NULL, 0,
				     NULL);
	assert_rc_equal(-DER_CSUM, rc);

	((char *)sgl.sg_iovs[0].iov_buf)[0]--; /* Un-corrupt the data */
	/* Corrupt data elsewhere*/
	sgl_buf_half = daos_sgl_buf_size(&sgl) / 2;
	((char *)sgl.sg_iovs[0].iov_buf)[sgl_buf_half + 1]++;
	rc = daos_csummer_verify_iod(csummer, &iod, &sgl, iod_csums, NULL, 0,
				     NULL);
	assert_rc_equal(-DER_CSUM, rc);

	/* Clean up */
	daos_csummer_free_ic(csummer, &iod_csums);
	daos_csummer_destroy(&csummer);
	d_sgl_fini(&sgl, true);
}

static void
test_akey_csum(void **state)
{
	struct daos_csummer	*csummer;
	d_sg_list_t		 sgl;
	daos_recx_t		 recx;
	struct dcs_iod_csums	*actual;
	daos_iod_t		 iod = {0};
	int			 rc = 0;

	fake_get_size_result = 4;
	daos_csummer_init(&csummer, &fake_algo, 16, 0);
	fake_algo.cf_get_size = fake_get_size;

	dts_sgl_init_with_strings(&sgl, 1, "abcdef");

	recx.rx_idx = 0;
	recx.rx_nr = daos_sgl_buf_size(&sgl);
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	iod.iod_size = 1;
	iod.iod_type = DAOS_IOD_ARRAY;
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));

	rc = daos_csummer_calc_iods(csummer, &sgl, &iod, NULL, 1, 0, NULL, 0,
				    &actual);
	assert_rc_equal(0, rc);

	assert_int_equal(fake_get_size_result, actual->ic_akey.cs_buf_len);
	assert_int_equal(1, actual->ic_akey.cs_nr);
	assert_int_equal(fake_get_size_result, actual->ic_akey.cs_len);
	assert_int_equal((uint32_t)CSUM_NO_CHUNK, actual->ic_akey.cs_chunksize);

	assert_int_equal(1, *ic_idx2csum(actual, 0, 0));

	daos_csummer_free_ic(csummer, &actual);
	d_sgl_fini(&sgl, true);
	daos_csummer_destroy(&csummer);
}

static void
test_calc_rec_chunksize(void **state)
{
	/*	expected,	default_chunksize,	rec_size */
	assert_int_equal(1, csum_record_chunksize(1, 1));
	assert_int_equal(2, csum_record_chunksize(2, 2));
	assert_int_equal(2, csum_record_chunksize(2, 1));
	assert_int_equal(2, csum_record_chunksize(3, 2));
	assert_int_equal(3, csum_record_chunksize(4, 3));
	assert_int_equal(10, csum_record_chunksize(4, 10));
	assert_int_equal(18, csum_record_chunksize(20, 3));
	assert_int_equal(UINT_MAX,
		csum_record_chunksize(UINT_MAX - 1, UINT_MAX));
	assert_int_equal(UINT_MAX - 1,
		csum_record_chunksize(UINT_MAX, UINT_MAX - 1));
}

static void
test_formatter(void **state)
{
	uint64_t csum_buf = 1234567890123456789;
	char result[1024];
	struct dcs_csum_info ci = {
		.cs_csum = (uint8_t *)&csum_buf,
		.cs_nr = 1,
		.cs_chunksize = 1024,
		.cs_buf_len = sizeof(csum_buf),
		.cs_len = sizeof(csum_buf)
		};

	sprintf(result, DF_CI, DP_CI(ci));
	assert_string_equal("{nr: 1, len: 8, first_csum: 1234567890123456789, "
		     "csum_buf_len: 8}",
			    result);
}

static void
test_ci_serialize(void **state)
{
	const size_t		iov_buf_len = 64;
	const uint32_t		csum_size = 8;
	uint64_t		csum_buf = 0x1234567890ABCDEF;
	uint8_t			iov_buf[iov_buf_len];
	d_iov_t			iov = {0};
	int			rc;
	struct dcs_csum_info	*actual = NULL;
	struct dcs_csum_info	expected = {
		.cs_csum = (uint8_t *)&csum_buf,
		.cs_buf_len = csum_size,
		.cs_nr = 1,
		.cs_type = 99,
		.cs_len = csum_size,
		.cs_chunksize = 1234
	};

	iov.iov_buf = iov_buf;
	iov.iov_buf_len = iov_buf_len;
	rc = ci_serialize(&expected, &iov);
	assert_rc_equal(rc, 0);

	ci_cast(&actual, &iov);
	assert_ci_equal(expected, *actual);

	/* iov buf is too short */
	iov.iov_len = iov.iov_len - 1;
	ci_cast(&actual, &iov);
	assert_null(actual);
}

static void
csum_performance_measurements_experiment(uint32_t iod_nr, enum DAOS_HASH_TYPE algo_type)
{
	struct test_data	 td;
	struct daos_csummer	*csummer;
	daos_iod_t		*iods;
	d_sg_list_t		*sgls;
	struct dcs_iod_csums	*iod_csums = NULL;
	daos_key_t		 key = {0};
	struct dcs_csum_info	*key_csum;

	td_init_array_values(&td, iod_nr, 3, 1024, 1024);

	sgls = td.td_sgls;
	iods = td.td_iods;

	assert_success(daos_csummer_init_with_type(&csummer, algo_type, 1024 * 32, 0));

	/*
	 * checksum verification
	 */
	MEASURE_TIME(
	    daos_csummer_verify_iods(csummer, iods, sgls, iod_csums, iod_nr, NULL, -1, NULL),
	    daos_csummer_calc_iods(csummer, sgls, iods, NULL, iod_nr, false, NULL, -1, &iod_csums),
	    daos_csummer_free_ic(csummer, &iod_csums));

	/*
	 * checksum calculation
	 */
	MEASURE_TIME(daos_csummer_calc_iods(csummer, sgls, iods, NULL, iod_nr, false, NULL, -1,
					    &iod_csums),
		     noop(),
		     daos_csummer_free_ic(csummer, &iod_csums));

	/*
	 * iod_csum allocation
	 */
	MEASURE_TIME(daos_csummer_alloc_iods_csums(csummer, iods, iod_nr, false, NULL, &iod_csums),
		     noop(),
		     daos_csummer_free_ic(csummer, &iod_csums));

	/*
	 *  allocation size
	 */
	MEASURE_TIME(daos_csummer_allocation_size(csummer, iods, iod_nr, false, NULL),
		     noop(), noop());

	/*
	 * key verification
	 */
	dts_iov_alloc_str(&key, "key");

	MEASURE_TIME(daos_csummer_verify_key(csummer, &key, key_csum),
		     daos_csummer_calc_key(csummer, &key, &key_csum),
		     daos_csummer_free_ci(csummer, &key_csum));

	/*
	 * copy csummer
	 */
	struct daos_csummer *copy;

	MEASURE_TIME(copy = daos_csummer_copy(csummer),
		     noop(),
		     daos_csummer_destroy(&copy));

	/*
	 * Some helper functions
	 */
	MEASURE_TIME(daos_csummer_get_rec_chunksize(csummer, 3),
		     noop(), noop());

	MEASURE_TIME(csum_align_boundaries(10, 1000, 5, 100, 1, 8),
		     noop(), noop());

	/* Clean up */
	daos_csummer_destroy(&csummer);
	daos_iov_free(&key);
	td_destroy(&td);
}

static void
csum_performance_measurements(void **state)
{
	print_message("\n------\n1 iod, CRC32\n");
	csum_performance_measurements_experiment(1, HASH_TYPE_CRC32);
	print_message("\n------\n10 iod, CRC32\n");
	csum_performance_measurements_experiment(10, HASH_TYPE_CRC32);
	print_message("\n------\n10 iod, noop checksum\n");
	csum_performance_measurements_experiment(10, HASH_TYPE_NOOP);
}

static int
csum_test_setup(void **state)
{
	return 0;
}

static int
csum_test_teardown(void **state)
{
	reset_fake_algo();
	return 0;
}

#define TEST(dsc, test) { dsc, test, csum_test_setup, \
				csum_test_teardown }

static const struct CMUnitTest tests[] = {
	TEST("CSUM01: Test initialize and destroy checksummer",
	     test_init_and_destroy),
	TEST("CSUM02: Test update and get the checksum",
	     test_update_reset),
	TEST("CSUM03: Test update with multiple buffer",
	     test_update_with_multiple_buffers),
	TEST("CSUM04: Create checksum from a single iov, recx, and chunk",
	     test_daos_checksummer_with_single_iov_single_chunk),
	TEST("CSUM05: Create checksum from unaligned recx",
	     test_daos_checksummer_with_unaligned_recx),
	TEST("CSUM06: Create checksum from a multiple iov, "
	     "single recx, and chunk",
	     test_daos_checksummer_with_mult_iov_single_chunk),
	TEST("CSUM07: Create checksum from a multiple iov, "
	     "multi recx, and chunk",
	     test_daos_checksummer_with_multi_iov_multi_extents),
	TEST("CSUM08: More complicated daos checksumming",
	     test_daos_checksummer_with_multiple_chunks),

	TEST("CSUM09.0: Test all checksum algorithms: checksum size and "
	     "repeat calls result in same hash",
	     test_all_algo_basic),
	TEST("CSUM09.1: Test all checksum algorithms: when update is not "
	     "called, checksum buffer does not change.",
	     test_do_not_need_to_call),
	TEST("CSUM09.2: Test all checksum algorithms: Repeat calls to update "
	     "for different source buffers results in same checksum if all "
	     "data passed at once ",
	     test_repeat_updates),

	TEST("CSUM10: Test map from container prop to csum type",
	     test_container_prop_to_csum_type),
	TEST("CSUM11: Some helper function tests",
	     test_helper_functions),
	TEST("CSUM12: Is Valid Checksum Property",
	     test_is_valid_csum),
	TEST("CSUM13: Is Checksum Property Enabled",
	     test_is_csum_enabled),
	TEST("CSUM14: A simple checksum comparison test",
	     simple_test_compare_checksums),
	TEST("CSUM15: Compare checksums after actual calculation",
	     test_compare_checksums),
	TEST("CSUM16: Get Allocation size",
	     test_get_iod_csum_allocation_size),
	TEST("CSUM17: Calculating number of chunks for range",
	     test_csum_chunk_count),
	TEST("CSUM18: Calculating number of chunks for an extent",
	     test_recx_calc_chunks),
	TEST("CSUM19: Get chunk alignment given an offset and the chunk size",
	     test_daos_align_to_floor_of_chunk),
	TEST("CSUM20: Get chunk from recx",
	     daos_recx_get_chunk_tests),
	TEST("CSUM21: Align range boundaries to chunk borders",
	     test_align_boundaries),
	TEST("CSUM22: Align range to a single chunk",
	     test_align_to_chunk),
	TEST("CSUM23: Single value",
	     simple_sv),
	TEST("CSUM24: Compare single values checksums",
	     test_compare_sv_checksums),
	TEST("CSUM25: Verify single value data",
	     test_verify_sv_data),
	TEST("CSUM26: iod csums includes 'a' key csum",
	     test_akey_csum),
	TEST("CSUM27: Calc record chunk size",
	     test_calc_rec_chunksize),
	TEST("CSUM28: Formatter",
	     test_formatter),
	TEST("CSUM28: Get the recxes from a map", get_map_test),
	TEST("CSUM29: csum_info serialization", test_ci_serialize),
	TEST("CSUM29: Skip calculations based on csummer settings",
	     test_skip_csum_calculations_when_skip_set),
	TEST("CSUM30: csum_info list basic handling", test_csum_info_list_handling),
	TEST("CSUM30.1: csum_info list handle many", test_csum_info_list_handle_many),
	TEST("CSUM_HOLES01: With 2 mapped extents that leave a hole "
	     "at the beginning, in between and "
	     "at the end, all within a single chunk.", holes_1),
	TEST("CSUM_HOLES02: With 2 mapped extents that leave a hole at the "
	     "beginning, in between and at the end, with several chunks",
	     holes_2),
	TEST("CSUM_HOLES03: With 2 mapped extents with a hole that starts "
	     "and ends in different chunks", holes_3),
	TEST("CSUM_HOLES04: With 2 mapped extents with a hole that spans "
	     "multiple chunks", holes_4),
	TEST("CSUM_HOLES05: With record size 2 and many holes within a "
	     "single chunk", holes_5),
	TEST("CSUM_PERF: Some performance measurements", csum_performance_measurements),
};

int
daos_checksum_tests_run()
{
	verbose = false;
	return cmocka_run_group_tests_name("DAOS Checksum Tests", tests,
					   NULL, NULL);
}
