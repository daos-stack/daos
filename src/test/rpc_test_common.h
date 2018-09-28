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
/**
 * This file is part of the CaRT rpc test  example which is based on CaRT APIs.
 */

#ifndef __RPC_TEST_COMMON_H__
#define __RPC_TEST_COMMON_H__

#include <semaphore.h>
#include <cart/api.h>
#include <cart/types.h>
#include <gurt/common.h>

enum {
/* T1.0:- Without reply from server */
CRT_RPC_TEST_IO =		(0x54312e30),

/* T1.1:-Test without any operation */
CRT_RPC_TEST_NO_IO =		(0x54312e31),

/* T1.2:-Test IO */
CRT_RPC_TEST_ERR =		(0x54312e32),

/* T1.3:-Test TIMEOUT */
CRT_RPC_TEST_TIMEOUT =		(0x54312e33),

/* T0:-shutdown server without sending reply */
CRT_RPC_TEST_SHUTDOWN =		(0x5430),

/* T5.0:-Test without any IO operation */
CRT_RPC_TEST_GRP_IO =		(0x54312e50),

/* T7.0:-Test multitier IO */
CRT_RPC_MULTITIER_TEST_IO =	(0x54312e70),

/* T7.1:-Test multitier without any operation */
CRT_RPC_MULTITIER_TEST_NO_IO =	(0x54312e71)

};

#define DEBUG			1
#define FILE_PATH_SIZE		256
#define CRT_RPC_MULTITIER_GRPID	"rpc_test_multitier0"

#define CRT_ISEQ_TEST_IO	/* input fields */		 \
	((d_string_t)		(msg)			CRT_VAR) \
	((d_iov_t)		(raw_pkg)		CRT_VAR) \
	((int32_t)		(to_srv)		CRT_VAR) \
	((crt_status_t)		(from_srv)		CRT_VAR)

#define CRT_OSEQ_TEST_IO	/* output fields */		 \
	((d_string_t)		(msg)			CRT_VAR) \
	((d_iov_t)		(raw_pkg)		CRT_VAR) \
	((int32_t)		(to_srv)		CRT_VAR) \
	((crt_status_t)		(from_srv)		CRT_VAR)

#define CRT_OSEQ_TEST_TIMEOUT	/* output fields */

CRT_RPC_DECLARE(crt_rpc_io, CRT_ISEQ_TEST_IO, CRT_OSEQ_TEST_IO)
CRT_RPC_DEFINE(crt_rpc_io, CRT_ISEQ_TEST_IO, CRT_OSEQ_TEST_IO)

CRT_RPC_DECLARE(crt_test_err, CRT_ISEQ_TEST_IO, CRT_OSEQ_TEST_IO)
CRT_RPC_DEFINE(crt_test_err, CRT_ISEQ_TEST_IO, CRT_OSEQ_TEST_IO)

CRT_RPC_DECLARE(crt_test_timeout, CRT_ISEQ_TEST_IO, CRT_OSEQ_TEST_TIMEOUT)
CRT_RPC_DEFINE(crt_test_timeout, CRT_ISEQ_TEST_IO, CRT_OSEQ_TEST_TIMEOUT)

CRT_RPC_DECLARE(crt_multitier_test_io, CRT_ISEQ_TEST_IO, CRT_OSEQ_TEST_IO)
CRT_RPC_DEFINE(crt_multitier_test_io, CRT_ISEQ_TEST_IO, CRT_OSEQ_TEST_IO)

#define CRT_ISEQ_NULL		/* input fields */

#define CRT_OSEQ_NULL		/* output fields */

#define CRT_ISEQ_GRP_IO		/* input fields */		 \
	((d_string_t)		(msg)			CRT_VAR)

#define CRT_OSEQ_GRP_IO		/* output fields */		 \
	((crt_status_t)		(from_srv)		CRT_VAR)

CRT_RPC_DECLARE(crt_test_no_io, CRT_ISEQ_NULL, CRT_OSEQ_NULL)
CRT_RPC_DEFINE(crt_test_no_io, CRT_ISEQ_NULL, CRT_OSEQ_NULL)

CRT_RPC_DECLARE(crt_test_shutdown, CRT_ISEQ_NULL, CRT_OSEQ_NULL)
CRT_RPC_DEFINE(crt_test_shutdown, CRT_ISEQ_NULL, CRT_OSEQ_NULL)

CRT_RPC_DECLARE(crt_rpc_grp_io, CRT_ISEQ_GRP_IO, CRT_OSEQ_GRP_IO)
CRT_RPC_DEFINE(crt_rpc_grp_io, CRT_ISEQ_GRP_IO, CRT_OSEQ_GRP_IO)

CRT_RPC_DECLARE(crt_multitier_test_no_io, CRT_ISEQ_NULL, CRT_OSEQ_NULL)
CRT_RPC_DEFINE(crt_multitier_test_no_io, CRT_ISEQ_NULL, CRT_OSEQ_NULL)

struct rpc_test_cli {
	char			config_path[FILE_PATH_SIZE];
	char			test_file_path[FILE_PATH_SIZE];
	char			*local_group_name;
	char			*target_group_name;
	crt_group_t		*local_group;
	/*server group to attach to*/
	crt_group_t		*target_group[2];
	crt_context_t		crt_ctx;
	d_rank_list_t		*psr_cand_list;
	pthread_t		progress_thid;
	sem_t			cli_sem;
	uint32_t		timeout;
	uint32_t		shutdown;
	uint32_t		grp_size[2];
	uint32_t		target_grp_size;
};

struct rpc_test_srv {
	char			config_path[FILE_PATH_SIZE];
	char			*local_group_name;
	char			*target_group_name;
	crt_group_t		*cur_grp;
	crt_group_t		*local_group;
	crt_group_t		*target_group;
	crt_group_t		*target_multitier_grp;
	crt_context_t		crt_ctx;
	pthread_t		progress_thid;
	sem_t			srv_sem;
	uint32_t		my_rank;
	uint32_t		shutdown;
	uint32_t		grp_size;
	uint32_t		rpc_test_holdtime;
	uint32_t		target_group_size;
};

void crt_lm_fake_event_notify_fn(d_rank_t pmix_rank, bool *dead);

#define DBG(fmt, ...)				\
	printf("%s[%d]\t[%d]"fmt"\n",	\
	(strrchr(__FILE__, '/')+1), __LINE__, getpid(), ##__VA_ARGS__)

#if DEBUG == 1
#define dbg(fmt, ...)   D_DEBUG(DB_TEST, fmt, ##__VA_ARGS__)
#else
#define dbg(fmt, ...)	 DBG(fmt, ##__VA_ARGS__)
#endif

#endif /*__RPC_TEST_COMMON_H__ */
