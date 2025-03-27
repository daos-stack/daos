#
# Copyright (c) 2021 Nutanix Inc. All rights reserved.
#
# Authors: John Levon <john.levon@nutanix.com>
#          Swapnil Ingle <swapnil.ingle@nutanix.com>
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

import errno
from unittest.mock import patch
from libvfio_user import *

ctx = None
sock = None


def setup_function(function):
    global ctx, sock
    ctx = prepare_ctx_for_dma()
    assert ctx is not None

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    sock = connect_client(ctx)


def teardown_function(function):
    global ctx, sock
    disconnect_client(ctx, sock)
    vfu_destroy_ctx(ctx)


def setup_dma_regions(dma_regions=[(0x0, 0x1000)]):
    global ctx, sock
    for dma_region in dma_regions:
        payload = struct.pack("II", 0, 0)
        payload = vfio_user_dma_map(argsz=len(vfio_user_dma_map()),
            flags=(VFIO_USER_F_DMA_REGION_READ |
                   VFIO_USER_F_DMA_REGION_WRITE),
            offset=0, addr=dma_region[0], size=dma_region[1])
        msg(ctx, sock, VFIO_USER_DMA_MAP, payload)


def test_dma_unmap_short_write():
    payload = struct.pack("II", 0, 0)
    msg(ctx, sock, VFIO_USER_DMA_UNMAP, payload,
        expect=errno.EINVAL)


def test_dma_unmap_bad_argsz():

    payload = vfio_user_dma_unmap(argsz=8, flags=0, addr=0x1000, size=4096)
    msg(ctx, sock, VFIO_USER_DMA_UNMAP, payload,
        expect=errno.EINVAL)


def test_dma_unmap_bad_argsz2():

    payload = vfio_user_dma_unmap(argsz=SERVER_MAX_DATA_XFER_SIZE + 8, flags=0,
                                  addr=0x1000, size=4096)
    msg(ctx, sock, VFIO_USER_DMA_UNMAP, payload,
        expect=errno.EINVAL)


def test_dma_unmap_dirty_bad_argsz():

    argsz = len(vfio_user_dma_unmap()) + len(vfio_user_bitmap())
    unmap = vfio_user_dma_unmap(argsz=argsz,
        flags=VFIO_DMA_UNMAP_FLAG_GET_DIRTY_BITMAP, addr=0x1000, size=4096)
    bitmap = vfio_user_bitmap(pgsize=4096, size=(UINT64_MAX - argsz) + 8)
    payload = bytes(unmap) + bytes(bitmap)

    msg(ctx, sock, VFIO_USER_DMA_UNMAP, payload,
        expect=errno.EINVAL)


def test_dma_unmap_dirty_not_tracking():

    setup_dma_regions([(0x1000, 4096)])
    argsz = len(vfio_user_dma_unmap()) + len(vfio_user_bitmap()) + 8
    unmap = vfio_user_dma_unmap(argsz=argsz,
        flags=VFIO_DMA_UNMAP_FLAG_GET_DIRTY_BITMAP, addr=0x1000, size=4096)
    bitmap = vfio_user_bitmap(pgsize=4096, size=8)
    payload = bytes(unmap) + bytes(bitmap) + bytes(8)

    msg(ctx, sock, VFIO_USER_DMA_UNMAP, payload,
        expect=errno.EINVAL)


def test_dma_unmap_dirty_not_mapped():

    setup_dma_regions([(0x1000, 4096)])
    vfu_setup_device_migration_callbacks(ctx, offset=0x1000)
    payload = vfio_user_dirty_pages(argsz=len(vfio_user_dirty_pages()),
                                    flags=VFIO_IOMMU_DIRTY_PAGES_FLAG_START)

    msg(ctx, sock, VFIO_USER_DIRTY_PAGES, payload)

    argsz = len(vfio_user_dma_unmap()) + len(vfio_user_bitmap()) + 8
    unmap = vfio_user_dma_unmap(argsz=argsz,
        flags=VFIO_DMA_UNMAP_FLAG_GET_DIRTY_BITMAP, addr=0x1000, size=4096)
    bitmap = vfio_user_bitmap(pgsize=4096, size=8)
    payload = bytes(unmap) + bytes(bitmap) + bytes(8)

    msg(ctx, sock, VFIO_USER_DMA_UNMAP, payload,
        expect=errno.EINVAL)


def test_dma_unmap_invalid_flags():

    setup_dma_regions()
    payload = vfio_user_dma_unmap(argsz=len(vfio_user_dma_unmap()),
                                  flags=0x4, addr=0x1000, size=4096)
    msg(ctx, sock, VFIO_USER_DMA_UNMAP, payload,
        expect=errno.EINVAL)


def test_dma_unmap():

    setup_dma_regions()
    payload = vfio_user_dma_unmap(argsz=len(vfio_user_dma_unmap()),
                                  flags=0, addr=0x0, size=0x1000)
    msg(ctx, sock, VFIO_USER_DMA_UNMAP, payload)


def test_dma_unmap_invalid_addr():

    setup_dma_regions()
    payload = vfio_user_dma_unmap(argsz=len(vfio_user_dma_unmap()),
                                  addr=0x10000, size=4096)

    msg(ctx, sock, VFIO_USER_DMA_UNMAP, payload,
        expect=errno.ENOENT)


@patch('libvfio_user.quiesce_cb')
def test_dma_unmap_async(mock_quiesce):

    setup_dma_regions()
    mock_quiesce.side_effect = fail_with_errno(errno.EBUSY)
    payload = vfio_user_dma_unmap(argsz=len(vfio_user_dma_unmap()),
                                  flags=0, addr=0x0, size=0x1000)
    msg(ctx, sock, VFIO_USER_DMA_UNMAP, payload, rsp=False,
        busy=True)

    ret = vfu_device_quiesced(ctx, 0)
    assert ret == 0

    get_reply(sock)

    ret = vfu_run_ctx(ctx)
    assert ret == 0


def test_dma_unmap_all():

    setup_dma_regions((0x1000*i, 0x1000) for i in range(MAX_DMA_REGIONS))
    payload = vfio_user_dma_unmap(argsz=len(vfio_user_dma_unmap()),
        flags=VFIO_DMA_UNMAP_FLAG_ALL, addr=0, size=0)
    msg(ctx, sock, VFIO_USER_DMA_UNMAP, payload)


def test_dma_unmap_all_invalid_addr():

    payload = vfio_user_dma_unmap(argsz=len(vfio_user_dma_unmap()),
        flags=VFIO_DMA_UNMAP_FLAG_ALL, addr=0x10000, size=4096)

    msg(ctx, sock, VFIO_USER_DMA_UNMAP, payload,
        expect=errno.EINVAL)


def test_dma_unmap_all_invalid_flags():

    payload = vfio_user_dma_unmap(argsz=len(vfio_user_dma_unmap()),
        flags=(VFIO_DMA_UNMAP_FLAG_ALL | VFIO_DMA_UNMAP_FLAG_GET_DIRTY_BITMAP),
               addr=0, size=0)

    msg(ctx, sock, VFIO_USER_DMA_UNMAP, payload,
        expect=errno.EINVAL)

# FIXME need to add unit tests that test errors in get_request_header,
# do_reply, vfu_dma_transfer

# ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: #
