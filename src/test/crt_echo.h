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
	bool		grp_destroy_piggyback;
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

#define CRT_ISEQ_ECHO_CHECKIN	/* input fields */		 \
	((int32_t)		(age)			CRT_VAR) \
	((int32_t)		(days)			CRT_VAR) \
	((d_rank_t)		(rank)			CRT_VAR) \
	((uint32_t)		(tag)			CRT_VAR) \
	((d_iov_t)		(raw_package)		CRT_VAR) \
	((d_string_t)		(name)			CRT_VAR)

#define CRT_OSEQ_ECHO_CHECKIN	/* output fields */		 \
	((d_rank_t)		(rank)			CRT_VAR) \
	((uint32_t)		(tag)			CRT_VAR) \
	((int32_t)		(ret)			CRT_VAR) \
	((uint32_t)		(room_no)		CRT_VAR)

CRT_RPC_DECLARE(crt_echo_checkin, CRT_ISEQ_ECHO_CHECKIN, CRT_OSEQ_ECHO_CHECKIN)
CRT_RPC_DEFINE(crt_echo_checkin, CRT_ISEQ_ECHO_CHECKIN, CRT_OSEQ_ECHO_CHECKIN)

#define CRT_ISEQ_ECHO_CORPC_EXAMPLE /* input fields */		 \
	((d_string_t)		(co_msg)		CRT_VAR)

#define CRT_OSEQ_ECHO_CORPC_EXAMPLE /* output fields */		 \
	((uint32_t)		(co_result)		CRT_VAR)

CRT_RPC_DECLARE(crt_echo_corpc_example,
		CRT_ISEQ_ECHO_CORPC_EXAMPLE, CRT_OSEQ_ECHO_CORPC_EXAMPLE)
CRT_RPC_DEFINE(crt_echo_corpc_example,
		CRT_ISEQ_ECHO_CORPC_EXAMPLE, CRT_OSEQ_ECHO_CORPC_EXAMPLE)

#define CRT_ISEQ_ECHO_NOOP	/* input fields */
#define CRT_OSEQ_ECHO_NOOP	/* output fields */

CRT_RPC_DECLARE(crt_echo_noop, CRT_ISEQ_ECHO_NOOP, CRT_OSEQ_ECHO_NOOP)
CRT_RPC_DEFINE(crt_echo_noop, CRT_ISEQ_ECHO_NOOP, CRT_OSEQ_ECHO_NOOP)

#define CRT_ISEQ_ECHO_BULK	/* input fields */		 \
	((d_string_t)		(bulk_intro_msg)	CRT_VAR) \
	((d_string_t)		(bulk_md5_ptr)		CRT_VAR) \
	((crt_bulk_t)		(remote_bulk_hdl)	CRT_VAR) \
	((int32_t)		(bulk_forward)		CRT_VAR) \
	((int32_t)		(bulk_bind)		CRT_VAR) \
	((int32_t)		(bulk_forward_rank)	CRT_VAR) \
	((int32_t)		(completed_cnt)		CRT_VAR)

#define CRT_OSEQ_ECHO_BULK	/* output fields */		 \
	((d_string_t)		(echo_msg)		CRT_VAR) \
	((int32_t)		(ret)			CRT_VAR)

CRT_RPC_DECLARE(crt_echo_bulk, CRT_ISEQ_ECHO_BULK, CRT_OSEQ_ECHO_BULK)
CRT_RPC_DEFINE(crt_echo_bulk, CRT_ISEQ_ECHO_BULK, CRT_OSEQ_ECHO_BULK)

static inline void
parse_options(int argc, char *argv[])
{
	int ch;
	int rc;

	for (;;) {
		ch = getopt(argc, argv, "mp:sg");
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
		case 'g':
			gecho.grp_destroy_piggyback = true;
			break;
		default:
			printf("Usage: %s [OPTIONS]\n", argv[0]);
			printf("OPTIONS:\n");
			printf("	-m		multi tier test\n");
			printf("	-p <dir>	path to attach file\n");
			printf("	-s		singleton attach\n");
			printf("	-g		piggyback grp "
			       "destroy\n");
			exit(-1);
		}
	}
}

static crt_group_t	*tier2_grp;
bool			 should_rm_tier1_attach_info;

static inline void
echo_init(int server, bool tier2)
{
	d_rank_t	my_rank;
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

	rc = crt_context_create(&gecho.crt_ctx);
	assert(rc == 0);

	if (server != 0 && tier2 == false && gecho.singleton_test) {
		printf("Saving singleton attach info\n");
		rc = crt_group_config_save(NULL, false);
		assert(rc == 0);
		rc = crt_group_rank(NULL, &my_rank);
		D_ASSERT(rc == 0);
		if (my_rank == 0)
			should_rm_tier1_attach_info = true;

		if (gecho.multi_tier_test) {
			/* Test saving attach info for another group */
			rc = crt_group_attach(ECHO_2ND_TIER_GRPID, &tier2_grp);
			assert(rc == 0 && tier2_grp != NULL);
			rc = crt_group_config_save(tier2_grp, false);
			assert(rc == 0);
		}
	}

	gecho.server = (server != 0);

	if (server && ECHO_EXTRA_CONTEXT_NUM > 0) {
		gecho.extra_ctx = calloc(ECHO_EXTRA_CONTEXT_NUM,
					 sizeof(crt_context_t));
		assert(gecho.extra_ctx != NULL);
		for (i = 0; i < ECHO_EXTRA_CONTEXT_NUM; i++) {
			rc = crt_context_create(&gecho.extra_ctx[i]);
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
		rc = CRT_RPC_REGISTER(ECHO_OPC_NOOP, 0, crt_echo_noop);
		assert(rc == 0);
		rc = CRT_RPC_REGISTER(ECHO_OPC_CHECKIN, 0, crt_echo_checkin);
		assert(rc == 0);
		rc = CRT_RPC_REGISTER(ECHO_OPC_BULK_TEST, 0, crt_echo_bulk);
		assert(rc == 0);
		rc = crt_rpc_register(ECHO_OPC_SHUTDOWN,
					    CRT_RPC_FEAT_NO_REPLY,
					    NULL);
		assert(rc == 0);
	} else {
		rc = CRT_RPC_SRV_REGISTER(ECHO_OPC_NOOP,
					  0,
					  crt_echo_noop,
					  echo_srv_noop);
		assert(rc == 0);
		rc = CRT_RPC_SRV_REGISTER(ECHO_OPC_CHECKIN,
					  0,
					  crt_echo_checkin,
					  echo_srv_checkin);
		assert(rc == 0);
		rc = CRT_RPC_SRV_REGISTER(ECHO_OPC_BULK_TEST,
					  0,
					  crt_echo_bulk,
					  echo_srv_bulk_test);
		assert(rc == 0);
		rc = crt_rpc_srv_register(ECHO_OPC_SHUTDOWN,
					  CRT_RPC_FEAT_NO_REPLY, NULL,
					  echo_srv_shutdown);
		assert(rc == 0);
		rc = CRT_RPC_CORPC_REGISTER(ECHO_CORPC_EXAMPLE,
					    crt_echo_corpc_example,
					    echo_srv_corpc_example,
					    &echo_co_ops);
		assert(rc == 0);
	}
}

static inline void
echo_fini(void)
{
	int rc = 0, i;
	d_rank_t my_rank;

	if (tier2_grp != NULL) {
		rc = crt_group_rank(NULL, &my_rank);
		D_ASSERT(rc == 0);
		if (my_rank == 0) {
			rc = crt_group_config_remove(tier2_grp);
			assert(rc == 0);
		}
		rc = crt_group_detach(tier2_grp);
		assert(rc == 0);
	}

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

	if (should_rm_tier1_attach_info) {
		rc = crt_group_config_remove(NULL);
		assert(rc == 0);
	}

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
	struct crt_echo_checkin_in *e_req;
	struct crt_echo_checkin_out *e_reply;
	struct crt_echo_corpc_example_out *corpc_reply;

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

		D_ASSERTF(e_req->rank == e_reply->rank,
			"rank mismatch, sent to rank %d reply from rank %d\n",
			e_req->rank, e_reply->rank);
		D_ASSERTF(e_req->tag == e_reply->tag,
			"tag mismatch, sent to tag %d reply from tag %d\n",
			e_req->tag, e_reply->tag);

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
