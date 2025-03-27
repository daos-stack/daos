#
# Copyright (c) 2021 Nutanix Inc. All rights reserved.
#
# Authors: John Levon <john.levon@nutanix.com>
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are met:
#      * Redistributions of source code must retain the above copyright
#        notice, this list of conditions and the following disclaimer.
#      * Redistributions in binary form must reproduce the above copyright
#        notice, this list of conditions and the following disclaimer in the
#        documentation and/or other materials provided with the distribution.
#      * Neither the name of Nutanix nor the names of its contributors may be
#        used to endorse or promote products derived from this software without
#        specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
#  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
#  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
#  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
#  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
#  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
#  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
#  DAMAGE.
#

from unittest.mock import patch
from libvfio_user import *
import errno
import os

ctx = None
sock = None
argsz = len(vfio_irq_set())


def setup_function(function):
    global ctx, sock

    ctx = vfu_create_ctx(flags=LIBVFIO_USER_FLAG_ATTACH_NB)
    assert ctx is not None

    ret = vfu_pci_init(ctx)
    assert ret == 0

    ret = vfu_setup_device_nr_irqs(ctx, VFU_DEV_MSIX_IRQ, 2048)
    assert ret == 0

    vfu_setup_device_quiesce_cb(ctx)

    ret = vfu_setup_device_reset_cb(ctx)
    assert ret == 0

    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_MIGR_REGION_IDX, size=0x2000,
                           flags=VFU_REGION_FLAG_RW)
    assert ret == 0

    ret = vfu_setup_device_migration_callbacks(ctx, offset=0x4000)
    assert ret == 0

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    sock = connect_client(ctx)


def teardown_function(function):
    global ctx
    vfu_destroy_ctx(ctx)


def test_too_small():
    # struct vfio_user_header
    hdr = struct.pack("HHIII", 0xbad1, VFIO_USER_DEVICE_SET_IRQS,
                      SIZEOF_VFIO_USER_HEADER - 1, VFIO_USER_F_TYPE_COMMAND, 0)

    sock.send(hdr)
    vfu_run_ctx(ctx)
    get_reply(sock, expect=errno.EINVAL)


def test_too_large():
    # struct vfio_user_header
    hdr = struct.pack("HHIII", 0xbad1, VFIO_USER_DEVICE_SET_IRQS,
                      SERVER_MAX_MSG_SIZE + 1, VFIO_USER_F_TYPE_COMMAND, 0)

    sock.send(hdr)
    vfu_run_ctx(ctx)
    get_reply(sock, expect=errno.EINVAL)


def test_unsolicited_reply():
    # struct vfio_user_header
    hdr = struct.pack("HHIII", 0xbad2, VFIO_USER_DEVICE_SET_IRQS,
                      SIZEOF_VFIO_USER_HEADER, VFIO_USER_F_TYPE_REPLY, 0)

    sock.send(hdr)
    vfu_run_ctx(ctx)
    get_reply(sock, expect=errno.EINVAL)


def test_bad_command():
    hdr = vfio_user_header(VFIO_USER_MAX, size=1)

    sock.send(hdr + b'\0')
    vfu_run_ctx(ctx)
    get_reply(sock, expect=errno.EINVAL)


def test_no_payload():
    hdr = vfio_user_header(VFIO_USER_DEVICE_SET_IRQS, size=0)
    sock.send(hdr)
    vfu_run_ctx(ctx)
    get_reply(sock, expect=errno.EINVAL)


def test_bad_request_closes_fds():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_TRIGGER |
                           VFIO_IRQ_SET_DATA_BOOL, index=VFU_DEV_MSIX_IRQ,
                           start=0, count=1)

    fd1 = eventfd()
    fd2 = eventfd()

    hdr = vfio_user_header(VFIO_USER_DEVICE_SET_IRQS, size=len(payload))
    sock.sendmsg([hdr + payload], [(socket.SOL_SOCKET, socket.SCM_RIGHTS,
                 struct.pack("II", fd1, fd2))])
    vfu_run_ctx(ctx)
    get_reply(sock, expect=errno.EINVAL)

    #
    # It's a little cheesy, but this is just ensuring no fd's remain open past
    # the one we just allocated; i.e. free_msg() freed the fds it got.
    #
    test_fd = eventfd()
    assert test_fd == fd2 + 1
    os.close(test_fd)

    os.close(fd1)
    os.close(fd2)


@patch('libvfio_user.reset_cb')
@patch('libvfio_user.quiesce_cb', return_value=0)
def test_disconnected_socket(mock_quiesce, mock_reset):
    """Tests that calling vfu_run_ctx on a disconnected socket results in
    resetting the context and returning ENOTCONN."""

    global ctx, sock
    sock.close()

    vfu_run_ctx(ctx, errno.ENOTCONN)

    # quiesce callback gets called during reset
    # FIXME how can we ensure that quiesce is called before reset?
    mock_quiesce.assert_called_with(ctx)
    mock_reset.assert_called_with(ctx, VFU_RESET_LOST_CONN)


@patch('libvfio_user.quiesce_cb', side_effect=fail_with_errno(errno.EBUSY))
def test_disconnected_socket_quiesce_busy(mock_quiesce):
    """Tests that calling vfu_run_ctx on a disconnected socket results in
    resetting the context which returns EBUSY."""

    global ctx, sock
    sock.close()

    vfu_run_ctx(ctx, errno.EBUSY)

    # quiesce callback must be called during reset
    mock_quiesce.assert_called_once_with(ctx)

    # device hasn't finished quiescing
    for _ in range(0, 3):
        vfu_run_ctx(ctx, errno.EBUSY)

    # device quiesced
    ret = vfu_device_quiesced(ctx, 0)
    assert ret == 0

    vfu_run_ctx(ctx, errno.ENOTCONN)

    # no further calls to the quiesce callback should have been made
    mock_quiesce.assert_called_once_with(ctx)


@patch('libvfio_user.reset_cb')
@patch('libvfio_user.quiesce_cb', side_effect=fail_with_errno(errno.EBUSY))
@patch('libvfio_user.migr_get_pending_bytes_cb')
def test_reply_fail_quiesce_busy(mock_get_pending_bytes, mock_quiesce,
                                 mock_reset):
    """Tests failing to reply and the quiesce callback returning EBUSY."""

    global ctx, sock

    def get_pending_bytes_side_effect(ctx):
        sock.close()
        return 0
    mock_get_pending_bytes.side_effect = get_pending_bytes_side_effect

    # read the get_pending_bytes register, it should close the socket causing
    # the reply to fail
    read_region(ctx, sock, VFU_PCI_DEV_MIGR_REGION_IDX,
                vfio_user_migration_info.pending_bytes.offset,
                vfio_user_migration_info.pending_bytes.size, rsp=False,
                busy=True)

    # vfu_run_ctx will try to reset the context and to do that it needs to
    # quiesce the device first
    mock_quiesce.assert_called_once_with(ctx)

    # vfu_run_ctx will be returning EBUSY and nothing should have happened
    # until the device quiesces
    for _ in range(0, 3):
        vfu_run_ctx(ctx, errno.EBUSY)
    mock_quiesce.assert_called_once_with(ctx)
    mock_reset.assert_not_called()

    ret = vfu_device_quiesced(ctx, 0)
    assert ret == 0

    # the device quiesced, reset should should happen now
    mock_quiesce.assert_called_once_with(ctx)
    mock_reset.assert_called_once_with(ctx, VFU_RESET_LOST_CONN)

    try:
        get_reply(sock)
    except OSError as e:
        assert e.errno == errno.EBADF
    else:
        assert False

    vfu_run_ctx(ctx, errno.ENOTCONN)


# ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: #
