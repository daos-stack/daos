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
#include <daos_srv/daos_server.h>
#include <daos_srv/pool.h>

/**
 * Determine whether the provided credentials can access a pool.
 *
 * \param[in]	attr		Pool attributes
 * \param[in]	cred		Opaque Credential Data
 * \param[in]	access		Requested pool access
 *
 * \return	0		Success. The access is allowed.
 *		-DER_INVAL	Invalid parameter
 *		-DER_BADPATH	Can't connect to the agent socket at
 *				the expected path
 *		-DER_NOMEM	Out of memory
 *		-DER_NOREPLY	No response from agent
 *		-DER_MISC	Invalid response from agent
 */
int ds_sec_can_pool_connect(const struct pool_attr *attr, d_iov_t *cred,
				uint64_t access);
#endif /* __DAOS_SRV_SECURITY_H__ */
