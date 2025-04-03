/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MERCURY_PROC_BULK_H
#define MERCURY_PROC_BULK_H

#include "mercury_proc.h"

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

/*****************/
/* Public Macros */
/*****************/

/*********************/
/* Public Prototypes */
/*********************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Generic processing routine.
 *
 * \param proc [IN/OUT]         abstract processor object
 * \param handle [IN/OUT]       pointer to bulk handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PUBLIC hg_return_t
hg_proc_hg_bulk_t(hg_proc_t proc, void *data);

#ifdef __cplusplus
}
#endif

#endif /* MERCURY_PROC_BULK_H */
