/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
 * This file is part of daos
 *
 * common/debug.c
 *
 */
#include <limits.h>
#include <daos/common.h>

/* default debug log file */
#define DAOS_LOG_DEFAULT	"/tmp/daos.log"

#define DAOS_DBG_MAX_LEN	(32)
#define DAOS_FAC_MAX_LEN	(128)


static int dd_ref;
static pthread_mutex_t dd_lock = PTHREAD_MUTEX_INITIALIZER;

DAOS_FOREACH_DB(D_LOG_INSTANTIATE_DB, DAOS_FOREACH_DB)
DAOS_FOREACH_LOG_FAC(D_LOG_INSTANTIATE_FAC, DAOS_FOREACH_DB)

/* debug bit groups */
#define DB_GRP1 (DB_IO | DB_MD | DB_PL | DB_REBUILD | DB_SEC | DB_CSUM)

static void
debug_fini_locked(void)
{
	int	rc;

	/* Deregister daos specific error codes */
	daos_errno_fini();

	D_LOG_DEREGISTER_DB(DAOS_FOREACH_DB);

	daos_fail_fini();
	/* Unregister DAOS debug bit groups */
	rc = d_log_dbg_grp_dealloc("daos_default");
	if (rc < 0)
		D_PRINT_ERR("Error deallocating daos debug group\n");

	d_log_fini();
}

/**
 * I/O bypass descriptor, all supported I/O bypass mode should be put in
 * the dictionary below.
 */
struct io_bypass {
	int	 iob_bit;	/**< bit flag for bypass mode */
	char	*iob_str;	/**< string name for bypass mode */
};

struct io_bypass io_bypass_dict[] = {
	{
		.iob_bit	= IOBP_CLI_RPC,
		.iob_str	= IOBP_ENV_CLI_RPC,
	},
	{
		.iob_bit	= IOBP_SRV_BULK,
		.iob_str	= IOBP_ENV_SRV_BULK,
	},
	{
		.iob_bit	= IOBP_TARGET,
		.iob_str	= IOBP_ENV_TARGET,
	},
	{
		.iob_bit	= IOBP_NVME,
		.iob_str	= IOBP_ENV_NVME,
	},
	{
		.iob_bit	= IOBP_PM,
		.iob_str	= IOBP_ENV_PM,
	},
	{
		.iob_bit	= IOBP_PM_SNAP,
		.iob_str	= IOBP_ENV_PM_SNAP,
	},
	{
		.iob_bit	= IOBP_INLINE_COPY,
		.iob_str	= IOBP_ENV_INLINE_COPY,
	},
	{
		.iob_bit	= IOBP_PM_TX,
		.iob_str	= IOBP_ENV_PM_TX,
	},
	{
		.iob_bit	= IOBP_OFF,
		.iob_str	= NULL,
	},
};

unsigned int daos_io_bypass;

static void
io_bypass_init(void)
{
	char	*str = getenv(DENV_IO_BYPASS);
	char	*tok;

	if (!str)
		return;

	tok = strtok(str, ",");
	while (tok) {
		struct io_bypass *iob;

		str = strtok(NULL, ",");
		if (str)
			str[-1] = '\0';

		tok = daos_str_trimwhite(tok);
		for (iob = &io_bypass_dict[0]; iob->iob_str; iob++) {
			if (strcasecmp(tok, iob->iob_str) == 0) {
				daos_io_bypass |= iob->iob_bit;
				D_PRINT("debugging mode: %s is disabled\n",
				       iob->iob_str);
			}
		}
		tok = str;
	};
}

void
daos_debug_fini(void)
{
	D_MUTEX_LOCK(&dd_lock);
	dd_ref--;
	if (dd_ref == 0)
		debug_fini_locked();
	D_MUTEX_UNLOCK(&dd_lock);
}

/** Initialize debug system */
int
daos_debug_init(char *logfile)
{
	int		rc;

	D_MUTEX_LOCK(&dd_lock);
	if (dd_ref > 0) {
		dd_ref++;
		D_MUTEX_UNLOCK(&dd_lock);
		return 0;
	}

	if (getenv(D_LOG_FILE_ENV)) /* honor the env variable first */
		logfile = getenv(D_LOG_FILE_ENV);
	else if (logfile == NULL)
		logfile = DAOS_LOG_DEFAULT;


	rc = d_log_init_adv("DAOS", logfile,
			    DLOG_FLV_FAC | DLOG_FLV_LOGPID | DLOG_FLV_TAG,
			    DLOG_INFO, DLOG_CRIT);
	if (rc != 0) {
		D_PRINT_ERR("Failed to init DAOS debug log: "DF_RC"\n",
			DP_RC(rc));
		goto failed_unlock;
	}

	rc = D_LOG_REGISTER_FAC(DAOS_FOREACH_LOG_FAC);
	if (rc != 0) /* Just print a message but no need to fail */
		D_PRINT_ERR("Failed to register daos log facilities: "DF_RC"\n",
			DP_RC(rc));

	rc = D_LOG_REGISTER_DB(DAOS_FOREACH_DB);
	if (rc != 0) /* Just print a message but no need to fail */
		D_PRINT_ERR("Failed to register daos debug bits: "DF_RC"\n",
			DP_RC(rc));

	/* Register DAOS debug bit groups */
	rc = d_log_dbg_grp_alloc(DB_GRP1, "daos_default", D_LOG_SET_AS_DEFAULT);
	if (rc < 0) {
		D_PRINT_ERR("Error allocating daos debug group: "DF_RC"\n",
			DP_RC(rc));
		rc = -DER_UNINIT;
		goto failed_unlock;
	}

	/* Sync DAOS debug env with libgurt */
	d_log_sync_mask();

	rc = daos_fail_init();
	if (rc) {
		D_PRINT_ERR("Failed to init DAOS fail injection: "DF_RC"\n",
			DP_RC(rc));
		goto failed_unlock;
	}

	io_bypass_init();
	dd_ref = 1;
	D_MUTEX_UNLOCK(&dd_lock);

	/* Register daos specific error codes */
	rc = daos_errno_init();
	if (rc != 0) {
		D_ERROR("DAOS error strings could not be registered: "DF_RC"\n",
			DP_RC(rc));
		/* Ignore the error as it only affects new daos error codes
		 * log messages.
		 */
	}

	return 0;

failed_unlock:
	D_MUTEX_UNLOCK(&dd_lock);
	return rc;
}

static __thread char thread_uuid_str_buf[DF_UUID_MAX][DAOS_UUID_STR_SIZE];
static __thread int thread_uuid_str_buf_idx;

char *
DP_UUID(const void *uuid)
{
	char *buf = thread_uuid_str_buf[thread_uuid_str_buf_idx];

	if (uuid == NULL)
		snprintf(buf, DAOS_UUID_STR_SIZE, "?");
	else
		uuid_unparse_lower(uuid, buf);
	thread_uuid_str_buf_idx = (thread_uuid_str_buf_idx + 1) % DF_UUID_MAX;
	return buf;
}

