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

#ifndef __DAOS_DRPC_MODULES_H__
#define __DAOS_DRPC_MODULES_H__

/**
 * DAOS dRPC Modules
 *
 * dRPC modules are used to multiplex communications over the Unix Domain Socket
 * to appropriate handlers. They are populated in the Drpc__Call structure.
 *
 * dRPC module IDs must be unique. This is a list of all DAOS dRPC modules.
 */

enum drpc_module {
	DRPC_MODULE_TEST		= 0,	/* Reserved for testing */
	DRPC_MODULE_SEC_AGENT		= 1,	/* daos_agent security */
	DRPC_MODULE_MGMT		= 2,	/* daos_server mgmt */
	DRPC_MODULE_SRV			= 3,	/* daos_server */
	DRPC_MODULE_SEC			= 4,	/* daos_server security */

	NUM_DRPC_MODULES			/* Must be last */
};

enum drpc_sec_agent_method {
	DRPC_METHOD_SEC_AGENT_REQUEST_CREDS	= 101,

	NUM_DRPC_SEC_AGENT_METHODS		/* Must be last */
};

enum drpc_mgmt_method {
	DRPC_METHOD_MGMT_KILL_RANK		= 201,
	DRPC_METHOD_MGMT_SET_RANK		= 202,
	DRPC_METHOD_MGMT_CREATE_MS		= 203,
	DRPC_METHOD_MGMT_START_MS		= 204,
	DRPC_METHOD_MGMT_JOIN			= 205,
	DRPC_METHOD_MGMT_GET_ATTACH_INFO	= 206,
	DRPC_METHOD_MGMT_POOL_CREATE		= 207,
	DRPC_METHOD_MGMT_POOL_DESTROY		= 208,
	DRPC_METHOD_MGMT_SET_UP			= 209,
	DRPC_METHOD_MGMT_BIO_HEALTH_QUERY	= 210,
	DRPC_METHOD_MGMT_SMD_LIST_DEVS		= 211,
	DRPC_METHOD_MGMT_SMD_LIST_POOLS		= 212,
	DRPC_METHOD_MGMT_POOL_GET_ACL		= 213,
	DRPC_METHOD_MGMT_LIST_POOLS		= 214,
	DRPC_METHOD_MGMT_POOL_OVERWRITE_ACL	= 215,
	DRPC_METHOD_MGMT_POOL_UPDATE_ACL	= 216,
	DRPC_METHOD_MGMT_POOL_DELETE_ACL	= 217,
	DRPC_METHOD_MGMT_PREP_SHUTDOWN		= 218,
	DRPC_METHOD_MGMT_DEV_STATE_QUERY	= 219,
	DRPC_METHOD_MGMT_DEV_SET_FAULTY		= 220,
	DRPC_METHOD_MGMT_DEV_REPLACE		= 221,
	DRPC_METHOD_MGMT_LIST_CONTAINERS	= 222,
	DRPC_METHOD_MGMT_POOL_QUERY		= 223,
	DRPC_METHOD_MGMT_POOL_SET_PROP		= 224,
	DRPC_METHOD_MGMT_PING_RANK		= 225,
	DRPC_METHOD_MGMT_REINTEGRATE		= 226,
	DRPC_METHOD_MGMT_CONT_SET_OWNER		= 227,
	DRPC_METHOD_MGMT_EXCLUDE		= 228,
	DRPC_METHOD_MGMT_EXTEND			= 229,
	DRPC_METHOD_MGMT_POOL_EVICT		= 230,
	DRPC_METHOD_MGMT_DRAIN			= 231,
	DRPC_METHOD_MGMT_GROUP_UPDATE		= 232,
	DRPC_METHOD_MGMT_DISCONNECT		= 233,
	DRPC_METHOD_MGMT_DEV_IDENTIFY		= 234,

	NUM_DRPC_MGMT_METHODS			/* Must be last */
};

enum drpc_srv_method {
	DRPC_METHOD_SRV_NOTIFY_READY		= 301,
	DRPC_METHOD_SRV_BIO_ERR			= 302,
	DRPC_METHOD_SRV_GET_POOL_SVC		= 303,

	NUM_DRPC_SRV_METHODS			/* Must be last */
};

enum drpc_sec_method {
	DRPC_METHOD_SEC_VALIDATE_CREDS		= 401,

	NUM_DRPC_SEC_METHODS			/* Must be last */
};

#endif /* __DAOS_DRPC_MODULES_H__ */
