/**
 * (C) Copyright 2017-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <uuid/uuid.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>

#include <daos_srv/evtree.h>
#include <daos_srv/bio.h>
#include <daos/tests_lib.h>
#include <daos_pool.h>
#include <daos/cmd_parser.h>
#include <utest_common.h>

/*
 * The following structure used to
 * pass bool value , command line information
 * to various functions
 */
struct test_input_value {
	bool             input;
	int		 rect_nr;
	char             *optval;
};

struct test_input_value tst_fn_val;

/**
 * An example for integer key evtree .
 */

static struct utest_context	*ts_utx;
static struct umem_attr		*ts_uma;
static int                       ts_feats = EVT_FEAT_DEFAULT | EVT_FEAT_DYNAMIC_ROOT;

#define ORDER_DEF_INTERNAL	13
#define	ORDER_DEF		16
#define	ORDER_TEST_SIZE		200

static int			ts_order = ORDER_DEF;

static struct evt_root		*ts_root;
static daos_handle_t		ts_toh;

#define EVT_SEP			','
#define EVT_SEP_VAL		':'
#define EVT_SEP_EXT		'-'
#define EVT_SEP_MNR		'.'
#define EVT_SEP_EPC		'@'

/* Data sizes */
#define D_1K_SIZE		1024
#define D_16K_SIZE		(16 * 1024)
#define D_256K_SIZE		(256 * 1024)
#define D_512K_SIZE		(512 * 1024)
#define D_1M_SIZE		(1024 * 1024)
#define D_256M_SIZE		(256 * 1024 * 1024)

#define POOL_NAME "/mnt/daos/evtree-utest"
#define POOL_SIZE ((1024 * 1024  * 1024ULL))

struct test_arg {
	struct utest_context	*ta_utx;
	struct umem_attr	*ta_uma;
	struct evt_root		*ta_root;
	char			*ta_pool_name;
};

/* variables for test group */
static char		**test_group_args;
static int		test_group_argc;

static int
ts_evt_bio_free(struct umem_instance *umm, struct evt_desc *desc,
		daos_size_t nob, void *args)
{
	struct utest_context *utx;

	if (args)
		utx = ((struct test_arg *)args)->ta_utx;
	else
		utx = ts_utx;

	if (!bio_addr_is_hole(&desc->dc_ex_addr))
		utest_free(utx, desc->dc_ex_addr.ba_off);
	return 0;
}

static struct evt_desc_cbs	ts_evt_desc_cbs  = {
	.dc_bio_free_cb		= ts_evt_bio_free,
};

static int
ts_evt_bio_nofree(struct umem_instance *umm, struct evt_desc *desc,
		daos_size_t nob, void *args)
{
	/* caller is responsible for free */
	return 0;
}

static struct evt_desc_cbs	ts_evt_desc_nofree_cbs  = {
	.dc_bio_free_cb		= ts_evt_bio_nofree,
};

static void
ts_open_create(void)
{
	bool    create;
	char    *arg;
	int	rc;

	create = tst_fn_val.input;
	arg = tst_fn_val.optval;

	if (daos_handle_is_valid(ts_toh)) {
		D_PRINT("Tree has been opened\n");
		fail();
	}

	if (create && arg != NULL) {
		if (arg[0] != 'o' || arg[1] != EVT_SEP_VAL) {
			D_PRINT("incorrect format for tree order: %s\n", arg);
			fail();
		}

		ts_order = atoi(&arg[2]);
		if (ts_order < EVT_ORDER_MIN || ts_order > EVT_ORDER_MAX) {
			D_PRINT("Invalid tree order %d\n", ts_order);
			fail();
		}

	}

	if (create) {
		D_PRINT("Create evtree with order %d\n", ts_order);
		rc = evt_create(ts_root, ts_feats, ts_order, ts_uma,
				&ts_evt_desc_cbs, &ts_toh);
	} else {
		D_PRINT("Open evtree\n");
		rc = evt_open(ts_root, ts_uma, &ts_evt_desc_cbs, &ts_toh);
	}

	if (rc != 0) {
		D_PRINT("Tree %s failed: "DF_RC"\n", create ? "create" : "open",
			DP_RC(rc));
		fail();
	}
}

static void
ts_close_destroy(void)
{
	bool destroy;
	int rc;

	destroy = tst_fn_val.input;

	if (daos_handle_is_inval(ts_toh)) {
		D_PRINT("Invalid tree open handle\n");
		fail();
	}

	if (destroy) {
		D_PRINT("Destroy evtree\n");
		rc = evt_destroy(ts_toh);
	} else {
		D_PRINT("Close evtree\n");
		rc = evt_close(ts_toh);
	}

	ts_toh = DAOS_HDL_INVAL;
	if (rc != 0) {
		D_PRINT("Tree %s failed: "DF_RC"\n",
			destroy ? "destroy" : "close", DP_RC(rc));
		fail();
	}
}

static int
ts_parse_rect(char *str, struct evt_rect *rect, daos_epoch_range_t *epr_in,
	      char **val_p, bool *should_pass)
{
	char			*tmp;
	daos_epoch_range_t	 epr = {0, DAOS_EPOCH_MAX};

	if (should_pass == NULL) {
		if (str[0] == '-') {
			D_PRINT("should_pass not supported %s\n", str);
			return -1;
		}
		goto parse_rect;
	}
	*should_pass = true;
	if (str[0] == '-') {
		str++;
		*should_pass = false;
	}
parse_rect:
	rect->rc_ex.ex_lo = strtoull(str, NULL, 10);

	tmp = strchr(str, EVT_SEP_EXT);
	if (tmp == NULL) {
		D_PRINT("Invalid input string %s\n", str);
		return -1;
	}

	str = tmp + 1;
	rect->rc_ex.ex_hi = strtoull(str, NULL, 10);
	tmp = strchr(str, EVT_SEP_EPC);
	if (tmp == NULL) {
		D_PRINT("Invalid input string %s\n", str);
		return -1;
	}

	str = tmp + 1;
	rect->rc_epc = epr.epr_lo = strtoull(str, NULL, 10);
	tmp = strchr(str, EVT_SEP_MNR);
	if (tmp == NULL) {
		rect->rc_minor_epc = 1;
	} else {
		str = tmp + 1;
		rect->rc_minor_epc = atoi(str);
	}

	tmp = strchr(str, EVT_SEP_EXT);
	if (tmp != NULL) {
		str = tmp + 1;
		epr.epr_hi = strtoull(str, NULL, 10);
	}

	if (epr_in != NULL)
		*epr_in = epr;

	if (val_p == NULL) /* called by evt_find */
		return 0;

	tmp = strchr(str, EVT_SEP_VAL);
	if (tmp == NULL) {
		*val_p = NULL; /* punch */
		return 0;
	}

	str = tmp + 1;
	if (should_pass == NULL) {
		if (strlen(str) < 1 || strlen(str) > 2) {
			D_PRINT("Expected one of [-][HhBbVbCc]: got %s\n", str);
			return -1;
		}
		goto done;
	}

	if (strlen(str) != evt_rect_width(rect)) {
		D_PRINT("Length of string cannot match extent size %d/%d "
			"str=%s rect="DF_RECT"\n",
			(int)strlen(str), (int)evt_rect_width(rect), str,
			DP_RECT(rect));
		return -1;
	}
done:
	*val_p = str;
	return 0;
}

static void
init_mem(void *ptr, size_t size, const void *src_mem)
{
	memcpy(ptr, src_mem, size);
}

static inline int
bio_alloc_init(struct utest_context *utx, bio_addr_t *addr, const void *src,
	       size_t size)
{
	int		rc;
	umem_off_t	umoff;

	addr->ba_type = DAOS_MEDIA_SCM;
	if (src == NULL) {
		BIO_ADDR_SET_HOLE(addr);
		return 0;
	} else {
		BIO_ADDR_CLEAR_HOLE(addr);
	}
	rc = utest_alloc(utx, &umoff, size, init_mem, src);

	if (rc != 0)
		goto end;

	bio_addr_set(addr, DAOS_MEDIA_SCM, umoff);
end:
	return rc;
}

static int
bio_strdup(struct utest_context *utx, bio_addr_t *addr, const char *str)
{
	size_t len = 0;

	if (str != NULL)
		len = strlen(str) + 1;

	return bio_alloc_init(utx, addr, str, len);
}

static void
ts_add_rect(void)
{
	char			*val;
	bio_addr_t		 bio_addr = {0}; /* Fake bio addr */
	struct evt_entry_in	 entry = {0};
	int			 rc;
	bool			 should_pass;
	static int		 total_added;
	char			*arg;

	arg = tst_fn_val.optval;

	if (arg == NULL) {
		D_PRINT("No parameters\n");
		fail();
	}

	rc = ts_parse_rect(arg, &entry.ei_rect, NULL, &val, &should_pass);
	if (rc != 0) {
		D_PRINT("Parsing tree failure "DF_RC"\n", DP_RC(rc));
		fail();
	}
	D_PRINT("Insert "DF_RECT": val=%s expect_pass=%s (total in tree=%d)\n",
		DP_RECT(&entry.ei_rect), val ? val : "<NULL>",
		should_pass ? "true" : "false", total_added);


	rc = bio_strdup(ts_utx, &bio_addr, val);
	if (rc != 0) {
		D_FATAL("Insufficient memory for test\n");
		fail();
	}
	entry.ei_addr = bio_addr;
	entry.ei_ver = 0;
	entry.ei_bound = entry.ei_rect.rc_epc;
	entry.ei_inob = val == NULL ? 0 : 1;

	rc = evt_insert(ts_toh, &entry, NULL);
	if (rc == 1)
		rc = 0;
	if (rc == 0)
		total_added++;

	if (rc != 0 && !bio_addr_is_hole(&bio_addr))
		utest_free(ts_utx, bio_addr.ba_off);

	if (should_pass) {
		if (rc != 0) {
			D_FATAL("Add rect failed "DF_RC"\n", DP_RC(rc));
			fail();
		}
	} else {
		if (rc == 0) {
			D_FATAL("Add rect should have failed\n");
			fail();
		}
	}
}

static void
ts_delete_rect()
{
	char			*val;
	struct evt_entry	 ent;
	struct evt_rect		 rect;
	int			 rc;
	bool			 should_pass;
	static int		 total_deleted;
	char			*arg;

	arg = tst_fn_val.optval;
	if (arg == NULL)
		fail();

	rc = ts_parse_rect(arg, &rect, NULL, &val, &should_pass);
	if (rc != 0)
		fail();

	D_PRINT("Delete "DF_RECT": val=%s expect_pass=%s (total in tree=%d)\n",
		DP_RECT(&rect), val ? val : "<NULL>",
		should_pass ? "true" : "false", total_deleted);

	rc = evt_delete(ts_toh, &rect, &ent);

	if (rc == 0)
		total_deleted++;

	if (should_pass) {
		if (rc != 0) {
			D_FATAL("Delete rect failed "DF_RC"\n", DP_RC(rc));
		} else if (evt_rect_width(&rect) !=
			   evt_extent_width(&ent.en_sel_ext)) {
			rc = 1;
			D_FATAL("Returned rectangle width doesn't match\n");
		}
	} else {
		if (rc == 0) {
			D_FATAL("Delete rect should have failed\n");
			fail();
		}
		rc = 0;
	}

}

static void
ts_remove_rect(void)
{
	char			*arg;
	struct evt_rect		 rect;
	daos_epoch_range_t	 epr;
	int			 rc;
	bool			 should_pass;

	arg = tst_fn_val.optval;
	if (arg == NULL)
		fail();

	rc = ts_parse_rect(arg, &rect, &epr, NULL, &should_pass);
	if (rc != 0)
		fail();

	D_PRINT("Remove all "DF_EXT"@"DF_X64"-"DF_X64" expect_pass=%s\n",
		DP_EXT(&rect.rc_ex), epr.epr_lo, epr.epr_hi,
		should_pass ? "true" : "false");

	rc = evt_remove_all(ts_toh, &rect.rc_ex, &epr);

	if (should_pass) {
		if (rc < 0) {
			D_FATAL("Remove rect failed "DF_RC"\n", DP_RC(rc));
			fail();
		}
	} else {
		if (rc >= 0) {
			D_FATAL("Remove rect should have failed\n");
			fail();
		}
	}
}


static void
ts_find_rect(void)
{
	struct evt_entry	*ent;
	char			*val;
	struct evt_filter	 filter = {0};
	bio_addr_t		 addr;
	struct evt_rect		 rect;
	EVT_ENT_ARRAY_LG_PTR(ent_array);
	int			 rc;
	bool			 should_pass;
	char			*arg;

	arg = tst_fn_val.optval;
	if (arg == NULL)
		fail();

	rc = ts_parse_rect(arg, &rect, NULL, &val, &should_pass);
	if (rc != 0)
		fail();

	D_PRINT("Search rectangle "DF_RECT"\n", DP_RECT(&rect));

	filter.fr_epr.epr_lo = 0;
	filter.fr_epr.epr_hi = rect.rc_epc;
	filter.fr_epoch = filter.fr_epr.epr_hi;
	filter.fr_ex = rect.rc_ex;
	evt_ent_array_init(ent_array, 0);
	rc = evt_find(ts_toh, &filter, ent_array);
	if (rc != 0)
		D_FATAL("Add rect failed "DF_RC"\n", DP_RC(rc));

	evt_ent_array_for_each(ent, ent_array) {
		bool punched;
		addr = ent->en_addr;

		punched = bio_addr_is_hole(&addr);
		D_PRINT("Find rect "DF_ENT" width=%d "
			"val=%.*s\n", DP_ENT(ent),
			(int)evt_extent_width(&ent->en_sel_ext),
			punched ? 4 : (int)evt_extent_width(&ent->en_sel_ext),
			punched ? "None" : (char *)utest_off2ptr(ts_utx,
								 addr.ba_off));
	}

	evt_ent_array_fini(ent_array);
}

static void
ts_list_rect(void)
{
	char			*val;
	daos_anchor_t		 anchor;
	struct evt_filter	 filter = {0};
	struct evt_rect		 rect;
	daos_epoch_range_t	 epr;
	daos_handle_t		 ih;
	int			 i;
	char			*arg;
	int			 rc, rc2;
	int			 options = 0;
	bool			 probe = true;

	arg = tst_fn_val.optval;
	if (arg == NULL) {
		filter.fr_ex.ex_lo = 0;
		filter.fr_ex.ex_hi = ~(0ULL);
		filter.fr_epr.epr_lo = 0;
		filter.fr_epr.epr_hi = DAOS_EPOCH_MAX;
		filter.fr_epoch = filter.fr_epr.epr_hi;
		goto start;
	}

	rc = ts_parse_rect(arg, &rect, &epr, &val, NULL);
	if (rc != 0)
		fail();
	filter.fr_ex = rect.rc_ex;
	filter.fr_epr = epr;
	filter.fr_epoch = filter.fr_epr.epr_hi;
	if (!val)
		goto start;

	i = 0;
	if (val[0] == '-') {
		options = EVT_ITER_REVERSE;
		i = 1;
	}

	switch (val[i]) {
	case 'H':
		options |= EVT_ITER_EMBEDDED | EVT_ITER_VISIBLE |
			EVT_ITER_SKIP_HOLES;
		break;
	case 'h':
		options |= EVT_ITER_VISIBLE | EVT_ITER_SKIP_HOLES;
		probe = false;
		break;
	case 'V':
		options |= EVT_ITER_EMBEDDED;
	case 'v':
		options |= EVT_ITER_VISIBLE;
		probe = false;
		break;
	case 'C':
		options |= EVT_ITER_EMBEDDED;
	case 'c':
		options |= EVT_ITER_COVERED;
		probe = false;
		break;
	default:
		D_PRINT("Unknown iterator type: %c\n", val[0]);
		fail();
	}

start:
	rc = evt_iter_prepare(ts_toh, options, &filter, &ih);
	if (rc != 0) {
		D_PRINT("Failed to prepare iterator: "DF_RC"\n", DP_RC(rc));
		fail();
	}

	rc = evt_iter_probe(ih, EVT_ITER_FIRST, NULL, NULL);
	if (rc == -DER_NONEXIST)
		D_GOTO(out, rc = 0);

	if (rc != 0) {
		D_PRINT("Failed to probe: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	for (i = 0;; i++) {
		struct evt_entry	ent;
		unsigned int		inob = 0;

		rc = evt_iter_fetch(ih, &inob, &ent, &anchor);
		if (rc == 0) {
			if (inob != 1) {
				D_PRINT("Unexpected value for inob: %d\n",
					inob);
				fail();
			}
			D_PRINT("%d) "DF_ENT", val_addr="DF_U64" val=%.*s\n",
				i, DP_ENT(&ent),
				(uint64_t)utest_off2ptr(ts_utx,
							ent.en_addr.ba_off),
				bio_addr_is_hole(&ent.en_addr) ?
				4 : (int)evt_extent_width(&ent.en_sel_ext),
				bio_addr_is_hole(&ent.en_addr) ?
				"None" :
				(char *)utest_off2ptr(ts_utx,
						      ent.en_addr.ba_off));

			if (!probe)
				goto skip_probe;
			if (i % 3 == 0) {
				rect.rc_ex = ent.en_sel_ext;
				rect.rc_epc = ent.en_epoch;
				rect.rc_minor_epc = ent.en_minor_epc;
				rc = evt_iter_probe(ih, EVT_ITER_FIND,
						    &rect, NULL);
			}
			if (i % 3 == 1) {
				rc = evt_iter_probe(ih, EVT_ITER_FIND,
						    NULL, &anchor);
			}
		}
skip_probe:

		if (rc == -DER_NONEXIST) {
			D_PRINT("Found %d entries\n", i);
			D_GOTO(out, rc = 0);
		}

		if (rc != 0)
			D_GOTO(out, rc);

		rc = evt_iter_next(ih);
		if (rc != 0)
			D_GOTO(out, rc);
	}
out:
	rc2 = evt_iter_finish(ih);
	assert_rc_equal(rc2, 0);
}

#define TS_VAL_CYCLE	4

static void
ts_many_add(void)
{
	char			*buf;
	char			*tmp;
	uint64_t		*seq;
	struct evt_rect		*rect;
	struct evt_entry_in	 entry = {0};
	bio_addr_t		 bio_addr = {0}; /* Fake bio addr */
	long			 offset = 0;
	int			 size;
	int			 nr;
	int			 i;
	int			 rc;
	char			*arg;
	/* argument format: "s:NUM,e:NUM,n:NUM"
	 * s: start offset
	 * e: extent size
	 * n: number of extents
	 */
	arg = tst_fn_val.optval;
	if (!arg) {
		D_PRINT("need input parameters s:NUM,e:NUM,n:NUM\n");
		fail();
	}

	if (arg[0] == 's') {
		if (arg[1] != EVT_SEP_VAL) {
			D_PRINT("Invalid parameter %s\n", arg);
			fail();
		}
		offset = strtol(&arg[2], &tmp, 0);
		if (*tmp != EVT_SEP) {
			D_PRINT("Invalid parameter %s\n", arg);
			fail();
		}
		arg = tmp + 1;
	}

	if (arg[0] != 'e' || arg[1] != EVT_SEP_VAL) {
		D_PRINT("Invalid parameter %s\n", arg);
		fail();
	}

	size = strtol(&arg[2], &tmp, 0);
	if (size <= 0) {
		D_PRINT("Invalid extent size %d\n", size);
		fail();
	}
	if (*tmp != EVT_SEP) {
		D_PRINT("Invalid parameter %s\n", arg);
		fail();
	}
	arg = tmp + 1;

	if (arg[0] != 'n' || arg[1] != EVT_SEP_VAL) {
		D_PRINT("Invalid parameter %s\n", arg);
		fail();
	}
	nr = strtol(&arg[2], &tmp, 0);
	if (nr <= 0) {
		D_PRINT("Invalid extent number %d\n", nr);
		fail();
	}
	tst_fn_val.rect_nr = nr;

	D_ALLOC(buf, size);
	if (!buf)
		fail();

	seq = dts_rand_iarr_alloc_set(nr, 0, true);
	if (!seq) {
		D_FREE(buf);
		fail();
	}

	rect = &entry.ei_rect;

	for (i = 0; i < nr; i++) {
		rect->rc_ex.ex_lo = offset + seq[i] * size;
		rect->rc_ex.ex_hi = rect->rc_ex.ex_lo + size - 1;
		rect->rc_epc = (seq[i] % TS_VAL_CYCLE) + 1;

		memset(buf, 'a' + seq[i] % TS_VAL_CYCLE, size - 1);

		rc = bio_strdup(ts_utx, &bio_addr, buf);
		if (rc != 0) {
			D_FATAL("Insufficient memory for test\n");
			fail();
		}
		entry.ei_bound = entry.ei_rect.rc_epc;
		entry.ei_addr = bio_addr;
		entry.ei_ver = 0;
		entry.ei_inob = 1;

		rc = evt_insert(ts_toh, &entry, NULL);
		if (rc == 1)
			rc = 0;
		if (rc != 0) {
			D_FATAL("Add rect %d failed "DF_RC"\n", i, DP_RC(rc));
			fail();
		}
	}

	D_FREE(buf);
	D_FREE(seq);
}

static void
ts_tree_debug(void)
{
	int	level;
	char   *arg;

	arg = tst_fn_val.optval;
	level = atoi(arg);
	evt_debug(ts_toh, level);
}

static void
ts_drain(void)
{
	static int const drain_creds = 256;
	int	rc;

	ts_many_add();
	while (1) {
		bool destroyed = false;
		int creds = drain_creds;

		rc = evt_drain(ts_toh, &creds, &destroyed);
		if (rc) {
			print_message("Failed to drain: %s\n", d_errstr(rc));
			fail();
		}
		print_message("drained %d of %d\n", drain_creds - creds,
			      tst_fn_val.rect_nr);
		if (destroyed) {
			print_message("tree is empty\n");
			break;
		}
	}
}

int
teardown_builtin(void **state)
{
	struct test_arg *arg = *state;
	int		 rc;

	if (arg == NULL) {
		print_message("state not set, likely due to group-setup"
			      " issue\n");
		return 0;
	}

	rc = utest_utx_destroy(arg->ta_utx);

	D_FREE(arg->ta_pool_name);
	ts_evt_desc_cbs.dc_bio_free_args = NULL;

	return rc;
}
int
setup_builtin(void **state)
{
	struct test_arg	*arg = *state;
	static int	 tnum;
	int		 rc = 0;

	D_ASPRINTF(arg->ta_pool_name, "/mnt/daos/evtree-test-%d", tnum++);
	if (arg->ta_pool_name == NULL) {
		print_message("Failed to allocate test struct\n");
		return 1;
	}

	rc = utest_pmem_create(arg->ta_pool_name, POOL_SIZE,
			       sizeof(*arg->ta_root), NULL, &arg->ta_utx);
	if (rc != 0) {
		perror("Evtree internal test couldn't create pool");
		rc = 1;
		goto failed;
	}

	arg->ta_root = utest_utx2root(arg->ta_utx);
	arg->ta_uma = utest_utx2uma(arg->ta_utx);
	ts_evt_desc_cbs.dc_bio_free_args = arg;

	return 0;
failed:
	D_FREE(arg->ta_pool_name);
	return rc;
}

static int
global_setup(void **state)
{
	struct test_arg	*arg;

	D_ALLOC_PTR(arg);
	if (arg == NULL) {
		print_message("Failed to allocate test struct\n");
		return 1;
	}

	*state = arg;

	return 0;
}

static int
global_teardown(void **state)
{
	struct test_arg	*arg = *state;

	D_FREE(arg);

	return 0;
}

#define NUM_EPOCHS 100
#define NUM_PARTIAL 11
#define NUM_EXTENTS 30

/* copy_exp_val_to_array
* Input parameters : flag, evtdata
* Input/Output parameters : val, exp_size
*/
static void
copy_exp_val_to_array(int flag, int **evtdata,
					int *val, int *exp_size)
{
	int epoch;
	int offset;
	int loop_count;
	int count;
	int	incr = 0;

	count = *exp_size;

	for (epoch = 1; epoch <= NUM_EPOCHS; epoch++) {
	if (epoch < NUM_EPOCHS) {
		switch (flag) {
		case EVT_ITER_COVERED:
			incr = 0;
		break;
		default:
		break;
		}
		if (flag == EVT_ITER_VISIBLE) {
			val[epoch] = evtdata[epoch][epoch];
			 count++;
		} else if (flag == EVT_ITER_COVERED) {
			for (offset = epoch; offset >= 1; offset--) {
				if (evtdata[offset][epoch+incr] != 0) {
					val[count] =
					evtdata[offset][epoch+incr];
					count++;
				}
			}
		} else {
			if ((evtdata[epoch][epoch] & 0xFF) != 0xFF) {
				count++;
				val[count] = evtdata[epoch][epoch];
			}
		}
	} else {
		if (flag == EVT_ITER_VISIBLE) {
			for (offset = epoch;
			offset < NUM_EXTENTS + epoch; offset++) {
				val[offset] = evtdata[epoch][offset];
				 count++;
			}
		} else if (flag == (EVT_ITER_COVERED)) {
			for (loop_count = epoch;
				loop_count < NUM_EXTENTS+epoch; loop_count++) {
				for (offset = epoch; offset >= 1 ; offset--) {
					if (evtdata[offset][loop_count] != 0) {
						val[count] =
						evtdata[offset][loop_count];
						count++;
					}
				}
			}
		} else {
			for (offset = epoch; offset < NUM_EXTENTS + epoch;
			offset++) {
				if ((evtdata[epoch][offset] & 0xFF) != 0xFF) {
					count++;
					val[count] = evtdata[epoch][offset];
				}
			}
		}
	}
	}
	*exp_size = count;
}

/* create_expected_data:
* Input values:  iter_flag, evt_data
* Input/Output : expval, rev_expval
*/
static void
create_expected_data(int iter_flag, int **evt_data,
					int *expval, int *rev_expval)
{
	int expected_size;
	int count;
	int loop_count;

	count = 0;
	switch (iter_flag) {
	case EVT_ITER_EMBEDDED:
		print_message("EVT_ITER_EMBEDDED\n");
	break;
	case EVT_ITER_VISIBLE:
		print_message("EVT_ITER_VISIBLE\n");
		copy_exp_val_to_array(iter_flag, evt_data, expval, &count);
	break;
	case EVT_ITER_COVERED:
		print_message("EVT_ITER_VISIBLE (COVERED)\n");
		count = 1;
		copy_exp_val_to_array(iter_flag, evt_data, expval, &count);
	break;
	case (EVT_ITER_SKIP_HOLES|EVT_ITER_VISIBLE):
		print_message("EVT_ITER_SKIP_HOLES (VISIBLE)\n");
		copy_exp_val_to_array(iter_flag, evt_data, expval, &count);
	break;
	case (EVT_ITER_REVERSE | EVT_ITER_SKIP_HOLES
			| EVT_ITER_VISIBLE):
		print_message("EVT_ITER_REVERSE (SKIP_HOLES and VISIBLE)\n");
	break;
	case (EVT_ITER_REVERSE | EVT_ITER_COVERED):
		print_message("EVT_ITER_REVERSE (VISIBLE and COVERED)\n");
	break;
	case (EVT_ITER_REVERSE | EVT_ITER_VISIBLE):
		print_message("EVT_ITER_REVERSE (VISIBLE)\n");
	break;
	default:
		print_message("Invalid Flag\n");
	}
	if ((iter_flag & EVT_ITER_COVERED) == EVT_ITER_COVERED)
		expected_size = count-1;
	else
		expected_size = count;
	for (loop_count = expected_size,
		count = 1; loop_count >= 1;
		loop_count--, count++) {
		rev_expval[count] = expval[loop_count];
	}
}

/* test_evt_iter_flags :
*
* Validate the following conditions:
*   EVT_ITER_VISIBLE|EVT_ITER_SKIP_HOLES
*   EVT_ITER_REVERSE|EVT_ITER_VISIBLE|EVT_ITER_SKIP_HOLES
*   EVT_ITER_VISIBLE
*   EVT_ITER_REVERSE|EVT_ITER_VISIBLE
*   EVT_ITER_COVERED
*   EVT_ITER_REVERSE|EVT_ITER_COVERED
*   EVT_ITER_EMBEDDED
*/
static void
test_evt_iter_flags(void **state)
{
	struct test_arg		*arg = *state;
	int			*value;
	daos_handle_t		 toh;
	daos_handle_t		 ih;
	struct evt_entry_in	 entry = {0};
	struct evt_entry	 ent;
	int			rc;
	int			epoch;
	int			offset;
	int			sum;
	int			iter_count;
	int			count;
	uint32_t	inob;
	int		**data;
	int		*exp_val;
	int		*actual_val;
	int		*rev_exp_val;
	int		hole_epoch;
	int		val[] = {
		EVT_ITER_VISIBLE | EVT_ITER_SKIP_HOLES,
		EVT_ITER_REVERSE | EVT_ITER_VISIBLE | EVT_ITER_SKIP_HOLES,
		EVT_ITER_VISIBLE,
		EVT_ITER_REVERSE|EVT_ITER_VISIBLE,
		EVT_ITER_COVERED,
		EVT_ITER_REVERSE|EVT_ITER_COVERED,
		EVT_ITER_EMBEDDED};
	int		t_repeats;

	/* Create a evtree */
	rc = evt_create(arg->ta_root, ts_feats, ORDER_DEF_INTERNAL, arg->ta_uma,
			&ts_evt_desc_cbs, &toh);
	assert_rc_equal(rc, 0);
	D_ALLOC_ARRAY(data, (NUM_EPOCHS + 1));
	if (data == NULL)
		goto end;
	for (count = 0; count < NUM_EPOCHS + 1; count++) {
		D_ALLOC_ARRAY(data[count], (NUM_EPOCHS+NUM_EXTENTS+1));
		if (data[count] == NULL) {
			print_message("Cannot allocate Memory\n");
			goto finish3;
		}
	}
	D_ALLOC_ARRAY(exp_val, (NUM_EPOCHS+1)*
				(NUM_EPOCHS+NUM_EXTENTS+1));
	if (exp_val == NULL)
		goto finish2;
	D_ALLOC_ARRAY(actual_val, (NUM_EPOCHS+1)*
				(NUM_EPOCHS+NUM_EXTENTS+1));
	if (actual_val == NULL)
		goto finish1;
	D_ALLOC_ARRAY(rev_exp_val, (NUM_EPOCHS+1)*
				(NUM_EPOCHS+NUM_EXTENTS+1));
	if (rev_exp_val == NULL)
		goto finish;
	/* Insert a bunch of entries with hole*/
	srand(time(0));
	hole_epoch = (rand() % 28) + 1;
	print_message("Hole inserted %d epoch.\n", hole_epoch);
	for (epoch = 1; epoch <= NUM_EPOCHS; epoch++) {
		for (offset = epoch; offset < NUM_EXTENTS + epoch; offset++) {
			entry.ei_rect.rc_ex.ex_lo = offset;
			entry.ei_rect.rc_ex.ex_hi = offset;
			entry.ei_rect.rc_epc = epoch;
			memset(&entry.ei_csum, 0, sizeof(entry.ei_csum));
			entry.ei_ver = 0;
			entry.ei_bound = epoch;
			/* Insert a hole at random epoch */
			if (epoch == hole_epoch)
				entry.ei_inob = 0;
			else
				entry.ei_inob = sizeof(offset);
			sum = offset - epoch + 1;
			/* Use 0xFF as mask to mark a data as hole */
			/* The data we use range from 1 to 30. */
			if (entry.ei_inob == 0) {
				data[epoch][offset] = 0xFF;
				rc = bio_alloc_init(arg->ta_utx,
					&entry.ei_addr,	NULL, 0);
			} else {
				data[epoch][offset] = sum;
				rc = bio_alloc_init(arg->ta_utx,
					&entry.ei_addr,
					&sum, sizeof(sum));
			}
			if (rc != 0)
				goto finish;
			rc = evt_insert(toh, &entry, NULL);
			if (rc == 1)
				rc = 0;
			if (rc != 0)
				goto finish;
		}
	}
	for (iter_count = 0; iter_count < (
		sizeof(val)/sizeof(int)); iter_count++) {
		memset(exp_val, 0,
			(NUM_EPOCHS+1)*(NUM_EPOCHS+NUM_EXTENTS+1)*
			sizeof(int));
		memset(actual_val, 0,
			(NUM_EPOCHS+1)*(NUM_EPOCHS+NUM_EXTENTS+1)*
			sizeof(int));
		create_expected_data(val[iter_count], data, exp_val,
				     rev_exp_val);
		rc = evt_iter_prepare(toh, val[iter_count], NULL, &ih);
		if (rc != 0)
			goto finish;
		rc = evt_iter_probe(ih, EVT_ITER_FIRST, NULL, NULL);
		if (rc != 0)
			goto finish;
		sum = 0;
		count = 0;
		/* Gather the evtree data using evt_iter_fetch */
		for (;;) {
			rc = evt_iter_fetch(ih, &inob, &ent, NULL);
			if (rc == -DER_NONEXIST)
				break;
			if (rc != 0)
				goto finish;
			count++;
			if (!bio_addr_is_hole(&ent.en_addr)) {
				value = utest_off2ptr(arg->ta_utx,
					ent.en_addr.ba_off);
				actual_val[count] = *value;
				sum += *value;
			} else {
				actual_val[count] = 0xFF;
			}
			rc = evt_iter_next(ih);
			if (rc == -DER_NONEXIST)
				break;
			if (rc != 0)
				goto finish;
		}
		print_message("Compare Expected/Actual Result\n");
		if (val[iter_count] == EVT_ITER_EMBEDDED) {
			t_repeats = 0;
			for (t_repeats = 1; t_repeats < (NUM_EXTENTS+1);
				t_repeats++) {
				sum = 01;
				for (count = 0; count < (NUM_EPOCHS+1)*
					(NUM_EPOCHS+NUM_EXTENTS+1); count++) {
					if (actual_val[count] == t_repeats)
						++sum;
				}
				if (sum != NUM_EPOCHS)
					rc = 1;
				else
					rc = 0;
			}
		} else if (((val[iter_count] & EVT_ITER_EMBEDDED)
			!= EVT_ITER_EMBEDDED) &&
			((val[iter_count] &
			EVT_ITER_REVERSE) != EVT_ITER_REVERSE)) {
			rc = memcmp(exp_val, actual_val,
				(NUM_EPOCHS+1)*(NUM_EPOCHS+NUM_EXTENTS+1)*
				sizeof(int));
		} else {
			rc = memcmp(actual_val, rev_exp_val,
				(NUM_EPOCHS+1)*(NUM_EPOCHS+NUM_EXTENTS+1)*
				sizeof(int));

		}
		print_message("RC: %d\n", rc);
		assert_int_equal(rc, 0);
		if (rc != 0)
			goto finish;
		rc = evt_iter_finish(ih);
		if (rc != 0)
			goto finish;
	}
finish:
	D_FREE(rev_exp_val);
finish1:
	D_FREE(actual_val);
finish2:
	D_FREE(exp_val);
finish3:
	for (count = 0; count < NUM_EPOCHS + 1; count++)
		D_FREE(data[count]);
end:
	D_FREE(data);
	rc = evt_destroy(toh);
	assert_rc_equal(rc, 0);
}

static void
test_evt_iter_delete(void **state)
{
	struct test_arg		*arg = *state;
	int			*value;
	daos_handle_t		 toh;
	daos_handle_t		 ih;
	struct evt_entry_in	 entry = {0};
	struct evt_entry	 ent;
	int			 rc;
	int			 epoch;
	int			 offset;
	int			 sum, expected_sum;
	struct evt_filter	 filter = {0};
	uint32_t		 inob;

	rc = evt_create(arg->ta_root, ts_feats, ORDER_DEF_INTERNAL, arg->ta_uma,
			&ts_evt_desc_nofree_cbs, &toh);
	assert_rc_equal(rc, 0);
	rc = utest_sync_mem_status(arg->ta_utx);
	assert_int_equal(rc, 0);

	rc = evt_has_data(arg->ta_root, arg->ta_uma);
	assert_rc_equal(rc, 0);

	/* Insert a bunch of entries */
	for (epoch = 1; epoch <= NUM_EPOCHS; epoch++) {
		for (offset = epoch; offset < NUM_EXTENTS + epoch; offset++) {
			entry.ei_rect.rc_ex.ex_lo = offset;
			entry.ei_rect.rc_ex.ex_hi = offset;
			entry.ei_rect.rc_epc = epoch;
			entry.ei_bound = epoch;
			memset(&entry.ei_csum, 0, sizeof(entry.ei_csum));
			entry.ei_ver = 0;
			entry.ei_inob = sizeof(offset);
			sum = offset - epoch + 1;
			rc = bio_alloc_init(arg->ta_utx, &entry.ei_addr, &sum,
					    sizeof(sum));
			assert_int_equal(rc, 0);

			rc = evt_insert(toh, &entry, NULL);
			if (rc == 1)
				rc = 0;
			assert_rc_equal(rc, 0);
			rc = utest_check_mem_increase(arg->ta_utx);
			assert_int_equal(rc, 0);
			rc = utest_sync_mem_status(arg->ta_utx);
			assert_int_equal(rc, 0);
		}
	}
	rc = evt_has_data(arg->ta_root, arg->ta_uma);
	assert_rc_equal(rc, 1);

	rc = evt_iter_prepare(toh, EVT_ITER_VISIBLE, NULL, &ih);
	assert_rc_equal(rc, 0);
	rc = evt_iter_probe(ih, EVT_ITER_FIRST, NULL, NULL);
	assert_rc_equal(rc, 0);
	sum = 0;
	for (;;) {
		rc = evt_iter_fetch(ih, &inob, &ent, NULL);
		if (rc == -DER_NONEXIST)
			break;
		assert_rc_equal(rc, 0);
		assert_int_equal(inob, sizeof(sum));

		value = utest_off2ptr(arg->ta_utx, ent.en_addr.ba_off);
		sum += *value;

		rc = evt_iter_next(ih);
		if (rc == -DER_NONEXIST)
			break;
		assert_rc_equal(rc, 0);
	}
	expected_sum = NUM_EPOCHS + (NUM_EXTENTS * (NUM_EXTENTS + 1) / 2) - 1;
	assert_int_equal(expected_sum, sum);
	rc = evt_iter_finish(ih);
	assert_rc_equal(rc, 0);


	filter.fr_ex.ex_lo = 0;
	filter.fr_ex.ex_hi = NUM_EPOCHS + NUM_EXTENTS;
	filter.fr_epr.epr_lo = NUM_EPOCHS - NUM_PARTIAL + 1;
	filter.fr_epr.epr_hi = DAOS_EPOCH_MAX;
	filter.fr_epoch = filter.fr_epr.epr_hi;
	rc = evt_iter_prepare(toh, 0, &filter, &ih);
	assert_rc_equal(rc, 0);

	rc = evt_iter_probe(ih, EVT_ITER_FIRST, NULL, NULL);
	assert_rc_equal(rc, 0);

	sum = 0;

	/* Delete some of the entries */
	for (;;) {
		rc = evt_iter_delete(ih, &ent);
		if (rc == -DER_NONEXIST)
			break;

		assert_rc_equal(rc, 0);

		assert_false(bio_addr_is_hole(&ent.en_addr));

		value = utest_off2ptr(arg->ta_utx, ent.en_addr.ba_off);

		sum += *value;
		utest_free(arg->ta_utx, ent.en_addr.ba_off);
	}
	expected_sum = NUM_PARTIAL * (NUM_EXTENTS * (NUM_EXTENTS + 1) / 2);
	assert_int_equal(expected_sum, sum);

	rc = evt_iter_finish(ih);
	assert_rc_equal(rc, 0);

	/* No filter so delete everything */
	rc = evt_iter_prepare(toh, 0, NULL, &ih);
	assert_rc_equal(rc, 0);

	rc = evt_iter_probe(ih, EVT_ITER_FIRST, NULL, NULL);
	assert_rc_equal(rc, 0);

	/* Ok, delete the rest */
	while (!evt_iter_empty(ih)) {
		rc = evt_iter_delete(ih, &ent);
		assert_rc_equal(rc, 0);

		assert_false(bio_addr_is_hole(&ent.en_addr));

		value = utest_off2ptr(arg->ta_utx, ent.en_addr.ba_off);

		sum += *value;
		utest_free(arg->ta_utx, ent.en_addr.ba_off);
		rc = utest_check_mem_decrease(arg->ta_utx);
		assert_int_equal(rc, 0);
		rc = utest_sync_mem_status(arg->ta_utx);
		assert_int_equal(rc, 0);
	}
	rc = evt_iter_finish(ih);
	assert_rc_equal(rc, 0);

	expected_sum = NUM_EPOCHS * (NUM_EXTENTS * (NUM_EXTENTS + 1) / 2);
	assert_int_equal(expected_sum, sum);
	rc = utest_check_mem_initial_status(arg->ta_utx);
	assert_int_equal(rc, 0);
	rc = evt_destroy(toh);
	assert_rc_equal(rc, 0);
}

static void
test_evt_find_internal(void **state)
{
	struct test_arg		*arg = *state;
	daos_handle_t		 toh;
	struct evt_entry_in	 entry = {0};
	struct evt_entry	 *ent;
	struct evt_filter	 filter = {0};
	char                     *value;
	EVT_ENT_ARRAY_LG_PTR(ent_array);
	bio_addr_t		 addr;
	int			 rc;
	int			 epoch;
	int			 offset;
	int			 hole_epoch;
	char testdata[] = "deadbeef";

	/* Create a evtree */
	rc = evt_create(arg->ta_root, ts_feats, ORDER_DEF_INTERNAL, arg->ta_uma,
			&ts_evt_desc_cbs, &toh);
	assert_rc_equal(rc, 0);
	rc = utest_sync_mem_status(arg->ta_utx);
	assert_int_equal(rc, 0);
	srand(time(0));
	hole_epoch = (rand() % 98) + 1;
	print_message("Hole inserted %d epoch.\n", hole_epoch);
	/* Insert data : deadbeef */
	for (epoch = 1; epoch <= NUM_EPOCHS; epoch++) {
		for (offset = epoch;
			offset < sizeof(testdata) + epoch;
			offset += sizeof(testdata)) {
			entry.ei_rect.rc_ex.ex_lo = offset;
			entry.ei_rect.rc_ex.ex_hi = offset+sizeof(testdata);
			entry.ei_rect.rc_epc = epoch;
			entry.ei_bound = epoch;
			memset(&entry.ei_csum, 0, sizeof(entry.ei_csum));
			entry.ei_ver = 0;
			/* Insert a hole at random epoch */
			if (epoch == hole_epoch)
				entry.ei_inob = 0;
			else
				entry.ei_inob = sizeof(testdata);
			if (entry.ei_inob == 0) {
				rc = bio_alloc_init(arg->ta_utx,
						&entry.ei_addr, NULL, 0);
				assert_int_equal(rc, 0);
			} else {
				rc = bio_alloc_init(arg->ta_utx,
						&entry.ei_addr,
						testdata, sizeof(testdata));
				assert_int_equal(rc, 0);
			}
			rc = evt_insert(toh, &entry, NULL);
			if (rc == 1)
				rc = 0;
			assert_rc_equal(rc, 0);
			rc = utest_check_mem_increase(arg->ta_utx);
			assert_int_equal(rc, 0);
			rc = utest_sync_mem_status(arg->ta_utx);
			assert_int_equal(rc, 0);
		}
	}
	/*
	 * Delete each record from last and run evt_find
	 * you get deadbeef, d (2-records). Covered records
	 * should be exposed on each deletes
	 */
	filter.fr_epr.epr_lo = 0;
	for (epoch = NUM_EPOCHS; epoch > 0; epoch--) {
		filter.fr_ex.ex_lo = epoch - 1;
		filter.fr_ex.ex_hi = epoch + 9;
		filter.fr_epr.epr_hi = epoch;
		filter.fr_epoch = filter.fr_epr.epr_hi;
		evt_ent_array_init(ent_array, 1);
		rc = evt_find(toh, &filter, ent_array);
		if (rc != 0)
			D_FATAL("Find rect failed "DF_RC"\n", DP_RC(rc));
		evt_ent_array_for_each(ent, ent_array) {
			bool punched;
			static char buf[10];

			addr = ent->en_addr;
			strcpy(buf, " ");
			punched = bio_addr_is_hole(&addr);
			if (punched) {
				D_PRINT("Find rect " DF_ENT " (punch)\n", DP_ENT(ent));
			} else {
				/** Must have a valid address if the extent isn't punched */
				value = utest_off2ptr(arg->ta_utx, addr.ba_off);
				D_ASSERT(value != NULL);
				D_PRINT("Find rect " DF_ENT " width=%d val=%.*s\n", DP_ENT(ent),
					(int)evt_extent_width(&ent->en_sel_ext),
					(int)evt_extent_width(&ent->en_sel_ext), value);
			}
			punched ? strcpy(buf, "None") :
			strncpy(buf,
				(char *)utest_off2ptr(arg->ta_utx, addr.ba_off),
				(int)evt_extent_width(&ent->en_sel_ext));
			if (punched) {
				rc = strcmp(buf, "None");
				if (rc != 0)
					fail_msg("Data Check Failed");
			} else {
				if ((int)evt_extent_width(&ent->en_sel_ext)
					== 1) {
					rc = strcmp(buf, "d");
					if (rc != 0)
						fail_msg("Data Check Failed");
				} else {
					rc = strcmp(buf, "deadbeef");
					if (rc != 0)
						fail_msg("Data Check Failed");
				}
			}
		}
		/* Delete the last visible record */
		entry.ei_rect.rc_ex.ex_lo = epoch;
		entry.ei_rect.rc_ex.ex_hi = filter.fr_ex.ex_hi;
		entry.ei_rect.rc_epc = filter.fr_epr.epr_hi;
		filter.fr_epoch = filter.fr_epr.epr_hi;
		rc = evt_delete(toh, &entry.ei_rect, NULL);
		assert_rc_equal(rc, 0);
		rc = utest_check_mem_decrease(arg->ta_utx);
		assert_int_equal(rc, 0);
		rc = utest_sync_mem_status(arg->ta_utx);
		assert_int_equal(rc, 0);
		evt_ent_array_fini(ent_array);
	}
	rc = utest_check_mem_initial_status(arg->ta_utx);
	assert_int_equal(rc, 0);
	/* Destroy the tree */
	rc = evt_destroy(toh);
	assert_rc_equal(rc, 0);
}
/*
* Validate the following conditions:
*   EVT_ITER_VISIBLE|EVT_ITER_SKIP_HOLES
*   EVT_ITER_REVERSE|EVT_ITER_VISIBLE|EVT_ITER_SKIP_HOLES
*   EVT_ITER_VISIBLE
*   EVT_ITER_COVERED|EVT_ITER_EMBEDDED
*   EVT_ITER_REVERSE|EVT_ITER_VISIBLE
*   EVT_ITER_COVERED
*   EVT_ITER_REVERSE|EVT_ITER_COVERED
*   EVT_ITER_EMBEDDED
*/
static void
test_evt_iter_delete_internal(void **state)
{
	struct test_arg		*arg = *state;
	daos_handle_t		 toh;
	daos_handle_t		 ih;
	struct evt_entry_in	 entry = {0};
	int			 rc;
	int			 epoch;
	int			 offset;
	int		val[] = {
		EVT_ITER_VISIBLE | EVT_ITER_SKIP_HOLES,
		EVT_ITER_REVERSE | EVT_ITER_VISIBLE | EVT_ITER_SKIP_HOLES,
		EVT_ITER_VISIBLE,
		EVT_ITER_COVERED|EVT_ITER_EMBEDDED,
		EVT_ITER_REVERSE|EVT_ITER_VISIBLE,
		EVT_ITER_COVERED,
		EVT_ITER_REVERSE|EVT_ITER_COVERED,
		EVT_ITER_EMBEDDED};
	int			 iter_count;

	rc = evt_create(arg->ta_root, ts_feats, ORDER_DEF_INTERNAL, arg->ta_uma,
			&ts_evt_desc_cbs, &toh);
	assert_rc_equal(rc, 0);

	/* Insert a bunch of entries */
	for (epoch = 1; epoch <= NUM_EPOCHS; epoch++) {
		for (offset = epoch; offset < NUM_EXTENTS + epoch; offset++) {
			entry.ei_rect.rc_ex.ex_lo = offset;
			entry.ei_rect.rc_ex.ex_hi = offset;
			entry.ei_rect.rc_epc = epoch;
			entry.ei_bound = epoch;
			memset(&entry.ei_csum, 0, sizeof(entry.ei_csum));
			entry.ei_ver = 0;
			entry.ei_inob = sizeof(offset);
			rc = bio_alloc_init(arg->ta_utx, &entry.ei_addr,
					    &offset, sizeof(offset));
			assert_int_equal(rc, 0);

			rc = evt_insert(toh, &entry, NULL);
			if (rc == 1)
				rc = 0;
			assert_rc_equal(rc, 0);
		}
	}
	for (iter_count = 0; iter_count < (
		sizeof(val)/sizeof(int)); iter_count++) {
		struct evt_entry ent;

		/* No filter so delete everything */
		rc = evt_iter_prepare(toh, val[iter_count], NULL, &ih);
		assert_rc_equal(rc, 0);

		rc = evt_iter_probe(ih, EVT_ITER_FIRST, NULL, NULL);
		assert_rc_equal(rc, 0);

		/* Ok, delete the rest */
		while (!evt_iter_empty(ih)) {
			rc = evt_iter_delete(ih, &ent);
			if (val[iter_count] == EVT_ITER_EMBEDDED) {
				assert_rc_equal(rc, 0);
			} else {
				assert_int_not_equal(rc, 0);
				/* exit the loop */
				break;
			}
		}
		rc = evt_iter_finish(ih);
		assert_rc_equal(rc, 0);
	}
	rc = evt_destroy(toh);
	assert_rc_equal(rc, 0);
}

static void
test_evt_variable_record_size_internal(void **state)
{
	struct test_arg		*arg = *state;
	daos_handle_t		 toh;
	struct evt_entry_in	 entry = {0};
	int			 rc, count;
	int			 epoch;
	const int		val[] = {D_1K_SIZE,
					D_16K_SIZE};
	uint64_t		data_size;
	char                     *data;

	rc = evt_create(arg->ta_root, ts_feats, ORDER_DEF_INTERNAL,
			arg->ta_uma, &ts_evt_desc_cbs, &toh);
	assert_rc_equal(rc, 0);
	for (count = 0; count < sizeof(val)/sizeof(int); count++) {
		/* Try to insert a bunch of entries with variable data sizes */
		data_size = val[count];
		D_ALLOC(data, data_size);
		strcpy(data, "EVTree: Test Variable Record Size");
		for (epoch = 1; epoch < 3 ; epoch++) {
			entry.ei_rect.rc_ex.ex_lo = epoch;
			entry.ei_rect.rc_ex.ex_hi = epoch + data_size;
			entry.ei_rect.rc_epc = epoch;
			entry.ei_ver = 0;
			entry.ei_inob = data_size;
			entry.ei_bound = epoch;

			memset(&entry.ei_csum, 0, sizeof(entry.ei_csum));

			rc = bio_alloc_init(arg->ta_utx, &entry.ei_addr,
					    data, data_size);
			assert_int_equal(rc, 0);
			rc = evt_insert(toh, &entry, NULL);
			if (rc == 1)
				rc = 0;
			if (count > 0)
				assert_int_not_equal(rc, 0);
			else
				assert_rc_equal(rc, 0);
		}
		D_FREE(data);
	}
	rc = evt_destroy(toh);
	assert_rc_equal(rc, 0);
}

static void
test_evt_various_data_size_internal(void **state)
{
	struct test_arg		*arg = *state;
	daos_handle_t		 toh;
	daos_handle_t		 ih;
	struct evt_entry_in	 entry = {0};
	int			 rc, count;
	int			 epoch;
	const int		val[] = {D_1K_SIZE,
					D_16K_SIZE,
					D_256K_SIZE,
					D_512K_SIZE,
					D_1M_SIZE,
					D_256M_SIZE};
	uint64_t		data_size;
	char                     *data;
	EVT_ENT_ARRAY_LG_PTR(ent_array);
	struct evt_entry	 *ent;
	bio_addr_t		 addr;
	struct evt_filter	 filter = {0};
	int			 iteration = 0;

	for (count = 0; count < sizeof(val)/sizeof(int); count++) {
		rc = evt_create(arg->ta_root, ts_feats, ORDER_DEF_INTERNAL,
				arg->ta_uma, &ts_evt_desc_cbs, &toh);
		assert_rc_equal(rc, 0);
		rc = utest_sync_mem_status(arg->ta_utx);
		assert_int_equal(rc, 0);
		data_size = val[count];
		D_PRINT("Data Size: %ld\n", data_size);
		D_ALLOC(data, data_size);
		strcpy(data, "EVTree: Out of Memory");
		/* Loop does the following : evt_insert,
		* evt_find (first epoch) and evt_delete (random deletes)
		* till out of space condition
		*/
		iteration = 0;
		for (epoch = 1; ; epoch++) {
			if (DAOS_ON_VALGRIND) {
				iteration++;
				if (iteration > 20)
					break;
			}
			entry.ei_rect.rc_ex.ex_lo = epoch;
			entry.ei_rect.rc_ex.ex_hi = epoch + data_size - 1;
			entry.ei_rect.rc_epc = epoch;
			entry.ei_ver = 0;
			entry.ei_bound = epoch;
			entry.ei_inob = data_size;

			memset(&entry.ei_csum, 0, sizeof(entry.ei_csum));

			rc = bio_alloc_init(arg->ta_utx, &entry.ei_addr,
					    data, data_size);
			if (rc != 0) {
				assert_rc_equal(rc, -DER_NOSPACE);
				break;
			}
			rc = evt_insert(toh, &entry, NULL);
			if (rc == 1)
				rc = 0;
			if (rc != 0) {
				assert_rc_equal(rc, -DER_NOSPACE);
				break;
			}
			rc = utest_check_mem_increase(arg->ta_utx);
			assert_int_equal(rc, 0);
			rc = utest_sync_mem_status(arg->ta_utx);
			assert_int_equal(rc, 0);
			if (epoch == 1) {
				evt_ent_array_init(ent_array, 0);
				filter.fr_ex.ex_lo = epoch;
				filter.fr_ex.ex_hi = epoch + data_size - 1;
				filter.fr_epr.epr_hi = epoch;
				filter.fr_epoch = filter.fr_epr.epr_hi;
				rc = evt_find(toh, &filter, ent_array);
				if (rc != 0)
					D_FATAL("Find rect failed "DF_RC"\n",
						DP_RC(rc));
				evt_ent_array_for_each(ent, ent_array) {
					static char *actual;

					D_ALLOC(actual, data_size);
					addr = ent->en_addr;
					strncpy(actual,
					(char *)utest_off2ptr(arg->ta_utx,
						addr.ba_off),
					(int)evt_extent_width(
						&ent->en_sel_ext));
					rc = strcmp(actual, data);
					if (rc != 0) {
						D_FREE(actual);
						D_FREE(data);
						fail_msg("Data Check Failed\n");
					}
					D_FREE(actual);
				}
				evt_ent_array_fini(ent_array);
			}
			/* Delete a record*/
			if (epoch % 10 == 0) {
				entry.ei_rect.rc_ex.ex_lo = epoch;
				entry.ei_rect.rc_ex.ex_hi = epoch + data_size
							    - 1;
				entry.ei_rect.rc_epc = epoch;

				rc = evt_delete(toh, &entry.ei_rect, NULL);
				assert_rc_equal(rc, 0);
				rc = utest_check_mem_decrease(arg->ta_utx);
				assert_int_equal(rc, 0);
				rc = utest_sync_mem_status(arg->ta_utx);
				assert_int_equal(rc, 0);
			}
		}
		/* Delete remaining records: evt_iter_delete */
		rc = evt_iter_prepare(toh, 0, NULL, &ih);
		assert_rc_equal(rc, 0);
		rc = evt_iter_probe(ih, EVT_ITER_FIRST, NULL, NULL);
		assert_rc_equal(rc, 0);
		print_message("Deleting evtree contents\n");
		while (!evt_iter_empty(ih)) {
			rc = evt_iter_delete(ih, NULL);
			assert_rc_equal(rc, 0);
			rc = utest_check_mem_decrease(arg->ta_utx);
			assert_int_equal(rc, 0);
			rc = utest_sync_mem_status(arg->ta_utx);
			assert_int_equal(rc, 0);
		}
		rc = evt_iter_finish(ih);
		assert_rc_equal(rc, 0);
		/* Free allocated memory */
		D_FREE(data);
		rc = evt_destroy(toh);
		assert_rc_equal(rc, 0);
	}
}

static int
insert_val(struct test_arg *arg, daos_handle_t toh, daos_epoch_t epoch, uint64_t start_offset,
	   const char *data, uint64_t length)
{
	struct evt_entry_in	 entry = {0};
	int                      rc;

	entry.ei_rect.rc_ex.ex_lo = start_offset;
	entry.ei_rect.rc_ex.ex_hi = start_offset + length - 1;
	entry.ei_rect.rc_epc = epoch;
	entry.ei_ver = 0;
	entry.ei_bound = epoch;
	entry.ei_inob = 1;

	memset(&entry.ei_csum, 0, sizeof(entry.ei_csum));

	rc = bio_alloc_init(arg->ta_utx, &entry.ei_addr, &data, length);
	if (rc != 0)
		return rc;

	return evt_insert(toh, &entry, NULL);
}

static int
insert_one(struct test_arg *arg, daos_handle_t toh, daos_epoch_t epoch, uint64_t start_offset,
	   uint64_t length)
{
	char *data;
	int   rc;

	D_ALLOC_ARRAY(data, length);
	if (data == NULL)
		return -DER_NOMEM;
	memset(data, 'a', length);

	rc = insert_val(arg, toh, epoch, start_offset, data, length);

	D_FREE(data);

	return rc;
}

static int
insert_str(struct test_arg *arg, daos_handle_t toh, daos_epoch_t epoch, uint64_t start_offset,
	   const char *str)
{
	return insert_val(arg, toh, epoch, start_offset, str, strnlen(str, 32));
}

static void
test_evt_agg_check(void **state)
{
	struct test_arg		*arg = *state;
	daos_handle_t		 toh;
	int			 rc;
	int			 epoch;

	epoch = 1;
	rc = evt_create(arg->ta_root, ts_feats, ORDER_DEF_INTERNAL,
			arg->ta_uma, &ts_evt_desc_cbs, &toh);
	assert_rc_equal(rc, 0);

	rc = insert_one(arg, toh, epoch++, 0, 1);
	assert_rc_equal(rc, 0);

	rc = insert_one(arg, toh, epoch++, 1, 2);
	assert_rc_equal(rc, 1); /* Adjacent is aggregatable */

	rc = insert_one(arg, toh, epoch++, 10, 5);
	assert_rc_equal(rc, 0); /** Standalone, not aggregatable */

	rc = insert_one(arg, toh, epoch++, 9, 8);
	assert_rc_equal(rc, 1); /** Encapsulates prior extent, aggregatable */

	rc = insert_one(arg, toh, epoch++, 7, 2);
	assert_rc_equal(rc, 1); /** Adjacent to prior extent, aggregatable */

	rc = insert_one(arg, toh, epoch++, 5, 1);
	assert_rc_equal(rc, 0); /** Standalone, not aggregatable */

	rc = insert_one(arg, toh, epoch++, 11, 2);
	assert_rc_equal(rc, 1); /** Partial coverage, aggregatable */

	rc = insert_one(arg, toh, epoch++, DAOS_EC_PARITY_BIT | 1000, 2);
	assert_rc_equal(rc, 1); /** Parity written with non-parity in tree, aggregatable */

	rc = insert_one(arg, toh, epoch++, 1000, 2);
	assert_rc_equal(rc, 1); /** Simulate partial write with in-tree parity, aggregatable */

	rc = evt_destroy(toh);
	assert_rc_equal(rc, 0);
}

static void
test_evt_node_size_internal(void **state)
{
	struct test_arg		*arg = *state;
	daos_handle_t		 toh;
	int			order_size, rc;
	bool			evt_created;

	for (order_size = 0; order_size < ORDER_TEST_SIZE; order_size++) {
		evt_created = false;
		rc = evt_create(arg->ta_root, ts_feats, order_size,
				arg->ta_uma, &ts_evt_desc_cbs, &toh);
		if ((order_size >= EVT_ORDER_MIN) && (order_size <= EVT_ORDER_MAX)) {
			assert_rc_equal(rc, 0);
			evt_created = true;
		} else {
			assert_int_not_equal(rc, 0);
		}
		if (evt_created) {
			rc = evt_destroy(toh);
			assert_rc_equal(rc, 0);
		}
	}
}

struct expected_epochs {
	int	major_epc;
	int	minor_epc;
};

void
set_data(struct test_arg *arg, daos_handle_t toh, char *dest_data,
	 struct expected_epochs *dest_epc, int start, int size, const char *src,
	 int major_epc, int minor_epc)
{
	struct evt_entry_in	entry = {0};
	int			i, rc;
	int			end = start + size;

	if (dest_data != NULL)
		memcpy(dest_data + start, src, size);

	for (i = start; i < end; i++) {
		dest_epc[i].major_epc = major_epc;
		dest_epc[i].minor_epc = minor_epc;
	}

	entry.ei_rect.rc_ex.ex_lo = start;
	entry.ei_rect.rc_ex.ex_hi = end - 1;
	entry.ei_rect.rc_epc = major_epc;
	entry.ei_bound = major_epc;
	entry.ei_rect.rc_minor_epc = minor_epc;
	entry.ei_ver = 0;
	entry.ei_inob = 1;
	memset(&entry.ei_csum, 0, sizeof(entry.ei_csum));
	rc = bio_alloc_init(arg->ta_utx, &entry.ei_addr,
			    src, size);
	assert_int_equal(rc, 0);
	rc = evt_insert(toh, &entry, NULL);
	if (rc == 1)
		rc = 0;
	assert_rc_equal(rc, 0);
}

void
check_data(struct test_arg *arg, char *expected_data,
	   struct expected_epochs *expected_epochs, const struct evt_entry *ent)
{
	struct expected_epochs	*expected_epoch;
	char			*actual_data;

	expected_epoch = &expected_epochs[ent->en_sel_ext.ex_lo];

	actual_data = utest_off2ptr(arg->ta_utx, ent->en_addr.ba_off);
	if (actual_data == NULL)
		fail_msg("Read a hole where data was expected\n");

	if (memcmp(expected_data + ent->en_sel_ext.ex_lo, actual_data,
		   evt_extent_width(&ent->en_sel_ext)) != 0) {
		fail_msg("Extent mismatch %.*s != %.*s\n",
			 (int)evt_extent_width(&ent->en_sel_ext), actual_data,
			 (int)evt_extent_width(&ent->en_sel_ext),
			 expected_data + ent->en_sel_ext.ex_lo);
	}
	if (expected_epoch->major_epc != ent->en_epoch)
		fail_msg("Major epoch unexpected %x != "DF_X64"\n",
			 expected_epoch->major_epc, ent->en_epoch);
	if (expected_epoch->minor_epc != ent->en_minor_epc)
		fail_msg("Minor epoch unexpected %d != %d\n",
			 expected_epoch->minor_epc, ent->en_minor_epc);
}

static void
test_evt_overlap_split(struct test_arg *arg, int major_num, int minor_num)
{
	daos_handle_t		toh;
	daos_handle_t		ih;
	int			data_sizes[] = {1, 7, 4, 9, 5};
	int			offset_mult[] = {17, 113, 47, 21, 13, 101, 67,
						 47, 57, 31, 11, 71, 143, 19};
	int			data_size;
	int			rc;
	struct evt_entry	ent;
	int			minor_epc;
	int			major_epc;
	int			total_size;
	const char		*mystr = NULL;
	char			*expected_data = NULL;
	struct expected_epochs	*expected_epochs = NULL;
	uint32_t		 inob;
	int			tree_depth_fail = 0;

	rc = evt_create(arg->ta_root, ts_feats, ORDER_DEF_INTERNAL,
				arg->ta_uma, &ts_evt_desc_cbs, &toh);
	assert_rc_equal(rc, 0);
	/** Arbitrary character array, not too large to create many overlaps */
	total_size = NUM_EPOCHS * 2;
	D_ALLOC_ARRAY(expected_epochs, total_size);
	if (expected_epochs == NULL)
		goto finish;

	D_ALLOC_ARRAY(expected_data, total_size);
	if (expected_data == NULL)
		goto finish;

	memset(expected_data, 'X', total_size);

	set_data(arg, toh, NULL, expected_epochs, 0, total_size,
		 expected_data, 1, 1);

	memset(expected_data, 'X', total_size);

	for (major_epc = 1; major_epc <= major_num; major_epc++) {
		for (minor_epc = 1; minor_epc <= minor_num; minor_epc++) {
			int epoch = (major_epc - 1) * minor_num + minor_epc;
			int start;

			/** We write extent #1 above so skip it */
			if (epoch == 1)
				continue;

			data_size = data_sizes[epoch % ARRAY_SIZE(data_sizes)];
			start = offset_mult[epoch % ARRAY_SIZE(offset_mult)];
			start = (start * epoch) % total_size;
			if ((start + data_size) > total_size)
				start = (start + data_size) % total_size;

			switch (data_size) {
			case 1:
				mystr = "?";
				break;
			case 4:
				mystr = "dead";
				break;
			case 5:
				mystr = "moral";
				break;
			case 7:
				mystr = "January";
				break;
			case 9:
				mystr = "wonderful";
				break;
			default:
				fail_msg("Unexpected size");
				break;
			}
			set_data(arg, toh, expected_data,
				 expected_epochs, start, data_size,
				 mystr, major_epc, minor_epc);
		}
	}

	rc = evt_iter_prepare(toh, EVT_ITER_VISIBLE, NULL, &ih);
	if (rc != 0)
		goto finish;
	rc = evt_iter_probe(ih, EVT_ITER_FIRST, NULL, NULL);
	if (rc != 0)
		goto finish;

	for (;;) {
		rc = evt_iter_fetch(ih, &inob, &ent, NULL);
		if (rc == -DER_NONEXIST)
			break;
		assert_rc_equal(rc, 0);
		check_data(arg, expected_data, expected_epochs, &ent);
		rc = evt_iter_next(ih);
		if (rc == -DER_NONEXIST)
			break;
		assert_rc_equal(rc, 0);
	}

	rc = evt_iter_finish(ih);
	assert_rc_equal(rc, 0);

	D_PRINT("Tree depth :%d\n", arg->ta_root->tr_depth);
	if (arg->ta_root->tr_depth < 2)
		tree_depth_fail = 1;


finish:
	if (tree_depth_fail)
		fail_msg("Node not split\n");
	D_FREE(expected_data);
	D_FREE(expected_epochs);
	rc = evt_destroy(toh);
	assert_rc_equal(rc, 0);
}

#define NUM_MINOR 20
D_CASSERT((NUM_EPOCHS % NUM_MINOR) == 0);
#define NUM_MAJOR (NUM_EPOCHS / NUM_MINOR)
static void
test_evt_overlap_split_internal(void **state)
{
	struct test_arg		*arg = *state;

	test_evt_overlap_split(arg, NUM_EPOCHS, 1);
	test_evt_overlap_split(arg, NUM_MAJOR, NUM_MINOR);
	test_evt_overlap_split(arg, 1, NUM_EPOCHS);
}

static inline int
insert_and_check(daos_handle_t toh, struct evt_entry_in *entry, int idx, int nr)
{
	int		rc;
	static int	epoch = 1;

	entry->ei_rect.rc_ex.ex_lo = idx;
	entry->ei_rect.rc_ex.ex_hi = idx + nr - 1;
	entry->ei_rect.rc_epc = epoch;
	entry->ei_bound = epoch;
	rc = evt_insert(toh, entry, NULL);
	if (rc == 1)
		rc = 0;
	assert_rc_equal(rc, 0);

	return epoch++;
}

#define EVT_ALLOC_BUG_NR 1000
static void
test_evt_ent_alloc_bug(void **state)
{
	struct test_arg		*arg = *state;
	daos_handle_t		 toh;
	daos_handle_t		 ih;
	struct evt_entry_in	 entry_in = {0};
	struct evt_entry	 entry = {0};
	int			 rc;
	int			 count = 0;
	uint32_t		 inob;
	int			 last = 0;
	int			 idx1, nr1, idx2, nr2, idx3, nr3;

	rc = evt_create(arg->ta_root, ts_feats, ORDER_DEF_INTERNAL, arg->ta_uma,
			&ts_evt_desc_cbs, &toh);
	assert_rc_equal(rc, 0);

	idx1 = 0;
	nr1  = 500;
	idx2 = idx1 + nr1;
	nr2  = nr1;
	idx3 = idx1 + 1;
	nr3  = idx2 + nr2 - idx1 - 2;
	entry_in.ei_ver = 0;
	entry_in.ei_inob = 0;
	/* We will insert nothing but holes as we are just checking sorted
	 * iteration.
	 */
	bio_alloc_init(arg->ta_utx, &entry_in.ei_addr, NULL, 0);

	/* Fill in the entries */
	while (nr1 > 0 && nr2 > 0 && nr3 > 0) {
		last = insert_and_check(toh, &entry_in, idx1, nr1);
		if (last >= EVT_ALLOC_BUG_NR)
			break;
		idx1 += 2;
		nr1 -= 4;
		last = insert_and_check(toh, &entry_in, idx2, nr2);
		if (last >= EVT_ALLOC_BUG_NR)
			break;
		idx2 += 2;
		nr2 -= 4;
		last = insert_and_check(toh, &entry_in, idx3, nr3);
		if (last >= EVT_ALLOC_BUG_NR)
			break;
		idx3 += 2;
		nr3 -= 4;
	}

	/* Now, do a sorted iteration */
	rc = evt_iter_prepare(toh, EVT_ITER_VISIBLE | EVT_ITER_COVERED, NULL,
			      &ih);
	assert_rc_equal(rc, 0);

	rc = evt_iter_probe(ih, EVT_ITER_FIRST, NULL, NULL);
	assert_rc_equal(rc, 0);

	/* Ok, count the entries */
	while (!evt_iter_fetch(ih, &inob, &entry, NULL)) {
		rc = evt_iter_next(ih);
		if (rc == -DER_NONEXIST)
			break;
		assert_rc_equal(rc, 0);
		count++;
	}
	print_message("Number of entries is %d\n", count);
	rc = evt_iter_finish(ih);
	assert_rc_equal(rc, 0);

	rc = evt_destroy(toh);
	assert_rc_equal(rc, 0);

	assert_in_range(count, last, last * 3);
}

static void
test_evt_root_deactivate_bug(void **state)
{
	struct test_arg		*arg = *state;
	daos_handle_t		 toh;
	struct evt_entry_in	 entry_in = {0};
	int			 rc;

	rc = evt_create(arg->ta_root, ts_feats, ORDER_DEF_INTERNAL, arg->ta_uma,
			&ts_evt_desc_cbs, &toh);
	assert_rc_equal(rc, 0);

	entry_in.ei_ver = 0;
	entry_in.ei_inob = 0;
	/* We will insert nothing but holes as we are just checking sorted
	 * iteration.
	 */
	bio_alloc_init(arg->ta_utx, &entry_in.ei_addr, NULL, 0);

	insert_and_check(toh, &entry_in, 0, 1);

	rc = evt_delete(toh, &entry_in.ei_rect, NULL);
	assert_rc_equal(rc, 0);

	/* Insert it again now */
	insert_and_check(toh, &entry_in, 0, 1);

	rc = evt_destroy(toh);
	assert_rc_equal(rc, 0);
}

static void
test_evt_outer_punch(void **state)
{
	struct test_arg		*arg = *state;
	int			*value;
	daos_handle_t		 toh;
	daos_handle_t		 ih;
	struct evt_entry_in	 entry = {0};
	struct evt_entry	 ent;
	int			 rc;
	int			 epoch;
	int			 offset;
	int			 count;
	int			 sum, visible, covered;
	struct evt_filter	 filter = {0};
	uint32_t		 inob;

	rc = evt_create(arg->ta_root, ts_feats, ORDER_DEF_INTERNAL, arg->ta_uma,
			&ts_evt_desc_nofree_cbs, &toh);
	assert_rc_equal(rc, 0);
	sum = 1;

	/* Insert a bunch of entries */
	for (epoch = 1; epoch <= NUM_EPOCHS; epoch++) {
		for (count = 0; count < NUM_EXTENTS; count++) {
			offset = count * NUM_EXTENTS;
			entry.ei_rect.rc_ex.ex_lo = offset;
			entry.ei_rect.rc_ex.ex_hi = offset;
			entry.ei_rect.rc_epc = epoch;
			entry.ei_bound = epoch;
			memset(&entry.ei_csum, 0, sizeof(entry.ei_csum));
			entry.ei_ver = 0;
			entry.ei_inob = sizeof(offset);
			rc = bio_alloc_init(arg->ta_utx, &entry.ei_addr, &sum,
					    sizeof(sum));
			assert_int_equal(rc, 0);

			rc = evt_insert(toh, &entry, NULL);
			if (rc == 1)
				rc = 0;
			assert_rc_equal(rc, 0);
		}
	}

	filter.fr_ex.ex_lo = 0;
	filter.fr_ex.ex_hi = NUM_EPOCHS * NUM_EXTENTS;
	filter.fr_epr.epr_lo = 0;
	filter.fr_punch_epc = NUM_EPOCHS - 1;
	filter.fr_punch_minor_epc = EVT_MINOR_EPC_MAX;
	filter.fr_epr.epr_hi = DAOS_EPOCH_MAX;
	filter.fr_epoch = filter.fr_epr.epr_hi;
	rc = evt_iter_prepare(toh, EVT_ITER_VISIBLE | EVT_ITER_COVERED,
			      &filter, &ih);
	assert_rc_equal(rc, 0);

	rc = evt_iter_probe(ih, EVT_ITER_FIRST, NULL, NULL);
	assert_rc_equal(rc, 0);

	visible = covered = 0;

	for (;;) {
		rc = evt_iter_fetch(ih, &inob, &ent, NULL);
		assert_rc_equal(rc, 0);

		assert_false(bio_addr_is_hole(&ent.en_addr));

		value = utest_off2ptr(arg->ta_utx, ent.en_addr.ba_off);

		if (ent.en_visibility & EVT_COVERED) {
			covered += *value;
			assert_true(ent.en_epoch <= (NUM_EPOCHS - 1));
			assert_false(ent.en_visibility & EVT_VISIBLE);
		} else {
			visible += *value;
			assert_true(ent.en_epoch == NUM_EPOCHS);
			assert_false(ent.en_visibility & EVT_COVERED);
			assert_true(ent.en_visibility & EVT_VISIBLE);
		}

		rc = evt_iter_next(ih);
		if (rc == -DER_NONEXIST)
			break;
		assert_rc_equal(rc, 0);
	}
	assert_int_equal(visible, NUM_EXTENTS);
	assert_int_equal(covered, (NUM_EPOCHS - 1) * NUM_EXTENTS);

	rc = evt_iter_finish(ih);
	assert_rc_equal(rc, 0);

	/* If user specifies punch for unsorted iterator, it will mark
	 * punched entries EVT_COVERED
	 */
	rc = evt_iter_prepare(toh, 0, &filter, &ih);
	assert_rc_equal(rc, 0);

	rc = evt_iter_probe(ih, EVT_ITER_FIRST, NULL, NULL);
	assert_rc_equal(rc, 0);

	visible = covered = 0;

	for (;;) {
		rc = evt_iter_fetch(ih, &inob, &ent, NULL);
		assert_rc_equal(rc, 0);

		assert_false(bio_addr_is_hole(&ent.en_addr));

		value = utest_off2ptr(arg->ta_utx, ent.en_addr.ba_off);

		if (ent.en_visibility & EVT_COVERED) {
			covered += *value;
			assert_true(ent.en_epoch <= (NUM_EPOCHS - 1));
			assert_false(ent.en_visibility & EVT_VISIBLE);
		} else {
			visible += *value;
			assert_true(ent.en_epoch == NUM_EPOCHS);
		}

		rc = evt_iter_next(ih);
		if (rc == -DER_NONEXIST)
			break;
		assert_rc_equal(rc, 0);
	}
	assert_int_equal(visible, NUM_EXTENTS);
	assert_int_equal(covered, (NUM_EPOCHS - 1) * NUM_EXTENTS);

	rc = evt_iter_finish(ih);
	assert_rc_equal(rc, 0);

	rc = evt_destroy(toh);
	assert_rc_equal(rc, 0);
}

static void
test_dyn_root_yield(void **state)
{
	struct test_arg  *arg = *state;
	daos_handle_t     toh;
	daos_handle_t     ih[2];
	struct evt_entry  ent;
	int               rc;
	int               i;
	int               epoch  = 1;
	struct evt_filter filter = {0};
	uint32_t          inob;
	daos_anchor_t     anchor[2] = {0};

	rc = evt_create(arg->ta_root, ts_feats, ORDER_DEF_INTERNAL, arg->ta_uma,
			&ts_evt_desc_nofree_cbs, &toh);
	assert_rc_equal(rc, 0);

	rc = insert_str(arg, toh, epoch++, 0, "Hello World");
	assert_rc_equal(rc, 0);

	filter.fr_ex.ex_lo        = 0;
	filter.fr_ex.ex_hi        = 1000;
	filter.fr_punch_minor_epc = 0;
	filter.fr_epr.epr_hi      = DAOS_EPOCH_MAX;
	filter.fr_epoch           = filter.fr_epr.epr_hi;
	rc = evt_iter_prepare(toh, EVT_ITER_VISIBLE | EVT_ITER_COVERED, &filter, &ih[0]);
	assert_rc_equal(rc, 0);

	rc = evt_iter_prepare(toh, 0, &filter, &ih[1]);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		rc = evt_iter_probe(ih[i], EVT_ITER_FIRST, NULL, NULL);
		assert_rc_equal(rc, 0);
		rc = evt_iter_fetch(ih[i], &inob, &ent, &anchor[i]);
		assert_rc_equal(rc, 0);
		assert_false(bio_addr_is_hole(&ent.en_addr));
	}

	rc = insert_str(arg, toh, epoch++, 0, "Hello");
	assert_rc_equal(rc, 1);
	rc = insert_str(arg, toh, epoch++, 16, "Hello World");
	assert_rc_equal(rc, 0);
	rc = insert_str(arg, toh, epoch++, 32, "Hello World");
	assert_rc_equal(rc, 0);
	rc = insert_str(arg, toh, epoch++, 48, "Hello World");
	assert_rc_equal(rc, 0);
	rc = insert_str(arg, toh, epoch++, 64, "Hello World");
	assert_rc_equal(rc, 0);
	rc = insert_str(arg, toh, epoch++, 80, "Hello World");
	assert_rc_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		rc = evt_iter_probe(ih[i], EVT_ITER_FIND, NULL, &anchor[i]);
		assert_rc_equal(rc, 0);

		for (;;) {
			rc = evt_iter_fetch(ih[i], &inob, &ent, &anchor[i]);
			assert_rc_equal(rc, 0);
			assert_false(bio_addr_is_hole(&ent.en_addr));

			rc = evt_iter_next(ih[i]);
			if (rc == -DER_NONEXIST)
				break;
			assert_rc_equal(rc, 0);
		}
		rc = evt_iter_finish(ih[i]);
		assert_rc_equal(rc, 0);
	}

	rc = evt_destroy(toh);
	assert_rc_equal(rc, 0);
}

static int
run_internal_tests(char *test_name)
{
	static const struct CMUnitTest evt_builtin[] = {
	    {"EVT050: evt_iter_delete", test_evt_iter_delete, setup_builtin, teardown_builtin},
	    {"EVT051: evt_iter_delete_internal", test_evt_iter_delete_internal, setup_builtin,
	     teardown_builtin},
	    {"EVT052: evt_ent_alloc_bug", test_evt_ent_alloc_bug, setup_builtin, teardown_builtin},
	    {"EVT053: evt_root_deactivate_bug", test_evt_root_deactivate_bug, setup_builtin,
	     teardown_builtin},
	    {"EVT054: evt_iter_flags", test_evt_iter_flags, setup_builtin, teardown_builtin},
	    {"EVT055: evt_find_internal", test_evt_find_internal, setup_builtin, teardown_builtin},
	    {"EVT015: evt_overlap_split_internal", test_evt_overlap_split_internal, setup_builtin,
	     teardown_builtin},
	    {"EVT016: evt_variable_record_size_internal", test_evt_variable_record_size_internal,
	     setup_builtin, teardown_builtin},
	    {"EVT017: evt_iter_outer_punch", test_evt_outer_punch, setup_builtin, teardown_builtin},
	    {"EVT018: evt_node_size_internal", test_evt_node_size_internal, setup_builtin,
	     teardown_builtin},
	    {"EVT019: evt_various_data_size_internal", test_evt_various_data_size_internal,
	     setup_builtin, teardown_builtin},
	    {"EVT020: evt_agg_check", test_evt_agg_check, setup_builtin, teardown_builtin},
	    {"EVT021: dynamic root change during yield", test_dyn_root_yield, setup_builtin,
	     teardown_builtin},
	    {NULL, NULL, NULL, NULL}};

	return cmocka_run_group_tests_name(test_name, evt_builtin,
					   global_setup, global_teardown);
}

static struct option ts_ops[] = {
	{ "create",	required_argument,	NULL,	'C'	},
	{ "destroy",	no_argument,		NULL,	'D'	},
	{ "open",	no_argument,		NULL,	'o'	},
	{ "close",	no_argument,		NULL,	'c'	},
	{ "drain",	required_argument,	NULL,	'e'	},
	{ "add",	required_argument,	NULL,	'a'	},
	{ "many_add",	required_argument,	NULL,	'm'	},
	{ "find",	required_argument,	NULL,	'f'	},
	{ "remove_all",	required_argument,	NULL,	'r'	},
	{ "delete",	required_argument,	NULL,	'd'	},
	{ "list",	optional_argument,	NULL,	'l'	},
	{ "debug",	required_argument,	NULL,	'b'	},
	{ "test",	required_argument,	NULL,	't'	},
	{ "sort",	required_argument,	NULL,	's'	},
	{ NULL,		0,			NULL,	0	},
};

static int
ts_cmd_run(char opc, char *args)
{
	int	 rc = 0;

	tst_fn_val.optval = args;
	tst_fn_val.input = true;

	switch (opc) {
	case 'C':
		ts_open_create();
		break;
	case 'D':
		ts_close_destroy();
		break;
	case 'o':
		tst_fn_val.input = false;
		tst_fn_val.optval = NULL;
		ts_open_create();
		break;
	case 'c':
		tst_fn_val.input = false;
		ts_close_destroy();
		break;
	case 'a':
		ts_add_rect();
		break;
	case 'm':
		ts_many_add();
		break;
	case 'e':
		ts_drain();
		break;
	case 'f':
		ts_find_rect();
		break;
	case 'l':
		ts_list_rect();
		break;
	case 'd':
		ts_delete_rect();
		break;
	case 'r':
		ts_remove_rect();
		break;
	case 'b':
		ts_tree_debug();
		break;
	case 't':
		break;
	case 's':
		if (strcasecmp(args, "soff") == 0)
			ts_feats = EVT_FEAT_SORT_SOFF;
		else if (strcasecmp(args, "dist_even") == 0)
			ts_feats = EVT_FEAT_SORT_DIST_EVEN;
		break;
	default:
		D_PRINT("Unsupported command %c\n", opc);
		rc = 0;
		break;
	}

	return rc;
}

static void
ts_group(void **state)
{

	int	opc = 0;

	while ((opc = getopt_long(test_group_argc, test_group_args,
				  "C:a:m:e:f:g:d:b:Docl::ts:r:", ts_ops, NULL)) != -1) {
		ts_cmd_run(opc, optarg);
	}
}

static int
run_cmd_line_test(char *test_name, char **args, int argc)
{

	const struct CMUnitTest evt_test[] = {
		{ test_name, ts_group, NULL, NULL},
	};

	test_group_args = args;
	test_group_argc = argc;

	return cmocka_run_group_tests_name(test_name,
					   evt_test,
					   NULL,
					   NULL);
}

int
main(int argc, char **argv)
{
	struct timeval	tv;
	int		rc;
	int		j;
	int		start_idx;
	char           *test_name;

	d_register_alt_assert(mock_assert);

	gettimeofday(&tv, NULL);
	srand(tv.tv_usec);

	ts_toh = DAOS_HDL_INVAL;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0)
		return rc;

	/* Capture test_name args if any */
	start_idx = 0;
	test_name = "evtree default test suite name";
	if (argc > 1) {
		/* Get test suite variables */
		if (strcmp(argv[1], "--start-test") == 0) {
			start_idx = 2;
			test_name = argv[start_idx];
		}
	}
	optind = start_idx;
	/* Create pmem pool */
	rc = utest_pmem_create(POOL_NAME, POOL_SIZE, sizeof(*ts_root), NULL, &ts_utx);
	if (rc != 0) {
		perror("Evtree test couldn't create pool");
		return rc;
	}

	ts_root = utest_utx2root(ts_utx);
	ts_uma = utest_utx2uma(ts_utx);

	/* Start interactive session*/
	if ((argc - optind) == 1) {
		print_message("Starting interactive session...\n");
		rc = cmd_parser(ts_ops, "$ > ", ts_cmd_run);
		goto out;
	}

	/* Execute Internal tests in the command */
	for (j = 0; j < argc; j++) {
		if (strcmp(argv[j], "-t") == 0) {
			rc = run_internal_tests(test_name);
			if (rc != 0)
				D_PRINT("Internal tests failed rc="DF_RC"\n",
					DP_RC(rc));
			goto out;
		}
	}

	/* Execute the sequence of tests */
	rc = run_cmd_line_test(test_name, argv, argc);
 out:
	daos_debug_fini();
	rc += utest_utx_destroy(ts_utx);
	return rc;
}
