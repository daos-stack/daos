/**
 * (C) Copyright 2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
	umem_off_t	id_tx_id;
	/** timestamp of entry */
	daos_epoch_t	id_epoch;
};

enum {
	ILOG_OPC_FIRST,
	ILOG_OPC_LAST,
	ILOG_OPC_LE,
	ILOG_OPC_EQ,
	ILOG_OPC_GE,
	ILOG_OPC_COUNT,
};

/** Opaque root for incarnation log */
struct  ilog_df {
	char	id_pad[24];
};

struct umem_instance;

enum ilog_status {
	ILOG_REMOVED,
	ILOG_VISIBLE,
	ILOG_INVISIBLE,
};

/** Near term hack to hook things up with existing DTX */
struct ilog_callbacks {
	enum ilog_status (*dc_status_get)(struct umem_instance *umm,
					  umem_off_t tx_id, uint32_t intent);
	int (*dc_is_same_tx)(struct umem_instance *umm, umem_off_t tx_id,
			     bool *same);
	int (*dc_register)(struct umem_instance *umm, umem_off_t ilog_off,
			   umem_off_t *tx_id);
	int (*dc_deregister)(struct umem_instance *umm, umem_off_t ilog_off,
			     umem_off_t tx_id);
};

/** Globally initialize incarnation log */
int
ilog_init(void);

/** Register a set of custom callbacks for txn state */
void
ilog_set_callbacks(const struct ilog_callbacks *cbs);

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
 *  \param	loh[OUT]	Returned open log handle
 *
 *  \return 0 on success, error code on failure
 */
int
ilog_open(struct umem_instance *umm, struct ilog_df *root,
	      daos_handle_t *loh);

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
 *  \param	loh[in]	Open handle to destroy, free'd on success
 *
 *  \return 0 on success, error code on failure
 */
int
ilog_destroy(daos_handle_t loh);

/** Adds an entry to the incarnation log.  Note that if the entry is
 *  redundant, it may not actually be added but the operation will
 *  still report success.
 */
int
ilog_update(daos_handle_t loh, daos_epoch_t epoch, bool punch);
/** Makes a specific update to the incarnation log permanent and
 *  removes redundant entries
 */
int
ilog_persist(daos_handle_t loh, const struct ilog_id *id);
/** Removes a specific entry from the incarnation log if it exists */
int
ilog_abort(daos_handle_t loh, const struct ilog_id *id);
/**
 * Remove entries in the epoch range leaving only the latest update */
int
ilog_aggregate(daos_handle_t loh, const daos_epoch_range_t *epr);

#define ILOG_NUM_EMBEDDED 16

struct ilog_entry {
	struct ilog_id		ie_id;
	bool			ie_punch;
	uint32_t		ie_status;
};

struct ilog_entries {
	struct ilog_entry	*ie_entries;
	int			 ie_num_entries;
	int			 ie_alloc_size;
	daos_handle_t		 ie_ih;
	struct ilog_entry	 ie_embedded[ILOG_NUM_EMBEDDED];
};

/** Initialize an ilog_entries struct for fetch */
void ilog_fetch_init(struct ilog_entries *entries);
/** Fetch the ilog within the epr range */
int ilog_fetch(daos_handle_t loh, uint32_t intent,
	       const daos_epoch_range_t *epr, struct ilog_entries *entries);
/** Deallocate any memory associated with an ilog_entries struct for fetch */
void ilog_fetch_finish(struct ilog_entries *entries);

#define ilog_foreach_entry(ents, entry)		\
	for (entry = &(ents)->ie_entries[0];	\
	     entry != &(ents)->ie_entries[(ents)->ie_num_entries]; entry++)

#define ilog_foreach_entry_reverse(ents, entry)				\
	for (entry = &(ents)->ie_entries[(ents)->ie_num_entries - 1];	\
	     entry != &(ents)->ie_entries[-1]; entry--)

#endif /* __ILOG_H__ */
