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
 * (C) Copyright 2016 Intel Corporation.
 */
/*
 * dmgs: Internal Declarations
 *
 * This file contains all declarations that are only used by dmgss.
 * All external variables and functions must have a "dmgs_" prefix.
 */

#ifndef __DMGS_INTERNAL_H__
#define __DMGS_INTERNAL_H__

#include <daos/list.h>
#include <daos/transport.h>
#include <daos/rpc.h>
#include <daos/common.h>
#include <daos_srv/daos_server.h>
#include "dmg_rpc.h"

/** dmgs_pool.c */
int dmgs_hdlr_pool_create(dtp_rpc_t *rpc_req);
int dmgs_hdlr_pool_destroy(dtp_rpc_t *rpc_req);

/** dmgs_target.c */
int dmgs_tgt_init(void);
int dmgs_hdlr_tgt_create(dtp_rpc_t *rpc_req);
int dmgs_hdlr_tgt_destroy(dtp_rpc_t *rpc_req);
#endif /* __DMGS_INTERNAL_H__ */
