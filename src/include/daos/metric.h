/*
 * (C) Copyright 2020-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DAOS_METRIC_H__
#define __DAOS_METRIC_H__

/**
 *  Called during library initialization to init metrics.
 */
int dc_tm_init(void);

/**
 *  Called during library finalization to free metrics resources
 */
void dc_tm_fini(void);

#endif /* __DAOS_TM_H__ */
