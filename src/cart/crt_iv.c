/* Copyright (C) 2016-2017 Intel Corporation
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
 * This file is part of CaRT. It implements IV APIs.
 */
/* TODO list for stage2:
 * - iv_ver is not passed to most calls
 * - root_node flag is not passed during fetch/update
 * - update aggregation
 * - sync/refresh called on all nodes; might want to exclude update path
 * - CRT_IV_CLASS features (crt_iv_class::ivc_feats) not implemented
 * - Use hash table for list of keys in progress
 * - Support of endian-agnostic ivns_internal
 * - Optimize group lookup by using internal group id
 **/
#define D_LOGFAC	DD_FAC(corpc)

#include "crt_internal.h"
#include "cart/iv.h"

static D_LIST_HEAD(ns_list);
static uint32_t ns_id;

/* Lock for manimuplation of ns_list and ns_id */
static pthread_mutex_t ns_list_lock = PTHREAD_MUTEX_INITIALIZER;

/* Data structure for internal iv fetch rpc input */
struct iv_fetch_in {
	/* Namespace ID */
	d_iov_t		ifi_nsid;
	/* IV Key */
	d_iov_t		ifi_key;
	/* Bulk handle for iv value */
	crt_bulk_t	ifi_value_bulk;
	/* Class id */
	uint32_t	ifi_class_id;
	/* Root node for current fetch operation */
	d_rank_t	ifi_root_node;
};

/* Data structure for internal iv fetch rpc output*/
struct iv_fetch_out {
	/* Resultant return code of fetch rpc */
	uint32_t	ifo_rc;
};

/* Structure for uniquely identifying iv namespace */
struct crt_ivns_id {
	/* Rank of the namespace */
	d_rank_t	ii_rank;
	/* Unique ID within the rank */
	uint32_t	ii_nsid;
};

/* Structure for storing/passing of global namespace */
struct crt_global_ns {
	/* Namespace ID */
	struct crt_ivns_id	gn_ivns_id;
	/* Number of classes for this namespace; used for sanity check */
	uint32_t		gn_num_class;
	/* Associated tree topology */
	int			gn_tree_topo;
	/* Associated group ID */
	/* TODO: user internal group id */
	crt_group_id_t		gn_grp_id;
};

/* Structure for iv fetch callback info */
struct iv_fetch_cb_info {
	/* Fetch completion callback function and its argument */
	crt_iv_comp_cb_t		 ifc_comp_cb;
	void				*ifc_comp_cb_arg;

	/* Local bulk handle for iv value */
	crt_bulk_t			 ifc_bulk_hdl;

	/* Optional child's rpc and childs bulk handle, if child exists */
	crt_rpc_t			*ifc_child_rpc;
	crt_bulk_t			 ifc_child_bulk;

	crt_iv_key_t			 ifc_iv_key;
	/* IV value */
	d_sg_list_t			 ifc_iv_value;

	/* IV namespace */
	struct crt_ivns_internal	*ifc_ivns_internal;

	/* Class ID for ivns_internal */
	uint32_t			 ifc_class_id;

	/* User private data */
	void				*ifc_user_priv;
};

/* Structure for storing of pending iv fetch operations */
struct pending_fetch {
	struct iv_fetch_cb_info		*pf_cb_info;

	/* Link to ivf_key_in_progress::kip_pending_fetch_list */
	d_list_t			 pf_link;
};

/* Struture for list of all pending fetches for given key */
struct ivf_key_in_progress {
	crt_iv_key_t	kip_key;
	d_list_t	kip_pending_fetch_list;
	pthread_mutex_t	kip_lock;

	/* Link to crt_ivns_internal::cii_keys_in_progress_list */
	d_list_t	kip_link;

	/* Payload for kip_key->iov_buf */
	uintptr_t	payload[0];
};

/* Internal ivns structure */
struct crt_ivns_internal {
	/* IV Classes registered with this iv namespace */
	struct crt_iv_class	*cii_iv_classes;
	/* Context associated with IV namesapce */
	crt_context_t		 cii_ctx;

	/* Group to which this namesapce belongs*/
	crt_group_t		*cii_grp;

	/* Global namespace identifier */
	struct crt_global_ns	 cii_gns;

	/* Cached info to avoid cart queries */
	d_rank_t		 cii_local_rank;
	uint32_t		 cii_group_size;

	/* Link list of all keys in progress */
	d_list_t		 cii_keys_in_progress_list;

	/* Lock for modification of pending list */
	pthread_mutex_t		 cii_lock;

	/* Link to ns_list */
	d_list_t		 cii_link;
};

static int
crt_ivf_bulk_transfer(struct crt_ivns_internal *ivns_internal,
		      uint32_t class_id, d_iov_t *iv_key,
		      d_sg_list_t *iv_value, crt_bulk_t dest_bulk,
		      crt_rpc_t *rpc, void *user_priv);

static struct crt_iv_ops *
crt_iv_ops_get(struct crt_ivns_internal *ivns_internal, uint32_t class_id);

static bool
crt_iv_keys_match(crt_iv_key_t *key1, crt_iv_key_t *key2)
{
	/* Those below are critical, unrecoverable errors */
	D_ASSERT(key1 != NULL);
	D_ASSERT(key2 != NULL);
	D_ASSERT(key1->iov_buf != NULL);
	D_ASSERT(key2->iov_buf != NULL);

	if (key1->iov_len != key2->iov_len)
		return false;

	if (memcmp(key1->iov_buf, key2->iov_buf, key1->iov_len) == 0)
		return true;

	return false;
}

/* Check if key is in progress */
static struct ivf_key_in_progress *
crt_ivf_key_in_progress_find(struct crt_ivns_internal *ivns,
			struct crt_iv_ops *ops, crt_iv_key_t *key)
{
	struct ivf_key_in_progress *entry;

	d_list_for_each_entry(entry, &ivns->cii_keys_in_progress_list,
			      kip_link) {
		/* Use keys_match callback if client provided one */
		if (ops->ivo_keys_match) {
			if (ops->ivo_keys_match(ivns, &entry->kip_key, key))
				return entry;
		} else {
			if (crt_iv_keys_match(&entry->kip_key, key))
				return entry;
		}
	}

	return NULL;
}

/* Mark key as being in progress */
static int
crt_ivf_key_in_progress_set(struct crt_ivns_internal *ivns,
			crt_iv_key_t *key)
{
	struct ivf_key_in_progress	*entry;
	int				rc = 0;

	D_ALLOC(entry, offsetof(struct ivf_key_in_progress,
				payload[0]) + key->iov_buf_len);
	if (entry == NULL)
		return -DER_NOMEM;

	pthread_mutex_init(&entry->kip_lock, 0);

	entry->kip_key.iov_buf = entry->payload;
	entry->kip_key.iov_buf_len = key->iov_buf_len;
	entry->kip_key.iov_len = key->iov_len;

	memcpy(entry->kip_key.iov_buf, key->iov_buf, key->iov_buf_len);
	D_INIT_LIST_HEAD(&entry->kip_pending_fetch_list);

	/* TODO: Change to hash table */
	d_list_add_tail(&entry->kip_link, &ivns->cii_keys_in_progress_list);

	return rc;
}

/* Reverse operation of crt_ivf_key_in_progress_set */
static int
crt_ivf_key_in_progress_unset(struct ivf_key_in_progress *entry,
			crt_iv_key_t *key)
{
	if (!entry)
		return 0;

	pthread_mutex_destroy(&entry->kip_lock);
	d_list_del(&entry->kip_link);
	D_FREE(entry);

	return 0;
}

/* Add key to the list of pending requests */
static int
crt_ivf_pending_request_add(
			struct crt_ivns_internal *ivns_internal,
			struct crt_iv_ops *iv_ops,
			struct ivf_key_in_progress *entry,
			struct iv_fetch_cb_info *iv_info)
{
	struct pending_fetch	*pending_fetch;

	/* ivo_on_get() was done by the caller of crt_ivf_rpc_issue */
	iv_ops->ivo_on_put(ivns_internal, &iv_info->ifc_iv_value,
				iv_info->ifc_user_priv);

	D_ALLOC_PTR(pending_fetch);
	if (pending_fetch == NULL)
		return -DER_NOMEM;

	pending_fetch->pf_cb_info = iv_info;

	d_list_add_tail(&pending_fetch->pf_link,
			&entry->kip_pending_fetch_list);
	return 0;
}

/* Finalize fetch operation by either performing bulk transfer or
 * invoking fetch completion callback
 */
static int
crt_ivf_finalize(struct iv_fetch_cb_info *iv_info, crt_iv_key_t *iv_key,
		 d_sg_list_t *iv_value, int output_rc)
{
	crt_rpc_t		*rpc;
	int			 rc = 0;
	struct crt_iv_ops	*iv_ops;

	rpc = iv_info->ifc_child_rpc;
	iv_ops = crt_iv_ops_get(iv_info->ifc_ivns_internal,
				iv_info->ifc_class_id);

	if (rpc) {
		/* If there is child to respond to - bulk transfer to it */
		if (output_rc == 0) {
			/* Note: function will increment ref count on 'rpc' */
			rc = crt_ivf_bulk_transfer(iv_info->ifc_ivns_internal,
						iv_info->ifc_class_id,
						iv_key, iv_value,
						iv_info->ifc_child_bulk,
						rpc,
						iv_info->ifc_user_priv);
		} else {
			struct iv_fetch_out *output;

			iv_ops->ivo_on_put(iv_info->ifc_ivns_internal,
					   iv_value, iv_info->ifc_user_priv);

			output = crt_reply_get(rpc);
			output->ifo_rc = output_rc;

			rc = crt_reply_send(rpc);
			D_ASSERT(rc == 0);
		}

		/* addref done in crt_hdlr_iv_fetch */
		rc = crt_req_decref(rpc);
		D_ASSERT(rc == 0);
	} else {
		iv_info->ifc_comp_cb(iv_info->ifc_ivns_internal,
					iv_info->ifc_class_id,
					iv_key, NULL,
					iv_value,
					output_rc,
					iv_info->ifc_comp_cb_arg);
		iv_ops->ivo_on_put(iv_info->ifc_ivns_internal,
				   iv_value, iv_info->ifc_user_priv);
	}

	return rc;
}

/* Process pending requests for the specified ivns and key */
static int
crt_ivf_pending_reqs_process(struct crt_ivns_internal *ivns_internal,
				uint32_t class_id, crt_iv_key_t *key,
				uint32_t rc_value)
{
	struct ivf_key_in_progress	*entry;
	struct crt_iv_ops		*iv_ops;
	struct pending_fetch		*pending_fetch, *next;
	struct iv_fetch_cb_info		*iv_info;
	struct iv_fetch_out		*output;
	int				 rc = 0;
	bool				 put_needed = false;

	iv_ops = crt_iv_ops_get(ivns_internal, class_id);
	D_ASSERT(iv_ops != NULL);

	pthread_mutex_lock(&ivns_internal->cii_lock);
	entry = crt_ivf_key_in_progress_find(ivns_internal, iv_ops, key);
	pthread_mutex_unlock(&ivns_internal->cii_lock);

	/* Key is not in progress - safe to exit */
	if (!entry)
		D_GOTO(exit, rc);

	pthread_mutex_lock(&entry->kip_lock);

	/* If there is nothing pending - exit */
	if (d_list_empty(&entry->kip_pending_fetch_list))
		D_GOTO(cleanup, rc);

	/* Go through list of all pending fetches and finalize each one */
	d_list_for_each_entry_safe(pending_fetch, next,
				  &entry->kip_pending_fetch_list, pf_link) {
		d_sg_list_t tmp_iv_value = {0};

		iv_info = pending_fetch->pf_cb_info;


		/* Pending remote fetch case */
		if (iv_info->ifc_child_rpc) {

			/* For failed fetches respond to the child with error */
			if (rc_value != 0) {
				output = crt_reply_get(iv_info->ifc_child_rpc);

				output->ifo_rc = rc_value;

				/* Failing to send response isn't fatal */
				rc = crt_reply_send(iv_info->ifc_child_rpc);
				if (rc != 0)
					D_ERROR("crt_reply_send() rc=%d\n",
						rc);

				/* addref done in crt_hdlr_iv_fetch */
				crt_req_decref(iv_info->ifc_child_rpc);

				d_list_del(&pending_fetch->pf_link);
				D_FREE(pending_fetch->pf_cb_info);
				D_FREE(pending_fetch);
				continue;
			}

			rc = iv_ops->ivo_on_get(ivns_internal,
					&iv_info->ifc_iv_key, 0,
					CRT_IV_PERM_READ,
					&tmp_iv_value, &iv_info->ifc_user_priv);

			put_needed = false;
			if (rc == 0) {
				put_needed = true;
				rc = iv_ops->ivo_on_fetch(ivns_internal,
					&iv_info->ifc_iv_key, 0x0,
					CRT_IV_FLAG_PENDING_FETCH,
					&tmp_iv_value, iv_info->ifc_user_priv);
			}

			if (rc == 0) {
				crt_ivf_bulk_transfer(ivns_internal,
					class_id, &iv_info->ifc_iv_key,
					&tmp_iv_value,
					iv_info->ifc_child_bulk,
					iv_info->ifc_child_rpc,
					iv_info->ifc_user_priv);

			} else {
				D_ERROR("Failed to process pending request\n");

				output = crt_reply_get(iv_info->ifc_child_rpc);

				output->ifo_rc = rc;
				crt_reply_send(iv_info->ifc_child_rpc);

				if (put_needed) {
					iv_ops->ivo_on_put(ivns_internal,
						&tmp_iv_value,
						iv_info->ifc_user_priv);
				}
			}

			/* addref done in crt_hdlr_iv_fetch */
			crt_req_decref(iv_info->ifc_child_rpc);

		} else {
			if (rc_value != 0) {
				iv_info->ifc_comp_cb(ivns_internal, class_id,
					&iv_info->ifc_iv_key, NULL,
					&tmp_iv_value, rc_value,
					iv_info->ifc_comp_cb_arg);

				d_list_del(&pending_fetch->pf_link);
				D_FREE(pending_fetch->pf_cb_info);
				D_FREE(pending_fetch);
				continue;
			}

			/* Pending local fetch case */
			rc = iv_ops->ivo_on_get(ivns_internal,
					&iv_info->ifc_iv_key,
					0, CRT_IV_PERM_READ, &tmp_iv_value,
					&iv_info->ifc_user_priv);

			put_needed = false;

			if (rc == 0) {
				put_needed = true;

				rc = iv_ops->ivo_on_fetch(ivns_internal,
						&iv_info->ifc_iv_key,
						0x0,
						CRT_IV_FLAG_PENDING_FETCH,
						&tmp_iv_value,
						iv_info->ifc_user_priv);
			} else {
				rc_value = rc;
			}

			iv_info->ifc_comp_cb(ivns_internal, class_id,
					&iv_info->ifc_iv_key, NULL,
					&tmp_iv_value, rc_value,
					iv_info->ifc_comp_cb_arg);

			if (put_needed)
				iv_ops->ivo_on_put(ivns_internal, &tmp_iv_value,
					   iv_info->ifc_user_priv);
		}

		d_list_del(&pending_fetch->pf_link);
		D_FREE(pending_fetch->pf_cb_info);
		D_FREE(pending_fetch);
	}

cleanup:
	pthread_mutex_unlock(&entry->kip_lock);
	crt_ivf_key_in_progress_unset(entry, key);
exit:
	return rc;
}

/* Helper function to lookup ivns_internal based on ivns id */
static struct crt_ivns_internal *
crt_ivns_internal_lookup(struct crt_ivns_id *ivns_id)
{
	struct crt_ivns_internal *entry;

	pthread_mutex_lock(&ns_list_lock);
	d_list_for_each_entry(entry, &ns_list, cii_link) {
		if (entry->cii_gns.gn_ivns_id.ii_rank == ivns_id->ii_rank &&
			entry->cii_gns.gn_ivns_id.ii_nsid == ivns_id->ii_nsid) {

			pthread_mutex_unlock(&ns_list_lock);
			return entry;
		}
	}
	pthread_mutex_unlock(&ns_list_lock);

	return NULL;
}

/* Return internal ivns based on passed ivns */
static struct crt_ivns_internal *
crt_ivns_internal_get(crt_iv_namespace_t ivns)
{
	struct crt_ivns_internal *ivns_internal;

	ivns_internal = (struct crt_ivns_internal *)ivns;

	/* Perform lookup for verification purposes */
	return crt_ivns_internal_lookup(&ivns_internal->cii_gns.gn_ivns_id);
}

/* Allocate and populate new ivns internal structure. This function is
 * called both when creating new ivns and attaching existing global ivns
 */
static struct crt_ivns_internal *
crt_ivns_internal_create(crt_context_t crt_ctx, crt_group_t *grp,
		struct crt_iv_class *iv_classes, uint32_t num_class,
		int tree_topo, struct crt_ivns_id *ivns_id)
{
	struct crt_ivns_internal	*ivns_internal;
	struct crt_ivns_id		*internal_ivns_id;
	uint32_t			next_ns_id;
	int				rc = 0;

	D_ALLOC_PTR(ivns_internal);
	if (ivns_internal == NULL)
		D_GOTO(exit, 0);

	D_ALLOC_ARRAY(ivns_internal->cii_iv_classes, num_class);
	if (ivns_internal->cii_iv_classes == NULL) {
		D_FREE(ivns_internal);
		D_GOTO(exit, 0);
	}

	D_INIT_LIST_HEAD(&ivns_internal->cii_keys_in_progress_list);

	rc = crt_group_rank(grp, &ivns_internal->cii_local_rank);
	D_ASSERT(rc == 0);

	rc = crt_group_size(grp, &ivns_internal->cii_group_size);
	D_ASSERT(rc == 0);

	pthread_mutex_init(&ivns_internal->cii_lock, 0);

	internal_ivns_id = &ivns_internal->cii_gns.gn_ivns_id;

	/* If we are not passed an ivns_id, create new one */
	if (ivns_id == NULL) {
		pthread_mutex_lock(&ns_list_lock);
		next_ns_id = ns_id;
		ns_id++;
		pthread_mutex_unlock(&ns_list_lock);

		internal_ivns_id->ii_rank = ivns_internal->cii_local_rank;
		internal_ivns_id->ii_nsid = next_ns_id;
	} else {
		/* We are attaching ivns, created by someone else */
		internal_ivns_id->ii_rank = ivns_id->ii_rank;
		internal_ivns_id->ii_nsid = ivns_id->ii_nsid;
	}

	memcpy(ivns_internal->cii_iv_classes, iv_classes,
	       sizeof(*iv_classes) * num_class);

	ivns_internal->cii_gns.gn_num_class = num_class;
	ivns_internal->cii_gns.gn_tree_topo = tree_topo;
	ivns_internal->cii_ctx = crt_ctx;

	ivns_internal->cii_grp = grp;

	if (grp == NULL)
		ivns_internal->cii_gns.gn_grp_id = NULL;
	else
		ivns_internal->cii_gns.gn_grp_id = grp->cg_grpid;

	pthread_mutex_lock(&ns_list_lock);
	d_list_add_tail(&ivns_internal->cii_link, &ns_list);
	pthread_mutex_unlock(&ns_list_lock);

exit:
	return ivns_internal;
}

int
crt_iv_namespace_create(crt_context_t crt_ctx, crt_group_t *grp, int tree_topo,
			struct crt_iv_class *iv_classes, uint32_t num_class,
			crt_iv_namespace_t *ivns, d_iov_t *g_ivns)
{
	struct crt_ivns_internal	*ivns_internal = NULL;
	int				rc = 0;

	if (ivns == NULL || g_ivns == NULL) {
		D_ERROR("invalid parameter of NULL for ivns/g_ivns\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	ivns_internal = crt_ivns_internal_create(crt_ctx, grp,
						iv_classes, num_class,
						tree_topo, NULL);
	if (ivns_internal == NULL) {
		D_ERROR("Failed to create internal ivns\n");
		D_GOTO(exit, rc = -DER_NOMEM);
	}

	*ivns = (crt_iv_namespace_t)ivns_internal;

	/* TODO: Need to flatten the structure */
	g_ivns->iov_buf = &ivns_internal->cii_gns;
	g_ivns->iov_buf_len = sizeof(struct crt_global_ns);
	g_ivns->iov_len = sizeof(struct crt_global_ns);

exit:
	if (rc != 0 && ivns_internal)
		D_FREE_PTR(ivns_internal);

	return rc;
}

int
crt_iv_namespace_attach(crt_context_t crt_ctx, d_iov_t *g_ivns,
			struct crt_iv_class *iv_classes, uint32_t num_class,
			crt_iv_namespace_t *ivns)
{
	struct crt_ivns_internal	*ivns_internal = NULL;
	struct crt_global_ns		*ivns_global = NULL;
	crt_group_t			*grp;
	int				rc = 0;

	if (g_ivns == NULL) {
		D_ERROR("global ivns is NULL\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	if (iv_classes == NULL) {
		D_ERROR("iv_classes is NULL\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	if (ivns == NULL) {
		D_ERROR("ivns is NULL\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	/* TODO: Need to unflatten the structure */
	ivns_global = (struct crt_global_ns *)g_ivns->iov_buf;

	grp = crt_group_lookup(ivns_global->gn_grp_id);

	ivns_internal = crt_ivns_internal_create(crt_ctx, grp,
					iv_classes, num_class,
					ivns_global->gn_tree_topo,
					&ivns_global->gn_ivns_id);
	if (ivns_internal == NULL) {
		D_ERROR("Failed to create new ivns internal\n");
		D_GOTO(exit, rc = -DER_NOMEM);
	}

	*ivns = (crt_iv_namespace_t)ivns_internal;

exit:
	return rc;
}

int
crt_iv_namespace_destroy(crt_iv_namespace_t ivns)
{
	struct crt_ivns_internal	*ivns_internal;
	int				rc = 0;

	ivns_internal = crt_ivns_internal_get(ivns);
	if (ivns_internal == NULL) {
		D_ERROR("Invalid ivns passed\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	pthread_mutex_lock(&ns_list_lock);
	d_list_del(&ivns_internal->cii_link);
	pthread_mutex_unlock(&ns_list_lock);

	/* TODO: stage2 - wait for all pending requests to be finished*/
	/* TODO: Need refcount on ivns_internal to know when to free it */
	pthread_mutex_destroy(&ivns_internal->cii_lock);

	D_FREE_PTR(ivns_internal->cii_iv_classes);
	D_FREE_PTR(ivns_internal);

exit:
	return rc;
}

/* Return iv_ops based on class_id passed */
static struct crt_iv_ops *
crt_iv_ops_get(struct crt_ivns_internal *ivns_internal, uint32_t class_id)
{
	if (ivns_internal == NULL) {
		D_ERROR("ivns_internal was NULL\n");
		return NULL;
	}

	if (class_id >= ivns_internal->cii_gns.gn_num_class) {
		D_ERROR("class_id=%d exceeds num_class=%d\n", class_id,
			ivns_internal->cii_gns.gn_num_class);
		return NULL;
	}

	return ivns_internal->cii_iv_classes[class_id].ivc_ops;
}

/* Callback info for fetch's bulk transfer completion */
struct crt_ivf_transfer_cb_info {
	/* IV namespace */
	struct crt_ivns_internal	*tci_ivns_internal;

	/* Class ID for which operation was done */
	uint32_t			 tci_class_id;

	/* IV Key for which fetch was performed */
	d_iov_t				 tci_iv_key;

	/* IV value for which fetch was performed */
	d_sg_list_t			 tci_iv_value;

	/* User private data */
	void				*tci_user_priv;
};

/* Completion callback for fetch's bulk transfer */
static int
crt_ivf_bulk_transfer_done_cb(const struct crt_bulk_cb_info *info)
{
	struct crt_ivf_transfer_cb_info	*cb_info;
	struct iv_fetch_out		*output;
	struct crt_iv_ops		*iv_ops;
	crt_rpc_t			*rpc;
	int				rc = 0;

	/* Something is really bad if info is NULL */
	D_ASSERT(info != NULL);

	cb_info = (struct crt_ivf_transfer_cb_info *)info->bci_arg;
	rpc = info->bci_bulk_desc->bd_rpc;

	output = crt_reply_get(rpc);
	output->ifo_rc = info->bci_rc;

	iv_ops = crt_iv_ops_get(cb_info->tci_ivns_internal,
				cb_info->tci_class_id);
	D_ASSERT(iv_ops != NULL);

	rc = iv_ops->ivo_on_put(cb_info->tci_ivns_internal,
			&cb_info->tci_iv_value,
			cb_info->tci_user_priv);
	if (rc != 0)
		D_ERROR("ivo_on_put() failed; rc = %d\n", rc);

	/* Keep freeing things even if something fails */
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("crt_reply_send() failed; rc = %d\n", rc);

	rc = crt_bulk_free(info->bci_bulk_desc->bd_local_hdl);
	if (rc != 0)
		D_ERROR("crt_bulk_free() failed; rc = %d\n", rc);

	rc = crt_req_decref(rpc);
	if (rc != 0)
		D_ERROR("crt_req_decref() failed; rc = %d\n", rc);

	D_FREE_PTR(cb_info);

	return rc;
}

/* Helper function to issue bulk transfer */
static int
crt_ivf_bulk_transfer(struct crt_ivns_internal *ivns_internal,
		      uint32_t class_id, d_iov_t *iv_key,
		      d_sg_list_t *iv_value, crt_bulk_t dest_bulk,
		      crt_rpc_t *rpc, void *user_priv)
{
	struct crt_ivf_transfer_cb_info	*cb_info = NULL;
	struct crt_bulk_desc		bulk_desc;
	crt_bulk_opid_t			opid;
	crt_bulk_t			bulk_hdl;
	struct iv_fetch_out		*output;
	int				size;
	int				i;
	int				rc = 0;

	output = crt_reply_get(rpc);
	if (output == NULL) {
		D_ERROR("output was NULL\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	rc = crt_bulk_create(rpc->cr_ctx, iv_value, CRT_BULK_RW,
			&bulk_hdl);
	if (rc != 0) {
		D_ERROR("crt_bulk_create() failed with rc=%d\n", rc);
		D_GOTO(exit, rc);
	}

	/* Calculate total size of all iovs in sg list */
	size = 0;
	for (i = 0; i < iv_value->sg_nr.num; i++)
		size += iv_value->sg_iovs[i].iov_buf_len;

	/* crt_req_decref done in crt_ivf_bulk_transfer_done_cb */
	rc = crt_req_addref(rpc);
	D_ASSERT(rc == 0);

	memset(&bulk_desc, 0x0, sizeof(struct crt_bulk_desc));

	bulk_desc.bd_rpc = rpc;
	bulk_desc.bd_bulk_op = CRT_BULK_PUT;
	bulk_desc.bd_remote_hdl = dest_bulk;
	bulk_desc.bd_remote_off = 0;
	bulk_desc.bd_local_hdl = bulk_hdl;
	bulk_desc.bd_local_off = 0;
	bulk_desc.bd_len = size;

	D_ALLOC_PTR(cb_info);

	if (cb_info == NULL)
		D_GOTO(cleanup, rc = -DER_NOMEM);

	cb_info->tci_ivns_internal = ivns_internal;
	cb_info->tci_class_id = class_id;
	cb_info->tci_iv_key = *iv_key;
	cb_info->tci_iv_value = *iv_value;
	cb_info->tci_user_priv = user_priv;

	rc = crt_bulk_transfer(&bulk_desc, crt_ivf_bulk_transfer_done_cb,
				cb_info, &opid);

cleanup:
	if (rc != 0) {
		D_ERROR("Bulk transfer failed; rc=%d\n", rc);

		output->ifo_rc = rc;
		rc = crt_reply_send(rpc);
		D_ASSERT(rc == 0);

		rc = crt_req_decref(rpc);
		D_ASSERT(rc == 0);

		crt_bulk_free(bulk_hdl);
		if (cb_info)
			D_FREE_PTR(cb_info);
	}

exit:
	return rc;
}

/* Fetch response handler */
static void
handle_ivfetch_response(const struct crt_cb_info *cb_info)
{
	struct iv_fetch_cb_info		*iv_info = cb_info->cci_arg;
	crt_rpc_t			*rpc = cb_info->cci_rpc;
	struct iv_fetch_in		*input = crt_req_get(rpc);
	struct iv_fetch_out		*output = crt_reply_get(rpc);
	struct crt_iv_ops		*iv_ops;
	int				rc;

	if (cb_info->cci_rc == 0x0)
		rc = output->ifo_rc;
	else
		rc = cb_info->cci_rc;

	iv_ops = crt_iv_ops_get(iv_info->ifc_ivns_internal,
				input->ifi_class_id);

	/* In case of a failure, call on_refresh with NULL iv_value */
	iv_ops->ivo_on_refresh(iv_info->ifc_ivns_internal,
				&input->ifi_key,
				0, /* TODO: iv_ver */
				rc == 0 ? &iv_info->ifc_iv_value : NULL,
				false, rc, iv_info->ifc_user_priv);

	if (iv_info->ifc_bulk_hdl != 0x0)
		crt_bulk_free(iv_info->ifc_bulk_hdl);


	/* Finalization of fetch and processing of pending fetches must happen
	* after ivo_on_refresh() is invoked which would cause value associated
	* with the input->ifi_key to be updated
	*
	* Any unsuccessful fetch needs to process all pending requests before
	* finalizing, as the original caller might resubmit a failed fetch
	* for fault handling upon finalization. Not processing pending
	* fetches prior to finalization will cause new fetches done as
	* part of the fault handling to be added to the pending list.
	*
	* Any successful fetch should process all pending requests after
	* finalization as finalization can end up marking iv value as 'usable'
	* in some implementations of the framework callbacks
	**/
	if (rc != 0)
		crt_ivf_pending_reqs_process(iv_info->ifc_ivns_internal,
				iv_info->ifc_class_id,
				&input->ifi_key,
				rc);

	/* Finalize fetch operation */
	crt_ivf_finalize(iv_info, &input->ifi_key, &iv_info->ifc_iv_value,
			rc);

	if (rc == 0)
		crt_ivf_pending_reqs_process(iv_info->ifc_ivns_internal,
				iv_info->ifc_class_id,
				&input->ifi_key,
				rc);

	D_FREE_PTR(iv_info);
}

/* Helper function to issue internal iv_fetch RPC */
static int
crt_ivf_rpc_issue(d_rank_t dest_node, crt_iv_key_t *iv_key,
		  d_sg_list_t *iv_value, d_rank_t root_node,
		  struct iv_fetch_cb_info *cb_info)
{
	struct crt_ivns_internal	*ivns_internal;
	struct iv_fetch_in		*input;
	crt_bulk_t			local_bulk = CRT_BULK_NULL;
	crt_endpoint_t			ep = {0};
	crt_rpc_t			*rpc;
	struct ivf_key_in_progress	*entry;
	int				rc = 0;
	struct crt_iv_ops		*iv_ops;

	ivns_internal = cb_info->ifc_ivns_internal;

	iv_ops = crt_iv_ops_get(ivns_internal, cb_info->ifc_class_id);
	D_ASSERT(iv_ops != NULL);

	/* If there is already forwarded request in progress, do not
	* submit another one, instead add it to pending list
	*/
	pthread_mutex_lock(&ivns_internal->cii_lock);
	entry = crt_ivf_key_in_progress_find(ivns_internal, iv_ops, iv_key);
	pthread_mutex_unlock(&ivns_internal->cii_lock);

	if (entry) {
		rc = crt_ivf_pending_request_add(ivns_internal, iv_ops,
						entry, cb_info);
		if (rc != 0)
			D_ERROR("crt_ivf_pending_request_add() failed rc=%d\n",
				rc);
		return rc;
	}

	pthread_mutex_lock(&ivns_internal->cii_lock);
	rc = crt_ivf_key_in_progress_set(ivns_internal, iv_key);
	pthread_mutex_unlock(&ivns_internal->cii_lock);

	if (rc != 0) {
		D_ERROR("crt_ivf_key_in_progress_set() failed; rc = %d\n", rc);
		return rc;
	}

	rc = crt_bulk_create(ivns_internal->cii_ctx, iv_value, CRT_BULK_RW,
				&local_bulk);
	if (rc != 0) {
		D_ERROR("crt_bulk_create() failed; rc = %d\n", rc);
		D_GOTO(exit, rc);
	}

	ep.ep_grp = ivns_internal->cii_grp;
	ep.ep_rank = dest_node;

	rc = crt_req_create(ivns_internal->cii_ctx, &ep, CRT_OPC_IV_FETCH,
			    &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create() failed; rc = %d\n", rc);
		D_GOTO(exit, rc);
	}

	input = crt_req_get(rpc);
	D_ASSERT(input != NULL);

	input->ifi_value_bulk = local_bulk;

	cb_info->ifc_bulk_hdl = local_bulk;

	d_iov_set(&input->ifi_key, iv_key->iov_buf, iv_key->iov_buf_len);
	input->ifi_class_id = cb_info->ifc_class_id;
	input->ifi_root_node = root_node;

	d_iov_set(&input->ifi_nsid, &ivns_internal->cii_gns.gn_ivns_id,
		 sizeof(struct crt_ivns_id));

	rc = crt_req_send(rpc, handle_ivfetch_response, cb_info);

exit:
	if (rc != 0) {
		D_ERROR("Failed to send rpc to remote node = %d\n", dest_node);
		pthread_mutex_lock(&ivns_internal->cii_lock);

		/* Only unset if there are no pending fetches for this key */
		entry = crt_ivf_key_in_progress_find(ivns_internal,
						iv_ops, iv_key);

		if (entry && d_list_empty(&entry->kip_pending_fetch_list))
			crt_ivf_key_in_progress_unset(entry, iv_key);

		pthread_mutex_unlock(&ivns_internal->cii_lock);
		if (local_bulk != CRT_BULK_NULL)
			crt_bulk_free(local_bulk);
	}

	return rc;
}

/* Return next parent of the 'cur_node' */
static d_rank_t
crt_iv_ranks_parent_get(struct crt_ivns_internal *ivns_internal,
			d_rank_t cur_node, d_rank_t root_node)
{
	struct crt_grp_priv	*grp_priv;
	d_rank_t		 parent_rank;
	crt_group_t		*group;
	d_rank_list_t		*failed_ranks = NULL;
	int			 rc;

	if (cur_node == root_node)
		return root_node;

	/* group and grp_priv should never be NULL by the time we get here */
	group = crt_group_lookup(ivns_internal->cii_gns.gn_grp_id);
	D_ASSERT(group != NULL);

	grp_priv = container_of(group, struct crt_grp_priv, gp_pub);
	D_ASSERT(grp_priv != NULL);

	rc = crt_grp_failed_ranks_dup(group, &failed_ranks);

	if (rc != 0) {
		D_ERROR("crt_grp_failed_ranks_dup() failed; rc=%d\n", rc);
		failed_ranks = NULL;
	}

	rc = crt_tree_get_parent(grp_priv, 0, failed_ranks,
			ivns_internal->cii_gns.gn_tree_topo, root_node,
			cur_node, &parent_rank);
	D_ASSERT(rc == 0);

	d_rank_list_free(failed_ranks);

	D_DEBUG("parent lookup: current=%d, root=%d, parent=%d\n",
		cur_node, root_node, parent_rank);

	return parent_rank;
}

/* Return next parent for the current rank and root_node */
static d_rank_t
crt_iv_parent_get(struct crt_ivns_internal *ivns_internal,
		  d_rank_t root_node)
{
	return crt_iv_ranks_parent_get(ivns_internal,
				       ivns_internal->cii_local_rank,
				       root_node);
}

/* Internal handler for CRT_OPC_IV_FETCH RPC call*/
void
crt_hdlr_iv_fetch(crt_rpc_t *rpc_req)
{
	struct iv_fetch_in		*input;
	struct iv_fetch_out		*output;
	struct crt_ivns_id		*ivns_id;
	struct crt_ivns_internal	*ivns_internal;
	struct crt_iv_ops		*iv_ops;
	d_sg_list_t			iv_value = {0};
	int				rc = 0;
	bool				put_needed = false;
	void				*user_priv = NULL;

	input = crt_req_get(rpc_req);
	output = crt_reply_get(rpc_req);

	ivns_id = (struct crt_ivns_id *)input->ifi_nsid.iov_buf;

	ivns_internal = crt_ivns_internal_lookup(ivns_id);
	if (ivns_internal == NULL) {
		D_ERROR("Failed to lookup ivns internal!\n");
		D_GOTO(send_error, rc = -DER_INVAL);
	}

	iv_ops = crt_iv_ops_get(ivns_internal, input->ifi_class_id);
	if (iv_ops == NULL) {
		D_ERROR("Returned iv_ops were NULL\n");
		D_GOTO(send_error, rc = -DER_INVAL);
	}

	rc = iv_ops->ivo_on_get(ivns_internal, &input->ifi_key,
				0, CRT_IV_PERM_READ, &iv_value, &user_priv);

	if (rc != 0) {
		D_ERROR("ivo_on_get failed; rc=%d\n", rc);
		D_GOTO(send_error, rc);
	}

	put_needed = true;

	rc = iv_ops->ivo_on_fetch(ivns_internal, &input->ifi_key, 0,
				0x0, &iv_value, user_priv);
	if (rc == 0) {
		/* Note: Function will increment ref count on 'rpc_req' */
		rc = crt_ivf_bulk_transfer(ivns_internal,
					input->ifi_class_id, &input->ifi_key,
					&iv_value, input->ifi_value_bulk,
					rpc_req, user_priv);
		if (rc != 0) {
			D_ERROR("bulk transfer failed; rc = %d\n", rc);
			D_GOTO(send_error, rc);
		}
	} else if (rc == -DER_IVCB_FORWARD) {
		d_rank_t next_node;
		struct iv_fetch_cb_info *cb_info;

		if (ivns_internal->cii_local_rank == input->ifi_root_node) {
			D_ERROR("Forward requested for root node\n");
			D_GOTO(send_error, rc = -DER_INVAL);
		}

		rc = iv_ops->ivo_on_put(ivns_internal, &iv_value, user_priv);
		if (rc != 0) {
			D_ERROR("ivo_on_put() returend rc = %d\n", rc);
			D_GOTO(send_error, rc);
		}

		put_needed = false;

		/* Reset the iv_value, since it maybe freed in on_put() */
		memset(&iv_value, 0, sizeof(iv_value));
		rc = iv_ops->ivo_on_get(ivns_internal, &input->ifi_key,
					0, CRT_IV_PERM_WRITE, &iv_value,
					&user_priv);
		if (rc != 0) {
			D_ERROR("ivo_on_get() returned rc = %d\n", rc);
			D_GOTO(send_error, rc);
		}

		put_needed = true;

		next_node = crt_iv_parent_get(ivns_internal,
					input->ifi_root_node);
		D_ALLOC_PTR(cb_info);
		if (cb_info == NULL)
			D_GOTO(send_error, rc = -DER_NOMEM);

		cb_info->ifc_child_rpc = rpc_req;
		cb_info->ifc_child_bulk = input->ifi_value_bulk,

		/* crt_req_decref done in crt_ivf_finalize */
		rc = crt_req_addref(rpc_req);
		D_ASSERT(rc == 0);

		cb_info->ifc_iv_value = iv_value;
		cb_info->ifc_iv_key = input->ifi_key;

		cb_info->ifc_ivns_internal = ivns_internal;
		cb_info->ifc_class_id = input->ifi_class_id;
		cb_info->ifc_user_priv = user_priv;

		rc = crt_ivf_rpc_issue(next_node,
					&input->ifi_key, &cb_info->ifc_iv_value,
					input->ifi_root_node,
					cb_info);
		if (rc != 0) {
			D_ERROR("Failed to issue fetch rpc; rc = %d\n", rc);
			D_FREE_PTR(cb_info);
			crt_req_decref(rpc_req);
			D_GOTO(send_error, rc);
		}
	} else {
		D_ERROR("ERROR happened with rc = %d\n", rc);
		D_GOTO(send_error, rc);
	}

	return;

send_error:
	if (put_needed)
		iv_ops->ivo_on_put(ivns_internal, &iv_value, user_priv);
	output->ifo_rc = rc;
	rc = crt_reply_send(rpc_req);
	D_ASSERT(rc == 0);
}

int
crt_iv_fetch(crt_iv_namespace_t ivns, uint32_t class_id,
	    crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
	    crt_iv_shortcut_t shortcut,
	    crt_iv_comp_cb_t fetch_comp_cb, void *cb_arg)
{
	struct crt_ivns_internal	*ivns_internal;
	struct crt_iv_ops		*iv_ops;
	struct iv_fetch_cb_info		*cb_info;
	d_rank_t			 root_rank;
	d_rank_t			 next_node = 1;
	int				 rc;
	d_sg_list_t			*iv_value = NULL;
	void				*user_priv = NULL;

	if (iv_key == NULL) {
		D_ERROR("iv_key is NULL\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	ivns_internal = crt_ivns_internal_get(ivns);

	if (ivns_internal == NULL) {
		D_ERROR("Invalid ivns\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	iv_ops = crt_iv_ops_get(ivns_internal, class_id);
	if (iv_ops == NULL) {
		D_ERROR("Failed to get iv_ops for class_id = %d\n", class_id);
		D_GOTO(exit, rc = -DER_INVAL);
	}

	rc = iv_ops->ivo_on_hash(ivns_internal, iv_key, &root_rank);

	if (rc != 0) {
		D_ERROR("Failed to get hash\n");
		D_GOTO(exit, rc);
	}

	D_ALLOC_PTR(iv_value);
	if (iv_value == NULL)
		D_GOTO(exit, rc = -DER_NOMEM);

	rc = iv_ops->ivo_on_get(ivns_internal, iv_key, 0, CRT_IV_PERM_READ,
				iv_value, &user_priv);
	if (rc != 0) {
		D_ERROR("ivo_on_get() failed; rc = %d\n", rc);
		D_GOTO(exit, rc);
	}

	rc = iv_ops->ivo_on_fetch(ivns_internal, iv_key, 0,
				  0, iv_value, user_priv);
	if (rc == 0) {
		iv_ops->ivo_on_refresh(ivns_internal, iv_key, 0,
				iv_value, false, 0x0, user_priv);
		fetch_comp_cb(ivns_internal, class_id, iv_key, NULL,
			      iv_value, rc, cb_arg);
		iv_ops->ivo_on_put(ivns_internal, iv_value, user_priv);
		D_FREE_PTR(iv_value);
		return rc;
	} else if (rc != -DER_IVCB_FORWARD) {
		/* We got error, call the callback and exit */
		iv_ops->ivo_on_refresh(ivns_internal, iv_key, 0,
				NULL, false, rc, user_priv);
		fetch_comp_cb(ivns_internal, class_id, iv_key, NULL,
			      NULL, rc, cb_arg);
		iv_ops->ivo_on_put(ivns_internal, iv_value, user_priv);
		D_FREE_PTR(iv_value);
		return rc;
	}

	/* Return read-only copy and request 'write' version of iv_value */
	iv_ops->ivo_on_put(ivns_internal, iv_value, user_priv);

	rc = iv_ops->ivo_on_get(ivns_internal, iv_key, 0, CRT_IV_PERM_WRITE,
				iv_value, &user_priv);
	if (rc != 0) {
		D_ERROR("ivo_on_get() failed; rc = %d\n", rc);
		D_GOTO(exit, rc);
	}

	/* If we reached here, means we got DER_IVCB_FORWARD */

	switch (shortcut) {
	case CRT_IV_SHORTCUT_TO_ROOT:
		next_node = root_rank;
		break;

	case CRT_IV_SHORTCUT_NONE:
		next_node = crt_iv_parent_get(ivns_internal, root_rank);
		break;

	default:
		D_ERROR("Unknown shortcut=%d specified\n", shortcut);
		D_GOTO(exit, rc = -DER_INVAL);
	}

	D_ALLOC_PTR(cb_info);
	if (cb_info == NULL)
		D_GOTO(exit, rc = -DER_NOMEM);

	cb_info->ifc_user_priv = user_priv;
	cb_info->ifc_child_rpc = NULL;
	cb_info->ifc_bulk_hdl = CRT_BULK_NULL;

	cb_info->ifc_comp_cb = fetch_comp_cb;
	cb_info->ifc_comp_cb_arg = cb_arg;

	cb_info->ifc_iv_value = *iv_value;
	cb_info->ifc_iv_key = *iv_key;

	cb_info->ifc_ivns_internal = ivns_internal;
	cb_info->ifc_class_id = class_id;

	rc = crt_ivf_rpc_issue(next_node, iv_key, iv_value, root_rank,
				cb_info);
exit:
	if (rc != 0) {
		D_ERROR("Failed to issue IV fetch; rc = %d\n", rc);
		D_FREE_PTR(cb_info);
	}

	if (iv_value)
		D_FREE_PTR(iv_value);

	return rc;
}

/***************************************************************
 * IV UPDATE codebase
 **************************************************************/
struct iv_update_in {
	/* IV namespace ID */
	d_iov_t		ivu_nsid;

	/* IOV for key */
	d_iov_t		ivu_key;

	/* IOV for sync */
	d_iov_t		ivu_sync_type;

	/* Bulk handle for iv value */
	crt_bulk_t	ivu_iv_value_bulk;

	/* Root node for IV UPDATE */
	d_rank_t	ivu_root_node;

	/* Original node that issued crt_iv_update call */
	d_rank_t	ivu_caller_node;

	/* Class ID */
	uint32_t	ivu_class_id;

	uint32_t	padding;

};

struct iv_update_out {
	uint64_t		rc;
};

struct iv_sync_in {
	/* IV Namespace ID */
	d_iov_t		ivs_nsid;

	/* IOV for key */
	d_iov_t		ivs_key;

	/* IOV for sync type */
	d_iov_t		ivs_sync_type;

	/* IV Class ID */
	uint32_t	ivs_class_id;
};

struct iv_sync_out {
	int	rc;
};

/* Handler for internal SYNC CORPC */
void
crt_hdlr_iv_sync(crt_rpc_t *rpc_req)
{
	int				rc = 0;
	struct iv_sync_in		*input;
	struct iv_sync_out		*output;
	struct crt_ivns_internal	*ivns_internal;
	struct crt_iv_ops		*iv_ops;
	struct crt_ivns_id		*ivns_id;
	crt_iv_sync_t			*sync_type;
	d_sg_list_t			iv_value = {0};
	bool				 need_put = false;
	void				*user_priv = NULL;

	/* This is an internal call. All errors are fatal */
	input = crt_req_get(rpc_req);
	D_ASSERT(input != NULL);

	output = crt_reply_get(rpc_req);
	D_ASSERT(output != NULL);

	ivns_id = (struct crt_ivns_id *)input->ivs_nsid.iov_buf;
	sync_type = (crt_iv_sync_t *)input->ivs_sync_type.iov_buf;

	ivns_internal = crt_ivns_internal_lookup(ivns_id);
	D_ASSERT(ivns_internal != NULL);

	iv_ops = crt_iv_ops_get(ivns_internal, input->ivs_class_id);
	D_ASSERT(iv_ops != NULL);

	/* If bulk is not set, we issue invalidate call */
	if (rpc_req->cr_co_bulk_hdl == CRT_BULK_NULL) {
		rc = iv_ops->ivo_on_refresh(ivns_internal, &input->ivs_key,
					0, NULL, true, 0x0, NULL);
		D_GOTO(exit, rc);
	}

	/* If bulk is set, issue sync call based on ivs_event */
	switch (sync_type->ivs_event) {
	case CRT_IV_SYNC_EVENT_UPDATE:
	{
		d_sg_list_t tmp_iv;

		rc = iv_ops->ivo_on_get(ivns_internal, &input->ivs_key,
				0, CRT_IV_PERM_WRITE, &iv_value, &user_priv);

		tmp_iv = iv_value;
		if (rc != 0) {
			D_ERROR("ivo_on_get() failed; rc=%d\n", rc);
			D_GOTO(exit, rc);
		}

		need_put = true;

		rc = crt_bulk_access(rpc_req->cr_co_bulk_hdl, &tmp_iv);
		if (rc != 0) {
			D_ERROR("crt_bulk_access() failed; rc=%d\n", rc);
			D_GOTO(exit, rc);
		}

		rc = iv_ops->ivo_on_refresh(ivns_internal, &input->ivs_key,
					0, &tmp_iv, false, 0, user_priv);
		if (rc != 0) {
			D_ERROR("ivo_on_refresh() failed; rc=%d\n", rc);
			D_GOTO(exit, rc);
		}

		rc = iv_ops->ivo_on_put(ivns_internal, &iv_value, user_priv);
		if (rc != 0) {
			D_ERROR("ivo_on_put() failed; rc=%d\n", rc);
			D_GOTO(exit, rc);
		}
		need_put = false;

		break;
	}

	case CRT_IV_SYNC_EVENT_NOTIFY:
		rc = iv_ops->ivo_on_refresh(ivns_internal, &input->ivs_key,
					    0, 0, false, 0, user_priv);
		if (rc != 0) {
			D_ERROR("ivo_on_refresh() failed; rc=%d\n", rc);
			D_GOTO(exit, rc);
		}

		break;

	default:
		D_ERROR("Unknown event type %#x", sync_type->ivs_event);
		D_GOTO(exit, rc = -DER_INVAL);
		break;
	}

exit:
	if (need_put)
		iv_ops->ivo_on_put(ivns_internal, &iv_value, user_priv);

	output->rc = rc;
	crt_reply_send(rpc_req);
}

/* Results aggregate function for sync CORPC */
int
crt_iv_sync_corpc_aggregate(crt_rpc_t *source, crt_rpc_t *result, void *arg)
{
	struct iv_sync_out *output_source;
	struct iv_sync_out *output_result;

	output_source = crt_reply_get(source);
	output_result = crt_reply_get(result);

	/* Only set new rc if so far rc is 0 */
	if (output_result->rc == 0) {
		if (output_source->rc != 0)
			output_result->rc = output_source->rc;
	}

	return 0;
}

/* Calback structure for iv sync RPC */
struct iv_sync_cb_info {
	/* Local bulk handle to free in callback */
	crt_bulk_t			isc_bulk_hdl;

	/* Internal IV namespace */
	struct crt_ivns_internal	*isc_ivns_internal;

	/* Class id associated with namespace */
	uint32_t			isc_class_id;

	/* IV key/value; used for issuing completion callback */
	crt_iv_key_t			isc_iv_key;
	d_sg_list_t			isc_iv_value;

	/* Flag indicating whether to perform callback */
	bool				isc_do_callback;

	/* Completion callback, arguments for it and rc */
	crt_iv_comp_cb_t		isc_update_comp_cb;
	void				*isc_cb_arg;
	int				isc_update_rc;
};

/* IV_SYNC response handler */
static void
handle_ivsync_response(const struct crt_cb_info *cb_info)
{
	struct iv_sync_cb_info *iv_sync = cb_info->cci_arg;

	crt_bulk_free(iv_sync->isc_bulk_hdl);

	/* do_callback is set based on sync value specified */
	if (iv_sync->isc_do_callback) {
		iv_sync->isc_update_comp_cb(iv_sync->isc_ivns_internal,
					iv_sync->isc_class_id,
					&iv_sync->isc_iv_key,
					NULL,
					&iv_sync->isc_iv_value,
					iv_sync->isc_update_rc,
					iv_sync->isc_cb_arg);

		D_FREE(iv_sync->isc_iv_key.iov_buf);
	}
	D_FREE(iv_sync);
}

/* Helper function to issue update sync
 * Important note: iv_key and iv_value are destroyed right after this call,
 * as such they need to be copied over
 **/
static int
crt_ivsync_rpc_issue(struct crt_ivns_internal *ivns_internal, uint32_t class_id,
		     crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
		     d_sg_list_t *iv_value, crt_iv_sync_t sync_type,
		     d_rank_t src_node, d_rank_t dst_node,
		     crt_iv_comp_cb_t update_comp_cb, void *cb_arg,
		     void *user_priv, int update_rc)
{
	crt_rpc_t		*corpc_req;
	struct iv_sync_in	*input;
	int			rc = 0;
	bool			sync = false;
	struct iv_sync_cb_info	*iv_sync_cb = NULL;
	struct crt_iv_ops	*iv_ops;
	crt_bulk_t		local_bulk = CRT_BULK_NULL;
	d_rank_list_t		excluded_list;
	d_rank_t		excluded_ranks[1]; /* Excluding self */

	/* TODO: An optional feature for future get all ranks between
	* source node and destination in order to exclude them from
	* being synchronized (as they already got updated)
	**/
#if 0
	d_rank_t		cur_rank;

	cur_rank = src_node;
	while (1) {
		if (cur_rank == dst_node)
			break;

		cur_rank = get_ranks_parent(ivns_internal, cur_rank,
					dst_node);
	}
#endif

	iv_ops = crt_iv_ops_get(ivns_internal, class_id);

	switch (sync_type.ivs_mode) {
	case CRT_IV_SYNC_NONE:
		D_GOTO(exit, rc = 0);

	case CRT_IV_SYNC_EAGER:
		sync = true;
		break;

	case CRT_IV_SYNC_LAZY:
		sync = false;
		break;

	default:
		D_ERROR("Unknown ivs_mode %d\n", sync_type.ivs_mode);
		D_GOTO(exit, rc = -DER_INVAL);
	}

	/* Exclude self from corpc */
	excluded_list.rl_nr.num = 1;
	excluded_list.rl_ranks = excluded_ranks;
	excluded_ranks[0] = ivns_internal->cii_local_rank;
	/* Perform refresh on local node */
	if (sync_type.ivs_event == CRT_IV_SYNC_EVENT_UPDATE)
		rc = iv_ops->ivo_on_refresh(ivns_internal, iv_key, 0,
					iv_value, iv_value ? false : true,
					0, user_priv);
	else if (sync_type.ivs_event == CRT_IV_SYNC_EVENT_NOTIFY)
		rc = iv_ops->ivo_on_refresh(ivns_internal, iv_key, 0,
					NULL, iv_value ? false : true,
					0, user_priv);
	else {
		D_ERROR("Unknown ivs_event %d\n", sync_type.ivs_event);
		D_GOTO(exit, rc = -DER_INVAL);
	}

	if (iv_value != NULL) {
		rc = crt_bulk_create(ivns_internal->cii_ctx, iv_value,
				CRT_BULK_RO, &local_bulk);
		if (rc != 0) {
			D_ERROR("ctt_bulk_create() failed; rc=%d\n", rc);
			D_GOTO(exit, rc);
		}
	} else {
		local_bulk = CRT_BULK_NULL;
	}

	rc = crt_corpc_req_create(ivns_internal->cii_ctx,
				ivns_internal->cii_grp,
				&excluded_list,
				CRT_OPC_IV_SYNC,
				local_bulk, NULL, 0,
				ivns_internal->cii_gns.gn_tree_topo,
				&corpc_req);
	if (rc != 0) {
		D_ERROR("crt_corpc_req_create() failed; rc=%d\n", rc);
		D_GOTO(exit, rc);
	}

	input = crt_req_get(corpc_req);
	D_ASSERT(input != NULL);

	d_iov_set(&input->ivs_nsid, &ivns_internal->cii_gns.gn_ivns_id,
		  sizeof(struct crt_ivns_id));
	d_iov_set(&input->ivs_key, iv_key->iov_buf, iv_key->iov_buf_len);
	d_iov_set(&input->ivs_sync_type, &sync_type, sizeof(crt_iv_sync_t));
	input->ivs_class_id = class_id;

	D_ALLOC_PTR(iv_sync_cb);
	if (iv_sync_cb == NULL)
		D_GOTO(exit, rc = -DER_NOMEM);

	iv_sync_cb->isc_bulk_hdl = local_bulk;
	iv_sync_cb->isc_do_callback = sync;

	/* If sync is set, perform callback from sync response handler */
	if (sync) {
		iv_sync_cb->isc_update_comp_cb = update_comp_cb;
		iv_sync_cb->isc_cb_arg = cb_arg;
		iv_sync_cb->isc_update_rc = update_rc;
		iv_sync_cb->isc_ivns_internal = ivns_internal;
		iv_sync_cb->isc_class_id = class_id;

		/* Copy iv_key over as it will get destroyed after this call */
		D_ALLOC(iv_sync_cb->isc_iv_key.iov_buf, iv_key->iov_buf_len);
		if (iv_sync_cb->isc_iv_key.iov_buf == NULL)
			D_GOTO(exit, rc = -DER_NOMEM);

		memcpy(iv_sync_cb->isc_iv_key.iov_buf, iv_key->iov_buf,
			iv_key->iov_buf_len);

		iv_sync_cb->isc_iv_key.iov_buf_len = iv_key->iov_buf_len;
		iv_sync_cb->isc_iv_key.iov_len = iv_key->iov_len;

		/* Copy underlying sg_list as iv_value pointer will not be valid
		* once this function exits
		**/
		if (iv_value)
			iv_sync_cb->isc_iv_value = *iv_value;
	}

	rc = crt_req_send(corpc_req, handle_ivsync_response, iv_sync_cb);
	D_ASSERT(rc == 0);

exit:
	if (sync == false)
		update_comp_cb(ivns_internal, class_id, iv_key, NULL, iv_value,
			       update_rc, cb_arg);

	if (rc != 0) {
		if (local_bulk != CRT_BULK_NULL)
			crt_bulk_free(local_bulk);

		if (iv_sync_cb) {
			if (iv_sync_cb->isc_iv_key.iov_buf)
				D_FREE(iv_sync_cb->isc_iv_key.iov_buf);

			D_FREE_PTR(iv_sync_cb);
		}
	}

	return rc;
}

struct update_cb_info {
	/* Update completion callback and argument */
	crt_iv_comp_cb_t		uci_comp_cb;
	void				*uci_cb_arg;

	/* RPC of the caller if one exists */
	crt_rpc_t			*uci_child_rpc;

	/* Internal IV namespace and IV class id */
	struct crt_ivns_internal	*uci_ivns_internal;
	uint32_t			uci_class_id;

	/* Local bulk handle and associated iv value */
	crt_bulk_t			uci_bulk_hdl;
	d_sg_list_t			uci_iv_value;

	/* Caller of the crt_iv_update() API */
	d_rank_t			uci_caller_rank;

	/* Sync type associated with this update */
	crt_iv_sync_t			uci_sync_type;

	/* User private data */
	void				*uci_user_priv;
};

/* IV_UPDATE internal rpc response handler */
static void
handle_ivupdate_response(const struct crt_cb_info *cb_info)
{
	struct update_cb_info	*iv_info = cb_info->cci_arg;
	struct iv_update_in	*input = crt_req_get(cb_info->cci_rpc);
	struct iv_update_out	*output = crt_reply_get(cb_info->cci_rpc);
	struct iv_update_out	*child_output;
	struct crt_iv_ops	*iv_ops;
	int			rc;

	if (iv_info->uci_child_rpc) {

		child_output = crt_reply_get(iv_info->uci_child_rpc);

		/* uci_bulk_hdl will not be set for invalidate call */
		if (iv_info->uci_bulk_hdl != CRT_BULK_NULL) {
			iv_ops = crt_iv_ops_get(iv_info->uci_ivns_internal,
						iv_info->uci_class_id);
			D_ASSERT(iv_ops != NULL);

			rc = iv_ops->ivo_on_put(iv_info->uci_ivns_internal,
						&iv_info->uci_iv_value,
						iv_info->uci_user_priv);

			if (rc != 0) {
				D_ERROR("ivo_on_put() failed; rc=%d\n", rc);
				child_output->rc = rc;
			} else {
				child_output->rc = output->rc;
			}
		} else {
			child_output->rc = output->rc;
		}

		/* Fatal if reply send fails */
		rc = crt_reply_send(iv_info->uci_child_rpc);
		D_ASSERT(rc == 0);

		rc = crt_req_decref(iv_info->uci_child_rpc);
		D_ASSERT(rc == 0);
	} else {
		d_sg_list_t *tmp_iv_value;
		void *priv;

		if (iv_info->uci_bulk_hdl == NULL)
			tmp_iv_value = NULL;
		else
			tmp_iv_value = &iv_info->uci_iv_value;

		iv_ops = crt_iv_ops_get(iv_info->uci_ivns_internal,
					iv_info->uci_class_id);
		D_ASSERT(iv_ops != NULL);

		/* Since ivsysnc_rpc_issue will do local node refresh,
		 * let's get the write access here.
		 */
		rc = iv_ops->ivo_on_get(iv_info->uci_ivns_internal,
					&input->ivu_key, 0, CRT_IV_PERM_WRITE,
					NULL, &priv);
		D_ASSERT(rc == 0);

		crt_ivsync_rpc_issue(iv_info->uci_ivns_internal,
					iv_info->uci_class_id,
					&input->ivu_key, 0,
					tmp_iv_value,
					iv_info->uci_sync_type,
					input->ivu_caller_node,
					input->ivu_root_node,
					iv_info->uci_comp_cb,
					iv_info->uci_cb_arg,
					priv,
					output->rc);

		rc = iv_ops->ivo_on_put(iv_info->uci_ivns_internal,
					NULL, priv);

	}

	if (iv_info->uci_bulk_hdl != CRT_BULK_NULL)
		crt_bulk_free(iv_info->uci_bulk_hdl);

	D_FREE_PTR(iv_info);
}

/* Helper function to issue IV UPDATE RPC*/
static int
crt_ivu_rpc_issue(d_rank_t dest_rank, crt_iv_key_t *iv_key,
		  d_sg_list_t *iv_value, crt_iv_sync_t *sync_type,
		  d_rank_t root_rank, struct update_cb_info *cb_info)
{
	struct crt_ivns_internal	*ivns_internal;
	struct iv_update_in		*input;
	crt_bulk_t			local_bulk = CRT_BULK_NULL;
	crt_endpoint_t			ep = {0};
	crt_rpc_t			*rpc;
	int				rc = 0;

	ivns_internal = cb_info->uci_ivns_internal;

	ep.ep_grp = ivns_internal->cii_grp;
	ep.ep_rank = dest_rank;

	rc = crt_req_create(ivns_internal->cii_ctx, &ep, CRT_OPC_IV_UPDATE,
			    &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create() failed; rc=%d\n", rc);
		D_GOTO(exit, rc);
	}

	input = crt_req_get(rpc);

	/* Update with NULL value is invalidate call */
	if (iv_value) {
		rc = crt_bulk_create(ivns_internal->cii_ctx, iv_value,
				CRT_BULK_RW, &local_bulk);

		if (rc != 0) {
			D_ERROR("crt_bulk_create() failed; rc=%d\n", rc);
			D_GOTO(exit, rc);
		}
	} else {
		local_bulk = CRT_BULK_NULL;
	}

	input->ivu_iv_value_bulk = local_bulk;
	cb_info->uci_bulk_hdl = local_bulk;

	d_iov_set(&input->ivu_key, iv_key->iov_buf, iv_key->iov_buf_len);
	input->ivu_class_id = cb_info->uci_class_id;
	input->ivu_root_node = root_rank;
	input->ivu_caller_node = cb_info->uci_caller_rank;

	/* iv_value might not be set */
	if (iv_value)
		cb_info->uci_iv_value = *iv_value;


	d_iov_set(&input->ivu_nsid, &ivns_internal->cii_gns.gn_ivns_id,
		 sizeof(struct crt_ivns_id));

	d_iov_set(&input->ivu_sync_type, sync_type, sizeof(crt_iv_sync_t));

	cb_info->uci_sync_type = *sync_type;

	rc = crt_req_send(rpc, handle_ivupdate_response, cb_info);
	if (rc != 0)
		D_ERROR("crt_req_send() failed; rc=%d\n", rc);


exit:
	if (rc != 0) {
		if (local_bulk != CRT_BULK_NULL)
			crt_bulk_free(local_bulk);
	}

	return rc;
}

/* bulk transfer update callback info */
struct bulk_update_cb_info {
	/* Input buffer for iv update rpc */
	struct iv_update_in	*buc_input;
	/* Local bulk handle to free */
	crt_bulk_t		buc_bulk_hdl;
	/* IV value */
	d_sg_list_t		buc_iv_value;
	/* Users private data */
	void			*buc_user_priv;
};

static int
bulk_update_transfer_done(const struct crt_bulk_cb_info *info)
{
	struct bulk_update_cb_info	*cb_info;
	struct crt_ivns_id		*ivns_id;
	struct crt_ivns_internal	*ivns_internal;
	struct crt_iv_ops		*iv_ops;
	struct iv_update_in		*input;
	struct iv_update_out		*output;
	struct update_cb_info		*update_cb_info = NULL;
	int				rc = 0;
	d_rank_t			next_rank;
	int				update_rc;
	crt_iv_sync_t			*sync_type;

	cb_info = (struct bulk_update_cb_info *)info->bci_arg;

	input = cb_info->buc_input;

	ivns_id = (struct crt_ivns_id *)input->ivu_nsid.iov_buf;

	/* ivns_internal and iv_ops better not be NULL at this point */
	ivns_internal = crt_ivns_internal_lookup(ivns_id);
	D_ASSERT(ivns_internal != NULL);

	iv_ops = crt_iv_ops_get(ivns_internal, input->ivu_class_id);
	D_ASSERT(iv_ops != NULL);

	output = crt_reply_get(info->bci_bulk_desc->bd_rpc);
	D_ASSERT(output != NULL);

	if (info->bci_rc != 0) {
		D_ERROR("bulk update transfer failed; rc = %d",
			info->bci_rc);
		D_GOTO(send_error, rc = info->bci_rc);
	}

	update_rc = iv_ops->ivo_on_update(ivns_internal,
			&input->ivu_key, 0, false, &cb_info->buc_iv_value,
			cb_info->buc_user_priv);

	if (update_rc == -DER_IVCB_FORWARD) {
		next_rank = crt_iv_parent_get(ivns_internal,
					input->ivu_root_node);

		D_ALLOC_PTR(update_cb_info);
		if (update_cb_info == NULL)
			D_GOTO(send_error, rc = -DER_NOMEM);

		sync_type = input->ivu_sync_type.iov_buf;

		update_cb_info->uci_child_rpc = info->bci_bulk_desc->bd_rpc;
		update_cb_info->uci_ivns_internal = ivns_internal;
		update_cb_info->uci_class_id = input->ivu_class_id;
		update_cb_info->uci_caller_rank = input->ivu_caller_node;
		update_cb_info->uci_sync_type = *sync_type;
		update_cb_info->uci_user_priv = cb_info->buc_user_priv;

		crt_ivu_rpc_issue(next_rank, &input->ivu_key,
				&cb_info->buc_iv_value, sync_type,
				input->ivu_root_node, update_cb_info);
	} else if (update_rc == 0) {
		rc = iv_ops->ivo_on_put(ivns_internal,
					&cb_info->buc_iv_value,
					cb_info->buc_user_priv);

		output->rc = rc;
		rc = crt_reply_send(info->bci_bulk_desc->bd_rpc);

		crt_req_decref(info->bci_bulk_desc->bd_rpc);

	} else {
		D_GOTO(send_error, rc = update_rc);
	}

	rc = crt_bulk_free(cb_info->buc_bulk_hdl);
	D_FREE_PTR(cb_info);

	return rc;

send_error:
	rc = crt_bulk_free(cb_info->buc_bulk_hdl);
	D_FREE_PTR(cb_info);

	output->rc = rc;

	crt_reply_send(info->bci_bulk_desc->bd_rpc);
	crt_req_decref(info->bci_bulk_desc->bd_rpc);

	return rc;
}

/* IV UPDATE RPC handler */
void
crt_hdlr_iv_update(crt_rpc_t *rpc_req)
{
	struct iv_update_in		*input;
	struct iv_update_out		*output;
	struct crt_ivns_id		*ivns_id;
	struct crt_ivns_internal	*ivns_internal;
	struct crt_iv_ops		*iv_ops;
	d_sg_list_t			iv_value = {0};
	struct crt_bulk_desc		bulk_desc;
	crt_bulk_t			local_bulk_handle;
	struct bulk_update_cb_info	*cb_info;
	crt_iv_sync_t			*sync_type;
	d_rank_t			next_rank;
	struct update_cb_info		*update_cb_info;
	int				size;
	void				*user_priv;
	int				i;
	int				rc = 0;

	input = crt_req_get(rpc_req);
	output = crt_reply_get(rpc_req);

	D_ASSERT(input != NULL);
	D_ASSERT(output != NULL);

	ivns_id = (struct crt_ivns_id *)input->ivu_nsid.iov_buf;
	ivns_internal = crt_ivns_internal_lookup(ivns_id);

	if (ivns_internal == NULL) {
		D_ERROR("Invalid internal ivns\n");
		D_GOTO(send_error, rc = -DER_INVAL);
	}

	iv_ops = crt_iv_ops_get(ivns_internal, input->ivu_class_id);

	if (iv_ops == NULL) {
		D_ERROR("Invalid class id passed\n");
		D_GOTO(send_error, rc = -DER_INVAL);
	}

	if (input->ivu_iv_value_bulk == CRT_BULK_NULL) {

		rc = iv_ops->ivo_on_refresh(ivns_internal, &input->ivu_key,
					0, NULL, true, 0, NULL);

		if (rc == -DER_IVCB_FORWARD) {
			next_rank = crt_iv_parent_get(ivns_internal,
						input->ivu_root_node);

			D_ALLOC_PTR(update_cb_info);
			if (update_cb_info == NULL)
				D_GOTO(send_error, rc = -DER_NOMEM);

			sync_type = (crt_iv_sync_t *)
					input->ivu_sync_type.iov_buf;

			crt_req_addref(rpc_req);

			update_cb_info->uci_child_rpc = rpc_req;
			update_cb_info->uci_ivns_internal = ivns_internal;
			update_cb_info->uci_class_id = input->ivu_class_id;
			update_cb_info->uci_caller_rank =
							input->ivu_caller_node;

			update_cb_info->uci_sync_type = *sync_type;

			crt_ivu_rpc_issue(next_rank, &input->ivu_key,
					NULL, sync_type,
					input->ivu_root_node, update_cb_info);
		} else if (rc == 0) {
			output->rc = rc;
			rc = crt_reply_send(rpc_req);
		} else {
			D_GOTO(send_error, rc);
		}

		D_GOTO(exit, rc = 0);
	}

	rc = iv_ops->ivo_on_get(ivns_internal, &input->ivu_key, 0,
				CRT_IV_PERM_WRITE, &iv_value, &user_priv);
	if (rc != 0) {
		D_ERROR("ivo_on_get() failed; rc=%d\n", rc);
		D_GOTO(send_error, rc);
	}

	size = 0;
	for (i = 0; i < iv_value.sg_nr.num; i++)
		size += iv_value.sg_iovs[i].iov_buf_len;

	rc = crt_bulk_create(rpc_req->cr_ctx, &iv_value, CRT_BULK_RW,
			&local_bulk_handle);
	if (rc != 0) {
		D_ERROR("crt_bulk_create() failed; rc=%d\n", rc);
		D_GOTO(send_error, rc);
	}

	bulk_desc.bd_rpc = rpc_req;
	bulk_desc.bd_bulk_op = CRT_BULK_GET;
	bulk_desc.bd_remote_hdl = input->ivu_iv_value_bulk;
	bulk_desc.bd_remote_off = 0;
	bulk_desc.bd_local_hdl = local_bulk_handle;
	bulk_desc.bd_local_off = 0;
	bulk_desc.bd_len = size;

	D_ALLOC_PTR(cb_info);
	if (cb_info == NULL) {
		crt_bulk_free(local_bulk_handle);
		D_GOTO(send_error, rc = -DER_NOMEM);
	}

	cb_info->buc_input = input;
	cb_info->buc_bulk_hdl = local_bulk_handle;
	cb_info->buc_iv_value = iv_value;
	cb_info->buc_user_priv = user_priv;

	crt_req_addref(rpc_req);

	rc = crt_bulk_transfer(&bulk_desc, bulk_update_transfer_done,
				cb_info, 0);
	if (rc != 0) {
		D_ERROR("crt_bulk_transfer() failed; rc=%d\n", rc);
		crt_bulk_free(local_bulk_handle);
		crt_req_decref(rpc_req);
		D_GOTO(send_error, rc);
	}

exit:
	return;

send_error:
	output->rc = rc;

	crt_reply_send(rpc_req);
}

static int
crt_iv_update_internal(crt_iv_namespace_t ivns, uint32_t class_id,
		       crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
		       d_sg_list_t *iv_value, crt_iv_shortcut_t shortcut,
		       crt_iv_sync_t sync_type, crt_iv_comp_cb_t update_comp_cb,
		       void *cb_arg)
{
	struct crt_iv_ops		*iv_ops;
	struct crt_ivns_internal	*ivns_internal;
	d_rank_t			 root_rank;
	d_rank_t			 next_node;
	struct update_cb_info		*cb_info;
	void				*priv;
	int				 rc = 0;

	ivns_internal = crt_ivns_internal_get(ivns);
	if (ivns_internal == NULL) {
		D_ERROR("Invalid ivns specified\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	iv_ops = crt_iv_ops_get(ivns_internal, class_id);
	if (iv_ops == NULL) {
		D_ERROR("Invalid class_id specified\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	rc = iv_ops->ivo_on_hash(ivns, iv_key, &root_rank);
	if (rc != 0) {
		D_ERROR("ivo_on_hash() failed; rc=%d\n", rc);
		D_GOTO(exit, rc);
	}

	rc = iv_ops->ivo_on_get(ivns, iv_key,
				0, CRT_IV_PERM_WRITE, NULL, &priv);

	if (iv_value != NULL)
		rc = iv_ops->ivo_on_update(ivns, iv_key, 0,
			(root_rank == ivns_internal->cii_local_rank),
			iv_value, priv);
	else
		rc = iv_ops->ivo_on_refresh(ivns, iv_key, 0, NULL,
					true, 0, priv);

	if (rc == 0) {
		/* issue sync. will call completion callback */
		rc = crt_ivsync_rpc_issue(ivns_internal, class_id, iv_key,
				iv_ver, iv_value, sync_type,
				ivns_internal->cii_local_rank,
				root_rank, update_comp_cb, cb_arg, priv, rc);
	} else  if (rc == -DER_IVCB_FORWARD) {
		if (shortcut == CRT_IV_SHORTCUT_TO_ROOT)
			next_node = root_rank;
		else if (shortcut == CRT_IV_SHORTCUT_NONE)
			next_node = crt_iv_parent_get(ivns_internal, root_rank);
		else {
			D_ERROR("Unknown shortcut argument %d\n", shortcut);
			D_GOTO(put, rc = -DER_INVAL);
		}

		D_ALLOC_PTR(cb_info);
		if (cb_info == NULL)
			D_GOTO(put, rc = -DER_NOMEM);

		cb_info->uci_comp_cb = update_comp_cb;
		cb_info->uci_cb_arg = cb_arg;

		cb_info->uci_child_rpc = NULL;
		cb_info->uci_ivns_internal = ivns_internal;
		cb_info->uci_class_id = class_id;
		cb_info->uci_caller_rank = ivns_internal->cii_local_rank;

		rc = crt_ivu_rpc_issue(next_node, iv_key, iv_value,
					&sync_type, root_rank, cb_info);

		if (rc != 0) {
			D_ERROR("crt_ivu_rpc_issue() failed; rc=%d\n", rc);
			D_FREE_PTR(cb_info);
			D_GOTO(put, rc);
		}
	} else {
		D_ERROR("ivo_on_update failed with rc = %d\n", rc);

		update_comp_cb(ivns, class_id, iv_key, NULL,
			       iv_value, rc, cb_arg);
		D_GOTO(put, rc);
	}

put:
	iv_ops->ivo_on_put(ivns, NULL, priv);
exit:
	return rc;
}

int
crt_iv_update(crt_iv_namespace_t ivns, uint32_t class_id,
	      crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
	      d_sg_list_t *iv_value, crt_iv_shortcut_t shortcut,
	      crt_iv_sync_t sync_type, crt_iv_comp_cb_t update_comp_cb,
	      void *cb_arg)
{
	int rc;

	/* TODO: In future consider allowing updates with NULL value.
	* Currently calling crt_iv_update_internal with NULL value results in
	* internal 'invalidate' call being done on the specified key.
	*
	* All other checks are performed inside of crt_iv_update_interna.
	*/
	if (iv_value == NULL) {
		D_ERROR("iv_value is NULL\n");

		rc = -DER_INVAL;
		update_comp_cb(ivns, class_id, iv_key, NULL, iv_value,
			rc, cb_arg);
		D_GOTO(exit, rc);
	}

	rc = crt_iv_update_internal(ivns, class_id, iv_key, iv_ver, iv_value,
				shortcut, sync_type, update_comp_cb, cb_arg);

exit:
	return rc;
}

int
crt_iv_invalidate(crt_iv_namespace_t ivns, uint32_t class_id,
		crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
		crt_iv_shortcut_t shortcut, crt_iv_sync_t sync_type,
		crt_iv_comp_cb_t invali_comp_cb,
		  void *cb_arg)
{
	return crt_iv_update_internal(ivns, class_id, iv_key, iv_ver, NULL,
			shortcut, sync_type, invali_comp_cb, cb_arg);
}

int
crt_iv_get_nchildren(crt_iv_namespace_t ivns, uint32_t class_id,
		    crt_iv_key_t *iv_key, uint32_t *nchildren)
{
	struct crt_iv_ops		*iv_ops;
	struct crt_ivns_internal	*ivns_internal;
	struct crt_grp_priv		*grp_priv;
	d_rank_t			 root_rank;
	d_rank_t			 self_rank;
	int				 rc = 0;

	if (iv_key == NULL || nchildren == NULL) {
		D_ERROR("invalid parameter (NULL key or nchildren).\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}
	ivns_internal = crt_ivns_internal_get(ivns);
	if (ivns_internal == NULL) {
		D_ERROR("Invalid ivns specified\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}
	rc = crt_group_rank(ivns_internal->cii_grp, &self_rank);
	if (rc != 0) {
		D_ERROR("crt_group_rank(grp %s) failed, rc=%d.\n",
			ivns_internal->cii_grp->cg_grpid, rc);
		D_GOTO(exit, rc);
	}

	iv_ops = crt_iv_ops_get(ivns_internal, class_id);
	if (iv_ops == NULL) {
		D_ERROR("Invalid class_id specified\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}
	rc = iv_ops->ivo_on_hash(ivns, iv_key, &root_rank);
	if (rc != 0) {
		D_ERROR("ivo_on_hash() failed; rc=%d\n", rc);
		D_GOTO(exit, rc);
	}

	grp_priv = crt_grp_pub2priv(ivns_internal->cii_grp);
	rc = crt_tree_get_nchildren(grp_priv, 0, NULL,
		ivns_internal->cii_gns.gn_tree_topo, root_rank, self_rank,
		nchildren);
	if (rc != 0)
		D_ERROR("crt_tree_get_nchildren(grp %s, root %d self %d), "
			"failed, rc=%d.\n", ivns_internal->cii_grp->cg_grpid,
			root_rank, self_rank, rc);

exit:
	return rc;
}
