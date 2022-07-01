/*
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DAOS_JOB_H__
#define __DAOS_JOB_H__

/**
 *  Called during library initialization to extract the jobid.
 */
int
dc_job_init(void);

/**
 *  Called during library finalization to free allocated jobid resources
 */
void
	     dc_job_fini(void);

/**
 * Environment variable name that holds the jobid for this invocation.
 */
extern char *dc_jobid_env;

/**
 * jobid for this invocation.
 */
extern char *dc_jobid;

/**
 * Environment variable used to contain envar name where jobid is stored
 */
#define JOBID_ENV         "DAOS_JOBID_ENV"

/**
 * Default environment variable name for jobid when one is not specified
 */
#define DEFAULT_JOBID_ENV "DAOS_JOBID"

/*
 * The answer of what the max length of envvar name is very tricky. Arguments
 * and environment variable share the same memory space so to make things easy
 * we enforce an arbitrary length of 80 which some other shells enforce
 */
#define MAX_ENV_NAME      80

#define MAX_JOBID_LEN     1024

#endif /* __DAOS_JOB_H__ */
