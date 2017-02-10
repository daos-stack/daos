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
#include <pthread.h>
#include <crt_internal.h>

/*
 * An overview of self-test sessions:
 *
 * Primary role of sessions:
 * - Memory pre-allocated by open and cleaned up by close (no allocations
 *   during the actual test).
 * - In the future, the amount of information passed to self-test can grow
 *   without changing the size of the test RPCs (which instead only require a
 *   session id to convey all that same information)
 * - This should reduce the number of opcodes needed for self-test to only
 *   three - one each for open / close / testmessage
 *   TODO: Currently four opcodes are used - they need to be consolidated
 *
 * Opening a session before starting a test is required only if the reply size
 * is nonzero. In the future, sessions will also be required for all bulk
 * transfers
 *
 * When a session is opened, a pool of buffers is allocated (with the number of
 * buffers specified by the caller of open). These buffers are then placed in
 * a stack (aka first-in-last-out queue) for that session. When a new test RPC
 * request is received, a free buffer is popped off the stack and used to
 * service that request. After the response is sent, the buffer is re-added at
 * the front of the stack. This keeps a few buffers constantly in use and some
 * completely idle, which increases the likelihood that buffers will already be
 * in cache. Each session has a lock to protect the stack from concurrent
 * modification.
 *
 * Corner cases that have to be handled:
 * - Open session / close session can be called while RPCs are processing
 *   - This implementation uses read-write locks. Many parallel test messages
 *     can grab as many read locks as needed to satisfy the incoming requests.
 *     When an open or close is called, a write lock is placed over the list of
 *     sessions which excludes all the readers temporarily.
 *   - In the event of open - business returns to normal for ongoing test RPCs
 *   - In the event of close - the ongoing RPCs are no longer able to locate
 *     the requested session ID and will fail gracefully
 *
 * - Minimal buffer / lock contention for multiple threads working on RPCs
 *   - No memory allocation / recollection is performed while holding a lock
 *   - Write locks that disrupt all test messages are only required briefly
 *     while adding or removing a session
 *   - Spinlocks are used to take/return available buffers from the per-session
 *     stack
 */

/*
 * Very simple buffer entries that can be formed into a stack
 *
 * Note that buf is a flat array tacked on to the end of this structure rather
 * than a pointer to separately allocated memory - it's all one big block
 */
struct st_buf_entry {
	struct st_buf_entry *next;
	char buf[];
};

struct st_session {
	/** Session ID. Note that session ID's must be unique */
	int32_t session_id;

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

static int32_t g_last_session_id;

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
static struct st_session *find_session(int32_t session_id,
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

void crt_self_test_init(void)
{
	int ret;

	ret = pthread_rwlock_init(&g_all_session_lock, NULL);
	C_ASSERT(ret == 0);
}

int crt_self_test_open_session_handler(crt_rpc_t *rpc_req)
{
	struct crt_st_session_params	*args;
	struct st_session		*new_session;
	int32_t				*reply_session_id;
	int32_t				 session_id;
	uint32_t			 i;
	int				 ret;

	/* Get pointers to the arguments and response buffers */
	args = (struct crt_st_session_params *)crt_req_get(rpc_req);
	C_ASSERT(args != NULL);

	reply_session_id = (int32_t *)crt_reply_get(rpc_req);
	C_ASSERT(reply_session_id != NULL);

	/* Allocate a structure for the new session */
	C_ALLOC(new_session, sizeof(struct st_session));
	if (new_session == NULL) {
		C_ERROR("Failed to allocate new session\n");
		C_GOTO(send_rpc, *reply_session_id = -1);
	}

	/* Initialize the new session */
	ret = pthread_spin_init(&new_session->buf_list_lock,
				PTHREAD_PROCESS_PRIVATE);
	C_ASSERT(ret == 0);

	/* Copy the session parameters */
	memcpy(&new_session->params, args, sizeof(new_session->params));

	/*
	 * Allocate as many descriptors (with accompanying buffers) as
	 * requested by the caller
	 */
	for (i = 0; i < args->num_buffers; i++) {
		struct st_buf_entry *new_entry;

		C_ALLOC(new_entry,
			sizeof(struct st_buf_entry) + args->reply_size);
		if (new_entry == NULL) {
			C_ERROR("self-test memory allocation failed for new"
				" session - num_buffers=%d, reply_size=%d\n",
				args->num_buffers, args->reply_size);
			C_GOTO(send_rpc, *reply_session_id = -1);
		}

		/* Fill the buffer with an arbitrary data pattern */
		memset(new_entry->buf, 0xA7, args->reply_size);

		/* Push this new entry onto the head of the stack */
		new_entry->next = new_session->buf_list;
		new_session->buf_list = new_entry;
	}

	/******************** LOCK: g_all_session_lock (w) ********************/
	ret = pthread_rwlock_wrlock(&g_all_session_lock);
	C_ASSERT(ret == 0);

	/*
	 * Check session_id's for availability starting with one more than the
	 * most recent session_id issued. This rolls around to zero when
	 * reaching INT_MAX so that every possible session_id is tried before
	 * giving up.
	 *
	 * This means that until INT_MAX session IDs are issued, only one search
	 * through the list has to be performed to open a new session
	 */
	session_id = g_last_session_id + 1;
	while (session_id != g_last_session_id) {
		struct st_session *found_session;

		found_session = find_session(session_id, NULL);
		if (found_session == NULL)
			/* No exsiting session - use this session_id */
			break;

		if (session_id == INT_MAX)
			session_id = 0;
		else
			session_id++;
	}

	if (session_id == g_last_session_id) {
		C_ERROR("self-test: No test sessions available to reserve\n");
		*reply_session_id = -1;
	} else {
		/* Success - found an unused session ID */
		new_session->session_id = session_id;
		g_last_session_id = session_id;
		*reply_session_id = session_id;

		/* Add the new session to the list of open sessions */
		new_session->next = g_session_list;
		g_session_list = new_session;
	}

	ret = pthread_rwlock_unlock(&g_all_session_lock);
	C_ASSERT(ret == 0);
	/******************* UNLOCK: g_all_session_lock *******************/

send_rpc:
	/* Release any allocated memory if returning an invalid session ID */
	if (*reply_session_id < 0 && new_session != NULL) {
		struct st_buf_entry *free_entry;

		free_entry = new_session->buf_list;
		while (free_entry != NULL) {
			new_session->buf_list = free_entry->next;
			C_FREE(free_entry, sizeof(struct st_buf_entry)
					   + args->reply_size);

			free_entry = new_session->buf_list;
		}

		C_FREE(new_session, sizeof(struct st_session));
	}

	ret = crt_reply_send(rpc_req);
	if (ret != 0)
		C_ERROR("self-test: crt_reply_send failed; ret = %d\n", ret);

	return 0;
}

int crt_self_test_close_session_handler(crt_rpc_t *rpc_req)
{
	int32_t			*args;
	struct st_session	*del_session;
	struct st_session	**prev;
	struct st_buf_entry	*free_entry;
	int32_t			 session_id;
	int			 ret;

	args = (int32_t *)crt_req_get(rpc_req);
	C_ASSERT(args != NULL);
	session_id = *args;

	/******************** LOCK: g_all_session_lock (w) ********************/
	ret = pthread_rwlock_wrlock(&g_all_session_lock);
	C_ASSERT(ret == 0);

	/* Find the session if it exists */
	del_session = find_session(session_id, &prev);
	if (del_session == NULL) {
		C_ERROR("Self-test session %d not found\n", session_id);
		goto send_rpc;
	}

	/* Remove the session from the list of active sessions */
	*prev = del_session->next;

	ret = pthread_rwlock_unlock(&g_all_session_lock);
	C_ASSERT(ret == 0);
	/******************* UNLOCK: g_all_session_lock *******************/

	/* At this point this function has the only reference to del_session */

	free_entry = del_session->buf_list;
	while (free_entry != NULL) {
		del_session->buf_list = free_entry->next;
		C_FREE(free_entry, sizeof(struct st_buf_entry)
				   + del_session->params.reply_size);

		free_entry = del_session->buf_list;
	}

	C_FREE(del_session, sizeof(struct st_session));

send_rpc:

	ret = crt_reply_send(rpc_req);
	if (ret != 0)
		C_ERROR("self-test: crt_reply_send failed; ret = %d\n", ret);

	return 0;
}


void crt_self_test_msg_send_reply(crt_rpc_t *rpc_req,
				  struct st_session *session,
				  struct st_buf_entry *buf_entry,
				  int do_unlock)
{
	crt_iov_t	*res;
	int		 ret;

	if (buf_entry != NULL) {
		/* Get the IOV reply handle */
		res = (crt_iov_t *)crt_reply_get(rpc_req);
		C_ASSERT(res != NULL);

		/* Set the reply buffer */
		crt_iov_set(res, buf_entry->buf, session->params.reply_size);
	}

	ret = crt_reply_send(rpc_req);
	if (ret != 0)
		C_ERROR("self-test: crt_reply_send failed; ret = %d\n", ret);

	/*
	 * If a buffer was pulled off the stack, re-add it now that it has
	 * served its purpose
	 */
	if (buf_entry != NULL) {
		/************* LOCK: session->buf_list_lock *************/
		pthread_spin_lock(&session->buf_list_lock);

		buf_entry->next = session->buf_list;
		session->buf_list = buf_entry;

		pthread_spin_unlock(&session->buf_list_lock);
		/************ UNLOCK: session->buf_list_lock ************/
	}

	if (do_unlock) {
		/************** UNLOCK: g_all_session_lock **************/
		ret = pthread_rwlock_unlock(&g_all_session_lock);
		C_ASSERT(ret == 0);
	}
}


int crt_self_test_msg_handler(crt_rpc_t *rpc_req)
{
	void			*args;
	struct st_buf_entry	*buf_entry = NULL;
	struct st_session	*session;
	int32_t			 session_id;
	int			 ret;

	C_ASSERT(rpc_req->cr_opc == CRT_OPC_SELF_TEST_BOTH_EMPTY ||
		 rpc_req->cr_opc == CRT_OPC_SELF_TEST_SEND_EMPTY_REPLY_IOV ||
		 rpc_req->cr_opc == CRT_OPC_SELF_TEST_SEND_IOV_REPLY_EMPTY ||
		 rpc_req->cr_opc == CRT_OPC_SELF_TEST_BOTH_IOV);

	/*
	 * For messages that do not use bulk and have no reply data, skip
	 * directly to sending the reply
	 */
	if (rpc_req->cr_opc == CRT_OPC_SELF_TEST_BOTH_EMPTY ||
	    rpc_req->cr_opc == CRT_OPC_SELF_TEST_SEND_IOV_REPLY_EMPTY) {
		crt_self_test_msg_send_reply(rpc_req, NULL, NULL, 0);
		return 0;
	}

	/*
	 * Past this point messages require a dedicated buffer on this end
	 * (either to use for IOV out, bulk transfer in, or bulk transfer out)
	 */

	/* Get input RPC buffer */
	args = crt_req_get(rpc_req);
	C_ASSERT(args != NULL);

	/* Retrieve the session ID from the beginning of the arguments */
	session_id = *((int32_t *)args);

	/******************** LOCK: g_all_session_lock (r) ********************/
	ret = pthread_rwlock_rdlock(&g_all_session_lock);
	C_ASSERT(ret == 0);

	session = find_session(session_id, NULL);
	if (session == NULL) {
		C_ERROR("Unable to locate session_id %d\n", session_id);
		crt_self_test_msg_send_reply(rpc_req, NULL, NULL, 1);
		return 0;
	}

	/* Retrieve the next available buffer from the stack for this session */
	while (buf_entry == NULL) {
		/************* LOCK: session->buf_list_lock *************/
		pthread_spin_lock(&session->buf_list_lock);

		/* Retrieve a send buffer from the top of the stack */
		buf_entry = session->buf_list;
		if (buf_entry != NULL)
			session->buf_list = buf_entry->next;

		pthread_spin_unlock(&session->buf_list_lock);
		/************ UNLOCK: session->buf_list_lock ************/

		/* No buffers available currently, need to wait */
		if (buf_entry == NULL) {
			C_WARN("No self-test buffers available for session %d,"
			       " num allocated = %d."
			       " This will decrease performance.\n",
			       session_id, session->params.num_buffers);
			sched_yield();
		}
	}

	crt_self_test_msg_send_reply(rpc_req, session, buf_entry, 1);

	return 0;
}
