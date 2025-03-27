/*
 * Copyright(c) 2019-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <ocf/ocf.h>
#include "volume.h"
#include "data.h"
#include "ctx.h"

#define VOL_SIZE 200*1024*1024

/*
 * In open() function we store uuid data as volume name (for debug messages)
 * and allocate 200 MiB of memory to simulate backend storage device.
 */
static int volume_open(ocf_volume_t volume, void *volume_params)
{
	const struct ocf_volume_uuid *uuid = ocf_volume_get_uuid(volume);
	struct myvolume *myvolume = ocf_volume_get_priv(volume);

	myvolume->name = ocf_uuid_to_str(uuid);
	myvolume->mem = malloc(VOL_SIZE);

	printf("VOL OPEN: (name: %s)\n", myvolume->name);

	return 0;
}

/*
 * In close() function we just free memory allocated in open().
 */
static void volume_close(ocf_volume_t volume)
{
	struct myvolume *myvolume = ocf_volume_get_priv(volume);

	printf("VOL CLOSE: (name: %s)\n", myvolume->name);
	free(myvolume->mem);
}

/*
 * In submit_io() function we simulate read or write to backend storage device
 * by doing memcpy() to or from previously allocated memory buffer.
 */
static void volume_submit_io(struct ocf_io *io)
{
	struct volume_data *data;
	struct myvolume *myvolume;

	data = ocf_io_get_data(io);
	myvolume = ocf_volume_get_priv(ocf_io_get_volume(io));

	if (io->dir == OCF_WRITE) {
		memcpy(myvolume->mem + io->addr,
				data->ptr + data->offset, io->bytes);
	} else {
		memcpy(data->ptr + data->offset,
				myvolume->mem + io->addr, io->bytes);
	}

	printf("VOL: (name: %s), IO: (dir: %s, addr: %ld, bytes: %d)\n",
			myvolume->name, io->dir == OCF_READ ? "read" : "write",
			io->addr, io->bytes);

	io->end(io, 0);
}

/*
 * We don't need to implement submit_flush(). Just complete io with success.
 */
static void volume_submit_flush(struct ocf_io *io)
{
	io->end(io, 0);
}

/*
 * We don't need to implement submit_discard(). Just complete io with success.
 */
static void volume_submit_discard(struct ocf_io *io)
{
	io->end(io, 0);
}

/*
 * Let's set maximum io size to 128 KiB.
 */
static unsigned int volume_get_max_io_size(ocf_volume_t volume)
{
	return 128 * 1024;
}

/*
 * Return volume size.
 */
static uint64_t volume_get_length(ocf_volume_t volume)
{
	return VOL_SIZE;
}

/*
 * In set_data() we just assing data and offset to io.
 */
static int myvolume_io_set_data(struct ocf_io *io, ctx_data_t *data,
		uint32_t offset)
{
	struct myvolume_io *myvolume_io = ocf_io_get_priv(io);

	myvolume_io->data = data;
	myvolume_io->offset = offset;

	return 0;
}

/*
 * In get_data() return data stored in io.
 */
static ctx_data_t *myvolume_io_get_data(struct ocf_io *io)
{
	struct myvolume_io *myvolume_io = ocf_io_get_priv(io);

	return myvolume_io->data;
}

/*
 * This structure contains volume properties. It describes volume
 * type, which can be later instantiated as backend storage for cache
 * or core.
 */
const struct ocf_volume_properties volume_properties = {
	.name = "Example volume",
	.io_priv_size = sizeof(struct myvolume_io),
	.volume_priv_size = sizeof(struct myvolume),
	.caps = {
		.atomic_writes = 0,
	},
	.ops = {
		.open = volume_open,
		.close = volume_close,
		.submit_io = volume_submit_io,
		.submit_flush = volume_submit_flush,
		.submit_discard = volume_submit_discard,
		.get_max_io_size = volume_get_max_io_size,
		.get_length = volume_get_length,
	},
	.io_ops = {
		.set_data = myvolume_io_set_data,
		.get_data = myvolume_io_get_data,
	},
};

/*
 * This function registers volume type in OCF context.
 * It should be called just after context initialization.
 */
int volume_init(ocf_ctx_t ocf_ctx)
{
	return ocf_ctx_register_volume_type(ocf_ctx, VOL_TYPE,
			&volume_properties);
}

/*
 * This function unregisters volume type in OCF context.
 * It should be called just before context cleanup.
 */
void volume_cleanup(ocf_ctx_t ocf_ctx)
{
	ocf_ctx_unregister_volume_type(ocf_ctx, VOL_TYPE);
}
