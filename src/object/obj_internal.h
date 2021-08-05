/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos_sr
 *
 * src/object/obj_internal.h
 */
#ifndef __DAOS_OBJ_INTENRAL_H__
#define __DAOS_OBJ_INTENRAL_H__

#include <abt.h>
#include <stdint.h>
#include <daos/common.h>
#include <daos/event.h>
#include <daos/tse.h>
#include <daos/task.h>
#include <daos/placement.h>
#include <daos/btree.h>
#include <daos/btree_class.h>
#include <daos/dtx.h>
#include <daos/object.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/dtx_srv.h>
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_producer.h>

#include "obj_rpc.h"
#include "obj_ec.h"

/**
 * This environment is mostly for performance evaluation.
 */
#define IO_BYPASS_ENV	"DAOS_IO_BYPASS"

struct obj_io_context;

/**
 * Bypass client I/O RPC, it means the client stack will complete the
 * fetch/update RPC immediately, nothing will be submitted to remote server.
 * This mode is for client I/O stack performance benchmark.
 */
extern bool	cli_bypass_rpc;
/** Switch of server-side IO dispatch */
extern unsigned int	srv_io_mode;

/** client object shard */
struct dc_obj_shard {
	/** refcount */
	unsigned int		do_ref;
	/** object id */
	daos_unit_oid_t		do_id;
	/** container handler of the object */
	daos_handle_t		do_co_hdl;
	uint8_t			do_target_idx;	/* target VOS index in node */
	uint32_t		do_target_rank;
	struct pl_obj_shard	do_pl_shard;
	/** point back to object */
	struct dc_object	*do_obj;
};

#define do_shard	do_pl_shard.po_shard
#define do_target_id	do_pl_shard.po_target
#define do_fseq		do_pl_shard.po_fseq
#define do_rebuilding	do_pl_shard.po_rebuilding

/** client object layout */
struct dc_obj_layout {
	/** The reference for the shards that are opened (in-using). */
	unsigned int		do_open_count;
	struct dc_obj_shard	do_shards[0];
};

/** Client stack object */
struct dc_object {
	/** link chain in the global handle hash table */
	struct d_hlink		 cob_hlink;
	/**
	 * Object metadata stored in the OI table. For those object classes
	 * and have no metadata in OI table, DAOS only stores OID and pool map
	 * version in it.
	 */
	struct daos_obj_md	 cob_md;
	/** object class attribute */
	struct daos_oclass_attr	 cob_oca;
	/** container open handle */
	daos_handle_t		 cob_coh;
	/** cob_spin protects obj_shards' do_ref */
	pthread_spinlock_t	 cob_spin;

	/* cob_lock protects layout and shard objects ptrs */
	pthread_rwlock_t	 cob_lock;

	/** object open mode */
	unsigned int		 cob_mode;
	unsigned int		 cob_version;
	unsigned int		 cob_shards_nr;
	unsigned int		 cob_grp_size;
	unsigned int		 cob_grp_nr;
	/**
	 * The array for the latest time (in second) of
	 * being asked to fetch from leader.
	 */
	uint64_t		*cob_time_fetch_leader;
	/** shard object ptrs */
	struct dc_obj_layout	*cob_shards;
};

/**
 * Reassembled obj request.
 * User input iod/sgl possibly need to be reassembled at client before sending
 * to server, for example:
 * 1) merge adjacent recxs, or sort out-of-order recxs and generate new sgl to
 *    match with it;
 * 2) For EC obj, split iod/recxs to each target, generate new sgl to match with
 *    it, create oiod/siod to specify each shard/tgt's IO req.
 */
struct obj_reasb_req {
	/* epoch for IO (now only used for fetch */
	struct dtx_epoch		 orr_epoch;
	/* original user input iods/sgls */
	daos_iod_t			*orr_uiods;
	d_sg_list_t			*orr_usgls;
	/* reassembled iods/sgls */
	daos_iod_t			*orr_iods;
	d_sg_list_t			*orr_sgls;
	struct obj_io_desc		*orr_oiods;
	struct obj_ec_recx_array	*orr_recxs;
	struct obj_ec_seg_sorter	*orr_sorters;
	struct dcs_layout		*orr_singv_los;
	/* to record returned data size from each targets */
	daos_size_t			*orr_data_sizes;
	/* number of targets this IO req involves */
	uint32_t			 orr_tgt_nr;
	/* number of targets that with IOM handled */
	uint32_t			 orr_iom_tgt_nr;
	/* number of iom extends */
	uint32_t			 orr_iom_nr;
	struct daos_oclass_attr		*orr_oca;
	struct obj_ec_codec		*orr_codec;
	pthread_mutex_t			 orr_mutex;
	/* target bitmap, one bit for each target (from first data cell to last
	 * parity cell.
	 */
	uint8_t				*tgt_bitmap;
	struct obj_tgt_oiod		*tgt_oiods;
	/* IO failure information */
	struct obj_ec_fail_info		*orr_fail;
	/* object ID */
	daos_obj_id_t			 orr_oid;
	/* #iods of IO req */
	uint32_t			 orr_iod_nr;
	/* for data recovery flag */
	uint32_t			 orr_recov:1,
	/* for snapshot data recovery flag */
					 orr_recov_snap:1,
	/* for iod_size fetching flag */
					 orr_size_fetch:1,
	/* for iod_size fetched flag */
					 orr_size_fetched:1,
	/* only with single target flag */
					 orr_single_tgt:1,
	/* only for single-value IO flag */
					 orr_singv_only:1,
	/* the flag of IOM re-allocable (used for EC IOM merge) */
					 orr_iom_realloc:1,
	/* iod_size is set by IO reply */
					 orr_size_set:1,
	/* orr_fail allocated flag, recovery task's orr_fail is inherited */
					 orr_fail_alloc:1;
};

static inline void
enum_anchor_copy(daos_anchor_t *dst, daos_anchor_t *src)
{
	memcpy(dst, src, sizeof(*dst));
}

extern struct dss_module_key obj_module_key;

/* Per pool attached to the migrate tls(per xstream) */
struct migrate_pool_tls {
	/* POOL UUID and pool to be migrated */
	uuid_t			mpt_pool_uuid;
	struct ds_pool_child	*mpt_pool;
	unsigned int		mpt_version;

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

	/* The current inflight iod, mainly used for controlling
	 * rebuild inflight rate to avoid the DMA buffer overflow.
	 */
	uint64_t		mpt_inflight_size;
	uint64_t		mpt_inflight_max_size;
	ABT_cond		mpt_inflight_cond;
	ABT_mutex		mpt_inflight_mutex;
	int			mpt_inflight_max_ult;
	/* migrate leader ULT */
	unsigned int		mpt_ult_running:1,
	/* Indicates whether objects on the migration destination should be
	 * removed prior to migrating new data here. This is primarily useful
	 * for reintegration to ensure that any data that has adequate replica
	 * data to reconstruct will prefer the remote data over possibly stale
	 * existing data. Objects that don't have remote replica data will not
	 * be removed.
	 */
				mpt_del_local_objs:1,
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
};

struct obj_ec_parity {
	unsigned char	**p_bufs;
	unsigned int	  p_nr;
};

static inline struct obj_tls *
obj_tls_get()
{
	return dss_module_key_get(dss_tls_get(), &obj_module_key);
}

typedef int (*shard_io_cb_t)(struct dc_obj_shard *shard, enum obj_rpc_opc opc,
			     void *shard_args,
			     struct daos_shard_tgt *fw_shard_tgts,
			     uint32_t fw_cnt, tse_task_t *task);

/* shard update/punch auxiliary args, must be the first field of
 * shard_rw_args and shard_punch_args.
 */
struct shard_auxi_args {
	struct dc_object	*obj;
	struct obj_auxi_args	*obj_auxi;
	shard_io_cb_t		 shard_io_cb;
	struct dtx_epoch	 epoch;
	uint32_t		 shard;
	uint32_t		 target;
	uint32_t		 map_ver;
	/* only for EC, the target idx [0, k + p) */
	uint16_t		 ec_tgt_idx;
	/* group index within the req_tgts->ort_shard_tgts */
	uint16_t		 grp_idx;
	/* only for EC, the start shard of the EC stripe */
	uint32_t		 start_shard;
	uint32_t		 flags;
};

struct shard_rw_args {
	struct shard_auxi_args	 auxi;
	daos_obj_rw_t		*api_args;
	struct dtx_id		 dti;
	uint64_t		 dkey_hash;
	crt_bulk_t		*bulks;
	struct obj_io_desc	*oiods;
	uint64_t		*offs;
	struct dcs_csum_info	*dkey_csum;
	struct dcs_iod_csums	*iod_csums;
	struct obj_reasb_req	*reasb_req;
};

struct shard_punch_args {
	struct shard_auxi_args	 pa_auxi;
	daos_obj_punch_t	*pa_api_args;
	uuid_t			 pa_coh_uuid;
	uuid_t			 pa_cont_uuid;
	uint64_t		 pa_dkey_hash;
	struct dtx_id		 pa_dti;
	uint32_t		 pa_opc;
};

struct shard_list_args {
	struct shard_auxi_args	 la_auxi;
	daos_obj_list_t		*la_api_args;
	struct dtx_id		 la_dti;
	daos_recx_t		*la_recxs;
	uint32_t		la_nr;
	d_sg_list_t		*la_sgl;
	daos_key_desc_t		*la_kds;
	daos_anchor_t		*la_anchor;
	daos_anchor_t		*la_akey_anchor;
	daos_anchor_t		*la_dkey_anchor;
};

struct obj_auxi_list_recx {
	daos_recx_t	recx;
	d_list_t	recx_list;
};

struct obj_auxi_list_key {
	d_iov_t		key;
	d_list_t	key_list;
};

struct obj_auxi_list_obj_enum {
	d_iov_t		dkey;
	d_list_t	enum_list;
	daos_iod_t	*iods;
	d_list_t	*recx_lists;
	int		iods_nr;
};

int
merge_recx(d_list_t *head, uint64_t offset, uint64_t size);

struct ec_bulk_spec {
	uint64_t is_skip:	1;
	uint64_t len:		63;
};
D_CASSERT(sizeof(struct ec_bulk_spec) == sizeof(uint64_t));

static inline void
ec_bulk_spec_set(uint64_t len, bool skip, int index,
		 struct ec_bulk_spec **skip_list)
{
	(*skip_list)[index].is_skip = skip;
	(*skip_list)[index].len = len;
}

static inline uint64_t
ec_bulk_spec_get_len(int index, struct ec_bulk_spec *skip_list)
{
	return skip_list[index].len;
}

static inline bool
ec_bulk_spec_get_skip(int index, struct ec_bulk_spec *skip_list)
{
	return skip_list[index].is_skip;
}
struct shard_sync_args {
	struct shard_auxi_args	 sa_auxi;
	daos_epoch_t		*sa_epoch;
};

#define DOVA_NUM	32
#define DOVA_BUF_LEN	4096

struct dc_obj_verify_cursor {
	daos_key_t		 dkey;
	daos_iod_t		 iod;
	daos_recx_t		 recx;
	uint32_t		 gen;
	uint32_t		 type;
	uint32_t		 kds_idx;
	uint32_t		 iod_off;
	void			*ptr;
};

struct dc_obj_verify_args {
	daos_handle_t			 oh;
	daos_handle_t			 th;
	daos_size_t			 size;
	uint32_t			 num;
	unsigned int			 eof:1,
					 non_exist:1,
					 data_fetched:1;
	daos_key_desc_t			 kds[DOVA_NUM];
	d_sg_list_t			 list_sgl;
	d_sg_list_t			 fetch_sgl;
	daos_anchor_t			 anchor;
	daos_anchor_t			 dkey_anchor;
	daos_anchor_t			 akey_anchor;
	d_iov_t				 list_iov;
	d_iov_t				 fetch_iov;
	daos_size_t			 list_buf_len;
	daos_size_t			 fetch_buf_len;
	char				*list_buf;
	char				*fetch_buf;
	char				 inline_buf[DOVA_BUF_LEN];
	uint32_t			 current_shard;
	struct dc_obj_verify_cursor	 cursor;
};

int
dc_set_oclass(uint64_t rf_factor, int domain_nr, int target_nr,
	      daos_ofeat_t ofeats, daos_oclass_hints_t hints,
	      daos_oclass_id_t *oc_id_);

int dc_obj_shard_open(struct dc_object *obj, daos_unit_oid_t id,
		      unsigned int mode, struct dc_obj_shard *shard);
void dc_obj_shard_close(struct dc_obj_shard *shard);

int dc_obj_shard_rw(struct dc_obj_shard *shard, enum obj_rpc_opc opc,
		    void *shard_args, struct daos_shard_tgt *fw_shard_tgts,
		    uint32_t fw_cnt, tse_task_t *task);

int
ec_obj_update_encode(tse_task_t *task, daos_obj_id_t oid,
		     struct daos_oclass_attr *oca, uint64_t *tgt_set);

int dc_obj_shard_punch(struct dc_obj_shard *shard, enum obj_rpc_opc opc,
		       void *shard_args, struct daos_shard_tgt *fw_shard_tgts,
		       uint32_t fw_cnt, tse_task_t *task);

int dc_obj_shard_list(struct dc_obj_shard *shard, enum obj_rpc_opc opc,
		      void *shard_args, struct daos_shard_tgt *fw_shard_tgts,
		      uint32_t fw_cnt, tse_task_t *task);

int dc_obj_shard_query_key(struct dc_obj_shard *shard, struct dtx_epoch *epoch,
			   uint32_t flags, struct dc_object *obj,
			   daos_key_t *dkey, daos_key_t *akey,
			   daos_recx_t *recx, const uuid_t coh_uuid,
			   const uuid_t cont_uuid, struct dtx_id *dti,
			   unsigned int *map_ver, daos_handle_t th,
			   tse_task_t *task);

int dc_obj_shard_sync(struct dc_obj_shard *shard, enum obj_rpc_opc opc,
		      void *shard_args, struct daos_shard_tgt *fw_shard_tgts,
		      uint32_t fw_cnt, tse_task_t *task);

int dc_obj_verify_rdg(struct dc_object *obj, struct dc_obj_verify_args *dova,
		      uint32_t rdg_idx, uint32_t reps, daos_epoch_t epoch);
bool obj_op_is_ec_fetch(struct obj_auxi_args *obj_auxi);
int obj_recx_ec2_daos(struct daos_oclass_attr *oca, int shard,
		      daos_recx_t **recxs_p, unsigned int *nr);
int obj_reasb_req_init(struct obj_reasb_req *reasb_req, daos_iod_t *iods,
		       uint32_t iod_nr, struct daos_oclass_attr *oca);
void obj_reasb_req_fini(struct obj_reasb_req *reasb_req, uint32_t iod_nr);
int obj_bulk_prep(d_sg_list_t *sgls, unsigned int nr, bool bulk_bind,
		  crt_bulk_perm_t bulk_perm, tse_task_t *task,
		  crt_bulk_t **p_bulks);
struct daos_oclass_attr *obj_get_oca(struct dc_object *obj);
bool obj_is_ec(struct dc_object *obj);
int obj_get_replicas(struct dc_object *obj);
int obj_shard_open(struct dc_object *obj, unsigned int shard,
		   unsigned int map_ver, struct dc_obj_shard **shard_ptr);
int obj_dkey2grpidx(struct dc_object *obj, uint64_t hash, unsigned int map_ver);
int obj_pool_query_task(tse_sched_t *sched, struct dc_object *obj,
			unsigned int map_ver, tse_task_t **taskp);
bool obj_csum_dedup_candidate(struct cont_props *props, daos_iod_t *iods,
			      uint32_t iod_nr);

#define obj_shard_close(shard)	dc_obj_shard_close(shard)
int obj_recx_ec_daos2shard(struct daos_oclass_attr *oca, int shard,
			   daos_recx_t **recxs_p, unsigned int *iod_nr);
int obj_ec_singv_encode_buf(daos_unit_oid_t oid, struct daos_oclass_attr *oca,
			    daos_iod_t *iod, d_sg_list_t *sgl, d_iov_t *e_iov);
int obj_ec_singv_split(daos_unit_oid_t oid, struct daos_oclass_attr *oca,
		       daos_size_t iod_size, d_sg_list_t *sgl);
int
obj_singv_ec_rw_filter(daos_unit_oid_t oid, struct daos_oclass_attr *oca,
		       daos_iod_t *iods, uint64_t *offs, daos_epoch_t epoch,
		       uint32_t flags, uint32_t start_shard,
		       uint32_t nr, bool for_update, bool deg_fetch,
		       struct daos_recx_ep_list **recov_lists_ptr);

static inline struct pl_obj_shard*
obj_get_shard(void *data, int idx)
{
	struct dc_object	*obj = data;

	return &obj->cob_shards->do_shards[idx].do_pl_shard;
}

static inline bool
obj_retry_error(int err)
{
	return err == -DER_TIMEDOUT || err == -DER_STALE ||
	       err == -DER_INPROGRESS || err == -DER_GRPVER ||
	       err == -DER_EXCLUDED || err == -DER_CSUM ||
	       err == -DER_TX_BUSY || err == -DER_TX_UNCERTAIN ||
	       daos_crt_network_error(err);
}

static inline daos_handle_t
obj_ptr2hdl(struct dc_object *obj)
{
	daos_handle_t oh;

	daos_hhash_link_key(&obj->cob_hlink, &oh.cookie);
	return oh;
}

static inline void
dc_io_epoch_set(struct dtx_epoch *epoch)
{
	if (srv_io_mode == DIM_CLIENT_DISPATCH) {
		epoch->oe_value = crt_hlc_get();
		epoch->oe_first = epoch->oe_value;
		/* DIM_CLIENT_DISPATCH doesn't promise consistency. */
		epoch->oe_flags = 0;
	} else {
		epoch->oe_value = DAOS_EPOCH_MAX;
		epoch->oe_first = epoch->oe_value;
		epoch->oe_flags = 0;
	}
}

static inline void
dc_sgl_out_set(d_sg_list_t *sgl, daos_size_t data_size)
{
	d_iov_t		*iov;
	daos_size_t	 buf_size;
	uint32_t	 i;

	if (data_size == 0) {
		sgl->sg_nr_out = 0;
		return;
	}
	buf_size = 0;
	for (i = 0; i < sgl->sg_nr; i++) {
		iov = &sgl->sg_iovs[i];
		buf_size += iov->iov_buf_len;
		if (buf_size < data_size) {
			iov->iov_len = iov->iov_buf_len;
			sgl->sg_nr_out = i + 1;
			continue;
		}

		iov->iov_len = iov->iov_buf_len -
			       (buf_size - data_size);
		sgl->sg_nr_out = i + 1;
		break;
	}
}

void obj_shard_decref(struct dc_obj_shard *shard);
void obj_shard_addref(struct dc_obj_shard *shard);
void obj_addref(struct dc_object *obj);
void obj_decref(struct dc_object *obj);
int obj_get_grp_size(struct dc_object *obj);
struct dc_object *obj_hdl2ptr(daos_handle_t oh);

/* handles, pointers for handling I/O */
struct obj_io_context {
	struct ds_cont_hdl	*ioc_coh;
	struct ds_cont_child	*ioc_coc;
	struct daos_oclass_attr	 ioc_oca;
	daos_handle_t		 ioc_vos_coh;
	uint32_t		 ioc_map_ver;
	uint32_t		 ioc_opc;
	uint64_t		 ioc_start_time;
	uint64_t		 ioc_io_size;
	uint32_t		 ioc_began:1,
				 ioc_free_sgls:1,
				 ioc_lost_reply:1,
				 ioc_fetch_snap:1;
};

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


static inline struct daos_cpd_sub_head *
ds_obj_cpd_get_dcsh(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_sub_heads.ca_count <= dtx_idx)
		return NULL;

	dcs = (struct daos_cpd_sg *)oci->oci_sub_heads.ca_arrays + dtx_idx;

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
	return dcs->dcs_buf;
}

static inline struct daos_cpd_disp_ent *
ds_obj_cpd_get_dcde(crt_rpc_t *rpc, int dtx_idx, int ent_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_disp_ents.ca_count <= dtx_idx)
		return NULL;

	dcs = (struct daos_cpd_sg *)oci->oci_disp_ents.ca_arrays + dtx_idx;

	if (ent_idx >= dcs->dcs_nr)
		return NULL;

	return (struct daos_cpd_disp_ent *)dcs->dcs_buf + ent_idx;
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
ds_obj_cpd_get_tgt_cnt(crt_rpc_t *rpc, int dtx_idx)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct daos_cpd_sg	*dcs;

	if (oci->oci_disp_tgts.ca_count <= dtx_idx)
		return -DER_INVAL;

	dcs = (struct daos_cpd_sg *)oci->oci_disp_tgts.ca_arrays + dtx_idx;
	return dcs->dcs_nr;
}

static inline uint64_t
obj_dkey2hash(daos_obj_id_t oid, daos_key_t *dkey)
{
	/* return 0 for NULL dkey, for example obj punch and list dkey */
	if (dkey == NULL)
		return 0;

	if (daos_obj_id2feat(oid) & DAOS_OF_DKEY_UINT64)
		return *(uint64_t *)dkey->iov_buf;

	return d_hash_murmur64((unsigned char *)dkey->iov_buf,
			       dkey->iov_len, 5731);
}

static inline int
recx_compare(const void *rank1, const void *rank2)
{
	const daos_recx_t *r1 = rank1;
	const daos_recx_t *r2 = rank2;

	D_ASSERT(r1 != NULL && r2 != NULL);
	if (r1->rx_idx < r2->rx_idx)
		return -1;
	else if (r1->rx_idx == r2->rx_idx)
		return 0;
	else /** r1->rx_idx < r2->rx_idx */
		return 1;
}

static inline void
daos_iom_sort(daos_iom_t *map)
{
	if (map == NULL)
		return;
	qsort(map->iom_recxs, map->iom_nr_out,
	      sizeof(*map->iom_recxs), recx_compare);
}

static inline void
daos_iom_dump(daos_iom_t *iom)
{
	uint32_t	i;

	if (iom == NULL)
		return;

	if (iom->iom_type == DAOS_IOD_ARRAY)
		D_PRINT("iom_type array\n");
	else if (iom->iom_type == DAOS_IOD_SINGLE)
		D_PRINT("iom_type single\n");
	else
		D_PRINT("iom_type bad (%d)\n", iom->iom_type);

	D_PRINT("iom_nr %d, iom_nr_out %d, iom_flags %d\n",
		iom->iom_nr, iom->iom_nr_out, iom->iom_flags);
	D_PRINT("iom_size "DF_U64"\n", iom->iom_size);
	D_PRINT("iom_recx_lo - "DF_RECX"\n", DP_RECX(iom->iom_recx_lo));
	D_PRINT("iom_recx_hi - "DF_RECX"\n", DP_RECX(iom->iom_recx_hi));

	if (iom->iom_recxs == NULL) {
		D_PRINT("NULL iom_recxs array\n");
		return;
	}

	D_PRINT("iom_recxs array -\n");
	for (i = 0; i < iom->iom_nr_out; i++) {
		D_PRINT("[%d] "DF_RECX" ", i, DP_RECX(iom->iom_recxs[i]));
		if (i % 8 == 7)
			D_PRINT("\n");
	}
	D_PRINT("\n");
}

static inline bool
obj_dtx_need_refresh(struct dtx_handle *dth, int rc)
{
	return rc == -DER_INPROGRESS && dth->dth_share_tbd_count > 0;
}

static inline void
daos_recx_ep_list_set(struct daos_recx_ep_list *lists, unsigned int nr,
		      daos_epoch_t epoch, bool snapshot)
{
	struct daos_recx_ep_list	*list;
	struct daos_recx_ep		*recx_ep;
	unsigned int			 i, j;

	for (i = 0; i < nr; i++) {
		list = &lists[i];
		list->re_ep_valid = 1;
		if (epoch == 0)
			continue;
		if (snapshot)
			list->re_snapshot = 1;
		for (j = 0; j < list->re_nr; j++) {
			recx_ep = &list->re_items[j];
			if (snapshot)
				recx_ep->re_ep = epoch;
			else
				recx_ep->re_ep = max(recx_ep->re_ep, epoch);
		}
	}
}

static inline bool
daos_recx_ep_list_ep_valid(struct daos_recx_ep_list *list)
{
	return (list->re_ep_valid == 1);
}

/** Query the highest and lowest recx in the recx_ep_list */
int  obj_class_init(void);
void obj_class_fini(void);
int  obj_utils_init(void);
void obj_utils_fini(void);

/* obj_tx.c */
int
dc_tx_check_pmv(daos_handle_t th);

int
dc_tx_hdl2epoch_and_pmv(daos_handle_t th, struct dtx_epoch *epoch,
			uint32_t *pmv);

/** See dc_tx_get_epoch. */
enum dc_tx_get_epoch_rc {
	DC_TX_GE_CHOSEN,
	DC_TX_GE_CHOOSING,
	DC_TX_GE_REINIT
};

int
dc_tx_get_epoch(tse_task_t *task, daos_handle_t th, struct dtx_epoch *epoch);

int
dc_tx_op_end(tse_task_t *task, daos_handle_t th, struct dtx_epoch *req_epoch,
	     int rep_rc, daos_epoch_t rep_epoch);

int
dc_tx_get_dti(daos_handle_t th, struct dtx_id *dti);

int
dc_tx_attach(daos_handle_t th, struct dc_object *obj, enum obj_rpc_opc opc,
	     tse_task_t *task);

int
dc_tx_convert(struct dc_object *obj, enum obj_rpc_opc opc, tse_task_t *task);

/* obj_enum.c */
int
fill_oid(daos_unit_oid_t oid, struct dss_enum_arg *arg);
#endif /* __DAOS_OBJ_INTENRAL_H__ */
