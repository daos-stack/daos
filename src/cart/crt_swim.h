/* Copyright (C) 2019-2020 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 * 4. All publications or advertising materials mentioning features or use of
 *    this software are asked, but not required, to acknowledge that it was
 *    developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * This file is part of CaRT. It's the header for crt_swim.c.
 */

#ifndef __CRT_SWIM_H__
#define __CRT_SWIM_H__

#include "gurt/list.h"
#include "cart/swim.h"

#define CRT_SWIM_RPC_TIMEOUT		1	/* 1 sec */
#define CRT_SWIM_FLUSH_ATTEMPTS		10
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
};


int  crt_swim_enable(struct crt_grp_priv *grp_priv, int crt_ctx_idx);
int  crt_swim_disable(struct crt_grp_priv *grp_priv, int crt_ctx_idx);
void crt_swim_disable_all(void);
int  crt_swim_rank_add(struct crt_grp_priv *grp_priv, d_rank_t rank);
int  crt_swim_rank_del(struct crt_grp_priv *grp_priv, d_rank_t rank);
void crt_swim_rank_del_all(struct crt_grp_priv *grp_priv);

#endif /* __CRT_SWIM_H__ */
