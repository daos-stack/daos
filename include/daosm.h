/*
 * Proof-of-Concept DAOS-M API
 */

#include <daos_types.h>

/*
 * Container Create/Open/Close/Query
 */

/* Container information */
typedef struct {
	/* ... */
	unsigned int		ci_nshards;
	unsigned int		ci_ndisabled;
	unsigned int		ci_nsnapshots;
	daosm_epoch_info_t	ci_epoch_info;
	/* ... */
} daosm_co_info_t;

/*
 * Create a container, without opening it.  For cases in which a container only
 * needs to be created but not opened.
 *
 *	uuid	IN  container UUID
 *	shards	IN  set of shards to create container on
 *	cshards	IN  consensus subset of shards
 *	event	IN  completion event
 */
int
daosm_co_create(uuid_t uuid, daos_rank_group_t *shards,
		daos_rank_group_t *cshards, daos_event_t *event);

/*
 * Open a container, optionally creating it first.  See also daosm_co_create().
 *
 *	uuid	IN  container UUID
 *	shards	IN  hint of shards, or if mode contains create, set of
 *		    shards to create
 *	cshards	IN  unused, or if mode contains create, consensus subset of
 *		    shards
 *	mode	IN  read-only, read-write, and optionally also create
 *	handle	OUT container handle
 *	status	OUT container status
 *	event	IN  completion event
 */
int
daosm_co_open(uuid_t uuid, daos_rank_group_t *shards,
	      daos_rank_group_t *cshards, unsigned int mode,
	      daos_handle_t *handle, daosm_co_status_t *status,
	      daos_event_t *event);

/*
 * Close a container handle.
 *
 *	handle	IN  container handle
 *	event	IN  completion event
 */
int
daosm_co_close(daos_handle_t handle, daos_event_t *event);

/*
 * Destroy a container.
 *
 *	uuid	IN  container UUID
 *	shards	IN  hint of shards
 *	event	IN  completion event
 */
int
daosm_co_destory(uuid_t uuid, daos_rank_group_t *shards, daos_event_t *event);

/*
 * Query a container's various information.
 *
 *	handle		IN  container handle
 *	info		OUT container information
 *	shards		OUT list of all shards
 *	disabled	OUT list of indices of disabled shards
 *	n		OUT number of indices in disabled
 *	event		IN  completion event
 */
int
daosm_co_query(daos_handle_t handle, daosm_co_info_t *info,
	       daos_rank_group_t *shards, unsigned int *disabled,
	       unsigned int *n, daos_event_t *event);

/*
 * Container Layout
 */

/*
 * Modify a container's layout.  Existing shards in disable are disabled and
 * new shards in add are appended to the list of shards in the order they
 * appear in add.  Disabling an unexistent shard or adding an existing shard
 * gets an error, with the layout left intact.
 *
 * XXX: Whether shard indices can be reused...
 *
 *	handle	IN  container handle
 *	disable	IN  set of existing shards to disable
 *	add	IN  list of new shards to add
 *	cadd	IN  subset of add that should be consensus shards
 *	event	IN  completion event
 */
int
daosm_co_reconfig(daos_handle_t handle, daos_rank_group_t *disable,
		  daos_rank_group_t *add, daos_rank_group_t *cadd,
		  daos_event_t *event);

/*
 * Container extended attribute
 *
 * An attribute is a name-value pair.  A name must be a '\0'-terminated string.
 * These attributes are not versioned.
 */

/*
 * List all attribute names in a buffer, with each name terminated by a '\0'.
 *
 *	handle	IN  container handle
 *	buffer	OUT buffer
 *	size	IN  buffer size
 *		OUT total size of all names (regardless of actual buffer size)
 *	event	IN  completion event
 */
int
daosm_co_xattr_list(daos_handle_t handle, char *buffer, size_t *size,
		    daos_event_t *event);

/*
 * Get a set of attributes.
 *
 *	handle	IN  container handle
 *	n	IN  number of attributes
 *	names	IN  array of attribute names
 *	buffers	OUT array of attribute values
 *	sizes	IN  array of buffer sizes
 *		OUT array of value sizes (regardless of actual buffer sizes)
 *	event	IN  completion event
 */
int
daosm_co_xattr_get(daos_handle_t handle, unsigned int n, char **names,
		   void **buffers, size_t **sizes, daos_event_t *event);

/*
 * Set a set of attributes.
 *
 *	handle	IN  container handle
 *	n	IN  number of attributes
 *	names	IN  array of attribute names
 *	values	IN  array of attribute values
 *	sizes	IN  array of value sizes
 *	event	IN  completion event
 */
int
daosm_co_xattr_set(daos_handle_t handle, unsigned int n, char **names,
		   void **values, size_t *sizes, daos_event_t *event);

/*
 * Epoch
 */

/* Epoch information */
typedef struct daosm_epoch_info {
	daos_epoch_t	ei_hce;
	/* Lowest referenced epoch of current container handle */
	daos_epoch_t	ei_lre;
	/* Highest referenced epoch of current container handle */
	daos_epoch_t	ei_hre;
	/* ... */
} daosm_epoch_info_t;

/*
 * Query latest epoch information.
 *
 *	handle	IN  container handle
 *	info	OUT latest epoch information
 *	event	IN  completion event
 */
int
daosm_epoch_query(daos_handle_t handle, daosm_epoch_info_t *info,
		  daos_event_t *event);

/*
 * Change a container handle's current range of referenced epochs [l, h], where
 * l <= HCE <= h.  The resulting range [l', h'] is determined this way:
 *
 *	l' = min(HCE, max(l, lowest))
 *	h' = max(HCE, highest)
 *
 * The container handle is responsible of committing all uncommitted epochs it
 * has held.
 *
 *	handle	IN  container handle
 *	lowest	IN  new lowest referenced epoch
 *	highest	IN  new highest referenced epoch
 *	info	OUT resulting epoch information
 *	event	IN  completion event
 */
int
daosm_epoch_hold(daos_handle_t handle, daos_epoch_t lowest,
		 daos_epoch_t highest, daosm_epoch_info_t *info,
		 daos_event_t *event);

/*
 * Commit an epoch.  The epoch must have already been held by handle.
 *
 *	handle		IN  container handle
 *	epoch		IN  epoch to commit
 *	depends		IN  epochs the committing epoch depends on
 *	ndepends	IN  number of epochs in depends
 *	event		IN  completion event
 */
int
daosm_epoch_commit(daos_handle_t handle, daos_epoch_t epoch,
		   daos_epoch_t *depends, int ndepends, daos_event_t *event);

/*
 * Abort an epoch.  The epoch must have already been held by handle.
 *
 *	handle	IN  container handle
 *	epoch	IN  epoch to abort
 *	nepochs	IN  number of epochs in epochs
 *	event	IN  completion event
 */
int
daosm_epoch_abort(daos_handle_t handle, daos_epoch_t *epochs, int nepochs,
		  daos_event_t *event);

/*
 * Wait for an epoch to be committed.
 *
 *	handle	IN  container handle
 *	epoch	IN  epoch to wait
 *	info	OUT latest epoch information
 *	event	IN  completion event
 */
int
daosm_epoch_wait(daos_handle_t handle, daos_epoch_t epoch,
		 daosm_epoch_info_t *info, daos_event_t *event);

/*
 * Snapshot
 *
 * Snapshots are assumed to be nameless; they can only be referred to be the
 * epochs they correspond to.
 */

/*
 * List epochs of all snapshot of a container.
 *
 *	handle	IN  container handle
 *	buffer	IN  buffer to epochs
 *		OUT array of epochs of snapshots
 *	n	IN  number of epochs buffer can hold
 *		OUT number of all snapshots (regardless of buffer size)
 *	event	IN  completion event
 */
int
daosm_snap_list(daos_handle_t handle, daos_epoch_t *buffer, unsigned int *n,
		daos_event_t *event);

/*
 * Take a snapshot of an epoch.
 *
 *	handle	IN  container handle
 *	epoch	IN  epoch to snapshot
 *	event	IN  completion event
 */
int
daosm_snap_create(daos_handle_t handle, daos_epoch_t epoch,
		  daos_event_t *event);

/*
 * Destroy a snapshot.  The epoch corresponding to the snapshot is not
 * discarded, but may be aggregated.
 *
 *	handle	IN  container handle
 *	epoch	IN  snapshot to destory
 *	event	IN  completion event
 */
int
daosm_snap_destroy(daos_handle_t handle, daos_epoch_t epoch,
		   daos_event_t *event);
