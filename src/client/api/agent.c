/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdlib.h>
#include <daos/agent.h>

char *dc_agent_sockpath;

int
dc_agent_init()
{
	char	*path = NULL;
	char	*envpath = getenv(DAOS_AGENT_DRPC_DIR_ENV);

	if (envpath == NULL) {
		D_STRNDUP(path, DEFAULT_DAOS_AGENT_DRPC_SOCK,
				sizeof(DEFAULT_DAOS_AGENT_DRPC_SOCK));
	} else {
		D_ASPRINTF(path, "%s/%s", envpath,
				DAOS_AGENT_DRPC_SOCK_NAME);
	}

	if (path == NULL) {
		return -DER_NOMEM;
	}

	dc_agent_sockpath = path;
	return 0;
}

void
dc_agent_fini()
{
	D_FREE(dc_agent_sockpath);
}
