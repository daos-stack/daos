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

/* CaRT (Collective and RPC Transport) IV (Incast Variable) APIs. */

#ifndef __CRT_IV_H__
#define __CRT_IV_H__

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <uuid/uuid.h>

#include <crt_types.h>
#include <crt_errno.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Local handle for an incast variable namespace */
typedef void		*crt_iv_namespace_t;

/**
 * The version is an optional feature of incast variable. Each iv can have its
 * own version which is a customized value from upper layer or application.
 *
 * It can be used to identify and aggregate updates from different nodes of the
 * group (parent can ignore update from children if it has the same or higher
 * update version that has already done previously). Or to resolve conflicting
 * updates that higher version wins. The detailed semantics provided by user.
 * User can select to not use the version in which case there is no aggregation
 * for updates, for this usage user can always use one same version for example
 * zero.
 */
typedef uint32_t	crt_iv_ver_t;

/**
 * The shortcut hints to optimize the request propagation.
 *
 * One usage example is indicating the #level of the tree of the group to avoid
 * the request traverses every level.
 * Another possible usage is to indicate user's accessing behavior for example:
 * contention unlikely means the request with low likelihood to content with
 * other requests, so the request can be directly sent to root.
 *
 * Currently only supports CRT_IV_SHORTCUT_TO_ROOT which directly sends to root
 * node. Will add other kind of shortcut should requirements arise in future.
 *
 * User can indicate it for every specific fetch/update request.
 */
typedef enum {
	/* no shortcut */
	CRT_IV_SHORTCUT_NONE	= 0,
	/* directly send request to root node */
	CRT_IV_SHORTCUT_TO_ROOT	= 1
} crt_iv_shortcut_t;

/* key is the unique ID for IV within namespace */
typedef crt_iov_t	crt_iv_key_t;

/**
 * Incast variable on_fetch callback which will be called when the fetching
 * request propagated to the node.
 *
 * \param ivns [IN]		the local handle of the IV namespace
 * \param iv_key [IN]		key of the IV
 * \param iv_ver [IN/OUT]	version of the IV
 * \param root_flag [IN]	true when on root node or the IV
 *				(TODO really need it? as user can calculate it
 *				 by using on_hash callback).
 * \param iv_value [OUT]	IV value returned
 *
 * \return			zero on success handled locally,
 *				-CER_IVCB_FORWARD when cannot handle locally and
 *				need to forward to next hop,
 *				other negative value if error
 */
typedef int (*crt_iv_on_fetch_cb_t)(crt_iv_namespace_t ivns,
				    crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
				    bool root_flag, crt_sg_list_t *iv_value);

/**
 * Incast variable on_update callback which will be called when the updating
 * request propagated to the node (flowing up from leaf to root).
 *
 * \param ivns [IN]		the local handle of the IV namespace
 * \param iv_key [IN]		key of the IV
 * \param iv_ver [IN]		version of the IV
 * \param root_flag [IN]	true when on root node or the IV
 *				(TODO really need it? as user can calculate it
 *				 by using on_hash callback).
 * \param iv_value [IN]		IV value to be update
 *
 * \return			zero on success handled locally,
 *				-CER_IVCB_FORWARD when cannot handle locally and
 *				need to forward to next hop,
 *				other negative value if error
 */
typedef int (*crt_iv_on_update_cb_t)(crt_iv_namespace_t ivns,
				     crt_iv_key_t *iv_key, crt_iv_ver_t iv_ver,
				     bool root_flag, crt_sg_list_t *iv_value);

/**
 * Incast variable on_refresh callback which will be called when the
 * synchronization/notification propagated to the node (flowing down from root
 * to leaf), or when serving invalidate request.
 *
 * \param ivns [IN]		the local handle of the IV namespace
 * \param iv_key [IN]		key of the IV
 * \param iv_ver [IN]		version of the IV
 * \param iv_value [IN]		IV value to be refresh
 * \param invalidate [IN]       true for invalidate the IV in which case the
 *				iv_ver and iv_value can be ignored.
 *
 * \return			zero on success handled locally,
 *				-CER_IVCB_FORWARD when cannot handle locally and
 *				need to forward to next hop,
 *				other negative value if error
 */
typedef int (*crt_iv_on_refresh_cb_t)(crt_iv_namespace_t ivns,
				      crt_iv_key_t *iv_key, crt_iv_ver_t iv_ver,
				      crt_sg_list_t *iv_value, bool invalidate);

/**
 * The hash function to hash one IV's key to a crt_rank_t result which is to be
 * the root node of that IV.
 *
 * The root of IV is the node that finally serve the IV fetch/update request if
 * the request cannot be satisfied by intermediate nodes. User can provide a
 * hash function to make it be controllable by itself.
 *
 * \param ivns [IN]		the local handle of the IV namespace
 * \param iv_key [IN]		key of the IV
 * \param root [OUT]		the hashed result root rank
 *
 * \return			zero on success, negative value if error
 */
typedef int (*crt_iv_on_hash_cb_t)(crt_iv_namespace_t ivns,
				   crt_iv_key_t *iv_key, crt_rank_t *root);

struct crt_iv_ops {
	crt_iv_on_fetch_cb_t		ivo_on_fetch;
	crt_iv_on_update_cb_t		ivo_on_update;
	crt_iv_on_refresh_cb_t		ivo_on_refresh;
	crt_iv_on_hash_cb_t		ivo_on_hash;
};

/**
 * IV class to classify incast variables with common properties or features,
 * for example:
 * 1) When root node synchronizes the update to other nodes, whether or not need
 *    to keep the same order, i.e. different updates be made to all nodes with
 *    the same order. Otherwise only highest version will be updated to IV and
 *    all lower version are ignored -- this is suitable for overwriting usecase.
 * 2) When switching incast tree (for fault-tolerant), whether or not discard
 *    the internal cache for IV.
 * These similar usages can use ivc_feats (feature bits) to differentiate.
 *
 * The IV callbacks are bonded to IV class which is identified by a unique
 * class ID (ivc_id). User can provide different or same callbacks for different
 * IV classes.
 */
/* some IV feature bit flags for IV class */
#define CRT_IV_CLASS_UPDATE_IN_ORDER	(0x0001U)
#define CRT_IV_CLASS_DISCARD_CACHE	(0x0002U)

struct crt_iv_class {
	/* ID of the IV class */
	uint32_t		 ivc_id;
	/* feature bits of the IV class */
	uint32_t		 ivc_feats;
	/* IV callback table for the IV class */
	struct crt_iv_ops	*ivc_ops;
};

/**
 * Create an incast variable namespace.
 *
 * \param crt_ctx [IN]          CRT transport namespace
 * \param grp [IN]		CRT group for the IV namespace
 * \param tree_topo[IN]		tree topology for the IV message propagation,
 *				can be calculated by crt_tree_topo().
 *				/see enum crt_tree_type, /see crt_tree_topo().
 * \param iv_classes [IN]	the array of IV class. User must ensure passing
 *				same set of iv_classes when creating or
 *				attaching IV namespace, or undefined results are
 *				expected.
 * \param num_class [IN]	the number of elements in the iv_classes array,
 *				one IV namespace should have at least one IV
 *				class.
 * \param ivns [OUT]		the local handle of the IV namespace
 * \param g_ivns [OUT]		the global handle of the IV namespace. It can be
 *				transferred to other processes on any other node
 *				in the group, and the crt_iv_namespace_attach()
 *				can attach to the global handle to get a local
 *				usable IV namespace handle.
 *
 * \return			zero on success, negative value if error
 */
int
crt_iv_namespace_create(crt_context_t crt_ctx, crt_group_t *grp, int tree_topo,
			struct crt_iv_class *iv_classes, uint32_t num_class,
			crt_iv_namespace_t *ivns, crt_iov_t *g_ivns);

/**
 * Attach to a global IV namespace to get a local handle of the IV namespace.
 *
 * User should call crt_iv_namespace_create on one node within the group of the
 * namespace, and call crt_iv_namespace_attach on all other nodes with in the
 * group. And need to provide the same set of IV classes, Otherwise IV behaviour
 * is undermined.
 *
 * \param crt_ctx [IN]		CRT transport namespace
 * \param g_ivns [IN]		the global handle of the IV namespace
 * \param iv_classes [IN]	the array of IV class. User must ensure passing
 *				same set of iv_classes when creating or
 *				attaching IV namespace, or undefined results are
 *				expected.
 * \param num_class [IN]	the number of elements in the iv_classes array,
 *				one IV namespace should have at least one IV
 *				class.
 * \param ivns [OUT]		the local handle of the IV namespace
 *
 * \return			zero on success, negative value if error
 */
int
crt_iv_namespace_attach(crt_context_t crt_ctx, crt_iov_t *g_ivns,
			struct crt_iv_class *iv_classes, uint32_t num_class,
			crt_iv_namespace_t *ivns);

/**
 * Destroy an IV namespace, after that all related resources of the namespace
 * (including all IVs in the namespace) are released. It is a local operation,
 * every node in the group needs to do the destroy respectively.
 *
 * \param ivns [IN]		the local handle of the IV namespace
 *
 * \return			zero on success, negative value if error
 */
int
crt_iv_namespace_destroy(crt_iv_namespace_t ivns);


/* IV fetch/update/invalidate completion callback
 *
 * \param ivns [IN]		the local handle of the IV namespace
 * \param class_id [IN]		IV class ID the IV belong to
 * \param iv_key [IN/OUT]	key of the IV, output only for fetch
 * \param iv_ver [IN]		version of the IV
 * \param iv_value [IN/OUT]	IV value buffer, input for update, output for
 *				fetch.
 * \param cb_arg [IN]		pointer to argument passed to fetch/update/
 *				invalidate.
 *
 * \return			zero on success, negative value if error
 */
typedef int (*crt_iv_comp_cb_t)(crt_iv_namespace_t ivns, uint32_t class_id,
				crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
				crt_sg_list_t *iv_value,
				void *cb_args);

/**
 * Fetch the value of incast variable.
 *
 * \param ivns [IN]		the local handle of the IV namespace
 * \param class_id [IN]		IV class ID the IV belong to
 * \param iv_key [IN]		key of the IV
 * \param iv_ver [IN/OUT]	version of the IV
 *				for input parameter:
 *				1) (version == 0) means caller does not care
 *				what version it is, or depend on updating’s
 *				synchronization to get back the fresh value.
 *				2) (version == -1) means caller wants to get
 *				back the latest value, the fetch request will
 *				always be propagated to root node to fetch the
 *				up-to-date value.
 *				3) other positive value means caller want to
 *				get back the value equal or higher than the
 *				version.
 *				The actual version will be returned through this
 *				parameter in fetch_comp_cb.
 * \param iv_value [OUT]	IV value, buffer provided by caller, result data
 *				available in fetch_comp_cb.
 * \param shortcut [IN]		the shortcut hints to optimize the propagation
 *				of accessing request, \see crt_iv_shortcut_t
 * \param fetch_comp_cb [IN]	pointer to fetch completion callback
 * \param cb_arg [IN]		pointer to argument passed to fetch_comp_cb
 *
 * \return			zero on success, negative value if error
 */
int
crt_iv_fetch(crt_iv_namespace_t ivns, uint32_t class_id,
	    crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
	    crt_sg_list_t *iv_value, crt_iv_shortcut_t shortcut,
	    crt_iv_comp_cb_t fetch_comp_cb, void *cb_arg);

/**
 * The mode of synchronizing the update request or notification (from root to
 * other nodes).
 *
 * When user updates one incast variable, it can select to deliver an update
 * synchronization or notification to all nodes.
 * CRT_IV_SYNC_NONE  - no synchronization required.
 * CRT_IV_SYNC_EAGER - must synchronize update request or notification to all
 *		       nodes firstly, and then finish the update.
 * CRT_IV_SYNC_LAZY  - can make the update and finish it firstly and then
 *		       synchronize the update to all nodes.
 *		       For lazy synchronization, IV framework will lazily
 *		       synchronize the update request to all nodes.
 *		       And will keep the consistent order of updating if
 *		       CRT_IV_CLASS_UPDATE_IN_ORDER is set in the iv class.
 */
typedef enum {
	CRT_IV_SYNC_NONE	= 0,
	CRT_IV_SYNC_EAGER	= 1,
	CRT_IV_SYNC_LAZY	= 2,
} crt_iv_sync_mode_t;

#define CRT_IV_SYNC_EVENT_UPDATE	(0x01U)
#define CRT_IV_SYNC_EVENT_NOTIFY	(0x02U)

typedef struct {
	crt_iv_sync_mode_t	ivs_mode;
	/*
	 * The ivs_event can be the bit flag of:
	 * CRT_IV_SYNC_EVENT_UPDATE -- synchronize the update request
	 * CRT_IV_SYNC_EVENT_NOTIFY -- synchronize the notification that some
	 *			       IVs are updated
	 * The on_refresh callback will be triggered when the update/notify
	 * propagates to one node, for notify NULL will be passed for the
	 * iv_value parameter. /see crt_iv_on_update_cb_t.
	 */
	uint32_t		ivs_event;
} crt_iv_sync_t;

/* some common crt_iv_sync_t definitions */
#define CRT_IV_SYNC_NONE	(crt_iv_sync_t {0, 0})
#define CRT_IV_SYNC_UPDATE_EAGER					\
	(crt_iv_sync_t {CRT_IV_SYNC_EVENT_UPDATE,	CRT_IV_SYNC_EAGER})
#define CRT_IV_SYNC_UPDATE_LAZY						\
	(crt_iv_sync_t {CRT_IV_SYNC_EVENT_UPDATE,	CRT_IV_SYNC_LAZY})
#define CRT_IV_SYNC_NOTIFY_EAGER					\
	(crt_iv_sync_t {CRT_IV_SYNC_EVENT_NOTIFY,	CRT_IV_SYNC_EAGER})
#define CRT_IV_SYNC_NOTIFY_LAZY						\
	(crt_iv_sync_t {CRT_IV_SYNC_EVENT_NOTIFY,	CRT_IV_SYNC_LAZY})

/**
 * Update the value of incast variable.
 *
 * \param ivns [IN]		the local handle of the IV namespace
 * \param class_id [IN]		IV class ID the IV belong to
 * \param iv_key [IN]		key of the IV
 * \param iv_ver [IN]		version of the IV
 * \param iv_value [IN]		IV value buffer
 * \param shortcut [IN]		the shortcut hints to optimize the propagation
 *				of accessing request, \see crt_iv_shortcut_t
 * \param sync_type [IN]	synchronization type.
 *				If user wants to synchronize the update or
 *				notification to other nodes, it can select eager
 *				or lazy mode, and update or notification.
 *				\see crt_iv_sync_t.
 * \param update_comp_cb [IN]	pointer to update completion callback
 * \param cb_arg [IN]		pointer to argument passed to update_comp_cb
 *
 * \return			zero on success, negative value if error
 */
int
crt_iv_update(crt_iv_namespace_t ivns, uint32_t class_id,
	      crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
	      crt_sg_list_t *iv_value, crt_iv_shortcut_t shortcut,
	      crt_iv_sync_t sync_type, crt_iv_comp_cb_t update_comp_cb,
	      void *cb_arg);

/**
 * Invalidate an incast variable.
 *
 * It will invalidate cache on all nodes(by calling the on_refresh callback with
 * invalidate flag set as true). User only needs to call it on one node, it will
 * internally do a broadcast and ensure the on_refresh callback being called on
 * all nodes within the group of the namespace.
 *
 * \param ivns [IN]		the local handle of the IV namespace
 * \param class_id [IN]		IV class ID the IV belong to
 * \param iv_key [IN]		key of the IV
 * \param invali_comp_cb [IN]	pointer to invalidate completion callback
 * \param cb_arg [IN]		pointer to argument passed to invali_comp_cb
 *
 * \return			zero on success, negative value if error
 */
int
crt_iv_invalidate(crt_iv_namespace_t ivns, uint32_t class_id,
		  crt_iv_key_t *iv_key, crt_iv_comp_cb_t invali_comp_cb,
		  void *cb_arg);

#if defined(__cplusplus)
}
#endif

#endif /*  __CRT_IV_H__ */
