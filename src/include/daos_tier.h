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
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * @file
 * @brief DAOS Caching and Tiering APIs
 *
 * Author: John Keys <john.keys@intel.com>
 * Author: Ian F. Adams <ian.f.adams@intel.com>
 * Version 0.1
 */

#ifndef __DAOS_TIER_H__
#define __DAOS_TIER_H__

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * CT Specific Structs
 */

/**
 * Place Holder for caching policy
 */
typedef struct {
	/* Choice of eviction alg */
	uint32_t	cp_evict;
	/* Choice of persistence policy */
	uint32_t	cp_persist;
	/* Choice of read ahead policy */
	uint32_t	cp_read_ahead;
	/* hi-water mark for eviction */
	uint64_t	cp_hi_water;
	/* lo-water for eviction */
	uint64_t	cp_lo_water;
} daos_cache_pol_t;

/**
 * Type of pool/tier
 */
typedef enum {
	/* A regular caching tier */
	DAOS_TR_CACHE,
	/* A parking tier */
	DAOS_TR_PARKING,
} daos_tier_type_t;

/**
 * Summarize a pool and its policies for caching
 */
typedef struct {
	/* What is the primary media of the pool */
	daos_target_type_t	ti_media;
	/* Describe the caching policy */
	daos_cache_pol_t	ti_policy;
	/* What type of tier (currently only cache or parking) */
	daos_tier_type_t	ti_type;
	/* Temperature of the tier-pool, used to set up a hierarchy */
	uint32_t		ti_tmpr;
	/* Open handle affiliated with this pool tier */
	daos_handle_t		ti_poh;
	/* UUID of the pool */
	uuid_t			ti_pool_id;
	/* Group leader for pool */
	daos_rank_t		ti_leader;
	/* Group name for pool */
	crt_group_t		ti_group;
} daos_tier_info_t;

/* Convenient struct for moving all tier info together */
typedef struct {
	/* Number of tiers */
	daos_nr_t		 tl_nr;
	/* Reference to tier list */
	daos_tier_info_t	*tl_tiers;
} daos_tier_list_t;

/**
 * CT (Pre)Fetch API
 */

/**
 * Move an entire containers content at a specified highest committed epoch
 * HCE to the target pool. This is sourced from the coldest tier of the
 * tier hierarchy
 *
 * \param poh	[IN]	Pool connection handle of the target pool
 * \param cont_id
 *		[IN]	UUID of the container to fetch
 * \param fetch_ep
 *		[IN]	Epoch to fetch. To retrieve HCE pass in 0.
 * \param obj_list	List of objects to fetch, if NULL, all objects in the
 *			container will be retrieved
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::error in
 *			non-blocking mode:
 *			-0		Success
 *			-DER_NO_HDL	Invalid pool handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_NONEXIST	Container is nonexistent on lower tier
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_PERM	Permission denied
 */
int
daos_tier_fetch_cont(daos_handle_t poh, const uuid_t cont_id,
		     daos_epoch_t fetch_ep, daos_oid_list_t *obj_list,
		     daos_event_t *ev);

/**
 * CT Tier Mapping API
 */

 /**
 * Registers one (or more) pools as tiers
 * \param local_pl_id
 *		[IN]	The ID of the pool that is local. This is used
 *			in figuring out which tiers are warmer
 *			and colder than self.
 * \param local_temp
 *		[IN]	temperature of the local tier, used to figure
 *			out who is warmer and colder than the local tier
 * \param tier_list
 *		[IN]	list of all tiers for a particular workflow
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::error in
 *			non-blocking mode:
 *			-0		Success
 *			-DER_NO_HDL	Invalid pool handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 */
int
daos_tier_register(uuid_t local_pl_id, uint32_t local_temp,
		   daos_tier_list_t tier_list, daos_event_t *ev);

/**
 * PING client call, mostly for testing and playing around
 * TODO add actual docstring if we decide to keep it
 */
int
daos_tier_ping(uint32_t ping_val, daos_event_t *ev);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_TIER_H__ */
