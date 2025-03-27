/*
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Copyright (c) 2021-2022 Hewlett Packard Enterprise Development LP
 */
#include <stdio.h>
#include <stdlib.h>

#include <criterion/criterion.h>
#include <criterion/parameterized.h>

#include <ofi.h>

#include "cxip.h"
#include "cxip_test_common.h"

#define	TRACE(fmt, ...)	CXIP_COLL_TRACE(CXIP_TRC_CTRL, fmt, ##__VA_ARGS__)

TestSuite(ctrl, .init = cxit_setup_rma, .fini = cxit_teardown_rma,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

/**
 * @brief Test reversibility of N <-> (r,c), error conditions
 *
 * For a range of radix values, select a node number (N), convert to
 * a (row,column) pair, and then convert back to node number. These
 * should match, unless an invalid column (for a row) is specified,
 * in which case we see an error.
 */
Test(ctrl, radix_tree_reversible)
{
	int radix, N, M, row, col, siz, rowold, rowwid;

	for (radix = 1; radix < 8; radix++) {
		rowold = -1;
		rowwid = 1;
		for (N = 0; N < 256; N++) {
			/* test reversibility */
			cxip_tree_rowcol(radix, N, &row, &col, &siz);
			cxip_tree_nodeidx(radix, row, col, &M);
			cr_assert(M == N, "M=%d != N=%d\n", M, N);
			if (rowold != row) {
				rowold = row;
				rowwid *= radix;
			}
			/* test invalid column */
			col = rowwid + 1;
			cxip_tree_nodeidx(radix, row, col, &M);
			cr_assert(M == -1,
				  "radix=%d N=%d row=%d col=%d"
				  " M=%d != -1\n",
				  radix, N, row, col, M);
		}
	}
}

/**
 * @brief Test parent/child mapping.
 *
 * For a range of radix values, generate the relatives in the tree (one
 * parent, multiple children), and confirm that these relatives have the
 * expected position in the tree, which guarantees that we have no loops
 * in the tree, and that every node has a parent (except the root), and
 * is a child of its parent.
 */
Test(ctrl, radix_tree_mapping)
{
	int *rels, parent, child;
	int radix, nodes, N, M;
	int count, i;

	/* Test radix zero case */
	M = cxip_tree_relatives(0, 0, 0, NULL);
	cr_assert(M == 0);

	/* Test expected pattern of parent/child indices */
	for (radix = 1; radix < 8; radix++) {
		/* only needs radix+1, but for test, provide extra space */
		rels = calloc(radix+2, sizeof(*rels));
		for (nodes = 0; nodes < 256; nodes++) {
			count = 0;
			parent = -1;
			child = 1;
			for (N = 0; N < nodes; N++) {
				M = cxip_tree_relatives(radix, N, nodes, rels);
				cr_assert(M >= 0);
				cr_assert(M <= radix+1);
				if (M > 0) {
					/* test parent node index */
					cr_assert(rels[0] == parent,
						"radix=%d nodes=%d index=%d"
						" parent=%d != rels[0]=%d\n",
						radix, nodes, N, parent, rels[0]);
					/* test child node indices */
					for (i = 1; i < M; i++, child++)
						cr_assert(rels[i] == child,
							"radix=%d nodes=%d"
							" index=%d child=%d"
							" != rels[%d]=%d\n",
							radix, nodes, N,
							child, i, rels[i]);
				}
				count++;
				if (N == 0 || count >= radix) {
					count = 0;
					parent++;
				}
			}
		}
		free(rels);
	}
}

/* Utility to show the node relatives */
__attribute__((unused))
static void dumpmap(struct cxip_zbcoll_obj *zb)
{
	int i, j;

	printf("MAP=======\n");
	for (i = 0; i < zb->simcount; i++) {
		printf("%2d:", i);
		for (j = 0; j < zb->state[i].num_relatives; j++)
			printf(" %2d", zb->state[i].relatives[j]);
		printf("\n");
	}
	printf("\n");
}

/**
 * @brief Test the valid and invalid cxip_zbcoll_obj configurations.
 */
Test(ctrl, zb_config)
{
	struct cxip_ep *cxip_ep;
	struct cxip_ep_obj *ep_obj;
	struct cxip_zbcoll_obj *zb;
	struct cxip_addr *caddrs;
	fi_addr_t *fiaddrs;
	int i, ret;

	int num_addrs = 5;

	cxip_ep = container_of(cxit_ep, struct cxip_ep, ep.fid);
	ep_obj = cxip_ep->ep_obj;

	caddrs = calloc(num_addrs, sizeof(*caddrs));
	cr_assert(caddrs);
	fiaddrs = calloc(num_addrs, sizeof(*fiaddrs));
	cr_assert(fiaddrs);

	for (i = 0; i < num_addrs; i++)
		caddrs[i] = ep_obj->src_addr;
	ret = fi_av_insert(&ep_obj->av->av_fid, caddrs, num_addrs, fiaddrs,
			   0L, NULL);
	cr_assert(ret == num_addrs);

	/* test case, object but no tree */
	TRACE("case: no tree\n");
	ret = cxip_zbcoll_alloc(ep_obj, 0, NULL, ZB_NOSIM, &zb);
	cr_assert(ret == 0,
		  "no tree: ret=%d\n", ret);
	cr_assert(zb->simcount == 1,
		  "no tree: simcnt=%d\n", zb->simcount);
	cr_assert(zb->num_caddrs == 1,
		  "no_tree: num_caddrs=%d\n", zb->num_caddrs);
	cr_assert(memcmp(&zb->caddrs[0], &ep_obj->src_addr,
			 sizeof(ep_obj->src_addr)) == 0);
	cxip_zbcoll_free(zb);

	/* request simulation */
	TRACE("case: simulated\n");
	ret = cxip_zbcoll_alloc(ep_obj, num_addrs, NULL, ZB_ALLSIM, &zb);
	cr_assert(ret == 0,
		  "sim tree 4: ret=%d\n", ret);
	cr_assert(zb->simcount == num_addrs,
		  "sim tree 4: cnt=%d\n", zb->simcount);
	cxip_zbcoll_free(zb);

	/* exercise real setup, send-to-self-only */
	TRACE("case: real send-only\n");
	ret = cxip_zbcoll_alloc(ep_obj, 0, NULL, ZB_NOSIM, &zb);
	cr_assert(ret == 0, "cxip_zbcoll_alloc() = %s\n", fi_strerror(-ret));
	cr_assert(zb != NULL);
	cr_assert(zb->simcount == 1);
	cr_assert(zb->state != NULL);
	cr_assert(CXIP_ADDR_EQUAL(zb->caddrs[0], ep_obj->src_addr));

	/* exercise real setup success, all caddrs are real */
	TRACE("case: real addresses root 0\n");
	ret = cxip_zbcoll_alloc(ep_obj, num_addrs, fiaddrs, ZB_NOSIM, &zb);
	cr_assert(ret == 0, "real tree0: ret=%s\n", fi_strerror(-ret));
	cr_assert(zb->simcount == 1, "real tree0: simcnt=%d\n", zb->simcount);
	cr_assert(zb->state[0].grp_rank == 0, "real tree0: grp_rank=%d\n",
		  zb->state[0].grp_rank);
	cxip_zbcoll_free(zb);

	/* exercise real setup success, first caddr is not me */
	TRACE("case: real addresses root 1\n");
	caddrs[0].nic += 1;
	ret = fi_av_insert(&ep_obj->av->av_fid, caddrs, num_addrs, fiaddrs,
			   0L, NULL);
	cr_assert(ret == num_addrs);
	ret = cxip_zbcoll_alloc(ep_obj, num_addrs, fiaddrs, ZB_NOSIM, &zb);
	cr_assert(ret == 0, "real tree1: ret=%s\n", fi_strerror(-ret));
	cr_assert(zb->simcount == 1, "real tree1: simcnt=%d\n", zb->simcount);
	cr_assert(zb->state[0].grp_rank == 1, "real tree1: grp_rank=%d\n",
		  zb->state[0].grp_rank);
	cxip_zbcoll_free(zb);

	/* exercise real setup failure, no caddr is me */
	TRACE("case: real addresses root N\n");
	for (i = 0; i < num_addrs; i++)
		caddrs[i].nic += i + 1;
	ret = fi_av_insert(&ep_obj->av->av_fid, caddrs, num_addrs, fiaddrs,
			   0L, NULL);
	cr_assert(ret == num_addrs);
	ret = cxip_zbcoll_alloc(ep_obj, num_addrs, fiaddrs, ZB_NOSIM, &zb);
	cr_assert(ret == -FI_ECONNREFUSED, "real treeN: ret=%s\n", fi_strerror(-ret));
	cxip_zbcoll_free(zb);

	free(fiaddrs);
}

/**
 * @brief Send a single packet using a self to self send-only configuration.
 */
Test(ctrl, zb_send0)
{
	struct cxip_ep *cxip_ep;
	struct cxip_ep_obj *ep_obj;
	struct cxip_zbcoll_obj *zb;
	union cxip_match_bits mb = {.raw = 0};
	uint32_t dsc, err, ack, rcv, cnt;
	int ret;

	cr_assert(sizeof(union cxip_match_bits) == 8);

	cxip_ep = container_of(cxit_ep, struct cxip_ep, ep.fid);
	ep_obj = cxip_ep->ep_obj;

	/* Set up the send-only zbcoll */
	ret = cxip_zbcoll_alloc(ep_obj, 0, NULL, ZB_NOSIM, &zb);

	/* Test that if disabled, getgroup is no-op */
	ep_obj->zbcoll.disable = true;
	ret = cxip_zbcoll_getgroup(zb);
	cr_assert(ret == 0, "getgroup = %s\n", fi_strerror(-ret));

	/* Legitimate send to self */
	cxip_zbcoll_reset_counters(ep_obj);
	cxip_zbcoll_send(zb, 0, 0, mb.raw);
	cnt = 0;
	do {
		usleep(1);
		cxip_ep_zbcoll_progress(ep_obj);
		cxip_zbcoll_get_counters(ep_obj, &dsc, &err, &ack, &rcv);
		ret = (dsc || err || (ack && rcv));
		cnt++;
	} while (!ret && cnt < 1000);
	cr_assert(cnt < 1000, "repeat count = %d >= %d\n", cnt, 1000);
	cr_assert(dsc == 0, "dsc = %d, != 0\n", dsc);
	cr_assert(err == 0, "err = %d, != 0\n", err);
	cr_assert(ack == 1, "ack = %d, != 1\n", ack);
	cr_assert(rcv == 1, "rcv = %d, != 1\n", rcv);

	/* Invalid send to out-of-range address index */
	cxip_zbcoll_reset_counters(ep_obj);
	cxip_zbcoll_send(zb, 0, 1, mb.raw);
	cnt = 0;
	do {
		usleep(1);
		cxip_ep_zbcoll_progress(ep_obj);
		cxip_zbcoll_get_counters(ep_obj, &dsc, &err, &ack, &rcv);
		ret = (err || dsc || (ack && rcv));
		cnt++;
	} while (!ret && cnt < 1000);
	cr_assert(cnt < 1000, "repeat count = %d < %d\n", cnt, 1000);
	cr_assert(dsc == 0, "dsc = %d, != 0\n", dsc);
	cr_assert(err == 1, "err = %d, != 1\n", err);
	cr_assert(ack == 0, "ack = %d, != 0\n", ack);
	cr_assert(rcv == 0, "rcv = %d, != 0\n", rcv);

	cxip_zbcoll_free(zb);
}

/* utility to send from src to dst */
static void _send(struct cxip_zbcoll_obj *zb, int srcidx, int dstidx)
{
	struct cxip_ep_obj *ep_obj;
	union cxip_match_bits mb = {.zb_data=0};
	int ret, cnt;
	uint32_t dsc, err, ack, rcv;

	/* send to dstidx simulated address */
	ep_obj = zb->ep_obj;
	cxip_zbcoll_reset_counters(ep_obj);
	cxip_zbcoll_send(zb, srcidx, dstidx, mb.raw);

	/* wait for errors, or completion */
	cnt = 0;
	do {
		usleep(1);
		cxip_ep_zbcoll_progress(ep_obj);
		cxip_zbcoll_get_counters(ep_obj, &dsc, &err, &ack, &rcv);
		ret = (err || dsc || (ack && rcv));
		cnt++;
	} while (!ret && cnt < 1000);
	cr_assert(cnt < 1000, "repeat count = %d\n", cnt);

	cr_assert(dsc == 0, "dsc = %d, != 0\n", dsc);
	cr_assert(err == 0, "err = %d, != 0\n", err);
	cr_assert(ack == 1, "ack = %d, != 1\n", ack);
	cr_assert(rcv == 1, "rcv = %d, != 1\n", rcv);
}

/**
 * @brief Send a single packet from each src to dst in NETSIM simulation.
 *
 * Scales as O(N^2), so keep number of addresses small.
 */
Test(ctrl, zb_sendN)
{
	struct cxip_ep *cxip_ep;
	struct cxip_ep_obj *ep_obj;
	struct cxip_zbcoll_obj *zb;
	int srcidx, dstidx, ret;

	int num_addrs = 5;

	cxip_ep = container_of(cxit_ep, struct cxip_ep, ep.fid);
	ep_obj = cxip_ep->ep_obj;

	ret = cxip_zbcoll_alloc(ep_obj, num_addrs, NULL, ZB_ALLSIM, &zb);
	cr_assert(ret == 0, "cxip_zbcoll_alloc() = %s\n", fi_strerror(-ret));
	cr_assert(zb != NULL);
	cr_assert(zb->simcount == num_addrs);
	cr_assert(zb->state != NULL);

	/* Test that if disabled, getgroup is no-op */
	ep_obj->zbcoll.disable = true;
	ret = cxip_zbcoll_getgroup(zb);
	cr_assert(ret == 0, "getgroup = %s\n", fi_strerror(-ret));

	for (srcidx = 0; srcidx < num_addrs; srcidx++)
		for (dstidx = 0; dstidx < num_addrs; dstidx++)
			_send(zb, srcidx, dstidx);
	cxip_zbcoll_free(zb);
}

/* Utility to wait until an ALLSIM collective has completed */
static int _await_complete(struct cxip_zbcoll_obj *zb)
{
	uint32_t rep;

	/* We only wait for 1 sec */
	for (rep = 0; rep < 10000; rep++) {
		usleep(100);
		cxip_ep_zbcoll_progress(zb->ep_obj);
		if (zb->error)
			return zb->error;
		if (!zb->busy)
			break;
	}
	return (zb->busy) ? -FI_ETIMEDOUT : FI_SUCCESS;
}

/* Utility to wait until a multi-zb collective has completed */
static int _await_complete_all(struct cxip_zbcoll_obj **zb, int cnt)
{
	uint32_t i, rep;

	/* We only wait for 1 sec */
	for (rep = 0; rep < 10000; rep++) {
		usleep(100);
		cxip_ep_zbcoll_progress(zb[0]->ep_obj);
		for (i = 0; i < cnt; i++) {
			if (zb[i]->error)
				return zb[i]->error;
			if (zb[i]->busy)
				break;
		}
		if (i == cnt)
			break;
	}
	return (i < cnt) ? -FI_ETIMEDOUT : FI_SUCCESS;
}

/* shuffle the array */
void _shuffle_array32(uint32_t *array, size_t size)
{
	uint32_t i, j, t;

	for (i = 0; i < size-1; i++) {
		j = i + rand() / (RAND_MAX / (size - i) + 1);
		t = array[j];
		array[j] = array[i];
		array[i] = t;
	}
}

/* create a randomized shuffle array */
void _addr_shuffle(struct cxip_zbcoll_obj *zb, bool shuffle)
{
	struct timespec tv;
	int i;

	clock_gettime(CLOCK_MONOTONIC, &tv);
	srand((unsigned int)tv.tv_nsec);
	free(zb->shuffle);
	zb->shuffle = calloc(zb->simcount, sizeof(uint32_t));
	if (!zb->shuffle)
		return;
	/* create ordered list */
	for (i = 0; i < zb->simcount; i++)
		zb->shuffle[i] = i;
	/* if requested, randomize */
	if (shuffle)
		_shuffle_array32(zb->shuffle, zb->simcount);
}

/*****************************************************************/
/**
 * @brief Test simulated getgroup.
 *
 * This exercises the basic getgroup operation, the user callback, and the
 * non-concurrency lockout. It tests grpid wrap-around at the limit.
 *
 * This does not test error returns, which are not robustly simulated.
 */

struct getgroup_data {
	int count;
};
static void getgroup_func(struct cxip_zbcoll_obj *zb, void *usrptr)
{
	struct getgroup_data *data = (struct getgroup_data *)usrptr;
	data->count++;
}

/* Test getgroup single-zb simulation */
Test(ctrl, zb_getgroup)
{
	struct cxip_ep *cxip_ep;
	struct cxip_ep_obj *ep_obj;
	struct cxip_zbcoll_obj **zb;
	struct getgroup_data zbd = {};
	int i, j, ret;
	uint32_t dsc, err, ack, rcv;
	int max_zb = cxip_zbcoll_max_grps(true);
	int num_zb = 2*max_zb;
	int num_addrs = 9;
	int cnt = 0;

	cxip_ep = container_of(cxit_ep, struct cxip_ep, ep.fid);
	ep_obj = cxip_ep->ep_obj;

	zb = calloc(num_zb, sizeof(struct cxip_zbcoll_obj *));
	cr_assert(zb, "zb out of memory\n");

	TRACE("%s entry\n", __func__);
	for (i = 0; i < num_zb; i++) {
		/* Verify multiple allocations */
		ret = cxip_zbcoll_alloc(ep_obj, num_addrs, NULL,
					ZB_ALLSIM, &zb[i]);
		cr_assert(ret == 0, "cxip_zbcoll_alloc() = %s\n",
			  fi_strerror(-ret));
		cr_assert(zb[i]->simcount == num_addrs,
			"zb->simcount = %d, != %d\n",
			zb[i]->simcount, num_addrs);
		/* Add callback function */
		cxip_zbcoll_set_user_cb(zb[i], getgroup_func, &zbd);
		/* Initialize the address shuffling */
		_addr_shuffle(zb[i], true);
		TRACE("created zb[%d]\n", i);
	}
	for (i = j = 0; i < num_zb; i++) {
		/* Free space if necessary */
		while ((i - j) >= max_zb)
			cxip_zbcoll_free(zb[j++]);
		_addr_shuffle(zb[i], true);
		/* Test getgroup operation */
		TRACE("initiate getgroup %d\n", i);
		ret = cxip_zbcoll_getgroup(zb[i]);
		cr_assert(ret == FI_SUCCESS, "%d getgroup = %s\n",
			  i, fi_strerror(-ret));
		/* Test getgroup non-concurrency */
		TRACE("second initiate getgroup %d\n", i);
		ret = cxip_zbcoll_getgroup(zb[i]);
		cr_assert(ret == -FI_EAGAIN, "%d getgroup = %s\n",
			  i, fi_strerror(-ret));
		/* Poll until complete */
		TRACE("await completion %d\n", i);
		ret = _await_complete(zb[i]);
		cr_assert(ret == FI_SUCCESS, "%d getgroup = %s\n",
			  i, fi_strerror(-ret));
		/* Check user callback completion count result */
		cr_assert(zbd.count == i+1, "%d zbdcount = %d\n",
			  i, zbd.count);
		/* Confirm expected grpid */
		cr_assert(zb[i]->grpid == (i % max_zb),
			  "%d grpid = %d, exp %d\n",
			  i, zb[i]->grpid, i % max_zb);
		TRACE("second getgroup after completion\n");
		/* Attempt another getgroup on same zb */
		ret = cxip_zbcoll_getgroup(zb[i]);
		cr_assert(ret == -FI_EINVAL, "%d getgroup = %s\n",
			  i, fi_strerror(-ret));
		/* Compute expected transfer count */
		cnt += 2 * (num_addrs - 1);
	}

	cxip_zbcoll_get_counters(ep_obj, &dsc, &err, &ack, &rcv);
	cr_assert(dsc == 0 && err == 0,
		  "FAILED dsc=%d err=%d ack=%d rcv=%d cnt=%d\n",
		  dsc, err, ack, rcv, cnt);
	/* cleanup */
	while (j < num_zb)
		cxip_zbcoll_free(zb[j++]);
	free(zb);
}

/*****************************************************************/
/**
 * @brief Test simulated getgroup with multi-zb model.
 */

void _getgroup_multi(int num_addrs, struct cxip_zbcoll_obj **zb,
		     int expect_grpid)
{
	struct cxip_ep *cxip_ep;
	struct cxip_ep_obj *ep_obj;
	struct getgroup_data zbd = {};
	int i, ret;

	cxip_ep = container_of(cxit_ep, struct cxip_ep, ep.fid);
	ep_obj = cxip_ep->ep_obj;

	/* allocate multiple zb objects, simrank = i */
	for (i = 0; i < num_addrs; i++) {
		ret = cxip_zbcoll_alloc(ep_obj, num_addrs, NULL, i, &zb[i]);
		cr_assert(ret == 0, "cxip_zbcoll_alloc() = %s\n",
			  fi_strerror(-ret));
		cr_assert(zb[i]->simcount == num_addrs,
			  "zb->simcount = %d, != %d\n",
			  zb[i]->simcount, num_addrs);
		ret = cxip_zbcoll_simlink(zb[0], zb[i]);
		cr_assert(!ret, "link zb[%d] failed\n", i);
	}

	for (i = 0; i < num_addrs; i++)
		cxip_zbcoll_set_user_cb(zb[i], getgroup_func, &zbd);

	/* initiate getgroup across all of the zb objects */
	for (i = 0; i < num_addrs; i++) {
		ret = cxip_zbcoll_getgroup(zb[i]);
		cr_assert(ret == FI_SUCCESS, "getgroup[%d]=%s, exp success\n",
			  i, fi_strerror(-ret));
	}

	/* make a second attempt */
	for (i = 0; i < num_addrs; i++) {
		ret = cxip_zbcoll_getgroup(zb[i]);
		cr_assert(ret == -FI_EAGAIN, "getgroup[%d]=%s exp FI_EAGAIN\n",
			  i, fi_strerror(-ret));
	}

	/* Poll until all are complete */
	ret = _await_complete_all(zb, num_addrs);
	cr_assert(ret == FI_SUCCESS, "getgroup = %s\n",
		  fi_strerror(-ret));

	/* Ensure all objects have the same group ids */
	ret = 0;
	for (i = 0; i < num_addrs; i++) {
		if (zb[i]->grpid != expect_grpid) {
			TRACE("zb[%d]->grpid = %d, exp %d\n",
			    i, zb[i]->grpid, expect_grpid);
			ret++;
		}
	}
	cr_assert(!ret, "Some zb objects have the wrong group id\n");

	/* Make sure we can't take a second group id */
	for (i = 0; i < num_addrs; i++) {
		ret = cxip_zbcoll_getgroup(zb[i]);
		cr_assert(ret == -FI_EINVAL, "getgroup[%d]=%s exp FI_EINVAL\n",
			  i, fi_strerror(-ret));
	}

}

void _free_getgroup_multi(int num_addrs, struct cxip_zbcoll_obj **zb)
{
	int i;

	for (i = 0; i < num_addrs; i++)
		cxip_zbcoll_free(zb[i]);
	free(zb);
}

/* Test getgroup multi-zb simulation */
Test(ctrl, zb_getgroup2)
{
	struct cxip_zbcoll_obj **zb1, **zb2;
	int num_addrs = 9;	// arbitrary

	zb1 = calloc(num_addrs, sizeof(struct cxip_zbcoll_obj *));
	cr_assert(zb1, "zb out of memory\n");
	zb2 = calloc(num_addrs, sizeof(struct cxip_zbcoll_obj *));
	cr_assert(zb2, "zb out of memory\n");

	_getgroup_multi(num_addrs, zb1, 0);
	_getgroup_multi(num_addrs, zb2, 1);

	_free_getgroup_multi(num_addrs, zb2);
	_free_getgroup_multi(num_addrs, zb1);
}

/*****************************************************************/
/**
 * @brief Test simulated barrier.
 *
 * This exercises the basic barrier operation, the user callback, and the
 * non-concurrency lockout.
 *
 * This is done in a single thread, so it tests only a single barrier across
 * multiple addrs. It randomizes the nid processing order, and performs multiple
 * barriers to uncover any ordering issues.
 */
struct barrier_data {
	int count;
};
static void barrier_func(struct cxip_zbcoll_obj *zb, void *usrptr)
{
	struct barrier_data *data = (struct barrier_data *)usrptr;

	/* increment the user completion count */
	data->count++;
}

/* Test barrier single-zb simulation */
Test(ctrl, zb_barrier)
{
	struct cxip_ep *cxip_ep;
	struct cxip_ep_obj *ep_obj;
	struct cxip_zbcoll_obj *zb;
	struct barrier_data zbd;
	int rep, ret;

	int num_addrs = 9;

	cxip_ep = container_of(cxit_ep, struct cxip_ep, ep.fid);
	ep_obj = cxip_ep->ep_obj;

	ret = cxip_zbcoll_alloc(ep_obj, num_addrs, NULL, ZB_ALLSIM, &zb);
	cr_assert(ret == 0, "cxip_zbcoll_alloc() = %s\n", fi_strerror(-ret));
	cr_assert(zb->simcount == num_addrs,
		  "zb->simcount = %d, != %d\n", zb->simcount, num_addrs);
	/* Initialize the addresses */
	_addr_shuffle(zb, true);

	/* Acquire a group id */
	ret = cxip_zbcoll_getgroup(zb);
	cr_assert(ret == 0, "getgroup = %s\n", fi_strerror(-ret));
	ret = _await_complete(zb);
	cr_assert(ret == 0, "getgroup done = %s\n", fi_strerror(-ret));

	cxip_zbcoll_set_user_cb(zb, barrier_func, &zbd);

	memset(&zbd, 0, sizeof(zbd));
	for (rep = 0; rep < 20; rep++) {
		/* Shuffle the addresses */
		_addr_shuffle(zb, true);
		/* Perform a barrier */
		ret = cxip_zbcoll_barrier(zb);
		cr_assert(ret == 0, "%d barrier = %s\n",
			  rep, fi_strerror(-ret));
		/* Try again immediately, should show BUSY */
		ret = cxip_zbcoll_barrier(zb);
		cr_assert(ret == -FI_EAGAIN, "%d barrier = %s\n",
			  rep, fi_strerror(-ret));
		/* Poll until complete */
		ret = _await_complete(zb);
		cr_assert(ret == FI_SUCCESS, "%d barrier = %s\n",
			  rep, fi_strerror(-ret));
	}
	/* Confirm completion count */
	cr_assert(zbd.count == rep, "expected zbd.count=%d == rep=%d\n",
		  zbd.count, rep);

	uint32_t dsc, err, ack, rcv;
	cxip_zbcoll_get_counters(ep_obj, &dsc, &err, &ack, &rcv);
	cr_assert(dsc == 0 && err == 0,
		  "FAILED dsc=%d err=%d ack=%d rcv=%d\n",
		  dsc, err, ack, rcv);

	cxip_zbcoll_free(zb);
}

/* Test barrier multi-zb simulation */
Test(ctrl, zb_barrier2)
{
	struct cxip_zbcoll_obj **zb1, **zb2;
	struct barrier_data zbd1 = {};
	struct barrier_data zbd2 = {};
	int num_addrs = 17;	// arbitrary
	int i, ret;

	zb1 = calloc(num_addrs, sizeof(*zb1));
	cr_assert(zb1);
	zb2 = calloc(num_addrs, sizeof(*zb2));
	cr_assert(zb2);

	_getgroup_multi(num_addrs, zb1, 0);
	_getgroup_multi(num_addrs, zb2, 1);

	for (i = 0; i < num_addrs; i++) {
		cxip_zbcoll_set_user_cb(zb1[i], barrier_func, &zbd1);
		cxip_zbcoll_set_user_cb(zb2[i], barrier_func, &zbd2);
	}

	for (i = 0; i < num_addrs; i++) {
		ret = cxip_zbcoll_barrier(zb1[i]);
		cr_assert(!ret, "zb1 barrier[%d]=%s\n", i, fi_strerror(-ret));

		ret = cxip_zbcoll_barrier(zb2[i]);
		cr_assert(!ret, "zb2 barrier[%d]=%s\n", i, fi_strerror(-ret));
	}

	/* Poll until all are complete */
	ret = _await_complete_all(zb1, num_addrs);
	cr_assert(ret == FI_SUCCESS, "zb1 barrier = %s\n",
		  fi_strerror(-ret));
	ret = _await_complete_all(zb2, num_addrs);
	cr_assert(ret == FI_SUCCESS, "zb2 barrier = %s\n",
		  fi_strerror(-ret));

	/* Validate data */
	cr_assert(zbd1.count == num_addrs, "zb1 count=%d != %d\n",
		  zbd1.count, num_addrs);
	cr_assert(zbd2.count == num_addrs, "zb2 count=%d != %d\n",
		  zbd2.count, num_addrs);

	_free_getgroup_multi(num_addrs, zb2);
	_free_getgroup_multi(num_addrs, zb1);
}

/*****************************************************************/
/**
 * @brief Perform a simulated broadcast.
 *
 * This exercises the basic broadcast operation, the user callback, and the
 * non-concurrency lockout. The user callback captures all of the results and
 * ensures they all match the broadcast value.
 *
 * This is done in a single thread, so it tests only a single broadcast across
 * multiple addrs. It randomizes the nid processing order, and performs multiple
 * broadcasts to uncover any ordering issues.
 */
struct bcast_data {
	uint64_t *data;
	int count;
};

static void bcast_func(struct cxip_zbcoll_obj *zb, void *usrptr)
{
	struct bcast_data *data = (struct bcast_data *)usrptr;
	int i;

	if (zb->simrank >= 0) {
		data->data[zb->simrank] = *zb->state[zb->simrank].dataptr;
	} else {
		for (i = 0; i < zb->simcount; i++)
			data->data[i] = *zb->state[i].dataptr;
	}
	data->count++;
}

/* Test broadcast single-zb simulation */
Test(ctrl, zb_broadcast)
{
	struct cxip_ep *cxip_ep;
	struct cxip_ep_obj *ep_obj;
	struct cxip_zbcoll_obj *zb;
	struct bcast_data zbd = {};
	int i, n, rep, ret;
	uint64_t *data;

	int num_addrs = 25;

	cxip_ep = container_of(cxit_ep, struct cxip_ep, ep.fid);
	ep_obj = cxip_ep->ep_obj;

	ret = cxip_zbcoll_alloc(ep_obj, num_addrs, NULL, ZB_ALLSIM, &zb);
	cr_assert(ret == 0, "cxip_zbcoll_alloc() = %s\n", fi_strerror(-ret));
	cr_assert(zb->simcount == num_addrs,
		  "zb->simcount = %d, != %d\n", zb->simcount, num_addrs);
	_addr_shuffle(zb, true);

	data = calloc(num_addrs, sizeof(uint64_t));

	/* Acquire a group id */
	ret = cxip_zbcoll_getgroup(zb);
	cr_assert(ret == 0, "getgroup = %s\n", fi_strerror(-ret));
	ret = _await_complete(zb);
	cr_assert(ret == 0, "getgroup done = %s\n", fi_strerror(-ret));

	cxip_zbcoll_set_user_cb(zb, bcast_func, &zbd);

	memset(&zbd, 0, sizeof(zbd));
	zbd.data = calloc(num_addrs, sizeof(uint64_t));
	for (rep = 0; rep < 20; rep++) {
		_addr_shuffle(zb, true);
		n = zb->shuffle[0];
		memset(zbd.data, -1, num_addrs*sizeof(uint64_t));
		/* Perform a broadcast */
		for (i = 0; i < num_addrs; i++)
			data[i] = (rand() & ((1 << 29) - 1)) | (1 << 28);
		ret = cxip_zbcoll_broadcast(zb, data);
		cr_assert(ret == 0, "%d bcast = %s\n",
			  rep, fi_strerror(-ret));
		/* Try again immediately, should fail */
		ret = cxip_zbcoll_broadcast(zb, data);
		cr_assert(ret == -FI_EAGAIN, "%d bcast = %s\n",
			  rep, fi_strerror(-ret));
		/* Poll until complete */
		ret = _await_complete(zb);
		cr_assert(ret == FI_SUCCESS, "%d bcast = %s\n",
			  rep, fi_strerror(-ret));
		/* Validate the data */
		for (i = 0; i < num_addrs; i++)
			cr_assert(zbd.data[i] == data[n], "[%d] %ld != %ld\n",
				  i, zbd.data[i], data[n]);
	}
	cr_assert(zbd.count == rep, "zbd.count=%d rep=%d\n",
		  zbd.count, rep);

	uint32_t dsc, err, ack, rcv;
	cxip_zbcoll_get_counters(ep_obj, &dsc, &err, &ack, &rcv);
	cr_assert(dsc == 0 && err == 0,
		  "FAILED dsc=%d err=%d ack=%d rcv=%d\n",
		  dsc, err, ack, rcv);

	free(zbd.data);
	free(data);
	cxip_zbcoll_free(zb);
}

/* Test broadcast multi-zb simulation */
Test(ctrl, zb_broadcast2)
{
	struct cxip_zbcoll_obj **zb1, **zb2;
	uint64_t data1, data2;
	struct bcast_data zbd1 = {};
	struct bcast_data zbd2 = {};
	int i, ret;

	int num_addrs = 11;	// arbitrary

	zb1 = calloc(num_addrs, sizeof(*zb1));
	cr_assert(zb1);
	zb2 = calloc(num_addrs, sizeof(*zb2));
	cr_assert(zb2);
	zbd1.data = calloc(num_addrs, sizeof(*zbd1.data));
	cr_assert(zbd1.data);
	zbd2.data = calloc(num_addrs, sizeof(*zbd2.data));
	cr_assert(zbd2.data);

	/* Acquire group ids */
	_getgroup_multi(num_addrs, zb1, 0);
	_getgroup_multi(num_addrs, zb2, 1);

	data1 = (rand() & ((1 << 29) - 1)) | (1 << 28);
	data2 = (rand() & ((1 << 29) - 1)) | (1 << 28);

	for (i = 0; i < num_addrs; i++) {
		cxip_zbcoll_set_user_cb(zb1[i], bcast_func, &zbd1);
		cxip_zbcoll_set_user_cb(zb2[i], bcast_func, &zbd2);
	}
	for (i = 0; i < num_addrs; i++) {
		ret = cxip_zbcoll_broadcast(zb1[i], &data1);
		cr_assert(!ret, "zb1 broadcast[%d]=%s\n", i, fi_strerror(-ret));

		ret = cxip_zbcoll_broadcast(zb2[i], &data2);
		cr_assert(!ret, "zb2 broadcast[%d]=%s\n", i, fi_strerror(-ret));
	}

	/* Poll until all are complete */
	ret = _await_complete_all(zb1, num_addrs);
	cr_assert(ret == FI_SUCCESS, "zb1 broadcast = %s\n",
		  fi_strerror(-ret));
	ret = _await_complete_all(zb2, num_addrs);
	cr_assert(ret == FI_SUCCESS, "zb2 broadcast = %s\n",
		  fi_strerror(-ret));

	/* Validate data */
	cr_assert(zbd1.count == num_addrs, "count=%d != %d\n",
		  zbd1.count, num_addrs);
	for (i = 0; i < num_addrs; i++) {
		cr_assert(data1 == zbd1.data[i],
			  "data1=%ld != zbd1[%d]=%ld\n",
			  data1, i, zbd1.data[i]);
	}
	cr_assert(zbd2.count == num_addrs, "count=%d != %d\n",
		  zbd2.count, num_addrs);
	for (i = 0; i < zbd2.count; i++) {
		cr_assert(data2 == zbd2.data[i],
			  "data2=%ld != zbd2[%d]=%ld\n",
			  data2, i, zbd2.data[i]);
	}

	_free_getgroup_multi(num_addrs, zb2);
	_free_getgroup_multi(num_addrs, zb1);
}

/*****************************************************************/
/**
 * @brief Perform a simulated reduce.
 *
 * This exercises the basic reduce operation, the user callback, and the
 * non-concurrency lockout. The user callback captures all of the results and
 * ensures they all match the reduce value.
 *
 * This is done in a single thread, so it tests only a single reduce across
 * multiple addrs. It randomizes the nid processing order, and performs multiple
 * reductions to uncover any ordering issues.
 */
struct reduce_data {
	uint64_t *data;
	int count;
};

static void reduce_func(struct cxip_zbcoll_obj *zb, void *usrptr)
{
	struct reduce_data *data = (struct reduce_data *)usrptr;
	int i;

	if (zb->simrank >= 0) {
		data->data[zb->simrank] = *zb->state[zb->simrank].dataptr;
	} else {
		for (i = 0; i < zb->simcount; i++)
			data->data[i] = *zb->state[i].dataptr;
	}
	data->count++;
}

/* Test reduce single-zb simulation */
Test(ctrl, zb_reduce)
{
	struct cxip_ep *cxip_ep;
	struct cxip_ep_obj *ep_obj;
	struct cxip_zbcoll_obj *zb;
	struct reduce_data zbd = {};
	int i, rep, ret;
	uint64_t *data, rslt;

	int num_addrs = 25;

	cxip_ep = container_of(cxit_ep, struct cxip_ep, ep.fid);
	ep_obj = cxip_ep->ep_obj;

	ret = cxip_zbcoll_alloc(ep_obj, num_addrs, NULL, ZB_ALLSIM, &zb);
	cr_assert(ret == 0, "cxip_zbcoll_alloc() = %s\n", fi_strerror(-ret));
	cr_assert(zb->simcount == num_addrs,
		  "zb->simcount = %d, != %d\n", zb->simcount, num_addrs);
	_addr_shuffle(zb, true);

	data = calloc(num_addrs, sizeof(uint64_t));

	/* Acquire a group id */
	ret = cxip_zbcoll_getgroup(zb);
	cr_assert(ret == 0, "getgroup = %s\n", fi_strerror(-ret));
	ret = _await_complete(zb);
	cr_assert(ret == 0, "getgroup done = %s\n", fi_strerror(-ret));

	cxip_zbcoll_set_user_cb(zb, reduce_func, &zbd);

	memset(&zbd, 0, sizeof(zbd));
	zbd.data = calloc(num_addrs, sizeof(uint64_t));

	for (rep = 0; rep < 20; rep++) {
		_addr_shuffle(zb, true);
		memset(zbd.data, -1, num_addrs*sizeof(uint64_t));
		/* Perform a reduce */
		for (i = 0; i < num_addrs; i++) {
			data[i] = (rand() & ((1 << 29) - 1)) | (1 << 28);
			data[i] |= 3;
		}
		rslt = -1L;
		for (i = 1; i < num_addrs; i++) {
			rslt &= data[i];
		}
		ret = cxip_zbcoll_reduce(zb, data);
		cr_assert(ret == 0, "%d reduce = %s\n",
			  rep, fi_strerror(-ret));
		/* Try again immediately, should fail */
		ret = cxip_zbcoll_reduce(zb, data);
		cr_assert(ret == -FI_EAGAIN, "%d reduce = %s\n",
			  rep, fi_strerror(-ret));
		/* Poll until complete */
		ret = _await_complete(zb);
		cr_assert(ret == FI_SUCCESS, "%d reduce = %s\n",
			  rep, fi_strerror(-ret));
		/* Validate the data */
		for (i = 0; i < num_addrs; i++)
			cr_assert(zbd.data[i] == rslt, "[%d] %lx != %lx\n",
				  i, zbd.data[i], rslt);
	}
	cr_assert(zbd.count == rep, "zbd.count=%d rep=%d\n",
		  zbd.count, rep);

	uint32_t dsc, err, ack, rcv;
	cxip_zbcoll_get_counters(ep_obj, &dsc, &err, &ack, &rcv);
	cr_assert(dsc == 0 && err == 0,
		  "FAILED dsc=%d err=%d ack=%d rcv=%d\n",
		  dsc, err, ack, rcv);

	free(zbd.data);
	free(data);
	cxip_zbcoll_free(zb);
}

/* Test reduce multi-zb simulation */
Test(ctrl, zb_reduce2)
{
	struct cxip_zbcoll_obj **zb1, **zb2;
	int num_addrs = 11;	// arbitrary
	uint64_t data1, data2;
	struct reduce_data zbd1 = {};
	struct reduce_data zbd2 = {};
	int i, ret;

	zb1 = calloc(num_addrs, sizeof(*zb1));
	cr_assert(zb1);
	zb2 = calloc(num_addrs, sizeof(*zb2));
	cr_assert(zb2);
	zbd1.data = calloc(num_addrs, sizeof(*zbd1.data));
	cr_assert(zbd1.data);
	zbd2.data = calloc(num_addrs, sizeof(*zbd2.data));
	cr_assert(zbd2.data);

	_getgroup_multi(num_addrs, zb1, 0);
	_getgroup_multi(num_addrs, zb2, 1);

	data1 = (rand() & ((1 << 29) - 1)) | (1 << 28);
	data2 = (rand() & ((1 << 29) - 1)) | (1 << 28);

	for (i = 0; i < num_addrs; i++) {
		cxip_zbcoll_set_user_cb(zb1[i], reduce_func, &zbd1);
		cxip_zbcoll_set_user_cb(zb2[i], reduce_func, &zbd2);
	}
	for (i = 0; i < num_addrs; i++) {
		ret = cxip_zbcoll_reduce(zb1[i], &data1);
		cr_assert(!ret, "zb1 reduce[%d]=%s\n", i, fi_strerror(-ret));

		ret = cxip_zbcoll_reduce(zb2[i], &data2);
		cr_assert(!ret, "zb2 reduce[%d]=%s\n", i, fi_strerror(-ret));
	}

	/* Poll until all are complete */
	ret = _await_complete_all(zb1, num_addrs);
	cr_assert(ret == FI_SUCCESS, "zb1 reduce = %s\n",
		  fi_strerror(-ret));
	ret = _await_complete_all(zb2, num_addrs);
	cr_assert(ret == FI_SUCCESS, "zb2 reduce = %s\n",
		  fi_strerror(-ret));

	/* Validate data */
	cr_assert(zbd1.count == num_addrs, "count=%d != %d\n",
		  zbd1.count, num_addrs);
	for (i = 0; i < num_addrs; i++) {
		cr_assert(data1 == zbd1.data[i],
			  "data1=%ld != zbd1[%d]=%ld\n",
			  data1, i, zbd1.data[i]);
	}
	cr_assert(zbd2.count == num_addrs, "count=%d != %d\n",
		  zbd2.count, num_addrs);
	for (i = 0; i < zbd2.count; i++) {
		cr_assert(data2 == zbd2.data[i],
			  "data2=%ld != zbd2[%d]=%ld\n",
			  data2, i, zbd2.data[i]);
	}

	_free_getgroup_multi(num_addrs, zb2);
	_free_getgroup_multi(num_addrs, zb1);
}
