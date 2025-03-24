/**
 * (C) Copyright 2024-2025 Intel Corporation.
 * (C) Copyright 2024-2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Unit test for shared memory.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdbool.h>
#include <stdio.h>
#include <getopt.h>

#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <pthread.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/limits.h>

#include <gurt/debug.h>
#include <gurt/shm_alloc.h>
#include <gurt/shm_dict.h>
#include <gurt/shm_utils.h>

#define N_LOOP_MEM (8)

void
test_mem(void **state)
{
	int    i;
	size_t align = 4;
	size_t size;
	char  *buf_list[N_LOOP_MEM];

	srandom(1);
	/* testing allocation with alignment and deallocation */
	for (i = 0; i < N_LOOP_MEM; i++) {
		size        = (size_t)(random() % (120 * 1024));
		buf_list[i] = shm_memalign(align, size);
		assert_non_null(buf_list[i]);
		assert_true((uint64_t)buf_list[i] % align == 0);
		align *= 2;
	}
	for (i = 0; i < N_LOOP_MEM; i++) {
		shm_free(buf_list[i]);
	}

	/* testing allocation without alignment and deallocation */
	for (i = 0; i < N_LOOP_MEM; i++) {
		size        = (size_t)(random() % (120 * 1024));
		buf_list[i] = shm_alloc(size);
		assert_non_null(buf_list[i]);
	}
	for (i = 0; i < N_LOOP_MEM; i++) {
		shm_free(buf_list[i]);
	}
}

#define HT_NAME "shm_ht_test"
#define KEY_1   "key_1"
#define VAL_1   "value_1"
#define KEY_2   "key_2"
#define VAL_2   "value_2"
#define KEY_3   "key_3"
#define VAL_3   "value_3"

void
verify_hash(void)
{
	int                     rc;
	int                     err;
	char                   *value;
	struct d_shm_ht_rec_loc rec_loc;
	struct d_shm_ht_loc     ht_lock;

	/* look up hash key in current process */
	rc = shm_ht_open_with_name(HT_NAME, &ht_lock);
	assert_true(rc == 0);

	value = (char *)shm_ht_rec_find(&ht_lock, KEY_1, strlen(KEY_1), &rec_loc, &err);
	assert_non_null(value);
	assert_true(strcmp(value, VAL_1) == 0);
	assert_true(value == shm_ht_rec_data(&rec_loc, &err));

	rc = shm_ht_rec_decref(&rec_loc);
	assert_true(rc == 0);

	value = (char *)shm_ht_rec_find(&ht_lock, KEY_2, strlen(KEY_2), &rec_loc, &err);
	assert_non_null(value);
	assert_true(strcmp(value, VAL_2) == 0);
	assert_true(value == shm_ht_rec_data(&rec_loc, &err));

	rc = shm_ht_rec_decref(&rec_loc);
	assert_true(rc == 0);

	value = (char *)shm_ht_rec_find(&ht_lock, KEY_3, strlen(KEY_3), &rec_loc, &err);
	assert_non_null(value);
	assert_true(strcmp(value, VAL_3) == 0);
	assert_true(value == shm_ht_rec_data(&rec_loc, &err));

	rc = shm_ht_rec_decref(&rec_loc);
	assert_true(rc == 0);

	shm_ht_decref(&ht_lock);
}

void
verify_hash_by_child(void)
{
	int                     rc;
	int                     err;
	char                   *value;
	struct d_shm_ht_rec_loc rec_loc;
	struct d_shm_ht_loc     ht_lock;

	/* look up hash key in child process */
	rc = shm_ht_open_with_name(HT_NAME, &ht_lock);
	assert_true(rc == 0);

	value = (char *)shm_ht_rec_find(&ht_lock, KEY_1, strlen(KEY_1), &rec_loc, &err);
	assert_non_null(value);
	assert_true(strcmp(value, VAL_1) == 0);
	assert_true(value == shm_ht_rec_data(&rec_loc, &err));

	rc = shm_ht_rec_decref(&rec_loc);
	assert_true(rc == 0);

	value = (char *)shm_ht_rec_find(&ht_lock, KEY_2, strlen(KEY_2), &rec_loc, &err);
	assert_non_null(value);
	assert_true(strcmp(value, VAL_2) == 0);
	assert_true(value == shm_ht_rec_data(&rec_loc, &err));

	rc = shm_ht_rec_decref(&rec_loc);
	assert_true(rc == 0);

	value = (char *)shm_ht_rec_find(&ht_lock, KEY_3, strlen(KEY_3), &rec_loc, &err);
	assert_non_null(value);
	assert_true(strcmp(value, VAL_3) == 0);
	assert_true(value == shm_ht_rec_data(&rec_loc, &err));

	rc = shm_ht_rec_decref(&rec_loc);
	assert_true(rc == 0);

	shm_ht_decref(&ht_lock);
}

#define NUM_KV      (2560)
#define MAX_KEY_LEN (12)
#define N_THREAD    (8)

static void *
thread_ht_op(void *arg)
{
	int                      thread_id;
	int                      i;
	int                      rc;
	int                      len;
	int                      err;
	struct d_shm_ht_loc      ht_loc;
	struct d_shm_ht_rec_loc  rec_loc;
	char                    *value;
	char                    *key_name;
	char                    *key_set = NULL;
	struct d_shm_ht_rec_loc *rec_loc_set;

	thread_id = *(int *)arg;

	rc = shm_ht_open_with_name(HT_NAME, &ht_loc);
	assert_true(rc == 0);

	key_set = malloc(MAX_KEY_LEN * NUM_KV);
	assert_non_null(key_set);
	rec_loc_set = malloc(sizeof(struct d_shm_ht_rec_loc) * NUM_KV);
	assert_non_null(rec_loc_set);

	for (i = 0; i < NUM_KV; i++) {
		key_name = key_set + i * MAX_KEY_LEN;
		len      = snprintf(key_name, MAX_KEY_LEN, "key_%d_%d", thread_id, i);
		assert_true(len < (MAX_KEY_LEN - 1));
		value = shm_ht_rec_find_insert(&ht_loc, key_name, len, VAL_1, sizeof(VAL_1),
					       &rec_loc_set[i], &err);
		assert_non_null(value);
		assert_true(value == shm_ht_rec_data(&rec_loc_set[i], &err));

		rc = shm_ht_rec_decref(&rec_loc_set[i]);
		assert_true(rc == 0);
	}

	/* make sure all inserted records exist */
	for (i = 0; i < NUM_KV; i++) {
		key_name = key_set + i * MAX_KEY_LEN;
		value    = shm_ht_rec_find(&ht_loc, key_name, strlen(key_name), &rec_loc, &err);
		assert_non_null(value);
		assert_non_null(rec_loc.ht_rec);
		assert_true(value == shm_ht_rec_data(&rec_loc, &err));

		rc = shm_ht_rec_decref(&rec_loc);
		assert_true(rc == 0);
	}

	/* delete ht records with shm_ht_rec_delete() */
	for (i = 0; i < NUM_KV; i += 2) {
		key_name = key_set + i * MAX_KEY_LEN;
		rc       = shm_ht_rec_delete(&ht_loc, key_name, strlen(key_name));
		assert_true(rc == 0);
	}

	/* delete ht records with shm_ht_rec_delete_at() */
	for (i = 1; i < NUM_KV; i += 2) {
		rc = shm_ht_rec_delete_at(&(rec_loc_set[i]));
		assert_true(rc == 0);
	}

	shm_ht_decref(&ht_loc);

	free(key_set);
	free(rec_loc_set);

	pthread_exit(NULL);
}

void
test_hash(void **state)
{
	int                     i;
	int                     rc;
	int                     err;
	int                     status;
	/* the hash table in shared memory */
	struct d_shm_ht_loc     ht_loc;
	struct d_shm_ht_rec_loc rec_loc;
	char                   *value;
	char                   *argv[3] = {"test_shm", "--verifykv", NULL};
	char                   *exe_path;
	pid_t                   pid;
	int                     id_list[N_THREAD];
	pthread_t               thread_list[N_THREAD];

	/* create shared memory, create a hash table, insert three keys */
	rc = shm_ht_create(HT_NAME, 8, 16, &ht_loc);
	assert_true(rc == 0);
	/* shm_ht_create() increases reference count */
	assert_true(shm_ht_num_ref(&ht_loc) == 1);

	rc = shm_ht_open_with_name(HT_NAME, &ht_loc);
	assert_true(rc == 0);
	/* shm_ht_open_with_name() increases reference count too */
	assert_true(shm_ht_num_ref(&ht_loc) == 2);

	shm_ht_decref(&ht_loc);
	assert_true(shm_ht_num_ref(&ht_loc) == 1);

	value = shm_ht_rec_find_insert(&ht_loc, KEY_1, strlen(KEY_1), VAL_1, sizeof(VAL_1),
				       &rec_loc, &err);
	assert_non_null(value);
	assert_true(value == shm_ht_rec_data(&rec_loc, &err));

	/* verify ht record reference count */
	assert_true(shm_ht_rec_num_ref(&rec_loc) == 1);
	/* Non-NULL d_shm_ht_rec_loc_t will increase record reference */
	value = shm_ht_rec_find(&ht_loc, KEY_1, strlen(KEY_1), &rec_loc, &err);
	assert_non_null(value);
	assert_true(shm_ht_rec_num_ref(&rec_loc) == 2);
	/* NULL d_shm_ht_rec_loc_t will not increase record reference */
	value = shm_ht_rec_find(&ht_loc, KEY_1, strlen(KEY_1), NULL, &err);
	assert_non_null(value);
	assert_true(shm_ht_rec_num_ref(&rec_loc) == 2);
	/* decrease reference count */
	rc = shm_ht_rec_decref(&rec_loc);
	assert_true(rc == 0);
	assert_true(shm_ht_rec_num_ref(&rec_loc) == 1);

	rc = shm_ht_rec_decref(&rec_loc);
	assert_true(rc == 0);

	value = shm_ht_rec_find_insert(&ht_loc, KEY_2, strlen(KEY_2), VAL_2, sizeof(VAL_2),
				       &rec_loc, &err);
	assert_non_null(value);
	assert_true(value == shm_ht_rec_data(&rec_loc, &err));

	rc = shm_ht_rec_decref(&rec_loc);
	assert_true(rc == 0);

	value = shm_ht_rec_find_insert(&ht_loc, KEY_3, strlen(KEY_3), VAL_3, sizeof(VAL_3),
				       &rec_loc, &err);
	assert_non_null(value);
	assert_true(value == shm_ht_rec_data(&rec_loc, &err));

	rc = shm_ht_rec_decref(&rec_loc);
	assert_true(rc == 0);

	verify_hash();

	/* start a child process and run test_shm & verify key-value pairs */
	exe_path = malloc(PATH_MAX);
	assert_non_null(exe_path);
	rc = readlink("/proc/self/exe", exe_path, PATH_MAX - 1);
	assert_true(rc > 0);
	exe_path[rc] = 0;

	pid = fork();
	if (pid == 0)
		execvp(exe_path, argv);
	waitpid(pid, &status, 0);
	if (WIFEXITED(status))
		assert_int_equal(WEXITSTATUS(status), 0);
	free(exe_path);

	/* remove key_1 from ht */
	rc = shm_ht_rec_delete(&ht_loc, KEY_1, strlen(KEY_1));
	assert_true(rc == 0);
	value = shm_ht_rec_find(&ht_loc, KEY_1, strlen(KEY_1), NULL, &err);
	assert_true(value == NULL);

	/* remove key_2 from ht */
	rc = shm_ht_rec_delete(&ht_loc, KEY_2, strlen(KEY_2));
	assert_true(rc == 0);
	value = shm_ht_rec_find(&ht_loc, KEY_2, strlen(KEY_2), NULL, &err);
	assert_true(value == NULL);

	/* remove key_3 from ht */
	rc = shm_ht_rec_delete_at(&rec_loc);
	assert_true(rc == 0);
	value = shm_ht_rec_find(&ht_loc, KEY_3, strlen(KEY_3), &rec_loc, &err);
	assert_true(value == NULL);
	assert_true(value == shm_ht_rec_data(&rec_loc, &err));

	/* start multiple threads to operate ht currently */
	for (i = 0; i < N_THREAD; i++) {
		id_list[i] = i;
		rc = pthread_create(&thread_list[i], NULL, thread_ht_op, (void *)&id_list[i]);
		assert_true(rc == 0);
	}
	for (i = 0; i < N_THREAD; i++) {
		rc = pthread_join(thread_list[i], NULL);
		assert_true(rc == 0);
	}

	rc = shm_ht_destroy(&ht_loc, false);
	assert_true(rc == SHM_HT_BUSY);

	shm_ht_decref(&ht_loc);

	rc = shm_ht_destroy(&ht_loc, false);
	assert_true(rc == SHM_HT_SUCCESS);
}

#define TIME_SLEEP (1)

void
do_lock_mutex_child(bool lock_only)
{
	int                     rc;
	int                     err;
	struct d_shm_ht_rec_loc rec_loc;
	struct d_shm_ht_loc     ht_loc;
	const char              ht_name[] = "shm_lock_test";
	const char              key[]     = "mutex";
	d_shm_mutex_t          *mutex;

	/* test lock a mutex in shared memory in a child process */
	rc = shm_ht_open_with_name(ht_name, &ht_loc);
	assert_true(rc == 0);

	mutex = (d_shm_mutex_t *)shm_ht_rec_find(&ht_loc, key, strlen(key), &rec_loc, &err);
	assert_true(mutex != NULL);
	assert_true(mutex == shm_ht_rec_data(&rec_loc, &err));

	rc = shm_ht_rec_decref(&rec_loc);
	assert_true(rc == 0);

	shm_mutex_lock(mutex, NULL);
	sleep(TIME_SLEEP);
	if (!lock_only) {
		shm_mutex_unlock(mutex);
		shm_ht_decref(&ht_loc);
	} else {
		/* Do not unmap shared memory to simulate a process crashing */
		_exit(0);
	}

	/**
	 * shm_fini() is NOT called to unmap shm. Otherwise EOWNERDEAD will not be triggered. This
	 * mimics unexpected process termination before unlocking and shm_fini().
	 */
}

void
test_lock(void **state)
{
	int                     rc;
	int                     err;
	int                     status;
	d_shm_mutex_t          *mutex;
	/* the hash table in shared memory */
	struct d_shm_ht_loc     ht_loc;
	const char              ht_name[] = "shm_lock_test";
	const char              key[]     = "mutex";
	struct timeval          tm1, tm2;
	double                  dt;
	struct d_shm_ht_rec_loc rec_loc;
	char                   *argv[3]  = {"test_shm", "--lockmutex", NULL};
	char                   *argv2[3] = {"test_shm", "--lockonly", NULL};
	char                   *exe_path;
	pid_t                   pid;
	bool                    owner_dead;

	/**
	 * create shared memory, create a hash table, insert a key whose value is a struct of
	 * d_shm_mutex_t
	 */
	rc = shm_ht_create(ht_name, 8, 16, &ht_loc);
	assert_true(rc == 0);

	mutex = (d_shm_mutex_t *)shm_ht_rec_find_insert(
	    &ht_loc, key, strlen(key), INIT_KEY_VALUE_MUTEX, sizeof(d_shm_mutex_t), &rec_loc, &err);
	assert_true(mutex != NULL);
	assert_true(mutex == shm_ht_rec_data(&rec_loc, &err));

	rc = shm_ht_rec_decref(&rec_loc);
	assert_true(rc == 0);

	/* start a child process to lock this mutex */
	exe_path = malloc(PATH_MAX);
	assert_non_null(exe_path);
	rc = readlink("/proc/self/exe", exe_path, PATH_MAX - 1);
	assert_true(rc > 0);
	exe_path[rc] = 0;

	pid = fork();
	if (pid == 0)
		execvp(exe_path, argv);
	else
		/* take a short nap to allow the child process to lock the mutex first */
		usleep(18000);

	gettimeofday(&tm1, NULL);
	shm_mutex_lock(mutex, &owner_dead);
	gettimeofday(&tm2, NULL);
	dt = (tm2.tv_sec - tm1.tv_sec) + (tm2.tv_usec - tm1.tv_usec) * 0.000001;
	assert_true(fabs(dt - TIME_SLEEP) < 0.25);
	shm_mutex_unlock(mutex);
	assert_true(owner_dead == false);

	waitpid(pid, &status, 0);
	if (WIFEXITED(status))
		assert_int_equal(WEXITSTATUS(status), 0);

	/**
	 * start a child process to lock this mutex and exit without unlocking this mutex to mimic
	 * a lock owner process crashes or is killed
	 */
	pid = fork();
	if (pid == 0)
		execvp(exe_path, argv2);

	/* the child process should finish now with mutex unlocked */
	waitpid(pid, &status, 0);
	if (WIFEXITED(status))
		assert_int_equal(WEXITSTATUS(status), 0);

	shm_mutex_lock(mutex, &owner_dead);
	shm_mutex_unlock(mutex);
	assert_true(owner_dead);
	free(exe_path);
	shm_ht_decref(&ht_loc);
}

static int
init_tests(void **state)
{
	int rc;

	rc = shm_init();
	assert_true(rc == 0);
	assert_true(shm_inited() == true);
	return d_log_init();
}

static int
fini_tests(void **state)
{
	shm_fini();
	d_log_fini();

	return 0;
}

int
main(int argc, char **argv)
{
	int                     opt = 0, index = 0, rc;
	const struct CMUnitTest tests[] = {cmocka_unit_test(test_hash), cmocka_unit_test(test_lock),
					   cmocka_unit_test(test_mem)};
	// clang-format off
	static struct option    long_options[] = {
	    {"all", no_argument, NULL, 'a'},      {"hash", no_argument, NULL, 'h'},
	    {"lock", no_argument, NULL, 'l'},     {"lockmutex", no_argument, NULL, 'k'},
	    {"memory", no_argument, NULL, 'm'},   {"lockonly", no_argument, NULL, 'o'},
	    {"verifykv", no_argument, NULL, 'v'}, {NULL, 0, NULL, 0}};
	// clang-format on
	while ((opt = getopt_long(argc, argv, ":ahlkmov", long_options, &index)) != -1) {
		switch (opt) {
		case 'a':
			break;
		case 'v':
			/* only run by child process */
			init_tests(NULL);
			verify_hash_by_child();
			goto exit_child;
		case 'k':
			/* only run by child process */
			init_tests(NULL);
			do_lock_mutex_child(false);
			goto exit_child;
		case 'o':
			/* only run by child process */
			init_tests(NULL);
			do_lock_mutex_child(true);
			goto exit_child;
		default:
			printf("Unknown Option\n");
			return 1;
		}
	}

	d_register_alt_assert(mock_assert);
	rc = cmocka_run_group_tests_name("test_shm", tests, init_tests, fini_tests);

	/* unlink shared memory file under /dev/shm/ */
	shm_destroy(true);

	return rc;

exit_child:
	fini_tests(NULL);
	return 0;
}
