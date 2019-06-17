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
#include <daos_srv/daos_server.h>
#include <daos_srv/dtx_srv.h>
#include <daos_types.h>

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
extern bool	srv_io_dispatch;
extern bool	srv_enable_dtx;

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

	unsigned int		cob_version;
	unsigned int		cob_shards_nr;
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
	daos_epoch_t		 epoch;
	struct dtx_id		 dti;
	daos_key_t		*dkey;
	uint64_t		 dkey_hash;
	uint32_t		 nr;
	daos_iod_t		*iods;
	d_sg_list_t		*sgls;
	crt_bulk_t		*bulks;
	daos_iom_t		*maps; /* only for fetch */
};

struct shard_punch_args {
	struct shard_auxi_args	 pa_auxi;
	uuid_t			 pa_coh_uuid;
	uuid_t			 pa_cont_uuid;
	daos_obj_punch_t	*pa_api_args;
	uint64_t		 pa_dkey_hash;
	daos_epoch_t		 pa_epoch;
	struct dtx_id		 pa_dti;
	uint32_t		 pa_opc;
};

int dc_obj_shard_open(struct dc_object *obj, daos_unit_oid_t id,
		      unsigned int mode, struct dc_obj_shard *shard);
void dc_obj_shard_close(struct dc_obj_shard *shard);

int dc_obj_shard_rw(struct dc_obj_shard *shard, enum obj_rpc_opc opc,
		    void *shard_args, struct daos_shard_tgt *fw_shard_tgts,
		    uint32_t fw_cnt, tse_task_t *task);

int
ec_obj_update_encode(tse_task_t *task, daos_obj_id_t oid,
		     daos_oclass_attr_t *oca, uint64_t *tgt_set);

int dc_obj_shard_punch(struct dc_obj_shard *shard, enum obj_rpc_opc opc,
		       void *shard_args,
		       struct daos_shard_tgt *fw_shard_tgts,
		       uint32_t fw_cnt, tse_task_t *task);

int
dc_obj_shard_list(struct dc_obj_shard *obj_shard, unsigned int opc,
		  daos_epoch_t epoch, daos_key_t *dkey, daos_key_t *akey,
		  daos_iod_type_t type, daos_size_t *size, uint32_t *nr,
		  daos_key_desc_t *kds, d_sg_list_t *sgl,
		  daos_recx_t *recxs, daos_epoch_range_t *eprs,
		  daos_anchor_t *anchor, daos_anchor_t  *dkey_anchor,
		  daos_anchor_t  *akey_anchor, unsigned int *map_ver,
		  tse_task_t *task);

int dc_obj_shard_query_key(struct dc_obj_shard *shard, daos_epoch_t epoch,
			   uint32_t flags, daos_key_t *dkey, daos_key_t *akey,
			   daos_recx_t *recx, const uuid_t coh_uuid,
			   const uuid_t cont_uuid, unsigned int *map_ver,
			   tse_task_t *task);

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
ds_obj_remote_update(struct dtx_handle *dth, void *arg, int idx,
		     dtx_exec_shard_comp_cb_t comp_cb, void *cb_arg);
int
ds_obj_remote_punch(struct dtx_handle *dth, void *arg, int idx,
		    dtx_exec_shard_comp_cb_t comp_cb, void *cb_arg);
/* srv_obj.c */
void ds_obj_rw_handler(crt_rpc_t *rpc);
void ds_obj_tgt_update_handler(crt_rpc_t *rpc);
void ds_obj_enum_handler(crt_rpc_t *rpc);
void ds_obj_punch_handler(crt_rpc_t *rpc);
void ds_obj_tgt_punch_handler(crt_rpc_t *rpc);
void ds_obj_query_key_handler(crt_rpc_t *rpc);
ABT_pool
ds_obj_abt_pool_choose_cb(crt_rpc_t *rpc, ABT_pool *pools);
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

/* obj_class.c */
int obj_ec_codec_init(void);
void obj_ec_codec_fini(void);
struct obj_ec_codec *obj_ec_codec_get(daos_oclass_id_t oc_id);
int obj_encode_full_stripe(daos_obj_id_t oid, d_sg_list_t *sgl,
			   uint32_t *sg_idx, size_t *sg_off,
			   struct obj_ec_parity *parity, int p_idx);

#endif /* __DAOS_OBJ_INTENRAL_H__ */
