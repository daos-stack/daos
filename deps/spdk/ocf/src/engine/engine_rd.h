/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef ENGINE_RD_H_
#define ENGINE_RD_H_

int ocf_read_generic(struct ocf_request *req);

void ocf_read_generic_submit_hit(struct ocf_request *req);

#endif /* ENGINE_RD_H_ */
