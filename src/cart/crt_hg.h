/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It is the header file of bridging to mercury.
 */
#ifndef __CRT_MERCURY_H__
#define __CRT_MERCURY_H__

#include <gurt/list.h>

#include <mercury.h>
#include <mercury_types.h>
#include <mercury_macros.h>
#include <mercury_proc.h>
#include <mercury_proc_string.h>
#include <mercury_log.h>

/** the shared HG RPC ID used for all CRT opc */
#define CRT_HG_RPCID		(0xDA036868)
#define CRT_HG_ONEWAY_RPCID	(0xDA036869)

/** MAX number of HG handles in pool */
#define CRT_HG_POOL_MAX_NUM	(512)
/** number of prepost HG handles when enable pool */
#define CRT_HG_POOL_PREPOST_NUM	(16)

struct crt_rpc_priv;
struct crt_common_hdr;
struct crt_corpc_hdr;

/** type of NA plugin */
enum crt_na_type {
	CRT_NA_SM		= 0,
	CRT_NA_OFI_SOCKETS	= 1,
	CRT_NA_OFI_VERBS_RXM	= 2,
	CRT_NA_OFI_VERBS	= 3,
	CRT_NA_OFI_GNI		= 4,
	CRT_NA_OFI_PSM2		= 5,
	CRT_NA_OFI_TCP_RXM	= 6,

	/* Note: This entry should be the last one in enum */
	CRT_NA_OFI_COUNT,
};

static inline bool
crt_na_type_is_ofi(int na_type)
{
	return (na_type >= CRT_NA_OFI_SOCKETS) &&
	       (na_type < CRT_NA_OFI_COUNT);
}

struct crt_na_dict {
	char	*nad_str;
	int	nad_type;
	/* a flag of explicitly bind with IP:port to create NA class */
	bool	nad_port_bind;
};

extern struct crt_na_dict crt_na_dict[];

struct crt_hg_hdl {
	/* link to crt_hg_pool::chp_hg_list */
	d_list_t		chh_link;
	/* HG handle */
	hg_handle_t		chh_hdl;
};

struct crt_hg_pool {
	pthread_spinlock_t	chp_lock;
	/* number of HG handles in pool */
	int32_t			chp_num;
	/* maximum number of HG handles in pool */
	int32_t			chp_max_num;
	/* HG handle list */
	d_list_t		chp_list;
	bool			chp_enabled;
};

/** HG context */
struct crt_hg_context {
	/* Flag indicating whether hg class is shared; true for SEP mode */
	bool			 chc_shared_hg_class;
	hg_class_t		*chc_hgcla; /* HG class */
	hg_context_t		*chc_hgctx; /* HG context */
	hg_class_t		*chc_bulkcla; /* bulk class */
	hg_context_t		*chc_bulkctx; /* bulk context */
	struct crt_hg_pool	 chc_hg_pool; /* HG handle pool */
};

/* crt_hg.c */
int crt_hg_init(void);
int crt_hg_fini(void);
int crt_hg_ctx_init(struct crt_hg_context *hg_ctx, int provider, int idx);
int crt_hg_ctx_fini(struct crt_hg_context *hg_ctx);
int crt_hg_req_create(struct crt_hg_context *hg_ctx,
		      struct crt_rpc_priv *rpc_priv);
void crt_hg_req_destroy(struct crt_rpc_priv *rpc_priv);
int crt_hg_req_send(struct crt_rpc_priv *rpc_priv);
int crt_hg_reply_send(struct crt_rpc_priv *rpc_priv);
void crt_hg_reply_error_send(struct crt_rpc_priv *rpc_priv, int error_code);
int crt_hg_req_cancel(struct crt_rpc_priv *rpc_priv);
int crt_hg_progress(struct crt_hg_context *hg_ctx, int64_t timeout);
int crt_hg_addr_free(struct crt_hg_context *hg_ctx, hg_addr_t addr);
int crt_hg_get_addr(hg_class_t *hg_class, char *addr_str, size_t *str_size);

int crt_rpc_handler_common(hg_handle_t hg_hdl);

/* crt_hg_proc.c */
int crt_hg_unpack_header(hg_handle_t hg_hdl, struct crt_rpc_priv *rpc_priv,
			 crt_proc_t *proc);
void crt_hg_header_copy(struct crt_rpc_priv *in, struct crt_rpc_priv *out);
void crt_hg_unpack_cleanup(crt_proc_t proc);
int crt_hg_unpack_body(struct crt_rpc_priv *rpc_priv, crt_proc_t proc);
int crt_proc_in_common(crt_proc_t proc, crt_rpc_input_t *data);
int crt_proc_out_common(crt_proc_t proc, crt_rpc_output_t *data);

bool crt_provider_is_contig_ep(int provider);

static inline int
crt_hgret_2_der(int hg_ret)
{
	switch (hg_ret) {
	case HG_SUCCESS:
		return 0;
	case HG_TIMEOUT:
		return -DER_TIMEDOUT;
	case HG_INVALID_ARG:
		return -DER_INVAL;
	case HG_MSGSIZE:
		return -DER_OVERFLOW;
	case HG_NOMEM:
		return -DER_NOMEM;
	case HG_CANCELED:
		return -DER_CANCELED;
	default:
		return -DER_HG;
	};
}

static inline int
crt_der_2_hgret(int der)
{
	switch (der) {
	case 0:
		return HG_SUCCESS;
	case -DER_TIMEDOUT:
		return HG_TIMEOUT;
	case -DER_INVAL:
		return HG_INVALID_ARG;
	case -DER_OVERFLOW:
		return HG_MSGSIZE;
	case -DER_NOMEM:
		return HG_NOMEM;
	case -DER_CANCELED:
		return HG_CANCELED;
	default:
		return HG_OTHER_ERROR;
	};
}

/* some simple helper functions */
typedef hg_rpc_cb_t crt_hg_rpc_cb_t;
static inline int
crt_hg_reg(hg_class_t *hg_class, hg_id_t rpcid, crt_proc_cb_t in_proc_cb,
	   crt_proc_cb_t out_proc_cb, crt_hg_rpc_cb_t rpc_cb)
{
	hg_return_t hg_ret;
	int         rc = 0;

	D_ASSERT(hg_class != NULL);

	hg_ret = HG_Register(hg_class, rpcid, (hg_proc_cb_t)in_proc_cb,
			     (hg_proc_cb_t)out_proc_cb, rpc_cb);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Register(rpcid: %#lx) failed, hg_ret: %d.\n",
			rpcid, hg_ret);
		rc = crt_hgret_2_der(hg_ret);
	}
	return rc;
}

static inline int
crt_hg_bulk_free(crt_bulk_t bulk_hdl)
{
	hg_return_t	hg_ret;

	hg_ret = HG_Bulk_free(bulk_hdl);
	if (hg_ret != HG_SUCCESS)
		D_ERROR("HG_Bulk_free failed, hg_ret: %d.\n", hg_ret);

	return crt_hgret_2_der(hg_ret);
}

static inline int
crt_hg_bulk_addref(crt_bulk_t bulk_hdl)
{
	hg_return_t	hg_ret;

	hg_ret = HG_Bulk_ref_incr(bulk_hdl);
	if (hg_ret != HG_SUCCESS)
		D_ERROR("HG_Bulk_ref_incr failed, hg_ret: %d.\n", hg_ret);

	return crt_hgret_2_der(hg_ret);
}

static inline int
crt_hg_bulk_get_len(crt_bulk_t bulk_hdl, size_t *bulk_len)
{
	hg_size_t	hg_size;

	if (bulk_len == NULL) {
		D_ERROR("bulk_len is NULL\n");
		return -DER_INVAL;
	}

	if (bulk_hdl == CRT_BULK_NULL) {
		D_ERROR("bulk_hdl is NULL\n");
		return -DER_INVAL;
	}

	hg_size = HG_Bulk_get_size(bulk_hdl);
	*bulk_len = hg_size;

	return 0;
}

static inline int
crt_hg_bulk_get_sgnum(crt_bulk_t bulk_hdl, unsigned int *bulk_sgnum)
{
	hg_uint32_t	hg_sgnum;

	D_ASSERT(bulk_sgnum != NULL);
	hg_sgnum = HG_Bulk_get_segment_count(bulk_hdl);
	*bulk_sgnum = hg_sgnum;

	return 0;
}

int crt_hg_bulk_create(struct crt_hg_context *hg_ctx, d_sg_list_t *sgl,
		       crt_bulk_perm_t bulk_perm, crt_bulk_t *bulk_hdl);
int crt_hg_bulk_bind(crt_bulk_t bulk_hdl, struct crt_hg_context *hg_ctx);
int crt_hg_bulk_access(crt_bulk_t bulk_hdl, d_sg_list_t *sgl);
int crt_hg_bulk_transfer(struct crt_bulk_desc *bulk_desc,
			 crt_bulk_cb_t complete_cb, void *arg,
			 crt_bulk_opid_t *opid, bool bind);
static inline int
crt_hg_bulk_cancel(crt_bulk_opid_t opid)
{
	return HG_Bulk_cancel(opid);
}

#endif /* __CRT_MERCURY_H__ */
