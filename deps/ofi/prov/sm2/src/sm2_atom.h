/*
 * Copyright (c) 2018      Los Alamos National Security, LLC. All rights
 *                         reserved.
 * Copyright (c) 2018      Research Organization for Information Science
 *                         and Technology (RIST).  All rights reserved.
 * Copyright (c) 2019-2021 Google, LLC. All rights reserved.
 * Copyright (c) 2019      Triad National Security, LLC. All rights
 *                         reserved.
 * Copyright (c)           Amazon.com, Inc. or its affiliates.
 *                         All Rights reserved.
 * $COPYRIGHT$
 * Most files in this release are marked with the copyrights of the
 * organizations who have edited them.  The copyrights below are in no
 * particular order and generally reflect members of the Open MPI core
 * team who have contributed code to this release.  The copyrights for
 * code used under license from other parties are included in the
 * corresponding files.
 *
 * Copyright (c) 2004-2012 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                        Corporation.  All rights reserved.
 * Copyright (c) 2004-2021 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2018 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2008 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2006-2018 Los Alamos National Security, LLC.  All rights
 *                         reserved.
 * Copyright (c) 2006-2021 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2006-2010 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2006-2021 Sandia National Laboratories. All rights reserved.
 * Copyright (c) 2006-2010 Sun Microsystems, Inc.  All rights reserved.
 *                         Use is subject to license terms.
 * Copyright (c) 2006-2021 The University of Houston. All rights reserved.
 * Copyright (c) 2006-2009 Myricom, Inc.  All rights reserved.
 * Copyright (c) 2007-2017 UT-Battelle, LLC. All rights reserved.
 * Copyright (c) 2007-2021 IBM Corporation.  All rights reserved.
 * Copyright (c) 1998-2005 Forschungszentrum Juelich, Juelich Supercomputing
 *                         Centre, Federal Republic of Germany
 * Copyright (c) 2005-2008 ZIH, TU Dresden, Federal Republic of Germany
 * Copyright (c) 2007      Evergrid, Inc. All rights reserved.
 * Copyright (c) 2008-2016 Chelsio, Inc.  All rights reserved.
 * Copyright (c) 2008-2009 Institut National de Recherche en
 *                         Informatique.  All rights reserved.
 * Copyright (c) 2007      Lawrence Livermore National Security, LLC.
 *                         All rights reserved.
 * Copyright (c) 2007-2019 Mellanox Technologies.  All rights reserved.
 * Copyright (c) 2006-2010 QLogic Corporation.  All rights reserved.
 * Copyright (c) 2008-2017 Oak Ridge National Labs.  All rights reserved.
 * Copyright (c) 2006-2012 Oracle and/or its affiliates.  All rights reserved.
 * Copyright (c) 2009-2015 Bull SAS.  All rights reserved.
 * Copyright (c) 2010      ARM ltd.  All rights reserved.
 * Copyright (c) 2016      ARM, Inc.  All rights reserved.
 * Copyright (c) 2010-2011 Alex Brick <bricka@ccs.neu.edu>.  All rights
 * reserved. Copyright (c) 2012      The University of Wisconsin-La Crosse. All
 * rights reserved. Copyright (c) 2013-2020 Intel, Inc. All rights reserved.
 * Copyright (c) 2011-2021 NVIDIA Corporation.  All rights reserved.
 * Copyright (c) 2016-2018 Broadcom Limited.  All rights reserved.
 * Copyright (c) 2011-2021 Fujitsu Limited.  All rights reserved.
 * Copyright (c) 2014-2015 Hewlett-Packard Development Company, LP.  All
 *                         rights reserved.
 * Copyright (c) 2013-2021 Research Organization for Information Science (RIST).
 *                         All rights reserved.
 * Copyright (c)           Amazon.com, Inc. or its affiliates.  All Rights
 *                         reserved.
 * Copyright (c) 2018      DataDirect Networks. All rights reserved.
 * Copyright (c) 2018-2021 Triad National Security, LLC. All rights reserved.
 * Copyright (c) 2019-2021 Hewlett Packard Enterprise Development, LP.
 * Copyright (c) 2020-2021 Google, LLC. All rights reserved.
 * Copyright (c) 2002      University of Chicago
 * Copyright (c) 2001      Argonne National Laboratory
 * Copyright (c) 2020-2021 Cornelis Networks, Inc. All rights reserved.
 * Copyright (c) 2021      Nanook Consulting
 * Copyright (c) 2017-2019 Iowa State University Research Foundation, Inc.
 *                         All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer listed
 *   in this license in the documentation and/or other materials
 *   provided with the distribution.
 *
 * - Neither the name of the copyright holders nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * The copyright holders provide no reassurances that the source code
 * provided does not infringe any patent, copyright, or any other
 * intellectual property rights of third parties.  The copyright holders
 * disclaim any liability to any recipient for claims brought against
 * recipient by any third party for infringement of that parties
 * intellectual property rights.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ----------------[Copyright from inclusion of MPICH code]----------------
 *
 * The following is a notice of limited availability of the code, and disclaimer
 * which must be included in the prologue of the code and in all source listings
 * of the code.
 *
 * Copyright Notice
 *  + 2002 University of Chicago
 *
 * Permission is hereby granted to use, reproduce, prepare derivative works, and
 * to redistribute to others.  This software was authored by:
 *
 * Mathematics and Computer Science Division
 * Argonne National Laboratory, Argonne IL 60439
 *
 * (and)
 *
 * Department of Computer Science
 * University of Illinois at Urbana-Champaign
 *
 *
 * 			      GOVERNMENT LICENSE
 *
 * Portions of this material resulted from work developed under a U.S.
 * Government Contract and are subject to the following license: the Government
 * is granted for itself and others acting on its behalf a paid-up,
 * nonexclusive, irrevocable worldwide license in this computer software to
 * reproduce, prepare derivative works, and perform publicly and display
 * publicly.
 *
 * 				  DISCLAIMER
 *
 * This computer code material was prepared, in part, as an account of work
 * sponsored by an agency of the United States Government.  Neither the United
 * States, nor the University of Chicago, nor any of their employees, makes any
 * warranty express or implied, or assumes any legal liability or responsibility
 * for the accuracy, completeness, or usefulness of any information, apparatus,
 * product, or process disclosed, or represents that its use would not infringe
 * privately owned rights.
 */
/*
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

#include "config.h"
#include <stdbool.h>

#ifdef HAVE_ATOMICS
#include <stdatomic.h>

static inline void atomic_mb(void)
{
	atomic_thread_fence(memory_order_seq_cst);
}

static inline void atomic_rmb(void)
{
#if defined(PLATFORM_ARCH_X86_64) && defined(PLATFORM_COMPILER_GNU) && \
	__GNUC__ < 8
	/* work around a bug in older gcc versions (observed in gcc 6.x)
	 * where acquire seems to get treated as a no-op instead of being
	 * equivalent to __asm__ __volatile__("": : :"memory") on x86_64.
	 * The issue has been fixed in the GCC 8 release series:
	 * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80640
	 */
	atomic_mb();
#else
	atomic_thread_fence(memory_order_acquire);
#endif
}

static inline void atomic_wmb(void)
{
	atomic_thread_fence(memory_order_release);
}

#define atomic_compare_exchange(addr, compare, value)       \
	atomic_compare_exchange_strong_explicit(            \
		(_Atomic uintptr_t *) addr, compare, value, \
		memory_order_acquire, memory_order_relaxed)

#define atomic_swap_ptr(addr, value)                                \
	atomic_exchange_explicit((_Atomic uintptr_t *) addr, value, \
				 memory_order_relaxed)

#elif defined(HAVE_BUILTIN_MM_ATOMICS)

static inline void atomic_mb(void)
{
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static inline void atomic_rmb(void)
{
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
}

static inline void atomic_wmb(void)
{
	__atomic_thread_fence(__ATOMIC_RELEASE);
}

#define atomic_compare_exchange(x, y, z)                                    \
	__atomic_compare_exchange_n((uintptr_t *) (x), (int64_t *) (y),     \
				    (int64_t) (z), false, __ATOMIC_ACQUIRE, \
				    __ATOMIC_RELAXED)

static inline long int atomic_swap_ptr(long int *addr, long int value)
{
	long int oldval;
	__atomic_exchange(addr, &value, &oldval, __ATOMIC_RELAXED);
	return oldval;
}

#else
#error "No atomics support found for SM2."
#endif
