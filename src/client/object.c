/**
 * (C) Copyright 2015, 2016 Intel Corporation.
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
#define DD_SUBSYS	DD_FAC(client)

#include <daos/object.h>
#include "client_internal.h"
#include "task_internal.h"

int
daos_obj_class_register(daos_handle_t coh, daos_oclass_id_t cid,
			daos_oclass_attr_t *cattr, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
#if 0
	daos_obj_class_register_t	arg;
	tse_task_t			*task;

	arg.coh		= coh;
	arg.cid		= cid;
	arg.cattr	= cattr;

	dc_task_create(DAOS_OPC_OBJ_CLASS_REGISTER, &arg, sizeof(arg), &task,
		       &ev);
	return daos_client_result_wait(ev);
#endif
}

int
daos_obj_class_query(daos_handle_t coh, daos_oclass_id_t cid,
		     daos_oclass_attr_t *cattr, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
#if 0
	daos_obj_class_query_t	arg;
	tse_task_t		*task;

	arg.coh		= coh;
	arg.cid		= cid;
	arg.cattr	= cattr;

	dc_task_create(DAOS_OPC_OBJ_CLASS_QUERY, &arg, sizeof(arg), &task, &ev);
	return daos_client_result_wait(ev);
#endif
}

int
daos_obj_class_list(daos_handle_t coh, daos_oclass_list_t *clist,
		    daos_hash_out_t *anchor, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
#if 0
	daos_obj_class_list_t	arg;
	tse_task_t		*task;

	arg.coh		= coh;
	arg.clist	= clist;
	arg.anchor	= anchor;

	dc_task_create(DAOS_OPC_OBJ_CLASS_LIST, &arg, sizeof(arg), &task, &ev);
	return daos_client_result_wait(ev);
#endif
}

int
daos_obj_declare(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
		 daos_obj_attr_t *oa, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
#if 0
	daos_obj_declare_t	arg;
	tse_task_t		*task;

	arg.coh		= coh;
	arg.oid		= oid;
	arg.epoch	= epoch;
	arg.oa		= oa;

	dc_task_create(DAOS_OPC_OBJ_DECLARE, &arg, sizeof(arg), &task, &ev);
	return daos_client_result_wait(ev);
#endif
}

int
daos_obj_open(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
	      unsigned int mode, daos_handle_t *oh, daos_event_t *ev)
{
	daos_obj_open_t		args;
	tse_task_t		*task;

	args.coh	= coh;
	args.oid	= oid;
	args.epoch	= epoch;
	args.mode	= mode;
	args.oh		= oh;

	DAOS_API_ARG_ASSERT(args, OBJ_OPEN);

	dc_task_create(DAOS_OPC_OBJ_OPEN, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_obj_close(daos_handle_t oh, daos_event_t *ev)
{
	daos_obj_close_t	args;
	tse_task_t		*task;

	args.oh		= oh;

	DAOS_API_ARG_ASSERT(args, OBJ_CLOSE);

	dc_task_create(DAOS_OPC_OBJ_CLOSE, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_obj_punch(daos_handle_t oh, daos_epoch_t epoch, daos_event_t *ev)
{
	daos_obj_punch_t	args;
	tse_task_t		*task;

	args.epoch	= epoch;
	args.oh		= oh;

	DAOS_API_ARG_ASSERT(args, OBJ_PUNCH);

	dc_task_create(DAOS_OPC_OBJ_PUNCH, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_obj_punch_dkeys(daos_handle_t oh, daos_epoch_t epoch, unsigned int nr,
		     daos_key_t *dkeys, daos_event_t *ev)
{
	daos_obj_punch_dkeys_t	args;
	tse_task_t		*task;

	args.oh		= oh;
	args.epoch	= epoch;
	args.nr		= nr;
	args.dkeys	= dkeys;

	DAOS_API_ARG_ASSERT(args, OBJ_PUNCH_DKEYS);

	dc_task_create(DAOS_OPC_OBJ_PUNCH_DKEYS, &args, sizeof(args), &task,
		       &ev);
	return daos_client_result_wait(ev);
}

int
daos_obj_punch_akeys(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		     unsigned int nr, daos_key_t *akeys, daos_event_t *ev)
{
	daos_obj_punch_akeys_t	args;
	tse_task_t		*task;

	args.oh		= oh;
	args.epoch	= epoch;
	args.dkey	= dkey;
	args.nr		= nr;
	args.akeys	= akeys;

	DAOS_API_ARG_ASSERT(args, OBJ_PUNCH_AKEYS);

	dc_task_create(DAOS_OPC_OBJ_PUNCH_AKEYS, &args, sizeof(args), &task,
		       &ev);
	return daos_client_result_wait(ev);
}

int
daos_obj_query(daos_handle_t oh, daos_epoch_t epoch, daos_obj_attr_t *oa,
	       daos_rank_list_t *ranks, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
	       unsigned int nr, daos_iod_t *iods, daos_sg_list_t *sgls,
	       daos_iom_t *maps, daos_event_t *ev)
{
	daos_obj_fetch_t	args;
	tse_task_t		*task;

	DAOS_API_ARG_ASSERT(args, OBJ_FETCH);

	args.oh		= oh;
	args.epoch	= epoch;
	args.dkey	= dkey;
	args.nr		= nr;
	args.iods	= iods;
	args.sgls	= sgls;
	args.maps	= maps;

	dc_task_create(DAOS_OPC_OBJ_FETCH, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_obj_update(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		unsigned int nr, daos_iod_t *iods, daos_sg_list_t *sgls,
		daos_event_t *ev)
{
	daos_obj_update_t	args;
	tse_task_t		*task;

	DAOS_API_ARG_ASSERT(args, OBJ_UPDATE);

	args.oh		= oh;
	args.epoch	= epoch;
	args.dkey	= dkey;
	args.nr		= nr;
	args.iods	= iods;
	args.sgls	= sgls;

	dc_task_create(DAOS_OPC_OBJ_UPDATE, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_obj_list_dkey(daos_handle_t oh, daos_epoch_t epoch, uint32_t *nr,
		   daos_key_desc_t *kds, daos_sg_list_t *sgl,
		   daos_hash_out_t *anchor, daos_event_t *ev)
{
	daos_obj_list_dkey_t	args;
	tse_task_t		*task;

	DAOS_API_ARG_ASSERT(args, OBJ_LIST_DKEY);

	args.oh		= oh;
	args.epoch	= epoch;
	args.nr		= nr;
	args.kds	= kds;
	args.sgl	= sgl;
	args.anchor	= anchor;

	dc_task_create(DAOS_OPC_OBJ_LIST_DKEY, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		   uint32_t *nr, daos_key_desc_t *kds, daos_sg_list_t *sgl,
		   daos_hash_out_t *anchor, daos_event_t *ev)
{
	daos_obj_list_akey_t	args;
	tse_task_t		*task;

	DAOS_API_ARG_ASSERT(args, OBJ_LIST_AKEY);

	args.oh		= oh;
	args.epoch	= epoch;
	args.dkey	= dkey;
	args.nr		= nr;
	args.kds	= kds;
	args.sgl	= sgl;
	args.anchor	= anchor;

	dc_task_create(DAOS_OPC_OBJ_LIST_AKEY, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_obj_list_recx(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		   daos_key_t *akey, daos_size_t *size, uint32_t *nr,
		   daos_recx_t *recxs, daos_epoch_range_t *eprs,
		   daos_hash_out_t *anchor, bool incr_order, daos_event_t *ev)
{
	daos_obj_list_recx_t	args;
	tse_task_t		*task;

	DAOS_API_ARG_ASSERT(args, OBJ_LIST_RECX);

	args.oh		= oh;
	args.epoch	= epoch;
	args.dkey	= dkey;
	args.akey	= akey;
	args.type	= DAOS_IOD_ARRAY;
	args.size	= size;
	args.nr		= nr;
	args.recxs	= recxs;
	args.eprs	= eprs;
	args.cookies	= NULL;
	args.versions	= NULL;
	args.anchor	= anchor;
	args.incr_order = incr_order;

	dc_task_create(DAOS_OPC_OBJ_LIST_RECX, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}
