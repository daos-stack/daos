/**
 * (C) Copyright 2017-2019 Intel Corporation.
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
 * Array task functions.
 */

#ifndef __DAOS_ARRAYX_H__
#define  __DAOS_ARRAYX_H__

/* task functions for array operations */
int dc_array_create(tse_task_t *task);
int dc_array_open(tse_task_t *task);
int dc_array_close(tse_task_t *task);
int dc_array_destroy(tse_task_t *task);
int dc_array_get_attr(daos_handle_t oh, daos_size_t *chunk_size,
		      daos_size_t *cell_size);
int dc_array_read(tse_task_t *task);
int dc_array_write(tse_task_t *task);
int dc_array_punch(tse_task_t *task);
int dc_array_get_size(tse_task_t *task);
int dc_array_set_size(tse_task_t *task);
int dc_array_local2global(daos_handle_t oh, d_iov_t *glob);
int dc_array_global2local(daos_handle_t coh, d_iov_t glob, unsigned int mode,
			  daos_handle_t *oh);

#endif /* __DAOS_ARRAYX_H__ */
