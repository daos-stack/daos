/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(rdb)

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <daos_srv/daos_engine.h>	/* for dss_module */
#include <daos_srv/rdb.h>
#include <daos_srv/rsvc.h>
#include "../rdb_internal.h"
#include "rpc.h"

#define DB_CAP	(1L << 25)

static char	       *test_svc_name = "rsvc_test";
static d_iov_t		test_svc_id;

/* Root KVS layout */
RDB_STRING_KEY(rdbt_key_, kvs1);

struct rdbt_svc {
	struct ds_rsvc		rt_rsvc;
	rdb_path_t		rt_root_kvs_path;
	rdb_path_t		rt_kvs1_path;
};

static struct rdbt_svc *
rdbt_svc_obj(struct ds_rsvc *rsvc)
{
	return container_of(rsvc, struct rdbt_svc, rt_rsvc);
}

#define ID_OK(id) ioveq((id), &test_svc_id)

#define MUST(call)							\
do {									\
	int _rc = call;							\
									\
	D_ASSERTF(_rc == 0, "%d\n", _rc);				\
} while (0)

static void
iovok(const d_iov_t *iov)
{
	D_ASSERT((iov->iov_buf == NULL && iov->iov_buf_len == 0) ||
		 (iov->iov_buf != NULL && iov->iov_buf_len > 0));
	D_ASSERT(iov->iov_len <= iov->iov_buf_len);
}

static void
ioveq(const d_iov_t *iov1, const d_iov_t *iov2)
{
	D_ASSERTF(iov1->iov_len == iov2->iov_len, DF_U64" == "DF_U64"\n",
		  iov1->iov_len, iov2->iov_len);
	D_ASSERT(memcmp(iov1->iov_buf, iov2->iov_buf, iov1->iov_len) == 0);
}

static int
test_svc_name_cb(d_iov_t *id, char **name)
{
	ID_OK(id);
	D_STRNDUP(*name, test_svc_name, strlen(test_svc_name));
	D_ASSERT(*name != NULL);
	return 0;
}

static int
test_svc_locate_cb(d_iov_t *id, char **path)
{
	int	rc;

	ID_OK(id);
	rc = asprintf(path, "%s/rdbt-%s", dss_storage_path, test_svc_name);
	D_ASSERTF(rc > 0, "%d\n", rc);
	D_ASSERT(*path != NULL);
	return 0;
}

static int
test_svc_alloc_cb(d_iov_t *id, struct ds_rsvc **svcp)
{
	struct rdbt_svc	       *svc;

	ID_OK(id);
	D_ALLOC_PTR(svc);
	D_ASSERT(svc != NULL);
	svc->rt_rsvc.s_id = test_svc_id;

	MUST(rdb_path_init(&svc->rt_root_kvs_path));
	MUST(rdb_path_push(&svc->rt_root_kvs_path, &rdb_path_root_key));
	MUST(rdb_path_clone(&svc->rt_root_kvs_path, &svc->rt_kvs1_path));
	MUST(rdb_path_push(&svc->rt_kvs1_path, &rdbt_key_kvs1));

	*svcp = &svc->rt_rsvc;
	return 0;
}

static void
test_svc_free_cb(struct ds_rsvc *rsvc)
{
	struct rdbt_svc		*svc;

	D_ASSERT(rsvc != NULL);
	svc = rdbt_svc_obj(rsvc);
	rdb_path_fini(&svc->rt_kvs1_path);
	rdb_path_fini(&svc->rt_root_kvs_path);
	D_FREE(svc);
}

static int
test_svc_step_up_cb(struct ds_rsvc *svc)
{
	d_rank_t rank;
	int	 rc;

	rc = crt_group_rank(NULL, &rank);
	D_ASSERTF(rc == 0, "%d\n", rc);
	D_WARN("rank %u became leader of term "DF_U64"\n", rank, svc->s_term);
	return 0;
}

static void
test_svc_step_down_cb(struct ds_rsvc *svc)
{
	d_rank_t rank;
	int	 rc;

	rc = crt_group_rank(NULL, &rank);
	D_ASSERTF(rc == 0, "%d\n", rc);
	D_WARN("rank %u is no longer leader of term "DF_U64"\n", rank,
	       svc->s_term);
}

static void
test_svc_drain_cb(struct ds_rsvc *rsvc)
{
}

static struct ds_rsvc_class test_svc_rsvc_class = {
	.sc_name	= test_svc_name_cb,
	.sc_locate	= test_svc_locate_cb,
	.sc_alloc	= test_svc_alloc_cb,
	.sc_free	= test_svc_free_cb,
	.sc_step_up	= test_svc_step_up_cb,
	.sc_step_down	= test_svc_step_down_cb,
	.sc_drain	= test_svc_drain_cb
};

static void
rdbt_test_util(void)
{
	d_iov_t	empty = {};
	d_iov_t	v1;
	d_iov_t	v2;
	char		buf1[] = "012345678901234";
	char		buf2[32];
	size_t		len1;
	size_t		len2;
	ssize_t		n;

	D_WARN("encode/decode empty iov\n");
	v1 = empty;
	len1 = rdb_encode_iov(&v1, NULL);
	D_ASSERTF(len1 == sizeof(uint32_t) * 2, "%zu\n", len1);
	len2 = rdb_encode_iov(&v1, buf2);
	D_ASSERTF(len2 == len1, "%zu == %zu\n", len2, len1);
	v2 = empty;
	n = rdb_decode_iov(buf2, len2, &v2);
	D_ASSERTF(n == len2, "%zd == %zu\n", n, len2);
	iovok(&v2);
	ioveq(&v1, &v2);

	D_WARN("encode/decode non-empty iov\n");
	d_iov_set(&v1, buf1, strlen(buf1) + 1);
	len1 = rdb_encode_iov(&v1, NULL);
	D_ASSERTF(len1 == sizeof(uint32_t) * 2 + strlen(buf1) + 1, "%zu\n",
		  len1);
	D_ASSERT(len1 <= sizeof(buf2));
	len2 = rdb_encode_iov(&v1, buf2);
	D_ASSERTF(len2 == len1, "%zu == %zu\n", len2, len1);
	v2 = empty;
	n = rdb_decode_iov(buf2, len2, &v2);
	D_ASSERTF(n == len2, "%zd == %zu\n", n, len2);
	iovok(&v2);
	ioveq(&v1, &v2);
}

struct rdbt_test_path_arg {
	int		n;
	d_iov_t     *keys;
	int		nkeys;
};

static int
rdbt_test_path_cb(d_iov_t *key, void *varg)
{
	struct rdbt_test_path_arg *arg = varg;

	if (arg->keys != NULL)
		ioveq(key, &arg->keys[arg->n]);
	arg->n++;
	return 0;
}

RDB_STRING_KEY(rdbt_key_, foo);

static void
rdbt_test_path(void)
{
	d_iov_t			keys[] = {
		{.iov_buf = "a", .iov_buf_len = 2, .iov_len = 2},
		{.iov_buf = "bPPP", .iov_buf_len = 5, .iov_len = 1},
		{.iov_buf = "c\0\0", .iov_buf_len = 4, .iov_len = 3},
		{.iov_buf = "", .iov_buf_len = 1, .iov_len = 1},
		{.iov_buf = "e", .iov_buf_len = 2, .iov_len = 2}
	};
	rdb_path_t			path;
	struct rdbt_test_path_arg	arg = {};
	int				i;
	int				rc;

	D_WARN("RDB_STRING_KEY\n");
	D_ASSERTF(rdbt_key_foo.iov_len == strlen("foo") + 1, DF_U64"\n",
		  rdbt_key_foo.iov_len);
	D_ASSERTF(rdbt_key_foo.iov_buf_len == rdbt_key_foo.iov_len, DF_U64"\n",
		  rdbt_key_foo.iov_buf_len);

	D_WARN("init rdb path\n");
	MUST(rdb_path_init(&path));
	iovok(&path);

	D_WARN("pop empty rdb path\n");
	rc = rdb_path_pop(&path);
	D_ASSERTF(rc == -DER_NONEXIST, "%d\n", rc);

	D_WARN("iterate empty rdb path\n");
	MUST(rdb_path_iterate(&path, rdbt_test_path_cb, &arg));
	D_ASSERTF(arg.n == 0, "%d\n", arg.n);

	D_WARN("push to rdb path\n");
	for (i = 0; i < ARRAY_SIZE(keys); i++) {
		MUST(rdb_path_push(&path, &keys[i]));
		iovok(&path);
	}

	D_WARN("pop rdb path\n");
	MUST(rdb_path_pop(&path));

	D_WARN("iterate non-empty rdb path\n");
	arg.n = 0;
	arg.keys = keys;
	arg.nkeys = ARRAY_SIZE(keys) - 1; /* popped one already */
	MUST(rdb_path_iterate(&path, rdbt_test_path_cb, &arg));
	D_ASSERTF(arg.n == arg.nkeys, "%d\n", arg.n);

	D_WARN("fini rdb path\n");
	rdb_path_fini(&path);
}

struct iterate_cb_arg {
	uint64_t       *keys;
	int		nkeys;
	int		i;
};

static int
iterate_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *varg)
{
	struct iterate_cb_arg *arg = varg;

	D_ASSERTF(key->iov_len == sizeof(arg->keys[arg->i]), "%zu\n",
		  key->iov_len);
	D_ASSERT(memcmp(key->iov_buf, &arg->keys[arg->i],
			sizeof(arg->keys[arg->i])) == 0);
	(arg->i)++;
	return 0;
}

static int
rdbt_ping(struct rsvc_hint *hintp)
{
	struct ds_rsvc  *svc;
	int		rc;

	D_WARN("lookup leader\n");
	rc = ds_rsvc_lookup_leader(DS_RSVC_CLASS_TEST, &test_svc_id, &svc,
				   hintp);
	if (rc != 0) {
		if (rc == -DER_NOTLEADER) {
			if (hintp->sh_flags & RSVC_HINT_VALID)
				D_WARN("not leader; try rank %u\n",
				       hintp->sh_rank);
			else
				D_WARN("not leader\n");
		} else if (rc == -DER_NOTREPLICA) {
			D_WARN("not a replica\n");
		} else {
			D_WARN("unknown error, rc=%d\n", rc);
		}
	} else {
		D_WARN("leader, hint is %s valid, rank=%u, term="DF_U64"\n",
		       ((hintp->sh_flags & RSVC_HINT_VALID) ? "" : "NOT"),
		       hintp->sh_rank, hintp->sh_term);
		ds_rsvc_put_leader(svc);
	}

	return rc;
}

static int
rdbt_create(struct rsvc_hint *hintp)
{
	struct ds_rsvc	       *rsvc;
	struct rdbt_svc	       *svc;
	struct rdb_tx		tx;
	struct rdb_kvs_attr	attr;
	int			rc;

	D_WARN("lookup leader\n");
	rc = ds_rsvc_lookup_leader(DS_RSVC_CLASS_TEST, &test_svc_id, &rsvc,
				   hintp);
	if (rc != 0) {
		if (rc == -DER_NOTLEADER) {
			if (hintp->sh_flags & RSVC_HINT_VALID)
				D_WARN("not leader; try rank %u\n",
				       hintp->sh_rank);
			else
				D_WARN("not leader\n");
		} else if (rc == -DER_NOTREPLICA) {
			D_WARN("not a replica\n");
		} else {
			D_WARN("unknown error, rc=%d\n", rc);
		}
		goto out;
	}

	D_WARN("leader, hint is %s valid, rank=%u, term="DF_U64"\n",
	       ((hintp->sh_flags & RSVC_HINT_VALID) ? "" : "NOT"),
	       hintp->sh_rank, hintp->sh_term);
	svc = rdbt_svc_obj(rsvc);

	D_WARN("create KVSs and regular keys\n");
	MUST(rdb_tx_begin(rsvc->s_db, RDB_NIL_TERM, &tx));
	/* Create the root KVS. */
	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 4;
	MUST(rdb_tx_create_root(&tx, &attr));
	/* Create a KVS 'kvs1' under the root KVS. */
	attr.dsa_class = RDB_KVS_INTEGER;
	attr.dsa_order = 4;
	MUST(rdb_tx_create_kvs(&tx, &svc->rt_root_kvs_path,
			       &rdbt_key_kvs1, &attr));
	MUST(rdb_tx_commit(&tx));
	rdb_tx_end(&tx);

	ds_rsvc_put_leader(rsvc);

out:
	return rc;
}

static int
rdbt_destroy(struct rsvc_hint *hintp)
{
	struct ds_rsvc *rsvc;
	struct rdbt_svc	       *svc;
	struct rdb_tx		tx;
	int		rc;

	D_WARN("lookup leader\n");
	rc = ds_rsvc_lookup_leader(DS_RSVC_CLASS_TEST, &test_svc_id, &rsvc,
				   hintp);
	if (rc != 0) {
		if (rc == -DER_NOTLEADER) {
			if (hintp->sh_flags & RSVC_HINT_VALID)
				D_WARN("not leader; try rank %u\n",
				       hintp->sh_rank);
			else
				D_WARN("not leader\n");
		} else if (rc == -DER_NOTREPLICA) {
			D_WARN("not a replica\n");
		} else {
			D_WARN("unknown error, rc=%d\n", rc);
		}
		goto out;
	}

	D_WARN("leader, hint is %s valid, rank=%u, term="DF_U64"\n",
	       ((hintp->sh_flags & RSVC_HINT_VALID) ? "" : "NOT"),
	       hintp->sh_rank, hintp->sh_term);
	svc = rdbt_svc_obj(rsvc);

	D_WARN("destroy KVSs\n");
	MUST(rdb_tx_begin(rsvc->s_db, RDB_NIL_TERM, &tx));
	MUST(rdb_tx_destroy_kvs(&tx, &svc->rt_root_kvs_path, &rdbt_key_kvs1));
	MUST(rdb_tx_destroy_root(&tx));
	MUST(rdb_tx_commit(&tx));
	rdb_tx_end(&tx);

	ds_rsvc_put_leader(rsvc);

out:
	return rc;
}

static int
rdbt_test_tx(bool update, enum rdbt_membership_op memb_op, uint64_t user_key,
	     uint64_t user_val_in, uint64_t *user_val_outp,
	     struct rsvc_hint *hintp)
{
	struct ds_rsvc	       *rsvc;
	struct rdbt_svc	       *svc;
	d_iov_t			key;
	d_iov_t			value;
	char			value_written[] = "value";
	char			buf[32];
	uint64_t		keys[] = {11, 22, 33, user_key};
	struct rdb_tx		tx;
	struct iterate_cb_arg	arg;
	uint64_t		k = 0;
	int			i;
	int			rc;

	D_WARN("lookup leader\n");
	rc = ds_rsvc_lookup_leader(DS_RSVC_CLASS_TEST, &test_svc_id,
				   &rsvc, hintp);
	if (rc != 0) {
		if (rc == -DER_NOTLEADER) {
			if (hintp->sh_flags & RSVC_HINT_VALID)
				D_WARN("not leader; try rank %u\n",
				       hintp->sh_rank);
			else
				D_WARN("not leader\n");
		} else if (rc == -DER_NOTREPLICA) {
			D_WARN("not a replica\n");
		} else {
			D_WARN("unknown error, rc=%d\n", rc);
		}
		return rc;
	}

	D_WARN("leader, hint is %s valid, rank=%u, term="DF_U64"\n",
	       ((hintp->sh_flags & RSVC_HINT_VALID) ? "" : "NOT"),
	       hintp->sh_rank, hintp->sh_term);

	svc = rdbt_svc_obj(rsvc);

	D_WARN("commit empty tx\n");
	MUST(rdb_tx_begin(svc->rt_rsvc.s_db, RDB_NIL_TERM, &tx));
	MUST(rdb_tx_commit(&tx));
	rdb_tx_end(&tx);

	if (update) {
		D_WARN("update: user record: (K=0x%"PRIx64", V="DF_U64")\n",
		       user_key, user_val_in);
		MUST(rdb_tx_begin(svc->rt_rsvc.s_db, RDB_NIL_TERM, &tx));

		/* verify KVS "kvs1" has been created in root KVS */
		d_iov_set(&value, NULL /* buf */, 0 /* size */);
		MUST(rdb_tx_lookup(&tx, &svc->rt_root_kvs_path, &rdbt_key_kvs1,
				   &value));

		/* Update keys in "kvs1". */
		for (i = 0; i < ARRAY_SIZE(keys); i++) {
			d_iov_set(&key, &keys[i], sizeof(keys[0]));
			if (keys[i] == user_key)
				d_iov_set(&value, &user_val_in,
					  sizeof(user_val_in));
			else
				d_iov_set(&value, value_written,
					  strlen(value_written) + 1);
			MUST(rdb_tx_update(&tx, &svc->rt_kvs1_path,
					   &key, &value));
		}

		/* If testing membership change, will cause tx commit to fail */
		D_WARN("membership change op: %s\n",
		       rdbt_membership_opname(memb_op));
		switch (memb_op) {
		case RDBT_MEMBER_RESIGN:
			/* Lose leadership */
			rdb_resign(svc->rt_rsvc.s_db, svc->rt_rsvc.s_term);
			break;
		case RDBT_MEMBER_CAMPAIGN:
			/* Call election, likely retain leadership (new term) */
			MUST(rdb_campaign(svc->rt_rsvc.s_db));
			break;
		default:
			break;
		}
		/* Commit. */
		rc = rdb_tx_commit(&tx);
		rdb_tx_end(&tx);
		if (rc != 0) {
			ds_rsvc_put_leader(rsvc);
			return rc;
		}
	}

	D_WARN("query regular keys\n");
	MUST(rdb_tx_begin(svc->rt_rsvc.s_db, RDB_NIL_TERM, &tx));
	/* Look up keys[0]. */
	d_iov_set(&key, &keys[0], sizeof(keys[0]));
	d_iov_set(&value, buf, sizeof(buf));
	value.iov_len = 0; /* no size check */
	MUST(rdb_tx_lookup(&tx, &svc->rt_kvs1_path, &key, &value));
	D_ASSERTF(value.iov_len == strlen(value_written) + 1, DF_U64" == %zu\n",
		  value.iov_len, strlen(value_written) + 1);
	D_ASSERT(memcmp(value.iov_buf, value_written,
			strlen(value_written) + 1) == 0);
	/* Iterate "kvs1". */
	arg.keys = keys;
	arg.nkeys = ARRAY_SIZE(keys);
	arg.i = 0;
	MUST(rdb_tx_iterate(&tx, &svc->rt_kvs1_path, false /* backward */,
			    iterate_cb, &arg));
	/* Fetch the first key. */
	d_iov_set(&key, &k, sizeof(k));
	d_iov_set(&value, NULL, 0);
	MUST(rdb_tx_fetch(&tx, &svc->rt_kvs1_path, RDB_PROBE_FIRST, NULL,
			  &key, &value));
	D_ASSERTF(key.iov_len == sizeof(k), DF_U64" == %zu\n", key.iov_len,
		  sizeof(k));
	D_ASSERTF(k == keys[0], DF_U64" == "DF_U64"\n", k, keys[0]);
	D_ASSERTF(value.iov_len == strlen(value_written) + 1, DF_U64" == %zu\n",
		  value.iov_len, strlen(value_written) + 1);
	D_ASSERT(memcmp(value.iov_buf, value_written,
			strlen(value_written) + 1) == 0);

	/* Lookup user key */
	d_iov_set(&key, &user_key, sizeof(user_key));
	d_iov_set(&value, user_val_outp, sizeof(*user_val_outp));
	MUST(rdb_tx_lookup(&tx, &svc->rt_kvs1_path, &key, &value));
	MUST(rdb_tx_commit(&tx));
	D_WARN("lookup: user record: (K=0x%"PRIx64", V="DF_U64")\n",
	       user_key, *user_val_outp);
	rdb_tx_end(&tx);

	ds_rsvc_put_leader(rsvc);
	return 0;
}

static void
get_all_ranks(d_rank_list_t **list)
{
	crt_group_t	*group;
	d_rank_list_t	*ranks;
	int		 i;

	group = crt_group_lookup(NULL /* grp_id */);
	D_ASSERT(group != NULL);
	MUST(crt_group_ranks_get(group, list));
	if (*list != NULL)
		return;
	D_ALLOC_PTR(ranks);
	D_ASSERT(ranks != NULL);
	MUST(crt_group_size(group, &ranks->rl_nr));
	D_ALLOC_ARRAY(ranks->rl_ranks, ranks->rl_nr);
	D_ASSERT(ranks->rl_ranks != NULL);
	for (i = 0; i < ranks->rl_nr; ++i)
		ranks->rl_ranks[i] = i;
	*list = ranks;
}

static void
rdbt_init_handler(crt_rpc_t *rpc)
{
	struct rdbt_init_in	*in = crt_req_get(rpc);
	d_rank_t		 rank;
	d_rank_t		 ri;
	d_rank_list_t		*ranks;

	MUST(crt_group_rank(NULL /* grp */, &rank));
	get_all_ranks(&ranks);
	D_ASSERT(ranks != NULL);
	if (in->tii_nreplicas < ranks->rl_nr)
		ranks->rl_nr = in->tii_nreplicas;

	D_WARN("initializing rank %u: nreplicas=%u\n", rank, ranks->rl_nr);
	for (ri = 0; ri < ranks->rl_nr; ri++)
		D_WARN("ranks[%u]=%u\n", ri, ranks->rl_ranks[ri]);

	MUST(ds_rsvc_dist_start(DS_RSVC_CLASS_TEST, &test_svc_id, in->tii_uuid,
				ranks, true /* create */, true /* bootstrap */,
				DB_CAP));
	crt_reply_send(rpc);
}

static void
rdbt_fini_handler(crt_rpc_t *rpc)
{
	struct ds_rsvc		*rsvc;
	d_rank_t		 rank;
	d_rank_list_t		*ranks;
	d_rank_t		 ri;

	MUST(crt_group_rank(NULL /* grp */, &rank));
	D_WARN("finalizing rank %u\n", rank);
	MUST(ds_rsvc_lookup(DS_RSVC_CLASS_TEST, &test_svc_id, &rsvc));
	MUST(rdb_get_ranks(rsvc->s_db, &ranks));
	ds_rsvc_put(rsvc);
	D_WARN("finalizing rank %u: nreplicas=%u\n", rank, ranks->rl_nr);
	for (ri = 0; ri < ranks->rl_nr; ri++)
		D_WARN("ranks[%u]=%u\n", ri, ranks->rl_ranks[ri]);

	MUST(ds_rsvc_dist_stop(DS_RSVC_CLASS_TEST, &test_svc_id, ranks, NULL,
			       true));
	crt_reply_send(rpc);
}

static void
rdbt_ping_handler(crt_rpc_t *rpc)
{
	struct rdbt_ping_out   *out = crt_reply_get(rpc);
	d_rank_t		rank;
	int			rc;

	MUST(crt_group_rank(NULL /* grp */, &rank));
	D_WARN("testing rank %u: ping\n", rank);

	rc = rdbt_ping(&out->tpo_hint);
	out->tpo_rc = rc;

	D_WARN("rpc reply from rank %u: rc=%d\n", rank, rc);
	crt_reply_send(rpc);
}

static void
rdbt_create_handler(crt_rpc_t *rpc)
{
	struct rdbt_create_out *out = crt_reply_get(rpc);
	d_rank_t		rank;
	int			rc;

	MUST(crt_group_rank(NULL /* grp */, &rank));
	D_WARN("rank %u: received create kvstores RPC\n", rank);

	rc = rdbt_create(&out->tco_hint);
	out->tco_rc = rc;

	D_WARN("rpc reply from rank %u: rc=%d\n", rank, rc);
	crt_reply_send(rpc);
}

static void
rdbt_destroy_handler(crt_rpc_t *rpc)
{
	struct rdbt_destroy_out	       *out = crt_reply_get(rpc);
	d_rank_t			rank;
	int				rc;

	MUST(crt_group_rank(NULL /* grp */, &rank));
	D_WARN("rank %u: received destroy kvstores RPC\n", rank);

	rc = rdbt_destroy(&out->tdo_hint);
	out->tdo_rc = rc;

	D_WARN("rpc reply from rank %u: rc=%d\n", rank, rc);
	crt_reply_send(rpc);
}

static void
rdbt_test_handler(crt_rpc_t *rpc)
{
	struct rdbt_test_in    *in = crt_req_get(rpc);
	struct rdbt_test_out   *out = crt_reply_get(rpc);
	d_rank_t		rank;
	int			rc;

	MUST(crt_group_rank(NULL /* grp */, &rank));
	D_WARN("testing rank %u: update=%d %s\n", rank, in->tti_update,
	       rdbt_membership_opname(in->tti_memb_op));
	rdbt_test_util();
	rdbt_test_path();
	rc = rdbt_test_tx(in->tti_update, in->tti_memb_op, in->tti_key,
			  in->tti_val, &out->tto_val, &out->tto_hint);
	out->tto_rc = rc;
	D_WARN("rpc reply from rank %u: tto_rc=%d\n", rank, rc);
	crt_reply_send(rpc);
}

static int
rdbt_module_init(void)
{
	d_iov_set(&test_svc_id, test_svc_name, strlen(test_svc_name) + 1);
	ds_rsvc_class_register(DS_RSVC_CLASS_TEST, &test_svc_rsvc_class);
	return 0;
}

static int
rdbt_module_fini(void)
{
	ds_rsvc_class_unregister(DS_RSVC_CLASS_TEST);
	return 0;
}

static void
rdbt_replicas_add_handler(crt_rpc_t *rpc)
{
	struct rdbt_replicas_add_in	*in = crt_req_get(rpc);
	struct rdbt_replicas_add_out	*out = crt_reply_get(rpc);
	d_rank_list_t			*ranks;
	d_rank_t			 rank;
	int				 rc;

	MUST(crt_group_rank(NULL /* grp */, &rank));
	D_WARN("rank %u: replicas add RPC request\n", rank);
	rc = daos_rank_list_dup(&ranks, in->rtmi_ranks);
	if (rc != 0)
		goto out;

	rc = ds_rsvc_add_replicas(DS_RSVC_CLASS_TEST, &test_svc_id, ranks,
				  DB_CAP, &out->rtmo_hint);
	out->rtmo_failed = ranks;

out:
	D_WARN("rpc reply from rank %u: rc=%d\n", rank, rc);
	out->rtmo_rc = rc;
	crt_reply_send(rpc);
}

static void
rdbt_replicas_remove_handler(crt_rpc_t *rpc)
{
	struct rdbt_replicas_remove_in	*in = crt_req_get(rpc);
	struct rdbt_replicas_remove_out	*out = crt_reply_get(rpc);
	d_rank_list_t			*ranks;
	d_rank_t			 rank;
	int				 rc;

	MUST(crt_group_rank(NULL /* grp */, &rank));
	D_WARN("rank %u: replicas remove RPC request\n", rank);
	rc = daos_rank_list_dup(&ranks, in->rtmi_ranks);
	if (rc != 0)
		goto out;

	rc = ds_rsvc_remove_replicas(DS_RSVC_CLASS_TEST, &test_svc_id, ranks,
				     true /* stop */, &out->rtmo_hint);
	out->rtmo_failed = ranks;

out:
	D_WARN("rpc reply from rank %u: rc=%d\n", rank, rc);
	out->rtmo_rc = rc;
	crt_reply_send(rpc);
}

static void
rdbt_start_election_handler(crt_rpc_t *rpc)
{
	struct ds_rsvc			*rsvc;
	struct rdbt_start_election_out	*out = crt_reply_get(rpc);
	d_rank_t			 rank;
	int				 rc;

	MUST(crt_group_rank(NULL /* grp */, &rank));
	D_WARN("rank %u calling new election\n", rank);

	rc = ds_rsvc_lookup(DS_RSVC_CLASS_TEST, &test_svc_id, &rsvc);
	if (rc != 0)
		goto out;

	rc = rdb_campaign(rsvc->s_db);
	ds_rsvc_put(rsvc);

out:
	D_WARN("rpc reply from rank %u: rc=%d\n", rank, rc);
	out->rtse_rc = rc;
	crt_reply_send(rpc);

}

/* Define for cont_rpcs[] array population below.
 * See RDBT_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.dr_opc       = a,	\
	.dr_hdlr      = d,	\
	.dr_corpc_ops = e,	\
}

static struct daos_rpc_handler rdbt_handlers[] = {
	RDBT_PROTO_CLI_RPC_LIST,
};

#undef X

struct dss_module rdbt_module = {
	.sm_name	= "rdbt",
	.sm_mod_id	= DAOS_RDBT_MODULE,
	.sm_ver		= DAOS_RDBT_VERSION,
	.sm_init	= rdbt_module_init,
	.sm_fini	= rdbt_module_fini,
	.sm_proto_fmt	= &rdbt_proto_fmt,
	.sm_cli_count	= RDBT_PROTO_CLI_COUNT,
	.sm_handlers	= rdbt_handlers,
	.sm_key		= NULL
};
