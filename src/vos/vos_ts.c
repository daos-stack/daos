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
 * vos/vos_ts.c
 *
 * Author: Jeff Olivier <jeffrey.v.olivier@intel.com>
 */
#define D_LOGFAC DD_FAC(vos)

#include "vos_ts.h"

#define OBJ_TABLE_SIZE (32 * 1024)
#define DKEY_TABLE_SIZE (128 * 1024)
#define AKEY_TABLE_SIZE (VOS_TS_SIZE - OBJ_TABLE_SIZE - DKEY_TABLE_SIZE)
D_CASSERT((int)AKEY_TABLE_SIZE > 0);

int
vos_ts_table_alloc(struct vos_ts_table **ts_tablep, daos_epoch_t start_hlc)
{
	struct vos_ts_table	*ts_table;
	struct vos_ts_info	*info;
	struct vos_ts_entry	*current;
	uint32_t		 cur_idx;
	uint32_t		 next_idx;
	uint32_t		 prev_idx;
	uint32_t		 i, count, offset;

	D_ALLOC_PTR(ts_table);
	if (ts_table == NULL)
		return -DER_NOMEM;

	cur_idx = 0;
	for (i = 0; i < VOS_TS_TYPE_COUNT; i++) {
		info = &ts_table->tt_type_info[i];
		info->ti_ts_rl = start_hlc;
		info->ti_ts_rh = start_hlc;
		info->ti_ts_w = start_hlc;

		switch (i) {
		case VOS_TS_TYPE_OBJ:
			count = OBJ_TABLE_SIZE;
			offset = cur_idx;
			break;
		case VOS_TS_TYPE_DKEY:
			count = DKEY_TABLE_SIZE;
			offset = cur_idx;
			break;
		case VOS_TS_TYPE_AKEY:
			count = AKEY_TABLE_SIZE;
			offset = cur_idx;
			D_ASSERT(offset + count == VOS_TS_SIZE);
			break;
		}

		info->ti_lru = cur_idx;
		prev_idx = info->ti_mru = offset + count - 1;
		while (cur_idx < (offset + count)) {
			next_idx = offset + ((cur_idx + 1 - offset) % count);
			current = &ts_table->tt_table[cur_idx];
			current->te_next_idx = next_idx;
			current->te_prev_idx = prev_idx;
			prev_idx = cur_idx;
			cur_idx++;
		}
	}

	*ts_tablep = ts_table;

	return 0;
}

void
vos_ts_table_free(struct vos_ts_table *ts_table)
{
	D_FREE(ts_table);
}

uint32_t
vos_ts_evict_lru(struct vos_ts_table *ts_table, struct vos_ts_entry **entryp,
		 uint32_t type)
{
	struct vos_ts_entry	*entry;
	uint32_t		 idx;
	struct vos_ts_info	*info = &ts_table->tt_type_info[type];

	/** Ok, grab and evict the LRU */
	idx = info->ti_lru;
	entry = &ts_table->tt_table[idx];
	info->ti_lru = entry->te_next_idx;
	info->ti_mru = idx;

	if (entry->te_record_ptr != NULL) {
		info->ti_ts_rl = MAX(info->ti_ts_rl, entry->te_ts_rl);
		info->ti_ts_rh = MAX(info->ti_ts_rh, entry->te_ts_rh);
		info->ti_ts_w = MAX(info->ti_ts_w, entry->te_ts_w);
	}

	/** Set new entry to the lower bounds */
	entry->te_ts_rl = info->ti_ts_rl;
	entry->te_ts_rh = info->ti_ts_rh;
	entry->te_ts_w = info->ti_ts_w;
	entry->te_record_ptr = NULL;
	uuid_clear(entry->te_tx_rl);
	uuid_clear(entry->te_tx_rh);
	uuid_clear(entry->te_tx_w);

	*entryp = entry;

	return idx;
}
