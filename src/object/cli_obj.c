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

#include <daos_event.h>
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
	if (layout->ol_shards[shard] == -1 ||
	    layout->ol_targets[shard] == -1) {
		pthread_rwlock_unlock(&obj->cob_lock);
		return -DER_NONEXIST;
	}

	/* XXX could be otherwise for some object classes? */
	D_ASSERT(layout->ol_shards[shard] == shard);

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
				       layout->ol_targets[shard],
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

int
dc_obj_open(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
	    unsigned int mode, daos_handle_t *oh, struct daos_task *task)
{
	struct dc_object	*obj;
	int			 rc = 0;

	obj = obj_alloc();
	if (obj == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	obj->cob_coh  = coh;
	obj->cob_mode = mode;

	pthread_rwlock_init(&obj->cob_lock, NULL);

	/* it is a local operation for now, does not require event */
	rc = obj_fetch_md(oid, &obj->cob_md, NULL);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = obj_layout_create(obj);
	if (rc != 0)
		D_GOTO(out, rc);

	obj_hdl_link(obj);
	*oh = obj_ptr2hdl(obj);
out:
	if (obj != NULL)
		obj_decref(obj);

	daos_task_complete(task, rc);
	return rc;
}

int
dc_obj_close(daos_handle_t oh, struct daos_task *task)
{
	struct dc_object   *obj;
	int		   rc = 0;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	obj_hdl_unlink(obj);
	obj_decref(obj);

out:
	daos_task_complete(task, rc);
	return 0;
}

int
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
			unsigned int map_ver)
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

	for (i = 0; i < grp_size; i++) {
		if (obj->cob_layout->ol_shards[idx] != -1)
			break;

		if (idx < idx_last)
			idx++;
		else
			idx = idx_first;
	}
	pthread_rwlock_unlock(&obj->cob_lock);

	if (i == grp_size)
		return -DER_NONEXIST;

	return idx;
}

static int
obj_grp_shard_get(struct dc_object *obj, uint32_t grp_idx,
		  uint64_t hash, unsigned int map_ver)
{
	int	grp_size;
	int	idx;

	grp_size = obj_get_grp_size(obj);
	idx = hash % grp_size + grp_idx * grp_size;
	return obj_grp_valid_shard_get(obj, idx, map_ver);
}

static int
obj_dkey2shard(struct dc_object *obj, daos_key_t *dkey,
	       unsigned int map_ver)
{
	uint64_t hash;
	int	 grp_idx;

	hash = daos_hash_murmur64((unsigned char *)dkey->iov_buf,
				  dkey->iov_len, 5731);

	grp_idx = obj_dkey2grp(obj, hash, map_ver);
	if (grp_idx < 0)
		return grp_idx;

	return obj_grp_shard_get(obj, grp_idx, hash, map_ver);
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

/* Get pool map version from object handle */
static int
dc_obj_pool_map_version_get(daos_handle_t oh, unsigned int *map_ver)
{
	daos_handle_t	ph;
	daos_handle_t	ch;
	int rc;

	ch = dc_obj_hdl2cont_hdl(oh);
	if (daos_handle_is_inval(ch))
		return -DER_NO_HDL;

	ph = dc_cont_hdl2pool_hdl(ch);
	if (daos_handle_is_inval(ph))
		return -DER_NO_HDL;

	rc = dc_pool_map_version_get(ph, map_ver);

	return rc;
}

int
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
dc_obj_task_cb(struct daos_task *task, void *arg)
{
	struct dc_object *obj = *((struct dc_object **)arg);
	int result = 0;

	daos_task_result_process(task, dc_obj_update_result, &result);

	if (task->dt_result == 0)
		task->dt_result = result;

	obj_decref(obj);

	return 0;
}

int
dc_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
	     unsigned int nr, daos_iod_t *iods, daos_sg_list_t *sgls,
	     daos_iom_t *maps, struct daos_task *task)
{
	struct dc_object	*obj;
	unsigned int		map_ver;
	int			shard;
	daos_handle_t		shard_oh;
	int			rc;

	rc = dc_obj_pool_map_version_get(oh, &map_ver);
	if (rc)
		D_GOTO(out_task, rc);

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	shard = obj_dkey2shard(obj, dkey, map_ver);
	if (shard < 0)
		D_GOTO(out_put, rc = shard);

	rc = obj_shard_open(obj, shard, &shard_oh, map_ver);
	if (rc != 0)
		D_GOTO(out_put, rc);

	rc = daos_task_register_comp_cb(task, dc_obj_task_cb, sizeof(obj),
					&obj);
	if (rc != 0) {
		dc_obj_shard_close(shard_oh);
		D_GOTO(out_put, rc);
	}

	D_DEBUG(DB_IO, "fetch "DF_OID" shard %u\n",
		DP_OID(obj->cob_md.omd_id), shard);
	rc = dc_obj_shard_fetch(shard_oh, epoch, dkey, nr, iods, sgls,
				maps, map_ver, task);

	dc_obj_shard_close(shard_oh);
	return rc;

out_put:
	obj_decref(obj);
out_task:
	daos_task_complete(task, rc);
	return rc;
}

#define MAX_TMP_SHARDS	6
int
dc_obj_update(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
	      unsigned int nr, daos_iod_t *iods, daos_sg_list_t *sgls,
	      struct daos_task *task)
{
	struct daos_sched	*sched = daos_task2sched(task);
	struct dc_object	*obj;
	unsigned int		map_ver;
	unsigned int		shard;
	unsigned int		shards_cnt = 0;
	struct daos_task	*tmp_tasks[MAX_TMP_SHARDS];
	struct daos_task	**shard_tasks = NULL;
	daos_handle_t		tmp_oh[MAX_TMP_SHARDS];
	daos_handle_t		*shard_ohs = NULL;
	int			real_shards = 0;
	int			i;
	int			rc = 0;

	rc = dc_obj_pool_map_version_get(oh, &map_ver);
	if (rc)
		D_GOTO(out_task, rc);

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	rc = obj_dkey2update_grp(obj, dkey, &shard, &shards_cnt, map_ver);
	if (rc != 0) {
		obj_decref(obj);
		D_GOTO(out_task, rc);
	}

	D_DEBUG(DB_IO, "update "DF_OID" start %u cnt %u\n",
		DP_OID(obj->cob_md.omd_id), shard, shards_cnt);

	rc = daos_task_register_comp_cb(task, dc_obj_task_cb, sizeof(obj),
					&obj);
	if (rc != 0) {
		obj_decref(obj);
		D_GOTO(out_task, rc);
	}

	/* If there is only 1 shards, then we do not need create
	 * shard tasks
	 **/
	if (shards_cnt == 1) {
		daos_handle_t shard_oh;

		rc = obj_shard_open(obj, shard, &shard_oh, map_ver);
		if (rc != 0)
			D_GOTO(out_task, rc);

		rc = dc_obj_shard_update(shard_oh, epoch, dkey, nr,
					 iods, sgls, map_ver, task);
		dc_obj_shard_close(shard_oh);
		return rc;
	}

	/* For multiple shards write, it needs create one task
	 * for each shards
	 **/
	if (shards_cnt > MAX_TMP_SHARDS) {
		D_ALLOC(shard_tasks, sizeof(*shard_tasks) * shards_cnt);
		if (shard_tasks == NULL)
			D_GOTO(out_task, rc = -DER_NOMEM);
		D_ALLOC(shard_ohs, sizeof(*shard_ohs) * shards_cnt);
		if (shard_ohs == NULL)
			D_GOTO(out_task, rc = -DER_NOMEM);
	} else {
		shard_tasks = tmp_tasks;
		shard_ohs = tmp_oh;
	}

	for (i = 0; i < shards_cnt; i++, shard++) {
		struct daos_task *shard_task;
		daos_handle_t shard_oh;

		rc = obj_shard_open(obj, shard, &shard_oh, map_ver);
		if (rc != 0) {
			/* skip the failed target */
			if (rc == -DER_NONEXIST) {
				rc = 0;
				continue;
			}
			break;
		}

		D_ALLOC_PTR(shard_task);
		if (shard_task == NULL) {
			rc = -DER_NOMEM;
			break;
		}

		rc = daos_task_init(shard_task, NULL, NULL, 0,
				    sched, NULL);
		if (rc != 0) {
			D_FREE_PTR(shard_task);
			break;
		}

		rc = daos_task_add_dependent(task, shard_task);
		if (rc != 0)
			break;

		shard_ohs[real_shards] = shard_oh;
		shard_tasks[real_shards] = shard_task;
		real_shards++;
	}

	/* XXX if there are no avaible targets, it might
	 * go endless here?
	 **/
	if (real_shards == 0 && rc == 0)
		D_GOTO(out_task, rc = -DER_STALE);

	if (rc != 0) {
		/* If there are already shard tasks, let's
		 * only complete shard tasks, which will
		 * finally complete the upper layer tasks */
		if (real_shards > 0) {
			for (i = 0; i < real_shards; i++) {
				dc_obj_shard_close(shard_ohs[i]);
				daos_task_complete(shard_tasks[i], rc);
			}
			D_GOTO(out_free, rc);
		} else {
			D_GOTO(out_task, rc);
		}
	}

	/* Trigger all shard tasks. */
	for (i = 0; i < real_shards; i++) {
		rc = dc_obj_shard_update(shard_ohs[i], epoch, dkey, nr,
					 iods, sgls, map_ver, shard_tasks[i]);
		D_DEBUG(DB_IO, "update "DF_OID" shard %u : rc %d\n",
			DP_OID(obj->cob_md.omd_id), shard, rc);
		dc_obj_shard_close(shard_ohs[i]);
	}

out_free:
	if (shard_tasks != NULL && shard_tasks != tmp_tasks)
		D_FREE(shard_tasks, sizeof(*shard_tasks) * shards_cnt);

	if (shard_ohs != NULL && shard_ohs != tmp_oh)
		D_FREE(shard_ohs, sizeof(*shard_ohs) * shards_cnt);

	return rc;

out_task:
	daos_task_complete(task, rc);
	goto out_free;
}

struct dc_obj_list_arg {
	struct dc_object *obj;
	daos_hash_out_t	 *anchor;
};

static int
dc_obj_list_dkey_cb(struct daos_task *task, void *data)
{
	struct dc_obj_list_arg *arg = *((struct dc_obj_list_arg **)data);
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
		enum_anchor_set_shard(anchor, shard);
	} else if (shard < obj->cob_layout->ol_nr - grp_size) {
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

static int
dc_obj_list_akey_cb(struct daos_task *task, void *data)
{
	struct dc_obj_list_arg	*arg = *((struct dc_obj_list_arg **)data);
	struct dc_object	*obj = arg->obj;
	daos_hash_out_t		*anchor = arg->anchor;
	int			rc = task->dt_result;

	if (rc != 0)
		return rc;

	if (daos_hash_is_eof(anchor))
		D_DEBUG(DB_IO, "Enumerated All shards\n");

	obj_decref(obj);

	return 0;
}

static int
dc_obj_list_key(daos_handle_t oh, uint32_t op, daos_epoch_t epoch,
		daos_key_t *key, uint32_t *nr,
		daos_key_desc_t *kds, daos_sg_list_t *sgl,
		daos_hash_out_t *anchor, struct daos_task *task)
{
	struct dc_object	*obj = NULL;
	struct dc_obj_list_arg	*arg;
	unsigned int		map_ver;
	daos_handle_t		shard_oh;
	int			shard;
	int			rc;

	if (nr == NULL || *nr == 0 || kds == NULL || sgl == NULL) {
		D_DEBUG(DB_IO, "Invalid API parameter.\n");
		D_GOTO(out_task, rc = -DER_INVAL);
	}

	rc = dc_obj_pool_map_version_get(oh, &map_ver);
	if (rc)
		D_GOTO(out_task, rc);

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	arg = daos_task_buf_get(task, sizeof(*arg));
	arg->obj = obj;
	arg->anchor = anchor;
	if (op == DAOS_OBJ_AKEY_RPC_ENUMERATE) {
		shard = obj_dkey2shard(obj, key, map_ver);
		if (shard < 0)
			D_GOTO(out_put, rc = shard);

		enum_anchor_set_shard(anchor, shard);
		rc = daos_task_register_comp_cb(task, dc_obj_list_akey_cb,
						sizeof(arg), &arg);
		if (rc != 0)
			D_GOTO(out_put, rc);
	} else {
		shard = enum_anchor_get_shard(anchor);
		shard = obj_grp_valid_shard_get(obj, shard, map_ver);
		if (shard < 0)
			D_GOTO(out_put, rc = shard);

		rc = daos_task_register_comp_cb(task, dc_obj_list_dkey_cb,
						sizeof(arg), &arg);
		if (rc != 0)
			D_GOTO(out_put, rc);
	}

	/** object will be decref by task complete cb */
	rc = obj_shard_open(obj, shard, &shard_oh, map_ver);
	if (rc != 0)
		D_GOTO(out_task, rc);

	rc = dc_obj_shard_list_key(shard_oh, op, epoch, key, nr,
				   kds, sgl, anchor, map_ver, task);

	dc_obj_shard_close(shard_oh);
	D_DEBUG(DB_IO, "Enumerate keys in shard %d: rc %d\n",
		shard, rc);

	return rc;
out_put:
	obj_decref(obj);
out_task:
	daos_task_complete(task, rc);
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
