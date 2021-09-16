/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * Primitives to share between data and control planes.
 */

#ifndef __CONTROL_H__
#define __CONTROL_H__

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/*
 * Space separated string of CLI options to pass to DPDK when started during
 * spdk_env_init(). These options will override the DPDK defaults.
 */
extern const char *
dpdk_cli_override_opts;

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

/*
 * Current device health state (health statistics). Periodically updated in
 * bio_bs_monitor(). Used to determine faulty device status.
 * Also retrieved on request via go-spdk bindings from the control-plane.
 */
struct nvme_stats {
	uint64_t	 timestamp;
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
	/* Intel vendor unique SMART attributes */
	/* normalized value, percent remaining of allowable program fails */
	uint8_t     program_fail_cnt_norm;
	/* current raw value, total count of program fails */
	uint64_t    program_fail_cnt_raw;
	uint8_t     erase_fail_cnt_norm;    /* erase fail count */
	uint64_t    erase_fail_cnt_raw;
	uint8_t     wear_leveling_cnt_norm; /* wear leveling count */
	uint16_t    wear_leveling_cnt_min;
	uint16_t    wear_leveling_cnt_max;
	uint16_t    wear_leveling_cnt_avg;
	uint64_t    endtoend_err_cnt_raw; /* end-to-end error count */
	uint64_t    crc_err_cnt_raw;      /* CRC error count */
	uint64_t    media_wear_raw;       /* timed workload, media wear */
	uint64_t    host_reads_raw;       /* timed workload, host reads */
	uint64_t    workload_timer_raw;   /* timed workload, timer */
	uint8_t	    thermal_throttle_status; /* thermal throttle status */
	uint64_t    thermal_throttle_event_cnt;
	uint64_t    retry_buffer_overflow_cnt; /* Retry Buffer overflow count */
	uint64_t    pll_lock_loss_cnt;	  /* PCIe Refclock PLL unlocj count */
	uint64_t    nand_bytes_written;	/* NAND bytes written, 1count=32MiB) */
	uint64_t    host_bytes_written; /* Host bytes written, 1count=32MiB) */
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
