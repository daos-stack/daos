/* Test double only: never put this directory on a production include path.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef CRT_TEST_VERBS_H
#define CRT_TEST_VERBS_H
#include <stddef.h>
#include <stdint.h>

struct ibv_device {
	int unused;
};
struct ibv_context {
	int unused;
};
struct ibv_pd {
	int unused;
};
struct ibv_cq {
	int unused;
};
struct ibv_qp {
	uint32_t qp_num;
};
struct ibv_mr {
	void    *addr;
	size_t   length;
	uint32_t lkey, rkey;
};
union ibv_gid {
	uint8_t raw[16];
};
enum ibv_mtu { IBV_MTU_256 = 1, IBV_MTU_512, IBV_MTU_1024, IBV_MTU_2048, IBV_MTU_4096 };
enum {
	IBV_PORT_ACTIVE         = 4,
	IBV_LINK_LAYER_ETHERNET = 2,
	IBV_QPT_RC              = 2,
	IBV_QPS_INIT            = 1,
	IBV_QPS_RTR,
	IBV_QPS_RTS,
	IBV_QPS_ERR               = 6,
	IBV_ACCESS_LOCAL_WRITE    = 1,
	IBV_ACCESS_REMOTE_READ    = 2,
	IBV_ACCESS_REMOTE_WRITE   = 4,
	IBV_QP_STATE              = 1,
	IBV_QP_PORT               = 2,
	IBV_QP_PKEY_INDEX         = 4,
	IBV_QP_ACCESS_FLAGS       = 8,
	IBV_QP_AV                 = 16,
	IBV_QP_PATH_MTU           = 32,
	IBV_QP_DEST_QPN           = 64,
	IBV_QP_RQ_PSN             = 128,
	IBV_QP_MAX_DEST_RD_ATOMIC = 256,
	IBV_QP_MIN_RNR_TIMER      = 512,
	IBV_QP_SQ_PSN             = 1024,
	IBV_QP_TIMEOUT            = 2048,
	IBV_QP_RETRY_CNT          = 4096,
	IBV_QP_RNR_RETRY          = 8192,
	IBV_QP_MAX_QP_RD_ATOMIC   = 16384,
	IBV_WR_SEND               = 0,
	IBV_WR_RDMA_READ          = 1,
	IBV_WR_RDMA_WRITE         = 2,
	IBV_SEND_SIGNALED         = 2,
	IBV_WC_SUCCESS            = 0,
	IBV_WC_WR_FLUSH_ERR       = 5
};
struct ibv_device_attr {
	int max_qp_rd_atom, max_qp_init_rd_atom;
};
int
ibv_query_device(struct ibv_context *, struct ibv_device_attr *);
struct ibv_port_attr {
	int          state, link_layer;
	uint16_t     lid;
	enum ibv_mtu active_mtu;
};
struct ibv_qp_cap {
	uint32_t max_send_wr, max_recv_wr, max_send_sge, max_recv_sge;
};
struct ibv_qp_init_attr {
	struct ibv_cq    *send_cq, *recv_cq;
	int               qp_type;
	struct ibv_qp_cap cap;
};
struct ibv_global_route {
	union ibv_gid dgid;
	uint8_t       sgid_index, hop_limit;
};
struct ibv_ah_attr {
	uint8_t                 port_num, is_global;
	uint16_t                dlid;
	struct ibv_global_route grh;
};
struct ibv_qp_attr {
	int     qp_state;
	uint8_t port_num, max_dest_rd_atomic, min_rnr_timer, timeout, retry_cnt, rnr_retry,
	    max_rd_atomic;
	uint16_t           pkey_index;
	int                qp_access_flags;
	enum ibv_mtu       path_mtu;
	uint32_t           dest_qp_num, rq_psn, sq_psn;
	struct ibv_ah_attr ah_attr;
};
struct ibv_sge {
	uint64_t addr;
	uint32_t length, lkey;
};
struct ibv_send_wr {
	uint64_t        wr_id;
	struct ibv_sge *sg_list;
	int             num_sge, send_flags, opcode;
	union {
		struct {
			uint64_t remote_addr;
			uint32_t rkey;
		} rdma;
	} wr;
};
struct ibv_recv_wr {
	uint64_t        wr_id;
	struct ibv_sge *sg_list;
	int             num_sge;
};
struct ibv_wc {
	uint64_t wr_id;
	int      status;
	uint32_t byte_len;
};

struct ibv_device **
ibv_get_device_list(int *);
const char *
ibv_get_device_name(struct ibv_device *);
void
ibv_free_device_list(struct ibv_device **);
struct ibv_context *
ibv_open_device(struct ibv_device *);
int
ibv_close_device(struct ibv_context *);
int
ibv_query_port(struct ibv_context *, uint8_t, struct ibv_port_attr *);
int
ibv_query_gid(struct ibv_context *, uint8_t, int, union ibv_gid *);
struct ibv_pd *
ibv_alloc_pd(struct ibv_context *);
int
ibv_dealloc_pd(struct ibv_pd *);
struct ibv_cq *
ibv_create_cq(struct ibv_context *, int, void *, void *, int);
int
ibv_destroy_cq(struct ibv_cq *);
struct ibv_qp *
ibv_create_qp(struct ibv_pd *, struct ibv_qp_init_attr *);
int
ibv_modify_qp(struct ibv_qp *, struct ibv_qp_attr *, int);
int
ibv_destroy_qp(struct ibv_qp *);
struct ibv_mr *
ibv_reg_mr(struct ibv_pd *, void *, size_t, int);
int
ibv_dereg_mr(struct ibv_mr *);
int
ibv_post_send(struct ibv_qp *, struct ibv_send_wr *, struct ibv_send_wr **);
int
ibv_post_recv(struct ibv_qp *, struct ibv_recv_wr *, struct ibv_recv_wr **);
int
ibv_poll_cq(struct ibv_cq *, int, struct ibv_wc *);
#endif
