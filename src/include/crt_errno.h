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
 * CaRT Error numbers
 */

#ifndef __CRT_ERRNO_H__
#define __CRT_ERRNO_H__

typedef enum {
	CER_ERR_BASE		= 1000,
	/** no permission */
	CER_NO_PERM		= (CER_ERR_BASE + 1),
	/** invalid handle */
	CER_NO_HDL		= (CER_ERR_BASE + 2),
	/** invalid parameters */
	CER_INVAL		= (CER_ERR_BASE + 3),
	/** entity already exists */
	CER_EXIST		= (CER_ERR_BASE + 4),
	/** nonexistent entity */
	CER_NONEXIST		= (CER_ERR_BASE + 5),
	/** unreachable node */
	CER_UNREACH		= (CER_ERR_BASE + 6),
	/** no space on storage target */
	CER_NOSPACE		= (CER_ERR_BASE + 7),
	/** already did sth */
	CER_ALREADY		= (CER_ERR_BASE + 8),
	/** NO memory */
	CER_NOMEM		= (CER_ERR_BASE + 9),
	/** Function not implemented */
	CER_NOSYS		= (CER_ERR_BASE + 10),
	/** timed out */
	CER_TIMEDOUT		= (CER_ERR_BASE + 11),
	/** Busy */
	CER_BUSY		= (CER_ERR_BASE + 12),
	/** Try again */
	CER_AGAIN		= (CER_ERR_BASE + 13),
	/** incompatible protocol */
	CER_PROTO		= (CER_ERR_BASE + 14),
	/** un-initialized */
	CER_UNINIT		= (CER_ERR_BASE + 15),
	/** buffer too short (larger buffer needed) */
	CER_TRUNC		= (CER_ERR_BASE + 16),
	/** value too large for defined data type */
	CER_OVERFLOW		= (CER_ERR_BASE + 17),
	/** operation canceled */
	CER_CANCELED		= (CER_ERR_BASE + 18),
	/** Out-Of-Group or member list */
	CER_OOG			= (CER_ERR_BASE + 19),
	/** transport layer mercury error */
	CER_HG			= (CER_ERR_BASE + 20),
	/** CRT RPC (opcode) unregister */
	CER_UNREG		= (CER_ERR_BASE + 21),
	/** CRT failed to generate an address string */
	CER_ADDRSTR_GEN		= (CER_ERR_BASE + 22),
	/** CRT MCL layer error */
	CER_MCL			= (CER_ERR_BASE + 33),
	/** unknown error */
	CER_UNKNOWN		= (CER_ERR_BASE + 500),
	/** TODO: add more error numbers */
} crt_errno_t;

const char *crt_errstr(crt_errno_t errno);

#endif /*  __CRT_ERRNO_H__ */
