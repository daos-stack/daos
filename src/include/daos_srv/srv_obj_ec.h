/**
 * (C) Copyright 2019-2021 Intel Corporation.
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

#ifndef __DAOS_SRV_OBJ_EC_H__
#define __DAOS_SRV_OBJ_EC_H__

#include <daos_srv/container.h>

int
ds_obj_ec_aggregate(struct ds_cont_child *cont, daos_epoch_range_t *epr,
		    bool (*yield_func)(void *arg), void *yield_arg,
		    bool is_current);

#endif /* __DAOS_SRV_OBJ_EC_H__ */
