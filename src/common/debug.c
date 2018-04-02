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
 */
#include <limits.h>
#include <daos/common.h>

/* default debug log file */
#define DAOS_LOG_DEFAULT	"/tmp/daos.log"

#define DAOS_DBG_MAX_LEN	(32)
#define DAOS_FAC_MAX_LEN	(128)

static int dd_ref;
static pthread_mutex_t dd_lock = PTHREAD_MUTEX_INITIALIZER;

/** DAOS debug tunables */
bool dd_tune_alloc = false;	/* disabled */

#define DECLARE_FAC(name)	int DD_FAC(name)
/** predefined log facilities */
DECLARE_FAC(null);
DECLARE_FAC(common);
DECLARE_FAC(tree);
DECLARE_FAC(vos);
DECLARE_FAC(client);
DECLARE_FAC(server);
DECLARE_FAC(rdb);
DECLARE_FAC(pool);
DECLARE_FAC(container);
DECLARE_FAC(object);
DECLARE_FAC(placement);
DECLARE_FAC(rebuild);
DECLARE_FAC(tier);
DECLARE_FAC(mgmt);
DECLARE_FAC(tests);

/** debug facility (or subsystem/module) */
struct daos_debug_fac {
	/** name of the facility */
	char		*df_name;
	/** pointer to the facility ID */
	int		*df_idp;
	/** debug bit-mask of the facility */
	uint64_t	 df_mask;
	/** facility is enabled */
	int		 df_enabled;
	size_t		 df_name_size;
};

#define DBG_DICT_ENTRY(bit, name, lname)			\
{								\
	.db_bit = bit,						\
	.db_name = name,					\
	.db_lname = lname,					\
	.db_name_size = sizeof(name),				\
	.db_lname_size = sizeof(lname),				\
}

static struct d_debug_bit daos_bit_dict[] = {
	/* load DAOS-specific debug bits into dict */
	DBG_DICT_ENTRY(DB_MD,		"md",		"metadata"),
	DBG_DICT_ENTRY(DB_PL,		"pl",		"placement"),
	DBG_DICT_ENTRY(DB_MGMT,		"mgmt",		"management"),
	DBG_DICT_ENTRY(DB_EPC,		"epc",		"epoch"),
	DBG_DICT_ENTRY(DB_DF,		"df",		"durable_format"),
	DBG_DICT_ENTRY(DB_REBUILD,	"rebuild",	"rebuild"),
};

#define NUM_DBG_BIT_ENTRIES ARRAY_SIZE(daos_bit_dict)

#define DBG_FAC_DICT_ENT(name, idp, mask, enabled)		\
{								\
	.df_name	= name,					\
	.df_idp		= idp,					\
	.df_mask	= mask,					\
	.df_enabled	= enabled,				\
	.df_name_size	= sizeof(name),				\
}

/** dictionary for all facilities */
static struct daos_debug_fac debug_fac_dict[] = {
	/* MUST be the first one */
	/* no facility name for NULL */
	DBG_FAC_DICT_ENT("null",	&d_null_logfac,		DB_NULL, 1),
	DBG_FAC_DICT_ENT("common",	&d_common_logfac,	DB_DEFAULT, 0),
	DBG_FAC_DICT_ENT("tree",	&d_tree_logfac,		DB_DEFAULT, 0),
	DBG_FAC_DICT_ENT("vos",		&d_vos_logfac,		DB_DEFAULT, 0),
	DBG_FAC_DICT_ENT("client",	&d_client_logfac,	DB_DEFAULT, 1),
	DBG_FAC_DICT_ENT("server",	&d_server_logfac,	DB_DEFAULT, 1),
	DBG_FAC_DICT_ENT("rdb",		&d_rdb_logfac,		DB_DEFAULT, 1),
	DBG_FAC_DICT_ENT("pool",	&d_pool_logfac,		DB_DEFAULT, 1),
	DBG_FAC_DICT_ENT("container",	&d_container_logfac,	DB_DEFAULT, 1),
	DBG_FAC_DICT_ENT("object",	&d_object_logfac,	DB_DEFAULT, 1),
	DBG_FAC_DICT_ENT("placement",	&d_placement_logfac,	DB_DEFAULT, 1),
	DBG_FAC_DICT_ENT("rebuild",	&d_rebuild_logfac,	DB_DEFAULT, 1),
	DBG_FAC_DICT_ENT("tier",	&d_tier_logfac,		DB_DEFAULT, 1),
	DBG_FAC_DICT_ENT("mgmt",	&d_mgmt_logfac,		DB_DEFAULT, 1),
	DBG_FAC_DICT_ENT("tests",	&d_tests_logfac,	DB_DEFAULT, 0),
};

#define NUM_DBG_FAC_ENTRIES ARRAY_SIZE(debug_fac_dict)
/** Load enabled debug facilities from the environment variable. */
static void
debug_fac_load_env(void)
{
	char	*fac_env;
	char	*fac_str;
	char	*cur;
	int	 i;

	fac_env = getenv(DD_FAC_ENV);
	if (fac_env == NULL)
		return;

	D_STRNDUP(fac_str, fac_env, DAOS_FAC_MAX_LEN);
	if (fac_str == NULL) {
		fprintf(stderr, "D_STRNDUP of fac mask failed");
		return;
	}

	/* Disable all facilities. The first one is ignored because NULL is
	 * always enabled.
	 */
	for (i = 1; i < NUM_DBG_FAC_ENTRIES; i++)
		debug_fac_dict[i].df_enabled = 0;

	cur = strtok(fac_str, DD_SEP);
	while (cur != NULL) {
		/* skip 1 because it's NULL and enabled always */
		for (i = 1; i < NUM_DBG_FAC_ENTRIES; i++) {
			if (debug_fac_dict[i].df_name != NULL &&
			    strncasecmp(cur, debug_fac_dict[i].df_name,
					debug_fac_dict[i].df_name_size)
					== 0) {
				debug_fac_dict[i].df_enabled = 1;
				break;
			} else if (strncasecmp(cur, DD_FAC_ALL,
						strlen(DD_FAC_ALL)) == 0) {
				debug_fac_dict[i].df_enabled = 1;
			}
		}
		cur = strtok(NULL, DD_SEP);
	}
	D_FREE(fac_str);
}

/** Load the debug mask from the environment variable. */
static uint64_t
debug_mask_load_env(void)
{
	char		*mask_env;
	char		*mask_str;
	char		*cur;
	int		i;
	uint64_t	dd_mask;

	mask_env = getenv(DD_MASK_ENV);
	if (mask_env == NULL)
		return 0;

	D_STRNDUP(mask_str, mask_env, DAOS_DBG_MAX_LEN);
	if (mask_str == NULL) {
		fprintf(stderr, "D_STRNDUP of debug mask failed");
		return 0;
	}

	dd_mask = 0;
	cur = strtok(mask_str, DD_SEP);
	while (cur != NULL) {
		for (i = 0; i < NUM_DBG_BIT_ENTRIES; i++) {
			if (daos_bit_dict[i].db_name != NULL &&
			    strncasecmp(cur, daos_bit_dict[i].db_name,
					daos_bit_dict[i].db_name_size)
					== 0) {
				dd_mask |= daos_bit_dict[i].db_bit;
				break;
			}
			if (daos_bit_dict[i].db_lname != NULL &&
			    strncasecmp(cur, daos_bit_dict[i].db_lname,
					daos_bit_dict[i].db_lname_size)
					== 0) {
				dd_mask |= daos_bit_dict[i].db_bit;
				break;
			}
		}
		cur = strtok(NULL, DD_SEP);
	}
	D_FREE(mask_str);
	return dd_mask;
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

	rc = d_log_allocfacility(dfac->df_name, dfac->df_name);
	if (rc < 0)
		return rc;

	*dfac->df_idp = rc;
	return 0;
}

static void
debug_fini_locked(void)
{
	d_log_fini();
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
	int		i;
	int		rc;
	uint64_t	dd_mask;

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

	dd_mask = debug_mask_load_env();
	/* load other env variables */
	debug_fac_load_env();
	debug_tunables_load_env();

	rc = d_log_init_adv("DAOS", logfile, DLOG_FLV_LOGPID,
			    DLOG_INFO, DLOG_CRIT);
	if (rc != 0) {
		fprintf(stderr, "Failed to init DAOS debug log: %d\n", rc);
		goto failed_unlock;
	}

	for (i = 0; i < NUM_DBG_FAC_ENTRIES; i++) {
		if (!debug_fac_dict[i].df_enabled) {
			/* redirect disabled facility to NULL */
			*debug_fac_dict[i].df_idp = d_null_logfac;
			continue;
		}

		rc = debug_fac_register(&debug_fac_dict[i]);
		if (rc != 0) {
			fprintf(stderr, "Failed to add DAOS facility %s: %d\n",
				debug_fac_dict[i].df_name, rc);
			goto failed_fini;
		}
	}

	d_log_sync_mask(dd_mask, false);

	dd_ref = 1;
	D_MUTEX_UNLOCK(&dd_lock);

	return 0;

failed_fini:
	debug_fini_locked();

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
