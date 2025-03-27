/*
 * Copyright (c) 2021-2022 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under the BSD license below:
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 *	rdmabw-xe.c
 *
 *	This is a IB Verbs RDMA bandwidth test with buffers allocated via
 *	oneAPI L0 functions. Kernel and user space RDMA/dma-buf support is
 *	required (kernel 5.12 and later, rdma-core v34 and later, or MOFED
 *	5.5 and later).
 *
 *	Examples of running the test on a single node:
 *
 *	RDMA write host memory --> device memory:
 *
 *	    ./xe_rdmabw -m device -d 0 &
 *	    sleep 1 &&
 *	    ./xe_rdmabw -m host -t write localhost
 *
 *	RDMA read host memory <-- device memory:
 *
 *	    ./xe_rdmabw -m device -d 0 &
 *	    sleep 1 &&
 *	    ./xe_rdmabw -m host -t read localhost
 *
 *	RDMA write between memory on the same device:
 *
 *	    ./xe_rdmabw -m device -d 0 &
 *	    sleep 1 &&
 *	    ./xe_rdmabw -m device -d 0 -t write localhost
 *
 *	RDMA write between memory on different devices:
 *
 *	    ./xe_rdmabw -m device -d 0 &
 *	    sleep 1 &&
 *	    ./xe_rdmabw -m device -d 1 -t write localhost
 *
 *	RDMA write between memory on different devices, use specified IB device:
 *
 *	    ./xe_rdmabw -m device -d 0 -D mlx5_0 &
 *	    sleep 1 &&
 *	    ./xe_rdmabw -m device -d 1 -D mlx5_1 -t write localhost
 *
 *	For more options:
 *
 *	    ./xe_rdmabw -h
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <level_zero/ze_api.h>
#include "util.h"
#include "xe.h"

#define MAX_SIZE	(4*1024*1024)
#define MIN_PROXY_BLOCK	(131072)
#define TX_DEPTH	(128)
#define RX_DEPTH	(1)
#define MAX_NICS	(32)

enum test_type {
	READ,
	WRITE
};

static struct business_card {
	int	num_nics;
	int	num_gpus;
	struct {
		int		lid;
		int		qpn;
		int		psn;
		union ibv_gid	gid;
	} nics[MAX_NICS];
	struct {
		uint64_t	addr;
		uint64_t	rkeys[MAX_NICS];
	} bufs[MAX_GPUS];
} me, peer;

struct nic {
	struct ibv_device	*dev;
	struct ibv_context	*context;
	struct ibv_pd		*pd;
	struct ibv_cq		*cq;
	struct ibv_qp		*qp;
	int			lid;
	union ibv_gid		gid;
};

static struct ibv_device	**dev_list;
static struct nic		nics[MAX_NICS];
static int			num_nics;
static int			gid_idx = -1;
static int			mtu = IBV_MTU_4096;

struct buf {
	struct xe_buf		xe_buf;
	struct ibv_mr		*mrs[MAX_NICS];
};

static int			num_gpus;
static struct buf		bufs[MAX_GPUS];
static struct buf		proxy_buf;
static int			buf_location = MALLOC;
static int			use_proxy;
static int			proxy_block = MAX_SIZE;
static int			use_sync_ib;
static int			use_inline_send;
static int			use_odp;
static int			verify;

static void init_buf(size_t buf_size, char c)
{
	int page_size = sysconf(_SC_PAGESIZE);
	int i;
	void *buf;

	for (i = 0; i < num_gpus; i++) {
		buf = xe_alloc_buf(page_size, buf_size, buf_location, i,
				   &bufs[i].xe_buf);
		if (!buf) {
			fprintf(stderr, "Couldn't allocate work buf.\n");
			exit(-1);
		}
		xe_set_buf(buf, c, buf_size, buf_location, i);
	}

	if (buf_location == DEVICE && use_proxy) {
		if (!xe_alloc_buf(page_size, buf_size, HOST, 0, &proxy_buf.xe_buf)) {
			fprintf(stderr, "Couldn't allocate proxy buf.\n");
			exit(-1);
		}
	}
}

static void check_buf(size_t size, char c, int gpu)
{
	unsigned long mismatches = 0;
	int i;
	char *bounce_buf;

	bounce_buf = malloc(size);
	if (!bounce_buf) {
		perror("malloc bounce buffer");
		return;
	}

	xe_copy_buf(bounce_buf, bufs[gpu].xe_buf.buf, size, gpu);

	for (i = 0; i < size; i++)
		if (bounce_buf[i] != c) {
			mismatches++;
			if (mismatches < 10)
			printf("value at [%d] is '%c'(0x%02x), expecting '%c'(0x%02x)\n",
				i, bounce_buf[i], bounce_buf[i], c, c);
		}

	free(bounce_buf);

	if (mismatches)
		printf("%lu mismatches found\n", mismatches);
	else
		printf("all %lu bytes are correct.\n", size);
}

static void free_buf(void)
{
	int i;

	for (i = 0; i < num_gpus; i++)
		xe_free_buf(bufs[i].xe_buf.buf, bufs[i].xe_buf.location);

	if (use_proxy)
		xe_free_buf(proxy_buf.xe_buf.buf, proxy_buf.xe_buf.location);
}

/*
 * Fabric setup & tear-down
 */

static int connect_ib(int port, struct business_card *dest)
{
	struct ibv_qp_attr qp_attr = {};
	int qp_rtr_flags, qp_rts_flags;
	int i;

	/* set up pair-wise connection, not all-to-all connection */

	for (i = 0; i < num_nics; i++) {
		qp_attr.qp_state		= IBV_QPS_RTR;
		qp_attr.path_mtu		= mtu;
		qp_attr.dest_qp_num		= dest->nics[i].qpn;
		qp_attr.rq_psn			= dest->nics[i].psn;
		qp_attr.max_dest_rd_atomic	= 16;
		qp_attr.min_rnr_timer		= 12;
		qp_attr.ah_attr.is_global	= 0;
		qp_attr.ah_attr.dlid		= dest->nics[i].lid;
		qp_attr.ah_attr.sl		= 0;
		qp_attr.ah_attr.src_path_bits	= 0;
		qp_attr.ah_attr.port_num	= port;

		if (dest->nics[i].gid.global.interface_id) {
			qp_attr.ah_attr.is_global	= 1;
			qp_attr.ah_attr.grh.hop_limit	= 1;
			qp_attr.ah_attr.grh.dgid	= dest->nics[i].gid;
			qp_attr.ah_attr.grh.sgid_index	= gid_idx;
		}

		qp_rtr_flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
			       IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
			       IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;

		CHECK_ERROR(ibv_modify_qp(nics[i].qp, &qp_attr, qp_rtr_flags));

		qp_attr.qp_state = IBV_QPS_RTS;
		qp_attr.timeout	= 14;
		qp_attr.retry_cnt = 7;
		qp_attr.rnr_retry = 7;
		qp_attr.sq_psn = 0;
		qp_attr.max_rd_atomic = 16;

		qp_rts_flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
			       IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
			       IBV_QP_MAX_QP_RD_ATOMIC;

		CHECK_ERROR(ibv_modify_qp(nics[i].qp, &qp_attr, qp_rts_flags));
	}
	return 0;

err_out:
	return -1;
}

static void free_ib(void)
{
	int i, j;

	for (i = 0; i < num_nics; i++) {
		if (nics[i].qp)
			ibv_destroy_qp(nics[i].qp);
		if (nics[i].cq)
			ibv_destroy_cq(nics[i].cq);
		if (use_proxy && proxy_buf.mrs[i])
			ibv_dereg_mr(proxy_buf.mrs[i]);
		for (j = 0; j < num_gpus; j++)
			if (bufs[j].mrs[i])
				ibv_dereg_mr(bufs[j].mrs[i]);
		ibv_dealloc_pd(nics[i].pd);
		ibv_close_device(nics[i].context);
	}
	ibv_free_device_list(dev_list);
}

static struct ibv_mr *reg_mr(struct ibv_pd *pd, void *buf, uint64_t size,
			     void *base, int where)
{
	int mr_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
			      IBV_ACCESS_REMOTE_WRITE;
	int odp_flag = use_odp ? IBV_ACCESS_ON_DEMAND : 0;

	if (where == MALLOC || use_dmabuf_reg)
		return ibv_reg_mr(pd, buf, size, mr_access_flags | odp_flag);
	else
		return ibv_reg_dmabuf_mr(pd,
					 (uint64_t)((char *)buf - (char *)base),
					 size, (uint64_t)buf, /* iova */
					 xe_get_buf_fd(buf), mr_access_flags);
}

static int init_nic(int nic, char *ibdev_name, int ib_port)
{
	int qp_init_flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT |
			    IBV_QP_ACCESS_FLAGS;
	struct ibv_qp_attr qp_attr = {};
	struct ibv_qp_init_attr qp_init_attr = {};
	struct ibv_port_attr port_attr;
	struct ibv_device **p;
	struct ibv_device *dev = NULL;
	struct ibv_context *context = NULL;
	struct ibv_pd *pd = NULL;
	struct ibv_cq *cq = NULL;
	struct ibv_qp *qp = NULL;
	int i;

	p = dev_list;
	dev = *p;
	while (*p && ibdev_name) {
		dev = *p;
		if (!strcasecmp(ibdev_name, ibv_get_device_name(dev)))
			break;
		dev = NULL;
		p++;
	}
	if (!dev) {
		fprintf(stderr, "IB devices %s not found\n", ibdev_name);
		return -ENODEV;
	}

	printf("Using IB device %s\n", ibv_get_device_name(dev));

	/* open dev, pd, mr, cq */
	CHECK_NULL((context = ibv_open_device(dev)));
	CHECK_NULL((pd = ibv_alloc_pd(context)));
	for (i = 0; i < num_gpus; i++)
		CHECK_NULL((bufs[i].mrs[nic] =
				reg_mr(pd, bufs[i].xe_buf.buf,
				       bufs[i].xe_buf.size,
				       bufs[i].xe_buf.base,
				       bufs[i].xe_buf.location)));
	if (proxy_buf.xe_buf.buf)
		CHECK_NULL((proxy_buf.mrs[nic] =
				reg_mr(pd, proxy_buf.xe_buf.buf,
				       proxy_buf.xe_buf.size,
				       proxy_buf.xe_buf.base,
				       proxy_buf.xe_buf.location)));
	CHECK_NULL((cq = ibv_create_cq(context, TX_DEPTH + RX_DEPTH, NULL,
				       NULL, 0)));

	/* create & initialize qp */
	qp_init_attr.send_cq = cq;
	qp_init_attr.recv_cq = cq;
	qp_init_attr.cap.max_send_wr = TX_DEPTH * (MAX_SIZE / MIN_PROXY_BLOCK);
	qp_init_attr.cap.max_recv_wr = RX_DEPTH;
	qp_init_attr.cap.max_send_sge = 1;
	qp_init_attr.cap.max_recv_sge = 1;
	qp_init_attr.qp_type = IBV_QPT_RC;
	CHECK_NULL((qp = ibv_create_qp(pd, &qp_init_attr)));

	qp_attr.qp_state = IBV_QPS_INIT;
	qp_attr.pkey_index = 0;
	qp_attr.port_num = ib_port;
	qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
	CHECK_ERROR(ibv_modify_qp(qp, &qp_attr, qp_init_flags));

	nics[nic].dev = dev;
	nics[nic].context = context;
	nics[nic].pd = pd;
	nics[nic].qp = qp;
	nics[nic].cq = cq;

	CHECK_ERROR(ibv_query_port(context, ib_port, &port_attr));
	nics[nic].lid = port_attr.lid;

	if (gid_idx >= 0)
		CHECK_ERROR(ibv_query_gid(context, ib_port, gid_idx, &nics[nic].gid));
	else
		memset(&nics[nic].gid, 0, sizeof nics[nic].gid);

	return 0;

err_out:
	return -1;
}

static void show_business_card(struct business_card *bc, char *name)
{
	char gid[33];
	int i, j;

	printf("%s:\tnum_nics %d num_gpus %d ", name, bc->num_nics, bc->num_gpus);
	for (i = 0; i < bc->num_nics; i++) {
		inet_ntop(AF_INET6, &bc->nics[i].gid, gid, sizeof gid);
		printf("[NIC %d] lid 0x%04x qpn 0x%06x gid %s ",
			i, bc->nics[i].lid, bc->nics[i].qpn, gid);
	}
	for (i = 0; i < bc->num_gpus; i++) {
		printf("[BUF %d] addr %lx rkeys (", i, bc->bufs[i].addr);
		for (j = 0; j < bc->num_nics; j++)
			printf("%lx ", bc->bufs[i].rkeys[j]);
		printf(") ");
	}
	printf("\n");
}

static void init_ib(char *ibdev_names, int sockfd)
{
	int ib_port = 1;
	char *ibdev_name;
	int i, j;
	char *saveptr;

	dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		perror("Failed to get IB devices list");
		exit(-1);
	}

	if (!*dev_list) {
		fprintf(stderr, "No IB devices found\n");
		exit(-1);
	}

	num_nics = 0;
	if (ibdev_names) {
		ibdev_name = strtok_r(ibdev_names, ",", &saveptr);
		while (ibdev_name && num_nics < MAX_NICS) {
			EXIT_ON_ERROR(init_nic(num_nics, ibdev_name, ib_port));
			num_nics++;
			ibdev_name = strtok_r(NULL, ",", &saveptr);
		}
	} else {
		EXIT_ON_ERROR(init_nic(0, NULL, ib_port));
		num_nics++;
	}

	for (i = 0; i < num_nics; i++) {
		memcpy(&me.nics[i].gid, &nics[i].gid, sizeof(nics[i].gid));
		me.nics[i].lid = nics[i].lid;
		me.nics[i].qpn = nics[i].qp->qp_num;
		me.nics[i].psn = 0;
	}
	for (i = 0; i < num_gpus; i++) {
		me.bufs[i].addr = (uint64_t)bufs[i].xe_buf.buf;
		for (j = 0; j < num_nics; j++)
			me.bufs[i].rkeys[j] = bufs[i].mrs[j]->rkey;
	}
	me.num_nics = num_nics;
	me.num_gpus = num_gpus;

	show_business_card(&me, "Me");

	EXIT_ON_ERROR(exchange_info(sockfd, sizeof(me), &me, &peer));

	show_business_card(&peer, "Peer");

	if (me.num_nics != peer.num_nics) {
		printf("The number of IB devices doesn't match. Exiting\n");
		exit(-1);
	}

	EXIT_ON_ERROR(connect_ib(ib_port, &peer));
}

/*
 * Test routines
 */

static int post_rdma(int nic, int gpu, int rgpu, int test_type, size_t size,
		     int idx, int signaled)
{
	struct ibv_sge list = {
		.addr = (uintptr_t)bufs[gpu].xe_buf.buf + idx * size,
		.length = size,
		.lkey = bufs[gpu].mrs[nic]->lkey
	};
	struct ibv_send_wr wr = {
		.sg_list = &list,
		.num_sge = 1,
		.opcode = test_type == READ ? IBV_WR_RDMA_READ :
					      IBV_WR_RDMA_WRITE,
		.send_flags = signaled ? IBV_SEND_SIGNALED : 0,
		.wr = {
		  .rdma = {
		    .remote_addr = peer.bufs[rgpu].addr + idx * size,
		    .rkey = peer.bufs[rgpu].rkeys[nic],
		  }
		}
	};
	struct ibv_send_wr *bad_wr;

	return ibv_post_send(nics[nic].qp, &wr, &bad_wr);
}

static int post_proxy_write(int nic, int gpu, int rgpu, size_t size, int idx,
			    int signaled)
{
	uintptr_t offset = idx * size;
	struct ibv_sge list = {
		.addr = (uintptr_t)proxy_buf.xe_buf.buf + offset,
		.length = proxy_block,
		.lkey = proxy_buf.mrs[nic]->lkey
	};
	struct ibv_send_wr wr = {
		.sg_list = &list,
		.num_sge = 1,
		.opcode = IBV_WR_RDMA_WRITE,
		.wr = {
		  .rdma = {
		    .remote_addr = peer.bufs[rgpu].addr + offset,
		    .rkey = peer.bufs[rgpu].rkeys[nic],
		  }
		}
	};
	struct ibv_send_wr *bad_wr;
	size_t sent, block_size = proxy_block;
	int ret;

	for (sent = 0; sent < size;) {
		if (block_size >= size - sent) {
			block_size = size - sent;
			list.length = block_size;
			wr.send_flags = signaled ? IBV_SEND_SIGNALED : 0;
		}

		xe_copy_buf((char *)proxy_buf.xe_buf.buf + offset + sent,
			    (char *)bufs[gpu].xe_buf.buf + offset + sent,
			    block_size, gpu);

		ret = ibv_post_send(nics[nic].qp, &wr, &bad_wr);
		if (ret)
			break;

		sent += block_size;
		list.addr += block_size;
		wr.wr.rdma.remote_addr += block_size;
	}

	return ret;
}

static int post_send(int nic, int gpu, size_t size, int idx, int signaled)
{
	struct ibv_sge list = {
		.addr = (uintptr_t)bufs[gpu].xe_buf.buf + idx * size,
		.length = size,
		.lkey = bufs[gpu].mrs[nic]->lkey
	};
	struct ibv_send_wr wr = {
		.sg_list = &list,
		.num_sge = 1,
		.opcode = IBV_WR_SEND,
		.send_flags = (signaled ? IBV_SEND_SIGNALED : 0) |
			      (use_inline_send ? IBV_SEND_INLINE : 0)
	};
	struct ibv_send_wr *bad_wr;

	printf("%s: size %ld, signaled %d, inline_send %d\n", __func__,
		size, signaled, use_inline_send);
	return ibv_post_send(nics[nic].qp, &wr, &bad_wr);
}

static int post_recv(int nic, int gpu, size_t size, int idx)
{
	struct ibv_sge list = {
		.addr = (uintptr_t)bufs[gpu].xe_buf.buf + idx * size,
		.length = size,
		.lkey = bufs[gpu].mrs[nic]->lkey
	};
	struct ibv_recv_wr wr = {
		.sg_list = &list,
		.num_sge = 1,
	};
	struct ibv_recv_wr *bad_wr;

	printf("%s: size %ld\n", __func__, size);
	return ibv_post_recv(nics[nic].qp, &wr, &bad_wr);
}

void check_completions(struct ibv_wc *wc, int n)
{
	int i;
	static uint64_t errcnt = 0;

	for (i = 0; i < n; i++) {
		if (wc->status != IBV_WC_SUCCESS) {
			fprintf(stderr,
				"Completion with error: %s. [total %ld]\n",
				ibv_wc_status_str(wc->status), ++errcnt);
			exit(-1);
		}
		wc++;
	}
}

static void sync_ib(size_t size)
{
	int n, i;
	int pending = 2 * num_nics;
	struct ibv_wc wc[2];

	for (i = 0; i < num_nics; i++) {
		CHECK_ERROR(post_recv(i, 0, size, 0));
		CHECK_ERROR(post_send(i, 0, size, 0, 1));
	}

	while (pending > 0) {
		for (i = 0; i < num_nics; i++) {
			n = ibv_poll_cq(nics[i].cq, 2, wc);
			if (n < 0) {
				fprintf(stderr, "poll CQ failed %d\n", n);
				return;
			} else if (n > 0) {
				printf("%s: n %d, pending %d\n", __func__, n,
					pending);
				check_completions(wc, n);
				pending -= n;
			}
		}
	}

err_out:
	return;
}

void run_rdma_test(int test_type, int size, int iters, int batch,
		   int output_result)
{
	int i, j, completed, pending;
	int n, n0;
	double t1, t2;
	struct ibv_wc wc[16];
	int signaled;
	int nic, gpu, rgpu;

	t1 = when();
	for (i = completed = pending = 0; i < iters || completed < iters;) {
		while (i < iters && pending < TX_DEPTH) {
			nic = i % num_nics;
			gpu = i % num_gpus;
			rgpu = i % peer.num_gpus;
			signaled = ((i / num_nics) % batch) == batch -1 ||
				   i >= iters - num_nics;
			if (use_proxy && test_type == WRITE)
				CHECK_ERROR(post_proxy_write(nic, gpu, rgpu,
							     size, i % batch,
							     signaled));
			else
				CHECK_ERROR(post_rdma(nic, gpu, rgpu, test_type,
						      size, i % batch,
						      signaled));
			pending++;
			i++;
		}
		do {
			n0 = 0;
			for (j = 0; j < num_nics; j++) {
				n = ibv_poll_cq(nics[j].cq, 16, wc);
				if (n < 0) {
					fprintf(stderr, "poll CQ failed %d\n", n);
					return;
				} else {
					check_completions(wc, n);
					pending -= n * batch;
					completed += n * batch;
					n0 += n;
				}
			}
		} while (n0 > 0);
	}
	t2 = when();

	if (output_result)
		printf("%10d (x %4d) %10.2lf us %12.2lf MB/s\n", size, iters,
		       (t2 - t1), (long)size * iters / (t2 - t1));

	return;

err_out:
	printf("%10d aborted due to fail to post read request\n", size);
}

static void usage(char *prog_name)
{
	printf("Usage: %s [options][server_name]\n", prog_name);
	printf("Options:\n");
	printf("\t-m <location>    Where to allocate the buffer, can be 'host','device' or 'shared', default: host\n");
	printf("\t-d <gpu_devs>    Use the GPU devices specified as comma separated list of <dev>[.<subdev>], default: 0\n");
	printf("\t-D <ibdev_names> Use the IB devices named comma separated list of <ibdev_name>, default: the first one\n");
	printf("\t-g <gid_index>   Specify local port gid index, default: unused\n");
	printf("\t-M <mtu>         Set the MTU, default: 4096\n");
	printf("\t-n <iters>       Set the number of iterations for each message size, default: 1000\n");
	printf("\t-b <batch>       Generate completion for every <batch> iterations (default: 16)\n");
	printf("\t-S <size>        Set the message size to test (0: all, -1: none), default: 0\n");
	printf("\t-t <test_type>   Type of test to perform, can be 'read' or 'write', default: read\n");
	printf("\t-P               Proxy device buffer through host buffer (for write only), default: off\n");
	printf("\t-B <block_size>  Set the block size for proxying, default: maximum message size\n");
	printf("\t-O               Use on-demand paging flag (host memory only)\n");
	printf("\t-r               Reverse the direction of data movement (server initates RDMA ops)\n");
	printf("\t-R               Enable dmabuf_reg (plug-in for MOFED peer-memory)\n");
	printf("\t-s               Sync with send/recv at the end\n");
	printf("\t-i               Use inline send\n");
	printf("\t-v               Verify the data (for read test only)\n");
	printf("\t-2               Run test in both direction\n");
	printf("\t-h               Print this message\n");
}

int main(int argc, char *argv[])
{
	char *server_name = NULL;
	char *ibdev_names = NULL;
	char *gpu_dev_nums = NULL;
	int enable_multi_gpu;
	unsigned int port = 12345;
	int test_type = READ;
	int iters = 1000;
	int batch = 16;
	int reverse = 0;
	int sockfd;
	int size;
	int c;
	int initiator;
	int bidir = 0;
	int msg_size = 0;

	while ((c = getopt(argc, argv, "b:d:D:g:m:M:n:t:PB:OrRsS:ihv2")) != -1) {
		switch (c) {
		case 'b':
			batch = atoi(optarg);
			break;
		case 'd':
			gpu_dev_nums = strdup(optarg);
			break;
		case 'D':
			ibdev_names = strdup(optarg);
			break;
		case 'g':
			gid_idx = atoi(optarg);
			break;
		case 'm':
			if (strcasecmp(optarg, "malloc") == 0)
				buf_location = MALLOC;
			else if (strcasecmp(optarg, "host") == 0)
				buf_location = HOST;
			else if (strcasecmp(optarg, "device") == 0)
				buf_location = DEVICE;
			else
				buf_location = SHARED;
			break;
		case 'M':
			if (strcmp(optarg, "256") == 0)
				mtu = IBV_MTU_256;
			else if (strcmp(optarg, "512") == 0)
				mtu = IBV_MTU_512;
			else if (strcmp(optarg, "1024") == 0)
				mtu = IBV_MTU_1024;
			else if (strcmp(optarg, "2048") == 0)
				mtu = IBV_MTU_2048;
			else if (strcmp(optarg, "4096") == 0)
				mtu = IBV_MTU_4096;
			else
				printf("invalid mtu: %s, ignored. "
				       "valid values are: 256, 512, 1024, 2048, 4096\n",
				       optarg);
			break;
		case 'n':
			iters = atoi(optarg);
			break;
		case 't':
			if (strcasecmp(optarg, "read") == 0)
				test_type = READ;
			else if (strcasecmp(optarg, "write") == 0)
				test_type = WRITE;
			break;
		case 'P':
			use_proxy = 1;
			break;
		case 'B':
			proxy_block = atoi(optarg);
			if (proxy_block < MIN_PROXY_BLOCK) {
				fprintf(stderr,
					"Block size too small, adjusted to %d\n",
					MIN_PROXY_BLOCK);
				proxy_block = MIN_PROXY_BLOCK;
			}
			break;
		case 'O':
			use_odp = 1;
			break;
		case 'r':
			reverse = 1;
			break;
		case 'R':
			use_dmabuf_reg = 1;
			break;
		case 's':
			use_sync_ib = 1;
			break;
		case 'S':
			msg_size = atoi(optarg);
			break;
		case 'i':
			use_inline_send = 1;
			break;
		case 'v':
			verify = 1;
			break;
		case '2':
			bidir = 1;
			break;
		default:
			usage(argv[0]);
			exit(-1);
			break;
		}
	}

	if (argc > optind)
		server_name = strdup(argv[optind]);

	sockfd = connect_tcp(server_name, port);
	if (sockfd < 0) {
		fprintf(stderr, "Cannot create socket connection\n");
		exit(-1);
	}

	initiator = (!reverse && server_name) || (reverse && !server_name);

	if (use_dmabuf_reg && dmabuf_reg_open())
		exit(-1);

	/* multi-GPU test doesn't make sense if buffers are on the host */
	enable_multi_gpu = buf_location != MALLOC && buf_location != HOST;
	num_gpus = xe_init(gpu_dev_nums, enable_multi_gpu);

	init_buf(MAX_SIZE * batch, initiator ? 'A' : 'a');
	init_ib(ibdev_names, sockfd);

	sync_tcp(sockfd);
	printf("Warming up ...\n");
	if (initiator || bidir)
		run_rdma_test(test_type, 1, 16, 1, 0);

	sync_tcp(sockfd);
	printf("Start RDMA test ...\n");
	for (size = 1; size <= MAX_SIZE; size <<= 1) {
		if (msg_size < 0)
			break;
		else if (msg_size > 0)
			size = msg_size;
		if (initiator || bidir)
			run_rdma_test(test_type, size, iters, batch, 1);
		sync_tcp(sockfd);
		if (verify) {
			if (test_type == READ)
				check_buf(size, 'a', 0);
			else
				check_buf(size, 'A', 0);
		}
		if (msg_size)
			break;
	}
	sync_tcp(sockfd);

	if (use_sync_ib)
		sync_ib(4);

	free_ib();
	free_buf();

	if (use_dmabuf_reg)
		dmabuf_reg_close();

	close(sockfd);

	return 0;
}

