/* Copyright (C) 2017-2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __IOF_CTRL_UTIL_H__
#define __IOF_CTRL_UTIL_H__

#include <iof_defines.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Maximum length of a CTRL string with NULL character */
#define IOF_CTRL_MAX_LEN 4096

#define IOF_CTRL_MAX_CONSTANT_LEN 128

enum iof_ctrl_error {
	IOF_CTRL_SUCCESS,
	IOF_CTRL_NOT_FOUND,
	IOF_CTRL_INVALID_ARG,
	IOF_CTRL_NOT_INITIALIZED,
	IOF_CTRL_OPEN_FAILED,
	IOF_CTRL_IO_FAILED,
	IOF_CTRL_BAD_FILE,
};

/* Initialize the cnss ctrl fs utility library.  Returns the
 * CNSS_PREFIX if found and the CNSS identifier
 */
int IOF_PUBLIC iof_ctrl_util_init(const char **prefix, int *id);

/* Finalize the cnss ctrl fs utility library. */
int IOF_PUBLIC iof_ctrl_util_finalize(void);

/* Copies contents of ctrl file to str if NULL terminated
 * string will fit in len characters.
 * \param str[out] buffer to write
 * \param len[in] size in bytes of str
 * \param path[in] path to control (relative to ctrl root)
 * \retval 0 if successful
 * \retval required_len if not enough space (str unchanged)
 * \retval -errcode on any other error
 */
int IOF_PUBLIC iof_ctrl_read_str(char *str, int len, const char *path);

/* Gets a value from ctrl fs as a 64-bit integer
 * \param val[out] value to write
 * \param path[in] path to control (relative to ctrl root)
 * \retval 0 on success
 * \retval -errcode on error
 */
int IOF_PUBLIC iof_ctrl_read_int64(int64_t *val, const char *path);

/* Gets a value from ctrl fs as a 32-bit integer
 * \param val[out] value to write
 * \param path[in] path to control (relative to ctrl root)
 * \retval 0 on success
 * \retval -errcode on error
 */
int IOF_PUBLIC iof_ctrl_read_int32(int32_t *val, const char *path);

/* Gets a value from ctrl fs as a 64-bit unsigned integer
 * \param val[out] value to write
 * \param path[in] path to control (relative to ctrl root)
 * \retval 0 on success
 * \retval -errcode on error
 */
int IOF_PUBLIC iof_ctrl_read_uint64(uint64_t *val, const char *path);

/* Gets a value from ctrl fs as a 32-bit unsigned integer
 * \param val[out] value to write
 * \param path[in] path to control (relative to ctrl root)
 * \retval 0 on success
 * \retval -errcode on error
 */
int IOF_PUBLIC iof_ctrl_read_uint32(uint32_t *val, const char *path);

/* Trigger a control event
 * \param path[in] path to control (relative to ctrl root)
 * \retval 0 on success
 * \retval -errcode on error
 */
int IOF_PUBLIC iof_ctrl_trigger(const char *path);

/* Gets a tracker id from ctrl fs
 * \param val[out] value to write
 * \param path[in] path to control (relative to ctrl root)
 * \retval 0 on success
 * \retval -errcode on error
 */
int IOF_PUBLIC iof_ctrl_get_tracker_id(int *value, const char *path);

/* Write a string to a ctrl file
 * \param str[in] value to write
 * \param path[in] path to control (relative to ctrl root)
 * \retval 0 if successful
 * \retval required_len if not enough space (str unchanged)
 * \retval -errcode on any other error
 */
#define iof_ctrl_write_str(str, path) \
	iof_ctrl_write_strf(path, "%s", str)

/* Write format string to ctrl variable
 * \param path[in] path to control (relative to ctrl root)
 * \param format[in] format string
 * \retval 0 if successful
 * \retval required_len if not enough space (str unchanged)
 * \retval -errcode on any other error
 */
int IOF_PUBLIC iof_ctrl_write_strf(const char *path, const char *format, ...)
	__attribute__ ((__format__(__printf__, 2, 3)));

/* Write a 64 bit int to a ctrl file
 * \param val[in] value to write
 * \param path[in] path to control (relative to ctrl root)
 * \retval 0 if successful
 * \retval -errcode on any other error
 */
#define iof_ctrl_write_int64(val, path) \
	iof_ctrl_write_strf(path, "%" PRIi64, ((int64_t)(val)))

/* Write a 64 bit uint to a ctrl file
 * \param val[in] value to write
 * \param path[in] path to control (relative to ctrl root)
 * \retval 0 if successful
 * \retval -errcode on any other error
 */
#define iof_ctrl_write_uint64(val, path) \
	iof_ctrl_write_strf(path, "%" PRIu64, ((uint64_t)(val)))

#if defined(__cplusplus)
extern "C" }
#endif

#endif /* __IOF_CTRL_UTIL_H__ */
