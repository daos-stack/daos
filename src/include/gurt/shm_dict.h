/**
 * (C) Copyright 2024-2025 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <gurt/shm_alloc.h>

/* the max length allowed for a hash table name */
#define MAX_HT_NAME_LEN      16

/* reserved string for d_shm_mutex_t as ht record value */
#define INIT_KEY_VALUE_MUTEX "INIT_MUTEX"

/* struct of the record in the hash table stored in shared memory */
struct shm_ht_rec {
	/* length of key */
	int      len_key;
	/* length of value */
	int      len_value;
	/* length of padding. Padding may be required when value is a mutex!!!! */
	int      len_padding;
	/* the index of bucket in which this record is stored */
	int      idx;
	/* offset pointer to the previous record in record link list */
	long int prev;
	/* offset pointer to the next record in record link list */
	long int next;

	/* char key[len_key] will be stored here */
	/* char padding[len_padding] may be stored here */
	/* char value[len_value] will be stored here */
};

/* struct of the head of the hash table stored in shared memory */
struct d_shm_ht_head {
	/* This 64 bits contains two parts, a randomly generated ht id (lower 48 bits) and the
	 * number of threads accessing current hash table (upper 16 bits). ht id needs a local copy
	 * which is required in ht record search, insert, and remove.
	 */
	_Atomic int64_t nuser_htid;

	/* hash table name. get_shm_ht_with_name() can get hash table by name search. */
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

/* local struct for accessing a hash table stored in shared memory */
struct d_shm_ht_loc {
	/* the address of ht head in current process */
	struct d_shm_ht_head *ht_head;
	/* a local copy of ht_id */
	int64_t               ht_id;
};

/* the address of shared memory region */
extern struct d_shm_hdr *d_shm_head;

/**
 * create a hash table with given name, size (2^bits), number of locks if it does not exist.
 * number of buckets, 2^bits, needs to be a multiplier of number of locks.
 *
 * \param[in] name		name string
 * \param[in] bits		used to set the number of buckets, 2^bits 
 * \param[in] n_lock		the number of locks shared by buckets

 * \param[out] shm_ht_loc	local struct contains ht_head in shm and ht_id local copy
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_ht_create(const char name[], int bits, int n_lock, struct d_shm_ht_loc *shm_ht_loc);

/**
 * destroy a hash table
 *
 * \param[in] shm_ht_loc	local struct contains ht_head in shm and ht_id local copy
 *
 */
void
shm_ht_destroy(struct d_shm_ht_loc *shm_ht_loc);

/**
 * query a shm hash table with name
 *
 * \param[in] name		name string

 * \param[out] shm_ht_loc	local struct contains ht_head in shm and ht_id local copy
*
 * \return			zero for success. error code otherwise.
 */
int
get_shm_ht_with_name(const char *name, struct d_shm_ht_loc *shm_ht_loc);

/**
 * lookup \p key in the hash table, the value in the record is returned on success.
 *
 * \param[in] shm_ht_loc	local struct contains ht_head in shm and ht_id local copy
 * \param[in] key		The key to search
 * \param[in] ksize		Size of the key

 * \param[out] link		The pointer to the hash table record
 *
 * \return			value
 */
void *
shm_ht_rec_find(struct d_shm_ht_loc *shm_ht_loc, const char *key, const int ksize,
		struct shm_ht_rec **link);

/**
 * Lookup \p key in the hash table, if there is a matched record, it should be
 * returned, otherwise a new record is inserted in the hash table.
 *
 * \param[in] shm_ht_loc	local struct contains ht_head in shm and ht_id local copy
 * \param[in] key		The key to be inserted
 * \param[in] ksize		Size of the key
 * \param[in] val		The value for the key
 * \param[in] len_value		The size of the value

 * \param[out] link		The pointer to the hash table record
 *
 * \return			value
 */
void *
shm_ht_rec_find_insert(struct d_shm_ht_loc *shm_ht_loc, const char *key, const int ksize,
		       const char *val, const int len_value, struct shm_ht_rec **link);

/**
 * Search and delete the record identified by \p key from the hash table.
 *
 * \param[in] shm_ht_loc	local struct contains ht_head in shm and ht_id local copy
 * \param[in] key		The key of the record being deleted
 * \param[in] ksize		Size of the key
 *
 * \retval			true	Item with \p key has been deleted
 * \retval			false	Fail to remove the record
 */
bool
shm_ht_rec_delete(struct d_shm_ht_loc *shm_ht_loc, const char *key, const int ksize);

/**
 * Delete the record linked by the chain \p link.
 *
 * \param[in] shm_ht_loc	local struct contains ht_head in shm and ht_id local copy
 * \param[in] ht_id		the local copy of ht_id stored in ht head
 * \param[in] link		The link chain of the record
 *
 * \retval			true	Successfully deleted the record
 * \retval			false	Fail to remove the record
 */
bool
shm_ht_rec_delete_at(struct d_shm_ht_loc *shm_ht_loc, struct shm_ht_rec *link);

/**
 * Query whether a hash table is still usable or not with comparing ht id and saved local copy.
 * This hash table and its data should not be used any more if it returns false.
 *
 * \param[in] shm_ht_loc	local struct contains ht_head in shm and ht_id local copy
 *
 * \retval			true	Hash table is valid
 * \retval			false	Hash table is NOT valid
 */
bool
shm_ht_is_usable(struct d_shm_ht_loc *shm_ht_loc);
