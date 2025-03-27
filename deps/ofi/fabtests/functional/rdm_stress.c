/*
 * Copyright (c) 2022 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under the BSD license
 * below:
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
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_tagged.h>

#include "shared.h"
#include "jsmn.h"


/* Input test control */
enum {
	op_noop,
	op_hello,
	op_goodbye,
	op_msg_req,
	op_msg_inject_req,
	op_msg_resp,
	op_tag_req,
	op_tag_resp,
	op_read_req,
	op_read_resp,
	op_write_req,
	op_write_resp,
	op_sleep,
	op_exit,
	op_last,
};

struct rpc_ctrl {
	uint32_t op;
	uint64_t size;
	uint64_t count;
	union {
		uint64_t offset;
		uint64_t tag;
	};
	char *buf;
	struct fid_mr *mr;
};

int rpc_timeout = 2000; /* ms */

static const uint32_t invalid_id = ~0;

enum {
	rpc_write_key = 189,
	rpc_read_key = 724,
	rpc_threads = 32,
};

/* Wire protocol */
enum {
	cmd_hello,
	cmd_goodbye,
	cmd_msg,
	cmd_msg_inject,
	cmd_tag,
	cmd_read,
	cmd_write,
	cmd_last,
};

struct rpc_hdr {
	uint32_t client_id;
	uint32_t cmd;
	uint64_t size;
	uint64_t offset;
	uint64_t data;
};

struct rpc_hello_msg {
	struct rpc_hdr hdr;
	char addr[32];
};

enum {
	rpc_flag_ack = (1 << 0),
};

struct rpc_resp {
	struct fid_mr *mr;
	int status;
	int flags;
	struct rpc_hdr hdr;
};

struct rpc_client {
	pid_t pid;
};

struct rpc_ctrl *ctrls;
struct rpc_ctrl *pending_req;
int ctrl_cnt;
#define MAX_RPC_CLIENTS 128
struct rpc_client clients[MAX_RPC_CLIENTS];

static uint32_t myid;
static uint32_t id_at_server;
static fi_addr_t server_addr;

static char *rpc_cmd_str(uint32_t cmd)
{
	static char *cmd_str[cmd_last] = {
		"hello",
		"goodbye",
		"msg",
		"msg_inject",
		"tag",
		"read",
		"write",
	};

	if (cmd >= cmd_last)
		return "unknown";

	return cmd_str[cmd];
}

static char *rpc_op_str(uint32_t op)
{
	static char *op_str[op_last] = {
		"noop",
		"hello",
		"goodbye",
		"msg_req",
		"msg_inject_req",
		"msg_resp",
		"tag_req",
		"tag_resp",
		"read_req",
		"read_resp",
		"write_req",
		"write_resp",
		"sleep",
		"exit",
	};

	if (op >= op_last)
		return "unknown";

	return op_str[op];
}

static int rpc_inject(struct rpc_hdr *hdr, fi_addr_t addr)
{
	uint64_t start;
	int ret;

	start = ft_gettime_ms();
	do {
		(void) fi_cq_read(txcq, NULL, 0);
		ret = (int) fi_inject(ep, hdr, sizeof(*hdr), addr);
	} while ((ret == -FI_EAGAIN) && (ft_gettime_ms() - start < rpc_timeout));

	if (ret)
		FT_PRINTERR("fi_inject", ret);

	return ret;
}

static int rpc_send(struct rpc_hdr *hdr, size_t size, fi_addr_t addr)
{
	struct fi_cq_tagged_entry comp;
	uint64_t start;
	int ret;

	start = ft_gettime_ms();
	do {
		(void) fi_cq_read(txcq, NULL, 0);
		ret = (int) fi_send(ep, hdr, size, NULL, addr, hdr);
	} while ((ret == -FI_EAGAIN) && (ft_gettime_ms() - start < rpc_timeout));

	if (ret) {
		FT_PRINTERR("fi_send", ret);
		return ret;
	}

	ret = fi_cq_sread(txcq, &comp, 1, NULL, rpc_timeout);
	return ret == 1 ? 0 : ret;
}

static int rpc_deliver(struct rpc_hdr *hdr, size_t size, fi_addr_t addr)
{
	struct fi_msg msg = {0};
	struct iovec iov;
	struct fi_cq_tagged_entry comp;
	uint64_t start;
	int ret;

	iov.iov_base = hdr;
	iov.iov_len = size;

	msg.msg_iov = &iov;
	msg.iov_count = 1;
	msg.addr = addr;
	msg.context = hdr;

	start = ft_gettime_ms();
	do {
		(void) fi_cq_read(txcq, NULL, 0);
		ret = (int) fi_sendmsg(ep, &msg, FI_DELIVERY_COMPLETE);
	} while ((ret == -FI_EAGAIN) && (ft_gettime_ms() - start < rpc_timeout));

	if (ret) {
		FT_PRINTERR("fi_sendmsg (delivery_complete)", ret);
		return ret;
	}

	ret = fi_cq_sread(txcq, &comp, 1, NULL, rpc_timeout);
	return ret == 1 ? 0 : ret;
}

static int rpc_recv(struct rpc_hdr *hdr, size_t size, fi_addr_t addr)
{
	struct fi_cq_tagged_entry comp;
	int ret;

	ret = (int) fi_recv(ep, hdr, size, NULL, addr, hdr);
	if (ret) {
		FT_PRINTERR("fi_recv", ret);
		return ret;
	}

	ret = fi_cq_sread(rxcq, &comp, 1, NULL, rpc_timeout);
	return ret == 1 ? 0 : ret;
}

static int
rpc_trecv(struct rpc_hdr *hdr, size_t size, uint64_t tag, fi_addr_t addr)
{
	struct fi_cq_tagged_entry comp;
	int ret;

	ret = (int) fi_trecv(ep, hdr, size, NULL, addr, tag, 0, hdr);
	if (ret) {
		FT_PRINTERR("fi_trecv", ret);
		return ret;
	}

	ret = fi_cq_sread(rxcq, &comp, 1, NULL, rpc_timeout);
	return ret == 1 ? 0 : ret;
}


static int rpc_send_req(struct rpc_ctrl *req, struct rpc_hdr *hdr)
{
	int ret;

	ret = rpc_inject(hdr, server_addr);
	if (!ret)
		pending_req = req;
	return ret;
}

/* Only support 1 outstanding request at a time for now */
static struct rpc_ctrl *rcp_get_req(struct rpc_ctrl *resp)
{
	return pending_req;
}

static int rpc_noop(struct rpc_ctrl *ctrl)
{
	return 0;
}

/* Send server our address.  This call is synchronous since we need
 * the response before we can send any other requests.
 */
static int rpc_hello(struct rpc_ctrl *ctrl)
{
	struct rpc_hello_msg msg = {0};
	struct rpc_hdr resp;
	size_t addrlen;
	int ret;

	printf("(%d-?) saying hello\n", myid);
	msg.hdr.client_id = myid;
	msg.hdr.cmd = cmd_hello;

	addrlen = sizeof(msg.addr);
	ret = fi_getname(&ep->fid, &msg.addr, &addrlen);
	if (ret) {
		FT_PRINTERR("fi_getname", ret);
		return ret;
	}

	msg.hdr.size = addrlen;
	ret = rpc_send(&msg.hdr, sizeof(msg.hdr) + addrlen, server_addr);
	if (ret)
		return ret;

	ret = rpc_recv(&resp, sizeof(resp), FI_ADDR_UNSPEC);
	if (ret)
		return ret;

	ft_assert(resp.cmd == cmd_hello);
	id_at_server = resp.client_id;
	printf("(%d-%d) we're friends now\n", myid, id_at_server);
	return (int) resp.data;
}

/* Let server know we're leaving gracefully - no response expected. */
static int rpc_goodbye(struct rpc_ctrl *ctrl)
{
	struct rpc_hdr hdr = {0};

	hdr.client_id = id_at_server;
	hdr.cmd = cmd_goodbye;
	return rpc_deliver(&hdr, sizeof hdr, server_addr);
}

static int rpc_msg_req(struct rpc_ctrl *ctrl)
{
	struct rpc_hdr req = {0};

	req.client_id = id_at_server;
	req.cmd = cmd_msg;
	req.size = ctrl->size;
	return rpc_send_req(ctrl, &req);
}

static int rpc_msg_inject_req(struct rpc_ctrl *ctrl)
{
	struct rpc_hdr req = {0};

	req.client_id = id_at_server;
	req.cmd = cmd_msg_inject;
	req.size = ctrl->size;
	return rpc_send_req(ctrl, &req);
}

static int rpc_msg_resp(struct rpc_ctrl *ctrl)
{
	struct rpc_ctrl *req;
	struct rpc_hdr *resp;
	size_t size;
	int ret;

	req = rcp_get_req(ctrl);
	size = sizeof(*resp) + req->size;
	resp = calloc(1, size);
	if (!resp)
		return -FI_ENOMEM;

	ret = rpc_recv(resp, size, FI_ADDR_UNSPEC);
	if (ret)
		goto free;

	ft_assert(resp->cmd == cmd_msg || resp->cmd == cmd_msg_inject);
	ret = ft_check_buf(resp + 1, req->size);

free:
	free(resp);
	return ret;
}

static int rpc_tag_req(struct rpc_ctrl *ctrl)
{
	struct rpc_hdr req = {0};

	req.client_id = id_at_server;
	req.cmd = cmd_tag;
	req.size = ctrl->size;
	req.data = ctrl->tag;
	return rpc_send_req(ctrl, &req);
}

static int rpc_tag_resp(struct rpc_ctrl *ctrl)
{
	struct rpc_ctrl *req;
	struct rpc_hdr *resp;
	size_t size;
	int ret;

	req = rcp_get_req(ctrl);
	size = sizeof(*resp) + req->size;
	resp = calloc(1, size);
	if (!resp)
		return -FI_ENOMEM;

	ret = rpc_trecv(resp, size, req->tag, FI_ADDR_UNSPEC);
	if (ret)
		goto free;

	ft_assert(resp->cmd == cmd_tag);
	ret = ft_check_buf(resp + 1, req->size);

free:
	free(resp);
	return ret;
}

static int rpc_reg_buf(struct rpc_ctrl *ctrl, size_t size, uint64_t access)
{
	int ret;

	ret = fi_mr_reg(domain, ctrl->buf, size, access, 0,
			rpc_read_key, 0, &ctrl->mr, NULL);
	if (ret) {
		FT_PRINTERR("fi_mr_reg", ret);
		return ret;
	}

	if (fi->domain_attr->mr_mode & FI_MR_ENDPOINT) {
		ret = fi_mr_bind(ctrl->mr, &ep->fid, 0);
		if (ret) {
			FT_PRINTERR("fi_mr_bind", ret);
			goto close;
		}
		ret = fi_mr_enable(ctrl->mr);
		if (ret) {
			FT_PRINTERR("fi_mr_enable", ret);
			goto close;
		}
	}
	return FI_SUCCESS;

close:
	FT_CLOSE_FID(ctrl->mr);
	return ret;
}

static int rpc_read_req(struct rpc_ctrl *ctrl)
{
	struct rpc_hdr req = {0};
	size_t size;
	int ret;

	size = ctrl->offset + ctrl->size;
	ctrl->buf = calloc(1, size);
	if (!ctrl->buf)
		return -FI_ENOMEM;

	ret = ft_fill_buf(&ctrl->buf[ctrl->offset], ctrl->size);
	if (ret)
		goto free;

	ret = rpc_reg_buf(ctrl, size, FI_REMOTE_READ);
	if (ret)
		goto free;

	req.client_id = id_at_server;
	req.cmd = cmd_read;
	req.size = ctrl->size;

	req.offset = ctrl->offset;
	if (fi->domain_attr->mr_mode & FI_MR_VIRT_ADDR)
		req.offset += (uintptr_t) ctrl->buf;
	req.data = fi_mr_key(ctrl->mr);

	ret = rpc_send_req(ctrl, &req);
	if (ret)
		goto close;

	return 0;

close:
	FT_CLOSE_FID(ctrl->mr);
free:
	free(ctrl->buf);
	return ret;
}

static int rpc_read_resp(struct rpc_ctrl *ctrl)
{
	struct rpc_hdr resp = {0};
	struct rpc_ctrl *req;
	int ret;

	req = rcp_get_req(ctrl);
	ret = rpc_recv(&resp, sizeof(resp), FI_ADDR_UNSPEC);
	if (ret)
		goto close;

	ft_assert(resp.cmd == cmd_read);
	ret = ft_check_buf(&req->buf[req->offset], req->size);

close:
	FT_CLOSE_FID(req->mr);
	free(req->buf);
	return ret;
}

static int rpc_write_req(struct rpc_ctrl *ctrl)
{
	struct rpc_hdr req = {0};
	size_t size;
	int ret;

	size = ctrl->offset + ctrl->size;
	ctrl->buf = calloc(1, size);
	if (!ctrl->buf)
		return -FI_ENOMEM;

	ret = rpc_reg_buf(ctrl, size, FI_REMOTE_WRITE);
	if (ret)
		goto free;

	req.client_id = id_at_server;
	req.cmd = cmd_write;
	req.size = ctrl->size;

	req.offset = ctrl->offset;
	if (fi->domain_attr->mr_mode & FI_MR_VIRT_ADDR)
		req.offset += (uintptr_t) ctrl->buf;
	req.data = fi_mr_key(ctrl->mr);

	ret = rpc_send_req(ctrl, &req);
	if (ret)
		goto close;

	return 0;

close:
	FT_CLOSE_FID(ctrl->mr);
free:
	free(ctrl->buf);
	return ret;
}

static int rpc_write_resp(struct rpc_ctrl *ctrl)
{
	struct rpc_hdr resp = {0};
	struct rpc_ctrl *req;
	int ret;

	req = rcp_get_req(ctrl);
	ret = rpc_recv(&resp, sizeof(resp), FI_ADDR_UNSPEC);
	if (ret)
		goto close;

	ft_assert(resp.cmd == cmd_write);
	ret = ft_check_buf(&req->buf[req->offset], req->size);

close:
	FT_CLOSE_FID(req->mr);
	free(req->buf);
	return ret;
}

/* Used to delay client, which can force server into a flow control
 * state or into the middle of a transfer when the client exits.
 */
static int rpc_sleep(struct rpc_ctrl *ctrl)
{
	int ret;

	ret = usleep((useconds_t) ctrl->size * 1000);
	return ret ? -errno : 0;
}

static int rpc_exit(struct rpc_ctrl *ctrl)
{
	exit(0);
}


int (*ctrl_op[op_last])(struct rpc_ctrl *ctrl) = {
	rpc_noop,
	rpc_hello,
	rpc_goodbye,
	rpc_msg_req,
	rpc_msg_inject_req,
	rpc_msg_resp,
	rpc_tag_req,
	rpc_tag_resp,
	rpc_read_req,
	rpc_read_resp,
	rpc_write_req,
	rpc_write_resp,
	rpc_sleep,
	rpc_exit,
};

static int run_child(void)
{
	int i, j, ret;

	printf("(%d-?) running\n", myid);
	ret = ft_init_fabric();
	if (ret) {
		FT_PRINTERR("ft_init_fabric", ret);
		return ret;
	}

	ret = fi_av_insert(av, fi->dest_addr, 1, &server_addr, 0, NULL);
	if (ret != 1) {
		ret = -FI_EINTR;
		FT_PRINTERR("fi_av_insert", ret);
		goto free;
	}

	ret = rpc_hello(NULL);
	if (ret)
		goto free;

	for (i = 0; i < ctrl_cnt && !ret; i++) {
		for (j = 0; j < ctrls[i].count && !ret; j++) {
			printf("(%d-%d) rpc op %s iteration %d\n", myid, id_at_server,
			       rpc_op_str(ctrls[i].op), j);
			ret = ctrl_op[ctrls[i].op](&ctrls[i]);
		}
	}

free:
	ft_free_res();
	return ret;
}

static bool get_uint64_val(const char *js, jsmntok_t *t, uint64_t *val)
{
	if (t->type != JSMN_PRIMITIVE)
		return false;
	return (sscanf(&js[t->start], "%lu", val) == 1);
}

static bool get_op_enum(const char *js, jsmntok_t *t, uint32_t *op)
{
	const char *str;
	size_t len;

	if (t->type != JSMN_STRING)
		return false;

	str = &js[t->start];
	len = t->end - t->start;

	if (FT_TOKEN_CHECK(str, len, "msg_req")) {
		*op = op_msg_req;
		return true;
	} else if (FT_TOKEN_CHECK(str, len, "msg_inject_req")) {
		*op = op_msg_inject_req;
		return true;
	} else if (FT_TOKEN_CHECK(str, len, "msg_resp")) {
		*op = op_msg_resp;
		return true;
	} else if (FT_TOKEN_CHECK(str, len, "tag_req")) {
		*op = op_tag_req;
		return true;
	} else if (FT_TOKEN_CHECK(str, len, "tag_resp")) {
		*op = op_tag_resp;
		return true;
	} else if (FT_TOKEN_CHECK(str, len, "read_req")) {
		*op = op_read_req;
		return true;
	} else if (FT_TOKEN_CHECK(str, len, "read_resp")) {
		*op = op_read_resp;
		return true;
	} else if (FT_TOKEN_CHECK(str, len, "write_req")) {
		*op = op_write_req;
		return true;
	} else if (FT_TOKEN_CHECK(str, len, "write_resp")) {
		*op = op_write_resp;
		return true;
	} else if (FT_TOKEN_CHECK(str, len, "sleep")) {
		*op = op_sleep;
		return true;
	} else if (FT_TOKEN_CHECK(str, len, "noop")) {
		*op = op_noop;
		return true;
	} else if (FT_TOKEN_CHECK(str, len, "goodbye")) {
		*op = op_goodbye;
		return true;
	} else if (FT_TOKEN_CHECK(str, len, "hello")) {
		*op = op_hello;
		return true;
	} else if (FT_TOKEN_CHECK(str, len, "exit")) {
		*op = op_exit;
		return true;
	}

	return false;
}

static void init_rpc_ctrl(struct rpc_ctrl *ctrl)
{
	ctrl->op = op_last;
	ctrl->size = 0;
	ctrl->count = 1;
	ctrl->offset = 0;
	ctrl->buf = 0;
	ctrl->mr = 0;
}

/* add_ctrl extracts a rpc_ctrl struct from information in jts[idx], a
 *          JSMN_OBJECT, and its child tokens.
 * Returns true if a valid rpc_ctrl is extracted.
 *         false otherwise.
 */
static bool add_ctrl(const char *js, int njts, jsmntok_t *jts,
		     struct rpc_ctrl *ctrl, int *idx)
{
	int oidx = *idx;
	int osize = jts[*idx].size;
	jsmntok_t *t;
	const char *ks;
	size_t len;
	int i;

	ft_assert(jts[*idx].type == JSMN_OBJECT);

	init_rpc_ctrl(ctrl);
	/* i is indexing # of key:value pairs in JSMN_OBJECT */
	for (i = 0; i < osize && *idx < njts; i++) {
		(*idx)++; /* advance to next token, expecting key token */
		t = &jts[*idx];
		if (t->type != JSMN_STRING || t->size != 1)
			goto err_out;

		ks = &js[t->start];
		len = t->end - t->start;
		if (FT_TOKEN_CHECK(ks, len, "op")) {
			(*idx)++;
			t = &jts[*idx];
			if (!get_op_enum(js, t, &ctrl->op))
				goto err_out;
		} else if (FT_TOKEN_CHECK(ks, len, "size")) {
			(*idx)++;
			t = &jts[*idx];
			if (!get_uint64_val(js, t, &ctrl->size))
				goto err_out;
		} else if (FT_TOKEN_CHECK(ks, len, "offset")) {
			(*idx)++;
			t = &jts[*idx];
			if (!get_uint64_val(js, t, &ctrl->offset))
				goto err_out;
		} else if (FT_TOKEN_CHECK(ks, len, "ms")) {
			(*idx)++;
			t = &jts[*idx];
			if (!get_uint64_val(js, t, &ctrl->size))
				goto err_out;
		} else if (FT_TOKEN_CHECK(ks, len, "count")) {
			(*idx)++;
			t = &jts[*idx];
			if (!get_uint64_val(js, t, &ctrl->count))
				goto err_out;
		} else {
			goto err_out;
		}
	}

	/* op is rquired for rpc_ctrl to be valid */
	if (ctrl->op == op_last)
		goto err_out;
	return true;

err_out:
	printf("Invalid JSON entry: %.*s\n",
		jts[oidx].end - jts[oidx].start,
		&js[jts[oidx].start]);
	init_rpc_ctrl(ctrl);
	return false;
}

/* read and parse control file */
static int init_ctrls(const char *ctrlfile)
{
	FILE *ctrl_f;
	struct stat sb;
	char *js;	/* control file loaded in string */
	jsmn_parser jp;
	int njts;	/* # of JSON tokens in the control file */
	jsmntok_t *jts;
	int nobj;	/* # of JSON objects = possible rpc_ctrl entries */
	int start, i;
	int ret = 0;

	ctrl_f = fopen(ctrlfile, "r");
	if (!ctrl_f) {
		FT_PRINTERR("fopen", -errno);
		return -errno;
	}

	if (stat(ctrlfile, &sb)) {
		FT_PRINTERR("stat", -errno);
		ret = -errno;
		goto no_mem_out;
	}

	js = malloc(sb.st_size + 1);
	if (!js) {
		ret = -FI_ENOMEM;
		goto no_mem_out;
	}

	if (fread(js, sb.st_size, 1, ctrl_f) != 1) {
		ret = -FI_EINVAL;
		goto read_err_out;
	}
	js[sb.st_size] = 0;

	/* get # of tokens, allcoate memory and parse JSON */
	jsmn_init(&jp);
	njts = jsmn_parse(&jp, js, sb.st_size, NULL, 0);
	if (njts < 0) {
		ret = -FI_EINVAL;
		goto read_err_out;
	}

	jts = malloc(sizeof(jsmntok_t) * njts);
	if (!jts) {
		ret = -FI_ENOMEM;
		goto read_err_out;
	}

	jsmn_init(&jp);
	if (jsmn_parse(&jp, js, sb.st_size, jts, njts) != njts) {
		ret = -FI_EINVAL;
		goto parse_err_out;
	}

	/* find the first JSON array bypassing comments at the top */
	for (start = 0; start < njts && jts[start].type != JSMN_ARRAY; start++)
		;
	if (start == njts) {
		ret = -FI_EINVAL;
		goto parse_err_out;
	}

	/* count # of JSMN_OBJECT which is # of potential rpc_ctrl entries */
	for (i = start, nobj = 0; i < njts; i++)
		if  (jts[i].type == JSMN_OBJECT)
			nobj++;

	if (nobj <= 0) {
		ret = -FI_EINVAL;
		goto parse_err_out;
	}

	ctrls = malloc(sizeof(struct rpc_ctrl) * nobj);
	if (!ctrls) {
		ret = -FI_ENOMEM;
		goto parse_err_out;
	}

	/* extract rpc_ctrl structs from tokens */
	for (ctrl_cnt = 0; start < njts; start++) {
		if (jts[start].type != JSMN_OBJECT)
			continue;

		if (add_ctrl(js, njts, jts, &ctrls[ctrl_cnt], &start))
			ctrl_cnt++;
	}

	free(jts);
	free(js);
	fclose(ctrl_f);

	if (ctrl_cnt <= 0) {
		free(ctrls);
		ctrls = NULL;
		return -FI_EINVAL;
	}
	return 0;

parse_err_out:
	free(jts);
read_err_out:
	free(js);
no_mem_out:
	fclose(ctrl_f);
	return ret;
}

static void free_ctrls(void)
{
	free(ctrls);
	ctrls = NULL;
}

static int run_parent(const char *ctrlfile)
{
	pid_t pid;
	int i, ret;

	if (!ctrlfile)
		return -FI_ENOENT;

	printf("Starting rpc client(s)\n");
	ret = init_ctrls(ctrlfile);
	if (ret)
		return ret;

	for (i = 0; i < opts.iterations; i++) {
		/* If there's only 1 client, run it from this process.  This
		 * greatly helps with debugging.
		 */
		if (opts.num_connections == 1) {
			ret = run_child();
			if (ret)
				goto free;
			continue;
		}

		for (myid = 0; myid < (uint32_t) opts.num_connections; myid++) {
			if (clients[myid].pid) {
				pid = waitpid(clients[myid].pid, NULL, 0);
				if (pid < 0)
					FT_PRINTERR("waitpid", -errno);
				clients[myid].pid = 0;
			}

			ret = fork();
			if (!ret)
				return run_child();
			if (ret < 0) {
				ret = -errno;
				goto free;
			}

			clients[myid].pid = ret;
			ret = 0;
		}
	}

	for (myid = 0; myid < (uint32_t) opts.num_connections; myid++) {
		if (clients[myid].pid) {
			pid = waitpid(clients[myid].pid, NULL, 0);
			if (pid < 0)
				FT_PRINTERR("waitpid", -errno);
			clients[myid].pid = 0;
		}
	}

free:
	free_ctrls();
	return ret;
}

static void complete_rpc(struct rpc_resp *resp)
{
	fi_addr_t addr;
	int ret;

	printf("(%d) complete rpc %s (%s)\n", resp->hdr.client_id,
	       rpc_cmd_str(resp->hdr.cmd), fi_strerror(resp->status));

	if (!resp->status && (resp->flags & rpc_flag_ack))
		ret = rpc_inject(&resp->hdr, resp->hdr.client_id);
	else
		ret = resp->status;

	if (ret) {
		if (resp->hdr.client_id != invalid_id) {
			addr = resp->hdr.client_id;
			printf("(%d) unreachable, removing\n", resp->hdr.client_id);
			ret = fi_av_remove(av, &addr, 1, 0);
			if (ret)
				FT_PRINTERR("fi_av_remove", ret);
		}
	}

	if (resp->mr)
		FT_CLOSE_FID(resp->mr);

	(void) ft_check_buf(resp + 1, resp->hdr.size);
	free(resp);
}

/* If we fail to send the response (e.g. EAGAIN), we need to remove the
 * address from the AV to avoid double insertions.  We could loop on
 * EAGAIN in this call, but by replaying the entire handle_hello sequence
 * we end up stressing the AV insert/remove path more.
 */
static int handle_hello(struct rpc_hdr *req, struct rpc_resp *resp)
{
	struct rpc_hello_msg *msg;
	fi_addr_t addr;
	int ret;

	if (!req->size || req->size > sizeof(msg->addr))
		return -FI_EINVAL;

	msg = (struct rpc_hello_msg *) req;
	ret = fi_av_insert(av, &msg->addr, 1, &addr, 0, NULL);
	if (ret <= 0)
		return -FI_EADDRNOTAVAIL;

	resp->hdr.client_id = (uint32_t) addr;
	resp->hdr.size = 0;
	ret = fi_send(ep, &resp->hdr, sizeof(resp->hdr), NULL, addr, resp);
	if (ret) {
		(void) fi_av_remove(av, &addr, 1, 0);
		resp->hdr.client_id = invalid_id;
	}
	return ret;
}

/* How do we know that the client didn't restart with the same address
 * and send a hello message immediately before we could handle this
 * goodbye message?  This is a race that the test has to handle, rather
 * than libfabric, but also very unlikely to happen unless the client
 * intentionally re-uses addresses.
 */
static int handle_goodbye(struct rpc_hdr *req, struct rpc_resp *resp)
{
	fi_addr_t addr;
	int ret;

	addr = req->client_id;
	ret = fi_av_remove(av, &addr, 1, 0);
	if (ret)
		FT_PRINTERR("fi_av_remove", ret);

	/* No response generated */
	printf("(%d) complete rpc %s (%s)\n", resp->hdr.client_id,
	       rpc_cmd_str(resp->hdr.cmd), fi_strerror(resp->status));
	free(resp);
	return 0;
}

static int handle_msg(struct rpc_hdr *req, struct rpc_resp *resp)
{
	return fi_send(ep, &resp->hdr, sizeof(resp->hdr) + resp->hdr.size,
		       NULL, req->client_id, resp);
}

static int handle_msg_inject(struct rpc_hdr *req, struct rpc_resp *resp)
{
	int ret;

	ret = fi_inject(ep, &resp->hdr, sizeof(resp->hdr) + resp->hdr.size,
		        req->client_id);
	if (!ret)
		complete_rpc(resp);
	return ret;
}

static int handle_tag(struct rpc_hdr *req, struct rpc_resp *resp)
{
	return fi_tsend(ep, &resp->hdr, sizeof(resp->hdr) + resp->hdr.size,
			NULL, req->client_id, req->data, resp);
}

static int handle_read(struct rpc_hdr *req, struct rpc_resp *resp)
{
	resp->flags = rpc_flag_ack;
	return fi_read(ep, resp + 1, resp->hdr.size, NULL, req->client_id,
		       req->offset, req->data, resp);
}

static int handle_write(struct rpc_hdr *req, struct rpc_resp *resp)
{
	resp->flags = rpc_flag_ack;
	return fi_write(ep, resp + 1, resp->hdr.size, NULL, req->client_id,
			req->offset, req->data, resp);
}

int (*handle_rpc[cmd_last])(struct rpc_hdr *req, struct rpc_resp *resp) = {
	handle_hello,
	handle_goodbye,
	handle_msg,
	handle_msg_inject,
	handle_tag,
	handle_read,
	handle_write,
};

static void start_rpc(struct rpc_hdr *req)
{
	struct rpc_resp *resp;
	uint64_t start;
	int ret;

	printf("(%d) start rpc %s\n", req->client_id, rpc_cmd_str(req->cmd));
	if (req->cmd >= cmd_last)
		goto free;

	if (!req->size)
		goto free;

	resp = calloc(1, sizeof(*resp) + req->size);
	if (!resp)
		goto free;

	resp->hdr = *req;
	ret = ft_fill_buf(resp + 1, resp->hdr.size);
	if (ret) {
		free(resp);
		goto free;
	}

	start = ft_gettime_ms();
	do {
		(void) fi_cq_read(txcq, NULL, 0);
		ret = handle_rpc[req->cmd](req, resp);
	} while ((ret == -FI_EAGAIN) && (ft_gettime_ms() - start < rpc_timeout));

	if (ret) {
		resp->status = ret;
		(void) complete_rpc(resp);
	}

free:
	free(req);
}

/* Completion errors are expected as clients are misbehaving */
static int handle_cq_error(void)
{
	struct fi_cq_err_entry cq_err = {0};
	struct rpc_resp *resp;
	int ret;

	ret = fi_cq_readerr(txcq, &cq_err, 0);
	if (ret < 0) {
		if (ret == -FI_EAGAIN)
			return 0;

		FT_PRINTERR("fi_cq_readerr", ret);
		return ret;
	}

	resp = cq_err.op_context;
	resp->status = -cq_err.err;
	FT_CQ_ERR(txcq, cq_err, NULL, 0);
	complete_rpc(resp);
	return 0;
}

static int wait_on_fd(struct fid_cq *cq, struct fi_cq_tagged_entry *comp)
{
	struct fid *fids[1];
	int fd, ret;

	fd = (cq == txcq) ? tx_fd : rx_fd;
	fids[0] = &cq->fid;

	do {
		ret = fi_trywait(fabric, fids, 1);
		if (ret == FI_SUCCESS) {
			ret = ft_poll_fd(fd, -1);
			if (ret && ret != -FI_EAGAIN)
				break;
		}

		ret = fi_cq_read(cq, comp, 1);
	} while (ret == -FI_EAGAIN);

	return ret;
}

static int wait_for_comp(struct fid_cq *cq, struct fi_cq_tagged_entry *comp)
{
	int ret;

	if (opts.comp_method == FT_COMP_SREAD)
		ret = fi_cq_sread(cq, comp, 1, NULL, -1);
	else
		ret = wait_on_fd(cq, comp);

	return ret;
}

static void *process_rpcs(void *context)
{
	struct fi_cq_tagged_entry comp = {0};
	struct rpc_hello_msg *req;
	int ret;

	do {
		req = calloc(1, sizeof(*req));
		if (!req) {
			ret = -FI_ENOMEM;
			break;
		}

		ret = (int) fi_recv(ep, req, sizeof(*req), NULL,
				    FI_ADDR_UNSPEC, req);
		if (ret) {
			FT_PRINTERR("fi_read", ret);
			break;
		}

		do {
			/* The rx and tx cq's are the same */
			ret = wait_for_comp(rxcq, &comp);
			if (ret < 0) {
				comp.flags = FI_SEND;
				ret = handle_cq_error();
			} else if (ret > 0) {
				ret = 0;
				if (comp.flags & FI_RECV) {
					req = comp.op_context;
					start_rpc(&req->hdr);
				} else {
					complete_rpc(comp.op_context);
				}
			}
		} while (!ret && !(comp.flags & FI_RECV));
	} while (!ret);

	return NULL;
}

static int run_server(void)
{
	pthread_t thread[rpc_threads];
	int i, ret;

	printf("Starting rpc stress server\n");
	opts.options |= FT_OPT_CQ_SHARED;
	ret = ft_init_fabric();
	if (ret)
		return ret;

	for (i = 0; i < rpc_threads; i++) {
		ret = pthread_create(&thread[i], NULL, process_rpcs,
				     (void *) (uintptr_t) i);
		if (ret) {
			ret = -ret;
			FT_PRINTERR("pthread_create", ret);
			break;
		}
	}

	while (i-- > 0)
		pthread_join(thread[i], NULL);

	ft_free_res();
	return ret;
}

int main(int argc, char **argv)
{
	char *ctrlfile = NULL;
	int op, ret;

	opts = INIT_OPTS;
	opts.options |= FT_OPT_SKIP_MSG_ALLOC | FT_OPT_SKIP_ADDR_EXCH;
	opts.mr_mode = FI_MR_PROV_KEY | FI_MR_ALLOCATED | FI_MR_ENDPOINT |
		       FI_MR_VIRT_ADDR | FI_MR_LOCAL | FI_MR_HMEM;
	opts.iterations = 1;
	opts.num_connections = 16;
	opts.comp_method = FT_COMP_WAIT_FD;
	opts.av_size = MAX_RPC_CLIENTS;

	hints = fi_allocinfo();
	if (!hints)
		return EXIT_FAILURE;

	while ((op = getopt_long(argc, argv, "u:h" CS_OPTS INFO_OPTS,
				  long_opts, &lopt_idx)) != -1) {
		switch (op) {
		default:
			if (!ft_parse_long_opts(op, optarg))
				continue;
			ft_parsecsopts(op, optarg, &opts);
			ft_parseinfo(op, optarg, hints, &opts);
			break;
		case 'u':
			ctrlfile = optarg;
			break;
		case '?':
		case 'h':
			ft_csusage(argv[0], "An RDM endpoint error stress test.");
			ft_longopts_usage();
			FT_PRINT_OPTS_USAGE("-u <test_config.json>",
				"specify test control file at client");
			fprintf(stderr, "\nExample execution:\n");
			fprintf(stderr, "  server: %s -p tcp -s 127.0.0.1\n", argv[0]);
			fprintf(stderr, "  client: %s -p tcp -u "
				"fabtests/test_configs/rdm_stress/stress.json "
				"127.0.0.1\n", argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (timeout >= 0)
		rpc_timeout = timeout * 1000;
	if (optind < argc)
		opts.dst_addr = argv[optind];

	/* limit num_connections to MAX_RPC_CLIENTS */
	opts.num_connections = MIN(opts.num_connections, MAX_RPC_CLIENTS);

	hints->caps = FI_MSG | FI_TAGGED | FI_RMA;
	hints->domain_attr->mr_mode = opts.mr_mode;
	hints->domain_attr->av_type = FI_AV_TABLE;
	hints->ep_attr->type = FI_EP_RDM;
	hints->tx_attr->inject_size = sizeof(struct rpc_hello_msg);

	if (opts.dst_addr)
		ret = run_parent(ctrlfile);
	else
		ret = run_server();

	return ft_exit_code(ret);
}
