/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dsr: RPC Protocol Definitions
 *
 * This is naturally shared by both dsrc and dsrs. The in and out data
 * structures may safely contain compiler-generated paddings, which will be
 * removed crt's serialization process.
 *
 */

#ifndef __DAOS_OBJ_RPC_H__
#define __DAOS_OBJ_RPC_H__

#include <stdint.h>
#include <uuid/uuid.h>
#include <daos/checksum.h>
#include <daos/dtx.h>
#include <daos/event.h>
#include <daos/object.h>
#include <daos/rpc.h>

#include "obj_ec.h"

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See daos_rpc.h.
 */
#define DAOS_OBJ_VERSION 8
/* LIST of internal RPCS in form of:
 * OPCODE, flags, FMT, handler, corpc_hdlr and name
 */

#define OBJ_PROTO_CLI_RPC_LIST						\
	X(DAOS_OBJ_RPC_UPDATE,						\
		0, &CQF_obj_rw,						\
		ds_obj_rw_handler, NULL, "update")			\
	X(DAOS_OBJ_RPC_FETCH,						\
		0, &CQF_obj_rw,						\
		ds_obj_rw_handler, NULL, "fetch")			\
	X(DAOS_OBJ_DKEY_RPC_ENUMERATE,					\
		0, &CQF_obj_key_enum,					\
		ds_obj_enum_handler, NULL, "dkey_enum")			\
	X(DAOS_OBJ_AKEY_RPC_ENUMERATE,					\
		0, &CQF_obj_key_enum,					\
		ds_obj_enum_handler, NULL, "akey_enum")			\
	X(DAOS_OBJ_RECX_RPC_ENUMERATE,					\
		0, &CQF_obj_key_enum,					\
		ds_obj_enum_handler, NULL, "recx_enum")			\
	X(DAOS_OBJ_RPC_ENUMERATE,					\
		0, &CQF_obj_key_enum,					\
		ds_obj_enum_handler, NULL, "obj_enum")			\
	X(DAOS_OBJ_RPC_PUNCH,						\
		0, &CQF_obj_punch,					\
		ds_obj_punch_handler, NULL, "obj_punch")		\
	X(DAOS_OBJ_RPC_PUNCH_DKEYS,					\
		0, &CQF_obj_punch,					\
		ds_obj_punch_handler, NULL, "dkey_punch")		\
	X(DAOS_OBJ_RPC_PUNCH_AKEYS,					\
		0, &CQF_obj_punch,					\
		ds_obj_punch_handler, NULL, "akey_punch")		\
	X(DAOS_OBJ_RPC_QUERY_KEY,					\
		0, &CQF_obj_query_key,					\
		ds_obj_query_key_handler, NULL, "key_query")		\
	X(DAOS_OBJ_RPC_SYNC,						\
		0, &CQF_obj_sync,					\
		ds_obj_sync_handler, NULL, "obj_sync")			\
	X(DAOS_OBJ_RPC_TGT_UPDATE,					\
		0, &CQF_obj_rw,						\
		ds_obj_tgt_update_handler, NULL, "tgt_update")		\
	X(DAOS_OBJ_RPC_TGT_PUNCH,					\
		0, &CQF_obj_punch,					\
		ds_obj_tgt_punch_handler, NULL, "tgt_punch")		\
	X(DAOS_OBJ_RPC_TGT_PUNCH_DKEYS,					\
		0, &CQF_obj_punch,					\
		ds_obj_tgt_punch_handler, NULL, "tgt_dkey_punch")	\
	X(DAOS_OBJ_RPC_TGT_PUNCH_AKEYS,					\
		0, &CQF_obj_punch,					\
		ds_obj_tgt_punch_handler, NULL, "tgt_akey_punch")	\
	X(DAOS_OBJ_RPC_MIGRATE,						\
		0, &CQF_obj_migrate,					\
		ds_obj_migrate_handler, NULL, "migrate")		\
	X(DAOS_OBJ_RPC_EC_AGGREGATE,					\
		0, &CQF_obj_ec_agg,					\
		ds_obj_ec_agg_handler, NULL, "ec_agg")			\
	X(DAOS_OBJ_RPC_EC_REPLICATE,					\
		0, &CQF_obj_ec_rep,					\
		ds_obj_ec_rep_handler, NULL, "ec_rep")			\
	X(DAOS_OBJ_RPC_CPD,						\
		0, &CQF_obj_cpd,					\
		ds_obj_cpd_handler, NULL, "compound")			\
	X(DAOS_OBJ_RPC_KEY2ANCHOR,					\
		0, &CQF_obj_key2anchor,					\
		ds_obj_key2anchor_handler, NULL, "key2anchor")

/* Define for RPC enum population below */
#define X(a, b, c, d, e, f) a,
enum obj_rpc_opc {
	OBJ_PROTO_CLI_RPC_LIST
	OBJ_PROTO_CLI_COUNT,
	OBJ_PROTO_CLI_LAST = OBJ_PROTO_CLI_COUNT - 1,
};
#undef X

extern struct crt_proto_format obj_proto_fmt;
extern int dc_obj_proto_version;

/* Helper function to convert opc to name */
static inline char *
obj_opc_to_str(crt_opcode_t opc)
{
	switch (opc) {
#define X(a, b, c, d, e, f) case a: return f;
		OBJ_PROTO_CLI_RPC_LIST
#undef X
	}
	return "unknown";
}

enum obj_rpc_flags {
	ORF_BULK_BIND		= (1 << 0),
	/** It is a resent RPC. */
	ORF_RESEND		= (1 << 1),
	/** Commit DTX synchronously. */
	ORF_DTX_SYNC		= (1 << 2),
	/** Reports prior fetch CSUM mismatch */
	ORF_CSUM_REPORT		= (1 << 3),
	/**
	 * Erasure coding flag, to avoid server recheck from oca,
	 * now only used for single value EC handling.
	 */
	ORF_EC			= (1 << 4),
	/** Include the map on fetch (daos_iom_t) */
	ORF_CREATE_MAP		= (1 << 5),
	/** The epoch (e.g., orw_epoch for OBJ_RW) is uncertain. */
	ORF_EPOCH_UNCERTAIN	= (1 << 6),
	/** Erasure coding degraded fetch flag */
	ORF_EC_DEGRADED		= (1 << 7),
	/**
	 * ENUM without an epoch range. oei_epr.epr_lo is epoch_first;
	 * oei_epr.epr_hi is epoch.
	 */
	ORF_ENUM_WITHOUT_EPR	= (1 << 8),
	/* CPD RPC leader */
	ORF_CPD_LEADER		= (1 << 9),
	/* Bulk data transfer for CPD RPC. */
	ORF_CPD_BULK		= (1 << 10),
	/* Contain EC split req, only used on CPD leader locally. Obsolete - DAOS-10348. */
	ORF_HAS_EC_SPLIT	= (1 << 11),
	/* Checking the existence of the object/key. */
	ORF_CHECK_EXISTENCE	= (1 << 12),
	/** Include the map details on fetch (daos_iom_t::iom_recxs) */
	ORF_CREATE_MAP_DETAIL	= (1 << 13),
	/* For data migration. */
	ORF_FOR_MIGRATION	= (1 << 14),
	/* Force DTX refresh if hit non-committed DTX on non-leader. Out-of-date DAOS-7878. */
	ORF_DTX_REFRESH		= (1 << 15),
	/* for EC aggregate (to bypass read perm check related with RF) */
	ORF_FOR_EC_AGG		= (1 << 16),
	/* for EC data recovery */
	ORF_EC_RECOV		= (1 << 17),
	/* EC data recovery from snapshot */
	ORF_EC_RECOV_SNAP	= (1 << 18),
	/* EC data recovery from parity */
	ORF_EC_RECOV_FROM_PARITY = (1 << 19),
	/* The forward targets array contains leader information. */
	ORF_CONTAIN_LEADER	= (1 << 20),
	/*Ascending Order - 0, descending order - 1*/
	ORF_DESCENDING_ORDER	= (1 << 21),
	/* The IO include reintegrating target */
	ORF_REINTEGRATING_IO	= (1 << 22),
	/* The IO include rebuilding target */
	ORF_REBUILDING_IO	= (1 << 23),
	/* 'sgls' is NULL, for update sub-request of CPD RPC. */
	ORF_EMPTY_SGL		= (1 << 24),
};

/* common for update/fetch */
#define DAOS_ISEQ_OBJ_RW	/* input fields */		 \
	((struct dtx_id)	(orw_dti)		CRT_RAW) \
	((daos_unit_oid_t)	(orw_oid)		CRT_RAW) \
	((uuid_t)		(orw_pool_uuid)		CRT_VAR) \
	((uuid_t)		(orw_co_hdl)		CRT_VAR) \
	((uuid_t)		(orw_co_uuid)		CRT_VAR) \
	((uint64_t)		(orw_epoch)		CRT_VAR) \
	((uint64_t)		(orw_epoch_first)	CRT_VAR) \
	((uint64_t)		(orw_api_flags)		CRT_VAR) \
	((uint64_t)		(orw_dkey_hash)		CRT_VAR) \
	((uint32_t)		(orw_map_ver)		CRT_VAR) \
	((uint32_t)		(orw_nr)		CRT_VAR) \
	((uint32_t)		(orw_start_shard)	CRT_VAR) \
	((uint32_t)		(orw_flags)		CRT_VAR) \
	((daos_key_t)		(orw_dkey)		CRT_VAR) \
	((struct dcs_csum_info)	(orw_dkey_csum)		CRT_PTR) \
	((struct obj_iod_array)	(orw_iod_array)		CRT_VAR) \
	((struct dtx_id)	(orw_dti_cos)		CRT_ARRAY) \
	((d_sg_list_t)		(orw_sgls)		CRT_ARRAY) \
	((crt_bulk_t)		(orw_bulks)		CRT_ARRAY) \
	((struct daos_shard_tgt)(orw_shard_tgts)	CRT_ARRAY) \
	/* orw_tgt_idx and orw_tgt_max only for EC obj */	   \
	((uint32_t)		(orw_tgt_idx)		CRT_VAR)   \
	((uint32_t)		(orw_tgt_max)		CRT_VAR)

#define DAOS_OSEQ_OBJ_RW	/* output fields */		 \
	((int32_t)		(orw_ret)		CRT_VAR) \
	((uint32_t)		(orw_map_version)	CRT_VAR) \
	((uint64_t)		(orw_epoch)		CRT_VAR) \
	((daos_size_t)		(orw_iod_sizes)		CRT_ARRAY) \
	((daos_size_t)		(orw_data_sizes)	CRT_ARRAY) \
	((d_sg_list_t)		(orw_sgls)		CRT_ARRAY) \
	((uint32_t)		(orw_nrs)		CRT_ARRAY) \
	((struct dcs_iod_csums)	(orw_iod_csums)		CRT_ARRAY) \
	((struct daos_recx_ep_list)	(orw_rels)	CRT_ARRAY) \
	((daos_iom_t)		(orw_maps)		CRT_ARRAY)

CRT_RPC_DECLARE(obj_rw,		DAOS_ISEQ_OBJ_RW, DAOS_OSEQ_OBJ_RW)

/* object Enumerate in/out */
#define DAOS_ISEQ_OBJ_KEY_ENUM	/* input fields */		 \
	((struct dtx_id)	(oei_dti)		CRT_RAW) \
	((daos_unit_oid_t)	(oei_oid)		CRT_RAW) \
	((uuid_t)		(oei_pool_uuid)		CRT_VAR) \
	((uuid_t)		(oei_co_hdl)		CRT_VAR) \
	((uuid_t)		(oei_co_uuid)		CRT_VAR) \
	((daos_epoch_range_t)	(oei_epr)		CRT_VAR) \
	((uint32_t)		(oei_map_ver)		CRT_VAR) \
	((uint32_t)		(oei_nr)		CRT_VAR) \
	((uint32_t)		(oei_rec_type)		CRT_VAR) \
	((uint32_t)		(oei_flags)		CRT_VAR) \
	((daos_key_t)		(oei_dkey)		CRT_VAR) \
	((daos_key_t)		(oei_akey)		CRT_VAR) \
	((daos_anchor_t)	(oei_anchor)		CRT_RAW) \
	((daos_anchor_t)	(oei_dkey_anchor)	CRT_RAW) \
	((daos_anchor_t)	(oei_akey_anchor)	CRT_RAW) \
	((d_sg_list_t)		(oei_sgl)		CRT_VAR) \
	((crt_bulk_t)		(oei_bulk)		CRT_VAR) \
	((crt_bulk_t)		(oei_kds_bulk)		CRT_VAR)

#define DAOS_OSEQ_OBJ_KEY_ENUM	/* output fields */		 \
	((int32_t)		(oeo_ret)		CRT_VAR) \
	((uint32_t)		(oeo_map_version)	CRT_VAR) \
	((uint64_t)		(oeo_epoch)		CRT_VAR) \
	((uint32_t)		(oeo_num)		CRT_VAR) \
	((uint32_t)		(oeo_padding)		CRT_VAR) \
	((uint64_t)		(oeo_size)		CRT_VAR) \
	((daos_anchor_t)	(oeo_anchor)		CRT_RAW) \
	((daos_anchor_t)	(oeo_dkey_anchor)	CRT_RAW) \
	((daos_anchor_t)	(oeo_akey_anchor)	CRT_RAW) \
	((daos_key_desc_t)	(oeo_kds)		CRT_ARRAY) \
	((d_sg_list_t)		(oeo_sgl)		CRT_VAR) \
	((d_iov_t)		(oeo_csum_iov)		CRT_VAR) \
	((daos_recx_t)		(oeo_recxs)		CRT_ARRAY) \
	((daos_epoch_range_t)	(oeo_eprs)		CRT_ARRAY)

CRT_RPC_DECLARE(obj_key_enum, DAOS_ISEQ_OBJ_KEY_ENUM, DAOS_OSEQ_OBJ_KEY_ENUM)

#define DAOS_ISEQ_OBJ_PUNCH	/* input fields */		 \
	((struct dtx_id)	(opi_dti)		CRT_RAW) \
	((uuid_t)		(opi_pool_uuid)		CRT_VAR) \
	((uuid_t)		(opi_co_hdl)		CRT_VAR) \
	((uuid_t)		(opi_co_uuid)		CRT_VAR) \
	((daos_unit_oid_t)	(opi_oid)		CRT_RAW) \
	((uint64_t)		(opi_epoch)		CRT_VAR) \
	((uint64_t)		(opi_api_flags)		CRT_VAR) \
	((uint64_t)		(opi_dkey_hash)		CRT_VAR) \
	((uint32_t)		(opi_map_ver)		CRT_VAR) \
	((uint32_t)		(opi_flags)		CRT_VAR) \
	((struct dtx_id)	(opi_dti_cos)		CRT_ARRAY) \
	((d_iov_t)		(opi_dkeys)		CRT_ARRAY) \
	((d_iov_t)		(opi_akeys)		CRT_ARRAY) \
	((struct daos_shard_tgt) (opi_shard_tgts)	CRT_ARRAY)

#define DAOS_OSEQ_OBJ_PUNCH	/* output fields */		 \
	((int32_t)		(opo_ret)		CRT_VAR) \
	((uint32_t)		(opo_map_version)	CRT_VAR)

CRT_RPC_DECLARE(obj_punch, DAOS_ISEQ_OBJ_PUNCH, DAOS_OSEQ_OBJ_PUNCH)

#define DAOS_ISEQ_OBJ_QUERY_KEY	/* input fields */		 \
	((struct dtx_id)	(okqi_dti)		CRT_RAW) \
	((uuid_t)		(okqi_co_hdl)		CRT_VAR) \
	((uuid_t)		(okqi_pool_uuid)	CRT_VAR) \
	((uuid_t)		(okqi_co_uuid)		CRT_VAR) \
	((daos_unit_oid_t)	(okqi_oid)		CRT_RAW) \
	((uint64_t)		(okqi_epoch)		CRT_VAR) \
	((uint64_t)		(okqi_epoch_first)	CRT_VAR) \
	((uint32_t)		(okqi_map_ver)		CRT_VAR) \
	((uint32_t)		(okqi_flags)		CRT_VAR) \
	((uint64_t)		(okqi_api_flags)	CRT_VAR) \
	((daos_key_t)		(okqi_dkey)		CRT_VAR) \
	((daos_key_t)		(okqi_akey)		CRT_VAR)

#define DAOS_OSEQ_OBJ_QUERY_KEY		/* output fields */	 \
	((int32_t)		(okqo_ret)		CRT_VAR) \
	((uint32_t)		(okqo_map_version)	CRT_VAR) \
	((uint64_t)		(okqo_epoch)		CRT_VAR) \
	((uint32_t)		(okqo_flags)		CRT_VAR) \
	((uint32_t)		(okqo_pad32_1)		CRT_VAR) \
	((daos_key_t)		(okqo_dkey)		CRT_VAR) \
	((daos_key_t)		(okqo_akey)		CRT_VAR) \
	/* recx for visible extent */				 \
	((daos_recx_t)		(okqo_recx)		CRT_VAR) \
	/* okqo_recx_parity/okqo_recx_punched not used after 2.2 */ \
	/* recx for EC parity space */				 \
	((daos_recx_t)		(okqo_recx_parity)	CRT_VAR) \
	/* recx for punched EC extents */			 \
	((daos_recx_t)		(okqo_recx_punched)	CRT_VAR) \
	/* epoch for max write */				 \
	((uint64_t)		(okqo_max_epoch)	CRT_VAR)

CRT_RPC_DECLARE(obj_query_key, DAOS_ISEQ_OBJ_QUERY_KEY, DAOS_OSEQ_OBJ_QUERY_KEY)

#define DAOS_ISEQ_OBJ_SYNC /* input fields */			 \
	((uuid_t)		(osi_co_hdl)		CRT_VAR) \
	((uuid_t)		(osi_pool_uuid)		CRT_VAR) \
	((uuid_t)		(osi_co_uuid)		CRT_VAR) \
	((daos_unit_oid_t)	(osi_oid)		CRT_RAW) \
	((uint64_t)		(osi_epoch)		CRT_VAR) \
	((uint32_t)		(osi_map_ver)		CRT_VAR) \
	((uint32_t)		(osi_padding)		CRT_VAR)

#define DAOS_OSEQ_OBJ_SYNC /* output fields */			 \
	((int32_t)		(oso_ret)		CRT_VAR) \
	((uint32_t)		(oso_map_version)	CRT_VAR) \
	((uint64_t)		(oso_epoch)		CRT_VAR)

CRT_RPC_DECLARE(obj_sync, DAOS_ISEQ_OBJ_SYNC, DAOS_OSEQ_OBJ_SYNC)

#define DAOS_ISEQ_OBJ_MIGRATE	/* input fields */			\
	((uuid_t)		(om_pool_uuid)		CRT_VAR)	\
	((uuid_t)		(om_cont_uuid)		CRT_VAR)	\
	((uuid_t)		(om_poh_uuid)		CRT_VAR)	\
	((uuid_t)		(om_coh_uuid)		CRT_VAR)	\
	((uint64_t)		(om_max_eph)		CRT_VAR)	\
	((uint32_t)		(om_version)		CRT_VAR)	\
	((uint32_t)		(om_tgt_idx)		CRT_VAR)	\
	((daos_unit_oid_t)	(om_oids)		CRT_ARRAY)	\
	((uint64_t)		(om_ephs)		CRT_ARRAY)	\
	((uint64_t)		(om_punched_ephs)	CRT_ARRAY)	\
	((uint32_t)		(om_shards)		CRT_ARRAY)	\
	((uint32_t)		(om_new_layout_ver)	CRT_VAR)	\
	((uint32_t)		(om_opc)		CRT_VAR)	\
	((uint32_t)		(om_generation)		CRT_VAR)

#define DAOS_OSEQ_OBJ_MIGRATE	/* output fields */		 \
	((int32_t)		(om_status)		CRT_VAR)

CRT_RPC_DECLARE(obj_migrate, DAOS_ISEQ_OBJ_MIGRATE, DAOS_OSEQ_OBJ_MIGRATE)

#define DAOS_ISEQ_OBJ_EC_AGG	/* input fields */			\
	((uuid_t)		(ea_pool_uuid)		CRT_VAR)	\
	((uuid_t)		(ea_cont_uuid)		CRT_VAR)	\
	((uuid_t)		(ea_poh_uuid)		CRT_VAR)	\
	((uuid_t)		(ea_coh_uuid)		CRT_VAR)	\
	((daos_unit_oid_t)	(ea_oid)		CRT_VAR)	\
	((daos_key_t)		(ea_dkey)		CRT_VAR)	\
	((daos_iod_t)		(ea_iod)		CRT_VAR)	\
	((struct dcs_iod_csums)	(ea_iod_csums)		CRT_ARRAY)	\
	((daos_epoch_range_t)	(ea_epoch_range)	CRT_VAR)	\
	((uint64_t)		(ea_stripenum)		CRT_VAR)	\
	((crt_bulk_t)		(ea_bulk)		CRT_VAR)	\
	((uint32_t)		(ea_map_ver)		CRT_VAR)


#define DAOS_OSEQ_OBJ_EC_AGG	/* output fields */		 \
	((int32_t)		(ea_status)		CRT_VAR) \
	((uint32_t)		(ea_map_ver)		CRT_VAR)

CRT_RPC_DECLARE(obj_ec_agg, DAOS_ISEQ_OBJ_EC_AGG, DAOS_OSEQ_OBJ_EC_AGG)

#define DAOS_ISEQ_OBJ_EC_REP	/* input fields */			\
	((uuid_t)		(er_pool_uuid)		CRT_VAR)	\
	((uuid_t)		(er_cont_uuid)		CRT_VAR)	\
	((uuid_t)		(er_poh_uuid)		CRT_VAR)	\
	((uuid_t)		(er_coh_uuid)		CRT_VAR)	\
	((daos_unit_oid_t)	(er_oid)		CRT_VAR)	\
	((daos_key_t)		(er_dkey)		CRT_VAR)	\
	((daos_iod_t)		(er_iod)		CRT_VAR)	\
	((struct dcs_iod_csums)	(er_iod_csums)		CRT_ARRAY)	\
	((daos_epoch_range_t)	(er_epoch_range)	CRT_VAR)	\
	((uint64_t)		(er_stripenum)		CRT_VAR)	\
	((crt_bulk_t)		(er_bulk)		CRT_VAR)	\
	((uint32_t)		(er_map_ver)		CRT_VAR)

#define DAOS_OSEQ_OBJ_EC_REP	/* output fields */		 \
	((int32_t)		(er_status)		CRT_VAR) \
	((uint32_t)		(er_map_ver)		CRT_VAR)

CRT_RPC_DECLARE(obj_ec_rep, DAOS_ISEQ_OBJ_EC_REP, DAOS_OSEQ_OBJ_EC_REP)

/* object key2anchor in/out */
#define DAOS_ISEQ_OBJ_KEY2ANCHOR	/* input fields */	 \
	((struct dtx_id)	(oki_dti)		CRT_RAW) \
	((daos_unit_oid_t)	(oki_oid)		CRT_RAW) \
	((uuid_t)		(oki_pool_uuid)		CRT_VAR) \
	((uuid_t)		(oki_co_hdl)		CRT_VAR) \
	((uuid_t)		(oki_co_uuid)		CRT_VAR) \
	((uint64_t)		(oki_epoch)		CRT_VAR) \
	((uint32_t)		(oki_map_ver)		CRT_VAR) \
	((uint32_t)		(oki_flags)		CRT_VAR) \
	((daos_key_t)		(oki_dkey)		CRT_VAR) \
	((daos_key_t)		(oki_akey)		CRT_VAR)

#define DAOS_OSEQ_OBJ_KEY2ANCHOR	/* output fields */	 \
	((int32_t)		(oko_ret)		CRT_VAR) \
	((uint32_t)		(oko_map_version)	CRT_VAR) \
	((uint64_t)		(oko_epoch)		CRT_VAR) \
	((daos_anchor_t)	(oko_anchor)		CRT_RAW)

CRT_RPC_DECLARE(obj_key2anchor, DAOS_ISEQ_OBJ_KEY2ANCHOR, DAOS_OSEQ_OBJ_KEY2ANCHOR)

void daos_dc_obj2id(void *ptr, daos_unit_oid_t *id);

enum daos_cpd_sub_opc {
	DCSO_UPDATE		= 0,
	DCSO_READ		= 1,
	DCSO_PUNCH_OBJ		= 2,
	DCSO_PUNCH_DKEY		= 3,
	DCSO_PUNCH_AKEY		= 4,
};

/**
 * Each transaction (in spite of distributed one or simple individual
 * modification) has a 'daos_cpd_sub_head', that is shared by the sub
 * requests belong to the transaction.
 */
struct daos_cpd_sub_head {
	struct dtx_id			 dcsh_xid;
	/* The object ID is used to elect leader for DTX recovery.
	 * If it is empty, then it is for a readonly transaction.
	 */
	daos_unit_oid_t			 dcsh_leader_oid;
	struct dtx_epoch		 dcsh_epoch;
	struct dtx_memberships		*dcsh_mbs;
};

struct daos_cpd_ec_tgts {
	uint32_t			 dcet_shard_idx;
	uint32_t			 dcet_tgt_id;
};

struct daos_cpd_update {
	struct dcs_csum_info		*dcu_dkey_csum;
	struct daos_cpd_ec_tgts		*dcu_ec_tgts;
	/* ID array for the DAOS targets that takes part in EC object update. */
	struct obj_iod_array		 dcu_iod_array;
	/* Used for split EC update request. */
	uint32_t			 dcu_start_shard;
	/* see obj_rpc_flags. */
	uint32_t			 dcu_flags;
	union {
		d_sg_list_t		*dcu_sgls;
		crt_bulk_t		*dcu_bulks;
	};
};

struct daos_cpd_punch {
	daos_key_t			*dcp_akeys;
};

/**
 * It is fake read style (daos fetch/enumerate/query) operation, only for set
 * read timestamp on related target(s). It does not need iod/sgl information.
 */
struct daos_cpd_read {
	daos_iod_t			*dcr_iods;
};

/**
 * Each daos_cpd_sub_req stands for one simple DAOS operation that can be
 * handled via single VOS API call.
 */
struct daos_cpd_sub_req {
	/* See enum daos_cpd_sub_opc.*/
	uint16_t			 dcsr_opc;
	/* Size of 'dcu_ec_tgts', only used for updating of EC object. */
	uint16_t			 dcsr_ec_tgt_nr;
	uint32_t			 dcsr_nr;
	union {
		/* Used by CPD PRC and server side logic. */
		daos_unit_oid_t		 dcsr_oid;
		/* Used by client side cache. */
		struct {
			void			*dcsr_obj;
			d_sg_list_t		*dcsr_sgls;
			struct obj_reasb_req	*dcsr_reasb;
		};
	};
	daos_key_t			 dcsr_dkey;
	uint64_t			 dcsr_dkey_hash;
	uint64_t			 dcsr_api_flags;
	union {
		struct daos_cpd_update	 dcsr_update;
		struct daos_cpd_punch	 dcsr_punch;
		struct daos_cpd_read	 dcsr_read;
	};
};

/**
 * Used for locating a sub request to be executed on the specified DAOS target.
 */
struct daos_cpd_req_idx {
	/* Shard index of the object for the sub request on this DAOS target. */
	uint32_t			 dcri_shard_off;
	/* Shard identifier of the object for the sub request. */
	uint32_t			 dcri_shard_id;
	/* The index (relative to the first sub request for its transaction)
	 * of sub-request in the 'oci_sub_reqs' array. For parsing convenience,
	 * DCSO_READ requests firstly, then modification ones. The update and
	 * punch are sorted as their original executed order.
	 */
	uint32_t			 dcri_req_idx;
	/* 64-bits alignment. */
	uint32_t			 dcri_padding;
};

/**
 * Each 'daos_cpd_disp_ent' describes all the sub requests that belong to
 * one transaction (in spite of compounded or not) on one DAOS target.
 */
struct daos_cpd_disp_ent {
	/* The count of read sub requests for the DTX on the DAOS target. */
	uint32_t			 dcde_read_cnt;
	/* The count of write sub requests for the DTX on the DAOS target.
	 * It can be up to '2 ^ 16 - 1' at most because of the restriction
	 * of 16-bits minor epoch on the server.
	 */
	uint32_t			 dcde_write_cnt;
	/* Pointer to 'daos_cpd_req_idx' array. */
	struct daos_cpd_req_idx		*dcde_reqs;
};

enum daos_cpd_sg_type {
	DCST_UNKNOWN	= 0,
	DCST_HEAD	= 1,
	DCST_REQ_CLI	= 2,
	DCST_REQ_SRV	= 3,
	DCST_ENT	= 4,
	DCST_TGT	= 5,
	DCST_BULK_HEAD	= 6,
	DCST_BULK_REQ	= 7,
	DCST_BULK_ENT	= 8,
	DCST_BULK_TGT	= 9,
};

struct daos_cpd_bulk {
	/*
	 * The epoch may be generated when leader handle the CPD RPC, so we cannot bulk
	 * transfer the epoch from client to non-leader. So DCST_BULK_HEAD only contain
	 * the MBS information. The other parts of daos_cpd_sub_head are packed here.
	 *
	 * Another benefit of separated MBS from the other parts from daos_cpd_sub_head
	 * is to avoid data copy when prepare the MBS bulk. See dc_tx_commit_prepare().
	 */
	struct daos_cpd_sub_head	 dcb_head;
	uint32_t			 dcb_size;
	uint32_t			 dcb_padding;
	crt_bulk_t			*dcb_bulk;
	/*
	 * The following are only used to handle the bulk for CPD RPC body temporarily,
	 * do not pack on-wire.
	 */
	d_sg_list_t			 dcb_sgl;
	d_iov_t				 dcb_iov;
	uint32_t			 dcb_type;
	uint32_t			 dcb_item_nr;
	crt_proc_t			 dcb_proc;
	struct daos_cpd_sub_req		*dcb_reqs;
};

/** Scatter/gather info for CPD RPC data structure. */
struct daos_cpd_sg {
	union {
		uint32_t		 dcs_type;	/* See enum daos_cpd_sg_type. */
		struct {
			uint16_t	 dcs_type_base;
			/* If dcs_type is DCST_BULK_ENT, then it is used to locate the
			 * daos_cpd_disp_ent in the bulk transferred (global) dispatch
			 * entries (dcs_buf) on non-leader. Only used on server side.
			 *
			 * For 2.2 or older release, it is always zero.
			 */
			uint16_t	 dcs_dcde_idx;
		};
	};
	uint32_t			 dcs_nr;
	void				*dcs_buf;
};

#define DAOS_ISEQ_OBJ_CPD /* input fields */				    \
	((uuid_t)			(oci_pool_uuid)		CRT_VAR)    \
	((uuid_t)			(oci_co_hdl)		CRT_VAR)    \
	((uuid_t)			(oci_co_uuid)		CRT_VAR)    \
	((uint32_t)			(oci_map_ver)		CRT_VAR)    \
	((uint32_t)			(oci_flags)		CRT_VAR)    \
	/* scatter array for daos_cpd_sub_head. */			    \
	((struct daos_cpd_sg)		(oci_sub_heads)		CRT_ARRAY)  \
	/* scatter array for daos_cpd_sub_req. */			    \
	((struct daos_cpd_sg)		(oci_sub_reqs)		CRT_ARRAY)  \
	/* scatter array for daos_cpd_disp_ent. */			    \
	((struct daos_cpd_sg)		(oci_disp_ents)		CRT_ARRAY)  \
	/* scatter array for daos_shard_tgt. */				    \
	((struct daos_cpd_sg)		(oci_disp_tgts)		CRT_ARRAY)

	/* For parse and dispatch convenience, the 'oci_disp_tgts' are sorted
	 * as the same order as 'oci_disp_ents' do. Each transaction has each
	 * own different daos_shard_tgt set. If some DAOS target contains the
	 * requests belonging to multiple transactions, its daos_shard_tgt is
	 * repeatedly packed in every related dipatch set.
	 */

#define DAOS_OSEQ_OBJ_CPD /* output fields */				    \
	((int32_t)			(oco_ret)		CRT_VAR)    \
	((uint32_t)			(oco_map_version)	CRT_VAR)    \
	((uint64_t)			(oco_sub_epochs)	CRT_ARRAY)  \
	((int32_t)			(oco_sub_rets)		CRT_ARRAY)

	/* Compouned replies. Eac sub reply responding to one independent
	 * transaction.
	 *
	 * Resent:
	 * If the CPD RPC needs to be resent, then all the sub requests in
	 * the CPD RPC are resent. So the server side logic needs to track
	 * sub requests resent one by one.
	 *
	 * Restart:
	 * Support restart the specified independent DTX without affecting
	 * other transactions in the same CPD RPC.
	 */

CRT_RPC_DECLARE(obj_cpd, DAOS_ISEQ_OBJ_CPD, DAOS_OSEQ_OBJ_CPD)

static inline int
obj_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep, crt_opcode_t opc,
	       crt_rpc_t **req)
{
	crt_opcode_t opcode;

	if (DAOS_FAIL_CHECK(DAOS_OBJ_REQ_CREATE_TIMEOUT))
		return -DER_TIMEDOUT;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_OBJ_MODULE,
				 dc_obj_proto_version ? dc_obj_proto_version : DAOS_OBJ_VERSION);
	tgt_ep->ep_tag = daos_rpc_tag(DAOS_REQ_IO, tgt_ep->ep_tag);

	return crt_req_create(crt_ctx, tgt_ep, opcode, req);
}

void obj_reply_set_status(crt_rpc_t *rpc, int status);
int obj_reply_get_status(crt_rpc_t *rpc);
void obj_reply_map_version_set(crt_rpc_t *rpc, uint32_t map_version);
uint32_t obj_reply_map_version_get(crt_rpc_t *rpc);

int crt_proc_struct_daos_cpd_sub_req(crt_proc_t proc, crt_proc_op_t proc_op,
				     struct daos_cpd_sub_req *dcsr, bool with_oid);

static inline bool
obj_is_modification_opc(uint32_t opc)
{
	return opc == DAOS_OBJ_RPC_UPDATE || opc == DAOS_OBJ_RPC_TGT_UPDATE ||
		opc == DAOS_OBJ_RPC_PUNCH || opc == DAOS_OBJ_RPC_TGT_PUNCH ||
		opc == DAOS_OBJ_RPC_PUNCH_DKEYS ||
		opc == DAOS_OBJ_RPC_TGT_PUNCH_DKEYS ||
		opc == DAOS_OBJ_RPC_PUNCH_AKEYS ||
		opc == DAOS_OBJ_RPC_TGT_PUNCH_AKEYS;
}

#define DAOS_OBJ_UPDATE_MODE_MASK	(DAOS_OO_RW | DAOS_OO_EXCL |	\
					 DAOS_OO_IO_RAND | DAOS_OO_IO_SEQ)

static inline bool
obj_is_fetch_opc(uint32_t opc)
{
	return opc == DAOS_OBJ_RPC_FETCH;
}

static inline bool
obj_is_ec_agg_opc(uint32_t opc)
{
	return opc == DAOS_OBJ_RPC_EC_AGGREGATE ||
	       opc == DAOS_OBJ_RPC_EC_REPLICATE;
}

static inline bool
obj_rpc_is_update(crt_rpc_t *rpc)
{
	return opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_UPDATE ||
	       opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_TGT_UPDATE;
}

static inline bool
obj_rpc_is_fetch(crt_rpc_t *rpc)
{
	return opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_FETCH;
}

static inline bool
obj_rpc_is_punch(crt_rpc_t *rpc)
{
	return opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_PUNCH ||
	       opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_PUNCH_DKEYS ||
	       opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_PUNCH_AKEYS ||
	       opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_TGT_PUNCH ||
	       opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_TGT_PUNCH_DKEYS ||
	       opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_TGT_PUNCH_AKEYS;
}

static inline bool
obj_rpc_is_migrate(crt_rpc_t *rpc)
{
	return opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_MIGRATE;
}

static inline bool
obj_is_enum_opc(uint32_t opc)
{
	return (opc == DAOS_OBJ_DKEY_RPC_ENUMERATE ||
		opc == DAOS_OBJ_RPC_ENUMERATE ||
		opc == DAOS_OBJ_AKEY_RPC_ENUMERATE ||
		opc == DAOS_OBJ_RECX_RPC_ENUMERATE);
}
#endif /* __DAOS_OBJ_RPC_H__ */
