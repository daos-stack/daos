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
 * Server-side management API offering the following functionalities:
 * - manage storage allocation (PMEM files, disk partitions, ...)
 * - initialize pool and target service
 * - provide fault domains
 */

#ifndef __DMG_SRV_H__
#define __DMG_SRV_H__

/**
 * Common file names used by each layer to store persistent data
 */
#define	VOS_FILE	"vos-" /* suffixed by thread id */
#define	DSM_META_FILE	"meta"

/**
 * Generate path to a target file for pool \a pool_uuid with a filename set to
 * \a fname and suffixed by \a idx. \a idx can be NULL.
 */
int
dmgs_tgt_file(const uuid_t pool_uuid, const char *fname, int *idx,
	      char **fpath);
#endif /* __DMG_SRV_H__ */
