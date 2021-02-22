/*
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

#define TEST_RPC_COMMON_BASE 0x010000000
#define TEST_RPC_COMMON_VER  0


enum {
/* T1.0:- Without reply from server */
CRT_RPC_TEST_IO =		CRT_PROTO_OPC(TEST_RPC_COMMON_BASE,
					TEST_RPC_COMMON_VER, 0),

/* T1.1:-Test without any operation */
CRT_RPC_TEST_NO_IO =		CRT_PROTO_OPC(TEST_RPC_COMMON_BASE,
					TEST_RPC_COMMON_VER, 1),

/* T1.2:-Test IO */
CRT_RPC_TEST_ERR =		CRT_PROTO_OPC(TEST_RPC_COMMON_BASE,
					TEST_RPC_COMMON_VER, 2),

/* T1.3:-Test TIMEOUT */
CRT_RPC_TEST_TIMEOUT =		CRT_PROTO_OPC(TEST_RPC_COMMON_BASE,
					TEST_RPC_COMMON_VER, 3),

/* T0:-shutdown server without sending reply */
CRT_RPC_TEST_SHUTDOWN =		CRT_PROTO_OPC(TEST_RPC_COMMON_BASE,
					TEST_RPC_COMMON_VER, 4),

/* T5.0:-Test without any IO operation */
CRT_RPC_TEST_GRP_IO =		CRT_PROTO_OPC(TEST_RPC_COMMON_BASE,
					TEST_RPC_COMMON_VER, 5),

/* T7.0:-Test multitier IO */
CRT_RPC_MULTITIER_TEST_IO =	CRT_PROTO_OPC(TEST_RPC_COMMON_BASE,
					TEST_RPC_COMMON_VER, 6),

/* T7.1:-Test multitier without any operation */
CRT_RPC_MULTITIER_TEST_NO_IO =	CRT_PROTO_OPC(TEST_RPC_COMMON_BASE,
					TEST_RPC_COMMON_VER, 7)

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

#define DBG(fmt, ...)				\
	printf("%s[%d]\t[%d]"fmt"\n",	\
	(strrchr(__FILE__, '/')+1), __LINE__, getpid(), ##__VA_ARGS__)

#if DEBUG == 1
#define dbg(fmt, ...)   D_DEBUG(DB_TEST, fmt, ##__VA_ARGS__)
#else
#define dbg(fmt, ...)	 DBG(fmt, ##__VA_ARGS__)
#endif

#endif /*__RPC_TEST_COMMON_H__ */
