/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <pmfs/vos_tasks.h>

static inline int
client_task_enqueue(struct spdk_ring *task_ring, struct vos_client_task *task)
{
	int rc;

	rc = spdk_ring_enqueue(task_ring, (void **)&task, 1, NULL);
	if (rc != 1) {
		return -EIO;
	}

	return 0;
}

static void
client_rw_task_init(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch, uint32_t pm_ver,
		    uint64_t flags, daos_key_t *dkey, unsigned int akey_nr, daos_key_t *akeys,
		    unsigned int iod_nr, daos_iod_t *iods, d_sg_list_t *sgls,
		    struct dtx_handle *dth, struct vos_client_task *task)
{
	task->args.obj_rw.coh = coh;
	task->args.obj_rw.oid.id_pub = oid;
	task->args.obj_rw.oid.id_shard = 0;
	task->args.obj_rw.epoch = epoch;
	task->args.obj_rw.pm_ver = pm_ver;
	task->args.obj_rw.flags = flags;
	task->args.obj_rw.dkey = dkey;
	task->args.obj_rw.akey_nr = akey_nr;
	task->args.obj_rw.akeys = akeys;
	task->args.obj_rw.iod_nr = iod_nr;
	task->args.obj_rw.iods = iods;
	task->args.obj_rw.sgls = sgls;
	task->args.obj_rw.dth = dth;
}

static void
client_list_task_init(daos_handle_t coh, daos_obj_id_t oid, uint32_t *nr, uint64_t *len,
		      daos_key_desc_t *kds, void *buf, struct vos_client_task *task)
{
	task->args.obj_list.coh = coh;
	task->args.obj_list.oid.id_pub = oid;
	task->args.obj_list.oid.id_shard = 0;
	task->args.obj_list.nr = nr;
	task->args.obj_list.len = len;
	task->args.obj_list.kds = kds;
	task->args.obj_list.buf = buf;
}

static int
vos_client_complete_cb(void *cb_args, int rc)
{
	struct vos_client_task *task = cb_args;

	task->rc = rc;

	sem_post(&task->sem);

	return 0;
}

int
vos_client_obj_update(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
		      uint32_t pm_ver, uint64_t flags, daos_key_t *dkey,
		      unsigned int iod_nr, daos_iod_t *iods, d_sg_list_t *sgls,
		      struct spdk_ring *task_ring, struct vos_client_task *task)
{
	task->opc = VOS_OBJ_UPDATE;

	client_rw_task_init(coh, oid, epoch, pm_ver, flags, dkey, 0, NULL, iod_nr,
			    iods, sgls, NULL, task);

	return client_task_enqueue(task_ring, task);
}

int
vos_client_obj_update_sync(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
			   uint32_t pm_ver, uint64_t flags, daos_key_t *dkey,
			   unsigned int iod_nr, daos_iod_t *iods, d_sg_list_t *sgls,
			   struct spdk_ring *task_ring)
{
	struct vos_client_task *task;
	int rc;

	task = calloc(1, sizeof(*task));
	if (!task) {
		return -ENOMEM;
	}

	sem_init(&task->sem, 0, 0);
	task->opc = VOS_OBJ_UPDATE;
	task->cb_fn = vos_client_complete_cb;
	task->cb_args = task;

	client_rw_task_init(coh, oid, epoch, pm_ver, flags, dkey, 0, NULL, iod_nr,
			    iods, sgls, NULL, task);
	rc = client_task_enqueue(task_ring, task);
	if (rc) {
		free(task);
		return rc;
	}
	sem_wait(&task->sem);
	return task->rc;
}

int
vos_client_obj_fetch(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
		     uint64_t flags, daos_key_t *dkey,
		     unsigned int iod_nr, daos_iod_t *iods, d_sg_list_t *sgls,
		     struct spdk_ring *task_ring, struct vos_client_task *task)
{
	task->opc = VOS_OBJ_FETCH;

	client_rw_task_init(coh, oid, epoch, 0, flags, dkey, 0, NULL, iod_nr, iods,
			    sgls, NULL, task);

	return client_task_enqueue(task_ring, task);
}

int
vos_client_obj_fetch_sync(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
			  uint64_t flags, daos_key_t *dkey,
			  unsigned int iod_nr, daos_iod_t *iods, d_sg_list_t *sgls,
			  struct spdk_ring *task_ring)
{
	struct vos_client_task *task;
	int rc;

	task = calloc(1, sizeof(*task));
	if (!task) {
		return -ENOMEM;
	}

	sem_init(&task->sem, 0, 0);
	task->opc = VOS_OBJ_FETCH;
	task->cb_fn = vos_client_complete_cb;
	task->cb_args = task;

	client_rw_task_init(coh, oid, epoch, 0, flags, dkey, 0, NULL, iod_nr, iods,
			    sgls, NULL, task);
	rc = client_task_enqueue(task_ring, task);
	if (rc) {
		free(task);
		return rc;
	}

	sem_wait(&task->sem);
	return task->rc;
}

int
vos_client_obj_punch(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
		     uint32_t pm_ver, uint64_t flags, daos_key_t *dkey,
		     unsigned int akey_nr, daos_key_t *akeys,
		     struct spdk_ring *task_ring, struct vos_client_task *task)
{
	task->opc = VOS_OBJ_PUNCH;

	client_rw_task_init(coh, oid, epoch, pm_ver, flags, dkey, akey_nr, akeys, 0,
			    NULL, NULL, NULL, task);

	return client_task_enqueue(task_ring, task);

}

int
vos_client_obj_punch_sync(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
			  uint32_t pm_ver, uint64_t flags, daos_key_t *dkey,
			  unsigned int akey_nr, daos_key_t *akeys,
			  struct spdk_ring *task_ring)
{
	struct vos_client_task *task;
	int rc;

	task = calloc(1, sizeof(*task));
	if (!task) {
		return -ENOMEM;
	}

	sem_init(&task->sem, 0, 0);
	task->opc = VOS_OBJ_PUNCH;
	task->cb_fn = vos_client_complete_cb;
	task->cb_args = task;

	client_rw_task_init(coh, oid, epoch, pm_ver, flags, dkey, akey_nr, akeys, 0,
			    NULL, NULL, NULL, task);
	rc = client_task_enqueue(task_ring, task);
	if (rc) {
		free(task);
		return rc;
	}

	sem_wait(&task->sem);
	return task->rc;
}

int
vos_client_obj_get_num_dkeys(daos_handle_t coh, daos_obj_id_t oid, uint32_t *nr, uint64_t *len,
			     struct spdk_ring *task_ring, struct vos_client_task *task)
{
	task->opc = VOS_OBJ_GET_NUM_DKEYS;
	client_list_task_init(coh, oid, nr, len, NULL, NULL, task);
	return client_task_enqueue(task_ring, task);
}

int
vos_client_obj_get_num_dkeys_sync(daos_handle_t coh, daos_obj_id_t oid, uint32_t *nr,
				  uint64_t *len, struct spdk_ring *task_ring)
{
	struct vos_client_task *task;
	int rc;

	task = calloc(1, sizeof(*task));
	if (!task) {
		return -ENOMEM;
	}

	sem_init(&task->sem, 0, 0);
	task->opc = VOS_OBJ_GET_NUM_DKEYS;
	task->cb_fn = vos_client_complete_cb;
	task->cb_args = task;

	client_list_task_init(coh, oid, nr, len, NULL, NULL, task);
	rc = client_task_enqueue(task_ring, task);
	if (rc) {
		free(task);
		return rc;
	}

	sem_wait(&task->sem);
	return task->rc;
}

int
vos_client_obj_list_dkeys(daos_handle_t coh, daos_obj_id_t oid, uint32_t *nr, uint64_t *len,
			  daos_key_desc_t *kds, void *buf, struct spdk_ring *task_ring,
			  struct vos_client_task *task)
{
	task->opc = VOS_OBJ_LIST_DKEYS;
	client_list_task_init(coh, oid, nr, NULL, kds, buf, task);
	return client_task_enqueue(task_ring, task);
}

int
vos_client_obj_list_dkeys_sync(daos_handle_t coh, daos_obj_id_t oid, uint32_t *nr, uint64_t *len,
			       daos_key_desc_t *kds, void *buf, struct spdk_ring *task_ring)
{
	struct vos_client_task *task;
	int rc;

	task = calloc(1, sizeof(*task));
	if (!task) {
		return -ENOMEM;
	}

	sem_init(&task->sem, 0, 0);
	task->opc = VOS_OBJ_LIST_DKEYS;
	task->cb_fn = vos_client_complete_cb;
	task->cb_args = task;

	client_list_task_init(coh, oid, nr, len, kds, buf, task);
	rc = client_task_enqueue(task_ring, task);
	if (rc) {
		free(task);
		return rc;
	}

	sem_wait(&task->sem);
	return task->rc;
}
