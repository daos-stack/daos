#
# Copyright (c) 2021 Nutanix Inc. All rights reserved.
#
# Authors: Thanos Makatos <thanos.makatos@nutanix.com>
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

from libvfio_user import *
import errno
from unittest import mock
from unittest.mock import patch


ctx = None


def setup_function(function):
    global ctx, sock
    ctx = prepare_ctx_for_dma(migration_callbacks=True)
    assert ctx is not None
    sock = connect_client(ctx)


def teardown_function(function):
    global ctx
    vfu_destroy_ctx(ctx)


@patch('libvfio_user.quiesce_cb')
def test_device_quiesced_no_quiesce_requested(mock_quiesce):
    """
    Checks that vfu_device_quiesce returns an error if called when there is
    no pending quiesce operation.
    """

    global ctx
    ret = vfu_device_quiesced(ctx, 0)
    assert ret == -1
    assert c.get_errno() == errno.EINVAL
    assert mock_quiesce.call_count == 0


@patch('libvfio_user.quiesce_cb', side_effect=fail_with_errno(errno.ENOTTY))
def test_device_quiesce_error(mock_quiesce):
    """
    Checks that if the device quiesce callback fails then the operation
    that requested it also fails with the same error.
    """

    global ctx, sock

    payload = vfio_user_dma_map(argsz=len(vfio_user_dma_map()),
        flags=(VFIO_USER_F_DMA_REGION_READ |
               VFIO_USER_F_DMA_REGION_WRITE),
        offset=0, addr=0x10000, size=0x1000)

    msg(ctx, sock, VFIO_USER_DMA_MAP, payload, errno.ENOTTY)


@patch('libvfio_user.dma_register')
@patch('libvfio_user.quiesce_cb', side_effect=fail_with_errno(errno.EBUSY))
def test_device_quiesce_error_after_busy(mock_quiesce, mock_dma_register):
    """
    Checks that the device fails to quiesce after it was busy quiescing.
    """

    global ctx, sock

    payload = vfio_user_dma_map(argsz=len(vfio_user_dma_map()),
        flags=(VFIO_USER_F_DMA_REGION_READ |
               VFIO_USER_F_DMA_REGION_WRITE),
        offset=0, addr=0x10000, size=0x1000)

    msg(ctx, sock, VFIO_USER_DMA_MAP, payload, rsp=False,
        busy=True)

    ret = vfu_device_quiesced(ctx, errno.ENOTTY)
    assert ret == 0

    mock_dma_register.assert_not_called()

    # check that the DMA region was NOT added
    count, sgs = vfu_addr_to_sg(ctx, 0x10000, 0x1000)
    assert count == -1
    assert c.get_errno() == errno.ENOENT


# DMA map/unmap, migration device state transition, and reset callbacks
# have the same function signature in Python
def _side_effect(ctx, _):
    count, sgs = vfu_addr_to_sg(ctx, 0x10000, 0x1000)
    assert count == 1
    sg = sgs[0]
    assert sg.dma_addr == 0x10000 and sg.region == 0 \
        and sg.length == 0x1000 and sg.offset == 0 and sg.writeable
    iovec = iovec_t()
    ret = vfu_map_sg(ctx, sg, iovec)
    assert ret == 0, "%s" % c.get_errno()
    assert iovec.iov_base != 0
    assert iovec.iov_len == 0x1000
    assert ret == 0
    vfu_unmap_sg(ctx, sg, iovec)
    return 0


def _map_dma_region(ctx, sock, busy=False):
    f = tempfile.TemporaryFile()
    f.truncate(0x1000)
    map_payload = vfio_user_dma_map(argsz=len(vfio_user_dma_map()),
        flags=(VFIO_USER_F_DMA_REGION_READ | VFIO_USER_F_DMA_REGION_WRITE),
        offset=0, addr=0x10000, size=0x1000)
    msg(ctx, sock, VFIO_USER_DMA_MAP, map_payload, busy=busy, fds=[f.fileno()])


def _unmap_dma_region(ctx, sock, busy=False):
    unmap_payload = vfio_user_dma_unmap(argsz=len(vfio_user_dma_unmap()),
                                  addr=0x10000, size=0x1000)
    msg(ctx, sock, VFIO_USER_DMA_UNMAP, unmap_payload, busy=busy)


@patch('libvfio_user.dma_register', side_effect=_side_effect)
@patch('libvfio_user.quiesce_cb')
def test_allowed_funcs_in_quiesced_dma_register(mock_quiesce,
                                                mock_dma_register):

    global ctx, sock

    # FIXME assert quiesce callback is called
    _map_dma_region(ctx, sock)
    # FIXME it's difficult to check that mock_dma_register has been called with
    # the expected DMA info because we don't know the vaddr and the mapping
    # (2nd and 3rd arguments of vfu_dma_info_t) as they're values returned from
    # mmap(0) so they can't be predicted. Using mock.ANY in their place fails
    # with "TypeError: cannot be converted to pointer". In any case this is
    # tested by other unit tests.
    mock_dma_register.assert_called_once_with(ctx, mock.ANY)


@patch('libvfio_user.dma_unregister', side_effect=_side_effect)
@patch('libvfio_user.quiesce_cb')
def test_allowed_funcs_in_quiesced_dma_unregister(mock_quiesce,
                                                  mock_dma_unregister):

    global ctx, sock
    _map_dma_region(ctx, sock)
    _unmap_dma_region(ctx, sock)
    mock_dma_unregister.assert_called_once_with(ctx, mock.ANY)


@patch('libvfio_user.dma_register', side_effect=_side_effect)
@patch('libvfio_user.quiesce_cb', side_effect=fail_with_errno(errno.EBUSY))
def test_allowed_funcs_in_quiesced_dma_register_busy(mock_quiesce,
                                                     mock_dma_register):

    global ctx, sock
    _map_dma_region(ctx, sock, errno.EBUSY)
    ret = vfu_device_quiesced(ctx, 0)
    assert ret == 0
    mock_dma_register.assert_called_once_with(ctx, mock.ANY)


@patch('libvfio_user.dma_unregister', side_effect=_side_effect)
@patch('libvfio_user.quiesce_cb')
def test_allowed_funcs_in_quiesced_dma_unregister_busy(mock_quiesce,
                                                       mock_dma_unregister):

    global ctx, sock
    _map_dma_region(ctx, sock)
    mock_quiesce.side_effect = fail_with_errno(errno.EBUSY)
    _unmap_dma_region(ctx, sock, busy=True)
    ret = vfu_device_quiesced(ctx, 0)
    assert ret == 0
    mock_dma_unregister.assert_called_once_with(ctx, mock.ANY)


@patch('libvfio_user.migr_trans_cb', side_effect=_side_effect)
@patch('libvfio_user.quiesce_cb')
def test_allowed_funcs_in_quiesed_migration(mock_quiesce,
                                            mock_trans):

    global ctx, sock
    _map_dma_region(ctx, sock)
    data = VFIO_DEVICE_STATE_V1_SAVING.to_bytes(c.sizeof(c.c_int), 'little')
    write_region(ctx, sock, VFU_PCI_DEV_MIGR_REGION_IDX, offset=0,
                 count=len(data), data=data)
    mock_trans.assert_called_once_with(ctx, VFIO_DEVICE_STATE_V1_SAVING)


@patch('libvfio_user.migr_trans_cb', side_effect=_side_effect)
@patch('libvfio_user.quiesce_cb')
def test_allowed_funcs_in_quiesed_migration_busy(mock_quiesce,
                                                 mock_trans):

    global ctx, sock
    _map_dma_region(ctx, sock)
    mock_quiesce.side_effect = fail_with_errno(errno.EBUSY)
    data = VFIO_DEVICE_STATE_V1_STOP.to_bytes(c.sizeof(c.c_int), 'little')
    write_region(ctx, sock, VFU_PCI_DEV_MIGR_REGION_IDX, offset=0,
                 count=len(data), data=data, rsp=False,
                 busy=True)
    ret = vfu_device_quiesced(ctx, 0)
    assert ret == 0
    mock_trans.assert_called_once_with(ctx, VFIO_DEVICE_STATE_V1_STOP)


@patch('libvfio_user.reset_cb', side_effect=_side_effect)
@patch('libvfio_user.quiesce_cb')
def test_allowed_funcs_in_quiesced_reset(mock_quiesce, mock_reset):
    global ctx, sock
    _map_dma_region(ctx, sock)
    msg(ctx, sock, VFIO_USER_DEVICE_RESET)
    mock_reset.assert_called_once_with(ctx, VFU_RESET_DEVICE)


@patch('libvfio_user.reset_cb', side_effect=_side_effect)
@patch('libvfio_user.quiesce_cb')
def test_allowed_funcs_in_quiesced_reset_busy(mock_quiesce, mock_reset):
    global ctx, sock
    _map_dma_region(ctx, sock)
    mock_quiesce.side_effect = fail_with_errno(errno.EBUSY)
    msg(ctx, sock, VFIO_USER_DEVICE_RESET, rsp=False,
        busy=True)
    ret = vfu_device_quiesced(ctx, 0)
    assert ret == 0
    mock_reset.assert_called_once_with(ctx, VFU_RESET_DEVICE)


# ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: #
