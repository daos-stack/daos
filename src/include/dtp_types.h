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
/**
 * This file is part of daos_transport
 *
 * include/dtp_types.h
 *
 * Author: Xuezhao Liu <xuezhao.liu@intel.com>
 */
#ifndef __DTP_TYPES_H__
#define __DTP_TYPES_H__

#include <daos_common.h>
#include <daos_types.h>
#include <daos_errno.h>

/* dtp context handle */
typedef void * dtp_context_t;

/* Physical address string, e.g., "bmi+tcp://localhost:3344". */
typedef char * dtp_phy_addr_t;

typedef uuid_t dtp_group_id_t;

/* all ranks in the group */
#define DTP_RANK_ALL      ((daos_rank_t)-1)

/* transport endpoint identifier */
typedef struct {
	dtp_group_id_t    dep_grp_id;
	daos_rank_t       dep_rank;
	uint32_t          dep_pad; /* pad just to align to 8 bytes */
} dtp_endpoint_t;

typedef uint32_t dtp_opcode_t;
typedef uint32_t dtp_version_t;

/* MAX wait time set to one hour */
#define DTP_PROGRESS_MAXWAIT         (3600 * 1000)
/* return immediately if no operation to progress */
#define DTP_PROGRESS_NOWAIT          (0)

typedef void * dtp_rpc_input_t;
typedef void * dtp_rpc_output_t;

/* Public RPC request/reply, exports to user */
typedef struct {
	dtp_context_t     dr_ctx; /* DTP context of the RPC */
	dtp_rpc_input_t   dr_input; /* input parameter struct */
	dtp_rpc_output_t  dr_output; /* output parameter struct */
	/* ... */
} dtp_rpc_t;

typedef void * dtp_bulk_t; /* abstract bulk handle */

typedef enum {
	DTP_BULK_PUT = 0x68,
	DTP_BULK_GET,
} dtp_bulk_op_t;

typedef void * dtp_bulk_opid_t;

typedef enum {
	/* read/write */
	DTP_BULK_RW = 0x88,
	/* read-only */
	DTP_BULK_RO,
	/* write-only */
	DTP_BULK_WO,
} dtp_bulk_perm_t;

/* bulk transferring descriptor */
typedef struct {
	dtp_endpoint_t    dbd_remote_ep; /* remote endpoint */
	dtp_bulk_op_t     dbd_bulk_op; /* DTP_BULK_PUT or DTP_BULK_GET */
	dtp_bulk_t        dbd_remote_hdl; /* remote bulk handle */
	daos_off_t        dbd_remote_off; /* remote offset */
	dtp_bulk_t        dbd_local_hdl; /* local bulk handle */
	daos_off_t        dbd_local_off; /* local offset */
	daos_size_t       dbd_len; /* length of the bulk transferring */
} dtp_bulk_desc_t;

typedef struct dtp_cb_info {
	void              *dci_arg; /* User passed in arg */
	dtp_rpc_t         *dci_rpc; /* rpc struct */
	int               dci_rc; /* return code */
} dtp_cb_info_t;

typedef void * dtp_bulk_cb_info_t;

/* server-side RPC handler */
typedef int (*dtp_rpc_cb_t)(dtp_rpc_t *rpc);

/* completion callback for dtp_req_send/dtp_reply_send */
typedef int (*dtp_cb_t)(const dtp_cb_info_t *cb_info);

/* completion callback for bulk transferring, i.e. dtp_bulk_transfer() */
typedef int (*dtp_bulk_cb_t)(const dtp_bulk_cb_info_t *cb_info);

/* Abstraction pack/unpack processor */
typedef void * dtp_proc_t;
/* Proc callback for pack/unpack parameters */
typedef int (*dtp_proc_cb_t)(dtp_proc_t proc, void *data);

/**
 * Progress condition callback.
 * Returning non-zero means stop the progressing and exit.
 */
typedef int (*dtp_progress_cond_cb_t)(void *arg);

#endif /* __DTP_TYPES_H__ */
