/*
 * Copyright (c) 2014-2017, Cisco Systems, Inc. All rights reserved.
 *
 * LICENSE_BEGIN
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * LICENSE_END
 *
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sched.h>

#include <infiniband/verbs.h>

#include "kcompat.h"
#include "usnic_ib_abi.h"

#include "usnic_direct.h"
#include "usd.h"
#include "usd_ib_cmd.h"

int
usd_ib_cmd_get_context(struct usd_context *uctx)
{
    struct usnic_get_context cmd;
    struct usnic_get_context_resp resp;
    struct ib_uverbs_cmd_hdr *ich;
    struct ib_uverbs_get_context *icp;
    struct ib_uverbs_get_context_resp *irp;
    struct usnic_ib_get_context_cmd *ucp;
    struct usnic_ib_get_context_resp *urp;
    int n;

    /* clear cmd and response */
    memset(&cmd, 0, sizeof(cmd));
    memset(&resp, 0, sizeof(resp));

    /* fill in the command struct */
    ich = &cmd.ibv_cmd_hdr;
    ich->command = IB_USER_VERBS_CMD_GET_CONTEXT;
    ich->in_words = sizeof(cmd) / 4;
    ich->out_words = sizeof(resp) / 4;

    icp = &cmd.ibv_cmd;
    icp->response = (uintptr_t) & resp;

    ucp = &cmd.usnic_cmd;

/*
 *  Because usnic_verbs kernel module with USNIC_CTX_RESP_VERSION as 1
 *  silently returns success even it receives resp_version larger than 1,
 *  without filling in capbility information, here we still fill in
 *  command with resp_version as 1 in order to retrive cababiltiy information.
 *  Later when we decide to drop support for this version of kernel
 *  module, we should replace the next two lines of code with commented-out
 *  code below.
    ucp->resp_version = USNIC_CTX_RESP_VERSION;
    ucp->v2.encap_subcmd = 0;
    ucp->v2.num_caps = USNIC_CAP_CNT;
*/
    ucp->resp_version = 1;
    ucp->v1.num_caps = USNIC_CAP_CNT;

    n = write(uctx->ucx_ib_dev_fd, &cmd, sizeof(cmd));
    if (n != sizeof(cmd)) {
        return -errno;
    }

    irp = &resp.ibv_resp;
    uctx->event_fd = irp->async_fd;
    uctx->num_comp_vectors = irp->num_comp_vectors;

    urp = &resp.usnic_resp;

/*
 * Replace the code below with the commented-out line if dropping
 * support for kernel module with resp_version support as 1
    if (urp->resp_version == USNIC_CTX_RESP_VERSION) {
 */
    if (urp->resp_version == 1) {
        if (urp->num_caps > USNIC_CAP_CQ_SHARING &&
            urp->cap_info[USNIC_CAP_CQ_SHARING] > 0) {
            uctx->ucx_caps[USD_CAP_CQ_SHARING] = 1;
        }
        if (urp->num_caps > USNIC_CAP_MAP_PER_RES &&
            urp->cap_info[USNIC_CAP_MAP_PER_RES] > 0) {
            uctx->ucx_caps[USD_CAP_MAP_PER_RES] = 1;
        }
        if (urp->num_caps > USNIC_CAP_PIO &&
            urp->cap_info[USNIC_CAP_PIO] > 0) {
            uctx->ucx_caps[USD_CAP_PIO] = 1;
        }
        if (urp->num_caps > USNIC_CAP_CQ_INTR &&
            urp->cap_info[USNIC_CAP_CQ_INTR] > 0) {
            uctx->ucx_caps[USD_CAP_CQ_INTR] = 1;
        }
        if (urp->num_caps > USNIC_CAP_GRP_INTR &&
            urp->cap_info[USNIC_CAP_GRP_INTR] > 0) {
            uctx->ucx_caps[USD_CAP_GRP_INTR] = 1;
        }
    }

    return 0;
}

int
usd_ib_cmd_devcmd(
    struct usd_device *dev,
    enum vnic_devcmd_cmd devcmd,
    u64 *a0, u64 *a1, int wait)
{
    struct usnic_get_context cmd;
    struct usnic_get_context_resp resp;
    struct ib_uverbs_cmd_hdr *ich;
    struct ib_uverbs_get_context *icp;
    struct usnic_ib_get_context_cmd *ucp;
    struct usnic_ib_get_context_resp *urp;
    struct usnic_udevcmd_cmd udevcmd;
    struct usnic_udevcmd_resp udevcmd_resp;
    int n;

    if (dev->ud_ctx->ucmd_ib_dev_fd < 0)
        return -ENOENT;

    /* clear cmd and response */
    memset(&cmd, 0, sizeof(cmd));
    memset(&resp, 0, sizeof(resp));
    memset(&udevcmd, 0, sizeof(udevcmd));
    memset(&udevcmd_resp, 0, sizeof(udevcmd_resp));

    /* fill in the command struct */
    ich = &cmd.ibv_cmd_hdr;
    ich->command = IB_USER_VERBS_CMD_GET_CONTEXT;
    ich->in_words = sizeof(cmd) / 4;
    ich->out_words = sizeof(resp) / 4;

    icp = &cmd.ibv_cmd;
    icp->response = (uintptr_t) & resp;

    /* fill in usnic devcmd struct */
    udevcmd.vnic_idx = dev->ud_vf_list->vf_id;
    udevcmd.devcmd = devcmd;
    udevcmd.wait = wait;
    udevcmd.num_args = 2;
    udevcmd.args[0] = *a0;
    udevcmd.args[1] = *a1;

    ucp = &cmd.usnic_cmd;
    ucp->resp_version = USNIC_CTX_RESP_VERSION;
    ucp->v2.encap_subcmd = 1;
    ucp->v2.usnic_ucmd.ucmd = USNIC_USER_CMD_DEVCMD;
    ucp->v2.usnic_ucmd.inbuf = (uintptr_t) &udevcmd;
    ucp->v2.usnic_ucmd.inlen = (u32)sizeof(udevcmd);
    ucp->v2.usnic_ucmd.outbuf = (uintptr_t) &udevcmd_resp;
    ucp->v2.usnic_ucmd.outlen = (u32)sizeof(udevcmd_resp);

    n = write(dev->ud_ctx->ucmd_ib_dev_fd, &cmd, sizeof(cmd));
    urp = &resp.usnic_resp;
    /*
     * If returns success, it's an old kernel who does not understand
     * version 2 command, then we need to close the command FD to
     * release the created ucontext object
     */
    if (n == sizeof(cmd)) {
        usd_err(
            "The running usnic_verbs kernel module does not support "
            "encapsulating devcmd through IB GET_CONTEXT command\n");
        close(dev->ud_ctx->ucmd_ib_dev_fd);
        dev->ud_ctx->ucmd_ib_dev_fd = -1;
        return -ENOTSUP;
    } else if (errno != ECHILD) {
        return -errno;
    } else if (urp->resp_version != USNIC_CTX_RESP_VERSION) {
        /* Kernel needs to make sure it returns response with a format
         * understandable by the library. */
        usd_err(
            "The returned resp version does not match with requested\n");
        return -ENOTSUP;
    }

    *a0 = udevcmd_resp.args[0];
    *a1 = udevcmd_resp.args[1];

    return 0;
}

/*
 * Issue IB DEALLOC_PD command to alloc a PD in kernel
 */
static int
_usd_ib_cmd_dealloc_pd(
    struct usd_device *dev,
    uint32_t pd_handle)
{
    struct usnic_dealloc_pd cmd;
    struct ib_uverbs_cmd_hdr *ich;
    struct ib_uverbs_dealloc_pd *icp;
    int n;

    memset(&cmd, 0, sizeof(cmd));

    ich = &cmd.ibv_cmd_hdr;
    ich->command = IB_USER_VERBS_CMD_DEALLOC_PD;
    ich->in_words = sizeof(cmd) / 4;
    ich->out_words = 0;

    icp = &cmd.ibv_cmd;
    icp->pd_handle = pd_handle;

    n = write(dev->ud_ctx->ucx_ib_dev_fd, &cmd, sizeof(cmd));
    if (n != sizeof(cmd)) {
        return -errno;
    }

    return 0;
}

/*
 * Issue IB ALLOC_PD command to alloc a PD in kernel
 */
static int
_usd_ib_cmd_alloc_pd(
    struct usd_device *dev,
    uint32_t *handle_o,
    uint32_t *vfid,
    uint32_t *grp_vect_buf_len)
{
    struct usnic_alloc_pd cmd;
    struct usnic_alloc_pd_resp resp;
    struct ib_uverbs_cmd_hdr *ich;
    struct ib_uverbs_alloc_pd *icp;
    struct usnic_ib_alloc_pd_cmd *ucp;
    struct ib_uverbs_alloc_pd_resp *irp;
    struct usnic_ib_alloc_pd_resp *urp;
    int n;

    memset(&cmd, 0, sizeof(cmd));
    memset(&resp, 0, sizeof(resp));

    /* fill in command */
    ich = &cmd.ibv_cmd_hdr;
    ich->command = IB_USER_VERBS_CMD_ALLOC_PD;
    ich->in_words = sizeof(cmd) / 4;
    ich->out_words = sizeof(resp) / 4;

    icp = &cmd.ibv_cmd;
    icp->response = (uintptr_t) & resp;

    /*
     * Only need to get group vector size and vf information
     * if group interrupt is enabled
     */
    if (dev->ud_ctx->ucx_caps[USD_CAP_GRP_INTR] > 0) {
        ucp = &cmd.usnic_cmd;
        ucp->resp_version = USNIC_IB_ALLOC_PD_VERSION;
    }

    n = write(dev->ud_ctx->ucx_ib_dev_fd, &cmd, sizeof(cmd));
    if (n != sizeof(cmd)) {
        return -errno;
    }

    /* process response */
    irp = &resp.ibv_resp;
    *handle_o = irp->pd_handle;
    urp = &resp.usnic_resp;
    if (urp->resp_version >= 1) {
        *vfid = urp->cur.vfid;
        *grp_vect_buf_len = urp->cur.grp_vect_buf_len;
    }

    return 0;
}

/*
 * Create a protection domain
 */
int
usd_ib_cmd_alloc_pd(
    struct usd_device *dev,
    uint32_t *handle_o)
{
    uint32_t vfid = 0;
    uint32_t grp_vect_buf_len = 0;
    int err;

    /* Issue IB alloc_pd command, get assigned VF id and group vector size */
    err = _usd_ib_cmd_alloc_pd(dev, handle_o, &vfid, &grp_vect_buf_len);
    if (err) {
        return err;
    }

    /* MAP group vector address to userspace
     * Kernel module then maps group vector user address to IOMMU and
     * program VIC HW register
     */
    if (dev->ud_ctx->ucx_caps[USD_CAP_GRP_INTR] > 0) {
        void *va;
        off64_t offset;

        offset = USNIC_ENCODE_PGOFF(vfid, USNIC_MMAP_GRPVECT, 0);
        va = mmap64(NULL, grp_vect_buf_len, PROT_READ + PROT_WRITE,
                    MAP_SHARED, dev->ud_ctx->ucx_ib_dev_fd, offset);

        if (va == MAP_FAILED) {
            usd_err("Failed to map group vector for vf %u, grp_vect_size %u, "
                    "error %d\n",
                    vfid, grp_vect_buf_len, errno);
            _usd_ib_cmd_dealloc_pd(dev, *handle_o);
            return -errno;
        }

        dev->grp_vect_map.va = va;
        dev->grp_vect_map.len = grp_vect_buf_len;
        dev->grp_vect_map.vfid = vfid;
    }

    return 0;
}

int
usd_ib_cmd_reg_mr(
    struct usd_device *dev,
    void *vaddr,
    size_t length,
    struct usd_mr *mr)
{
    struct usnic_reg_mr cmd;
    struct usnic_reg_mr_resp resp;
    struct ib_uverbs_cmd_hdr *ich;
    struct ib_uverbs_reg_mr *icp;
    struct ib_uverbs_reg_mr_resp *irp;
    int n;

    memset(&cmd, 0, sizeof(cmd));
    memset(&resp, 0, sizeof(resp));

    ich = &cmd.ibv_cmd_hdr;
    ich->command = IB_USER_VERBS_CMD_REG_MR;
    ich->in_words = sizeof(cmd) / 4;
    ich->out_words = sizeof(resp) / 4;

    icp = &cmd.ibv_cmd;
    icp->response = (uintptr_t) & resp;
    icp->start = (uintptr_t) vaddr;
    icp->length = length;
    icp->hca_va = (uintptr_t) vaddr;
    icp->pd_handle = dev->ud_pd_handle;
    icp->access_flags = IBV_ACCESS_LOCAL_WRITE;

    /* Issue command to IB driver */
    n = write(dev->ud_ctx->ucx_ib_dev_fd, &cmd, sizeof(cmd));
    if (n != sizeof(cmd)) {
        return errno;
    }

    /* process response */
    irp = &resp.ibv_resp;
    mr->umr_handle = irp->mr_handle;
    mr->umr_lkey = irp->lkey;
    mr->umr_rkey = irp->rkey;

    return 0;
}

int
usd_ib_cmd_dereg_mr(
    struct usd_device *dev,
    struct usd_mr *mr)
{
    struct usnic_dereg_mr cmd;
    struct ib_uverbs_cmd_hdr *ich;
    struct ib_uverbs_dereg_mr *icp;
    int n;

    memset(&cmd, 0, sizeof(cmd));

    ich = &cmd.ibv_cmd_hdr;
    ich->command = IB_USER_VERBS_CMD_DEREG_MR;
    ich->in_words = sizeof(cmd) / 4;
    ich->out_words = 0;

    icp = &cmd.ibv_cmd;
    icp->mr_handle = mr->umr_handle;

    /* Issue command to IB driver */
    n = write(dev->ud_ctx->ucx_ib_dev_fd, &cmd, sizeof(cmd));
    if (n != sizeof(cmd)) {
        return -errno;
    }

    return 0;
}

/*
 * Make the verbs call to create a CQ
 */
int
usd_ib_cmd_create_cq(
    struct usd_device *dev,
    struct usd_cq_impl *cq,
    void *ibv_cq,
    int comp_channel,
    int comp_vector)
{
    struct usnic_create_cq cmd;
    struct usnic_create_cq_resp resp;
    struct ib_uverbs_cmd_hdr *ich;
    struct ib_uverbs_create_cq *icp;
    struct ib_uverbs_create_cq_resp *irp;
    cpu_set_t *affinity_mask = NULL;
    int flags = 0;
    int n;

    memset(&cmd, 0, sizeof(cmd));
    memset(&resp, 0, sizeof(resp));

    ich = &cmd.ibv_cmd_hdr;
    ich->command = IB_USER_VERBS_CMD_CREATE_CQ;
    ich->in_words = sizeof(cmd) / 4;
    ich->out_words = sizeof(resp) / 4;

    icp = &cmd.ibv_cmd;
    icp->response = (uintptr_t) & resp;

    if (ibv_cq == NULL) {
        icp->user_handle = (uintptr_t) cq;
    } else {
        icp->user_handle = (uintptr_t) ibv_cq;  /* Pass real verbs cq pointer to kernel
                                                 * to make ibv_get_cq_event happy */
        flags |= USNIC_CQ_COMP_SIGNAL_VERBS;
    }
    icp->cqe = cq->ucq_num_entries;
    icp->comp_channel = comp_channel;
    icp->comp_vector = comp_vector;

    if (comp_channel != -1) {
        if (dev->ud_ctx->ucx_caps[USD_CAP_GRP_INTR] != 1) {
            usd_err("usd_create_cq failed. No interrupt support\n");
            return -ENOTSUP;
        }
        cmd.usnic_cmd.resp_version = USNIC_IB_CREATE_CQ_VERSION;
        cmd.usnic_cmd.cur.flags = flags;
        cmd.usnic_cmd.cur.comp_event_fd = comp_channel;
        if ((affinity_mask = CPU_ALLOC(sysconf(_SC_NPROCESSORS_ONLN)))
                != NULL &&
            sched_getaffinity(getpid(),
                        CPU_ALLOC_SIZE(sysconf(_SC_NPROCESSORS_ONLN)),
                        affinity_mask) == 0) {
            cmd.usnic_cmd.cur.affinity_mask_ptr = (u64)affinity_mask;
            cmd.usnic_cmd.cur.affinity_mask_len =
                            CPU_ALLOC_SIZE(sysconf(_SC_NPROCESSORS_ONLN));
        } else {
            cmd.usnic_cmd.cur.affinity_mask_ptr = (u64)NULL;
            cmd.usnic_cmd.cur.affinity_mask_len = 0;
        }
    } else {
        /*
         * If appliation does not request cq completion event support,
         * send command with version 0 to allow compatibility with
         * old kernel library
         */
        cmd.usnic_cmd.resp_version = 0;
    }

    /* Issue command to IB driver */
    n = write(dev->ud_ctx->ucx_ib_dev_fd, &cmd, sizeof(cmd));
    if (n != sizeof(cmd)) {
        return -errno;
    }

    /* process response */
    irp = &resp.ibv_resp;
    cq->ucq_handle = irp->cq_handle;

    if (affinity_mask != NULL)
        CPU_FREE(affinity_mask);

    return 0;
}

/*
 * Make the verbs call to destroy a CQ
 */
int
usd_ib_cmd_destroy_cq(
    struct usd_device *dev,
    struct usd_cq_impl *cq)
{
    struct usnic_destroy_cq cmd;
    struct ib_uverbs_cmd_hdr *ich;
    struct ib_uverbs_destroy_cq *icp;
    int n;

    memset(&cmd, 0, sizeof(cmd));

    ich = &cmd.ibv_cmd_hdr;
    ich->command = IB_USER_VERBS_CMD_DESTROY_CQ;
    ich->in_words = sizeof(cmd) / 4;
    ich->out_words = 0;

    icp = &cmd.ibv_cmd;
    icp->cq_handle = cq->ucq_handle;

    /* Issue command to IB driver */
    n = write(dev->ud_ctx->ucx_ib_dev_fd, &cmd, sizeof(cmd));
    if (n != sizeof(cmd)) {
        return -errno;
    }

    return 0;
}

/*
 * Create a verbs QP without attaching any real resources to it yet
 */
int
usd_ib_cmd_create_qp(
    struct usd_device *dev,
    struct usd_qp_impl *qp,
    struct usd_vf_info *vfip)
{
    struct usnic_create_qp cmd;
    struct usnic_create_qp_resp *resp;
    struct ib_uverbs_cmd_hdr *ich;
    struct ib_uverbs_create_qp *icp;
    struct ib_uverbs_create_qp_resp *irp = NULL;
    struct usnic_ib_create_qp_cmd *ucp;
    struct usnic_ib_create_qp_resp *urp;
    struct usd_qp_filter *qfilt;
    int ret;
    int n;
    uint32_t i;
    struct usnic_vnic_barres_info *resources;

    ucp = NULL;
    resources = NULL;
    irp = NULL;
    memset(&cmd, 0, sizeof(cmd));

    resp = calloc(1, sizeof(*resp));
    if (resp == NULL) {
        usd_err("Failed to allocate memory for create_qp_resp\n");
        return -ENOMEM;
    }

    ich = &cmd.ibv_cmd_hdr;
    ich->command = IB_USER_VERBS_CMD_CREATE_QP;
    ich->in_words = sizeof(cmd) / 4;
    ich->out_words = sizeof(*resp) / 4;

    icp = &cmd.ibv_cmd;
    icp->response = (uintptr_t) resp;
    icp->user_handle = (uintptr_t) qp;
    icp->pd_handle = dev->ud_pd_handle;
    icp->send_cq_handle = qp->uq_wq.uwq_cq->ucq_handle;
    icp->recv_cq_handle = qp->uq_rq.urq_cq->ucq_handle;
    icp->srq_handle = 0;
    icp->max_send_wr = qp->uq_wq.uwq_num_entries;
    icp->max_recv_wr = qp->uq_rq.urq_num_entries;
    icp->max_send_sge = 1;
    icp->max_recv_sge = 1;
    icp->max_inline_data = 1024;
    icp->sq_sig_all = 0;
    icp->qp_type = IBV_QPT_UD;
    icp->is_srq = 0;
    icp->reserved = 0;

    ucp = &cmd.usnic_cmd;

    if (dev->ud_ctx->ucx_caps[USD_CAP_GRP_INTR]) {
        ucp->cmd_version = 2;
    } else {
            /*
             * Allow compatibility with old kernel module when
             * application does not require cq completion notification
             */
            ucp->cmd_version = 1;
    }

    qfilt = &qp->uq_filter;
    if (qfilt->qf_type == USD_FTY_UDP ||
            qfilt->qf_type == USD_FTY_UDP_SOCK) {
        /*
         * Command versions 0,1,2 need to fill in the spec_v2 struct.
         * Newer versions need to fill in the spec struct.
         */
        if (ucp->cmd_version <= 2) {
            ucp->spec_v2.trans_type = USNIC_TRANSPORT_IPV4_UDP;
            ucp->spec_v2.ip.sock_fd = qfilt->qf_filter.qf_udp.u_sockfd;
        } else {
            ucp->spec.trans_type = USNIC_TRANSPORT_IPV4_UDP;
            ucp->spec.ip.sock_fd = qfilt->qf_filter.qf_udp.u_sockfd;
        }
    } else {
        ret = -EINVAL;
        goto out;
    }

    ucp->u.cur.resources_len = RES_TYPE_MAX * sizeof(*resources);
    resources = calloc(RES_TYPE_MAX, sizeof(*resources));
    if (resources == NULL) {
        usd_err("unable to allocate resources array\n");
        ret = -ENOMEM;
        goto out;
    }
    ucp->u.cur.resources = (u64)(uintptr_t)resources;

    /* Issue command to IB driver */
    n = write(dev->ud_ctx->ucx_ib_dev_fd, &cmd, sizeof(cmd));
    if (n != sizeof(cmd)) {
        ret = -errno;
        goto out;
    }

    /* process IB part of response */
    irp = &resp->ibv_resp;
    qp->uq_qp_handle = irp->qp_handle;
    qp->uq_qp_num = irp->qpn;

    /* process usnic part response */
    urp = &resp->usnic_resp;

    qp->uq_rq.urq_index = urp->rq_idx[0];
    qp->uq_wq.uwq_index = urp->wq_idx[0];

    qp->uq_rq.urq_cq->ucq_index = urp->cq_idx[0];
    if (qp->uq_rq.urq_cq != qp->uq_wq.uwq_cq) {
        qp->uq_wq.uwq_cq->ucq_index = urp->cq_idx[1];
    }

    /* Pull VF info */
    vfip->vi_vfid = urp->vfid;
    vfip->vi_bar_bus_addr = urp->bar_bus_addr;
    vfip->vi_bar_len = urp->bar_len;

    if (urp->cmd_version == ucp->cmd_version) {
        /* got expected version */
        if (dev->ud_ctx->ucx_caps[USD_CAP_MAP_PER_RES] > 0) {
            for (i = 0; i < MIN(RES_TYPE_MAX, urp->u.cur.num_barres); i++) {
                enum vnic_res_type type = resources[i].type;
                if (type < RES_TYPE_MAX) {
                    vfip->barres[type].type = type;
                    vfip->barres[type].bus_addr = resources[i].bus_addr;
                    vfip->barres[type].len = resources[i].len;
                }
            }
            if (vfip->barres[RES_TYPE_WQ].bus_addr == 0) {
                    usd_err("Failed to retrieve WQ res info\n");
                    ret = -ENXIO;
                    goto out;
            }
            if (vfip->barres[RES_TYPE_RQ].bus_addr == 0) {
                    usd_err("Failed to retrieve RQ res info\n");
                    ret = -ENXIO;
                    goto out;
            }
            if (vfip->barres[RES_TYPE_CQ].bus_addr == 0) {
                    usd_err("Failed to retrieve CQ res info\n");
                    ret = -ENXIO;
                    goto out;
            }
            if (vfip->barres[RES_TYPE_INTR_CTRL].bus_addr == 0) {
                    usd_err("Failed to retrieve INTR res info\n");
                    ret = -ENXIO;
                    goto out;
            }
            if (vfip->barres[RES_TYPE_DEVCMD].bus_addr == 0) {
                    usd_err("Failed to retrieve DEVCMD res info\n");
                    ret = -ENXIO;
                    goto out;
            }
        }
    } else if (urp->cmd_version == 0) {
        /* special case, old kernel that won't tell us about individual barres
         * info but should otherwise work fine */

        if (dev->ud_ctx->ucx_caps[USD_CAP_MAP_PER_RES] != 0) {
            /* should not happen, only the presence of never-released kernel
             * code should cause this case */
            usd_err("USD_CAP_MAP_PER_RES claimed but qp_create cmd_version == 0\n");
            ret = -ENXIO;
            goto out;
        }
    }  else {
        usd_err("unexpected cmd_version (%u)\n", urp->cmd_version);
        ret = -ENXIO;
        goto out;
    }

    /* version 2 and beyond has interrupt support */
    if (urp->cmd_version > 1) {
        qp->uq_rq.urq_cq->intr_offset = urp->u.cur.rcq_intr_offset;
        if (qp->uq_rq.urq_cq != qp->uq_wq.uwq_cq) {
            qp->uq_wq.uwq_cq->intr_offset = urp->u.cur.wcq_intr_offset;
        }
        vfip->vi_barhead_len = urp->u.cur.barhead_len;
    }

    free(resources);
    free(resp);
    return 0;

  out:
    if (irp != NULL)                   /* indicates successful IB create QP */
        usd_ib_cmd_destroy_qp(dev, qp);
    free(resources);
    free(resp);
    return ret;
}

int
usd_ib_cmd_modify_qp(
    struct usd_device *dev,
    struct usd_qp_impl *qp,
    int state)
{
    struct usnic_modify_qp cmd;
    struct ib_uverbs_cmd_hdr *ich;
    struct ib_uverbs_modify_qp *icp;
    int n;

    memset(&cmd, 0, sizeof(cmd));

    ich = &cmd.ibv_cmd_hdr;
    ich->command = IB_USER_VERBS_CMD_MODIFY_QP;
    ich->in_words = sizeof(cmd) / 4;
    ich->out_words = 0;

    icp = &cmd.ibv_cmd;
    icp->qp_handle = qp->uq_qp_handle;
    icp->attr_mask = IBV_QP_STATE;
    icp->qp_state = state;

    /* Issue command to IB driver */
    n = write(dev->ud_ctx->ucx_ib_dev_fd, &cmd, sizeof(cmd));
    if (n != sizeof(cmd)) {
        return -errno;
    }

    return 0;
}

int
usd_ib_cmd_destroy_qp(
    struct usd_device *dev,
    struct usd_qp_impl *qp)
{
    struct usnic_destroy_qp cmd;
    struct ib_uverbs_destroy_qp_resp resp;
    struct ib_uverbs_cmd_hdr *ich;
    struct ib_uverbs_destroy_qp *icp;
    int n;

    memset(&cmd, 0, sizeof(cmd));

    ich = &cmd.ibv_cmd_hdr;
    ich->command = IB_USER_VERBS_CMD_DESTROY_QP;
    ich->in_words = sizeof(cmd) / 4;
    ich->out_words = sizeof(resp) / 4;

    icp = &cmd.ibv_cmd;
    icp->response = (uintptr_t) & resp;
    icp->qp_handle = qp->uq_qp_handle;

    /* Issue command to IB driver */
    n = write(dev->ud_ctx->ucx_ib_dev_fd, &cmd, sizeof(cmd));
    if (n != sizeof(cmd)) {
        return -errno;
    }

    return 0;
}

static int
usd_ib_cmd_query_device(
    struct usd_device *dev,
    struct ib_uverbs_query_device_resp *irp)
{
    struct usnic_query_device cmd;
    struct ib_uverbs_cmd_hdr *ich;
    struct ib_uverbs_query_device *icp;
    int n;

    memset(&cmd, 0, sizeof(cmd));

    ich = &cmd.ibv_cmd_hdr;
    ich->command = IB_USER_VERBS_CMD_QUERY_DEVICE;
    ich->in_words = sizeof(cmd) / 4;
    ich->out_words = sizeof(*irp) / 4;

    icp = &cmd.ibv_cmd;
    icp->response = (uintptr_t) irp;

    /* keep Valgrind happy */
    memset(irp, 0x00, sizeof(*irp));

    /* Issue command to IB driver */
    n = write(dev->ud_ctx->ucx_ib_dev_fd, &cmd, sizeof(cmd));
    if (n != sizeof(cmd)) {
        return -errno;
    }

    return 0;
}

static int
usd_ib_cmd_query_port(
    struct usd_device *dev,
    struct ib_uverbs_query_port_resp *irp)
{
    struct usnic_query_port cmd;
    struct ib_uverbs_cmd_hdr *ich;
    struct ib_uverbs_query_port *icp;
    int n;

    memset(&cmd, 0, sizeof(cmd));

    ich = &cmd.ibv_cmd_hdr;
    ich->command = IB_USER_VERBS_CMD_QUERY_PORT;
    ich->in_words = sizeof(cmd) / 4;
    ich->out_words = sizeof(*irp) / 4;

    icp = &cmd.ibv_cmd;
    icp->response = (uintptr_t) irp;
    icp->port_num = 1;

    /* keep Valgrind happy */
    memset(irp, 0x00, sizeof(*irp));

    /* Issue command to IB driver */
    n = write(dev->ud_ctx->ucx_ib_dev_fd, &cmd, sizeof(cmd));
    if (n != sizeof(cmd)) {
        return -errno;
    }

    return 0;
}

/*
 * For code readability, copy these two enums from kernel
 * /usr/include/rdma/ib_verbs.h (otherwise, we'd would have to
 * hard-code the integer values below).
 */
enum ib_port_width {
    IB_WIDTH_1X  = 1,
    IB_WIDTH_4X  = 2,
    IB_WIDTH_8X  = 4,
    IB_WIDTH_12X = 8
};

enum ib_port_speed {
    IB_SPEED_SDR   = 1,  // 2.5 Gbps
    IB_SPEED_DDR   = 2,  // 5 Gbps
    IB_SPEED_QDR   = 4,  // 10 Gbps
    IB_SPEED_FDR10 = 8,  // 10.3125 Gbps
    IB_SPEED_FDR   = 16, // 14.0625 Gbps
    IB_SPEED_EDR   = 32, // 25.78125 Gbps
    IB_SPEED_HDR   = 64  // 50 Gbps
};


/*
 * Issue query commands for device and port and interpret the resaults
 */
int
usd_ib_query_dev(
    struct usd_device *dev)
{
    struct ib_uverbs_query_device_resp dresp;
    struct ib_uverbs_query_port_resp presp;
    struct usd_device_attrs *dap;
    unsigned speed;
    int ret;

    ret = usd_ib_cmd_query_device(dev, &dresp);
    if (ret != 0)
        return ret;

    ret = usd_ib_cmd_query_port(dev, &presp);
    if (ret != 0)
        return ret;

    /* copy out the attributes we care about */
    dap = &dev->ud_attrs;

    dap->uda_link_state =
        (presp.state == 4) ? USD_LINK_UP : USD_LINK_DOWN;

    /*
     * If link is up, derive bandwidth from speed and width.
     * If link is down, driver reports bad speed, try to deduce from the
     * NIC device ID.
     */
    if (dap->uda_link_state == USD_LINK_UP) {
#define MKSW(S,W) (((S)<<8)|(W))
        speed = MKSW(presp.active_speed, presp.active_width);
        switch (speed) {
        case MKSW(IB_SPEED_FDR10, IB_WIDTH_1X):
        case MKSW(IB_SPEED_DDR, IB_WIDTH_4X):
            dap->uda_bandwidth = 10000;
            break;
        case MKSW(IB_SPEED_QDR, IB_WIDTH_4X):
            dap->uda_bandwidth = 25000;
            break;
        case MKSW(IB_SPEED_FDR10, IB_WIDTH_4X):
            dap->uda_bandwidth = 40000;
            break;
        case MKSW(IB_SPEED_HDR, IB_WIDTH_1X):
            dap->uda_bandwidth = 50000;
            break;
        case MKSW(IB_SPEED_EDR, IB_WIDTH_4X):
            dap->uda_bandwidth = 100000;
            break;
        case MKSW(IB_SPEED_HDR, IB_WIDTH_4X):
            dap->uda_bandwidth = 200000;
            break;
        case MKSW(IB_SPEED_HDR, IB_WIDTH_8X):
            dap->uda_bandwidth = 400000;
            break;
        default:
            printf("Warning: unrecognized speed/width %d/%d, defaulting to 10G\n",
                   presp.active_speed, presp.active_width);
            dap->uda_bandwidth = 10000;
            break;
        }
    } else {
        /* from pci_ids.h */
        switch (dap->uda_device_id) {
        case 0x4f: /* Vasona */
        case 0x84: /* Cotati */
        case 0x85: /* Lexington */
        case 0x12c: /* Calistoga */
        case 0x137: /* Mountain View */
        case 0x138: /* Walnut Creek */
            dap->uda_bandwidth = 10000;
            break;
        case 0xcd:  /* icehouse */
        case 0x14d: /* clearlake */
            dap->uda_bandwidth = 40000;
            break;
        default:
            dap->uda_bandwidth = 0;
        }
    }

    dap->uda_vendor_id = dresp.vendor_id;
    dap->uda_vendor_part_id = dresp.vendor_part_id;
    dap->uda_device_id = dresp.hw_ver;

    dap->uda_max_qp = dresp.max_qp;
    dap->uda_max_cq = dresp.max_cq;

    return 0;
}


int
usd_ib_cmd_create_comp_channel(
    struct usd_device *dev,
    int *comp_fd_o)
{
    int n;
    struct usnic_create_comp_channel cmd;
    struct ib_uverbs_create_comp_channel_resp resp;
    struct ib_uverbs_cmd_hdr *ich;
    struct ib_uverbs_create_comp_channel *icp;
    struct ib_uverbs_create_comp_channel_resp *irp;

    memset(&cmd, 0, sizeof(cmd));

    ich = &cmd.ibv_cmd_hdr;
    ich->command = IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL;
    ich->in_words = sizeof(cmd) / 4;
    ich->out_words = sizeof(resp) / 4;

    icp = &cmd.ibv_cmd;
    icp->response = (uintptr_t) & resp;

    /* Issue command to IB driver */
    n = write(dev->ud_ctx->ucx_ib_dev_fd, &cmd, sizeof(cmd));
    if (n != sizeof(cmd)) {
        return -errno;
    }

    irp = &resp;
    *comp_fd_o = irp->fd;

    return 0;
}
