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
#include <crt_internal.h>
#include <crt_iv.h>

static CRT_LIST_HEAD(ns_list);
static uint32_t ns_id;

/* Lock for manimuplation of ns_list and ns_id */
static pthread_mutex_t ns_list_lock = PTHREAD_MUTEX_INITIALIZER;

/* Data structure for internal iv fetch rpc input */
struct iv_fetch_in {
	/* Namespace ID */
	crt_iov_t	ifi_nsid;
	/* IV Key */
	crt_iov_t	ifi_key;
	/* Bulk handle for iv value */
	crt_bulk_t	ifi_value_bulk;
	/* Class id */
	uint32_t	ifi_class_id;
	/* Root node for current fetch operation */
	crt_rank_t	ifi_root_node;
};

/* Data structure for internal iv fetch rpc output*/
struct iv_fetch_out {
	/* Resultant return code of fetch rpc */
	uint32_t	ifo_rc;
};

/* Structure for uniquely identifying iv namespace */
struct crt_ivns_id {
	/* Rank of the namespace */
	crt_rank_t	ii_rank;
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
	crt_iv_comp_cb_t		ifc_comp_cb;
	void				*ifc_comp_cb_arg;

	/* Local bulk handle for iv value */
	crt_bulk_t			ifc_bulk_hdl;

	/* Optional child's rpc and childs bulk handle, if child exists */
	crt_rpc_t			*ifc_child_rpc;
	crt_bulk_t			ifc_child_bulk;

	/* IV value */
	crt_sg_list_t			ifc_iv_value;

	/* IV namespace */
	struct crt_ivns_internal	*ifc_ivns_internal;

	/* Class ID for ivns_internal */
	uint32_t			ifc_class_id;
};

/* Structure for storing of pending iv fetch operations */
struct pending_fetch {
	struct iv_fetch_cb_info		*pf_cb_info;

	/* Link to ivf_key_in_progress::kip_pending_fetch_list */
	crt_list_t			pf_link;
};

/* Struture for list of all pending fetches for given key */
struct ivf_key_in_progress {
	crt_iv_key_t	kip_key;
	crt_list_t	kip_pending_fetch_list;

	/* Link to crt_ivns_internal::cii_keys_in_progress_list */
	crt_list_t	kip_link;

	/* Payload for kip_key->iov_buf */
	uintptr_t	payload[0];
};

/* Internal ivns structure */
struct crt_ivns_internal {
	/* IV Classes registered with this iv namespace */
	struct crt_iv_class	*cii_iv_classes;
	/* Context associated with IV namesapce */
	crt_context_t		cii_ctx;

	/* Group to which this namesapce belongs*/
	crt_group_t		*cii_grp;

	/* Global namespace identifier */
	struct crt_global_ns	cii_gns;

	/* Cached info to avoid cart queries */
	crt_rank_t		cii_local_rank;
	uint32_t		cii_group_size;

	/* Link list of all keys in progress */
	crt_list_t		cii_keys_in_progress_list;

	/* Lock for modification of pending list */
	pthread_mutex_t		cii_lock;

	/* Link to ns_list */
	crt_list_t		cii_link;
};

static int
crt_ivf_bulk_transfer(struct crt_ivns_internal *ivns_internal,
			uint32_t class_id, crt_iov_t *iv_key,
			crt_sg_list_t *iv_value, crt_bulk_t dest_bulk,
			crt_rpc_t *rpc);

static struct crt_iv_ops *
crt_iv_ops_get(struct crt_ivns_internal *ivns_internal, uint32_t class_id);

static bool
crt_iv_keys_match(crt_iv_key_t *key1, crt_iv_key_t *key2)
{
	/* Those below are critical, unrecoverable errors */
	C_ASSERT(key1 != NULL);
	C_ASSERT(key2 != NULL);
	C_ASSERT(key1->iov_buf != NULL);
	C_ASSERT(key2->iov_buf != NULL);

	if (key1->iov_len != key2->iov_len)
		return false;

	if (memcmp(key1->iov_buf, key2->iov_buf, key1->iov_len) == 0)
		return true;

	return false;
}

/* Check if key is in progress */
static struct ivf_key_in_progress *
crt_ivf_key_in_progress_find(struct crt_ivns_internal *ivns, crt_iv_key_t *key)
{
	struct ivf_key_in_progress *entry;

	crt_list_for_each_entry(entry, &ivns->cii_keys_in_progress_list,
				kip_link) {
		if (crt_iv_keys_match(&entry->kip_key, key) == true)
			return entry;
	}

	return NULL;
}

/* Mark key as being in progress */
static int
crt_ivf_in_progress_set(struct crt_ivns_internal *ivns,
			crt_iv_key_t *key)
{
	struct ivf_key_in_progress	*entry;
	int				rc = 0;

	C_ALLOC(entry, offsetof(struct ivf_key_in_progress,
				payload[0]) + key->iov_buf_len);
	if (!entry) {
		C_ERROR("Failed to allocate entry");
		return -CER_NOMEM;
	}

	entry->kip_key.iov_buf = entry->payload;
	entry->kip_key.iov_buf_len = key->iov_buf_len;
	entry->kip_key.iov_len = key->iov_len;

	memcpy(entry->kip_key.iov_buf, key->iov_buf, key->iov_buf_len);
	CRT_INIT_LIST_HEAD(&entry->kip_pending_fetch_list);

	/* TODO: Change to hash table */
	crt_list_add_tail(&entry->kip_link, &ivns->cii_keys_in_progress_list);

	return rc;
}

/* Reverse operation of crt_ivf_in_progress_set */
static int
crt_ivf_in_progress_unset(struct ivf_key_in_progress *entry,
			crt_iv_key_t *key)
{
	if (!entry)
		return 0;

	crt_list_del(&entry->kip_link);
	C_FREE(entry, offsetof(struct ivf_key_in_progress,
		payload[0]) + key->iov_buf_len);

	return 0;
}


/* Add key to the list of pending requests */
static int
crt_ivf_pending_request_add(struct ivf_key_in_progress *entry,
			struct iv_fetch_cb_info *iv_info)
{
	struct pending_fetch	*pending_fetch;

	C_ALLOC_PTR(pending_fetch);
	if (pending_fetch == NULL) {
		C_ERROR("Failed to allocate pending fetch");
		return -CER_NOMEM;
	}

	pending_fetch->pf_cb_info = iv_info;

	crt_list_add_tail(&pending_fetch->pf_link,
			&entry->kip_pending_fetch_list);
	return 0;
}

/* Finalize fetch operation by either performing bulk transfer or
 * invoking fetch completion callback
 */
static int
crt_ivf_finalize(struct iv_fetch_cb_info *iv_info, crt_iv_key_t *iv_key,
		crt_sg_list_t *iv_value, int output_rc)
{
	crt_rpc_t		*rpc;
	int			rc = 0;
	struct crt_iv_ops	*iv_ops;

	rpc = iv_info->ifc_child_rpc;

	if (rpc) {
		/* If there is child to respond to - bulk transfer to it */
		if (output_rc == 0) {
			rc = crt_ivf_bulk_transfer(iv_info->ifc_ivns_internal,
						iv_info->ifc_class_id,
						iv_key, iv_value,
						iv_info->ifc_child_bulk,
						rpc);
		} else {
			struct iv_fetch_out *output;

			iv_ops = crt_iv_ops_get(iv_info->ifc_ivns_internal,
				iv_info->ifc_class_id);

			iv_ops->ivo_on_put(iv_info->ifc_ivns_internal,
					iv_key, 0, iv_value);

			output = crt_reply_get(rpc);
			output->ifo_rc = output_rc;

			rc = crt_reply_send(rpc);
			C_ASSERT(rc == 0);

			/* addref done in crt_hdlr_iv_fetch */
			rc = crt_req_decref(rpc);
			C_ASSERT(rc == 0);
		}
	} else {
		iv_info->ifc_comp_cb(iv_info->ifc_ivns_internal,
					iv_info->ifc_class_id,
					iv_key, NULL,
					iv_value,
					output_rc,
					iv_info->ifc_comp_cb_arg);
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
	crt_sg_list_t			tmp_value;
	struct iv_fetch_cb_info		*iv_info;
	int				rc = 0;
	bool				need_put = false;

	/* TODO: This function executes with lock; consider changing it */
	entry = crt_ivf_key_in_progress_find(ivns_internal, key);

	iv_ops = crt_iv_ops_get(ivns_internal, class_id);
	C_ASSERT(iv_ops != NULL);

	/* Key is not in progress - safe to exit */
	if (!entry)
		C_GOTO(exit, rc);

	/* If there is nothing pending - exit */
	if (crt_list_empty(&entry->kip_pending_fetch_list))
		C_GOTO(cleanup, rc);

	rc = iv_ops->ivo_on_get(ivns_internal, key, 0,
				CRT_IV_PERM_READ, &tmp_value);

	if (rc != 0) {
		C_ERROR("ivo_on_get() returned rc=%d\n", rc);
		C_GOTO(cleanup, rc);
	}
	need_put = true;

	/* TODO: stage2 -- pass root flag */
	rc = iv_ops->ivo_on_fetch(ivns_internal, key, 0, false,
				&tmp_value);

	if (rc != 0) {
		C_ERROR("Unexpected error. Retrying on_fetch failed\n");
		C_GOTO(cleanup, rc);
	}

	/* Go through list of all pending fetches and finalize each one */
	crt_list_for_each_entry_safe(pending_fetch, next,
				&entry->kip_pending_fetch_list, pf_link) {
		iv_info = pending_fetch->pf_cb_info;

		crt_ivf_finalize(iv_info, key, &tmp_value, rc_value);

		crt_list_del(&pending_fetch->pf_link);
		C_FREE_PTR(pending_fetch->pf_cb_info);
		C_FREE_PTR(pending_fetch);
	}

cleanup:
	crt_list_del_init(&entry->kip_link);

	if (need_put)
		iv_ops->ivo_on_put(ivns_internal, key, 0, &tmp_value);

	C_FREE_PTR(entry);
exit:
	return rc;
}

/* Helper function to lookup ivns_internal based on ivns id */
static struct crt_ivns_internal *
crt_ivns_internal_lookup(struct crt_ivns_id *ivns_id)
{
	struct crt_ivns_internal *entry;

	pthread_mutex_lock(&ns_list_lock);
	crt_list_for_each_entry(entry, &ns_list, cii_link) {
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
	int				size;
	int				rc = 0;

	C_ALLOC_PTR(ivns_internal);
	if (ivns_internal == NULL) {
		C_ERROR("Failed to allocate memory for ivns_internal\n");
		C_GOTO(exit, ivns_internal);
	}

	size = sizeof(struct crt_iv_class) * num_class;
	C_ALLOC(ivns_internal->cii_iv_classes, size);
	if (ivns_internal->cii_iv_classes == NULL) {
		C_ERROR("Failed to allocate storage for iv_classes\n");
		C_FREE_PTR(ivns_internal);
		C_GOTO(exit, ivns_internal = NULL);
	}

	CRT_INIT_LIST_HEAD(&ivns_internal->cii_keys_in_progress_list);

	rc = crt_group_rank(grp, &ivns_internal->cii_local_rank);
	C_ASSERT(rc == 0);

	rc = crt_group_size(grp, &ivns_internal->cii_group_size);
	C_ASSERT(rc == 0);

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

	memcpy(ivns_internal->cii_iv_classes, iv_classes, size);

	ivns_internal->cii_gns.gn_num_class = num_class;
	ivns_internal->cii_gns.gn_tree_topo = tree_topo;
	ivns_internal->cii_ctx = crt_ctx;

	ivns_internal->cii_grp = grp;

	if (grp == NULL)
		ivns_internal->cii_gns.gn_grp_id = NULL;
	else
		ivns_internal->cii_gns.gn_grp_id = grp->cg_grpid;

	pthread_mutex_lock(&ns_list_lock);
	crt_list_add_tail(&ivns_internal->cii_link, &ns_list);
	pthread_mutex_unlock(&ns_list_lock);

exit:
	return ivns_internal;
}

int
crt_iv_namespace_create(crt_context_t crt_ctx, crt_group_t *grp, int tree_topo,
			struct crt_iv_class *iv_classes, uint32_t num_class,
			crt_iv_namespace_t *ivns, crt_iov_t *g_ivns)
{
	struct crt_ivns_internal	*ivns_internal = NULL;
	int				rc = 0;

	if (ivns == NULL || g_ivns == NULL) {
		C_ERROR("invalid parameter of NULL for ivns/g_ivns\n");
		C_GOTO(exit, rc = -CER_INVAL);
	}

	ivns_internal = crt_ivns_internal_create(crt_ctx, grp,
						iv_classes, num_class,
						tree_topo, NULL);
	if (ivns_internal == NULL) {
		C_ERROR("Failed to create internal ivns\n");
		C_GOTO(exit, rc = -CER_NOMEM);
	}

	*ivns = (crt_iv_namespace_t)ivns_internal;

	/* TODO: Need to flatten the structure */
	g_ivns->iov_buf = &ivns_internal->cii_gns;
	g_ivns->iov_buf_len = sizeof(struct crt_global_ns);
	g_ivns->iov_len = sizeof(struct crt_global_ns);

exit:
	if (rc != 0 && ivns_internal)
		C_FREE_PTR(ivns_internal);

	return rc;
}

int
crt_iv_namespace_attach(crt_context_t crt_ctx, crt_iov_t *g_ivns,
			struct crt_iv_class *iv_classes, uint32_t num_class,
			crt_iv_namespace_t *ivns)
{
	struct crt_ivns_internal	*ivns_internal = NULL;
	struct crt_global_ns		*ivns_global = NULL;
	crt_group_t			*grp;
	int				rc = 0;

	if (g_ivns == NULL) {
		C_ERROR("global ivns is NULL\n");
		C_GOTO(exit, rc = -CER_INVAL);
	}

	if (iv_classes == NULL) {
		C_ERROR("iv_classes is NULL\n");
		C_GOTO(exit, rc = -CER_INVAL);
	}

	if (ivns == NULL) {
		C_ERROR("ivns is NULL\n");
		C_GOTO(exit, rc = -CER_INVAL);
	}

	/* TODO: Need to unflatten the structure */
	ivns_global = (struct crt_global_ns *)g_ivns->iov_buf;

	grp = crt_group_lookup(ivns_global->gn_grp_id);

	ivns_internal = crt_ivns_internal_create(crt_ctx, grp,
					iv_classes, num_class,
					ivns_global->gn_tree_topo,
					&ivns_global->gn_ivns_id);
	if (ivns_internal == NULL) {
		C_ERROR("Failed to create new ivns internal\n");
		C_GOTO(exit, rc = -CER_NOMEM);
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
		C_ERROR("Invalid ivns passed\n");
		C_GOTO(exit, rc = -CER_INVAL);
	}

	pthread_mutex_lock(&ns_list_lock);
	crt_list_del(&ivns_internal->cii_link);
	pthread_mutex_unlock(&ns_list_lock);

	/* TODO: stage2 - wait for all pending requests to be finished*/
	/* TODO: Need refcount on ivns_internal to know when to free it */
	pthread_mutex_destroy(&ivns_internal->cii_lock);

	C_FREE_PTR(ivns_internal->cii_iv_classes);
	C_FREE_PTR(ivns_internal);

exit:
	return rc;
}

/* Return iv_ops based on class_id passed */
static struct crt_iv_ops *
crt_iv_ops_get(struct crt_ivns_internal *ivns_internal, uint32_t class_id)
{
	if (ivns_internal == NULL) {
		C_ERROR("ivns_internal was NULL\n");
		return NULL;
	}

	if (class_id >= ivns_internal->cii_gns.gn_num_class) {
		C_ERROR("class_id=%d exceeds num_class=%d\n", class_id,
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
	uint32_t			tci_class_id;

	/* IV Key for which fetch was performed */
	crt_iov_t			*tci_iv_key;

	/* IV value for which fetch was performed */
	crt_sg_list_t			*tci_iv_value;
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
	C_ASSERT(info != NULL);

	cb_info = (struct crt_ivf_transfer_cb_info *)info->bci_arg;
	rpc = info->bci_bulk_desc->bd_rpc;

	output = crt_reply_get(rpc);
	output->ifo_rc = info->bci_rc;

	/* Keep freeing things even if something fails */
	rc = crt_reply_send(rpc);
	if (rc != 0)
		C_ERROR("crt_reply_send() failed; rc = %d\n", rc);

	rc = crt_bulk_free(info->bci_bulk_desc->bd_local_hdl);
	if (rc != 0)
		C_ERROR("crt_bulk_free() failed; rc = %d\n", rc);

	rc = crt_req_decref(rpc);
	if (rc != 0)
		C_ERROR("crt_req_decref() failed; rc = %d\n", rc);

	iv_ops = crt_iv_ops_get(cb_info->tci_ivns_internal,
				cb_info->tci_class_id);
	C_ASSERT(iv_ops != NULL);

	rc = iv_ops->ivo_on_put(cb_info->tci_ivns_internal,
				cb_info->tci_iv_key, 0, cb_info->tci_iv_value);
	if (rc != 0)
		C_ERROR("ivo_on_put() failed; rc = %d\n", rc);

	C_FREE_PTR(cb_info);

	return rc;
}

/* Helper function to issue bulk transfer */
static int
crt_ivf_bulk_transfer(struct crt_ivns_internal *ivns_internal,
			uint32_t class_id, crt_iov_t *iv_key,
			crt_sg_list_t *iv_value, crt_bulk_t dest_bulk,
			crt_rpc_t *rpc)
{
	struct crt_ivf_transfer_cb_info	*cb_info;
	struct crt_bulk_desc		bulk_desc;
	crt_bulk_opid_t			opid;
	crt_bulk_t			bulk_hdl;
	struct iv_fetch_out		*output;
	int				size;
	int				i;
	int				rc = 0;

	output = crt_reply_get(rpc);
	if (output == NULL) {
		C_ERROR("output was NULL\n");
		C_GOTO(exit, rc = -CER_INVAL);
	}

	rc = crt_bulk_create(rpc->cr_ctx, iv_value, CRT_BULK_RW,
			&bulk_hdl);
	if (rc != 0) {
		C_ERROR("crt_bulk_create() failed with rc=%d\n", rc);
		C_GOTO(exit, rc);
	}

	/* Calculate total size of all iovs in sg list */
	size = 0;
	for (i = 0; i < iv_value->sg_nr.num; i++)
		size += iv_value->sg_iovs[i].iov_buf_len;

	rc = crt_req_addref(rpc);
	C_ASSERT(rc == 0);

	bulk_desc.bd_rpc = rpc;
	bulk_desc.bd_bulk_op = CRT_BULK_PUT;
	bulk_desc.bd_remote_hdl = dest_bulk;
	bulk_desc.bd_remote_off = 0;
	bulk_desc.bd_local_hdl = bulk_hdl;
	bulk_desc.bd_local_off = 0;
	bulk_desc.bd_len = size;

	C_ALLOC_PTR(cb_info);

	if (cb_info == NULL) {
		C_ERROR("Failed to allocate memory for cb_info\n");
		C_GOTO(exit, rc = -CER_NOMEM);
	}

	cb_info->tci_ivns_internal = ivns_internal;
	cb_info->tci_class_id = class_id;
	cb_info->tci_iv_key = iv_key;
	cb_info->tci_iv_value = iv_value;

	rc = crt_bulk_transfer(&bulk_desc, crt_ivf_bulk_transfer_done_cb,
				cb_info, &opid);
	if (rc != 0) {
		C_ERROR("crt_bulk_transfer() failed with rc=%d\n", rc);

		output->ifo_rc = -1;
		rc = crt_reply_send(rpc);
		C_ASSERT(rc == 0);

		rc = crt_req_decref(rpc);
		C_ASSERT(rc == 0);

		crt_bulk_free(bulk_hdl);
		C_FREE_PTR(cb_info);
	}

exit:
	return rc;
}

/* Fetch response handler */
static int
handle_ivfetch_response(const struct crt_cb_info *cb_info)
{
	struct iv_fetch_cb_info		*iv_info;
	struct iv_fetch_out		*output;
	struct iv_fetch_in		*input;
	struct crt_iv_ops		*iv_ops;
	crt_rpc_t			*rpc;

	iv_info = (struct iv_fetch_cb_info *)cb_info->cci_arg;

	rpc = cb_info->cci_rpc;
	output = crt_reply_get(rpc);
	input = crt_req_get(rpc);
	C_ASSERT(output != NULL);

	iv_ops = crt_iv_ops_get(iv_info->ifc_ivns_internal,
				input->ifi_class_id);

	if (output->ifo_rc == 0)
		iv_ops->ivo_on_refresh(iv_info->ifc_ivns_internal,
				&input->ifi_key,
				0, /* TODO: iv_ver */
				&iv_info->ifc_iv_value,
				false);

	if (iv_info->ifc_bulk_hdl != 0x0)
		crt_bulk_free(iv_info->ifc_bulk_hdl);

	/* Finalize fetch operation */
	crt_ivf_finalize(iv_info, &input->ifi_key, &iv_info->ifc_iv_value,
			output->ifo_rc);

	/* This needs to happen after on_refresh */
	pthread_mutex_lock(&iv_info->ifc_ivns_internal->cii_lock);
	crt_ivf_pending_reqs_process(iv_info->ifc_ivns_internal,
				iv_info->ifc_class_id,
				&input->ifi_key,
				output->ifo_rc);
	pthread_mutex_unlock(&iv_info->ifc_ivns_internal->cii_lock);

	C_FREE_PTR(iv_info);
	return 0;
}

/* Helper function to issue internal iv_fetch RPC */
static int
crt_ivf_rpc_issue(crt_rank_t dest_node, crt_iv_key_t *iv_key,
		crt_sg_list_t *iv_value, crt_rank_t root_node,
		struct iv_fetch_cb_info *cb_info)
{
	struct crt_ivns_internal	*ivns_internal;
	struct iv_fetch_in		*input;
	crt_bulk_t			local_bulk;
	crt_endpoint_t			ep;
	crt_rpc_t			*rpc;
	struct ivf_key_in_progress	*entry;
	int				rc = 0;

	ivns_internal = cb_info->ifc_ivns_internal;

	/* If there is already forwarded request in progress, do not
	* submit another one, instead add it to pending list
	*/
	pthread_mutex_lock(&ivns_internal->cii_lock);
	entry = crt_ivf_key_in_progress_find(ivns_internal, iv_key);

	if (entry) {
		rc = crt_ivf_pending_request_add(entry, cb_info);
		pthread_mutex_unlock(&ivns_internal->cii_lock);
		return rc;
	}

	rc = crt_ivf_in_progress_set(ivns_internal, iv_key);
	pthread_mutex_unlock(&ivns_internal->cii_lock);

	if (rc != 0) {
		C_ERROR("crt_ivf_in_progress_set() failed; rc = %d\n", rc);
		return rc;
	}

	rc = crt_bulk_create(ivns_internal->cii_ctx, iv_value, CRT_BULK_RW,
				&local_bulk);
	if (rc != 0) {
		C_ERROR("crt_bulk_create() failed; rc = %d\n", rc);

		pthread_mutex_lock(&ivns_internal->cii_lock);

		/* Only unset if there are no pending fetches for this key */
		entry = crt_ivf_key_in_progress_find(ivns_internal, iv_key);

		if (entry && crt_list_empty(&entry->kip_pending_fetch_list))
			crt_ivf_in_progress_unset(entry, iv_key);

		pthread_mutex_unlock(&ivns_internal->cii_lock);
		return rc;
	}

	ep.ep_grp = ivns_internal->cii_grp;
	ep.ep_rank = dest_node;
	ep.ep_tag = 0;

	rc = crt_req_create(ivns_internal->cii_ctx, ep, CRT_OPC_IV_FETCH, &rpc);
	if (rc != 0) {
		C_ERROR("crt_req_create() failed; rc = %d\n", rc);
		C_GOTO(exit, rc);
	}

	input = crt_req_get(rpc);
	C_ASSERT(input != NULL);

	input->ifi_value_bulk = local_bulk;

	cb_info->ifc_bulk_hdl = local_bulk;

	crt_iov_set(&input->ifi_key, iv_key->iov_buf, iv_key->iov_buf_len);
	input->ifi_class_id = cb_info->ifc_class_id;
	input->ifi_root_node = root_node;

	crt_iov_set(&input->ifi_nsid, &ivns_internal->cii_gns.gn_ivns_id,
		sizeof(struct crt_ivns_id));

	rc = crt_req_send(rpc, handle_ivfetch_response, cb_info);

exit:
	if (rc != 0) {
		C_ERROR("Failed to send rpc to remote node = %d\n", dest_node);
		pthread_mutex_lock(&ivns_internal->cii_lock);

		/* Only unset if there are no pending fetches for this key */
		entry = crt_ivf_key_in_progress_find(ivns_internal, iv_key);

		if (entry && crt_list_empty(&entry->kip_pending_fetch_list))
			crt_ivf_in_progress_unset(entry, iv_key);

		pthread_mutex_unlock(&ivns_internal->cii_lock);
		crt_bulk_free(local_bulk);
	}

	return rc;
}

/* Return next parent of the 'cur_node' */
static crt_rank_t
crt_iv_ranks_parent_get(struct crt_ivns_internal *ivns_internal,
		crt_rank_t cur_node, crt_rank_t root_node)
{
	struct crt_grp_priv	*grp_priv;
	crt_rank_t		parent_rank;
	crt_group_t		*group;
	int			rc;

	if (cur_node == root_node)
		return root_node;

	/* group and grp_priv should never be NULL by the time we get here */
	group = crt_group_lookup(ivns_internal->cii_gns.gn_grp_id);
	C_ASSERT(group != NULL);

	grp_priv = container_of(group, struct crt_grp_priv, gp_pub);
	C_ASSERT(grp_priv != NULL);

	rc = crt_tree_get_parent(grp_priv, 0, NULL,
			ivns_internal->cii_gns.gn_tree_topo, root_node,
			cur_node, &parent_rank);
	C_ASSERT(rc == 0);

	return parent_rank;
}

/* Return next parent for the current rank and root_node */
static crt_rank_t
crt_iv_parent_get(struct crt_ivns_internal *ivns_internal,
		crt_rank_t root_node)
{
	return crt_iv_ranks_parent_get(ivns_internal,
				ivns_internal->cii_local_rank,
				root_node);
}

/* Internal handler for CRT_OPC_IV_FETCH RPC call*/
int
crt_hdlr_iv_fetch(crt_rpc_t *rpc_req)
{
	struct iv_fetch_in		*input;
	struct iv_fetch_out		*output;
	struct crt_ivns_id		*ivns_id;
	struct crt_ivns_internal	*ivns_internal;
	struct crt_iv_ops		*iv_ops;
	crt_sg_list_t			iv_value;
	int				rc = 0;
	bool				put_needed = false;

	input = crt_req_get(rpc_req);
	output = crt_reply_get(rpc_req);

	ivns_id = (struct crt_ivns_id *)input->ifi_nsid.iov_buf;

	ivns_internal = crt_ivns_internal_lookup(ivns_id);
	if (ivns_internal == NULL) {
		C_ERROR("Failed to lookup ivns internal!\n");
		C_GOTO(send_error, rc = -CER_INVAL);
	}

	iv_ops = crt_iv_ops_get(ivns_internal, input->ifi_class_id);
	if (iv_ops == NULL) {
		C_ERROR("Returned iv_ops were NULL\n");
		C_GOTO(send_error, rc = -CER_INVAL);
	}

	rc = iv_ops->ivo_on_get(ivns_internal, &input->ifi_key,
				0, CRT_IV_PERM_READ, &iv_value);

	if (rc != 0) {
		C_ERROR("ivo_on_get failed; rc=%d\n", rc);
		C_GOTO(send_error, rc);
	}

	put_needed = true;

	rc = iv_ops->ivo_on_fetch(ivns_internal, &input->ifi_key, 0,
				(ivns_internal->cii_local_rank ==
					input->ifi_root_node),
				&iv_value);
	if (rc == 0) {
		rc = crt_req_addref(rpc_req);
		C_ASSERT(rc == 0);

		rc = crt_ivf_bulk_transfer(ivns_internal,
					input->ifi_class_id, &input->ifi_key,
					&iv_value, input->ifi_value_bulk,
					rpc_req);
		if (rc != 0) {
			C_ERROR("bulk transfer failed; rc = %d\n", rc);
			C_GOTO(send_error, rc);
		}
	} else if (rc == -CER_IVCB_FORWARD) {
		crt_rank_t next_node;
		struct iv_fetch_cb_info *cb_info;

		if (ivns_internal->cii_local_rank == input->ifi_root_node) {
			C_ERROR("Forward requested for root node\n");
			C_GOTO(send_error, rc = -CER_INVAL);
		}

		rc = iv_ops->ivo_on_put(ivns_internal, &input->ifi_key,
					0, &iv_value);
		if (rc != 0) {
			C_ERROR("ivo_on_put() returend rc = %d\n", rc);
			C_GOTO(send_error, rc);
		}

		put_needed = false;
		rc = iv_ops->ivo_on_get(ivns_internal, &input->ifi_key,
					0, CRT_IV_PERM_WRITE, &iv_value);
		if (rc != 0) {
			C_ERROR("ivo_on_get() returned rc = %d\n", rc);
			C_GOTO(send_error, rc);
		}

		put_needed = true;

		next_node = crt_iv_parent_get(ivns_internal,
					input->ifi_root_node);
		C_ALLOC_PTR(cb_info);
		if (cb_info == NULL) {
			C_ERROR("Failed to allocate memory for cb_info\n");
			C_GOTO(send_error, rc = -CER_NOMEM);
		}

		cb_info->ifc_child_rpc = rpc_req;
		cb_info->ifc_child_bulk = input->ifi_value_bulk,

		rc = crt_req_addref(rpc_req);
		C_ASSERT(rc == 0);

		cb_info->ifc_iv_value = iv_value;

		cb_info->ifc_ivns_internal = ivns_internal;
		cb_info->ifc_class_id = input->ifi_class_id;

		rc = crt_ivf_rpc_issue(next_node,
					&input->ifi_key, &cb_info->ifc_iv_value,
					input->ifi_root_node,
					cb_info);
		if (rc != 0) {
			C_ERROR("Failed to issue fetch rpc; rc = %d\n", rc);
			C_FREE_PTR(cb_info);
			crt_req_decref(rpc_req);
			C_GOTO(send_error, rc);
		}
	} else {
		C_ERROR("ERROR happened with rc = %d\n", rc);
		C_GOTO(send_error, rc);
	}

	return 0;

send_error:
	if (put_needed)
		iv_ops->ivo_on_put(ivns_internal, &input->ifi_key,
				0, &iv_value);
	output->ifo_rc = rc;
	rc = crt_reply_send(rpc_req);
	C_ASSERT(rc == 0);

	return 0;
}

int
crt_iv_fetch(crt_iv_namespace_t ivns, uint32_t class_id,
	    crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
	    crt_sg_list_t *iv_value, crt_iv_shortcut_t shortcut,
	    crt_iv_comp_cb_t fetch_comp_cb, void *cb_arg)
{
	struct crt_ivns_internal	*ivns_internal;
	struct crt_iv_ops		*iv_ops;
	struct iv_fetch_cb_info		*cb_info;
	crt_rank_t			root_rank;
	crt_rank_t			next_node = 1;
	int				rc;

	if (iv_key == NULL || iv_value == NULL) {
		C_ERROR("iv_key or iv_value are NULL\n");
		C_GOTO(exit, rc = -CER_INVAL);
	}

	ivns_internal = crt_ivns_internal_get(ivns);

	if (ivns_internal == NULL) {
		C_ERROR("Invalid ivns\n");
		C_GOTO(exit, rc = -CER_INVAL);
	}

	iv_ops = crt_iv_ops_get(ivns_internal, class_id);

	if (iv_ops == NULL) {
		C_ERROR("Failed to get iv_ops for class_id = %d\n", class_id);
		C_GOTO(exit, rc = -CER_INVAL);
	}

	rc = iv_ops->ivo_on_hash(ivns, iv_key, &root_rank);

	if (rc != 0) {
		C_ERROR("Failed to get hash\n");
		C_GOTO(exit, rc);
	}

	rc = iv_ops->ivo_on_fetch(ivns_internal, iv_key, 0,
				(ivns_internal->cii_local_rank == root_rank),
				iv_value);
	if (rc == 0) {
		fetch_comp_cb(ivns_internal, class_id, iv_key, NULL,
				iv_value, rc, cb_arg);
		return rc;
	} else if (rc != -CER_IVCB_FORWARD) {
		/* We got error, call the callback and exit */
		fetch_comp_cb(ivns_internal, class_id, iv_key, NULL,
				NULL, rc, cb_arg);

		return rc;
	}

	/* If we reached here, means we got CER_IVCB_FORWARD */

	switch (shortcut) {
	case CRT_IV_SHORTCUT_TO_ROOT:
		next_node = root_rank;
		break;

	case CRT_IV_SHORTCUT_NONE:
		next_node = crt_iv_parent_get(ivns_internal, root_rank);
		break;

	default:
		C_ERROR("Unknown shortcut=%d specified\n", shortcut);
		C_GOTO(exit, rc = -CER_INVAL);
	}

	C_ALLOC_PTR(cb_info);
	if (cb_info == NULL) {
		C_ERROR("Failed to allocate callback info\n");
		C_GOTO(exit, rc = -CER_NOMEM);
	}

	cb_info->ifc_bulk_hdl = CRT_BULK_NULL;

	cb_info->ifc_comp_cb = fetch_comp_cb;
	cb_info->ifc_comp_cb_arg = cb_arg;

	cb_info->ifc_iv_value = *iv_value;

	cb_info->ifc_ivns_internal = ivns_internal;
	cb_info->ifc_class_id = class_id;

	rc = crt_ivf_rpc_issue(next_node, iv_key, iv_value, root_rank,
				cb_info);
exit:
	if (rc != 0) {
		C_ERROR("crt_ivf_rpc_issue() failed; rc = %d\n", rc);
		C_FREE_PTR(cb_info);
	}

	return rc;
}

int
crt_iv_update(crt_iv_namespace_t ivns, uint32_t class_id,
	      crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
	      crt_sg_list_t *iv_value, crt_iv_shortcut_t shortcut,
	      crt_iv_sync_t sync_type, crt_iv_comp_cb_t update_comp_cb,
	      void *cb_arg)
{
	C_ERROR("Not implemented call\n");
	return 0;
}

int
crt_iv_invalidate(crt_iv_namespace_t ivns, uint32_t class_id,
		  crt_iv_key_t *iv_key, crt_iv_comp_cb_t invali_comp_cb,
		  void *cb_arg)
{
	C_ERROR("Not implemented call\n");
	return 0;
}

