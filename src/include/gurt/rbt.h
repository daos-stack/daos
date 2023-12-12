/*
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */


#ifndef __GURT_RBT_H__
#define __GURT_RBT_H__

#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

/** @addtogroup GURT
 * @{
 */
/******************************************************************************
 * Generic Red Black Tree APIs / data structures
 ******************************************************************************/

struct d_rbt;

int
d_rbt_create(struct d_rbt **rbt,
	     int (*cmp_key)(const void *, const void *),
	     void (*free_key)(void *),
	     void (*free_data)(void *));
void
d_rbt_destroy(struct d_rbt *rbt, bool destroy_record);
int
d_rbt_insert(struct d_rbt *rbt, void *key, void *data, bool overwrite);
int
d_rbt_find(void **data, const struct d_rbt *rbt, const void *key);
int
d_rbt_delete(struct d_rbt *rbt, const void *key, bool destroy_record);


size_t
d_rbt_get_depth_min(const struct d_rbt *rbt);
size_t
d_rbt_get_depth_max(const struct d_rbt *rbt);
void*
d_rbt_get_key_min(const struct d_rbt *rbt);
void*
d_rbt_get_key_max(const struct d_rbt *rbt);
bool
d_rbt_is_ordered(const struct d_rbt *rbt);
size_t
d_rbt_get_black_height(const struct d_rbt *rbt);
void
d_rbt_print(const struct d_rbt *rbt, void (*print_record)(const void *, const void *));

/** @}
*/

#if defined(__cplusplus)
}
#endif

#endif /* __GURT_RBT_H__ */
