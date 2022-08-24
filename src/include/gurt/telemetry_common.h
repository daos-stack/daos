/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __TELEMETRY_COMMON_H__
#define __TELEMETRY_COMMON_H__

#include <gurt/common.h>

#define D_TM_VERSION                   1
#define D_TM_MAX_NAME_LEN              256
#define D_TM_MAX_DESC_LEN              128
#define D_TM_MAX_UNIT_LEN              32
#define D_TM_TIME_BUFF_LEN             26

#define D_TM_SHARED_MEMORY_KEY         0x10242048
#define D_TM_SHARED_MEMORY_SIZE        (1024 * 1024)

/**
 * The following definitions are suggested strings for units that may be used
 * when explicitly calling d_tm_add_metric() to initialize a metric before use.
 */

/** Units of measure for time */
#define D_TM_YEAR                      "y"
#define D_TM_MONTH                     "mo"
#define D_TM_WEEK                      "w"
#define D_TM_DAY                       "d"
#define D_TM_HOUR                      "h"
#define D_TM_MINUTE                    "min"
#define D_TM_SECOND                    "s"
#define D_TM_MILLISECOND               "ms"
#define D_TM_MICROSECOND               "us"
#define D_TM_NANOSECOND                "ns"

/** Units of measure for storage */
#define D_TM_BIT                       "b"
#define D_TM_BYTE                      "B"

#define D_TM_KILOBIT                   "kbit"
#define D_TM_KIBIBIT                   "Kibit"
#define D_TM_KILOBYTE                  "kB"
#define D_TM_KIBIBYTE                  "KiB"

#define D_TM_MEGABIT                   "Mbit"
#define D_TM_MEBIBIT                   "Mibit"
#define D_TM_MEGABYTE                  "MB"
#define D_TM_MEBIBYTE                  "MiB"

#define D_TM_GIGABIT                   "Gbit"
#define D_TM_GIGIBIT                   "Gibit"
#define D_TM_GIGABYTE                  "GB"
#define D_TM_GIGIBYTE                  "GiB"

#define D_TM_TERABIT                   "Tbit"
#define D_TM_TEBIBIT                   "Tibit"
#define D_TM_TERABYTE                  "TB"
#define D_TM_TEBIBYTE                  "TiB"

#define D_TM_PETABIT                   "Pbit"
#define D_TM_PEBIBIT                   "Pibit"
#define D_TM_PETABYTE                  "PB"
#define D_TM_PEBIBYTE                  "PiB"

#define D_TM_EXABIT                    "Ebit"
#define D_TM_EXBIBIT                   "Eibit"
#define D_TM_EXABYTE                   "EB"
#define D_TM_EXBIBYTE                  "EiB"

#define D_TM_ZETTABIT                  "Zbit"
#define D_TM_ZEBIBIT                   "Zibit"
#define D_TM_ZETTABYTE                 "ZB"
#define D_TM_ZEBIBYTE                  "ZiB"

#define D_TM_YOTTABIT                  "Ybit"
#define D_TM_YOBIBIT                   "Yibit"
#define D_TM_YOTTABYTE                 "YB"
#define D_TM_YOBIBYTE                  "YiB"

/** Units of measure for data rates */
#define D_TM_BIT_PER_SECOND            D_TM_BIT "/s"
#define D_TM_BYTE_PER_SECOND           D_TM_BYTE "/s"

#define D_TM_KILOBIT_PER_SECOND        D_TM_KILOBIT "/s"
#define D_TM_KIBIBIT_PER_SECOND        D_TM_KIBIBIT "/s"
#define D_TM_KILOBYTE_PER_SECOND       D_TM_KILOBYTE "/s"
#define D_TM_KIBIBYTE_PER_SECOND       D_TM_KIBIBYTE "/s"

#define D_TM_MEGABIT_PER_SECOND        D_TM_MEGABIT "/s"
#define D_TM_MEBIBIT_PER_SECOND        D_TM_MEBIBIT "/s"
#define D_TM_MEGABYTE_PER_SECOND       D_TM_MEGABYTE "/s"
#define D_TM_MEBIBYTE_PER_SECOND       D_TM_MEBIBYTE "/s"

#define D_TM_GIGABIT_PER_SECOND        D_TM_GIGABIT "/s"
#define D_TM_GIGIBIT_PER_SECOND        D_TM_GIGIBIT "/s"
#define D_TM_GIGABYTE_PER_SECOND       D_TM_GIGABYTE "/s"
#define D_TM_GIGIBYTE_PER_SECOND       D_TM_GIGIBYTE "/s"

#define D_TM_TERABIT_PER_SECOND        D_TM_TERABIT "/s"
#define D_TM_TEBIBIT_PER_SECOND        D_TM_TEBIBIT "/s"
#define D_TM_TERABYTE_PER_SECOND       D_TM_TERABYTE "/s"
#define D_TM_TEBIBYTE_PER_SECOND       D_TM_TEBIBYTE "/s"

#define D_TM_PETABIT_PER_SECOND        D_TM_PETABIT "/s"
#define D_TM_PEBIBIT_PER_SECOND        D_TM_PEBIBIT "/s"
#define D_TM_PETABYTE_PER_SECOND       D_TM_PETABYTE "/s"
#define D_TM_PEBIBYTE_PER_SECOND       D_TM_PEBIBYTE "/s"

#define D_TM_EXABIT_PER_SECOND         D_TM_EXABIT "/s"
#define D_TM_EXBIBIT_PER_SECOND        D_TM_EXBIBIT "s"
#define D_TM_EXABYTE_PER_SECOND        D_TM_EXABYTE "/s"
#define D_TM_EXBIBYTE_PER_SECOND       D_TM_EXBIBYTE "/s"

#define D_TM_ZETTABIT_PER_SECOND       D_TM_ZETTABIT "/s"
#define D_TM_ZEBIBIT_PER_SECOND        D_TM_ZEBIBIT "/s"
#define D_TM_ZETTABYTE_PER_SECOND      D_TM_ZETTABYTE "/s"
#define D_TM_ZEBIBYTE_PER_SECOND       D_TM_ZEBIBYTE "/s"

#define D_TM_YOTTABIT_PER_SECOND       D_TM_YOTTABIT "/s"
#define D_TM_YOBIBIT_PER_SECOND        D_TM_YOBIBIT "/s"
#define D_TM_YOTTABYTE_PER_SECOND      D_TM_YOTTABYTE "/s"
#define D_TM_YOBIBYTE_PER_SECOND       D_TM_YOBIBYTE "/s"

#define D_TM_CLOCK_REALTIME_STR        "CLOCK_REALTIME"
#define D_TM_CLOCK_PROCESS_CPUTIME_STR "CLOCK_PROCESS_CPUTIME_ID"
#define D_TM_CLOCK_THREAD_CPUTIME_STR  "CLOCK_THREAD_CPUTIME_ID"

/** d_tm_metric_types */
enum {
	D_TM_DIRECTORY             = 0x001,
	D_TM_COUNTER               = 0x002,
	D_TM_TIMESTAMP             = 0x004,
	D_TM_TIMER_SNAPSHOT        = 0x008,
	D_TM_DURATION              = 0x010,
	D_TM_GAUGE                 = 0x020,
	D_TM_STATS_GAUGE           = 0x040,
	D_TM_CLOCK_REALTIME        = 0x080,
	D_TM_CLOCK_PROCESS_CPUTIME = 0x100,
	D_TM_CLOCK_THREAD_CPUTIME  = 0x200,
	D_TM_LINK                  = 0x400,
	D_TM_ALL_NODES = (D_TM_DIRECTORY | D_TM_COUNTER | D_TM_TIMESTAMP | D_TM_TIMER_SNAPSHOT |
			  D_TM_DURATION | D_TM_GAUGE | D_TM_STATS_GAUGE | D_TM_LINK)
};

enum {
	D_TM_SERVER_PROCESS = 0x000,
	D_TM_SERIALIZATION  = 0x001,
	D_TM_RETAIN_SHMEM   = 0x002,
};

/** Output formats */
enum {
	D_TM_STANDARD = 0x001,
	D_TM_CSV      = 0x002,
};

/** Optional field descriptors */
enum {
	D_TM_INCLUDE_TIMESTAMP = 0x001,
	D_TM_INCLUDE_METADATA  = 0x002,
	D_TM_INCLUDE_TYPE      = 0x004,
};

/**
 * @brief Statistics for gauge and duration metrics
 *
 * Stores the computed min, max, sum, standard deviation, mean, sum of squares
 * and sample size.
 */
struct d_tm_stats_t {
	uint64_t dtm_min;
	uint64_t dtm_max;
	uint64_t dtm_sum;
	double   std_dev;
	double   mean;
	double   sum_of_squares;
	uint64_t sample_size;
};

struct d_tm_bucket_t {
	uint64_t            dtb_min;
	uint64_t            dtb_max;
	struct d_tm_node_t *dtb_bucket;
};

struct d_tm_histogram_t {
	struct d_tm_bucket_t *dth_buckets;
	int                   dth_num_buckets;
	int                   dth_initial_width;
	int                   dth_value_multiplier;
};

struct d_tm_metric_t {
	union data {
		uint64_t        value;
		struct timespec tms[2];
	} dtm_data;
	struct d_tm_stats_t     *dtm_stats;
	struct d_tm_histogram_t *dtm_histogram;
	char                    *dtm_desc;
	char                    *dtm_units;
};

struct d_tm_node_t {
	struct d_tm_node_t   *dtn_child;     /** first child */
	struct d_tm_node_t   *dtn_sibling;   /** first sibling */
	char                 *dtn_name;      /** metric name */
	int                   dtn_type;      /** mask of D_TM_ types */
	key_t                 dtn_shmem_key; /** shmem region key */
	pthread_mutex_t       dtn_lock;      /** individual mutex */
	struct d_tm_metric_t *dtn_metric;    /** values */
	bool                  dtn_protect;   /** synchronized access */
};

struct d_tm_nodeList_t {
	struct d_tm_node_t     *dtnl_node;
	struct d_tm_nodeList_t *dtnl_next;
};

/** Context for a telemetry instance */
struct d_tm_context;

key_t
d_tm_get_srv_key(int srv_idx);
struct d_tm_node_t *
d_tm_follow_link(struct d_tm_context *ctx, struct d_tm_node_t *link);
int
d_tm_list_add_node(struct d_tm_node_t *src, struct d_tm_nodeList_t **nodelist);
void
d_tm_list_free(struct d_tm_nodeList_t *nodeList);
int
d_tm_get_version(void);
void
d_tm_compute_stats(struct d_tm_node_t *node, uint64_t value);
double
d_tm_compute_standard_dev(double sum_of_squares, uint64_t sample_size, double mean);
void
d_tm_compute_histogram(struct d_tm_node_t *node, uint64_t value);
void
d_tm_print_stats(FILE *stream, struct d_tm_stats_t *stats, int format);
#endif /* __TELEMETRY_COMMON_H__ */
