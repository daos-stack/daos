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
#include <openssl/md5.h>

#define ECHO_OPC_CHECKIN    (0xA1)
#define ECHO_OPC_BULK_TEST  (0xA2)
#define ECHO_OPC_SHUTDOWN   (0x100)

struct gecho {
	dtp_context_t	dtp_ctx;
	int		complete;
};

extern struct gecho gecho;

int echo_srv_checkin(dtp_rpc_t *rpc);
int echo_srv_bulk_test(dtp_rpc_t *rpc);
int echo_srv_shutdown(dtp_rpc_t *rpc);

DTP_GEN_PROC(echo_checkin_in_t,
	((bool)(age)) ((dtp_string_t)(name)) ((uint32_t)(days)))
DTP_GEN_PROC(echo_checkin_out_t,
	((int32_t)(ret)) ((uint32_t)(room_no)))

DTP_GEN_PROC(echo_bulk_test_in_t,
	((dtp_string_t)(bulk_intro_msg)) ((dtp_bulk_t)(bulk_hdl))
	((dtp_string_t)(bulk_md5_str)))
DTP_GEN_PROC(echo_bulk_test_out_t,
	((int32_t)(ret)) ((dtp_string_t)(bulk_echo_msg)))


static inline void
echo_init(int server)
{
	int rc = 0;

	rc = dtp_init(server);
	assert(rc == 0);

	rc = dtp_context_create(NULL, &gecho.dtp_ctx);

	/* Just show the case that the client does not know the rpc handler,
	 * then client side can use dtp_rpc_reg, and server side can use
	 * dtp_rpc_srv_reg.
	 * If both client and server side know the rpc handler, they can call
	 * the same dtp_rpc_srv_reg. */
	if (server == 0) {
		rc = dtp_rpc_reg(ECHO_OPC_CHECKIN, dtp_proc_echo_checkin_in_t,
				 dtp_proc_echo_checkin_out_t,
				 sizeof(echo_checkin_in_t),
				 sizeof(echo_checkin_out_t));
		assert(rc == 0);
		rc = dtp_rpc_reg(ECHO_OPC_BULK_TEST,
				 dtp_proc_echo_bulk_test_in_t,
				 dtp_proc_echo_bulk_test_out_t,
				 sizeof(echo_bulk_test_in_t),
				 sizeof(echo_bulk_test_out_t));
		assert(rc == 0);
		rc = dtp_rpc_reg(ECHO_OPC_SHUTDOWN, NULL, NULL, 0, 0);
		assert(rc == 0);
	} else {
		rc = dtp_rpc_srv_reg(ECHO_OPC_CHECKIN,
				     dtp_proc_echo_checkin_in_t,
				     dtp_proc_echo_checkin_out_t,
				     sizeof(echo_checkin_in_t),
				     sizeof(echo_checkin_out_t),
				     echo_srv_checkin);
		assert(rc == 0);
		rc = dtp_rpc_srv_reg(ECHO_OPC_BULK_TEST,
				     dtp_proc_echo_bulk_test_in_t,
				     dtp_proc_echo_bulk_test_out_t,
				     sizeof(echo_bulk_test_in_t),
				     sizeof(echo_bulk_test_out_t),
				     echo_srv_bulk_test);
		assert(rc == 0);
		rc = dtp_rpc_srv_reg(ECHO_OPC_SHUTDOWN, NULL, NULL, 0, 0,
				     echo_srv_shutdown);
		assert(rc == 0);
	}
}

static inline void
echo_fini(void)
{
	int rc = 0;

	rc = dtp_context_destroy(gecho.dtp_ctx, 0);
	assert(rc == 0);

	rc = dtp_finalize();
	assert(rc == 0);
}

/* convert to string just to facilitate the pack/unpack */
static inline void
echo_md5_to_string(unsigned char *md5, dtp_string_t md5_str)
{
	char tmp[3] = {'\0'};
	int i;

	assert(md5 != NULL && md5_str != NULL);

	for (i = 0; i < 16; i++) {
		sprintf(tmp, "%02x", md5[i]);
		strcat(md5_str, tmp);
	}
}

#endif /* __DTP_ECHO_H__ */
