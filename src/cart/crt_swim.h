/*
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It's the header for crt_swim.c.
 */

#ifndef __CRT_SWIM_H__
#define __CRT_SWIM_H__

#include <gurt/list.h>
#include <cart/swim.h>
#include "swim/swim_internal.h"

#define CRT_SWIM_NGLITCHES_TRESHOLD	10
#define CRT_SWIM_NMESSAGES_TRESHOLD	1000
#define CRT_SWIM_FLUSH_ATTEMPTS		100
#define CRT_SWIM_TARGET_INVALID		UINT32_MAX
#define CRT_DEFAULT_PROGRESS_CTX_IDX	1

struct crt_swim_target {
	d_list_t			 cst_link;
	swim_id_t			 cst_id;
	struct swim_member_state	 cst_state;
};

/**
 * The csm_table field is a hash table of crt_swim_target objects with cst_id
 * being key.
 *
 * The csm_list field points to an array of csm_list_cap entries. The first
 * csm_list_len entries are member ranks (the rest are empty). It is used to
 * hold a random permutation of all members.
 *
 * The csm_target field is an index into csm_list. It is used to select dping
 * and iping targets.
 */
struct crt_swim_membs {
	pthread_spinlock_t		 csm_lock;
	struct d_hash_table		*csm_table;
	d_rank_t			*csm_list;
	uint32_t			 csm_list_cap;
	uint32_t			 csm_list_len;
	uint32_t			 csm_target;
	struct swim_context		*csm_ctx;
	uint64_t			 csm_incarnation;
	uint64_t			 csm_last_unpack_hlc;
	uint64_t			 csm_alive_count;
	int				 csm_crt_ctx_idx;
	int				 csm_nglitches;
	int				 csm_nmessages;
};

static inline void
crt_swim_csm_lock(struct crt_swim_membs *csm)
{
	int rc;

	rc = D_SPIN_LOCK(&csm->csm_lock);
	if (rc != 0)
		DS_ERROR(rc, "D_SPIN_LOCK()");
}

static inline void
crt_swim_csm_unlock(struct crt_swim_membs *csm)
{
	int rc;

	rc = D_SPIN_UNLOCK(&csm->csm_lock);
	if (rc != 0)
		DS_ERROR(rc, "D_SPIN_UNLOCK()");
}

static inline uint32_t
crt_swim_rpc_timeout(void)
{
	uint32_t timeout_sec;

	/*
	 * Convert SWIM ping timeout from ms to seconds with rounding up.
	 * Increase it by 2 seconds to avoid early expiration by timeout
	 * in case of network glitches.
	 */
	timeout_sec = 3 + swim_ping_timeout_get() / 1000 /* ms per sec */;

	return timeout_sec;
}

int  crt_swim_enable(struct crt_grp_priv *grp_priv, int crt_ctx_idx);
int  crt_swim_disable(struct crt_grp_priv *grp_priv, int crt_ctx_idx);
void crt_swim_disable_all(void);
void crt_swim_suspend_all(void);
void crt_swim_accommodate(void);
int  crt_swim_rank_add(struct crt_grp_priv *grp_priv, d_rank_t rank, uint64_t incarnation);
int  crt_swim_rank_del(struct crt_grp_priv *grp_priv, d_rank_t rank);
void crt_swim_rank_del_all(struct crt_grp_priv *grp_priv);
void crt_swim_rank_shuffle(struct crt_grp_priv *grp_priv);
int crt_swim_rank_check(struct crt_grp_priv *grp_priv, d_rank_t rank, uint64_t incarnation);

#endif /* __CRT_SWIM_H__ */
