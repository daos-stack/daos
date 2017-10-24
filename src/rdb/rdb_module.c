/**
 * (C) Copyright 2017 Intel Corporation.
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
 * rdb: Server Module Interface
 */

#define DDSUBSYS DDFAC(rdb)

#include <daos_srv/rdb.h>

#include <daos_srv/daos_server.h>
#include "rdb_internal.h"

static int
rdb_module_init(void)
{
	return rdb_hash_init();
}

static int
rdb_module_fini(void)
{
	rdb_hash_fini();
	return 0;
}

static struct daos_rpc_handler rdb_handlers[] = {
	{
		.dr_opc		= RDB_REQUESTVOTE,
		.dr_hdlr	= rdb_requestvote_handler
	}, {
		.dr_opc		= RDB_APPENDENTRIES,
		.dr_hdlr	= rdb_appendentries_handler
	}, {
		.dr_opc		= RDB_START,
		.dr_hdlr	= rdb_start_handler,
		.dr_corpc_ops	= {
			.co_aggregate	= rdb_start_aggregator
		}
	}, {
		.dr_opc		= RDB_STOP,
		.dr_hdlr	= rdb_stop_handler,
		.dr_corpc_ops	= {
			.co_aggregate	= rdb_stop_aggregator
		}
	}, {
	}
};

struct dss_module rdb_module = {
	.sm_name	= "rdb",
	.sm_mod_id	= DAOS_RDB_MODULE,
	.sm_ver		= 1,
	.sm_init	= rdb_module_init,
	.sm_fini	= rdb_module_fini,
	.sm_cl_rpcs	= NULL,
	.sm_srv_rpcs	= rdb_srv_rpcs,
	.sm_handlers	= rdb_handlers,
	.sm_key		= NULL
};
