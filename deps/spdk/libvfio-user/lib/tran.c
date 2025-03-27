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
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <json.h>

#include "libvfio-user.h"
#include "migration.h"
#include "tran.h"

// FIXME: is this the value we want?
#define SERVER_MAX_FDS 8

/*
 * Expected JSON is of the form:
 *
 * {
 *     "capabilities": {
 *         "max_msg_fds": 32,
 *         "max_data_xfer_size": 1048576
 *         "migration": {
 *             "pgsize": 4096
 *         }
 *     }
 * }
 *
 * with everything being optional. Note that json_object_get_uint64() is only
 * available in newer library versions, so we don't use it.
 */
int
tran_parse_version_json(const char *json_str, int *client_max_fdsp,
                        size_t *client_max_data_xfer_sizep, size_t *pgsizep)
{
    struct json_object *jo_caps = NULL;
    struct json_object *jo_top = NULL;
    struct json_object *jo = NULL;
    int ret = EINVAL;

    if ((jo_top = json_tokener_parse(json_str)) == NULL) {
        goto out;
    }

    if (!json_object_object_get_ex(jo_top, "capabilities", &jo_caps)) {
        ret = 0;
        goto out;
    }

    if (json_object_get_type(jo_caps) != json_type_object) {
        goto out;
    }

    if (json_object_object_get_ex(jo_caps, "max_msg_fds", &jo)) {
        if (json_object_get_type(jo) != json_type_int) {
            goto out;
        }

        errno = 0;
        *client_max_fdsp = (int)json_object_get_int64(jo);

        if (errno != 0) {
            goto out;
        }
    }

    if (json_object_object_get_ex(jo_caps, "max_data_xfer_size", &jo)) {
        if (json_object_get_type(jo) != json_type_int) {
            goto out;
        }

        errno = 0;
        *client_max_data_xfer_sizep = (int)json_object_get_int64(jo);

        if (errno != 0) {
            goto out;
        }
    }
    if (json_object_object_get_ex(jo_caps, "migration", &jo)) {
        struct json_object *jo2 = NULL;

        if (json_object_get_type(jo) != json_type_object) {
            goto out;
        }

        if (json_object_object_get_ex(jo, "pgsize", &jo2)) {
            if (json_object_get_type(jo2) != json_type_int) {
                goto out;
            }

            errno = 0;
            *pgsizep = (size_t)json_object_get_int64(jo2);

            if (errno != 0) {
                goto out;
            }
        }
    }

    ret = 0;

out:
    /* We just need to put our top-level object. */
    json_object_put(jo_top);
    if (ret != 0) {
        return ERROR_INT(ret);
    }
    return 0;
}

static int
recv_version(vfu_ctx_t *vfu_ctx, uint16_t *msg_idp,
             struct vfio_user_version **versionp)
{
    struct vfio_user_version *cversion = NULL;
    vfu_msg_t msg = { { 0 } };
    int ret;

    *versionp = NULL;

    ret = vfu_ctx->tran->recv_msg(vfu_ctx, &msg);

    if (ret < 0) {
        vfu_log(vfu_ctx, LOG_ERR, "failed to receive version: %m");
        return ret;
    }

    *msg_idp = msg.hdr.msg_id;

    if (msg.hdr.cmd != VFIO_USER_VERSION) {
        vfu_log(vfu_ctx, LOG_ERR, "msg%#hx: invalid cmd %hu (expected %u)",
                *msg_idp, msg.hdr.cmd, VFIO_USER_VERSION);
        ret = EINVAL;
        goto out;
    }

    if (msg.in.nr_fds != 0) {
        vfu_log(vfu_ctx, LOG_ERR,
                "msg%#hx: VFIO_USER_VERSION: sent with %zu fds", *msg_idp,
                msg.in.nr_fds);
        ret = EINVAL;
        goto out;
    }

    if (msg.in.iov.iov_len < sizeof(*cversion)) {
        vfu_log(vfu_ctx, LOG_ERR,
                "msg%#hx: VFIO_USER_VERSION: invalid size %lu",
                *msg_idp, msg.in.iov.iov_len);
        ret = EINVAL;
        goto out;
    }

    cversion = msg.in.iov.iov_base;

    if (cversion->major != LIB_VFIO_USER_MAJOR) {
        vfu_log(vfu_ctx, LOG_ERR, "unsupported client major %hu (must be %u)",
                cversion->major, LIB_VFIO_USER_MAJOR);
        ret = EINVAL;
        goto out;
    }

    vfu_ctx->client_max_fds = 1;
    vfu_ctx->client_max_data_xfer_size = VFIO_USER_DEFAULT_MAX_DATA_XFER_SIZE;

    if (msg.in.iov.iov_len > sizeof(*cversion)) {
        const char *json_str = (const char *)cversion->data;
        size_t len = msg.in.iov.iov_len - sizeof(*cversion);
        size_t pgsize = 0;

        if (json_str[len - 1] != '\0') {
            vfu_log(vfu_ctx, LOG_ERR, "ignoring invalid JSON from client");
            ret = EINVAL;
            goto out;
        }

        ret = tran_parse_version_json(json_str, &vfu_ctx->client_max_fds,
                                      &vfu_ctx->client_max_data_xfer_size,
                                      &pgsize);

        if (ret < 0) {
            /* No client-supplied strings in the log for release build. */
#ifdef DEBUG
            vfu_log(vfu_ctx, LOG_ERR, "failed to parse client JSON \"%s\"",
                    json_str);
#else
            vfu_log(vfu_ctx, LOG_ERR, "failed to parse client JSON");
#endif
            ret = errno;
            goto out;
        }

        if (vfu_ctx->migration != NULL && pgsize != 0) {
            ret = migration_set_pgsize(vfu_ctx->migration, pgsize);

            if (ret != 0) {
                vfu_log(vfu_ctx, LOG_ERR, "refusing client page size of %zu",
                        pgsize);
                ret = errno;
                goto out;
            }
        }

        // FIXME: is the code resilient against ->client_max_fds == 0?
        if (vfu_ctx->client_max_fds < 0 ||
            vfu_ctx->client_max_fds > VFIO_USER_CLIENT_MAX_MSG_FDS_LIMIT) {
            vfu_log(vfu_ctx, LOG_ERR, "refusing client max_msg_fds of %d",
                    vfu_ctx->client_max_fds);
            ret = EINVAL;
            goto out;
        }
    }

out:
    if (ret != 0) {
        vfu_msg_t rmsg = { { 0 } };
        size_t i;

        rmsg.hdr = msg.hdr;

        (void) vfu_ctx->tran->reply(vfu_ctx, &rmsg, ret);

        for (i = 0; i < msg.in.nr_fds; i++) {
            if (msg.in.fds[i] != -1) {
                close(msg.in.fds[i]);
            }
        }

        free(msg.in.iov.iov_base);

        *versionp = NULL;
        return ERROR_INT(ret);
    }

    *versionp = cversion;
    return 0;
}

static int
send_version(vfu_ctx_t *vfu_ctx, uint16_t msg_id,
             struct vfio_user_version *cversion)
{
    struct vfio_user_version sversion = { 0 };
    struct iovec iovecs[2] = { { 0 } };
    char server_caps[1024];
    vfu_msg_t msg = { { 0 } };
    int slen;

    if (vfu_ctx->migration == NULL) {
        slen = snprintf(server_caps, sizeof(server_caps),
            "{"
                "\"capabilities\":{"
                    "\"max_msg_fds\":%u,"
                    "\"max_data_xfer_size\":%u"
                "}"
             "}", SERVER_MAX_FDS, SERVER_MAX_DATA_XFER_SIZE);
    } else {
        slen = snprintf(server_caps, sizeof(server_caps),
            "{"
                "\"capabilities\":{"
                    "\"max_msg_fds\":%u,"
                    "\"max_data_xfer_size\":%u,"
                    "\"migration\":{"
                        "\"pgsize\":%zu"
                    "}"
                "}"
             "}", SERVER_MAX_FDS, SERVER_MAX_DATA_XFER_SIZE,
                  migration_get_pgsize(vfu_ctx->migration));
    }

    // FIXME: we should save the client minor here, and check that before trying
    // to send unsupported things.
    sversion.major =  LIB_VFIO_USER_MAJOR;
    sversion.minor = MIN(cversion->minor, LIB_VFIO_USER_MINOR);

    iovecs[0].iov_base = &sversion;
    iovecs[0].iov_len = sizeof(sversion);
    iovecs[1].iov_base = server_caps;
    /* Include the NUL. */
    iovecs[1].iov_len = slen + 1;

    msg.hdr.cmd = VFIO_USER_VERSION;
    msg.hdr.msg_id = msg_id;
    msg.out_iovecs = iovecs;
    msg.nr_out_iovecs = 2;

    return vfu_ctx->tran->reply(vfu_ctx, &msg, 0);
}

int
tran_negotiate(vfu_ctx_t *vfu_ctx)
{
    struct vfio_user_version *client_version = NULL;
    uint16_t msg_id = 0x0bad;
    int ret;

    ret = recv_version(vfu_ctx, &msg_id, &client_version);

    if (ret < 0) {
        vfu_log(vfu_ctx, LOG_ERR, "failed to recv version: %m");
        return ret;
    }

    ret = send_version(vfu_ctx, msg_id, client_version);

    free(client_version);

    if (ret < 0) {
        vfu_log(vfu_ctx, LOG_ERR, "failed to send version: %m");
    }

    return ret;
}

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
