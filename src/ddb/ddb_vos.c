/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <string.h>
#include <libpmemobj/types.h>
#include <daos_srv/vos.h>
#include <gurt/debug.h>
#include <vos_internal.h>
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
		return -DER_NONEXIST;

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

static void
get_object_type(enum daos_otype_t type, char *type_str)
{
	switch (type) {
	case DAOS_OT_MULTI_HASHED:
		strcpy(type_str, "DAOS_OT_MULTI_HASHED");
		break;
	case DAOS_OT_OIT:
		strcpy(type_str, "DAOS_OT_OIT");
		break;
	case DAOS_OT_DKEY_UINT64:
		strcpy(type_str, "DAOS_OT_DKEY_UINT64");
		break;
	case DAOS_OT_AKEY_UINT64:
		strcpy(type_str, "DAOS_OT_AKEY_UINT64");
		break;
	case DAOS_OT_MULTI_UINT64:
		strcpy(type_str, "DAOS_OT_MULTI_UINT64");
		break;
	case DAOS_OT_DKEY_LEXICAL:
		strcpy(type_str, "DAOS_OT_DKEY_LEXICAL");
		break;
	case DAOS_OT_AKEY_LEXICAL:
		strcpy(type_str, "DAOS_OT_AKEY_LEXICAL");
		break;
	case DAOS_OT_MULTI_LEXICAL:
		strcpy(type_str, "DAOS_OT_MULTI_LEXICAL");
		break;
	case DAOS_OT_KV_HASHED:
		strcpy(type_str, "DAOS_OT_KV_HASHED");
		break;
	case DAOS_OT_KV_UINT64:
		strcpy(type_str, "DAOS_OT_KV_UINT64");
		break;
	case DAOS_OT_KV_LEXICAL:
		strcpy(type_str, "DAOS_OT_KV_LEXICAL");
		break;
	case DAOS_OT_ARRAY:
		strcpy(type_str, "DAOS_OT_ARRAY");
		break;
	case DAOS_OT_ARRAY_ATTR:
		strcpy(type_str, "DAOS_OT_ARRAY_ATTR");
		break;
	case DAOS_OT_ARRAY_BYTE:
		strcpy(type_str, "DAOS_OT_ARRAY_BYTE");
		break;
	default:
		strcpy(type_str, "UNKNOWN");
		break;
	}
}

void
dv_oid_to_obj(daos_obj_id_t oid, struct ddb_obj *obj)
{
	obj->ddbo_oid = oid;
	obj->ddbo_nr_grps = (oid.hi & OID_FMT_META_MASK) >> OID_FMT_META_SHIFT;

	/*
	 * It would be nice to get the object class name, but currently that is client
	 * functionality and this tool is being installed as a server binary. If that changes, the
	 * following code might be used ...
	 * char			 obj_class_name[32];
	 * int rc = obj_class_init();
	 * daos_oclass_id_t	 oclass;
	 * oclass = daos_obj_id2class(obj->ddbo_oid);
	 * if (!SUCCESS(rc))
	 *	return rc;
	 * daos_oclass_id2name(oclass, obj_class_name);
	 * obj_class_fini();
	*/

	obj->ddbo_otype = daos_obj_id2type(oid);
	get_object_type(obj->ddbo_otype, obj->ddbo_otype_str);
}

static int
handle_obj(struct ddb_iter_ctx *ctx, vos_iter_entry_t *entry)
{
	struct ddb_obj		obj = {0};

	D_ASSERT(ctx && ctx->handlers && ctx->handlers->ddb_obj_handler);

	dv_oid_to_obj(entry->ie_oid.id_pub, &obj);

	obj.ddbo_idx = ctx->obj_seen++;

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

int
dv_superblock(daos_handle_t poh, dv_dump_superblock_cb cb, void *cb_args)
{
	struct ddb_superblock	 sb = {0};
	struct vos_pool		*pool;
	struct vos_pool_df	*pool_df;

	D_ASSERT(cb);

	pool = vos_hdl2pool(poh);

	if (pool == NULL)
		return -DER_INVAL;

	pool_df = pool->vp_pool_df;

	if (pool_df == NULL || pool_df->pd_magic != POOL_DF_MAGIC)
		return -DER_DF_INVAL;

	uuid_copy(sb.dsb_id, pool_df->pd_id);
	sb.dsb_durable_format_version = pool_df->pd_version;
	sb.dsb_cont_nr = pool_df->pd_cont_nr;
	sb.dsb_nvme_sz = pool_df->pd_nvme_sz;
	sb.dsb_scm_sz = pool_df->pd_scm_sz;

	sb.dsb_blk_sz = pool_df->pd_vea_df.vsd_blk_sz;
	sb.dsb_hdr_blks = pool_df->pd_vea_df.vsd_hdr_blks;
	sb.dsb_tot_blks = pool_df->pd_vea_df.vsd_tot_blks;


	cb(cb_args, &sb);

	return 0;
}

int
dv_dump_value(daos_handle_t poh, struct dv_tree_path *path, dv_dump_value_cb dump_cb, void *cb_arg)
{
	daos_iod_t	iod = {0};
	d_sg_list_t	sgl;
	daos_handle_t	coh;
	size_t		data_size;
	int		rc;

	d_sgl_init(&sgl, 1);

	rc = vos_cont_open(poh, path->vtp_cont, &coh);
	if (!SUCCESS(rc))
		return rc;

	iod.iod_name = path->vtp_akey;
	iod.iod_recxs = &path->vtp_recx;
	iod.iod_nr = 1;
	iod.iod_size = 0;
	iod.iod_type = path->vtp_recx.rx_nr == 0 ? DAOS_IOD_SINGLE : DAOS_IOD_ARRAY;

	/* First, get record size */
	rc = vos_obj_fetch(coh, path->vtp_oid, DAOS_EPOCH_MAX, 0, &path->vtp_dkey, 1, &iod, NULL);
	if (!SUCCESS(rc)) {
		d_sgl_fini(&sgl, true);
		vos_cont_close(coh);

		return rc;
	}

	data_size = iod.iod_size;

	if (path->vtp_recx.rx_nr > 0)
		data_size *= path->vtp_recx.rx_nr;

	D_ALLOC(sgl.sg_iovs[0].iov_buf, data_size);
	if (sgl.sg_iovs[0].iov_buf == NULL)
		return -DER_NOMEM;
	sgl.sg_iovs[0].iov_buf_len = data_size;

	rc = vos_obj_fetch(coh, path->vtp_oid, DAOS_EPOCH_MAX, 0, &path->vtp_dkey, 1, &iod, &sgl);
	if (!SUCCESS(rc)) {
		D_ERROR("Unable to fetch object: "DF_RC"\n", DP_RC(rc));
		d_sgl_fini(&sgl, true);
		vos_cont_close(coh);

		return rc;
	}

	if (dump_cb)
		rc = dump_cb(cb_arg, &sgl.sg_iovs[0]);

	d_sgl_fini(&sgl, true);
	vos_cont_close(coh);

	return rc;
}

static void
ilog_entry_status(enum ilog_status status, char *status_str, uint32_t status_str_len)
{
	switch (status) {

	case ILOG_INVALID:
		snprintf(status_str, status_str_len, "INVALID");
		break;
	case ILOG_COMMITTED:
		snprintf(status_str, status_str_len, "COMMITTED");
		break;
	case ILOG_UNCOMMITTED:
		snprintf(status_str, status_str_len, "UNCOMMITTED");
		break;
	case ILOG_REMOVED:
		snprintf(status_str, status_str_len, "REMOVED");
		break;
	}
}


static int
cb_foreach_entry(dv_dump_ilog_entry cb, void *cb_args, struct ilog_entries *entries)
{
	struct ilog_entry	 e;
	struct ddb_ilog_entry	 ent = {0};
	int			 rc;

	ilog_foreach_entry(entries, &e) {
		ent.die_idx = e.ie_idx;
		ent.die_status = e.ie_status;
		ilog_entry_status(e.ie_status, ent.die_status_str, ARRAY_SIZE(ent.die_status_str));
		ent.die_epoch = e.ie_id.id_epoch;
		ent.die_tx_id = e.ie_id.id_tx_id;
		ent.die_update_minor_eph = e.ie_id.id_update_minor_eph;
		ent.die_punch_minor_eph = e.ie_id.id_punch_minor_eph;

		rc = cb(cb_args, &ent);
		if (!SUCCESS(rc))
			return rc;
	}

	return 0;
}

int
dv_get_obj_ilog_entries(daos_handle_t coh, daos_unit_oid_t oid, dv_dump_ilog_entry cb,
			void *cb_args)
{
	struct ilog_entries	 entries = {0};
	struct ilog_desc_cbs	 cbs = {0};
	struct vos_container	*cont = NULL;
	struct vos_obj_df	*obj_df = NULL;
	struct umem_instance	*umm;
	int			 rc;

	D_ASSERT(cb);
	if (daos_handle_is_inval(coh) || daos_unit_oid_is_null(oid))
		return -DER_INVAL;

	ilog_fetch_init(&entries);
	cont = vos_hdl2cont(coh);

	rc = vos_oi_find(cont, oid, &obj_df, NULL);
	if (!SUCCESS(rc)) {
		if (rc == -DER_NONEXIST)
			return -DER_INVAL;
		return rc;
	}

	umm = vos_cont2umm(cont);

	vos_ilog_desc_cbs_init(&cbs, coh);
	rc = ilog_fetch(umm, &obj_df->vo_ilog, &cbs, DAOS_INTENT_DEFAULT, &entries);
	if (!SUCCESS(rc))
		return rc;

	rc = cb_foreach_entry(cb, cb_args, &entries);
	return rc;
}

static inline int
ddb_key_iter_fetch_helper(struct vos_obj_iter *oiter, struct vos_rec_bundle *rbund, d_iov_t *keybuf)
{
	d_iov_t			 kiov;
	d_iov_t			 riov;
	struct dcs_csum_info	 csum;

	tree_rec_bundle2iov(rbund, &riov);

	rbund->rb_iov	= keybuf;
	rbund->rb_csum	= &csum;

	d_iov_set(rbund->rb_iov, NULL, 0); /* no copy */
	ci_set_null(rbund->rb_csum);

	return dbtree_iter_fetch(oiter->it_hdl, &kiov, &riov, NULL);
}

struct ilog_cb_args {
	daos_key_t *key;
	dv_dump_ilog_entry cb;
	void *cb_args;
};

static int
dkey_ilog_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
	     vos_iter_param_t *param, void *cb_arg, unsigned int *acts)
{
	struct vos_iterator	*iter = vos_hdl2iter(ih);
	struct vos_obj_iter	*oiter = vos_iter2oiter(iter);
	struct umem_instance	*umm;
	struct vos_rec_bundle	 rbund;
	daos_key_t		 key;
	struct ilog_cb_args	*args = cb_arg;
	struct vos_krec_df	*krec;
	int			 rc;
	struct ilog_desc_cbs	 cbs = {0};
	daos_handle_t		 coh = param->ip_hdl;
	struct ilog_entries	 entries = {0};

	D_ASSERT(type == VOS_ITER_DKEY);
	if (!daos_key_match(&entry->ie_key, args->key))
		return 0;

	ilog_fetch_init(&entries);

	rc = ddb_key_iter_fetch_helper(oiter, &rbund, &key);
	if (!SUCCESS(rc))
		return rc;

	krec = rbund.rb_krec;
	umm = vos_obj2umm(oiter->it_obj);

	vos_ilog_desc_cbs_init(&cbs, coh);

	rc = ilog_fetch(umm, &krec->kr_ilog, &cbs, DAOS_INTENT_DEFAULT, &entries);
	if (!SUCCESS(rc))
		return rc;

	rc = cb_foreach_entry(args->cb, args->cb_args, &entries);

	return rc;
}

int
dv_get_dkey_ilog_entries(daos_handle_t coh, daos_unit_oid_t oid, daos_key_t *dkey,
			 dv_dump_ilog_entry cb, void *cb_args)
{
	vos_iter_param_t	param = {0};
	struct vos_iter_anchors anchors = {0};
	struct ilog_cb_args	args = {0};

	D_ASSERT(cb);

	if (daos_handle_is_inval(coh) || daos_unit_oid_is_null(oid) ||
	    dkey == NULL || dkey->iov_len == 0)
		return -DER_INVAL;

	param.ip_hdl = coh;
	param.ip_oid = oid;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	args.key = dkey;
	args.cb = cb;
	args.cb_args = cb_args;

	return ddb_vos_iterate(&param, VOS_ITER_DKEY, false, &anchors, dkey_ilog_cb, &args);
}

struct committed_dtx_cb_arg {
	dv_committed_dtx_handler handler;
	void *handler_arg;
};

struct active_dtx_cb_arg {
	dv_active_dtx_handler handler;
	void *handler_arg;
};

static int
committed_dtx_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *cb_arg)
{
	int rc;
	struct committed_dtx_cb_arg *arg = cb_arg;
	struct dv_dtx_committed_entry entry;

	struct vos_dtx_cmt_ent *ent = val->iov_buf;

	uuid_copy(entry.ddtx_uuid, ent->dce_base.dce_xid.dti_uuid);
	entry.ddtx_reindex = ent->dce_reindex;
	entry.ddtx_exist = ent->dce_exist;
	entry.ddtx_invalid = ent->dce_invalid;
	entry.ddtx_cmt_time = ent->dce_base.dce_cmt_time;
	entry.ddtx_epoch = ent->dce_base.dce_epoch;



	rc = arg->handler(&entry, arg->handler_arg);

	return rc;
}

static int
active_dtx_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *cb_arg)
{
	struct dv_dtx_active_entry	 entry = {0};
	struct active_dtx_cb_arg	*arg = cb_arg;
	struct vos_dtx_act_ent		*ent = val->iov_buf;
	int				 rc;

	uuid_copy(entry.ddtx_uuid, ent->dae_base.dae_xid.dti_uuid);
	entry.ddtx_oid_cnt = ent->dae_oid_cnt;
	entry.ddtx_start_time = ent->dae_start_time;
	entry.ddtx_committable = ent->dae_committable;
	entry.ddtx_committed = ent->dae_committed;
	entry.ddtx_aborted = ent->dae_aborted;
	entry.ddtx_maybe_shared = ent->dae_maybe_shared;
	entry.ddtx_prepared = ent->dae_prepared;
	entry.ddtx_epoch = ent->dae_base.dae_epoch;
	entry.ddtx_grp_cnt = ent->dae_base.dae_grp_cnt;
	entry.ddtx_ver = ent->dae_base.dae_ver;
	entry.ddtx_rec_cnt = ent->dae_base.dae_rec_cnt;
	entry.ddtx_mbs_flags = ent->dae_base.dae_mbs_flags;
	entry.ddtx_flags = ent->dae_base.dae_flags;
	entry.ddtx_oid = ent->dae_base.dae_oid;

	rc = arg->handler(&entry, arg->handler_arg);

	return rc;
}

int
dv_committed_dtx(daos_handle_t coh, dv_committed_dtx_handler handler_cb, void *handler_arg)
{
	struct vos_container		*cont;
	int				 rc;
	struct committed_dtx_cb_arg	 cb_arg = {0};

	if (daos_handle_is_inval(coh))
		return -DER_INVAL;

	cb_arg.handler = handler_cb;
	cb_arg.handler_arg = handler_arg;

	cont = vos_hdl2cont(coh);
	rc = dbtree_iterate(cont->vc_dtx_committed_hdl, DAOS_INTENT_DEFAULT, false,
			    committed_dtx_cb, &cb_arg);
	return rc;
}

int
dv_active_dtx(daos_handle_t coh, dv_active_dtx_handler handler_cb, void *handler_arg)
{
	struct vos_container	*cont;
	int			 rc;
	struct active_dtx_cb_arg cb_arg = {0};

	if (daos_handle_is_inval(coh))
		return -DER_INVAL;

	cb_arg.handler = handler_cb;
	cb_arg.handler_arg = handler_arg;

	cont = vos_hdl2cont(coh);


	rc = dbtree_iterate(cont->vc_dtx_active_hdl, DAOS_INTENT_DEFAULT, false,
			    active_dtx_cb, &cb_arg);

	return rc;
}

int
dv_delete(daos_handle_t poh, struct dv_tree_path *vtp)
{
	daos_handle_t	coh;
	int		rc;

	/* Don't allow deleting all contents ... must specify at least a container */
	if (dvp_is_empty(vtp))
		return -DER_INVAL;

	if (!SUCCESS(ddb_vtp_verify(poh, vtp)))
		return -DER_NONEXIST;

	if (!dv_has_obj(vtp))
		return vos_cont_destroy(poh, vtp->vtp_cont);

	rc = dv_cont_open(poh, vtp->vtp_cont, &coh);
	if (!SUCCESS(rc))
		return rc;

	if (dv_has_akey(vtp))
		rc = vos_obj_del_key(coh, vtp->vtp_oid, &vtp->vtp_dkey, &vtp->vtp_akey);
	else if (dv_has_dkey(vtp))
		rc = vos_obj_del_key(coh, vtp->vtp_oid, &vtp->vtp_dkey, NULL);
	else /* delete object */
		rc = vos_obj_delete(coh, vtp->vtp_oid);

	dv_cont_close(&coh);

	return rc;
}

int dv_update(daos_handle_t poh, struct dv_tree_path *vtp, d_iov_t *iov, daos_epoch_t epoch)
{
	daos_iod_t	iod = {0};
	d_sg_list_t	sgl = {0};
	uint64_t	flags = 0;
	daos_handle_t	coh;
	uint32_t	pool_ver = 0;
	int		rc;

	if (!dvp_is_complete(vtp) || iov->iov_len == 0)
		return -DER_INVAL;

	rc = dv_cont_open(poh, vtp->vtp_cont, &coh);
	if (!SUCCESS(rc))
		return rc;

	d_sgl_init(&sgl, 1);
	sgl.sg_nr_out = 1;
	sgl.sg_iovs[0] = *iov;

	iod.iod_name = vtp->vtp_akey;
	iod.iod_nr = 1;
	if (vtp->vtp_recx.rx_nr == 0) {
		iod.iod_type = DAOS_IOD_SINGLE;
		iod.iod_size = iov->iov_len;
	} else {
		iod.iod_type = DAOS_IOD_ARRAY;
		iod.iod_recxs = &vtp->vtp_recx;
		iod.iod_size = 1;
	}

	rc = vos_obj_update(coh, vtp->vtp_oid, epoch, pool_ver, flags,
			    &vtp->vtp_dkey, 1, &iod, NULL, &sgl);
	if (rc == -DER_NO_PERM)
		D_ERROR("Unable to update. Trying to update with the wrong value type? "
			"(Array vs SV)\n");
	if (rc == -DER_REC2BIG)
		D_ERROR("Unable to update. Data value might not be large enough to fill the "
			"supplied recx\n");
	d_sgl_fini(&sgl, false);
	dv_cont_close(&coh);

	return rc;
}

static bool
daos_recx_match(daos_recx_t a, daos_recx_t b)
{
	return a.rx_nr == b.rx_nr && a.rx_idx == b.rx_idx;
}

static int
find_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type, vos_iter_param_t *param,
	void *cb_arg, unsigned int *acts)
{
	struct dv_tree_path *path = cb_arg;

	switch (type) {

	case VOS_ITER_NONE:
		break;
	case VOS_ITER_COUUID:
		break;
	case VOS_ITER_OBJ:
		if (daos_oid_cmp(path->vtp_oid.id_pub, entry->ie_oid.id_pub) == 0)
			return 1;
		break;
	case VOS_ITER_DKEY:
		if (daos_key_match(&path->vtp_dkey, &entry->ie_key))
			return 1;
		break;
	case VOS_ITER_AKEY:
		if (daos_key_match(&path->vtp_akey, &entry->ie_key))
			return 1;
		break;
	case VOS_ITER_SINGLE:
		break;
	case VOS_ITER_RECX:
		if (daos_recx_match(path->vtp_recx, entry->ie_orig_recx))
			return 1;
		break;
	case VOS_ITER_DTX:
		break;
	}
	return 0;
}

/* Note:
 * This can be improved by verifying the path in a single vos_iterate ... instead of 1 for
 * path part.
 */
static bool
part_is_valid(daos_handle_t coh, struct dv_tree_path *path, vos_iter_type_t type)
{
	vos_iter_param_t param = {0};
	struct vos_iter_anchors anchors = {0};

	param.ip_hdl = coh;
	param.ip_oid = path->vtp_oid;
	param.ip_dkey = path->vtp_dkey;
	if (type == VOS_ITER_RECX)
		param.ip_akey = path->vtp_akey;

	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;

	return vos_iterate(&param, type, false, &anchors, find_cb, NULL, path, NULL) == 1;
}

int
ddb_vtp_verify(daos_handle_t poh, struct dv_tree_path *vtp)
{
	daos_handle_t coh;
	int rc = 0;

	if (uuid_is_null(vtp->vtp_cont)) /* empty path is fine */
		return 0;

	rc = dv_cont_open(poh, vtp->vtp_cont, &coh);
	if (!SUCCESS(rc))
		return rc;

	if (!daos_oid_is_null(vtp->vtp_oid.id_pub) && !part_is_valid(coh, vtp, VOS_ITER_OBJ))
		D_GOTO(done, rc = -DER_NONEXIST);

	if (vtp->vtp_dkey.iov_len > 0 && !part_is_valid(coh, vtp, VOS_ITER_DKEY))
		D_GOTO(done, rc = -DER_NONEXIST);

	if (vtp->vtp_akey.iov_len > 0 && !part_is_valid(coh, vtp, VOS_ITER_AKEY))
		D_GOTO(done, rc = -DER_NONEXIST);

	if (vtp->vtp_recx.rx_nr > 0 && !part_is_valid(coh, vtp, VOS_ITER_RECX))
		D_GOTO(done, rc = -DER_NONEXIST);

done:
	dv_cont_close(&coh);

	return rc;
}
