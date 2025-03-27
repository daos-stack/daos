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
#  SERVICESLOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
#  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
#  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
#  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
#  DAMAGE.
#

from unittest import mock
from unittest.mock import patch
import mmap

from libvfio_user import *
import errno

#
# NB: this is currently very incomplete
#

ctx = None


def setup_function(function):
    global ctx, sock
    ctx = prepare_ctx_for_dma()
    assert ctx is not None
    sock = connect_client(ctx)


def teardown_function(function):
    global ctx, sock
    disconnect_client(ctx, sock)
    vfu_destroy_ctx(ctx)


def test_dma_region_too_big():
    global ctx, sock

    payload = vfio_user_dma_map(argsz=len(vfio_user_dma_map()),
        flags=(VFIO_USER_F_DMA_REGION_READ |
               VFIO_USER_F_DMA_REGION_WRITE),
        offset=0, addr=0x10000, size=MAX_DMA_SIZE + 4096)

    msg(ctx, sock, VFIO_USER_DMA_MAP, payload, expect=errno.ENOSPC)


def test_dma_region_too_many():
    global ctx, sock

    for i in range(1, MAX_DMA_REGIONS + 2):
        payload = vfio_user_dma_map(argsz=len(vfio_user_dma_map()),
            flags=(VFIO_USER_F_DMA_REGION_READ |
                   VFIO_USER_F_DMA_REGION_WRITE),
            offset=0, addr=0x1000 * i, size=4096)

        if i == MAX_DMA_REGIONS + 1:
            expect = errno.EINVAL
        else:
            expect = 0

        msg(ctx, sock, VFIO_USER_DMA_MAP, payload, expect=expect)


@patch('libvfio_user.quiesce_cb', side_effect=fail_with_errno(errno.EBUSY))
@patch('libvfio_user.dma_register')
def test_dma_map_busy(mock_dma_register, mock_quiesce):
    """
    Checks that during a DMA map operation where the device is initially busy
    quiescing, and then eventually quiesces, the DMA map operation succeeds.
    """

    global ctx, sock

    payload = vfio_user_dma_map(argsz=len(vfio_user_dma_map()),
        flags=(VFIO_USER_F_DMA_REGION_READ |
               VFIO_USER_F_DMA_REGION_WRITE),
        offset=0, addr=0x10000, size=0x1000)

    msg(ctx, sock, VFIO_USER_DMA_MAP, payload, rsp=False,
        busy=True)

    assert mock_dma_register.call_count == 0

    ret = vfu_device_quiesced(ctx, 0)
    assert ret == 0

    # check that DMA register callback got called
    dma_info = vfu_dma_info_t(iovec_t(iov_base=0x10000, iov_len=0x1000),
        None, iovec_t(None, 0), 0x1000, mmap.PROT_READ | mmap.PROT_WRITE)
    mock_dma_register.assert_called_once_with(ctx, dma_info)

    get_reply(sock)

    ret = vfu_run_ctx(ctx)
    assert ret == 0

    # the callback shouldn't be called again
    mock_dma_register.assert_called_once()

    # check that the DMA region has been added
    count, sgs = vfu_addr_to_sg(ctx, 0x10000, 0x1000)
    assert len(sgs) == 1
    sg = sgs[0]
    assert sg.dma_addr == 0x10000 and sg.region == 0 and sg.length == 0x1000 \
        and sg.offset == 0 and sg.writeable


# FIXME better move this test and the following to test_request_errors


# FIXME need the same test for (1) DMA unmap, (2) device reset, and
# (3) migration, where quiesce returns EBUSY but replying fails.
@patch('libvfio_user.reset_cb')
@patch('libvfio_user.quiesce_cb', return_value=0)
@patch('libvfio_user.dma_register')
def test_dma_map_reply_fail(mock_dma_register, mock_quiesce, mock_reset):
    """Tests mapping a DMA region where the quiesce callback returns 0 and
    replying fails."""

    global ctx, sock

    # The only chance we have to allow the message to be received but for the
    # reply to fail is in the DMA map callback, where the message has been
    # received but reply hasn't been sent yet.
    def side_effect(ctx, info):
        sock.close()

    mock_dma_register.side_effect = side_effect

    # Send a DMA map command.
    payload = vfio_user_dma_map(
        argsz=len(vfio_user_dma_map()),
        flags=(VFIO_USER_F_DMA_REGION_READ |
               VFIO_USER_F_DMA_REGION_WRITE),
        offset=0, addr=0x10000, size=0x1000)

    msg(ctx, sock, VFIO_USER_DMA_MAP, payload, rsp=False)

    vfu_run_ctx(ctx, errno.ENOTCONN)

    # TODO not sure whether the following is worth it?
    try:
        get_reply(sock)
    except OSError as e:
        assert e.errno == errno.EBADF
    else:
        assert False

    # 1st call is for adding the DMA region, 2nd call is for the reset
    mock_quiesce.assert_has_calls([mock.call(ctx)] * 2)
    mock_reset.assert_called_once_with(ctx, VFU_RESET_LOST_CONN)

    # no need to check that DMA region wasn't added as the context is reset


# FIXME need the same test for (1) DMA unmap, (2) device reset, and
# (3) migration, where quiesce returns EBUSY but replying fails.
@patch('libvfio_user.reset_cb')
@patch('libvfio_user.quiesce_cb', side_effect=fail_with_errno(errno.EBUSY))
@patch('libvfio_user.dma_register')
def test_dma_map_busy_reply_fail(mock_dma_register, mock_quiesce, mock_reset):
    """
    Tests mapping a DMA region where the quiesce callback returns EBUSY and
    replying fails.
    """

    global ctx, sock

    # Send a DMA map command.
    payload = vfio_user_dma_map(
        argsz=len(vfio_user_dma_map()),
        flags=(VFIO_USER_F_DMA_REGION_READ |
               VFIO_USER_F_DMA_REGION_WRITE),
        offset=0, addr=0x10000, size=0x1000)

    msg(ctx, sock, VFIO_USER_DMA_MAP, payload, rsp=False,
        busy=True)

    mock_quiesce.assert_called_once_with(ctx)

    # pretend there's a connection failure while the device is still quiescing
    sock.close()

    mock_dma_register.assert_not_called()
    mock_reset.assert_not_called()

    # device quiesces
    ret = vfu_device_quiesced(ctx, 0)
    assert ret == 0

    dma_info = vfu_dma_info_t(iovec_t(iov_base=0x10000, iov_len=0x1000),
        None, iovec_t(None, 0), 0x1000, mmap.PROT_READ | mmap.PROT_WRITE)
    mock_dma_register.assert_called_once_with(ctx, dma_info)

    # device reset callback should be called (by do_reply)
    mock_reset.assert_called_once_with(ctx, True)

    vfu_run_ctx(ctx, errno.ENOTCONN)

    # callbacks shouldn't be called further
    mock_quiesce.assert_called_once()
    mock_dma_register.assert_called_once()
    mock_reset.assert_called_once()

    # check that the DMA region was NOT added
    count, sgs = vfu_addr_to_sg(ctx, 0x10000, 0x1000)
    assert count == -1
    assert c.get_errno() == errno.ENOENT


# ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: #
