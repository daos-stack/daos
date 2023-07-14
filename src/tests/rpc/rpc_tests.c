/**
* (C) Copyright 2023 Intel Corporation.
*
* SPDX-License-Identifier: BSD-2-Clause-Patent
*/

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <cmocka.h>

#include <daos/checksum.h>
#include <daos/tests_lib.h>

/* Testing internal interfaces */
#include <object/rpc_csum.h>

bool g_verbose;

void
print_verbose(const char *msg, ...)
{
	va_list args;

	if (!g_verbose)
		return;
	va_start(args, msg);
	vprintf(msg, args);
	va_end(args);
}

uint8_t g_buf[1024];
uint8_t *g_buf_ptr;
uint32_t g_buf_remaining;

static inline void
g_buf_reset_idx()
{
	g_buf_ptr = g_buf;
	g_buf_remaining = ARRAY_SIZE(g_buf);
}

static inline void
g_buf_reset()
{
	memset(g_buf, 0, ARRAY_SIZE(g_buf));
	g_buf_reset_idx();
}

/* Fake the mercury buffer */
void *
hg_proc_save_ptr(void *proc, uint64_t data_size)
{
	uint8_t *ptr;

	D_ASSERT(g_buf_ptr != NULL);

	ptr = g_buf_ptr;

	g_buf_ptr += data_size;
	if (g_buf_remaining < data_size)
		fail_msg("test buffer exceeded");
	g_buf_remaining -= data_size;

	return ptr;
}

int
crt_proc_memcpy(crt_proc_t proc, crt_proc_op_t proc_op,
		void *data, size_t data_size)
{
	void *buf;

	if (FREEING(proc_op))
		return 0;
	buf = hg_proc_save_ptr(proc, data_size);
	if (ENCODING(proc_op)) {
		memcpy(buf, data, data_size);
		print_verbose("Encoding memcpy size: %lu\n", data_size);
	} else { /* DECODING(proc_op) */
		memcpy(data, buf, data_size);
		print_verbose("Decoding memcpy size: %lu\n", data_size);
	}

	return 0;
}

#define CRT_PROC_TYPE_FUNC(type)                                                                   \
	int crt_proc_##type(crt_proc_t proc, crt_proc_op_t proc_op, type * data)                   \
	{                                                                                          \
		type *buf;                                                                         \
		if (FREEING(proc_op))                                                              \
			return 0;                                                                  \
		buf = hg_proc_save_ptr(proc, sizeof(*buf));                                        \
		if (ENCODING(proc_op)) {                                                           \
			*buf = *data;                                                              \
			print_verbose("Encoding " #type ": %lu\n", (uint64_t)*buf);                \
		} else /* DECODING(proc_op) */ {                                                   \
			*data = *buf;                                                              \
			print_verbose("Decoding " #type ": %lu\n", (uint64_t)*buf);                \
		}                                                                                  \
		return 0;                                                                          \
	}

CRT_PROC_TYPE_FUNC(int8_t)
CRT_PROC_TYPE_FUNC(uint8_t)
CRT_PROC_TYPE_FUNC(int16_t)
CRT_PROC_TYPE_FUNC(uint16_t)
CRT_PROC_TYPE_FUNC(int32_t)
CRT_PROC_TYPE_FUNC(uint32_t)
CRT_PROC_TYPE_FUNC(int64_t)
CRT_PROC_TYPE_FUNC(uint64_t)
CRT_PROC_TYPE_FUNC(bool)

/*
 * -----------------------
 * Tests
 * -----------------------
 */

#define assert_ci_equal(e, a) do {\
	assert_int_equal((e).cs_nr, (a).cs_nr); \
	assert_int_equal((e).cs_len, (a).cs_len); \
	assert_int_equal((e).cs_buf_len, (a).cs_buf_len); \
	assert_int_equal((e).cs_chunksize, (a).cs_chunksize); \
	assert_int_equal((e).cs_type, (a).cs_type); \
	assert_memory_equal((e).cs_csum, (a).cs_csum, (e).cs_len * (e).cs_nr); \
	} while (0)

static void
csum_info_encode_decode_free(void **state)
{
	uint8_t			 csum_buf[1024];
	struct dcs_csum_info	 csums = {0};
	struct dcs_csum_info	*p_csum_encoded = &csums;
	struct dcs_csum_info	*p_csum_decoded;

	p_csum_encoded->cs_nr = 4;
	p_csum_encoded->cs_len = 4;
	p_csum_encoded->cs_chunksize = 32 * 1024;
	p_csum_encoded->cs_type = 1;
	p_csum_encoded->cs_csum = csum_buf;
	p_csum_encoded->cs_buf_len = ARRAY_SIZE(csum_buf);
	memset(csum_buf, 0xAA, ARRAY_SIZE(csum_buf));

	/* encode */
	assert_success(crt_proc_struct_dcs_csum_info(NULL, CRT_PROC_ENCODE, &p_csum_encoded));

	/* reset the internal buf for decoding */
	g_buf_reset_idx();

	/* decode */
	assert_success(crt_proc_struct_dcs_csum_info(NULL, CRT_PROC_DECODE, &p_csum_decoded));

	/* decoded should match encoded */
	assert_int_equal(p_csum_encoded->cs_nr, p_csum_decoded->cs_nr);
	assert_int_equal(p_csum_encoded->cs_len, p_csum_decoded->cs_len);
	assert_int_equal(p_csum_encoded->cs_chunksize, p_csum_decoded->cs_chunksize);
	assert_int_equal(p_csum_encoded->cs_type, p_csum_decoded->cs_type);

	/* buf of decoded will be limited to size needed, not the same size as the decoded csum */
	assert_int_equal(ci_csums_len(*p_csum_encoded), p_csum_decoded->cs_buf_len);
	assert_memory_equal(p_csum_encoded->cs_csum, p_csum_decoded->cs_csum,
			    p_csum_decoded->cs_buf_len);

	/* Free */
	assert_success(crt_proc_struct_dcs_csum_info(NULL, CRT_PROC_FREE, &p_csum_decoded));
	assert_null(p_csum_decoded);

	/* buf should not be smaller than csums * csum_size */
	p_csum_encoded->cs_buf_len = 1;
	assert_rc_equal(-DER_HG,
			crt_proc_struct_dcs_csum_info(NULL, CRT_PROC_ENCODE, &p_csum_encoded));
}

static void
iod_csum_encode_decode_free(void **state)
{
	struct dcs_iod_csums    iod_csum_encoded = {0};
	struct dcs_iod_csums	iod_csum_decoded = {0};
	struct dcs_csum_info	csum_infos[2] = {0};
	uint8_t			akey_csum_buf[4] = {0};
	uint8_t			csum_bufs[2][4] = {0};
	const uint32_t		csum_size = 4;
	const uint32_t		csum_type = 99;
	int			i;

	iod_csum_encoded.ic_nr = ARRAY_SIZE(csum_infos);
	iod_csum_encoded.ic_data = csum_infos;
	ci_set(&iod_csum_encoded.ic_akey, akey_csum_buf, ARRAY_SIZE(akey_csum_buf), csum_size, 1,
	       CSUM_NO_CHUNK, csum_type);
	for (i = 0; i < ARRAY_SIZE(csum_infos); i++) {
		ci_set(&csum_infos[i], csum_bufs[i], ARRAY_SIZE(csum_bufs[i]), csum_size,
		       ARRAY_SIZE(csum_bufs[i]) / csum_size, 1024, csum_type);
	}

	/* encode */
	assert_success(crt_proc_struct_dcs_iod_csums(NULL, CRT_PROC_ENCODE, &iod_csum_encoded));

	g_buf_reset_idx();

	/* decode */
	assert_success(crt_proc_struct_dcs_iod_csums(NULL, CRT_PROC_DECODE, &iod_csum_decoded));

	assert_int_equal(iod_csum_encoded.ic_nr, iod_csum_decoded.ic_nr);
	assert_ci_equal(iod_csum_encoded.ic_akey, iod_csum_decoded.ic_akey);

	for (i = 0; i < ARRAY_SIZE(csum_infos); i++)
		assert_ci_equal(iod_csum_encoded.ic_data[i], iod_csum_decoded.ic_data[i]);

	assert_success(crt_proc_struct_dcs_iod_csums(NULL, CRT_PROC_FREE, &iod_csum_decoded));
	assert_null(iod_csum_decoded.ic_data);
	assert_null(iod_csum_decoded.ic_akey.cs_csum);

	/* if the buf len is too small for the checksum count/size, an error is returned */
	iod_csum_encoded.ic_akey.cs_buf_len = 1;
	assert_rc_equal(-DER_HG,
			crt_proc_struct_dcs_iod_csums(NULL, CRT_PROC_ENCODE, &iod_csum_encoded));
}

/* ---------------------------------------------------------------------------------------- */

int rpc_test_setup(void **state)
{
	g_buf_reset();

	return 0;
}

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define	TS(test_fn) \
	{ "RPC_" STR(__COUNTER__)": " #test_fn, test_fn, rpc_test_setup, NULL }

/* Test list */
static const struct CMUnitTest rpc_tests[] = {
	TS(csum_info_encode_decode_free),
	TS(iod_csum_encode_decode_free),
};

/*
 * ----------------------------------------------
 * Main
 * ----------------------------------------------
 */
int
main(int argc, char **argv)
{
	int	rc = 0;

	g_verbose = false;
#if CMOCKA_FILTER_SUPPORTED == 1 /** for cmocka filter(requires cmocka 1.1.5) */
	char	 filter[1024];

	if (argc > 1) {
		snprintf(filter, 1024, "*%s*", argv[1]);
		cmocka_set_test_filter(filter);
	}
#endif

	rc += cmocka_run_group_tests_name("RPC encoding/decoding", rpc_tests, NULL, NULL);

	return rc;
}
