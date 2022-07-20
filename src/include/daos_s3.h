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

/** DAOS S3 Pool handle */
typedef struct ds3        ds3_t;

/** DAOS S3 Bucket handle */
typedef struct ds3_bucket ds3_bucket_t;

/** DAOS S3 Object handle */
typedef struct ds3_obj    ds3_obj_t;

/** S3 User information */
struct ds3_user_info {
};

/** S3 Bucket information */
struct ds3_bucket_info {
	/** Bucket name */
	char name[DAOS_PROP_LABEL_MAX_LEN + 1];
};

struct ds3_object_list_params {
	char prefix[128];
	char delim[128];
	char marker[128];
	bool list_versions;
	bool allow_unordered;
	/** If the reults are truncated */
	bool is_truncated;
	char next_marker[128]; // TODO adjust length
			       // TODO fill
};

/** S3 Object information */
struct ds3_object_info {
	/** Object key */
	char key[128]; // TODO adjust length
		       // TODO fill
};

// General S3

/**
 * Initialize all the relevant DAOS libraries.
 *
 * \return              0 on success, errno code on failure.
 */
int
ds3_init();

/**
 * Finalize the relevant DAOS libraries if necessary.
 *
 * \return              0 on success, errno code on failure.
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
 * \return              0 on success, errno code on failure.
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
 * \return              0 on success, errno code on failure.
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
 * \return              0 on success, errno code on failure.
 */
int
ds3_user_set(const char *name, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev);

/**
 * Remove user from S3 user database.
 *
 * \param[in]	name	Name of the S3 user to look up.
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, errno code on failure.
 */
int
ds3_user_remove(const char *name, ds3_t *ds3, daos_event_t *ev);

/**
 * Look-up S3 user information by name.
 *
 * \param[in]	name	Name of the S3 user to look up.
 * \param[in]	info	User info.
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, errno code on failure.
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
 * \return              0 on success, errno code on failure.
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
 * \return              0 on success, errno code on failure.
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
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, errno code on failure.
 */
int
ds3_bucket_list(daos_size_t *nbuck, struct ds3_bucket_info *buf, ds3_t *ds3, daos_event_t *ev);

/**
 * Create a bucket in the DAOS pool identified by \a ds3.
 * Optionally set attributes for hints on the container.
 *
 * \param[in]	name	Bucket name. Must be unique in the pool.
 * \param[in]	info	Bucket info to be added to the bucket.
 * \param[in]	props	Optional set of properties to set on the container. Pass NULL if none.
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, errno code on failure.
 */
int
ds3_bucket_create(const char *name, struct ds3_bucket_info *info, daos_prop_t *props, ds3_t *ds3,
		  daos_event_t *ev);

/**
 * Destroy a bucket in the DAOS pool identified by \a ds3.
 *
 * \param[in]	name	Name of the bucket to destroy.
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, errno code on failure.
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
 * \return              0 on success, errno code on failure.
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
 * \return              0 on success, errno code on failure.
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
 * \return              0 on success, errno code on failure.
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
 * \return              0 on success, errno code on failure.
 */
int
ds3_bucket_set_info(struct ds3_bucket_info *info, ds3_bucket_t *ds3b, daos_event_t *ev);

/**
 * List S3 objects stored in the S3 bucket identified by \a ds3b.
 *
 * \param[in,out]
 *		nobj	[in] \a buf length in items.
 *			[out] Number of objects returned.
 * \param[out]	buf	Array of object info structures.
 * \param[in,out]
 * 		params	[in] Params for the list operation
 *			[out] Results from the list operation
 * \param[in]	ds3b	Pointer to the S3 bucket handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, errno code on failure.
 */
int
ds3_bucket_list_obj(daos_size_t *nobj, struct ds3_object_info *buf,
		    struct ds3_object_list_params *params, ds3_bucket_t *ds3b, daos_event_t *ev);

// TODO multipart api

// S3 Objects

/**
 * Create \s S3 Object in the S3 bucket identified by \a ds3b.
 *
 * \param[in]	key	Key of the S3 object to destroy.
 * \param[in]	info	S3 object info.
 * \param[in]	ds3b	ds3b	Pointer to the S3 bucket handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, errno code on failure.
 */
int
ds3_obj_create(const char *key, struct ds3_object_info *info, ds3_bucket_t *ds3b, daos_event_t *ev);

/**
 * Open \a S3 object.
 *
 * \param[in]	key	Key of the object to open.
 * \param[out]	ds3o	Returned S3 object handle.
 * \param[in]	ds3b	Pointer to the S3 bucket handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, errno code on failure.
 */
int
ds3_obj_open(const char *key, ds3_obj_t **ds3o, ds3_bucket_t *ds3b, daos_event_t *ev);

/**
 * Close an object handle.
 *
 * \param[in]	ds3o	S3 object handle to close.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, errno code on failure.
 */
int
ds3_obj_close(ds3_obj_t *ds3o, daos_event_t *ev);

/**
 * Get S3 object info.
 *
 * \param[out]	info	Returned S3 object info.
 * \param[in]	ds3o	Pointer to the S3 object handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, errno code on failure.
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
 * \return              0 on success, errno code on failure.
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
 * \return              0 on success, errno code on failure.
 */
int
ds3_obj_read(void *buf, daos_off_t off, daos_size_t *size, ds3_obj_t *ds3o, daos_event_t *ev);

/**
 * Destroy \a S3 object in the S3 bucket identified by \a ds3b.
 *
 * \param[in]	key	Key of the S3 object to destroy.
 * \param[in]	ds3b	ds3b	Pointer to the S3 bucket handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              0 on success, errno code on failure.
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
 * \return              0 on success, errno code on failure.
 */
int
ds3_obj_write(const void *buf, daos_off_t off, daos_size_t *size, ds3_obj_t *ds3o,
	      daos_event_t *ev);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_S3_H__ */
