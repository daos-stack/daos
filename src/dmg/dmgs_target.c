/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/*
 * dmgs: Target Methods
 */

#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sysinfo.h>
#include <ftw.h>

#include "dmgs_internal.h"

#include <daos_srv/vos.h>
#include <daos_srv/daos_m_srv.h>
#include <daos_srv/daos_mgmt_srv.h>

/** TODO: Not use harcoded path to storage */
#define STORAGE_PATH	"/mnt/daos/"
/** directory for newly created pool, reclaimed on restart */
#define NEWBORNS	STORAGE_PATH "NEWBORNS/"
/** directory for destroyed pool */
#define ZOMBIES		STORAGE_PATH "ZOMBIES/"

static inline int
dir_fsync(const char *path)
{
	int	fd;
	int	rc;

	fd = open(path, O_RDONLY|O_DIRECTORY);
	if (fd < 0) {
		D_ERROR("failed to open %s for sync: %d\n", path, errno);
		return daos_errno2der();
	}

	rc = fsync(fd);
	if (rc < 0) {
		D_ERROR("failed to fync %s: %d\n", path, errno);
		rc = daos_errno2der();
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
		rc = daos_errno2der();

	return rc;
}

int
dmgs_tgt_init(void)
{
	int rc;

	/** create NEWBORNS directory if it does not exist already */
	rc = mkdir(NEWBORNS, 0700);
	if (rc < 0 && errno != EEXIST) {
		D_ERROR("failed to create NEWBORNS dir: %d\n", errno);
		return daos_errno2der();
	}

	/** create ZOMBIES directory if it does not exist already */
	rc = mkdir(ZOMBIES, 0700);
	if (rc < 0 && errno != EEXIST) {
		D_ERROR("failed to create ZOMBIES dir: %d\n", errno);
		return daos_errno2der();
	}

	/** remove leftover from previous runs */
	rc = subtree_destroy(NEWBORNS);
	if (rc)
		/** only log error, will try again next time */
		D_ERROR("failed to cleanup NEWBORNS dir: %d, will try again\n",
			rc);

	rc = subtree_destroy(ZOMBIES);
	if (rc)
		/** only log error, will try again next time */
		D_ERROR("failed to cleanup ZOMBIES dir: %d, will try again\n",
			rc);
	return 0;
}

static int
path_gen(const uuid_t pool_uuid, const char *dir, const char *fname, int *idx,
	 char **fpath)
{
	int	 size;
	int	 off;

	/** DAOS_UUID_STR_SIZE includes the trailing '\0', +1 for '/' */
	size = strlen(dir) + DAOS_UUID_STR_SIZE + 1;
	if (fname)
		size += strlen(fname);
	if (idx)
		size += snprintf(NULL, 0, "%d", *idx);

	D_ALLOC(*fpath, size);
	if (*fpath == NULL)
		return -DER_NOMEM;

	/**
	 * generate path to target file:
	 * /mnt/daos/[PENDING/]{UUID}/{fname}{idx}
	 */
	off = sprintf(*fpath, "%s", dir);
	uuid_unparse_lower(pool_uuid, *fpath + off);
	off += DAOS_UUID_STR_SIZE - 1;
	off += sprintf(*fpath + off, "/");
	if (fname)
		off += sprintf(*fpath + off, fname);
	if (idx)
		sprintf(*fpath + off, "%d", *idx);

	return 0;
}

/**
 * Generate path to a target file for pool \a pool_uuid with a filename set to
 * \a fname and suffixed by \a idx. \a idx can be NULL.
 */
int
dmgs_tgt_file(const uuid_t pool_uuid, const char *fname, int *idx, char **fpath)
{
	return path_gen(pool_uuid, STORAGE_PATH, fname, idx, fpath);
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
	 * Create one VOS file per thread
	 * 16MB minimum per file
	 */
	size = max(tgt_size / dss_nthreads, 1 << 24);
	/** tc_in->tc_tgt_dev is assumed to point at PMEM for now */

	for (i = 0; i < dss_nthreads; i++) {
		daos_handle_t	 vph;

		rc = path_gen(uuid, NEWBORNS, VOS_FILE, &i, &path);
		if (rc)
			break;

		D_DEBUG(DF_MGMT, DF_UUID": creating vos file %s\n",
			DP_UUID(uuid), path);

		fd = open(path, O_CREAT|O_RDWR, 0600);
		if (fd < 0) {
			D_ERROR(DF_UUID": failed to create vos file %s: %d\n",
				DP_UUID(uuid), path, rc);
			rc = daos_errno2der();
			break;
		}

		rc = posix_fallocate(fd, 0, size);
		if (rc) {
			D_ERROR(DF_UUID": failed to allocate vos file %s: %d\n",
				DP_UUID(uuid), path, rc);
			rc = daos_errno2der();
			break;
		}

		/* A zero size accommodates the existing file */
		rc = vos_pool_create(path, (unsigned char *)uuid, 0 /* size */,
				     &vph, NULL /* event */);
		if (rc) {
			D_ERROR(DF_UUID": failed to init vos pool %s: %d\n",
				DP_UUID(uuid), path, rc);
			break;
		}

		rc = vos_pool_close(vph, NULL /* event */);
		if (rc) {
			D_ERROR(DF_UUID": failed to close vos pool %s: %d\n",
				DP_UUID(uuid), path, rc);
			break;
		}

		rc = fsync(fd);
		(void)close(fd);
		fd = -1;
		if (rc) {
			D_ERROR(DF_UUID": failed to sync vos pool %s: %d\n",
				DP_UUID(uuid), path, rc);
			rc = daos_errno2der();
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
	rc = path_gen(pool_uuid, NEWBORNS, NULL, NULL, &newborn);
	if (rc)
		return rc;

	rc = mkdir(newborn, 0700);
	if (rc < 0 && errno != EEXIST) {
		D_ERROR("failed to created pool directory: %d\n", rc);
		D_GOTO(out, rc = daos_errno2der());
	}

	/** create VOS files */
	rc = tgt_vos_create(pool_uuid, size);
	if (rc)
		D_GOTO(out_tree, rc);

	/** initialize DAOS-M target and fetch uuid */
	rc = dsms_pool_create(pool_uuid, newborn, tgt_uuid);
	if (rc)
		D_GOTO(out_tree, rc);

	/** ready for prime time, move away from NEWBORNS dir */
	rc = rename(newborn, path);
	if (rc < 0) {
		D_ERROR("failed to rename pool directory: %d\n", rc);
		D_GOTO(out_tree, rc = daos_errno2der());
	}

	/** make sure the rename is persistent */
	rc = dir_fsync(path);

	D_GOTO(out, rc);

out_tree:
	/** cleanup will be re-executed on several occasions */
	(void)subtree_destroy(newborn);
	(void)rmdir(newborn);
out:
	free(newborn);
	return rc;
}

/**
 * RPC handler for target creation
 */
int
dmgs_hdlr_tgt_create(dtp_rpc_t *tc_req)
{
	struct dmg_tgt_create_in	*tc_in;
	struct dmg_tgt_create_out	*tc_out;
	char				*path = NULL;
	int				 rc = 0;

	/** incoming request buffer */
	tc_in = dtp_req_get(tc_req);
	/** reply buffer */
	tc_out = dtp_reply_get(tc_req);
	D_ASSERT(tc_in != NULL && tc_out != NULL);

	/** generate path to the target directory */
	rc = dmgs_tgt_file(tc_in->tc_pool_uuid, NULL, NULL, &path);
	if (rc)
		D_GOTO(out, rc);

	/** check whether the target already exists */
	rc = access(path, F_OK);
	if (rc >= 0) {
		/** target already exists, let's reuse it for idempotence */
		/** TODO: fetch tgt uuid from existing DSM pool */
		uuid_generate(tc_out->tc_tgt_uuid);

		/**
		 * flush again in case the previous one in tgt_create()
		 * failed
		 */
		rc = dir_fsync(path);
	} else if (errno == ENOENT) {
		/** target doesn't exist, create one */
		rc = tgt_create(tc_in->tc_pool_uuid, tc_out->tc_tgt_uuid,
				tc_in->tc_tgt_size, path);
	} else {
		rc = daos_errno2der();
	}

	free(path);
out:
	tc_out->tc_rc = rc;
	return dtp_reply_send(tc_req);
}

static int
tgt_destroy(uuid_t pool_uuid, char *path)
{
	char	*zombie = NULL;
	int	 rc;


	/** XXX: many synchronous/blocking operations below */

	/** move target directory to ZOMBIES */
	rc = path_gen(pool_uuid, ZOMBIES, NULL, NULL, &zombie);
	if (rc)
		return rc;

	rc = rename(path, zombie);
	if (rc < 0)
		D_GOTO(out, rc = daos_errno2der());

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
	free(zombie);
	return rc;
}

/**
 * RPC handler for target destroy
 */
int
dmgs_hdlr_tgt_destroy(dtp_rpc_t *td_req)
{
	struct dmg_tgt_destroy_in	*td_in;
	struct dmg_tgt_destroy_out	*td_out;
	char				*path;
	int				 rc;

	/** incoming request buffer */
	td_in = dtp_req_get(td_req);
	/** reply buffer */
	td_out = dtp_reply_get(td_req);
	D_ASSERT(td_in != NULL && td_out != NULL);

	/** generate path to the target directory */
	rc = dmgs_tgt_file(td_in->td_pool_uuid, NULL, NULL, &path);
	if (rc)
		D_GOTO(out, rc);

	/** check whether the target exists */
	rc = access(path, F_OK);
	if (rc >= 0) {
		/** target is still there, destroy it */
		rc = tgt_destroy(td_req->dr_input, path);
	} else if (errno == ENOENT) {
		char	*zombie;

		/**
		 * target is gone already, report success for idempotence
		 * that said, the previous flush in tgt_destroy() might have
		 * failed, so flush again.
		 */
		rc = path_gen(td_in->td_pool_uuid, ZOMBIES, NULL, NULL,
			      &zombie);
		if (rc)
			D_GOTO(out, rc);
		rc = dir_fsync(path);
		if (rc == -DER_NONEXIST)
			rc = 0;
		free(zombie);
	} else {
		rc = daos_errno2der();
	}

	free(path);
out:
	td_out->td_rc = rc;
	return dtp_reply_send(td_req);
}
