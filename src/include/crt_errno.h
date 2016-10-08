/* Copyright (C) 2016 Intel Corporation
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
	/** not initialized */
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
	/** CRT PMIx layer error */
	CER_PMIX		= (CER_ERR_BASE + 23),
	/** CRT IV callback - cannot handle locally */
	CER_IVCB_FORWARD	= (CER_ERR_BASE + 24),
	/** CRT miscellaneous error */
	CER_MISC		= (CER_ERR_BASE + 25),
	/** unknown error */
	CER_UNKNOWN		= (CER_ERR_BASE + 500),
	/** TODO: add more error numbers */
} crt_errno_t;

const char *crt_errstr(crt_errno_t errno);

#endif /*  __CRT_ERRNO_H__ */
