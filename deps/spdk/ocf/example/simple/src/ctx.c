/*
 * Copyright(c) 2019-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <execinfo.h>
#include <ocf/ocf.h>
#include "ocf_env.h"
#include "data.h"
#include "volume.h"
#include "ctx.h"

#define PAGE_SIZE 4096

/*
 * Allocate structure representing data for io operations.
 */
ctx_data_t *ctx_data_alloc(uint32_t pages)
{
	struct volume_data *data;

	data = malloc(sizeof(*data));
	data->ptr = malloc(pages * PAGE_SIZE);
	data->offset = 0;

	return data;
}

/*
 * Free data structure.
 */
void ctx_data_free(ctx_data_t *ctx_data)
{
	struct volume_data *data = ctx_data;

	if (!data)
		return;

	free(data->ptr);
	free(data);
}

/*
 * This function is supposed to set protection of data pages against swapping.
 * Can be non-implemented if not needed.
 */
static int ctx_data_mlock(ctx_data_t *ctx_data)
{
	return 0;
}

/*
 * Stop protecting data pages against swapping.
 */
static void ctx_data_munlock(ctx_data_t *ctx_data)
{
}

/*
 * Read data into flat memory buffer.
 */
static uint32_t ctx_data_read(void *dst, ctx_data_t *src, uint32_t size)
{
	struct volume_data *data = src;

	memcpy(dst, data->ptr + data->offset, size);

	return size;
}

/*
 * Write data from flat memory buffer.
 */
static uint32_t ctx_data_write(ctx_data_t *dst, const void *src, uint32_t size)
{
	struct volume_data *data = dst;

	memcpy(data->ptr + data->offset, src, size);

	return size;
}

/*
 * Fill data with zeros.
 */
static uint32_t ctx_data_zero(ctx_data_t *dst, uint32_t size)
{
	struct volume_data *data = dst;

	memset(data->ptr + data->offset, 0, size);

	return size;
}

/*
 * Perform seek operation on data.
 */
static uint32_t ctx_data_seek(ctx_data_t *dst, ctx_data_seek_t seek,
		uint32_t offset)
{
	struct volume_data *data = dst;

	switch (seek) {
	case ctx_data_seek_begin:
		data->offset = offset;
		break;
	case ctx_data_seek_current:
		data->offset += offset;
		break;
	}

	return offset;
}

/*
 * Copy data from one structure to another.
 */
static uint64_t ctx_data_copy(ctx_data_t *dst, ctx_data_t *src,
		uint64_t to, uint64_t from, uint64_t bytes)
{
	struct volume_data *data_dst = dst;
	struct volume_data *data_src = src;

	memcpy(data_dst->ptr + to, data_src->ptr + from, bytes);

	return bytes;
}

/*
 * Perform secure erase of data (e.g. fill pages with zeros).
 * Can be left non-implemented if not needed.
 */
static void ctx_data_secure_erase(ctx_data_t *ctx_data)
{
}

/*
 * Initialize cleaner thread. Cleaner thread is left non-implemented,
 * to keep this example as simple as possible.
 */
static int ctx_cleaner_init(ocf_cleaner_t c)
{
	return 0;
}

/*
 * Kick cleaner thread. Cleaner thread is left non-implemented,
 * to keep this example as simple as possible.
 */
static void ctx_cleaner_kick(ocf_cleaner_t c)
{
}

/*
 * Stop cleaner thread. Cleaner thread is left non-implemented, to keep
 * this example as simple as possible.
 */
static void ctx_cleaner_stop(ocf_cleaner_t c)
{
}

/*
 * Function prividing interface for printing to log used by OCF internals.
 * It can handle differently messages at varous log levels.
 */
static int ctx_logger_print(ocf_logger_t logger, ocf_logger_lvl_t lvl,
		const char *fmt, va_list args)
{
	FILE *lfile = stdout;

	if (lvl > log_info)
		return 0;

	if (lvl <= log_warn)
		lfile = stderr;

	return vfprintf(lfile, fmt, args);
}

#define CTX_LOG_TRACE_DEPTH	16

/*
 * Function prividing interface for printing current stack. Used for debugging,
 * and for providing additional information in log in case of errors.
 */
static int ctx_logger_dump_stack(ocf_logger_t logger)
{
	void *trace[CTX_LOG_TRACE_DEPTH];
	char **messages = NULL;
	int i, size;

	size = backtrace(trace, CTX_LOG_TRACE_DEPTH);
	messages = backtrace_symbols(trace, size);
	printf("[stack trace]>>>\n");
	for (i = 0; i < size; ++i)
		printf("%s\n", messages[i]);
	printf("<<<[stack trace]\n");
	free(messages);

	return 0;
}

/*
 * This structure describes context config, containing simple context info
 * and pointers to ops callbacks. Ops are splitted into few categories:
 * - data ops, providing context specific data handing interface,
 * - cleaner ops, providing interface to start and stop clener thread,
 * - metadata updater ops, providing interface for starting, stoping
 *   and kicking metadata updater thread.
 * - logger ops, providing interface for text message logging
 */
static const struct ocf_ctx_config ctx_cfg = {
	.name = "OCF Example",
	.ops = {
		.data = {
			.alloc = ctx_data_alloc,
			.free = ctx_data_free,
			.mlock = ctx_data_mlock,
			.munlock = ctx_data_munlock,
			.read = ctx_data_read,
			.write = ctx_data_write,
			.zero = ctx_data_zero,
			.seek = ctx_data_seek,
			.copy = ctx_data_copy,
			.secure_erase = ctx_data_secure_erase,
		},

		.cleaner = {
			.init = ctx_cleaner_init,
			.kick = ctx_cleaner_kick,
			.stop = ctx_cleaner_stop,
		},

		.logger = {
			.print = ctx_logger_print,
			.dump_stack = ctx_logger_dump_stack,
		},
	},
};


/*
 * Function initializing context. Prepares context, sets logger and
 * registers volume type.
 */
int ctx_init(ocf_ctx_t *ctx)
{
	int ret;

	ret = ocf_ctx_create(ctx, &ctx_cfg);
	if (ret)
		return ret;

	ret = volume_init(*ctx);
	if (ret) {
		ocf_ctx_put(*ctx);
		return ret;
	}

	return 0;
}

/*
 * Function cleaning up context. Unregisters volume type and
 * deinitializes context.
 */
void ctx_cleanup(ocf_ctx_t ctx)
{
	volume_cleanup(ctx);
	ocf_ctx_put(ctx);
}
