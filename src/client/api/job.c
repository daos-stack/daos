/*
 * (C) Copyright 2020-2021 Intel Corporation.
 * (C) Copyright 2025 Google LLC
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

static int
get_jobid_env_var(char **jobid_env)
{
	char *tmp_env = NULL;

	d_agetenv_str(&tmp_env, JOBID_ENV);
	if (tmp_env == NULL) {
		D_STRNDUP_S(tmp_env, DEFAULT_JOBID_ENV);
	} else {
		char *dup = tmp_env;

		D_STRNDUP(tmp_env, dup, MAX_ENV_NAME);
		d_freeenv_str(&dup);
	}
	if (tmp_env == NULL)
		return -DER_NOMEM;

	*jobid_env = tmp_env;
	return 0;
}

int
dc_set_default_jobid(const char *default_jobid)
{
	char *jobid_env = NULL;
	char *jobid     = NULL;
	int   rc        = 0;

	/* first, determine which environment variable to check/set */
	rc = get_jobid_env_var(&jobid_env);
	if (rc)
		D_GOTO(out, rc);

	/* next, check to see if a jobid has already been set in the environment */
	d_agetenv_str(&jobid, jobid_env);
	if (jobid != NULL) {
		d_freeenv_str(&jobid);
		goto out;
	}

	/* set it to the default value since it wasn't already set */
	d_setenv(jobid_env, default_jobid, 0);
out:
	D_FREE(jobid_env);
	return rc;
}

int
dc_job_init(void)
{
	char *jobid;
	int   err = 0;

	err = get_jobid_env_var(&dc_jobid_env);
	if (err)
		D_GOTO(out_err, err);

	d_agetenv_str(&jobid, dc_jobid_env);
	if (jobid == NULL) {
		err = craft_default_jobid(&jobid);
		if (err)
			D_GOTO(out_env, err);
	} else {
		char *tmp_jobid = jobid;

		D_STRNDUP(jobid, tmp_jobid, MAX_JOBID_LEN);
		d_freeenv_str(&tmp_jobid);
		if (jobid == NULL)
			D_GOTO(out_env, err = -DER_NOMEM);
	}

	dc_jobid = jobid;

	D_INFO("Using JOBID ENV: %s\n", dc_jobid_env);
	D_INFO("Using JOBID %s\n", dc_jobid);
	return 0;

out_env:
	D_FREE(dc_jobid_env);
out_err:
	return err;
}

void
dc_job_fini()
{
	D_FREE(dc_jobid);
	D_FREE(dc_jobid_env);
}
