/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * common helper functions for object
 */
#define DDSUBSYS	DDFAC(object)

#include <daos_types.h>
#include <daos/debug.h>
#include <daos/job.h>
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_producer.h>
#include "obj_internal.h"

static daos_size_t
daos_iod_len(daos_iod_t *iod)
{
	daos_size_t	len;
	int		i;

	if (iod->iod_size == DAOS_REC_ANY)
		return -1; /* unknown size */

	len = 0;

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		len += iod->iod_size;
	} else {
		if (iod->iod_recxs == NULL)
			return 0;

		for (i = 0, len = 0; i < iod->iod_nr; i++)
			len += iod->iod_size * iod->iod_recxs[i].rx_nr;
	}

	return len;
}

daos_size_t
daos_iods_len(daos_iod_t *iods, int nr)
{
	daos_size_t iod_length = 0;
	int	    i;

	for (i = 0; i < nr; i++) {
		daos_size_t len = daos_iod_len(&iods[i]);

		if (len == (daos_size_t)-1) /* unknown */
			return -1;

		iod_length += len;
	}
	return iod_length;
}

int
daos_iod_copy(daos_iod_t *dst, daos_iod_t *src)
{
	int rc;

	rc = daos_iov_copy(&dst->iod_name, &src->iod_name);
	if (rc)
		return rc;

	dst->iod_type	= src->iod_type;
	dst->iod_size	= src->iod_size;
	dst->iod_nr	= src->iod_nr;
	dst->iod_recxs	= src->iod_recxs;

	return 0;
}

void
daos_iods_free(daos_iod_t *iods, int nr, bool need_free)
{
	int i;

	for (i = 0; i < nr; i++) {
		daos_iov_free(&iods[i].iod_name);

		if (iods[i].iod_recxs)
			D_FREE(iods[i].iod_recxs);
	}

	if (need_free)
		D_FREE(iods);
}

int
obj_latency_tm_init(uint32_t opc, int tgt_id, struct d_tm_node_t **tm, char *op, char *desc,
		    bool server)
{
	unsigned int bucket_max = 256;
	int          i;
	int          rc = 0;

	for (i = 0; i < NR_LATENCY_BUCKETS; i++) {
		char *path;

		if (server) {
			if (bucket_max < 1024) /** B */
				D_ASPRINTF(path, "io/latency/%s/%uB/tgt_%u", op, bucket_max,
					   tgt_id);
			else if (bucket_max < 1024 * 1024) /** KB */
				D_ASPRINTF(path, "io/latency/%s/%uKB/tgt_%u", op, bucket_max / 1024,
					   tgt_id);
			else if (bucket_max <= 1024 * 1024 * 4) /** MB */
				D_ASPRINTF(path, "io/latency/%s/%uMB/tgt_%u", op,
					   bucket_max / (1024 * 1024), tgt_id);
			else /** >4MB */
				D_ASPRINTF(path, "io/latency/%s/GT4MB/tgt_%u", op, tgt_id);
		} else {
			unsigned long tid = pthread_self();

			if (bucket_max < 1024) /** B */
				D_ASPRINTF(path, "%lu/io/latency/%s/%uB", tid, op, bucket_max);
			else if (bucket_max < 1024 * 1024) /** KB */
				D_ASPRINTF(path, "%lu/io/latency/%s/%uKB", tid, op,
					   bucket_max / 1024);
			else if (bucket_max <= 1024 * 1024 * 4) /** MB */
				D_ASPRINTF(path, "%lu/io/latency/%s/%uMB", tid, op,
					   bucket_max / (1024 * 1024));
			else /** >4MB */
				D_ASPRINTF(path, "%lu/io/latency/%s/GT4MB", tid, op);
		}
		rc = d_tm_add_metric(&tm[i], D_TM_STATS_GAUGE, desc, "us", path);
		if (rc)
			D_WARN("Failed to create per-I/O size latency "
			       "sensor: " DF_RC "\n",
			       DP_RC(rc));
		D_FREE(path);

		bucket_max <<= 1;
	}

	return rc;
}

void
obj_metrics_free(void *data)
{
	D_FREE(data);
}

int
obj_metrics_count(void)
{
	return (sizeof(struct obj_pool_metrics) / sizeof(struct d_tm_node_t *));
}

void *
obj_metrics_alloc_internal(const char *path, int tgt_id, bool server)
{
	struct obj_pool_metrics *metrics;
	char                     tgt_path[32];
	uint32_t                 opc;
	int                      rc;

	D_ASSERT(tgt_id >= 0);
	if (server)
		snprintf(tgt_path, sizeof(tgt_path), "/tgt_%u", tgt_id);
	else
		tgt_path[0] = '\0';

	D_ALLOC_PTR(metrics);
	if (metrics == NULL) {
		D_ERROR("failed to alloc object metrics");
		return NULL;
	}

	/** register different per-opcode counters */
	for (opc = 0; opc < OBJ_PROTO_CLI_COUNT; opc++) {
		/** Then the total number of requests, of type counter */
		rc = d_tm_add_metric(&metrics->opm_total[opc], D_TM_COUNTER,
				     "total number of processed object RPCs", "ops", "%s/ops/%s%s",
				     path, obj_opc_to_str(opc), tgt_path);
		if (rc)
			D_WARN("Failed to create total counter: " DF_RC "\n", DP_RC(rc));
	}

	/** Total number of silently restarted updates, of type counter */
	rc = d_tm_add_metric(&metrics->opm_update_restart, D_TM_COUNTER,
			     "total number of restarted update ops", "updates", "%s/restarted%s",
			     path, tgt_path);
	if (rc)
		D_WARN("Failed to create restarted counter: " DF_RC "\n", DP_RC(rc));

	/** Total number of resent updates, of type counter */
	rc = d_tm_add_metric(&metrics->opm_update_resent, D_TM_COUNTER,
			     "total number of resent update RPCs", "updates", "%s/resent%s", path,
			     tgt_path);
	if (rc)
		D_WARN("Failed to create resent counter: " DF_RC "\n", DP_RC(rc));

	/** Total number of retry updates locally, of type counter */
	rc = d_tm_add_metric(&metrics->opm_update_retry, D_TM_COUNTER,
			     "total number of retried update RPCs", "updates", "%s/retry%s", path,
			     tgt_path);
	if (rc)
		D_WARN("Failed to create retry cnt sensor: " DF_RC "\n", DP_RC(rc));

	/** Total bytes read */
	rc = d_tm_add_metric(&metrics->opm_fetch_bytes, D_TM_COUNTER,
			     "total number of bytes fetched/read", "bytes", "%s/xferred/fetch%s",
			     path, tgt_path);
	if (rc)
		D_WARN("Failed to create bytes fetch counter: " DF_RC "\n", DP_RC(rc));

	/** Total bytes written */
	rc = d_tm_add_metric(&metrics->opm_update_bytes, D_TM_COUNTER,
			     "total number of bytes updated/written", "bytes",
			     "%s/xferred/update%s", path, tgt_path);
	if (rc)
		D_WARN("Failed to create bytes update counter: " DF_RC "\n", DP_RC(rc));

	/** Total number of EC full-stripe update operations, of type counter */
	rc = d_tm_add_metric(&metrics->opm_update_ec_full, D_TM_COUNTER,
			     "total number of EC full-stripe updates", "updates",
			     "%s/EC_update/full_stripe%s", path, tgt_path);
	if (rc)
		D_WARN("Failed to create EC full stripe update counter: " DF_RC "\n", DP_RC(rc));

	/** Total number of EC partial update operations, of type counter */
	rc = d_tm_add_metric(&metrics->opm_update_ec_partial, D_TM_COUNTER,
			     "total number of EC partial updates", "updates",
			     "%s/EC_update/partial%s", path, tgt_path);
	if (rc)
		D_WARN("Failed to create EC partial update counter: " DF_RC "\n", DP_RC(rc));

	return metrics;
}

struct recx_rec {
	daos_recx_t	*rr_recx;
};

static int
recx_key_cmp(struct btr_instance *tins, struct btr_record *rec, d_iov_t *key)
{
	struct recx_rec	*r = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	daos_recx_t	*key_recx = (daos_recx_t *)(key->iov_buf);

	D_ASSERT(key->iov_len == sizeof(*key_recx));

	if (DAOS_RECX_PTR_OVERLAP(r->rr_recx, key_recx)) {
		D_ERROR("recx overlap between ["DF_U64", "DF_U64"], "
			"["DF_U64", "DF_U64"].\n", r->rr_recx->rx_idx,
			r->rr_recx->rx_nr, key_recx->rx_idx, key_recx->rx_nr);
		return BTR_CMP_ERR;
	}

	/* will never return BTR_CMP_EQ */
	D_ASSERT(r->rr_recx->rx_idx != key_recx->rx_idx);
	if (r->rr_recx->rx_idx < key_recx->rx_idx)
		return BTR_CMP_LT;

	return BTR_CMP_GT;
}

static int
recx_rec_alloc(struct btr_instance *tins, d_iov_t *key, d_iov_t *val,
	     struct btr_record *rec, d_iov_t *val_out)
{
	struct recx_rec	*r;
	umem_off_t	roff;
	daos_recx_t	*key_recx = (daos_recx_t *)(key->iov_buf);

	if (key_recx == NULL || key->iov_len != sizeof(*key_recx))
		return -DER_INVAL;

	roff = umem_zalloc(&tins->ti_umm, sizeof(*r));
	if (UMOFF_IS_NULL(roff))
		return tins->ti_umm.umm_nospc_rc;

	r = umem_off2ptr(&tins->ti_umm, roff);
	r->rr_recx = key_recx;
	rec->rec_off = roff;

	return 0;
}

static int
recx_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	umem_free(&tins->ti_umm, rec->rec_off);
	return 0;
}

static int
recx_rec_update(struct btr_instance *tins, struct btr_record *rec,
		d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	D_ASSERTF(0, "recx_rec_update should never be called.\n");
	return 0;
}

static int
recx_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	       d_iov_t *key, d_iov_t *val)
{
	D_ASSERTF(0, "recx_rec_fetch should never be called.\n");
	return 0;
}

static void
recx_key_encode(struct btr_instance *tins, d_iov_t *key,
	daos_anchor_t *anchor)
{
	D_ASSERTF(0, "recx_key_encode should never be called.\n");
}

static void
recx_key_decode(struct btr_instance *tins, d_iov_t *key,
	daos_anchor_t *anchor)
{
	D_ASSERTF(0, "recx_key_decode should never be called.\n");
}

static char *
recx_rec_string(struct btr_instance *tins, struct btr_record *rec, bool leaf,
		char *buf, int buf_len)
{
	struct recx_rec	*r = NULL;
	daos_recx_t	*recx;

	if (!leaf) {
		/* no record body on intermediate node */
		snprintf(buf, buf_len, "--");
	} else {
		r = (struct recx_rec *)umem_off2ptr(&tins->ti_umm,
						   rec->rec_off);
		recx = r->rr_recx;
		snprintf(buf, buf_len, "rx_idx - "DF_U64" : rx_nr - "DF_U64,
			 recx->rx_idx, recx->rx_nr);
	}

	return buf;
}

static btr_ops_t recx_btr_ops = {
	.to_key_cmp	= recx_key_cmp,
	.to_rec_alloc	= recx_rec_alloc,
	.to_rec_free	= recx_rec_free,
	.to_rec_fetch	= recx_rec_fetch,
	.to_rec_update	= recx_rec_update,
	.to_rec_string	= recx_rec_string,
	.to_key_encode	= recx_key_encode,
	.to_key_decode	= recx_key_decode
};

int
obj_utils_init(void)
{
	int	rc;

	rc = dbtree_class_register(DBTREE_CLASS_RECX, BTR_FEAT_DIRECT_KEY,
				   &recx_btr_ops);
	if (rc != 0 && rc != -DER_EXIST) {
		D_ERROR("failed to register DBTREE_CLASS_RECX: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(failed, rc);
	}
	return 0;
failed:
	D_ERROR("Failed to initialize DAOS object utilities\n");
	return rc;
}

void
obj_utils_fini(void)
{
}
