/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "../cart_proc.h"
#include <stdatomic.h>

atomic_uint crt_test_hg_fallbacks;
#define FALLBACK(type)                                                                             \
	int crt_hg_proc_##type(crt_proc_t p, crt_proc_op_t op, type *v)                            \
	{                                                                                          \
		(void)p;                                                                           \
		(void)op;                                                                          \
		(void)v;                                                                           \
		atomic_fetch_add(&crt_test_hg_fallbacks, 1);                                       \
		return 123;                                                                        \
	}
FALLBACK(int8_t)
FALLBACK(uint8_t)
FALLBACK(int16_t)
FALLBACK(uint16_t)
FALLBACK(int32_t)
FALLBACK(uint32_t)
FALLBACK(int64_t)
FALLBACK(uint64_t)
FALLBACK(bool)
FALLBACK(crt_bulk_t)
FALLBACK(d_string_t)
FALLBACK(d_const_string_t)
FALLBACK(d_iov_t)
FALLBACK(d_sg_list_t)
int
crt_hg_proc_d_rank_list_t(crt_proc_t p, crt_proc_op_t op, d_rank_list_t **v)
{
	(void)p;
	(void)op;
	(void)v;
	atomic_fetch_add(&crt_test_hg_fallbacks, 1);
	return 123;
}
int
crt_hg_proc_get_op(crt_proc_t p, crt_proc_op_t *op)
{
	(void)p;
	(void)op;
	atomic_fetch_add(&crt_test_hg_fallbacks, 1);
	return 123;
}
int
crt_hg_proc_memcpy(crt_proc_t p, crt_proc_op_t op, void *v, size_t size)
{
	(void)p;
	(void)op;
	(void)v;
	(void)size;
	atomic_fetch_add(&crt_test_hg_fallbacks, 1);
	return 123;
}
