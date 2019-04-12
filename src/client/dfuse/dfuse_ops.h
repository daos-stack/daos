/**
 * (C) Copyright 2016-2019 Intel Corporation.
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

#ifndef __DFUSE_OPS_H__
#define __DFUSE_OPS_H__

#define STR_H(s) #s
#define TO_STR(s) STR_H(s)
#define TRACE_TYPE TO_STR(TYPE_NAME)

#define DFUSE_REQ_INIT(src, FSH, api, in, rc)				\
	do {								\
		rc = 0;							\
		/* Acquire new object only if NULL */			\
		if (!src) {						\
			src = dfuse_da_acquire(FSH->POOL_NAME);	\
			DFUSE_TRA_UP(src, FSH, TRACE_TYPE);		\
		}							\
		if (!src) {						\
			rc = ENOMEM;					\
			break;						\
		}							\
		(src)->REQ_NAME.ir_api = &api;				\
	} while (0)

/* Initialise a descriptor and make the dfuse_request a child of it */
#define DFUSE_REQ_INIT_REQ(src, fsh, api, fuse_req, rc)			\
	do {								\
		rc = 0;							\
		/* Acquire new object only if NULL */			\
		if (!(src)) {						\
			src = dfuse_da_acquire((fsh)->POOL_NAME);	\
			if (!(src)) {					\
				rc = ENOMEM;				\
				break;					\
			}						\
			DFUSE_TRA_UP(src, fsh, TRACE_TYPE);		\
		}							\
		(src)->REQ_NAME.ir_api = &(api);			\
		(src)->REQ_NAME.req = fuse_req;				\
	} while (0)

#define CONTAINER(req) container_of(req, struct TYPE_NAME, REQ_NAME)

#endif /* __DFUSE_OPS_H__ */
