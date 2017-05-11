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

#if !defined(__CRT_ERRNO_H__) || defined(CRT_ERRNO_GEN_ERRSTR)
#define __CRT_ERRNO_H__

/*
 * This preprocessor machinery implements crt_errstr() automatically (with
 * the help of crt_errno.c) without duplicating the actual list of values
 *
 * If CRT_ERRNO_GEN_ERRSTR is NOT defined (default), this creates:
 *   typedef enum {
 *    ...
 *   } crt_errno_t;
 *
 * If CRT_ERRNO_GEN_ERRSTR is defined (when included by crt_errno.c),
 * this creates:
 *   const char *crt_errstr(crt_errno_t crt_errno) {
 *     switch (crt_errno) {
 *     case (...)
 *     default:
 *       return "Unknown crt_errno_t";
 *     }
 *   }
 */

#ifndef CRT_ERRNO_GEN_ERRSTR
#define CRT_ERRNO_BEGIN_ENUM typedef enum {
#define CRT_ERRNO_DECL(name, value) name = value,
#define CRT_ERRNO_END_ENUM } crt_errno_t;
#else
#define CRT_ERRNO_BEGIN_ENUM const char *crt_errstr(crt_errno_t crt_errno) {   \
	switch (crt_errno) {
#define CRT_ERRNO_DECL(name, value) case name: return #name;
#define CRT_ERRNO_END_ENUM default: return "Unknown crt_errno_t"; } }
#endif

CRT_ERRNO_BEGIN_ENUM
	CRT_ERRNO_DECL(CER_ERR_BASE,		1000)
	/** no permission */
	CRT_ERRNO_DECL(CER_NO_PERM,		(CER_ERR_BASE + 1))
	/** invalid handle */
	CRT_ERRNO_DECL(CER_NO_HDL,		(CER_ERR_BASE + 2))
	/** invalid parameters */
	CRT_ERRNO_DECL(CER_INVAL,		(CER_ERR_BASE + 3))
	/** entity already exists */
	CRT_ERRNO_DECL(CER_EXIST,		(CER_ERR_BASE + 4))
	/** nonexistent entity */
	CRT_ERRNO_DECL(CER_NONEXIST,		(CER_ERR_BASE + 5))
	/** unreachable node */
	CRT_ERRNO_DECL(CER_UNREACH,		(CER_ERR_BASE + 6))
	/** no space on storage target */
	CRT_ERRNO_DECL(CER_NOSPACE,		(CER_ERR_BASE + 7))
	/** already did sth */
	CRT_ERRNO_DECL(CER_ALREADY,		(CER_ERR_BASE + 8))
	/** NO memory */
	CRT_ERRNO_DECL(CER_NOMEM,		(CER_ERR_BASE + 9))
	/** Function not implemented */
	CRT_ERRNO_DECL(CER_NOSYS,		(CER_ERR_BASE + 10))
	/** timed out */
	CRT_ERRNO_DECL(CER_TIMEDOUT,		(CER_ERR_BASE + 11))
	/** Busy */
	CRT_ERRNO_DECL(CER_BUSY,		(CER_ERR_BASE + 12))
	/** Try again */
	CRT_ERRNO_DECL(CER_AGAIN,		(CER_ERR_BASE + 13))
	/** incompatible protocol */
	CRT_ERRNO_DECL(CER_PROTO,		(CER_ERR_BASE + 14))
	/** not initialized */
	CRT_ERRNO_DECL(CER_UNINIT,		(CER_ERR_BASE + 15))
	/** buffer too short (larger buffer needed) */
	CRT_ERRNO_DECL(CER_TRUNC,		(CER_ERR_BASE + 16))
	/** value too large for defined data type */
	CRT_ERRNO_DECL(CER_OVERFLOW,		(CER_ERR_BASE + 17))
	/** operation canceled */
	CRT_ERRNO_DECL(CER_CANCELED,		(CER_ERR_BASE + 18))
	/** Out-Of-Group or member list */
	CRT_ERRNO_DECL(CER_OOG,			(CER_ERR_BASE + 19))
	/** transport layer mercury error */
	CRT_ERRNO_DECL(CER_HG,			(CER_ERR_BASE + 20))
	/** CRT RPC (opcode) unregister */
	CRT_ERRNO_DECL(CER_UNREG,		(CER_ERR_BASE + 21))
	/** CRT failed to generate an address string */
	CRT_ERRNO_DECL(CER_ADDRSTR_GEN,		(CER_ERR_BASE + 22))
	/** CRT PMIx layer error */
	CRT_ERRNO_DECL(CER_PMIX,		(CER_ERR_BASE + 23))
	/** CRT IV callback - cannot handle locally */
	CRT_ERRNO_DECL(CER_IVCB_FORWARD,	(CER_ERR_BASE + 24))
	/** CRT miscellaneous error */
	CRT_ERRNO_DECL(CER_MISC,		(CER_ERR_BASE + 25))
	/** Bad path name */
	CRT_ERRNO_DECL(CER_BADPATH,		(CER_ERR_BASE + 26))
	/** Not a directory */
	CRT_ERRNO_DECL(CER_NOTDIR,		(CER_ERR_BASE + 27))
	/** corpc failed */
	CRT_ERRNO_DECL(CER_CORPC_INCOMPLETE,	(CER_ERR_BASE + 28))
	/** no rank is subscribed to RAS */
	CRT_ERRNO_DECL(CER_NO_RAS_RANK,		(CER_ERR_BASE + 29))
	/** service group not attached */
	CRT_ERRNO_DECL(CER_NOTATTACH,		(CER_ERR_BASE + 30))
	/** version mismatch */
	CRT_ERRNO_DECL(CER_MISMATCH,		(CER_ERR_BASE + 31))
	/** rank has been evicted */
	CRT_ERRNO_DECL(CER_EVICTED,		(CER_ERR_BASE + 32))
	/** user-provided RPC handler didn't send reply back */
	CRT_ERRNO_DECL(CER_NOREPLY,		(CER_ERR_BASE + 33))
	/** denial-of-service */
	CRT_ERRNO_DECL(CER_DOS,			(CER_ERR_BASE + 34))
	/** unknown error */
	CRT_ERRNO_DECL(CER_UNKNOWN,		(CER_ERR_BASE + 500))
	/** TODO: add more error numbers */
CRT_ERRNO_END_ENUM

#undef CRT_ERRNO_BEGIN_ENUM
#undef CRT_ERRNO_DECL
#undef CRT_ERRNO_END_ENUM

const char *crt_errstr(crt_errno_t crt_errno);

#endif /*  __CRT_ERRNO_H__ */
