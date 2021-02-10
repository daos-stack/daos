/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * DAOS API
 */

#ifndef __DAOS_H__
#define __DAOS_H__

#include <daos_version.h>
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
 * Should be invoked prior to any DAOS calls. Can be called multiple times.
 */
int
daos_init(void);

/**
 * Finalize the DAOS library.
 * Should be invoked only when daos_init() was previously successfully executed.
 * An internal reference count is maintained by the library that will tear down
 * the DAOS stack on the last call to daos_fini()
 */
int
daos_fini(void);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_H__ */
