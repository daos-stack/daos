/*
 * (C) Copyright 2020 Intel Corporation.
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
#include <daos/common.h>
#include <daos/job.h>

char *dc_jobid_env;
char *dc_jobid;

int dc_job_init()
{
	char *jobid;
	char *jobid_env = getenv(JOBID_ENV);

	if (jobid_env == NULL) {
		D_STRNDUP(jobid_env, DEFAULT_JOBID_ENV,
				sizeof(DEFAULT_JOBID_ENV));
	}
	dc_jobid_env = jobid_env;

	jobid = getenv(dc_jobid_env);
	if (jobid == NULL) {
		D_STRNDUP(jobid, DEFAULT_JOBID,
				sizeof(DEFAULT_JOBID));
	}
	dc_jobid = jobid;
	return 0;
}

void
dc_job_fini()
{
	D_FREE(dc_jobid);
	D_FREE(dc_jobid_env);
}