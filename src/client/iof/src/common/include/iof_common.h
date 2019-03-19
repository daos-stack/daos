/* Copyright (C) 2016-2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef IOF_COMMON_H
#define IOF_COMMON_H

#include <sys/stat.h>
#include <cart/api.h>
#include <gurt/common.h>

#include "ios_gah.h"

#define IOF_DEFAULT_SET "IONSS"

/*
 * IOF features are represented by an 8-bit unsigned bit vector
 * \ref iof_fs_info.flags and are used turn various features on or off.
 * A combination of different features defines the projection mode
 *
 * Features that don't require separate implementations:
 * Bit [0]	: 0=Read-Only, 1=Read-Write
 * Bit [1]	: Failover [0=Off, 1=On]
 *
 * Features that may require separate implementations::
 * Bit [2]	: Striped Metadata [0=Off, 1=On]
 * Bit [3]	: Striped Data [0=Off, 1=On]
 *
 * Features of the projected storage type:
 * Bit [5,4]	: 00=Default, 01=Lustre, 010=DW-Scratch, 011=DW-Cache
 *
 */
#define IOF_FS_DEFAULT			0x00UL
#define IOF_FS_LUSTRE			0x10UL
#define IOF_DW_SCRATCH			0x20UL
#define IOF_DW_CACHE			0x30UL

#define IOF_WRITEABLE			0x01UL
#define IOF_FAILOVER			0x02UL
#define IOF_STRIPED_METADATA		0x04UL
#define IOF_STRIPED_DATA		0x08UL

#define IOF_IS_WRITEABLE(FLAGS) ((FLAGS) & IOF_WRITEABLE)
#define IOF_HAS_FAILOVER(FLAGS) ((FLAGS) & IOF_FAILOVER)

#define IOF_CNSS_MT			0x080UL
#define IOF_FUSE_READ_BUF		0x100UL
#define IOF_FUSE_WRITE_BUF		0x200UL

enum iof_projection_mode {
	/* Private Access Mode */
	IOF_DEFAULT_PRIVATE,
	/* Striped Metadata on PFS */
	IOF_PFS_STRIPED_METADATA,
	/* Striped Data on PFS */
	IOF_PFS_STRIPED_DATA,
	/* Striped Metadata on Lustre */
	IOF_LUSTRE_STRIPED_METADATA,

	/* Data Warp [Scratch], Private */
	IOF_DWS_PRIVATE,
	/* Data Warp [Cache], Private */
	IOF_DWC_PRIVATE,
	/* Data Warp [Scratch], Striped Data */
	IOF_DWS_STRIPED_DATA,
	/* Data Warp [Cache], Striped Data */
	IOF_DWC_STRIPED_DATA,

	/* Total number of Projection Modes */
	IOF_PROJECTION_MODES
};

/* The name of a filesystem entry
 *
 */
struct ios_name {
	char name[NAME_MAX + 1];
};

#define IOF_FS_INFO							\
	((struct ios_name)	(dir_name)		CRT_VAR)	\
	((struct ios_gah)	(gah)			CRT_VAR)	\
	((uint64_t)		(flags)			CRT_VAR)	\
	((uint32_t)		(timeout)		CRT_VAR)	\
	((uint32_t)		(max_read)		CRT_VAR)	\
	((uint32_t)		(max_write)		CRT_VAR)	\
	((uint32_t)		(readdir_size)		CRT_VAR)	\
	((uint32_t)		(max_iov_read)		CRT_VAR)	\
	((uint32_t)		(max_iov_write)		CRT_VAR)	\
	((uint32_t)		(htable_size)		CRT_VAR)	\
	((uint32_t)		(cnss_thread_count)	CRT_VAR)	\
	((int)			(id)			CRT_VAR)	\

CRT_GEN_STRUCT(iof_fs_info, IOF_FS_INFO)

#define IOF_SQ_OUT							\
	((uint32_t)		(poll_interval)		CRT_VAR)	\
	((bool)			(progress_callback)	CRT_VAR)	\
	((struct iof_fs_info)	(info)			CRT_ARRAY)

CRT_RPC_DECLARE(iof_query, ,IOF_SQ_OUT)

struct iof_gah_string_in {
	struct ios_gah gah;
	struct ios_name name;
};

struct iof_imigrate_in {
	struct ios_gah gah;
	struct ios_name name;
	int inode;
};

struct iof_string_out {
	d_string_t path;
	int rc;
	int err;
};

struct iof_entry_out {
	struct ios_gah gah;
	struct stat stat;
	int rc;
	int err;
};

struct iof_create_out {
	struct ios_gah gah;
	struct ios_gah igah;
	struct stat stat;
	int rc;
	int err;
};

struct iof_two_string_in {
	struct iof_gah_string_in common;
	d_string_t oldpath;
};

struct iof_create_in {
	struct iof_gah_string_in common;
	uint32_t mode;
	uint32_t flags;
};

/* We reuse iof_gah_string_in in a few input structs and we need to
 * ensure compiler isn't adding padding.   This should always be
 * the case now unless we change the struct.  This assert is here
 * to force the modifier to ensure the same condition is met.
 */
_Static_assert(sizeof(struct iof_gah_string_in) ==
	       (sizeof(struct ios_gah) + sizeof(struct ios_name)),
	       "iof_gah_string_in size unexpected");

_Static_assert(NAME_MAX == 255, "NAME_MAX wrong size");

struct iof_rename_in {
	struct ios_gah old_gah;
	struct ios_gah new_gah;
	struct ios_name old_name;
	struct ios_name new_name;
	uint32_t flags;
};

struct iof_open_in {
	struct ios_gah gah;
	uint32_t flags;
};

struct iof_unlink_in {
	struct ios_name name;
	struct ios_gah gah;
	uint32_t flags;
};

struct iof_attr_out {
	struct stat stat;
	int rc;
	int err;
};

struct iof_opendir_out {
	struct ios_gah gah;
	int rc;
	int err;
};

struct iof_readdir_in {
	struct ios_gah gah;
	crt_bulk_t bulk;
	uint64_t offset;
};

/* Each READDIR rpc contains an array of these */
struct iof_readdir_reply {
	char d_name[NAME_MAX + 1];
	struct stat stat;
	off_t nextoff;
	int read_rc;
	int stat_rc;
};

struct iof_readdir_out {
	d_iov_t replies;
	int last;
	int iov_count;
	int bulk_count;
	int err;
};

struct iof_open_out {
	struct ios_gah gah;
	int rc;
	int err;
};

struct iof_data_out {
	d_iov_t data;
	int rc;
	int err;
};

struct iof_status_out {
	int rc;
	int err;
};

struct iof_gah_in {
	struct ios_gah gah;
};

struct iof_setattr_in {
	struct ios_gah gah;
	struct stat stat;
	uint32_t to_set;
};

extern struct crt_req_format QUERY_RPC_FMT;

#define DEF_RPC_TYPE(TYPE) IOF_OPI_##TYPE

#define IOF_RPCS_LIST					\
	X(opendir,	gah_in,		gah_pair)	\
	X(readdir,	readdir_in,	readdir_out)	\
	X(closedir,	gah_in,		NULL)		\
	X(getattr,	gah_in,		attr_out)	\
	X(rename,	rename_in,	status_out)	\
	X(unlink,	unlink_in,	status_out)	\
	X(open,		open_in,	gah_pair)	\
	X(create,	create_in,	create_out)	\
	X(close,	gah_in,		NULL)		\
	X(mkdir,	create_in,	entry_out)	\
	X(readlink,	gah_in,		string_out)	\
	X(symlink,	two_string_in,	entry_out)	\
	X(fsync,	gah_in,		status_out)	\
	X(fdatasync,	gah_in,		status_out)	\
	X(statfs,	gah_in,		iov_pair)	\
	X(lookup,	gah_string_in,	entry_out)	\
	X(setattr,	setattr_in,	attr_out)	\
	X(imigrate,	imigrate_in,	entry_out)

#define X(a, b, c) DEF_RPC_TYPE(a),

enum {
	IOF_RPCS_LIST
};

#undef X

#define IOF_STRUCT_XTVEC		\
	((uint64_t) (xt_off) CRT_VAR)	\
	((uint64_t) (xt_len) CRT_VAR)

CRT_GEN_STRUCT(iof_xtvec, IOF_STRUCT_XTVEC);

#define IOF_RPC_READX_IN					\
	((struct ios_gah)	(gah)		CRT_VAR)	\
	((struct iof_xtvec)	(xtvec)		CRT_VAR)	\
	((uint64_t)		(xtvec_len)	CRT_VAR)	\
	((uint64_t)		(bulk_len)	CRT_VAR)	\
	((crt_bulk_t)		(xtvec_bulk)	CRT_VAR)	\
	((crt_bulk_t)		(data_bulk)	CRT_VAR)

#define IOF_RPC_READX_OUT			\
	((d_iov_t) (data) CRT_VAR)		\
	((uint64_t) (bulk_len) CRT_VAR)		\
	((uint32_t) (iov_len) CRT_VAR)		\
	((int) (rc) CRT_VAR)			\
	((int) (err) CRT_VAR)

CRT_RPC_DECLARE(iof_readx, IOF_RPC_READX_IN, IOF_RPC_READX_OUT)

#define IOF_RPC_WRITEX_IN					\
	((struct ios_gah)	(gah)		CRT_VAR)	\
	((d_iov_t)		(data)		CRT_VAR)	\
	((struct iof_xtvec)	(xtvec)		CRT_VAR)	\
	((uint64_t)		(xtvec_len)	CRT_VAR)	\
	((uint64_t)		(bulk_len)	CRT_VAR)	\
	((crt_bulk_t)		(xtvec_bulk)	CRT_VAR)	\
	((crt_bulk_t)		(data_bulk)	CRT_VAR)

#define IOF_RPC_WRITEX_OUT			\
	((uint64_t)	(len)	CRT_VAR)	\
	((int)		(rc)	CRT_VAR)	\
	((int)		(err)	CRT_VAR)	\
	((uint64_t)	(pad0)	CRT_VAR)	\
	((uint64_t)	(pad1)	CRT_VAR)

CRT_RPC_DECLARE(iof_writex, IOF_RPC_WRITEX_IN, IOF_RPC_WRITEX_OUT)

int
iof_write_register(crt_rpc_cb_t handlers[]);

int
iof_io_register(struct crt_proto_format **proto,
		crt_rpc_cb_t handlers[]);

int
iof_signon_register(crt_rpc_cb_t handlers[]);

int
iof_client_register(crt_endpoint_t *tgt_ep,
		    struct crt_proto_format **signon,
		    struct crt_proto_format **write,
		    struct crt_proto_format **io);

#endif
