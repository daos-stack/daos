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

#include "vos_policy.h"

static daos_media_type_t
vos_media_select(struct vos_pool *pool, daos_iod_type_t type, daos_size_t size)
{
	if (pool->vp_vea_info == NULL)
		return DAOS_MEDIA_SCM;

	return (size >= VOS_BLK_SZ) ? DAOS_MEDIA_NVME : DAOS_MEDIA_SCM;
}


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

static daos_media_type_t
policy_write_intensivity(struct vos_pool *pool, daos_iod_type_t type,
                         daos_size_t size)
{
        return DAOS_MEDIA_NVME;
}

static daos_media_type_t (*vos_policies[TIER_POLICY_MAX])(struct vos_pool*,
                                                        daos_iod_type_t,
                                                        daos_size_t) =
                   {vos_media_select, policy_io_size, policy_write_intensivity};



// void
// vos_policy_init()
// {
//         return;
// }

daos_media_type_t
vos_policy_media_select(struct vos_pool *pool, daos_iod_type_t type,
                        daos_size_t size)
{
        return vos_policies[pool->vp_policy](pool, type, size);
}
