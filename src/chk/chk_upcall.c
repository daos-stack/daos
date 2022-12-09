/*
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(chk)

#include <time.h>
#include <daos_types.h>
#include <daos/common.h>
#include <daos/object.h>
#include <daos/drpc_modules.h>
#include <daos_srv/ras.h>

#include "chk.pb-c.h"
#include "chk_internal.h"

#define CHK_ACTION_MAX	CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_EC_DATA

static void
chk_sg_free(char **buf, int cnt)
{
	int	i;

	if (buf != NULL) {
		for (i = 0; i < cnt; i++)
			D_FREE(buf[i]);
		D_FREE(buf);
	}
}

static int
chk_sg_list2string_array(d_sg_list_t *sgls, uint32_t sgl_nr, char ***array)
{
	char	**buf = NULL;
	int	  cnt = 0;
	int	  rc = 0;
	int	  i;
	int	  j;
	int	  k;

	for (i = 0; i < sgl_nr; i++)
		cnt += sgls[i].sg_nr;

	if (unlikely(cnt == 0))
		goto out;

	D_ALLOC_ARRAY(buf, cnt);
	if (buf == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* QUEST: How to transfer all the data into d_sg_list_t array? Some may be not string. */

	for (i = 0, k = 0; i < sgl_nr; i++) {
		for (j = 0; j < sgls[i].sg_nr; j++, k++) {
			rc = chk_dup_string(&buf[k], sgls[i].sg_iovs[j].iov_buf,
					    sgls[i].sg_iovs[j].iov_len);
			if (rc != 0)
				goto out;
		}
	}

out:
	if (rc == 0) {
		*array = buf;
	} else {
		chk_sg_free(buf, cnt);
		cnt = rc;
	}

	return cnt;
}

int
chk_report_upcall(uint64_t gen, uint64_t seq, uint32_t cla, uint32_t act, int result,
		  d_rank_t rank, uint32_t target, uuid_t *pool, char *pool_label,
		  uuid_t *cont, char *cont_label, daos_unit_oid_t *obj,
		  daos_key_t *dkey, daos_key_t *akey, char *msg, uint32_t option_nr,
		  uint32_t *options, uint32_t detail_nr, d_sg_list_t *details)
{
	Chk__CheckReport	  report = CHK__CHECK_REPORT__INIT;
	time_t			  tm = time(NULL);
	int			  rc;

	report.seq = seq;
	report.class_ = cla;
	report.action = act;
	report.result = result;
	report.rank = rank;
	report.target = target;

	if (pool != NULL && !uuid_is_null(*pool)) {
		D_ASPRINTF(report.pool_uuid, DF_UUIDF, DP_UUID(*pool));
		if (report.pool_uuid == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	} else {
		report.pool_uuid = NULL;
	}

	report.pool_label = pool_label;

	if (cont != NULL && !uuid_is_null(*cont)) {
		D_ASPRINTF(report.cont_uuid, DF_UUIDF, DP_UUID(*cont));
		if (report.cont_uuid == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	} else {
		report.cont_uuid = NULL;
	}

	report.cont_label = cont_label;

	if (obj != NULL && !daos_unit_oid_is_null(*obj)) {
		D_ASPRINTF(report.objid, DF_UOID, DP_UOID(*obj));
		if (report.objid == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	} else {
		report.objid = NULL;
	}

	if (!daos_iov_empty(dkey)) {
		D_ASPRINTF(report.dkey, DF_KEY, DP_KEY(dkey));
		if (report.dkey == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	} else {
		report.dkey = NULL;
	}

	if (!daos_iov_empty(akey)) {
		D_ASPRINTF(report.akey, DF_KEY, DP_KEY(akey));
		if (report.akey == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	} else {
		report.akey = NULL;
	}

	D_ASPRINTF(report.timestamp, "%s", ctime(&tm));
	if (report.timestamp == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	report.msg = msg;
	report.n_act_choices = option_nr;
	report.act_choices = option_nr != 0 ? options : NULL;

	if (detail_nr != 0) {
		D_ASSERT(details != NULL);

		rc = chk_sg_list2string_array(details, detail_nr, &report.act_details);
		if (rc < 0)
			goto out;

		report.n_act_details = rc;
	} else {
		report.n_act_details = 0;
		report.act_details = NULL;
	}

	rc = ds_chk_report_upcall(&report);

out:
	D_FREE(report.pool_uuid);
	D_FREE(report.cont_uuid);
	D_FREE(report.objid);
	D_FREE(report.dkey);
	D_FREE(report.akey);
	D_FREE(report.timestamp);
	chk_sg_free(report.act_details, report.n_act_details);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Check leader upcall for instance "DF_X64" for seq "DF_X64": "DF_RC"\n",
		 gen, seq, DP_RC(rc));

	return rc;
}
