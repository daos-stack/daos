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
 * This file is part of daos_sr
 *
 * src/object/cli_obj.c
 */
#include <daos_event.h>
#include <daos_types.h>
#include "obj_rpc.h"
#include "obj_internal.h"

#define CLI_OBJ_IO_PARMS	8

struct obj_io_ctx;
typedef int (*obj_io_comp_t)(struct obj_io_ctx *iocx, int rc);

/** I/O context for DSR client object */
struct obj_io_ctx {
	/** reference of the object */
	struct dc_object	*cx_obj;
	/** event for the I/O context */
	struct daos_event	*cx_event;
	/** scheduler for the I/O context */
	struct daos_sched	*cx_sched;
	/** pointer to list anchor */
	void			*cx_args[CLI_OBJ_IO_PARMS];
	/** completion callback */
	obj_io_comp_t		 cx_comp;

	int			 cx_sync:1;
};

static struct dc_object *
obj_alloc(void)
{
	struct dc_object *obj;

	D_ALLOC_PTR(obj);
	if (obj == NULL)
		return NULL;

	obj->cob_ref = 1;
	return obj;
}

static void
obj_free(struct dc_object *obj)
{
	struct pl_obj_layout *layout;
	int		      i;

	layout = obj->cob_layout;
	if (layout == NULL)
		goto out;

	if (obj->cob_mohs != NULL) {
		for (i = 0; i < layout->ol_nr; i++) {
			if (!daos_handle_is_inval(obj->cob_mohs[i]))
				dc_obj_shard_close(obj->cob_mohs[i]);
		}
		D_FREE(obj->cob_mohs, layout->ol_nr * sizeof(*obj->cob_mohs));
	}

	pl_obj_layout_free(layout);
 out:
	D_FREE_PTR(obj);
}

static void
obj_decref(struct dc_object *obj)
{
	obj->cob_ref--;
	if (obj->cob_ref == 0)
		obj_free(obj);
}

static void
obj_addref(struct dc_object *obj)
{
	obj->cob_ref++;
}

static daos_handle_t
obj_ptr2hdl(struct dc_object *obj)
{
	daos_handle_t oh;

	oh.cookie = (uint64_t)obj;
	return oh;
}

static struct dc_object *
obj_hdl2ptr(daos_handle_t oh)
{
	struct dc_object *obj;

	obj = (struct dc_object *)oh.cookie;
	obj_addref(obj);
	return obj;
}

static void
obj_hdl_link(struct dc_object *obj)
{
	obj_addref(obj);
}

static void
obj_hdl_unlink(struct dc_object *obj)
{
	obj_decref(obj);
}

/**
 * Open an object shard (shard object), cache the open handle.
 */
static int
obj_shard_open(struct dc_object *obj, unsigned int shard, daos_handle_t *oh)
{
	struct pl_obj_layout	*layout;
	int			 rc = 0;

	layout = obj->cob_layout;

	/* Skip the invalid shards and targets */
	if (layout->ol_shards[shard] == -1 ||
	    layout->ol_targets[shard] == -1)
		return -DER_NONEXIST;

	/* XXX could be otherwise for some object classes? */
	D_ASSERT(layout->ol_shards[shard] == shard);

	D_DEBUG(DF_SRC, "Open object shard %d\n", shard);
	if (daos_handle_is_inval(obj->cob_mohs[shard])) {
		daos_unit_oid_t	 oid;

		memset(&oid, 0, sizeof(oid));
		oid.id_shard = shard;
		oid.id_pub   = obj->cob_md.omd_id;
		/* NB: obj open is a local operation, so it is ok to call
		 * it in sync mode, at least for now.
		 */
		rc = dc_obj_shard_open(obj->cob_coh,
				       layout->ol_targets[shard],
				       oid, obj->cob_mode,
				       &obj->cob_mohs[shard]);
	}

	if (rc == 0)
		*oh = obj->cob_mohs[shard];

	return rc;
}

static int
obj_iocx_comp(void *args, int rc)
{
	struct obj_io_ctx	*iocx = args;

	if (iocx->cx_comp)
		iocx->cx_comp(iocx, rc);

	if (iocx->cx_obj != NULL)
		obj_decref(iocx->cx_obj);

	D_FREE_PTR(iocx);
	return rc;
}

/** Initialise I/O context for a client object */
static int
obj_iocx_create(daos_handle_t oh, daos_event_t *event,
		struct obj_io_ctx **iocx_pp)
{
	struct obj_io_ctx	*iocx;
	int			rc;

	D_ALLOC_PTR(iocx);
	if (iocx == NULL)
		return -DER_NOMEM;

	iocx->cx_obj = obj_hdl2ptr(oh);
	if (iocx->cx_obj == NULL)
		D_GOTO(failed, rc = -DER_NO_HDL);

	iocx->cx_event = event;
	/* Create a daos scheduler for this IO */
	iocx->cx_sched = daos_ev2sched(event);
	if (iocx->cx_sched == NULL)
		D_GOTO(failed, rc = -DER_NOMEM);

	rc = daos_sched_init(iocx->cx_sched, event);
	if (rc != 0)
		D_GOTO(failed, rc);

	rc = daos_sched_register_comp_cb(iocx->cx_sched, obj_iocx_comp, iocx);
	if (rc != 0)
		D_GOTO(failed, rc);

	*iocx_pp = iocx;

	return 0;

failed:
	if (iocx->cx_sched != NULL)
		daos_sched_cancel(iocx->cx_sched, rc);

	obj_iocx_comp((void *)iocx, rc);
	daos_event_complete(event, rc);

	return rc;
}

static int
obj_iocx_launch(struct obj_io_ctx *iocx, bool *launched)
{
	struct daos_event *event = iocx->cx_event;
	int rc = 0;

	rc = daos_event_launch(event);
	if (rc != 0)
		return rc;

	*launched = true;
	daos_sched_run(iocx->cx_sched);

	return 0;
}

int
dc_obj_declare(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
	       daos_obj_attr_t *oa, daos_event_t *ev)
{
	struct daos_oclass_attr *oc_attr;
	int			 rc;

	/* XXX Only support internal classes for now */
	oc_attr = daos_oclass_attr_find(oid);
	rc = oc_attr != NULL ? 0 : -DER_INVAL;

	if (rc == 0 && ev != NULL) {
		daos_event_launch(ev);
		daos_event_complete(ev, 0);
	}
	return rc;
}

static int
obj_fetch_md(daos_obj_id_t oid, struct daos_obj_md *md, daos_event_t *ev)
{
	/* For predefined object classes, do nothing at here. But for those
	 * customized classes, we need to fetch for the remote OI table.
	 */
	memset(md, 0, sizeof(*md));
	md->omd_id = oid;
	return 0;
}

int
dc_obj_open(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
	    unsigned int mode, daos_handle_t *oh, daos_event_t *ev)
{
	struct dc_object	*obj;
	struct pl_obj_layout	*layout;
	struct pl_map		*map;
	int			 i;
	int			 nr;
	int			 rc;

	obj = obj_alloc();
	if (obj == NULL)
		return -DER_NOMEM;

	obj->cob_coh  = coh;
	obj->cob_mode = mode;

	/* it is a local operation for now, does not require event */
	rc = obj_fetch_md(oid, &obj->cob_md, NULL);
	if (rc != 0)
		D_GOTO(out, rc);

	map = pl_map_find(coh, oid);
	if (map == NULL) {
		D_DEBUG(DF_SRC, "Cannot find valid placement map\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = pl_obj_place(map, &obj->cob_md, NULL, &layout);
	if (rc != 0) {
		D_DEBUG(DF_SRC, "Failed to generate object layout\n");
		D_GOTO(out, rc);
	}
	D_DEBUG(DF_SRC, "Place object on %d targets\n", layout->ol_nr);

	obj->cob_layout = layout;
	nr = layout->ol_nr;

	D_ALLOC(obj->cob_mohs, nr * sizeof(*obj->cob_mohs));
	if (obj->cob_mohs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	for (i = 0; i < nr; i++)
		obj->cob_mohs[i] = DAOS_HDL_INVAL;

	obj_hdl_link(obj);
	*oh = obj_ptr2hdl(obj);
 out:
	obj_decref(obj);
	if (rc == 0 && ev != NULL) {
		daos_event_launch(ev);
		daos_event_complete(ev, 0);
	}
	return rc;
}

int
dc_obj_close(daos_handle_t oh, daos_event_t *ev)
{
	struct dc_object   *obj;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		return -DER_NO_HDL;

	obj_hdl_unlink(obj);
	obj_decref(obj);

	if (ev != NULL) {
		daos_event_launch(ev);
		daos_event_complete(ev, 0);
	}
	return 0;
}

int
dc_obj_punch(daos_handle_t oh, daos_epoch_t epoch, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
dc_obj_query(daos_handle_t oh, daos_epoch_t epoch, daos_obj_attr_t *oa,
	      daos_rank_list_t *ranks, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

static int
obj_get_grp_size(struct dc_object *obj)
{
	struct daos_oclass_attr *oc_attr;

	oc_attr = daos_oclass_attr_find(obj->cob_md.omd_id);
	D_ASSERT(oc_attr != NULL);
	return daos_oclass_grp_size(oc_attr);
}

static unsigned int
obj_dkey2shard(struct dc_object *obj, daos_key_t *dkey,
		   unsigned int *shards_cnt,
		   unsigned int *random_shard)
{
	int			grp_size;
	uint64_t		hash;
	uint64_t		start_shard;

	grp_size = obj_get_grp_size(obj);
	D_ASSERT(grp_size > 0);

	hash = daos_hash_murmur64((unsigned char *)dkey->iov_buf,
				  dkey->iov_len, 5731);

	D_ASSERT(obj->cob_layout->ol_nr >= grp_size);

	/* XXX, consistent hash? */
	start_shard = hash % (obj->cob_layout->ol_nr / grp_size);
	if (shards_cnt != NULL)
		*shards_cnt = grp_size;

	/* Return an random shard among the group for fetch. */
	/* XXX check if the target of the shard is avaible. */
	if (random_shard != NULL) {
		uint32_t idx = hash % grp_size + start_shard;
		int i;

		*random_shard = -1;
		for (i = 0; i < grp_size; i++) {
			if (obj->cob_layout->ol_shards[idx] != -1) {
				*random_shard = idx;
				break;
			}

			if (idx < start_shard + grp_size - 1)
				idx++;
			else
				idx = start_shard;
		}
		D_ASSERT(*random_shard != -1);
	}

	return (unsigned int)start_shard;
}

struct daos_obj_fetch_arg {
	daos_handle_t		 oh;
	daos_epoch_t		 epoch;
	daos_key_t		*dkey;
	unsigned int		 nr;
	daos_vec_iod_t		*iods;
	daos_sg_list_t		*sgls;
	daos_vec_map_t		*maps;
};

static int
obj_shard_fetch_task(struct daos_task *task)
{
	struct daos_obj_fetch_arg *arg = daos_task2arg(task);

	return dc_obj_shard_fetch(arg->oh, arg->epoch, arg->dkey,
				  arg->nr, arg->iods, arg->sgls,
				  arg->maps, task);
}

int
dc_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
	     unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls,
	     daos_vec_map_t *maps, daos_event_t *ev)
{
	struct obj_io_ctx	*iocx;
	unsigned int		shard;
	daos_handle_t		shard_oh;
	struct daos_task	*task = NULL;
	struct daos_obj_fetch_arg *arg;
	bool			launched = false;
	int			rc;

	rc = obj_iocx_create(oh, ev, &iocx);
	if (rc != 0)
		return rc;

	obj_dkey2shard(iocx->cx_obj, dkey, NULL, &shard);
	rc = obj_shard_open(iocx->cx_obj, shard, &shard_oh);
	if (rc != 0)
		D_GOTO(failed, rc);

	D_ALLOC_PTR(task);
	if (task == NULL)
		D_GOTO(failed, rc);

	/* Create a daos task and schedule it */
	rc = daos_task_init(task, obj_shard_fetch_task,
			    iocx->cx_sched);
	if (rc != 0) {
		D_FREE_PTR(task);
		D_GOTO(failed, rc);
	}

	arg = daos_task2arg(task);
	arg->oh		= shard_oh;
	arg->epoch	= epoch;
	arg->dkey	= dkey;
	arg->nr		= nr;
	arg->iods	= iods;
	arg->sgls	= sgls;
	arg->maps	= maps;

	rc = obj_iocx_launch(iocx, &launched);
	if (!launched)
		D_GOTO(failed, rc);

	return rc;

failed:
	if (iocx->cx_sched != NULL)
		daos_sched_cancel(iocx->cx_sched, rc);

	obj_iocx_comp((void *)iocx, rc);
	return rc;
}

struct daos_obj_update_arg {
	daos_handle_t oh;
	daos_epoch_t epoch;
	daos_key_t *dkey;
	unsigned int nr;
	daos_vec_iod_t *iods;
	daos_sg_list_t *sgls;
};

static int
obj_shard_update_task(struct daos_task *task)
{
	struct daos_obj_update_arg *arg = daos_task2arg(task);

	return dc_obj_shard_update(arg->oh, arg->epoch, arg->dkey,
				   arg->nr, arg->iods, arg->sgls, task);
}

int
daos_obj_update_callback(void *arg)
{
	/* Check if update succeeds, or it will need to update
	 * pool map
	 **/
	return 0;
}

int
dc_obj_update(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
	      unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls,
	      daos_event_t *ev)
{
	struct obj_io_ctx	*iocx;
	struct daos_task_group	*dtg;
	unsigned int		 shard;
	unsigned int		 shards_cnt;
	int			 i;
	int			 rc;
	bool			non_tasks = true;
	bool			launched = false;

	rc = obj_iocx_create(oh, ev, &iocx);
	if (rc != 0)
		return rc;

	shard = obj_dkey2shard(iocx->cx_obj, dkey, &shards_cnt, NULL);
	if (rc != 0)
		D_GOTO(failed, rc);

	D_DEBUG(DF_MISC, "update "DF_OID" start %u cnt %u\n",
		DP_OID(iocx->cx_obj->cob_md.omd_id), shard,
		shards_cnt);

	dtg = daos_sched_get_inline_dtg(iocx->cx_sched);
	daos_task_group_init(dtg, iocx->cx_sched,
			     daos_obj_update_callback, NULL);
	for (i = 0; i < shards_cnt; i++, shard++) {
		struct daos_task *task;
		struct daos_obj_update_arg *arg;
		daos_handle_t shard_oh;

		rc = obj_shard_open(iocx->cx_obj, shard, &shard_oh);
		if (rc != 0) {
			if (rc == -DER_NONEXIST) {
				rc = 0;
				continue;
			}
			D_GOTO(failed, rc);
		}

		D_ALLOC_PTR(task);
		if (task == NULL)
			D_GOTO(failed, rc);

		/* Create a daos task and schedule it */
		rc = daos_task_init(task, obj_shard_update_task,
				    iocx->cx_sched);
		if (rc != 0) {
			D_FREE_PTR(task);
			D_GOTO(failed, rc);
		}

		rc = daos_task_group_add(dtg, task);
		if (rc != 0)
			D_GOTO(failed, rc);

		arg = daos_task2arg(task);
		arg->oh		= shard_oh;
		arg->epoch	= epoch;
		arg->dkey	= dkey;
		arg->nr		= nr;
		arg->iods	= iods;
		arg->sgls	= sgls;
		non_tasks	= false;

	}

	if (non_tasks)
		D_GOTO(failed, rc = -DER_NONEXIST);

	rc = obj_iocx_launch(iocx, &launched);
	if (!launched)
		D_GOTO(failed, rc);

	return rc;
failed:
	if (iocx->cx_sched != NULL)
		daos_sched_cancel(iocx->cx_sched, rc);

	obj_iocx_comp((void *)iocx, rc);
	return rc;
}

static int
obj_list_dkey_comp(struct obj_io_ctx *ctx, int rc)
{
	struct dc_object	*obj = ctx->cx_obj;
	daos_hash_out_t		*anchor = ctx->cx_args[0];
	uint32_t		shard = (unsigned long)ctx->cx_args[1];
	int			grp_size;

	if (rc != 0)
		return rc;

	grp_size = obj_get_grp_size(obj);
	D_ASSERT(grp_size > 0);

	if (!daos_hash_is_eof(anchor)) {
		D_DEBUG(DF_SRC, "More keys in shard %d\n", shard);
		enum_anchor_set_shard(anchor, shard);
	} else if (shard < obj->cob_layout->ol_nr - grp_size) {
		shard += grp_size;
		D_DEBUG(DF_SRC, "Enumerate the next shard %d\n", shard);

		memset(anchor, 0, sizeof(*anchor));
		enum_anchor_set_shard(anchor, shard);
	} else {
		D_DEBUG(DF_SRC, "Enumerated All shards\n");
	}
	return rc;
}

static int
obj_list_akey_comp(struct obj_io_ctx *ctx, int rc)
{
	daos_hash_out_t *anchor = ctx->cx_args[0];

	if (rc != 0)
		return rc;

	if (daos_hash_is_eof(anchor))
		D_DEBUG(DF_SRC, "Enumerated All shards\n");

	return 0;
}

struct daos_obj_list_args {
	daos_handle_t	 oh;
	uint32_t	 op;
	daos_epoch_t	 epoch;
	daos_key_t	*key;
	uint32_t	*nr;
	daos_key_desc_t *kds;
	daos_sg_list_t	*sgl;
	daos_hash_out_t *anchor;
};

static int
obj_shard_list_task(struct daos_task *task)
{
	struct daos_obj_list_args *arg = daos_task2arg(task);

	return dc_obj_shard_list_key(arg->oh, arg->op, arg->epoch, arg->key,
				     arg->nr, arg->kds, arg->sgl, arg->anchor,
				     task);
}

static int
dc_obj_list_key(daos_handle_t oh, uint32_t op, daos_epoch_t epoch,
		daos_key_t *key, uint32_t *nr,
		daos_key_desc_t *kds, daos_sg_list_t *sgl,
		daos_hash_out_t *anchor, daos_event_t *ev)
{
	struct obj_io_ctx	*iocx;
	struct daos_obj_list_args *arg;
	struct daos_task	*task;
	daos_handle_t		shard_oh;
	uint32_t		shard;
	bool			launched = false;
	int			rc;

	if (nr == NULL || *nr == 0 || kds == NULL || sgl == NULL) {
		D_DEBUG(DF_SRC, "Invalid API parameter.\n");
		return -DER_INVAL;
	}

	rc = obj_iocx_create(oh, ev, &iocx);
	if (rc != 0)
		return rc;

	if (op == DAOS_OBJ_AKEY_RPC_ENUMERATE) {
		obj_dkey2shard(iocx->cx_obj, key, NULL, &shard);
		iocx->cx_comp = obj_list_akey_comp;
		enum_anchor_set_shard(anchor, shard);
	} else {
		shard = enum_anchor_get_shard(anchor);
		if (shard >= iocx->cx_obj->cob_layout->ol_nr) {
			D_ERROR("Invalid shard (%u) > layout_nr (%u)\n",
				shard, iocx->cx_obj->cob_layout->ol_nr);
			D_GOTO(failed, rc = -DER_INVAL);
		}
		iocx->cx_comp = obj_list_dkey_comp;
	}

	D_DEBUG(DF_SRC, "Enumerate keys in shard %d\n", shard);

	iocx->cx_args[0] = (void *)anchor;
	iocx->cx_args[1] = (void *)(unsigned long)shard;

	rc = obj_shard_open(iocx->cx_obj, shard, &shard_oh);
	if (rc != 0)
		D_GOTO(failed, rc);

	D_ALLOC_PTR(task);
	if (task == NULL)
		D_GOTO(failed, rc);

	/* Create a daos task and schedule it */
	rc = daos_task_init(task, obj_shard_list_task,
			    iocx->cx_sched);
	if (rc != 0)
		D_GOTO(failed, rc);

	arg = daos_task2arg(task);
	arg->oh		= shard_oh;
	arg->epoch	= epoch;
	arg->key	= key;
	arg->nr		= nr;
	arg->kds	= kds;
	arg->sgl	= sgl;
	arg->anchor	= anchor;
	arg->op		= op;

	rc = obj_iocx_launch(iocx, &launched);
	if (!launched)
		D_GOTO(failed, rc);

	return rc;

failed:
	if (iocx->cx_sched != NULL)
		daos_sched_cancel(iocx->cx_sched, rc);

	obj_iocx_comp((void *)iocx, rc);
	return rc;
}

int
dc_obj_list_dkey(daos_handle_t oh, daos_epoch_t epoch, uint32_t *nr,
		 daos_key_desc_t *kds, daos_sg_list_t *sgl,
		 daos_hash_out_t *anchor, daos_event_t *ev)
{
	/* XXX list_dkey might also input akey later */
	return dc_obj_list_key(oh, DAOS_OBJ_DKEY_RPC_ENUMERATE, epoch, NULL,
			       nr, kds, sgl, anchor, ev);
}

int
dc_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		 uint32_t *nr, daos_key_desc_t *kds, daos_sg_list_t *sgl,
		 daos_hash_out_t *anchor, daos_event_t *ev)
{
	return dc_obj_list_key(oh, DAOS_OBJ_AKEY_RPC_ENUMERATE, epoch, dkey,
			       nr, kds, sgl, anchor, ev);
}
