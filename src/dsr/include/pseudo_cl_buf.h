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
 * (C) Copyright 2015 Intel Corporation.
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
