/**
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * DAOS Error numbers
 */

#ifndef __DAOS_ERRNO_H__
#define __DAOS_ERRNO_H__

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef __GURT_ERRNO_H__
#ifdef DAOS_VERSION
#error "daos_errno.h included after gurt/errno.h"
#endif
/* Detects when a 3rd party user includes gurt/errno.h first */
#define DAOS_USE_GURT_ERRNO
#endif

#define D_ERRNO_V2
#include <gurt/errno.h>

#undef D_FOREACH_DAOS_ERR

#define D_FOREACH_DAOS_ERR(ACTION)					\
	/** Generic I/O error */					\
	ACTION(DER_IO,			(DER_ERR_DAOS_BASE + 1))	\
	/** Memory free error */					\
	ACTION(DER_FREE_MEM,		(DER_ERR_DAOS_BASE + 2))	\
	/** Entry not found */						\
	ACTION(DER_ENOENT,		(DER_ERR_DAOS_BASE + 3))	\
	/** Unknown object type */					\
	ACTION(DER_NOTYPE,		(DER_ERR_DAOS_BASE + 4))	\
	/** Unknown object schema */					\
	ACTION(DER_NOSCHEMA,		(DER_ERR_DAOS_BASE + 5))	\
	/** Object is not local */					\
	ACTION(DER_NOLOCAL,		(DER_ERR_DAOS_BASE + 6))	\
	/** stale pool map version */					\
	ACTION(DER_STALE,		(DER_ERR_DAOS_BASE + 7))	\
	/** Not service leader */					\
	ACTION(DER_NOTLEADER,		(DER_ERR_DAOS_BASE + 8))	\
	/** * Target create error */					\
	ACTION(DER_TGT_CREATE,		(DER_ERR_DAOS_BASE + 9))	\
	/** Epoch is read-only */					\
	ACTION(DER_EP_RO,		(DER_ERR_DAOS_BASE + 10))	\
	/** Epoch is too old, all data have been recycled */		\
	ACTION(DER_EP_OLD,		(DER_ERR_DAOS_BASE + 11))	\
	/** Key is too large */						\
	ACTION(DER_KEY2BIG,		(DER_ERR_DAOS_BASE + 12))	\
	/** Record is too large */					\
	ACTION(DER_REC2BIG,		(DER_ERR_DAOS_BASE + 13))	\
	/** IO buffers can't match object extents */			\
	ACTION(DER_IO_INVAL,		(DER_ERR_DAOS_BASE + 14))	\
	/** Event queue is busy */					\
	ACTION(DER_EQ_BUSY,		(DER_ERR_DAOS_BASE + 15))	\
	/** Domain of cluster component can't match */			\
	ACTION(DER_DOMAIN,		(DER_ERR_DAOS_BASE + 16))	\
	/** Service should shut down */					\
	ACTION(DER_SHUTDOWN,		(DER_ERR_DAOS_BASE + 17))	\
	/** Operation now in progress */				\
	ACTION(DER_INPROGRESS,		(DER_ERR_DAOS_BASE + 18))	\
	/** Not applicable. */						\
	ACTION(DER_NOTAPPLICABLE,	(DER_ERR_DAOS_BASE + 19))	\
	/** Not a service replica */					\
	ACTION(DER_NOTREPLICA,		(DER_ERR_DAOS_BASE + 20))	\
	/** Checksum error */						\
	ACTION(DER_CSUM,		(DER_ERR_DAOS_BASE + 21))	\
	/** Unsupported durable format */				\
	ACTION(DER_DF_INVAL,		(DER_ERR_DAOS_BASE + 22))	\
	/** Incompatible durable format version */			\
	ACTION(DER_DF_INCOMPT,		(DER_ERR_DAOS_BASE + 23))	\
	/** Record size error */					\
	ACTION(DER_REC_SIZE,		(DER_ERR_DAOS_BASE + 24))	\
	/** XXX: Use it only when needs to restart the DTX */		\
	ACTION(DER_RESTART,		(DER_ERR_DAOS_BASE + 25))

#ifdef DAOS_USE_GURT_ERRNO
	/* When new errno's added above, we need to define them here
	 * as well numerically.   For instance, if we add a new one named
	 * DER_FOO, we would put this here:
	 *
	 * #define DER_FOO (DER_ERR_DAOS_BASE + 25)
	 *
	 * This isn't strictly necessary as it's only needed for external
	 * support if they reference an errno directly by name.  If they
	 * use d_errstr(errno), they will get the correct string in such
	 * cases.
	 */
#else
/** Define the DAOS error numbers */
D_DEFINE_RANGE_ERRNO(DAOS, 2000)
#endif

/** Register the DAOS error codes with gurt */
int
daos_errno_init(void);

/** Deregister the DAOS error codes with gurt */
void
daos_errno_fini(void);

#ifndef DF_RC
#define DF_RC "%s(%d)"
#define DP_RC(rc) d_errstr(rc), rc
#endif /* DF_RC */

#if defined(__cplusplus)
}
#endif

#endif /*  __DAOS_ERRNO_H__ */
