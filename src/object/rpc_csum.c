/**
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * DSR: RPC Protocol Serialization Functions
 */
#define D_LOGFAC	DD_FAC(object)

#include <daos/rpc.h>
#include "obj_rpc.h"
#include "rpc_csum.h"

#define PROC(type, value)						\
		do {							\
			rc = crt_proc_##type(proc, proc_op, value);	\
			if (unlikely(rc))				\
				return rc;				\
		} while (0)

/**
 * advanced dcs_csum_info proc, can be used to proc partial data of the csum
 * for EC single-value.
 */
static int
proc_struct_dcs_csum_info_adv(crt_proc_t proc, crt_proc_op_t proc_op,
			      struct dcs_csum_info *csum, uint32_t idx,
			      uint32_t nr)
{
	uint32_t	buf_len = 0;
	int		rc;

	if (csum == NULL)
		return 0;

	if (FREEING(proc_op)) {
		D_FREE(csum->cs_csum);
		return 0;
	}

	if (ENCODING(proc_op)) {
		D_ASSERT(nr == csum->cs_nr || nr == 1);
		PROC(uint32_t, &nr);
		buf_len = nr * csum->cs_len;
		PROC(uint32_t, &buf_len);
	} else {
		PROC(uint32_t, &csum->cs_nr);
		PROC(uint32_t, &csum->cs_buf_len);
	}
	PROC(uint32_t, &csum->cs_chunksize);
	PROC(uint16_t, &csum->cs_type);
	PROC(uint16_t, &csum->cs_len);

	if (csum->cs_buf_len < csum->cs_len * csum->cs_nr) {
		D_ERROR("invalid csum buf len %iu < csum len %hu\n",
			csum->cs_buf_len, csum->cs_len);
		return -DER_HG;
	}

	if (csum->cs_buf_len == 0)
		return 0;

	if (ENCODING(proc_op)) {
		D_ASSERT(csum->cs_csum != NULL);
		rc = crt_proc_memcpy(proc, proc_op,
				     csum->cs_csum + idx * csum->cs_len,
				     buf_len);
		if (unlikely(rc))
			return rc;
	}

	if (DECODING(proc_op)) {
		D_ALLOC(csum->cs_csum, csum->cs_buf_len);
		if (csum->cs_csum == NULL)
			return -DER_NOMEM;

		rc = crt_proc_memcpy(proc, proc_op, csum->cs_csum, csum->cs_buf_len);
		if (unlikely(rc)) {
			D_FREE(csum->cs_csum);
			return rc;
		}
	}

	return 0;
}

static int
proc_struct_dcs_csum_info(crt_proc_t proc, crt_proc_op_t proc_op,
			  struct dcs_csum_info *csum)
{
	if (csum == NULL)
		return 0;

	return proc_struct_dcs_csum_info_adv(proc, proc_op, csum, 0,
					     csum->cs_nr);
}

int
crt_proc_struct_dcs_csum_info(crt_proc_t proc, crt_proc_op_t proc_op,
			      struct dcs_csum_info **p_csum)
{
	bool			csum_enabled = 0;
	int			rc = 0;

	if (ENCODING(proc_op)) {
		csum_enabled = *p_csum != NULL;
		PROC(bool, &csum_enabled);
		if (csum_enabled) {
			rc = proc_struct_dcs_csum_info(proc, proc_op, *p_csum);
			if (unlikely(rc))
				return rc;
		}

		return 0;
	}

	if (DECODING(proc_op)) {
		PROC(bool, &csum_enabled);
		if (!csum_enabled) {
			*p_csum = NULL;
			return 0;
		}
		D_ALLOC_PTR(*p_csum);
		if (*p_csum == NULL)
			return -DER_NOMEM;
		rc = proc_struct_dcs_csum_info(proc, proc_op, *p_csum);
		if (unlikely(rc)) {
			D_FREE(*p_csum);
			return rc;
		}
	}

	if (FREEING(proc_op)) {
		rc = proc_struct_dcs_csum_info(proc, proc_op, *p_csum);
		D_FREE(*p_csum);
	}

	return rc;
}

/**
 * advanced iod_csums proc, can be used to proc partial data of the iod_csum
 * for EC obj.
 */
int
crt_proc_struct_dcs_iod_csums_adv(crt_proc_t proc, crt_proc_op_t proc_op,
				  struct dcs_iod_csums *iod_csum, bool singv,
				  uint32_t idx, uint32_t nr)
{
	struct dcs_csum_info	*singv_ci;
	int			 rc = 0, i;

	if (ENCODING(proc_op)) {
		if (iod_csum->ic_nr != 0) {
			D_ASSERT(nr <= iod_csum->ic_nr);
			if (!singv)
				D_ASSERT(idx < iod_csum->ic_nr);
		} else {
			/* only with akey csum */
			idx = 0;
			nr = 0;
		}
		PROC(uint32_t, &nr);
		if (singv) {
			D_ASSERT(nr == 1);
			D_ASSERT(iod_csum->ic_nr == 1);
			singv_ci = &iod_csum->ic_data[0];
			D_ASSERT(idx < singv_ci->cs_nr);
			rc = proc_struct_dcs_csum_info_adv(proc, proc_op, singv_ci, idx, 1);
			if (unlikely(rc))
				return rc;
		} else {
			for (i = idx; i < idx + nr; i++) {
				rc = proc_struct_dcs_csum_info(proc, proc_op,
							&iod_csum->ic_data[i]);
				if (unlikely(rc))
					return rc;
			}
		}
	}

	if (DECODING(proc_op)) {
		PROC(uint32_t, &iod_csum->ic_nr);
		if (iod_csum->ic_nr) {
			D_ALLOC_ARRAY(iod_csum->ic_data, iod_csum->ic_nr);
			if (iod_csum->ic_data == NULL)
				return -DER_NOMEM;
			for (i = 0; i < iod_csum->ic_nr; i++) {
				rc = proc_struct_dcs_csum_info(proc, proc_op,
							       &iod_csum->ic_data[i]);
				if (unlikely(rc)) {
					D_FREE(iod_csum->ic_data);
					return rc;
				}
			}
		}
	}

	if (FREEING(proc_op)) {
		for (i = 0; i < iod_csum->ic_nr; i++) {
			rc = proc_struct_dcs_csum_info(proc, proc_op,
						       &iod_csum->ic_data[i]);
			if (unlikely(rc))
				break;
		}
		if (rc != 0)
			return rc;
		D_FREE(iod_csum->ic_data);
	}

	rc = proc_struct_dcs_csum_info(proc, proc_op, &iod_csum->ic_akey);
	if (unlikely(rc)) {
		if (DECODING(proc_op))
			D_FREE(iod_csum->ic_data);
		return rc;
	}

	return rc;
}

int
crt_proc_struct_dcs_iod_csums(crt_proc_t proc, crt_proc_op_t proc_op,
			      struct dcs_iod_csums *iod_csum)
{
	return crt_proc_struct_dcs_iod_csums_adv(proc, proc_op, iod_csum, false,
						 0, iod_csum->ic_nr);
}
