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
#define D_LOGFAC	DD_FAC(object)

#include <daos/object.h>
#include <daos/container.h>
#include <daos/pool.h>
#include <daos_task.h>
#include <daos_types.h>
#include "obj_rpc.h"
#include "obj_internal.h"

#define CLI_OBJ_IO_PARMS	8

/**
 * Open an object shard (shard object), cache the open handle.
 */
static int
obj_shard_open(struct dc_object *obj, unsigned int shard, unsigned int map_ver,
	       struct dc_obj_shard **shard_ptr)
{
	struct pl_obj_layout	*layout;
	struct dc_obj_shard	*obj_shard;
	bool			 lock_upgraded = false;
	int			 rc = 0;

	D_RWLOCK_RDLOCK(&obj->cob_lock);
open_retry:
	layout = obj->cob_layout;
	if (layout->ol_ver != map_ver) {
		D_RWLOCK_UNLOCK(&obj->cob_lock);
		return -DER_STALE;
	}

	/* Skip the invalid shards and targets */
	if (layout->ol_shards[shard].po_shard == -1 ||
	    layout->ol_shards[shard].po_target == -1) {
		D_RWLOCK_UNLOCK(&obj->cob_lock);
		return -DER_NONEXIST;
	}

	/* XXX could be otherwise for some object classes? */
	D_ASSERT(layout->ol_shards[shard].po_shard == shard);

	D_DEBUG(DB_IO, "Open object shard %d\n", shard);
	obj_shard = obj->cob_obj_shards[shard];
	if (obj_shard == NULL) {
		daos_unit_oid_t	 oid;

		/* upgrade to write lock to safely update open shard cache */
		if (!lock_upgraded) {
			D_RWLOCK_UNLOCK(&obj->cob_lock);
			D_RWLOCK_WRLOCK(&obj->cob_lock);
			lock_upgraded = true;
			goto open_retry;
		}

		memset(&oid, 0, sizeof(oid));
		oid.id_shard = shard;
		oid.id_pub   = obj->cob_md.omd_id;
		/* NB: obj open is a local operation, so it is ok to call
		 * it in sync mode, at least for now.
		 */
		rc = dc_obj_shard_open(obj, layout->ol_shards[shard].po_target,
				       oid, obj->cob_mode, &obj_shard);
		if (rc == 0) {
			D_ASSERT(obj_shard != NULL);
			obj->cob_obj_shards[shard] = obj_shard;
		}
	}

	if (rc == 0) {
		/* hold the object shard */
		obj_shard_addref(obj_shard);
		*shard_ptr = obj_shard;
	}

	D_RWLOCK_UNLOCK(&obj->cob_lock);

	return rc;
}

#define obj_shard_close(shard)	dc_obj_shard_close(shard)

static void
obj_layout_free(struct dc_object *obj)
{
	struct pl_obj_layout *layout;
	int		     i;

	layout = obj->cob_layout;
	if (layout == NULL)
		return;

	if (obj->cob_obj_shards != NULL) {
		for (i = 0; i < layout->ol_nr; i++) {
			if (obj->cob_obj_shards[i] != NULL)
				obj_shard_close(obj->cob_obj_shards[i]);
		}
		D_FREE(obj->cob_obj_shards);
		obj->cob_obj_shards = NULL;
	}

	pl_obj_layout_free(layout);
	obj->cob_layout = NULL;
}

static void
obj_free(struct d_hlink *hlink)
{
	struct dc_object *obj;

	obj = container_of(hlink, struct dc_object, cob_hlink);
	D_ASSERT(daos_hhash_link_empty(&obj->cob_hlink));
	obj_layout_free(obj);
	D_SPIN_DESTROY(&obj->cob_spin);
	D_RWLOCK_DESTROY(&obj->cob_lock);
	D_FREE_PTR(obj);
}

static struct d_hlink_ops obj_h_ops = {
	.hop_free	= obj_free,
};

static struct dc_object *
obj_alloc(void)
{
	struct dc_object *obj;

	D_ALLOC_PTR(obj);
	if (obj == NULL)
		return NULL;

	daos_hhash_hlink_init(&obj->cob_hlink, &obj_h_ops);
	return obj;
}

void
obj_decref(struct dc_object *obj)
{
	daos_hhash_link_putref(&obj->cob_hlink);
}

void
obj_addref(struct dc_object *obj)
{
	daos_hhash_link_getref(&obj->cob_hlink);
}

static daos_handle_t
obj_ptr2hdl(struct dc_object *obj)
{
	daos_handle_t oh;

	daos_hhash_link_key(&obj->cob_hlink, &oh.cookie);
	return oh;
}

static struct dc_object *
obj_hdl2ptr(daos_handle_t oh)
{
	struct d_hlink *hlink;

	hlink = daos_hhash_link_lookup(oh.cookie);
	if (hlink == NULL)
		return NULL;

	return container_of(hlink, struct dc_object, cob_hlink);
}

static void
obj_hdl_link(struct dc_object *obj)
{
	daos_hhash_link_insert(&obj->cob_hlink, D_HTYPE_OBJ);
}

static void
obj_hdl_unlink(struct dc_object *obj)
{
	daos_hhash_link_delete(&obj->cob_hlink);
}

static daos_handle_t
obj_hdl2cont_hdl(daos_handle_t oh)
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

static int
obj_layout_create(struct dc_object *obj)
{
	struct pl_obj_layout	*layout;
	struct dc_pool		*pool;
	struct pl_map		*map;
	int			 nr;
	int			 rc;

	pool = dc_hdl2pool(dc_cont_hdl2pool_hdl(obj->cob_coh));
	D_ASSERT(pool != NULL);

	map = pl_map_find(pool->dp_pool, obj->cob_md.omd_id);
	dc_pool_put(pool);

	if (map == NULL) {
		D_DEBUG(DB_PL, "Cannot find valid placement map\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = pl_obj_place(map, &obj->cob_md, NULL, &layout);
	pl_map_decref(map);
	if (rc != 0) {
		D_DEBUG(DB_PL, "Failed to generate object layout\n");
		D_GOTO(out, rc);
	}
	D_DEBUG(DB_PL, "Place object on %d targets\n", layout->ol_nr);

	D_ASSERT(obj->cob_layout == NULL);
	obj->cob_layout = layout;
	nr = layout->ol_nr;

	D_ASSERT(obj->cob_obj_shards == NULL);
	D_ALLOC(obj->cob_obj_shards, nr * sizeof(obj->cob_obj_shards[0]));
	if (obj->cob_obj_shards == NULL)
		rc = -DER_NOMEM;

out:
	return rc;
}

static int
obj_layout_refresh(struct dc_object *obj)
{
	int	rc;

	D_RWLOCK_WRLOCK(&obj->cob_lock);
	obj_layout_free(obj);
	rc = obj_layout_create(obj);
	D_RWLOCK_UNLOCK(&obj->cob_lock);

	return rc;
}

static int
obj_get_grp_size(struct dc_object *obj)
{
	struct daos_oclass_attr *oc_attr;
	unsigned int grp_size;

	oc_attr = daos_oclass_attr_find(obj->cob_md.omd_id);
	D_ASSERT(oc_attr != NULL);
	grp_size = daos_oclass_grp_size(oc_attr);
	if (grp_size == DAOS_OBJ_REPL_MAX)
		grp_size = obj->cob_layout->ol_nr;
	return grp_size;
}

static int
obj_dkey2grp(struct dc_object *obj, uint64_t hash, unsigned int map_ver)
{
	int		grp_size;
	uint64_t	grp_idx;

	grp_size = obj_get_grp_size(obj);
	D_ASSERT(grp_size > 0);

	D_RWLOCK_RDLOCK(&obj->cob_lock);
	if (obj->cob_layout->ol_ver != map_ver) {
		D_RWLOCK_UNLOCK(&obj->cob_lock);
		return -DER_STALE;
	}

	D_ASSERT(obj->cob_layout->ol_nr >= grp_size);

	/* XXX, consistent hash? */
	grp_idx = hash % (obj->cob_layout->ol_nr / grp_size);
	D_RWLOCK_UNLOCK(&obj->cob_lock);

	return grp_idx;
}

/* Get a valid shard from an object group */
static int
obj_grp_valid_shard_get(struct dc_object *obj, int idx,
			unsigned int map_ver, uint32_t op)
{
	int idx_first;
	int idx_last;
	int grp_size;
	bool rebuilding = false;
	int i = 0;

	grp_size = obj_get_grp_size(obj);
	D_ASSERT(grp_size > 0);

	idx_first = (idx / grp_size) * grp_size;
	idx_last = idx_first + grp_size - 1;

	D_ASSERT(obj->cob_layout->ol_nr > 0);
	D_ASSERTF(idx_last < obj->cob_layout->ol_nr,
		  "idx %d, first %d, last %d, shard_nr %d\n",
		  idx, idx_first, idx_last, obj->cob_layout->ol_nr);

	D_RWLOCK_RDLOCK(&obj->cob_lock);
	if (obj->cob_layout->ol_ver != map_ver) {
		/* Sigh, someone else change the pool map */
		D_RWLOCK_UNLOCK(&obj->cob_lock);
		return -DER_STALE;
	}

	if (DAOS_FAIL_CHECK(DAOS_OBJ_SPECIAL_SHARD)) {
		idx = daos_fail_value_get();

		if (idx >= grp_size) {
			D_RWLOCK_UNLOCK(&obj->cob_lock);
			return -DER_INVAL;
		}

		if (obj->cob_layout->ol_shards[idx].po_shard != -1) {
			D_DEBUG(DB_TRACE, "special shard %d\n", idx);
			D_RWLOCK_UNLOCK(&obj->cob_lock);
			return idx;
		}
	}

	for (i = 0; i < grp_size; i++,
	     idx = (idx + 1) % grp_size + idx_first) {
		/* let's skip the rebuild shard for non-update op */
		if (op != DAOS_OBJ_RPC_UPDATE &&
		    obj->cob_layout->ol_shards[idx].po_rebuilding) {
			rebuilding = true;
			continue;
		}

		if (obj->cob_layout->ol_shards[idx].po_shard != -1)
			break;
	}
	D_RWLOCK_UNLOCK(&obj->cob_lock);

	if (i == grp_size) {
		if (op == DAOS_OBJ_RPC_UPDATE || !rebuilding)
			return -DER_NONEXIST;

		/* For non-update ops, some of rebuilding shards
		 * might not be refreshed yet, let's update pool
		 * map and retry.
		 */
		return -DER_STALE;
	}

	return idx;
}

static int
obj_grp_shard_get(struct dc_object *obj, uint32_t grp_idx,
		  uint64_t hash, unsigned int map_ver, uint32_t op)
{
	int	grp_size;
	int	idx;

	grp_size = obj_get_grp_size(obj);
	idx = hash % grp_size + grp_idx * grp_size;
	return obj_grp_valid_shard_get(obj, idx, map_ver, op);
}

static int
obj_dkeyhash2shard(struct dc_object *obj, uint64_t hash, unsigned int map_ver,
		   uint32_t op)
{
	int	 grp_idx;

	grp_idx = obj_dkey2grp(obj, hash, map_ver);
	if (grp_idx < 0)
		return grp_idx;

	return obj_grp_shard_get(obj, grp_idx, hash, map_ver, op);
}

static int
obj_dkeyhash2update_grp(struct dc_object *obj, uint64_t hash, uint32_t map_ver,
			uint32_t *start_shard, uint32_t *grp_size)
{
	int	 grp_idx;

	grp_idx = obj_dkey2grp(obj, hash, map_ver);
	if (grp_idx < 0)
		return grp_idx;

	*grp_size = obj_get_grp_size(obj);
	*start_shard = grp_idx * *grp_size;

	return 0;
}

static void
obj_ptr2shards(struct dc_object *obj, uint32_t *start_shard,
	       uint32_t *shard_nr)
{
	*start_shard = 0;
	*shard_nr = obj->cob_layout->ol_nr;
}

static uint32_t
obj_shard2tgt(struct dc_object *obj, uint32_t shard)
{
	D_ASSERT(shard < obj->cob_layout->ol_nr);
	return obj->cob_layout->ol_shards[shard].po_target;
}

static int
obj_ptr2poh(struct dc_object *obj, daos_handle_t *ph)
{
	daos_handle_t   coh;

	coh = obj->cob_coh;
	if (daos_handle_is_inval(coh))
		return -DER_NO_HDL;

	*ph = dc_cont_hdl2pool_hdl(coh);
	if (daos_handle_is_inval(*ph))
		return -DER_NO_HDL;

	return 0;
}

/* Get pool map version from object handle */
static int
obj_ptr2pm_ver(struct dc_object *obj, unsigned int *map_ver)
{
	daos_handle_t	ph;
	int		rc;

	rc = obj_ptr2poh(obj, &ph);
	if (rc != 0)
		return rc;

	rc = dc_pool_map_version_get(ph, map_ver);
	return rc;
}

static int
obj_pool_query_cb(tse_task_t *task, void *data)
{
	struct dc_object	*obj = *((struct dc_object **)data);
	daos_pool_query_t	*args;

	obj_layout_refresh(obj);
	obj_decref(obj);

	args = dc_task_get_args(task);
	D_FREE_PTR(args->info);
	return 0;
}

static int
obj_pool_query_task(tse_sched_t *sched, struct dc_object *obj,
		    tse_task_t **taskp)
{
	tse_task_t		*task;
	daos_pool_query_t	*args;
	daos_handle_t		 ph;
	int			 rc = 0;

	rc = obj_ptr2poh(obj, &ph);
	if (rc != 0)
		return rc;

	rc = dc_task_create(dc_pool_query, sched, NULL, &task);
	if (rc != 0)
		return rc;

	args = dc_task_get_args(task);
	args->poh = ph;
	D_ALLOC_PTR(args->info);
	if (args->info == NULL)
		D_GOTO(err, rc = -DER_NOMEM);

	obj_addref(obj);
	rc = dc_task_reg_comp_cb(task, obj_pool_query_cb, &obj, sizeof(obj));
	if (rc != 0) {
		obj_decref(obj);
		D_GOTO(err, rc);
	}

	*taskp = task;
	return 0;
err:
	dc_task_decref(task);
	if (args->info)
		D_FREE_PTR(args->info);

	return rc;
}

int
dc_obj_class_register(tse_task_t *task)
{
	D_ERROR("Unsupported API\n");
	tse_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_obj_class_query(tse_task_t *task)
{
	D_ERROR("Unsupported API\n");
	tse_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_obj_class_list(tse_task_t *task)
{
	D_ERROR("Unsupported API\n");
	tse_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_obj_declare(tse_task_t *task)
{
	D_ERROR("Unsupported API\n");
	tse_task_complete(task, -DER_NOSYS);
	return 0;
#if 0
	daos_obj_declare_t	args;
	struct daos_oclass_attr *oc_attr;
	int			rc;

	args = daos_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	/* XXX Only support internal classes for now */
	oc_attr = daos_oclass_attr_find(args->oid);
	rc = oc_attr != NULL ? 0 : -DER_INVAL;

	tse_task_complete(task, rc);
	return rc;
#endif
}

int
dc_obj_open(tse_task_t *task)
{
	daos_obj_open_t		*args;
	struct dc_object	*obj;
	int			rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	obj = obj_alloc();
	if (obj == NULL)
		return -DER_NOMEM;

	obj->cob_coh  = args->coh;
	obj->cob_mode = args->mode;

	rc = D_SPIN_INIT(&obj->cob_spin, PTHREAD_PROCESS_PRIVATE);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = D_RWLOCK_INIT(&obj->cob_lock, NULL);
	if (rc != 0)
		D_GOTO(out, rc);

	/* it is a local operation for now, does not require event */
	rc = dc_obj_fetch_md(args->oid, &obj->cob_md);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = obj_layout_create(obj);
	if (rc != 0)
		D_GOTO(out, rc);

	obj_hdl_link(obj);
	*args->oh = obj_ptr2hdl(obj);

out:
	obj_decref(obj);
	tse_task_complete(task, rc);
	return rc;
}

int
dc_obj_close(tse_task_t *task)
{
	daos_obj_close_t	*args;
	struct dc_object	*obj;
	int			 rc = 0;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	obj = obj_hdl2ptr(args->oh);
	if (obj == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	obj_hdl_unlink(obj);
	obj_decref(obj);

out:
	tse_task_complete(task, rc);
	return 0;
}

int
dc_obj_fetch_md(daos_obj_id_t oid, struct daos_obj_md *md)
{
	/* For predefined object classes, do nothing at here. But for those
	 * customized classes, we need to fetch for the remote OI table.
	 */
	memset(md, 0, sizeof(*md));
	md->omd_id = oid;
	return 0;
}

int
dc_obj_layout_get(daos_handle_t oh, struct pl_obj_layout **layout,
		  unsigned int *grp_nr, unsigned int *grp_size)
{
	struct daos_oclass_attr *oc_attr;
	struct dc_object *obj;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		return -DER_NO_HDL;

	*layout = obj->cob_layout;

	oc_attr = daos_oclass_attr_find(obj->cob_md.omd_id);
	D_ASSERT(oc_attr != NULL);
	*grp_size = daos_oclass_grp_size(oc_attr);
	*grp_nr = daos_oclass_grp_nr(oc_attr, &obj->cob_md);
	if (*grp_nr == DAOS_OBJ_GRP_MAX)
		*grp_nr = obj->cob_layout->ol_nr / *grp_size;
	obj_decref(obj);
	return 0;
}

int
dc_obj_query(tse_task_t *task)
{
	D_ERROR("Unsupported API\n");
	tse_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_obj_layout_refresh(daos_handle_t oh)
{
	struct dc_object *obj;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		return -DER_NO_HDL;

	obj_layout_refresh(obj);

	obj_decref(obj);

	return 0;
}

static int
obj_retry_cb(tse_task_t *task, struct dc_object *obj, bool io_retry)
{
	tse_sched_t	 *sched = tse_task2sched(task);
	tse_task_t	 *pool_task = NULL;
	int		  result = task->dt_result;
	int		  rc;

	/** if succeed or no retry, leave */
	if (!obj_retry_error(result) && !io_retry)
		return result;

	/* Add pool map update task */
	rc = obj_pool_query_task(sched, obj, &pool_task);
	if (rc != 0)
		D_GOTO(err, rc);

	if (io_retry) {
		/* Let's reset task result before retry */
		rc = dc_task_resched(task);
		if (rc != 0) {
			D_ERROR("Failed to re-init task (%p)\n", task);
			D_GOTO(err, rc);
		}

		rc = dc_task_depend(task, 1, &pool_task);
		if (rc != 0) {
			D_ERROR("Failed to add dependency on pool "
				 "query task (%p)\n", pool_task);
			D_GOTO(err, rc);
		}
	}

	D_DEBUG(DB_IO, "Retrying task=%p for err=%d, io_retry=%d\n",
		 task, result, io_retry);
	/* ignore returned value, error is reported by comp_cb */
	dc_task_schedule(pool_task, io_retry);

	return 0;
err:
	if (pool_task)
		dc_task_decref(pool_task);

	task->dt_result = result; /* restore the orignal error */
	D_ERROR("Failed to retry task=%p(err=%d), io_retry=%d, rc %d.\n",
		task, result, io_retry, rc);
	return rc;
}

/* Auxiliary args for object I/O */
struct obj_auxi_args {
	int		 opc;
	uint32_t	 map_ver_req;
	uint32_t	 map_ver_reply;
	uint32_t	 io_retry:1,
			 shard_task_scheded:1;
	int		 result;
	d_list_t	 shard_task_head;
	tse_task_t	*obj_task;
};

/* shard update/punch auxiliary args, must be the first field of
 * shard_update_args and shard_punch_args.
 */
struct shard_auxi_args {
	struct dc_object	*obj;
	struct obj_auxi_args	*obj_auxi;
	uint32_t		 shard;
	uint32_t		 target;
	uint32_t		 map_ver;
};

struct obj_list_arg {
	struct dc_object	*obj;
	daos_hash_out_t		*anchor;
	unsigned int		 single_shard:1;
};

struct shard_update_args {
	struct shard_auxi_args	 auxi;
	daos_epoch_t		 epoch;
	daos_key_t		*dkey;
	uint64_t		 dkey_hash;
	unsigned int		 nr;
	daos_iod_t		*iods;
	daos_sg_list_t		*sgls;
};

static int
shard_result_process(tse_task_t *task, void *arg)
{
	struct obj_auxi_args	*obj_auxi = (struct obj_auxi_args *)arg;
	struct shard_auxi_args	*shard_auxi;
	int			 ret = task->dt_result;

	/*
	 * Check shard IO task's completion status:
	 * 1) if succeed just stores the highest replied pm version.
	 * 2) if any shard got retryable error, mark the IO as retryable.
	 * 3) for the un-retryable failure, store it in obj_auxi->result.
	 */
	shard_auxi = tse_task_buf_embedded(task, sizeof(*shard_auxi));
	if (ret == 0) {
		if (obj_auxi->map_ver_reply < shard_auxi->map_ver)
			obj_auxi->map_ver_reply = shard_auxi->map_ver;
	} else if (obj_retry_error(ret)) {
		D_DEBUG(DB_IO, "shard %d ret %d.\n", shard_auxi->shard, ret);
		obj_auxi->io_retry = 1;
	} else {
		/* for un-retryable failure, set the err to whole obj IO */
		D_DEBUG(DB_IO, "shard %d ret %d.\n", shard_auxi->shard, ret);
		obj_auxi->result = ret;
	}

	return 0;
}

static int
shard_task_remove(tse_task_t *task, void *arg)
{
	tse_task_list_del(task);
	tse_task_decref(task);
	return 0;
}

static void
obj_list_dkey_cb(tse_task_t *task, struct obj_list_arg *arg)
{
	struct dc_object       *obj = arg->obj;
	daos_hash_out_t	       *anchor = arg->anchor;
	uint32_t		shard = dc_obj_anchor2shard(anchor);
	int			grp_size;

	if (task->dt_result != 0)
		return;

	grp_size = obj_get_grp_size(obj);
	D_ASSERT(grp_size > 0);

	if (!daos_hash_is_eof(anchor)) {
		D_DEBUG(DB_IO, "More keys in shard %d\n", shard);
	} else if ((shard < obj->cob_layout->ol_nr - grp_size) &&
		   !arg->single_shard) {
		shard += grp_size;
		D_DEBUG(DB_IO, "next shard %d grp %d nr %u\n",
			shard, grp_size, obj->cob_layout->ol_nr);

		enum_anchor_reset_hkey(anchor);
		enum_anchor_set_tag(anchor, 0);
		dc_obj_shard2anchor(anchor, shard);
	} else {
		D_DEBUG(DB_IO, "Enumerated All shards\n");
	}
}

static int
obj_comp_cb(tse_task_t *task, void *data)
{
	struct obj_list_arg	*arg;
	struct dc_object	*obj;
	struct obj_auxi_args	*obj_auxi;
	d_list_t		*head = NULL;
	bool			 pm_stale = false;
	bool			 io_retry = false;

	obj_auxi = tse_task_stack_pop(task, sizeof(*obj_auxi));
	switch (obj_auxi->opc) {
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
		arg = data;
		obj = arg->obj;
		obj_list_dkey_cb(task, arg);
		break;
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
		arg = data;
		obj = arg->obj;
		if (daos_hash_is_eof(arg->anchor))
			D_DEBUG(DB_IO, "Enumerated completed\n");
		break;
	case DAOS_OBJ_RPC_FETCH:
		obj = *((struct dc_object **)data);
		break;
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
		obj = *((struct dc_object **)data);
		obj_auxi->map_ver_reply = 0;
		head = &obj_auxi->shard_task_head;
		D_ASSERT(!d_list_empty(head));
		obj_auxi->result = 0;
		obj_auxi->io_retry = 0;
		tse_task_list_traverse(head, shard_result_process, obj_auxi);
		/* for stale pm version, retry the obj IO at there will check
		 * if need to retry shard IO.
		 */
		if (obj_auxi->map_ver_reply > obj_auxi->map_ver_req) {
			D_DEBUG(DB_IO, "map_ver stale (req %d, reply %d).\n",
				obj_auxi->map_ver_req, obj_auxi->map_ver_reply);
			obj_auxi->io_retry = 1;
		}
		break;
	default:
		D_ERROR("incorrect opc %#x.\n", obj_auxi->opc);
		D_ASSERT(0);
		break;
	};

	if (obj_auxi->map_ver_reply > obj_auxi->map_ver_req)
		pm_stale = true;
	if (obj_retry_error(task->dt_result) || obj_auxi->io_retry)
		io_retry = true;

	if (pm_stale || io_retry)
		obj_retry_cb(task, obj, io_retry);
	else if (task->dt_result == 0)
		task->dt_result = obj_auxi->result;

	if (!io_retry && head != NULL) {
		tse_task_list_traverse(head, shard_task_remove, NULL);
		D_ASSERT(d_list_empty(head));
	}

	obj_decref(obj);
	return 0;
}

int
dc_obj_fetch(tse_task_t *task)
{
	daos_obj_fetch_t	*args = dc_task_get_args(task);
	struct obj_auxi_args	*obj_auxi;
	struct dc_object	*obj;
	struct dc_obj_shard	*obj_shard;
	int			 shard;
	unsigned int		 map_ver;
	uint64_t		 dkey_hash;
	int			 rc;

	obj = obj_hdl2ptr(args->oh);
	if (obj == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	obj_auxi = tse_task_stack_push(task, sizeof(*obj_auxi));
	rc = tse_task_register_comp_cb(task, obj_comp_cb, &obj,
				       sizeof(obj));
	if (rc != 0) {
		/* NB: process_rc_cb() will release refcount in other cases */
		obj_decref(obj);
		D_GOTO(out_task, rc);
	}

	rc = obj_ptr2pm_ver(obj, &map_ver);
	if (rc)
		D_GOTO(out_task, rc);

	dkey_hash = obj_dkey2hash(args->dkey);
	shard = obj_dkeyhash2shard(obj, dkey_hash, map_ver,
				   DAOS_OPC_OBJ_UPDATE);
	if (shard < 0)
		D_GOTO(out_task, rc = shard);

	rc = obj_shard_open(obj, shard, map_ver, &obj_shard);
	if (rc != 0)
		D_GOTO(out_task, rc);

	obj_auxi->opc = DAOS_OBJ_RPC_FETCH;
	obj_auxi->map_ver_req = map_ver;
	obj_auxi->map_ver_reply = map_ver;
	D_DEBUG(DB_IO, "fetch "DF_OID" shard %u\n",
		DP_OID(obj->cob_md.omd_id), shard);
	tse_task_stack_push_data(task, &dkey_hash, sizeof(dkey_hash));
	rc = dc_obj_shard_fetch(obj_shard, args->epoch, args->dkey, args->nr,
				args->iods, args->sgls, args->maps,
				&obj_auxi->map_ver_reply, task);
	obj_shard_close(obj_shard);
	return rc;

out_task:
	tse_task_complete(task, rc);
	return rc;
}

static int
shard_update_task(tse_task_t *task)
{
	struct shard_update_args	*args;
	struct dc_object		*obj;
	struct dc_obj_shard		*obj_shard;
	int				 rc;

	args = tse_task_buf_embedded(task, sizeof(*args));
	obj = args->auxi.obj;
	D_ASSERT(obj != NULL);

	if (args->auxi.shard == 0 &&
	    DAOS_FAIL_CHECK(DAOS_SHARD_OBJ_UPDATE_TIMEOUT_SINGLE)) {
		D_INFO("Set Shard 0 update to return -DER_TIMEDOUT\n");
		daos_fail_loc_set(DAOS_SHARD_OBJ_UPDATE_TIMEOUT |
				  DAOS_FAIL_ONCE);
	}

	rc = obj_shard_open(obj, args->auxi.shard, args->auxi.map_ver,
			    &obj_shard);
	if (rc != 0) {
		/* skip a failed target */
		if (rc == -DER_NONEXIST)
			rc = 0;

		tse_task_complete(task, rc);
		return rc;
	}

	tse_task_stack_push_data(task, &args->dkey_hash,
				 sizeof(args->dkey_hash));
	rc = dc_obj_shard_update(obj_shard, args->epoch, args->dkey, args->nr,
				 args->iods, args->sgls, &args->auxi.map_ver,
				 task);

	obj_shard_close(obj_shard);
	return rc;
}

static void
shard_task_list_init(struct obj_auxi_args *auxi)
{
	if (auxi->io_retry == 0) {
		D_INIT_LIST_HEAD(&auxi->shard_task_head);
		return;
	}

	D_ASSERT(!d_list_empty(&auxi->shard_task_head));
}

static int
shard_task_abort(tse_task_t *task, void *arg)
{
	int	rc = *((int *)arg);

	tse_task_complete(task, rc);

	return 0;
}

static int
shard_task_sched(tse_task_t *task, void *arg)
{
	tse_task_t			*obj_task;
	struct obj_auxi_args		*obj_auxi;
	struct shard_auxi_args		*shard_auxi;
	uint32_t			 target;
	unsigned int			 map_ver;
	int				 rc = 0;

	shard_auxi = tse_task_buf_embedded(task, sizeof(*shard_auxi));
	obj_auxi = shard_auxi->obj_auxi;
	map_ver = obj_auxi->map_ver_req;
	obj_task = obj_auxi->obj_task;
	if (obj_auxi->io_retry) {
		/* For retried IO, check if the shard's target changed after
		 * pool map query. If match then need not do anything, if
		 * mismatch then need to re-schedule the shard IO on the new
		 * pool map.
		 * Also retry the shard IO if it got retryable error last time.
		 */
		target = obj_shard2tgt(shard_auxi->obj, shard_auxi->shard);
		if (obj_retry_error(task->dt_result) ||
		    target != shard_auxi->target) {
			rc = tse_task_reinit(task);
			if (rc != 0)
				goto out;

			rc = tse_task_register_deps(obj_task, 1, &task);
			if (rc != 0)
				goto out;

			shard_auxi->map_ver	= map_ver;
			shard_auxi->target	= target;
			obj_auxi->shard_task_scheded = 1;
			D_DEBUG(DB_IO, "shard %d, dt_result %d, target %d @ "
				"map_ver %d, target %d @ last_map_ver %d, "
				"shard task %p re-scheduled.\n",
				shard_auxi->shard, task->dt_result, target,
				map_ver, shard_auxi->target,
				shard_auxi->map_ver, task);
		}
	} else {
		tse_task_schedule(task, true);
		obj_auxi->shard_task_scheded = 1;
	}

out:
	if (rc != 0)
		tse_task_complete(task, rc);
	return rc;
}

static void
obj_shard_task_sched(struct obj_auxi_args *obj_auxi)
{
	D_ASSERT(!d_list_empty(&obj_auxi->shard_task_head));
	obj_auxi->shard_task_scheded = 0;
	tse_task_list_traverse(&obj_auxi->shard_task_head, shard_task_sched,
			       NULL);
	/* It is possible that the IO retried by stale pm version found, but
	 * the IO involved shards' targets not changed. No any shard task
	 * re-scheduled for this case, can complete the obj IO task.
	 */
	if (obj_auxi->shard_task_scheded == 0)
		tse_task_complete(obj_auxi->obj_task, 0);
}

int
dc_obj_update(tse_task_t *task)
{
	daos_obj_update_t	*args = dc_task_get_args(task);
	tse_sched_t		*sched = tse_task2sched(task);
	struct obj_auxi_args	*obj_auxi;
	struct dc_object	*obj;
	d_list_t		*head = NULL;
	unsigned int		shard;
	unsigned int		shards_cnt;
	unsigned int		map_ver;
	uint64_t		dkey_hash;
	int			i;
	int			rc;

	obj = obj_hdl2ptr(args->oh);
	if (obj == NULL) {
		rc = -DER_NO_HDL;
		goto out_task;
	}

	obj_auxi = tse_task_stack_push(task, sizeof(*obj_auxi));
	shard_task_list_init(obj_auxi);
	rc = tse_task_register_comp_cb(task, obj_comp_cb, &obj,
				       sizeof(obj));
	if (rc != 0) {
		/* NB: obj_comp_cb() will release refcount in other cases */
		obj_decref(obj);
		goto out_task;
	}

	rc = obj_ptr2pm_ver(obj, &map_ver);
	if (rc)
		goto out_task;

	dkey_hash = obj_dkey2hash(args->dkey);
	rc = obj_dkeyhash2update_grp(obj, dkey_hash, map_ver,
				     &shard, &shards_cnt);
	if (rc != 0)
		goto out_task;

	obj_auxi->opc = DAOS_OBJ_RPC_UPDATE;
	obj_auxi->map_ver_req = map_ver;
	obj_auxi->obj_task = task;
	D_DEBUG(DB_IO, "update "DF_OID" start %u cnt %u\n",
		DP_OID(obj->cob_md.omd_id), shard, shards_cnt);

	head = &obj_auxi->shard_task_head;
	/* for retried obj IO, reuse the previous shard tasks and resched it */
	if (obj_auxi->io_retry)
		goto task_sched;
	for (i = 0; i < shards_cnt; i++, shard++) {
		tse_task_t			*shard_task;
		struct shard_update_args	*shard_arg;

		rc = tse_task_create(shard_update_task, sched, NULL,
				     &shard_task);
		if (rc != 0)
			goto out_task;

		shard_arg = tse_task_buf_embedded(shard_task,
						  sizeof(*shard_arg));
		shard_arg->epoch		= args->epoch;
		shard_arg->dkey			= args->dkey;
		shard_arg->dkey_hash		= dkey_hash;
		shard_arg->nr			= args->nr;
		shard_arg->iods			= args->iods;
		shard_arg->sgls			= args->sgls;
		shard_arg->auxi.map_ver		= map_ver;
		shard_arg->auxi.shard		= shard;
		shard_arg->auxi.target		= obj_shard2tgt(obj, shard);
		shard_arg->auxi.obj		= obj;
		shard_arg->auxi.obj_auxi	= obj_auxi;

		rc = tse_task_register_deps(task, 1, &shard_task);
		if (rc != 0) {
			tse_task_complete(shard_task, rc);
			goto out_task;
		}
		/* decref and delete from head at shard_task_remove */
		tse_task_addref(shard_task);
		tse_task_list_add(shard_task, head);
	}

task_sched:
	obj_shard_task_sched(obj_auxi);
	return 0;

out_task:
	if (head == NULL || d_list_empty(head))
		tse_task_complete(task, rc);
	else
		tse_task_list_traverse(head, shard_task_abort, &rc);
	return rc;
}

static int
dc_obj_list_internal(daos_handle_t oh, uint32_t op, daos_epoch_t epoch,
		     daos_key_t *dkey, daos_key_t *akey, daos_iod_type_t type,
		     daos_size_t *size, uint32_t *nr, daos_key_desc_t *kds,
		     daos_sg_list_t *sgl, daos_recx_t *recxs,
		     daos_epoch_range_t *eprs, uuid_t *cookies,
		     uint32_t *versions, daos_hash_out_t *anchor,
		     bool incr_order, bool single_shard, tse_task_t *task)
{
	struct dc_object	*obj;
	struct dc_obj_shard	*obj_shard;
	struct obj_auxi_args	*obj_auxi;
	unsigned int		 map_ver;
	struct obj_list_arg	 list_args;
	uint64_t		 dkey_hash;
	int			 shard;
	int			 rc;

	if (nr == NULL || *nr == 0) {
		D_DEBUG(DB_IO, "Invalid API parameter.\n");
		D_GOTO(out_task, rc = -DER_INVAL);
	}

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	list_args.obj = obj;
	list_args.anchor = anchor;
	list_args.single_shard = single_shard;

	obj_auxi = tse_task_stack_push(task, sizeof(*obj_auxi));
	obj_auxi->opc = op;
	rc = tse_task_register_comp_cb(task, obj_comp_cb,
				       &list_args, sizeof(list_args));
	if (rc != 0) {
		/* NB: process_rc_cb() will release refcount in other cases */
		obj_decref(obj);
		D_GOTO(out_task, rc);
	}

	rc = obj_ptr2pm_ver(obj, &map_ver);
	if (rc)
		D_GOTO(out_task, rc);

	dkey_hash = obj_dkey2hash(dkey);
	if (op == DAOS_OBJ_DKEY_RPC_ENUMERATE) {
		shard = dc_obj_anchor2shard(anchor);
		shard = obj_grp_valid_shard_get(obj, shard, map_ver, op);
		if (shard < 0)
			D_GOTO(out_task, rc = shard);

		dc_obj_shard2anchor(anchor, shard);
	} else {
		shard = obj_dkeyhash2shard(obj, dkey_hash, map_ver, op);
		if (shard < 0)
			D_GOTO(out_task, rc = shard);

		dc_obj_shard2anchor(anchor, shard);
	}

	/** object will be decref by task complete cb */
	rc = obj_shard_open(obj, shard, map_ver, &obj_shard);
	if (rc != 0)
		D_GOTO(out_task, rc);

	tse_task_stack_push_data(task, &dkey_hash, sizeof(dkey_hash));
	obj_auxi->map_ver_req = map_ver;
	obj_auxi->map_ver_reply = map_ver;
	if (op == DAOS_OBJ_RECX_RPC_ENUMERATE)
		rc = dc_obj_shard_list_rec(obj_shard, op, epoch, dkey, akey,
					   type, size, nr, recxs, eprs,
					   cookies, versions, anchor,
					   &obj_auxi->map_ver_reply,
					   incr_order, task);
	else
		rc = dc_obj_shard_list_key(obj_shard, op, epoch, dkey, nr, kds,
				sgl, anchor, &obj_auxi->map_ver_reply, task);

	D_DEBUG(DB_IO, "Enumerate keys in shard %d: rc %d\n", shard, rc);
	obj_shard_close(obj_shard);

	return rc;

out_task:
	tse_task_complete(task, rc);
	return rc;
}

int
dc_obj_list_dkey(tse_task_t *task)
{
	daos_obj_list_dkey_t	*args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return dc_obj_list_internal(args->oh, DAOS_OBJ_DKEY_RPC_ENUMERATE,
				    args->epoch, NULL, NULL, DAOS_IOD_NONE,
				    NULL, args->nr, args->kds, args->sgl,
				    NULL, NULL, NULL, NULL, args->anchor,
				    true, false, task);
}

int
dc_obj_list_akey(tse_task_t *task)
{
	daos_obj_list_akey_t	*args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return dc_obj_list_internal(args->oh, DAOS_OBJ_AKEY_RPC_ENUMERATE,
				    args->epoch, args->dkey, NULL,
				    DAOS_IOD_NONE, NULL, args->nr, args->kds,
				    args->sgl, NULL, NULL, NULL, NULL,
				    args->anchor, true, false, task);
}

int
dc_obj_list_rec(tse_task_t *task)
{
	daos_obj_list_recx_t	*args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return dc_obj_list_internal(args->oh, DAOS_OBJ_RECX_RPC_ENUMERATE,
				    args->epoch, args->dkey, args->akey,
				    args->type, args->size, args->nr, NULL,
				    NULL, args->recxs, args->eprs,
				    args->cookies, args->versions, args->anchor,
				    args->incr_order, false, task);
}

int
dc_obj_single_shard_list_dkey(tse_task_t *task)
{
	daos_obj_list_dkey_t	*args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return dc_obj_list_internal(args->oh, DAOS_OBJ_DKEY_RPC_ENUMERATE,
				    args->epoch, NULL, NULL, DAOS_IOD_NONE,
				    NULL, args->nr, args->kds, args->sgl, NULL,
				    NULL, NULL, NULL, args->anchor, true, true,
				    task);
}

struct shard_punch_args {
	struct shard_auxi_args	 pa_auxi;
	uuid_t			 pa_coh_uuid;
	uuid_t			 pa_cont_uuid;
	uint32_t		 pa_opc;
	daos_obj_punch_t	*pa_api_args;
	uint64_t		 pa_dkey_hash;
};

static int
shard_punch_task(tse_task_t *task)
{
	struct shard_punch_args		*args;
	daos_obj_punch_t		*api_args;
	struct dc_object		*obj;
	struct dc_obj_shard		*obj_shard;
	int				 rc;

	args = tse_task_buf_embedded(task, sizeof(*args));
	obj = args->pa_auxi.obj;

	rc = obj_shard_open(obj, args->pa_auxi.shard, args->pa_auxi.map_ver,
			    &obj_shard);
	if (rc != 0) {
		/* skip a failed target */
		if (rc == -DER_NONEXIST)
			rc = 0;

		tse_task_complete(task, rc);
		return rc;
	}

	tse_task_stack_push_data(task, &args->pa_dkey_hash,
				 sizeof(args->pa_dkey_hash));
	api_args = args->pa_api_args;
	rc = dc_obj_shard_punch(obj_shard, args->pa_opc, api_args->epoch,
				api_args->dkey, api_args->akeys,
				api_args->akey_nr, args->pa_coh_uuid,
				args->pa_cont_uuid, &args->pa_auxi.map_ver,
				task);

	obj_shard_close(obj_shard);
	return rc;
}

static int
obj_punch_internal(tse_task_t *api_task, enum obj_rpc_opc opc,
		   daos_obj_punch_t *api_args)
{
	tse_sched_t		*sched = tse_task2sched(api_task);
	struct obj_auxi_args	*obj_auxi;
	struct dc_object	*obj;
	d_list_t		*head = NULL;
	daos_handle_t		 coh;
	uuid_t			 coh_uuid;
	uuid_t			 cont_uuid;
	unsigned int		 shard_first;
	unsigned int		 shard_nr;
	unsigned int		 map_ver;
	uint64_t		 dkey_hash;
	int			 i = 0;
	int			 rc;

	/** Register retry CB */
	obj = obj_hdl2ptr(api_args->oh);
	if (!obj) {
		rc = -DER_NO_HDL;
		goto out_task;
	}

	obj_auxi = tse_task_stack_push(api_task, sizeof(*obj_auxi));
	shard_task_list_init(obj_auxi);

	rc = tse_task_register_comp_cb(api_task, obj_comp_cb, &obj,
				       sizeof(obj));
	if (rc) {
		/* NB: obj_comp_cb() will release refcount in other cases */
		obj_decref(obj);
		goto out_task;
	}

	coh = obj_hdl2cont_hdl(api_args->oh);
	if (daos_handle_is_inval(coh)) {
		rc = -DER_NO_HDL;
		goto out_task;
	}

	rc = dc_cont_hdl2uuid(coh, &coh_uuid, &cont_uuid);
	if (rc != 0)
		D_GOTO(out_task, rc);

	rc = obj_ptr2pm_ver(obj, &map_ver);
	if (rc)
		goto out_task;

	dkey_hash = obj_dkey2hash(api_args->dkey);
	if (opc == DAOS_OBJ_RPC_PUNCH) {
		obj_ptr2shards(obj, &shard_first, &shard_nr);
	} else {
		D_ASSERTF(api_args->dkey != NULL, "dkey should not be NULL\n");
		rc = obj_dkeyhash2update_grp(obj, dkey_hash, map_ver,
					     &shard_first, &shard_nr);
		if (rc != 0)
			goto out_task;
	}

	obj_auxi->opc = opc;
	obj_auxi->map_ver_req = map_ver;
	obj_auxi->obj_task = api_task;
	D_DEBUG(DB_IO, "punch "DF_OID" start %u cnt %u\n",
		 DP_OID(obj->cob_md.omd_id), shard_first, shard_nr);

	head = &obj_auxi->shard_task_head;
	/* for retried obj IO, reuse the previous shard tasks and resched it */
	if (obj_auxi->io_retry)
		goto task_sched;
	for (i = 0; i < shard_nr; i++) {
		tse_task_t		*task;
		struct shard_punch_args	*args;
		unsigned int		 shard;

		shard = shard_first + i;
		rc = tse_task_create(shard_punch_task, sched, NULL, &task);
		if (rc != 0)
			goto out_task;

		args = tse_task_buf_embedded(task, sizeof(*args));
		args->pa_api_args	= api_args;
		args->pa_auxi.shard	= shard;
		args->pa_auxi.target	= obj_shard2tgt(obj, shard);
		args->pa_auxi.map_ver	= map_ver;
		args->pa_auxi.obj	= obj;
		args->pa_auxi.obj_auxi	= obj_auxi;
		args->pa_opc		= opc;
		args->pa_dkey_hash	= dkey_hash;
		uuid_copy(args->pa_coh_uuid, coh_uuid);
		uuid_copy(args->pa_cont_uuid, cont_uuid);

		rc = tse_task_register_deps(api_task, 1, &task);
		if (rc != 0) {
			tse_task_complete(task, rc);
			goto out_task;
		}
		/* decref and delete from head at shard_task_remove */
		tse_task_addref(task);
		tse_task_list_add(task, head);
	}

task_sched:
	obj_shard_task_sched(obj_auxi);
	return rc;

out_task:
	if (head == NULL || d_list_empty(head)) /* nothing has been started */
		tse_task_complete(api_task, rc);
	else
		tse_task_list_traverse(head, shard_task_abort, &rc);

	return rc;
}

int
dc_obj_punch(tse_task_t *task)
{
	daos_obj_punch_t	*args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return obj_punch_internal(task, DAOS_OBJ_RPC_PUNCH, args);
}

int
dc_obj_punch_dkeys(tse_task_t *task)
{
	daos_obj_punch_t	*args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return obj_punch_internal(task, DAOS_OBJ_RPC_PUNCH_DKEYS, args);
}

int
dc_obj_punch_akeys(tse_task_t *task)
{
	daos_obj_punch_t	*args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return obj_punch_internal(task, DAOS_OBJ_RPC_PUNCH_AKEYS, args);
}
