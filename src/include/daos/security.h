/*
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_SECURITY_INT_H__
#define __DAOS_SECURITY_INT_H__

#include <stdint.h>
#include <stddef.h>
#include <daos_types.h>
#include <daos_prop.h>

/** Structure representing a resource's ownership by user and group, respectively. */
struct d_ownership {
	char *user;	/** name of the user owner */
	char *group;	/** name of the group owner */
};

/**
 * Request the security credentials for the current user from the DAOS agent.
 *
 * The security credentials are a blob of bytes that contains security
 * information that can be interpreted by relevant readers.
 *
 * The DAOS agent must be alive and listening on the configured agent socket.
 *
 * \param[out]	creds		Returned security credentials for current user.
 *
 * \return	0		Success. The security credential has
 *				been returned in \a creds.
 *		-DER_INVAL	Invalid parameter
 *		-DER_BADPATH	Can't connect to the agent socket at
 *				the expected path
 *		-DER_NOMEM	Out of memory
 *		-DER_NOREPLY	No response from agent
 *		-DER_MISC	Invalid response from agent
 */
int dc_sec_request_creds(d_iov_t *creds);

/**
 * Request security credentials with per-pool node certificate.
 *
 * Like dc_sec_request_creds(), but also requests a per-pool node certificate
 * and proof-of-possession from the agent. Used by pool connect to support
 * per-pool node certificate authentication.
 *
 * \param[out]	creds		Returned security credentials for current user.
 * \param[in]	pool_uuid	Pool UUID.
 * \param[in]	handle_uuid	Pool handle UUID for PoP binding.
 * \param[out]	node_cert	Returned PEM node certificate (may be empty).
 * \param[out]	node_cert_pop	Returned PoP signature + payload (may be empty).
 *
 * \return	Same as dc_sec_request_creds().
 */
int dc_sec_request_pool_creds(d_iov_t *creds, uuid_t pool_uuid,
			      uuid_t handle_uuid, d_iov_t *node_cert,
			      d_iov_t *node_cert_pop,
			      d_iov_t *node_cert_payload);

/**
 * Request a user's permissions for a specific pool.
 *
 * \param[in]	pool_prop	Pool property containing pool ACL and owner/group
 * \param[in]	uid		Uid of the local user whose permissions to look up
 * \param[in]	gids		Gids of the user's groups
 * \param[in]	nr_gids		Length of the gids list
 * \param[out]	perms		Bitmap representing the user's permissions. Bits are defined
 *				in enum daos_acl_perm.
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		-DER_NONEXIST	UID or GID not found on the system
 *		-DER_NOMEM	Could not allocate memory
 */
int
dc_sec_get_pool_permissions(daos_prop_t *pool_prop, uid_t uid, gid_t *gids, size_t nr_gids,
			    uint64_t *perms);

/**
 * Request a user's permissions for a specific container.
 *
 * \param[in]	cont_prop	Container property containing pool ACL and owner/group
 * \param[in]	uid		Uid of the local user whose permissions to look up
 * \param[in]	gids		Gids of the user's groups
 * \param[in]	nr_gids		Length of the gids list
 * \param[out]	perms		Bitmap representing the user's permissions. Bits are defined
 *				in enum daos_acl_perm.
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		-DER_NONEXIST	UID or GID not found on the system
 *		-DER_NOMEM	Could not allocate memory
 */
int
dc_sec_get_cont_permissions(daos_prop_t *cont_prop, uid_t uid, gid_t *gids, size_t nr_gids,
			    uint64_t *perms);

#endif /* __DAOS_SECURITY_INT_H__ */
