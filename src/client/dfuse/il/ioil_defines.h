/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __IOIL_DEFINES_H__
#define __IOIL_DEFINES_H__

#include <inttypes.h>

#ifdef DFUSE_DECLARE_WEAK
/* For LD_PRELOAD, declaring public symbols as weak allows 3rd
 * party libraries to use the headers without knowing beforehand
 * if the ioil libraries will be present at runtime
 */
#define DFUSE_PUBLIC __attribute__((visibility("default"), weak))
#else
#define DFUSE_PUBLIC __attribute__((visibility("default")))
#endif

#endif /* __IOIL_DEFINES_H__ */
