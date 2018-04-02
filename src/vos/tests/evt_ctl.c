/**
 * (C) Copyright 2017 Intel Corporation.
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
#include <daos/tests_lib.h>

/**
 * An example for integer key evtree .
 */

static struct umem_attr	ts_uma = {
	/* XXX pmem */
	.uma_id			= UMEM_CLASS_VMEM,
};

#define ORDER_DEF		16

static int			ts_order = ORDER_DEF;

static TMMID(struct evt_root)	ts_root_mmid;
static struct evt_root		ts_root;
static daos_handle_t		ts_toh;
static uuid_t			ts_uuid;

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
		D__PRINT("Tree has been opened\n");
		return -1;
	}

	if (create && args != NULL) {
		if (args[0] == 'i') { /* inplace create/open */
			inplace = true;
			if (args[1] != EVT_SEP) {
				D__PRINT("wrong parameter format %s\n", args);
				return -1;
			}
			args += 2;
		}

		if (args[0] != 'o' || args[1] != EVT_SEP_VAL) {
			D__PRINT("incorrect format for tree order: %s\n", args);
			return -1;
		}

		ts_order = atoi(&args[2]);
		if (ts_order < EVT_ORDER_MIN || ts_order > EVT_ORDER_MAX) {
			D__PRINT("Invalid tree order %d\n", ts_order);
			return -1;
		}

	} else if (!create) {
		inplace = (ts_root.tr_feats != 0);
		if (TMMID_IS_NULL(ts_root_mmid) && !inplace) {
			D__PRINT("Please create tree first\n");
			return -1;
		}
	}

	if (create) {
		D__PRINT("Create evtree with order %d%s\n",
			ts_order, inplace ? " inplace" : "");
		if (inplace) {
			rc = evt_create_inplace(EVT_FEAT_DEFAULT, ts_order,
						&ts_uma, &ts_root, &ts_toh);
		} else {
			rc = evt_create(EVT_FEAT_DEFAULT, ts_order, &ts_uma,
					&ts_root_mmid, &ts_toh);
		}
	} else {
		D__PRINT("Open evtree %s\n", inplace ? " inplace" : "");
		if (inplace)
			rc = evt_open_inplace(&ts_root, &ts_uma, &ts_toh);
		else
			rc = evt_open(ts_root_mmid, &ts_uma, &ts_toh);
	}

	if (rc != 0) {
		D__PRINT("Tree %s failed: %d\n", create ? "create" : "open", rc);
		return -1;
	}
	return 0;
}

static int
ts_close_destroy(bool destroy)
{
	int rc;

	if (daos_handle_is_inval(ts_toh)) {
		D__PRINT("Invalid tree open handle\n");
		return -1;
	}

	if (destroy) {
		D__PRINT("Destroy evtree\n");
		rc = evt_destroy(ts_toh);
	} else {
		D__PRINT("Close evtree\n");
		rc = evt_close(ts_toh);
	}

	ts_toh = DAOS_HDL_INVAL;
	if (rc != 0) {
		D__PRINT("Tree %s failed: %d\n",
			destroy ? "destroy" : "close", rc);
		return -1;
	}
	return rc;
}

static int
ts_parse_rect(char *str, struct evt_rect *rect, char **val_p)
{
	char	*tmp;

	rect->rc_off_lo = atoi(str);
	tmp = strchr(str, EVT_SEP_EXT);
	if (tmp == NULL) {
		D__PRINT("Invalid input string %s\n", str);
		return -1;
	}

	str = tmp + 1;
	rect->rc_off_hi = atoi(str);
	tmp = strchr(str, EVT_SEP_EPC);
	if (tmp == NULL) {
		D__PRINT("Invalid input string %s\n", str);
		return -1;
	}

	str = tmp + 1;
	rect->rc_epc_lo = atoi(str);
	tmp = strchr(str, EVT_SEP_EXT);
	if (tmp != NULL) {
		str = tmp + 1;
		rect->rc_epc_hi = atoi(str);
	} else {
		rect->rc_epc_hi = val_p == NULL ?
				  rect->rc_epc_lo : DAOS_EPOCH_MAX;
	}

	if (val_p == NULL) /* called by evt_find */
		return 0;

	tmp = strchr(str, EVT_SEP_VAL);
	if (tmp == NULL) {
		*val_p = NULL; /* punch */
		return 0;
	}

	str = tmp + 1;
	if (strlen(str) != evt_rect_width(rect)) {
		D__PRINT("Length of string cannot match extent size %d/%d\n",
			(int)strlen(str), (int)evt_rect_width(rect));
		return -1;
	}
	*val_p = str;
	return 0;
}

static int
ts_add_rect(char *args)
{
	char		*val;
	struct evt_rect	 rect;
	daos_sg_list_t	 sgl;
	daos_iov_t	 iov;
	int		 rc;

	if (args == NULL)
		return -1;

	rc = ts_parse_rect(args, &rect, &val);
	if (rc != 0)
		return -1;

	D__PRINT("Insert "DF_RECT": val=%s\n", DP_RECT(&rect),
		val ? val : "<NULL>");

	daos_iov_set(&iov, val, rect.rc_off_hi - rect.rc_off_lo + 1);
	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;

	rc = evt_insert_sgl(ts_toh, ts_uuid, 0, &rect, val ? 1 : 0, &sgl);
	if (rc != 0)
		D_FATAL("Add rect failed %d\n", rc);

	return rc;
}

static int
ts_find_rect(char *args)
{
	struct evt_entry	*ent;
	struct evt_rect		 rect;
	struct evt_entry_list	 enlist;
	int			 rc;

	if (args == NULL)
		return -1;

	rc = ts_parse_rect(args, &rect, NULL);
	if (rc != 0)
		return -1;

	D__PRINT("Search rectangle "DF_RECT"\n", DP_RECT(&rect));

	evt_ent_list_init(&enlist);
	rc = evt_find(ts_toh, &rect, &enlist);
	if (rc != 0)
		D_FATAL("Add rect failed %d\n", rc);

	evt_ent_list_for_each(ent, &enlist) {
		D__PRINT("Find rect "DF_RECT", val=%s\n",
			DP_RECT(&ent->en_rect),
			ent->en_addr ? (char *)ent->en_addr : "<NULL>");
	}

	evt_ent_list_fini(&enlist);
	return rc;
}

static int
ts_list_rect(void)
{
	daos_handle_t	ih;
	int		i;
	int		rc;

	rc = evt_iter_prepare(ts_toh, 0, &ih);
	if (rc != 0) {
		D__PRINT("Failed to prepare iterator: %d\n", rc);
		return -1;
	}

	rc = evt_iter_probe(ih, EVT_ITER_FIRST, NULL, NULL);
	if (rc == -DER_NONEXIST)
		D__GOTO(out, rc = 0);

	if (rc != 0) {
		D__PRINT("Failed to probe: %d\n", rc);
		D__GOTO(out, rc);
	}

	for (i = 0;; i++) {
		struct evt_entry ent;
		daos_hash_out_t	 anchor;

		rc = evt_iter_fetch(ih, &ent, &anchor);
		if (rc == 0) {
			D__PRINT("%d) "DF_RECT", val=%s\n",
				i, DP_RECT(&ent.en_rect),
				ent.en_addr ? (char *)ent.en_addr : "<NULL>");

			if (i % 3 == 0)
				rc = evt_iter_probe(ih, EVT_ITER_FIND,
						    &ent.en_rect, NULL);
			if (i % 3 == 1)
				rc = evt_iter_probe(ih, EVT_ITER_FIND,
						    NULL, &anchor);
		}

		if (rc == -DER_NONEXIST) {
			D__PRINT("Found %d entries\n", i);
			D__GOTO(out, rc = 0);
		}

		if (rc != 0)
			D__GOTO(out, rc);

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
	char		*buf;
	char		*tmp;
	int		*seq;
	struct evt_rect	 rect;
	daos_sg_list_t	 sgl;
	daos_iov_t	 iov;
	long		 offset = 0;
	int		 size;
	int		 nr;
	int		 i;
	int		 rc;

	/* argument format: "s:NUM,e:NUM,n:NUM"
	 * s: start offset
	 * e: extent size
	 * n: number of extents
	 */
	if (args[0] == 's') {
		if (args[1] != EVT_SEP_VAL) {
			D__PRINT("Invalid parameter %s\n", args);
			return -1;
		}
		offset = strtol(&args[2], &tmp, 0);
		if (*tmp != EVT_SEP) {
			D__PRINT("Invalid parameter %s\n", args);
			return -1;
		}
		args = tmp + 1;
	}

	if (args[0] != 'e' || args[1] != EVT_SEP_VAL) {
		D__PRINT("Invalid parameter %s\n", args);
		return -1;
	}

	size = strtol(&args[2], &tmp, 0);
	if (size <= 0) {
		D__PRINT("Invalid extent size %d\n", size);
		return -1;
	}
	if (*tmp != EVT_SEP) {
		D__PRINT("Invalid parameter %s\n", args);
		return -1;
	}
	args = tmp + 1;

	if (args[0] != 'n' || args[1] != EVT_SEP_VAL) {
		D__PRINT("Invalid parameter %s\n", args);
		return -1;
	}
	nr = strtol(&args[2], &tmp, 0);
	if (nr <= 0) {
		D__PRINT("Invalid extent number %d\n", nr);
		return -1;
	}

	buf = malloc(size);
	if (!buf)
		return -1;

	seq = dts_rand_iarr_alloc(nr, 0);
	if (!seq) {
		free(buf);
		return -1;
	}

	for (i = 0; i < nr; i++) {
		rect.rc_off_lo = offset + seq[i] * size;
		rect.rc_off_hi = rect.rc_off_lo + size - 1;
		rect.rc_epc_lo = (seq[i] % TS_VAL_CYCLE) + 1;
		rect.rc_epc_hi = DAOS_EPOCH_MAX;

		memset(buf, 'a' + seq[i] % TS_VAL_CYCLE, size);
		daos_iov_set(&iov, buf, size);
		sgl.sg_nr = 1;
		sgl.sg_iovs = &iov;

		rc = evt_insert_sgl(ts_toh, ts_uuid, 0, &rect, 1, &sgl);
		if (rc != 0) {
			D_FATAL("Add rect %d failed %d\n", i, rc);
			break;
		}
	}

	free(buf);
	free(seq);
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

static struct option ts_ops[] = {
	{ "create",	required_argument,	NULL,	'C'	},
	{ "destroy",	no_argument,		NULL,	'D'	},
	{ "open",	no_argument,		NULL,	'o'	},
	{ "close",	no_argument,		NULL,	'c'	},
	{ "add",	required_argument,	NULL,	'a'	},
	{ "many_add",	required_argument,	NULL,	'm'	},
	{ "find",	required_argument,	NULL,	'f'	},
	{ "delete",	required_argument,	NULL,	'd'	},
	{ "list",	no_argument,		NULL,	'l'	},
	{ "debug",	required_argument,	NULL,	'b'	},
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
		rc = ts_list_rect();
		break;
	case 'b':
		rc = ts_tree_debug(args);
		break;
	default:
		D__PRINT("Unsupported command %c\n", opc);
		rc = 0;
		break;
	}
	return rc;
}

int
main(int argc, char **argv)
{
	int	rc;

	ts_toh = DAOS_HDL_INVAL;

	ts_root.tr_feats = 0;
	ts_root_mmid = TMMID_NULL(struct evt_root);

	rc = daos_debug_init(NULL);
	if (rc != 0)
		return rc;

	if (argc == 1) {
		rc = dts_cmd_parser(ts_ops, "$ > ", ts_cmd_run);
		goto out;
	}

	optind = 0;
	while ((rc = getopt_long(argc, argv, "C:a:m:f:d:b:Docl",
				 ts_ops, NULL)) != -1) {
		rc = ts_cmd_run(rc, optarg);
		if (rc != 0)
			break;
	}
 out:
	daos_debug_fini();
	return rc;
}
