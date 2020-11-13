/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
 * dc_debug: Client debug API
 */

#ifndef __DC_DEBUG_H__
#define __DC_DEBUG_H__

#include <daos/common.h>
#include <daos/tse.h>
#include <daos_types.h>

int dc_mgmt_set_params(tse_task_t *task);
int dc_mgmt_add_mark(const char *mark);

/**
 * Set parameter on servers.
 *
 * \param grp	[IN]	Process set name of the DAOS servers managing the pool
 * \param rank	[IN]	Ranks to set parameter. -1 means setting on all servers.
 * \param key_id [IN]	key ID of the parameter.
 * \param value [IN]	value of the parameter.
 * \param value_extra [IN]
 *			optional extra value to set the fail value when
 *			\a key_id is DMG_CMD_FAIL_LOC and \a value is in
 *			DAOS_FAIL_VALUE mode.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_mgmt_set_params(const char *grp, d_rank_t rank, unsigned int key_id,
		     uint64_t value, uint64_t value_extra, daos_event_t *ev);

/**
 * Add mark to servers.
 *
 * \param mark	[IN]	mark to add to the debug log.
 */
int
daos_mgmt_add_mark(const char *mark);

#endif
