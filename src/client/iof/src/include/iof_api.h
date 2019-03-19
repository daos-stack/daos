/* Copyright (C) 2017-2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __IOF_API_H__
#define __IOF_API_H__

#include <stdbool.h>
#include <iof_defines.h>

#if defined(__cplusplus)
extern "C" {
#endif

enum iof_bypass_status {
	IOF_IO_EXTERNAL = 0,	/** File is not forwarded by IOF */
	IOF_IO_BYPASS,		/** Kernel bypass is enabled */
	IOF_IO_DIS_MMAP,	/** Bypass disabled for mmap'd file */
	IOF_IO_DIS_FLAG,	/* Bypass is disabled for file because
				 *  O_APPEND or O_PATH was used
				 */
	IOF_IO_DIS_FCNTL,	/* Bypass is disabled for file because
				 * bypass doesn't support an fcntl
				 */
	IOF_IO_DIS_STREAM,	/* Bypass is disabled for file opened as a
				 * stream.
				 */
	IOF_IO_DIS_RSRC,	/* Bypass is disabled due to lack of
				 * resources in interception library
				 */
};

/** Return a value indicating the status of the file with respect to
 *  IOF.  Possible values are defined in /p enum iof_bypass_status
 */
IOF_PUBLIC int iof_get_bypass_status(int fd);

#endif /* __IOF_IO_H__ */
