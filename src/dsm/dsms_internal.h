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
 * dsms: Internal Declarations
 *
 * This file contains all declarations that are only used by dsms but do not
 * belong to the more specific headers like dsms_storage.h. All external
 * variables and functions must have a "dsms_" prefix, however, even if they
 * are only used by dsms.
 */

#ifndef __DSMS_INTERNAL_H__
#define __DSMS_INTERNAL_H__

#include <daos/daos_transport.h>

/*
 * dsms_storage.c
 */
int dsms_storage_init(void);
void dsms_storage_fini(void);
int dsms_kvs_nv_update(daos_handle_t kvsh, const char *name, const void *value,
		       size_t size);

/*
 * dsms_pool.c
 */
int dsms_hdlr_pool_connect(dtp_rpc_t *rpc);
int dsms_hdlr_pool_disconnect(dtp_rpc_t *rpc);

#endif /* __DSMS_INTERNAL_H__ */
