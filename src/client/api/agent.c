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
