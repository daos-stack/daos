/*
 * Copyright (c) 2013-2018 Intel Corporation.  All rights reserved.
 * Copyright (c) 2016 Cray Inc.  All rights reserved.
 * Copyright (c) 2014-2017, Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2021 Amazon.com, Inc. or its affiliates. All rights reserved.
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

#include <assert.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_atomic.h>
#include <rdma/fi_collective.h>

#include "shared.h"
#include "hmem.h"

struct fi_info *fi_pep, *fi, *hints;
struct fid_fabric *fabric;
struct fid_wait *waitset;
struct fid_domain *domain;
struct fid_poll *pollset;
struct fid_pep *pep;
struct fid_ep *ep, *alias_ep;
struct fid_cq *txcq, *rxcq;
struct fid_cntr *txcntr, *rxcntr, *rma_cntr;

struct fid_ep *srx;
struct fid_stx *stx;
struct fid_mr *mr;
void *mr_desc = NULL;
struct fid_av *av = NULL;
struct fid_eq *eq;
struct fid_mc *mc;

struct fid_mr no_mr;
struct fi_context tx_ctx, rx_ctx;
struct ft_context *tx_ctx_arr = NULL, *rx_ctx_arr = NULL;
uint64_t remote_cq_data = 0;

uint64_t tx_seq, rx_seq, tx_cq_cntr, rx_cq_cntr;
int (*ft_mr_alloc_func)(void);
uint64_t ft_tag = 0;
int ft_parent_proc = 0;
pid_t ft_child_pid = 0;
int ft_socket_pair[2];

fi_addr_t remote_fi_addr = FI_ADDR_UNSPEC;
char *buf = NULL, *tx_buf, *rx_buf;
/*
 * dev_host_buf are used by ft_fill_buf() to stage data sent over wire,
 * when tx_buf is on device memory.
 */
void *dev_host_buf = NULL;

char **tx_mr_bufs = NULL, **rx_mr_bufs = NULL;
size_t buf_size, tx_buf_size, rx_buf_size;
size_t tx_size, rx_size, tx_mr_size, rx_mr_size;
int rx_fd = -1, tx_fd = -1;
char default_port[8] = "9228";
static char default_oob_port[8] = "3000";
const char *greeting = "Hello from Client!";


char test_name[50] = "custom";
int timeout = -1;
struct timespec start, end;

int listen_sock = -1;
int sock = -1;
int oob_sock = -1;

struct fi_av_attr av_attr = {
	.type = FI_AV_MAP,
	.count = 1
};
struct fi_eq_attr eq_attr = {
	.wait_obj = FI_WAIT_UNSPEC
};
struct fi_cq_attr cq_attr = {
	.wait_obj = FI_WAIT_NONE
};
struct fi_cntr_attr cntr_attr = {
	.events = FI_CNTR_EVENTS_COMP,
	.wait_obj = FI_WAIT_NONE
};

struct fi_rma_iov remote;

struct ft_opts opts;

struct test_size_param def_test_sizes[] = {
	{ 1 <<  0, 0 },
	{ 1 <<  1, 0 }, { (1 <<  1) + (1 <<  0), 0 },
	{ 1 <<  2, 0 }, { (1 <<  2) + (1 <<  1), 0 },
	{ 1 <<  3, 0 }, { (1 <<  3) + (1 <<  2), 0 },
	{ 1 <<  4, 0 }, { (1 <<  4) + (1 <<  3), 0 },
	{ 1 <<  5, 0 }, { (1 <<  5) + (1 <<  4), 0 },
	{ 1 <<  6, FT_DEFAULT_SIZE }, { (1 <<  6) + (1 <<  5), 0 },
	{ 1 <<  7, 0 }, { (1 <<  7) + (1 <<  6), 0 },
	{ 1 <<  8, FT_DEFAULT_SIZE }, { (1 <<  8) + (1 <<  7), 0 },
	{ 1 <<  9, 0 }, { (1 <<  9) + (1 <<  8), 0 },
	{ 1 << 10, FT_DEFAULT_SIZE }, { (1 << 10) + (1 <<  9), 0 },
	{ 1 << 11, 0 }, { (1 << 11) + (1 << 10), 0 },
	{ 1 << 12, FT_DEFAULT_SIZE }, { (1 << 12) + (1 << 11), 0 },
	{ 1 << 13, 0 }, { (1 << 13) + (1 << 12), 0 },
	{ 1 << 14, 0 }, { (1 << 14) + (1 << 13), 0 },
	{ 1 << 15, 0 }, { (1 << 15) + (1 << 14), 0 },
	{ 1 << 16, FT_DEFAULT_SIZE }, { (1 << 16) + (1 << 15), 0 },
	{ 1 << 17, 0 }, { (1 << 17) + (1 << 16), 0 },
	{ 1 << 18, 0 }, { (1 << 18) + (1 << 17), 0 },
	{ 1 << 19, 0 }, { (1 << 19) + (1 << 18), 0 },
	{ 1 << 20, FT_DEFAULT_SIZE }, { (1 << 20) + (1 << 19), 0 },
	{ 1 << 21, 0 }, { (1 << 21) + (1 << 20), 0 },
	{ 1 << 22, 0 }, { (1 << 22) + (1 << 21), 0 },
	{ 1 << 23, 0 },
};

unsigned int test_cnt = (sizeof def_test_sizes / sizeof def_test_sizes[0]);

struct test_size_param *test_size = def_test_sizes;
/* range of messages is dynamically allocated */
struct test_size_param *user_test_sizes;

static const char integ_alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const int integ_alphabet_length = (sizeof(integ_alphabet)/sizeof(*integ_alphabet)) - 1;


int ft_poll_fd(int fd, int timeout)
{
	struct pollfd fds;
	int ret;

	fds.fd = fd;
	fds.events = POLLIN;
	ret = poll(&fds, 1, timeout);
	if (ret == -1) {
		FT_PRINTERR("poll", -errno);
		ret = -errno;
	} else if (!ret) {
		ret = -FI_EAGAIN;
	} else {
		ret = 0;
	}
	return ret;
}

size_t ft_tx_prefix_size(void)
{
	return (fi->tx_attr->mode & FI_MSG_PREFIX) ?
		fi->ep_attr->msg_prefix_size : 0;
}

size_t ft_rx_prefix_size(void)
{
	return (fi->rx_attr->mode & FI_MSG_PREFIX) ?
		fi->ep_attr->msg_prefix_size : 0;
}

int ft_check_opts(uint64_t flags)
{
	return (opts.options & flags) == flags;
}

static void ft_cq_set_wait_attr(void)
{
	switch (opts.comp_method) {
	case FT_COMP_SREAD:
		cq_attr.wait_obj = FI_WAIT_UNSPEC;
		cq_attr.wait_cond = FI_CQ_COND_NONE;
		break;
	case FT_COMP_WAITSET:
		assert(waitset);
		cq_attr.wait_obj = FI_WAIT_SET;
		cq_attr.wait_cond = FI_CQ_COND_NONE;
		cq_attr.wait_set = waitset;
		break;
	case FT_COMP_WAIT_FD:
		cq_attr.wait_obj = FI_WAIT_FD;
		cq_attr.wait_cond = FI_CQ_COND_NONE;
		break;
	case FT_COMP_YIELD:
		cq_attr.wait_obj = FI_WAIT_YIELD;
		cq_attr.wait_cond = FI_CQ_COND_NONE;
		break;
	default:
		cq_attr.wait_obj = FI_WAIT_NONE;
		break;
	}
}

static void ft_cntr_set_wait_attr(void)
{
	switch (opts.comp_method) {
	case FT_COMP_SREAD:
		cntr_attr.wait_obj = FI_WAIT_UNSPEC;
		break;
	case FT_COMP_WAITSET:
		assert(waitset);
		cntr_attr.wait_obj = FI_WAIT_SET;
		break;
	case FT_COMP_WAIT_FD:
		cntr_attr.wait_obj = FI_WAIT_FD;
		break;
	case FT_COMP_YIELD:
		cntr_attr.wait_obj = FI_WAIT_YIELD;
		break;
	default:
		cntr_attr.wait_obj = FI_WAIT_NONE;
		break;
	}
}

int ft_cntr_open(struct fid_cntr **cntr)
{
	ft_cntr_set_wait_attr();
	return fi_cntr_open(domain, &cntr_attr, cntr, cntr);
}

static inline int ft_rma_read_target_allowed(uint64_t caps)
{
	if (caps & (FI_RMA | FI_ATOMIC)) {
		if (caps & FI_REMOTE_READ)
			return 1;
		return !(caps & (FI_READ | FI_WRITE | FI_REMOTE_WRITE));
	}
	return 0;
}

static inline int ft_rma_write_target_allowed(uint64_t caps)
{
	if (caps & (FI_RMA | FI_ATOMIC)) {
		if (caps & FI_REMOTE_WRITE)
			return 1;
		return !(caps & (FI_READ | FI_WRITE | FI_REMOTE_WRITE));
	}
	return 0;
}

static inline int ft_check_mr_local_flag(struct fi_info *info)
{
	return ((info->mode & FI_LOCAL_MR) ||
		(info->domain_attr->mr_mode & FI_MR_LOCAL));
}

uint64_t ft_info_to_mr_access(struct fi_info *info)
{
	uint64_t mr_access = 0;
	if (ft_check_mr_local_flag(info)) {
		if (info->caps & (FI_MSG | FI_TAGGED)) {
			if (info->caps & FT_MSG_MR_ACCESS) {
				mr_access |= info->caps & FT_MSG_MR_ACCESS;
			} else {
				mr_access |= FT_MSG_MR_ACCESS;
			}
		}

		if (info->caps & (FI_RMA | FI_ATOMIC)) {
			if (info->caps & FT_RMA_MR_ACCESS) {
				mr_access |= info->caps & FT_RMA_MR_ACCESS;
			} else	{
				mr_access |= FT_RMA_MR_ACCESS;
			}
		}
	} else {
		if (info->caps & (FI_RMA | FI_ATOMIC)) {
			if (ft_rma_read_target_allowed(info->caps)) {
				mr_access |= FI_REMOTE_READ;
			}
			if (ft_rma_write_target_allowed(info->caps)) {
				mr_access |= FI_REMOTE_WRITE;
			}
		}
	}
	return mr_access;
}

#define bit_isset(x, i) (x >> i & 1)
#define for_each_bit(x, i) for (i = 0; i < (8 * sizeof(x)); i++)

static inline int bit_set_count(uint64_t val)
{
	int cnt = 0;
	while (val) {
		cnt++;
		val &= val - 1;
	}
	return cnt;
}

int ft_alloc_bit_combo(uint64_t fixed, uint64_t opt,
		       uint64_t **combos, int *len)
{
	uint64_t *flags;
	int i, num_flags;
	uint64_t index;
	int ret;

	num_flags = bit_set_count(opt) + 1;
	flags = calloc(num_flags, sizeof(fixed));
	if (!flags) {
		perror("calloc");
		return -FI_ENOMEM;
	}

	*len = 1 << (num_flags - 1);
	*combos = calloc(*len, sizeof(fixed));
	if (!(*combos)) {
		perror("calloc");
		ret = -FI_ENOMEM;
		goto clean;
	}

	num_flags = 0;
	for_each_bit(opt, i) {
		if (bit_isset(opt, i))
			flags[num_flags++] = 1ULL << i;
	}

	for (index = 0; index < (*len); index++) {
		(*combos)[index] = fixed;
		for_each_bit(index, i) {
			if (bit_isset(index, i))
				(*combos)[index] |= flags[i];
		}
	}
	ret = FI_SUCCESS;

clean:
	free(flags);
	return ret;
}

void ft_free_bit_combo(uint64_t *combo)
{
	free(combo);
}

void ft_fill_mr_attr(struct iovec *iov, struct fi_mr_dmabuf *dmabuf,
		     int iov_count, uint64_t access,
		     uint64_t key, enum fi_hmem_iface iface, uint64_t device,
		     struct fi_mr_attr *attr, uint64_t flags)
{
	if (flags & FI_MR_DMABUF)
		attr->dmabuf = dmabuf;
	else
		attr->mr_iov = iov;
	attr->iov_count = iov_count;
	attr->access = access;
	attr->offset = 0;
	attr->requested_key = key;
	attr->context = NULL;
	attr->iface = iface;

	switch (iface) {
	case FI_HMEM_NEURON:
		attr->device.neuron = device;
		break;
	case FI_HMEM_ZE:
		attr->device.ze = fi_hmem_ze_device(0, device);
		break;
	case FI_HMEM_CUDA:
		attr->device.cuda = device;
		break;
	default:
		break;
	}
}

bool ft_need_mr_reg(struct fi_info *fi)
{
	return (fi->caps & (FI_RMA | FI_ATOMIC)) ||
	       (fi->domain_attr->mr_mode & FI_MR_LOCAL) ||
	       ((fi->domain_attr->mr_mode & FI_MR_HMEM) &&
		(opts.options & FT_OPT_USE_DEVICE));
}

/**
 * @brief Update an array of fi_mr_dmabuf objects
 * from an array of iov with the same iov count
 *
 * @param dmabuf ptr to the array of fi_mr_dmabuf objects
 * @param iov ptr to the array of iov objects
 * @param iov_count iov count
 * @param iface hmem iface
 * @return int FI_SUCCESS on success, negative integer upon
 * error
 */
int ft_get_dmabuf_from_iov(struct fi_mr_dmabuf *dmabuf,
			   struct iovec *iov, size_t iov_count,
			   enum fi_hmem_iface iface)
{
	int ret, i;
	int dmabuf_fd;
	uint64_t dmabuf_offset;

	for (i = 0; i < iov_count; i++) {
		ret = ft_hmem_get_dmabuf_fd(iface,
			iov[i].iov_base, iov[i].iov_len,
			&dmabuf_fd, &dmabuf_offset);
		if (ret)
			return ret;

		dmabuf[i].fd = dmabuf_fd;
		dmabuf[i].offset = dmabuf_offset;
		dmabuf[i].len = iov[i].iov_len;
		dmabuf[i].base_addr = (void *)(
			(uintptr_t) iov[i].iov_base - dmabuf_offset);
	}
	return FI_SUCCESS;
}

int ft_reg_mr(struct fi_info *fi, void *buf, size_t size, uint64_t access,
	      uint64_t key, enum fi_hmem_iface iface, uint64_t device,
	      struct fid_mr **mr, void **desc)
{
	struct fi_mr_attr attr = {0};
	struct iovec iov = {0};
	int ret;
	uint64_t flags;
	int dmabuf_fd;
	uint64_t dmabuf_offset;
	struct fi_mr_dmabuf dmabuf = {0};

	if (!ft_need_mr_reg(fi))
		return 0;

	iov.iov_base = buf;
	iov.iov_len = size;

	flags = (iface) ? FI_HMEM_DEVICE_ONLY : 0;

	if (opts.options & FT_OPT_REG_DMABUF_MR) {
		ret = ft_hmem_get_dmabuf_fd(iface, buf, size,
					    &dmabuf_fd, &dmabuf_offset);
		if (ret)
			return ret;

		dmabuf.fd = dmabuf_fd;
		dmabuf.offset = dmabuf_offset;
		dmabuf.len = size;
		dmabuf.base_addr = (void *)((uintptr_t) buf - dmabuf_offset);
		flags |= FI_MR_DMABUF;
	}

	ft_fill_mr_attr(&iov, &dmabuf, 1, access, key, iface, device, &attr, flags);
	ret = fi_mr_regattr(domain, &attr, flags, mr);
	if (ret)
		return ret;

	if (desc)
		*desc = fi_mr_desc(*mr);

        if (fi->domain_attr->mr_mode & FI_MR_ENDPOINT) {
		ret = fi_mr_bind(*mr, &ep->fid, 0);
		if (ret)
			return ret;

		ret = fi_mr_enable(*mr);
		if (ret)
			return ret;
	}

	return FI_SUCCESS;
}

static int ft_alloc_ctx_array(struct ft_context **mr_array, char ***mr_bufs,
			      char *default_buf, size_t mr_size,
			      uint64_t start_key)
{
	int i, ret;
	uint64_t access = ft_info_to_mr_access(fi);
	struct ft_context *context;

	*mr_array = calloc(opts.window_size, sizeof(**mr_array));
	if (!*mr_array)
		return -FI_ENOMEM;

	if (opts.options & FT_OPT_ALLOC_MULT_MR) {
		*mr_bufs = calloc(opts.window_size, sizeof(**mr_bufs));
		if (!*mr_bufs)
			return -FI_ENOMEM;
	}

	for (i = 0; i < opts.window_size; i++) {
		context = &(*mr_array)[i];
		if (!(opts.options & FT_OPT_ALLOC_MULT_MR)) {
			context->buf = default_buf + mr_size * i;
			context->mr = mr;
			context->desc = mr_desc;
			continue;
		}
		ret = ft_hmem_alloc(opts.iface, opts.device,
				    (void **) &((*mr_bufs)[i]), mr_size);
		if (ret)
			return ret;

		context->buf = (*mr_bufs)[i];

		ret = ft_reg_mr(fi, context->buf, mr_size, access,
				start_key + i, opts.iface, opts.device,
				&context->mr, &context->desc);
		if (ret)
			return ret;
	}

	return 0;
}

static void ft_set_tx_rx_sizes(size_t *set_tx, size_t *set_rx)
{
	*set_tx = opts.options & FT_OPT_SIZE ?
		  opts.transfer_size : test_size[TEST_CNT - 1].size;
	if (*set_tx > fi->ep_attr->max_msg_size)
		*set_tx = fi->ep_attr->max_msg_size;
	*set_rx = *set_tx + ft_rx_prefix_size();
	*set_tx += ft_tx_prefix_size();
}

void ft_free_host_tx_buf(void)
{
	int ret;

	ret = ft_hmem_free_host(opts.iface, dev_host_buf);
	if (ret)
		FT_PRINTERR("ft_hmem_free_host", ret);
	dev_host_buf = NULL;
}

/*
 * Include FI_MSG_PREFIX space in the allocated buffer, and ensure that the
 * buffer is large enough for a control message used to exchange addressing
 * data.
 */
int ft_alloc_msgs(void)
{
	int ret;
	int rma_resv_bytes;
	long alignment = 64;
	size_t max_msg_size;

	if (buf)
		return 0;

	if (ft_check_opts(FT_OPT_SKIP_MSG_ALLOC))
		return 0;

	if (opts.options & FT_OPT_ALLOC_MULT_MR) {
		ft_set_tx_rx_sizes(&tx_mr_size, &rx_mr_size);
		rx_size = FT_MAX_CTRL_MSG + ft_rx_prefix_size();
		tx_size = FT_MAX_CTRL_MSG + ft_tx_prefix_size();
		rx_buf_size = rx_size;
		tx_buf_size = tx_size;
	} else {
		ft_set_tx_rx_sizes(&tx_size, &rx_size);
		tx_mr_size = 0;
		rx_mr_size = 0;
		rx_buf_size = MAX(rx_size, FT_MAX_CTRL_MSG) * opts.window_size;
		tx_buf_size = MAX(tx_size, FT_MAX_CTRL_MSG) * opts.window_size;
	}

	/* Allow enough space for RMA to operate in a distinct memory
	 * region that ft_sync() won't touch.
	 */
	rma_resv_bytes = FT_RMA_SYNC_MSG_BYTES +
			 MAX( ft_tx_prefix_size(), ft_rx_prefix_size() );
	tx_buf_size += rma_resv_bytes;
	rx_buf_size += rma_resv_bytes;

	if (opts.options & FT_OPT_ALIGN && !(opts.options & FT_OPT_USE_DEVICE)) {
		alignment = sysconf(_SC_PAGESIZE);
		if (alignment < 0)
			return -errno;
	}

	rx_buf_size = ft_get_aligned_size(rx_buf_size, alignment);
	tx_buf_size = ft_get_aligned_size(tx_buf_size, alignment);

	buf_size = rx_buf_size + tx_buf_size;
	if (opts.options & FT_OPT_ALIGN && !(opts.options & FT_OPT_USE_DEVICE)) {
		ret = posix_memalign((void **) &buf, (size_t) alignment,
				buf_size);
		if (ret) {
			FT_PRINTERR("posix_memalign", ret);
			return ret;
		}
	} else {
		/* allocate extra "alignment" bytes, to handle the case
		 * "buf" returned by ft_hmem_alloc() is not aligned.
		 */
		buf_size += alignment;
		ret = ft_hmem_alloc(opts.iface, opts.device, (void **) &buf,
				    buf_size);
		if (ret)
			return ret;

		max_msg_size = (opts.options & FT_OPT_ALLOC_MULT_MR)
				? tx_mr_size : tx_size;

		/* dev_host_buf is used by ft_fill_buf() and ft_check_buf() as
		 * staging area to copy data to and from device buffer during
		 * data setup and verification.
		 *
		 * its size therefore should be the maximum size that
		 * fi_fill_buf() and ft_check_buf() are called with, which is
		 * max_msg_size * opts.window_size, because tests like
		 * fi_rma_bw initializes all data in a window before
		 * a window started, and check all data in a window after
		 * a window completed.
		 */
		ret = ft_hmem_alloc_host(opts.iface, &dev_host_buf,
					 max_msg_size * opts.window_size);
		if (ret)
			return ret;
	}

	ret = ft_hmem_memset(opts.iface, opts.device, (void *) buf, 0, buf_size);
	if (ret)
		return ret;

	rx_buf = (char *)ft_get_aligned_addr(buf, alignment);
	tx_buf = rx_buf + rx_buf_size;
	remote_cq_data = ft_init_cq_data(fi);

	mr = &no_mr;
	if (!ft_mr_alloc_func && !ft_check_opts(FT_OPT_SKIP_REG_MR)) {
		ret = ft_reg_mr(fi, rx_buf, rx_buf_size + tx_buf_size,
				ft_info_to_mr_access(fi),
				FT_MR_KEY, opts.iface, opts.device, &mr,
				&mr_desc);
		if (ret)
			return ret;
	} else {
		if (ft_mr_alloc_func) {
			assert(!ft_check_opts(FT_OPT_SKIP_REG_MR));
			ret = ft_mr_alloc_func();
			if (ret)
				return ret;
		}
	}

	ret = ft_alloc_ctx_array(&tx_ctx_arr, &tx_mr_bufs, tx_buf,
				 tx_mr_size, FT_TX_MR_KEY);
	if (ret)
		return -FI_ENOMEM;

	ret = ft_alloc_ctx_array(&rx_ctx_arr, &rx_mr_bufs, rx_buf,
				 rx_mr_size, FT_RX_MR_KEY);
	if (ret)
		return -FI_ENOMEM;

	return 0;
}

int ft_open_domain_res(void)
{
	int ret;

	ret = fi_domain(fabric, fi, &domain, NULL);
	if (ret) {
		FT_PRINTERR("fi_domain", ret);
		return ret;
	}

	if (opts.options & FT_OPT_DOMAIN_EQ) {
		ret = fi_domain_bind(domain, &eq->fid, 0);
		if (ret) {
			FT_PRINTERR("fi_domain_bind", ret);
			return ret;
		}
	}

	if (opts.options & FT_OPT_STX) {
		ret = fi_stx_context(domain, fi->tx_attr, &stx, NULL);
		if (ret) {
			FT_PRINTERR("fi_stx_context", ret);
			return ret;
		}
	}

	if (opts.options & FT_OPT_SRX) {
		ret = fi_srx_context(domain, fi->rx_attr, &srx, NULL);
		if (ret) {
			FT_PRINTERR("fi_srx_context", ret);
			return ret;
		}
	}
	return 0;
}

int ft_open_fabric_res(void)
{
	int ret;

	ret = fi_fabric(fi->fabric_attr, &fabric, NULL);
	if (ret) {
		FT_PRINTERR("fi_fabric", ret);
		return ret;
	}

	ret = fi_eq_open(fabric, &eq_attr, &eq, NULL);
	if (ret) {
		FT_PRINTERR("fi_eq_open", ret);
		return ret;
	}

	return ft_open_domain_res();
}

int ft_alloc_ep_res(struct fi_info *fi, struct fid_cq **new_txcq,
		    struct fid_cq **new_rxcq, struct fid_cntr **new_txcntr,
		    struct fid_cntr **new_rxcntr,
		    struct fid_cntr **new_rma_cntr,
		    struct fid_av **new_av)
{
	int ret;

	if (cq_attr.format == FI_CQ_FORMAT_UNSPEC) {
		if (fi->caps & FI_TAGGED)
			cq_attr.format = FI_CQ_FORMAT_TAGGED;
		else
			cq_attr.format = FI_CQ_FORMAT_CONTEXT;
	}

	if (opts.options & FT_OPT_CQ_SHARED) {
		ft_cq_set_wait_attr();
		cq_attr.size = 0;

		if (opts.tx_cq_size)
			cq_attr.size += opts.tx_cq_size;
		else
			cq_attr.size += fi->tx_attr->size;

		if (opts.rx_cq_size)
			cq_attr.size += opts.rx_cq_size;
		else
			cq_attr.size += fi->rx_attr->size;

		ret = fi_cq_open(domain, &cq_attr, new_txcq, new_txcq);
		if (ret) {
			FT_PRINTERR("fi_cq_open", ret);
			return ret;
		}
		*new_rxcq = *new_txcq;
	}

	if (!(opts.options & FT_OPT_CQ_SHARED)) {
		ft_cq_set_wait_attr();
		if (opts.tx_cq_size)
			cq_attr.size = opts.tx_cq_size;
		else
			cq_attr.size = fi->tx_attr->size;

		ret = fi_cq_open(domain, &cq_attr, new_txcq, new_txcq);
		if (ret) {
			FT_PRINTERR("fi_cq_open", ret);
			return ret;
		}
	}

	if (opts.options & FT_OPT_TX_CNTR) {
		ret = ft_cntr_open(new_txcntr);
		if (ret) {
			FT_PRINTERR("fi_cntr_open", ret);
			return ret;
		}
	}

	if (!(opts.options & FT_OPT_CQ_SHARED)) {
		ft_cq_set_wait_attr();
		if (opts.rx_cq_size)
			cq_attr.size = opts.rx_cq_size;
		else
			cq_attr.size = fi->rx_attr->size;

		ret = fi_cq_open(domain, &cq_attr, new_rxcq, new_rxcq);
		if (ret) {
			FT_PRINTERR("fi_cq_open", ret);
			return ret;
		}
	}

	if (opts.options & FT_OPT_RX_CNTR) {
		ret = ft_cntr_open(new_rxcntr);
		if (ret) {
			FT_PRINTERR("fi_cntr_open", ret);
			return ret;
		}

		if (fi->caps & FI_RMA) {
			ret = ft_cntr_open(new_rma_cntr);
			if (ret) {
				FT_PRINTERR("fi_cntr_open", ret);
				return ret;
			}
		}
	}

	if (!*new_av && (fi->ep_attr->type == FI_EP_RDM || fi->ep_attr->type == FI_EP_DGRAM)) {
		if (fi->domain_attr->av_type != FI_AV_UNSPEC)
			av_attr.type = fi->domain_attr->av_type;

		if (opts.av_name) {
			av_attr.name = opts.av_name;
		}
		av_attr.count = opts.av_size;
		ret = fi_av_open(domain, &av_attr, new_av, NULL);
		if (ret) {
			FT_PRINTERR("fi_av_open", ret);
			return ret;
		}
	}
	return 0;
}

int ft_alloc_active_res(struct fi_info *fi)
{
	int ret;
	ret = ft_alloc_ep_res(fi, &txcq, &rxcq, &txcntr, &rxcntr, &rma_cntr, &av);
	if (ret)
		return ret;

	ret = fi_endpoint(domain, fi, &ep, NULL);
	if (ret) {
		FT_PRINTERR("fi_endpoint", ret);
		return ret;
	}

	return 0;
}

int ft_init(void)
{
	int ret;

	tx_seq = 0;
	rx_seq = 0;
	tx_cq_cntr = 0;
	rx_cq_cntr = 0;

	ret = ft_startup();
	if (ret) {
		FT_ERR("ft_startup: %d", ret);
		return ret;
	}

	ret = ft_hmem_init(opts.iface);
	if (ret)
		FT_PRINTERR("ft_hmem_init", ret);
	return ret;
}

int ft_sock_setup(int sock)
{
	int ret, op;

	op = 1;
	ret = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
			  (void *) &op, sizeof(op));
	if (ret)
		return ret;

	ret = ft_fd_nonblock(sock);
	if (ret)
		return ret;

	return 0;
}

int ft_init_oob(void)
{
	struct addrinfo *ai = NULL;
	int ret;
	char *addr = opts.oob_addr;

	if (!(opts.options & FT_OPT_OOB_CTRL) || oob_sock != -1)
		return 0;

	if (!opts.oob_port)
		opts.oob_port = default_oob_port;

	if (!opts.dst_addr) {
		if (!addr)
			addr = opts.src_addr;

		ret = ft_sock_listen(addr, opts.oob_port);
		if (ret)
			return ret;

		oob_sock = accept(listen_sock, NULL, 0);
		if (oob_sock < 0) {
			perror("accept");
			ret = oob_sock;
			return ret;
		}

		ft_close_fd(listen_sock);
	} else {
		if (!addr)
			addr = opts.dst_addr;

		ret = getaddrinfo(addr, opts.oob_port, NULL, &ai);
		if (ret) {
			perror("getaddrinfo");
			return ret;
		}

		oob_sock = socket(ai->ai_family, SOCK_STREAM, 0);
		if (oob_sock < 0) {
			perror("socket");
			ret = oob_sock;
			goto free;
		}

		ret = connect(oob_sock, ai->ai_addr, ai->ai_addrlen);
		if (ret) {
			perror("connect");
			ft_close_fd(oob_sock);
			goto free;
		}
		sleep(1);
	}

	ret = ft_sock_setup(oob_sock);

free:
	if (ai)
		freeaddrinfo(ai);
	return ret;
}

/*
 * Handles a persistent server communicating with multiple clients,
 * one at a time, in sequence.
 */
int ft_accept_next_client() {
	int ret;

	if (!ft_check_opts(FT_OPT_SKIP_MSG_ALLOC) && (fi->caps & (FI_MSG | FI_TAGGED))) {
		/* Initial receive will get remote address for unconnected EPs */
		ret = ft_post_rx(ep, MAX(rx_size, FT_MAX_CTRL_MSG), &rx_ctx);
		if (ret)
			return ret;
	}

	/* Clients may be separate processes, so re-initialize any OOB setup. */
	if (opts.options & FT_OPT_OOB_ADDR_EXCH) {
		ret = ft_reset_oob();
		if (ret)
			return ret;

		ret = ft_sock_sync(oob_sock, 0);
		if (ret)
			return ret;
}
	return ft_init_av();
}

/*
 * Re-initialize the OOB setup.
 */
int ft_reset_oob()
{
	int ret;
	ret = ft_close_oob();
	if (ret) {
		FT_PRINTERR("ft_close_oob", ret);
		return ret;
	}
	ret = ft_init_oob();
	if (ret) {
		FT_PRINTERR("ft_init_oob", ret);
		return ret;
	}
	return 0;
}

int ft_close_oob()
{
	int ret;
	if (oob_sock == -1)
		return 0;
	ret = ft_close_fd(oob_sock);
	if (ret) {
		FT_PRINTERR("ft_close_fd", errno);
		return ret;
	}
	oob_sock = -1;
	return 0;
}

int ft_getinfo(struct fi_info *hints, struct fi_info **info)
{
	char *node, *service;
	uint64_t flags = 0;
	int ret;

	ret = ft_read_addr_opts(&node, &service, hints, &flags, &opts);
	if (ret)
		return ret;

	if (!hints->ep_attr->type)
		hints->ep_attr->type = FI_EP_RDM;

	if (opts.options & FT_OPT_ENABLE_HMEM) {
		hints->caps |= FI_HMEM;
		hints->domain_attr->mr_mode |= FI_MR_HMEM;
	}

	ret = fi_getinfo(FT_FIVERSION, node, service, flags, hints, info);
	if (ret) {
		FT_PRINTERR("fi_getinfo", ret);
		return ret;
	}

	if (!ft_check_prefix_forced(*info, &opts)) {
		FT_ERR("Provider disabled requested prefix mode.");
		return -FI_ENODATA;
	}

	return 0;
}

int ft_init_fabric_cm(void)
{
	int ret;
	if (!opts.dst_addr) {
		ret = ft_start_server();
		if (ret)
			return ret;
	}

	ret = opts.dst_addr ? ft_client_connect() : ft_server_connect();

	return ret;
}

int ft_start_server(void)
{
	int ret;

	ret = ft_init();
	if (ret)
		return ret;

	ret = ft_init_oob();
	if (ret)
		return ret;

	ret = ft_getinfo(hints, &fi_pep);
	if (ret)
		return ret;

	ret = fi_fabric(fi_pep->fabric_attr, &fabric, NULL);
	if (ret) {
		FT_PRINTERR("fi_fabric", ret);
		return ret;
	}

	ret = fi_eq_open(fabric, &eq_attr, &eq, NULL);
	if (ret) {
		FT_PRINTERR("fi_eq_open", ret);
		return ret;
	}

	ret = fi_passive_ep(fabric, fi_pep, &pep, NULL);
	if (ret) {
		FT_PRINTERR("fi_passive_ep", ret);
		return ret;
	}

	ret = fi_pep_bind(pep, &eq->fid, 0);
	if (ret) {
		FT_PRINTERR("fi_pep_bind", ret);
		return ret;
	}

	ret = fi_listen(pep);
	if (ret) {
		FT_PRINTERR("fi_listen", ret);
		return ret;
	}

	return 0;
}

int ft_complete_connect(struct fid_ep *ep, struct fid_eq *eq)
{
	struct fi_eq_cm_entry entry;
	uint32_t event;
	ssize_t rd;
	int ret;

	rd = fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0);
	if (rd != sizeof(entry)) {
		FT_PROCESS_EQ_ERR(rd, eq, "fi_eq_sread", "accept");
		ret = (int) rd;
		return ret;
	}

	if (event != FI_CONNECTED || entry.fid != &ep->fid) {
		fprintf(stderr, "Unexpected CM event %d fid %p (ep %p)\n",
			event, entry.fid, ep);
		ret = -FI_EOTHER;
		return ret;
	}

	return 0;
}

int ft_verify_info(struct fi_info *fi_pep, struct fi_info *info)
{
	if (!info || !info->fabric_attr || !info->domain_attr ||
	    !info->ep_attr || !info->tx_attr || !info->rx_attr)
		return -FI_EINVAL;

	if (!info->fabric_attr->prov_name ||
	    !info->fabric_attr->name || !info->domain_attr->name ||
	    info->fabric_attr->api_version != fi_pep->fabric_attr->api_version)
		return -FI_EINVAL;

	return 0;
}

int ft_retrieve_conn_req(struct fid_eq *eq, struct fi_info **fi)
{
	struct fi_eq_cm_entry entry;
	uint32_t event;
	ssize_t rd;
	int ret;

	rd = fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0);
	if (rd != sizeof entry) {
		FT_PROCESS_EQ_ERR(rd, eq, "fi_eq_sread", "listen");
		return (int) rd;
	}

	*fi = entry.info;
	if (event != FI_CONNREQ) {
		fprintf(stderr, "Unexpected CM event %d\n", event);
		ret = -FI_EOTHER;
		return ret;
	}

	if ((ret = ft_verify_info(fi_pep, entry.info))) {
		printf("ret: %d\n", ret);
		return ret;
	}

	return 0;
}

int ft_accept_connection(struct fid_ep *ep, struct fid_eq *eq)
{
	int ret;

	ret = fi_accept(ep, NULL, 0);
	if (ret) {
		FT_PRINTERR("fi_accept", ret);
		return ret;
	}

	ret = ft_complete_connect(ep, eq);
	if (ret)
		return ret;

	return 0;
}

int ft_server_connect(void)
{
	int ret;

	ret = ft_retrieve_conn_req(eq, &fi);
	if (ret)
		goto err;

	ret = ft_open_domain_res();
	if (ret)
		goto err;

	ret = ft_alloc_active_res(fi);
	if (ret)
		goto err;

	ret = ft_enable_ep_recv();
	if (ret)
		goto err;

	ret = ft_accept_connection(ep, eq);
	if (ret)
		goto err;

	if (ft_check_opts(FT_OPT_FORK_CHILD))
		ft_fork_child();

	return 0;

err:
	if (fi)
		fi_reject(pep, fi->handle, NULL, 0);
	return ret;
}

int ft_connect_ep(struct fid_ep *ep,
		struct fid_eq *eq, fi_addr_t *remote_addr)
{
	int ret;

	ret = fi_connect(ep, remote_addr, NULL, 0);
	if (ret) {
		FT_PRINTERR("fi_connect", ret);
		return ret;
	}

	ret = ft_complete_connect(ep, eq);
	if (ret)
		return ret;

	return 0;
}

int ft_client_connect(void)
{
	int ret;

	ret = ft_init();
	if (ret)
		return ret;

	ret = ft_init_oob();
	if (ret)
		return ret;

	ret = ft_getinfo(hints, &fi);
	if (ret)
		return ret;

	ret = ft_open_fabric_res();
	if (ret)
		return ret;

	ret = ft_alloc_active_res(fi);
	if (ret)
		return ret;

	ret = ft_enable_ep_recv();
	if (ret)
		return ret;

	ret = ft_connect_ep(ep, eq, fi->dest_addr);
	if (ret)
		return ret;

	if (ft_check_opts(FT_OPT_FORK_CHILD))
		ft_fork_child();

	return 0;
}

int ft_init_fabric(void)
{
	int ret;

	ret = ft_init();
	if (ret)
		return ret;

	ret = ft_init_oob();
	if (ret)
		return ret;

	if (oob_sock >= 0 && opts.dst_addr) {
		ret = ft_sock_sync(oob_sock, 0);
		if (ret)
			return ret;
	}

	ret = ft_getinfo(hints, &fi);
	if (ret)
		return ret;

	ret = ft_open_fabric_res();
	if (ret)
		return ret;

	ret = ft_alloc_active_res(fi);
	if (ret)
		return ret;

	ret = ft_enable_ep_recv();
	if (ret)
		return ret;

	if (oob_sock >= 0 && !opts.dst_addr) {
		ret = ft_sock_sync(oob_sock, 0);
		if (ret)
			return ret;
	}

	ret = ft_init_av();
	if (ret)
		return ret;

	if (ft_check_opts(FT_OPT_FORK_CHILD))
		ft_fork_child();

	return 0;
}

int ft_get_cq_fd(struct fid_cq *cq, int *fd)
{
	int ret = FI_SUCCESS;

	if (cq && opts.comp_method == FT_COMP_WAIT_FD) {
		ret = fi_control(&cq->fid, FI_GETWAIT, fd);
		if (ret)
			FT_PRINTERR("fi_control(FI_GETWAIT)", ret);
	}

	return ret;
}

int ft_init_alias_ep(uint64_t flags)
{
	int ret;
	ret = fi_ep_alias(ep, &alias_ep, flags);
	if (ret) {
		FT_PRINTERR("fi_ep_alias", ret);
		return ret;
	}
	return 0;
}

int ft_enable_ep(struct fid_ep *bind_ep, struct fid_eq *bind_eq, struct fid_av *bind_av,
		 struct fid_cq *bind_txcq, struct fid_cq *bind_rxcq,
		 struct fid_cntr *bind_txcntr, struct fid_cntr *bind_rxcntr,
		 struct fid_cntr *bind_rma_cntr)
{
	uint64_t flags;
	int ret;

	if ((fi->ep_attr->type == FI_EP_MSG || fi->caps & FI_MULTICAST ||
	    fi->caps & FI_COLLECTIVE) && !(opts.options & FT_OPT_DOMAIN_EQ))
		FT_EP_BIND(bind_ep, bind_eq, 0);

	FT_EP_BIND(bind_ep, bind_av, 0);
	FT_EP_BIND(bind_ep, stx, 0);
	FT_EP_BIND(bind_ep, srx, 0);

	flags = FI_TRANSMIT;
	if (!(opts.options & FT_OPT_TX_CQ))
		flags |= FI_SELECTIVE_COMPLETION;
	FT_EP_BIND(bind_ep, bind_txcq, flags);

	flags = FI_RECV;
	if (!(opts.options & FT_OPT_RX_CQ))
		flags |= FI_SELECTIVE_COMPLETION;
	FT_EP_BIND(bind_ep, bind_rxcq, flags);

	ret = ft_get_cq_fd(bind_txcq, &tx_fd);
	if (ret)
		return ret;

	ret = ft_get_cq_fd(bind_rxcq, &rx_fd);
	if (ret)
		return ret;

	/* TODO: use control structure to select counter bindings explicitly */
	if (opts.options & FT_OPT_TX_CQ)
		flags = 0;
	else
		flags = FI_SEND;

	if (hints->caps & (FI_RMA | FI_ATOMICS))
		flags |= FI_WRITE | FI_READ;
	FT_EP_BIND(bind_ep, bind_txcntr, flags);

	if (opts.options & FT_OPT_RX_CQ)
		flags = 0;
	else
		flags = FI_RECV;

	FT_EP_BIND(bind_ep, bind_rxcntr, flags);

	if (hints->caps & (FI_RMA | FI_ATOMICS) && hints->caps & FI_RMA_EVENT) {
		flags = fi->caps & (FI_REMOTE_WRITE | FI_REMOTE_READ);
		FT_EP_BIND(bind_ep, bind_rma_cntr, flags);
	}

	if (opts.max_msg_size) {
		ret = fi_setopt(&bind_ep->fid, FI_OPT_ENDPOINT, FI_OPT_MAX_MSG_SIZE, &opts.max_msg_size, sizeof opts.max_msg_size);
		if (ret && ret != -FI_EOPNOTSUPP) {
			FT_PRINTERR("fi_setopt(FI_OPT_MAX_MSG_SIZE)", ret);
			return ret;
		}
	}

	ret = fi_enable(bind_ep);
	if (ret) {
		FT_PRINTERR("fi_enable", ret);
		return ret;
	}

	return 0;
}

int ft_enable_ep_recv(void)
{
	int ret;

	ret = ft_enable_ep(ep, eq, av, txcq, rxcq, txcntr, rxcntr, rma_cntr);
	if (ret)
		return ret;

	ret = ft_alloc_msgs();
	if (ret)
		return ret;

	if (!ft_check_opts(FT_OPT_SKIP_MSG_ALLOC) &&
	    (fi->caps & (FI_MSG | FI_TAGGED))) {
		/* Initial receive will get remote address for unconnected EPs */
		ret = ft_post_rx(ep, MAX(rx_size, FT_MAX_CTRL_MSG), &rx_ctx);
		if (ret)
			return ret;
	}

	return 0;
}

int ft_join_mc(void)
{
	struct fi_eq_entry entry;
	uint32_t event;
	ssize_t rd;
	int ret;

	ret = fi_join(ep, fi->dest_addr, 0, &mc, ep->fid.context);
	if (ret) {
		FT_PRINTERR("fi_join", ret);
		return ret;
	}

	rd = fi_eq_sread(eq, &event, &entry, sizeof entry, -1, 0);
	if (rd != sizeof entry) {
		FT_PROCESS_EQ_ERR(rd, eq, "fi_eq_sread", "join");
		ret = (int) rd;
		return ret;
	}

	if (event != FI_JOIN_COMPLETE || entry.fid != &mc->fid) {
		fprintf(stderr, "Unexpected join event %d fid %p (mc %p)\n",
			event, entry.fid, ep);
		ret = -FI_EOTHER;
		return ret;
	}

	return 0;
}

int ft_av_insert(struct fid_av *av, void *addr, size_t count, fi_addr_t *fi_addr,
		uint64_t flags, void *context)
{
	int ret;

	ret = fi_av_insert(av, addr, count, fi_addr, flags, context);
	if (ret < 0) {
		FT_PRINTERR("fi_av_insert", ret);
		return ret;
	} else if (ret != count) {
		FT_ERR("fi_av_insert: number of addresses inserted = %d;"
			       " number of addresses given = %zd\n", ret, count);
		return -EXIT_FAILURE;
	}

	return 0;
}

int ft_init_av(void)
{
	return ft_init_av_dst_addr(av, ep, &remote_fi_addr);
}

int ft_exchange_addresses_oob(struct fid_av *av_ptr, struct fid_ep *ep_ptr,
		fi_addr_t *remote_addr)
{
	char buf[FT_MAX_CTRL_MSG];
	int ret;
	size_t addrlen = FT_MAX_CTRL_MSG;

	ret = fi_getname(&ep_ptr->fid, buf, &addrlen);
	if (ret) {
		FT_PRINTERR("fi_getname", ret);
		return ret;
	}

	ret = ft_sock_send(oob_sock, buf, FT_MAX_CTRL_MSG);
	if (ret)
		return ret;

	ret = ft_sock_recv(oob_sock, buf, FT_MAX_CTRL_MSG);
	if (ret)
		return ret;

	ret = ft_av_insert(av_ptr, buf, 1, remote_addr, 0, NULL);
	if (ret)
		return ret;

	return 0;
}

/* TODO: retry send for unreliable endpoints */
int ft_init_av_dst_addr(struct fid_av *av_ptr, struct fid_ep *ep_ptr,
		fi_addr_t *remote_addr)
{
	char temp[FT_MAX_CTRL_MSG];
	size_t addrlen;
	int ret;

	if (opts.options & FT_OPT_SKIP_ADDR_EXCH)
		return 0;

	if (opts.options & FT_OPT_OOB_ADDR_EXCH) {
		ret = ft_exchange_addresses_oob(av_ptr, ep_ptr, remote_addr);
		if (ret)
			return ret;
		else
			goto set_rx_seq_close;
	}

	if (opts.dst_addr) {
		ret = ft_av_insert(av_ptr, fi->dest_addr, 1, remote_addr, 0, NULL);
		if (ret)
			return ret;

		addrlen = FT_MAX_CTRL_MSG;
		ret = fi_getname(&ep_ptr->fid, temp, &addrlen);
		if (ret) {
			FT_PRINTERR("fi_getname", ret);
			return ret;
		}

		ret = ft_hmem_copy_to(opts.iface, opts.device,
				      tx_buf + ft_tx_prefix_size(), temp, addrlen);
		if (ret)
			return ret;

		ret = (int) ft_tx(ep, *remote_addr, addrlen, &tx_ctx);
		if (ret)
			return ret;

		ret = ft_rx(ep, 1);
		if (ret)
			return ret;
	} else {
		ret = ft_get_rx_comp(rx_seq);
		if (ret)
			return ret;

		ret = ft_hmem_copy_from(opts.iface, opts.device, temp,
					rx_buf + ft_rx_prefix_size(),
					FT_MAX_CTRL_MSG);
		if (ret)
			return ret;

		/* Test passing NULL fi_addr on one of the sides (server) if
		 * AV type is FI_AV_TABLE */
		ret = ft_av_insert(av_ptr, temp, 1,
				   ((fi->domain_attr->av_type == FI_AV_TABLE) ?
				       NULL : remote_addr), 0, NULL);
		if (ret)
			return ret;

		ret = ft_post_rx(ep, rx_size, &rx_ctx);
		if (ret)
			return ret;

		if (fi->domain_attr->av_type == FI_AV_TABLE)
			*remote_addr = 0;

		ret = (int) ft_tx(ep, *remote_addr, 1, &tx_ctx);
		if (ret)
			return ret;
	}

set_rx_seq_close:
	/*
	* For a test which does not have MSG or TAGGED
	* capabilities, but has RMA/Atomics and uses the OOB sync.
	* If no recv is going to be posted,
	* then the rx_seq needs to be incremented to wait on the first RMA/Atomic
	* completion.
	*/
	if (!(fi->caps & FI_MSG) && !(fi->caps & FI_TAGGED) && opts.oob_port)
		rx_seq++;

	return 0;
}

/* TODO: retry send for unreliable endpoints */
int ft_init_av_addr(struct fid_av *av_ptr, struct fid_ep *ep_ptr,
		fi_addr_t *remote_addr)
{
	size_t addrlen;
	int ret;

	if (opts.options & FT_OPT_SKIP_ADDR_EXCH)
		return 0;

	if (opts.options & FT_OPT_OOB_ADDR_EXCH)
		return ft_exchange_addresses_oob(av_ptr, ep_ptr, remote_addr);

	if (opts.dst_addr) {
		addrlen = FT_MAX_CTRL_MSG;
		ret = fi_getname(&ep_ptr->fid, (char *) tx_buf + ft_tx_prefix_size(),
				 &addrlen);
		if (ret) {
			FT_PRINTERR("fi_getname", ret);
			return ret;
		}

		ret = (int) ft_tx(ep, remote_fi_addr, addrlen, &tx_ctx);
		if (ret)
			return ret;

		ret = (int) ft_rx(ep, FT_MAX_CTRL_MSG);
		if (ret)
			return ret;

		ret = ft_av_insert(av_ptr, (char *) rx_buf + ft_rx_prefix_size(),
				1, remote_addr, 0, NULL);
		if (ret)
			return ret;
	} else {
		ret = (int) ft_rx(ep, FT_MAX_CTRL_MSG);
		if (ret)
			return ret;

		ret = ft_av_insert(av_ptr, (char *) rx_buf + ft_rx_prefix_size(),
				   1, remote_addr, 0, NULL);
		if (ret)
			return ret;

		addrlen = FT_MAX_CTRL_MSG;
		ret = fi_getname(&ep_ptr->fid,
				(char *) tx_buf + ft_tx_prefix_size(),
				&addrlen);
		if (ret) {
			FT_PRINTERR("fi_getname", ret);
			return ret;
		}

		ret = (int) ft_tx(ep, remote_fi_addr, addrlen, &tx_ctx);
		if (ret)
			return ret;
	}

	return 0;
}

int ft_exchange_keys(struct fi_rma_iov *peer_iov)
{
	char temp[FT_MAX_CTRL_MSG];
	struct fi_rma_iov *rma_iov = (struct fi_rma_iov *) temp;
	size_t key_size = 0, len;
	uint64_t addr;
	int ret;

	if (fi->domain_attr->mr_mode & FI_MR_RAW) {
		ret = fi_mr_raw_attr(mr, &addr, NULL, &key_size, 0);
		if (ret != -FI_ETOOSMALL)
			return ret;
		len = sizeof(*rma_iov) + key_size - sizeof(rma_iov->key);
		if (len > FT_MAX_CTRL_MSG) {
			FT_PRINTERR("Raw key too large for ctrl message",
				    -FI_ETOOSMALL);
			return -FI_ETOOSMALL;
		}
	} else {
		len = sizeof(*rma_iov);
	}

	if ((fi->domain_attr->mr_mode == FI_MR_BASIC) ||
	    (fi->domain_attr->mr_mode & FI_MR_VIRT_ADDR)) {
		rma_iov->addr = (uintptr_t) rx_buf + ft_rx_prefix_size();
	} else {
		rma_iov->addr = 0;
	}

	if (fi->domain_attr->mr_mode & FI_MR_RAW) {
		ret = fi_mr_raw_attr(mr, &addr, (uint8_t *) &rma_iov->key,
				     &key_size, 0);
		if (ret)
			return ret;
	} else {
		rma_iov->key = fi_mr_key(mr);
	}

	ret = ft_hmem_copy_to(opts.iface, opts.device,
			      tx_buf + ft_tx_prefix_size(), temp, len);
	if (ret)
		return ret;

	ret = ft_tx(ep, remote_fi_addr, len + ft_tx_prefix_size(), &tx_ctx);
	if (ret)
		return ret;

	ret = ft_get_rx_comp(rx_seq);
	if (ret)
		return ret;

	ret = ft_hmem_copy_from(opts.iface, opts.device, temp,
				rx_buf + ft_rx_prefix_size(), FT_MAX_CTRL_MSG);
	if (ret)
		return ret;

	if (fi->domain_attr->mr_mode & FI_MR_RAW) {
		peer_iov->addr = rma_iov->addr;
		peer_iov->len = rma_iov->len;
		ret = fi_mr_map_raw(domain, rma_iov->addr,
				    (uint8_t *) &rma_iov->key, key_size,
				    &peer_iov->key, 0);
		if (ret)
			return ret;
	} else {
		*peer_iov = *rma_iov;
	}

	ret = ft_post_rx(ep, rx_size, &rx_ctx);
	if (ret)
		return ret;

	return ft_sync();
}

static void ft_cleanup_mr_array(struct ft_context *ctx_arr, char **mr_bufs)
{
	int i, ret;

	if (!mr_bufs)
		return;

	for (i = 0; i < opts.window_size; i++) {
		FT_CLOSE_FID(ctx_arr[i].mr);
		ret = ft_hmem_free(opts.iface, mr_bufs[i]);
		if (ret)
			FT_PRINTERR("ft_hmem_free", ret);
	}
}

void ft_close_fids(void)
{
	FT_CLOSE_FID(mc);
	FT_CLOSE_FID(alias_ep);
	if (fi && fi->domain_attr->mr_mode & FI_MR_ENDPOINT) {
		if (mr != &no_mr) {
			FT_CLOSE_FID(mr);
			mr = &no_mr;
		}
	}
	FT_CLOSE_FID(ep);
	FT_CLOSE_FID(pep);
	if (opts.options & FT_OPT_CQ_SHARED) {
		FT_CLOSE_FID(txcq);
	} else {
		FT_CLOSE_FID(rxcq);
		FT_CLOSE_FID(txcq);
	}
	FT_CLOSE_FID(rxcntr);
	FT_CLOSE_FID(txcntr);
	FT_CLOSE_FID(rma_cntr);
	FT_CLOSE_FID(pollset);
	if (mr != &no_mr)
		FT_CLOSE_FID(mr);
	FT_CLOSE_FID(av);
	FT_CLOSE_FID(srx);
	FT_CLOSE_FID(stx);
	FT_CLOSE_FID(domain);
	FT_CLOSE_FID(eq);
	FT_CLOSE_FID(waitset);
	FT_CLOSE_FID(fabric);
}

/* We need to free any data that we allocated before freeing the
 * hints.  Windows doesn't like it when a library frees memory that
 * was allocated by the application.
 */
void ft_freehints(struct fi_info *hints)
{
	if (!hints)
		return;

	if (hints->domain_attr->name) {
		free(hints->domain_attr->name);
		hints->domain_attr->name = NULL;
	}
	if (hints->fabric_attr->name) {
		free(hints->fabric_attr->name);
		hints->fabric_attr->name = NULL;
	}
	if (hints->fabric_attr->prov_name) {
		free(hints->fabric_attr->prov_name);
		hints->fabric_attr->prov_name = NULL;
	}
	if (hints->src_addr) {
		free(hints->src_addr);
		hints->src_addr = NULL;
		hints->src_addrlen = 0;
	}
	if (hints->dest_addr) {
		free(hints->dest_addr);
		hints->dest_addr = NULL;
		hints->dest_addrlen = 0;
	}

	fi_freeinfo(hints);
}

void ft_free_res(void)
{
	int ret;

	ft_cleanup_mr_array(tx_ctx_arr, tx_mr_bufs);
	ft_cleanup_mr_array(rx_ctx_arr, rx_mr_bufs);

	free(tx_ctx_arr);
	free(rx_ctx_arr);
	tx_ctx_arr = NULL;
	rx_ctx_arr = NULL;

	ft_close_fids();
	free(user_test_sizes);
	if (buf) {
		ret = ft_hmem_free(opts.iface, buf);
		if (ret)
			FT_PRINTERR("ft_hmem_free", ret);
		buf = rx_buf = tx_buf = NULL;
		buf_size = rx_size = tx_size = tx_mr_size = rx_mr_size = 0;
	}
	if (dev_host_buf)
		ft_free_host_tx_buf();

	if (fi_pep) {
		fi_freeinfo(fi_pep);
		fi_pep = NULL;
	}
	if (fi) {
		fi_freeinfo(fi);
		fi = NULL;
	}
	if (hints) {
		ft_freehints(hints);
		hints = NULL;
	}

	ret = ft_hmem_cleanup(opts.iface);
	if (ret)
		FT_PRINTERR("ft_hmem_cleanup", ret);
}

static int dupaddr(void **dst_addr, size_t *dst_addrlen,
		void *src_addr, size_t src_addrlen)
{
	*dst_addr = malloc(src_addrlen);
	if (!*dst_addr) {
		FT_ERR("address allocation failed");
		return EAI_MEMORY;
	}
	*dst_addrlen = src_addrlen;
	memcpy(*dst_addr, src_addr, src_addrlen);
	return 0;
}

static int getaddr(char *node, char *service,
		   struct fi_info *hints, uint64_t flags)
{
	int ret;
	struct fi_info *fi;

	if (!node && !service) {
		if (flags & FI_SOURCE) {
			hints->src_addr = NULL;
			hints->src_addrlen = 0;
		} else {
			hints->dest_addr = NULL;
			hints->dest_addrlen = 0;
		}
		return 0;
	}

	ret = fi_getinfo(FT_FIVERSION, node, service, flags, hints, &fi);
	if (ret) {
		FT_PRINTERR("fi_getinfo", ret);
		return ret;
	}
	hints->addr_format = fi->addr_format;

	if (flags & FI_SOURCE) {
		ret = dupaddr(&hints->src_addr, &hints->src_addrlen,
				fi->src_addr, fi->src_addrlen);
	} else {
		ret = dupaddr(&hints->dest_addr, &hints->dest_addrlen,
				fi->dest_addr, fi->dest_addrlen);
	}

	fi_freeinfo(fi);
	return ret;
}

int ft_getsrcaddr(char *node, char *service, struct fi_info *hints)
{
	return getaddr(node, service, hints, FI_SOURCE);
}

int ft_read_addr_opts(char **node, char **service, struct fi_info *hints,
		uint64_t *flags, struct ft_opts *opts)
{
	int ret;

	if (opts->options & FT_OPT_ADDR_IS_OOB) {
		*service = NULL;
		*node = NULL;
	} else if (opts->address_format == FI_ADDR_STR) {
		/* We are likely dealing with a provider specific address format.
		 * I.e. NOT an IP address or host name
		 */
		*service = NULL;
		if (opts->dst_addr) {
			*node = opts->dst_addr;
		} else {
			*node = opts->src_addr;
			*flags = FI_SOURCE;
		}
	} else if (opts->dst_addr) {
		if (!opts->dst_port)
			opts->dst_port = default_port;

		ret = ft_getsrcaddr(opts->src_addr, opts->src_port, hints);
		if (ret)
			return ret;
		*node = opts->dst_addr;
		*service = opts->dst_port;
	} else {
		if (!opts->src_port)
			opts->src_port = default_port;

		*node = opts->src_addr;
		*service = opts->src_port;
		*flags = FI_SOURCE;
	}

	return 0;
}

char *size_str(char str[FT_STR_LEN], long long size)
{
	long long base, fraction = 0;
	char mag;

	memset(str, '\0', FT_STR_LEN);

	if (size >= (1 << 30)) {
		base = 1 << 30;
		mag = 'g';
	} else if (size >= (1 << 20)) {
		base = 1 << 20;
		mag = 'm';
	} else if (size >= (1 << 10)) {
		base = 1 << 10;
		mag = 'k';
	} else {
		base = 1;
		mag = '\0';
	}

	if (size / base < 10)
		fraction = (size % base) * 10 / base;

	if (fraction)
		snprintf(str, FT_STR_LEN, "%lld.%lld%c", size / base, fraction, mag);
	else
		snprintf(str, FT_STR_LEN, "%lld%c", size / base, mag);

	return str;
}

char *cnt_str(char str[FT_STR_LEN], long long cnt)
{
	if (cnt >= 1000000000)
		snprintf(str, FT_STR_LEN, "%lldb", cnt / 1000000000);
	else if (cnt >= 1000000)
		snprintf(str, FT_STR_LEN, "%lldm", cnt / 1000000);
	else if (cnt >= 1000)
		snprintf(str, FT_STR_LEN, "%lldk", cnt / 1000);
	else
		snprintf(str, FT_STR_LEN, "%lld", cnt);

	return str;
}

int size_to_count(int size)
{
	if (size >= (1 << 20))
		return (opts.options & FT_OPT_BW) ? 200 : 100;
	else if (size >= (1 << 16))
		return (opts.options & FT_OPT_BW) ? 2000 : 1000;
	else
		return (opts.options & FT_OPT_BW) ? 20000: 10000;
}

static const size_t datatype_size_table[] = {
	[FI_INT8]   = sizeof(int8_t),
	[FI_UINT8]  = sizeof(uint8_t),
	[FI_INT16]  = sizeof(int16_t),
	[FI_UINT16] = sizeof(uint16_t),
	[FI_INT32]  = sizeof(int32_t),
	[FI_UINT32] = sizeof(uint32_t),
	[FI_INT64]  = sizeof(int64_t),
	[FI_UINT64] = sizeof(uint64_t),
	[FI_FLOAT]  = sizeof(float),
	[FI_DOUBLE] = sizeof(double),
	[FI_FLOAT_COMPLEX]  = sizeof(OFI_COMPLEX(float)),
	[FI_DOUBLE_COMPLEX] = sizeof(OFI_COMPLEX(double)),
	[FI_LONG_DOUBLE]    = sizeof(long double),
	[FI_LONG_DOUBLE_COMPLEX] = sizeof(OFI_COMPLEX(long_double)),
	/* Compute 128-bit integer size, since compiler may not support type. */
	[FI_INT128]  = sizeof(int64_t) * 2,
	[FI_UINT128] = sizeof(uint64_t) * 2,
};

size_t datatype_to_size(enum fi_datatype datatype)
{
	if (datatype >= ARRAY_SIZE(datatype_size_table))
		return 0;

	return datatype_size_table[datatype];
}

void init_test(struct ft_opts *opts, char *test_name, size_t test_name_len)
{
	char sstr[FT_STR_LEN];

	size_str(sstr, opts->transfer_size);
	if (!strcmp(test_name, "custom"))
		snprintf(test_name, test_name_len, "%s_lat", sstr);
	if (!(opts->options & FT_OPT_ITER))
		opts->iterations = size_to_count(opts->transfer_size);
}

void ft_force_progress(void)
{
	if (txcq)
		(void) fi_cq_read(txcq, NULL, 0);
	if (rxcq)
		(void) fi_cq_read(rxcq, NULL, 0);
}

int ft_progress(struct fid_cq *cq, uint64_t total, uint64_t *cq_cntr)
{
	struct fi_cq_err_entry comp;
	int ret;

	ret = fi_cq_read(cq, &comp, 1);
	if (ret > 0)
		(*cq_cntr)++;

	if (ret >= 0 || ret == -FI_EAGAIN)
		return 0;

	if (ret == -FI_EAVAIL) {
		ret = ft_cq_readerr(cq);
		(*cq_cntr)++;
	} else {
		FT_PRINTERR("fi_cq_read/sread", ret);
	}
	return ret;
}

#define FT_POST(post_fn, progress_fn, cq, seq, cq_cntr, op_str, ...)		\
	do {									\
		int timeout_save;						\
		int ret, rc;							\
										\
		while (1) {							\
			ret = post_fn(__VA_ARGS__);				\
			if (!ret)						\
				break;						\
										\
			if (ret != -FI_EAGAIN) {				\
				FT_PRINTERR(op_str, ret);			\
				return ret;					\
			}							\
										\
			timeout_save = timeout;					\
			timeout = 0;						\
			rc = progress_fn(cq, seq, cq_cntr);			\
			if (rc && rc != -FI_EAGAIN) {				\
				FT_ERR("Failed to get " op_str " completion");	\
				return rc;					\
			}							\
			timeout = timeout_save;					\
		}								\
		seq++;								\
	} while (0)

ssize_t ft_post_tx_buf(struct fid_ep *ep, fi_addr_t fi_addr, size_t size,
		       uint64_t data, void *ctx,
		       void *op_buf, void *op_mr_desc, uint64_t op_tag)
{
	size += ft_tx_prefix_size();
	if (hints->caps & FI_TAGGED) {
		op_tag = op_tag ? op_tag : tx_seq;
		if (data != NO_CQ_DATA) {
			FT_POST(fi_tsenddata, ft_progress, txcq, tx_seq,
				&tx_cq_cntr, "transmit", ep, op_buf, size,
				op_mr_desc, data, fi_addr, op_tag, ctx);
		} else {
			FT_POST(fi_tsend, ft_progress, txcq, tx_seq,
				&tx_cq_cntr, "transmit", ep, op_buf, size,
				op_mr_desc, fi_addr, op_tag, ctx);
		}
	} else {
		if (data != NO_CQ_DATA) {
			FT_POST(fi_senddata, ft_progress, txcq, tx_seq,
				&tx_cq_cntr, "transmit", ep, op_buf, size,
				op_mr_desc, data, fi_addr, ctx);

		} else {
			FT_POST(fi_send, ft_progress, txcq, tx_seq,
				&tx_cq_cntr, "transmit", ep, op_buf, size,
				op_mr_desc, fi_addr, ctx);
		}
	}
	return 0;
}

ssize_t ft_post_tx(struct fid_ep *ep, fi_addr_t fi_addr, size_t size,
		   uint64_t data, void *ctx)
{
	return ft_post_tx_buf(ep, fi_addr, size, data,
			      ctx, tx_buf, mr_desc, ft_tag);
}

ssize_t ft_tx(struct fid_ep *ep, fi_addr_t fi_addr, size_t size, void *ctx)
{
	ssize_t ret;

	if (ft_check_opts(FT_OPT_VERIFY_DATA | FT_OPT_ACTIVE)) {
		ret = ft_fill_buf((char *) tx_buf + ft_tx_prefix_size(), size);
		if (ret)
			return ret;
	}

	ret = ft_post_tx(ep, fi_addr, size, NO_CQ_DATA, ctx);
	if (ret)
		return ret;

	ret = ft_get_tx_comp(tx_seq);
	return ret;
}

ssize_t ft_tx_rma(enum ft_rma_opcodes rma_op, struct fi_rma_iov *remote,
		  struct fid_ep *ep, fi_addr_t fi_addr, size_t size, void *ctx)
{
	ssize_t ret;

	if (ft_check_opts(FT_OPT_VERIFY_DATA | FT_OPT_ACTIVE)) {
		/* Fill data. Last byte reserved for iteration number */
		ret = ft_fill_buf((char *) tx_buf, size - 1);
		if (ret)
			return ret;
	}

	ret = ft_post_rma(rma_op, tx_buf, size, remote, ctx);
	if (ret)
		return ret;

	ret = ft_get_tx_comp(tx_seq);
	return ret;
}

ssize_t ft_post_inject_buf(struct fid_ep *ep, fi_addr_t fi_addr, size_t size,
			   uint64_t data, void *op_buf, uint64_t op_tag)
{
	if (hints->caps & FI_TAGGED) {
		if (data != NO_CQ_DATA) {
			FT_POST(fi_tinjectdata, ft_progress, txcq, tx_seq, &tx_cq_cntr,
				"inject", ep, op_buf, size + ft_tx_prefix_size(),
				data, fi_addr, op_tag);
		} else {
			FT_POST(fi_tinject, ft_progress, txcq, tx_seq, &tx_cq_cntr,
				"inject", ep, op_buf, size + ft_tx_prefix_size(),
				fi_addr, op_tag);
		}
	} else {
		if (data != NO_CQ_DATA) {
			FT_POST(fi_injectdata, ft_progress, txcq, tx_seq, &tx_cq_cntr,
				"inject", ep, op_buf, size + ft_tx_prefix_size(),
				data, fi_addr);
		} else {
			FT_POST(fi_inject, ft_progress, txcq, tx_seq, &tx_cq_cntr,
				"inject", ep, op_buf, size + ft_tx_prefix_size(),
				fi_addr);
		}
	}

	tx_cq_cntr++;
	return 0;
}

ssize_t ft_post_inject(struct fid_ep *ep, fi_addr_t fi_addr, size_t size)
{
	return ft_post_inject_buf(ep, fi_addr, size, NO_CQ_DATA, tx_buf, tx_seq);
}

ssize_t ft_inject(struct fid_ep *ep, fi_addr_t fi_addr, size_t size)
{
	ssize_t ret;

	if (ft_check_opts(FT_OPT_VERIFY_DATA | FT_OPT_ACTIVE)) {
		ret = ft_fill_buf((char *) tx_buf + ft_tx_prefix_size(), size);
		if (ret)
			return ret;
	}

	ret = ft_post_inject(ep, fi_addr, size);
	if (ret)
		return ret;

	return ret;
}

ssize_t ft_inject_rma(enum ft_rma_opcodes rma_op, struct fi_rma_iov *remote,
		      struct fid_ep *ep, fi_addr_t fi_addr, size_t size)
{
	ssize_t ret;

	if (ft_check_opts(FT_OPT_VERIFY_DATA | FT_OPT_ACTIVE)) {
		/* Fill data. Last byte reserved for iteration number */
		ret = ft_fill_buf((char *) tx_buf, size - 1);
		if (ret)
			return ret;
	}

	ret = ft_post_rma_inject(rma_op, tx_buf, size, remote);
	if (ret)
		return ret;

	return ret;
}

static size_t ft_remote_write_offset(const char *buf)
{
	assert(buf >= tx_buf && buf < (tx_buf + tx_buf_size));
	/* rx_buf area is at the beginning of the remote region */
	return buf - tx_buf;
}

static size_t ft_remote_read_offset(const char *buf)
{
	assert(buf >= rx_buf && buf < (rx_buf + rx_buf_size));
	/* We want to read from remote peer's tx_buf area,
	 * which immediately follow the rx_buf, hence add rx_buf_size
	 */
	return buf - rx_buf + rx_buf_size;
}

ssize_t ft_post_rma(enum ft_rma_opcodes op, char *buf, size_t size,
		struct fi_rma_iov *remote, void *context)
{
	switch (op) {
	case FT_RMA_WRITE:
		FT_POST(fi_write, ft_progress, txcq, tx_seq, &tx_cq_cntr,
			"fi_write", ep, buf, size, mr_desc,
			remote_fi_addr, remote->addr + ft_remote_write_offset(buf),
			remote->key, context);
		break;
	case FT_RMA_WRITEDATA:
		FT_POST(fi_writedata, ft_progress, txcq, tx_seq, &tx_cq_cntr,
			"fi_writedata", ep, buf, size, mr_desc,
			remote_cq_data, remote_fi_addr,
			remote->addr + ft_remote_write_offset(buf),
			remote->key, context);
		break;
	case FT_RMA_READ:
		FT_POST(fi_read, ft_progress, txcq, tx_seq, &tx_cq_cntr,
			"fi_read", ep, buf, size, mr_desc,
			remote_fi_addr, remote->addr + ft_remote_read_offset(buf),
			remote->key, context);
		break;
	default:
		FT_ERR("Unknown RMA op type\n");
		return EXIT_FAILURE;
	}

	return 0;
}

ssize_t ft_post_rma_inject(enum ft_rma_opcodes op, char *buf, size_t size,
		struct fi_rma_iov *remote)
{
	switch (op) {
	case FT_RMA_WRITE:
		FT_POST(fi_inject_write, ft_progress, txcq, tx_seq, &tx_cq_cntr,
			"fi_inject_write", ep, buf, opts.transfer_size,
			remote_fi_addr, remote->addr + ft_remote_write_offset(buf),
			remote->key);
		break;
	case FT_RMA_WRITEDATA:
		FT_POST(fi_inject_writedata, ft_progress, txcq, tx_seq,
			&tx_cq_cntr, "fi_inject_writedata", ep, buf,
			opts.transfer_size, remote_cq_data, remote_fi_addr,
			remote->addr + ft_remote_write_offset(buf), remote->key);
		break;
	default:
		FT_ERR("Unknown RMA inject op type\n");
		return EXIT_FAILURE;
	}

	tx_cq_cntr++;
	return 0;
}

ssize_t ft_post_atomic(enum ft_atomic_opcodes opcode, struct fid_ep *ep,
		       void *compare, void *compare_desc, void *result,
		       void *result_desc, struct fi_rma_iov *remote,
		       enum fi_datatype datatype, enum fi_op atomic_op,
		       void *context)
{
	size_t size, count;

	size = datatype_to_size(datatype);
	if (!size) {
		FT_ERR("Unknown datatype\n");
		return EXIT_FAILURE;
	}
	count = opts.transfer_size / size;

	switch (opcode) {
	case FT_ATOMIC_BASE:
		FT_POST(fi_atomic, ft_progress, txcq, tx_seq, &tx_cq_cntr,
			"fi_atomic", ep, buf, count, mr_desc, remote_fi_addr,
			remote->addr, remote->key, datatype, atomic_op, context);
		break;
	case FT_ATOMIC_FETCH:
		FT_POST(fi_fetch_atomic, ft_progress, txcq, tx_seq, &tx_cq_cntr,
			"fi_fetch_atomic", ep, buf, count, mr_desc, result,
			result_desc, remote_fi_addr, remote->addr, remote->key,
			datatype, atomic_op, context);
		break;
	case FT_ATOMIC_COMPARE:
		FT_POST(fi_compare_atomic, ft_progress, txcq, tx_seq,
			&tx_cq_cntr, "fi_compare_atomic", ep, buf, count,
			mr_desc, compare, compare_desc, result, result_desc,
			remote_fi_addr, remote->addr, remote->key, datatype,
			atomic_op, context);
		break;
	default:
		FT_ERR("Unknown atomic opcode\n");
		return EXIT_FAILURE;
	}

	return 0;
}

static int check_atomic_attr(enum fi_op op, enum fi_datatype datatype,
			     uint64_t flags)
{
	struct fi_atomic_attr attr;
	int ret;

	ret = fi_query_atomic(domain, datatype, op, &attr, flags);
	if (ret) {
		FT_PRINTERR("fi_query_atomic", ret);
		return ret;
	}

	if (attr.size != datatype_to_size(datatype)) {
		fprintf(stderr, "Provider atomic size mismatch\n");
		return -FI_ENOSYS;
	}

	return 0;
}

int check_base_atomic_op(struct fid_ep *endpoint, enum fi_op op,
			 enum fi_datatype datatype, size_t *count)
{
	int ret;

	ret = fi_atomicvalid(endpoint, datatype, op, count);
	if (ret)
		return ret;

	return check_atomic_attr(op, datatype, 0);
}

int check_fetch_atomic_op(struct fid_ep *endpoint, enum fi_op op,
			  enum fi_datatype datatype, size_t *count)
{
	int ret;

	ret = fi_fetch_atomicvalid(endpoint, datatype, op, count);
	if (ret)
		return ret;

	return check_atomic_attr(op, datatype, FI_FETCH_ATOMIC);
}

int check_compare_atomic_op(struct fid_ep *endpoint, enum fi_op op,
			    enum fi_datatype datatype, size_t *count)
{
	int ret;

	ret = fi_compare_atomicvalid(endpoint, datatype, op, count);
	if (ret)
		return ret;

	return check_atomic_attr(op, datatype, FI_COMPARE_ATOMIC);
}

ssize_t ft_post_rx_buf(struct fid_ep *ep, size_t size, void *ctx,
		       void *op_buf, void *op_mr_desc, uint64_t op_tag)
{
	size = MAX(size, FT_MAX_CTRL_MSG) + ft_rx_prefix_size();
	if (hints->caps & FI_TAGGED) {
		op_tag = op_tag ? op_tag : rx_seq;
		FT_POST(fi_trecv, ft_progress, rxcq, rx_seq, &rx_cq_cntr,
			"receive", ep, op_buf, size, op_mr_desc,
			remote_fi_addr, op_tag, 0, ctx);
	} else {
		FT_POST(fi_recv, ft_progress, rxcq, rx_seq, &rx_cq_cntr,
			"receive", ep, op_buf, size, op_mr_desc, remote_fi_addr, ctx);
	}
	return 0;
}

ssize_t ft_post_rx(struct fid_ep *ep, size_t size, void *ctx)
{
	return ft_post_rx_buf(ep, size, ctx, rx_buf, mr_desc, ft_tag);
}

ssize_t ft_rx(struct fid_ep *ep, size_t size)
{
	ssize_t ret;

	ret = ft_get_rx_comp(rx_seq);
	if (ret)
		return ret;

	if (ft_check_opts(FT_OPT_VERIFY_DATA | FT_OPT_ACTIVE)) {
		ret = ft_check_buf((char *) rx_buf + ft_rx_prefix_size(), size);
		if (ret)
			return ret;
	}
	/* TODO: verify CQ data, if available */

	/* Ignore the size arg. Post a buffer large enough to handle all message
	 * sizes. ft_sync() makes use of ft_rx() and gets called in tests just before
	 * message size is updated. The recvs posted are always for the next incoming
	 * message */
	ret = ft_post_rx(ep, rx_size, &rx_ctx);
	return ret;
}

ssize_t ft_rx_rma(int iter, enum ft_rma_opcodes rma_op, struct fid_ep *ep,
		  size_t size)
{
	ssize_t ret;

	switch (rma_op) {
	case FT_RMA_WRITE:
		/* No completion at target. Poll the recv buf instead. */
		ret = ft_rma_poll_buf(rx_buf, iter, size);
		if (ret)
			return ret;
		break;
	case FT_RMA_WRITEDATA:
		/* Get recv-side write-imm completion */
		ret = ft_get_rx_comp(rx_seq);
		if (ret)
			return ret;
		break;
	default:
		FT_ERR("Unsupported RMA op type");
		return EXIT_FAILURE;
	}

	if (ft_check_opts(FT_OPT_VERIFY_DATA | FT_OPT_ACTIVE)) {
		ret = ft_check_buf((char *) rx_buf, size - 1);
		if (ret)
			return ret;
	}

	/* TODO: verify CQ data, if available */

	if (rma_op == FT_RMA_WRITEDATA) {
		if (fi->rx_attr->mode & FI_RX_CQ_DATA) {
			ret = ft_post_rx(ep, 0, &rx_ctx);
		} else {
			/* Just increment the seq # instead of
			 * posting recv so that we wait for
			 * remote write completion on the next
			 * iteration */
			rx_seq++;
		}
	}

	return ret;
}

/*
 * Received messages match tagged buffers in order, but the completions can be
 * reported out of order.  A tag is valid if it's within the current window.
 */
static inline int
ft_tag_is_valid(struct fid_cq * cq, struct fi_cq_err_entry *comp, uint64_t tag)
{
	int valid = 1;

	if (opts.options & FT_OPT_DISABLE_TAG_VALIDATION)
		return valid;

	if ((hints->caps & FI_TAGGED) && (cq == rxcq)) {
		if (opts.options & FT_OPT_BW) {
			/* valid: (tag - window) < comp->tag < (tag + window) */
			valid = (tag < comp->tag + opts.window_size) &&
				(comp->tag < tag + opts.window_size);
		} else {
			valid = (comp->tag == tag);
		}

		if (!valid) {
			FT_ERR("Tag mismatch!. Expected: %"PRIu64", actual: %"
				PRIu64, tag, comp->tag);
		}
	}

	return valid;
}
/*
 * fi_cq_err_entry can be cast to any CQ entry format.
 */
static int ft_spin_for_comp(struct fid_cq *cq, uint64_t *cur,
			    uint64_t total, int timeout,
			    uint64_t tag)
{
	struct fi_cq_err_entry comp;
	struct timespec a, b;
	int ret;

	if (timeout >= 0)
		clock_gettime(CLOCK_MONOTONIC, &a);

	do {
		ret = fi_cq_read(cq, &comp, 1);
		if (ret > 0) {
			if (timeout >= 0)
				clock_gettime(CLOCK_MONOTONIC, &a);
			if (!ft_tag_is_valid(cq, &comp, tag ? tag : rx_cq_cntr))
				return -FI_EOTHER;
			(*cur)++;
		} else if (ret < 0 && ret != -FI_EAGAIN) {
			return ret;
		} else if (timeout >= 0) {
			clock_gettime(CLOCK_MONOTONIC, &b);
			if ((b.tv_sec - a.tv_sec) > timeout) {
				fprintf(stderr, "%ds timeout expired\n", timeout);
				return -FI_ENODATA;
			}
		}
	} while (total - *cur > 0);

	return 0;
}

/*
 * fi_cq_err_entry can be cast to any CQ entry format.
 */
static int ft_wait_for_comp(struct fid_cq *cq, uint64_t *cur,
			    uint64_t total, int timeout,
			    uint64_t tag)
{
	struct fi_cq_err_entry comp;
	int ret;

	while (total - *cur > 0) {
		ret = fi_cq_sread(cq, &comp, 1, NULL, timeout);
		if (ret > 0) {
			if (!ft_tag_is_valid(cq, &comp, tag ? tag : rx_cq_cntr))
				return -FI_EOTHER;
			(*cur)++;
		} else if (ret < 0 && ret != -FI_EAGAIN) {
			return ret;
		}
	}

	return 0;
}

/*
 * fi_cq_err_entry can be cast to any CQ entry format.
 */
static int ft_fdwait_for_comp(struct fid_cq *cq, uint64_t *cur,
			    uint64_t total, int timeout,
			    uint64_t tag)
{
	struct fi_cq_err_entry comp;
	struct fid *fids[1];
	int fd, ret;

	fd = cq == txcq ? tx_fd : rx_fd;
	fids[0] = &cq->fid;

	while (total - *cur > 0) {
		ret = fi_trywait(fabric, fids, 1);
		if (ret == FI_SUCCESS) {
			ret = ft_poll_fd(fd, timeout);
			if (ret && ret != -FI_EAGAIN)
				return ret;
		}

		ret = fi_cq_read(cq, &comp, 1);
		if (ret > 0) {
			if (!ft_tag_is_valid(cq, &comp, tag ? tag : rx_cq_cntr))
				return -FI_EOTHER;
			(*cur)++;
		} else if (ret < 0 && ret != -FI_EAGAIN) {
			return ret;
		}
	}

	return 0;
}

int ft_read_cq(struct fid_cq *cq, uint64_t *cur,
		uint64_t total, int timeout,
		uint64_t tag)
{
	int ret;

	switch (opts.comp_method) {
	case FT_COMP_SREAD:
	case FT_COMP_YIELD:
		ret = ft_wait_for_comp(cq, cur, total, timeout, tag);
		break;
	case FT_COMP_WAIT_FD:
		ret = ft_fdwait_for_comp(cq, cur, total, timeout, tag);
		break;
	default:
		ret = ft_spin_for_comp(cq, cur, total, timeout, tag);
		break;
	}
	return ret;
}

int ft_get_cq_comp(struct fid_cq *cq, uint64_t *cur,
		    uint64_t total, int timeout)
{
	int ret;

	ret = ft_read_cq(cq, cur, total, timeout, ft_tag);

	if (ret) {
		if (ret == -FI_EAVAIL) {
			ret = ft_cq_readerr(cq);
			(*cur)++;
		} else {
			FT_PRINTERR("ft_get_cq_comp", ret);
		}
	}
	return ret;
}

static int ft_spin_for_cntr(struct fid_cntr *cntr, uint64_t total, int timeout)
{
	struct timespec a, b;
	uint64_t cur;

	if (timeout >= 0)
		clock_gettime(CLOCK_MONOTONIC, &a);

	for (;;) {
		cur = fi_cntr_read(cntr);
		if (cur >= total)
			return 0;

		if (timeout >= 0) {
			clock_gettime(CLOCK_MONOTONIC, &b);
			if ((b.tv_sec - a.tv_sec) > timeout)
				break;
		}
	}

	fprintf(stderr, "%ds timeout expired\n", timeout);
	return -FI_ENODATA;
}

static int ft_wait_for_cntr(struct fid_cntr *cntr, uint64_t total, int timeout)
{
	int ret;

	while (fi_cntr_read(cntr) < total) {
		ret = fi_cntr_wait(cntr, total, timeout);
		if (ret)
			FT_PRINTERR("fi_cntr_wait", ret);
		else
			break;
	}
	return 0;
}

int ft_get_cntr_comp(struct fid_cntr *cntr, uint64_t total, int timeout)
{
	int ret = 0;

	switch (opts.comp_method) {
	case FT_COMP_SREAD:
	case FT_COMP_WAITSET:
	case FT_COMP_WAIT_FD:
	case FT_COMP_YIELD:
		ret = ft_wait_for_cntr(cntr, total, timeout);
		break;
	default:
		ret = ft_spin_for_cntr(cntr, total, timeout);
		break;
	}

	if (ret)
		FT_PRINTERR("fs_get_cntr_comp", ret);

	return ret;
}

int ft_get_rx_comp(uint64_t total)
{
	int ret = FI_SUCCESS;

	if (opts.options & FT_OPT_RX_CQ) {
		ret = ft_get_cq_comp(rxcq, &rx_cq_cntr, total, timeout);
	} else if (rxcntr) {
		ret = ft_get_cntr_comp(rxcntr, total, timeout);
	} else {
		FT_ERR("Trying to get a RX completion when no RX CQ or counter were opened");
		ret = -FI_EOTHER;
	}
	return ret;
}

int ft_get_tx_comp(uint64_t total)
{
	int ret;

	if (opts.options & FT_OPT_TX_CQ) {
		ret = ft_get_cq_comp(txcq, &tx_cq_cntr, total, -1);
	} else if (txcntr) {
		ret = ft_get_cntr_comp(txcntr, total, -1);
	} else {
		FT_ERR("Trying to get a TX completion when no TX CQ or counter were opened");
		ret = -FI_EOTHER;
	}
	return ret;
}

int ft_tx_msg(struct fid_ep *ep, fi_addr_t fi_addr, size_t size, void *ctx, uint64_t flags)
{
	int ret;

	if (ft_check_opts(FT_OPT_VERIFY_DATA | FT_OPT_ACTIVE)) {
		ret = ft_fill_buf((char *) tx_buf + ft_tx_prefix_size(), size);
		if (ret)
			return ret;
	}

	ret = ft_sendmsg(ep, fi_addr, size, ctx, flags);
	if (ret)
		return ret;

	ret = ft_get_tx_comp(tx_seq);
	return ret;
}

int ft_sendmsg(struct fid_ep *ep, fi_addr_t fi_addr,
		size_t size, void *ctx, int flags)
{
	struct fi_msg msg;
	struct fi_msg_tagged tagged_msg;
	struct iovec msg_iov;

	msg_iov.iov_base = tx_buf;
	msg_iov.iov_len = size + ft_tx_prefix_size();

	if (hints->caps & FI_TAGGED) {
		tagged_msg.msg_iov = &msg_iov;
		tagged_msg.desc = &mr_desc;
		tagged_msg.iov_count = 1;
		tagged_msg.addr = fi_addr;
		tagged_msg.data = NO_CQ_DATA;
		tagged_msg.context = ctx;
		tagged_msg.tag = ft_tag ? ft_tag : tx_seq;
		tagged_msg.ignore = 0;

		FT_POST(fi_tsendmsg, ft_progress, txcq, tx_seq,
			&tx_cq_cntr, "tsendmsg", ep, &tagged_msg,
			flags);
	} else {
		msg.msg_iov = &msg_iov;
		msg.desc = &mr_desc;
		msg.iov_count = 1;
		msg.addr = fi_addr;
		msg.data = NO_CQ_DATA;
		msg.context = ctx;

		FT_POST(fi_sendmsg, ft_progress, txcq, tx_seq,
			&tx_cq_cntr, "sendmsg", ep, &msg,
			flags);
	}

	return 0;
}


int ft_recvmsg(struct fid_ep *ep, fi_addr_t fi_addr,
	       size_t size, void *ctx, int flags)
{
	struct fi_msg msg;
	struct fi_msg_tagged tagged_msg;
	struct iovec msg_iov;

	msg_iov.iov_base = rx_buf;
	msg_iov.iov_len = size;

	if (hints->caps & FI_TAGGED) {
		tagged_msg.msg_iov = &msg_iov;
		tagged_msg.desc = &mr_desc;
		tagged_msg.iov_count = 1;
		tagged_msg.addr = fi_addr;
		tagged_msg.data = NO_CQ_DATA;
		tagged_msg.context = ctx;
		tagged_msg.tag = ft_tag ? ft_tag : tx_seq;
		tagged_msg.ignore = 0;

		FT_POST(fi_trecvmsg, ft_progress, rxcq, rx_seq,
			&rx_cq_cntr, "trecvmsg", ep, &tagged_msg,
			flags);
	} else {
		msg.msg_iov = &msg_iov;
		msg.desc = &mr_desc;
		msg.iov_count = 1;
		msg.addr = fi_addr;
		msg.data = NO_CQ_DATA;
		msg.context = ctx;

		FT_POST(fi_recvmsg, ft_progress, rxcq, rx_seq,
			&rx_cq_cntr, "recvmsg", ep, &msg,
			flags);
	}

	return 0;
}

int ft_cq_read_verify(struct fid_cq *cq, void *op_context)
{
	int ret;
	struct fi_cq_err_entry completion;

	do {
		/* read events from the completion queue */
		ret = fi_cq_read(cq, (void *)&completion, 1);

		if (ret > 0) {
			if (op_context != completion.op_context) {
				fprintf(stderr, "ERROR: op ctx=%p cq_ctx=%p\n",
					op_context, completion.op_context);
				return -FI_EOTHER;
			}
			if (!ft_tag_is_valid(cq, &completion,
					     ft_tag ? ft_tag : rx_cq_cntr))
				return -FI_EOTHER;
		} else if ((ret <= 0) && (ret != -FI_EAGAIN)) {
			FT_PRINTERR("POLL: Error\n", ret);
			if (ret == -FI_EAVAIL)
				FT_PRINTERR("POLL: error available\n", ret);
			return -FI_EOTHER;
		}
	} while (ret == -FI_EAGAIN);

	return 0;
}

int ft_cq_readerr(struct fid_cq *cq)
{
	struct fi_cq_err_entry cq_err;
	int ret;

	memset(&cq_err, 0, sizeof(cq_err));
	ret = fi_cq_readerr(cq, &cq_err, 0);
	if (ret < 0) {
		FT_PRINTERR("fi_cq_readerr", ret);
	} else {
		FT_CQ_ERR(cq, cq_err, NULL, 0);
		ret = -cq_err.err;
	}
	return ret;
}

void eq_readerr(struct fid_eq *eq, const char *eq_str)
{
	struct fi_eq_err_entry eq_err;
	int rd;

	memset(&eq_err, 0, sizeof(eq_err));
	rd = fi_eq_readerr(eq, &eq_err, 0);
	if (rd != sizeof(eq_err)) {
		FT_PRINTERR("fi_eq_readerr", rd);
	} else {
		FT_EQ_ERR(eq, eq_err, NULL, 0);
	}
}

int ft_sync()
{
	char buf = 'a';
	int ret;

	if (opts.dst_addr) {
		if (!(opts.options & FT_OPT_OOB_SYNC)) {
			ret = ft_tx_msg(ep, remote_fi_addr, 1, &tx_ctx, FI_DELIVERY_COMPLETE);
			if (ret)
				return ret;

			ret = ft_rx(ep, 1);
		} else {
			ret = ft_sock_send(oob_sock, &buf, 1);
			if (ret)
				return ret;

			ret = ft_sock_recv(oob_sock, &buf, 1);
			if (ret)
				return ret;
		}
	} else {
		if (!(opts.options & FT_OPT_OOB_SYNC)) {
			ret = ft_rx(ep, 1);
			if (ret)
				return ret;

			ret = ft_tx_msg(ep, remote_fi_addr, 1, &tx_ctx, FI_DELIVERY_COMPLETE);
			if (ret)
				return ret;
		} else {
			ret = ft_sock_recv(oob_sock, &buf, 1);
			if (ret)
				return ret;

			ret = ft_sock_send(oob_sock, &buf, 1);
			if (ret)
				return ret;
		}
	}

	return ret;
}

int ft_sync_pair(int status)
{
	int ret;
	int pair_status;

	if (ft_parent_proc) {
		ret = write(ft_socket_pair[1], &status, sizeof(int));
		if (ret < 0) {
			FT_PRINTERR("write", errno);
			return ret;
		}
		ret = read(ft_socket_pair[1], &pair_status, sizeof(int));
		if (ret < 0) {
			FT_PRINTERR("read", errno);
			return ret;
		}
	} else {
		ret = read(ft_socket_pair[0], &pair_status, sizeof(int));
		if (ret < 0) {
			FT_PRINTERR("read", errno);
			return ret;
		}
		ret = write(ft_socket_pair[0], &status, sizeof(int));
		if (ret < 0) {
			FT_PRINTERR("write", errno);
			return ret;
		}
	}

	/* check status reported the other guy */
	if (pair_status != FI_SUCCESS)
		return pair_status;

	return 0;
}

int ft_fork_and_pair(void)
{
	int ret;

	ret = socketpair(AF_LOCAL, SOCK_STREAM, 0, ft_socket_pair);
	if (ret) {
		FT_PRINTERR("socketpair", errno);
		return -errno;
	}

	ft_child_pid = fork();
	if (ft_child_pid < 0) {
		FT_PRINTERR("fork", ft_child_pid);
		return -errno;
	}
	if (ft_child_pid)
		ft_parent_proc = 1;

	return 0;
}

int ft_fork_child(void)
{
	ft_child_pid = fork();
	if (ft_child_pid < 0) {
		FT_PRINTERR("fork", ft_child_pid);
		return -errno;
	}

	if (ft_child_pid == 0) {
		exit(0);
	}

	return 0;
}

int ft_wait_child(void)
{
	int ret;

	ret = ft_close_fd(ft_socket_pair[0]);
	if (ret) {
		FT_PRINTERR("ft_close_fd", errno);
		return ret;
	}
	ret = ft_close_fd(ft_socket_pair[1]);
	if (ret) {
		FT_PRINTERR("ft_close_fd", errno);
		return ret;
	}
	if (ft_parent_proc) {
		ret = waitpid(ft_child_pid, NULL, WCONTINUED);
		if (ret < 0) {
			FT_PRINTERR("waitpid", errno);
			return ret;
		}
	}

	return 0;
}

int ft_finalize_ep(struct fid_ep *ep)
{
	int ret;
	struct fi_context ctx;

	ret = ft_sendmsg(ep, remote_fi_addr, 4, &ctx, FI_TRANSMIT_COMPLETE);
	if (ret)
		return ret;

	ret = ft_get_tx_comp(tx_seq);
	if (ret)
		return ret;

	ret = ft_get_rx_comp(rx_seq);
	if (ret)
		return ret;

	return 0;
}

int ft_finalize(void)
{
	int ret;

	if (fi->domain_attr->mr_mode & FI_MR_RAW) {
		ret = fi_mr_unmap_key(domain, remote.key);
		if (ret)
			return ret;
	}

	return ft_finalize_ep(ep);
}

int64_t get_elapsed(const struct timespec *b, const struct timespec *a,
		    enum precision p)
{
	int64_t elapsed;

	elapsed = difftime(a->tv_sec, b->tv_sec) * 1000 * 1000 * 1000;
	elapsed += a->tv_nsec - b->tv_nsec;
	return elapsed / p;
}

void show_perf(char *name, size_t tsize, int iters, struct timespec *start,
		struct timespec *end, int xfers_per_iter)
{
	static int header = 1;
	char str[FT_STR_LEN];
	int64_t elapsed = get_elapsed(start, end, MICRO);
	long long bytes = (long long) iters * tsize * xfers_per_iter;
	float usec_per_xfer;

	if (name) {
		if (header) {
			printf("%-50s%-8s%-8s%-8s%8s %10s%13s%13s\n",
					"name", "bytes", "iters",
					"total", "time", "MB/sec",
					"usec/xfer", "Mxfers/sec");
			header = 0;
		}

		printf("%-50s", name);
	} else {
		if (header) {
			printf("%-8s%-8s%-8s%8s %10s%13s%13s\n",
					"bytes", "iters", "total",
					"time", "MB/sec", "usec/xfer",
					"Mxfers/sec");
			header = 0;
		}
	}

	printf("%-8s", size_str(str, tsize));

	printf("%-8s", cnt_str(str, iters));

	printf("%-8s", size_str(str, bytes));

	usec_per_xfer = ((float)elapsed / iters / xfers_per_iter);
	printf("%8.2fs%10.2f%11.2f%11.2f\n",
		elapsed / 1000000.0, bytes / (1.0 * elapsed),
		usec_per_xfer, 1.0/usec_per_xfer);
}

void show_perf_mr(size_t tsize, int iters, struct timespec *start,
		  struct timespec *end, int xfers_per_iter, int argc, char *argv[])
{
	static int header = 1;
	int64_t elapsed = get_elapsed(start, end, MICRO);
	long long total = (long long) iters * tsize * xfers_per_iter;
	int i;
	float usec_per_xfer;

	if (header) {
		printf("---\n");

		for (i = 0; i < argc; ++i)
			printf("%s ", argv[i]);

		printf(":\n");
		header = 0;
	}

	usec_per_xfer = ((float)elapsed / iters / xfers_per_iter);

	printf("- { ");
	printf("xfer_size: %zu, ", tsize);
	printf("iterations: %d, ", iters);
	printf("total: %lld, ", total);
	printf("time: %f, ", elapsed / 1000000.0);
	printf("MB/sec: %f, ", (total) / (1.0 * elapsed));
	printf("usec/xfer: %f, ", usec_per_xfer);
	printf("Mxfers/sec: %f", 1.0/usec_per_xfer);
	printf(" }\n");
}

void ft_addr_usage()
{
	FT_PRINT_OPTS_USAGE("-B <src_port>", "non default source port number");
	FT_PRINT_OPTS_USAGE("-P <dst_port>", "non default destination port number");
	FT_PRINT_OPTS_USAGE("-s <address>", "source address");
	FT_PRINT_OPTS_USAGE("-b[=<oob_port>]", "enable out-of-band address exchange and "
			"synchronization over the, optional, port");
	FT_PRINT_OPTS_USAGE("-E[=<oob_port>]", "enable out-of-band address exchange only "
			"over the, optional, port");
	FT_PRINT_OPTS_USAGE("-C <number>", "simultaneous connections to server");
	FT_PRINT_OPTS_USAGE("-O <addr>", "use the provided addr for out of band");
	FT_PRINT_OPTS_USAGE("-F <addr_format>", "Address format (default:FI_FORMAT_UNSPEC)");
}

void ft_usage(char *name, char *desc)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  %s [OPTIONS]\t\tstart server\n", name);
	fprintf(stderr, "  %s [OPTIONS] <host>\tconnect to server\n", name);

	if (desc)
		fprintf(stderr, "\n%s\n", desc);

	fprintf(stderr, "\nOptions:\n");
	ft_addr_usage();
	FT_PRINT_OPTS_USAGE("-f <fabric>", "fabric name");
	FT_PRINT_OPTS_USAGE("-d <domain>", "domain name");
	FT_PRINT_OPTS_USAGE("-p <provider>", "specific provider name eg sockets, verbs");
	FT_PRINT_OPTS_USAGE("-e <ep_type>", "Endpoint type: msg|rdm|dgram (default:rdm)");
	FT_PRINT_OPTS_USAGE("", "Only the following tests support this option for now:");
	FT_PRINT_OPTS_USAGE("", "fi_rma_bw");
	FT_PRINT_OPTS_USAGE("", "fi_shared_ctx");
	FT_PRINT_OPTS_USAGE("", "fi_multi_mr");
	FT_PRINT_OPTS_USAGE("", "fi_multi_ep");
	FT_PRINT_OPTS_USAGE("", "fi_recv_cancel");
	FT_PRINT_OPTS_USAGE("", "fi_unexpected_msg");
	FT_PRINT_OPTS_USAGE("", "fi_resmgmt_test");
	FT_PRINT_OPTS_USAGE("", "fi_bw");
	FT_PRINT_OPTS_USAGE("-U", "run fabtests with FI_DELIVERY_COMPLETE set");
	FT_PRINT_OPTS_USAGE("", "Only the following tests support this option for now:");
	FT_PRINT_OPTS_USAGE("", "fi_bw");
	FT_PRINT_OPTS_USAGE("", "fi_rdm");
	FT_PRINT_OPTS_USAGE("", "fi_rdm_atomic");
	FT_PRINT_OPTS_USAGE("", "fi_rdm_pingpong");
	FT_PRINT_OPTS_USAGE("", "fi_rdm_tagged_bw");
	FT_PRINT_OPTS_USAGE("", "fi_rdm_tagged_pingpong");
	FT_PRINT_OPTS_USAGE("", "fi_rma_bw");
	FT_PRINT_OPTS_USAGE("-M <mode>", "Disable mode bit from test");
	FT_PRINT_OPTS_USAGE("-K", "fork a child process after initializing endpoint");
	FT_PRINT_OPTS_USAGE("", "mr_local");
	FT_PRINT_OPTS_USAGE("-a <address vector name>", "name of address vector");
	FT_PRINT_OPTS_USAGE("-h", "display this help output");

	return;
}

void ft_hmem_usage()
{
	FT_PRINT_OPTS_USAGE("-D <device_iface>", "Specify device interface: "
			    "e.g. cuda, ze, neuron, synapseai (default: None). "
			    "Automatically enables FI_HMEM (-H)");
	FT_PRINT_OPTS_USAGE("-i <device_id>", "Specify which device to use (default: 0)");
	FT_PRINT_OPTS_USAGE("-H", "Enable provider FI_HMEM support");
	FT_PRINT_OPTS_USAGE("-R", "Register HMEM memory with fi_mr_dmabuf API");
}

void ft_mcusage(char *name, char *desc)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  %s [OPTIONS] -M <mcast_addr>\tstart listener\n", name);
	fprintf(stderr, "  %s [OPTIONS] <mcast_addr>\tsend to group\n", name);

	if (desc)
		fprintf(stderr, "\n%s\n", desc);

	fprintf(stderr, "\nOptions:\n");
	ft_addr_usage();
	FT_PRINT_OPTS_USAGE("-f <fabric>", "fabric name");
	FT_PRINT_OPTS_USAGE("-d <domain>", "domain name");
	FT_PRINT_OPTS_USAGE("-p <provider>", "specific provider name eg sockets, verbs");
	ft_hmem_usage();
	FT_PRINT_OPTS_USAGE("-h", "display this help output");

	return;
}

void ft_csusage(char *name, char *desc)
{
	ft_usage(name, desc);
	FT_PRINT_OPTS_USAGE("-I <number>", "number of iterations");
	FT_PRINT_OPTS_USAGE("-Q", "bind EQ to domain (vs. endpoint)");
	FT_PRINT_OPTS_USAGE("-w <number>", "number of warmup iterations");
	FT_PRINT_OPTS_USAGE("-S <size>", "specific transfer size or "
			    "a range of sizes (syntax r:start,inc,end) or "
			    "a list of sizes (syntax l:1,1,2,3,5,...) or 'all'");
	FT_PRINT_OPTS_USAGE("-l", "align transmit and receive buffers to page size");
	FT_PRINT_OPTS_USAGE("-m", "machine readable output");
	ft_hmem_usage();
	FT_PRINT_OPTS_USAGE("-t <type>", "completion type [queue, counter]");
	FT_PRINT_OPTS_USAGE("-c <method>", "completion method [spin, sread, fd, yield]");
	FT_PRINT_OPTS_USAGE("-h", "display this help output");

	return;
}

void ft_parseinfo(int op, char *optarg, struct fi_info *hints,
		  struct ft_opts *opts)
{
	switch (op) {
	case 'f':
		if (!hints->fabric_attr) {
			hints->fabric_attr = malloc(sizeof *(hints->fabric_attr));
			if (!hints->fabric_attr) {
				perror("malloc");
				exit(EXIT_FAILURE);
			}
		}
		hints->fabric_attr->name = strdup(optarg);
		break;
	case 'd':
		if (!hints->domain_attr) {
			hints->domain_attr = malloc(sizeof *(hints->domain_attr));
			if (!hints->domain_attr) {
				perror("malloc");
				exit(EXIT_FAILURE);
			}
		}
		hints->domain_attr->name = strdup(optarg);
		break;
	case 'p':
		if (!hints->fabric_attr) {
			hints->fabric_attr = malloc(sizeof *(hints->fabric_attr));
			if (!hints->fabric_attr) {
				perror("malloc");
				exit(EXIT_FAILURE);
			}
		}
		hints->fabric_attr->prov_name = strdup(optarg);
		break;
	case 'e':
		if (!strncasecmp("msg", optarg, 3))
			hints->ep_attr->type = FI_EP_MSG;
		if (!strncasecmp("rdm", optarg, 3))
			hints->ep_attr->type = FI_EP_RDM;
		if (!strncasecmp("dgram", optarg, 5))
			hints->ep_attr->type = FI_EP_DGRAM;
		break;
	case 'M':
		if (!strncasecmp("mr_local", optarg, 8))
			opts->mr_mode &= ~FI_MR_LOCAL;
		break;
	case 'K':
		opts->options |= FT_OPT_FORK_CHILD;
		break;
	default:
		ft_parse_hmem_opts(op, optarg, opts);
		/* let getopt handle unknown opts*/
		break;
	}
}

void ft_parse_addr_opts(int op, char *optarg, struct ft_opts *opts)
{
	switch (op) {
	case 's':
		opts->src_addr = optarg;
		break;
	case 'B':
		opts->src_port = optarg;
		break;
	case 'P':
		opts->dst_port = optarg;
		break;
	case 'b':
		opts->options |= FT_OPT_OOB_SYNC;
		/* fall through */
	case 'E':
		opts->options |= FT_OPT_OOB_ADDR_EXCH;
		if (optarg && strlen(optarg) > 1)
			opts->oob_port = optarg + 1;
		else
			opts->oob_port = default_oob_port;
		if (!opts->oob_addr)
			opts->options |= FT_OPT_ADDR_IS_OOB;
		break;
	case 'F':
		if (!strncasecmp("fi_addr_str", optarg, 11))
			opts->address_format = FI_ADDR_STR;
		else if (!strncasecmp("fi_sockaddr_in6", optarg, 15))
			opts->address_format = FI_SOCKADDR_IN6;
		else if (!strncasecmp("fi_sockaddr_in", optarg, 14))
			opts->address_format = FI_SOCKADDR_IN;
		else if (!strncasecmp("fi_sockaddr_ib", optarg, 14))
			opts->address_format = FI_SOCKADDR_IB;
		else if (!strncasecmp("fi_sockaddr", optarg, 11)) /* keep me last */
			opts->address_format = FI_SOCKADDR;
		break;
	case 'C':
		opts->options |= FT_OPT_SERVER_PERSIST;
		opts->num_connections = atoi(optarg);
		break;
	case 'O':
		opts->oob_addr = optarg;
		opts->options &= ~FT_OPT_ADDR_IS_OOB;
		break;
	default:
		/* let getopt handle unknown opts*/
		break;
	}
}

void ft_parse_hmem_opts(int op, char *optarg, struct ft_opts *opts)
{
	switch (op) {
	case 'D':
		if (!strncasecmp("ze", optarg, 2))
			opts->iface = FI_HMEM_ZE;
		else if (!strncasecmp("cuda", optarg, 4))
			opts->iface = FI_HMEM_CUDA;
		else if (!strncasecmp("neuron", optarg, 6))
			opts->iface = FI_HMEM_NEURON;
		else if (!strncasecmp("synapseai", optarg, 9)) {
			opts->iface = FI_HMEM_SYNAPSEAI;
			opts->options |= FT_OPT_REG_DMABUF_MR;
		}
		else
			printf("Unsupported interface\n");
		opts->options |= FT_OPT_ENABLE_HMEM | FT_OPT_USE_DEVICE;
		break;
	case 'i':
		opts->device = atoi(optarg);
		break;
	case 'H':
		opts->options |= FT_OPT_ENABLE_HMEM;
		break;
	case 'R':
		opts->options |= FT_OPT_REG_DMABUF_MR;
		break;
	default:
		/* Let getopt handle unknown opts*/
		break;
	}
}

void ft_parse_opts_range(char* optarg)
{
	size_t start, inc, end;
	int i, ret;

	ret = sscanf(optarg, "r:%zd,%zd,%zd", &start, &inc, &end);
	if (ret != 3) {
		perror("sscanf");
		exit(EXIT_FAILURE);
	}
	assert(end >= start && inc > 0);
	test_cnt = (end - start) / inc + 1;
	user_test_sizes = calloc(test_cnt, sizeof(*user_test_sizes));
	if (!user_test_sizes) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}
	for (i = 0; i < test_cnt && i < end; i++) {
		user_test_sizes[i].size = start + (i * inc);
		user_test_sizes[i].enable_flags = 0;
	}
	test_size = user_test_sizes;
}

void ft_parse_opts_list(char* optarg)
{
	int i, ret;
	char *token;
	char *saveptr;

	optarg += 2; // remove 'l:'
	test_cnt = 1;
	for (i = 0; optarg[i] != '\0'; i++) {
		test_cnt += optarg[i] == ',';
	}
	user_test_sizes = calloc(test_cnt, sizeof(*user_test_sizes));
	if (!user_test_sizes) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}

	token = strtok_r(optarg, ",", &saveptr);
	test_cnt = 0;
	while (token != NULL) {
		ret = sscanf(token, "%zu", &user_test_sizes[test_cnt].size);
		if (ret != 1) {
			fprintf(stderr, "Cannot parse integer \"%s\" in list.\n",token);
			exit(EXIT_FAILURE);
		}
		test_cnt++;
		token = strtok_r(NULL, ",", &saveptr);
	}
	test_size = user_test_sizes;
}

void ft_parsecsopts(int op, char *optarg, struct ft_opts *opts)
{
	ft_parse_addr_opts(op, optarg, opts);

	switch (op) {
	case 'I':
		opts->options |= FT_OPT_ITER;
		opts->iterations = atoi(optarg);
		break;
	case 'Q':
		opts->options |= FT_OPT_DOMAIN_EQ;
		break;
	case 'S':
		if (!strncasecmp("all", optarg, 3)) {
			opts->sizes_enabled = FT_ENABLE_SIZES;
		} else if (!strncasecmp("r:", optarg, 2)){
			opts->sizes_enabled = FT_ENABLE_SIZES;
			ft_parse_opts_range(optarg);
		} else if (!strncasecmp("l:", optarg, 2)){
			opts->sizes_enabled = FT_ENABLE_SIZES;
			ft_parse_opts_list(optarg);
		} else {
			opts->options |= FT_OPT_SIZE;
			opts->transfer_size = atol(optarg);
		}
		break;
	case 'm':
		opts->machr = 1;
		break;
	case 'c':
		if (!strncasecmp("sread", optarg, 5))
			opts->comp_method = FT_COMP_SREAD;
		else if (!strncasecmp("fd", optarg, 2))
			opts->comp_method = FT_COMP_WAIT_FD;
		else if (!strncasecmp("yield", optarg, 5))
			opts->comp_method = FT_COMP_YIELD;
		break;
	case 't':
		if (!strncasecmp("counter", optarg, 7)) {
			opts->options |= FT_OPT_RX_CNTR | FT_OPT_TX_CNTR;
			opts->options &= ~(FT_OPT_RX_CQ | FT_OPT_TX_CQ);
		}
		break;
	case 'a':
		opts->av_name = optarg;
		break;
	case 'w':
		opts->warmup_iterations = atoi(optarg);
		break;
	case 'l':
		opts->options |= FT_OPT_ALIGN;
		break;
	default:
		/* let getopt handle unknown opts*/
		break;
	}
}

int ft_parse_api_opts(int op, char *optarg, struct fi_info *hints,
		      struct ft_opts *opts)
{
	switch (op) {
	case 'o':
		if (!strcasecmp(optarg, "read")) {
			hints->caps |= FI_READ | FI_REMOTE_READ;
			opts->rma_op = FT_RMA_READ;
		} else if (!strcasecmp(optarg, "writedata")) {
			hints->caps |= FI_WRITE | FI_REMOTE_WRITE;
			hints->mode |= FI_RX_CQ_DATA;
			hints->domain_attr->cq_data_size = 4;
			opts->rma_op = FT_RMA_WRITEDATA;
			opts->cqdata_op = FT_CQDATA_WRITEDATA;
			cq_attr.format = FI_CQ_FORMAT_DATA;
		} else if (!strcasecmp(optarg, "senddata")) {
			hints->mode |= FI_RX_CQ_DATA;
			hints->domain_attr->cq_data_size = 4;
			opts->cqdata_op = FT_CQDATA_SENDDATA;
			cq_attr.format = FI_CQ_FORMAT_DATA;
		} else if (!strcasecmp(optarg, "write")) {
			hints->caps |= FI_WRITE | FI_REMOTE_WRITE;
			opts->rma_op = FT_RMA_WRITE;
		} else if (!strcasecmp(optarg, "msg")) {
			hints->caps |= FI_MSG;
		} else if (!strcasecmp(optarg, "tagged")) {
			hints->caps |= FI_TAGGED;
		} else {
			fprintf(stderr, "Invalid operation type: \"%s\"."
				"Usage:\n-o <op>\top: "
				"read|write|writedata|msg|tagged\n", optarg);
			return EXIT_FAILURE;
		}
		break;
	default:
		/* let getopt handle unknown opts*/
		break;
	}
	return 0;
}

int ft_fill_buf(void *buf, size_t size)
{
	char *msg_buf;
	int msg_index = 0;
	size_t i;
	int ret = 0;

	if (opts.iface != FI_HMEM_SYSTEM) {
		assert(dev_host_buf);
		msg_buf = dev_host_buf;
	} else {
		msg_buf = (char *) buf;
	}

	for (i = 0; i < size; i++) {
		msg_buf[i] = integ_alphabet[msg_index];
		if (++msg_index >= integ_alphabet_length)
			msg_index = 0;
	}

	if (opts.iface != FI_HMEM_SYSTEM) {
		ret = ft_hmem_copy_to(opts.iface, opts.device, buf, msg_buf, size);
		if (ret)
			goto out;
	}
out:
	return ret;
}

int ft_check_buf(void *buf, size_t size)
{
	char *recv_data;
	char c;
	int msg_index = 0;
	size_t i;
	int ret = 0;

	if (opts.iface != FI_HMEM_SYSTEM) {
		assert(dev_host_buf);
		ret = ft_hmem_copy_from(opts.iface, opts.device,
					dev_host_buf, buf, size);
		if (ret)
			return ret;
		recv_data = (char *)dev_host_buf;
	} else {
		recv_data = (char *)buf;
	}

	for (i = 0; i < size; i++) {
		c = integ_alphabet[msg_index];
		if (++msg_index >= integ_alphabet_length)
			msg_index = 0;
		if (c != recv_data[i])
			break;
	}
	if (i != size) {
		printf("Data check error (%c!=%c) at byte %zu for "
		       "buffer size %zu\n", c, recv_data[i], i, size);
		ret = -FI_EIO;
	}

	return ret;
}

int ft_rma_poll_buf(void *buf, int iter, size_t size)
{
	volatile char *recv_data;
	struct timespec a, b;

	if (opts.iface != FI_HMEM_SYSTEM) {
		FT_ERR("FI_HMEM not supported for write latency test");
		return EXIT_FAILURE;
	}

	recv_data = (char *)buf + size - 1;

	if (timeout >= 0)
		clock_gettime(CLOCK_MONOTONIC, &a);

	char expected_val = (char)iter;
	while (*recv_data != expected_val) {

		ft_force_progress();

		if (timeout >= 0) {
			clock_gettime(CLOCK_MONOTONIC, &b);
			if ((b.tv_sec - a.tv_sec) > timeout) {
				fprintf(stderr, "%ds timeout expired\n", timeout);
				return -FI_ENODATA;
			}
		}
	}

	return 0;
}

uint64_t ft_init_cq_data(struct fi_info *info)
{
	if (info->domain_attr->cq_data_size >= sizeof(uint64_t)) {
		return 0x0123456789abcdefULL;
	} else {
		return 0x0123456789abcdef &
			((0x1ULL << (info->domain_attr->cq_data_size * 8)) - 1);
	}
}

int check_recv_msg(const char *message)
{
	size_t recv_len;
	size_t message_len = strlen(message) + 1;
	char *recv_buf;
	int ret;

	/* Account for null terminated byte. */

	if (opts.iface != FI_HMEM_SYSTEM) {
		assert(dev_host_buf);
		ret = ft_hmem_copy_from(opts.iface, opts.device, dev_host_buf,
					rx_buf, message_len);
		if (ret) {
			fprintf(stderr, "Received length does not match expected length.\n");
			return -1;
		}
		recv_buf = dev_host_buf;
	} else {
		recv_buf = rx_buf;
	}

	recv_len = strlen(recv_buf) + 1;

	if (recv_len != message_len) {
		fprintf(stderr, "Received length does not match expected length.\n");
		return -1;
	}

	if (strncmp(recv_buf, message, message_len)) {
		fprintf(stderr, "Received message does not match expected message.\n");
		return -1;
	}
	fprintf(stdout, "Data check OK\n");
	fprintf(stdout, "Received data from client: %s\n", (char *) recv_buf);
	return 0;
}

int ft_send_greeting(struct fid_ep *ep)
{
	size_t message_len = strlen(greeting) + 1;
	int ret;

	fprintf(stdout, "Sending message...\n");
	if (message_len >= tx_size) {
		fprintf(stderr, "Transmit buffer too small.\n");
		return -FI_ETOOSMALL;
	}

	if (opts.iface == FI_HMEM_SYSTEM) {
		snprintf(tx_buf, tx_size, "%s", greeting);
	} else {
		assert(dev_host_buf);
		snprintf(dev_host_buf, tx_size, "%s", greeting);
		ret = ft_hmem_copy_to(opts.iface, opts.device, tx_buf,
				      dev_host_buf, message_len);
		if (ret) {
			fprintf(stderr, "Error copying to device buffer\n");
			return ret;
		}
	}

	ret = ft_tx(ep, remote_fi_addr, message_len, &tx_ctx);
	if (ret)
		return ret;

	fprintf(stdout, "Send completion received\n");
	return 0;
}

int ft_recv_greeting(struct fid_ep *ep)
{
	int ret;

	fprintf(stdout, "Waiting for message from client...\n");
	ret = ft_get_rx_comp(rx_seq);
	if (ret)
		return ret;

	ret = check_recv_msg(greeting);
	if (ret)
		return ret;

	return 0;
}

int ft_send_recv_greeting(struct fid_ep *ep)
{
	return opts.dst_addr ? ft_send_greeting(ep) : ft_recv_greeting(ep);
}

int ft_sock_listen(char *node, char *service)
{
	struct addrinfo *ai, hints;
	int val, ret;

	memset(&hints, 0, sizeof hints);
	hints.ai_flags = AI_PASSIVE;

	ret = getaddrinfo(node, service, &hints, &ai);
	if (ret) {
		fprintf(stderr, "getaddrinfo() %s\n", gai_strerror(ret));
		return ret;
	}

	listen_sock = socket(ai->ai_family, SOCK_STREAM, 0);
	if (listen_sock < 0) {
		perror("socket");
		ret = listen_sock;
		goto out;
	}

	val = 1;
	ret = setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR,
			 (void *) &val, sizeof val);
	if (ret) {
		perror("setsockopt SO_REUSEADDR");
		goto out;
	}

	ret = bind(listen_sock, ai->ai_addr, ai->ai_addrlen);
	if (ret) {
		perror("bind");
		goto out;
	}

	ret = listen(listen_sock, 511);
	if (ret)
		perror("listen");

out:
	if (ret && listen_sock >= 0)
		ft_close_fd(listen_sock);
	freeaddrinfo(ai);
	return ret;
}

int ft_sock_connect(char *node, char *service)
{
	struct addrinfo *ai;
	int ret;

	ret = getaddrinfo(node, service, NULL, &ai);
	if (ret) {
		perror("getaddrinfo");
		return ret;
	}

	sock = socket(ai->ai_family, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("socket");
		ret = sock;
		goto free;
	}

	ret = connect(sock, ai->ai_addr, ai->ai_addrlen);
	if (ret) {
		perror("connect");
		ft_close_fd(sock);
		goto free;
	}

	ret = ft_sock_setup(sock);
free:
	freeaddrinfo(ai);
	return ret;
}

int ft_sock_accept()
{
	int ret;

	sock = accept(listen_sock, NULL, 0);
	if (sock < 0) {
		ret = sock;
		perror("accept");
		return ret;
	}

	ret = ft_sock_setup(sock);
	return ret;
}

int ft_sock_send(int fd, void *msg, size_t len)
{
	size_t sent;
	ssize_t ret, err = 0;

	for (sent = 0; sent < len; ) {
		ret = ofi_send_socket(fd, ((char *) msg) + sent, len - sent, 0);
		if (ret > 0) {
			sent += ret;
		} else if (ofi_sockerr() == EAGAIN || ofi_sockerr() == EWOULDBLOCK) {
			ft_force_progress();
		} else {
			err = -ofi_sockerr();
			break;
		}
	}

	return err ? err: 0;
}

int ft_sock_recv(int fd, void *msg, size_t len)
{
	size_t rcvd;
	ssize_t ret, err = 0;

	for (rcvd = 0; rcvd < len; ) {
		ret = ofi_recv_socket(fd, ((char *) msg) + rcvd, len - rcvd, 0);
		if (ret > 0) {
			rcvd += ret;
		} else if (ret == 0) {
			err = -FI_ENOTCONN;
			break;
		} else if (ofi_sockerr() == EAGAIN || ofi_sockerr() == EWOULDBLOCK) {
			ft_force_progress();
		} else {
			err = -ofi_sockerr();
			break;
		}
	}

	return err ? err: 0;
}

int ft_sock_sync(int fd, int value)
{
	int result = -FI_EOTHER;
	int ret;

	if (listen_sock < 0) {
		ret = ft_sock_send(fd, &value,  sizeof value);
		if (ret) {
			FT_PRINTERR("ft_sock_send", ret);
			return ret;
		}

		ret = ft_sock_recv(fd, &result, sizeof result);
		if (ret) {
			FT_PRINTERR("ft_sock_recv", ret);
			return ret;
		}
	} else {
		ret = ft_sock_recv(fd, &result, sizeof result);
		if (ret) {
			FT_PRINTERR("ft_sock_recv", ret);
			return ret;
		}

		ret = ft_sock_send(fd, &value,  sizeof value);
		if (ret) {
			FT_PRINTERR("ft_sock_send", ret);
			return ret;
		}
	}

	return result;
}

void ft_sock_shutdown(int fd)
{
	shutdown(fd, SHUT_RDWR);
	ft_close_fd(fd);
}

static int ft_has_util_prefix(const char *str)
{
	return !strncasecmp(str, OFI_UTIL_PREFIX, strlen(OFI_UTIL_PREFIX));
}

const char *ft_util_name(const char *str, size_t *len)
{
	char *delim;

	delim = strchr(str, OFI_NAME_DELIM);
	if (delim) {
		if (ft_has_util_prefix(delim + 1)) {
			*len = strlen(delim + 1);
			return delim + 1;
		} else if (ft_has_util_prefix(str)) {
			*len = delim - str;
			return str;
		}
	} else if (ft_has_util_prefix(str)) {
		*len = strlen(str);
		return str;
	}
	*len = 0;
	return NULL;
}

const char *ft_core_name(const char *str, size_t *len)
{
	char *delim;

	delim = strchr(str, OFI_NAME_DELIM);
	if (delim) {
		if (!ft_has_util_prefix(delim + 1)) {
			*len = strlen(delim + 1);
			return delim + 1;
		} else if (!ft_has_util_prefix(str)) {
			*len = delim - str;
			return str;
		}
	} else if (!ft_has_util_prefix(str)) {
		*len = strlen(str);
		return str;
	}
	*len = 0;
	return NULL;
}

/* Split the given string "s" using the specified delimiter(s) in the string
 * "delim" and return an array of strings. The array is terminated with a NULL
 * pointer. Returned array should be freed with ft_free_string_array().
 *
 * Returns NULL on failure.
 */

char **ft_split_and_alloc(const char *s, const char *delim, size_t *count)
{
	int i, n;
	char *tmp;
	char *dup = NULL;
	char **arr = NULL;

	if (!s || !delim)
		return NULL;

	dup = strdup(s);
	if (!dup)
		return NULL;

	/* compute the array size */
	n = 1;
	for (tmp = dup; *tmp != '\0'; ++tmp) {
		for (i = 0; delim[i] != '\0'; ++i) {
			if (*tmp == delim[i]) {
				++n;
				break;
			}
		}
	}

	/* +1 to leave space for NULL terminating pointer */
	arr = calloc(n + 1, sizeof(*arr));
	if (!arr)
		goto cleanup;

	/* set array elts to point inside the dup'ed string */
	for (tmp = dup, i = 0; tmp != NULL; ++i) {
		arr[i] = strsep(&tmp, delim);
	}
	assert(i == n);

	if (count)
		*count = n;
	return arr;

cleanup:
	free(dup);
	free(arr);
	return NULL;
}

/* see ft_split_and_alloc() */
void ft_free_string_array(char **s)
{
	/* all strings are allocated from the same strdup'ed slab, so just free
	 * the first element */
	if (s != NULL)
		free(s[0]);

	/* and then the actual array of pointers */
	free(s);
}

#ifndef __APPLE__
static const char *nexttoken(const char *str,  int chr)
{
	if (str)
		str = strchr(str, chr);
	if (str)
		str++;
	return str;
}

static int ft_pin_core(const char *core_list)
{
	cpu_set_t mask;
	size_t max_bits;
	const char *curr_ptr, *next_ptr;
	int r = 0;

	max_bits = 8 * sizeof(mask);
	next_ptr = core_list;
	CPU_ZERO(&mask);

	// parse each sub-list, delimited by comma
	while (curr_ptr = next_ptr, next_ptr = nexttoken(next_ptr, ','), curr_ptr) {
		int start, end;
		const char *p1, *p2;
		char c;
		//get starting cpu number
		if ((r = sscanf(curr_ptr, "%u%c", &start, &c)) < 1)
			return EXIT_FAILURE;

		end = start;
		p1 = nexttoken(curr_ptr, '-');
		p2 = nexttoken(curr_ptr, ',');
		// get ending cpu number
		if (p1 != NULL && (p2 == NULL || p1 < p2)) {
			if ((r = sscanf(p1, "%u%c", &end, &c)) < 1)
				return EXIT_FAILURE;
		}

		if (start > end)
			return EXIT_FAILURE;
		while (start <= end) {
			if (start > max_bits)
				return EXIT_FAILURE;
			CPU_SET(start, &mask);
			start++;
		}
	}

	if (r == 2)
		return EXIT_FAILURE;

	return sched_setaffinity(0, sizeof(mask), &mask);
}
#else
static int ft_pin_core(const char *core_list)
{
    return EXIT_FAILURE;
}
#endif

static int ft_parse_pin_core_opt(char *optarg)
{
	if (optarg) {
		if (ft_pin_core(optarg))
			FT_WARN("Pin to core %s failed\n", optarg);
	}
	return 0;
}

void ft_longopts_usage()
{
	FT_PRINT_OPTS_USAGE("--pin-core <core_list>",
		"Specify which cores to pin process to using a\n"
		"a comma-separated list format, e.g.: 0,2-4.\n"
		"Disabled by default.");
	FT_PRINT_OPTS_USAGE("--timeout <seconds>",
		"Overrides default timeout for test specific transfers.");
	FT_PRINT_OPTS_USAGE("--debug-assert",
		"Replace asserts with while loops to force process to\n"
		"spin until a debugger can be attached.");
	FT_PRINT_OPTS_USAGE("--data-progress <progress_model>",
		"manual, or auto");
	FT_PRINT_OPTS_USAGE("--control-progress <progress_model>",
		"manual, auto, or unified");
	FT_PRINT_OPTS_USAGE("--max-msg-size <size>",
		"maximum untagged message size");
}

int debug_assert;

int lopt_idx = 0;
struct option long_opts[] = {
	{"pin-core", required_argument, NULL, LONG_OPT_PIN_CORE},
	{"timeout", required_argument, NULL, LONG_OPT_TIMEOUT},
	{"debug-assert", no_argument, &debug_assert, LONG_OPT_DEBUG_ASSERT},
	{"data-progress", required_argument, NULL, LONG_OPT_DATA_PROGRESS},
	{"control-progress", required_argument, NULL, LONG_OPT_CONTROL_PROGRESS},
	{"max-msg-size", required_argument, NULL, LONG_OPT_MAX_MSG_SIZE},
	{NULL, 0, NULL, 0},
};

int ft_parse_progress_model_string(char* progress_str)
{
	int ret = -1;

	if (!strcasecmp("manual", progress_str))
		ret = FI_PROGRESS_MANUAL;
	else if (!strcasecmp("auto", progress_str))
		ret = FI_PROGRESS_AUTO;
	else if (!strcasecmp("unified", progress_str))
		ret = FI_PROGRESS_CONTROL_UNIFIED;

	return ret;
}

int ft_parse_long_opts(int op, char *optarg)
{
	switch (op) {
	case LONG_OPT_PIN_CORE:
		return ft_parse_pin_core_opt(optarg);
	case LONG_OPT_TIMEOUT:
		timeout = atoi(optarg);
		return 0;
	case LONG_OPT_DEBUG_ASSERT:
		return 0;
	case LONG_OPT_DATA_PROGRESS:
		hints->domain_attr->data_progress = ft_parse_progress_model_string(optarg);
		if (hints->domain_attr->data_progress == -1)
			return EXIT_FAILURE;
		return 0;
	case LONG_OPT_CONTROL_PROGRESS:
		hints->domain_attr->control_progress = ft_parse_progress_model_string(optarg);
		if (hints->domain_attr->control_progress == -1)
			return EXIT_FAILURE;
		return 0;
	case LONG_OPT_MAX_MSG_SIZE:
		opts.max_msg_size = atoi(optarg);
		return 0;
	default:
		return EXIT_FAILURE;
	}
}
