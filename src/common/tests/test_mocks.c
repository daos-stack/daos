/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <daos/test_mocks.h>
#include <daos/test_utils.h>

#if D_HAS_WARNING(4, "-Wframe-larger-than=")
	#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif

/**
 * Generic mocks for external functions
 */

void
mock_socket_setup(void)
{
	socket_return = 25; /* arbitrary default - it's a fd */
	socket_family = 0;
	socket_type = 0;
	socket_protocol = 0;
}

int socket_return; /* value to be returned by socket() */
int socket_family; /* saved input */
int socket_type; /* saved input */
int socket_protocol; /* saved input */
int
socket(int family, int type, int protocol)
{
	socket_family = family;
	socket_type = type;
	socket_protocol = protocol;
	return socket_return;
}

void
mock_connect_setup(void)
{
	connect_return = 0; /* success */
	connect_sockfd = 0;
	connect_addr_ptr = NULL;
	memset(&connect_addr, 0, sizeof(connect_addr));
	connect_addrlen = 0;
}

int connect_return; /* value to be returned by connect() */
int connect_sockfd; /* saved input */
const struct sockaddr *connect_addr_ptr; /* for nullcheck */
struct sockaddr_un connect_addr; /* saved copy of input value */
socklen_t connect_addrlen; /* saved input */
int
connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	connect_sockfd = sockfd;
	connect_addr_ptr = addr;
	if (addr != NULL) {
		memcpy(&connect_addr, (struct sockaddr_un *)addr,
				sizeof(struct sockaddr_un));
	}
	connect_addrlen = addrlen;
	return connect_return;
}

void mock_bind_setup(void)
{
	bind_return = 0; /* success */
	bind_sockfd = 0;
	bind_addr_ptr = NULL;
	memset(&bind_addr, 0, sizeof(bind_addr));
	bind_addrlen = 0;
}

int bind_return; /* value to be returned by bind() */
int bind_sockfd; /* saved input */
const struct sockaddr *bind_addr_ptr; /* for nullcheck */
struct sockaddr_un bind_addr; /* saved copy of input value */
socklen_t bind_addrlen; /* saved input */
int
bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	bind_sockfd = sockfd;
	bind_addr_ptr = addr;
	if (addr != NULL) {
		memcpy(&bind_addr, (struct sockaddr_un *)addr,
				sizeof(struct sockaddr_un));
	}
	bind_addrlen = addrlen;
	return bind_return;
}

void
mock_fcntl_setup(void)
{
	fcntl_return = 0; /* greater than 0 is success */
	fcntl_fd = 0;
	fcntl_cmd = 0;
	fcntl_arg = 0;
}

int fcntl_return; /* value to be returned by fcntl() */
int fcntl_fd; /* saved input */
int fcntl_cmd; /* saved input */
int fcntl_arg; /* saved input */
int
fcntl(int fd, int cmd, ...)
{
	va_list arglist;

	/*
	 * Assuming only one arg for these tests
	 */
	va_start(arglist, cmd);
	fcntl_arg = va_arg(arglist, int);
	va_end(arglist);

	fcntl_fd = fd;
	fcntl_cmd = cmd;
	return fcntl_return;
}

void
mock_listen_setup(void)
{
	listen_return = 0; /* success */
	listen_sockfd = 0;
	listen_backlog = 0;
}

int listen_return; /* value to be returned by listen() */
int listen_sockfd; /* saved input */
int listen_backlog; /* saved input */
int
listen(int sockfd, int backlog)
{
	listen_sockfd = sockfd;
	listen_backlog = backlog;
	return listen_return;
}

void
mock_accept_setup(void)
{
	accept_call_count = 0;
	accept_return = 50; /* arbitrary default -- it's a fd */
	accept_sockfd = 0;
	accept_addr_ptr = NULL;
	accept_addrlen_ptr = NULL;
}

int accept_call_count;
int accept_return; /* value to be returned by accept() */
int accept_sockfd; /* saved input */
struct sockaddr *accept_addr_ptr; /* saved input ptr */
socklen_t *accept_addrlen_ptr; /* saved input ptr */
int
accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
	accept_sockfd = sockfd;
	accept_addr_ptr = addr;
	accept_addrlen_ptr = addrlen;
	accept_call_count++;
	return accept_return;
}

void
mock_close_setup(void)
{
	close_call_count = 0;
	close_return = 0; /* success */
	close_fd = 0;
}

int close_call_count;
int close_return; /* value to be returned by close() */
int close_fd; /* saved input */
int
close(int fd)
{
	close_fd = fd;
	close_call_count++;
	return close_return;
}

void
mock_sendmsg_setup(void)
{
	sendmsg_call_count = 0;
	sendmsg_return = 5; /* greater than 0 is success */
	sendmsg_sockfd = 0;
	sendmsg_msg_ptr = NULL;
	sendmsg_msg_iov_base_ptr = NULL;
	sendmsg_msg_iov_len = 0;
	memset(&sendmsg_msg_content, 0, sizeof(sendmsg_msg_content));
	sendmsg_flags = 0;
}

int sendmsg_call_count; /* how many times it was called */
ssize_t sendmsg_return; /* to be returned by sendmsg() */
int sendmsg_sockfd; /* saved input */
const struct msghdr *sendmsg_msg_ptr; /* saved address */
void *sendmsg_msg_iov_base_ptr; /* saved ptr address */
size_t sendmsg_msg_iov_len; /* saved iov len */
uint8_t sendmsg_msg_content[UNIXCOMM_MAXMSGSIZE]; /* copied into iov */
int sendmsg_flags; /* saved input */
ssize_t
sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
	sendmsg_call_count++;

	sendmsg_sockfd = sockfd;
	sendmsg_msg_ptr = msg;
	if (msg != NULL) {
		memcpy(sendmsg_msg_content, msg->msg_iov[0].iov_base,
				msg->msg_iov[0].iov_len);
		sendmsg_msg_iov_base_ptr = msg->msg_iov[0].iov_base;
		sendmsg_msg_iov_len = msg->msg_iov[0].iov_len;
	}
	sendmsg_flags = flags;
	return sendmsg_return;
}

void
mock_recvmsg_setup(void)
{
	recvmsg_call_count = 0;
	recvmsg_return = 5; /* greater than 0 is success */
	recvmsg_sockfd = 0;
	recvmsg_msg_ptr = NULL;
	recvmsg_msg_iov_base_ptr = NULL;
	recvmsg_msg_iov_len = 0;
	memset(recvmsg_msg_content, 0, sizeof(recvmsg_msg_content));
	recvmsg_flags = 0;
}

int recvmsg_call_count; /* how many times it was called */
ssize_t recvmsg_return; /* value to be returned */
int recvmsg_sockfd; /* saved input */
struct msghdr *recvmsg_msg_ptr; /* saved ptr address */
void *recvmsg_msg_iov_base_ptr; /* saved ptr address */
size_t recvmsg_msg_iov_len; /* saved iov len */
uint8_t recvmsg_msg_content[UNIXCOMM_MAXMSGSIZE]; /* copied into iov */
int recvmsg_flags; /* saved input */
ssize_t
recvmsg(int sockfd, struct msghdr *msg, int flags)
{
	recvmsg_call_count++;

	recvmsg_sockfd = sockfd;
	recvmsg_msg_ptr = msg;
	if (msg != NULL) {
		/*
		 * Making an assumption that the size of the IOV
		 * provided is big enough to hold the message
		 */
		memcpy(msg->msg_iov[0].iov_base, recvmsg_msg_content,
			msg->msg_iov[0].iov_len);
		recvmsg_msg_iov_base_ptr = msg->msg_iov[0].iov_base;
		recvmsg_msg_iov_len = msg->msg_iov[0].iov_len;
	}
	recvmsg_flags = flags;
	return recvmsg_return;
}

void
mock_poll_setup(void)
{
	poll_return = 1; /* greater than 0 is success */
	poll_fds_ptr = NULL;
	poll_fds = NULL;
	poll_nfds = 0;
	poll_timeout = 0;
	memset(poll_revents_return, 0, sizeof(poll_revents_return));
}

void
mock_poll_teardown(void)
{
	D_FREE(poll_fds);
}

int poll_return; /* value to be returned */
void *poll_fds_ptr; /* saved ptr address */
struct pollfd *poll_fds; /* saved copy of input */
nfds_t poll_nfds; /* saved input */
int poll_timeout; /* saved input */
int poll_revents_return[1024]; /* to be returned in revents struct */
int
poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	poll_fds_ptr = (void *)fds;
	if (fds != NULL) {
		nfds_t i;

		/* Make a copy */
		D_ALLOC_ARRAY(poll_fds, (int)nfds);
		memcpy(poll_fds, fds, nfds * sizeof(struct pollfd));

		/* return events for the fds */
		for (i = 0; i < nfds; i++) {
			fds[i].revents = poll_revents_return[i];
		}
	}
	poll_nfds = nfds;
	poll_timeout = timeout;
	return poll_return;
}

void
mock_unlink_setup(void)
{
	unlink_call_count = 0;
	unlink_name = NULL;
}

int unlink_call_count;
const char *unlink_name;
int
unlink(const char *__name)
{
	unlink_call_count++;
	unlink_name = __name;
	return 0;
}

/* Mock for the drpc->handler function pointer */
void
mock_drpc_handler_setup(void)
{
	mock_drpc_handler_call_count = 0;
	mock_drpc_handler_call = NULL;
	mock_drpc_handler_resp_ptr = NULL;
	mock_drpc_handler_resp_return = new_drpc_response();
}

void
mock_drpc_handler_teardown(void)
{
	if (mock_drpc_handler_call != NULL) {
		drpc__call__free_unpacked(mock_drpc_handler_call, NULL);
	}

	drpc__response__free_unpacked(mock_drpc_handler_resp_return, NULL);
}

int mock_drpc_handler_call_count; /* how many times it was called */
Drpc__Call *mock_drpc_handler_call; /* alloc copy of the structure passed in */
void *mock_drpc_handler_resp_ptr; /* saved value of resp ptr */
Drpc__Response *mock_drpc_handler_resp_return; /* to be returned in *resp */
void
mock_drpc_handler(Drpc__Call *call, Drpc__Response *resp)
{
	uint8_t buffer[UNIXCOMM_MAXMSGSIZE];

	mock_drpc_handler_call_count++;

	if (call == NULL) {
		mock_drpc_handler_call = NULL;
	} else {
		/*
		 * Caller will free the original so we want to make a copy.
		 * Keep only the latest call.
		 */
		if (mock_drpc_handler_call != NULL) {
			drpc__call__free_unpacked(mock_drpc_handler_call, NULL);
		}

		/*
		 * Drpc__Call has hierarchy of pointers - easiest way to
		 * copy is to pack and unpack.
		 */
		drpc__call__pack(call, buffer);
		mock_drpc_handler_call = drpc__call__unpack(NULL,
				drpc__call__get_packed_size(call),
				buffer);
	}

	mock_drpc_handler_resp_ptr = (void *)resp;

	if (resp != NULL && mock_drpc_handler_resp_return != NULL) {
		size_t len;

		len = mock_drpc_handler_resp_return->body.len;
		memcpy(resp, mock_drpc_handler_resp_return,
				sizeof(Drpc__Response));
		resp->body.len = len;
		if (len > 0) {
			D_ALLOC(resp->body.data, len);
			memcpy(resp->body.data,
				mock_drpc_handler_resp_return->body.data, len);
		}

	}
}

/* Mocks/stubs for Argobots functions */

/* TODO: implement mock */
int
ABT_mutex_lock(ABT_mutex mutex)
{
	return 0;
}

/* TODO: implement mock */
int
ABT_mutex_unlock(ABT_mutex mutex)
{
	return 0;
}

void
mock_ABT_mutex_create_setup(void)
{
	ABT_mutex_create_return = 0;
	ABT_mutex_create_newmutex_ptr = NULL;
}

int ABT_mutex_create_return;
ABT_mutex *ABT_mutex_create_newmutex_ptr;
int
ABT_mutex_create(ABT_mutex *newmutex)
{
	ABT_mutex_create_newmutex_ptr = newmutex;
	return ABT_mutex_create_return;
}

void
mock_ABT_mutex_free_setup(void)
{
	ABT_mutex_free_return = 0;
	ABT_mutex_free_mutex_ptr = NULL;
}

int ABT_mutex_free_return;
ABT_mutex *ABT_mutex_free_mutex_ptr;
int
ABT_mutex_free(ABT_mutex *mutex)
{
	ABT_mutex_free_mutex_ptr = mutex;
	return ABT_mutex_free_return;
}

void
mock_ABT_thread_join_setup(void)
{
	ABT_thread_join_return = 0;
	ABT_thread_join_call_count = 0;
}

int ABT_thread_join_return;
int ABT_thread_join_call_count;
int
ABT_thread_join(ABT_thread thread)
{
	ABT_thread_join_call_count++;
	return ABT_thread_join_return;
}

void
mock_ABT_thread_free_setup(void)
{
	ABT_thread_free_return = 0;
	ABT_thread_free_thread_ptr = NULL;
}

int ABT_thread_free_return;
ABT_thread *ABT_thread_free_thread_ptr;
int
ABT_thread_free(ABT_thread *thread)
{
	ABT_thread_free_thread_ptr = thread;
	return ABT_thread_free_return;
}
