/**
 * (C) Copyright 2016-2020 Intel Corporation.
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

#include <pthread.h>

#include "dfuse_common.h"
#include "dfuse.h"

/* Async progress thread.
 *
 * This thread is started at launch time with an event queue and blocks
 * on a semaphore until a asynchronous event is created, at which point
 * the thread wakes up and busy polls in daos_eq_poll() until it's complete.
 */
static void *
dfuse_progress_thread(void *arg)
{
	struct dfuse_projection_info *fs_handle = arg;
	int rc;
	daos_event_t *dev;
	struct dfuse_event *ev;

	while (1) {

		errno = 0;
		rc = sem_wait(&fs_handle->dpi_sem);
		if (rc != 0) {
			rc = errno;

			if (rc == EINTR)
				continue;

			DFUSE_TRA_ERROR(fs_handle,
					"Error from sem_wait: %d", rc);
		}

		if (fs_handle->dpi_shutdown)
			return NULL;

		rc = daos_eq_poll(fs_handle->dpi_eq, 1,
				  DAOS_EQ_WAIT,
				1,
				&dev);

		if (rc == 1) {
			ev = container_of(dev, struct dfuse_event, de_ev);

			ev->de_complete_cb(ev);

			D_FREE(ev);
		}
	}
	return NULL;
}

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

	/* First check if it's the same container (dfs struct) */
	if (ir->ir_id.irid_dfs != ir_id->irid_dfs)
		return false;

	/* Then check if the both parts of the OID match */
	if (ir->ir_id.irid_oid.lo != ir_id->irid_oid.lo)
		return false;

	if (ir->ir_id.irid_oid.hi != ir_id->irid_oid.hi)
		return false;

	return true;
}

static uint32_t
ir_rec_hash(struct d_hash_table *htable, d_list_t *rlink)
{
	const struct dfuse_inode_record		*ir;

	ir = container_of(rlink, struct dfuse_inode_record, ir_htl);

	return (uint32_t)ir->ir_id.irid_oid.hi;
}

static void
ir_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfuse_inode_record *ir;

	ir = container_of(rlink, struct dfuse_inode_record, ir_htl);

	D_FREE(ir);
}

/* Inode entry hash table operations */

static uint32_t
ih_key_hash(struct d_hash_table *htable, const void *key,
	    unsigned int ksize)
{
	const ino_t *ino = key;

	return (uint32_t)(*ino);
}

static bool
ih_key_cmp(struct d_hash_table *htable, d_list_t *rlink,
	   const void *key, unsigned int ksize)
{
	const struct dfuse_inode_entry	*ie;
	const ino_t			*ino = key;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	return *ino == ie->ie_stat.st_ino;
}

static uint32_t
ih_rec_hash(struct d_hash_table *htable, d_list_t *rlink)
{
	const struct dfuse_inode_entry	*ie;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	return (uint32_t)ie->ie_stat.st_ino;
}

static void
ih_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfuse_inode_entry	*ie;
	uint				oldref;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);
	oldref = atomic_fetch_add_relaxed(&ie->ie_ref, 1);
	DFUSE_TRA_DEBUG(ie, "addref to %u", oldref + 1);
}

static bool
ih_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfuse_inode_entry	*ie;
	uint				oldref;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);
	oldref = atomic_fetch_sub_relaxed(&ie->ie_ref, 1);
	DFUSE_TRA_DEBUG(ie, "decref to %u", oldref - 1);
	return oldref == 1;
}

static int
ih_ndecref(struct d_hash_table *htable, d_list_t *rlink, int count)
{
	struct dfuse_inode_entry	*ie;
	uint				oldref = 0;
	uint				newref = 0;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	do {
		oldref = atomic_load_relaxed(&ie->ie_ref);

		if (oldref < count)
			break;

		newref = oldref - count;

	} while (!atomic_compare_exchange(&ie->ie_ref, oldref, newref));

	if (oldref < count) {
		DFUSE_TRA_ERROR(ie, "unable to decref %u from %u",
				count, oldref);
		return -DER_INVAL;
	}

	DFUSE_TRA_DEBUG(ie, "decref of %u to %u", count, newref);
	if (newref == 0)
		return 1;
	return 0;
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
	.hop_key_cmp		= ih_key_cmp,
	.hop_key_hash		= ih_key_hash,
	.hop_rec_hash		= ih_rec_hash,
	.hop_rec_addref		= ih_addref,
	.hop_rec_decref		= ih_decref,
	.hop_rec_ndecref	= ih_ndecref,
	.hop_rec_free		= ih_free,
};

static d_hash_table_ops_t ir_hops = {
	.hop_key_cmp		= ir_key_cmp,
	.hop_key_hash		= ir_key_hash,
	.hop_rec_hash		= ir_rec_hash,
	.hop_rec_free		= ir_free,

};

void
dfuse_dfs_init(struct dfuse_dfs *dfs, struct dfuse_dfs *parent)
{
	D_MUTEX_INIT(&dfs->dfs_read_mutex, NULL);

	if (!parent)
		return;

	dfs->dfs_attr_timeout = parent->dfs_attr_timeout;
}

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
		return -DER_NOMEM;

	DFUSE_TRA_ROOT(fs_handle, "fs_handle");

	fs_handle->dpi_info = dfuse_info;

	/* Max read and max write are handled differently because of the way
	 * the interception library handles reads vs writes
	 */
	fs_handle->dpi_max_read = 1024 * 1024 * 4;
	fs_handle->dpi_max_write = 1024 * 1024;

	rc = d_hash_table_create_inplace(D_HASH_FT_RWLOCK | D_HASH_FT_EPHEMERAL,
					 3, fs_handle, &ie_hops,
					 &fs_handle->dpi_iet);
	if (rc != 0)
		D_GOTO(err, 0);

	rc = d_hash_table_create_inplace(D_HASH_FT_RWLOCK, 3, fs_handle,
					 &ir_hops, &fs_handle->dpi_irt);
	if (rc != 0)
		D_GOTO(err_iet, 0);

	atomic_store_relaxed(&fs_handle->dpi_ino_next, 2);

	args.argc = 4;

	/* These allocations are freed later by libfuse so do not use the
	 * standard allocation macros
	 */
	args.allocated = 1;
	args.argv = calloc(sizeof(*args.argv), args.argc);
	if (!args.argv)
		D_GOTO(err_irt, rc = -DER_NOMEM);

	args.argv[0] = strndup("", 1);
	if (!args.argv[0])
		D_GOTO(err_irt, rc = -DER_NOMEM);

	args.argv[1] = strndup("-ofsname=dfuse", 32);
	if (!args.argv[1])
		D_GOTO(err_irt, rc = -DER_NOMEM);

	args.argv[2] = strndup("-osubtype=daos", 32);
	if (!args.argv[2])
		D_GOTO(err_irt, rc = -DER_NOMEM);

	rc = asprintf(&args.argv[3], "-omax_read=%u", fs_handle->dpi_max_read);
	if (rc < 0 || !args.argv[3])
		D_GOTO(err_irt, rc = -DER_NOMEM);

	fuse_ops = dfuse_get_fuse_ops();
	if (!fuse_ops)
		D_GOTO(err_irt, rc = -DER_NOMEM);

	/* Create the root inode and insert into table */
	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(err_irt, rc = -DER_NOMEM);

	DFUSE_TRA_UP(ie, fs_handle, "root_inode");

	ie->ie_root = true;

	ie->ie_dfs = dfs;
	ie->ie_parent = 1;
	atomic_store_relaxed(&ie->ie_ref, 1);
	ie->ie_stat.st_ino = 1;
	ie->ie_stat.st_mode = 0700 | S_IFDIR;
	dfs->dfs_root = ie->ie_stat.st_ino;

	if (dfs->dfs_ops == &dfuse_dfs_ops) {
		rc = dfs_lookup(dfs->dfs_ns, "/", O_RDONLY, &ie->ie_obj,
				NULL, NULL);
		if (rc) {
			DFUSE_TRA_ERROR(ie, "dfs_lookup() failed: (%s)",
					strerror(rc));
			D_GOTO(err_irt, rc = daos_errno2der(rc));
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
		D_GOTO(err_ie_remove, 0);
	}

	rc = daos_eq_create(&fs_handle->dpi_eq);
	if (rc != -DER_SUCCESS)
		D_GOTO(err, 0);

	rc = sem_init(&fs_handle->dpi_sem, 0, 0);
	if (rc != 0)
		D_GOTO(err, 0);

	fs_handle->dpi_shutdown = false;
	rc = pthread_create(&fs_handle->dpi_thread, NULL,
			    dfuse_progress_thread, fs_handle);
	if (rc != 0)
		D_GOTO(err, 0);

	pthread_setname_np(fs_handle->dpi_thread, "dfuse_progress");

	if (!dfuse_launch_fuse(dfuse_info, fuse_ops, &args, fs_handle)) {
		DFUSE_TRA_ERROR(fs_handle, "Unable to register FUSE fs");
		D_GOTO(err_ie_remove, rc = -DER_INVAL);
	}

	D_FREE(fuse_ops);

	return -DER_SUCCESS;

err_ie_remove:
	d_hash_rec_delete_at(&fs_handle->dpi_iet, &ie->ie_htl);
err_irt:
	d_hash_table_destroy_inplace(&fs_handle->dpi_irt, false);
err_iet:
	d_hash_table_destroy_inplace(&fs_handle->dpi_iet, false);
err:
	DFUSE_TRA_ERROR(fs_handle, "Failed to start dfuse, rc: %d", rc);
	D_FREE(fuse_ops);
	D_FREE(ie);
	D_FREE(fs_handle);
	return rc;
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
	if (rc != 0 && rc != -EBADF)
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


	fs_handle->dpi_shutdown = true;
	sem_post(&fs_handle->dpi_sem);

	pthread_join(fs_handle->dpi_thread, NULL);

	sem_destroy(&fs_handle->dpi_sem);

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

		ref = atomic_load_relaxed(&ie->ie_ref);

		DFUSE_TRA_DEBUG(ie, "Dropping %d", ref);

		refs += ref;
		d_hash_rec_ndecref(&fs_handle->dpi_iet, ref, rlink);
		handles++;
	} while (rlink);

	if (handles && rc != -DER_SUCCESS && rc != -DER_NO_HDL) {
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

	DFUSE_TRA_INFO(fs_handle, "Draining inode record table");
	do {
		struct dfuse_inode_record *ir;

		rlink = d_hash_rec_first(&fs_handle->dpi_irt);

		if (!rlink)
			break;

		ir = container_of(rlink, struct dfuse_inode_record, ir_htl);

		d_hash_rec_delete_at(&fs_handle->dpi_irt, rlink);

		D_FREE(ir);
	} while (rlink);

	rc = d_hash_table_destroy_inplace(&fs_handle->dpi_irt, false);
	if (rc) {
		DFUSE_TRA_WARNING(fs_handle, "Failed to close inode handles");
		rcp = EINVAL;
	}

	return rcp;
}
