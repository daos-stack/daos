/* Copyright (C) 2016-2017 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * This is a runtime IV server test that implements IV framework callbacks
 * TODOs:
 * - Randomize size of keys and values
 * - Return shared buffer instead of a copy during fetch
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <gurt/common.h>
#include <gurt/list.h>

#define _SERVER
#include "iv_common.h"

static char g_hostname[100];

static d_rank_t g_my_rank;
static uint32_t g_group_size;

static int g_verbose_mode;

#define DBG_PRINT(x...)							\
	do {								\
		printf("[%s:%d:SERV]\t", g_hostname, g_my_rank);	\
		printf(x);						\
	} while (0)


/* Verbose mode:
 * 0 - disabled
 * 1 - Entry/Exists
 * 2 - Dump keys
 **/
#define DBG_ENTRY()						\
do {								\
	if (g_verbose_mode >= 1) {				\
		DBG_PRINT(">>>> Entered %s\n", __func__);	\
	}							\
} while (0)

#define DBG_EXIT()							\
do {									\
	if (g_verbose_mode >= 1) {					\
		DBG_PRINT("<<<< Exited %s:%d\n\n", __func__, __LINE__);	\
	}								\
} while (0)

struct iv_value_struct {
	/* IV value embeds root rank for verification purposes */
	d_rank_t	root_rank;

	/* Actual data string */
	char		str_data[MAX_DATA_SIZE];
};

#define NUM_WORK_CTX 9
crt_context_t g_work_ctx[NUM_WORK_CTX];
crt_context_t g_main_ctx;
static int g_do_shutdown;
pthread_t g_progress_thread[NUM_WORK_CTX + 1];
pthread_mutex_t g_key_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK_KEYS() pthread_mutex_lock(&g_key_lock)
#define UNLOCK_KEYS() pthread_mutex_unlock(&g_key_lock)

static void *
progress_function(void *data)
{
	crt_context_t *p_ctx = (crt_context_t *)data;

	while (g_do_shutdown == 0)
		crt_progress(*p_ctx, 1000, NULL, NULL);

	/* Note the first thread cleans up g_main_ctx */
	crt_context_destroy(*p_ctx, 1);

	return NULL;
}

static void
shutdown(void)
{
	int i;

	DBG_PRINT("Joining threads\n");

	for (i = 0; i < NUM_WORK_CTX + 1; i++)
		pthread_join(g_progress_thread[i], NULL);

	DBG_PRINT("Finished joining all threads\n");
}

/* handler for RPC_SHUTDOWN */
int
iv_shutdown(crt_rpc_t *rpc) {
	struct rpc_shutdown_in	*input;
	struct rpc_shutdown_out	*output;
	int			 rc;

	DBG_ENTRY();

	DBG_PRINT("Received shutdown request\n");

	input = crt_req_get(rpc);
	output = crt_reply_get(rpc);

	assert(input != NULL);
	assert(output != NULL);

	output->rc = 0;

	rc = crt_reply_send(rpc);
	assert(rc == 0);

	g_do_shutdown = 1;

	DBG_EXIT();
	return 0;
}

static void
init_work_contexts(void)
{
	int i;
	int rc;

	rc = crt_context_create(NULL, &g_main_ctx);
	assert(rc == 0);

	rc = pthread_create(&g_progress_thread[0], 0,
			    progress_function, &g_main_ctx);
	assert(rc == 0);

	for (i = 0; i < NUM_WORK_CTX; i++) {
		rc = crt_context_create(NULL, &g_work_ctx[i]);
		assert(rc == 0);

		rc = pthread_create(&g_progress_thread[i + 1], 0,
				    progress_function, &g_work_ctx[i]);
		assert(rc == 0);
	}
}

#define NUM_LOCAL_IVS 10

static uint32_t g_test_user_priv = 0xDEAD1337;

/* TODO: Change to hash table instead of list */
static D_LIST_HEAD(g_kv_pair_head);

/* Key-value pair */
struct kv_pair_entry {
	crt_iv_key_t	key;
	d_sg_list_t	value;
	bool		valid;
	d_list_t	link;
};

static crt_iv_key_t *
alloc_key(int root, int key_id)
{
	crt_iv_key_t		*key;
	struct iv_key_struct	*key_struct;

	D_ALLOC_PTR(key);
	assert(key != NULL);

	D_ALLOC(key->iov_buf, sizeof(struct iv_key_struct));
	assert(key->iov_buf != NULL);

	key->iov_buf_len = sizeof(struct iv_key_struct);
	key->iov_len = key->iov_buf_len;

	key_struct = (struct iv_key_struct *)key->iov_buf;

	key_struct->rank = root;
	key_struct->key_id = key_id;

	return key;
}

static void
verify_key_value_pair(crt_iv_key_t *key, d_sg_list_t *value)
{
	struct iv_key_struct	*key_struct;
	struct iv_value_struct	*value_struct;

	key_struct = (struct iv_key_struct *)key->iov_buf;
	value_struct = (struct iv_value_struct *)value->sg_iovs[0].iov_buf;

	assert(key_struct->rank == value_struct->root_rank);
}

void
deinit_iv_storage(void)
{
	struct kv_pair_entry	*entry;
	struct kv_pair_entry	*temp;

	d_list_for_each_entry_safe(entry, temp, &g_kv_pair_head, link) {
		d_list_del(&entry->link);

		D_FREE(entry->value.sg_iovs[0].iov_buf);
		D_FREE(entry->value.sg_iovs);
		D_FREE(entry->key.iov_buf);
		D_FREE(entry);
	}
}

/* Generate storage for iv keys */
static void
init_iv_storage(void)
{
	int			 i;
	struct kv_pair_entry	*entry;
	struct iv_key_struct	*key_struct;
	struct iv_value_struct	*value_struct;
	crt_iv_key_t		*key;
	d_sg_list_t		*value;
	int			 size;

	/* First NUM_LOCAL_IVS are owned by the current rank */
	for (i = 0; i < NUM_LOCAL_IVS; i++) {
		D_ALLOC_PTR(entry);
		assert(entry != NULL);

		key = &entry->key;
		value = &entry->value;

		D_ALLOC(key->iov_buf, sizeof(struct iv_key_struct));
		assert(key->iov_buf != NULL);

		/* Fill in the key */
		key_struct = (struct iv_key_struct *)key->iov_buf;
		key_struct->rank = g_my_rank;
		key_struct->key_id = i;

		key->iov_len = sizeof(struct iv_key_struct);
		key->iov_buf_len = key->iov_len;

		entry->valid = true;

		/* Fill in the value */
		value->sg_nr.num = 1;
		D_ALLOC_PTR(value->sg_iovs);
		assert(value->sg_iovs != NULL);

		size = sizeof(struct iv_value_struct);
		D_ALLOC(value->sg_iovs[0].iov_buf, size);
		assert(value->sg_iovs[0].iov_buf != NULL);

		value->sg_iovs[0].iov_len = size;
		value->sg_iovs[0].iov_buf_len = size;

		assert(value->sg_iovs[0].iov_buf != NULL);

		value_struct = (struct iv_value_struct *)
			       value->sg_iovs[0].iov_buf;

		value_struct->root_rank = g_my_rank;

		sprintf(value_struct->str_data,
			"Default value for key %d:%d", g_my_rank, i);

		d_list_add_tail(&entry->link, &g_kv_pair_head);
	}

	d_list_for_each_entry(entry, &g_kv_pair_head, link) {
		key = &entry->key;
		value = &entry->value;

		verify_key_value_pair(key, value);
	}

	DBG_PRINT("Default %d keys for rank %d initialized\n",
		  NUM_LOCAL_IVS, g_my_rank);
}

static bool
keys_equal(crt_iv_key_t *key1, crt_iv_key_t *key2)
{
	struct iv_key_struct *key_struct1;
	struct iv_key_struct *key_struct2;

	key_struct1 = (struct iv_key_struct *)key1->iov_buf;
	key_struct2 = (struct iv_key_struct *)key2->iov_buf;

	if ((key_struct1->rank == key_struct2->rank) &&
	    (key_struct1->key_id == key_struct2->key_id)) {
		return true;
	}

	return false;
}

static int
copy_iv_value(d_sg_list_t *dst, d_sg_list_t *src)
{
	uint32_t i;

	assert(dst != NULL);
	assert(src != NULL);

	if (dst->sg_nr.num != src->sg_nr.num) {
		DBG_PRINT("dst = %d, src = %d\n",
			  dst->sg_nr.num, src->sg_nr.num);

		assert(dst->sg_nr.num == src->sg_nr.num);
	}

	for (i = 0; i < dst->sg_nr.num; i++) {

		assert(dst->sg_iovs[i].iov_buf != NULL);
		assert(src->sg_iovs[i].iov_buf != NULL);

		memcpy(dst->sg_iovs[i].iov_buf, src->sg_iovs[i].iov_buf,
		       src->sg_iovs[i].iov_buf_len);

		assert(dst->sg_iovs[i].iov_buf_len ==
		       src->sg_iovs[i].iov_buf_len);

		assert(dst->sg_iovs[i].iov_len ==
		       src->sg_iovs[i].iov_len);
	}

	return 0;
}

static void
verify_key(crt_iv_key_t *iv_key)
{
	assert(iv_key != NULL);
	assert(iv_key->iov_buf_len == sizeof(struct iv_key_struct));
	assert(iv_key->iov_len == sizeof(struct iv_key_struct));
	assert(iv_key->iov_buf != NULL);
}

static void
verify_value(d_sg_list_t *iv_value)
{
	size_t size;

	size = sizeof(struct iv_value_struct);

	assert(iv_value != NULL);
	assert(iv_value->sg_nr.num == 1);
	assert(iv_value->sg_iovs != NULL);
	assert(iv_value->sg_iovs[0].iov_buf_len == size);
	assert(iv_value->sg_iovs[0].iov_len == size);
	assert(iv_value->sg_iovs[0].iov_buf != NULL);
}

static int
add_new_kv_pair(crt_iv_key_t *iv_key, d_sg_list_t *iv_value,
		bool is_valid_entry)
{
	struct kv_pair_entry	*entry;
	int			 size;
	int			 i;

	/* If we are here it means we don't have this key cached yet */
	D_ALLOC_PTR(entry);
	assert(entry != NULL);

	entry->valid = is_valid_entry;

	/* Allocate space for iv key and copy it over*/
	D_ALLOC(entry->key.iov_buf, iv_key->iov_buf_len);
	assert(entry->key.iov_buf != NULL);

	memcpy(entry->key.iov_buf, iv_key->iov_buf, iv_key->iov_buf_len);

	entry->key.iov_buf_len = iv_key->iov_buf_len;
	entry->key.iov_len = iv_key->iov_len;

	/* Allocate space for iv value */
	entry->value.sg_nr.num = 1;
	D_ALLOC_PTR(entry->value.sg_iovs);
	assert(entry->value.sg_iovs != NULL);

	size = sizeof(struct iv_value_struct);

	for (i = 0; i < entry->value.sg_nr.num; i++) {
		D_ALLOC(entry->value.sg_iovs[i].iov_buf, size);
		assert(entry->value.sg_iovs[i].iov_buf != NULL);

		entry->value.sg_iovs[i].iov_buf_len = size;
		entry->value.sg_iovs[i].iov_len = size;
	}

	if (is_valid_entry)
		copy_iv_value(&entry->value, iv_value);
	else {
		iv_value->sg_nr = entry->value.sg_nr;
		iv_value->sg_iovs = entry->value.sg_iovs;
	}

	d_list_add_tail(&entry->link, &g_kv_pair_head);

	return 0;
}

static void
print_key_value(char *hdr, crt_iv_key_t *iv_key, d_sg_list_t *iv_value)
{
	struct iv_key_struct *key_struct;
	struct iv_value_struct *value_struct;

	printf("[%s:%d:SERV]\t", g_hostname, g_my_rank);

	printf("%s", hdr);

	if (iv_key == NULL) {
		printf("key=NULL");
	} else {

		key_struct = (struct iv_key_struct *)iv_key->iov_buf;
		if (key_struct == NULL)
			printf("key=EMPTY");
		else
			printf("key=[%d:%d]", key_struct->rank,
			       key_struct->key_id);
	}

	printf(" ");

	if (iv_value == NULL) {
		printf("value=NULL");
	} else {

		value_struct = (struct iv_value_struct *)
			       iv_value->sg_iovs[0].iov_buf;

		if (value_struct == NULL)
			printf("value=EMPTY");
		else
			printf("value='%s'", value_struct->str_data);
	}

	printf("\n");
}

static void
dump_all_keys(char *msg)
{
	struct kv_pair_entry *entry;

	if (g_verbose_mode < 2)
		return;

	DBG_PRINT("Dumping keys from %s\n", msg);

	LOCK_KEYS();
	d_list_for_each_entry(entry, &g_kv_pair_head, link) {
		print_key_value("Entry = ", &entry->key, &entry->value);
	}
	UNLOCK_KEYS();

	DBG_PRINT("\n\n");
}

static int
iv_on_fetch(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
	    crt_iv_ver_t *iv_ver, uint32_t flags, d_sg_list_t *iv_value,
	    void *user_priv)
{
	struct kv_pair_entry	*entry;
	struct iv_key_struct	*key_struct;
	uint32_t		 nchildren = -1;
	int			 rc;

	DBG_ENTRY();

	assert(user_priv == &g_test_user_priv);

	verify_key(iv_key);
	assert(iv_value != NULL);

	/* just to test API usage */
	rc = crt_iv_get_nchildren(ivns, 0, iv_key, &nchildren);
	if (rc == 0)
		DBG_PRINT("in IV tree, nchildren: %d.\n", nchildren);
	else
		/*
		 * Just to catch the error earlier than fetch completion
		 * callback for testing.
		 */
		D_ASSERTF(rc == 0, "crt_iv_get_nchildren failed, rc=%d.\n", rc);

	key_struct = (struct iv_key_struct *)iv_key->iov_buf;

	dump_all_keys("ON_FETCH");

	LOCK_KEYS();
	d_list_for_each_entry(entry, &g_kv_pair_head, link) {

		if (keys_equal(iv_key, &entry->key) == true) {

			if (entry->valid) {
				copy_iv_value(iv_value, &entry->value);
				print_key_value("FETCH found key ", iv_key,
						iv_value);

				UNLOCK_KEYS();
				DBG_EXIT();
				return 0;
			}

			if (key_struct->rank == g_my_rank) {
				DBG_PRINT("Was my key, but its not valid\n");
				UNLOCK_KEYS();
				DBG_EXIT();
				return -1;
			}

			DBG_PRINT("Found key, but wasn't valid, forwarding\n");
			UNLOCK_KEYS();
			DBG_EXIT();
			return -DER_IVCB_FORWARD;
		}
	}
	UNLOCK_KEYS();

	DBG_PRINT("FETCH: Key [%d:%d] not found\n",
		  key_struct->rank, key_struct->key_id);

	if (key_struct->rank == g_my_rank) {
		DBG_EXIT();
		return -1;
	}

	DBG_EXIT();
	return -DER_IVCB_FORWARD;
}

static int
iv_on_update(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
	     crt_iv_ver_t iv_ver, uint32_t flags, d_sg_list_t *iv_value,
	     void *user_priv)
{
	struct kv_pair_entry *entry;
	struct iv_key_struct *key_struct;
	int rc;

	DBG_ENTRY();

	assert(user_priv == &g_test_user_priv);
	verify_key(iv_key);
	verify_value(iv_value);

	print_key_value("UPDATE called ", iv_key, iv_value);

	dump_all_keys("ON_UPDATE");
	key_struct = (struct iv_key_struct *)iv_key->iov_buf;

	if (key_struct->rank == g_my_rank)
		rc = 0;
	else
		rc = -DER_IVCB_FORWARD;

	LOCK_KEYS();
	d_list_for_each_entry(entry, &g_kv_pair_head, link) {
		if (keys_equal(iv_key, &entry->key) == true) {
			copy_iv_value(&entry->value, iv_value);
			UNLOCK_KEYS();

			dump_all_keys("ON_UPDATE; after copy");
			DBG_EXIT();
			return rc;
		}
	}

	add_new_kv_pair(iv_key, iv_value, true);
	UNLOCK_KEYS();
	DBG_EXIT();
	return rc;
}

static int
iv_on_refresh(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
	      crt_iv_ver_t iv_ver, d_sg_list_t *iv_value, bool invalidate,
	      int refresh_rc, void *user_priv)
{
	struct kv_pair_entry	*entry = NULL;
	bool			 valid;
	struct iv_key_struct	*key_struct;
	int			 rc;

	DBG_ENTRY();
	assert(user_priv == &g_test_user_priv);
	valid = invalidate ? false : true;

	verify_key(iv_key);
	print_key_value("REFRESH called ", iv_key, iv_value);
	dump_all_keys("ON_REFRESH");

	key_struct = (struct iv_key_struct *)iv_key->iov_buf;

	if (key_struct->rank == g_my_rank)
		rc = 0;
	else
		rc = -DER_IVCB_FORWARD;


	LOCK_KEYS();
	d_list_for_each_entry(entry, &g_kv_pair_head, link) {

		if (keys_equal(iv_key, &entry->key) == true) {

			if (iv_value == NULL) {
				DBG_PRINT("Marking entry as invalid!\n");
				entry->valid = false;
			} else {
				copy_iv_value(&entry->value, iv_value);

				entry->valid = valid;
			}

			UNLOCK_KEYS();
			DBG_EXIT();
			return rc;
		}
	}

	if (iv_value != NULL)
		add_new_kv_pair(iv_key, iv_value, valid);

	UNLOCK_KEYS();

	DBG_EXIT();

	return rc;
}

static int
iv_on_hash(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key, d_rank_t *root)
{
	struct iv_key_struct *key_struct;

	DBG_ENTRY();
	verify_key(iv_key);

	dump_all_keys("ON_HASH");
	key_struct = (struct iv_key_struct *)iv_key->iov_buf;
	*root = key_struct->rank;

	DBG_EXIT();
	return 0;
}


static int
iv_on_get(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
	  crt_iv_ver_t iv_ver, crt_iv_perm_t permission,
	  d_sg_list_t *iv_value, void **user_priv)
{
	int size;

	DBG_ENTRY();
	dump_all_keys("ON_GETVALUE");

	*user_priv = &g_test_user_priv;

	size = sizeof(struct iv_value_struct);

	D_ALLOC_PTR(iv_value->sg_iovs);
	assert(iv_value->sg_iovs != NULL);

	D_ALLOC(iv_value->sg_iovs[0].iov_buf, size);
	assert(iv_value->sg_iovs[0].iov_buf != NULL);

	iv_value->sg_iovs[0].iov_len = size;
	iv_value->sg_iovs[0].iov_buf_len = size;

	iv_value->sg_nr.num = 1;

	DBG_EXIT();
	return 0;
}

static int
iv_on_put(crt_iv_namespace_t ivns, d_sg_list_t *iv_value, void *user_priv)
{
	DBG_ENTRY();

	assert(user_priv == &g_test_user_priv);

	free(iv_value->sg_iovs[0].iov_buf);
	free(iv_value->sg_iovs);

	dump_all_keys("ON_PUTVALUE");
	DBG_EXIT();

	return 0;
}

struct crt_iv_ops g_ivc_ops = {
	.ivo_on_fetch = iv_on_fetch,
	.ivo_on_update = iv_on_update,
	.ivo_on_refresh = iv_on_refresh,
	.ivo_on_hash = iv_on_hash,
	.ivo_on_get = iv_on_get,
	.ivo_on_put = iv_on_put,
};

static crt_iv_namespace_t g_ivns;

static void
init_iv(void)
{
	struct crt_iv_class	 iv_class;
	crt_endpoint_t		 server_ep = {0};
	struct rpc_set_ivns_in	*input;
	struct rpc_set_ivns_out	*output;
	int			 rc;
	uint32_t		 rank;
	crt_rpc_t		*rpc;
	int			 tree_topo;

	/*
	 * This is the IV "global" ivns handle - which is *sent* to other nodes
	 * It is not consumed on this node outside of this function
	 */
	d_iov_t			 s_ivns;

	tree_topo = crt_tree_topo(CRT_TREE_KNOMIAL, 2);

	if (g_my_rank == 0) {
		iv_class.ivc_id = 0;
		iv_class.ivc_feats = 0;
		iv_class.ivc_ops = &g_ivc_ops;

		/*
		 * Here g_ivns is the "local" handle
		 * The "global" (to all nodes) handle is s_ivns
		 */
		rc = crt_iv_namespace_create(g_main_ctx, NULL, tree_topo,
					     &iv_class, 1, &g_ivns, &s_ivns);
		assert(rc == 0);

		for (rank = 1; rank < g_group_size; rank++) {
			server_ep.ep_rank = rank;

			rc = prepare_rpc_request(g_main_ctx, RPC_SET_IVNS,
						 &server_ep, (void *)&input,
						 &rpc);
			assert(rc == 0);

			input->global_ivns_iov.iov_buf = s_ivns.iov_buf;
			input->global_ivns_iov.iov_buf_len = s_ivns.iov_buf_len;
			input->global_ivns_iov.iov_len = s_ivns.iov_len;

			rc = send_rpc_request(g_main_ctx, rpc, (void *)&output);
			assert(rc == 0);
			assert(output->rc == 0);

			rc = crt_req_decref(rpc);
			assert(rc == 0);
		}
	}
}

static void
deinit_iv(void) {
	int rc = 0;

	if (g_ivns != NULL) {
		rc = crt_iv_namespace_destroy(g_ivns);
		assert(rc == 0);
	}
}

/* handler for RPC_SET_IVNS */
int
iv_set_ivns(crt_rpc_t *rpc)
{
	struct crt_iv_class	 iv_class;
	struct rpc_set_ivns_in	*input;
	struct rpc_set_ivns_out	*output;
	int			 rc;

	DBG_ENTRY();

	input = crt_req_get(rpc);
	output = crt_reply_get(rpc);

	assert(input != NULL);
	assert(output != NULL);

	iv_class.ivc_id = 0;
	iv_class.ivc_feats = 0;
	iv_class.ivc_ops = &g_ivc_ops;

	rc = crt_iv_namespace_attach(g_main_ctx, &input->global_ivns_iov,
				     &iv_class, 1, &g_ivns);
	assert(rc == 0);

	output->rc = 0;

	rc = crt_reply_send(rpc);
	assert(rc == 0);

	DBG_EXIT();
	return 0;
}

struct fetch_done_cb_info {
	crt_iv_key_t	*key;
	crt_rpc_t	*rpc;
};

static int
fetch_done(crt_iv_namespace_t ivns, uint32_t class_id,
	   crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver, d_sg_list_t *iv_value,
	   int fetch_rc, void *cb_args)
{
	struct iv_key_struct		*key_struct;
	struct iv_value_struct		*value_struct;
	crt_iv_key_t			*expected_key;
	struct iv_key_struct		*expected_key_struct;
	struct fetch_done_cb_info	*cb_info;
	struct rpc_test_fetch_iv_out	*output;
	int				 rc;

	cb_info = (struct fetch_done_cb_info *)cb_args;
	assert(cb_info != NULL);

	output = crt_reply_get(cb_info->rpc);
	assert(output != NULL);

	if (fetch_rc != 0) {
		DBG_PRINT("----------------------------------\n");
		print_key_value("Fetch failed: ", iv_key, iv_value);
		DBG_PRINT("----------------------------------\n");

		output->rc = fetch_rc;
		rc = crt_reply_send(cb_info->rpc);
		assert(rc == 0);

		rc = crt_req_decref(cb_info->rpc);
		assert(rc == 0);

		free(cb_info);
		return 0;
	}

	expected_key = cb_info->key;
	assert(expected_key != NULL);

	expected_key_struct = (struct iv_key_struct *)iv_key->iov_buf;
	assert(expected_key_struct != NULL);

	key_struct = (struct iv_key_struct *)iv_key->iov_buf;
	value_struct = (struct iv_value_struct *)iv_value->sg_iovs[0].iov_buf;

	assert(key_struct->rank == expected_key_struct->rank);
	assert(key_struct->key_id == expected_key_struct->key_id);
	assert(value_struct->root_rank == key_struct->rank);

	DBG_PRINT("----------------------------------\n");
	print_key_value("Fetch result: ", iv_key, iv_value);
	DBG_PRINT("----------------------------------\n");

	output->rc = 0;

	rc = crt_reply_send(cb_info->rpc);
	assert(rc == 0);

	rc = crt_req_decref(cb_info->rpc);
	assert(rc == 0);

	free(cb_info);
	free(iv_key->iov_buf);
	return 0;
}

struct update_done_cb_info {
	crt_iv_key_t	*key;
	crt_rpc_t	*rpc;
};

static int
update_done(crt_iv_namespace_t ivns, uint32_t class_id,
	    crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver, d_sg_list_t *iv_value,
	    int update_rc, void *cb_args)
{
	struct update_done_cb_info	*cb_info;
	struct rpc_test_update_iv_out	*output;
	int				 rc;

	DBG_ENTRY();
	dump_all_keys("ON_UPDATE_DONE");

	cb_info = (struct update_done_cb_info *)cb_args;

	print_key_value("UPDATE_DONE called ", iv_key, iv_value);

	output = crt_reply_get(cb_info->rpc);
	output->rc = update_rc;

	rc = crt_reply_send(cb_info->rpc);
	assert(rc == 0);

	rc = crt_req_decref(cb_info->rpc);
	assert(rc == 0);

	free(cb_info);

	DBG_EXIT();
	return 0;
}

/* handler for RPC_TEST_UPDATE_IV */
int
iv_test_update_iv(crt_rpc_t *rpc)
{
	struct rpc_test_update_iv_in	*input;
	crt_iv_key_t			*key;
	struct iv_key_struct		*key_struct;
	int				 rc;
	d_sg_list_t			 iv_value;
	struct iv_value_struct		*value_struct;
	struct update_done_cb_info	*update_cb_info;
	crt_iv_sync_t			*sync;

	input = crt_req_get(rpc);
	assert(input != NULL);

	key_struct = (struct iv_key_struct *)input->iov_key.iov_buf;
	key = alloc_key(key_struct->rank, key_struct->key_id);
	assert(key != NULL);

	DBG_PRINT("Performing update for %d:%d value=%s\n",
		  key_struct->rank, key_struct->key_id, input->str_value);

	iv_value.sg_nr.num = 1;
	D_ALLOC_PTR(iv_value.sg_iovs);
	assert(iv_value.sg_iovs != NULL);

	D_ALLOC(iv_value.sg_iovs[0].iov_buf, sizeof(struct iv_value_struct));
	assert(iv_value.sg_iovs[0].iov_buf != NULL);

	iv_value.sg_iovs[0].iov_buf_len = sizeof(struct iv_value_struct);
	iv_value.sg_iovs[0].iov_len = sizeof(struct iv_value_struct);

	value_struct = (struct iv_value_struct *)iv_value.sg_iovs[0].iov_buf;
	value_struct->root_rank = key_struct->rank;

	strncpy(value_struct->str_data, input->str_value, MAX_DATA_SIZE);

	sync = (crt_iv_sync_t *)input->iov_sync.iov_buf;
	assert(sync != NULL);

	D_ALLOC_PTR(update_cb_info);
	assert(update_cb_info != NULL);

	update_cb_info->key = key;
	update_cb_info->rpc = rpc;

	rc = crt_req_addref(rpc);
	assert(rc == 0);

	rc = crt_iv_update(g_ivns, 0, key, 0, &iv_value, 0, *sync, update_done,
			   update_cb_info);

	return 0;
}

/* handler for RPC_TEST_FETCH_IV */
int
iv_test_fetch_iv(crt_rpc_t *rpc)
{
	struct rpc_test_fetch_iv_in	*input;
	crt_iv_key_t			*key;
	struct fetch_done_cb_info	*cb_info;
	int				 rc;
	struct iv_key_struct		*key_struct;

	input = crt_req_get(rpc);
	assert(input != NULL);

	key_struct = (struct iv_key_struct *)input->iov_key.iov_buf;

	key = alloc_key(key_struct->rank, key_struct->key_id);
	assert(key != NULL);

	D_ALLOC_PTR(cb_info);
	assert(cb_info != NULL);

	cb_info->key = key;
	cb_info->rpc = rpc;

	rc = crt_req_addref(rpc);
	assert(rc == 0);

	rc = crt_iv_fetch(g_ivns, 0, key, 0, 0, fetch_done, cb_info);

	return 0;
}

struct invalidate_cb_info {
	crt_iv_key_t	*expect_key;
	crt_rpc_t	*rpc;
};

static int
invalidate_done(crt_iv_namespace_t ivns, uint32_t class_id,
		crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
		d_sg_list_t *iv_value, int invalidate_rc, void *cb_args)
{
	struct invalidate_cb_info		*cb_info;
	struct rpc_test_invalidate_iv_out	*output;
	struct iv_key_struct			*key_struct;
	struct iv_key_struct			*expect_key_struct;
	int					 rc;

	DBG_ENTRY();

	cb_info = (struct invalidate_cb_info *)cb_args;
	assert(cb_info != NULL);

	output = crt_reply_get(cb_info->rpc);
	assert(output != NULL);

	key_struct = (struct iv_key_struct *)iv_key->iov_buf;
	expect_key_struct = (struct iv_key_struct *)
			    cb_info->expect_key->iov_buf;

	assert(key_struct->rank == expect_key_struct->rank);
	assert(key_struct->key_id == expect_key_struct->key_id);

	if (invalidate_rc != 0) {
		DBG_PRINT("----------------------------------\n");
		DBG_PRINT("Key = [%d,%d] Failed\n", key_struct->rank,
			  key_struct->key_id);
		DBG_PRINT("----------------------------------\n");
	} else {
		DBG_PRINT("----------------------------------\n");
		DBG_PRINT("Key = [%d,%d] PASSED\n", key_struct->rank,
			  key_struct->key_id);
		DBG_PRINT("----------------------------------\n");
	}

	output->rc = invalidate_rc;

	rc = crt_reply_send(cb_info->rpc);
	assert(rc == 0);

	rc = crt_req_decref(cb_info->rpc);
	assert(rc == 0);

	free(cb_info);
	DBG_EXIT();

	return 0;
}

int iv_test_invalidate_iv(crt_rpc_t *rpc)
{
	struct rpc_test_invalidate_iv_in	*input;
	struct iv_key_struct			*key_struct;
	crt_iv_key_t				*key;
	struct invalidate_cb_info		*cb_info;
	crt_iv_sync_t				 sync = CRT_IV_SYNC_MODE_NONE;
	int					 rc;

	input = crt_req_get(rpc);
	assert(input != NULL);

	key_struct = (struct iv_key_struct *)input->iov_key.iov_buf;

	key = alloc_key(key_struct->rank, key_struct->key_id);
	assert(key != NULL);

	rc = crt_req_addref(rpc);
	assert(rc == 0);

	D_ALLOC_PTR(cb_info);
	assert(cb_info != NULL);

	cb_info->rpc = rpc;
	cb_info->expect_key = key;

	rc = crt_iv_invalidate(g_ivns, 0, key, 0, CRT_IV_SHORTCUT_NONE,
			       sync, invalidate_done, cb_info);
	return 0;
}

static void
show_usage(char *app_name)
{
	printf("Usage: %s [options]\n", app_name);
	printf("Options are:\n");
	printf("-v <num> : verbose mode\n");
	printf("Verbose numbers are 0,1,2\n\n");
}

int main(int argc, char **argv)
{
	char	*arg_verbose = NULL;
	int	 c;
	int	 rc;

	while ((c = getopt(argc, argv, "v:")) != -1) {
		switch (c) {
		case 'v':
			arg_verbose = optarg;
			break;
		default:
			printf("Unknown option %c\n", c);
			show_usage(argv[0]);
			return -1;
		}
	}

	if (arg_verbose == NULL)
		g_verbose_mode = 0;
	else
		g_verbose_mode = atoi(arg_verbose);

	if (g_verbose_mode < 0 || g_verbose_mode > 3) {
		printf("-v verbose mode is between 0 and 3\n");
		return -1;
	}

	init_hostname(g_hostname, sizeof(g_hostname));

	rc = crt_init(NULL, CRT_FLAG_BIT_SERVER);
	assert(rc == 0);

	rc = crt_group_config_save(NULL, true);
	assert(rc == 0);

	rc = RPC_REGISTER(RPC_TEST_FETCH_IV);
	assert(rc == 0);

	rc = RPC_REGISTER(RPC_TEST_UPDATE_IV);
	assert(rc == 0);

	rc = RPC_REGISTER(RPC_TEST_INVALIDATE_IV);
	assert(rc == 0);

	rc = RPC_REGISTER(RPC_SET_IVNS);
	assert(rc == 0);

	rc = RPC_REGISTER(RPC_SHUTDOWN);
	assert(rc == 0);

	rc = crt_group_rank(NULL, &g_my_rank);
	assert(rc == 0);

	rc = crt_group_size(NULL, &g_group_size);
	assert(rc == 0);

	init_work_contexts();
	init_iv_storage();
	init_iv();

	while (!g_do_shutdown)
		sleep(1);

	shutdown();
	deinit_iv_storage();
	deinit_iv();

	rc = crt_finalize();
	assert(rc == 0);

	return 0;
}
