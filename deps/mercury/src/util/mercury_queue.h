/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MERCURY_QUEUE_H
#define MERCURY_QUEUE_H

#include "mercury_util_config.h"

#ifdef HG_UTIL_HAS_SYSQUEUE_H
#    include <sys/queue.h>
#else
#    include "mercury_sys_queue.h"
#endif

#endif /* MERCURY_QUEUE_H */
