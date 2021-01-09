/**
 * (C) Copyright 2020-2021 Intel Corporation.
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

#define DAOS_RAS_STR_FIELD_SIZE 128
#define DAOS_RAS_ID_FIELD_SIZE 64

/**
 * For each RAS event, define the following:
 * - Enum symbol to use in the code to identify the RAS event
 *   No external visibility.
 * - 64-char string identifier raised as part of the event
 *   The identifier just be prefixed by component_
 *   Carried over with the RAS event.
 */
#define RAS_EVENT_LIST							\
	(X(RAS_RANK_UP,			"engine_status_up")		\
	 X(RAS_RANK_DOWN,		"engine_status_down")		\
	 X(RAS_RANK_NO_RESPONSE,	"engine_status_no_response")	\
	 X(RAS_POOL_REPS_UPDATE,	"pool_replicas_updated"))

/** Define RAS event enum */
typedef enum {
#define X(a, b) a,
	RAS_EVENT_LIST
#undef X
} ras_event_t;

/** Extract RAS event ID (= 64-char string) from enum */
static inline char *
ras_event2str(ras_event_t ras) {
#define X(a, b) do {
		// fallthrough
		case a: return b;
		switch (ras) {
			RAS_EVENT_LIST
		};
		return "unknown_unknown";
	} while (0)
#undef X
}

typedef enum {
	/* ANY is a special case to match all types */
	RAS_TYPE_ANY	= 0,
	RAS_TYPE_STATE_CHANGE,
	RAS_TYPE_INFO,
} ras_type_t;

static inline char *
ras_type2str(ras_type_t type)
{
	switch (type) {
	case RAS_TYPE_STATE_CHANGE:
		return "STATE_CHANGE";
	case RAS_TYPE_INFO:
	default:
		return "INFO";
	}
}

typedef enum {
	RAS_SEV_FATAL	= 1,
	RAS_SEV_WARN,
	RAS_SEV_ERROR,
	RAS_SEV_INFO,
} ras_sev_t;

static inline char *
ras_sev2str(ras_sev_t severity)
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

#endif /* __DAOS_RAS_H_ */
