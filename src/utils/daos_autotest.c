/**
 * (C) Copyright 2020-2021 Intel Corporation.
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

#include "daos_hdlr.h"

/** Input arguments passed to daos utility */
struct cmd_args_s *autotest_ap;

/** How many concurrent I/O in flight */
#define MAX_INFLIGHT 16

/** Step timing  */
clock_t	start;
clock_t	end;

/** generated container UUID */
uuid_t		cuuid;
/** initial object ID */
uint64_t	oid_hi = 1;
daos_obj_id_t	oid = { .hi = 1, .lo = 1 }; /** object ID */

/** pool handle */
daos_handle_t	poh = DAOS_HDL_INVAL;
/** container handle */
daos_handle_t	coh = DAOS_HDL_INVAL;

/** force cleanup */
int force;

static inline void
new_oid(void)
{
	oid.hi = ++oid_hi;
	oid.lo = 1;
}

static inline float
duration(void)
{
	return (float) (end - start) * 1E-6;
}

static inline void
step_print(const char *status, const char *comment, ...)
{
	va_list	ap;
	char	timing[8];
	int	i;

	end = clock();
	printf("  %s    ", status);
	sprintf(timing, "%03.3f", duration());
	for (i = strlen(timing); i < 7; i++)
		printf(" ");
	printf("%s  ", timing);
	va_start(ap, comment);
	vprintf(comment, ap);
	va_end(ap);
	printf("\n");
}

static inline void
step_success(const char *comment, ...)
{
	va_list	ap;

	va_start(ap, comment);
	step_print("\033[0;32mOK\033[0m",comment, ap);
	va_end(ap);
}

static inline void
step_fail(const char *comment, ...)
{
	va_list	ap;

	va_start(ap, comment);
	step_print("\033[0;31mKO\033[0m",comment, ap);
	va_end(ap);
}

static inline void
step_new(int step, char *msg)
{
	int i;

	printf("%3d  %s", step, msg);
	for (i = strlen(msg); i < 25; i++)
		printf(" ");
	fflush(stdout);
	start = clock();
}

static inline void
step_init(void)
{
	printf("\033[1;35mStep Operation               ");
	printf("Status Time(sec) Comment\033[0m\n");
}

static int
init(void)
{
	int rc;

	rc = daos_init();
	if (rc) {
		step_fail(d_errdesc(rc));
		return -1;
	}
	step_success("");
	return 0;
}

static int
pconnect(void)
{
	int rc;

	/** Connect to pool */
	rc = daos_pool_connect(autotest_ap->p_uuid, autotest_ap->sysname,
			       DAOS_PC_RW, &poh, NULL, NULL);
	if (rc) {
		step_fail(d_errdesc(rc));
		return -1;
	}

	step_success("");
	return 0;
}

static int
ccreate(void)
{
	int rc;

	/** Create container */
	uuid_generate(cuuid);
	rc = daos_cont_create(poh, cuuid, NULL, NULL);
	if (rc) {
		step_fail(d_errdesc(rc));
		return -1;
	}

	step_success("uuid = "DF_UUIDF, DP_UUID(cuuid));
	return 0;
}

static int
copen(void)
{
	int rc;

	/** Open container */
	rc = daos_cont_open(poh, cuuid, DAOS_COO_RW, &coh, NULL, NULL);
	if (rc) {
		step_fail(d_errdesc(rc));
		return -1;
	}

	step_success("");
	return 0;
}

static int
oS1(void)
{
	daos_handle_t	oh = DAOS_HDL_INVAL; /** object handle */
	int		i;
	int		rc;

	new_oid();
	daos_obj_generate_id(&oid, 0, OC_S1, 0);

	for (i = 0; i < 1000000; i++) {

		rc = daos_obj_open(coh, oid, DAOS_OO_RO, &oh, NULL);
		if (rc) {
			step_fail("failed to open object: %s", d_errdesc(rc));
			return -1;
		}

		rc = daos_obj_close(oh, NULL);
		if (rc) {
			step_fail("failed to close object: %s", d_errdesc(rc));
			return -1;
		}
	}

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
	daos_obj_generate_id(&oid, 0, OC_SX, 0);

	for (i = 0; i < 10000; i++) {

		rc = daos_obj_open(coh, oid, DAOS_OO_RO, &oh, NULL);
		if (rc) {
			step_fail("failed to open object: %s", d_errdesc(rc));
			return -1;
		}

		rc = daos_obj_close(oh, NULL);
		if (rc) {
			step_fail("failed to close object: %s", d_errdesc(rc));
			return -1;
		}
	}

	step_success("");
	return 0;
}

static int
kv_put(daos_handle_t oh, daos_size_t size, uint64_t nr)
{
	daos_handle_t	eq;
	daos_event_t	ev_array[MAX_INFLIGHT];
	char		key[MAX_INFLIGHT][10];
	char		*val;
	daos_event_t	*evp;
	uint64_t	i;
	int		rc;
	int		eq_rc;

	/** Create event queue to manage asynchronous I/Os */
	rc = daos_eq_create(&eq);
	if (rc) {
		return rc;
	}

	/** allocate buffer to store value */
	D_ALLOC(val, size * MAX_INFLIGHT);
	if (val == NULL) {
		rc = daos_eq_destroy(eq, 0);
		return -DER_NOMEM;
	}
	memset(val, 'D', size * MAX_INFLIGHT);

	/** Issue actual I/Os */
	for (i = 1; i < nr + 1; i++) {
		char *key_cur;
		char *val_cur;

		if (i < MAX_INFLIGHT) {
			/** Haven't reached max request in flight yet */
			evp = &ev_array[i];
			rc = daos_event_init(evp, eq, NULL);
			if (rc) {
				break;
			}
			key_cur = key[i];
			val_cur = val + size * i;
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
		sprintf(key_cur, "%ld", i);
		/** value = sequend ID + DDDDDDD... */
		*((uint64_t *)val_cur) = i;

		/** Insert kv pair */
		rc = daos_kv_put(oh, DAOS_TX_NONE, 0, key_cur, size, val_cur,
				evp);
		if (rc) {
			break;
		}
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

	return rc;
}

static int
kv_get(daos_handle_t oh, daos_size_t size, uint64_t nr)
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

	/** Create event queue to manage asynchronous I/Os */
	rc = daos_eq_create(&eq);
	if (rc) {
		return rc;
	}

	/** allocate buffer to store value */
	D_ALLOC(val, size * MAX_INFLIGHT);
	if (val == NULL) {
		eq_rc = daos_eq_destroy(eq, 0);
		return -DER_NOMEM;
	}

	/** Issue actual I/Os */
	for (i = 1; i < nr + 1; i++) {
		char		*key_cur;
		char		*val_cur;
		daos_size_t	*val_sz_cur;

		if (i < MAX_INFLIGHT) {
			/** Haven't reached max request in flight yet */
			evp = &ev_array[i];
			rc = daos_event_init(evp, eq, NULL);
			if (rc) {
				break;
			}
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

	/** verify that we got the sum of all integers from 1 to nr */
	if (res != nr * (nr + 1) / 2)
		rc = -DER_MISMATCH;

	return rc;
}

static int
kv_insert128(void)
{
	daos_handle_t	oh = DAOS_HDL_INVAL; /** object handle */
	int		put_rc;
	int		rc;

	new_oid();
	daos_obj_generate_id(&oid, DAOS_OF_KV_FLAT, OC_SX, 0);

	rc = daos_kv_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	if (rc) {
		step_fail("failed to open object: %s", d_errdesc(rc));
		return -1;
	}

	put_rc = kv_put(oh, 128, 1000000);
	rc = daos_kv_close(oh, NULL);

	if (put_rc) {
		step_fail("failed to insert: %s", d_errdesc(put_rc));
		return -1;
	}

	if (rc) {
		step_fail("failed to close object: %s", d_errdesc(rc));
		return -1;
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
		return -1;
	}

	get_rc = kv_get(oh, 128, 1000000);
	rc = daos_kv_close(oh, NULL);

	if (get_rc) {
		step_fail("failed to insert: %s", d_errdesc(get_rc));
		return -1;
	}

	if (rc) {
		step_fail("failed to close object: %s", d_errdesc(rc));
		return -1;
	}

	step_success("");
	return 0;
}

#if 0
/**
 * Disable since it triggers an assertion error on the client.
 * Will be enabled once problem is fixed.
 */
static int
kv_punch(void)
{
	daos_handle_t	oh = DAOS_HDL_INVAL; /** object handle */
	int		punch_rc;
	int		rc;

	rc = daos_kv_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	if (rc) {
		step_fail("failed to open object: %s", d_errdesc(rc));
		return -1;
	}

	punch_rc = daos_obj_punch(oh, DAOS_TX_NONE, 0, NULL);
	rc = daos_kv_close(oh, NULL);

	if (punch_rc) {
		step_fail("failed to punch object: %s", d_errdesc(punch_rc));
		return -1;
	}

	if (rc) {
		step_fail("failed to close object: %s", d_errdesc(rc));
		return -1;
	}

	step_success("");
	return 0;
}
#endif

static int
kv_insert4k(void)
{
	daos_handle_t	oh = DAOS_HDL_INVAL; /** object handle */
	int		put_rc;
	int		rc;

	new_oid();
	daos_obj_generate_id(&oid, DAOS_OF_KV_FLAT, OC_SX, 0);

	rc = daos_kv_open(coh, oid, DAOS_OO_RO, &oh, NULL);
	if (rc) {
		step_fail("failed to open object: %s", d_errdesc(rc));
		return -1;
	}

	put_rc = kv_put(oh, 4096, 1000000);
	rc = daos_kv_close(oh, NULL);

	if (put_rc) {
		step_fail("failed to insert: %s", d_errdesc(put_rc));
		return -1;
	}

	if (rc) {
		step_fail("failed to close object: %s", d_errdesc(rc));
		return -1;
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
		return -1;
	}

	get_rc = kv_get(oh, 4096, 1000000);
	rc = daos_kv_close(oh, NULL);

	if (get_rc) {
		step_fail("failed to insert: %s", d_errdesc(get_rc));
		return -1;
	}

	if (rc) {
		step_fail("failed to close object: %s", d_errdesc(rc));
		return -1;
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
	daos_obj_generate_id(&oid, DAOS_OF_KV_FLAT, OC_SX, 0);

	rc = daos_kv_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	if (rc) {
		step_fail("failed to open object: %s", d_errdesc(rc));
		return -1;
	}

	put_rc = kv_put(oh, 1048576, 100000);
	rc = daos_kv_close(oh, NULL);

	if (put_rc) {
		step_fail("failed to insert: %s", d_errdesc(put_rc));
		return -1;
	}

	if (rc) {
		step_fail("failed to close object: %s", d_errdesc(rc));
		return -1;
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
		return -1;
	}

	get_rc = kv_get(oh, 1048576, 100000);
	rc = daos_kv_close(oh, NULL);

	if (get_rc) {
		step_fail("failed to insert: %s", d_errdesc(get_rc));
		return -1;
	}

	if (rc) {
		step_fail("failed to close object: %s", d_errdesc(rc));
		return -1;
	}

	step_success("");
	return 0;
}

static int
cclose(void)
{
	int rc;

	rc = daos_cont_close(coh, NULL);
	if (rc) {
		step_fail(d_errdesc(rc));
		return -1;
	}
	step_success("");
	return 0;
}

static int
cdestroy(void)
{
	int rc;

	rc = daos_cont_destroy(poh, cuuid, force, NULL);
	if (rc) {
		step_fail(d_errdesc(rc));
		return -1;
	}
	step_success("");
	return 0;
}

static int
pdisconnect(void)
{
	int rc;

	rc = daos_pool_disconnect(poh, NULL);
	if (rc) {
		step_fail(d_errdesc(rc));
		return -1;
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
		return -1;
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
	{ 0,	"Initializing DAOS",		init ,		100 },
	{ 1,	"Connecting to pool",		pconnect,	99 },
	{ 2,	"Creating container",		ccreate,	98 },
	{ 3,	"Opening container",		copen,		97 },

	/** Layout generation tests */
	{ 10,	"Generating 1M S1 layouts",	oS1,		96 },
	{ 11,	"Generating 10K SX layouts",	oSX,		96 },

	/** KV tests */
	{ 20,	"Inserting 1M 128B values",	kv_insert128,	96 },
	{ 21,	"Reading 128B values back",	kv_read128,	96 },
	//{ 22,	"Listing keys",			kv_list,	96 },
	//{ 23,	"Punching object",		kv_punch,	96 },
	{ 24,	"Inserting 1M 4KB values",	kv_insert4k,	96 },
	{ 25,	"Reading 4KB values back",	kv_read4k,	96 },
	//{ 26,	"Listing keys",			kv_list,	96 },
	//{ 27,	"Punching object",		kv_punch,	96 },
	{ 28,	"Inserting 100K 1MB values",	kv_insert1m,	96 },
	{ 29,	"Reading 1MB values back",	kv_read1m,	96 },
	//{ 30,	"Listing keys",			kv_list,	96 },
	//{ 31,	"Punching object",		kv_punch,	96 },

	/** Array tests */

	/** Tear down */
	{ 96,	"Closing container",		cclose,		97 },
	{ 97,	"Destroying container",		cdestroy,	98 },
	{ 98,	"Disconnecting from pool",	pdisconnect,	99 },
	{ 99,	"Tearing down DAOS",		fini,		100 },
	{ 100,	"",				NULL,		100 }
};

int
pool_autotest_hdlr(struct cmd_args_s *ap)
{
	int		rc;
	int		resume = 0;
	struct step	*s;

	assert(ap != NULL);
	assert(ap->p_op == POOL_AUTOTEST);

	autotest_ap = ap;

	step_init();

	for (s = steps; s->func != NULL; s++) {
		if (s->id < resume)
			continue;
		step_new(s->id, s->op);
		rc = (s->func)();
		if (rc) {
			force = 1;
			resume = s->clean_step;
		}
	}

	if (force) {
		printf("\nSome steps \033[0;31mfailed\033[0m.\n");
		rc = 1;
	} else {
		printf("\nAll steps \033[0;32mpassed\033[0m.\n");
		rc = 0;
	}

	return rc;
}
