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
 * This file is part of CaRT. It implements the RPC register related APIs and
 * internal handling.
 */

#include <crt_internal.h>

int
crt_opc_map_create(unsigned int bits)
{
	struct crt_opc_map    *map = NULL;
	int                   rc = 0, i;

	C_ALLOC_PTR(map);
	if (map == NULL)
		return -CER_NOMEM;

	map->dom_pid = getpid();
	map->dom_bits = bits;
	C_ALLOC(map->dom_hash, sizeof(map->dom_hash[0]) * (1 << bits));
	if (map->dom_hash == NULL) {
		C_GOTO(out, rc = -CER_NOMEM);
	}
	for (i = 0; i < (1 << bits); i++)
		CRT_INIT_LIST_HEAD(&map->dom_hash[i]);

	rc = pthread_rwlock_init(&map->dom_rwlock, NULL);
	if (rc != 0) {
		C_ERROR("Failed to create mutex for CaRT opc map.\n");
		C_GOTO(out, rc = -rc);
	}

	map->dom_lock_init = 1;
	crt_gdata.dg_opc_map = map;

	rc = crt_internal_rpc_register();
	if (rc != 0)
		C_ERROR("crt_internal_rpc_register failed, rc: %d.\n", rc);

out:
	if (rc != 0)
		crt_opc_map_destroy(map);
	return rc;
}

void
crt_opc_map_destroy(struct crt_opc_map *map)
{
	struct crt_opc_info	*info;
	int			i;

	/* map = crt_gdata.dg_opc_map; */
	C_ASSERT(map != NULL);
	if (map->dom_hash == NULL)
		goto skip;

	for (i = 0; i < (1 << map->dom_bits); i++) {
		while (!crt_list_empty(&map->dom_hash[i])) {
			info = crt_list_entry(map->dom_hash[i].next,
					       struct crt_opc_info, doi_link);
			crt_list_del_init(&info->doi_link);
			/*
			C_DEBUG(CF_TP, "deleted opc: 0x%x from map(hash %d).\n",
				info->doi_opc, i);
			*/
			C_FREE_PTR(info);
		}
	}
	C_FREE(map->dom_hash, sizeof(map->dom_hash[0]) * map->dom_bits);

skip:
	if (map->dom_lock_init && map->dom_pid == getpid())
		pthread_rwlock_destroy(&map->dom_rwlock);

	crt_gdata.dg_opc_map = NULL;
	C_FREE_PTR(map);
}

static inline unsigned int
crt_opc_hash(struct crt_opc_map *map, crt_opcode_t opc)
{
	C_ASSERT(map != NULL);
	return opc & ((1U << map->dom_bits) - 1);
}

static inline void
crt_opc_info_init(struct crt_opc_info *info)
{
	C_ASSERT(info != NULL);
	CRT_INIT_LIST_HEAD(&info->doi_link);
	/* C_ALLOC zeroed the content already. */
	/*
	info->doi_opc = 0;
	info->doi_proc_init = 0;
	info->doi_rpccb_init = 0;
	info->doi_input_size = 0;
	info->doi_output_size = 0;
	info->doi_drf = NULL;
	*/
}

struct crt_opc_info *
crt_opc_lookup(struct crt_opc_map *map, crt_opcode_t opc, int locked)
{
	struct crt_opc_info *info = NULL;
	unsigned int         hash;

	hash = crt_opc_hash(map, opc);

	if (locked == 0)
		pthread_rwlock_rdlock(&map->dom_rwlock);

	crt_list_for_each_entry(info, &map->dom_hash[hash], doi_link) {
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
crt_opc_reg(struct crt_opc_map *map, crt_opcode_t opc,
	    struct crt_req_format *drf, crt_size_t input_size,
	    crt_size_t output_size, crt_rpc_cb_t rpc_cb,
	    struct crt_corpc_ops *co_ops, int locked)
{
	struct crt_opc_info *info = NULL, *new_info;
	unsigned int         hash;
	int                  rc = 0;

	hash = crt_opc_hash(map, opc);

	if (locked == 0)
		pthread_rwlock_wrlock(&map->dom_rwlock);

	crt_list_for_each_entry(info, &map->dom_hash[hash], doi_link) {
		if (info->doi_opc == opc) {
			/*
			C_DEBUG(CF_TP, "re-reg, opc 0x%x.\n", opc);
			*/
			if (info->doi_input_size != input_size) {
				C_DEBUG(CF_TP, "opc 0x%x, update input_size "
					"from "CF_U64" to "CF_U64".\n", opc,
					info->doi_input_size, input_size);
				info->doi_input_size = input_size;
			}
			if (info->doi_output_size != output_size) {
				C_DEBUG(CF_TP, "opc 0x%x, update output_size "
					"from "CF_U64" to "CF_U64".\n", opc,
					info->doi_output_size, output_size);
				info->doi_output_size = output_size;
			}
			info->doi_drf = drf;
			if (rpc_cb != NULL) {
				if (info->doi_rpc_cb != NULL)
					C_DEBUG(CF_TP, "re-reg rpc callback, "
						"opc 0x%x.\n", opc);
				else
					info->doi_rpccb_init = 1;
				info->doi_rpc_cb = rpc_cb;
			}
			if (co_ops != NULL) {
				if (info->doi_co_ops != NULL)
					C_DEBUG(CF_TP, "re-reg co_ops, "
						"opc 0x%x.\n", opc);
				else
					info->doi_coops_init = 1;
				info->doi_co_ops = co_ops;
			}
			C_GOTO(out, rc = 0);
		}
		if (info->doi_opc > opc)
			break;
	}

	C_ALLOC_PTR(new_info);
	if (new_info == NULL)
		C_GOTO(out, rc = -CER_NOMEM);

	crt_opc_info_init(new_info);
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
	crt_list_add_tail(&new_info->doi_link, &info->doi_link);

out:
	if (locked == 0)
		pthread_rwlock_unlock(&map->dom_rwlock);
	return rc;
}

int
crt_rpc_reg_internal(crt_opcode_t opc, struct crt_req_format *drf,
		     crt_rpc_cb_t rpc_handler, struct crt_corpc_ops *co_ops)
{
	crt_size_t		input_size = 0;
	crt_size_t		output_size = 0;
	struct crt_msg_field	*dmf;
	int			rc = 0;
	int			i;

	/* when no input/output parameter needed, the drf can be NULL */
	if (drf == NULL)
		C_GOTO(reg_opc, rc);

	/* calculate the total input size and output size */
	for (i = 0; i < drf->drf_fields[CRT_IN].drf_count; i++) {
		dmf = drf->drf_fields[CRT_IN].drf_msg[i];
		C_ASSERT(dmf->dmf_size > 0);
		input_size += dmf->dmf_size;
	}
	for (i = 0; i < drf->drf_fields[CRT_OUT].drf_count; i++) {
		dmf = drf->drf_fields[CRT_OUT].drf_msg[i];
		C_ASSERT(dmf->dmf_size > 0);
		output_size += dmf->dmf_size;
	}

	if (input_size > CRT_MAX_INPUT_SIZE ||
	    output_size > CRT_MAX_OUTPUT_SIZE) {
		C_ERROR("input_size "CF_U64" or output_size "CF_U64" "
			"too large.\n", input_size, output_size);
		C_GOTO(out, rc = -CER_INVAL);
	}

reg_opc:
	rc = crt_opc_reg(crt_gdata.dg_opc_map, opc, drf, input_size,
			 output_size, rpc_handler, co_ops, CRT_UNLOCK);
	if (rc != 0)
		C_ERROR("rpc (opc: 0x%x) register failed, rc: %d.\n", opc, rc);

out:
	return rc;
}

int
crt_rpc_reg(crt_opcode_t opc, struct crt_req_format *drf)
{
	if (crt_opcode_reserved(opc)) {
		C_ERROR("opc 0x%x reserved.\n", opc);
		return -CER_INVAL;
	}
	return crt_rpc_reg_internal(opc, drf, NULL, NULL);
}

int
crt_rpc_srv_reg(crt_opcode_t opc, struct crt_req_format *drf,
		crt_rpc_cb_t rpc_handler)
{
	if (crt_opcode_reserved(opc)) {
		C_ERROR("opc 0x%x reserved.\n", opc);
		return -CER_INVAL;
	}
	if (rpc_handler == NULL) {
		C_ERROR("invalid parameter NULL rpc_handler.\n");
		return -CER_INVAL;
	}

	return crt_rpc_reg_internal(opc, drf, rpc_handler, NULL);
}

int
crt_corpc_reg(crt_opcode_t opc, struct crt_req_format *drf,
	      crt_rpc_cb_t rpc_handler, struct crt_corpc_ops *co_ops)
{
	if (crt_opcode_reserved(opc)) {
		C_ERROR("opc 0x%x reserved.\n", opc);
		return -CER_INVAL;
	}
	if (co_ops == NULL) {
		C_ERROR("invalid parameter NULL co_ops.\n");
		return -CER_INVAL;
	}

	return crt_rpc_reg_internal(opc, drf, rpc_handler, co_ops);
}
