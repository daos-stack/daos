/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2018-2020, Intel Corporation */

/*
 * critnib.h -- internal definitions for critnib tree
 */

#ifndef __DAOS_COMMON_CRITNIB_H
#define __DAOS_COMMON_CRITNIB_H 1

#include <stdint.h>

struct critnib;

struct critnib *critnib_new(void);
void critnib_delete(struct critnib *c);

int critnib_insert(struct critnib *c, uint64_t key, void *value);
void *critnib_remove(struct critnib *c, uint64_t key);
void *critnib_get(struct critnib *c, uint64_t key);
void *critnib_find_le(struct critnib *c, uint64_t key);

#endif /* __DAOS_COMMON_CRITNIB_H */
