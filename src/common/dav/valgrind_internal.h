/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2015-2021, Intel Corporation */

/*
 * valgrind_internal.h -- internal definitions for valgrind macros
 */

#ifndef __DAOS_COMMON_VALGRIND_INTERNAL_H
#define __DAOS_COMMON_VALGRIND_INTERNAL_H 1

#ifdef D_HAS_VALGRIND
#if !defined(_WIN32) && !defined(__FreeBSD__) && !defined(__riscv)
#define VG_TXINFO_ENABLED 1
#define VG_HELGRIND_ENABLED 1
#define VG_MEMCHECK_ENABLED 1
#define VG_DRD_ENABLED 1
#endif
#endif

#if VG_TXINFO_ENABLED || VG_HELGRIND_ENABLED || VG_MEMCHECK_ENABLED || \
	VG_DRD_ENABLED
#define ANY_VG_TOOL_ENABLED 1
#else
#define ANY_VG_TOOL_ENABLED 0
#endif

#if ANY_VG_TOOL_ENABLED
extern unsigned _On_valgrind;
#define On_valgrind __builtin_expect(_On_valgrind, 0)
#include "valgrind/valgrind.h"
#else
#define On_valgrind (0)
#endif

#if VG_HELGRIND_ENABLED
extern unsigned _On_helgrind;
#define On_helgrind __builtin_expect(_On_helgrind, 0)
#include "valgrind/helgrind.h"
#else
#define On_helgrind (0)
#endif

#if VG_DRD_ENABLED
extern unsigned _On_drd;
#define On_drd __builtin_expect(_On_drd, 0)
#include "valgrind/drd.h"
#else
#define On_drd (0)
#endif

#if VG_HELGRIND_ENABLED || VG_DRD_ENABLED

extern unsigned _On_drd_or_hg;
#define On_drd_or_hg __builtin_expect(_On_drd_or_hg, 0)

#define VALGRIND_ANNOTATE_HAPPENS_BEFORE(obj) do {\
	if (On_drd_or_hg) \
		ANNOTATE_HAPPENS_BEFORE((obj));\
} while (0)

#define VALGRIND_ANNOTATE_HAPPENS_AFTER(obj) do {\
	if (On_drd_or_hg) \
		ANNOTATE_HAPPENS_AFTER((obj));\
} while (0)

#define VALGRIND_ANNOTATE_NEW_MEMORY(addr, size) do {\
	if (On_drd_or_hg) \
		ANNOTATE_NEW_MEMORY((addr), (size));\
} while (0)

#define VALGRIND_ANNOTATE_IGNORE_READS_BEGIN() do {\
	if (On_drd_or_hg) \
	ANNOTATE_IGNORE_READS_BEGIN();\
} while (0)

#define VALGRIND_ANNOTATE_IGNORE_READS_END() do {\
	if (On_drd_or_hg) \
	ANNOTATE_IGNORE_READS_END();\
} while (0)

#define VALGRIND_ANNOTATE_IGNORE_WRITES_BEGIN() do {\
	if (On_drd_or_hg) \
	ANNOTATE_IGNORE_WRITES_BEGIN();\
} while (0)

#define VALGRIND_ANNOTATE_IGNORE_WRITES_END() do {\
	if (On_drd_or_hg) \
	ANNOTATE_IGNORE_WRITES_END();\
} while (0)

/* Supported by both helgrind and drd. */
#define VALGRIND_HG_DRD_DISABLE_CHECKING(addr, size) do {\
	if (On_drd_or_hg) \
		VALGRIND_HG_DISABLE_CHECKING((addr), (size));\
} while (0)

#else

#define On_drd_or_hg (0)

#define VALGRIND_ANNOTATE_HAPPENS_BEFORE(obj) { (void)(obj); }

#define VALGRIND_ANNOTATE_HAPPENS_AFTER(obj) { (void)(obj); }

#define VALGRIND_ANNOTATE_NEW_MEMORY(addr, size) do {\
	(void) (addr);\
	(void) (size);\
} while (0)

#define VALGRIND_ANNOTATE_IGNORE_READS_BEGIN() do {} while (0)

#define VALGRIND_ANNOTATE_IGNORE_READS_END() do {} while (0)

#define VALGRIND_ANNOTATE_IGNORE_WRITES_BEGIN() do {} while (0)

#define VALGRIND_ANNOTATE_IGNORE_WRITES_END() do {} while (0)

#define VALGRIND_HG_DRD_DISABLE_CHECKING(addr, size) do {\
	(void) (addr);\
	(void) (size);\
} while (0)

#endif

#if VG_TXINFO_ENABLED

extern int _Vg_txinfo_emit;
#define VG_txinfo_emit __builtin_expect(_Vg_txinfo_emit, 0)

void util_emit_log(const char *func, int order);

#define VALGRIND_SET_CLEAN(addr, len) do {\
	(void)(addr);\
	(void)(len);\
} while (0)

#define VALGRIND_START_TX do {} while (0)

#define VALGRIND_END_TX do {} while (0)

#define VALGRIND_ADD_TO_TX(addr, len) do {\
	(void) (addr);\
	(void) (len);\
} while (0)

#define VALGRIND_REMOVE_FROM_TX(addr, len) do {\
	(void) (addr);\
	(void) (len);\
} while (0)

#define VALGRIND_ADD_TO_GLOBAL_TX_IGNORE(addr, len) do {\
	(void) (addr);\
	(void) (len);\
} while (0)

/*
 * Logs library and function name with proper suffix
 * to VG log file.
 */
#define DAV_API_START() do {\
	if (VG_txinfo_emit)\
		VALGRIND_PRINTF("%s BEGIN\n", __func__);\
} while (0)
#define DAV_API_END() do {\
	if (VG_txinfo_emit)\
		VALGRIND_PRINTF("%s END\n", __func__);\
} while (0)

#else /* VG_TXINFO_ENABLED */

#define VG_txinfo_emit (0)

#define VALGRIND_SET_CLEAN(addr, len) do {\
	(void) (addr);\
	(void) (len);\
} while (0)

#define VALGRIND_START_TX do {} while (0)

#define VALGRIND_END_TX do {} while (0)

#define VALGRIND_ADD_TO_TX(addr, len) do {\
	(void) (addr);\
	(void) (len);\
} while (0)

#define VALGRIND_REMOVE_FROM_TX(addr, len) do {\
	(void) (addr);\
	(void) (len);\
} while (0)

#define VALGRIND_ADD_TO_GLOBAL_TX_IGNORE(addr, len) do {\
	(void) (addr);\
	(void) (len);\
} while (0)

#define DAV_API_START() do {} while (0)

#define DAV_API_END() do {} while (0)

#endif /* VG_TXINFO_ENABLED */

#if VG_MEMCHECK_ENABLED

extern unsigned _On_memcheck;
#define On_memcheck __builtin_expect(_On_memcheck, 0)

#include "valgrind/memcheck.h"

#define VALGRIND_DO_DISABLE_ERROR_REPORTING do {\
	if (On_valgrind)\
		VALGRIND_DISABLE_ERROR_REPORTING;\
} while (0)

#define VALGRIND_DO_ENABLE_ERROR_REPORTING do {\
	if (On_valgrind)\
		VALGRIND_ENABLE_ERROR_REPORTING;\
} while (0)

#define VALGRIND_DO_CREATE_MEMPOOL(heap, rzB, is_zeroed) do {\
	if (On_memcheck)\
		VALGRIND_CREATE_MEMPOOL(heap, rzB, is_zeroed);\
} while (0)

#define VALGRIND_DO_DESTROY_MEMPOOL(heap) do {\
	if (On_memcheck)\
		VALGRIND_DESTROY_MEMPOOL(heap);\
} while (0)

#define VALGRIND_DO_MEMPOOL_ALLOC(heap, addr, size) do {\
	if (On_memcheck)\
		VALGRIND_MEMPOOL_ALLOC(heap, addr, size);\
} while (0)

#define VALGRIND_DO_MEMPOOL_FREE(heap, addr) do {\
	if (On_memcheck)\
		VALGRIND_MEMPOOL_FREE(heap, addr);\
} while (0)

#define VALGRIND_DO_MAKE_MEM_DEFINED(addr, len) do {\
	if (On_memcheck)\
		VALGRIND_MAKE_MEM_DEFINED(addr, len);\
} while (0)

#define VALGRIND_DO_MAKE_MEM_UNDEFINED(addr, len) do {\
	if (On_memcheck)\
		VALGRIND_MAKE_MEM_UNDEFINED(addr, len);\
} while (0)

#define VALGRIND_DO_MAKE_MEM_NOACCESS(addr, len) do {\
	if (On_memcheck)\
		VALGRIND_MAKE_MEM_NOACCESS(addr, len);\
} while (0)

#define VALGRIND_DO_CHECK_MEM_IS_ADDRESSABLE(addr, len) do {\
	if (On_memcheck)\
		VALGRIND_CHECK_MEM_IS_ADDRESSABLE(addr, len);\
} while (0)

#else /* VG_MEMCHECK_ENABLED */

#define On_memcheck (0)

#define VALGRIND_DO_DISABLE_ERROR_REPORTING do {} while (0)

#define VALGRIND_DO_ENABLE_ERROR_REPORTING do {} while (0)

#define VALGRIND_DO_CREATE_MEMPOOL(heap, rzB, is_zeroed)\
	do { (void) (heap); (void) (rzB); (void) (is_zeroed); } while (0)

#define VALGRIND_DO_DESTROY_MEMPOOL(heap) { (void) (heap); }

#define VALGRIND_DO_MEMPOOL_ALLOC(heap, addr, size)\
	do { (void) (heap); (void) (addr); (void) (size); } while (0)

#define VALGRIND_DO_MEMPOOL_FREE(heap, addr)\
	do { (void) (heap); (void) (addr); } while (0)

#define VALGRIND_DO_MAKE_MEM_DEFINED(addr, len)\
	do { (void) (addr); (void) (len); } while (0)

#define VALGRIND_DO_MAKE_MEM_UNDEFINED(addr, len)\
	do { (void) (addr); (void) (len); } while (0)

#define VALGRIND_DO_MAKE_MEM_NOACCESS(addr, len)\
	do { (void) (addr); (void) (len); } while (0)

#define VALGRIND_DO_CHECK_MEM_IS_ADDRESSABLE(addr, len)\
	do { (void) (addr); (void) (len); } while (0)

#endif /* VG_MEMCHECK_ENABLED */

#endif /* __DAOS_COMMON_VALGRIND_INTERNAL_H */
