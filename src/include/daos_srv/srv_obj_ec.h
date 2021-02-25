/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_SRV_OBJ_EC_H__
#define __DAOS_SRV_OBJ_EC_H__

#include <daos_srv/container.h>

int
ds_obj_ec_aggregate(struct ds_cont_child *cont, daos_epoch_range_t *epr,
		    bool (*yield_func)(void *arg), void *yield_arg,
		    bool is_current);

#endif /* __DAOS_SRV_OBJ_EC_H__ */
