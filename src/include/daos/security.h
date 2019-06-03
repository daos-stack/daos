/*
 * (C) Copyright 2018 Intel Corporation.
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

#ifndef __DAOS_SECURITY_INT_H__
#define __DAOS_SECURITY_INT_H__

#include <stdint.h>
#include <stddef.h>
#include <daos_types.h>


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

#endif /* __DAOS_SECURITY_INT_H__ */
