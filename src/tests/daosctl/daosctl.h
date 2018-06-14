/**
 * (C) Copyright 2018 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#ifndef _DAOSCTL_BUILTIN_H_
#define _DAOSCTL_BUILTIN_H_

extern const char daosctl_usage_string[];
extern const char daosctl_more_info_string[];

struct cmd_struct {
	const char *cmd;
	int (*fn)(int, const char **, void *ctx);
};

int cmd_create_container(int argc, const char **argv, void *ctx);
int cmd_create_pool(int argc, const char **argv, void *ctx);
int cmd_destroy_container(int argc, const char **argv, void *ctx);
int cmd_destroy_pool(int argc, const char **argv, void *ctx);
int cmd_evict_pool(int argc, const char **argv, void *ctx);
int cmd_exclude_target(int argc, const char **argv, void *ctx);
int cmd_help(int argc, const char **argv, void *ctx);
int cmd_kill_server(int argc, const char **argv, void *ctx);
int cmd_list(int argc, const char **argv, void *ctx);
int cmd_query_container(int argc, const char **argv, void *ctx);
int cmd_query_pool_status(int argc, const char **argv, void *ctx);
int cmd_kill_pool_leader(int argc, const char **argv, void *ctx);
/* these are test functions that maybe removed later */
int cmd_connect_pool(int argc, const char **argv, void *ctx);
int cmd_test_create_pool(int argc, const char **argv, void *ctx);
int cmd_test_connect_pool(int argc, const char **argv, void *ctx);
int cmd_test_evict_pool(int argc, const char **argv, void *ctx);
int cmd_test_query_pool(int argc, const char **argv, void *ctx);
int cmd_kill_server(int argc, const char **argv, void *ctx);
int cmd_write_pattern(int argc, const char **argv, void *ctx);
int cmd_verify_pattern(int argc, const char **argv, void *ctx);

#endif /* _DAOSCTL_BUILTIN_H_ */
