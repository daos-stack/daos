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
/*
 * dctc: Client object operations
 *
 * dctc is the DCT client module/library. It exports the DCT API defined in
 * daos_ct.h.
 */
#define DDSUBSYS	DDFAC(tier)

#include <daos/tier.h>
#include <daos_tier.h>
#include <pthread.h>
#include <daos/rpc.h>
#include "rpc.h"

int
dc_tier_obj_declare(daos_handle_t coh, daos_obj_id_t *id, daos_epoch_t epoch,
		 daos_obj_attr_t *oa, daos_handle_t *oh, daos_event_t *ev)
{
	return 0;
}

int
dc_tier_obj_open(daos_handle_t coh, daos_obj_id_t id, daos_epoch_t epoch,
	      unsigned int mode, daos_handle_t *oh, daos_event_t *ev)
{
	return 0;
}


int
dc_tier_obj_close(daos_handle_t oh, daos_event_t *ev)
{
	return 0;
}

int
dc_tier_obj_punch(daos_handle_t oh, daos_epoch_t epoch, daos_event_t *ev)
{
	return 0;
}

int
dc_tier_obj_query(daos_handle_t oh, daos_epoch_t epoch, daos_obj_attr_t *oa,
	       d_rank_list_t *ranks, daos_event_t *ev)
{
	return 0;
}

/**
 * Object I/O API
 */

int
dc_tier_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		  unsigned int nr, daos_iod_t *iods, daos_sg_list_t *sgls,
		  daos_iom_t *maps, daos_event_t *ev)
{
	return 0;
}

int
dc_tier_obj_update(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		   unsigned int nr, daos_iod_t *iods, daos_sg_list_t *sgls,
		   daos_event_t *ev)
{
	return 0;
}


int
dc_tier_obj_list_dkey(daos_handle_t oh, daos_epoch_t epoch, uint32_t nr,
		      daos_key_desc_t *kds, daos_sg_list_t *sgl,
		      daos_hash_out_t *anchor, daos_event_t *ev)
{
	return 0;
}

int
dc_tier_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		      uint32_t nr, daos_key_desc_t *kds, daos_sg_list_t *sgl,
		      daos_hash_out_t *anchor, daos_event_t *ev)
{
	return 0;
}
