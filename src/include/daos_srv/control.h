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
 * Primitives to share between data and control planes.
 */

#ifndef __CONTROL_H__
#define __CONTROL_H__

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

enum {
	/* Device is plugged */
	NVME_DEV_FL_PLUGGED	= 0x1,
	/* Device is used by DAOS (present in SMD) */
	NVME_DEV_FL_INUSE	= 0x2,
	/* Device is marked as FAULTY */
	NVME_DEV_FL_FAULTY	= 0x4,
};

enum bio_dev_state {
	/* fully functional and in-use */
	BIO_DEV_NORMAL	= 0,
	/* evicted device */
	BIO_DEV_FAULTY,
	/* unplugged device */
	BIO_DEV_OUT,
	/* new device not currently in-use */
	BIO_DEV_NEW,
};

/*
 * Convert device state to human-readable string
 *
 * \param [IN]  state   Device state
 *
 * \return              Static string representing enum value
 */
static inline char *
bio_dev_state_enum_to_str(enum bio_dev_state state)
{
	switch (state) {
	case BIO_DEV_NORMAL: return "NORMAL";
	case BIO_DEV_FAULTY: return "EVICTED";
	case BIO_DEV_OUT:    return "UNPLUGGED";
	case BIO_DEV_NEW:    return "NEW";
	}

	return "Undefined state";
}

#define HEALTH_STAT_STR_LEN 128

/*
 * Current device health state (health statistics). Periodically updated in
 * bio_bs_monitor(). Used to determine faulty device status.
 * Also retrieved on request via go-spdk bindings from the control-plane.
 */
struct nvme_stats {
	uint64_t	 timestamp;
	/* Device identifiers */
	char		 model[HEALTH_STAT_STR_LEN];
	char		 serial[HEALTH_STAT_STR_LEN];
	/* Device space utilization */
	uint64_t	 total_bytes;
	uint64_t	 avail_bytes;
	/* Device health details */
	uint32_t	 warn_temp_time;
	uint32_t	 crit_temp_time;
	uint64_t	 ctrl_busy_time;
	uint64_t	 power_cycles;
	uint64_t	 power_on_hours;
	uint64_t	 unsafe_shutdowns;
	uint64_t	 media_errs;
	uint64_t	 err_log_entries;
	/* I/O error counters */
	uint32_t	 bio_read_errs;
	uint32_t	 bio_write_errs;
	uint32_t	 bio_unmap_errs;
	uint32_t	 checksum_errs;
	uint16_t	 temperature; /* in Kelvin */
	/* Critical warnings */
	bool		 temp_warn;
	bool		 avail_spare_warn;
	bool		 dev_reliability_warn;
	bool		 read_only_warn;
	bool		 volatile_mem_warn; /*volatile memory backup*/
};

/**
 * Parse input string and output ASCII as required by the NVMe spec.
 *
 * \param[out] dst	pre-allocated destination string buffer
 * \param[in]  dst_sz	destination buffer size
 * \param[in]  src	source buffer containing char array
 * \param[in]  src_sz	source buffer size
 *
 * \return		Zero on success, negative value on error
 */
int copy_ascii(char *dst, size_t dst_sz, const void *src, size_t src_sz);
#endif /* __CONTROL_H_ */
