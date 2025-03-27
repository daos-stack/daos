/*
 * Copyright (c) 2020 Nutanix Inc. All rights reserved.
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
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>

#include "tran_sock.h"

typedef struct {
    int listen_fd;
    int conn_fd;
} tran_sock_t;

int
tran_sock_send_iovec(int sock, uint16_t msg_id, bool is_reply,
                     enum vfio_user_command cmd,
                     struct iovec *iovecs, size_t nr_iovecs,
                     int *fds, int count, int err)
{
    int ret;
    struct vfio_user_header hdr = { .msg_id = msg_id };
    struct msghdr msg;
    size_t i;
    size_t size = count * sizeof(*fds);
    char *buf;

    if (nr_iovecs == 0) {
        iovecs = alloca(sizeof(*iovecs));
        nr_iovecs = 1;
    }

    memset(&msg, 0, sizeof(msg));

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

    for (i = 0; i < nr_iovecs; i++) {
        hdr.msg_size += iovecs[i].iov_len;
    }

    msg.msg_iovlen = nr_iovecs;
    msg.msg_iov = iovecs;

    if (fds != NULL) {
        size_t cmsg_space_aligned = MAX(CMSG_SPACE(size), sizeof(struct cmsghdr));

        buf = alloca(cmsg_space_aligned);
        memset(buf, 0, cmsg_space_aligned);

        msg.msg_control = buf;
        msg.msg_controllen = CMSG_SPACE(size);

        struct cmsghdr * cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(size);
        memcpy(CMSG_DATA(cmsg), fds, size);
    }

    ret = sendmsg(sock, &msg, MSG_NOSIGNAL);

    if (ret == -1) {
        /* Treat a failed write due to EPIPE the same as a short write. */
        if (errno == EPIPE) {
            return ERROR_INT(ECONNRESET);
        }
        return -1;
    } else if ((size_t)ret < hdr.msg_size) {
        return ERROR_INT(ECONNRESET);
    }

    return 0;
}

int
tran_sock_send(int sock, uint16_t msg_id, bool is_reply,
               enum vfio_user_command cmd,
               void *data, size_t data_len)
{
    /* [0] is for the header. */
    struct iovec iovecs[2] = {
        [1] = {
            .iov_base = data,
            .iov_len = data_len
        }
    };
    return tran_sock_send_iovec(sock, msg_id, is_reply, cmd, iovecs,
                                ARRAY_SIZE(iovecs), NULL, 0, 0);
}

static int
get_msg(void *data, size_t len, int *fds, size_t *nr_fds, int sock_fd,
        int sock_flags)
{
    int ret;
    struct iovec iov = {.iov_base = data, .iov_len = len};
    struct msghdr msg = {.msg_iov = &iov, .msg_iovlen = 1};
    struct cmsghdr *cmsg;

    if (nr_fds != NULL && *nr_fds > 0) {
        assert(fds != NULL);
        msg.msg_controllen = CMSG_SPACE(sizeof(int) * *nr_fds);
        msg.msg_control = alloca(msg.msg_controllen);
        *nr_fds = 0;
    }

    ret = recvmsg(sock_fd, &msg, sock_flags);
    if (ret == -1) {
        return -1;
    } else if (ret == 0) {
        return ERROR_INT(ENOMSG);
    } else if ((size_t)ret < len) {
        return ERROR_INT(ECONNRESET);
    }

    if (msg.msg_flags & MSG_CTRUNC || msg.msg_flags & MSG_TRUNC) {
        return ERROR_INT(EFAULT);
    }

    if (nr_fds != NULL) {
        for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
            if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
                continue;
            }
            if (cmsg->cmsg_len < CMSG_LEN(sizeof(int))) {
                return ERROR_INT(EINVAL);
            }
            int size = cmsg->cmsg_len - CMSG_LEN(0);
            if (size % sizeof(int) != 0) {
                return ERROR_INT(EINVAL);
            }
            *nr_fds = (int)(size / sizeof(int));
            memcpy(fds, CMSG_DATA(cmsg), *nr_fds * sizeof(int));
            break;
        }
    }

    return ret;
}

/*
 * Receive a vfio-user message.  If "len" is set to non-zero, the message should
 * include data of that length, which is stored in the pre-allocated "data"
 * pointer.
 */
static int
tran_sock_recv_fds(int sock, struct vfio_user_header *hdr, bool is_reply,
                   uint16_t *msg_id, void *data, size_t *len, int *fds,
                   size_t *nr_fds)
{
    int ret;

    /* FIXME if ret == -1 then fcntl can overwrite recv's errno */

    ret = get_msg(hdr, sizeof(*hdr), fds, nr_fds, sock, 0);
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
        ret = recv(sock, data, MIN(hdr->msg_size - sizeof(*hdr), *len),
                   MSG_WAITALL);
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

int
tran_sock_recv(int sock, struct vfio_user_header *hdr, bool is_reply,
         uint16_t *msg_id, void *data, size_t *len)
{
    return tran_sock_recv_fds(sock, hdr, is_reply, msg_id,
                              data, len, NULL, NULL);
}

/*
 * Like tran_sock_recv(), but will automatically allocate reply data.
 */
int
tran_sock_recv_alloc(int sock, struct vfio_user_header *hdr, bool is_reply,
                     uint16_t *msg_id, void **datap, size_t *lenp)
{
    void *data;
    size_t len;
    int ret;

    ret = tran_sock_recv(sock, hdr, is_reply, msg_id, NULL, NULL);

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

    ret = recv(sock, data, len, MSG_WAITALL);
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
int
tran_sock_msg_iovec(int sock, uint16_t msg_id, enum vfio_user_command cmd,
                    struct iovec *iovecs, size_t nr_iovecs,
                    int *send_fds, size_t send_fd_count,
                    struct vfio_user_header *hdr,
                    void *recv_data, size_t recv_len,
                    int *recv_fds, size_t *recv_fd_count)
{
    int ret = tran_sock_send_iovec(sock, msg_id, false, cmd, iovecs, nr_iovecs,
                                   send_fds, send_fd_count, 0);
    if (ret < 0) {
        return ret;
    }
    if (hdr == NULL) {
        hdr = alloca(sizeof(*hdr));
    }
    return tran_sock_recv_fds(sock, hdr, true, &msg_id, recv_data, &recv_len,
                              recv_fds, recv_fd_count);
}

int
tran_sock_msg_fds(int sock, uint16_t msg_id, enum vfio_user_command cmd,
                  void *send_data, size_t send_len,
                  struct vfio_user_header *hdr,
                  void *recv_data, size_t recv_len, int *recv_fds,
                  size_t *recv_fd_count)
{
    /* [0] is for the header. */
    struct iovec iovecs[2] = {
        [1] = {
            .iov_base = send_data,
            .iov_len = send_len
        }
    };
    return tran_sock_msg_iovec(sock, msg_id, cmd, iovecs, ARRAY_SIZE(iovecs),
                               NULL, 0, hdr, recv_data, recv_len, recv_fds,
                               recv_fd_count);
}

int
tran_sock_msg(int sock, uint16_t msg_id, enum vfio_user_command cmd,
              void *send_data, size_t send_len,
              struct vfio_user_header *hdr,
              void *recv_data, size_t recv_len)
{
    return tran_sock_msg_fds(sock, msg_id, cmd, send_data, send_len, hdr,
                             recv_data, recv_len, NULL, NULL);
}

static int
tran_sock_init(vfu_ctx_t *vfu_ctx)
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    tran_sock_t *ts = NULL;
    int ret;

    assert(vfu_ctx != NULL);

    ts = calloc(1, sizeof(tran_sock_t));

    if (ts == NULL) {
        return -1;
    }

    ts->listen_fd = -1;
    ts->conn_fd = -1;

    if ((ts->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        ret = errno;
        goto out;
    }

    if (vfu_ctx->flags & LIBVFIO_USER_FLAG_ATTACH_NB) {
        ret = fcntl(ts->listen_fd, F_SETFL,
                    fcntl(ts->listen_fd, F_GETFL, 0) | O_NONBLOCK);
        if (ret < 0) {
            ret = errno;
            goto out;
        }
    }

    ret = snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", vfu_ctx->uuid);
    if (ret >= (int)sizeof(addr.sun_path)) {
        ret = ENAMETOOLONG;
        goto out;
    } else if (ret < 0) {
        ret = EINVAL;
        goto out;
    }

    /* start listening for business */
    ret = bind(ts->listen_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        ret = errno;
        goto out;
    }

    ret = listen(ts->listen_fd, 0);
    if (ret < 0) {
        ret = errno;
    }

out:
    if (ret != 0) {
        if (ts != NULL && ts->listen_fd != -1) {
            close(ts->listen_fd);
        }
        free(ts);
        return ERROR_INT(ret);
    }

    vfu_ctx->tran_data = ts;
    return 0;
}

static int
tran_sock_get_poll_fd(vfu_ctx_t *vfu_ctx)
{
    tran_sock_t *ts = vfu_ctx->tran_data;

    if (ts->conn_fd != -1) {
        return ts->conn_fd;
    }

    return ts->listen_fd;
}

static int
tran_sock_attach(vfu_ctx_t *vfu_ctx)
{
    tran_sock_t *ts;
    int ret;

    assert(vfu_ctx != NULL);
    assert(vfu_ctx->tran_data != NULL);

    ts = vfu_ctx->tran_data;

    if (ts->conn_fd != -1) {
        vfu_log(vfu_ctx, LOG_ERR, "%s: already attached with fd=%d",
                __func__, ts->conn_fd);
        return ERROR_INT(EINVAL);
    }

    ts->conn_fd = accept(ts->listen_fd, NULL, NULL);
    if (ts->conn_fd == -1) {
        return -1;
    }

    ret = tran_negotiate(vfu_ctx);
    if (ret < 0) {
        ret = errno;
        close(ts->conn_fd);
        ts->conn_fd = -1;
        return ERROR_INT(ret);
    }

    return 0;
}

static int
tran_sock_get_request_header(vfu_ctx_t *vfu_ctx, struct vfio_user_header *hdr,
                             int *fds, size_t *nr_fds)
{
    tran_sock_t *ts;
    int sock_flags = 0;

    assert(vfu_ctx != NULL);
    assert(vfu_ctx->tran_data != NULL);

    ts = vfu_ctx->tran_data;

    if (ts->conn_fd == -1) {
        vfu_log(vfu_ctx, LOG_ERR, "%s: not connected", __func__);
        return ERROR_INT(ENOTCONN);
    }

    /*
     * TODO ideally we should set O_NONBLOCK on the fd so that the syscall is
     * faster (?). I tried that and get short reads, so we need to store the
     * partially received buffer somewhere and retry.
     */
    if (vfu_ctx->flags & LIBVFIO_USER_FLAG_ATTACH_NB) {
        sock_flags = MSG_DONTWAIT | MSG_WAITALL;
    }
    return get_msg(hdr, sizeof(*hdr), fds, nr_fds, ts->conn_fd, sock_flags);
}

static int
tran_sock_recv_body(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg)
{
    tran_sock_t *ts;
    int ret;

    assert(vfu_ctx != NULL);
    assert(vfu_ctx->tran_data != NULL);
    assert(msg != NULL);

    ts = vfu_ctx->tran_data;

    if (ts->conn_fd == -1) {
        vfu_log(vfu_ctx, LOG_ERR, "%s: not connected", __func__);
        return ERROR_INT(ENOTCONN);
    }

    assert(msg->in.iov.iov_len <= SERVER_MAX_MSG_SIZE);

    msg->in.iov.iov_base = malloc(msg->in.iov.iov_len);

    if (msg->in.iov.iov_base == NULL) {
        return -1;
    }

    ret = recv(ts->conn_fd, msg->in.iov.iov_base, msg->in.iov.iov_len, 0);

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
tran_sock_recv_msg(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg)
{
    tran_sock_t *ts;

    assert(vfu_ctx != NULL);
    assert(vfu_ctx->tran_data != NULL);
    assert(msg != NULL);

    ts = vfu_ctx->tran_data;

    if (ts->conn_fd == -1) {
        vfu_log(vfu_ctx, LOG_ERR, "%s: not connected", __func__);
        return ERROR_INT(ENOTCONN);
    }

    return tran_sock_recv_alloc(ts->conn_fd, &msg->hdr, false, NULL,
                                &msg->in.iov.iov_base, &msg->in.iov.iov_len);
}

static int
tran_sock_reply(vfu_ctx_t *vfu_ctx, vfu_msg_t *msg, int err)
{
    struct iovec *iovecs;
    size_t nr_iovecs;
    tran_sock_t *ts;
    int ret;

    assert(vfu_ctx != NULL);
    assert(vfu_ctx->tran_data != NULL);
    assert(msg != NULL);

    ts = vfu_ctx->tran_data;

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

    ret = tran_sock_send_iovec(ts->conn_fd, msg->hdr.msg_id, true, msg->hdr.cmd,
                               iovecs, nr_iovecs,
                               msg->out.fds, msg->out.nr_fds, err);

    free(iovecs);

    return ret;
}

static int
tran_sock_send_msg(vfu_ctx_t *vfu_ctx, uint16_t msg_id,
              enum vfio_user_command cmd,
              void *send_data, size_t send_len,
              struct vfio_user_header *hdr,
              void *recv_data, size_t recv_len)
{
    tran_sock_t *ts;

    assert(vfu_ctx != NULL);
    assert(vfu_ctx->tran_data != NULL);

    ts = vfu_ctx->tran_data;

    return tran_sock_msg(ts->conn_fd, msg_id, cmd, send_data, send_len,
                         hdr, recv_data, recv_len);
}

static void
tran_sock_detach(vfu_ctx_t *vfu_ctx)
{
    tran_sock_t *ts;

    assert(vfu_ctx != NULL);

    ts = vfu_ctx->tran_data;

    if (ts != NULL && ts->conn_fd != -1) {
        // FIXME: handle EINTR
        (void) close(ts->conn_fd);
        ts->conn_fd = -1;
    }
}

static void
tran_sock_fini(vfu_ctx_t *vfu_ctx)
{
    tran_sock_t *ts;

    assert(vfu_ctx != NULL);

    ts = vfu_ctx->tran_data;

    if (ts != NULL && ts->listen_fd != -1) {
        // FIXME: handle EINTR
        (void) close(ts->listen_fd);
        ts->listen_fd = -1;
    }

    free(vfu_ctx->tran_data);
    vfu_ctx->tran_data = NULL;
}

struct transport_ops tran_sock_ops = {
    .init = tran_sock_init,
    .get_poll_fd = tran_sock_get_poll_fd,
    .attach = tran_sock_attach,
    .get_request_header = tran_sock_get_request_header,
    .recv_body = tran_sock_recv_body,
    .reply = tran_sock_reply,
    .recv_msg = tran_sock_recv_msg,
    .send_msg = tran_sock_send_msg,
    .detach = tran_sock_detach,
    .fini = tran_sock_fini
};

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
