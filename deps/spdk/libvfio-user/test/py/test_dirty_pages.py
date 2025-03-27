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

from libvfio_user import *
import errno
import mmap
import tempfile

ctx = None


@vfu_dma_register_cb_t
def dma_register(ctx, info):
    return 0


@vfu_dma_unregister_cb_t
def dma_unregister(ctx, info):
    return 0


def test_dirty_pages_setup():
    global ctx, sock

    ctx = vfu_create_ctx(flags=LIBVFIO_USER_FLAG_ATTACH_NB)
    assert ctx is not None

    ret = vfu_pci_init(ctx)
    assert ret == 0

    ret = vfu_setup_device_dma(ctx, dma_register, dma_unregister)
    assert ret == 0

    f = tempfile.TemporaryFile()
    f.truncate(0x2000)

    mmap_areas = [(0x1000, 0x1000)]

    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_MIGR_REGION_IDX, size=0x2000,
                           flags=VFU_REGION_FLAG_RW, mmap_areas=mmap_areas,
                           fd=f.fileno())
    assert ret == 0

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    sock = connect_client(ctx)

    f = tempfile.TemporaryFile()
    f.truncate(0x10000)

    payload = vfio_user_dma_map(argsz=len(vfio_user_dma_map()),
        flags=(VFIO_USER_F_DMA_REGION_READ | VFIO_USER_F_DMA_REGION_WRITE),
        offset=0, addr=0x10000, size=0x10000)

    msg(ctx, sock, VFIO_USER_DMA_MAP, payload, fds=[f.fileno()])

    payload = vfio_user_dma_map(argsz=len(vfio_user_dma_map()),
        flags=(VFIO_USER_F_DMA_REGION_READ | VFIO_USER_F_DMA_REGION_WRITE),
        offset=0, addr=0x20000, size=0x10000)

    msg(ctx, sock, VFIO_USER_DMA_MAP, payload)


def test_dirty_pages_short_write():
    payload = struct.pack("I", 8)

    msg(ctx, sock, VFIO_USER_DIRTY_PAGES, payload,
        expect=errno.EINVAL)


def test_dirty_pages_bad_argsz():
    payload = vfio_user_dirty_pages(argsz=4,
        flags=VFIO_IOMMU_DIRTY_PAGES_FLAG_START)

    msg(ctx, sock, VFIO_USER_DIRTY_PAGES, payload,
        expect=errno.EINVAL)


def test_dirty_pages_start_no_migration():
    payload = vfio_user_dirty_pages(argsz=len(vfio_user_dirty_pages()),
        flags=VFIO_IOMMU_DIRTY_PAGES_FLAG_START)

    msg(ctx, sock, VFIO_USER_DIRTY_PAGES, payload,
        expect=errno.ENOTSUP)


def test_dirty_pages_start_bad_flags():
    #
    # This is a little cheeky, after vfu_realize_ctx(), but it works at the
    # moment.
    #
    vfu_setup_device_migration_callbacks(ctx, offset=0x1000)

    payload = vfio_user_dirty_pages(argsz=len(vfio_user_dirty_pages()),
        flags=(VFIO_IOMMU_DIRTY_PAGES_FLAG_START |
               VFIO_IOMMU_DIRTY_PAGES_FLAG_STOP))

    msg(ctx, sock, VFIO_USER_DIRTY_PAGES, payload,
        expect=errno.EINVAL)

    payload = vfio_user_dirty_pages(argsz=len(vfio_user_dirty_pages()),
        flags=(VFIO_IOMMU_DIRTY_PAGES_FLAG_START |
               VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP))

    msg(ctx, sock, VFIO_USER_DIRTY_PAGES, payload,
        expect=errno.EINVAL)


def start_logging():
    payload = vfio_user_dirty_pages(argsz=len(vfio_user_dirty_pages()),
                                    flags=VFIO_IOMMU_DIRTY_PAGES_FLAG_START)

    msg(ctx, sock, VFIO_USER_DIRTY_PAGES, payload)


def test_dirty_pages_start():
    start_logging()
    # should be idempotent
    start_logging()


def test_dirty_pages_get_short_read():
    payload = vfio_user_dirty_pages(argsz=len(vfio_user_dirty_pages()),
        flags=VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP)

    msg(ctx, sock, VFIO_USER_DIRTY_PAGES, payload,
        expect=errno.EINVAL)


#
# This should in fact work; update when it does.
#
def test_dirty_pages_get_sub_range():
    argsz = len(vfio_user_dirty_pages()) + len(vfio_user_bitmap_range()) + 8
    dirty_pages = vfio_user_dirty_pages(argsz=argsz,
        flags=VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP)
    bitmap = vfio_user_bitmap(pgsize=0x1000, size=8)
    br = vfio_user_bitmap_range(iova=0x11000, size=0x1000, bitmap=bitmap)

    payload = bytes(dirty_pages) + bytes(br)

    msg(ctx, sock, VFIO_USER_DIRTY_PAGES, payload,
        expect=errno.ENOTSUP)


def test_dirty_pages_get_bad_page_size():
    argsz = len(vfio_user_dirty_pages()) + len(vfio_user_bitmap_range()) + 8
    dirty_pages = vfio_user_dirty_pages(argsz=argsz,
        flags=VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP)
    bitmap = vfio_user_bitmap(pgsize=0x2000, size=8)
    br = vfio_user_bitmap_range(iova=0x10000, size=0x10000, bitmap=bitmap)

    payload = bytes(dirty_pages) + bytes(br)

    msg(ctx, sock, VFIO_USER_DIRTY_PAGES, payload,
        expect=errno.EINVAL)


def test_dirty_pages_get_bad_bitmap_size():
    argsz = len(vfio_user_dirty_pages()) + len(vfio_user_bitmap_range()) + 8
    dirty_pages = vfio_user_dirty_pages(argsz=argsz,
        flags=VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP)
    bitmap = vfio_user_bitmap(pgsize=0x1000, size=1)
    br = vfio_user_bitmap_range(iova=0x10000, size=0x10000, bitmap=bitmap)

    payload = bytes(dirty_pages) + bytes(br)

    msg(ctx, sock, VFIO_USER_DIRTY_PAGES, payload,
        expect=errno.EINVAL)


def test_dirty_pages_get_bad_argsz():
    dirty_pages = vfio_user_dirty_pages(argsz=SERVER_MAX_DATA_XFER_SIZE + 8,
        flags=VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP)
    bitmap = vfio_user_bitmap(pgsize=0x1000,
                              size=SERVER_MAX_DATA_XFER_SIZE + 8)
    br = vfio_user_bitmap_range(iova=0x10000, size=0x10000, bitmap=bitmap)

    payload = bytes(dirty_pages) + bytes(br)

    msg(ctx, sock, VFIO_USER_DIRTY_PAGES, payload,
        expect=errno.EINVAL)


def test_dirty_pages_get_short_reply():
    dirty_pages = vfio_user_dirty_pages(argsz=len(vfio_user_dirty_pages()),
        flags=VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP)
    bitmap = vfio_user_bitmap(pgsize=0x1000, size=8)
    br = vfio_user_bitmap_range(iova=0x10000, size=0x10000, bitmap=bitmap)

    payload = bytes(dirty_pages) + bytes(br)

    result = msg(ctx, sock, VFIO_USER_DIRTY_PAGES, payload)

    assert len(result) == len(vfio_user_dirty_pages())

    dirty_pages, _ = vfio_user_dirty_pages.pop_from_buffer(result)

    argsz = len(vfio_user_dirty_pages()) + len(vfio_user_bitmap_range()) + 8

    assert dirty_pages.argsz == argsz
    assert dirty_pages.flags == VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP


def test_get_dirty_page_bitmap_unmapped():
    argsz = len(vfio_user_dirty_pages()) + len(vfio_user_bitmap_range()) + 8

    dirty_pages = vfio_user_dirty_pages(argsz=argsz,
        flags=VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP)
    bitmap = vfio_user_bitmap(pgsize=0x1000, size=8)
    br = vfio_user_bitmap_range(iova=0x20000, size=0x10000, bitmap=bitmap)

    payload = bytes(dirty_pages) + bytes(br)

    msg(ctx, sock, VFIO_USER_DIRTY_PAGES, payload,
        expect=errno.EINVAL)


def test_dirty_pages_get_unmodified():
    argsz = len(vfio_user_dirty_pages()) + len(vfio_user_bitmap_range()) + 8

    dirty_pages = vfio_user_dirty_pages(argsz=argsz,
        flags=VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP)
    bitmap = vfio_user_bitmap(pgsize=0x1000, size=8)
    br = vfio_user_bitmap_range(iova=0x10000, size=0x10000, bitmap=bitmap)

    payload = bytes(dirty_pages) + bytes(br)

    result = msg(ctx, sock, VFIO_USER_DIRTY_PAGES, payload)

    assert len(result) == argsz

    dirty_pages, result = vfio_user_dirty_pages.pop_from_buffer(result)

    assert dirty_pages.argsz == argsz
    assert dirty_pages.flags == VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP

    br, result = vfio_user_bitmap_range.pop_from_buffer(result)

    assert br.iova == 0x10000
    assert br.size == 0x10000

    assert br.bitmap.pgsize == 0x1000
    assert br.bitmap.size == 8


def get_dirty_page_bitmap():
    argsz = len(vfio_user_dirty_pages()) + len(vfio_user_bitmap_range()) + 8

    dirty_pages = vfio_user_dirty_pages(argsz=argsz,
        flags=VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP)
    bitmap = vfio_user_bitmap(pgsize=0x1000, size=8)
    br = vfio_user_bitmap_range(iova=0x10000, size=0x10000, bitmap=bitmap)

    payload = bytes(dirty_pages) + bytes(br)

    result = msg(ctx, sock, VFIO_USER_DIRTY_PAGES, payload)

    dirty_pages, result = vfio_user_dirty_pages.pop_from_buffer(result)
    br, result = vfio_user_bitmap_range.pop_from_buffer(result)
    return struct.unpack("Q", result)[0]


sg3 = None
iovec3 = None


def test_dirty_pages_get_modified():
    ret, sg1 = vfu_addr_to_sg(ctx, dma_addr=0x10000, length=0x1000)
    assert ret == 1
    iovec1 = iovec_t()
    ret = vfu_map_sg(ctx, sg1, iovec1)
    assert ret == 0

    ret, sg2 = vfu_addr_to_sg(ctx, dma_addr=0x11000, length=0x1000,
                              prot=mmap.PROT_READ)
    assert ret == 1
    iovec2 = iovec_t()
    ret = vfu_map_sg(ctx, sg2, iovec2)
    assert ret == 0

    global sg3, iovec3
    ret, sg3 = vfu_addr_to_sg(ctx, dma_addr=0x14000, length=0x4000)
    assert ret == 1
    iovec3 = iovec_t()
    ret = vfu_map_sg(ctx, sg3, iovec3)
    assert ret == 0

    bitmap = get_dirty_page_bitmap()
    assert bitmap == 0b11110001

    # unmap segment, dirty bitmap should be the same
    vfu_unmap_sg(ctx, sg1, iovec1)
    bitmap = get_dirty_page_bitmap()
    assert bitmap == 0b11110001

    # check again, previously unmapped segment should be clean
    bitmap = get_dirty_page_bitmap()
    assert bitmap == 0b11110000


def stop_logging():
    payload = vfio_user_dirty_pages(argsz=len(vfio_user_dirty_pages()),
                                    flags=VFIO_IOMMU_DIRTY_PAGES_FLAG_STOP)

    msg(ctx, sock, VFIO_USER_DIRTY_PAGES, payload)


def test_dirty_pages_stop():
    stop_logging()

    # one segment is still mapped, starting logging again and bitmap should be
    # dirty
    start_logging()
    assert get_dirty_page_bitmap() == 0b11110000

    # unmap segment, bitmap should still be dirty
    vfu_unmap_sg(ctx, sg3, iovec3)
    assert get_dirty_page_bitmap() == 0b11110000

    # bitmap should be clear after it was unmapped before previous reqeust for
    # dirty pages
    assert get_dirty_page_bitmap() == 0b00000000

    # FIXME we have a memory leak as we don't free dirty bitmaps when
    # destroying the context.
    stop_logging()


def test_dirty_pages_cleanup():
    disconnect_client(ctx, sock)
    vfu_destroy_ctx(ctx)

# ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab:
