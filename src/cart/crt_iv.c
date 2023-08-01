/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
 **/

#define D_LOGFAC	DD_FAC(iv)

#include "crt_internal.h"
#include "cart/iv.h"

#define IV_DBG(key, msg, ...) \
	D_DEBUG(DB_TRACE, "[key=%p] " msg, (key)->iov_buf, ##__VA_ARGS__)

static D_LIST_HEAD(ns_list);

/* Lock for manimuplation of ns_list and ns_id */
static pthread_mutex_t ns_list_lock = PTHREAD_MUTEX_INITIALIZER;

/* Structure for uniquely identifying iv namespace */
struct crt_ivns_id {
	/* Group name associated with namespace */
	crt_group_id_t	ii_group_name;

	/* Unique namespace ID within the group */
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

};

/* Structure for iv fetch callback info */
struct iv_fetch_cb_info {
	/* Fetch completion callback function and its argument */
	crt_iv_comp_cb_t		 ifc_comp_cb;
	void				*ifc_comp_cb_arg;

	/* Local bulk handle for iv value */
	crt_bulk_t			 ifc_bulk_hdl;

	/* Optional child's rpc and child's bulk handle, if child exists */
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

/* Structure for list of all pending fetches for given key */
struct ivf_key_in_progress {
	crt_iv_key_t	kip_key;
	d_list_t	kip_pending_fetch_list;
	pthread_mutex_t	kip_lock;

	bool		kip_rpc_in_progress;
	uint32_t	kip_refcnt;
	/* Link to crt_ivns_internal::cii_keys_in_progress_list */
	d_list_t	kip_link;

	/* Payload for kip_key->iov_buf */
	uintptr_t	payload[0];
};

/* Internal ivns structure */
struct crt_ivns_internal {
	/* IV Classes registered with this iv namespace */
	struct crt_iv_class		*cii_iv_classes;

	/* Context associated with IV namespace */
	crt_context_t			 cii_ctx;

	/* Private group struct associated with IV namespace */
	struct crt_grp_priv		*cii_grp_priv;

	/* Global namespace identifier */
	struct crt_global_ns		 cii_gns;

	/* Link list of all keys in progress */
	d_list_t			 cii_keys_in_progress_list;

	/* Lock for modification of pending list */
	pthread_mutex_t			 cii_lock;

	/* Link to ns_list */
	d_list_t			 cii_link;

	/* ref count spinlock */
	pthread_spinlock_t		 cii_ref_lock;

	/* reference count */
	int				 cii_ref_count;
	/* completion callback for crt_iv_namespace_destroy() */
	crt_iv_namespace_destroy_cb_t	 cii_destroy_cb;
	/* user data for cii_destroy_cb() */
	void				*cii_destroy_cb_arg;
	/* user private data associated with ns */
	void				*cii_user_priv;
};

static void
handle_response_cb(const struct crt_cb_info *cb_info);

static void
ivns_destroy(struct crt_ivns_internal *ivns_internal)
{
	crt_iv_namespace_destroy_cb_t	 destroy_cb;
	crt_iv_namespace_t		 ivns;
	void				*cb_arg;

	D_MUTEX_LOCK(&ns_list_lock);
	D_SPIN_LOCK(&ivns_internal->cii_ref_lock);
	if (ivns_internal->cii_ref_count == 0) {
		d_list_del(&ivns_internal->cii_link);
	} else { /* It was found in ns_list and ref incremented again */
		D_SPIN_UNLOCK(&ivns_internal->cii_ref_lock);
		D_MUTEX_UNLOCK(&ns_list_lock);
		return;
	}
	D_SPIN_UNLOCK(&ivns_internal->cii_ref_lock);
	D_MUTEX_UNLOCK(&ns_list_lock);

	ivns = ivns_internal;
	destroy_cb = ivns_internal->cii_destroy_cb;
	cb_arg = ivns_internal->cii_destroy_cb_arg;

	if (destroy_cb)
		destroy_cb(ivns, cb_arg);

	/* addref in crt_grp_lookup_int_grpid or crt_iv_namespace_create */
	crt_grp_priv_decref(ivns_internal->cii_grp_priv);

	D_MUTEX_DESTROY(&ivns_internal->cii_lock);
	D_SPIN_DESTROY(&ivns_internal->cii_ref_lock);

	D_FREE(ivns_internal->cii_iv_classes);
	ivns_internal->cii_gns.gn_ivns_id.ii_nsid = 0;
	D_FREE(ivns_internal->cii_gns.gn_ivns_id.ii_group_name);
	D_FREE(ivns_internal);
}

#define IVNS_ADDREF(xx)						\
do {								\
	int __ref;						\
	struct crt_ivns_internal *__ivns = xx;			\
								\
	D_SPIN_LOCK(&__ivns->cii_ref_lock);			\
	D_ASSERTF(__ivns->cii_ref_count != 0,			\
		"%p addref from zero\n", __ivns);		\
	__ref = ++__ivns->cii_ref_count;			\
	D_SPIN_UNLOCK(&__ivns->cii_ref_lock);			\
	D_DEBUG(DB_TRACE, "addref to %d ivns=%p\n",		\
		 __ref, __ivns);				\
} while (0)

#define IVNS_DECREF_N(xx, num)					\
do {								\
	int __ref;						\
	struct crt_ivns_internal *__ivns = xx;			\
								\
	if (__ivns == NULL)					\
		break;						\
								\
	D_SPIN_LOCK(&__ivns->cii_ref_lock);			\
	D_ASSERTF(__ivns->cii_ref_count >= (num),		\
		"%p decref(%d) from %d\n",			\
		__ivns, num, __ivns->cii_ref_count);		\
	__ivns->cii_ref_count -= num;				\
	__ref = __ivns->cii_ref_count;				\
	D_SPIN_UNLOCK(&__ivns->cii_ref_lock);			\
	D_DEBUG(DB_TRACE, "decref to %d ivns=%p\n",		\
		__ref, __ivns);					\
								\
	if (__ref == 0)						\
		ivns_destroy(__ivns);				\
} while (0)

#define IVNS_DECREF(xx) IVNS_DECREF_N(xx, 1)

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

/* Check if key is in progress; if so return locked KIP entry */
static struct ivf_key_in_progress *
crt_ivf_key_in_progress_find(struct crt_ivns_internal *ivns,
			     struct crt_iv_ops *ops, crt_iv_key_t *key)
{
	struct ivf_key_in_progress *entry;
	bool found = false;

	d_list_for_each_entry(entry, &ivns->cii_keys_in_progress_list,
			      kip_link) {
		/* Use keys_match callback if client provided one */
		if (ops->ivo_keys_match) {
			if (ops->ivo_keys_match(ivns, &entry->kip_key, key)) {
				found = true;
				break;
			}
		} else {
			if (crt_iv_keys_match(&entry->kip_key, key)) {
				found = true;
				break;
			}
		}
	}

	if (found) {
		D_MUTEX_LOCK(&entry->kip_lock);
		return entry;
	}

	return NULL;
}

/* Mark key as being in progress */
static struct ivf_key_in_progress *
crt_ivf_key_in_progress_set(struct crt_ivns_internal *ivns,
			    crt_iv_key_t *key)
{
	struct ivf_key_in_progress	*entry;
	int				rc;

	D_ALLOC(entry, offsetof(struct ivf_key_in_progress,
				payload[0]) + key->iov_buf_len);
	if (entry == NULL)
		return NULL;

	rc = D_MUTEX_INIT(&entry->kip_lock, 0);
	if (rc != 0) {
		D_FREE(entry);
		return NULL;
	}

	entry->kip_key.iov_buf = entry->payload;
	entry->kip_key.iov_buf_len = key->iov_buf_len;
	entry->kip_key.iov_len = key->iov_len;

	entry->kip_refcnt = 0;

	memcpy(entry->kip_key.iov_buf, key->iov_buf, key->iov_buf_len);
	D_INIT_LIST_HEAD(&entry->kip_pending_fetch_list);

	/* TODO: Change to hash table */
	d_list_add_tail(&entry->kip_link, &ivns->cii_keys_in_progress_list);

	D_MUTEX_LOCK(&entry->kip_lock);

	return entry;
}

/* Reverse operation of crt_ivf_key_in_progress_set
 * Caller must hold entry->kip_lock before calling
 * Returns true if entry is destroyed, false otherwise
 */
static bool
crt_ivf_key_in_progress_unset(struct crt_ivns_internal *ivns,
			      struct ivf_key_in_progress *entry)
{
	if (!entry)
		return true;

	entry->kip_refcnt--;
	D_DEBUG(DB_TRACE, "kip_entry=%p  refcnt=%d\n", entry,
		entry->kip_refcnt);

	if (entry->kip_refcnt == 0) {
		d_list_del(&entry->kip_link);

		D_MUTEX_UNLOCK(&entry->kip_lock);
		D_MUTEX_DESTROY(&entry->kip_lock);
		D_FREE(entry);
		return true;
	}

	return false;
}

/* Add key to the list of pending requests */
static int
crt_ivf_pending_request_add(struct crt_ivns_internal *ivns_internal,
			    struct crt_iv_ops *iv_ops,
			    struct ivf_key_in_progress *entry,
			    struct iv_fetch_cb_info *iv_info)
{
	struct pending_fetch	*pending_fetch;

	D_ALLOC_PTR(pending_fetch);
	if (pending_fetch == NULL)
		return -DER_NOMEM;

	/* ivo_on_get() was done by the caller of crt_ivf_rpc_issue */
	iv_ops->ivo_on_put(ivns_internal, &iv_info->ifc_iv_value,
			   iv_info->ifc_user_priv);

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
		 int output_rc)
{
	crt_rpc_t		*rpc;
	int			 rc = 0;
	struct crt_iv_ops	*iv_ops;
	d_sg_list_t		*iv_value;
	bool			 need_put = true;

	iv_value = &iv_info->ifc_iv_value;
	rpc = iv_info->ifc_child_rpc;
	iv_ops = crt_iv_ops_get(iv_info->ifc_ivns_internal,
				iv_info->ifc_class_id);
	D_ASSERT(iv_ops != NULL);

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
			if (rc != 0)
				D_ERROR("Bulk transfer failed for key=%p\n",
					iv_key);
			else
				need_put = false;
		} else {
			struct crt_iv_fetch_out *output;

			output = crt_reply_get(rpc);
			output->ifo_rc = output_rc;

			/* Reply can fail */
			crt_reply_send(rpc);
		}

		/* addref done in crt_hdlr_iv_fetch */
		RPC_PUB_DECREF(rpc);
	} else {
		iv_info->ifc_comp_cb(iv_info->ifc_ivns_internal,
					iv_info->ifc_class_id,
					iv_key, NULL,
					iv_value,
					output_rc,
					iv_info->ifc_comp_cb_arg);
	}

	if (need_put)
		iv_ops->ivo_on_put(iv_info->ifc_ivns_internal, iv_value,
				   iv_info->ifc_user_priv);

	return rc;
}

/* Process pending requests for the specified ivns and key */
static int
crt_ivf_pending_reqs_process(struct crt_ivns_internal *ivns_internal,
			     uint32_t class_id,
			     struct ivf_key_in_progress *kip_entry,
			     uint32_t rc_value)
{
	struct crt_iv_ops		*iv_ops;
	struct pending_fetch		*pending_fetch;
	struct iv_fetch_cb_info		*iv_info;
	struct crt_iv_fetch_out		*output;
	int				 rc = 0;
	bool				 put_needed = false;

	iv_ops = crt_iv_ops_get(ivns_internal, class_id);
	D_ASSERT(iv_ops != NULL);

	/* Key is not in progress - safe to exit */
	if (!kip_entry)
		D_GOTO(exit, rc);

	D_DEBUG(DB_TRACE, "Processing requests for kip_entry=%p\n", kip_entry);

	/* Go through list of all pending fetches and finalize each one */
	while ((pending_fetch = d_list_pop_entry(
					&kip_entry->kip_pending_fetch_list,
					 struct pending_fetch,
					 pf_link))) {
		d_sg_list_t tmp_iv_value = {0};

		iv_info = pending_fetch->pf_cb_info;

		IV_DBG(&iv_info->ifc_iv_key,
		       "Processing request for kip=%p\n", kip_entry);

		/* Pending remote fetch case */
		if (iv_info->ifc_child_rpc) {
			IV_DBG(&iv_info->ifc_iv_key,
			       "pending remote fetch for kip=%p\n", kip_entry);

			/* For failed fetches respond to the child with error */
			if (rc_value != 0) {
				output = crt_reply_get(iv_info->ifc_child_rpc);

				output->ifo_rc = rc_value;

				/* Failing to send response isn't fatal */
				rc = crt_reply_send(iv_info->ifc_child_rpc);
				if (rc != 0)
					D_ERROR("crt_reply_send(): "DF_RC"\n",
						DP_RC(rc));

				/* addref done in crt_hdlr_iv_fetch */
				RPC_PUB_DECREF(iv_info->ifc_child_rpc);

				IVNS_DECREF(iv_info->ifc_ivns_internal);
				D_FREE(iv_info);

				D_FREE(pending_fetch);
				continue;
			}

			rc = iv_ops->ivo_on_get(ivns_internal,
						&iv_info->ifc_iv_key, 0,
						CRT_IV_PERM_READ,
						&tmp_iv_value,
						&iv_info->ifc_user_priv);

			put_needed = false;
			if (rc == 0) {
				put_needed = true;
				rc = iv_ops->ivo_on_fetch(ivns_internal,
					&iv_info->ifc_iv_key, 0x0,
					CRT_IV_FLAG_PENDING_FETCH,
					&tmp_iv_value, iv_info->ifc_user_priv);
			}

			if (rc == 0) {
				/* Function will do IVNS_ADDREF if needed */
				rc = crt_ivf_bulk_transfer(ivns_internal,
							class_id,
							&iv_info->ifc_iv_key,
							&tmp_iv_value,
							iv_info->ifc_child_bulk,
							iv_info->ifc_child_rpc,
							iv_info->ifc_user_priv);
			} else {
				D_ERROR("Failed to process pending request\n");

				output = crt_reply_get(iv_info->ifc_child_rpc);

				output->ifo_rc = rc;
				crt_reply_send(iv_info->ifc_child_rpc);
			}

			if (rc != 0 && put_needed)
				iv_ops->ivo_on_put(ivns_internal, &tmp_iv_value,
						   iv_info->ifc_user_priv);

			/* addref done in crt_hdlr_iv_fetch */
			RPC_PUB_DECREF(iv_info->ifc_child_rpc);
		} else {
			IV_DBG(&iv_info->ifc_iv_key,
			       "pending local fetch for kip=%p\n", kip_entry);

			if (rc_value != 0) {
				iv_info->ifc_comp_cb(ivns_internal, class_id,
						     &iv_info->ifc_iv_key,
						     NULL,
						     &tmp_iv_value, rc_value,
						     iv_info->ifc_comp_cb_arg);

				IVNS_DECREF(iv_info->ifc_ivns_internal);
				D_FREE(iv_info);
				D_FREE(pending_fetch);

				continue;
			}

			/* Pending local fetch case */
			rc = iv_ops->ivo_on_get(ivns_internal,
						&iv_info->ifc_iv_key,
						0, CRT_IV_PERM_READ,
						&tmp_iv_value,
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

		IVNS_DECREF(iv_info->ifc_ivns_internal);
		D_FREE(iv_info);
		D_FREE(pending_fetch);
	}

	D_DEBUG(DB_TRACE, "Done processing requests for kip_entry=%p\n",
		kip_entry);

	kip_entry->kip_rpc_in_progress = false;
	D_MUTEX_UNLOCK(&kip_entry->kip_lock);

	/* Grab an entry again and make sure RPC hasn't been submitted
	* by crt_ivf_rpc_issue() logic
	*/
	D_MUTEX_LOCK(&ivns_internal->cii_lock);
	D_MUTEX_LOCK(&kip_entry->kip_lock);
	D_DEBUG(DB_TRACE, "kip_entry=%p in_prog=%d\n",
		kip_entry, kip_entry->kip_rpc_in_progress);

	if (kip_entry->kip_rpc_in_progress == false) {
		if (crt_ivf_key_in_progress_unset(ivns_internal,
						  kip_entry) == false)
			D_MUTEX_UNLOCK(&kip_entry->kip_lock);
	} else {
		D_MUTEX_UNLOCK(&kip_entry->kip_lock);
	}
	D_MUTEX_UNLOCK(&ivns_internal->cii_lock);

exit:
	return rc;
}

/* Helper function to lookup ivns_internal based on ivns id */
static struct crt_ivns_internal *
crt_ivns_internal_lookup(struct crt_ivns_id *ivns_id)
{
	struct crt_ivns_internal *entry;

	D_MUTEX_LOCK(&ns_list_lock);
	d_list_for_each_entry(entry, &ns_list, cii_link) {
		/* avoid checkpatch warning */
		if ((entry->cii_gns.gn_ivns_id.ii_nsid == ivns_id->ii_nsid) &&
		    (strcmp(entry->cii_gns.gn_ivns_id.ii_group_name,
			    ivns_id->ii_group_name) == 0)) {
			IVNS_ADDREF(entry);

			D_MUTEX_UNLOCK(&ns_list_lock);
			return entry;
		}
	}
	D_MUTEX_UNLOCK(&ns_list_lock);

	D_DEBUG(DB_ALL, "Failed to lookup IVNS for %s:%d\n",
		ivns_id->ii_group_name,
		ivns_id->ii_nsid);

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
crt_ivns_internal_create(crt_context_t crt_ctx, struct crt_grp_priv *grp_priv,
			 struct crt_iv_class *iv_classes, uint32_t num_class,
			 int tree_topo, uint32_t nsid, void *user_priv)
{
	struct crt_ivns_internal	*ivns_internal;
	struct crt_ivns_id		*internal_ivns_id;
	int				rc;

	D_ALLOC_PTR(ivns_internal);
	if (ivns_internal == NULL)
		D_GOTO(exit, 0);

	rc = D_MUTEX_INIT(&ivns_internal->cii_lock, 0);
	if (rc != 0) {
		D_FREE(ivns_internal);
		D_GOTO(exit, ivns_internal = NULL);
	}

	rc = D_SPIN_INIT(&ivns_internal->cii_ref_lock, 0);
	if (rc != 0) {
		D_MUTEX_DESTROY(&ivns_internal->cii_lock);
		D_FREE(ivns_internal);
		D_GOTO(exit, ivns_internal = NULL);
	}

	ivns_internal->cii_ref_count = 1;

	D_ALLOC_ARRAY(ivns_internal->cii_iv_classes, num_class);
	if (ivns_internal->cii_iv_classes == NULL) {
		D_MUTEX_DESTROY(&ivns_internal->cii_lock);
		D_SPIN_DESTROY(&ivns_internal->cii_ref_lock);
		D_FREE(ivns_internal);
		D_GOTO(exit, ivns_internal = NULL);
	}

	D_INIT_LIST_HEAD(&ivns_internal->cii_keys_in_progress_list);

	internal_ivns_id = &ivns_internal->cii_gns.gn_ivns_id;

	internal_ivns_id->ii_nsid = nsid;
	D_STRNDUP(internal_ivns_id->ii_group_name,
		  grp_priv->gp_pub.cg_grpid,
		  CRT_GROUP_ID_MAX_LEN);

	if (internal_ivns_id->ii_group_name == NULL) {
		D_FREE(ivns_internal->cii_iv_classes);
		D_MUTEX_DESTROY(&ivns_internal->cii_lock);
		D_SPIN_DESTROY(&ivns_internal->cii_ref_lock);
		D_FREE(ivns_internal);
		D_GOTO(exit, ivns_internal = NULL);
	}

	memcpy(ivns_internal->cii_iv_classes, iv_classes,
	       sizeof(*iv_classes) * num_class);

	ivns_internal->cii_gns.gn_num_class = num_class;
	ivns_internal->cii_gns.gn_tree_topo = tree_topo;
	ivns_internal->cii_ctx = crt_ctx;

	ivns_internal->cii_grp_priv = grp_priv;
	ivns_internal->cii_user_priv = user_priv;

	D_MUTEX_LOCK(&ns_list_lock);
	d_list_add_tail(&ivns_internal->cii_link, &ns_list);
	D_MUTEX_UNLOCK(&ns_list_lock);

exit:
	return ivns_internal;
}

int
crt_iv_namespace_create(crt_context_t crt_ctx, crt_group_t *grp, int tree_topo,
			struct crt_iv_class *iv_classes, uint32_t num_classes,
			uint32_t iv_ns_id, crt_iv_namespace_t *ivns)
{
	return crt_iv_namespace_create_priv(crt_ctx, grp, tree_topo, iv_classes,
					    num_classes, iv_ns_id, NULL, ivns);
}

int
crt_iv_namespace_create_priv(crt_context_t crt_ctx, crt_group_t *grp,
			     int tree_topo, struct crt_iv_class *iv_classes,
			     uint32_t num_classes, uint32_t iv_ns_id,
			     void *user_priv,
			     crt_iv_namespace_t *ivns)
{
	struct crt_ivns_internal	*ivns_internal = NULL;
	struct crt_grp_priv		*grp_priv = NULL;
	int				rc = 0;

	if (ivns == NULL) {
		D_ERROR("Passed ivns is NULL\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	grp_priv = crt_grp_pub2priv(grp);
	if (grp_priv == NULL) {
		D_ERROR("Invalid group passed\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}
	/* decref done in crt_iv_namespace_destroy */
	crt_grp_priv_addref(grp_priv);

	ivns_internal = crt_ivns_internal_create(crt_ctx, grp_priv,
						 iv_classes, num_classes,
						 tree_topo, iv_ns_id,
						 user_priv);
	if (ivns_internal == NULL) {
		D_ERROR("Failed to create internal ivns\n");
		D_GOTO(exit, rc = -DER_NOMEM);
	}

	*ivns = (crt_iv_namespace_t)ivns_internal;

exit:
	if (rc != 0) {
		D_FREE(ivns_internal);
		if (grp_priv)
			crt_grp_priv_decref(grp_priv);
	}

	return rc;
}

int
crt_iv_namespace_priv_set(crt_iv_namespace_t *ivns, void *priv)
{
	struct crt_ivns_internal	*ivns_internal;
	int				rc = 0;

	if (ivns == NULL) {
		D_ERROR("NULL ivns passed\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	ivns_internal = crt_ivns_internal_get(ivns);

	if (ivns_internal == NULL) {
		D_ERROR("Invalid ivns passed\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	ivns_internal->cii_user_priv = priv;
	IVNS_DECREF(ivns_internal);

exit:
	return rc;
}

int
crt_iv_namespace_priv_get(crt_iv_namespace_t *ivns, void **priv)
{
	struct crt_ivns_internal	*ivns_internal;
	int				rc = 0;

	if (ivns == NULL) {
		D_ERROR("NULL ivns passed\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	if (priv == NULL) {
		D_ERROR("NULL priv passed\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	ivns_internal = crt_ivns_internal_get(ivns);

	if (ivns_internal == NULL) {
		D_ERROR("Invalid ivns passed\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	*priv = ivns_internal->cii_user_priv;
	IVNS_DECREF(ivns_internal);
exit:
	return rc;
}

int
crt_iv_namespace_id_get(crt_iv_namespace_t *ivns, uint32_t *id)
{
	struct crt_ivns_internal	*ivns_internal;
	int			     rc = 0;

	if (ivns == NULL) {
		D_ERROR("NULL ivns passed\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	if (id == NULL) {
		D_ERROR("NULL id passed\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	ivns_internal = crt_ivns_internal_get(ivns);
	if (ivns_internal == NULL) {
		D_ERROR("Invalid ivns passed\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	*id = ivns_internal->cii_gns.gn_ivns_id.ii_nsid;

	IVNS_DECREF(ivns_internal);
exit:
	return rc;
}

int
crt_iv_namespace_destroy(crt_iv_namespace_t ivns,
			 crt_iv_namespace_destroy_cb_t destroy_cb,
			 void *cb_arg)
{
	struct crt_ivns_internal	*ivns_internal;

	ivns_internal = crt_ivns_internal_get(ivns);
	if (ivns_internal == NULL) {
		D_DEBUG(DB_ALL, "ivns does not exist\n");
		return 0;
	}

	ivns_internal->cii_destroy_cb = destroy_cb;
	ivns_internal->cii_destroy_cb_arg = cb_arg;

	/* addref done in crt_ivns_internal_get() and at attach/create time  */
	IVNS_DECREF_N(ivns_internal, 2);

	return 0;
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
	struct crt_iv_fetch_out		*output;
	struct crt_iv_ops		*iv_ops;
	crt_rpc_t			*rpc;
	int				rc = 0;

	D_ASSERT(info != NULL);

	/* Keep freeing things even if something fails */
	rc = crt_bulk_free(info->bci_bulk_desc->bd_local_hdl);
	if (rc != 0)
		D_ERROR("crt_bulk_free(): "DF_RC"\n", DP_RC(rc));

	cb_info = info->bci_arg;
	rpc = info->bci_bulk_desc->bd_rpc;

	output = crt_reply_get(rpc);
	output->ifo_rc = info->bci_rc;

	iv_ops = crt_iv_ops_get(cb_info->tci_ivns_internal,
				cb_info->tci_class_id);
	D_ASSERT(iv_ops != NULL);

	iv_ops->ivo_on_put(cb_info->tci_ivns_internal, &cb_info->tci_iv_value,
			   cb_info->tci_user_priv);

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("crt_reply_send(): "DF_RC"\n", DP_RC(rc));

	RPC_PUB_DECREF(rpc);

	/* ADDREF done in crt_ivf_bulk_transfer */
	IVNS_DECREF(cb_info->tci_ivns_internal);
	D_FREE(cb_info);

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
	struct crt_bulk_desc		 bulk_desc;
	crt_bulk_opid_t			 opid;
	crt_bulk_t			 bulk_hdl;
	struct crt_iv_fetch_out		*output;
	int				 size;
	int				 i;
	int				 rc2;
	int				 rc = 0;

	output = crt_reply_get(rpc);
	if (output == NULL) {
		D_ERROR("output was NULL\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	rc = crt_bulk_create(rpc->cr_ctx, iv_value, CRT_BULK_RW,
			     &bulk_hdl);
	if (rc != 0) {
		D_ERROR("crt_bulk_create(): "DF_RC"\n", DP_RC(rc));
		D_GOTO(exit, rc);
	}

	/* Calculate total size of all iovs in sg list */
	size = 0;
	for (i = 0; i < iv_value->sg_nr; i++)
		size += iv_value->sg_iovs[i].iov_buf_len;

	/* crt_req_decref done in crt_ivf_bulk_transfer_done_cb */
	RPC_PUB_ADDREF(rpc);

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
	IVNS_ADDREF(ivns_internal);

	cb_info->tci_class_id = class_id;
	cb_info->tci_iv_key = *iv_key;
	cb_info->tci_iv_value = *iv_value;
	cb_info->tci_user_priv = user_priv;

	rc = crt_bulk_transfer(&bulk_desc, crt_ivf_bulk_transfer_done_cb,
			       cb_info, &opid);
cleanup:
	if (rc != 0) {
		D_ERROR("Bulk transfer failed; "DF_RC"\n", DP_RC(rc));

		output->ifo_rc = rc;
		/* Reply can fail */
		crt_reply_send(rpc);

		RPC_PUB_DECREF(rpc);

		rc2 = crt_bulk_free(bulk_hdl);
		if (rc2 != 0)
			D_ERROR("crt_bulk_free(): "DF_RC"\n", DP_RC(rc2));

		if (cb_info) {
			IVNS_DECREF(cb_info->tci_ivns_internal);
			D_FREE(cb_info);
		}
	}
exit:
	return rc;
}

/* Fetch response handler (from previous request)*/
static void
handle_ivfetch_response(const struct crt_cb_info *cb_info)
{
	struct iv_fetch_cb_info		*iv_info = cb_info->cci_arg;
	crt_rpc_t			*rpc = cb_info->cci_rpc;
	struct crt_iv_fetch_in		*input = crt_req_get(rpc);
	struct crt_iv_fetch_out		*output = crt_reply_get(rpc);
	struct crt_iv_ops		*iv_ops;
	struct crt_ivns_internal	*ivns;
	struct ivf_key_in_progress	*kip_entry;
	uint32_t			class_id;
	int				rc;

	if (cb_info->cci_rc == 0x0)
		rc = output->ifo_rc;
	else
		rc = cb_info->cci_rc;

	ivns = iv_info->ifc_ivns_internal;
	class_id = iv_info->ifc_class_id;

	iv_ops = crt_iv_ops_get(ivns, class_id);
	D_ASSERT(iv_ops != NULL);

	IV_DBG(&input->ifi_key, "response received, rc = %d\n", rc);

	/* In case of a failure, call on_refresh with NULL iv_value */
	iv_ops->ivo_on_refresh(ivns, &input->ifi_key,
				0, /* TODO: iv_ver */
				rc == 0 ? &iv_info->ifc_iv_value : NULL,
				false, rc, iv_info->ifc_user_priv);

	if (iv_info->ifc_bulk_hdl)
		crt_bulk_free(iv_info->ifc_bulk_hdl);

	D_MUTEX_LOCK(&ivns->cii_lock);
	kip_entry = crt_ivf_key_in_progress_find(ivns, iv_ops, &input->ifi_key);
	D_MUTEX_UNLOCK(&ivns->cii_lock);

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
		crt_ivf_pending_reqs_process(ivns, class_id, kip_entry, rc);

	/* Finalize fetch operation */
	crt_ivf_finalize(iv_info, &input->ifi_key, rc);

	if (rc == 0)
		crt_ivf_pending_reqs_process(ivns, class_id, kip_entry, rc);

	/* ADDREF done by caller of crt_ivf_rpc_issue() */
	IVNS_DECREF(iv_info->ifc_ivns_internal);
	D_FREE(iv_info);
}

/* Helper function to issue internal iv_fetch RPC */
static int
crt_ivf_rpc_issue(d_rank_t dest_node, crt_iv_key_t *iv_key,
		  d_sg_list_t *iv_value, d_rank_t root_node,
		  uint32_t grp_ver,
		  struct iv_fetch_cb_info *cb_info)
{
	struct crt_ivns_internal	*ivns_internal;
	struct crt_iv_fetch_in		*input;
	crt_bulk_t			local_bulk = CRT_BULK_NULL;
	crt_endpoint_t			ep = {0};
	crt_rpc_t			*rpc;
	struct ivf_key_in_progress	*entry;
	int				rc = 0;
	struct crt_iv_ops		*iv_ops;
	uint32_t			local_grp_ver;

	ivns_internal = cb_info->ifc_ivns_internal;

	iv_ops = crt_iv_ops_get(ivns_internal, cb_info->ifc_class_id);
	D_ASSERT(iv_ops != NULL);

	IV_DBG(iv_key, "rpc to be issued to rank=%d\n", dest_node);

	/* Check if RPC for this key has already been submitted */
	D_MUTEX_LOCK(&ivns_internal->cii_lock);
	entry = crt_ivf_key_in_progress_find(ivns_internal, iv_ops, iv_key);

	/* If entry exists, rpc was sent at some point */
	if (entry) {
		/* If rpc is in progress, add request to the pending list */
		if (entry->kip_rpc_in_progress == true) {
			rc = crt_ivf_pending_request_add(ivns_internal, iv_ops,
							 entry, cb_info);

			IV_DBG(iv_key, "added to kip_entry=%p\n", entry);
			D_MUTEX_UNLOCK(&entry->kip_lock);
			D_MUTEX_UNLOCK(&ivns_internal->cii_lock);
			return rc;
		}
		IV_DBG(iv_key, "kip_entry=%p present\n", entry);
	} else {
		/* New request, rpc does not exit previously */
		entry = crt_ivf_key_in_progress_set(ivns_internal, iv_key);
		if (!entry) {
			D_ERROR("crt_ivf_key_in_progres_set() failed\n");
			D_MUTEX_UNLOCK(&ivns_internal->cii_lock);
			return -DER_NOMEM;
		}
		IV_DBG(iv_key, "new kip_entry=%p added\n", entry);
	}

	/* RPC is in progress */
	entry->kip_rpc_in_progress = true;
	entry->kip_refcnt++;

	IV_DBG(iv_key, "kip_entry=%p refcnt=%d\n", entry, entry->kip_refcnt);

	D_MUTEX_UNLOCK(&entry->kip_lock);
	D_MUTEX_UNLOCK(&ivns_internal->cii_lock);

	rc = crt_bulk_create(ivns_internal->cii_ctx, iv_value, CRT_BULK_RW,
			     &local_bulk);
	if (rc != 0) {
		D_ERROR("crt_bulk_create(): "DF_RC"\n", DP_RC(rc));
		D_GOTO(exit, rc);
	}

	/* Note: destination node is using global rank already */
	ep.ep_grp = NULL;
	ep.ep_rank = dest_node;

	rc = crt_req_create(ivns_internal->cii_ctx, &ep, CRT_OPC_IV_FETCH,
			    &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create(): "DF_RC"\n", DP_RC(rc));
		D_GOTO(exit, rc);
	}

	input = crt_req_get(rpc);
	D_ASSERT(input != NULL);

	input->ifi_value_bulk = local_bulk;

	cb_info->ifc_bulk_hdl = local_bulk;

	d_iov_set(&input->ifi_key, iv_key->iov_buf, iv_key->iov_buf_len);
	input->ifi_class_id = cb_info->ifc_class_id;
	input->ifi_root_node = root_node;

	input->ifi_ivns_id = ivns_internal->cii_gns.gn_ivns_id.ii_nsid;
	input->ifi_ivns_group = ivns_internal->cii_gns.gn_ivns_id.ii_group_name;

	/*
	 * If version passed in does not match current ivns version,
	 * then the version has changed during the rpc build process.
	 * MUST not set it to (could cause a race):
	 *    input->ifi_grp_ver = ivns_internal->cii_grp_priv->gp_membs_ver
	 */
	D_RWLOCK_RDLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	local_grp_ver = ivns_internal->cii_grp_priv->gp_membs_ver;
	D_RWLOCK_UNLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	if (local_grp_ver == grp_ver) {
		input->ifi_grp_ver = grp_ver;
	} else {
		D_DEBUG(DB_ALL, "Group Version Changed: From %d: To %d\n",
			grp_ver, local_grp_ver);
		D_GOTO(exit, rc = -DER_GRPVER);
	}

	rc = crt_req_send(rpc, handle_response_cb, cb_info);

	IV_DBG(iv_key, "crt_req_send() to %d rc=%d\n", dest_node, rc);
exit:
	if (rc != 0) {
		D_ERROR("Failed to send rpc to remote node = %d\n", dest_node);

		D_MUTEX_LOCK(&ivns_internal->cii_lock);

		/* Only unset if there are no pending fetches for this key */
		entry = crt_ivf_key_in_progress_find(ivns_internal,
						     iv_ops, iv_key);

		if (entry) {
			if (d_list_empty(&entry->kip_pending_fetch_list)) {
				/* returns false if entry is not destroyed */
				if (!crt_ivf_key_in_progress_unset(
						ivns_internal, entry))
					D_MUTEX_UNLOCK(&entry->kip_lock);
			} else {
				D_MUTEX_UNLOCK(&entry->kip_lock);
			}
		}

		D_MUTEX_UNLOCK(&ivns_internal->cii_lock);
		if (local_bulk != CRT_BULK_NULL)
			crt_bulk_free(local_bulk);
	}
	return rc;
}

/* Returns the parent of 'cur_node' into 'ret_node' on success */
static int
crt_iv_ranks_parent_get(struct crt_ivns_internal *ivns_internal,
			d_rank_t cur_node, d_rank_t root_node,
			d_rank_t *ret_node)
{
	d_rank_t		 parent_rank;
	int			 rc;

	D_ASSERT(ret_node != NULL);

	if (cur_node == root_node) {
		*ret_node = root_node;
		return 0;
	}

	D_ASSERT(ivns_internal->cii_grp_priv != NULL);

	rc = crt_tree_get_parent(ivns_internal->cii_grp_priv, 0, NULL,
				 ivns_internal->cii_gns.gn_tree_topo,
				 root_node, cur_node, &parent_rank);
	if (rc == 0)
		*ret_node = parent_rank;

	D_DEBUG(DB_TRACE, "parent lookup: current=%d, root=%d, parent=%d "
			  "rc=%d\n",
			  cur_node, root_node, parent_rank, rc);
	return rc;
}

/* Return next parent (in ret_node) for the current rank and root_node */
static int
crt_iv_parent_get(struct crt_ivns_internal *ivns_internal,
		  d_rank_t root_node, d_rank_t *ret_node)
{
	d_rank_t self = ivns_internal->cii_grp_priv->gp_self;

	if (self == CRT_NO_RANK) {
		D_DEBUG(DB_ALL, "%s: self rank not known yet\n",
			ivns_internal->cii_grp_priv->gp_pub.cg_grpid);
		return -DER_GRPVER;
	}

	return crt_iv_ranks_parent_get(ivns_internal, self, root_node,
				       ret_node);
}

/* Internal handler for CRT_OPC_IV_FETCH RPC call*/
static void
crt_hdlr_iv_fetch_aux(void *arg)
{
	struct crt_iv_fetch_in		*input;
	struct crt_iv_fetch_out		*output;
	struct crt_ivns_id		ivns_id;
	struct crt_ivns_internal	*ivns_internal = NULL;
	struct crt_iv_ops		*iv_ops = NULL;
	d_sg_list_t			 iv_value = {0};
	bool				 put_needed = false;
	void				*user_priv = NULL;
	crt_rpc_t			*rpc_req;
	uint32_t			 grp_ver_entry;
	uint32_t			 grp_ver_current;
	int				 rc = 0;

	rpc_req = arg;
	input = crt_req_get(rpc_req);
	output = crt_reply_get(rpc_req);

	ivns_id.ii_group_name = input->ifi_ivns_group;
	ivns_id.ii_nsid = input->ifi_ivns_id;

	/* ADDREF */
	ivns_internal = crt_ivns_internal_lookup(&ivns_id);
	if (ivns_internal == NULL) {
		D_ERROR("Failed to lookup ivns internal!\n");
		D_GOTO(send_error, rc = -DER_NONEXIST);
	}

	/* This function is called with ivns_internal ref count held. Since
	 * we grabbed our own ref count in lookup, decrement ref count.
	 * Reconsider creating wrapper function with passed ivns
	 */
	IVNS_DECREF(ivns_internal);

	/*
	 * Check if current group version matches that of the ifi structure.
	 * Test whether the current node changed its version number from
	 * the time it initially received a request to the time it
	 * is to send the response.
	 */
	D_RWLOCK_RDLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	grp_ver_entry = ivns_internal->cii_grp_priv->gp_membs_ver;
	D_RWLOCK_UNLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	if (grp_ver_entry != input->ifi_grp_ver) {
		D_DEBUG(DB_ALL,
			"Group (%s) version mismatch. Local: %d Remote :%d\n",
			ivns_id.ii_group_name, grp_ver_entry,
			input->ifi_grp_ver);
		D_GOTO(send_error, rc = -DER_GRPVER);
	}

	iv_ops = crt_iv_ops_get(ivns_internal, input->ifi_class_id);
	if (iv_ops == NULL) {
		D_ERROR("Returned iv_ops were NULL\n");
		D_GOTO(send_error, rc = -DER_INVAL);
	}

	IV_DBG(&input->ifi_key, "fetch handler entered\n");
	rc = iv_ops->ivo_on_get(ivns_internal, &input->ifi_key,
				0, CRT_IV_PERM_READ, &iv_value, &user_priv);
	if (rc != 0) {
		D_ERROR("ivo_on_get(): "DF_RC"\n", DP_RC(rc));
		D_GOTO(send_error, rc);
	}

	put_needed = true;

	rc = iv_ops->ivo_on_fetch(ivns_internal, &input->ifi_key, 0,
				  0x0, &iv_value, user_priv);
	if (rc == 0) {
		/* Note: This increments ref count on 'rpc_req' and ivns */
		rc = crt_ivf_bulk_transfer(ivns_internal,
					   input->ifi_class_id,
					   &input->ifi_key,
					   &iv_value, input->ifi_value_bulk,
					   rpc_req, user_priv);
		if (rc != 0) {
			D_ERROR("bulk transfer failed; "DF_RC"\n", DP_RC(rc));
			D_GOTO(send_error, rc);
		}
	} else if (rc == -DER_IVCB_FORWARD) {
		/* Forward the request to the parent */
		d_rank_t next_node;
		struct iv_fetch_cb_info *cb_info;

		if (ivns_internal->cii_grp_priv->gp_self ==
							input->ifi_root_node) {
			D_ERROR("Forward requested for root node\n");
			D_GOTO(send_error, rc = -DER_INVAL);
		}

		iv_ops->ivo_on_put(ivns_internal, &iv_value, user_priv);
		put_needed = false;

		/* Reset the iv_value, since it maybe freed in on_put() */
		memset(&iv_value, 0, sizeof(iv_value));
		rc = iv_ops->ivo_on_get(ivns_internal, &input->ifi_key,
					0, CRT_IV_PERM_WRITE, &iv_value,
					&user_priv);
		if (rc != 0) {
			D_ERROR("ivo_on_get(): "DF_RC"\n", DP_RC(rc));
			D_GOTO(send_error, rc);
		}

		put_needed = true;

		/* get group version and next node to transfer to */
		D_RWLOCK_RDLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
		grp_ver_current = ivns_internal->cii_grp_priv->gp_membs_ver;
		rc = crt_iv_parent_get(ivns_internal, input->ifi_root_node,
				       &next_node);
		D_RWLOCK_UNLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
		if (rc != 0) {
			D_DEBUG(DB_TRACE, "crt_iv_parent_get() returned %d\n",
				rc);
			D_GOTO(send_error, rc = -DER_OOG);
		}

		/* Check here for change in group */
		if (grp_ver_entry != grp_ver_current) {
			D_DEBUG(DB_ALL, "Group (%s) version changed. "
				"On Entry: %d:: Changed To :%d\n",
				ivns_id.ii_group_name,
				grp_ver_entry, grp_ver_current);
			D_GOTO(send_error, rc = -DER_GRPVER);
		}

		D_ALLOC_PTR(cb_info);
		if (cb_info == NULL)
			D_GOTO(send_error, rc = -DER_NOMEM);

		cb_info->ifc_child_rpc = rpc_req;
		cb_info->ifc_child_bulk = input->ifi_value_bulk;

		/* crt_req_decref done in crt_ivf_finalize */
		RPC_PUB_ADDREF(rpc_req);

		cb_info->ifc_iv_value = iv_value;
		cb_info->ifc_iv_key = input->ifi_key;

		cb_info->ifc_ivns_internal = ivns_internal;
		IVNS_ADDREF(ivns_internal);

		cb_info->ifc_class_id = input->ifi_class_id;
		cb_info->ifc_user_priv = user_priv;

		rc = crt_ivf_rpc_issue(next_node,
				       &input->ifi_key, &cb_info->ifc_iv_value,
				       input->ifi_root_node, grp_ver_entry,
				       cb_info);
		if (rc != 0) {
			D_ERROR("Failed to issue fetch rpc; "DF_RC"\n",
				DP_RC(rc));
			RPC_PUB_DECREF(rpc_req);

			IVNS_DECREF(cb_info->ifc_ivns_internal);
			D_FREE(cb_info);
			D_GOTO(send_error, rc);
		}
	} else {
		D_ERROR("ERROR happened: "DF_RC"\n", DP_RC(rc));
		D_GOTO(send_error, rc);
	}

	/* addref in crt_hdlr_iv_fetch */
	RPC_PUB_DECREF(rpc_req);
	IV_DBG(&input->ifi_key, "fetch handler exiting\n");

	/* ADDREF done in lookup above */
	IVNS_DECREF(ivns_internal);
	return;

send_error:
	if (put_needed && iv_ops)
		iv_ops->ivo_on_put(ivns_internal, &iv_value, user_priv);
	output->ifo_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != DER_SUCCESS) {
		D_ERROR("crt_reply_send(opc: %#x): "DF_RC"\n",
			rpc_req->cr_opc, DP_RC(rc));
	}

	/* ADDREF done in lookup above */
	if (ivns_internal)
		IVNS_DECREF(ivns_internal);

	/* addref in crt_hdlr_iv_fetch */
	RPC_PUB_DECREF(rpc_req);
}

/* Internal handler for CRT_OPC_IV_FETCH RPC call*/
void
crt_hdlr_iv_fetch(crt_rpc_t *rpc_req)
{
	struct crt_iv_fetch_in		*input;
	struct crt_iv_fetch_out		*output;
	struct crt_ivns_id		ivns_id;
	struct crt_ivns_internal	*ivns_internal = NULL;
	struct crt_iv_ops		*iv_ops;
	uint32_t			 grp_ver;
	int				 rc;

	input = crt_req_get(rpc_req);
	output = crt_reply_get(rpc_req);

	ivns_id.ii_group_name = input->ifi_ivns_group;
	ivns_id.ii_nsid = input->ifi_ivns_id;

	/* ADDREF */
	ivns_internal = crt_ivns_internal_lookup(&ivns_id);
	if (ivns_internal == NULL) {
		D_ERROR("Failed to look up ivns_id! ivns_id=%s:%d\n",
			ivns_id.ii_group_name, ivns_id.ii_nsid);
		D_GOTO(send_error, rc = -DER_NONEXIST);
	}

	/* Check local group version matching with in coming request */
	D_RWLOCK_RDLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	grp_ver = ivns_internal->cii_grp_priv->gp_membs_ver;
	D_RWLOCK_UNLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);

	if (grp_ver != input->ifi_grp_ver) {
		D_DEBUG(DB_ALL,
			"Group (%s) version mismatch. Local: %d Remote :%d\n",
			ivns_id.ii_group_name, grp_ver,
			input->ifi_grp_ver);
		D_GOTO(send_error, rc = -DER_GRPVER);
	}

	iv_ops = crt_iv_ops_get(ivns_internal, input->ifi_class_id);
	if (iv_ops == NULL) {
		D_ERROR("Returned iv_ops were NULL, class_id: %d\n",
			input->ifi_class_id);
		D_GOTO(send_error, rc = -DER_INVAL);
	}

	/* prevent rpc_req from being destroyed, dec ref in
	 * crt_hdlr_iv_fetch_aux
	 */
	RPC_PUB_ADDREF(rpc_req);

	/* rpc_req::input->ifi_nsid.iov_buf refers to this ivns. Prevent
	 * this ivns from being destroyed until crt_hdlr_iv_fetch_aux()
	 * can grab its own reference.
	 * TODO: Consider wrapping rpc_req and ivns in a struct
	 * in ivo_pre_fetch() case, and change handler function.
	 */
	IVNS_ADDREF(ivns_internal);

	if (iv_ops->ivo_pre_fetch != NULL) {
		D_DEBUG(DB_TRACE, "Executing ivo_pre_fetch\n");
		iv_ops->ivo_pre_fetch(ivns_internal,
				      &input->ifi_key,
				      crt_hdlr_iv_fetch_aux,
				      rpc_req);
	} else {
		crt_hdlr_iv_fetch_aux(rpc_req);
	}

	/* ADDREF done above in lookup */
	IVNS_DECREF(ivns_internal);
	return;

send_error:
	output->ifo_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != DER_SUCCESS)
		D_ERROR("crt_reply_send(opc: %#x): "DF_RC"\n",
			rpc_req->cr_opc, DP_RC(rc));

	/* ADDREF done above in lookup */
	if (ivns_internal)
		IVNS_DECREF(ivns_internal);
}

static int
get_shortcut_path(struct crt_ivns_internal *ivns, d_rank_t root_rank,
		  crt_iv_shortcut_t shortcut, d_rank_t *next_node)
{
	int rc = 0;

	D_ASSERT(ivns != NULL);
	D_ASSERT(next_node != NULL);

	switch (shortcut) {
	case CRT_IV_SHORTCUT_TO_ROOT:
		*next_node = root_rank;
		break;

	case CRT_IV_SHORTCUT_NONE:
		rc = crt_iv_parent_get(ivns, root_rank, next_node);
		if (rc != 0) {
			D_DEBUG(DB_TRACE, "crt_iv_parent_get() returned %d\n",
				rc);
			D_GOTO(exit, rc = -DER_OOG);
		}
		break;

	default:
		D_ERROR("Unknown shortcut=%d specified\n", shortcut);
		D_GOTO(exit, rc = -DER_INVAL);
	}
exit:
	return rc;
}

int
crt_iv_fetch(crt_iv_namespace_t ivns, uint32_t class_id,
	     crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
	     crt_iv_shortcut_t shortcut,
	     crt_iv_comp_cb_t fetch_comp_cb, void *cb_arg)
{
	struct crt_ivns_internal	*ivns_internal = NULL;
	struct crt_iv_ops		*iv_ops;
	struct iv_fetch_cb_info		*cb_info = NULL;
	d_rank_t			 root_rank;
	d_rank_t			 next_node = 1;
	int				 rc;
	d_sg_list_t			*iv_value = NULL;
	void				*user_priv = NULL;
	bool				 put_needed = false;
	uint32_t			 grp_ver_entry;

	if (iv_key == NULL) {
		D_ERROR("iv_key is NULL\n");
		return -DER_INVAL;
	}

	IV_DBG(iv_key, "fetch issued\n");

	/* ADDREF */
	ivns_internal = crt_ivns_internal_get(ivns);
	if (ivns_internal == NULL) {
		D_ERROR("Invalid ivns\n");
		return -DER_NONEXIST;
	}

	/* Get group name space internal operations */
	iv_ops = crt_iv_ops_get(ivns_internal, class_id);
	if (iv_ops == NULL) {
		D_ERROR("Failed to get iv_ops for class_id = %d\n", class_id);
		/* ADDREF done above in lookup */
		IVNS_DECREF(ivns_internal);
		return -DER_INVAL;
	}

	/* Get local version and associated root rank for latter comparison. */
	D_RWLOCK_RDLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	grp_ver_entry = ivns_internal->cii_grp_priv->gp_membs_ver;
	rc = iv_ops->ivo_on_hash(ivns_internal, iv_key, &root_rank);
	D_RWLOCK_UNLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	if (rc != 0) {
		D_CDEBUG(rc == -DER_NOTLEADER, DB_ANY, DLOG_ERR,
			 "Failed to get hash, rc="DF_RC"\n",
			 DP_RC(rc));
		D_GOTO(exit, rc);
	}

	/* Allocate memory pointer for scatter/gather list */
	D_ALLOC_PTR(iv_value);
	if (iv_value == NULL)
		D_GOTO(exit, rc = -DER_NOMEM);

	rc = iv_ops->ivo_on_get(ivns_internal, iv_key, 0, CRT_IV_PERM_READ,
				iv_value, &user_priv);
	if (rc != 0) {
		D_ERROR("ivo_on_get(): "DF_RC"\n", DP_RC(rc));
		D_GOTO(exit, rc);
	}
	put_needed = true;

	rc = iv_ops->ivo_on_fetch(ivns_internal, iv_key, 0,
				  0, iv_value, user_priv);

	/* The fetch info is contained on current server.  */
	if (rc == 0) {
		/* Finish up the completion call back */
		iv_ops->ivo_on_refresh(ivns_internal, iv_key, 0,
				       iv_value, false, 0x0, user_priv);

		fetch_comp_cb(ivns_internal, class_id, iv_key, NULL,
			      iv_value, rc, cb_arg);

		iv_ops->ivo_on_put(ivns_internal, iv_value, user_priv);
		D_FREE(iv_value);

		/* ADDREF done above in lookup */
		IVNS_DECREF(ivns_internal);
		return rc;
	} else if (rc != -DER_IVCB_FORWARD) {
		/* We got error, call the callback and exit */
		iv_ops->ivo_on_refresh(ivns_internal, iv_key, 0,
				       NULL, false, rc, user_priv);

		fetch_comp_cb(ivns_internal, class_id, iv_key, NULL,
			      NULL, rc, cb_arg);

		iv_ops->ivo_on_put(ivns_internal, iv_value, user_priv);
		D_FREE(iv_value);

		/* ADDREF done above in lookup */
		IVNS_DECREF(ivns_internal);
		return rc;
	}
	/*
	 * The request is not located on current server.
	 * Create an rpc request to external server.
	 * Return read-only copy and request 'write' version of iv_value
	 * Free up previous iv_value structure.
	 */
	iv_ops->ivo_on_put(ivns_internal, iv_value, user_priv);
	put_needed = false;

	/* Setup user private pointer.  Alloc and fill in iv_value structure */
	rc = iv_ops->ivo_on_get(ivns_internal, iv_key, 0, CRT_IV_PERM_WRITE,
				iv_value, &user_priv);
	if (rc != 0) {
		D_ERROR("ivo_on_get(): "DF_RC"\n", DP_RC(rc));
		D_GOTO(exit, rc);
	}
	put_needed = true;

	/*
	 * If we reached here, means we got DER_IVCB_FORWARD
	 * Do not need a version check after call.
	 * We will create a new rpc for synchronization
	 */
	rc = get_shortcut_path(ivns_internal, root_rank, shortcut, &next_node);
	if (rc != 0)
		D_GOTO(exit, rc);

	IV_DBG(iv_key, "root=%d next_parent=%d\n", root_rank, next_node);

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
	IVNS_ADDREF(cb_info->ifc_ivns_internal);
	cb_info->ifc_class_id = class_id;

	/* Issue a forwarding rpc to next node in list */
	rc = crt_ivf_rpc_issue(next_node, iv_key, iv_value, root_rank,
			       grp_ver_entry, cb_info);
exit:
	if (rc != 0) {
		fetch_comp_cb(ivns, class_id, iv_key, NULL, NULL, rc, cb_arg);

		if (put_needed)
			iv_ops->ivo_on_put(ivns, iv_value, user_priv);

		D_CDEBUG(rc == -DER_NOTLEADER, DB_ANY, DLOG_ERR,
			 "Failed to issue IV fetch, rc="DF_RC"\n",
			 DP_RC(rc));

		if (cb_info) {
			IVNS_DECREF(cb_info->ifc_ivns_internal);
			D_FREE(cb_info);
		}
	}

	D_FREE(iv_value);

	/* ADDREF done in lookup above */
	if (ivns_internal)
		IVNS_DECREF(ivns_internal);
	return rc;
}

/***************************************************************
 * IV UPDATE codebase
 **************************************************************/

static void
crt_hdlr_iv_sync_aux(void *arg)
{
	int				rc = 0;
	struct crt_iv_sync_in		*input;
	struct crt_iv_sync_out		*output;
	struct crt_ivns_internal	*ivns_internal;
	struct crt_iv_ops		*iv_ops = NULL;
	struct crt_ivns_id		ivns_id;
	crt_iv_sync_t			*sync_type;
	d_sg_list_t			iv_value = {0};
	bool				 need_put = false;
	void				*user_priv = NULL;
	crt_rpc_t			*rpc_req;
	uint32_t			 grp_ver;

	rpc_req = arg;
	/* This is an internal call. All errors are fatal */
	input = crt_req_get(rpc_req);
	D_ASSERT(input != NULL);

	output = crt_reply_get(rpc_req);
	D_ASSERT(output != NULL);

	ivns_id.ii_group_name = input->ivs_ivns_group;
	ivns_id.ii_nsid = input->ivs_ivns_id;
	sync_type = (crt_iv_sync_t *)input->ivs_sync_type.iov_buf;

	/* ADDREF */
	ivns_internal = crt_ivns_internal_lookup(&ivns_id);

	/*
	 * In some use-cases, sync can arrive to a node that hasn't attached
	 * iv namespace yet. Treat such errors as fatal if the flag is set.
	 */
	if (ivns_internal == NULL) {
		D_ERROR("ivns_internal was NULL. ivns_id=%s:%d\n",
			ivns_id.ii_group_name, ivns_id.ii_nsid);

		if (sync_type->ivs_flags & CRT_IV_SYNC_FLAG_NS_ERRORS_FATAL)
			D_ASSERT(ivns_internal != NULL);
		else
			D_GOTO(exit, rc = -DER_NONEXIST);
	}

	/* Check group version match */
	D_RWLOCK_RDLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	grp_ver = ivns_internal->cii_grp_priv->gp_membs_ver;
	D_RWLOCK_UNLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	if (grp_ver != input->ivs_grp_ver) {
		D_DEBUG(DB_ALL,
			"Group (%s) version mismatch. Local: %d Remote :%d\n",
			ivns_id.ii_group_name, grp_ver,
			input->ivs_grp_ver);
		D_GOTO(exit, rc = -DER_GRPVER);
	}

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
		d_sg_list_t	tmp_iv;
		d_iov_t		*tmp_iovs;

		rc = iv_ops->ivo_on_get(ivns_internal, &input->ivs_key,
					0, CRT_IV_PERM_READ, &iv_value,
					&user_priv);
		if (rc != 0) {
			D_ERROR("ivo_on_get(): "DF_RC"\n", DP_RC(rc));
			D_GOTO(exit, rc);
		}

		need_put = true;

		D_ALLOC_ARRAY(tmp_iovs, iv_value.sg_nr);
		if (tmp_iovs == NULL) {
			D_GOTO(exit, rc = -DER_NOMEM);
		}

		tmp_iv.sg_nr = iv_value.sg_nr;
		tmp_iv.sg_iovs = tmp_iovs;

		/* Populate tmp_iv.sg_iovs[0] to [sg_nr] */
		rc = crt_bulk_access(rpc_req->cr_co_bulk_hdl, &tmp_iv);
		if (rc != 0) {
			D_FREE(tmp_iovs);
			D_ERROR("crt_bulk_access(): "DF_RC"\n", DP_RC(rc));
			D_GOTO(exit, rc);
		}

		rc = iv_ops->ivo_on_refresh(ivns_internal, &input->ivs_key,
					    0, &tmp_iv, false, 0, user_priv);
		D_FREE(tmp_iovs);
		if (rc != 0) {
			D_ERROR("ivo_on_refresh(): "DF_RC"\n", DP_RC(rc));
			D_GOTO(exit, rc);
		}

		iv_ops->ivo_on_put(ivns_internal, &iv_value, user_priv);
		need_put = false;

		break;
	}

	case CRT_IV_SYNC_EVENT_NOTIFY:
		rc = iv_ops->ivo_on_refresh(ivns_internal, &input->ivs_key,
					    0, 0, false, 0, user_priv);
		if (rc != 0) {
			D_ERROR("ivo_on_refresh(): "DF_RC"\n", DP_RC(rc));
			D_GOTO(exit, rc);
		}

		break;

	default:
		D_ERROR("Unknown event type %#x\n", sync_type->ivs_event);
		D_GOTO(exit, rc = -DER_INVAL);
		break;
	}

exit:
	if (need_put && iv_ops)
		iv_ops->ivo_on_put(ivns_internal, &iv_value, user_priv);

	output->rc = rc;
	crt_reply_send(rpc_req);

	/* ADDREF done in lookup above */
	IVNS_DECREF(ivns_internal);

	/* add ref in crt_hdlr_iv_sync */
	RPC_PUB_DECREF(rpc_req);
}

/* Handler for internal SYNC CORPC */
void
crt_hdlr_iv_sync(crt_rpc_t *rpc_req)
{
	struct crt_iv_sync_in		*input;
	struct crt_iv_sync_out		*output;
	struct crt_ivns_internal	*ivns_internal = NULL;
	struct crt_iv_ops		*iv_ops = NULL;
	struct crt_ivns_id		ivns_id;
	crt_iv_sync_t			*sync_type;
	uint32_t			 grp_ver;
	int				 rc = 0;

	/* This is an internal call. All errors are fatal */
	input = crt_req_get(rpc_req);
	D_ASSERT(input != NULL);

	output = crt_reply_get(rpc_req);
	D_ASSERT(output != NULL);

	ivns_id.ii_group_name = input->ivs_ivns_group;
	ivns_id.ii_nsid = input->ivs_ivns_id;
	sync_type = (crt_iv_sync_t *)input->ivs_sync_type.iov_buf;

	/* ADDREF */
	ivns_internal = crt_ivns_internal_lookup(&ivns_id);

	/* In some use-cases sync can arrive to a node that hasn't attached
	* iv namespace yet. Treat such errors as fatal if the flag is set.
	**/
	if (ivns_internal == NULL) {
		D_ERROR("ivns_internal was NULL. ivns_id=%s:%d\n",
			ivns_id.ii_group_name, ivns_id.ii_nsid);

		D_ASSERT(!(sync_type->ivs_flags &
			   CRT_IV_SYNC_FLAG_NS_ERRORS_FATAL));
		D_GOTO(exit, rc = -DER_NONEXIST);
	}

	/* Check group version match */
	D_RWLOCK_RDLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	grp_ver = ivns_internal->cii_grp_priv->gp_membs_ver;
	D_RWLOCK_UNLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	if (grp_ver != input->ivs_grp_ver) {
		D_DEBUG(DB_ALL,
			"Group (%s) version mismatch. Local: %d Remote :%d\n",
			ivns_id.ii_group_name, grp_ver,
			input->ivs_grp_ver);
		D_GOTO(exit, rc = -DER_GRPVER);
	}

	iv_ops = crt_iv_ops_get(ivns_internal, input->ivs_class_id);
	D_ASSERT(iv_ops != NULL);

	/* prevent rpc_req from being destroyed, decref in
	 * crt_hdlr_iv_sync_aux()
	 */
	RPC_PUB_ADDREF(rpc_req);
	if (iv_ops->ivo_pre_refresh != NULL) {
		D_DEBUG(DB_TRACE, "Executing ivo_pre_refresh\n");
		iv_ops->ivo_pre_refresh(ivns_internal, &input->ivs_key,
					crt_hdlr_iv_sync_aux, rpc_req);
	} else {
		crt_hdlr_iv_sync_aux(rpc_req);
	}

	/* ADDREF done in lookup above */
	IVNS_DECREF(ivns_internal);
	return;
exit:
	output->rc = rc;
	crt_reply_send(rpc_req);

	if (ivns_internal) {
		/* ADDREF done in lookup above */
		IVNS_DECREF(ivns_internal);
	}
}

/* Results aggregate function for sync CORPC */
int
crt_iv_sync_corpc_aggregate(crt_rpc_t *source, crt_rpc_t *result, void *arg)
{
	struct crt_iv_sync_out *output_source;
	struct crt_iv_sync_out *output_result;

	output_source = crt_reply_get(source);
	output_result = crt_reply_get(result);

	/* Only set new rc if so far rc is 0 */
	if (output_result->rc == 0) {
		if (output_source->rc != 0)
			output_result->rc = output_source->rc;
	}

	return 0;
}

static int
call_pre_sync_cb(struct crt_ivns_internal *ivns_internal,
		 struct crt_iv_sync_in *input, crt_rpc_t *rpc_req)
{
	struct crt_iv_ops	*iv_ops;
	d_sg_list_t		 iv_value;
	d_sg_list_t		 tmp_iv;
	d_iov_t			*tmp_iovs = NULL;
	void			*user_priv;
	bool			 need_put = false;
	int			 rc;

	iv_ops = crt_iv_ops_get(ivns_internal, input->ivs_class_id);
	D_ASSERT(iv_ops != NULL);

	rc = iv_ops->ivo_on_get(ivns_internal, &input->ivs_key, 0,
				CRT_IV_PERM_READ, &iv_value,
				&user_priv);
	if (rc != 0) {
		D_ERROR("ivo_on_get(): "DF_RC"\n", DP_RC(rc));
		D_GOTO(exit, rc);
	}
	need_put = true;

	if (rpc_req->cr_co_bulk_hdl != CRT_BULK_NULL) {
		D_ALLOC_ARRAY(tmp_iovs, iv_value.sg_nr);
		if (tmp_iovs == NULL)
			D_GOTO(exit, rc = -DER_NOMEM);

		tmp_iv.sg_nr = iv_value.sg_nr;
		tmp_iv.sg_iovs = tmp_iovs;

		/* Populate tmp_iv.sg_iovs[0] to [sg_nr] */
		rc = crt_bulk_access(rpc_req->cr_co_bulk_hdl, &tmp_iv);
		if (rc != 0) {
			D_ERROR("crt_bulk_access(): "DF_RC"\n", DP_RC(rc));
			D_GOTO(exit, rc);
		}
	}

	D_DEBUG(DB_TRACE, "Executing ivo_pre_sync\n");
	rc = iv_ops->ivo_pre_sync(ivns_internal, &input->ivs_key, 0,
				  &tmp_iv, user_priv);
	if (rc != 0)
		D_ERROR("ivo_pre_sync(): "DF_RC"\n", DP_RC(rc));

exit:
	D_FREE(tmp_iovs);
	if (need_put)
		iv_ops->ivo_on_put(ivns_internal, &iv_value, user_priv);
	return rc;
}

int
crt_iv_sync_corpc_pre_forward(crt_rpc_t *rpc, void *arg)
{
	struct crt_iv_sync_in		*input;
	struct crt_ivns_internal	*ivns_internal;
	struct crt_iv_ops		*iv_ops;
	struct crt_ivns_id		 ivns_id;
	crt_iv_sync_t			*sync_type;
	int				 rc = 0;

	/* This is an internal call. All errors are fatal */
	input = crt_req_get(rpc);
	D_ASSERT(input != NULL);

	ivns_id.ii_group_name = input->ivs_ivns_group;
	ivns_id.ii_nsid = input->ivs_ivns_id;
	sync_type = (crt_iv_sync_t *)input->ivs_sync_type.iov_buf;

	ivns_internal = crt_ivns_internal_lookup(&ivns_id);

	/* In some use-cases sync can arrive to a node that hasn't attached
	* iv namespace yet. Treat such errors as fatal if the flag is set.
	**/
	if (ivns_internal == NULL) {
		D_ERROR("ivns_internal was NULL. ivns_id=%s:%d\n",
			ivns_id.ii_group_name, ivns_id.ii_nsid);

		D_ASSERT(!(sync_type->ivs_flags &
			   CRT_IV_SYNC_FLAG_NS_ERRORS_FATAL));
		return -DER_NONEXIST;
	}

	iv_ops = crt_iv_ops_get(ivns_internal, input->ivs_class_id);
	D_ASSERT(iv_ops != NULL);

	if (iv_ops->ivo_pre_sync != NULL)
		rc = call_pre_sync_cb(ivns_internal, input, rpc);

	IVNS_DECREF(ivns_internal);
	return rc;
}

/* Callback structure for iv sync RPC */
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

	/* user private data */
	void				*isc_user_priv;

	/* sync type */
	crt_iv_sync_t			isc_sync_type;
};

/* IV_SYNC response handler */
static void
handle_ivsync_response(const struct crt_cb_info *cb_info)
{
	struct iv_sync_cb_info	*iv_sync = cb_info->cci_arg;
	struct crt_iv_ops	*iv_ops;

	if (iv_sync->isc_bulk_hdl != CRT_BULK_NULL)
		crt_bulk_free(iv_sync->isc_bulk_hdl);

	/* do_callback is set based on sync value specified */
	if (iv_sync->isc_do_callback) {
		if (cb_info->cci_rc != 0)
			iv_sync->isc_update_rc = cb_info->cci_rc;

		iv_sync->isc_update_comp_cb(iv_sync->isc_ivns_internal,
					    iv_sync->isc_class_id,
					    &iv_sync->isc_iv_key,
					    NULL,
					    &iv_sync->isc_iv_value,
					    iv_sync->isc_update_rc,
					    iv_sync->isc_cb_arg);

		D_FREE(iv_sync->isc_iv_key.iov_buf);
	} else {
		D_DEBUG(DB_TRACE, "Call Back not supplied\n");
		D_ASSERT(iv_sync->isc_ivns_internal == NULL);
	}

	if (iv_sync->isc_ivns_internal) {
		iv_ops = crt_iv_ops_get(iv_sync->isc_ivns_internal,
					iv_sync->isc_class_id);
		D_ASSERT(iv_ops != NULL);

		iv_ops->ivo_on_put(iv_sync->isc_ivns_internal, NULL,
				   iv_sync->isc_user_priv);
		IVNS_DECREF(iv_sync->isc_ivns_internal);
	}
	D_FREE(iv_sync);
}

/* Helper function to issue update sync
 * Important note: iv_key and iv_value are destroyed right after this call,
 * as such they need to be copied over
 *
 * TODO: This is leaking memory on failure.
 */
static int
crt_ivsync_rpc_issue(struct crt_ivns_internal *ivns_internal, uint32_t class_id,
		     crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
		     d_sg_list_t *iv_value, crt_iv_sync_t *sync_type,
		     d_rank_t src_node, d_rank_t dst_node,
		     crt_iv_comp_cb_t update_comp_cb, void *cb_arg,
		     void *user_priv, int update_rc)
{
	crt_rpc_t		*corpc_req;
	struct crt_iv_sync_in	*input;
	int			rc = 0;
	bool			delay_completion = false;
	struct iv_sync_cb_info	*iv_sync_cb = NULL;
	struct crt_iv_ops	*iv_ops;
	crt_bulk_t		local_bulk = CRT_BULK_NULL;
	d_rank_list_t		excluded_list;
	d_rank_t		excluded_ranks[1]; /* Excluding self */

	iv_ops = crt_iv_ops_get(ivns_internal, class_id);
	D_ASSERT(iv_ops != NULL);

	switch (sync_type->ivs_mode) {
	case CRT_IV_SYNC_NONE:
		D_DEBUG(DB_TRACE, "NONE syncMode\n");
		D_GOTO(exit, rc = 0);

	case CRT_IV_SYNC_EAGER:
		D_DEBUG(DB_TRACE, "EAGER syncMode\n");
		delay_completion = true;
		break;

	case CRT_IV_SYNC_LAZY:
		D_DEBUG(DB_TRACE, "LAZY syncMode\n");
		delay_completion = false;
		break;

	default:
		D_ERROR("Unknown ivs_mode %d\n", sync_type->ivs_mode);
		D_GOTO(exit, rc = -DER_INVAL);
	}

	/* Exclude self from corpc */
	excluded_list.rl_nr = 1;
	excluded_list.rl_ranks = excluded_ranks;
	excluded_ranks[0] = ivns_internal->cii_grp_priv->gp_self;
	/* Perform refresh on local node */
	if (sync_type->ivs_event == CRT_IV_SYNC_EVENT_UPDATE)
		rc = iv_ops->ivo_on_refresh(ivns_internal, iv_key, 0,
					iv_value, iv_value ? false : true,
					0, user_priv);
	else if (sync_type->ivs_event == CRT_IV_SYNC_EVENT_NOTIFY)
		rc = iv_ops->ivo_on_refresh(ivns_internal, iv_key, 0,
					NULL, iv_value ? false : true,
					0, user_priv);
	else {
		D_ERROR("Unknown ivs_event %d\n", sync_type->ivs_event);
		D_GOTO(exit, rc = -DER_INVAL);
	}

	local_bulk = CRT_BULK_NULL;
	if (iv_value != NULL) {
		D_DEBUG(DB_TRACE, "Create Bulk\n");
		rc = crt_bulk_create(ivns_internal->cii_ctx, iv_value,
				     CRT_BULK_RO, &local_bulk);
		if (rc != 0) {
			D_ERROR("ctt_bulk_create(): "DF_RC"\n", DP_RC(rc));
			D_GOTO(exit, rc);
		}
	}

	rc = crt_corpc_req_create(ivns_internal->cii_ctx,
				  &ivns_internal->cii_grp_priv->gp_pub,
				  &excluded_list,
				  CRT_OPC_IV_SYNC,
				  local_bulk, NULL, 0,
				  ivns_internal->cii_gns.gn_tree_topo,
				  &corpc_req);
	if (rc != 0) {
		D_ERROR("crt_corpc_req_create(): "DF_RC"\n", DP_RC(rc));
		D_GOTO(exit, rc);
	}

	input = crt_req_get(corpc_req);
	D_ASSERT(input != NULL);

	D_ALLOC_PTR(iv_sync_cb);
	if (iv_sync_cb == NULL) {
		D_GOTO(exit, rc = -DER_NOMEM);
	}

	iv_sync_cb->isc_sync_type = *sync_type;
	input->ivs_ivns_id = ivns_internal->cii_gns.gn_ivns_id.ii_nsid;
	input->ivs_ivns_group = ivns_internal->cii_gns.gn_ivns_id.ii_group_name;
	D_RWLOCK_RDLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	input->ivs_grp_ver = ivns_internal->cii_grp_priv->gp_membs_ver;
	D_RWLOCK_UNLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	d_iov_set(&input->ivs_key, iv_key->iov_buf, iv_key->iov_buf_len);
	d_iov_set(&input->ivs_sync_type, &iv_sync_cb->isc_sync_type,
		  sizeof(crt_iv_sync_t));

	input->ivs_class_id = class_id;

	iv_sync_cb->isc_bulk_hdl = local_bulk;
	iv_sync_cb->isc_do_callback = delay_completion;
	iv_sync_cb->isc_user_priv = user_priv;
	iv_sync_cb->isc_ivns_internal = NULL;

	/* Perform callback from sync response handler */
	if (iv_sync_cb->isc_do_callback) {
		iv_sync_cb->isc_ivns_internal = ivns_internal;
		IVNS_ADDREF(ivns_internal);

		iv_sync_cb->isc_update_comp_cb = update_comp_cb;
		iv_sync_cb->isc_cb_arg = cb_arg;
		iv_sync_cb->isc_update_rc = update_rc;
		iv_sync_cb->isc_class_id = class_id;

		/* Copy iv_key over as it will get destroyed after this call */
		D_ALLOC(iv_sync_cb->isc_iv_key.iov_buf, iv_key->iov_buf_len);
		if (iv_sync_cb->isc_iv_key.iov_buf == NULL) {
			/* Avoid checkpatch warning */
			D_GOTO(exit, rc = -DER_NOMEM);
		}

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

	rc = crt_req_send(corpc_req, handle_response_cb, iv_sync_cb);
	if (rc != 0)
		D_ERROR("crt_req_send(): "DF_RC"\n", DP_RC(rc));

exit:
	if (delay_completion == false || rc != 0) {
		if (rc != 0)
			update_rc = rc;

		update_comp_cb(ivns_internal, class_id, iv_key, NULL, iv_value,
			       update_rc, cb_arg);
		if (rc == 0)
			iv_ops->ivo_on_put(ivns_internal, NULL, user_priv);
	}

	if (rc != 0) {
		if (local_bulk != CRT_BULK_NULL)
			crt_bulk_free(local_bulk);

		if (iv_sync_cb) {
			if (iv_sync_cb->isc_ivns_internal)
				IVNS_DECREF(iv_sync_cb->isc_ivns_internal);
			D_FREE(iv_sync_cb->isc_iv_key.iov_buf);
			D_FREE(iv_sync_cb);
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

/* Helper function for finalizing of transfer back of the iv_value
 * from a parent back to the child
 */
static void
finalize_transfer_back(struct update_cb_info *cb_info, int rc)
{
	struct crt_ivns_internal	*ivns;
	struct crt_iv_ops		*iv_ops;
	struct crt_iv_update_out	*child_output;

	child_output = crt_reply_get(cb_info->uci_child_rpc);
	child_output->rc = rc;

	ivns = cb_info->uci_ivns_internal;

	iv_ops = crt_iv_ops_get(ivns, cb_info->uci_class_id);
	D_ASSERT(iv_ops != NULL);

	iv_ops->ivo_on_put(ivns, &cb_info->uci_iv_value,
			   cb_info->uci_user_priv);

	crt_reply_send(cb_info->uci_child_rpc);

	/* ADDREF done in crt_hdlr_iv_update */
	crt_bulk_free(cb_info->uci_bulk_hdl);
	RPC_PUB_DECREF(cb_info->uci_child_rpc);

	/* addref in transfer_back_to_child() */
	IVNS_DECREF(cb_info->uci_ivns_internal);
	D_FREE(cb_info);
}

/* Bulk update completion callback for transferring values back
 * to the original caller/child.
 */
static int
bulk_update_transfer_back_done(const struct crt_bulk_cb_info *info)
{
	finalize_transfer_back(info->bci_arg, info->bci_rc);
	return 0;
}

/* Helper function to transfer iv_value back to child */
static
int transfer_back_to_child(crt_iv_key_t *key, struct update_cb_info *cb_info,
			   bool do_refresh, int update_rc)
{
	struct crt_bulk_desc		bulk_desc = {0};
	struct crt_iv_update_in		*child_input;
	struct crt_ivns_internal	*ivns;
	struct crt_iv_ops		*iv_ops;
	int				size = 0;
	int				i;
	int				rc = 0;

	ivns = cb_info->uci_ivns_internal;

	iv_ops = crt_iv_ops_get(ivns, cb_info->uci_class_id);
	D_ASSERT(iv_ops != NULL);

	if (do_refresh)
		iv_ops->ivo_on_refresh(ivns, key, 0,
				&cb_info->uci_iv_value,
				false, update_rc, cb_info->uci_user_priv);

	/* No more children -- we are the originator; call update_cb */
	if (cb_info->uci_child_rpc == NULL) {
		cb_info->uci_comp_cb(ivns, cb_info->uci_class_id, key, NULL,
				     &cb_info->uci_iv_value, update_rc,
				     cb_info->uci_cb_arg);

		/* Corresponding on_get() done in crt_iv_update_internal */
		iv_ops->ivo_on_put(ivns, NULL, cb_info->uci_user_priv);

		if (cb_info->uci_bulk_hdl != CRT_BULK_NULL)
			crt_bulk_free(cb_info->uci_bulk_hdl);

		/* addref done in crt_hdlr_iv_update */
		IVNS_DECREF(cb_info->uci_ivns_internal);
		D_FREE(cb_info);
		return 0;
	}

	/* Perform bulk transfer back to the child */
	child_input = crt_req_get(cb_info->uci_child_rpc);

	/* Calculate size of iv value */
	for (i = 0; i < cb_info->uci_iv_value.sg_nr; i++)
		size += cb_info->uci_iv_value.sg_iovs[i].iov_buf_len;

	bulk_desc.bd_rpc = cb_info->uci_child_rpc;
	bulk_desc.bd_bulk_op = CRT_BULK_PUT;
	bulk_desc.bd_remote_hdl = child_input->ivu_iv_value_bulk;
	bulk_desc.bd_remote_off = 0;
	bulk_desc.bd_local_hdl = cb_info->uci_bulk_hdl;
	bulk_desc.bd_local_off = 0;
	bulk_desc.bd_len = size;

	rc = crt_bulk_transfer(&bulk_desc, bulk_update_transfer_back_done,
			       cb_info, 0);
	if (rc != 0) {
		D_ERROR("Failed to transfer data back\n");
		/* IVNS_DECREF done in the function */
		finalize_transfer_back(cb_info, rc);
	}
	return rc;
}

/* IV_UPDATE internal rpc response handler */
static void
handle_ivupdate_response(const struct crt_cb_info *cb_info)
{
	struct update_cb_info	 *iv_info = cb_info->cci_arg;
	struct crt_iv_update_in	 *input = crt_req_get(cb_info->cci_rpc);
	struct crt_iv_update_out *output = crt_reply_get(cb_info->cci_rpc);
	struct crt_iv_update_out *child_output;
	struct crt_iv_ops	 *iv_ops;
	int			 rc;

	/* For bi-directional updates, transfer data back to child */
	if (iv_info->uci_sync_type.ivs_flags & CRT_IV_SYNC_BIDIRECTIONAL) {
		transfer_back_to_child(&input->ivu_key, iv_info, true,
				       cb_info->cci_rc ?: output->rc);
		D_GOTO(exit, 0);
	}

	iv_ops = crt_iv_ops_get(iv_info->uci_ivns_internal,
				iv_info->uci_class_id);
	D_ASSERT(iv_ops != NULL);

	if (iv_info->uci_child_rpc) {
		child_output = crt_reply_get(iv_info->uci_child_rpc);

		/* uci_bulk_hdl will not be set for invalidate call */
		if (iv_info->uci_bulk_hdl != CRT_BULK_NULL)
			iv_ops->ivo_on_put(iv_info->uci_ivns_internal, &iv_info->uci_iv_value,
					   iv_info->uci_user_priv);

		child_output->rc = output->rc;

		if (cb_info->cci_rc != 0)
			child_output->rc = cb_info->cci_rc;

		/* Respond back to child; might fail if child is not alive */
		if (crt_reply_send(iv_info->uci_child_rpc) != DER_SUCCESS)
			D_ERROR("Failed to respond on rpc: %p\n", iv_info->uci_child_rpc);

		/* ADDREF done in crt_hdlr_iv_update */
		RPC_PUB_DECREF(iv_info->uci_child_rpc);
	} else {
		d_sg_list_t *tmp_iv_value;

		if (iv_info->uci_bulk_hdl == NULL)
			tmp_iv_value = NULL;
		else
			tmp_iv_value = &iv_info->uci_iv_value;

		rc = output->rc;

		if (cb_info->cci_rc != 0)
			rc = cb_info->cci_rc;

		rc = crt_ivsync_rpc_issue(iv_info->uci_ivns_internal,
					  iv_info->uci_class_id,
					  &input->ivu_key, 0,
					  tmp_iv_value,
					  &iv_info->uci_sync_type,
					  input->ivu_caller_node,
					  input->ivu_root_node,
					  iv_info->uci_comp_cb,
					  iv_info->uci_cb_arg,
					  iv_info->uci_user_priv,
					  rc);
		if (rc != 0) {
			iv_ops->ivo_on_put(iv_info->uci_ivns_internal, tmp_iv_value,
					   iv_info->uci_user_priv);
		}
	}

	if (iv_info->uci_bulk_hdl != CRT_BULK_NULL)
		crt_bulk_free(iv_info->uci_bulk_hdl);

	/* addref done in crt_hdlr_iv_update */
	IVNS_DECREF(iv_info->uci_ivns_internal);
	D_FREE(iv_info);
exit:
	;	/* avoid compiler error: gcc 3.4 */
}

/* Helper function to issue IV UPDATE RPC*/
static int
crt_ivu_rpc_issue(d_rank_t dest_rank, crt_iv_key_t *iv_key,
		  d_sg_list_t *iv_value, crt_iv_sync_t *sync_type,
		  d_rank_t root_rank, uint32_t grp_ver,
		  struct update_cb_info *cb_info)
{
	struct crt_ivns_internal	*ivns_internal;
	struct crt_iv_update_in		*input;
	crt_bulk_t			local_bulk = CRT_BULK_NULL;
	crt_endpoint_t			ep = {0};
	crt_rpc_t			*rpc;
	int				rc = 0;
	uint32_t			local_grp_ver;

	ivns_internal = cb_info->uci_ivns_internal;

	/* Note: destination node is using global rank already */
	ep.ep_grp = NULL;
	ep.ep_rank = dest_rank;

	rc = crt_req_create(ivns_internal->cii_ctx, &ep, CRT_OPC_IV_UPDATE,
			    &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create(): "DF_RC"\n", DP_RC(rc));
		D_GOTO(exit, rc);
	}

	input = crt_req_get(rpc);

	/* Update with NULL value is invalidate call */
	if (iv_value) {
		rc = crt_bulk_create(ivns_internal->cii_ctx, iv_value,
				     CRT_BULK_RW, &local_bulk);

		if (rc != 0) {
			D_ERROR("crt_bulk_create(): "DF_RC"\n", DP_RC(rc));
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

	input->ivu_ivns_id = ivns_internal->cii_gns.gn_ivns_id.ii_nsid;
	input->ivu_ivns_group = ivns_internal->cii_gns.gn_ivns_id.ii_group_name;

	/*
	 * If the current version does not match that which has come in,
	 * then the version number does not match that version associated
	 * with the root rank node we are sending to.
	 */
	D_RWLOCK_RDLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	local_grp_ver =  ivns_internal->cii_grp_priv->gp_membs_ver;
	D_RWLOCK_UNLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	if (grp_ver != local_grp_ver) {
		D_DEBUG(DB_ALL,
			"Group (%s) version mismatch. "
			"On entry: %d: Changed to :%d\n",
			ivns_internal->cii_gns.gn_ivns_id.ii_group_name,
			grp_ver, local_grp_ver);
		D_GOTO(exit, rc = -DER_GRPVER);
	}
	input->ivu_grp_ver = grp_ver;

	/* Do not need sync comp cb for update */
	cb_info->uci_sync_type = *sync_type;
	d_iov_set(&input->ivu_sync_type, &cb_info->uci_sync_type,
		  sizeof(crt_iv_sync_t));
	rc = crt_req_send(rpc, handle_response_cb, cb_info);
	if (rc != 0)
		D_ERROR("crt_req_send(): "DF_RC"\n", DP_RC(rc));

exit:
	if (rc != 0) {
		if (local_bulk != CRT_BULK_NULL)
			crt_bulk_free(local_bulk);
	}
	return rc;
}

static void
handle_response_internal(void *arg)
{
	const struct crt_cb_info *cb_info = arg;
	crt_rpc_t		 *rpc = cb_info->cci_rpc;
	void			 *cb_arg = cb_info->cci_arg;

	switch (rpc->cr_opc) {
	case CRT_OPC_IV_FETCH:
		handle_ivfetch_response(cb_info);
		break;

	case CRT_OPC_IV_SYNC:
		handle_ivsync_response(cb_info);
		break;

	case CRT_OPC_IV_UPDATE:
		handle_ivupdate_response(cb_info);
		break;
	default:
		D_ERROR("wrong opc cb_info: %p rpc: %p opc: %#x\n", cb_info, rpc, rpc->cr_opc);
		D_FREE(cb_arg);
	}
}

static void
handle_response_cb_internal(void *arg)
{
	struct crt_cb_info	*cb_info = arg;
	crt_rpc_t		*rpc = cb_info->cci_rpc;
	struct crt_rpc_priv	*rpc_priv;

	handle_response_internal(arg);

	rpc_priv = container_of(rpc, struct crt_rpc_priv, crp_pub);
	RPC_DECREF(rpc_priv);
	D_FREE(cb_info);
}

static void
handle_response_cb(const struct crt_cb_info *cb_info)
{
	crt_rpc_t		*rpc = cb_info->cci_rpc;
	struct crt_rpc_priv	*rpc_priv;
	struct crt_context	*crt_ctx;

	/* handle locally generated errors during IV operations synchronously to ensure unregister
	 * of bulk buffer will occur before freeing it, just in case peer will finally make it
	 * unexpectedly
	 */
	if (cb_info->cci_rc == -DER_TIMEDOUT || cb_info->cci_rc == -DER_EXCLUDED ||
	    cb_info->cci_rc == -DER_CANCELED)
		goto callback;

	rpc_priv = container_of(rpc, struct crt_rpc_priv, crp_pub);
	D_ASSERT(rpc_priv != NULL);
	crt_ctx = rpc_priv->crp_pub.cr_ctx;

	/* Current call back response */
	if (crt_ctx->cc_iv_resp_cb != NULL) {
		int rc;
		struct crt_cb_info *info;

		D_ALLOC_PTR(info);
		if (info == NULL) {
			D_WARN("allocate fails, do cb directly\n");
			goto callback;
		}

		/* Create child process to handle the call back */
		RPC_ADDREF(rpc_priv);
		info->cci_rpc = cb_info->cci_rpc;
		info->cci_rc = cb_info->cci_rc;
		info->cci_arg = cb_info->cci_arg;

		rc = crt_ctx->cc_iv_resp_cb((crt_context_t)crt_ctx,
					    info,
					    handle_response_cb_internal,
					    crt_ctx->cc_rpc_cb_arg);
		if (rc) {
			D_WARN("rpc_cb failed %d, do cb directly\n", rc);
			RPC_DECREF(rpc_priv);
			D_FREE(info);
			goto callback;
		}
		return;
	}
callback:
	handle_response_internal((void *)cb_info);
}

/* bulk transfer update callback info */
struct bulk_update_cb_info {
	struct crt_ivns_internal *buc_ivns;

	/* Input buffer for iv update rpc */
	struct crt_iv_update_in	*buc_input;
	/* Local bulk handle to free */
	crt_bulk_t		buc_bulk_hdl;
	/* IV value */
	d_sg_list_t		buc_iv_value;
	/* Users private data */
	void			*buc_user_priv;
};

static int
bulk_update_transfer_done_aux(const struct crt_bulk_cb_info *info)
{
	struct bulk_update_cb_info	*cb_info;
	struct crt_ivns_internal	*ivns_internal;
	struct crt_iv_ops		*iv_ops;
	struct crt_iv_update_in		*input;
	struct crt_iv_update_out	*output;
	struct update_cb_info		*update_cb_info = NULL;
	int				rc = 0;
	d_rank_t			next_rank;
	int				update_rc;
	crt_iv_sync_t			*sync_type;
	uint32_t			grp_ver;

	cb_info = info->bci_arg;

	input = cb_info->buc_input;

	ivns_internal = cb_info->buc_ivns;
	D_ASSERT(ivns_internal != NULL);

	iv_ops = crt_iv_ops_get(ivns_internal, input->ivu_class_id);
	D_ASSERT(iv_ops != NULL);

	output = crt_reply_get(info->bci_bulk_desc->bd_rpc);
	D_ASSERT(output != NULL);

	if (info->bci_rc != 0) {
		D_ERROR("bulk update transfer failed; "DF_RC"\n", DP_RC(info->bci_rc));
		D_GOTO(send_error, rc = info->bci_rc);
	}

	update_rc = iv_ops->ivo_on_update(ivns_internal,
					  &input->ivu_key, 0, false,
					  &cb_info->buc_iv_value,
					  cb_info->buc_user_priv);

	sync_type = input->ivu_sync_type.iov_buf;

	D_ALLOC_PTR(update_cb_info);
	if (update_cb_info == NULL)
		D_GOTO(send_error, rc = -DER_NOMEM);

	update_cb_info->uci_child_rpc = info->bci_bulk_desc->bd_rpc;

	update_cb_info->uci_ivns_internal = ivns_internal;
	IVNS_ADDREF(ivns_internal);

	update_cb_info->uci_class_id = input->ivu_class_id;
	update_cb_info->uci_caller_rank = input->ivu_caller_node;
	update_cb_info->uci_sync_type = *sync_type;
	update_cb_info->uci_user_priv = cb_info->buc_user_priv;
	update_cb_info->uci_iv_value = cb_info->buc_iv_value;
	update_cb_info->uci_bulk_hdl = cb_info->buc_bulk_hdl;

	if (update_rc == -DER_IVCB_FORWARD) {
		/*
		 * Forward request to the parent
		 * Get group version to associate with next_rank.
		 * Pass it down to crt_ivu_rpc_issue
		 */
		D_RWLOCK_RDLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
		grp_ver = ivns_internal->cii_grp_priv->gp_membs_ver;

		rc = crt_iv_parent_get(ivns_internal,
				       input->ivu_root_node, &next_rank);
		D_RWLOCK_UNLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);

		if (rc != 0) {
			D_DEBUG(DB_TRACE, "crt_iv_parent_get() returned %d\n",
				rc);
			D_GOTO(send_error, rc = -DER_OOG);
		}

		rc = crt_ivu_rpc_issue(next_rank, &input->ivu_key,
				       &cb_info->buc_iv_value, sync_type,
				       input->ivu_root_node, grp_ver,
				       update_cb_info);
		if (rc != 0) {
			D_ERROR("crt_ivu_rpc_issue(): "DF_RC"\n", DP_RC(rc));
			D_GOTO(send_error, rc);
		}
	} else if (update_rc == 0) {
		/* If sync was bi-directional - transfer value back */
		if (sync_type->ivs_flags & CRT_IV_SYNC_BIDIRECTIONAL) {
			rc = transfer_back_to_child(&input->ivu_key,
						    update_cb_info,
						    false, update_rc);
			if (rc == 0)
				rc = update_rc;

			D_GOTO(exit, rc);
		}
		output->rc = -DER_SUCCESS;
		iv_ops->ivo_on_put(ivns_internal, &cb_info->buc_iv_value, cb_info->buc_user_priv);

		crt_reply_send(info->bci_bulk_desc->bd_rpc);
		RPC_PUB_DECREF(info->bci_bulk_desc->bd_rpc);

		IVNS_DECREF(update_cb_info->uci_ivns_internal);
		D_FREE(update_cb_info);
	} else {
		D_GOTO(send_error, rc = update_rc);
	}

	rc = crt_bulk_free(cb_info->buc_bulk_hdl);
exit:
	return rc;

send_error:
	iv_ops->ivo_on_put(ivns_internal, &cb_info->buc_iv_value,
			   cb_info->buc_user_priv);

	rc = crt_bulk_free(cb_info->buc_bulk_hdl);
	output->rc = rc;

	crt_reply_send(info->bci_bulk_desc->bd_rpc);
	RPC_PUB_DECREF(info->bci_bulk_desc->bd_rpc);

	if (update_cb_info) {
		IVNS_DECREF(update_cb_info->uci_ivns_internal);
		D_FREE(update_cb_info);
	}

	return rc;
}

static void
bulk_update_transfer_done_aux_wrapper(void *arg)
{
	struct crt_bulk_cb_info		*info;
	struct bulk_update_cb_info	*cb_info;

	info = arg;

	D_DEBUG(DB_TRACE, "Triggering bulk_update_transfer_done_aux()\n");

	bulk_update_transfer_done_aux(info);

	cb_info = info->bci_arg;

	/* addref done by crt_hdlr_iv_update() */
	IVNS_DECREF(cb_info->buc_ivns);
	D_FREE(cb_info);

	D_FREE(info->bci_bulk_desc);
	D_FREE(info);
}

static int
bulk_update_transfer_done(const struct crt_bulk_cb_info *info)
{
	struct crt_bulk_cb_info		*info_dup;
	struct bulk_update_cb_info	*cb_info;
	struct crt_ivns_internal	*ivns_internal;
	struct crt_iv_ops		*iv_ops;
	struct crt_iv_update_in		*input;
	struct crt_iv_update_out	*output;
	int				 rc = DER_SUCCESS;

	cb_info = info->bci_arg;

	input = cb_info->buc_input;

	ivns_internal = cb_info->buc_ivns;
	D_ASSERT(ivns_internal != NULL);

	iv_ops = crt_iv_ops_get(ivns_internal, input->ivu_class_id);
	D_ASSERT(iv_ops != NULL);

	output = crt_reply_get(info->bci_bulk_desc->bd_rpc);
	D_ASSERT(output != NULL);

	if (info->bci_rc != 0) {
		D_ERROR("bulk update transfer failed; "DF_RC"\n",
			DP_RC(info->bci_rc));
		D_GOTO(send_error, rc = info->bci_rc);
	}

	if (iv_ops->ivo_pre_update != NULL) {
		D_ALLOC_PTR(info_dup);
		if (info_dup == NULL)
			D_GOTO(send_error, rc = -DER_NOMEM);

		D_ALLOC_PTR(info_dup->bci_bulk_desc);
		if (info_dup->bci_bulk_desc == NULL) {
			D_FREE(info_dup);
			D_GOTO(send_error, rc = -DER_NOMEM);
		}

		/* cb_info is inside of bci_arg */
		info_dup->bci_arg = info->bci_arg;
		info_dup->bci_rc = info->bci_rc;
		crt_bulk_desc_dup(info_dup->bci_bulk_desc,
				  info->bci_bulk_desc);
		IV_DBG(&input->ivu_key, "Executing ivo_pre_update\n");

		/* Note: cb_info free-ed by the aux_wrapper */
		iv_ops->ivo_pre_update(ivns_internal, &input->ivu_key,
				       bulk_update_transfer_done_aux_wrapper,
				       info_dup);
	} else {
		bulk_update_transfer_done_aux(info);

		/* addref done by crt_hdlr_iv_update() */
		IVNS_DECREF(cb_info->buc_ivns);
		D_FREE(cb_info);
	}
	return rc;

send_error:
	output->rc = rc;
	crt_reply_send(info->bci_bulk_desc->bd_rpc);

	crt_bulk_free(cb_info->buc_bulk_hdl);
	RPC_PUB_DECREF(info->bci_bulk_desc->bd_rpc);

	/* addref done by crt_hdlr_iv_update() */
	IVNS_DECREF(cb_info->buc_ivns);
	D_FREE(cb_info);
	return rc;
}

/* IV UPDATE RPC handler */
void
crt_hdlr_iv_update(crt_rpc_t *rpc_req)
{
	struct crt_iv_update_in		*input;
	struct crt_iv_update_out	*output;
	struct crt_ivns_id		ivns_id;
	struct crt_ivns_internal	*ivns_internal = NULL;
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
	uint32_t			grp_ver_entry;
	uint32_t			grp_ver_current;
	bool				put_needed = false;
	int				rc = 0;

	input = crt_req_get(rpc_req);
	output = crt_reply_get(rpc_req);

	D_ASSERT(input != NULL);
	D_ASSERT(output != NULL);

	ivns_id.ii_group_name = input->ivu_ivns_group;
	ivns_id.ii_nsid = input->ivu_ivns_id;

	/* ADDREF */
	ivns_internal = crt_ivns_internal_lookup(&ivns_id);

	if (ivns_internal == NULL) {
		D_ERROR("Invalid internal ivns\n");
		D_GOTO(send_error, rc = -DER_NONEXIST);
	}

	/* Check group version match with rpc request*/
	D_RWLOCK_RDLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	grp_ver_entry = ivns_internal->cii_grp_priv->gp_membs_ver;
	D_RWLOCK_UNLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	if (grp_ver_entry != input->ivu_grp_ver) {
		D_DEBUG(DB_ALL,
			"Group (%s) version mismatch. Local: %d Remote :%d\n",
			ivns_id.ii_group_name, grp_ver_entry,
			input->ivu_grp_ver);
		D_GOTO(send_error, rc = -DER_GRPVER);
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
			/*
			 * MUST use version number prior to rpc version
			 * check get next_rank to send to.
			 * Otherwise, we could miss a version change that
			 * happens between these 2 points.
			 */
			rc = crt_iv_parent_get(ivns_internal,
					       input->ivu_root_node,
					       &next_rank);
			if (rc != 0) {
				D_DEBUG(DB_TRACE, "crt_iv_parent_get() rc=%d\n",
					rc);
				D_GOTO(send_error, rc = -DER_OOG);
			}

			/*
			 * Check here for change in version prior to getting
			 * next
			 */
			D_RWLOCK_RDLOCK(&ivns_internal->cii_grp_priv->
					gp_rwlock);
			grp_ver_current =
				ivns_internal->cii_grp_priv->gp_membs_ver;
			D_RWLOCK_UNLOCK(&ivns_internal->cii_grp_priv->
					gp_rwlock);
			if (grp_ver_entry != grp_ver_current) {
				D_DEBUG(DB_ALL,
					"Group (%s) version mismatch. "
					"On Entry: %d:: Changed to:%d\n",
					ivns_id.ii_group_name,
					grp_ver_entry, grp_ver_current);
				D_GOTO(send_error, rc = -DER_GRPVER);
			}

			D_ALLOC_PTR(update_cb_info);
			if (update_cb_info == NULL)
				D_GOTO(send_error, rc = -DER_NOMEM);

			sync_type = (crt_iv_sync_t *)
					input->ivu_sync_type.iov_buf;

			update_cb_info->uci_child_rpc = rpc_req;
			RPC_PUB_ADDREF(rpc_req);
			update_cb_info->uci_ivns_internal = ivns_internal;
			IVNS_ADDREF(ivns_internal);
			update_cb_info->uci_class_id = input->ivu_class_id;
			update_cb_info->uci_caller_rank =
							input->ivu_caller_node;

			update_cb_info->uci_sync_type = *sync_type;

			rc = crt_ivu_rpc_issue(next_rank, &input->ivu_key,
					       NULL, sync_type,
					       input->ivu_root_node,
					       grp_ver_entry,
					       update_cb_info);

			if (rc != 0) {
				RPC_PUB_DECREF(rpc_req);
				IVNS_DECREF(update_cb_info->uci_ivns_internal);
				D_FREE(update_cb_info);
				D_GOTO(send_error, rc);
			}

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
		D_ERROR("ivo_on_get(): "DF_RC"\n", DP_RC(rc));
		D_GOTO(send_error, rc);
	}
	put_needed = true;

	size = 0;
	for (i = 0; i < iv_value.sg_nr; i++)
		size += iv_value.sg_iovs[i].iov_buf_len;

	rc = crt_bulk_create(rpc_req->cr_ctx, &iv_value, CRT_BULK_RW,
			     &local_bulk_handle);
	if (rc != 0) {
		D_ERROR("crt_bulk_create(): "DF_RC"\n", DP_RC(rc));
		D_GOTO(send_error, rc);
	}

	bulk_desc.bd_rpc = rpc_req;
	RPC_PUB_ADDREF(rpc_req);
	bulk_desc.bd_bulk_op = CRT_BULK_GET;
	bulk_desc.bd_remote_hdl = input->ivu_iv_value_bulk;
	bulk_desc.bd_remote_off = 0;
	bulk_desc.bd_local_hdl = local_bulk_handle;
	bulk_desc.bd_local_off = 0;
	bulk_desc.bd_len = size;

	D_ALLOC_PTR(cb_info);
	if (cb_info == NULL) {
		RPC_PUB_DECREF(bulk_desc.bd_rpc);
		crt_bulk_free(local_bulk_handle);
		D_GOTO(send_error, rc = -DER_NOMEM);
	}

	cb_info->buc_ivns = ivns_internal;
	IVNS_ADDREF(ivns_internal);
	cb_info->buc_input = input;
	cb_info->buc_bulk_hdl = local_bulk_handle;
	cb_info->buc_iv_value = iv_value;
	cb_info->buc_user_priv = user_priv;

	rc = crt_bulk_transfer(&bulk_desc, bulk_update_transfer_done,
			       cb_info, 0);
	if (rc != 0) {
		D_ERROR("crt_bulk_transfer(): "DF_RC"\n", DP_RC(rc));
		crt_bulk_free(local_bulk_handle);
		RPC_PUB_DECREF(bulk_desc.bd_rpc);
		IVNS_DECREF(cb_info->buc_ivns);
		D_FREE(cb_info);
		D_GOTO(send_error, rc);
	}

exit:
	/* ADDREF done in lookup above */
	IVNS_DECREF(ivns_internal);
	return;

send_error:
	output->rc = rc;
	crt_reply_send(rpc_req);

	if (put_needed)
		iv_ops->ivo_on_put(ivns_internal, &iv_value, &user_priv);

	/* ADDREF done in lookup above */
	if (ivns_internal)
		IVNS_DECREF(ivns_internal);
}

static int
check_sync_type(crt_iv_sync_t *sync)
{
	int rc = 0;

	D_ASSERT(sync != NULL);

	/* Bidirectional sync is only allowed during UPDATE event */
	if (sync->ivs_flags & CRT_IV_SYNC_BIDIRECTIONAL) {
		if (sync->ivs_mode != CRT_IV_SYNC_NONE) {
			D_ERROR("ivs_mode must be set to CRT_IV_SYNC_NONE\n");
			return -DER_INVAL;
		}

		if (sync->ivs_event != CRT_IV_SYNC_EVENT_UPDATE) {
			D_ERROR("ivs_event must be set to "
				"CRT_IV_SYNC_EVENT_UPDATE\n");
			return -DER_INVAL;
		}
	}
	return rc;
}

static int
crt_iv_update_internal(crt_iv_namespace_t ivns, uint32_t class_id,
		       crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
		       d_sg_list_t *iv_value, crt_iv_shortcut_t shortcut,
		       crt_iv_sync_t sync_type, crt_iv_comp_cb_t update_comp_cb,
		       void *cb_arg)
{
	struct crt_iv_ops		*iv_ops;
	struct crt_ivns_internal	*ivns_internal = NULL;
	d_rank_t			 root_rank;
	d_rank_t			 next_node;
	struct update_cb_info		*cb_info;
	void				*priv;
	int				 rc = 0;
	uint32_t			 grp_ver;
	uint32_t			 grp_ver2;

	rc = check_sync_type(&sync_type);
	if (rc != 0) {
		D_ERROR("Invalid sync specified\n");
		D_GOTO(exit, rc);
	}

	/* IVNS_ADDREF is done upon successful get */
	ivns_internal = crt_ivns_internal_get(ivns);
	if (ivns_internal == NULL) {
		D_ERROR("Invalid ivns specified\n");
		D_GOTO(exit, rc = -DER_NONEXIST);
	}

	if (ivns_internal->cii_grp_priv->gp_self == CRT_NO_RANK) {
		IV_DBG(iv_key, "%s: self rank not known yet\n",
		       ivns_internal->cii_grp_priv->gp_pub.cg_grpid);
		D_GOTO(exit, rc = -DER_GRPVER);
	}

	iv_ops = crt_iv_ops_get(ivns_internal, class_id);
	if (iv_ops == NULL) {
		D_ERROR("Invalid class_id specified\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}

	/* Need to get a version number associated with root_rank. */
	D_RWLOCK_RDLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	grp_ver =  ivns_internal->cii_grp_priv->gp_membs_ver;

	rc = iv_ops->ivo_on_hash(ivns, iv_key, &root_rank);
	D_RWLOCK_UNLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
	if (rc != 0) {
		D_CDEBUG(rc == -DER_NOTLEADER, DB_ANY, DLOG_ERR,
			 "ivo_on_hash() failed, rc="DF_RC"\n",
			 DP_RC(rc));
		D_GOTO(exit, rc);
	}

	rc = iv_ops->ivo_on_get(ivns, iv_key, 0, CRT_IV_PERM_WRITE, NULL, &priv);
	if (rc != 0) {
		D_ERROR("ivo_on_get(): " DF_RC, DP_RC(rc));
		D_GOTO(exit, rc);
	}

	if (iv_value != NULL)
		rc = iv_ops->ivo_on_update(ivns, iv_key, 0,
			(root_rank == ivns_internal->cii_grp_priv->gp_self),
			iv_value, priv);
	else
		rc = iv_ops->ivo_on_refresh(ivns, iv_key, 0, NULL,
					true, 0, priv);

	if (rc == 0) {
		if (sync_type.ivs_flags & CRT_IV_SYNC_BIDIRECTIONAL) {
			rc = update_comp_cb(ivns_internal, class_id, iv_key,
					    NULL, iv_value, rc, cb_arg);
		} else {
			/* issue sync. will call completion callback */
			rc = crt_ivsync_rpc_issue(ivns_internal, class_id,
						  iv_key, iv_ver, iv_value,
						  &sync_type,
						  ivns_internal->cii_grp_priv->
									gp_self,
						  root_rank, update_comp_cb,
						  cb_arg, priv, rc);
			/* on_put() done in crt_ivsync_rpc_issue() */
			if (rc == 0)
				D_GOTO(exit, rc);
		}

		D_GOTO(put, rc);
	} else  if (rc == -DER_IVCB_FORWARD) {
		/*
		 * Send synchronization to parent
		 * Need to get a version number associated with next node.
		 * Need to compare with previous version.  If not equal,
		 * then there has been a version change in between.
		 */
		D_RWLOCK_RDLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
		grp_ver2 =  ivns_internal->cii_grp_priv->gp_membs_ver;

		rc = get_shortcut_path(ivns_internal, root_rank, shortcut,
				       &next_node);
		D_RWLOCK_UNLOCK(&ivns_internal->cii_grp_priv->gp_rwlock);
		if (rc != 0)
			D_GOTO(put, rc);

		if (grp_ver != grp_ver2) {
			D_DEBUG(DB_ALL,
				"Group (%s) version mismatch. "
				"On Entry: %d:: Changed to:%d\n",
				ivns_internal->cii_gns.gn_ivns_id.ii_group_name,
				grp_ver, grp_ver2);
			D_GOTO(put, rc = -DER_GRPVER);
		}

		/* comp_cb is only for sync update for now */
		D_ALLOC_PTR(cb_info);
		if (cb_info == NULL)
			D_GOTO(put, rc = -DER_NOMEM);

		cb_info->uci_comp_cb = update_comp_cb;
		cb_info->uci_cb_arg = cb_arg;

		cb_info->uci_child_rpc = NULL;
		cb_info->uci_ivns_internal = ivns_internal;
		IVNS_ADDREF(ivns_internal);

		cb_info->uci_class_id = class_id;
		cb_info->uci_caller_rank = ivns_internal->cii_grp_priv->gp_self;
		cb_info->uci_user_priv = priv;

		rc = crt_ivu_rpc_issue(next_node, iv_key, iv_value,
				       &sync_type, root_rank,
				       grp_ver, cb_info);

		if (rc != 0) {
			D_ERROR("crt_ivu_rpc_issue(): "DF_RC"\n", DP_RC(rc));
			IVNS_DECREF(cb_info->uci_ivns_internal);
			D_FREE(cb_info);
			D_GOTO(put, rc);
		}

		D_GOTO(exit, rc);
	} else {
		D_CDEBUG(rc == -DER_NONEXIST || rc == -DER_NOTLEADER,
			 DLOG_INFO, DLOG_ERR,
			 "ivo_on_update failed with rc = "DF_RC"\n",
			 DP_RC(rc));

		update_comp_cb(ivns, class_id, iv_key, NULL,
			       iv_value, rc, cb_arg);
		D_GOTO(put, rc);
	}

put:
	iv_ops->ivo_on_put(ivns, NULL, priv);
exit:
	if (ivns_internal)
		IVNS_DECREF(ivns_internal);
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
		rc = -DER_INVAL;

		D_ERROR("iv_value is NULL " DF_RC "\n", DP_RC(rc));

		update_comp_cb(ivns, class_id, iv_key, NULL, iv_value, rc, cb_arg);

		D_GOTO(exit, rc);
	}

	rc = crt_iv_update_internal(ivns, class_id, iv_key, iv_ver, iv_value,
				    shortcut, sync_type, update_comp_cb,
				    cb_arg);
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
	struct crt_ivns_internal	*ivns_internal = NULL;
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
		D_GOTO(exit, rc = -DER_NONEXIST);
	}

	self_rank = ivns_internal->cii_grp_priv->gp_self;
	if (self_rank == CRT_NO_RANK) {
		D_DEBUG(DB_ALL, "%s: self rank not known yet\n",
			ivns_internal->cii_grp_priv->gp_pub.cg_grpid);
		D_GOTO(exit, rc = -DER_GRPVER);
	}

	iv_ops = crt_iv_ops_get(ivns_internal, class_id);
	if (iv_ops == NULL) {
		D_ERROR("Invalid class_id specified\n");
		D_GOTO(exit, rc = -DER_INVAL);
	}
	rc = iv_ops->ivo_on_hash(ivns, iv_key, &root_rank);
	if (rc != 0) {
		D_CDEBUG(rc == -DER_NOTLEADER, DB_ANY, DLOG_ERR,
			 "ivo_on_hash() failed, rc="DF_RC"\n",
			 DP_RC(rc));
		D_GOTO(exit, rc);
	}

	rc = crt_tree_get_nchildren(ivns_internal->cii_grp_priv, 0, NULL,
				    ivns_internal->cii_gns.gn_tree_topo,
				    root_rank, self_rank,
				    nchildren);
	if (rc != 0)
		D_ERROR("grp %s, root %d self %d failed; "DF_RC"\n",
			ivns_internal->cii_grp_priv->gp_pub.cg_grpid,
			root_rank, self_rank, DP_RC(rc));

exit:
	/* addref done in crt_ivns_internal_get() */
	if (ivns_internal)
		IVNS_DECREF(ivns_internal);
	return rc;
}
