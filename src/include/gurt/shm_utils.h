/**
 * (C) Copyright 2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_SHM_UTILS_H__
#define __DAOS_SHM_UTILS_H__

#include <stdatomic.h>

/* default value for invalid offset pointer */
#define INVALID_OFFSET          (-1L)

#define INVALID_FI_POINT        0x7FFFFFFF
#define HT_NAME_FI              "shm_rwlock_fi"

/* memory block alignment in shared memory */
#define SHM_MEM_ALIGN           4UL

/* default size of the pre-allocate buffer for tid list of readers in rwlock */
#define DEFAULT_MAX_NUM_READERS (8)

/* the offset of field "next" in struct pthread_mutex_t. Need to be consistent with the robust mutex
 * in pthread library.
 */
#define NEXT_OFFSET_IN_MUTEX    (32)

/* the struct of robust mutex based on shared memory */
struct d_shm_mutex {
	_Atomic int         lock;
	/* this field is not used. It is used to be compatible with robust mutex in pthread */
	char                padding[NEXT_OFFSET_IN_MUTEX - sizeof(void *) * 1 - sizeof(int) * 1];
	/* pointer to previous record of robust mutex */
	struct robust_list *prev;
	/* pointer to next record of robust mutex */
	struct robust_list *next;
};

typedef struct d_shm_mutex d_shm_mutex_t;

/* the struct of rwlock based on shared memory */
struct d_shm_rwlock {
	/* mutex to get the access to read */
	d_shm_mutex_t rlock;
	/* mutex to get the access to write */
	d_shm_mutex_t wlock;

	/* the maximum number of reader tid can be stored */
	int           max_num_reader;
	/* current number of reader holding read lock */
	_Atomic int   num_reader;
	/* offset of the array of readers' tid */
	long int      off_tid_readers;
	/**
	 * pre-allocated space for tid list of readers. If the list is long, need to dynamically
	 * allocate a larger memory block then.
	 */
	int           tid_readers[DEFAULT_MAX_NUM_READERS];
};

typedef struct d_shm_rwlock d_shm_rwlock_t;

/* the max length allowed for a hash table name */
#define MAX_HT_NAME_LEN       16

/* reserved string for d_shm_mutex_t as ht record value */
#define INIT_KEY_VALUE_MUTEX  "INIT_MUTEX"

/* reserved string for d_shm_rwlock_t as ht record value */
#define INIT_KEY_VALUE_RWLOCK "INIT_RWLOCK"

#define HT_NAME_TID_MUTEX     "TID_MUTEX"

/* error code for hash table related functions */
enum SHM_HT_ERROR {
	SHM_HT_SUCCESS     = 0,
	SHM_HT_INVALID_ARG = EINVAL,
	SHM_HT_NOT_EXIST   = ENOENT,
	SHM_HT_BUSY        = 0xA0,
	SHM_HT_INVALID_HT,
	SHM_HT_NEGATIVE_REF,
	SHM_HT_REC_BUSY,
	SHM_HT_REC_INVALID,
	SHM_HT_REC_NOT_EXIST,
	SHM_HT_REC_NEGATIVE_REF
};

/* struct of the record in the hash table stored in shared memory */
struct d_shm_ht_rec {
	/* length of key */
	int         len_key;
	/* length of value */
	int         len_value;
	/* length of padding. Padding may be required when value is a mutex!!!! */
	int         len_padding;
	/* the index of bucket in which this record is stored */
	int         idx;
	/* reference count of this record */
	_Atomic int ref_count;
	/* dummy int for padding */
	int         dummy_int;
	/* offset pointer to the previous record in record link list */
	long int    prev;
	/* offset pointer to the next record in record link list */
	long int    next;

	/* char key[len_key] will be stored here */
	/* char padding[len_padding] may be stored here */
	/* char value[len_value] will be stored here */
};

/* struct of the head of the hash table stored in shared memory */
struct d_shm_ht_head {
	/* This 64 bits contains two parts, a randomly generated ht id (lower 40 bits) and the
	 * number of reference accessing current hash table (upper 24 bits). ht id needs a local
	 * copy which is required in ht record search, insert, and remove.
	 */
	_Atomic int64_t nref_htid;

	/* hash table name. shm_ht_open_with_name() can get hash table by name search. */
	char            ht_name[MAX_HT_NAME_LEN];
	/* size of hash table. Forced to be power of 2. */
	int             n_bucket;
	/* number of records stored */
	int             n_lock;
	/* offset to find the previous d_shm_ht_head */
	long int        prev;
	/* offset to find the next d_shm_ht_head */
	long int        next;

	/**
	 * d_shm_mutex_t locks[n_lock] will be stored here. Multiple mutexes to alleviate lock
	 * contention.
	 */
	/**
	 * long int off_next[n_bucket] will be stored here. The array of offset to next shm_ht_rec
	 */
};

typedef struct d_shm_ht_head *d_shm_ht_head_t;

/* local struct for accessing a hash table stored in shared memory */
struct d_shm_ht_loc {
	/* the address of ht head in current process */
	d_shm_ht_head_t ht_head;
	/* a local copy of ht_id which can be used for verification */
	int64_t         ht_id;
};

/* the address of shared memory region */
extern struct d_shm_hdr     *d_shm_head;

typedef struct d_shm_ht_loc *d_shm_ht_loc_t;
typedef struct d_shm_ht_rec *d_shm_ht_rec_t;

/* local struct for accessing a hash table record stored in shared memory */
struct d_shm_ht_rec_loc {
	/* ht head in shm which can be used to check ht is valid or not */
	struct d_shm_ht_loc ht_head_loc;
	/* the record in shm */
	d_shm_ht_rec_t      ht_rec;
};

typedef struct d_shm_ht_rec_loc *d_shm_ht_rec_loc_t;

/**
 * Initialize shared memory region in current process
 *
 * \return			zero for success. return error code otherwise.
 */
int
shm_init(void);

/**
 * Unmap and decrease the reference count of shared memory. Shared memory should not be referenced
 * after shm_dec_ref() is called.
 */
void
shm_fini(void);

/**
 * Allocate memory from shared memory region
 *
 * \param[in] size		size of memory block requested
 *
 * \return			buffer address
 */
void *
shm_alloc(size_t size);

/**
 * Remove shared memory file under /dev/shm/ when tests finish
 *
 * \param[in] force		remove shared memory file immediately
 */
void
shm_destroy(bool force);

/**
 * Allocate memory from shared memory region with alignment
 *
 * \param[in] align		size of alignment
 * \param[in] size		size of memory block requested
 *
 * \return			buffer address
 */
void *
shm_memalign(size_t align, size_t size);

/**
 * Free a memory block which was allocated from shared memory region
 *
 * \param[in] ptr		memory block address
 *
 */
void
shm_free(void *ptr);

/**
 * Query whether shared memory region is initialized properly or not
 *
 * \return			True/False
 */
bool
shm_inited(void);

/**
 * query the base address of shared memory region
 */
void *
shm_base(void);

/**
 * initlize a mutex
 *
 * \param[in] mutex		pointer to metex
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_mutex_init(d_shm_mutex_t *mutex);

/**
 * lock a mutex
 *
 * \param[in] mutex		pointer to metex
 * \param[out] pre_owner_dead	whether the previous locker owner dead
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_mutex_lock(d_shm_mutex_t *mutex, bool *pre_owner_dead);

/**
 * unlock a mutex
 *
 * \param[in] mutex		pointer to metex
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_mutex_unlock(d_shm_mutex_t *mutex);

/**
 * initlize a readers-writer lock
 *
 * \param[in] rwlock		pointer to rwlock
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_rwlock_init(d_shm_rwlock_t *rwlock);

/**
 * destroy a readers-writer lock
 *
 * \param[in] rwlock		pointer to rwlock
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_rwlock_destroy(d_shm_rwlock_t *rwlock);

/**
 * lock a readers-writer lock for read
 *
 * \param[in] rwlock		pointer to rwlock
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_rwlock_rd_lock(d_shm_rwlock_t *rwlock);

/**
 * unlock a readers-writer lock by a reader
 *
 * \param[in] rwlock		pointer to rwlock
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_rwlock_rd_unlock(d_shm_rwlock_t *rwlock);

/**
 * lock a readers-writer lock for write
 *
 * \param[in] rwlock		pointer to rwlock
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_rwlock_wr_lock(d_shm_rwlock_t *rwlock);

/**
 * unlock a readers-writer lock by a writer
 *
 * \param[in] rwlock		pointer to rwlock
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_rwlock_wr_unlock(d_shm_rwlock_t *rwlock);

/**
 * initialize thread local data and set up a monitor for current thread
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_thread_data_init(void);

/**
 * destroy the monitor for current thread
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_thread_data_fini(void);

#if FAULT_INJECTION
/**
 * initialize parameters used for fault injection to test shared memory utilities
 */
void
shm_fi_init(void);

/**
 * return the accumulated number of fault injection targets
 *
 * \return			the number of fault injection targets
 */
int
shm_fi_counter_value(void);

/**
 * set the first target of fault injection
 *
 * \param[in] fi_p		the first fault injection target
 */
void
shm_fi_set_p1(int fi_p);

/**
 * set the second target of fault injection
 *
 * \param[in] fi_p		the second fault injection target
 */
void
shm_fi_set_p2(int fi_p);
#endif

/**
 * create a hash table with given name, size (2^bits), number of locks if it does not exist.
 * number of buckets, 2^bits, needs to be a multiplier of number of locks. Hash table reference
 * is increased by 1.
 *
 * \param[in] name		name string
 * \param[in] bits		used to set the number of buckets, 2^bits
 * \param[in] n_lock		the number of locks shared by buckets

 * \param[out] shm_ht_loc	local struct contains ht_head in shm and ht_id local copy
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_ht_create(const char name[], int bits, int n_lock, d_shm_ht_loc_t shm_ht_loc);

/**
 * destroy a hash table. destroy will fail immediately if reference count is non-zero when force
 * is false. Memory of ht records with zero reference count will be freed if force is true.
 *
 * \param[in] shm_ht_loc	local struct contains ht_head in shm and ht_id local copy
 * \param[in] force		destroy hash table by force
 *
 */
int
shm_ht_destroy(d_shm_ht_loc_t shm_ht_loc, bool force);

/**
 * open a shm hash table with name. Hash table reference is increased by 1.
 *
 * \param[in] name		name string
 *
 * \param[out] shm_ht_loc	local struct contains ht_head in shm and ht_id local copy
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_ht_open_with_name(const char *name, d_shm_ht_loc_t shm_ht_loc);

/**
 * decrease hash table reference by 1.
 *
 * \param[in] shm_ht_loc	local struct contains ht_head in shm and ht_id local copy
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_ht_decref(d_shm_ht_loc_t shm_ht_loc);

/**
 * return hash table reference. Mainly used for debugging.
 *
 * \param[in] shm_ht_loc	local struct contains ht_head in shm and ht_id local copy
 *
 * \return			ht reference count. Negative number for invalid ht
 */
int
shm_ht_num_ref(d_shm_ht_loc_t shm_ht_loc);

/**
 * check whether a hash table is still usable or not with comparing ht id and saved local copy.
 *
 * \param[in] shm_ht_loc	local struct contains ht_head in shm and ht_id local copy
 *
 * \retval			true/false Hash table is valid/invalid
 */
bool
shm_ht_is_usable(d_shm_ht_loc_t shm_ht_loc);

/**
 * lookup \p key in the hash table, the value in the record is returned on success.
 *
 * \param[in] shm_ht_loc	local struct contains ht_head in shm and ht_id local copy
 * \param[in] key		the key to search
 * \param[in] ksize		size of the key

 * \param[out] rec_loc		pointer to local struct for accessing a hash table record. If not
 *				null, record refcount is increased by 1
 * \param[out] err		error code
 *
 * \return			value
 */
void *
shm_ht_rec_find(d_shm_ht_loc_t shm_ht_loc, const char *key, const int ksize,
		d_shm_ht_rec_loc_t rec_loc, int *err);

/**
 * lookup \p key in the hash table, if there is a matched record, it should be
 * returned, otherwise a new record is inserted in the hash table.
 *
 * \param[in] shm_ht_loc	local struct contains ht_head in shm and ht_id local copy
 * \param[in] key		the key to be inserted
 * \param[in] ksize		size of the key
 * \param[in] val		the value for the key
 * \param[in] len_value		the size of the value

 * \param[out] rec_loc		pointer to local struct for accessing a hash table record. If not
 *				null, record refcount is increased by 1
 * \param[out] created		whether a new hash record was created or not
 * \param[out] err		error code
 *
 * \return			value
 */
void *
shm_ht_rec_find_insert(d_shm_ht_loc_t shm_ht_loc, const char *key, const int ksize, const char *val,
		       const int len_value, d_shm_ht_rec_loc_t rec_loc, bool *created, int *err);

/**
 * decrease the refcount of the record by 1.
 *
 * \param[in] rec_loc		pointer to local struct for accessing a hash table record
 */
int
shm_ht_rec_decref(d_shm_ht_rec_loc_t rec_loc);

/**
 * search and delete the record identified by \p key from the hash table.
 *
 * \param[in] shm_ht_loc	local struct contains ht_head in shm and ht_id local copy
 * \param[in] key		the key of the record being deleted
 * \param[in] ksize		size of the key
 *
 * \retval			0 - success, otherwise errno
 */
int
shm_ht_rec_delete(d_shm_ht_loc_t shm_ht_loc, const char *key, const int ksize);

/**
 * delete the record linked by the chain \p link.
 *
 * \param[in] rec_loc		pointer to local struct for accessing a hash table record
 *
 * \retval			0 - success, otherwise errno
 */
int
shm_ht_rec_delete_at(d_shm_ht_rec_loc_t rec_loc);

/**
 * return the address of the data stored in ht record. It does not affect record reference.
 *
 * \param[in] rec_loc		pointer to local struct for accessing a hash table record
 *
 * \param[out] err		error code
 *
 * \return			address of the data
 */
void *
shm_ht_rec_data(d_shm_ht_rec_loc_t rec_loc, int *err);

/**
 * return the reference count of a ht record. Mainly used for debugging.
 *
 * \param[in] rec_loc		pointer to local struct for accessing a hash table record
 *
 * \return			ht record reference count. Negative number for invalid ht
 */
int
shm_ht_rec_num_ref(d_shm_ht_rec_loc_t rec_loc);

/* dynamic allocation if data is larger than this threshold */
#define LRU_ALLOC_SIZE_THRESHOLD (4096)

/* Node in LRU cache */
typedef struct shm_lru_node {
	/* key size*/
	int         key_size;
	/* data size*/
	int         data_size;
	/* store key if length is not larger than sizeof(long int), otherwise store the offset to key */
	long int    key;
	/* store data if length is not larger than sizeof(long int), otherwise store offset to data */
	long int    data;
	/* the reference count of this record */
	_Atomic int ref_count;
	/* the index of hash bucket this record is in */
	int         idx_bucket;
	/* off_prev and off_next are used in doubly linked list for LRU */
	int         off_prev;
	int         off_next;
	/* offset to the next node in hash chain in each bucket for allocated node. point to next
	 * available node for free nodes
	 */
	int         off_hnext;
} shm_lru_node_t;

/* This implementation of shm LRU is mainly optimized for performance by using pre-allocated buffer
 * when possible
 */

/* LRU Cache structure */
typedef struct {
	/* max number of nodes to hold */
	int           capacity;
	/* number of nodes */
	int           size;
	/* Most recently used node */
	int           off_head;
	/* Least recently used node */
	int           off_tail;
	/* First available/free node */
	int           first_av;
	/* the size of key. zero means key size is variable */
	int           key_size;
	/* the size of data. zero means data size is variable */
	int           data_size;
	/* the offset to the array of offset of hash buckets */
	int           off_hashbuckets;
	/* the offset to the array of preallocated array of nodes */
	int           off_nodelist;
	/* the offset to the array of preallocated array of keys */
	long int      off_keylist;
	/* the offset to the array of preallocated array of data */
	long int      off_datalist;
	d_shm_mutex_t lock;
} shm_lru_cache_t;

enum SHM_LRU_ERROR {
	SHM_LRU_SUCCESS = 0,
	SHM_LRU_NO_SPACE,
	SHM_LRU_OUT_OF_MEM,
	SHM_LRU_REC_NOT_FOUND
};

enum SHM_LRU_CACHE_TYPE {
	CACHE_DENTRY = 0,
	CACHE_DATA
};

/* key for data caching: object id of file + offset */
#define KEY_SIZE_FILE_ID_OFF (sizeof(uint64_t)*2 + sizeof(off_t))

#define DEFAULT_CACHE_DATA_CAPACITY (2048)
#define DEFAULT_CACHE_DATA_SIZE     (512 * 1024)
#define MAX_PREFETCH_READ_SIZE      (2 * 1024 * 1024)

/**
 * create LRU cache
 *
 * \param[in] capacity		max number of records in cache
 * \param[in] key_size		size of key in bytes. zero for non-uniform size
 * \param[in] data_size		size of data in bytes. zero for non-uniform size
 *
 * \param[out] cache		LRU cache created
 *
 * \return					error code
 */
int
shm_lru_create_cache(int capacity, int key_size, int data_size, shm_lru_cache_t **cache);

/**
 * decrease the reference count of a LRU cache node after retrieving data
 *
 * \param[in] node			LRU node
 */
void
shm_lru_node_dec_ref(shm_lru_node_t *node);

/**
 * create/update LRU record
 *
 * \param[in] cache			LRU cache
 * \param[in] key			key
 * \param[in] key_size		size of key in bytes
 * \param[in] data			data
 * \param[in] data_size		size of data in bytes
 *
 * \return					error code
 */
int
shm_lru_put(shm_lru_cache_t *cache, void *key, int key_size, void *data, int data_size);

/**
 * query LRU cache
 *
 * \param[in] cache			LRU cache
 * \param[in] key			key
 * \param[in] key_size		size of key in bytes
 *
 * \param[out] node_found	LRU cache node containing the key
 * \param[out] val			LRU cache node containing the key
 *
 * \return					error code. 0 - success, otherwise error
 */
int
shm_lru_get(shm_lru_cache_t *cache, void *key, int key_size, shm_lru_node_t **node_found, void **val);

/**
 * destroy LRU cache
 *
 * \param[in] cache			LRU cache created
 *
 * \return					error code
 */
void
shm_lru_destroy_cache(shm_lru_cache_t *cache);

/**
 * Query LRU cache saved in shm header
 *
 * \param[in] type			type of LRU cache: data cache or dentry
 *
 * \return					LRU cache
 */
shm_lru_cache_t *
shm_lru_get_cache(enum SHM_LRU_CACHE_TYPE type);

void
printCache(shm_lru_cache_t *cache);

#endif
