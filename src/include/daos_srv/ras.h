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

#ifndef __RAS_H__
#define __RAS_H__

#define RAS_ID_UNKNOWN_STR "Unknown RAS event"
#define RAS_SEV_UNKNOWN_STR "Unknown RAS event severity"
#define RAS_TYPE_UNKNOWN_STR "Unknown RAS event type"

enum ras_event_id {
	RAS_RANK_EXIT	= 1,
	RAS_RANK_NO_RESP,
};

static inline char *
ras_event_id_enum_to_name(enum ras_event_id id)
{
	switch (id) {
	case RAS_RANK_EXIT:
		return "daos_rank_exited";
	case RAS_RANK_NO_RESP:
		return "daos_rank_no_response";
	}

	return RAS_ID_UNKNOWN_STR;
}

static inline char *
ras_event_id_enum_to_msg(enum ras_event_id id)
{
	switch (id) {
	case RAS_RANK_EXIT:
		return "DAOS rank exited";
	case RAS_RANK_NO_RESP:
		return "DAOS rank unresponsive";
	}

	return RAS_ID_UNKNOWN_STR;
}

enum ras_event_sev {
	RAS_SEV_FATAL	= 1,
	RAS_SEV_WARN,
	RAS_SEV_ERROR,
	RAS_SEV_INFO,
};

static inline char *
ras_event_sev_enum_to_name(enum ras_event_sev severity)
{
	switch (severity) {
	case RAS_SEV_FATAL:
		return "FATAL";
	case RAS_SEV_WARN:
		return "WARN";
	case RAS_SEV_ERROR:
		return "ERROR";
	case RAS_SEV_INFO:
		return "INFO";
	}

	return RAS_SEV_UNKNOWN_STR;
}

enum ras_event_type {
	/* ANY is a special case to match all types */
	RAS_TYPE_ANY	= 0,
	RAS_TYPE_RANK_STATE_CHANGE,
	RAS_TYPE_INFO_ONLY,
};

static inline char *
ras_event_type_enum_to_name(enum ras_event_type type)
{
	switch (type) {
	case RAS_TYPE_RANK_STATE_CHANGE:
		return "RANK_STATE_CHANGE";
	case RAS_TYPE_INFO_ONLY:
		return "INFO_ONLY";
	}

	return RAS_TYPE_UNKNOWN_STR;
}
#endif /* __RAS_H_ */
