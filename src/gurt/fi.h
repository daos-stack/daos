/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * This file is part of gurt, it contains internal variables and functions for
 * the  fault injection feature.
 */

#ifndef __FI_H__
#define __FI_H__

/** @addtogroup GURT
 * @{
 */

#if defined(__cplusplus)
extern "C" {
#endif

struct d_fault_attr {
	d_list_t		fa_link;
	struct d_fault_attr_t	fa_attr;
};

#if defined(__cplusplus)
}
#endif

/** @}
 */
#endif /* __FI_H__ */
