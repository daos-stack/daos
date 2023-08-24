/*
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * \file
 *
 * CaRT (Collective and RPC Transport) IV (Incast Variable) APIs and types.
 */

#ifndef __CRT_IV_H__
#define __CRT_IV_H__

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <uuid/uuid.h>

#include <cart/types.h>
#include <daos_errno.h>

#if defined(__cplusplus)
extern "C" {
#endif

/** @defgroup CART_IV CART IV API */

/** @addtogroup CART_IV
 * @{
 */

/** Local handle for an incast variable namespace */
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
 * One usage example is indicating the level of the tree of the group to avoid
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
	/** no shortcut */
	CRT_IV_SHORTCUT_NONE	= 0,
	/** directly send request to root node */
	CRT_IV_SHORTCUT_TO_ROOT	= 1
} crt_iv_shortcut_t;

/** key is the unique ID for IV within namespace */
typedef d_iov_t	crt_iv_key_t;

/**
 * Operation flags passed to callbacks
 *
 * Currently only supports CRT_IV_FLAG_PENDING_FETCH flag. This flag will be
 * set during on_fetch() callback whenever such is called as part of the
 * aggregation logic. Based on this flag, client has ability to perform
 * desired optimizations, such as potentially reusing iv_value buffers
 * previously allocated/reserved.
 */
typedef enum {
	/** Called node is the root for the operation */
	CRT_IV_FLAG_ROOT = 0x1,
	/** Fetch was performed as a result of aggregation */
	CRT_IV_FLAG_PENDING_FETCH = 0x2,
} crt_iv_flag_t;

/**
 * Incast variable on_fetch callback which will be called when the fetching
 * request propagated to the node.
 *
 * \param[in] ivns		the local handle of the IV namespace
 * \param[in] iv_key		key of the IV
 * \param[in,out] iv_ver	version of the IV
 * \param[in] flags		OR-ed combination of 0 or more crt_iv_flag_t
 *				flags
 * \param[out] iv_value	IV value returned
 * \param[in] arg		private user data
 *
 * \retval			DER_SUCCESS on success handled locally,
 * \retval			-DER_IVCB_FORWARD when cannot handle locally and
 *				need to forward to next hop,
 *				other negative value if error
 */
typedef int (*crt_iv_on_fetch_cb_t)(crt_iv_namespace_t ivns,
				    crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
				    uint32_t flags, d_sg_list_t *iv_value,
				    void *arg);

/**
 * Incast variable on_update callback which will be called when the updating
 * request propagated to the node (flowing up from leaf to root).
 *
 * \param[in] ivns		the local handle of the IV namespace
 * \param[in] iv_key		key of the IV
 * \param[in] iv_ver		version of the IV
 * \param[in] flags		OR-ed combination of 0 or more crt_iv_flag_t
 *				flags
 * \param[in] iv_value		IV value to be update
 * \param[in] arg		private user data
 *
 * \retval			DER_SUCCESS on success handled locally,
 * \retval			-DER_IVCB_FORWARD when cannot handle locally and
 *				need to forward to next hop,
 *				other negative value if error
 */
typedef int (*crt_iv_on_update_cb_t)(crt_iv_namespace_t ivns,
				     crt_iv_key_t *iv_key, crt_iv_ver_t iv_ver,
				     uint32_t flags, d_sg_list_t *iv_value,
				     void *arg);

/**
 * If provided, this callback is executed on intermediate nodes during
 * crt_iv_fetch() before ivo_on_fetch(). This callback has to call cb(cb_arg),
 * either synchronously or asynchronously. cb(cb_arg) will execute user-provided
 * ivo_on_fetch(). This callback gives the user a chance to execute
 * ivo_on_fetch() outside the crt_progress() function.
 *
 * \param[in] ivns		the local handle of the IV namespace
 * \param[in] iv_key		key of the IV
 * \param[in] cb		a callback which must be called or scheduled by
 *				the user in order to compelete the handling of
 *				the crt_iv_fetch() request.
 * \param[in] cb_arg		arguments for \a cb.
 *
 * \note Here pre_fetch() merely means it will be executed before
 * ivo_on_fetch(). It's not related to the notion of prefeching data. Same for
 * pre_update() and pre_refresh().
 */
typedef void (*crt_iv_pre_fetch_cb_t)(crt_iv_namespace_t ivns,
				     crt_iv_key_t *iv_key,
				     crt_generic_cb_t cb,
				     void *cb_arg);

/**
 * If provided, this callback is executed on intermediate nodes during
 * crt_iv_update() before ivo_on_update(). This callback has to call cb(cb_arg),
 * either synchronously or asynchronously. cb(cb_arg) will execute user-provided
 * ivo_on_update(). This callback gives the user a chance to execute
 * ivo_on_update() outside the crt_progress() function.
 *
 * \param[in] ivns		the local handle of the IV namespace
 * \param[in] iv_key		key of the IV
 * \param[in] cb		a callback which must be called or scheduled by
 *				the user in order to compelete the handling of
 *				the crt_iv_update() request.
 * \param[in] cb_arg		arguments for \a cb.
 */
typedef void (*crt_iv_pre_update_cb_t)(crt_iv_namespace_t ivns,
				     crt_iv_key_t *iv_key,
				     crt_generic_cb_t cb,
				     void *cb_arg);

/**
 * If provided, this callback is executed on intermediate nodes during
 * IV request before ivo_on_refresh(). This callback has to call cb(cb_arg),
 * either synchronously or asynchronously. cb(cb_arg) will execute user-provided
 * ivo_on_refresh(). This callback gives the user a chance to execute
 * ivo_on_refresh() outside the crt_progress() function.
 *
 * \param[in] ivns		the local handle of the IV namespace
 * \param[in] iv_key		key of the IV
 * \param[in] cb		a callback which must be called or scheduled by
 *				the user in order to compelete the handling of
 *				the crt_iv_sync() request.
 * \param[in] cb_arg		arguments for \a cb.
 */
typedef void (*crt_iv_pre_refresh_cb_t)(crt_iv_namespace_t ivns,
				     crt_iv_key_t *iv_key,
				     crt_generic_cb_t cb,
				     void *cb_arg);

/**
 * Incast variable on_refresh callback which will be called when the
 * synchronization/notification propagated to the node (flowing down from root
 * to leaf), or when serving invalidate request. It also will be called when the
 * fetch request's reply flows down; if fetch request is not successful this
 * callback will be invoked with NULL for iv_value.
 *
 * \param[in] ivns		the local handle of the IV namespace
 * \param[in] iv_key		key of the IV
 * \param[in] iv_ver		version of the IV
 * \param[in] iv_value		IV value to be refresh
 * \param[in] invalidate       true for invalidate the IV in which case the
 *				iv_ver and iv_value can be ignored.
 * \param[in] rc		Status of the operation.
 * \param[in] arg		private user data
 *
 * \return			DER_SUCCESS on success, negative value if error
 */
typedef int (*crt_iv_on_refresh_cb_t)(crt_iv_namespace_t ivns,
				      crt_iv_key_t *iv_key, crt_iv_ver_t iv_ver,
				      d_sg_list_t *iv_value, bool invalidate,
				      int rc, void *arg);

/**
 * The hash function to hash one IV's key to a d_rank_t result which is to be
 * the root node of that IV.
 *
 * The root of IV is the node that finally serves the IV fetch/update request if
 * the request cannot be satisfied by intermediate nodes.
 *
 * \param[in] ivns		the local handle of the IV namespace
 * \param[in] iv_key		key of the IV
 * \param[out] root		the hashed result root rank
 *
 * \return			DER_SUCCESS on success, negative value if error
 */
typedef int (*crt_iv_on_hash_cb_t)(crt_iv_namespace_t ivns,
				   crt_iv_key_t *iv_key, d_rank_t *root);


/**
 * Permission flag passed to crt_iv_on_get_cb_t
 */
typedef enum {
	CRT_IV_PERM_READ = 0x1,
	CRT_IV_PERM_WRITE = 0x2,
} crt_iv_perm_t;

/**
 * Get value function to get buffers for iv_value for the specified iv_key.
 *
 * Callback implementation is expected to provide buffers inside of iv_value
 * parameter with sufficient space to store value for the specified iv_key.
 *
 * If iv_value == NULL, then it means the caller does not need the buffer,
 * but in this callback, it should still check if it can access the IV
 * value by permission flag(\ref crt_iv_perm_t), and setup the cache entry if
 * necessary.
 *
 * When called with CRT_IV_PERM_READ permission, it will fetch the IV value,
 * i.e. the following callback will be iv_on_fetch(), and also returned iv_value
 * will only be used for reading data out of it.
 *
 * When called with CRT_IV_PERM_WRITE permission, it will update the IV value,
 * i.e. the following callback will be iv_on_update() etc, and also returned
 * iv_value will be used by framework for storage of intermediate values.
 *
 * Callback implementation should consider iv_value being 'in use' until
 * a corresponding crt_iv_on_put_cb_t callback is called.
 *
 * \param[in] ivns		the local handle to the IV namespace
 * \param[in] iv_key		key of the IV
 * \param[in] iv_ver		Version of iv_key
 * \param[in] permission	crt_iv_perm_t flags
 * \param[out] iv_value	Resultant placeholder for iv value buffer
 * \param[out] arg		Pointer to the private data
 *
 * \return			DER_SUCCESS on success, negative value if error
 */
typedef int (*crt_iv_on_get_cb_t)(crt_iv_namespace_t ivns,
				  crt_iv_key_t *iv_key, crt_iv_ver_t iv_ver,
				  crt_iv_perm_t permission,
				  d_sg_list_t *iv_value,
				  void **arg);

/**
 * Put value function to return buffers retrieved for the specified iv_key
 * Original buffers in iv_value are to be retrieved via
 * crt_iv_on_get_cb_t call.
 *
 * \param[in] ivns		the local handle to the IV namespace
 * \param[in] iv_value		iv_value buffers to return
 * \param[in] arg		private user data
 *
 */
typedef void (*crt_iv_on_put_cb_t)(crt_iv_namespace_t ivns, d_sg_list_t *iv_value, void *arg);

/**
 * Compares two passed iv keys 'key1' and 'key2' and returns either
 * true or false. This is an optional callback that clients can implement
 * if they do not want a default 'memcmp' comparison for keys.
 *
 * Key comparison is used during fetch aggregation logic. Two requests
 * going for the same key will be aggregated if keys match.
 *
 *
 * \param[in] ivns		the local handle to the IV namespace
 * \param[in] key1		first iv key
 * \param[in] key2		second iv key
 *
 * \return			true if keys match, false otherwise
 */
typedef bool (*crt_iv_keys_match_cb_t)(crt_iv_namespace_t ivns,
				crt_iv_key_t *key1, crt_iv_key_t *key2);

/**
 * If provided, this callback will be called before the
 * synchronization/notification is propagated (flowing down from root to leaf)
 * to the child nodes, if any.
 *
 * \param[in] ivns		the local handle of the IV namespace
 * \param[in] iv_key		key of the IV
 * \param[in] iv_ver		version of the IV
 * \param[in] iv_value		IV value to be refresh
 * \param[in] arg		private user data
 *
 * \return			DER_SUCCESS on success, negative value if error
 */
typedef int (*crt_iv_pre_sync_cb_t)(crt_iv_namespace_t ivns,
				    crt_iv_key_t *iv_key, crt_iv_ver_t iv_ver,
				    d_sg_list_t *iv_value, void *arg);

struct crt_iv_ops {
	crt_iv_pre_fetch_cb_t	ivo_pre_fetch;
	crt_iv_on_fetch_cb_t	ivo_on_fetch;
	crt_iv_pre_update_cb_t	ivo_pre_update;
	crt_iv_on_update_cb_t	ivo_on_update;
	crt_iv_pre_refresh_cb_t	ivo_pre_refresh;
	crt_iv_on_refresh_cb_t	ivo_on_refresh;
	crt_iv_on_hash_cb_t	ivo_on_hash;
	crt_iv_on_get_cb_t	ivo_on_get;
	crt_iv_on_put_cb_t	ivo_on_put;
	crt_iv_keys_match_cb_t	ivo_keys_match;
	crt_iv_pre_sync_cb_t	ivo_pre_sync;
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
	/** ID of the IV class */
	uint32_t		 ivc_id;
	/** feature bits of the IV class */
	uint32_t		 ivc_feats;
	/** IV callback table for the IV class */
	struct crt_iv_ops	*ivc_ops;
};

/**
 * Create an incast variable namespace.
 *
 * \param[in] crt_ctx		CRT transport namespace
 * \param[in] grp		CRT group for the IV namespace
 * \param[in] tree_topo		tree topology for the IV message propagation,
 *				can be calculated by crt_tree_topo().
 *				See \a enum crt_tree_type,
 *				\a crt_tree_topo().
 * \param[in] iv_classes	the array of IV class. User must ensure passing
 *				same set of iv_classes when adding IV namespace
 *				on all participating nodes.
 * \param[in] num_class		the number of elements in the iv_classes array,
 *				one IV namespace should have at least one IV
 *				class.
 * \param[in] iv_ns_id		Unique id, identifying the namespace within the
 *				group.
 * \param[out] ivns		Local handle of the IV namespace
 *
 * \return			DER_SUCCESS on success, negative value if error
 */
int
crt_iv_namespace_create(crt_context_t crt_ctx, crt_group_t *grp, int tree_topo,
		struct crt_iv_class *iv_classes, uint32_t num_classes,
		uint32_t iv_ns_id, crt_iv_namespace_t *ivns);

/**
 * Create an incast variable namespace with associated user priv
 *
 * \param[in] crt_ctx		CRT transport namespace
 * \param[in] grp		CRT group for the IV namespace
 * \param[in] tree_topo		tree topology for the IV message propagation,
 *				can be calculated by crt_tree_topo().
 *				See \a enum crt_tree_type,
 *				\a crt_tree_topo().
 * \param[in] iv_classes	the array of IV class. User must ensure passing
 *				same set of iv_classes when adding IV namespace
 *				on all participating nodes.
 * \param[in] num_class		the number of elements in the iv_classes array,
 *				one IV namespace should have at least one IV
 *				class.
 * \param[in] iv_ns_id		Unique id, identifying the namespace within the
 *				group.
 * \param[in] user_priv		Optional private data to associate with IV
 *				namespace
 * \param[out] ivns		Local handle of the IV namespace
 *
 * \return			DER_SUCCESS on success, negative value if error
 */
int
crt_iv_namespace_create_priv(crt_context_t crt_ctx, crt_group_t *grp,
		int tree_topo, struct crt_iv_class *iv_classes,
		uint32_t num_classes, uint32_t iv_ns_id, void *user_priv,
		crt_iv_namespace_t *ivns);

/**
 * Retrieve IV namespace id from the handle.
 *
 * \param[in] ivns		Incast variable namespace handle
 * \param[out] id		Associated id returned
 *
 * \return			DER_SUCCESS on success, negative value on error
 */
int
crt_iv_namespace_id_get(crt_iv_namespace_t *ivns, uint32_t *id);

/**
 * Associate priv data with IV namespace
 *
 * \param[in] ivns		Incast variable namespace handle
 * \param[in] priv		Private user data
 *
 * \return			DER_SUCCESS on success, negative value on error
 */
int
crt_iv_namespace_priv_set(crt_iv_namespace_t *ivns, void *priv);

/**
 * Retrieve private data associated with IV namespace
 *
 * \param[in] ivns		Incast variable namespace handle
 * \param[out] priv		Private user data
 *
 * \return			DER_SUCCESS on success, negative value on error
 */
int
crt_iv_namespace_priv_get(crt_iv_namespace_t *ivns, void **priv);

/**
 * Completion callback for \ref crt_iv_namespace_destroy.
 *
 * \param[in] ivns		the local handle of the IV namespace that has
 *				been destroyed.
 * \param[in] cb_arg		pointer to argument provide by the user to
 *				crt_iv_namespace_destroy.
 */
typedef void (*crt_iv_namespace_destroy_cb_t)(crt_iv_namespace_t ivns,
					      void *cb_arg);

/**
 * Destroy an IV namespace, after that all related resources of the namespace
 * (including all IVs in the namespace) are released. It is a local operation,
 * every node in the group needs to do the destroy respectively.
 *
 * \param[in] ivns		the local handle of the IV namespace
 * \param[in] cb		pointer to completion callback
 * \param[in] cb_arg		pointer to completion callback argument, will be
 *				available as cb_arg in \a cb.
 * \return			DER_SUCCESS on success, negative value if error
 */
int
crt_iv_namespace_destroy(crt_iv_namespace_t ivns,
			 crt_iv_namespace_destroy_cb_t cb,
			 void *cb_arg);

/**
 * IV fetch/update/invalidate completion callback
 *
 * \param[in] ivns		the local handle of the IV namespace
 * \param[in] class_id		IV class ID the IV belong to
 * \param[in,out] iv_key	key of the IV, output only for fetch
 * \param[in] iv_ver		version of the IV
 * \param[in,out] iv_value	IV value buffer, input for update, output for
 *				fetch.
 * \param[in] rc		Return code of fetch/update/invalidate operation
 * \param[in] cb_arg		pointer to argument passed to fetch/update/
 *				invalidate.
 *
 * \return			DER_SUCCESS on success, negative value if error
 */
typedef int (*crt_iv_comp_cb_t)(crt_iv_namespace_t ivns, uint32_t class_id,
				crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
				d_sg_list_t *iv_value,
				int rc, void *cb_arg);

/**
 * Fetch the value of incast variable.
 *
 * \param[in] ivns		the local handle of the IV namespace
 * \param[in] class_id		IV class ID the IV belong to
 * \param[in] iv_key		key of the IV
 * \param[in,out] iv_ver	version of the IV
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
 * \param[in] shortcut		the shortcut hints to optimize the propagation
 *				of accessing request, See \ref crt_iv_shortcut_t
 * \param[in] fetch_comp_cb	pointer to fetch completion callback
 * \param[in] cb_arg		pointer to argument passed to fetch_comp_cb
 *
 * \return			DER_SUCCESS on success, negative value if error
 */
int
crt_iv_fetch(crt_iv_namespace_t ivns, uint32_t class_id,
	    crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
	    crt_iv_shortcut_t shortcut,
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


/**
 * The type of the synchronization event requested.
 */
typedef enum {
	/** No synchronization */
	CRT_IV_SYNC_EVENT_NONE		= 0,
	/**
	 * Update synchronization. IV value is propagated to all the
	 * nodes during the synchronization phase.
	 */
	CRT_IV_SYNC_EVENT_UPDATE	= 1,
	/**
	 * Notification only. IV value is not propagated during the
	 * synchronization phase.
	 */
	CRT_IV_SYNC_EVENT_NOTIFY	= 2,
} crt_iv_sync_event_t;

typedef enum {
	/** Treat namespace lookup errors as fatal during sync */
	CRT_IV_SYNC_FLAG_NS_ERRORS_FATAL = 0x1,

	/**
	 * Bi-directional update. When this flag is set, it causes IV
	 * framework to propagate IV value in both directions --
	 * from the caller of crt_iv_update up to the root and from the root
	 * back to the caller.
	 * The default behavior is to only propagate IV value from the
	 * caller up to the root.
	 *
	 * Currently, if this flag is specified ivs_mode must be set to
	 * CRT_IV_SYNC_NONE, ivs_event must be set to CRT_IV_SYNC_EVENT_UPDATE
	 */
	CRT_IV_SYNC_BIDIRECTIONAL = 0x2,
} crt_iv_sync_flag_t;

typedef int (*crt_iv_sync_done_cb_t)(void *cb_arg, int rc);
typedef struct {
	crt_iv_sync_mode_t	ivs_mode;
	crt_iv_sync_event_t	ivs_event;
	/* OR-ed combination of 0 or more crt_iv_sync_flag_t flags */
	uint32_t		ivs_flags;
} crt_iv_sync_t;

/* some common crt_iv_sync_t definitions */
#define CRT_IV_SYNC_MODE_NONE	{0, 0, 0}

#define CRT_IV_SYNC_UPDATE_EAGER(flags) \
	((crt_iv_sync_t) {CRT_IV_SYNC_EVENT_UPDATE, CRT_IV_SYNC_EAGER, flags})

#define CRT_IV_SYNC_UPDATE_LAZY(flags) \
	((crt_iv_sync_t) {CRT_IV_SYNC_EVENT_UPDATE, CRT_IV_SYNC_LAZY, flags})

#define CRT_IV_SYNC_NOTIFY_EAGER(flags) \
	((crt_iv_sync_t) {CRT_IV_SYNC_EVENT_NOTIFY, CRT_IV_SYNC_EAGER, flags})

#define CRT_IV_SYNC_NOTIFY_LAZY(flags) \
	((crt_iv_sync_t) {CRT_IV_SYNC_EVENT_NOTIFY, CRT_IV_SYNC_LAZY, flags})

/**
 * Update the value of incast variable.
 *
 * \param[in] ivns		the local handle of the IV namespace
 * \param[in] class_id		IV class ID the IV belong to
 * \param[in] iv_key		key of the IV
 * \param[in] iv_ver		version of the IV
 * \param[in] iv_value		IV value buffer
 * \param[in] shortcut		the shortcut hints to optimize the propagation
 *				of accessing request, See \ref crt_iv_shortcut_t
 * \param[in] sync_type		synchronization type.
 *				If user wants to synchronize the update or
 *				notification to other nodes, it can select eager
 *				or lazy mode, and update or notification.
 *				See \ref crt_iv_sync_t.
 * \param[in] update_comp_cb	pointer to update completion callback
 * \param[in] cb_arg		pointer to argument passed to update_comp_cb
 *
 * \return			DER_SUCCESS on success, negative value if error
 */
int
crt_iv_update(crt_iv_namespace_t ivns, uint32_t class_id,
	      crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
	      d_sg_list_t *iv_value, crt_iv_shortcut_t shortcut,
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
 * \param[in] ivns		the local handle of the IV namespace
 * \param[in] class_id		IV class ID the IV belong to
 * \param[in] iv_key		key of the IV
 * \param[in] iv_ver		Version of the IV
 * \param[in] shortcut		the shotrcut hints to optimize the propagation
 *				of accessing request. See \ref crt_iv_shortcut_t
 * \param[in] sync_type		synchronization type
 *
 * \param[in] invali_comp_cb	pointer to invalidate completion callback
 * \param[in] cb_arg		pointer to argument passed to invali_comp_cb
 *
 * \return			DER_SUCCESS on success, negative value if error
 */
int
crt_iv_invalidate(crt_iv_namespace_t ivns, uint32_t class_id,
		crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
		crt_iv_shortcut_t shortcut, crt_iv_sync_t sync_type,
		crt_iv_comp_cb_t invali_comp_cb,
		  void *cb_arg);

/**
 * Query the topo info for the number of immediate children of the caller in IV
 * tree.
 *
 * \param[in] ivns		the local handle of the IV namespace
 * \param[in] class_id		IV class ID the IV belong to
 * \param[in] iv_key		key of the IV
 * \param[out] nchildren	number of children
 *
 * \return			DER_SUCCESS on success, negative value if error
 */
int
crt_iv_get_nchildren(crt_iv_namespace_t ivns, uint32_t class_id,
		     crt_iv_key_t *iv_key, uint32_t *nchildren);


/** @}
 */
#if defined(__cplusplus)
}
#endif

#endif /*  __CRT_IV_H__ */
