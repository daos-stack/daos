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
 * dc_cont: Container Client API
 *
 * This consists of dc_cont methods that do not belong to DAOS API.
 */

#ifndef __DAOS_CONTAINER_H__
#define __DAOS_CONTAINER_H__

#include <daos_types.h>
#include <daos/pool_map.h>

int dc_cont_init(void);
void dc_cont_fini(void);

int dc_cont_tgt_idx2pool_tgt(daos_handle_t coh, struct pool_target **tgt,
			     uint32_t tgt_idx);

int dc_cont_hdl2uuid_map_ver(daos_handle_t coh, uuid_t *con_hdl,
			      uint32_t *ver);
#endif /* __DAOS_CONTAINER_H__ */
