/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
 * This file is part of daos_sr
 *
 * src/object/obj_internal.h
 */
#ifndef __DAOS_OBJ_INTENRAL_H__
#define __DAOS_OBJ_INTENRAL_H__

#include <abt.h>
#include <daos/common.h>
#include <daos/event.h>
#include <daos/tse.h>
#include <daos/placement.h>
#include <daos/btree.h>
#include <daos/btree_class.h>
#include <daos/dtx.h>
#include <daos/object.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/dtx_srv.h>

#include "obj_rpc.h"

/**
 * This environment is mostly for performance evaluation.
 */
#define IO_BYPASS_ENV	"DAOS_IO_BYPASS"

/* EC parity is stored in a private address range that is selected by setting
 * the most-significant bit of the offset (an unsigned long). This effectively
 * limits the addressing of user extents to the lower 63 bits of the offset
 * range. The client stack should enforce this limitation.
 */
#define PARITY_INDICATOR (1UL << 63)

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
	/* Metadata for this shard */
	uint64_t		do_attr;
	/** refcount */
	unsigned int		do_ref;
	/** object id */
	daos_unit_oid_t		do_id;
	/** container handler of the object */
	daos_handle_t		do_co_hdl;
	uint32_t		do_target_idx;	/* target VOS index in node */
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
	/** container open handle */
	daos_handle_t		 cob_coh;
	/** object open mode */
	unsigned int		 cob_mode;
	/** cob_spin protects obj_shards' do_ref */
	pthread_spinlock_t	 cob_spin;

	/* cob_lock protects layout and shard objects ptrs */
	pthread_rwlock_t	 cob_lock;

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

/** EC codec for object EC encoding/decoding */
struct obj_ec_codec {
	/** encode matrix, can be used to generate decode matrix */
	unsigned char		*ec_en_matrix;
	/**
	 * GF (galois field) tables, pointer to array of input tables generated
	 * from coding coefficients. Needed for both encoding and decoding.
	 */
	unsigned char		*ec_gftbls;
};

static inline void
enum_anchor_copy(daos_anchor_t *dst, daos_anchor_t *src)
{
	memcpy(dst, src, sizeof(*dst));
}

extern struct dss_module_key obj_module_key;
enum obj_profile_op {
	OBJ_PF_UPDATE_PREP = 0,
	OBJ_PF_UPDATE_DISPATCH,
	OBJ_PF_UPDATE_LOCAL,
	OBJ_PF_UPDATE_END,
	OBJ_PF_UPDATE_WAIT,
	OBJ_PF_UPDATE_REPLY,
	OBJ_PF_UPDATE,
};

struct obj_tls {
	d_sg_list_t		ot_echo_sgl;
	struct srv_profile	*ot_sp;
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
	uint64_t		 epoch;
	uint32_t		 shard;
	uint32_t		 target;
	uint32_t		 map_ver;
	uint16_t		 flags;
	/* group index within the req_tgts->ort_shard_tgts */
	uint16_t		 grp_idx;
	/* only for EC, the start shard of the EC stripe */
	uint32_t		 start_shard;
};

struct shard_rw_args {
	struct shard_auxi_args	 auxi;
	daos_obj_rw_t		*api_args;
	struct dtx_id		 dti;
	uint64_t		 dkey_hash;
	crt_bulk_t		*bulks;
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
};

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
	daos_epoch_range_t		 eprs[DOVA_NUM];
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
	struct dc_obj_verify_cursor	 cursor;
};

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

int dc_obj_shard_query_key(struct dc_obj_shard *shard, daos_epoch_t epoch,
			   uint32_t flags, daos_key_t *dkey, daos_key_t *akey,
			   daos_recx_t *recx, const uuid_t coh_uuid,
			   const uuid_t cont_uuid, unsigned int *map_ver,
			   tse_task_t *task);

int dc_obj_shard_sync(struct dc_obj_shard *shard, enum obj_rpc_opc opc,
		      void *shard_args, struct daos_shard_tgt *fw_shard_tgts,
		      uint32_t fw_cnt, tse_task_t *task);

int dc_obj_verify_rdg(struct dc_object *obj, struct dc_obj_verify_args *dova,
		      uint32_t rdg_idx, uint32_t reps, daos_epoch_t epoch);

static inline bool
obj_retry_error(int err)
{
	return err == -DER_TIMEDOUT || err == -DER_STALE ||
	       err == -DER_INPROGRESS || daos_crt_network_error(err);
}

void obj_shard_decref(struct dc_obj_shard *shard);
void obj_shard_addref(struct dc_obj_shard *shard);
void obj_addref(struct dc_object *obj);
void obj_decref(struct dc_object *obj);
int obj_get_grp_size(struct dc_object *obj);

struct ds_obj_exec_arg {
	crt_rpc_t		*rpc;
	struct ds_cont_hdl	*cont_hdl;
	struct ds_cont_child	*cont;
	uint32_t		flags;
};

int
ds_obj_remote_update(struct dtx_leader_handle *dth, void *arg, int idx,
		     dtx_sub_comp_cb_t comp_cb);
int
ds_obj_remote_punch(struct dtx_leader_handle *dth, void *arg, int idx,
		    dtx_sub_comp_cb_t comp_cb);
/* srv_obj.c */
void ds_obj_rw_handler(crt_rpc_t *rpc);
void ds_obj_tgt_update_handler(crt_rpc_t *rpc);
void ds_obj_enum_handler(crt_rpc_t *rpc);
void ds_obj_punch_handler(crt_rpc_t *rpc);
void ds_obj_tgt_punch_handler(crt_rpc_t *rpc);
void ds_obj_query_key_handler(crt_rpc_t *rpc);
void ds_obj_sync_handler(crt_rpc_t *rpc);
ABT_pool ds_obj_abt_pool_choose_cb(crt_rpc_t *rpc, ABT_pool *pools);
typedef int (*ds_iofw_cb_t)(crt_rpc_t *req, void *arg);

static inline uint64_t
obj_dkey2hash(daos_key_t *dkey)
{
	/* return 0 for NULL dkey, for example obj punch and list dkey */
	if (dkey == NULL)
		return 0;

	return d_hash_murmur64((unsigned char *)dkey->iov_buf,
			       dkey->iov_len, 5731);
}

int  obj_utils_init(void);
void obj_utils_fini(void);

/* obj_class.c */
int obj_ec_codec_init(void);
void obj_ec_codec_fini(void);
struct obj_ec_codec *obj_ec_codec_get(daos_oclass_id_t oc_id);
int obj_encode_full_stripe(daos_obj_id_t oid, d_sg_list_t *sgl,
			   uint32_t *sg_idx, size_t *sg_off,
			   struct obj_ec_parity *parity, int p_idx);
bool
ec_mult_data_targets(uint32_t fw_cnt, daos_obj_id_t oid);

int
ec_data_target(unsigned int dtgt_idx, unsigned int nr, daos_iod_t *iods,
	       struct daos_oclass_attr *oca, struct ec_bulk_spec **skip_list);

int
ec_parity_target(unsigned int ptgt_idx, unsigned int nr, daos_iod_t *iods,
		 struct daos_oclass_attr *oca, struct ec_bulk_spec **skip_list);


int
ec_copy_iods(daos_iod_t *in, int nr, daos_iod_t **out);

/* cli_ec.c */
void
ec_get_tgt_set(daos_iod_t *iods, unsigned int nr, struct daos_oclass_attr *oca,
	       bool parify_include, uint64_t *tgt_set);

void
ec_free_iods(daos_iod_t *iods, int nr);

#endif /* __DAOS_OBJ_INTENRAL_H__ */
