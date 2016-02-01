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
 * dtp/dtp_mercury.h
 *
 * Author: Xuezhao Liu <xuezhao.liu@intel.com>
 */
#ifndef __DTP_MERCURY_H__
#define __DTP_MERCURY_H__

#include <mercury.h>
#include <mercury_types.h>
#include <mercury_macros.h>

#include <na.h>

/**
 * Basic approach for core affinity handling:
 * 1) DTP layer can create different affinity contexts (dtp_context_t, for
 *    example one affinity context for one NUMA node),
 * 2) DAOS transport associates different hg_context for different dtp_context_t
 * 3) RPC server (mercury server) internally dispatches different peers to
 *    different affinity context, maybe by hashing peers ID or by a user
 *    registered callback. Using NUMA allocator for memory allocation.
 * 4) Within DAOS, the calling context of dtp_progress() should consider the low
 *    layer's affinity context, and call the dpt_progress for different context.
 *
 * Notes: current HG data structures of hg_class/hg_context/na_class/na_context
 *        and internal handling need refactoring for core affinity.
 */
typedef struct dtp_context {
	na_class_t        *dc_nacla; /* NA class */
	na_context_t      *dc_nactx; /* NA context */
	hg_class_t        *dc_hgcla; /* HG class */
	hg_context_t      *dc_hgctx; /* HG context */
	/* .... */
} dtp_hg_context_t;


#endif /* __DTP_MERCURY_H__ */
