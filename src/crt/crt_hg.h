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
 * This file is part of CaRT. It is the header file of bridging to mercury.
 */
#ifndef __CRT_MERCURY_H__
#define __CRT_MERCURY_H__

#include <crt_util/list.h>

#include <mercury.h>
#include <mercury_types.h>
#include <mercury_macros.h>
#include <mercury_proc.h>
#include <mercury_proc_string.h>
#include <na.h>
#include <na_error.h>

/** change to 0 to disable the low-level unpack */
#define CRT_HG_LOWLEVEL_UNPACK	(1)

/** the shared HG RPC ID used for all CRT opc */
#define CRT_HG_RPCID	(0xDA036868)

struct crt_rpc_priv;
struct crt_common_hdr;

/** HG context */
struct crt_hg_context {
	bool			dhc_shared_na; /* flag for shared na_class */
	na_class_t		*dhc_nacla; /* NA class */
	na_context_t		*dhc_nactx; /* NA context */
	hg_class_t		*dhc_hgcla; /* HG class */
	hg_context_t		*dhc_hgctx; /* HG context */
	hg_class_t		*dhc_bulkcla; /* bulk class */
	hg_context_t		*dhc_bulkctx; /* bulk context */
};

/** HG level global data */
struct crt_hg_gdata {
	na_class_t		*dhg_nacla; /* NA class */
	na_context_t		*dhg_nactx; /* NA context */
	hg_class_t		*dhg_hgcla; /* HG class */
};

/* crt_hg.c */
int crt_hg_init(crt_phy_addr_t *addr, bool server);
int crt_hg_fini();
int crt_hg_ctx_init(struct crt_hg_context *hg_ctx, int idx);
int crt_hg_ctx_fini(struct crt_hg_context *hg_ctx);
int crt_hg_req_create(struct crt_hg_context *hg_ctx, crt_endpoint_t tgt_ep,
		      struct crt_rpc_priv *rpc_priv);
int crt_hg_req_destroy(struct crt_rpc_priv *rpc_priv);
int crt_hg_req_send(struct crt_rpc_priv *rpc_priv);
int crt_hg_reply_send(struct crt_rpc_priv *rpc_priv);
int crt_hg_req_cancel(struct crt_rpc_priv *rpc_priv);
int crt_hg_progress(struct crt_hg_context *hg_ctx, int64_t timeout);

int crt_rpc_handler_common(hg_handle_t hg_hdl);

/* crt_hg_proc.c */
int crt_proc_common_hdr(crt_proc_t proc, struct crt_common_hdr *hdr);
int crt_hg_unpack_header(struct crt_rpc_priv *rpc_priv, crt_proc_t *proc);
void crt_hg_unpack_cleanup(crt_proc_t proc);
int crt_proc_internal(struct drf_field *drf, crt_proc_t proc, void *data);
int crt_proc_input(struct crt_rpc_priv *rpc_priv, crt_proc_t proc);
int crt_proc_output(struct crt_rpc_priv *rpc_priv, crt_proc_t proc);
int crt_hg_unpack_body(struct crt_rpc_priv *rpc_priv, crt_proc_t proc);
int crt_proc_in_common(crt_proc_t proc, crt_rpc_input_t *data);
int crt_proc_out_common(crt_proc_t proc, crt_rpc_output_t *data);

/* some simple helper functions */
typedef hg_rpc_cb_t crt_hg_rpc_cb_t;
static inline int
crt_hg_reg(hg_class_t *hg_class, hg_id_t rpcid, crt_proc_cb_t in_proc_cb,
	   crt_proc_cb_t out_proc_cb, crt_hg_rpc_cb_t rpc_cb)
{
	hg_return_t hg_ret;
	int         rc = 0;

	C_ASSERT(hg_class != NULL);

	hg_ret = HG_Register(hg_class, rpcid, (hg_proc_cb_t)in_proc_cb,
			     (hg_proc_cb_t)out_proc_cb, rpc_cb);
	if (hg_ret != HG_SUCCESS) {
		C_ERROR("HG_Register(rpcid: 0x%x) failed, hg_ret: %d.\n",
			rpcid, hg_ret);
		rc = -CER_HG;
	}
	return rc;
}

static inline int
crt_hg_bulk_free(crt_bulk_t bulk_hdl)
{
	hg_return_t	hg_ret;
	int		rc = 0;

	hg_ret = HG_Bulk_free(bulk_hdl);
	if (hg_ret != HG_SUCCESS) {
		C_ERROR("HG_Bulk_free failed, hg_ret: %d.\n", hg_ret);
		rc = -CER_HG;
	}

	return rc;
}

static inline int
crt_hg_bulk_get_len(crt_bulk_t bulk_hdl, crt_size_t *bulk_len)
{
	hg_size_t	hg_size;

	C_ASSERT(bulk_len != NULL);
	hg_size = HG_Bulk_get_size(bulk_hdl);
	*bulk_len = hg_size;

	return 0;
}

static inline int
crt_hg_bulk_get_sgnum(crt_bulk_t bulk_hdl, unsigned int *bulk_sgnum)
{
	hg_uint32_t	hg_sgnum;

	C_ASSERT(bulk_sgnum != NULL);
	hg_sgnum = HG_Bulk_get_segment_count(bulk_hdl);
	*bulk_sgnum = hg_sgnum;

	return 0;
}

int crt_hg_bulk_create(struct crt_hg_context *hg_ctx, crt_sg_list_t *sgl,
		       crt_bulk_perm_t bulk_perm, crt_bulk_t *bulk_hdl);
int crt_hg_bulk_access(crt_bulk_t bulk_hdl, crt_sg_list_t *sgl);
int crt_hg_bulk_transfer(struct crt_bulk_desc *bulk_desc,
			 crt_bulk_cb_t complete_cb,
			 void *arg, crt_bulk_opid_t *opid);
static inline int
crt_hg_bulk_cancel(crt_bulk_opid_t opid)
{
	return HG_Bulk_cancel(opid);
}

#endif /* __CRT_MERCURY_H__ */
