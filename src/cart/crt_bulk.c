/*
 * (C) Copyright 2016-2022 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It implements the CaRT bulk related APIs.
 */
#define D_LOGFAC	DD_FAC(bulk)

#include "crt_internal.h"

/** Check the validation of the d_sg_list_t parameter */
static inline bool
crt_sgl_valid(d_sg_list_t *sgl)
{
	d_iov_t	*iov;
	int		i;

	if (sgl == NULL || sgl->sg_nr == 0) {
		if (sgl == NULL)
			D_ERROR("invalid parameter, NULL sgl.\n");
		else
			D_ERROR("invalid parameter, zero sgl.sg_nr.\n");
		return false;
	}

	/* HG_Bulk_create allows to pass in a NULL but_ptrs in which case HG
	 * will internally allocate memory, temporarily not use this feature.
	 */
	if (sgl->sg_iovs == NULL) {
		D_ERROR("invalid parameter, NULL sgl->sg_iovs.\n");
		return false;
	}
	for (i = 0; i < sgl->sg_nr; i++) {
		iov = &sgl->sg_iovs[i];
		if (iov->iov_buf == NULL || iov->iov_buf_len == 0) {
			if (iov->iov_buf == NULL)
				D_ERROR("invalid parameter, sg_iovs[%d]."
					"iov_buf is NULL.\n", i);
			else
				D_ERROR("invalid parameter, sg_iovs[%d]."
					"iov_buf_len is 0.\n", i);
			return false;
		}
	}
	return true;
}

/** check the validation of bulk descriptor */
static inline bool
crt_bulk_desc_valid(struct crt_bulk_desc *bulk_desc)
{
	if (bulk_desc == NULL || bulk_desc->bd_rpc == NULL ||
	    bulk_desc->bd_rpc->cr_ctx == CRT_CONTEXT_NULL ||
	    bulk_desc->bd_remote_hdl == CRT_BULK_NULL ||
	    bulk_desc->bd_local_hdl == CRT_BULK_NULL ||
	    (bulk_desc->bd_bulk_op != CRT_BULK_PUT &&
	     bulk_desc->bd_bulk_op != CRT_BULK_GET) ||
	    bulk_desc->bd_len == 0) {
		if (bulk_desc == NULL) {
			D_ERROR("invalid parameter, NULL bulk_desc.\n");
			return false;
		}
		if (bulk_desc->bd_rpc == NULL) {
			D_ERROR("invalid parameter, NULL bulk_desc->db_rpc.\n");
			return false;
		}
		if (bulk_desc->bd_rpc->cr_ctx == CRT_CONTEXT_NULL) {
			D_ERROR("invalid parameter, NULL bulk_desc->db_rpc->dr_ctx.\n");
			return false;
		}
		D_ERROR("invalid parameter, bulk_desc remote_hdl:%p,"
			"local_hdl:%p, bulk_op:%d, len: "DF_U64".\n",
			bulk_desc->bd_remote_hdl,
			bulk_desc->bd_local_hdl,
			bulk_desc->bd_bulk_op, bulk_desc->bd_len);
		return false;
	} else {
		return true;
	}
}

int
crt_bulk_create(crt_context_t crt_ctx, d_sg_list_t *sgl,
		crt_bulk_perm_t bulk_perm, crt_bulk_t *bulk_hdl)
{
	struct crt_context *ctx;
	struct crt_bulk    *ret_hdl  = NULL;
	int                 quota_rc = 0;
	int                 rc       = 0;

	if (crt_ctx == CRT_CONTEXT_NULL || !crt_sgl_valid(sgl) ||
	    (bulk_perm != CRT_BULK_RW && bulk_perm != CRT_BULK_RO && bulk_perm != CRT_BULK_WO) ||
	    bulk_hdl == NULL) {
		D_ERROR("invalid parameter, crt_ctx: %p, "
			"crt_sgl_valid: %d, bulk_perm: %d, bulk_hdl: %p.\n",
			crt_ctx, crt_sgl_valid(sgl), bulk_perm, bulk_hdl);
		D_GOTO(out, rc = -DER_INVAL);
	}

	ctx = crt_ctx;

	D_ALLOC_PTR(ret_hdl);
	if (ret_hdl == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	quota_rc = get_quota_resource(crt_ctx, CRT_QUOTA_BULKS);
	if (quota_rc == -DER_QUOTA_LIMIT) {
		D_DEBUG(DB_ALL, "Exceeded bulk limit, deferring bulk handle allocation\n");
		ret_hdl->bound       = false;
		ret_hdl->sgl         = *sgl;
		ret_hdl->bulk_perm   = bulk_perm;
		ret_hdl->hg_bulk_hdl = HG_BULK_NULL;
		ret_hdl->crt_ctx     = crt_ctx;
		ret_hdl->deferred    = true;
		D_GOTO(out, rc = DER_SUCCESS);
	}

	ret_hdl->deferred = false;
	ret_hdl->crt_ctx  = crt_ctx;

	rc = crt_hg_bulk_create(&ctx->cc_hg_ctx, sgl, bulk_perm, &ret_hdl->hg_bulk_hdl);
	if (rc != 0) {
		D_ERROR("crt_hg_bulk_create() failed, rc: " DF_RC "\n", DP_RC(rc));
		D_FREE(ret_hdl);
		D_GOTO(out, rc);
	}

out:
	if (rc == 0 && bulk_hdl)
		*bulk_hdl = ret_hdl;
	return rc;
}

int
crt_bulk_bind(crt_bulk_t crt_bulk, crt_context_t crt_ctx)
{
	struct crt_context *ctx  = crt_ctx;
	struct crt_bulk    *bulk = crt_bulk;
	int                 rc   = 0;

	if (ctx == NULL || bulk == NULL) {
		D_ERROR("invalid parameter, NULL crt_ctx or crt_bulk.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (bulk->deferred) {
		bulk->bound = true;
		D_GOTO(out, rc = DER_SUCCESS);
	}

	rc = crt_hg_bulk_bind(bulk->hg_bulk_hdl, &ctx->cc_hg_ctx);
	if (rc != 0) {
		D_ERROR("crt_hg_bulk_bind() failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}

out:
	return rc;
}

int
crt_bulk_addref(crt_bulk_t crt_bulk)
{
	struct crt_bulk *bulk = crt_bulk;
	int              rc   = -DER_SUCCESS;
	hg_return_t      hg_ret;

	if (bulk == NULL) {
		D_ERROR("invalid parameter, NULL bulk\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	hg_ret = HG_Bulk_ref_incr(bulk->hg_bulk_hdl);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Bulk_ref_incr failed, hg_ret: %d.\n", hg_ret);
		rc = crt_hgret_2_der(hg_ret);
	}

out:
	return rc;
}

int
crt_bulk_free(crt_bulk_t crt_bulk)
{
	struct crt_bulk *bulk = crt_bulk;
	int              rc   = -DER_SUCCESS;
	hg_return_t      hg_ret;

	if (bulk == NULL) {
		D_ERROR("invalid parameter, NULL bulk\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	/* This can happen if D_QUOTA_BULKS is enabled on a client */
	if (bulk->hg_bulk_hdl == HG_BULK_NULL) {
		if (bulk->deferred) {
			/* Treat as success */
			D_GOTO(out, rc = DER_SUCCESS);
		} else {
			D_ASSERTF(0, "Bulk handle should not be NULL\n");
		}
	}

	hg_ret = HG_Bulk_free(bulk->hg_bulk_hdl);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Bulk_free failed, hg_ret: %d.\n", hg_ret);
		rc = crt_hgret_2_der(hg_ret);
	}

	/* decoded bulks are not counted towards quota; such bulks have crt_ctx set to NULL */
	if (bulk->crt_ctx)
		put_quota_resource(bulk->crt_ctx, CRT_QUOTA_BULKS);
out:
	D_FREE(bulk);
	return rc;
}

int
crt_bulk_transfer(struct crt_bulk_desc *bulk_desc, crt_bulk_cb_t complete_cb,
		  void *arg, crt_bulk_opid_t *opid)
{
	int			rc = 0;

	if (!crt_bulk_desc_valid(bulk_desc)) {
		D_ERROR("invalid parameter of bulk_desc.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = crt_hg_bulk_transfer(bulk_desc, complete_cb, arg, opid, false);
	if (rc != 0)
		DL_ERROR(rc, "crt_hg_bulk_transfer() failed");

out:
	return rc;
}

int
crt_bulk_bind_transfer(struct crt_bulk_desc *bulk_desc,
		       crt_bulk_cb_t complete_cb, void *arg,
		       crt_bulk_opid_t *opid)
{
	int			rc = 0;

	if (!crt_bulk_desc_valid(bulk_desc)) {
		D_ERROR("invalid parameter, bulk_desc not valid.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = crt_hg_bulk_transfer(bulk_desc, complete_cb, arg, opid, true);
	if (rc != 0)
		D_ERROR("crt_hg_bulk_transfer() failed, rc: %d.\n", rc);

out:
	return rc;
}

int
crt_bulk_get_len(crt_bulk_t crt_bulk, size_t *bulk_len)
{
	struct crt_bulk *bulk = crt_bulk;

	if (bulk_len == NULL) {
		D_ERROR("bulk_len is NULL\n");
		return -DER_INVAL;
	}

	if (bulk == NULL) {
		D_ERROR("bulk is NULL\n");
		return -DER_INVAL;
	}

	if (bulk->deferred)
		return -DER_NOTSUPPORTED;

	*bulk_len = crt_hg_bulk_get_len(bulk->hg_bulk_hdl);
	return 0;
}

int
crt_bulk_get_sgnum(crt_bulk_t crt_bulk, unsigned int *bulk_sgnum)
{
	struct crt_bulk *bulk = crt_bulk;

	D_ASSERT(bulk_sgnum != NULL);
	D_ASSERT(bulk != NULL);

	if (bulk->deferred)
		return -DER_NOTSUPPORTED;

	*bulk_sgnum = crt_hg_bulk_get_sgnum(bulk->hg_bulk_hdl);
	return 0;
}

int
crt_bulk_access(crt_bulk_t crt_bulk, d_sg_list_t *sgl)
{
	struct crt_bulk *bulk = crt_bulk;
	int              rc   = 0;

	if (bulk == NULL) {
		D_ERROR("invalid parameter, NULL bulk.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (sgl == NULL) {
		D_ERROR("invalid parameter, NULL sgl pointer.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (bulk->deferred)
		*sgl = bulk->sgl;
	else
		rc = crt_hg_bulk_access(bulk->hg_bulk_hdl, sgl);

out:
	return rc;
}

int
crt_bulk_abort(crt_context_t crt_ctx, crt_bulk_opid_t opid)
{
	return HG_Bulk_cancel(opid);
}
