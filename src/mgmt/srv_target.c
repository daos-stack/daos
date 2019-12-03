/*
 * (C) Copyright 2016-2019 Intel Corporation.
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
/*
 * Target Methods
 */
#define D_LOGFAC	DD_FAC(mgmt)

#include <sys/stat.h>
#include <fcntl.h>
#include <linux/falloc.h>
#include <sys/sysinfo.h>
#include <ftw.h>
#include <dirent.h>

#include <daos_srv/vos.h>
#include <daos_srv/pool.h>
#include <daos_srv/daos_mgmt_srv.h>

#include "srv_internal.h"
#include "srv_layout.h"		/* for a couple of constants only */

/** directory for newly created pool, reclaimed on restart */
static char *newborns_path;
/** directory for destroyed pool */
static char *zombies_path;

static inline int
dir_fsync(const char *path)
{
	int	fd;
	int	rc;

	fd = open(path, O_RDONLY|O_DIRECTORY);
	if (fd < 0) {
		D_ERROR("failed to open %s for sync: %d\n", path, errno);
		return daos_errno2der(errno);
	}

	rc = fsync(fd);
	if (rc < 0) {
		D_ERROR("failed to fync %s: %d\n", path, errno);
		rc = daos_errno2der(errno);
	}

	(void)close(fd);

	return rc;
}

static int
destroy_cb(const char *path, const struct stat *sb, int flag,
	   struct FTW *ftwbuf)
{
	int rc;

	if (ftwbuf->level == 0)
		return 0;

	if (flag == FTW_DP || flag == FTW_D)
		rc = rmdir(path);
	else
		rc = unlink(path);
	if (rc)
		D_ERROR("failed to remove %s\n", path);
	return rc;
}

static int
subtree_destroy(const char *path)
{
	int rc;

	rc = nftw(path, destroy_cb, 32, FTW_DEPTH | FTW_PHYS | FTW_MOUNT);
	if (rc)
		rc = daos_errno2der(errno);

	return rc;
}

int
ds_mgmt_tgt_init(void)
{
	mode_t	stored_mode, mode;
	int	rc;

	/** create the path string */
	rc = asprintf(&newborns_path, "%s/NEWBORNS", dss_storage_path);
	if (rc < 0)
		D_GOTO(err, rc = -DER_NOMEM);
	rc = asprintf(&zombies_path, "%s/ZOMBIES", dss_storage_path);
	if (rc < 0)
		D_GOTO(err_newborns, rc = -DER_NOMEM);

	stored_mode = umask(0);
	mode = S_IRWXU | S_IRWXG | S_IRWXO;
	/** create NEWBORNS directory if it does not exist already */
	rc = mkdir(newborns_path, mode);
	if (rc < 0 && errno != EEXIST) {
		D_ERROR("failed to create NEWBORNS dir: %d\n", errno);
		umask(stored_mode);
		D_GOTO(err_zombies, rc = daos_errno2der(errno));
	}

	/** create ZOMBIES directory if it does not exist already */
	rc = mkdir(zombies_path, mode);
	if (rc < 0 && errno != EEXIST) {
		D_ERROR("failed to create ZOMBIES dir: %d\n", errno);
		umask(stored_mode);
		D_GOTO(err_zombies, rc = daos_errno2der(errno));
	}
	umask(stored_mode);

	/** remove leftover from previous runs */
	rc = subtree_destroy(newborns_path);
	if (rc)
		/** only log error, will try again next time */
		D_ERROR("failed to cleanup NEWBORNS dir: %d, will try again\n",
			rc);
	rc = subtree_destroy(zombies_path);
	if (rc)
		/** only log error, will try again next time */
		D_ERROR("failed to cleanup ZOMBIES dir: %d, will try again\n",
			rc);
	return 0;

err_zombies:
	D_FREE(zombies_path);
err_newborns:
	D_FREE(newborns_path);
err:
	return rc;
}

void
ds_mgmt_tgt_fini(void)
{
	D_FREE(zombies_path);
	D_FREE(newborns_path);
}

static int
path_gen(const uuid_t pool_uuid, const char *dir, const char *fname, int *idx,
	 char **fpath)
{
	int	 size;
	int	 off;

	/** *fpath = dir + "/" + pool_uuid + "/" + fname + idx */

	/** DAOS_UUID_STR_SIZE includes the trailing '\0' */
	size = strlen(dir) + 1 /* "/" */ + DAOS_UUID_STR_SIZE;
	if (fname != NULL || idx != NULL)
		size += 1 /* "/" */;
	if (fname)
		size += strlen(fname);
	if (idx)
		size += snprintf(NULL, 0, "%d", *idx);

	D_ALLOC(*fpath, size);
	if (*fpath == NULL)
		return -DER_NOMEM;

	off = sprintf(*fpath, "%s", dir);
	off += sprintf(*fpath + off, "/");
	uuid_unparse_lower(pool_uuid, *fpath + off);
	off += DAOS_UUID_STR_SIZE - 1;
	if (fname != NULL || idx != NULL)
		off += sprintf(*fpath + off, "/");
	if (fname)
		off += sprintf(*fpath + off, "%s", fname);
	if (idx)
		sprintf(*fpath + off, "%d", *idx);

	return 0;
}

/**
 * Generate path to a target file for pool \a pool_uuid with a filename set to
 * \a fname and suffixed by \a idx. \a idx can be NULL.
 */
int
ds_mgmt_tgt_file(const uuid_t pool_uuid, const char *fname, int *idx,
		 char **fpath)
{
	return path_gen(pool_uuid, dss_storage_path, fname, idx, fpath);
}

/**
 * Iterate pools that have targets on this node by scanning the storage. \a cb
 * will be called with the UUID of each pool. When \a cb returns an rc,
 *
 *   - if rc == 0, the iteration continues;
 *   - if rc == 1, the iteration stops and returns 0;
 *   - otherwise, the iteration stops and returns rc.
 *
 * \param[in]	cb	callback called for each pool
 * \param[in]	arg	argument passed to each \a cb call
 */
int
ds_mgmt_tgt_pool_iterate(int (*cb)(uuid_t uuid, void *arg), void *arg)
{
	DIR    *storage;
	int	rc;
	int	rc_tmp;

	storage = opendir(dss_storage_path);
	if (storage == NULL) {
		D_ERROR("failed to open %s: %d\n", dss_storage_path, errno);
		return daos_errno2der(errno);
	}

	for (;;) {
		struct dirent  *entry;
		uuid_t		uuid;

		rc = 0;
		errno = 0;
		entry = readdir(storage);
		if (entry == NULL) {
			if (errno != 0) {
				D_ERROR("failed to read %s: %d\n",
					dss_storage_path, errno);
				rc = daos_errno2der(errno);
			}
			break;
		}

		/* A pool directory must have a valid UUID as its name. */
		rc = uuid_parse(entry->d_name, uuid);
		if (rc != 0)
			continue;

		rc = cb(uuid, arg);
		if (rc != 0) {
			if (rc == 1)
				rc = 0;
			break;
		}
	}

	rc_tmp = closedir(storage);
	if (rc_tmp != 0) {
		D_ERROR("failed to close %s: %d\n", dss_storage_path, errno);
		rc_tmp = daos_errno2der(errno);
	}

	return rc == 0 ? rc_tmp : rc;
}

struct vos_pool_arg {
	uuid_t		vpa_uuid;
	daos_size_t	vpa_scm_size;
	daos_size_t	vpa_nvme_size;
};

static int
tgt_vos_create_one(void *varg)
{
	struct dss_module_info	*info = dss_get_module_info();
	struct vos_pool_arg	*vpa = varg;
	char			*path = NULL;
	int			 rc;

	rc = path_gen(vpa->vpa_uuid, newborns_path, VOS_FILE, &info->dmi_tgt_id,
		      &path);
	if (rc)
		return rc;

	rc = vos_pool_create(path, (unsigned char *)vpa->vpa_uuid,
			     vpa->vpa_scm_size, vpa->vpa_nvme_size);
	if (rc)
		D_ERROR(DF_UUID": failed to init vos pool %s: %d\n",
			DP_UUID(vpa->vpa_uuid), path, rc);

	if (path)
		D_FREE(path);
	return rc;
}

struct vos_create {
	uuid_t		vc_uuid;
	daos_size_t	vc_scm_size;
	int		vc_tgt_nr;
	int		vc_rc;
};

static void *
tgt_vos_preallocate(void *arg)
{
	char			*path = NULL;
	struct vos_create	*vc = arg;
	int			 i;
	int			 fd = -1;

	for (i = 0; i < vc->vc_tgt_nr; i++) {
		vc->vc_rc = path_gen(vc->vc_uuid, newborns_path, VOS_FILE, &i,
				     &path);
		if (vc->vc_rc)
			break;

		D_DEBUG(DB_MGMT, DF_UUID": creating vos file %s\n",
			DP_UUID(vc->vc_uuid), path);

		fd = open(path, O_CREAT|O_RDWR, 0600);
		if (fd < 0) {
			vc->vc_rc = daos_errno2der(errno);
			D_ERROR(DF_UUID": failed to create vos file %s: %d\n",
				DP_UUID(vc->vc_uuid), path, vc->vc_rc);
			break;
		}

		/**
		 * Pre-allocate blocks for vos files in order to provide
		 * consistent performance and avoid entering into the backend
		 * filesystem allocator through page faults.
		 * Use fallocate(2) instead of posix_fallocate(3) since the
		 * latter is bogus with tmpfs.
		 */
		vc->vc_rc = fallocate(fd, 0, 0, vc->vc_scm_size);
		if (vc->vc_rc) {
			vc->vc_rc = daos_errno2der(errno);
			D_ERROR(DF_UUID": failed to allocate vos file %s with "
				"size: "DF_U64", rc: %d, %s.\n",
				DP_UUID(vc->vc_uuid), path, vc->vc_scm_size,
				vc->vc_rc, strerror(errno));
			break;
		}

		vc->vc_rc = fsync(fd);
		(void)close(fd);
		fd = -1;
		if (vc->vc_rc) {
			D_ERROR(DF_UUID": failed to sync vos pool %s: %d\n",
				DP_UUID(vc->vc_uuid), path, vc->vc_rc);
			vc->vc_rc = daos_errno2der(errno);
			break;
		}
		D_FREE(path);
	}

	if (fd != -1)
		close(fd);

	D_FREE(path);

	return NULL;
}

static int
tgt_vos_create(uuid_t uuid, daos_size_t tgt_scm_size, daos_size_t tgt_nvme_size)
{
	daos_size_t		scm_size, nvme_size;
	struct vos_create	vc = {0};
	int			rc = 0;
	pthread_t		thread;


	/**
	 * Create one VOS file per execution stream
	 * 16MB minimum per pmemobj file (SCM partition)
	 */
	D_ASSERT(dss_tgt_nr > 0);
	scm_size = max(tgt_scm_size / dss_tgt_nr, 1 << 24);
	nvme_size = tgt_nvme_size / dss_tgt_nr;

	vc.vc_tgt_nr = dss_tgt_nr;
	vc.vc_scm_size = scm_size;
	uuid_copy(vc.vc_uuid, uuid);

	rc = pthread_create(&thread, NULL, tgt_vos_preallocate, &vc);
	if (rc != 0) {
		rc = daos_errno2der(errno);
		D_ERROR(DF_UUID": failed to create thread for vos file "
			"creation: %d\n", DP_UUID(uuid), rc);
		return rc;
	}

	for (;;) {
		rc = pthread_tryjoin_np(thread, NULL);
		if (rc == 0)
			break;
		ABT_thread_yield();
	}

	rc = vc.vc_rc;
	if (!rc) {
		struct vos_pool_arg	vpa;

		uuid_copy(vpa.vpa_uuid, uuid);
		/* A zero size accommodates the existing file */
		vpa.vpa_scm_size = 0;
		vpa.vpa_nvme_size = nvme_size;

		rc = dss_thread_collective(tgt_vos_create_one, &vpa, 0);
	}

	/** brute force cleanup to be done by the caller */
	return rc;
}

static int
tgt_create(uuid_t pool_uuid, uuid_t tgt_uuid, daos_size_t scm_size,
	   daos_size_t nvme_size, char *path)
{
	char	*newborn = NULL;
	int	 rc;

	/** XXX: many synchronous/blocking operations below */

	/** create the pool directory under NEWBORNS */
	rc = path_gen(pool_uuid, newborns_path, NULL, NULL, &newborn);
	if (rc)
		return rc;

	rc = mkdir(newborn, 0700);
	if (rc < 0 && errno != EEXIST) {
		D_ERROR("failed to created pool directory: %d\n", rc);
		D_GOTO(out, rc = daos_errno2der(errno));
	}

	/** create VOS files */
	rc = tgt_vos_create(pool_uuid, scm_size, nvme_size);
	if (rc)
		D_GOTO(out_tree, rc);

	/** initialize DAOS-M target and fetch uuid */
	rc = ds_pool_create(pool_uuid, newborn, tgt_uuid);
	if (rc) {
		D_ERROR("ds_pool_create failed, rc: %d.\n", rc);
		D_GOTO(out_tree, rc);
	}

	/** ready for prime time, move away from NEWBORNS dir */
	rc = rename(newborn, path);
	if (rc < 0) {
		D_ERROR("failed to rename pool directory: %d\n", rc);
		D_GOTO(out_tree, rc = daos_errno2der(errno));
	}

	/** make sure the rename is persistent */
	rc = dir_fsync(path);

	D_GOTO(out, rc);

out_tree:
	/** cleanup will be re-executed on several occasions */
	(void)subtree_destroy(newborn);
	(void)rmdir(newborn);
out:
	D_FREE(newborn);
	return rc;
}

int
ds_mgmt_tgt_create_aggregator(crt_rpc_t *source, crt_rpc_t *result,
			      void *priv)
{
	struct mgmt_tgt_create_out	*tc_out;
	struct mgmt_tgt_create_out	*ret_out;
	uuid_t				*tc_uuids;
	d_rank_t			*tc_ranks;
	unsigned int			tc_uuids_nr;
	uuid_t				*ret_uuids;
	d_rank_t			*ret_ranks;
	unsigned int			ret_uuids_nr;
	uuid_t				*new_uuids;
	d_rank_t			*new_ranks;
	unsigned int			new_uuids_nr;
	int				i;

	tc_out = crt_reply_get(source);
	tc_uuids_nr = tc_out->tc_tgt_uuids.ca_count;
	tc_uuids = tc_out->tc_tgt_uuids.ca_arrays;
	tc_ranks = tc_out->tc_ranks.ca_arrays;

	ret_out = crt_reply_get(result);
	ret_uuids_nr = ret_out->tc_tgt_uuids.ca_count;
	ret_uuids = ret_out->tc_tgt_uuids.ca_arrays;
	ret_ranks = ret_out->tc_ranks.ca_arrays;

	if (tc_out->tc_rc != 0)
		ret_out->tc_rc = tc_out->tc_rc;
	if (tc_uuids_nr == 0)
		return 0;

	new_uuids_nr = ret_uuids_nr + tc_uuids_nr;

	/* Append tc_uuids to ret_uuids */
	D_ALLOC_ARRAY(new_uuids, new_uuids_nr);
	if (new_uuids == NULL)
		return -DER_NOMEM;

	D_ALLOC_ARRAY(new_ranks, new_uuids_nr);
	if (new_ranks == NULL) {
		D_FREE(new_uuids);
		return -DER_NOMEM;
	}

	for (i = 0; i < ret_uuids_nr + tc_uuids_nr; i++) {
		if (i < ret_uuids_nr) {
			uuid_copy(new_uuids[i], ret_uuids[i]);
			new_ranks[i] = ret_ranks[i];
		} else {
			uuid_copy(new_uuids[i], tc_uuids[i - ret_uuids_nr]);
			new_ranks[i] = tc_ranks[i - ret_uuids_nr];
		}
	}

	D_FREE(ret_uuids);
	D_FREE(ret_ranks);

	ret_out->tc_tgt_uuids.ca_arrays = new_uuids;
	ret_out->tc_tgt_uuids.ca_count = new_uuids_nr;
	ret_out->tc_ranks.ca_arrays = new_ranks;
	ret_out->tc_ranks.ca_count = new_uuids_nr;
	return 0;
}

/**
 * RPC handler for target creation
 */
void
ds_mgmt_hdlr_tgt_create(crt_rpc_t *tc_req)
{
	struct mgmt_tgt_create_in	*tc_in;
	struct mgmt_tgt_create_out	*tc_out;
	uuid_t				tgt_uuid;
	d_rank_t			*rank;
	uuid_t				*tmp_tgt_uuid;
	char				*path = NULL;
	int				 rc = 0;

	/** incoming request buffer */
	tc_in = crt_req_get(tc_req);
	/** reply buffer */
	tc_out = crt_reply_get(tc_req);
	D_ASSERT(tc_in != NULL && tc_out != NULL);

	/** generate path to the target directory */
	rc = ds_mgmt_tgt_file(tc_in->tc_pool_uuid, NULL, NULL, &path);
	if (rc)
		D_GOTO(out, rc);

	/** check whether the target already exists */
	rc = access(path, F_OK);
	if (rc >= 0) {
		/** target already exists, let's reuse it for idempotence */
		/** TODO: fetch tgt uuid from existing DSM pool */
		uuid_generate(tgt_uuid);

		/**
		 * flush again in case the previous one in tgt_create()
		 * failed
		 */
		rc = dir_fsync(path);
	} else if (errno == ENOENT) {
		/** target doesn't exist, create one */
		rc = tgt_create(tc_in->tc_pool_uuid, tgt_uuid,
				tc_in->tc_scm_size, tc_in->tc_nvme_size, path);
	} else {
		rc = daos_errno2der(errno);
	}

	if (rc)
		D_GOTO(free, rc);

	D_ALLOC_PTR(tmp_tgt_uuid);
	if (tmp_tgt_uuid == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	uuid_copy(*tmp_tgt_uuid, tgt_uuid);
	tc_out->tc_tgt_uuids.ca_arrays = tmp_tgt_uuid;
	tc_out->tc_tgt_uuids.ca_count = 1;

	D_ALLOC_PTR(rank);
	if (rank == NULL) {
		D_FREE(tmp_tgt_uuid);
		D_GOTO(free, rc = -DER_NOMEM);
	}

	rc = crt_group_rank(NULL, rank);
	D_ASSERT(rc == 0);
	tc_out->tc_ranks.ca_arrays = rank;
	tc_out->tc_ranks.ca_count = 1;

free:
	D_FREE(path);
out:
	tc_out->tc_rc = rc;
	crt_reply_send(tc_req);
}

int
tgt_kill_pool(void *args)
{
	struct d_uuid	*id = args;

	/* XXX: there are a few test cases that leak pool close
	 * before destroying pool, we have to force the kill to pass
	 * those tests, but we should try to disable "force" and
	 * fix those issues in the future.
	 */
	return vos_pool_kill(id->uuid, true);
}

static int
tgt_destroy(uuid_t pool_uuid, char *path)
{
	char	      *zombie = NULL;
	struct d_uuid  id;
	int	       rc;

	/** XXX: many synchronous/blocking operations below */

	/** move target directory to ZOMBIES */
	rc = path_gen(pool_uuid, zombies_path, NULL, NULL, &zombie);
	if (rc)
		return rc;

	/* destroy blobIDs first */
	uuid_copy(id.uuid, pool_uuid);
	rc = dss_thread_collective(tgt_kill_pool, &id, 0);
	if (rc)
		D_GOTO(out, rc);

	rc = rename(path, zombie);
	if (rc < 0)
		D_GOTO(out, rc = daos_errno2der(errno));

	/** make sure the rename is persistent */
	rc = dir_fsync(zombie);
	if (rc < 0)
		D_GOTO(out, rc);

	/**
	 * once successfully moved to the ZOMBIES directory, the target will
	 * take care of retrying on failure and thus always report success to
	 * the caller.
	 */
	(void)subtree_destroy(zombie);
	(void)rmdir(zombie);
out:
	D_FREE(zombie);
	return rc;
}

/**
 * RPC handler for target destroy
 */
void
ds_mgmt_hdlr_tgt_destroy(crt_rpc_t *td_req)
{
	struct mgmt_tgt_destroy_in	*td_in;
	struct mgmt_tgt_destroy_out	*td_out;
	char				*path;
	int				  rc;

	/** incoming request buffer */
	td_in = crt_req_get(td_req);
	/** reply buffer */
	td_out = crt_reply_get(td_req);
	D_ASSERT(td_in != NULL && td_out != NULL);

	/** generate path to the target directory */
	rc = ds_mgmt_tgt_file(td_in->td_pool_uuid, NULL, NULL, &path);
	if (rc)
		D_GOTO(out, rc);

	/** check whether the target exists */
	rc = access(path, F_OK);
	if (rc >= 0) {
		/** target is still there, destroy it */
		rc = tgt_destroy(td_req->cr_input, path);
	} else if (errno == ENOENT) {
		char	*zombie;

		/**
		 * target is gone already, report success for idempotence
		 * that said, the previous flush in tgt_destroy() might have
		 * failed, so flush again.
		 */
		rc = path_gen(td_in->td_pool_uuid, zombies_path, NULL, NULL,
			      &zombie);
		if (rc)
			D_GOTO(out, rc);
		rc = dir_fsync(path);
		if (rc == -DER_NONEXIST)
			rc = 0;
		D_FREE(zombie);
	} else {
		rc = daos_errno2der(errno);
	}

	D_FREE(path);
out:
	td_out->td_rc = rc;
	crt_reply_send(td_req);
}

/**
 * Set parameter on a single target.
 */
void
ds_mgmt_tgt_params_set_hdlr(crt_rpc_t *rpc)
{
	struct mgmt_tgt_params_set_in	*in;
	struct mgmt_tgt_params_set_out	*out;
	int rc;

	in = crt_req_get(rpc);
	D_ASSERT(in != NULL);

	rc = dss_parameters_set(in->tps_key_id, in->tps_value);
	if (rc == 0 && in->tps_key_id == DSS_KEY_FAIL_LOC)
		rc = dss_parameters_set(DSS_KEY_FAIL_VALUE,
					in->tps_value_extra);
	if (rc)
		D_ERROR("Set parameter failed key_id %d: rc %d\n",
			 in->tps_key_id, rc);

	out = crt_reply_get(rpc);
	out->srv_rc = rc;
	crt_reply_send(rpc);
}

static int
tgt_profile_task(void *arg)
{
	struct mgmt_profile_in *in = arg;
	int mod_id = 0;
	int rc = 0;

	for (mod_id = 0; mod_id < 64; mod_id++) {
		uint64_t mask = 1 << mod_id;
		struct dss_module *module;

		if (!(in->p_module & mask))
			continue;

		module = dss_module_get(mod_id);
		if (module == NULL || module->sm_mod_ops == NULL) {
			D_ERROR("no module sm_mod_ops %d\n", mod_id);
			continue;
		}

		if (in->p_op == MGMT_PROFILE_START)
			rc = module->sm_mod_ops->dms_profile_start(in->p_path);
		else
			rc = module->sm_mod_ops->dms_profile_stop();
		if (rc)
			break;
	}

	D_DEBUG(DB_MGMT, "profile task: rc %d\n", rc);
	return rc;
}

/**
 * start/stop profile on a single target.
 */
void
ds_mgmt_tgt_profile_hdlr(crt_rpc_t *rpc)
{
	struct mgmt_profile_in	*in;
	struct mgmt_profile_out	*out;
	int rc;

	in = crt_req_get(rpc);
	D_ASSERT(in != NULL);

	rc = dss_task_collective(tgt_profile_task, in, 0);

	out = crt_reply_get(rpc);
	out->p_rc = rc;
	crt_reply_send(rpc);
}

/**
 * Do Mark on a single target.
 */
void
ds_mgmt_tgt_mark_hdlr(crt_rpc_t *rpc)
{
	struct mgmt_mark_in	*in;
	struct mgmt_mark_out	*out;

	in = crt_req_get(rpc);
	D_ASSERT(in != NULL);

	D_DEBUG(DB_TRACE, "Mark trace %s.\n", in->m_mark);

	out = crt_reply_get(rpc);
	out->m_rc = 0;
	crt_reply_send(rpc);
}

/* State of the local system map (i.e., the CaRT PG membership) */
static uint32_t sys_map_version;

int
ds_mgmt_tgt_map_update_pre_forward(crt_rpc_t *rpc, void *arg)
{
	struct mgmt_tgt_map_update_in  *in = crt_req_get(rpc);
	struct server_entry	       *servers = in->tm_servers.ca_arrays;
	crt_group_t		       *group = crt_group_lookup(NULL);
	d_rank_t			self_rank;
	d_rank_list_t		       *ranks;
	int				i;
	int				rc;

	if (in->tm_map_version <= sys_map_version)
		return 0;

	rc = crt_group_rank(group, &self_rank);
	if (rc != 0) {
		D_DEBUG(DB_MGMT, "self rank unknown: %d\n", rc);
		return rc;
	}

	rc = crt_group_ranks_get(group, &ranks);
	if (rc != 0) {
		D_ERROR("failed to get existing ranks: %d\n", rc);
		return rc;
	}

	for (i = 0; i < in->tm_servers.ca_count; i++) {
		bool existing = d_rank_list_find(ranks, servers[i].se_rank,
						 NULL /* idx */);

		if (servers[i].se_flags & SERVER_IN) {
			if (existing)
				continue;
			D_DEBUG(DB_MGMT, "add rank=%u uri=%s nctxs=%u\n",
				servers[i].se_rank, servers[i].se_uri,
				servers[i].se_nctxs);
			rc = crt_group_primary_rank_add(rpc->cr_ctx, group,
							servers[i].se_rank,
							servers[i].se_uri);
			if (rc != 0)
				D_ERROR("failed to add rank=%u uri=%s: %d\n",
					servers[i].se_rank, servers[i].se_uri,
					rc);
		} else {
			if (!existing)
				continue;
			D_DEBUG(DB_MGMT, "remove rank=%u uri=%s nctxs=%u\n",
				servers[i].se_rank, servers[i].se_uri,
				servers[i].se_nctxs);
			rc = crt_group_rank_remove(group, servers[i].se_rank);
			if (rc != 0)
				D_ERROR("failed to remove rank=%u: %d\n",
					servers[i].se_rank, rc);
		}
		/*
		 * Commit suicide upon errors, so that others can detect and
		 * choose to proceed without me.
		 */
		D_ASSERTF(rc == 0, "update system map (version %u): %d\n",
			  in->tm_map_version, rc);
	}
	sys_map_version = in->tm_map_version;

	d_rank_list_free(ranks);
	return 0;
}

void
ds_mgmt_hdlr_tgt_map_update(crt_rpc_t *rpc)
{
	struct mgmt_tgt_map_update_in  *in = crt_req_get(rpc);
	struct mgmt_tgt_map_update_out *out = crt_reply_get(rpc);

	if (in->tm_map_version != sys_map_version)
		out->tm_rc = 1;

	crt_reply_send(rpc);
}

int
ds_mgmt_tgt_map_update_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				  void *priv)
{
	struct mgmt_tgt_map_update_out *out_source = crt_reply_get(source);
	struct mgmt_tgt_map_update_out *out_result = crt_reply_get(result);

	out_result->tm_rc += out_source->tm_rc;
	return 0;
}
