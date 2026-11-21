/*
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2025 Google LLC
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * \file
 *
 * CART provides a set of macros for RPC registration. Using the macro interface
 * to register RPCs is much simpler and reduces the opportunities for mistakes.
 */

#ifndef __CRT_RPC_GEN_MACROS_H__
#define __CRT_RPC_GEN_MACROS_H__

#include "cart/types.h"

#include <boost/preprocessor.hpp>

/**
 * public macros:
 *
 *     preparation:
 *         - CRT_RPC_DECLARE()
 *         - CRT_RPC_DEFINE()
 *
 *     registration:
 *         - CRT_RPC_REGISTER()
 *         - CRT_RPC_SRV_REGISTER()
 *
 * To register an RPC using macros:
 *     CRT_RPC_DECLARE(my_rpc_name, input_fields, output_fields)
 *     CRT_RPC_DEFINE(my_rpc_name, input_fields, output_fields)
 *     CRT_RPC_REGISTER(opcode, flags, my_rpc_name);
 *
 * The input/output structs can be accessed using the following pointers:
 *     struct my_rpc_name_in *rpc_in;
 *     struct my_rpc_name_out *rpc_out;
 */
/**
 * Prepare struct types and format description for the input/output of an RPC.
 * Supported types in the fields_in/fields_out list can be found in
 * include/cart/types.h
 *
 * Example usage:
 *
 * \#define CRT_ISEQ_MY_RPC
 *     ((int32_t)       (mr_arg_1)     CRT_VAR)
 *     ((uint32_t)      (mr_arg_2)     CRT_VAR)
 *     ((d_rank_t)      (mr_rank)      CRT_VAR)
 *     ((d_rank_list_t) (mr_rank_list) CRT_PTR)
 *     ((uuid_t)        (mr_array)     CRT_ARRAY)
 *     ((d_string_t)    (mr_name)      CRT_VAR)
 *
 * \#define CRT_OSEQ_MY_RPC
 *     ((int32_t)       (mr_ret)       CRT_VAR)
 *
 * CRT_RPC_DECLARE(my_rpc, CRT_ISEQ_MY_RPC, CRT_OSEQ_MY_RPC)
 * CRT_RPC_REGISTER(opcode, flags, my_rpc);
 *
 * these two macros above expands into:
 *
 * struct my_rpc_in {
 *     int32_t           mr_arg_1;
 *     uint32_t          mr_arg_2;
 *     d_rank_t          mr_rank;
 *     d_rank_list_t    *mr_rank_list;
 *     struct crt_array  mr_array;
 *     d_string_t        mr_name;
 * };
 *
 * struct my_rpc_out {
 *     int32_t           mr_ret;
 * };
 *
 * crt_register(opcode, flags, &CQF_my_rpc);
 *
 * the macros CRT_RPC_DEFINE(my_rpc, CRT_ISEQ_MY_RPC, CRT_OSEQ_MY_RPC) expands
 * into internal RPC definition which will be used in RPC registration.
 * The content of this macro expansion will be changed in the future.
 *
 * To use array types it's possible to define types as above, and then use the
 * same macros to declare types and proc structs for types, and then reference
 * the type directly in the RPC definition.
 *
 * CRT_GEN_STRUCT(struct, CRT_SEQ_MY_TYPE)
 # CRT_GEN_PROC_FUNC(struct, CRT_SEQ_MY_TYPE)
 *
 */
/* clang-format off */
#define CRT_VAR		(0)
#define CRT_PTR		(1)
#define CRT_ARRAY	(2)
#define CRT_RAW		(3)

#define CRT_GEN_GET_TYPE(seq) BOOST_PP_SEQ_ELEM(0, seq)
#define CRT_GEN_GET_NAME(seq) BOOST_PP_SEQ_ELEM(1, seq)
#define CRT_GEN_GET_KIND(seq) BOOST_PP_SEQ_ELEM(2, seq)

/* convert constructed name into proper name */
#define crt_proc_struct BOOST_PP_RPAREN() BOOST_PP_CAT BOOST_PP_LPAREN() \
	crt_proc_struct_,

#define CRT_GEN_X(x) x
#define CRT_GEN_X2(x) CRT_GEN_X BOOST_PP_LPAREN() crt_proc_##x BOOST_PP_RPAREN()
#define CRT_GEN_GET_FUNC(seq) CRT_GEN_X2 BOOST_PP_SEQ_FIRST_N(1, seq)
#define CRT_GEN_X3(x) CRT_GEN_X BOOST_PP_LPAREN() fail_##x BOOST_PP_RPAREN()
#define CRT_GEN_FAIL_LABEL(seq) CRT_GEN_X3 BOOST_PP_SEQ_TAIL(BOOST_PP_SEQ_FIRST_N(2, seq))

#define CRT_GEN_STRUCT_FIELD(seq)					\
	BOOST_PP_EXPAND(						\
	BOOST_PP_IF(BOOST_PP_EQUAL(CRT_GEN_X CRT_ARRAY, CRT_GEN_GET_KIND(seq)),\
		struct {						\
			uint64_t		 ca_count;		\
			CRT_GEN_GET_TYPE(seq)	*ca_arrays;		\
		},							\
		CRT_GEN_GET_TYPE(seq))					\
	BOOST_PP_IF(BOOST_PP_EQUAL(CRT_GEN_X CRT_PTR, CRT_GEN_GET_KIND(seq)), \
		*CRT_GEN_GET_NAME(seq), CRT_GEN_GET_NAME(seq));)

#define CRT_GEN_STRUCT_FIELDS(z, n, seq)				\
	CRT_GEN_STRUCT_FIELD(BOOST_PP_SEQ_ELEM(n, seq))

#define CRT_GEN_STRUCT(struct_type_name, seq)				\
	struct struct_type_name {					\
		BOOST_PP_REPEAT(BOOST_PP_SEQ_SIZE(seq),			\
				CRT_GEN_STRUCT_FIELDS, seq)		\
	};

#define CRT_GEN_PROC_ARRAY(ptr, seq)					\
	do {								\
	CRT_GEN_GET_TYPE(seq) **e_ptrp = &ptr->CRT_GEN_GET_NAME(seq).ca_arrays;\
	CRT_GEN_GET_TYPE(seq) *e_ptr = ptr->CRT_GEN_GET_NAME(seq).ca_arrays; \
	uint64_t count = ptr->CRT_GEN_GET_NAME(seq).ca_count;		\
	uint64_t i;							\
	if (proc_op == CRT_PROC_DECODE)					\
		*e_ptrp = NULL;						\
	/* process the count of array first */				\
	rc = crt_proc_uint64_t(proc, proc_op, &count);			\
	if (unlikely(rc))						\
		goto CRT_GEN_FAIL_LABEL(seq);				\
	ptr->CRT_GEN_GET_NAME(seq).ca_count = count;			\
	if (unlikely(count == 0))					\
		break; /* goto next field */				\
	if (proc_op == CRT_PROC_DECODE) {				\
		D_ALLOC_ARRAY(e_ptr, (int)count);			\
		if (unlikely(e_ptr == NULL)) {				\
			rc = -DER_NOMEM;				\
			goto CRT_GEN_FAIL_LABEL(seq);			\
		}							\
		*e_ptrp = e_ptr;					\
	}								\
	/* process the elements of array */				\
	for (i = 0; i < count && e_ptr != NULL; i++) {			\
		rc = CRT_GEN_GET_FUNC(seq)(proc, proc_op, &e_ptr[i]);	\
		if (unlikely(rc)) {					\
			if (proc_op == CRT_PROC_DECODE)			\
				ptr->CRT_GEN_GET_NAME(seq).ca_count = i;\
			goto CRT_GEN_FAIL_LABEL(seq);			\
		}							\
	}								\
	if (proc_op == CRT_PROC_FREE)					\
		D_FREE(*e_ptrp);					\
	} while (0);

#define CRT_GEN_FAIL_ARRAY(ptr, seq)					\
	CRT_GEN_FAIL_LABEL(seq):					\
	if (proc_op == CRT_PROC_DECODE) {				\
		CRT_GEN_GET_TYPE(seq) **e_ptrp = &ptr->CRT_GEN_GET_NAME(seq).ca_arrays;\
		CRT_GEN_GET_TYPE(seq) *e_ptr = ptr->CRT_GEN_GET_NAME(seq).ca_arrays; \
		uint64_t count = ptr->CRT_GEN_GET_NAME(seq).ca_count;	\
		uint64_t i;						\
		/* process the elements of array */			\
		for (i = 0; i < count && e_ptr != NULL; i++)		\
			(void)CRT_GEN_GET_FUNC(seq)(proc, CRT_PROC_FREE,\
						    &e_ptr[i]);		\
		D_FREE(*e_ptrp);					\
		ptr->CRT_GEN_GET_NAME(seq).ca_count = 0;		\
	}

#define CRT_GEN_PROC_RAW(ptr, seq)					\
	rc = crt_proc_memcpy(proc, proc_op, &ptr->CRT_GEN_GET_NAME(seq),\
			     sizeof(CRT_GEN_GET_TYPE(seq)));		\
	if (unlikely(rc))						\
		goto CRT_GEN_FAIL_LABEL(seq);

#define CRT_GEN_FAIL_RAW(ptr, seq)					\
	CRT_GEN_FAIL_LABEL(seq):;

#define CRT_GEN_PROC_ALL(ptr, seq)					\
	rc = CRT_GEN_GET_FUNC(seq)(proc, proc_op,			\
				   &ptr->CRT_GEN_GET_NAME(seq));	\
	if (unlikely(rc))						\
		goto CRT_GEN_FAIL_LABEL(seq);

#define CRT_GEN_FAIL_ALL(ptr, seq)					\
	(void)CRT_GEN_GET_FUNC(seq)(proc, CRT_PROC_FREE,		\
				    &ptr->CRT_GEN_GET_NAME(seq));	\
	CRT_GEN_FAIL_LABEL(seq):;

#define CRT_GEN_PROC_FIELD(ptr, seq)					\
	BOOST_PP_EXPAND(						\
	BOOST_PP_IF(BOOST_PP_EQUAL(CRT_GEN_X CRT_ARRAY, CRT_GEN_GET_KIND(seq)),\
		CRT_GEN_PROC_ARRAY(ptr, seq),				\
	BOOST_PP_IF(BOOST_PP_EQUAL(CRT_GEN_X CRT_RAW, CRT_GEN_GET_KIND(seq)), \
		CRT_GEN_PROC_RAW(ptr, seq),				\
		CRT_GEN_PROC_ALL(ptr, seq))))

#define CRT_GEN_FAIL_FIELD(ptr, seq)					\
	BOOST_PP_EXPAND(						\
	BOOST_PP_IF(BOOST_PP_EQUAL(CRT_GEN_X CRT_ARRAY, CRT_GEN_GET_KIND(seq)),\
		CRT_GEN_FAIL_ARRAY(ptr, seq),				\
	BOOST_PP_IF(BOOST_PP_EQUAL(CRT_GEN_X CRT_RAW, CRT_GEN_GET_KIND(seq)), \
		CRT_GEN_FAIL_RAW(ptr, seq),				\
		CRT_GEN_FAIL_ALL(ptr, seq))))

#define CRT_GEN_PROC_FIELDS(z, n, seq)					\
	CRT_GEN_PROC_FIELD(ptr, BOOST_PP_SEQ_ELEM(n, seq))

#define CRT_GEN_FAIL_FIELDS(z, n, seq)					\
	CRT_GEN_FAIL_FIELD(ptr, BOOST_PP_SEQ_ELEM(BOOST_PP_SUB(BOOST_PP_DEC(BOOST_PP_SEQ_SIZE(seq)), n), seq))

#define CRT_GEN_PROC_FUNC(type_name, seq)				\
	static int							\
	crt_proc_##type_name(crt_proc_t proc, struct type_name *ptr) {	\
		crt_proc_op_t proc_op;					\
		int rc;							\
		if (unlikely(proc == NULL || ptr == NULL))		\
			return -DER_INVAL;				\
		rc = crt_proc_get_op(proc, &proc_op);			\
		if (unlikely(rc))					\
			return rc;					\
		BOOST_PP_REPEAT(BOOST_PP_SEQ_SIZE(seq),			\
				CRT_GEN_PROC_FIELDS, seq)		\
		if (unlikely(rc)) {					\
			BOOST_PP_REPEAT(BOOST_PP_SEQ_SIZE(seq),		\
					CRT_GEN_FAIL_FIELDS, seq)	\
		}							\
		return rc;						\
	}

#define POP_BACK(seq) BOOST_PP_SEQ_HEAD(BOOST_PP_SEQ_REVERSE(seq))
#define FOFFSET(sname, seq)						\
	offsetof(struct sname, CRT_GEN_GET_NAME(POP_BACK(seq)))

#define CRT_RPC_DECLARE(rpc_name, fields_in, fields_out)		\
	BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_in),			\
		    CRT_GEN_STRUCT(rpc_name##_in, fields_in), )		\
	BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_out),			\
		    CRT_GEN_STRUCT(rpc_name##_out, fields_out), )	\
	/* Generate a packed struct and assert use the offset of the */	\
	/* last field to assert that there are no holes */		\
	_Pragma("pack(push, 1)")					\
	BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_in),			\
		CRT_GEN_STRUCT(rpc_name##_in_packed, fields_in), )	\
	BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_out),			\
		CRT_GEN_STRUCT(rpc_name##_out_packed, fields_out), )	\
	_Pragma("pack(pop)")						\
	BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_out),			\
		    static_assert(FOFFSET(rpc_name##_out_packed,	\
					  ((_) (_) (_)) fields_out) ==	\
				  FOFFSET(rpc_name##_out, ((_) (_) (_))	\
					  fields_out), #rpc_name	\
				  " output struct has a hole");, )	\
	BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_in),			\
		    static_assert(FOFFSET(rpc_name##_in_packed,		\
					  ((_) (_) (_)) fields_in) ==	\
				  FOFFSET(rpc_name##_in, ((_) (_) (_))	\
					  fields_in), #rpc_name		\
				  " input struct has a hole");, )	\
	extern struct crt_req_format CQF_##rpc_name;

/* warning was introduced in version 8 of GCC */
#if D_HAS_WARNING(8, "-Wsizeof-pointer-div")
#define CRT_DISABLE_SIZEOF_POINTER_DIV					\
	_Pragma("GCC diagnostic ignored \"-Wsizeof-pointer-div\"")
#else /* warning not available */
#define CRT_DISABLE_SIZEOF_POINTER_DIV
#endif /* warning is available */

#define CRT_RPC_DEFINE(rpc_name, fields_in, fields_out)			\
	BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_in),			\
		    CRT_GEN_PROC_FUNC(rpc_name##_in, fields_in), )	\
	BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_out),			\
		    CRT_GEN_PROC_FUNC(rpc_name##_out, fields_out), )	\
	_Pragma("GCC diagnostic push")					\
	CRT_DISABLE_SIZEOF_POINTER_DIV					\
	struct crt_req_format CQF_##rpc_name = {			\
		.crf_proc_in  = (crt_proc_cb_t)				\
		BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_in),		\
			crt_proc_##rpc_name##_in, NULL),		\
		.crf_proc_out = (crt_proc_cb_t)				\
		BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_out),		\
			crt_proc_##rpc_name##_out, NULL),		\
		.crf_size_in  =						\
		BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_in),		\
			sizeof(struct rpc_name##_in), 0),		\
		.crf_size_out =						\
		BOOST_PP_IF(BOOST_PP_SEQ_SIZE(fields_out),		\
			sizeof(struct rpc_name##_out), 0)		\
	};								\
	_Pragma("GCC diagnostic pop")

#define CRT_RPC_CORPC_REGISTER(opcode, rpc_name, rpc_handler, co_ops)	\
	crt_corpc_register(opcode, &CQF_##rpc_name, rpc_handler, co_ops)

#define CRT_RPC_SRV_REGISTER(opcode, flags, rpc_name, rpc_handler)	\
	crt_rpc_srv_register(opcode, flags, &CQF_##rpc_name, rpc_handler)

#define CRT_RPC_REGISTER(opcode, flags, rpc_name)			\
	crt_rpc_register(opcode, flags, &CQF_##rpc_name)

/* clang-format on */

#endif /* __CRT_RPC_GEN_MACROS_H__ */