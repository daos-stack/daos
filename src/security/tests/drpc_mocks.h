/*
 * (C) Copyright 2019 Intel Corporation.
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

/**
 * Mocks of dRPC framework functions
 */

#ifndef __DAOS_DRPC_MOCKS_H__
#define __DAOS_DRPC_MOCKS_H__

#include <daos/drpc.h>
#include "../security.pb-c.h"

/**
 * drpc_connect mock values
 */
extern struct drpc *drpc_connect_return; /* value to be returned */
extern char drpc_connect_sockaddr[PATH_MAX + 1]; /* saved copy of input */

void mock_drpc_connect_setup(void);
void mock_drpc_connect_teardown(void);

/* Convenience method to free mocks */
void free_drpc_connect_return(void);

/**
 * drpc_call mock values
 */
extern int drpc_call_return; /* value to be returned */
extern struct drpc *drpc_call_ctx; /* saved input */
extern int drpc_call_flags; /* saved input */
extern Drpc__Call drpc_call_msg_content; /* saved copy of input */
/* saved input ptr address (for checking non-NULL) */
extern Drpc__Call *drpc_call_msg_ptr;
/* saved input ptr address (for checking non-NULL) */
extern Drpc__Response **drpc_call_resp_ptr;
/* ptr to content to allocate in response (can be NULL) */
extern Drpc__Response *drpc_call_resp_return_ptr;
/* actual content to allocate in response */
extern Drpc__Response drpc_call_resp_return_content;

void mock_drpc_call_setup(void);
void mock_drpc_call_teardown(void);

/* Convenience methods to initialize mocks */
void pack_cred_in_drpc_call_resp_body(SecurityCredential *cred);
void pack_token_in_drpc_call_resp_body(AuthToken *token);

/* Convenience methods to free mocks */
void free_drpc_call_msg_body(void);
void free_drpc_call_resp_body(void);

/**
 * drpc_close mock values
 */
extern int drpc_close_return; /* value to be returned */
extern struct drpc *drpc_close_ctx; /* saved input ptr */

void mock_drpc_close_setup(void);

#endif /* __DAOS_DRPC_MOCKS_H__ */
