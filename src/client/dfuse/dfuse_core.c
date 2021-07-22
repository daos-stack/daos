/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

#if 0
/* Parse a string to a time, used for reading container attributes info
 * timeouts.
 */
static int
dfuse_parse_time(char *buff, size_t len, unsigned int *_out)
{
	int		matched;
	unsigned int	out = 0;
	int		count0 = 0;
	int		count1 = 0;
	char		c = '\0';

	matched = sscanf(buff, "%u%n%c%n", &out, &count0, &c, &count1);

	if (matched == 0)
		return EINVAL;

	if (matched == 1 && len != count0)
		return EINVAL;

	if (matched == 2 && len != count1)
		return EINVAL;

	if (matched == 2) {
		if (c == 'm' || c == 'M')
			out *= 60;
		if (c == 's' || c == 'S')
			true;
		else
			return EINVAL;
	}

	*_out = out;
	return 0;
}
#endif

/* Inode entry hash table operations */

/* Shrink a 64 bit value into 32 bits to avoid hash collisions */
static uint32_t
ih_key_hash(struct d_hash_table *htable, const void *key,
	    unsigned int ksize)
{
	const ino_t *_ino = key;
	ino_t ino = *_ino;
	uint32_t hash = ino ^ (ino >> 32);

	return hash;
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

	return ih_key_hash(NULL,
			   &ie->ie_stat.st_ino,
			   sizeof(ie->ie_stat.st_ino));
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

	DFUSE_TRA_DEBUG(ie, "parent %#lx", ie->ie_parent);
	dfuse_ie_close(fs_handle, ie);
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

static uint32_t
ph_key_hash(struct d_hash_table *htable, const void *key,
	    unsigned int ksize)
{
	return *((const uint32_t *)key);
}

static uint32_t
ph_rec_hash(struct d_hash_table *htable, d_list_t *link)
{
	struct dfuse_pool *dfp;

	dfp = container_of(link, struct dfuse_pool, dfp_entry);

	return ph_key_hash(NULL, &dfp->dfp_pool, sizeof(dfp->dfp_pool));
}

static bool
ph_key_cmp(struct d_hash_table *htable, d_list_t *link,
	   const void *key, unsigned int ksize)
{
	struct dfuse_pool *dfp;

	dfp = container_of(link, struct dfuse_pool, dfp_entry);
	return uuid_compare(dfp->dfp_pool, key) == 0;
}

static void
ph_addref(struct d_hash_table *htable, d_list_t *link)
{
	struct dfuse_pool	 *dfp;
	uint			 oldref;

	dfp = container_of(link, struct dfuse_pool, dfp_entry);
	oldref = atomic_fetch_add_relaxed(&dfp->dfp_ref, 1);
	DFUSE_TRA_DEBUG(dfp, "addref to %u", oldref + 1);
}

static bool
ph_decref(struct d_hash_table *htable, d_list_t *link)
{
	struct dfuse_pool	*dfp;
	uint			oldref;

	dfp = container_of(link, struct dfuse_pool, dfp_entry);
	oldref = atomic_fetch_sub_relaxed(&dfp->dfp_ref, 1);
	DFUSE_TRA_DEBUG(dfp, "decref to %u", oldref - 1);
	return oldref == 1;
}

static void
_ph_free(struct dfuse_pool *dfp)
{
	int rc;

	if (daos_handle_is_valid(dfp->dfp_poh)) {
		rc = daos_pool_disconnect(dfp->dfp_poh, NULL);
		if (rc != -DER_SUCCESS)
			DFUSE_TRA_ERROR(dfp,
					"daos_pool_disconnect() failed: "DF_RC,
					DP_RC(rc));
	}

	rc = d_hash_table_destroy_inplace(&dfp->dfp_cont_table, false);
	if (rc != -DER_SUCCESS)
		DFUSE_TRA_ERROR(dfp, "Failed to destroy pool hash table: "DF_RC,
				DP_RC(rc));

	D_FREE(dfp);
}

static void
ph_free(struct d_hash_table *htable, d_list_t *link)
{
	_ph_free(container_of(link, struct dfuse_pool, dfp_entry));
}

static d_hash_table_ops_t pool_hops = {
	.hop_key_cmp		= ph_key_cmp,
	.hop_key_hash		= ph_key_hash,
	.hop_rec_hash		= ph_rec_hash,
	.hop_rec_addref		= ph_addref,
	.hop_rec_decref		= ph_decref,
	.hop_rec_free		= ph_free,
};

static uint32_t
ch_key_hash(struct d_hash_table *htable, const void *key,
	    unsigned int ksize)
{
	return *((const uint32_t *)key);
}

static uint32_t
ch_rec_hash(struct d_hash_table *htable, d_list_t *link)
{
	struct dfuse_cont *dfc;

	dfc = container_of(link, struct dfuse_cont, dfs_entry);

	return ch_key_hash(NULL, &dfc->dfs_cont, sizeof(dfc->dfs_cont));
}

static bool
ch_key_cmp(struct d_hash_table *htable, d_list_t *link,
	   const void *key, unsigned int ksize)
{
	struct dfuse_cont *dfc;

	dfc = container_of(link, struct dfuse_cont, dfs_entry);
	return uuid_compare(dfc->dfs_cont, key) == 0;
}

static void
ch_addref(struct d_hash_table *htable, d_list_t *link)
{
	struct dfuse_cont	*dfc;
	uint			oldref;

	dfc = container_of(link, struct dfuse_cont, dfs_entry);
	oldref = atomic_fetch_add_relaxed(&dfc->dfs_ref, 1);
	DFUSE_TRA_DEBUG(dfc, "addref to %u", oldref + 1);
}

static bool
ch_decref(struct d_hash_table *htable, d_list_t *link)
{
	struct dfuse_cont	*dfc;
	uint			oldref;

	dfc = container_of(link, struct dfuse_cont, dfs_entry);
	oldref = atomic_fetch_sub_relaxed(&dfc->dfs_ref, 1);
	DFUSE_TRA_DEBUG(dfc, "decref to %u", oldref - 1);
	return oldref == 1;
}

static void
_ch_free(struct dfuse_projection_info *fs_handle, struct dfuse_cont *dfc)
{
	D_MUTEX_DESTROY(&dfc->dfs_read_mutex);

	if (daos_handle_is_valid(dfc->dfs_coh)) {
		int rc;

		rc = dfs_umount(dfc->dfs_ns);
		if (rc != 0)
			DFUSE_TRA_ERROR(dfc, "dfs_umount() failed, "DF_RC,
					DP_RC(rc));

		rc = daos_cont_close(dfc->dfs_coh, NULL);
		if (rc != 0)
			DFUSE_TRA_ERROR(dfc, "dfs_cont_close() failed, "DF_RC,
					DP_RC(rc));
	}

	d_hash_rec_decref(&fs_handle->dpi_pool_table, &dfc->dfs_dfp->dfp_entry);

	D_FREE(dfc);
}

static void
ch_free(struct d_hash_table *htable, d_list_t *link)
{
	_ch_free(htable->ht_priv,
		 container_of(link, struct dfuse_cont, dfs_entry));
}

d_hash_table_ops_t cont_hops = {
	.hop_key_cmp		= ch_key_cmp,
	.hop_key_hash		= ch_key_hash,
	.hop_rec_hash		= ch_rec_hash,
	.hop_rec_addref		= ch_addref,
	.hop_rec_decref		= ch_decref,
	.hop_rec_free		= ch_free,
};

/* Return a pool connection by label.
 *
 * Only used for command line parsing, so does not check for existing pools
 *
 * Return code is a system errno.
 */
int
dfuse_pool_connect_by_label(struct dfuse_projection_info *fs_handle,
			const char *label,
			struct dfuse_pool **_dfp)
{
	struct dfuse_pool	*dfp;
	daos_pool_info_t        p_info = {};
	d_list_t		*rlink;
	int			rc;

	D_ALLOC_PTR(dfp);
	if (dfp == NULL)
		D_GOTO(err, rc = ENOMEM);

	atomic_store_relaxed(&dfp->dfp_ref, 1);

	DFUSE_TRA_UP(dfp, fs_handle, "dfp");

	rc = daos_pool_connect(label, fs_handle->dpi_info->di_group,
			       DAOS_PC_RW, &dfp->dfp_poh, &p_info, NULL);
	if (rc) {
		if (rc == -DER_NO_PERM)
			DFUSE_TRA_INFO(dfp,
				"daos_pool_connect() failed, "
				DF_RC, DP_RC(rc));
		else
			DFUSE_TRA_ERROR(dfp,
					"daos_pool_connect() failed, "
					DF_RC, DP_RC(rc));
		D_GOTO(err_free, rc = daos_der2errno(rc));
	}

	uuid_copy(dfp->dfp_pool, p_info.pi_uuid);

	rc = d_hash_table_create_inplace(D_HASH_FT_LRU | D_HASH_FT_EPHEMERAL,
					 3, fs_handle, &cont_hops,
					 &dfp->dfp_cont_table);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(dfp, "Failed to create hash table: "DF_RC,
				DP_RC(rc));
		D_GOTO(err_disconnect, rc = daos_der2errno(rc));
	}

	rlink = d_hash_rec_find_insert(&fs_handle->dpi_pool_table,
				       &dfp->dfp_pool, sizeof(dfp->dfp_pool),
				       &dfp->dfp_entry);

	if (rlink != &dfp->dfp_entry) {
		DFUSE_TRA_DEBUG(dfp, "Found existing pool, reusing");
		_ph_free(dfp);
		dfp = container_of(rlink, struct dfuse_pool, dfp_entry);
	}

	DFUSE_TRA_DEBUG(dfp, "Returning dfp for "DF_UUID,
			DP_UUID(dfp->dfp_pool));

	*_dfp = dfp;
	return rc;
err_disconnect:
	daos_pool_disconnect(dfp->dfp_poh, NULL);
err_free:
	D_FREE(dfp);
err:
	return rc;
}

/* Return a pool connection by uuid.
 *
 * Re-use an existing connection if possible, otherwise open new connection.
 *
 * If successsfull with pass out a pool pointer, with one reference held.
 *
 * Return code is a system errno.
 */
int
dfuse_pool_connect(struct dfuse_projection_info *fs_handle, uuid_t *pool,
		   struct dfuse_pool **_dfp)
{
	struct dfuse_pool	*dfp;
	d_list_t		*rlink;
	int			rc;

	rlink = d_hash_rec_find(&fs_handle->dpi_pool_table,
				pool, sizeof(*pool));
	if (rlink) {
		*_dfp = container_of(rlink, struct dfuse_pool, dfp_entry);
		return 0;
	}

	D_ALLOC_PTR(dfp);
	if (dfp == NULL)
		D_GOTO(err, rc = ENOMEM);

	atomic_store_relaxed(&dfp->dfp_ref, 1);

	DFUSE_TRA_UP(dfp, fs_handle, "dfp");

	DFUSE_TRA_DEBUG(dfp, "New pool "DF_UUIDF,
			DP_UUID(pool));

	if (uuid_is_null(*pool) == 0) {
		uuid_copy(dfp->dfp_pool, *pool);

		rc = daos_pool_connect(dfp->dfp_pool,
				       fs_handle->dpi_info->di_group,
				       DAOS_PC_RW,
				       &dfp->dfp_poh, NULL, NULL);
		if (rc) {
			if (rc == -DER_NO_PERM)
				DFUSE_TRA_INFO(dfp,
					       "daos_pool_connect() failed, "
					       DF_RC, DP_RC(rc));
			else
				DFUSE_TRA_ERROR(dfp,
						"daos_pool_connect() failed, "
						DF_RC, DP_RC(rc));
			D_GOTO(err_free, rc = daos_der2errno(rc));
		}
	}

	rc = d_hash_table_create_inplace(D_HASH_FT_LRU | D_HASH_FT_EPHEMERAL,
					 3, fs_handle, &cont_hops,
					 &dfp->dfp_cont_table);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(dfp, "Failed to create hash table: "DF_RC,
				DP_RC(rc));
		D_GOTO(err_disconnect, rc = daos_der2errno(rc));
	}

	rlink = d_hash_rec_find_insert(&fs_handle->dpi_pool_table,
				       &dfp->dfp_pool, sizeof(dfp->dfp_pool),
				       &dfp->dfp_entry);

	if (rlink != &dfp->dfp_entry) {
		DFUSE_TRA_DEBUG(dfp, "Found existing pool, reusing");
		_ph_free(dfp);
		dfp = container_of(rlink, struct dfuse_pool, dfp_entry);
	}

	DFUSE_TRA_DEBUG(dfp, "Returning dfp for "DF_UUID,
			DP_UUID(dfp->dfp_pool));

	*_dfp = dfp;
	return rc;
err_disconnect:
	if (daos_handle_is_valid(dfp->dfp_poh))
		daos_pool_disconnect(dfp->dfp_poh, NULL);
err_free:
	D_FREE(dfp);
err:
	return rc;
}

#define ATTR_COUNT 6

char const *const
cont_attr_names[ATTR_COUNT] = {"dfuse-attr-time",
			       "dfuse-dentry-time",
			       "dfuse-dentry-dir-time",
			       "dfuse-ndentry-time",
			       "dfuse-data-cache",
			       "dfuse-direct-io-disable"};

#define ATTR_TIME_INDEX		0
#define ATTR_DENTRY_INDEX	1
#define ATTR_DENTRY_DIR_INDEX	2
#define ATTR_NDENTRY_INDEX	3
#define ATTR_DATA_CACHE_INDEX	4
#define ATTR_DIRECT_IO_DISABLE_INDEX	5

/* Attribute values are of the form "120M", so the buffer does not need to be
 * large.
 */
#define ATTR_VALUE_LEN 128

/* Setup caching attributes for a container.
 *
 * These are read from pool attributes, or can be overwritten on the command
 * line, but only for the root dfc in that case, so to use caching with
 * multiple containers it needs to be set via attributes.
 *
 * Returns a  error code on error, or ENODATA if no attributes are
 * set.
 */
static int
dfuse_cont_get_cache(struct dfuse_cont *dfc)
{
#if 1
	/**
	 * XXX use default cache value until DAOS-7671 is fixed
	 */
	dfuse_set_default_cont_cache_values(dfc);
	return 0;
#else
	size_t		size;
	char		*buff;
	int		rc;
	int		i;
	unsigned int	value;
	bool		have_dentry = false;
	bool		have_dentry_dir = false;
	bool		have_attr = false;
	bool		have_dio = false;
	bool		have_cache_off = false;

	D_ALLOC(buff, ATTR_VALUE_LEN);
	if (buff == NULL)
		return ENOMEM;

	for (i = 0; i < ATTR_COUNT; i++) {
		size = ATTR_VALUE_LEN - 1;

		rc = daos_cont_get_attr(dfc->dfs_coh, 1, &cont_attr_names[i],
					(void * const*)&buff,
					&size, NULL);
		if (rc == -DER_NONEXIST) {
			continue;
		} else if (rc != -DER_SUCCESS) {
			DFUSE_TRA_WARNING(dfc, "Failed to load value for '%s' "
					  DF_RC, cont_attr_names[i], DP_RC(rc));
			D_GOTO(out, rc = daos_der2errno(rc));
		}
		have_attr = true;

		if (i == ATTR_DATA_CACHE_INDEX) {
			if (strncmp(buff, "on", size) == 0) {
				dfc->dfc_data_caching = true;
			} else if (strncmp(buff, "off", size) == 0) {
				have_cache_off = true;
				dfc->dfc_data_caching = false;
			} else {
				DFUSE_TRA_WARNING(dfc,
						  "Failed to parse '%s' for '%s'",
						  buff, cont_attr_names[i]);
				dfc->dfc_data_caching = false;
			}
			continue;
		}
		if (i == ATTR_DIRECT_IO_DISABLE_INDEX) {
			if (strncmp(buff, "on", size) == 0) {
				have_dio = true;
				dfc->dfc_direct_io_disable = true;
			} else if (strncmp(buff, "off", size) == 0) {
				dfc->dfc_direct_io_disable = false;
			} else {
				DFUSE_TRA_WARNING(dfc,
						  "Failed to parse '%s' for '%s'",
						  buff, cont_attr_names[i]);
				dfc->dfc_data_caching = false;
			}
			continue;
		}

		/* Ensure the character after the fetch string is zero, in case
		 * of non-null terminated strings.
		 */
		buff[size] = '\0';

		rc = dfuse_parse_time(buff, size, &value);
		if (rc != 0) {
			DFUSE_TRA_WARNING(dfc, "Failed to parse '%s' for '%s'",
					  buff, cont_attr_names[i]);
			continue;
		}
		DFUSE_TRA_INFO(dfc, "setting '%s' is %u",
			       cont_attr_names[i], value);
		if (i == ATTR_TIME_INDEX) {
			dfc->dfc_attr_timeout = value;
		} else if (i == ATTR_DENTRY_INDEX) {
			have_dentry = true;
			dfc->dfc_dentry_timeout = value;
		} else if (i == ATTR_DENTRY_DIR_INDEX) {
			have_dentry_dir = true;
			dfc->dfc_dentry_dir_timeout = value;
		} else if (i == ATTR_NDENTRY_INDEX) {
			dfc->dfc_ndentry_timeout = value;
		}
	}
	/* Check if dfuse-direct-io-disable is set to on but
	 * dfuse-data-cache is set to off.  This combination
	 * does not make sense, so warn in this case and set
	 * caching to on.
	 */
	if (have_dio) {
		if (have_cache_off)
			DFUSE_TRA_WARNING(dfc, "Caching enabled because of %s",
					  cont_attr_names[ATTR_DIRECT_IO_DISABLE_INDEX]);
		dfc->dfc_data_caching = true;
	}

	if (have_dentry && !have_dentry_dir)
		dfc->dfc_dentry_dir_timeout = dfc->dfc_dentry_timeout;
	rc = 0;
	if (!have_attr)
		rc = ENODATA;
out:
	D_FREE(buff);
	return rc;
#endif
}

/* Set default cache values for a container.
 *
 * These are used by default if the container does not set any attributes
 * itself, and there are no command-line settings to overrule them.
 *
 * It is intended to improve performance and usability on interactive
 * nodes without preventing use across nodes, as such data cache is enabled
 * and metadata cache is on but with relatively short timeouts.
 *
 * One second is used for attributes, dentries and negative dentries, however
 * dentries which represent directories and are therefore referenced much
 * more often during path-walk activities are set to five seconds.
 */
void
dfuse_set_default_cont_cache_values(struct dfuse_cont *dfc)
{
	dfc->dfc_attr_timeout = 1;
	dfc->dfc_dentry_timeout = 1;
	dfc->dfc_dentry_dir_timeout = 5;
	dfc->dfc_ndentry_timeout = 1;
	dfc->dfc_data_caching = true;
	dfc->dfc_direct_io_disable = false;
}

/* Open a cont by label.
 *
 * Only used for command line labels, not for paths in dfuse.
 */
int
dfuse_cont_open_by_label(struct dfuse_projection_info *fs_handle,
			struct dfuse_pool *dfp,
			const char *label,
			struct dfuse_cont **_dfc)
{
	struct dfuse_cont *dfc;
	daos_cont_info_t c_info = {};
	int rc;

	D_ALLOC_PTR(dfc);
	if (dfc == NULL)
		D_GOTO(err_free, rc = ENOMEM);

	DFUSE_TRA_UP(dfc, dfp, "dfc");

	rc = daos_cont_open(dfp->dfp_poh, label, DAOS_COO_RW, &dfc->dfs_coh,
			    &c_info, NULL);
	if (rc == -DER_NONEXIST) {
		DFUSE_TRA_INFO(dfc,
			"daos_cont_open() failed: "
			DF_RC, DP_RC(rc));
		D_GOTO(err_free, rc = daos_der2errno(rc));
	} else if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(dfc,
				"daos_cont_open() failed: "
				DF_RC, DP_RC(rc));
		D_GOTO(err_free, rc = daos_der2errno(rc));
	}

	uuid_copy(dfc->dfs_cont, c_info.ci_uuid);

	rc = dfs_mount(dfp->dfp_poh, dfc->dfs_coh, O_RDWR, &dfc->dfs_ns);
	if (rc) {
		DFUSE_TRA_ERROR(dfc,
				"dfs_mount() failed: (%s)",
				strerror(rc));
		D_GOTO(err_close, rc);
	}

	if (fs_handle->dpi_info->di_caching) {
		rc = dfuse_cont_get_cache(dfc);
		if (rc == ENODATA) {
			/* If there is no container specific
			 * attributes then use defaults
			 */
			DFUSE_TRA_INFO(dfc,
				"Using default caching values");
			dfuse_set_default_cont_cache_values(dfc);
			rc = 0;
		} else if (rc != 0) {
			D_GOTO(err_close, rc);
		}
	} else {
		DFUSE_TRA_INFO(dfc,
			"Caching disabled");
	}

	rc = dfuse_cont_open(fs_handle, dfp, &c_info.ci_uuid, &dfc);
	if (rc) {
		D_FREE(dfc);
		return rc;
	}
	*_dfc = dfc;
	return 0;

err_close:
	daos_cont_close(dfc->dfs_coh, NULL);
err_free:
	D_FREE(dfc);
	return rc;
}

/*
 * Return a container connection by uuid.
 *
 * Re-use an existing connection if possible, otherwise open new connection
 * and setup dfs.
 *
 * In the case of a container which has been created by mkdir _dfs will be a
 * valid pointer, with dfs_ns and dfs_coh set already.  Failure in this case
 * will result in the memory being freed.
 *
 * If successful will pass out a dfs pointer, with one reference held.
 *
 * Return code is a system errno.
 */
int
dfuse_cont_open(struct dfuse_projection_info *fs_handle, struct dfuse_pool *dfp,
		uuid_t *cont, struct dfuse_cont **_dfc)
{
	struct dfuse_cont	*dfc = NULL;
	d_list_t		*rlink;
	int			rc = -DER_SUCCESS;

	if (*_dfc) {
		dfc = *_dfc;
	} else {
		/* Check if there is already a open container connection, and
		 * just use it if there is.  The rec_find() will take the
		 * additional reference for us.
		 */
		rlink = d_hash_rec_find(&dfp->dfp_cont_table,
					cont, sizeof(*cont));
		if (rlink) {
			*_dfc = container_of(rlink, struct dfuse_cont,
					     dfs_entry);
			return 0;
		}

		D_ALLOC_PTR(dfc);
		if (!dfc)
			D_GOTO(err, rc = ENOMEM);

		DFUSE_TRA_UP(dfc, dfp, "dfc");
	}

	/* No existing container found, so setup dfs and connect to one */

	atomic_store_relaxed(&dfc->dfs_ref, 1);

	DFUSE_TRA_DEBUG(dfp, "New cont "DF_UUIDF" in pool "DF_UUIDF,
			DP_UUID(cont), DP_UUID(dfp->dfp_pool));

	dfc->dfs_dfp = dfp;

	/* Allow for uuid to be NULL, in which case this represents a pool */
	if (uuid_is_null(*cont)) {
		dfc->dfs_ops = &dfuse_cont_ops;

		/* Turn on some caching of metadata, otherwise container
		 * operations will be very frequent
		 */
		dfc->dfc_attr_timeout = 5;
		dfc->dfc_dentry_dir_timeout = 5;
		dfc->dfc_ndentry_timeout = 5;

	} else if (*_dfc == NULL) {
		dfc->dfs_ops = &dfuse_dfs_ops;
		uuid_copy(dfc->dfs_cont, *cont);
		rc = daos_cont_open(dfp->dfp_poh, dfc->dfs_cont,
				    DAOS_COO_RW, &dfc->dfs_coh,
				    NULL, NULL);
		if (rc == -DER_NONEXIST) {
			DFUSE_TRA_INFO(dfc, "daos_cont_open() failed: "DF_RC,
				       DP_RC(rc));
			D_GOTO(err_free, rc = daos_der2errno(rc));
		} else if (rc != -DER_SUCCESS) {
			DFUSE_TRA_ERROR(dfc, "daos_cont_open() failed: "
					DF_RC, DP_RC(rc));
			D_GOTO(err_free, rc = daos_der2errno(rc));
		}

		rc = dfs_mount(dfp->dfp_poh, dfc->dfs_coh, O_RDWR,
			       &dfc->dfs_ns);
		if (rc) {
			DFUSE_TRA_ERROR(dfc,
					"dfs_mount() failed: (%s)",
					strerror(rc));
			D_GOTO(err_close, rc);
		}

		if (fs_handle->dpi_info->di_caching) {
			rc = dfuse_cont_get_cache(dfc);
			if (rc == ENODATA) {
				/* If there is no container specific
				 * attributes then use defaults
				 */
				DFUSE_TRA_INFO(dfc,
					"Using default caching values");
				dfuse_set_default_cont_cache_values(dfc);
				rc = 0;
			} else if (rc != 0) {
				D_GOTO(err_close, rc);
			}
		} else {
			DFUSE_TRA_INFO(dfc,
				"Caching disabled");
		}
	} else {
		/* This is either a container where a label is set on the
		 * command line, or one created through mkdir, in either case
		 * the container will be mounted, and caching etc already
		 * setup.
		 */
		dfc->dfs_ops = &dfuse_dfs_ops;
	}

	dfc->dfs_ino = atomic_fetch_add_relaxed(&fs_handle->dpi_ino_next, 1);
	D_MUTEX_INIT(&dfc->dfs_read_mutex, NULL);

	/* Take a reference on the pool */
	d_hash_rec_addref(&fs_handle->dpi_pool_table, &dfp->dfp_entry);

	/* Finally insert into the hash table.  This may return an existing
	 * container if there is a race to insert, so if that happens
	 * just use that one.
	 */
	rlink = d_hash_rec_find_insert(&dfp->dfp_cont_table,
				       &dfc->dfs_cont, sizeof(dfc->dfs_cont),
				       &dfc->dfs_entry);

	if (rlink != &dfc->dfs_entry) {
		DFUSE_TRA_DEBUG(dfp, "Found existing container, reusing");

		_ch_free(fs_handle, dfc);

		dfc = container_of(rlink, struct dfuse_cont, dfs_entry);
	}

	DFUSE_TRA_DEBUG(dfc, "Returning dfs for "DF_UUID" ref %d",
			DP_UUID(dfc->dfs_cont), dfc->dfs_ref);

	*_dfc = dfc;

	return rc;

err_close:
	daos_cont_close(dfc->dfs_coh, NULL);
err_free:
	D_FREE(dfc);
err:
	return rc;
}

int
dfuse_fs_init(struct dfuse_info *dfuse_info,
	      struct dfuse_projection_info **_fsh)
{
	struct dfuse_projection_info	*fs_handle;
	int				rc;

	D_ALLOC_PTR(fs_handle);
	if (!fs_handle)
		return -DER_NOMEM;

	DFUSE_TRA_UP(fs_handle, dfuse_info, "fs_handle");

	fs_handle->dpi_info = dfuse_info;

	rc = d_hash_table_create_inplace(D_HASH_FT_LRU | D_HASH_FT_EPHEMERAL,
					 3, fs_handle, &pool_hops,
					 &fs_handle->dpi_pool_table);
	if (rc != 0)
		D_GOTO(err, 0);

	rc = d_hash_table_create_inplace(D_HASH_FT_LRU | D_HASH_FT_EPHEMERAL,
					 5, fs_handle, &ie_hops,
					 &fs_handle->dpi_iet);
	if (rc != 0)
		D_GOTO(err_pt, 0);

	atomic_store_relaxed(&fs_handle->dpi_ino_next, 2);

	rc = daos_eq_create(&fs_handle->dpi_eq);
	if (rc != -DER_SUCCESS)
		D_GOTO(err_iht, 0);

	rc = sem_init(&fs_handle->dpi_sem, 0, 0);
	if (rc != 0)
		D_GOTO(err_eq, 0);

	fs_handle->dpi_shutdown = false;
	*_fsh = fs_handle;
	return rc;

err_eq:
	daos_eq_destroy(fs_handle->dpi_eq, DAOS_EQ_DESTROY_FORCE);
err_iht:
	d_hash_table_destroy_inplace(&fs_handle->dpi_iet, false);
err_pt:
	d_hash_table_destroy_inplace(&fs_handle->dpi_pool_table, false);
err:
	D_FREE(fs_handle);
	return rc;
}

void
dfuse_ie_close(struct dfuse_projection_info *fs_handle,
	       struct dfuse_inode_entry *ie)
{
	int	rc;
	int	ref = atomic_load_relaxed(&ie->ie_ref);

	DFUSE_TRA_DEBUG(ie,
			"closing, inode %#lx ref %u, name '%s', parent %#lx",
			ie->ie_stat.st_ino, ref, ie->ie_name, ie->ie_parent);

	D_ASSERT(ref == 0);

	if (ie->ie_obj) {
		rc = dfs_release(ie->ie_obj);
		if (rc) {
			DFUSE_TRA_ERROR(ie, "dfs_release() failed: (%s)",
					strerror(rc));
		}
	}

	if (ie->ie_root) {
		struct dfuse_cont	*dfc = ie->ie_dfs;
		struct dfuse_pool	*dfp = dfc->dfs_dfp;

		DFUSE_TRA_INFO(ie, "Closing poh %d coh %d",
			       daos_handle_is_valid(dfp->dfp_poh),
			       daos_handle_is_valid(dfc->dfs_coh));

		d_hash_rec_decref(&dfp->dfp_cont_table, &dfc->dfs_entry);
	}

	D_FREE(ie);
}

int
dfuse_start(struct dfuse_projection_info *fs_handle,
	    struct dfuse_cont *dfs)
{
	struct fuse_args		args = {0};
	struct fuse_lowlevel_ops	*fuse_ops = NULL;
	struct dfuse_inode_entry	*ie = NULL;
	int				rc;

	args.argc = 4;

	/* These allocations are freed later by libfuse so do not use the
	 * standard allocation macros
	 */
	args.allocated = 1;
	args.argv = calloc(sizeof(*args.argv), args.argc);
	if (!args.argv)
		D_GOTO(err, rc = -DER_NOMEM);

	args.argv[0] = strdup("");
	if (!args.argv[0])
		D_GOTO(err, rc = -DER_NOMEM);

	args.argv[1] = strdup("-ofsname=dfuse");
	if (!args.argv[1])
		D_GOTO(err, rc = -DER_NOMEM);

	args.argv[2] = strdup("-osubtype=daos");
	if (!args.argv[2])
		D_GOTO(err, rc = -DER_NOMEM);

	args.argv[3] = strdup("-odefault_permissions");
	if (!args.argv[3])
		D_GOTO(err, rc = -DER_NOMEM);

	fuse_ops = dfuse_get_fuse_ops();
	if (!fuse_ops)
		D_GOTO(err, rc = -DER_NOMEM);

	/* Create the root inode and insert into table */
	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(err, rc = -DER_NOMEM);

	DFUSE_TRA_UP(ie, fs_handle, "root_inode");

	ie->ie_dfs = dfs;
	ie->ie_root = true;
	ie->ie_parent = 1;
	atomic_store_relaxed(&ie->ie_ref, 1);
	ie->ie_stat.st_ino = 1;
	ie->ie_stat.st_uid = geteuid();
	ie->ie_stat.st_gid = getegid();
	ie->ie_stat.st_mode = 0700 | S_IFDIR;
	dfs->dfs_ino = ie->ie_stat.st_ino;

	if (dfs->dfs_ops == &dfuse_dfs_ops) {
		rc = dfs_lookup(dfs->dfs_ns, "/", O_RDWR, &ie->ie_obj,
				NULL, NULL);
		if (rc) {
			DFUSE_TRA_ERROR(ie, "dfs_lookup() failed: (%s)",
					strerror(rc));
			D_GOTO(err, rc = daos_errno2der(rc));
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

	rc = pthread_create(&fs_handle->dpi_thread, NULL,
			    dfuse_progress_thread, fs_handle);
	if (rc != 0)
		D_GOTO(err_ie_remove, 0);

	pthread_setname_np(fs_handle->dpi_thread, "dfuse_progress");

	rc = dfuse_launch_fuse(fs_handle, fuse_ops, &args);
	D_FREE(fuse_ops);
	if (!rc) {
		(void)dfuse_fs_fini(fs_handle);
		DFUSE_TRA_ERROR(fs_handle, "Unable to register FUSE fs");
		return -DER_INVAL;
	}

	return -DER_SUCCESS;

err_ie_remove:
	d_hash_rec_delete_at(&fs_handle->dpi_iet, &ie->ie_htl);
err:
	DFUSE_TRA_ERROR(fs_handle, "Failed to start dfuse, rc: %d", rc);
	D_FREE(fuse_ops);
	D_FREE(ie);
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

	/* Do not evict root itself */
	if (ie->ie_stat.st_ino == 1)
		return 0;

	rc = fuse_lowlevel_notify_inval_entry(fs_handle->dpi_info->di_session,
					      ie->ie_parent,
					      ie->ie_name,
					      strlen(ie->ie_name));
	if (rc != 0 && rc != -EBADF)
		DFUSE_TRA_WARNING(ie,
				  "%#lx %#lx '%s': %d %s",
				  ie->ie_parent, ie->ie_stat.st_ino,
				  ie->ie_name, rc, strerror(-rc));
	else
		DFUSE_TRA_INFO(ie,
			       "%#lx %#lx '%s': %d %s",
			       ie->ie_parent, ie->ie_stat.st_ino,
			       ie->ie_name, rc, strerror(-rc));

	/* If the FUSE connection is dead then do not traverse further, it
	 * doesn't matter what gets returned here, as long as it's negative
	 */
	if (rc == -EBADF)
		return -DER_NO_HDL;

	return -DER_SUCCESS;
}

static int
dfuse_cont_close_cb(d_list_t *rlink, void *handle)
{
	struct dfuse_cont *dfc;

	dfc = container_of(rlink, struct dfuse_cont, dfs_entry);

	DFUSE_TRA_ERROR(dfc, "Failed to close cont ref %d "DF_UUID,
			dfc->dfs_ref, DP_UUID(dfc->dfs_cont));
	return 0;
}

/* Called during shutdown on still-open pools.  We've already stopped
 * taking requests from the kernel at this point and joined all the thread
 * as well as drained the inode table and dropped all references held there
 * so anything still held at this point represents a reference leak.
 *
 * As such log what we have as an error, and attempt to close/free everything.
 * the dfp itself will remain allocated as well as some hash table metadata.
 */
static int
dfuse_pool_close_cb(d_list_t *rlink, void *handle)
{
	struct dfuse_pool *dfp;
	int rc;

	dfp = container_of(rlink, struct dfuse_pool, dfp_entry);

	DFUSE_TRA_ERROR(dfp, "Failed to close pool ref %d "DF_UUID,
			dfp->dfp_ref, DP_UUID(dfp->dfp_pool));

	d_hash_table_traverse(&dfp->dfp_cont_table,
			      dfuse_cont_close_cb, NULL);

	rc = d_hash_table_destroy_inplace(&dfp->dfp_cont_table, false);
	if (rc != -DER_SUCCESS)
		DFUSE_TRA_ERROR(dfp, "Failed to close cont table");

	if (daos_handle_is_valid(dfp->dfp_poh)) {
		rc = daos_pool_disconnect(dfp->dfp_poh, NULL);
		if (rc != -DER_SUCCESS)
			DFUSE_TRA_ERROR(dfp,
					"daos_pool_disconnect() failed: "DF_RC,
					DP_RC(rc));
	}

	return 0;
}

/* Called once per projection, after the FUSE filesystem has been torn down */
int
dfuse_fs_fini(struct dfuse_projection_info *fs_handle)
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

	DFUSE_TRA_INFO(fs_handle, "Flush complete: "DF_RC, DP_RC(rc));

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

	rc = daos_eq_destroy(fs_handle->dpi_eq, 0);
	if (rc) {
		DFUSE_TRA_WARNING(fs_handle, "Failed to destroy EQ");
		rcp = EINVAL;
	}

	rc = d_hash_table_destroy_inplace(&fs_handle->dpi_iet, false);
	if (rc) {
		DFUSE_TRA_WARNING(fs_handle, "Failed to close inode handles");
		rcp = EINVAL;
	}

	d_hash_table_traverse(&fs_handle->dpi_pool_table,
			      dfuse_pool_close_cb, NULL);

	rc = d_hash_table_destroy_inplace(&fs_handle->dpi_pool_table, false);
	if (rc) {
		DFUSE_TRA_WARNING(fs_handle, "Failed to close pools");
		rcp = EINVAL;
	}

	return rcp;
}
