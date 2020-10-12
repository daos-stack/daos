/*
 * (C) Copyright 2019 - 2020 Intel Corporation.
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

/** struct that has the values to make the connection from the UNS to DAOS */
struct duns_attr_t {
	/** Pool uuid of the container. */
	uuid_t			da_puuid;
	/** Container uuid that is created for the path. */
	uuid_t			da_cuuid;
	/** Container layout (POSIX, HDF5) */
	daos_cont_layout_t	da_type;
	/** Default Object Class for all objects in the container */
	daos_oclass_id_t	da_oclass_id;
	/** Default Chunks size for all files in container */
	daos_size_t		da_chunk_size;
	/*
	 * If using direct access to a POSIX container with the prefix, return
	 * path without the prefix.
	 */
	char			*da_rel_path;
	/** container properties **/
	daos_prop_t		*da_props;
	/** Path is on Lustre */
	bool			da_on_lustre;
	/*
	 * Path that is passed does not have daos: prefix but is direct:
	 * (/puuid/cuuid/xyz) and does not need to parse a path UNS attrs.
	 */
	bool			da_no_prefix;
};

/** extended attribute name that will container the UNS info */
#define DUNS_XATTR_NAME		"user.daos"
/** Length of the extended attribute */
#define DUNS_MAX_XATTR_LEN	170

/**
 * Create a special directory (POSIX) or file (HDF5) depending on the container
 * type, and create a new DAOS container in the pool that is passed in \a
 * attr.da_puuid. The uuid of the container is returned in \a attr.da_cuuid. Set
 * extended attributes on the dir/file created that points to pool uuid,
 * container uuid. This is to be used in a unified namespace solution to be able
 * to map a path in the unified namespace to a location in the DAOS tier.
 *
 * \param[in]	poh	Pool handle
 * \param[in]	path	Valid path in an existing namespace.
 * \param[in,out]
 *		attrp	Struct containing the attributes. The uuid of the
 *			container created is returned in da_cuuid.
 *
 * \return		0 on Success. errno code on failure.
 */
int
duns_create_path(daos_handle_t poh, const char *path,
		 struct duns_attr_t *attrp);

/**
 * Retrieve the pool and container uuids from a path corresponding to a DAOS
 * location. If this was a path created with duns_create_path(), then this call
 * would return the pool, container, and type values in the \a attr struct (the
 * rest of the values are not populated.
 *
 * To avoid going through the UNS if the user knows the pool and container
 * uuids, a special format can be passed as a prefix for a "fast path", and this
 * call would parse those out in the \a attr struct and return whatever is left
 * from the path in \a attr.da_rel_path. The user is responsible to free \a
 * attr.da_rel_path. This mode is provided as a convenience to IO middleware
 * libraries and to settle on a unified format for a mode where users know the
 * pool and container uuids and would just like to pass them directly instead of
 * a traditional path. The format of this path should be:
 *  daos://pool_uuid/container_uuid/xyz
 * xyz here can be a path relative to the root of a POSIX container if the user
 * is accessing a posix container, or it can be empty for example in the case of
 * an HDF5 file.
 *
 * \param[in]		path	Valid path in an existing namespace.
 * \param[in,out]	attr	Struct containing the xattrs on the path.
 *
 * \return		0 on Success. errno code on failure.
 */
int
duns_resolve_path(const char *path, struct duns_attr_t *attr);

/**
 * Destroy a container and remove the path associated with it in the UNS.
 *
 * \param[in]	poh	Pool handle
 * \param[in]	path	Valid path in an existing namespace.
 *
 * \return		0 on Success. errno code on failure.
 */
int
duns_destroy_path(daos_handle_t poh, const char *path);

/**
 * Convert a string into duns_attr_t.
 *
 * \param[in]	str	Input string
 * \param[in]	len	Length of input string
 * \param[out]	attr	Struct containing the xattrs on the path.
 *
 * \return		0 on Success. errno code on failure.
 */
int
duns_parse_attr(char *str, daos_size_t len, struct duns_attr_t *attr);

#if defined(__cplusplus)
}
#endif
#endif /* __DAOS_UNS_H__ */
