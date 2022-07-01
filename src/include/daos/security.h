/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
int
dc_sec_request_creds(d_iov_t *creds);

#endif /* __DAOS_SECURITY_INT_H__ */
