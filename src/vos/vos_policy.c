/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Object placement policy
 *  Policy definitions
 *
 * Author: Krzysztof Majzerowicz-Jaszcz (krzysztof.majzerowicz-jaszcz@intel.com)
 */

#include <daos/policy.h>
#include "vos_policy.h"


/* policy functions definitions */

/* default policy - former "vos_media_select" function */
static daos_media_type_t
policy_default(struct vos_pool *pool, daos_iod_type_t type, daos_size_t size)
{
	if (pool->vp_vea_info == NULL)
		return DAOS_MEDIA_SCM;

	return (size >= VOS_BLK_SZ) ? DAOS_MEDIA_NVME : DAOS_MEDIA_SCM;
}

/* policy based on io size */
static daos_media_type_t
policy_io_size(struct vos_pool *pool, daos_iod_type_t type, daos_size_t size)
{
        daos_media_type_t medium;

        if (pool->vp_vea_info == NULL)
                return DAOS_MEDIA_SCM;

        if (size >= VOS_POLICY_OPTANE_THRESHOLD)
                medium = DAOS_MEDIA_NVME;
        else if (size >= VOS_POLICY_SCM_THRESHOLD)
                medium = DAOS_MEDIA_OPTANE;
        else
                medium = DAOS_MEDIA_SCM;

        return medium;
}

/* policy based on how write-intesive is data to store */
static daos_media_type_t
policy_write_intensivity(struct vos_pool *pool, daos_iod_type_t type,
                         daos_size_t size)
{
        return DAOS_MEDIA_NVME;
}


/* policy functions table */
static daos_media_type_t (*vos_policies[TIER_POLICY_MAX])(struct vos_pool*,
                                                        daos_iod_type_t,
                                                        daos_size_t) =
                   {policy_default, policy_io_size, policy_write_intensivity};




daos_media_type_t
vos_policy_media_select(struct vos_pool *pool, daos_iod_type_t type,
                        daos_size_t size)
{
        return vos_policies[pool->vp_policy](pool, type, size);
}
