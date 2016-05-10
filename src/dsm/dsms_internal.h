/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/*
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

#include <daos/list.h>
#include <daos/transport.h>

/*
 * Metadata pmem pool descriptor
 *
 * Referenced by pool metadata and container metadata descriptors.
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
int dsms_kvs_uv_update(daos_handle_t kvsh, const uuid_t uuid,
		       const void *value, size_t size);
int dsms_kvs_uv_lookup(daos_handle_t kvsh, const uuid_t uuid, void *value,
		       size_t size);
int dsms_kvs_uv_delete(daos_handle_t kvsh, const uuid_t uuid);
int dsms_kvs_uv_create_kvs(daos_handle_t kvsh, const uuid_t uuid,
			   unsigned int class, uint64_t feats,
			   unsigned int order, PMEMobjpool *mp,
			   daos_handle_t *kvsh_new);
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

/* TODO: Move these two path generators to daos_mgmt_srv.h. */

static inline void
print_vos_path(const char *path, const uuid_t pool_uuid, char *buf, size_t size)
{
	char	uuid_str[DAOS_UUID_STR_SIZE];
	int	rc;

	uuid_unparse_lower(pool_uuid, uuid_str);
	rc = snprintf(buf, size, "%s/%s-vos", path, uuid_str);
	D_ASSERT(rc < size);
}

static inline void
print_meta_path(const char *path, const uuid_t pool_uuid, char *buf,
		size_t size)
{
	char	uuid_str[DAOS_UUID_STR_SIZE];
	int	rc;

	uuid_unparse_lower(pool_uuid, uuid_str);
	rc = snprintf(buf, size, "%s/%s-meta", path, uuid_str);
	D_ASSERT(rc < size);
}

#endif /* __DSMS_INTERNAL_H__ */
