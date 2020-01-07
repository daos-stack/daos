/**
 * (C) Copyright 2019 Intel Corporation.
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
/**
 * Incarnation log wrappers for fetching the log and checking existence
 * vos/vos_ilog.h
 *
 * Author: Jeff Olivier <jeffrey.v.olivier@intel.com>
 */

#ifndef __VOS_ILOG_H__
#define __VOS_ILOG_H__

#include <daos/common.h>
#include <ilog.h>

struct vos_container;

struct vos_ilog_info {
	struct ilog_entries	ii_entries;
	/** If non-zero, earliest creation timestamp in current incarnation. */
	daos_epoch_t		ii_create;
	/** If non-zero, prior committed punch */
	daos_epoch_t		ii_prior_punch;
	/** If non-zero, prior committed or uncommitted punch */
	daos_epoch_t		ii_prior_any_punch;
	/** If non-zero, subsequent committed punch */
	daos_epoch_t		ii_next_punch;
	/** The entity has no valid log entries */
	bool			ii_empty;
};

/** Initialize the incarnation log globals */
int
vos_ilog_init(void);

/** Initialize incarnation log information */
void
vos_ilog_fetch_init(struct vos_ilog_info *info);

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
		struct ilog_df *ilog, daos_epoch_t epoch, daos_epoch_t punched,
		const struct vos_ilog_info *parent, struct vos_ilog_info *info);

/**
 * Check the incarnation log if an update is needed and update it.  Refreshes
 * the log into \p entries.
 *
 * \param	cont[IN]	Pointer to vos container
 * \param	ilog[IN]	The incarnation log root
 * \param	epr[IN]		Range of update
 * \param	parent[IN]	parent incarnation log info (NULL if no parent
 *				log exists).  Fetch should have already been
 *				called at same epoch or parent.
 * \param	info[IN,OUT]	incarnation log info
 *
 * \return	0		Successful update
 *		other		Appropriate error code
 */
#define vos_ilog_update vos_ilog_update_
int
vos_ilog_update_(struct vos_container *cont, struct ilog_df *ilog,
		 const daos_epoch_range_t *epr, struct vos_ilog_info *parent,
		 struct vos_ilog_info *info);


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
 * \param	punched[IN]	Highest epoch where parent is punched
 * \param	info[IN]	Incarnation log info
 *
 * \return	0		Success
 *		1		Indicates log is empty
 *		-DER_NONEXIST	Indicates log no longer visible
 *		< 0		Failure
 */
int
vos_ilog_aggregate(daos_handle_t coh, struct ilog_df *ilog,
		   const daos_epoch_range_t *epr, bool discard,
		   daos_epoch_t punched, struct vos_ilog_info *info);

#define ILOG_TRACE
#ifdef ILOG_TRACE
#undef vos_ilog_fetch
#undef vos_ilog_update
#undef vos_ilog_check
/* Useful for debugging the incarnation log but too much information for
 * normal debugging.
 */
#define vos_ilog_fetch(umm, coh, intent, ilog, epoch, punched, parent, info)\
({									\
	int __rc;							\
									\
	D_DEBUG(DB_TRACE, "vos_ilog_fetch: log="DF_X64" intent=%d"	\
		" epoch="DF_U64" punched="DF_U64"\n",			\
		umem_ptr2off(umm, ilog), intent, epoch,			\
		(uint64_t)punched);					\
	__rc = vos_ilog_fetch_(umm, coh, intent, ilog, epoch, punched,	\
			       parent, info);				\
	D_DEBUG(DB_TRACE, "vos_ilog_fetch: returned "DF_RC" create="	\
		DF_U64" pp="DF_U64" pap="DF_U64" np="DF_U64" %s\n",	\
		DP_RC(__rc), (info)->ii_create, (info)->ii_prior_punch,	\
		(info)->ii_prior_any_punch, (info)->ii_next_punch,	\
		(info)->ii_empty ? "is empty" : "");			\
	__rc;								\
})

#define vos_ilog_update(cont, ilog, epr, parent, info)			\
({									\
	struct umem_instance	*__umm = vos_cont2umm(cont);		\
	int			 __rc;					\
									\
	D_DEBUG(DB_TRACE, "vos_ilog_update: log="DF_X64" epr="		\
		DF_U64"-"DF_U64"\n", umem_ptr2off(__umm, ilog),		\
		(epr)->epr_lo, (epr)->epr_hi);				\
	__rc = vos_ilog_update_(cont, ilog, epr, parent, info);		\
	D_DEBUG(DB_TRACE, "vos_ilog_update: returned "DF_RC" create="	\
		DF_U64" pap="DF_U64"\n", DP_RC(__rc), (info)->ii_create,\
		(info)->ii_prior_any_punch);				\
	__rc;								\
})

#define vos_ilog_check(info, epr_in, epr_out, visible_only)		\
({									\
	int __rc;							\
									\
	_Pragma("GCC diagnostic push")					\
	_Pragma("GCC diagnostic ignored \"-Waddress\"")			\
	D_DEBUG(DB_TRACE, "vos_ilog_check: epr_in="DF_U64"-"DF_U64	\
		" %s\n", (epr_in)->epr_lo, (epr_in)->epr_hi,		\
		(visible_only) ? "visible" : "all");			\
	__rc = vos_ilog_check_(info, epr_in, epr_out, visible_only);	\
	D_DEBUG(DB_TRACE, "vos_ilog_check: returned "DF_RC" %s"		\
		DF_U64"-"DF_U64"\n", DP_RC(__rc),			\
		((epr_out) != NULL) ? " epr_out=" : " #",		\
		((epr_out) != NULL) ? (epr_out)->epr_lo : 0,		\
		((epr_out) != NULL) ? (epr_out)->epr_hi : 0);		\
	_Pragma("GCC diagnostic pop")					\
	__rc;								\
})

#endif


#endif /* __VOS_ILOG_H__ */
