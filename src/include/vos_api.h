/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2015 Intel Corporation.
 */
/**
 * This file describes the API for a versioning object store.
 * These APIs will help build a versioned store with
 * key-value and byte-array object types.
 * These APIs provide ways to create, delete, search and enumerate
 * multiversion concurrent key-value and byte-array objects.
 *
 * Author :  Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */

#ifndef __VOS_API_H
#define __VOS_API_H

#include <daos_types.h>

/**
 * Versioning Object Storage Pool (VOSP)
 * An OSP creates and manages a versioned object store on a local
 * storage device. The capacity of an OSP is determined
 * by the capacity of the underlying storage device
 */

/**
 * initialize a Versioning object storage pool (VOSP)
 * This API would open and initialize an VOSP. While initializing a
 * root object will be created. If the VOSP has been initialized this call
 * will open the pool. Path to this memory pool is passed to the versioning
 * object store.
 *
 * \param path  [IN]    Path of the memory pool
 * \param uuid  [IN]    parent UUID
 * \param pool [OUT]    OSP handle
 *
 * \return              zero on success, negative value if error
 *
 */
int
vos_pool_init(const char *path, uuid_t uuid, daos_handle_t *pool);

/**
 * Close a Versioned Object Storage Pool (VOSP)
 * This API deletes any temporary references created
 * and closes the pool
 *
 * \param pool [IN]    pool  handle
 *
 *
 * \return             zero on success, negative value if error
 *
 */
int
vos_pool_finalize(daos_handle_t pool);

/**
 * Flush changes until epoch(version)
 * This function will wait for pending changes of epochs smaller or
 * equal to \a epoch to be flushed to storage.
 *
 * \param pool  [IN]   pool handle
 * \param epoch [IN]   epoch
 *
 * \return             zero on success, negative value if error
 *
 */
int
vos_epoch_flush(daos_handle_t pool, daos_epoch_t epoch);

/**
 * Aggregates all epochs specified in the range between
 * start and end epochs. All epochs which get get aggregated
 * will be discarded. All epochs will be aggregated to
 * the end epoch specified
 *
 * \param pool     [IN]   pool handle
 * \param epochs   [IN]   List of epoch(s) to be aggregated
 * \param n_epochs [IN]   number of epochs in list
 *
 * \return                zero on success, negative value if error
 *
 */
int
vos_epoch_aggregate(daos_handle_t pool, daos_epoch_t start_epoch,
		    daos_epoch_t end_epoch);

/**
 * Discards changes in current and all epochs specified in the specified range
 * with start and end
 *
 *
 * \param pool [IN]      pool handle
 * \param epoch [IN]     epoch(s) to be discarded
 * \param n_epochs [IN]  number of epochs in list
 *
 * \return               zero on success, negative value if error
 *
 */
int vos_epoch_discard(daos_handle_t pool, daos_epoch_t start_epoch,
		      daos_epoch_t end_epoch);

typedef struct {
	vs_size_t	ba_objects;	/* Number of ba objects in this epoch */
	vs_size_t	kv_objects;	/* Number of kv objects in this epoch */
	vs_epoch_t	highest_epoch;	/* Highest epoch in this pool */
	vs_epoch_t	lowest_epoch;	/* Lowest epoch in this pool */
	vs_size_t	maxbytes;	/* Total space available */
	vs_size_t	savail;		/* Current available space */
} vs_stat_t;

/**
 * Get statistics about the current pool
 *
 * \param pool    [IN]   pool handle
 * \param stat_obj [OUT] pool stats object
 *
 * \return               zero on success, negative value if error
 *
 */
int
vos_pool_stat(daos_handle_t pool, vs_stat_t *stat_obj);

/**
 * Byte Array API
 */

/**
 * Write to byte arrays for a given object ID and a given epoch
 * Writes happen in two parts, in begin, the required descriptors are
 * created for the write operation and in the second part the
 * value is updated directly in persistent memory
 *
 *  Write begin
 * \param pool      [IN]  pool handle
 * \param object_id [IN]  object ID
 * \param epoch     [IN]  epoch for write
 * \param extents   [IN]  object extents
 * \param desc      [OUT] descriptor with location to write
 *
 * \return                 zero on success, negative value if error
 */
int
vos_ba_write_begin(daos_handle_t pool, daos_obj_id_t object_id,
		   daos_epoch_t epoch, daos_ext_list_t *extents,
		   daos_sg_list_t **desc);

/**
 * Write end
 * \param pool       [IN]   pool handle
 * \param object_id  [IN]   object ID
 * \param epoch      [IN]   epoch to write at
 * \param desc       [IN]   descriptor with location to write
 * \param write_array[IN]   memory buffer with data to be written
 * \param checksum   [IN]   cheksum of the dats to be written
 *
 * \return                   zero on success, negative value if error
 */
int
vos_ba_write_end(daos_handle_t pool, daos_obj_id_t object_id,
		 daos_epoch_t epoch, daos_sg_list_t *desc,
		 daos_sg_list_t *write_array, daos_sg_list_t *checksum);

/**
 * Punch will zero specified offsets and mark the object for
 * deletion.
 *
 * \param pool       [IN]   pool handle
 * \param object_id  [IN]   object ID
 * \param epoch      [IN]   epoch to write at
 * \param extents    [IN]   extents to punch
 *
 * \return                   zero on success, negative value if error
 */
int
vos_ba_punch(daos_handle_t pool, daos_obj_id_t object_id,
	     daos_epoch_t epoch, daos_ext_list_t *extents);

/**
 * Read from byte arrays for a given object ID and a given epoch
 * Similar to writes reads also happen in two parts, in begin,
 * the required descriptors are created with the location of data
 * and copies the data to user buffer, andin the second
 * it would clean up the internal references that were created for the
 * read operation. [NOTE: Read may change to one operation instead of
 * split depending on the implementation.]
 *
 *  Read begin
 * \param pool       [IN]   pool handle
 * \param object_id  [IN]   object ID
 * \param epoch      [IN]   epoch to read from
 * \param extents    [IN]   object extents to read from
 * \param read_array [IN]   memory buffer for data to be read
 * \param holes      [OUT]  holes for the given extent range.
 *
 * \return                  zero on success, negative value if error
 *                          positive value (when the current buffer
 *                          contains only partial output, returns
 *                          the number of entries to be allocated
 *                          to obtain remaining)
 */
int
vos_ba_read_begin(daos_handle_t pool, daos_obj_id_t object_id,
		  daos_epoch_t epoch, daos_ext_list_t *desc,
		  daos_sg_list_t *read_array, daos_ext_list_t *holes);

/**
 * Read end
 * \param pool       [IN]   pool handle
 * \param object_id  [IN]   object ID
 * \param epoch      [IN]   epoch to read from
 * \param desc       [IN]   descriptor with addr, off info to read
 * \param read_array [OUT]  memory buffer for data to be read
 * \param checksum   [OUT]  checksum of the value read
 *
 * \return                  zero on success, negative value if error
 */
int
vos_ba_read_end(daos_handle_t pool, daos_obj_id_t object_id,
		daos_epoch_t epoch,  daos_ext_list_t *desc,
		daos_sg_list_t *read_array, daos_sg_list_t *checksum);

/**
 * Find holes in Byte Array Extents
 *
 * Checks for  byte arrays requested and returns the list of offset,len
 * pairs for those which were holes
 *
 * \param pool       [IN]   pool handle
 * \param object_id  [IN]   object ID
 * \param epoch      [IN]   epoch
 * \param rd_desc    [IN]   Extents to look for holes
 * \param holes      [IN]   Returns extents which are holes
 *                          found during read of extents
 *
 * \return                  zero on success, negative value if error
 *                          positive value (when the current buffer
 *                          contains only partial output, returns
 *                          the number of entries to be allocated
 *                          to obtain remaining)

 */
int
vos_ba_find(daos_handle_t pool, daos_obj_id_t object_id,
	    daos_epoch_t epoch, daos_ext_list_t *rd_desc,
	    daos_ext_list_t *holes);


/**
 * Enumerating non-empty byte-array objects in a pool
 *
 * Enumerate all objects in a given epoch from a given offset
 * User specifies a required number of objects and is returned
 * a list of objectIDs. If the objectID count does not match the
 * one returned by the user then -1 is returned
 *
 * \param pool       [IN]      pool handle
 * \param epoch      [IN]      epoch
 * \param anchor     [IN/OUT]  anchor for the next objectID
                               it will be -1 there are no more objects
                               points to the location of ref for listing
                               objects
 * \param id_cnt      [OUT]    size of the object ID list
 * \param id_list     [OUT]    Object ID list
 */
int
vos_list_ba_objects(daos_handle_t pool, daos_epoch_t epoch,
		    daos_obj_id_t anchor, daos_size_t id_cnt,
		    daos_obj_id_t *id_list );

/**
 * KV API
 */

/**
 * Key Value Update/insert
 * In KVs keys and values of any length are allowed to be inserted.
 * To support small keys and values the insert can happen
 * inline. For large keys and values the insert needs to happen in
 * two steps similar to that of byte arrays.
 *
 *
 * Key value update (small keys and values)
 * Used for inline updates
 *
 * \param pool       [IN]   pool handle
 * \param object_id  [IN]   object ID
 * \param epoch      [IN]   epoch where the update happens
 * \param entry      [IN]   key-value entry
 *                          (key size, value size and actual data)
 *
 * \return                  zero on success, negative value if error
 */
int
vos_kv_update(daos_handle_t pool, daos_obj_id_t object_id,
	      daos_epoch_t epoch, daos_kv_t entry, daos_kv_t checksum);

/**
 * Key value update (large keys and values)
 * Used for large keys, values. Has an additional descriptor where
 * initial set of indexes are storage location for the key and the
 * latter the location for values
 *
 * Key Value Update begin
 *
 * \param pool        [IN]   pool handle
 * \param object_id   [IN]   object ID
 * \Paramx epoch      [IN]   epoch where the update happens
 * \param entry       [IN]   key-value entry
 *                           (key size, value size and actual data)
 * \param desc        [OUT]  location for writing keys and values
 *
 * \return                   zero on success, negative value if error
 */
int
vos_kv_update_begin(daos_handle_t pool, daos_obj_id_t object_id,
		    daos_epoch_t epoch, daos_kv_t entry, daos_sg_list_t **desc);

/**
 * Key Value Update end
 *
 * \param pool       [IN]   pool handle
 * \param object_id  [IN]   object ID
 * \param epoch      [IN]   epoch where the update happens
 * \param entry      [IN]   key-value entry
 *                          (key size, value size and actual data)
 * \param checksum   [IN]   Checksum value of both key and value
 * \param desc       [IN]   location of keys and values in PM
 *
 * \return                   zero on success, negative value if error
 */
int
vos_kv_update_end(daos_handle_t pool, daos_obj_id_t object_id,
		  daos_epoch_t epoch, daos_kv_t entry, daos_kv_t checksum,
		  daos_ext_list_t *desc);

/**
 * Key Value lookup
 *
 * Lets to lookup values for a given key. Returns the key-value entry
 * If not found returns NULL in the key-value result and a negative
 * return value
 *
 * \param pool       [IN]   pool handle
 * \param object_id  [IN]   object ID
 * \param key        [IN]   key to lookup
 * \param epoch      [IN]   epoch
 * \param entry      [OUT]  kv_entry value on success, NULL on fail
 * \param checksum   [OUT]  checksum of the key-value on lookup
 * \return                   zero on success, -1 if key does not exist (hole)
 *
 */
int
vos_kv_lookup(daos_handle_t pool, daos_obj_id_t object_id,
	      daos_epoch_t epoch, daos_sg_list_t *key,
	      daos_kv_t *entry, daos_kv_t *checksum);

/**
 * Key Value Delete
 * Locates and deletes a particular key-value pair.
 *
 * \param pool       [IN]   pool handle
 * \param object_id  [IN]   object ID
 * \param key        [IN]   key to lookup
 * \param epoch      [IN]   epoch
 *
 *
 * \return                   zero on success, negative value if error

 */
int
vos_kv_delete(daos_handle_t pool, daos_obj_id_t object_id, daos_epoch_t epoch,
	      daos_sg_list_t *key);

/**
 * Find holes in Key Value Store
 *
 * Does lookup for a list of keys and then returns 0 if the key, value
 * exist, else returns -1 for the same query
 *
 * \param pool       [IN]   pool handle
 * \param object_id  [IN]   object ID
 * \param epoch      [IN]   epoch
 * \param keys       [IN]   keys to lookup
 * \param holes      [OUT]  Returns if there are holes found during
 *                          lookup
 *
 * \return                  zero on success, negative value if error
 *                          positive value (when the current buffer
 *                          contains only partial output, returns
 *                          the number of entries to be allocated
 *                          to obtain remaining)
 */
int
vos_kv_find(daos_handle_t pool, daos_obj_id_t object_id, daos_epoch_t epoch,
	    daos_sg_list_t **keys, int *holes);

/**
 * Enumerating non-empty key value objects in a pool
 *
 * Enumerate all KV objects in a given epoch
 * User specifies a required number of objects and is returned
 * a list of objectIDs. If the objectID count does not match the
 * one returned by the user then -1 is returned
 *
 * \param pool       [IN]      pool handle
 * \param epoch      [IN]      epoch
 * \param anchor     [IN/OUT]  anchor for the next objectID
                               it will be -1 there are no more objects
                               points to the location of ref for listing
                               objects
 * \param id_cnt     [OUT]     size of the object ID list
 * \param id_list    [OUT]     Object ID list
 *
 * \return                     zero on success, negative value if error
 */
int
vos_list_kv_objects(daos_handle_t pool, daos_epoch_t epoch,
		    daos_obj_id_t anchor, daos_size_t id_cnt,
		    daos_obj_id_t *obj_ids);

/**
 * Parsing a key-value store
 * These APIs allows to iterate over all the key-value pairs in an
 * object at a specific epoch
 *
 **/

struct kv_iter;
typedef struct kv_iter* kv_iterator_t;

/**
  * Create an iterator to iterate over all the KV pairs in the KV
  * store of an objectID for a specific epoch
  *
  * \param pool        [IN]       pool handle
  * \param object_id   [IN]       object ID to parse
  * \param epoch       [IN]       epoch
  * \param anchro      [IN/OUT]   anchor key
  * \param kv_iter     [OUT]      Iterator to iterate over all KV pairs
  *
  * \return                       zero on success, negative value if error
 */
int
vos_kv_iter_create(daos_handle_t pool, daos_obj_id_t obj_id, daos_epoch_t epoch,
		   daos_sg_list_t *anchor, kv_iterator_t *kv_iter);

/**
  * Delete a KV  iterator
  *
  * \param kv_iter    [IN]       Iterator to iterate over all KV pairs
  *
  * \return                      zero on success, negative value if error
 */
int
vos_kv_iter_destroy(kv_iterator_t *kv_iter);

/**
  * Move to the beginning of the KV store
  * This function moves the reference to parse the KV to the first
  * element of the KV which the iterator belongs.
  *
  * \param kv_iter      [IN/OUT]  Iterator
  *
  * \return                       zero on success, negative value if error
 */
int
vos_kv_begin(kv_iterator_t *kv_iter);

/**
  * Move the iterator to the position pointed by the key
  * This function moves the reference to parse the KV to the
  * position of the key element of the KV which the iterator belongs.
  *
  * \param kv_iter      [IN/OUT]  Iterator
  *
  * \return                       zero on success, negative value if error
 */
int
vos_kv_pos(kv_iterator_t *kv_iter, daos_sg_list_t *key);

/**
  * Move to the end of the KV store
  * This function moves the reference to parse the KV to the last
  * element of the KV which the iterator belongs.
  *
  * \param kv_iter      [IN/OUT]  Iterator
  *
  * \return                       zero on success, negative value if error
 */
int
vos_kv_end(kv_iterator_t *kv_iter);

/**
  * Move to the next element of the KV store
  * This function moves the reference to parse the KV to the next
  * element of the KV which the iterator belongs.
  *
  * \param kv_iter      [IN/OUT]  Iterator
  *
  * \return                       zero on success, negative value if error
 */
int
vos_kv_next(kv_iterator_t *kv_iter);

/**
 * Get value along with the key from the iterator
 *
 *
 * \param kv_iterator_t [IN]      current key-value entry reference
 * \param entry         [OUT]     current key-value pair
 *
 * \return                        returns 0 on success -1 on failure
 */
int
vos_kv_get_value(kv_iterator_t iterator, daos_kv_t* kv_entry);

/**
 * Get key from the iterator
 *
 *
 * \param kv_iterator_t [IN]      current key-value entry reference
 * \param key           [OUT]     current key
 * \param key_size      [OUT]     size of the key
 *
 * \return                        returns 0 on success -1 on failure
 */
int
vos_kv_get_key(kv_iterator_t iterator, void* key, daos_size_t key_size);

#endif /* __VOS_API_H */
