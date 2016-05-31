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
 * This file is part of daos_transport. It implements the main RPC routines.
 */

#include <dtp_internal.h>

int
dtp_req_create(dtp_context_t dtp_ctx, dtp_endpoint_t tgt_ep, dtp_opcode_t opc,
	       dtp_rpc_t **req)
{
	struct dtp_context	*ctx;
	struct dtp_rpc_priv	*rpc_priv = NULL;
	struct dtp_opc_info	*opc_info = NULL;
	dtp_rpc_t		*rpc_pub;
	int			rc = 0;

	if (dtp_ctx == DTP_CONTEXT_NULL || req == NULL) {
		D_ERROR("invalid parameter (NULL dtp_ctx or req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	/* TODO: possibly with multiple service group */
	if (tgt_ep.ep_rank >= dtp_gdata.dg_mcl_srv_set->size) {
		D_ERROR("invalid parameter, rank %d, group_size: %d.\n",
			tgt_ep.ep_rank, dtp_gdata.dg_mcl_srv_set->size);
		D_GOTO(out, rc = -DER_INVAL);
	}

	opc_info = dtp_opc_lookup(dtp_gdata.dg_opc_map, opc, DTP_UNLOCK);
	if (opc_info == NULL) {
		D_ERROR("opc: 0x%x, lookup failed.\n", opc);
		D_GOTO(out, rc = -DER_DTP_UNREG);
	}
	D_ASSERT(opc_info->doi_input_size <= DTP_MAX_INPUT_SIZE &&
		 opc_info->doi_output_size <= DTP_MAX_OUTPUT_SIZE);

	D_ALLOC_PTR(rpc_priv);
	if (rpc_priv == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rpc_pub = &rpc_priv->drp_pub;
	rpc_pub->dr_ep = tgt_ep;
	rpc_priv->drp_opc_info = opc_info;

	rc = dtp_rpc_inout_buff_init(rpc_pub);
	if (rc != 0)
		D_GOTO(out, rc);

	dtp_rpc_priv_init(rpc_priv, dtp_ctx, opc, 0);

	ctx = (struct dtp_context *)dtp_ctx;
	rc = dtp_hg_req_create(&ctx->dc_hg_ctx, tgt_ep, rpc_priv);
	if (rc != 0) {
		D_ERROR("dtp_hg_req_create failed, rc: %d, opc: 0x%x.\n",
			rc, opc);
		D_GOTO(out, rc);
	}

	*req = rpc_pub;

out:
	if (rc < 0) {
		if (rpc_priv != NULL)
			D_FREE_PTR(rpc_priv);
	}
	return rc;
}

int
dtp_req_addref(dtp_rpc_t *req)
{
	struct dtp_rpc_priv	*rpc_priv = NULL;
	int			rc = 0;

	if (req == NULL) {
		D_ERROR("invalid parameter (NULL req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct dtp_rpc_priv, drp_pub);
	pthread_spin_lock(&rpc_priv->drp_lock);
	rpc_priv->drp_refcount++;
	pthread_spin_unlock(&rpc_priv->drp_lock);

out:
	return rc;
}

int
dtp_req_decref(dtp_rpc_t *req)
{
	struct dtp_rpc_priv	*rpc_priv = NULL;
	int			rc = 0, destroy = 0;

	if (req == NULL) {
		D_ERROR("invalid parameter (NULL req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct dtp_rpc_priv, drp_pub);
	pthread_spin_lock(&rpc_priv->drp_lock);
	rpc_priv->drp_refcount--;
	if (rpc_priv->drp_refcount == 0)
		destroy = 1;
	pthread_spin_unlock(&rpc_priv->drp_lock);

	if (destroy == 1) {
		if (req->dr_final_cb != NULL)
			req->dr_final_cb(req);
		rc = dtp_hg_req_destroy(rpc_priv);
		if (rc != 0)
			D_ERROR("dtp_hg_req_destroy failed, rc: %d, "
				"opc: 0x%x.\n", rc, req->dr_opc);
	}

out:
	return rc;
}

int
dtp_req_send(dtp_rpc_t *req, dtp_cb_t complete_cb, void *arg)
{
	struct dtp_rpc_priv	*rpc_priv = NULL;
	int			rc = 0;

	if (req == NULL) {
		D_ERROR("invalid parameter (NULL req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct dtp_rpc_priv, drp_pub);
	rc = dtp_hg_req_send(rpc_priv, complete_cb, arg);
	if (rc != 0) {
		D_ERROR("dtp_hg_req_send failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_priv->drp_pub.dr_opc);
	}

out:
	return rc;
}

int
dtp_reply_send(dtp_rpc_t *req)
{
	struct dtp_rpc_priv	*rpc_priv = NULL;
	int			rc = 0;

	if (req == NULL) {
		D_ERROR("invalid parameter (NULL req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct dtp_rpc_priv, drp_pub);
	rc = dtp_hg_reply_send(rpc_priv);
	if (rc != 0) {
		D_ERROR("dtp_hg_reply_send failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_priv->drp_pub.dr_opc);
	}

out:
	return rc;
}

int
dtp_req_abort(dtp_rpc_t *req)
{
	return -DER_NOSYS;
}

#define DTP_DEFAULT_TIMEOUT	(20 * 1000 * 1000) /* Milli-seconds */

static int
dtp_cb_common(const struct dtp_cb_info *cb_info)
{
	*(int *)cb_info->dci_arg = 1;
	return 0;
}

/**
 * Send rpc synchronously
 *
 * \param[IN] rpc	point to DTP request.
 * \param[IN] timeout	timeout (Milliseconds) to wait, if
 *                      timeout <= 0, it will wait infinitely.
 * \return		0 if rpc return successfuly.
 * \return		negative errno if sending fails or timeout.
 */
int
dtp_sync_req(dtp_rpc_t *rpc, uint64_t timeout)
{
	struct timeval	tv;
	uint64_t now;
	uint64_t end;
	int rc;
	int complete = 0;

	/* Send request */
	rc = dtp_req_send(rpc, dtp_cb_common, &complete);
	if (rc != 0)
		return rc;

	/* Check if we are lucky */
	if (complete)
		return 0;

	timeout = timeout ? timeout : DTP_DEFAULT_TIMEOUT;
	/* Wait the request to be completed in timeout milliseconds */
	gettimeofday(&tv, NULL);
	now = tv.tv_sec * 1000 * 1000 + tv.tv_usec;
	end = now + timeout;

	while (1) {
		uint64_t interval = 1000; /* milliseconds */

		rc = dtp_progress(rpc->dr_ctx, interval, NULL, NULL);
		if (rc != 0 && rc != -DER_TIMEDOUT) {
			D_ERROR("dtp_progress failed rc: %d.\n", rc);
			break;
		}

		if (complete) {
			rc = 0;
			break;
		}

		gettimeofday(&tv, NULL);
		now = tv.tv_sec * 1000 * 1000 + tv.tv_usec;
		if (now >= end) {
			rc = -DER_TIMEDOUT;
			break;
		}
	}

	return rc;
}

void
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

void
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

int
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
