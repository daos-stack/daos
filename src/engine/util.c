/*
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(server)

#include <daos_srv/daos_engine.h>

#ifdef DAOS_WITH_REF_TRACKER

static void
dss_ref_tracker_dumper_ult(void *arg)
{
	struct dss_ref_tracker_dumper *dumper = arg;
	int                            n;

	for (n = 0; !dss_ult_exiting(dumper->rftd_req); n++) {
		if (n % 10 == 0)
			d_ref_tracker_dump(dumper->rftd_tracker, dumper->rftd_func,
					   dumper->rftd_line);
		sched_req_sleep(dumper->rftd_req, 1000 /* ms */);
	}
}

/** Use DSS_REF_TRACKER_INIT_DUMPER instead. */
void
dss_ref_tracker_init_dumper(struct dss_ref_tracker_dumper *dumper, struct d_ref_tracker *tracker,
			     const char *func, int line)
{
	uuid_t                anonym_uuid;
	struct sched_req_attr attr;

	uuid_clear(anonym_uuid);
	sched_req_attr_init(&attr, SCHED_REQ_ANONYM, &anonym_uuid);
	dumper->rftd_req = sched_create_ult(&attr, dss_ref_tracker_dumper_ult, dumper, 0);
	D_ASSERT(dumper->rftd_req != NULL);

	dumper->rftd_tracker = tracker;
	dumper->rftd_func    = func;
	dumper->rftd_line    = line;
}

/** Use DSS_REF_TRACKER_FINI_DUMPER instead. */
void
dss_ref_tracker_fini_dumper(struct dss_ref_tracker_dumper *dumper)
{
	sched_req_wait(dumper->rftd_req, true);
	sched_req_put(dumper->rftd_req);
}

#endif /* DAOS_WITH_REF_TRACKER */
