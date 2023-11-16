/*
 * (C) Copyright 2019-2023 Intel Corporation.
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
	char	*dir_path;
	char	 env[1024];
	int	 rc;

	rc = d_getenv_str(env, sizeof(env), DAOS_AGENT_DRPC_DIR_ENV);
	dir_path = (rc == -DER_NONEXIST)?NULL:env;
	if (dir_path)
		D_ASPRINTF(path, "%s/%s", dir_path,
				DAOS_AGENT_DRPC_SOCK_NAME);
	else
		D_STRNDUP_S(path, DEFAULT_DAOS_AGENT_DRPC_SOCK);

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
