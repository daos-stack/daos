/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
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
