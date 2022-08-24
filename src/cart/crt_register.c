/*
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It implements the RPC register related APIs and
 * internal handling.
 */
#define D_LOGFAC	DD_FAC(rpc)

#include <semaphore.h>
#include "crt_internal.h"

static int
crt_proto_query_local(crt_opcode_t base_opc, uint32_t ver);

/* init L2, alloc 32 entries by default */
static int
crt_opc_map_L2_create(struct crt_opc_map_L2 *L2_entry)
{
	D_ALLOC_ARRAY(L2_entry->L2_map, 32);
	if (L2_entry->L2_map == NULL)
		return -DER_NOMEM;

	return DER_SUCCESS;
}

/* bits is the number of bits for protocol portion of the opcode */
int
crt_opc_map_create()
{
	struct crt_opc_map	*map;
	uint32_t		count;
	int			rc;
	int			i;
	unsigned int		bits = 8;

	D_ALLOC_PTR(map);
	if (map == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&map->com_coq_list);

	count = ~(0xFFFFFFFFUL >> bits << bits) + 1;
	D_ALLOC_ARRAY(map->com_map, count);
	if (map->com_map == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	map->com_num_slots_total = count;
	/* initialize the first 16 entries. Grow on demand */
	for (i = 0; i < 16; i++) {
		rc = crt_opc_map_L2_create(&map->com_map[i]);
		if (rc != DER_SUCCESS) {
			D_ERROR("crt_opc_map_L2_create() failed, " DF_RC "\n",
				DP_RC(rc));
			D_GOTO(out, rc);
		}
	}

	rc = D_RWLOCK_INIT(&map->com_rwlock, NULL);
	if (rc != 0) {
		D_ERROR("Failed to create mutex for CaRT opc map.\n");
		D_GOTO(out, rc);
	}

	crt_gdata.cg_opc_map = map;

out:
	if (rc != 0)
		crt_opc_map_destroy(map);
	return rc;
}

static void
crt_opc_map_L3_destroy(struct crt_opc_map_L3 *L3_entry)
{
	if (L3_entry == NULL)
		return;

	L3_entry->L3_num_slots_total = 0;
	L3_entry->L3_num_slots_used = 0;
	if (L3_entry->L3_map)
		D_FREE(L3_entry->L3_map);
}

static void
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
crt_opc_map_destroy(struct crt_opc_map *map)
{
	struct crt_opc_queried *coq, *next;
	int			i;

	D_ASSERT(map != NULL);

	if (map->com_map == NULL) {
		D_DEBUG(DB_TRACE, "opc map empty, skipping.\n");
		D_GOTO(skip, 0);
	}

	for (i = 0; i < map->com_num_slots_total; i++)
		crt_opc_map_L2_destroy(&map->com_map[i]);
	D_FREE(map->com_map);

	d_list_for_each_entry_safe(coq, next, &map->com_coq_list, coq_list)
		D_FREE(coq);

skip:
	D_RWLOCK_DESTROY(&map->com_rwlock);

	crt_gdata.cg_opc_map = NULL;
	D_FREE(map);
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
	    struct crt_req_format *crf, crt_rpc_cb_t rpc_cb,
	    struct crt_corpc_ops *co_ops)
{
	size_t	size_in  = 0;
	size_t	size_out = 0;
	int	rc = 0;

	if (opc_info->coi_inited == 1) {
		D_ERROR("RPC with opcode %#x already registered\n",
			opc_info->coi_opc);
		D_GOTO(out, rc = -DER_EXIST);
	};

	opc_info->coi_opc = opc;
	opc_info->coi_crf = crf;
	opc_info->coi_proc_init = 1;
	if (rpc_cb != NULL) {
		opc_info->coi_rpc_cb = rpc_cb;
		opc_info->coi_rpccb_init = 1;
	}
	if (co_ops != NULL) {
		opc_info->coi_co_ops = co_ops;
		opc_info->coi_coops_init = 1;
	}
	if (opc_info->coi_crf != NULL) {
		size_in  = opc_info->coi_crf->crf_size_in;
		size_out = opc_info->coi_crf->crf_size_out;
	}

	opc_info->coi_inited = 1;

	/* Calculate the size required for the RPC.
	 *
	 * If crp_forward is enabled memory is only allocated for output buffer,
	 * not input so put the output buffer first and allocate input_offset
	 * bytes only if forward is set.
	 */
	opc_info->coi_output_offset = D_ALIGNUP(sizeof(struct crt_rpc_priv),
						64);
	opc_info->coi_input_offset  = D_ALIGNUP(opc_info->coi_output_offset +
						size_out, 64);
	opc_info->coi_rpc_size = sizeof(struct crt_rpc_priv) +
				 opc_info->coi_input_offset + size_in;

	/* set RPC features */
	opc_info->coi_no_reply = D_BIT_IS_SET(flags, CRT_RPC_FEAT_NO_REPLY);
	opc_info->coi_reset_timer = D_BIT_IS_SET(flags, CRT_RPC_FEAT_NO_TIMEOUT);
	opc_info->coi_queue_front = D_BIT_IS_SET(flags, CRT_RPC_FEAT_QUEUE_FRONT);

	D_DEBUG(DB_TRACE,
		"opc %#x, no_reply %s, reset_timer %s, queue_front %s\n",
		opc,
		opc_info->coi_no_reply ? "enabled" : "disabled",
		opc_info->coi_reset_timer ? "enabled" : "disabled",
		opc_info->coi_queue_front ? "enabled" : "disabled");

out:
	return rc;
}

static int
crt_opc_reg_internal(struct crt_opc_info *opc_info, crt_opcode_t opc,
		     struct crt_proto_rpc_format *prf)
{
	struct crt_req_format	*crf = prf->prf_req_fmt;
	int			 rc = 0;

	/* when no input/output parameter needed, the crf can be NULL */
	if (crf == NULL)
		D_GOTO(reg_opc, rc);

	if (crf->crf_size_in > CRT_MAX_INPUT_SIZE ||
	    crf->crf_size_out > CRT_MAX_OUTPUT_SIZE) {
		D_ERROR("input_size "DF_U64" or output_size "DF_U64" "
			"too large.\n", crf->crf_size_in, crf->crf_size_out);
		D_GOTO(out, rc = -DER_INVAL);
	}

reg_opc:
	rc = crt_opc_reg(opc_info, opc, prf->prf_flags, crf, prf->prf_hdlr,
			 prf->prf_co_ops);
	if (rc != 0)
		D_ERROR("rpc (opc: %#x) register failed, rc: %d.\n", opc, rc);

out:
	return rc;
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
		D_REALLOC_ARRAY(info_array, L3_map->L3_map,
				L3_map->L3_num_slots_total, cpf->cpf_count);
		if (info_array == NULL)
			return -DER_NOMEM;
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
		D_REALLOC_ARRAY(new_map, L2_map->L2_map,
				L2_map->L2_num_slots_total, cpf->cpf_ver + 1);
		if (new_map == NULL)
			return NULL;
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
	if (L3_map == NULL)
		return -DER_NOMEM;

	if (L3_map->L3_num_slots_total == 0)
		L2_map->L2_num_slots_used++;
	rc = crt_proto_reg_L3(L3_map, cpf);
	if (rc != 0)
		D_ERROR("crt_proto_reg_L3() failed, " DF_RC "\n", DP_RC(rc));

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
		D_ERROR("crt_proto_reg_L2() failed, " DF_RC "\n", DP_RC(rc));
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

	/* Check the list of previously queried protocols, and log a warning if
	 * a client is using a protocol without checking the version.
	 */
	if (!crt_gdata.cg_server) {
		struct crt_opc_queried	*coq;
		bool			found = false;

		/* Allow the internal protocol to be registered, it contains the
		 * proto query RPC so needs to be
		 */
		if (cpf->cpf_base == CRT_OPC_INTERNAL_BASE)
			found = true;

		d_list_for_each_entry(coq, &crt_gdata.cg_opc_map->com_coq_list,
				      coq_list) {
			if ((coq->coq_base == cpf->cpf_base) &&
			    (coq->coq_version == cpf->cpf_ver)) {
				found = true;
				break;
			}
		}

		if (!found)

			/* TODO: Make this a D_WARN and resolve client issues
			 * after the 1.2 release.
			 */
			D_DEBUG(DB_ALL, "Registering protocol without checking"
				" %s.%#x %d\n",
				cpf->cpf_name, cpf->cpf_base, cpf->cpf_ver);
	}

	/* reg L1 */
	rc = crt_proto_reg_L1(crt_gdata.cg_opc_map, cpf);
	if (rc != 0)
		D_ERROR("crt_proto_reg_L1() failed, "
			"protocol: '%s', version %u, base_opc %#x. " DF_RC "\n",
			cpf->cpf_name, cpf->cpf_ver, cpf->cpf_base, DP_RC(rc));
	else
		D_DEBUG(DB_TRACE, "registered protocol: '%s', version %u, "
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
	/* TODO: This doesn't make any sense, a XOR used as a truth value is
	 * just checking equlity so only one mask would be allowed here
	 */
	if (cpf->cpf_base ^ CRT_PROTO_BASEOPC_MASK) {
		D_ERROR("Invalid base_opc: %#x.\n", cpf->cpf_base);
		return -DER_INVAL;
	}

	return crt_proto_register_common(cpf);
}

struct proto_query_t {
	crt_proto_query_cb_t	pq_user_cb;
	void			*pq_user_arg;
	struct crt_opc_queried  *pq_coq;
};

static void
proto_query_cb(const struct crt_cb_info *cb_info)
{
	crt_rpc_t			*rpc_req = cb_info->cci_rpc;
	struct crt_proto_query_in	*rpc_req_input;
	struct crt_proto_query_out	*rpc_req_output;
	struct proto_query_t		*proto_query = cb_info->cci_arg;
	struct crt_proto_query_cb_info	 user_cb_info = {};

	user_cb_info.pq_arg = proto_query->pq_user_arg;
	rpc_req_input = crt_req_get(rpc_req);
	D_FREE(rpc_req_input->pq_ver.iov_buf);

	if (cb_info->cci_rc != 0) {
		D_ERROR("rpc (opc: %#x) failed: "DF_RC"\n", rpc_req->cr_opc,
			DP_RC(cb_info->cci_rc));
		D_GOTO(out, user_cb_info.pq_rc = cb_info->cci_rc);
	}

	rpc_req_output = crt_reply_get(rpc_req);
	user_cb_info.pq_rc = rpc_req_output->pq_rc;
	user_cb_info.pq_ver = rpc_req_output->pq_ver;

out:
	if (user_cb_info.pq_rc == 0) {
		struct crt_opc_queried *coq = proto_query->pq_coq;

		coq->coq_version = user_cb_info.pq_ver;
		d_list_add(&coq->coq_list,
			   &crt_gdata.cg_opc_map->com_coq_list);
	} else {
		D_FREE(proto_query->pq_coq);
	}

	proto_query->pq_user_cb(&user_cb_info);

	D_FREE(proto_query);
}

int
crt_proto_query_int(crt_endpoint_t *tgt_ep, crt_opcode_t base_opc, uint32_t *ver, int count,
		    crt_proto_query_cb_t cb, void *arg, crt_context_t ctx)
{
	crt_rpc_t			*rpc_req = NULL;
	crt_context_t			 crt_ctx;
	struct crt_proto_query_in	*rpc_req_input;
	struct proto_query_t		*proto_query = NULL;
	uint32_t			*tmp_array = NULL;
	int				 rc = DER_SUCCESS;

	if (ver == NULL) {
		D_ERROR("ver is NULL.\n");
		return -DER_INVAL;
	}

	if (cb == NULL)
		D_WARN("crt_proto_query() is not useful when cb is NULL.\n");

	if (ctx == NULL) {
		crt_ctx = crt_context_lookup(0);
		if (crt_ctx == NULL) {
			D_ERROR("crt_context 0 doesn't exist.\n");
			return -DER_INVAL;
		}
	} else {
		crt_ctx = ctx;
	}

	rc = crt_req_create(crt_ctx, tgt_ep, CRT_OPC_PROTO_QUERY, &rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create() failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rpc_req_input = crt_req_get(rpc_req);

	D_ALLOC_ARRAY(tmp_array, count);
	if (tmp_array == NULL)
		D_GOTO(err_rpc, rc = -DER_NOMEM);
	memcpy(tmp_array, ver, sizeof(tmp_array[0]) * count);

	/* set input */
	d_iov_set_safe(&rpc_req_input->pq_ver, tmp_array, sizeof(*ver) * count);
	rpc_req_input->pq_ver_count = count;
	rpc_req_input->pq_base_opc = base_opc;

	D_ALLOC_PTR(proto_query);
	if (proto_query == NULL)
		D_GOTO(err_rpc, rc = -DER_NOMEM);

	D_ALLOC_PTR(proto_query->pq_coq);
	if (proto_query->pq_coq == NULL)
		D_GOTO(err_rpc, rc = -DER_NOMEM);

	proto_query->pq_user_cb = cb;
	proto_query->pq_user_arg = arg;
	proto_query->pq_coq->coq_base = base_opc;

	rc = crt_req_send(rpc_req, proto_query_cb, proto_query);
	if (rc != 0)
		D_ERROR("crt_req_send() failed: "DF_RC"\n", DP_RC(rc));
	D_GOTO(out, rc);

err_rpc:
	crt_req_decref(rpc_req);
out:
	if (rc != DER_SUCCESS) {
		if (proto_query) {
			D_FREE(proto_query->pq_coq);
			D_FREE(proto_query);
		}

		D_FREE(tmp_array);
	}

	return rc;
}

int
crt_proto_query(crt_endpoint_t *tgt_ep, crt_opcode_t base_opc,
		uint32_t *ver, int count, crt_proto_query_cb_t cb, void *arg)
{
	return crt_proto_query_int(tgt_ep, base_opc, ver, count, cb, arg, NULL);
}

int
crt_proto_query_with_ctx(crt_endpoint_t *tgt_ep, crt_opcode_t base_opc, uint32_t *ver, int count,
			 crt_proto_query_cb_t cb, void *arg, crt_context_t ctx)
{
	return crt_proto_query_int(tgt_ep, base_opc, ver, count, cb, arg, ctx);
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

		if (ver >= high_ver) {
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
