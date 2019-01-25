/**
 * (C) Copyright 2015, 2016 Intel Corporation.
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

#ifndef __DAOS_DEBUG_H__
#define __DAOS_DEBUG_H__

#include <stdio.h>

/**
 * predefined debug facilities (subsystems/modules), they have to be declared
 * before including any libgurt headers
 */
extern int d_addons_logfac;
extern int d_common_logfac;
extern int d_tree_logfac;
extern int d_vos_logfac;
extern int d_client_logfac;
extern int d_server_logfac;
extern int d_rdb_logfac;
extern int d_rsvc_logfac;
extern int d_pool_logfac;
extern int d_container_logfac;
extern int d_object_logfac;
extern int d_placement_logfac;
extern int d_rebuild_logfac;
extern int d_mgmt_logfac;
extern int d_bio_logfac;
extern int d_tests_logfac;
extern int d_dfs_logfac;
extern int d_duns_logfac;
extern int d_drpc_logfac;
extern int d_security_logfac;

#include <gurt/debug.h>

extern uint64_t DB_MD; /* metadata operation */
extern uint64_t DB_PL; /* placement */
extern uint64_t DB_MGMT; /* pool management */
extern uint64_t DB_EPC; /* epoch system */
extern uint64_t DB_DF; /* durable format */
extern uint64_t DB_REBUILD; /* rebuild process */
extern uint64_t DB_SEC; /* Security checks */

#define DB_DEFAULT	DLOG_DBG
#define DB_NULL		0
/** XXX Temporary things, should be replaced by debug bits above */
#define DF_DSMC		DB_ANY
#define DF_DSMS		DB_ANY
#define DF_MISC		DB_ANY

/** initialize the debug system */
int  daos_debug_init(char *logfile);
/** finalize the debug system */
void daos_debug_fini(void);

#endif /* __DAOS_DEBUG_H__ */
