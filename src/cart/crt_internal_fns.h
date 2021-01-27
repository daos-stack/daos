/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It gives out the main CaRT internal function
 * declarations which are not included by other specific header files.
 */

#ifndef __CRT_INTERNAL_FNS_H__
#define __CRT_INTERNAL_FNS_H__

/** crt_init.c */
bool crt_initialized(void);

/** crt_register.c */
int crt_opc_map_create(unsigned int bits);
void crt_opc_map_destroy(struct crt_opc_map *map);
struct crt_opc_info *crt_opc_lookup(struct crt_opc_map *map, crt_opcode_t opc,
				    int locked);

/** crt_context.c */
/* return values of crt_context_req_track, in addition to standard
 * gurt error values.
 */
enum {
	CRT_REQ_TRACK_IN_INFLIGHQ = 0,
	CRT_REQ_TRACK_IN_WAITQ,
};

int crt_context_req_track(struct crt_rpc_priv *rpc_priv);
bool crt_context_empty(int locked);
void crt_context_req_untrack(struct crt_rpc_priv *rpc_priv);
crt_context_t crt_context_lookup(int ctx_idx);
crt_context_t crt_context_lookup_locked(int ctx_idx);
void crt_rpc_complete(struct crt_rpc_priv *rpc_priv, int rc);
int crt_req_timeout_track(struct crt_rpc_priv *rpc_priv);
void crt_req_timeout_untrack(struct crt_rpc_priv *rpc_priv);
void crt_req_force_timeout(struct crt_rpc_priv *rpc_priv);

/** crt_hlct.c */
uint64_t crt_hlct_get(void);
void crt_hlct_sync(uint64_t msg);

/** some simple helper functions */

static inline bool
crt_is_service()
{
	return crt_gdata.cg_server;
}

static inline void
crt_bulk_desc_dup(struct crt_bulk_desc *bulk_desc_new,
		  struct crt_bulk_desc *bulk_desc)
{
	D_ASSERT(bulk_desc_new != NULL && bulk_desc != NULL);
	*bulk_desc_new = *bulk_desc;
}

void
crt_hdlr_proto_query(crt_rpc_t *rpc_req);

#endif /* __CRT_INTERNAL_FNS_H__ */
