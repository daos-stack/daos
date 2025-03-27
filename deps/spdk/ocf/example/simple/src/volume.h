/*
 * Copyright(c) 2019-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __VOLUME_H__
#define __VOLUME_H__

#include <ocf/ocf.h>
#include "ocf_env.h"
#include "ctx.h"
#include "data.h"

struct myvolume_io {
	struct volume_data *data;
	uint32_t offset;
};

struct myvolume {
	uint8_t *mem;
	const char *name;
};

int volume_init(ocf_ctx_t ocf_ctx);
void volume_cleanup(ocf_ctx_t ocf_ctx);

#endif
