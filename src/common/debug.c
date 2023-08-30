/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * common/debug.c
 *
 */
#include <limits.h>
#include <daos/common.h>

#define DAOS_DBG_MAX_LEN	(32)
#define DAOS_FAC_MAX_LEN	(128)


static int dd_ref;
static pthread_mutex_t dd_lock = PTHREAD_MUTEX_INITIALIZER;

DAOS_FOREACH_DB(D_LOG_INSTANTIATE_DB, DAOS_FOREACH_DB)
DAOS_FOREACH_LOG_FAC(D_LOG_INSTANTIATE_FAC, DAOS_FOREACH_DB)

static d_log_id_cb_t	log_id_cb;
/* debug bit groups */
/* default debug group */
#define DB_GRP1 (DB_IO | DB_MD | DB_PL | DB_REBUILD | DB_SEC | DB_CSUM)
#define DB_GRP1_NAME "group_default"
/* metadata debug group (default plus metadata-specific) */
#define DB_GRP_MD (DB_GRP1 | DB_MGMT)
#define DB_GRP_MD_NAME "group_metadata"
/* metadata-only debug group */
#define DB_GRP_MD_ONLY (DB_MD | DB_MGMT)
#define DB_GRP_MD_ONLY_NAME "group_metadata_only"


static void
debug_fini_locked(void)
{
	int	rc;

	D_LOG_DEREGISTER_DB(DAOS_FOREACH_DB);

	daos_fail_fini();
	/* Unregister DAOS debug bit groups */
	rc = d_log_dbg_grp_dealloc(DB_GRP_MD_ONLY_NAME);
	if (rc < 0)
		D_PRINT_ERR("Error deallocating daos debug group: %s\n", DB_GRP_MD_ONLY_NAME);

	rc = d_log_dbg_grp_dealloc(DB_GRP_MD_NAME);
	if (rc < 0)
		D_PRINT_ERR("Error deallocating daos debug group: %s\n", DB_GRP_MD_NAME);

	rc = d_log_dbg_grp_dealloc(DB_GRP1_NAME);
	if (rc < 0)
		D_PRINT_ERR("Error deallocating daos debug group: %s\n", DB_GRP1_NAME);

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
		.iob_bit	= IOBP_SRV_BULK_CACHE,
		.iob_str	= IOBP_ENV_SRV_BULK_CACHE,
	},
	{
		.iob_bit	= IOBP_WAL_COMMIT,
		.iob_str	= IOBP_ENV_WAL_COMMIT,
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
	char	*saved_ptr;

	if (!str)
		return;

	tok = strtok_r(str, ",", &saved_ptr);
	while (tok) {
		struct io_bypass *iob;

		str = strtok_r(NULL, ",", &saved_ptr);
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

void
daos_debug_set_id_cb(d_log_id_cb_t cb)
{
	log_id_cb = cb;
}

/** Initialize debug system */
int
daos_debug_init_ex(char *logfile, d_dbug_t logmask)
{
	int	flags = DLOG_FLV_FAC | DLOG_FLV_LOGPID | DLOG_FLV_TAG;
	int	rc;

	D_MUTEX_LOCK(&dd_lock);
	if (dd_ref > 0) {
		dd_ref++;
		D_MUTEX_UNLOCK(&dd_lock);
		return 0;
	}

	/* honor the env variable first */
	logfile = getenv(D_LOG_FILE_ENV);
	if (logfile == NULL || strlen(logfile) == 0) {
		flags |= DLOG_FLV_STDOUT;
		logfile = NULL;
	} else if (!strncmp(logfile, "/dev/null", 9)) {
		/* Don't set up logging or log to stdout if the log file is /dev/null */
		logfile = NULL;
	}

	rc = d_log_init_adv("DAOS", logfile, flags, logmask, DLOG_CRIT,
			    log_id_cb);
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
	rc = d_log_dbg_grp_alloc(DB_GRP1, DB_GRP1_NAME, D_LOG_SET_AS_DEFAULT);
	if (rc < 0) {
		D_PRINT_ERR("Error allocating daos debug group: "DF_RC"\n",
			DP_RC(rc));
		rc = -DER_UNINIT;
		goto failed_unlock;
	}

	rc = d_log_dbg_grp_alloc(DB_GRP_MD, DB_GRP_MD_NAME, 0);
	if (rc < 0) {
		D_PRINT_ERR("Error allocating debug group '%s': "DF_RC"\n",
			    DB_GRP_MD_NAME, DP_RC(rc));
		rc = -DER_UNINIT;
		goto failed_unlock;
	}

	rc = d_log_dbg_grp_alloc(DB_GRP_MD_ONLY, DB_GRP_MD_ONLY_NAME, 0);
	if (rc < 0) {
		D_PRINT_ERR("Error allocating debug group '%s': "DF_RC"\n",
			    DB_GRP_MD_ONLY_NAME, DP_RC(rc));
		rc = -DER_UNINIT;
		goto failed_unlock;
	}

	/* Sync DAOS debug env with libgurt */
	d_log_sync_mask();

	rc = daos_fail_init();
	if (rc) {
		D_PRINT_ERR("Failed to init DAOS fault injection: "DF_RC"\n",
			DP_RC(rc));
		goto failed_unlock;
	}

	io_bypass_init();
	dd_ref = 1;
	D_MUTEX_UNLOCK(&dd_lock);

	return 0;

failed_unlock:
	D_MUTEX_UNLOCK(&dd_lock);
	return rc;
}

int daos_debug_init(char *logfile)
{
	/* for client side, by default use ERR level */
	return daos_debug_init_ex(logfile, DLOG_ERR);
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

#ifndef DAOS_BUILD_RELEASE
#define DF_KEY_MAX		8

static __thread int thread_key_buf_idx;
static __thread char thread_key_buf[DF_KEY_MAX][DF_KEY_STR_SIZE];

char *
daos_key2str(daos_key_t *key)
{
	char *buf = thread_key_buf[thread_key_buf_idx];

	if (!key->iov_buf || key->iov_len == 0) {
		strcpy(buf, "<NULL>");
	} else {
		int	len = min(key->iov_len, DF_KEY_STR_SIZE - 1);
		char	*akey = key->iov_buf;
		bool	can_print = true;
		bool	is_int = key->iov_len == sizeof(uint64_t);
		int	i;

		for (i = 0 ; i < len ; i++) {
			if (akey[i] == '\0')
				break;
			if (!isprint(akey[i])) {
				can_print = false;
				break;
			}
		}

		if (can_print) {
			if (is_int)
				snprintf(buf, DF_KEY_STR_SIZE, "%.*s uint64:"DF_U64, len, akey,
					 *(uint64_t *)akey);
			else
				snprintf(buf, DF_KEY_STR_SIZE, "%.*s", len, akey);
		} else if (is_int) {
			snprintf(buf, DF_KEY_STR_SIZE, "uint64:"DF_U64, *(uint64_t *)akey);
		} else {
			strcpy(buf, "????");
		}
	}
	thread_key_buf_idx = (thread_key_buf_idx + 1) % DF_KEY_MAX;
	return buf;
}
#endif

