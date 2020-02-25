/**
 * (C) Copyright 2020 Intel Corporation.
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

#include <daos/rpc.h>
#include "obj_rpc.h"
#include "rpc_csum.h"

#define ENCODING(proc) (proc_op == CRT_PROC_ENCODE)
#define DECODING(proc) (proc_op == CRT_PROC_DECODE)
#define FREEING(proc) (proc_op == CRT_PROC_FREE)
#define PROC(type, value) \
		do {\
			rc = crt_proc_##type(proc, value); \
			if (rc != 0)\
				return -DER_HG; \
		} while (0)

static int
proc_struct_dcs_csum_info(crt_proc_t proc, struct dcs_csum_info *csum)
{
	crt_proc_op_t		proc_op;
	int			rc;

	if (csum == NULL)
		return 0;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_HG;

	PROC(uint32_t, &csum->cs_nr);
	PROC(uint32_t, &csum->cs_chunksize);
	PROC(uint16_t, &csum->cs_type);
	PROC(uint16_t, &csum->cs_len);
	PROC(uint32_t, &csum->cs_buf_len);

	if (csum->cs_buf_len < csum->cs_len * csum->cs_nr) {
		D_ERROR("invalid csum buf len %iu < csum len %hu\n",
			csum->cs_buf_len, csum->cs_len);
		return -DER_HG;
	}

	if (csum->cs_buf_len == 0)
		return 0;

	if (ENCODING(proc_op)) {
		rc = crt_proc_memcpy(proc, csum->cs_csum, csum->cs_buf_len);
		if (rc != 0)
			return -DER_HG;
	}

	if (DECODING(proc_op)) {
		D_ALLOC(csum->cs_csum, csum->cs_buf_len);
		if (csum->cs_csum == NULL)
			return -DER_NOMEM;

		rc = crt_proc_memcpy(proc, csum->cs_csum, csum->cs_buf_len);
		if (rc != 0) {
			D_FREE(csum->cs_csum);
			return -DER_HG;
		}
	}

	if (FREEING(proc_op))
		D_FREE(csum->cs_csum);

	return 0;
}

int
crt_proc_struct_dcs_csum_info(crt_proc_t proc, struct dcs_csum_info **p_csum)
{
	crt_proc_op_t		proc_op;
	bool			csum_enabled;
	int			rc;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0 || p_csum == NULL)
		return -DER_HG;

	if (ENCODING(proc_op)) {
		csum_enabled = *p_csum != NULL;
		PROC(bool, &csum_enabled);
		if (csum_enabled) {
			rc = proc_struct_dcs_csum_info(proc, *p_csum);
			if (rc != 0)
				return -DER_HG;
		}

		return 0;
	}

	if (DECODING(proc_op)) {
		PROC(bool, &csum_enabled);
		if (!csum_enabled)
			return 0;
		D_ALLOC_PTR(*p_csum);
		if (*p_csum == NULL)
			return -DER_NOMEM;
		rc = proc_struct_dcs_csum_info(proc, *p_csum);
		if (rc != 0) {
			D_FREE(*p_csum);
			return rc;
		}
	}

	if (FREEING(proc_op)) {
		rc = proc_struct_dcs_csum_info(proc, *p_csum);
		D_FREE(*p_csum);
	}

	return rc;
}

int
crt_proc_struct_dcs_iod_csums(crt_proc_t proc, struct dcs_iod_csums *iod_csum)
{
	crt_proc_op_t		 proc_op;
	int			 rc, i;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_HG;

	rc = proc_struct_dcs_csum_info(proc, &iod_csum->ic_akey);
	if (rc != 0)
		return rc;

	if (ENCODING(proc_op)) {
		PROC(uint32_t, &iod_csum->ic_nr);
		for (i = 0; i < iod_csum->ic_nr; i++) {
			rc = proc_struct_dcs_csum_info(proc,
				&iod_csum->ic_data[i]);
			if (rc != 0)
				return rc;
		}
	}

	if (DECODING(proc)) {
		PROC(uint32_t, &iod_csum->ic_nr);
		D_ALLOC_ARRAY(iod_csum->ic_data, iod_csum->ic_nr);
		for (i = 0; i < iod_csum->ic_nr; i++) {
			rc = proc_struct_dcs_csum_info(proc,
				&iod_csum->ic_data[i]);
			if (rc != 0) {
				D_FREE(iod_csum->ic_data);
				return rc;
			}
		}
	}

	if (FREEING(proc)) {
		for (i = 0; i < iod_csum->ic_nr; i++) {
			rc = proc_struct_dcs_csum_info(proc,
				&iod_csum->ic_data[i]);
			if (rc != 0)
				break;
		}
		D_FREE(iod_csum->ic_data);
	}

	return rc;
}
