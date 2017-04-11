/* Copyright (C) 2016 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

	map->com_pid = getpid();
	map->com_bits = bits;
	C_ALLOC(map->com_hash, sizeof(map->com_hash[0]) * (1 << bits));
	if (map->com_hash == NULL) {
		C_GOTO(out, rc = -CER_NOMEM);
	}
	for (i = 0; i < (1 << bits); i++)
		CRT_INIT_LIST_HEAD(&map->com_hash[i]);

	rc = pthread_rwlock_init(&map->com_rwlock, NULL);
	if (rc != 0) {
		C_ERROR("Failed to create mutex for CaRT opc map.\n");
		C_GOTO(out, rc = -rc);
	}

	map->com_lock_init = 1;
	crt_gdata.cg_opc_map = map;

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

	/* map = crt_gdata.cg_opc_map; */
	C_ASSERT(map != NULL);
	if (map->com_hash == NULL)
		goto skip;

	for (i = 0; i < (1 << map->com_bits); i++) {
		while (!crt_list_empty(&map->com_hash[i])) {
			info = crt_list_entry(map->com_hash[i].next,
					       struct crt_opc_info, coi_link);
			crt_list_del_init(&info->coi_link);
			/*
			C_DEBUG("deleted opc: 0x%x from map(hash %d).\n",
				info->coi_opc, i);
			*/
			C_FREE_PTR(info);
		}
	}
	C_FREE(map->com_hash, sizeof(map->com_hash[0]) * map->com_bits);

skip:
	if (map->com_lock_init && map->com_pid == getpid())
		pthread_rwlock_destroy(&map->com_rwlock);

	crt_gdata.cg_opc_map = NULL;
	C_FREE_PTR(map);
}

static inline unsigned int
crt_opc_hash(struct crt_opc_map *map, crt_opcode_t opc)
{
	C_ASSERT(map != NULL);
	return opc & ((1U << map->com_bits) - 1);
}

static inline void
crt_opc_info_init(struct crt_opc_info *info)
{
	C_ASSERT(info != NULL);
	CRT_INIT_LIST_HEAD(&info->coi_link);
	/* C_ALLOC zeroed the content already. */
	/*
	info->coi_opc = 0;
	info->coi_proc_init = 0;
	info->coi_rpccb_init = 0;
	info->coi_input_size = 0;
	info->coi_output_size = 0;
	info->coi_crf = NULL;
	*/
}

struct crt_opc_info *
crt_opc_lookup(struct crt_opc_map *map, crt_opcode_t opc, int locked)
{
	struct crt_opc_info *info = NULL;
	unsigned int         hash;

	hash = crt_opc_hash(map, opc);

	if (locked == 0)
		pthread_rwlock_rdlock(&map->com_rwlock);

	crt_list_for_each_entry(info, &map->com_hash[hash], coi_link) {
		if (info->coi_opc == opc) {
			if (locked == 0)
				pthread_rwlock_unlock(&map->com_rwlock);
			return info;
		}
		if (info->coi_opc > opc)
			break;
	}

	if (locked == 0)
		pthread_rwlock_unlock(&map->com_rwlock);

	return NULL;
}

static int
crt_opc_reg(struct crt_opc_map *map, crt_opcode_t opc,
	    struct crt_req_format *crf, crt_size_t input_size,
	    crt_size_t output_size, crt_rpc_cb_t rpc_cb,
	    struct crt_corpc_ops *co_ops, int locked)
{
	struct crt_opc_info *info = NULL, *new_info;
	unsigned int         hash;
	int                  rc = 0;

	hash = crt_opc_hash(map, opc);

	if (locked == 0)
		pthread_rwlock_wrlock(&map->com_rwlock);

	crt_list_for_each_entry(info, &map->com_hash[hash], coi_link) {
		if (info->coi_opc == opc) {
			/*
			C_DEBUG("re-reg, opc 0x%x.\n", opc);
			*/
			if (info->coi_input_size != input_size) {
				C_DEBUG("opc 0x%x, update input_size "
					"from "CF_U64" to "CF_U64".\n", opc,
					info->coi_input_size, input_size);
				info->coi_input_size = input_size;
			}
			if (info->coi_output_size != output_size) {
				C_DEBUG("opc 0x%x, update output_size "
					"from "CF_U64" to "CF_U64".\n", opc,
					info->coi_output_size, output_size);
				info->coi_output_size = output_size;
			}
			info->coi_crf = crf;
			if (rpc_cb != NULL) {
				if (info->coi_rpc_cb != NULL)
					C_DEBUG("re-reg rpc callback, "
						"opc 0x%x.\n", opc);
				else
					info->coi_rpccb_init = 1;
				info->coi_rpc_cb = rpc_cb;
			}
			if (co_ops != NULL) {
				if (info->coi_co_ops != NULL)
					C_DEBUG("re-reg co_ops, "
						"opc 0x%x.\n", opc);
				else
					info->coi_coops_init = 1;
				info->coi_co_ops = co_ops;
			}
			C_GOTO(out, rc = 0);
		}
		if (info->coi_opc > opc)
			break;
	}

	C_ALLOC_PTR(new_info);
	if (new_info == NULL)
		C_GOTO(out, rc = -CER_NOMEM);

	crt_opc_info_init(new_info);
	new_info->coi_opc = opc;
	new_info->coi_crf = crf;
	new_info->coi_input_size = input_size;
	new_info->coi_output_size = output_size;
	new_info->coi_proc_init = 1;
	if (rpc_cb != NULL) {
		new_info->coi_rpc_cb = rpc_cb;
		new_info->coi_rpccb_init = 1;
	}
	if (co_ops != NULL) {
		new_info->coi_co_ops = co_ops;
		new_info->coi_coops_init = 1;
	}
	crt_list_add_tail(&new_info->coi_link, &info->coi_link);

out:
	if (locked == 0)
		pthread_rwlock_unlock(&map->com_rwlock);
	return rc;
}

int
crt_rpc_reg_internal(crt_opcode_t opc, struct crt_req_format *crf,
		     crt_rpc_cb_t rpc_handler, struct crt_corpc_ops *co_ops)
{
	crt_size_t		input_size = 0;
	crt_size_t		output_size = 0;
	struct crt_msg_field	*cmf;
	int			rc = 0;
	int			i;

	/* when no input/output parameter needed, the crf can be NULL */
	if (crf == NULL)
		C_GOTO(reg_opc, rc);

	/* calculate the total input size and output size */
	for (i = 0; i < crf->crf_fields[CRT_IN].crf_count; i++) {
		cmf = crf->crf_fields[CRT_IN].crf_msg[i];
		C_ASSERT(cmf->cmf_size > 0);
		if (cmf->cmf_flags & CMF_ARRAY_FLAG)
			input_size += sizeof(struct crt_array);
		else
			input_size += cmf->cmf_size;
	}
	for (i = 0; i < crf->crf_fields[CRT_OUT].crf_count; i++) {
		cmf = crf->crf_fields[CRT_OUT].crf_msg[i];
		C_ASSERT(cmf->cmf_size > 0);
		if (cmf->cmf_flags & CMF_ARRAY_FLAG)
			output_size += sizeof(struct crt_array);
		else
			output_size += cmf->cmf_size;
	}

	if (input_size > CRT_MAX_INPUT_SIZE ||
	    output_size > CRT_MAX_OUTPUT_SIZE) {
		C_ERROR("input_size "CF_U64" or output_size "CF_U64" "
			"too large.\n", input_size, output_size);
		C_GOTO(out, rc = -CER_INVAL);
	}

reg_opc:
	rc = crt_opc_reg(crt_gdata.cg_opc_map, opc, crf, input_size,
			 output_size, rpc_handler, co_ops, CRT_UNLOCK);
	if (rc != 0)
		C_ERROR("rpc (opc: 0x%x) register failed, rc: %d.\n", opc, rc);

out:
	return rc;
}

int
crt_rpc_register(crt_opcode_t opc, struct crt_req_format *crf)
{
	if (crt_opcode_reserved(opc)) {
		C_ERROR("opc 0x%x reserved.\n", opc);
		return -CER_INVAL;
	}
	return crt_rpc_reg_internal(opc, crf, NULL, NULL);
}

int
crt_rpc_srv_register(crt_opcode_t opc, struct crt_req_format *crf,
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

	return crt_rpc_reg_internal(opc, crf, rpc_handler, NULL);
}

int
crt_corpc_register(crt_opcode_t opc, struct crt_req_format *crf,
		   crt_rpc_cb_t rpc_handler, struct crt_corpc_ops *co_ops)
{
	if (crt_opcode_reserved(opc)) {
		C_ERROR("opc 0x%x reserved.\n", opc);
		return -CER_INVAL;
	}
	if (co_ops == NULL) {
		C_WARN("NULL co_ops to be registered for corpc 0x%x.\n", opc);
	}

	return crt_rpc_reg_internal(opc, crf, rpc_handler, co_ops);
}
