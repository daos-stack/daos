/* Copyright (C) 2016-2017 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This file tests macros in GURT
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "utest_cmocka.h"
#include "gurt/common.h"
#include "gurt/list.h"
#include "gurt/heap.h"
#include "gurt/dlog.h"
#include "gurt/hash.h"

static char *__root;

static void
test_time(void **state)
{
	struct timespec	t1;
	struct timespec	t2;
	uint64_t	timeleft;

	t1.tv_sec = 1;
	t1.tv_nsec = 1;
	d_timeinc(&t1, NSEC_PER_SEC + 1);

	assert_int_equal(t1.tv_sec, 2);
	assert_int_equal(t1.tv_nsec, 2);

	t2.tv_sec = 0;
	t2.tv_nsec = 0;
	assert_int_equal(d_timediff_ns(&t2, &t1),
			 (NSEC_PER_SEC * 2) + 2);

	t2.tv_sec = 2;
	t2.tv_nsec = 2 + NSEC_PER_USEC;
	assert(d_time2us(d_timediff(t1, t2)) == 1.0);
	assert(d_time2us(d_timediff(t2, t1)) == -1.0);

	t2.tv_nsec = 2 + NSEC_PER_MSEC;
	assert(d_time2ms(d_timediff(t1, t2)) == 1.0);
	assert(d_time2ms(d_timediff(t2, t1)) == -1.0);

	t2.tv_sec = 3;
	t2.tv_nsec = 2;
	assert(d_time2s(d_timediff(t1, t2)) == 1.0);
	assert(d_time2s(d_timediff(t2, t1)) == -1.0);

	t2.tv_sec = 2;
	t2.tv_nsec = 2;
	assert_int_equal(d_timediff_ns(&t2, &t1), 0);

	t2.tv_sec = 3;
	t2.tv_nsec = 2;
	assert_int_equal(d_timediff_ns(&t2, &t1), -NSEC_PER_SEC);

	t2.tv_sec = 2;
	t2.tv_nsec = 3;
	assert_int_equal(d_timediff_ns(&t2, &t1), -1);

	t2.tv_nsec = 1;
	assert_int_equal(d_timediff_ns(&t2, &t1),
			 1);

	d_timeinc(&t1, 100000);

	assert_int_equal(t1.tv_sec, 2);
	assert_int_equal(t1.tv_nsec, 100002);

	d_gettime(&t1);
	d_timeinc(&t1, NSEC_PER_SEC / 10);

	timeleft = d_timeleft_ns(&t1);
	/* This check shouldn't take 1 second */
	assert_in_range(timeleft, 0, NSEC_PER_SEC);

	/* Sleep for 1 second.  Time should expire */
	sleep(1);

	timeleft = d_timeleft_ns(&t1);
	assert_int_equal(timeleft, 0);
}

#define D_CHECK_STRLIMITS(name, base)				\
	do {							\
		value = d_errstr(-DER_ERR_##name##_BASE);	\
		assert_string_equal(value, "DER_UNKNOWN");	\
		value = d_errstr(-DER_ERR_##name##_LIMIT);	\
		assert_string_equal(value, "DER_UNKNOWN");	\
		value = d_errstr(DER_ERR_##name##_BASE);	\
		assert_string_equal(value, "DER_UNKNOWN");	\
		value = d_errstr(DER_ERR_##name##_LIMIT);	\
		assert_string_equal(value, "DER_UNKNOWN");	\
	} while (0);

#define D_CHECK_ERR_IN_RANGE(name, value)			\
	do {							\
		const char	*str = d_errstr(name);		\
		assert_string_not_equal(str, "DER_UNKNOWN");	\
	} while (0);

#define D_CHECK_IN_RANGE(name, base)		\
	D_FOREACH_##name##_ERR(D_CHECK_ERR_IN_RANGE)

void test_d_errstr(void **state)
{
	const char	*value;

	D_FOREACH_ERR_RANGE(D_CHECK_STRLIMITS)
	D_FOREACH_ERR_RANGE(D_CHECK_IN_RANGE)
	value = d_errstr(-DER_INVAL);
	assert_string_equal(value, "DER_INVAL");
	value = d_errstr(DER_INVAL);
	assert_string_equal(value, "DER_INVAL");
	value = d_errstr(5000000);
	assert_string_equal(value, "DER_UNKNOWN");
	value = d_errstr(3);
	assert_string_equal(value, "DER_UNKNOWN");
	value = d_errstr(-3);
	assert_string_equal(value, "DER_UNKNOWN");
	value = d_errstr(0);
	assert_string_equal(value, "DER_SUCCESS");
	value = d_errstr(DER_SUCCESS);
	assert_string_equal(value, "DER_SUCCESS");
	value = d_errstr(-DER_IVCB_FORWARD);
	assert_string_equal(value, "DER_IVCB_FORWARD");
	value = d_errstr(-DER_FREE_MEM);
	assert_string_equal(value, "DER_FREE_MEM");
	value = d_errstr(-DER_STALE);
	assert_string_equal(value, "DER_STALE");
}

static int
init_tests(void **state)
{
	char		*tmp;
	unsigned int	 seed;

	__root = strdup("/tmp/XXXXXX");
	tmp = mkdtemp(__root);

	if (tmp != __root) {
		fprintf(stderr, "Could not create tmp dir\n");
		return -1;
	}

	/* Seed the random number generator once per test run */
	seed = time(NULL);
	fprintf(stdout, "Seeding this test run with seed=%u\n", seed);
	srand(seed);

	return 0;
}

static int
fini_tests(void **state)
{
	rmdir(__root);
	free(__root);

	return 0;
}

struct d_list_test_entry {
	int num;
	d_list_t link;
};

static D_LIST_HEAD(head1);

#define NUM_ENTRIES 20

static void
assert_list_node_status(void **state, d_list_t *head, int value, bool in_list)
{
	d_list_t			*pos;
	struct d_list_test_entry	*entry;

	d_list_for_each(pos, head) {
		entry = d_list_entry(pos, struct d_list_test_entry, link);
		if (entry->num == value) {
			if (in_list)
				return;
			assert(0);
		}
	}

	if (in_list)
		assert(0);
}

static void
assert_list_node_count(void **state, d_list_t *head, int count)
{
	d_list_t	*pos;
	int		i = 0;

	d_list_for_each(pos, head) {
		i++;
	}

	assert_int_equal(i, count);
}

static void
test_gurt_list(void **state)
{
	d_list_t			*pos;
	d_list_t			*temp;
	d_list_t			head2;
	d_list_t			head3;
	struct d_list_test_entry	*entry;
	struct d_list_test_entry	*tentry;
	struct d_list_test_entry	entry2;
	struct d_list_test_entry	entry3;
	int		i;

	D_INIT_LIST_HEAD(&head2);
	D_INIT_LIST_HEAD(&head3);

	entry2.num = 2000;
	entry3.num = 3000;
	d_list_add(&entry3.link, &head3);
	assert(!d_list_empty(&head3));
	d_list_splice(&head2, &head3);
	assert(!d_list_empty(&head3));
	D_INIT_LIST_HEAD(&head2);
	d_list_splice(&head3, &head2);
	assert(!d_list_empty(&head2));
	d_list_del(&entry3.link);
	assert(d_list_empty(&head2));
	D_INIT_LIST_HEAD(&head2);
	D_INIT_LIST_HEAD(&head3);
	d_list_add(&entry3.link, &head3);
	d_list_add(&entry2.link, &head2);
	d_list_splice(&head3, &head2);
	assert_list_node_count(state, &head2, 2);
	D_INIT_LIST_HEAD(&head3);
	d_list_move(&entry2.link, &head3);
	assert_list_node_status(state, &head3, entry2.num, true);
	assert_list_node_status(state, &head2, entry3.num, true);
	d_list_move_tail(&entry2.link, &head2);
	assert_list_node_status(state, &head2, entry2.num, true);
	assert_list_node_status(state, &head3, entry2.num, false);

	D_INIT_LIST_HEAD(&head2);

	for (i = NUM_ENTRIES * 2 - 1; i >= NUM_ENTRIES; i--) {
		D_ALLOC(entry, sizeof(struct d_list_test_entry));
		assert_non_null(entry);
		entry->num = i;
		d_list_add(&entry->link, &head2);
		assert_list_node_status(state, &head2, i, true);

		d_list_del_init(&entry->link);
		assert(d_list_empty(&entry->link));
		assert_list_node_status(state, &head2, i, false);

		d_list_add(&entry->link, &head2);
		assert_list_node_status(state, &head2, i, true);
	}

	for (i = 0; i < NUM_ENTRIES; i++) {
		D_ALLOC(entry, sizeof(struct d_list_test_entry));
		assert_non_null(entry);
		entry->num = i;
		d_list_add_tail(&entry->link, &head1);
		assert_list_node_status(state, &head1, i, true);

		d_list_del(&entry->link);
		assert_list_node_status(state, &head1, i, false);

		d_list_add_tail(&entry->link, &head1);
		assert_list_node_status(state, &head1, i, true);
	}

	d_list_splice_init(&head1, &head2);

	assert(d_list_empty(&head1));
	assert_list_node_count(state, &head2, NUM_ENTRIES * 2);

	i = 0;
	d_list_for_each_entry(entry, &head2, link) {
		assert_int_equal(i, entry->num);
		i++;
	}

	i = NUM_ENTRIES * 2 - 1;
	d_list_for_each_entry_reverse(entry, &head2, link) {
		assert_int_equal(i, entry->num);
		i--;
	}

	i = 0;
	d_list_for_each_safe(pos, temp, &head2) {
		entry = d_list_entry(pos, struct d_list_test_entry, link);
		assert_int_equal(i, entry->num);
		i++;
		if (i == NUM_ENTRIES)
			break;
		d_list_del(pos);
		D_FREE(entry);
	}

	d_list_for_each_entry_continue(entry, &head2, link) {
		assert_int_equal(i, entry->num);
		i++;
	}

	d_list_for_each_entry_safe(entry, tentry, &head2, link) {
		d_list_del(&entry->link);
		D_FREE(entry);
	}

	assert(d_list_empty(&head2));
}

struct dhlist_test_entry {
	int num;
	d_hlist_node_t link;
};

static D_HLIST_HEAD(hhead1);

static void
assert_hlist_node_status(void **state, d_hlist_head_t *head,
			 int value, bool in_list)
{
	d_hlist_node_t		*pos;
	struct dhlist_test_entry	*entry;

	dhlist_for_each(pos, head) {
		entry = d_hlist_entry(pos, struct dhlist_test_entry, link);
		if (entry->num == value) {
			if (in_list)
				return;
			assert(0);
		}
	}

	if (in_list)
		assert(0);
}

static void
assert_hlist_node_count(void **state, d_hlist_head_t *head, int count)
{
	d_hlist_node_t	*pos;
	int			i = 0;

	dhlist_for_each(pos, head) {
		i++;
	}

	assert_int_equal(i, count);
}

static void
test_gurt_hlist(void **state)
{
	d_hlist_node_t		*pos;
	d_hlist_node_t		*temp;
	d_hlist_head_t		head2;
	struct dhlist_test_entry	*entry;
	struct dhlist_test_entry	entry2;
	struct dhlist_test_entry	entry3;
	int		i;

	D_INIT_HLIST_NODE(&entry2.link);
	D_INIT_HLIST_NODE(&entry3.link);
	entry2.num = 2000;
	entry3.num = 3000;
	d_hlist_add_head(&entry3.link, &hhead1);
	d_hlist_add_before(&entry2.link, &entry3.link);
	assert(!d_hlist_empty(&hhead1));
	assert_hlist_node_status(state, &hhead1, entry2.num, true);
	assert_hlist_node_status(state, &hhead1, entry3.num, true);
	assert_hlist_node_count(state, &hhead1, 2);
	assert_non_null(entry2.link.next);
	assert_non_null(entry3.link.pprev);
	assert_int_equal(entry2.link.next, &entry3.link);
	assert_int_equal(entry3.link.pprev, &entry2.link);
	d_hlist_del_init(&entry2.link);
	assert_hlist_node_status(state, &hhead1, entry2.num, false);
	assert_hlist_node_count(state, &hhead1, 1);
	d_hlist_add_after(&entry2.link, &entry3.link);
	assert_hlist_node_count(state, &hhead1, 2);
	assert_non_null(entry2.link.pprev);
	assert_non_null(entry3.link.next);
	assert_int_equal(entry3.link.next, &entry2.link);
	assert_int_equal(entry2.link.pprev, &entry3.link);
	assert_hlist_node_status(state, &hhead1, entry2.num, true);
	assert_hlist_node_status(state, &hhead1, entry3.num, true);
	assert_hlist_node_count(state, &hhead1, 2);

	D_INIT_HLIST_HEAD(&head2);

	for (i = NUM_ENTRIES - 1; i >= 0; i--) {
		D_ALLOC(entry, sizeof(struct dhlist_test_entry));
		assert_non_null(entry);
		entry->num = i;
		d_hlist_add_head(&entry->link, &head2);
		assert_hlist_node_status(state, &head2, i, true);

		d_hlist_del_init(&entry->link);
		assert_hlist_node_status(state, &head2, i, false);

		d_hlist_add_head(&entry->link, &head2);
		assert_hlist_node_status(state, &head2, i, true);
	}

	assert_hlist_node_count(state, &head2, NUM_ENTRIES);

	i = 0;
	dhlist_for_each_entry(entry, pos, &head2, link) {
		assert_int_equal(i, entry->num);
		i++;
	}

	i = 0;
	dhlist_for_each_safe(pos, temp, &head2) {
		entry = d_hlist_entry(pos, struct dhlist_test_entry, link);
		assert_int_equal(i, entry->num);
		i++;
		if (i == NUM_ENTRIES / 2)
			break;
		d_hlist_del(pos);
		D_FREE(entry);
	}

	dhlist_for_each_entry_continue(entry, pos, link) {
		assert_int_equal(i, entry->num);
		i++;
	}

	dhlist_for_each_entry_safe(entry, pos, temp, &head2, link) {
		d_hlist_del(&entry->link);
		D_FREE(entry);
	}

	assert(d_hlist_empty(&head2));
}

struct test_minheap_node {
	struct d_binheap_node		dbh_node;
	int				key;
};

static bool
heap_node_cmp(struct d_binheap_node *a, struct d_binheap_node *b)
{
	struct test_minheap_node	*nodea, *nodeb;

	nodea = container_of(a, struct test_minheap_node, dbh_node);
	nodeb = container_of(b, struct test_minheap_node, dbh_node);

	return nodea->key < nodeb->key;
}

static void
test_binheap(void **state)
{
	struct d_binheap		*h = NULL;
	struct test_minheap_node	 n1, n2, n3;
	struct d_binheap_node		*n_tmp;
	uint32_t			 size;
	int				 rc;
	struct d_binheap_ops		 ops = {
		.hop_enter	= NULL,
		.hop_exit	= NULL,
		.hop_compare	= heap_node_cmp,
	};

	(void)state;

	rc = d_binheap_create(0, 0, NULL, &ops, &h);
	assert_int_equal(rc, 0);
	assert_non_null(h);

	n1.key = 1;
	n2.key = 2;
	n3.key = 3;

	rc = d_binheap_insert(h, &n1.dbh_node);
	assert_int_equal(rc, 0);
	rc = d_binheap_insert(h, &n2.dbh_node);
	assert_int_equal(rc, 0);
	rc = d_binheap_insert(h, &n3.dbh_node);
	assert_int_equal(rc, 0);

	n_tmp = d_binheap_root(h);
	assert_true(n_tmp == &n1.dbh_node);

	d_binheap_remove(h, &n1.dbh_node);
	n_tmp = d_binheap_root(h);
	assert_true(n_tmp == &n2.dbh_node);

	n_tmp = d_binheap_find(h, 0);
	assert_true(n_tmp == &n2.dbh_node);
	n_tmp = d_binheap_find(h, 1);
	assert_true(n_tmp == &n3.dbh_node);
	n_tmp = d_binheap_find(h, 2);
	assert_true(n_tmp == NULL);

	size = d_binheap_size(h);
	assert_true(size == 2);

	n_tmp = d_binheap_remove_root(h);
	assert_true(n_tmp == &n2.dbh_node);
	size = d_binheap_size(h);
	assert_true(size == 1);

	d_binheap_destroy(h);
}

#define LOG_DEBUG(fac, ...) \
	d_log(fac | DLOG_DBG, __VA_ARGS__)

#define LOG_INFO(fac, ...) \
	d_log(fac | DLOG_INFO, __VA_ARGS__)

static void
test_log(void **state)
{
	char *logmask;
	char *allocated_mask = NULL;
	int rc;
	int logfac1;
	int logfac2;
	const char *preset = "D0xF";
	const char *preset1 = "D0xACF";
	char retbuf[1024];

	setenv("D_LOG_MASK", "CLOG=DEBUG,T1=DEBUG", 1);
	memset(retbuf, 0x00, sizeof(retbuf));
	rc = d_log_init();
	assert_int_equal(rc, 0);

	logfac1 = d_log_allocfacility("T1", "TEST1");
	assert_int_not_equal(logfac1, 0);

	logfac2 = d_log_allocfacility("T2", "TEST2");
	assert_int_not_equal(logfac2, 0);

	LOG_DEBUG(logfac1, "log1 debug should not print\n");
	/* Sync the cart mask */
	d_log_sync_mask();

	LOG_DEBUG(logfac1, "log1 debug should print\n");
	LOG_DEBUG(logfac2, "log2 debug should not print\n");

	/* Alternatively, a component may have its own mask */
	logmask = getenv("TEST_LOG_MASK");
	if (logmask == NULL)
		logmask = allocated_mask = strdup("ERR,T1=DEBUG,CLOG=DEBUG");
	assert_non_null(logmask);

	rc = d_log_setmasks(logmask, -1);
	LOG_DEBUG(logfac1, "rc after 1st setmaks is %x\n", rc);
	rc = d_log_setmasks(logmask, -1);
	LOG_DEBUG(logfac1, "rc after 2nd setmasks is %x\n", rc);
	if (allocated_mask != NULL) {
		free(allocated_mask);
		allocated_mask = NULL;
	}

	d_log_getmasks(retbuf, 0, 1024, 0);
	LOG_DEBUG(logfac1, "log mask: %s\n\n", retbuf);
	memset(retbuf, 0x00, sizeof(retbuf));

	LOG_DEBUG(logfac1, "log1 debug test message %d\n", logfac1);
	LOG_DEBUG(logfac2, "log2 debug test message %d\n", logfac2);
	LOG_INFO(logfac1, "log1 info test message %d\n", logfac2);
	LOG_INFO(logfac2, "log2 info test message %d\n", logfac2);

	logmask = allocated_mask = strdup("T1=D10");
	assert_non_null(logmask);

	rc = d_log_setmasks(logmask, -1);
	/* should be all f's from earlier */
	assert_int_equal(rc & DLOG_PRIMASK, (0xFFFF00));

	rc = d_log_setmasks(logmask, -1);
	assert_int_equal(rc & DLOG_PRIMASK, (1 << (DLOG_DPRISHIFT + 10)));
	if (allocated_mask != NULL) {
		free(allocated_mask);
		allocated_mask = NULL;
	}

	/* todo add new test here for the new levels */
	setenv("D_LOG_MASK", "T1=D0", 1);
	d_log_sync_mask();
	logmask = allocated_mask = strdup("T1=D0");
	assert_non_null(logmask);

	rc = d_log_setmasks(logmask, -1);
	assert_int_equal(rc & DLOG_PRIMASK, (1 << (DLOG_DPRISHIFT + 0)));
	if (allocated_mask != NULL) {
		free(allocated_mask);
		allocated_mask = NULL;
	}

	rc = d_log_getmasks(retbuf, 0, 200, 0);
	LOG_DEBUG(logfac1, "log mask: %s\n\n", retbuf);
	memset(retbuf, 0x00, sizeof(retbuf));


	setenv("D_LOG_MASK", "T1=D4", 1);
	d_log_sync_mask();

	rc = d_log_getmasks(retbuf, 0, 200, 0);
	LOG_DEBUG(logfac1, "log mask: %s\n\n", retbuf);
	memset(retbuf, 0x00, sizeof(retbuf));
	logmask = allocated_mask = strdup("T1=D4");
	assert_non_null(logmask);

	rc = d_log_setmasks(logmask, -1);
	assert_int_equal(rc & DLOG_PRIMASK, (1 << (DLOG_DPRISHIFT + 4)));
	if (allocated_mask != NULL) {
		free(allocated_mask);
		allocated_mask = NULL;
	}

	setenv("D_LOG_MASK", "T1=D0xACF", 1);
	d_log_sync_mask();

	rc = d_log_getmasks(retbuf, 0, 200, 0);
	LOG_DEBUG(logfac1, "log mask: %s\n\n", retbuf);
	memset(retbuf, 0x00, sizeof(retbuf));

	setenv("D_LOG_MASK", "T1=D0xACFFF", 1);
	d_log_sync_mask();

	rc = d_log_getmasks(retbuf, 0, 200, 0);
	LOG_DEBUG(logfac1, "log mask: %s\n\n", retbuf);
	memset(retbuf, 0x00, sizeof(retbuf));

	setenv("D_LOG_MASK", "T1=DEBUG", 1);
	d_log_sync_mask();

	rc = d_log_getmasks(retbuf, 0, 200, 0);
	LOG_DEBUG(logfac1, "log mask: %s\n\n", retbuf);
	memset(retbuf, 0x00, sizeof(retbuf));

	rc = d_log_str2pri(preset);
	assert_int_equal(rc, 0xF << DLOG_DPRISHIFT);

	rc = d_log_str2pri(preset1);
	assert_int_equal(rc, 0xACF << DLOG_DPRISHIFT);
	d_log_fini();
}

#define TEST_GURT_HASH_NUM_BITS (16)
#define TEST_GURT_HASH_NUM_ENTRIES (1 << TEST_GURT_HASH_NUM_BITS)
#define TEST_GURT_HASH_KEY_LEN (65L)

struct test_hash_entry {
	int		tl_ref;
	d_list_t	tl_link;
	unsigned char	tl_key[TEST_GURT_HASH_KEY_LEN];
};

static struct test_hash_entry *
test_gurt_hash_link2ptr(d_list_t *link)
{
	return container_of(link, struct test_hash_entry, tl_link);
}

static bool
test_gurt_hash_op_key_cmp(struct d_chash_table *thtab, d_list_t *link,
			  const void *key, unsigned int ksize)
{
	struct test_hash_entry *tlink = test_gurt_hash_link2ptr(link);

	assert_int_equal(ksize, TEST_GURT_HASH_KEY_LEN);
	return !memcmp(tlink->tl_key, key, ksize);
}

static void
test_gurt_hash_op_rec_addref(struct d_chash_table *thtab, d_list_t *link)
{
	struct test_hash_entry *tlink = test_gurt_hash_link2ptr(link);

	tlink->tl_ref++;
}

static bool
test_gurt_hash_op_rec_decref(struct d_chash_table *thtab, d_list_t *link)
{
	struct test_hash_entry *tlink = test_gurt_hash_link2ptr(link);

	tlink->tl_ref--;

	return tlink->tl_ref == 0;
}

static void
test_gurt_hash_op_rec_free(struct d_chash_table *thtab, d_list_t *link)
{
	struct test_hash_entry *tlink = test_gurt_hash_link2ptr(link);

	D_FREE(tlink);
}

static d_chash_table_ops_t th_ops = {
	.hop_key_cmp    = test_gurt_hash_op_key_cmp,
};

int
test_gurt_hash_empty_traverse_cb(d_list_t *rlink, void *arg)
{
	/* No nodes should exist for empty table */
	assert_true(0);

	return 0;
}

static struct test_hash_entry **
test_gurt_hash_alloc_items(int num_entries)
{
	struct test_hash_entry	**entries;
	int			  i;
	ssize_t			  j;

	D_ALLOC_ARRAY(entries, num_entries);
	assert_non_null(entries);

	/* Allocate each member of the array */
	for (i = 0; i < num_entries; i++) {
		D_ALLOC_PTR(entries[i]);
		assert_non_null(entries[i]);

		/* Generate a random key */
		j = 0;
		while (j < TEST_GURT_HASH_KEY_LEN) {
			entries[i]->tl_key[j] = rand() & 0xFF;
			j++;
		}

		/*
		 * Last four bytes are used for key index to make sure
		 * keys are unique (little-endian)
		 */
		entries[i]->tl_key[TEST_GURT_HASH_KEY_LEN - 4] = (i & 0xFF);
		entries[i]->tl_key[TEST_GURT_HASH_KEY_LEN - 3] =
			((i >> 8) & 0xFF);
		entries[i]->tl_key[TEST_GURT_HASH_KEY_LEN - 2] =
			((i >> 16) & 0xFF);
		entries[i]->tl_key[TEST_GURT_HASH_KEY_LEN - 1] =
			((i >> 24) & 0xFF);
	}

	return entries;
}

static void
test_gurt_hash_free_items(struct test_hash_entry **entries, int num_entries)
{
	int i;

	if (entries == NULL)
		return;

	for (i = 0; i < num_entries; i++)
		D_FREE(entries[i]);

	D_FREE(entries);
}

static void
test_gurt_hash_empty(void **state)
{
	/* Just test the minimum-size hash table */
	const int		  num_bits = 1;
	struct d_chash_table	 *thtab;
	int			  rc;
	struct test_hash_entry	**entries;
	d_list_t		 *test;
	int			  i;

	/* Allocate test entries to use */
	entries = test_gurt_hash_alloc_items(TEST_GURT_HASH_NUM_ENTRIES);
	assert_non_null(entries);

	/* Create a minimum-size hash table */
	rc = d_chash_table_create(0, num_bits, NULL, &th_ops, &thtab);
	assert_int_equal(rc, 0);

	/* Traverse the empty hash table and look for entries */
	rc = d_chash_table_traverse(thtab, test_gurt_hash_empty_traverse_cb,
				    NULL);
	assert_int_equal(rc, 0);

	/* Get the first element in the table, which should be NULL */
	assert_null(d_chash_rec_first(thtab));

	/* Try to look up the random entries and make sure they fail */
	for (i = 0; i < TEST_GURT_HASH_NUM_ENTRIES; i++) {
		test = d_chash_rec_find(thtab, entries[i]->tl_key,
					TEST_GURT_HASH_KEY_LEN);
		assert_null(test);
	}

	/* Destroy the hash table, force = false (should fail if not empty) */
	rc = d_chash_table_destroy(thtab, 0);
	assert_int_equal(rc, 0);

	/* Free the temporary keys */
	test_gurt_hash_free_items(entries, TEST_GURT_HASH_NUM_ENTRIES);
}

static d_chash_table_ops_t th_ops_ref = {
	.hop_key_cmp	= test_gurt_hash_op_key_cmp,
	.hop_rec_addref	= test_gurt_hash_op_rec_addref,
	.hop_rec_decref	= test_gurt_hash_op_rec_decref,
	.hop_rec_free	= test_gurt_hash_op_rec_free,
};

/* Check that addref/decref work with D_HASH_FT_EPHEMERAL
 */
static void
test_gurt_hash_decref(void **state)
{
	/* Just test the minimum-size hash table */
	const int		  num_bits = 1;
	struct d_chash_table	 *thtab;
	int			  rc;
	struct test_hash_entry	 *entry;
	d_list_t		 *test;

	D_ALLOC_PTR(entry);

	/* Create a minimum-size hash table */
	rc = d_chash_table_create(D_HASH_FT_EPHEMERAL, num_bits, NULL,
				  &th_ops_ref, &thtab);
	assert_int_equal(rc, 0);

	rc = d_chash_rec_insert(thtab, entry->tl_key, TEST_GURT_HASH_KEY_LEN,
				&entry->tl_link, true);
	assert_int_equal(rc, 0);

	/* No ref should be taken on insert */
	assert_int_equal(entry->tl_ref, 0);

	/* This insert should fail */
	rc = d_chash_rec_insert(thtab, entry->tl_key, TEST_GURT_HASH_KEY_LEN,
				&entry->tl_link, true);
	assert_int_equal(rc, -DER_EXIST);

	/* One ref should be taken by find */
	test = d_chash_rec_find(thtab, entry->tl_key, TEST_GURT_HASH_KEY_LEN);
	assert_non_null(test);
	assert_ptr_equal(test, &entry->tl_link);
	assert_int_equal(entry->tl_ref, 1);

	/* Take two more refs */
	d_chash_rec_addref(thtab, test);
	assert_int_equal(entry->tl_ref, 2);
	d_chash_rec_addref(thtab, test);
	assert_int_equal(entry->tl_ref, 3);

	/* Drop one ref */
	rc = d_chash_rec_ndecref(thtab, 1, test);
	assert_int_equal(rc, 0);
	assert_int_equal(entry->tl_ref, 2);

	/* Drop 20 refs, which should fail but remove and free the descriptor */
	rc = d_chash_rec_ndecref(thtab, 20, test);
	assert_int_equal(rc, -DER_INVAL);

	/* Get the first element in the table, which should be NULL */
	assert_null(d_chash_rec_first(thtab));

	/* Destroy the hash table, force = false (should fail if not empty) */
	rc = d_chash_table_destroy(thtab, 0);
	assert_int_equal(rc, 0);
}

int
main(int argc, char **argv)
{
	const struct CMUnitTest	tests[] = {
		cmocka_unit_test(test_time),
		cmocka_unit_test(test_d_errstr),
		cmocka_unit_test(test_gurt_list),
		cmocka_unit_test(test_gurt_hlist),
		cmocka_unit_test(test_binheap),
		cmocka_unit_test(test_log),
		cmocka_unit_test(test_gurt_hash_empty),
		cmocka_unit_test(test_gurt_hash_decref),
	};

	return cmocka_run_group_tests(tests, init_tests, fini_tests);
}
