/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * VOS Object/Key incarnation log
 * vos/ilog.h
 *
 * Author: Jeff Olivier <jeffrey.v.olivier@intel.com>
 */

#ifndef __ILOG_H__
#define __ILOG_H__
#include <daos_types.h>

struct ilog_id {
	/** DTX of entry */
	union {
		uint64_t	id_value;
		struct {
			uint32_t	 id_tx_id;
			uint16_t	 id_punch_minor_eph;
			uint16_t	 id_update_minor_eph;
		};
	};
	/** timestamp of entry */
	daos_epoch_t	id_epoch;
};

/** Opaque root for incarnation log */
struct  ilog_df {
	char	id_pad[24];
};

struct umem_instance;

enum ilog_status {
	/** Log status is not set */
	ILOG_INVALID,
	/** Log entry is visible to caller */
	ILOG_COMMITTED,
	/** Log entry is not yet visible */
	ILOG_UNCOMMITTED,
	/** Log entry can be removed */
	ILOG_REMOVED,
};

/** Near term hack to hook things up with existing DTX */
struct ilog_desc_cbs {
	/** Retrieve the status of a log entry (See enum ilog_status). On error
	 *  return error code < 0.
	 */
	int (*dc_log_status_cb)(struct umem_instance *umm, uint32_t tx_id,
				daos_epoch_t epoch, uint32_t intent,
				void *args);
	void	*dc_log_status_args;
	/** Check if the log entry was created by current transaction */
	int (*dc_is_same_tx_cb)(struct umem_instance *umm, uint32_t tx_id,
				daos_epoch_t epoch, bool *same, void *args);
	void	*dc_is_same_tx_args;
	/** Register the log entry with the transaction log */
	int (*dc_log_add_cb)(struct umem_instance *umm, umem_off_t ilog_off,
			     uint32_t *tx_id, daos_epoch_t epoch, void *args);
	void	*dc_log_add_args;
	/** Remove the log entry from the transaction log */
	int (*dc_log_del_cb)(struct umem_instance *umm, umem_off_t ilog_off,
			     uint32_t tx_id, daos_epoch_t epoch, bool abort,
			     void *args);
	void	*dc_log_del_args;
};

/** Globally initialize incarnation log */
int
ilog_init(void);

/** Create a new incarnation log in place
 *
 *  \param	umm[IN]		The umem instance
 *  \param	root[IN]	A pointer to the allocated root
 *
 *  \return 0 on success, error code on failure
 */
int
ilog_create(struct umem_instance *umm, struct ilog_df *root);

/** Open an existing incarnation log in place and create a handle to
 *  access it.
 *
 *  \param	umm[IN]		The umem instance
 *  \param	root[IN]	A pointer to the allocated root
 *  \param	cbs[in]		Incarnation log transaction log callbacks
 *  \param	loh[OUT]	Returned open log handle
 *
 *  \return 0 on success, error code on failure
 */
int
ilog_open(struct umem_instance *umm, struct ilog_df *root,
	  const struct ilog_desc_cbs *cbs, daos_handle_t *loh);

/** Close an open incarnation log handle
 *
 *  \param	loh[in]	Open handle to close
 *
 *  \return 0 on success, error code on failure
 */
int
ilog_close(daos_handle_t loh);

/** Destroy an incarnation log
 *
 *  \param	umm[in]		The umem instance
 *  \param	cbs[in]		Incarnation log transaction log callbacks
 *  \param	root[IN]	A pointer to an initialized incarnation log
 *
 *  \return 0 on success, error code on failure
 */
int
ilog_destroy(struct umem_instance *umm, struct ilog_desc_cbs *cbs,
	     struct ilog_df *root);

/** Logs or updates an entry in the incaration log identified by the epoch
 *  and the currently executing transaction.  If a visible creation entry
 *  exists, nothing will be logged and the function will succeed.
 *
 *  \param	loh[in]		Open log handle
 *  \param	epr[in]		Limiting range
 *  \param	major_eph[in]	Major epoch of update
 *  \param	minor_eph[in]	Minor epoch of update
 *  \param	punch[in]	Punch if true, update otherwise
 *
 *  \return 0 on success, error code on failure
 */
int
ilog_update(daos_handle_t loh, const daos_epoch_range_t *epr,
	    daos_epoch_t major_eph, uint16_t minor_eph, bool punch);

/** Updates specified log entry to mark it as persistent (remove
 * the transaction identifier from the entry.   Additionally, this will
 * remove redundant entries, such as later uncommitted updates.
 *
 *  \param	loh[in]		Open log handle
 *  \param	id[in]		Identifier for log entry
 *
 *  \return 0 on success, error code on failure
 */
int
ilog_persist(daos_handle_t loh, const struct ilog_id *id);

/** Removes an aborted entry from the incarnation log
 *
 *  \param	loh[in]		Open log handle
 *  \param	id[in]		Identifier for log entry
 *
 *  \return 0 on success, error code on failure
 */
int
ilog_abort(daos_handle_t loh, const struct ilog_id *id);

/** Incarnation log entry description */
struct ilog_entry {
	/** The epoch and tx_id for the log entry */
	struct ilog_id	ie_id;
	/** The status of the incarnation log entry.  See enum ilog_status */
	int32_t		ie_status;
	/** Index of the ilog entry */
	int32_t		ie_idx;
};

#define ILOG_PRIV_SIZE 416
/** Structure for storing the full incarnation log for ilog_fetch.  The
 * fields shouldn't generally be accessed directly but via the iteration
 * APIs below.
 */
struct ilog_entries {
	/** Array of log entries */
	struct ilog_id		*ie_ids;
	uint32_t		*ie_statuses;
	/** Number of entries in the log */
	int64_t			 ie_num_entries;
	/** Private log data */
	uint8_t			 ie_priv[ILOG_PRIV_SIZE];
};

/**
 * Cleanup the incarnation log
 *
 *  \param	umm[in]		The umem instance
 *  \param	root[in]	Pointer to log root
 *  \param	cbs[in]		Incarnation log transaction log callbacks
 *  \param	epr[in]		Epoch range for cleanup
 *  \param	discard[in]	Normally, aggregate will only remove entries
 *				that are provably not needed.  If discard is
 *				set, it will remove everything in the epoch
 *				range.
 *  \param	punch_major[in]	Max major epoch punch of parent incarnation log
 *  \param	punch_major[in]	Max minor epoch punch of parent incarnation log
 *  \param	entries[in]	Used for efficiency since aggregation is used
 *				by vos_iterator
 *
 *  \return	0		success
 *		1		success but indicates log is empty
 *		< 0		Error
 */
int
ilog_aggregate(struct umem_instance *umm, struct ilog_df *root,
	       const struct ilog_desc_cbs *cbs, const daos_epoch_range_t *epr,
	       bool discard, daos_epoch_t punch_major, uint16_t punch_minor,
	       struct ilog_entries *entries);

/** Initialize an ilog_entries struct for fetch
 *
 *  \param	entries[in]	Allocated structure where entries are stored
 */
void
ilog_fetch_init(struct ilog_entries *entries);

/** Fetch the entire incarnation log.  This function will refresh only when
 * the underlying log or the intent has changed.  If the struct is shared
 * between multiple ULT's fetch should be done after every yield.
 *
 *  \param	umm[in]		The umem instance
 *  \param	root[in]	Pointer to log root
 *  \param	cbs[in]		Incarnation log transaction log callbacks
 *  \param	intent[in]	The intent of the operation
 *  \param	entries[in,out]	Allocated structure passed in will be filled
 *				with incarnation log entries in the range.
 *
 *  \return 0 on success, error code on failure
 */
int
ilog_fetch(struct umem_instance *umm, struct ilog_df *root,
	   const struct ilog_desc_cbs *cbs, uint32_t intent,
	   struct ilog_entries *entries);

/** Deallocate any memory associated with an ilog_entries struct for fetch
 *
 *  \param	entries[in]	Allocated structure to be finalized
 */
void
ilog_fetch_finish(struct ilog_entries *entries);

/** For internal use by ilog_foreach* */
static inline bool
ilog_cache_entry(const struct ilog_entries *entries, struct ilog_entry *entry, int idx)
{
	entry->ie_id.id_value = entries->ie_ids[idx].id_value;
	entry->ie_id.id_epoch = entries->ie_ids[idx].id_epoch;
	entry->ie_status = entries->ie_statuses[idx];
	return true;
}

/** Iterator for fetched incarnation log entries
 *
 *  \param	entries[in]	The fetched entries
 */
#define ilog_foreach_entry(ents, entry)						\
	for ((entry)->ie_idx = 0; (entry)->ie_idx < (ents)->ie_num_entries &&	\
	     ilog_cache_entry(ents, entry, (entry)->ie_idx); (entry)->ie_idx++)

/** Reverse iterator for fetched incarnation log entries
 *
 *  \param	entries[in]	The fetched entries
 */
#define ilog_foreach_entry_reverse(ents, entry)						\
	for ((entry)->ie_idx = (ents)->ie_num_entries - 1; (entry)->ie_idx >= 0 &&	\
	     ilog_cache_entry(ents, entry, (entry)->ie_idx); (entry)->ie_idx--)

/** Fetch the address of the timestamp index from the ilog
 *
 * \param	entries[in]	The incarnation log
 *
 * \returns a pointer to the index
 */
uint32_t *
ilog_ts_idx_get(struct ilog_df *ilog_df);

/** Retrieve the current version of the incarnation log
 *
 * \param	loh[in]	Open log handle
 *
 * Returns the version of the log or 0 if log handle is invalid
 **/
uint32_t
ilog_version_get(daos_handle_t loh);

/** Returns true if there is a punch minor epoch */
static inline bool
ilog_has_punch(const struct ilog_entry *entry)
{
	return entry->ie_id.id_punch_minor_eph > 0;
}

static inline bool
ilog_is_punch(const struct ilog_entry *entry)
{
	return entry->ie_id.id_punch_minor_eph >
		entry->ie_id.id_update_minor_eph;
}

#endif /* __ILOG_H__ */
