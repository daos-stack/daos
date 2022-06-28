/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <daos/common.h>
#include <daos_srv/vos.h>
#include <pmfs/pmfs.h>
#include <pmfs/vos_target_fs.h>
#include <pmfs/vos_tasks.h>
#include <pmfs/pmfs_cmd.h>

static struct vos_fs_cmd_args *g_vfca;
static  struct pmfs_pool  *g_pmfs_pool;
static int count;
D_LIST_HEAD(g_test_pool_list);
D_LIST_HEAD(g_test_fini_list);

void
pmfs_mkfs_cb(void *arg)
{
	struct mkfs_args *mags = arg;

	mags->errorno = pmfs_mkfs(mags->poh, mags->uuid);
}

void
pmfs_mount_cb(void *arg)
{
	struct mount_args *mount_args = arg;

	mount_args->errorno = pmfs_mount(mount_args->poh, mount_args->coh, mount_args->flags,
			mount_args->pmfs);
}

static void
pmfs_umount_cb(void *arg)
{
	struct umount_args *umount_args = arg;

	umount_args->errorno = pmfs_umount(umount_args->pmfs);
}

static void
pmfs_mkdir_cb(void *arg)
{
	struct mkdir_args *mkdir_args = arg;

	mkdir_args->errorno = pmfs_mkdir(mkdir_args->pmfs, mkdir_args->parent,
					 mkdir_args->name,
					 mkdir_args->mode);
}

static void
pmfs_listdir_cb(void *arg)
{
	struct listdir_args *listdir_args = arg;

	listdir_args->errorno = pmfs_listdir(listdir_args->pmfs, listdir_args->obj,
					     &listdir_args->nr);
}

static void
pmfs_remove_cb(void *arg)
{
	struct remove_args *remove_args = arg;

	remove_args->errorno =	pmfs_remove(remove_args->pmfs, remove_args->parent,
					    remove_args->name, remove_args->force,
					    remove_args->oid);
}

static void
pmfs_open_cb(void *arg)
{
	struct open_args *open_args = arg;

	open_args->errorno = pmfs_open(open_args->pmfs, open_args->parent, open_args->name,
				       open_args->mode, open_args->flags, open_args->chunk_size,
				       open_args->value, &open_args->obj);
}

static void
pmfs_readdir_cb(void *arg)
{
	struct readdir_args *readdir_args = arg;

	readdir_args->errorno = pmfs_readdir(readdir_args->pmfs, readdir_args->obj,
					     readdir_args->nr, readdir_args->dirs);
}

static void
pmfs_lookup_cb(void *arg)
{
	struct lookup_args *lookup_args = arg;

	lookup_args->errorno = pmfs_lookup(lookup_args->pmfs, lookup_args->path,
					   lookup_args->flags, &lookup_args->obj,
					   lookup_args->mode, lookup_args->stbuf);
}

static void
pmfs_release_cb(void *arg)
{
	struct release_args *release_args = arg;

	release_args->errorno = pmfs_release(release_args->obj);
}

static void
pmfs_punch_cb(void *arg)
{
	struct punch_args *punch_args = arg;

	punch_args->errorno = pmfs_punch(punch_args->pmfs, punch_args->obj,
					 punch_args->offset, punch_args->len);
}

static int
pmfs_write_cb(void *arg)
{
	struct write_args *write_args = arg;
	d_sg_list_t     *sgl = write_args->user_sgl;
	daos_size_t     off = write_args->off;
	int     rc = 0;

	g_vfca->vfcmd = "PMFS_TASKS";
	write_args->errorno = pmfs_write_sync(write_args->pmfs, write_args->obj,
					      sgl, off);
	rc = write_args->errorno;
	if (rc != 0) {
		D_PRINT("write error\r\n");
		return rc;
	}

	return 0;
}

static int
pmfs_read_cb(void *arg)
{
	struct read_args *read_args = arg;

	d_sg_list_t *sgl = read_args->user_sgl;
	daos_size_t off = read_args->off;
	daos_size_t read_size  = *read_args->read_size;
	int rc = 0;

	g_vfca->vfcmd = "PMFS_TASKS";

	read_args->errorno = pmfs_read_sync(read_args->pmfs, read_args->obj, sgl, off,
					    &read_size);
	rc = read_args->errorno;
	if (rc != 0) {
		read_args->errorno = -EINVAL;
		return rc;
	}

	return 0;
}

static void
pmfs_stat_cb(void *arg)
{
	struct stat_args *stat_args = arg;

	stat_args->errorno = pmfs_stat(stat_args->pmfs, stat_args->parent,
				       stat_args->name, stat_args->stbuf);
}

static void
pmfs_rename_cb(void *arg)
{
	struct rename_args *rename_args = arg;

	rename_args->errorno = pmfs_rename(rename_args->pmfs, rename_args->parent,
					   rename_args->old_name, rename_args->new_name);
}

static void
pmfs_truncate_cb(void *arg)
{
	struct truncate_args *truncate_args = arg;

	truncate_args->errorno = pmfs_truncate(truncate_args->pmfs,
					       truncate_args->obj, truncate_args->len);
}

int
pmfs_mount_start(daos_handle_t poh, daos_handle_t coh, struct pmfs **pmfs)
{
	struct mount_args mount_args;
	int rc = 0;

	memset(&mount_args, 0, sizeof(mount_args));
	mount_args.poh = poh;
	mount_args.coh = coh;
	mount_args.flags = O_RDWR;
	mount_args.pmfs = pmfs;
	g_vfca->vfcmd = "PMFS_TASKS";

	rc = pmfs_thread_create(pmfs_mount_cb, (void *)&mount_args, 0);
	if (rc != 0)
		return rc;

	return mount_args.errorno;
}

int
pmfs_mkdir_start(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name,
		 mode_t mode)
{
	struct mkdir_args mkdir_args;
	int rc = 0;

	memset(&mkdir_args, 0, sizeof(mkdir_args));
	mkdir_args.pmfs = pmfs;
	mkdir_args.parent = parent;
	mkdir_args.name = name;
	mkdir_args.mode = mode;

	g_vfca->vfcmd = "PMFS_TASKS";

	rc = pmfs_thread_create(pmfs_mkdir_cb, (void *)&mkdir_args, 0);
	if (rc != 0)
		return rc;

	return mkdir_args.errorno;
}

int
pmfs_listdir_start(struct pmfs *pmfs, struct pmfs_obj *obj, uint32_t *nr)
{
	struct listdir_args listdir_args;
	int rc = 0;

	memset(&listdir_args, 0, sizeof(listdir_args));
	listdir_args.pmfs = pmfs;
	listdir_args.obj = obj;
	listdir_args.nr = *nr;

	g_vfca->vfcmd = "PMFS_TASKS";

	rc = pmfs_thread_create(pmfs_listdir_cb, (void *)&listdir_args, 0);
	if (rc != 0)
		return rc;

	*nr = listdir_args.nr;

	return listdir_args.errorno;
}

int
pmfs_remove_start(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name,
		  bool force, daos_obj_id_t *oid)
{
	struct remove_args remove_args;
	int rc = 0;

	memset(&remove_args, 0, sizeof(remove_args));
	remove_args.pmfs = pmfs;
	remove_args.parent = parent;
	remove_args.name = name;
	remove_args.force = force;
	remove_args.oid = oid;

	g_vfca->vfcmd = "PMFS_TASKS";

	rc = pmfs_thread_create(pmfs_remove_cb, (void *)&remove_args, 0);
	if (rc != 0)
		return rc;

	return remove_args.errorno;
}

int
pmfs_open_start(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name,
		mode_t mode, int flags, daos_size_t chunk_size,
		const char *value, struct pmfs_obj **_obj)
{
	struct open_args open_args;
	int rc = 0;

	memset(&open_args, 0, sizeof(open_args));
	open_args.pmfs = pmfs;
	open_args.parent = parent;
	open_args.name = name;
	open_args.mode = mode;
	open_args.flags = flags;
	open_args.chunk_size = chunk_size;
	open_args.value = value;
	open_args.obj = *_obj;


	g_vfca->vfcmd = "PMFS_TASKS";

	rc = pmfs_thread_create(pmfs_open_cb, (void *)&open_args, 0);
	if (rc != 0)
		return rc;

	*_obj = open_args.obj;

	return open_args.errorno;
}

int
pmfs_readdir_start(struct pmfs *pmfs, struct pmfs_obj *obj, uint32_t *nr,
		   struct dirent *dirs)
{
	struct readdir_args readdir_args;
	int rc = 0;

	memset(&readdir_args, 0, sizeof(readdir_args));
	readdir_args.pmfs = pmfs;
	readdir_args.obj = obj;
	readdir_args.nr = nr;
	readdir_args.dirs = dirs;

	g_vfca->vfcmd = "PMFS_TASKS";

	rc = pmfs_thread_create(pmfs_readdir_cb, (void *)&readdir_args, 0);
	if (rc != 0)
		return rc;

	return readdir_args.errorno;
}

int
pmfs_lookup_start(struct pmfs *pmfs, const char *path, int flags,
		  struct pmfs_obj **obj, mode_t *mode, struct stat *stbuf)
{
	struct lookup_args lookup_args;
	int rc = 0;

	memset(&lookup_args, 0, sizeof(lookup_args));
	lookup_args.pmfs = pmfs;
	lookup_args.path = path;
	lookup_args.flags = flags;
	lookup_args.obj = *obj;
	lookup_args.mode = mode;
	lookup_args.stbuf = stbuf;

	g_vfca->vfcmd = "PMFS_TASKS";

	rc = pmfs_thread_create(pmfs_lookup_cb, (void *)&lookup_args, 0);
	if (rc != 0)
		return rc;

	*obj = lookup_args.obj;

	return lookup_args.errorno;
}

int
pmfs_punch_start(struct pmfs *pmfs, struct pmfs_obj *obj, daos_off_t offset,
		 daos_size_t len)
{
	struct punch_args punch_args;
	int rc = 0;

	memset(&punch_args, 0, sizeof(punch_args));
	punch_args.pmfs = pmfs;
	punch_args.obj = obj;
	punch_args.offset = offset;
	punch_args.len = len;

	g_vfca->vfcmd = "PMFS_TASKS";

	rc = pmfs_thread_create(pmfs_punch_cb, (void *)&punch_args, 0);
	if (rc != 0)
		return rc;

	return punch_args.errorno;
}

int
pmfs_write_start(struct pmfs *pmfs, struct pmfs_obj *obj, d_sg_list_t *user_sgl,
		 daos_off_t off, daos_size_t *write_size)
{
	struct write_args write_args;
	int rc = 0;

	memset(&write_args, 0, sizeof(write_args));
	write_args.pmfs = pmfs;
	write_args.obj = obj;
	write_args.user_sgl = user_sgl;
	write_args.off = off;
	write_args.write_size = write_size;

	g_vfca->vfcmd = "PMFS_TASKS";

	rc = pmfs_thread_create(pmfs_write_cb, (void *)&write_args, 0);
	if (rc != 0)
		return rc;

	return write_args.errorno;
}

int
pmfs_read_start(struct pmfs *pmfs, struct pmfs_obj *obj, d_sg_list_t *user_sgl,
		daos_off_t off,	daos_size_t *read_size)
{
	struct read_args read_args;
	int rc = 0;

	memset(&read_args, 0, sizeof(read_args));
	read_args.pmfs = pmfs;
	read_args.obj = obj;
	read_args.user_sgl = user_sgl;
	read_args.off = off;
	read_args.read_size = read_size;

	g_vfca->vfcmd = "PMFS_TASKS";

	rc = pmfs_thread_create(pmfs_read_cb, (void *)&read_args, 0);
	if (rc != 0)
		return rc;

	return read_args.errorno;
}

int
pmfs_stat_start(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name,
		struct stat *stbuf)
{
	struct stat_args stat_args;
	int rc = 0;

	memset(&stat_args, 0, sizeof(stat_args));
	stat_args.pmfs = pmfs;
	stat_args.parent = parent;
	stat_args.name = name;
	stat_args.stbuf = stbuf;

	g_vfca->vfcmd = "PMFS_TASKS";

	rc = pmfs_thread_create(pmfs_stat_cb, (void *)&stat_args, 0);
	if (rc != 0)
		return rc;

	return stat_args.errorno;
}

int
pmfs_rename_start(struct pmfs *pmfs, struct pmfs_obj *parent, const char *old_name,
		  const char *new_name)
{
	struct rename_args rename_args;
	int rc = 0;

	memset(&rename_args, 0, sizeof(rename_args));
	rename_args.pmfs = pmfs;
	rename_args.parent = parent;
	rename_args.old_name = old_name;
	rename_args.new_name = new_name;

	g_vfca->vfcmd = "PMFS_TASKS";

	rc = pmfs_thread_create(pmfs_rename_cb, (void *)&rename_args, 0);
	if (rc != 0)
		return rc;

	return rename_args.errorno;
}

int
pmfs_truncate_start(struct pmfs *pmfs, struct pmfs_obj *obj, daos_size_t len)
{
	struct truncate_args truncate_args;
	int rc = 0;

	memset(&truncate_args, 0, sizeof(truncate_args));
	truncate_args.pmfs = pmfs;
	truncate_args.obj = obj;
	truncate_args.len = len;

	g_vfca->vfcmd = "PMFS_TASKS";

	rc = pmfs_thread_create(pmfs_truncate_cb, (void *)&truncate_args, 0);
	if (rc != 0)
		return rc;

	return truncate_args.errorno;
}

int
pmfs_release_start(struct pmfs_obj *obj)
{
	struct release_args release_args;
	int rc = 0;

	memset(&release_args, 0, sizeof(release_args));
	release_args.obj = obj;

	g_vfca->vfcmd = "PMFS_TASKS";

	rc = pmfs_thread_create(pmfs_release_cb, (void *)&release_args, 0);
	if (rc != 0)
		return rc;

	return release_args.errorno;
}


int
pmfs_umount_start(struct pmfs *pmfs)
{
	struct umount_args umount_args;
	int rc = 0;

	memset(&umount_args, 0, sizeof(umount_args));
	umount_args.pmfs = pmfs;

	rc = pmfs_thread_create(pmfs_umount_cb, (void *)&umount_args, 0);
	if (rc != 0)
		return rc;

	return umount_args.errorno;
}

int
pmfs_init_pool(void *arg, struct scan_context ctx)
{
	struct vos_fs_cmd_args *vfca = arg;
	int rc;

	uuid_copy(ctx.pool_uuid, vfca->pmfs_ctx->pmfs_pool.tsc_pool_uuid);
	ctx.pool_hdl = vfca->pmfs_ctx->pmfs_pool.tsc_poh;
	ctx.cur_cont = vfca->pmfs_ctx->pmfs_pool.pmfs_container;
	ctx.cur_cont.cl = vfca->pmfs_ctx->pmfs_pool.pmfs_container.cl;
	rc = pmfs_scan_pool(&ctx);

	if (rc != 0)
		printf("init pool, rebuild container list failed\r\n");

	vfca->pmfs_ctx->pmfs_pool.pmfs_container = ctx.cur_cont;

	return rc;
}

struct pmfs *
pmfs_start_mount(struct pmfs_pool *pmfs_pool, struct pmfs *pmfs)
{
	struct scan_context ctx = { };
	daos_handle_t test_coh;
	int rc = 0;

	g_vfca->pmfs_ctx->pmfs_pool = *pmfs_pool;
	D_PRINT("---------------start scan pool---------------------------\r\n");
	D_PRINT("---------------rebuild container list before mount-------\r\n");
	rc = pmfs_init_pool(g_vfca, ctx);
	if (rc != 0)
		return NULL;

	D_PRINT("---------------rebuild container list done---------------\r\n");
	/* start mount thread */
	test_coh = g_vfca->pmfs_ctx->pmfs_pool.pmfs_container.tsc_coh;
	rc = pmfs_mount_start(g_vfca->pmfs_ctx->pmfs_pool.tsc_poh, test_coh,
			      &pmfs);
	if (rc != 0)
		return NULL;

	return pmfs;
}

int
pmfs_start_mkfs(struct pmfs_pool *pmfs_pool)
{
	struct mkfs_args mkfs_args;
	int rc;

	memset(&mkfs_args, 0, sizeof(mkfs_args));
	mkfs_args.poh = pmfs_pool->tsc_poh;
	uuid_generate(mkfs_args.uuid);

	g_vfca->vfcmd = "PMFS_MKFS";

	rc = pmfs_thread_create(pmfs_mkfs_cb, (void *)&mkfs_args, 0);
	if (rc != 0)
		return rc;

	return 0;
}

static struct pmfs_pool
pmfs_add_single_pool(char *tsc_pmem_file, uint64_t tsc_nvme_size,
		     uint64_t tsc_scm_size, bool tsc_skip_cont_create, bool amend)
{
	struct pmfs_pool *pmfs_pool;

	D_ALLOC(pmfs_pool, sizeof(struct pmfs_pool));
	D_ASSERT(pmfs_pool != NULL);

	if (tsc_pmem_file == NULL) {
		char ts_pmem_file[PATH_MAX];

		snprintf(ts_pmem_file, sizeof(ts_pmem_file),
			 "/mnt/daos/pmfs_cli%d.pmem", count);
		tsc_pmem_file = ts_pmem_file;
		printf("tsc pmem file = %s\r\n", tsc_pmem_file);
	}
	uuid_generate(pmfs_pool->tsc_pool_uuid);
	pmfs_pool->tsc_pmem_file = tsc_pmem_file;
	pmfs_pool->tsc_nvme_size = tsc_nvme_size;
	pmfs_pool->tsc_scm_size = tsc_scm_size;
	pmfs_pool->tsc_skip_cont_create = tsc_skip_cont_create;
	count++;
	d_list_add(&pmfs_pool->pl, &g_test_pool_list);
	if (amend) {
		engine_pool_single_node_init(pmfs_pool, false);
		pmfs_combine_pool_fini_list(&g_test_fini_list);
	}

	return *pmfs_pool;
}

static struct pmfs_context *
pmfs_set_ctx(uint64_t tsc_nvme_size,  uint64_t tsc_scm_size)
{
	struct pmfs_context *pmfs_ctx;

	D_ALLOC(pmfs_ctx, sizeof(struct pmfs_context));
	D_ASSERT(pmfs_ctx != NULL);

	D_INIT_LIST_HEAD(&pmfs_ctx->pmfs_pool.pl);
	/* add pool mapping to /mnt/daos/pmfs_cli0.pmem, 8G NVME,2G SCM, skip container create */
	pmfs_ctx->pmfs_pool = pmfs_add_single_pool("/mnt/daos/pmfs_cli0.pmem",
						   tsc_nvme_size, tsc_scm_size, true,
						   false);
	/* That aims to associated with engine lib */
	pmfs_ctx->pmfs_pool.pl = g_test_pool_list;
	pmfs_ctx_combine_pool_list(pmfs_ctx);
	return pmfs_ctx;
}

static int
app_send_task_process_thread(void)
{
	int rc;
	pthread_t pid;

	rc = pthread_create(&pid, NULL, (void *)vos_task_process,
			    g_vfca);
	if (rc != 0)
		return rc;

	return 0;
}

struct pmfs_pool *
pmfs_init_target_env(uint64_t tsc_nvme_size,  uint64_t tsc_scm_size)
{
	g_vfca = calloc(1, sizeof(struct vos_fs_cmd_args));
	if (!g_vfca)
		return NULL;

	g_vfca->vct = calloc(1, sizeof(g_vfca->vct));
	if (!g_vfca->vct) {
		free(g_vfca);
		return NULL;
	}

	g_vfca->duration = calloc(1, sizeof(g_vfca->duration));
	if (!g_vfca->duration) {
		free(g_vfca->vct);
		free(g_vfca);
		return NULL;
	}

	g_vfca->pmfs_ctx = pmfs_set_ctx(tsc_nvme_size, tsc_nvme_size);
	/* Start to init process env */
	vos_task_process_init(g_vfca);
	pmfs_combine_pool_fini_list(&g_test_fini_list);

	/* Start to process cmds */
	app_send_task_process_thread();

	g_pmfs_pool = pmfs_find_pool("/mnt/daos/pmfs_cli0.pmem");

	return g_pmfs_pool;
}

int
pmfs_prepare_mounted_env_in_pool(struct pmfs_pool *pmfs_pool, struct pmfs **pmfs)
{
	struct mkfs_args mags;
	struct scan_context ctx = { };
	daos_handle_t coh;
	int rc;

	memset(&mags, 0, sizeof(mags));
	mags.poh = pmfs_pool->tsc_poh;
	uuid_generate(mags.uuid);

	g_vfca->vfcmd = "PMFS_MKFS";
	/* start mkfs start thread */
	rc = pmfs_thread_create(pmfs_mkfs_cb, (void *)&mags, 0);
	if (rc != 0)
		return rc;
	/* start pmfs_init_pool */
	g_vfca->pmfs_ctx->pmfs_pool = *pmfs_pool;
	rc = pmfs_init_pool(g_vfca, ctx);
	if (rc != 0)
		return rc;
	/* start mount thread */
	coh = g_vfca->pmfs_ctx->pmfs_pool.pmfs_container.tsc_coh;
	rc = pmfs_mount_start(g_vfca->pmfs_ctx->pmfs_pool.tsc_poh, coh,
			      pmfs);
	if (rc != 0)
		return rc;

	return 0;
}

void
pmfs_set_cmd_type(const char *type)
{
	/* TASK TYPE --"PMFS_MKFS" or "PMFS_TASKS"*/
	g_vfca->vfcmd = type;
}

void
pmfs_fini_target_env(void)
{
	vos_task_process_fini(g_vfca);
	free(g_vfca->pmfs_ctx);
	free(g_vfca->duration);
	free(g_vfca->vct);
	free(g_vfca);
}
