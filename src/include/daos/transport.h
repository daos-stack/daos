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
 * DAOS common header for RPC transport.
 */

#ifndef __DRPC_TRANSPORT_H__
#define __DRPC_TRANSPORT_H__

#include "crt_api.h"
#include "crt_types.h"
#include "crt_errno.h"
#include "crt_iv.h"

#include "crt_util/clog.h"
#include "crt_util/common.h"
#include "crt_util/hash.h"
#include "crt_util/list.h"

extern struct crt_msg_field CMF_SGL;
extern struct crt_msg_field CMF_SGL_ARRAY;
extern struct crt_msg_field CMF_SGL_DESC;
extern struct crt_msg_field CMF_SGL_DESC_ARRAY;

#endif /* __DRPC_TRANSPORT_H__ */
