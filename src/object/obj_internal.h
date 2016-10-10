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

#include <daos/common.h>
#include <daos/event.h>
#include <daos/scheduler.h>
#include <daos/placement.h>
#include <daos_api.h>

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
	/** algorithmically generated object layout */
	struct pl_obj_layout	*cob_layout;
	/** object handles of underlying DSM objects */
	daos_handle_t		*cob_mohs;
};

/* client object shard */
struct dc_obj_shard {
	/** rank of the target this object belongs to */
	daos_rank_t		do_rank;
	/** refcount */
	unsigned int		do_ref;
	/** number of service threads running on the target */
	int			do_nr_srv;
	/** object id */
	daos_unit_oid_t		do_id;
	/** container handler of the object */
	daos_handle_t		do_co_hdl;
	/** list to the container */
	daos_list_t		do_co_list;
};

int
dc_obj_shard_open(daos_handle_t coh, uint32_t tgt, daos_unit_oid_t id,
		  unsigned int mode, daos_handle_t *oh, daos_event_t *ev);

int
dc_obj_shard_close(daos_handle_t oh, daos_event_t *ev);

int
dc_obj_shard_update(daos_handle_t oh, daos_epoch_t epoch,
		    daos_dkey_t *dkey, unsigned int nr,
		    daos_vec_iod_t *iods, daos_sg_list_t *sgls,
		    struct daos_task *task);

int
dc_obj_shard_fetch(daos_handle_t oh, daos_epoch_t epoch,
		   daos_dkey_t *dkey, unsigned int nr,
		   daos_vec_iod_t *iods, daos_sg_list_t *sgls,
		   daos_vec_map_t *maps, struct daos_task *task);
int
dc_obj_shard_list_key(daos_handle_t oh, uint32_t op, daos_epoch_t epoch,
		      daos_key_t *key, uint32_t *nr, daos_key_desc_t *kds,
		      daos_sg_list_t *sgl, daos_hash_out_t *anchor,
		      struct daos_task *task);
/* srv_obj.c */
int ds_obj_rw_handler(dtp_rpc_t *rpc);
int ds_obj_enum_handler(dtp_rpc_t *rpc);

#endif /* __DAOS_OBJ_INTENRAL_H__ */
