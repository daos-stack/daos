/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * ds_cont: Epoch Operations
 */
#define D_LOGFAC	DD_FAC(container)

#include <daos_srv/pool.h>
#include <daos_srv/rdb.h>
#include <daos_srv/security.h>
#include "rpc.h"
#include "srv_internal.h"
#include "srv_layout.h"

struct snap_list_iter_args {
	int		 sla_index;
	int		 sla_count;
	int		 sla_max_count;
	daos_epoch_t	*sla_buf;
};

static int
snap_list_iter_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val,
		  void *arg)
{
	struct snap_list_iter_args *i_args = arg;

	D_ASSERTF(key->iov_len == sizeof(daos_epoch_t),
		  DF_U64"\n", key->iov_len);

	if (i_args->sla_count > 0) {
		/* Check if we've reached capacity */
		if (i_args->sla_index == i_args->sla_count)  {
			/* Increase capacity exponentially */
			i_args->sla_count *= 2;
			/* If max_count < 0, there is no upper limit */
			if (i_args->sla_max_count > 0 &&
			    i_args->sla_max_count < i_args->sla_count)
				i_args->sla_count = i_args->sla_max_count;

			/* Re-allocate only if count actually increased */
			if (i_args->sla_index < i_args->sla_count) {
				void *ptr;

				D_REALLOC_ARRAY(ptr, i_args->sla_buf,
						i_args->sla_index,
						i_args->sla_count);
				if (ptr == NULL)
					return -DER_NOMEM;
				i_args->sla_buf = ptr;
			}
		}

		if (i_args->sla_index < i_args->sla_count)
			memcpy(&i_args->sla_buf[i_args->sla_index],
			       key->iov_buf, sizeof(daos_epoch_t));
	}
	++i_args->sla_index;
	return 0;
}

/* Read snapshot epochs from rdb (TODO: add names) */
static int
read_snap_list(struct rdb_tx *tx, struct cont *cont, daos_epoch_t **buf, int *count)
{
	struct snap_list_iter_args iter_args;
	int rc;

	iter_args.sla_index = 0;
	iter_args.sla_max_count = *count;
	if (*count != 0) {
		/* start with initial size then grow the buffer */
		iter_args.sla_count = *count > 0 && *count < 64 ? *count : 64;
		D_ALLOC_ARRAY(iter_args.sla_buf, iter_args.sla_count);
		if (iter_args.sla_buf == NULL)
			return -DER_NOMEM;
	} else {
		iter_args.sla_count = 0;
		iter_args.sla_buf = NULL;
	}
	rc = rdb_tx_iterate(tx, &cont->c_snaps, false /* !backward */,
			    snap_list_iter_cb, &iter_args);
	if (rc != 0) {
		D_FREE(iter_args.sla_buf);
		return rc;
	}
	*count = iter_args.sla_index;
	*buf   = iter_args.sla_buf;
	return 0;
}

int
ds_cont_epoch_aggregate(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
			struct cont *cont, struct container_hdl *hdl,
			crt_rpc_t *rpc)
{
	struct cont_epoch_op_in	*in = crt_req_get(rpc);
	daos_epoch_t		 epoch = in->cei_epoch;
	int			 rc = 0;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: epoch="DF_U64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		in->cei_epoch);

	/* Verify handle has write access */
	if (!ds_sec_cont_can_write_data(hdl->ch_sec_capas)) {
		D_ERROR(DF_CONT": permission denied to aggregate\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid));
		rc = -DER_NO_PERM;
		goto out;
	}

	if (epoch >= DAOS_EPOCH_MAX) {
		rc = -DER_INVAL;
		goto out;
	} else if (in->cei_epoch == 0) {
		epoch = crt_hlc_get();
	}

out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: epoch="DF_U64", %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		epoch, rc);
	return rc;
}

static int
snap_create_bcast(struct rdb_tx *tx, struct cont *cont, uuid_t coh_uuid,
		  uint64_t opts, crt_context_t *ctx, daos_epoch_t *epoch)
{
	struct cont_tgt_snapshot_notify_in	*in;
	struct cont_tgt_snapshot_notify_out	*out;
	crt_rpc_t				*rpc;
	char					 zero = 0;
	d_iov_t					 key;
	d_iov_t					 value;
	uint32_t				 nsnapshots;
	int					 rc;

	rc = ds_cont_bcast_create(ctx, cont->c_svc,
				  CONT_TGT_SNAPSHOT_NOTIFY, &rpc);
	if (rc != 0)
		goto out;

	in = crt_req_get(rpc);
	uuid_copy(in->tsi_pool_uuid, cont->c_svc->cs_pool_uuid);
	uuid_copy(in->tsi_cont_uuid, cont->c_uuid);
	uuid_copy(in->tsi_coh_uuid, coh_uuid);
	in->tsi_epoch = crt_hlc_get();
	in->tsi_opts = opts;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		goto out_rpc;

	out = crt_reply_get(rpc);
	rc = out->tso_rc;
	if (rc != 0) {
		D_ERROR(DF_CONT": snapshot notify failed on %d targets\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);
		rc = -DER_IO;
		goto out_rpc;
	}

	*epoch = in->tsi_epoch;
	d_iov_set(&key, epoch, sizeof(*epoch));
	d_iov_set(&value, &zero, sizeof(zero));
	rc = rdb_tx_update(tx, &cont->c_snaps, &key, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to create snapshot: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);
		goto out_rpc;
	}
	/* Update number of snapshots */
	d_iov_set(&value, &nsnapshots, sizeof(nsnapshots));
	rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_nsnapshots, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to lookup nsnapshots, "DF_RC"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
		goto out_rpc;
	}
	nsnapshots++;
	rc = rdb_tx_update(tx, &cont->c_prop, &ds_cont_prop_nsnapshots, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to update nsnapshots, "DF_RC"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
		goto out_rpc;
	}

	D_DEBUG(DF_DSMS, DF_CONT": created snapshot "DF_U64"\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), *epoch);
out_rpc:
	crt_req_decref(rpc);
out:
	return rc;
}

int
ds_cont_snap_create(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		    struct cont *cont, struct container_hdl *hdl,
		    crt_rpc_t *rpc)
{
	struct cont_epoch_op_in	       *in = crt_req_get(rpc);
	struct cont_epoch_op_out       *out = crt_reply_get(rpc);
	daos_epoch_t			snap_eph;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc);

	/* Verify handle has write access */
	if (!ds_sec_cont_can_write_data(hdl->ch_sec_capas)) {
		D_ERROR(DF_CONT": permission denied to create snapshot\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid));
		rc = -DER_NO_PERM;
		goto out;
	}

	rc = snap_create_bcast(tx, cont, in->cei_op.ci_hdl, in->cei_opts,
			       rpc->cr_ctx, &snap_eph);
	if (rc == 0)
		out->ceo_epoch = snap_eph;
out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		rc);
	return rc;
}

int
ds_cont_snap_destroy(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		    struct cont *cont, struct container_hdl *hdl,
		    crt_rpc_t *rpc)
{
	struct cont_epoch_op_in		*in = crt_req_get(rpc);
	d_iov_t				 key;
	d_iov_t				 value;
	uint32_t			 nsnapshots;
	int				 rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: epoch="DF_U64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid),
		rpc, in->cei_epoch);

	/* Verify the handle has write access */
	if (!ds_sec_cont_can_write_data(hdl->ch_sec_capas)) {
		D_ERROR(DF_CONT": permission denied to delete snapshot\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid));
		rc = -DER_NO_PERM;
		goto out;
	}

	/* Lookup snapshot, so that nsnapshots-- does not occur if snapshot does not exist */
	d_iov_set(&key, &in->cei_epoch, sizeof(daos_epoch_t));
	d_iov_set(&value, NULL, 0);
	rc = rdb_tx_lookup(tx, &cont->c_snaps, &key, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to lookup snapshot [%lu]: "DF_RC"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), in->cei_epoch, DP_RC(rc));
		goto out;
	}

	rc = rdb_tx_delete(tx, &cont->c_snaps, &key);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to delete snapshot [%lu]: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			in->cei_epoch, rc);
		goto out;
	}

	/* Update number of snapshots */
	d_iov_set(&value, &nsnapshots, sizeof(nsnapshots));
	rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_nsnapshots, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to lookup nsnapshots, "DF_RC"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
		goto out;
	}
	nsnapshots--;
	rc = rdb_tx_update(tx, &cont->c_prop, &ds_cont_prop_nsnapshots, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to update nsnapshots, "DF_RC"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
		goto out;
	}

	D_DEBUG(DF_DSMS, DF_CONT": deleted snapshot [%lu]\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid),
		in->cei_epoch);
out:
	return rc;
}

static int
bulk_cb(const struct crt_bulk_cb_info *cb_info)
{
	ABT_eventual *eventual = cb_info->bci_arg;

	ABT_eventual_set(*eventual, (void *)&cb_info->bci_rc,
			 sizeof(cb_info->bci_rc));
	return 0;
}

/* Transfer snapshots to client (TODO: add snapshot names) */
static int
xfer_snap_list(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl, struct cont *cont,
	       struct container_hdl *hdl, crt_rpc_t *rpc, crt_bulk_t *bulk, int *snap_countp)
{
	int		rc;
	daos_size_t	bulk_size;
	int		snap_count;
	daos_epoch_t   *snapshots = NULL;
	int		xfer_size;

	/*
	 * If remote bulk handle does not exist, only aggregate size is sent.
	 */
	if (bulk) {
		rc = crt_bulk_get_len(bulk, &bulk_size);
		if (rc != 0)
			goto out;
		D_DEBUG(DF_DSMS, DF_CONT": bulk_size=%lu\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid, cont->c_uuid), bulk_size);

		snap_count = (int)(bulk_size / sizeof(daos_epoch_t));
	} else {
		bulk_size = 0;
		snap_count = 0;
	}
	rc = read_snap_list(tx, cont, &snapshots, &snap_count);
	if (rc != 0)
		goto out;

	xfer_size = snap_count * sizeof(daos_epoch_t);
	xfer_size = MIN(xfer_size, bulk_size);

	D_DEBUG(DF_DSMS, DF_CONT": snap_count=%d, bulk_size=%zu, xfer_size=%d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, cont->c_uuid), snap_count, bulk_size,
		xfer_size);
	if (xfer_size > 0) {
		ABT_eventual		 eventual;
		int			*status;
		d_iov_t			 iov = {
			.iov_buf			= snapshots,
			.iov_len			= xfer_size,
			.iov_buf_len			= xfer_size
		};
		d_sg_list_t		 sgl = {
			.sg_nr_out			= 1,
			.sg_nr				= 1,
			.sg_iovs			= &iov
		};
		struct crt_bulk_desc	 bulk_desc = {
			.bd_rpc				= rpc,
			.bd_bulk_op			= CRT_BULK_PUT,
			.bd_local_off			= 0,
			.bd_remote_hdl			= bulk,
			.bd_remote_off			= 0,
			.bd_len				= xfer_size
		};

		rc = ABT_eventual_create(sizeof(*status), &eventual);
		if (rc != ABT_SUCCESS) {
			rc = dss_abterr2der(rc);
			goto out_mem;
		}

		rc = crt_bulk_create(rpc->cr_ctx, &sgl, CRT_BULK_RW, &bulk_desc.bd_local_hdl);
		if (rc != 0)
			goto out_eventual;
		rc = crt_bulk_transfer(&bulk_desc, bulk_cb, &eventual, NULL);
		if (rc != 0)
			goto out_bulk;
		rc = ABT_eventual_wait(eventual, (void **)&status);
		if (rc != ABT_SUCCESS)
			rc = dss_abterr2der(rc);
		else
			rc = *status;
		D_DEBUG(DF_DSMS, DF_CONT": done bulk transfer xfer_size=%d, rc=%d\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid, cont->c_uuid), xfer_size, rc);

out_bulk:
		crt_bulk_free(bulk_desc.bd_local_hdl);
out_eventual:
		ABT_eventual_free(&eventual);
	}

out_mem:
	if (snapshots)
		D_FREE(snapshots);
out:
	if (rc == 0)
		*snap_countp = snap_count;
	return rc;
}

int
ds_cont_snap_list(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		  struct cont *cont, struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_snap_list_in	*in		= crt_req_get(rpc);
	struct cont_snap_list_out	*out		= crt_reply_get(rpc);
	int				 snap_count;
	int				 rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->sli_op.ci_uuid),
		rpc, DP_UUID(in->sli_op.ci_hdl));

	/* Verify the handle has read access */
	if (!ds_sec_cont_can_read_data(hdl->ch_sec_capas)) {
		D_ERROR(DF_CONT": permission denied to list snapshots\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid));
		rc = -DER_NO_PERM;
		goto out;
	}

	rc = xfer_snap_list(tx, pool_hdl, cont, hdl, rpc, in->sli_bulk, &snap_count);
	if (rc)
		goto out;
	out->slo_count = snap_count;

out:
	return rc;
}

int
ds_cont_get_snapshots(uuid_t pool_uuid, uuid_t cont_uuid,
		      daos_epoch_t **snapshots, int *snap_count)
{
	struct cont_svc	*svc;
	struct rdb_tx	tx;
	struct cont	*cont = NULL;
	int		rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	rc = cont_svc_lookup_leader(pool_uuid, 0, &svc, NULL);
	if (rc != 0)
		return rc;

	rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
	if (rc != 0)
		D_GOTO(out_put, rc);

	ABT_rwlock_rdlock(svc->cs_lock);
	rc = cont_lookup(&tx, svc, cont_uuid, &cont);
	if (rc != 0)
		D_GOTO(out_lock, rc);

	rc = read_snap_list(&tx, cont, snapshots, snap_count);
	cont_put(cont);
	if (rc != 0)
		D_GOTO(out_lock, rc);

out_lock:
	ABT_rwlock_unlock(svc->cs_lock);
	rdb_tx_end(&tx);
out_put:
	cont_svc_put_leader(svc);

	D_DEBUG(DB_TRACE, DF_UUID"/"DF_UUID" get %d snaps rc %d\n",
		DP_UUID(pool_uuid), DP_UUID(cont_uuid), *snap_count, rc);

	return rc;
}

/*
 * Propagate new snapshot list to all servers through snapshot IV, errors
 * are ignored.
 */
void
ds_cont_update_snap_iv(struct cont_svc *svc, uuid_t cont_uuid)
{
	struct rdb_tx	 tx;
	struct cont	*cont = NULL;
	uint64_t	*snapshots = NULL;
	int		 snap_count = -1, rc;

	/* Only happens on xstream 0 */
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
	if (rc != 0) {
		D_ERROR(DF_UUID": Failed to start rdb tx: %d\n",
			DP_UUID(svc->cs_pool_uuid), rc);
		return;
	}

	ABT_rwlock_rdlock(svc->cs_lock);
	rc = cont_lookup(&tx, svc, cont_uuid, &cont);
	if (rc != 0) {
		D_ERROR(DF_CONT": Failed to look container: %d\n",
			DP_CONT(svc->cs_pool_uuid, cont_uuid), rc);
		goto out_lock;
	}

	rc = read_snap_list(&tx, cont, &snapshots, &snap_count);
	cont_put(cont);
	if (rc != 0) {
		D_ERROR(DF_CONT": Failed to read snap list: %d\n",
			DP_CONT(svc->cs_pool_uuid, cont_uuid), rc);
		goto out_lock;
	}

	rc = cont_iv_snapshots_update(svc->cs_pool->sp_iv_ns, cont_uuid,
				      snapshots, snap_count);
	if (rc != 0)
		D_ERROR(DF_CONT": Failed to update snapshots IV: %d\n",
			DP_CONT(svc->cs_pool_uuid, cont_uuid), rc);

	D_FREE(snapshots);
out_lock:
	ABT_rwlock_unlock(svc->cs_lock);
	rdb_tx_end(&tx);
}
