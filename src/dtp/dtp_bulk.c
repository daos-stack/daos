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
 * This file is part of daos_transport. It implements the dtp bulk related APIs.
 */

#include <dtp_internal.h>

/** Check the validation of the daos_sg_list_t parameter */
static inline bool
dtp_sgl_valid(daos_sg_list_t *sgl)
{
	daos_iov_t	*iov;
	int		i;

	if (sgl == NULL || sgl->sg_nr.num == 0)
		return false;

	/* HG_Bulk_create allows to pass in a NULL but_ptrs in which case HG
	 * will internally allocate memory, temporarily not use this feature. */
	if (sgl->sg_iovs == NULL)
		return false;
	for (i = 0; i < sgl->sg_nr.num; i++) {
		iov = &sgl->sg_iovs[i];
		if (iov->iov_buf == NULL || iov->iov_buf_len == 0)
			return false;
	}
	return true;
}

/** check the validation of bulk descriptor */
static inline bool
dtp_bulk_desc_valid(struct dtp_bulk_desc *bulk_desc)
{
	if (bulk_desc == NULL || bulk_desc->bd_rpc == NULL ||
	    bulk_desc->bd_rpc->dr_ctx == DTP_CONTEXT_NULL ||
	    bulk_desc->bd_remote_hdl == DTP_BULK_NULL ||
	    bulk_desc->bd_local_hdl == DTP_BULK_NULL ||
	    (bulk_desc->bd_bulk_op != DTP_BULK_PUT &&
	     bulk_desc->bd_bulk_op != DTP_BULK_GET) ||
	    bulk_desc->bd_len == 0) {
		if (bulk_desc == NULL) {
			D_ERROR("invalid parameter of NULL bulk_desc.\n");
			return false;
		}
		if (bulk_desc->bd_rpc == NULL) {
			D_ERROR("invalid parameter(NULL bulk_desc->db_rpc).\n");
			return false;
		}
		if (bulk_desc->bd_rpc->dr_ctx == DTP_CONTEXT_NULL) {
			D_ERROR("invalid parameter(NULL bulk_desc->db_rpc"
				"->dr_ctx).\n");
			return false;
		}
		D_ERROR("invalid parameter of bulk_desc (remote_hdl:%p,"
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
dtp_bulk_create(dtp_context_t dtp_ctx, daos_sg_list_t *sgl,
		dtp_bulk_perm_t bulk_perm, dtp_bulk_t *bulk_hdl)
{
	struct dtp_hg_context	*hg_ctx;
	int			rc = 0;

	if (dtp_ctx == DTP_CONTEXT_NULL || !dtp_sgl_valid(sgl) ||
	    /* Now HG treats WO as invalid parameter */
	    (bulk_perm != DTP_BULK_RW && bulk_perm != DTP_BULK_RO /* &&
	     bulk_perm != DTP_BULK_WO */) || bulk_hdl == NULL) {
		D_ERROR("invalid parameter for dtp_bulk_create.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	hg_ctx = (struct dtp_hg_context *)dtp_ctx;
	rc = dtp_hg_bulk_create(hg_ctx, sgl, bulk_perm, bulk_hdl);
	if (rc != 0)
		D_ERROR("dtp_hg_bulk_create failed, rc: %d.\n", rc);

out:
	return rc;
}

int
dtp_bulk_free(dtp_bulk_t bulk_hdl)
{
	int	rc = 0;

	if (bulk_hdl == DTP_BULK_NULL) {
		D_DEBUG(DF_TP, "dtp_bulk_free with NULL bulk_hdl.\n");
		D_GOTO(out, rc);
	}

	rc = dtp_hg_bulk_free(bulk_hdl);
	if (rc != 0)
		D_ERROR("dtp_hg_bulk_free failed, rc: %d.\n", rc);

out:
	return rc;
}

int
dtp_bulk_transfer(struct dtp_bulk_desc *bulk_desc, dtp_bulk_cb_t complete_cb,
		  void *arg, dtp_bulk_opid_t *opid)
{
	int			rc = 0;

	if (!dtp_bulk_desc_valid(bulk_desc) || opid == NULL) {
		D_ERROR("invalid parameter for dtp_bulk_transfer.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dtp_hg_bulk_transfer(bulk_desc, complete_cb, arg, opid);
	if (rc != 0)
		D_ERROR("dtp_hg_bulk_transfer failed, rc: %d.\n", rc);

out:
	return rc;
}

int
dtp_bulk_get_len(dtp_bulk_t bulk_hdl, daos_size_t *bulk_len)
{
	int	rc = 0;

	if (bulk_hdl == DTP_BULK_NULL || bulk_len == NULL) {
		D_ERROR("invalid parameter, NULL bulk_hdl or bulk_len.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dtp_hg_bulk_get_len(bulk_hdl, bulk_len);
	if (rc != 0)
		D_ERROR("dtp_hg_bulk_get_len failed, rc: %d.\n", rc);

out:
	return rc;
}

int
dtp_bulk_get_sgnum(dtp_bulk_t bulk_hdl, unsigned int *bulk_sgnum)
{
	int	rc = 0;

	if (bulk_hdl == DTP_BULK_NULL || bulk_sgnum == NULL) {
		D_ERROR("invalid parameter, NULL bulk_hdl or bulk_sgnum.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dtp_hg_bulk_get_sgnum(bulk_hdl, bulk_sgnum);
	if (rc != 0)
		D_ERROR("dtp_hg_bulk_get_sgnum failed, rc: %d.\n", rc);

out:
	return rc;
}

int
dtp_bulk_abort(dtp_context_t dtp_ctx, dtp_bulk_opid_t opid)
{
	/* HG_Bulk_cancel is not implemented now. */
	return -DER_NOSYS;
}
