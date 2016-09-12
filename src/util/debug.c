/**
 * (C) Copyright 2016 Intel Corporation.
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of cart, it implements the debug subsystem based on clog.
 */

#include <stdlib.h>
#include <stdio.h>

#include <crt_errno.h>
#include <crt_util/common.h>

#define CRT_LOG_FILE_ENV	"CRT_LOG_FILE"
#define CRT_LOG_MASK_ENV	"CRT_LOG_MASK"

bool crt_log_initialized;
int crt_logfac;
int crt_mem_logfac;
int crt_misc_logfac;

#define CLOG_MAX_FAC_HINT	(128)

/**
 * Setup the clog facility names and mask.
 *
 * \param masks [IN]	 masks in crt_log_setmasks() format, or NULL.
 */
static inline int
setup_clog_facnamemask(char *masks)
{
	int rc;

	/* first add the clog/mem/misc facility */
	rc = crt_add_log_facility("CLOG", "CLOG");
	if (rc < 0) {
		C_PRINT_ERR("crt_add_log_facility CLOG failed.\n");
		C_GOTO(out, rc = -CER_UNINIT);
	}
	rc = crt_add_log_facility("MEM", "memory");
	if (rc < 0) {
		C_PRINT_ERR("crt_add_log_facility CLOG failed.\n");
		C_GOTO(out, rc = -CER_UNINIT);
	}
	rc = crt_add_log_facility("MISC", "miscellaneous");
	if (rc < 0) {
		C_PRINT_ERR("crt_add_log_facility CLOG failed.\n");
		C_GOTO(out, rc = -CER_UNINIT);
	}
	rc = 0;
	/* add CRT specific log facility */
	crt_logfac = crt_add_log_facility("CRT", "CaRT");
	if (crt_logfac < 0) {
		C_PRINT_ERR("crt_add_log_facility failed, crt_logfac %d.\n",
			    crt_logfac);
		C_GOTO(out, rc = -CER_UNINIT);
	}
	/* finally handle any crt_log_setmasks() calls */
	if (masks != NULL)
		crt_log_setmasks(masks, -1);

out:
	return rc;
}

int
crt_debug_init(void)
{
	char	*log_file;
	char	*log_mask;
	int	 rc = 0;

	log_file = getenv(CRT_LOG_FILE_ENV);
	log_mask = getenv(CRT_LOG_MASK_ENV);

	if (log_file == NULL || strlen(log_file) == 0) {
		C_PRINT("ENV %s not valid, will log to stdout.\n",
			CRT_LOG_FILE_ENV);
		log_file = "/dev/stdout";
	} else {
		C_PRINT("ENV %s found: %s, will log to it.\n",
			CRT_LOG_FILE_ENV, log_file);
	}

	crt_log_initialized = false;
	if (crt_log_open((char *)"CaRT" /* tag */, CLOG_MAX_FAC_HINT,
		      CLOG_WARN /* default_mask */, CLOG_EMERG/* stderr_mask */,
		      log_file, CLOG_LOGPID /* flags */) == 0) {
		rc = setup_clog_facnamemask(log_mask);
		if (rc != 0)
			goto out;
		crt_log_initialized = true;
	} else {
		fprintf(stderr, "crt_log_open failed.\n");
		C_GOTO(out, rc = -CER_UNINIT);
	}

out:
	if (rc != 0)
		C_PRINT_ERR("crt_debug_init failed, rc: %d.\n", rc);
	return rc;
}

void crt_debug_fini(void)
{
	crt_log_close();
	crt_log_initialized = false;
}

static __thread char thread_uuid_str_buf[CF_UUID_MAX][CRT_UUID_STR_SIZE];
static __thread int thread_uuid_str_buf_idx;

char *
CP_UUID(const void *uuid)
{
	char *buf = thread_uuid_str_buf[thread_uuid_str_buf_idx];

	uuid_unparse_lower(uuid, buf);
	thread_uuid_str_buf_idx = (thread_uuid_str_buf_idx + 1) % CF_UUID_MAX;
	return buf;
}
