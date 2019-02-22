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

#ifndef __SECURITY_SRV_INTERNAL_H__
#define __SECURITY_SRV_INTERNAL_H__

#include <daos_types.h>
#include "security.pb-c.h"
#include <daos_srv/daos_server.h>

#define DAOS_SEC_VERSION 1

extern char *ds_sec_server_socket_path;

/**
 * Definitions for DAOS server dRPC modules and their methods.
 * These numeric designations are used in dRPC communications in the Drpc__Call
 * structure.
 */

/**
 *  Module: Security Server
 *
 *  The server module that deals with client security requests.
 */
#define DRPC_MODULE_SECURITY_SERVER				1

/**
 * Method: Validate Security Credential
 *
 * Requests validation of the security credential.
 */
#define DRPC_METHOD_SECURITY_SERVER_VALIDATE_CREDENTIALS	101

int ds_sec_validate_credentials(daos_iov_t *creds, AuthToken **token);

#endif /* __SECURITY_SRV_INTERNAL_H__ */
