/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "na_loc.h"
#include "na_error.h"

#ifdef NA_HAS_HWLOC
#    include <hwloc.h>
#endif
#include <stdlib.h>
#include <string.h>

/****************/
/* Local Macros */
/****************/

/************************************/
/* Local Type and Struct Definition */
/************************************/

struct na_loc_info {
#ifdef NA_HAS_HWLOC
    hwloc_topology_t topology;
    hwloc_bitmap_t proc_cpuset;
#else
    void *topology;
#endif
};

/********************/
/* Local Prototypes */
/********************/

/*******************/
/* Local Variables */
/*******************/

/*---------------------------------------------------------------------------*/
na_return_t
na_loc_info_init(struct na_loc_info **na_loc_info_p)
{
    struct na_loc_info *na_loc_info = NULL;
    na_return_t ret;
#ifdef NA_HAS_HWLOC
    int rc;
#endif

    na_loc_info = (struct na_loc_info *) malloc(sizeof(*na_loc_info));
    NA_CHECK_SUBSYS_ERROR(cls, na_loc_info == NULL, error, ret, NA_NOMEM,
        "Could not allocate NA loc info");
    memset(na_loc_info, 0, sizeof(*na_loc_info));

#ifdef NA_HAS_HWLOC
    rc = hwloc_topology_init(&na_loc_info->topology);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, NA_PROTOCOL_ERROR,
        "hwloc_topology_init() failed");

#    if HWLOC_API_VERSION < 0x20000
    rc = hwloc_topology_set_flags(
        na_loc_info->topology, HWLOC_TOPOLOGY_FLAG_IO_DEVICES);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, NA_PROTOCOL_ERROR,
        "hwloc_topology_set_flags() failed");
#    else
    rc = hwloc_topology_set_io_types_filter(
        na_loc_info->topology, HWLOC_TYPE_FILTER_KEEP_IMPORTANT);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, NA_PROTOCOL_ERROR,
        "hwloc_topology_set_io_types_filter() failed");
#    endif

    rc = hwloc_topology_load(na_loc_info->topology);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, NA_PROTOCOL_ERROR,
        "hwloc_topology_load() failed");

    /* Allocate memory for proc_cpuset */
    na_loc_info->proc_cpuset = hwloc_bitmap_alloc();
    NA_CHECK_SUBSYS_ERROR(cls, na_loc_info->proc_cpuset == NULL, error, ret,
        NA_NOMEM, "hwloc_bitmap_alloc() failed");

    /* Fill cpuset with the collection of cpu cores that the process runs on */
    rc = hwloc_get_cpubind(
        na_loc_info->topology, na_loc_info->proc_cpuset, HWLOC_CPUBIND_PROCESS);
    NA_CHECK_SUBSYS_ERROR(cls, rc < 0, error, ret, NA_PROTOCOL_ERROR,
        "hwloc_get_cpubind() failed");
#endif

    *na_loc_info_p = na_loc_info;

    return NA_SUCCESS;

error:
    if (na_loc_info)
        na_loc_info_destroy(na_loc_info);

    return ret;
}

/*---------------------------------------------------------------------------*/
void
na_loc_info_destroy(struct na_loc_info *na_loc_info)
{
#ifdef NA_HAS_HWLOC
    if (na_loc_info->proc_cpuset)
        hwloc_bitmap_free(na_loc_info->proc_cpuset);
    if (na_loc_info->topology)
        hwloc_topology_destroy(na_loc_info->topology);
#endif
    free(na_loc_info);
}

/*---------------------------------------------------------------------------*/
bool
na_loc_check_pcidev(const struct na_loc_info *na_loc_info,
    unsigned int domain_id, unsigned int bus_id, unsigned int device_id,
    unsigned int function_id)
{
#ifdef NA_HAS_HWLOC
    hwloc_obj_t obj = NULL;
#endif

    /* Cannot find topology info if no topology is found */
    NA_CHECK_SUBSYS_ERROR_NORET(
        cls, na_loc_info == NULL, error, "na_loc_info not initialized");
    NA_CHECK_SUBSYS_ERROR_NORET(
        cls, na_loc_info->topology == NULL, error, "topology not initialized");

#ifdef NA_HAS_HWLOC
    /* Get the pci device from bdf */
    obj = hwloc_get_pcidev_by_busid(
        na_loc_info->topology, domain_id, bus_id, device_id, function_id);
    NA_CHECK_SUBSYS_ERROR_NORET(
        cls, obj == NULL, error, "hwloc_get_pcidev_by_busid() failed");

    /* pcidev objects don't have cpusets, find the first non-io object above */
    obj = hwloc_get_non_io_ancestor_obj(na_loc_info->topology, obj);
    NA_CHECK_SUBSYS_ERROR_NORET(
        cls, obj == NULL, error, "hwloc_get_non_io_ancestor_obj() failed");

    return (bool) hwloc_bitmap_intersects(
        na_loc_info->proc_cpuset, obj->cpuset);
#else
    (void) domain_id;
    (void) bus_id;
    (void) device_id;
    (void) function_id;
#endif

error:
    return false;
}
