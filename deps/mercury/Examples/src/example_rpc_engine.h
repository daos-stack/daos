/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef EXAMPLE_RPC_ENGINE_H
#define EXAMPLE_RPC_ENGINE_H

#include <mercury.h>

/* example_rpc_engine: API of generic utilities and progress engine hooks that
 * are reused across many RPC functions.  init and finalize() manage a
 * dedicated thread that will drive all HG progress
 */
void
hg_engine_init(hg_bool_t listen, const char *local_addr);
void
hg_engine_finalize(void);
hg_class_t *
hg_engine_get_class(void);
void
hg_engine_print_self_addr(void);
void
hg_engine_addr_lookup(const char *name, hg_addr_t *addr);
void
hg_engine_addr_free(hg_addr_t addr);
void
hg_engine_create_handle(hg_addr_t addr, hg_id_t id, hg_handle_t *handle);

#endif /* EXAMPLE_RPC_ENGINE_H */
