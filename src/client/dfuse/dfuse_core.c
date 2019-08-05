/**
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

#include "dfuse_common.h"
#include "dfuse.h"

/* Inode record hash table operations */

/* Use a custom hash function for this table, as the key contains a pointer
 * to some entries which need to be checked, therefore different pointer
 * values will return different hash buckets, even if the data pointed to
 * would match.  By providing a custom hash function this ensures that only
 * invariant data is checked.
 */
static uint32_t
ir_key_hash(struct d_hash_table *htable, const void *key,
	    unsigned int ksize)
{
	const struct dfuse_inode_record_id *ir_id = key;

	return (uint32_t)ir_id->irid_oid.hi;
}

static bool
ir_key_cmp(struct d_hash_table *htable, d_list_t *rlink,
	   const void *key, unsigned int ksize)
{
	const struct dfuse_inode_record		*ir;
	const struct dfuse_inode_record_id	*ir_id = key;

	ir = container_of(rlink, struct dfuse_inode_record, ir_htl);

	/* First check if the both parts of the OID match */
	if (ir->ir_id.irid_oid.lo != ir_id->irid_oid.lo)
		return false;

	if (ir->ir_id.irid_oid.hi != ir_id->irid_oid.hi)
		return false;

	/* Then check if it's the same container (dfs struct) */
	if (ir->ir_id.irid_dfs == ir_id->irid_dfs) {
		return true;
	}

	/* Now check the pool name */
	if (strncmp(ir->ir_id.irid_dfs->dfs_pool,
		    ir_id->irid_dfs->dfs_pool,
		    NAME_MAX) != 0) {
		return false;
	}

	/* Now check the container name */
	if (strncmp(ir->ir_id.irid_dfs->dfs_cont,
		    ir_id->irid_dfs->dfs_cont,
			NAME_MAX) != 0) {
		return false;
	}

	/* This case means it's the same container name, but a different dfs
	 * struct which can happen with repeated lookups of already open
	 * containers
	 */
	return true;
}

static void
ir_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfuse_inode_record *ir;

	ir = container_of(rlink, struct dfuse_inode_record, ir_htl);

	D_FREE(ir);
}

/* Inode entry hash table operations */

static bool
ih_key_cmp(struct d_hash_table *htable, d_list_t *rlink,
	   const void *key, unsigned int ksize)
{
	const struct dfuse_inode_entry	*ie;
	const ino_t			*ino = key;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	return *ino == ie->ie_stat.st_ino;
}

static void
ih_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfuse_inode_entry	*ie;
	int				oldref;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);
	oldref = atomic_fetch_add(&ie->ie_ref, 1);
	DFUSE_TRA_DEBUG(ie, "addref to %u", oldref + 1);
}

static bool
ih_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfuse_inode_entry	*ie;
	int				oldref;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);
	oldref = atomic_fetch_sub(&ie->ie_ref, 1);
	DFUSE_TRA_DEBUG(ie, "decref to %u", oldref - 1);
	return oldref == 1;
}

static void
ih_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfuse_projection_info	*fs_handle = htable->ht_priv;
	struct dfuse_inode_entry	*ie;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	DFUSE_TRA_DEBUG(ie, "parent %lu", ie->ie_parent);
	ie_close(fs_handle, ie);
}

static d_hash_table_ops_t ie_hops = {
	.hop_key_cmp	= ih_key_cmp,
	.hop_rec_addref	= ih_addref,
	.hop_rec_decref	= ih_decref,
	.hop_rec_free	= ih_free,
};

static d_hash_table_ops_t ir_hops = {
	.hop_key_cmp	= ir_key_cmp,
	.hop_key_hash   = ir_key_hash,
	.hop_rec_free	= ir_free,

};

int
dfuse_start(struct dfuse_info *dfuse_info, struct dfuse_dfs *dfs)
{
	struct dfuse_projection_info	*fs_handle;
	struct fuse_args		args = {0};
	struct fuse_lowlevel_ops	*fuse_ops = NULL;
	struct dfuse_inode_entry	*ie = NULL;
	int				rc;

	D_ALLOC_PTR(fs_handle);
	if (!fs_handle)
		return false;

	fs_handle->dpi_info = dfuse_info;

	rc = d_hash_table_create_inplace(D_HASH_FT_RWLOCK | D_HASH_FT_EPHEMERAL,
					 3, fs_handle, &ie_hops,
					 &fs_handle->dpi_iet);
	if (rc != 0)
		D_GOTO(err, 0);

	rc = d_hash_table_create_inplace(D_HASH_FT_RWLOCK, 3, fs_handle,
					 &ir_hops, &fs_handle->dpi_irt);
	if (rc != 0)
		D_GOTO(err, 0);

	fs_handle->dpi_proj.progress_thread = 1;

	atomic_fetch_add(&fs_handle->dpi_ino_next, 2);

	args.argc = 4;

	args.allocated = 1;
	D_ALLOC_ARRAY(args.argv, args.argc);
	if (!args.argv)
		D_GOTO(err, 0);

	D_STRNDUP(args.argv[0], "", 1);
	if (!args.argv[0])
		D_GOTO(err, 0);

	D_STRNDUP(args.argv[1], "-ofsname=dfuse", 32);
	if (!args.argv[1])
		D_GOTO(err, 0);

	D_STRNDUP(args.argv[2], "-osubtype=daos", 32);
	if (!args.argv[2])
		D_GOTO(err, 0);

	D_ASPRINTF(args.argv[3], "-omax_read=%u", fs_handle->dpi_max_read);
	if (!args.argv[3])
		D_GOTO(err, 0);

	fuse_ops = dfuse_get_fuse_ops();
	if (!fuse_ops)
		D_GOTO(err, 0);

	/* Create the root inode and insert into table */
	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(err, 0);

	ie->ie_dfs = dfs;
	ie->ie_parent = 1;
	atomic_fetch_add(&ie->ie_ref, 1);
	ie->ie_stat.st_ino = 1;
	ie->ie_stat.st_mode = 0700 | S_IFDIR;
	dfs->dfs_root = ie->ie_stat.st_ino;

	if (dfs->dfs_ops == &dfuse_dfs_ops) {
		rc = dfs_lookup(dfs->dfs_ns, "/", O_RDONLY, &ie->ie_obj,
				NULL, NULL);
		if (rc) {
			DFUSE_TRA_ERROR(ie, "dfs_lookup() failed: (%s)",
					strerror(-rc));
			D_GOTO(err, 0);
		}
	}

	rc = d_hash_rec_insert(&fs_handle->dpi_iet,
			       &ie->ie_stat.st_ino,
			       sizeof(ie->ie_stat.st_ino),
			       &ie->ie_htl,
			       false);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(fs_handle, "hash_insert() failed: %d",
				rc);
		D_GOTO(err, 0);
	}

	if (!dfuse_launch_fuse(dfuse_info, fuse_ops, &args, fs_handle)) {
		DFUSE_TRA_ERROR(fs_handle, "Unable to register FUSE fs");
		D_GOTO(err, 0);
	}

	D_FREE(fuse_ops);

	return -DER_SUCCESS;
err:
	DFUSE_TRA_ERROR(fs_handle, "Failed");
	D_FREE(fuse_ops);
	D_FREE(ie);
	D_FREE(fs_handle);
	return -DER_INVAL;
}

static int
ino_flush(d_list_t *rlink, void *arg)
{
	struct dfuse_projection_info *fs_handle = arg;
	struct dfuse_inode_entry *ie = container_of(rlink,
						  struct dfuse_inode_entry,
						  ie_htl);
	int rc;

	/* Only evict entries that are direct children of the root, the kernel
	 * will walk the tree for us
	 */
	if (ie->ie_parent != 1)
		return 0;

	rc = fuse_lowlevel_notify_inval_entry(fs_handle->dpi_info->di_session,
					      ie->ie_parent,
					      ie->ie_name,
					      strlen(ie->ie_name));
	if (rc != 0)
		DFUSE_TRA_WARNING(ie,
				  "%lu %lu '%s': %d %s",
				  ie->ie_parent, ie->ie_stat.st_ino,
				  ie->ie_name, rc, strerror(-rc));
	else
		DFUSE_TRA_INFO(ie,
			       "%lu %lu '%s': %d %s",
			       ie->ie_parent, ie->ie_stat.st_ino,
			       ie->ie_name, rc, strerror(-rc));

	/* If the FUSE connection is dead then do not traverse further, it
	 * doesn't matter what gets returned here, as long as it's negative
	 */
	if (rc == -EBADF)
		return -DER_NO_HDL;

	return -DER_SUCCESS;
}

/* Called once per projection, after the FUSE filesystem has been torn down */
int
dfuse_destroy_fuse(struct dfuse_projection_info *fs_handle)
{
	d_list_t	*rlink;
	uint64_t	refs = 0;
	int		handles = 0;
	int		rc;
	int		rcp = 0;

	DFUSE_TRA_INFO(fs_handle, "Flushing inode table");

	rc = d_hash_table_traverse(&fs_handle->dpi_iet, ino_flush, fs_handle);

	DFUSE_TRA_INFO(fs_handle, "Flush complete: %d", rc);

	DFUSE_TRA_INFO(fs_handle, "Draining inode table");
	do {
		struct dfuse_inode_entry *ie;
		uint32_t ref;

		rlink = d_hash_rec_first(&fs_handle->dpi_iet);

		if (!rlink)
			break;

		ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

		ref = atomic_load_consume(&ie->ie_ref);

		DFUSE_TRA_DEBUG(ie, "Dropping %d", ref);

		refs += ref;
		ie->ie_parent = 0;
		d_hash_rec_ndecref(&fs_handle->dpi_iet, ref, rlink);
		handles++;
	} while (rlink);

	if (handles) {
		DFUSE_TRA_WARNING(fs_handle, "dropped %lu refs on %u inodes",
				  refs, handles);
	} else {
		DFUSE_TRA_INFO(fs_handle, "dropped %lu refs on %u inodes",
			       refs, handles);
	}

	rc = d_hash_table_destroy_inplace(&fs_handle->dpi_iet, false);
	if (rc) {
		DFUSE_TRA_WARNING(fs_handle, "Failed to close inode handles");
		rcp = EINVAL;
	}

	rc = d_hash_table_destroy_inplace(&fs_handle->dpi_irt, true);
	if (rc) {
		DFUSE_TRA_WARNING(fs_handle, "Failed to close inode handles");
		rcp = EINVAL;
	}

	return rcp;
}
