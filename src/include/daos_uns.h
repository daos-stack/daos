/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
	/** IN: Pool uuid of the container. */
	uuid_t			da_puuid;
	/** IN: Container uuid that is created for the path. */
	uuid_t			da_cuuid;
	/** IN: Container layout (POSIX, HDF5) */
	daos_cont_layout_t	da_type;
	/** IN: (Optional) Object Class for all objects in the container */
	daos_oclass_id_t	da_oclass_id;
	/** IN: (Optional) Chunk size for all files in container */
	daos_size_t		da_chunk_size;
	/** OUT: Relative component of path from where the UNS entry is located.
	 *
	 * This is returned if the UNS entry is not the last entry in the path,
	 * and the UNS library performs a reverse lookup to find a UNS entry in
	 * the path. To check only the last entry in the path and not return
	 * this relative path to that entry, set \a da_no_reverse_lookup. User
	 * is responsible to free this if it is returned.
	 */
	char			*da_rel_path;
	/** IN: (Optional Container props to be added with duns_path_create */
	daos_prop_t		*da_props;
	/** OUT: This is set to true if path is on Lustre filesystem */
	bool			da_on_lustre;
	/** IN: String does not include daos:// prefix
	 *
	 * Path that is passed does not have daos: prefix but is direct:
	 * (/puuid/cuuid/xyz) and does not need to parse a path UNS attrs.
	 * This is usually set to false.
	 */
	bool			da_no_prefix;
	/** IN: look only at the last entry in the path for duns_resolve_path */
	bool			da_no_reverse_lookup;
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
 * rest of the values are not populated.  By default, this call does a reverse
 * lookup on the realpath until it finds an entry in the path that has the UNS
 * attr. The rest of the path from that entry point is returned in \a
 * attr.da_rel_path which the user is responsible to free.  If the entire path
 * does not have the entry, ENODATA error is returned. To avoid doing the
 * reverse lookup and check only that last entry, set \a
 * attr.da_no_reverse_lookup.
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
 * \param[in,out]	attr	Struct containing the attrs on the path.
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
 * \param[out]	attr	Struct containing the attrs on the path.
 *
 * \return		0 on Success. errno code on failure.
 */
int
duns_parse_attr(char *str, daos_size_t len, struct duns_attr_t *attr);

#if defined(__cplusplus)
}
#endif
#endif /* __DAOS_UNS_H__ */
