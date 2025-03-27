/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __OCF_VOLUME_PRIV_H__
#define __OCF_VOLUME_PRIV_H__

#include "ocf_env.h"
#include "ocf_io_priv.h"
#include "utils/utils_refcnt.h"
#include "utils/utils_io_allocator.h"

struct ocf_volume_extended {
	ocf_io_allocator_type_t allocator_type;
};

struct ocf_volume_type {
	const struct ocf_volume_properties *properties;
	struct ocf_io_allocator allocator;
};

struct ocf_volume {
	ocf_volume_type_t type;
	struct ocf_volume_uuid uuid;
	struct {
		unsigned discard_zeroes:1;
			/* true if reading discarded pages returns 0 */
	} features;
	bool opened;
	bool uuid_copy;
	void *priv;
	ocf_cache_t cache;
	struct list_head core_pool_item;
	struct ocf_refcnt refcnt __attribute__((aligned(64)));
} __attribute__((aligned(64)));

int ocf_volume_type_init(struct ocf_volume_type **type,
		const struct ocf_volume_properties *properties,
		const struct ocf_volume_extended *extended);

void ocf_volume_type_deinit(struct ocf_volume_type *type);

void ocf_volume_move(ocf_volume_t volume, ocf_volume_t from);

void ocf_volume_set_uuid(ocf_volume_t volume,
		const struct ocf_volume_uuid *uuid);

static inline void ocf_volume_submit_metadata(struct ocf_io *io)
{
	ocf_volume_t volume = ocf_io_get_volume(io);

	ENV_BUG_ON(!volume->type->properties->ops.submit_metadata);

	volume->type->properties->ops.submit_metadata(io);
}

static inline void ocf_volume_submit_write_zeroes(struct ocf_io *io)
{
	ocf_volume_t volume = ocf_io_get_volume(io);

	ENV_BUG_ON(!volume->type->properties->ops.submit_write_zeroes);

	volume->type->properties->ops.submit_write_zeroes(io);
}

#endif  /*__OCF_VOLUME_PRIV_H__ */
