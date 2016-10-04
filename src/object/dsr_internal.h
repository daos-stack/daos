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
 * src/dsr/dsr_internal.h
 */
#ifndef __DSR_INTENRAL_H__
#define __DSR_INTENRAL_H__

#include <daos/common.h>
#include <daos/event.h>
#include <daos_sr.h>
#include "placement.h"
#include "dsr_types.h"

/* hhash table for all of objects on daos client,
 * pool, container, object etc
 **/
extern struct daos_hhash *dsr_shard_hhash;

struct daos_oclass_attr *dsr_oclass_attr_find(daos_obj_id_t oid);
int dsr_oclass_grp_size(struct daos_oclass_attr *oc_attr);
int dsr_oclass_grp_nr(struct daos_oclass_attr *oc_attr, struct dsr_obj_md *md);

/* XXX These functions should be changed to support per-pool
 * placement map.
 */
void dsr_pl_map_fini(void);
int  dsr_pl_map_init(struct pool_map *po_map);
struct pl_map *dsr_pl_map_find(daos_handle_t coh, daos_obj_id_t oid);

/* dsr shard object */
struct dsr_shard_object {
	struct daos_hlink	do_hlink;

	/* rank of the target this object belongs to */
	daos_rank_t		do_rank;

	/* number of service threads running on the target */
	int			do_nr_srv;

	/* object id */
	daos_unit_oid_t		do_id;

	/* container handler of the object */
	daos_handle_t		do_co_hdl;

	/* list to the container */
	daos_list_t		do_co_list;
};

int
dsr_shard_obj_open(daos_handle_t coh, uint32_t tgt, daos_unit_oid_t id,
		   unsigned int mode, daos_handle_t *oh, daos_event_t *ev);

int
dsr_shard_obj_close(daos_handle_t oh, daos_event_t *ev);

int
dsr_shard_obj_update(daos_handle_t oh, daos_epoch_t epoch,
		     daos_dkey_t *dkey, unsigned int nr,
		     daos_vec_iod_t *iods, daos_sg_list_t *sgls,
		     daos_event_t *ev);

int
dsr_shard_obj_fetch(daos_handle_t oh, daos_epoch_t epoch,
		    daos_dkey_t *dkey, unsigned int nr,
		    daos_vec_iod_t *iods, daos_sg_list_t *sgls,
		    daos_vec_map_t *maps, daos_event_t *ev);

int
dsr_shard_obj_list_dkey(daos_handle_t oh, daos_epoch_t epoch,
			uint32_t *nr, daos_key_desc_t *kds,
			daos_sg_list_t *sgl, daos_hash_out_t *anchor,
			daos_event_t *ev);

/**
 * Temporary solution for packing the tag into the hash out,
 * which will stay at 25-28 bytes of daos_hash_out_t->body
 */
#define DAOS_HASH_DSM_TAG_OFFSET 24
#define DAOS_HASH_DSM_TAG_LENGTH 4

static inline void
dsr_hash_hkey_copy(daos_hash_out_t *dst, daos_hash_out_t *src)
{
	memcpy(&dst->body[DAOS_HASH_HKEY_START],
	       &src->body[DAOS_HASH_HKEY_START],
	       DAOS_HASH_HKEY_LENGTH);
}

static inline void
dsr_hash_set_start(daos_hash_out_t *hash_out)
{
	memset(&hash_out->body[DAOS_HASH_HKEY_START], 0,
	       DAOS_HASH_HKEY_LENGTH);
}

static inline uint32_t
dsr_hash_get_tag(daos_hash_out_t *anchor)
{
	uint32_t tag;

	D_CASSERT(DAOS_HASH_HKEY_START + DAOS_HASH_HKEY_LENGTH <
		  DAOS_HASH_DSM_TAG_OFFSET);
	memcpy(&tag, &anchor->body[DAOS_HASH_DSM_TAG_OFFSET],
		     DAOS_HASH_DSM_TAG_LENGTH);
	return tag;
}

static inline void
dsr_hash_set_tag(daos_hash_out_t *anchor, uint32_t tag)
{
	memcpy(&anchor->body[DAOS_HASH_DSM_TAG_OFFSET], &tag,
	       DAOS_HASH_DSM_TAG_LENGTH);
}

/* dsrs_object.c */
int dsrs_hdlr_object_rw(dtp_rpc_t *rpc);
int dsrs_hdlr_object_enumerate(dtp_rpc_t *rpc);


#endif /* __DSR_INTENRAL_H__ */
