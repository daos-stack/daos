/*
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
 * \file
 *
 * DAOS Unified Namespace API
 *
 * The unified namespace API provides functions and tools to be able to link
 * files and directories in a system namespace to a location in the DAOS tier
 * (pool and container), in addition to other properties such as object class.
 */

#ifndef __DAOS_UNS_H__
#define __DAOS_UNS_H__

#if defined(__cplusplus)
extern "C" {
#endif

struct duns_attr_t {
	/** Pool uuid of the container. */
	uuid_t			da_puuid;
	/** Container uuid that is created for the path. */
	uuid_t			da_cuuid;
	/** Container layout (POSIX, HDF5) */
	daos_cont_layout_t	da_type;
	/** Default Object Class for all objects in the container */
	daos_oclass_id_t	da_oclass;
};

/**
 * Create a special directory (POSIX) or file (HDF5) depending on the container
 * type, and create a new DAOS container in the pool that is passed in \a
 * attr.da_puuid. The uuid of the container is returned in \a attr.da_cuuid. Set
 * extended attributes on the dir/file created that points to pool uuid,
 * container uuid, and default object class to be used for object in that
 * container. This is to be used in a unified namespace solution to be able to
 * map a path in the unified namespace to a location in the DAOS tier.
 *
 * \param[in]	path	Valid path in an existing namespace.
 * \param[in/out]
 *		attr	Struct containing the attributes. The uuid of the
 *			container created is returned in da_cuuid.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
duns_link_path(const char *path, struct duns_attr_t attr);

/**
 * Retrieve the extended attributes on a path corresponding to DAOS location and
 * properties of that path.
 *
 * \param[in]	path	Valid path in an existing namespace.
 * \param[out]	attr	Struct containing the xattrs on the path.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
duns_resolve_path(const char *path, struct duns_attr_t *attr);


#if defined(__cplusplus)
}
#endif
#endif /* __DAOS_UNS_H__ */
