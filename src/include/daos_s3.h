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
#include <daos_fs.h>

/** Maximum bucket name length */
#define DS3_MAX_BUCKET_NAME DAOS_PROP_MAX_LABEL_BUF_LEN

/** Maximum key length, allows adding [latest] */
#define DS3_MAX_KEY         (DFS_MAX_PATH - 8)

/** Maximum key buffer length */
#define DS3_MAX_KEY_BUFF    DFS_MAX_PATH

/** Maximum user info length */
#define DS3_MAX_USER_NAME   DFS_MAX_NAME

/** Maximum upload_id length */
#define DS3_MAX_UPLOAD_ID   35

/** Maximum encoded length */
#define DS3_MAX_ENCODED_LEN DFS_MAX_XATTR_LEN

/** Latest instance */
#define DS3_LATEST_INSTANCE "latest"

/** DAOS S3 Pool handle */
typedef struct ds3        ds3_t;

/** DAOS S3 Bucket handle */
typedef struct ds3_bucket ds3_bucket_t;

/** DAOS S3 Object handle */
typedef struct ds3_obj    ds3_obj_t;

/** DAOS S3 Upload Part handle */
typedef struct ds3_part   ds3_part_t;

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
	char   key[DS3_MAX_KEY_BUFF];
	/** Opaque encoded object info */
	void  *encoded;
	/** Length of encoded data */
	size_t encoded_length;
};

/** S3 Common Prefix information */
struct ds3_common_prefix_info {
	/** Common Prefix */
	char prefix[DS3_MAX_KEY_BUFF];
};

/** S3 Multipart Upload information */
struct ds3_multipart_upload_info {
	/** Upload id */
	char   upload_id[DS3_MAX_UPLOAD_ID];
	/** Object key */
	char   key[DS3_MAX_KEY_BUFF];
	/** Opaque encoded upload info */
	void  *encoded;
	/** Length of encoded data */
	size_t encoded_length;
};

/** S3 Multipart part information */
struct ds3_multipart_part_info {
	/** Part number */
	uint64_t part_num;
	/** Opaque encoded part info */
	void    *encoded;
	/** Length of encoded data */
	size_t   encoded_length;
};

/* General S3 */

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
 * \return		0 on success, -errno code on failure.
 */
int
ds3_disconnect(ds3_t *ds3, daos_event_t *ev);

/* S3 users */

/**
 * Add/update user information in the S3 user database.
 *
 * \param[in]	name		Name of the S3 user to look up.
 * \param[in]	info		User info.
 * \param[in]	old_info	(Optional) Old user info.
 * \param[in]	ds3		Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev		Completion event, it is optional and can be NULL.
 *				Function will run in blocking mode if \a ev is NULL.
 *
 * \return			0 on success, -errno code on failure.
 */
int
ds3_user_set(const char *name, struct ds3_user_info *info, struct ds3_user_info *old_info,
	     ds3_t *ds3, daos_event_t *ev);

/**
 * Remove user from S3 user database.
 *
 * \param[in]	name	Name of the S3 user to look up.
 * \param[in]	info	User info. Necessary to remove symlinks
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on success, -errno code on failure.
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
 * \return		0 on success, -errno code on failure.
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
 * \return		0 on success, -errno code on failure.
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
 * \return		0 on success, -errno code on failure.
 */
int
ds3_user_get_by_key(const char *key, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev);

/* S3 Buckets */

/**
 * List buckets stored in the DAOS pool identified by \a ds3.
 *
 * \param[in,out]
 *		nbuck	[in]:	\a buf length in items.
 *			[out]:	Number of buckets returned.
 * \param[out]	buf		Array of bucket info structures.
 * \param[in,out]
 *		marker	[in]:	Start listing from marker key
 *			[out]:	Returned marker key for next call
 * \param[out]	is_truncated	Are the results truncated
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on success, -errno code on failure.
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
 * \return		0 on success, -errno code on failure.
 */
int
ds3_bucket_create(const char *name, struct ds3_bucket_info *info, dfs_attr_t *attr, ds3_t *ds3,
		  daos_event_t *ev);

/**
 * Destroy a bucket in the DAOS pool identified by \a ds3.
 *
 * \param[in]	name	Name of the bucket to destroy.
 * \param[in]	force	If true, remove bucket even if non-empty.
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on success, -errno code on failure.
 */
int
ds3_bucket_destroy(const char *name, bool force, ds3_t *ds3, daos_event_t *ev);

/**
 * Open an S3 bucket identified by \a name.
 *
 * \param[in]	name	Name of the bucket to open.
 * \param[out]	ds3b	Returned S3 bucket handle.
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on success, -errno code on failure.
 */
int
ds3_bucket_open(const char *name, ds3_bucket_t **ds3b, ds3_t *ds3, daos_event_t *ev);

/**
 * Close an S3 bucket handle.
 *
 * \param[in]	ds3b	S3 bucket handle to close.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on success, -errno code on failure.
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
 * \return		0 on success, -errno code on failure.
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
 * \return		0 on success, -errno code on failure.
 */
int
ds3_bucket_set_info(struct ds3_bucket_info *info, ds3_bucket_t *ds3b, daos_event_t *ev);

/**
 * List S3 objects stored in the S3 bucket identified by \a ds3b.
 *
 * \param[in,out]
 *		nobj	[in]:	\a objs length in items.
 *			[out]:	Number of objects returned.
 * \param[out]	objs		Array of object info structures.
 * \param[in,out]
 *		ncp	[in]:	\a cps length in items.
 *			[out]:	Number of common prefixes returned.
 * \param[out]	cps		Array of common prefix info structures.
 * \param[in]	prefix		(Optional) List objects that start with this prefix.
 * \param[in]	delim		(Optional) Divide results by delim.
 * \param[in,out]
 *		marker	[in]:	Start listing from marker key.
 *			[out]:	Next marker to be used by subsequent calls.
 * \param[in]	list_versions	Also include versions
 * \param[out]	is_truncated	Are the results truncated
 * \param[in]	ds3b		Pointer to the S3 bucket handle to use.
 *
 * \return			0 on success, -errno code on failure.
 */
int
ds3_bucket_list_obj(uint32_t *nobj, struct ds3_object_info *objs, uint32_t *ncp,
		    struct ds3_common_prefix_info *cps, const char *prefix, const char *delim,
		    char *marker, bool list_versions, bool *is_truncated, ds3_bucket_t *ds3b);

/* S3 Objects */

/**
 * Create an S3 Object in the S3 bucket identified by \a ds3b.
 *
 * \param[in]	key	Key of the S3 object to destroy.
 * \param[out]	ds3o	Returned S3 object handle.
 * \param[in]	ds3b	Pointer to the S3 bucket handle to use.
 *
 * \return		0 on success, -errno code on failure.
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
 * \return		0 on success, -errno code on failure.
 */
int
ds3_obj_open(const char *key, ds3_obj_t **ds3o, ds3_bucket_t *ds3b);

/**
 * Close an object handle.
 *
 * \param[in]	ds3o	S3 object handle to close.
 *
 * \return		0 on success, -errno code on failure.
 */
int
ds3_obj_close(ds3_obj_t *ds3o);

/**
 * Get S3 object info.
 *
 * \param[out]	info	Returned S3 object info.
 * \param[in]	ds3b	Pointer to the S3 bucket handle to use.
 * \param[in]	ds3o	Pointer to the S3 object handle to use.
 *
 * \return		0 on success, -errno code on failure.
 */
int
ds3_obj_get_info(struct ds3_object_info *info, ds3_bucket_t *ds3b, ds3_obj_t *ds3o);

/**
 * Set S3 object info.
 *
 * \param[in]	info	S3 object info.
 * \param[in]	ds3b	Pointer to the S3 bucket handle to use.
 * \param[in]	ds3o	Pointer to the S3 object handle to use.
 *
 * \return		0 on success, -errno code on failure.
 */
int
ds3_obj_set_info(struct ds3_object_info *info, ds3_bucket_t *ds3b, ds3_obj_t *ds3o);

/**
 * Read S3 object data.
 *
 * \param[in,out]
 *		buf	[in]:	Allocated buffer for data.
 *			[out]:	Actual data read.
 * \param[in]	off		Offset into the file to read from.
 * \param[in,out]
 *		size	[in]:	Size of buffer passed in.
 *			[out]:	Actual size of data read.
 * \param[in]	ds3b	ds3b	Pointer to the S3 bucket handle to use.
 * \param[in]	ds3o	S3 object handle to read from.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on success, -errno code on failure.
 */
int
ds3_obj_read(void *buf, daos_off_t off, daos_size_t *size, ds3_bucket_t *ds3b, ds3_obj_t *ds3o,
	     daos_event_t *ev);

/**
 * Destroy an S3 object in the S3 bucket identified by \a ds3b.
 *
 * \param[in]	key	Key of the S3 object to destroy.
 * \param[in]	ds3b	Pointer to the S3 bucket handle to use.
 *
 * \return		0 on success, -errno code on failure.
 */
int
ds3_obj_destroy(const char *key, ds3_bucket_t *ds3b);

/**
 * Write S3 object data.
 *
 * \param[in]	buf	Data to write.
 * \param[in]	off	Offset into the file to write to.
 * \param[in,out]
 *		size	[in]:	Size of buffer passed in.
 *			[out]:	Actual size of data written.
 * \param[in]	ds3b	Pointer to the S3 bucket handle to use.
 * \param[in]	ds3o	S3 object handle to read from.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on success, -errno code on failure.
 */
int
ds3_obj_write(void *buf, daos_off_t off, daos_size_t *size, ds3_bucket_t *ds3b, ds3_obj_t *ds3o,
	      daos_event_t *ev);

/**
 * Mark an S3 object in the S3 bucket identified by \a ds3b as being the latest version.
 *
 * \param[in]	key	Key of the S3 object to mark as latest.
 * \param[in]	ds3b	Pointer to the S3 bucket handle to use.
 *
 * \return		0 on success, -errno code on failure.
 */
int
ds3_obj_mark_latest(const char *key, ds3_bucket_t *ds3b);

/* S3 Multipart api */

/**
 * List S3 multipart uploads pending in the S3 bucket identified by \a bucket_name.
 *
 * \param[in]	bucket_name	Name of the bucket.
 * \param[in,out]
 *		nmp	[in]:	\a mps length in items.
 *			[out]:	Number of multipart uploads returned.
 * \param[out]	mps		Array of object info structures.
 * \param[in,out]
 *		ncp	[in]:	\a cps length in items.
 *			[out]:	Number of common prefixes returned.
 * \param[out]	cps		Array of common prefix info structures.
 * \param[in]	prefix		(Optional) List multipart uploads that start with this
 *				prefix.
 * \param[in]	delim		(Optional) Divide results by delim.
 * \param[in,out]
 *		marker	[in]:	Start listing from marker key.
 *			[out]:	Next marker to be used by subsequent calls.
 * \param[out]	is_truncated	Are the results truncated
 * \param[in]	ds3		Pointer to the DAOS S3 pool handle to use.
 *
 * \return			0 on success, -errno code on failure.
 */
int
ds3_bucket_list_multipart(const char *bucket_name, uint32_t *nmp,
			  struct ds3_multipart_upload_info *mps, uint32_t *ncp,
			  struct ds3_common_prefix_info *cps, const char *prefix, const char *delim,
			  char *marker, bool *is_truncated, ds3_t *ds3);

/**
 * List S3 multipart uploaded parts related to \a upload_id in the bucket identified by \a
 * bucket_name
 *
 * \param[in]	bucket_name	Name of the bucket.
 * \param[in]	upload_id	ID of the upload.
 * \param[in,out]
 *		npart	[in]:	\a parts length in items.
 *			[out]:	Number of parts returned.
 * \param[out]	parts		Array of multipart part info structures.
 * \param[in,out]
 *		marker	[in]:	Start listing from marker key.
 *			[out]:	Next marker to be used by subsequent calls.
 * \param[out]	is_truncated	Are the results truncated
 * \param[in]	ds3		Pointer to the DAOS S3 pool handle to use.
 *
 * \return			0 on success, -errno code on failure.
 */
int
ds3_upload_list_parts(const char *bucket_name, const char *upload_id, uint32_t *npart,
		      struct ds3_multipart_part_info *parts, uint32_t *marker, bool *is_truncated,
		      ds3_t *ds3);

/**
 * Init an S3 upload in the S3 bucket identified by \a bucket_name
 *
 * \param[in]	info		S3 upload info.
 * \param[in]	bucket_name	Name of the bucket.
 * \param[in]	ds3		Pointer to the DAOS S3 pool handle to use.
 *
 * \return			0 on success, -errno code on failure.
 */
int
ds3_upload_init(struct ds3_multipart_upload_info *info, const char *bucket_name, ds3_t *ds3);

/**
 * Remove the S3 multipart upload identified by \a upload_id in the bucket identified by \a
 * bucket_name
 *
 * \param[in]	bucket_name	Name of the bucket.
 * \param[in]	upload_id	ID of the upload.
 * \param[in]	ds3		Pointer to the DAOS S3 pool handle to use.
 *
 * \return			0 on success, -errno code on failure.
 */
int
ds3_upload_remove(const char *bucket_name, const char *upload_id, ds3_t *ds3);

/**
 * Gwt S3 multipart upload info identified by \a upload_id in the bucket identified by \a
 * bucket_name
 *
 * \param[in]	info		S3 upload info.
 * \param[in]	bucket_name	Name of the bucket.
 * \param[in]	upload_id	ID of the upload.
 * \param[in]	ds3		Pointer to the DAOS S3 pool handle to use.
 *
 * \return			0 on success, -errno code on failure.
 */
int
ds3_upload_get_info(struct ds3_multipart_upload_info *info, const char *bucket_name,
		    const char *upload_id, ds3_t *ds3);

/**
 * Open an S3 multipart part identified by \a part_num.
 *
 * \param[in]	bucket_name	Name of the bucket.
 * \param[in]	upload_id	ID of the upload.
 * \param[in]	part_num	The part number.
 * \param[in]	truncate	whether to truncate the part object.
 * \param[out]	ds3p		Returned S3 object handle.
 * \param[in]	ds3		Pointer to the DAOS S3 pool handle to use.
 *
 * \return			0 on success, -errno code on failure.
 */
int
ds3_part_open(const char *bucket_name, const char *upload_id, uint64_t part_num, bool truncate,
	      ds3_part_t **ds3p, ds3_t *ds3);

/**
 * Close a part handle.
 *
 * \param[in]	ds3p	S3 part handle to close.
 *
 * \return		0 on success, -errno code on failure.
 */
int
ds3_part_close(ds3_part_t *ds3p);

/**
 * Write S3 part data.
 *
 * \param[in]	buf	Data to write.
 * \param[in]	off	Offset into the file to write to.
 * \param[in,out]
 *		size	[in]:	Size of buffer passed in.
 *			[out]:	Actual size of data written.
 * \param[in]	ds3p	S3 part handle to read from.
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on success, -errno code on failure.
 */
int
ds3_part_write(void *buf, daos_off_t off, daos_size_t *size, ds3_part_t *ds3p, ds3_t *ds3,
	       daos_event_t *ev);

/**
 * Read S3 part data.
 *
 * \param[in]	buf	Data to read.
 * \param[in]	off	Offset into the file to read from.
 * \param[in,out]
 *		size	[in]:	Size of buffer passed in.
 *			[out]:	Actual size of data read.
 * \param[in]	ds3p	S3 part handle to read from.
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on success, -errno code on failure.
 */
int
ds3_part_read(void *buf, daos_off_t off, daos_size_t *size, ds3_part_t *ds3p, ds3_t *ds3,
	      daos_event_t *ev);

/**
 * Set S3 part info.
 *
 * \param[in]	info	S3 multipart upload part info.
 * \param[in]	ds3p	Pointer to the S3 part handle to use.
 * \param[in]	ds3	Pointer to the DAOS S3 pool handle to use.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on success, -errno code on failure.
 */
int
ds3_part_set_info(struct ds3_multipart_part_info *info, ds3_part_t *ds3p, ds3_t *ds3,
		  daos_event_t *ev);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_S3_H__ */
