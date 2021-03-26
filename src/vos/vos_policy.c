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

/* policy based on io size */
static enum daos_media_type_t
policy_io_size(struct vos_pool *pool, daos_iod_type_t type, daos_size_t size)
{
        enum daos_media_type_t medium;
        uint32_t scm_threshold;

        if (pool->vp_vea_info == NULL)
                return DAOS_MEDIA_SCM;

        scm_threshold = pool->vp_policy_desc.params[0] > 0 ?
                        pool->vp_policy_desc.params[0] : VOS_POLICY_SCM_THRESHOLD;

        if (size >= scm_threshold)
                medium = DAOS_MEDIA_NVME;
        else
                medium = DAOS_MEDIA_SCM;

        return medium;
}

/* policy based on how write-intesive is data to store */
static enum daos_media_type_t
policy_write_intensivity(struct vos_pool *pool, daos_iod_type_t type,
                         daos_size_t size)
{
        return DAOS_MEDIA_NVME;
}


/* policy functions table */
static enum daos_media_type_t (*vos_policies[DAOS_MEDIA_POLICY_MAX])(struct vos_pool*,
                                                        daos_iod_type_t,
                                                        daos_size_t) =
                   {policy_io_size, policy_write_intensivity};




enum daos_media_type_t
vos_policy_media_select(struct vos_pool *pool, daos_iod_type_t type,
                        daos_size_t size, enum vos_io_stream ios)
{

        return vos_policies[pool->vp_policy_desc.policy](pool, type, size);
}
