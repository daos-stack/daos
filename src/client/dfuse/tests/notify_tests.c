/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <stdio.h>
#include <cmocka.h>

#include <daos/common.h>
#include <daos/debug.h>

/* DFUSE_NOTIFY_RAW_OK is defined inside inval.c before it pulls in dfuse.h, so the poison
 * pragma there never fires for this TU; these stand in for the real fuse session calls.
 */
static int stub_notify_rc;
static int stub_inval_entry_calls;
static int stub_delete_calls;
static int stub_inval_inode_calls;

#include "../inval.c"

int
fuse_lowlevel_notify_inval_entry(struct fuse_session *se, fuse_ino_t parent, const char *name,
				 size_t namelen)
{
	stub_inval_entry_calls++;
	return stub_notify_rc;
}

int
fuse_lowlevel_notify_expire_entry(struct fuse_session *se, fuse_ino_t parent, const char *name,
				  size_t namelen)
{
	return stub_notify_rc;
}

int
fuse_lowlevel_notify_delete(struct fuse_session *se, fuse_ino_t parent, fuse_ino_t child,
			    const char *name, size_t namelen)
{
	stub_delete_calls++;
	return stub_notify_rc;
}

int
fuse_lowlevel_notify_inval_inode(struct fuse_session *se, fuse_ino_t ino, off_t off, off_t len)
{
	stub_inval_inode_calls++;
	return stub_notify_rc;
}

/* Eviction sweep is out of scope here; ival_loop() is compiled but never invoked. */
bool
dfuse_dentry_get_valid(struct dfuse_inode_entry *ie, double max_age, double *timeout)
{
	return false;
}

static struct dfuse_info tnotify_info;

static void
tnotify_reset(void)
{
	memset(&tnotify_info, 0, sizeof(tnotify_info));
	memset(&ival_data, 0, sizeof(ival_data));
	memset(&notify_data, 0, sizeof(notify_data));
	notify_queued          = 0;
	notify_stop            = false;
	notify_congest_total   = 0;
	stub_notify_rc         = 0;
	stub_inval_entry_calls = 0;
	stub_delete_calls      = 0;
	stub_inval_inode_calls = 0;

	assert_int_equal(d_slab_init(&tnotify_info.di_slab, &tnotify_info), -DER_SUCCESS);
	assert_int_equal(ival_init(&tnotify_info), 0);
	assert_int_equal(notify_init(&tnotify_info), -DER_SUCCESS);
}

static int
tnotify_setup(void **state)
{
	tnotify_reset();
	return 0;
}

static int
tnotify_teardown(void **state)
{
	D_MUTEX_LOCK(&notify_lock);
	notify_drain_locked();
	D_MUTEX_UNLOCK(&notify_lock);

	ival_fini();
	d_slab_destroy(&tnotify_info.di_slab);
	return 0;
}

static void
test_coalesce_duplicate(void **state)
{
	notify_enqueue(&tnotify_info, NOTIFY_INVAL_ENTRY, 10, 0, "foo");
	assert_int_equal(notify_queued, 1);
	assert_int_equal(atomic_load_relaxed(&tnotify_info.di_notify_enqueued), 1);

	notify_enqueue(&tnotify_info, NOTIFY_INVAL_ENTRY, 10, 0, "foo");
	assert_int_equal(notify_queued, 1);
	assert_int_equal(atomic_load_relaxed(&tnotify_info.di_notify_coalesced), 1);
	assert_int_equal(atomic_load_relaxed(&tnotify_info.di_notify_enqueued), 1);
}

static void
test_coalesce_key_fields_differ(void **state)
{
	notify_enqueue(&tnotify_info, NOTIFY_INVAL_ENTRY, 10, 0, "foo");
	notify_enqueue(&tnotify_info, NOTIFY_INVAL_INODE, 10, 0, "foo");
	assert_int_equal(notify_queued, 2);

	notify_enqueue(&tnotify_info, NOTIFY_INVAL_ENTRY, 11, 0, "foo");
	assert_int_equal(notify_queued, 3);

	notify_enqueue(&tnotify_info, NOTIFY_INVAL_ENTRY, 10, 0, "bar");
	assert_int_equal(notify_queued, 4);

	assert_int_equal(atomic_load_relaxed(&tnotify_info.di_notify_coalesced), 0);
}

/* Same parent+name but different child ino: the kernel verifies child identity on delete, so
 * collapsing these would silently drop one of the two deletions (design-review finding).
 */
static void
test_coalesce_delete_distinct_child_ino(void **state)
{
	notify_enqueue(&tnotify_info, NOTIFY_DELETE, 10, 100, "foo");
	notify_enqueue(&tnotify_info, NOTIFY_DELETE, 10, 200, "foo");

	assert_int_equal(notify_queued, 2);
	assert_int_equal(atomic_load_relaxed(&tnotify_info.di_notify_coalesced), 0);
}

static void
test_pop_before_delivery_not_coalesced(void **state)
{
	struct notify_entry *ne;

	notify_enqueue(&tnotify_info, NOTIFY_INVAL_ENTRY, 10, 0, "foo");

	D_MUTEX_LOCK(&notify_lock);
	ne = notify_dequeue_locked();
	D_MUTEX_UNLOCK(&notify_lock);
	assert_non_null(ne);
	d_slab_release(notify_slab, ne);

	notify_enqueue(&tnotify_info, NOTIFY_INVAL_ENTRY, 10, 0, "foo");
	assert_int_equal(notify_queued, 1);
	assert_int_equal(atomic_load_relaxed(&tnotify_info.di_notify_coalesced), 0);
	assert_int_equal(atomic_load_relaxed(&tnotify_info.di_notify_enqueued), 2);
}

static void
test_backstop_drop_at_max(void **state)
{
	int i;

	for (i = 0; i < NOTIFY_QUEUE_MAX; i++)
		notify_enqueue(&tnotify_info, NOTIFY_INVAL_INODE, 0, i + 1, NULL);

	assert_int_equal(notify_queued, NOTIFY_QUEUE_MAX);
	assert_int_equal(atomic_load_relaxed(&tnotify_info.di_notify_dropped), 0);

	notify_enqueue(&tnotify_info, NOTIFY_INVAL_INODE, 0, NOTIFY_QUEUE_MAX + 1, NULL);

	assert_int_equal(notify_queued, NOTIFY_QUEUE_MAX);
	assert_int_equal(atomic_load_relaxed(&tnotify_info.di_notify_dropped), 1);
}

static void
test_stop_latch_drops(void **state)
{
	notify_stop = true;

	notify_enqueue(&tnotify_info, NOTIFY_INVAL_ENTRY, 10, 0, "foo");

	assert_int_equal(notify_queued, 0);
	assert_int_equal(atomic_load_relaxed(&tnotify_info.di_notify_dropped), 1);
	assert_int_equal(atomic_load_relaxed(&tnotify_info.di_notify_enqueued), 0);
}

static void
test_congest_warn_silent_below_threshold(void **state)
{
	fuse_ino_t ino = 1;

	for (; notify_queued < NOTIFY_CONGEST_HIGH - 1; ino++)
		notify_enqueue(&tnotify_info, NOTIFY_INVAL_INODE, 0, ino, NULL);

	assert_int_equal(notify_congest_total, 0);
}

static void
test_congest_warn_at_and_above_threshold(void **state)
{
	fuse_ino_t ino = 1;

	for (; notify_queued < NOTIFY_CONGEST_HIGH; ino++)
		notify_enqueue(&tnotify_info, NOTIFY_INVAL_INODE, 0, ino, NULL);
	assert_int_equal(notify_congest_total, 1);

	notify_enqueue(&tnotify_info, NOTIFY_INVAL_INODE, 0, ino++, NULL);
	assert_int_equal(notify_congest_total, 2);
}

static void
test_delivery_rc_enoent_ignored(void **state)
{
	struct notify_entry *ne;

	notify_enqueue(&tnotify_info, NOTIFY_INVAL_ENTRY, 10, 0, "foo");

	D_MUTEX_LOCK(&notify_lock);
	ne = notify_dequeue_locked();
	D_MUTEX_UNLOCK(&notify_lock);
	assert_non_null(ne);

	stub_notify_rc = -ENOENT;
	notify_run(ne);
	d_slab_release(notify_slab, ne);

	assert_false(atomic_load_relaxed(&ival_data.session_dead));
	assert_int_equal(atomic_load_relaxed(&tnotify_info.di_notify_delivered), 1);
	assert_int_equal(atomic_load_relaxed(&tnotify_info.di_notify_dropped), 0);
}

static void
test_delivery_rc_ebadf_latches_and_drains(void **state)
{
	struct notify_entry *ne;

	notify_enqueue(&tnotify_info, NOTIFY_INVAL_ENTRY, 10, 0, "foo");
	notify_enqueue(&tnotify_info, NOTIFY_INVAL_ENTRY, 11, 0, "bar");

	D_MUTEX_LOCK(&notify_lock);
	ne = notify_dequeue_locked();
	D_MUTEX_UNLOCK(&notify_lock);
	assert_non_null(ne);

	stub_notify_rc = -EBADF;
	notify_run(ne);
	d_slab_release(notify_slab, ne);

	assert_true(atomic_load_relaxed(&ival_data.session_dead));
	assert_int_equal(atomic_load_relaxed(&tnotify_info.di_notify_dropped), 1);

	/* Session dead: the delivery thread drains whatever remains queued */
	D_MUTEX_LOCK(&notify_lock);
	notify_drain_locked();
	D_MUTEX_UNLOCK(&notify_lock);

	assert_int_equal(notify_queued, 0);
	assert_int_equal(atomic_load_relaxed(&tnotify_info.di_notify_dropped), 2);
}

static void
test_hash_same_key_same_bucket(void **state)
{
	uint32_t b1 = notify_hash(NOTIFY_DELETE, 42, 7, "somefile", 8);
	uint32_t b2 = notify_hash(NOTIFY_DELETE, 42, 7, "somefile", 8);

	assert_int_equal(b1, b2);
}

#define HASH_SAMPLE_COUNT 4096

static void
test_hash_distribution_smoke(void **state)
{
	static unsigned int counts[NOTIFY_COALESCE_BUCKETS];
	unsigned int        max_count = 0;
	int                 i;

	memset(counts, 0, sizeof(counts));

	for (i = 0; i < HASH_SAMPLE_COUNT; i++) {
		char     name[32];
		uint32_t bucket;

		snprintf(name, sizeof(name), "file-%d", i);
		bucket = notify_hash(NOTIFY_INVAL_ENTRY, i, 0, name, strlen(name));
		counts[bucket]++;
		if (counts[bucket] > max_count)
			max_count = counts[bucket];
	}

	/* Loose FNV-1a smoke check: average is 1/bucket here, no bucket should be swamped */
	assert_true(max_count < 20);
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
	    cmocka_unit_test_setup_teardown(test_coalesce_duplicate, tnotify_setup,
					    tnotify_teardown),
	    cmocka_unit_test_setup_teardown(test_coalesce_key_fields_differ, tnotify_setup,
					    tnotify_teardown),
	    cmocka_unit_test_setup_teardown(test_coalesce_delete_distinct_child_ino, tnotify_setup,
					    tnotify_teardown),
	    cmocka_unit_test_setup_teardown(test_pop_before_delivery_not_coalesced, tnotify_setup,
					    tnotify_teardown),
	    cmocka_unit_test_setup_teardown(test_backstop_drop_at_max, tnotify_setup,
					    tnotify_teardown),
	    cmocka_unit_test_setup_teardown(test_stop_latch_drops, tnotify_setup, tnotify_teardown),
	    cmocka_unit_test_setup_teardown(test_congest_warn_silent_below_threshold, tnotify_setup,
					    tnotify_teardown),
	    cmocka_unit_test_setup_teardown(test_congest_warn_at_and_above_threshold, tnotify_setup,
					    tnotify_teardown),
	    cmocka_unit_test_setup_teardown(test_delivery_rc_enoent_ignored, tnotify_setup,
					    tnotify_teardown),
	    cmocka_unit_test_setup_teardown(test_delivery_rc_ebadf_latches_and_drains,
					    tnotify_setup, tnotify_teardown),
	    cmocka_unit_test_setup_teardown(test_hash_same_key_same_bucket, tnotify_setup,
					    tnotify_teardown),
	    cmocka_unit_test_setup_teardown(test_hash_distribution_smoke, tnotify_setup,
					    tnotify_teardown),
	};
	int rc;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0)
		return rc;

	rc = cmocka_run_group_tests_name("dfuse notify queue", tests, NULL, NULL);

	daos_debug_fini();

	return rc;
}
