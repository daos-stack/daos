/* *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
 #include <stdio.h>
 #include <string.h>
 #include <errno.h>
 #include <sys/types.h>
 #include <sys/stat.h>
 #include <sys/mman.h>
 #include <stdlib.h>
 #include <unistd.h>
 #include <daos_srv/vos.h>
 #include <daos_fs_sys.h>
 #include <spdk/env.h>
 #include <pmfs/vos_target_fs.h>
 #include <pmfs/vos_tasks.h>
 #include <pmfs/vos_target_engine.h>
 #include <pmfs/pmfs.h>

char dfs_pmem_file[PATH_MAX];
static pthread_mutex_t vos_fs_cmd_lock = PTHREAD_MUTEX_INITIALIZER;
static struct vos_fs_cmd_args *g_vfca;
static  struct pmfs_pool  *g_pmfs_pool;
static int count;

D_LIST_HEAD(g_vos_pool_list);
D_LIST_HEAD(g_vos_fini_list);
D_LIST_HEAD(g_task_ring);

static ABT_xstream	abt_xstream;
static ABT_pool		pool;
static int
fs_abt_init(void)
{
	int cpuid;
	int rc;
	int num_cpus;

	rc = ABT_init(0, NULL);
	if (rc != ABT_SUCCESS) {
		fprintf(stderr, "ABT init failed %d\n", rc);
		return -1;
	}

	rc = ABT_xstream_self(&abt_xstream);
	if (rc != 0) {
		fprintf(stderr, "ABT get self xstream failed %d\n", rc);
	}

	rc = ABT_xstream_get_cpubind(abt_xstream, &cpuid);
	if (rc != 0) {
		fprintf(stderr, "get CPU bind failed %d\n", rc);
		fprintf(stderr, "No CPU affinity for this target\n");
		fprintf(stderr, "Build ABT by --enable-affinity if you want to\n"
			"use CPU affinity\n");
		return 0;
	}
	rc = ABT_xstream_get_affinity(abt_xstream, 0, NULL, &num_cpus);
	if (rc != ABT_SUCCESS) {
		fprintf(stderr, "get num_cpus: %d\n", rc);
		fprintf(stderr, "No CPU affinity for this target.\n");
		fprintf(stderr, "Build ABT by --enable-affinity if\n"
			"you want to try CPU affinity.\n");
		return 0;
	}

	cpuid = (cpuid + 1) % num_cpus;
	rc = ABT_xstream_set_cpubind(abt_xstream, cpuid);
	if (rc != ABT_SUCCESS) {
		fprintf(stderr, "set affinity: %d\n", rc);
		fprintf(stderr, "No CPU affinity for this target.\n");
		fprintf(stderr, "Build ABT by --enable-affinity if\n"
			"you want to try CPU affinity.\n");
		return 0;
	}
	return 0;
}

static void
fs_abt_fini(void)
{
	ABT_xstream_join(abt_xstream);
	ABT_xstream_free(&abt_xstream);
	ABT_finalize();
}

static inline double
dts_time_now(void)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);
	return (tv.tv_sec + tv.tv_usec / 1000000.0);
}

const char *cmd_string[] = {"none", "update", "fetch",
			    "punch", "get_num_dkeys", "list_dkeys"};

static int
vos_obj_get_info_dkeys(daos_handle_t coh, daos_unit_oid_t oid,
		       enum task_op opc, uint32_t *nr, uint64_t *len,
		       daos_key_desc_t *kds, void *buf)
{
	struct scan_context ctx = { 0 };
	struct pmfs_obj_info *uoi = NULL;
	int rc = 0;

	ctx.cur_cont.tsc_coh = coh;
	ctx.uoi.oid = oid;
	ctx.uoi.nr = *nr;
	ctx.uoi.len = *len;

	D_ALLOC(uoi, sizeof(struct pmfs_obj_info));
	D_ASSERT(uoi != NULL);

	rc = pmfs_scan_cont(&ctx, &uoi, opc);
	if (rc != 0) {
		D_ERROR("Object get dkeys info failed: " DF_RC "\n",
			DP_RC(rc));
		return rc;
	}

	*nr = uoi->nr;
	*len = uoi->len;

	return 0;
}

static int
vos_obj_list_info_dkeys(daos_handle_t coh, daos_unit_oid_t oid,
			enum task_op opc, uint32_t *nr, uint64_t *len,
			daos_key_desc_t *kds, void *buf)
{
	struct scan_context ctx = { 0 };
	struct pmfs_obj_info *uoi = NULL;
	int rc = 0;

	ctx.cur_cont.tsc_coh = coh;
	ctx.uoi.oid = oid;
	ctx.uoi.nr =  *nr;
	ctx.uoi.len = *len;
	ctx.uoi.kds = kds;
	ctx.uoi.buf = buf;

	D_ALLOC(uoi, sizeof(struct pmfs_obj_info));
	D_ASSERT(uoi != NULL);

	rc = pmfs_scan_cont(&ctx, &uoi, opc);
	if (rc != 0) {
		D_ERROR("Object get dkeys info failed: " DF_RC "\n",
			DP_RC(rc));
		return rc;
	}

	*nr = uoi->nr;
	*len = uoi->len;
	kds = uoi->kds;
	buf = uoi->buf;

	return 0;
}


struct vos_fs_ult_arg {
	void *args;
	enum task_op opc;
	double *duration;
	int status;
};

static int
_vos_parse_commands(enum task_op opc, void *union_args)
{
	int rc = 0;

	if (opc == VOS_OBJ_UPDATE) {
		struct vos_client_obj_rw_args *args = union_args;

		rc = vos_obj_update(args->coh, args->oid,
				    args->epoch, args->pm_ver,
				    args->flags, args->dkey,
				    args->iod_nr, args->iods, NULL,
				    args->sgls);

	} else if (opc == VOS_OBJ_FETCH) {
		struct vos_client_obj_rw_args *args = union_args;

		rc = vos_obj_fetch(args->coh, args->oid,
				   args->epoch, args->flags,
				   args->dkey, args->iod_nr,
				   args->iods, args->sgls);
	} else if (opc == VOS_OBJ_PUNCH) {
		struct vos_client_obj_rw_args *args = union_args;

		rc = vos_obj_punch(args->coh, args->oid,
				   args->epoch, args->pm_ver,
				   args->flags, args->dkey,
				   args->akey_nr, args->akeys,
				   NULL);

	} else if (opc == VOS_OBJ_GET_NUM_DKEYS) {
		struct vos_client_obj_list_args *args = union_args;

		rc = vos_obj_get_info_dkeys(args->coh, args->oid,
					    opc, args->nr, args->len,
					    args->kds, args->buf);
	} else if (opc == VOS_OBJ_LIST_DKEYS) {
		struct vos_client_obj_list_args *args = union_args;

		rc = vos_obj_list_info_dkeys(args->coh, args->oid,
					     opc, args->nr, args->len,
					     args->kds, args->buf);
	} else {
		  D_ERROR("Command is not recongnized.");
		  rc = -1;
	}

	return rc;
}

static int
_vos_fs_execute_command(void *_args, enum task_op opc, double *duration)
{
	double then, now;
	int rc = 0;

	then = dts_time_now();
	rc = _vos_parse_commands(opc, _args);

	now = dts_time_now();
	*duration = now - then;
#if DEBUG
	D_PRINT("execute command %s end success\r\n", cmd_string[opc]);
	D_PRINT("Dts last time = %-10.6fs\r\n", now - then);
#endif
	return rc;
}

static void
vos_fs_execute_command_ult(void *arg)
{
	struct vos_fs_ult_arg *vfua = arg;

	D_INFO("execute command %s\r\n", cmd_string[vfua->opc]);
	vfua->status =
		_vos_fs_execute_command(vfua->args, vfua->opc,
					vfua->duration);
}

int
pmfs_thread_create(void *fs_cb, void *arg)
{
	int rc;

	ABT_thread thread;
	ABT_xstream xstream;

	ABT_xstream_create(ABT_SCHED_NULL, &xstream);
	ABT_xstream_get_main_pools(xstream, 1, &pool);

	rc = ABT_thread_create(pool, fs_cb, arg, ABT_THREAD_ATTR_NULL, &thread);
	if (rc != ABT_SUCCESS) {
		D_ERROR("ABT_thread_create failed: " DF_RC "\n",
			DP_RC(rc));
		return rc;
	}

	rc = ABT_thread_join(thread);
	if (rc != ABT_SUCCESS) {
		D_ERROR("ABT_thread_join failed:" DF_RC "\n",
			DP_RC(rc));
		return rc;
	}

	ABT_thread_free(&thread);
	ABT_xstream_free(&xstream);
	return 0;
}

static int
vos_task_ult(void *args, enum task_op opc, double *duration)
{
	struct vos_fs_ult_arg vfua;
	int rc;

	vfua.args = args;
	vfua.duration = duration;
	vfua.opc = opc;

	rc = pmfs_thread_create(vos_fs_execute_command_ult, (void *)&vfua);
	if (rc)
		return rc;
	duration = vfua.duration;

	return 0;
}

static int
vos_task_dequeue(struct vos_fs_cmd_args *vfca)
{
	struct spdk_ring *task_ring;
	struct vos_client_task *vct;
	int rc = 0;

	task_ring = vos_task_get_ring(vfca->vfcmd, vfca);
	if (task_ring == NULL || !spdk_ring_count(task_ring)) {
		return 0;
	}

	do {
		D_INFO("find the ring\r\n");
		spdk_ring_dequeue(task_ring, (void **)&vct, 1);
		if (vct->opc == VOS_OBJ_UPDATE || vct->opc == VOS_OBJ_FETCH
		    || vct->opc == VOS_OBJ_PUNCH) {
			D_INFO("start to ult obj rw\r\n");
			rc = vos_task_ult(&vct->args.obj_rw, vct->opc,
					  vfca->duration);
			if (rc != 0) {
				D_ERROR("vos_task_ult starts object rw failed:" DF_RC "\n",
					DP_RC(rc));
				return rc;
			}
		} else {
			D_INFO("start to ult obj list\r\n");
			rc = vos_task_ult(&vct->args.obj_list, vct->opc,
					  vfca->duration);
			if (rc != 0) {
				D_ERROR("vos_task_ult starts object list failed:" DF_RC "\n",
					DP_RC(rc));
				return rc;
			}
		}
		D_INFO("start cb to sem wait\r\n");
		vct->cb_fn(vct->cb_args, rc);
		D_INFO("sem pos\r\n");
	} while (spdk_ring_count(task_ring));

	return 0;
}

static void
collect_fs_tasks(void *arg)
{
	struct vos_fs_cmd_args *vfca = arg;

	D_ASSERT(vfca != NULL);
	D_ALLOC(vfca->task_ring_list, sizeof(struct ring_list));
	if (vfca->task_ring_list == NULL) {
		return;
	}

	D_INIT_LIST_HEAD(&vfca->task_ring_list->rl);
}

static void
vos_end_tasks(void *arg)
{
	struct ring_list *task_ring_list, *tmp;

	d_list_for_each_entry_safe(task_ring_list, tmp, &g_task_ring, rl) {
		d_list_del(&task_ring_list->rl);
		D_FREE(task_ring_list);
	}

}

static int
vos_task_completion(void *args)
{
	struct vos_fs_cmd_args *vfca = args;
	int rc;

	rc = vos_task_dequeue(vfca);
	return rc;
}

struct spdk_ring *
vos_task_get_ring(const char *name, void *arg)
{
	struct ring_list *task_ring_list;
	struct vos_fs_cmd_args *vfca = arg;

	if (!name || !vfca->task_ring_list) {
		return NULL;
	}

	vfca->task_ring_list->rl = g_task_ring;
	if (d_list_empty(&vfca->task_ring_list->rl)) {
		return NULL;
	}

	d_list_for_each_entry(task_ring_list, &vfca->task_ring_list->rl, rl) {
		if (strcmp(task_ring_list->ring_name, name) == 0) {
			return task_ring_list->task_ring;
		}
	}

	return NULL;
}

void
vos_task_bind_ring(const char *name,
		   struct spdk_ring *ring, struct ring_list *ring_list)
{
	D_MUTEX_LOCK(&vos_fs_cmd_lock);

	D_ALLOC(ring_list, sizeof(struct ring_list));
	D_ASSERT(ring_list != NULL);
	ring_list->ring_name = name;
	ring_list->task_ring = ring;
	d_list_add(&ring_list->rl, &g_task_ring);

	D_MUTEX_UNLOCK(&vos_fs_cmd_lock);
}

void
vos_task_process_init(void *arg)
{
	struct vos_fs_cmd_args *vfca = arg;
	int rc = 0;

	rc = vt_ctx_init(vfca->pmfs_ctx);
	if (rc) {
		D_ERROR("context init error\n");
		return;
	}
	rc = fs_abt_init();
	if (rc) {
		D_ERROR("ABT init error\n");
		return;
	}

	collect_fs_tasks(vfca);
	D_INFO("process init ok\r\n");
}

void
vos_task_process_fini(void *arg)
{
	struct vos_fs_cmd_args *vfca = arg;

	fs_abt_fini();
	vos_end_tasks(vfca);
	vt_ctx_fini(vfca->pmfs_ctx);
	D_INFO("process fini\r\n");
}

void
vos_task_process(void *arg)
{
	struct vos_fs_cmd_args *vfca = arg;
	int rc;

	while (!vfca->force_exit) {
		rc = vos_task_completion(vfca);
		if (rc)
			break;
	}
}

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

	mount_args->errorno = pmfs_mount(mount_args->poh, mount_args->coh,
					 mount_args->flags,
					 mount_args->pmfs);
}

void
pmfs_umount_cb(void *arg)
{
	struct umount_args *umount_args = arg;

	umount_args->errorno = pmfs_umount(umount_args->pmfs);
}

void
pmfs_mkdir_cb(void *arg)
{
	struct mkdir_args *mkdir_args = arg;

	mkdir_args->errorno = pmfs_mkdir(mkdir_args->pmfs, mkdir_args->parent,
					 mkdir_args->name,
					 mkdir_args->mode);
}

void
pmfs_listdir_cb(void *arg)
{
	struct listdir_args *listdir_args = arg;

	listdir_args->errorno = pmfs_listdir(listdir_args->pmfs, listdir_args->obj,
					     &listdir_args->nr);
}

void
pmfs_remove_cb(void *arg)
{
	struct remove_args *remove_args = arg;

	remove_args->errorno =	pmfs_remove(remove_args->pmfs, remove_args->parent,
					    remove_args->name, remove_args->force,
					    remove_args->oid);
}

void
pmfs_open_cb(void *arg)
{
	struct open_args *open_args = arg;

	open_args->errorno = pmfs_open(open_args->pmfs, open_args->parent, open_args->name,
				       open_args->mode, open_args->flags, open_args->chunk_size,
				       open_args->value, &open_args->obj);
}

void
pmfs_readdir_cb(void *arg)
{
	struct readdir_args *readdir_args = arg;

	readdir_args->errorno = pmfs_readdir(readdir_args->pmfs, readdir_args->obj,
					     readdir_args->nr, readdir_args->dirs);
}

void
pmfs_lookup_cb(void *arg)
{
	struct lookup_args *lookup_args = arg;

	lookup_args->errorno = pmfs_lookup(lookup_args->pmfs, lookup_args->path,
					   lookup_args->flags, &lookup_args->obj,
					   lookup_args->mode, lookup_args->stbuf);
}

void
pmfs_release_cb(void *arg)
{
	struct release_args *release_args = arg;

	release_args->errorno = pmfs_release(release_args->obj);
}

void
pmfs_punch_cb(void *arg)
{
	struct punch_args *punch_args = arg;

	punch_args->errorno = pmfs_punch(punch_args->pmfs, punch_args->obj,
					 punch_args->offset, punch_args->len);
}

int
pmfs_write_cb(void *arg)
{
	struct write_args *write_args = arg;
	d_sg_list_t     *sgl = write_args->user_sgl;
	daos_size_t     off = write_args->off;
	int     rc = 0;

	g_vfca->vfcmd = "PMFS_TASKS";
	D_MUTEX_LOCK(&vos_fs_cmd_lock);
	write_args->errorno = pmfs_write_sync(write_args->pmfs, write_args->obj,
					      sgl, off);
	D_MUTEX_UNLOCK(&vos_fs_cmd_lock);
	rc = write_args->errorno;
	if (rc != 0) {
		D_PRINT("write error\r\n");
		return rc;
	}

	return 0;
}

int
pmfs_read_cb(void *arg)
{
	struct read_args *read_args = arg;

	d_sg_list_t *sgl = read_args->user_sgl;
	daos_size_t off = read_args->off;
	daos_size_t read_size  = *read_args->read_size;
	int rc = 0;

	g_vfca->vfcmd = "PMFS_TASKS";

	D_MUTEX_LOCK(&vos_fs_cmd_lock);
	read_args->errorno = pmfs_read_sync(read_args->pmfs, read_args->obj, sgl, off,
					    &read_size);
	D_MUTEX_UNLOCK(&vos_fs_cmd_lock);
	rc = read_args->errorno;
	if (rc != 0) {
		read_args->errorno = -EINVAL;
		return rc;
	}

	return 0;
}

void
pmfs_stat_cb(void *arg)
{
	struct stat_args *stat_args = arg;

	stat_args->errorno = pmfs_stat(stat_args->pmfs, stat_args->parent,
				       stat_args->name, stat_args->stbuf);
}

void
pmfs_rename_cb(void *arg)
{
	struct rename_args *rename_args = arg;

	rename_args->errorno = pmfs_rename(rename_args->pmfs, rename_args->parent,
					   rename_args->old_name, rename_args->new_name);
}

void
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

	rc = pmfs_thread_create(pmfs_mount_cb, (void *)&mount_args);
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

	rc = pmfs_thread_create(pmfs_mkdir_cb, (void *)&mkdir_args);
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

	rc = pmfs_thread_create(pmfs_listdir_cb, (void *)&listdir_args);
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

	rc = pmfs_thread_create(pmfs_remove_cb, (void *)&remove_args);
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

	rc = pmfs_thread_create(pmfs_open_cb, (void *)&open_args);
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

	rc = pmfs_thread_create(pmfs_readdir_cb, (void *)&readdir_args);
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

	rc = pmfs_thread_create(pmfs_lookup_cb, (void *)&lookup_args);
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

	rc = pmfs_thread_create(pmfs_punch_cb, (void *)&punch_args);
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

	rc = pmfs_thread_create(pmfs_write_cb, (void *)&write_args);
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

	rc = pmfs_thread_create(pmfs_read_cb, (void *)&read_args);
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

	rc = pmfs_thread_create(pmfs_stat_cb, (void *)&stat_args);
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

	rc = pmfs_thread_create(pmfs_rename_cb, (void *)&rename_args);
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

	rc = pmfs_thread_create(pmfs_truncate_cb, (void *)&truncate_args);
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

	rc = pmfs_thread_create(pmfs_release_cb, (void *)&release_args);
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

	rc = pmfs_thread_create(pmfs_umount_cb, (void *)&umount_args);
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
	daos_handle_t tmp_coh;
	int rc = 0;

	g_vfca->pmfs_ctx->pmfs_pool = *pmfs_pool;
	D_PRINT("---------------start scan pool---------------------------\r\n");
	D_PRINT("---------------rebuild container list before mount-------\r\n");
	rc = pmfs_init_pool(g_vfca, ctx);
	if (rc != 0)
		return NULL;

	D_PRINT("---------------rebuild container list done---------------\r\n");
	/* start mount thread */
	tmp_coh = g_vfca->pmfs_ctx->pmfs_pool.pmfs_container.tsc_coh;
	rc = pmfs_mount_start(g_vfca->pmfs_ctx->pmfs_pool.tsc_poh, tmp_coh,
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

	rc = pmfs_thread_create(pmfs_mkfs_cb, (void *)&mkfs_args);
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
	d_list_add(&pmfs_pool->pl, &g_vos_pool_list);
	if (amend) {
		engine_pool_single_node_init(pmfs_pool, false);
		pmfs_combine_pool_fini_list(&g_vos_fini_list);
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
	/* add pool mapping to /mnt/daos/pmfs_cli0.pmem,tsc_nvme_size,
	 * tsc_scm_size, skip container create
	 */
	pmfs_ctx->pmfs_pool = pmfs_add_single_pool("/mnt/daos/pmfs_cli0.pmem",
						   tsc_nvme_size, tsc_scm_size, true,
						   false);
	/* That aims to associated with engine lib */
	pmfs_ctx->pmfs_pool.pl = g_vos_pool_list;

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

	g_vfca->pmfs_ctx = pmfs_set_ctx(tsc_nvme_size, tsc_scm_size);
	/* Start to init process env */
	vos_task_process_init(g_vfca);
	pmfs_combine_pool_fini_list(&g_vos_fini_list);

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
	rc = pmfs_thread_create(pmfs_mkfs_cb, (void *)&mags);
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
