/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <pthread.h>

#include "dfuse_common.h"
#include "dfuse.h"

/* Async progress thread.
 *
 * A number of threads are created at launch, each thread having its own event queue with a
 * semaphore to wakeup, posted for each entry added to the event queue and once for shutdown.
 * When there are no entries on the eq then the thread will yield in the semaphore, when there
 * are pending events it'll spin in eq_poll() for completion.  All pending events should be
 * completed before thread exit, should exit be called with pending events.
 */
static void *
dfuse_progress_thread(void *arg)
{
	struct dfuse_eq *eqt = arg;
	daos_event_t    *dev[128];
	int              to_consume = 1;

	while (1) {
		int rc;
		int i;

		for (i = 0; i < to_consume; i++) {
cont:
			errno = 0;
			rc    = sem_wait(&eqt->de_sem);

			if (rc != 0) {
				rc = errno;

				if (rc == EINTR)
					D_GOTO(cont, 0);

				DFUSE_TRA_ERROR(eqt, "Error from sem_wait: %d", rc);
			}
		}

		if (eqt->de_handle->di_shutdown) {
			int pending;

			pending = daos_eq_query(eqt->de_eq, DAOS_EQR_ALL, 0, NULL);
			DFUSE_TRA_INFO(eqt, "There are %d events pending", pending);

			if (pending == 0)
				return NULL;
		}

		rc = daos_eq_poll(eqt->de_eq, 1, DAOS_EQ_WAIT, 128, &dev[0]);
		if (rc >= 1) {
			for (i = 0; i < rc; i++) {
				struct dfuse_event *ev;

				ev = container_of(dev[i], struct dfuse_event, de_ev);
				ev->de_complete_cb(ev);
			}
			to_consume = rc;
		} else if (rc < 0) {
			DFUSE_TRA_WARNING(eqt, "Error from daos_eq_poll, " DF_RC, DP_RC(rc));
			to_consume = 0;
		} else {
			to_consume = 0;
		}
	}
	return NULL;
}

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
		if (c == 'd' || c == 'D')
			out *= 60 * 60 * 24;
		else if (c == 'h' || c == 'H')
			out *= 60 * 60;
		else if (c == 'm' || c == 'M')
			out *= 60;
		else if (c == 's' || c == 'S')
			true;
		else
			return EINVAL;
	}

	*_out = out;
	return 0;
}

/* Inode entry hash table operations */

/* Shrink a 64 bit value into 32 bits to avoid hash collisions */
static uint32_t
ih_key_hash(struct d_hash_table *htable, const void *key, unsigned int ksize)
{
	const ino_t *_ino = key;
	ino_t        ino  = *_ino;
	uint32_t     hash = ino ^ (ino >> 32);

	return hash;
}

static bool
ih_key_cmp(struct d_hash_table *htable, d_list_t *rlink, const void *key, unsigned int ksize)
{
	const struct dfuse_inode_entry *ie;
	const ino_t                    *ino = key;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	return *ino == ie->ie_stat.st_ino;
}

static uint32_t
ih_rec_hash(struct d_hash_table *htable, d_list_t *rlink)
{
	const struct dfuse_inode_entry *ie;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	return ih_key_hash(NULL, &ie->ie_stat.st_ino, sizeof(ie->ie_stat.st_ino));
}

static void
ih_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfuse_inode_entry *ie;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);
	atomic_fetch_add_relaxed(&ie->ie_ref, 1);
}

static bool
ih_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfuse_inode_entry *ie;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);
	return (atomic_fetch_sub_relaxed(&ie->ie_ref, 1) == 1);
}

static int
ih_ndecref(struct d_hash_table *htable, d_list_t *rlink, int count)
{
	struct dfuse_inode_entry *ie;
	uint32_t                  oldref = 0;
	uint32_t                  newref = 0;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	do {
		oldref = atomic_load_relaxed(&ie->ie_ref);

		if (oldref < count)
			break;

		newref = oldref - count;

	} while (!atomic_compare_exchange(&ie->ie_ref, oldref, newref));

	if (oldref < count) {
		DFUSE_TRA_ERROR(ie, "unable to decref %u from %u", count, oldref);
		return -DER_INVAL;
	}

	if (newref == 0)
		return 1;
	return 0;
}

static void
ih_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfuse_inode_entry *ie;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	DFUSE_TRA_DEBUG(ie, "parent %#lx", ie->ie_parent);
	dfuse_ie_close(htable->ht_priv, ie);
}

static d_hash_table_ops_t ie_hops = {
    .hop_key_cmp     = ih_key_cmp,
    .hop_key_hash    = ih_key_hash,
    .hop_rec_hash    = ih_rec_hash,
    .hop_rec_addref  = ih_addref,
    .hop_rec_decref  = ih_decref,
    .hop_rec_ndecref = ih_ndecref,
    .hop_rec_free    = ih_free,
};

static uint32_t
ph_key_hash(struct d_hash_table *htable, const void *key, unsigned int ksize)
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
ph_key_cmp(struct d_hash_table *htable, d_list_t *link, const void *key, unsigned int ksize)
{
	struct dfuse_pool *dfp;

	dfp = container_of(link, struct dfuse_pool, dfp_entry);
	return uuid_compare(dfp->dfp_pool, key) == 0;
}

static void
ph_addref(struct d_hash_table *htable, d_list_t *link)
{
	struct dfuse_pool *dfp;
	uint32_t           oldref;

	dfp    = container_of(link, struct dfuse_pool, dfp_entry);
	oldref = atomic_fetch_add_relaxed(&dfp->dfp_ref, 1);
	DFUSE_TRA_DEBUG(dfp, "addref to %u", oldref + 1);
}

static bool
ph_decref(struct d_hash_table *htable, d_list_t *link)
{
	struct dfuse_pool *dfp;
	uint32_t           oldref;

	dfp    = container_of(link, struct dfuse_pool, dfp_entry);
	oldref = atomic_fetch_sub_relaxed(&dfp->dfp_ref, 1);
	DFUSE_TRA_DEBUG(dfp, "decref to %u", oldref - 1);
	return oldref == 1;
}

static void
_ph_free(struct dfuse_info *dfuse_info, struct dfuse_pool *dfp)
{
	int rc;

	if (daos_handle_is_valid(dfp->dfp_poh)) {
		rc = daos_pool_disconnect(dfp->dfp_poh, NULL);
		/* Hook for fault injection testing, if the disconnect fails with out of memory
		 * then simply try it again.
		 */
		if (rc == -DER_NOMEM)
			rc = daos_pool_disconnect(dfp->dfp_poh, NULL);
		if (rc != -DER_SUCCESS)
			DFUSE_TRA_ERROR(dfp, "daos_pool_disconnect() failed: " DF_RC, DP_RC(rc));
	}

	rc = d_hash_table_destroy_inplace(&dfp->dfp_cont_table, false);
	if (rc != -DER_SUCCESS)
		DFUSE_TRA_ERROR(dfp, "Failed to destroy pool hash table: " DF_RC, DP_RC(rc));

	atomic_fetch_sub_relaxed(&dfuse_info->di_pool_count, 1);

	D_FREE(dfp);
}

static void
ph_free(struct d_hash_table *htable, d_list_t *link)
{
	_ph_free(htable->ht_priv, container_of(link, struct dfuse_pool, dfp_entry));
}

static d_hash_table_ops_t pool_hops = {
    .hop_key_cmp    = ph_key_cmp,
    .hop_key_hash   = ph_key_hash,
    .hop_rec_hash   = ph_rec_hash,
    .hop_rec_addref = ph_addref,
    .hop_rec_decref = ph_decref,
    .hop_rec_free   = ph_free,
};

static uint32_t
ch_key_hash(struct d_hash_table *htable, const void *key, unsigned int ksize)
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
ch_key_cmp(struct d_hash_table *htable, d_list_t *link, const void *key, unsigned int ksize)
{
	struct dfuse_cont *dfc;

	dfc = container_of(link, struct dfuse_cont, dfs_entry);
	return uuid_compare(dfc->dfs_cont, key) == 0;
}

static void
ch_addref(struct d_hash_table *htable, d_list_t *link)
{
	struct dfuse_cont *dfc;
	uint32_t           oldref;

	dfc    = container_of(link, struct dfuse_cont, dfs_entry);
	oldref = atomic_fetch_add_relaxed(&dfc->dfs_ref, 1);
	DFUSE_TRA_DEBUG(dfc, "addref to %u", oldref + 1);
}

static bool
ch_decref(struct d_hash_table *htable, d_list_t *link)
{
	struct dfuse_cont *dfc;
	uint32_t           oldref;

	dfc    = container_of(link, struct dfuse_cont, dfs_entry);
	oldref = atomic_fetch_sub_relaxed(&dfc->dfs_ref, 1);
	DFUSE_TRA_DEBUG(dfc, "decref to %u", oldref - 1);
	return oldref == 1;
}

static void
_ch_free(struct dfuse_info *dfuse_info, struct dfuse_cont *dfc)
{
	if (daos_handle_is_valid(dfc->dfs_coh)) {
		int rc;

		rc = dfs_umount(dfc->dfs_ns);
		if (rc != 0)
			DFUSE_TRA_ERROR(dfc, "dfs_umount() failed: %d (%s)", rc, strerror(rc));

		rc = daos_cont_close(dfc->dfs_coh, NULL);
		if (rc == -DER_NOMEM)
			rc = daos_cont_close(dfc->dfs_coh, NULL);
		if (rc != 0)
			DFUSE_TRA_ERROR(dfc, "daos_cont_close() failed, " DF_RC, DP_RC(rc));
	}

	atomic_fetch_sub_relaxed(&dfuse_info->di_container_count, 1);
	d_hash_rec_decref(&dfuse_info->di_pool_table, &dfc->dfs_dfp->dfp_entry);

	D_FREE(dfc);
}

static void
ch_free(struct d_hash_table *htable, d_list_t *link)
{
	_ch_free(htable->ht_priv, container_of(link, struct dfuse_cont, dfs_entry));
}

d_hash_table_ops_t cont_hops = {
    .hop_key_cmp    = ch_key_cmp,
    .hop_key_hash   = ch_key_hash,
    .hop_rec_hash   = ch_rec_hash,
    .hop_rec_addref = ch_addref,
    .hop_rec_decref = ch_decref,
    .hop_rec_free   = ch_free,
};

/* Connect to a pool.
 *
 * Daos accepts labels and uuids via the same function so simply call that, connect to a pool
 * and setup a descriptor, then enter into the hash table and verify that it's unique.  If there
 * is likely already a connection for this pool then use dfuse_pool_get_handle() instead
 * which will do a hash-table lookup in advance.
 *
 * Return code is a system errno.
 */
int
dfuse_pool_connect(struct dfuse_info *fs_handle, const char *label, struct dfuse_pool **_dfp)
{
	struct dfuse_pool *dfp;
	d_list_t          *rlink;
	int                rc;
	int                ret;

	D_ALLOC_PTR(dfp);
	if (dfp == NULL)
		D_GOTO(err, rc = ENOMEM);

	atomic_init(&dfp->dfp_ref, 1);

	DFUSE_TRA_UP(dfp, fs_handle, "dfp");

	/* Handle the case where no identifier is supplied, this is for when dfuse
	 * is started without any pool on the command line.
	 */
	if (label[0]) {
		daos_pool_info_t p_info = {};

		rc = daos_pool_connect(label, fs_handle->di_group, DAOS_PC_RO, &dfp->dfp_poh,
				       &p_info, NULL);
		if (rc) {
			if (rc == -DER_NO_PERM || rc == -DER_NONEXIST)
				DFUSE_TRA_INFO(dfp, "daos_pool_connect() failed, " DF_RC,
					       DP_RC(rc));
			else
				DFUSE_TRA_ERROR(dfp, "daos_pool_connect() '%s' failed, " DF_RC,
						label, DP_RC(rc));
			D_GOTO(err_free, rc = daos_der2errno(rc));
		}

		uuid_copy(dfp->dfp_pool, p_info.pi_uuid);
	}

	rc = d_hash_table_create_inplace(D_HASH_FT_LRU | D_HASH_FT_EPHEMERAL, 3, fs_handle,
					 &cont_hops, &dfp->dfp_cont_table);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(dfp, "Failed to create hash table: " DF_RC, DP_RC(rc));
		D_GOTO(err_disconnect, rc = daos_der2errno(rc));
	}

	atomic_fetch_add_relaxed(&fs_handle->di_pool_count, 1);

	rlink = d_hash_rec_find_insert(&fs_handle->di_pool_table, &dfp->dfp_pool,
				       sizeof(dfp->dfp_pool), &dfp->dfp_entry);

	if (rlink != &dfp->dfp_entry) {
		DFUSE_TRA_DEBUG(dfp, "Found existing pool, reusing");
		_ph_free(fs_handle, dfp);
		dfp = container_of(rlink, struct dfuse_pool, dfp_entry);
	}

	DFUSE_TRA_DEBUG(dfp, "Returning dfp for " DF_UUID, DP_UUID(dfp->dfp_pool));

	*_dfp = dfp;
	return rc;
err_disconnect:
	ret = daos_pool_disconnect(dfp->dfp_poh, NULL);
	if (ret)
		DFUSE_TRA_WARNING(dfp, "Failed to disconnect pool: "DF_RC, DP_RC(ret));
err_free:
	D_FREE(dfp);
err:
	return rc;
}

int
dfuse_pool_get_handle(struct dfuse_info *dfuse_info, uuid_t pool, struct dfuse_pool **_dfp)
{
	d_list_t *rlink;
	char      uuid_str[37];

	rlink = d_hash_rec_find(&dfuse_info->di_pool_table, pool, sizeof(*pool));
	if (rlink) {
		*_dfp = container_of(rlink, struct dfuse_pool, dfp_entry);
		return 0;
	}

	uuid_unparse(pool, uuid_str);

	return dfuse_pool_connect(dfuse_info, uuid_str, _dfp);
}

#define ATTR_COUNT 6

char const *const cont_attr_names[ATTR_COUNT] = {
    "dfuse-attr-time",    "dfuse-dentry-time", "dfuse-dentry-dir-time",
    "dfuse-ndentry-time", "dfuse-data-cache",  "dfuse-direct-io-disable"};

#define ATTR_TIME_INDEX              0
#define ATTR_DENTRY_INDEX            1
#define ATTR_DENTRY_DIR_INDEX        2
#define ATTR_NDENTRY_INDEX           3
#define ATTR_DATA_CACHE_INDEX        4
#define ATTR_DIRECT_IO_DISABLE_INDEX 5

/* Attribute values are of the form "120M", so the buffer does not need to be
 * large.
 */
#define ATTR_VALUE_LEN               128

static bool
dfuse_char_enabled(char *addr, size_t len)
{
	if (strncasecmp(addr, "on", len) == 0)
		return true;
	if (strncasecmp(addr, "true", len) == 0)
		return true;
	return false;
}

static bool
dfuse_char_disabled(char *addr, size_t len)
{
	if (strncasecmp(addr, "off", len) == 0)
		return true;
	if (strncasecmp(addr, "false", len) == 0)
		return true;
	return false;
}

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
	size_t       sizes[ATTR_COUNT];
	char        *buff;
	char        *buff_addrs[ATTR_COUNT];
	int          rc;
	int          i;
	unsigned int value;
	bool         have_dentry     = false;
	bool         have_dentry_dir = false;
	bool         have_dio        = false;
	bool         have_cache_off  = false;

	D_ALLOC(buff, ATTR_VALUE_LEN * ATTR_COUNT);

	if (buff == NULL)
		return ENOMEM;

	for (i = 0; i < ATTR_COUNT; i++) {
		sizes[i]      = ATTR_VALUE_LEN - 1;
		buff_addrs[i] = buff + i * ATTR_VALUE_LEN;
	}

	rc = daos_cont_get_attr(dfc->dfs_coh, ATTR_COUNT, cont_attr_names,
				(void *const *)buff_addrs, sizes, NULL);

	if (rc == -DER_NONEXIST) {
		/* none of the cache related attrs are present */
		D_GOTO(out, rc = ENODATA);
	} else if (rc != -DER_SUCCESS) {
		DFUSE_TRA_WARNING(dfc, "Failed to load values for all cache related attrs" DF_RC,
				  DP_RC(rc));
		D_GOTO(out, rc = daos_der2errno(rc));
	}

	for (i = 0; i < ATTR_COUNT; i++) {
		if (sizes[i] == 0) {
			/* attr is not present */
			continue;
		}

		/* Ensure the character after the fetched string is zero in case
		 * of non-null terminated strings.  size always refers to the
		 * number of non-null characters in this case, regardless of if
		 * the attribute is null terminated or not.
		 */
		if (*(buff_addrs[i] + sizes[i] - 1) == '\0')
			sizes[i]--;
		else
			*(buff_addrs[i] + sizes[i]) = '\0';

		if (i == ATTR_DATA_CACHE_INDEX) {
			if (dfuse_char_enabled(buff_addrs[i], sizes[i])) {
				dfc->dfc_data_timeout = -1;
				DFUSE_TRA_INFO(dfc, "setting '%s' is enabled", cont_attr_names[i]);
			} else if (dfuse_char_disabled(buff_addrs[i], sizes[i])) {
				have_cache_off        = true;
				dfc->dfc_data_timeout = 0;
				DFUSE_TRA_INFO(dfc, "setting '%s' is disabled", cont_attr_names[i]);
			} else if (dfuse_parse_time(buff_addrs[i], sizes[i], &value) == 0) {
				DFUSE_TRA_INFO(dfc, "setting '%s' is %u seconds",
					       cont_attr_names[i], value);
				dfc->dfc_data_timeout = value;
			} else {
				DFUSE_TRA_WARNING(dfc, "Failed to parse '%s' for '%s'",
						  buff_addrs[i], cont_attr_names[i]);
				dfc->dfc_data_timeout = 0;
			}
			continue;
		}
		if (i == ATTR_DIRECT_IO_DISABLE_INDEX) {
			if (dfuse_char_enabled(buff_addrs[i], sizes[i])) {
				have_dio                   = true;
				dfc->dfc_direct_io_disable = true;
				DFUSE_TRA_INFO(dfc, "setting '%s' is enabled", cont_attr_names[i]);
			} else if (dfuse_char_disabled(buff_addrs[i], sizes[i])) {
				dfc->dfc_direct_io_disable = false;
				DFUSE_TRA_INFO(dfc, "setting '%s' is disabled", cont_attr_names[i]);
			} else {
				DFUSE_TRA_WARNING(dfc, "Failed to parse '%s' for '%s'",
						  buff_addrs[i], cont_attr_names[i]);
				dfc->dfc_direct_io_disable = false;
			}
			continue;
		}

		rc = dfuse_parse_time(buff_addrs[i], sizes[i], &value);
		if (rc != 0) {
			DFUSE_TRA_WARNING(dfc, "Failed to parse '%s' for '%s'", buff_addrs[i],
					  cont_attr_names[i]);
			continue;
		}
		DFUSE_TRA_INFO(dfc, "setting '%s' is %u seconds", cont_attr_names[i], value);
		if (i == ATTR_TIME_INDEX) {
			dfc->dfc_attr_timeout = value;
		} else if (i == ATTR_DENTRY_INDEX) {
			have_dentry             = true;
			dfc->dfc_dentry_timeout = value;
		} else if (i == ATTR_DENTRY_DIR_INDEX) {
			have_dentry_dir             = true;
			dfc->dfc_dentry_dir_timeout = value;
		} else if (i == ATTR_NDENTRY_INDEX) {
			dfc->dfc_ndentry_timeout = value;
		}
	}

	/* Check if dfuse-direct-io-disable is set to on but dfuse-data-cache is set to off.
	 * This combination does not make sense, so warn in this case and set caching to on.
	 */
	if (have_dio) {
		if (have_cache_off)
			DFUSE_TRA_WARNING(dfc, "Caching enabled because of %s",
					  cont_attr_names[ATTR_DIRECT_IO_DISABLE_INDEX]);
		dfc->dfc_data_timeout = -1;
	}

	if (have_dentry && !have_dentry_dir)
		dfc->dfc_dentry_dir_timeout = dfc->dfc_dentry_timeout;
	rc = 0;
out:
	D_FREE(buff);
	return rc;
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
	dfc->dfc_attr_timeout       = 1;
	dfc->dfc_dentry_timeout     = 1;
	dfc->dfc_dentry_dir_timeout = 5;
	dfc->dfc_ndentry_timeout    = 1;
	dfc->dfc_data_timeout       = 60 * 10;
	dfc->dfc_direct_io_disable  = false;
}

/* Open a cont by label.
 *
 * Only used for command line labels, not for paths in dfuse.
 */
int
dfuse_cont_open_by_label(struct dfuse_info *dfuse_info, struct dfuse_pool *dfp, const char *label,
			 struct dfuse_cont **_dfc)
{
	struct dfuse_cont *dfc;
	daos_cont_info_t   c_info    = {};
	int                dfs_flags = O_RDWR;
	int                rc;
	int                ret;

	D_ALLOC_PTR(dfc);
	if (dfc == NULL)
		D_GOTO(err_free, rc = ENOMEM);

	DFUSE_TRA_UP(dfc, dfp, "dfc");

	rc = daos_cont_open(dfp->dfp_poh, label, DAOS_COO_RW, &dfc->dfs_coh, &c_info, NULL);
	if (rc == -DER_NO_PERM) {
		dfs_flags = O_RDONLY;
		rc = daos_cont_open(dfp->dfp_poh, label, DAOS_COO_RO, &dfc->dfs_coh, &c_info, NULL);
	}
	if (rc == -DER_NONEXIST) {
		DFUSE_TRA_INFO(dfc, "daos_cont_open() failed: " DF_RC, DP_RC(rc));
		D_GOTO(err_free, rc = daos_der2errno(rc));
	} else if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(dfc, "daos_cont_open() failed: " DF_RC, DP_RC(rc));
		D_GOTO(err_free, rc = daos_der2errno(rc));
	}

	uuid_copy(dfc->dfs_cont, c_info.ci_uuid);

	rc = dfs_mount(dfp->dfp_poh, dfc->dfs_coh, dfs_flags, &dfc->dfs_ns);
	if (rc) {
		DFUSE_TRA_ERROR(dfc, "dfs_mount() failed: %d (%s)", rc, strerror(rc));
		D_GOTO(err_close, rc);
	}

	if (dfuse_info->di_caching) {
		rc = dfuse_cont_get_cache(dfc);
		if (rc == ENODATA) {
			/* If there is no container specific
			 * attributes then use defaults
			 */
			DFUSE_TRA_INFO(dfc, "Using default caching values");
			dfuse_set_default_cont_cache_values(dfc);
		} else if (rc != 0) {
			D_GOTO(err_close, rc);
		}
	} else {
		DFUSE_TRA_INFO(dfc, "Caching disabled");
	}

	rc = dfuse_cont_open(dfuse_info, dfp, &c_info.ci_uuid, &dfc);
	if (rc) {
		D_FREE(dfc);
		return rc;
	}
	*_dfc = dfc;
	return 0;

err_close:
	ret = daos_cont_close(dfc->dfs_coh, NULL);
	if (ret)
		DFUSE_TRA_WARNING(dfc, "daos_cont_close() failed: " DF_RC, DP_RC(ret));
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
dfuse_cont_open(struct dfuse_info *dfuse_info, struct dfuse_pool *dfp, uuid_t *cont,
		struct dfuse_cont **_dfc)
{
	struct dfuse_cont *dfc = NULL;
	d_list_t          *rlink;
	int                rc = -DER_SUCCESS;

	if (*_dfc) {
		dfc = *_dfc;
	} else {
		/* Check if there is already a open container connection, and
		 * just use it if there is.  The rec_find() will take the
		 * additional reference for us.
		 */
		rlink = d_hash_rec_find(&dfp->dfp_cont_table, cont, sizeof(*cont));
		if (rlink) {
			*_dfc = container_of(rlink, struct dfuse_cont, dfs_entry);
			return 0;
		}

		D_ALLOC_PTR(dfc);
		if (!dfc)
			D_GOTO(err, rc = ENOMEM);

		DFUSE_TRA_UP(dfc, dfp, "dfc");
	}

	/* No existing container found, so setup dfs and connect to one */

	atomic_init(&dfc->dfs_ref, 1);

	DFUSE_TRA_DEBUG(dfp, "New cont "DF_UUIDF" in pool "DF_UUIDF,
			DP_UUID(cont), DP_UUID(dfp->dfp_pool));

	dfc->dfs_dfp = dfp;

	/* Allow for uuid to be NULL, in which case this represents a pool */
	if (uuid_is_null(*cont)) {
		if (uuid_is_null(dfp->dfp_pool))
			dfc->dfs_ops = &dfuse_pool_ops;
		else
			dfc->dfs_ops = &dfuse_cont_ops;

		/* Turn on some caching of metadata, otherwise container
		 * operations will be very frequent
		 */
		dfc->dfc_attr_timeout       = 60;
		dfc->dfc_dentry_dir_timeout = 60;
		dfc->dfc_ndentry_timeout    = 60;

	} else if (*_dfc == NULL) {
		char str[37];
		int  dfs_flags = O_RDWR;

		dfc->dfs_ops = &dfuse_dfs_ops;
		uuid_copy(dfc->dfs_cont, *cont);
		uuid_unparse(dfc->dfs_cont, str);
		rc = daos_cont_open(dfp->dfp_poh, str, DAOS_COO_RW, &dfc->dfs_coh, NULL, NULL);
		if (rc == -DER_NO_PERM) {
			dfs_flags = O_RDONLY;
			rc = daos_cont_open(dfp->dfp_poh, str, DAOS_COO_RO, &dfc->dfs_coh, NULL,
					    NULL);
		}
		if (rc == -DER_NONEXIST) {
			DFUSE_TRA_INFO(dfc, "daos_cont_open() failed: " DF_RC, DP_RC(rc));
			D_GOTO(err_free, rc = daos_der2errno(rc));
		} else if (rc != -DER_SUCCESS) {
			DFUSE_TRA_ERROR(dfc, "daos_cont_open() failed: " DF_RC, DP_RC(rc));
			D_GOTO(err_free, rc = daos_der2errno(rc));
		}
		rc = dfs_mount(dfp->dfp_poh, dfc->dfs_coh, dfs_flags, &dfc->dfs_ns);
		if (rc) {
			DFUSE_TRA_ERROR(dfc, "dfs_mount() failed: %d (%s)", rc, strerror(rc));
			D_GOTO(err_close, rc);
		}

		if (dfuse_info->di_caching) {
			rc = dfuse_cont_get_cache(dfc);
			if (rc == ENODATA) {
				/* If there are no container attributes then use defaults */
				DFUSE_TRA_INFO(dfc, "Using default caching values");
				dfuse_set_default_cont_cache_values(dfc);
				rc = 0;
			} else if (rc != 0) {
				D_GOTO(err_umount, rc);
			}
		} else {
			DFUSE_TRA_INFO(dfc, "Caching disabled");
		}
	} else {
		/* This is either a container where a label is set on the
		 * command line, or one created through mkdir, in either case
		 * the container will be mounted, and caching etc already
		 * setup.
		 */
		dfc->dfs_ops = &dfuse_dfs_ops;
	}

	dfc->dfs_ino = atomic_fetch_add_relaxed(&dfuse_info->di_ino_next, 1);

	/* Take a reference on the pool */
	d_hash_rec_addref(&dfuse_info->di_pool_table, &dfp->dfp_entry);

	atomic_fetch_add_relaxed(&dfuse_info->di_container_count, 1);

	/* Finally insert into the hash table.  This may return an existing
	 * container if there is a race to insert, so if that happens
	 * just use that one.
	 */
	rlink = d_hash_rec_find_insert(&dfp->dfp_cont_table,
				       &dfc->dfs_cont, sizeof(dfc->dfs_cont),
				       &dfc->dfs_entry);

	if (rlink != &dfc->dfs_entry) {
		DFUSE_TRA_DEBUG(dfp, "Found existing container, reusing");

		_ch_free(dfuse_info, dfc);

		dfc = container_of(rlink, struct dfuse_cont, dfs_entry);
	}

	DFUSE_TRA_DEBUG(dfc, "Returning dfs for " DF_UUID " ref %d", DP_UUID(dfc->dfs_cont),
			dfc->dfs_ref);

	*_dfc = dfc;

	return rc;
err_umount:
	dfs_umount(dfc->dfs_ns);
err_close:
	daos_cont_close(dfc->dfs_coh, NULL);
err_free:
	D_FREE(dfc);
err:
	return rc;
}

/* Set a timer to mark cache entry as valid */
void
dfuse_mcache_set_time(struct dfuse_inode_entry *ie)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
	ie->ie_mcache_last_update = now;
}

void
dfuse_mcache_evict(struct dfuse_inode_entry *ie)
{
	ie->ie_mcache_last_update.tv_sec  = 0;
	ie->ie_mcache_last_update.tv_nsec = 0;
}

bool
dfuse_mcache_get_valid(struct dfuse_inode_entry *ie, double max_age, double *timeout)
{
	bool            use = false;
	struct timespec now;
	struct timespec left;
	double          time_left;

	D_ASSERT(max_age != -1);
	D_ASSERT(max_age >= 0);

	if (ie->ie_mcache_last_update.tv_sec == 0)
		return false;

	clock_gettime(CLOCK_MONOTONIC_COARSE, &now);

	left.tv_sec  = now.tv_sec - ie->ie_mcache_last_update.tv_sec;
	left.tv_nsec = now.tv_nsec - ie->ie_mcache_last_update.tv_nsec;
	if (left.tv_nsec < 0) {
		left.tv_sec--;
		left.tv_nsec += 1000000000;
	}
	time_left = max_age - (left.tv_sec + ((double)left.tv_nsec / 1000000000));
	if (time_left > 0) {
		use = true;

		DFUSE_TRA_DEBUG(ie, "Allowing cache use, time remaining: %lf", time_left);

		if (timeout)
			*timeout = time_left;
	}

	return use;
}

/* Set a timer to mark cache entry as valid */
void
dfuse_dcache_set_time(struct dfuse_inode_entry *ie)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
	ie->ie_dcache_last_update = now;
}

void
dfuse_dcache_evict(struct dfuse_inode_entry *ie)
{
	ie->ie_dcache_last_update.tv_sec  = 0;
	ie->ie_dcache_last_update.tv_nsec = 0;
}

bool
dfuse_dcache_get_valid(struct dfuse_inode_entry *ie, double max_age)
{
	bool            use = false;
	struct timespec now;
	struct timespec left;
	double          time_left;

	if (max_age == -1)
		return true;

	if (ie->ie_dcache_last_update.tv_sec == 0)
		return false;

	clock_gettime(CLOCK_MONOTONIC_COARSE, &now);

	left.tv_sec  = now.tv_sec - ie->ie_dcache_last_update.tv_sec;
	left.tv_nsec = now.tv_nsec - ie->ie_dcache_last_update.tv_nsec;
	if (left.tv_nsec < 0) {
		left.tv_sec--;
		left.tv_nsec += 1000000000;
	}
	time_left = max_age - (left.tv_sec + ((double)left.tv_nsec / 1000000000));
	if (time_left > 0) {
		use = true;

		DFUSE_TRA_DEBUG(ie, "Allowing cache use");
	}

	return use;
}

void
dfuse_cache_evict(struct dfuse_inode_entry *ie)
{
	dfuse_mcache_evict(ie);
	dfuse_dcache_evict(ie);
}

int
dfuse_fs_init(struct dfuse_info *fs_handle)
{
	int rc;
	int i;

	D_ALLOC_ARRAY(fs_handle->di_eqt, fs_handle->di_eq_count);
	if (fs_handle->di_eqt == NULL)
		D_GOTO(err, rc = -DER_NOMEM);

	atomic_init(&fs_handle->di_inode_count, 0);
	atomic_init(&fs_handle->di_fh_count, 0);
	atomic_init(&fs_handle->di_pool_count, 0);
	atomic_init(&fs_handle->di_container_count, 0);

	rc = d_hash_table_create_inplace(D_HASH_FT_LRU | D_HASH_FT_EPHEMERAL, 3, fs_handle,
					 &pool_hops, &fs_handle->di_pool_table);
	if (rc != 0)
		D_GOTO(err, rc);

	rc = d_hash_table_create_inplace(D_HASH_FT_LRU | D_HASH_FT_EPHEMERAL, 16, fs_handle,
					 &ie_hops, &fs_handle->dpi_iet);
	if (rc != 0)
		D_GOTO(err_pt, rc);

	atomic_init(&fs_handle->di_ino_next, 2);
	atomic_init(&fs_handle->di_eqt_idx, 0);

	D_SPIN_INIT(&fs_handle->di_lock, 0);

	D_RWLOCK_INIT(&fs_handle->di_forget_lock, 0);

	for (i = 0; i < fs_handle->di_eq_count; i++) {
		struct dfuse_eq *eqt = &fs_handle->di_eqt[i];

		eqt->de_handle = fs_handle;

		DFUSE_TRA_UP(eqt, fs_handle, "event_queue");

		/* Create the semaphore before the eq as there's no way to check if sem_init()
		 * has been called or not and it's invalid to call sem_destroy if it hasn't.  This
		 * way we can avoid adding additional memory for tracking status of the semaphore.
		 */
		rc = sem_init(&eqt->de_sem, 0, 0);
		if (rc != 0)
			D_GOTO(err_eq, rc = daos_errno2der(errno));

		rc = daos_eq_create(&eqt->de_eq);
		if (rc != -DER_SUCCESS) {
			sem_destroy(&eqt->de_sem);

			DFUSE_TRA_DOWN(eqt);
			D_GOTO(err_eq, rc);
		}
	}

	fs_handle->di_shutdown = false;
	return rc;

err_eq:
	D_SPIN_DESTROY(&fs_handle->di_lock);
	D_RWLOCK_DESTROY(&fs_handle->di_forget_lock);

	for (i = 0; i < fs_handle->di_eq_count; i++) {
		struct dfuse_eq *eqt = &fs_handle->di_eqt[i];
		int              rc2;

		if (daos_handle_is_inval(eqt->de_eq))
			continue;

		rc2 = daos_eq_destroy(eqt->de_eq, 0);
		if (rc2 != -DER_SUCCESS)
			DFUSE_TRA_ERROR(eqt, "Failed to destroy event queue:" DF_RC, DP_RC(rc2));

		sem_destroy(&eqt->de_sem);
		DFUSE_TRA_DOWN(eqt);
	}
	d_hash_table_destroy_inplace(&fs_handle->dpi_iet, false);
err_pt:
	d_hash_table_destroy_inplace(&fs_handle->di_pool_table, false);
err:
	D_FREE(fs_handle->di_eqt);
	return rc;
}

void
dfuse_open_handle_init(struct dfuse_info *dfuse_info, struct dfuse_obj_hdl *oh,
		       struct dfuse_inode_entry *ie)
{
	oh->doh_dfs             = ie->ie_dfs->dfs_ns;
	oh->doh_ie              = ie;
	oh->doh_linear_read     = true;
	oh->doh_linear_read_pos = 0;
	atomic_init(&oh->doh_il_calls, 0);
	atomic_init(&oh->doh_readdir_number, 0);
	atomic_init(&oh->doh_write_count, 0);
	atomic_fetch_add_relaxed(&dfuse_info->di_fh_count, 1);
}

void
dfuse_ie_init(struct dfuse_info *dfuse_info, struct dfuse_inode_entry *ie)
{
	atomic_init(&ie->ie_ref, 1);
	atomic_init(&ie->ie_open_count, 0);
	atomic_init(&ie->ie_open_write_count, 0);
	atomic_init(&ie->ie_il_count, 0);
	atomic_init(&ie->ie_readdir_number, 0);
	atomic_fetch_add_relaxed(&dfuse_info->di_inode_count, 1);
}

void
dfuse_ie_close(struct dfuse_info *dfuse_info, struct dfuse_inode_entry *ie)
{
	int      rc;
	uint32_t ref;

	ref = atomic_load_relaxed(&ie->ie_ref);
	DFUSE_TRA_DEBUG(ie, "closing, inode %#lx ref %u, name " DF_DE ", parent %#lx",
			ie->ie_stat.st_ino, ref, DP_DE(ie->ie_name), ie->ie_parent);

	D_ASSERT(ref == 0);
	D_ASSERT(atomic_load_relaxed(&ie->ie_readdir_number) == 0);
	D_ASSERT(atomic_load_relaxed(&ie->ie_il_count) == 0);
	D_ASSERT(atomic_load_relaxed(&ie->ie_open_count) == 0);

	if (ie->ie_obj) {
		rc = dfs_release(ie->ie_obj);
		if (rc)
			DFUSE_TRA_ERROR(ie, "dfs_release() failed: %d (%s)", rc, strerror(rc));
	}

	if (ie->ie_root) {
		struct dfuse_cont *dfc = ie->ie_dfs;
		struct dfuse_pool *dfp = dfc->dfs_dfp;

		DFUSE_TRA_INFO(ie, "Closing poh %d coh %d", daos_handle_is_valid(dfp->dfp_poh),
			       daos_handle_is_valid(dfc->dfs_coh));

		d_hash_rec_decref(&dfp->dfp_cont_table, &dfc->dfs_entry);
	}

	dfuse_ie_free(dfuse_info, ie);
}

static void
dfuse_event_init(void *arg, void *handle)
{
	struct dfuse_event *ev = arg;

	ev->de_eqt = handle;
}

static bool
dfuse_read_event_reset(void *arg)
{
	struct dfuse_event *ev = arg;
	int                 rc;

	if (ev->de_iov.iov_buf == NULL) {
		D_ALLOC(ev->de_iov.iov_buf, DFUSE_MAX_READ);
		if (ev->de_iov.iov_buf == NULL)
			return false;

		ev->de_iov.iov_buf_len = DFUSE_MAX_READ;
		ev->de_sgl.sg_iovs     = &ev->de_iov;
		ev->de_sgl.sg_nr       = 1;
	}

	rc = daos_event_init(&ev->de_ev, ev->de_eqt->de_eq, NULL);
	if (rc != -DER_SUCCESS) {
		return false;
	}

	return true;
}

static bool
dfuse_write_event_reset(void *arg)
{
	struct dfuse_event *ev = arg;
	int                 rc;

	if (ev->de_iov.iov_buf == NULL) {
		D_ALLOC(ev->de_iov.iov_buf, DFUSE_MAX_READ);
		if (ev->de_iov.iov_buf == NULL)
			return false;

		ev->de_iov.iov_buf_len = DFUSE_MAX_READ;
		ev->de_sgl.sg_iovs     = &ev->de_iov;
		ev->de_sgl.sg_nr       = 1;
	}

	rc = daos_event_init(&ev->de_ev, ev->de_eqt->de_eq, NULL);
	if (rc != -DER_SUCCESS) {
		return false;
	}

	return true;
}

static void
dfuse_event_release(void *arg)
{
	struct dfuse_event *ev = arg;

	D_FREE(ev->de_iov.iov_buf);
}

int
dfuse_fs_start(struct dfuse_info *fs_handle, struct dfuse_cont *dfs)
{
	struct fuse_args          args     = {0};
	struct dfuse_inode_entry *ie       = NULL;
	struct d_slab_reg         read_slab  = {.sr_init    = dfuse_event_init,
						.sr_reset   = dfuse_read_event_reset,
						.sr_release = dfuse_event_release,
						POOL_TYPE_INIT(dfuse_event, de_list)};
	struct d_slab_reg         write_slab = {.sr_init    = dfuse_event_init,
						.sr_reset   = dfuse_write_event_reset,
						.sr_release = dfuse_event_release,
						POOL_TYPE_INIT(dfuse_event, de_list)};
	int                       i;
	int                       rc;

	args.argc = 5;

	if (fs_handle->di_multi_user)
		args.argc++;

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

	args.argv[4] = strdup("-onoatime");
	if (!args.argv[4])
		D_GOTO(err, rc = -DER_NOMEM);

	if (fs_handle->di_multi_user) {
		args.argv[5] = strdup("-oallow_other");
		if (!args.argv[5])
			D_GOTO(err, rc = -DER_NOMEM);
	}

	/* Create the root inode and insert into table */
	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(err, rc = -DER_NOMEM);

	DFUSE_TRA_UP(ie, fs_handle, "root_inode");

	ie->ie_dfs    = dfs;
	ie->ie_root   = true;
	ie->ie_parent = 1;
	dfuse_ie_init(fs_handle, ie);

	if (dfs->dfs_ops == &dfuse_dfs_ops) {
		rc = dfs_lookup(dfs->dfs_ns, "/", O_RDWR, &ie->ie_obj, NULL, &ie->ie_stat);
		if (rc) {
			DFUSE_TRA_ERROR(ie, "dfs_lookup() failed: %d (%s)", rc, strerror(rc));
			D_GOTO(err_ie, rc = daos_errno2der(rc));
		}
	} else {
		ie->ie_stat.st_uid  = geteuid();
		ie->ie_stat.st_gid  = getegid();
		ie->ie_stat.st_mode = 0700 | S_IFDIR;
	}
	ie->ie_stat.st_ino = 1;
	dfs->dfs_ino       = ie->ie_stat.st_ino;

	rc = d_hash_rec_insert(&fs_handle->dpi_iet,
			       &ie->ie_stat.st_ino,
			       sizeof(ie->ie_stat.st_ino),
			       &ie->ie_htl,
			       false);
	D_ASSERT(rc == -DER_SUCCESS);

	rc = d_slab_init(&fs_handle->di_slab, fs_handle);
	if (rc != -DER_SUCCESS)
		D_GOTO(err_ie_remove, rc);

	for (i = 0; i < fs_handle->di_eq_count; i++) {
		struct dfuse_eq *eqt = &fs_handle->di_eqt[i];

		rc = d_slab_register(&fs_handle->di_slab, &read_slab, eqt, &eqt->de_read_slab);
		if (rc != -DER_SUCCESS)
			D_GOTO(err_threads, rc);

		rc = d_slab_register(&fs_handle->di_slab, &write_slab, eqt, &eqt->de_write_slab);
		if (rc != -DER_SUCCESS)
			D_GOTO(err_threads, rc);

		rc = pthread_create(&eqt->de_thread, NULL, dfuse_progress_thread, eqt);
		if (rc != 0)
			D_GOTO(err_threads, rc = daos_errno2der(rc));

		pthread_setname_np(eqt->de_thread, "dfuse_progress");
	}

	rc = dfuse_launch_fuse(fs_handle, &args);
	if (rc == -DER_SUCCESS) {
		fuse_opt_free_args(&args);
		return rc;
	}

err_threads:
	for (i = 0; i < fs_handle->di_eq_count; i++) {
		struct dfuse_eq *eqt = &fs_handle->di_eqt[i];

		if (!eqt->de_thread)
			continue;

		sem_post(&eqt->de_sem);
		pthread_join(eqt->de_thread, NULL);
		sem_destroy(&eqt->de_sem);
	}

	d_slab_destroy(&fs_handle->di_slab);
err_ie_remove:
	dfs_release(ie->ie_obj);
	d_hash_rec_delete_at(&fs_handle->dpi_iet, &ie->ie_htl);
err_ie:
	dfuse_ie_free(fs_handle, ie);
err:
	DFUSE_TRA_ERROR(fs_handle, "Failed to start dfuse, rc: " DF_RC, DP_RC(rc));
	fuse_opt_free_args(&args);
	return rc;
}

static int
ino_flush(d_list_t *rlink, void *arg)
{
	struct dfuse_info        *dfuse_info = arg;
	struct dfuse_inode_entry *ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);
	int                       rc;

	/* Only evict entries that are direct children of the root, the kernel
	 * will walk the tree for us
	 */
	if (ie->ie_parent != 1)
		return 0;

	/* Do not evict root itself */
	if (ie->ie_stat.st_ino == 1)
		return 0;

	rc = fuse_lowlevel_notify_inval_entry(dfuse_info->di_session, ie->ie_parent, ie->ie_name,
					      strlen(ie->ie_name));
	if (rc != 0 && rc != -EBADF)
		DFUSE_TRA_WARNING(ie, "%#lx %#lx " DF_DE ": %d %s", ie->ie_parent,
				  ie->ie_stat.st_ino, DP_DE(ie->ie_name), rc, strerror(-rc));
	else
		DFUSE_TRA_INFO(ie, "%#lx %#lx " DF_DE ": %d %s", ie->ie_parent, ie->ie_stat.st_ino,
			       DP_DE(ie->ie_name), rc, strerror(-rc));

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

/* Called as part of shutdown, if the startup was successful.  Releases resources created during
 * operation.
 */
int
dfuse_fs_stop(struct dfuse_info *fs_handle)
{
	d_list_t *rlink;
	uint64_t  refs    = 0;
	int       handles = 0;
	int       rc;
	int       i;

	DFUSE_TRA_INFO(fs_handle, "Flushing inode table");

	fs_handle->di_shutdown = true;

	for (i = 0; i < fs_handle->di_eq_count; i++) {
		struct dfuse_eq *eqt = &fs_handle->di_eqt[i];

		sem_post(&eqt->de_sem);
	}

	for (i = 0; i < fs_handle->di_eq_count; i++) {
		struct dfuse_eq *eqt = &fs_handle->di_eqt[i];

		pthread_join(eqt->de_thread, NULL);

		sem_destroy(&eqt->de_sem);
	}

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

		atomic_store_relaxed(&ie->ie_il_count, 0);
		atomic_store_relaxed(&ie->ie_open_count, 0);

		DFUSE_TRA_DEBUG(ie, "Dropping %d", ref);

		refs += ref;
		d_hash_rec_ndecref(&fs_handle->dpi_iet, ref, rlink);
		handles++;
	} while (rlink);

	if (handles && rc != -DER_SUCCESS && rc != -DER_NO_HDL)
		DFUSE_TRA_WARNING(fs_handle, "dropped %lu refs on %u inodes", refs, handles);
	else
		DFUSE_TRA_INFO(fs_handle, "dropped %lu refs on %u inodes", refs, handles);

	d_hash_table_traverse(&fs_handle->di_pool_table, dfuse_pool_close_cb, NULL);

	d_slab_destroy(&fs_handle->di_slab);

	return 0;
}

/* Called as part of shutdown, after fs_stop(), and regardless of if dfuse started or not.
 * Releases core resources.
 */
int
dfuse_fs_fini(struct dfuse_info *dfuse_info)
{
	int rc = -DER_SUCCESS;
	int rc2;
	int i;

	D_SPIN_DESTROY(&dfuse_info->di_lock);
	D_RWLOCK_DESTROY(&dfuse_info->di_forget_lock);

	for (i = 0; i < dfuse_info->di_eq_count; i++) {
		struct dfuse_eq *eqt = &dfuse_info->di_eqt[i];

		rc = daos_eq_destroy(eqt->de_eq, 0);
		if (rc)
			DFUSE_TRA_WARNING(dfuse_info, "Failed to destroy EQ" DF_RC, DP_RC(rc));

		DFUSE_TRA_DOWN(eqt);
	}

	D_FREE(dfuse_info->di_eqt);

	rc2 = d_hash_table_destroy_inplace(&dfuse_info->dpi_iet, false);
	if (rc2) {
		DFUSE_TRA_WARNING(dfuse_info, "Failed to close inode handles");
		if (rc == -DER_SUCCESS)
			rc = rc2;
	}

	rc2 = d_hash_table_destroy_inplace(&dfuse_info->di_pool_table, false);
	if (rc2) {
		DFUSE_TRA_WARNING(dfuse_info, "Failed to close pools");
		if (rc == -DER_SUCCESS)
			rc = rc2;
	}

	return rc;
}
