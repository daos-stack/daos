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
 */
/**
 * DAOS Management API.
 */

#ifndef __DAOS_MGMT_H__
#define __DAOS_MGMT_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <uuid/uuid.h>

#include <daos_event.h>
#include <daos_types.h>
#include <daos_pool.h>

/*
 * DAOS management pool information
 */
typedef struct {
	/* TODO? same pool info structure as a pool query?
	 * requires back-end RPC to each pool service.
	 * daos_pool_info_t		 mgpi_info;
	 */
	uuid_t				 mgpi_uuid;
	/** List of current pool service replica ranks */
	d_rank_list_t			*mgpi_svc;
} daos_mgmt_pool_info_t;

/**
 * Currently, the following methods are mostly for dmg and tests.
 */

/**
 * Kill a remote server.
 *
 * \param grp	[IN]	Process set name of the DAOS servers managing the pool
 * \param rank	[IN]	Rank to kill
 * \param force	[IN]	Abrupt shutdown, no cleanup
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_mgmt_svc_rip(const char *grp, d_rank_t rank, bool force,
		  daos_event_t *ev);

/**
 * Exclude a set of storage targets from a pool.
 *
 * \param uuid	[IN]	UUID of the pool
 * \param grp	[IN]	process set name of the DAOS servers managing the pool
 * \param svc	[IN]	list of pool service ranks
 * \param tgts	[IN]	Target to be excluded from the pool.
 *			Now can-only exclude one target per API calling. If
 *			tl_tgts = -1, it means it will exclude all targets
 *			on the rank.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NONEXIST	Pool is nonexistent
 */
int
daos_pool_tgt_exclude(const uuid_t uuid, const char *grp,
		      const d_rank_list_t *svc, struct d_tgt_list *tgts,
		      daos_event_t *ev);

/**
 * Extend the pool to more targets. If \a tgts is NULL, this function
 * will extend the pool to all the targets in the group, otherwise it will
 * only extend the pool to the included targets.
 *
 * NB: Doubling storage targets in the pool can have better performance than
 * arbitrary targets adding.
 *
 * \param uuid	[IN]	UUID of the pool to extend
 * \param grp	[IN]	Process set name of the DAOS servers managing the pool
 * \param tgts	[IN]	Optional, only extend the pool to included targets.
 * \param failed
 *		[OUT]	Optional, buffer to store faulty targets on failure.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_PERM	Permission denied
 *			-DER_NONEXIST	Storage target is nonexistent
 */
int
daos_pool_extend(const uuid_t uuid, const char *grp, d_rank_list_t *tgts,
		 d_rank_list_t *failed, daos_event_t *ev);

/**
 * reintegrate a set of storage targets from a pool.
 *
 * \param uuid	[IN]	UUID of the pool
 * \param grp	[IN]	process set name of the DAOS servers managing the pool
 * \param svc	[IN]	list of pool service ranks
 * \param tgts	[IN]	Target array to be reintegrated from the pool.  If
 *			tl_tgts = -1, it means it will reintegrate all targets
 *			on the rank.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_PERM	Permission denied
 */
int
daos_pool_reint_tgt(const uuid_t uuid, const char *grp,
		    const d_rank_list_t *svc, struct d_tgt_list *tgts,
		    daos_event_t *ev);

/**
 * drain a set of storage targets from a pool.
 *
 * \param uuid	[IN]	UUID of the pool
 * \param grp	[IN]	process set name of the DAOS servers managing the pool
 * \param svc	[IN]	list of pool service ranks
 * \param tgts	[IN]	Target array to be added from the pool.  If
 *			tl_tgts = -1, it means it will add all targets
 *			on the rank.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_PERM	Permission denied
 */
int
daos_pool_drain_tgt(const uuid_t uuid, const char *grp,
		    const d_rank_list_t *svc, struct d_tgt_list *tgts,
		    daos_event_t *ev);


/**
 * Exclude completely a set of storage targets from a pool. Compared with
 * daos_pool_tgt_exclude(), this API will mark the targets to be DOWNOUT, i.e.
 * the rebuilding for this target is done, while daos_pool_tgt_exclude() only
 * mark the target to be DOWN, i.e. the rebuilding might not finished yet.
 *
 * \param uuid	[IN]	UUID of the pool
 * \param grp	[IN]	process set name of the DAOS servers managing the pool
 * \param svc	[IN]	list of pool service ranks
 * \param tgts	[IN]	Target array to be excluded from the pool.
 *			Now can-only exclude out one target per API calling. If
 *			tl_tgts = -1, it means it will exclude out all targets
 *			on the rank.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_PERM	Permission denied
 */
int
daos_pool_tgt_exclude_out(const uuid_t uuid, const char *grp,
			  const d_rank_list_t *svc, struct d_tgt_list *tgts,
			  daos_event_t *ev);

/**
 * Stop the current pool service leader.
 *
 * \param poh	[IN]	Pool connection handle
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid pool handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_PERM	Permission denied
 */
int
daos_pool_stop_svc(daos_handle_t poh, daos_event_t *ev);

/**
 * The operation code for DAOS client to set different parameters globally
 * on all servers.
 */
enum {
	DMG_KEY_FAIL_LOC	 = 0,
	DMG_KEY_FAIL_VALUE,
	DMG_KEY_FAIL_NUM,
	DMG_KEY_REBUILD_THROTTLING,
	DMG_KEY_NUM,
};

/**
 * Set parameter on servers.
 *
 * \param grp	[IN]	Process set name of the DAOS servers managing the pool
 * \param rank	[IN]	Ranks to set parameter. -1 means setting on all servers.
 * \param key_id [IN]	key ID of the parameter.
 * \param value [IN]	value of the parameter.
 * \param value_extra [IN]
 *			optional extra value to set the fail value when
 *			\a key_id is DMG_CMD_FAIL_LOC and \a value is in
 *			DAOS_FAIL_VALUE mode.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_mgmt_set_params(const char *grp, d_rank_t rank, unsigned int key_id,
		     uint64_t value, uint64_t value_extra, daos_event_t *ev);

/**
 * Add mark to servers.
 *
 * \param mark	[IN]	mark to add to the debug log.
 */
int
daos_mgmt_add_mark(const char *mark);

/**
 * Query internal blobstore state for given blobstore uuid in the specified
 * DAOS system.
 *
 * \param group		  [IN]	Name of DAOS system managing the service.
 * \param blobstore_uuid  [IN]	UUID of the blobstore to query.
 * \param blobstore_state [OUT] Will return an enum integer that will
 *				later be converted to a blobstore state:
 *				SETUP, NORMAL, FAULTY, TEARDOWN, or OUT
 * \param ev		  [IN]  Completion event. Optional and can be NULL.
 *				The function will run in blocking mode
 *				if \a ev is NULL.
 *
 * \return			0		Success
 *
 */
int
daos_mgmt_get_bs_state(const char *group, uuid_t blobstore_uuid,
		       int *blobstore_state, daos_event_t *ev);


#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_MGMT_H__ */
