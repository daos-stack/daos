/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DFUSE_VECTOR_H__
#define __DFUSE_VECTOR_H__

#include <daos_errno.h>

/* An opaque 64-bit structure allocated by the user */
typedef struct {
	char pad[256];
} vector_t;

typedef void (*vector_destroy_cb)(void *arg);

/* Initialize a vector of fixed sized entries.
 * \param vector[in, out] The vector to initialize
 * \param sizeof_entry[in] size of each entry.
 * \param max_entries[in] The maximum number of entries in the vector,
 *                        0 for no maximum
 * \retval -DER_SUCCESS on success
 */
int vector_init(vector_t *vector, int sizeof_entry, int max_entries,
		vector_destroy_cb destroy_cb);

/* Destroy a vector
 * \param vector[in] The vector to destroy
 * \return 0 on success
 */
int vector_destroy(vector_t *vector);

/* Get an existing vector entry from the table
 * Increments the reference count on the entry
 * \param vector[in] The vector
 * \param index[in] The index of the entry to get
 * \param entry[out] Pointer to the object
 * \retval -DER_SUCCESS Entry was retrieved
 * \retval -DER_INVAL Bad arguments
 * \retval -DER_NOMEM Out of memory
 * \retval -DER_UNINIT Vector not initialized
 * \retval -DER_NONEXIST No entry at index
 */
#define vector_get(vector, index, entrypp) \
	vector_get_(vector, index, (void **)(entrypp))
int vector_get_(vector_t *vector, unsigned int index, void **entry);

/* Duplicate a vector entry.  If src_idx isn't empty, dst_idx will
 * reference the same entry.
 * Increments the reference count on the entry.
 * If dst_idx exists, it is removed.
 * \param vector[in] The vector
 * \param src_idx[in] Source index in vector
 * \param dst_idx[in] Destination index in vector
 * \param entry[out] Pointer to the object
 * \retval -DER_SUCCESS Entry was duplicated
 * \retval -DER_INVAL Bad arguments
 * \retval -DER_NOMEM Out of memory
 * \retval -DER_UNINIT Vector not initialized
 * \retval -DER_NONEXIST No entry at src_idx
 */
#define vector_dup(vector, dst_idx, src_idx, entrypp) \
	vector_dup_(vector, dst_idx, src_idx, (void **)(entrypp))
int vector_dup_(vector_t *vector, unsigned int src_idx, unsigned int dst_idx,
		void **entry);

/* Decrement the reference count on an entry.  Delete the entry if
 * refcount is 0
 * Should be called once for each 'get' routine that returns a valid entry
 * \param vector[in] The vector
 * \param index[in] The index of the entry to set
 * \param typep[in] A pointer data to put into table
 * \retval -DER_SUCCESS on success.
 * \retval -DER_SUCCESS refcount decremented
 * \retval -DER_INVAL Bad arguments
 * \retval -DER_UNINIT Vector not initialized
 */
int vector_decref(vector_t *vector, void *entry);

/* Allocate and initialize an entry
 * If index exists, it is removed.
 * \param vector[in] The vector
 * \param index[in] The index of the entry to allocate
 * \param entry[in] Pointer to the object
 * \retval -DER_SUCCESS Entry was allocated
 * \retval -DER_INVAL Bad arguments
 * \retval -DER_NOMEM Out of memory
 * \retval -DER_UNINIT Vector not initialized
 */
#define vector_set(vector, index, entryp) \
	vector_set_(vector, index, entryp, sizeof(*entryp))

/* Allocate and initialize an entry.  Normally, the vector_set macro
 * will suffice.  However, if the size isn't determinable from the
 * pointer type, use this function instead.
 * If index exists, it is removed.
 * \param vector[in] The vector
 * \param index[in] The index of the entry to allocate
 * \param entry[in] Pointer to the object
 * \param size[in] size of object
 * \retval -DER_SUCCESS Entry was allocated and initialized
 * \retval -DER_INVAL Bad arguments
 * \retval -DER_NOMEM Out of memory
 * \retval -DER_UNINIT Vector not initialized
 */
int vector_set_(vector_t *vector, unsigned int index, void *entry, size_t size);

/* Remove an entry from the vector.  If entrypp is not NULL and entry
 * was present, a reference is taken on the entry.
 * \param vector[in] The vector
 * \param index[in] The index of the item to remove
 * \param entrypp[out] Optionally, return a pointer to the entry
 * \retval -DER_SUCCESS Entry was removed
 * \retval -DER_INVAL Bad arguments
 * \retval -DER_UNINIT Vector not initialized
 * \retval -DER_NONEXIST No entry at index
 */
#define vector_remove(vector, index, entrypp) \
	vector_remove_(vector, index, (void **)entrypp)
int vector_remove_(vector_t *vector, unsigned int index, void **entry);

#endif /*  __DFUSE_VECTOR_H__ */
