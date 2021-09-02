/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It's the header for crt_swim.c.
 */

#ifndef __CRT_SWIM_H__
#define __CRT_SWIM_H__

#include "gurt/list.h"
#include "cart/swim.h"
#include "swim/swim_internal.h"

#define CRT_SWIM_FLUSH_ATTEMPTS		100
#define CRT_SWIM_PROGRESS_TIMEOUT	0	/* minimal progressing time */
#define CRT_DEFAULT_PROGRESS_CTX_IDX	0

struct crt_swim_target {
	d_circleq_entry(crt_swim_target) cst_link;
	swim_id_t			 cst_id;
	struct swim_member_state	 cst_state;
};

struct crt_swim_membs {
	pthread_spinlock_t		 csm_lock;
	D_CIRCLEQ_HEAD(, crt_swim_target) csm_head;
	struct crt_swim_target		*csm_target;
	struct swim_context		*csm_ctx;
	int				 csm_crt_ctx_idx;
	uint64_t			 csm_incarnation;
};

static inline void
crt_swim_csm_lock(struct crt_swim_membs *csm)
{
	int rc;

	rc = D_SPIN_LOCK(&csm->csm_lock);
	if (rc != 0)
		D_ERROR("D_SPIN_LOCK(): %s\n", strerror(rc));
}

static inline void
crt_swim_csm_unlock(struct crt_swim_membs *csm)
{
	int rc;

	rc = D_SPIN_UNLOCK(&csm->csm_lock);
	if (rc != 0)
		D_ERROR("D_SPIN_UNLOCK(): %s\n", strerror(rc));
}

static inline uint32_t
crt_swim_rpc_timeout(void)
{
	uint32_t timeout_sec;

	/*
	 * Convert SWIM ping timeout from ms to seconds with rounding up
	 */
	timeout_sec = 1 + swim_ping_timeout_get() / 1000 /* ms per sec */;

	return timeout_sec;
}

int  crt_swim_enable(struct crt_grp_priv *grp_priv, int crt_ctx_idx);
int  crt_swim_disable(struct crt_grp_priv *grp_priv, int crt_ctx_idx);
void crt_swim_disable_all(void);
void crt_swim_suspend_all(void);
void crt_swim_accommodate(void);
int  crt_swim_rank_add(struct crt_grp_priv *grp_priv, d_rank_t rank);
int  crt_swim_rank_del(struct crt_grp_priv *grp_priv, d_rank_t rank);
void crt_swim_rank_del_all(struct crt_grp_priv *grp_priv);

#endif /* __CRT_SWIM_H__ */
