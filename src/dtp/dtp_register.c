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
 * This file is part of daos_transport. It implements the RPC register related
 * APIs and internal handling.
 */

#include <dtp_internal.h>

int
dtp_opc_map_create(unsigned int bits)
{
	struct dtp_opc_map    *map = NULL;
	int                   rc = 0, i;

	D_ALLOC_PTR(map);
	if (map == NULL)
		return -DER_NOMEM;

	map->dom_pid = getpid();
	map->dom_bits = bits;
	D_ALLOC(map->dom_hash, sizeof(map->dom_hash[0]) * (1 << bits));
	if (map->dom_hash == NULL) {
		D_GOTO(out, rc = -DER_NOMEM);
	}
	for (i = 0; i < (1 << bits); i++)
		DAOS_INIT_LIST_HEAD(&map->dom_hash[i]);

	rc = pthread_rwlock_init(&map->dom_rwlock, NULL);
	if (rc != 0) {
		D_ERROR("Failed to create mutex for dtp opc map.\n");
		D_GOTO(out, rc = -rc);
	}

	map->dom_lock_init = 1;
	dtp_gdata.dg_opc_map = map;

	rc = dtp_internal_rpc_register();
	if (rc != 0)
		D_ERROR("dtp_internal_rpc_register failed, rc: %d.\n", rc);

out:
	if (rc != 0)
		dtp_opc_map_destroy(map);
	return rc;
}

void
dtp_opc_map_destroy(struct dtp_opc_map *map)
{
	struct dtp_opc_info	*info;
	int			i;

	/* map = dtp_gdata.dg_opc_map; */
	D_ASSERT(map != NULL);
	if (map->dom_hash == NULL)
		goto skip;

	for (i = 0; i < (1 << map->dom_bits); i++) {
		while (!daos_list_empty(&map->dom_hash[i])) {
			info = daos_list_entry(map->dom_hash[i].next,
					       struct dtp_opc_info, doi_link);
			daos_list_del_init(&info->doi_link);
			/*
			D_DEBUG(DF_TP, "deleted opc: 0x%x from map(hash %d).\n",
				info->doi_opc, i);
			*/
			D_FREE_PTR(info);
		}
	}
	D_FREE(map->dom_hash, sizeof(map->dom_hash[0]) * map->dom_bits);

skip:
	if (map->dom_lock_init && map->dom_pid == getpid())
		pthread_rwlock_destroy(&map->dom_rwlock);

	dtp_gdata.dg_opc_map = NULL;
	D_FREE_PTR(map);
}

static inline unsigned int
dtp_opc_hash(struct dtp_opc_map *map, dtp_opcode_t opc)
{
	D_ASSERT(map != NULL);
	return opc & ((1U << map->dom_bits) - 1);
}

static inline void
dtp_opc_info_init(struct dtp_opc_info *info)
{
	D_ASSERT(info != NULL);
	DAOS_INIT_LIST_HEAD(&info->doi_link);
	/* D_ALLOC zeroed the content already. */
	/*
	info->doi_opc = 0;
	info->doi_proc_init = 0;
	info->doi_rpccb_init = 0;
	info->doi_input_size = 0;
	info->doi_output_size = 0;
	info->doi_drf = NULL;
	*/
}

struct dtp_opc_info *
dtp_opc_lookup(struct dtp_opc_map *map, dtp_opcode_t opc, int locked)
{
	struct dtp_opc_info *info = NULL;
	unsigned int         hash;

	hash = dtp_opc_hash(map, opc);

	if (locked == 0)
		pthread_rwlock_rdlock(&map->dom_rwlock);

	daos_list_for_each_entry(info, &map->dom_hash[hash], doi_link) {
		if (info->doi_opc == opc) {
			if (locked == 0)
				pthread_rwlock_unlock(&map->dom_rwlock);
			return info;
		}
		if (info->doi_opc > opc)
			break;
	}

	if (locked == 0)
		pthread_rwlock_unlock(&map->dom_rwlock);

	return NULL;
}

static int
dtp_opc_reg(struct dtp_opc_map *map, dtp_opcode_t opc,
	    struct dtp_req_format *drf, daos_size_t input_size,
	    daos_size_t output_size, dtp_rpc_cb_t rpc_cb,
	    struct dtp_corpc_ops *co_ops, int locked)
{
	struct dtp_opc_info *info = NULL, *new_info;
	unsigned int         hash;
	int                  rc = 0;

	hash = dtp_opc_hash(map, opc);

	if (locked == 0)
		pthread_rwlock_wrlock(&map->dom_rwlock);

	daos_list_for_each_entry(info, &map->dom_hash[hash], doi_link) {
		if (info->doi_opc == opc) {
			/*
			D_DEBUG(DF_TP, "re-reg, opc 0x%x.\n", opc);
			*/
			if (info->doi_input_size != input_size) {
				D_DEBUG(DF_TP, "opc 0x%x, update input_size "
					"from "DF_U64" to "DF_U64".\n", opc,
					info->doi_input_size, input_size);
				info->doi_input_size = input_size;
			}
			if (info->doi_output_size != output_size) {
				D_DEBUG(DF_TP, "opc 0x%x, update output_size "
					"from "DF_U64" to "DF_U64".\n", opc,
					info->doi_output_size, output_size);
				info->doi_output_size = output_size;
			}
			info->doi_drf = drf;
			if (rpc_cb != NULL) {
				if (info->doi_rpc_cb != NULL)
					D_DEBUG(DF_TP, "re-reg rpc callback, "
						"opc 0x%x.\n", opc);
				else
					info->doi_rpccb_init = 1;
				info->doi_rpc_cb = rpc_cb;
			}
			if (co_ops != NULL) {
				if (info->doi_co_ops != NULL)
					D_DEBUG(DF_TP, "re-reg co_ops, "
						"opc 0x%x.\n", opc);
				else
					info->doi_coops_init = 1;
				info->doi_co_ops = co_ops;
			}
			D_GOTO(out, rc = 0);
		}
		if (info->doi_opc > opc)
			break;
	}

	D_ALLOC_PTR(new_info);
	if (new_info == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dtp_opc_info_init(new_info);
	new_info->doi_opc = opc;
	new_info->doi_drf = drf;
	new_info->doi_input_size = input_size;
	new_info->doi_output_size = output_size;
	new_info->doi_proc_init = 1;
	if (rpc_cb != NULL) {
		new_info->doi_rpc_cb = rpc_cb;
		new_info->doi_rpccb_init = 1;
	}
	if (co_ops != NULL) {
		new_info->doi_co_ops = co_ops;
		new_info->doi_coops_init = 1;
	}
	daos_list_add_tail(&new_info->doi_link, &info->doi_link);

out:
	if (locked == 0)
		pthread_rwlock_unlock(&map->dom_rwlock);
	return rc;
}

int
dtp_rpc_reg_internal(dtp_opcode_t opc, struct dtp_req_format *drf,
		     dtp_rpc_cb_t rpc_handler, struct dtp_corpc_ops *co_ops)
{
	daos_size_t		input_size = 0;
	daos_size_t		output_size = 0;
	struct dtp_msg_field	*dmf;
	int			rc = 0;
	int			i;

	/* when no input/output parameter needed, the drf can be NULL */
	if (drf == NULL)
		D_GOTO(reg_opc, rc);

	/* calculate the total input size and output size */
	for (i = 0; i < drf->drf_fields[DTP_IN].drf_count; i++) {
		dmf = drf->drf_fields[DTP_IN].drf_msg[i];
		D_ASSERT(dmf->dmf_size > 0);
		if (dmf->dmf_flags & DMF_ARRAY_FLAG)
			input_size += sizeof(struct dtp_array);
		else
			input_size += dmf->dmf_size;
	}
	for (i = 0; i < drf->drf_fields[DTP_OUT].drf_count; i++) {
		dmf = drf->drf_fields[DTP_OUT].drf_msg[i];
		D_ASSERT(dmf->dmf_size > 0);
		if (dmf->dmf_flags & DMF_ARRAY_FLAG)
			output_size += sizeof(struct dtp_array);
		else
			output_size += dmf->dmf_size;
	}

	if (input_size > DTP_MAX_INPUT_SIZE ||
	    output_size > DTP_MAX_OUTPUT_SIZE) {
		D_ERROR("input_size "DF_U64" or output_size "DF_U64" "
			"too large.\n", input_size, output_size);
		D_GOTO(out, rc = -DER_INVAL);
	}

reg_opc:
	rc = dtp_opc_reg(dtp_gdata.dg_opc_map, opc, drf, input_size,
			 output_size, rpc_handler, co_ops, DTP_UNLOCK);
	if (rc != 0)
		D_ERROR("rpc (opc: 0x%x) register failed, rc: %d.\n", opc, rc);

out:
	return rc;
}

int
dtp_rpc_reg(dtp_opcode_t opc, struct dtp_req_format *drf)
{
	if (dtp_opcode_reserved(opc)) {
		D_ERROR("opc 0x%x reserved.\n", opc);
		return -DER_INVAL;
	}
	return dtp_rpc_reg_internal(opc, drf, NULL, NULL);
}

int
dtp_rpc_srv_reg(dtp_opcode_t opc, struct dtp_req_format *drf,
		dtp_rpc_cb_t rpc_handler)
{
	if (dtp_opcode_reserved(opc)) {
		D_ERROR("opc 0x%x reserved.\n", opc);
		return -DER_INVAL;
	}
	if (rpc_handler == NULL) {
		D_ERROR("invalid parameter NULL rpc_handler.\n");
		return -DER_INVAL;
	}

	return dtp_rpc_reg_internal(opc, drf, rpc_handler, NULL);
}

int
dtp_corpc_reg(dtp_opcode_t opc, struct dtp_req_format *drf,
	      dtp_rpc_cb_t rpc_handler, struct dtp_corpc_ops *co_ops)
{
	if (dtp_opcode_reserved(opc)) {
		D_ERROR("opc 0x%x reserved.\n", opc);
		return -DER_INVAL;
	}
	if (co_ops == NULL) {
		D_ERROR("invalid parameter NULL co_ops.\n");
		return -DER_INVAL;
	}

	return dtp_rpc_reg_internal(opc, drf, rpc_handler, co_ops);
}
