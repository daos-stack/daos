/*
 * (C) Copyright 2019-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of CaRT. It's the header for crt_swim.c.
 */

#ifndef __CRT_SWIM_H__
#define __CRT_SWIM_H__

#include "gurt/list.h"
#include "cart/swim.h"

#define CRT_SWIM_RPC_TIMEOUT		1	/* 1 sec */
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
	uint64_t			 csm_hlc;
	int				 csm_crt_ctx_idx;
};

int  crt_swim_enable(struct crt_grp_priv *grp_priv, int crt_ctx_idx);
int  crt_swim_disable(struct crt_grp_priv *grp_priv, int crt_ctx_idx);
void crt_swim_disable_all(void);
int  crt_swim_rank_add(struct crt_grp_priv *grp_priv, d_rank_t rank);
int  crt_swim_rank_del(struct crt_grp_priv *grp_priv, d_rank_t rank);
void crt_swim_rank_del_all(struct crt_grp_priv *grp_priv);

#endif /* __CRT_SWIM_H__ */
