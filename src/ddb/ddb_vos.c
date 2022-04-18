/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <string.h>
#include <libpmemobj/types.h>
#include <daos_srv/vos.h>
#include <gurt/debug.h>
#include "ddb_common.h"
#include "ddb_parse.h"
#include "ddb_vos.h"

#define ddb_vos_iterate(param, iter_type, recursive, anchors, cb, args) \
				vos_iterate(param, iter_type, recursive, \
						anchors, cb, NULL, args, NULL)

static int
vos_path_parse(const char *path, uuid_t pool_uuid,
	       char *pool_base, uint32_t pool_base_len)
{
	uint32_t	path_len = strlen(path);
	char		uuid_test_str[DAOS_UUID_STR_SIZE];
	int		path_idx, sub_path_idx;

	D_ASSERT(path != NULL && pool_base != NULL);

	for (path_idx = 0, sub_path_idx = 0; path_idx < path_len; path_idx++) {
		if (path[path_idx] == '/') {
			uuid_test_str[sub_path_idx] = '\0';
			if (uuid_parse(uuid_test_str, pool_uuid) == 0) {
				uint32_t src_pool_base_len = path_idx - sub_path_idx;

				if (src_pool_base_len > pool_base_len) {
					D_ERROR("The path's pool base is too long.\n");
					return -DER_INVAL;
				}

				strncpy(pool_base, path, src_pool_base_len);
				pool_base[src_pool_base_len + 1] = '\0';
				return 0;
			}
			/* start on the next dir */
			sub_path_idx = 0;
		} else {
			if (sub_path_idx <= DAOS_UUID_STR_SIZE)
				uuid_test_str[sub_path_idx++] = path[path_idx];
		}
	}

	return -DER_INVAL;
}

int
ddb_vos_pool_open(char *path, daos_handle_t *poh)
{
	char		pool_base[64];
	uuid_t		pool_uuid = {0};
	uint32_t	flags = 0; /* Will need to be a flag to ignore uuid check */
	int		rc;

	rc = vos_path_parse(path, pool_uuid, pool_base, ARRAY_SIZE(pool_base));
	if (!SUCCESS(rc))
		return rc;
	/*
	 * VOS files must be under /mnt/daos directory. This is a current limitation and
	 * will change in the future
	 */
	rc = vos_self_init(pool_base);
	if (!SUCCESS(rc))
		return rc;

	rc = vos_pool_open(path, pool_uuid, flags, poh);
	if (!SUCCESS(rc))
		vos_self_fini();

	return rc;
}

int
dv_cont_open(daos_handle_t poh, uuid_t uuid, daos_handle_t *coh)
{
	return vos_cont_open(poh, uuid, coh);
}

int
dv_cont_close(daos_handle_t *coh)
{
	int rc;

	D_ASSERT(coh);
	if (daos_handle_is_inval(*coh))
		return 0;

	rc = vos_cont_close(*coh);

	*coh = DAOS_HDL_INVAL;

	return rc;
}

int
ddb_vos_pool_close(daos_handle_t poh)
{
	int rc;

	rc = vos_pool_close(poh);
	vos_self_fini();

	return rc;
}

struct search_args {
	uint32_t	sa_idx;
	uint32_t	sa_current;
	uuid_t		sa_uuid;
	daos_unit_oid_t	sa_uoid;
	daos_key_t	sa_key;
	daos_recx_t	sa_recx;
};

static int
get_by_idx_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
	      vos_iter_param_t *param, void *cb_arg, unsigned int *acts)
{
	struct search_args *args = cb_arg;

	/* not found yet */
	if (args->sa_idx != args->sa_current) {
		args->sa_current++;
		return 0;
	}

	switch (type) {
	case VOS_ITER_COUUID:
		uuid_copy(args->sa_uuid, entry->ie_couuid);
		break;
	case VOS_ITER_OBJ:
		args->sa_uoid = entry->ie_oid;
		break;
	case VOS_ITER_DKEY:
		args->sa_key = entry->ie_key;
		break;
	case VOS_ITER_AKEY:
		args->sa_key = entry->ie_key;
		break;
	case VOS_ITER_SINGLE:
		break;
	case VOS_ITER_RECX:
		args->sa_recx = entry->ie_orig_recx;
		break;
	case VOS_ITER_DTX:
		break;
	case VOS_ITER_NONE:
		break;
	}

	return 1;
}

static int
get_by_idx(daos_handle_t hdl, uint32_t idx, struct search_args *args, daos_unit_oid_t *uoid,
	   daos_key_t *dkey, daos_key_t *akey, vos_iter_type_t type)
{
	vos_iter_param_t param = {0};
	struct vos_iter_anchors anchors = {0};
	int found;

	args->sa_idx = idx;

	param.ip_hdl = hdl;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	if (uoid)
		param.ip_oid = *uoid;
	if (dkey)
		param.ip_dkey = *dkey;
	if (akey)
		param.ip_akey = *akey;
	found = vos_iterate(&param, type, false, &anchors, get_by_idx_cb, NULL, args, NULL);

	if (!found)
		return -DER_INVAL;

	return 0;
}

int
dv_get_cont_uuid(daos_handle_t poh, uint32_t idx, uuid_t uuid)
{
	struct search_args args = {0};
	int rc;

	rc = get_by_idx(poh, idx, &args, NULL, NULL, NULL, VOS_ITER_COUUID);
	if (SUCCESS(rc))
		uuid_copy(uuid, args.sa_uuid);
	return rc;
}

int
dv_get_object_oid(daos_handle_t coh, uint32_t idx, daos_unit_oid_t *uoid)
{
	struct search_args args = {0};
	int rc;

	D_ASSERT(uoid != NULL);
	if (daos_handle_is_inval(coh))
		return -DER_INVAL;

	rc = get_by_idx(coh, idx, &args, NULL, NULL, NULL, VOS_ITER_OBJ);
	if (SUCCESS(rc))
		*uoid = args.sa_uoid;

	return rc;
}

int
dv_get_dkey(daos_handle_t coh, daos_unit_oid_t uoid, uint32_t idx, daos_key_t *dkey)
{
	struct search_args args = {0};
	int rc;

	D_ASSERT(dkey != NULL);
	if (daos_handle_is_inval(coh) || daos_unit_oid_is_null(uoid))
		return -DER_INVAL;

	rc = get_by_idx(coh, idx, &args, &uoid, NULL, NULL, VOS_ITER_DKEY);
	if (SUCCESS(rc))
		*dkey = args.sa_key;

	return rc;
}

int
dv_get_akey(daos_handle_t coh, daos_unit_oid_t uoid, daos_key_t *dkey, uint32_t idx,
	    daos_key_t *akey)
{
	struct search_args args = {0};
	int rc;

	D_ASSERT(dkey != NULL);
	D_ASSERT(akey != NULL);
	if (daos_handle_is_inval(coh) || daos_unit_oid_is_null(uoid))
		return -DER_INVAL;

	rc = get_by_idx(coh, idx, &args, &uoid, dkey, NULL, VOS_ITER_AKEY);
	if (SUCCESS(rc))
		*akey = args.sa_key;

	return rc;
}

int
dv_get_recx(daos_handle_t coh, daos_unit_oid_t uoid, daos_key_t *dkey, daos_key_t *akey,
	    uint32_t idx, daos_recx_t *recx)
{
	struct search_args args = {0};
	int rc;

	D_ASSERT(dkey != NULL);
	D_ASSERT(akey != NULL);
	D_ASSERT(recx != NULL);
	if (daos_handle_is_inval(coh) || daos_unit_oid_is_null(uoid))
		return -DER_INVAL;

	rc = get_by_idx(coh, idx, &args, &uoid, dkey, akey, VOS_ITER_RECX);
	if (SUCCESS(rc))
		*recx = args.sa_recx;

	return rc;
}

#define is_path_idx_set(idx) ((idx) != DDB_IDX_UNSET)

int
dv_path_update_from_indexes(struct dv_tree_path_builder *vt_path)
{
	daos_handle_t	poh = vt_path->vtp_poh;
	daos_handle_t	coh = {0};
	int		rc = 0;

	if (is_path_idx_set(vt_path->vtp_cont_idx))
		dv_get_cont_uuid(poh, vt_path->vtp_cont_idx,
				 vt_path->vtp_path.vtp_cont);

	if (is_path_idx_set(vt_path->vtp_oid_idx)) {
		daos_unit_oid_t uoid = {0};

		rc = dv_cont_open(poh, vt_path->vtp_path.vtp_cont, &coh);
		if (!SUCCESS(rc))
			return rc;
		rc = dv_get_object_oid(coh, vt_path->vtp_oid_idx, &uoid);
		if (!SUCCESS(rc))
			goto out;
		vt_path->vtp_path.vtp_oid = uoid;
	}

	if (is_path_idx_set(vt_path->vtp_dkey_idx)) {
		if (daos_handle_is_inval(coh)) {
			rc = dv_cont_open(poh, vt_path->vtp_path.vtp_cont, &coh);
			if (!SUCCESS(rc))
				return rc;
		}

		rc = dv_get_dkey(coh, vt_path->vtp_path.vtp_oid, vt_path->vtp_dkey_idx,
				 &vt_path->vtp_path.vtp_dkey);
		if (!SUCCESS(rc))
			goto out;
	}

	if (is_path_idx_set(vt_path->vtp_akey_idx)) {
		if (daos_handle_is_inval(coh)) {
			rc = dv_cont_open(poh, vt_path->vtp_path.vtp_cont, &coh);
			if (!SUCCESS(rc))
				return rc;
		}

		rc = dv_get_akey(coh, vt_path->vtp_path.vtp_oid,
				 &vt_path->vtp_path.vtp_dkey, vt_path->vtp_akey_idx,
				 &vt_path->vtp_path.vtp_akey);
		if (!SUCCESS(rc))
			goto out;

	}

	if (is_path_idx_set(vt_path->vtp_recx_idx)) {
		rc = dv_get_recx(coh, vt_path->vtp_path.vtp_oid,
				 &vt_path->vtp_path.vtp_dkey,
				 &vt_path->vtp_path.vtp_akey,
				 vt_path->vtp_recx_idx,
				 &vt_path->vtp_path.vtp_recx);
	}
out:
	dv_cont_close(&coh);

	return rc;
}

struct ddb_iter_ctx {
	daos_handle_t			 poh;
	struct vos_tree_handlers	*handlers;
	void				*handler_args;
	uuid_t				 current_cont;
	uint32_t			 cont_seen;
	daos_unit_oid_t			 current_obj;
	uint32_t			 obj_seen;
	daos_key_t			 current_dkey;
	uint32_t			 dkey_seen;
	daos_key_t			 current_akey;
	uint32_t			 akey_seen;
	uint32_t			 value_seen;
};

static int
handle_cont(struct ddb_iter_ctx *ctx, vos_iter_entry_t *entry, vos_iter_param_t *param)
{
	struct ddb_cont	cont = {0};

	D_ASSERT(ctx && ctx->handlers && ctx->handlers->ddb_cont_handler);

	uuid_copy(ctx->current_cont, entry->ie_couuid);
	uuid_copy(cont.ddbc_cont_uuid, entry->ie_couuid);
	cont.ddbc_idx = ctx->cont_seen++;

	/* Restart object count for container */
	ctx->obj_seen = 0;

	return ctx->handlers->ddb_cont_handler(&cont, ctx->handler_args);
}

static int
handle_obj(struct ddb_iter_ctx *ctx, vos_iter_entry_t *entry)
{
	struct ddb_obj obj = {0};

	D_ASSERT(ctx && ctx->handlers && ctx->handlers->ddb_obj_handler);
	obj.ddbo_idx = ctx->obj_seen++;
	obj.ddbo_oid = entry->ie_oid.id_pub;
	ctx->current_obj = entry->ie_oid;

	/* Restart dkey count for the object */
	ctx->dkey_seen = 0;

	return ctx->handlers->ddb_obj_handler(&obj, ctx->handler_args);
}

static int
handle_dkey(struct ddb_iter_ctx *ctx, vos_iter_entry_t *entry)
{
	struct ddb_key dkey = {0};

	D_ASSERT(ctx && ctx->handlers && ctx->handlers->ddb_dkey_handler);
	dkey.ddbk_idx = ctx->dkey_seen++;
	dkey.ddbk_key = entry->ie_key;
	ctx->current_dkey = entry->ie_key;

	/* Restart the akey count for the dkey */
	ctx->akey_seen = 0;

	return ctx->handlers->ddb_dkey_handler(&dkey, ctx->handler_args);
}

static int
handle_akey(struct ddb_iter_ctx *ctx, vos_iter_entry_t *entry)
{
	struct ddb_key akey = {0};

	D_ASSERT(ctx && ctx->handlers && ctx->handlers->ddb_akey_handler);
	akey.ddbk_idx = ctx->akey_seen++;
	akey.ddbk_key = entry->ie_key;
	ctx->current_akey = entry->ie_key;


	/* Restart the values seen for the akey */
	ctx->value_seen = 0;

	return ctx->handlers->ddb_akey_handler(&akey, ctx->handler_args);
}

static int
handle_sv(struct ddb_iter_ctx *ctx, vos_iter_entry_t *entry)
{
	struct ddb_sv value = {0};

	D_ASSERT(ctx && ctx->handlers && ctx->handlers->ddb_sv_handler);
	value.ddbs_record_size = entry->ie_rsize;
	value.ddbs_idx = ctx->value_seen++;


	return ctx->handlers->ddb_sv_handler(&value, ctx->handler_args);
}

static int
handle_array(struct ddb_iter_ctx *ctx, vos_iter_entry_t *entry)
{
	struct ddb_array value = {0};

	D_ASSERT(ctx && ctx->handlers && ctx->handlers->ddb_array_handler);
	value.ddba_record_size = entry->ie_rsize;
	value.ddba_recx = entry->ie_orig_recx;
	value.ddba_idx = ctx->value_seen++;

	return ctx->handlers->ddb_array_handler(&value, ctx->handler_args);
}

static int
handle_iter_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
	       vos_iter_param_t *param, void *cb_arg, unsigned int *acts)
{
	switch (type) {
	case VOS_ITER_COUUID:
		return handle_cont(cb_arg, entry, param);
	case VOS_ITER_OBJ:
		return handle_obj(cb_arg, entry);
	case VOS_ITER_DKEY:
		return handle_dkey(cb_arg, entry);
	case VOS_ITER_AKEY:
		return handle_akey(cb_arg, entry);
	case VOS_ITER_SINGLE:
		return handle_sv(cb_arg, entry);
	case VOS_ITER_RECX:
		return handle_array(cb_arg, entry);
	case VOS_ITER_DTX:
		printf("dtx\n");
		break;
	case VOS_ITER_NONE:
		break;
	}

	return 0;
}

static int
iter_cont_recurse_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
		     vos_iter_param_t *param, void *cb_arg, unsigned int *acts)
{
	vos_iter_param_t	 cont_param = {0};
	struct vos_iter_anchors	 anchors = {0};
	daos_handle_t		 coh;
	int			 rc;

	D_ASSERT(type == VOS_ITER_COUUID);

	rc = handle_cont(cb_arg, entry, param);
	if (!SUCCESS(rc))
		return rc;

	/* recursively iterate the objects in the container */
	rc = vos_cont_open(param->ip_hdl, entry->ie_couuid, &coh);
	if (!SUCCESS(rc))
		return rc;

	cont_param.ip_hdl = coh;
	cont_param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	rc = ddb_vos_iterate(&cont_param, VOS_ITER_OBJ, true, &anchors, handle_iter_cb, cb_arg);

	if (rc != 0)
		D_ERROR("vos_iterate error: "DF_RC"\n", DP_RC(rc));

	rc = vos_cont_close(coh);

	return rc;
}

static int
iter_cont_recurse(vos_iter_param_t *param, struct ddb_iter_ctx *ctx)
{
	struct vos_iter_anchors	anchors = {0};

	return ddb_vos_iterate(param, VOS_ITER_COUUID, false, &anchors, iter_cont_recurse_cb, ctx);
}

int
dv_iterate(daos_handle_t poh, struct dv_tree_path *path, bool recursive,
	   struct vos_tree_handlers *handlers, void *handler_args)
{
	vos_iter_param_t	param = {0};
	struct vos_iter_anchors	anchors = {0};
	int			rc;
	daos_handle_t		coh = DAOS_HDL_INVAL;
	vos_iter_type_t		type;
	struct ddb_iter_ctx	ctx = {0};

	ctx.handlers = handlers;
	ctx.handler_args = handler_args;
	ctx.poh = poh;

	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;

	if (uuid_is_null(path->vtp_cont)) {
		param.ip_hdl = poh;

		if (recursive)
			/*
			 * currently vos_iterate doesn't handle recursive iteration starting with a
			 * container. This works around that limitation.
			 */
			return iter_cont_recurse(&param, &ctx);
		return ddb_vos_iterate(&param, VOS_ITER_COUUID, false, &anchors,
				       handle_iter_cb, &ctx);
	}

	rc = vos_cont_open(poh, path->vtp_cont, &coh);
	if (!SUCCESS(rc))
		return rc;

	param.ip_hdl = coh;

	param.ip_oid = path->vtp_oid;
	param.ip_dkey = path->vtp_dkey;
	param.ip_akey = path->vtp_akey;

	if (daos_oid_is_null(path->vtp_oid.id_pub)) {
		type = VOS_ITER_OBJ;
	} else if (path->vtp_dkey.iov_len == 0) {
		type = VOS_ITER_DKEY;
	} else if (path->vtp_akey.iov_len == 0) {
		type = VOS_ITER_AKEY;
	} else {
		/* Don't know if the akey value is sv or array so just
		 * try to iterate both. Doesn't seem to have any negative consequences for
		 * trying to iterate what the value is not.
		 */
		rc = ddb_vos_iterate(&param, VOS_ITER_RECX, recursive, &anchors,
				     handle_iter_cb, &ctx);
		if (!SUCCESS(rc)) {
			vos_cont_close(coh);

			return rc;
		}
		rc = ddb_vos_iterate(&param, VOS_ITER_SINGLE, recursive, &anchors,
				     handle_iter_cb, &ctx);
		vos_cont_close(coh);

		return rc;
	}

	rc = ddb_vos_iterate(&param, type, recursive, &anchors, handle_iter_cb, &ctx);

	if (!daos_handle_is_inval(coh))
		vos_cont_close(coh);

	return rc;
}
