/**
* Copyright (c) NVIDIA CORPORATION & AFFILIATES, 2001-2019. ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef UCT_RC_MLX5_H
#define UCT_RC_MLX5_H

#include "rc_mlx5_common.h"

#include <uct/ib/rc/base/rc_iface.h>
#include <uct/ib/rc/base/rc_ep.h>
#include <uct/ib/mlx5/ib_mlx5.h>
#include <ucs/datastruct/queue.h>
#include <ucs/type/class.h>


#define UCT_RC_MLX5_CHECK_RES_PTR(_iface, _ep) \
    UCT_RC_CHECK_CQE_RET(&(_iface)->super, &(_ep)->super, \
                         UCS_STATUS_PTR(UCS_ERR_NO_RESOURCE)) \
    UCT_RC_CHECK_TXQP_RET(&(_iface)->super, &(_ep)->super, \
                          UCS_STATUS_PTR(UCS_ERR_NO_RESOURCE)) \
    UCT_RC_CHECK_NUM_RDMA_READ_RET(&(_iface)->super, \
                                   UCS_STATUS_PTR(UCS_ERR_NO_RESOURCE))


enum {
    /* EP address includes flush_rkey value */
    UCT_RC_MLX5_EP_ADDR_FLAG_FLUSH_RKEY = UCS_BIT(0)
};


/**
 * RC remote endpoint
 */
typedef struct uct_rc_mlx5_ep {
    uct_rc_ep_t              super;
    struct {
        uct_ib_mlx5_txwq_t   wq;
    } tx;
    uct_ib_mlx5_qp_t         tm_qp;
    uct_rc_mlx5_mp_context_t mp;
} uct_rc_mlx5_ep_t;


/**
 * RC MLX5 EP cleanup context
 */
typedef struct {
    uct_rc_iface_qp_cleanup_ctx_t super; /* Base class */
    uct_ib_mlx5_qp_t              qp; /* Main QP */
    uct_ib_mlx5_qp_t              tm_qp; /* TM Rendezvous QP */
    uct_ib_mlx5_mmio_reg_t        *reg; /* Doorbell register */
} uct_rc_mlx5_iface_qp_cleanup_ctx_t;


typedef struct uct_rc_mlx5_ep_address {
    uct_ib_uint24_t  qp_num;
    /* For RNDV TM enabling 2 QPs should be created, one is for sending WRs and
     * another one for HW (device will use it for RDMA reads and sending RNDV
     * Complete messages). */
    uct_ib_uint24_t  tm_qp_num;
    uint8_t          atomic_mr_id;
} UCS_S_PACKED uct_rc_mlx5_ep_address_t;


typedef struct uct_rc_mlx5_ep_ext_address {
    uct_rc_mlx5_ep_address_t super;
    uint8_t                  flags;
} UCS_S_PACKED uct_rc_mlx5_ep_ext_address_t;


UCS_CLASS_DECLARE(uct_rc_mlx5_ep_t, const uct_ep_params_t *);
UCS_CLASS_DECLARE_NEW_FUNC(uct_rc_mlx5_ep_t, uct_ep_t, const uct_ep_params_t *);
UCS_CLASS_DECLARE_DELETE_FUNC(uct_rc_mlx5_ep_t, uct_ep_t);

struct mlx5_cqe64 *
uct_rc_mlx5_iface_check_rx_completion(uct_ib_iface_t   *ib_iface,
                                      uct_ib_mlx5_cq_t *cq,
                                      struct mlx5_cqe64 *cqe, int poll_flags);

ucs_status_t uct_rc_mlx5_ep_put_short(uct_ep_h tl_ep, const void *buffer, unsigned length,
                                      uint64_t remote_addr, uct_rkey_t rkey);

ssize_t uct_rc_mlx5_ep_put_bcopy(uct_ep_h tl_ep, uct_pack_callback_t pack_cb,
                                 void *arg, uint64_t remote_addr, uct_rkey_t rkey);

ucs_status_t uct_rc_mlx5_ep_put_zcopy(uct_ep_h tl_ep, const uct_iov_t *iov, size_t iovcnt,
                                      uint64_t remote_addr, uct_rkey_t rkey,
                                      uct_completion_t *comp);

ucs_status_t uct_rc_mlx5_ep_get_bcopy(uct_ep_h tl_ep,
                                      uct_unpack_callback_t unpack_cb,
                                      void *arg, size_t length,
                                      uint64_t remote_addr, uct_rkey_t rkey,
                                      uct_completion_t *comp);

ucs_status_t uct_rc_mlx5_ep_get_zcopy(uct_ep_h tl_ep, const uct_iov_t *iov, size_t iovcnt,
                                      uint64_t remote_addr, uct_rkey_t rkey,
                                      uct_completion_t *comp);

ucs_status_t uct_rc_mlx5_ep_am_short(uct_ep_h tl_ep, uint8_t id, uint64_t header,
                                     const void *payload, unsigned length);

ucs_status_t uct_rc_mlx5_ep_am_short_iov(uct_ep_h tl_ep, uint8_t id,
                                         const uct_iov_t *iov, size_t iovcnt);

ssize_t uct_rc_mlx5_ep_am_bcopy(uct_ep_h tl_ep, uint8_t id,
                                uct_pack_callback_t pack_cb, void *arg,
                                unsigned flags);

ucs_status_t uct_rc_mlx5_ep_am_zcopy(uct_ep_h tl_ep, uint8_t id, const void *header,
                                     unsigned header_length, const uct_iov_t *iov,
                                     size_t iovcnt, unsigned flags,
                                     uct_completion_t *comp);

ucs_status_t uct_rc_mlx5_ep_atomic_cswap64(uct_ep_h tl_ep, uint64_t compare, uint64_t swap,
                                           uint64_t remote_addr, uct_rkey_t rkey,
                                           uint64_t *result, uct_completion_t *comp);

ucs_status_t uct_rc_mlx5_ep_atomic_cswap32(uct_ep_h tl_ep, uint32_t compare, uint32_t swap,
                                           uint64_t remote_addr, uct_rkey_t rkey,
                                           uint32_t *result, uct_completion_t *comp);

ucs_status_t uct_rc_mlx5_ep_atomic64_post(uct_ep_h ep, unsigned opcode, uint64_t value,
                                          uint64_t remote_addr, uct_rkey_t rkey);

ucs_status_t uct_rc_mlx5_ep_atomic32_post(uct_ep_h ep, unsigned opcode, uint32_t value,
                                          uint64_t remote_addr, uct_rkey_t rkey);

ucs_status_t uct_rc_mlx5_ep_atomic64_fetch(uct_ep_h ep, uct_atomic_op_t opcode,
                                           uint64_t value, uint64_t *result,
                                           uint64_t remote_addr, uct_rkey_t rkey,
                                           uct_completion_t *comp);

ucs_status_t uct_rc_mlx5_ep_atomic32_fetch(uct_ep_h ep, uct_atomic_op_t opcode,
                                           uint32_t value, uint32_t *result,
                                           uint64_t remote_addr, uct_rkey_t rkey,
                                           uct_completion_t *comp);

ucs_status_t uct_rc_mlx5_ep_fence(uct_ep_h tl_ep, unsigned flags);

void uct_rc_mlx5_ep_post_check(uct_ep_h tl_ep);

void uct_rc_mlx5_ep_vfs_populate(uct_rc_ep_t *rc_ep);

ucs_status_t uct_rc_mlx5_ep_flush(uct_ep_h tl_ep, unsigned flags, uct_completion_t *comp);

ucs_status_t uct_rc_mlx5_ep_invalidate(uct_ep_h tl_ep, unsigned flags);

ucs_status_t uct_rc_mlx5_ep_fc_ctrl(uct_ep_t *tl_ep, unsigned op,
                                    uct_rc_pending_req_t *req);

ucs_status_t uct_rc_mlx5_iface_create_qp(uct_rc_mlx5_iface_common_t *iface,
                                         uct_ib_mlx5_qp_t *qp,
                                         uct_ib_mlx5_txwq_t *txwq,
                                         uct_ib_mlx5_qp_attr_t *attr);

ucs_status_t
uct_rc_mlx5_ep_connect_qp(uct_rc_mlx5_iface_common_t *iface,
                          uct_ib_mlx5_qp_t *qp, uint32_t qp_num,
                          struct ibv_ah_attr *ah_attr, enum ibv_mtu path_mtu,
                          uint8_t path_index);

ucs_status_t
uct_rc_mlx5_ep_connect_to_ep_v2(uct_ep_h tl_ep,
                                const uct_device_addr_t *device_addr,
                                const uct_ep_addr_t *ep_addr,
                                const uct_ep_connect_to_ep_params_t *params);


ucs_status_t uct_rc_mlx5_ep_tag_eager_short(uct_ep_h tl_ep, uct_tag_t tag,
                                            const void *data, size_t length);

ssize_t uct_rc_mlx5_ep_tag_eager_bcopy(uct_ep_h tl_ep, uct_tag_t tag,
                                       uint64_t imm,
                                       uct_pack_callback_t pack_cb,
                                       void *arg, unsigned flags);

ucs_status_t uct_rc_mlx5_ep_tag_eager_zcopy(uct_ep_h tl_ep, uct_tag_t tag,
                                            uint64_t imm, const uct_iov_t *iov,
                                            size_t iovcnt, unsigned flags,
                                            uct_completion_t *comp);

ucs_status_ptr_t uct_rc_mlx5_ep_tag_rndv_zcopy(uct_ep_h tl_ep, uct_tag_t tag,
                                               const void *header,
                                               unsigned header_length,
                                               const uct_iov_t *iov,
                                               size_t iovcnt, unsigned flags,
                                               uct_completion_t *comp);

ucs_status_t uct_rc_mlx5_ep_tag_rndv_request(uct_ep_h tl_ep, uct_tag_t tag,
                                             const void* header,
                                             unsigned header_length,
                                             unsigned flags);

ucs_status_t uct_rc_mlx5_ep_get_address(uct_ep_h tl_ep, uct_ep_addr_t *addr);

unsigned uct_rc_mlx5_ep_cleanup_qp(void *arg);

ucs_status_t uct_rc_mlx5_iface_event_fd_get(uct_iface_h tl_iface, int *fd_p);

#endif
