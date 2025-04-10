/**
 * (C) Copyright 2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <errno.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "shm_internal.h"
#include <gurt/shm_utils.h>

/* This takes one bit in lock to indicate whether a write mutex is locked by a reader or not.
 * Normally robust mutex is always used. The write mutex by a reader is an exception.
 */
#define NOT_ROBUST (0x20000000)
/* the tid mask we actually use instead of 0x3FFFFFFF (FUTEX_TID_MASK) since NON_ROBUST takes one
 * bit.
 */
#define TID_MASK         (0x1FFFFFFF)

/* the address of head of robust mutex list maintained by pthread */
static __thread struct robust_list_head *thread_robust_head;
/* thread id of current thread */
__thread pid_t                           d_tid;

/* whether a hash record was added to monitor thread running or not */
static __thread bool                     thread_monitor_inited;
/* pointer to the head of hash table tid which tracks thread exists or not */
static __thread struct d_shm_ht_loc      ht_tid;
/* pointer to the head of hash table tid record for tracking thread existence */
static __thread struct d_shm_ht_rec_loc  rec_tid_mon;

/* the actual implementation of mutex locking */
static int
shm_mutex_lock_ex(d_shm_mutex_t *mutex, const struct timespec *timeout, bool *pre_owner_dead,
		  bool robust);

#if FAULT_INJECTION

/* set fault injection counter zero */
void
shm_fi_init(void)
{
	atomic_store(&d_shm_head->fi_counter, 0);
	d_shm_head->fi_point1 = INVALID_FI_POINT;
	d_shm_head->fi_point2 = INVALID_FI_POINT;
}

int
shm_fi_counter_value(void)
{
	return atomic_load(&d_shm_head->fi_counter);
}

static inline int
shm_fi_counter_inc(void)
{
	return (atomic_fetch_add(&d_shm_head->fi_counter, 1) + 1);
}

void
shm_fi_set_p1(int fi_p)
{
	d_shm_head->fi_point1 = fi_p;
}

void
shm_fi_set_p2(int fi_p)
{
	d_shm_head->fi_point2 = fi_p;
}

/* the hash table in shared memory */
static __thread struct d_shm_ht_loc ht_fi_tid_line;

#endif /* FAULT_INJECTION */

/* fault injection with calling pthread_exit() to simulate unexpected crash */
static inline void
shm_rwlock_fi_here(void)
{
#if FAULT_INJECTION
	uint64_t fi_counter;

	fi_counter = shm_fi_counter_inc();
	if (fi_counter == d_shm_head->fi_point1 || fi_counter == d_shm_head->fi_point2)
		/* quit current thread to introduce a single point failure */
		pthread_exit(NULL);
#endif /* FAULT_INJECTION */
}

/* only triggered one time */
static inline void
shm_rwlock_fi_here_uniqe(const char *file,  int line)
{
#if FAULT_INJECTION
	uint64_t                fi_counter;
	int                     rc;
	int                     err;
	int                     keys[2];
	bool                    created;
	struct d_shm_ht_rec_loc link;

	keys[0] = d_tid;
	keys[1] = line;
	if (ht_fi_tid_line.ht_head == 0) {
		rc = shm_ht_open_with_name(HT_NAME_FI, &ht_fi_tid_line);
		if (rc)
			return;
	}
	shm_ht_rec_find_insert(&ht_fi_tid_line, (char *)keys, sizeof(int) * 2, "NUL", 4, &link,
			       &created, &err);
	if (!created) {
		shm_ht_rec_decref(&link);
		return;
	}
	/* do not decrease ht record reference, so this record will be persistent */

	fi_counter = shm_fi_counter_inc();
	if (fi_counter == d_shm_head->fi_point1 || fi_counter == d_shm_head->fi_point2)
		/* quit current thread to introduce a single point failure */
		pthread_exit(NULL);
#endif /* FAULT_INJECTION */
}

int
thread_monitor_init(void)
{
	int                     rc;
	int                     err;
	int                     tid_masked;
	d_shm_mutex_t          *mutex;
	bool                    created;

	D_ASSERT(d_tid != 0);
	D_ASSERT(ht_tid.ht_head != NULL);

	if (thread_monitor_inited)
		return 0;

	tid_masked = d_tid & TID_MASK;
	/* insert a record in hash table, tid as the key and a robust mutex as data, then lock the
	 * mutex. Since robust mutex is used, the lock value indicates whether the thread exists
	 * or not.
	 */
	mutex = shm_ht_rec_find_insert(&ht_tid, (const char *)&tid_masked, sizeof(int),
				       INIT_KEY_VALUE_MUTEX, sizeof(d_shm_mutex_t), &rec_tid_mon,
				       &created, &err);
	if (mutex == NULL)
		return err;

	rc = shm_mutex_init(mutex);
	if (rc)
		return rc;
	rc = shm_mutex_lock_ex(mutex, NULL, NULL, true);
	if (rc)
		return rc;
	thread_monitor_inited = true;
	return 0;
}

static int
thread_monitor_fini(void)
{
	int rc;

	if (!thread_monitor_inited)
		return 0;

	rc = shm_ht_rec_decref(&rec_tid_mon);
	if (rc)
		return rc;
	rc = shm_ht_rec_delete_at(&rec_tid_mon);
	if (rc)
		return rc;
	thread_monitor_inited = false;

	return 0;
}

int
shm_mutex_init(d_shm_mutex_t *mutex)
{
	if ((unsigned long)mutex & (SHM_MEM_ALIGN - 1))
		/* not correctly aligned */
		return EINVAL;

	memset(mutex, 0, sizeof(d_shm_mutex_t));
	return 0;
}

static int
futex(unsigned int *uaddr, int futex_op, unsigned int val, const struct timespec *timeout,
      unsigned int *uaddr2, unsigned int val3)
{
	return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

static int
futex_wait(unsigned int *futex_ptr, unsigned int expect_val, const struct timespec *timeout)
{
	return futex((unsigned int *)futex_ptr, FUTEX_WAIT, expect_val, timeout, NULL, 0);
}

static int
futex_wake(unsigned int *futex_ptr)
{
	return futex((unsigned int *)futex_ptr, FUTEX_WAKE, 1, NULL, NULL, 0);
}

static void insert_robust_futex_to_list(d_shm_mutex_t *mutex)
{
	d_shm_mutex_t *next_mutex;

	__asm ("" ::: "memory");
	next_mutex = (d_shm_mutex_t *)((char *)((struct robust_list *)thread_robust_head)->next -
				       NEXT_OFFSET_IN_MUTEX);
	next_mutex->prev = (struct robust_list *)&mutex->next;
	mutex->next      = ((struct robust_list *)thread_robust_head)->next;
	mutex->prev      = (struct robust_list *)thread_robust_head;

	((struct robust_list *)thread_robust_head)->next = (struct robust_list *)&mutex->next;
}

static void remove_robust_futex_from_list(d_shm_mutex_t *mutex)
{
	d_shm_mutex_t *target_mutex;
	d_shm_mutex_t *prev_mutex;
	d_shm_mutex_t *next_mutex;

	__asm ("" ::: "memory");
	target_mutex = (d_shm_mutex_t *)((char *)((struct robust_list *)thread_robust_head)->next -
					 NEXT_OFFSET_IN_MUTEX);
	do {
		if (target_mutex == mutex)
			break;
		target_mutex = (d_shm_mutex_t *)((char *)target_mutex->next - NEXT_OFFSET_IN_MUTEX);
	} while (target_mutex->next != (struct robust_list *)thread_robust_head);
	D_ASSERT(target_mutex == mutex);
	next_mutex       = (d_shm_mutex_t *)((char *)mutex->next - NEXT_OFFSET_IN_MUTEX);
	next_mutex->prev = mutex->prev;
	prev_mutex       = (d_shm_mutex_t *)((char *)mutex->prev - NEXT_OFFSET_IN_MUTEX);
	prev_mutex->next = mutex->next;
	__asm ("" ::: "memory");
	mutex->next = NULL;
	mutex->prev = NULL;
}

/* determine robust head in current thread */
static void
query_thread_robust_head(void)
{
	int                 len;
	pthread_mutex_t     mutex;
	pthread_mutexattr_t mutex_attr;

	if (thread_robust_head)
		return;

	pthread_mutexattr_init(&mutex_attr);
	pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_NORMAL);
	pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
	pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);

	pthread_mutex_init(&mutex, &mutex_attr);
	/* query the head of robust mutex list maintained by pthread list */
	syscall(__NR_get_robust_list, d_tid, &thread_robust_head, &len);
	pthread_mutex_destroy(&mutex);
	pthread_mutexattr_destroy(&mutex_attr);

	/* verify d_shm_mutex_t and pthread_mutex_t are consistent */
	assert(-offsetof(d_shm_mutex_t, next) == thread_robust_head->futex_offset);
}

int
shm_thread_data_init(void)
{
	int rc;

	if (d_tid == 0)
		d_tid = syscall(SYS_gettid);
	if (thread_robust_head == NULL)
		query_thread_robust_head();
	if (!shm_inited())
		return 0;
	if (ht_tid.ht_head == NULL) {
		rc = shm_ht_create(HT_NAME_TID_MUTEX, 8, 16, &ht_tid);
		if (rc)
			return rc;
		/* no need to hold the reference */
		shm_ht_decref(&ht_tid);
	}
	if (!thread_monitor_inited)
		return thread_monitor_init();

	return 0;
}

int
shm_thread_data_fini(void)
{
	return thread_monitor_fini();
}

static int
shm_mutex_lock_ex(d_shm_mutex_t *mutex, const struct timespec *timeout, bool *pre_owner_dead,
		  bool robust)
{
	int oldval;
	int newval;
	int ncall_futex_wait             = 0;
	unsigned int other_futex_waiters = 0;

	if (pre_owner_dead)
		*pre_owner_dead = false;
	__asm ("" ::: "memory");
	thread_robust_head->list_op_pending = (struct robust_list *)&mutex->next;
	__asm ("" ::: "memory");
	oldval = mutex->lock;

	while (1) {
		/* Try to acquire the lock through a CAS */
		newval = robust ? (d_tid & (~NOT_ROBUST)):(d_tid | NOT_ROBUST);
		oldval = __sync_val_compare_and_swap(&mutex->lock, 0, newval | other_futex_waiters);
		if (oldval == 0)
			break;

		if ((oldval & FUTEX_OWNER_DIED) != 0) {
			/* The previous owner died.  Try locking the mutex.  */
			newval = robust ? (d_tid & (~NOT_ROBUST)):(d_tid | NOT_ROBUST);
			newval |= (oldval & FUTEX_WAITERS) | other_futex_waiters;
			newval = __sync_val_compare_and_swap (&mutex->lock, oldval, newval);
			if (newval != oldval) {
				oldval = newval;
				continue;
			}

			/* got the lock */
			if (pre_owner_dead)
				*pre_owner_dead = true;
			break;
		}

		/* We cannot acquire the mutex nor has its owner died. Thus, try to block using
		 * futexes. Set FUTEX_WAITERS if necessary so that other threads are aware that
		 * there are potentially threads blocked on the futex. Restart if oldval changed in
		 * the meantime.
		 */
		if ((oldval & FUTEX_WAITERS) == 0) {
			if (!__sync_bool_compare_and_swap (&mutex->lock, oldval, oldval | FUTEX_WAITERS)) {
				oldval = mutex->lock;
				continue;
			}
			oldval |= FUTEX_WAITERS;
		}

		/* It is now possible that we share the FUTEX_WAITERS flag with another thread;
		 * therefore, update assume_other_futex_waiters so that we do not forget about this
		 * when handling other cases above and thus do not cause lost wake-ups.
		 */
		other_futex_waiters |= FUTEX_WAITERS;

		if (timeout && ncall_futex_wait > 0)
			return 1;
		/* Block using the futex and reload current lock value.  */
		futex_wait((unsigned int *)&mutex->lock, oldval, timeout);
		ncall_futex_wait++;
		oldval = mutex->lock;
	}

	D_ASSERT((mutex->lock & TID_MASK)== (d_tid & TID_MASK));
	__asm ("" ::: "memory");
	if (robust)
		insert_robust_futex_to_list(mutex);
	/* clear op_pending after we enqueue the mutex. */
	thread_robust_head->list_op_pending = NULL;
	return 0;
}

int
shm_mutex_lock(d_shm_mutex_t *mutex, bool *pre_owner_dead)
{
	return shm_mutex_lock_ex(mutex, NULL, pre_owner_dead, true);
}

static int
shm_mutex_unlock_ex(d_shm_mutex_t *mutex, bool ignore_owner_tid)
{
	int oldvalue;

	oldvalue = atomic_load_explicit(&mutex->lock, memory_order_relaxed);
	if (oldvalue == 0)
		return EPERM;
	/* unlocked or locked by another thread */
	if (((oldvalue & TID_MASK) != (d_tid & TID_MASK)) && (!ignore_owner_tid))
		return EPERM;

	__asm ("" ::: "memory");
	thread_robust_head->list_op_pending = (struct robust_list *)&mutex->next;
	__asm ("" ::: "memory");
	if ((oldvalue & TID_MASK) == (d_tid & TID_MASK) && (ignore_owner_tid == false))
		remove_robust_futex_from_list(mutex);

	oldvalue = atomic_exchange(&mutex->lock, 0);
	if ((oldvalue & FUTEX_WAITERS) != 0)
		futex_wake((unsigned int *)&mutex->lock);
	__asm ("" ::: "memory");
	/* clear op_pending after we enqueue the mutex. */
	thread_robust_head->list_op_pending = NULL;

	return 0;
}

int
shm_mutex_unlock(d_shm_mutex_t *mutex)
{
	return shm_mutex_unlock_ex(mutex, false);
}

int
shm_rwlock_init(d_shm_rwlock_t *rwlock)
{
	int rc;

	rc = shm_mutex_init(&rwlock->rlock);
	if (rc)
		return rc;
	rc = shm_mutex_init(&rwlock->wlock);
	if (rc)
		return rc;

	atomic_store(&rwlock->num_reader, 0);
	rwlock->max_num_reader  = DEFAULT_MAX_NUM_READERS;
	rwlock->off_tid_readers = (long int)(&rwlock->tid_readers) - (long int)d_shm_head;

	return 0;
}

int
shm_rwlock_destroy(d_shm_rwlock_t *rwlock)
{
	if (rwlock->max_num_reader > DEFAULT_MAX_NUM_READERS)
		shm_free((char*)d_shm_head + rwlock->off_tid_readers);

	memset(rwlock, 0, sizeof(d_shm_rwlock_t));

	return 0;
}

#define EXPANSION_FACTOR  (1.6)
#define LOW_OCCUPANCY     (0.3)
#define SHRINK_FACTOR     (0.625)
#define SLOT_SAVED_CUTOFF (8)

static inline bool
does_rwlock_list_need_shrinking(d_shm_rwlock_t *rwlock)
{
	int slot_saved;
	int num_reader;

	/**
	 * To avoid back-and-forth shrinking/expansion, shrink reader list only if occupancy ratio
	 * is lower than LOW_OCCUPANCY and can save space for more then SLOT_SAVED_CUTOFF records.
	 */
	if (rwlock->max_num_reader == DEFAULT_MAX_NUM_READERS)
		return false;
	num_reader = atomic_load(&rwlock->num_reader);
	if (num_reader < ((int)(rwlock->max_num_reader * LOW_OCCUPANCY)))
		return true;

	slot_saved = (int)(rwlock->max_num_reader * (1.0f - SHRINK_FACTOR));
	if (slot_saved >= SLOT_SAVED_CUTOFF && (((int)(rwlock->max_num_reader * SHRINK_FACTOR) - num_reader) > 4))
		return true;
	return false;
}

#define SMALL (0.0001)
static int
expand_rwlock_reader_list(d_shm_rwlock_t *rwlock)
{
	int      new_max_num_reader;
	int     *new_reader_list;
	long int off_tid_readers_save;

	new_max_num_reader = (int)(rwlock->max_num_reader * EXPANSION_FACTOR + SMALL);
	new_reader_list = shm_alloc(sizeof(int) * new_max_num_reader);
	if (new_reader_list == NULL)
		return ENOMEM;
	/* copy existing tid list of readers */
	memcpy(new_reader_list, rwlock->off_tid_readers + (char *)d_shm_head,
	       sizeof(int) * atomic_load(&rwlock->num_reader));
	off_tid_readers_save = rwlock->off_tid_readers;
	rwlock->off_tid_readers = (long int)new_reader_list - (long int)d_shm_head;
	/* free existing reader list */
	if (rwlock->max_num_reader > DEFAULT_MAX_NUM_READERS)
		shm_free((char*)d_shm_head + off_tid_readers_save);
	rwlock->max_num_reader = new_max_num_reader;
	return 0;
}

static int
shrink_rwlock_reader_list(d_shm_rwlock_t *rwlock)
{
	int      num_reader;
	int      new_max_num_reader;
	int     *new_reader_list;
	long int off_tid_readers_save;

	new_max_num_reader = max((int)(rwlock->max_num_reader * SHRINK_FACTOR + SMALL),
				 DEFAULT_MAX_NUM_READERS);
	num_reader = atomic_load(&rwlock->num_reader);
	D_ASSERT(new_max_num_reader > num_reader);
	if (new_max_num_reader > DEFAULT_MAX_NUM_READERS) {
		new_reader_list = shm_alloc(sizeof(int) * new_max_num_reader);
		if (new_reader_list == NULL)
			return ENOMEM;
	} else {
		new_reader_list = rwlock->tid_readers;
	}
	/* copy existing tid list of readers */
	memcpy(new_reader_list, rwlock->off_tid_readers + (char *)d_shm_head,
	       sizeof(int) * num_reader);
	off_tid_readers_save = rwlock->off_tid_readers;
	rwlock->off_tid_readers = (long int)new_reader_list - (long int)d_shm_head;
	/* free existing reader list */
	shm_free((char*)d_shm_head + off_tid_readers_save);
	rwlock->max_num_reader = new_max_num_reader;

	return 0;
}
#undef SMALL

static inline bool
is_thread_ended(int tid_check)
{
	int                     tid_local = tid_check;
	int                     err;
	bool                    thread_ended;
	d_shm_mutex_t          *pid_monitor;
	struct d_shm_ht_rec_loc link;

	pid_monitor = (d_shm_mutex_t *)shm_ht_rec_find(&ht_tid, (const char *)&tid_local,
						       sizeof(int), &link, &err);
	if (pid_monitor == NULL)
		/* thread exits normally with removing the ht record */
		return true;
	thread_ended = (pid_monitor->lock & FUTEX_OWNER_DIED) ? true : false;
	shm_ht_rec_decref(&link);
	return thread_ended;
}

static int
shm_rwlock_refresh_reader_list(d_shm_rwlock_t *rwlock)
{
	int *tid_list;
	int  i;
	int  j;
	int  next = 0;
	int  num_reader;
	int  num_reader_real = 0;

	/* go through tid list to check whether processes still exist or not. Read lock is needed
	 * to avoid race condition.
	 */
	tid_list = (int *)((char *)d_shm_head + rwlock->off_tid_readers);
	num_reader = atomic_load(&rwlock->num_reader);

	for (i = 0; i < num_reader; i++) {
		if (is_thread_ended(tid_list[i]))
			/* clear tid if this process does not exist */
			tid_list[i] = 0;
	}

	for (i = 0; i < num_reader; i++) {
		if (tid_list[i] == 0) {
			/* move the next valid tid to this spot */
			j = (next == 0) ? (i + 1) : next;
			while (j < num_reader) {
				if (tid_list[j] != 0) {
					next        = j + 1;
					tid_list[i] = tid_list[j];
					tid_list[j] = 0;
					break;
				}
				j++;
			}
			if (tid_list[i] == 0)
				/* no more readers */
				break;
			else
				num_reader_real++;
			if (next >= num_reader)
				break;
		} else {
			num_reader_real++;
		}
	}
	atomic_store(&rwlock->num_reader, num_reader_real);
	if (does_rwlock_list_need_shrinking(rwlock))
		return shrink_rwlock_reader_list(rwlock);

	return 0;
}

/* time out to wait for acquiring write lock. If timeout, check read mutex owners exist or not.
 * Unit is nano second.
 */
#define WLOCK_TIMEOUT (50000)

int
shm_rwlock_rd_lock(d_shm_rwlock_t *rwlock)
{
	int  rc;
	bool mutex_owner_dead;
	int *reader_list;
	int  num_reader;

	if (!thread_monitor_inited)
		thread_monitor_init();

	rc = shm_mutex_lock_ex(&(rwlock->rlock), NULL, &mutex_owner_dead, true);

	shm_rwlock_fi_here();

	if (mutex_owner_dead) {
		/* Make read list consistent since the previous mutex owner is dead. */
		rc = shm_rwlock_refresh_reader_list(rwlock);
		if (rc) {
			shm_mutex_unlock_ex(&rwlock->rlock, false);
			return rc;
		}
	}
	num_reader = atomic_load(&rwlock->num_reader);
	if (num_reader < rwlock->max_num_reader) {
		reader_list             = (int *)(rwlock->off_tid_readers + (char *)d_shm_head);
		reader_list[num_reader] = d_tid;
		atomic_fetch_add(&rwlock->num_reader, 1);
	} else if (num_reader == rwlock->max_num_reader) {
		/* reader list is full, need to be expanded */
		rc = expand_rwlock_reader_list(rwlock);
		if (rc) {
			shm_mutex_unlock_ex(&rwlock->rlock, false);
			return rc;
		}
		/* append the new tid */
		reader_list = (int *)(rwlock->off_tid_readers + (char *)d_shm_head);
		reader_list[num_reader] = d_tid;
		atomic_fetch_add(&rwlock->num_reader, 1);
	}

	if (rwlock->num_reader == 1) {
		/* the first reader needs to lock write mutex */
		struct timespec timeout = {.tv_sec = 0, .tv_nsec = WLOCK_TIMEOUT};
		int             oldvalue;

		while (1) {
			/* special case: non-robust mutex is used since the mutex may need to be
			 * unlocked by a different process/thread!
			 */
			rc = shm_mutex_lock_ex(&rwlock->wlock, &timeout, &mutex_owner_dead, false);
			if (rc == 0) {
				break;
			} else {
				oldvalue = atomic_load_explicit(&(rwlock->wlock.lock),
								memory_order_relaxed);
				if (is_thread_ended(oldvalue & TID_MASK) &&
				    (oldvalue & NOT_ROBUST)) {
					/* The mutex owner does not exist any more. This is a
					 * non-robust mutex
					 */
					if (__sync_bool_compare_and_swap(&(rwlock->wlock.lock),
									 oldvalue, 0)) {
						/* mutex is unlocked by force */
						break;
					}
				}
			}
		}
	}

	shm_rwlock_fi_here();

	rc = shm_mutex_unlock_ex(&rwlock->rlock, false);

	shm_rwlock_fi_here();
	return rc;
}

int
shm_rwlock_rd_unlock(d_shm_rwlock_t *rwlock)
{
	int  rc;
	bool mutex_owner_dead;
	bool dec_num_reader;
	int  i;
	int *reader_list;
	int  num_reader;

	/* previous owner could */
	rc = shm_mutex_lock_ex(&(rwlock->rlock), NULL, &mutex_owner_dead, true);
	if (mutex_owner_dead) {
		shm_rwlock_fi_here();
		/* Make read list consistent since the previous mutex owner is dead. */
		rc = shm_rwlock_refresh_reader_list(rwlock);
		shm_rwlock_fi_here();
		if (rc) {
			shm_mutex_unlock_ex(&rwlock->rlock, false);
			return rc;
		}
	}
	shm_rwlock_fi_here();
	num_reader = atomic_load(&rwlock->num_reader);
	dec_num_reader = false;
	reader_list = (int *)(rwlock->off_tid_readers + (char *)d_shm_head);
	/* remove the record in tid list of readers */
	for (i = 0; i < num_reader; i++) {
		if (reader_list[i] == d_tid) {
			if (i != (num_reader - 1))
				/* move the last tid to this spot */
				reader_list[i] = reader_list[num_reader - 1];
			num_reader--;
			dec_num_reader = true;
			break;
		}
	}
	shm_rwlock_fi_here();
	D_ASSERT(dec_num_reader);
        if (does_rwlock_list_need_shrinking(rwlock))
		shrink_rwlock_reader_list(rwlock);

	if (num_reader == 0)
		/* there should be no other thread except this one holding the write lock. In case
		 * the write lock was acquired by another reader thread and this thread was killed
		 * for any reason, force unlocking this mutex then.
		 */
		shm_mutex_unlock_ex(&rwlock->wlock, true);

	shm_rwlock_fi_here();
	atomic_store(&rwlock->num_reader, num_reader);
	return shm_mutex_unlock_ex(&rwlock->rlock, false);
}

int
shm_rwlock_wr_lock(d_shm_rwlock_t *rwlock)
{
	int             rc;
	int             oldvalue;
	bool            mutex_owner_dead;
	struct timespec timeout = {.tv_sec = 0, .tv_nsec = WLOCK_TIMEOUT};

trylock:
	rc = shm_mutex_lock_ex(&rwlock->wlock, &timeout, &mutex_owner_dead, true);
	if (rc == 0) {
		if (atomic_load(&rwlock->num_reader) == 0) {
			shm_rwlock_fi_here_uniqe(__FILE__, __LINE__);
			/* holding write lock and there is no reader */
			return 0;
		} else {
			shm_rwlock_fi_here_uniqe(__FILE__, __LINE__);
			/* release write lock since readers exist */
			shm_mutex_unlock_ex(&rwlock->wlock, false);
		}
	}

	/* failed to get the write lock, need to get read lock to refresh reader list */
	rc = shm_mutex_lock_ex(&rwlock->rlock, NULL, &mutex_owner_dead, true);
	shm_rwlock_fi_here_uniqe(__FILE__, __LINE__);
	if (rc)
		return rc;
	/* we do not care whether previous read lock owner dead or not here */
	rc = shm_rwlock_refresh_reader_list(rwlock);
	if (rc)
		return rc;

	oldvalue = atomic_load_explicit(&(rwlock->wlock.lock), memory_order_relaxed);
	if (is_thread_ended(oldvalue & TID_MASK) && (oldvalue & NOT_ROBUST) &&
	    atomic_load(&rwlock->num_reader) == 0)
		/* no reader and mutex is still in locked state by a reader, then unlock by force */
		__sync_bool_compare_and_swap(&(rwlock->wlock.lock), oldvalue, 0);
	shm_mutex_unlock_ex(&rwlock->rlock, false);

	goto trylock;

	return 0;
}

int
shm_rwlock_wr_unlock(d_shm_rwlock_t *rwlock)
{
	return shm_mutex_unlock_ex(&rwlock->wlock, false);
}
