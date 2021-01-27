/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __IOIL_API_H__
#define __IOIL_API_H__

#include <stdbool.h>
#include "ioil_defines.h"

enum dfuse_bypass_status {
	DFUSE_IO_EXTERNAL = 0,	/** File is not forwarded by IOF */
	DFUSE_IO_BYPASS,		/** Kernel bypass is enabled */
	DFUSE_IO_DIS_MMAP,	/** Bypass disabled for mmap'd file */
	DFUSE_IO_DIS_FLAG,	/* Bypass is disabled for file because
				 *  O_APPEND or O_PATH was used
				 */
	DFUSE_IO_DIS_FCNTL,	/* Bypass is disabled for file because
				 * bypass doesn't support an fcntl
				 */
	DFUSE_IO_DIS_STREAM,	/* Bypass is disabled for file opened as a
				 * stream.
				 */
	DFUSE_IO_DIS_RSRC,	/* Bypass is disabled due to lack of
				 * resources in interception library
				 */
};

/** Return a value indicating the status of the file with respect to
 *  IOF.  Possible values are defined in /p enum dfuse_bypass_status
 */
DFUSE_PUBLIC int dfuse_get_bypass_status(int fd);

#endif /* __IOIL_API_H__ */
