/**
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
* Primitives to share between data and control planes.
*/

#ifndef __CONTROL_H__
#define __CONTROL_H__

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <daos/common.h>

/**
* Space separated string of CLI options to pass to DPDK when started during
* spdk_env_init(). These options will override the DPDK defaults.
*/
extern const char *
dpdk_cli_override_opts;

/** Device state flags */
#define NVME_DEV_FL_PLUGGED	(1 << 0)	/* Device is present in slot */
#define NVME_DEV_FL_INUSE	(1 << 1)	/* Used by DAOS (present in SMD) */
#define NVME_DEV_FL_FAULTY	(1 << 2)	/* Faulty state has been assigned */
#define NVME_DEV_FL_INVALID	(1 << 3)	/* State combination is invalid */

/** Device state combinations */
#define NVME_DEV_STATE_NORMAL		(NVME_DEV_FL_PLUGGED | NVME_DEV_FL_INUSE)
#define NVME_DEV_STATE_NORMAL_FAULTY	(NVME_DEV_STATE_NORMAL | NVME_DEV_FL_FAULTY)
#define NVME_DEV_STATE_NEW		NVME_DEV_FL_PLUGGED
#define NVME_DEV_STATE_NEW_FAULTY	(NVME_DEV_STATE_NEW | NVME_DEV_FL_FAULTY)

/** VMD LED device states */
#define DAOS_LED_ST_OFF		0	/* SPDK_VMD_LED_STATE_OFF */
#define DAOS_LED_ST_IDENTIFY	1	/* SPDK_VMD_LED_STATE_IDENTIFY	(4kHz blink) */
#define DAOS_LED_ST_FAULT	2	/* SPDK_VMD_LED_STATE_FAULT	(solid on) */
#define DAOS_LED_ST_REBUILD	3	/* SPDK_VMD_LED_STATE_REBUILD	(1kHz blink) */
#define DAOS_LED_ST_UNKNOWN	4	/* SPDK_VMD_LED_STATE_UNKNOWN	(VMD not enabled) */

typedef enum {
	NONE = 0,
	ONE = 1,
} numbers;

/** VMD LED device actions */
#define DAOS_LED_ACT_GET	0	/* Get LED state */
#define DAOS_LED_ACT_SET	1	/* Set LED state */
#define DAOS_LED_ACT_RESET	2	/* Reset LED state */

/** Env defining the size of a metadata pmem pool/file in MiBs */
#define DAOS_MD_CAP_ENV			"DAOS_MD_CAP"
/** Default size of a metadata pmem pool/file (128 MiB) */
#define DEFAULT_DAOS_MD_CAP_SIZE	(1ul << 27)

/** Utility macros */
#define CHK_FLAG(x, m) ((x & m) == m)
#define SET_FLAG(x, m) (x |= m)
#define UNSET_FLAG(x, m) (x &= ~(m))
#define STR_EQ(x, m) (strcmp(x, m) == 0)

/** NVMe config keys */
#define NVME_CONF_ATTACH_CONTROLLER	"bdev_nvme_attach_controller"
#define NVME_CONF_ENABLE_VMD		"enable_vmd"
#define NVME_CONF_SET_HOTPLUG_RANGE	"hotplug_busid_range"
#define NVME_CONF_SET_ACCEL_PROPS	"accel_props"

/** Supported acceleration engine settings */
#define NVME_ACCEL_NONE		"none"
#define NVME_ACCEL_SPDK		"spdk"
#define NVME_ACCEL_DML		"dml"

/** Acceleration engine optional capabilities */
#define NVME_ACCEL_FLAG_MOVE	(1 << 0)
#define NVME_ACCEL_FLAG_CRC	(1 << 1)

static inline char *
nvme_state2str(int state)
{
	if (state >= NVME_DEV_FL_INVALID)
		return "INVALID";
	if (!CHK_FLAG(state, NVME_DEV_FL_PLUGGED))
		return "UNPLUGGED";
	if CHK_FLAG(state, NVME_DEV_FL_FAULTY)
		return "EVICTED";
	if (!CHK_FLAG(state, NVME_DEV_FL_INUSE))
		return "NEW";
	return "NORMAL";
}

static inline int
nvme_str2state(char *state)
{
	if STR_EQ(state, "NORMAL")
		return NVME_DEV_STATE_NORMAL;
	if STR_EQ(state, "NEW")
		return NVME_DEV_STATE_NEW;
	if STR_EQ(state, "EVICTED")
		return NVME_DEV_STATE_NORMAL_FAULTY;
	if STR_EQ(state, "UNPLUGGED")
		return 0;
	return NVME_DEV_FL_INVALID;
}

//static inline char *
//led_state2str(int state)
//{
//	/** VMD LED states */
//	if (BIT_SET(state, VMD_LED_ST_OFF))
//		return "OFF";
//
//	if (BIT_SET(state, VMD_LED_ST_IDENTIFY))
//		return "QUICK-BLINK";
//
//	if (BIT_SET(state, VMD_LED_ST_FAULT))
//		return "ON";
//
//	if (BIT_SET(state, VMD_LED_ST_INVALID))
//		return "INVALID";
//
//	/* LED not supported, not a VMD device */
//	return "NA";
//}
//
//static inline int
//led_str2state(char *state)
//{
//	/** VMD LED states */
//	if STR_EQ(state, "OFF")
//		return VMD_LED_ST_OFF;
//	if STR_EQ(state, "QUICK-BLINK")
//		return VMD_LED_ST_IDENTIFY;
//	if STR_EQ(state, "ON")
//		return VMD_LED_ST_FAULT;
//	if STR_EQ(state, "INVALID")
//		return VMD_LED_ST_INVALID;
//
//	/** not a valid state */
//	return VMD_LED_ST_NA;
//}

/**
 * Current device health state (health statistics). Periodically updated in
 * bio_bs_monitor(). Used to determine faulty device status.
 * Also retrieved on request via go-spdk bindings from the control-plane.
 */
struct nvme_stats {
	uint64_t	 timestamp;
	/* Device space utilization */
	uint64_t	 total_bytes;
	uint64_t	 avail_bytes;
	uint64_t	 cluster_size;
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
#endif /** __CONTROL_H_ */
