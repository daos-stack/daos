/*
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
 */
/**
 * \file
 *
 * DAOS API
 */

#ifndef __DAOS_H__
#define __DAOS_H__

#include <daos_types.h>
#include <daos_event.h>

#include <daos_obj.h>
#include <daos_array.h>
#include <daos_kv.h>
#include <daos_prop.h>
#include <daos_cont.h>
#include <daos_pool.h>
#include <daos_mgmt.h>
#include <daos_security.h>

#include <daos_api.h>
#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Initialize the DAOS library.
 */
int
daos_init(void);

/**
 * Finalize the DAOS library.
 */
int
daos_fini(void);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_H__ */
