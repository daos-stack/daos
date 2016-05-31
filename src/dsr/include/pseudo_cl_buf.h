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
 * This file is part of daos_sr
 *
 * dsr/include/pseudo_cl_map.h
 *
 * cl_pseudo_buf APIs provide simple interfaces to build a pseudo cl_buf_t as
 * cluster description for dsr/tests/pseudo_cluster(a simulator), or any
 * other testing programs that need to build a pseudo cluster.
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */

#ifndef __PSEUDO_CL_MAP_H__
#define __PSEUDO_CL_MAP_H__

#include <pl_map.h>
/**
 * descriptor for pseudo cluster map
 */
typedef struct {
	/** type of components */
	cl_comp_type_t          cd_type;
	/** number of components */
	unsigned int            cd_number;
	/** the start rank of component */
	unsigned int		cd_rank;
} cl_pseudo_comp_desc_t;

void cl_pseudo_buf_free(cl_buf_t *buf);
int cl_pseudo_buf_build(unsigned int ndesc, cl_pseudo_comp_desc_t *desc,
			bool create, cl_buf_t **buf_pp);

#endif /* __PSEUDO_CL_MAP_H__ */
