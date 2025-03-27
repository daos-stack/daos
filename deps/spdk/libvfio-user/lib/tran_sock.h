/*
 * Copyright (c) 2019 Nutanix Inc. All rights reserved.
 *
 * Authors: Thanos Makatos <thanos@nutanix.com>
 *          Swapnil Ingle <swapnil.ingle@nutanix.com>
 *          Felipe Franciosi <felipe@nutanix.com>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *      * Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *      * Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *      * Neither the name of Nutanix nor the names of its contributors may be
 *        used to endorse or promote products derived from this software without
 *        specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 *  DAMAGE.
 *
 */

#ifndef LIB_VFIO_USER_TRAN_SOCK_H
#define LIB_VFIO_USER_TRAN_SOCK_H

#include "libvfio-user.h"
#include "tran.h"

extern struct transport_ops tran_sock_ops;

/*
 * These are not public routines, but for convenience, they are used by the
 * sample/test code as well as privately within libvfio-user.
 *
 * Note there is currently only one real transport - talking over a UNIX socket.
 */

/*
 * Send a message to the other end.  The iovecs array should leave the first
 * entry empty, as it will be used for the header.
 */
int
tran_sock_send_iovec(int sock, uint16_t msg_id, bool is_reply,
                     enum vfio_user_command cmd, struct iovec *iovecs,
                     size_t nr_iovecs, int *fds, int count, int err);

/*
 * Send a message to the other end with the given data.
 */
int
tran_sock_send(int sock, uint16_t msg_id, bool is_reply,
               enum vfio_user_command cmd, void *data, size_t data_len);

/*
 * Receive a message from the other end, and place the data into the given
 * buffer. If data is supplied by the other end, it must be exactly *len in
 * size.
 */
int
tran_sock_recv(int sock, struct vfio_user_header *hdr, bool is_reply,
               uint16_t *msg_id, void *data, size_t *len);

/*
 * Receive a message from the other end, but automatically allocate a buffer for
 * it, which must be freed by the caller.  If there is no data, *datap is set to
 * NULL.
 */
int
tran_sock_recv_alloc(int sock, struct vfio_user_header *hdr, bool is_reply,
                     uint16_t *msg_id, void **datap, size_t *lenp);

/*
 * Send and receive a message to the other end, using iovecs for the send. The
 * iovecs array should leave the first entry empty, as it will be used for the
 * header.
 *
 * If specified, the given @send_fds are sent to the other side. @hdr is filled
 * with the reply header if non-NULL.
 *
 * @recv_fds and @recv_fd_count are used to receive file descriptors.
 * If @recv_fd_count is NULL then @recv_fds is ignored and no file descriptors
 * are received. If @recv_fd_count is non-NULL then it contains the number of
 * file descriptors that can be stored in @recv_fds, in which case @recv_fds
 * must point to sufficient memory. On return, @recv_fd_count contains the
 * number of file decriptors actually received, which does not exceeed the
 * original value of @recv_fd_count.
 */
int
tran_sock_msg_iovec(int sock, uint16_t msg_id,
                    enum vfio_user_command cmd,
                    struct iovec *iovecs, size_t nr_iovecs,
                    int *send_fds, size_t send_fd_count,
                    struct vfio_user_header *hdr,
                    void *recv_data, size_t recv_len,
                    int *recv_fds, size_t *recv_fd_count);

/*
 * Send and receive a message to the other end.  @hdr is filled with the reply
 * header if non-NULL.
 */
int
tran_sock_msg(int sock, uint16_t msg_id,
              enum vfio_user_command cmd,
              void *send_data, size_t send_len,
              struct vfio_user_header *hdr,
              void *recv_data, size_t recv_len);

/*
 * Same as tran_sock_msg excecpt that file descriptors can be received, see
 * tran_sock_msg_iovec for the semantics of @recv_fds and @recv_fd_count.
 */
int
tran_sock_msg_fds(int sock, uint16_t msg_id,
                  enum vfio_user_command cmd,
                  void *send_data, size_t send_len,
                  struct vfio_user_header *hdr,
                  void *recv_data, size_t recv_len,
                  int *recv_fds, size_t *recv_fd_count);


#endif /* LIB_VFIO_USER_TRAN_SOCK_H */

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
