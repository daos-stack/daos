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
#define D_LOGFAC	DD_FAC(rdb)

#include <daos_srv/daos_server.h>	/* for dss_module */
#include <daos_srv/rdb.h>
#include <daos_srv/rsvc.h>
#include "../rdb_internal.h"
#include "rpc.h"

#define DB_CAP	(1L << 25)

static char		*test_svc_name = "rsvc_test";
static d_iov_t	 test_svc_id;
static uuid_t		 test_db_uuid;

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
test_svc_load_uuid_cb(d_iov_t *id, uuid_t db_uuid)
{
	ID_OK(id);
	uuid_copy(db_uuid, test_db_uuid);
	return 0;
}

static int
test_svc_store_uuid_cb(d_iov_t *id, uuid_t db_uuid)
{
	ID_OK(id);
	return 0;
}

static int
test_svc_delete_uuid_cb(d_iov_t *id)
{
	ID_OK(id);
	return 0;
}

static int
test_svc_locate_cb(d_iov_t *id, char **path)
{
	char	uuid_string[DAOS_UUID_STR_SIZE];
	int	rc;

	ID_OK(id);
	uuid_unparse_lower(test_db_uuid, uuid_string);
	rc = asprintf(path, "%s/rdbt-%s", dss_storage_path, uuid_string);
	D_ASSERTF(rc > 0, "%d\n", rc);
	D_ASSERT(*path != NULL);
	return 0;
}

static int
test_svc_alloc_cb(d_iov_t *id, struct ds_rsvc **svcp)
{
	ID_OK(id);
	D_ALLOC_PTR(*svcp);
	D_ASSERT(*svcp != NULL);
	(*svcp)->s_id = test_svc_id;
	return 0;
}

static void
test_svc_free_cb(struct ds_rsvc *svc)
{
	D_ASSERT(svc != NULL);
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
	.sc_load_uuid	= test_svc_load_uuid_cb,
	.sc_store_uuid	= test_svc_store_uuid_cb,
	.sc_delete_uuid	= test_svc_delete_uuid_cb,
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

static void
rdbt_test_tx(bool update)
{
	struct ds_rsvc	       *svc;
	struct rsvc_hint	hint;
	rdb_path_t		path;
	d_iov_t		key;
	d_iov_t		value;
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
	rc = ds_rsvc_lookup_leader(DS_RSVC_CLASS_TEST, &test_svc_id, &svc,
				   &hint);
	if (rc == -DER_NOTLEADER) {
		if (hint.sh_flags & RSVC_HINT_VALID)
			D_WARN("not leader; try rank %u\n", hint.sh_rank);
		else
			D_WARN("not leader\n");
		return;
	}
	MUST(rdb_tx_begin(svc->s_db, RDB_NIL_TERM, &tx));
	MUST(rdb_tx_commit(&tx));
	rdb_tx_end(&tx);

	if (update) {
		D_WARN("create KVSs and regular keys\n");
		MUST(rdb_tx_begin(svc->s_db, RDB_NIL_TERM, &tx));
		/* Create the root KVS. */
		MUST(rdb_path_init(&path));
		MUST(rdb_path_push(&path, &rdb_path_root_key));
		attr.dsa_class = RDB_KVS_GENERIC;
		attr.dsa_order = 4;
		MUST(rdb_tx_create_root(&tx, &attr));
		/* Create a KVS 'kvs1' under the root KVS. */
		d_iov_set(&key, "kvs1", strlen("kvs1") + 1);
		attr.dsa_class = RDB_KVS_INTEGER;
		attr.dsa_order = 4;
		MUST(rdb_tx_create_kvs(&tx, &path, &key, &attr));
		/* Update keys in "kvs1". */
		MUST(rdb_path_push(&path, &key));
		for (i = 0; i < ARRAY_SIZE(keys); i++) {
			d_iov_set(&key, &keys[i], sizeof(keys[0]));
			d_iov_set(&value, value_written,
				     strlen(value_written) + 1);
			MUST(rdb_tx_update(&tx, &path, &key, &value));
		}
		rdb_path_fini(&path);
		/* Commit. */
		MUST(rdb_tx_commit(&tx));
		rdb_tx_end(&tx);
	}

	D_WARN("query regular keys\n");
	MUST(rdb_tx_begin(svc->s_db, RDB_NIL_TERM, &tx));
	MUST(rdb_path_init(&path));
	/* Look up keys[0]. */
	MUST(rdb_path_push(&path, &rdb_path_root_key));
	d_iov_set(&key, "kvs1", strlen("kvs1") + 1);
	MUST(rdb_path_push(&path, &key));
	d_iov_set(&key, &keys[0], sizeof(keys[0]));
	d_iov_set(&value, buf, sizeof(buf));
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
	d_iov_set(&key, &k, sizeof(k));
	d_iov_set(&value, NULL, 0);
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
		MUST(rdb_tx_begin(svc->s_db, RDB_NIL_TERM, &tx));
		MUST(rdb_path_init(&path));
		MUST(rdb_path_push(&path, &rdb_path_root_key));
		d_iov_set(&key, "kvs1", strlen("kvs1") + 1);
		MUST(rdb_tx_destroy_kvs(&tx, &path, &key));
		rdb_path_fini(&path);
		MUST(rdb_tx_destroy_root(&tx));
		MUST(rdb_tx_commit(&tx));
		rdb_tx_end(&tx);
	}

	ds_rsvc_put_leader(svc);
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
	d_rank_list_t		*ranks;

	uuid_copy(test_db_uuid, in->tii_uuid);
	MUST(crt_group_rank(NULL /* grp */, &rank));
	get_all_ranks(&ranks);
	D_ASSERT(ranks != NULL);
	if (in->tii_nreplicas < ranks->rl_nr)
		ranks->rl_nr = in->tii_nreplicas;

	D_WARN("initializing rank %u: nreplicas=%u\n", rank, ranks->rl_nr);
	MUST(ds_rsvc_dist_start(DS_RSVC_CLASS_TEST, &test_svc_id, test_db_uuid,
				ranks, true /* create */, true /* bootstrap */,
				DB_CAP));
	crt_reply_send(rpc);
}

static void
rdbt_fini_handler(crt_rpc_t *rpc)
{
	struct ds_rsvc	*svc;
	d_rank_t	 rank;
	d_rank_list_t	*ranks;

	MUST(crt_group_rank(NULL /* grp */, &rank));
	D_WARN("finalizing rank %u\n", rank);
	MUST(ds_rsvc_lookup(DS_RSVC_CLASS_TEST, &test_svc_id, &svc));
	MUST(rdb_get_ranks(svc->s_db, &ranks));
	ds_rsvc_put(svc);
	MUST(ds_rsvc_dist_stop(DS_RSVC_CLASS_TEST, &test_svc_id, ranks, NULL,
			       true));
	crt_reply_send(rpc);
}

static void
rdbt_test_handler(crt_rpc_t *rpc)
{
	struct rdbt_test_in    *in = crt_req_get(rpc);
	d_rank_t		rank;

	MUST(crt_group_rank(NULL /* grp */, &rank));
	D_WARN("testing rank %u: update=%d\n", rank, in->tti_update);
	rdbt_test_util();
	rdbt_test_path();
	rdbt_test_tx(in->tti_update);
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
