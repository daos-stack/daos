/**
 * (C) Copyright 2016 Intel Corporation.
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

#include <cart/errno.h>

typedef enum {
	/**
	 * Common error codes
	 */
	/** First DAOS error	 (1000) */
	DER_ERR_FIRST		= CER_ERR_BASE,
	/** No permission	 (CER_ERR_BASE + 1) */
	DER_NO_PERM		= CER_NO_PERM,
	/** Invalid handle	 (CER_ERR_BASE + 2) */
	DER_NO_HDL		= CER_NO_HDL,
	/** Invalid parameters	 (CER_ERR_BASE + 3) */
	DER_INVAL		= CER_INVAL,
	/** Entity already exists (CER_ERR_BASE + 4) */
	DER_EXIST		= CER_EXIST,
	/** Nonexistent entity	 (CER_ERR_BASE + 5) */
	DER_NONEXIST		= CER_NONEXIST,
	/** Unreachable node	 (CER_ERR_BASE + 6) */
	DER_UNREACH		= CER_UNREACH,
	/** No space on storage target (CER_ERR_BASE + 7) */
	DER_NOSPACE		= CER_NOSPACE,
	/** Already did sth	 (CER_ERR_BASE + 8) */
	DER_ALREADY		= CER_ALREADY,
	/** NO memory		 (CER_ERR_BASE + 9) */
	DER_NOMEM		= CER_NOMEM,
	/** Function not implemented (CER_ERR_BASE + 10) */
	DER_NOSYS		= CER_NOSYS,
	/** Timed out		 (CER_ERR_BASE + 11) */
	DER_TIMEDOUT		= CER_TIMEDOUT,
	/** Busy		 (CER_ERR_BASE + 12) */
	DER_BUSY		= CER_BUSY,
	/** Try again		 (CER_ERR_BASE + 13) */
	DER_AGAIN		= CER_AGAIN,
	/** incompatible protocol (CER_ERR_BASE + 14) */
	DER_PROTO		= CER_PROTO,
	/** Un-initialized	 (CER_ERR_BASE + 15) */
	DER_UNINIT		= CER_UNINIT,
	/** Buffer too short, larger buffer needed (CER_ERR_BASE + 16) */
	DER_TRUNC		= CER_TRUNC,
	/** Value too large for defined data type (CER_ERR_BASE + 17) */
	DER_OVERFLOW		= CER_OVERFLOW,
	/** Operation cancelled	 (CER_ERR_BASE + 18) */
	DER_CANCELED		= CER_CANCELED,
	/** Out-Of-Group or member list (CER_ERR_BASE + 19) */
	DER_OOG			= CER_OOG,
	/** Transport layer mercury error (CER_ERR_BASE + 20) */
	DER_CRT_HG		= CER_HG,
	/** CRT RPC (opcode) unregister (CER_ERR_BASE + 21) */
	DER_CRT_UNREG		= CER_UNREG,
	/** CRT failed to generate an address string (CER_ERR_BASE + 22) */
	DER_CRT_ADDRSTR_GEN	= CER_ADDRSTR_GEN,
	/** CRT PMIx layer error (CER_ERR_BASE + 23) */
	DER_CRT_PMIX		= CER_PMIX,
	/** CRT IV callback - cannot handle locally (CER_ERR_BASE + 24) */
	DER_IVCB_FORWARD	= CER_IVCB_FORWARD,
	/** CRT miscellaneous error (CER_ERR_BASE + 25) */
	DER_MISC		= CER_MISC,
	/** Bad path name	 (CER_ERR_BASE + 26) */
	DER_BADPATH		= CER_BADPATH,
	/** Not a directory	 (CER_ERR_BASE + 27) */
	DER_NOTDIR		= CER_NOTDIR,
	/** Unknown error	 (CER_ERR_BASE + 500) */
	DER_UNKNOWN		= CER_UNKNOWN,

	/**
	 * DAOS-specific error codes
	 */
	DER_ERR_BASE		= 2000,
	/** Generic I/O error */
	DER_IO			= (DER_ERR_BASE + 1),
	/** Memory free error */
	DER_FREE_MEM		= (DER_ERR_BASE + 2),
	/** Entry not found */
	DER_ENOENT		= (DER_ERR_BASE + 3),
	/** Unknown object type */
	DER_NOTYPE		= (DER_ERR_BASE + 4),
	/** Unknown object schema */
	DER_NOSCHEMA		= (DER_ERR_BASE + 5),
	/** Object is not local */
	DER_NOLOCAL		= (DER_ERR_BASE + 6),
	/** stale pool map version */
	DER_STALE		= (DER_ERR_BASE + 7),
	/** Not service leader */
	DER_NOTLEADER		= (DER_ERR_BASE + 8),
	/** Target create error */
	DER_TGT_CREATE		= (DER_ERR_BASE + 100),
	/** Epoch is read-only */
	DER_EP_RO		= (DER_ERR_BASE + 200),
	/** Epoch is too old, all data have been recycled */
	DER_EP_OLD		= (DER_ERR_BASE + 201),
	/** Key is too large */
	DER_KEY2BIG		= (DER_ERR_BASE + 250),
	/** Record is too large */
	DER_REC2BIG		= (DER_ERR_BASE + 251),
	/** IO buffers can't match object extents */
	DER_IO_INVAL		= (DER_ERR_BASE + 300),
	/** Event queue is busy */
	DER_EQ_BUSY		= (DER_ERR_BASE + 400),
	/** Domain of cluster component can't match */
	DER_DOMAIN		= (DER_ERR_BASE + 500),
} daos_errno_t;

const char *daos_errstr(daos_errno_t errno);

#if defined(__cplusplus)
}
#endif

#endif /*  __DAOS_ERRNO_H__ */
