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
 * dsms: Container Operations
 *
 * This file contains the server API methods and the RPC handlers that are both
 * related container metadata.
 */

#include <daos_srv/daos_m_srv.h>
#include <uuid/uuid.h>
#include <daos/transport.h>
#include "dsm_rpc.h"
#include "dsms_internal.h"
#include "dsms_layout.h"

/*
 * Container service
 *
 * References the mpool descriptor. Identified by a number unique within the
 * pool.
 *
 * TODO: Store and look up this in a hash table.
 */
struct cont_svc {
	uint64_t		cs_id;
	struct mpool	       *cs_mpool;
	pthread_rwlock_t	cs_rwlock;
	pthread_mutex_t		cs_lock;
	int			cs_ref;
	daos_handle_t		cs_containers;	/* of container index KVS */
};

static int
cont_svc_init(const uuid_t pool_uuid, int id, struct cont_svc *svc)
{
	int rc;

	svc->cs_id = id;
	svc->cs_ref = 1;

	rc = dsms_mpool_lookup(pool_uuid, &svc->cs_mpool);
	if (rc != 0)
		D_GOTO(err, rc);

	rc = pthread_rwlock_init(&svc->cs_rwlock, NULL /* attr */);
	if (rc != 0) {
		D_ERROR("failed to initialize cs_rwlock: %d\n", rc);
		D_GOTO(err_mp, rc = -DER_NOMEM);
	}

	rc = pthread_mutex_init(&svc->cs_lock, NULL /* attr */);
	if (rc != 0) {
		D_ERROR("failed to initialize cs_lock: %d\n", rc);
		D_GOTO(err_rwlock, rc = -DER_NOMEM);
	}

	rc = dsms_kvs_nv_open_kvs(svc->cs_mpool->mp_root, CONTAINERS,
				  svc->cs_mpool->mp_pmem, &svc->cs_containers);
	if (rc != 0) {
		D_ERROR("failed to open containers kvs: %d\n", rc);
		D_GOTO(err_lock, rc);
	}

	return 0;

err_lock:
	pthread_mutex_destroy(&svc->cs_lock);
err_rwlock:
	pthread_rwlock_destroy(&svc->cs_rwlock);
err_mp:
	dsms_mpool_put(svc->cs_mpool);
err:
	return rc;
}

static int
cont_svc_lookup(const uuid_t pool_uuid, int id, struct cont_svc **svc)
{
	struct cont_svc	       *p;
	int			rc;

	/* TODO: Hash table. */

	D_ALLOC_PTR(p);
	if (p == NULL) {
		D_ERROR("failed to allocate container service descriptor\n");
		return -DER_NOMEM;
	}

	rc = cont_svc_init(pool_uuid, id, p);
	if (rc != 0) {
		D_FREE_PTR(p);
		return rc;
	}

	*svc = p;
	return 0;
}

static void
cont_svc_put(struct cont_svc *svc)
{
	dbtree_close(svc->cs_containers);
	pthread_mutex_destroy(&svc->cs_lock);
	pthread_rwlock_destroy(&svc->cs_rwlock);
	dsms_mpool_put(svc->cs_mpool);
	D_FREE_PTR(svc);
}

int
dsms_hdlr_cont_create(dtp_rpc_t *rpc)
{
	struct cont_create_in  *in = dtp_req_get(rpc);
	struct cont_create_out *out = dtp_reply_get(rpc);
	struct cont_svc	       *svc;
	volatile daos_handle_t	ch = DAOS_HDL_INVAL;
	int			rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);
	D_ASSERT(in != NULL);
	D_ASSERT(out != NULL);

	D_DEBUG(DF_DSMS, "enter: pool="DF_UUID" pool_hdl="DF_UUID" cont="DF_UUID
		"\n", DP_UUID(in->cci_pool), DP_UUID(in->cci_pool_hdl),
		DP_UUID(in->cci_cont));

	/* TODO: Pool handle verfiication. */

	/*
	 * TODO: How to map to the correct container service among those
	 * running of this storage node? (Currently, there is only one, with ID
	 * 0, colocated with the pool service.)
	 */
	rc = cont_svc_lookup(in->cci_pool, 0 /* id */, &svc);
	if (rc != 0)
		D_GOTO(out, rc);

	pthread_rwlock_wrlock(&svc->cs_rwlock);

	TX_BEGIN(svc->cs_mpool->mp_pmem) {
		daos_handle_t h;

		/* Create the container KVS under the container index KVS. */
		rc = dsms_kvs_uv_create_kvs(svc->cs_containers, in->cci_cont,
					    KVS_NV, 0 /* feats */,
					    16 /* order */,
					    svc->cs_mpool->mp_pmem, &h);
		if (rc != 0) {
			D_ERROR("failed to create container kvs: %d\n", rc);
			pmemobj_tx_abort(rc);
		}

		ch = h;

		rc = dsms_kvs_nv_create_kvs(ch, CONT_HCES, KVS_EC,
					    0 /* feats */, 16 /* order */,
					    svc->cs_mpool->mp_pmem,
					    NULL /* kvsh_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_create_kvs(ch, CONT_LRES, KVS_EC,
					    0 /* feats */, 16 /* order */,
					    svc->cs_mpool->mp_pmem,
					    NULL /* kvsh_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_create_kvs(ch, CONT_LHES, KVS_EC,
					    0 /* feats */, 16 /* order */,
					    svc->cs_mpool->mp_pmem,
					    NULL /* kvsh_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_create_kvs(ch, CONT_SNAPSHOTS, KVS_EC,
					    0 /* feats */, 16 /* order */,
					    svc->cs_mpool->mp_pmem,
					    NULL /* kvsh_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_create_kvs(ch, CONT_HANDLES, KVS_UV,
					    0 /* feats */, 16 /* order */,
					    svc->cs_mpool->mp_pmem,
					    NULL /* kvsh_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);
	} TX_FINALLY {
		if (!daos_handle_is_inval(ch))
			dbtree_close(ch);
	} TX_END

	rc = pmemobj_tx_errno();
	if (rc != 0) {
		/* May be a system error number from libpmemobj. */
		if (rc < DER_ERR_BASE)
			rc = -DER_NOSPACE;
	}

	pthread_rwlock_unlock(&svc->cs_rwlock);
	cont_svc_put(svc);
out:
	D_DEBUG(DF_DSMS, "leave: rc=%d\n", rc);
	out->cco_ret = rc;
	return dtp_reply_send(rpc);
}

int
dsms_hdlr_cont_destroy(dtp_rpc_t *rpc)
{
	struct cont_destroy_in	       *in = dtp_req_get(rpc);
	struct cont_destroy_out	       *out = dtp_reply_get(rpc);
	struct cont_svc		       *svc;
	volatile daos_handle_t		ch;
	daos_handle_t			h;
	int				rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);
	D_ASSERT(in != NULL);
	D_ASSERT(out != NULL);

	D_DEBUG(DF_DSMS, "enter: pool="DF_UUID" pool_hdl="DF_UUID" cont="DF_UUID
		" force=%u\n", DP_UUID(in->cdi_pool), DP_UUID(in->cdi_pool_hdl),
		DP_UUID(in->cdi_cont), in->cdi_force);

	/* TODO: Pool handle verfiication. */

	rc = cont_svc_lookup(in->cdi_pool, 0 /* id */, &svc);
	if (rc != 0)
		D_GOTO(out, rc);

	pthread_rwlock_wrlock(&svc->cs_rwlock);

	rc = dsms_kvs_uv_open_kvs(svc->cs_containers, in->cdi_cont,
				  svc->cs_mpool->mp_pmem, &h);
	if (rc != 0)
		D_GOTO(out_rwlock, rc);

	ch = h;

	TX_BEGIN(svc->cs_mpool->mp_pmem) {
		rc = dsms_kvs_nv_destroy_kvs(ch, CONT_HANDLES,
					     svc->cs_mpool->mp_pmem);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_destroy_kvs(ch, CONT_SNAPSHOTS,
					     svc->cs_mpool->mp_pmem);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_destroy_kvs(ch, CONT_LHES,
					     svc->cs_mpool->mp_pmem);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_destroy_kvs(ch, CONT_LRES,
					     svc->cs_mpool->mp_pmem);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_destroy_kvs(ch, CONT_HCES,
					     svc->cs_mpool->mp_pmem);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_destroy(ch);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		ch = DAOS_HDL_INVAL;

		rc = dsms_kvs_uv_delete(svc->cs_containers, in->cdi_cont);
		if (rc != 0) {
			D_ERROR("failed to delete container kvs: %d\n", rc);
			pmemobj_tx_abort(rc);
		}
	} TX_ONABORT {
		if (!daos_handle_is_inval(ch))
			dbtree_close(ch);
	} TX_END

	rc = pmemobj_tx_errno();
	if (rc != 0) {
		/* May be a system error number from libpmemobj. */
		if (rc < DER_ERR_BASE)
			rc = -DER_NOSPACE;
	}

out_rwlock:
	pthread_rwlock_unlock(&svc->cs_rwlock);
	cont_svc_put(svc);
out:
	D_DEBUG(DF_DSMS, "leave: rc=%d\n", rc);
	out->cdo_ret = rc;
	return dtp_reply_send(rpc);
}

