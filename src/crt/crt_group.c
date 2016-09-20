/* Copyright (C) 2016 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * This file is part of CaRT. It implements the main group APIs.
 */

#include <crt_internal.h>

/* global CRT group list */
CRT_LIST_HEAD(crt_grp_list);
/* protect global group list */
pthread_rwlock_t crt_grp_list_rwlock = PTHREAD_RWLOCK_INITIALIZER;

static void crt_li_destroy(struct crt_lookup_item *li);

static struct crt_lookup_item *
li_link2ptr(crt_list_t *rlink)
{
	C_ASSERT(rlink != NULL);
	return container_of(rlink, struct crt_lookup_item, li_link);
}

static int
li_op_key_get(struct dhash_table *hhtab, crt_list_t *rlink, void **key_pp)
{
	struct crt_lookup_item *li = li_link2ptr(rlink);

	*key_pp = (void *)&li->li_rank;
	return sizeof(li->li_rank);
}

static uint32_t
li_op_key_hash(struct dhash_table *hhtab, const void *key, unsigned int ksize)
{
	C_ASSERT(ksize == sizeof(crt_rank_t));

	return (unsigned int)(*(const uint32_t *)key %
		(1U << CRT_LOOKUP_CACHE_BITS));
}

static bool
li_op_key_cmp(struct dhash_table *hhtab, crt_list_t *rlink,
	  const void *key, unsigned int ksize)
{
	struct crt_lookup_item *li = li_link2ptr(rlink);

	C_ASSERT(ksize == sizeof(crt_rank_t));

	return li->li_rank == *(crt_rank_t *)key;
}

static void
li_op_rec_addref(struct dhash_table *hhtab, crt_list_t *rlink)
{
	struct crt_lookup_item *li = li_link2ptr(rlink);

	C_ASSERT(li->li_initialized);
	pthread_mutex_lock(&li->li_mutex);
	li_link2ptr(rlink)->li_ref++;
	pthread_mutex_unlock(&li->li_mutex);
}

static bool
li_op_rec_decref(struct dhash_table *hhtab, crt_list_t *rlink)
{
	struct crt_lookup_item *li = li_link2ptr(rlink);

	li->li_ref--;
	return li->li_ref == 0;
}

static void
li_op_rec_free(struct dhash_table *hhtab, crt_list_t *rlink)
{
	crt_li_destroy(li_link2ptr(rlink));
}

static dhash_table_ops_t lookup_table_ops = {
	.hop_key_get		= li_op_key_get,
	.hop_key_hash		= li_op_key_hash,
	.hop_key_cmp		= li_op_key_cmp,
	.hop_rec_addref		= li_op_rec_addref,
	.hop_rec_decref		= li_op_rec_decref,
	.hop_rec_free		= li_op_rec_free,
};

static void
crt_li_destroy(struct crt_lookup_item *li)
{
	C_ASSERT(li != NULL);

	C_ASSERT(li->li_ref == 0);
	C_ASSERT(li->li_initialized == 1);

	C_ASSERT(li->li_base_phy_addr != NULL);
	free(li->li_base_phy_addr);

	/*
	struct crt_context	*ctx;
	hg_return_t		hg_ret;
	int			i;

	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		if (li->li_tag_addr[i] == NULL)
			continue;
		ctx = li->li_grp_priv->gp_ctx;
		hg_ret = HG_Addr_free(ctx->dc_hg_ctx.dhc_hgcla,
				      li->li_tag_addr[i]);
		if (hg_ret != HG_SUCCESS)
			C_ERROR("HG_Addr_free failed, hg_ret: %d.\n", hg_ret);
	}
	*/

	pthread_mutex_destroy(&li->li_mutex);

	C_FREE_PTR(li);
}

static int
crt_grp_lc_create(struct crt_grp_priv *grp_priv)
{
	struct dhash_table	*htable = NULL;
	int			rc;

	C_ASSERT(grp_priv != NULL);
	if (grp_priv->gp_primary == 0) {
		C_ERROR("need not create lookup cache for sub-group.\n");
		C_GOTO(out, rc = -CER_NO_PERM);
	}

	rc = dhash_table_create(DHASH_FT_NOLOCK, CRT_LOOKUP_CACHE_BITS,
				NULL, &lookup_table_ops, &htable);
	if (rc != 0) {
		C_ERROR("dhash_table_create_inplace failed, rc: %d.\n", rc);
		C_GOTO(out, rc);
	}
	C_ASSERT(htable != NULL);

	grp_priv->gp_lookup_cache = htable;

out:
	if (rc != 0)
		C_ERROR("crt_grp_lc_create failed, rc: %d.\n", rc);
	return rc;
}

static int
crt_grp_lc_destroy(struct crt_grp_priv *grp_priv)
{
	int	rc;

	C_ASSERT(grp_priv != NULL);

	if (grp_priv->gp_lookup_cache == NULL)
		return 0;

	rc = dhash_table_destroy(grp_priv->gp_lookup_cache, true /* force */);
	if (rc != 0)
		C_ERROR("dhash_table_destroy_inplace failed, rc: %d.\n", rc);

	return rc;
}

static int
crt_conn_tag(struct crt_hg_context *hg_ctx, crt_phy_addr_t base_addr,
	     uint32_t tag, na_addr_t *na_addr)
{
	hg_class_t	*hg_class;
	hg_context_t	*hg_context;
	hg_addr_t	tmp_addr;
	uint32_t	ctx_idx;
	char		*tmp_addrstr;
	bool		allocated = false;
	char		*pchar;
	int		port;
	int		rc = 0;

	if (tag >= CRT_SRV_CONTEXT_NUM) {
		C_ERROR("invalid tag %d (CRT_SRV_CONTEXT_NUM %d).\n",
			tag, CRT_SRV_CONTEXT_NUM);
		C_GOTO(out, rc = -CER_INVAL);
	}

	C_ASSERT(hg_ctx != NULL);
	C_ASSERT(base_addr != NULL && strlen(base_addr) > 0);
	C_ASSERT(na_addr != NULL);

	hg_class = hg_ctx->dhc_hgcla;
	hg_context = hg_ctx->dhc_hgctx;
	C_ASSERT(hg_class != NULL);
	C_ASSERT(hg_context != NULL);

	ctx_idx = tag;
	if (ctx_idx == 0) {
		tmp_addrstr = base_addr;
	} else {
		C_ALLOC(tmp_addrstr, CRT_ADDR_STR_MAX_LEN);
		if (tmp_addrstr == NULL)
			C_GOTO(out, rc = -CER_NOMEM);
		allocated = true;
		/* calculate the ctx_idx's listening address and connect */
		strncpy(tmp_addrstr, base_addr, CRT_ADDR_STR_MAX_LEN);
		pchar = strrchr(tmp_addrstr, ':');
		if (pchar == NULL) {
			C_ERROR("bad format of base_addr %s.\n", tmp_addrstr);
			C_GOTO(out, rc = -CER_INVAL);
		}
		pchar++;
		port = atoi(pchar);
		port += ctx_idx;
		snprintf(pchar, 16, "%d", port);
		C_DEBUG("base uri(%s), tag(%d) uri(%s).\n",
			base_addr, tag, tmp_addrstr);
	}

	rc = crt_hg_addr_lookup_wait(hg_class, hg_context, tmp_addrstr,
				     &tmp_addr);
	if (rc == 0) {
		C_DEBUG("Connect to %s succeed.\n", tmp_addrstr);
		C_ASSERT(tmp_addr != NULL);
		*na_addr = tmp_addr;
	} else {
		C_ERROR("Could not connect to %s, rc: %d.\n", tmp_addrstr, rc);
		C_GOTO(out, rc);
	}

out:
	if (allocated)
		C_FREE(tmp_addrstr, CRT_ADDR_STR_MAX_LEN);
	if (rc != 0)
		C_ERROR("crt_conn_tag (base_addr %s, tag %d) failed, rc: %d.\n",
			base_addr, tag, rc);
	return rc;
}

/*
 * lookup addr cache.
 * if (na_addr == NULL) then means caller only want to lookup the base_addr, or
 * will establish the connection and return the connect addr to na_addr.
 */
int
crt_grp_lc_lookup(struct crt_grp_priv *grp_priv, struct crt_hg_context *hg_ctx,
		  crt_rank_t rank, uint32_t tag, crt_phy_addr_t *base_addr,
		  na_addr_t *na_addr)
{
	struct crt_lookup_item	*li;
	bool			found = false;
	crt_list_t		*rlink;
	int			rc = 0;

	C_ASSERT(grp_priv != NULL);
	C_ASSERT(grp_priv->gp_primary != 0);
	C_ASSERT(rank < grp_priv->gp_size);
	C_ASSERT(tag < CRT_SRV_CONTEXT_NUM);
	C_ASSERT(base_addr != NULL || na_addr != NULL);
	if (na_addr != NULL)
		C_ASSERT(hg_ctx != NULL);

lookup_again:
	pthread_rwlock_rdlock(&grp_priv->gp_rwlock);
	rlink = dhash_rec_find(grp_priv->gp_lookup_cache, (void *)&rank,
			       sizeof(rank));
	if (rlink != NULL) {
		li = li_link2ptr(rlink);
		C_ASSERT(li->li_grp_priv == grp_priv);
		C_ASSERT(li->li_rank == rank);
		C_ASSERT(li->li_base_phy_addr != NULL &&
			 strlen(li->li_base_phy_addr) > 0);
		C_ASSERT(li->li_initialized != 0);

		found = true;
		if (base_addr != NULL)
			*base_addr = li->li_base_phy_addr;
		if (na_addr == NULL) {
			C_ASSERT(base_addr != NULL);
			pthread_rwlock_unlock(&grp_priv->gp_rwlock);
			dhash_rec_decref(grp_priv->gp_lookup_cache, rlink);
			C_GOTO(out, rc);
		}
		if (li->li_tag_addr[tag] != NULL) {
			*na_addr = li->li_tag_addr[tag];
			pthread_rwlock_unlock(&grp_priv->gp_rwlock);
			dhash_rec_decref(grp_priv->gp_lookup_cache, rlink);
			C_GOTO(out, rc);
		}
	}
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);

	if (found) {
		C_ASSERT(na_addr != NULL);
		C_ASSERT(li != NULL);
		pthread_mutex_lock(&li->li_mutex);
		if (li->li_tag_addr[tag] != NULL) {
			*na_addr = li->li_tag_addr[tag];
			pthread_mutex_unlock(&li->li_mutex);
			dhash_rec_decref(grp_priv->gp_lookup_cache, rlink);
			C_GOTO(out, rc);
		}
		rc = crt_conn_tag(hg_ctx, li->li_base_phy_addr, tag,
				  &li->li_tag_addr[tag]);
		if (rc == 0) {
			C_ASSERT(li->li_tag_addr[tag] != NULL);
			*na_addr = li->li_tag_addr[tag];
		}
		pthread_mutex_unlock(&li->li_mutex);
		dhash_rec_decref(grp_priv->gp_lookup_cache, rlink);
		C_GOTO(out, rc);
	}

	C_ASSERT(found == false);
	C_ALLOC_PTR(li);
	if (li == NULL)
		C_GOTO(out, rc = -CER_NOMEM);
	CRT_INIT_LIST_HEAD(&li->li_link);
	li->li_grp_priv = grp_priv;
	li->li_rank = rank;
	rc = crt_grp_uri_lookup(grp_priv, rank, &li->li_base_phy_addr);
	if (rc != 0) {
		C_ERROR("crt_grp_uri_lookup failed, rc: %d.\n", rc);
		C_FREE_PTR(li);
		C_GOTO(out, rc);
	}
	C_ASSERT(li->li_base_phy_addr != NULL);
	li->li_initialized = 1;
	pthread_mutex_init(&li->li_mutex, NULL);

	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	rlink = dhash_rec_find(grp_priv->gp_lookup_cache, (void *)&rank,
			       sizeof(rank));
	if (rlink != NULL) {
		/* race condition, goto above path to lookup again */
		crt_li_destroy(li);
		pthread_rwlock_unlock(&grp_priv->gp_rwlock);
		dhash_rec_decref(grp_priv->gp_lookup_cache, rlink);
		goto lookup_again;
	}
	rc = dhash_rec_insert(grp_priv->gp_lookup_cache, &rank, sizeof(rank),
			      &li->li_link, true /* exclusive */);
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);
	if (rc == 0) {
		goto lookup_again;
	} else {
		C_ERROR("dhash_rec_insert failed, rc: %d.\n", rc);
		pthread_mutex_destroy(&li->li_mutex);
		C_FREE_PTR(li);
	}

out:
	return rc;
}

static inline bool
crt_grp_id_identical(crt_group_id_t grp_id_1, crt_group_id_t grp_id_2)
{
	C_ASSERT(grp_id_1 != NULL && strlen(grp_id_1) > 0 &&
		 strlen(grp_id_1) < CRT_GROUP_ID_MAX_LEN);
	C_ASSERT(grp_id_2 != NULL && strlen(grp_id_2) > 0 &&
		 strlen(grp_id_2) < CRT_GROUP_ID_MAX_LEN);
	return strcmp(grp_id_1, grp_id_2) == 0;
}

static inline struct crt_grp_priv *
crt_grp_lookup_locked(crt_group_id_t grp_id)
{
	struct crt_grp_priv	*grp_priv;
	bool			found = false;

	crt_list_for_each_entry(grp_priv, &crt_grp_list, gp_link) {
		if (crt_grp_id_identical(grp_priv->gp_pub.cg_grpid,
					 grp_id)) {
			found = true;
			break;
		}
	}
	return (found == true) ? grp_priv : NULL;
}

static inline void
crt_grp_insert_locked(struct crt_grp_priv *grp_priv)
{
	C_ASSERT(grp_priv != NULL);
	crt_list_add_tail(&grp_priv->gp_link, &crt_grp_list);
}

static inline void
crt_grp_del_locked(struct crt_grp_priv *grp_priv)
{
	C_ASSERT(grp_priv != NULL);
	crt_list_del_init(&grp_priv->gp_link);
}

static inline int
crt_grp_priv_create(struct crt_grp_priv **grp_priv_created,
		    crt_group_id_t grp_id, bool primary_grp,
		    crt_rank_list_t *membs, crt_grp_create_cb_t grp_create_cb,
		    void *priv)
{
	struct crt_grp_priv *grp_priv;
	int	rc = 0;

	C_ASSERT(grp_priv_created != NULL);
	C_ASSERT(grp_id != NULL && strlen(grp_id) > 0 &&
		 strlen(grp_id) < CRT_GROUP_ID_MAX_LEN);

	C_ALLOC_PTR(grp_priv);
	if (grp_priv == NULL)
		C_GOTO(out, rc = -CER_NOMEM);

	CRT_INIT_LIST_HEAD(&grp_priv->gp_link);
	grp_priv->gp_primary = primary_grp;
	grp_priv->gp_pub.cg_grpid = strdup(grp_id);
	if (grp_priv->gp_pub.cg_grpid == NULL) {
		C_ERROR("strdup grp_id (%s) failed.\n", grp_id);
		C_FREE_PTR(grp_priv);
		C_GOTO(out, rc = -CER_NOMEM);
	}
	rc = crt_rank_list_dup(&grp_priv->gp_membs, membs, true /* input */);
	if (rc != 0) {
		C_ERROR("crt_rank_list_dup failed, rc: %d.\n", rc);
		C_FREE(grp_priv->gp_pub.cg_grpid, strlen(grp_id));
		C_FREE_PTR(grp_priv);
		C_GOTO(out, rc);
	}

	grp_priv->gp_status = CRT_GRP_CREATING;
	CRT_INIT_LIST_HEAD(&grp_priv->gp_child_rpcs);
	grp_priv->gp_priv = priv;

	if (!primary_grp) {
		crt_rank_list_sort(grp_priv->gp_membs);
		grp_priv->gp_parent_rpc = NULL;
		/* TODO tree children num */
		grp_priv->gp_child_num = membs->rl_nr.num;
		grp_priv->gp_child_ack_num = 0;
		grp_priv->gp_failed_ranks = NULL;
		grp_priv->gp_create_cb = grp_create_cb;
	}

	pthread_rwlock_init(&grp_priv->gp_rwlock, NULL);

	*grp_priv_created = grp_priv;

out:
	return rc;
}

static inline int
crt_grp_lookup_create(crt_group_id_t grp_id, crt_rank_list_t *member_ranks,
		      crt_grp_create_cb_t grp_create_cb, void *priv,
		      struct crt_grp_priv **grp_result)
{
	struct crt_grp_priv	*grp_priv = NULL;
	int			rc = 0;

	C_ASSERT(member_ranks != NULL);
	C_ASSERT(grp_result != NULL);

	pthread_rwlock_wrlock(&crt_grp_list_rwlock);
	grp_priv = crt_grp_lookup_locked(grp_id);
	if (grp_priv != NULL) {
		pthread_rwlock_unlock(&crt_grp_list_rwlock);
		*grp_result = grp_priv;
		C_GOTO(out, rc = -CER_EXIST);
	}

	rc = crt_grp_priv_create(&grp_priv, grp_id, false /* primary group */,
				 member_ranks, grp_create_cb, priv);
	if (rc != 0) {
		C_ERROR("crt_grp_priv_create failed, rc: %d.\n", rc);
		pthread_rwlock_unlock(&crt_grp_list_rwlock);
		C_GOTO(out, rc);
	}
	C_ASSERT(grp_priv != NULL);
	crt_grp_insert_locked(grp_priv);
	pthread_rwlock_unlock(&crt_grp_list_rwlock);

	*grp_result = grp_priv;

out:
	return rc;
}

static inline void
crt_grp_priv_destroy(struct crt_grp_priv *grp_priv)
{
	if (grp_priv == NULL)
		return;

	/* remove from group list */
	pthread_rwlock_wrlock(&crt_grp_list_rwlock);
	crt_grp_del_locked(grp_priv);
	pthread_rwlock_unlock(&crt_grp_list_rwlock);

	/* destroy the grp_priv */
	crt_rank_list_free(grp_priv->gp_membs);
	crt_rank_list_free(grp_priv->gp_failed_ranks);
	if (grp_priv->gp_psr_phy_addr != NULL)
		free(grp_priv->gp_psr_phy_addr);
	pthread_rwlock_destroy(&grp_priv->gp_rwlock);
	C_FREE(grp_priv->gp_pub.cg_grpid, strlen(grp_priv->gp_pub.cg_grpid));

	C_FREE_PTR(grp_priv);
}

struct gc_req {
	crt_list_t	 gc_link;
	crt_rpc_t	*gc_rpc;
};

static inline int
gc_add_child_rpc(struct crt_grp_priv *grp_priv, crt_rpc_t *gc_rpc)
{
	struct gc_req	*gc_req_item;
	int		rc = 0;

	C_ASSERT(grp_priv != NULL);
	C_ASSERT(gc_rpc != NULL);

	C_ALLOC_PTR(gc_req_item);
	if (gc_req_item == NULL)
		C_GOTO(out, rc = -CER_NOMEM);

	CRT_INIT_LIST_HEAD(&gc_req_item->gc_link);
	gc_req_item->gc_rpc = gc_rpc;

	rc = crt_req_addref(gc_rpc);
	C_ASSERT(rc == 0);

	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	crt_list_add_tail(&gc_req_item->gc_link, &grp_priv->gp_child_rpcs);
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);

out:
	return rc;
}

static inline void
gc_del_child_rpc(struct crt_grp_priv *grp_priv, crt_rpc_t *gc_rpc)
{
	struct gc_req	*gc, *gc_next;
	int		rc = 0;

	C_ASSERT(grp_priv != NULL);
	C_ASSERT(gc_rpc != NULL);

	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	crt_list_for_each_entry_safe(gc, gc_next, &grp_priv->gp_child_rpcs,
				      gc_link) {
		if (gc->gc_rpc == gc_rpc) {
			crt_list_del_init(&gc->gc_link);
			/* decref corresponds to the addref in
			 * gc_add_child_rpc */
			rc = crt_req_decref(gc_rpc);
			C_ASSERT(rc == 0);
			C_FREE_PTR(gc);
			break;
		}
	}
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);
}

int
crt_hdlr_grp_create(crt_rpc_t *rpc_req)
{
	struct crt_grp_priv		*grp_priv = NULL;
	struct crt_grp_create_in	*gc_in;
	struct crt_grp_create_out	*gc_out;
	crt_rank_t			my_rank;
	int				rc = 0;

	C_ASSERT(rpc_req != NULL);
	gc_in = crt_req_get(rpc_req);
	gc_out = crt_reply_get(rpc_req);
	C_ASSERT(gc_in != NULL && gc_out != NULL);

	rc = crt_grp_lookup_create(gc_in->gc_grp_id, gc_in->gc_membs,
				   NULL /* grp_create_cb */, NULL /* priv */,
				   &grp_priv);
	if (rc == 0) {
		grp_priv->gp_status = CRT_GRP_NORMAL;
		grp_priv->gp_ctx = rpc_req->dr_ctx;
		C_GOTO(out, rc);
	}
	if (rc == -CER_EXIST) {
		rc = crt_group_rank(NULL, &my_rank);
		C_ASSERT(rc == 0);
		if (my_rank == gc_in->gc_initiate_rank &&
		    grp_priv->gp_status == CRT_GRP_CREATING) {
			grp_priv->gp_status = CRT_GRP_NORMAL;
			grp_priv->gp_ctx = rpc_req->dr_ctx;
			rc = 0;
		}
	} else {
		C_ERROR("crt_grp_lookup_create failed, rc: %d.\n", rc);
		C_GOTO(out, rc);
	}

out:
	crt_group_rank(NULL, &gc_out->gc_rank);
	gc_out->gc_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		C_ERROR("crt_reply_send failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_req->dr_opc);
	return rc;
}

static int
gc_rpc_cb(const struct crt_cb_info *cb_info)
{
	struct crt_grp_priv		*grp_priv;
	crt_rpc_t			*gc_req;
	struct crt_grp_create_in	*gc_in;
	struct crt_grp_create_out	*gc_out;
	crt_rank_t			 my_rank;
	bool				 gc_done = false;
	int				 rc = 0;

	gc_req = cb_info->dci_rpc;
	gc_in = crt_req_get(gc_req);
	gc_out = crt_reply_get(gc_req);
	rc = cb_info->dci_rc;
	grp_priv = (struct crt_grp_priv *)cb_info->dci_arg;
	C_ASSERT(grp_priv != NULL && gc_in != NULL && gc_out != NULL);

	crt_group_rank(NULL, &my_rank);
	if (rc != 0)
		C_ERROR("RPC error, rc: %d.\n", rc);
	if (gc_out->gc_rc)
		C_ERROR("group create failed at rank %d, rc: %d.\n",
			gc_out->gc_rank, gc_out->gc_rc);

	/* TODO error handling */

	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	if (rc != 0 || gc_out->gc_rc != 0)
		grp_priv->gp_rc = (rc == 0) ? gc_out->gc_rc : rc;
	grp_priv->gp_child_ack_num++;
	C_ASSERT(grp_priv->gp_child_ack_num <= grp_priv->gp_child_num);
	if (grp_priv->gp_child_ack_num == grp_priv->gp_child_num)
		gc_done = true;
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);

	gc_del_child_rpc(grp_priv, gc_req);

	if (!gc_done)
		C_GOTO(out, rc);

	if (grp_priv->gp_create_cb != NULL)
		grp_priv->gp_create_cb(&grp_priv->gp_pub, grp_priv->gp_priv,
				       grp_priv->gp_rc);

	if (grp_priv->gp_rc != 0) {
		C_ERROR("group create failed, rc: %d.\n", grp_priv->gp_rc);
		crt_grp_priv_destroy(grp_priv);
	} else {
		grp_priv->gp_status = CRT_GRP_NORMAL;
	}

out:
	return rc;
}

int
crt_group_create(crt_group_id_t grp_id, crt_rank_list_t *member_ranks,
		 bool populate_now, crt_grp_create_cb_t grp_create_cb,
		 void *priv)
{
	crt_context_t		crt_ctx;
	struct crt_grp_priv	*grp_priv = NULL;
	bool			 gc_req_sent = false;
	crt_rank_t		 myrank;
	bool			 in_grp = false;
	int			 i;
	int			 rc = 0;

	if (!crt_initialized()) {
		C_ERROR("CRT un-initialized.\n");
		C_GOTO(out, rc = -CER_UNINIT);
	}
	if (!crt_is_service()) {
		C_ERROR("Cannot create subgroup on pure client side.\n");
		C_GOTO(out, rc = -CER_NO_PERM);
	}
	if (grp_id == NULL || strlen(grp_id) == 0 ||
	    strlen(grp_id) >= CRT_GROUP_ID_MAX_LEN) {
		C_ERROR("invalid parameter of grp_id.\n");
		C_GOTO(out, rc = -CER_INVAL);
	}
	if (member_ranks == NULL || grp_create_cb == NULL) {
		C_ERROR("invalid arg, member_ranks %p, grp_create_cb %p.\n",
			member_ranks, grp_create_cb);
		C_GOTO(out, rc = -CER_INVAL);
	}
	crt_group_rank(NULL, &myrank);
	for (i = 0; i < member_ranks->rl_nr.num; i++) {
		if (member_ranks->rl_ranks[i] == myrank) {
			in_grp = true;
			break;
		}
	}
	if (in_grp == false) {
		C_ERROR("myrank %d not in member_ranks, cannot create group.\n",
			myrank);
		C_GOTO(out, rc = -CER_OOG);
	}
	crt_ctx = crt_context_lookup(0);
	if (crt_ctx == CRT_CONTEXT_NULL) {
		C_ERROR("crt_context_lookup failed.\n");
		C_GOTO(out, rc = -CER_UNINIT);
	}

	rc = crt_grp_lookup_create(grp_id, member_ranks, grp_create_cb, priv,
				   &grp_priv);
	if (rc != 0) {
		C_ERROR("crt_grp_lookup_create failed, rc: %d.\n", rc);
		C_GOTO(out, rc);
	}
	grp_priv->gp_ctx = crt_ctx;

	/* TODO handle the populate_now == false */

	/* send RPC one by one now */
	for (i = 0; i < member_ranks->rl_nr.num; i++) {
		crt_rpc_t			*gc_rpc;
		struct crt_grp_create_in	*gc_in;
		crt_endpoint_t			 tgt_ep;

		tgt_ep.ep_grp = NULL;
		tgt_ep.ep_rank = member_ranks->rl_ranks[i];
		tgt_ep.ep_tag = 0;
		rc = crt_req_create(crt_ctx, tgt_ep, CRT_OPC_GRP_CREATE,
				    &gc_rpc);
		if (rc != 0) {
			C_ERROR("crt_req_create(CRT_OPC_GRP_CREATE) failed, "
				"tgt_ep: %d, rc: %d.\n", tgt_ep.ep_rank, rc);
			grp_priv->gp_child_ack_num +=
				grp_priv->gp_child_num - i;
			grp_priv->gp_rc = rc;
			C_GOTO(out, rc);
		}

		gc_in = crt_req_get(gc_rpc);
		C_ASSERT(gc_in != NULL);
		gc_in->gc_grp_id = grp_id;
		gc_in->gc_membs = member_ranks;
		crt_group_rank(NULL, &gc_in->gc_initiate_rank);

		rc = crt_req_send(gc_rpc, gc_rpc_cb, grp_priv);
		if (rc != 0) {
			C_ERROR("crt_req_send(CRT_OPC_GRP_CREATE) failed, "
				"tgt_ep: %d, rc: %d.\n", tgt_ep.ep_rank, rc);
			grp_priv->gp_child_ack_num +=
				grp_priv->gp_child_num - i;
			grp_priv->gp_rc = rc;
			C_GOTO(out, rc);
		}
		rc = gc_add_child_rpc(grp_priv, gc_rpc);
		C_ASSERT(rc == 0);

		gc_req_sent =  true;
	}

out:
	if (gc_req_sent == false) {
		C_ASSERT(rc != 0);
		C_ERROR("crt_group_create failed, rc: %d.\n", rc);

		if (grp_create_cb != NULL)
			grp_create_cb(NULL, priv, rc);

		crt_grp_priv_destroy(grp_priv);
	}
	return rc;
}

crt_group_t *
crt_group_lookup(crt_group_id_t grp_id)
{
	struct crt_grp_priv	*grp_priv = NULL;

	pthread_rwlock_rdlock(&crt_grp_list_rwlock);
	grp_priv = crt_grp_lookup_locked(grp_id);
	if (grp_priv == NULL)
		C_DEBUG("group non-exist.\n");
	pthread_rwlock_unlock(&crt_grp_list_rwlock);

	return (grp_priv == NULL) ? NULL : &grp_priv->gp_pub;
}

int
crt_hdlr_grp_destroy(crt_rpc_t *rpc_req)
{
	struct crt_grp_priv		*grp_priv = NULL;
	struct crt_grp_destroy_in	*gd_in;
	struct crt_grp_destroy_out	*gd_out;
	crt_rank_t			my_rank;
	int				rc = 0;

	C_ASSERT(rpc_req != NULL);
	gd_in = crt_req_get(rpc_req);
	gd_out = crt_reply_get(rpc_req);
	C_ASSERT(gd_in != NULL && gd_out != NULL);

	pthread_rwlock_rdlock(&crt_grp_list_rwlock);
	grp_priv = crt_grp_lookup_locked(gd_in->gd_grp_id);
	if (grp_priv == NULL) {
		C_DEBUG("group non-exist.\n");
		pthread_rwlock_unlock(&crt_grp_list_rwlock);
		C_GOTO(out, rc = -CER_NONEXIST);
	}
	pthread_rwlock_unlock(&crt_grp_list_rwlock);

	rc = crt_group_rank(NULL, &my_rank);
	C_ASSERT(rc == 0);
	/* for gd_initiate_rank, destroy the group in gd_rpc_cb */
	if (my_rank != gd_in->gd_initiate_rank)
		crt_grp_priv_destroy(grp_priv);

out:
	crt_group_rank(NULL, &gd_out->gd_rank);
	gd_out->gd_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		C_ERROR("crt_reply_send failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_req->dr_opc);
	return rc;
}

static int
gd_rpc_cb(const struct crt_cb_info *cb_info)
{
	struct crt_grp_priv		*grp_priv;
	crt_rpc_t			*gd_req;
	struct crt_grp_destroy_in	*gd_in;
	struct crt_grp_destroy_out	*gd_out;
	crt_rank_t			 my_rank;
	bool				 gd_done = false;
	int				 rc = 0;

	gd_req = cb_info->dci_rpc;
	gd_in = crt_req_get(gd_req);
	gd_out = crt_reply_get(gd_req);
	rc = cb_info->dci_rc;
	grp_priv = (struct crt_grp_priv *)cb_info->dci_arg;
	C_ASSERT(grp_priv != NULL && gd_in != NULL && gd_out != NULL);

	crt_group_rank(NULL, &my_rank);
	if (rc != 0)
		C_ERROR("RPC error, rc: %d.\n", rc);
	if (gd_out->gd_rc)
		C_ERROR("group create failed at rank %d, rc: %d.\n",
			gd_out->gd_rank, gd_out->gd_rc);

	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	if (rc != 0 || gd_out->gd_rc != 0)
		grp_priv->gp_rc = (rc == 0) ? gd_out->gd_rc : rc;
	grp_priv->gp_child_ack_num++;
	C_ASSERT(grp_priv->gp_child_ack_num <= grp_priv->gp_child_num);
	if (grp_priv->gp_child_ack_num == grp_priv->gp_child_num)
		gd_done = true;
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);

	gc_del_child_rpc(grp_priv, gd_req);

	if (!gd_done)
		C_GOTO(out, rc);

	if (grp_priv->gp_destroy_cb != NULL)
		grp_priv->gp_destroy_cb(grp_priv->gp_destroy_cb_arg,
					grp_priv->gp_rc);

	if (grp_priv->gp_rc != 0)
		C_ERROR("group destroy failed, rc: %d.\n", grp_priv->gp_rc);
	else
		crt_grp_priv_destroy(grp_priv);

out:
	return rc;
}

int
crt_group_destroy(crt_group_t *grp, crt_grp_destroy_cb_t grp_destroy_cb,
		  void *args)
{
	struct crt_grp_priv	*grp_priv = NULL;
	crt_rank_list_t	*member_ranks;
	crt_context_t		 crt_ctx;
	bool			 gd_req_sent = false;
	int			 i;
	int			 rc = 0;

	if (grp == NULL) {
		C_ERROR("invalid paramete of NULL grp.\n");
		C_GOTO(out, rc = -CER_INVAL);
	}
	grp_priv = container_of(grp, struct crt_grp_priv, gp_pub);

	pthread_rwlock_rdlock(&crt_grp_list_rwlock);
	if (grp_priv->gp_status != CRT_GRP_NORMAL) {
		C_ERROR("group status: 0x%x, cannot be destroyed.\n",
			grp_priv->gp_status);
		pthread_rwlock_unlock(&crt_grp_list_rwlock);
		C_GOTO(out, rc = -CER_BUSY);
	}
	C_ASSERT(grp_priv->gp_rc == 0);
	member_ranks = grp_priv->gp_membs;
	C_ASSERT(member_ranks != NULL);
	grp_priv->gp_status = CRT_GRP_DESTROYING;
	grp_priv->gp_child_num = member_ranks->rl_nr.num;
	grp_priv->gp_child_ack_num = 0;
	grp_priv->gp_destroy_cb = grp_destroy_cb;
	grp_priv->gp_destroy_cb_arg = args;
	pthread_rwlock_unlock(&crt_grp_list_rwlock);

	crt_ctx = grp_priv->gp_ctx;
	C_ASSERT(crt_ctx != NULL);

	/* send RPC one by one now */
	for (i = 0; i < member_ranks->rl_nr.num; i++) {
		crt_rpc_t			*gd_rpc;
		struct crt_grp_destroy_in	*gd_in;
		crt_endpoint_t			 tgt_ep;

		tgt_ep.ep_grp = NULL;
		tgt_ep.ep_rank = member_ranks->rl_ranks[i];
		tgt_ep.ep_tag = 0;
		rc = crt_req_create(crt_ctx, tgt_ep, CRT_OPC_GRP_DESTROY,
				    &gd_rpc);
		if (rc != 0) {
			C_ERROR("crt_req_create(CRT_OPC_GRP_DESTROY) failed, "
				"tgt_ep: %d, rc: %d.\n", tgt_ep.ep_rank, rc);
			grp_priv->gp_child_ack_num +=
				grp_priv->gp_child_num - i;
			grp_priv->gp_rc = rc;
			C_GOTO(out, rc);
		}

		gd_in = crt_req_get(gd_rpc);
		C_ASSERT(gd_in != NULL);
		gd_in->gd_grp_id = grp->cg_grpid;
		crt_group_rank(NULL, &gd_in->gd_initiate_rank);

		rc = crt_req_send(gd_rpc, gd_rpc_cb, grp_priv);
		if (rc != 0) {
			C_ERROR("crt_req_send(CRT_OPC_GRP_DESTROY) failed, "
				"tgt_ep: %d, rc: %d.\n", tgt_ep.ep_rank, rc);
			grp_priv->gp_child_ack_num +=
				grp_priv->gp_child_num - i;
			grp_priv->gp_rc = rc;
			C_GOTO(out, rc);
		}

		gd_req_sent =  true;
	}

out:
	if (gd_req_sent == false) {
		C_ASSERT(rc != 0);
		C_ERROR("crt_group_destroy failed, rc: %d.\n", rc);

		if (grp_destroy_cb != NULL)
			grp_destroy_cb(args, rc);
	}
	return rc;
}

/* TODO - currently only with one global service group and one client group */
int
crt_group_rank(crt_group_t *grp, crt_rank_t *rank)
{
	struct crt_grp_gdata	*grp_gdata;

	grp_gdata = crt_gdata.cg_grp;
	if (grp_gdata == NULL) {
		C_ERROR("crt group un-initialized.\n");
		return -CER_UNINIT;
	}
	if (rank == NULL) {
		C_ERROR("invalid parameter of NULL rank pointer.\n");
		return -CER_INVAL;
	}

	/* now only support query the global group */
	if (grp == NULL)
		*rank = crt_is_service() ? grp_gdata->gg_srv_pri_grp->gp_self :
			grp_gdata->gg_cli_pri_grp->gp_self;
	else
		return -CER_NOSYS;

	return 0;
}

int
crt_group_size(crt_group_t *grp, uint32_t *size)
{
	struct crt_grp_gdata	*grp_gdata;

	grp_gdata = crt_gdata.cg_grp;
	if (grp_gdata == NULL) {
		C_ERROR("crt group un-initialized.\n");
		return -CER_UNINIT;
	}
	if (size == NULL) {
		C_ERROR("invalid parameter of NULL size pointer.\n");
		return -CER_INVAL;
	}

	/* now only support query the global group */
	if (grp == NULL)
		*size = crt_is_service() ? grp_gdata->gg_srv_pri_grp->gp_size :
			grp_gdata->gg_cli_pri_grp->gp_size;
	else
		return -CER_NOSYS;

	return 0;
}

crt_group_id_t
crt_global_grp_id(void)
{
	return crt_is_service() ? crt_gdata.cg_srv_grp_id :
				  crt_gdata.cg_cli_grp_id;
}

static int
crt_primary_grp_init(void)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_pmix_gdata	*pmix_gdata;
	struct crt_grp_priv	*grp_priv = NULL;
	crt_group_id_t		 grp_id;
	crt_group_t		*srv_grp = NULL;
	bool			 is_service;
	int			 rc = 0;

	grp_gdata = crt_gdata.cg_grp;
	C_ASSERT(grp_gdata != NULL);
	pmix_gdata = grp_gdata->gg_pmix;
	C_ASSERT(grp_gdata->gg_pmix_inited == 1);
	C_ASSERT(pmix_gdata != NULL);

	is_service = crt_is_service();
	grp_id = is_service ? CRT_GLOBAL_SRV_GROUP_NAME : CRT_CLI_GROUP_NAME;
	rc = crt_grp_priv_create(&grp_priv, grp_id, true /* primary group */,
				 NULL /* member_ranks */,
				 NULL /* grp_create_cb */, NULL /* priv */);
	if (rc != 0) {
		C_ERROR("crt_grp_priv_create failed, rc: %d.\n", rc);
		C_GOTO(out, rc);
	}
	C_ASSERT(grp_priv != NULL);
	grp_priv->gp_status = CRT_GRP_NORMAL;
	grp_priv->gp_local = 1;
	grp_priv->gp_service = is_service;

	rc = crt_pmix_assign_rank(grp_priv);
	if (rc != 0)
		C_GOTO(out, rc);

	rc = crt_pmix_publish_self(grp_priv);
	if (rc != 0)
		C_GOTO(out, rc);

	rc = crt_pmix_fence();
	if (rc != 0)
		C_GOTO(out, rc);

	if (is_service) {
		grp_gdata->gg_srv_pri_grp = grp_priv;
		rc = crt_grp_lc_create(grp_gdata->gg_srv_pri_grp);
	} else {
		grp_gdata->gg_cli_pri_grp = grp_priv;
		rc = crt_group_attach(CRT_GLOBAL_SRV_GROUP_NAME, &srv_grp);
		if (rc != 0) {
			C_ERROR("failed to attach to %s, rc: %d.\n",
				CRT_GLOBAL_SRV_GROUP_NAME, rc);
			C_GOTO(out, rc);
		}
		C_ASSERT(srv_grp != NULL);
		grp_gdata->gg_srv_pri_grp =
			container_of(srv_grp, struct crt_grp_priv, gp_pub);
		rc = crt_grp_lc_create(grp_gdata->gg_srv_pri_grp);
	}

out:
	if (rc != 0) {
		C_ERROR("crt_primary_grp_init failed, rc: %d.\n", rc);
		if (grp_priv == NULL)
			crt_grp_priv_destroy(grp_priv);
	}

	return rc;
}

static int
crt_primary_grp_fini(void)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_pmix_gdata	*pmix_gdata;
	int			 rc = 0;

	grp_gdata = crt_gdata.cg_grp;
	C_ASSERT(grp_gdata != NULL);
	pmix_gdata = grp_gdata->gg_pmix;
	C_ASSERT(grp_gdata->gg_pmix_inited == 1);
	C_ASSERT(pmix_gdata != NULL);

	rc = crt_grp_lc_destroy(grp_gdata->gg_srv_pri_grp);
	if (rc != 0)
		C_GOTO(out, rc);

	if (crt_is_service()) {
		crt_grp_priv_destroy(grp_gdata->gg_srv_pri_grp);
	} else {
		/* TODO detach */
		crt_grp_priv_destroy(grp_gdata->gg_cli_pri_grp);
	}

out:
	if (rc != 0)
		C_ERROR("crt_primary_grp_fini failed, rc: %d.\n", rc);
	return rc;
}

int
crt_hdlr_uri_lookup(crt_rpc_t *rpc_req)
{
	struct crt_grp_priv		*grp_priv;
	struct crt_hg_context		*hg_ctx;
	struct crt_uri_lookup_in	*ul_in;
	struct crt_uri_lookup_out	*ul_out;
	int				rc = 0;

	C_ASSERT(rpc_req != NULL);
	ul_in = crt_req_get(rpc_req);
	ul_out = crt_reply_get(rpc_req);
	C_ASSERT(ul_in != NULL && ul_out != NULL);

	if (!crt_is_service()) {
		C_ERROR("crt_hdlr_uri_lookup invalid on client.\n");
		rc = -CER_PROTO;
	}
	grp_priv = crt_gdata.cg_grp->gg_srv_pri_grp;
	if (strncmp(ul_in->ul_grp_id, grp_priv->gp_pub.cg_grpid,
		    CRT_GROUP_ID_MAX_LEN) != 0) {
		C_ERROR("ul_grp_id %s mismatch with gg_srv_pri_grp %s.\n",
			ul_in->ul_grp_id, grp_priv->gp_pub.cg_grpid);
		rc = -CER_INVAL;
	}
	if (rc != 0) {
		ul_out->ul_uri = NULL;
		C_GOTO(out, rc = 0);
	}

	hg_ctx = &((struct crt_context *)rpc_req->dr_ctx)->dc_hg_ctx;
	rc = crt_grp_lc_lookup(grp_priv, hg_ctx, ul_in->ul_rank, 0 /* tag */,
			       &ul_out->ul_uri, NULL /* na_addr */);
	if (rc != 0)
		C_ERROR("crt_grp_lc_lookup rank %d failed, rc: %d.\n",
			ul_in->ul_rank, rc);

out:
	ul_out->ul_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		C_ERROR("crt_reply_send failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_req->dr_opc);
	return rc;
}

int
crt_grp_uri_lookup(struct crt_grp_priv *grp_priv, crt_rank_t rank, char **uri)
{
	crt_rpc_t			*rpc_req = NULL;
	struct crt_uri_lookup_in	*ul_in;
	struct crt_uri_lookup_out	*ul_out;
	struct crt_grp_gdata		*grp_gdata;
	struct crt_pmix_gdata		*pmix_gdata;
	crt_context_t			 crt_ctx;
	crt_endpoint_t			 svr_ep;
	crt_group_id_t			 grp_id;
	int				 rc = 0;

	C_ASSERT(uri != NULL);

	grp_gdata = crt_gdata.cg_grp;
	C_ASSERT(grp_gdata != NULL);
	pmix_gdata = grp_gdata->gg_pmix;
	C_ASSERT(grp_gdata->gg_pmix_inited == 1);
	C_ASSERT(pmix_gdata != NULL);

	if (grp_priv == NULL)
		grp_id = CRT_GLOBAL_SRV_GROUP_NAME;
	else
		grp_id = grp_priv->gp_pub.cg_grpid;

	if (grp_priv->gp_local == 0) {
		/* attached group, for PSR just return the gp_psr_phy_addr, for
		 * other rank will send RPC to PSR */
		if (rank == grp_priv->gp_psr_rank) {
			*uri = strndup(grp_priv->gp_psr_phy_addr,
				       CRT_ADDR_STR_MAX_LEN);
			if (*uri == NULL) {
				C_ERROR("strndup gp_psr_phy_addr failed.\n");
				rc = -CER_NOMEM;
			}
			C_GOTO(out, rc);
		}
		crt_ctx = crt_context_lookup(0);
		C_ASSERT(crt_ctx != NULL);

		svr_ep.ep_grp = &grp_priv->gp_pub;
		svr_ep.ep_rank = grp_priv->gp_psr_rank;
		svr_ep.ep_tag = 0;
		rc = crt_req_create(crt_ctx, svr_ep, CRT_OPC_URI_LOOKUP,
				    &rpc_req);
		if (rc != 0) {
			C_ERROR("crt_req_create URI_LOOKUP failed, rc: %d.\n",
				rc);
			C_GOTO(out, rc);
		}
		ul_in = crt_req_get(rpc_req);
		ul_out = crt_reply_get(rpc_req);
		C_ASSERT(ul_in != NULL && ul_out != NULL);
		ul_in->ul_grp_id = grp_id;
		ul_in->ul_rank = rank;

		crt_req_addref(rpc_req);
		rc = crt_sync_req(rpc_req, CRT_URI_LOOKUP_TIMEOUT);
		if (rc != 0) {
			C_ERROR("crt_sync_req URI_LOOKUP failed, rc: %d.\n",
				rc);
			crt_req_decref(rpc_req);
			C_GOTO(out, rc);
		}

		if (ul_out->ul_rc != 0) {
			C_ERROR("crt_sync_req URI_LOOKUP reply rc: %d.\n",
				ul_out->ul_rc);
			rc = ul_out->ul_rc;
		} else {
			*uri = strndup(ul_out->ul_uri, CRT_ADDR_STR_MAX_LEN);
			if (*uri == NULL) {
				C_ERROR("strndup gp_psr_phy_addr failed.\n");
				rc = -CER_NOMEM;
			}
		}
		crt_req_decref(rpc_req);
	} else {
		/* server side directly lookup through PMIx */
		rc = crt_pmix_uri_lookup(grp_id, rank, uri);
	}

out:
	if (rc != 0)
		C_ERROR("crt_grp_uri_lookup(grp_id %s, rank %d) failed, "
			"rc: %d.\n", grp_id, rank, rc);
	return rc;
}

int
crt_group_attach(crt_group_id_t srv_grpid, crt_group_t **attached_grp)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_grp_priv	*grp_priv = NULL;
	size_t			 len;
	int			 rc = 0;

	if (srv_grpid == NULL) {
		C_ERROR("invalid parameter, NULL srv_grpid.\n");
		C_GOTO(out, rc = -CER_INVAL);
	}
	len = strlen(srv_grpid);
	if (len == 0 || len > CRT_GROUP_ID_MAX_LEN) {
		C_ERROR("invalid srv_grpid %s (len %zu).\n", srv_grpid, len);
		C_GOTO(out, rc = -CER_INVAL);
	}
	if (attached_grp == NULL) {
		C_ERROR("invalid parameter, NULL attached_grp.\n");
		C_GOTO(out, rc = -CER_INVAL);
	}

	grp_gdata = crt_gdata.cg_grp;
	if (grp_gdata == NULL) {
		C_ERROR("crt group un-initialized.\n");
		C_GOTO(out, rc = -CER_UNINIT);
	}

	rc = crt_grp_priv_create(&grp_priv, srv_grpid, true /* primary group */,
				 NULL /* member_ranks */,
				 NULL /* grp_create_cb */, NULL /* priv */);
	if (rc != 0) {
		C_ERROR("crt_grp_priv_create failed, rc: %d.\n", rc);
		C_GOTO(out, rc);
	}
	C_ASSERT(grp_priv != NULL);
	grp_priv->gp_status = CRT_GRP_NORMAL;
	grp_priv->gp_local = 0;
	grp_priv->gp_service = 1;

	rc = crt_pmix_attach(grp_priv);
	if (rc != 0) {
		C_ERROR("crt_pmix_attach GROUP %s failed, rc: %d.\n",
			srv_grpid, rc);
		C_GOTO(out, rc);
	}

	*attached_grp = &grp_priv->gp_pub;

out:
	if (rc != 0) {
		C_ERROR("crt_group_attach, failed, rc: %d.\n", rc);
		if (grp_priv == NULL)
			crt_grp_priv_destroy(grp_priv);
	}

	return rc;
}

int
crt_grp_init(void)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_pmix_gdata	*pmix_gdata;
	int			 rc = 0;

	C_ASSERT(crt_gdata.cg_grp_inited == 0);
	C_ASSERT(crt_gdata.cg_grp == NULL);

	C_ALLOC_PTR(grp_gdata);
	if (grp_gdata == NULL)
		C_GOTO(out, rc = -CER_NOMEM);

	CRT_INIT_LIST_HEAD(&grp_gdata->gg_cli_grps_attached);
	CRT_INIT_LIST_HEAD(&grp_gdata->gg_srv_grps_attached);
	CRT_INIT_LIST_HEAD(&grp_gdata->gg_sub_grps);
	pthread_rwlock_init(&grp_gdata->gg_rwlock, NULL);

	crt_gdata.cg_grp = grp_gdata;

	rc = crt_pmix_init();
	if (rc != 0)
		C_GOTO(out, rc);
	pmix_gdata = grp_gdata->gg_pmix;
	C_ASSERT(grp_gdata->gg_pmix_inited == 1);
	C_ASSERT(pmix_gdata != NULL);

	rc = crt_primary_grp_init();
	if (rc != 0) {
		crt_pmix_fini();
		C_GOTO(out, rc);
	}

	grp_gdata->gg_inited = 1;
	crt_gdata.cg_grp_inited = 1;

out:
	if (rc != 0) {
		C_ERROR("crt_grp_init failed, rc: %d.\n", rc);
		if (grp_gdata != NULL)
			C_FREE_PTR(grp_gdata);
		crt_gdata.cg_grp = NULL;
	}
	return rc;
}

int
crt_grp_fini(void)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_pmix_gdata	*pmix_gdata;
	int			 rc = 0;

	C_ASSERT(crt_gdata.cg_grp_inited == 1);
	C_ASSERT(crt_gdata.cg_grp != NULL);
	grp_gdata = crt_gdata.cg_grp;
	pmix_gdata = grp_gdata->gg_pmix;
	C_ASSERT(pmix_gdata != NULL);

	rc = crt_primary_grp_fini();
	if (rc != 0)
		C_GOTO(out, rc);

	rc = crt_pmix_fini();
	if (rc != 0)
		C_GOTO(out, rc);

	pthread_rwlock_destroy(&grp_gdata->gg_rwlock);
	C_FREE_PTR(grp_gdata);
	crt_gdata.cg_grp = NULL;
	crt_gdata.cg_grp_inited = 0;

out:
	if (rc != 0)
		C_ERROR("crt_grp_fini failed, rc: %d.\n", rc);
	return rc;
}
