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
 * provided in Contract No. B620873.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/*
 * Types for blob I/O library.
 */

#ifndef __BIO_TYPES_H__
#define __BIO_TYPES_H__

#include <daos/common.h>
#include <daos/mem.h>

#define BIO_DEV_STR_LEN 128

typedef struct {
	/*
	 * Byte offset within PMDK pmemobj pool for SCM;
	 * Byte offset within SPDK blob for NVMe.
	 */
	uint64_t	ba_off;
	/* DAOS_MEDIA_SCM or DAOS_MEDIA_NVME */
	uint16_t	ba_type;
	/* Is the address a hole ? */
	uint16_t	ba_hole;
	uint16_t	ba_dedup;
	uint16_t	ba_padding;
} bio_addr_t;

/** Ensure this remains compatible */
D_CASSERT(sizeof(((bio_addr_t *)0)->ba_off) == sizeof(umem_off_t));

struct bio_iov {
	/*
	 * For SCM, it's direct memory address of 'ba_off';
	 * For NVMe, it's a DMA buffer allocated by SPDK malloc API.
	 */
	void		*bi_buf;
	/* Data length in bytes */
	size_t		 bi_data_len;
	bio_addr_t	 bi_addr;

	/** can be used to fetch more than actual address. Useful if more
	 * data is needed for processing (like checksums) than requested.
	 * Prefix and suffix are needed because 'extra' needed data might
	 * be before or after actual requested data.
	 */
	size_t		 bi_prefix_len; /** bytes before */
	size_t		 bi_suffix_len; /** bytes after */
};

struct bio_sglist {
	struct bio_iov	*bs_iovs;
	unsigned int	 bs_nr;
	unsigned int	 bs_nr_out;
};

/* Opaque I/O descriptor */
struct bio_desc;
/* Opaque I/O context */
struct bio_io_context;
/* Opaque per-xstream context */
struct bio_xs_context;
struct bio_blobstore;

/**
 * Header for SPDK blob per VOS pool
 */
struct bio_blob_hdr {
	uint32_t	bbh_magic;
	uint32_t	bbh_blk_sz;
	uint32_t	bbh_hdr_sz; /* blocks reserved for blob header */
	uint32_t	bbh_vos_id; /* service xstream id */
	uint64_t	bbh_blob_id;
	uuid_t		bbh_blobstore;
	uuid_t		bbh_pool;
};

/*
 * Current device health state (health statistics). Periodically updated in
 * bio_bs_monitor(). Used to determine faulty device status.
 */
struct bio_dev_state {
	char		 bds_model[BIO_DEV_STR_LEN];
	char		 bds_serial[BIO_DEV_STR_LEN];
	uint64_t	 bds_timestamp;
	uint64_t	 bds_error_count; /* error log page */
	/* Device health details */
	uint32_t	 bds_warn_temp_time;
	uint32_t	 bds_crit_temp_time;
	/* Support 128-bit values */
	uint64_t	 bds_ctrl_busy_time[2];
	uint64_t	 bds_power_cycles[2];
	uint64_t	 bds_power_on_hours[2];
	uint64_t	 bds_unsafe_shutdowns[2];
	uint64_t	 bds_media_errors[2];
	uint64_t	 bds_error_log_entries[2];
	/* I/O error counters */
	uint32_t	 bds_bio_read_errs;
	uint32_t	 bds_bio_write_errs;
	uint32_t	 bds_bio_unmap_errs;
	uint32_t	 bds_checksum_errs;
	uint16_t	 bds_temperature; /* in Kelvin */
	/* Critical warnings */
	uint8_t		 bds_temp_warning : 1;
	uint8_t		 bds_avail_spare_warning : 1;
	uint8_t		 bds_dev_reliabilty_warning : 1;
	uint8_t		 bds_read_only_warning : 1;
	uint8_t		 bds_volatile_mem_warning: 1; /*volatile memory backup*/
};
#endif /* __BIO_TYPES_H__ */
