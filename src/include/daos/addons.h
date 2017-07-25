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
 * Addons task functions.
 */

#ifndef __DAOS_ADDONS_H__
#define  __DAOS_ADDONS_H__

/* task functions for array operations */
int dac_array_create(struct daos_task *task);
int dac_array_open(struct daos_task *task);
int dac_array_close(struct daos_task *task);
int dac_array_read(struct daos_task *task);
int dac_array_write(struct daos_task *task);
int dac_array_get_size(struct daos_task *task);
int dac_array_set_size(struct daos_task *task);

/* task function for HL operations */
int dac_kv_get(struct daos_task *task);
int dac_kv_put(struct daos_task *task);
int dac_kv_remove(struct daos_task *task);
int dac_obj_fetch_multi(struct daos_task *task);
int dac_obj_update_multi(struct daos_task *task);
#endif /* __DAOS_ADDONS_H__ */
