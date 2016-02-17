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
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2015 Intel Corporation.
 */
/**
 * This file is part of the dtp echo example which is based on dtp APIs.
 */

#ifndef __DTP_ECHO_H__
#define __DTP_ECHO_H__

#include <daos/daos_transport.h>

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>

#define ECHO_OPC_CHECKIN    (0xa1)
#define ECHO_OPC_SHUTDOWN   (0x99)

struct gecho {
	dtp_phy_addr_t	uri;
	dtp_context_t	dtp_ctx;
	int		complete;
};

extern struct gecho gecho;

int echo_srv_checkin(dtp_rpc_t *rpc);
int echo_srv_shutdown(dtp_rpc_t *rpc);

DTP_GEN_PROC(echo_checkin_in_t,
	((bool)(age)) ((dtp_string_t)(name)) ((uint32_t)(days)))
DTP_GEN_PROC(echo_checkin_out_t,
	((int32_t)(ret)) ((uint32_t)(room_no)))

static inline void echo_init(int server)
{
	int rc = 0;

	rc = dtp_init(gecho.uri, server);
	assert(rc == 0);

	rc = dtp_context_create(NULL, &gecho.dtp_ctx);

	if (server == 0) {
		rc = dtp_rpc_reg(ECHO_OPC_CHECKIN,
			(dtp_proc_cb_t)dtp_proc_echo_checkin_in_t,
			(dtp_proc_cb_t)dtp_proc_echo_checkin_out_t,
			sizeof(echo_checkin_in_t), sizeof(echo_checkin_out_t));
		assert(rc == 0);
		rc = dtp_rpc_reg(ECHO_OPC_SHUTDOWN, NULL, NULL, 0, 0);
		assert(rc == 0);
	} else {
		rc = dtp_rpc_srv_reg(ECHO_OPC_CHECKIN,
			(dtp_proc_cb_t)dtp_proc_echo_checkin_in_t,
			(dtp_proc_cb_t)dtp_proc_echo_checkin_out_t,
			sizeof(echo_checkin_in_t), sizeof(echo_checkin_out_t),
			echo_srv_checkin);
		assert(rc == 0);
		rc = dtp_rpc_srv_reg(ECHO_OPC_SHUTDOWN, NULL, NULL, 0, 0,
			echo_srv_shutdown);
		assert(rc == 0);
	}
}

static inline void echo_fini(void)
{
	int rc = 0;

	rc = dtp_context_destroy(gecho.dtp_ctx, 0);
	assert(rc == 0);

	rc = dtp_finalize();
	assert(rc == 0);
}

#endif /* __DTP_ECHO_H__ */
