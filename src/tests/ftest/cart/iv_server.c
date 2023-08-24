/*
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

#include <gurt/list.h>
#include <cart/api.h>

#define _SERVER
#include "iv_common.h"

#define MY_IVNS_ID 0xABCD

static d_rank_t g_my_rank;
static uint32_t g_group_size;

static int g_verbose_mode;

static int namespace_attached;

static crt_group_t *grp;

/* See iv_client.c for definition/usage of g_timing */
static uint32_t		g_grp_version;
static int		g_timing;

static void wait_for_namespace(void)
{
	while (!namespace_attached) {
		sched_yield();

		/* namespace_attached doesn't get updated properly without
		 * this call being present
		 */
		__sync_synchronize();
	}
}

/* Verbose mode:
 * 0 - disabled
 * 1 - Entry/Exists
 * 2 - Dump keys
 **/
#undef  DBG_ENTRY
#define DBG_ENTRY()						\
do {								\
	if (g_verbose_mode >= 1) {				\
		DBG_PRINT(">>>> Entered %s\n", __func__);	\
	}							\
} while (0)

#undef  DBG_EXIT
#define DBG_EXIT()							\
do {									\
	if (g_verbose_mode >= 1) {					\
		DBG_PRINT("<<<< Exited %s:%d\n\n", __func__, __LINE__);	\
	}								\
} while (0)

struct iv_value_struct {
	char data[MAX_DATA_SIZE];
};

static crt_context_t g_main_ctx;
static pthread_t g_progress_thread;
static pthread_mutex_t g_key_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK_KEYS() D_MUTEX_LOCK(&g_key_lock)
#define UNLOCK_KEYS() D_MUTEX_UNLOCK(&g_key_lock)

/* handler for RPC_SHUTDOWN */
int
iv_shutdown(crt_rpc_t *rpc)
{
	struct RPC_SHUTDOWN_in	*input;
	struct RPC_SHUTDOWN_out	*output;
	int			 rc;

	DBG_ENTRY();

	DBG_PRINT("\n\n***************************\n");
	DBG_PRINT("Received shutdown request\n");
	DBG_PRINT("***************************\n");

	if (g_my_rank == 0) {
		rc = crt_group_config_remove(grp);
		assert(rc == 0);
	}

	input = crt_req_get(rpc);
	output = crt_reply_get(rpc);

	assert(input != NULL);
	assert(output != NULL);

	output->rc = 0;

	rc = crt_reply_send(rpc);
	assert(rc == 0);

	crtu_progress_stop();

	DBG_EXIT();
	return 0;
}

static void
init_work_contexts(void)
{
	int rc;

	rc = crt_context_create(&g_main_ctx);
	assert(rc == 0);

	rc = pthread_create(&g_progress_thread, 0,
			    crtu_progress_fn, &g_main_ctx);
	assert(rc == 0);
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

	if (dst->sg_nr != src->sg_nr) {
		DBG_PRINT("dst = %d, src = %d\n",
			  dst->sg_nr, src->sg_nr);

		assert(dst->sg_nr == src->sg_nr);
	}

	for (i = 0; i < dst->sg_nr; i++) {
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
	assert(iv_value->sg_nr == 1);
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
	uint32_t		 i;

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
	D_DEBUG(DB_TEST, "IV Variable:\n");

	/* Allocate space for iv value */
	entry->value.sg_nr = 1;
	D_ALLOC_PTR(entry->value.sg_iovs);
	assert(entry->value.sg_iovs != NULL);

	size = sizeof(struct iv_value_struct);

	for (i = 0; i < entry->value.sg_nr; i++) {
		D_ALLOC(entry->value.sg_iovs[i].iov_buf, size);
		assert(entry->value.sg_iovs[i].iov_buf != NULL);

		entry->value.sg_iovs[i].iov_buf_len = size;
		entry->value.sg_iovs[i].iov_len = size;
	}

	if (is_valid_entry) {
		copy_iv_value(&entry->value, iv_value);
	} else {
		iv_value->sg_nr = entry->value.sg_nr;
		iv_value->sg_iovs = entry->value.sg_iovs;
	}

	d_list_add_tail(&entry->link, &g_kv_pair_head);

	return 0;
}

static void
print_key_value(char *hdr, crt_iv_key_t *iv_key, d_sg_list_t *iv_value)
{
#	define MAX_BUF_SIZE 128
	struct iv_key_struct *key_struct;
	struct iv_value_struct *value_struct;

	char		buffer[MAX_BUF_SIZE];
	int		rindex = 0;
	int		rc;

	rc = snprintf(&buffer[rindex], MAX_BUF_SIZE - rindex,
		      "    %s:", hdr);
	if (rc > 0) {
		/* Avoid checkpatch warning */
		rindex += rc;
	}

	if (iv_key == NULL) {
		rc = snprintf(&buffer[rindex], MAX_BUF_SIZE - rindex, "%s",
			      "key=NULL");
		if (rc > 0) {
			/* Avoid checkpatch warning */
			rindex += rc;
		}
	} else {
		key_struct = (struct iv_key_struct *)iv_key->iov_buf;
		if (key_struct == NULL) {
			rc = snprintf(&buffer[rindex], MAX_BUF_SIZE - rindex,
				      "%s", "key=EMPTY");
			if (rc > 0) {
				/* Avoid checkpatch warning */
				rindex += rc;
			}
		} else {
			rc = snprintf(&buffer[rindex], MAX_BUF_SIZE - rindex,
				      "key=[%d:%d]", key_struct->rank,
				      key_struct->key_id);
			if (rc > 0) {
				/* Avoid checkpatch warning */
				rindex += rc;
			}
		}
	}

	rc = snprintf(&buffer[rindex], MAX_BUF_SIZE - rindex,
		      "%s", " ");
	if (rc > 0)
		rindex += rc;

	if (iv_value == NULL) {
		rc = snprintf(&buffer[rindex], MAX_BUF_SIZE - rindex,
			      "%s", "value=NULL");
		if (rc > 0) {
			/* Avoid checkpatch warning */
			rindex += rc;
		}
	} else {
		value_struct = (struct iv_value_struct *)
			       iv_value->sg_iovs[0].iov_buf;

		if (value_struct == NULL) {
			rc = snprintf(&buffer[rindex], MAX_BUF_SIZE - rindex,
				      "%s", "value=EMPTY");
			if (rc > 0) {
				/* Avoid checkpatch warning */
				rindex += rc;
			}
		} else {
			rc = snprintf(&buffer[rindex], MAX_BUF_SIZE - rindex,
				      "value='%s'", value_struct->data);
			if (rc > 0) {
				/* Avoid checkpatch warning */
				rindex += rc;
			}
		}
	}
	DBG_PRINT("%s\n", buffer);
#	undef MAX_BUF_SIZE
}

static void
dump_all_keys(char *msg)
{
	struct kv_pair_entry *entry;

	if (g_verbose_mode < 2)
		return;

	LOCK_KEYS();
	d_list_for_each_entry(entry, &g_kv_pair_head, link) {
		print_key_value(msg, &entry->key, &entry->value);
	}
	UNLOCK_KEYS();
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

/* update/add to iv scatter/gather list with new keys */
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

	/* user_priv can be NULL in invalidate case */
	if ((invalidate == false) && (iv_value != NULL))
		assert(user_priv == &g_test_user_priv);

	valid = invalidate ? false : true;

	verify_key(iv_key);
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

/* Return root owner of key */
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
	int rc;

	DBG_ENTRY();
	dump_all_keys("ON_GETVALUE");

	*user_priv = &g_test_user_priv;

	size = sizeof(struct iv_value_struct);

	/* Allocate and initialize scatter/gather list */
	if (iv_value != NULL) {
		rc = d_sgl_init(iv_value, 1);
		assert(rc == 0);

		D_ALLOC(iv_value->sg_iovs[0].iov_buf, size);
		assert(iv_value->sg_iovs[0].iov_buf != NULL);

		iv_value->sg_iovs[0].iov_len = size;
		iv_value->sg_iovs[0].iov_buf_len = size;
	}

	DBG_EXIT();
	return 0;
}

static void
iv_on_put(crt_iv_namespace_t ivns, d_sg_list_t *iv_value, void *user_priv)
{
	DBG_ENTRY();

	assert(user_priv == &g_test_user_priv);

	/* Frees the IOV buf also */
	d_sgl_fini(iv_value, true);

	dump_all_keys("ON_PUTVALUE");
	DBG_EXIT();
}

static void
iv_pre_common(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
	      crt_generic_cb_t cb_func, void *cb_arg)
{
	DBG_ENTRY();
	cb_func(cb_arg);
	DBG_EXIT();
}

static void
iv_pre_fetch(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
	     crt_generic_cb_t cb_func, void *cb_arg)
{
	DBG_ENTRY();
	/*
	 * Test break case:
	 *  Version change on server while it handles a
	 *  rpc request from another server.
	 */
	if (g_timing == 2) {
		crt_group_version_set(grp, g_grp_version);
		g_timing = 0;
	}

	cb_func(cb_arg);
	DBG_EXIT();
}

struct crt_iv_ops g_ivc_ops = {
	.ivo_pre_fetch = iv_pre_fetch,
	.ivo_on_fetch = iv_on_fetch,
	.ivo_pre_update = iv_pre_common,
	.ivo_on_update = iv_on_update,
	.ivo_pre_refresh = iv_pre_common,
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
	struct RPC_SET_IVNS_in	*input;
	struct RPC_SET_IVNS_out	*output;
	int			 rc;
	uint32_t		 rank;
	crt_rpc_t		*rpc;
	int			 tree_topo;

	tree_topo = crt_tree_topo(CRT_TREE_KNOMIAL, 2);

	if (g_my_rank == 0) {
		iv_class.ivc_id = 0;
		iv_class.ivc_feats = 0;
		iv_class.ivc_ops = &g_ivc_ops;

		rc = crt_iv_namespace_create(g_main_ctx, NULL, tree_topo,
					     &iv_class, 1,
					     MY_IVNS_ID, &g_ivns);
		assert(rc == 0);

		namespace_attached = 1;

		for (rank = 1; rank < g_group_size; rank++) {
			server_ep.ep_rank = rank;

			rc = prepare_rpc_request(g_main_ctx, RPC_SET_IVNS,
						 &server_ep, (void *)&input,
						 &rpc);
			assert(rc == 0);

			rc = send_rpc_request(g_main_ctx, rpc, (void *)&output);
			assert(rc == 0);
			assert(output->rc == 0);

			rc = crt_req_decref(rpc);
			assert(rc == 0);
		}
	}
}

static void
iv_destroy_cb(crt_iv_namespace_t ivns, void *arg)
{
	D_ASSERT(ivns != NULL);
	D_ASSERT(arg != NULL);

	D_DEBUG(DB_TRACE, "ivns %p was destroyed, arg %p\n", ivns, arg);
}

static void
deinit_iv(void) {
	int rc = 0;

	if (g_ivns != NULL) {
		rc = crt_iv_namespace_destroy(g_ivns, iv_destroy_cb, g_ivns);
		assert(rc == 0);
	}
}

/* handler for RPC_SET_IVNS */
int
iv_set_ivns(crt_rpc_t *rpc)
{
	struct crt_iv_class	 iv_class;
	struct RPC_SET_IVNS_in	*input;
	struct RPC_SET_IVNS_out	*output;
	int			 rc;

	DBG_ENTRY();

	input = crt_req_get(rpc);
	output = crt_reply_get(rpc);

	assert(input != NULL);
	assert(output != NULL);

	iv_class.ivc_id = 0;
	iv_class.ivc_feats = 0;
	iv_class.ivc_ops = &g_ivc_ops;

	/* Don't get back ivns handle as we don't need it */
	rc = crt_iv_namespace_create(g_main_ctx, NULL,
				     crt_tree_topo(CRT_TREE_KNOMIAL, 2),
				     &iv_class, 1, MY_IVNS_ID, &g_ivns);
	assert(rc == 0);

	output->rc = 0;

	rc = crt_reply_send(rpc);
	assert(rc == 0);

	namespace_attached = 1;

	DBG_EXIT();
	return 0;
}

static int fetch_bulk_put_cb(const struct crt_bulk_cb_info *cb_info)
{
	crt_rpc_t			*rpc;
	struct RPC_TEST_FETCH_IV_out	*output;
	int				 rc;

	DBG_ENTRY();
	rpc = cb_info->bci_bulk_desc->bd_rpc;
	output = crt_reply_get(rpc);
	assert(output != NULL);

	output->rc = cb_info->bci_rc;
	if (output->rc == 0)
		output->size = cb_info->bci_bulk_desc->bd_len;
	else
		output->size = 0;

	rc = crt_reply_send(rpc);
	assert(rc == 0);

	rc = crt_req_decref(rpc);
	assert(rc == 0);

	rc = crt_bulk_free(cb_info->bci_bulk_desc->bd_local_hdl);
	assert(rc == 0);

	DBG_EXIT();
	return 0;
}

static int
fetch_done(crt_iv_namespace_t ivns, uint32_t class_id,
	   crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver, d_sg_list_t *iv_value,
	   int fetch_rc, void *cb_args)
{
	crt_rpc_t			*rpc;
	struct RPC_TEST_FETCH_IV_in	*input;
	struct RPC_TEST_FETCH_IV_out	*output;
	crt_bulk_perm_t			 perms = CRT_BULK_RO;
	crt_bulk_t			 bulk_hdl = NULL;
	struct crt_bulk_desc		 bulk_desc = {0};
	struct kv_pair_entry		*entry;
	bool				 found = false;
	int				 rc;

	DBG_ENTRY();

	rpc = (crt_rpc_t *)cb_args;
	assert(rpc != NULL);

	output = crt_reply_get(rpc);
	assert(output != NULL);

	/* When this RPC eventually gets sent back, include the returned key */
	assert(iv_key->iov_buf != NULL);
	output->key.iov_buf = iv_key->iov_buf;
	output->key.iov_buf_len = iv_key->iov_buf_len;
	output->key.iov_len = iv_key->iov_len;

	input = crt_req_get(rpc);
	assert(input != NULL);

	/* If the IV fetch call itself failed, return the error */
	if (fetch_rc != 0) {
		output->rc = fetch_rc;
		output->size = 0;
		memset(&output->key, 0, sizeof(output->key));
		goto fail_reply;
	}

	/* TODO: fetch test only supports one sglist buffer! */
	assert(iv_value->sg_nr == 1);
	assert(iv_value->sg_iovs[0].iov_buf != NULL);

	bulk_hdl = NULL;
	LOCK_KEYS();
	d_list_for_each_entry(entry, &g_kv_pair_head, link) {
		if (keys_equal(iv_key, &entry->key) == true) {
			rc = crt_bulk_create(g_main_ctx, &entry->value,
					     perms, &bulk_hdl);
			assert(rc == 0);
			found = true;
			break;
		}
	}
	UNLOCK_KEYS();

	D_ASSERT(bulk_hdl != NULL);
	D_ASSERT(found == true);

	/*
	 * Transfer the IV payload back to the client.
	 * Rely on bulk API to return an error if it can't make the transfer
	 */
	bulk_desc.bd_rpc = rpc;
	bulk_desc.bd_bulk_op = CRT_BULK_PUT;
	bulk_desc.bd_remote_hdl = input->bulk_hdl;
	assert(bulk_desc.bd_remote_hdl != NULL);
	bulk_desc.bd_remote_off = 0;
	bulk_desc.bd_local_hdl = bulk_hdl;
	bulk_desc.bd_local_off = 0;
	bulk_desc.bd_len = MAX_DATA_SIZE;

	/* Transfer the result of the fetch to the client */
	rc = crt_bulk_transfer(&bulk_desc, fetch_bulk_put_cb, 0, 0);
	if (rc != 0) {
		DBG_PRINT("Bulk transfer of fetch result failed! rc=%d\n", rc);

		output->rc = rc;
		output->size = 0;
		memset(&output->key, 0, sizeof(output->key));
		goto fail_reply;
	}

	DBG_EXIT();
	return 0;

	/* If something goes wrong, still send the error back to the client */
fail_reply:
	if (bulk_hdl != NULL)
		crt_bulk_free(bulk_hdl);

	rc = crt_reply_send(rpc);
	assert(rc == 0);

	rc = crt_req_decref(rpc);
	assert(rc == 0);

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
	struct RPC_TEST_UPDATE_IV_out	*output;
	int				 rc;

	DBG_ENTRY();
	dump_all_keys("ON_UPDATE_DONE");

	cb_info = (struct update_done_cb_info *)cb_args;

	print_key_value("UPDATE_DONE called", iv_key, iv_value);

	output = crt_reply_get(cb_info->rpc);
	output->rc = update_rc;

	D_DEBUG(DB_TRACE, "Respond/Send to change in IV\n");
	rc = crt_reply_send(cb_info->rpc);
	assert(rc == 0);

	rc = crt_req_decref(cb_info->rpc);
	assert(rc == 0);

	D_FREE(cb_info);

	D_FREE(iv_value->sg_iovs[0].iov_buf);
	D_FREE(iv_value->sg_iovs);

	D_FREE(iv_key->iov_buf);
	DBG_EXIT();
	return 0;
}

/* handler for RPC_TEST_UPDATE_IV */
/* Place the IV value "iv_value" into list for "key" */
int
iv_test_update_iv(crt_rpc_t *rpc)
{
	struct RPC_TEST_UPDATE_IV_in	*input;
	crt_iv_key_t			*key;
	struct iv_key_struct		*key_struct;
	int				 rc;
	d_sg_list_t			 iv_value;
	struct iv_value_struct		*value_struct;
	struct update_done_cb_info	*update_cb_info;
	crt_iv_sync_t			*sync;

	DBG_ENTRY();

	wait_for_namespace();

	input = crt_req_get(rpc);
	assert(input != NULL);

	key_struct = (struct iv_key_struct *)input->iov_key.iov_buf;
	key = alloc_key(key_struct->rank, key_struct->key_id);
	assert(key != NULL);

	DBG_PRINT("Performing update for %d:%d value='%s'\n",
		  key_struct->rank, key_struct->key_id,
		  (char *)input->iov_value.iov_buf);

	rc = d_sgl_init(&iv_value, 1);
	assert(rc == 0);

	D_ALLOC(iv_value.sg_iovs[0].iov_buf, sizeof(struct iv_value_struct));
	assert(iv_value.sg_iovs[0].iov_buf != NULL);

	iv_value.sg_iovs[0].iov_buf_len = sizeof(struct iv_value_struct);
	iv_value.sg_iovs[0].iov_len = sizeof(struct iv_value_struct);

	value_struct = (struct iv_value_struct *)iv_value.sg_iovs[0].iov_buf;

	memcpy(value_struct->data, input->iov_value.iov_buf,
	       input->iov_value.iov_buf_len > MAX_DATA_SIZE ?
			MAX_DATA_SIZE : input->iov_value.iov_buf_len);

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

	D_FREE(key);
	DBG_EXIT();
	return 0;
}

/* handler for RPC_SET_GRP_VERSION */
int
iv_set_grp_version(crt_rpc_t *rpc)
{
	struct RPC_SET_GRP_VERSION_in	*input;
	struct RPC_SET_GRP_VERSION_out	*output;
	int				 rc = 0;

	DBG_ENTRY();

	input = crt_req_get(rpc);
	assert(input != NULL);
	output = crt_reply_get(rpc);
	assert(output != NULL);

	g_grp_version = input->version;
	g_timing = input->timing;
	D_DEBUG(DB_TEST, "  set_grp_version: to 0x%0x: %d\n",
		g_grp_version, g_grp_version);

	/* implement code here */
	if (g_timing == 0) {
		/* Set grpup version. Avoid checpatch warning */
		crt_group_version_set(grp, g_grp_version);
	}

	/* set output results */
	output->rc = rc;

	rc = crt_reply_send(rpc);
	assert(rc == 0);

	DBG_EXIT();
	return 0;
}

/* handler for RPC_GET_GRP_VERSION */
int
iv_get_grp_version(crt_rpc_t *rpc)
{
	struct RPC_GET_GRP_VERSION_in	*input;
	struct RPC_GET_GRP_VERSION_out	*output;
	uint32_t			 version = 0;
	int				 rc = 0;

	DBG_ENTRY();

	input = crt_req_get(rpc);
	assert(input != NULL);
	output = crt_reply_get(rpc);
	assert(output != NULL);

	/* implement code here */
	rc = crt_group_version(grp, &version);

	/* result of test output */
	D_DEBUG(DB_TEST, " grp version: 0x%08x : %d::  rc %d:\n",
		version, version, rc);

	/* Set output results */
	output->version = version;
	output->rc = rc;

	rc = crt_reply_send(rpc);
	assert(rc == 0);
	DBG_EXIT();
	return 0;
}

/* handler for RPC_TEST_FETCH_IV */
int
iv_test_fetch_iv(crt_rpc_t *rpc)
{
	struct RPC_TEST_FETCH_IV_in	*input;
	int				 rc;

	DBG_ENTRY();
	wait_for_namespace();

	input = crt_req_get(rpc);
	assert(input != NULL);

	rc = crt_req_addref(rpc);
	assert(rc == 0);

	rc = crt_iv_fetch(g_ivns, 0, &input->key, 0, 0, fetch_done, rpc);

	/*
	 * Test break case:
	 * Version change while valid request is in flight
	 */
	if (g_timing == 1) {
		crt_group_version_set(grp, g_grp_version);
		g_timing = 0;
	}

	DBG_EXIT();
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
	struct RPC_TEST_INVALIDATE_IV_out	*output;
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
		DBG_PRINT("Invalidate: Key = [%d,%d] Failed\n",
			  key_struct->rank, key_struct->key_id);
	} else {
		DBG_PRINT("Invalidate: Key = [%d,%d] PASSED\n",
			  key_struct->rank, key_struct->key_id);
	}

	output->rc = invalidate_rc;

	rc = crt_reply_send(cb_info->rpc);
	assert(rc == 0);

	rc = crt_req_decref(cb_info->rpc);
	assert(rc == 0);

	D_FREE(cb_info->expect_key->iov_buf);
	D_FREE(cb_info->expect_key);
	D_FREE(cb_info);
	DBG_EXIT();

	return 0;
}

int iv_test_invalidate_iv(crt_rpc_t *rpc)
{
	struct RPC_TEST_INVALIDATE_IV_in	*input;
	struct iv_key_struct			*key_struct;
	crt_iv_key_t				*key;
	struct invalidate_cb_info		*cb_info;
	crt_iv_sync_t				 dsync = CRT_IV_SYNC_MODE_NONE;
	crt_iv_sync_t				*sync = &dsync;
	int					 rc;

	DBG_ENTRY();

	wait_for_namespace();
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

	if (input->iov_sync.iov_buf != NULL)
		sync = (crt_iv_sync_t *)input->iov_sync.iov_buf;

	rc = crt_iv_invalidate(g_ivns, 0, key, 0, CRT_IV_SHORTCUT_NONE,
			       *sync, invalidate_done, cb_info);
	DBG_EXIT();
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
	char		*arg_verbose = NULL;
	char		*env_self_rank;
	char		*grp_cfg_file;
	d_rank_list_t	*rank_list;
	d_rank_t	my_rank;
	int		c;
	int		rc;
	uint32_t	version;

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

	env_self_rank = getenv("CRT_L_RANK");
	if (env_self_rank == NULL) {
		printf("CRT_L_RANK was not set\n");
		return -1;
	}

	my_rank = atoi(env_self_rank);

	/* rank, num_attach_retries, is_server, assert_on_error */
	crtu_test_init(my_rank, 20, true, true);

	rc = crt_init(IV_GRP_NAME, CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	assert(rc == 0);

	rc = crt_rank_self_set(my_rank, 1 /* group_version_min */);
	assert(rc == 0);

	grp = crt_group_lookup(IV_GRP_NAME);
	assert(grp != NULL);

	crt_group_version(grp, &version);

	if (grp == NULL) {
		D_ERROR("Failed to lookup group %s\n", IV_GRP_NAME);
		assert(0);
	}

	rc = crt_proto_register(&my_proto_fmt_iv);
	assert(rc == 0);

	init_work_contexts();

	/* Load the group configuration file */
	grp_cfg_file = getenv("CRT_L_GRP_CFG");
	if (grp_cfg_file == NULL) {
		D_ERROR("CRT_L_GRP_CFG was not set\n");
		assert(0);
	} else {
		D_DEBUG(DB_TEST, "Group Config File: %s\n", grp_cfg_file);
	}

	rc = crtu_load_group_from_file(grp_cfg_file, g_main_ctx, grp, my_rank,
				     true);
	if (rc != 0) {
		D_ERROR("Failed to load group file %s\n", grp_cfg_file);
		assert(0);
	}

	/* Start the server for myself */
	DBG_PRINT("Server starting, self_rank=%d\n", my_rank);

	rc = crt_group_rank(NULL, &g_my_rank);
	assert(rc == 0);

	rc = crt_group_size(NULL, &g_group_size);
	assert(rc == 0);
	D_DEBUG(DB_TEST, "My_rank %d: grp size %d\n",
		g_my_rank, g_group_size);

	rc = crt_group_ranks_get(grp, &rank_list);
	assert(rc == 0);

	rc = crtu_wait_for_ranks(g_main_ctx, grp, rank_list, 0, 1, 60, 120);
	assert(rc == 0);

	d_rank_list_free(rank_list);

	init_iv();

	/* Wait for IV namespace attach before saving group config
	 * This prevents singleton iv_client from connecting to servers
	 * before those are fully initialized
	 */
	wait_for_namespace();

	if (g_my_rank == 0) {
		rc = crt_group_config_save(grp, true);
		D_ASSERTF(rc == 0, "crt_group_config_save failed %d\n", rc);
	}

	pthread_join(g_progress_thread, NULL);
	DBG_PRINT("Finished joining progress thread\n");

	deinit_iv_storage();
	deinit_iv();

	rc = crt_finalize();
	assert(rc == 0);

	return 0;
}
