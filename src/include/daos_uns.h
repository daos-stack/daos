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
 * The unified namespace API provides functions and tools to be able to link files and directories
 * in a system namespace to a location in the DAOS tier (pool and container), in addition to other
 * properties such as object class.
 */

#ifndef __DAOS_UNS_H__
#define __DAOS_UNS_H__

#if defined(__cplusplus)
extern "C" {
#endif

/** Flags for duns_resolve_path */
enum {
	/*
	 * String does not include daos:// prefix
	 * Path that is passed does not have daos: prefix but is direct:
	 * (/puuid/cuuid/xyz) and does not need to parse a path UNS attrs.
	 * This is usually not set.
	 */
	DUNS_NO_PREFIX = (1 << 0),

	/* look only at the last entry in the path. */
	DUNS_NO_REVERSE_LOOKUP = (1 << 1),

	/*
	 * check only for direct path.
	 * Do not attempt to get the extended attribute of the path, and assume
	 * the path is a direct path that is either of format:
	 *   - /puuid/cuuid/xyz
	 *   - /pool_label/container_label/xyz
	 * This is usually not set.
	 */
	DUNS_NO_CHECK_PATH = (1 << 2),
};

/** struct that has the values to make the connection from the UNS to DAOS */
struct duns_attr_t {
	/** IN/OUT: Container layout (POSIX, HDF5, Python, etc.) */
	daos_cont_layout_t	da_type;
	/** IN: (Optional) For a POSIX container, set a default object class for all objects. */
	daos_oclass_id_t	da_oclass_id;
	/** IN: (Optional) For a POSIX container, set a default chunk size for all files. */
	daos_size_t		da_chunk_size;
	/** IN: (Optional Container props to be added with duns_path_create */
	daos_prop_t		*da_props;
	/** IN: access flags
	 *
	 * DUNS_NO_PREFIX
	 * DUNS_NO_REVERSE_LOOKUP
	 * DUNS_NO_CHECK_PATH:
	 */
	uint32_t		da_flags;
	/** OUT: Pool UUID or label string.
	 *
	 * On duns_resolve_path(), a UUID string is returned for the pool that was stored on that
	 * path. If the path is a direct path, we parse the first entry (pool) in the path as either
	 * a UUID or a label. This can be used in daos_pool_connect() regardless of whether it's a
	 * UUID or label.
	 */
	char			da_pool[DAOS_PROP_LABEL_MAX_LEN + 1];
	/** OUT: Container UUID or label string.
	 *
	 * On duns_resolve_path(), a UUID string is returned for the container that was stored on
	 * that path. If the path is a direct path, we parse the second entry (cont) in the path as
	 * either a UUID or a label. This can be used in daos_cont_open() regardless of whether it's
	 * a UUID or label. on duns_create_path(), the uuid of the container created is also
	 * populated in this field.
	 */
	char			da_cont[DAOS_PROP_LABEL_MAX_LEN + 1];
	/** OUT: DAOS System Name. (The UNS does not maintain this yet, and this is not set)
	 *
	 * On duns_resolve_path(), the daos system name is returned that can be used on
	 * daos_pool_connect().
	 */
	char			*da_sys;
	/** OUT: Relative component of path from where the UNS entry is located (returned on
	 * duns_resolve_path()).
	 *
	 * This is returned if the UNS entry is not the last entry in the path, and the UNS library
	 * performs a reverse lookup to find a UNS entry in the path. To check only the last entry
	 * in the path and not return this relative path to that entry, set DUNS_NO_REVERSE_LOOKUP
	 * on \a da_flags.
	 */
	char			*da_rel_path;
	/** OUT: This is set to true if path is on Lustre filesystem */
	bool			da_on_lustre;
	/** IN: (Deprecated - use flags) String does not include daos:// prefix
	 *
	 * Path that is passed does not have daos: prefix but is direct: (/pool/cont/xyz) and does
	 * not need to parse a path for the UNS attrs.  This is usually set to false.
	 */
	bool			da_no_prefix;
	/** IN/OUT: (Deprecated) Pool UUID of the container to be created in duns_create_path().
	 *
	 * The pool UUID is now obtained from the pool handle in duns_create_path(). The pool UUID
	 * is returned as a string in \a da_pool with duns_resolve_path().
	 */
	uuid_t			da_puuid;
	/** IN/OUT: (Deprecated) Optional UUID of the cont to be created in duns_create_path().
	 *
	 * The UUID will be used to create the container in duns_create_path() if set, otherwise a
	 * random one will be generated. The cont UUID or label is returned as a string in \a
	 * da_cont with duns_resolve_path().
	 */
	uuid_t			da_cuuid;
};

/** extended attribute name that will store the UNS info */
#define DUNS_XATTR_NAME		"user.daos"
/** Length of the extended attribute */
#define DUNS_MAX_XATTR_LEN	170
/** Format of daos attributes in the extended attribute */
#define DUNS_XATTR_FMT		"DAOS.%s://%36s/%36s"
/**
 * Lustre specific foreign LOV/LMV format (container type will be encoded in
 * lfm_flag field and extra slashes will be added when needed by foreign_symlink
 * Lustre code)
 */
#define DUNS_LUSTRE_XATTR_FMT		"%36s/%36s"

/**
 * Create a special directory (POSIX) or file (HDF5) depending on the container type, and create a
 * new DAOS container. The uuid of the container can be either passed in \a attrp->da_cuuid
 * (deprecated) or generated internally and returned in \a da_cont.
 * The extended attributes are set on the dir/file created that points to pool uuid, container
 * uuid. This is to be used in a unified namespace solution to be able to map a path in the unified
 * namespace to a location in the DAOS tier.  The container and pool can have labels, but the UNS
 * stores the uuids only and so labels are ignored in \a attrp.
 * The user is not required to call duns_destory_attrs on \a attrp as this call does not allocate
 * any buffers in attrp.
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
duns_create_path(daos_handle_t poh, const char *path, struct duns_attr_t *attrp);

/**
 * Retrieve the pool and container uuids from a path corresponding to a DAOS location. If this was a
 * path created with duns_create_path(), then this call would return the pool, container, and type
 * values in the \a attr struct (the rest of the values are not populated.  By default, this call
 * does a reverse lookup on the realpath until it finds an entry in the path that has the UNS
 * attr. The rest of the path from that entry point is returned in \a attr.da_rel_path.  If the
 * entire path does not have the entry, ENODATA error is returned. To avoid doing the reverse lookup
 * and check only that last entry, set \a attr.da_no_reverse_lookup.
 *
 * To avoid going through the UNS if the user knows the pool and container uuids, a special format
 * can be passed as a prefix for a "fast path", and this call would parse those out in the \a attr
 * struct and return whatever is left from the path in \a attr.da_rel_path. This mode is provided as
 * a convenience to IO middleware libraries and to settle on a unified format for a mode where users
 * know the pool and container uuids and would just like to pass them directly instead of a
 * traditional path. The format of this path should be:
 *  daos://pool_uuid/container_uuid/xyz
 * xyz here can be a path relative to the root of a POSIX container if the user is accessing a posix
 * container, or it can be empty for example in the case of an HDF5 file.
 *
 * User is responsible to call duns_destroy_attr on \a attr to free the internal buffers allocated.
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

/**
 * Set the system name in the duns struct in case it was obtained in a different way than
 * using duns_resolve_path().
 *
 * \param[in]	attrp	Attr pointer
 * \param[in]	sys	DAOS System name
 *
 * \return              0 on Success. errno code on failure.
 */
int
duns_set_sys_name(struct duns_attr_t *attrp, const char *sys);

/**
 * Free internal buffers allocated by the DUNS on the \a attr struct.
 *
 * \param[in]	attrp	Attr pointer that was passed in to duns_resolve_path.
 */
void
duns_destroy_attr(struct duns_attr_t *attrp);

#if defined(__cplusplus)
}
#endif
#endif /* __DAOS_UNS_H__ */
