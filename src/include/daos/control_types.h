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
#include <uuid/uuid.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Maximum number of targets per device (matches BIO_XS_CNT_MAX).
 */
#define MAX_TEST_TARGETS_PER_DEVICE 48

/**
 * Maximum hostname length.
 */
#define DSS_HOSTNAME_MAX_LEN        255

/**
 * Storage device information.
 */
typedef struct device_list {
	uuid_t device_id;
	char   state[10];
	int    rank;
	char   host[DSS_HOSTNAME_MAX_LEN];
	int    tgtidx[MAX_TEST_TARGETS_PER_DEVICE];
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
	int      dcri_result;
	int      dcri_option_nr;
	int      dcri_options[4];
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
	int                            dci_pool_nr;
	int                            dci_report_nr;
	struct daos_check_pool_info   *dci_pools;
	struct daos_check_report_info *dci_reports;
};

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_CONTROL_TYPES_H__ */
