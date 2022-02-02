/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * Test mocks for common syscalls and other functions that may need to be
 * commonly mocked
 */

#ifndef __DAOS_DRPC_MOCKS_INTERNAL_H__
#define __DAOS_DRPC_MOCKS_INTERNAL_H__

#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <poll.h>
#include <abt.h>
#include <daos/drpc.h>
#include <daos/drpc.pb-c.h>

void mock_socket_setup(void);
extern int socket_return; /* value to be returned by socket() */
extern int socket_family; /* saved input */
extern int socket_type; /* saved input */
extern int socket_protocol; /* saved input */

void mock_connect_setup(void);
extern int connect_return; /* value to be returned by connect() */
extern int connect_sockfd; /* saved input */
extern const struct sockaddr *connect_addr_ptr; /* for nullcheck */
extern struct sockaddr_un connect_addr; /* saved copy of input value */
extern socklen_t connect_addrlen; /* saved input */

void mock_bind_setup(void);
extern int bind_return; /* value to be returned by bind() */
extern int bind_sockfd; /* saved input */
extern const struct sockaddr *bind_addr_ptr; /* for nullcheck */
extern struct sockaddr_un bind_addr; /* saved copy of input value */
extern socklen_t bind_addrlen; /* saved input */

void mock_fcntl_setup(void);
extern int fcntl_return; /* value to be returned by fcntl() */
extern int fcntl_fd; /* saved input */
extern int fcntl_cmd; /* saved input */
extern int fcntl_arg; /* saved input */

void mock_listen_setup(void);
extern int listen_return; /* value to be returned by listen() */
extern int listen_sockfd; /* saved input */
extern int listen_backlog; /* saved input */

void mock_accept_setup(void);
extern int accept_call_count; /* number of times accept() was called */
extern int accept_return; /* value to be returned by accept() */
extern int accept_sockfd; /* saved input */
extern struct sockaddr *accept_addr_ptr; /* saved input ptr */
extern socklen_t *accept_addrlen_ptr; /* saved input ptr */

void mock_close_setup(void);
extern int close_call_count; /* number of times it was called */
extern int close_return; /* value to be returned by close() */
extern int close_fd; /* saved input */

void mock_sendmsg_setup(void);
extern int sendmsg_call_count; /* how many times it was called */
extern ssize_t sendmsg_return; /* to be returned by sendmsg() */
extern int sendmsg_sockfd; /* saved input */
extern const struct msghdr *sendmsg_msg_ptr; /* saved address */
extern void *sendmsg_msg_iov_base_ptr; /* saved ptr address */
extern size_t sendmsg_msg_iov_len; /* saved iov len */
extern uint8_t sendmsg_msg_content[UNIXCOMM_MAXMSGSIZE]; /* copied into iov */
extern int sendmsg_flags; /* saved input */

void mock_recvmsg_setup(void);
extern int recvmsg_call_count; /* how many times it was called */
extern ssize_t recvmsg_return; /* value to be returned */
extern int recvmsg_sockfd; /* saved input */
extern struct msghdr *recvmsg_msg_ptr; /* saved ptr address */
extern void *recvmsg_msg_iov_base_ptr; /* saved ptr address */
extern size_t recvmsg_msg_iov_len; /* saved iov len */
extern uint8_t recvmsg_msg_content[UNIXCOMM_MAXMSGSIZE]; /* copied into iov */
extern int recvmsg_flags; /* saved input */

/**
 * Sets up recvmsg mock to populate a valid serialized Drpc__Call as the message received.
 */
void mock_valid_drpc_call_in_recvmsg(void);

/**
 * Sets up recvmsg mock to populate a valid serialized Drpc__Response as the message received.
 */
void mock_valid_drpc_resp_in_recvmsg(Drpc__Status status);

void mock_poll_setup(void);
void mock_poll_teardown(void);
extern int poll_return; /* value to be returned */
extern void *poll_fds_ptr; /* saved ptr address */
extern struct pollfd *poll_fds; /* saved copy of input */
extern nfds_t poll_nfds; /* saved input */
extern int poll_timeout; /* saved input */
extern int poll_revents_return[1024]; /* to be returned in revents struct */

void mock_unlink_setup(void);
extern int unlink_call_count;
extern const char *unlink_name;

void mock_ABT_mutex_create_setup(void);
extern int ABT_mutex_create_return; /* value to be returned */
extern ABT_mutex *ABT_mutex_create_newmutex_ptr; /* saved ptr address */

void mock_ABT_mutex_free_setup(void);
extern int ABT_mutex_free_return; /* value to be returned */
extern ABT_mutex *ABT_mutex_free_mutex_ptr; /* saved ptr address */

void mock_ABT_thread_join_setup(void);
extern int ABT_thread_join_return; /* value to be returned */
extern int ABT_thread_join_call_count; /* number of times called */

void mock_ABT_thread_free_setup(void);
extern int ABT_thread_free_return;
extern ABT_thread *ABT_thread_free_thread_ptr;

#endif /* __DAOS_DRPC_MOCKS_INTERNAL_H__ */
