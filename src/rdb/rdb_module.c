/**
 * (C) Copyright 2017-2018 Intel Corporation.
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

#define D_LOGFAC	DD_FAC(rdb)

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
/* Define for cont_rpcs[] array population below.
 * See RDB_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.dr_opc       = a,	\
	.dr_hdlr      = d,	\
	.dr_corpc_ops = e,	\
}

static struct daos_rpc_handler rdb_handlers[] = {
	RDB_PROTO_SRV_RPC_LIST,
};

#undef X

struct dss_module rdb_module = {
	.sm_name	= "rdb",
	.sm_mod_id	= DAOS_RDB_MODULE,
	.sm_ver		= DAOS_RDB_VERSION,
	.sm_init	= rdb_module_init,
	.sm_fini	= rdb_module_fini,
	.sm_proto_fmt	= &rdb_proto_fmt,
	.sm_cli_count	= 0,
	.sm_handlers	= rdb_handlers,
	.sm_key		= NULL
};
