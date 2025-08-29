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
#include <gurt/shm_utils.h>

#define MAX_THREAD 32

/* convert second to micro second. Scale down to make tests shorter */
#define T_SCLAE    (1000000 * 0.01)
#define US_TO_S    (0.000001)

enum THREAD_TYPE { READ, WRITE };

struct thread_param {
	int              sec_sleep;
	int              sec_locked;
	enum THREAD_TYPE job_type;
	d_shm_rwlock_t  *rwlock;
};

struct thread_counter_param {
	enum THREAD_TYPE job_type;
	d_shm_rwlock_t  *rwlock;
	int             *counter;
};

pthread_t                   thread_list[MAX_THREAD];
struct thread_param         param_list[MAX_THREAD];
struct thread_counter_param counter_param_list[MAX_THREAD];

void *
reader(void *param)
{
	int                  rc;
	struct thread_param *my_param = param;

	rc = shm_thread_data_init();
	assert_true(rc == 0);

	usleep(my_param->sec_sleep * T_SCLAE);
	shm_rwlock_rd_lock(my_param->rwlock);
	usleep(my_param->sec_locked * T_SCLAE);
	shm_rwlock_rd_unlock(my_param->rwlock);

	rc = shm_thread_data_fini();
	assert_true(rc == 0);
	pthread_exit(NULL);
}

void *
writer(void *param)
{
	int                  rc;
	struct thread_param *my_param = param;

	rc = shm_thread_data_init();
	assert_true(rc == 0);

	usleep(my_param->sec_sleep * T_SCLAE);
	shm_rwlock_wr_lock(my_param->rwlock);
	usleep(my_param->sec_locked * T_SCLAE);
	shm_rwlock_wr_unlock(my_param->rwlock);

	rc = shm_thread_data_fini();
	assert_true(rc == 0);
	pthread_exit(NULL);
}

#define NUM_REPEAT (50000)

void *
read_or_inc_counter(void *param)
{
	int                          i;
	int                          rc;
	int                         *counter;
	struct thread_counter_param *my_param = param;

	rc = shm_thread_data_init();
	assert_true(rc == 0);

	for (i = 0; i < NUM_REPEAT; i++) {
		if (my_param->job_type == READ) {
			shm_rwlock_rd_lock(my_param->rwlock);
			/* do not thing with read lock */
			shm_rwlock_rd_unlock(my_param->rwlock);
		} else {
			shm_rwlock_wr_lock(my_param->rwlock);
			/* increase the counter with write lock */
			counter  = my_param->counter;
			*counter = *counter + 1;
			shm_rwlock_wr_unlock(my_param->rwlock);
		}
	}

	rc = shm_thread_data_fini();
	assert_true(rc == 0);
	pthread_exit(NULL);
}

void
verify_rwlock(char *cmd, double dt_exp, d_shm_rwlock_t *rwlock, bool fi_enabled)
{
	int                 i;
	int                 rc;
	int                 pos;
	int                 len;
	int                 nthreads;
	int                 len_tmp;
	char                sec_sleep_str[16];
	char                sec_lock_str[16];
	struct timeval      tm1, tm2;
	double              dt;
	struct d_shm_ht_loc ht_head_fi_tid_line;
	const char          ht_name[] = HT_NAME_FI;

	len      = strlen(cmd);
	pos      = 0;
	nthreads = 0;
	while (pos < len) {
		if (cmd[pos] != 'S' && cmd[pos] != 's') {
			printf("Unexpected cmd: %c\n", cmd[pos]);
			exit(1);
		}
		pos++;
		if (pos >= len) {
			printf("Incomplete cmd: %s\n", cmd);
			exit(1);
		}

		len_tmp = 0;
		while (cmd[pos] >= '0' && cmd[pos] <= '9') {
			sec_sleep_str[len_tmp] = cmd[pos];
			pos++;
			len_tmp++;
		}
		if (pos >= len) {
			printf("Incomplete cmd: %s\n", cmd);
			exit(1);
		}
		sec_sleep_str[len_tmp]         = 0;
		param_list[nthreads].sec_sleep = atoi(sec_sleep_str);

		if (cmd[pos] == 'R' || cmd[pos] == 'r') {
			param_list[nthreads].job_type = READ;
		} else if (cmd[pos] == 'W' || cmd[pos] == 'w') {
			param_list[nthreads].job_type = WRITE;
		} else {
			printf("Unexpected cmd: %c. Not R or W\n", cmd[pos]);
			exit(1);
		}
		pos++;

		len_tmp = 0;
		while (cmd[pos] >= '0' && cmd[pos] <= '9') {
			sec_lock_str[len_tmp] = cmd[pos];
			pos++;
			len_tmp++;
			if (pos == len) {
				break;
			}
		}
		sec_lock_str[len_tmp]           = 0;
		param_list[nthreads].sec_locked = atoi(sec_lock_str);
		param_list[nthreads].rwlock     = rwlock;
		nthreads++;
		if (pos == len)
			break;
	}

	/* create hash table shm_rwlock_fi for tracking fi locations */
	rc = shm_ht_create(ht_name, 7, 16, &ht_head_fi_tid_line);
	assert_true(rc == 0);

	gettimeofday(&tm1, NULL);
	for (i = 0; i < nthreads; i++) {
		if (param_list[i].job_type == READ)
			pthread_create(&thread_list[i], NULL, reader, &param_list[i]);
		else if (param_list[i].job_type == WRITE)
			pthread_create(&thread_list[i], NULL, writer, &param_list[i]);
	}

	for (i = 0; i < nthreads; i++) {
		pthread_join(thread_list[i], NULL);
	}
	gettimeofday(&tm2, NULL);
	dt = (tm2.tv_sec - tm1.tv_sec) + (tm2.tv_usec - tm1.tv_usec) * 0.000001;
	if (!fi_enabled)
		/* check time roughly due to large performance variance in vm */
		assert_true((dt / (dt_exp * T_SCLAE * US_TO_S)) <= 5.0);

	rc = shm_ht_decref(&ht_head_fi_tid_line);
	assert_true(rc == 0);
	shm_ht_destroy(&ht_head_fi_tid_line, false);
}

void
verify_rwlock_fi(char *cmd, double dt_exp, d_shm_rwlock_t *rwlock)
{
#if FAULT_INJECTION
	int i;
	int j;
	int num_fi_target;

	/* clear fi counters and fi target */
	shm_fi_init();
	verify_rwlock(cmd, dt_exp, rwlock, false);
	/* get the total number of fault injection target in this test */
	num_fi_target = shm_fi_counter_value();

	for (i = 0; i < num_fi_target; i++) {
		for (j = i; j < num_fi_target; j++) {
			shm_fi_init();
			shm_fi_set_p1(i);
			shm_fi_set_p2(j);
			verify_rwlock(cmd, dt_exp, rwlock, true);
			/* run test again after fault injection to make sure rwlock still behaves as
			 * expected.
			 */
			shm_fi_init();
			verify_rwlock(cmd, dt_exp, rwlock, false);
		}
	}
#endif
}

void
verify_counter(d_shm_rwlock_t *rwlock)
{
	int i;
	int nthreads = 4;
	int counter_exp;
	int counter;

	while (nthreads <= MAX_THREAD) {
		counter_exp = 0;
		counter     = 0;
		for (i = 0; i < nthreads; i++) {
			/* evenly allocate threads as reader & writer */
			counter_param_list[i].job_type = (i & 0x1) ? READ : WRITE;
			if (counter_param_list[i].job_type == WRITE)
				counter_exp++;
			counter_param_list[i].rwlock  = rwlock;
			counter_param_list[i].counter = &counter;
		}
		counter_exp *= NUM_REPEAT;

		for (i = 0; i < nthreads; i++) {
			pthread_create(&thread_list[i], NULL, read_or_inc_counter,
				       &counter_param_list[i]);
		}

		for (i = 0; i < nthreads; i++) {
			pthread_join(thread_list[i], NULL);
		}
		assert_true(counter_exp == counter);

		nthreads *= 2;
	}
	assert_true(rwlock->max_num_reader == DEFAULT_MAX_NUM_READERS);
}

/*
s ns [r/w] nl
ns is the number of seconds to sleep before acquiring lock
nl is the number of seconds to sleep after acquiring lock
*/

#define NUM_RWLOCK_TEST 5
void
test_rwlock(void **state)
{
	int    i;
	int    rc;
	int    err;
	char  *test_list[NUM_RWLOCK_TEST] = {"s0r2s1w2", "s0r2s0r3s1w2", "s0r1s0r2s0r3s1w2",
					     "s0w2s1r2s1r3s1r4", "s0w2s1w2s3r2s4r2"};
	double t_list[NUM_RWLOCK_TEST]    = {4.0, 5.0, 5.0, 6.0, 6.0};

	d_shm_rwlock_t         *rwlock;
	/* the hash table in shared memory */
	struct d_shm_ht_loc     ht_loc;
	struct d_shm_ht_rec_loc rec_loc;
	const char              ht_name[] = "shm_rwlock_test";
	const char              key[]     = "rwlock";
	bool                    created;

	rc = shm_ht_create(ht_name, 8, 16, &ht_loc);
	assert_true(rc == 0);

	rwlock = (d_shm_rwlock_t *)shm_ht_rec_find_insert(
	    &ht_loc, key, strlen(key), INIT_KEY_VALUE_RWLOCK, sizeof(d_shm_rwlock_t), &rec_loc,
	    &created, &err);
	assert_true(rwlock != NULL);
	assert_true(created);

	verify_counter(rwlock);

	for (i = 0; i < NUM_RWLOCK_TEST; i++) {
		verify_rwlock_fi(test_list[i], t_list[i], rwlock);
	}

	/* recrease the reference count of hash record */
	rc = shm_ht_rec_decref(&rec_loc);
	assert_true(rc == 0);
	/* remove the hash record */
	rc = shm_ht_rec_delete_at(&rec_loc);
	assert_true(rc == 0);

	rc = shm_ht_decref(&ht_loc);
	assert_true(rc == 0);
}

void
test_lrucache(void **state)
{
	int              i;
	int              rc;
	int              key;
	int              val;
	int             *addr_val;
	int              key_size;
	char             key_long[16] = "aaaaaaaaaaaaaaa";
	char             key_var[16];
	char             data_long[16] = "bbbbbbbbbbbbbbbb";
	shm_lru_node_t  *node_found;
	int              capacity;
	shm_lru_cache_t *cache;

	/* test keys with various size */
	/* key_size is zero, so key could have various length */
	rc = shm_lru_create_cache(16, 0, sizeof(int), &cache);
	assert_true(rc == 0);

	for (key_size = 1; key_size <= 15; key_size++) {
		memcpy(key_var, key_long, key_size);
		val = key_size;
		shm_lru_put(cache, key_var, key_size, &val, sizeof(int));
	}

	for (key_size = 1; key_size <= 15; key_size++) {
		memcpy(key_var, key_long, key_size);
		rc = shm_lru_get(cache, key_var, key_size, &node_found, (void **)&addr_val);
		assert(rc == 0);
		assert(*addr_val == key_size);
		shm_lru_node_dec_ref(node_found);
	}

	shm_lru_destroy_cache(cache);

	/* test various key size and data size */
	rc = shm_lru_create_cache(16, 0, 0, &cache);
	assert_true(rc == 0);

	for (i = 1; i <= 16; i++) {
		shm_lru_put(cache, key_long, i, data_long, i);
	}

	for (i = 1; i <= 16; i++) {
		key = i;
		rc  = shm_lru_get(cache, key_long, i, &node_found, (void **)&addr_val);
		assert(rc == 0);
		assert_true(memcmp(addr_val, data_long, i) == 0);
		shm_lru_node_dec_ref(node_found);
	}

	shm_lru_destroy_cache(cache);

	/* test updating existing key */
	rc = shm_lru_create_cache(2, sizeof(int), sizeof(int), &cache);
	assert_true(rc == 0);

	key = 1;
	val = 1;
	shm_lru_put(cache, &key, sizeof(int), &val, sizeof(int));
	key = 2;
	val = 2;
	shm_lru_put(cache, &key, sizeof(int), &val, sizeof(int));

	key = 1;
	val = 10;
	shm_lru_put(cache, &key, sizeof(int), &val, sizeof(int));

	key = 1;
	rc  = shm_lru_get(cache, &key, sizeof(int), &node_found, (void **)&addr_val);
	assert(rc == 0);
	assert(*addr_val == 10);
	shm_lru_node_dec_ref(node_found);

	key = 3;
	val = 3;
	shm_lru_put(cache, &key, sizeof(int), &val, sizeof(int));

	key = 2;
	rc  = shm_lru_get(cache, &key, sizeof(int), &node_found, (void **)&addr_val);
	assert(rc == SHM_LRU_REC_NOT_FOUND);

	key = 1;
	rc  = shm_lru_get(cache, &key, sizeof(int), &node_found, (void **)&addr_val);
	assert(rc == 0);
	assert(*addr_val == 10);
	shm_lru_node_dec_ref(node_found);

	shm_lru_destroy_cache(cache);

	/* large number of operations */
	capacity = 100;
	rc       = shm_lru_create_cache(capacity, sizeof(int), sizeof(int), &cache);
	assert_true(rc == 0);

	/* make cache full */
	for (i = 0; i < capacity; i++) {
		key = i;
		val = i;
		shm_lru_put(cache, &key, sizeof(int), &val, sizeof(int));
	}

	/* verify all items exist */
	for (i = 0; i < capacity; i++) {
		key = i;
		rc  = shm_lru_get(cache, &key, sizeof(int), &node_found, (void **)&addr_val);
		assert(rc == 0);
		assert(*addr_val == i);
		shm_lru_node_dec_ref(node_found);
	}

	/* add more items to force eviction */
	for (i = capacity; i < capacity + 50; i++) {
		key = i;
		val = i;
		shm_lru_put(cache, &key, sizeof(int), &val, sizeof(int));
	}

	/* verify first 50 items are evicted */
	for (i = 0; i < 50; i++) {
		key = i;
		rc  = shm_lru_get(cache, &key, sizeof(int), &node_found, (void **)&addr_val);
		assert(rc == SHM_LRU_REC_NOT_FOUND);
	}

	/* verify remaining items do exist */
	for (i = 50; i < capacity + 50; i++) {
		key = i;
		rc  = shm_lru_get(cache, &key, sizeof(int), &node_found, (void **)&addr_val);
		assert(rc == 0);
		assert(*addr_val == i);
		shm_lru_node_dec_ref(node_found);
	}

	shm_lru_destroy_cache(cache);
}

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
	bool                     created;
	struct d_shm_ht_rec_loc *rec_loc_set;

	thread_id = *(int *)arg;

	rc = shm_thread_data_init();
	assert_true(rc == 0);
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
					       &rec_loc_set[i], &created, &err);
		assert_non_null(value);
		assert_true(created);
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

	rc = shm_thread_data_fini();
	assert_true(rc == 0);
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
	bool                    created;

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
				       &rec_loc, &created, &err);
	assert_non_null(value);
	assert_true(created);
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
				       &rec_loc, &created, &err);
	assert_non_null(value);
	assert_true(created);
	assert_true(value == shm_ht_rec_data(&rec_loc, &err));

	rc = shm_ht_rec_decref(&rec_loc);
	assert_true(rc == 0);

	value = shm_ht_rec_find_insert(&ht_loc, KEY_3, strlen(KEY_3), VAL_3, sizeof(VAL_3),
				       &rec_loc, &created, &err);
	assert_non_null(value);
	assert_true(created);
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
	bool                    created;
	bool                    owner_dead;

	/**
	 * create shared memory, create a hash table, insert a key whose value is a struct of
	 * d_shm_mutex_t
	 */
	rc = shm_ht_create(ht_name, 8, 16, &ht_loc);
	assert_true(rc == 0);

	mutex = (d_shm_mutex_t *)shm_ht_rec_find_insert(&ht_loc, key, strlen(key),
							INIT_KEY_VALUE_MUTEX, sizeof(d_shm_mutex_t),
							&rec_loc, &created, &err);
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
	assert_true(fabs(dt - TIME_SLEEP) < 0.15);
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
	rc = shm_thread_data_init();
	assert_true(rc == 0);
	return d_log_init();
}

static int
fini_tests(void **state)
{
	int rc;

	rc = shm_thread_data_fini();
	assert_true(rc == 0);
	shm_fini();
	d_log_fini();

	return 0;
}

int
main(int argc, char **argv)
{
	int                     opt = 0, index = 0, rc;
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_hash), cmocka_unit_test(test_lock), cmocka_unit_test(test_mem),
		cmocka_unit_test(test_rwlock), cmocka_unit_test(test_lrucache)};

	// clang-format off
	static struct option    long_options[] = {
	    {"all", no_argument, NULL, 'a'},      {"hash", no_argument, NULL, 'h'},
	    {"lock", no_argument, NULL, 'l'},     {"lockmutex", no_argument, NULL, 'k'},
	    {"memory", no_argument, NULL, 'm'},   {"lockonly", no_argument, NULL, 'o'},
	    {"rwlock", no_argument, NULL, 'r'},   {"verifykv", no_argument, NULL, 'v'},
	    {"lrucache", no_argument, NULL, 'c'}, {NULL, 0, NULL, 0}};
	// clang-format on

	while ((opt = getopt_long(argc, argv, ":ahlkmorvc", long_options, &index)) != -1) {
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
