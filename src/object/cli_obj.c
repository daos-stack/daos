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
#include <daos/container.h>
#include "obj_rpc.h"
#include "obj_internal.h"

#define CLI_OBJ_IO_PARMS	8

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

daos_handle_t
dc_obj_hdl2cont_hdl(daos_handle_t oh)
{
	struct dc_object *obj;
	daos_handle_t hdl;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		return DAOS_HDL_INVAL;

	hdl = obj->cob_coh;
	obj_decref(obj);
	return hdl;
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

int
dc_obj_update_result(struct daos_task *task, void *arg)
{
	int *result = arg;
	int ret = task->dt_result;

	/* if result is 0, it means one shard already succeeds */
	if (*result == 0)
		return 0;

	/* If one shard succeeds, then it means I/O succeeds */
	if (ret == 0) {
		*result = 0;
		return 0;
	}

	/* if result is TIMEOUT or STALE, it means it might
	 * need retry in client
	 **/
	if (*result == -DER_STALE || *result == -DER_TIMEDOUT)
		return 0;

	*result = ret;

	return 0;
}

static int
dc_obj_task_cb(struct daos_task *task, void *arg)
{
	struct dc_object *obj = arg;
	int result = -1;

	daos_task_result_process(task, dc_obj_update_result, &result);

	if (result != -1)
		task->dt_result = result;

	obj_decref(obj);

	return 0;
}

int
dc_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
	     unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls,
	     daos_vec_map_t *maps, struct daos_task *task)
{
	struct dc_object	*obj;
	unsigned int		shard;
	daos_handle_t		shard_oh;
	int			rc;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		return -DER_NO_HDL;

	obj_dkey2shard(obj, dkey, NULL, &shard);
	rc = obj_shard_open(obj, shard, &shard_oh);
	if (rc != 0) {
		obj_decref(obj);
		return rc;
	}

	rc = daos_task_register_comp_cb(task, dc_obj_task_cb, obj);
	if (rc != 0) {
		obj_decref(obj);
		return rc;
	}

	rc = dc_obj_shard_fetch(shard_oh, epoch, dkey,
				nr, iods, sgls, maps, task);

	return rc;
}

int
dc_obj_update(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
	      unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls,
	      struct daos_task *task)
{
	struct daos_sched	*sched = daos_task2sched(task);
	struct dc_object	*obj;
	unsigned int		shard;
	unsigned int		shards_cnt;
	bool			non_update = true;
	int			i;
	int			rc = 0;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		return -DER_NO_HDL;

	shard = obj_dkey2shard(obj, dkey, &shards_cnt, NULL);
	D_DEBUG(DF_MISC, "update "DF_OID" start %u cnt %u\n",
		DP_OID(obj->cob_md.omd_id), shard, shards_cnt);

	rc = daos_task_register_comp_cb(task, dc_obj_task_cb, obj);
	if (rc != 0) {
		obj_decref(obj);
		return rc;
	}

	for (i = 0; i < shards_cnt; i++, shard++) {
		struct daos_task *shard_task;
		daos_handle_t shard_oh;

		rc = obj_shard_open(obj, shard, &shard_oh);
		if (rc != 0) {
			if (rc == -DER_NONEXIST) {
				rc = 0;
				continue;
			}
			break;
		}

		D_ALLOC_PTR(shard_task);
		if (shard_task == NULL)
			D_GOTO(out_put, rc = -DER_NOMEM);

		rc = daos_task_init(shard_task, NULL, NULL, 0, sched, NULL);
		if (rc != 0) {
			D_FREE_PTR(shard_task);
			D_GOTO(out_put, rc);
		}

		rc = daos_task_add_dependent(task, shard_task);
		if (rc != 0)
			D_GOTO(out_put, rc);

		rc = dc_obj_shard_update(shard_oh, epoch, dkey, nr, iods, sgls,
					 shard_task);
		if (rc != 0) {
			D_DEBUG(DF_MISC, "fails on i %d, continue try\n", i);
			continue;
		}
		non_update = false;
	}

	if (non_update)
		D_GOTO(out_put, rc = -DER_STALE);
out_put:
	return rc;
}

struct dc_obj_list_arg {
	struct dc_object *obj;
	daos_hash_out_t	 *anchor;
	uint32_t	 shard;
};

static int
dc_obj_list_dkey_cb(struct daos_task *task, void *data)
{
	struct dc_obj_list_arg	*arg = data;
	struct dc_object	*obj = arg->obj;
	daos_hash_out_t		*anchor = arg->anchor;
	uint32_t		shard = arg->shard;
	int			grp_size;
	int			rc = task->dt_result;

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
dc_obj_list_akey_cb(struct daos_task *task, void *data)
{
	struct dc_obj_list_arg *arg = data;
	daos_hash_out_t *anchor = arg->anchor;
	int rc = task->dt_result;

	if (rc != 0)
		return rc;

	if (daos_hash_is_eof(anchor))
		D_DEBUG(DF_SRC, "Enumerated All shards\n");

	return 0;
}

static int
dc_obj_list_key(daos_handle_t oh, uint32_t op, daos_epoch_t epoch,
		daos_key_t *key, uint32_t *nr,
		daos_key_desc_t *kds, daos_sg_list_t *sgl,
		daos_hash_out_t *anchor, struct daos_task *task)
{
	struct dc_object	*obj;
	struct dc_obj_list_arg	*arg;
	daos_handle_t		shard_oh;
	uint32_t		shard;
	int			rc;

	if (nr == NULL || *nr == 0 || kds == NULL || sgl == NULL) {
		D_DEBUG(DF_SRC, "Invalid API parameter.\n");
		return -DER_INVAL;
	}

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		return -DER_NO_HDL;

	arg = daos_task_buf_get(task, sizeof(*arg));
	arg->obj = obj;
	arg->anchor = anchor;
	if (op == DAOS_OBJ_AKEY_RPC_ENUMERATE) {
		obj_dkey2shard(obj, key, NULL, &shard);
		enum_anchor_set_shard(anchor, shard);
		arg->shard = shard;
		rc = daos_task_register_comp_cb(task, dc_obj_list_akey_cb,
						arg);
		if (rc != 0)
			D_GOTO(out_put, rc);
	} else {
		shard = enum_anchor_get_shard(anchor);
		if (shard >= obj->cob_layout->ol_nr) {
			D_ERROR("Invalid shard (%u) > layout_nr (%u)\n",
				shard, obj->cob_layout->ol_nr);
			D_GOTO(out_put, rc = -DER_INVAL);
		}
		rc = daos_task_register_comp_cb(task, dc_obj_list_dkey_cb,
						arg);
		if (rc != 0)
			D_GOTO(out_put, rc);
	}

	D_DEBUG(DF_SRC, "Enumerate keys in shard %d\n", shard);

	rc = obj_shard_open(obj, shard, &shard_oh);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = dc_obj_shard_list_key(shard_oh, op, epoch, key, nr,
				   kds, sgl, anchor, task);
out:
	return rc;

out_put:
	obj_decref(obj);
	return rc;
}

int
dc_obj_list_dkey(daos_handle_t oh, daos_epoch_t epoch, uint32_t *nr,
		 daos_key_desc_t *kds, daos_sg_list_t *sgl,
		 daos_hash_out_t *anchor, struct daos_task *task)
{
	/* XXX list_dkey might also input akey later */
	return dc_obj_list_key(oh, DAOS_OBJ_DKEY_RPC_ENUMERATE, epoch, NULL,
			       nr, kds, sgl, anchor, task);
}

int
dc_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		 uint32_t *nr, daos_key_desc_t *kds, daos_sg_list_t *sgl,
		 daos_hash_out_t *anchor, struct daos_task *task)
{
	return dc_obj_list_key(oh, DAOS_OBJ_AKEY_RPC_ENUMERATE, epoch, dkey,
			       nr, kds, sgl, anchor, task);
}
