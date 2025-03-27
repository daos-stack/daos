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
 *	fi-rdmabw-xe.c
 *
 *	This is a libfabric veriosn of the RDMA bandwidth test with buffers
 *	allocated via oneAPI L0 functions. Kernel and user space RDMA/dma-buf
 *	support is required (kernel 5.12 or later, rdma-core v34 and later,
 *	or MOFED 5.5 and later).
 *
 * 	Examples of running the test on a single node:
 *
 *	RDMA write host memory --> device memory:
 *
 *	    ./fi_xe_rdmabw -m device -d 0 &
 *	    sleep 1 &&
 *	    ./fi_xe_rdmabw -m host -t write localhost
 *
 *	RDMA read host memory <-- device memory:
 *
 *	    ./fi_xe_rdmabw -m device -d 0 &
 *	    sleep 1 &&
 *	    ./fi_xe_rdmabw -m host -t read localhost
 *
 *	RDMA write between memory on the same device:
 *
 *	    ./fi_xe_rdmabw -m device -d 0 &
 *	    sleep 1 &&
 *	    ./fi_xe_rdmabw -m device -d 0 -t write localhost
 *
 *	RDMA write test between memory on different devices:
 *
 *	    ./fi_xe_rdmabw -m device -d 0 &
 *	    sleep 1 &&
 *	    ./fi_xe_rdmabw -m device -d 1 -t write localhost
 *
 *	RDMA write test between memory on different devices, use specified
 *	network interface (as OFI domain name):
 *
 *	    ./fi_xe_rdmabw -m device -d 0 -D mlx5_0 &
 *	    sleep 1 &&
 *	    ./fi_xe_rdmabw -m device -d 1 -D mlx5_1 -t write localhost
 *
 *	For more options:
 *
 *	    ./fi_xe_rdmabw -h
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <arpa/inet.h>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_atomic.h>
#include <rdma/fi_errno.h>
#include <level_zero/ze_api.h>
#include "util.h"
#include "xe.h"
#include "ofi_ctx_pool.h"

#define MAX_SIZE	(4*1024*1024)
#define MIN_PROXY_BLOCK	(131072)
#define TX_DEPTH	(128)
#define RX_DEPTH	(1)
#define MAX_NICS	(32)
#define MAX_RAW_KEY_SIZE (256)

enum test_type {
	READ,
	WRITE,
	SEND,
	RECV, /* internal use only */
};

struct raw_key {
	uint64_t	size;
	uint8_t		key[MAX_RAW_KEY_SIZE];
};

static struct business_card {
	int num_nics;
	int num_gpus;
	struct {
		union {
			struct {
				uint64_t	one;
				uint64_t	two;
				uint64_t	three;
				uint64_t	four;
			};
			uint8_t bytes[1024];
		} ep_name;
	} nics[MAX_NICS];
	struct {
		uint64_t	addr;
		uint64_t	rkeys[MAX_NICS];
		struct raw_key 	raw_keys[MAX_NICS];
	} bufs[MAX_GPUS];
	int use_raw_key;
} me, peer;

static char			*server_name;
static char			*prov_name;
static char			*domain_names;
static int			client;
static int			ep_type = FI_EP_RDM;
static int			use_raw_key;

struct nic {
	struct fi_info		*fi, *fi_pep;
	struct fid_fabric	*fabric;
	struct fid_eq		*eq;
	struct fid_domain	*domain;
	struct fid_pep		*pep;
	struct fid_ep		*ep;
	struct fid_av		*av;
	struct fid_cq		*cq;
	fi_addr_t		peer_addr;
};

static struct nic 		nics[MAX_NICS];
static int			num_nics;

struct context_pool		*context_pool;

struct buf {
	struct xe_buf		xe_buf;
	struct fid_mr		*mrs[MAX_NICS];
};

static int			num_gpus;
static struct buf		bufs[MAX_GPUS];
static struct buf		proxy_buf;
static struct buf		sync_buf;
static int			buf_location = MALLOC;

static int			use_proxy;
static int			proxy_block = MAX_SIZE;
static int			use_sync_ofi;
static int			verify;
static int			prepost;
static int			batch = 1;
static size_t			max_size = MAX_SIZE;

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
		if (!xe_alloc_buf(page_size, buf_size, HOST, 0,
				  &proxy_buf.xe_buf)) {
			fprintf(stderr, "Couldn't allocate proxy buf.\n");
			exit(-1);
		}
	}

	if (!xe_alloc_buf(page_size, page_size, MALLOC, 0, &sync_buf.xe_buf)) {
		fprintf(stderr, "Couldn't allocate sync buf.\n");
		exit(-1);
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

	xe_free_buf(sync_buf.xe_buf.buf, sync_buf.xe_buf.location);
}

/*
 * Fabric setup & tear-down
 */

static int wait_conn_req(struct fid_eq *eq, struct fi_info **fi)
{
	struct fi_eq_cm_entry entry;
	struct fi_eq_err_entry err_entry;
	uint32_t event;
	ssize_t ret;

	ret = fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0);
	if (ret != sizeof entry) {
		printf("%s: fi_eq_sread returned %ld, expecting %ld\n",
			__func__, ret, sizeof(entry));
		if (ret == -FI_EAVAIL) {
			fi_eq_readerr(eq, &err_entry, 0);
			printf("%s: error %d prov_errno %d\n", __func__,
				err_entry.err, err_entry.prov_errno);
		}
		return (int) ret;
	}

	*fi = entry.info;
	if (event != FI_CONNREQ) {
		printf("%s: unexpected CM event %d\n", __func__, event);
		return -FI_EOTHER;
	}

	return 0;
}

static int wait_connected(struct fid_ep *ep, struct fid_eq *eq)
{
	struct fi_eq_cm_entry entry;
	struct fi_eq_err_entry err_entry;
	uint32_t event;
	ssize_t ret;

	ret = fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0);
	if (ret != sizeof(entry)) {
		printf("%s: fi_eq_sread returns %ld, expecting %ld\n",
			__func__, ret, sizeof(entry));
		if (ret == -FI_EAVAIL) {
			fi_eq_readerr(eq, &err_entry, 0);
			printf("%s: error %d prov_errno %d\n", __func__,
				err_entry.err, err_entry.prov_errno);
		}
		return (int)ret;
	}

	if (event != FI_CONNECTED || entry.fid != &ep->fid) {
		printf("%s: unexpected CM event %d fid %p (ep %p)\n",
			__func__, event, entry.fid, ep);
		return -FI_EOTHER;
	}

	return 0;
}

static int init_nic(int nic, char *domain_name, char *server_name, int port,
		    int test_type)
{
	struct fi_info *fi, *fi_pep = NULL;
	struct fid_fabric *fabric;
	struct fid_eq *eq = NULL;
	struct fid_domain *domain;
	struct fid_pep *pep = NULL;
	struct fid_ep *ep;
	struct fid_av *av;
	struct fid_cq *cq;
	struct fid_mr *mr = NULL;
	struct fi_info *hints;
	struct fi_cq_attr cq_attr = { .format = FI_CQ_FORMAT_CONTEXT };
	struct fi_av_attr av_attr = {};
	struct fi_mr_attr mr_attr = {};
	struct fi_eq_attr eq_attr = { .wait_obj = FI_WAIT_UNSPEC };
	struct iovec iov;
	int version;
	char port_name[16];
	int i;

	EXIT_ON_NULL((hints = fi_allocinfo()));

	hints->ep_attr->type = ep_type;
	hints->ep_attr->tx_ctx_cnt = 1;
	hints->ep_attr->rx_ctx_cnt = 1;
	if (prov_name)
		hints->fabric_attr->prov_name = strdup(prov_name);
	hints->caps = FI_MSG | FI_RMA;
	if (buf_location != MALLOC)
		hints->caps |= FI_HMEM;
	hints->mode = FI_CONTEXT;
	hints->domain_attr->control_progress = FI_PROGRESS_MANUAL;
	hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;
	hints->domain_attr->mr_mode = FI_MR_ALLOCATED | FI_MR_PROV_KEY |
				      FI_MR_VIRT_ADDR | FI_MR_LOCAL |
				      FI_MR_HMEM | FI_MR_ENDPOINT | FI_MR_RAW;
	if (domain_name)
		hints->domain_attr->name = strdup(domain_name);

	sprintf(port_name, "%d", port);
	version = FI_VERSION(1, 12);
	if (ep_type == FI_EP_MSG) {
		if (client)
			EXIT_ON_ERROR(fi_getinfo(version, server_name,
						 port_name, 0, hints, &fi));
		else
			EXIT_ON_ERROR(fi_getinfo(version, server_name,
						 port_name, FI_SOURCE, hints,
						 &fi));
	} else {
		EXIT_ON_ERROR(fi_getinfo(version, NULL, NULL, 0, hints, &fi));
	}

	fi_freeinfo(hints);

	if (ep_type == FI_EP_RDM || client)
		printf("Using OFI device: %s (%s)\n",
			fi->fabric_attr->prov_name, fi->domain_attr->name);

	EXIT_ON_ERROR(fi_fabric(fi->fabric_attr, &fabric, NULL));
	if (ep_type == FI_EP_MSG) {
		EXIT_ON_ERROR(fi_eq_open(fabric, &eq_attr, &eq, NULL));
		if (!client) {
			fi_pep = fi;
			EXIT_ON_ERROR(fi_passive_ep(fabric, fi_pep, &pep, NULL));
			EXIT_ON_ERROR(fi_pep_bind(pep, (fid_t)eq, 0));
			EXIT_ON_ERROR(fi_listen(pep));
			EXIT_ON_ERROR(wait_conn_req(eq, &fi));
			printf("Using OFI device: %s (%s)\n",
				fi_pep->fabric_attr->prov_name,
				fi->domain_attr->name);
		}
	}
	EXIT_ON_ERROR(fi_domain(fabric, fi, &domain, NULL));
	EXIT_ON_ERROR(fi_endpoint(domain, fi, &ep, NULL));
	EXIT_ON_ERROR(fi_cq_open(domain, &cq_attr, &cq, NULL));
	if (ep_type == FI_EP_RDM) {
		EXIT_ON_ERROR(fi_av_open(domain, &av_attr, &av, NULL));
		EXIT_ON_ERROR(fi_ep_bind(ep, (fid_t)av, 0));
	} else {
		EXIT_ON_ERROR(fi_ep_bind(ep, (fid_t)eq, 0));
	}
	EXIT_ON_ERROR(fi_ep_bind(ep, (fid_t)cq,
		    (FI_TRANSMIT | FI_RECV | FI_SELECTIVE_COMPLETION)));
	EXIT_ON_ERROR(fi_enable(ep));

	if (ep_type == FI_EP_MSG) {
		if (client)
			EXIT_ON_ERROR(fi_connect(ep, fi->dest_addr, NULL, 0));
		else
			EXIT_ON_ERROR(fi_accept(ep, NULL, 0));
		EXIT_ON_ERROR(wait_connected(ep, eq));
	}

	if (test_type == SEND &&
	    !(fi->domain_attr->mr_mode & (FI_MR_HMEM | FI_MR_LOCAL))) {
		printf("Local MR registration skipped.\n");
		goto done;
	}

	if (fi->domain_attr->mr_mode & FI_MR_RAW)
		use_raw_key = 1;

	for (i = 0; i < num_gpus; i++) {
		iov.iov_base = bufs[i].xe_buf.buf;
		iov.iov_len = max_size * batch;
		mr_attr.mr_iov = &iov;
		mr_attr.iov_count = 1;
		mr_attr.access = FI_REMOTE_READ | FI_REMOTE_WRITE |
				 FI_READ | FI_WRITE | FI_SEND | FI_RECV;
		mr_attr.requested_key = i + 1;
		mr_attr.iface = bufs[i].xe_buf.location == MALLOC ?
					FI_HMEM_SYSTEM : FI_HMEM_ZE;
		mr_attr.device.ze = xe_get_dev_num(i);
		EXIT_ON_ERROR(fi_mr_regattr(domain, &mr_attr, 0, &mr));

		if (fi->domain_attr->mr_mode & FI_MR_ENDPOINT) {
			EXIT_ON_ERROR(fi_mr_bind(mr, (fid_t)ep, 0));
			EXIT_ON_ERROR(fi_mr_enable(mr));
		}

		bufs[i].mrs[nic] = mr;
	}

	if (buf_location == DEVICE && use_proxy) {
		iov.iov_base = proxy_buf.xe_buf.buf;
		iov.iov_len = max_size * batch;
		mr_attr.mr_iov = &iov;
		mr_attr.iov_count = 1;
		mr_attr.access = FI_REMOTE_READ | FI_REMOTE_WRITE |
				 FI_READ | FI_WRITE | FI_SEND | FI_RECV;
		mr_attr.requested_key = i + 1;
		mr_attr.iface = FI_HMEM_ZE;
		mr_attr.device.ze = xe_get_dev_num(i);
		EXIT_ON_ERROR(fi_mr_regattr(domain, &mr_attr, 0, &mr));

		if (fi->domain_attr->mr_mode & FI_MR_ENDPOINT) {
			EXIT_ON_ERROR(fi_mr_bind(mr, (fid_t)ep, 0));
			EXIT_ON_ERROR(fi_mr_enable(mr));
		}

		proxy_buf.mrs[nic] = mr;
	}

	iov.iov_base = sync_buf.xe_buf.buf;
	iov.iov_len = 4;
	mr_attr.mr_iov = &iov;
	mr_attr.iov_count = 1;
	mr_attr.access = FI_SEND | FI_RECV;
	mr_attr.requested_key = i + 2;
	mr_attr.iface = FI_HMEM_SYSTEM;
	mr_attr.device.ze = 0;
	EXIT_ON_ERROR(fi_mr_regattr(domain, &mr_attr, 0, &mr));

	if (fi->domain_attr->mr_mode & FI_MR_ENDPOINT) {
		EXIT_ON_ERROR(fi_mr_bind(mr, (fid_t)ep, 0));
		EXIT_ON_ERROR(fi_mr_enable(mr));
	}

	sync_buf.mrs[nic] = mr;

done:
	nics[nic].fi = fi;
	nics[nic].fi_pep = fi_pep;
	nics[nic].fabric = fabric;
	nics[nic].eq = eq;
	nics[nic].domain = domain;
	nics[nic].pep = pep;
	nics[nic].ep = ep;
	nics[nic].av = av;
	nics[nic].cq = cq;
	return 0;
}

static void show_business_card(struct business_card *bc, char *name)
{
	int i, j;

	printf("%s:\tnum_nics %d num_gpus %d use_raw_key %d", name,
		bc->num_nics, bc->num_gpus, bc->use_raw_key);
	for (i = 0; i < bc->num_nics; i++) {
		printf("[NIC %d] %lx:%lx:%lx:%lx ", i,
			bc->nics[i].ep_name.one, bc->nics[i].ep_name.two,
			bc->nics[i].ep_name.three, bc->nics[i].ep_name.four);
	}
	for (i = 0; i < bc->num_gpus; i++) {
		printf("[BUF %d] addr %lx rkeys (", i, bc->bufs[i].addr);
		for (j = 0; j < bc->num_nics; j++)
			printf("%lx ", bc->bufs[i].rkeys[j]);
		printf(") ");
	}
	printf("\n");

}

static void init_ofi(int sockfd, char *server_name, int port, int test_type)
{
	int i, j;
	int err;
	size_t len;
	char *domain_name;
	char *saveptr;

	EXIT_ON_NULL((context_pool = init_context_pool(TX_DEPTH + 1)));

	num_nics = 0;
	if (domain_names) {
		domain_name = strtok_r(domain_names, ",", &saveptr);
		while (domain_name && num_nics < MAX_NICS) {
			err = init_nic(num_nics, domain_name, server_name,
				       port, test_type);
			if (err)
				return;
			num_nics++;
			domain_name = strtok_r(NULL, ",", &saveptr);
		}
	} else {
		err = init_nic(num_nics, NULL, server_name, port, test_type);
		if (err)
			return;
		num_nics++;
	}

	for (i = 0; i < num_gpus; i++) {
		me.bufs[i].addr = (uint64_t)bufs[i].xe_buf.buf;
		for (j = 0; j < num_nics; j++) {
			len = sizeof(me.nics[j].ep_name);
			EXIT_ON_ERROR(fi_getname((fid_t)nics[j].ep,
						 &me.nics[j].ep_name, &len));
			me.bufs[i].rkeys[j]= bufs[i].mrs[j] ?
						fi_mr_key(bufs[i].mrs[j]) : 0;
			if (use_raw_key && bufs[i].mrs[j]) {
				me.bufs[i].raw_keys[j].size = MAX_RAW_KEY_SIZE;
				fi_mr_raw_attr(bufs[i].mrs[j],
					       (void *)(uintptr_t)me.bufs[i].addr,
					       me.bufs[i].raw_keys[j].key,
					       &me.bufs[i].raw_keys[j].size, 0);
			}
		}
	}
	me.num_nics = num_nics;
	me.num_gpus = num_gpus;
	me.use_raw_key = use_raw_key;

	show_business_card(&me, "Me");

	EXIT_ON_ERROR(exchange_info(sockfd, sizeof(me), &me, &peer));

	if (!(nics[0].fi->domain_attr->mr_mode & FI_MR_VIRT_ADDR)) {
		for (i = 0; i < num_gpus; i++)
			peer.bufs[i].addr = 0;
	}

	show_business_card(&peer, "Peer");

	if (me.num_nics != peer.num_nics) {
		printf("The number of network devices doesn't match. Exiting\n");
		exit(-1);
	}

	if (me.use_raw_key != peer.use_raw_key) {
		printf("The use of raw key doesn't match. Exiting\n");
		exit(-1);
	}

	if (use_raw_key) {
		for (i = 0; i < peer.num_gpus; i++) {
			for (j = 0; j < peer.num_nics; j++) {
				if (!peer.bufs[i].rkeys[j])
					continue;
				EXIT_ON_ERROR(fi_mr_map_raw(nics[j].domain,
							    peer.bufs[i].addr,
							    peer.bufs[i].raw_keys[j].key,
							    peer.bufs[i].raw_keys[j].size,
							    &peer.bufs[i].rkeys[j],
							    0));
			}
		}
	}

	if (ep_type == FI_EP_MSG)
		return;

	for (i = 0; i < num_nics; i++) {
		EXIT_ON_NEG_ERROR(fi_av_insert(nics[i].av, &peer.nics[i].ep_name,
					       1, &nics[i].peer_addr, 0, NULL));
	}

	return;
}

static void finalize_ofi(void)
{
	int i, j;

	if (use_raw_key) {
		for (i = 0; i < peer.num_gpus; i++) {
			for (j = 0; j < peer.num_nics; j++) {
				if (peer.bufs[i].rkeys[j])
					fi_mr_unmap_key(nics[j].domain,
							peer.bufs[i].rkeys[j]);
			}
		}
	}

	for (i = 0; i < num_nics; i++) {
		if (sync_buf.mrs[i])
			fi_close((fid_t)sync_buf.mrs[i]);
		if (buf_location == DEVICE && use_proxy && proxy_buf.mrs[i])
			fi_close((fid_t)proxy_buf.mrs[i]);
		for (j = 0; j < num_gpus ; j++)
			if (bufs[j].mrs[i])
				fi_close((fid_t)bufs[j].mrs[i]);
		fi_close((fid_t)nics[i].ep);
		if (ep_type == FI_EP_RDM)
			fi_close((fid_t)nics[i].av);
		fi_close((fid_t)nics[i].cq);
		fi_close((fid_t)nics[i].domain);
		if (ep_type == FI_EP_MSG && !client)
			fi_close((fid_t)nics[i].pep);
		if (ep_type == FI_EP_MSG)
			fi_close((fid_t)nics[i].eq);
		fi_close((fid_t)nics[i].fabric);
		fi_freeinfo(nics[i].fi);
		if (ep_type == FI_EP_MSG && !client)
			fi_freeinfo(nics[i].fi_pep);
	}
}

/*
 * Test routines
 */

static int post_rdma(int nic, int gpu, int rgpu, int test_type, size_t size,
		     int idx, int signaled)
{
	struct iovec iov;
	void *desc = fi_mr_desc(bufs[gpu].mrs[nic]);
	struct fi_rma_iov rma_iov;
	struct fi_msg_rma msg;
	int err;

	iov.iov_base = (char *)bufs[gpu].xe_buf.buf + idx * size;
	iov.iov_len = size;
	rma_iov.addr = peer.bufs[rgpu].addr + idx * size;
	rma_iov.len = size;
	rma_iov.key = peer.bufs[rgpu].rkeys[nic];
	msg.msg_iov = &iov;
	msg.desc = &desc;
	msg.iov_count = 1;
	msg.addr = nics[nic].peer_addr;
	msg.rma_iov = &rma_iov;
	msg.rma_iov_count = 1;
	msg.context = get_context(context_pool);
	msg.data = 0;

try_again:
	if (test_type == READ)
		err = fi_readmsg(nics[nic].ep, &msg,
				 signaled ? FI_COMPLETION : 0);
	else
		err = fi_writemsg(nics[nic].ep, &msg,
				  signaled ? FI_COMPLETION : 0);

	if (err == -FI_EAGAIN) {
		fi_cq_read(nics[nic].cq, NULL, 0);
		goto try_again;
	}

	return err;
}

static int post_proxy_write(int nic, int gpu, int rgpu, size_t size, int idx,
			    int signaled)
{
	uintptr_t offset = idx * size;
	struct iovec iov;
	void *desc = fi_mr_desc(proxy_buf.mrs[nic]);
	struct fi_rma_iov rma_iov;
	struct fi_msg_rma msg;
	size_t sent, block_size = proxy_block;
	int flags = 0;
	int ret;

	iov.iov_base = (char *)proxy_buf.xe_buf.buf + offset;
	iov.iov_len = proxy_block;
	rma_iov.addr = peer.bufs[rgpu].addr + offset;
	rma_iov.len = proxy_block;
	rma_iov.key = peer.bufs[rgpu].rkeys[nic];
	msg.msg_iov = &iov;
	msg.desc = &desc;
	msg.iov_count = 1;
	msg.addr = nics[nic].peer_addr;
	msg.rma_iov = &rma_iov;
	msg.rma_iov_count = 1;
	msg.context = get_context(context_pool);
	msg.data = 0;

	for (sent = 0; sent < size;) {
		if (block_size >= size - sent) {
			block_size = size - sent;
			iov.iov_len = block_size;
			rma_iov.len = block_size;
			flags = signaled ? FI_COMPLETION : 0;
		}

		xe_copy_buf((char *)proxy_buf.xe_buf.buf + offset + sent,
			    (char *)bufs[gpu].xe_buf.buf + offset + sent,
			    block_size, gpu);

try_again:
		ret = fi_writemsg(nics[nic].ep, &msg, flags);
		if (ret == -FI_EAGAIN) {
			fi_cq_read(nics[nic].cq, NULL, 0);
			goto try_again;
		} else if (ret) {
			break;
		}

		sent += block_size;
		iov.iov_base = (char *)iov.iov_base + block_size;
		rma_iov.addr += block_size;
	}

	return ret;
}

static int post_proxy_send(int nic, int gpu, size_t size, int idx, int signaled)
{
	uintptr_t offset = idx * size;
	struct iovec iov;
	void *desc = proxy_buf.mrs[nic] ? fi_mr_desc(proxy_buf.mrs[nic]) : NULL;
	struct fi_msg msg;
	size_t sent, block_size = proxy_block;
	int flags = 0;
	int ret;

	iov.iov_base = (char *)proxy_buf.xe_buf.buf + offset;
	iov.iov_len = proxy_block;
	msg.msg_iov = &iov;
	msg.desc = proxy_buf.mrs[nic] ? &desc : NULL;
	msg.iov_count = 1;
	msg.addr = nics[nic].peer_addr;
	msg.context = get_context(context_pool);
	msg.data = 0;

	for (sent = 0; sent < size;) {
		if (block_size >= size - sent) {
			block_size = size - sent;
			iov.iov_len = block_size;
			flags = signaled ? FI_COMPLETION : 0;
		}

		xe_copy_buf((char *)proxy_buf.xe_buf.buf + offset + sent,
			    (char *)bufs[gpu].xe_buf.buf + offset + sent,
			    block_size, gpu);

try_again:
		ret = fi_sendmsg(nics[nic].ep, &msg, flags);
		if (ret == -FI_EAGAIN) {
			fi_cq_read(nics[nic].cq, NULL, 0);
			goto try_again;
		} else if (ret) {
			break;
		}

		sent += block_size;
		iov.iov_base = (char *)iov.iov_base + block_size;
	}

	return ret;
}

static int post_sync_send(int nic, size_t size)
{
	struct iovec iov;
	void *desc = sync_buf.mrs[nic] ? fi_mr_desc(sync_buf.mrs[nic]) : NULL;
	struct fi_msg msg;
	int ret;

	iov.iov_base = (char *)sync_buf.xe_buf.buf;
	iov.iov_len = size;
	msg.msg_iov = &iov;
	msg.desc = sync_buf.mrs[nic] ? &desc : NULL;
	msg.iov_count = 1;
	msg.addr = nics[nic].peer_addr;
	msg.context = get_context(context_pool);
	msg.data = 0;

try_again:
	ret = fi_sendmsg(nics[nic].ep, &msg, FI_COMPLETION);
	if (ret == -FI_EAGAIN) {
		fi_cq_read(nics[nic].cq, NULL, 0);
		goto try_again;
	}
	return ret;
}

static int post_sync_recv(int nic, size_t size)
{
	struct iovec iov;
	void *desc = sync_buf.mrs[nic] ? fi_mr_desc(sync_buf.mrs[nic]) : NULL;
	struct fi_msg msg;
	int ret;

	iov.iov_base = (char *)sync_buf.xe_buf.buf;
	iov.iov_len = size;
	msg.msg_iov = &iov;
	msg.desc = sync_buf.mrs[nic] ? &desc : NULL;
	msg.iov_count = 1;
	msg.addr = nics[nic].peer_addr;
	msg.context = get_context(context_pool);
	msg.data = 0;

try_again:
	ret = fi_recvmsg(nics[nic].ep, &msg, FI_COMPLETION);
	if (ret == -FI_EAGAIN) {
		fi_cq_read(nics[nic].cq, NULL, 0);
		goto try_again;
	}
	return ret;
}

static int post_send(int nic, int gpu, size_t size, int idx, int signaled)
{
	struct iovec iov;
	void *desc = bufs[gpu].mrs[nic] ? fi_mr_desc(bufs[gpu].mrs[nic]) : NULL;
	struct fi_msg msg;
	int ret;

	iov.iov_base = (char *)bufs[gpu].xe_buf.buf + idx * size;
	iov.iov_len = size;
	msg.msg_iov = &iov;
	msg.desc = bufs[gpu].mrs[nic] ? &desc : NULL;
	msg.iov_count = 1;
	msg.addr = nics[nic].peer_addr;
	msg.context = get_context(context_pool);
	msg.data = 0;

try_again:
	ret = fi_sendmsg(nics[nic].ep, &msg, signaled ? FI_COMPLETION : 0);
	if (ret == -FI_EAGAIN) {
		fi_cq_read(nics[nic].cq, NULL, 0);
		goto try_again;
	}
	return ret;
}

static int post_recv(int nic, int gpu, size_t size, int idx)
{
	struct iovec iov;
	void *desc = bufs[gpu].mrs[nic] ? fi_mr_desc(bufs[gpu].mrs[nic]) : NULL;
	struct fi_msg msg;
	int ret;

	iov.iov_base = (char *)bufs[gpu].xe_buf.buf + idx * size;
	iov.iov_len = size;
	msg.msg_iov = &iov;
	msg.desc = bufs[gpu].mrs[nic] ? &desc : NULL;
	msg.iov_count = 1;
	msg.addr = nics[nic].peer_addr;
	msg.context = get_context(context_pool);
	msg.data = 0;

try_again:
	ret = fi_recvmsg(nics[nic].ep, &msg, FI_COMPLETION);
	if (ret == -FI_EAGAIN) {
		fi_cq_read(nics[nic].cq, NULL, 0);
		goto try_again;
	}
	return ret;
}

static inline void wait_completion(int n)
{
	struct fi_cq_entry wc[16];
	struct fi_cq_err_entry error;
	int ret, completed = 0;
	int i, j;

	while (completed < n) {
		for (i = 0; i < num_nics; i++) {
			ret = fi_cq_read(nics[i].cq, wc, 16);
			if (ret == -FI_EAGAIN)
				continue;
			if (ret < 0) {
				fi_cq_readerr(nics[i].cq, &error, 0);
				fprintf(stderr,
					"Completion with error: %s (err %d prov_errno %d).\n",
					fi_strerror(error.err), error.err,
					error.prov_errno);
				return;
			}
			for (j = 0; j < ret; j++)
				put_context(context_pool, wc[j].op_context);
			completed += ret;
		}
	}
}

static void sync_ofi(size_t size)
{
	int i;

	for (i = 0; i < num_nics; i++) {
		EXIT_ON_ERROR(post_sync_recv(i, size));
		EXIT_ON_ERROR(post_sync_send(i, size));
	}

	wait_completion(num_nics * 2);
	return;
}

static void sync_send(size_t size)
{
	int i;

	for (i = 0; i < num_nics; i++)
		EXIT_ON_ERROR(post_sync_send(i, size));
	wait_completion(num_nics);
	return;
}

static void sync_recv(size_t size)
{
	int i;

	for (i = 0; i < num_nics; i++)
		EXIT_ON_ERROR(post_sync_recv(i, size));
	wait_completion(num_nics);
	return;
}

int run_test(int test_type, size_t size, int iters, int batch, int output_result)
{
	int i, j, k, completed, pending;
	int n, n0;
	double t1, t2;
	struct fi_cq_entry wc[16];
	struct fi_cq_err_entry error;
	int signaled;
	int nic, gpu, rgpu;

	i = completed = pending = 0;

	if (test_type == RECV) {
		batch = 1;
		for (; i < prepost; i++) {
			nic = i % num_nics;
			gpu = i % num_gpus;
			CHECK_ERROR(post_recv(nic, gpu, size, i % batch));
			pending++;
		}
	}

	t1 = when();
	while (i < iters || completed < iters) {
		while (i < iters && pending < TX_DEPTH) {
			nic = i % num_nics;
			gpu = i % num_gpus;
			rgpu = i % peer.num_gpus;
			signaled = ((i / num_nics) % batch) == batch -1 ||
				   i >= iters - num_nics;
			switch (test_type) {
			case WRITE:
				if (buf_location == DEVICE && use_proxy)
					CHECK_ERROR(post_proxy_write(nic, gpu,
								     rgpu, size,
								     i % batch,
								     signaled));
				else
					CHECK_ERROR(post_rdma(nic, gpu, rgpu,
							      test_type, size,
							      i % batch,
							      signaled));
				break;
			case READ:
				CHECK_ERROR(post_rdma(nic, gpu, rgpu,
						      test_type, size,
						      i % batch, signaled));
				break;
			case SEND:
				if (buf_location == DEVICE && use_proxy)
					CHECK_ERROR(post_proxy_send(nic, gpu,
								    size,
								    i % batch,
								    signaled));
				else
					CHECK_ERROR(post_send(nic, gpu, size,
							      i % batch, signaled));
				break;
			case RECV:
				CHECK_ERROR(post_recv(nic, gpu, size, i % batch));
				break;
			}
			nic = (nic + 1) % num_nics;
			pending++;
			i++;
		}
		do {
			n0 = 0;
			for (j = 0; j < num_nics; j++) {
				n = fi_cq_read(nics[j].cq, wc, 16);
				if (n == -FI_EAGAIN) {
					continue;
				} else if (n < 0) {
					fi_cq_readerr(nics[j].cq, &error, 0);
					fprintf(stderr,
						"Completion with error: %s (err %d prov_errno %d).\n",
						fi_strerror(error.err),
						error.err, error.prov_errno);
					return -1;
				} else {
					for (k = 0; k < n; k++)
						put_context(context_pool, wc[k].op_context);
					pending -= n * batch;
					completed += n * batch;
					n0 += n;
				}
			}
		} while (n0 > 0);
	}

	if (test_type == SEND)
		sync_recv(4);

	if (test_type == RECV)
		sync_send(4);

	t2 = when();

	if (test_type == RECV)
		return 0;

	if (output_result)
		printf("%10zd (x %4d) %10.2lf us %12.2lf MB/s\n", size, iters,
		       (t2 - t1), (long)size * iters / (t2 - t1));

	return 0;

err_out:
	printf("%10zd aborted due to fail to post read request\n", size);
	return -1;
}

static void usage(char *prog)
{
	printf("Usage: %s [options][server_name]\n", prog);
	printf("Options:\n");
	printf("\t-m <location>    Where to allocate the buffer, can be 'malloc', 'host', 'device' or 'shared', default: malloc\n");
	printf("\t-d <gpu_devs>    Use the GPU device(s) specified as comma separated list of <dev>[.<subdev>], default: 0\n");
	printf("\t-e <ep_type>     Set the endpoint type, can be 'rdm' or 'msg', default: rdm\n");
	printf("\t-p <prov_name>   Use the OFI provider named as <prov_name>, default: the first one\n");
	printf("\t-D <domain_names> Open OFI domain(s) specified as comma separated list of <domain_name>, default: automatic\n");
	printf("\t-n <iters>       Set the number of iterations for each message size, default: 1000\n");
	printf("\t-b <batch>       Generate completion for every <batch> iterations (default: 1)\n");
	printf("\t-S <size>        Set the message size to test (0: all, -1: none), can use suffix K/M/G, default: 0\n");
	printf("\t-M <size>        Set the maximum message size to test, can use suffix K/M/G, default: 4194304 (4M)\n");
	printf("\t-t <test_type>   Type of test to perform, can be 'read', 'write', or 'send', default: read\n");
	printf("\t-P               Proxy device buffer through host buffer (for write and send only), default: off\n");
	printf("\t-B <block_size>  Set the block size for proxying, default: maximum message size\n");
	printf("\t-r               Reverse the direction of data movement (server initates RDMA ops)\n");
	printf("\t-R               Enable dmabuf_reg (plug-in for MOFED peer-memory)\n");
	printf("\t-s               Sync with send/recv at the end\n");
	printf("\t-2               Run the test in both direction (for 'read' and 'write' only)\n");
	printf("\t-x <num_recv>    Prepost <num_recv> recieves (for 'send' only)\n");
	printf("\t-v               Verify the data (for read test only)\n");
	printf("\t-h               Print this message\n");
}

static inline int string_to_location(char *s, int default_loc)
{
	int loc;

	if (strcasecmp(s, "malloc") == 0)
		loc = MALLOC;
	else if (strcasecmp(s, "host") == 0)
		loc = HOST;
	else if (strcasecmp(s, "device") == 0)
		loc = DEVICE;
	else if (strcasecmp(s, "shared") == 0)
		loc = SHARED;
	else
		loc = default_loc;

	return loc;
}

void parse_buf_location(char *string, int *loc1, int *loc2, int default_loc)
{
	char *s;
	char *saveptr;

	s = strtok_r(string, ":", &saveptr);
	if (s) {
		*loc1 = string_to_location(s, default_loc);
		s = strtok_r(NULL, ":", &saveptr);
		if (s)
			*loc2 = string_to_location(s, default_loc);
		else
			*loc2 = *loc1;
	} else {
		*loc1 = *loc2 = default_loc;
	}
}

size_t parse_size(char *string)
{
	size_t size = MAX_SIZE;
	char unit = '\0';

	sscanf(string, "%zd%c", &size, &unit);

	if (unit == 'k' || unit == 'K')
		size *= 1024;
	else if (unit == 'm' || unit == 'M')
		size *= 1024 * 1024;
	else if (unit == 'g' || unit == 'G')
		size *= 1024 * 1024 * 1024;

	return size;
}

int main(int argc, char *argv[])
{
	char *gpu_dev_nums = NULL;
	int enable_multi_gpu;
	unsigned int port = 12345;
	int test_type = READ;
	int iters = 1000;
	int reverse = 0;
	int bidir = 0;
	int sockfd;
	size_t size;
	int c;
	int initiator;
	ssize_t msg_size = 0;
	size_t warm_up_size;
	int err = 0;
	int rank;
	char *s;
	int loc1 = MALLOC, loc2 = MALLOC;

	while ((c = getopt(argc, argv, "2b:d:D:e:p:m:M:n:t:gPB:rRsS:x:hv")) != -1) {
		switch (c) {
		case '2':
			bidir = 1;
			break;
		case 'b':
			batch = atoi(optarg);
			if (batch <= 0) {
				fprintf(stderr,
					"Batch too small, adjusted to 1\n");
				batch = 1;
			} else if (batch > TX_DEPTH) {
				fprintf(stderr,
					"Batch too large, adjusted to %d\n",
					TX_DEPTH);
				batch = TX_DEPTH;
			}
			break;
		case 'd':
			gpu_dev_nums = strdup(optarg);
			break;
		case 'D':
			domain_names = strdup(optarg);
			break;
		case 'e':
			if (strcasecmp(optarg, "rdm") == 0)
				ep_type = FI_EP_RDM;
			else if (strcasecmp(optarg, "msg") == 0)
				ep_type = FI_EP_MSG;
			break;
		case 'p':
			prov_name = strdup(optarg);
			break;
		case 'm':
			parse_buf_location(optarg, &loc1, &loc2, MALLOC);
			break;
		case 'n':
			iters = atoi(optarg);
			break;
		case 't':
			if (strcasecmp(optarg, "read") == 0)
				test_type = READ;
			else if (strcasecmp(optarg, "write") == 0)
				test_type = WRITE;
			else if (strcasecmp(optarg, "send") == 0)
				test_type = SEND;
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
		case 'r':
			reverse = 1;
			break;
		case 'R':
			use_dmabuf_reg = 1;
			break;
		case 's':
			use_sync_ofi = 1;
			break;
		case 'S':
			msg_size = parse_size(optarg);
			break;
		case 'M':
			max_size = parse_size(optarg);
			proxy_block = max_size;
			break;
		case 'x':
			prepost = atoi(optarg);
			break;
		case 'v':
			verify = 1;
			break;
		default:
			usage(argv[0]);
			exit(-1);
			break;
		}
	}

	if (msg_size > 0 && msg_size > max_size) {
		max_size = msg_size;
		proxy_block = msg_size;
		fprintf(stderr,
			"Max_size smaller than message size, adjusted to %zd\n",
			max_size);
	}

	if (max_size * batch / batch != max_size) {
		fprintf(stderr,
			"Buffer_size = Max_size (%zd) * Batch (%d) overflows\n",
			max_size, batch);
		exit(-1);
	}

	if (argc > optind) {
		client = 1;
		server_name = strdup(argv[optind]);
	}

	/*
	 * If started by a job launcher, perform pair-wise test.
	 */
	s = getenv("PMI_RANK");
	if (s) {
		rank = atoi(s);
		client = rank % 2;
		port += rank >> 1;
		if (!client && server_name) {
			free(server_name);
			server_name = NULL;
		}
	}

	buf_location = client ? loc2 : loc1;

	sockfd = connect_tcp(server_name, port);
	if (sockfd < 0) {
		fprintf(stderr, "Cannot create socket connection\n");
		exit(-1);
	}

	initiator = (!reverse && server_name) || (reverse && !server_name);

	if (use_dmabuf_reg)
		dmabuf_reg_open();

	/* multi-GPU test doesn't make sense if buffers are on the host */
	enable_multi_gpu = buf_location != MALLOC && buf_location != HOST;
	num_gpus = xe_init(gpu_dev_nums, enable_multi_gpu);

	init_buf(max_size * batch, initiator ? 'A' : 'a');
	init_ofi(sockfd, server_name, port + 1000, test_type);

	sync_tcp(sockfd);
	printf("Warming up ...\n");
	warm_up_size = msg_size > 0 ? msg_size : 1;
	if (initiator) {
		run_test(test_type, warm_up_size, 16, 1, 0);
		sync_send(4);
	} else {
		if (test_type == SEND)
			run_test(RECV, warm_up_size, 16, 1, 0);
		else if (bidir)
			run_test(test_type, warm_up_size, 16, 1, 0);
		sync_recv(4);
	}

	sync_tcp(sockfd);
	printf("Start test ...\n");
	for (size = 1; size <= max_size && !err; size <<= 1) {
		if (msg_size < 0)
			break;
		else if (msg_size > 0)
			size = msg_size;
		if (initiator) {
			err = run_test(test_type, size, iters, batch, 1);
			sync_send(4);
		} else {
			if (test_type == SEND)
				err = run_test(RECV, size, iters, 1, 1);
			else if (bidir)
				err = run_test(test_type, size, iters, batch, 1);
			sync_recv(4);
		}
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

	if (use_sync_ofi)
		sync_ofi(4);

	finalize_ofi();
	free_buf();

	if (use_dmabuf_reg)
		dmabuf_reg_close();

	close(sockfd);

	return 0;
}

