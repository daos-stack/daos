/*
 * Copyright (c) 2021 Nutanix Inc. All rights reserved.
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

#include <sys/param.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <strings.h>

#include "tran_pipe.h"

typedef struct {
    int in_fd;
    int out_fd;
} tran_pipe_t;

static int
tran_pipe_send_iovec(int fd, uint16_t msg_id, bool is_reply,
                     enum vfio_user_command cmd,
                     struct iovec *iovecs, size_t nr_iovecs, int err)
{
    struct vfio_user_header hdr = { .msg_id = msg_id };
    ssize_t ret;

    if (nr_iovecs == 0) {
        iovecs = alloca(sizeof(*iovecs));
        nr_iovecs = 1;
    }

    if (is_reply) {
        hdr.flags.type = VFIO_USER_F_TYPE_REPLY;
        hdr.cmd = cmd;
        if (err != 0) {
            hdr.flags.error = 1U;
            hdr.error_no = err;
        }
    } else {
        hdr.cmd = cmd;
        hdr.flags.type = VFIO_USER_F_TYPE_COMMAND;
    }

    iovecs[0].iov_base = &hdr;
    iovecs[0].iov_len = sizeof(hdr);

    ret = writev(fd, iovecs, nr_iovecs);

    /* Quieten static analysis. */
    iovecs[0].iov_base = NULL;
    iovecs[0].iov_len = 0;

    if (ret == -1) {
        /* Treat a failed write due to EPIPE the same as a short write. */
        if (errno == EPIPE) {
            return ERROR_INT(ECONNRESET);
        }
        return -1;
    } else if (ret < hdr.msg_size) {
        return ERROR_INT(ECONNRESET);
    }

    return 0;
}

static int
tran_pipe_get_msg(void *data, size_t len, int fd)
{
    ssize_t ret;

    ret = read(fd, data, len);

    if (ret == -1) {
        return -1;
    } else if (ret == 0) {
        return ERROR_INT(ENOMSG);
    } else if ((size_t)ret < len) {
        return ERROR_INT(ECONNRESET);
    }

    return ret;
}

/*
 * Receive a vfio-user message.  If "len" is set to non-zero, the message should
 * include data of that length, which is stored in the pre-allocated "data"
 * pointer.
 */
static int
tran_pipe_recv(int fd, struct vfio_user_header *hdr, bool is_reply,
               uint16_t *msg_id, void *data, size_t *len)
{
    int ret;

    /* FIXME if ret == -1 then fcntl can overwrite recv's errno */

    ret = tran_pipe_get_msg(hdr, sizeof(*hdr), fd);
    if (ret < 0) {
        return ret;
    }

    if (is_reply) {
        if (msg_id != NULL && hdr->msg_id != *msg_id) {
            return ERROR_INT(EPROTO);
        }

        if (hdr->flags.type != VFIO_USER_F_TYPE_REPLY) {
            return ERROR_INT(EINVAL);
        }

        if (hdr->flags.error == 1U) {
            if (hdr->error_no <= 0) {
                hdr->error_no = EINVAL;
            }
            return ERROR_INT(hdr->error_no);
        }
    } else {
        if (hdr->flags.type != VFIO_USER_F_TYPE_COMMAND) {
            return ERROR_INT(EINVAL);
        }
        if (msg_id != NULL) {
            *msg_id = hdr->msg_id;
        }
    }

    if (hdr->msg_size < sizeof(*hdr) || hdr->msg_size > SERVER_MAX_MSG_SIZE) {
        return ERROR_INT(EINVAL);
    }

    if (len != NULL && *len > 0 && hdr->msg_size > sizeof(*hdr)) {
        ret = read(fd, data, MIN(hdr->msg_size - sizeof(*hdr), *len));
        if (ret < 0) {
            return -1;
        } else if (ret == 0) {
            return ERROR_INT(ENOMSG);
        } else if (*len != (size_t)ret) {
            return ERROR_INT(ECONNRESET);
        }
        *len = ret;
    }

    return 0;
}

/*
 * Like tran_pipe_recv(), but will automatically allocate reply data.
 */
static int
tran_pipe_recv_alloc(int fd, struct vfio_user_header *hdr, bool is_reply,
                     uint16_t *msg_id, void **datap, size_t *lenp)
{
    void *data;
    size_t len;
    int ret;

    ret = tran_pipe_recv(fd, hdr, is_reply, msg_id, NULL, NULL);

    if (ret != 0) {
        return ret;
    }

    assert(hdr->msg_size >= sizeof(*hdr));
    assert(hdr->msg_size <= SERVER_MAX_MSG_SIZE);

    len = hdr->msg_size - sizeof(*hdr);

    if (len == 0) {
        *datap = NULL;
        *lenp = 0;
        return 0;
    }

    data = calloc(1, len);

    if (data == NULL) {
        return -1;
    }

    ret = read(fd, data, len);
    if (ret < 0) {
        ret = errno;
        free(data);
        return ERROR_INT(ret);
    } else if (ret == 0) {
        free(data);
        return ERROR_INT(ENOMSG);
    } else if (len != (size_t)ret) {
        free(data);
        return ERROR_INT(ECONNRESET);
    }

    *datap = data;
    *lenp = len;
    return 0;
}

/*
 * FIXME: all these send/recv handlers need to be made robust against async
 * messages.
 */
static int
tran_pipe_msg_iovec(tran_pipe_t *tp, uint16_t msg_id,
                    enum vfio_user_command cmd,
                    struct iovec *iovecs, size_t nr_iovecs,
                    struct vfio_user_header *hdr,
                    void *recv_data, size_t recv_len)
{
    int ret = tran_pipe_send_iovec(tp->out_fd, msg_id, false, cmd, iovecs,
                                   nr_iovecs, 0);
    if (ret < 0) {
        return ret;
    }
    if (hdr == NULL) {
        hdr = alloca(sizeof(*hdr));
    }
    return tran_pipe_recv(tp->in_fd, hdr, true, &msg_id, recv_data, &recv_len);
}

static int
tran_pipe_init(vfu_ctx_t *vfu_ctx)
{
    tran_pipe_t *tp = NULL;

    assert(vfu_ctx != NULL);

    tp = calloc(1, sizeof(tran_pipe_t));

    if (tp == NULL) {
        return -1;
    }

    tp->in_fd = -1;
    tp->out_fd = -1;

    vfu_ctx->tran_data = tp;
    return 0;
}

static int
tran_pipe_get_poll_fd(vfu_ctx_t *vfu_ctx)
{
    tran_pipe_t *tp = vfu_ctx->tran_data;

    return tp->in_fd;
}

static int
tran_pipe_attach(vfu_ctx_t *vfu_ctx)
{
    tran_pipe_t *tp;
    int ret;

    assert(vfu_ctx != NULL);
    assert(vfu_ctx->tran_data != NULL);

    tp = vfu_ctx->tran_data;

    tp->in_fd = STDIN_FILENO;
    tp->out_fd = STDOUT_FILENO;

    ret = tran_negotiate(vfu_ctx);
    if (ret < 0) {
        ret = errno;
        tp->in_fd = -1;
        tp->out_fd = -1;
        return ERROR_INT(ret);
    }

    return 0;
}

static int
tran_pipe_get_request_header(vfu_ctx_t *vfu_ctx, struct vfio_user_header *hdr,
                             int *fds UNUSED, size_t *nr_fds)
{
    tran_pipe_t *tp;

    assert(vfu_ctx != NULL);
    assert(vfu_ctx->tran_data != NULL);

    tp = vfu_ctx->tran_data;

    *nr_fds = 0;

    return tran_pipe_get_msg(hdr, sizeof(*hdr), tp->in_fd);
}

static int
tran_pipe_recv_body(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg)
{
    tran_pipe_t *tp;
    int ret;

    assert(vfu_ctx != NULL);
    assert(vfu_ctx->tran_data != NULL);
    assert(msg != NULL);

    tp = vfu_ctx->tran_data;

    assert(msg->in.iov.iov_len <= SERVER_MAX_MSG_SIZE);

    msg->in.iov.iov_base = malloc(msg->in.iov.iov_len);

    if (msg->in.iov.iov_base == NULL) {
        return -1;
    }

    ret = read(tp->in_fd, msg->in.iov.iov_base, msg->in.iov.iov_len);

    if (ret < 0) {
        ret = errno;
        free(msg->in.iov.iov_base);
        msg->in.iov.iov_base = NULL;
        return ERROR_INT(ret);
    } else if (ret == 0) {
        free(msg->in.iov.iov_base);
        msg->in.iov.iov_base = NULL;
        return ERROR_INT(ENOMSG);
    } else if (ret != (int)msg->in.iov.iov_len)  {
        vfu_log(vfu_ctx, LOG_ERR, "msg%#hx: short read: expected=%zu, actual=%d",
                msg->hdr.msg_id, msg->in.iov.iov_len, ret);
        free(msg->in.iov.iov_base);
        msg->in.iov.iov_base = NULL;
        return ERROR_INT(EINVAL);
    }

    return 0;
}

static int
tran_pipe_recv_msg(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg)
{
    tran_pipe_t *tp;

    assert(vfu_ctx != NULL);
    assert(vfu_ctx->tran_data != NULL);
    assert(msg != NULL);

    tp = vfu_ctx->tran_data;

    if (tp->in_fd == -1) {
        vfu_log(vfu_ctx, LOG_ERR, "%s: not connected", __func__);
        return ERROR_INT(ENOTCONN);
    }

    return tran_pipe_recv_alloc(tp->in_fd, &msg->hdr, false, NULL,
                                &msg->in.iov.iov_base, &msg->in.iov.iov_len);
}

static int
tran_pipe_reply(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg, int err)
{
    struct iovec *iovecs;
    size_t nr_iovecs;
    tran_pipe_t *tp;
    int ret;

    assert(vfu_ctx != NULL);
    assert(vfu_ctx->tran_data != NULL);
    assert(msg != NULL);

    tp = vfu_ctx->tran_data;

    /* First iovec entry is for msg header. */
    nr_iovecs = (msg->nr_out_iovecs != 0) ? (msg->nr_out_iovecs + 1) : 2;
    iovecs = calloc(nr_iovecs, sizeof(*iovecs));

    if (iovecs == NULL) {
        return -1;
    }

    if (msg->out_iovecs != NULL) {
        bcopy(msg->out_iovecs, iovecs + 1,
              msg->nr_out_iovecs * sizeof(*iovecs));
    } else {
        iovecs[1].iov_base = msg->out.iov.iov_base;
        iovecs[1].iov_len = msg->out.iov.iov_len;
    }

    ret = tran_pipe_send_iovec(tp->out_fd, msg->hdr.msg_id, true, msg->hdr.cmd,
                               iovecs, nr_iovecs, err);

    free(iovecs);

    return ret;
}

static int
tran_pipe_send_msg(vfu_ctx_t *vfu_ctx, uint16_t msg_id,
              enum vfio_user_command cmd,
              void *send_data, size_t send_len,
              struct vfio_user_header *hdr,
              void *recv_data, size_t recv_len)
{
    /* [0] is for the header. */
    struct iovec iovecs[2] = {
        [1] = {
            .iov_base = send_data,
            .iov_len = send_len
        }
    };
    tran_pipe_t *tp;

    assert(vfu_ctx != NULL);
    assert(vfu_ctx->tran_data != NULL);

    tp = vfu_ctx->tran_data;

    return tran_pipe_msg_iovec(tp, msg_id, cmd, iovecs, ARRAY_SIZE(iovecs),
                               hdr, recv_data, recv_len);
}

static void
tran_pipe_detach(vfu_ctx_t *vfu_ctx)
{
    assert(vfu_ctx != NULL);
}

static void
tran_pipe_fini(vfu_ctx_t *vfu_ctx)
{
    assert(vfu_ctx != NULL);

    free(vfu_ctx->tran_data);
    vfu_ctx->tran_data = NULL;
}

struct transport_ops tran_pipe_ops = {
    .init = tran_pipe_init,
    .get_poll_fd = tran_pipe_get_poll_fd,
    .attach = tran_pipe_attach,
    .get_request_header = tran_pipe_get_request_header,
    .recv_body = tran_pipe_recv_body,
    .reply = tran_pipe_reply,
    .recv_msg = tran_pipe_recv_msg,
    .send_msg = tran_pipe_send_msg,
    .detach = tran_pipe_detach,
    .fini = tran_pipe_fini
};

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
