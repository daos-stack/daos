/**
 * (C) Copyright 2016-2024 Intel Corporation.
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

		rc = daos_eq_poll(eqt->de_eq, 1, DAOS_EQ_NOWAIT, 128, &dev[0]);
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

static void
ih_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfuse_inode_entry *ie;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	DFUSE_TRA_DEBUG(ie, "parent %#lx", ie->ie_parent);
	dfuse_ie_close(htable->ht_priv, ie);
}

static d_hash_table_ops_t ie_hops = {
    .hop_key_cmp    = ih_key_cmp,
    .hop_key_hash   = ih_key_hash,
    .hop_rec_hash   = ih_rec_hash,
    .hop_rec_addref = ih_addref,
    .hop_rec_decref = ih_decref,
    .hop_rec_free   = ih_free,
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

	return ph_key_hash(NULL, &dfp->dfp_uuid, sizeof(dfp->dfp_uuid));
}

static bool
ph_key_cmp(struct d_hash_table *htable, d_list_t *link, const void *key, unsigned int ksize)
{
	struct dfuse_pool *dfp;

	dfp = container_of(link, struct dfuse_pool, dfp_entry);
	return uuid_compare(dfp->dfp_uuid, key) == 0;
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
_ph_free(struct dfuse_info *dfuse_info, struct dfuse_pool *dfp, bool used)
{
	struct dfuse_cont_core *dfcc, *dfccn;
	bool                    keep = used;
	int                     rc;

	if (dfuse_info->di_shutdown)
		keep = false;

	/* Iterate over all historic containers in this pool forgetting about them.  If the handle
	 * is still valid, for example because of a previous failed attempt to close it then re-try
	 * here.
	 * Other uses of this list are protected by dfuse_info->di_lock however this is in pool free
	 * so no references are held on the pool at this point so no lock is required here.
	 */

	d_list_for_each_entry_safe(dfcc, dfccn, &dfp->dfp_historic, dfcc_entry) {
		if (daos_handle_is_valid(dfcc->dfcc_coh)) {
			rc = daos_cont_close(dfcc->dfcc_coh, NULL);
			if (rc == -DER_SUCCESS)
				dfcc->dfcc_coh = DAOS_HDL_INVAL;
			else
				DHL_ERROR(dfcc, rc, "daos_cont_close() failed");
		}

		if (daos_handle_is_inval(dfcc->dfcc_coh) && dfcc->dfcc_ino == 0) {
			d_list_del(&dfcc->dfcc_entry);
			D_FREE(dfcc);
		}
	}

	if (daos_handle_is_valid(dfp->dfp_poh)) {
		rc = daos_pool_disconnect(dfp->dfp_poh, NULL);
		if (rc == -DER_SUCCESS) {
			dfp->dfp_poh = DAOS_HDL_INVAL;
		} else {
			keep = true;
			DHL_ERROR(dfp, rc, "daos_pool_disconnect() failed");
		}
	}

	rc = d_hash_table_destroy(dfp->dfp_cont_table, false);
	if (rc != -DER_SUCCESS)
		DHL_ERROR(dfp, rc, "Failed to destroy pool hash table");

	atomic_fetch_sub_relaxed(&dfuse_info->di_pool_count, 1);

	if (keep) {
		struct dfuse_pool *dfpp;

		D_SPIN_LOCK(&dfuse_info->di_lock);
		d_list_for_each_entry(dfpp, &dfuse_info->di_pool_historic, dfp_entry) {
			if (uuid_compare(dfpp->dfp_uuid, dfp->dfp_uuid) != 0)
				continue;
			keep = false;
			d_list_splice_init(&dfp->dfp_historic, &dfpp->dfp_historic);
			break;
		}

		if (daos_handle_is_valid(dfp->dfp_poh))
			keep = true;

		if (keep)
			d_list_add(&dfp->dfp_entry, &dfuse_info->di_pool_historic);
		D_SPIN_UNLOCK(&dfuse_info->di_lock);
	}

	if (!keep) {
		d_list_for_each_entry_safe(dfcc, dfccn, &dfp->dfp_historic, dfcc_entry) {
			d_list_del(&dfcc->dfcc_entry);
			D_FREE(dfcc);
		}
		D_FREE(dfp);
	}
}

static void
ph_free(struct d_hash_table *htable, d_list_t *link)
{
	_ph_free(htable->ht_priv, container_of(link, struct dfuse_pool, dfp_entry), true);
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

	return ch_key_hash(NULL, &dfc->dfc_uuid, sizeof(dfc->dfc_uuid));
}

static bool
ch_key_cmp(struct d_hash_table *htable, d_list_t *link, const void *key, unsigned int ksize)
{
	struct dfuse_cont *dfc;

	dfc = container_of(link, struct dfuse_cont, dfs_entry);
	return uuid_compare(dfc->dfc_uuid, key) == 0;
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

#define STAT_COUNT(name, ...)                                                                      \
	tstats += atomic_fetch_add_relaxed(&dfc->dfs_stat_value[DS_##name], 0);

#define SHOW_STAT(name, ...)                                                                       \
	{                                                                                          \
		uint64_t value = atomic_fetch_add_relaxed(&dfc->dfs_stat_value[DS_##name], 0);     \
		if (value != 0)                                                                    \
			DFUSE_TRA_INFO(dfc, "%5.1f%% " #name " (%#lx)",                            \
				       (double)value / tstats * 100, value);                       \
	}

static void
container_stats_log(struct dfuse_cont *dfc)
{
	uint64_t tstats = 0;

	D_FOREACH_DFUSE_STATX(STAT_COUNT);
	D_FOREACH_DFUSE_STATX(SHOW_STAT);
}

static void
_ch_free(struct dfuse_info *dfuse_info, struct dfuse_cont *dfc, bool used)
{
	struct dfuse_pool *dfp  = dfc->dfs_dfp;
	bool               keep = used;

	if (dfuse_info->di_shutdown)
		keep = false;

	if (!dfc->dfc_save_ino) {
		dfc->dfs_ino = 0;
		keep         = false;
	}

	if (daos_handle_is_valid(dfc->dfs_coh)) {
		int rc;

		rc = dfs_umount(dfc->dfs_ns);
		if (rc != 0)
			DHS_ERROR(dfc, rc, "dfs_umount() failed");

		rc = daos_cont_close(dfc->dfs_coh, NULL);
		if (rc == -DER_SUCCESS)
			dfc->dfs_coh = DAOS_HDL_INVAL;
		else {
			keep = true;
			DHL_ERROR(dfc, rc, "daos_cont_close() failed");
		}
	}

	atomic_fetch_sub_relaxed(&dfuse_info->di_container_count, 1);

	ival_dec_cont_buckets(dfc);

	container_stats_log(dfc);

	/* If the container was allocated a fresh inode number or has a open container handle
	 * then keep a copy, else discard it.
	 */
	if (keep) {
		struct dfuse_cont_core *dfcc;
		void                   *old = dfc;

		D_REALLOC(dfcc, old, sizeof(*dfc), sizeof(*dfcc));
		if (dfcc == NULL)
			dfcc = &dfc->core;

		D_SPIN_LOCK(&dfuse_info->di_lock);
		d_list_add(&dfcc->dfcc_entry, &dfp->dfp_historic);
		D_SPIN_UNLOCK(&dfuse_info->di_lock);
	}

	/* Do not drop the reference on the poool until after adding to the historic list */
	d_hash_rec_decref(&dfuse_info->di_pool_table, &dfp->dfp_entry);

	if (!keep)
		D_FREE(dfc);
}

static void
ch_free(struct d_hash_table *htable, d_list_t *link)
{
	_ch_free(htable->ht_priv, container_of(link, struct dfuse_cont, dfs_entry), true);
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
dfuse_pool_connect(struct dfuse_info *dfuse_info, const char *label, struct dfuse_pool **_dfp)
{
	struct dfuse_pool *dfp;
	d_list_t          *rlink;
	int                rc;
	int                ret;

	D_ALLOC_PTR(dfp);
	if (dfp == NULL)
		D_GOTO(err, rc = ENOMEM);

	atomic_init(&dfp->dfp_ref, 1);
	D_INIT_LIST_HEAD(&dfp->dfp_historic);

	DFUSE_TRA_UP(dfp, dfuse_info, "dfp");

	/* Handle the case where no identifier is supplied, this is for when dfuse
	 * is started without any pool on the command line.
	 */
	if (label) {
		daos_pool_info_t p_info = {};

		rc = daos_pool_connect(label, dfuse_info->di_group, DAOS_PC_RO, &dfp->dfp_poh,
				       &p_info, NULL);
		if (rc) {
			if (rc == -DER_NO_PERM || rc == -DER_NONEXIST)
				DHL_INFO(dfp, rc, "daos_pool_connect() failed");
			else
				DHL_ERROR(dfp, rc, "daos_pool_connect() failed");
			D_GOTO(err_free, rc = daos_der2errno(rc));
		}

		uuid_copy(dfp->dfp_uuid, p_info.pi_uuid);
	}

	rc = d_hash_table_create(D_HASH_FT_LRU | D_HASH_FT_EPHEMERAL, 3, dfuse_info, &cont_hops,
				 &dfp->dfp_cont_table);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(dfp, "Failed to create hash table: " DF_RC, DP_RC(rc));
		D_GOTO(err_disconnect, rc = daos_der2errno(rc));
	}

	atomic_fetch_add_relaxed(&dfuse_info->di_pool_count, 1);

	rlink = d_hash_rec_find_insert(&dfuse_info->di_pool_table, &dfp->dfp_uuid,
				       sizeof(dfp->dfp_uuid), &dfp->dfp_entry);

	if (rlink != &dfp->dfp_entry) {
		DFUSE_TRA_DEBUG(dfp, "Found existing pool, reusing");
		_ph_free(dfuse_info, dfp, false);
		dfp = container_of(rlink, struct dfuse_pool, dfp_entry);
	}

	DFUSE_TRA_DEBUG(dfp, "Returning dfp for " DF_UUID, DP_UUID(dfp->dfp_uuid));

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
dfuse_cont_get_cache(struct dfuse_info *dfuse_info, struct dfuse_cont *dfc)
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
			} else if (strncasecmp(buff_addrs[i], "otoc", sizes[i]) == 0) {
				dfc->dfc_data_otoc = true;
				DFUSE_TRA_INFO(dfc, "setting '%s' is open-to-close",
					       cont_attr_names[i]);
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

	if (dfc->dfc_data_timeout != 0 && dfuse_info->di_wb_cache)
		dfc->dfc_wb_cache = true;
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

/*
 * Return a container connection by uuid.
 *
 * Reuse an existing connection if possible, otherwise open new connection and setup dfs.
 *
 * If called from dfuse_cont_open_by_label() _dfs will be a valid pointer, with dfs_ns and dfs_coh
 * set already.  Failure in this case will result in the memory being freed.
 *
 * If successful will pass out a dfs pointer, with one reference held.
 *
 * Return code is a system errno.
 */
int
dfuse_cont_open(struct dfuse_info *dfuse_info, struct dfuse_pool *dfp, const char *label,
		daos_epoch_t snap_epoch, const char *snap_name, struct dfuse_cont **_dfc)
{
	struct dfuse_cont *dfc;
	d_list_t          *rlink;
	int                rc = 0;

	D_ALLOC_PTR(dfc);
	if (!dfc)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(dfc, dfp, "dfc");

	atomic_init(&dfc->dfs_ref, 1);

	dfc->dfs_dfp = dfp;

	/* Allow for label to be NULL, in which case this represents a pool */
	if (label == NULL) {
		if (uuid_is_null(dfp->dfp_uuid)) {
			/* This represents the root of the mount where no pool is set so entries
			 * in the directory will be pool uuids only.
			 */
			dfc->dfs_ops = &dfuse_pool_ops;
			dfc->dfs_ino = 1;
		} else {
			/* This represents the case where a pool is being accessed without a
			 * container, so either just a pool is specified or neither is and this is
			 * a second level directory.  If this is a second level directory then it
			 * could expire and be re-accessed so save the allocated inode.
			 */
			struct dfuse_pool *dfpp;

			dfc->dfs_ops = &dfuse_cont_ops;

			D_SPIN_LOCK(&dfuse_info->di_lock);
			d_list_for_each_entry(dfpp, &dfuse_info->di_pool_historic, dfp_entry) {
				struct dfuse_cont_core *dfcc;

				if (uuid_compare(dfpp->dfp_uuid, dfp->dfp_uuid) != 0)
					continue;

				d_list_for_each_entry(dfcc, &dfpp->dfp_historic, dfcc_entry) {
					if (dfcc->dfcc_ino == 0)
						continue;
					if (!uuid_is_null(dfcc->dfcc_uuid))
						continue;
					dfc->dfs_ino = dfcc->dfcc_ino;
					break;
				}
				if (dfc->dfs_ino != 0)
					break;
			}
			D_SPIN_UNLOCK(&dfuse_info->di_lock);
		}

		dfc->dfc_attr_timeout       = 307;
		dfc->dfc_dentry_dir_timeout = 307;
		dfc->dfc_ndentry_timeout    = 307;

		rc = ival_add_cont_buckets(dfc);
		if (rc != 0)
			goto err_free;

	} else {
		daos_cont_info_t        c_info = {};
		struct dfuse_cont_core *dfcc;
		int                     dfs_flags = O_RDWR;

		dfc->dfs_ops = &dfuse_dfs_ops;
		if (dfuse_info->di_read_only) {
			dfs_flags = O_RDONLY;
			rc        = daos_cont_open(dfp->dfp_poh, label, DAOS_COO_RO, &dfc->dfs_coh,
						   &c_info, NULL);
		} else {
			rc = daos_cont_open(dfp->dfp_poh, label, DAOS_COO_RW, &dfc->dfs_coh,
					    &c_info, NULL);
			if (rc == -DER_NO_PERM) {
				dfs_flags = O_RDONLY;
				rc = daos_cont_open(dfp->dfp_poh, label, DAOS_COO_RO, &dfc->dfs_coh,
						    &c_info, NULL);
			}
		}
		if (rc != -DER_SUCCESS) {
			if (rc == -DER_NONEXIST || rc == -DER_NO_PERM)
				DHL_INFO(dfc, rc, "daos_cont_open() failed");
			else
				DHL_ERROR(dfc, rc, "daos_cont_open() failed");
			D_GOTO(err_free, rc = daos_der2errno(rc));
		}

		if (snap_epoch != 0 || snap_name != NULL)
			rc = dfs_mount_snap(dfp->dfp_poh, dfc->dfs_coh, dfs_flags, snap_epoch,
					    snap_name, &dfc->dfs_ns);
		else
			rc = dfs_mount(dfp->dfp_poh, dfc->dfs_coh, dfs_flags, &dfc->dfs_ns);
		if (rc) {
			DHS_ERROR(dfc, rc, "dfs mount() failed");
			D_GOTO(err_close, rc);
		}

		uuid_copy(dfc->dfc_uuid, c_info.ci_uuid);

		if (dfuse_info->di_caching) {
			rc = dfuse_cont_get_cache(dfuse_info, dfc);
			if (rc == ENODATA) {
				DFUSE_TRA_INFO(dfc, "Using default caching values");
				dfuse_set_default_cont_cache_values(dfc);
			} else if (rc != 0) {
				D_GOTO(err_umount, rc);
			}
		}

		rc = ival_add_cont_buckets(dfc);
		if (rc != 0)
			goto err_umount;

		/* Check if this container has been accessed in the past and if so then reuse the
		 * inode number.
		 */
		D_SPIN_LOCK(&dfuse_info->di_lock);
		d_list_for_each_entry(dfcc, &dfp->dfp_historic, dfcc_entry) {
			if (dfcc->dfcc_ino == 0)
				continue;
			if (uuid_compare(dfcc->dfcc_uuid, dfc->dfc_uuid) != 0)
				continue;
			dfc->dfs_ino = dfcc->dfcc_ino;
			break;
		}
		if (dfc->dfs_ino == 0) {
			struct dfuse_pool *dfpp;

			DFUSE_TRA_INFO(dfc, "Looking for inode");

			d_list_for_each_entry(dfpp, &dfuse_info->di_pool_historic, dfp_entry) {
				DFUSE_TRA_INFO(dfc, "Looking for inode " DF_UUID,
					       DP_UUID(dfpp->dfp_uuid));

				if (uuid_compare(dfpp->dfp_uuid, dfp->dfp_uuid) != 0)
					continue;

				d_list_for_each_entry(dfcc, &dfpp->dfp_historic, dfcc_entry) {
					DFUSE_TRA_INFO(
					    dfc, "Looking for inode " DF_UUID " " DF_UUID,
					    DP_UUID(dfpp->dfp_uuid), DP_UUID(dfcc->dfcc_uuid));
					if (dfcc->dfcc_ino == 0)
						continue;
					if (uuid_compare(dfcc->dfcc_uuid, dfc->dfc_uuid) != 0)
						continue;
					dfc->dfs_ino = dfcc->dfcc_ino;
					break;
				}
			}
		}
		D_SPIN_UNLOCK(&dfuse_info->di_lock);
	}

	DFUSE_TRA_DEBUG(dfp, "New cont " DF_UUIDF " in pool " DF_UUIDF, DP_UUID(dfc->dfc_uuid),
			DP_UUID(dfp->dfp_uuid));

	if (dfc->dfs_ino == 0) {
		dfc->dfs_ino      = atomic_fetch_add_relaxed(&dfuse_info->di_ino_next, 1);
		dfc->dfc_save_ino = true;
		DFUSE_TRA_INFO(dfc, "Assigned new inode number %ld", dfc->dfs_ino);

	} else {
		DFUSE_TRA_INFO(dfc, "Reusing inode number %ld", dfc->dfs_ino);
	}

	/* Take a reference on the pool */
	d_hash_rec_addref(&dfuse_info->di_pool_table, &dfp->dfp_entry);

	atomic_fetch_add_relaxed(&dfuse_info->di_container_count, 1);

	/* Finally insert into the hash table.  This may return an existing
	 * container if there is a race to insert, so if that happens
	 * just use that one.
	 */
	rlink = d_hash_rec_find_insert(dfp->dfp_cont_table, &dfc->dfc_uuid, sizeof(dfc->dfc_uuid),
				       &dfc->dfs_entry);
	if (rlink != &dfc->dfs_entry) {
		DFUSE_TRA_DEBUG(dfp, "Found existing container, reusing");

		_ch_free(dfuse_info, dfc, false);

		dfc = container_of(rlink, struct dfuse_cont, dfs_entry);
		DFUSE_TRA_DEBUG(dfc, "Returning dfs for " DF_UUID " ref %d", DP_UUID(dfc->dfc_uuid),
				dfc->dfs_ref);
	}
	*_dfc = dfc;

	return rc;
err_umount:
	(void)dfs_umount(dfc->dfs_ns);
err_close:
	(void)daos_cont_close(dfc->dfs_coh, NULL);
err_free:
	D_FREE(dfc);
err:
	return rc;
}

int
dfuse_cont_get_handle(struct dfuse_info *dfuse_info, struct dfuse_pool *dfp, uuid_t cont,
		      struct dfuse_cont **_dfc)
{
	d_list_t *rlink;

	rlink = d_hash_rec_find(dfp->dfp_cont_table, cont, sizeof(*cont));
	if (rlink) {
		*_dfc = container_of(rlink, struct dfuse_cont, dfs_entry);
		return 0;
	}

	if (uuid_is_null(cont)) {
		return dfuse_cont_open(dfuse_info, dfp, NULL, 0, NULL, _dfc);
	} else {
		char uuid_str[37];

		uuid_unparse(cont, uuid_str);
		return dfuse_cont_open(dfuse_info, dfp, uuid_str, 0, NULL, _dfc);
	}
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

		DFUSE_TRA_DEBUG(ie, "Allowing cache use, time remaining: %.1lf", time_left);

		if (timeout)
			*timeout = time_left;
	}

	return use;
}

bool
dfuse_dentry_get_valid(struct dfuse_inode_entry *ie, double max_age, double *timeout)
{
	bool            use = false;
	struct timespec now;
	struct timespec left;
	double          time_left;

	D_ASSERT(max_age != -1);
	D_ASSERT(max_age >= 0);

	if (ie->ie_dentry_last_update.tv_sec == 0)
		return false;

	clock_gettime(CLOCK_MONOTONIC_COARSE, &now);

	left.tv_sec  = now.tv_sec - ie->ie_dentry_last_update.tv_sec;
	left.tv_nsec = now.tv_nsec - ie->ie_dentry_last_update.tv_nsec;
	if (left.tv_nsec < 0) {
		left.tv_sec--;
		left.tv_nsec += 1000000000;
	}
	time_left = max_age - (left.tv_sec + ((double)left.tv_nsec / 1000000000));
	if (time_left > 0)
		use = true;

	if (use && timeout)
		*timeout = time_left;

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
dfuse_fs_init(struct dfuse_info *dfuse_info)
{
	int rc;
	int i;

	D_ALLOC_ARRAY(dfuse_info->di_eqt, dfuse_info->di_eq_count);
	if (dfuse_info->di_eqt == NULL)
		D_GOTO(err, rc = -DER_NOMEM);

	D_INIT_LIST_HEAD(&dfuse_info->di_pool_historic);

	atomic_init(&dfuse_info->di_inode_count, 0);
	atomic_init(&dfuse_info->di_fh_count, 0);
	atomic_init(&dfuse_info->di_pool_count, 0);
	atomic_init(&dfuse_info->di_container_count, 0);

	rc = d_hash_table_create_inplace(D_HASH_FT_LRU | D_HASH_FT_EPHEMERAL, 3, dfuse_info,
					 &pool_hops, &dfuse_info->di_pool_table);
	if (rc != 0)
		D_GOTO(err, rc);

	rc = d_hash_table_create_inplace(D_HASH_FT_LRU | D_HASH_FT_EPHEMERAL, 16, dfuse_info,
					 &ie_hops, &dfuse_info->dpi_iet);
	if (rc != 0)
		D_GOTO(err_pt, rc);

	rc = ival_init(dfuse_info);
	if (rc != 0)
		D_GOTO(err_it, rc = d_errno2der(rc));

	atomic_init(&dfuse_info->di_ino_next, 2);
	atomic_init(&dfuse_info->di_eqt_idx, 0);

	D_SPIN_INIT(&dfuse_info->di_lock, 0);

	D_RWLOCK_INIT(&dfuse_info->di_forget_lock, 0);

	for (i = 0; i < dfuse_info->di_eq_count; i++) {
		struct dfuse_eq *eqt = &dfuse_info->di_eqt[i];

		eqt->de_handle = dfuse_info;

		DFUSE_TRA_UP(eqt, dfuse_info, "event_queue");

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

	dfuse_info->di_shutdown = false;
	return rc;

err_eq:
	D_SPIN_DESTROY(&dfuse_info->di_lock);
	D_RWLOCK_DESTROY(&dfuse_info->di_forget_lock);

	for (i = 0; i < dfuse_info->di_eq_count; i++) {
		struct dfuse_eq *eqt = &dfuse_info->di_eqt[i];
		int              rc2;

		if (daos_handle_is_inval(eqt->de_eq))
			continue;

		rc2 = daos_eq_destroy(eqt->de_eq, 0);
		if (rc2 != -DER_SUCCESS)
			DFUSE_TRA_ERROR(eqt, "Failed to destroy event queue:" DF_RC, DP_RC(rc2));

		sem_destroy(&eqt->de_sem);
		DFUSE_TRA_DOWN(eqt);
	}

	ival_thread_stop();
	ival_fini();
err_it:
	d_hash_table_destroy_inplace(&dfuse_info->dpi_iet, false);
err_pt:
	d_hash_table_destroy_inplace(&dfuse_info->di_pool_table, false);
err:
	D_FREE(dfuse_info->di_eqt);
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
	atomic_init(&ie->ie_linear_read, true);
	atomic_fetch_add_relaxed(&dfuse_info->di_inode_count, 1);
	D_INIT_LIST_HEAD(&ie->ie_evict_entry);
	D_RWLOCK_INIT(&ie->ie_wlock, 0);
}

void
dfuse_ie_close(struct dfuse_info *dfuse_info, struct dfuse_inode_entry *ie)
{
	int      rc;
	uint32_t ref;

	ival_drop_inode(ie);

	ref = atomic_load_relaxed(&ie->ie_ref);
	DFUSE_TRA_DEBUG(ie, "closing, inode %#lx ref %u, name " DF_DE ", parent %#lx",
			ie->ie_stat.st_ino, ref, DP_DE(ie->ie_name), ie->ie_parent);

	D_ASSERTF(ref == 0, "Reference is %d", ref);
	D_ASSERTF(atomic_load_relaxed(&ie->ie_il_count) == 0, "il_count is %d",
		  atomic_load_relaxed(&ie->ie_il_count));
	D_ASSERTF(atomic_load_relaxed(&ie->ie_open_count) == 0, "open_count is %d",
		  atomic_load_relaxed(&ie->ie_open_count));
	D_ASSERT(!ie->ie_active);

	if (ie->ie_obj) {
		rc = dfs_release(ie->ie_obj);
		if (rc)
			DHS_ERROR(ie, rc, "dfs_release() failed");
	}

	if (ie->ie_root) {
		struct dfuse_cont *dfc = ie->ie_dfs;
		struct dfuse_pool *dfp = dfc->dfs_dfp;

		DFUSE_TRA_DEBUG(ie, "Closing poh %d coh %d", daos_handle_is_valid(dfp->dfp_poh),
				daos_handle_is_valid(dfc->dfs_coh));
		d_hash_rec_decref(dfp->dfp_cont_table, &dfc->dfs_entry);
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
dfuse_read_event_size(void *arg, size_t size)
{
	struct dfuse_event *ev = arg;
	int                 rc;

	if (ev->de_iov.iov_buf == NULL) {
		D_ALLOC_NZ(ev->de_iov.iov_buf, size);
		if (ev->de_iov.iov_buf == NULL)
			return false;

		ev->de_iov.iov_buf_len = size;
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
dfuse_pre_read_event_reset(void *arg)
{
	return dfuse_read_event_size(arg, DFUSE_MAX_PRE_READ);
}

static bool
dfuse_read_event_reset(void *arg)
{
	return dfuse_read_event_size(arg, DFUSE_MAX_READ);
}

static bool
dfuse_write_event_reset(void *arg)
{
	struct dfuse_event *ev = arg;
	int                 rc;

	if (ev->de_iov.iov_buf == NULL) {
		D_ALLOC_NZ(ev->de_iov.iov_buf, DFUSE_MAX_READ);
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
dfuse_fs_start(struct dfuse_info *dfuse_info, struct dfuse_cont *dfs)
{
	struct fuse_args          args     = {0};
	struct dfuse_inode_entry *ie;
	struct d_slab_reg         read_slab  = {.sr_init    = dfuse_event_init,
						.sr_reset   = dfuse_read_event_reset,
						.sr_release = dfuse_event_release,
						POOL_TYPE_INIT(dfuse_event, de_list)};
	struct d_slab_reg         pre_read_slab = {.sr_init    = dfuse_event_init,
						   .sr_reset   = dfuse_pre_read_event_reset,
						   .sr_release = dfuse_event_release,
						   POOL_TYPE_INIT(dfuse_event, de_list)};
	struct d_slab_reg         write_slab = {.sr_init    = dfuse_event_init,
						.sr_reset   = dfuse_write_event_reset,
						.sr_release = dfuse_event_release,
						POOL_TYPE_INIT(dfuse_event, de_list)};
	int                       rc;
	int                       idx = 0;

	args.argc = 5;

	if (dfuse_info->di_read_only)
		args.argc++;

	if (dfuse_info->di_multi_user)
		args.argc++;

	/* These allocations are freed later by libfuse so do not use the
	 * standard allocation macros
	 */
	args.allocated = 1;
	args.argv      = calloc(args.argc, sizeof(*args.argv));
	if (!args.argv)
		D_GOTO(err, rc = -DER_NOMEM);

	args.argv[idx] = strdup("");
	if (!args.argv[idx])
		D_GOTO(err, rc = -DER_NOMEM);
	idx++;

	args.argv[idx] = strdup("-ofsname=dfuse");
	if (!args.argv[idx])
		D_GOTO(err, rc = -DER_NOMEM);
	idx++;

	args.argv[idx] = strdup("-osubtype=daos");
	if (!args.argv[idx])
		D_GOTO(err, rc = -DER_NOMEM);
	idx++;

	args.argv[idx] = strdup("-odefault_permissions");
	if (!args.argv[idx])
		D_GOTO(err, rc = -DER_NOMEM);
	idx++;

	args.argv[idx] = strdup("-onoatime");
	if (!args.argv[idx])
		D_GOTO(err, rc = -DER_NOMEM);
	idx++;

	if (dfuse_info->di_read_only) {
		args.argv[idx] = strdup("-oro");
		if (!args.argv[idx])
			D_GOTO(err, rc = -DER_NOMEM);
		idx++;
	}

	if (dfuse_info->di_multi_user) {
		args.argv[idx] = strdup("-oallow_other");
		if (!args.argv[idx])
			D_GOTO(err, rc = -DER_NOMEM);
		idx++;
	}

	D_ASSERT(idx == args.argc);

	/* Create the root inode and insert into table */
	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(err, rc = -DER_NOMEM);

	DFUSE_TRA_UP(ie, dfuse_info, "root_inode");

	ie->ie_dfs    = dfs;
	ie->ie_root   = true;
	ie->ie_parent = 1;
	dfuse_ie_init(dfuse_info, ie);

	if (dfs->dfs_ops == &dfuse_dfs_ops) {
		rc = dfs_lookup(dfs->dfs_ns, "/", O_RDWR, &ie->ie_obj, NULL, &ie->ie_stat);
		if (rc) {
			DHS_ERROR(ie, rc, "dfs_lookup() failed");
			D_GOTO(err_ie, rc = daos_errno2der(rc));
		}
	} else {
		ie->ie_stat.st_uid  = geteuid();
		ie->ie_stat.st_gid  = getegid();
		ie->ie_stat.st_mode = 0700 | S_IFDIR;
	}
	ie->ie_stat.st_ino = 1;
	dfs->dfs_ino       = ie->ie_stat.st_ino;

	rc = d_hash_rec_insert(&dfuse_info->dpi_iet, &ie->ie_stat.st_ino,
			       sizeof(ie->ie_stat.st_ino), &ie->ie_htl, false);
	D_ASSERT(rc == -DER_SUCCESS);

	rc = d_slab_init(&dfuse_info->di_slab, dfuse_info);
	if (rc != -DER_SUCCESS)
		D_GOTO(err_ie_remove, rc);

	for (int i = 0; i < dfuse_info->di_eq_count; i++) {
		struct dfuse_eq *eqt = &dfuse_info->di_eqt[i];

		rc = d_slab_register(&dfuse_info->di_slab, &read_slab, eqt, &eqt->de_read_slab);
		if (rc != -DER_SUCCESS)
			D_GOTO(err_threads, rc);

		rc = d_slab_register(&dfuse_info->di_slab, &pre_read_slab, eqt,
				     &eqt->de_pre_read_slab);
		if (rc != -DER_SUCCESS)
			D_GOTO(err_threads, rc);

		d_slab_restock(eqt->de_read_slab);
		d_slab_restock(eqt->de_pre_read_slab);

		if (!dfuse_info->di_read_only) {
			rc = d_slab_register(&dfuse_info->di_slab, &write_slab, eqt,
					     &eqt->de_write_slab);
			if (rc != -DER_SUCCESS)
				D_GOTO(err_threads, rc);
			d_slab_restock(eqt->de_write_slab);
		}

		rc = pthread_create(&eqt->de_thread, NULL, dfuse_progress_thread, eqt);
		if (rc != 0)
			D_GOTO(err_threads, rc = daos_errno2der(rc));

		pthread_setname_np(eqt->de_thread, "dfuse progress");
	}

	rc = dfuse_launch_fuse(dfuse_info, &args);
	if (rc == -DER_SUCCESS) {
		fuse_opt_free_args(&args);
		return rc;
	}

err_threads:
	dfuse_info->di_shutdown = true;

	for (int i = 0; i < dfuse_info->di_eq_count; i++) {
		struct dfuse_eq *eqt = &dfuse_info->di_eqt[i];

		if (!eqt->de_thread)
			continue;

		sem_post(&eqt->de_sem);
		pthread_join(eqt->de_thread, NULL);
		sem_destroy(&eqt->de_sem);
	}

	d_slab_destroy(&dfuse_info->di_slab);
err_ie_remove:
	dfs_release(ie->ie_obj);
	d_hash_rec_delete_at(&dfuse_info->dpi_iet, &ie->ie_htl);
err_ie:
	dfuse_ie_free(dfuse_info, ie);
err:
	DFUSE_TRA_ERROR(dfuse_info, "Failed to start dfuse, rc: " DF_RC, DP_RC(rc));
	fuse_opt_free_args(&args);
	return rc;
}

static int
ino_dfs_flush(d_list_t *rlink, void *arg)
{
	struct dfuse_info        *dfuse_info = arg;
	struct dfuse_inode_entry *ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	d_list_del(&ie->ie_htl);

	atomic_store_relaxed(&ie->ie_ref, 0);

	dfuse_ie_close(dfuse_info, ie);

	return -DER_SUCCESS;
}

static int
ino_dfs_flush_nr(d_list_t *rlink, void *arg)
{
	struct dfuse_info        *dfuse_info = arg;
	struct dfuse_inode_entry *ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (ie->ie_root)
		return 0;

	d_list_del(&ie->ie_htl);

	atomic_store_relaxed(&ie->ie_ref, 0);

	dfuse_ie_close(dfuse_info, ie);

	return -DER_SUCCESS;
}

static int
ino_kernel_flush(d_list_t *rlink, void *arg)
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
		DHS_WARN(ie, -rc, "%#lx %#lx " DF_DE, ie->ie_parent, ie->ie_stat.st_ino,
			 DP_DE(ie->ie_name));
	else
		DHS_INFO(ie, -rc, "%#lx %#lx " DF_DE, ie->ie_parent, ie->ie_stat.st_ino,
			 DP_DE(ie->ie_name));

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

	DFUSE_TRA_ERROR(dfc, "Failed to close cont ref %d " DF_UUID, dfc->dfs_ref,
			DP_UUID(dfc->dfc_uuid));
	return 0;
}

/* Called during shutdown on still-open pools.  We've already stopped
 * taking requests from the kernel at this point and joined all the thread
 * as well as drained the inode table and dropped all references held there
 * so anything still held at this point represents a reference leak and a dfuse bug.
 *
 * As such log what we have as an error, and attempt to close/free everything.
 * the dfp itself will remain allocated as well as some hash table metadata.
 */
static int
dfuse_pool_close_cb(d_list_t *rlink, void *handle)
{
	struct dfuse_info *dfuse_info = handle;
	struct dfuse_cont_core *dfcc, *dfccn;
	struct dfuse_pool *dfp;
	int                rc;

	dfp = container_of(rlink, struct dfuse_pool, dfp_entry);

	DFUSE_TRA_ERROR(dfp, "Failed to close pool ref %d " DF_UUID, dfp->dfp_ref,
			DP_UUID(dfp->dfp_uuid));

	d_hash_table_traverse(dfp->dfp_cont_table, dfuse_cont_close_cb, handle);

	rc = d_hash_table_destroy(dfp->dfp_cont_table, true);
	if (rc != -DER_SUCCESS)
		DHL_ERROR(dfp, rc, "Failed to close cont table");

	if (daos_handle_is_valid(dfp->dfp_poh)) {
		rc = daos_pool_disconnect(dfp->dfp_poh, NULL);
		if (rc != -DER_SUCCESS)
			DHL_ERROR(dfp, rc, "daos_pool_disconnect() failed");
	}

	atomic_fetch_sub_relaxed(&dfuse_info->di_pool_count, 1);

	d_list_for_each_entry_safe(dfcc, dfccn, &dfp->dfp_historic, dfcc_entry) {
		d_list_del(&dfcc->dfcc_entry);
		D_FREE(dfcc);
	}

	d_list_del(&dfp->dfp_entry);
	D_FREE(dfp);

	return rc;
}

/* Called as part of shutdown, if the startup was successful.  Releases resources created during
 * operation.
 */
int
dfuse_fs_stop(struct dfuse_info *dfuse_info)
{
	struct dfuse_pool *dfp, *dfpp;
	int                rc;

	DFUSE_TRA_INFO(dfuse_info, "Flushing inode table");

	dfuse_info->di_shutdown = true;

	for (int i = 0; i < dfuse_info->di_eq_count; i++) {
		struct dfuse_eq *eqt = &dfuse_info->di_eqt[i];

		sem_post(&eqt->de_sem);
	}

	/* Stop and drain invalidation queues */
	ival_thread_stop();

	for (int i = 0; i < dfuse_info->di_eq_count; i++) {
		struct dfuse_eq *eqt = &dfuse_info->di_eqt[i];

		pthread_join(eqt->de_thread, NULL);

		sem_destroy(&eqt->de_sem);
	}

	/* First flush, instruct the kernel to forget items.  This will run and work in ideal cases
	 * but often if the filesystem is unmounted it'll abort part-way through.
	 */
	rc = d_hash_table_traverse(&dfuse_info->dpi_iet, ino_kernel_flush, dfuse_info);

	DHL_INFO(dfuse_info, rc, "Kernel flush complete");

	/* At this point there's a number of inodes which are in memory, traverse these and free
	 * them, along with any resources.
	 * The reference count on inodes match kernel references but the fuse module is disconnected
	 * at this point so simply set this to 0.
	 * Inodes do not hold a reference on their parent so removing a single entry should not
	 * affect other entries in the list, however pools and containers are more tricy, first
	 * iterate over the list and release inodes which are not the root of a container, then
	 * do a second pass and release everything.  This will ensure that all dfs objects in a
	 * container are released before dfs_umount() is called.
	 */
	DFUSE_TRA_INFO(dfuse_info, "Draining inode table");

	rc = d_hash_table_traverse(&dfuse_info->dpi_iet, ino_dfs_flush_nr, dfuse_info);

	DHL_INFO(dfuse_info, rc, "First flush complete");

	/* Second pass, this should close all inodes which are the root of containers and therefore
	 * close all containers and pools.
	 */
	rc = d_hash_table_traverse(&dfuse_info->dpi_iet, ino_dfs_flush, dfuse_info);

	DHL_INFO(dfuse_info, rc, "Second flush complete");

	d_list_for_each_entry_safe(dfp, dfpp, &dfuse_info->di_pool_historic, dfp_entry) {
		struct dfuse_cont_core *dfcc, *dfccn;

		if (daos_handle_is_valid(dfp->dfp_poh)) {
			rc = daos_pool_disconnect(dfp->dfp_poh, NULL);
			if (rc != -DER_SUCCESS) {
				DHL_ERROR(dfp, rc, "daos_pool_disconnect() failed");
			}
		}

		d_list_for_each_entry_safe(dfcc, dfccn, &dfp->dfp_historic, dfcc_entry) {
			d_list_del(&dfcc->dfcc_entry);
			D_FREE(dfcc);
		}

		D_FREE(dfp);
	}

	/* This hash table should now be empty, but check it anyway and fail if any entries */
	rc = d_hash_table_traverse(&dfuse_info->di_pool_table, dfuse_pool_close_cb, dfuse_info);
	DHL_INFO(dfuse_info, rc, "Handle flush complete");

	ival_fini();

	d_slab_destroy(&dfuse_info->di_slab);

	return rc;
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
		DHL_WARN(dfuse_info, rc2, "Failed to destroy pool hash table");
		if (rc == -DER_SUCCESS)
			rc = rc2;
	}

	return rc;
}
