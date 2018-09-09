/*
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#ifndef __DAOS_DRPC_H__
#define __DAOS_DRPC_H__

#include <daos/drpc.pb-c.h>

/*
 * Using a packetsocket over the unix domain socket means that we receive
 * a whole message at a time without knowing its size. So for this reason
 * we need to restrict the maximum message size so we can preallocate a
 * buffer to put all of the information in. This value is also defined in
 * the corresponding golang file domain_socket_server.go. If changed here
 * it must be changed in that file as well
 */
#define UNIXCOMM_MAXMSGSIZE 16384

struct unixcomm {
	int fd;
	int flags;
};

struct drpc {
	struct unixcomm *comm;
	int sequence;
};

enum rpcflags {
	R_SYNC = 1
};

int drpc_call(struct drpc *ctx, int flags, Drpc__Call *msg,
		Drpc__Response **resp);
struct drpc *drpc_connect(char *sockaddr);
int drpc_close(struct drpc *ctx);

#endif /* __DAOS_DRPC_H__ */
