/* Copyright (C) 2016-2018 Intel Corporation
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
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <utime.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "log.h"
#include "ctrl_common.h"
#include "ctrl_fs_util_test.h"

int cnss_dump_log(struct ctrl_info *info)
{
	return 0;
}

int cnss_client_attach(int client_id, void *arg)
{
	IOF_LOG_INFO("attached %d", client_id);
	return 0;
}

int cnss_client_detach(int client_id, void *arg)
{
	IOF_LOG_INFO("detached %d", client_id);
	return 0;
}

static int read_foo(char *buf, size_t len, void *arg)
{
	int *foo = (int *)arg;

	snprintf(buf, len, "%d", *foo);

	return 0;
}

static int write_foo(const char *value, void *arg)
{
	int val = atoi(value);
	int *foo = (int *)arg;

	*foo += val;

	return 0;
}

static int check_destroy_foo(void *arg)
{
	int *foo = (int *)arg;

	*foo = -1;

	return 0;
}

static int check_file_read(const char *fname, const char *expected,
			   const char *source, int line)
{
	char buf[IOF_CTRL_MAX_LEN];
	int rc;

	IOF_LOG_INFO("Run check at %s:%d\n", source, line);

	rc = iof_ctrl_read_str(buf, IOF_CTRL_MAX_LEN, fname);

	if (rc != 0) {
		printf("Error reading %s at %s:%d.  (rc = %d, errno = %s)\n",
		       fname, source, line, rc, strerror(errno));
		return 1;
	}

	if (strncmp(buf, expected, 100) != 0) {
		printf("Value unexpected in %s: (%s != %s).  Test"
		       " %s:%d failed\n", fname, buf, expected, source, line);
		return 1;
	}

	IOF_LOG_INFO("Done with check at %s:%d\n", source, line);

	return 0;
}

static int check_file_write(const char *fname, const char *value,
			    const char *source, int line)
{
	int rc;

	IOF_LOG_INFO("Run check at %s:%d\n", source, line);

	rc = iof_ctrl_write_str(value, fname);

	if (rc != 0) {
		printf("Error writing %s at %s:%d.  (rc = %d, errno = %s)\n",
		       fname, source, line, rc, strerror(errno));
		return 1;
	}

	return 0;
}

#define DECLARE_READ_FUNC(ext, type, fmt) \
static int check_file_read_##ext(const char *fname, type expected, \
				 const char *source, int line)     \
{                                                                             \
	type value;                                                           \
	int rc;                                                               \
	IOF_LOG_INFO("Run check at %s:%d\n", source, line);                   \
	rc = iof_ctrl_read_##ext(&value, fname);                              \
	if (rc != 0) {                                                        \
		printf("Error reading %s at %s:%d.  (rc = %d, errno = %s)\n", \
		       fname, source, line, rc, strerror(errno));             \
		return 1;                                                     \
	}                                                                     \
	if (value != expected) {                                              \
		printf("Value unexpected in %s: (" fmt " != " fmt ").  Test"  \
		       " %s:%d failed\n", fname, value, expected, source,     \
		       line);                                                 \
		return 1;                                                     \
	}                                                                     \
	IOF_LOG_INFO("Done with check at %s:%d\n", source, line);             \
	return 0;                                                             \
}

DECLARE_READ_FUNC(int32, int32_t, "%" PRId32)
DECLARE_READ_FUNC(uint32, uint32_t, "%" PRIu32)
DECLARE_READ_FUNC(int64, int64_t, "%" PRId64)
DECLARE_READ_FUNC(uint64, uint64_t, "%" PRIu64)

#define DECLARE_WRITE_FUNC(ext, type, fmt) \
static int check_file_write_##ext(const char *fname, type value,              \
			    const char *source, int line)                     \
{                                                                             \
	int rc;                                                               \
	IOF_LOG_INFO("Run check at %s:%d\n", source, line);                   \
	rc = iof_ctrl_write_##ext(value, fname);                              \
	if (rc != 0) {                                                        \
		printf("Error writing %s at %s:%d.  (rc = %d, errno = %s)\n", \
		       fname, source, line, rc, strerror(errno));             \
		return 1;                                                     \
	}                                                                     \
	return 0;                                                             \
}

DECLARE_WRITE_FUNC(int64, int64_t, "%" PRId64)
DECLARE_WRITE_FUNC(uint64, uint64_t, "%" PRIu64)

#define CHECK_FILE_READ(name, expected) \
	check_file_read(name, expected, __FILE__, __LINE__)

#define CHECK_FILE_WRITE(name, value) \
	check_file_write(name, value, __FILE__, __LINE__)

#define CHECK_FILE_READ_VAL(name, expected, ext) \
	check_file_read_##ext(name, expected, __FILE__, __LINE__)

#define CHECK_FILE_WRITE_VAL(name, value, ext) \
	check_file_write_##ext(name, value, __FILE__, __LINE__)

static int run_tests(void)
{
	int num_failures = 0;
	int id;
	int rc;

	/* Only checks the first 100 bytes so this check will work */
	num_failures += CHECK_FILE_READ("class/bar/hello", "Hello World");
	num_failures += CHECK_FILE_READ_VAL("class/bar/foo", 0, int32);
	num_failures += CHECK_FILE_WRITE("class/bar/foo", "10");
	num_failures += CHECK_FILE_READ_VAL("class/bar/foo", 10, uint32);
	num_failures += CHECK_FILE_WRITE_VAL("class/bar/foo", 55, uint64);
	num_failures += CHECK_FILE_READ_VAL("class/bar/foo", 65, int64);
	num_failures += CHECK_FILE_WRITE_VAL("class/bar/foo", -12, int64);
	num_failures += CHECK_FILE_READ_VAL("class/bar/foo", 53, uint32);
	num_failures += CHECK_FILE_READ_VAL("client", 1, int32);
	num_failures += CHECK_FILE_READ_VAL("client", 2, int32);
	num_failures += CHECK_FILE_READ_VAL("client", 3, int32);
	num_failures += CHECK_FILE_READ_VAL("client", 4, int32);
	num_failures += CHECK_FILE_READ_VAL("int", -1, int64);
	num_failures += CHECK_FILE_READ_VAL("uint", (uint64_t)-1, uint64);
	num_failures += CHECK_FILE_WRITE("dump_log", "test");
	num_failures += CHECK_FILE_WRITE("write_log", "test");

	rc = iof_ctrl_get_tracker_id(&id, "client");
	if (rc != 0 || id != 5) {
		printf("Expected 5 from client file\n");
		num_failures++;
	}

	return num_failures;
}

static int track_open(int *value, void *cb_arg)
{
	int *current = (int *)cb_arg;

	(*current)++;
	*value = *current;

	return 0;
}

static int track_close(int value, void *cb_arg)
{
	int *current = (int *)cb_arg;

	if (value != *current) {
		printf("Unexpected value for tracker %d\n", value);
		/* Changing this will cause the test to fail */
		(*current)++;
	}

	return 0;
}

int main(int argc, char **argv)
{
	char *tmp_dir = NULL;
	char *prefix = NULL;
	char *buf = NULL;
	char cmd_buf[32];
	char *tmp_name = NULL;
	int rc;
	int foo = 0;
	int opt;
	int tracker_value = 0;
	int num_failures = 0;
	bool interactive = false;
	struct ctrl_dir *class_dir;
	struct ctrl_dir *bar_dir;
	struct ctrl_info info = {0};

	for (;;) {
		opt = getopt(argc, argv, "i");
		if (opt == -1)
			break;
		switch (opt) {
		case 'i':
			interactive = true;
			break;
		default:
			printf("Usage: %s [-i]\n", argv[0]);
			printf("\n    -i  Interactive run\n");
			return -1;
		}
	}

	tmp_dir = getenv("IOF_TMP_DIR");
	if (tmp_dir == NULL) {
		buf = strdup("/tmp/iofXXXXXX");
		if (buf == NULL) {
			printf("Could not allocate memory for CNSS_PREFIX\n");
			return -1;
		}
	} else {
		rc = asprintf(&buf, "%s/iofXXXXXX", tmp_dir);
		if (rc == -1) {
			printf("Could not allocate memory for CNSS_PREFIX\n");
			return -1;
		}
	}

	prefix = mkdtemp(buf);

	if (prefix == NULL) {
		printf("Could not allocate temporary directory for tests\n");
		return -1;
	}

	printf("Testing iof_ctrl in %s\n", prefix);
	rc = asprintf(&tmp_name, "%s/iof.log", prefix);
	if (rc == -1) {
		printf("Could not allocate memory for log file name\n");
		free(buf);
		return -1;
	}

	setenv("D_LOG_FILE", tmp_name, 1);
	free(tmp_name);
	setenv("D_LOG_MASK", "INFO,ctrl=DEBUG", 1);
	iof_log_init("ctrl", "iof_ctrl_test", NULL);

	rc = asprintf(&tmp_name, "%s/.ctrl", prefix);
	if (rc == -1) {
		printf("Could not allocate memory for ctrl prefix\n");
		free(buf);
		return -1;
	}

	ctrl_fs_start(tmp_name);

	ctrl_info_init(&info);
	if (register_cnss_controls(&info) != 0)
		goto shutdown;

	ctrl_create_subdir(NULL, "class", &class_dir);
	ctrl_create_subdir(class_dir, "bar", &bar_dir);
	ctrl_register_variable(bar_dir, "foo", read_foo,
			       write_foo, check_destroy_foo, &foo);
	ctrl_register_constant(bar_dir, "hello", "Hello World");

	ctrl_register_tracker(NULL, "client", track_open, track_close,
			      check_destroy_foo, &tracker_value);
	ctrl_register_constant_int64(NULL, "int", -1);
	ctrl_register_constant_uint64(NULL, "uint", (uint64_t)-1);

	iof_ctrl_util_test_init(tmp_name);

	num_failures = run_tests();
	if (!interactive) { /* Invoke shutdown */
		rc = iof_ctrl_write_int64(1, "shutdown");
		if (rc != 0) {
			num_failures++;
			printf("shutdown trigger failed: rc = %d\n", rc);
		}
	}

	wait_for_shutdown(&info);
shutdown:

	iof_ctrl_util_test_finalize();

	ctrl_fs_shutdown();

	if (foo != -1) {
		num_failures++;
		printf("Destroy callback never invoked\n");
	}
	if (tracker_value != -1) {
		num_failures++;
		printf("Tracker destroy callback never invoked\n");
	}
	if (num_failures != 0)
		printf("%d ctrl_fs tests failed\n", num_failures);
	else
		printf("All ctrl_fs tests passed\n");

	iof_log_close();

	if (!interactive) { /* Delete the temporary directory */
		sprintf(cmd_buf, "rm -rf %s", prefix);
		system(cmd_buf);
	}

	free(tmp_name);
	free(buf);

	return num_failures;
}
