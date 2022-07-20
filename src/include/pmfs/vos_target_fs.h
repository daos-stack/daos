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

enum {
	MKFS,
	MOUNT
};

enum {
	PTHREAD_WITH_JOIN = 0,
	ABT_THREAD,
	PTHREAD_NO_JOIN
};

struct mkfs_args {
	daos_handle_t poh;
	uuid_t uuid;
	int errorno;
};

struct mount_args {
	daos_handle_t poh;
	daos_handle_t coh;
	int flags;
	struct pmfs **pmfs;
	int errorno;
};

struct umount_args {
	struct pmfs *pmfs;
	int errorno;
};

struct mkdir_args {
	struct pmfs *pmfs;
	struct pmfs_obj *parent;
	const char *name;
	mode_t mode;
	int errorno;
};

struct listdir_args {
	struct pmfs *pmfs;
	struct pmfs_obj *obj;
	uint32_t nr;
	int errorno;
};

struct remove_args {
	struct pmfs *pmfs;
	struct pmfs_obj *parent;
	const char *name;
	bool force;
	daos_obj_id_t *oid;
	int errorno;
};

struct open_args {
	struct pmfs *pmfs;
	struct pmfs_obj *parent;
	const char *name;
	mode_t mode;
	int flags;
	daos_size_t chunk_size;
	const char *value;
	struct pmfs_obj *obj;
	int errorno;
};

struct readdir_args {
	struct pmfs *pmfs;
	struct pmfs_obj *obj;
	uint32_t *nr;
	struct dirent *dirs;
	int errorno;
};

struct lookup_args {
	struct pmfs *pmfs;
	const char *path;
	int flags;
	struct pmfs_obj *obj;
	mode_t *mode;
	struct stat *stbuf;
	int errorno;
};

struct release_args {
	struct pmfs_obj *obj;
	int errorno;
};

struct punch_args {
	struct pmfs *pmfs;
	struct pmfs_obj *obj;
	daos_off_t offset;
	daos_size_t len;
	int errorno;
};

struct write_args {
	struct pmfs *pmfs;
	struct pmfs_obj *obj;
	d_sg_list_t *user_sgl;
	daos_off_t off;
	daos_size_t *write_size;
	int errorno;
};

struct read_args {
	struct pmfs *pmfs;
	struct pmfs_obj *obj;
	d_sg_list_t *user_sgl;
	daos_off_t off;
	daos_size_t *read_size;
	int errorno;
};

struct stat_args {
	struct pmfs *pmfs;
	struct pmfs_obj *parent;
	const char *name;
	struct stat *stbuf;
	int errorno;
};

struct rename_args {
	struct pmfs *pmfs;
	struct pmfs_obj *parent;
	const char *old_name;
	const char *new_name;
	int errorno;
};

struct truncate_args {
	struct pmfs *pmfs;
	struct pmfs_obj *obj;
	daos_size_t len;
	int errorno;
};

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
/* Function : fs_cb arg: arg if using thread type: thread_type */
int pmfs_thread_create(void *fs_cb, void *arg, int thread_type);

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


/* User level APIs that for simple calling than raw APIs in pmfs.h */
/* Callbacks for pmfs commands, open these for customerized requests */
void pmfs_mkfs_cb(void *arg);
void pmfs_mount_cb(void *arg);
void pmfs_umount_cb(void *arg);
void pmfs_mkdir_cb(void *arg);
void pmfs_listdir_cb(void *arg);
void pmfs_remove_cb(void *arg);
void pmfs_open_cb(void *arg);
void pmfs_readdir_cb(void *arg);
void pmfs_lookup_cb(void *arg);
void pmfs_punch_cb(void *arg);
void pmfs_stat_cb(void *arg);
void pmfs_release_cb(void *arg);
int pmfs_write_cb(void *arg);
int pmfs_read_cb(void *arg);

int pmfs_init_pool(void *arg, struct scan_context ctx);
/* PMFS_MKFS or PMFS_TASKS*/
void pmfs_set_cmd_type(const char *type);

/* The following APIs encapsulate with thread (argobots or regular thread) */
/* Launching pmfs command using callbacks */
/* They are the same with pmfs.h just add thread launching */
/*  API + pmfs_thread_create , please read pmfs.h */

int pmfs_mount_start(daos_handle_t poh, daos_handle_t coh, struct pmfs **pmfs);
int pmfs_mkdir_start(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name,
		     mode_t mode);
int pmfs_listdir_start(struct pmfs *pmfs, struct pmfs_obj *obj, uint32_t *nr);
int pmfs_remove_start(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name,
		      bool force, daos_obj_id_t *oid);
int pmfs_open_start(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name,
		    mode_t mode, int flags, daos_size_t chunk_size,
		    const char *value, struct pmfs_obj **_obj);
int pmfs_readdir_start(struct pmfs *pmfs, struct pmfs_obj *obj, uint32_t *nr,
		       struct dirent *dirs);
int pmfs_lookup_start(struct pmfs *pmfs, const char *path, int flags,
		      struct pmfs_obj **obj, mode_t *mode, struct stat *stbuf);
int pmfs_punch_start(struct pmfs *pmfs, struct pmfs_obj *obj, daos_off_t offset,
		     daos_size_t len);

/* Thread_type : PTHREAD_WITH_JOIN/ABT_THREAD/PTHREAD_NO_JOIN */
int pmfs_write_start(struct pmfs *pmfs, struct pmfs_obj *obj, d_sg_list_t *user_sgl,
		     daos_off_t off, daos_size_t *write_size, int thread_type);

/* Thread_type : PTHREAD_WITH_JOIN/ABT_THREAD/PTHREAD_NO_JOIN */
int pmfs_read_start(struct pmfs *pmfs, struct pmfs_obj *obj, d_sg_list_t *user_sgl,
		    daos_off_t off, daos_size_t *read_size, int thread_type);
int pmfs_stat_start(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name,
		    struct stat *stbuf);
int pmfs_rename_start(struct pmfs *pmfs, struct pmfs_obj *parent, const char *old_name,
		      const char *new_name);
int pmfs_truncate_start(struct pmfs *pmfs, struct pmfs_obj *obj, daos_size_t len);
int pmfs_release_start(struct pmfs_obj *obj);
int pmfs_umount_start(struct pmfs *pmfs);
int pmfs_start_mkfs(struct pmfs_pool *pmfs_pool);

struct pmfs *pmfs_start_mount(struct pmfs_pool *pmfs_pool, struct pmfs *pmfs);
/* pmfs_init_target_env: using for init a target env, resources allocating */
struct pmfs_pool *pmfs_init_target_env(uint64_t tsc_nvme_size,  uint64_t tsc_scm_size);

/* Preparing a mounted environment at got the valid persistent memory file system pointer */
int pmfs_prepare_mounted_env_in_pool(struct pmfs_pool *pmfs_pool, struct pmfs **pmfs);

/* exit the persistent memory file system environment and clean up resources that allocated */
void pmfs_fini_target_env(void);
#endif
