/* Copyright (C) 2017 Intel Corporation
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
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
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
 * This file is part of CaRT.  It is the internal barrier interface.
 */

#ifndef __CRT_BARRIER_H__
#define __CRT_BARRIER_H__

#define CRT_MAX_BARRIER_INFLIGHT 4

struct crt_barrier {
	crt_rpc_t		*b_enter_rpc;   /* enter rpc */
	crt_barrier_cb_t	b_complete_cb;  /* user callback */
	void			*b_arg;         /* user callback arg */
	bool			b_active;       /* Local rank in barrier */
	bool			b_pending_exit; /* Master ready to exit */
};

struct crt_barrier_info {
	crt_rank_list_t		*bi_exclude_self;    /* rank list for self */
	struct crt_grp_priv	*bi_primary_grp;     /* primary group */
	pthread_mutex_t		 bi_lock;            /* lock for barriers */
	struct crt_barrier	 bi_barriers[CRT_MAX_BARRIER_INFLIGHT];
	crt_rank_t		 bi_master_pri_rank; /* lowest live rank */
	int			 bi_master_idx;      /* index of master */
	int			 bi_num_created;     /* creation count */
	int			 bi_num_exited;      /* completion count */
};

void crt_barrier_info_init(struct crt_grp_priv *grp_priv);
void crt_barrier_info_destroy(struct crt_grp_priv *grp_priv);
int crt_hdlr_barrier_enter(crt_rpc_t *rpc_req);
int crt_hdlr_barrier_exit(crt_rpc_t *rpc_req);
int crt_hdlr_barrier_aggregate(crt_rpc_t *source, crt_rpc_t *result,
			       void *priv);
/* Update the barrier master */
bool crt_barrier_update_master(struct crt_grp_priv *grp_priv);

/* Transfer to new barrier master, if necessary, on rank eviction */
void crt_barrier_handle_eviction(struct crt_grp_priv *grp_priv);


#endif /* __CRT_BARRIER_H__ */
