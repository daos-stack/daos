/*
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * DAOS S3 API
 *
 * The DS3 API provides an emulation of the S3 API over DAOS.
 * An S3 bucket map to one container.
 */

#ifndef __DAOS_S3_H__
#define __DAOS_S3_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <daos_event.h>

/** Maximum bucket name length */
#define DS3_MAX_BUCKET_NAME DAOS_PROP_LABEL_MAX_LEN + 1

/** Maximum key length */
#define DS3_MAX_KEY         DFS_MAX_PATH

/** Maximum user info length */
#define DS3_MAX_USER_NAME   DFS_MAX_NAME

// TODO This is temporarily moved here
#define METADATA_DIR_LIST                                                                          \
	X(USERS_DIR, "users")                                                                      \
	X(EMAILS_DIR, "emails")                                                                    \
	X(ACCESS_KEYS_DIR, "access_keys")                                                          \
	X(MULTIPART_DIR, "multipart")

/* Define for RPC enum population below */
#define X(a, b) a,
enum meta_dir { METADATA_DIR_LIST METADATA_DIR_LAST };
#undef X

/** DAOS S3 Pool handle */
typedef struct {
	/** Pool handle */
	daos_handle_t    poh;
	/** Pool information */
	daos_pool_info_t pinfo;
	/** Metadata container handle */
	daos_handle_t    meta_coh;
	/** Metadata dfs mount */
	dfs_t           *meta_dfs;
	/** Array of metadata dir handle */
	dfs_obj_t       *meta_dirs[METADATA_DIR_LAST];
} ds3_t;

/** DAOS S3 Bucket handle */
typedef struct ds3_bucket {
	/** DAOS container handle */
	daos_handle_t    coh;
	/** Container information */
	daos_cont_info_t cont_info;
	/** DFS handle */
	dfs_t           *dfs;
} ds3_bucket_t;

typedef struct {
    dfs_obj_t* dfs_obj;
} ds3_obj_t;

// TODO end of temporarily moved here

/** DAOS S3 Pool handle */
// typedef struct ds3        ds3_t;

/** DAOS S3 Bucket handle */
// typedef struct ds3_bucket ds3_bucket_t;

/** DAOS S3 Object handle */
// typedef struct ds3_obj    ds3_obj_t;

/** S3 User information */
struct ds3_user_info {
	/** User name */
	const char  *name;
	/** User email */
	const char  *email;
	/** User access ids */
	const char **access_ids;
	/** Length of access_ids */
	const size_t access_ids_nr;
	/** Opaque encoded user info */
	void        *encoded;
	/** Length of encoded data */
	size_t       encoded_length;
};

/** S3 Bucket information */
struct ds3_bucket_info {
	/** Bucket name */
	char   name[DS3_MAX_BUCKET_NAME];
	/** Opaque encoded bucket info */
	void  *encoded;
	/** Length of encoded data */
	size_t encoded_length;
};

/** S3 Object information */
struct ds3_object_info {
	/** Object key */
	char   key[DS3_MAX_KEY];
	/** Opaque encoded bucket info */
	void  *encoded;
	/** Length of encoded data */
	size_t encoded_length;
};

/** S3 Common Prefix information */
struct ds3_common_prefix_info {
	/** Common Prefix */
	char prefix[DS3_MAX_KEY];
};

// General S3

/**
 * Initialize all the relevant DAOS libraries.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_init();

/**
 * Finalize the relevant DAOS libraries if necessary.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_fini();

/**
 * Connect to the pool where all buckets are/will be stored and
 * return a ds3 handle.
 *
 * \param[in]	pool	Pool label or UUID string to connect to.
 * \param[in]	sys	DAOS system name to use for the pool connect.
 *			Pass NULL to use the default system.
 * \param[out]	ds3	Pointer to the created DAOS S3 pool handle
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_connect(const char *pool, const char *sys, ds3_t **ds3, daos_event_t *ev);

/**
 * Release the DAOS S3 pool handle.
 *
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to release.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_disconnect(ds3_t *ds3, daos_event_t *ev);

// S3 users

/**
 * Add/update user information in the S3 user database.
 *
 * \param[in]	name	Name of the S3 user to look up.
 * \param[in]	info	User info.
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_user_set(const char *name, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev);

/**
 * Remove user from S3 user database.
 *
 * \param[in]	name	Name of the S3 user to look up.
 * \param[in]	info	User info. Necessary to remove symlinks
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_user_remove(const char *name, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev);

/**
 * Look-up S3 user information by name.
 *
 * \param[in]	name	Name of the S3 user to look up.
 * \param[in]	info	User info.
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_user_get(const char *name, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev);

/**
 * Look-up S3 user information by email.
 *
 * \param[in]	email	Email of the S3 user to look up.
 * \param[in]	info	User info.
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_user_get_by_email(const char *email, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev);

/**
 * Look-up S3 user information by key.
 *
 * \param[in]	key	Key associated with the S3 user to look up.
 * \param[in]	info	User info.
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_user_get_by_key(const char *key, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev);

// S3 Buckets

/**
 * List buckets stored in the DAOS pool identified by \a ds3.
 *
 * \param[in,out]
 *		nbuck	[in] \a buf length in items.
 *			[out] Number of buckets returned.
 * \param[out]	buf	Array of bucket info structures.
 * \param[in,out]
 *		marker	[in] Start listing from marker key
 *			[out] Returned marker key for next call
 * \param[out]	is_truncated	Are the results truncated
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_bucket_list(daos_size_t *nbuck, struct ds3_bucket_info *buf, char *marker, bool *is_truncated,
		ds3_t *ds3, daos_event_t *ev);

/**
 * Create a bucket in the DAOS pool identified by \a ds3.
 * Optionally set attributes for hints on the container.
 *
 * \param[in]	name	Bucket name. Must be unique in the pool.
 * \param[in]	info	Bucket info to be added to the bucket.
 * \param[in]	attr	Optional set of properties and attributes to set on the container.
 *			Pass NULL if none.
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_bucket_create(const char *name, struct ds3_bucket_info *info, dfs_attr_t *attr, ds3_t *ds3,
		  daos_event_t *ev);

/**
 * Destroy a bucket in the DAOS pool identified by \a ds3.
 *
 * \param[in]	name	Name of the bucket to destroy.
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_bucket_destroy(const char *name, ds3_t *ds3, daos_event_t *ev);

/**
 * Open a bucket.
 *
 * \param[in]	name	Name of the bucket to destroy.
 * \param[out]	ds3b	Returned S3 bucket handle.
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_bucket_open(const char *name, ds3_bucket_t **ds3b, ds3_t *ds3, daos_event_t *ev);

/**
 * Close a bucket handle.
 *
 * \param[in]	ds3b	S3 bucket handle to close.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_bucket_close(ds3_bucket_t *ds3b, daos_event_t *ev);

/**
 * Get S3 bucket info.
 *
 * \param[out]	info	Returned S3 bucket info.
 * \param[in]	ds3b	Pointer to the S3 bucket handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_bucket_get_info(struct ds3_bucket_info *info, ds3_bucket_t *ds3b, daos_event_t *ev);

/**
 * Set S3 bucket info.
 *
 * \param[in]	info	S3 bucket info.
 * \param[in]	ds3b	Pointer to the S3 bucket handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_bucket_set_info(struct ds3_bucket_info *info, ds3_bucket_t *ds3b, daos_event_t *ev);

/**
 * List S3 objects stored in the S3 bucket identified by \a ds3b.
 *
 * \param[in,out]
 *		nobj	[in] \a objs length in items.
 *			[out] Number of objects returned.
 * \param[out]	objs	Array of object info structures.
 * \param[in,out]
 *		ncp	[in] \a cps length in items.
 *			[out] Number of objects returned.
 * \param[out]	cps	Array of object info structures.
 * \param[in]	prefix	List objects that start with this prefix.
 * \param[in]	delim	Divide results by delim.
 * \param[in,out]
 *		marker	[in] Start listing from marker key.
 *			[out] Next marker to be used by subsequent calls.
 * \param[in]	list_versions	Also include versions
 * \param[out]	is_truncated	Are the results truncated
 * \param[in]	ds3b	Pointer to the S3 bucket handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_bucket_list_obj(uint32_t *nobj, struct ds3_object_info *objs, uint32_t *ncp,
		    struct ds3_common_prefix_info *cps, const char *prefix, const char *delim,
		    char *marker, bool list_versions, bool *is_truncated, ds3_bucket_t *ds3b,
		    daos_event_t *ev);

// TODO multipart api

// S3 Objects

/**
 * Create an S3 Object in the S3 bucket identified by \a ds3b.
 *
 * \param[in]	key	Key of the S3 object to destroy.
 * \param[out]	ds3o	Returned S3 object handle.
 * \param[in]	ds3b	ds3b	Pointer to the S3 bucket handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_obj_create(const char *key, ds3_obj_t **ds3o, ds3_bucket_t *ds3b);

/**
 * Open an S3 object.
 *
 * \param[in]	key	Key of the object to open.
 * \param[out]	ds3o	Returned S3 object handle.
 * \param[in]	ds3b	Pointer to the S3 bucket handle to use.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_obj_open(const char *key, ds3_obj_t **ds3o, ds3_bucket_t *ds3b);

/**
 * Close an object handle.
 *
 * \param[in]	ds3o	S3 object handle to close.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_obj_close(ds3_obj_t *ds3o);

/**
 * Get S3 object info.
 *
 * \param[out]	info	Returned S3 object info.
 * \param[in]	ds3o	Pointer to the S3 object handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_obj_get_info(struct ds3_object_info *info, ds3_obj_t *ds3o, daos_event_t *ev);

/**
 * Set S3 object info.
 *
 * \param[in]	info	S3 object info.
 * \param[in]	ds3o	Pointer to the S3 object handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_obj_set_info(struct ds3_object_info *info, ds3_obj_t *ds3o, daos_event_t *ev);

/**
 * Read S3 object data.
 *
 * \param[in,out]
 *		buf	[in]: Allocated buffer for data.
 *			[out]: Actual data read.
 * \param[in]	off	Offset into the file to read from.
 * \param[in,out]
 *		size	[in]: Size of buffer passed in.
 * 				[out]: Actual size of data read.
 * \param[in]	ds3o	S3 object handle to read from.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_obj_read(void *buf, daos_off_t off, daos_size_t *size, ds3_obj_t *ds3o, daos_event_t *ev);

/**
 * Destroy an S3 object in the S3 bucket identified by \a ds3b.
 *
 * \param[in]	key	Key of the S3 object to destroy.
 * \param[in]	ds3b	ds3b	Pointer to the S3 bucket handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_obj_destroy(const char *key, ds3_bucket_t *ds3b, daos_event_t *ev);

/**
 * Write S3 object data.
 *
 * \param[in]	buf	Data to write.
 * \param[in]	off	Offset into the file to write to.
 * \param[in,out]
 *		size	[in]: Size of buffer passed in.
 * 				[out]: Actual size of data written.
 * \param[in]	ds3o	S3 object handle to read from.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_obj_write(const void *buf, daos_off_t off, daos_size_t *size, ds3_obj_t *ds3o,
	      daos_event_t *ev);

/**
 * Mark an S3 object in the S3 bucket identified by \a ds3b as being the latest version.
 *
 * \param[in]	key	Key of the S3 object to mark as latest.
 * \param[in]	ds3b	ds3b	Pointer to the S3 bucket handle to use.
 *
 * \return              0 on success, -errno code on failure.
 */
int
ds3_obj_mark_latest(const char *key, ds3_bucket_t *ds3b);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_S3_H__ */
