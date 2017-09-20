/* Copyright (C) 2017 Intel Corporation
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
 * GURT Error numbers
 */

#if !defined(__GURT_ERRNO_H__) || defined(D_ERRNO_GEN_ERRSTR)
#define __GURT_ERRNO_H__

/*
 * This preprocessor machinery implements d_errstr() automatically (with
 * the help of d_errno.c) without duplicating the actual list of values
 *
 * If D_ERRNO_GEN_ERRSTR is NOT defined (default), this creates:
 *   typedef enum {
 *    ...
 *   } d_errno_t;
 *
 * If D_ERRNO_GEN_ERRSTR is defined (when included by d_errno.c),
 * this creates:
 *   const char *d_errstr(d_errno_t d_errno) {
 *     switch (d_errno) {
 *     case (...)
 *     default:
 *       return "Unknown d_errno_t";
 *     }
 *   }
 */

#ifndef D_ERRNO_GEN_ERRSTR
#define D_ERRNO_BEGIN_ENUM typedef enum {
#define D_ERRNO_DECL(name, value) name = value,
#define D_ERRNO_END_ENUM } d_errno_t;
#else
#define D_ERRNO_BEGIN_ENUM const char *d_errstr(d_errno_t d_errno) {   \
	switch (d_errno) {
#define D_ERRNO_DECL(name, value) case name: return #name;
#define D_ERRNO_END_ENUM default: return "Unknown d_errno_t"; } }
#endif

D_ERRNO_BEGIN_ENUM
	D_ERRNO_DECL(DER_ERR_BASE,		1000)
	/** no permission */
	D_ERRNO_DECL(DER_NO_PERM,		(DER_ERR_BASE + 1))
	/** invalid handle */
	D_ERRNO_DECL(DER_NO_HDL,		(DER_ERR_BASE + 2))
	/** invalid parameters */
	D_ERRNO_DECL(DER_INVAL,			(DER_ERR_BASE + 3))
	/** entity already exists */
	D_ERRNO_DECL(DER_EXIST,			(DER_ERR_BASE + 4))
	/** nonexistent entity */
	D_ERRNO_DECL(DER_NONEXIST,		(DER_ERR_BASE + 5))
	/** unreachable node */
	D_ERRNO_DECL(DER_UNREACH,		(DER_ERR_BASE + 6))
	/** no space on storage target */
	D_ERRNO_DECL(DER_NOSPACE,		(DER_ERR_BASE + 7))
	/** already did sth */
	D_ERRNO_DECL(DER_ALREADY,		(DER_ERR_BASE + 8))
	/** NO memory */
	D_ERRNO_DECL(DER_NOMEM,			(DER_ERR_BASE + 9))
	/** Function not implemented */
	D_ERRNO_DECL(DER_NOSYS,			(DER_ERR_BASE + 10))
	/** timed out */
	D_ERRNO_DECL(DER_TIMEDOUT,		(DER_ERR_BASE + 11))
	/** Busy */
	D_ERRNO_DECL(DER_BUSY,			(DER_ERR_BASE + 12))
	/** Try again */
	D_ERRNO_DECL(DER_AGAIN,			(DER_ERR_BASE + 13))
	/** incompatible protocol */
	D_ERRNO_DECL(DER_PROTO,			(DER_ERR_BASE + 14))
	/** not initialized */
	D_ERRNO_DECL(DER_UNINIT,		(DER_ERR_BASE + 15))
	/** buffer too short (larger buffer needed) */
	D_ERRNO_DECL(DER_TRUNC,			(DER_ERR_BASE + 16))
	/** value too large for defined data type */
	D_ERRNO_DECL(DER_OVERFLOW,		(DER_ERR_BASE + 17))
	/** operation canceled */
	D_ERRNO_DECL(DER_CANCELED,		(DER_ERR_BASE + 18))
	/** Out-Of-Group or member list */
	D_ERRNO_DECL(DER_OOG,			(DER_ERR_BASE + 19))
	/** transport layer mercury error */
	D_ERRNO_DECL(DER_HG,			(DER_ERR_BASE + 20))
	/** RPC (opcode) unregister */
	D_ERRNO_DECL(DER_UNREG,			(DER_ERR_BASE + 21))
	/** failed to generate an address string */
	D_ERRNO_DECL(DER_ADDRSTR_GEN,		(DER_ERR_BASE + 22))
	/** PMIx layer error */
	D_ERRNO_DECL(DER_PMIX,			(DER_ERR_BASE + 23))
	/** IV callback - cannot handle locally */
	D_ERRNO_DECL(DER_IVCB_FORWARD,		(DER_ERR_BASE + 24))
	/** miscellaneous error */
	D_ERRNO_DECL(DER_MISC,			(DER_ERR_BASE + 25))
	/** Bad path name */
	D_ERRNO_DECL(DER_BADPATH,		(DER_ERR_BASE + 26))
	/** Not a directory */
	D_ERRNO_DECL(DER_NOTDIR,		(DER_ERR_BASE + 27))
	/** corpc failed */
	D_ERRNO_DECL(DER_CORPC_INCOMPLETE,	(DER_ERR_BASE + 28))
	/** no rank is subscribed to RAS */
	D_ERRNO_DECL(DER_NO_RAS_RANK,		(DER_ERR_BASE + 29))
	/** service group not attached */
	D_ERRNO_DECL(DER_NOTATTACH,		(DER_ERR_BASE + 30))
	/** version mismatch */
	D_ERRNO_DECL(DER_MISMATCH,		(DER_ERR_BASE + 31))
	/** rank has been evicted */
	D_ERRNO_DECL(DER_EVICTED,		(DER_ERR_BASE + 32))
	/** user-provided RPC handler didn't send reply back */
	D_ERRNO_DECL(DER_NOREPLY,		(DER_ERR_BASE + 33))
	/** denial-of-service */
	D_ERRNO_DECL(DER_DOS,			(DER_ERR_BASE + 34))
	/** unknown error */
	D_ERRNO_DECL(DER_UNKNOWN,		(DER_ERR_BASE + 500))
	/** TODO: add more error numbers */
D_ERRNO_END_ENUM

#undef D_ERRNO_BEGIN_ENUM
#undef D_ERRNO_DECL
#undef D_ERRNO_END_ENUM

const char *d_errstr(d_errno_t d_errno);



#endif /*  __GURT_ERRNO_H__ */
