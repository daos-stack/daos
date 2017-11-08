/**
 * (C) Copyright 2017 Intel Corporation.
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
 * rebuild: server daos api
 *
 * This file includes functions to call client daos API on the server side.
 */
#define DDSUBSYS	DDFAC(rebuild)

#include <daos_types.h>
#include <daos_errno.h>
#include <daos_event.h>
#include <daos_task.h>

#include <daos/pool.h>
#include <daos/container.h>
#include <daos/object.h>

#include <daos_srv/daos_server.h>
#include "rebuild_internal.h"

#if 0
int
ds_pool_connect(uuid_t pool_uuid, daos_handle_t *ph)
{
	daos_pool_connect_t	arg;
	int			rc;

	uuid_copy(arg.pool_uuid, pool_uuid);
	rc = dss_async_task(ds_pool_connect_cb, &arg, sizeof(arg));
	if (rc == 0)
		*ph = arg.ph;

	return rc;
}

int
ds_pool_disconnect(daos_handle_t ph)
{
	return dss_async_task(ds_pool_disconnect_cb, &ph, sizeof(ph));
}

int
ds_cont_open(daos_handle_t pool_hl, uuid_t cont_uuid,
	     daos_handle_t *cont_hl, daos_cont_info_t *cont_info)
{
	daos_cont_open_t	arg;
	int			rc;

	arg.poh = pool_hl;
	uuid_copy(arg.uuid, cont_uuid);
	arg.flags = 0;
	arg.coh = cont_hl;
	arg.cont_info = cont_info;

	rc = dss_sync_task(DAOS_OPC_CONT_OPEN, &arg, sizeof(arg));
	if (rc == 0) {
		*cont_hl = arg.cont_hdl;
		*cont_info = arg.cont_info;
	}

	return rc;
}

int
ds_cont_create(daos_handle_t pool_hl, uuid_t cont_uuid)
{
	daos_cont_create_t arg;

	arg.poh = pool_hl;
	uuid_copy(arg.cont_uuid, cont_uuid);
	return dss_sync_task(DAOS_OPC_CONT_CREATE, &arg, sizeof(arg));
}

int
ds_cont_close(daos_handle_t cont_hl)
{
	daos_cont_close_t arg;

	arg.coh = cont_hl;
	return dss_sync_task(DAOS_OPC_CONT_CLOSE, &arg, sizeof(arg));
}
#endif

int
ds_obj_open(daos_handle_t coh, daos_obj_id_t oid,
	    daos_epoch_t epoch, unsigned int mode,
	    daos_handle_t *oh)
{
	daos_obj_open_t	arg;

	arg.coh = coh;
	arg.oid = oid;
	arg.epoch = epoch;
	arg.mode = mode;
	arg.oh = oh;

	return dss_sync_task(DAOS_OPC_OBJ_OPEN, &arg, sizeof(arg),
			     DSS_POOL_PRIV_LOW_PRIORITY);
}

int
ds_obj_close(daos_handle_t obj_hl)
{
	daos_obj_close_t arg;

	arg.oh = obj_hl;

	return dss_sync_task(DAOS_OPC_OBJ_CLOSE, &arg, sizeof(arg),
			     DSS_POOL_PRIV_LOW_PRIORITY);
}

int
ds_obj_single_shard_list_dkey(daos_handle_t oh, daos_epoch_t epoch,
			      uint32_t *nr, daos_key_desc_t *kds,
			      daos_sg_list_t *sgl, daos_hash_out_t *anchor)
{
	daos_obj_list_dkey_t	arg;

	arg.oh = oh;
	arg.epoch = epoch;
	arg.nr = nr;
	arg.kds = kds;
	arg.sgl = sgl;
	arg.anchor = anchor;

	return dss_sync_task(DAOS_OPC_OBJ_SHARD_LIST_DKEY, &arg, sizeof(arg),
			     DSS_POOL_PRIV_LOW_PRIORITY);
}

int
ds_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		 uint32_t *nr, daos_key_desc_t *kds, daos_sg_list_t *sgl,
		 daos_hash_out_t *anchor)
{
	daos_obj_list_akey_t	arg;

	arg.oh		= oh;
	arg.epoch	= epoch;
	arg.dkey	= dkey;
	arg.nr		= nr;
	arg.kds		= kds;
	arg.sgl		= sgl;
	arg.anchor	= anchor;

	return dss_sync_task(DAOS_OPC_OBJ_LIST_AKEY, &arg, sizeof(arg),
			     DSS_POOL_PRIV_LOW_PRIORITY);
}

int
ds_obj_fetch(daos_handle_t oh, daos_epoch_t epoch,
	     daos_key_t *dkey, unsigned int nr,
	     daos_iod_t *iods, daos_sg_list_t *sgls,
	     daos_iom_t *maps)
{
	daos_obj_fetch_t arg;

	arg.oh = oh;
	arg.epoch = epoch;
	arg.dkey = dkey;
	arg.nr = nr;
	arg.iods = iods;
	arg.sgls = sgls;
	arg.maps = maps;

	return dss_sync_task(DAOS_OPC_OBJ_FETCH, &arg, sizeof(arg),
			     DSS_POOL_PRIV_LOW_PRIORITY);
}

int
ds_obj_list_rec(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		daos_key_t *akey, daos_iod_type_t type, daos_size_t *size,
		uint32_t *nr, daos_recx_t *recxs, daos_epoch_range_t *eprs,
		uuid_t *cookies, uint32_t *versions, daos_hash_out_t *anchor,
		bool incr)
{
	daos_obj_list_recx_t	arg;

	arg.oh = oh;
	arg.epoch = epoch;
	arg.dkey = dkey;
	arg.akey = akey;
	arg.type = type;
	arg.size = size;
	arg.nr = nr;
	arg.recxs = recxs;
	arg.eprs = eprs;
	arg.cookies = cookies;
	arg.versions = versions;
	arg.anchor = anchor;
	arg.incr_order = incr;

	return dss_sync_task(DAOS_OPC_OBJ_LIST_RECX, &arg, sizeof(arg),
			     DSS_POOL_PRIV_LOW_PRIORITY);
}
