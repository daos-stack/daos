/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of vos
 *
 * vos/vos_target_fs.h
 */
 #ifndef __VOS_TARGET_FS_H__
 #define __VOS_TARGET_FS_H__
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <inttypes.h>
 #include <daos_srv/vos.h>
 #include "vos_target_engine.h"

struct ring_list {
	const char *ring_name;
	struct spdk_ring *task_ring;
	d_list_t rl;
};

struct vos_fs_cmd_args {
	daos_handle_t oh;	/* opened object */
	daos_obj_id_t oid;	/* object ID */
	daos_unit_oid_t uoid;	/* object shard IDs (for VOS) */
	daos_epoch_t epoch;
	double *duration;
	bool force_exit;
	const char *vfcmd;
	struct ring_list *task_ring_list;
	struct vos_client_task *vct;
	struct pmfs_context *pmfs_ctx;
	int status;
};

/* All the fs commands can use this function to create thread */
/* Function : fs_cb arg: arg if using argobots thread: is_abt */
int pmfs_thread_create(void *fs_cb, void *arg, bool is_abt);
/* Find the ring matched with the name , return the ring pinter */
/* in the task ring */
struct spdk_ring *vos_task_get_ring(const char *name, void *arg);
/* Put fs commands in a ring and bind the ring */
void vos_task_bind_ring(const char *name, struct spdk_ring *ring,
			struct ring_list *ring_list);

/* For external users can call the following APIs */
/* Init the task process environments */
void vos_task_process_init(void *arg);
/* Polling for draning task queue and executing */
void vos_task_process(void *arg);
/* Finish the vos task process environments and clean resources */
void vos_task_process_fini(void *arg);
 #endif
