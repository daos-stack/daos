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
#ifndef __DAOS_CLIENT_H__
#define __DAOS_CLIENT_H__

int
daos_rebuild_tgt(uuid_t pool_uuid, daos_rank_list_t *failed_list,
		 daos_event_t *ev);

int
daos_rebuild_fini(uuid_t pool_uuid, daos_rank_list_t *failed_list,
		  daos_event_t *ev);

int
daos_rebuild_query(uuid_t pool_uuid, daos_rank_list_t *failed_list,
		   int *done, int *failed, unsigned int *rec_count,
		   unsigned int *obj_count, daos_event_t *ev);
#endif
