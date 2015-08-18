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
