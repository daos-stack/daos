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

#define OBJID_STR_SIZE	32
#define TIME_STR_SIZE	128

#ifndef DF_KEY_STR_SIZE
#define DF_KEY_STR_SIZE	64
#endif

#define CHK_ACTION_MAX	CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_EC_DATA

/* XXX: Must be strictly matches the order in Chk__CheckInconsistAction. */
static char *chk_act_strings[CHK_ACTION_MAX + 1] = {
	/* CHK__CHECK_INCONSIST_ACTION__CIA_DEFAULT = 0 */
	"Default action, depends on the detailed inconsistency class.",

	/* CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT = 1 */
	"Interact with administrator for further action.",

	/* CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE = 2 */
	"Ignore but log the inconsistency.",

	/* CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD = 3 */
	"Discard the unrecognized element: pool service, pool itself, container, and so on.",

	/* CHK__CHECK_INCONSIST_ACTION__CIA_READD = 4 */
	"Re-add the missing element: pool to MS, target to pool map, and so on.",

	/* CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_MS = 5 */
	"Trust the information recorded in MS DB.",

	/* CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS = 6 */
	"Trust the information recorded in PS DB.",

	/* CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_TARGET = 7 */
	"Trust the information recorded by target(s).",

	/* CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_MAJORITY = 8 */
	"Trust the majority parts (if have).",

	/* CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_LATEST = 9 */
	"Trust the one with latest (pool map or epoch) information. Keep the latest data.",

	/* CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_OLDEST = 10 */
	"Trust the one with oldest (pool map or epoch) information. Rollback to old version.",

	/* CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_EC_PARITY = 11 */
	"Trust EC parity shard.",

	/* CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_EC_DATA = 12 */
	"Trust EC data shard."
};

static int
chk_sg_list2string_array(d_sg_list_t *sgls, uint32_t sgl_nr, char ***array)
{
	char	**buf = NULL;
	int	  cnt = 0;
	int	  i;
	int	  j;
	int	  k;

	for (i = 0; i < sgl_nr; i++)
		cnt += sgls[i].sg_nr;

	if (unlikely(cnt == 0))
		goto out;

	D_ALLOC_ARRAY(buf, cnt);
	if (buf == NULL)
		D_GOTO(out, cnt = -DER_NOMEM);

	/*
	 * XXX: How to transfer all the data into d_sg_list_t array? Some may be not string.
	 */

	for (i = 0, k = 0; i < sgl_nr; i++) {
		for (j = 0; j < sgls[i].sg_nr; j++)
			buf[k++] = sgls[i].sg_iovs[j].iov_buf;
	}

out:
	*array = buf;

	return cnt;
}

int
chk_report_upcall(uint64_t gen, uint64_t seq, uint32_t cla, uint32_t act, int result,
		  d_rank_t rank, uint32_t target, uuid_t *pool, uuid_t *cont, daos_unit_oid_t *obj,
		  daos_key_t *dkey, daos_key_t *akey, char *msg, uint32_t option_nr,
		  uint32_t *options, uint32_t detail_nr, d_sg_list_t *details)
{
	Chk__CheckReport	  report = CHK__CHECK_REPORT__INIT;
	time_t			  tm = time(NULL);
	char			**act_msgs = NULL;
	int			  rc;
	int			  i;

	if (act == CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT) {
		D_ASSERT(option_nr > 0);
		D_ASSERT(options != NULL);

		D_ALLOC(act_msgs, option_nr);
		if (act_msgs == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		for (i = 0; i < option_nr; i++) {
			D_ASSERT(options[i] <= CHK_ACTION_MAX);
			act_msgs[i] = chk_act_strings[options[i]];
		}
	}

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

	if (cont != NULL && !uuid_is_null(*cont)) {
		D_ASPRINTF(report.cont_uuid, DF_UUIDF, DP_UUID(*cont));
		if (report.cont_uuid == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	} else {
		report.cont_uuid = NULL;
	}

	if (obj != NULL && !daos_unit_oid_is_null(*obj)) {
		D_ASPRINTF(report.objid, DF_UOID, DP_UOID(*obj));
		if (report.objid == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	} else {
		report.objid = NULL;
	}

	if (dkey != NULL && !daos_key_is_null(*dkey)) {
		D_ASPRINTF(report.dkey, DF_KEY, DP_KEY(dkey));
		if (report.dkey == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	} else {
		report.dkey = NULL;
	}

	if (akey != NULL && !daos_key_is_null(*akey)) {
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

	report.n_act_msgs = option_nr;
	report.act_msgs = act_msgs;

	rc = ds_chk_report_upcall(&report);

out:
	D_FREE(act_msgs);
	D_FREE(report.pool_uuid);
	D_FREE(report.cont_uuid);
	D_FREE(report.objid);
	D_FREE(report.dkey);
	D_FREE(report.akey);
	D_FREE(report.timestamp);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Check leader upcall for instance "DF_X64" for seq "DF_X64": "DF_RC"\n",
		 gen, seq, DP_RC(rc));

	return rc;
}
