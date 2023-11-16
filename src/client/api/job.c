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
	char	 env[1024];
	int	 rc;

	rc = d_getenv_str(env, sizeof(env), JOBID_ENV);
	if (rc == -DER_NONEXIST) {
		D_STRNDUP_S(dc_jobid_env, DEFAULT_JOBID_ENV);
	} else {
		D_STRNDUP(dc_jobid_env, env, MAX_ENV_NAME);
	}
	if (dc_jobid_env == NULL)
		D_GOTO(out_err, rc = -DER_NOMEM);

	rc = d_getenv_str(env, sizeof(env), dc_jobid_env);
	if (rc == -DER_NONEXIST) {
		rc = craft_default_jobid(&dc_jobid_env);
		if (rc)
			D_GOTO(out_env, rc);
	} else {
		D_STRNDUP(dc_jobid, env, MAX_JOBID_LEN);
		if (dc_jobid == NULL)
			D_GOTO(out_env, rc = -DER_NOMEM);
	}

	D_INFO("Using JOBID ENV: %s\n", dc_jobid_env);
	D_INFO("Using JOBID %s\n", dc_jobid);
	return -DER_SUCCESS;

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
