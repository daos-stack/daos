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
 * DAOS Error numbers
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 *
 * Version 0.1
 */

#ifndef __DAOS_ERRNO_H__
#define __DAOS_ERRNO_H__

typedef enum {
	DER_ERR_BASE		= 2000,
	/** no permission */
	DER_NO_PERM		= (DER_ERR_BASE + 1),
	/** invalid handle */
	DER_NO_HDL		= (DER_ERR_BASE + 2),
	/** invalid parameters */
	DER_INVAL		= (DER_ERR_BASE + 3),
	/** entity already exists */
	DER_EXIST		= (DER_ERR_BASE + 4),
	/** nonexistent entity */
	DER_NONEXIST		= (DER_ERR_BASE + 5),
	/** unreachable node */
	DER_UNREACH		= (DER_ERR_BASE + 6),
	/** no space on storage target */
	DER_NOSPACE		= (DER_ERR_BASE + 7),
	/** unknown object type */
	DER_NOTYPE		= (DER_ERR_BASE + 8),
	/** unknown object schema */
	DER_NOSCHEMA		= (DER_ERR_BASE + 9),
	/** object is not local */
	DER_NOLOCAL		= (DER_ERR_BASE + 10),
	/** already did sth */
	DER_ALREADY		= (DER_ERR_BASE + 11),
	/** NO memory */
	DER_NOMEM		= (DER_ERR_BASE + 12),
	/** Function not implemented */
	DER_NOSYS		= (DER_ERR_BASE + 13),
	/** timed out */
	DER_TIMEDOUT		= (DER_ERR_BASE + 14),
	/** Memory free error */
	DER_FREE_MEM		= (DER_ERR_BASE + 15),
	/** Entry not found */
	DER_ENOENT		= (DER_ERR_BASE + 16),
	/** Busy */
	DER_BUSY		= (DER_ERR_BASE + 17),
	/** Try again */
	DER_AGAIN		= (DER_ERR_BASE + 18),
	/** incompatible protocol */
	DER_PROTO		= (DER_ERR_BASE + 19),
	/** un-initialized */
	DER_UNINIT		= (DER_ERR_BASE + 20),
	/** epoch is read-only */
	DER_EP_RO		= (DER_ERR_BASE + 200),
	/** epoch is too old, all data have been recycled */
	DER_EP_OLD		= (DER_ERR_BASE + 201),
	/** key is too large */
	DER_KEY2BIG		= (DER_ERR_BASE + 250),
	/** record is too large */
	DER_REC2BIG		= (DER_ERR_BASE + 251),
	/** IO buffers can't match object extents */
	DER_IO_INVAL		= (DER_ERR_BASE + 300),
	/** event queue is busy */
	DER_EQ_BUSY		= (DER_ERR_BASE + 400),
	/** domain of cluster component can't match */
	DER_DOMAIN		= (DER_ERR_BASE + 500),
	/** transport layer mercury error */
	DER_DTP_HG		= (DER_ERR_BASE + 600),
	/** DTP RPC (opcode) unregister */
	DER_DTP_UNREG		= (DER_ERR_BASE + 601),
	/** DTP failed to generate an address string */
	DER_DTP_ADDRSTR_GEN	= (DER_ERR_BASE + 602),
	/** DTP MCL layer error */
	DER_DTP_MCL		= (DER_ERR_BASE + 603),
	/** TODO: add more error numbers */
} daos_errno_t;

const char *daos_errstr(daos_errno_t errno);

#endif /*  __DAOS_ERRNO_H__ */
