/*
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(st)

#include <pthread.h>
#include "crt_internal.h"

#define ISBULK(type) ((type) == CRT_SELF_TEST_MSG_TYPE_BULK_GET || \
		      (type) == CRT_SELF_TEST_MSG_TYPE_BULK_PUT)

/* Very simple buffer entries that can be formed into a stack or list */
struct st_buf_entry {
	struct st_buf_entry	*next;

	/* Session this buffer entry belongs to */
	struct st_session	*session;

	/* Payload */
	char			*buf;

	/* Length of the buf array */
	size_t			 buf_len;

	/* Local bulk handle for this buf - only valid if session uses bulk */
	crt_bulk_t		 bulk_hdl;

	/* Scatter-gather list with one iov buffer */
	d_sg_list_t		 sg_list;
	d_iov_t			 sg_iov;
};

struct st_session {
	/** Session ID. Note that session ID's must be unique */
	int64_t session_id;

	/** Reference count, one session be destroyed when dropped to 0 */
	int64_t session_refcnt;

	/** Parameters for the session (send size, reply size, etc.) */
	struct crt_st_session_params	 params;

	/** List of free buffers associated with this session */
	struct st_buf_entry		*buf_list;

	/** Lock to protect the list head pointer */
	pthread_spinlock_t		 buf_list_lock;

	/** Pointer to the next session in the session list */
	struct st_session		*next;
};

/**
 * List of all open sessions. If a session is on this list, it is ready to be
 * used. Protected by g_all_session_lock - test messages lock via read,
 * open/close lock via write
 *
 * New sessions are added at the front of the list
 */
static struct st_session *g_session_list;

/**
 * Read-write lock that is write-locked by open and close, and
 * read locked by test messages
 *
 * Controls modification of the overall list of sessions
 */
static pthread_rwlock_t g_all_session_lock;

static int64_t g_last_session_id;

/**
 * Finds a session in the g_session_list based on its session_id.
 *
 * Any caller of this function should be holding the g_all_session_lock in at
 * least read mode
 *
 * If requested by the caller, a reference to the pointer that points to the
 * session will be stored at the pointer prev_ptr. This makes it easy for
 * close_session() to unlink the returned session from the list
 *
 * Note that prev_ptr is NOT a pointer to a st_session - it is a pointer to
 * either the head of the list (g_session_list) or a pointer to the .next
 * element of the preceding st_session in the list
 */
static struct st_session *find_session(int64_t session_id,
				       struct st_session ***prev_ptr)
{
	struct st_session **prev = &g_session_list;

	while (*prev != NULL) {
		if ((*prev)->session_id == session_id) {
			if (prev_ptr != NULL)
				*prev_ptr = prev;
			return *prev;
		}
		prev = &(*prev)->next;
	}

	return NULL;
}

/*
 * Frees the session pointed to by the provided pointer, and sets the caller's
 * pointer to NULL
 */
static void free_session(struct st_session **session)
{
	struct st_buf_entry *free_entry;

	D_ASSERT(session != NULL);
	if (*session == NULL)
		return;

	free_entry = (*session)->buf_list;
	while (free_entry != NULL) {
		(*session)->buf_list = free_entry->next;

		D_FREE(free_entry->buf);
		if (free_entry->bulk_hdl != CRT_BULK_NULL)
			crt_bulk_free(free_entry->bulk_hdl);
		D_FREE(free_entry);

		free_entry = (*session)->buf_list;
	}

	D_SPIN_DESTROY(&(*session)->buf_list_lock);

	D_FREE(*session);
}

static inline void
addref_session(struct st_session *session)
{
	D_SPIN_LOCK(&session->buf_list_lock);
	session->session_refcnt++;
	D_SPIN_UNLOCK(&session->buf_list_lock);
}

static inline void
decref_session(struct st_session *session)
{
	bool destroy = false;

	D_SPIN_LOCK(&session->buf_list_lock);
	session->session_refcnt--;
	if (session->session_refcnt == 0)
		destroy = true;
	D_SPIN_UNLOCK(&session->buf_list_lock);

	if (destroy)
		free_session(&session);
}

static int alloc_buf_entry(struct st_buf_entry **const return_entry,
		    const struct st_session *session,
		    crt_context_t crt_ctx)
{
	size_t			 alloc_buf_len;
	size_t			 test_buf_len;
	struct st_buf_entry	*new_entry;
	int			 ret;

	D_ASSERT(return_entry != NULL);
	/* No returned buffer yet */
	*return_entry = NULL;

	/*
	 * Compute the amount of spaced needed for this test run
	 * Note that if bulk is used for sending, need to make sure this is
	 * big enough to receive the message AND send the response
	 */
	if (ISBULK(session->params.send_type))
		test_buf_len = max(session->params.send_size,
				   session->params.reply_size);
	else
		test_buf_len = session->params.reply_size;

	/*
	 * If the user requested that messages be aligned, add additional
	 * space so that a requested aligned value will always be present
	 *
	 * Note that CRT_ST_BUF_ALIGN_MAX is required to be one less than a
	 * power of two
	 */
	if (session->params.buf_alignment != CRT_ST_BUF_ALIGN_DEFAULT)
		alloc_buf_len = test_buf_len + CRT_ST_BUF_ALIGN_MAX;
	else
		alloc_buf_len = test_buf_len;

	/* If no buffer is required, don't bother to allocate any */
	if (test_buf_len == 0)
		return 0;

	D_ASSERT(alloc_buf_len > 0);

	D_ALLOC_PTR(new_entry);
	if (new_entry == NULL)
		return -DER_NOMEM;

	D_ALLOC(new_entry->buf, alloc_buf_len);
	if (new_entry->buf == NULL) {
		D_FREE(new_entry);
		return -DER_NOMEM;
	}

	/* Fill the buffer with an arbitrary data pattern */
	memset(new_entry->buf, 0xA7, alloc_buf_len);

	/* Track how big the buffer is for bookkeeping */
	new_entry->buf_len = alloc_buf_len;

	/*
	 * Set up the scatter-gather list to point to the newly
	 * allocated buffer it is attached to
	 *
	 * Note that here the length is the length of the actual
	 * buffer; this will probably need to be changed when it
	 * comes time to actually do a bulk transfer
	 */
	new_entry->sg_list.sg_iovs = &new_entry->sg_iov;
	new_entry->sg_list.sg_nr = 1;
	d_iov_set(&new_entry->sg_iov,
		    crt_st_get_aligned_ptr(new_entry->buf,
					   session->params.buf_alignment),
		    test_buf_len);

	/* If this session will use bulk, initialize a bulk descriptor */
	if (ISBULK(session->params.send_type)
	    || ISBULK(session->params.reply_type)) {
		crt_bulk_perm_t perms = ISBULK(session->params.send_type) ?
					CRT_BULK_RW : CRT_BULK_RO;

		ret = crt_bulk_create(crt_ctx, &new_entry->sg_list,
				      perms, &new_entry->bulk_hdl);
		if (ret != 0) {
			D_ERROR("crt_bulk_create failed; ret=%d\n", ret);
			D_FREE(new_entry->buf);
			D_FREE(new_entry);
			return ret;
		}
		D_ASSERT(new_entry->bulk_hdl != CRT_BULK_NULL);
	}

	/* Buffer entry contains a pointer to its session */
	new_entry->session = (struct st_session *)session;

	*return_entry = new_entry;
	return 0;
}

void crt_self_test_service_init(void)
{
	D_RWLOCK_INIT(&g_all_session_lock, NULL);
}

void crt_self_test_service_fini(void)
{
	D_RWLOCK_DESTROY(&g_all_session_lock);
}

void crt_self_test_init(void)
{
	crt_self_test_service_init();
	crt_self_test_client_init();
}

void crt_self_test_fini(void)
{
	crt_self_test_service_fini();
	crt_self_test_client_fini();
}

void
crt_self_test_open_session_handler(crt_rpc_t *rpc_req)
{
	struct crt_st_session_params	*args;
	struct st_session		*new_session = NULL;
	int64_t				*reply_session_id;
	int64_t				 session_id;
	uint32_t			 i;
	int				 ret;

	/* Get pointers to the arguments and response buffers */
	args = crt_req_get(rpc_req);
	D_ASSERT(args != NULL);

	reply_session_id = crt_reply_get(rpc_req);
	D_ASSERT(reply_session_id != NULL);

	/* Validate session parameters */
	if (args->send_type == CRT_SELF_TEST_MSG_TYPE_BULK_PUT ||
	    args->reply_type == CRT_SELF_TEST_MSG_TYPE_BULK_GET) {
		D_ERROR("Sending BULK_PUT and/or replying with BULK_GET"
			" are not supported\n");
		D_GOTO(send_rpc, *reply_session_id = -1);
	}

	/* Allocate a structure for the new session */
	D_ALLOC_PTR(new_session);
	if (new_session == NULL)
		D_GOTO(send_rpc, *reply_session_id = -1);

	/* Initialize the new session */
	ret = D_SPIN_INIT(&new_session->buf_list_lock,
				PTHREAD_PROCESS_PRIVATE);
	D_ASSERT(ret == 0);

	/* Copy the session parameters */
	memcpy(&new_session->params, args, sizeof(new_session->params));

	/*
	 * Allocate as many descriptors (with accompanying buffers) as
	 * requested by the caller
	 */
	for (i = 0; i < new_session->params.num_buffers; i++) {
		struct st_buf_entry *new_entry;

		/* Allocate the new entry (and bulk handle if applicable) */
		ret = alloc_buf_entry(&new_entry, new_session, rpc_req->cr_ctx);
		if (ret != 0) {
			D_ERROR("Failed to allocate buf_entry; ret=%d\n", ret);
			D_GOTO(send_rpc, *reply_session_id = -1);
		}

		/* No error code and no buffer allocated means none needed */
		if (new_entry == NULL) {
			new_session->params.num_buffers = 0;
			break;
		}

		/* Push this new entry onto the head of the stack */
		new_entry->next = new_session->buf_list;
		new_session->buf_list = new_entry;
	}

	/******************** LOCK: g_all_session_lock (w) ********************/
	D_RWLOCK_WRLOCK(&g_all_session_lock);

	/*
	 * Check session_id's for availability starting with one more than the
	 * most recent session_id issued. This rolls around to zero when
	 * reaching INT_MAX so that every possible session_id is tried before
	 * giving up.
	 *
	 * This means that until INT64_MAX session IDs are issued, only one
	 * search through the list has to be performed to open a new session
	 */
	session_id = g_last_session_id + 1;
	while (session_id != g_last_session_id) {
		struct st_session *found_session;

		found_session = find_session(session_id, NULL);
		if (found_session == NULL)
			/* No existing session - use this session_id */
			break;

		if (session_id == INT64_MAX)
			session_id = 0;
		else
			session_id++;
	}

	if (session_id == g_last_session_id) {
		D_ERROR("self-test: No test sessions available to reserve\n");
		*reply_session_id = -1;
	} else {
		/* Success - found an unused session ID */
		new_session->session_id = session_id;
		g_last_session_id = session_id;
		*reply_session_id = session_id;

		/* Add the new session to the list of open sessions */
		new_session->next = g_session_list;
		/* decref in crt_self_test_close_session_handler */
		addref_session(new_session);
		g_session_list = new_session;
	}

	D_RWLOCK_UNLOCK(&g_all_session_lock);
	/******************* UNLOCK: g_all_session_lock *******************/

send_rpc:
	/* Release any allocated memory if returning an invalid session ID */
	if (*reply_session_id < 0 && new_session != NULL)
		free_session(&new_session);

	ret = crt_reply_send(rpc_req);
	if (ret != 0)
		D_ERROR("self-test: crt_reply_send failed; ret = %d\n", ret);
}

void
crt_self_test_close_session_handler(crt_rpc_t *rpc_req)
{
	int64_t			*args;
	struct st_session	*del_session;
	struct st_session	**prev;
	int64_t			 session_id;
	int			 ret;

	args = crt_req_get(rpc_req);
	D_ASSERT(args != NULL);
	session_id = *args;

	/******************** LOCK: g_all_session_lock (w) ********************/
	D_RWLOCK_WRLOCK(&g_all_session_lock);

	/* Find the session if it exists */
	del_session = find_session(session_id, &prev);
	if (del_session == NULL) {
		D_ERROR("Self-test session %ld not found\n", session_id);
		goto send_rpc;
	}

	/* Remove the session from the list of active sessions */
	*prev = del_session->next;

	D_RWLOCK_UNLOCK(&g_all_session_lock);
	/******************* UNLOCK: g_all_session_lock *******************/

	/* addref in crt_self_test_open_session_handler */
	decref_session(del_session);

send_rpc:

	ret = crt_reply_send(rpc_req);
	if (ret != 0)
		D_ERROR("self-test: crt_reply_send failed; ret = %d\n", ret);
}

void crt_self_test_msg_send_reply(crt_rpc_t *rpc_req,
				  struct st_buf_entry *buf_entry,
				  int do_decref)
{
	d_iov_t				*res;
	int				 ret;
	struct st_session		*session = NULL;
	struct crt_st_session_params	*params = NULL;

	/* Grab some shorter aliases */
	if (buf_entry != NULL) {
		session = buf_entry->session;
		D_ASSERT(session != NULL);
		params = &session->params;
	}

	if (buf_entry != NULL &&
	    params->reply_type == CRT_SELF_TEST_MSG_TYPE_IOV) {
		/* Get the IOV reply handle */
		res = crt_reply_get(rpc_req);
		D_ASSERT(res != NULL);

		/* Set the reply buffer */
		d_iov_set(res,
			 crt_st_get_aligned_ptr(buf_entry->buf,
						params->buf_alignment),
			 params->reply_size);
	}

	ret = crt_reply_send(rpc_req);
	if (ret != 0)
		D_ERROR("self-test: crt_reply_send failed; ret = %d\n", ret);

	/*
	 * If a buffer was pulled off the stack, re-add it now that it has
	 * served its purpose
	 */
	if (buf_entry != NULL) {
		/************* LOCK: session->buf_list_lock *************/
		D_SPIN_LOCK(&session->buf_list_lock);

		buf_entry->next = session->buf_list;
		session->buf_list = buf_entry;

		D_SPIN_UNLOCK(&session->buf_list_lock);
		/************ UNLOCK: session->buf_list_lock ************/
	}

	if (do_decref && session) {
		/* addref in crt_self_test_msg_handler */
		decref_session(session);
	}

	/*
	 * Decrement the reference counter. This is where cleanup for the RPC
	 * always happens.
	 */
	crt_req_decref(rpc_req);
}

int crt_self_test_msg_bulk_put_cb(const struct crt_bulk_cb_info *cb_info)
{
	struct st_buf_entry	*buf_entry;

	D_ASSERT(cb_info);
	D_ASSERT(cb_info->bci_arg);
	D_ASSERT(cb_info->bci_bulk_desc);
	D_ASSERT(cb_info->bci_bulk_desc->bd_rpc);

	buf_entry = cb_info->bci_arg;

	/* Check for errors and proceed regardless */
	if (cb_info->bci_rc != 0)
		D_ERROR("BULK_GET failed; bci_rc=%d\n", cb_info->bci_rc);

	crt_self_test_msg_send_reply(cb_info->bci_bulk_desc->bd_rpc,
				     buf_entry, 1);

	return 0;
}

int crt_self_test_msg_bulk_get_cb(const struct crt_bulk_cb_info *cb_info)
{
	struct crt_bulk_desc	 bulk_desc_out;
	struct crt_bulk_desc	*bulk_desc_in;
	struct st_buf_entry	*buf_entry;
	int			 ret;

	D_ASSERT(cb_info);
	D_ASSERT(cb_info->bci_arg);
	D_ASSERT(cb_info->bci_bulk_desc);
	D_ASSERT(cb_info->bci_bulk_desc->bd_rpc);

	/* Check for errors and proceed regardless */
	if (cb_info->bci_rc != 0)
		D_ERROR("BULK_GET failed; bci_rc=%d\n", cb_info->bci_rc);

	buf_entry = cb_info->bci_arg;
	bulk_desc_in = cb_info->bci_bulk_desc;

	if (buf_entry->session->params.reply_type ==
			CRT_SELF_TEST_MSG_TYPE_BULK_PUT) {
		bulk_desc_out.bd_rpc = bulk_desc_in->bd_rpc;
		bulk_desc_out.bd_bulk_op = CRT_BULK_PUT;
		bulk_desc_out.bd_remote_hdl = bulk_desc_in->bd_remote_hdl;
		bulk_desc_out.bd_remote_off = 0;
		bulk_desc_out.bd_local_hdl = bulk_desc_in->bd_local_hdl;
		bulk_desc_out.bd_local_off = 0;
		bulk_desc_out.bd_len = buf_entry->session->params.reply_size;

		ret = crt_bulk_transfer(&bulk_desc_out,
					crt_self_test_msg_bulk_put_cb,
					buf_entry, NULL);
		if (ret != 0) {
			D_ERROR("self-test service BULK_GET failed; ret=%d\n",
				ret);
			crt_self_test_msg_send_reply(bulk_desc_in->bd_rpc,
						     NULL, 1);
		}
	} else {
		crt_self_test_msg_send_reply(cb_info->bci_bulk_desc->bd_rpc,
					     buf_entry, 1);
	}

	return 0;
}

void
crt_self_test_msg_handler(crt_rpc_t *rpc_req)
{
	void			*args;
	struct st_buf_entry	*buf_entry = NULL;
	struct st_session	*session;
	int64_t			 session_id;
	int			 ret;

	D_ASSERT(rpc_req->cr_opc == CRT_OPC_SELF_TEST_BOTH_EMPTY ||
		 rpc_req->cr_opc == CRT_OPC_SELF_TEST_SEND_ID_REPLY_IOV ||
		 rpc_req->cr_opc == CRT_OPC_SELF_TEST_SEND_IOV_REPLY_EMPTY ||
		 rpc_req->cr_opc == CRT_OPC_SELF_TEST_BOTH_IOV ||
		 rpc_req->cr_opc == CRT_OPC_SELF_TEST_SEND_BULK_REPLY_IOV ||
		 rpc_req->cr_opc == CRT_OPC_SELF_TEST_SEND_IOV_REPLY_BULK ||
		 rpc_req->cr_opc == CRT_OPC_SELF_TEST_BOTH_BULK);

	/*
	 * Increment the reference counter for this RPC
	 * It is decremented by crt_self_test_msg_send_reply
	 */
	crt_req_addref(rpc_req);

	/*
	 * For messages that do not use bulk and have no reply data, skip
	 * directly to sending the reply
	 */
	if (rpc_req->cr_opc == CRT_OPC_SELF_TEST_BOTH_EMPTY ||
	    rpc_req->cr_opc == CRT_OPC_SELF_TEST_SEND_IOV_REPLY_EMPTY) {
		crt_self_test_msg_send_reply(rpc_req, NULL, 0);
		return;
	}

	/*
	 * Past this point messages require a dedicated buffer on this end
	 * (either to use for IOV out, bulk transfer in, or bulk transfer out)
	 */

	/* Get input RPC buffer */
	args = crt_req_get(rpc_req);
	D_ASSERT(args != NULL);

	/* Retrieve the session ID from the beginning of the arguments */
	session_id = *((int64_t *)args);

	/******************** LOCK: g_all_session_lock (r) ********************/
	D_RWLOCK_RDLOCK(&g_all_session_lock);

	session = find_session(session_id, NULL);
	if (session == NULL) {
		D_ERROR("Unable to locate session_id %ld\n", session_id);
		crt_self_test_msg_send_reply(rpc_req, NULL, 1);
		return;
	}

	/* decref in crt_self_test_msg_send_reply */
	addref_session(session);

	D_RWLOCK_UNLOCK(&g_all_session_lock);

	/* Now that we have the session, do a little more validation */
	if (session->params.send_type == CRT_SELF_TEST_MSG_TYPE_BULK_PUT ||
	    session->params.reply_type == CRT_SELF_TEST_MSG_TYPE_BULK_GET) {
		D_ERROR("Only bulk send/GET reply/PUT are supported\n");
		crt_self_test_msg_send_reply(rpc_req, NULL, 1);
		return;
	}
	if (rpc_req->cr_opc !=
		crt_st_compute_opcode(session->params.send_type,
				      session->params.reply_type)) {
		D_ERROR("Opcode / self-test session params mismatch\n");
		crt_self_test_msg_send_reply(rpc_req, NULL, 1);
		return;
	}

	/* Retrieve the next available buffer from the stack for this session */
	while (buf_entry == NULL) {
		/************* LOCK: session->buf_list_lock *************/
		D_SPIN_LOCK(&session->buf_list_lock);

		/* Retrieve a send buffer from the top of the stack */
		buf_entry = session->buf_list;
		if (buf_entry != NULL)
			session->buf_list = buf_entry->next;

		D_SPIN_UNLOCK(&session->buf_list_lock);
		/************ UNLOCK: session->buf_list_lock ************/

		/* No buffers available currently, need to wait */
		if (buf_entry == NULL) {
			D_WARN("No self-test buffers available for session %ld,"
			       " num allocated = %d."
			       " This will decrease performance.\n",
			       session_id, session->params.num_buffers);

			/*
			 * IMPORTANT NOTE
			 *
			 * This is only likely to happen when there is only a
			 * single thread calling crt_progress, and it is
			 * heavily loaded. In this situation, the application
			 * is likely to deadlock here without the following
			 * code because no other threads will call crt_progress
			 * to potentially free up a buffer to use. Worse, this
			 * function can't abort without losing this test
			 * message for no good reason
			 *
			 * Instead of deadlocking or dropping a test message,
			 * the following code allocates a new buffer to use
			 *
			 * This is the *only* place self-test performs
			 * allocation while a test is running
			 */

			ret = alloc_buf_entry(&buf_entry, session,
					      rpc_req->cr_ctx);
			if (ret != 0) {
				D_ERROR("Failed to allocate buf_entry;"
					" ret=%d\n", ret);
				return;
			}

			session->params.num_buffers++;
		}
	}

	if (session->params.send_type == CRT_SELF_TEST_MSG_TYPE_BULK_GET) {
		struct crt_bulk_desc bulk_desc;
		crt_bulk_t bulk_remote_hdl =
			((struct crt_st_send_id_bulk *)args)->bulk_hdl;

		D_ASSERT(bulk_remote_hdl != CRT_BULK_NULL);
		D_ASSERT(buf_entry->bulk_hdl != CRT_BULK_NULL);
		D_ASSERT(rpc_req != NULL);

		bulk_desc.bd_rpc = rpc_req;
		bulk_desc.bd_bulk_op = CRT_BULK_GET;
		bulk_desc.bd_remote_hdl = bulk_remote_hdl;
		bulk_desc.bd_remote_off = 0;
		bulk_desc.bd_local_hdl = buf_entry->bulk_hdl;
		bulk_desc.bd_local_off = 0;
		bulk_desc.bd_len = session->params.send_size;

		ret = crt_bulk_transfer(&bulk_desc,
					crt_self_test_msg_bulk_get_cb,
					buf_entry, NULL);
		if (ret != 0) {
			D_ERROR("self-test service BULK_GET failed; ret=%d\n",
				ret);
			crt_self_test_msg_send_reply(rpc_req, NULL, 1);
			return;
		}
	} else if (session->params.reply_type ==
		   CRT_SELF_TEST_MSG_TYPE_BULK_PUT) {
		struct crt_bulk_desc bulk_desc;
		crt_bulk_t bulk_remote_hdl = CRT_BULK_NULL;

		if (session->params.send_type == CRT_SELF_TEST_MSG_TYPE_IOV)
			bulk_remote_hdl = ((struct crt_st_send_id_iov_bulk *)
					   args)->bulk_hdl;
		else
			bulk_remote_hdl = ((struct crt_st_send_id_bulk *)
					   args)->bulk_hdl;

		bulk_desc.bd_rpc = rpc_req;
		bulk_desc.bd_bulk_op = CRT_BULK_PUT;
		bulk_desc.bd_remote_hdl = bulk_remote_hdl;
		bulk_desc.bd_remote_off = 0;
		bulk_desc.bd_local_hdl = buf_entry->bulk_hdl;
		bulk_desc.bd_local_off = 0;
		bulk_desc.bd_len = buf_entry->session->params.reply_size;

		ret = crt_bulk_transfer(&bulk_desc,
					crt_self_test_msg_bulk_put_cb,
					buf_entry, NULL);
		if (ret != 0) {
			D_ERROR("self-test service BULK_GET failed; ret=%d\n",
				ret);
			crt_self_test_msg_send_reply(rpc_req, NULL, 1);
			return;
		}
	} else {
		crt_self_test_msg_send_reply(rpc_req, buf_entry, 1);
		return;
	}
}
