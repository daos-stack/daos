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
 * This file is part of daos
 *
 * common/debug.c
 *
 * NB: Do NOT use D_DEBUG in this file.
 */
#include <limits.h>
#include <daos/common.h>

struct daos_debug_data {
	pthread_mutex_t		 dd_lock;
	unsigned int		 dd_ref;
	/** debug bitmask, e.g. DB_IO | DB_MD... */
	uint64_t		 dd_mask;
	/** priority level that should be output to stderr */
	uint64_t		 dd_prio_err;
	char			 dd_logfile[PATH_MAX];
};

static struct daos_debug_data	debug_data = {
	.dd_lock		= PTHREAD_MUTEX_INITIALIZER,
	.dd_ref			= 0,
	/* 0 means we should use the mask provided by facilities */
	.dd_mask		= 0,
	/* output critical or higher priority message to stderr */
	.dd_prio_err		= DP_CRIT,
	.dd_logfile		= DD_LOG_DEFAULT,
};

/** DAOS debug tunables */
bool dd_tune_alloc = false;	/* disabled */

/** predefined log facilities */
unsigned int dd_fac_null;
unsigned int dd_fac_misc;
unsigned int dd_fac_common;
unsigned int dd_fac_tree;
unsigned int dd_fac_vos;
unsigned int dd_fac_client;
unsigned int dd_fac_server;
unsigned int dd_fac_pool;
unsigned int dd_fac_container;
unsigned int dd_fac_object;
unsigned int dd_fac_placement;
unsigned int dd_fac_rebuild;
unsigned int dd_fac_tier;
unsigned int dd_fac_mgmt;
unsigned int dd_fac_utils;
unsigned int dd_fac_tests;

/** debug facility (or subsystem/module) */
struct daos_debug_fac {
	/** name of the facility */
	char		*df_name;
	/** pointer to the facility ID */
	unsigned int	*df_idp;
	/** debug bit-mask of the facility */
	uint64_t	 df_mask;
	/** facility is enabled */
	int		 df_enabled;
};

/** dictionary for all facilities */
static struct daos_debug_fac debug_fac_dict[] = {
	{ /* MUST be the first one */
		.df_name	= "",	/* no facility name for NULL */
		.df_idp		= &dd_fac_null,
		.df_mask	= DB_NULL,
		.df_enabled	= 1,
	},
	{
		.df_name	= "misc",
		.df_idp		= &dd_fac_misc,
		.df_mask	= DB_DEFAULT,
		.df_enabled	= 1,
	},
	{
		.df_name	= "common",
		.df_idp		= &dd_fac_common,
		.df_mask	= DB_DEFAULT,
		.df_enabled	= 0, /* disabled by default */
	},
	{
		.df_name	= "tree",
		.df_idp		= &dd_fac_tree,
		.df_mask	= DB_DEFAULT,
		.df_enabled	= 0, /* disabled by default */
	},
	{
		.df_name	= "vos",
		.df_idp		= &dd_fac_vos,
		.df_mask	= DB_DEFAULT,
		.df_enabled	= 1,
	},
	{
		.df_name	= "client",
		.df_idp		= &dd_fac_client,
		.df_mask	= DB_DEFAULT,
		.df_enabled	= 1,
	},
	{
		.df_name	= "server",
		.df_idp		= &dd_fac_server,
		.df_mask	= DB_DEFAULT,
		.df_enabled	= 1,
	},
	{
		.df_name	= "pool",
		.df_idp		= &dd_fac_pool,
		.df_mask	= DB_DEFAULT,
		.df_enabled	= 1,
	},
	{
		.df_name	= "container",
		.df_idp		= &dd_fac_container,
		.df_mask	= DB_DEFAULT,
		.df_enabled	= 1,
	},
	{
		.df_name	= "object",
		.df_idp		= &dd_fac_object,
		.df_mask	= DB_DEFAULT,
		.df_enabled	= 1,
	},
	{
		.df_name	= "placement",
		.df_idp		= &dd_fac_placement,
		.df_mask	= DB_DEFAULT,
		.df_enabled	= 1,
	},
	{
		.df_name	= "rebuild",
		.df_idp		= &dd_fac_rebuild,
		.df_mask	= DB_DEFAULT,
		.df_enabled	= 1,
	},
	{
		.df_name	= "tier",
		.df_idp		= &dd_fac_tier,
		.df_mask	= DB_DEFAULT,
		.df_enabled	= 1,
	},
	{
		.df_name	= "mgmt",
		.df_idp		= &dd_fac_mgmt,
		.df_mask	= DB_DEFAULT,
		.df_enabled	= 1,
	},
	{
		.df_name	= "utils",
		.df_idp		= &dd_fac_utils,
		.df_mask	= DB_DEFAULT,
		.df_enabled	= 0, /* disabled by default */
	},
	{
		.df_name	= "tests",
		.df_idp		= &dd_fac_tests,
		.df_mask	= DB_DEFAULT,
		.df_enabled	= 0, /* disabled by default */
	},
	{
		.df_name	= NULL,
	},
};

/**
 * Priority level for debug message.
 * It is only used by D_INFO, D_NOTE, D_WARN, D_ERROR, D_CRIT and D_FATAL.
 * - All priority debug messages are always stored in the debug log.
 * - User can decide the priority level to output to stderr by setting
 *   env variable DD_STDERR, the default level is D_CRIT.
 */
struct daos_debug_priority {
	char		*dd_name;
	char		*dd_lname;	/**< long name */
	uint64_t	 dd_prio;
};

static struct daos_debug_priority debug_prio_dict[] = {
	{
		.dd_name	= "info",
		.dd_lname	= NULL,
		.dd_prio	= DP_INFO,
	},
	{
		.dd_name	= "note",
		.dd_lname	= NULL,
		.dd_prio	= DP_NOTE,
	},
	{
		.dd_name	= "warn",
		.dd_lname	= "warning",
		.dd_prio	= DP_WARN,
	},
	{
		.dd_name	= "err",
		.dd_lname	= "error",
		.dd_prio	= DP_ERR,
	},
	{
		.dd_name	= "crit",
		.dd_name	= "critical",
		.dd_prio	= DP_CRIT,
	},
	{
		.dd_name	= "fatal",
		.dd_lname	= NULL,
		.dd_prio	= DP_FATAL,
	},
	{
		.dd_name	= NULL,
	}
};

/**
 * Predefined bits for the debug mask, each bit can represent a functionality
 * of the system, e.g. DB_MEM, DB_IO, DB_MD, DB_PL...
 */
struct daos_debug_bit {
	uint64_t		 db_bit;
	char			*db_name;
	char			*db_lname;
};

static struct daos_debug_bit debug_bit_dict[] = {
	{
		.db_bit		= DB_ANY,
		.db_name	= "any",
	},
	{
		.db_bit		= DB_MEM,
		.db_name	= "mem",
		.db_lname	= "memory",
	},
	{
		.db_bit		= DB_NET,
		.db_name	= "net",
		.db_lname	= "network",
	},
	{
		.db_bit		= DB_IO,
		.db_name	= "io",
	},
	{
		.db_bit		= DB_MD,
		.db_name	= "md",
		.db_lname	= "metadata",
	},
	{
		.db_bit		= DB_PL,
		.db_name	= "pl",
		.db_lname	= "placement",
	},
	{
		.db_bit		= DB_MGMT,
		.db_name	= "mgmt",
		.db_lname	= "management",
	},
	{
		.db_bit		= DB_EPC,
		.db_name	= "epc",
		.db_lname	= "epoch",
	},
	{
		.db_bit		= DB_TRACE,
		.db_name	= "trace",
	},
	{
		.db_name	= NULL,
	},
};

/**
 * Load priority error from environment variable
 * A Priority error will be output to stderr by the debug system.
 */
static void
debug_prio_err_load_env(void)
{
	struct daos_debug_priority *dict;
	char			   *env;

	env = getenv(DD_STDERR_ENV);
	if (env == NULL)
		return;

	for (dict = &debug_prio_dict[0]; dict->dd_name != NULL; dict++) {
		if (strcasecmp(env, dict->dd_name) == 0)
			break;

		if (dict->dd_lname != NULL &&
		    strcasecmp(env, dict->dd_lname) == 0)
			break;
	}

	if (dict->dd_name != NULL) /* found */
		debug_data.dd_prio_err = dict->dd_prio;
}

/** Load the debug mask from the environment variable. */
static void
debug_mask_load_env(void)
{
	char	*mask_env;
	char	*mask_str;
	char	*cur;
	char	*tmp;
	int	 i;

	mask_env = getenv(DD_MASK_ENV);
	if (mask_env == NULL)
		return;

	mask_str = strdup(mask_env);
	if (mask_str == NULL)
		return;

	debug_data.dd_mask = 0;
	/* enable those bits provided by the env variable */
	for (cur = tmp = mask_str; cur != NULL && *cur != '\0'; cur = tmp) {
		tmp = strchr(cur, DD_SEP);
		if (tmp != NULL) {
			for (; *tmp == DD_SEP; tmp++)
				*tmp = 0;
		}

		cur = daos_str_trimwhite(cur);
		if (cur == NULL)
			continue;

		for (i = 0; debug_bit_dict[i].db_name; i++) {
			if (strcasecmp(cur, debug_bit_dict[i].db_name) == 0) {
				debug_data.dd_mask |= debug_bit_dict[i].db_bit;
				break;
			}
		}
	}
	free(mask_str);
}

/** Load enabled debug facilities from the environment variable. */
static void
debug_fac_load_env(void)
{
	char	*fac_env;
	char	*fac_str;
	char	*cur;
	char	*tmp;
	int	 i;

	fac_env = getenv(DD_FAC_ENV);
	if (fac_env == NULL)
		return;

	fac_str = strdup(fac_env);
	if (fac_str == NULL)
		return;

	/* Disable all facilities. The first one is ignored because NULL is
	 * always enabled.
	 */
	for (i = 1; debug_fac_dict[i].df_name; i++)
		debug_fac_dict[i].df_enabled = 0;

	/* then enable those facilities provided by the env variable */
	for (cur = tmp = fac_str; cur != NULL && *cur != '\0'; cur = tmp) {
		tmp = strchr(cur, DD_SEP);
		if (tmp != NULL) {
			for (; *tmp == DD_SEP; tmp++)
				*tmp = 0;
		}

		cur = daos_str_trimwhite(cur);
		if (cur == NULL)
			continue;

		/* skip 1 because it's NULL and enabled always */
		for (i = 1; debug_fac_dict[i].df_name; i++) {
			if (strcasecmp(cur, debug_fac_dict[i].df_name) == 0) {
				debug_fac_dict[i].df_enabled = 1;
				break;
			}
		}
	}
	free(fac_str);
}

/** loading misc debug tunables */
static void
debug_tunables_load_env(void)
{
	char	*tune_alloc;

	tune_alloc = getenv(DD_TUNE_ALLOC);
	if (tune_alloc == NULL)
		return;

	dd_tune_alloc = !!atoi(tune_alloc);
}

static int
debug_fac_register(struct daos_debug_fac *dfac)
{
	int	rc;

	rc = crt_log_allocfacility(dfac->df_name, dfac->df_name);
	if (rc < 0)
		return rc;

	*dfac->df_idp = rc;
	return 0;
}

static void
debug_fini_locked(void)
{
	crt_log_fini();
}

void
daos_debug_fini(void)
{
	pthread_mutex_lock(&debug_data.dd_lock);
	debug_data.dd_ref--;
	if (debug_data.dd_ref == 0)
		debug_fini_locked();
	pthread_mutex_unlock(&debug_data.dd_lock);
}

/** Initialize debug system */
int
daos_debug_init(char *logfile)
{
	int	i;
	int	rc;

	pthread_mutex_lock(&debug_data.dd_lock);
	if (debug_data.dd_ref > 0) {
		debug_data.dd_ref++;
		pthread_mutex_unlock(&debug_data.dd_lock);
		return 0;
	}

	if (getenv(DD_LOG_ENV)) /* honor the env variable first */
		logfile = getenv(DD_LOG_ENV);
	else if (logfile == NULL)
		logfile = DD_LOG_DEFAULT;

	strncpy(debug_data.dd_logfile, logfile, sizeof(debug_data.dd_logfile));

	/* load other env variables */
	debug_prio_err_load_env();
	debug_mask_load_env();
	debug_fac_load_env();
	debug_tunables_load_env();

	rc = crt_log_init_adv("DAOS", debug_data.dd_logfile, CLOG_FLV_LOGPID,
			      DP_INFO, debug_data.dd_prio_err);
	if (rc != 0) {
		fprintf(stderr, "Failed to initialize debug log: %d\n", rc);
		goto failed_unlock;
	}

	for (i = 0; debug_fac_dict[i].df_name != NULL; i++) {
		unsigned int	mask;

		if (!debug_fac_dict[i].df_enabled) {
			/* redirect disabled facility to NULL */
			*debug_fac_dict[i].df_idp = dd_fac_null;
			continue;
		}

		rc = debug_fac_register(&debug_fac_dict[i]);
		if (rc != 0) {
			fprintf(stderr, "Failed to add facility %s: %d\n",
				debug_fac_dict[i].df_name, rc);
			goto failed_fini;
		}

		mask = debug_data.dd_mask != 0 ?
		       debug_data.dd_mask : debug_fac_dict[i].df_mask;

		crt_log_setlogmask(*debug_fac_dict[i].df_idp, mask);
	}
	debug_data.dd_ref = 1;
	pthread_mutex_unlock(&debug_data.dd_lock);

	return 0;

failed_fini:
	debug_fini_locked();

failed_unlock:
	pthread_mutex_unlock(&debug_data.dd_lock);
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
