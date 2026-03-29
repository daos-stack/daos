/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_RPC_H
#define DAOS_DDB_RPC_H

int
ddb_rpc_server_init(struct ddb_ctx *ctx, const char *db_path);

int
ddb_rpc_server_fini(struct ddb_ctx *ctx);

int
ddb_rpc_dev_list_init(struct ddb_ctx *ctx, int *dev_cnt);

void
ddb_rpc_dev_list(struct ddb_ctx *ctx, struct bio_dev_info *dev_info);

#endif /* DAOS_DDB_RPC_H */
