/**
 * (C) Copyright 2020 Intel Corporation.
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
 * Record timestamp table
 * vos/vos_ts.h
 *
 * Author: Jeff Olivier <jeffrey.v.olivier@intel.com>
 */

#ifndef __VOS_TS__
#define __VOS_TS__

#include <daos_types.h>
#include <daos/common.h>

struct vos_ts_entry {
	/** Uniquely identifies the record */
	void		*te_record_ptr;
	/** Next most recently used */
	uint32_t		 te_next_idx;
	/** Previous most recently used */
	uint32_t		 te_prev_idx;
	/** Low read time */
	daos_epoch_t	 te_ts_rl;
	/** High read time */
	daos_epoch_t	 te_ts_rh;
	/** Write time */
	daos_epoch_t	 te_ts_w;
	/** uuid's of transactions.  These can potentially be changed
	 *  to 16 bits and save some space here.  But for now, stick
	 *  with the full id.
	 */
	/** Low read tx */
	uuid_t		 te_tx_rl;
	/** high read tx */
	uuid_t		 te_tx_rh;
	/** write tx */
	uuid_t		 te_tx_w;
};

#define VOS_TS_BITS	20
#define VOS_TS_SIZE	(1 << VOS_TS_BITS)
#define VOS_TS_MASK	(VOS_TS_SIZE - 1)

enum {
	VOS_TS_TYPE_OBJ,
	VOS_TS_TYPE_DKEY,
	VOS_TS_TYPE_AKEY,
	VOS_TS_TYPE_COUNT,
};

struct vos_ts_info {
	/** Least recently accessed index */
	uint32_t		ti_lru;
	/** Most recently accessed index */
	uint32_t		ti_mru;
	/** Type read low timestamp */
	daos_epoch_t		ti_ts_rl;
	/** Type read high timestamp */
	daos_epoch_t		ti_ts_rh;
	/** Type write timestamp */
	daos_epoch_t		ti_ts_w;
};

struct vos_ts_table {
	/** Timestamp table pointers for a type */
	struct vos_ts_info	tt_type_info[VOS_TS_TYPE_COUNT];
	/** The table entries */
	struct vos_ts_entry	tt_table[VOS_TS_SIZE];
};

/** Evict the LRU, move it to MRU, update global time stamps, and return the index */
uint32_t
vos_ts_evict_lru(struct vos_ts_table *ts_table, struct vos_ts_entry **entryp,
		 uint32_t type);

static inline void
move_lru(struct vos_ts_table *ts_table, struct vos_ts_entry *entry,
	 uint32_t idx, uint32_t type)
{
	uint32_t		 prev_idx;
	uint32_t		 next_idx;
	struct vos_ts_entry	*prev;
	struct vos_ts_entry	*next;
	struct vos_ts_info	*info = &ts_table->tt_type_info[type];

	if (info->ti_mru == idx) {
		/** Already the mru */
		return;
	}

	next_idx = entry->te_next_idx;
	if (info->ti_lru == idx)
		info->ti_lru = next_idx;

	/** First remove */
	prev_idx = entry->te_prev_idx;
	prev = &ts_table->tt_table[prev_idx];
	next = &ts_table->tt_table[next_idx];
	next->te_prev_idx = prev_idx;
	prev->te_next_idx = next_idx;

	/** Now add */
	next_idx = info->ti_lru;
	prev_idx = info->ti_mru;
	prev = &ts_table->tt_table[prev_idx];
	next = &ts_table->tt_table[next_idx];
	next->te_prev_idx = idx;
	prev->te_next_idx = idx;
	entry->te_prev_idx = prev_idx;
	entry->te_next_idx = next_idx;

	info->ti_mru = idx;
}

static inline struct vos_ts_entry *
vos_ts_lookup(struct vos_ts_table *ts_table, uint32_t *idx, uint32_t type)
{
	struct vos_ts_entry	*entry;
	uint32_t		 tindex = *idx & VOS_TS_MASK;

	entry = &ts_table->tt_table[tindex];
	if (entry->te_record_ptr == idx) {
		move_lru(ts_table, entry, tindex, type);
		return entry;
	}

	*idx = vos_ts_evict_lru(ts_table, &entry, type);
	entry->te_record_ptr = idx;

	return entry;

}

int
vos_ts_table_alloc(struct vos_ts_table **ts_tablep, daos_epoch_t start_hlc);

void
vos_ts_table_free(struct vos_ts_table *ts_tablep);

#endif /* __VOS_TS__ */
