/*
 * Copyright (c) 2020 Intel Corporation. All rights reserved.
 * Copyright (c) 2022 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <complex.h>

#include <criterion/criterion.h>
#include <criterion/parameterized.h>

#include <ofi.h>

#include "cxip.h"
#include "cxip_test_common.h"

TestSuite(avset, .init = cxit_setup_rma, .fini = cxit_teardown_rma,
	  .disabled = false, .timeout = CXIT_DEFAULT_TIMEOUT);

/*
 * Simple test to ensure that any attempt to close the AV before closing any AV
 * Set will fail with -FI_EBUSY.
 */
Test(avset, av_set_refcnt)
{
	// Make sure open AV sets preclude closing AV
	struct fi_av_set_attr attr = {.flags=FI_UNIVERSE};
	struct fid_av_set *set;
	int ret;

	ret = fi_av_set(cxit_av, &attr, &set, NULL);
	cr_expect_eq(ret, 0, "fi_av_set failed, ret=%d", ret);

	ret = fi_close(&cxit_av->fid);
	cr_expect_eq(ret, -FI_EBUSY, "premature AV close failed, ret=%d", ret);

	ret = fi_close(&set->fid);
	cr_expect_eq(ret, 0, "fi_close(set) failed, ret=%d", ret);
}

/*
 * Test of AVSet operations
 *
 * We choose by-two and by-three spans to explore union, intersection, diff
 */
static bool is_div_2(fi_addr_t addr)
{
	return (addr & 1) == 0;
}

static bool is_div_3(fi_addr_t addr)
{
	return ((addr / 3) * 3) == addr;
}

static bool is_not2_and_3(fi_addr_t addr)
{
	return !is_div_2(addr) && is_div_3(addr);
}

static bool is_2_and_3(fi_addr_t addr)
{
	return is_div_2(addr) && is_div_3(addr);
}

static bool is_2_or_3(fi_addr_t addr)
{
	return is_div_2(addr) || is_div_3(addr);
}

static bool is_2_and_not14(fi_addr_t addr)
{
	return is_div_2(addr) && addr != 14;
}

static int _comp_fi_addr(const void *a, const void *b)
{
	// for sorting unsigned
	if (*(fi_addr_t *)a < *(fi_addr_t *)b) return -1;
	if (*(fi_addr_t *)a > *(fi_addr_t *)b) return  1;
	return 0;
}

static int check_av_set(const char *name, struct fid_av_set *set, int max,
			bool (*func)(fi_addr_t), bool is_ordered)
{
	// ensure all elements of set satisfy expectations
	struct cxip_av_set *cxi_set;
	fi_addr_t *local;
	int locidx = 0;
	int errors = 0;
	int i;

	cxi_set = container_of(set, struct cxip_av_set, av_set_fid);

	// Create the expected result
	local = calloc(max, sizeof(fi_addr_t));
	cr_assert_not_null(local, "calloc failure");
	for (i = 0; i < max; i++) {
		if ((*func)(i))
			local[locidx++] = i;
	}

	// If set is not ordered, sort into order to test
	if (! is_ordered)
		qsort(cxi_set->fi_addr_ary, cxi_set->fi_addr_cnt,
		      sizeof(fi_addr_t), _comp_fi_addr);

	// Traverse maximum span, ensuring that allowed addr is the next addr
	if (locidx != cxi_set->fi_addr_cnt) {
		errors++;
	} else {
		for (i = 0; i < locidx; i++) {
			if (local[i] != cxi_set->fi_addr_ary[i]) {
				errors++;
				break;
			}
		}
	}
	if (errors) {
		printf("%s: bad set:\n", name);
		printf("  exp  act\n");
		for (i = 0; i < locidx && i < cxi_set->fi_addr_cnt; i++) {
			printf("  %3ld  %3ld\n", local[i], cxi_set->fi_addr_ary[i]);
		}
		for ( ; i < locidx; i++) {
			printf("  %3ld  ---\n", local[i]);
		}
		for ( ; i < cxi_set->fi_addr_cnt; i++) {
			printf("  ---  %3ld\n", cxi_set->fi_addr_ary[i]);
		}
	}
	free(local);
	return errors;
}

enum {
	ordered = true,
	unordered = false
};

Test(avset, basics)
{
	// Test basic set operations
	struct fi_av_set_attr attr2 = {
		.count = 20, .start_addr = 0, .end_addr = 19, .stride = 2
	};
	struct fi_av_set_attr attr3 = {
		.count = 20, .start_addr = 0, .end_addr = 19, .stride = 3
	};
	struct fid_av_set *set2;
	struct fid_av_set *setX;
	int errors;
	int i, ret;

	errors = 0;

	// Expand the AV, so we have enough addresses to test
	for (i = 0; i < 20; i++) {
		struct cxip_addr fake_addr = { .nic = i, .pid = 0xff };
		int inserted;

		inserted = fi_av_insert(cxit_av, (void *)&fake_addr,
					1, NULL, 0, NULL);
		cr_expect_eq(inserted, 1,
			     "fi_av_insert[%2d] failed, inserted=%d",
			     i, inserted);
	}

	// Create a stride of every second element
	ret = fi_av_set(cxit_av, &attr2, &set2, NULL);
	cr_expect_eq(ret, 0, "1 fi_av_set set2 failed, ret=%d", ret);
	errors += check_av_set("1 two", set2, 20, is_div_2, ordered);

	// Create a stride of every third element
	ret = fi_av_set(cxit_av, &attr3, &setX, NULL);
	cr_expect_eq(ret, 0, "1 fi_av_set setX failed, ret=%d", ret);
	errors += check_av_set("1 three", setX, 20, is_div_3, ordered);

	ret = fi_close(&setX->fid);
	cr_expect_eq(ret, 0, "1 fi_close(setX) failed, ret=%d", ret);

	// 3 union 2
	ret = fi_av_set(cxit_av, &attr3, &setX, NULL);
	cr_expect_eq(ret, 0, "2 fi_av_set setX failed, ret=%d", ret);
	errors += check_av_set("2 dst", setX, 20, is_div_3, ordered);

	ret = fi_av_set_union(setX, set2);
	cr_expect_eq(ret, 0, "2 fi_av_set set_union failed, ret=%d", ret);
	errors += check_av_set("2 union", setX, 20, is_2_or_3, unordered);

	ret = fi_close(&setX->fid);
	cr_expect_eq(ret, 0, "2 fi_close(setX) failed, ret=%d", ret);

	// 3 diff 2
	ret = fi_av_set(cxit_av, &attr3, &setX, NULL);
	cr_expect_eq(ret, 0, "3 fi_av_set setX failed, ret=%d", ret);
	errors += check_av_set("3 dst", setX, 20, is_div_3, ordered);

	ret = fi_av_set_diff(setX, set2);
	cr_expect_eq(ret, 0, "3 fi_av_set set_diff failed, ret=%d", ret);
	errors += check_av_set("3 diff", setX, 20, is_not2_and_3, ordered);

	ret = fi_close(&setX->fid);
	cr_expect_eq(ret, 0, "3 fi_close(setX) failed, ret=%d", ret);

	// 3 intersect 2
	ret = fi_av_set(cxit_av, &attr3, &setX, NULL);
	cr_expect_eq(ret, 0, "4 fi_av_set setX failed, ret=%d", ret);
	errors += check_av_set("4 dst", setX, 20, is_div_3, ordered);

	ret = fi_av_set_intersect(setX, set2);
	cr_expect_eq(ret, 0, "4 fi_av_set set_intersect failed, ret=%d", ret);
	errors += check_av_set("4 intersect", setX, 20, is_2_and_3, ordered);

	ret = fi_close(&setX->fid);
	cr_expect_eq(ret, 0, "4 fi_close(setX) failed, ret=%d", ret);

	// remove address 14
	ret = fi_av_set(cxit_av, &attr2, &setX, NULL);
	cr_expect_eq(ret, 0, "5 fi_av_set setX failed, ret=%d", ret);
	errors += check_av_set("5 dst", setX, 20, is_div_2, ordered);

	ret = fi_av_set_remove(setX, 14);
	cr_expect_eq(ret, 0, "5 fi_av_set fi_av_set_remove failed, ret=%d", ret);
	errors += check_av_set("4 remove", setX, 20, is_2_and_not14, ordered);

	ret = fi_close(&setX->fid);
	cr_expect_eq(ret, 0, "4 fi_close(setX) failed, ret=%d", ret);

	// clean up
	ret = fi_close(&set2->fid);
	cr_expect_eq(ret, 0, "fi_close(set2) failed, ret=%d", ret);

	cr_expect_eq(errors, 0, "Errors detected");
}


