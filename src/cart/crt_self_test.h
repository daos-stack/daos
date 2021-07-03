/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __CRT_SELF_TEST_H__
#define __CRT_SELF_TEST_H__



/*
 * List of supported self-test strategies:
 *
 * SEND:  EMPTY, IOV, BULK_GET
 * REPLY: EMPTY, IOV, BULK_PUT
 *
 * All 9 combinations of the above are supported. Using 7 unique opcodes.
 *
 * Here's a table:
 *
 * SEND:     REPLY:    OPCODE:
 * EMPTY     EMPTY     CRT_OPC_SELF_TEST_BOTH_EMPTY
 * EMPTY     IOV       CRT_OPC_SELF_TEST_SEND_ID_REPLY_IOV
 * EMPTY     BULK_PUT  CRT_OPC_SELF_TEST_BOTH_BULK
 * EMPTY     BULK_GET  <invalid>
 * IOV       EMPTY     CRT_OPC_SELF_TEST_SEND_IOV_REPLY_EMPTY
 * IOV       IOV       CRT_OPC_SELF_TEST_BOTH_IOV
 * IOV       BULK_PUT  CRT_OPC_SELF_TEST_SEND_IOV_REPLY_BULK
 * IOV       BULK_GET  <invalid>
 * BULK_GET  EMPTY     CRT_OPC_SELF_TEST_BOTH_BULK
 * BULK_GET  IOV       CRT_OPC_SELF_TEST_SEND_BULK_REPLY_IOV
 * BULK_GET  BULK_PUT  CRT_OPC_SELF_TEST_BOTH_BULK
 * BULK_GET  BULK_GET  <invalid>
 * BULK_PUT  EMPTY     <invalid>
 * BULK_PUT  IOV       <invalid>
 * BULK_PUT  BULK_PUT  <invalid>
 * BULK_PUT  BULK_GET  <invalid>
 *
 * There are only 7 opcodes because three operations involving bulk all have
 * identical send/reply messages and therefore do not require unique opcodes
 *
 * Note that BULK_GET on the sending side means that the client will init a bulk
 * session and send it to the service which will perform a BULK_GET to transfer
 * the data. Note that sending a BULK_PUT is not supported because this would
 * require an extra RPC - the service would first have to init its own buffer
 * before instructing the client to perform a BULK_PUT.
 *
 * Similarly, BULK_PUT on the reply side means that the service will perform a
 * BULK_PUT before replying to the test RPC. A BULK_GET is not supported for
 * replies because, again, an extra RPC would be needed to instruct the service
 * to clean up the bulk session at the end of the transfer.
 *
 *
 * The following data structures are used for the various possible RPCs:
 * SEND:
 *    <empty>                                  (NULL)
 *        CRT_OPC_SELF_TEST_BOTH_EMPTY
 *    session_id only                          (int32_t)
 *        CRT_OPC_SELF_TEST_SEND_ID_REPLY_IOV
 *    session_id, iov                          (int32_t, d_iov_t)
 *        CRT_OPC_SELF_TEST_SEND_IOV_REPLY_EMPTY
 *        CRT_OPC_SELF_TEST_BOTH_IOV
 *    session_id, iov, bulk handle             (int32_t, d_iov_t, crt_bulk_t)
 *        CRT_OPC_SELF_TEST_SEND_IOV_REPLY_BULK
 *    session_id, bulk handle                  (int32_t, crt_bulk_t)
 *        CRT_OPC_SELF_TEST_SEND_BULK_REPLY_IOV
 *        CRT_OPC_SELF_TEST_BOTH_BULK
 *
 * REPLY:
 *    <empty>                                  (NULL)
 *        CRT_OPC_SELF_TEST_BOTH_EMPTY
 *        CRT_OPC_SELF_TEST_SEND_IOV_REPLY_EMPTY
 *        CRT_OPC_SELF_TEST_SEND_IOV_REPLY_BULK
 *        CRT_OPC_SELF_TEST_BOTH_BULK
 *    iov                                      (d_iov_t)
 *        CRT_OPC_SELF_TEST_SEND_ID_REPLY_IOV
 *        CRT_OPC_SELF_TEST_BOTH_IOV
 *        CRT_OPC_SELF_TEST_SEND_BULK_REPLY_IOV
 */

/*
 * An overview of self-test sessions:
 *
 * Primary role of sessions:
 * - Memory pre-allocated by open and cleaned up by close (no allocations
 *   during the actual test).
 * - In the future, the amount of information passed to self-test can grow
 *   without changing the size of the test RPCs (which instead only require a
 *   session id to convey all that same information)
 * - Provide long-lived bulk handles to re-use across multiple test messages,
 *   reducing their overhead
 *
 * Opening a session before starting a test is required for all messages except
 * those that are completely empty (send and reply size = 0)
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

#define CRT_ST_BUF_ALIGN_DEFAULT (-1)
#define CRT_ST_BUF_ALIGN_MIN (0)
/** Maximum alignment must be one less than a power of two */
#define CRT_ST_BUF_ALIGN_MAX (255)

enum crt_st_msg_type {
	CRT_SELF_TEST_MSG_TYPE_EMPTY = 0,
	CRT_SELF_TEST_MSG_TYPE_IOV,
	CRT_SELF_TEST_MSG_TYPE_BULK_PUT,
	CRT_SELF_TEST_MSG_TYPE_BULK_GET,
};

struct crt_st_session_params {
	uint32_t send_size;
	uint32_t reply_size;
	uint32_t num_buffers;
	union {
		struct {
			enum crt_st_msg_type send_type: 2;
			enum crt_st_msg_type reply_type: 2;
			int16_t buf_alignment: 16;
		};
		uint32_t flags;
	};
};

enum crt_st_status {
	/* No test session / data was found */
	CRT_ST_STATUS_INVAL = -DER_INVAL,

	/* Test found and still busy processing */
	CRT_ST_STATUS_TEST_IN_PROGRESS = -DER_BUSY,

	/* Test complete and returned data is valid */
	CRT_ST_STATUS_TEST_COMPLETE = 0,

	/* Test finished unsuccessfully but partial data was returned */
	CRT_ST_STATUS_TEST_COMPLETE_WITH_ERRORS = 1,
};

/*
 * Note that for these non-empty send structures the session_id is always
 * the first value. This allows the session to be retrieved without knowing
 * what the rest of the structure contains
 */

struct crt_st_send_id_iov {
	int64_t		session_id;
	d_iov_t		buf;
};

struct crt_st_send_id_iov_bulk {
	int64_t		session_id;
	d_iov_t		buf;
	crt_bulk_t	bulk_hdl;
};

struct crt_st_send_id_bulk {
	int64_t session_id;
	crt_bulk_t bulk_hdl;
};

struct crt_st_start_params {
	crt_group_id_t srv_grp;
	/*
	 * Array of rank (uint32_t) and tag (uint32_t) pairs
	 * num_endpts = endpts.len / 8
	 */
	d_iov_t endpts;
	uint32_t rep_count;
	uint32_t max_inflight;
	uint32_t send_size;
	uint32_t reply_size;
	union {
		struct {
			enum crt_st_msg_type send_type: 2;
			enum crt_st_msg_type reply_type: 2;
			int16_t buf_alignment: 16;
		};
		uint32_t flags;
	};
};

struct st_latency {
	int64_t val;
	uint32_t rank;
	uint32_t tag;
	int32_t cci_rc;
};

static inline crt_opcode_t
crt_st_compute_opcode(enum crt_st_msg_type send_type,
		      enum crt_st_msg_type reply_type)
{
	D_ASSERT(send_type >= 0 && send_type < 4);
	D_ASSERT(reply_type >= 0 && reply_type < 4);
	D_ASSERT(send_type != CRT_SELF_TEST_MSG_TYPE_BULK_PUT);
	D_ASSERT(reply_type != CRT_SELF_TEST_MSG_TYPE_BULK_GET);

	crt_opcode_t opcodes[4][4] = { { CRT_OPC_SELF_TEST_BOTH_EMPTY,
					 CRT_OPC_SELF_TEST_SEND_ID_REPLY_IOV,
					 CRT_OPC_SELF_TEST_BOTH_BULK,
					 -1 },
				       { CRT_OPC_SELF_TEST_SEND_IOV_REPLY_EMPTY,
					 CRT_OPC_SELF_TEST_BOTH_IOV,
					 CRT_OPC_SELF_TEST_SEND_IOV_REPLY_BULK,
					 -1 },
				       { -1, -1, -1, -1 },
				       { CRT_OPC_SELF_TEST_BOTH_BULK,
					 CRT_OPC_SELF_TEST_SEND_BULK_REPLY_IOV,
					 CRT_OPC_SELF_TEST_BOTH_BULK,
					 -1 } };

	return opcodes[send_type][reply_type];
}

static inline void *crt_st_get_aligned_ptr(void *base, int16_t buf_alignment)
{
	void *returnptr;
	uint32_t offset;

	if (buf_alignment == CRT_ST_BUF_ALIGN_DEFAULT)
		return base;

	D_ASSERT(buf_alignment >= CRT_ST_BUF_ALIGN_MIN &&
		 buf_alignment <= CRT_ST_BUF_ALIGN_MAX);

	offset = (buf_alignment - (((size_t)base) & CRT_ST_BUF_ALIGN_MAX)) %
		(CRT_ST_BUF_ALIGN_MAX + 1);

	returnptr = ((char *)base) + offset;

	/* Catch math bugs */
	D_ASSERT((char *)returnptr <= (((char *)base) + CRT_ST_BUF_ALIGN_MAX));
	D_ASSERT((((size_t)returnptr) & CRT_ST_BUF_ALIGN_MAX) == buf_alignment);

	return returnptr;
}

void crt_self_test_service_init(void);
void crt_self_test_service_finit(void);
void crt_self_test_client_init(void);
void crt_self_test_client_fini(void);
void crt_self_test_init(void);
void crt_self_test_fini(void);
void crt_self_test_msg_handler(crt_rpc_t *rpc_req);
void crt_self_test_open_session_handler(crt_rpc_t *rpc_req);
void crt_self_test_close_session_handler(crt_rpc_t *rpc_req);
void crt_self_test_start_handler(crt_rpc_t *rpc_req);
void crt_self_test_status_req_handler(crt_rpc_t *rpc_req);

#endif /* __CRT_SELF_TEST_H__ */
