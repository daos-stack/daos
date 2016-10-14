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
 *
 * Ensures any missing sys/queue.h routines are present on the platform
 */
#ifndef __CRT_SYSQUEUE_H__
#define __CRT_SYSQUEUE_H__
#include <sys/queue.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Ensure safe traversal is implemented for sys/queue.h wrappers */

#define CONTAINER_FOREACH_SAFE(type, var, head, field, tmp)	\
	for ((var) = type##_FIRST(head), (tmp) = NULL;		\
	     ((var) != NULL)					\
	     && (((tmp) = type##_NEXT(var, field)) || 1);	\
	     (var) = (tmp))

#ifndef SLIST_FOREACH_SAFE
/*
 * Removal safe traversal of a sys/queue.h SLIST.
 * The tmp argument is a temporary pointer of the same type as var
 */
#define SLIST_FOREACH_SAFE(var, head, field, tmp)	\
	CONTAINER_FOREACH_SAFE(SLIST, var, head, field, tmp)
#endif /* SLIST_FOREACH_SAFE */

#ifndef LIST_FOREACH_SAFE
/*
 * Removal safe traversal of a sys/queue.h LIST.
 * The tmp argument is a temporary pointer of the same type as var
 */
#define LIST_FOREACH_SAFE(var, head, field, tmp)	\
	CONTAINER_FOREACH_SAFE(LIST, var, head, field, tmp)
#endif /* LIST_FOREACH_SAFE */

#ifndef STAILQ_FOREACH_SAFE
/*
 * Removal safe traversal of a sys/queue.h STAILQ.
 * The tmp argument is a temporary pointer of the same type as var
 */
#define STAILQ_FOREACH_SAFE(var, head, field, tmp)	\
	CONTAINER_FOREACH_SAFE(STAILQ, var, head, field, tmp)
#endif /* STAILQ_FOREACH_SAFE */

#ifndef TAILQ_FOREACH_SAFE
/*
 * Removal safe traversal of a sys/queue.h TAILQ.
 * The tmp argument is a temporary pointer of the same type as var
 */
#define TAILQ_FOREACH_SAFE(var, head, field, tmp)	\
	CONTAINER_FOREACH_SAFE(TAILQ, var, head, field, tmp)
#endif /* TAILQ_FOREACH_SAFE */


#if defined(__cplusplus)
}
#endif
#endif /* _CRT_SYSQUEUE_H__ */
