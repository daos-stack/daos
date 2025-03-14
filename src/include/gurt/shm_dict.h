/**
 * (C) Copyright 2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <gurt/shm_alloc.h>

/* the max length allowed for a hash table name */
#define MAX_HT_NAME_LEN      16

/* reserved string for d_shm_mutex_t as ht record value */
#define INIT_KEY_VALUE_MUTEX "INIT_MUTEX"

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
 * \param[out] err		error code
 *
 * \return			value
 */
void *
shm_ht_rec_find_insert(d_shm_ht_loc_t shm_ht_loc, const char *key, const int ksize, const char *val,
		       const int len_value, d_shm_ht_rec_loc_t rec_loc, int *err);

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
