/**
 * (C) Copyright 2024-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_ABT_H__
#define __DAOS_ABT_H__

#include <abt.h>

int
da_initialize(int argc, char *argv[]);
void
da_finalize(void);

/* XXX presently ABT_thread_create_[to,many]() are not used in DAOS code, but if it becomes we will
 * also have to introduce a corresponding wrapper
 */
int
da_thread_create_on_pool(ABT_pool, void (*)(void *), void *, ABT_thread_attr, ABT_thread *);
int
da_thread_create_on_xstream(ABT_xstream, void (*)(void *), void *, ABT_thread_attr, ABT_thread *);

int
da_thread_get_func(ABT_thread, void (**)(void *));
int
da_thread_get_arg(ABT_thread, void **);

#endif /* __DAOS_ABT_H__ */
