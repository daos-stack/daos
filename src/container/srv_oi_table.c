/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Enumerate all object IDs and store them as KVs in a special object
 */
#define D_LOGFAC	DD_FAC(container)

#include <daos_srv/pool.h>
#include <daos/btree_class.h>
#include <daos/pool_map.h>
#include <daos/rpc.h>
#include <daos/object.h>
#include <daos/container.h>
#include <daos/pool.h>
#include <daos_srv/container.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/vos.h>
#include <daos_srv/dtx_srv.h>
#include "srv_internal.h"

#define OID_SEND_MAX	128

/* NB: simplify client implementation, all OIDs are stored under one dkey,
 * this should be changed in the future, e.g. bump it to 1024 dkeys so OIDs
 * can scatter to more targets.
 */
#define OIT_BUCKET_MAX	1

/* All OIDs within a bucket are stored under the same dkey of OIT */
struct oit_bucket {
	daos_obj_id_t		*ob_oids;
	int			 ob_nr;
};

/* Input & output parameter for VOS object iterator */
struct oit_scan_args {
	daos_handle_t		oa_poh;
	daos_handle_t		oa_coh;
	daos_handle_t		oa_oh;
	/* dkey is just the bucket ID */
	daos_key_t		oa_dkey;
	daos_epoch_t		oa_epoch;
	daos_obj_id_t		oa_oit_id;
	daos_obj_id_t		oa_pre_id;
	d_iov_t			oa_iov;
	int			oa_hash;
	/** sgl for OID each bucket */
	d_sg_list_t		oa_sgls[OID_SEND_MAX];
	/** IOD for OID each bucket */
	daos_iod_t		oa_iods[OID_SEND_MAX];
	/** OID buckets, OIDs are hashed into different buckets */
	struct oit_bucket	oa_buckets[OIT_BUCKET_MAX];
};

static int
cont_send_oit_bucket(struct oit_scan_args *oa, uint32_t bucket_id)
{
	struct oit_bucket *bucket = &oa->oa_buckets[bucket_id];
	int		   i;
	int		   rc;

	D_ASSERT(bucket->ob_nr <= OID_SEND_MAX);
	d_iov_set(&oa->oa_dkey, &bucket_id, sizeof(bucket_id));
	d_iov_set(&oa->oa_iov, &oa->oa_epoch, sizeof(oa->oa_epoch));

	for (i = 0; i < bucket->ob_nr; i++) {
		daos_iod_t	*iod = &oa->oa_iods[i];
		d_sg_list_t	*sgl = &oa->oa_sgls[i];

		d_iov_set(&iod->iod_name, &bucket->ob_oids[i],
			  sizeof(bucket->ob_oids[i]));
		iod->iod_type	= DAOS_IOD_SINGLE;
		iod->iod_size	= sizeof(oa->oa_epoch);
		iod->iod_nr	= 1;
		sgl->sg_iovs	= &oa->oa_iov;
		sgl->sg_nr	= 1;
	}

	/* XXX: we really should use the same epoch as snapshot, otherwise
	 * the same object ID (from different targets) can be overwritten
	 * for many times in different epochs and consume way more space.
	 */
	D_DEBUG(DB_IO, "Store %d OIDs\n", bucket->ob_nr);
	rc = dsc_obj_update(oa->oa_oh, 0, &oa->oa_dkey, bucket->ob_nr,
			    oa->oa_iods, oa->oa_sgls);
	if (rc)
		goto failed;

	return 0;
failed:
	return rc;
}

static int
cont_iter_obj_cb(daos_handle_t ch, vos_iter_entry_t *ent, vos_iter_type_t type,
		 vos_iter_param_t *param, void *data, unsigned *acts)
{
	struct oit_scan_args	*oa = data;
	struct oit_bucket	*bucket;
	daos_obj_id_t		 oid;
	unsigned int		 bid;
	int			 rc;

	oid = ent->ie_oid.id_pub;
	if (daos_oid_is_oit(oid))
		return 0; /* ignore IOT object */

	/* There might be some objects, which has same oid.id_pub, but different
	 * id_shard, so let's compare with the previous oid to avoid duplicate
	 * oid. Because these same oid will be put together in OI table, so only
	 * check the previous OID should be safe here.
	 */
	if (daos_oid_cmp(oa->oa_pre_id, oid) == 0) {
		D_DEBUG(DB_TRACE, "skip duplicate OID="DF_UOID"\n", DP_UOID(ent->ie_oid));
		return 0;
	}
	oa->oa_pre_id = oid;

	D_DEBUG(DB_TRACE, "enumerate OID="DF_OID"\n", DP_OID(oid));

	bid = d_hash_murmur64((unsigned char *)&oid, sizeof(oid), 0) %
			      OIT_BUCKET_MAX;
	bucket = &oa->oa_buckets[bid];
	if (bucket->ob_nr < OID_SEND_MAX) {
		bucket->ob_oids[bucket->ob_nr] = oid;
		bucket->ob_nr++;
		return 0;
	}

	/* bucket is full, store it now */
	D_DEBUG(DB_TRACE, "Bucket is full, send OIDs\n");
	rc = cont_send_oit_bucket(oa, bid);
	if (rc == 0) { /* store the current OID for the next send */
		bucket->ob_oids[0] = oid;
		bucket->ob_nr = 1;
	}
	*acts |= VOS_ITER_CB_YIELD;
	return rc;
}

int
cont_child_gather_oids(struct ds_cont_child *coc, uuid_t coh_uuid,
		       daos_epoch_t epoch)
{
	struct ds_pool_child	*poc = coc->sc_pool;
	struct oit_scan_args	*oa;
	struct oit_bucket	*bucket;
	d_rank_list_t		*svc = NULL;
	struct vos_iter_anchors	 anchors = {0};
	vos_iter_param_t	 ip;
	uuid_t			 uuid;
	int			 i;
	int			 rc;

	D_ALLOC_PTR(oa); /* NB: too large to be stack */
	if (!oa)
		return -DER_NOMEM;

	for (i = 0; i < OIT_BUCKET_MAX; i++) {
		daos_obj_id_t *oids;

		D_ALLOC_ARRAY(oids, OID_SEND_MAX);
		if (oids == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		oa->oa_buckets[i].ob_oids = oids;
	}

	rc = ds_pool_iv_svc_fetch(poc->spc_pool, &svc);
	if (rc)
		D_GOTO(out, rc);

	oa->oa_oit_id = daos_oit_gen_id(epoch, coc->sc_props.dcp_redun_fac);
	D_DEBUG(DB_IO, "OIT="DF_OID"\n", DP_OID(oa->oa_oit_id));

	uuid_generate(uuid);
	rc = dsc_pool_open(poc->spc_uuid, uuid, 0, NULL,
			   poc->spc_pool->sp_map, svc, &oa->oa_poh);
	if (rc)
		D_GOTO(out, rc);

	/* use the same container open handle of snapshot creation
	 * for OIT write.
	 */
	rc = dsc_cont_open(oa->oa_poh, coc->sc_uuid, coh_uuid, 0, &oa->oa_coh);
	if (rc)
		D_GOTO(out, rc);

	rc = dsc_obj_open(oa->oa_coh, oa->oa_oit_id, DAOS_OO_RW, &oa->oa_oh);
	if (rc)
		D_GOTO(out, rc);

	memset(&ip, 0, sizeof(ip));
	ip.ip_epr.epr_lo = epoch;
	ip.ip_epr.epr_hi = epoch;
	ip.ip_flags	 = VOS_IT_FOR_MIGRATION;	/* XXX */
	ip.ip_hdl	 = coc->sc_hdl;

	rc = vos_iterate(&ip, VOS_ITER_OBJ, false, &anchors,
			 cont_iter_obj_cb, NULL, oa, NULL);
	if (rc)
		D_GOTO(out, rc);

	/* send out remaining OIDs */
	for (i = 0; i < OIT_BUCKET_MAX; i++) {
		if (oa->oa_buckets[i].ob_nr > 0) {
			rc = cont_send_oit_bucket(oa, i);
			oa->oa_buckets[i].ob_nr = 0;
		}
	}
out:
	if (daos_handle_is_valid(oa->oa_oh))
		dsc_obj_close(oa->oa_oh);
	if (daos_handle_is_valid(oa->oa_coh))
		dsc_cont_close(oa->oa_poh, oa->oa_coh);
	if (daos_handle_is_valid(oa->oa_poh))
		dsc_pool_close(oa->oa_poh);
	if (svc)
		d_rank_list_free(svc);

	for (i = 0; i < OIT_BUCKET_MAX; i++) {
		bucket = &oa->oa_buckets[i];
		if (bucket->ob_oids)
			D_FREE(bucket->ob_oids);
	}
	D_FREE(oa);
	return rc;
}
