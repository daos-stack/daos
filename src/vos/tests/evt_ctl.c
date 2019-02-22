/**
 * (C) Copyright 2017-2019 Intel Corporation.
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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

#include <daos_srv/evtree.h>
#include <daos_srv/bio.h>
#include <daos/tests_lib.h>
#include <utest_common.h>

/**
 * An example for integer key evtree .
 */

static struct utest_context	*ts_utx;
static struct umem_attr		*ts_uma;

#define ORDER_DEF		16

static int			ts_order = ORDER_DEF;

static TMMID(struct evt_root)	ts_root_mmid;
static struct evt_root		*ts_root;
static daos_handle_t		ts_toh;

#define EVT_SEP			','
#define EVT_SEP_VAL		':'
#define EVT_SEP_EXT		'-'
#define EVT_SEP_EPC		'@'

static int
ts_open_create(bool create, char *args)
{
	bool	inplace = false;
	int	rc;

	if (!daos_handle_is_inval(ts_toh)) {
		D_PRINT("Tree has been opened\n");
		return -1;
	}

	if (create && args != NULL) {
		if (args[0] == 'i') { /* inplace create/open */
			inplace = true;
			if (args[1] != EVT_SEP) {
				D_PRINT("wrong parameter format %s\n", args);
				return -1;
			}
			args += 2;
		}

		if (args[0] != 'o' || args[1] != EVT_SEP_VAL) {
			D_PRINT("incorrect format for tree order: %s\n", args);
			return -1;
		}

		ts_order = atoi(&args[2]);
		if (ts_order < EVT_ORDER_MIN || ts_order > EVT_ORDER_MAX) {
			D_PRINT("Invalid tree order %d\n", ts_order);
			return -1;
		}

	} else if (!create) {
		inplace = (ts_root->tr_feats != 0);
		if (TMMID_IS_NULL(ts_root_mmid) && !inplace) {
			D_PRINT("Please create tree first\n");
			return -1;
		}
	}

	if (create) {
		D_PRINT("Create evtree with order %d%s\n",
			ts_order, inplace ? " inplace" : "");
		if (inplace) {
			rc = evt_create_inplace(EVT_FEAT_DEFAULT, ts_order,
						ts_uma, ts_root,
						DAOS_HDL_INVAL, &ts_toh);
		} else {
			rc = evt_create(EVT_FEAT_DEFAULT, ts_order, ts_uma,
					&ts_root_mmid, &ts_toh);
		}
	} else {
		D_PRINT("Open evtree %s\n", inplace ? " inplace" : "");
		if (inplace)
			rc = evt_open_inplace(ts_root, ts_uma, DAOS_HDL_INVAL,
					      NULL, &ts_toh);
		else
			rc = evt_open(ts_root_mmid, ts_uma, &ts_toh);
	}

	if (rc != 0) {
		D_PRINT("Tree %s failed: %d\n", create ? "create" : "open", rc);
		return -1;
	}
	return 0;
}

static int
ts_close_destroy(bool destroy)
{
	int rc;

	if (daos_handle_is_inval(ts_toh)) {
		D_PRINT("Invalid tree open handle\n");
		return -1;
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
		D_PRINT("Tree %s failed: %d\n",
			destroy ? "destroy" : "close", rc);
		return -1;
	}
	return rc;
}

static int
ts_parse_rect(char *str, struct evt_rect *rect, daos_epoch_t *high,
	      char **val_p, bool *should_pass)
{
	char	*tmp;

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
	rect->rc_ex.ex_lo = atoi(str);

	tmp = strchr(str, EVT_SEP_EXT);
	if (tmp == NULL) {
		D_PRINT("Invalid input string %s\n", str);
		return -1;
	}

	str = tmp + 1;
	rect->rc_ex.ex_hi = atoi(str);
	tmp = strchr(str, EVT_SEP_EPC);
	if (tmp == NULL) {
		D_PRINT("Invalid input string %s\n", str);
		return -1;
	}

	str = tmp + 1;
	rect->rc_epc = atoi(str);

	if (high) {
		*high = DAOS_EPOCH_MAX;
		tmp = strchr(str, EVT_SEP_EXT);
		if (tmp == NULL)
			goto parse_value;
		str = tmp + 1;
		*high = atoi(str);
	}

parse_value:
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
	umem_id_t	mmid;

	addr->ba_type = DAOS_MEDIA_SCM;
	if (src == NULL) {
		addr->ba_hole = 1;
		return 0;
	}

	rc = utest_alloc(utx, &mmid, size, init_mem, src);

	if (rc != 0)
		goto end;

	addr->ba_off = mmid.off;
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

static int
ts_add_rect(char *args)
{
	char			*val;
	bio_addr_t		 bio_addr = {0}; /* Fake bio addr */
	struct evt_entry_in	 entry;
	int			 rc;
	bool			 should_pass;
	static int		 total_added;

	if (args == NULL)
		return -1;

	rc = ts_parse_rect(args, &entry.ei_rect, NULL, &val, &should_pass);
	if (rc != 0)
		return -1;

	D_PRINT("Insert "DF_RECT": val=%s expect_pass=%s (total in tree=%d)\n",
		DP_RECT(&entry.ei_rect), val ? val : "<NULL>",
		should_pass ? "true" : "false", total_added);


	rc = bio_strdup(ts_utx, &bio_addr, val);
	if (rc != 0) {
		D_FATAL("Insufficient memory for test\n");
		return rc;
	}
	entry.ei_addr = bio_addr;
	entry.ei_ver = 0;
	entry.ei_inob = val == NULL ? 0 : 1;

	rc = evt_insert(ts_toh, &entry);
	if (rc == 0)
		total_added++;
	if (should_pass) {
		if (rc != 0)
			D_FATAL("Add rect failed %d\n", rc);
	} else {
		if (rc == 0) {
			D_FATAL("Add rect should have failed\n");
			return -1;
		}
		rc = 0;
	}

	return rc;
}

static int
ts_delete_rect(char *args)
{
	char			*val;
	struct evt_entry	 ent;
	struct evt_rect		 rect;
	int			 rc;
	bool			 should_pass;
	static int		 total_deleted;

	if (args == NULL)
		return -1;

	rc = ts_parse_rect(args, &rect, NULL, &val, &should_pass);
	if (rc != 0)
		return -1;

	D_PRINT("Delete "DF_RECT": val=%s expect_pass=%s (total in tree=%d)\n",
		DP_RECT(&rect), val ? val : "<NULL>",
		should_pass ? "true" : "false", total_deleted);

	rc = evt_delete(ts_toh, &rect, &ent);

	if (rc == 0)
		total_deleted++;

	if (should_pass) {
		if (rc != 0)
			D_FATAL("Delete rect failed %d\n", rc);
		else if (evt_rect_width(&rect) !=
			 evt_extent_width(&ent.en_sel_ext)) {
			rc = 1;
			D_FATAL("Returned rectangle width doesn't match\n");
		}

		if (!bio_addr_is_hole(&ent.en_addr)) {
			umem_id_t	mmid;

			mmid = utest_off2mmid(ts_utx, ent.en_addr.ba_off);
			utest_free(ts_utx, mmid);
		}
	} else {
		if (rc == 0) {
			D_FATAL("Delete rect should have failed\n");
			return -1;
		}
		rc = 0;
	}

	return rc;
}

static int
ts_find_rect(char *args)
{
	struct evt_entry	*ent;
	char			*val;
	bio_addr_t		 addr;
	struct evt_rect		 rect;
	struct evt_entry_array	 ent_array;
	int			 rc;
	bool			 should_pass;

	if (args == NULL)
		return -1;

	rc = ts_parse_rect(args, &rect, NULL, &val, &should_pass);
	if (rc != 0)
		return -1;

	D_PRINT("Search rectangle "DF_RECT"\n", DP_RECT(&rect));

	evt_ent_array_init(&ent_array);
	rc = evt_find(ts_toh, &rect, &ent_array);
	if (rc != 0)
		D_FATAL("Add rect failed %d\n", rc);

	evt_ent_array_for_each(ent, &ent_array) {
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

	evt_ent_array_fini(&ent_array);
	return rc;
}

static int
ts_list_rect(char *args)
{
	char			*val;
	daos_anchor_t		 anchor;
	struct evt_filter	 filter;
	struct evt_rect		 rect;
	daos_epoch_t		 high = DAOS_EPOCH_MAX;
	daos_handle_t		 ih;
	int			 i;
	int			 rc;
	int			 options = 0;
	bool			 probe = true;

	if (args == NULL) {
		filter.fr_ex.ex_lo = 0;
		filter.fr_ex.ex_hi = ~(0ULL);
		filter.fr_epr.epr_lo = 0;
		filter.fr_epr.epr_hi = DAOS_EPOCH_MAX;
		goto start;
	}

	rc = ts_parse_rect(args, &rect, &high, &val, NULL);
	if (rc != 0)
		return -1;
	filter.fr_ex = rect.rc_ex;
	filter.fr_epr.epr_lo = rect.rc_epc;
	filter.fr_epr.epr_hi = high;
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
	case 'B':
		options |= EVT_ITER_EMBEDDED;
	case 'b':
		options |= (EVT_ITER_VISIBLE | EVT_ITER_COVERED);
		/* Don't skip the probe in this case just to test that path */
		break;
	default:
		D_PRINT("Unknown iterator type: %c\n", val[0]);
		return -1;
	}

start:
	rc = evt_iter_prepare(ts_toh, options, &filter, &ih);
	if (rc != 0) {
		D_PRINT("Failed to prepare iterator: %d\n", rc);
		return -1;
	}

	rc = evt_iter_probe(ih, EVT_ITER_FIRST, NULL, NULL);
	if (rc == -DER_NONEXIST)
		D_GOTO(out, rc = 0);

	if (rc != 0) {
		D_PRINT("Failed to probe: %d\n", rc);
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
				return -1;
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
	}
 out:
	evt_iter_finish(ih);
	return 0;
}

#define TS_VAL_CYCLE	4

static int
ts_many_add(char *args)
{
	char			*buf;
	char			*tmp;
	int			*seq;
	struct evt_rect		*rect;
	struct evt_entry_in	 entry;
	bio_addr_t		 bio_addr = {0}; /* Fake bio addr */
	long			 offset = 0;
	int			 size;
	int			 nr;
	int			 i;
	int			 rc;

	/* argument format: "s:NUM,e:NUM,n:NUM"
	 * s: start offset
	 * e: extent size
	 * n: number of extents
	 */
	if (args[0] == 's') {
		if (args[1] != EVT_SEP_VAL) {
			D_PRINT("Invalid parameter %s\n", args);
			return -1;
		}
		offset = strtol(&args[2], &tmp, 0);
		if (*tmp != EVT_SEP) {
			D_PRINT("Invalid parameter %s\n", args);
			return -1;
		}
		args = tmp + 1;
	}

	if (args[0] != 'e' || args[1] != EVT_SEP_VAL) {
		D_PRINT("Invalid parameter %s\n", args);
		return -1;
	}

	size = strtol(&args[2], &tmp, 0);
	if (size <= 0) {
		D_PRINT("Invalid extent size %d\n", size);
		return -1;
	}
	if (*tmp != EVT_SEP) {
		D_PRINT("Invalid parameter %s\n", args);
		return -1;
	}
	args = tmp + 1;

	if (args[0] != 'n' || args[1] != EVT_SEP_VAL) {
		D_PRINT("Invalid parameter %s\n", args);
		return -1;
	}
	nr = strtol(&args[2], &tmp, 0);
	if (nr <= 0) {
		D_PRINT("Invalid extent number %d\n", nr);
		return -1;
	}

	D_ALLOC(buf, size);
	if (!buf)
		return -1;

	seq = dts_rand_iarr_alloc(nr, 0);
	if (!seq) {
		D_FREE(buf);
		return -1;
	}

	rect = &entry.ei_rect;

	for (i = 0; i < nr; i++) {
		rect->rc_ex.ex_lo = offset + seq[i] * size;
		rect->rc_ex.ex_hi = rect->rc_ex.ex_lo + size - 1;
		rect->rc_epc = (seq[i] % TS_VAL_CYCLE) + 1;

		memset(buf, 'a' + seq[i] % TS_VAL_CYCLE, size);

		rc = bio_strdup(ts_utx, &bio_addr, buf);
		if (rc != 0) {
			D_FATAL("Insufficient memory for test\n");
			return rc;
		}
		entry.ei_addr = bio_addr;
		entry.ei_ver = 0;
		entry.ei_inob = 1;

		rc = evt_insert(ts_toh, &entry);
		if (rc != 0) {
			D_FATAL("Add rect %d failed %d\n", i, rc);
			break;
		}
	}

	D_FREE(buf);
	D_FREE(seq);
	return rc;
}

static int
ts_tree_debug(char *args)
{
	int	level;

	level = atoi(args);
	evt_debug(ts_toh, level);
	return 0;
}

#define POOL_NAME "/mnt/daos/evtree-utest"
#define POOL_SIZE ((1024 * 1024  * 1024ULL))

struct test_arg {
	struct utest_context	*ta_utx;
	struct umem_attr	*ta_uma;
	struct evt_root		*ta_root;
	char			*ta_pool_name;
};

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
			       sizeof(*arg->ta_root), &arg->ta_utx);
	if (rc != 0) {
		perror("Evtree internal test couldn't create pool");
		rc = 1;
		goto failed;
	}

	arg->ta_root = utest_utx2root(arg->ta_utx);
	arg->ta_uma = utest_utx2uma(arg->ta_utx);

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

static void
test_evt_iter_delete(void **state)
{
	TMMID(struct evt_root)	 root_mmid;
	struct test_arg		*arg = *state;
	int			*value;
	umem_id_t		 mmid;
	daos_handle_t		 toh;
	daos_handle_t		 ih;
	struct evt_entry_in	 entry = {0};
	struct evt_entry	 ent;
	int			 rc;
	int			 epoch;
	int			 offset;
	int			 sum, expected_sum;
	struct evt_filter	 filter;
	uint32_t		 inob;

	root_mmid = TMMID_NULL(struct evt_root);

	rc = evt_create(EVT_FEAT_DEFAULT, 13, arg->ta_uma, &root_mmid, &toh);
	assert_int_equal(rc, 0);

	/* Insert a bunch of entries */
	for (epoch = 1; epoch <= NUM_EPOCHS; epoch++) {
		for (offset = epoch; offset < NUM_EXTENTS + epoch; offset++) {
			entry.ei_rect.rc_ex.ex_lo = offset;
			entry.ei_rect.rc_ex.ex_hi = offset;
			entry.ei_rect.rc_epc = epoch;
			entry.ei_csum = 0;
			entry.ei_ver = 0;
			entry.ei_inob = sizeof(offset);
			sum = offset - epoch + 1;
			rc = bio_alloc_init(arg->ta_utx, &entry.ei_addr, &sum,
					    sizeof(sum));
			assert_int_equal(rc, 0);

			rc = evt_insert(toh, &entry);
			assert_int_equal(rc, 0);
		}
	}

	rc = evt_iter_prepare(toh, EVT_ITER_VISIBLE, NULL, &ih);
	assert_int_equal(rc, 0);
	rc = evt_iter_probe(ih, EVT_ITER_FIRST, NULL, NULL);
	assert_int_equal(rc, 0);
	sum = 0;
	for (;;) {
		rc = evt_iter_fetch(ih, &inob, &ent, NULL);
		if (rc == -DER_NONEXIST)
			break;
		assert_int_equal(rc, 0);
		assert_int_equal(inob, sizeof(sum));

		value = utest_off2ptr(arg->ta_utx, ent.en_addr.ba_off);
		sum += *value;

		rc = evt_iter_next(ih);
		if (rc == -DER_NONEXIST)
			break;
		assert_int_equal(rc, 0);
	}
	expected_sum = NUM_EPOCHS + (NUM_EXTENTS * (NUM_EXTENTS + 1) / 2) - 1;
	assert_int_equal(expected_sum, sum);
	rc = evt_iter_finish(ih);
	assert_int_equal(rc, 0);


	filter.fr_ex.ex_lo = 0;
	filter.fr_ex.ex_hi = NUM_EPOCHS + NUM_EXTENTS;
	filter.fr_epr.epr_lo = NUM_EPOCHS - NUM_PARTIAL + 1;
	filter.fr_epr.epr_hi = DAOS_EPOCH_MAX;
	rc = evt_iter_prepare(toh, 0, &filter, &ih);
	assert_int_equal(rc, 0);

	rc = evt_iter_probe(ih, EVT_ITER_FIRST, NULL, NULL);
	assert_int_equal(rc, 0);

	sum = 0;

	/* Delete some of the entries */
	for (;;) {
		bio_addr_t addr;

		rc = evt_iter_delete(ih, &addr);
		if (rc == -DER_NONEXIST)
			break;

		assert_int_equal(rc, 0);

		assert_false(bio_addr_is_hole(&addr));

		mmid = utest_off2mmid(arg->ta_utx, addr.ba_off);
		value = utest_off2ptr(arg->ta_utx, addr.ba_off);

		sum += *value;
		utest_free(arg->ta_utx, mmid);
	}
	expected_sum = NUM_PARTIAL * (NUM_EXTENTS * (NUM_EXTENTS + 1) / 2);
	assert_int_equal(expected_sum, sum);

	rc = evt_iter_finish(ih);
	assert_int_equal(rc, 0);

	/* No filter so delete everything */
	rc = evt_iter_prepare(toh, 0, NULL, &ih);
	assert_int_equal(rc, 0);

	rc = evt_iter_probe(ih, EVT_ITER_FIRST, NULL, NULL);
	assert_int_equal(rc, 0);

	/* Ok, delete the rest */
	while (!evt_iter_empty(ih)) {
		bio_addr_t addr;

		rc = evt_iter_delete(ih, &addr);
		assert_int_equal(rc, 0);

		assert_false(bio_addr_is_hole(&addr));

		mmid = utest_off2mmid(arg->ta_utx, addr.ba_off);
		value = utest_off2ptr(arg->ta_utx, addr.ba_off);

		sum += *value;
		utest_free(arg->ta_utx, mmid);
	}
	rc = evt_iter_finish(ih);
	assert_int_equal(rc, 0);

	expected_sum = NUM_EPOCHS * (NUM_EXTENTS * (NUM_EXTENTS + 1) / 2);
	assert_int_equal(expected_sum, sum);

	rc = evt_destroy(toh);
	assert_int_equal(rc, 0);
}

static void
test_evt_iter_delete_internal(void **state)
{
	TMMID(struct evt_root)	 root_mmid;
	struct test_arg		*arg = *state;
	daos_handle_t		 toh;
	daos_handle_t		 ih;
	struct evt_entry_in	 entry = {0};
	int			 rc;
	int			 epoch;
	int			 offset;

	root_mmid = TMMID_NULL(struct evt_root);

	rc = evt_create(EVT_FEAT_DEFAULT, 13, arg->ta_uma, &root_mmid, &toh);
	assert_int_equal(rc, 0);

	/* Insert a bunch of entries */
	for (epoch = 1; epoch <= NUM_EPOCHS; epoch++) {
		for (offset = epoch; offset < NUM_EXTENTS + epoch; offset++) {
			entry.ei_rect.rc_ex.ex_lo = offset;
			entry.ei_rect.rc_ex.ex_hi = offset;
			entry.ei_rect.rc_epc = epoch;
			entry.ei_csum = 0;
			entry.ei_ver = 0;
			entry.ei_inob = sizeof(offset);
			rc = bio_alloc_init(arg->ta_utx, &entry.ei_addr,
					    &offset, sizeof(offset));
			assert_int_equal(rc, 0);

			rc = evt_insert(toh, &entry);
			assert_int_equal(rc, 0);
		}
	}

	/* No filter so delete everything */
	rc = evt_iter_prepare(toh, 0, NULL, &ih);
	assert_int_equal(rc, 0);

	rc = evt_iter_probe(ih, EVT_ITER_FIRST, NULL, NULL);
	assert_int_equal(rc, 0);

	/* Ok, delete the rest */
	while (!evt_iter_empty(ih)) {
		rc = evt_iter_delete(ih, NULL);
		assert_int_equal(rc, 0);
	}
	rc = evt_iter_finish(ih);
	assert_int_equal(rc, 0);

	rc = evt_destroy(toh);
	assert_int_equal(rc, 0);
}

static int
run_internal_tests(void)
{
	static const struct CMUnitTest evt_builtin[] = {
		{ "EVT001: evt_iter_delete", test_evt_iter_delete,
			setup_builtin, teardown_builtin},
		{ "EVT002: evt_iter_delete_internal",
			test_evt_iter_delete_internal,
			setup_builtin, teardown_builtin},
		{ NULL, NULL, NULL, NULL }
	};

	return cmocka_run_group_tests_name("evtree built-in tests", evt_builtin,
					   global_setup, global_teardown);
}


static struct option ts_ops[] = {
	{ "create",	required_argument,	NULL,	'C'	},
	{ "destroy",	no_argument,		NULL,	'D'	},
	{ "open",	no_argument,		NULL,	'o'	},
	{ "close",	no_argument,		NULL,	'c'	},
	{ "add",	required_argument,	NULL,	'a'	},
	{ "many_add",	required_argument,	NULL,	'm'	},
	{ "find",	required_argument,	NULL,	'f'	},
	{ "delete",	required_argument,	NULL,	'd'	},
	{ "list",	optional_argument,	NULL,	'l'	},
	{ "debug",	required_argument,	NULL,	'b'	},
	{ "test",	required_argument,	NULL,	't'	},
	{ NULL,		0,			NULL,	0	},
};

static int
ts_cmd_run(char opc, char *args)
{
	int	rc;

	switch (opc) {
	case 'C':
		rc = ts_open_create(true, args);
		break;
	case 'D':
		rc = ts_close_destroy(true);
		break;
	case 'o':
		rc = ts_open_create(false, NULL);
		break;
	case 'c':
		rc = ts_close_destroy(false);
		break;
	case 'a':
		rc = ts_add_rect(args);
		break;
	case 'm':
		rc = ts_many_add(args);
		break;
	case 'f':
		rc = ts_find_rect(args);
		break;
	case 'l':
		rc = ts_list_rect(args);
		break;
	case 'd':
		rc = ts_delete_rect(args);
		break;
	case 'b':
		rc = ts_tree_debug(args);
		break;
	case 't':
		rc = run_internal_tests();
		break;
	default:
		D_PRINT("Unsupported command %c\n", opc);
		rc = 0;
		break;
	}
	if (rc != 0)
		D_PRINT("opc=%d failed with rc=%d\n", opc, rc);

	return rc;
}

int
main(int argc, char **argv)
{
	struct timeval	tv;
	int		rc;

	gettimeofday(&tv, NULL);
	srand(tv.tv_usec);

	ts_toh = DAOS_HDL_INVAL;

	ts_root_mmid = TMMID_NULL(struct evt_root);

	rc = daos_debug_init(NULL);
	if (rc != 0)
		return rc;

	optind = 0;
	if (argc > 1 && strcmp(argv[1], "pmem") == 0) {
		optind = 1;
		rc = utest_pmem_create(POOL_NAME, POOL_SIZE, sizeof(*ts_root),
				       &ts_utx);
	} else {
		rc = utest_vmem_create(sizeof(*ts_root), &ts_utx);
	}

	if (rc != 0)
		return rc;

	ts_root = utest_utx2root(ts_utx);
	ts_uma = utest_utx2uma(ts_utx);

	if ((argc - optind) == 1) {
		rc = dts_cmd_parser(ts_ops, "$ > ", ts_cmd_run);
		goto out;
	}

	while ((rc = getopt_long(argc, argv, "C:a:m:f:g:d:b:Docl::t",
				 ts_ops, NULL)) != -1) {
		rc = ts_cmd_run(rc, optarg);
		if (rc != 0)
			goto out;
	}
	rc = 0;
 out:
	daos_debug_fini();
	rc += utest_utx_destroy(ts_utx);
	return rc;
}
