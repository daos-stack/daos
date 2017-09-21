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
#include "gurt/path.h"
#include "gurt/list.h"
#include "gurt/heap.h"
#include "gurt/dlog.h"

static char *__cwd;
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

static void __test_norm_in_place(const char *origin,
				const char *exp_result)
{
	char	*path;
	int	ret;

	path = strdup(origin);

	assert_non_null(path);

	strcpy(path, origin);

	ret = d_normalize_in_place(path);
	if (ret != 0)
		free(path);

	assert_int_equal(ret, 0);

	ret = strcmp(path, exp_result);
	free(path);

	assert_int_equal(ret, 0);
}

void test_normalize_in_place(void **state)
{
	__test_norm_in_place("/foo/bar/", "/foo/bar/");
	__test_norm_in_place("/foo/bar", "/foo/bar");
	__test_norm_in_place("/foo/./", "/foo/");
	__test_norm_in_place("/foo/../", "/foo/../");
	__test_norm_in_place("/.foo", "/.foo");
	__test_norm_in_place("///foo/.//bar/", "/foo/bar/");
	__test_norm_in_place("/foo/.../", "/foo/.../");
	__test_norm_in_place("/foo//.//.//.//..", "/foo/..");
	__test_norm_in_place("foo/bar", "foo/bar");
	__test_norm_in_place("foo./bar", "foo./bar");
	__test_norm_in_place("foo/.bar", "foo/.bar");
	__test_norm_in_place("foo./.bar", "foo./.bar");
	__test_norm_in_place(".././/////././.foo", "../.foo");
	__test_norm_in_place("/", "/");
	__test_norm_in_place("..", "..");
	__test_norm_in_place("///////", "/");
	__test_norm_in_place("/././././", "/");
	__test_norm_in_place("../../../", "../../../");
	__test_norm_in_place(".././.././.././", "../../../");
	__test_norm_in_place("../../..", "../../..");
	__test_norm_in_place("...", "...");

	/* NOTE: we don't test ./foo on purpose. ./ is not normalized
	 * correctly but relative paths are not supported
	 */
}

static char *
create_string(const char *format, ...)
{
	int	ret;
	char	*str;
	va_list	ap;

	va_start(ap, format);
	ret = vasprintf(&str, format, ap);
	va_end(ap);

	if (ret == -1)
		return NULL;

	return str;
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


void test_prepend_cwd(void **state)
{
	const char	*value;
	char		*expected;
	char		*checked;
	int		ret;

	/* Should return an error */
	ret = d_prepend_cwd(NULL, &checked);

	assert_int_equal(ret, -DER_INVAL);

	assert_null(checked);

	/* This should be unchanged.   It's already
	 * a valid prefix path
	 */
	value = "////foo bar//fub";
	ret = d_prepend_cwd(value, &checked);
	assert_int_equal(ret, 0);
	assert_null(checked);

	/* The current directory prepended.
	 */
	value = "bar/fub";
	ret = d_prepend_cwd(value, &checked);
	assert_int_equal(ret, 0);
	assert_non_null(checked);
	expected = create_string("%s/%s", __cwd, value);
	assert_non_null(expected);
	assert_string_equal(expected, checked);
	free(checked);
	free(expected);
}

void test_check_directory(void **state)
{
	int	ret;
	char	*path;
	char	*new_path;
	char	*path2;

	ret = d_check_directory(__root, NULL, false);
	assert_int_equal(ret, 0);

	ret = d_check_directory("/bar/foo", NULL, false);
	assert_int_equal(ret, -DER_BADPATH);

	path = create_string("%s/SConstruct", __cwd);
	assert_non_null(path);
	ret = d_check_directory(path, NULL, false);
	assert_int_equal(ret, -DER_NOTDIR);

	ret = d_check_directory(path, NULL, true);
	assert_int_equal(ret, -DER_NOTDIR);
	free(path);

	path = create_string("%s/utest/dir1/dir2", __cwd);
	assert_non_null(path);
	ret = d_check_directory(path, NULL, true);
	assert_int_equal(ret, 0);
	free(path);

	path = create_string("%s////foobar", __root);
	assert_non_null(path);
	path2 = create_string("%s/foobar", __root);
	assert_non_null(path2);
	rmdir(path);
	ret = d_check_directory(path, &new_path, true);
	assert_int_equal(ret, 0);
	assert_int_equal(rmdir(path), 0);
	assert_string_equal(path2, new_path);
	free(path);
	free(path2);
	free(new_path);
}

#define NUM_DIRS 5
void test_create_subdirs(void **state)
{
	int		ret;
	char		*dir;
	char		*pos;
	char		*path;
	int		i;
	const char	*dirs[NUM_DIRS] = {"fub", "bob", "long/path/name",
		"long/path", "long"};

	ret = d_create_subdirs("/usr/lib64/libc.so", "", &path);
	assert_int_equal(ret, -DER_NOTDIR);
	assert_null(path);

	ret = d_create_subdirs(__root, "", &path);
	assert_int_equal(ret, 0);
	assert_string_equal(path, __root);
	free(path);

	dir = create_string("%s/foo/bar#               ", __root);
	assert_non_null(dir);
	pos = strrchr(dir, '#');
	*pos = 0;

	/* Remove the directories if they exist */
	for (i = 0; i < NUM_DIRS; i++) {
		*pos = '/';
		strcpy(pos+1, dirs[i]);
		rmdir(dir);
	}
	/* Remove bar and foo if they exist */
	*pos = 0;
	rmdir(dir);
	*(pos - 4) = 0;
	rmdir(dir);
	*(pos - 4) = '/';

	*pos = 0;
	ret = d_create_subdirs(__root, "foo/bar", &path);
	assert_int_equal(ret, 0);
	assert_non_null(path);
	assert_string_equal(path, dir);
	free(path);

	/* Create the directories */
	for (i = 0; i < NUM_DIRS; i++) {
		*pos = 0;
		ret = d_create_subdirs(dir, dirs[i], &path);
		assert_int_equal(ret, 0);
		*pos = '/';
		strcpy(pos+1, dirs[i]);
		assert_string_equal(path, dir);
		free(path);
	}

	/* Do it one more time */
	for (i = 0; i < NUM_DIRS; i++) {
		*pos = 0;
		ret = d_create_subdirs(dir, dirs[i], &path);
		assert_int_equal(ret, 0);
		*pos = '/';
		strcpy(pos+1, dirs[i]);
		assert_string_equal(path, dir);
		free(path);
	}

	/* Remove the directories */
	for (i = 0; i < NUM_DIRS; i++) {
		*pos = '/';
		strcpy(pos+1, dirs[i]);
		assert_int_equal(rmdir(dir), 0);
	}

	/* Create a file */
	strcpy(pos, "/bob");
	open(dir, O_CREAT|O_WRONLY, 0600);
	*pos = 0;
	ret = d_create_subdirs(dir, "bob", &path);
	*pos = '/';
	assert_int_equal(ret, -DER_NOTDIR);
	assert_null(path);
	unlink(dir);

	/* Test a directory that user can't write */
	ret = d_create_subdirs("/usr/lib", "cppr_test_path", &path);
	assert_int_equal(ret, -DER_NO_PERM);
	assert_null(path);

	/* Remove bar and foo */
	*pos = 0;
	assert_int_equal(rmdir(dir), 0);
	*(pos - 4) = 0;
	assert_int_equal(rmdir(dir), 0);

	free(dir);
}

static int
init_tests(void **state)
{
	char	*tmp;

	__root = strdup("/tmp/XXXXXX");
	tmp = mkdtemp(__root);

	if (tmp != __root) {
		fprintf(stderr, "Could not create tmp dir");
		return -1;
	}

	__cwd = d_getcwd();

	if (__cwd == NULL)
		return -1;

	return 0;
}

static int
fini_tests(void **state)
{
	rmdir(__root);
	free(__cwd);
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
		D_FREE(entry, sizeof(*entry));
	}

	d_list_for_each_entry_continue(entry, &head2, link) {
		assert_int_equal(i, entry->num);
		i++;
	}

	d_list_for_each_entry_safe(entry, tentry, &head2, link) {
		d_list_del(&entry->link);
		D_FREE(entry, sizeof(*entry));
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
		D_FREE(entry, sizeof(*entry));
	}

	dhlist_for_each_entry_continue(entry, pos, link) {
		assert_int_equal(i, entry->num);
		i++;
	}

	dhlist_for_each_entry_safe(entry, pos, temp, &head2, link) {
		d_hlist_del(&entry->link);
		D_FREE(entry, sizeof(*entry));
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

	setenv("CRT_LOG_MASK", "CLOG=DEBUG,T1=DEBUG", 1);
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
	setenv("CRT_LOG_MASK", "T1=D0", 1);
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


	setenv("CRT_LOG_MASK", "T1=D4", 1);
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

	setenv("CRT_LOG_MASK", "T1=D0xACF", 1);
	d_log_sync_mask();

	rc = d_log_getmasks(retbuf, 0, 200, 0);
	LOG_DEBUG(logfac1, "log mask: %s\n\n", retbuf);
	memset(retbuf, 0x00, sizeof(retbuf));

	setenv("CRT_LOG_MASK", "T1=D0xACFFF", 1);
	d_log_sync_mask();

	rc = d_log_getmasks(retbuf, 0, 200, 0);
	LOG_DEBUG(logfac1, "log mask: %s\n\n", retbuf);
	memset(retbuf, 0x00, sizeof(retbuf));

	setenv("CRT_LOG_MASK", "T1=DEBUG", 1);
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

int
main(int argc, char **argv)
{
	const struct CMUnitTest	tests[] = {
		cmocka_unit_test(test_time),
		cmocka_unit_test(test_create_subdirs),
		cmocka_unit_test(test_check_directory),
		cmocka_unit_test(test_d_errstr),
		cmocka_unit_test(test_prepend_cwd),
		cmocka_unit_test(test_normalize_in_place),
		cmocka_unit_test(test_gurt_list),
		cmocka_unit_test(test_gurt_hlist),
		cmocka_unit_test(test_binheap),
		cmocka_unit_test(test_log),
	};

	return cmocka_run_group_tests(tests, init_tests, fini_tests);
}
