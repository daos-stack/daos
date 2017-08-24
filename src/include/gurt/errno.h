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

#if !defined(__GURT_ERRNO_H__) || defined(DERRNO_GEN_ERRSTR)
#define __GURT_ERRNO_H__

#ifndef DERRNO_GEN_ERRSTR
#define DERRNO_BEGIN_ENUM typedef enum {
#define DERRNO_DECL(name, value) name = value,
#define DERRNO_END_ENUM } d_errno_t;
#else
#define DERRNO_BEGIN_ENUM const char *d_errstr(d_errno_t d_errno) {   \
	switch (d_errno) {
#define DERRNO_DECL(name, value) case name: return #name;
#define DERRNO_END_ENUM default: return "Unknown d_errno_t"; } }
#endif

DERRNO_BEGIN_ENUM
	DERRNO_DECL(DER_ERR_BASE,		1000)
	/** no permission */
	DERRNO_DECL(DER_NO_PERM,		(DER_ERR_BASE + 1))
	/** invalid parameters */
	DERRNO_DECL(DER_INVAL,			(DER_ERR_BASE + 3))
	/** entity already exists */
	DERRNO_DECL(DER_EXIST,			(DER_ERR_BASE + 4))
	/** nonexistent entity */
	DERRNO_DECL(DER_NONEXIST,		(DER_ERR_BASE + 5))
	/** no space on storage target */
	DERRNO_DECL(DER_NOSPACE,		(DER_ERR_BASE + 7))
	/** NO memory */
	DERRNO_DECL(DER_NOMEM,			(DER_ERR_BASE + 9))
	/** Busy */
	DERRNO_DECL(DER_BUSY,			(DER_ERR_BASE + 12))
	/** not initialized */
	DERRNO_DECL(DER_UNINIT,			(DER_ERR_BASE + 15))
	/** operation canceled */
	DERRNO_DECL(DER_CANCELED,		(DER_ERR_BASE + 18))
	/** GURT miscellaneous error */
	DERRNO_DECL(DER_MISC,			(DER_ERR_BASE + 25))
	/** Bad path name */
	DERRNO_DECL(DER_BADPATH,		(DER_ERR_BASE + 26))
	/** Not a directory */
	DERRNO_DECL(DER_NOTDIR,			(DER_ERR_BASE + 27))
DERRNO_END_ENUM

#undef DERRNO_BEGIN_ENUM
#undef DERRNO_DECL
#undef DERRNO_END_ENUM

const char *d_errstr(d_errno_t d_errno);

#endif /*  __GURT_ERRNO_H__ */
