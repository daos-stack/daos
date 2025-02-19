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
	int         len_key;
	/* length of value */
	int         len_value;
	/* length of padding. Padding may be required when value is a mutex!!!! */
	int         len_padding;
	/* reference count of this record */
	_Atomic int ref_count;
	/* the index of bucket in which this record is stored */
	int         idx;
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
	/* hash table name. get_shm_ht_with_name() can get hash table by name search. */
	char     ht_name[MAX_HT_NAME_LEN];
	/* size of hash table. Forced to be power of 2. */
	int      n_bucket;
	/* number of records stored */
	int      n_lock;
	/* offset to find the previous d_shm_ht_head */
	long int prev;
	/* offset to find the next d_shm_ht_head */
	long int next;

	/**
	 * d_shm_mutex_t locks[n_lock] will be stored here. Multiple mutexes to alleviate lock
	 * contention.
	 */
	/**
	 * long int off_next[n_bucket] will be stored here. The array of offset to next shm_ht_rec
	 */
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

 * \param[out] ht_head		the head of hash table newly created
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_ht_create(const char name[], int bits, int n_lock, struct d_shm_ht_head **ht_head);

/**
 * destroy a hash table by force
 *
 * \param[in] ht_head		the head of hash table newly created
 *
 */
void
shm_ht_destroy(struct d_shm_ht_head *ht_head);

/**
 * query a shm hash table with name
 *
 * \param[in] name		name string

 * \param[out] ht_head		the head of hash table
 *
 * \return			zero for success. error code otherwise.
 */
int
get_shm_ht_with_name(const char *name, struct d_shm_ht_head **ht_head);

/**
 * lookup \p key in the hash table, the value in the record is returned on success.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] key		The key to search
 * \param[in] ksize		Size of the key

 * \param[out] link		The pointer to the hash table record
 *
 * \return			value
 */
void *
shm_ht_rec_find(struct d_shm_ht_head *ht_head, const char *key, const int ksize,
		struct shm_ht_rec **link);

/**
 * Lookup \p key in the hash table, if there is a matched record, it should be
 * returned, otherwise a new record is inserted in the hash table.
 *
 * \param[in] ht_head		Pointer to the hash table
 * \param[in] key		The key to be inserted
 * \param[in] ksize		Size of the key
 * \param[in] val		The value for the key
 * \param[in] len_value		The size of the value

 * \param[out] link		The pointer to the hash table record
 *
 * \return			value
 */
void *
shm_ht_rec_find_insert(struct d_shm_ht_head *ht_head, const char *key, const int ksize,
		       const char *val, const int len_value, struct shm_ht_rec **link);

/**
 * Search and delete the record identified by \p key from the hash table.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] key		The key of the record being deleted
 * \param[in] ksize		Size of the key
 *
 * \retval			true	Item with \p key has been deleted
 * \retval			false	Fail to remove the record
 */
bool
shm_ht_rec_delete(struct d_shm_ht_head *ht_head, const char *key, const int ksize);

/**
 * Delete the record linked by the chain \p link.
 * This record will be freed if hop_rec_free() is defined and the hash table
 * holds the last refcount.
 *
 * \param[in] ht_head		Pointer to the hash table
 * \param[in] link		The link chain of the record
 *
 * \retval			true	Successfully deleted the record
 * \retval			false	Fail to remove the record
 */
bool
shm_ht_rec_delete_at(struct d_shm_ht_head *ht_head, struct shm_ht_rec *link);

/**
 * Decrease the refcount of the record.
 *
 * \param[in] link		Chain link of the hash record
 */
void
shm_ht_rec_decref(struct shm_ht_rec *link);

/**
 * Increase the refcount of the record.
 *
 * \param[in] link		Chain link of the hash record
 */
void
shm_ht_rec_addref(struct shm_ht_rec *link);
