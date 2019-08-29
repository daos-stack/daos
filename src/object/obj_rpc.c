/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
 * DSR: RPC Protocol Serialization Functions
 */
#define D_LOGFAC	DD_FAC(object)

#include <daos/common.h>
#include <daos/event.h>
#include <daos/rpc.h>
#include <daos/object.h>
#include <daos_obj.h>
#include <gurt/types.h>
#include "obj_rpc.h"

static int
crt_proc_struct_dtx_id(crt_proc_t proc, struct dtx_id *dti)
{
	int rc;

	rc = crt_proc_uuid_t(proc, &dti->dti_uuid);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &dti->dti_hlc);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_daos_key_desc_t(crt_proc_t proc, daos_key_desc_t *key)
{
	int rc;

	rc = crt_proc_uint64_t(proc, &key->kd_key_len);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &key->kd_val_type);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint16_t(proc, &key->kd_csum_type);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint16_t(proc, &key->kd_csum_len);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_daos_obj_id_t(crt_proc_t proc, daos_obj_id_t *doi)
{
	int rc;

	rc = crt_proc_uint64_t(proc, &doi->lo);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &doi->hi);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_daos_unit_oid_t(crt_proc_t proc, daos_unit_oid_t *doi)
{
	int rc;

	rc = crt_proc_daos_obj_id_t(proc, &doi->id_pub);
	if (rc != 0)
		return rc;

	rc = crt_proc_uint32_t(proc, &doi->id_shard);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &doi->id_pad_32);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_daos_recx_t(crt_proc_t proc, daos_recx_t *recx)
{
	int rc;

	rc = crt_proc_uint64_t(proc, &recx->rx_idx);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &recx->rx_nr);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_daos_epoch_range_t(crt_proc_t proc, daos_epoch_range_t *erange)
{
	int rc;

	rc = crt_proc_uint64_t(proc, &erange->epr_lo);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &erange->epr_hi);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_daos_csum_buf_t(crt_proc_t proc, daos_csum_buf_t *csum)
{
	crt_proc_op_t	proc_op;
	int		rc;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &csum->cs_nr);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &csum->cs_chunksize);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint16_t(proc, &csum->cs_type);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint16_t(proc, &csum->cs_len);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &csum->cs_buf_len);
	if (rc != 0)
		return -DER_HG;

	if (csum->cs_buf_len < csum->cs_len) {
		D_ERROR("invalid csum buf len %iu < csum len %hu\n",
			csum->cs_buf_len, csum->cs_len);
		return -DER_HG;
	}

	if (proc_op == CRT_PROC_DECODE && csum->cs_buf_len > 0) {
		D_ALLOC(csum->cs_csum, csum->cs_buf_len);
		if (csum->cs_csum == NULL)
			return -DER_NOMEM;
	} else if (proc_op == CRT_PROC_FREE && csum->cs_buf_len > 0) {
		D_FREE(csum->cs_csum);
	}

	if (csum->cs_len > 0 && proc_op != CRT_PROC_FREE) {
		rc = crt_proc_memcpy(proc, csum->cs_csum, csum->cs_len);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				D_FREE(csum->cs_csum);
			return -DER_HG;
		}
	}

	return 0;
}

#define IOD_REC_EXIST	(1 << 0)
#define IOD_CSUM_EXIST	(1 << 1)
#define IOD_EPRS_EXIST	(1 << 2)
static int
crt_proc_daos_iod_t(crt_proc_t proc, daos_iod_t *dvi)
{
	crt_proc_op_t	proc_op;
	int		rc;
	int		i;
	uint32_t	existing_flags = 0;

	if (proc == NULL || dvi == NULL) {
		D_ERROR("Invalid parameter, proc: %p, data: %p.\n", proc, dvi);
		return -DER_INVAL;
	}
	if (dvi->iod_name.iov_buf_len < dvi->iod_name.iov_len)
		D_ERROR("RYON: Issue with IOV: iov buf len ("DF_U64")"
					" < iov len "DF_U64"\n",
			dvi->iod_name.iov_buf_len, dvi->iod_name.iov_len);

	rc = crt_proc_d_iov_t(proc, &dvi->iod_name);
	if (rc != 0)
		return rc;

	rc = crt_proc_daos_csum_buf_t(proc, &dvi->iod_kcsum);
	if (rc != 0)
		return rc;

	rc = crt_proc_memcpy(proc, &dvi->iod_type, sizeof(dvi->iod_type));
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &dvi->iod_size);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &dvi->iod_nr);
	if (rc != 0)
		return -DER_HG;

	if (dvi->iod_nr == 0 && dvi->iod_type != DAOS_IOD_ARRAY) {
		D_ERROR("invalid I/O descriptor, iod_nr = 0\n");
		return -DER_HG;
	}

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_HG;

	if (proc_op == CRT_PROC_ENCODE || proc_op == CRT_PROC_FREE) {
		if (dvi->iod_type == DAOS_IOD_ARRAY && dvi->iod_recxs != NULL)
			existing_flags |= IOD_REC_EXIST;
		if (dvi->iod_csums != NULL)
			existing_flags |= IOD_CSUM_EXIST;
		if (dvi->iod_eprs != NULL)
			existing_flags |= IOD_EPRS_EXIST;
	}

	rc = crt_proc_uint32_t(proc, &existing_flags);
	if (rc != 0)
		return -DER_HG;

	if (proc_op == CRT_PROC_DECODE) {
		if (existing_flags & IOD_REC_EXIST) {
			D_ALLOC_ARRAY(dvi->iod_recxs, dvi->iod_nr);
			if (dvi->iod_recxs == NULL)
				D_GOTO(free, rc = -DER_NOMEM);
		}

		if (existing_flags & IOD_CSUM_EXIST) {
			D_ALLOC_ARRAY(dvi->iod_csums, dvi->iod_nr);
			if (dvi->iod_csums == NULL)
				D_GOTO(free, rc = -DER_NOMEM);
		}

		if (existing_flags & IOD_EPRS_EXIST) {
			D_ALLOC_ARRAY(dvi->iod_eprs, dvi->iod_nr);
			if (dvi->iod_eprs == NULL)
				D_GOTO(free, rc = -DER_NOMEM);
		}
	}

	if (existing_flags & IOD_REC_EXIST) {
		for (i = 0; i < dvi->iod_nr; i++) {
			rc = crt_proc_daos_recx_t(proc, &dvi->iod_recxs[i]);
			if (rc != 0) {
				if (proc_op == CRT_PROC_DECODE)
					D_GOTO(free, rc);
				return rc;
			}
		}
	}

	if (existing_flags & IOD_CSUM_EXIST) {
		for (i = 0; i < dvi->iod_nr; i++) {
			rc = crt_proc_daos_csum_buf_t(proc, &dvi->iod_csums[i]);
			if (rc != 0) {
				if (proc_op == CRT_PROC_DECODE)
					D_GOTO(free, rc);
				return rc;
			}
		}
	}

	if (existing_flags & IOD_EPRS_EXIST) {
		for (i = 0; i < dvi->iod_nr; i++) {
			rc = crt_proc_daos_epoch_range_t(proc,
							 &dvi->iod_eprs[i]);
			if (rc != 0) {
				if (proc_op == CRT_PROC_DECODE)
					D_GOTO(free, rc);
				return rc;
			}
		}
	}

	if (proc_op == CRT_PROC_FREE) {
free:
		if (dvi->iod_recxs != NULL)
			D_FREE(dvi->iod_recxs);
		if (dvi->iod_csums != NULL)
			D_FREE(dvi->iod_csums);
		if (dvi->iod_eprs != NULL)
			D_FREE(dvi->iod_eprs);
	}

	return rc;
}

static int
crt_proc_daos_anchor_t(crt_proc_t proc, daos_anchor_t *anchor)
{
	if (crt_proc_uint16_t(proc, &anchor->da_type) != 0)
		return -DER_HG;

	if (crt_proc_uint16_t(proc, &anchor->da_shard) != 0)
		return -DER_HG;

	if (crt_proc_uint32_t(proc, &anchor->da_flags) != 0)
		return -DER_HG;

	if (crt_proc_raw(proc, anchor->da_buf, sizeof(anchor->da_buf)) != 0)
		return -DER_HG;

	return 0;
}

#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>

/* Obtain a backtrace and print it to stdout. */
void
print_trace (void)
{
	void *array[10];
	size_t size;
	char **strings;
	size_t i;

	size = backtrace (array, 10);
	strings = backtrace_symbols (array, size);

	D_ERROR ("Obtained %zd stack frames.\n", size);

	for (i = 0; i < size; i++)
		D_ERROR ("%s\n", strings[i]);

	free (strings);
}

static int
crt_proc_d_sg_list_t(crt_proc_t proc, d_sg_list_t *sgl)
{
	crt_proc_op_t	proc_op;
	int		i;
	int		rc;

	rc = crt_proc_uint32_t(proc, &sgl->sg_nr);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &sgl->sg_nr_out);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_HG;

	if (proc_op == CRT_PROC_DECODE && sgl->sg_nr > 0) {
		D_ALLOC_ARRAY(sgl->sg_iovs, sgl->sg_nr);
		if (sgl->sg_iovs == NULL)
			return -DER_NOMEM;
	}

	for (i = 0; i < sgl->sg_nr; i++) {
		d_iov_t *iov = &sgl->sg_iovs[i];
		if (iov->iov_buf_len < iov->iov_len) {

			D_ERROR("RYON: Issue with IOV (OPCODE: %d): "
				"iov buf len ("DF_U64")"
				" < iov len "DF_U64"\n",
				proc_op, iov->iov_buf_len, iov->iov_len);
			print_trace();
		}
		rc = crt_proc_d_iov_t(proc, &sgl->sg_iovs[i]);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				D_FREE(sgl->sg_iovs);
			return -DER_HG;
		}
	}

	if (proc_op == CRT_PROC_FREE && sgl->sg_iovs != NULL)
		D_FREE(sgl->sg_iovs);

	return rc;
}


static int
crt_proc_struct_daos_shard_tgt(crt_proc_t proc, struct daos_shard_tgt *st)
{
	int rc;

	rc = crt_proc_uint32_t(proc, &st->st_rank);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_uint32_t(proc, &st->st_shard);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_uint32_t(proc, &st->st_tgt_idx);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_uint32_t(proc, &st->st_tgt_id);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

// CRT_RPC_DEFINE(obj_update, DAOS_ISEQ_OBJ_RW, DAOS_OSEQ_OBJ_RW)
static int crt_proc_struct_obj_update_in(crt_proc_t proc,
					 struct obj_update_in *ptr)
{
	int rc = 0;
	rc = crt_proc_struct_dtx_id(proc, &ptr->orw_dti);
	if (rc)
		do {
			__typeof__(rc) __rc = (rc);
			(void) (__rc);
			goto out;
		}
		while (0);
	rc = crt_proc_daos_unit_oid_t(proc, &ptr->orw_oid);
	if (rc)
		do {
			__typeof__(rc) __rc = (rc);
			(void) (__rc);
			goto out;
		}
		while (0);
	rc = crt_proc_uuid_t(proc, &ptr->orw_pool_uuid);
	if (rc)
		do {
			__typeof__(rc) __rc = (rc);
			(void) (__rc);
			goto out;
		}
		while (0);
	rc = crt_proc_uuid_t(proc, &ptr->orw_co_hdl);
	if (rc)
		do {
			__typeof__(rc) __rc = (rc);
			(void) (__rc);
			goto out;
		}
		while (0);
	rc = crt_proc_uuid_t(proc, &ptr->orw_co_uuid);
	if (rc)
		do {
			__typeof__(rc) __rc = (rc);
			(void) (__rc);
			goto out;
		}
		while (0);
	rc = crt_proc_uint64_t(proc, &ptr->orw_epoch);
	if (rc)
		do {
			__typeof__(rc) __rc = (rc);
			(void) (__rc);
			goto out;
		}
		while (0);
	rc = crt_proc_uint64_t(proc, &ptr->orw_dkey_hash);
	if (rc)
		do {
			__typeof__(rc) __rc = (rc);
			(void) (__rc);
			goto out;
		}
		while (0);
	rc = crt_proc_uint32_t(proc, &ptr->orw_map_ver);
	if (rc)
		do {
			__typeof__(rc) __rc = (rc);
			(void) (__rc);
			goto out;
		}
		while (0);
	rc = crt_proc_uint32_t(proc, &ptr->orw_nr);
	if (rc)
		do {
			__typeof__(rc) __rc = (rc);
			(void) (__rc);
			goto out;
		}
		while (0);
	rc = crt_proc_uint32_t(proc, &ptr->orw_start_shard);
	if (rc)
		do {
			__typeof__(rc) __rc = (rc);
			(void) (__rc);
			goto out;
		}
		while (0);
	rc                  = crt_proc_uint32_t(proc, &ptr->orw_flags);
	if (rc)
		do {
			__typeof__(rc) __rc = (rc);
			(void) (__rc);
			goto out;
		}
		while (0);
	daos_key_t *tmp_iov = &ptr->orw_dkey;
	if (tmp_iov->iov_buf_len < tmp_iov->iov_len) {
		D_ERROR("RYON: Issue with IOV: iov buf len ("DF_U64")"
								   " < iov len "DF_U64"\n",
			tmp_iov->iov_buf_len, tmp_iov->iov_len);
		print_trace();
	}
	rc = crt_proc_d_iov_t(proc, &ptr->orw_dkey);
	if (rc)
		do {
			__typeof__(rc) __rc = (rc);
			(void) (__rc);
			goto out;
		}
		while (0);
	{
		uint64_t      count    = ptr->orw_dti_cos.ca_count;
		struct dtx_id **e_ptrp = &ptr->orw_dti_cos.ca_arrays;
		struct dtx_id *e_ptr   = ptr->orw_dti_cos.ca_arrays;
		int           i;
		crt_proc_op_t proc_op;
		rc = crt_proc_get_op(proc, &proc_op);
		if (rc)
			do {
				__typeof__(rc) __rc = (rc);
				(void) (__rc);
				goto out;
			}
			while (0);
		rc = crt_proc_uint64_t(proc, &count);
		if (rc)
			do {
				__typeof__(rc) __rc = (rc);
				(void) (__rc);
				goto out;
			}
			while (0);
		ptr->orw_dti_cos.ca_count = count;
		if (count == 0) {
			if (proc_op == CRT_PROC_DECODE)
				*e_ptrp = ((void *) 0);
			goto next_field_14;
		}
		if (proc_op == CRT_PROC_DECODE) {
			do {
				(e_ptr) = (__typeof__(e_ptr)) calloc(
					(int) count, (sizeof(*e_ptr)));
				do {
					if (({
						_Bool __rc;
						__rc = d_fault_inject &&
						       d_should_fail(
							       d_fault_attr_mem);
						if (__rc)
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DLOG_WARN) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "fault_id %d, injecting fault.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      d_fault_attr_mem->fa_id);
								}
								while (0);
							}
							while (0);
						__rc;
					})) {
						free(e_ptr);
						e_ptr = ((void *) 0);
					}
					if ((1) && (e_ptr) != ((void *) 0)) {
						if ((int) count <= 1)
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DB_MEM) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "alloc(" "calloc" ") '" "e_ptr" "': %i at %p.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      (int) (sizeof(*e_ptr)),
										      (e_ptr));
								}
								while (0);
							}
							while (0);
						else
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DB_MEM) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "alloc(" "calloc" ") '" "e_ptr" "': %i * '" "(int)count" "':%i at %p.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      (int) (sizeof(*e_ptr)),
										      (int) ((int) count),
										      (e_ptr));
								}
								while (0);
							}
							while (0);
						break;
					}
					(void) (0);
					if ((int) count >= 1)
						do {
							int __tmp_mask;
							do {
								(__tmp_mask) = d_log_check(
									(DLOG_ERR) |
									daos_object_logfac);
								if (__tmp_mask)
									d_log(__tmp_mask,
									      "%s:%d %s() " "out of memory (tried to " "calloc" " '" "e_ptr" "': %i)\n",
									      "_file_name_",
									      442,
									      "_function_name_",
									      (int) ((sizeof(*e_ptr)) *
										     ((int) count)));
							}
							while (0);
						}
						while (0);
					else
						do {
							int __tmp_mask;
							do {
								(__tmp_mask) = d_log_check(
									(DLOG_ERR) |
									daos_object_logfac);
								if (__tmp_mask)
									d_log(__tmp_mask,
									      "%s:%d %s() " "out of memory (tried to " "calloc" " '" "e_ptr" "': %i)\n",
									      "_file_name_",
									      442,
									      "_function_name_",
									      (int) (sizeof(*e_ptr)));
							}
							while (0);
						}
						while (0);
				}
				while (0);
			}
			while (0);
			if (e_ptr == ((void *) 0))
				do {
					__typeof__(rc = -DER_NOMEM) __rc = (rc = -DER_NOMEM);
					(void) (__rc);
					goto out;
				}
				while (0);
			*e_ptrp = e_ptr;
		}
		for (i                    = 0; i < count; i++) {
			rc = crt_proc_struct_dtx_id(proc, &e_ptr[i]);
			if (rc)
				do {
					__typeof__(rc) __rc = (rc);
					(void) (__rc);
					goto out;
				}
				while (0);
		}
		if (proc_op == CRT_PROC_FREE)
			do {
				do {
					int __tmp_mask;
					do {
						(__tmp_mask) = d_log_check(
							(DB_MEM) |
							daos_object_logfac);
						if (__tmp_mask)
							d_log(__tmp_mask,
							      "%s:%d %s() " "free '" "e_ptr" "' at %p.\n",
							      "_file_name_",
							      442,
							      "_function_name_",
							      (e_ptr));
					}
					while (0);
				}
				while (0);
				free(e_ptr);
				(e_ptr) = ((void *) 0);
			}
			while (0);
	}
	next_field_14:
	{
		uint64_t      count    = ptr->orw_iods.ca_count;
		daos_iod_t    **e_ptrp = &ptr->orw_iods.ca_arrays;
		daos_iod_t    *e_ptr   = ptr->orw_iods.ca_arrays;
		int           i;
		crt_proc_op_t proc_op;
		rc                     = crt_proc_get_op(proc, &proc_op);
		if (rc)
			do {
				__typeof__(rc) __rc = (rc);
				(void) (__rc);
				goto out;
			}
			while (0);
		rc = crt_proc_uint64_t(proc, &count);
		if (rc)
			do {
				__typeof__(rc) __rc = (rc);
				(void) (__rc);
				goto out;
			}
			while (0);
		ptr->orw_iods.ca_count = count;
		if (count == 0) {
			if (proc_op == CRT_PROC_DECODE)
				*e_ptrp = ((void *) 0);
			goto next_field_15;
		}
		if (proc_op == CRT_PROC_DECODE) {
			do {
				(e_ptr) = (__typeof__(e_ptr)) calloc(
					(int) count, (sizeof(*e_ptr)));
				do {
					if (({
						_Bool __rc;
						__rc = d_fault_inject &&
						       d_should_fail(
							       d_fault_attr_mem);
						if (__rc)
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DLOG_WARN) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "fault_id %d, injecting fault.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      d_fault_attr_mem->fa_id);
								}
								while (0);
							}
							while (0);
						__rc;
					})) {
						free(e_ptr);
						e_ptr = ((void *) 0);
					}
					if ((1) && (e_ptr) != ((void *) 0)) {
						if ((int) count <= 1)
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DB_MEM) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "alloc(" "calloc" ") '" "e_ptr" "': %i at %p.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      (int) (sizeof(*e_ptr)),
										      (e_ptr));
								}
								while (0);
							}
							while (0);
						else
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DB_MEM) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "alloc(" "calloc" ") '" "e_ptr" "': %i * '" "(int)count" "':%i at %p.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      (int) (sizeof(*e_ptr)),
										      (int) ((int) count),
										      (e_ptr));
								}
								while (0);
							}
							while (0);
						break;
					}
					(void) (0);
					if ((int) count >= 1)
						do {
							int __tmp_mask;
							do {
								(__tmp_mask) = d_log_check(
									(DLOG_ERR) |
									daos_object_logfac);
								if (__tmp_mask)
									d_log(__tmp_mask,
									      "%s:%d %s() " "out of memory (tried to " "calloc" " '" "e_ptr" "': %i)\n",
									      "_file_name_",
									      442,
									      "_function_name_",
									      (int) ((sizeof(*e_ptr)) *
										     ((int) count)));
							}
							while (0);
						}
						while (0);
					else
						do {
							int __tmp_mask;
							do {
								(__tmp_mask) = d_log_check(
									(DLOG_ERR) |
									daos_object_logfac);
								if (__tmp_mask)
									d_log(__tmp_mask,
									      "%s:%d %s() " "out of memory (tried to " "calloc" " '" "e_ptr" "': %i)\n",
									      "_file_name_",
									      442,
									      "_function_name_",
									      (int) (sizeof(*e_ptr)));
							}
							while (0);
						}
						while (0);
				}
				while (0);
			}
			while (0);
			if (e_ptr == ((void *) 0))
				do {
					__typeof__(rc = -DER_NOMEM) __rc = (rc = -DER_NOMEM);
					(void) (__rc);
					goto out;
				}
				while (0);
			*e_ptrp = e_ptr;
		}
		for (i                 = 0; i < count; i++) {
			rc = crt_proc_daos_iod_t(proc, &e_ptr[i]);
			if (rc)
				do {
					__typeof__(rc) __rc = (rc);
					(void) (__rc);
					goto out;
				}
				while (0);
		}
		if (proc_op == CRT_PROC_FREE)
			do {
				do {
					int __tmp_mask;
					do {
						(__tmp_mask) = d_log_check(
							(DB_MEM) |
							daos_object_logfac);
						if (__tmp_mask)
							d_log(__tmp_mask,
							      "%s:%d %s() " "free '" "e_ptr" "' at %p.\n",
							      "_file_name_",
							      442,
							      "_function_name_",
							      (e_ptr));
					}
					while (0);
				}
				while (0);
				free(e_ptr);
				(e_ptr) = ((void *) 0);
			}
			while (0);
	}
	next_field_15:
	{
		uint64_t      count    = ptr->orw_sgls.ca_count;
		d_sg_list_t   **e_ptrp = &ptr->orw_sgls.ca_arrays;
		d_sg_list_t   *e_ptr   = ptr->orw_sgls.ca_arrays;
		int           i;
		crt_proc_op_t proc_op;
		rc                     = crt_proc_get_op(proc, &proc_op);
		if (rc)
			do {
				__typeof__(rc) __rc = (rc);
				(void) (__rc);
				goto out;
			}
			while (0);
		rc = crt_proc_uint64_t(proc, &count);
		if (rc)
			do {
				__typeof__(rc) __rc = (rc);
				(void) (__rc);
				goto out;
			}
			while (0);
		ptr->orw_sgls.ca_count = count;
		if (count == 0) {
			if (proc_op == CRT_PROC_DECODE)
				*e_ptrp = ((void *) 0);
			goto next_field_16;
		}
		if (proc_op == CRT_PROC_DECODE) {
			do {
				(e_ptr) = (__typeof__(e_ptr)) calloc(
					(int) count, (sizeof(*e_ptr)));
				do {
					if (({
						_Bool __rc;
						__rc = d_fault_inject &&
						       d_should_fail(
							       d_fault_attr_mem);
						if (__rc)
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DLOG_WARN) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "fault_id %d, injecting fault.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      d_fault_attr_mem->fa_id);
								}
								while (0);
							}
							while (0);
						__rc;
					})) {
						free(e_ptr);
						e_ptr = ((void *) 0);
					}
					if ((1) && (e_ptr) != ((void *) 0)) {
						if ((int) count <= 1)
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DB_MEM) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "alloc(" "calloc" ") '" "e_ptr" "': %i at %p.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      (int) (sizeof(*e_ptr)),
										      (e_ptr));
								}
								while (0);
							}
							while (0);
						else
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DB_MEM) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "alloc(" "calloc" ") '" "e_ptr" "': %i * '" "(int)count" "':%i at %p.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      (int) (sizeof(*e_ptr)),
										      (int) ((int) count),
										      (e_ptr));
								}
								while (0);
							}
							while (0);
						break;
					}
					(void) (0);
					if ((int) count >= 1)
						do {
							int __tmp_mask;
							do {
								(__tmp_mask) = d_log_check(
									(DLOG_ERR) |
									daos_object_logfac);
								if (__tmp_mask)
									d_log(__tmp_mask,
									      "%s:%d %s() " "out of memory (tried to " "calloc" " '" "e_ptr" "': %i)\n",
									      "_file_name_",
									      442,
									      "_function_name_",
									      (int) ((sizeof(*e_ptr)) *
										     ((int) count)));
							}
							while (0);
						}
						while (0);
					else
						do {
							int __tmp_mask;
							do {
								(__tmp_mask) = d_log_check(
									(DLOG_ERR) |
									daos_object_logfac);
								if (__tmp_mask)
									d_log(__tmp_mask,
									      "%s:%d %s() " "out of memory (tried to " "calloc" " '" "e_ptr" "': %i)\n",
									      "_file_name_",
									      442,
									      "_function_name_",
									      (int) (sizeof(*e_ptr)));
							}
							while (0);
						}
						while (0);
				}
				while (0);
			}
			while (0);
			if (e_ptr == ((void *) 0))
				do {
					__typeof__(rc = -DER_NOMEM) __rc = (rc = -DER_NOMEM);
					(void) (__rc);
					goto out;
				}
				while (0);
			*e_ptrp = e_ptr;
		}
		for (i                 = 0; i < count; i++) {
			rc = crt_proc_d_sg_list_t(proc, &e_ptr[i]);
			if (rc)
				do {
					__typeof__(rc) __rc = (rc);
					(void) (__rc);
					goto out;
				}
				while (0);
		}
		if (proc_op == CRT_PROC_FREE)
			do {
				do {
					int __tmp_mask;
					do {
						(__tmp_mask) = d_log_check(
							(DB_MEM) |
							daos_object_logfac);
						if (__tmp_mask)
							d_log(__tmp_mask,
							      "%s:%d %s() " "free '" "e_ptr" "' at %p.\n",
							      "_file_name_",
							      442,
							      "_function_name_",
							      (e_ptr));
					}
					while (0);
				}
				while (0);
				free(e_ptr);
				(e_ptr) = ((void *) 0);
			}
			while (0);
	}
	next_field_16:
	{
		uint64_t      count    = ptr->orw_bulks.ca_count;
		crt_bulk_t    **e_ptrp = &ptr->orw_bulks.ca_arrays;
		crt_bulk_t    *e_ptr   = ptr->orw_bulks.ca_arrays;
		int           i;
		crt_proc_op_t proc_op;
		rc                     = crt_proc_get_op(proc, &proc_op);
		if (rc)
			do {
				__typeof__(rc) __rc = (rc);
				(void) (__rc);
				goto out;
			}
			while (0);
		rc = crt_proc_uint64_t(proc, &count);
		if (rc)
			do {
				__typeof__(rc) __rc = (rc);
				(void) (__rc);
				goto out;
			}
			while (0);
		ptr->orw_bulks.ca_count = count;
		if (count == 0) {
			if (proc_op == CRT_PROC_DECODE)
				*e_ptrp = ((void *) 0);
			goto next_field_17;
		}
		if (proc_op == CRT_PROC_DECODE) {
			do {
				(e_ptr) = (__typeof__(e_ptr)) calloc(
					(int) count, (sizeof(*e_ptr)));
				do {
					if (({
						_Bool __rc;
						__rc = d_fault_inject &&
						       d_should_fail(
							       d_fault_attr_mem);
						if (__rc)
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DLOG_WARN) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "fault_id %d, injecting fault.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      d_fault_attr_mem->fa_id);
								}
								while (0);
							}
							while (0);
						__rc;
					})) {
						free(e_ptr);
						e_ptr = ((void *) 0);
					}
					if ((1) && (e_ptr) != ((void *) 0)) {
						if ((int) count <= 1)
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DB_MEM) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "alloc(" "calloc" ") '" "e_ptr" "': %i at %p.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      (int) (sizeof(*e_ptr)),
										      (e_ptr));
								}
								while (0);
							}
							while (0);
						else
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DB_MEM) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "alloc(" "calloc" ") '" "e_ptr" "': %i * '" "(int)count" "':%i at %p.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      (int) (sizeof(*e_ptr)),
										      (int) ((int) count),
										      (e_ptr));
								}
								while (0);
							}
							while (0);
						break;
					}
					(void) (0);
					if ((int) count >= 1)
						do {
							int __tmp_mask;
							do {
								(__tmp_mask) = d_log_check(
									(DLOG_ERR) |
									daos_object_logfac);
								if (__tmp_mask)
									d_log(__tmp_mask,
									      "%s:%d %s() " "out of memory (tried to " "calloc" " '" "e_ptr" "': %i)\n",
									      "_file_name_",
									      442,
									      "_function_name_",
									      (int) ((sizeof(*e_ptr)) *
										     ((int) count)));
							}
							while (0);
						}
						while (0);
					else
						do {
							int __tmp_mask;
							do {
								(__tmp_mask) = d_log_check(
									(DLOG_ERR) |
									daos_object_logfac);
								if (__tmp_mask)
									d_log(__tmp_mask,
									      "%s:%d %s() " "out of memory (tried to " "calloc" " '" "e_ptr" "': %i)\n",
									      "_file_name_",
									      442,
									      "_function_name_",
									      (int) (sizeof(*e_ptr)));
							}
							while (0);
						}
						while (0);
				}
				while (0);
			}
			while (0);
			if (e_ptr == ((void *) 0))
				do {
					__typeof__(rc = -DER_NOMEM) __rc = (rc = -DER_NOMEM);
					(void) (__rc);
					goto out;
				}
				while (0);
			*e_ptrp = e_ptr;
		}
		for (i                  = 0; i < count; i++) {
			rc = crt_proc_crt_bulk_t(proc, &e_ptr[i]);
			if (rc)
				do {
					__typeof__(rc) __rc = (rc);
					(void) (__rc);
					goto out;
				}
				while (0);
		}
		if (proc_op == CRT_PROC_FREE)
			do {
				do {
					int __tmp_mask;
					do {
						(__tmp_mask) = d_log_check(
							(DB_MEM) |
							daos_object_logfac);
						if (__tmp_mask)
							d_log(__tmp_mask,
							      "%s:%d %s() " "free '" "e_ptr" "' at %p.\n",
							      "_file_name_",
							      442,
							      "_function_name_",
							      (e_ptr));
					}
					while (0);
				}
				while (0);
				free(e_ptr);
				(e_ptr) = ((void *) 0);
			}
			while (0);
	}
	next_field_17:
	{
		uint64_t              count    = ptr->orw_shard_tgts.ca_count;
		struct daos_shard_tgt **e_ptrp = &ptr->orw_shard_tgts.ca_arrays;
		struct daos_shard_tgt *e_ptr   = ptr->orw_shard_tgts.ca_arrays;
		int                   i;
		crt_proc_op_t         proc_op;
		rc                             = crt_proc_get_op(proc,
								 &proc_op);
		if (rc)
			do {
				__typeof__(rc) __rc = (rc);
				(void) (__rc);
				goto out;
			}
			while (0);
		rc = crt_proc_uint64_t(proc, &count);
		if (rc)
			do {
				__typeof__(rc) __rc = (rc);
				(void) (__rc);
				goto out;
			}
			while (0);
		ptr->orw_shard_tgts.ca_count = count;
		if (count == 0) {
			if (proc_op == CRT_PROC_DECODE)
				*e_ptrp = ((void *) 0);
			goto next_field_18;
		}
		if (proc_op == CRT_PROC_DECODE) {
			do {
				(e_ptr) = (__typeof__(e_ptr)) calloc(
					(int) count, (sizeof(*e_ptr)));
				do {
					if (({
						_Bool __rc;
						__rc = d_fault_inject &&
						       d_should_fail(
							       d_fault_attr_mem);
						if (__rc)
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DLOG_WARN) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "fault_id %d, injecting fault.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      d_fault_attr_mem->fa_id);
								}
								while (0);
							}
							while (0);
						__rc;
					})) {
						free(e_ptr);
						e_ptr = ((void *) 0);
					}
					if ((1) && (e_ptr) != ((void *) 0)) {
						if ((int) count <= 1)
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DB_MEM) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "alloc(" "calloc" ") '" "e_ptr" "': %i at %p.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      (int) (sizeof(*e_ptr)),
										      (e_ptr));
								}
								while (0);
							}
							while (0);
						else
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DB_MEM) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "alloc(" "calloc" ") '" "e_ptr" "': %i * '" "(int)count" "':%i at %p.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      (int) (sizeof(*e_ptr)),
										      (int) ((int) count),
										      (e_ptr));
								}
								while (0);
							}
							while (0);
						break;
					}
					(void) (0);
					if ((int) count >= 1)
						do {
							int __tmp_mask;
							do {
								(__tmp_mask) = d_log_check(
									(DLOG_ERR) |
									daos_object_logfac);
								if (__tmp_mask)
									d_log(__tmp_mask,
									      "%s:%d %s() " "out of memory (tried to " "calloc" " '" "e_ptr" "': %i)\n",
									      "_file_name_",
									      442,
									      "_function_name_",
									      (int) ((sizeof(*e_ptr)) *
										     ((int) count)));
							}
							while (0);
						}
						while (0);
					else
						do {
							int __tmp_mask;
							do {
								(__tmp_mask) = d_log_check(
									(DLOG_ERR) |
									daos_object_logfac);
								if (__tmp_mask)
									d_log(__tmp_mask,
									      "%s:%d %s() " "out of memory (tried to " "calloc" " '" "e_ptr" "': %i)\n",
									      "_file_name_",
									      442,
									      "_function_name_",
									      (int) (sizeof(*e_ptr)));
							}
							while (0);
						}
						while (0);
				}
				while (0);
			}
			while (0);
			if (e_ptr == ((void *) 0))
				do {
					__typeof__(rc = -DER_NOMEM) __rc = (rc = -DER_NOMEM);
					(void) (__rc);
					goto out;
				}
				while (0);
			*e_ptrp = e_ptr;
		}
		for (i                       = 0; i < count; i++) {
			rc = crt_proc_struct_daos_shard_tgt(proc, &e_ptr[i]);
			if (rc)
				do {
					__typeof__(rc) __rc = (rc);
					(void) (__rc);
					goto out;
				}
				while (0);
		}
		if (proc_op == CRT_PROC_FREE)
			do {
				do {
					int __tmp_mask;
					do {
						(__tmp_mask) = d_log_check(
							(DB_MEM) |
							daos_object_logfac);
						if (__tmp_mask)
							d_log(__tmp_mask,
							      "%s:%d %s() " "free '" "e_ptr" "' at %p.\n",
							      "_file_name_",
							      442,
							      "_function_name_",
							      (e_ptr));
					}
					while (0);
				}
				while (0);
				free(e_ptr);
				(e_ptr) = ((void *) 0);
			}
			while (0);
	}
	next_field_18:
	out:
	return rc;
}

static struct crt_msg_field CMF_obj_update_in            = {.cmf_flags=0, .cmf_size=sizeof(struct obj_update_in), .cmf_proc=(crt_proc_cb_t) crt_proc_struct_obj_update_in};
static struct crt_msg_field *crt_obj_update_in_fields[]  = {&CMF_obj_update_in};

static int crt_proc_struct_obj_update_out(crt_proc_t proc,
					  struct obj_update_out *ptr)
{
	int rc = 0;
	rc = crt_proc_int32_t(proc, &ptr->orw_ret);
	if (rc)
		do {
			__typeof__(rc) __rc = (rc);
			(void) (__rc);
			goto out;
		}
		while (0);
	rc = crt_proc_uint32_t(proc, &ptr->orw_map_version);
	if (rc)
		do {
			__typeof__(rc) __rc = (rc);
			(void) (__rc);
			goto out;
		}
		while (0);
	rc = crt_proc_uint64_t(proc, &ptr->orw_attr);
	if (rc)
		do {
			__typeof__(rc) __rc = (rc);
			(void) (__rc);
			goto out;
		}
		while (0);
	rc = crt_proc_uint64_t(proc, &ptr->orw_dkey_conflict);
	if (rc)
		do {
			__typeof__(rc) __rc = (rc);
			(void) (__rc);
			goto out;
		}
		while (0);
	rc = crt_proc_struct_dtx_id(proc, &ptr->orw_dti_conflict);
	if (rc)
		do {
			__typeof__(rc) __rc = (rc);
			(void) (__rc);
			goto out;
		}
		while (0);
	{
		uint64_t      count    = ptr->orw_sizes.ca_count;
		daos_size_t   **e_ptrp = &ptr->orw_sizes.ca_arrays;
		daos_size_t   *e_ptr   = ptr->orw_sizes.ca_arrays;
		int           i;
		crt_proc_op_t proc_op;
		rc = crt_proc_get_op(proc, &proc_op);
		if (rc)
			do {
				__typeof__(rc) __rc = (rc);
				(void) (__rc);
				goto out;
			}
			while (0);
		rc = crt_proc_uint64_t(proc, &count);
		if (rc)
			do {
				__typeof__(rc) __rc = (rc);
				(void) (__rc);
				goto out;
			}
			while (0);
		ptr->orw_sizes.ca_count = count;
		if (count == 0) {
			if (proc_op == CRT_PROC_DECODE)
				*e_ptrp = ((void *) 0);
			goto next_field_7;
		}
		if (proc_op == CRT_PROC_DECODE) {
			do {
				(e_ptr) = (__typeof__(e_ptr)) calloc(
					(int) count, (sizeof(*e_ptr)));
				do {
					if (({
						_Bool __rc;
						__rc = d_fault_inject &&
						       d_should_fail(
							       d_fault_attr_mem);
						if (__rc)
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DLOG_WARN) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "fault_id %d, injecting fault.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      d_fault_attr_mem->fa_id);
								}
								while (0);
							}
							while (0);
						__rc;
					})) {
						free(e_ptr);
						e_ptr = ((void *) 0);
					}
					if ((1) && (e_ptr) != ((void *) 0)) {
						if ((int) count <= 1)
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DB_MEM) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "alloc(" "calloc" ") '" "e_ptr" "': %i at %p.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      (int) (sizeof(*e_ptr)),
										      (e_ptr));
								}
								while (0);
							}
							while (0);
						else
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DB_MEM) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "alloc(" "calloc" ") '" "e_ptr" "': %i * '" "(int)count" "':%i at %p.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      (int) (sizeof(*e_ptr)),
										      (int) ((int) count),
										      (e_ptr));
								}
								while (0);
							}
							while (0);
						break;
					}
					(void) (0);
					if ((int) count >= 1)
						do {
							int __tmp_mask;
							do {
								(__tmp_mask) = d_log_check(
									(DLOG_ERR) |
									daos_object_logfac);
								if (__tmp_mask)
									d_log(__tmp_mask,
									      "%s:%d %s() " "out of memory (tried to " "calloc" " '" "e_ptr" "': %i)\n",
									      "_file_name_",
									      442,
									      "_function_name_",
									      (int) ((sizeof(*e_ptr)) *
										     ((int) count)));
							}
							while (0);
						}
						while (0);
					else
						do {
							int __tmp_mask;
							do {
								(__tmp_mask) = d_log_check(
									(DLOG_ERR) |
									daos_object_logfac);
								if (__tmp_mask)
									d_log(__tmp_mask,
									      "%s:%d %s() " "out of memory (tried to " "calloc" " '" "e_ptr" "': %i)\n",
									      "_file_name_",
									      442,
									      "_function_name_",
									      (int) (sizeof(*e_ptr)));
							}
							while (0);
						}
						while (0);
				}
				while (0);
			}
			while (0);
			if (e_ptr == ((void *) 0))
				do {
					__typeof__(rc = -DER_NOMEM) __rc = (rc = -DER_NOMEM);
					(void) (__rc);
					goto out;
				}
				while (0);
			*e_ptrp = e_ptr;
		}
		for (i                  = 0; i < count; i++) {
			rc = crt_proc_uint64_t(proc, &e_ptr[i]);
			if (rc)
				do {
					__typeof__(rc) __rc = (rc);
					(void) (__rc);
					goto out;
				}
				while (0);
		}
		if (proc_op == CRT_PROC_FREE)
			do {
				do {
					int __tmp_mask;
					do {
						(__tmp_mask) = d_log_check(
							(DB_MEM) |
							daos_object_logfac);
						if (__tmp_mask)
							d_log(__tmp_mask,
							      "%s:%d %s() " "free '" "e_ptr" "' at %p.\n",
							      "_file_name_",
							      442,
							      "_function_name_",
							      (e_ptr));
					}
					while (0);
				}
				while (0);
				free(e_ptr);
				(e_ptr) = ((void *) 0);
			}
			while (0);
	}
	next_field_7:
	{
		uint64_t      count    = ptr->orw_sgls.ca_count;
		d_sg_list_t   **e_ptrp = &ptr->orw_sgls.ca_arrays;
		d_sg_list_t   *e_ptr   = ptr->orw_sgls.ca_arrays;
		int           i;
		crt_proc_op_t proc_op;
		rc                     = crt_proc_get_op(proc, &proc_op);
		if (rc)
			do {
				__typeof__(rc) __rc = (rc);
				(void) (__rc);
				goto out;
			}
			while (0);
		rc = crt_proc_uint64_t(proc, &count);
		if (rc)
			do {
				__typeof__(rc) __rc = (rc);
				(void) (__rc);
				goto out;
			}
			while (0);
		ptr->orw_sgls.ca_count = count;
		if (count == 0) {
			if (proc_op == CRT_PROC_DECODE)
				*e_ptrp = ((void *) 0);
			goto next_field_8;
		}
		if (proc_op == CRT_PROC_DECODE) {
			do {
				(e_ptr) = (__typeof__(e_ptr)) calloc(
					(int) count, (sizeof(*e_ptr)));
				do {
					if (({
						_Bool __rc;
						__rc = d_fault_inject &&
						       d_should_fail(
							       d_fault_attr_mem);
						if (__rc)
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DLOG_WARN) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "fault_id %d, injecting fault.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      d_fault_attr_mem->fa_id);
								}
								while (0);
							}
							while (0);
						__rc;
					})) {
						free(e_ptr);
						e_ptr = ((void *) 0);
					}
					if ((1) && (e_ptr) != ((void *) 0)) {
						if ((int) count <= 1)
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DB_MEM) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "alloc(" "calloc" ") '" "e_ptr" "': %i at %p.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      (int) (sizeof(*e_ptr)),
										      (e_ptr));
								}
								while (0);
							}
							while (0);
						else
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DB_MEM) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "alloc(" "calloc" ") '" "e_ptr" "': %i * '" "(int)count" "':%i at %p.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      (int) (sizeof(*e_ptr)),
										      (int) ((int) count),
										      (e_ptr));
								}
								while (0);
							}
							while (0);
						break;
					}
					(void) (0);
					if ((int) count >= 1)
						do {
							int __tmp_mask;
							do {
								(__tmp_mask) = d_log_check(
									(DLOG_ERR) |
									daos_object_logfac);
								if (__tmp_mask)
									d_log(__tmp_mask,
									      "%s:%d %s() " "out of memory (tried to " "calloc" " '" "e_ptr" "': %i)\n",
									      "_file_name_",
									      442,
									      "_function_name_",
									      (int) ((sizeof(*e_ptr)) *
										     ((int) count)));
							}
							while (0);
						}
						while (0);
					else
						do {
							int __tmp_mask;
							do {
								(__tmp_mask) = d_log_check(
									(DLOG_ERR) |
									daos_object_logfac);
								if (__tmp_mask)
									d_log(__tmp_mask,
									      "%s:%d %s() " "out of memory (tried to " "calloc" " '" "e_ptr" "': %i)\n",
									      "_file_name_",
									      442,
									      "_function_name_",
									      (int) (sizeof(*e_ptr)));
							}
							while (0);
						}
						while (0);
				}
				while (0);
			}
			while (0);
			if (e_ptr == ((void *) 0))
				do {
					__typeof__(rc = -DER_NOMEM) __rc = (rc = -DER_NOMEM);
					(void) (__rc);
					goto out;
				}
				while (0);
			*e_ptrp = e_ptr;
		}
		for (i                 = 0; i < count; i++) {
			rc = crt_proc_d_sg_list_t(proc, &e_ptr[i]);
			if (rc)
				do {
					__typeof__(rc) __rc = (rc);
					(void) (__rc);
					goto out;
				}
				while (0);
		}
		if (proc_op == CRT_PROC_FREE)
			do {
				do {
					int __tmp_mask;
					do {
						(__tmp_mask) = d_log_check(
							(DB_MEM) |
							daos_object_logfac);
						if (__tmp_mask)
							d_log(__tmp_mask,
							      "%s:%d %s() " "free '" "e_ptr" "' at %p.\n",
							      "_file_name_",
							      442,
							      "_function_name_",
							      (e_ptr));
					}
					while (0);
				}
				while (0);
				free(e_ptr);
				(e_ptr) = ((void *) 0);
			}
			while (0);
	}
	next_field_8:
	{
		uint64_t      count    = ptr->orw_nrs.ca_count;
		uint32_t      **e_ptrp = &ptr->orw_nrs.ca_arrays;
		uint32_t      *e_ptr   = ptr->orw_nrs.ca_arrays;
		int           i;
		crt_proc_op_t proc_op;
		rc                     = crt_proc_get_op(proc, &proc_op);
		if (rc)
			do {
				__typeof__(rc) __rc = (rc);
				(void) (__rc);
				goto out;
			}
			while (0);
		rc = crt_proc_uint64_t(proc, &count);
		if (rc)
			do {
				__typeof__(rc) __rc = (rc);
				(void) (__rc);
				goto out;
			}
			while (0);
		ptr->orw_nrs.ca_count = count;
		if (count == 0) {
			if (proc_op == CRT_PROC_DECODE)
				*e_ptrp = ((void *) 0);
			goto next_field_9;
		}
		if (proc_op == CRT_PROC_DECODE) {
			do {
				(e_ptr) = (__typeof__(e_ptr)) calloc(
					(int) count, (sizeof(*e_ptr)));
				do {
					if (({
						_Bool __rc;
						__rc = d_fault_inject &&
						       d_should_fail(
							       d_fault_attr_mem);
						if (__rc)
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DLOG_WARN) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "fault_id %d, injecting fault.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      d_fault_attr_mem->fa_id);
								}
								while (0);
							}
							while (0);
						__rc;
					})) {
						free(e_ptr);
						e_ptr = ((void *) 0);
					}
					if ((1) && (e_ptr) != ((void *) 0)) {
						if ((int) count <= 1)
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DB_MEM) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "alloc(" "calloc" ") '" "e_ptr" "': %i at %p.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      (int) (sizeof(*e_ptr)),
										      (e_ptr));
								}
								while (0);
							}
							while (0);
						else
							do {
								int __tmp_mask;
								do {
									(__tmp_mask) = d_log_check(
										(DB_MEM) |
										daos_object_logfac);
									if (__tmp_mask)
										d_log(__tmp_mask,
										      "%s:%d %s() " "alloc(" "calloc" ") '" "e_ptr" "': %i * '" "(int)count" "':%i at %p.\n",
										      "_file_name_",
										      442,
										      "_function_name_",
										      (int) (sizeof(*e_ptr)),
										      (int) ((int) count),
										      (e_ptr));
								}
								while (0);
							}
							while (0);
						break;
					}
					(void) (0);
					if ((int) count >= 1)
						do {
							int __tmp_mask;
							do {
								(__tmp_mask) = d_log_check(
									(DLOG_ERR) |
									daos_object_logfac);
								if (__tmp_mask)
									d_log(__tmp_mask,
									      "%s:%d %s() " "out of memory (tried to " "calloc" " '" "e_ptr" "': %i)\n",
									      "_file_name_",
									      442,
									      "_function_name_",
									      (int) ((sizeof(*e_ptr)) *
										     ((int) count)));
							}
							while (0);
						}
						while (0);
					else
						do {
							int __tmp_mask;
							do {
								(__tmp_mask) = d_log_check(
									(DLOG_ERR) |
									daos_object_logfac);
								if (__tmp_mask)
									d_log(__tmp_mask,
									      "%s:%d %s() " "out of memory (tried to " "calloc" " '" "e_ptr" "': %i)\n",
									      "_file_name_",
									      442,
									      "_function_name_",
									      (int) (sizeof(*e_ptr)));
							}
							while (0);
						}
						while (0);
				}
				while (0);
			}
			while (0);
			if (e_ptr == ((void *) 0))
				do {
					__typeof__(rc = -DER_NOMEM) __rc = (rc = -DER_NOMEM);
					(void) (__rc);
					goto out;
				}
				while (0);
			*e_ptrp = e_ptr;
		}
		for (i                = 0; i < count; i++) {
			rc = crt_proc_uint32_t(proc, &e_ptr[i]);
			if (rc)
				do {
					__typeof__(rc) __rc = (rc);
					(void) (__rc);
					goto out;
				}
				while (0);
		}
		if (proc_op == CRT_PROC_FREE)
			do {
				do {
					int __tmp_mask;
					do {
						(__tmp_mask) = d_log_check(
							(DB_MEM) |
							daos_object_logfac);
						if (__tmp_mask)
							d_log(__tmp_mask,
							      "%s:%d %s() " "free '" "e_ptr" "' at %p.\n",
							      "_file_name_",
							      442,
							      "_function_name_",
							      (e_ptr));
					}
					while (0);
				}
				while (0);
				free(e_ptr);
				(e_ptr) = ((void *) 0);
			}
			while (0);
	}
	next_field_9:
	out:
	return rc;
}

static struct crt_msg_field CMF_obj_update_out           = {.cmf_flags=0, .cmf_size=sizeof(struct obj_update_out), .cmf_proc=(crt_proc_cb_t) crt_proc_struct_obj_update_out};
static struct crt_msg_field *crt_obj_update_out_fields[] = {
	&CMF_obj_update_out};
_Pragma("GCC diagnostic push")
struct crt_req_format CQF_obj_update = {crf_in:{crf_count:(((crt_obj_update_in_fields) ==
							    ((void *) 0)) ? 0
									  : (
								   sizeof(crt_obj_update_in_fields) /
								   sizeof((crt_obj_update_in_fields)[0]))), crf_msg:((crt_obj_update_in_fields))}, crf_out:{crf_count:(((crt_obj_update_out_fields) ==
																					((void *) 0))
																				       ? 0
																				       : (sizeof(crt_obj_update_out_fields) /
																					  sizeof((crt_obj_update_out_fields)[0]))), crf_msg:((crt_obj_update_out_fields))}};
_Pragma("GCC diagnostic pop")







CRT_RPC_DEFINE(obj_fetch, DAOS_ISEQ_OBJ_RW, DAOS_OSEQ_OBJ_RW)
CRT_RPC_DEFINE(obj_key_enum, DAOS_ISEQ_OBJ_KEY_ENUM, DAOS_OSEQ_OBJ_KEY_ENUM)
CRT_RPC_DEFINE(obj_punch, DAOS_ISEQ_OBJ_PUNCH, DAOS_OSEQ_OBJ_PUNCH)
CRT_RPC_DEFINE(obj_query_key, DAOS_ISEQ_OBJ_QUERY_KEY, DAOS_OSEQ_OBJ_QUERY_KEY)

/* Define for cont_rpcs[] array population below.
 * See OBJ_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
}

static struct crt_proto_rpc_format obj_proto_rpc_fmt[] = {
	OBJ_PROTO_CLI_RPC_LIST,
};

#undef X

struct crt_proto_format obj_proto_fmt = {
	.cpf_name  = "daos-obj-proto",
	.cpf_ver   = DAOS_OBJ_VERSION,
	.cpf_count = ARRAY_SIZE(obj_proto_rpc_fmt),
	.cpf_prf   = obj_proto_rpc_fmt,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_OBJ_MODULE, 0)
};

void
obj_reply_set_status(crt_rpc_t *rpc, int status)
{
	void *reply = crt_reply_get(rpc);

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_FETCH:
	case DAOS_OBJ_RPC_TGT_UPDATE:
		((struct obj_rw_out *)reply)->orw_ret = status;
		break;
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
		((struct obj_key_enum_out *)reply)->oeo_ret = status;
		break;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH_AKEYS:
		((struct obj_punch_out *)reply)->opo_ret = status;
		break;
	case DAOS_OBJ_RPC_QUERY_KEY:
		((struct obj_query_key_out *)reply)->okqo_ret = status;
		break;
	default:
		D_ASSERT(0);
	}
}

int
obj_reply_get_status(crt_rpc_t *rpc)
{
	void *reply = crt_reply_get(rpc);

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_TGT_UPDATE:
	case DAOS_OBJ_RPC_FETCH:
		return ((struct obj_rw_out *)reply)->orw_ret;
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
		return ((struct obj_key_enum_out *)reply)->oeo_ret;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH_AKEYS:
		return ((struct obj_punch_out *)reply)->opo_ret;
	case DAOS_OBJ_RPC_QUERY_KEY:
		return ((struct obj_query_key_out *)reply)->okqo_ret;
	default:
		D_ASSERT(0);
	}
	return 0;
}

void
obj_reply_map_version_set(crt_rpc_t *rpc, uint32_t map_version)
{
	void *reply = crt_reply_get(rpc);

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_TGT_UPDATE:
	case DAOS_OBJ_RPC_FETCH:
		((struct obj_rw_out *)reply)->orw_map_version = map_version;
		break;
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
		((struct obj_key_enum_out *)reply)->oeo_map_version =
								map_version;
		break;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH_AKEYS:
		((struct obj_punch_out *)reply)->opo_map_version = map_version;
		break;
	case DAOS_OBJ_RPC_QUERY_KEY:
		((struct obj_query_key_out *)reply)->okqo_map_version =
			map_version;
		break;
	default:
		D_ASSERT(0);
	}
}

uint32_t
obj_reply_map_version_get(crt_rpc_t *rpc)
{
	void *reply = crt_reply_get(rpc);

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_TGT_UPDATE:
	case DAOS_OBJ_RPC_FETCH:
		return ((struct obj_rw_out *)reply)->orw_map_version;
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
		return ((struct obj_key_enum_out *)reply)->oeo_map_version;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH_AKEYS:
		return ((struct obj_punch_out *)reply)->opo_map_version;
	case DAOS_OBJ_RPC_QUERY_KEY:
		return ((struct obj_query_key_out *)reply)->okqo_map_version;
	default:
		D_ASSERT(0);
	}
	return 0;
}

void
obj_reply_dtx_conflict_set(crt_rpc_t *rpc, struct dtx_conflict_entry *dce)
{
	void *reply = crt_reply_get(rpc);

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_TGT_UPDATE: {
		struct obj_rw_out	*orw = reply;

		daos_dti_copy(&orw->orw_dti_conflict, &dce->dce_xid);
		orw->orw_dkey_conflict = dce->dce_dkey;
		break;
	}
	case DAOS_OBJ_RPC_TGT_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH_AKEYS: {
		struct obj_punch_out	*opo = reply;

		daos_dti_copy(&opo->opo_dti_conflict, &dce->dce_xid);
		opo->opo_dkey_conflict = dce->dce_dkey;
		break;
	}
	default:
		D_ASSERT(0);
	}
}
