/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Thread local storage for vos
 * vos/vos_tls.h
 */

#ifndef __VOS_TLS_H__
#define __VOS_TLS_H__

#include <gurt/list.h>
#include <gurt/hash.h>
#include <daos/btree.h>
#include <daos/common.h>
#include <daos/lru.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/bio.h>
#include <daos_srv/dtx_srv.h>
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_producer.h>

/* Forward declarations */
struct vos_ts_table;
struct dtx_handle;

/** VOS thread local storage structure */
struct vos_tls {
	/** pools registered for GC */
	d_list_t			 vtl_gc_pools;
	/** tracking GC running status */
	int				 vtl_gc_running;
	/* PMDK transaction stage callback data */
	struct umem_tx_stage_data	 vtl_txd;
	/** XXX: The DTX handle.
	 *
	 *	 Transferring DTX handle via TLS can avoid much changing
	 *	 of existing functions' interfaces, and avoid the corner
	 *	 cases that someone may miss to set the DTX handle when
	 *	 operate related tree.
	 *
	 *	 But honestly, it is some hack to pass the DTX handle via
	 *	 the TLS. It requires that there is no CPU yield during the
	 *	 processing. Otherwise, the vtl_dth may be changed by other
	 *	 ULTs. The user needs to guarantee that by itself.
	 */
	struct dtx_handle		*vtl_dth;
	/** Timestamp table for xstream */
	struct vos_ts_table		*vtl_ts_table;
	/** profile for standalone vos test */
	struct daos_profile		*vtl_dp;
	/** In-memory object cache for the PMEM object table */
	struct daos_lru_cache		*vtl_ocache;
	/** pool open handle hash table */
	struct d_hash_table		*vtl_pool_hhash;
	/** container open handle hash table */
	struct d_hash_table		*vtl_cont_hhash;
	/** saved hash value */
	struct {
		uint64_t		 vtl_hash;
		bool			 vtl_hash_set;
	};
	struct d_tm_node_t		 *vtl_committed;
};

struct bio_xs_context *vos_xsctxt_get(void);
struct vos_tls *vos_tls_get();

static inline struct d_hash_table *
vos_pool_hhash_get(void)
{
	return vos_tls_get()->vtl_pool_hhash;
}

static inline struct d_hash_table *
vos_cont_hhash_get(void)
{
	return vos_tls_get()->vtl_cont_hhash;
}

static inline struct daos_lru_cache *
vos_obj_cache_get(void)
{
	return vos_tls_get()->vtl_ocache;
}

static inline struct umem_tx_stage_data *
vos_txd_get(void)
{
	return &vos_tls_get()->vtl_txd;
}

static inline struct vos_ts_table *
vos_ts_table_get(void)
{
	return vos_tls_get()->vtl_ts_table;
}

static inline void
vos_ts_table_set(struct vos_ts_table *ts_table)
{
	vos_tls_get()->vtl_ts_table = ts_table;
}

static inline void
vos_dth_set(struct dtx_handle *dth)
{
	struct vos_tls		*tls = vos_tls_get();
	struct dtx_share_peer	*dsp;

	if (dth != NULL && dth != tls->vtl_dth &&
	    dth->dth_share_tbd_count != 0) {
		while ((dsp = d_list_pop_entry(&dth->dth_share_tbd_list,
					       struct dtx_share_peer,
					       dsp_link)) != NULL)
			dtx_dsp_free(dsp);
		dth->dth_share_tbd_count = 0;
	}

	tls->vtl_dth = dth;
}

static inline struct dtx_handle *
vos_dth_get(void)
{
	struct vos_tls	*tls = vos_tls_get();

	if (tls != NULL)
		return vos_tls_get()->vtl_dth;

	return NULL;
}

static inline void
vos_kh_clear(void)
{
	vos_tls_get()->vtl_hash_set = false;
}

static inline void
vos_kh_set(uint64_t hash)
{
	struct vos_tls	*tls = vos_tls_get();

	tls->vtl_hash = hash;
	tls->vtl_hash_set = true;

}

static inline bool
vos_kh_get(uint64_t *hash)
{
	struct vos_tls	*tls = vos_tls_get();

	*hash = tls->vtl_hash;

	return tls->vtl_hash_set;
}

static inline uint64_t
vos_hash_get(const void *buf, uint64_t len)
{
	uint64_t	hash;

	if (vos_kh_get(&hash)) {
		vos_kh_clear();
		return hash;
	}

	return d_hash_murmur64(buf, len, BTR_MUR_SEED);
}

#ifdef VOS_STANDALONE
static inline uint64_t
vos_sched_seq(void)
{
	return 0;
}
#else
static inline uint64_t
vos_sched_seq(void)
{
	return sched_cur_seq();
}
#endif

#endif /* __VOS_TLS_H__ */
