/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <sys/mman.h>
#include <unistd.h>

#include <gurt/debug.h>

/** helper */
#define _STRINGIFY(x)       #x
#define STRINGIFY(x)        _STRINGIFY(x)

#define FULL_LINE_LEN       16
#define MAP_ADDRESS         0x12340000
#define LINE_01_ADDRESS     0x12340010
#define LINE_02_ADDRESS     0x12340020
#define LINE_00_ADDRESS_STR STRINGIFY(MAP_ADDRESS)
#define LINE_01_ADDRESS_STR STRINGIFY(LINE_01_ADDRESS)
#define LINE_02_ADDRESS_STR STRINGIFY(LINE_02_ADDRESS)

#define HDR_STR(SIZE_STR)   "ptr=" LINE_00_ADDRESS_STR ", size=" SIZE_STR "\n"
#define EXP_LINE_00_01B     LINE_00_ADDRESS_STR ": ff \n"
#define EXP_LINE_00_15B     LINE_00_ADDRESS_STR ": ff fe fd fc fb fa f9 f8 f7 f6 f5 f4 f3 f2 f1 \n"
#define EXP_LINE_00_FULL    LINE_00_ADDRESS_STR ": ff fe fd fc fb fa f9 f8 f7 f6 f5 f4 f3 f2 f1 f0 \n"
#define EXP_LINE_01_01B     LINE_01_ADDRESS_STR ": ef \n"
#define EXP_LINE_01_15B     LINE_01_ADDRESS_STR ": ef ee ed ec eb ea e9 e8 e7 e6 e5 e4 e3 e2 e1 \n"
#define EXP_LINE_01_FULL    LINE_01_ADDRESS_STR ": ef ee ed ec eb ea e9 e8 e7 e6 e5 e4 e3 e2 e1 e0 \n"
#define EXP_LINE_02_01B     LINE_02_ADDRESS_STR ": df \n"
#define EXP_LINE_02_FULL    LINE_02_ADDRESS_STR ": df de dd dc db da d9 d8 d7 d6 d5 d4 d3 d2 d1 d0 \n"

static const char Exp_line_00_full[] = EXP_LINE_00_FULL;
static const char Exp_line_01_full[] = EXP_LINE_01_FULL;
static const char Exp_line_02_full[] = EXP_LINE_02_FULL;

/** mocks */

#define BUF_SIZE 1024

void
__wrap_d_vlog(int flags, const char *fmt, va_list ap)
{
	static char buf[BUF_SIZE];
	const char *output;
	int         rc;

	/** generate the output string */
	rc = vsnprintf(buf, BUF_SIZE, fmt, ap);
	assert(rc > 0);

	/** skip the "file:line_number func() " bit */
	output = strchr(buf, ' ');
	assert_non_null(output);
	output += 1;
	output = strchr(output, ' ');
	assert_non_null(output);
	output += 1;

	check_expected(output);
}

/** setup & teardown */

static int
setup(void **state)
{
	void  *addr = (void *)MAP_ADDRESS; /** desired address */
	size_t size = 4096;                /** one page */

	void  *ptr = mmap(addr, size, PROT_READ | PROT_WRITE,
			  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	assert_int_not_equal(ptr, MAP_FAILED);

	uint8_t *mem = ptr;

	/** initialize the bit which is in use - three lines 16 bytes each */
	for (int line = 0; line < 3; ++line) {
		for (int _byte = 0; _byte < 16; ++_byte) {
			int index  = line * 16 + _byte;
			mem[index] = 0xff - index;
		}
	}

	*state = ptr;

	return 0;
}

static int
teardown(void **state)
{
	void *ptr = *state;
	int   rc;

	rc = munmap(ptr, 4096);
	assert_int_equal(rc, 0);

	return 0;
}

/** tests */

static void
test_ptr_NULL(void **state)
{
	const char hdr[] = "ptr=(nil), size=0\n";

	expect_string(__wrap_d_vlog, output, hdr);
	d_log_memory(NULL, 0);
}

static void
test_size_0(void **state)
{
	const char     hdr[] = HDR_STR("0");
	const uint8_t *mem   = *state;

	expect_string(__wrap_d_vlog, output, hdr);
	d_log_memory(mem, 0);
}

static void
test_very_short_line(void **state)
{
	const char hdr[] = HDR_STR("1");
	const char exp[] = EXP_LINE_00_01B;
	uint8_t   *mem   = *state;

	expect_string(__wrap_d_vlog, output, hdr);
	expect_string(__wrap_d_vlog, output, exp);
	d_log_memory(mem, 1);
}

static void
test_short_line(void **state)
{
	const char hdr[] = HDR_STR("15");
	const char exp[] = EXP_LINE_00_15B;
	uint8_t   *mem   = *state;

	expect_string(__wrap_d_vlog, output, hdr);
	expect_string(__wrap_d_vlog, output, exp);
	d_log_memory(mem, FULL_LINE_LEN - 1);
}

static void
test_full_line(void **state)
{
	const char hdr[] = HDR_STR("16");
	uint8_t   *mem   = *state;

	expect_string(__wrap_d_vlog, output, hdr);
	expect_string(__wrap_d_vlog, output, Exp_line_00_full);
	d_log_memory(mem, FULL_LINE_LEN);
}

static void
test_full_line_plus(void **state)
{
	const char hdr[]  = HDR_STR("17");
	const char exp1[] = EXP_LINE_01_01B;
	uint8_t   *mem    = *state;

	expect_string(__wrap_d_vlog, output, hdr);
	expect_string(__wrap_d_vlog, output, Exp_line_00_full);
	expect_string(__wrap_d_vlog, output, exp1);
	d_log_memory(mem, FULL_LINE_LEN + 1);
}

static void
test_almost_two_lines(void **state)
{
	const char hdr[]  = HDR_STR("31");
	const char exp1[] = EXP_LINE_01_15B;
	uint8_t   *mem    = *state;

	expect_string(__wrap_d_vlog, output, hdr);
	expect_string(__wrap_d_vlog, output, Exp_line_00_full);
	expect_string(__wrap_d_vlog, output, exp1);
	d_log_memory(mem, FULL_LINE_LEN * 2 - 1);
}

static void
test_two_lines(void **state)
{
	const char hdr[] = HDR_STR("32");
	uint8_t   *mem   = *state;

	expect_string(__wrap_d_vlog, output, hdr);
	expect_string(__wrap_d_vlog, output, Exp_line_00_full);
	expect_string(__wrap_d_vlog, output, Exp_line_01_full);
	d_log_memory(mem, FULL_LINE_LEN * 2);
}

static void
test_two_lines_plus(void **state)
{
	const char hdr[]  = HDR_STR("33");
	const char exp2[] = EXP_LINE_02_01B;
	uint8_t   *mem    = *state;

	expect_string(__wrap_d_vlog, output, hdr);
	expect_string(__wrap_d_vlog, output, Exp_line_00_full);
	expect_string(__wrap_d_vlog, output, Exp_line_01_full);
	expect_string(__wrap_d_vlog, output, exp2);
	d_log_memory(mem, FULL_LINE_LEN * 2 + 1);
}

static void
test_three_lines(void **state)
{
	const char hdr[] = HDR_STR("48");
	uint8_t   *mem   = *state;

	expect_string(__wrap_d_vlog, output, hdr);
	expect_string(__wrap_d_vlog, output, Exp_line_00_full);
	expect_string(__wrap_d_vlog, output, Exp_line_01_full);
	expect_string(__wrap_d_vlog, output, Exp_line_02_full);
	d_log_memory(mem, FULL_LINE_LEN * 3);
}

static const struct CMUnitTest tests[] = {
    {"DUMP001: ptr == NULL", test_ptr_NULL, NULL, NULL},
    {"DUMP002: size == 0", test_size_0, NULL, NULL},
    {"DUMP003: very short line (1 byte)", test_very_short_line, NULL, NULL},
    {"DUMP004: short line (15 bytes)", test_short_line, NULL, NULL},
    {"DUMP005: full line (16 bytes)", test_full_line, NULL, NULL},
    {"DUMP006: full line + 1 (17 bytes)", test_full_line_plus, NULL, NULL},
    {"DUMP007: almost two lines (31 bytes)", test_almost_two_lines, NULL, NULL},
    {"DUMP008: two lines (32 bytes)", test_two_lines, NULL, NULL},
    {"DUMP009: two lines + 1 (33 bytes)", test_two_lines_plus, NULL, NULL},
    {"DUMP010: three lines (48 bytes)", test_three_lines, NULL, NULL},
    {NULL, NULL, NULL, NULL}};

int
main(int argc, char **argv)
{
	int rc;

	d_log_init();

	d_register_alt_assert(mock_assert);

	rc = cmocka_run_group_tests_name("d_log_memory() tests", tests, setup, teardown);

	d_log_fini();

	return rc;
}
