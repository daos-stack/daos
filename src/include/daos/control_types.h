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
	const char *config_file;
	/** Path to the log file (NULL disables logging) */
	const char *log_file;
	/** Log level: debug, info, notice, error (NULL selects notice) */
	const char *log_level;
};

/**
 * Arguments for daos_control_pool_create.
 */
struct daos_control_pool_create_args {
	/** UID to record as the pool's owner. */
	uid_t          uid;
	/** GID to record as the pool's owner group. */
	gid_t          gid;
	/** System/group name (NULL selects the default system). */
	const char    *grp;
	/** Target ranks to host the pool (NULL lets the server choose). */
	d_rank_list_t *tgts;
	/** SCM tier capacity per target, in bytes. */
	daos_size_t    scm_size;
	/** NVMe tier capacity per target, in bytes. */
	daos_size_t    nvme_size;
	/** Optional pool properties (NULL for defaults). */
	daos_prop_t   *prop;
	/** Requested number of service replicas (dmg --nsvc); 0 = server picks. */
	uint32_t       nsvc;
	/**
	 * [out] Service replica rank list, allocated by the library with
	 *       d_rank_list_alloc and populated with the server's chosen
	 *       replicas. MUST be NULL on input. Release with d_rank_list_free
	 *       when done. Remains NULL if the server returns no replicas.
	 */
	d_rank_list_t *svc;
	/** [out] UUID of the newly created pool. */
	uuid_t        *pool_uuid;
};

/**
 * Maximum number of targets that can be attached to a single device. Must stay
 * in sync with BIO_MAX_VOS_TGT_CNT (daos_srv/bio.h) — enforced by a
 * D_CASSERT in src/common/tests_dmg_helpers.c, which sees both headers.
 */
#define DAOS_MAX_TARGETS_PER_DEVICE 96

/**
 * Maximum hostname length (matches POSIX _POSIX_HOST_NAME_MAX).
 */
#define DAOS_HOSTNAME_MAX_LEN       255

/**
 * Maximum NVMe device state name length (longest current value is "UNPLUGGED",
 * 9 chars + NUL). A D_CASSERT in src/common/tests_dmg_helpers.c pins this to
 * the struct field width so the two stay in sync.
 */
#define DAOS_DEV_STATE_MAX_LEN      10

/**
 * Maximum number of interactive action choices per check report. The server
 * currently caps this at CHK_INTERACT_OPTION_MAX (src/chk/chk_internal.h);
 * this value must be >= the server cap. check.go truncates extra choices if
 * the server ever sends more than fit here.
 */
#define DAOS_CHECK_MAX_ACT_OPTIONS  4

/**
 * Storage device information.
 */
typedef struct device_list {
	uuid_t device_id;
	char   state[DAOS_DEV_STATE_MAX_LEN];
	int    rank;
	char   host[DAOS_HOSTNAME_MAX_LEN];
	int    tgtidx[DAOS_MAX_TARGETS_PER_DEVICE];
	int    n_tgtidx;
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
