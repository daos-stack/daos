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
#define DD_SUBSYS	DD_FAC(object)

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

	D_ALLOC_PTR(obj);
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
		D_FREE(obj->cob_mohs, layout->ol_nr * sizeof(*obj->cob_mohs));
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
obj_shard_open(struct dc_object *obj, unsigned int shard, daos_handle_t *oh,
	       unsigned int map_ver)
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
	D_ASSERT(layout->ol_shards[shard].po_shard == shard);

	D_DEBUG(DB_IO, "Open object shard %d\n", shard);
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
		D_ASSERT(shard_obj != NULL);
	}

	pthread_rwlock_unlock(&obj->cob_lock);

	return rc;
}

int
obj_fetch_md(daos_obj_id_t oid, struct daos_obj_md *md, daos_event_t *ev)
{
	/* For predefined object classes, do nothing at here. But for those
	 * customized classes, we need to fetch for the remote OI table.
	 */
	memset(md, 0, sizeof(*md));
	md->omd_id = oid;
	return 0;
}

static int
obj_layout_create(struct dc_object *obj)
{
	struct pl_obj_layout	*layout;
	struct pl_map		*map;
	int			 i;
	int			 nr;
	int			 rc;

	map = pl_map_find(obj->cob_coh, obj->cob_md.omd_id);
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

	D_ASSERT(obj->cob_mohs == NULL);
	D_ALLOC(obj->cob_mohs, nr * sizeof(*obj->cob_mohs));
	if (obj->cob_mohs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	for (i = 0; i < nr; i++)
		obj->cob_mohs[i] = DAOS_HDL_INVAL;

out:
	return rc;
}

static int
dc_obj_layout_refresh(daos_handle_t oh)
{
	struct dc_object *obj;
	int rc;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		return -DER_NO_HDL;

	pthread_rwlock_wrlock(&obj->cob_lock);
	obj_layout_free(obj);

	rc = obj_layout_create(obj);
	pthread_rwlock_unlock(&obj->cob_lock);
	obj_decref(obj);

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

	pthread_rwlock_rdlock(&obj->cob_lock);
	if (obj->cob_layout->ol_ver != map_ver) {
		pthread_rwlock_unlock(&obj->cob_lock);
		return -DER_STALE;
	}

	D_ASSERT(obj->cob_layout->ol_nr >= grp_size);

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
	int i;

	grp_size = obj_get_grp_size(obj);
	D_ASSERT(grp_size > 0);

	idx_first = (idx / grp_size) * grp_size;
	idx_last = idx_first + grp_size - 1;

	D_ASSERT(obj->cob_layout->ol_nr > 0);
	D_ASSERTF(idx_last < obj->cob_layout->ol_nr,
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
		    obj->cob_layout->ol_shards[idx].po_rebuilding)
			continue;

		if (obj->cob_layout->ol_shards[idx].po_shard != -1)
			break;
	}
	pthread_rwlock_unlock(&obj->cob_lock);

	if (i == grp_size) {
		if (op == DAOS_OBJ_RPC_UPDATE)
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
		    uint32_t *start_shard, uint32_t *grp_size,
		    unsigned int map_ver)
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
dc_obj_update_result(struct daos_task *task, void *arg)
{
	int *result = arg;
	int ret = task->dt_result;

	/* if result is TIMEOUT or STALE, let's keep it, so the
	 * upper layer can refresh the layout.
	 **/
	if (daos_obj_retry_error(*result))
		return 0;

	/* Do not miss these error, so the upper layer can refresh
	 * layout anyway
	 **/
	if (daos_obj_retry_error(ret)) {
		*result = ret;
		return 0;
	}

	if (*result == 0)
		*result = ret;

	return 0;
}

static int
dc_obj_process_rc_cb(struct daos_task *task, void *arg)
{
	struct dc_object *obj = *((struct dc_object **)arg);
	int result = 0;

	daos_task_result_process(task, dc_obj_update_result, &result);
	if (task->dt_result == 0)
		task->dt_result = result;

	obj_decref(obj);

	return 0;
}

static int
dc_obj_pool_hdl_get(daos_handle_t oh, daos_handle_t *ph)
{
	daos_handle_t   ch;

	ch = dc_obj_hdl2cont_hdl(oh);
	if (daos_handle_is_inval(ch))
		return -DER_NO_HDL;

	*ph = dc_cont_hdl2pool_hdl(ch);
	if (daos_handle_is_inval(*ph))
		return -DER_NO_HDL;

	return 0;
}

/* Get pool map version from object handle */
static int
dc_obj_pool_map_version_get(daos_handle_t oh, unsigned int *map_ver)
{
	daos_handle_t	ph;
	int		rc;

	rc = dc_obj_pool_hdl_get(oh, &ph);
	if (rc != 0)
		return rc;

	rc = dc_pool_map_version_get(ph, map_ver);

	return rc;
}

static int
pool_query_comp_cb(struct daos_task *task, void *data)
{
	daos_pool_query_t	*args;
	daos_handle_t		*oh = (daos_handle_t *)data;

	args = daos_task_get_args(DAOS_OPC_POOL_QUERY, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	dc_obj_layout_refresh(*oh);

	D_FREE_PTR(args->info);
	return 0;
}

static int
dc_obj_pool_query(struct daos_task **taskp, struct daos_sched *sched,
		  daos_handle_t *oh)
{
	daos_pool_query_t	args;
	daos_handle_t		ph;
	int			rc = 0;

	rc = dc_obj_pool_hdl_get(*oh, &ph);
	if (rc != 0)
		return rc;

	args.tgts = NULL;
	D_ALLOC_PTR(args.info);
	if (args.info == NULL)
		return -DER_NOMEM;
	args.poh = ph;

	rc = daos_task_create(DAOS_OPC_POOL_QUERY, sched, &args, 0, NULL,
			      taskp);
	if (rc != 0)
		return rc;

	rc = daos_task_register_comp_cb(*taskp, pool_query_comp_cb, oh,
					sizeof(*oh));
	if (rc != 0)
		D_GOTO(err, rc);

	return rc;

err:
	if (args.info)
		D_FREE_PTR(args.info);
	return rc;
}

static int
dc_obj_retry_cb(struct daos_task *task, void *data)
{
	struct daos_sched	*sched = daos_task2sched(task);
	daos_handle_t		*oh = (daos_handle_t *)data;
	struct daos_task	*pool_task;
	int			 rc = task->dt_result;

	/** if succeed or no retry, leave */
	if (rc == 0 || !daos_obj_retry_error(rc))
		return rc;
	D_DEBUG(DB_IO, "task %p OBJ IO fails %d let's retry.\n",
		task, rc);

	/* Let's reset task result before retry */
	task->dt_result = 0;

	/* Add pool map update task */
	rc = dc_obj_pool_query(&pool_task, sched, oh);
	if (rc != 0) {
		D_FREE_PTR(pool_task);
		D_GOTO(out, rc);
	}

	rc = daos_task_reinit(task);
	if (rc != 0) {
		D_ERROR("Failed to re-init task (%p)\n", task);
		D_GOTO(out, rc);
	}

	rc = daos_task_register_deps(task, 1, &pool_task);
	if (rc != 0) {
		D_ERROR("Failed to add dependency on pool query task (%p)\n",
			pool_task);
		D_GOTO(out, rc);
	}

	rc = daos_task_schedule(pool_task, true);
	if (rc != 0)
		D_GOTO(out, rc);
out:
	return rc;
}

int
dc_obj_class_register(struct daos_task *task)
{
	D_ERROR("Unsupported API\n");
	daos_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_obj_class_query(struct daos_task *task)
{
	D_ERROR("Unsupported API\n");
	daos_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_obj_class_list(struct daos_task *task)
{
	D_ERROR("Unsupported API\n");
	daos_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_obj_declare(struct daos_task *task)
{
	D_ERROR("Unsupported API\n");
	daos_task_complete(task, -DER_NOSYS);
	return 0;
#if 0
	daos_obj_declare_t	args;
	struct daos_oclass_attr *oc_attr;
	int			rc;

	args = daos_task_get_args(DAOS_OPC_OBJ_DECLARE, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	/* XXX Only support internal classes for now */
	oc_attr = daos_oclass_attr_find(args->oid);
	rc = oc_attr != NULL ? 0 : -DER_INVAL;

	daos_task_complete(task, rc);
	return rc;
#endif
}

int
dc_obj_open(struct daos_task *task)
{
	daos_obj_open_t		*args;
	struct dc_object	*obj;
	int			rc;

	args = daos_task_get_args(DAOS_OPC_OBJ_OPEN, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	obj = obj_alloc();
	if (obj == NULL)
		return -DER_NOMEM;

	obj->cob_coh  = args->coh;
	obj->cob_mode = args->mode;

	pthread_rwlock_init(&obj->cob_lock, NULL);

	/* it is a local operation for now, does not require event */
	rc = obj_fetch_md(args->oid, &obj->cob_md, NULL);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = obj_layout_create(obj);
	if (rc != 0)
		D_GOTO(out, rc);

	obj_hdl_link(obj);
	*args->oh = obj_ptr2hdl(obj);

out:
	obj_decref(obj);
	daos_task_complete(task, rc);
	return rc;
}

int
dc_obj_close(struct daos_task *task)
{
	daos_obj_close_t	*args;
	struct dc_object	*obj;
	int			 rc = 0;

	args = daos_task_get_args(DAOS_OPC_OBJ_CLOSE, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	obj = obj_hdl2ptr(args->oh);
	if (obj == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	obj_hdl_unlink(obj);
	obj_decref(obj);

out:
	daos_task_complete(task, rc);
	return 0;
}

int
dc_obj_punch(struct daos_task *task)
{
	D_ERROR("Unsupported API\n");
	daos_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_obj_query(struct daos_task *task)
{
	D_ERROR("Unsupported API\n");
	daos_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_obj_fetch(struct daos_task *task)
{
	daos_obj_fetch_t	*args;
	daos_handle_t		oh;
	struct dc_object	*obj;
	unsigned int		map_ver;
	int			shard;
	daos_handle_t		shard_oh;
	int			rc;

	args = daos_task_get_args(DAOS_OPC_OBJ_FETCH, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");
	oh = args->oh;

	/** Register retry CB */
	rc = daos_task_register_comp_cb(task, dc_obj_retry_cb, &oh, sizeof(oh));
	if (rc != 0)
		D_GOTO(out_task, rc);

	rc = dc_obj_pool_map_version_get(oh, &map_ver);
	if (rc)
		D_GOTO(out_task, rc);

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	rc = daos_task_register_comp_cb(task, dc_obj_process_rc_cb, &obj,
					sizeof(obj));
	if (rc != 0) {
		/* NB: process_rc_cb() will release refcount in other cases */
		obj_decref(obj);
		D_GOTO(out_task, rc);
	}

	shard = obj_dkey2shard(obj, args->dkey, map_ver, DAOS_OPC_OBJ_UPDATE);
	if (shard < 0)
		D_GOTO(out_task, rc = shard);

	rc = obj_shard_open(obj, shard, &shard_oh, map_ver);
	if (rc != 0)
		D_GOTO(out_task, rc);

	D_DEBUG(DB_IO, "fetch "DF_OID" shard %u\n",
		DP_OID(obj->cob_md.omd_id), shard);
	rc = dc_obj_shard_fetch(shard_oh, args->epoch, args->dkey, args->nr,
				args->iods, args->sgls, args->maps, map_ver,
				task);
	dc_obj_shard_close(shard_oh);
	return rc;

out_task:
	daos_task_complete(task, rc);
	return rc;
}

struct daos_obj_shard_args {
	daos_handle_t		*oh;
	daos_epoch_t		epoch;
	daos_key_t		*dkey;
	unsigned int		nr;
	daos_iod_t		*iods;
	daos_sg_list_t		*sgls;
	unsigned int		map_ver;
	unsigned int		shard;
};

static int
dc_obj_shard_task_update(struct daos_task *task)
{
	struct daos_obj_shard_args	*args = daos_task2arg(task);
	struct dc_object		*obj;
	daos_handle_t			shard_oh;
	unsigned int			shard, shards_cnt;
	int				rc;

	/** Register retry CB */
	rc = daos_task_register_comp_cb(task, dc_obj_retry_cb, args->oh,
					sizeof(args->oh));
	if (rc != 0)
		return rc;

	if (args->shard == 0 &&
	    DAOS_FAIL_CHECK(DAOS_SHARD_OBJ_UPDATE_TIMEOUT_SINGLE)) {
		D_INFO("Set Shard 0 update to return -DER_TIMEOUT\n");
		daos_fail_loc_set(DAOS_SHARD_OBJ_UPDATE_TIMEOUT |
				  DAOS_FAIL_ONCE);
	}

	rc = dc_obj_pool_map_version_get(*args->oh, &args->map_ver);
	if (rc != 0)
		return rc;

	obj = obj_hdl2ptr(*args->oh);
	if (obj == NULL)
		return -DER_NO_HDL;

	rc = obj_dkey2update_grp(obj, args->dkey, &shard, &shards_cnt,
				 args->map_ver);
	if (rc != 0) {
		obj_decref(obj);
		return rc;
	}

	rc = obj_shard_open(obj, args->shard, &shard_oh, args->map_ver);
	if (rc != 0) {
		obj_decref(obj);
		/* skip a failed target */
		if (rc == -DER_NONEXIST) {
			daos_task_complete(task, 0);
			rc = 0;
		}
		return rc;
	}

	rc = dc_obj_shard_update(shard_oh, args->epoch, args->dkey, args->nr,
				 args->iods, args->sgls, args->map_ver, task);

	dc_obj_shard_close(shard_oh);

	obj_decref(obj);
	return rc;
}

#define MAX_TMP_SHARDS	6
int
dc_obj_update(struct daos_task *task)
{
	daos_obj_update_t	*args;
	unsigned int		map_ver;
	struct daos_sched	*sched = daos_task2sched(task);
	struct dc_object	*obj;
	unsigned int		shard;
	unsigned int		shards_cnt = 0;
	struct daos_task	*tmp_tasks[MAX_TMP_SHARDS];
	struct daos_task	**shard_tasks = NULL;
	int			i;
	int			rc = 0;

	args = daos_task_get_args(DAOS_OPC_OBJ_UPDATE, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	/** Register retry CB */
	rc = daos_task_register_comp_cb(task, dc_obj_retry_cb, &args->oh,
					sizeof(args->oh));
	if (rc != 0)
		D_GOTO(out_task, rc);

	rc = dc_obj_pool_map_version_get(args->oh, &map_ver);
	if (rc)
		D_GOTO(out_task, rc);

	obj = obj_hdl2ptr(args->oh);
	if (obj == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	rc = daos_task_register_comp_cb(task, dc_obj_process_rc_cb, &obj,
					sizeof(obj));
	if (rc != 0) {
		/* NB: process_rc_cb() will release refcount in other cases */
		obj_decref(obj);
		D_GOTO(out_task, rc);
	}

	rc = obj_dkey2update_grp(obj, args->dkey, &shard, &shards_cnt, map_ver);
	if (rc != 0)
		D_GOTO(out_task, rc);

	D_DEBUG(DB_IO, "update "DF_OID" start %u cnt %u\n",
		DP_OID(obj->cob_md.omd_id), shard, shards_cnt);

	/** for one shard, continue with the same task */
	if (shards_cnt == 1) {
		daos_handle_t shard_oh;

		rc = obj_shard_open(obj, shard, &shard_oh, map_ver);
		if (rc != 0)
			D_GOTO(out_task, rc);

		rc = dc_obj_shard_update(shard_oh, args->epoch, args->dkey,
					 args->nr, args->iods, args->sgls,
					 map_ver, task);
		dc_obj_shard_close(shard_oh);

		return rc;
	}

	/** For multiple shards, create 1 task for each shard I/O */
	if (shards_cnt > MAX_TMP_SHARDS) {
		D_ALLOC(shard_tasks, sizeof(*shard_tasks) * shards_cnt);
		if (shard_tasks == NULL)
			D_GOTO(out_task, rc = -DER_NOMEM);
	} else {
		shard_tasks = tmp_tasks;
	}

	for (i = 0; i < shards_cnt; i++, shard++) {
		struct daos_task *shard_task = NULL;
		struct daos_obj_shard_args shard_arg;

		shard_arg.oh = &args->oh;
		shard_arg.epoch = args->epoch;
		shard_arg.dkey = args->dkey;
		shard_arg.nr = args->nr;
		shard_arg.iods = args->iods;
		shard_arg.sgls = args->sgls;
		shard_arg.map_ver = map_ver;
		shard_arg.shard = shard;
		rc = daos_task_init(&shard_task, dc_obj_shard_task_update,
				    &shard_arg, sizeof(shard_arg), sched);
		if (rc != 0)
			break;

		rc = daos_task_add_dependent(task, shard_task);
		if (rc != 0) {
			D_ERROR("Failed to add dependency on shard task %p\n",
				shard_task);
			break;
		}

		shard_tasks[i] = shard_task;
	}

	if (rc == 0) {
		for (i = 0; i < shards_cnt; i++) {
			rc = daos_task_schedule(shard_tasks[i], true);
			if (rc != 0)
				break;
		}
	}

	if (rc != 0) {
		int real_shards = i;

		/** If something failed, complete inflight shard tasks */
		if (real_shards > 0) {
			for (i = 0; i < real_shards; i++)
				daos_task_complete(shard_tasks[i], rc);
			D_GOTO(out_free, rc);
		} else {
			D_GOTO(out_task, rc);
		}
	}

out_free:
	if (shard_tasks != NULL && shard_tasks != tmp_tasks)
		D_FREE(shard_tasks, sizeof(*shard_tasks) * shards_cnt);

	return rc;

out_task:
	daos_task_complete(task, rc);
	goto out_free;
}

struct dc_obj_list_arg {
	struct dc_object *obj;
	daos_hash_out_t	 *anchor;
	unsigned int	 single_shard:1;
};

/** completion callback for akey/recx enumeration */
static int
dc_obj_list_cb(struct daos_task *task, void *data)
{
	struct dc_obj_list_arg *arg = data;
	daos_hash_out_t *anchor = arg->anchor;
	int rc = task->dt_result;

	if (rc != 0)
		return rc;

	if (daos_hash_is_eof(anchor))
		D_DEBUG(DB_IO, "Enumerated All shards\n");

	obj_decref(arg->obj);
	return rc;
}

static int
dc_obj_list_dkey_cb(struct daos_task *task, void *data)
{
	struct dc_obj_list_arg *arg = (struct dc_obj_list_arg *)data;
	struct dc_object       *obj = arg->obj;
	daos_hash_out_t	       *anchor = arg->anchor;
	uint32_t		shard = enum_anchor_get_shard(anchor);
	int			grp_size;
	int			rc = task->dt_result;

	if (rc != 0)
		return rc;

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
		enum_anchor_set_shard(anchor, shard);
	} else {
		D_DEBUG(DB_IO, "Enumerated All shards\n");
	}

	obj_decref(obj);

	return rc;
}

static daos_task_cb_t
obj_list_opc2comp_cb(uint32_t opc)
{
	switch (opc) {
	default:
		D_ASSERT(0);
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
		return dc_obj_list_dkey_cb;
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
		return dc_obj_list_cb;
	}
}

static int
dc_obj_list_internal(daos_handle_t oh, uint32_t op, daos_epoch_t epoch,
		     daos_key_t *dkey, daos_key_t *akey, daos_iod_type_t type,
		     daos_size_t *size, uint32_t *nr, daos_key_desc_t *kds,
		     daos_sg_list_t *sgl, daos_recx_t *recxs,
		     daos_epoch_range_t *eprs, uuid_t *cookies,
		     daos_hash_out_t *anchor, bool incr_order,
		     bool single_shard, struct daos_task *task)
{
	struct dc_object	*obj = NULL;
	unsigned int		map_ver;
	struct dc_obj_list_arg	list_args;
	daos_handle_t		shard_oh;
	int			shard;
	int			rc;

	if (nr == NULL || *nr == 0) {
		D_DEBUG(DB_IO, "Invalid API parameter.\n");
		D_GOTO(out_task, rc = -DER_INVAL);
	}

	/** Register retry CB */
	rc = daos_task_register_comp_cb(task, dc_obj_retry_cb, &oh, sizeof(oh));
	if (rc != 0)
		D_GOTO(out_task, rc);

	rc = dc_obj_pool_map_version_get(oh, &map_ver);
	if (rc)
		D_GOTO(out_task, rc);

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	list_args.obj = obj;
	list_args.anchor = anchor;
	list_args.single_shard = single_shard;

	rc = daos_task_register_comp_cb(task, obj_list_opc2comp_cb(op),
					&list_args, sizeof(list_args));
	if (rc != 0) {
		/* NB: process_rc_cb() will release refcount in other cases */
		obj_decref(obj);
		D_GOTO(out_task, rc);
	}

	if (op == DAOS_OBJ_DKEY_RPC_ENUMERATE) {
		shard = enum_anchor_get_shard(anchor);
		shard = obj_grp_valid_shard_get(obj, shard, map_ver, op);
		if (shard < 0)
			D_GOTO(out_task, rc = shard);

		enum_anchor_set_shard(anchor, shard);
	} else {
		shard = obj_dkey2shard(obj, dkey, map_ver, op);
		if (shard < 0)
			D_GOTO(out_task, rc = shard);

		enum_anchor_set_shard(anchor, shard);
	}

	/** object will be decref by task complete cb */
	rc = obj_shard_open(obj, shard, &shard_oh, map_ver);
	if (rc != 0)
		D_GOTO(out_task, rc);

	if (op == DAOS_OBJ_RECX_RPC_ENUMERATE)
		rc = dc_obj_shard_list_rec(shard_oh, op, epoch, dkey, akey,
					   type, size, nr, recxs, eprs,
					   cookies, anchor, map_ver, incr_order,
					   task);
	else
		rc = dc_obj_shard_list_key(shard_oh, op, epoch, dkey, nr,
					   kds, sgl, anchor, map_ver, task);

	D_DEBUG(DB_IO, "Enumerate keys in shard %d: rc %d\n", shard, rc);
	dc_obj_shard_close(shard_oh);

	return rc;

out_task:
	daos_task_complete(task, rc);
	return rc;
}

int
dc_obj_list_dkey(struct daos_task *task)
{
	daos_obj_list_dkey_t	*args;

	args = daos_task_get_args(DAOS_OPC_OBJ_LIST_DKEY, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return dc_obj_list_internal(args->oh, DAOS_OBJ_DKEY_RPC_ENUMERATE,
				    args->epoch, NULL, NULL, DAOS_IOD_NONE,
				    NULL, args->nr, args->kds, args->sgl,
				    NULL, NULL, NULL, args->anchor, true, false,
				    task);
}

int
dc_obj_list_akey(struct daos_task *task)
{
	daos_obj_list_akey_t	*args;

	args = daos_task_get_args(DAOS_OPC_OBJ_LIST_AKEY, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return dc_obj_list_internal(args->oh, DAOS_OBJ_AKEY_RPC_ENUMERATE,
				    args->epoch, args->dkey, NULL,
				    DAOS_IOD_NONE, NULL, args->nr, args->kds,
				    args->sgl, NULL, NULL, NULL, args->anchor,
				    true, false, task);
}

int
dc_obj_list_rec(struct daos_task *task)
{
	daos_obj_list_recx_t	*args;

	args = daos_task_get_args(DAOS_OPC_OBJ_LIST_RECX, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return dc_obj_list_internal(args->oh, DAOS_OBJ_RECX_RPC_ENUMERATE,
				    args->epoch, args->dkey, args->akey,
				    args->type, args->size, args->nr, NULL,
				    NULL, args->recxs, args->eprs,
				    args->cookies, args->anchor,
				    args->incr_order, false, task);
}

int
dc_obj_single_shard_list_dkey(struct daos_task *task)
{
	daos_obj_list_dkey_t	*args;

	args = daos_task_get_args(DAOS_OPC_OBJ_SHARD_LIST_DKEY, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return dc_obj_list_internal(args->oh, DAOS_OBJ_DKEY_RPC_ENUMERATE,
				    args->epoch, NULL, NULL, DAOS_IOD_NONE,
				    NULL, args->nr, args->kds, args->sgl, NULL,
				    NULL, NULL, args->anchor, true, true, task);
}
