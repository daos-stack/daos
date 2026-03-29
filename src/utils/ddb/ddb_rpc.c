/**
 * Copyright 2026 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(ddb)

#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <daos/debug.h>
#include <gurt/debug.h>

#include "ddb_common.h"
#include "ddb_vos.h"

#define PIPE_READ 0
#define PIPE_WRITE 1

struct rpc_ctx {
        int tx;
        int rx;
};

enum rcp_request_type {
        RPC_INIT,
        RPC_DEV_LIST_INIT,
        RPC_FINI,
};

struct rpc_init_req {
       char db_path[DDB_PATH_MAX]; 
};

struct rpc_req {
        enum rcp_request_type type;
        union {
                struct rpc_init_req init;
        };
};

struct rpc_dev_list_init_resp {
        int dev_cnt;
};

struct rpc_dev_list_resp {
        uuid_t dev_id;
        uint32_t dev_flags;
};

struct rpc_resp {
        int rc;
        union {
                struct rpc_dev_list_init_resp dev_list_init;
                struct rpc_dev_list_resp dev_list;
        };
};

static void
server_dv_list(struct rpc_ctx *ctx)
{
	struct bio_dev_info *dev_info              = NULL, *tmp;
	d_list_t             dev_list;
	int                  dev_cnt = 0;
        struct rpc_resp resp;

        D_INIT_LIST_HEAD(&dev_list);
        resp.rc = dv_dev_list(&dev_list, &dev_cnt);
        resp.dev_list_init.dev_cnt = dev_cnt;
        write(ctx->tx, &resp, sizeof(struct rpc_resp));
        if (resp.rc != DER_SUCCESS) {
                return;
        }
	d_list_for_each_entry_safe(dev_info, tmp, &dev_list, bdi_link) {
                uuid_copy(resp.dev_list.dev_id, dev_info->bdi_dev_id);
                resp.dev_list.dev_flags = dev_info->bdi_flags;

                write(ctx->tx, &resp, sizeof(struct rpc_resp));

		d_list_del_init(&dev_info->bdi_link);
		bio_free_dev_info(dev_info);
	}
}

static void
server_loop(int rx, int tx)
{
        struct rpc_ctx ctx = {.rx = tx, .tx = tx};
        struct rpc_req req;
        struct rpc_resp resp;
        bool stop = false;
        int rc;

        do {
                read(rx, &req, sizeof(struct rpc_req));

                switch (req.type) {
                        case RPC_INIT:
                                rc = dv_init(req.init.db_path);
                                if (rc != DER_SUCCESS) {
                                        stop = true;
                                }

                                resp.rc = rc;
                                write(tx, &resp, sizeof(struct rpc_resp));
                        break;
                        case RPC_FINI:
                                dv_fini();
                                resp.rc = DER_SUCCESS;
                                write(tx, &resp, sizeof(struct rpc_resp));
                                stop = true;
                        break;
                        case RPC_DEV_LIST_INIT:
                                server_dv_list(&ctx);
                        break;
                }
        } while(!stop);
}

int
ddb_rpc_server_init(struct ddb_ctx *ctx, const char *db_path)
{
        int p2c[2]; // parent to child
        int c2p[2]; // child to parent

        struct rpc_req req;
        struct rpc_resp resp;

        pipe(p2c);
        pipe(c2p);

        pid_t pid = fork();

        if (pid == 0) {
                // --- CHILD PROCESS ---
                close(p2c[PIPE_WRITE]);
                close(c2p[PIPE_READ]);

                server_loop(p2c[PIPE_READ], c2p[PIPE_WRITE]);

                close(p2c[PIPE_READ]);
                close(c2p[PIPE_WRITE]);

                exit(0);
        } else {
                // --- PARENT PROCESS ---       
                close(p2c[PIPE_READ]);
                close(c2p[PIPE_WRITE]);

                ctx->tx = p2c[PIPE_WRITE];
                ctx->rx = c2p[PIPE_READ];

                req.type = RPC_INIT;
                strncpy(req.init.db_path, db_path, DDB_PATH_MAX);

                write(ctx->tx, &req, sizeof(struct rpc_req));

                read(ctx->rx, &resp, sizeof(struct rpc_resp));

                if (resp.rc != DER_SUCCESS) {
                        ddb_errorf(ctx, "Initialize standalone VOS failed: " DF_RC "\n", DP_RC(resp.rc));
                }

                return resp.rc;
        }
}

int
ddb_rpc_server_fini(struct ddb_ctx *ctx)
{
        struct rpc_req req;
        struct rpc_resp resp;

        req.type = RPC_FINI;
        write(ctx->tx, &req, sizeof(struct rpc_req));
        read(ctx->rx, &resp, sizeof(struct rpc_resp));

        close(ctx->tx);
        close(ctx->rx);

        ctx->tx = ctx->rx = -1;

        return resp.rc;
}

int
ddb_rpc_dev_list_init(struct ddb_ctx *ctx, int *dev_cnt)
{
        struct rpc_req req;
        struct rpc_resp resp;

        req.type = RPC_DEV_LIST_INIT;

        write(ctx->tx, &req, sizeof(struct rpc_req));
        read(ctx->rx, &resp, sizeof(struct rpc_resp));

	if (resp.rc != DER_SUCCESS) {
		DL_ERROR(resp.rc, "Failed to list devices.");
                return resp.rc;
        }

        *dev_cnt = resp.dev_list_init.dev_cnt;

	return DER_SUCCESS;
}

void
ddb_rpc_dev_list(struct ddb_ctx *ctx, struct bio_dev_info *dev_info)
{
        struct rpc_resp resp;

        read(ctx->rx, &resp, sizeof(struct rpc_resp));

        uuid_copy(dev_info->bdi_dev_id, resp.dev_list.dev_id);
        dev_info->bdi_flags = resp.dev_list.dev_flags;
}
