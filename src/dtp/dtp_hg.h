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

#include <daos/list.h>

#include <mercury.h>
#include <mercury_types.h>
#include <mercury_macros.h>
#include <mercury_proc.h>
#include <mercury_proc_string.h>
#include <na.h>
#include <na_error.h>

#include <dtp_internal_types.h>

/* change to 0 to disable the low-level unpack */
#define DTP_HG_LOWLEVEL_UNPACK	(1)

/* the shared HG RPC ID used for all DAOS opc */
#define DTP_HG_RPCID	(0xDA036868)

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
	int			dhc_idx; /* context index */
	bool			dhc_shared_na; /* flag for shared na_class */
	na_class_t		*dhc_nacla; /* NA class */
	na_context_t		*dhc_nactx; /* NA context */
	hg_class_t		*dhc_hgcla; /* HG class */
	hg_context_t		*dhc_hgctx; /* HG context */
	hg_class_t		*dhc_bulkcla; /* bulk class */
	hg_context_t		*dhc_bulkctx; /* bulk context */
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
	struct dtp_opc_info	*drp_opc_info;
};

int dtp_hg_init(dtp_phy_addr_t *addr, bool server);
int dtp_hg_fini();
int dtp_hg_ctx_init(struct dtp_hg_context *hg_ctx, int idx);
int dtp_hg_ctx_fini(struct dtp_hg_context *hg_ctx);
int dtp_hg_req_create(struct dtp_hg_context *hg_ctx, dtp_endpoint_t tgt_ep,
		      struct dtp_rpc_priv *rpc_priv);
int dtp_hg_req_destroy(struct dtp_rpc_priv *rpc_priv);
int dtp_hg_req_send(struct dtp_rpc_priv *rpc_priv, dtp_cb_t complete_cb,
		    void *arg);
int dtp_hg_reply_send(struct dtp_rpc_priv *rpc_priv);
int dtp_hg_progress(struct dtp_hg_context *hg_ctx, int64_t timeout);

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
	/* initialize as 1, so user can cal dtp_req_decref to destroy new req */
	rpc_priv->drp_refcount = 1;
	pthread_spin_init(&rpc_priv->drp_lock, PTHREAD_PROCESS_PRIVATE);

	rpc_priv->drp_pub.dr_opc = opc;
	rpc_priv->drp_pub.dr_ctx = dtp_ctx;
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

static inline int
dtp_rpc_inout_buff_init(dtp_rpc_t *rpc_pub)
{
	struct dtp_rpc_priv	*rpc_priv;
	struct dtp_opc_info	*opc_info;
	int			rc = 0;

	D_ASSERT(rpc_pub != NULL);
	D_ASSERT(rpc_pub->dr_input == NULL);
	D_ASSERT(rpc_pub->dr_output == NULL);
	rpc_priv = container_of(rpc_pub, struct dtp_rpc_priv, drp_pub);
	opc_info = rpc_priv->drp_opc_info;
	D_ASSERT(opc_info != NULL);

	if (opc_info->doi_input_size > 0) {
		D_ALLOC(rpc_pub->dr_input, opc_info->doi_input_size);
		if (rpc_pub->dr_input == NULL) {
			D_ERROR("cannot allocate memory(size "DF_U64") for "
				"dr_input.\n", opc_info->doi_input_size);
			D_GOTO(out, rc = -DER_NOMEM);
		}
		rpc_pub->dr_input_size = opc_info->doi_input_size;
	}
	if (opc_info->doi_output_size > 0) {
		D_ALLOC(rpc_pub->dr_output, opc_info->doi_output_size);
		if (rpc_pub->dr_output == NULL) {
			D_ERROR("cannot allocate memory(size "DF_U64") for "
				"dr_putput.\n", opc_info->doi_input_size);
			D_GOTO(out, rc = -DER_NOMEM);
		}
		rpc_pub->dr_output_size = opc_info->doi_output_size;
	}

out:
	if (rc < 0)
		dtp_rpc_inout_buff_fini(rpc_pub);
	return rc;
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
	hg_ret = dtp_proc_uuid_t(hg_proc, &hdr->dch_grp_id);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_rank);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	/*
	D_DEBUG(DF_TP,"in dtp_proc_common_hdr, opc: 0x%x.\n", hdr->dch_opc);
	*/

	/* proc the 2 paddings */
	hg_ret = hg_proc_memcpy(hg_proc, &hdr->dch_padding[0],
				2 * sizeof(uint32_t));
	if (hg_ret != HG_SUCCESS)
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);

out:
	return rc;
}

/* For unpacking only the common header to know about the DAOS opc */
static inline int
dtp_hg_unpack_header(struct dtp_rpc_priv *rpc_priv, dtp_proc_t *proc)
{
	int	rc = 0;

#if DTP_HG_LOWLEVEL_UNPACK
	/*
	 * Use some low level HG APIs to unpack header first and then unpack the
	 * body, avoid unpacking two times (which needs to lookup, create the
	 * proc multiple times).
	 * The potential risk is mercury possibly will not export those APIs
	 * later, and the hard-coded method HG_CRC64 used below which maybe
	 * different with future's mercury code change.
	 */
	void			*in_buf;
	hg_size_t		in_buf_size;
	hg_return_t		hg_ret = HG_SUCCESS;
	hg_handle_t		handle;
	hg_class_t		*hg_class;
	struct dtp_hg_context	*hg_ctx;
	hg_proc_t		hg_proc = HG_PROC_NULL;

	/* Get input buffer */
	handle = rpc_priv->drp_hg_hdl;
	hg_ret = HG_Core_get_input(handle, &in_buf, &in_buf_size);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("Could not get input buffer, hg_ret: %d.", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	/* Create a new decoding proc */
	hg_ctx = (struct dtp_hg_context *)(rpc_priv->drp_pub.dr_ctx);
	hg_class = hg_ctx->dhc_hgcla;
	hg_ret = hg_proc_create(hg_class, in_buf, in_buf_size, HG_DECODE,
				HG_CRC64, &hg_proc);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("Could not create proc, hg_ret: %d.", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	/* Decode header */
	rc = dtp_proc_common_hdr(hg_proc, &rpc_priv->drp_req_hdr);
	if (rc != 0)
		D_ERROR("dtp_proc_common_hdr failed rc: %d.\n", rc);

	*proc = hg_proc;
out:
	return rc;
#else
	/*
	 * In the case that if mercury does not export the HG_Core_xxx APIs,
	 * we can only use the HG_Get_input to unpack the header which indeed
	 * will cause the unpacking twice as later we still need to unpack the
	 * body.
	 *
	 * Notes: as here we only unpack DAOS common header and not finish
	 * the HG_Get_input() procedure, so for mercury need to turn off the
	 * checksum compiling option (-DMERCURY_USE_CHECKSUMS=OFF), or mercury
	 * will report checksum mismatch in the call of HG_Get_input.
	 */
	void		*hg_in_struct;
	hg_return_t	hg_ret = HG_SUCCESS;

	D_ASSERT(rpc_priv != NULL && proc != NULL);
	D_ASSERT(rpc_priv->drp_pub.dr_input == NULL);

	hg_in_struct = &rpc_priv->drp_pub.dr_input;
	hg_ret = HG_Get_input(rpc_priv->drp_hg_hdl, hg_in_struct);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Get_input failed, hg_ret: %d.\n", hg_ret);
		rc = -DER_DTP_HG;
	}

	return rc;
#endif
}

static inline void
dtp_hg_unpack_cleanup(dtp_proc_t proc)
{
#if DTP_HG_LOWLEVEL_UNPACK
	if (proc != HG_PROC_NULL)
		hg_proc_free(proc);
#endif
}

static inline int
dtp_proc_internal(struct drf_field *drf,
			 dtp_proc_t proc, void *data)
{
	int rc = 0;
	void *ptr = data;
	int i;
	int j;

	for (i = 0; i < drf->drf_count; i++) {
		if (drf->drf_msg[i]->dmf_flags & DMF_ARRAY_FLAG) {
			struct dtp_array *array = ptr;
			hg_proc_op_t	 proc_op;
			hg_return_t	 hg_ret;
			void		*array_ptr;

			/* retrieve the count of array first */
			hg_ret = hg_proc_hg_uint64_t(proc, &array->count);
			if (hg_ret != HG_SUCCESS) {
				rc = -DER_DTP_HG;
				break;
			}

			/* Let's assume array is not zero size now */
			D_ASSERT(array->count > 0);
			proc_op = hg_proc_get_op(proc);
			if (proc_op == HG_DECODE) {
				D_ALLOC(array->arrays,
				     array->count * drf->drf_msg[i]->dmf_size);
				if (array->arrays == NULL) {
					rc = -DER_NOMEM;
					break;
				}
			}
			array_ptr = array->arrays;
			for (j = 0; j < array->count; j++) {
				rc = drf->drf_msg[i]->dmf_proc(proc, array_ptr);
				if (rc != 0)
					break;

				array_ptr = (char *)array_ptr +
					    drf->drf_msg[j]->dmf_size;
			}

			if (proc_op == HG_FREE) {
				D_FREE(array->arrays,
				     array->count * drf->drf_msg[i]->dmf_size);
			}
			ptr = (char *)ptr + sizeof(struct dtp_array);
		} else {
			rc = drf->drf_msg[i]->dmf_proc(proc, ptr);

			ptr = (char *)ptr + drf->drf_msg[i]->dmf_size;
		}

		if (rc < 0)
			break;
	}

	return rc;
}

static inline int
dtp_proc_input(struct dtp_rpc_priv *rpc_priv,
		      dtp_proc_t proc)
{
	struct dtp_req_format *drf = rpc_priv->drp_opc_info->doi_drf;

	D_ASSERT(drf != NULL);
	return dtp_proc_internal(&drf->drf_fields[DTP_IN],
				 proc, rpc_priv->drp_pub.dr_input);
}

static inline int
dtp_proc_output(struct dtp_rpc_priv *rpc_priv,
		       dtp_proc_t proc)
{
	struct dtp_req_format *drf = rpc_priv->drp_opc_info->doi_drf;

	D_ASSERT(drf != NULL);
	return dtp_proc_internal(&drf->drf_fields[DTP_OUT],
				 proc, rpc_priv->drp_pub.dr_output);
}

static inline int
dtp_hg_unpack_body(struct dtp_rpc_priv *rpc_priv, dtp_proc_t proc)
{
	int	rc = 0;

#if DTP_HG_LOWLEVEL_UNPACK
	hg_return_t	hg_ret;

	D_ASSERT(rpc_priv != NULL && proc != HG_PROC_NULL);

	/* Decode input parameters */
	rc = dtp_proc_input(rpc_priv, proc);
	if (rc != 0) {
		D_ERROR("dtp_hg_unpack_body failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_priv->drp_pub.dr_opc);
		D_GOTO(out, rc);
	}

	/* Flush proc */
	hg_ret = hg_proc_flush(proc);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("Error in proc flush, hg_ret: %d, opc: 0x%x.",
			hg_ret, rpc_priv->drp_pub.dr_opc);
		D_GOTO(out, rc);
	}
out:
	dtp_hg_unpack_cleanup(proc);

#else
	void		*hg_in_struct;
	hg_return_t	hg_ret = HG_SUCCESS;

	D_ASSERT(rpc_priv != NULL);
	D_ASSERT(rpc_priv->drp_pub.dr_input != NULL);

	hg_in_struct = &rpc_priv->drp_pub.dr_input;
	hg_ret = HG_Get_input(rpc_priv->drp_hg_hdl, hg_in_struct);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Get_input failed, hg_ret: %d.\n", hg_ret);
		rc = -DER_DTP_HG;
	}
#endif
	return rc;
}

/* NB: caller should pass in &rpc_pub->dr_input as the \param data */
static inline int
dtp_proc_in_common(dtp_proc_t proc, dtp_rpc_input_t *data)
{
	struct dtp_rpc_priv	*rpc_priv;
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

	rc = dtp_proc_input(rpc_priv, proc);
	if (rc != 0) {
		D_ERROR("unpack input fails for opc: %s\n",
			rpc_priv->drp_opc_info->doi_drf->drf_name);
		D_GOTO(out, rc);
	}
out:
	return rc;
}

/* NB: caller should pass in &rpc_pub->dr_output as the \param data */
static inline int
dtp_proc_out_common(dtp_proc_t proc, dtp_rpc_output_t *data)
{
	struct dtp_rpc_priv	*rpc_priv;
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

	rc = dtp_proc_output(rpc_priv, proc);
out:
	return rc;
}

int dtp_rpc_handler_common(hg_handle_t hg_hdl);

typedef hg_rpc_cb_t dtp_hg_rpc_cb_t;

static inline int
dtp_hg_reg(hg_class_t *hg_class, hg_id_t rpcid, dtp_proc_cb_t in_proc_cb,
	   dtp_proc_cb_t out_proc_cb, dtp_hg_rpc_cb_t rpc_cb)
{
	hg_return_t hg_ret;
	int         rc = 0;

	D_ASSERT(hg_class != NULL);

	hg_ret = HG_Register(hg_class, rpcid, (hg_proc_cb_t)in_proc_cb,
			     (hg_proc_cb_t)out_proc_cb, rpc_cb);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Register(rpcid: 0x%x) failed, hg_ret: %d.\n",
			rpcid, hg_ret);
		rc = -DER_DTP_HG;
	}
	return rc;
}

static inline int
dtp_hg_bulk_free(dtp_bulk_t bulk_hdl)
{
	hg_return_t	hg_ret;
	int		rc = 0;

	hg_ret = HG_Bulk_free(bulk_hdl);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Bulk_free failed, hg_ret: %d.\n", hg_ret);
		rc = -DER_DTP_HG;
	}

	return rc;
}

static inline int
dtp_hg_bulk_get_len(dtp_bulk_t bulk_hdl, daos_size_t *bulk_len)
{
	hg_size_t	hg_size;

	D_ASSERT(bulk_len != NULL);
	hg_size = HG_Bulk_get_size(bulk_hdl);
	*bulk_len = hg_size;

	return 0;
}

static inline int
dtp_hg_bulk_get_sgnum(dtp_bulk_t bulk_hdl, unsigned int *bulk_sgnum)
{
	hg_uint32_t	hg_sgnum;

	D_ASSERT(bulk_sgnum != NULL);
	hg_sgnum = HG_Bulk_get_segment_count(bulk_hdl);
	*bulk_sgnum = hg_sgnum;

	return 0;
}

int dtp_hg_bulk_create(struct dtp_hg_context *hg_ctx, daos_sg_list_t *sgl,
		       dtp_bulk_perm_t bulk_perm, dtp_bulk_t *bulk_hdl);
int dtp_hg_bulk_transfer(struct dtp_bulk_desc *bulk_desc,
			 dtp_bulk_cb_t complete_cb,
			 void *arg, dtp_bulk_opid_t *opid);
#endif /* __DTP_MERCURY_H__ */
