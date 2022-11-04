/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dc_cont: Container Client Internal Declarations
 */

#ifndef __CONTAINER_CLIENT_INTERNAL_H__
#define __CONTAINER_CLIENT_INTERNAL_H__

void dc_cont_hdl_link(struct dc_cont *dc);
void dc_cont_hdl_unlink(struct dc_cont *dc);

struct dc_cont *dc_cont_alloc(const uuid_t uuid);
void dc_cont_put(struct dc_cont *dc);

#endif /* __CONTAINER_CLIENT_INTERNAL_H__ */
