/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#ifndef __OCF_PRIV_H__
#define __OCF_PRIV_H__

#include "ocf_env.h"
#include "ocf_def_priv.h"

#define OCF_CHECK_NULL(p) ENV_BUG_ON(!(p))

#define OCF_CMPL_RET(args...) ({ \
	cmpl(args); \
	return; \
})

#endif /* __OCF_PRIV_H__ */
