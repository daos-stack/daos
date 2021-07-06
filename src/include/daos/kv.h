/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Kv task functions.
 */

#ifndef __DAOS_KVX_H__
#define  __DAOS_KVX_H__

/* task function for HL operations */
int dc_kv_open(tse_task_t *task);
int dc_kv_close(tse_task_t *task);
int dc_kv_destroy(tse_task_t *task);
int dc_kv_get(tse_task_t *task);
int dc_kv_put(tse_task_t *task);
int dc_kv_remove(tse_task_t *task);
int dc_kv_list(tse_task_t *task);

#endif /* __DAOS_KVX_H__ */
