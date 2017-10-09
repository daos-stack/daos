/* Copyright (C) 2016-2017 Intel Corporation
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
/**
 * This file is part of the CaRT echo example which is based on CaRT APIs.
 */

#ifndef __CRT_ECHO_H__
#define __CRT_ECHO_H__

#include <gurt/common.h>
#include <cart/api.h>

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <openssl/md5.h>

#define ECHO_OPC_NOOP       (0xA0)
#define ECHO_OPC_CHECKIN    (0xA1)
#define ECHO_OPC_BULK_TEST  (0xA2)
#define ECHO_OPC_SHUTDOWN   (0x100)
#define ECHO_CORPC_EXAMPLE  (0x886)

#define ECHO_EXTRA_CONTEXT_NUM (3)

#define ECHO_2ND_TIER_GRPID	"echo_2nd_tier"

struct gecho {
	crt_context_t	crt_ctx;
	crt_context_t	*extra_ctx;
	int		complete;
	bool		server;
	bool		multi_tier_test;
	bool		singleton_test;
	sem_t		token_to_proceed;
};

extern struct gecho gecho;

extern struct crt_corpc_ops echo_co_ops;

static inline
void echo_srv_noop(crt_rpc_t *rpc_req)
{
	printf("echo_srver recv'd NOOP RPC, opc: %#x.\n",
	       rpc_req->cr_opc);
	crt_reply_send(rpc_req);
}

void echo_srv_checkin(crt_rpc_t *rpc);
void echo_srv_bulk_test(crt_rpc_t *rpc);
void echo_srv_shutdown(crt_rpc_t *rpc);
void echo_srv_corpc_example(crt_rpc_t *rpc);

struct crt_msg_field *echo_ping_checkin[] = {
	&CMF_UINT32,
	&CMF_UINT32,
	&CMF_IOVEC,
	&CMF_STRING,
};
struct crt_echo_checkin_req {
	int		age;
	int		days;
	d_iov_t		raw_package;
	d_string_t	name;
};

struct crt_msg_field *echo_ping_checkout[] = {
	&CMF_INT,
	&CMF_UINT32,
};
struct crt_echo_checkin_reply {
	int ret;
	uint32_t room_no;
};

struct crt_msg_field *echo_corpc_example_in[] = {
	&CMF_STRING,
};
struct crt_echo_corpc_example_req {
	d_string_t	co_msg;
};

struct crt_msg_field *echo_corpc_example_out[] = {
	&CMF_UINT32,
};
struct crt_echo_corpc_example_reply {
	uint32_t	co_result;
};

struct crt_msg_field *echo_bulk_test_in[] = {
	&CMF_STRING,
	&CMF_STRING,
	&CMF_BULK,
};
struct crt_echo_bulk_in_req {
	d_string_t bulk_intro_msg;
	d_string_t bulk_md5_ptr;
	crt_bulk_t remote_bulk_hdl;
};

struct crt_msg_field *echo_bulk_test_out[] = {
	&CMF_STRING,
	&CMF_INT,
};

struct crt_echo_bulk_out_reply {
	char *echo_msg;
	int ret;
};

struct crt_req_format CQF_ECHO_NOOP =
	DEFINE_CRT_REQ_FMT("ECHO_PING_NOOP", NULL, NULL);

struct crt_req_format CQF_ECHO_PING_CHECK =
	DEFINE_CRT_REQ_FMT("ECHO_PING_CHECK", echo_ping_checkin,
			   echo_ping_checkout);

struct crt_req_format CQF_ECHO_CORPC_EXAMPLE =
	DEFINE_CRT_REQ_FMT("ECHO_CORPC_EXAMPLE", echo_corpc_example_in,
			   echo_corpc_example_out);

struct crt_req_format CQF_ECHO_BULK_TEST =
	DEFINE_CRT_REQ_FMT("ECHO_BULK_TEST", echo_bulk_test_in,
			   echo_bulk_test_out);

static inline void
parse_options(int argc, char *argv[])
{
	int ch;
	int rc;

	for (;;) {
		ch = getopt(argc, argv, "mp:s");
		if (ch == -1)
			break;
		switch (ch) {
		case 'm':
			gecho.multi_tier_test = true;
			break;
		case 'p':
			rc = crt_group_config_path_set(optarg);
			if (rc != 0) {
				printf("Bad attach prefix: %s\n", optarg);
				exit(-1);
			}
			break;
		case 's':
			gecho.singleton_test = true;
			break;
		default:
			printf("Usage: %s [OPTIONS]\n", argv[0]);
			printf("OPTIONS:\n");
			printf("	-m		multi tier test\n");
			printf("	-p <dir>	path to attach file\n");
			printf("	-s		singleton attach\n");
			exit(-1);
		}
	}
}

static inline void
echo_init(int server, bool tier2)
{
	crt_group_t	*tier2_grp;
	uint32_t	flags;
	int		rc = 0, i;

	rc = sem_init(&gecho.token_to_proceed, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");
	flags = (server != 0) ? CRT_FLAG_BIT_SERVER : 0;
	if (server == 0 && gecho.singleton_test)
		flags |= CRT_FLAG_BIT_SINGLETON;

	if (server != 0 && tier2 == true)
		rc = crt_init(ECHO_2ND_TIER_GRPID, flags);
	else
		rc = crt_init(NULL, flags);

	assert(rc == 0);

	rc = crt_context_create(NULL, &gecho.crt_ctx);
	assert(rc == 0);

	if (server != 0 && tier2 == false && gecho.singleton_test) {
		printf("Saving singleton attach info\n");
		rc = crt_group_config_save(NULL, false);
		assert(rc == 0);

		if (gecho.multi_tier_test) {
			/* Test saving attach info for another group */
			rc = crt_group_attach(ECHO_2ND_TIER_GRPID, &tier2_grp);
			assert(rc == 0 && tier2_grp != NULL);
			rc = crt_group_config_save(tier2_grp, false);
			assert(rc == 0);
			rc = crt_group_detach(tier2_grp);
			assert(rc == 0);
		}
	}

	gecho.server = (server != 0);

	if (server && ECHO_EXTRA_CONTEXT_NUM > 0) {
		gecho.extra_ctx = calloc(ECHO_EXTRA_CONTEXT_NUM,
					 sizeof(crt_context_t));
		assert(gecho.extra_ctx != NULL);
		for (i = 0; i < ECHO_EXTRA_CONTEXT_NUM; i++) {
			rc = crt_context_create(NULL, &gecho.extra_ctx[i]);
			assert(rc == 0);
		}
	}

	/* Just show the case that the client does not know the rpc handler,
	 * then client side can use crt_rpc_register, and server side can use
	 * crt_rpc_srv_register.
	 * If both client and server side know the rpc handler, they can call
	 * the same crt_rpc_srv_register.
	 */
	if (server == 0) {
		rc = crt_rpc_register(ECHO_OPC_NOOP, &CQF_ECHO_NOOP);
		assert(rc == 0);
		rc = crt_rpc_register(ECHO_OPC_CHECKIN, &CQF_ECHO_PING_CHECK);
		assert(rc == 0);
		rc = crt_rpc_register(ECHO_OPC_BULK_TEST, &CQF_ECHO_BULK_TEST);
		assert(rc == 0);
		rc = crt_rpc_register(ECHO_OPC_SHUTDOWN, NULL);
		assert(rc == 0);
		rc = crt_rpc_set_feats(ECHO_OPC_SHUTDOWN,
				       CRT_RPC_FEAT_NO_REPLY);
		assert(rc == 0);
	} else {
		rc = crt_rpc_srv_register(ECHO_OPC_NOOP,
					  &CQF_ECHO_NOOP,
					  echo_srv_noop);
		assert(rc == 0);
		rc = crt_rpc_srv_register(ECHO_OPC_CHECKIN,
					  &CQF_ECHO_PING_CHECK,
					  echo_srv_checkin);
		assert(rc == 0);
		rc = crt_rpc_srv_register(ECHO_OPC_BULK_TEST,
					  &CQF_ECHO_BULK_TEST,
					  echo_srv_bulk_test);
		assert(rc == 0);
		rc = crt_rpc_srv_register(ECHO_OPC_SHUTDOWN, NULL,
					  echo_srv_shutdown);
		assert(rc == 0);
		rc = crt_rpc_set_feats(ECHO_OPC_SHUTDOWN,
				       CRT_RPC_FEAT_NO_REPLY);
		assert(rc == 0);
		rc = crt_corpc_register(ECHO_CORPC_EXAMPLE,
					&CQF_ECHO_CORPC_EXAMPLE,
					echo_srv_corpc_example, &echo_co_ops);
		assert(rc == 0);
	}
}

static inline void
echo_fini(void)
{
	int rc = 0, i;

	rc = crt_context_destroy(gecho.crt_ctx, 0);
	assert(rc == 0);

	if (gecho.server && ECHO_EXTRA_CONTEXT_NUM > 0) {
		for (i = 0; i < ECHO_EXTRA_CONTEXT_NUM; i++) {
			rc = crt_context_destroy(gecho.extra_ctx[i], 0);
			assert(rc == 0);
		}
		free(gecho.extra_ctx);
	}

	rc = sem_destroy(&gecho.token_to_proceed);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");
	rc = crt_finalize();
	assert(rc == 0);
}

/* convert to string just to facilitate the pack/unpack */
static inline void
echo_md5_to_string(unsigned char *md5, d_string_t md5_str)
{
	char tmp[3] = {'\0'};
	int i;

	assert(md5 != NULL && md5_str != NULL);

	for (i = 0; i < 16; i++) {
		sprintf(tmp, "%02x", md5[i]);
		strcat(md5_str, tmp);
	}
}

void
client_cb_common(const struct crt_cb_info *cb_info)
{
	crt_rpc_t		*rpc_req;
	struct crt_echo_checkin_req *e_req;
	struct crt_echo_checkin_reply *e_reply;
	struct crt_echo_corpc_example_reply *corpc_reply;

	rpc_req = cb_info->cci_rpc;

	/* set complete flag */
	printf("in client_cb_common, opc: %#x, cci_rc: %d.\n",
	       rpc_req->cr_opc, cb_info->cci_rc);
	if (cb_info->cci_arg != NULL)
		*(int *) cb_info->cci_arg = 1;
	assert(cb_info->cci_rc != -DER_TIMEDOUT);

	switch (cb_info->cci_rpc->cr_opc) {
	case ECHO_OPC_CHECKIN:
		e_req = crt_req_get(rpc_req);
		if (e_req == NULL)
			return;

		e_reply = crt_reply_get(rpc_req);
		if (e_reply == NULL)
			return;

		printf("%s checkin result - ret: %d, room_no: %d.\n",
		       e_req->name, e_reply->ret, e_reply->room_no);
		sem_post(&gecho.token_to_proceed);
		break;
	case ECHO_CORPC_EXAMPLE:
		corpc_reply = crt_reply_get(rpc_req);
		printf("ECHO_CORPC_EXAMPLE finished, co_result: %d.\n",
		       corpc_reply->co_result);
		sem_post(&gecho.token_to_proceed);
		break;
	default:
		break;
	}
}

#endif /* __CRT_ECHO_H__ */
