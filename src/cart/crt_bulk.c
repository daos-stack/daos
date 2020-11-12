/*
 * (C) Copyright 2016-2020 Intel Corporation.
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
	struct crt_context	*ctx;
	int			rc = 0;

	if (crt_ctx == CRT_CONTEXT_NULL || !crt_sgl_valid(sgl) ||
	    /* Now HG treats WO as invalid parameter */
	    (bulk_perm != CRT_BULK_RW && bulk_perm != CRT_BULK_RO /* &&
	     bulk_perm != CRT_BULK_WO */) || bulk_hdl == NULL) {
		D_ERROR("invalid parameter, crt_ctx: %p, "
			"crt_sgl_valid: %d, bulk_perm: %d, bulk_hdl: %p.\n",
			crt_ctx, crt_sgl_valid(sgl), bulk_perm, bulk_hdl);
		D_GOTO(out, rc = -DER_INVAL);
	}

	ctx = crt_ctx;
	rc = crt_hg_bulk_create(&ctx->cc_hg_ctx, sgl, bulk_perm, bulk_hdl);
	if (rc != 0)
		D_ERROR("crt_hg_bulk_create() failed, rc: " DF_RC "\n", DP_RC(rc));

out:
	return rc;
}

int
crt_bulk_bind(crt_bulk_t bulk_hdl, crt_context_t crt_ctx)
{
	struct crt_context	*ctx = crt_ctx;
	int			rc = 0;

	if (ctx == CRT_CONTEXT_NULL || bulk_hdl == CRT_BULK_NULL) {
		D_ERROR("invalid parameter, NULL crt_ctx or bulk_hdl.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = crt_hg_bulk_bind(bulk_hdl, &ctx->cc_hg_ctx);
	if (rc != 0)
		D_ERROR("crt_hg_bulk_bind() failed, rc: %d.\n", rc);

out:
	return rc;
}

int
crt_bulk_addref(crt_bulk_t bulk_hdl)
{
	int	rc;

	if (bulk_hdl == CRT_BULK_NULL) {
		D_ERROR("invalid parameter, NULL bulk_hdl.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = crt_hg_bulk_addref(bulk_hdl);
	if (rc != 0)
		D_ERROR("crt_hg_bulk_addref() failed, rc: %d.\n", rc);

out:
	return rc;
}

int
crt_bulk_free(crt_bulk_t bulk_hdl)
{
	int	rc;

	if (bulk_hdl == CRT_BULK_NULL) {
		D_ERROR("invalid parameter, NULL bulk_hdl.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = crt_hg_bulk_free(bulk_hdl);
	if (rc != 0)
		D_ERROR("crt_hg_bulk_free() failed, rc: %d.\n", rc);

out:
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
		D_ERROR("crt_hg_bulk_transfer() failed, rc: %d.\n", rc);

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
crt_bulk_get_len(crt_bulk_t bulk_hdl, size_t *bulk_len)
{
	return crt_hg_bulk_get_len(bulk_hdl, bulk_len);
}

int
crt_bulk_get_sgnum(crt_bulk_t bulk_hdl, unsigned int *bulk_sgnum)
{
	return crt_hg_bulk_get_sgnum(bulk_hdl, bulk_sgnum);
}

int
crt_bulk_access(crt_bulk_t bulk_hdl, d_sg_list_t *sgl)
{
	int		rc = 0;

	if (bulk_hdl == CRT_BULK_NULL) {
		D_ERROR("invalid parameter, NULL bulk_hdl.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (sgl == NULL) {
		D_ERROR("invalid parameter, NULL sgl pointer.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = crt_hg_bulk_access(bulk_hdl, sgl);

out:
	return rc;
}

int
crt_bulk_abort(crt_context_t crt_ctx, crt_bulk_opid_t opid)
{
	return crt_hg_bulk_cancel(opid);
}
