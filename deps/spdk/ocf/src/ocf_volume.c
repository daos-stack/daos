/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "ocf_priv.h"
#include "ocf_volume_priv.h"
#include "ocf_io_priv.h"
#include "ocf_env.h"

int ocf_uuid_set_str(ocf_uuid_t uuid, char *str)
{
        size_t len = env_strnlen(str, OCF_VOLUME_UUID_MAX_SIZE);

        if (len >= OCF_VOLUME_UUID_MAX_SIZE)
                return -OCF_ERR_INVAL;

        uuid->data = str;
        uuid->size = len + 1;

        return 0;
}

/* *** Bottom interface *** */

/*
 * Volume type
 */

int ocf_volume_type_init(struct ocf_volume_type **type,
		const struct ocf_volume_properties *properties,
		const struct ocf_volume_extended *extended)
{
	const struct ocf_volume_ops *ops = &properties->ops;
	ocf_io_allocator_type_t allocator_type;
	struct ocf_volume_type *new_type;
	int ret;

	if (!ops->submit_io || !ops->open || !ops->close ||
			!ops->get_max_io_size || !ops->get_length) {
		return -OCF_ERR_INVAL;
	}

	if (properties->caps.atomic_writes && !ops->submit_metadata)
		return -OCF_ERR_INVAL;

	new_type = env_zalloc(sizeof(**type), ENV_MEM_NORMAL);
	if (!new_type)
		return -OCF_ERR_NO_MEM;

	if (extended && extended->allocator_type)
		allocator_type = extended->allocator_type;
	else
		allocator_type = ocf_io_allocator_get_type_default();

	ret = ocf_io_allocator_init(&new_type->allocator, allocator_type,
			properties->io_priv_size, properties->name);
	if (ret)
		goto err;

	new_type->properties = properties;

	*type = new_type;

	return 0;

err:
	env_free(new_type);
	return ret;
}

void ocf_volume_type_deinit(struct ocf_volume_type *type)
{
	if (type->properties->deinit)
		type->properties->deinit();

	ocf_io_allocator_deinit(&type->allocator);
	env_free(type);
}

/*
 * Volume frontend API
 */

int ocf_volume_init(ocf_volume_t volume, ocf_volume_type_t type,
		struct ocf_volume_uuid *uuid, bool uuid_copy)
{
	uint32_t priv_size;
	void *data;
	int ret;

	if (!volume || !type)
		return -OCF_ERR_INVAL;

	priv_size = type->properties->volume_priv_size;

	volume->opened = false;
	volume->type = type;

	volume->priv = env_zalloc(priv_size, ENV_MEM_NORMAL);
	if (!volume->priv)
		return -OCF_ERR_NO_MEM;

	ocf_refcnt_init(&volume->refcnt);
	ocf_refcnt_freeze(&volume->refcnt);

	if (!uuid) {
		volume->uuid.size = 0;
		volume->uuid.data = NULL;
		volume->uuid_copy = false;
		return 0;
	}

	volume->uuid_copy = uuid_copy;

	if (uuid_copy) {
		data = env_vmalloc(uuid->size);
		if (!data)
			goto err;

		ret = env_memcpy(data, uuid->size, uuid->data, uuid->size);
		if (ret) {
			env_vfree(data);
			goto err;
		}

		volume->uuid.data = data;
	} else {
		volume->uuid.data = uuid->data;
	}

	volume->uuid.size = uuid->size;

	return 0;

err:
	ocf_refcnt_unfreeze(&volume->refcnt);
	env_free(volume->priv);
	return -OCF_ERR_NO_MEM;
}

void ocf_volume_deinit(ocf_volume_t volume)
{
	OCF_CHECK_NULL(volume);

	env_free(volume->priv);

	if (volume->uuid_copy && volume->uuid.data) {
		env_vfree(volume->uuid.data);
		volume->uuid.data = NULL;
		volume->uuid.size = 0;
	}
}

void ocf_volume_move(ocf_volume_t volume, ocf_volume_t from)
{
	OCF_CHECK_NULL(volume);
	OCF_CHECK_NULL(from);

	ocf_volume_deinit(volume);

	volume->opened = from->opened;
	volume->type = from->type;
	volume->uuid = from->uuid;
	volume->uuid_copy = from->uuid_copy;
	volume->priv = from->priv;
	volume->cache = from->cache;
	volume->features = from->features;
	volume->refcnt = from->refcnt;

	/*
	 * Deinitialize original volume without freeing resources.
	 */
	from->opened = false;
	from->priv = NULL;
	from->uuid.data = NULL;
}

int ocf_volume_create(ocf_volume_t *volume, ocf_volume_type_t type,
		struct ocf_volume_uuid *uuid)
{
	ocf_volume_t tmp_volume;
	int ret;

	OCF_CHECK_NULL(volume);

	tmp_volume = env_zalloc(sizeof(*tmp_volume), ENV_MEM_NORMAL);
	if (!tmp_volume)
		return -OCF_ERR_NO_MEM;

	ret = ocf_volume_init(tmp_volume, type, uuid, true);
	if (ret) {
		env_free(tmp_volume);
		return ret;
	}

	*volume = tmp_volume;

	return 0;
}

void ocf_volume_destroy(ocf_volume_t volume)
{
	OCF_CHECK_NULL(volume);

	ocf_volume_deinit(volume);
	env_free(volume);
}

ocf_volume_type_t ocf_volume_get_type(ocf_volume_t volume)
{
	OCF_CHECK_NULL(volume);

	return volume->type;
}

const struct ocf_volume_uuid *ocf_volume_get_uuid(ocf_volume_t volume)
{
	OCF_CHECK_NULL(volume);

	return &volume->uuid;
}

void ocf_volume_set_uuid(ocf_volume_t volume, const struct ocf_volume_uuid *uuid)
{
	OCF_CHECK_NULL(volume);

	if (volume->uuid_copy && volume->uuid.data)
		env_vfree(volume->uuid.data);

	volume->uuid.data = uuid->data;
	volume->uuid.size = uuid->size;
}

void *ocf_volume_get_priv(ocf_volume_t volume)
{
	return volume->priv;
}

ocf_cache_t ocf_volume_get_cache(ocf_volume_t volume)
{
	OCF_CHECK_NULL(volume);

	return volume->cache;
}

int ocf_volume_is_atomic(ocf_volume_t volume)
{
	return volume->type->properties->caps.atomic_writes;
}

struct ocf_io *ocf_volume_new_io(ocf_volume_t volume, ocf_queue_t queue,
		uint64_t addr, uint32_t bytes, uint32_t dir,
		uint32_t io_class, uint64_t flags)
{
	return ocf_io_new(volume, queue, addr, bytes, dir, io_class, flags);
}

void ocf_volume_submit_io(struct ocf_io *io)
{
	ocf_volume_t volume = ocf_io_get_volume(io);

	ENV_BUG_ON(!volume->type->properties->ops.submit_io);

	if (!volume->opened)
		io->end(io, -OCF_ERR_IO);

	volume->type->properties->ops.submit_io(io);
}

void ocf_volume_submit_flush(struct ocf_io *io)
{
	ocf_volume_t volume = ocf_io_get_volume(io);

	ENV_BUG_ON(!volume->type->properties->ops.submit_flush);

	if (!volume->opened)
		io->end(io, -OCF_ERR_IO);

	if (!volume->type->properties->ops.submit_flush) {
		ocf_io_end(io, 0);
		return;
	}

	volume->type->properties->ops.submit_flush(io);
}

void ocf_volume_submit_discard(struct ocf_io *io)
{
	ocf_volume_t volume = ocf_io_get_volume(io);

	if (!volume->opened)
		io->end(io, -OCF_ERR_IO);

	if (!volume->type->properties->ops.submit_discard) {
		ocf_io_end(io, 0);
		return;
	}

	volume->type->properties->ops.submit_discard(io);
}

int ocf_volume_open(ocf_volume_t volume, void *volume_params)
{
	int ret;

	ENV_BUG_ON(!volume->type->properties->ops.open);
	ENV_BUG_ON(volume->opened);

	ret = volume->type->properties->ops.open(volume, volume_params);
	if (ret)
		return ret;

	ocf_refcnt_unfreeze(&volume->refcnt);
	volume->opened = true;

	return 0;
}

static void ocf_volume_close_end(void *ctx)
{
	env_completion *cmpl = ctx;

	env_completion_complete(cmpl);
}

void ocf_volume_close(ocf_volume_t volume)
{
	env_completion cmpl;

	ENV_BUG_ON(!volume->type->properties->ops.close);
	ENV_BUG_ON(!volume->opened);

	env_completion_init(&cmpl);
	ocf_refcnt_freeze(&volume->refcnt);
	ocf_refcnt_register_zero_cb(&volume->refcnt, ocf_volume_close_end,
			&cmpl);
	env_completion_wait(&cmpl);
	env_completion_destroy(&cmpl);

	volume->type->properties->ops.close(volume);
	volume->opened = false;
}

unsigned int ocf_volume_get_max_io_size(ocf_volume_t volume)
{
	ENV_BUG_ON(!volume->type->properties->ops.get_max_io_size);

	if (!volume->opened)
		return 0;

	return volume->type->properties->ops.get_max_io_size(volume);
}

uint64_t ocf_volume_get_length(ocf_volume_t volume)
{
	ENV_BUG_ON(!volume->type->properties->ops.get_length);

	if (!volume->opened)
		return 0;

	return volume->type->properties->ops.get_length(volume);
}
