/*
 * (C) Copyright 2019-2020 Intel Corporation.
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/**
 * ds_sec: Security Framework Server Internal Declarations
 */

#ifndef __DAOS_SRV_SECURITY_H__
#define __DAOS_SRV_SECURITY_H__

#include <daos_types.h>
#include <daos_security.h>
#include <daos_srv/pool.h>

/**
 * Structure representing a resource's ownership by user and group,
 * respectively.
 */
struct ownership {
	char *user;	/** name of the user owner */
	char *group;	/** name of the group owner */
};

/**
 * Allocate the default ACL for DAOS pools.
 *
 * \return	Newly allocated struct daos_acl
 */
struct daos_acl *
ds_sec_alloc_default_daos_pool_acl(void);

/**
 * Allocate the default ACL for DAOS containers.
 *
 * \return	Newly allocated struct daos_acl
 */
struct daos_acl *
ds_sec_alloc_default_daos_cont_acl(void);

/**
 * Derive the pool security capabilities for the given user credential, using
 * the pool ownership information, pool ACL, and requested flags.
 *
 * \param[in]	flags		Requested DAOS_PC flags
 * \param[in]	cred		User's security credential
 * \param[in]	ownership	Pool ownership information
 * \param[in]	acl		Pool ACL
 * \param[out]	capas		Capability bits for this user
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		-DER_BADPATH	Can't connect to the control plane socket at
 *				the expected path
 *		-DER_NOMEM	Out of memory
 *		-DER_NOREPLY	No response from control plane
 *		-DER_MISC	Error in control plane communications
 *		-DER_PROTO	Unexpected or corrupt payload from control plane
 */
int
ds_sec_pool_get_capabilities(uint64_t flags, d_iov_t *cred,
			     struct ownership *ownership,
			     struct daos_acl *acl, uint64_t *capas);

/**
 * Derive the container security capabilities for the given user credential,
 * using the container ownership information, container ACL, and requested
 * flags.
 *
 * This function assumes the credential was acquired internally and was
 * previously validated with the control plane.
 *
 * \param[in]	flags		Requested DAOS_COO flags
 * \param[in]	cred		User's security credential
 * \param[in]	ownership	Container ownership information
 * \param[in]	acl		Container ACL
 * \param[out]	capas		Capability bits for this user
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		-DER_NOMEM	Out of memory
 */
int
ds_sec_cont_get_capabilities(uint64_t flags, d_iov_t *cred,
			     struct ownership *ownership,
			     struct daos_acl *acl, uint64_t *capas);

/**
 * Determine if the pool connection can be established based on the calculated
 * set of pool capabilities.
 *
 * \param	pool_capas	Capability bits acquired via
 *				ds_sec_pool_get_capabilities
 *
 * \return	True		Access allowed
 *		False		Access denied
 */
bool
ds_sec_pool_can_connect(uint64_t pool_capas);

/**
 * Determine if a pool handle with given security capabilities can create a
 * container.
 *
 * \param	pool_capas	Capability bits acquired via
 *				ds_sec_pool_get_capabilities
 *
 * \return	True		Operation allowed
 *		False		Operation forbidden
 */
bool
ds_sec_pool_can_create_cont(uint64_t pool_capas);

/**
 * Determine if a pool handle with given security capabilities can delete a
 * container.
 *
 * \param	pool_capas	Capability bits acquired via
 *				ds_sec_pool_get_capabilities
 *
 * \return	True		Operation allowed
 *		False		Operation forbidden
 */
bool
ds_sec_pool_can_delete_cont(uint64_t pool_capas);

/**
 * Determine if the container can be opened based on the calculated set of
 * container capabilities.
 *
 * \param	cont_capas	Capability bits acquired via
 *				ds_sec_cont_get_capabilities
 *
 * \return	True		Access allowed
 *		False		Access denied
 */
bool
ds_sec_cont_can_open(uint64_t cont_capas);

/**
 * Determine if the container can be deleted by the user with the given
 * credential, based on the container ACL and ownership information.
 *
 * \param	pool_flags	Parent pool handle flags
 * \param	cred		Pool's security credential
 * \param	ownership	Container ownership information
 * \param	acl		Container ACL
 *
 * \return	True		Operation allowed
 *		False		Operation forbidden
 */
bool
ds_sec_cont_can_delete(uint64_t pool_flags, d_iov_t *cred,
		       struct ownership *ownership, struct daos_acl *acl);

/**
 * Determine if the container properties can be viewed based on the container
 * security capabilities.
 *
 * \param	cont_capas	Capability bits acquired via
 *				ds_sec_cont_get_capabilities
 *
 * \return	True		Access allowed
 *		False		Access denied
 */
bool
ds_sec_cont_can_get_props(uint64_t cont_capas);

/**
 * Determine if the container properties can be modified based on the container
 * security capabilities.
 *
 * \param	cont_capas	Capability bits acquired via
 *				ds_sec_cont_get_capabilities
 *
 * \return	True		Access allowed
 *		False		Access denied
 */
bool
ds_sec_cont_can_set_props(uint64_t cont_capas);

/**
 * Determine if the container Access Control List can be viewed based on the
 * container security capabilities.
 *
 * \param	cont_capas	Capability bits acquired via
 *				ds_sec_cont_get_capabilities
 *
 * \return	True		Access allowed
 *		False		Access denied
 */
bool
ds_sec_cont_can_get_acl(uint64_t cont_capas);

/**
 * Determine if the container Access Control List can be modified based on the
 * container security capabilities.
 *
 * \param	cont_capas	Capability bits acquired via
 *				ds_sec_cont_get_capabilities
 *
 * \return	True		Access allowed
 *		False		Access denied
 */
bool
ds_sec_cont_can_set_acl(uint64_t cont_capas);

/**
 * Determine if the container ownership can be modified based on the container
 * security capabilities.
 *
 * \param	cont_capas	Capability bits acquired via
 *				ds_sec_cont_get_capabilities
 *
 * \return	True		Access allowed
 *		False		Access denied
 */
bool
ds_sec_cont_can_set_owner(uint64_t cont_capas);

/**
 * Determine if the container can be written based on the container security
 * capabilities.
 *
 * \param	cont_capas	Capability bits acquired via
 *				ds_sec_cont_get_capabilities
 *
 * \return	True		Access allowed
 *		False		Access denied
 */
bool
ds_sec_cont_can_write_data(uint64_t cont_capas);

/**
 * Determine if the container can be read based on the container security
 * capabilities.
 *
 * \param	cont_capas	Capability bits acquired via
 *				ds_sec_cont_get_capabilities
 *
 * \return	True		Access allowed
 *		False		Access denied
 */
bool
ds_sec_cont_can_read_data(uint64_t cont_capas);

/**
 * Get the security capabilities for a rebuild container handle created by the
 * DAOS server.
 *
 * @return	Bits representing security capabilities
 */
uint64_t
ds_sec_get_rebuild_cont_capabilities(void);

/**
 * Get the security capabilities for a container handle that can perform
 * administrative tasks.
 *
 * @return	Bits representing security capabilities
 */
uint64_t
ds_sec_get_admin_cont_capabilities(void);

#endif /* __DAOS_SRV_SECURITY_H__ */
