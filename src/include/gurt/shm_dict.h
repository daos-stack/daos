/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <gurt/shm_alloc.h>

/* the max length allowed for a hash table name */
#define MAX_HT_NAME_LEN 16

/* reserved string for pthread_rwlockattr_t as ht record value */
#define KEY_VALUE_PTHREAD_RWLOCK "INIT_PTHREAD_RWLOCK"

/* reserved string for pthread_mutexattr_t as ht record value */
#define KEY_VALUE_PTHREAD_LOCK   "INIT_PTHREAD_LOCK"

/* struct of the record in the hash table stored in shared memory */
struct shm_ht_rec {
	/* length of key */
	int      len_key;
	/* length of value */
	int      len_value;
	/* length of padding. Padding may be needed when value is a mutex!!!! */
	int      len_padding;
	/* reference count of this record */
	int      ref_count;
	/* the index of the mutex to be locked when updating this record */
	int      idx_lock;
	/* offset pointer to the previous record in record link list */
	long int prev;
	/* offset pointer to the next record in record link list */
	long int next;

	/* char key[len_key] will be stored here */
	/* char value[len_value] will be stored here */
};

/* struct of the head of the hash table stored in shared memory */
struct d_shm_ht_head {
	char     ht_name[MAX_HT_NAME_LEN];
	int      n_bucket;           /* size of hash table */
	int      n_lock;              /* number of records stored */
	long int prev;  /* offset to find the previous d_shm_ht_head */
	long int next;  /* offset to find the next d_shm_ht_head */

	/**
	 * pthread_mutex_t locks[n_lock] will be stored here. Multiple mutexes to alleviate lock
	 *  contention
	 */
	/**
	 * long int off_next[n_bucket] will be stored here. The array of offset to next shm_ht_rec
	 */
};

/* the address of shared memory region */
extern struct d_shm_alloc *d_shm_head;

int
shm_ht_create(const char name[], int bits, int n_lock, struct d_shm_ht_head **ht_head);

int
shm_ht_destroy(struct d_shm_ht_head *ht_head, int force);

int
get_ht_with_name(const char *name, struct d_shm_ht_head **ht_head);

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
shm_ht_rec_find(struct d_shm_ht_head *ht_head, const char *key, const int ksize, struct shm_ht_rec
		**link);

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
 * \retval			false	Can't find the record by \p key
 */
int
shm_ht_rec_delete(struct d_shm_ht_head *ht_head, const char *key, const int ksize);

/**
 * Delete the record linked by the chain \p link.
 * This record will be freed if hop_rec_free() is defined and the hash table
 * holds the last refcount.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] link		The link chain of the record
 *
 * \retval			true	Successfully deleted the record
 * \retval			false	The record has already been unlinked
 *					from the hash table
 */
int
shm_ht_rec_delete_at(struct d_shm_ht_head *ht_head, struct shm_ht_rec *link);

/**
 * Decrease the refcount of the record.
 * The record will be freed if hop_decref() returns true and the EPHEMERAL bit
 * is set.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] link		Chain link of the hash record
 */
void
shm_ht_rec_decref(struct d_shm_ht_head *ht_head, struct shm_ht_rec *link);

/**
 * Increase the refcount of the record.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] link		The link chain of the record
 */
void
shm_ht_rec_addref(struct d_shm_ht_head *ht_head, struct shm_ht_rec *link);
