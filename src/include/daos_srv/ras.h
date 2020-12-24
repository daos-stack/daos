/**
 * (C) Copyright 2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/*
 * RAS event definitions to the used in either data or control planes.
 */

#ifndef __DAOS_RAS_H__
#define __DAOS_RAS_H__

#include <daos/common.h>
#include <daos_types.h>
#include <sys/utsname.h>

#define DAOS_RAS_EVENT_STR_MAX_LEN 64

enum ras_event_sev {
	RAS_SEV_FATAL	= 1,
	RAS_SEV_WARN,
	RAS_SEV_ERROR,
	RAS_SEV_INFO,
};

static inline char *
ras_event_sev2str(enum ras_event_sev severity)
{
	switch (severity) {
	case RAS_SEV_FATAL:
		return "FATAL";
	case RAS_SEV_WARN:
		return "WARN";
	case RAS_SEV_ERROR:
		return "ERROR";
	case RAS_SEV_INFO:
	default:
		return "INFO";
	}
}

enum ras_event_type {
	/* ANY is a special case to match all types */
	RAS_TYPE_ANY	= 0,
	RAS_TYPE_STATE_CHANGE,
	RAS_TYPE_INFO,
};

static inline char *
ras_event_type2str(enum ras_event_type type)
{
	switch (type) {
	case RAS_TYPE_STATE_CHANGE:
		return "STATE_CHANGE";
	case RAS_TYPE_INFO:
	default:
		return "INFO";
	}
}

/**
 * RAS event should be both printed to debug log and sent to the control
 * plane.
 * XXX: only print to stdout for now
 */
static inline void
d_ras_raise(const char *id, enum ras_event_type type, enum ras_event_sev sev,
	    char *hid, d_rank_t rank, char *jid, uuid_t puuid, uuid_t cuuid,
	    daos_obj_id_t *oid, const char *cop, const char *msg,
	    const char *data, ...)
{
	struct timeval	tv;
	struct tm	*tm;
	struct utsname	uts;
	va_list		ap;

	printf("&&& RAS EVENT id: [%s]", id);

	/** print timestamp */
	(void)gettimeofday(&tv, 0);
	tm = localtime(&tv.tv_sec);
	if (tm) {
		printf(" ts: [%04d/%02d/%02d-%02d:%02d:%02d.%02ld]",
		       tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		       tm->tm_hour, tm->tm_min, tm->tm_sec,
		       (long int) tv.tv_usec / 10000);
	}

	/** XXX: nodename should be fetched only once and not on every call */
	(void) uname(&uts);
	printf(" host: [%s]", uts.nodename);

	printf(" type: [%s] sev: [%s]", ras_event_type2str(type),
	       ras_event_sev2str(sev));

	if (hid)
		printf(" hwid: [%s]", hid);

	if (rank)
		printf(" rank: [%u]", rank);

	if (jid)
		printf(" jobid: [%s]", jid);

	if (!uuid_is_null(puuid))
		printf(" puuid: ["DF_UUIDF"]", DP_UUID(puuid));

	if (!uuid_is_null(cuuid))
		printf(" cuuid: ["DF_UUIDF"]", DP_UUID(cuuid));

	if (oid)
		printf(" oid: ["DF_OID"]", DP_OID(*oid));

	if (cop)
		printf(" control_op: [%s]", cop);

	/** print msg */
	printf(" msg: [%s]", msg);

	/** print data blob */
	printf(" data: [");
	va_start(ap, data);
	vprintf(data, ap);
	va_end(ap);
	printf("]\n");
}

static inline void
d_ras_raise_pool(const char *id, enum ras_event_type type,
		 enum ras_event_sev sev, d_rank_t rank, uuid_t puuid,
		 const char *cop, const char *msg, const char *data, ...)
{
	uuid_t	uuid;
	va_list	ap;

	uuid_clear(uuid);

	va_start(ap, data);
	d_ras_raise(id, type, sev, NULL, rank, NULL, puuid, NULL, NULL,
		    NULL, msg, data, ap);
	va_end(ap);
}
#endif /* __DAOS_RAS_H_ */
