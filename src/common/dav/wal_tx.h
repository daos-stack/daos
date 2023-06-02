/**
 * (C) Copyright 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_COMMON_DAV_WAL_TX_
#define __DAOS_COMMON_DAV_WAL_TX_

#include <gurt/list.h>
#include <daos_types.h>
#include <daos/mem.h>

struct dav_obj;

struct wal_action {
	d_list_t                wa_link;
	struct umem_action      wa_act;
};

struct dav_tx {
	struct dav_obj		*wt_dav_hdl;
	d_list_t		 wt_redo;
	uint32_t		 wt_redo_cnt;
	uint32_t		 wt_redo_payload_len;
	struct wal_action	*wt_redo_act_pos;
};
D_CASSERT(sizeof(struct dav_tx) <= UTX_PRIV_SIZE,
	  "Size of struct dav_tx is too big!");

#define dav_action_get_next(it) d_list_entry(it.next, struct wal_action, wa_link)

struct umem_wal_tx *dav_umem_wtx_new(struct dav_obj *dav_hdl);
void dav_umem_wtx_cleanup(struct umem_wal_tx *utx);
int dav_wal_tx_reserve(struct dav_obj *hdl, uint64_t *id);
int dav_wal_tx_commit(struct dav_obj *hdl, struct umem_wal_tx *utx, void *data);
int dav_wal_tx_snap(void *hdl, void *addr, daos_size_t size, void *src, uint32_t flags);
int dav_wal_tx_assign(void *hdl, void *addr, uint64_t val);
int dav_wal_tx_clr_bits(void *hdl, void *addr, uint32_t pos, uint16_t num_bits);
int dav_wal_tx_set_bits(void *hdl, void *addr, uint32_t pos, uint16_t num_bits);
int dav_wal_tx_set(void *hdl, void *addr, char c, daos_size_t size);
int dav_wal_replay_cb(uint64_t tx_id, struct umem_action *act, void *base);

#endif	/*__DAOS_COMMON_DAV_WAL_TX_*/
