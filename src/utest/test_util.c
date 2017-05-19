/* Copyright (C) 2016 Intel Corporation
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
 * This file tests macros in crt_util/queue.h
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "utest_cmocka.h"
#include "crt_util/common.h"
#include "crt_util/path.h"
#include "crt_util/list.h"
#include "crt_util/sysqueue.h"
#include "crt_util/heap.h"
#include "crt_util/clog.h"

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
	crt_timeinc(&t1, NSEC_PER_SEC + 1);

	assert_int_equal(t1.tv_sec, 2);
	assert_int_equal(t1.tv_nsec, 2);

	t2.tv_sec = 0;
	t2.tv_nsec = 0;
	assert_int_equal(crt_timediff_ns(&t2, &t1),
			 (NSEC_PER_SEC * 2) + 2);

	t2.tv_sec = 2;
	t2.tv_nsec = 2 + NSEC_PER_USEC;
	assert(crt_time2us(crt_timediff(t1, t2)) == 1.0);
	assert(crt_time2us(crt_timediff(t2, t1)) == -1.0);

	t2.tv_nsec = 2 + NSEC_PER_MSEC;
	assert(crt_time2ms(crt_timediff(t1, t2)) == 1.0);
	assert(crt_time2ms(crt_timediff(t2, t1)) == -1.0);

	t2.tv_sec = 3;
	t2.tv_nsec = 2;
	assert(crt_time2s(crt_timediff(t1, t2)) == 1.0);
	assert(crt_time2s(crt_timediff(t2, t1)) == -1.0);

	t2.tv_sec = 2;
	t2.tv_nsec = 2;
	assert_int_equal(crt_timediff_ns(&t2, &t1), 0);

	t2.tv_sec = 3;
	t2.tv_nsec = 2;
	assert_int_equal(crt_timediff_ns(&t2, &t1), -NSEC_PER_SEC);

	t2.tv_sec = 2;
	t2.tv_nsec = 3;
	assert_int_equal(crt_timediff_ns(&t2, &t1), -1);

	t2.tv_nsec = 1;
	assert_int_equal(crt_timediff_ns(&t2, &t1),
			 1);

	crt_timeinc(&t1, 100000);

	assert_int_equal(t1.tv_sec, 2);
	assert_int_equal(t1.tv_nsec, 100002);

	crt_gettime(&t1);
	crt_timeinc(&t1, NSEC_PER_SEC / 10);

	timeleft = crt_timeleft_ns(&t1);
	/* This check shouldn't take 1 second */
	assert_in_range(timeleft, 0, NSEC_PER_SEC);

	/* Sleep for 1 second.  Time should expire */
	sleep(1);

	timeleft = crt_timeleft_ns(&t1);
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

	ret = crt_normalize_in_place(path);
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

void test_prepend_cwd(void **state)
{
	const char	*value;
	char		*expected;
	char		*checked;
	int		ret;

	/* Should return an error */
	ret = crt_prepend_cwd(NULL, &checked);

	assert_int_equal(ret, -CER_INVAL);
	assert_null(checked);

	/* This should be unchanged.   It's already
	 * a valid prefix path
	 */
	value = "////foo bar//fub";
	ret = crt_prepend_cwd(value, &checked);
	assert_int_equal(ret, 0);
	assert_null(checked);

	/* The current directory prepended.
	 */
	value = "bar/fub";
	ret = crt_prepend_cwd(value, &checked);
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

	ret = crt_check_directory(__root, NULL, false);
	assert_int_equal(ret, 0);

	ret = crt_check_directory("/bar/foo", NULL, false);
	assert_int_equal(ret, -CER_BADPATH);

	path = create_string("%s/SConstruct", __cwd);
	assert_non_null(path);
	ret = crt_check_directory(path, NULL, false);
	assert_int_equal(ret, -CER_NOTDIR);

	ret = crt_check_directory(path, NULL, true);
	assert_int_equal(ret, -CER_NOTDIR);
	free(path);

	path = create_string("%s/utest/dir1/dir2", __cwd);
	assert_non_null(path);
	ret = crt_check_directory(path, NULL, true);
	assert_int_equal(ret, 0);
	free(path);

	path = create_string("%s////foobar", __root);
	assert_non_null(path);
	path2 = create_string("%s/foobar", __root);
	assert_non_null(path2);
	rmdir(path);
	ret = crt_check_directory(path, &new_path, true);
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

	ret = crt_create_subdirs("/usr/lib64/libc.so", "", &path);
	assert_int_equal(ret, -CER_NOTDIR);
	assert_null(path);

	ret = crt_create_subdirs(__root, "", &path);
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
	ret = crt_create_subdirs(__root, "foo/bar", &path);
	assert_int_equal(ret, 0);
	assert_non_null(path);
	assert_string_equal(path, dir);
	free(path);

	/* Create the directories */
	for (i = 0; i < NUM_DIRS; i++) {
		*pos = 0;
		ret = crt_create_subdirs(dir, dirs[i], &path);
		assert_int_equal(ret, 0);
		*pos = '/';
		strcpy(pos+1, dirs[i]);
		assert_string_equal(path, dir);
		free(path);
	}

	/* Do it one more time */
	for (i = 0; i < NUM_DIRS; i++) {
		*pos = 0;
		ret = crt_create_subdirs(dir, dirs[i], &path);
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
	ret = crt_create_subdirs(dir, "bob", &path);
	*pos = '/';
	assert_int_equal(ret, -CER_NOTDIR);
	assert_null(path);
	unlink(dir);

	/* Test a directory that user can't write */
	ret = crt_create_subdirs("/usr/lib", "cppr_test_path", &path);
	assert_int_equal(ret, -CER_NO_PERM);
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

	__cwd = crt_getcwd();

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

static STAILQ_HEAD(stq_head, stq_entry) stq_head =
	STAILQ_HEAD_INITIALIZER(stq_head);

struct stq_entry {
	STAILQ_ENTRY(stq_entry) entries;
	int num;
};

static void
test_stailq_safe(void **state)
{
	int			i;
	struct stq_entry	*item;
	struct stq_entry	*temp;

	for (i = 0; i < 10; i++) {
		item = (struct stq_entry *)malloc(sizeof(struct stq_entry));
		assert_non_null(item);

		item->num = i;
		STAILQ_INSERT_TAIL(&stq_head, item, entries);
	}

	i = 0;
	STAILQ_FOREACH_SAFE(item, &stq_head, entries, temp) {

		assert_int_equal(i, item->num);
		STAILQ_REMOVE(&stq_head, item, stq_entry, entries);
		i++;
		free(item);
	}

	assert_int_not_equal(0, STAILQ_EMPTY(&stq_head));
}

static TAILQ_HEAD(tq_head, tq_entry) tq_head =
	TAILQ_HEAD_INITIALIZER(tq_head);

struct tq_entry {
	TAILQ_ENTRY(tq_entry) entries;
	int num;
};

static void
test_tailq_safe(void **state)
{
	int			i;
	struct tq_entry	*item;
	struct tq_entry	*temp;

	for (i = 0; i < 10; i++) {
		item = (struct tq_entry *)malloc(sizeof(struct tq_entry));
		assert_non_null(item);

		item->num = i;
		TAILQ_INSERT_TAIL(&tq_head, item, entries);
	}

	i = 0;
	TAILQ_FOREACH_SAFE(item, &tq_head, entries, temp) {

		assert_int_equal(i, item->num);
		TAILQ_REMOVE(&tq_head, item, entries);
		i++;
		free(item);
	}

	assert_int_not_equal(0, TAILQ_EMPTY(&tq_head));
}

static SLIST_HEAD(sl_head, sl_entry) sl_head =
	SLIST_HEAD_INITIALIZER(sl_head);

struct sl_entry {
	SLIST_ENTRY(sl_entry) entries;
	int num;
};

static void
test_slist_safe(void **state)
{
	int			i;
	struct sl_entry	*item;
	struct sl_entry	*temp;

	for (i = 0; i < 10; i++) {
		item = (struct sl_entry *)malloc(sizeof(struct sl_entry));
		assert_non_null(item);

		item->num = i;
		SLIST_INSERT_HEAD(&sl_head, item, entries);
	}

	i = 9;
	SLIST_FOREACH_SAFE(item, &sl_head, entries, temp) {

		assert_int_equal(i, item->num);
		SLIST_REMOVE(&sl_head, item, sl_entry, entries);
		i--;
		free(item);
	}

	assert_int_not_equal(0, SLIST_EMPTY(&sl_head));
}

static LIST_HEAD(l_head, l_entry) l_head =
	LIST_HEAD_INITIALIZER(l_head);

struct l_entry {
	LIST_ENTRY(l_entry) entries;
	int num;
};

static void
test_list_safe(void **state)
{
	int			i;
	struct l_entry	*item;
	struct l_entry	*temp;

	for (i = 0; i < 10; i++) {
		item = (struct l_entry *)malloc(sizeof(struct l_entry));
		assert_non_null(item);

		item->num = i;
		LIST_INSERT_HEAD(&l_head, item, entries);
	}

	i = 9;
	LIST_FOREACH_SAFE(item, &l_head, entries, temp) {

		assert_int_equal(i, item->num);
		LIST_REMOVE(item, entries);
		i--;
		free(item);
	}

	assert_int_not_equal(0, LIST_EMPTY(&l_head));
}

struct crt_list_test_entry {
	int num;
	crt_list_t link;
};

static CRT_LIST_HEAD(head1);

#define NUM_ENTRIES 20

static void
assert_list_node_status(void **state, crt_list_t *head, int value, bool in_list)
{
	crt_list_t			*pos;
	struct crt_list_test_entry	*entry;

	crt_list_for_each(pos, head) {
		entry = crt_list_entry(pos, struct crt_list_test_entry, link);
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
assert_list_node_count(void **state, crt_list_t *head, int count)
{
	crt_list_t	*pos;
	int		i = 0;

	crt_list_for_each(pos, head) {
		i++;
	}

	assert_int_equal(i, count);
}

static void
test_crt_list(void **state)
{
	crt_list_t			*pos;
	crt_list_t			*temp;
	crt_list_t			head2;
	crt_list_t			head3;
	struct crt_list_test_entry	*entry;
	struct crt_list_test_entry	*tentry;
	struct crt_list_test_entry	entry2;
	struct crt_list_test_entry	entry3;
	int		i;

	CRT_INIT_LIST_HEAD(&head2);
	CRT_INIT_LIST_HEAD(&head3);

	entry2.num = 2000;
	entry3.num = 3000;
	crt_list_add(&entry3.link, &head3);
	assert(!crt_list_empty(&head3));
	crt_list_splice(&head2, &head3);
	assert(!crt_list_empty(&head3));
	CRT_INIT_LIST_HEAD(&head2);
	crt_list_splice(&head3, &head2);
	assert(!crt_list_empty(&head2));
	crt_list_del(&entry3.link);
	assert(crt_list_empty(&head2));
	CRT_INIT_LIST_HEAD(&head2);
	CRT_INIT_LIST_HEAD(&head3);
	crt_list_add(&entry3.link, &head3);
	crt_list_add(&entry2.link, &head2);
	crt_list_splice(&head3, &head2);
	assert_list_node_count(state, &head2, 2);
	CRT_INIT_LIST_HEAD(&head3);
	crt_list_move(&entry2.link, &head3);
	assert_list_node_status(state, &head3, entry2.num, true);
	assert_list_node_status(state, &head2, entry3.num, true);
	crt_list_move_tail(&entry2.link, &head2);
	assert_list_node_status(state, &head2, entry2.num, true);
	assert_list_node_status(state, &head3, entry2.num, false);

	CRT_INIT_LIST_HEAD(&head2);

	for (i = NUM_ENTRIES * 2 - 1; i >= NUM_ENTRIES; i--) {
		C_ALLOC(entry, sizeof(struct crt_list_test_entry));
		assert_non_null(entry);
		entry->num = i;
		crt_list_add(&entry->link, &head2);
		assert_list_node_status(state, &head2, i, true);

		crt_list_del_init(&entry->link);
		assert(crt_list_empty(&entry->link));
		assert_list_node_status(state, &head2, i, false);

		crt_list_add(&entry->link, &head2);
		assert_list_node_status(state, &head2, i, true);
	}

	for (i = 0; i < NUM_ENTRIES; i++) {
		C_ALLOC(entry, sizeof(struct crt_list_test_entry));
		assert_non_null(entry);
		entry->num = i;
		crt_list_add_tail(&entry->link, &head1);
		assert_list_node_status(state, &head1, i, true);

		crt_list_del(&entry->link);
		assert_list_node_status(state, &head1, i, false);

		crt_list_add_tail(&entry->link, &head1);
		assert_list_node_status(state, &head1, i, true);
	}

	crt_list_splice_init(&head1, &head2);

	assert(crt_list_empty(&head1));
	assert_list_node_count(state, &head2, NUM_ENTRIES * 2);

	i = 0;
	crt_list_for_each_entry(entry, &head2, link) {
		assert_int_equal(i, entry->num);
		i++;
	}

	i = NUM_ENTRIES * 2 - 1;
	crt_list_for_each_entry_reverse(entry, &head2, link) {
		assert_int_equal(i, entry->num);
		i--;
	}

	i = 0;
	crt_list_for_each_safe(pos, temp, &head2) {
		entry = crt_list_entry(pos, struct crt_list_test_entry, link);
		assert_int_equal(i, entry->num);
		i++;
		if (i == NUM_ENTRIES)
			break;
		crt_list_del(pos);
		C_FREE(entry, sizeof(*entry));
	}

	crt_list_for_each_entry_continue(entry, &head2, link) {
		assert_int_equal(i, entry->num);
		i++;
	}

	crt_list_for_each_entry_safe(entry, tentry, &head2, link) {
		crt_list_del(&entry->link);
		C_FREE(entry, sizeof(*entry));
	}

	assert(crt_list_empty(&head2));
}

struct crt_hlist_test_entry {
	int num;
	crt_hlist_node_t link;
};

static CRT_HLIST_HEAD(hhead1);

static void
assert_hlist_node_status(void **state, crt_hlist_head_t *head,
			 int value, bool in_list)
{
	crt_hlist_node_t		*pos;
	struct crt_hlist_test_entry	*entry;

	crt_hlist_for_each(pos, head) {
		entry = crt_hlist_entry(pos, struct crt_hlist_test_entry, link);
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
assert_hlist_node_count(void **state, crt_hlist_head_t *head, int count)
{
	crt_hlist_node_t	*pos;
	int			i = 0;

	crt_hlist_for_each(pos, head) {
		i++;
	}

	assert_int_equal(i, count);
}

static void
test_crt_hlist(void **state)
{
	crt_hlist_node_t		*pos;
	crt_hlist_node_t		*temp;
	crt_hlist_head_t		head2;
	struct crt_hlist_test_entry	*entry;
	struct crt_hlist_test_entry	entry2;
	struct crt_hlist_test_entry	entry3;
	int		i;

	CRT_INIT_HLIST_NODE(&entry2.link);
	CRT_INIT_HLIST_NODE(&entry3.link);
	entry2.num = 2000;
	entry3.num = 3000;
	crt_hlist_add_head(&entry3.link, &hhead1);
	crt_hlist_add_before(&entry2.link, &entry3.link);
	assert(!crt_hlist_empty(&hhead1));
	assert_hlist_node_status(state, &hhead1, entry2.num, true);
	assert_hlist_node_status(state, &hhead1, entry3.num, true);
	assert_hlist_node_count(state, &hhead1, 2);
	assert_non_null(entry2.link.next);
	assert_non_null(entry3.link.pprev);
	assert_int_equal(entry2.link.next, &entry3.link);
	assert_int_equal(entry3.link.pprev, &entry2.link);
	crt_hlist_del_init(&entry2.link);
	assert_hlist_node_status(state, &hhead1, entry2.num, false);
	assert_hlist_node_count(state, &hhead1, 1);
	crt_hlist_add_after(&entry2.link, &entry3.link);
	assert_hlist_node_count(state, &hhead1, 2);
	assert_non_null(entry2.link.pprev);
	assert_non_null(entry3.link.next);
	assert_int_equal(entry3.link.next, &entry2.link);
	assert_int_equal(entry2.link.pprev, &entry3.link);
	assert_hlist_node_status(state, &hhead1, entry2.num, true);
	assert_hlist_node_status(state, &hhead1, entry3.num, true);
	assert_hlist_node_count(state, &hhead1, 2);

	CRT_INIT_HLIST_HEAD(&head2);

	for (i = NUM_ENTRIES - 1; i >= 0; i--) {
		C_ALLOC(entry, sizeof(struct crt_hlist_test_entry));
		assert_non_null(entry);
		entry->num = i;
		crt_hlist_add_head(&entry->link, &head2);
		assert_hlist_node_status(state, &head2, i, true);

		crt_hlist_del_init(&entry->link);
		assert_hlist_node_status(state, &head2, i, false);

		crt_hlist_add_head(&entry->link, &head2);
		assert_hlist_node_status(state, &head2, i, true);
	}

	assert_hlist_node_count(state, &head2, NUM_ENTRIES);

	i = 0;
	crt_hlist_for_each_entry(entry, pos, &head2, link) {
		assert_int_equal(i, entry->num);
		i++;
	}

	i = 0;
	crt_hlist_for_each_safe(pos, temp, &head2) {
		entry = crt_hlist_entry(pos, struct crt_hlist_test_entry, link);
		assert_int_equal(i, entry->num);
		i++;
		if (i == NUM_ENTRIES / 2)
			break;
		crt_hlist_del(pos);
		C_FREE(entry, sizeof(*entry));
	}

	crt_hlist_for_each_entry_continue(entry, pos, link) {
		assert_int_equal(i, entry->num);
		i++;
	}

	crt_hlist_for_each_entry_safe(entry, pos, temp, &head2, link) {
		crt_hlist_del(&entry->link);
		C_FREE(entry, sizeof(*entry));
	}

	assert(crt_hlist_empty(&head2));
}

struct test_minheap_node {
	struct crt_binheap_node		cbh_node;
	int				key;
};

static bool
heap_node_cmp(struct crt_binheap_node *a, struct crt_binheap_node *b)
{
	struct test_minheap_node	*nodea, *nodeb;

	nodea = container_of(a, struct test_minheap_node, cbh_node);
	nodeb = container_of(b, struct test_minheap_node, cbh_node);

	return nodea->key < nodeb->key;
}

static void
test_binheap(void **state)
{
	struct crt_binheap		*h = NULL;
	struct test_minheap_node	 n1, n2, n3;
	struct crt_binheap_node		*n_tmp;
	uint32_t			 size;
	int				 rc;
	struct crt_binheap_ops		 ops = {
		.hop_enter	= NULL,
		.hop_exit	= NULL,
		.hop_compare	= heap_node_cmp,
	};

	(void)state;

	rc = crt_binheap_create(0, 0, NULL, &ops, &h);
	assert_int_equal(rc, 0);
	assert_non_null(h);

	n1.key = 1;
	n2.key = 2;
	n3.key = 3;

	rc = crt_binheap_insert(h, &n1.cbh_node);
	assert_int_equal(rc, 0);
	rc = crt_binheap_insert(h, &n2.cbh_node);
	assert_int_equal(rc, 0);
	rc = crt_binheap_insert(h, &n3.cbh_node);
	assert_int_equal(rc, 0);

	n_tmp = crt_binheap_root(h);
	assert_true(n_tmp == &n1.cbh_node);

	crt_binheap_remove(h, &n1.cbh_node);
	n_tmp = crt_binheap_root(h);
	assert_true(n_tmp == &n2.cbh_node);

	n_tmp = crt_binheap_find(h, 0);
	assert_true(n_tmp == &n2.cbh_node);
	n_tmp = crt_binheap_find(h, 1);
	assert_true(n_tmp == &n3.cbh_node);
	n_tmp = crt_binheap_find(h, 2);
	assert_true(n_tmp == NULL);

	size = crt_binheap_size(h);
	assert_true(size == 2);

	n_tmp = crt_binheap_remove_root(h);
	assert_true(n_tmp == &n2.cbh_node);
	size = crt_binheap_size(h);
	assert_true(size == 1);

	crt_binheap_destroy(h);
}

#define LOG_DEBUG(fac, ...) \
	crt_log(fac | CLOG_DBG, __VA_ARGS__)

#define LOG_INFO(fac, ...) \
	crt_log(fac | CLOG_INFO, __VA_ARGS__)

static void
test_log(void **state)
{
	char *logmask;
	char *allocated_mask = NULL;
	int rc;
	int logfac1;
	int logfac2;
	setenv("CRT_LOG_MASK", "CLOG=DEBUG,T1=DEBUG", 1);
	const char *preset = "D0xF";
	const char *preset1 = "D0xACF";
	char retbuf[200];

	memset(retbuf, 0x00, sizeof(retbuf));
	rc = crt_log_init();
	assert_int_equal(rc, 0);

	logfac1 = crt_log_allocfacility("T1", "TEST1");
	assert_int_not_equal(logfac1, 0);

	logfac2 = crt_log_allocfacility("T2", "TEST2");
	assert_int_not_equal(logfac2, 0);

	LOG_DEBUG(logfac1, "log1 debug should not print\n");
	/* Sync the cart mask */
	crt_log_sync_mask();

	LOG_DEBUG(logfac1, "log1 debug should print\n");
	LOG_DEBUG(logfac2, "log2 debug should not print\n");

	/* Alternatively, a component may have its own mask */
	logmask = getenv("TEST_LOG_MASK");
	if (logmask == NULL)
		logmask = allocated_mask = strdup("ERR,T1=DEBUG,CLOG=DEBUG");
	assert_non_null(logmask);

	rc = crt_log_setmasks(logmask, -1);
	LOG_DEBUG(logfac1, "rc after 1st setmaks is %x\n", rc);
	rc = crt_log_setmasks(logmask, -1);
	LOG_DEBUG(logfac1, "rc after 2nd setmasks is %x\n", rc);
	if (allocated_mask != NULL) {
		free(allocated_mask);
		allocated_mask = NULL;
	}

	crt_log_getmasks(retbuf, 0, 200, 0);
	LOG_DEBUG(logfac1, "log mask: %s\n\n", retbuf);
	memset(retbuf, 0x00, sizeof(retbuf));

	LOG_DEBUG(logfac1, "log1 debug test message %d\n", logfac1);
	LOG_DEBUG(logfac2, "log2 debug test message %d\n", logfac2);
	LOG_INFO(logfac1, "log1 info test message %d\n", logfac2);
	LOG_INFO(logfac2, "log2 info test message %d\n", logfac2);

	logmask = allocated_mask = strdup("T1=D10");
	assert_non_null(logmask);

	rc = crt_log_setmasks(logmask, -1);
	/* should be all f's from earlier */
	assert_int_equal(rc & CLOG_PRIMASK, (0xFFFF00));

	rc = crt_log_setmasks(logmask, -1);
	assert_int_equal(rc & CLOG_PRIMASK, (1 << (CLOG_DPRISHIFT + 10)));
	if (allocated_mask != NULL) {
		free(allocated_mask);
		allocated_mask = NULL;
	}

	/* todo add new test here for the new levels */
	setenv("CRT_LOG_MASK", "T1=D0", 1);
	crt_log_sync_mask();
	logmask = allocated_mask = strdup("T1=D0");
	assert_non_null(logmask);

	rc = crt_log_setmasks(logmask, -1);
	assert_int_equal(rc & CLOG_PRIMASK, (1 << (CLOG_DPRISHIFT + 0)));
	if (allocated_mask != NULL) {
		free(allocated_mask);
		allocated_mask = NULL;
	}

	rc = crt_log_getmasks(retbuf, 0, 200, 0);
	LOG_DEBUG(logfac1, "log mask: %s\n\n", retbuf);
	memset(retbuf, 0x00, sizeof(retbuf));


	setenv("CRT_LOG_MASK", "T1=D4", 1);
	crt_log_sync_mask();

	rc = crt_log_getmasks(retbuf, 0, 200, 0);
	LOG_DEBUG(logfac1, "log mask: %s\n\n", retbuf);
	memset(retbuf, 0x00, sizeof(retbuf));
	logmask = allocated_mask = strdup("T1=D4");
	assert_non_null(logmask);

	rc = crt_log_setmasks(logmask, -1);
	assert_int_equal(rc & CLOG_PRIMASK, (1 << (CLOG_DPRISHIFT + 4)));
	if (allocated_mask != NULL) {
		free(allocated_mask);
		allocated_mask = NULL;
	}

	setenv("CRT_LOG_MASK", "T1=D0xACF", 1);
	crt_log_sync_mask();

	rc = crt_log_getmasks(retbuf, 0, 200, 0);
	LOG_DEBUG(logfac1, "log mask: %s\n\n", retbuf);
	memset(retbuf, 0x00, sizeof(retbuf));

	setenv("CRT_LOG_MASK", "T1=D0xACFFF", 1);
	crt_log_sync_mask();

	rc = crt_log_getmasks(retbuf, 0, 200, 0);
	LOG_DEBUG(logfac1, "log mask: %s\n\n", retbuf);
	memset(retbuf, 0x00, sizeof(retbuf));

	setenv("CRT_LOG_MASK", "T1=DEBUG", 1);
	crt_log_sync_mask();

	rc = crt_log_getmasks(retbuf, 0, 200, 0);
	LOG_DEBUG(logfac1, "log mask: %s\n\n", retbuf);
	memset(retbuf, 0x00, sizeof(retbuf));

	rc = crt_log_str2pri(preset);
	assert_int_equal(rc, 0xF << CLOG_DPRISHIFT);

	rc = crt_log_str2pri(preset1);
	assert_int_equal(rc, 0xACF << CLOG_DPRISHIFT);
	crt_log_fini();
}

int
main(int argc, char **argv)
{
	const struct CMUnitTest	tests[] = {
		cmocka_unit_test(test_time),
		cmocka_unit_test(test_create_subdirs),
		cmocka_unit_test(test_check_directory),
		cmocka_unit_test(test_prepend_cwd),
		cmocka_unit_test(test_normalize_in_place),
		cmocka_unit_test(test_stailq_safe),
		cmocka_unit_test(test_tailq_safe),
		cmocka_unit_test(test_slist_safe),
		cmocka_unit_test(test_list_safe),
		cmocka_unit_test(test_crt_list),
		cmocka_unit_test(test_crt_hlist),
		cmocka_unit_test(test_binheap),
		cmocka_unit_test(test_log),
	};

	return cmocka_run_group_tests(tests, init_tests, fini_tests);
}
