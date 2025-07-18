/**
 * (C) Copyright 2020-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * RAS event definitions to the used in either data or control planes.
 */

#ifndef __DAOS_RAS_H__
#define __DAOS_RAS_H__

#include <daos_types.h>
#include <daos/object.h>
#include <daos_srv/daos_chk.h>

#define DAOS_RAS_STR_FIELD_SIZE 128
#define DAOS_RAS_ID_FIELD_SIZE 64

/**
 * For each RAS event, define the following:
 * - Enum symbol to use in the code to identify the RAS event
 *   No external visibility.
 * - 64-char string identifier raised as part of the event
 *   The identifier just be prefixed by component_
 *   Carried over with the RAS event.
 *
 * NB: Any events that should be acted upon by the control plane
 * will need complementary constants defined in src/control/events/ras.go.
 * Events that are informational-only (i.e. just logged) don't need to be
 * mirrored in the control plane.
 *
 * In order to minimize conflicts between patches, please:
 *   * Don't change the first and last entries in the list
 *   * Don't arbitrarily reorder entries
 *   * Do limit lines to 99 columns, wrapping as necessary
 */
#define RAS_EVENT_LIST                                                                             \
	X(RAS_UNKNOWN_EVENT, "unknown_ras_event")                                                  \
	X(RAS_ENGINE_FORMAT_REQUIRED, "engine_format_required")                                    \
	X(RAS_ENGINE_DIED, "engine_died")                                                          \
	X(RAS_ENGINE_ASSERTED, "engine_asserted")                                                  \
	X(RAS_ENGINE_CLOCK_DRIFT, "engine_clock_drift")                                            \
	X(RAS_POOL_CORRUPTION_DETECTED, "pool_corruption_detected")                                \
	X(RAS_POOL_REBUILD_START, "pool_rebuild_started")                                          \
	X(RAS_POOL_REBUILD_END, "pool_rebuild_finished")                                           \
	X(RAS_POOL_REBUILD_FAILED, "pool_rebuild_failed")                                          \
	X(RAS_POOL_REPS_UPDATE, "pool_replicas_updated")                                           \
	X(RAS_POOL_DF_INCOMPAT, "pool_durable_format_incompatible")                                \
	X(RAS_POOL_DEFER_DESTROY, "pool_destroy_deferred")                                         \
	X(RAS_POOL_START_FAILED, "pool_start_failed")                                              \
	X(RAS_CONT_DF_INCOMPAT, "container_durable_format_incompatible")                           \
	X(RAS_RDB_DF_INCOMPAT, "rdb_durable_format_incompatible")                                  \
	X(RAS_SWIM_RANK_ALIVE, "swim_rank_alive")                                                  \
	X(RAS_SWIM_RANK_DEAD, "swim_rank_dead")                                                    \
	X(RAS_SYSTEM_START_FAILED, "system_start_failed")                                          \
	X(RAS_SYSTEM_STOP_FAILED, "system_stop_failed")                                            \
	X(RAS_DEVICE_SET_FAULTY, "device_set_faulty")                                              \
	X(RAS_DEVICE_MEDIA_ERROR, "device_media_error")                                            \
	X(RAS_DEVICE_UNPLUGGED, "device_unplugged")                                                \
	X(RAS_DEVICE_PLUGGED, "device_plugged")                                                    \
	X(RAS_DEVICE_REPLACE, "device_replace")                                                    \
	X(RAS_SYSTEM_FABRIC_PROV_CHANGED, "system_fabric_provider_changed")                        \
	X(RAS_ENGINE_JOIN_FAILED, "engine_join_failed")                                            \
	X(RAS_DEVICE_LINK_SPEED_CHANGED, "device_link_speed_changed")                              \
	X(RAS_DEVICE_LINK_WIDTH_CHANGED, "device_link_width_changed")

/** Define RAS event enum */
typedef enum {
#define X(a, b) a,
	RAS_EVENT_LIST
#undef X
} ras_event_t;

/** Extract RAS event ID (= 64-char string) from enum */
static inline char *
ras_event2str(ras_event_t ras) {
#define X(a, b) case a: return b;
	switch (ras) {
		RAS_EVENT_LIST
	};
	return "unknown_unknown";
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
	RAS_SEV_UNKNOWN = 0,
	RAS_SEV_ERROR,
	RAS_SEV_WARNING,
	RAS_SEV_NOTICE,
} ras_sev_t;

static inline char *
ras_sev2str(ras_sev_t severity)
{
	switch (severity) {
	case RAS_SEV_ERROR:
		return "ERROR";
	case RAS_SEV_WARNING:
		return "WARNING";
	default:
		return "NOTICE";
	}
}

/**
 * Raise a RAS event and forward to the control plane.
 *
 * \param[in] id	Unique event identifier.
 * \param[in] msg	Human readable message.
 * \param[in] type	Event type.
 * \param[in] sev	Event instance severity.
 * \param[in] hwid	(Optional) Hardware component involved.
 * \param[in] rank	(Optional) DAOS rank involved.
 * \param[in] inc	(Optional) Incarnation of DAOS rank involved.
 * \param[in] jobid	(Optional) Client job involved.
 * \param[in] pool	(Optional) DAOS pool involved.
 * \param[in] cont	(Optional) DAOS container involved.
 * \param[in] objid	(Optional) DAOS object involved.
 * \param[in] ctlop	(Optional) Recommended automatic control operation.
 * \param[in] data	(Optional) Specific instance data treated as a blob.
 *
 * \retval		N/A
 */
void __attribute__((weak))
ds_notify_ras_event(ras_event_t id, char *msg, ras_type_t type, ras_sev_t sev,
		    char *hwid, d_rank_t *rank, uint64_t *inc, char *jobid,
		    uuid_t *pool, uuid_t *cont, daos_obj_id_t *objid, char *ctlop,
		    char *data);

/**
 * A printf-style message-formatting wrapper for ds_notify_ras_event. If the
 * resulting message is too long for DAOS_RAS_STR_FIELD_SIZE, it will be ended
 * with a '$' to indicate so. See ds_notify_ras_event for parameter
 * documentation.
 */
void __attribute__((weak, __format__(__printf__, 13, 14)))
ds_notify_ras_eventf(ras_event_t id, ras_type_t type, ras_sev_t sev, char *hwid,
		     d_rank_t *rank, uint64_t *inc, char *jobid, uuid_t *pool,
		     uuid_t *cont, daos_obj_id_t *objid, char *ctlop, char *data,
		     const char *fmt, ...);
/*
 * Wrapper of ds_notify_ras_event.
 */
static inline void
ras_notify_event(ras_event_t id, char *msg, ras_type_t type, ras_sev_t sev, char *hwid,
		 d_rank_t *rank, uint64_t *inc, char *jobid, uuid_t *pool,
		 uuid_t *cont, daos_obj_id_t *objid, char *ctlop, char *data)
{
	if (ds_notify_ras_event != NULL) {
		ds_notify_ras_event(id, msg, type, sev, hwid, rank, inc, jobid,
				    pool, cont, objid, ctlop, data);
	} else {
		switch (sev) {
		case RAS_SEV_ERROR:
			D_ERROR("%s\n", msg);
			break;
		case RAS_SEV_NOTICE:
			D_INFO("%s\n", msg);
			break;
		case RAS_SEV_WARNING:
			D_WARN("%s\n", msg);
			break;
		default:
			D_ERROR("%s\n", msg);
			break;
		}
	}
}

/*
 * Wrapper of ds_notify_ras_eventf.
 */
static inline void
ras_notify_eventf(ras_event_t id, ras_type_t type, ras_sev_t sev, char *hwid,
		  d_rank_t *rank, uint64_t *inc, char *jobid, uuid_t *pool,
		  uuid_t *cont, daos_obj_id_t *objid, char *ctlop, char *data,
		  const char *fmt, ...)
{
	char	buf[DAOS_RAS_STR_FIELD_SIZE];
	va_list	ap;
	int	rc;

	va_start(ap, fmt);
	rc = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (rc >= sizeof(buf))
		/* The message is too long. End it with '$'. */
		buf[sizeof(buf) - 2] = '$';

	ras_notify_event(id, buf, type, sev, hwid, rank, inc, jobid,
			 pool, cont, objid, ctlop, data);
}

/**
 * Notify control plane of an update to a pool's service replicas and wait for
 * a response.
 *
 * \param[in] pool	UUID of DAOS pool with updated service replicas.
 * \param[in] svcl	New list of pool service replica ranks.
 * \param[in] version	Version of \a svcl.
 *
 * \retval		Zero on success, non-zero otherwise.
 */
int
ds_notify_pool_svc_update(uuid_t *pool, d_rank_list_t *svcl, uint64_t version);

/**
 * Notify control plane that swim has detected a dead rank.
 *
 * \param[in] rank		Rank that was marked dead.
 * \param[in] incarnation	Incarnation of rank that was marked dead.
 *
 * \retval		Zero on success, non-zero otherwise.
 */
int
ds_notify_swim_rank_dead(d_rank_t rank, uint64_t incarnation);

/**
 * List all the known pools from control plane (MS).
 *
 * \param[out] clp	The pools list.
 *
 * \retval		Positive value for the conut of pools.
 *			Negative value if error.
 */
int
ds_chk_listpool_upcall(struct chk_list_pool **clp);

/**
 * Register the pool to control plane (MS).
 *
 * \param[in] seq	DAOS Check event sequence, unique for the instance.
 * \param[in] uuid	The pool uuid.
 * \param[in] label	The pool label, optional.
 * \param[in] svcreps	Ranks for the pool service.
 *
 * \retval		Zero on success, non-zero otherwise.
 */
int
ds_chk_regpool_upcall(uint64_t seq, uuid_t uuid, char *label, d_rank_list_t *svcreps);

/**
 * Deregister the pool from control plane (MS).
 *
 * \param[in] seq	DAOS Check event sequence, unique for the instance.
 * \param[in] uuid	The pool uuid.
 *
 * \retval		Zero on success, non-zero otherwise.
 */
int
ds_chk_deregpool_upcall(uint64_t seq, uuid_t uuid);

/**
 * Report inconsistency to control plane (MS).
 *
 * \param[in] rpt	The pointer to Chk__CheckReport.
 *
 * \retval		Zero on success, non-zero otherwise.
 */
int
ds_chk_report_upcall(void *rpt);

void
ds_chk_free_pool_list(struct chk_list_pool *clp, uint32_t nr);

#endif /* __DAOS_RAS_H_ */
