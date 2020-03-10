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

#ifndef __DAOS_RPC_CSUM_H__
#define __DAOS_RPC_CSUM_H__

int
crt_proc_struct_dcs_csum_info(crt_proc_t proc, struct dcs_csum_info **csum);

int
crt_proc_struct_dcs_iod_csums(crt_proc_t proc, struct dcs_iod_csums *iod_csum);

int
crt_proc_struct_dcs_iod_csums_adv(crt_proc_t proc, crt_proc_op_t proc_op,
				  struct dcs_iod_csums *iod_csum, bool singv,
				  uint32_t idx, uint32_t nr);

#endif /** __DAOS_RPC_CSUM_H__ */
