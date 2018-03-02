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
/*
 * Target Methods
 */
#define DDSUBSYS	DDFAC(mgmt)

#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sysinfo.h>
#include <ftw.h>
#include <dirent.h>

#include "srv_internal.h"

#include <daos_srv/vos.h>
#include <daos_srv/pool.h>
#include <daos_srv/daos_mgmt_srv.h>

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
		D__ERROR("failed to open %s for sync: %d\n", path, errno);
		return daos_errno2der(errno);
	}

	rc = fsync(fd);
	if (rc < 0) {
		D__ERROR("failed to fync %s: %d\n", path, errno);
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
		D__ERROR("failed to remove %s\n", path);
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
	rc = asprintf(&newborns_path, "%s/NEWBORNS", storage_path);
	if (rc < 0)
		D__GOTO(err, rc = -DER_NOMEM);
	rc = asprintf(&zombies_path, "%s/ZOMBIES", storage_path);
	if (rc < 0)
		D__GOTO(err_newborns, rc = -DER_NOMEM);

	stored_mode = umask(0);
	mode = S_IRWXU | S_IRWXG | S_IRWXO;
	/** create NEWBORNS directory if it does not exist already */
	rc = mkdir(newborns_path, mode);
	if (rc < 0 && errno != EEXIST) {
		D__ERROR("failed to create NEWBORNS dir: %d\n", errno);
		umask(stored_mode);
		D__GOTO(err_zombies, rc = daos_errno2der(errno));
	}

	/** create ZOMBIES directory if it does not exist already */
	rc = mkdir(zombies_path, mode);
	if (rc < 0 && errno != EEXIST) {
		D__ERROR("failed to create ZOMBIES dir: %d\n", errno);
		umask(stored_mode);
		D__GOTO(err_zombies, rc = daos_errno2der(errno));
	}
	umask(stored_mode);

	/** remove leftover from previous runs */
	rc = subtree_destroy(newborns_path);
	if (rc)
		/** only log error, will try again next time */
		D__ERROR("failed to cleanup NEWBORNS dir: %d, will try again\n",
			rc);

	rc = subtree_destroy(zombies_path);
	if (rc)
		/** only log error, will try again next time */
		D__ERROR("failed to cleanup ZOMBIES dir: %d, will try again\n",
			rc);
	return 0;

err_zombies:
	free(zombies_path);
err_newborns:
	free(newborns_path);
err:
	return rc;
}

void
ds_mgmt_tgt_fini(void)
{
	free(zombies_path);
	free(newborns_path);
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

	D__ALLOC(*fpath, size);
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
	return path_gen(pool_uuid, storage_path, fname, idx, fpath);
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
ds_mgmt_tgt_pool_iterate(int (*cb)(const uuid_t uuid, void *arg), void *arg)
{
	DIR    *storage;
	int	rc;
	int	rc_tmp;

	storage = opendir(storage_path);
	if (storage == NULL) {
		D_ERROR("failed to open %s: %d\n", storage_path, errno);
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
				D_ERROR("failed to read %s: %d\n", storage_path,
					errno);
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
		D_ERROR("failed to close %s: %d\n", storage_path, errno);
		rc_tmp = daos_errno2der(errno);
	}

	return rc == 0 ? rc_tmp : rc;
}

static int
tgt_vos_create(uuid_t uuid, daos_size_t tgt_size)
{
	daos_size_t	 size;
	int		 i;
	char		*path = NULL;
	int		 fd = -1;
	int		 rc = 0;

	/**
	 * Create one VOS file per execution stream
	 * 16MB minimum per file
	 */
	size = max(tgt_size / dss_nxstreams, 1 << 24);
	/** tc_in->tc_tgt_dev is assumed to point at PMEM for now */

	for (i = 0; i < dss_nxstreams; i++) {

		rc = path_gen(uuid, newborns_path, VOS_FILE, &i, &path);
		if (rc)
			break;

		D__DEBUG(DB_MGMT, DF_UUID": creating vos file %s\n",
			DP_UUID(uuid), path);

		fd = open(path, O_CREAT|O_RDWR, 0600);
		if (fd < 0) {
			D__ERROR(DF_UUID": failed to create vos file %s: %d\n",
				DP_UUID(uuid), path, rc);
			rc = daos_errno2der(errno);
			break;
		}

		rc = posix_fallocate(fd, 0, size);
		if (rc) {
			D__ERROR(DF_UUID": failed to allocate vos file %s with "
				"size: "DF_U64", rc: %d.\n",
				DP_UUID(uuid), path, size, rc);
			rc = daos_errno2der(rc);
			break;
		}

		/* A zero size accommodates the existing file */
		rc = vos_pool_create(path, (unsigned char *)uuid, 0 /* size */);
		if (rc) {
			D__ERROR(DF_UUID": failed to init vos pool %s: %d\n",
				DP_UUID(uuid), path, rc);
			break;
		}

		rc = fsync(fd);
		(void)close(fd);
		fd = -1;
		if (rc) {
			D__ERROR(DF_UUID": failed to sync vos pool %s: %d\n",
				DP_UUID(uuid), path, rc);
			rc = daos_errno2der(errno);
			break;
		}
	}
	if (path)
		free(path);
	if (fd >= 0)
		(void)close(fd);

	/** brute force cleanup to be done by the caller */
	return rc;
}

static int
tgt_create(uuid_t pool_uuid, uuid_t tgt_uuid, daos_size_t size, char *path)
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
		D__ERROR("failed to created pool directory: %d\n", rc);
		D__GOTO(out, rc = daos_errno2der(errno));
	}

	/** create VOS files */
	rc = tgt_vos_create(pool_uuid, size);
	if (rc)
		D__GOTO(out_tree, rc);

	/** initialize DAOS-M target and fetch uuid */
	rc = ds_pool_create(pool_uuid, newborn, tgt_uuid);
	if (rc) {
		D__ERROR("ds_pool_create failed, rc: %d.\n", rc);
		D__GOTO(out_tree, rc);
	}

	/** ready for prime time, move away from NEWBORNS dir */
	rc = rename(newborn, path);
	if (rc < 0) {
		D__ERROR("failed to rename pool directory: %d\n", rc);
		D__GOTO(out_tree, rc = daos_errno2der(errno));
	}

	/** make sure the rename is persistent */
	rc = dir_fsync(path);

	D__GOTO(out, rc);

out_tree:
	/** cleanup will be re-executed on several occasions */
	(void)subtree_destroy(newborn);
	(void)rmdir(newborn);
out:
	free(newborn);
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
	if (tc_uuids_nr == 0)
		return 0;

	new_uuids_nr = ret_uuids_nr + tc_uuids_nr;

	/* Append tc_uuids to ret_uuids */
	D__ALLOC(new_uuids, sizeof(*new_uuids) * new_uuids_nr);
	if (new_uuids == NULL)
		return -DER_NOMEM;

	D__ALLOC(new_ranks, sizeof(*new_ranks) * new_uuids_nr);
	if (new_ranks == NULL) {
		D__FREE(new_uuids, sizeof(*new_uuids) * new_uuids_nr);
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

	D__FREE(ret_uuids, sizeof(*ret_uuids) * ret_uuids_nr);
	D__FREE(ret_ranks, sizeof(*ret_uuids) * ret_uuids_nr);

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
	D__ASSERT(tc_in != NULL && tc_out != NULL);

	/** generate path to the target directory */
	rc = ds_mgmt_tgt_file(tc_in->tc_pool_uuid, NULL, NULL, &path);
	if (rc)
		D__GOTO(out, rc);

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
				tc_in->tc_tgt_size, path);
	} else {
		rc = daos_errno2der(errno);
	}

	if (rc)
		D__GOTO(free, rc);

	D__ALLOC_PTR(tmp_tgt_uuid);
	if (tmp_tgt_uuid == NULL)
		D__GOTO(free, rc = -DER_NOMEM);

	uuid_copy(*tmp_tgt_uuid, tgt_uuid);
	tc_out->tc_tgt_uuids.ca_arrays = tmp_tgt_uuid;
	tc_out->tc_tgt_uuids.ca_count = 1;

	D__ALLOC_PTR(rank);
	if (rank == NULL) {
		D__FREE_PTR(tmp_tgt_uuid);
		D__GOTO(free, rc = -DER_NOMEM);
	}

	rc = crt_group_rank(NULL, rank);
	D__ASSERT(rc == 0);
	tc_out->tc_ranks.ca_arrays = rank;
	tc_out->tc_ranks.ca_count = 1;

free:
	free(path);
out:
	tc_out->tc_rc = rc;
	crt_reply_send(tc_req);
}

static int
tgt_destroy(uuid_t pool_uuid, char *path)
{
	char	*zombie = NULL;
	int	 rc;


	/** XXX: many synchronous/blocking operations below */

	/** move target directory to ZOMBIES */
	rc = path_gen(pool_uuid, zombies_path, NULL, NULL, &zombie);
	if (rc)
		return rc;

	rc = rename(path, zombie);
	if (rc < 0)
		D__GOTO(out, rc = daos_errno2der(errno));

	/** make sure the rename is persistent */
	rc = dir_fsync(zombie);
	if (rc < 0)
		D__GOTO(out, rc);

	/**
	 * once successfully moved to the ZOMBIES directory, the target will
	 * take care of retrying on failure and thus always report success to
	 * the caller.
	 */
	(void)subtree_destroy(zombie);
	(void)rmdir(zombie);
out:
	free(zombie);
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
	D__ASSERT(td_in != NULL && td_out != NULL);

	/** generate path to the target directory */
	rc = ds_mgmt_tgt_file(td_in->td_pool_uuid, NULL, NULL, &path);
	if (rc)
		D__GOTO(out, rc);

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
			D__GOTO(out, rc);
		rc = dir_fsync(path);
		if (rc == -DER_NONEXIST)
			rc = 0;
		free(zombie);
	} else {
		rc = daos_errno2der(errno);
	}

	free(path);
out:
	td_out->td_rc = rc;
	crt_reply_send(td_req);
}
