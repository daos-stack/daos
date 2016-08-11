/**
 * (C) Copyright 2016 Intel Corporation.
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
 * This file is part of CaRT. It gives out the main CaRT internal function
 * declarations which are not included by other specific header files.
 */

#ifndef __CRT_INTERNAL_FNS_H__
#define __CRT_INTERNAL_FNS_H__

#include <crt_internal_types.h>

/** crt_init.c */
bool crt_initialized();

/** crt_group.c */
crt_group_id_t crt_global_grp_id(void);
int crt_hdlr_grp_create(crt_rpc_t *rpc_req);
int crt_hdlr_grp_destroy(crt_rpc_t *rpc_req);

/** crt_register.c */
int crt_opc_map_create(unsigned int bits);
void crt_opc_map_destroy(struct crt_opc_map *map);
struct crt_opc_info *crt_opc_lookup(struct crt_opc_map *map, crt_opcode_t opc,
				    int locked);
int crt_rpc_reg_internal(crt_opcode_t opc, struct crt_req_format *drf,
			 crt_rpc_cb_t rpc_handler,
			 struct crt_corpc_ops *co_ops);

/** crt_context.c */
bool crt_context_empty(int locked);
int crt_context_req_track(crt_rpc_t *req);
void crt_context_req_untrack(crt_rpc_t *req);
crt_context_t crt_context_lookup(int ctx_idx);
void crt_rpc_complete(struct crt_rpc_priv *rpc_priv, int rc);

enum {
	CRT_REQ_TRACK_IN_INFLIGHQ = 0,
	CRT_REQ_TRACK_IN_WAITQ,
};

/** some simple helper functions */

static inline void
crt_bulk_desc_dup(struct crt_bulk_desc *bulk_desc_new,
		  struct crt_bulk_desc *bulk_desc)
{
	C_ASSERT(bulk_desc_new != NULL && bulk_desc != NULL);
	*bulk_desc_new = *bulk_desc;
}

static inline uint64_t
crt_time_usec(unsigned sec_diff)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);
	return (tv.tv_sec + sec_diff) * 1000 * 1000 + tv.tv_usec;
}

static inline bool
crt_ep_identical(crt_endpoint_t *ep1, crt_endpoint_t *ep2)
{
	C_ASSERT(ep1 != NULL);
	C_ASSERT(ep2 != NULL);
	/* TODO: check group */
	if (ep1->ep_rank == ep2->ep_rank)
		return true;
	else
		return false;
}

static inline void
crt_ep_copy(crt_endpoint_t *dst_ep, crt_endpoint_t *src_ep)
{
	C_ASSERT(dst_ep != NULL);
	C_ASSERT(src_ep != NULL);
	/* TODO: copy grp id */
	dst_ep->ep_rank = src_ep->ep_rank;
	dst_ep->ep_tag = src_ep->ep_tag;
}

#endif /* __CRT_INTERNAL_FNS_H__ */
