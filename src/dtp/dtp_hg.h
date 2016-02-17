/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * This file is part of daos_transport. It is the header file of bridging to
 * mercury.
 */
#ifndef __DTP_MERCURY_H__
#define __DTP_MERCURY_H__

#include <daos/daos_list.h>

#include <mercury.h>
#include <mercury_types.h>
#include <mercury_macros.h>
#include <mercury_proc.h>
#include <mercury_proc_string.h>
#include <na.h>

#include <dtp_internal_types.h>

/**
 * Basic approach for core affinity handling:
 * 1) DTP layer can create different affinity contexts (dtp_context_t, for
 *    example one affinity context for one NUMA node),
 * 2) DAOS transport associates different hg_context for different dtp_context_t
 * 3) RPC server (mercury server) internally dispatches different peers to
 *    different affinity context, maybe by hashing peers ID or by a user
 *    registered callback. Using NUMA allocator for memory allocation.
 * 4) Within DAOS, the calling context of dtp_progress() should consider the low
 *    layer's affinity context, and call the dpt_progress for different context.
 *
 * Notes: current HG data structures of hg_class/hg_context/na_class/na_context
 *        and internal handling need refactoring for core affinity.
 */
struct dtp_hg_context {
	daos_list_t		dhc_link; /* link to gdata.dg_ctx_list */
	na_class_t		*dhc_nacla; /* NA class */
	na_context_t		*dhc_nactx; /* NA context */
	hg_class_t		*dhc_hgcla; /* HG class */
	hg_context_t		*dhc_hgctx; /* HG context */
};

struct dtp_hg_gdata {
	na_class_t		*dhg_nacla; /* NA class */
	na_context_t		*dhg_nactx; /* NA context */
	hg_class_t		*dhg_hgcla; /* HG class */
};

struct dtp_rpc_priv {
	daos_list_t		drp_link; /* link to sent_list */
	dtp_rpc_t		drp_pub; /* public part */
	struct dtp_common_hdr	drp_req_hdr; /* common header for request */
	struct dtp_common_hdr	drp_reply_hdr; /* common header for reply */
	dtp_rpc_state_t		drp_state; /* RPC state */
	hg_handle_t		drp_hg_hdl;
	na_addr_t		drp_na_addr;
	uint32_t		drp_srv:1, /* flag of server received request */
				drp_output_got:1,
				drp_input_got:1;
	uint32_t		drp_refcount;
	pthread_spinlock_t	drp_lock;
};

int dtp_hg_init(const char *info_string, bool server);
int dtp_hg_fini();
int dtp_hg_ctx_init(struct dtp_hg_context *hg_ctx);
int dtp_hg_ctx_fini(struct dtp_hg_context *hg_ctx);
int dtp_hg_req_create(struct dtp_hg_context *hg_ctx, dtp_endpoint_t tgt_ep,
		      struct dtp_rpc_priv *rpc_priv);
int dtp_hg_req_destroy(struct dtp_rpc_priv *rpc_priv);
int dtp_hg_req_send(struct dtp_rpc_priv *rpc_priv, dtp_cb_t complete_cb,
		    void *arg);
int dtp_hg_reply_send(struct dtp_rpc_priv *rpc_priv, dtp_cb_t complete_cb,
		      void *arg);
int dtp_hg_progress(struct dtp_hg_context *hg_ctx, unsigned int timeout);

static inline struct dtp_hg_context *
dtp_hg_context_lookup(hg_context_t *ctx)
{
	struct dtp_hg_context	*dtp_hg_ctx = NULL;
	int			found = 0;

	pthread_rwlock_rdlock(&dtp_gdata.dg_rwlock);

	daos_list_for_each_entry(dtp_hg_ctx, &dtp_gdata.dg_ctx_list, dhc_link) {
		if (dtp_hg_ctx->dhc_hgctx == ctx) {
			found = 1;
			break;
		}
	}

	pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);

	return (found == 1) ? dtp_hg_ctx : NULL;
}

static inline void
dtp_rpc_priv_init(struct dtp_rpc_priv *rpc_priv, dtp_context_t dtp_ctx,
		  dtp_opcode_t opc, int srv_flag)
{
	D_ASSERT(rpc_priv != NULL);
	DAOS_INIT_LIST_HEAD(&rpc_priv->drp_link);
	dtp_common_hdr_init(&rpc_priv->drp_req_hdr, opc);
	dtp_common_hdr_init(&rpc_priv->drp_reply_hdr, opc);
	rpc_priv->drp_state = RPC_INITED;
	rpc_priv->drp_srv = (srv_flag != 0);
	rpc_priv->drp_refcount = 0;
	pthread_spin_init(&rpc_priv->drp_lock, PTHREAD_PROCESS_PRIVATE);

	rpc_priv->drp_pub.dr_opc = opc;
	rpc_priv->drp_pub.dr_ctx = dtp_ctx;
}

static inline int
dtp_rpc_inout_buff_init(dtp_rpc_t *rpc_pub, daos_size_t input_size,
			daos_size_t output_size)
{
	int	rc = 0;

	D_ASSERT(rpc_pub != NULL);
	D_ASSERT(input_size <= DTP_MAX_INPUT_SIZE &&
		 output_size <= DTP_MAX_OUTPUT_SIZE);

	if (input_size > 0) {
		D_ALLOC(rpc_pub->dr_input, input_size);
		if (rpc_pub->dr_input == NULL) {
			D_ERROR("cannot allocate memory(size "DF_U64") for "
				"dr_input.\n", input_size);
			D_GOTO(out, rc = -DER_NOMEM);
		}
		rpc_pub->dr_input_size = input_size;
	}

	if (output_size > 0) {
		D_ALLOC(rpc_pub->dr_output, output_size);
		if (rpc_pub->dr_output == NULL) {
			D_ERROR("cannot allocate memory(size "DF_U64") for "
				"dr_output.\n", output_size);
			D_FREE(rpc_pub->dr_input, rpc_pub->dr_input_size);
			D_GOTO(out, rc = -DER_NOMEM);
		}
		rpc_pub->dr_output_size = output_size;
	}

out:
	return rc;
}

static inline void
dtp_rpc_inout_buff_fini(dtp_rpc_t *rpc_pub)
{
	D_ASSERT(rpc_pub != NULL);

	if (rpc_pub->dr_input != NULL) {
		D_ASSERT(rpc_pub->dr_input_size != 0);
		D_FREE(rpc_pub->dr_input, rpc_pub->dr_input_size);
		rpc_pub->dr_input_size = 0;
	}
	if (rpc_pub->dr_output != NULL) {
		D_ASSERT(rpc_pub->dr_output_size != 0);
		D_FREE(rpc_pub->dr_output, rpc_pub->dr_output_size);
		rpc_pub->dr_output_size = 0;
	}
}

#define DTP_PROC_NULL (NULL)

static inline int
dtp_proc_common_hdr(dtp_proc_t proc, struct dtp_common_hdr *hdr)
{
	hg_proc_t     hg_proc;
	hg_return_t   hg_ret = HG_SUCCESS;
	int           rc = 0;

	if (proc == DTP_PROC_NULL || hdr == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	hg_proc = proc;
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_magic);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_version);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_opc);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_cksum);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_flags);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	/*
	D_DEBUG(DF_TP,"in dtp_proc_common_hdr, opc: 0x%x.\n", hdr->dch_opc);
	*/

	/* proc the 3 paddings */
	hg_ret = hg_proc_memcpy(hg_proc, &hdr->dch_padding[0],
				3 * sizeof(uint32_t));
	if (hg_ret != HG_SUCCESS)
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);

out:
	return rc;
}

/* NB: caller should pass in &rpc_pub->dr_input as the \param data */
static inline int
dtp_proc_in_common(dtp_proc_t proc, dtp_rpc_input_t *data)
{
	struct dtp_rpc_priv	*rpc_priv;
	struct dtp_opc_info	*opc_info = NULL;
	int			rc = 0;

	if (proc == DTP_PROC_NULL)
		D_GOTO(out, rc = -DER_INVAL);

	D_ASSERT(data != NULL);
	rpc_priv = container_of(data, struct dtp_rpc_priv, drp_pub.dr_input);
	D_ASSERT(rpc_priv != NULL);

	/* D_DEBUG(DF_TP,"in dtp_proc_in_common, data: %p\n", *data); */

	rc = dtp_proc_common_hdr(proc, &rpc_priv->drp_req_hdr);
	if (rc != 0) {
		D_ERROR("dtp_proc_common_hdr failed rc: %d.\n", rc);
		D_GOTO(out, rc);
	}

	if (*data == NULL) {
		/*
		D_DEBUG(DF_TP,"dtp_proc_in_common, opc: 0x%x, NULL input.\n",
			rpc_priv->drp_req_hdr.dch_opc);
		*/
		D_GOTO(out, rc);
	}

	opc_info = dtp_opc_lookup(dtp_gdata.dg_opc_map,
				  rpc_priv->drp_req_hdr.dch_opc, DTP_UNLOCK);
	if (opc_info == NULL) {
		D_ERROR("opc: 0x%x, lookup failed.\n",
			rpc_priv->drp_req_hdr.dch_opc);
		D_GOTO(out, rc = -DER_DTP_UNREG);
	}

	if (opc_info->doi_inproc_cb != NULL)
		rc = opc_info->doi_inproc_cb(proc, *data);

out:
	return rc;
}

/* NB: caller should pass in &rpc_pub->dr_output as the \param data */
static inline int
dtp_proc_out_common(dtp_proc_t proc, dtp_rpc_output_t *data)
{
	struct dtp_rpc_priv	*rpc_priv;
	struct dtp_opc_info	*opc_info = NULL;
	int			rc = 0;

	if (proc == DTP_PROC_NULL)
		D_GOTO(out, rc = -DER_INVAL);

	D_ASSERT(data != NULL);
	rpc_priv = container_of(data, struct dtp_rpc_priv, drp_pub.dr_output);
	D_ASSERT(rpc_priv != NULL);

	/* D_DEBUG(DF_TP,"in dtp_proc_out_common, data: %p\n", *data); */

	rc = dtp_proc_common_hdr(proc, &rpc_priv->drp_reply_hdr);
	if (rc != 0) {
		D_ERROR("dtp_proc_common_hdr failed rc: %d.\n", rc);
		D_GOTO(out, rc);
	}

	if (*data == NULL) {
		/*
		D_DEBUG(DF_TP,"dtp_proc_out_common, opc: 0x%x, NULL output.\n",
			rpc_priv->drp_req_hdr.dch_opc);
		*/
		D_GOTO(out, rc);
	}

	opc_info = dtp_opc_lookup(dtp_gdata.dg_opc_map,
				  rpc_priv->drp_reply_hdr.dch_opc, DTP_UNLOCK);
	if (opc_info == NULL) {
		D_ERROR("opc: 0x%x, lookup failed.\n",
			rpc_priv->drp_reply_hdr.dch_opc);
		D_GOTO(out, rc = -DER_DTP_UNREG);
	}

	if (opc_info->doi_outproc_cb != NULL)
		rc = opc_info->doi_outproc_cb(proc, *data);

out:
	return rc;
}

int dtp_rpc_handler_common(hg_handle_t hg_hdl);

typedef hg_rpc_cb_t dtp_hg_rpc_cb_t;

static inline int
dtp_hg_reg(dtp_opcode_t opc, dtp_proc_cb_t in_proc_cb,
	   dtp_proc_cb_t out_proc_cb, dtp_hg_rpc_cb_t rpc_cb)
{
	hg_return_t hg_ret;
	int         rc = 0;

	hg_ret = HG_Register(dtp_gdata.dg_hg->dhg_hgcla, opc,
			     (hg_proc_cb_t)in_proc_cb,
			     (hg_proc_cb_t)out_proc_cb, rpc_cb);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Register(opc: 0x%x) failed, hg_ret: %d.\n",
			opc, hg_ret);
		rc = -DER_DTP_HG;
	}
	return rc;
}


/* only-for-testing */
extern na_addr_t na_addr_test;


#endif /* __DTP_MERCURY_H__ */
