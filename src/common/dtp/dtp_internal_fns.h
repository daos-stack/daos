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
 * This file is part of daos_transport. It gives out the main dtp internal
 * function declarations which are not included by other specific header files.
 */

#ifndef __DTP_INTERNAL_FNS_H__
#define __DTP_INTERNAL_FNS_H__

#include <dtp_internal_types.h>

/** dtp_init.c */
bool dtp_initialized();

/** dtp_group.c */
dtp_group_id_t dtp_global_grp_id(void);
int dtp_hdlr_grp_create(dtp_rpc_t *rpc_req);
int dtp_hdlr_grp_destroy(dtp_rpc_t *rpc_req);

/** dtp_register.c */
int dtp_opc_map_create(unsigned int bits);
void dtp_opc_map_destroy(struct dtp_opc_map *map);
struct dtp_opc_info *dtp_opc_lookup(struct dtp_opc_map *map, dtp_opcode_t opc,
				    int locked);
int dtp_rpc_reg_internal(dtp_opcode_t opc, struct dtp_req_format *drf,
			 dtp_rpc_cb_t rpc_handler,
			 struct dtp_corpc_ops *co_ops);

/** dtp_context.c */
bool dtp_context_empty(int locked);
int dtp_context_req_track(dtp_rpc_t *req);
void dtp_context_req_untrack(dtp_rpc_t *req);
dtp_context_t dtp_context_lookup(int ctx_idx);
void dtp_rpc_complete(struct dtp_rpc_priv *rpc_priv, int rc);

enum {
	DTP_REQ_TRACK_IN_INFLIGHQ = 0,
	DTP_REQ_TRACK_IN_WAITQ,
};

/** some simple helper functions */

static inline void
dtp_bulk_desc_dup(struct dtp_bulk_desc *bulk_desc_new,
		  struct dtp_bulk_desc *bulk_desc)
{
	D_ASSERT(bulk_desc_new != NULL && bulk_desc != NULL);
	*bulk_desc_new = *bulk_desc;
}

static inline uint64_t
dtp_time_usec(unsigned sec_diff)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);
	return (tv.tv_sec + sec_diff) * 1000 * 1000 + tv.tv_usec;
}

static inline bool
dtp_ep_identical(dtp_endpoint_t *ep1, dtp_endpoint_t *ep2)
{
	D_ASSERT(ep1 != NULL);
	D_ASSERT(ep2 != NULL);
	/* TODO: check group */
	if (ep1->ep_rank == ep2->ep_rank)
		return true;
	else
		return false;
}

static inline void
dtp_ep_copy(dtp_endpoint_t *dst_ep, dtp_endpoint_t *src_ep)
{
	D_ASSERT(dst_ep != NULL);
	D_ASSERT(src_ep != NULL);
	/* TODO: copy grp id */
	dst_ep->ep_rank = src_ep->ep_rank;
	dst_ep->ep_tag = src_ep->ep_tag;
}

#endif /* __DTP_INTERNAL_FNS_H__ */
