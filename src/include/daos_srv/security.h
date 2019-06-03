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
 * Structure representing the pool's ownership by user and group, respectively.
 */
struct pool_owner {
	char *user;	/** name of the user owner */
	char *group;	/** name of the group owner */
};

/**
 * Determine whether the provided credentials can access a pool.
 *
 * \param[in]	acl		Access Control List for pool
 * \param[in]	ownership	Pool ownership information
 * \param[in]	cred		Credentials of user attempting access
 * \param[in]	capas		Requested access capabilities (DAOS_PC_* flags
 *				from include/daos_types.h)
 *
 * \return	0		Requested access is allowed
 *		-DER_NO_PERM	Requested access is forbidden
 *		-DER_INVAL	Invalid parameter
 *		-DER_BADPATH	Can't connect to the control plane socket at
 *				the expected path
 *		-DER_NOMEM	Out of memory
 *		-DER_NOREPLY	No response from control plane
 *		-DER_MISC	Error in control plane communications
 *		-DER_PROTO	Unexpected or corrupt payload from control plane
 */
int
ds_sec_check_pool_access(struct daos_acl *acl, struct pool_owner *ownership,
			 d_iov_t *cred, uint64_t capas);

#endif /* __DAOS_SRV_SECURITY_H__ */
