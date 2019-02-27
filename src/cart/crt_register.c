/* Copyright (C) 2016-2018 Intel Corporation
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
#define D_LOGFAC	DD_FAC(rpc)

#include <semaphore.h>
#include "crt_internal.h"

/* init L2, alloc 32 entries by default */
static int
crt_opc_map_L2_create(struct crt_opc_map_L2 *L2_entry)
{
	D_ALLOC_ARRAY(L2_entry->L2_map, 32);
	if (L2_entry->L2_map == NULL)
		return -DER_NOMEM;

	return DER_SUCCESS;
}

int
crt_opc_map_create_legacy(unsigned int bits)
{
	struct crt_opc_map_legacy	*map = NULL;
	int				 rc = 0, i;

	D_ALLOC_PTR(map);
	if (map == NULL)
		return -DER_NOMEM;

	map->com_pid = getpid();
	map->com_bits = bits;
	D_ALLOC_ARRAY(map->com_hash, (1 << bits));
	if (map->com_hash == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	for (i = 0; i < (1 << bits); i++)
		D_INIT_LIST_HEAD(&map->com_hash[i]);

	rc = D_RWLOCK_INIT(&map->com_rwlock, NULL);
	if (rc != 0) {
		D_ERROR("Failed to create mutex for CaRT opc map.\n");
		D_GOTO(out, rc);
	}

	map->com_lock_init = 1;
	crt_gdata.cg_opc_map_legacy = map;

out:
	if (rc != 0)
		crt_opc_map_destroy_legacy(map);
	return rc;
}

/* bits is the number of bits for protocol portion of the opcode */
int
crt_opc_map_create(unsigned int bits)
{
	struct crt_opc_map	*map;
	uint32_t		 count;
	int			 rc = 0, i;

	D_ALLOC_PTR(map);
	if (map == NULL)
		return -DER_NOMEM;

	map->com_pid = getpid();
	map->com_bits = bits;

	count = ~(0xFFFFFFFFUL >> bits << bits) + 1;
	D_ALLOC_ARRAY(map->com_map, count);
	if (map->com_map == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	map->com_num_slots_total = count;
	/* initialize the first 16 entries. Grow on demand */
	for (i = 0; i < 16; i++) {
		rc = crt_opc_map_L2_create(&map->com_map[i]);
		if (rc != DER_SUCCESS) {
			D_ERROR("crt_opc_map_L2_create() failed, rc %d.\n", rc);
			D_GOTO(out, rc);
		}
	}

	rc = D_RWLOCK_INIT(&map->com_rwlock, NULL);
	if (rc != 0) {
		D_ERROR("Failed to create mutex for CaRT opc map.\n");
		D_GOTO(out, rc);
	}

	map->com_lock_init = 1;
	crt_gdata.cg_opc_map = map;

	rc = crt_internal_rpc_register();
	if (rc != 0)
		D_ERROR("crt_internal_rpc_register failed, rc: %d.\n", rc);

out:
	if (rc != 0)
		crt_opc_map_destroy(map);
	return rc;
}

void
crt_opc_map_L3_destroy(struct crt_opc_map_L3 *L3_entry)
{
	if (L3_entry == NULL)
		return;

	L3_entry->L3_num_slots_total = 0;
	L3_entry->L3_num_slots_used = 0;
	if (L3_entry->L3_map)
		D_FREE(L3_entry->L3_map);
}

void
crt_opc_map_L2_destroy(struct crt_opc_map_L2 *L2_entry)
{
	int	i;

	if (L2_entry == NULL)
		return;

	for (i = 0; i < L2_entry->L2_num_slots_total; i++)
		crt_opc_map_L3_destroy(&L2_entry->L2_map[i]);

	L2_entry->L2_num_slots_total = 0;
	L2_entry->L2_num_slots_used = 0;

	if (L2_entry->L2_map)
		D_FREE(L2_entry->L2_map);
}

void
crt_opc_map_destroy_legacy(struct crt_opc_map_legacy *map)
{
	struct crt_opc_info	*info;
	int			i;

	/* map = crt_gdata.cg_opc_map; */
	D_ASSERT(map != NULL);
	if (map->com_hash == NULL)
		D_GOTO(skip, 0);

	for (i = 0; i < (1 << map->com_bits); i++) {
		while ((info = d_list_pop_entry(&map->com_hash[i],
						struct crt_opc_info,
						coi_link))) {
			D_FREE(info);
		}
	}
	D_FREE(map->com_hash);

skip:
	if (map->com_lock_init && map->com_pid == getpid())
		D_RWLOCK_DESTROY(&map->com_rwlock);

	crt_gdata.cg_opc_map_legacy = NULL;
	D_FREE(map);
}

void
crt_opc_map_destroy(struct crt_opc_map *map)
{
	/* struct crt_opc_info	*info; */
	int			i;

	/* map = crt_gdata.cg_opc_map; */
	D_ASSERT(map != NULL);
	D_DEBUG(DB_TRACE, "inside crt_opc_map_destroy\n");
	if (map->com_map == NULL) {
		D_DEBUG(DB_TRACE, "opc map empty, skipping.\n");
		D_GOTO(skip, 0);
	}

	for (i = 0; i < map->com_num_slots_total; i++)
		crt_opc_map_L2_destroy(&map->com_map[i]);
	D_FREE(map->com_map);

skip:
	if (map->com_lock_init && map->com_pid == getpid())
		D_RWLOCK_DESTROY(&map->com_rwlock);

	crt_gdata.cg_opc_map = NULL;
	D_FREE(map);
}

/* for legacy opcode map */
static inline unsigned int
crt_opc_hash_legacy(struct crt_opc_map_legacy *map, crt_opcode_t opc)
{
	D_ASSERT(map != NULL);
	return opc & ((1U << map->com_bits) - 1);
}

static inline void
crt_opc_info_init_legacy(struct crt_opc_info *info)
{
	D_ASSERT(info != NULL);
	D_INIT_LIST_HEAD(&info->coi_link);
	/* D_ALLOC zeroed the content already. */
	/*
	info->coi_opc = 0;
	info->coi_proc_init = 0;
	info->coi_rpccb_init = 0;
	info->coi_input_size = 0;
	info->coi_output_size = 0;
	info->coi_crf = NULL;
	*/
}
/* end for legacy opcode map */

struct crt_opc_info *
crt_opc_lookup_legacy(struct crt_opc_map_legacy *map,
		      crt_opcode_t opc, int locked)
{
	struct crt_opc_info *info = NULL;
	unsigned int         hash;

	hash = crt_opc_hash_legacy(map, opc);

	if (locked == 0)
		D_RWLOCK_RDLOCK(&map->com_rwlock);

	d_list_for_each_entry(info, &map->com_hash[hash], coi_link) {
		if (info->coi_opc == opc) {
			if (locked == 0)
				D_RWLOCK_UNLOCK(&map->com_rwlock);
			return info;
		}
		if (info->coi_opc > opc)
			break;
	}

	if (locked == 0)
		D_RWLOCK_UNLOCK(&map->com_rwlock);

	return NULL;
}

static int
crt_proto_lookup(struct crt_opc_map *map, crt_opcode_t opc, int locked)
{
	unsigned int	L1_idx;
	unsigned int	L2_idx;
	int		rc = DER_SUCCESS;

	if (locked == 0)
		D_RWLOCK_RDLOCK(&map->com_rwlock);

	L1_idx = opc >> 24;
	L2_idx = (opc & CRT_PROTO_VER_MASK) >> 16;

	if (L1_idx >= map->com_num_slots_total) {
		D_ERROR("base opc %d out of range [0, 255]\n", L1_idx);
		D_GOTO(out, rc = -DER_NONEXIST);
	}

	if (map->com_map[L1_idx].L2_num_slots_total == 0) {
		D_ERROR("base opc %d not registered\n", L1_idx);
		D_GOTO(out, rc = -DER_NONEXIST);
	}

	if (L2_idx >= map->com_map[L1_idx].L2_num_slots_total) {
		D_ERROR("version number %d out of range [0, %d]\n", L2_idx,
			map->com_map[L1_idx].L2_num_slots_total - 1);
		D_GOTO(out, rc = -DER_UNREG);
	}

	if (map->com_map[L1_idx].L2_map[L2_idx].L3_num_slots_total == 0) {
		D_ERROR("version number %d has no entries\n", L2_idx);
		D_GOTO(out, rc = -DER_UNREG);
	}

out:
	if (locked == 0)
		D_RWLOCK_UNLOCK(&map->com_rwlock);

	return rc;
}

struct crt_opc_info *
crt_opc_lookup(struct crt_opc_map *map, crt_opcode_t opc, int locked)
{
	struct crt_opc_info	*info = NULL;
	unsigned int		 L1_idx;
	unsigned int		 L2_idx;
	unsigned int		 L3_idx;

	D_DEBUG(DB_ALL, "looking up opcode: %#x\n", opc);
	L1_idx = opc >> 24;
	L2_idx = (opc & CRT_PROTO_VER_MASK) >> 16;
	L3_idx = opc & CRT_PROTO_COUNT_MASK;

	if (locked == 0)
		D_RWLOCK_RDLOCK(&map->com_rwlock);

	if (L1_idx >= map->com_num_slots_total) {
		D_WARN("base opc %d out of range [0, %d]\n", L1_idx,
			map->com_num_slots_total);
		D_GOTO(out, 0);
	}
	if (L2_idx >= map->com_map[L1_idx].L2_num_slots_total) {
		D_WARN("version number %d out of range [0, %d]\n", L2_idx,
			map->com_map[L1_idx].L2_num_slots_total);
		D_GOTO(out, 0);
	}
	if (L3_idx >= map->com_map[L1_idx].L2_map[L2_idx].L3_num_slots_total) {
		D_WARN("rpc id %d out of range [0, %d]\n", L3_idx,
			map->com_map[L1_idx].L2_map[L2_idx].L3_num_slots_total);
		D_GOTO(out, 0);
	}

	info = &map->com_map[L1_idx].L2_map[L2_idx].L3_map[L3_idx];
out:
	if (locked == 0)
		D_RWLOCK_UNLOCK(&map->com_rwlock);

	return info;
}

static int
crt_opc_reg(struct crt_opc_info *opc_info, crt_opcode_t opc, uint32_t flags,
	    struct crt_req_format *crf, size_t input_size,
	    size_t output_size, crt_rpc_cb_t rpc_cb,
	    struct crt_corpc_ops *co_ops)
{
	bool		     disable_reply;
	bool		     enable_reset_timer;

	if (opc_info->coi_inited == 1) {
		if (opc_info->coi_input_size != input_size) {
			D_DEBUG(DB_TRACE, "opc %#x, update input_size "
					"from "DF_U64" to "DF_U64".\n", opc,
					opc_info->coi_input_size, input_size);
			opc_info->coi_input_size = input_size;
		}
		if (opc_info->coi_output_size != output_size) {
			D_DEBUG(DB_TRACE, "opc %#x, update output_size "
					"from "DF_U64" to "DF_U64".\n", opc,
					opc_info->coi_output_size, output_size);
			opc_info->coi_output_size = output_size;
		}
		opc_info->coi_crf = crf;
		if (rpc_cb != NULL) {
			if (opc_info->coi_rpc_cb != NULL)
				D_DEBUG(DB_TRACE, "re-reg rpc callback, "
						"opc %#x.\n", opc);
			else
				opc_info->coi_rpccb_init = 1;
			opc_info->coi_rpc_cb = rpc_cb;
		}
		if (co_ops != NULL) {
			if (opc_info->coi_co_ops != NULL)
				D_DEBUG(DB_TRACE, "re-reg co_ops, "
						"opc %#x.\n", opc);
			else
				opc_info->coi_coops_init = 1;
			opc_info->coi_co_ops = co_ops;
		}
		D_GOTO(set, 0);
	};

	opc_info->coi_opc = opc;
	opc_info->coi_crf = crf;
	opc_info->coi_input_size = input_size;
	opc_info->coi_output_size = output_size;
	opc_info->coi_proc_init = 1;
	if (rpc_cb != NULL) {
		opc_info->coi_rpc_cb = rpc_cb;
		opc_info->coi_rpccb_init = 1;
	}
	if (co_ops != NULL) {
		opc_info->coi_co_ops = co_ops;
		opc_info->coi_coops_init = 1;
	}

	opc_info->coi_inited = 1;

set:
	/* Calculate the size required for the RPC.
	 *
	 * If crp_forward is enabled memory is only allocated for output buffer,
	 * not input so put the output buffer first and allocate input_offset
	 * bytes only if forward is set.
	 */
	opc_info->coi_output_offset = D_ALIGNUP(sizeof(struct crt_rpc_priv),
					64);
	opc_info->coi_input_offset = D_ALIGNUP(opc_info->coi_output_offset +
						opc_info->coi_output_size, 64);
	opc_info->coi_rpc_size = sizeof(struct crt_rpc_priv) +
		opc_info->coi_input_offset +
		opc_info->coi_input_size;

	/* set RPC features */
	disable_reply =  flags & CRT_RPC_FEAT_NO_REPLY;
	enable_reset_timer = flags & CRT_RPC_FEAT_NO_TIMEOUT;

	opc_info->coi_no_reply = disable_reply;
	if (disable_reply)
		D_DEBUG(DB_TRACE, "opc %#x, reply disabled.\n", opc);
	else
		D_DEBUG(DB_TRACE, "opc %#x, reply enabled.\n", opc);

	opc_info->coi_reset_timer = enable_reset_timer;
	if (enable_reset_timer)
		D_DEBUG(DB_TRACE, "opc %#x, reset_timer enabled.\n", opc);
	else
		D_DEBUG(DB_TRACE, "opc %#x, reset_timer disabled.\n", opc);


	return DER_SUCCESS;
}

static int
crt_opc_reg_legacy(struct crt_opc_map_legacy *map, crt_opcode_t opc,
	    uint32_t flags,
	    struct crt_req_format *crf, size_t input_size,
	    size_t output_size, crt_rpc_cb_t rpc_cb,
	    struct crt_corpc_ops *co_ops, int locked)
{
	struct crt_opc_info *info = NULL, *new_info;
	unsigned int         hash;
	bool		     disable_reply;
	bool		     enable_reset_timer;
	int                  rc = 0;

	hash = crt_opc_hash_legacy(map, opc);

	if (locked == 0)
		D_RWLOCK_WRLOCK(&map->com_rwlock);

	d_list_for_each_entry(info, &map->com_hash[hash], coi_link) {
		if (info->coi_opc == opc) {
			/*
			D_DEBUG("re-reg, opc %#x.\n", opc);
			*/
			if (info->coi_input_size != input_size) {
				D_DEBUG(DB_TRACE, "opc %#x, update input_size "
					"from "DF_U64" to "DF_U64".\n", opc,
					info->coi_input_size, input_size);
				info->coi_input_size = input_size;
			}
			if (info->coi_output_size != output_size) {
				D_DEBUG(DB_TRACE, "opc %#x, update output_size "
					"from "DF_U64" to "DF_U64".\n", opc,
					info->coi_output_size, output_size);
				info->coi_output_size = output_size;
			}
			info->coi_crf = crf;
			if (rpc_cb != NULL) {
				if (info->coi_rpc_cb != NULL)
					D_DEBUG(DB_TRACE, "re-reg rpc callback,"
						" opc %#x.\n", opc);
				else
					info->coi_rpccb_init = 1;
				info->coi_rpc_cb = rpc_cb;
			}
			if (co_ops != NULL) {
				if (info->coi_co_ops != NULL)
					D_DEBUG(DB_TRACE, "re-reg co_ops, "
						"opc %#x.\n", opc);
				else
					info->coi_coops_init = 1;
				info->coi_co_ops = co_ops;
			}
			new_info = info;
			D_GOTO(set, rc = 0);
		}
		if (info->coi_opc > opc)
			break;
	}

	D_ALLOC_PTR(new_info);
	if (new_info == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	crt_opc_info_init_legacy(new_info);
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
	d_list_add_tail(&new_info->coi_link, &info->coi_link);

set:
	/* Calculate the size required for the RPC.
	 *
	 * If crp_forward is enabled memory is only allocated for output buffer,
	 * not input so put the output buffer first and allocate input_offset
	 * bytes only if forward is set.
	 */
	new_info->coi_output_offset = D_ALIGNUP(sizeof(struct crt_rpc_priv),
					64);
	new_info->coi_input_offset = D_ALIGNUP(new_info->coi_output_offset +
						new_info->coi_output_size, 64);
	new_info->coi_rpc_size = sizeof(struct crt_rpc_priv) +
		new_info->coi_input_offset +
		new_info->coi_input_size;

	/* set RPC features */
	disable_reply =  flags & CRT_RPC_FEAT_NO_REPLY;
	enable_reset_timer = flags & CRT_RPC_FEAT_NO_TIMEOUT;

	new_info->coi_no_reply = disable_reply;
	if (disable_reply)
		D_DEBUG(DB_TRACE, "opc %#x, reply disabled.\n", opc);
	else
		D_DEBUG(DB_TRACE, "opc %#x, reply enabled.\n", opc);

	new_info->coi_reset_timer = enable_reset_timer;
	if (enable_reset_timer)
		D_DEBUG(DB_TRACE, "opc %#x, reset_timer enabled.\n", opc);
	else
		D_DEBUG(DB_TRACE, "opc %#x, reset_timer disabled.\n", opc);


out:
	if (locked == 0)
		D_RWLOCK_UNLOCK(&map->com_rwlock);
	return rc;
}

static int
crt_rpc_reg_internal_legacy(crt_opcode_t opc, uint32_t flags,
		     struct crt_req_format *crf,
		     crt_rpc_cb_t rpc_handler,
		     struct crt_corpc_ops *co_ops)
{
	size_t			 input_size = 0;
	size_t			 output_size = 0;
	struct crt_msg_field	*cmf;
	int			 rc = 0;
	int			 i;

	/* when no input/output parameter needed, the crf can be NULL */
	if (crf == NULL)
		D_GOTO(reg_opc, rc);

	/* calculate the total input size and output size */
	for (i = 0; i < crf->crf_in.crf_count; i++) {
		cmf = crf->crf_in.crf_msg[i];
		D_ASSERT(cmf->cmf_size > 0);
		if (cmf->cmf_flags & CMF_ARRAY_FLAG)
			input_size += sizeof(struct crt_array);
		else
			input_size += cmf->cmf_size;
	}
	for (i = 0; i < crf->crf_out.crf_count; i++) {
		cmf = crf->crf_out.crf_msg[i];
		D_ASSERT(cmf->cmf_size > 0);
		if (cmf->cmf_flags & CMF_ARRAY_FLAG)
			output_size += sizeof(struct crt_array);
		else
			output_size += cmf->cmf_size;
	}

	if (input_size > CRT_MAX_INPUT_SIZE ||
	    output_size > CRT_MAX_OUTPUT_SIZE) {
		D_ERROR("input_size "DF_U64" or output_size "DF_U64" "
			"too large.\n", input_size, output_size);
		D_GOTO(out, rc = -DER_INVAL);
	}

reg_opc:
	rc = crt_opc_reg_legacy(crt_gdata.cg_opc_map_legacy, opc, flags, crf,
				input_size, output_size, rpc_handler, co_ops,
				CRT_UNLOCK);
	if (rc != 0)
		D_ERROR("rpc (opc: %#x) register failed, rc: %d.\n", opc, rc);

out:
	return rc;
}

static int
crt_opc_reg_internal(struct crt_opc_info *opc_info, crt_opcode_t opc,
		struct crt_proto_rpc_format *prf)
{
	struct crt_req_format	*crf = prf->prf_req_fmt;
	size_t			 input_size = 0;
	size_t			 output_size = 0;
	struct crt_msg_field	*cmf;
	int			 rc = 0;
	int			 i;

	/* when no input/output parameter needed, the crf can be NULL */
	if (crf == NULL)
		D_GOTO(reg_opc, rc);

	/* calculate the total input size and output size */
	for (i = 0; i < crf->crf_in.crf_count; i++) {
		cmf = crf->crf_in.crf_msg[i];
		D_ASSERT(cmf->cmf_size > 0);
		if (cmf->cmf_flags & CMF_ARRAY_FLAG)
			input_size += sizeof(struct crt_array);
		else
			input_size += cmf->cmf_size;
	}
	for (i = 0; i < crf->crf_out.crf_count; i++) {
		cmf = crf->crf_out.crf_msg[i];
		D_ASSERT(cmf->cmf_size > 0);
		if (cmf->cmf_flags & CMF_ARRAY_FLAG)
			output_size += sizeof(struct crt_array);
		else
			output_size += cmf->cmf_size;
	}

	if (input_size > CRT_MAX_INPUT_SIZE ||
	    output_size > CRT_MAX_OUTPUT_SIZE) {
		D_ERROR("input_size "DF_U64" or output_size "DF_U64" "
			"too large.\n", input_size, output_size);
		D_GOTO(out, rc = -DER_INVAL);
	}

reg_opc:
	rc = crt_opc_reg(opc_info, opc, prf->prf_flags, crf, input_size,
			 output_size, prf->prf_hdlr, prf->prf_co_ops);
	if (rc != 0)
		D_ERROR("rpc (opc: %#x) register failed, rc: %d.\n", opc, rc);

out:
	return rc;
}

int
crt_rpc_register(crt_opcode_t opc, uint32_t flags, struct crt_req_format *crf)
{
	if (!crt_initialized()) {
		D_ERROR("CART library not-initialed.\n");
		return -DER_UNINIT;
	}
	if (crt_opcode_reserved_legacy(opc)) {
		D_ERROR("opc %#x reserved.\n", opc);
		return -DER_INVAL;
	}
	return crt_rpc_reg_internal_legacy(opc, flags, crf, NULL, NULL);
}

int
crt_rpc_srv_register(crt_opcode_t opc, uint32_t flags,
		     struct crt_req_format *crf,
		     crt_rpc_cb_t rpc_handler)
{
	if (!crt_initialized()) {
		D_ERROR("CART library not-initialed.\n");
		return -DER_UNINIT;
	}
	if (crt_opcode_reserved_legacy(opc)) {
		D_ERROR("opc %#x reserved.\n", opc);
		return -DER_INVAL;
	}
	if (rpc_handler == NULL) {
		D_ERROR("invalid parameter NULL rpc_handler.\n");
		return -DER_INVAL;
	}

	return crt_rpc_reg_internal_legacy(opc, flags, crf, rpc_handler, NULL);
}

int
crt_corpc_register(crt_opcode_t opc, struct crt_req_format *crf,
		   crt_rpc_cb_t rpc_handler, struct crt_corpc_ops *co_ops)
{
	if (!crt_initialized()) {
		D_ERROR("CART library not-initialed.\n");
		return -DER_UNINIT;
	}
	if (crt_opcode_reserved_legacy(opc)) {
		D_ERROR("opc %#x reserved.\n", opc);
		return -DER_INVAL;
	}
	if (co_ops == NULL)
		D_WARN("NULL co_ops to be registered for corpc %#x.\n", opc);

	return crt_rpc_reg_internal_legacy(opc, 0, crf, rpc_handler, co_ops);
}

static inline bool
validate_base_opcode(crt_opcode_t base_opc)
{
	/* only the base opc bits could be set*/
	if (base_opc == 0)
		return false;
	if (base_opc & ~CRT_PROTO_BASEOPC_MASK)
		return false;
	/* the base opc CRT_PROTO_BASEOPC_MASK is reserved for internal RPCs */
	if (base_opc == CRT_PROTO_BASEOPC_MASK)
		return false;

	return true;
}

static int
crt_proto_reg_L3(struct crt_opc_map_L3 *L3_map,
		 struct crt_proto_format *cpf)
{
	struct crt_opc_info	*info_array;
	int			 i;
	int			 rc = 0;

	D_ASSERT(L3_map != NULL);

	/* make sure array is big enough, realloc if necessary */
	if (L3_map->L3_num_slots_total < cpf->cpf_count) {

		D_REALLOC_ARRAY(info_array, L3_map->L3_map, cpf->cpf_count);
		if (info_array == NULL)
			return -DER_NOMEM;
		/* set new space to 0 */
		memset(&info_array[L3_map->L3_num_slots_total], 0,
		       (cpf->cpf_count - L3_map->L3_num_slots_total)
		       *sizeof(struct crt_opc_info));
		L3_map->L3_map = info_array;
		L3_map->L3_num_slots_total = cpf->cpf_count;
	}

	for (i = 0; i < cpf->cpf_count; i++) {
		struct crt_proto_rpc_format *prf = &cpf->cpf_prf[i];

		rc = crt_opc_reg_internal(&L3_map->L3_map[i],
					  CRT_PROTO_OPC(cpf->cpf_base,
							cpf->cpf_ver,
							i),
					  prf);
		if (rc != 0) {
			D_ERROR("crt_opc_reg_internal(opc: %#x) failed, rc %d.\n",
				CRT_PROTO_OPC(cpf->cpf_base, cpf->cpf_ver, i),
				rc);
			return rc;
		}
	}

	return rc;
}

static struct crt_opc_map_L3 *
get_L3_map(struct crt_opc_map_L2 *L2_map, struct crt_proto_format *cpf)
{
	struct crt_opc_map_L3 *new_map;

	if (L2_map->L2_num_slots_total < cpf->cpf_ver + 1) {
		D_REALLOC_ARRAY(new_map, L2_map->L2_map, (cpf->cpf_ver + 1));
		if (new_map == NULL) {
			D_ERROR("not enough memory.\n");
			return NULL;
		}
		memset(&new_map[L2_map->L2_num_slots_total], 0,
		       (cpf->cpf_ver + 1 - L2_map->L2_num_slots_total)
		       *sizeof(struct crt_opc_map_L3));
		L2_map->L2_map = new_map;
		L2_map->L2_num_slots_total = cpf->cpf_ver + 1;
	}
	return &L2_map->L2_map[cpf->cpf_ver];
}

static int
crt_proto_reg_L2(struct crt_opc_map_L2 *L2_map,
		 struct crt_proto_format *cpf)
{
	struct crt_opc_map_L3	*L3_map;
	int			 rc;

	D_ASSERT(L2_map != NULL);

	/* get entry pointer, realloc if array not big enough */
	L3_map = get_L3_map(L2_map, cpf);
	if (L3_map == NULL) {
		D_ERROR("get_L3_map() failed.\n");
		return -DER_NOMEM;
	}

	if (L3_map->L3_num_slots_total == 0)
		L2_map->L2_num_slots_used++;
	rc = crt_proto_reg_L3(L3_map, cpf);
	if (rc != 0)
		D_ERROR("crt_proto_reg_L3() failed, rc: %d.\n", rc);

	return rc;
}


static int
crt_proto_reg_L1(struct crt_opc_map *map, struct crt_proto_format *cpf)
{
	struct crt_opc_map_L2	*L2_map;
	int			 index;
	int			 rc;

	D_ASSERT(map != NULL);

	index = cpf->cpf_base >> 24;
	D_ASSERT(index >= 0 && index < map->com_num_slots_total);

	D_RWLOCK_WRLOCK(&map->com_rwlock);
	L2_map = &map->com_map[index];
	D_ASSERT(L2_map != NULL);

	rc = crt_proto_reg_L2(L2_map, cpf);
	if (rc != 0)
		D_ERROR("crt_proto_reg_L2() failed, rc: %d.\n", rc);
	D_RWLOCK_UNLOCK(&map->com_rwlock);

	return rc;
}

static int
crt_proto_register_common(struct crt_proto_format *cpf)
{
	int rc;

	if (cpf->cpf_ver > CRT_PROTO_MAX_VER) {
		D_ERROR("Invalid version number %d, max version number is "
			"%lu.\n", cpf->cpf_ver, CRT_PROTO_MAX_VER);
		return -DER_INVAL;
	}

	if (cpf->cpf_count > CRT_PROTO_MAX_COUNT) {
		D_ERROR("Invalid member RPC count %d, max count is %lu.\n",
			cpf->cpf_count, CRT_PROTO_MAX_COUNT);
		return -DER_INVAL;
	}

	if (cpf->cpf_count == 0) {
		D_ERROR("Invalid member RPC count %d\n",
			cpf->cpf_count);
		return -DER_INVAL;
	}

	if (cpf->cpf_prf == NULL) {
		D_ERROR("prf can't be NULL\n");
		return -DER_INVAL;
	}

	/* reg L1 */
	rc = crt_proto_reg_L1(crt_gdata.cg_opc_map, cpf);
	if (rc != 0)
		D_ERROR("crt_proto_reg_L1() failed, "
				"protocol: %s, version %u, base_opc %#x.\n",
				cpf->cpf_name, cpf->cpf_ver, cpf->cpf_base);
	else
		D_DEBUG(DB_TRACE, "registered protocol: %s, version %u, "
			"base_opc %#x.\n",
			cpf->cpf_name, cpf->cpf_ver, cpf->cpf_base);

	return rc;
}

int
crt_proto_register(struct crt_proto_format *cpf)
{
	if (cpf == NULL) {
		D_ERROR("cpf can't be NULL.\n");
		return -DER_INVAL;
	}

	/* validate base_opc is in range */
	if (!validate_base_opcode(cpf->cpf_base)) {
		D_ERROR("Invalid base_opc: %#x.\n", cpf->cpf_base);
		return -DER_INVAL;
	}

	return crt_proto_register_common(cpf);
}

int
crt_proto_register_internal(struct crt_proto_format *cpf)
{
	if (cpf == NULL) {
		D_ERROR("cpf can't be NULL.\n");
		return -DER_INVAL;
	}

	/* validate base_opc is in range */
	if (cpf->cpf_base ^ CRT_PROTO_BASEOPC_MASK) {
		D_ERROR("Invalid base_opc: %#x.\n", cpf->cpf_base);
		return -DER_INVAL;
	}

	return crt_proto_register_common(cpf);
}

struct proto_query_t {
	crt_proto_query_cb_t	 pq_user_cb;
	void			*pq_user_arg;
};

static void
proto_query_cb(const struct crt_cb_info *cb_info)
{
	crt_rpc_t			*rpc_req = cb_info->cci_rpc;
	struct crt_proto_query_in	*rpc_req_input;
	struct crt_proto_query_out	*rpc_req_output;
	struct proto_query_t		*proto_query = cb_info->cci_arg;
	struct crt_proto_query_cb_info	 user_cb_info;

	if (cb_info->cci_rc != 0) {
		D_ERROR("rpc (opc: %#x failed, rc: %d.\n", rpc_req->cr_opc,
			cb_info->cci_rc);
		D_GOTO(out, user_cb_info.pq_rc = cb_info->cci_rc);
	}

	rpc_req_input = crt_req_get(rpc_req);
	D_FREE(rpc_req_input->pq_ver.iov_buf);

	rpc_req_output = crt_reply_get(rpc_req);
	user_cb_info.pq_rc = rpc_req_output->pq_rc;
	user_cb_info.pq_ver = rpc_req_output->pq_ver;
	user_cb_info.pq_arg = proto_query->pq_user_arg;

out:
	if (proto_query->pq_user_cb)
		proto_query->pq_user_cb(&user_cb_info);

	D_FREE(proto_query);
}

int
crt_proto_query(crt_endpoint_t *tgt_ep, crt_opcode_t base_opc,
		uint32_t *ver, int count, crt_proto_query_cb_t cb, void *arg)
{
	crt_rpc_t			*rpc_req;
	crt_context_t			 crt_ctx;
	struct crt_proto_query_in	*rpc_req_input;
	struct proto_query_t		*proto_query = NULL;
	uint32_t			*tmp_array;
	int				 rc = DER_SUCCESS;

	if (ver == NULL) {
		D_ERROR("ver is NULL.\n");
		return -DER_INVAL;
	}

	if (cb == NULL)
		D_WARN("crt_proto_query() is not useful when cb is NULL.\n");

	crt_ctx = crt_context_lookup(0);
	if (crt_ctx == NULL) {
		D_ERROR("crt_context 0 doesn't exist.\n");
		return -DER_INVAL;
	}

	rc = crt_req_create(crt_ctx, tgt_ep, CRT_OPC_PROTO_QUERY, &rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create() failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}

	rpc_req_input = crt_req_get(rpc_req);

	D_ALLOC(tmp_array, sizeof(*tmp_array)*count);
	if (tmp_array == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	memcpy(tmp_array, ver, sizeof(tmp_array[0])*count);

	/* set input */
	d_iov_set_safe(&rpc_req_input->pq_ver, tmp_array, sizeof(*ver) * count);
	rpc_req_input->pq_ver_count = count;
	rpc_req_input->pq_base_opc = base_opc;

	D_ALLOC_PTR(proto_query);
	if (proto_query == NULL)
		return -DER_NOMEM;

	proto_query->pq_user_cb = cb;
	proto_query->pq_user_arg = arg;

	rc = crt_req_send(rpc_req, proto_query_cb, proto_query);
	if (rc != 0)
		D_ERROR("crt_req_send() failed, rc: %d.\n", rc);

out:
	if (rc != DER_SUCCESS)
		D_FREE(proto_query);

	return rc;
}

/* local operation, query if base_opc with version number ver is registered. */
static int
crt_proto_query_local(crt_opcode_t base_opc, uint32_t ver)
{
	crt_opcode_t		 opc;

	opc = CRT_PROTO_OPC(base_opc, ver, 0);

	return crt_proto_lookup(crt_gdata.cg_opc_map, opc, CRT_LOCKED);
}

void
crt_hdlr_proto_query(crt_rpc_t *rpc_req)
{
	struct crt_proto_query_in	*rpc_req_input;
	struct crt_proto_query_out	*rpc_req_output;
	uint32_t			*version_array;
	int				 count;
	int				 i;
	uint32_t			 high_ver = 0;
	int				 rc_tmp = -DER_NONEXIST;
	int				 rc = -DER_NONEXIST;

	rpc_req_input = crt_req_get(rpc_req);
	rpc_req_output = crt_reply_get(rpc_req);

	version_array = rpc_req_input->pq_ver.iov_buf;
	D_ASSERT(version_array != NULL);
	count = rpc_req_input->pq_ver_count;

	D_RWLOCK_RDLOCK(&crt_gdata.cg_opc_map->com_rwlock);
	for (i = 0; i < count; i++) {
		uint32_t ver = version_array[i];

		if (ver < high_ver)
			continue;
		rc_tmp = crt_proto_query_local(rpc_req_input->pq_base_opc, ver);
		if (rc_tmp != DER_SUCCESS)
			continue;

		if (ver > high_ver) {
			high_ver = ver;
			rc = DER_SUCCESS;
		}
	}
	D_RWLOCK_UNLOCK(&crt_gdata.cg_opc_map->com_rwlock);

	if (rc != DER_SUCCESS)
		rc = rc_tmp;

	D_DEBUG(DB_TRACE, "high_ver %u\n", high_ver);
	rpc_req_output->pq_ver = high_ver;
	rpc_req_output->pq_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send() failed, rc: %d\n", rc);
}
