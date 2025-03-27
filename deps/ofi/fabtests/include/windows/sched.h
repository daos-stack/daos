/*
 * Copyright (c) 2022 Amazon.com, Inc. or its affiliates. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _WINDOWS_SCHED_H_
#define _WINDOWS_SCHED_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <windows.h>
#include <processthreadsapi.h>
#include "osd.h"

/* Type for array elements in 'cpu_set_t'. */
typedef unsigned long int __cpu_mask;

/* Size definition for CPU sets. */
#define __CPU_SETSIZE  1024
#define __NCPUBITS     (8 * sizeof (__cpu_mask))

/* Data structure to describe CPU mask. */
typedef struct cpu_set_s
{
    __cpu_mask __bits[__CPU_SETSIZE / __NCPUBITS];
} cpu_set_t;

/* Basic access functions.  */
#define __CPUELT(cpu)  ((cpu) / __NCPUBITS)
#define __CPUMASK(cpu) ((__cpu_mask) 1 << ((cpu) % __NCPUBITS))


#define CPU_ZERO(cpusetp)	__CPU_ZERO_S(sizeof(cpu_set_t), cpusetp)
#define CPU_SET(cpu, cpusetp)	__CPU_SET_S(cpu, sizeof(cpu_set_t), cpusetp)

#define __CPU_ZERO_S(setsize, cpusetp)				\
  do {								\
	size_t __i;						\
        size_t __imax = (setsize) / sizeof(__cpu_mask);		\
        __cpu_mask *__bits = (cpusetp)->__bits;			\
        for (__i = 0; __i < __imax; ++__i)			\
		__bits[__i] = 0;				\
  } while (0)

#define __CPU_SET_S(cpu, setsize, cpusetp) 			\
  size_t __cpu = (cpu);						\
  __cpu < 8 * (setsize)						\
  ? (((__cpu_mask *) ((cpusetp)->__bits))[__CPUELT (__cpu)]	\
     |= __CPUMASK (__cpu))					\
  : 0;


static inline int sched_setaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask)
{
	HANDLE process_handle;

	if (cpusetsize == 0 || mask == NULL)
		return -1;

	process_handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
	if (process_handle == NULL)
		return -1;

	if (SetProcessAffinityMask(process_handle, *mask->__bits) == 0) {
		CloseHandle(process_handle);
		return -1;
	}

	return 0;
}

static inline int sched_yield(void)
{
	(void) SwitchToThread();
	return 0;
}

#ifdef __cplusplus
}
#endif
#endif /* _WINDOWS_SCHED_H_ */
