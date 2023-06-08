/**
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_DDB_COMMON_H
#define __DAOS_DDB_COMMON_H

#include <daos/common.h>
#include <daos/object.h>
#include <daos_obj.h>
#include <daos_types.h>
#include "ddb.h"
#define COMMAND_NAME_MAX 64

#define SUCCESS(rc) ((rc) == DER_SUCCESS)

#define ddb_print(ctx, str) \
	do { if ((ctx)->dc_io_ft.ddb_print_message) \
		(ctx)->dc_io_ft.ddb_print_message(str); \
	else \
		printf(str); } while (0)

#define ddb_printf(ctx, fmt, ...) \
	do { if ((ctx)->dc_io_ft.ddb_print_message) \
		(ctx)->dc_io_ft.ddb_print_message(fmt, __VA_ARGS__); \
	else                            \
		printf(fmt, __VA_ARGS__); \
	} while (0)

#define ddb_error(ctx, str) \
	do { if (ctx->dc_io_ft.ddb_print_error) \
		ctx->dc_io_ft.ddb_print_error(str); \
	else \
		printf(str); } while (0)

#define ddb_errorf(ctx, fmt, ...) \
	do { if ((ctx)->dc_io_ft.ddb_print_error) \
		(ctx)->dc_io_ft.ddb_print_error(fmt, __VA_ARGS__); \
	else                            \
		printf(fmt, __VA_ARGS__); \
	} while (0)

struct argv_parsed {
	char		**ap_argv;
	void		 *ap_ctx;
	uint32_t	  ap_argc;
};

#endif /* __DAOS_DDB_COMMON_H */
