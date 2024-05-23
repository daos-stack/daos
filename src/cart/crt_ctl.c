/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It implements the server side of the cart_ctl
 * command line utility.
 */

#include "crt_internal.h"
#include <gurt/atomic.h>

#define MAX_HOSTNAME_SIZE 1024

static int verify_ctl_in_args(struct crt_ctl_ep_ls_in *in_args)
{
	struct crt_grp_priv	*grp_priv;
	int			rc = 0;

	if (in_args->cel_grp_id == NULL) {
		D_ERROR("invalid parameter, NULL input grp_id.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (crt_validate_grpid(in_args->cel_grp_id) != 0) {
		D_ERROR("srv_grpid contains invalid characters "
			"or is too long\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	grp_priv = crt_gdata.cg_grp->gg_primary_grp;

	if (!crt_grp_id_identical(in_args->cel_grp_id,
				  grp_priv->gp_pub.cg_grpid)) {
		D_ERROR("RPC request has wrong grp_id: %s\n",
			in_args->cel_grp_id);
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (in_args->cel_rank != grp_priv->gp_self) {
		D_ERROR("RPC request has wrong rank: %d\n", in_args->cel_rank);
		D_GOTO(out, rc = -DER_INVAL);
	}

out:
	return rc;
}

static int
crt_ctl_fill_buffer_cb(d_list_t *rlink, void *arg)
{
	crt_phy_addr_t		 uri;
	struct crt_uri_cache	*uri_cache = arg;
	struct crt_uri_item	*ui;
	uint32_t		*idx;
	uint32_t		 i;
	int			 rc = 0;

	D_ASSERT(rlink != NULL);
	D_ASSERT(arg != NULL);

	ui = crt_ui_link2ptr(rlink);
	idx = &uri_cache->idx;
	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		uri = atomic_load_relaxed(&ui->ui_uri[i]);
		if (uri == NULL)
			continue;

		if (*idx >= uri_cache->max_count) {
			D_ERROR("grp_cache index %u out of range [0, %u].\n",
				*idx, uri_cache->max_count);
			D_GOTO(out, rc = -DER_OVERFLOW);
		}

		uri_cache->grp_cache[*idx].gc_rank = ui->ui_rank;
		uri_cache->grp_cache[*idx].gc_tag = i;
		uri_cache->grp_cache[*idx].gc_uri = uri;

		*idx += 1;
	}

out:
	return rc;
}

void
crt_hdlr_ctl_get_uri_cache(crt_rpc_t *rpc_req)
{
	struct crt_uri_cache			 uri_cache = {0};
	struct crt_ctl_get_uri_cache_out	*out_args;
	struct crt_grp_priv			*grp_priv = NULL;
	int					 rc = 0;

	D_ASSERTF(crt_is_service(), "Must be called in a service process\n");
	out_args = crt_reply_get(rpc_req);

	grp_priv = crt_gdata.cg_grp->gg_primary_grp;

	D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);

	rc = verify_ctl_in_args(crt_req_get(rpc_req));
	if (rc != 0)
		D_GOTO(out, rc);

	/* calculate max possible count of grp_cache items */
	uri_cache.max_count = grp_priv->gp_size * CRT_SRV_CONTEXT_NUM;
	D_ALLOC_ARRAY(uri_cache.grp_cache, uri_cache.max_count);
	if (uri_cache.grp_cache == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	uri_cache.idx = 0;

	rc = d_hash_table_traverse(&grp_priv->gp_uri_lookup_cache,
				   crt_ctl_fill_buffer_cb, &uri_cache);
	if (rc != 0 && rc != -DER_OVERFLOW)
		D_GOTO(out, 0);

	out_args->cguc_grp_cache.ca_arrays = uri_cache.grp_cache;
	out_args->cguc_grp_cache.ca_count  = uri_cache.idx; /* actual count */
	rc = 0;
out:
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
	out_args->cguc_rc = rc;
	rc = crt_reply_send(rpc_req);
	D_ASSERTF(rc == 0, "crt_reply_send() failed. rc: %d\n", rc);
	D_DEBUG(DB_TRACE, "sent reply to get uri cache request\n");
	D_FREE(uri_cache.grp_cache);
}

void
crt_hdlr_ctl_get_hostname(crt_rpc_t *rpc_req)
{
	struct crt_ctl_get_host_out	*out_args;
	char				hostname[MAX_HOSTNAME_SIZE];
	int				rc;

	out_args = crt_reply_get(rpc_req);
	rc = verify_ctl_in_args(crt_req_get(rpc_req));
	if (rc != 0)
		D_GOTO(out, rc);

	if (gethostname(hostname, MAX_HOSTNAME_SIZE) != 0) {
		D_ERROR("gethostname() failed with errno %d\n", errno);
		D_GOTO(out, rc = -DER_INVAL);
	}

	d_iov_set(&out_args->cgh_hostname, hostname, strlen(hostname));

out:
	out_args->cgh_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send() failed with rc %d\n", rc);
}

void
crt_hdlr_ctl_get_pid(crt_rpc_t *rpc_req)
{
	struct crt_ctl_get_pid_out	*out_args;
	int				rc;

	out_args = crt_reply_get(rpc_req);
	rc = verify_ctl_in_args(crt_req_get(rpc_req));
	if (rc != 0)
		D_GOTO(out, rc);

	out_args->cgp_pid = getpid();

out:
	out_args->cgp_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send() failed with rc %d\n", rc);
}

void
crt_hdlr_ctl_ls(crt_rpc_t *rpc_req)
{
	struct crt_ctl_ep_ls_in		*in_args;
	struct crt_ctl_ep_ls_out	*out_args;
	char				 addr_str[CRT_ADDR_STR_MAX_LEN]
						= {'\0'};
	size_t				 str_size;
	char				*addr_buf = NULL;
	uint32_t			 addr_buf_len;
	int				 count;
	struct crt_context		*ctx = NULL;
	d_list_t			*ctx_list;
	int				 provider;
	int				 rc = 0;

	D_ASSERTF(crt_is_service(), "Must be called in a service process\n");
	in_args = crt_req_get(rpc_req);
	D_ASSERTF(in_args != NULL, "NULL input args\n");
	out_args = crt_reply_get(rpc_req);
	D_ASSERTF(out_args != NULL, "NULL output args\n");

	rc = verify_ctl_in_args(in_args);
	if (rc != 0)
		D_GOTO(out, rc);

	addr_buf_len = 0;

	D_RWLOCK_RDLOCK(&crt_gdata.cg_rwlock);

	/* TODO: Need to derive provider from rpc struct */
	provider = crt_gdata.cg_primary_prov;

	ctx_list = crt_provider_get_ctx_list(true, provider);

	out_args->cel_ctx_num = crt_provider_get_cur_ctx_num(true, provider);

	d_list_for_each_entry(ctx, ctx_list, cc_link) {
		str_size = CRT_ADDR_STR_MAX_LEN;

		D_MUTEX_LOCK(&ctx->cc_mutex);
		rc = crt_hg_get_addr(ctx->cc_hg_ctx.chc_hgcla, NULL, &str_size);
		D_MUTEX_UNLOCK(&ctx->cc_mutex);
		if (rc != 0) {
			D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);
			D_ERROR("context (idx %d), crt_hg_get_addr failed rc: "
				"%d.\n", ctx->cc_idx, rc);
			D_GOTO(out, rc);
		}
		addr_buf_len += str_size;
	}

	D_ALLOC(addr_buf, addr_buf_len);
	if (addr_buf == NULL) {
		D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);
		D_GOTO(out, rc = -DER_NOMEM);
	}

	count = 0;

	d_list_for_each_entry(ctx, ctx_list, cc_link) {
		str_size = CRT_ADDR_STR_MAX_LEN;
		rc = 0;

		D_MUTEX_LOCK(&ctx->cc_mutex);
		rc = crt_hg_get_addr(ctx->cc_hg_ctx.chc_hgcla, addr_str,
				     &str_size);
		D_MUTEX_UNLOCK(&ctx->cc_mutex);

		if (rc != 0) {
			D_ERROR("context (idx %d), crt_hg_get_addr failed rc: "
				"%d.\n", ctx->cc_idx, rc);
			break;
		}

		count += snprintf(addr_buf + count, addr_buf_len - count,
				  "%s", addr_str);
		count += 1;
	}

	D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);
	D_ASSERT(count <= addr_buf_len);

	D_DEBUG(DB_TRACE, "out_args->cel_ctx_num %d\n", out_args->cel_ctx_num);
	d_iov_set(&out_args->cel_addr_str, addr_buf, count);

out:
	out_args->cel_rc = rc;
	rc = crt_reply_send(rpc_req);
	D_ASSERTF(rc == 0, "crt_reply_send() failed. rc: %d\n", rc);
	D_DEBUG(DB_TRACE, "sent reply to endpoint list request\n");
	D_FREE(addr_buf);
}
