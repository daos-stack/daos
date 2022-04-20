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

#define COMMAND_NAME_MAX 64

#define SUCCESS(rc) ((rc) == DER_SUCCESS)

#define ddb_print(ctx, str) \
	do { if (ctx->dc_io_ft.ddb_print_message) \
		ctx->dc_io_ft.ddb_print_message(str); \
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


struct ddb_io_ft {
	/**
	 * Print a message.
	 *
	 * @param fmt	Typically printf string format
	 * @param ...	Additional args will be formatted into the printed string
	 * @return	Total number of characters written
	 */
	int (*ddb_print_message)(const char *fmt, ...);

	/**
	 * Print an error message.
	 *
	 * @param fmt	Typically printf string format
	 * @param ...	Additional args will be formatted into the printed string
	 * @return	Total number of characters written
	 */
	int (*ddb_print_error)(const char *fmt, ...);

	/**
	 * Read a line from stdin and stores into buf.
	 *
	 * @param buf		Pointer to an array where the string read is stored
	 * @param buf_len	Length of buf
	 * @return		On success the same buf parameter, else NULL
	 */
	char *(*ddb_get_input)(char *buf, uint32_t buf_len);

	/**
	 * Check if a file exists
	 *
	 * @param path	Path to file to check
	 * @return	true if the file exists, else false
	 */
	bool (*ddb_get_file_exists)(const char *path);

	/**
	 * Write the contents of the iov to a file
	 *
	 * @param dst_path	File to write to
	 * @param contents	Contents to be written
	 * @return		0 on success, else an error code
	 */
	int (*ddb_write_file)(const char *dst_path, d_iov_t *contents);

	/**
	 * Determine the size of a file at path
	 * @param path	Path of file to check
	 * @return	the size of the file at path in bytes
	 */
	size_t (*ddb_get_file_size)(const char *path);

	/**
	 * Read the contents of a file and store into the iov
	 * @param src_path	Path of the file to read
	 * @param contents	Where to load the contents of the file into
	 * @return		number of bytes read from the src_path
	 */
	size_t (*ddb_read_file)(const char *src_path, d_iov_t *contents);
};

struct ddb_ctx {
	struct ddb_io_ft	 dc_io_ft;
	daos_handle_t		 dc_poh;
	daos_handle_t		 dc_coh;
	bool			 dc_should_quit;
};

struct dv_tree_path {
	uuid_t		vtp_cont;
	daos_unit_oid_t vtp_oid;
	daos_key_t	vtp_dkey;
	daos_key_t	vtp_akey;
	daos_recx_t	vtp_recx;
};

/* Is used while parsing user input for building the vos tree path. The builder can use branch
 * indexes that will be converted to the appropriate vos part (i.e. cont uuid, object id, etc).
 */
struct dv_tree_path_builder {
	daos_handle_t		 vtp_poh; /* pool handle */
	struct dv_tree_path	 vtp_path;

	/*
	 * If a key value is passed instead of an index, then need a buffer to copy the key value
	 * into.
	 */
	uint8_t			*vtp_dkey_buf;
	uint8_t			*vtp_akey_buf;

	/*
	 * A user can pass an index of the path part. These indexes will be used to complete
	 * the path parts.
	 */
	uint32_t		 vtp_cont_idx;
	uint32_t		 vtp_oid_idx;
	uint32_t		 vtp_dkey_idx;
	uint32_t		 vtp_akey_idx;
	uint32_t		 vtp_recx_idx;
};

static inline bool
dv_has_cont(struct dv_tree_path *vtp)
{
	return !uuid_is_null(vtp->vtp_cont);
}

static inline bool
dv_has_obj(struct dv_tree_path *vtp)
{
	return !(vtp->vtp_oid.id_pub.lo == 0 &&
		 vtp->vtp_oid.id_pub.hi == 0);
}

static inline bool
dv_has_dkey(struct dv_tree_path *vtp)
{
	return vtp->vtp_dkey.iov_len > 0;
}

static inline bool
dv_has_akey(struct dv_tree_path *vtp)
{
	return vtp->vtp_akey.iov_len > 0;
}

static inline bool
dvp_is_complete(struct dv_tree_path *vtp)
{
	return dv_has_cont(vtp) && dv_has_obj(vtp) && dv_has_dkey(vtp) && dv_has_akey(vtp);
}

static inline bool
dvp_is_empty(struct dv_tree_path *vtp)
{
	return !dv_has_cont(vtp) && !dv_has_obj(vtp) && !dv_has_dkey(vtp) && !dv_has_akey(vtp);
}

static inline void
vtp_print(struct ddb_ctx *ctx, struct dv_tree_path *vt_path)
{
	if (dv_has_cont(vt_path))
		ddb_printf(ctx, "/"DF_UUIDF"", DP_UUID(vt_path->vtp_cont));
	if (dv_has_obj(vt_path))
		ddb_printf(ctx, "/"DF_UOID"",  DP_UOID(vt_path->vtp_oid));
	if (dv_has_dkey(vt_path))
		ddb_printf(ctx, "/%s", (char *)vt_path->vtp_dkey.iov_buf);
	if (dv_has_akey(vt_path))
		ddb_printf(ctx, "/%s", (char *)vt_path->vtp_akey.iov_buf);

	if (vt_path->vtp_recx.rx_nr > 0)
		ddb_printf(ctx, "/{%lu-%lu}", vt_path->vtp_recx.rx_idx,
			   vt_path->vtp_recx.rx_idx + vt_path->vtp_recx.rx_nr - 1);
	ddb_print(ctx, "/\n");
}

struct argv_parsed {
	char		**ap_argv;
	void		 *ap_ctx;
	uint32_t	  ap_argc;
};

#endif /** __DAOS_DDB_COMMON_H */
