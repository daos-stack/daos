/**
 * (C) Copyright 2017-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
int dc_array_close_direct(daos_handle_t oh);
int dc_array_destroy(tse_task_t *task);
int dc_array_get_attr(daos_handle_t oh, daos_size_t *chunk_size, daos_size_t *cell_size);
int dc_array_read(tse_task_t *task);
int dc_array_write(tse_task_t *task);
int dc_array_punch(tse_task_t *task);
int dc_array_get_size(tse_task_t *task);
int dc_array_stat(tse_task_t *task);
int dc_array_set_size(tse_task_t *task);
int dc_array_local2global(daos_handle_t oh, d_iov_t *glob);
int dc_array_global2local(daos_handle_t coh, d_iov_t glob, unsigned int mode, daos_handle_t *oh);
int dc_array_update_chunk_size(daos_handle_t oh, daos_size_t chunk_size);

#endif /* __DAOS_ARRAYX_H__ */
