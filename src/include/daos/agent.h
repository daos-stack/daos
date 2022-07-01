/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_AGENT_H__
#define __DAOS_AGENT_H__

#include <daos/drpc.h>

/**
 *  Called during library initialization to craft socket path for agent.
 */
int
dc_agent_init(void);

/**
 *  Called during library finalization to free allocated agent resources
 */
void
	     dc_agent_fini(void);

/**
 * Path to be used to communicate with the DAOS Agent set at library init.
 */
extern char *dc_agent_sockpath;

/**
 * Default runtime directory for daos_agent
 */
#define DAOS_AGENT_DRPC_DIR          "/var/run/daos_agent/"

/**
 * Environment variable for specifying an alternate dRPC socket path
 */
#define DAOS_AGENT_DRPC_DIR_ENV      "DAOS_AGENT_DRPC_DIR"

/**
 * Socket name used to craft path from environment variable
 */
#define DAOS_AGENT_DRPC_SOCK_NAME    "daos_agent.sock"

/**
 * Default Unix Domain Socket path for the DAOS agent dRPC connection
 */
#define DEFAULT_DAOS_AGENT_DRPC_SOCK (DAOS_AGENT_DRPC_DIR DAOS_AGENT_DRPC_SOCK_NAME)

#endif /* __DAOS_AGENT_H__ */
