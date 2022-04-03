/**
 * (C) Copyright 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __PERF_INTERNAL_H__
#define __PERF_INTERNAL_H__

#include <getopt.h>
#include <daos/common.h>
#include <daos/dts.h>

#define RANK_ZERO	(0)
#define STRIDE_MIN	(4) /* Should be changed with updating NB places */

#define PF_DKEY_PREF	"blade"
#define PF_AKEY_PREF	"apple"

enum ts_op_type {
	TS_DO_UPDATE = 0,
	TS_DO_FETCH
};

struct pf_param {
	/* output performance */
	bool		pa_perf;
	/** Verbose output */
	bool		pa_verbose;
	/* no key reset, verification cannot work after enabling it */
	bool		pa_no_reset;
	/* # iterations of the test */
	int		pa_iteration;
	/* output parameter */
	double		pa_duration;
	/** Subset of objects to write */
	int		pa_obj_nr;
	/** Subset of dkeys to write */
	int		pa_dkey_nr;
	/** Subset of akeys to write */
	int		pa_akey_nr;
	/** Subset of recx to write */
	int		pa_recx_nr;
	union {
		/* private parameter for iteration */
		struct {
			/* nested iterator */
			bool	nested;
			/* visible iteration */
			bool	visible;
		} pa_iter;
		/* private parameter for update, fetch and verify */
		struct {
			/* offset within stride */
			int	offset;
			/* size of the I/O */
			int	size;
			/* verify the read */
			bool	verify;
			/* dkey flag */
			bool	dkey_flag;
		} pa_rw;
		struct {
			/* full scan */
			bool	full_scan;
			/* Force merge */
			bool	force_merge;
		} pa_agg;
	};
};

typedef int (*pf_update_or_fetch_fn_t)(int, enum ts_op_type,
				       struct io_credit *, daos_epoch_t,
				       bool, double *);
typedef int (*pf_parse_cb_t)(char *, struct pf_param *, char **);

struct pf_test {
	/* identifier of test */
	char	  ts_code;
	/* name of the test */
	char	 *ts_name;
	/* parse test parameters */
	int	(*ts_parse)(char *str, struct pf_param *param, char **strpp);
	/* main test function */
	int	(*ts_func)(struct pf_test *ts, struct pf_param *param);
};

extern daos_size_t	ts_scm_size;
extern daos_size_t	ts_nvme_size;

extern bool		ts_const_akey;
extern char		*ts_dkey_prefix;
extern unsigned int	ts_obj_p_cont;
extern unsigned int	ts_dkey_p_obj;
extern unsigned int	ts_akey_p_dkey;
extern unsigned int	ts_recx_p_akey;
extern unsigned int	ts_stride;
extern unsigned int	ts_seed;
extern bool		ts_single;
extern bool		ts_random;
extern bool		ts_pause;

extern bool		ts_oid_init;

extern daos_handle_t	*ts_ohs;
extern daos_obj_id_t	*ts_oids;
extern daos_key_t	*ts_dkeys;
extern uint64_t		*ts_indices;

extern struct credit_context	ts_ctx;
extern pf_update_or_fetch_fn_t	ts_update_or_fetch_fn;

#define TS_TIME_START(time, start)		\
do {						\
	if (time == NULL)			\
		break;				\
	start = daos_get_ntime();		\
} while (0)

#define TS_TIME_END(time, start)		\
do {						\
	if ((time) == NULL)			\
		break;				\
	*time += (daos_get_ntime() - start)/1000;\
} while (0)

static inline bool
val_has_unit(char c)
{
	return c == 'k' || c == 'K' || c == 'm' ||
	       c == 'M' || c == 'g' || c == 'G';

}

static inline uint64_t
val_unit(uint64_t val, char unit)
{
	switch (unit) {
	default:
		return val;
	case 'k':
	case 'K':
		val *= 1024;
		return val;
	case 'm':
	case 'M':
		val *= 1024 * 1024;
		return val;
	case 'g':
	case 'G':
		val *= 1024 * 1024 * 1024;
		return val;
	}
}

static inline const char *
pf_class2name(int obj_class)
{
	switch (obj_class) {
	default:
		return "unknown";
	case DAOS_OC_ECHO_TINY_RW:
		return "ECHO TINY (network only, non-replica)";
	case DAOS_OC_ECHO_R2S_RW:
		return "ECHO R2S (network only, 2-replica)";
	case DAOS_OC_ECHO_R3S_RW:
		return "ECHO R3S (network only, 3-replica)";
	case DAOS_OC_ECHO_R4S_RW:
		return "ECHO R4S (network only, 4-replica)";
	case OC_S1:
		return "DAOS TINY (full stack, non-replica)";
	case OC_SX:
		return "DAOS LARGE (full stack, non-replica)";
	case OC_RP_2G1:
		return "DAOS R2S (full stack, 2 replica)";
	case OC_RP_3G1:
		return "DAOS R3S (full stack, 3 replica)";
	case OC_RP_4G1:
		return "DAOS R4S (full stack, 4 replics)";
	case OC_EC_2P2G1:
		return "DAOS OC_EC_2P2G1 (full stack 2+2 EC)";
	case OC_EC_4P2G1:
		return "DAOS OC_EC_4P2G1 (full stack 4+2 EC)";
	case OC_EC_8P2G1:
		return "DAOS OC_EC_8P2G1 (full stack 8+2 EC)";
	}
}

static inline const char *
ts_val_type(void)
{
	return ts_single ? "single" : "array";
}

void
stride_buf_init(int size);
void
stride_buf_fini(void);
int
objects_update(struct pf_param *param);
int
objects_fetch(struct pf_param *param);
int
pf_parse_common(char *str, struct pf_param *param, pf_parse_cb_t parse_cb,
		char **strp);
int
pf_parse_rw(char *str, struct pf_param *param, char **strp);
int
run_commands(char *cmds, struct pf_test pf_tests[]);
void
show_result(struct pf_param *param, uint64_t start, uint64_t end,
	    char *test_name);
int
perf_parse_opts(int rc, char **cmds);
void
perf_free_opts(struct option *opts, char *optstr);
int
perf_alloc_opts(const struct option opts_in[], int opt_len,
		const char optstr_in[], struct option **opts_out,
		char **optstr_out);
extern const char perf_common_usage[];
void
perf_free_keys(void);
int
perf_alloc_keys(void);
void
perf_setup_keys(void);

/** Add extern for vos internal function */
void gc_wait(void);

#endif /* __PERF_INTERNAL_H__ */
