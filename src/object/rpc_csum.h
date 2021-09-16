/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_RPC_CSUM_H__
#define __DAOS_RPC_CSUM_H__

int
crt_proc_struct_dcs_csum_info(crt_proc_t proc, crt_proc_op_t proc_op,
			      struct dcs_csum_info **csum);

int
crt_proc_struct_dcs_iod_csums(crt_proc_t proc, crt_proc_op_t proc_op,
			      struct dcs_iod_csums *iod_csum);

int
crt_proc_struct_dcs_iod_csums_adv(crt_proc_t proc, crt_proc_op_t proc_op,
				  struct dcs_iod_csums *iod_csum, bool singv,
				  uint32_t idx, uint32_t nr);

#endif /** __DAOS_RPC_CSUM_H__ */
