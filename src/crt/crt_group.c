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
 * This file is part of CaRT. It implements the main group APIs.
 */

#include <crt_internal.h>

/* global CRT group list */
CRT_LIST_HEAD(crt_grp_list);
/* protect global group list */
pthread_rwlock_t crt_grp_list_rwlock = PTHREAD_RWLOCK_INITIALIZER;

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
		if (crt_grp_id_identical(grp_priv->gp_pub.dg_grpid,
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

static inline int
crt_grp_priv_create(struct crt_grp_priv **grp_priv_created,
		    crt_group_id_t grp_id, crt_rank_list_t *membs,
		    crt_grp_create_cb_t grp_create_cb, void *priv)
{
	struct crt_grp_priv *grp_priv;
	int	rc = 0;

	C_ASSERT(grp_priv_created != NULL && membs != NULL);
	C_ASSERT(grp_id != NULL && strlen(grp_id) > 0 &&
		 strlen(grp_id) < CRT_GROUP_ID_MAX_LEN);

	C_ALLOC_PTR(grp_priv);
	if (grp_priv == NULL)
		C_GOTO(out, rc = -CER_NOMEM);

	CRT_INIT_LIST_HEAD(&grp_priv->gp_link);
	grp_priv->gp_pub.dg_grpid = strdup(grp_id);
	if (grp_priv->gp_pub.dg_grpid == NULL) {
		C_ERROR("strdup grp_id (%s) failed.\n", grp_id);
		C_FREE_PTR(grp_priv);
		C_GOTO(out, rc = -CER_NOMEM);
	}
	rc = crt_rank_list_dup(&grp_priv->gp_membs, membs, true /* input */);
	if (rc != 0) {
		C_ERROR("crt_rank_list_dup failed, rc: %d.\n", rc);
		C_FREE(grp_priv->gp_pub.dg_grpid, strlen(grp_id));
		C_FREE_PTR(grp_priv);
		C_GOTO(out, rc);
	}
	crt_rank_list_sort(grp_priv->gp_membs);
	grp_priv->gp_priv = priv;
	grp_priv->gp_status = CRT_GRP_CREATING;
	grp_priv->gp_parent_rpc = NULL;
	CRT_INIT_LIST_HEAD(&grp_priv->gp_child_rpcs);
	grp_priv->gp_child_num = membs->rl_nr.num; /* TODO tree children num */
	grp_priv->gp_child_ack_num = 0;
	grp_priv->gp_failed_ranks = NULL;
	grp_priv->gp_create_cb = grp_create_cb;
	pthread_mutex_init(&grp_priv->gp_mutex, NULL);

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
		C_DEBUG(CF_TP, "group existed or in creating/destroying.\n");
		pthread_rwlock_unlock(&crt_grp_list_rwlock);
		*grp_result = grp_priv;
		C_GOTO(out, rc = -CER_EXIST);
	}

	rc = crt_grp_priv_create(&grp_priv, grp_id, member_ranks,
				 grp_create_cb, priv);
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

	crt_rank_list_free(grp_priv->gp_membs);
	crt_rank_list_free(grp_priv->gp_failed_ranks);
	pthread_mutex_destroy(&grp_priv->gp_mutex);
	C_FREE(grp_priv->gp_pub.dg_grpid, strlen(grp_priv->gp_pub.dg_grpid));

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

	pthread_mutex_lock(&grp_priv->gp_mutex);
	crt_list_add_tail(&gc_req_item->gc_link, &grp_priv->gp_child_rpcs);
	pthread_mutex_unlock(&grp_priv->gp_mutex);

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

	pthread_mutex_lock(&grp_priv->gp_mutex);
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
	pthread_mutex_unlock(&grp_priv->gp_mutex);
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

	pthread_mutex_lock(&grp_priv->gp_mutex);
	if (rc != 0 || gc_out->gc_rc != 0)
		grp_priv->gp_rc = (rc == 0) ? gc_out->gc_rc : rc;
	grp_priv->gp_child_ack_num++;
	C_ASSERT(grp_priv->gp_child_ack_num <= grp_priv->gp_child_num);
	if (grp_priv->gp_child_ack_num == grp_priv->gp_child_num)
		gc_done = true;
	pthread_mutex_unlock(&grp_priv->gp_mutex);

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
		C_DEBUG(CF_TP, "group non-exist.\n");
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
		C_DEBUG(CF_TP, "group non-exist.\n");
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

	pthread_mutex_lock(&grp_priv->gp_mutex);
	if (rc != 0 || gd_out->gd_rc != 0)
		grp_priv->gp_rc = (rc == 0) ? gd_out->gd_rc : rc;
	grp_priv->gp_child_ack_num++;
	C_ASSERT(grp_priv->gp_child_ack_num <= grp_priv->gp_child_num);
	if (grp_priv->gp_child_ack_num == grp_priv->gp_child_num)
		gd_done = true;
	pthread_mutex_unlock(&grp_priv->gp_mutex);

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
		gd_in->gd_grp_id = grp->dg_grpid;
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
	if (rank == NULL) {
		C_ERROR("invalid parameter of NULL rank pointer.\n");
		return -CER_INVAL;
	}

	/* now only support query the global group */
	if (grp == NULL)
		*rank = (crt_gdata.dg_server == true) ?
		crt_gdata.dg_mcl_srv_set->self :
		crt_gdata.dg_mcl_cli_set->self;
	else
		return -CER_NOSYS;

	return 0;
}

int
crt_group_size(crt_group_t *grp, uint32_t *size)
{
	if (size == NULL) {
		C_ERROR("invalid parameter of NULL size pointer.\n");
		return -CER_INVAL;
	}

	/* now only support query the global group */
	if (grp == NULL)
		*size = (crt_gdata.dg_server == true) ?
		crt_gdata.dg_mcl_srv_set->size :
		crt_gdata.dg_mcl_cli_set->size;
	else
		return -CER_NOSYS;

	return 0;
}

crt_group_id_t
crt_global_grp_id(void)
{
	return (crt_gdata.dg_server == true) ? crt_gdata.dg_srv_grp_id :
					       crt_gdata.dg_cli_grp_id;
}
