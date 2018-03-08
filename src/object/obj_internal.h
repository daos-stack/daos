/**
 * (C) Copyright 2016 Intel Corporation.
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
#include <daos_types.h>

/**
 * This environment is mostly for performance evaluation.
 */
#define IO_BYPASS_ENV	"DAOS_IO_BYPASS"

/**
 * Bypass client I/O RPC, it means the client stack will complete the
 * fetch/update RPC immediately, nothing will be submitted to remote server.
 * This mode is for client I/O stack performance benchmark.
 */
extern bool	cli_bypass_rpc;
/**
 * Bypass bulk transfer on server side, instead data will be copy from/to
 * dummy buffer.
 * this mode is for performance evaluation on low bandwidth network.
 */
extern bool	srv_bypass_bulk;

/** client object shard */
struct dc_obj_shard {
	/** rank of the target this object belongs to */
	d_rank_t		do_rank;
	/** refcount */
	unsigned int		do_ref;
	/** number of partitions on the remote target */
	int			do_part_nr;
	/** object id */
	daos_unit_oid_t		do_id;
	/** container handler of the object */
	daos_handle_t		do_co_hdl;
	/** list to the container */
	d_list_t		do_co_list;
	/** point back to object */
	struct dc_object	*do_obj;
};

/** Client stack object */
struct dc_object {
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
	/** refcount on this object */
	unsigned int		 cob_ref;
	/** cob_spin protects cob_ref and obj_shards' do_ref */
	pthread_spinlock_t	 cob_spin;

	/* cob_lock protects layout and shard objects ptrs */
	pthread_rwlock_t	 cob_lock;
	/** algorithmically generated object layout */
	struct pl_obj_layout	*cob_layout;
	/** shard object ptrs */
	struct dc_obj_shard	**cob_obj_shards;
};

/**
 * Temporary solution for packing the tag/shard into the hash out,
 * tag stays at 25-28 bytes of daos_hash_out_t->body; shard stays
 * at 29-32 bytes of daos_hash_out_t->body; and the first 24 bytes
 * are hash key, see DAOS_HASH_HKEY_LENGTH.
 */

/* XXX This is a nasty workaround: shard is encoded in the highest
 * four bytes of the hash anchor. It is ok for now because VOS does
 * not use those bytes. We need a cleaner way to store shard index.
 */
#define ENUM_ANCHOR_TAG_OFF		24
#define ENUM_ANCHOR_TAG_LENGTH		4

/*
 * #define ENUM_ANCHOR_SHARD_OFF	28
 * #define ENUM_ANCHOR_SHARD_LENGTH	4
 */
static inline void
enum_anchor_copy_hkey(daos_hash_out_t *dst, daos_hash_out_t *src)
{
	memcpy(&dst->body[DAOS_HASH_HKEY_START],
	       &src->body[DAOS_HASH_HKEY_START], DAOS_HASH_HKEY_LENGTH);
}

static inline void
enum_anchor_reset_hkey(daos_hash_out_t *hash_out)
{
	memset(&hash_out->body[DAOS_HASH_HKEY_START], 0,
	       DAOS_HASH_HKEY_LENGTH);
}

static inline uint32_t
enum_anchor_get_tag(daos_hash_out_t *anchor)
{
	uint32_t tag;

	D_CASSERT(DAOS_HASH_HKEY_START + DAOS_HASH_HKEY_LENGTH <=
		  ENUM_ANCHOR_TAG_OFF);
	D_CASSERT(DAOS_HASH_HKEY_LENGTH + ENUM_ANCHOR_TAG_LENGTH +
		  ENUM_ANCHOR_SHARD_LENGTH <= DAOS_HKEY_MAX);

	memcpy(&tag, &anchor->body[ENUM_ANCHOR_TAG_OFF],
	       ENUM_ANCHOR_TAG_LENGTH);

	return tag;
}

static inline void
enum_anchor_set_tag(daos_hash_out_t *anchor, uint32_t tag)
{
	memcpy(&anchor->body[ENUM_ANCHOR_TAG_OFF], &tag,
	       ENUM_ANCHOR_TAG_LENGTH);
}

extern struct dss_module_key obj_module_key;
struct obj_tls {
	d_sg_list_t	ot_echo_sgl;
};

int dc_obj_shard_open(struct dc_object *obj, uint32_t tgt, daos_unit_oid_t id,
		      unsigned int mode, struct dc_obj_shard **shard);
void dc_obj_shard_close(struct dc_obj_shard *shard);

int dc_obj_shard_update(struct dc_obj_shard *shard, daos_epoch_t epoch,
			daos_key_t *dkey, unsigned int nr,
			daos_iod_t *iods, daos_sg_list_t *sgls,
			unsigned int map_ver, tse_task_t *task);
int dc_obj_shard_fetch(struct dc_obj_shard *shard, daos_epoch_t epoch,
		       daos_key_t *dkey, unsigned int nr,
		       daos_iod_t *iods, daos_sg_list_t *sgls,
		       daos_iom_t *maps, unsigned int map_ver,
		       tse_task_t *task);
int dc_obj_shard_list_key(struct dc_obj_shard *shard, uint32_t op,
			  daos_epoch_t epoch, daos_key_t *key, uint32_t *nr,
			  daos_key_desc_t *kds, daos_sg_list_t *sgl,
			  daos_hash_out_t *anchor, unsigned int map_ver,
			  tse_task_t *task);
int dc_obj_shard_list_rec(struct dc_obj_shard *shard, uint32_t op,
		      daos_epoch_t epoch, daos_key_t *dkey,
		      daos_key_t *akey, daos_iod_type_t type,
		      daos_size_t *size, uint32_t *nr,
		      daos_recx_t *recxs, daos_epoch_range_t *eprs,
		      uuid_t *cookies, uint32_t *versions,
		      daos_hash_out_t *anchor, unsigned int map_ver,
		      bool incr_order, tse_task_t *task);

static inline bool
obj_retry_error(int err)
{
	return err == -DER_TIMEDOUT || err == -DER_STALE ||
	       daos_crt_network_error(err);
}

/**
 * Task Arguments for key punch
 */
struct tsa_obj_punch {
	uint32_t		 pa_opc;
	uint32_t		 pa_mapv;
	uuid_t			 pa_coh_uuid;
	uuid_t			 pa_cont_uuid;
	struct dc_obj_shard	*pa_shard;
	daos_obj_punch_t	*pa_api_args;
	struct dc_object	*pa_obj;
	crt_rpc_t		*pa_rpc;
};

int dc_shard_punch(tse_task_t *task);

void obj_shard_decref(struct dc_obj_shard *shard);
void obj_shard_addref(struct dc_obj_shard *shard);
void obj_addref(struct dc_object *obj);
void obj_decref(struct dc_object *obj);

/* srv_obj.c */
void ds_obj_rw_handler(crt_rpc_t *rpc);
void ds_obj_enum_handler(crt_rpc_t *rpc);
void ds_obj_punch_handler(crt_rpc_t *rpc);

ABT_pool
ds_obj_abt_pool_choose_cb(crt_rpc_t *rpc, ABT_pool *pools);

#endif /* __DAOS_OBJ_INTENRAL_H__ */
