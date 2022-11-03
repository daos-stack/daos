/**
 * (C) Copyright 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_COMMON_DAV_WAL_TX_
#define __DAOS_COMMON_DAV_WAL_TX_

#include <gurt/list.h>
#include <daos_types.h>

struct dav_obj;

struct wal_tx {
	struct dav_obj		*wt_dav_hdl;
	uint64_t		 wt_id;
	d_list_t		 wt_redo;
	uint32_t		 wt_redo_cnt;
	uint32_t		 wt_redo_payload_len;
	struct umem_act_item	*wt_redo_act_pos;
};

extern int wal_tx_init(struct dav_obj *dav_hdl);
extern int wal_tx_commit(void *hdl);
extern int wal_tx_snap(void *hdl, void *addr, daos_size_t size, void *src, uint32_t flags);
extern int wal_tx_assign(void *hdl, void *addr, uint64_t val);
extern int wal_tx_clr_bits(void *hdl, void *addr, uint32_t pos, uint16_t num_bits);
extern int wal_tx_set_bits(void *hdl, void *addr, uint32_t pos, uint16_t num_bits);
extern int wal_tx_set(void *hdl, void *addr, char c, daos_size_t size);
extern uint32_t wal_tx_act_nr(struct wal_tx *tx);
extern uint32_t wal_tx_payload_len(struct wal_tx *tx);
extern struct umem_action *wal_tx_act_first(struct wal_tx *tx);
extern struct umem_action *wal_tx_act_next(struct wal_tx *tx);

#endif	/*__DAOS_COMMON_DAV_WAL_TX_*/
