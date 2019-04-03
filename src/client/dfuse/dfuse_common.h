/**
 * (C) Copyright 2016-2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#ifndef IOF_COMMON_H
#define IOF_COMMON_H

#include "dfuse_log.h"

#include <sys/stat.h>
#include <cart/api.h>
#include <gurt/common.h>

#include "dfuse_gah.h"

#define IOF_CNSS_MT			0x080UL
#define IOF_FUSE_READ_BUF		0x100UL
#define IOF_FUSE_WRITE_BUF		0x200UL

/* The name of a filesystem entry
 *
 */
struct ios_name {
	char name[NAME_MAX + 1];
};

struct dfuse_gah_string_in {
	struct ios_gah gah;
	struct ios_name name;
};

struct dfuse_imigrate_in {
	struct ios_gah gah;
	struct ios_name name;
	int inode;
};

struct dfuse_string_out {
	d_string_t path;
	int rc;
	int err;
};

struct dfuse_entry_out {
	struct ios_gah gah;
	struct stat stat;
	int rc;
	int err;
};

struct dfuse_create_out {
	struct ios_gah gah;
	struct ios_gah igah;
	struct stat stat;
	int rc;
	int err;
};

struct dfuse_two_string_in {
	struct dfuse_gah_string_in common;
	d_string_t oldpath;
};

struct dfuse_create_in {
	struct dfuse_gah_string_in common;
	uint32_t mode;
	uint32_t flags;
};

/* We reuse dfuse_gah_string_in in a few input structs and we need to
 * ensure compiler isn't adding padding.   This should always be
 * the case now unless we change the struct.  This assert is here
 * to force the modifier to ensure the same condition is met.
 */
_Static_assert(sizeof(struct dfuse_gah_string_in) ==
	       (sizeof(struct ios_gah) + sizeof(struct ios_name)),
	       "dfuse_gah_string_in size unexpected");

_Static_assert(NAME_MAX == 255, "NAME_MAX wrong size");

struct dfuse_rename_in {
	struct ios_gah old_gah;
	struct ios_gah new_gah;
	struct ios_name old_name;
	struct ios_name new_name;
	uint32_t flags;
};

struct dfuse_open_in {
	struct ios_gah gah;
	uint32_t flags;
};

struct dfuse_unlink_in {
	struct ios_name name;
	struct ios_gah gah;
	uint32_t flags;
};

struct dfuse_attr_out {
	struct stat stat;
	int rc;
	int err;
};

struct dfuse_opendir_out {
	struct ios_gah gah;
	int rc;
	int err;
};

struct dfuse_readdir_in {
	struct ios_gah gah;
	crt_bulk_t bulk;
	uint64_t offset;
};

/* Each READDIR rpc contains an array of these */
struct dfuse_readdir_reply {
	char d_name[NAME_MAX + 1];
	struct stat stat;
	off_t nextoff;
	int read_rc;
	int stat_rc;
};

struct dfuse_readdir_out {
	d_iov_t replies;
	int last;
	int iov_count;
	int bulk_count;
	int err;
};

struct dfuse_open_out {
	struct ios_gah gah;
	int rc;
	int err;
};

struct dfuse_data_out {
	d_iov_t data;
	int rc;
	int err;
};

struct dfuse_status_out {
	int rc;
	int err;
};

struct dfuse_gah_in {
	struct ios_gah gah;
};

struct dfuse_setattr_in {
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
	((uint64_t)(xt_off) CRT_VAR)	\
	((uint64_t)(xt_len) CRT_VAR)

CRT_GEN_STRUCT(dfuse_xtvec, IOF_STRUCT_XTVEC);

#define IOF_RPC_READX_IN					\
	((struct ios_gah)(gah)		CRT_VAR)	\
	((struct dfuse_xtvec)(xtvec)		CRT_VAR)	\
	((uint64_t)(xtvec_len)	CRT_VAR)	\
	((uint64_t)(bulk_len)	CRT_VAR)	\
	((crt_bulk_t)(xtvec_bulk)	CRT_VAR)	\
	((crt_bulk_t)(data_bulk)	CRT_VAR)

#define IOF_RPC_READX_OUT			\
	((d_iov_t)(data) CRT_VAR)		\
	((uint64_t)(bulk_len) CRT_VAR)		\
	((uint32_t)(iov_len) CRT_VAR)		\
	((int)(rc) CRT_VAR)			\
	((int)(err) CRT_VAR)

CRT_RPC_DECLARE(dfuse_readx, IOF_RPC_READX_IN, IOF_RPC_READX_OUT)

#define IOF_RPC_WRITEX_IN					\
	((struct ios_gah)(gah)		CRT_VAR)	\
	((d_iov_t)(data)		CRT_VAR)	\
	((struct dfuse_xtvec)(xtvec)		CRT_VAR)	\
	((uint64_t)(xtvec_len)	CRT_VAR)	\
	((uint64_t)(bulk_len)	CRT_VAR)	\
	((crt_bulk_t)(xtvec_bulk)	CRT_VAR)	\
	((crt_bulk_t)(data_bulk)	CRT_VAR)

#define IOF_RPC_WRITEX_OUT			\
	((uint64_t)(len)	CRT_VAR)	\
	((int)(rc)	CRT_VAR)	\
	((int)(err)	CRT_VAR)	\
	((uint64_t)(pad0)	CRT_VAR)	\
	((uint64_t)(pad1)	CRT_VAR)

CRT_RPC_DECLARE(dfuse_writex, IOF_RPC_WRITEX_IN, IOF_RPC_WRITEX_OUT)

int
dfuse_write_register(crt_rpc_cb_t handlers[]);

int
dfuse_io_register(struct crt_proto_format **proto,
		crt_rpc_cb_t handlers[]);

int
dfuse_client_register(crt_endpoint_t *tgt_ep,
		    struct crt_proto_format **write,
		    struct crt_proto_format **io);

#endif
