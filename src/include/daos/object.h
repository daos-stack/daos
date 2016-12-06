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
#ifndef __DAOS_OBJECT_H__
#define __DAOS_OBJECT_H__

#include <daos_types.h>
#include <daos_api.h>
#include <daos/scheduler.h>

int dc_obj_init(void);
void dc_obj_fini(void);

/** object metadata stored in the global OI table of container */
struct daos_obj_md {
	daos_obj_id_t		omd_id;
	uint32_t		omd_ver;
	uint32_t		omd_padding;
	union {
		uint32_t	omd_split;
		uint64_t	omd_loff;
	};
};

/** object shard metadata stored in each contianer shard */
struct daos_obj_shard_md {
	/** ID of the object shard */
	daos_unit_oid_t		smd_id;
	uint32_t		smd_po_ver;
	uint32_t		smd_padding;
};

struct daos_oclass_attr *daos_oclass_attr_find(daos_obj_id_t oid);
unsigned int daos_oclass_grp_size(struct daos_oclass_attr *oc_attr);
unsigned int daos_oclass_grp_nr(struct daos_oclass_attr *oc_attr,
				struct daos_obj_md *md);

int
dc_oclass_register(daos_handle_t coh, daos_oclass_id_t cid,
		   daos_oclass_attr_t *cattr, daos_event_t *ev);
int
dc_oclass_query(daos_handle_t coh, daos_oclass_id_t cid,
		daos_oclass_attr_t *cattr, daos_event_t *ev);
int
dc_oclass_list(daos_handle_t coh, daos_oclass_list_t *clist,
	       daos_hash_out_t *anchor, daos_event_t *ev);
int
dc_obj_open(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
	    unsigned int mode, daos_handle_t *oh, daos_event_t *ev);
int
dc_obj_close(daos_handle_t oh, daos_event_t *ev);
int
dc_obj_layout_refresh(daos_handle_t oh);
int
dc_obj_punch(daos_handle_t oh, daos_epoch_t epoch, daos_event_t *ev);
int
dc_obj_query(daos_handle_t oh, daos_epoch_t epoch, daos_obj_attr_t *oa,
	     daos_rank_list_t *ranks, daos_event_t *ev);
int
dc_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
	     unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls,
	     daos_vec_map_t *maps, unsigned int map_ver,
	     struct daos_task *task);
int
dc_obj_update(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
	      unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls,
	      unsigned int map_ver, struct daos_task *task);
int
dc_obj_list_dkey(daos_handle_t oh, daos_epoch_t epoch, uint32_t *nr,
		 daos_key_desc_t *kds, daos_sg_list_t *sgl,
		 daos_hash_out_t *anchor, unsigned int map_ver,
		 struct daos_task *task);
int
dc_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
		 uint32_t *nr, daos_key_desc_t *kds, daos_sg_list_t *sgl,
		 daos_hash_out_t *anchor, unsigned int map_ver,
		 struct daos_task *task);

daos_handle_t
dc_obj_hdl2cont_hdl(daos_handle_t obj_oh);

static inline bool
daos_obj_retry_error(int err)
{
	return err == -DER_TIMEDOUT || err == -DER_STALE ||
	       daos_crt_network_error(err);
}

#endif /* __DAOS_OBJECT_H__ */
