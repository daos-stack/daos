/*
 * (C) Copyright 2020-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdlib.h>
#include <sys/utsname.h>
#include <daos/common.h>
#include <daos/job.h>

char *dc_jobid_env;
char *dc_jobid;

static int
craft_default_jobid(char **jobid)
{
	struct utsname	name = {0};
	pid_t		pid;
	int		ret;

	ret = uname(&name);
	if (ret) {
		D_ERROR("Uname to get uname for creating default jobid\n");
		return daos_errno2der(errno);
	}

	pid = getpid();

	D_ASPRINTF(*jobid, "%s-%d", name.nodename, pid);
	if (*jobid == NULL)
		return -DER_NOMEM;
	return 0;
}

int
dc_job_init(void)
{
	char *jobid_env;
	char *jobid;
	int   rc = 0;

	rc = d_agetenv_str(&jobid_env, JOBID_ENV);
	if (jobid_env == NULL) {
		D_STRNDUP_S(jobid_env, DEFAULT_JOBID_ENV);
		if (jobid_env == NULL)
			D_GOTO(out_err, rc = -DER_NOMEM);
	}

	dc_jobid_env = jobid_env;

	rc = d_agetenv_str(&jobid, dc_jobid_env);
	if (jobid == NULL) {
		rc = craft_default_jobid(&jobid);
		if (rc)
			D_GOTO(out_env, rc);
	}

	dc_jobid = jobid;

	D_INFO("Using JOBID ENV: %s\n", dc_jobid_env);
	D_INFO("Using JOBID %s\n", dc_jobid);
	return 0;

out_env:
	D_FREE(dc_jobid_env);
out_err:
	return rc;
}

void
dc_job_fini()
{
	D_FREE(dc_jobid);
	D_FREE(dc_jobid_env);
}
