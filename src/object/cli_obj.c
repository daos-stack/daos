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
#define DDSUBSYS	DDFAC(object)

#include <daos_task.h>
#include <daos_types.h>
#include <daos/container.h>
#include <daos/pool.h>
#include "obj_rpc.h"
#include "obj_internal.h"

#define CLI_OBJ_IO_PARMS	8

static struct dc_object *
obj_alloc(void)
{
	struct dc_object *obj;

	D__ALLOC_PTR(obj);
	if (obj == NULL)
		return NULL;

	obj->cob_ref = 1;
	return obj;
}

static void
obj_layout_free(struct dc_object *obj)
{
	struct pl_obj_layout *layout;
	int		     i;

	layout = obj->cob_layout;
	if (layout == NULL)
		return;

	if (obj->cob_mohs != NULL) {
		for (i = 0; i < layout->ol_nr; i++) {
			if (!daos_handle_is_inval(obj->cob_mohs[i]))
				dc_obj_shard_close(obj->cob_mohs[i]);
		}
		D__FREE(obj->cob_mohs, layout->ol_nr * sizeof(*obj->cob_mohs));
		obj->cob_mohs = NULL;
	}

	pl_obj_layout_free(layout);
	obj->cob_layout = NULL;
}

static void
obj_free(struct dc_object *obj)
{
	obj_layout_free(obj);
	pthread_rwlock_destroy(&obj->cob_lock);
	D__FREE_PTR(obj);
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

/**
 * Open an object shard (shard object), cache the open handle.
 */
static int
obj_shard_open(struct dc_object *obj, unsigned int shard, unsigned int map_ver,
	       daos_handle_t *oh)
{
	struct pl_obj_layout	*layout;
	int			 rc = 0;

	pthread_rwlock_rdlock(&obj->cob_lock);
	layout = obj->cob_layout;
	if (layout->ol_ver != map_ver) {
		pthread_rwlock_unlock(&obj->cob_lock);
		return -DER_STALE;
	}

	/* Skip the invalid shards and targets */
	if (layout->ol_shards[shard].po_shard == -1 ||
	    layout->ol_shards[shard].po_target == -1) {
		pthread_rwlock_unlock(&obj->cob_lock);
		return -DER_NONEXIST;
	}

	/* XXX could be otherwise for some object classes? */
	D__ASSERT(layout->ol_shards[shard].po_shard == shard);

	D__DEBUG(DB_IO, "Open object shard %d\n", shard);
	if (daos_handle_is_inval(obj->cob_mohs[shard])) {
		daos_unit_oid_t	 oid;

		memset(&oid, 0, sizeof(oid));
		oid.id_shard = shard;
		oid.id_pub   = obj->cob_md.omd_id;
		/* NB: obj open is a local operation, so it is ok to call
		 * it in sync mode, at least for now.
		 */
		rc = dc_obj_shard_open(obj->cob_coh,
				       layout->ol_shards[shard].po_target,
				       oid, obj->cob_mode,
				       &obj->cob_mohs[shard]);
	}

	if (rc == 0) {
		struct dc_obj_shard *shard_obj;

		*oh = obj->cob_mohs[shard];
		/* hold the object */
		shard_obj = obj_shard_hdl2ptr(*oh);
		D__ASSERT(shard_obj != NULL);
	}

	pthread_rwlock_unlock(&obj->cob_lock);

	return rc;
}

static int
obj_layout_create(struct dc_object *obj)
{
	struct pl_obj_layout	*layout;
	struct dc_pool		*pool;
	struct pl_map		*map;
	int			 i;
	int			 nr;
	int			 rc;

	pool = dc_hdl2pool(dc_cont_hdl2pool_hdl(obj->cob_coh));
	D__ASSERT(pool != NULL);

	map = pl_map_find(pool->dp_pool, obj->cob_md.omd_id);
	dc_pool_put(pool);

	if (map == NULL) {
		D__DEBUG(DB_PL, "Cannot find valid placement map\n");
		D__GOTO(out, rc = -DER_INVAL);
	}

	rc = pl_obj_place(map, &obj->cob_md, NULL, &layout);
	pl_map_decref(map);
	if (rc != 0) {
		D__DEBUG(DB_PL, "Failed to generate object layout\n");
		D__GOTO(out, rc);
	}
	D__DEBUG(DB_PL, "Place object on %d targets\n", layout->ol_nr);

	D__ASSERT(obj->cob_layout == NULL);
	obj->cob_layout = layout;
	nr = layout->ol_nr;

	D__ASSERT(obj->cob_mohs == NULL);
	D__ALLOC(obj->cob_mohs, nr * sizeof(*obj->cob_mohs));
	if (obj->cob_mohs == NULL)
		D__GOTO(out, rc = -DER_NOMEM);

	for (i = 0; i < nr; i++)
		obj->cob_mohs[i] = DAOS_HDL_INVAL;

out:
	return rc;
}

static int
obj_layout_refresh(struct dc_object *obj)
{
	int	rc;

	pthread_rwlock_wrlock(&obj->cob_lock);
	obj_layout_free(obj);
	rc = obj_layout_create(obj);
	pthread_rwlock_unlock(&obj->cob_lock);

	return rc;
}

static int
obj_get_grp_size(struct dc_object *obj)
{
	struct daos_oclass_attr *oc_attr;
	unsigned int grp_size;

	oc_attr = daos_oclass_attr_find(obj->cob_md.omd_id);
	D__ASSERT(oc_attr != NULL);
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
	D__ASSERT(grp_size > 0);

	pthread_rwlock_rdlock(&obj->cob_lock);
	if (obj->cob_layout->ol_ver != map_ver) {
		pthread_rwlock_unlock(&obj->cob_lock);
		return -DER_STALE;
	}

	D__ASSERT(obj->cob_layout->ol_nr >= grp_size);

	/* XXX, consistent hash? */
	grp_idx = hash % (obj->cob_layout->ol_nr / grp_size);
	pthread_rwlock_unlock(&obj->cob_lock);

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
	int i;

	grp_size = obj_get_grp_size(obj);
	D__ASSERT(grp_size > 0);

	idx_first = (idx / grp_size) * grp_size;
	idx_last = idx_first + grp_size - 1;

	D__ASSERT(obj->cob_layout->ol_nr > 0);
	D__ASSERTF(idx_last < obj->cob_layout->ol_nr,
		  "idx %d, first %d, last %d, shard_nr %d\n",
		  idx, idx_first, idx_last, obj->cob_layout->ol_nr);

	pthread_rwlock_rdlock(&obj->cob_lock);
	if (obj->cob_layout->ol_ver != map_ver) {
		/* Sigh, someone else change the pool map */
		pthread_rwlock_unlock(&obj->cob_lock);
		return -DER_STALE;
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
	pthread_rwlock_unlock(&obj->cob_lock);

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
obj_dkey2shard(struct dc_object *obj, daos_key_t *dkey,
	       unsigned int map_ver, uint32_t op)
{
	uint64_t hash;
	int	 grp_idx;

	hash = daos_hash_murmur64((unsigned char *)dkey->iov_buf,
				  dkey->iov_len, 5731);

	grp_idx = obj_dkey2grp(obj, hash, map_ver);
	if (grp_idx < 0)
		return grp_idx;

	return obj_grp_shard_get(obj, grp_idx, hash, map_ver, op);
}

static int
obj_dkey2update_grp(struct dc_object *obj, daos_key_t *dkey,
		    uint32_t map_ver, uint32_t *start_shard, uint32_t *grp_size)
{
	uint64_t hash;
	int	 grp_idx;

	hash = daos_hash_murmur64((unsigned char *)dkey->iov_buf,
				  dkey->iov_len, 5731);

	grp_idx = obj_dkey2grp(obj, hash, map_ver);
	if (grp_idx < 0)
		return grp_idx;

	*grp_size = obj_get_grp_size(obj);
	*start_shard = grp_idx * *grp_size;

	return 0;
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
	D__FREE_PTR(args->info);
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

	/* NB: must call dc_task_get_args instead of tse_task_buf_embedded */
	args = dc_task_get_args(task);
	args->poh = ph;
	D__ALLOC_PTR(args->info);
	if (args->info == NULL)
		D__GOTO(err, rc = -DER_NOMEM);

	obj_addref(obj);
	rc = dc_task_reg_comp_cb(task, obj_pool_query_cb, &obj, sizeof(obj));
	if (rc != 0) {
		obj_decref(obj);
		D__GOTO(err, rc);
	}

	*taskp = task;
	return 0;
err:
	dc_task_decref(task);
	if (args->info)
		D__FREE_PTR(args->info);

	return rc;
}

int
dc_obj_class_register(tse_task_t *task)
{
	D__ERROR("Unsupported API\n");
	tse_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_obj_class_query(tse_task_t *task)
{
	D__ERROR("Unsupported API\n");
	tse_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_obj_class_list(tse_task_t *task)
{
	D__ERROR("Unsupported API\n");
	tse_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_obj_declare(tse_task_t *task)
{
	D__ERROR("Unsupported API\n");
	tse_task_complete(task, -DER_NOSYS);
	return 0;
#if 0
	daos_obj_declare_t	args;
	struct daos_oclass_attr *oc_attr;
	int			rc;

	args = daos_task_get_args(task);
	D__ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

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
	D__ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	obj = obj_alloc();
	if (obj == NULL)
		return -DER_NOMEM;

	obj->cob_coh  = args->coh;
	obj->cob_mode = args->mode;

	pthread_rwlock_init(&obj->cob_lock, NULL);

	/* it is a local operation for now, does not require event */
	rc = dc_obj_fetch_md(args->oid, &obj->cob_md);
	if (rc != 0)
		D__GOTO(out, rc);

	rc = obj_layout_create(obj);
	if (rc != 0)
		D__GOTO(out, rc);

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
	D__ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	obj = obj_hdl2ptr(args->oh);
	if (obj == NULL)
		D__GOTO(out, rc = -DER_NO_HDL);

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
	D__ASSERT(oc_attr != NULL);
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
	D__ERROR("Unsupported API\n");
	tse_task_complete(task, -DER_NOSYS);
	return 0;
}

static int
obj_retry_cb(tse_task_t *task, void *data)
{
	tse_sched_t	 *sched	    = tse_task2sched(task);
	struct dc_object *obj	    = (struct dc_object *)data;
	int		  result    = task->dt_result;
	tse_task_t	 *pool_task;
	int		  rc;

	/** if succeed or no retry, leave */
	if (!obj_retry_error(result))
		return result;

	D__DEBUG(DB_IO, "Retry task=%p for error=%d\n", task, result);

	/* Let's reset task result before retry */
	task->dt_result = 0;

	/* Add pool map update task */
	rc = obj_pool_query_task(sched, obj, &pool_task);
	if (rc != 0)
		D__GOTO(err, rc);

	rc = dc_task_resched(task);
	if (rc != 0) {
		D__ERROR("Failed to re-init task (%p)\n", task);
		D__GOTO(err, rc);
	}

	rc = dc_task_depend(task, 1, &pool_task);
	if (rc != 0) {
		D__ERROR("Failed to add dependency on pool query task (%p)\n",
			pool_task);
		D__GOTO(err, rc);
	}

	/* ignore returned value, error is reported by comp_cb */
	dc_task_schedule(pool_task, true);
	return 0;
err:
	if (pool_task)
		dc_task_decref(pool_task);

	task->dt_result = result; /* restore the orignal error */
	return rc;
}

static int
shard_process_rc(tse_task_t *task, void *arg)
{
	int *result = arg;
	int ret = task->dt_result;

	/* if result is TIMEOUT or STALE, let's keep it, so the
	 * upper layer can refresh the layout.
	 */
	if (obj_retry_error(*result))
		return 0;

	/* Do not miss these error, so the upper layer can refresh
	 * layout anyway
	 */
	if (obj_retry_error(ret)) {
		*result = ret;
		return 0;
	}

	if (*result == 0)
		*result = ret;

	return 0;
}

static int
obj_comp_cb(tse_task_t *task, void *arg)
{
	struct dc_object *obj = *((struct dc_object **)arg);
	int result = 0;

	/* NB: no subtask for fetch so it's noop */
	tse_task_result_process(task, shard_process_rc, &result);
	if (task->dt_result == 0)
		task->dt_result = result;

	if (task->dt_result)
		obj_retry_cb(task, obj);

	obj_decref(obj);
	return 0;
}

int
dc_obj_fetch(tse_task_t *task)
{
	daos_obj_fetch_t	*args = dc_task_get_args(task);
	struct dc_object	*obj;
	unsigned int		map_ver;
	int			shard;
	daos_handle_t		shard_oh;
	int			rc;

	obj = obj_hdl2ptr(args->oh);
	if (obj == NULL)
		D__GOTO(out_task, rc = -DER_NO_HDL);

	rc = tse_task_register_comp_cb(task, obj_comp_cb, &obj,
				       sizeof(obj));
	if (rc != 0) {
		/* NB: process_rc_cb() will release refcount in other cases */
		obj_decref(obj);
		D__GOTO(out_task, rc);
	}

	rc = obj_ptr2pm_ver(obj, &map_ver);
	if (rc)
		D__GOTO(out_task, rc);

	shard = obj_dkey2shard(obj, args->dkey, map_ver, DAOS_OPC_OBJ_UPDATE);
	if (shard < 0)
		D__GOTO(out_task, rc = shard);

	rc = obj_shard_open(obj, shard, map_ver, &shard_oh);
	if (rc != 0)
		D__GOTO(out_task, rc);

	D__DEBUG(DB_IO, "fetch "DF_OID" shard %u\n",
		DP_OID(obj->cob_md.omd_id), shard);
	rc = dc_obj_shard_fetch(shard_oh, args->epoch, args->dkey, args->nr,
				args->iods, args->sgls, args->maps, map_ver,
				task);
	dc_obj_shard_close(shard_oh);
	return rc;

out_task:
	tse_task_complete(task, rc);
	return rc;
}

struct shard_update_args {
	struct dc_object	*obj;
	daos_epoch_t		epoch;
	daos_key_t		*dkey;
	unsigned int		nr;
	daos_iod_t		*iods;
	daos_sg_list_t		*sgls;
	unsigned int		map_ver;
	unsigned int		shard;
	bool			retry;
};

static int
shard_update_task(tse_task_t *task)
{
	struct shard_update_args	*args;
	struct dc_object		*obj;
	daos_handle_t			shard_oh;
	int				rc;

	args = tse_task_buf_embedded(task, sizeof(*args));
	obj = args->obj;

	if (args->shard == 0 &&
	    DAOS_FAIL_CHECK(DAOS_SHARD_OBJ_UPDATE_TIMEOUT_SINGLE)) {
		D__INFO("Set Shard 0 update to return -DER_TIMEOUT\n");
		daos_fail_loc_set(DAOS_SHARD_OBJ_UPDATE_TIMEOUT |
				  DAOS_FAIL_ONCE);
	}

	if (args->retry) {
		rc = obj_ptr2pm_ver(obj, &args->map_ver);
		if (rc) {
			D__ERROR("obj_ptr2pm_ver failed, rc: %d.n", rc);
			return rc;
		}
		args->retry = false;
	}

	rc = obj_shard_open(obj, args->shard, args->map_ver, &shard_oh);
	if (rc != 0) {
		/* skip a failed target */
		if (rc == -DER_NONEXIST) {
			tse_task_complete(task, 0);
			rc = 0;
		}
		return rc;
	}

	rc = dc_obj_shard_update(shard_oh, args->epoch, args->dkey, args->nr,
				 args->iods, args->sgls, args->map_ver, task);

	dc_obj_shard_close(shard_oh);
	return rc;
}

static int
shard_update_cb(tse_task_t *task, void *nouse)
{
	struct shard_update_args *args;

	args = tse_task_buf_embedded(task, sizeof(*args));
	if (task->dt_result) {
		args->retry = true;
		obj_retry_cb(task, args->obj);
	}

	return 0;
}

int
dc_obj_update(tse_task_t *task)
{
	daos_obj_update_t	*args = dc_task_get_args(task);
	tse_sched_t		*sched = tse_task2sched(task);
	struct dc_object	*obj;
	daos_list_t		head;
	unsigned int		shard;
	unsigned int		shards_cnt;
	unsigned int		map_ver;
	int			i;
	int			rc;

	obj = obj_hdl2ptr(args->oh);
	if (obj == NULL)
		D__GOTO(out_task, rc = -DER_NO_HDL);

	rc = tse_task_register_comp_cb(task, obj_comp_cb, &obj,
				       sizeof(obj));
	if (rc != 0) {
		/* NB: process_rc_cb() will release refcount in other cases */
		obj_decref(obj);
		D__GOTO(out_task, rc);
	}

	rc = obj_ptr2pm_ver(obj, &map_ver);
	if (rc)
		D__GOTO(out_task, rc);

	rc = obj_dkey2update_grp(obj, args->dkey, map_ver, &shard, &shards_cnt);
	if (rc != 0)
		D__GOTO(out_task, rc);

	D__DEBUG(DB_IO, "update "DF_OID" start %u cnt %u\n",
		DP_OID(obj->cob_md.omd_id), shard, shards_cnt);

	DAOS_INIT_LIST_HEAD(&head);
	for (i = 0; i < shards_cnt; i++, shard++) {
		tse_task_t		 *shard_task;
		struct shard_update_args *shard_arg;

		rc = tse_task_create(shard_update_task, sched, NULL,
				     &shard_task);
		if (rc != 0)
			D__GOTO(out_task, rc);

		shard_arg = tse_task_buf_embedded(shard_task,
						  sizeof(*shard_arg));
		/* share the refcount taken by obj_comp_cb */
		shard_arg->obj	   = obj;
		shard_arg->epoch   = args->epoch;
		shard_arg->dkey	   = args->dkey;
		shard_arg->nr	   = args->nr;
		shard_arg->iods	   = args->iods;
		shard_arg->sgls	   = args->sgls;
		shard_arg->map_ver = map_ver;
		shard_arg->shard   = shard;
		shard_arg->retry   = false;

		/* Register retry CB.
		 * NB: share the obj refcount taken by obj_comp_cb
		 * which is guaranteed to be called at the end.
		 */
		rc = tse_task_register_comp_cb(shard_task, shard_update_cb,
					       NULL, 0);
		if (rc != 0) {
			tse_task_complete(task, rc);
			D__GOTO(out_task, rc);
		}

		rc = tse_task_add_dependent(task, shard_task);
		if (rc != 0) {
			tse_task_complete(shard_task, rc);
			D__GOTO(out_task, rc);
		}
		tse_task_list_add(shard_task, &head);
	}
	tse_task_list_sched(&head, true);
	return 0;
out_task:
	if (daos_list_empty(&head))
		tse_task_complete(task, rc);
	else
		tse_task_list_abort(&head, rc);
	return rc;
}

struct obj_list_arg {
	struct dc_object *obj;
	daos_hash_out_t	 *anchor;
	unsigned int	 single_shard:1;
	int		 opc;
};

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
	D__ASSERT(grp_size > 0);

	if (!daos_hash_is_eof(anchor)) {
		D__DEBUG(DB_IO, "More keys in shard %d\n", shard);
	} else if ((shard < obj->cob_layout->ol_nr - grp_size) &&
		   !arg->single_shard) {
		shard += grp_size;
		D__DEBUG(DB_IO, "next shard %d grp %d nr %u\n",
			shard, grp_size, obj->cob_layout->ol_nr);

		enum_anchor_reset_hkey(anchor);
		enum_anchor_set_tag(anchor, 0);
		dc_obj_shard2anchor(anchor, shard);
	} else {
		D__DEBUG(DB_IO, "Enumerated All shards\n");
	}
}

static int
obj_list_comp_cb(tse_task_t *task, void *data)
{
	struct obj_list_arg	*arg = data;

	switch (arg->opc) {
	default:
		D__ASSERT(0);
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
		obj_list_dkey_cb(task, arg);
		break;

	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
		if (daos_hash_is_eof(arg->anchor))
			D__DEBUG(DB_IO, "Enumerated completed\n");
		break;
	}

	if (task->dt_result)
		obj_retry_cb(task, arg->obj);

	obj_decref(arg->obj);
	return 0;
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
	unsigned int		map_ver;
	struct obj_list_arg	list_args;
	daos_handle_t		shard_oh;
	int			shard;
	int			rc;

	if (nr == NULL || *nr == 0) {
		D__DEBUG(DB_IO, "Invalid API parameter.\n");
		D__GOTO(out_task, rc = -DER_INVAL);
	}

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		D__GOTO(out_task, rc = -DER_NO_HDL);

	list_args.obj = obj;
	list_args.anchor = anchor;
	list_args.single_shard = single_shard;
	list_args.opc = op;

	rc = tse_task_register_comp_cb(task, obj_list_comp_cb,
				       &list_args, sizeof(list_args));
	if (rc != 0) {
		/* NB: process_rc_cb() will release refcount in other cases */
		obj_decref(obj);
		D__GOTO(out_task, rc);
	}

	rc = obj_ptr2pm_ver(obj, &map_ver);
	if (rc)
		D__GOTO(out_task, rc);

	if (op == DAOS_OBJ_DKEY_RPC_ENUMERATE) {
		shard = dc_obj_anchor2shard(anchor);
		shard = obj_grp_valid_shard_get(obj, shard, map_ver, op);
		if (shard < 0)
			D__GOTO(out_task, rc = shard);

		dc_obj_shard2anchor(anchor, shard);
	} else {
		shard = obj_dkey2shard(obj, dkey, map_ver, op);
		if (shard < 0)
			D__GOTO(out_task, rc = shard);

		dc_obj_shard2anchor(anchor, shard);
	}

	/** object will be decref by task complete cb */
	rc = obj_shard_open(obj, shard, map_ver, &shard_oh);
	if (rc != 0)
		D__GOTO(out_task, rc);

	if (op == DAOS_OBJ_RECX_RPC_ENUMERATE)
		rc = dc_obj_shard_list_rec(shard_oh, op, epoch, dkey, akey,
					   type, size, nr, recxs, eprs,
					   cookies, versions, anchor, map_ver,
					   incr_order, task);
	else
		rc = dc_obj_shard_list_key(shard_oh, op, epoch, dkey, nr,
					   kds, sgl, anchor, map_ver, task);

	D__DEBUG(DB_IO, "Enumerate keys in shard %d: rc %d\n", shard, rc);
	dc_obj_shard_close(shard_oh);

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
	D__ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

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
	D__ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

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
	D__ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

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
	D__ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return dc_obj_list_internal(args->oh, DAOS_OBJ_DKEY_RPC_ENUMERATE,
				    args->epoch, NULL, NULL, DAOS_IOD_NONE,
				    NULL, args->nr, args->kds, args->sgl, NULL,
				    NULL, NULL, NULL, args->anchor, true, true,
				    task);
}

static int
key_punch_comp_cb(tse_task_t *task, void *data)
{
	struct tsa_key_punch	*args;

	args = tse_task_buf_embedded(task, sizeof(*args));

	D_ASSERT(args->pa_shard);
	obj_shard_decref(args->pa_shard);
	args->pa_shard = NULL;

	if (args->pa_rpc) { /* -1 for RPC created by object shard */
		if (task->dt_result == 0)
			task->dt_result = obj_reply_get_status(args->pa_rpc);

		crt_req_decref(args->pa_rpc);
		args->pa_rpc = NULL;
	}
	return obj_retry_cb(task, args->pa_obj);
}

static int
key_punch(tse_task_t *api_task, enum obj_rpc_opc opc,
	  daos_obj_punch_key_t *api_args)
{
	tse_sched_t	   *sched = tse_task2sched(api_task);
	struct dc_object   *obj;
	daos_list_t	    head;
	daos_handle_t	    coh;
	uuid_t		    coh_uuid;
	uuid_t		    cont_uuid;
	unsigned int	    shard_first;
	unsigned int	    shard_nr;
	unsigned int	    map_ver;
	int		    i = 0;
	int		    rc;

	/** Register retry CB */
	obj = obj_hdl2ptr(api_args->oh);
	if (!obj)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	rc = tse_task_register_comp_cb(api_task, obj_comp_cb, &obj,
				       sizeof(obj));
	if (rc) {
		/* NB: process_rc_cb() will release refcount in other cases */
		obj_decref(obj);
		D_GOTO(out_task, rc);
	}

	coh = obj_hdl2cont_hdl(api_args->oh);
	if (daos_handle_is_inval(coh))
		D_GOTO(out_task, rc = -DER_NO_HDL);

	rc = dc_cont_hdl2uuid(coh, &coh_uuid, &cont_uuid);
	if (rc != 0)
		D_GOTO(out_task, rc);

	rc = obj_ptr2pm_ver(obj, &map_ver);
	if (rc)
		D_GOTO(out_task, rc);

	rc = obj_dkey2update_grp(obj, api_args->dkey, map_ver,
				 &shard_first, &shard_nr);
	if (rc != 0)
		D_GOTO(out_task, rc);

	D__DEBUG(DB_IO, "punch "DF_OID" start %u cnt %u\n",
		 DP_OID(obj->cob_md.omd_id), shard_first, shard_nr);

	DAOS_INIT_LIST_HEAD(&head);
	for (i = 0; i < shard_nr; i++) {
		tse_task_t		*task;
		struct tsa_key_punch	*args;
		daos_handle_t		 soh;

		rc = obj_shard_open(obj, shard_first + i, map_ver, &soh);
		if (rc == -DER_NONEXIST) {
			rc = 0;
			continue; /* skip a failed target */
		}

		if (rc != 0)
			D_GOTO(out_task, rc);

		rc = tse_task_create(dc_shard_key_punch, sched, NULL, &task);
		if (rc != 0)
			D_GOTO(out_task, rc);

		args = tse_task_buf_embedded(task, sizeof(*args));
		args->pa_api_args = api_args;
		args->pa_opc	  = opc;
		/* obj_process_rc_cb has taken refcount, which is called
		 * at the end.
		 */
		args->pa_obj	  = obj;
		args->pa_shard	  = obj_shard_hdl2ptr(soh);
		args->pa_mapv	  = map_ver;
		uuid_copy(args->pa_coh_uuid, coh_uuid);
		uuid_copy(args->pa_cont_uuid, cont_uuid);

		/** Register retry CB */
		rc = tse_task_register_comp_cb(task, key_punch_comp_cb,
					       NULL, 0);
		if (rc != 0) {
			tse_task_complete(task, rc);
			D_GOTO(out_task, rc);
		}

		rc = tse_task_add_dependent(api_task, task);
		if (rc != 0) {
			tse_task_complete(task, rc);
			D_GOTO(out_task, rc);
		}
		tse_task_list_add(task, &head);
	}

	tse_task_list_sched(&head, true);
	D_EXIT;
out:
	return rc;
out_task:
	if (daos_list_empty(&head)) /* nothing has been started */
		tse_task_complete(api_task, rc);
	else
		tse_task_list_abort(&head, rc);

	D_GOTO(out, rc);
}

struct obj_punch_args {
	crt_rpc_t	*rpc;
	daos_handle_t	*hdlp;
};

static int
dc_punch_cb(tse_task_t *task, void *arg)
{
	struct obj_punch_args	*punch_args = (struct obj_punch_args *)arg;
	struct obj_punch_in	*opi;
	uint32_t		 opc;
	int                      ret = task->dt_result;
	int			 rc = 0;

	opc = opc_get(punch_args->rpc->cr_opc);

	opi = crt_req_get(punch_args->rpc);
	D__ASSERT(opi != NULL);
	if (ret != 0) {
		D__ERROR("RPC %d failed: %d\n", opc, ret);
		D__GOTO(out, ret);
	}

	rc = obj_reply_get_status(punch_args->rpc);
	if (rc != 0) {
		D__ERROR("rpc %p RPC %d failed: %d\n", punch_args->rpc, opc, rc);
		D__GOTO(out, rc);
	}

out:
	crt_req_decref(punch_args->rpc);
	dc_pool_put((struct dc_pool *)punch_args->hdlp);

	if (ret == 0 || obj_retry_error(rc))
		ret = rc;
	return ret;
}

static int
dc_obj_punch_int(daos_handle_t oh, enum obj_rpc_opc opc, daos_epoch_t epoch,
		 uint32_t nr_dkeys, daos_key_t *dkeys, uint32_t nr_akeys,
		 daos_key_t *akeys, tse_task_t *task)
{
	crt_endpoint_t		tgt_ep;
	struct dc_pool		*pool;
	struct dc_object	*obj;
	crt_rpc_t		*req;
	uuid_t			cont_hdl_uuid;
	uuid_t			cont_uuid;
	struct obj_punch_in	*opi;
	struct obj_punch_args	punch_args;
	unsigned int		map_ver;
	daos_handle_t		ch;
	int			rc;

	obj = obj_hdl2ptr(oh);
	if (!obj)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	rc = obj_ptr2pm_ver(obj, &map_ver);
	if (rc)
		D__GOTO(out_task, rc);

	ch = obj_hdl2cont_hdl(oh);
	if (daos_handle_is_inval(ch))
		D__GOTO(out_task, rc = -DER_NO_HDL);

	rc = dc_cont_hdl2uuid(ch, &cont_hdl_uuid, &cont_uuid);
	if (rc != 0)
		D__GOTO(out_task, rc);

	pool = dc_hdl2pool(dc_cont_hdl2pool_hdl(ch));
	if (pool == NULL)
		D__GOTO(out_task, rc = -DER_NO_HDL);

	tgt_ep.ep_grp = pool->dp_group;
	pthread_mutex_lock(&pool->dp_client_lock);
	rsvc_client_choose(&pool->dp_client, &tgt_ep);
	pthread_mutex_unlock(&pool->dp_client_lock);

	rc = obj_req_create(daos_task2ctx(task), &tgt_ep, opc, &req);
	if (rc != 0)
		D__GOTO(out_pool, rc);

	opi = crt_req_get(req);
	D__ASSERT(opi != NULL);

	uuid_copy(opi->opi_co_hdl, cont_hdl_uuid);
	uuid_copy(opi->opi_co_uuid, cont_uuid);

	opi->opi_map_ver	 = map_ver;
	opi->opi_epoch		 = epoch;
	opi->opi_dkeys.da_count	 = nr_dkeys;
	opi->opi_dkeys.da_arrays = dkeys;
	opi->opi_akeys.da_count	 = nr_akeys;
	opi->opi_akeys.da_arrays = akeys;

	crt_req_addref(req);
	punch_args.rpc = req;
	punch_args.hdlp = (daos_handle_t *)pool;
	rc = tse_task_register_comp_cb(task, dc_punch_cb, &punch_args,
				       sizeof(punch_args));
	if (rc != 0)
		D__GOTO(out_args, rc);

	rc = daos_rpc_send(req, task);
	if (rc != 0) {
		D__ERROR("update/fetch rpc failed rc %d\n", rc);
		D__GOTO(out_args, rc);
	}

	return rc;

out_args:
	crt_req_decref(req);
	crt_req_decref(req);
out_pool:
	dc_pool_put(pool);
out_task:
	tse_task_complete(task, rc);
	return rc;
}

int
dc_obj_punch(tse_task_t *task)
{
	daos_obj_punch_t	*args;

	args = dc_task_get_args(task);
	D__ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return dc_obj_punch_int(args->oh, DAOS_OBJ_RPC_PUNCH, args->epoch,
				0, NULL, 0, NULL, task);
}

int
dc_obj_punch_dkeys(tse_task_t *task)
{
	daos_obj_punch_key_t	*args;

	args = dc_task_get_args(task);
	D__ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return key_punch(task, DAOS_OBJ_RPC_PUNCH_DKEYS, args);
}
int
dc_obj_punch_akeys(tse_task_t *task)
{
	daos_obj_punch_key_t	*args;

	args = dc_task_get_args(task);
	D__ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return key_punch(task, DAOS_OBJ_RPC_PUNCH_AKEYS, args);
}
