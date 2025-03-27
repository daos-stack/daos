/*
 * Copyright(c) 2019-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __UTILS_PIPELINE_H__
#define __UTILS_PIPELINE_H__

#include "ocf/ocf.h"

enum ocf_pipeline_step_type {
	ocf_pipeline_step_single,
	ocf_pipeline_step_foreach,
	ocf_pipeline_step_terminator,
};

enum ocf_pipeline_arg_type {
	ocf_pipeline_arg_none,
	ocf_pipeline_arg_int,
	ocf_pipeline_arg_ptr,
	ocf_pipeline_arg_terminator,
};

struct ocf_pipeline_arg {
	enum ocf_pipeline_arg_type type;
	union {
		int i;
		void *p;
	} val;
};

typedef struct ocf_pipeline_arg *ocf_pipeline_arg_t;

#define OCF_PL_ARG_NONE() \
	{ .type = ocf_pipeline_arg_none, }

#define OCF_PL_ARG_INT(_int) \
	{ .type = ocf_pipeline_arg_int, .val.i = _int }

#define OCF_PL_ARG_PTR(_ptr) \
	{ .type = ocf_pipeline_arg_ptr, .val.p = _ptr }

#define OCF_PL_ARG_TERMINATOR() \
	{ .type = ocf_pipeline_arg_terminator, }

static inline int ocf_pipeline_arg_get_int(ocf_pipeline_arg_t arg)
{
	ENV_BUG_ON(arg->type != ocf_pipeline_arg_int);

	return arg->val.i;
}

static inline void *ocf_pipeline_arg_get_ptr(ocf_pipeline_arg_t arg)
{
	ENV_BUG_ON(arg->type != ocf_pipeline_arg_ptr);

	return arg->val.p;
}

typedef struct ocf_pipeline *ocf_pipeline_t;

typedef void (*ocf_pipeline_step_hndl_t)(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg);

typedef void (*ocf_pipeline_finish_t)(ocf_pipeline_t pipeline,
		void *priv, int error);

struct ocf_pipeline_step {
	enum ocf_pipeline_step_type type;
	ocf_pipeline_step_hndl_t hndl;
	union {
		struct ocf_pipeline_arg arg;
		struct ocf_pipeline_arg *args;
	};
};

#define OCF_PL_STEP(_hndl) \
	{ \
		.type = ocf_pipeline_step_single, \
		.hndl = _hndl, \
	}

#define OCF_PL_STEP_ARG_INT(_hndl, _int) \
	{ \
		.type = ocf_pipeline_step_single, \
		.hndl = _hndl, \
		.arg = { \
			.type = ocf_pipeline_arg_int, \
			.val.i = _int, \
		} \
	}

#define OCF_PL_STEP_ARG_PTR(_hndl, _ptr) \
	{ \
		.type = ocf_pipeline_step_single, \
		.hndl = _hndl, \
		.arg = { \
			.type = ocf_pipeline_arg_ptr, \
			.val.p = _ptr, \
		} \
	}

#define OCF_PL_STEP_FOREACH(_hndl, _args) \
	{ \
		.type = ocf_pipeline_step_foreach, \
		.hndl = _hndl, \
		.args = _args, \
	}

#define OCF_PL_STEP_TERMINATOR() \
	{ \
		.type = ocf_pipeline_step_terminator, \
	}

struct ocf_pipeline_properties {
	uint32_t priv_size;
	ocf_pipeline_finish_t finish;
	struct ocf_pipeline_step steps[];
};

int ocf_pipeline_create(ocf_pipeline_t *pipeline, ocf_cache_t cache,
		struct ocf_pipeline_properties *properties);

void ocf_pipeline_set_priv(ocf_pipeline_t pipeline, void *priv);

void *ocf_pipeline_get_priv(ocf_pipeline_t pipeline);

void ocf_pipeline_destroy(ocf_pipeline_t pipeline);

void ocf_pipeline_next(ocf_pipeline_t pipeline);

void ocf_pipeline_finish(ocf_pipeline_t pipeline, int error);

#define OCF_PL_NEXT_RET(pipeline) ({ \
	ocf_pipeline_next(pipeline); \
	return; \
})

#define OCF_PL_FINISH_RET(pipeline, error) ({ \
	ocf_pipeline_finish(pipeline, error); \
	return; \
})

#define OCF_PL_NEXT_ON_SUCCESS_RET(pipeline, error) ({ \
	if (error) \
		ocf_pipeline_finish(pipeline, error); \
	else \
		ocf_pipeline_next(pipeline); \
	return; \
})


#endif /* __UTILS_PIPELINE_H__ */
