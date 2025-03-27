#
# Copyright (c) 2021 Nutanix Inc. All rights reserved.
#
# Authors: Swapnil Ingle <swapnil.ingle@nutanix.com>
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

import ctypes
import errno
from libvfio_user import *
import tempfile

ctx = None


def test_dma_sg_size():
    size = dma_sg_size()
    assert size == len(dma_sg_t())


def test_map_sg_with_invalid_region():
    global ctx

    ctx = prepare_ctx_for_dma()
    assert ctx is not None

    sg = dma_sg_t()
    iovec = iovec_t()
    ret = vfu_map_sg(ctx, sg, iovec)
    assert ret == -1
    assert ctypes.get_errno() == errno.EINVAL


def test_map_sg_without_fd():
    sock = connect_client(ctx)

    payload = vfio_user_dma_map(argsz=len(vfio_user_dma_map()),
        flags=(VFIO_USER_F_DMA_REGION_READ |
               VFIO_USER_F_DMA_REGION_WRITE),
        offset=0, addr=0x1000, size=4096)
    msg(ctx, sock, VFIO_USER_DMA_MAP, payload)
    sg = dma_sg_t()
    iovec = iovec_t()
    sg.region = 0
    ret = vfu_map_sg(ctx, sg, iovec)
    assert ret == -1
    assert ctypes.get_errno() == errno.EFAULT

    disconnect_client(ctx, sock)


def test_map_multiple_sge():
    sock = connect_client(ctx)
    regions = 4
    f = tempfile.TemporaryFile()
    f.truncate(0x1000 * regions)
    for i in range(1, regions):
        payload = vfio_user_dma_map(argsz=len(vfio_user_dma_map()),
            flags=(VFIO_USER_F_DMA_REGION_READ |
                   VFIO_USER_F_DMA_REGION_WRITE),
            offset=0, addr=0x1000 * i, size=4096)
        msg(ctx, sock, VFIO_USER_DMA_MAP, payload, fds=[f.fileno()])

    ret, sg = vfu_addr_to_sg(ctx, dma_addr=0x1000, length=4096 * 3, max_sg=3,
                             prot=mmap.PROT_READ)
    assert ret == 3

    iovec = (iovec_t * 3)()
    ret = vfu_map_sg(ctx, sg, iovec, cnt=3)
    assert ret == 0
    assert iovec[0].iov_len == 4096
    assert iovec[1].iov_len == 4096
    assert iovec[2].iov_len == 4096

    disconnect_client(ctx, sock)


def test_unmap_sg():
    sock = connect_client(ctx)
    regions = 4
    f = tempfile.TemporaryFile()
    f.truncate(0x1000 * regions)
    for i in range(1, regions):
        payload = vfio_user_dma_map(argsz=len(vfio_user_dma_map()),
            flags=(VFIO_USER_F_DMA_REGION_READ |
                   VFIO_USER_F_DMA_REGION_WRITE),
            offset=0, addr=0x1000 * i, size=4096)
        msg(ctx, sock, VFIO_USER_DMA_MAP, payload, fds=[f.fileno()])

    ret, sg = vfu_addr_to_sg(ctx, dma_addr=0x1000, length=4096 * 3, max_sg=3,
                             prot=mmap.PROT_READ)
    assert ret == 3

    iovec = (iovec_t * 3)()
    ret = vfu_map_sg(ctx, sg, iovec, cnt=3)
    assert ret == 0
    vfu_unmap_sg(ctx, sg, iovec, cnt=3)

    disconnect_client(ctx, sock)


def test_map_unmap_sg_cleanup():
    vfu_destroy_ctx(ctx)

# ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab:
