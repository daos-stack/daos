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
	char *path = NULL;
	char *envpath;

	d_agetenv_str(&envpath, DAOS_AGENT_DRPC_DIR_ENV);
	if (envpath != NULL)
		D_ASPRINTF(path, "%s/%s", envpath,
				DAOS_AGENT_DRPC_SOCK_NAME);
	else
		D_STRNDUP_S(path, DEFAULT_DAOS_AGENT_DRPC_SOCK);
	d_freeenv_str(&envpath);

	if (path == NULL)
		return -DER_NOMEM;

	dc_agent_sockpath = path;
	return 0;
}

void
dc_agent_fini()
{
	D_FREE(dc_agent_sockpath);
}
