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
 * This file is part of daos_sr
 *
 * src/dsr/dsr_internal.h
 */
#ifndef __DSR_INTENRAL_H__
#define __DSR_INTENRAL_H__

#include <daos/common.h>
#include <daos/event.h>
#include <daos_sr.h>
#include "dsr_types.h"
#include "placement.h"

struct daos_oclass_attr *dsr_oclass_attr_find(daos_obj_id_t oid);
int dsr_oclass_grp_size(struct daos_oclass_attr *oc_attr);
int dsr_oclass_grp_nr(struct daos_oclass_attr *oc_attr, struct dsr_obj_md *md);

/* XXX These functions should be changed to support per-pool
 * placement map.
 */
void dsr_pl_map_fini(void);
int  dsr_pl_map_init(struct pool_map *po_map);
struct pl_map *dsr_pl_map_find(daos_handle_t coh, daos_obj_id_t oid);

#endif /* __DSR_INTENRAL_H__ */
