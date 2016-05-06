/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * This file is part of daos_transport. It implements the main input/output
 * parameter serialization/de-serialization routines (proc functions).
 */

#include <dtp_internal.h>

int
dtp_proc_get_op(dtp_proc_t proc, dtp_proc_op_t *proc_op)
{
	hg_proc_op_t	hg_proc_op;
	int		rc = 0;

	if (proc == NULL) {
		D_ERROR("Proc is not initilalized.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (proc_op == NULL) {
		D_ERROR("invalid parameter - NULL proc_op.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	hg_proc_op = hg_proc_get_op(proc);
	switch (hg_proc_op) {
	case HG_ENCODE:
		*proc_op = DTP_ENCODE;
		break;
	case HG_DECODE:
		*proc_op = DTP_DECODE;
		break;
	case HG_FREE:
		*proc_op = DTP_FREE;
		break;
	default:
		D_ERROR("bad hg_proc_op: %d.\n", hg_proc_op);
		rc = -DER_INVAL;
	}

out:
	return rc;
}

int
dtp_proc_memcpy(dtp_proc_t proc, void *data, daos_size_t data_size)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_memcpy(proc, data, data_size);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_int8_t(dtp_proc_t proc, int8_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_int8_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_uint8_t(dtp_proc_t proc, uint8_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_uint8_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_int16_t(dtp_proc_t proc, int16_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_int16_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_uint16_t(dtp_proc_t proc, uint16_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_uint16_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_int32_t(dtp_proc_t proc, int32_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_int32_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_uint32_t(dtp_proc_t proc, uint32_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_uint32_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_int64_t(dtp_proc_t proc, int64_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_int64_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_uint64_t(dtp_proc_t proc, uint64_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_uint64_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_bool(dtp_proc_t proc, bool *data)
{
	hg_bool_t	hg_bool;
	hg_return_t	hg_ret;

	hg_bool = (*data == false) ? 0 : 1;
	hg_ret = hg_proc_hg_bool_t(proc, &hg_bool);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_raw(dtp_proc_t proc, void *buf, daos_size_t buf_size)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_raw(proc, buf, buf_size);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_dtp_bulk_t(dtp_proc_t proc, dtp_bulk_t *bulk_hdl)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_hg_bulk_t(proc, bulk_hdl);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_dtp_string_t(dtp_proc_t proc, dtp_string_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_hg_string_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_dtp_const_string_t(dtp_proc_t proc, dtp_const_string_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_hg_const_string_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_uuid_t(dtp_proc_t proc, uuid_t *data)
{
	return dtp_proc_memcpy(proc, data, sizeof(uuid_t));
}

int
dtp_proc_daos_rank_list_t(dtp_proc_t proc, daos_rank_list_t **data)
{
	daos_rank_list_t	*rank_list;
	hg_proc_op_t		proc_op;
	uint32_t		rank_num;
	int			i, rc = 0;

	if (proc == NULL || data == NULL) {
		D_ERROR("Invalid parameter, proc: %p, data: %p.\n", proc, data);
		D_GOTO(out, rc = -DER_INVAL);
	}

	proc_op = hg_proc_get_op(proc);
	switch (proc_op) {
	case HG_ENCODE:
		rank_list = *data;
		if (rank_list == NULL) {
			rank_num = 0;
			rc = dtp_proc_uint32_t(proc, &rank_num);
			if (rc != 0)
				D_ERROR("dtp_proc_uint32_t failed, rc: %d.\n",
					rc);
			D_GOTO(out, rc);
		}

		D_ASSERT(rank_list != NULL);
		rank_num = rank_list->rl_nr.num;
		rc = dtp_proc_uint32_t(proc, &rank_num);
		if (rc != 0) {
			D_ERROR("dtp_proc_uint32_t failed, rc: %d.\n",
				rc);
			D_GOTO(out, rc = -DER_DTP_HG);
		}
		for (i = 0; i < rank_num; i++) {
			rc = dtp_proc_daos_rank_t(proc,
						  &rank_list->rl_ranks[i]);
			if (rc != 0) {
				D_ERROR("dtp_proc_daso_rank_t failed,rc: %d.\n",
					rc);
				D_GOTO(out, rc = -DER_DTP_HG);
			}
		}
		break;
	case HG_DECODE:
		rc = dtp_proc_uint32_t(proc, &rank_num);
		if (rc != 0) {
			D_ERROR("dtp_proc_uint32_t failed, rc: %d.\n",
				rc);
			D_GOTO(out, rc = -DER_DTP_HG);
		}
		if (rank_num == 0) {
			*data = NULL;
			D_GOTO(out, rc);
		}
		D_ALLOC_PTR(rank_list);
		if (rank_list == NULL) {
			D_ERROR("Cannot allocate memory for rank list.\n");
			D_GOTO(out, rc = -DER_NOMEM);
		}
		rank_list->rl_nr.num = rank_num;
		D_ALLOC(rank_list->rl_ranks, rank_num * sizeof(daos_rank_t));
		if (rank_list->rl_ranks == NULL) {
			D_ERROR("Cannot allocate memory for rl_ranks.\n");
			D_FREE_PTR(rank_list);
			D_GOTO(out, rc = -DER_NOMEM);
		}
		for (i = 0; i < rank_num; i++) {
			rc = dtp_proc_daos_rank_t(proc,
						  &rank_list->rl_ranks[i]);
			if (rc != 0) {
				D_ERROR("dtp_proc_daso_rank_t failed,rc: %d.\n",
					rc);
				D_GOTO(out, rc = -DER_DTP_HG);
			}
		}
		*data = rank_list;
		break;
	case HG_FREE:
		rank_list = *data;
		daos_rank_list_free(rank_list);
		*data = NULL;
		break;
	default:
		D_ERROR("Bad proc op: %d.\n", proc_op);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

out:
	return rc;
}

struct dtp_msg_field DMF_UUID =
	DEFINE_DTP_MSG("dtp_uuid", 0, sizeof(uuid_t),
		       dtp_proc_uuid_t);

struct dtp_msg_field DMF_INT =
	DEFINE_DTP_MSG("dtp_int", 0, sizeof(int32_t),
		       dtp_proc_int);

struct dtp_msg_field DMF_UINT32 =
	DEFINE_DTP_MSG("dtp_uint32", 0, sizeof(uint32_t),
		       dtp_proc_uint32_t);

struct dtp_msg_field DMF_UINT64 =
	DEFINE_DTP_MSG("dtp_uint64", 0, sizeof(uint64_t),
			dtp_proc_uint64_t);

struct dtp_msg_field DMF_DAOS_SIZE =
	DEFINE_DTP_MSG("dtp_daos_size", 0, sizeof(daos_size_t),
			dtp_proc_daos_size_t);

struct dtp_msg_field DMF_BULK =
	DEFINE_DTP_MSG("dtp_bulk", 0, sizeof(dtp_bulk_t),
		       dtp_proc_dtp_bulk_t);

struct dtp_msg_field DMF_BOOL =
	DEFINE_DTP_MSG("dtp_bool", 0, sizeof(bool),
		       dtp_proc_bool);

struct dtp_msg_field DMF_STRING =
	DEFINE_DTP_MSG("dtp_string", 0,
		       sizeof(dtp_string_t), dtp_proc_dtp_string_t);

struct dtp_msg_field DMF_RANK_LIST =
	DEFINE_DTP_MSG("daos_rank_list", 0,
		       sizeof(daos_rank_list_t), dtp_proc_daos_rank_list_t);
