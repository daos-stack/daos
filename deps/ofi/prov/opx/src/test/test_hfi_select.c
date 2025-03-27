/*
 * Copyright (C) 2021 by Cornelis Networks.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "config.h"

#include <stdio.h>
#include <stdarg.h>
#include <check.h>

#include "fi_opx_hfi_select.h"
#include "rdma/providers/fi_log.h"

// dummy definitions
struct fi_provider *fi_opx_provider = NULL;

void fi_log(const struct fi_provider *prov, enum fi_log_level level,
	    enum fi_log_subsys subsys, const char *func, int line,
	    const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

int fi_log_enabled(const struct fi_provider *prov, enum fi_log_level level,
		   enum fi_log_subsys subsys)
{
	return 1;
}

START_TEST (test_empty)
{
	struct hfi_selector s;
	ck_assert_ptr_null(hfi_selector_next("", &s));
	ck_assert_ptr_null(hfi_selector_next("     ", &s));
}
END_TEST

START_TEST (test_hfi_select_bad)
{
	struct hfi_selector s;
	ck_assert_ptr_null(hfi_selector_next("notavalidselector", &s));
	ck_assert_ptr_null(hfi_selector_next("0,numa:0:0", &s));
}
END_TEST

START_TEST (test_hfi_unit)
{
	struct hfi_selector s;
	ck_assert_ptr_nonnull(hfi_selector_next("0", &s));
	ck_assert_int_eq(s.type, HFI_SELECTOR_FIXED);
	ck_assert_int_eq(s.unit, 0);

	ck_assert_ptr_nonnull(hfi_selector_next("4", &s));
	ck_assert_int_eq(s.type, HFI_SELECTOR_FIXED);
	ck_assert_int_eq(s.unit, 4);
}
END_TEST

START_TEST (test_hfi_unit_bad)
{
	struct hfi_selector s;
	ck_assert_ptr_null(hfi_selector_next("  0  ", &s));
	ck_assert_ptr_null(hfi_selector_next("0,", &s));
	ck_assert_ptr_null(hfi_selector_next("-1", &s));
}
END_TEST

START_TEST (test_mapby_numa)
{
	struct hfi_selector s;
	ck_assert_ptr_nonnull(hfi_selector_next("numa:0:0", &s));
	ck_assert_int_eq(s.type, HFI_SELECTOR_MAPBY);
	ck_assert_int_eq(s.unit, 0);
	ck_assert_int_eq(s.mapby.type, HFI_SELECTOR_MAPBY_NUMA);
	ck_assert_int_eq(s.mapby.rangeS, 0);

	ck_assert_ptr_nonnull(hfi_selector_next("numa:1:4", &s));
	ck_assert_int_eq(s.unit, 1);
	ck_assert_int_eq(s.mapby.rangeS, 4);
}
END_TEST

START_TEST (test_mapby_numa_many)
{
	struct hfi_selector s;
	const char *c = "numa:1:1,numa:0:3,numa:0:0,numa:0:2";
	int exp_unit_numa[] = { 1, 1, 0, 3, 0, 0, 0, 2 };
	int i = 0;
	for (i = 0; i < 8; i += 2) {
		c = hfi_selector_next(c, &s);
		ck_assert_ptr_nonnull(c);
		ck_assert_int_eq(s.type, HFI_SELECTOR_MAPBY);
		ck_assert_int_eq(s.unit, exp_unit_numa[i]);
		ck_assert_int_eq(s.mapby.type, HFI_SELECTOR_MAPBY_NUMA);
		ck_assert_int_eq(s.mapby.rangeS, exp_unit_numa[i + 1]);
		ck_assert_int_eq(s.mapby.rangeE, s.mapby.rangeS);
	}
	ck_assert_int_eq(i, 8);
}
END_TEST

START_TEST (test_mapby_bad)
{
	struct hfi_selector s;
	ck_assert_ptr_null(hfi_selector_next("notnuma:0:0", &s));
}
END_TEST

START_TEST (test_mapby_numa_bad)
{
	struct hfi_selector s;
	ck_assert_ptr_null(hfi_selector_next("numa:-1:0", &s));
	ck_assert_ptr_null(hfi_selector_next("numa:0:-1", &s));
	ck_assert_ptr_null(hfi_selector_next("numa:0", &s));
	ck_assert_ptr_null(hfi_selector_next("numa::0", &s));
	ck_assert_ptr_null(hfi_selector_next("numa:   :0", &s));
	ck_assert_ptr_null(hfi_selector_next("numa0:0:", &s));
	ck_assert_ptr_null(hfi_selector_next("numa:0:0:", &s));
}
END_TEST

START_TEST (test_mapby_core_standard)
{
	struct hfi_selector s;
	const char *c = "core:1:1,core:0:3,core:0:0,core:0:2";
        int exp_unit_numa[] = { 1, 1, 0, 3, 0, 0, 0, 2 };
        int i = 0;
        for (i = 0; i < 8; i += 2) {
                c = hfi_selector_next(c, &s);
                ck_assert_ptr_nonnull(c);
                ck_assert_int_eq(s.type, HFI_SELECTOR_MAPBY);
                ck_assert_int_eq(s.unit, exp_unit_numa[i]);
                ck_assert_int_eq(s.mapby.type, HFI_SELECTOR_MAPBY_CORE);
                ck_assert_int_eq(s.mapby.rangeS, exp_unit_numa[i + 1]);
		ck_assert_int_eq(s.mapby.rangeE, s.mapby.rangeS);
        }
        ck_assert_int_eq(i, 8);
}
END_TEST

START_TEST (test_mapby_core_range)
{
	struct hfi_selector s;
	const char *c;
	c = hfi_selector_next("core:0:0-5", &s);
	ck_assert_ptr_nonnull(c);
	ck_assert_int_eq(s.type, HFI_SELECTOR_MAPBY);
	ck_assert_int_eq(s.unit, 0);
	ck_assert_int_eq(s.mapby.type, HFI_SELECTOR_MAPBY_CORE);
	ck_assert_int_eq(s.mapby.rangeS, 0);
	ck_assert_int_eq(s.mapby.rangeE, 5);
}
END_TEST

START_TEST (test_mapby_core_mixed)
{
	struct hfi_selector s;
	const char *c = "core:0:1-5,core:1:0,core:1:2-5,core:1:7";
	int exp_unit_coreS_coreE[] = {0, 1, 5, 1, 0, 0, 1, 2, 5, 1, 7, 7};
	int i = 0;
	for (i = 0; i < 12; i += 3) {
                c = hfi_selector_next(c, &s);
                ck_assert_ptr_nonnull(c);
                ck_assert_int_eq(s.type, HFI_SELECTOR_MAPBY);
                ck_assert_int_eq(s.unit, exp_unit_coreS_coreE[i]);
                ck_assert_int_eq(s.mapby.type, HFI_SELECTOR_MAPBY_CORE);
                ck_assert_int_eq(s.mapby.rangeS, exp_unit_coreS_coreE[i + 1]);
                ck_assert_int_eq(s.mapby.rangeE, exp_unit_coreS_coreE[i+2]);
        }
        ck_assert_int_eq(i, 12);
}
END_TEST

START_TEST (test_mapby_core_bad)
{
	struct hfi_selector s;
	ck_assert_ptr_null(hfi_selector_next("core:-1:0", &s));
	ck_assert_ptr_null(hfi_selector_next("core:0:-1", &s));
	ck_assert_ptr_null(hfi_selector_next("core:0:-1-2", &s));
	ck_assert_ptr_null(hfi_selector_next("core:0:2-1", &s));
	ck_assert_ptr_null(hfi_selector_next("core:0:1--5", &s));
	ck_assert_ptr_null(hfi_selector_next("core:0-1:1", &s));
	ck_assert_ptr_null(hfi_selector_next("core:0:1 -2", &s));
	ck_assert_ptr_null(hfi_selector_next("core::0:1-2", &s));
	ck_assert_ptr_null(hfi_selector_next("core:1:2:", &s));
	ck_assert_ptr_null(hfi_selector_next("core:0:1-", &s));
}
END_TEST

START_TEST (test_default_good)
{
	struct hfi_selector s;
	const char *c = "default";
	c = hfi_selector_next(c, &s);
	ck_assert_ptr_nonnull(c);
	ck_assert_int_eq(s.type, HFI_SELECTOR_DEFAULT);
}
END_TEST

START_TEST (test_default_bad)
{
	struct hfi_selector s;
	ck_assert_ptr_null(hfi_selector_next("defaults", &s));
	ck_assert_ptr_null(hfi_selector_next("default:1", &s));
	ck_assert_ptr_null(hfi_selector_next("DEFAULT", &s));
}
END_TEST

START_TEST (test_fixed_good)
{
	struct hfi_selector s;
	const char *c = "fixed:10";
	c = hfi_selector_next(c, &s);
	ck_assert_ptr_nonnull(c);
	ck_assert_int_eq(s.type, HFI_SELECTOR_FIXED);
	ck_assert_int_eq(s.unit, 10);
}
END_TEST

START_TEST (test_fixed_bad)
{
	struct hfi_selector s;
	ck_assert_ptr_null(hfi_selector_next("fixed", &s));
	ck_assert_ptr_null(hfi_selector_next("fixed:1:2", &s));
}
END_TEST

START_TEST (test_mixed_selector_good)
{
	struct hfi_selector s;
	const char *c = "core:0:1-5,fixed:1";
	c = hfi_selector_next(c, &s);
	ck_assert_ptr_nonnull(c);
	ck_assert_int_eq(s.type, HFI_SELECTOR_MAPBY);
        ck_assert_int_eq(s.unit, 0);
        ck_assert_int_eq(s.mapby.type, HFI_SELECTOR_MAPBY_CORE);
        ck_assert_int_eq(s.mapby.rangeS, 1);
        ck_assert_int_eq(s.mapby.rangeE, 5);
	c = hfi_selector_next(c, &s);
	ck_assert_ptr_nonnull(c);
	ck_assert_int_eq(s.type, HFI_SELECTOR_FIXED);
	ck_assert_int_eq(s.unit, 1);
}
END_TEST


Suite *hfi_select_suite(void)
{
	Suite *s = suite_create("hfi_select");
	TCase *tc = tcase_create("envvar_parsing");

	tcase_add_test(tc, test_empty);
	tcase_add_test(tc, test_hfi_select_bad);
	tcase_add_test(tc, test_hfi_unit);
	tcase_add_test(tc, test_hfi_unit_bad);
	tcase_add_test(tc, test_mapby_bad);
	tcase_add_test(tc, test_mapby_numa);
	tcase_add_test(tc, test_mapby_numa_many);
	tcase_add_test(tc, test_mapby_numa_bad);
	tcase_add_test(tc, test_mapby_core_range);
	tcase_add_test(tc, test_mapby_core_bad);
	tcase_add_test(tc, test_mapby_core_standard);
	tcase_add_test(tc, test_mapby_core_mixed);
	tcase_add_test(tc, test_mixed_selector_good);
	tcase_add_test(tc, test_default_good);
	tcase_add_test(tc, test_default_bad);
	tcase_add_test(tc, test_fixed_good);
	tcase_add_test(tc, test_fixed_bad);

	suite_add_tcase(s, tc);
	return s;
}

int main(void)
{
	Suite *s = hfi_select_suite();
	SRunner *sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	int fail_count = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (fail_count == 0);
}
