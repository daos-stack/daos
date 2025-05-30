/*
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2025 Google LLC
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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
#include <mercury_config.h>

/** the shared HG RPC ID used for all CRT opc */
#define CRT_HG_RPCID		(0xDA036868)
#define CRT_HG_ONEWAY_RPCID	(0xDA036869)

/** MAX number of HG handles in pool */
#define CRT_HG_POOL_MAX_NUM	(512)
/** number of prepost HG handles when enable pool */
#define CRT_HG_POOL_PREPOST_NUM	(16)

/** interval at which to republish HG diagnostics as metrics */
#define CRT_HG_TM_PUB_INTERVAL_US 1 * (1000 * 1000)

/** default values for init / incr to prepost handles */
#define CRT_HG_POST_INIT        (512)
#define CRT_HG_POST_INCR        (512)
#define CRT_HG_MRECV_BUF        (16)

#define CRT_UCX_STR             "ucx"

struct crt_rpc_priv;
struct crt_common_hdr;
struct crt_corpc_hdr;

/**
 * Enumeration specifying providers supported by the library
 */
typedef enum {
	CRT_PROV_SM = 0,
	CRT_PROV_OFI_SOCKETS,
	CRT_PROV_OFI_VERBS_RXM,
	CRT_PROV_OFI_GNI,
	CRT_PROV_OFI_TCP,
	CRT_PROV_OFI_TCP_RXM,
	CRT_PROV_OFI_CXI,
	CRT_PROV_OFI_OPX,
	CRT_PROV_OFI_LAST = CRT_PROV_OFI_OPX,
	CRT_PROV_UCX,
	CRT_PROV_UCX_LAST = CRT_PROV_UCX,
	/* Note: This entry should be the last valid one in enum */
	CRT_PROV_COUNT,
	CRT_PROV_UNKNOWN = -1,
} crt_provider_t;

crt_provider_t
crt_prov_str_to_prov(const char *prov_str);

int
crt_hg_parse_uri(const char *uri, crt_provider_t *prov, char *addr);

static inline bool
crt_provider_is_ucx(crt_provider_t prov)
{
	return (prov >= CRT_PROV_UCX) && (prov <= CRT_PROV_UCX_LAST);
}

static inline bool
crt_provider_is_ofi(crt_provider_t prov)
{
	return (prov >= CRT_PROV_OFI_SOCKETS) &&
	       (prov <= CRT_PROV_OFI_LAST);
}

struct crt_na_dict {
	/** String identifying the provider */
	char	*nad_str;
	/** Alternative string */
	char	*nad_alt_str;
	int	nad_type;
	/** a flag of explicitly bind with IP:port to create NA class */
	bool	nad_port_bind;
	/** a flag to indicate if endpoints are contiguous */
	bool	nad_contig_eps;
	/** a flag to indicate if nad_str is allocated on the heap */
	bool     nad_str_alloc;
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

/** HG metrics (republished from class diagnostics counters) */
struct crt_hg_metrics {
	struct d_tm_node_t *chm_bulks;
	struct d_tm_node_t *chm_mr_copies;
	struct d_tm_node_t *chm_active_rpcs;
	struct d_tm_node_t *chm_extra_bulk_resp;
	struct d_tm_node_t *chm_extra_bulk_req;
	struct d_tm_node_t *chm_resp_recv;
	struct d_tm_node_t *chm_resp_sent;
	struct d_tm_node_t *chm_req_recv;
	struct d_tm_node_t *chm_req_sent;
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
	int			 chc_provider; /* provider */
	uint64_t                 chc_diag_pub_ts; /* time of last diagnostics pub */
	struct crt_hg_metrics    chc_metrics;     /* HG metrics */
};

/* crt_hg.c */
int crt_hg_get_protocol_info(const char *info_string, struct na_protocol_info **na_protocol_info_p);
void crt_hg_free_protocol_info(struct na_protocol_info *na_protocol_info);
int crt_hg_init(void);
int crt_hg_fini(void);
int crt_hg_ctx_init(struct crt_hg_context *hg_ctx, crt_provider_t provider, int ctx_idx, bool primary, int iface_idx);
int crt_hg_ctx_fini(struct crt_hg_context *hg_ctx);
int crt_hg_req_create(struct crt_hg_context *hg_ctx,
		      struct crt_rpc_priv *rpc_priv);
void crt_hg_req_destroy(struct crt_rpc_priv *rpc_priv);
void crt_hg_req_send(struct crt_rpc_priv *rpc_priv);
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

bool crt_provider_is_contig_ep(crt_provider_t provider);
bool crt_provider_is_port_based(crt_provider_t provider);
char *crt_provider_name_get(crt_provider_t provider);
uint32_t crt_provider_num_ifaces_get(bool primary, crt_provider_t provider);
bool crt_provider_is_sep(bool primary, crt_provider_t provider);
void crt_provider_set_sep(bool primary, crt_provider_t provider, bool enable);
int crt_provider_get_cur_ctx_num(bool primary, crt_provider_t provider);
int crt_provider_get_ctx_idx(bool primary, crt_provider_t provider);
void crt_provider_put_ctx_idx(bool primary, crt_provider_t provider, int idx);
int crt_provider_get_max_ctx_num(bool primary, crt_provider_t provider);
d_list_t *crt_provider_get_ctx_list(bool primary, crt_provider_t provider);
void crt_provider_get_ctx_list_and_num(bool primary, crt_provider_t provider, d_list_t **list, int *num);
char* crt_provider_iface_str_get(bool primary, crt_provider_t provider, int iface_idx);
struct crt_na_config*
crt_provider_get_na_config(bool primary, crt_provider_t provider);
int
crt_hgret_2_der(int hg_ret);

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
	case -DER_BUSY:
		return HG_BUSY;
	default:
		return HG_OTHER_ERROR;
	};
}

static inline int
crt_hg_bulk_get_sgnum(hg_bulk_t hg_bulk_hdl)
{
	D_ASSERT(hg_bulk_hdl != HG_BULK_NULL);

	return HG_Bulk_get_segment_count(hg_bulk_hdl);
}

static inline int
crt_hg_bulk_get_len(hg_bulk_t hg_bulk_hdl)
{
	D_ASSERT(hg_bulk_hdl != HG_BULK_NULL);

	return HG_Bulk_get_size(hg_bulk_hdl);
}

int
crt_hg_bulk_create(struct crt_hg_context *hg_ctx, d_sg_list_t *sgl, crt_bulk_perm_t bulk_perm,
		   hg_bulk_t *bulk_hdl);
int
crt_hg_bulk_bind(hg_bulk_t bulk_hdl, struct crt_hg_context *hg_ctx);
int
crt_hg_bulk_access(hg_bulk_t bulk_hdl, d_sg_list_t *sgl);
int
crt_hg_bulk_transfer(struct crt_bulk_desc *bulk_desc, crt_bulk_cb_t complete_cb, void *arg,
		     crt_bulk_opid_t *opid, bool bind);
void
crt_hg_republish_diags(struct crt_hg_context *hg_ctx);

#endif /* __CRT_MERCURY_H__ */
