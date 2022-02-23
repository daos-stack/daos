/**
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/* daos_autotest.c - smoke tests against an existing pool to verify installation
 * invoked by daos(8) utility
 */

#define D_LOGFAC	DD_FAC(client)

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include <daos.h>
#include <daos/common.h>
#include <daos/placement.h>
#include <daos/pool.h>
#include <daos/kv.h>

#include "daos_hdlr.h"
#include "math.h"

/** Input arguments passed to daos utility */
struct cmd_args_s *autotest_ap;

/** How many concurrent I/O in flight */
#define MAX_INFLIGHT 16

/** Step timing  */
clock_t	start;
clock_t	end;

/** generated container UUID */
const char	*cuuid;

/** generated label for aux containers */
const char	*cuuid2;
const char	*cuuid3;

/** initial object ID */
uint64_t	oid_hi = 1;
daos_obj_id_t	oid = { .hi = 1, .lo = 1 }; /** object ID */
daos_obj_id_t	oid2 = { .hi = 1, .lo = 1 }; /** object ID */
daos_obj_id_t	oid3 = { .hi = 1, .lo = 1 }; /** object ID */

/** pool handle */
daos_handle_t	poh = DAOS_HDL_INVAL;
/** container handle */
daos_handle_t	coh = DAOS_HDL_INVAL;
daos_handle_t	coh2 = DAOS_HDL_INVAL;
daos_handle_t	coh3 = DAOS_HDL_INVAL;

/** force cleanup */
int force;

/** skip big steps */
int skip_steps[] = {28, 29};

/** deadline/time limit */
uint64_t	deadline_count;
clock_t		deadline_limit = 30 * CLOCKS_PER_SEC;

int domain_nr;

/** total number of records for progress bar */
int total_nr;

/** how many ticks in progress bar */
int ticks;

/** how big each tick is in progress bar */
int tick_size;

void
setup_progress()
{
	ticks = 20;
	tick_size = total_nr / ticks;
	fprintf(autotest_ap->outstream, "     ");
}

void
increment_progress(int progress)
{
	if (progress % tick_size == 0) {
		int percentage;

		percentage = ceil((double) progress / (double) total_nr * 100.0);
		fprintf(autotest_ap->outstream, "\b\b\b\b\b");
		fprintf(autotest_ap->outstream, "% 4d%%", percentage);
		fflush(autotest_ap->outstream);
	}
}

void
finish_progress()
{
	fprintf(autotest_ap->outstream, "\b\b\b\b\b");
}


static inline void
new_oid(void)
{
	oid.hi = ++oid_hi;
	oid.lo = 1;
}

static inline void
new_oid2(void)
{
	oid2.hi = ++oid_hi;
	oid2.lo = 1;
}

static inline void
new_oid3(void)
{
	oid3.hi = ++oid_hi;
	oid3.lo = 1;
}

static inline float
duration(void)
{
	return (float) (end - start) * 1E-6;
}

static inline void
step_print(const char *status, const char *comment, va_list ap)
{
	char	timing[8];
	int	i;

	end = clock();
	fprintf(autotest_ap->outstream, "  %s  ", status);
	sprintf(timing, "%03.3f", duration());
	for (i = strlen(timing); i < 7; i++)
		fprintf(autotest_ap->outstream, " ");
	fprintf(autotest_ap->outstream, "%s  ", timing);
	vfprintf(autotest_ap->outstream, comment, ap);
	fprintf(autotest_ap->outstream, "\n");
}

static inline void
step_success(const char *comment, ...)
{
	va_list	ap;

	va_start(ap, comment);
	step_print("\033[0;32mPASS\033[0m", comment, ap);
	va_end(ap);
}

static inline void
step_fail(const char *comment, ...)
{
	va_list	ap;

	va_start(ap, comment);
	step_print("\033[0;31mFAIL\033[0m", comment, ap);
	va_end(ap);
}

static inline void
step_skip(const char *comment, ...)
{
	va_list ap;

	va_start(ap, comment);
	step_print("\033[0;33mSKIP\033[0m", comment, ap);
	va_end(ap);
}
static inline void
step_new(int step, char *msg)
{
	int i;

	fprintf(autotest_ap->outstream, "%3d  %s", step, msg);
	for (i = strlen(msg); i < 25; i++)
		fprintf(autotest_ap->outstream, " ");
	start = clock();
}

static inline void
step_init(void)
{
	fprintf(autotest_ap->outstream,
		"\033[1;35mStep Operation                 ");
	fprintf(autotest_ap->outstream, "Status Time(sec) Comment\033[0m\n");
}

static int
init(void)
{
	int rc;

	rc = daos_init();
	if (rc) {
		step_fail(d_errdesc(rc));
		return rc;
	}
	step_success("");
	return 0;
}

static int
pconnect(void)
{
	int rc;

	/** Connect to pool */
	rc = daos_pool_connect(autotest_ap->pool_str, autotest_ap->sysname,
			       DAOS_PC_RW, &poh, NULL, NULL);
	if (rc) {
		step_fail(d_errdesc(rc));
		return rc;
	}

	/** gather domain_nr for poh */
	struct dc_pool		*pool;
	struct pl_map_attr	attr;

	pool = dc_hdl2pool(poh);
	D_ASSERT(pool);

	rc = pl_map_query(pool->dp_pool, &attr);
	D_ASSERT(rc == 0);
	dc_pool_put(pool);
	domain_nr = attr.pa_domain_nr;

	step_success("");
	return 0;
}

static int
ccreate(void)
{
	int rc;

	/** Create container */
	cuuid = "autotest_cont_def";
	rc = daos_cont_create_with_label(poh, cuuid, NULL, NULL, NULL);
	if (rc)
		D_GOTO(fail, rc);

	if (domain_nr < 2) {
		step_skip("Group size 2 is larger than domain_nr(%d)", domain_nr);
		return 0;
	}

	/** Create container with RF=1 */
	daos_prop_t	*prop;

	prop = daos_prop_alloc(1);
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	prop->dpp_entries[0].dpe_val = DAOS_PROP_CO_REDUN_RF1;

	cuuid2 = "autotest_cont_rf1";
	rc = daos_cont_create_with_label(poh, cuuid2, prop, NULL, NULL);
	daos_prop_free(prop);
	if (rc)
		D_GOTO(fail, rc);

	if (domain_nr < 3) {
		step_skip("Group size 3 is larger than domain_nr(%d)", domain_nr);
		return 0;
	}

	/** Create container with RF=2 */
	daos_prop_t	*prop2;

	prop2 = daos_prop_alloc(1);
	prop2->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	prop2->dpp_entries[0].dpe_val = DAOS_PROP_CO_REDUN_RF2;

	cuuid3 = "autotest_cont_rf2";
	rc = daos_cont_create_with_label(poh, cuuid3, prop2, NULL, NULL);
	daos_prop_free(prop2);
	if (rc)
		D_GOTO(fail, rc);

	step_success("");
	return 0;

fail:
	step_fail(d_errdesc(rc));
	return rc;
}

static int
copen(void)
{
	int rc;

	/** Open container */
	rc = daos_cont_open(poh, cuuid, DAOS_COO_RW, &coh, NULL, NULL);
	if (rc)
		D_GOTO(fail, rc);

	if (domain_nr >= 2) {
		rc = daos_cont_open(poh, cuuid2, DAOS_COO_RW, &coh2, NULL, NULL);
		if (rc)
			D_GOTO(fail, rc);
	}

	if (domain_nr >= 3) {
		rc = daos_cont_open(poh, cuuid3, DAOS_COO_RW, &coh3, NULL, NULL);
		if (rc)
			D_GOTO(fail, rc);
	}

	step_success("");
	return 0;

fail:
	step_fail(d_errdesc(rc));
	return rc;
}

static int
oS1(void)
{
	daos_handle_t	oh = DAOS_HDL_INVAL; /** object handle */
	int		i;
	int		rc;

	new_oid();
	daos_obj_generate_oid(coh, &oid, 0, 0, 0, 0);

	total_nr = 1000000;
	setup_progress();

	for (i = 0; i < total_nr; i++) {

		rc = daos_obj_open(coh, oid, DAOS_OO_RO, &oh, NULL);
		if (rc) {
			step_fail("failed to open object: %s", d_errdesc(rc));
			return rc;
		}

		rc = daos_obj_close(oh, NULL);
		if (rc) {
			step_fail("failed to close object: %s", d_errdesc(rc));
			return rc;
		}
		increment_progress(i);
	}

	finish_progress();
	step_success("");
	return 0;
}

static int
oSX(void)
{
	daos_handle_t	oh = DAOS_HDL_INVAL; /** object handle */
	int		i;
	int		rc;

	new_oid();
	daos_obj_generate_oid(coh, &oid, 0, 0, 0, 0);

	total_nr = 10000;
	setup_progress();

	for (i = 0; i < total_nr; i++) {

		rc = daos_obj_open(coh, oid, DAOS_OO_RO, &oh, NULL);
		if (rc) {
			step_fail("failed to open object: %s", d_errdesc(rc));
			return rc;
		}

		rc = daos_obj_close(oh, NULL);
		if (rc) {
			step_fail("failed to close object: %s", d_errdesc(rc));
			return rc;
		}
		increment_progress(i);
	}

	finish_progress();
	step_success("");
	return 0;
}

static int pool_space_usage_ratio(void)
{
	int rc;
	daos_pool_info_t pinfo = {0};
	struct daos_pool_space *ps = &pinfo.pi_space;

	pinfo.pi_bits = DPI_ALL;
	rc = daos_pool_query(poh, NULL, &pinfo, NULL, NULL);
	if (rc)
		return rc;

	if (ps->ps_space.s_total[DAOS_MEDIA_NVME] > 0)
		return 100 - (ps->ps_space.s_free[DAOS_MEDIA_NVME] * 100 /
			      ps->ps_space.s_total[DAOS_MEDIA_NVME]);

	return 100 - (ps->ps_space.s_free[DAOS_MEDIA_SCM] * 100 /
		      ps->ps_space.s_total[DAOS_MEDIA_SCM]);
}

static int
kv_put(daos_handle_t oh, daos_size_t size)
{
	daos_handle_t	eq;
	daos_event_t	ev_array[MAX_INFLIGHT];
	char		key[MAX_INFLIGHT][10];
	char		*val;
	daos_event_t	*evp;
	int		rc, usage_ratio1, usage_ratio2;
	int		eq_rc;
	clock_t		last_query = start, current;

	deadline_count = 1;

	total_nr = deadline_limit / CLOCKS_PER_SEC;
	setup_progress();

	usage_ratio1 = pool_space_usage_ratio();
	if (usage_ratio1 < 0)
		return usage_ratio1;

	/** Create event queue to manage asynchronous I/Os */
	rc = daos_eq_create(&eq);
	if (rc)
		return rc;

	/** allocate buffer to store value */
	D_ALLOC(val, size * MAX_INFLIGHT);
	if (val == NULL) {
		rc = daos_eq_destroy(eq, 0);
		return -DER_NOMEM;
	}
	memset(val, 'D', size * MAX_INFLIGHT);

	/** Issue actual I/Os */
	while (true) {
		char *key_cur;
		char *val_cur;

		if (deadline_count < MAX_INFLIGHT) {
			/** Haven't reached max request in flight yet */
			evp = &ev_array[deadline_count];
			rc = daos_event_init(evp, eq, NULL);
			if (rc)
				break;
			key_cur = key[deadline_count];
			val_cur = val + size * (deadline_count);
		} else {
			int slot;

			/**
			 * Max request in flight reached, wait for one i/o to
			 * complete to reuse the slot
			 */
			rc = daos_eq_poll(eq, 1, DAOS_EQ_WAIT, 1, &evp);
			if (rc < 0)
				break;
			if (rc == 0) {
				rc = -DER_IO;
				break;
			}

			/** Check if completed operation failed */
			if (evp->ev_error != DER_SUCCESS) {
				rc = evp->ev_error;
				break;
			}
			evp->ev_error = 0;

			/* reuse slot */
			slot = evp - ev_array;
			key_cur = key[slot];
			val_cur = val + slot * size;
		}

		/** key = insert sequence ID */
		sprintf(key_cur, "%ld", deadline_count);
		/** value = sequend ID + DDDDDDD... */
		*((uint64_t *)val_cur) = deadline_count;

		/** Insert kv pair */
		rc = daos_kv_put(oh, DAOS_TX_NONE, 0, key_cur, size, val_cur,
				evp);

		/*
		 * We are limited by writing 1/10th of the
		 * available free space or 30s.
		 */
		current = clock();
		if (start + deadline_limit <= current)
			break;

		if (last_query + CLOCKS_PER_SEC < current) {
			increment_progress((current - start) / CLOCKS_PER_SEC);
			last_query = current;
			usage_ratio2 = pool_space_usage_ratio();
			if (usage_ratio2 < 0) {
				rc = usage_ratio2;
				break;
			}
			if ((usage_ratio2 - usage_ratio1) >=
			    (100 - usage_ratio1) / 10)
				break;
		}

		if (rc)
			break;

		deadline_count++;

	}

	/** Wait for completion of all in-flight requests */
	do {
		eq_rc = daos_eq_poll(eq, 1, DAOS_EQ_WAIT, 1, &evp);
		if (rc == 0 && eq_rc == 1) {
			rc = evp->ev_error;
		}
	} while (eq_rc == 1);

	if (rc == 0 && eq_rc < 0) {
		rc = eq_rc;
	}

	D_FREE(val);

	/** Destroy event queue */
	eq_rc = daos_eq_destroy(eq, 0);
	if (eq_rc) {
		if (rc == 0)
			rc = eq_rc;
	}

	finish_progress();
	return rc;
}

static int
kv_get(daos_handle_t oh, daos_size_t size)
{
	daos_handle_t	eq;
	daos_event_t	ev_array[MAX_INFLIGHT];
	char		key[MAX_INFLIGHT][10];
	daos_size_t	val_sz[MAX_INFLIGHT];
	char		*val;
	daos_event_t	*evp;
	uint64_t	i;
	uint64_t	res = 0;
	int		rc;
	int		eq_rc;

	total_nr = deadline_count;
	setup_progress();

	/** Create event queue to manage asynchronous I/Os */
	rc = daos_eq_create(&eq);
	if (rc)
		return rc;

	/** allocate buffer to store value */
	D_ALLOC(val, size * MAX_INFLIGHT);
	if (val == NULL) {
		eq_rc = daos_eq_destroy(eq, 0);
		return -DER_NOMEM;
	}

	/** Issue actual I/Os */
	for (i = 1; i < deadline_count + 1; i++) {
		char		*key_cur;
		char		*val_cur;
		daos_size_t	*val_sz_cur;

		if (i < MAX_INFLIGHT) {
			/** Haven't reached max request in flight yet */
			evp = &ev_array[i];
			rc = daos_event_init(evp, eq, NULL);
			if (rc)
				break;

			key_cur = key[i];
			val_cur = val + size * i;
			val_sz_cur = &val_sz[i];
		} else {
			int slot;

			/**
			 * Max request in flight reached, wait for one i/o to
			 * complete to reuse the slot
			 */
			rc = daos_eq_poll(eq, 1, DAOS_EQ_WAIT, 1, &evp);
			if (rc < 0)
				break;
			if (rc == 0) {
				rc = -DER_IO;
				break;
			}

			/** Check if completed operation failed */
			if (evp->ev_error != DER_SUCCESS) {
				rc = evp->ev_error;
				break;
			}
			evp->ev_error = 0;

			/* reuse slot */
			slot = evp - ev_array;
			key_cur = key[slot];
			val_cur = val + slot * size;
			val_sz_cur = &val_sz[slot];

			if (*val_sz_cur != size) {
				rc = -DER_MISMATCH;
				break;
			}

			/** to verify result */
			res += *((uint64_t *)val_cur);
		}

		/** key = insert sequence ID */
		sprintf(key_cur, "%ld", i);
		/** clear buffer */
		memset(val_cur, 0, size);
		*val_sz_cur = size;

		/** Insert kv pair */
		rc = daos_kv_get(oh, DAOS_TX_NONE, 0, key_cur, val_sz_cur,
				val_cur, evp);
		if (rc)
			break;
		increment_progress(i);
	}

	/** Wait for completion of all in-flight requests */
	do {
		eq_rc = daos_eq_poll(eq, 1, DAOS_EQ_WAIT, 1, &evp);
		if (rc == 0 && eq_rc == 1) {
			rc = evp->ev_error;
			if (rc == 0) {
				int slot = evp - ev_array;

				if (val_sz[slot] != size) {
					rc = -DER_MISMATCH;
				} else {
					res += *((uint64_t *)(val + slot * size));
				}
			}
		}
	} while (eq_rc == 1);

	if (rc == 0 && eq_rc < 0) {
		rc = eq_rc;
	}

	D_FREE(val);

	/** Destroy event queue */
	eq_rc = daos_eq_destroy(eq, 0);
	if (eq_rc) {
		if (rc)
			return rc;
		else
			return eq_rc;
	}

	/** verify that we got the sum of all integers from 1 to deadline_count */
	if (res != deadline_count * (deadline_count + 1) / 2)
		rc = -DER_MISMATCH;

	finish_progress();
	return rc;
}

static int
kv_insert128(void)
{
	daos_handle_t	oh = DAOS_HDL_INVAL; /** object handle */
	int		put_rc;
	int		rc;

	new_oid();
	daos_obj_generate_oid(coh, &oid, DAOS_OT_KV_HASHED, 0, 0, 0);

	rc = daos_kv_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	if (rc) {
		step_fail("failed to open object: %s", d_errdesc(rc));
		return rc;
	}

	put_rc = kv_put(oh, 128);
	rc = daos_kv_close(oh, NULL);

	if (put_rc) {
		step_fail("failed to insert: %s", d_errdesc(put_rc));
		return put_rc;
	}

	if (rc) {
		step_fail("failed to close object: %s", d_errdesc(rc));
		return rc;
	}

	step_success("");
	return 0;
}

static int
kv_read128(void)
{
	daos_handle_t	oh = DAOS_HDL_INVAL; /** object handle */
	int		get_rc;
	int		rc;

	rc = daos_kv_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	if (rc) {
		step_fail("failed to open object: %s", d_errdesc(rc));
		return rc;
	}

	get_rc = kv_get(oh, 128);
	rc = daos_kv_close(oh, NULL);

	if (get_rc) {
		step_fail("failed to read: %s", d_errdesc(get_rc));
		return get_rc;
	}

	if (rc) {
		step_fail("failed to close object: %s", d_errdesc(rc));
		return rc;
	}

	step_success("");
	return 0;
}

static int
kv_punch(void)
{
	daos_handle_t	kv_oh = DAOS_HDL_INVAL; /** kv object handle */
	daos_handle_t	oh;
	int		punch_rc;
	int		rc;

	rc = daos_kv_open(coh, oid, DAOS_OO_RW, &kv_oh, NULL);
	if (rc) {
		step_fail("failed to open object: %s", d_errdesc(rc));
		return rc;
	}

	oh = daos_kv2objhandle(kv_oh);
	if (!daos_handle_is_valid(oh)) {
		rc = daos_kv_close(kv_oh, NULL);
		return -DER_INVAL;
	}

	punch_rc = daos_obj_punch(daos_kv2objhandle(kv_oh), DAOS_TX_NONE,
				  0, NULL);
	rc = daos_kv_close(kv_oh, NULL);

	if (punch_rc) {
		step_fail("failed to punch object: %s", d_errdesc(punch_rc));
		return punch_rc;
	}

	if (rc) {
		step_fail("failed to close object: %s", d_errdesc(rc));
		return rc;
	}

	step_success("");
	return 0;
}

static int
kv_insert4k(void)
{
	daos_handle_t	oh = DAOS_HDL_INVAL; /** object handle */
	int		put_rc;
	int		rc;

	new_oid();
	daos_obj_generate_oid(coh, &oid, DAOS_OT_KV_HASHED, 0, 0, 0);

	rc = daos_kv_open(coh, oid, DAOS_OO_RO, &oh, NULL);
	if (rc) {
		step_fail("failed to open object: %s", d_errdesc(rc));
		return rc;
	}

	put_rc = kv_put(oh, 4096);
	rc = daos_kv_close(oh, NULL);

	if (put_rc) {
		step_fail("failed to insert: %s", d_errdesc(put_rc));
		return put_rc;
	}

	if (rc) {
		step_fail("failed to close object: %s", d_errdesc(rc));
		return rc;
	}

	step_success("");
	return 0;
}

static int
kv_read4k(void)
{
	daos_handle_t	oh = DAOS_HDL_INVAL; /** object handle */
	int		get_rc;
	int		rc;

	rc = daos_kv_open(coh, oid, DAOS_OO_RO, &oh, NULL);
	if (rc) {
		step_fail("failed to open object: %s", d_errdesc(rc));
		return rc;
	}

	get_rc = kv_get(oh, 4096);
	rc = daos_kv_close(oh, NULL);

	if (get_rc) {
		step_fail("failed to read: %s", d_errdesc(get_rc));
		return get_rc;
	}

	if (rc) {
		step_fail("failed to close object: %s", d_errdesc(rc));
		return rc;
	}

	step_success("");
	return 0;
}

static int
kv_insert1m(void)
{
	daos_handle_t	oh = DAOS_HDL_INVAL; /** object handle */
	int		put_rc;
	int		rc;

	new_oid();
	daos_obj_generate_oid(coh, &oid, DAOS_OT_KV_HASHED, 0, 0, 0);

	rc = daos_kv_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	if (rc) {
		step_fail("failed to open object: %s", d_errdesc(rc));
		return rc;
	}

	put_rc = kv_put(oh, 1048576);
	rc = daos_kv_close(oh, NULL);

	if (put_rc) {
		step_fail("failed to insert: %s", d_errdesc(put_rc));
		return put_rc;
	}

	if (rc) {
		step_fail("failed to close object: %s", d_errdesc(rc));
		return rc;
	}

	step_success("");
	return 0;
}

static int
kv_read1m(void)
{
	daos_handle_t	oh = DAOS_HDL_INVAL; /** object handle */
	int		get_rc;
	int		rc;

	rc = daos_kv_open(coh, oid, DAOS_OO_RO, &oh, NULL);
	if (rc) {
		step_fail("failed to open object: %s", d_errdesc(rc));
		return rc;
	}

	get_rc = kv_get(oh, 1048576);
	rc = daos_kv_close(oh, NULL);

	if (get_rc) {
		step_fail("failed to insert: %s", d_errdesc(get_rc));
		return get_rc;
	}

	if (rc) {
		step_fail("failed to close object: %s", d_errdesc(rc));
		return rc;
	}

	step_success("");
	return 0;
}

static int
kv_insertrf1(void)
{
	daos_handle_t	oh = DAOS_HDL_INVAL; /** object handle */
	int		put_rc;
	int		rc;

	if (domain_nr < 2)
		D_GOTO(skip_step, rc = -DER_INVAL);

	new_oid2();
	daos_obj_generate_oid(coh2, &oid2, DAOS_OT_KV_HASHED, 0, 0, 0);

	rc = daos_kv_open(coh2, oid2, DAOS_OO_RW, &oh, NULL);
	if (rc) {
		step_fail("failed to open object: %s", d_errdesc(rc));
		return rc;
	}

	put_rc = kv_put(oh, 128);
	rc = daos_kv_close(oh, NULL);

	if (put_rc) {
		step_fail("failed to insert: %s", d_errdesc(put_rc));
		return put_rc;
	}

	if (rc) {
		step_fail("failed to close object: %s", d_errdesc(rc));
		return rc;
	}
	step_success("");
	return 0;

skip_step:
	step_skip("Group size(2) is larger than domain_nr(%d)", domain_nr);
	return 0;
}

static int
kv_readrf1(void)
{
	daos_handle_t	oh = DAOS_HDL_INVAL; /** object handle */
	int		get_rc;
	int		rc;

	if (domain_nr < 2)
		D_GOTO(skip_step, rc = -DER_INVAL);

	rc = daos_kv_open(coh2, oid2, DAOS_OO_RW, &oh, NULL);
	if (rc) {
		step_fail("failed to open object: %s", d_errdesc(rc));
		return rc;
	}

	get_rc = kv_get(oh, 128);
	rc = daos_kv_close(oh, NULL);

	if (get_rc) {
		step_fail("failed to read: %s", d_errdesc(get_rc));
		return get_rc;
	}

	if (rc) {
		step_fail("failed to close object: %s", d_errdesc(rc));
		return rc;
	}

	step_success("");
	return 0;

skip_step:
	step_skip("Group size(2) is larger than domain_nr(%d)", domain_nr);
	return 0;
}

static int
kv_insertrf2(void)
{
	daos_handle_t	oh = DAOS_HDL_INVAL; /** object handle */
	int		put_rc;
	int		rc;

	if (domain_nr < 3)
		D_GOTO(skip_step, rc = -DER_INVAL);

	new_oid3();
	daos_obj_generate_oid(coh3, &oid3, DAOS_OT_KV_HASHED, 0, 0, 0);

	rc = daos_kv_open(coh3, oid3, DAOS_OO_RW, &oh, NULL);
	if (rc) {
		step_fail("failed to open object: %s", d_errdesc(rc));
		return rc;
	}

	put_rc = kv_put(oh, 128);
	rc = daos_kv_close(oh, NULL);

	if (put_rc) {
		step_fail("failed to insert: %s", d_errdesc(put_rc));
		return put_rc;
	}

	if (rc) {
		step_fail("failed to close object: %s", d_errdesc(rc));
		return rc;
	}

	step_success("");
	return 0;

skip_step:
	step_skip("Group size(3) is larger than domain_nr(%d)", domain_nr);
	return 0;
}

static int
kv_readrf2(void)
{
	daos_handle_t	oh = DAOS_HDL_INVAL; /** object handle */
	int		get_rc;
	int		rc;

	if (domain_nr < 3)
		D_GOTO(skip_step, rc = -DER_INVAL);

	rc = daos_kv_open(coh3, oid3, DAOS_OO_RW, &oh, NULL);
	if (rc) {
		step_fail("failed to open object: %s", d_errdesc(rc));
		return rc;
	}

	get_rc = kv_get(oh, 128);
	rc = daos_kv_close(oh, NULL);

	if (get_rc) {
		step_fail("failed to read: %s", d_errdesc(get_rc));
		return get_rc;
	}

	if (rc) {
		step_fail("failed to close object: %s", d_errdesc(rc));
		return rc;
	}

	step_success("");
	return 0;

skip_step:
	step_skip("Group size(3) is larger than domain_nr(%d)", domain_nr);
	return 0;
}

static int
cclose(void)
{
	int rc;

	rc = daos_cont_close(coh, NULL);
	if (rc)
		D_GOTO(fail, rc);

	if (domain_nr >= 2) {
		rc = daos_cont_close(coh2, NULL);
		if (rc)
			D_GOTO(fail, rc);
	}

	if (domain_nr >= 3) {
		rc = daos_cont_close(coh3, NULL);
		if (rc)
			D_GOTO(fail, rc);
	}

	step_success("");
	return 0;

fail:
	step_fail(d_errdesc(rc));
	return rc;
}

static int
cdestroy(void)
{
	int rc;

	rc = daos_cont_destroy(poh, cuuid, force, NULL);
	if (rc)
		D_GOTO(fail, rc);

	if (cuuid2) {
		rc = daos_cont_destroy(poh, cuuid2, force, NULL);
		if (rc)
			D_GOTO(fail, rc);
	}

	if (cuuid3) {
		rc = daos_cont_destroy(poh, cuuid3, force, NULL);
		if (rc)
			D_GOTO(fail, rc);
	}

	step_success("");
	return 0;

fail:
	step_fail(d_errdesc(rc));
	return rc;
}

static int
pdisconnect(void)
{
	int rc;

	rc = daos_pool_disconnect(poh, NULL);
	if (rc) {
		step_fail(d_errdesc(rc));
		return rc;
	}
	step_success("");
	return 0;
}

static int
fini(void)
{
	int rc;

	rc = daos_fini();
	if (rc) {
		step_fail(d_errdesc(rc));
		return rc;
	}
	step_success("");
	return 0;
}

struct step {
	int	id;		/** step number */
	char	op[26];		/** string describing the operation */
	int	(*func)(void);	/** function to be executed */
	int	clean_step;	/** upon failure, step to resume from */
};

static struct step steps[] = {
	/** Set up */
	{ 0,	"Initializing DAOS",			init,		100 },
	{ 1,	"Connecting to pool",			pconnect,	99 },
	{ 2,	"Creating containers",			ccreate,	98 },
	{ 3,	"Opening container",			copen,		97 },

	/** Layout generation tests */
	{ 10,	"Generating 1M S1 layouts",		oS1,		96 },
	{ 11,	"Generating 10K SX layouts",		oSX,		96 },

	/** KV tests */
	{ 20,	"Inserting 128B values",		kv_insert128,	96 },
	{ 21,	"Reading 128B values back",		kv_read128,	96 },
	/** { 22,	"Listing keys",				kv_list,	96 },
	*/
	{ 23,	"Punching object",			kv_punch,	96 },
	{ 24,	"Inserting 4KB values",			kv_insert4k,	96 },
	{ 25,	"Reading 4KB values back",		kv_read4k,	96 },
	/** { 26,	"Listing keys",				kv_list,	96 },
	*/
	{ 27,	"Punching object",			kv_punch,	96 },
	{ 28,	"Inserting 1MB values",			kv_insert1m,	96 },
	{ 29,	"Reading 1MB values back",		kv_read1m,	96 },
	/** { 30,	"Listing keys",				kv_list,	96 },
	*/
	{ 31,	"Punching object",			kv_punch,	96 },

	/** Test aux containers */
	{ 40,	"Inserting into RF1 cont",		kv_insertrf1,	96 },
	{ 41,	"Reading RF1 values back",		kv_readrf1,	96 },
	{ 42,	"Inserting into RF2 cont",		kv_insertrf2,	96 },
	{ 43,	"Reading RF2 values back",		kv_readrf2,	96 },

	/** Array tests */

	/** Tear down */
	{ 96,	"Closing containers",			cclose,		97 },
	{ 97,	"Destroying containers",		cdestroy,	98 },
	{ 98,	"Disconnecting from pool",		pdisconnect,	99 },
	{ 99,	"Tearing down DAOS",			fini,		100 },
	{ 100,	"",					NULL,		100 }
};

int
pool_autotest_hdlr(struct cmd_args_s *ap)
{
	int		rc;
	int		resume = 0;
	struct step	*s;
	int		ret = 0;

	assert(ap != NULL);
	assert(ap->p_op == POOL_AUTOTEST);

	uuid_unparse(ap->p_uuid, ap->pool_str);

	autotest_ap = ap;

	if (autotest_ap->deadline_limit)
		deadline_limit = autotest_ap->deadline_limit * CLOCKS_PER_SEC;

	step_init();

	for (s = steps; s->func != NULL; s++) {
		if (s->id < resume)
			continue;
		step_new(s->id, s->op);

		int	i;
		bool	found = false;

		if (ap->skip_big) {
			for (i = 0; i < sizeof(skip_steps) / sizeof(int); i++) {
				if (s->id == skip_steps[i]) {
					found = true;
					break;
				}
			}
		}
		if (found) {
			step_skip("skipped");
			rc = 0;
		} else {
			rc = (s->func)();
		}

		if (rc) {
			force = 1;
			if (!ret)
				ret = rc;
			resume = s->clean_step;
		}
	}

	if (force)
		fprintf(ap->outstream,
			"\nSome steps \033[0;31mfailed\033[0m.\n");
	else
		fprintf(ap->outstream,
			"\nAll steps \033[0;32mpassed\033[0m.\n");

	return ret;
}
