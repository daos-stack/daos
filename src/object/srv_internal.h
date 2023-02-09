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
};

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

static inline struct obj_tls *
obj_tls_get()
{
	return dss_module_key_get(dss_tls_get(), &obj_module_key);
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
void ds_obj_query_key_handler_0(crt_rpc_t *rpc);
void ds_obj_query_key_handler_1(crt_rpc_t *rpc);
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

	return dcs->dcs_type;
}

static inline struct daos_cpd_bulk *
ds_obj_cpd_get_head_bulk(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_sub_heads.ca_count <= dtx_idx)
		return NULL;

	dcs = (struct daos_cpd_sg *)oci->oci_sub_heads.ca_arrays + dtx_idx;
	if (dcs->dcs_type != DCST_BULK_HEAD)
		return NULL;

	return dcs->dcs_buf;
}

static inline struct daos_cpd_bulk *
ds_obj_cpd_get_disp_bulk(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_disp_ents.ca_count <= dtx_idx)
		return NULL;

	dcs = (struct daos_cpd_sg *)oci->oci_disp_ents.ca_arrays + dtx_idx;
	if (dcs->dcs_type != DCST_BULK_DISP)
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
	if (dcs->dcs_type != DCST_BULK_TGT)
		return NULL;

	return dcs->dcs_buf;
}

static inline struct daos_cpd_sub_head *
ds_obj_cpd_get_dcsh(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_sub_heads.ca_count <= dtx_idx)
		return NULL;

	dcs = (struct daos_cpd_sg *)oci->oci_sub_heads.ca_arrays + dtx_idx;

	if (dcs->dcs_type == DCST_BULK_HEAD)
		return &((struct daos_cpd_bulk *)dcs->dcs_buf)->dcb_head;

	/* daos_cpd_sub_head is unique for a DTX. */
	return dcs->dcs_buf;
}

static inline struct daos_cpd_sub_req *
ds_obj_cpd_get_dcsr(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_sub_reqs.ca_count <= dtx_idx)
		return NULL;

	dcs = (struct daos_cpd_sg *)oci->oci_sub_reqs.ca_arrays + dtx_idx;

	/* daos_cpd_sub_req array is shared by all tgts for a DTX. */
	return dcs->dcs_buf;
}

static inline struct daos_shard_tgt *
ds_obj_cpd_get_tgts(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_disp_tgts.ca_count <= dtx_idx)
		return NULL;

	dcs = (struct daos_cpd_sg *)oci->oci_disp_tgts.ca_arrays + dtx_idx;

	if (dcs->dcs_type == DCST_BULK_TGT)
		return ((struct daos_cpd_bulk *)dcs->dcs_buf)->dcb_iov.iov_buf;

	return dcs->dcs_buf;
}

static inline struct daos_cpd_disp_ent *
ds_obj_cpd_get_dcde(crt_rpc_t *rpc, int dtx_idx, int ent_idx)
{
	struct obj_cpd_in		*oci = crt_req_get(rpc);
	struct daos_cpd_sg		*dcs;
	struct daos_cpd_disp_ent	*dcde;

	if (oci->oci_disp_ents.ca_count <= dtx_idx)
		return NULL;

	dcs = (struct daos_cpd_sg *)oci->oci_disp_ents.ca_arrays + dtx_idx;

	if (ent_idx >= dcs->dcs_nr)
		return NULL;

	if (dcs->dcs_type == DCST_BULK_DISP)
		dcde = ((struct daos_cpd_bulk *)dcs->dcs_buf)->dcb_iov.iov_buf;
	else
		dcde = dcs->dcs_buf;

	return dcde + ent_idx;
}

static inline int
ds_obj_cpd_get_dcsr_cnt(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_sub_reqs.ca_count <= dtx_idx)
		return -DER_INVAL;

	dcs = (struct daos_cpd_sg *)oci->oci_sub_reqs.ca_arrays + dtx_idx;
	return dcs->dcs_nr;
}

static inline int
ds_obj_cpd_get_dcsh_cnt(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_sub_heads.ca_count <= dtx_idx)
		return -DER_INVAL;

	dcs = (struct daos_cpd_sg *)oci->oci_sub_heads.ca_arrays + dtx_idx;
	return dcs->dcs_nr;
}

static inline int
ds_obj_cpd_get_dcde_cnt(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_disp_ents.ca_count <= dtx_idx)
		return -DER_INVAL;

	dcs = (struct daos_cpd_sg *)oci->oci_disp_ents.ca_arrays + dtx_idx;
	return dcs->dcs_nr;
}

static inline int
ds_obj_cpd_get_tgt_cnt(crt_rpc_t *rpc, int dtx_idx)
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
int obj_ec_rw_req_split(daos_unit_oid_t oid, uint32_t start_tgt, struct obj_iod_array *iod_array,
			uint32_t iod_nr, uint32_t start_shard, uint32_t max_shard,
			uint32_t leader_id, void *tgt_map, uint32_t map_size,
			struct daos_oclass_attr *oca, uint32_t tgt_nr, struct daos_shard_tgt *tgts,
			struct obj_ec_split_req **split_req, struct obj_pool_metrics *opm);
void obj_ec_split_req_fini(struct obj_ec_split_req *req);

#endif /* __DAOS_OBJ_SRV_INTENRAL_H__ */
