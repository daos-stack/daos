/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 * Copyright (c) 2021 Carnegie Mellon University.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * Wrap PSM2 API under the original PSM API.
 * This file allows na_psm.c to be compiled to use the PSM2 API,
 * thus allowing the same na_psm.c source file to support both
 * PSM and PSM2.
 */

/* define PSM-style aliases for PSM2 data types */
typedef psm2_error_t psm_error_t;
typedef psm2_ep_t psm_ep_t;
typedef psm2_epaddr_t psm_epaddr_t;
typedef psm2_epid_t psm_epid_t;
typedef psm2_uuid_t psm_uuid_t;
typedef psm2_mq_t psm_mq_t;
typedef psm2_mq_req_t psm_mq_req_t;
typedef psm2_mq_status_t psm_mq_status_t;

/* locally defined struct aliases */
typedef struct psm2_optkey psm_optkey_t;
typedef struct psm2_ep_open_opts psm_ep_open_opts_t;

/* defines/errors used by na_psm.c */
#define PSM_VERNO_MAJOR       PSM2_VERNO_MAJOR
#define PSM_VERNO_MINOR       PSM2_VERNO_MINOR
#define PSM_EP_CLOSE_GRACEFUL PSM2_EP_CLOSE_GRACEFUL
#define PSM_EP_CLOSE_FORCE    PSM2_EP_CLOSE_FORCE
#define PSM_EP_CLOSE_TIMEOUT  PSM2_EP_CLOSE_TIMEOUT
#define PSM_MQ_ORDERMASK_ALL  PSM2_MQ_ORDERMASK_ALL

#define PSM_OK PSM2_OK

/* inline wrapper functions */

static NA_INLINE psm_error_t
psm_init(int *major, int *minor)
{
    return psm2_init(major, minor);
}

static NA_INLINE psm_error_t
psm_finalize(void)
{
    return psm2_finalize();
}

static NA_INLINE void
psm_epaddr_setctxt(psm_epaddr_t epaddr, void *ctxt)
{
    psm2_epaddr_setctxt(epaddr, ctxt);
}

#if 0 /* currently not used */
static NA_INLINE void *
psm_epaddr_getctxt(psm_epaddr_t epaddr) {
  return psm2_epaddr_getctxt(epaddr);
}
#endif

static NA_INLINE const char *
psm_error_get_string(psm_error_t error)
{
    return psm2_error_get_string(error);
}

static NA_INLINE psm_error_t
psm_mq_cancel(psm_mq_req_t *ireq)
{
    return psm2_mq_cancel(ireq);
}

static NA_INLINE psm_error_t
psm_mq_test(psm_mq_req_t *ireq, psm_mq_status_t *status)
{
    return psm2_mq_test(ireq, status);
}

static NA_INLINE psm_error_t
psm_mq_isend(psm_mq_t mq, psm_epaddr_t dest, uint32_t flags, uint64_t stag,
    const void *buf, uint32_t len, void *context, psm_mq_req_t *req)
{
    return psm2_mq_isend(mq, dest, flags, stag, buf, len, context, req);
}

static NA_INLINE psm_error_t
psm_mq_irecv(psm_mq_t mq, uint64_t tag, uint64_t tagsel, uint32_t flags,
    void *buf, uint32_t len, void *context, psm_mq_req_t *reqo)
{
    return psm2_mq_irecv(mq, tag, tagsel, flags, buf, len, context, reqo);
}

static NA_INLINE psm_error_t
psm_mq_ipeek(psm_mq_t mq, psm_mq_req_t *oreq, psm_mq_status_t *status)
{
    return psm2_mq_ipeek(mq, oreq, status);
}

static NA_INLINE psm_error_t
psm_mq_init(psm_ep_t ep, uint64_t ignored, const psm_optkey_t *opts,
    int numopts, psm_mq_t *mqo)
{
    return psm2_mq_init(ep, ignored, opts, numopts, mqo);
}

static NA_INLINE psm_error_t
psm_mq_finalize(psm_mq_t mq)
{
    return psm2_mq_finalize(mq);
}

static NA_INLINE psm_error_t
psm_ep_open_opts_get_defaults(psm_ep_open_opts_t *opts)
{
    return psm2_ep_open_opts_get_defaults(opts);
}

static NA_INLINE psm2_error_t
psm_ep_open(psm_uuid_t const unique_job_key, const psm_ep_open_opts_t *opts_i,
    psm_ep_t *epo, psm_epid_t *epido)
{
    return psm2_ep_open(unique_job_key, opts_i, epo, epido);
}

static NA_INLINE psm_error_t
psm_ep_close(psm_ep_t ep, int mode, int64_t timeout_in)
{
    return psm2_ep_close(ep, mode, timeout_in);
}

static NA_INLINE psm_error_t
psm_ep_connect(psm_ep_t ep, int num_of_epid, psm_epid_t const *array_of_epid,
    int const *array_of_epid_mask, psm_error_t *array_of_errors,
    psm_epaddr_t *array_of_epaddr, int64_t timeout)
{
    return psm2_ep_connect(ep, num_of_epid, array_of_epid, array_of_epid_mask,
        array_of_errors, array_of_epaddr, timeout);
}
