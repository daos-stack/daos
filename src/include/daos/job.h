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
#ifndef __DAOS_JOB_H__
#define __DAOS_JOB_H__

/**
 *  Called during library initialization to extract the jobid.
 */
int dc_job_init(void);

/**
 *  Called during library finalization to free allocated jobid resources
 */
void dc_job_fini(void);

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
#define JOBID_ENV "DAOS_JOBID_ENV"

/**
 * Default environment variable name for jobid when one is not specified
 */
#define DEFAULT_JOBID_ENV "DAOS_JOBID"


/*
 * The answer of what the max length of envvar name is very tricky. Arguments
 * and environment variable share the same memory space so to make things easy
 * we enforce an arbitrary length of 80 which some other shells enforce
 */
#define MAX_ENV_NAME 80

#define MAX_JOBID_LEN 1024

#endif /* __DAOS_JOB_H__ */
