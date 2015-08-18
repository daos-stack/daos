/**
 * (C) Copyright 2015 Intel Corporation.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */
/**
 * This file is part of daos_sr
 *
 * dsr/include/pseudo_cl_map.h
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
