/*
 * Copyright (c) 2019 Nutanix Inc. All rights reserved.
 *
 * Authors: Thanos Makatos <thanos@nutanix.com>
 *          Swapnil Ingle <swapnil.ingle@nutanix.com>
 *          Felipe Franciosi <felipe@nutanix.com>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *      * Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *      * Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *      * Neither the name of Nutanix nor the names of its contributors may be
 *        used to endorse or promote products derived from this software without
 *        specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 *  DAMAGE.
 *
 */

#ifndef LIB_VFIO_USER_TRAN_H
#define LIB_VFIO_USER_TRAN_H

#include "libvfio-user.h"
#include "private.h"

struct transport_ops {
    int (*init)(vfu_ctx_t *vfu_ctx);

    int (*get_poll_fd)(vfu_ctx_t *vfu_ctx);

    int (*attach)(vfu_ctx_t *vfu_ctx);

    int (*get_request_header)(vfu_ctx_t *vfu_ctx, struct vfio_user_header *hdr,
                              int *fds, size_t *nr_fds);

    int (*recv_body)(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg);

    int (*reply)(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg, int err);

    int (*recv_msg)(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg);

    int (*send_msg)(vfu_ctx_t *vfu_ctx, uint16_t msg_id,
                    enum vfio_user_command cmd,
                    void *send_data, size_t send_len,
                    struct vfio_user_header *hdr,
                    void *recv_data, size_t recv_len);

    void (*detach)(vfu_ctx_t *vfu_ctx);
    void (*fini)(vfu_ctx_t *vfu_ctx);
};

/* The largest number of fd's we are prepared to receive. */
// FIXME: value?
#define VFIO_USER_CLIENT_MAX_MSG_FDS_LIMIT (1024)

/*
 * Parse JSON supplied from the other side into the known parameters. Note: they
 * will not be set if not found in the JSON.
 */
int
tran_parse_version_json(const char *json_str, int *client_max_fdsp,
                        size_t *client_max_data_xfer_sizep, size_t *pgsizep);

int
tran_negotiate(vfu_ctx_t *vfu_ctx);

#endif /* LIB_VFIO_USER_TRAN_H */

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
