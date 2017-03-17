/**
 * (C) Copyright 2017 Intel Corporation.
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
 * Array internal data structures and routines.
 */

#ifndef __DAOS_ARRAY_INTERNAL_H__
#define  __DAOS_ARRAY_INTERNAL_H__

#include <daos_types.h>
#include <daos_task.h>

struct dac_array_create_t {
	daos_handle_t	coh;
	daos_obj_id_t	oid;
	daos_epoch_t	epoch;
	daos_size_t	cell_size;
	daos_size_t	block_size;
	daos_handle_t	*oh;
};

struct dac_array_open_t {
	daos_handle_t	coh;
	daos_obj_id_t	oid;
	daos_epoch_t	epoch;
	unsigned int	mode;
	daos_size_t	*cell_size;
	daos_size_t	*block_size;
	daos_handle_t	*oh;
};

struct dac_array_close_t {
	daos_handle_t	oh;
};

enum array_op_t {
	D_ARRAY_OP_WRITE,
	D_ARRAY_OP_READ
};

struct dac_array_io_t {
	enum array_op_t		op;
	daos_handle_t		oh;
	daos_epoch_t		epoch;
	daos_array_ranges_t	*ranges;
	daos_sg_list_t		*sgl;
	daos_csum_buf_t		*csums;
};

struct dac_array_get_size_t {
	daos_handle_t           oh;
	daos_epoch_t            epoch;
	daos_size_t		*size;
};

struct dac_array_set_size_t {
	daos_handle_t           oh;
	daos_epoch_t            epoch;
	daos_size_t		size;
};

/* task functions for array operations */
int dac_array_create(struct daos_task *task);
int dac_array_open(struct daos_task *task);
int dac_array_close(struct daos_task *task);
int dac_array_io(struct daos_task *task);
int dac_array_get_size(struct daos_task *task);
int dac_array_set_size(struct daos_task *task);
#endif /* __DAOS_ARRAY_INTERNAL_H__ */
