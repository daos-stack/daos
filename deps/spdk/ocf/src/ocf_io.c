/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "ocf_def_priv.h"
#include "ocf_io_priv.h"
#include "ocf_volume_priv.h"
#include "utils/utils_io_allocator.h"

/*
 * This is io allocator dedicated for bottom devices.
 * Out IO structure looks like this:
 * --------------> +-------------------------+
 * | OCF is aware  |                         |
 * | of this part. | struct ocf_io_meta      |
 * |               |                         |
 * |               +-------------------------+ <----------------
 * |               |                         |  Bottom adapter |
 * |               | struct ocf_io           |  is aware of    |
 * |               |                         |  this part.     |
 * --------------> +-------------------------+                 |
 *                 |                         |                 |
 *                 | Bottom adapter specific |                 |
 *                 | context data structure. |                 |
 *                 |                         |                 |
 *                 +-------------------------+ <----------------
 */

#define OCF_IO_TOTAL(priv_size) \
		(sizeof(struct ocf_io_internal) + priv_size)

static int ocf_io_allocator_default_init(ocf_io_allocator_t allocator,
		uint32_t priv_size, const char *name)
{
	allocator->priv = env_allocator_create(OCF_IO_TOTAL(priv_size), name,
			true);
	if (!allocator->priv)
		return -OCF_ERR_NO_MEM;

	return 0;
}

static void ocf_io_allocator_default_deinit(ocf_io_allocator_t allocator)
{
	env_allocator_destroy(allocator->priv);
	allocator->priv = NULL;
}

static void *ocf_io_allocator_default_new(ocf_io_allocator_t allocator,
		ocf_volume_t volume, ocf_queue_t queue,
		uint64_t addr, uint32_t bytes, uint32_t dir)
{
	return env_allocator_new(allocator->priv);
}

static void ocf_io_allocator_default_del(ocf_io_allocator_t allocator, void *obj)
{
	env_allocator_del(allocator->priv, obj);
}

const struct ocf_io_allocator_type type_default = {
	.ops = {
		.allocator_init = ocf_io_allocator_default_init,
		.allocator_deinit = ocf_io_allocator_default_deinit,
		.allocator_new = ocf_io_allocator_default_new,
		.allocator_del = ocf_io_allocator_default_del,
	},
};

ocf_io_allocator_type_t ocf_io_allocator_get_type_default(void)
{
	return &type_default;
}

/*
 * IO internal API
 */

static struct ocf_io_internal *ocf_io_get_internal(struct ocf_io* io)
{
	return container_of(io, struct ocf_io_internal, io);
}

struct ocf_io *ocf_io_new(ocf_volume_t volume, ocf_queue_t queue,
		uint64_t addr, uint32_t bytes, uint32_t dir,
		uint32_t io_class, uint64_t flags)
{
	struct ocf_io_internal *ioi;
	uint32_t sector_size = SECTORS_TO_BYTES(1);

	if ((addr % sector_size) || (bytes % sector_size))
		return NULL;

	if (!ocf_refcnt_inc(&volume->refcnt))
		return NULL;

	ioi = ocf_io_allocator_new(&volume->type->allocator, volume, queue,
			addr, bytes, dir);
	if (!ioi) {
		ocf_refcnt_dec(&volume->refcnt);
		return NULL;
	}

	ioi->meta.volume = volume;
	ioi->meta.ops = &volume->type->properties->io_ops;
	env_atomic_set(&ioi->meta.ref_count, 1);

	ioi->io.io_queue = queue;
	ioi->io.addr = addr;
	ioi->io.bytes = bytes;
	ioi->io.dir = dir;
	ioi->io.io_class = io_class;
	ioi->io.flags = flags;

	return &ioi->io;
}

/*
 * IO external API
 */

void *ocf_io_get_priv(struct ocf_io* io)
{
	return (void *)io + sizeof(struct ocf_io);
}

int ocf_io_set_data(struct ocf_io *io, ctx_data_t *data, uint32_t offset)
{
	struct ocf_io_internal *ioi = ocf_io_get_internal(io);

	return ioi->meta.ops->set_data(io, data, offset);
}

ctx_data_t *ocf_io_get_data(struct ocf_io *io)
{
	struct ocf_io_internal *ioi = ocf_io_get_internal(io);

	return ioi->meta.ops->get_data(io);
}

void ocf_io_get(struct ocf_io *io)
{
	struct ocf_io_internal *ioi = ocf_io_get_internal(io);

	env_atomic_inc_return(&ioi->meta.ref_count);
}

void ocf_io_put(struct ocf_io *io)
{
	struct ocf_io_internal *ioi = ocf_io_get_internal(io);
	struct ocf_volume *volume;

	if (env_atomic_dec_return(&ioi->meta.ref_count))
		return;

	/* Hold volume reference to avoid use after free of ioi */
	volume = ioi->meta.volume;

	ocf_io_allocator_del(&ioi->meta.volume->type->allocator, (void *)ioi);

	ocf_refcnt_dec(&volume->refcnt);
}

ocf_volume_t ocf_io_get_volume(struct ocf_io *io)
{
	struct ocf_io_internal *ioi = ocf_io_get_internal(io);

	return ioi->meta.volume;
}
