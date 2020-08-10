/*
 * (C) Copyright 2017-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
 * Common code for threaded_client/threaded_server testing multiple threads
 * using a single context
 */
#ifndef __THREADED_RPC_H__
#define __THREADED_RPC_H__

#include <cart/api.h>
#include "common.h"

#define CRT_ISEQ_RPC		/* input fields */		 \
	((int32_t)		(msg)			CRT_VAR) \
	((int32_t)		(payload)		CRT_VAR)

#define CRT_OSEQ_RPC		/* output fields */		 \
	((int32_t)		(msg)			CRT_VAR) \
	((int32_t)		(value)			CRT_VAR)

CRT_RPC_DECLARE(threaded_rpc, CRT_ISEQ_RPC, CRT_OSEQ_RPC)
CRT_RPC_DEFINE(threaded_rpc, CRT_ISEQ_RPC, CRT_OSEQ_RPC)

#define FOREACH_MSG_TYPE(ACTION)    \
	ACTION(MSG_START,  0xf00d)  \
	ACTION(MSG_TYPE1,  0xdead)  \
	ACTION(MSG_TYPE2,  0xfeed)  \
	ACTION(MSG_TYPE3,  0xdeaf)  \
	ACTION(MSG_STOP,   0xbaad)

#define GEN_ENUM(name, value) \
	name,

#define GEN_ENUM_VALUE(name, value) \
	value,

enum {
	FOREACH_MSG_TYPE(GEN_ENUM)
	MSG_COUNT,
};

static const int msg_values[MSG_COUNT] = {
	FOREACH_MSG_TYPE(GEN_ENUM_VALUE)
};

#define GEN_STR(name, value) \
	#name,

static const char *msg_strings[MSG_COUNT] = {
	FOREACH_MSG_TYPE(GEN_STR)
};

#define MSG_IN_VALUE 0xbeef
#define MSG_OUT_VALUE 0xbead

#define TEST_THREADED_BASE 0x010000000
#define TEST_THREADED_VER 0

#define RPC_ID CRT_PROTO_OPC(TEST_THREADED_BASE,	\
				TEST_THREADED_VER, 0)

#endif /* __THREADED_RPC_H__ */
