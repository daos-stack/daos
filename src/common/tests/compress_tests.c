/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC        DD_FAC(tests)
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h> /** For cmocka.h */
#include <stdint.h>
#include <cmocka.h>
#include <gurt/types.h>
#include <daos/compression.h>
#include <daos/common.h>
#include <daos/cont_props.h>
#include <daos/tests_lib.h>

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

#define MAX_INPUT_SIZE 4096

/** Text to be compressed */
static uint8_t origin_buf[] =
"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed luctus purus "
"et risus vulputate, et mollis orci ullamcorper. Nulla facilisi. Fusce in "
"ligula sed purus various aliquet interdum vitae justo. Proin quis diam velit."
"Nulla various iaculis auctor. Cras volutpat, justo eu dictum pulvinar, elit"
"sem porttitor metus, et imperdiet metus sapien et ante. Nullam nisi nulla, "
"ornare eu tristique eu, dignissim vitae diam. Nulla sagittis porta libero, "
"a accumsan felis sagittis scelerisque.  Integer laoreet eleifend congue. "
"Etiam rhoncus leo vel dolor fermentum, quis luctus nisl iaculis. Praesent "
"a erat sapien. Aliquam semper mi in lorem ultrices ultricies. Lorem ipsum "
"dolor sit amet, consectetur adipiscing elit. In feugiat risus sed enim "
"ultrices, at sodales nulla tristique. Maecenas eget pellentesque justo, "
"sed pellentesque lectus. Fusce sagittis sit amet elit vel various. Donec"
"sed ligula nec ligula vulputate rutrum sed ut lectus. Etiam congue pharetra "
"leo vitae cursus. Morbi enim ante, porttitor ut various vel, tincidunt quis"
"justo. Nunc iaculis, risus id ultrices semper, metus est efficitur ligula, "
"vel posuere risus nunc eget purus. Ut lorem turpis, condimentum at sem sed, "
"porta aliquam turpis. In ut sapien a nulla dictum tincidunt quis sit amet "
"lorem. Fusce at est egestas, luctus neque eu, consectetur tortor. Phasellus "
"eleifend ultricies nulla ac lobortis.  Morbi maximus quam cursus vehicula "
"iaculis. Maecenas cursus vel justo ut rutrum. Curabitur magna orci, dignissim"
" eget dapibus vitae, finibus id lacus. Praesent rhoncus mattis augue vitae "
"bibendum. Praesent porta mauris non ultrices fermentum. Quisque vulputate "
"ipsum in sodales pulvinar. Aliquam nec mollis felis. Donec vitae augue "
"pulvinar, congue nisl sed, pretium purus. Fusce lobortis mi ac neque "
"scelerisque semper. Pellentesque vel est vitae magna aliquet aliquet. "
"Nam non dolor. Nulla facilisi. Class aptent taciti sociosqu ad litora "
"torquent per conubia nostra, per inceptos himenaeos. Morbi ac lacinia "
"felis metus.";

/** Compressed buffer - same size as origin buffer */
static uint8_t comp_buf[sizeof(origin_buf)];

/** Decompressed buffer, used to verify the compressed data
 * can be decompressed and same as origin data
 */
static uint8_t decomp_buf[sizeof(origin_buf)];

static void
test_alg_basic(const char *alg_name)
{
	struct daos_compressor *compressor;
	int i = 0;
	int origion_sz = sizeof(origin_buf);
	int compressed_sz = sizeof(comp_buf);
	int decompressed_sz = sizeof(decomp_buf);
	size_t compr_output_sz = 0;
	size_t decompr_output_sz = 0;
	bool qat_preferred = true;

	/** Initialize compressor */
	int rc = daos_compressor_init_with_type(
				&compressor,
				daos_str2compresscontprop(alg_name),
				qat_preferred,
				MAX_INPUT_SIZE);

	assert_int_equal(DC_STATUS_OK, rc);
	/** Perform compress */
	rc = daos_compressor_compress(
				compressor,
				origin_buf,
				origion_sz,
				comp_buf,
				compressed_sz,
				&compr_output_sz);

	assert_int_equal(DC_STATUS_OK, rc);
	if (verbose)
		print_message("%s: compressed %d bytes --> %d bytes.\n",
			      alg_name, (int)origion_sz, (int)compr_output_sz);

	/** Perform decompress */
	rc = daos_compressor_decompress(
				compressor,
				comp_buf,
				compr_output_sz,
				decomp_buf,
				decompressed_sz,
				&decompr_output_sz);

	assert_int_equal(DC_STATUS_OK, rc);
	if (verbose)
		print_message("%s: decompressed %d bytes --> %d bytes.\n",
			      alg_name,
			      (int)compr_output_sz,
			      (int)decompr_output_sz);

	assert_int_equal(decompr_output_sz, origion_sz);

	/** Verify the results */
	for (i = 0; i < origion_sz; i++)
		if (origin_buf[i] != decomp_buf[i])
			fail_msg("compression type %s, decomp_buf[%d] "
				 "(%d) != (%d)",
				 alg_name, i,
				 origin_buf[i],
				 decomp_buf[i]);

	/** Destroy the compressor */
	daos_compressor_destroy(&compressor);
}

static void
test_lz4_algo_basic(void **state)
{
	test_alg_basic("lz4");
}

static void
test_deflate_algo_basic(void **state)
{
	test_alg_basic("deflate");
}

static void
test_deflate1_algo_basic(void **state)
{
	test_alg_basic("deflate1");
}

static void
test_deflate2_algo_basic(void **state)
{
	test_alg_basic("deflate2");
}

static void
test_deflate3_algo_basic(void **state)
{
	test_alg_basic("deflate3");
}

static void
test_deflate4_algo_basic(void **state)
{
	test_alg_basic("deflate4");
}

static int
compress_test_setup(void **state)
{
	return 0;
}

static int
compress_test_teardown(void **state)
{
	return 0;
}

#define TEST(dsc, test) { dsc, test, compress_test_setup, \
				compress_test_teardown }

static const struct CMUnitTest tests[] = {
	TEST("COMPRESS01: Test lz4 compression basic functions",
	     test_lz4_algo_basic),
	TEST("COMPRESS02: Test deflate compression basic functions",
	     test_deflate_algo_basic),
	TEST("COMPRESS03: Test deflate1 compression basic functions",
	     test_deflate1_algo_basic),
	TEST("COMPRESS04: Test deflate2 compression basic functions",
	     test_deflate2_algo_basic),
	TEST("COMPRESS05: Test deflate3 compression basic functions",
	     test_deflate3_algo_basic),
	TEST("COMPRESS06: Test deflate4 compression basic functions",
	     test_deflate4_algo_basic),
};

int
daos_compress_tests_run()
{
	verbose = false;
	return cmocka_run_group_tests_name("DAOS Compress Tests", tests,
					   NULL, NULL);
}
