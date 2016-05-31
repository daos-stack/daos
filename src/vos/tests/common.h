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
 * Test for KV object creation and destroy.
 * vos/tests/vos_ctl.c
 */

#ifndef __VOS_TEST_H__
#define __VOS_TEST_H__

struct vos_test_ctx {
	char		*tc_po_name;
	uuid_t		 tc_po_uuid;
	uuid_t		 tc_co_uuid;
	daos_handle_t	 tc_po_hdl;
	daos_handle_t	 tc_co_hdl;
	int		 tc_step;
};

extern int  vts_ctx_init(struct vos_test_ctx *tcx);
extern void vts_ctx_fini(struct vos_test_ctx *tcx);
extern bool vts_file_exists(const char *fname);

#endif
