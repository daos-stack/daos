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
 * dsms: Internal Declarations
 *
 * This file contains all declarations that are only used by dsms but do not
 * belong to the more specific headers like dsms_layout.h. All external
 * variables and functions must have a "dsms_" prefix, however, even if they
 * are only used by dsms.
 */

#ifndef __DSMS_INTERNAL_H__
#define __DSMS_INTERNAL_H__

#include <libpmemobj.h>
#include <pthread.h>
#include <uuid/uuid.h>

#include <daos/btree.h>
#include <daos/list.h>
#include <daos/transport.h>
#include <daos_srv/daos_mgmt_srv.h>

/*
 * Metadata pmem pool descriptor
 *
 * Referenced by pool and container index descriptors.
 */
struct mpool {
	daos_list_t	mp_entry;
	uuid_t		mp_uuid;	/* of the DAOS pool */
	pthread_mutex_t	mp_lock;
	int		mp_ref;
	PMEMobjpool    *mp_pmem;
	daos_handle_t	mp_root;	/* root KVS */
};

/*
 * dsms_storage.c
 */
int dsms_storage_init(void);
void dsms_storage_fini(void);
int dsms_kvs_nv_update(daos_handle_t kvsh, const char *name, const void *value,
		       size_t size);
int dsms_kvs_nv_lookup(daos_handle_t kvsh, const char *name, void *value,
		       size_t size);
int dsms_kvs_nv_lookup_ptr(daos_handle_t kvsh, const char *name, void **value,
			   size_t *size);
int dsms_kvs_nv_delete(daos_handle_t kvsh, const char *name);
int dsms_kvs_nv_create_kvs(daos_handle_t kvsh, const char *name,
			   unsigned int class, uint64_t feats,
			   unsigned int order, PMEMobjpool *mp,
			   daos_handle_t *kvsh_new);
int dsms_kvs_nv_open_kvs(daos_handle_t kvsh, const char *name, PMEMobjpool *mp,
			 daos_handle_t *kvsh_child);
int dsms_kvs_nv_destroy_kvs(daos_handle_t kvsh, const char *name,
			    PMEMobjpool *mp);
int dsms_kvs_nv_destroy(daos_handle_t kvsh, const char *name, PMEMobjpool *mp);
int dsms_kvs_uv_update(daos_handle_t kvsh, const uuid_t uuid,
		       const void *value, size_t size);
int dsms_kvs_uv_lookup(daos_handle_t kvsh, const uuid_t uuid, void *value,
		       size_t size);
int dsms_kvs_uv_delete(daos_handle_t kvsh, const uuid_t uuid);
int dsms_kvs_uv_create_kvs(daos_handle_t kvsh, const uuid_t uuid,
			   unsigned int class, uint64_t feats,
			   unsigned int order, PMEMobjpool *mp,
			   daos_handle_t *kvsh_new);
int dsms_kvs_uv_open_kvs(daos_handle_t kvsh, const uuid_t uuid, PMEMobjpool *mp,
			 daos_handle_t *kvsh_child);
int dsms_kvs_uv_destroy_kvs(daos_handle_t kvsh, const uuid_t uuid,
			    PMEMobjpool *mp);
int dsms_kvs_uv_destroy(daos_handle_t kvsh, const uuid_t uuid, PMEMobjpool *mp);
int dsms_kvs_ec_update(daos_handle_t kvsh, uint64_t epoch,
		       const uint64_t *count);
int dsms_kvs_ec_lookup(daos_handle_t kvsh, uint64_t epoch, uint64_t *count);
int dsms_kvs_ec_fetch(daos_handle_t kvsh, dbtree_probe_opc_t opc,
		      const uint64_t *epoch_in, uint64_t *epoch_out,
		      uint64_t *count);
int dsms_mpool_lookup(const uuid_t pool_uuid, struct mpool **mpool);
void dsms_mpool_get(struct mpool *mpool);
void dsms_mpool_put(struct mpool *mpool);

/*
 * dsms_pool.c
 */
int dsms_pool_init(void);
void dsms_pool_fini(void);
int dsms_hdlr_pool_connect(dtp_rpc_t *rpc);
int dsms_hdlr_pool_disconnect(dtp_rpc_t *rpc);

/*
 * dsms_container.c
 */
int dsms_hdlr_cont_create(dtp_rpc_t *rpc);
int dsms_hdlr_cont_destroy(dtp_rpc_t *rpc);
int dsms_hdlr_cont_open(dtp_rpc_t *rpc);
int dsms_hdlr_cont_close(dtp_rpc_t *rpc);
int dsms_hdlr_cont_op(dtp_rpc_t *rpc);

/*
 * dsms_object.c
 */
int dsms_hdlr_object_rw(dtp_rpc_t *rpc);
int dsms_hdlr_object_enumerate(dtp_rpc_t *rpc);

int
dsms_object_init();
void
dsms_object_fini();

#endif /* __DSMS_INTERNAL_H__ */
