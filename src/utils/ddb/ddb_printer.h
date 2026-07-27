/**
 * (C) Copyright 2022 Intel Corporation.
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_PRINTER_H
#define DAOS_DDB_PRINTER_H

#include "ddb_vos.h"

#define DF_IDX "[%d]"
#define DP_IDX(idx) idx

uint32_t
ddb_iov_to_printable_buf(d_iov_t *iov, char buf[], uint32_t buf_len, const char *prefix);
uint32_t
ddb_key_to_printable_buf(daos_key_t *key, enum daos_otype_t otype, char buf[], uint32_t buf_len);
void ddb_print_cont(struct ddb_ctx *ctx, struct ddb_cont *cont);
void ddb_print_obj(struct ddb_ctx *ctx, struct ddb_obj *obj, uint32_t indent);
void ddb_print_key(struct ddb_ctx *ctx, struct ddb_key *key, uint32_t indent);
void ddb_print_sv(struct ddb_ctx *ctx, struct ddb_sv *sv, uint32_t indent);
void ddb_print_array(struct ddb_ctx *ctx, struct ddb_array *sv, uint32_t indent);
void ddb_print_path(struct ddb_ctx *ctx, struct dv_indexed_tree_path *itp, uint32_t indent);
void ddb_print_superblock(struct ddb_ctx *ctx, struct ddb_superblock *sb);
void ddb_print_ilog_entry(struct ddb_ctx *ctx, struct ddb_ilog_entry *entry);
void ddb_print_dtx_committed(struct ddb_ctx *ctx, struct dv_dtx_committed_entry *entry);
void ddb_print_dtx_active(struct ddb_ctx *ctx, struct dv_dtx_active_entry *entry);

bool ddb_can_print(d_iov_t *iov);

/* some utility functions helpful for printing */
void ddb_bytes_hr(uint64_t bytes, char *buf, uint32_t buf_len);

static inline bool
ddb_key_is_lexical(enum daos_otype_t otype)
{
	return daos_is_dkey_lexical_type(otype) || daos_is_akey_lexical_type(otype);
}

static inline bool
ddb_key_is_int(enum daos_otype_t otype)
{
	return daos_is_dkey_uint64_type(otype) || daos_is_akey_uint64_type(otype);
}

#endif /* DAOS_DDB_PRINTER_H */
