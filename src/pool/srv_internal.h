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
 * ds_pool: Pool Server Internal Declarations
 */

#ifndef __POOL_SERVER_INTERNAL_H__
#define __POOL_SERVER_INTERNAL_H__

#include <daos/list.h>
#include <daos_srv/daos_server.h>

/**
 * DSM server thread local storage structure
 */
struct dsm_tls {
	/* in-memory structures TLS instance */
	struct daos_list_head	dt_pool_list;
};

extern struct dss_module_key dsm_module_key;

static inline struct dsm_tls *
dsm_tls_get()
{
	struct dsm_tls			*tls;
	struct dss_thread_local_storage	*dtc;

	dtc = dss_tls_get();
	tls = (struct dsm_tls *)dss_module_key_get(dtc, &dsm_module_key);
	return tls;
}

/*
 * srv.c
 */
int dsms_corpc_create(dtp_context_t ctx, dtp_group_t *group,
		      dtp_opcode_t opcode, dtp_rpc_t **rpc);

/*
 * srv_storage.c
 */
int dsms_storage_init(void);
void dsms_storage_fini(void);

/*
 * srv_pool.c
 */
int dsms_module_pool_init(void);
void dsms_module_pool_fini(void);
int dsms_hdlr_pool_connect(dtp_rpc_t *rpc);
int dsms_hdlr_pool_disconnect(dtp_rpc_t *rpc);

/*
 * srv_target.c
 */
int dsms_module_target_init(void);
void dsms_module_target_fini(void);
int dsms_hdlr_tgt_pool_connect(dtp_rpc_t *rpc);
int dsms_hdlr_tgt_pool_connect_aggregate(dtp_rpc_t *source, dtp_rpc_t *result,
					 void *priv);
int dsms_hdlr_tgt_pool_disconnect(dtp_rpc_t *rpc);
int dsms_hdlr_tgt_pool_disconnect_aggregate(dtp_rpc_t *source,
					    dtp_rpc_t *result, void *priv);

#endif /* __POOL_SERVER_INTERNAL_H__ */
