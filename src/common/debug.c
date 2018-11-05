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

#define DECLARE_FAC(name)	int DD_FAC(name)
/** predefined log facilities */
DECLARE_FAC(addons);
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
DECLARE_FAC(mgmt);
DECLARE_FAC(bio);
DECLARE_FAC(tests);
DECLARE_FAC(dfs);

uint64_t DB_MD; /* metadata operation */
uint64_t DB_PL; /* placement */
uint64_t DB_MGMT; /* pool management */
uint64_t DB_EPC; /* epoch system */
uint64_t DB_DF; /* durable format */
uint64_t DB_REBUILD; /* rebuild process */
/* debug bit groups */
#define DB_GRP1 (DB_IO | DB_MD | DB_PL | DB_REBUILD)

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
	DBG_DICT_ENTRY(&DB_MD,		"md",		"metadata"),
	DBG_DICT_ENTRY(&DB_PL,		"pl",		"placement"),
	DBG_DICT_ENTRY(&DB_MGMT,	"mgmt",		"management"),
	DBG_DICT_ENTRY(&DB_EPC,		"epc",		"epoch"),
	DBG_DICT_ENTRY(&DB_DF,		"df",		"durable_format"),
	DBG_DICT_ENTRY(&DB_REBUILD,	"rebuild",	"rebuild"),
};

#define NUM_DBG_BIT_ENTRIES	ARRAY_SIZE(daos_bit_dict)

#define DAOS_INIT_LOG_FAC(name, idp)			\
	d_init_log_facility(idp, name, name);

#define FOREACH_DAOS_LOG_FAC(ACTION)			\
	ACTION("addons", d_addons_logfac)		\
	ACTION("common", d_common_logfac)		\
	ACTION("tree", d_tree_logfac)			\
	ACTION("vos", d_vos_logfac)			\
	ACTION("client", d_client_logfac)		\
	ACTION("server", d_server_logfac)		\
	ACTION("rdb", d_rdb_logfac)			\
	ACTION("pool", d_pool_logfac)			\
	ACTION("container", d_container_logfac)		\
	ACTION("object", d_object_logfac)		\
	ACTION("placement", d_placement_logfac)		\
	ACTION("rebuild", d_rebuild_logfac)		\
	ACTION("mgmt", d_mgmt_logfac)			\
	ACTION("tests", d_tests_logfac)			\
	ACTION("bio", d_bio_logfac)			\
	ACTION("dfs", d_dfs_logfac)

#define DAOS_SETUP_FAC(name, idp)			\
	DAOS_INIT_LOG_FAC(name, &idp)

static void
debug_fini_locked(void)
{
	int	i;
	int	rc;

	/* Unregister DAOS debug bits */
	for (i = 0; i < NUM_DBG_BIT_ENTRIES; i++) {
		rc = d_log_dbg_bit_dealloc(daos_bit_dict[i].db_name);
		if (rc < 0)
			D_PRINT_ERR("Error deallocating daos debug bit for %s",
				    daos_bit_dict[i].db_name);
	}

	/* Unregister DAOS debug bit groups */
	rc = d_log_dbg_grp_dealloc("daos_default");
	if (rc < 0)
		D_PRINT_ERR("Error deallocating daos debug group\n");

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
	uint64_t	allocd_dbg_bit;

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
		D_PRINT_ERR("Failed to init DAOS debug log: %d\n", rc);
		goto failed_unlock;
	}

	FOREACH_DAOS_LOG_FAC(DAOS_SETUP_FAC)

	/* Register DAOS debug bits with gurt used with DD_MASK env */
	for (i = 0; i < NUM_DBG_BIT_ENTRIES; i++) {
		/* register DAOS debug bit masks */
		rc = d_log_dbg_bit_alloc(&allocd_dbg_bit,
					 daos_bit_dict[i].db_name,
					 daos_bit_dict[i].db_lname);
		if (rc < 0) {
			D_PRINT_ERR("Error allocating daos debug bit for %s\n",
				    daos_bit_dict[i].db_name);
			return -DER_UNINIT;
		}

		*daos_bit_dict[i].db_bit = allocd_dbg_bit;
	}

	/* Register DAOS debug bit groups */
	rc = d_log_dbg_grp_alloc(DB_GRP1, "daos_default");
	if (rc < 0) {
		D_PRINT_ERR("Error allocating daos debug group\n");
		return -DER_UNINIT;
	}

	/* Sync DAOS debug env with libgurt */
	d_log_sync_mask();

	dd_ref = 1;
	D_MUTEX_UNLOCK(&dd_lock);

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
