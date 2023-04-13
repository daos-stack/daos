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

/* [todo-ryon]: clean this up */
///* Is used while parsing user input for building the vos tree path. The builder can use branch
// * indexes that will be converted to the appropriate vos part (i.e. cont uuid, object id, etc).
// */
//struct dv_tree_path_builder {
//	daos_handle_t		 vtp_poh; /* pool handle */
//	struct dv_tree_path	 vtp_path;
//
//	/*
//	 * If a key value is passed instead of an index, then need a buffer to copy the key value
//	 * into.
//	 */
//	uint8_t			*vtp_dkey_buf;
//	uint8_t			*vtp_akey_buf;
//
//	/* Used during the verification process */
//	uint32_t		 vtp_current_idx;
//	/*
//	 * A user can pass an index of the path part. These indexes will be used to complete
//	 * the path parts.
//	 */
//	uint32_t		 vtp_cont_idx;
//	bool			 vtp_cont_verified;
//	uint32_t		 vtp_oid_idx;
//	bool			 vtp_oid_verified;
//	uint32_t		 vtp_dkey_idx;
//	bool			 vtp_dkey_verified;
//	uint32_t		 vtp_akey_idx;
//	bool			 vtp_akey_verified;
//	uint32_t		 vtp_recx_idx;
//	bool			 vtp_recx_verified;
//};



//void
//vtp_print(struct ddb_ctx *ctx, struct dv_tree_path *vt_path, bool include_new_line);
//
//void
//vtpb_print(struct ddb_ctx *ctx, struct dv_tree_path_builder *path, bool include_new_line);

struct argv_parsed {
	char		**ap_argv;
	void		 *ap_ctx;
	uint32_t	  ap_argc;
};

#endif /** __DAOS_DDB_COMMON_H */
