/*
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Copyright (c) 2015-2018 Hewlett Packard Enterprise Development LP
 */

#include <stdio.h>
#include <stdlib.h>
#include <netinet/ether.h>

#include <criterion/criterion.h>

#include "cxip.h"
#include "cxip_test_common.h"

static struct cxip_addr *test_addrs;
fi_addr_t *test_fi_addrs;
#define AV_COUNT 1024
int naddrs = AV_COUNT * 10;

static char *nic_to_amac(uint32_t nic)
{
	struct ether_addr mac = {};

	mac.ether_addr_octet[5] = nic;
	mac.ether_addr_octet[4] = nic >> 8;
	mac.ether_addr_octet[3] = nic >> 16;

	return ether_ntoa(&mac);
}

/* This allocates memory for naddrs FSAs (test_addrs), and naddrs tokens
 * (test_fi_addrs), and initializes the FSAs to unique addresses.
 */
static void
test_addrs_init(void)
{
	int i;

	test_addrs = malloc(naddrs * sizeof(struct cxip_addr));
	cr_assert(test_addrs != NULL);

	test_fi_addrs = calloc(naddrs, sizeof(fi_addr_t));
	cr_assert(test_fi_addrs != NULL);

	for (i = 0; i < naddrs; i++) {
		test_addrs[i].nic = i;
		test_addrs[i].pid = i + 1;
	}
}

/* Clean up the FSA and token memory.
 */
static void
test_addrs_fini(void)
{
	free(test_fi_addrs);
	free(test_addrs);
}

/* This creates an AV with 'count' objects, and peeks at internals to ensure
 * that the structure is sound. If 'count' is 0, this should default to
 * cxip_av_dev_sz.
 */
static void
test_create(size_t count)
{
	cxit_av_attr.count = count;
	cxit_create_av();

	/* Should allocate a structure   */
	cr_assert(cxit_av != NULL,
		"cxit_av=%p", cxit_av);

	cxit_destroy_av();
}

/* This inserts 'count' FSAs, looks up all of them, then removes all of them. It
 * repeats this 'iters' times without destroying the AV.
 */
static void
__test_insert(int count, int iters)
{
	int j, i, ret;
	struct cxip_addr addr;
	size_t addrlen;

	/* Can't test addresses we haven't set up   */
	cr_assert(naddrs >= count, "Invalid test case");

	cxit_create_av();
	test_addrs_init();

	for (j = 0; j < iters; j++) {
		/* Insert addresses   */
		for (i = 0; i < count; i++) {
			ret = fi_av_insert(cxit_av, &test_addrs[i], 1,
				&test_fi_addrs[i], 0, NULL);
			/* Should have inserted 1 item   */
			cr_assert(ret == 1,
				"fi_av_insert() iter=%d, idx=%d, ret=%d\n",
				j, i, ret);
			/* Returned tokens should match insertion order   */
			cr_assert(test_fi_addrs[i] == i,
				"fi_av_insert() iter=%d, idx=%d, index=%ld\n",
				j, i, test_fi_addrs[i]);
		}

		/* Lookup addresses   */
		for (i = 0; i < count; i++) {
			addrlen = sizeof(struct cxip_addr);
			ret = fi_av_lookup(cxit_av, test_fi_addrs[i], &addr,
				&addrlen);
			/* Should succeed   */
			cr_assert(ret == FI_SUCCESS,
				"fi_av_lookup() iter=%d, idx=%d, ret=%d",
				j, i, ret);
			/* Address should match what we expect   */
			cr_assert(addr.nic == test_addrs[i].nic,
				"fi_av_lookup() iter=%d, count=%d, i=%d, index=%ld, nic=%d, exp=%d",
				j, count, i, test_fi_addrs[i], addr.nic,
				test_addrs[i].nic);
			cr_assert(addr.pid == test_addrs[i].pid,
				"fi_av_lookup() iter=%d, idx=%d, pid=%d",
				j, i, addr.pid);
		}

		/* Spot-check. If we remove an arbitrary entry, and then insert
		 * a new address, it should always fill the hole left by the
		 * removal.
		 */

		/* Remove an arbitrary item in the middle   */
		i = count / 2;
		ret = fi_av_remove(cxit_av, &test_fi_addrs[i], 1, 0);
		cr_assert(ret == FI_SUCCESS,
			"fi_av_remove() mid iter=%d, idx=%d, ret=%d\n",
			j, i, ret);

		/* Insert an address   */
		ret = fi_av_insert(cxit_av, &test_addrs[i], 1,
			&test_fi_addrs[i], 0, NULL);
		cr_assert(ret == 1,
			"fi_av_insert() mid iter=%d, idx=%d, ret=%d\n",
			j, i, ret);
		cr_assert(test_fi_addrs[i] == i,
			"fi_av_insert() mid iter=%d, idx=%d, index=%ld\n",
			j, i, test_fi_addrs[i]);

		addrlen = sizeof(struct cxip_addr);
		ret = fi_av_lookup(cxit_av, test_fi_addrs[i], &addr,
			&addrlen);
		cr_assert(ret == FI_SUCCESS,
			"fi_av_lookup() mid iter=%d, idx=%d, ret=%d",
			j, i, ret);
		cr_assert(addr.nic == test_addrs[i].nic,
			"fi_av_lookup() mid iter=%d, count=%d, i=%d, index=%ld, nic=%d, exp=%d",
			j, count, i, test_fi_addrs[i], addr.nic,
			test_addrs[i].nic);
		cr_assert(addr.pid == test_addrs[i].pid,
			"fi_av_lookup() mid iter=%d, idx=%d, pid=%d",
			j, i, addr.pid);

		/* Remove all of the entries   */
		for (i = 0; i < count; i++) {
			ret = fi_av_remove(cxit_av, &test_fi_addrs[i], 1, 0);
			/* Should succeed   */
			cr_assert(ret == 0,
				"fi_av_remove() iter=%d, idx=%d, ret=%d",
				j, i, ret);
		}
	}

	test_addrs_fini();
	cxit_destroy_av();
}

/* Wrapper for insert test.
 *
 * The first call in each group only fills half of the initially allocated
 * space.
 *
 * The second call fills the entire initially allocated space.
 *
 * The third call requires multiple memory reallocations to expand the memory as
 * this inserts.
 */
static void
test_insert(void)
{
	int iters = 1;

	__test_insert(AV_COUNT / 2, iters);
	__test_insert(AV_COUNT, iters);
	__test_insert(naddrs, iters);

	iters = 3;

	__test_insert(AV_COUNT / 2, iters);
	__test_insert(AV_COUNT, iters);
	__test_insert(naddrs, iters);
}

TestSuite(av, .init = cxit_setup_av, .fini = cxit_teardown_av,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

ReportHook(TEST_CRASH)(struct criterion_test_stats *stats)
{
	printf("signal = %d\n", stats->signal);
}

/* Test AV creation syntax error */
Test(av, av_open_invalid)
{
	int ret;

	ret = fi_av_open(cxit_domain, NULL, NULL, NULL);
	cr_assert(ret == -FI_EINVAL, "fi_av_open AV all NULL = %d", ret);

	ret = fi_av_open(cxit_domain, &cxit_av_attr, NULL, NULL);
	cr_assert(ret == -FI_EINVAL, "fi_av_open AV NULL av = %d", ret);

	ret = fi_av_open(cxit_domain, NULL, &cxit_av, NULL);
	cr_assert(ret == -FI_EINVAL, "fi_av_open AV NULL av_attr = %d", ret);

	cxit_av_attr.type = 99;
	ret = fi_av_open(cxit_domain, &cxit_av_attr, &cxit_av, NULL);
	cr_assert(ret == -FI_EINVAL, "fi_av_open AV bad type = %d", ret);
	cxit_av_attr.type = 0;

	/* NOTE: FI_READ means read-only */
	cxit_av_attr.flags = FI_READ;
	ret = fi_av_open(cxit_domain, &cxit_av_attr, &cxit_av, NULL);
	cr_assert(ret == -FI_EINVAL, "fi_av_open AV FI_READ with no name = %d",
		ret);
	cxit_av_attr.flags = 0;

	cxit_av_attr.rx_ctx_bits = CXIP_EP_MAX_CTX_BITS + 1;
	ret = fi_av_open(cxit_domain, &cxit_av_attr, &cxit_av, NULL);
	cr_assert(ret == -FI_EINVAL, "fi_av_open AV too many bits = %d", ret);
	cxit_av_attr.rx_ctx_bits = 0;
}

/* Test AV bind not supported */
Test(av, av_bind_invalid)
{
	int ret;

	cxit_create_av();

	ret = fi_av_bind(cxit_av, NULL, 0);
	cr_assert(ret == -FI_ENOSYS, "fi_av_bind() = %d", ret);

	cxit_destroy_av();
}

/* Test AV control not supported */
Test(av, av_control_invalid)
{
	int ret;

	cxit_create_av();

	ret = fi_control(&cxit_av->fid, 0, NULL);
	cr_assert(ret == -FI_ENOSYS, "fi_control() = %d", ret);

	cxit_destroy_av();
}

/* Test AV open_ops not supported */
Test(av, av_open_ops_invalid)
{
	int ret;

	cxit_create_av();

	ret = fi_open_ops(&cxit_av->fid, NULL, 0, NULL, NULL);
	cr_assert(ret == -FI_ENOSYS, "fi_open_ops() = %d", ret);

	cxit_destroy_av();
}

/* Test basic AV table creation */
Test(av, table_create)
{
	cxit_av_attr.type = FI_AV_TABLE;
	test_create(0);
	test_create(1024);
}

/* Test basic AV map creation */
Test(av, map_create)
{
	cxit_av_attr.type = FI_AV_MAP;
	test_create(0);
	test_create(1024);
}

/* Test basic AV default creation */
Test(av, unspecified_create)
{
	cxit_av_attr.type = FI_AV_UNSPEC;
	test_create(0);
	test_create(1024);
}

/* Test basic AV table insert */
Test(av, table_insert)
{
	cxit_av_attr.count = AV_COUNT;
	cxit_av_attr.type = FI_AV_TABLE;
	naddrs = cxit_av_attr.count * 10;

	test_insert();
}

/* Test basic AV map insert */
Test(av, map_insert)
{
	cxit_av_attr.count = AV_COUNT;
	cxit_av_attr.type = FI_AV_MAP;
	naddrs = cxit_av_attr.count * 10;

	test_insert();
}

/* Test address conversion to string */
Test(av, straddr)
{
	uint32_t addr = 0xabcd1234;
	size_t len = 0;
	char *buf = NULL;
	const char *tmp_buf;

	cxit_create_av();

	tmp_buf = fi_av_straddr(cxit_av, &addr, buf, &len);
	cr_assert_null(tmp_buf, "fi_av_straddr() buffer not null %p", tmp_buf);

	buf = malloc(len);
	cr_assert(buf != NULL);

	tmp_buf = fi_av_straddr(cxit_av, &addr, buf, &len);
	cr_assert_not_null(tmp_buf, "fi_av_straddr() buffer is null");
	cr_assert_str_eq(tmp_buf, buf,
		"fi_av_straddr() buffer failure: '%s' != '%s'", tmp_buf, buf);

	free(buf);

	cxit_destroy_av();
}

Test(av, insertsvc)
{
	int i, ret;
	struct cxip_addr addr;
	size_t addrlen;
	char pid_str[256];

	cxit_create_av();
	test_addrs_init();

	ret = fi_av_insertsvc(cxit_av, NULL, pid_str, &test_fi_addrs[0], 0,
			      NULL);
	cr_assert(ret == -FI_EINVAL);

	ret = fi_av_insertsvc(cxit_av, nic_to_amac(test_addrs[0].nic), NULL,
			      &test_fi_addrs[0], 0, NULL);
	cr_assert(ret == -FI_EINVAL);

	ret = fi_av_insertsvc(cxit_av, NULL, NULL, &test_fi_addrs[0], 0, NULL);
	cr_assert(ret == -FI_EINVAL);

	/* Insert addresses   */
	for (i = 0; i < naddrs; i++) {
		ret = sprintf(pid_str, "%d", test_addrs[i].pid);
		cr_assert(ret > 0);

		ret = fi_av_insertsvc(cxit_av, nic_to_amac(test_addrs[i].nic),
				      pid_str, &test_fi_addrs[i], 0, NULL);
		/* Should have inserted 1 item   */
		cr_assert(ret == 1,
			"fi_av_insertsvc() idx=%d, ret=%d\n",
			i, ret);
		/* Returned tokens should match insertion order   */
		cr_assert(test_fi_addrs[i] == i,
			"fi_av_insertsvc() idx=%d, fi_addr=%ld\n",
			i, test_fi_addrs[i]);
	}

	/* Lookup addresses   */
	for (i = 0; i < naddrs; i++) {
		addrlen = sizeof(struct cxip_addr);
		ret = fi_av_lookup(cxit_av, test_fi_addrs[i], &addr,
			&addrlen);
		/* Should succeed   */
		cr_assert(ret == FI_SUCCESS,
			"fi_av_lookup() idx=%d, ret=%d",
			i, ret);
		/* Address should match what we expect   */
		cr_assert(addr.nic == test_addrs[i].nic,
			"fi_av_lookup() naddrs=%d, i=%d, index=%ld, nic=%d, exp=%d",
			naddrs, i, test_fi_addrs[i], addr.nic,
			test_addrs[i].nic);
		cr_assert(addr.pid == test_addrs[i].pid,
			"fi_av_lookup() idx=%d, pid=%d",
			i, addr.pid);
	}

	/* Spot-check. If we remove an arbitrary entry, and then insert
	 * a new address, it should always fill the hole left by the
	 * removal.
	 */

	/* Remove an arbitrary item in the middle   */
	i = naddrs / 2;
	ret = fi_av_remove(cxit_av, &test_fi_addrs[i], 1, 0);
	cr_assert(ret == FI_SUCCESS,
		"fi_av_remove() mid idx=%d, ret=%d\n",
		i, ret);

	/* Insert an address   */
	ret = fi_av_insert(cxit_av, &test_addrs[i], 1,
		&test_fi_addrs[i], 0, NULL);
	cr_assert(ret == 1,
		"fi_av_insert() mid idx=%d, ret=%d\n",
		i, ret);
	cr_assert(test_fi_addrs[i] == i,
		"fi_av_insert() mid idx=%d, index=%ld\n",
		i, test_fi_addrs[i]);

	addrlen = sizeof(struct cxip_addr);
	ret = fi_av_lookup(cxit_av, test_fi_addrs[i], &addr,
		&addrlen);
	cr_assert(ret == FI_SUCCESS,
		"fi_av_lookup() mid idx=%d, ret=%d",
		i, ret);
	cr_assert(addr.nic == test_addrs[i].nic,
		"fi_av_lookup() mid naddrs=%d, i=%d, index=%ld, nic=%d, exp=%d",
		naddrs, i, test_fi_addrs[i], addr.nic,
		test_addrs[i].nic);
	cr_assert(addr.pid == test_addrs[i].pid,
		"fi_av_lookup() mid idx=%d, pid=%d",
		i, addr.pid);

	/* Remove all of the entries   */
	for (i = 0; i < naddrs; i++) {
		ret = fi_av_remove(cxit_av, &test_fi_addrs[i], 1, 0);
		/* Should succeed   */
		cr_assert(ret == 0,
			"fi_av_remove() idx=%d, ret=%d",
			i, ret);
	}

	test_addrs_fini();
	cxit_destroy_av();
}

static double diff_timespec(const struct timespec *time1,
			    const struct timespec *time0) {
	return (time1->tv_sec - time0->tv_sec) +
		(time1->tv_nsec - time0->tv_nsec) / 1000000000.0;
}

/* Verify that reserve lookup is O(1). */
Test(av, reverse_lookup)
{
	int i;
	int ret;
	struct cxip_av *av;
	struct cxip_addr addr = {};
	struct timespec start;
	struct timespec end;
	double timestamp1;
	double timestamp2;
	fi_addr_t fi_addr;

	cxit_create_av();

	av = container_of(cxit_av, struct cxip_av, av_fid.fid);

	/* Insert lots of addresses into the AV. */
	for (i = 0; i < 10000; i++) {
		addr.nic = i;

		ret = fi_av_insert(cxit_av, &addr, 1, NULL, 0, NULL);
		cr_assert_eq(ret, 1, "fi_av_insert failed: %d", ret);
	}

	/* Verify that reserve lookup is not linear. Verify this by the
	 * addresses being within 5% of each other.
	 */
	addr.nic = 0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	fi_addr = cxip_av_lookup_fi_addr(av, &addr);
	clock_gettime(CLOCK_MONOTONIC, &end);

	cr_assert_neq(fi_addr, FI_ADDR_NOTAVAIL,
		      "cxip_av_lookup_fi_addr failed");
	timestamp1 = diff_timespec(&end, &start);

	addr.nic = i - 1;
	clock_gettime(CLOCK_MONOTONIC, &start);
	fi_addr = cxip_av_lookup_fi_addr(av, &addr);
	clock_gettime(CLOCK_MONOTONIC, &end);

	cr_assert_neq(fi_addr, FI_ADDR_NOTAVAIL,
		      "cxip_av_lookup_fi_addr failed");
	timestamp2 = diff_timespec(&end, &start);

	cr_assert((timestamp1 * 1.05) > timestamp2, "O(1) verification failed");

	cxit_destroy_av();
}

Test(av, av_user_id_invalid_insert_with_symmetric)
{
	int ret;
	struct cxip_addr addr = {};;
	fi_addr_t fi_addr = 0;

	cxit_av_attr.flags |= FI_SYMMETRIC;
	cxit_create_av();

	ret = fi_av_insert(cxit_av, &addr, 1, &fi_addr, FI_AV_USER_ID, NULL);
	cr_assert_eq(ret, -FI_EINVAL, "Bad fi_av_insert rc: %d", ret);

	cxit_destroy_av();
}

Test(av, av_user_id_invalid_null_fi_addr)
{
	int ret;
	struct cxip_addr addr = {};;

	cxit_create_av();

	ret = fi_av_insert(cxit_av, &addr, 1, NULL, FI_AV_USER_ID, NULL);
	cr_assert_eq(ret, -FI_EINVAL, "Bad fi_av_insert rc: %d", ret);

	cxit_destroy_av();
}

Test(av, invalid_fi_av_user_id_flag)
{
	int ret;
	struct cxip_addr addr = {};;
	fi_addr_t fi_addr = 0;

	cxit_av_attr.flags = FI_AV_USER_ID;
	cxit_create_av();

	ret = fi_av_insert(cxit_av, &addr, 1, &fi_addr, FI_AV_USER_ID, NULL);
	cr_assert_eq(ret, -FI_EINVAL, "Bad fi_av_insert rc: %d", ret);

	cxit_destroy_av();
}
