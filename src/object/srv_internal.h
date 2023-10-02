/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos_sr
 *
 * src/object/srv_internal.h
 */
#ifndef __DAOS_OBJ_SRV_INTENRAL_H__
#define __DAOS_OBJ_SRV_INTENRAL_H__

#include <abt.h>
#include <daos/dtx.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/dtx_srv.h>
#include <daos_srv/object.h>
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_producer.h>

#include "obj_internal.h"
#include "obj_rpc.h"
#include "obj_ec.h"

extern struct dss_module_key obj_module_key;

/* Per pool attached to the migrate tls(per xstream) */
struct migrate_pool_tls {
	/* POOL UUID and pool to be migrated */
	uuid_t			mpt_pool_uuid;
	struct ds_pool_child	*mpt_pool;
	unsigned int		mpt_version;
	unsigned int		mpt_generation;

	/* Link to the migrate_pool_tls list */
	d_list_t		mpt_list;

	/* Pool/Container handle UUID to be migrated, the migrate
	 * should provide the pool/handle uuid
	 */
	uuid_t			mpt_poh_uuid;
	uuid_t			mpt_coh_uuid;
	daos_handle_t		mpt_pool_hdl;

	/* Container/objects to be migrated will be attached to the tree */
	daos_handle_t		mpt_root_hdl;
	struct btr_root		mpt_root;

	/* Container/objects already migrated will be attached to the tree, to
	 * avoid the object being migrated multiple times.
	 */
	daos_handle_t		mpt_migrated_root_hdl;
	struct btr_root		mpt_migrated_root;

	/* Service rank list for migrate fetch RPC */
	d_rank_list_t		mpt_svc_list;

	ABT_eventual		mpt_done_eventual;
	/* Migrate status */
	uint64_t		mpt_obj_count;
	uint64_t		mpt_rec_count;
	uint64_t		mpt_size;
	int			mpt_status;

	/* Max epoch for the migration, used for migrate fetch RPC */
	uint64_t		mpt_max_eph;

	/* The ULT number generated on the xstream */
	uint64_t		mpt_generated_ult;

	/* The ULT number executed on the xstream */
	uint64_t		mpt_executed_ult;

	/* The ULT number generated for object on the xstream */
	uint64_t		mpt_obj_generated_ult;

	/* The ULT number executed on the xstream */
	uint64_t		mpt_obj_executed_ult;

	/* reference count for the structure */
	uint64_t		mpt_refcount;

	/* The current in-flight iod, mainly used for controlling
	 * rebuild in-flight rate to avoid the DMA buffer overflow.
	 */
	uint64_t		mpt_inflight_size;
	uint64_t		mpt_inflight_max_size;
	ABT_cond		mpt_inflight_cond;
	ABT_mutex		mpt_inflight_mutex;
	int			mpt_inflight_max_ult;
	uint32_t		mpt_opc;

	ABT_cond		mpt_init_cond;
	ABT_mutex		mpt_init_mutex;

	/* The new layout version for upgrade job */
	uint32_t		mpt_new_layout_ver;

	/* migrate leader ULT */
	unsigned int		mpt_ult_running:1,
				mpt_init_tls:1,
				mpt_fini:1;
};

void
migrate_pool_tls_destroy(struct migrate_pool_tls *tls);

/*
 * Report latency on a per-I/O size.
 * Buckets starts at [0; 256B[ and are increased by power of 2
 * (i.e. [256B; 512B[, [512B; 1KB[) up to [4MB; infinity[
 * Since 4MB = 2^22 and 256B = 2^8, this means
 * (22 - 8 + 1) = 15 buckets plus the 4MB+ bucket, so
 * 16 buckets in total.
 */
#define NR_LATENCY_BUCKETS 16

struct obj_pool_metrics {
	/** Count number of total per-opcode requests (type = counter) */
	struct d_tm_node_t	*opm_total[OBJ_PROTO_CLI_COUNT];
	/** Total number of bytes fetched (type = counter) */
	struct d_tm_node_t	*opm_fetch_bytes;
	/** Total number of bytes updated (type = counter) */
	struct d_tm_node_t	*opm_update_bytes;

	/** Total number of silently restarted updates (type = counter) */
	struct d_tm_node_t	*opm_update_restart;
	/** Total number of resent update operations (type = counter) */
	struct d_tm_node_t	*opm_update_resent;
	/** Total number of retry update operations (type = counter) */
	struct d_tm_node_t	*opm_update_retry;
	/** Total number of EC full-stripe update operations (type = counter) */
	struct d_tm_node_t	*opm_update_ec_full;
	/** Total number of EC partial update operations (type = counter) */
	struct d_tm_node_t	*opm_update_ec_partial;
};

struct obj_tls {
	d_sg_list_t		ot_echo_sgl;
	d_list_t		ot_pool_list;

	/** Measure per-operation latency in us (type = gauge) */
	struct d_tm_node_t	*ot_op_lat[OBJ_PROTO_CLI_COUNT];
	/** Count number of per-opcode active requests (type = gauge) */
	struct d_tm_node_t	*ot_op_active[OBJ_PROTO_CLI_COUNT];

	/** Measure update/fetch latency based on I/O size (type = gauge) */
	struct d_tm_node_t	*ot_update_lat[NR_LATENCY_BUCKETS];
	struct d_tm_node_t	*ot_fetch_lat[NR_LATENCY_BUCKETS];

	struct d_tm_node_t	*ot_tgt_update_lat[NR_LATENCY_BUCKETS];

	struct d_tm_node_t	*ot_update_bulk_lat[NR_LATENCY_BUCKETS];
	struct d_tm_node_t	*ot_fetch_bulk_lat[NR_LATENCY_BUCKETS];

	struct d_tm_node_t	*ot_update_vos_lat[NR_LATENCY_BUCKETS];
	struct d_tm_node_t	*ot_fetch_vos_lat[NR_LATENCY_BUCKETS];

	struct d_tm_node_t	*ot_update_bio_lat[NR_LATENCY_BUCKETS];
	struct d_tm_node_t	*ot_fetch_bio_lat[NR_LATENCY_BUCKETS];
};

static inline struct obj_tls *
obj_tls_get()
{
	return dss_module_key_get(dss_tls_get(), &obj_module_key);
}

static inline unsigned int
lat_bucket(uint64_t size)
{
	int nr;

	if (size <= 256)
		return 0;

	/** return number of leading zero-bits */
	nr =  __builtin_clzl(size - 1);

	/** >4MB, return last bucket */
	if (nr < 42)
		return NR_LATENCY_BUCKETS - 1;

	return 56 - nr;
}

enum latency_type {
	BULK_LATENCY,
	BIO_LATENCY,
	VOS_LATENCY,
};

static inline void
obj_update_latency(uint32_t opc, uint32_t type, uint64_t latency, uint64_t io_size)
{
	struct obj_tls		*tls = obj_tls_get();
	struct d_tm_node_t	*lat;

	latency >>= 10; /* convert to micro seconds */

	if (opc == DAOS_OBJ_RPC_FETCH) {
		switch (type) {
		case BULK_LATENCY:
			lat = tls->ot_fetch_bulk_lat[lat_bucket(io_size)];
			break;
		case BIO_LATENCY:
			lat = tls->ot_fetch_bio_lat[lat_bucket(io_size)];
			break;
		case VOS_LATENCY:
			lat = tls->ot_fetch_vos_lat[lat_bucket(io_size)];
			break;
		default:
			D_ASSERT(0);
		}
	} else if (opc == DAOS_OBJ_RPC_UPDATE || opc == DAOS_OBJ_RPC_TGT_UPDATE) {
		switch (type) {
		case BULK_LATENCY:
			lat = tls->ot_update_bulk_lat[lat_bucket(io_size)];
			break;
		case BIO_LATENCY:
			lat = tls->ot_update_bio_lat[lat_bucket(io_size)];
			break;
		case VOS_LATENCY:
			lat = tls->ot_update_vos_lat[lat_bucket(io_size)];
			break;
		default:
			D_ASSERT(0);
		}
	} else {
		/* Ignore other ops for the moment */
		return;
	}
	d_tm_set_gauge(lat, latency);
}

struct ds_obj_exec_arg {
	crt_rpc_t		*rpc;
	struct obj_io_context	*ioc;
	void			*args;
	uint32_t		 flags;
	uint32_t		 start; /* The start shard for EC obj. */
};

int
ds_obj_remote_update(struct dtx_leader_handle *dth, void *arg, int idx,
		     dtx_sub_comp_cb_t comp_cb);
int
ds_obj_remote_punch(struct dtx_leader_handle *dth, void *arg, int idx,
		    dtx_sub_comp_cb_t comp_cb);
int
ds_obj_cpd_dispatch(struct dtx_leader_handle *dth, void *arg, int idx,
		    dtx_sub_comp_cb_t comp_cb);

/* srv_obj.c */
void ds_obj_rw_handler(crt_rpc_t *rpc);
void ds_obj_tgt_update_handler(crt_rpc_t *rpc);
void ds_obj_enum_handler(crt_rpc_t *rpc);
void ds_obj_key2anchor_handler(crt_rpc_t *rpc);
void ds_obj_punch_handler(crt_rpc_t *rpc);
void ds_obj_tgt_punch_handler(crt_rpc_t *rpc);
void ds_obj_query_key_handler(crt_rpc_t *rpc);
void ds_obj_sync_handler(crt_rpc_t *rpc);
void ds_obj_migrate_handler(crt_rpc_t *rpc);
void ds_obj_ec_agg_handler(crt_rpc_t *rpc);
void ds_obj_ec_rep_handler(crt_rpc_t *rpc);
void ds_obj_cpd_handler(crt_rpc_t *rpc);
typedef int (*ds_iofw_cb_t)(crt_rpc_t *req, void *arg);

struct daos_cpd_args {
	struct obj_io_context	*dca_ioc;
	crt_rpc_t		*dca_rpc;
	ABT_future		 dca_future;
	uint32_t		 dca_idx;
};

static inline uint32_t
ds_obj_cpd_get_head_type(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_sub_heads.ca_count <= dtx_idx)
		return DCST_UNKNOWN;

	dcs = (struct daos_cpd_sg *)oci->oci_sub_heads.ca_arrays + dtx_idx;

	return dcs->dcs_type_base;
}

static inline uint32_t
ds_obj_cpd_get_reqs_type(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_sub_reqs.ca_count <= dtx_idx)
		return DCST_UNKNOWN;

	dcs = (struct daos_cpd_sg *)oci->oci_sub_reqs.ca_arrays + dtx_idx;

	return dcs->dcs_type_base;
}

static inline uint32_t
ds_obj_cpd_get_ents_type(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_disp_ents.ca_count <= dtx_idx)
		return DCST_UNKNOWN;

	dcs = (struct daos_cpd_sg *)oci->oci_disp_ents.ca_arrays + dtx_idx;

	return dcs->dcs_type_base;
}

static inline struct daos_cpd_bulk *
ds_obj_cpd_get_head_bulk(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_sub_heads.ca_count <= dtx_idx)
		return NULL;

	dcs = (struct daos_cpd_sg *)oci->oci_sub_heads.ca_arrays + dtx_idx;
	if (dcs->dcs_type_base != DCST_BULK_HEAD)
		return NULL;

	return dcs->dcs_buf;
}

static inline struct daos_cpd_bulk *
ds_obj_cpd_get_reqs_bulk(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_sub_reqs.ca_count <= dtx_idx)
		return NULL;

	dcs = (struct daos_cpd_sg *)oci->oci_sub_reqs.ca_arrays + dtx_idx;
	if (dcs->dcs_type_base != DCST_BULK_REQ)
		return NULL;

	return dcs->dcs_buf;
}

static inline struct daos_cpd_bulk *
ds_obj_cpd_get_ents_bulk(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_disp_ents.ca_count <= dtx_idx)
		return NULL;

	dcs = (struct daos_cpd_sg *)oci->oci_disp_ents.ca_arrays + dtx_idx;
	if (dcs->dcs_type_base != DCST_BULK_ENT)
		return NULL;

	return dcs->dcs_buf;
}

static inline struct daos_cpd_bulk *
ds_obj_cpd_get_tgts_bulk(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_disp_tgts.ca_count <= dtx_idx)
		return NULL;

	dcs = (struct daos_cpd_sg *)oci->oci_disp_tgts.ca_arrays + dtx_idx;
	if (dcs->dcs_type_base != DCST_BULK_TGT)
		return NULL;

	return dcs->dcs_buf;
}

static inline struct daos_cpd_sub_head *
ds_obj_cpd_get_head(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_sub_heads.ca_count <= dtx_idx)
		return NULL;

	dcs = (struct daos_cpd_sg *)oci->oci_sub_heads.ca_arrays + dtx_idx;

	if (dcs->dcs_type_base == DCST_BULK_HEAD)
		return &((struct daos_cpd_bulk *)dcs->dcs_buf)->dcb_head;

	/* daos_cpd_sub_head is unique for a DTX. */
	return dcs->dcs_buf;
}

static inline struct daos_cpd_sub_req *
ds_obj_cpd_get_reqs(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_sub_reqs.ca_count <= dtx_idx)
		return NULL;

	dcs = (struct daos_cpd_sg *)oci->oci_sub_reqs.ca_arrays + dtx_idx;

	if (dcs->dcs_type_base == DCST_BULK_REQ)
		return ((struct daos_cpd_bulk *)dcs->dcs_buf)->dcb_reqs;

	/* daos_cpd_sub_req array is shared by all tgts for a DTX. */
	return dcs->dcs_buf;
}

static inline struct daos_cpd_disp_ent *
ds_obj_cpd_get_ents(crt_rpc_t *rpc, int dtx_idx, int ent_idx)
{
	struct obj_cpd_in		*oci = crt_req_get(rpc);
	struct daos_cpd_sg		*dcs;
	struct daos_cpd_disp_ent	*dcde;

	if (oci->oci_disp_ents.ca_count <= dtx_idx)
		return NULL;

	dcs = (struct daos_cpd_sg *)oci->oci_disp_ents.ca_arrays + dtx_idx;

	if (dcs->dcs_type_base == DCST_BULK_ENT) {
		if (ent_idx < 0)
			ent_idx = dcs->dcs_dcde_idx;
		dcde = ((struct daos_cpd_bulk *)dcs->dcs_buf)->dcb_iov.iov_buf;
	} else {
		if (ent_idx < 0)
			ent_idx = 0;
		dcde = dcs->dcs_buf;
	}

	return dcde + ent_idx;
}

static inline struct daos_shard_tgt *
ds_obj_cpd_get_tgts(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_disp_tgts.ca_count <= dtx_idx)
		return NULL;

	dcs = (struct daos_cpd_sg *)oci->oci_disp_tgts.ca_arrays + dtx_idx;

	if (dcs->dcs_type_base == DCST_BULK_TGT)
		return ((struct daos_cpd_bulk *)dcs->dcs_buf)->dcb_iov.iov_buf;

	return dcs->dcs_buf;
}

static inline int
ds_obj_cpd_get_head_cnt(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_sub_heads.ca_count <= dtx_idx)
		return -DER_INVAL;

	dcs = (struct daos_cpd_sg *)oci->oci_sub_heads.ca_arrays + dtx_idx;
	return dcs->dcs_nr;
}

static inline int
ds_obj_cpd_get_reqs_cnt(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_sub_reqs.ca_count <= dtx_idx)
		return -DER_INVAL;

	dcs = (struct daos_cpd_sg *)oci->oci_sub_reqs.ca_arrays + dtx_idx;
	return dcs->dcs_nr;
}

static inline int
ds_obj_cpd_get_ents_cnt(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_disp_ents.ca_count <= dtx_idx)
		return -DER_INVAL;

	dcs = (struct daos_cpd_sg *)oci->oci_disp_ents.ca_arrays + dtx_idx;
	return dcs->dcs_nr;
}

static inline int
ds_obj_cpd_get_tgts_cnt(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_disp_tgts.ca_count <= dtx_idx)
		return -DER_INVAL;

	dcs = (struct daos_cpd_sg *)oci->oci_disp_tgts.ca_arrays + dtx_idx;
	return dcs->dcs_nr;
}

static inline bool
obj_dtx_need_refresh(struct dtx_handle *dth, int rc)
{
	return rc == -DER_INPROGRESS && dth->dth_share_tbd_count > 0;
}

/* obj_enum.c */
int
fill_oid(daos_unit_oid_t oid, struct ds_obj_enum_arg *arg);

/* srv_ec.c */
struct obj_rw_in;
void obj_ec_metrics_process(struct obj_iod_array *iod_array, struct obj_io_context *ioc);

#endif /* __DAOS_OBJ_SRV_INTENRAL_H__ */
