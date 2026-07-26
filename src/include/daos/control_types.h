/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * \file
 *
 * DAOS Control Plane C API Types
 *
 * This header defines types used by the libdaos_control shared library,
 * which provides C bindings to the DAOS management/control plane.
 */

#ifndef __DAOS_CONTROL_TYPES_H__
#define __DAOS_CONTROL_TYPES_H__

#include <stdint.h>
#include <sys/types.h>
#include <uuid/uuid.h>

#include <gurt/types.h>
#include <daos_types.h>
#include <daos_prop.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Initialization options for the DAOS control library.
 *
 * All fields are optional; NULL selects the corresponding default.
 */
struct daos_control_init_args {
	/** Path to the dmg config file (NULL for the default / insecure config) */
	const char *dcia_config_file;
	/** Path to the log file (NULL disables logging) */
	const char *dcia_log_file;
	/** Log level: debug, info, notice, error (NULL selects notice) */
	const char *dcia_log_level;
};

/**
 * Arguments for daos_control_pool_create.
 */
struct daos_control_pool_create_args {
	/** UID to record as the pool's owner. */
	uid_t          dcpa_uid;
	/** GID to record as the pool's owner group. */
	gid_t          dcpa_gid;
	/** System/group name (NULL selects the default system). */
	const char    *dcpa_grp;
	/** Target ranks to host the pool (NULL lets the server choose). */
	d_rank_list_t *dcpa_tgts;
	/** SCM tier capacity per target, in bytes. */
	daos_size_t    dcpa_scm_size;
	/** NVMe tier capacity per target, in bytes. */
	daos_size_t    dcpa_nvme_size;
	/** Optional pool properties (NULL for defaults). */
	daos_prop_t   *dcpa_prop;
	/** Requested number of service replicas (dmg --nsvc); 0 = server picks. */
	uint32_t       dcpa_nsvc;
};

/**
 * Maximum number of targets that can be attached to a single device. Must stay
 * in sync with BIO_MAX_VOS_TGT_CNT (daos_srv/bio.h) — enforced by a
 * D_CASSERT in src/common/tests_dmg_helpers.c, which sees both headers.
 */
#define DAOS_MAX_TARGETS_PER_DEVICE    96

/**
 * Maximum hostname length (matches POSIX _POSIX_HOST_NAME_MAX).
 */
#define DAOS_HOSTNAME_MAX_LEN          255

/**
 * Maximum NVMe device state name length (longest current value is "UNPLUGGED",
 * 9 chars + NUL). A D_CASSERT in src/common/tests_dmg_helpers.c pins this to
 * the struct field width so the two stay in sync.
 */
#define DAOS_DEV_STATE_MAX_LEN         10

/**
 * Server-side cap on interactive action choices per check report. Defined
 * here (the one header both the engine and libdaos_control can see) and
 * aliased by CHK_INTERACT_OPTION_MAX in src/chk/chk_internal.h.
 */
#define DAOS_CHECK_INTERACT_OPTION_MAX 3

/**
 * Capacity of the report snapshot below; must hold every option the server
 * may send. check.go truncates extra choices as a last line of defense.
 */
#define DAOS_CHECK_MAX_ACT_OPTIONS     4

#if defined(__cplusplus)
static_assert(DAOS_CHECK_MAX_ACT_OPTIONS >= DAOS_CHECK_INTERACT_OPTION_MAX,
	      "check report snapshot cannot hold every interactive option");
#else
_Static_assert(DAOS_CHECK_MAX_ACT_OPTIONS >= DAOS_CHECK_INTERACT_OPTION_MAX,
	       "check report snapshot cannot hold every interactive option");
#endif

/**
 * Storage device information.
 */
typedef struct device_list {
	uuid_t dl_device_id;
	char   dl_state[DAOS_DEV_STATE_MAX_LEN];
	int    dl_rank;
	char   dl_host[DAOS_HOSTNAME_MAX_LEN];
	int    dl_tgtidx[DAOS_MAX_TARGETS_PER_DEVICE];
	int    dl_n_tgtidx;
} device_list;

/**
 * DAOS checker pool information.
 */
struct daos_check_pool_info {
	uuid_t dcpi_uuid;
	char  *dcpi_status;
	char  *dcpi_phase;
};

/**
 * DAOS checker report information.
 */
struct daos_check_report_info {
	uuid_t   dcri_uuid;
	uint64_t dcri_seq;
	uint32_t dcri_class;
	uint32_t dcri_act;
	int      dcri_rank;
	int      dcri_result;
	int      dcri_option_nr;
	int      dcri_options[DAOS_CHECK_MAX_ACT_OPTIONS];
};

/**
 * DAOS checker query results.
 *
 * All pointer fields are allocated by daos_control_check_query() and must
 * be freed by calling daos_control_check_info_free().
 */
struct daos_check_info {
	char                          *dci_status;
	char                          *dci_phase;
	int                            dci_leader;
	int                            dci_pool_nr;
	int                            dci_report_nr;
	struct daos_check_pool_info   *dci_pools;
	struct daos_check_report_info *dci_reports;
};

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_CONTROL_TYPES_H__ */
