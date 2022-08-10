/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <linux/xattr.h>

#include <gurt/debug.h>
#include <gurt/types.h>
#include <gurt/common.h>
#include <daos/common.h>
#include <daos_srv/vos_types.h>
#include <daos_srv/vos.h>

#include <daos_obj.h>
#include <daos_types.h>

#include <spdk/env.h>
#include <spdk/log.h>
#include <pmfs/pmfs.h>
#include <pmfs/vos_tasks.h>
#include "pmfs_internal.h"

typedef uint64_t pmfs_magic_t;
typedef uint16_t pmfs_sb_ver_t;
typedef uint16_t pmfs_layout_ver_t;

static int
insert_entry(struct pmfs *pmfs, daos_handle_t coh, daos_obj_id_t oid, const char *name, size_t len,
	     uint64_t flags, struct pmfs_entry *entry);

/* This function is used to save the maximum allocated OID from container */
static int
super_block_update_global_oid(struct pmfs *pmfs)
{
	d_sg_list_t	sgl;
	d_iov_t		sg_iov;
	daos_iod_t	iod;
	daos_key_t	dkey;
	int		rc = 0;

	/** set dkey as SB_DKEY */
	d_iov_set(&dkey, SB_DKEY, sizeof(SB_DKEY) - 1);

	/** set akey as the OID_VALUE */
	d_iov_set(&iod.iod_name, OID_VALUE, sizeof(OID_VALUE) - 1);
	iod.iod_nr	= 1;
	iod.iod_recxs	= NULL;
	iod.iod_type	= DAOS_IOD_SINGLE;
	iod.iod_size	= sizeof(daos_obj_id_t);

	/** set sgl for update */
	d_iov_set(&sg_iov, (void *)&pmfs->oid, sizeof(daos_obj_id_t));
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= &sg_iov;

	rc = vos_client_obj_update_sync(pmfs->coh, pmfs->super_oid, crt_hlc_get(), 0,
					DAOS_COND_DKEY_UPDATE | DAOS_COND_AKEY_UPDATE, &dkey,
					1, &iod, &sgl, pmfs->task_ring);
	if (rc) {
		D_ERROR("Failed to update PMFS superblock "DF_RC"\n", DP_RC(rc));
	}

	return rc;
}

/*
 * OID generation for the pmfs objects.
 */
static int
oid_gen(struct pmfs *pmfs, daos_obj_id_t *oid)
{
	int	rc;

	D_MUTEX_LOCK(&pmfs->lock);
	oid->lo = ++pmfs->oid.lo;
	if (oid->lo == UINT64_MAX) {
		D_ERROR("PMFS is full\n");
		return -EINVAL;
	}
	oid->hi = pmfs->oid.hi;
	rc = super_block_update_global_oid(pmfs);
	D_MUTEX_UNLOCK(&pmfs->lock);

	return rc;
}

static void
set_daos_iod(bool create, daos_iod_t *iod, char *buf, size_t size)
{
	d_iov_set(&iod->iod_name, buf, strlen(buf));
	iod->iod_nr	= 1;
	iod->iod_size	= DAOS_REC_ANY;
	iod->iod_recxs	= NULL;
	iod->iod_type	= DAOS_IOD_SINGLE;

	if (create) {
		iod->iod_size = size;
	}
}

static void
set_super_block_params(bool for_update, daos_iod_t *iods, daos_key_t *dkey)
{
	int i = 0;

	d_iov_set(dkey, SB_DKEY, sizeof(SB_DKEY) - 1);

	set_daos_iod(for_update, &iods[i++], MAGIC_NAME, sizeof(pmfs_magic_t));
	set_daos_iod(for_update, &iods[i++],
		     SB_VERSION_NAME, sizeof(pmfs_sb_ver_t));
	set_daos_iod(for_update, &iods[i++],
		     LAYOUT_NAME, sizeof(pmfs_layout_ver_t));
	set_daos_iod(for_update, &iods[i++], CS_NAME, sizeof(daos_size_t));
	set_daos_iod(for_update, &iods[i++], MODE_NAME, sizeof(uint32_t));
	set_daos_iod(for_update, &iods[i++], OID_VALUE, sizeof(daos_obj_id_t));
}

static int
open_sb(daos_handle_t coh, bool create, daos_obj_id_t super_oid,
	struct pmfs_attr *attr, struct pmfs *pmfs)
{
	d_sg_list_t		sgls[SB_AKEYS];
	d_iov_t			sg_iovs[SB_AKEYS];
	daos_iod_t		iods[SB_AKEYS];
	daos_key_t		dkey;
	pmfs_magic_t		magic;
	pmfs_sb_ver_t		sb_ver;
	pmfs_layout_ver_t	layout_ver;
	daos_obj_id_t		pmfs_oid;
	daos_size_t		chunk_size = 0;
	uint32_t		mode;
	int			i, rc;

	d_iov_set(&sg_iovs[0], &magic, sizeof(pmfs_magic_t));
	d_iov_set(&sg_iovs[1], &sb_ver, sizeof(pmfs_sb_ver_t));
	d_iov_set(&sg_iovs[2], &layout_ver, sizeof(pmfs_layout_ver_t));
	d_iov_set(&sg_iovs[3], &chunk_size, sizeof(daos_size_t));
	d_iov_set(&sg_iovs[4], &mode, sizeof(uint32_t));
	d_iov_set(&sg_iovs[5], &pmfs_oid, sizeof(daos_obj_id_t));

	for (i = 0; i < SB_AKEYS; i++) {
		sgls[i].sg_nr		= 1;
		sgls[i].sg_nr_out	= 0;
		sgls[i].sg_iovs		= &sg_iovs[i];
	}

	set_super_block_params(create, iods, &dkey);

	/** create the SB and exit */
	if (create) {
		magic = PMFS_SB_MAGIC;
		sb_ver = PMFS_SB_VERSION;
		layout_ver = PMFS_LAYOUT_VERSION;
		pmfs_oid.hi = SB_HI;
		pmfs_oid.lo = RESERVED_LO;

		if (attr->da_chunk_size != 0) {
			chunk_size = attr->da_chunk_size;
		} else {
			chunk_size = PMFS_DEFAULT_CHUNK_SIZE;
		}

		mode = attr->da_mode;

		rc = vos_client_obj_update_sync(coh, super_oid, crt_hlc_get(), 0,
						DAOS_COND_DKEY_INSERT, &dkey,
						SB_AKEYS, iods, sgls,
						pmfs->task_ring);
		if (rc) {
			D_ERROR("Failed to create PMFS superblock "DF_RC"\n", DP_RC(rc));
			D_GOTO(err, rc = daos_der2errno(rc));
		}

		D_INFO("create a new sb\r\n");
		return 0;
	}

	/* otherwise fetch the values and verify SB */
	rc = vos_client_obj_fetch_sync(coh, super_oid, crt_hlc_get(), 0,
				       &dkey, SB_AKEYS, iods, sgls,
				       pmfs->task_ring);

	if (rc) {
		D_ERROR("Failed to fetch SB info, "DF_RC"\n", DP_RC(rc));
		D_GOTO(err, rc = daos_der2errno(rc));
	}

	/** check if SB info exists */
	if (iods[0].iod_size == 0) {
		D_ERROR("SB does not exist.\n");
		D_GOTO(err, rc = ENOENT);
	}

	if (magic != PMFS_SB_MAGIC) {
		D_ERROR("SB MAGIC verification failed.\n");
		D_GOTO(err, rc = EINVAL);
	}

	/** check version compatibility */
	if (iods[1].iod_size != sizeof(sb_ver) || sb_ver > PMFS_SB_VERSION) {
		D_ERROR("Incompatible SB version.\n");
		D_GOTO(err, rc = EINVAL);
	}

	if (iods[2].iod_size != sizeof(layout_ver) ||
	    layout_ver != PMFS_LAYOUT_VERSION) {
		D_ERROR("Incompatible PMFS Layout version.\n");
		D_GOTO(err, rc = EINVAL);
	}

	attr->da_chunk_size = (chunk_size) ? chunk_size :
			      PMFS_DEFAULT_CHUNK_SIZE;

	/** PMFS_RELAXED by default */
	attr->da_mode = mode;
	if (pmfs) {
		pmfs->oid = pmfs_oid;
	}

	return 0;
err:
	return rc;
}

int
pmfs_mkfs(daos_handle_t poh, uuid_t cuuid)
{
	daos_handle_t		coh = {};
	struct pmfs_entry	entry = {};
	struct pmfs_attr		dattr;
	daos_obj_id_t		cr_oids[2] = { {} };
	int			rc;
	struct pmfs			pmfs = {};

	dattr.da_mode = PMFS_RELAXED;
	dattr.da_chunk_size = PMFS_DEFAULT_CHUNK_SIZE;

	cr_oids[0].lo = RESERVED_LO;
	cr_oids[0].hi = SB_HI;

	cr_oids[1].lo = RESERVED_LO;
	cr_oids[1].hi = ROOT_HI;

	/* Create Task ring */
	pmfs.task_ring = vos_target_create_tasks("PMFS_MKFS", PMFS_MAX_TASKS);
	if (pmfs.task_ring == NULL) {
		return -EIO;
	}

	rc = vos_cont_create(poh, cuuid);
	if (rc) {
		D_ERROR("vos_cont_create() failed "DF_RC"\n", DP_RC(rc));
		vos_target_free_tasks(pmfs.task_ring);
		return rc;
	}

	rc = vos_cont_open(poh, cuuid, &coh);
	if (rc) {
		D_ERROR("daos_cont_open() failed "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_destroy, rc = daos_der2errno(rc));
	}

	/** Create SB */
	rc = open_sb(coh, true, cr_oids[0], &dattr, &pmfs);
	if (rc) {
		D_ERROR("open_sb() failed "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_close, rc);
	}

	/** Add root object */
	entry.oid = cr_oids[1];
	entry.mode = S_IFDIR | 0755;
	entry.atime = entry.mtime = entry.ctime = time(NULL);
	entry.chunk_size = dattr.da_chunk_size;

	rc = insert_entry(&pmfs, coh, cr_oids[0], "/", 1,
			  DAOS_COND_DKEY_INSERT, &entry);
	if (rc && rc != EEXIST) {
		D_ERROR("Failed to insert root entry, %d\n", rc);
		D_GOTO(err_close, rc);
	}

	rc = vos_cont_close(coh);
	if (rc) {
		D_ERROR("vos_cont_close() failed "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_destroy, rc = daos_der2errno(rc));
	}

	vos_target_free_tasks(pmfs.task_ring);
	return 0;

err_close:
	vos_cont_close(coh);

err_destroy:
	vos_cont_destroy(poh, cuuid);
	vos_target_free_tasks(pmfs.task_ring);
	return rc;

}

static inline int
get_daos_obj_mode(int flags)
{
	if ((flags & O_ACCMODE) == O_RDONLY) {
		return DAOS_OO_RO;
	} else if ((flags & O_ACCMODE) == O_RDWR ||
		   (flags & O_ACCMODE) == O_WRONLY) {
		return DAOS_OO_RW;
	} else {
		return -1;
	}
}

static inline void
oid_cp(daos_obj_id_t *dst, daos_obj_id_t src)
{
	dst->hi = src.hi;
	dst->lo = src.lo;
}

static int
fetch_entry(struct pmfs *pmfs, daos_handle_t coh, daos_obj_id_t parent_oid, const char *name,
	    size_t len, bool fetch_sym, bool *exists, struct pmfs_entry *entry)
{
	d_sg_list_t	sgl;
	d_iov_t		sg_iovs[INODE_AKEYS];
	daos_iod_t	iod;
	daos_recx_t	recx;
	daos_key_t	dkey;
	unsigned int	i;
	int		rc;

	D_ASSERT(name);

	/** TODO - not supported yet */
	if (strcmp(name, ".") == 0) {
		D_ASSERT(0);
	}

	d_iov_set(&dkey, (void *)name, len);
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_nr	= 1;
	recx.rx_idx	= 0;
	recx.rx_nr	= SYML_IDX;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;
	iod.iod_size	= 1;
	i = 0;

	d_iov_set(&sg_iovs[i++], &entry->mode, sizeof(mode_t));
	d_iov_set(&sg_iovs[i++], &entry->oid, sizeof(daos_obj_id_t));
	d_iov_set(&sg_iovs[i++], &entry->atime, sizeof(time_t));
	d_iov_set(&sg_iovs[i++], &entry->mtime, sizeof(time_t));
	d_iov_set(&sg_iovs[i++], &entry->ctime, sizeof(time_t));
	d_iov_set(&sg_iovs[i++], &entry->chunk_size, sizeof(daos_size_t));
	d_iov_set(&sg_iovs[i++], &entry->file_size, sizeof(daos_size_t));

	sgl.sg_nr	= i;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= sg_iovs;

	rc = vos_client_obj_fetch_sync(coh, parent_oid, crt_hlc_get(), 0,
				       &dkey, 1, &iod, &sgl,
				       pmfs->task_ring);
	if (rc) {
		D_ERROR("Failed to fetch entry %s "DF_RC"\n", name,
			DP_RC(rc));
		D_GOTO(out, rc = daos_der2errno(rc));
	}

	if (fetch_sym && S_ISLNK(entry->mode)) {
		char *value;

		D_ALLOC(value, PMFS_MAX_PATH);
		if (value == NULL) {
			D_GOTO(out, rc = ENOMEM);
		}

		recx.rx_idx = SYML_IDX;
		recx.rx_nr = PMFS_MAX_PATH;

		d_iov_set(&sg_iovs[0], value, PMFS_MAX_PATH);
		sgl.sg_nr	= 1;
		sgl.sg_nr_out	= 0;
		sgl.sg_iovs	= sg_iovs;

		rc = vos_client_obj_fetch_sync(coh, parent_oid, crt_hlc_get(), 0,
					       &dkey, 1, &iod, &sgl,
					       pmfs->task_ring);
		if (rc) {
			D_ERROR("Failed to fetch entry %s "DF_RC"\n", name,
				DP_RC(rc));
			D_FREE(value);
			D_GOTO(out, rc = daos_der2errno(rc));
		}

		entry->value_len = sg_iovs[0].iov_len;

		if (entry->value_len != 0) {
			/* Return value here, and allow the caller to truncate
			 * the buffer if they want to
			 */
			entry->value = value;
		} else {
			D_ERROR("Failed to load value for symlink\n");
			D_FREE(value);
			D_GOTO(out, rc = EIO);
		}
	}

	if (sgl.sg_nr_out == 0) {
		*exists = false;
	} else {
		*exists = true;
	}

out:
	return rc;
}

static int
remove_entry(struct pmfs *pmfs, daos_handle_t coh, daos_obj_id_t parent_oid,
	     const char *name, size_t len, struct pmfs_entry entry)
{
	daos_key_t	dkey;
	int		rc;

	if (S_ISLNK(entry.mode))
		goto punch_entry;

	rc = vos_client_obj_punch_sync(coh, entry.oid, crt_hlc_get(),
				       0, 0, NULL, 0, NULL,
				       pmfs->task_ring);
	if (rc) {
		return daos_der2errno(rc);
	}

punch_entry:
	d_iov_set(&dkey, (void *)name, len);
	rc = vos_client_obj_punch_sync(coh, parent_oid, crt_hlc_get(),
				       0, 0, &dkey, 0, NULL,
				       pmfs->task_ring);
	return daos_der2errno(rc);
}

static int
insert_entry(struct pmfs *pmfs, daos_handle_t coh, daos_obj_id_t oid, const char *name,
	     size_t len, uint64_t flags, struct pmfs_entry *entry)
{
	d_sg_list_t	sgl;
	d_iov_t		sg_iovs[INODE_AKEYS];
	daos_iod_t	iod = {};
	daos_recx_t	recx = {};
	daos_key_t	dkey;
	unsigned int	i;
	int		rc;

	d_iov_set(&dkey, (void *)name, len);
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_nr	= 1;
	recx.rx_idx	= 0;
	recx.rx_nr	= SYML_IDX;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;
	iod.iod_size	= 1;
	i = 0;

	d_iov_set(&sg_iovs[i++], &entry->mode, sizeof(mode_t));
	d_iov_set(&sg_iovs[i++], &entry->oid, sizeof(daos_obj_id_t));
	d_iov_set(&sg_iovs[i++], &entry->atime, sizeof(time_t));
	d_iov_set(&sg_iovs[i++], &entry->mtime, sizeof(time_t));
	d_iov_set(&sg_iovs[i++], &entry->ctime, sizeof(time_t));
	d_iov_set(&sg_iovs[i++], &entry->chunk_size, sizeof(daos_size_t));
	d_iov_set(&sg_iovs[i++], &entry->file_size, sizeof(daos_size_t));

	/** Add symlink value if Symlink */
	if (S_ISLNK(entry->mode)) {
		d_iov_set(&sg_iovs[i++], entry->value, entry->value_len);
		recx.rx_nr += entry->value_len;
	}

	sgl.sg_nr	= i;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= sg_iovs;

	rc = vos_client_obj_update_sync(coh, oid, crt_hlc_get(), 0, flags,
					&dkey, 1, &iod, &sgl,
					pmfs->task_ring);
	if (rc) {
		D_ERROR("Failed to insert entry %s, "DF_RC"\n",
			name, DP_RC(rc));
		return daos_der2errno(rc);
	}

	return 0;
}

/*
 * create a dir object ID.
 */
static inline int
create_dir(struct pmfs *pmfs, struct pmfs_obj *dir)
{
	int			rc;

	/** Allocate an OID for the dir - local operation */
	rc = oid_gen(pmfs, &dir->oid);
	if (rc != 0) {
		return rc;
	}

	return 0;
}

static int
open_dir(struct pmfs *pmfs, struct pmfs_obj *parent, int flags,
	 struct pmfs_entry *entry, size_t dir_len, struct pmfs_obj *dir)
{
	bool			exists;
	int			daos_mode;
	daos_obj_id_t		oid;
	int			rc;

	oid = parent ? parent->oid : pmfs->super_oid;

	if (flags & O_CREAT) {
		D_ASSERT(parent);

		/** this generates the OID */
		rc = create_dir(pmfs, dir);
		if (rc) {
			return rc;
		}

		entry->oid = dir->oid;
		entry->mode = dir->mode;
		entry->atime = entry->mtime = entry->ctime = time(NULL);
		entry->chunk_size = parent->chunk_size;

		/** since it's a single conditional op, we don't need a DTX */
		rc = insert_entry(pmfs, pmfs->coh, oid, dir->name, dir_len,
				  DAOS_COND_DKEY_INSERT, entry);
		if (rc != 0) {
			D_ERROR("Inserting dir entry %s failed (%d)\n",
				dir->name, rc);
		}

		dir->chunk_size = entry->chunk_size;
		return rc;
	}

	daos_mode = get_daos_obj_mode(flags);
	if (daos_mode == -1) {
		return EINVAL;
	}

	/* Check if parent has the dirname entry */
	rc = fetch_entry(pmfs, pmfs->coh, oid, dir->name, dir_len, false,
			 &exists, entry);
	if (rc) {
		return rc;
	}

	if (!exists) {
		return ENOENT;
	}

	/* Check that the opened object is the type that's expected, this could
	 * happen for example if pmfs_open() is called with S_IFDIR but without
	 * O_CREATE and a entry of a different type exists already.
	 */
	if (!S_ISDIR(entry->mode)) {
		return ENOTDIR;
	}

	dir->mode = entry->mode;
	oid_cp(&dir->oid, entry->oid);
	dir->chunk_size = entry->chunk_size;

	return 0;

}

static int
get_num_entries(struct pmfs *pmfs, daos_obj_id_t oid, uint32_t *nr, size_t *len)
{
	int rc;

	rc = vos_client_obj_get_num_dkeys_sync(pmfs->coh, oid, nr, len, pmfs->task_ring);
	if (rc) {
		D_ERROR("get_num_entries failed (%d)\n", rc);
	}

	return rc;
}

static inline int
check_name(const char *name, size_t *_len)
{
	size_t len;

	*_len = 0;

	if (name == NULL || strchr(name, '/')) {
		return EINVAL;
	}

	len = strnlen(name, PMFS_MAX_NAME + 1);
	if (len > PMFS_MAX_NAME) {
		return EINVAL;
	}

	*_len = len;
	return 0;
}

int
pmfs_mkdir(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name, mode_t mode)
{
	struct pmfs_obj		new_dir;
	struct pmfs_entry	entry = {};
	size_t			len;
	int			rc;

	if (pmfs == NULL || !pmfs->mounted) {
		return EINVAL;
	}
	if (pmfs->amode != O_RDWR) {
		return EPERM;
	}
	if (parent == NULL) {
		parent = &pmfs->root;
	} else if (!S_ISDIR(parent->mode)) {
		return ENOTDIR;
	}

	rc = check_name(name, &len);
	if (rc) {
		return rc;
	}

	strncpy(new_dir.name, name, len + 1);

	rc = create_dir(pmfs, &new_dir);
	if (rc) {
		return rc;
	}

	entry.oid = new_dir.oid;
	entry.mode = S_IFDIR | mode;
	entry.atime = entry.mtime = entry.ctime = time(NULL);
	entry.chunk_size = parent->chunk_size;

	rc = insert_entry(pmfs, pmfs->coh, parent->oid, new_dir.name, len,
			  DAOS_COND_DKEY_INSERT, &entry);
	if (rc != 0) {
		D_ERROR("Inserting dir entry %s failed (%d)\n",
			new_dir.name, rc);
	}

	return rc;
}

int
pmfs_obj_set_file_size(struct pmfs *pmfs, struct pmfs_obj *obj, daos_size_t fsize)
{
	d_sg_list_t		sgl;
	d_iov_t			sg_iov;
	daos_iod_t		iod;
	daos_recx_t		recx;
	daos_key_t		dkey;
	int			rc;

	if (obj == NULL) {
		return EINVAL;
	}
	if (S_ISDIR(obj->mode)) {
		return ENOTSUP;
	}

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));

	/** set akey as the inode name */
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME));
	iod.iod_nr	= 1;
	iod.iod_size	= 1;
	recx.rx_idx	= FSIZE_IDX;
	recx.rx_nr      = sizeof(daos_size_t);
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** set sgl for update */
	d_iov_set(&sg_iov, &fsize, sizeof(daos_size_t));
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= &sg_iov;

	rc = vos_client_obj_update_sync(pmfs->coh, obj->parent_oid, crt_hlc_get(), 0,
					0, &dkey,
					1, &iod, &sgl, pmfs->task_ring);
	if (rc) {
		D_ERROR("Failed to update file size ("DF_RC")\n", DP_RC(rc));
		goto exit;
	}

	obj->file_size = fsize;
exit:
	return rc;
}

int
pmfs_obj_get_file_size(struct pmfs *pmfs, struct pmfs_obj *obj, daos_size_t *fsize)
{
	d_sg_list_t		sgl;
	d_iov_t			sg_iov;
	daos_iod_t		iod;
	daos_recx_t		recx;
	daos_key_t		dkey;
	int			rc;

	if (obj == NULL) {
		return EINVAL;
	}
	if (S_ISDIR(obj->mode)) {
		return ENOTSUP;
	}

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));

	/** set akey as the inode name */
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_nr	= 1;
	iod.iod_size	= 1;
	recx.rx_idx	= FSIZE_IDX;
	recx.rx_nr      = sizeof(daos_size_t);
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** set sgl for update */
	d_iov_set(&sg_iov, fsize, sizeof(daos_size_t));
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= &sg_iov;

	rc = vos_client_obj_fetch_sync(pmfs->coh, obj->parent_oid, crt_hlc_get(), 0,
				       &dkey, 1, &iod, &sgl, pmfs->task_ring);
	if (rc) {
		D_ERROR("Failed to update file size ("DF_RC")\n", DP_RC(rc));
	}

	return rc;
}

static int
open_file(struct pmfs *pmfs, struct pmfs_obj *parent, int flags, daos_size_t chunk_size,
	  struct pmfs_entry *entry, daos_size_t *size,
	  size_t len, struct pmfs_obj *file)
{
	bool			exists;
	int			daos_mode;
	bool			ocreat = flags & O_CREAT;
	int			rc;

	if (ocreat) {
		rc = fetch_entry(pmfs, pmfs->coh, parent->oid, file->name, len, false,
				 &exists, entry);
		if (rc) {
			D_GOTO(out, rc);
		}

		/** Just open the file */
		if (exists) {
			goto fopen;
		}

		/** same logic for chunk size */
		if (chunk_size == 0) {
			if (parent->chunk_size == 0) {
				chunk_size = pmfs->attr.da_chunk_size;
			} else {
				chunk_size = parent->chunk_size;
			}
		} else {
			if (chunk_size % 512) {
				D_ERROR("Invalid chunk size\n");
				return -EINVAL;
			}
		}

		/** Get new OID for the file */
		rc = oid_gen(pmfs, &file->oid);
		if (rc != 0) {
			D_GOTO(out, rc);
		}
		oid_cp(&entry->oid, file->oid);

		/** Create and insert entry in parent dir object. */
		entry->mode = file->mode;
		entry->atime = entry->mtime = entry->ctime = time(NULL);
		entry->chunk_size = chunk_size;

		rc = insert_entry(pmfs, pmfs->coh, parent->oid, file->name, len,
				  DAOS_COND_DKEY_INSERT, entry);
		if (rc) {
			D_DEBUG(DB_TRACE, "Insert file entry %s failed (%d)\n",
				file->name, rc);
		}
		file->chunk_size = chunk_size;
		return rc;
	}

	/* Check if parent has the filename entry */
	rc = fetch_entry(pmfs, pmfs->coh, parent->oid, file->name, len, false,
			 &exists, entry);
	if (rc) {
		D_ERROR("fetch_entry %s failed %d.\n", file->name, rc);
		D_GOTO(out, rc);
	}

	if (!exists) {
		D_GOTO(out, rc = ENOENT);
	}

fopen:
	if (!S_ISREG(entry->mode)) {
		D_FREE(entry->value);
		D_GOTO(out, rc = EINVAL);
	}

	daos_mode = get_daos_obj_mode(flags);
	if (daos_mode == -1) {
		D_GOTO(out, rc = EINVAL);
	}

	D_ASSERT(entry->chunk_size);

	/** Open the byte array */
	file->mode = entry->mode;
	file->chunk_size = chunk_size;

	if (flags & O_TRUNC) {
		rc = pmfs_obj_set_file_size(pmfs, file, 0);
		if (rc) {
			D_ERROR("Failed to truncate file (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
		if (size) {
			*size = 0;
		}
	} else {
		if (size) {
			*size = entry->file_size;
		}
	}

	oid_cp(&file->oid, entry->oid);
	return 0;

out:
	return rc;
}

static int
open_symlink(struct pmfs *pmfs, struct pmfs_obj *parent, int flags,
	     const char *value, struct pmfs_entry *entry, size_t len,
	     struct pmfs_obj *sym)
{
	size_t			value_len;
	int			rc;

	if (flags & O_CREAT) {
		if (value == NULL) {
			return EINVAL;
		}

		value_len = strnlen(value, PMFS_MAX_PATH);

		if (value_len > PMFS_MAX_PATH - 1) {
			return EINVAL;
		}

		/*
		 * note that we don't use this object to store anything since
		 * the value is stored in the inode. This just an identifier for
		 * the symlink.
		 */
		rc = oid_gen(pmfs, &sym->oid);
		if (rc != 0) {
			return rc;
		}

		oid_cp(&entry->oid, sym->oid);
		entry->mode = sym->mode | 0777;
		entry->atime = entry->mtime = entry->ctime = time(NULL);
		D_STRNDUP(sym->value, value, value_len + 1);
		if (sym->value == NULL) {
			return ENOMEM;
		}

		entry->value = sym->value;
		entry->value_len = value_len;
		rc = insert_entry(pmfs, pmfs->coh, parent->oid, sym->name, len,
				  DAOS_COND_DKEY_INSERT, entry);
		if (rc) {
			D_FREE(sym->value);
			D_ERROR("Inserting entry %s failed (rc = %d)\n",
				sym->name, rc);
		}
		return rc;
	}

	return ENOTSUP;
}

int
pmfs_open_stat(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name,
	       mode_t mode, int flags, daos_size_t chunk_size, const char *value,
	       struct pmfs_obj **_obj, struct stat *stbuf)
{
	struct pmfs_entry	entry = {};
	struct pmfs_obj		*obj;
	size_t			len;
	daos_size_t		file_size = 0;
	int			rc;

	if (pmfs == NULL || !pmfs->mounted) {
		return EINVAL;
	}
	if ((pmfs->amode != O_RDWR) && (flags & O_CREAT)) {
		return EPERM;
	}
	if (_obj == NULL) {
		return EINVAL;
	}
	if (S_ISLNK(mode) && value == NULL) {
		return EINVAL;
	}
	if (parent == NULL) {
		parent = &pmfs->root;
	} else if (!S_ISDIR(parent->mode)) {
		return ENOTDIR;
	}

	if (stbuf && !(flags & O_CREAT)) {
		return ENOTSUP;
	}

	rc = check_name(name, &len);
	if (rc) {
		return rc;
	}

	D_ALLOC_PTR(obj);
	if (obj == NULL) {
		return ENOMEM;
	}

	strncpy(obj->name, name, len + 1);
	obj->mode = mode;
	obj->flags = flags;
	obj->chunk_size = chunk_size;
	oid_cp(&obj->parent_oid, parent->oid);

	switch (mode & S_IFMT) {
	case S_IFREG:
		rc = open_file(pmfs, parent, flags, chunk_size, &entry,
			       stbuf ? &file_size : NULL, len, obj);
		if (rc) {
			D_DEBUG(DB_TRACE, "Failed to open file (%d)\n", rc);
			D_GOTO(out, rc);
		}
		break;
	case S_IFDIR:
		rc = open_dir(pmfs, parent, flags, &entry, len, obj);
		if (rc) {
			D_DEBUG(DB_TRACE, "Failed to open dir (%d)\n", rc);
			D_GOTO(out, rc);
		}
		break;
	case S_IFLNK:
		rc = open_symlink(pmfs, parent, flags, value, &entry, len,
				  obj);
		if (rc) {
			D_DEBUG(DB_TRACE, "Failed to open symlink (%d)\n", rc);
			D_GOTO(out, rc);
		}
		break;
	default:
		D_ERROR("Invalid entry type (not a dir, file, symlink).\n");
		D_GOTO(out, rc = EINVAL);
	}

out:
	if (rc == 0) {
		if (stbuf) {
			stbuf->st_size = file_size;
			stbuf->st_nlink = 1;
			stbuf->st_mode = entry.mode;
			stbuf->st_uid = pmfs->uid;
			stbuf->st_gid = pmfs->gid;
			stbuf->st_atim.tv_sec = entry.atime;
			stbuf->st_mtim.tv_sec = entry.mtime;
			stbuf->st_ctim.tv_sec = entry.ctime;
		}
		*_obj = obj;
	} else {
		D_FREE(obj);
	}

	return rc;
}

static int
remove_dir_contents(struct pmfs *pmfs, struct pmfs_entry entry, uint32_t nr_children)
{
	daos_key_desc_t	*kds;
	char		*enum_buf;
	char		*ptr;
	int		rc;
	uint32_t	i;
	size_t		len_children;

	D_ASSERT(S_ISDIR(entry.mode));

	D_ALLOC_ARRAY(kds, nr_children);
	if (kds == NULL)
		return ENOMEM;

	D_ALLOC_ARRAY(enum_buf, nr_children * PMFS_MAX_NAME);
	if (enum_buf == NULL) {
		D_FREE(kds);
		return ENOMEM;
	}

	rc = vos_client_obj_list_dkeys_sync(pmfs->coh, entry.oid, &nr_children,
					    &len_children, kds, enum_buf, pmfs->task_ring);
	if (rc) {
		D_ERROR("vos_client_obj_list_dkeys_sync failed (%d)\n", rc);
		D_GOTO(out, rc);
	}

	for (ptr = enum_buf, i = 0; i < nr_children; i++) {
		struct pmfs_entry child_entry = {0};
		bool exists;

		/* TODO: ???? why moving the ptr at the beginning */
		ptr += kds[i].kd_key_len;
		rc = fetch_entry(pmfs, pmfs->coh, entry.oid, ptr, kds[i].kd_key_len, false,
				 &exists, &child_entry);
		if (rc)
			D_GOTO(out, rc);

		if (!exists)
			continue;

		if (S_ISDIR(child_entry.mode)) {
			uint32_t nr;
			size_t len;

			rc = get_num_entries(pmfs, child_entry.oid, &nr, &len);
			if (rc) {
				D_GOTO(out, rc);
			}

			rc = remove_dir_contents(pmfs, child_entry, nr);
			if (rc)
				D_GOTO(out, rc);
		}

		rc = remove_entry(pmfs, pmfs->coh, entry.oid, ptr,
				  kds[i].kd_key_len, child_entry);
		if (rc)
			D_GOTO(out, rc);
	}

out:
	D_FREE(kds);
	D_FREE(enum_buf);
	return rc;
}

int
pmfs_remove(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name, bool force,
	    daos_obj_id_t *oid)
{
	struct pmfs_entry	entry = {0};
	bool			exists;
	size_t			len;
	int			rc;

	if (pmfs == NULL || !pmfs->mounted)
		return EINVAL;
	if (pmfs->amode != O_RDWR)
		return EPERM;
	if (parent == NULL)
		parent = &pmfs->root;
	else if (!S_ISDIR(parent->mode))
		return ENOTDIR;

	rc = check_name(name, &len);
	if (rc)
		return rc;

	/** Even with cond punch, need to fetch the entry to check the type */
	rc = fetch_entry(pmfs, pmfs->coh, parent->oid, name, len, false, &exists, &entry);
	if (rc)
		D_GOTO(out, rc);

	if (!exists)
		D_GOTO(out, rc = ENOENT);

	if (S_ISDIR(entry.mode)) {
		uint32_t nr = 0;
		size_t   children_len = 0;

		rc = get_num_entries(pmfs, entry.oid, &nr, &children_len);
		if (rc) {
			D_GOTO(out, rc);
		}

		if (!force && nr != 0)
			D_GOTO(out, rc = ENOTEMPTY);

		if (force && nr != 0) {
			rc = remove_dir_contents(pmfs, entry, nr);
			if (rc)
				D_GOTO(out, rc);
		}

	}

	rc = remove_entry(pmfs, pmfs->coh, parent->oid, name, len, entry);
	if (rc)
		D_GOTO(out, rc);

	if (oid)
		oid_cp(oid, entry.oid);

out:
	return rc;
}

int
pmfs_listdir(struct pmfs *pmfs, struct pmfs_obj *obj, uint32_t *nr)
{
	uint32_t	nr_children;
	size_t		total_len;
	int		rc = 0;

	if (pmfs == NULL || !pmfs->mounted)
		return EINVAL;
	if (obj == NULL) {
		obj = &pmfs->root;
	} else if (!S_ISDIR(obj->mode)) {
		return ENOTDIR;
	}

	rc = get_num_entries(pmfs, obj->oid, &nr_children, &total_len);
	if (rc) {
		D_ERROR("get_num_entries, "DF_RC"\n", DP_RC(rc));
		return rc;
	}
	*nr = nr_children;

	return rc;
}

static int
lookup_rel_path(struct pmfs *pmfs, struct pmfs_obj *root, const char *path, int flags,
		struct pmfs_obj **_obj, mode_t *mode, struct stat *stbuf,
		size_t depth)
{
	struct pmfs_obj		parent;
	struct pmfs_obj		*obj = NULL;
	char			*token;
	char			*rem, *sptr = NULL; /* bogus compiler warning */
	bool			exists;
	int			daos_mode;
	struct pmfs_entry	entry = {0};
	size_t			len;
	int			rc = 0;
	bool			parent_fully_valid;

	/* Arbitrarily stop to avoid infinite recursion */
	if (depth >= PMFS_MAX_RECURSION)
		return ELOOP;

	/* Only paths from root can be absolute */
	if (path[0] == '/' && daos_oid_cmp(root->oid, pmfs->root.oid) != 0)
		return EINVAL;

	daos_mode = get_daos_obj_mode(flags);
	if (daos_mode == -1)
		return EINVAL;

	D_STRNDUP(rem, path, PMFS_MAX_PATH - 1);
	if (rem == NULL)
		return ENOMEM;

	if (stbuf)
		memset(stbuf, 0, sizeof(struct stat));

	D_ALLOC_PTR(obj);
	if (obj == NULL)
		D_GOTO(out, rc = ENOMEM);

	oid_cp(&obj->oid, root->oid);
	oid_cp(&obj->parent_oid, root->parent_oid);
	obj->chunk_size = root->chunk_size;
	obj->mode = root->mode;
	strncpy(obj->name, root->name, PMFS_MAX_NAME + 1);

	parent.mode = obj->mode;
	oid_cp(&parent.oid, obj->oid);
	oid_cp(&parent.parent_oid, obj->parent_oid);

	/** get the obj entry in the path */
	for (token = strtok_r(rem, "/", &sptr);
	     token != NULL;
	     token = strtok_r(NULL, "/", &sptr)) {
lookup_rel_path_loop:

		/*
		 * Open the directory object one level up.
		 * Since fetch_entry does not support ".",
		 * we can't support ".." as the last entry,
		 * nor can we support "../.." because we don't
		 * have parent.parent_oid and parent.mode.
		 * For now, represent this partial state with
		 * parent_fully_valid.
		 */
		parent_fully_valid = true;
		if (strcmp(token, "..") == 0) {
			parent_fully_valid = false;

			/* Cannot go outside the container */
			if (daos_oid_cmp(parent.oid, pmfs->root.oid) == 0) {
				D_DEBUG(DB_TRACE,
					"Failed to lookup path outside container: %s\n",
					path);
				D_GOTO(err_obj, rc = ENOENT);
			}

			oid_cp(&parent.oid, parent.parent_oid);

			/* TODO support fetch_entry(".") */
			token = strtok_r(NULL, "/", &sptr);
			if (!token || strcmp(token, "..") == 0)
				D_GOTO(err_obj, rc = ENOTSUP);
		}

		len = strlen(token);
		entry.chunk_size = 0;

		rc = fetch_entry(pmfs, pmfs->coh, parent.oid, token, len, true,
				 &exists, &entry);
		if (rc)
			D_GOTO(err_obj, rc);

		if (!exists)
			D_GOTO(err_obj, rc = ENOENT);

		oid_cp(&obj->oid, entry.oid);
		oid_cp(&obj->parent_oid, parent.oid);
		strncpy(obj->name, token, len + 1);
		obj->mode = entry.mode;

		/** if entry is a file, open the array object and return */
		if (S_ISREG(entry.mode)) {
			/* if there are more entries, then file is not a dir */
			if (strtok_r(NULL, "/", &sptr) != NULL) {
				D_ERROR("%s is not a directory\n", obj->name);
				D_GOTO(err_obj, rc = ENOENT);
			}

			if (stbuf) {
				stbuf->st_size = entry.file_size;
				stbuf->st_blocks =
					(stbuf->st_size + (1 << 9) - 1) >> 9;
			}
			break;
		}

		if (S_ISLNK(entry.mode)) {
			/*
			 * If there is a token after the sym link entry, treat
			 * the sym link as a directory and look up it's value.
			 */
			token = strtok_r(NULL, "/", &sptr);
			if (token) {
				struct pmfs_obj *sym;

				if (!parent_fully_valid &&
				    strncmp(entry.value, "..", 2) == 0) {
					D_FREE(entry.value);
					D_GOTO(err_obj, rc = ENOTSUP);
				}

				rc = lookup_rel_path(pmfs, &parent, entry.value,
						     flags, &sym, NULL, NULL,
						     depth + 1);
				if (rc) {
					D_DEBUG(DB_TRACE,
						"Failed to lookup symlink %s\n",
						entry.value);
					D_FREE(entry.value);
					D_GOTO(err_obj, rc);
				}

				parent.mode = sym->mode;
				oid_cp(&parent.oid, sym->oid);
				oid_cp(&parent.parent_oid, sym->parent_oid);

				D_FREE(sym);
				D_FREE(entry.value);
				obj->value = NULL;
				/*
				 * need to go to the beginning of loop but we
				 * already did the strtok.
				 */
				goto lookup_rel_path_loop;
			}

			/* Conditionally dereference leaf symlinks */
			if (!(flags & O_NOFOLLOW)) {
				struct pmfs_obj *sym;

				if (!parent_fully_valid &&
				    strncmp(entry.value, "..", 2) == 0) {
					D_FREE(entry.value);
					D_GOTO(err_obj, rc = ENOTSUP);
				}

				rc = lookup_rel_path(pmfs, &parent, entry.value,
						     flags, &sym, mode, stbuf,
						     depth + 1);
				if (rc) {
					D_DEBUG(DB_TRACE,
						"Failed to lookup symlink %s\n",
						entry.value);
					D_FREE(entry.value);
					D_GOTO(err_obj, rc);
				}

				/* return this dereferenced obj */
				D_FREE(obj);
				obj = sym;
				D_FREE(entry.value);
				D_GOTO(out, rc);
			}

			/* Create a truncated version of the string */
			D_STRNDUP(obj->value, entry.value, entry.value_len + 1);
			if (obj->value == NULL) {
				D_FREE(entry.value);
				D_GOTO(out, rc = ENOMEM);
			}
			D_FREE(entry.value);
			if (stbuf)
				stbuf->st_size = entry.value_len;
			/** return the symlink obj if this is the last entry */
			break;
		}

		if (!S_ISDIR(entry.mode)) {
			D_ERROR("Invalid entry type in path.\n");
			D_GOTO(err_obj, rc = EINVAL);
		}

		obj->chunk_size = entry.chunk_size;
		if (stbuf)
			stbuf->st_size = sizeof(entry);

		oid_cp(&parent.oid, obj->oid);
		oid_cp(&parent.parent_oid, obj->parent_oid);
		parent.mode = entry.mode;
	}

	if (mode)
		*mode = obj->mode;

	if (stbuf) {
		stbuf->st_nlink = 1;
		stbuf->st_mode = obj->mode;
		stbuf->st_uid = pmfs->uid;
		stbuf->st_gid = pmfs->gid;
		stbuf->st_atim.tv_sec = entry.atime;
		stbuf->st_mtim.tv_sec = entry.mtime;
		stbuf->st_ctim.tv_sec = entry.ctime;
	}

	obj->flags = flags;

out:
	D_FREE(rem);
	*_obj = obj;
	return rc;
err_obj:
	D_FREE(obj);
	goto out;
}

int
pmfs_lookup(struct pmfs *pmfs, const char *path, int flags, struct pmfs_obj **_obj,
	    mode_t *mode, struct stat *stbuf)
{
	if (pmfs == NULL || !pmfs->mounted)
		return EINVAL;
	if (_obj == NULL)
		return EINVAL;
	if (path == NULL || strnlen(path, PMFS_MAX_PATH) > PMFS_MAX_PATH - 1)
		return EINVAL;
	if (path[0] != '/')
		return EINVAL;

	return lookup_rel_path(pmfs, &pmfs->root, path, flags, _obj,
			       mode, stbuf, 0);
}

int
pmfs_readdir(struct pmfs *pmfs, struct pmfs_obj *obj, uint32_t *nr, struct dirent *dirs)
{
	daos_key_desc_t	*kds;
	char		*enum_buf;
	uint32_t	i, nr_children;
	int		rc = 0;
	uint64_t	len;
	char		*ptr;
	uint64_t	total_len;

	if (pmfs == NULL || !pmfs->mounted)
		return EINVAL;
	if (obj == NULL || !S_ISDIR(obj->mode))
		return ENOTDIR;
	if (*nr == 0)
		return 0;
	if (dirs == NULL)
		return EINVAL;

	rc = get_num_entries(pmfs, obj->oid, &nr_children, &total_len);
	if (rc) {
		D_ERROR("get_num_entries, "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	D_ALLOC_ARRAY(kds, nr_children);
	if (kds == NULL)
		return ENOMEM;

	D_ALLOC_ARRAY(enum_buf, nr_children * PMFS_MAX_NAME);
	if (enum_buf == NULL) {
		D_FREE(kds);
		return ENOMEM;
	}

	memset(enum_buf, 0, nr_children * PMFS_MAX_NAME);

	rc = vos_client_obj_list_dkeys_sync(pmfs->coh, obj->oid, &nr_children,
					    &len, kds, enum_buf,
					    pmfs->task_ring);
	if (rc)
		D_GOTO(out, rc = daos_der2errno(rc));

	for (ptr = enum_buf, i = 0; i < nr_children; i++) {
		len = snprintf(dirs[i].d_name,
			       kds[i].kd_key_len + 1, "%s", ptr);
		D_ASSERT(len >= kds[i].kd_key_len);
		ptr += kds[i].kd_key_len;
	}
	*nr = nr_children;

out:
	D_FREE(enum_buf);
	D_FREE(kds);
	return rc;
}

int
pmfs_open(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name, mode_t mode,
	  int flags, daos_size_t chunk_size, const char *value, struct pmfs_obj **_obj)
{
	return pmfs_open_stat(pmfs, parent, name, mode, flags, chunk_size,
			      value, _obj, NULL);
}

int
pmfs_mount(daos_handle_t poh, daos_handle_t coh, int flags, struct pmfs **pmfs)
{
	struct pmfs		*_pmfs;
	struct pmfs_entry	root_dir;
	daos_obj_id_t		cr_oids[2];
	int			amode, obj_mode;
	int			rc;

	if (pmfs == NULL) {
		return EINVAL;
	}

	amode = (flags & O_ACCMODE);
	obj_mode = get_daos_obj_mode(flags);
	if (obj_mode == -1) {
		return EINVAL;
	}

	D_ALLOC_PTR(_pmfs);
	if (_pmfs == NULL) {
		return -ENOMEM;
	}

	_pmfs->poh = poh;
	_pmfs->coh = coh;
	_pmfs->amode = amode;

	rc = D_MUTEX_INIT(&_pmfs->lock, NULL);
	if (rc != 0) {
		D_GOTO(err_pmfs, rc = daos_der2errno(rc));
	}

	cr_oids[0].lo = RESERVED_LO;
	cr_oids[0].hi = SB_HI;
	cr_oids[1].lo = RESERVED_LO;
	cr_oids[1].hi = ROOT_HI;

	_pmfs->super_oid = cr_oids[0];
	_pmfs->root.oid = cr_oids[1];
	_pmfs->root.parent_oid = _pmfs->super_oid;
	_pmfs->use_dtx = false;

	/* Create Task ring */
	_pmfs->task_ring = vos_target_create_tasks("PMFS_TASKS", PMFS_MAX_TASKS);
	if (_pmfs->task_ring == NULL) {
		rc = -ENOMEM;
		D_GOTO(err_pmfs, rc = daos_der2errno(rc));
	}

	/** Verify SB */
	rc = open_sb(coh, false, _pmfs->super_oid, &_pmfs->attr, _pmfs);
	if (rc) {
		D_GOTO(err_pmfs, rc);
	}

	/** Check if super object has the root entry */
	strcpy(_pmfs->root.name, "/");
	rc = open_dir(_pmfs, NULL, amode | S_IFDIR, &root_dir, 1, &_pmfs->root);
	if (rc) {
		D_ERROR("Failed to open root object, %d\n", rc);
		D_GOTO(err_pmfs, rc);
	}

	_pmfs->mounted = true;
	*pmfs = _pmfs;

	return 0;

err_pmfs:
	D_FREE(_pmfs);
	return rc;
}

int
pmfs_release(struct pmfs_obj *obj)
{
	int rc = 0;

	if (obj == NULL) {
		return EINVAL;
	}

	switch (obj->mode & S_IFMT) {
	case S_IFDIR:
		break;
	case S_IFREG:
		break;
	case S_IFLNK:
		D_FREE(obj->value);
		break;
	default:
		D_ERROR("Invalid entry type (not a dir, file, symlink).\n");
		rc = -DER_IO_INVAL;
	}

	if (rc) {
		D_ERROR("Failed to close PMFS object, "DF_RC"\n", DP_RC(rc));
	} else {
		D_FREE(obj);
	}

	return daos_der2errno(rc);
}

int
pmfs_umount(struct pmfs *pmfs)
{
	if (pmfs == NULL || !pmfs->mounted) {
		return EINVAL;
	}

	vos_target_free_tasks(pmfs->task_ring);

	D_MUTEX_DESTROY(&pmfs->lock);
	D_FREE(pmfs);

	return 0;
}

static void
compute_dkey(daos_size_t chunk_size, daos_off_t array_idx,
	     daos_size_t *num_records, daos_off_t *record_i, uint64_t *dkey)
{
	daos_size_t	dkey_val; /* dkey number */
	daos_off_t	dkey_i;	/* Logical Start IDX of dkey_val */
	daos_size_t	rec_i; /* the record index relative to the dkey */

	D_ASSERT(dkey);

	/* Compute dkey number and starting index relative to the array */
	dkey_val = array_idx / chunk_size;
	dkey_i = dkey_val * chunk_size;
	rec_i = array_idx - dkey_i;

	if (record_i)
		*record_i = rec_i;
	if (num_records)
		*num_records = chunk_size - rec_i;

	*dkey = dkey_val + 1;
}

static int
create_sgl(d_sg_list_t *user_sgl, daos_size_t num_records,
	   daos_off_t *sgl_off, daos_size_t *sgl_i,
	   d_sg_list_t *sgl)
{
	daos_size_t	k;
	daos_size_t	rem_records;
	daos_size_t	cur_i;
	daos_off_t	cur_off;

	cur_i = *sgl_i;
	cur_off = *sgl_off;
	k = 0;
	sgl->sg_nr = k;
	sgl->sg_iovs = NULL;
	rem_records = num_records;

	/*
	 * Keep iterating through the user sgl till we populate our sgl to
	 * satisfy the number of records to read/write from the KV object
	 */
	do {
		d_iov_t		*new_sg_iovs;

		D_ASSERT(user_sgl->sg_nr > cur_i);

		D_REALLOC_ARRAY(new_sg_iovs, sgl->sg_iovs, sgl->sg_nr,
				sgl->sg_nr + 1);
		if (new_sg_iovs == NULL)
			return -DER_NOMEM;
		sgl->sg_nr++;
		sgl->sg_iovs = new_sg_iovs;

		sgl->sg_iovs[k].iov_buf = user_sgl->sg_iovs[cur_i].iov_buf +
			cur_off;

		if (rem_records >= (user_sgl->sg_iovs[cur_i].iov_len - cur_off)) {
			sgl->sg_iovs[k].iov_len =
				user_sgl->sg_iovs[cur_i].iov_len - cur_off;
			cur_i++;
			cur_off = 0;
		} else {
			sgl->sg_iovs[k].iov_len = rem_records;
			cur_off += rem_records;
		}

		sgl->sg_iovs[k].iov_buf_len = sgl->sg_iovs[k].iov_len;
		rem_records -= sgl->sg_iovs[k].iov_len;
		k++;
	} while (rem_records && user_sgl->sg_nr > cur_i);

	sgl->sg_nr_out = 0;

	*sgl_i = cur_i;
	*sgl_off = cur_off;

	return 0;
}

int
pmfs_write_sync(struct pmfs *pmfs, struct pmfs_obj *obj, d_sg_list_t *user_sgl,
		daos_off_t off)
{
	daos_size_t		fsize, buf_size;
	int			i;
	int			rc;
	daos_iod_t		iod;
	daos_recx_t		recx;
	daos_key_t		dkey;
	uint64_t		dkey_val;
	daos_size_t		num_records, current_iov_nr;
	char			akey_val;
	daos_off_t		record_i, offset, iov_offset;
	d_sg_list_t		sgl;
	bool			need_update_fsize = false;

	if (pmfs == NULL || !pmfs->mounted)
		return EINVAL;
	if (pmfs->amode != O_RDWR)
		return EPERM;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if ((obj->flags & O_ACCMODE) == O_RDONLY)
		return EPERM;

	buf_size = 0;
	if (user_sgl) {
		for (i = 0; i < user_sgl->sg_nr; i++) {
			buf_size += user_sgl->sg_iovs[i].iov_len;
		}
	}
	if (buf_size == 0) {
		return 0;
	}

	rc = pmfs_obj_get_file_size(pmfs, obj, &fsize);
	if (rc) {
		D_ERROR("Failed to get file size %s, "DF_RC"\n",
			obj->name, DP_RC(rc));
		return daos_der2errno(rc);
	}

	if (off + buf_size > fsize) {
		need_update_fsize = true;
		fsize = off + buf_size;
	}

	offset = off;
	iov_offset = 0;
	current_iov_nr = 0;

	D_DEBUG(DB_TRACE, "PMFS Write: Off %"PRIu64", Len %zu\n", off, buf_size);

	do {
		compute_dkey(obj->chunk_size, offset, &num_records, &record_i,
			     &dkey_val);
		/** Set integer dkey descriptor */
		d_iov_set(&dkey, &dkey_val, sizeof(uint64_t));
		/** Set character akey descriptor - TODO: should be NULL */
		akey_val = '0';
		d_iov_set(&iod.iod_name, &akey_val, 1);

		if (num_records > buf_size) {
			num_records = buf_size;
		}

		/** Initialize the rest of the IOD fields */
		recx.rx_idx	= record_i;
		recx.rx_nr	= num_records;
		iod.iod_nr	= 1;
		iod.iod_recxs	= &recx;
		iod.iod_type	= DAOS_IOD_ARRAY;
		iod.iod_size	= 1;

		sgl.sg_iovs = NULL;
		rc = create_sgl(user_sgl, num_records, &iov_offset, &current_iov_nr,
				&sgl);
		if (rc) {
			return -ENOMEM;
		}

		rc = vos_client_obj_update_sync(pmfs->coh, obj->oid, crt_hlc_get(), 0,
						0, &dkey, 1, &iod, &sgl,
						pmfs->task_ring);
		if (rc) {
			D_ERROR("Failed to insert entry %s, "DF_RC"\n",
				obj->name, DP_RC(rc));
		}

		if (sgl.sg_iovs) {
			D_FREE(sgl.sg_iovs);
		}

		buf_size -= num_records;
		offset += num_records;

	} while (buf_size != 0);

	if (need_update_fsize) {
		rc = pmfs_obj_set_file_size(pmfs, obj, fsize);
		if (rc) {
			D_ERROR("Failed to update new size %s, "DF_RC"\n",
				obj->name, DP_RC(rc));
			return daos_der2errno(rc);
		}
	}

	return rc;
}

int
pmfs_read_sync(struct pmfs *pmfs, struct pmfs_obj *obj, d_sg_list_t *user_sgl,
	       daos_off_t off, daos_size_t *read_size)
{
	daos_size_t		buf_size;
	int			i;
	int			rc;
	daos_iod_t		iod;
	daos_recx_t		recx;
	daos_key_t		dkey;
	uint64_t		dkey_val;
	daos_size_t		num_records, current_iov_nr;
	char			akey_val;
	daos_off_t		record_i, offset, iov_offset;
	d_sg_list_t		sgl;

	if (pmfs == NULL || !pmfs->mounted)
		return EINVAL;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if (read_size == NULL)
		return EINVAL;
	if ((obj->flags & O_ACCMODE) == O_WRONLY)
		return EPERM;

	buf_size = 0;
	for (i = 0; i < user_sgl->sg_nr; i++) {
		buf_size += user_sgl->sg_iovs[i].iov_len;
	}

	if (buf_size == 0) {
		*read_size = 0;
		return 0;
	}
	offset = off;
	iov_offset = 0;
	current_iov_nr = 0;
	*read_size = buf_size;

	D_DEBUG(DB_TRACE, "PMFS Read: Off %"PRIu64", Len %zu\n", off, buf_size);
	do {
		compute_dkey(obj->chunk_size, offset, &num_records, &record_i,
			     &dkey_val);
		/** Set integer dkey descriptor */
		d_iov_set(&dkey, &dkey_val, sizeof(uint64_t));
		/** Set character akey descriptor - TODO: should be NULL */
		akey_val = '0';
		d_iov_set(&iod.iod_name, &akey_val, 1);

		if (num_records > buf_size) {
			num_records = buf_size;
		}

		/** Initialize the rest of the IOD fields */
		recx.rx_idx	= record_i;
		recx.rx_nr	= num_records;
		iod.iod_nr	= 1;
		iod.iod_recxs	= &recx;
		iod.iod_type	= DAOS_IOD_ARRAY;
		iod.iod_size	= 1;

		sgl.sg_iovs = NULL;
		sgl.sg_nr_out = 1;
		rc = create_sgl(user_sgl, num_records, &iov_offset, &current_iov_nr,
				&sgl);
		if (rc) {
			return -ENOMEM;
		}
		rc = vos_client_obj_fetch_sync(pmfs->coh, obj->oid, crt_hlc_get(), 0,
					       &dkey, 1, &iod, &sgl,
					       pmfs->task_ring);
		if (rc) {
			D_ERROR("Failed to insert entry %s, "DF_RC"\n",
				obj->name, DP_RC(rc));
		}

		if (sgl.sg_iovs) {
			D_FREE(sgl.sg_iovs);
		}

		buf_size -= num_records;
		offset += num_records;
	} while (buf_size != 0);

	return 0;
}

static int
punch_chunks(struct pmfs *pmfs, struct pmfs_obj *obj, daos_off_t off, daos_size_t len)
{
	int			rc;
	daos_iod_t		iod;
	daos_recx_t		recx;
	daos_key_t		dkey;
	daos_key_t		akey;
	uint64_t		dkey_val;
	char			akey_val;
	daos_off_t		record_i, offset;
	daos_size_t		num_records, length;

	offset = off;
	length = len;

	do {
		compute_dkey(obj->chunk_size, offset, &num_records, &record_i,
			     &dkey_val);
		/** Set integer dkey descriptor */
		d_iov_set(&dkey, &dkey_val, sizeof(uint64_t));
		/** Set character akey descriptor - TODO: should be NULL */
		akey_val = '0';

		if (num_records > length) {
			num_records = length;
		}

		/* Punch whole DKEY */
		if (num_records == obj->chunk_size) {
			d_iov_set(&akey, &akey_val, sizeof(char));
			rc = vos_client_obj_punch_sync(pmfs->coh, obj->oid, crt_hlc_get(),
						       0, 0, &dkey, 1, &akey,
						       pmfs->task_ring);
		} else {
			d_iov_set(&iod.iod_name, &akey_val, 1);
			recx.rx_idx	= record_i + 1;
			recx.rx_nr	= num_records;
			iod.iod_nr	= 1;
			iod.iod_recxs	= &recx;
			iod.iod_type	= DAOS_IOD_ARRAY;
			/* 0 to punch */
			iod.iod_size	= 0;
			rc = vos_client_obj_update_sync(pmfs->coh, obj->oid, crt_hlc_get(),
							0, 0,
							&dkey, 1, &iod, NULL,
							pmfs->task_ring);
		}

		if (rc) {
			D_ERROR("Failed to punch %s, "DF_RC"\n",
				obj->name, DP_RC(rc));
			return rc;
		}

		length -= num_records;
		offset += num_records;

	} while (length != 0);

	return 0;
}

/** Deallocate similar API */
int
pmfs_punch(struct pmfs *pmfs, struct pmfs_obj *obj, daos_off_t offset, daos_size_t len)
{
	daos_size_t		size;
	daos_off_t		hi;
	int			rc;

	if (pmfs == NULL || !pmfs->mounted)
		return EINVAL;
	if (pmfs->amode != O_RDWR)
		return EPERM;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if ((obj->flags & O_ACCMODE) == O_RDONLY)
		return EPERM;

	rc = pmfs_obj_get_file_size(pmfs, obj, &size);
	if (rc)
		return daos_der2errno(rc);

	/** nothing to do if offset is larger or equal to the file size */
	if (size <= offset)
		return 0;

	hi = offset + len;
	/** fsize is between the range to punch */
	if (offset < size && size <= hi) {
		/** Punch offset -> size - offset */
		rc = punch_chunks(pmfs, obj, offset, size - offset);
		if (rc) {
			D_ERROR("Failed to punch %s, "DF_RC"\n",
				obj->name, DP_RC(rc));
			return rc;
		}
		rc = pmfs_obj_set_file_size(pmfs, obj, offset);
		return rc;
	}

	D_ASSERT(size > hi);

	/** Punch offset -> len only */
	rc = punch_chunks(pmfs, obj, offset, len);
	if (rc) {
		D_ERROR("Failed to punch %s, "DF_RC"\n",
			obj->name, DP_RC(rc));
	}

	return rc;
}

static int
entry_stat(struct pmfs *pmfs, daos_handle_t coh, daos_obj_id_t parent_oid,
	   const char *name, size_t len, struct stat *stbuf)
{
	struct pmfs_entry	entry = {};
	bool			exists;
	daos_size_t		size = 0;
	int			rc;

	memset(stbuf, 0, sizeof(struct stat));

	/* Check if parent has the entry */
	rc = fetch_entry(pmfs, pmfs->coh, parent_oid, name, len,
			 false, &exists, &entry);
	if (rc)
		return rc;

	if (!exists)
		return ENOENT;

	switch (entry.mode & S_IFMT) {
	case S_IFDIR:
		size = sizeof(entry);
		break;
	case S_IFREG:
	{
		/*
		 * TODO - this is not accurate since it does not account for
		 * sparse files or file metadata or xattributes.
		 */
		stbuf->st_blocks = (entry.file_size + (1 << 9) - 1) >> 9;
		stbuf->st_blksize = entry.chunk_size ? entry.chunk_size :
			pmfs->attr.da_chunk_size;
		break;
	}
	case S_IFLNK:
		size = entry.value_len;
		D_FREE(entry.value);
		break;
	default:
		D_ERROR("Invalid entry type (not a dir, file, symlink).\n");
		return EINVAL;
	}

	stbuf->st_nlink = 1;
	stbuf->st_size = size;
	stbuf->st_mode = entry.mode;
	stbuf->st_uid = pmfs->uid;
	stbuf->st_gid = pmfs->gid;
	stbuf->st_atim.tv_sec = entry.atime;
	stbuf->st_mtim.tv_sec = entry.mtime;
	stbuf->st_ctim.tv_sec = entry.ctime;

	return 0;
}

int
pmfs_stat(struct pmfs *pmfs, struct pmfs_obj *parent, const char *name, struct stat *stbuf)
{
	size_t		len;
	daos_obj_id_t	oid;
	int		rc;

	if (pmfs == NULL || !pmfs->mounted)
		return EINVAL;
	if (parent == NULL)
		parent = &pmfs->root;
	else if (!S_ISDIR(parent->mode))
		return ENOTDIR;

	if (name == NULL) {
		if (strcmp(parent->name, "/") != 0) {
			D_ERROR("Invalid path %s and entry name is NULL)\n",
				parent->name);
			return EINVAL;
		}
		name = parent->name;
		len = strlen(parent->name);
		oid = pmfs->super_oid;
	} else {
		rc = check_name(name, &len);
		if (rc)
			return rc;
		oid = parent->oid;
	}

	return entry_stat(pmfs, pmfs->coh, oid, name, len, stbuf);
}

int
pmfs_truncate(struct pmfs *pmfs, struct pmfs_obj *obj, daos_size_t len)
{
	daos_size_t		size;
	int			rc;

	if (pmfs == NULL || !pmfs->mounted)
		return EINVAL;
	if (pmfs->amode != O_RDWR)
		return EPERM;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if ((obj->flags & O_ACCMODE) == O_RDONLY)
		return EPERM;

	rc = pmfs_obj_get_file_size(pmfs, obj, &size);
	if (rc) {
		return daos_der2errno(rc);
	}

	if (size > len) {
		/** Punch from len to size */
		rc = punch_chunks(pmfs, obj, len, size - len);
		if (rc) {
			D_ERROR("Failed to punch %s, "DF_RC"\n",
				obj->name, DP_RC(rc));
			return rc;
		}
	}

	rc = pmfs_obj_set_file_size(pmfs, obj, len);
	if (rc) {
		D_ERROR("Can't truncate file %s "DF_RC"\n", obj->name,
			DP_RC(rc));
		return rc;
	}

	return 0;
}

int
pmfs_rename(struct pmfs *pmfs, struct pmfs_obj *parent, const char *old_name,
	    const char *new_name)
{
	daos_obj_id_t		parent_oid;
	struct pmfs_entry	entry = {};
	struct pmfs_entry	tmp_entry = {};
	bool			exists;
	int			rc;
	size_t			old_len, new_len;

	if (pmfs == NULL || !pmfs->mounted)
		return EINVAL;
	if (pmfs->amode != O_RDWR)
		return EPERM;

	if (check_name(old_name, &old_len) || check_name(new_name, &new_len)) {
		return EINVAL;
	}

	if (parent) {
		parent_oid = parent->oid;
	} else {
		parent_oid = pmfs->super_oid;
	}

	/* Check if parent has the entry */
	rc = fetch_entry(pmfs, pmfs->coh, parent_oid, old_name, old_len,
			 false, &exists, &entry);
	if (rc) {
		D_ERROR("Can't fetch entry for name %s, "DF_RC"\n",
			old_name, DP_RC(rc));
		return rc;
	}

	if (!exists) {
		D_ERROR("Old entry %s doesn't exist, "DF_RC"\n", old_name,
			DP_RC(rc));
		return ENOENT;
	}

	exists = false;
	rc = fetch_entry(pmfs, pmfs->coh, parent_oid, new_name, new_len,
			 false, &exists, &tmp_entry);
	if (rc) {
		D_ERROR("Can't fetch entry of name %s, "DF_RC"\n", new_name,
			DP_RC(rc));
		return rc;
	}

	if (exists) {
		D_ERROR("New entry %s exists, "DF_RC"\n", new_name, DP_RC(rc));
		return EEXIST;
	}

	/* Update DKEY with new name */
	rc = insert_entry(pmfs, pmfs->coh, parent_oid, new_name, new_len,
			  DAOS_COND_DKEY_INSERT, &entry);
	if (rc) {
		D_ERROR("Failed to insert new entry %s, "DF_RC"\n", new_name,
			DP_RC(rc));
		return rc;
	}

	/* Remove old entry */
	rc = remove_entry(pmfs, pmfs->coh, parent_oid, old_name, old_len, entry);
	if (rc) {
		D_ERROR("Failed to remove old entry %s, "DF_RC"\n", old_name,
			DP_RC(rc));
		return rc;
	}

	return 0;
}
