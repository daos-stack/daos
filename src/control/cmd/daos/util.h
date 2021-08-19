/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __CMD_DAOS_UTIL_H__
#define __CMD_DAOS_UTIL_H__

#define D_LOGFAC	DD_FAC(client)

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <daos.h>
#include <daos/common.h>
#include <daos/debug.h>
#include <gurt/common.h>

#include "daos_types.h"
#include "daos_api.h"
#include "daos_fs.h"
#include "daos_uns.h"
#include "dfuse_ioctl.h"

#include "daos_hdlr.h"

int resolve_duns_path(struct cmd_args_s *ap);

/* cgo is unable to work directly with preprocessor macros
 * so we have to provide these glue helpers.
 */
static inline uint64_t
daos_prop_co_status_val(uint32_t status, uint32_t flag, uint32_t ver)
{
	return DAOS_PROP_CO_STATUS_VAL(status, flag, ver);
}

/* cgo is unable to work directly with unions, so we have
 *  to provide these glue helpers.
 */
static inline char *
get_dpe_str(struct daos_prop_entry *dpe)
{
	if (dpe == NULL)
		return NULL;

	return dpe->dpe_str;
}

static inline uint64_t
get_dpe_val(struct daos_prop_entry *dpe)
{
	if (dpe == NULL)
		return 0;

	return dpe->dpe_val;
}

static inline void *
get_dpe_val_ptr(struct daos_prop_entry *dpe)
{
	if (dpe == NULL)
		return NULL;

	return dpe->dpe_val_ptr;
}

static inline void
set_dpe_str(struct daos_prop_entry *dpe, d_string_t str)
{
	if (dpe == NULL)
		return;

	dpe->dpe_str = str;
}

static inline void
set_dpe_val(struct daos_prop_entry *dpe, uint64_t val)
{
	if (dpe == NULL)
		return;

	dpe->dpe_val = val;
}

static inline void
set_dpe_val_ptr(struct daos_prop_entry *dpe, void *val_ptr)
{
	if (dpe == NULL)
		return;

	dpe->dpe_val_ptr = val_ptr;
}

#endif /* __CMD_DAOS_UTIL_H__ */
