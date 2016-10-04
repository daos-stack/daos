/**
 * (C) Copyright 2016 Intel Corporation.
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
/**
 * This file is part of the dtp echo example which is based on dtp APIs.
 */

#ifndef __DTP_ECHO_H__
#define __DTP_ECHO_H__

#include <daos/common.h>
#include <daos/transport.h>

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <openssl/md5.h>

#define ECHO_OPC_CHECKIN    (0xA1)
#define ECHO_OPC_BULK_TEST  (0xA2)
#define ECHO_OPC_SHUTDOWN   (0x100)
#define ECHO_CORPC_EXAMPLE  (0x886)

#define ECHO_EXTRA_CONTEXT_NUM (3)

struct gecho {
	dtp_context_t	dtp_ctx;
	dtp_context_t	*extra_ctx;
	int		complete;
	bool		server;
};

extern struct gecho gecho;

extern struct dtp_corpc_ops echo_co_ops;

int echo_srv_checkin(dtp_rpc_t *rpc);
int echo_srv_bulk_test(dtp_rpc_t *rpc);
int echo_srv_shutdown(dtp_rpc_t *rpc);
int echo_srv_corpc_example(dtp_rpc_t *rpc);

struct dtp_msg_field *echo_ping_checkin[] = {
	&DMF_UINT32,
	&DMF_UINT32,
	&DMF_STRING,
};
struct dtp_echo_checkin_req {
	int age;
	int days;
	dtp_string_t name;
};

struct dtp_msg_field *echo_ping_checkout[] = {
	&DMF_INT,
	&DMF_UINT32,
};
struct dtp_echo_checkin_reply {
	int ret;
	uint32_t room_no;
};

struct dtp_msg_field *echo_corpc_example_in[] = {
	&DMF_STRING,
};
struct dtp_echo_corpc_example_req {
	dtp_string_t	co_msg;
};

struct dtp_msg_field *echo_corpc_example_out[] = {
	&DMF_UINT32,
};
struct dtp_echo_corpc_example_reply {
	uint32_t	co_result;
};

struct dtp_msg_field *echo_bulk_test_in[] = {
	&DMF_STRING,
	&DMF_STRING,
	&DMF_BULK,
};
struct dtp_echo_bulk_in_req {
	dtp_string_t bulk_intro_msg;
	dtp_string_t bulk_md5_ptr;
	dtp_bulk_t remote_bulk_hdl;
};

struct dtp_msg_field *echo_bulk_test_out[] = {
	&DMF_STRING,
	&DMF_INT,
};
struct dtp_echo_bulk_out_reply {
	char *echo_msg;
	int ret;
};

struct dtp_req_format DQF_ECHO_PING_CHECK =
	DEFINE_DTP_REQ_FMT("ECHO_PING_CHECK", echo_ping_checkin,
			   echo_ping_checkout);

struct dtp_req_format DQF_ECHO_CORPC_EXAMPLE =
	DEFINE_DTP_REQ_FMT("ECHO_CORPC_EXAMPLE", echo_corpc_example_in,
			   echo_corpc_example_out);

struct dtp_req_format DQF_ECHO_BULK_TEST =
	DEFINE_DTP_REQ_FMT("ECHO_BULK_TEST", echo_bulk_test_in,
			   echo_bulk_test_out);

static inline void
echo_init(int server)
{
	int rc = 0, i;

	rc = dtp_init(server);
	assert(rc == 0);

	gecho.server = (server != 0);

	rc = dtp_context_create(NULL, &gecho.dtp_ctx);
	assert(rc == 0);

	if (server && ECHO_EXTRA_CONTEXT_NUM > 0) {
		gecho.extra_ctx = calloc(ECHO_EXTRA_CONTEXT_NUM,
					 sizeof(dtp_context_t));
		assert(gecho.extra_ctx != NULL);
		for (i = 0; i < ECHO_EXTRA_CONTEXT_NUM; i++) {
			rc = dtp_context_create(NULL, &gecho.extra_ctx[i]);
			assert(rc == 0);
		}
	}

	/* Just show the case that the client does not know the rpc handler,
	 * then client side can use dtp_rpc_reg, and server side can use
	 * dtp_rpc_srv_reg.
	 * If both client and server side know the rpc handler, they can call
	 * the same dtp_rpc_srv_reg. */
	if (server == 0) {
		rc = dtp_rpc_reg(ECHO_OPC_CHECKIN, &DQF_ECHO_PING_CHECK);
		assert(rc == 0);
		rc = dtp_rpc_reg(ECHO_OPC_BULK_TEST, &DQF_ECHO_BULK_TEST);
		assert(rc == 0);
		rc = dtp_rpc_reg(ECHO_OPC_SHUTDOWN, NULL);
		assert(rc == 0);
	} else {
		rc = dtp_rpc_srv_reg(ECHO_OPC_CHECKIN, &DQF_ECHO_PING_CHECK,
				     echo_srv_checkin);
		assert(rc == 0);
		rc = dtp_rpc_srv_reg(ECHO_OPC_BULK_TEST, &DQF_ECHO_BULK_TEST,
				     echo_srv_bulk_test);
		assert(rc == 0);
		rc = dtp_rpc_srv_reg(ECHO_OPC_SHUTDOWN, NULL,
				     echo_srv_shutdown);
		assert(rc == 0);
		rc = dtp_corpc_reg(ECHO_CORPC_EXAMPLE, &DQF_ECHO_CORPC_EXAMPLE,
				   echo_srv_corpc_example, &echo_co_ops);
	}
}

static inline void
echo_fini(void)
{
	int rc = 0, i;

	rc = dtp_context_destroy(gecho.dtp_ctx, 0);
	assert(rc == 0);

	if (gecho.server && ECHO_EXTRA_CONTEXT_NUM > 0) {
		for (i = 0; i < ECHO_EXTRA_CONTEXT_NUM; i++) {
			rc = dtp_context_destroy(gecho.extra_ctx[i], 0);
			assert(rc == 0);
		}
	}

	rc = dtp_finalize();
	assert(rc == 0);
}

/* convert to string just to facilitate the pack/unpack */
static inline void
echo_md5_to_string(unsigned char *md5, dtp_string_t md5_str)
{
	char tmp[3] = {'\0'};
	int i;

	assert(md5 != NULL && md5_str != NULL);

	for (i = 0; i < 16; i++) {
		sprintf(tmp, "%02x", md5[i]);
		strcat(md5_str, tmp);
	}
}

int client_cb_common(const struct dtp_cb_info *cb_info)
{
	dtp_rpc_t		*rpc_req;
	struct dtp_echo_checkin_req *e_req;
	struct dtp_echo_checkin_reply *e_reply;
	struct dtp_echo_corpc_example_reply *corpc_reply;

	rpc_req = cb_info->dci_rpc;

	/* set complete flag */
	printf("in client_cb_common, opc: 0x%x, dci_rc: %d.\n",
	       rpc_req->dr_opc, cb_info->dci_rc);
	*(int *) cb_info->dci_arg = 1;

	switch (cb_info->dci_rpc->dr_opc) {
	case ECHO_OPC_CHECKIN:
		e_req = dtp_req_get(rpc_req);
		if (e_req == NULL)
			return -DER_INVAL;

		e_reply = dtp_reply_get(rpc_req);
		if (e_reply == NULL)
			return -DER_INVAL;

		printf("%s checkin result - ret: %d, room_no: %d.\n",
		       e_req->name, e_reply->ret, e_reply->room_no);
		break;
	case ECHO_OPC_SHUTDOWN:
		break;
	case ECHO_CORPC_EXAMPLE:
		corpc_reply = dtp_reply_get(rpc_req);
		printf("ECHO_CORPC_EXAMPLE finished, co_result: %d.\n",
		       corpc_reply->co_result);
		break;
	default:
		break;
	}

	return 0;
}

#endif /* __DTP_ECHO_H__ */
