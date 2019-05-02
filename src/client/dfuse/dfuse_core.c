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
#include "dfuse_da.h"

/*
 * Wrapper function that is called from FUSE to send RPCs. The idea is to
 * decouple the FUSE implementation from the actual sending of RPCs. The
 * FUSE callbacks only need to specify the inputs and outputs for the RPC,
 * without bothering about how RPCs are sent. This function is also intended
 * for abstracting various other features related to RPCs such as fail-over
 * and load balance, at the same time preventing code duplication.
 *
 */
int
dfuse_fs_send(struct dfuse_request *request)
{
	int rc;

	D_ASSERT(request->ir_api->on_result);
	/* If the API has passed in a simple inode number then translate it
	 * to either root, or do a hash table lookup on the inode number.
	 * Keep a reference on the inode open which will be dropped after
	 * a call to on_result().
	 */
	if (request->ir_ht == RHS_INODE_NUM) {

		if (request->ir_inode_num == 1) {
			request->ir_ht = RHS_ROOT;
		} else {
			rc = find_inode(request);
			if (rc != 0) {
				D_GOTO(err, 0);
			}
			request->ir_ht = RHS_INODE;
		}
	}
	return EIO;
err:
	DFUSE_TRA_ERROR(request, "Could not send rpc, rc = %d", rc);

	return rc;
}

static bool
ih_key_cmp(struct d_hash_table *htable, d_list_t *rlink,
	   const void *key, unsigned int ksize)
{
	const struct dfuse_inode_entry	*ie;
	const ino_t			*ino = key;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	return *ino == ie->stat.st_ino;
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
	struct dfuse_inode_entry		*ie;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	DFUSE_TRA_DEBUG(ie, "parent %lu", ie->parent);
	ie_close(fs_handle, ie);
}

d_hash_table_ops_t hops = {.hop_key_cmp = ih_key_cmp,
			   .hop_rec_addref = ih_addref,
			   .hop_rec_decref = ih_decref,
			   .hop_rec_free = ih_free,
};

#define COMMON_INIT(type)						\
	static void type##_common_init(void *arg, void *handle)		\
	{								\
		struct common_req *req = arg;				\
		DFUSE_REQUEST_INIT(&req->request, handle);		\
	}
COMMON_INIT(getattr);
COMMON_INIT(setattr);

/* Reset and prepare for use a common descriptor */
static bool
common_reset(void *arg)
{
	struct common_req *req = arg;

	req->request.req = NULL;

	DFUSE_REQUEST_RESET(&req->request);

	return true;
}

#define ENTRY_INIT(type)						\
	static void type##_entry_init(void *arg, void *handle)		\
	{								\
		struct entry_req *req = arg;				\
		DFUSE_REQUEST_INIT(&req->request, handle);		\
		req->dest = NULL;					\
		req->ie = NULL;						\
	}
ENTRY_INIT(symlink);

static bool
entry_reset(void *arg)
{
	struct entry_req	*req = arg;

	/* If this descriptor has previously been used then destroy the
	 * existing RPC
	 */
	DFUSE_REQUEST_RESET(&req->request);

	req->request.ir_ht = RHS_INODE_NUM;
	/* Free any destination string on this descriptor.  This is only used
	 * for symlink to store the link target whilst the RPC is being sent
	 */
	D_FREE(req->dest);

	if (!req->ie) {
		D_ALLOC_PTR(req->ie);
		if (!req->ie)
			return false;
		atomic_fetch_add(&req->ie->ie_ref, 1);
	}

	return true;
}

/* Destroy a descriptor which could be either getattr or getfattr */
static void
entry_release(void *arg)
{
	struct entry_req *req = arg;

	D_FREE(req->ie);
}

int
dfuse_start(struct dfuse_info *dfuse_info, dfs_t *ddfs)
{
	struct dfuse_projection_info	*fs_handle;
	struct fuse_args		args = {0};
	struct fuse_lowlevel_ops	*fuse_ops = NULL;
	struct dfuse_inode_entry	*inode = NULL;
	struct dfuse_dfs		*dfs = NULL;
	mode_t				mode;
	int				rc;

	struct dfuse_da_reg common = {.reset = common_reset,
				      POOL_TYPE_INIT(common_req, list)};

	struct dfuse_da_reg entry = {.reset = entry_reset,
				     .release = entry_release,
				     POOL_TYPE_INIT(entry_req, list)};

	D_ALLOC_PTR(fs_handle);
	if (!fs_handle)
		return false;

	rc = dfuse_da_init(&fs_handle->da, fs_handle);
	if (rc != -DER_SUCCESS)
		D_GOTO(err, 0);

	rc = d_hash_table_create_inplace(D_HASH_FT_RWLOCK |
					  D_HASH_FT_EPHEMERAL,
					  3,
					  fs_handle, &hops,
					  &fs_handle->inode_ht);
	if (rc != 0)
		D_GOTO(err, 0);

	fs_handle->proj.progress_thread = 1;

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

	D_STRNDUP(args.argv[2], "-osubtype=pam", 32);
	if (!args.argv[2])
		D_GOTO(err, 0);

	D_ASPRINTF(args.argv[3], "-omax_read=%u", fs_handle->max_read);
	if (!args.argv[3])
		D_GOTO(err, 0);

	fuse_ops = dfuse_get_fuse_ops(fs_handle->flags);
	if (!fuse_ops)
		D_GOTO(err, 0);

	common.init = getattr_common_init;
	fs_handle->fgh_da = dfuse_da_register(&fs_handle->da, &common);
	if (!fs_handle->fgh_da)
		D_GOTO(err, 0);

	common.init = setattr_common_init;
	fs_handle->fsh_da = dfuse_da_register(&fs_handle->da, &common);
	if (!fs_handle->fsh_da)
		D_GOTO(err, 0);

	entry.init = symlink_entry_init;
	fs_handle->symlink_da = dfuse_da_register(&fs_handle->da,
						      &entry);
	if (!fs_handle->symlink_da)
		D_GOTO(err, 0);

	/* Create the root inode and insert into table */
	D_ALLOC_PTR(inode);
	if (!inode) {
		D_GOTO(err, 0);
	}

	D_ALLOC_PTR(dfs);
	if (!dfs) {
		D_GOTO(err, 0);
	}

	dfs->dffs_dfs = ddfs;

	rc = dfs_lookup(dfs->dffs_dfs,
			"/", O_RDONLY, &inode->obj, &mode);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(fs_handle, "dfs_lookup() failed: %d",
				rc);
		D_GOTO(err, 0);
	}

	atomic_fetch_add(&inode->ie_ref, 1);

	rc = dfs_ostat(dfs->dffs_dfs, inode->obj, &inode->stat);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(fs_handle, "dfs_ostat() failed: %d",
				rc);
		D_GOTO(err, 0);
	}

	dfs->dffs_ops = &dfuse_dfs_ops;
	inode->ie_dfs = dfs;
	inode->parent = 1;

	inode->stat.st_ino = 1;

	rc = d_hash_rec_insert(&fs_handle->inode_ht,
			       &inode->stat.st_ino,
			       sizeof(inode->stat.st_ino),
			       &inode->ie_htl,
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
	dfuse_da_destroy(&fs_handle->da);
	D_FREE(fuse_ops);
	D_FREE(inode);
	D_FREE(dfs);
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
	if (ie->parent != 1)
		return 0;

	rc = fuse_lowlevel_notify_inval_entry(fs_handle->session,
					      ie->parent,
					      ie->name,
					      strlen(ie->name));
	if (rc != 0)
		DFUSE_TRA_WARNING(ie,
				  "%lu %lu '%s': %d %s",
				  ie->parent, ie->stat.st_ino, ie->name, rc,
				  strerror(-rc));
	else
		DFUSE_TRA_INFO(ie,
			       "%lu %lu '%s': %d %s",
			       ie->parent, ie->stat.st_ino, ie->name, rc,
			       strerror(-rc));

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

	rc = d_hash_table_traverse(&fs_handle->inode_ht, ino_flush,
				   fs_handle);

	DFUSE_TRA_INFO(fs_handle, "Flush complete: %d", rc);

	DFUSE_TRA_INFO(fs_handle, "Draining inode table");
	do {
		struct dfuse_inode_entry *ie;
		uint ref;

		rlink = d_hash_rec_first(&fs_handle->inode_ht);

		if (!rlink)
			break;

		ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

		ref = atomic_load_consume(&ie->ie_ref);

		DFUSE_TRA_DEBUG(ie, "Dropping %d", ref);

		refs += ref;
		ie->parent = 0;
		d_hash_rec_ndecref(&fs_handle->inode_ht, ref, rlink);
		handles++;
	} while (rlink);

	if (handles) {
		DFUSE_TRA_WARNING(fs_handle,
				  "dropped %lu refs on %u inodes",
				  refs, handles);
	} else {
		DFUSE_TRA_INFO(fs_handle,
			       "dropped %lu refs on %u inodes",
			       refs, handles);
	}

	rc = d_hash_table_destroy_inplace(&fs_handle->inode_ht, false);
	if (rc) {
		DFUSE_TRA_WARNING(fs_handle, "Failed to close inode handles");
		rcp = EINVAL;
	}

	do {
		/* If this context has a da associated with it then reap
		 * any descriptors with it so there are no pending RPCs when
		 * we call context_destroy.
		 */
		bool active;

		do {

			active = dfuse_da_reclaim(&fs_handle->da);

			if (!active)
				break;

			DFUSE_TRA_INFO(fs_handle,
				       "Active descriptors, waiting for one second");

		} while (active && rc == -DER_SUCCESS);

	} while (rc == -DER_BUSY);

	if (rc != -DER_SUCCESS)
		DFUSE_TRA_ERROR(fs_handle, "Count not destroy context");

	dfuse_da_destroy(&fs_handle->da);

	return rcp;
}
