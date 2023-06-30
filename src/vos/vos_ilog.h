/**
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Incarnation log wrappers for fetching the log and checking existence
 * vos/vos_ilog.h
 *
 * Author: Jeff Olivier <jeffrey.v.olivier@intel.com>
 */

#ifndef __VOS_ILOG_H__
#define __VOS_ILOG_H__

#include <daos/common.h>
#include "ilog.h"
#include "vos_ts.h"

/** Conditional mask for operation */
enum {
	/** No condition */
	VOS_ILOG_COND_NONE,
	/** Operation is conditional punch */
	VOS_ILOG_COND_PUNCH,
	/** Operation is conditional update */
	VOS_ILOG_COND_UPDATE,
	/** Operation is conditional insert */
	VOS_ILOG_COND_INSERT,
	/** Operation is conditional fetch */
	VOS_ILOG_COND_FETCH,
};

struct vos_container;

#define DF_PUNCH DF_X64".%d"
#define DP_PUNCH(punch) (punch)->pr_epc, (punch)->pr_minor_epc

struct vos_punch_record {
	/** Major epoch of punch */
	daos_epoch_t	pr_epc;
	/** Minor epoch of punch */
	uint16_t	pr_minor_epc;
};

struct vos_ilog_info {
	struct ilog_entries	 ii_entries;
	/** Visible uncommitted epoch */
	daos_epoch_t		 ii_uncommitted;
	/** If non-zero, earliest creation timestamp in current incarnation. */
	daos_epoch_t		 ii_create;
	/** If non-zero, prior committed punch */
	struct vos_punch_record	 ii_prior_punch;
	/** If non-zero, prior committed or uncommitted punch */
	struct vos_punch_record	 ii_prior_any_punch;
	/** If non-zero, subsequent committed punch.  Minor epoch not used for
	 *  subsequent punch as it does not need replay if it's intermediate
	 */
	daos_epoch_t		 ii_next_punch;
	/** True if there is an uncertain update.  If a punch is uncertain,
	 *  it should always cause a failure in vos_ilog_fetch.  But update
	 *  conflict depends on the operation doing the check.
	 */
	daos_epoch_t		 ii_uncertain_create;
	/** The entity has no valid log entries */
	bool			 ii_empty;
	/** All data is contained within specified epoch range */
	bool			 ii_full_scan;
};

/** Initialize the incarnation log globals */
int
vos_ilog_init(void);

/** Initialize incarnation log information */
void
vos_ilog_fetch_init(struct vos_ilog_info *info);

/** Move ilog information from src to dest, clearing src */
void
vos_ilog_fetch_move(struct vos_ilog_info *dest, struct vos_ilog_info *src);

/** Finalize incarnation log information */
void
vos_ilog_fetch_finish(struct vos_ilog_info *info);

/**
 * Read (or refresh) the incarnation log into \p entries.  Internally,
 * this will be a noop if the arguments are the same and nothing has
 * changed since the last invocation.
 *
 * \param	umm[IN]		umem instance
 * \param	coh[IN]		container open handle
 * \param	intent[IN]	Intent of the operation
 * \param	ilog[IN]	The incarnation log root
 * \param	epoch[IN]	Epoch to fetch
 * \param	has_cond[IN]	Whether for conditional operation or not
 * \param	bound[IN]	Epoch uncertainty bound
 * \param	punched[IN]	Punched epoch.  Ignored if parent is passed.
 * \param	parent[IN]	parent incarnation log info (NULL if no parent
 *				log exists).  Fetch should have already been
 *				called at same epoch or parent.
 * \param	info[IN,OUT]	incarnation log info
 *
 * \return	-DER_NONEXIST	Nothing in the log
 *		-DER_INPROGRESS	Local target doesn't know latest state
 *		0		Successful fetch
 *		other		Appropriate error code
 */
#define vos_ilog_fetch vos_ilog_fetch_
int
vos_ilog_fetch_(struct umem_instance *umm, daos_handle_t coh, uint32_t intent,
		struct ilog_df *ilog, daos_epoch_t epoch, daos_epoch_t bound,
		bool has_cond, const struct vos_punch_record *punched,
		const struct vos_ilog_info *parent, struct vos_ilog_info *info);

/**
 * Check the incarnation log if an update is needed and update it.  Refreshes
 * the log into \p entries.
 *
 * \param	cont[IN]	Pointer to vos container
 * \param	ilog[IN]	The incarnation log root
 * \param	epr[IN]		Range of update
 * \param	bound[IN]	Epoch uncertainty bound
 * \param	parent[IN]	parent incarnation log info (NULL if no parent
 *				log exists).  Fetch should have already been
 *				called at same epoch or parent.
 * \param	info[IN,OUT]	incarnation log info
 * \param	cond[IN]	Conditional flags.
 * \param	ts_set[IN]	timestamp set.
 *
 * \return	0		Successful update
 *		other		Appropriate error code
 */
#define vos_ilog_update vos_ilog_update_
int
vos_ilog_update_(struct vos_container *cont, struct ilog_df *ilog,
		 const daos_epoch_range_t *epr, daos_epoch_t bound,
		 struct vos_ilog_info *parent, struct vos_ilog_info *info,
		 uint32_t cond_flag, struct vos_ts_set *ts_set);

/**
 * Punch the incarnation log entry if it's the leaf.  Do conditional check if
 * necessary.   If it isn't a leaf and there is no condition, this is a noop.
 * If there is no condition, \p parent and \p info are ignored.
 *
 * \param	cont[IN]	Pointer to vos container
 * \param	ilog[IN]	The incarnation log root
 * \param	epr[IN]		Range of update
 * \param	bound[IN]	Epoch uncertainty bound
 * \param	parent[IN]	parent incarnation log info (NULL if no parent
 *				log exists).  Fetch should have already been
 *				called at same epoch or parent.
 * \param	info[IN,OUT]	incarnation log info
 * \param	ts_set[IN]	timestamp set.
 * \param	leaf[IN]	The actual entry to punch
 * \param	replay[IN]	True if replay punch
 *
 * \return	0		Successful update
 *		other		Appropriate error code
 */
#define vos_ilog_punch vos_ilog_punch_
int
vos_ilog_punch_(struct vos_container *cont, struct ilog_df *ilog,
		const daos_epoch_range_t *epr, daos_epoch_t bound,
		struct vos_ilog_info *parent, struct vos_ilog_info *info,
		struct vos_ts_set *ts_set, bool leaf, bool replay);

/**
 * Check the incarnation log for existence and return important information
 *
 * \param	info[IN]		Parsed incarnation log information
 * \param	epr_in[IN]		Input epoch range
 * \param	epr_out[IN, OUT]	Updated epoch range
 * \param	visible_only[IN]	Caller only wants to see entity if it's
 *					visible in the epoch range.
 *
 * \return	-DER_NONEXIST		The key/object either doesn't exist
 *					or if \p visible_only is false,
 *					there is no covered entries either.
 *		0			success
 */
#define vos_ilog_check vos_ilog_check_
int
vos_ilog_check_(struct vos_ilog_info *info, const daos_epoch_range_t *epr_in,
		daos_epoch_range_t *epr_out, bool visible_only);

/** Initialize callbacks for vos incarnation log */
void
vos_ilog_desc_cbs_init(struct ilog_desc_cbs *cbs, daos_handle_t coh);

/** Aggregate (or discard) the incarnation log in the specified range
 *
 * \param	coh[IN]		container handle
 * \param	ilog[IN]	Incarnation log
 * \param	epr[IN]		Aggregation range
 * \param	discard[IN]	Discard all entries in range
 * \param	inprogress[IN]	Discard only uncommitted entries
 * \param	punched[IN]	Highest epoch where parent is punched
 * \param	info[IN]	Incarnation log info
 *
 * \return	0		Success
 *		1		Indicates log is empty
 *		-DER_NONEXIST	Indicates log no longer visible
 *		< 0		Failure
 */
int
vos_ilog_aggregate(daos_handle_t coh, struct ilog_df *ilog, const daos_epoch_range_t *epr,
		   bool discard, bool inprogress, const struct vos_punch_record *parent_punch,
		   struct vos_ilog_info *info);

/** Check if the ilog can be discarded.  This will only return true if the ilog is punched at the
 *  specified epoch and there are no creation stamps outside of the range
 *
 * \param	coh[IN]		container handle
 * \param	ilog[IN]	Incarnation log
 * \param	epr[IN]		Aggregation range
 * \param	punched[IN]	Highest epoch where parent is punched
 * \param	info[IN]	Incarnation log info
 *
 * \return	true if fully punched, false otherwise
 */
bool
vos_ilog_is_punched(daos_handle_t coh, struct ilog_df *ilog, const daos_epoch_range_t *epr,
		    const struct vos_punch_record *parent_punch, struct vos_ilog_info *info);

/* #define ILOG_TRACE */
#ifdef ILOG_TRACE
#undef vos_ilog_fetch
#undef vos_ilog_update
#undef vos_ilog_punch
#undef vos_ilog_check
/* Useful for debugging the incarnation log but too much information for
 * normal debugging.
 */
#define vos_ilog_fetch(umm, coh, intent, ilog, epoch, bound, has_cond,	\
		       punched, parent, info)				\
({									\
	int __rc;							\
									\
	D_DEBUG(DB_TRACE, "vos_ilog_fetch: log="DF_X64" intent=%d"	\
		" epoch="DF_X64" bound="DF_X64" punched="DF_X64"(%s)\n",\
		umem_ptr2off(umm, ilog), intent, epoch, bound,		\
		(uint64_t)punched, has_cond ? "cond" : "non-cond");	\
	__rc = vos_ilog_fetch_(umm, coh, intent, ilog, epoch, bound,	\
			       has_cond, punched, parent, info);	\
	D_DEBUG(DB_TRACE, "vos_ilog_fetch: returned "DF_RC" create="	\
		DF_X64" pp="DF_PUNCH" pap="DF_PUNCH" np="DF_X64	\
		" %s\n", DP_RC(__rc), (info)->ii_create,		\
		DP_PUNCH(&(info)->ii_prior_punch),			\
		DP_PUNCH(&(info)->ii_prior_any_punch),			\
		(info)->ii_next_punch,					\
		(info)->ii_empty ? "is empty" : "");			\
	__rc;								\
})

#define vos_ilog_update(cont, ilog, epr, bound, parent, info, cond,	\
			ts_set)						\
({									\
	struct umem_instance	*__umm = vos_cont2umm(cont);		\
	int			 __rc;					\
									\
	D_DEBUG(DB_TRACE, "vos_ilog_update: log="DF_X64" epr="		\
		DF_X64"-"DF_X64" bound="DF_X64" cond=%d\n",		\
		umem_ptr2off(__umm, ilog), (epr)->epr_lo, (epr)->epr_hi,\
		(bound), (cond));					\
	__rc = vos_ilog_update_(cont, ilog, epr, bound, parent, info,	\
				cond, ts_set);				\
	D_DEBUG(DB_TRACE, "vos_ilog_update: returned "DF_RC" create="	\
		DF_X64" pap="DF_X64".%d\n", DP_RC(__rc),		\
		(info)->ii_create,					\
		DP_PUNCH(&(info)->ii_prior_any_punch));			\
	__rc;								\
})

#define vos_ilog_punch(cont, ilog, epr, bound, parent, info, ts_set,	\
		       leaf, replay)					\
({									\
	struct umem_instance	*__umm = vos_cont2umm(cont);		\
	int			 __rc;					\
									\
	D_DEBUG(DB_TRACE, "vos_ilog_punch: log="DF_X64" epr="		\
		DF_X64"-"DF_X64" bound="DF_X64" leaf=%d\n",		\
		umem_ptr2off(__umm, ilog), (epr)->epr_lo, (epr)->epr_hi,\
		(bound), (leaf));					\
	__rc = vos_ilog_punch_(cont, ilog, epr, bound, parent, info,	\
			       ts_set, leaf, replay);			\
	D_DEBUG(DB_TRACE, "vos_ilog_punch: returned "DF_RC"\n",		\
		DP_RC(__rc));						\
	__rc;								\
})

#define vos_ilog_check(info, epr_in, epr_out, visible_only)		\
({									\
	int __rc;							\
									\
	_Pragma("GCC diagnostic push")					\
	_Pragma("GCC diagnostic ignored \"-Waddress\"")			\
	D_DEBUG(DB_TRACE, "vos_ilog_check: epr_in="DF_X64"-"DF_X64	\
		" %s\n", (epr_in)->epr_lo, (epr_in)->epr_hi,		\
		(visible_only) ? "visible" : "all");			\
	__rc = vos_ilog_check_(info, epr_in, epr_out, visible_only);	\
	D_DEBUG(DB_TRACE, "vos_ilog_check: returned "DF_RC" %s"		\
		DF_X64"-"DF_X64"\n", DP_RC(__rc),			\
		((epr_out) != NULL) ? " epr_out=" : " #",		\
		((epr_out) != NULL) ? (epr_out)->epr_lo : 0,		\
		((epr_out) != NULL) ? (epr_out)->epr_hi : 0);		\
	_Pragma("GCC diagnostic pop")					\
	__rc;								\
})

#endif

static inline void
vos_ilog_ts_ignore(struct umem_instance *umm, struct ilog_df *ilog)
{
	if (!DAOS_ON_VALGRIND)
		return;

	umem_tx_xadd_ptr(umm, ilog_ts_idx_get(ilog), sizeof(int),
			 UMEM_XADD_NO_SNAPSHOT);
}


/** Check if the timestamps associated with the ilog are in cache.  If so,
 *  add them to the set.
 *
 *  \param	ts_set[in]	The timestamp set
 *  \param	ilog[in]	The incarnation log
 *  \param	record[in]	The record to hash
 *  \param	rec_size[in]	The size of the record
 *
 *  \return true if found or ts_set is NULL
 */
int
vos_ilog_ts_add(struct vos_ts_set *ts_set, struct ilog_df *ilog,
		const void *record, daos_size_t rec_size);

/** Mark the last timestamp entry corresponding to the ilog as newly created
 *  \param	ts_set[in]	The timestamp set
 *  \param	ilog[in]	The incarnation log
 */
void
vos_ilog_ts_mark(struct vos_ts_set *ts_set, struct ilog_df *ilog);

/** Evict the cached timestamp entry, if present
 *
 *  \param	ilog[in]	The incarnation log
 *  \param	type[in]	The timestamp type
 *  \param	standalone[in]	standloane TLS or not
 */
void
vos_ilog_ts_evict(struct ilog_df *ilog, uint32_t type, bool standalone);

void
vos_ilog_last_update(struct ilog_df *ilog, uint32_t type, daos_epoch_t *epc,
		     bool standalone);

#endif /* __VOS_ILOG_H__ */
