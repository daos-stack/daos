/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef ENGINE_DEBUG_H_
#define ENGINE_DEBUG_H_

#ifndef OCF_ENGINE_DEBUG
#define OCF_ENGINE_DEBUG 0
#endif

#if 1 == OCF_ENGINE_DEBUG

#ifndef OCF_ENGINE_DEBUG_IO_NAME
#define OCF_ENGINE_DEBUG_IO_NAME "null"
#endif

#define OCF_DEBUG_PREFIX "[Engine][%s] %s "

#define OCF_DEBUG_LOG(cache, format, ...) \
	ocf_cache_log_prefix(cache, log_info, OCF_DEBUG_PREFIX, \
			format"\n", OCF_ENGINE_DEBUG_IO_NAME, __func__, \
			##__VA_ARGS__)

#define OCF_DEBUG_TRACE(cache) OCF_DEBUG_LOG(cache, "")

#define OCF_DEBUG_MSG(cache, msg) OCF_DEBUG_LOG(cache, "- %s", msg)

#define OCF_DEBUG_PARAM(cache, format, ...) OCF_DEBUG_LOG(cache, "- "format, \
			##__VA_ARGS__)

#define OCF_DEBUG_RQ(req, format, ...) \
	ocf_cache_log(req->cache, log_info, "[Engine][%s][%s, %llu, %u] %s - " \
		format"\n", OCF_ENGINE_DEBUG_IO_NAME, \
		OCF_READ == (req)->rw ? "RD" : "WR", req->byte_position, \
		req->byte_length, __func__, ##__VA_ARGS__)

#else
#define OCF_DEBUG_PREFIX
#define OCF_DEBUG_LOG(cache, format, ...)
#define OCF_DEBUG_TRACE(cache)
#define OCF_DEBUG_MSG(cache, msg)
#define OCF_DEBUG_PARAM(cache, format, ...)
#define OCF_DEBUG_RQ(req, format, ...)
#endif

#endif /* ENGINE_DEBUG_H_ */
