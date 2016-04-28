/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * DAOS Management API.
 */

#ifndef __DMG_API_H__
#define __DMG_API_H__

/**
 *   Initialize dmgc client library.
 *
 *   This function will register the RPCs.
 */
int
dmg_init();

/**
 * Finalize dmgc client library.
 */
int
dmg_fini();

/**
 * Create a pool with \a uuid and \a mode.
 *
 * \a grp and \a tgts pass in the address of each target and the total
 * number of targets. The fault domains among the targets are retrieved
 * automatically from external sources.
 * Targets are assumed to share the same \a dev and \a size.
 *
 * \param uuid	[IN]	UUID of the pool to create
 * \param mode	[IN]	credentials associated with the pool
 * \param grp	[IN]	service group owning the pool
 * \param tgts	[IN]	Optional, allocate targets on this list of ranks
 *			If set to NULL, create the pool over all the ranks
 *			available in the service group.
 * \param dev	[IN]	string identifying the target devices to use
 * \param size	[IN]	target sizes (i.e., maximum amounts of storage space
 *			targets can consume) in bytes.
 * \param svc	[IN]	Must be pre-allocated by the caller
 *		[OUT]	Return a list of ranks where the pool service was
 *			initialized
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
dmg_pool_create(const uuid_t uuid, unsigned int mode, const daos_group_t *grp,
		const daos_rank_list_t *tgts, const char *dev, daos_size_t size,
		daos_rank_list_t *svc, daos_event_t *ev);

/**
 * Destroy a pool with \a uuid. If there is at least one connection to this
 * pool, and \a force is zero, then this operation completes with DER_BUSY.
 * Otherwise, the pool is destroyed when the operation completes.
 *
 * \param uuid	[IN]	UUID of the pool to destroy
 * \param force	[IN]	force destruction even if there are active connections
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
dmg_pool_destroy(const uuid_t uuid, int force, daos_event_t *ev);

/**
 * Extend the pool to more targets. If \a tgts is NULL, this function
 * will extend the pool to all the targets in the group, otherwise it will
 * only extend the pool to the included targets.
 *
 * NB: Doubling storage targets in the pool can have better performance than
 * arbitrary targets adding.
 *
 * \param uuid	[IN]	UUID of the pool to extend
 * \param grp	[IN]	service group owning the pool
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
 *			-DER_PERM	Permission denied
 *			-DER_NONEXIST	Storage target is nonexistent
 */
int
dmg_pool_extend(const uuid_t uuid, const daos_group_t *grp,
		daos_rank_list_t *tgts, daos_rank_list_t *failed,
		daos_event_t *ev);
#endif /* __DMG_API_H__ */
