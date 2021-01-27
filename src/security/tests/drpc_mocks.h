/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * Mocks of dRPC framework functions
 */

#ifndef __DAOS_DRPC_MOCKS_H__
#define __DAOS_DRPC_MOCKS_H__

#include <daos/drpc.h>
#include "../auth.pb-c.h"

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
void pack_get_cred_resp_in_drpc_call_resp_body(Auth__GetCredResp *resp);
void pack_validate_resp_in_drpc_call_resp_body(Auth__ValidateCredResp *resp);

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
