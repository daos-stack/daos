/**
 * (C) Copyright 2016 Intel Corporation.
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

#include <uuid/uuid.h>

#include <daos_event.h>
#include <daos_types.h>

/**
 * Create a pool with \a uuid and \a mode.
 *
 * \a grp and \a tgts pass in the address of each target and the total
 * number of targets. The fault domains among the targets are retrieved
 * automatically from external sources.
 * Targets are assumed to share the same \a dev and \a size.
 *
 * \param mode	[IN]	Capabilities permitted for the pool. May contain these
 * 			bits:
 * 			  0400	 user DAOS_PC_EX
 * 			  0200	 user DAOS_PC_RW
 * 			  0100	 user DAOS_PC_RO
 * 			  0040	group DAOS_PC_EX
 * 			  0020	group DAOS_PC_RW
 * 			  0010	group DAOS_PC_RO
 * 			  0004	other DAOS_PC_EX
 * 			  0002	other DAOS_PC_RW
 * 			  0001	other DAOS_PC_RO
 * \param uid	[IN]	user owning the pool
 * \param gid	[IN]	group owning the pool
 * \param grp	[IN]	process set name of the DAOS servers managing the pool
 * \param tgts	[IN]	Optional, allocate targets on this list of ranks
 *			If set to NULL, create the pool over all the ranks
 *			available in the service group.
 * \param dev	[IN]	string identifying the target devices to use
 * \param size	[IN]	target sizes (i.e., maximum amounts of storage space
 *			targets can consume) in bytes. Passing 0 will use the
 *			minimal supported target size.
 * \param svc	[IN]	Must be pre-allocated by the caller
 *		[OUT]	Return a list of ranks where the pool service was
 *			initialized
 * \param uuid	[OUT]	UUID of the pool created
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
daos_pool_create(unsigned int mode, unsigned int uid, unsigned int gid,
		 const char *grp, const daos_rank_list_t *tgts, const char *dev,
		 daos_size_t size, daos_rank_list_t *svc, uuid_t uuid,
		 daos_event_t *ev);

/**
 * Destroy a pool with \a uuid. If there is at least one connection to this
 * pool, and \a force is zero, then this operation completes with DER_BUSY.
 * Otherwise, the pool is destroyed when the operation completes.
 *
 * \param uuid	[IN]	UUID of the pool to destroy
 * \param grp	[IN]	process set name of the DAOS servers managing the pool
 * \param force	[IN]	force destruction even if there are active connections
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
daos_pool_destroy(const uuid_t uuid, const char *grp, int force,
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
 * \param grp	[IN]	process set name of the DAOS servers managing the pool
 * \param tgts	[IN]	Optional, only extend the pool to included targets.
 * \param failed
 *		[OUT]	Optional, buffer to store faulty targets on failure.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
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
daos_pool_extend(const uuid_t uuid, const char *grp, daos_rank_list_t *tgts,
		 daos_rank_list_t *failed, daos_event_t *ev);
#endif /* __DAOS_MGMT_H__ */
