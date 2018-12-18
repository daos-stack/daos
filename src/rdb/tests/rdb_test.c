/**
 * (C) Copyright 2017-2018 Intel Corporation.
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
#define D_LOGFAC	DD_FAC(rdb)

#include <daos_srv/daos_server.h>	/* for dss_module */
#include <daos_srv/rdb.h>
#include "../rdb_internal.h"
#include "rpc.h"

static char	       *rdb_file_path;
static uuid_t		rdb_uuid;
static struct rdb      *rdb_db;

#define MUST(call)							\
do {									\
	int _rc = call;							\
									\
	D_ASSERTF(_rc == 0, "%d\n", _rc);				\
} while (0)

static void
iovok(const daos_iov_t *iov)
{
	D_ASSERT((iov->iov_buf == NULL && iov->iov_buf_len == 0) ||
		 (iov->iov_buf != NULL && iov->iov_buf_len > 0));
	D_ASSERT(iov->iov_len <= iov->iov_buf_len);
}

static void
ioveq(const daos_iov_t *iov1, const daos_iov_t *iov2)
{
	D_ASSERTF(iov1->iov_len == iov2->iov_len, DF_U64" == "DF_U64"\n",
		  iov1->iov_len, iov2->iov_len);
	D_ASSERT(memcmp(iov1->iov_buf, iov2->iov_buf, iov1->iov_len) == 0);
}

static void
rdbt_test_util(void)
{
	daos_iov_t	empty = {};
	daos_iov_t	v1;
	daos_iov_t	v2;
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
	daos_iov_set(&v1, buf1, strlen(buf1) + 1);
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
	daos_iov_t     *keys;
	int		nkeys;
};

static int
rdbt_test_path_cb(daos_iov_t *key, void *varg)
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
	daos_iov_t			keys[] = {
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

static char *
rdbt_path(uuid_t uuid)
{
	char   *path;
	char	uuid_string[DAOS_UUID_STR_SIZE];
	int	rc;

	uuid_unparse_lower(uuid, uuid_string);
	rc = asprintf(&path, "%s/rdbt-%s", dss_storage_path, uuid_string);
	D_ASSERT(rc > 0 && path != NULL);
	return path;
}

struct iterate_cb_arg {
	uint64_t       *keys;
	int		nkeys;
	int		i;
};

static int
iterate_cb(daos_handle_t ih, daos_iov_t *key, daos_iov_t *val, void *varg)
{
	struct iterate_cb_arg *arg = varg;

	D_ASSERTF(key->iov_len == sizeof(arg->keys[arg->i]), "%zu\n",
		  key->iov_len);
	D_ASSERT(memcmp(key->iov_buf, &arg->keys[arg->i],
			sizeof(arg->keys[arg->i])) == 0);
	(arg->i)++;
	return 0;
}

static void
rdbt_test_tx(bool update)
{
	rdb_path_t		path;
	daos_iov_t		key;
	daos_iov_t		value;
	char			value_written[] = "value";
	char			buf[32];
	uint64_t		keys[] = {11, 22, 33};
	struct rdb_tx		tx;
	struct rdb_kvs_attr	attr;
	struct iterate_cb_arg	arg;
	uint64_t		k = 0;
	int			i;
	int			rc;

	D_WARN("commit empty tx\n");
	rc = rdb_tx_begin(rdb_db, RDB_NIL_TERM, &tx);
	if (rc == -DER_NOTLEADER) {
		uint64_t	term;
		d_rank_t	rank;

		rc = rdb_get_leader(rdb_db, &term, &rank);
		if (rc == 0)
			D_WARN("not leader; try rank %u\n", rank);
		else
			D_WARN("not leader\n");
		return;
	}
	MUST(rdb_tx_commit(&tx));
	rdb_tx_end(&tx);

	if (update) {
		D_WARN("create KVSs and regular keys\n");
		MUST(rdb_tx_begin(rdb_db, RDB_NIL_TERM, &tx));
		/* Create the root KVS. */
		MUST(rdb_path_init(&path));
		MUST(rdb_path_push(&path, &rdb_path_root_key));
		attr.dsa_class = RDB_KVS_GENERIC;
		attr.dsa_order = 4;
		MUST(rdb_tx_create_root(&tx, &attr));
		/* Create a KVS 'kvs1' under the root KVS. */
		daos_iov_set(&key, "kvs1", strlen("kvs1") + 1);
		attr.dsa_class = RDB_KVS_INTEGER;
		attr.dsa_order = 4;
		MUST(rdb_tx_create_kvs(&tx, &path, &key, &attr));
		/* Update keys in "kvs1". */
		MUST(rdb_path_push(&path, &key));
		for (i = 0; i < ARRAY_SIZE(keys); i++) {
			daos_iov_set(&key, &keys[i], sizeof(keys[0]));
			daos_iov_set(&value, value_written,
				     strlen(value_written) + 1);
			MUST(rdb_tx_update(&tx, &path, &key, &value));
		}
		rdb_path_fini(&path);
		/* Commit. */
		MUST(rdb_tx_commit(&tx));
		rdb_tx_end(&tx);
	}

	D_WARN("query regular keys\n");
	MUST(rdb_tx_begin(rdb_db, RDB_NIL_TERM, &tx));
	MUST(rdb_path_init(&path));
	/* Look up keys[0]. */
	MUST(rdb_path_push(&path, &rdb_path_root_key));
	daos_iov_set(&key, "kvs1", strlen("kvs1") + 1);
	MUST(rdb_path_push(&path, &key));
	daos_iov_set(&key, &keys[0], sizeof(keys[0]));
	daos_iov_set(&value, buf, sizeof(buf));
	value.iov_len = 0; /* no size check */
	MUST(rdb_tx_lookup(&tx, &path, &key, &value));
	D_ASSERTF(value.iov_len == strlen(value_written) + 1, DF_U64" == %zu\n",
		  value.iov_len, strlen(value_written) + 1);
	D_ASSERT(memcmp(value.iov_buf, value_written,
			strlen(value_written) + 1) == 0);
	/* Iterate "kvs1". */
	arg.keys = keys;
	arg.nkeys = ARRAY_SIZE(keys);
	arg.i = 0;
	MUST(rdb_tx_iterate(&tx, &path, false /* backward */, iterate_cb,
			    &arg));
	/* Fetch the first key. */
	daos_iov_set(&key, &k, sizeof(k));
	daos_iov_set(&value, NULL, 0);
	MUST(rdb_tx_fetch(&tx, &path, RDB_PROBE_FIRST, NULL, &key, &value));
	D_ASSERTF(key.iov_len == sizeof(k), DF_U64" == %zu\n", key.iov_len,
		  sizeof(k));
	D_ASSERTF(k == keys[0], DF_U64" == "DF_U64"\n", k, keys[0]);
	D_ASSERTF(value.iov_len == strlen(value_written) + 1, DF_U64" == %zu\n",
		  value.iov_len, strlen(value_written) + 1);
	D_ASSERT(memcmp(value.iov_buf, value_written,
			strlen(value_written) + 1) == 0);
	rdb_path_fini(&path);
	MUST(rdb_tx_commit(&tx));
	rdb_tx_end(&tx);

	if (!update) {
		D_WARN("destroy KVSs\n");
		MUST(rdb_tx_begin(rdb_db, RDB_NIL_TERM, &tx));
		MUST(rdb_path_init(&path));
		MUST(rdb_path_push(&path, &rdb_path_root_key));
		daos_iov_set(&key, "kvs1", strlen("kvs1") + 1);
		MUST(rdb_tx_destroy_kvs(&tx, &path, &key));
		rdb_path_fini(&path);
		MUST(rdb_tx_destroy_root(&tx));
		MUST(rdb_tx_commit(&tx));
		rdb_tx_end(&tx);
	}
}

static int
rdbt_module_init(void)
{
	return 0;
}

static int
rdbt_module_fini(void)
{
	return 0;
}

static int
rdbt_step_up(struct rdb *db, uint64_t term, void *arg)
{
	d_rank_t rank;

	crt_group_rank(NULL, &rank);
	D_WARN("rank %u became leader of term "DF_U64"\n", rank, term);
	return 0;
}

static void
rdbt_step_down(struct rdb *db, uint64_t term, void *arg)
{
	d_rank_t rank;

	crt_group_rank(NULL, &rank);
	D_WARN("rank %u is no longer leader of term "DF_U64"\n", rank, term);
}

static void
rdbt_stop(struct rdb *db, int err, void *arg)
{
	d_rank_t rank;

	crt_group_rank(NULL, &rank);
	D_WARN("rank %u should stop\n", rank);
	D_ASSERT(0);
}

static struct rdb_cbs rdbt_rdb_cbs = {
	.dc_step_up	= rdbt_step_up,
	.dc_step_down	= rdbt_step_down,
	.dc_stop	= rdbt_stop
};

static void
rdbt_init_handler(crt_rpc_t *rpc)
{
	struct rdbt_init_in    *in = crt_req_get(rpc);
	d_rank_t		rank;
	uint32_t		group_size;
	d_rank_list_t	ranks;
	int			i;
	int			rc;

	crt_group_rank(NULL /* grp */, &rank);
	crt_group_size(NULL /* grp */, &group_size);

	/* Build the rank list. */
	ranks.rl_nr = in->tii_nreplicas;
	if (ranks.rl_nr > group_size)
		ranks.rl_nr = group_size;
	D_ALLOC_ARRAY(ranks.rl_ranks, ranks.rl_nr);
	if (ranks.rl_ranks == NULL) {
		D_ERROR("failed to allocate ranks array\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}
	for (i = 0; i < ranks.rl_nr; i++)
		ranks.rl_ranks[i] = i;

	D_WARN("initializing rank %u: nreplicas=%u\n", rank, in->tii_nreplicas);
	rdb_file_path = rdbt_path(in->tii_uuid);
	uuid_copy(rdb_uuid, in->tii_uuid);
	MUST(rdb_create(rdb_file_path, in->tii_uuid, 1 << 25, &ranks));
	MUST(rdb_start(rdb_file_path, in->tii_uuid, &rdbt_rdb_cbs,
		       NULL /* arg */, &rdb_db));
out:
	crt_reply_send(rpc);
}

static void
rdbt_fini_handler(crt_rpc_t *rpc)
{
	d_rank_t rank;

	crt_group_rank(NULL /* grp */, &rank);
	D_WARN("finalizing rank %u\n", rank);
	rdb_stop(rdb_db);
	MUST(rdb_destroy(rdb_file_path, rdb_uuid));
	D_FREE(rdb_file_path);
	crt_reply_send(rpc);
}

static void
rdbt_test_handler(crt_rpc_t *rpc)
{
	struct rdbt_test_in    *in = crt_req_get(rpc);
	d_rank_t		rank;
	int			rc;

	rc = crt_group_rank(NULL /* grp */, &rank);
	D_ASSERTF(rc == 0, "%d\n", rc);
	D_WARN("testing rank %u: update=%d\n", rank, in->tti_update);
	rdbt_test_util();
	rdbt_test_path();
	rdbt_test_tx(in->tti_update);
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
