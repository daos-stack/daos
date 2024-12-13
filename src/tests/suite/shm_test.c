/**
 * (C) Copyright 2024 Hewlett Packard Enterprise Development LP.
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

#include <gurt/shm_alloc.h>
#include <gurt/shm_dict.h>
#include <gurt/shm_utils.h>
#include <daos/debug.h>

/* Tests can be run by specifying the appropriate argument for a test or all will be run if no test
 * is specified.
 */
static const char *all_tests = "hlm";

static void
print_usage()
{
	print_message("\n\nShared memory tests\n=============================\n");
	print_message("Tests: Use one of these arg(s) for specific test\n");
	print_message("shm_test -a|--all\n");
	print_message("shm_test -h|--hash\n");
	print_message("shm_test -l|--lock\n");
	print_message("shm_test -m|--memory\n");
	print_message("Default <shm_test> runs all tests\n");
	print_message("\n=============================\n");
}

#define N_LOOP_MEM (8)

void
do_mem(void **state)
{
	int    i;
	int    rc;
	size_t align = 4;
	size_t size;
	char  *buf_list[N_LOOP_MEM];

	rc = shm_init();
	assert_true(rc == 0);
	assert_true(shm_inited() == true);

	srandom(1);
	/* testing allocation with alignment and deallocation */
	for (i = 0; i < N_LOOP_MEM; i++) {
		size = (size_t)(random() % (120 * 1024));
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
		size = (size_t)(random() % (120 * 1024));
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
	int                   rc;
	char                 *value;
	struct shm_ht_rec    *link;
	struct d_shm_ht_head *ht_head_lock;

	/* look up hash key in current process */
	rc = get_ht_with_name(HT_NAME, &ht_head_lock);
	assert_true(rc == 0);

	value = (char *)shm_ht_rec_find(ht_head_lock, KEY_1, strlen(KEY_1), &link);
	assert_non_null(value);
	assert_true(strcmp(value, VAL_1) == 0);

	value = (char *)shm_ht_rec_find(ht_head_lock, KEY_2, strlen(KEY_2), &link);
	assert_non_null(value);
	assert_true(strcmp(value, VAL_2) == 0);

	value = (char *)shm_ht_rec_find(ht_head_lock, KEY_3, strlen(KEY_3), &link);
	assert_non_null(value);
	assert_true(strcmp(value, VAL_3) == 0);
}

void
verify_hash_by_child(void)
{
	int                   rc;
	char                 *value;
	struct shm_ht_rec    *link;
	struct d_shm_ht_head *ht_head_lock;

	/* look up hash key in child process */
	rc = shm_init();
	assert_true(rc == 0);
	assert_true(shm_inited() == true);

	rc = get_ht_with_name(HT_NAME, &ht_head_lock);
	assert_true(rc == 0);

	value = (char *)shm_ht_rec_find(ht_head_lock, KEY_1, strlen(KEY_1), &link);
	assert_non_null(value);
	assert_true(strcmp(value, VAL_1) == 0);

	value = (char *)shm_ht_rec_find(ht_head_lock, KEY_2, strlen(KEY_2), &link);
	assert_non_null(value);
	assert_true(strcmp(value, VAL_2) == 0);

	value = (char *)shm_ht_rec_find(ht_head_lock, KEY_3, strlen(KEY_3), &link);
	assert_non_null(value);
	assert_true(strcmp(value, VAL_3) == 0);
}


void
do_hash(void **state)
{
	int                   rc;
	int                   status;
	/* the hash table in shared memory */
	struct d_shm_ht_head *ht_head_lock;
	struct shm_ht_rec    *link;
	char                 *value;
	char                 *argv[3] = {"shm_test", "--verifykv", NULL};
	char                 *exe_path;
	pid_t                 pid;

	/* create shared memory, create a hash table, insert three keys */
	rc = shm_init();
	assert_true(rc == 0);
	assert_true(shm_inited() == true);

	rc = shm_ht_create(HT_NAME, 8, 16, &ht_head_lock);
	assert_true(rc == 0);

	value = shm_ht_rec_find_insert(ht_head_lock, KEY_1, strlen(KEY_1), VAL_1, sizeof(VAL_1),
				       &link);
	assert_non_null(value);

	value = shm_ht_rec_find_insert(ht_head_lock, KEY_2, strlen(KEY_2), VAL_2, sizeof(VAL_2),
				       &link);
	assert_non_null(value);

	value = shm_ht_rec_find_insert(ht_head_lock, KEY_3, strlen(KEY_3), VAL_3, sizeof(VAL_3),
				       &link);
	assert_non_null(value);

	verify_hash();

	/* start a child process and run shm_test & verify key-value pairs */
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
}

#define TIME_SLEEP (1)

void
do_lock_mutex_child(bool lock_only)
{
	int                   rc;
	struct shm_ht_rec    *link;
	struct d_shm_ht_head *ht_head_lock;
	const char            ht_name[] = "shm_lock_test";
	const char            key[]     = "mutex";
	pthread_mutex_t      *mutex;

	/* test lock a mutex in shared memory in a child process */
	rc = shm_init();
	assert_true(rc == 0);
	assert_true(shm_inited() == true);

	rc = get_ht_with_name(ht_name, &ht_head_lock);
	assert_true(rc == 0);

	mutex = (pthread_mutex_t *)shm_ht_rec_find(ht_head_lock, key, strlen(key), &link);
	assert_true(mutex != NULL);

	shm_mutex_lock(mutex);
	sleep(TIME_SLEEP);
	if (!lock_only)
		shm_mutex_unlock(mutex);
}

void
do_lock(void **state)
{
	int                   rc;
	int                   status;
	pthread_mutex_t      *mutex;
	/* the hash table in shared memory */
	struct d_shm_ht_head *ht_head_lock;
	const char            ht_name[] = "shm_lock_test";
	const char            key[]     = "mutex";
	struct timeval        tm1, tm2;
	double                dt;
	struct shm_ht_rec    *link;
	char                 *argv[3]  = {"shm_test", "--lockmutex", NULL};
	char                 *argv2[3] = {"shm_test", "--lockonly", NULL};
	char                 *exe_path;
	pid_t                 pid;

	/**
	 * create shared memory, create a hash table, insert a key whose value is a struct of
	 * pthread_mutex_t
	 */
	rc = shm_init();
	assert_true(rc == 0);
	assert_true(shm_inited() == true);

	rc = shm_ht_create(ht_name, 8, 16, &ht_head_lock);
	assert_true(rc == 0);

	mutex = (pthread_mutex_t *)shm_ht_rec_find_insert(ht_head_lock, key, strlen(key),
		KEY_VALUE_PTHREAD_LOCK, sizeof(pthread_mutex_t), &link);
	assert_true(mutex != NULL);

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
	shm_mutex_lock(mutex);
	gettimeofday(&tm2, NULL);
	dt = (tm2.tv_sec - tm1.tv_sec) + (tm2.tv_usec - tm1.tv_usec) * 0.000001;
	assert_true(fabs(dt - TIME_SLEEP) < 0.02);
	shm_mutex_unlock(mutex);

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

	shm_mutex_lock(mutex);
	shm_mutex_unlock(mutex);
}

static int
run_specified_tests(const char *tests, int *sub_tests, int sub_tests_size)
{
	int nr_failed = 0;

	if (strlen(tests) == 0)
		tests = all_tests;

	while (*tests != '\0') {
		switch (*tests) {
		case 'h':
			printf("\n\n=================");
			printf("shm hash table tests");
			printf("=====================\n");
			const struct CMUnitTest ht_tests[] = {
			    cmocka_unit_test(do_hash),
			};
			nr_failed += cmocka_run_group_tests(ht_tests, NULL, NULL);
			break;

		case 'l':
			printf("\n\n=================");
			printf("shm lock/unlock tests");
			printf("=====================\n");
			const struct CMUnitTest lock_tests[] = {
			    cmocka_unit_test(do_lock),
			};
			nr_failed += cmocka_run_group_tests(lock_tests, NULL, NULL);
			break;

		case 'm':
			printf("\n\n=================");
			printf("shm allocation/deallocation tests");
			printf("=====================\n");
			const struct CMUnitTest mem_tests[] = {
			    cmocka_unit_test(do_mem),
			};
			nr_failed += cmocka_run_group_tests(mem_tests, NULL, NULL);
			break;

		default:
			assert_true(0);
		}

		tests++;
	}

	return nr_failed;
}

int
main(int argc, char **argv)
{
	char                 tests[64] = {};
	int                  ntests    = 0;
	int                  nr_failed = 0;
	int                  opt = 0, index = 0, rc;

	static struct option long_options[] = {{"all", no_argument, NULL, 'a'},
					       {"hash", no_argument, NULL, 'h'},
					       {"lock", no_argument, NULL, 'l'},
					       {"lockmutex", no_argument, NULL, 'k'},
					       {"memory", no_argument, NULL, 'm'},
					       {"lockonly", no_argument, NULL, 'o'},
					       {"verifykv", no_argument, NULL, 'v'},
					       {NULL, 0, NULL, 0}};

	rc = daos_debug_init(NULL);
	assert_true(rc == 0);

	while ((opt = getopt_long(argc, argv, ":ahlkmov", long_options, &index)) != -1) {
		if (strchr(all_tests, opt) != NULL) {
			tests[ntests] = opt;
			ntests++;
			continue;
		}
		switch (opt) {
		case 'a':
			break;
		case 'v':
			/* only run by child process */
			verify_hash_by_child();
			goto exit_child;
		case 'k':
			/* only run by child process */
			do_lock_mutex_child(false);
			goto exit_child;
		case 'o':
			/* only run by child process */
			do_lock_mutex_child(true);
			goto exit_child;
		default:
			printf("Unknown Option\n");
			print_usage();
			return 1;
		}
	}

	nr_failed = run_specified_tests(tests, NULL, 0);

	print_message("\n============ Summary %s\n", __FILE__);
	if (nr_failed == 0)
		print_message("OK - NO TEST FAILURES\n");
	else
		print_message("ERROR, %i TEST(S) FAILED\n", nr_failed);

	/* unlink shared memory file under /dev/shm/ */
	shm_destroy();
	daos_debug_fini();

	return nr_failed;

exit_child:
	daos_debug_fini();
	return 0;
}
