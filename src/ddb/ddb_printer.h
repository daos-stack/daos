/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_PRINTER_H
#define DAOS_DDB_PRINTER_H

#include "ddb_vos.h"

void ddb_print_cont(struct ddb_ctx *ctx, struct ddb_cont *cont);
void ddb_print_obj(struct ddb_ctx *ctx, struct ddb_obj *obj, uint32_t indent);
void ddb_print_key(struct ddb_ctx *ctx, struct ddb_key *key, uint32_t indent);
void ddb_print_sv(struct ddb_ctx *ctx, struct ddb_sv *sv, uint32_t indent);
void ddb_print_array(struct ddb_ctx *ctx, struct ddb_array *sv, uint32_t indent);
void ddb_print_superblock(struct ddb_ctx *ctx, struct ddb_superblock *sb);
void ddb_print_ilog_entry(struct ddb_ctx *ctx, struct ddb_ilog_entry *entry);
void ddb_print_dtx_committed(struct ddb_ctx *ctx, struct dv_dtx_committed_entry *entry);
void ddb_print_dtx_active(struct ddb_ctx *ctx, struct dv_dtx_active_entry *entry);

/* some utility functions helpful for printing */
void ddb_bytes_hr(uint64_t bytes, char *buf, uint32_t buf_len);


#endif /* DAOS_DDB_PRINTER_H */
