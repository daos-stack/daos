/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * DAOS storage pool types and functions
 */
#ifndef __DAOS_POOL_H__
#define __DAOS_POOL_H__

#if defined(__cplusplus)
extern "C" {
#endif

/** Type of storage target */
typedef enum {
	DAOS_TP_UNKNOWN,
	/** Rotating disk */
	DAOS_TP_HDD,
	/** Flash-based */
	DAOS_TP_SSD,
	/** Persistent memory */
	DAOS_TP_PM,
	/** Volatile memory */
	DAOS_TP_VM,
} daos_target_type_t;

/** Current state of the storage target */
typedef enum {
	DAOS_TS_UNKNOWN,
	/* Not available */
	DAOS_TS_DOWN_OUT,
	/* Not available, may need rebuild */
	DAOS_TS_DOWN,
	/* Up */
	DAOS_TS_UP,
	/* Up and running */
	DAOS_TS_UP_IN,
	/* Intermediate state for pool map change */
	DAOS_TS_NEW,
	/* Being drained */
	DAOS_TS_DRAIN,
} daos_target_state_t;

/** Description of target performance */
typedef struct {
	/** TODO: storage/network bandwidth, latency etc */
	int			foo;
} daos_target_perf_t;

enum {
	DAOS_MEDIA_SCM	= 0,
	DAOS_MEDIA_NVME,
	DAOS_MEDIA_MAX
};

/** Pool target space usage information */
struct daos_space {
	/* Total space in bytes */
	uint64_t		s_total[DAOS_MEDIA_MAX];
	/* Free space in bytes */
	uint64_t		s_free[DAOS_MEDIA_MAX];
};

/** Target information */
typedef struct {
	daos_target_type_t	ta_type;
	daos_target_state_t	ta_state;
	daos_target_perf_t	ta_perf;
	struct daos_space	ta_space;
} daos_target_info_t;

/** Pool space usage information */
struct daos_pool_space {
	/* Aggregated space for all live targets */
	struct daos_space	ps_space;
	/* Min target free space in bytes */
	uint64_t		ps_free_min[DAOS_MEDIA_MAX];
	/* Max target free space in bytes */
	uint64_t		ps_free_max[DAOS_MEDIA_MAX];
	/* Average target free space in bytes */
	uint64_t		ps_free_mean[DAOS_MEDIA_MAX];
	/* Target(VOS) count */
	uint32_t		ps_ntargets;
	uint32_t		ps_padding;
};

struct daos_rebuild_status {
	/** pool map version in rebuilding or last completed rebuild */
	uint32_t		rs_version;
	/** Time (Seconds) for the rebuild */
	uint32_t		rs_seconds;
	/** errno for rebuild failure */
	int32_t			rs_errno;
	/**
	 * rebuild is done or not, it is valid only if @rs_version is non-zero
	 */
	int32_t			rs_done;

	/* padding of rebuild status */
	int32_t			rs_padding32;

	/* Failure on which rank */
	int32_t			rs_fail_rank;
	/** # total to-be-rebuilt objects, it's non-zero and increase when
	 * rebuilding in progress, when rs_done is 1 it will not change anymore
	 * and should equal to rs_obj_nr. With both rs_toberb_obj_nr and
	 * rs_obj_nr the user can know the progress of the rebuilding.
	 */
	uint64_t		rs_toberb_obj_nr;
	/** # rebuilt objects, it's non-zero only if rs_done is 1 */
	uint64_t		rs_obj_nr;
	/** # rebuilt records, it's non-zero only if rs_done is 1 */
	uint64_t		rs_rec_nr;

	/** rebuild space cost */
	uint64_t		rs_size;
};

/**
 * Pool info query bits.
 * The basic pool info like fields from pi_uuid to pi_leader will always be
 * queried for each daos_pool_query() calling. But the pi_space and
 * pi_rebuild_st are optional based on pi_mask's value.
 */
enum daos_pool_info_bit {
	/** true to query pool space usage */
	DPI_SPACE		= 1ULL << 0,
	/** true to query rebuild status */
	DPI_REBUILD_STATUS	= 1ULL << 1,
	/** query all above optional info */
	DPI_ALL			= -1,
};

/**
 * Storage pool
 */
typedef struct {
	/** Pool UUID */
	uuid_t				pi_uuid;
	/** Number of targets */
	uint32_t			pi_ntargets;
	/** Number of nodes */
	uint32_t			pi_nnodes;
	/** Number of deactivated targets */
	uint32_t			pi_ndisabled;
	/** Latest pool map version */
	uint32_t			pi_map_ver;
	/** current raft leader */
	uint32_t			pi_leader;
	/** pool info bits, see daos_pool_info_bit */
	uint64_t			pi_bits;
	/** Space usage */
	struct daos_pool_space		pi_space;
	/** rebuild status */
	struct daos_rebuild_status	pi_rebuild_st;
} daos_pool_info_t;

/** DAOS pool container information */
struct daos_pool_cont_info {
	uuid_t		pci_uuid;
	char		pci_label[DAOS_PROP_LABEL_MAX_LEN+1];
};

/**
 * Connect to the DAOS pool identified by \a pool, a label or UUID string.
 * Upon a successful completion, \a poh returns the pool handle, and \a info
 * returns the latest pool information.
 *
 * \param[in]	pool	label or UUID string to identify a pool.
 * \param[in]	sys	DAOS system name to use for the pool connect.
 *			Pass NULL to connect to the default system.
 * \param[in]	flags	Connect mode represented by the DAOS_PC_ bits.
 * \param[out]	poh	Returned open handle.
 * \param[in,out]
 *		info	Optional, returned pool information,
 *			see daos_pool_info_bit.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_PERM	Permission denied
 *			-DER_NONEXIST	Pool is nonexistent
 */
int
daos_pool_connect(const char *pool, const char *sys, unsigned int flags,
		  daos_handle_t *poh, daos_pool_info_t *info, daos_event_t *ev);

/**
 * Disconnect from the DAOS pool. It should revoke all the container open
 * handles of this pool.
 *
 * \param[in]	poh	Pool connection handle
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_HDL	Invalid pool handle
 */
int
daos_pool_disconnect(daos_handle_t poh, daos_event_t *ev);

/*
 * Handle API
 */

/**
 * Convert a local pool connection to global representation data which can be
 * shared with peer processes.
 * If glob->iov_buf is set to NULL, the actual size of the global handle is
 * returned through glob->iov_buf_len.
 * This function does not involve any communication and does not block.
 *
 * \param[in]	poh	Valid local pool connection handle to be shared
 * \param[out]	glob	Pointer to iov of the buffer to store handle information
 *
 * \return		These values will be returned:
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_HDL	Pool  handle is nonexistent
 *			-DER_TRUNC	Buffer in \a glob is too short, a larger
 *					buffer is required. In this case the
 *					required buffer size is returned through
 *					glob->iov_buf_len.
 */
int
daos_pool_local2global(daos_handle_t poh, d_iov_t *glob);

/**
 * Create a local pool connection for global representation data.
 *
 * \param[in]	glob	Global (shared) representation of a collective handle
 *			to be extracted
 * \param[out]	poh	Returned local pool connection handle
 *
 * \return		These values will be returned:
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 */
int
daos_pool_global2local(d_iov_t glob, daos_handle_t *poh);

/**
 * Query pool information. User should provide at least one of \a info and
 * \a tgts as output buffer.
 *
 * \param[in]	poh	Pool connection handle.
 * \param[out]	tgts	Optional, returned storage targets in this pool.
 * \param[in,out]
 *		info	Optional, returned pool information,
 *			see daos_pool_info_bit.
 * \param[out]	pool_prop
 *			Optional, returned pool properties.
 *			If it is NULL, then needs not query the properties.
 *			If pool_prop is non-NULL but its dpp_entries is NULL,
 *			will query all pool properties, DAOS internally
 *			allocates the needed buffers and assign pointer to
 *			dpp_entries.
 *			If pool_prop's dpp_nr > 0 and dpp_entries is non-NULL,
 *			will query the properties for specific dpe_type(s), DAOS
 *			internally allocates the needed buffer for dpe_str or
 *			dpe_val_ptr, if the dpe_type with immediate value then
 *			will directly assign it to dpe_val.
 *			User can free the associated buffer by calling
 *			daos_prop_free().
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_HDL	Invalid pool handle
 */
int
daos_pool_query(daos_handle_t poh, d_rank_list_t *tgts, daos_pool_info_t *info,
		daos_prop_t *pool_prop, daos_event_t *ev);

/**
 * Query information of storage targets within a DAOS pool.
 *
 * \param[in]	poh	Pool connection handle.
 * \param[in]	tgt	A single target index to query.
 * \param[in]	rank	Rank of the target index to query.
 * \param[out]	info	Returned storage information of \a tgt.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_HDL	Invalid pool handle
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NONEXIST	No pool on specified target
 */
int
daos_pool_query_target(daos_handle_t poh, uint32_t tgt, d_rank_t rank,
		       daos_target_info_t *info, daos_event_t *ev);

/**
 * List the names of all user-defined pool attributes.
 *
 * \param[in]	poh	Pool handle.
 * \param[out]	buffer	Buffer containing concatenation of all attribute
 *			names, each being null-terminated. No truncation is
 *			performed and only full names will be returned.
 *			NULL is permitted in which case only the aggregate
 *			size will be retrieved.
 * \param[in,out]
 *		size	[in]: Buffer size. [out]: Aggregate size of all
 *			attribute names (excluding terminating null
 *			characters), regardless of the actual buffer
 *			size.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_pool_list_attr(daos_handle_t poh, char *buffer, size_t *size,
		    daos_event_t *ev);

/**
 * Retrieve a list of user-defined pool attribute values.
 *
 * \param[in]	poh	Pool handle
 * \param[in]	n	Number of attributes
 * \param[in]	names	Array of \a n null-terminated attribute names.
 * \param[out]	buffers	Array of \a n buffers to store attribute values.
 *			Attribute values larger than corresponding buffer sizes
 *			will be truncated. NULL values are permitted and will be
 *			treated identical to zero-length buffers, in which case
 *			only the sizes of attribute values will be retrieved.
 * \param[in,out]
 *		sizes	[in]: Array of \a n buffer sizes. [out]: Array of actual
 *			sizes of \a n attribute values, regardless of given
 *			buffer sizes.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_pool_get_attr(daos_handle_t poh, int n, char const *const names[],
		   void *const buffers[], size_t sizes[], daos_event_t *ev);

/**
 * Create or update a list of user-defined pool attributes.
 *
 * \param[in]	poh	Pool handle
 * \param[in]	n	Number of attributes
 * \param[in]	names	Array of \a n null-terminated attribute names.
 * \param[in]	values	Array of \a n attribute values
 * \param[in]	sizes	Array of \a n elements containing the sizes of
 *			respective attribute values.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_pool_set_attr(daos_handle_t poh, int n, char const *const names[],
		   void const *const values[], size_t const sizes[],
		   daos_event_t *ev);

/**
 * Delete a list of user-defined pool attributes.
 *
 * \param[in]	poh	Pool handle
 * \param[in]	n	Number of attributes
 * \param[in]	names	Array of \a n null-terminated attribute names.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_NOMEM	Out of memory
 */
int
daos_pool_del_attr(daos_handle_t poh, int n, char const *const names[],
		   daos_event_t *ev);

/**
 * List a pool's containers.
 *
 * \param[in]	poh	Pool connection handle.
 * \param[in,out]
 *		ncont	[in] \a cbuf length in items.
 *			[out] Number of containers in the pool.
 * \param[out]	cbuf	Array of container structures.
 *			NULL is permitted in which case only the number
 *			of containers will be returned in \a ncont.
 * \param[in]	ev	Completion event. Optional and can be NULL.
 *			The function will run in blocking mode
 *			if \a ev is NULL.
 *
 * \return		0		Success
 *			-DER_TRUNC	\a cbuf cannot hold \a ncont items
 */
int
daos_pool_list_cont(daos_handle_t poh, daos_size_t *ncont,
		    struct daos_pool_cont_info *cbuf, daos_event_t *ev);

/**
 * Backward compatibility code.
 * Please don't use directly
 */
int
daos_pool_connect2(const char *pool, const char *sys, unsigned int flags,
		   daos_handle_t *poh, daos_pool_info_t *info, daos_event_t *ev);


#if defined(__cplusplus)
}
#define daos_pool_connect daos_pool_connect_cpp
static inline int
daos_pool_connect_cpp(const char *pool, const char *sys, unsigned int flags, daos_handle_t *poh,
		      daos_pool_info_t *info, daos_event_t *ev)
{
	return daos_pool_connect2(pool, sys, flags, poh, info, ev);
}

static inline int
daos_pool_connect_cpp(const uuid_t pool, const char *sys, unsigned int flags, daos_handle_t *poh,
		      daos_pool_info_t *info, daos_event_t *ev)
{
	char str[37];

	uuid_unparse(pool, str);
	return daos_pool_connect2(str, sys, flags, poh, info, ev);
}
#else
/**
 * For backward compatibility, support old API where a const uuid_t was used
 * instead of a string to identify the pool.
 */
#define daos_pool_connect(po, ...)					\
	({								\
		int _ret;						\
		char _str[37];						\
		const char *__str = NULL;				\
		if (d_is_string(po)) {					\
			__str = (const char *)(po);			\
		} else if (d_is_uuid(po)) {				\
			uuid_unparse((unsigned char *)(po), _str);	\
			__str = _str;					\
		}							\
		_ret = daos_pool_connect2(__str, __VA_ARGS__);		\
		_ret;							\
	})
#endif

#endif /* __DAOS_POOL_H__ */
