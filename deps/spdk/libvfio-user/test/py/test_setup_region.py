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

from libvfio_user import *
import ctypes as c
import errno
import tempfile

ctx = None


def test_setup_region_setup():
    global ctx

    ctx = vfu_create_ctx(flags=LIBVFIO_USER_FLAG_ATTACH_NB)
    assert ctx is not None


def test_setup_region_bad_flags():
    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_BAR2_REGION_IDX,
                           size=0x10000, flags=0x400)
    assert ret == -1
    assert c.get_errno() == errno.EINVAL

    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_BAR2_REGION_IDX,
                           size=0x10000, flags=0)
    assert ret == -1
    assert c.get_errno() == errno.EINVAL


def test_setup_region_bad_mmap_areas():

    f = tempfile.TemporaryFile()
    f.truncate(65536)

    mmap_areas = [(0x2000, 0x1000), (0x4000, 0x2000)]

    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_BAR2_REGION_IDX,
                           size=0x10000,
                           flags=(VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM),
                           mmap_areas=mmap_areas, nr_mmap_areas=0,
                           fd=f.fileno())
    assert ret == -1
    assert c.get_errno() == errno.EINVAL

    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_BAR2_REGION_IDX,
                           size=0x10000,
                           flags=(VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM),
                           mmap_areas=None, nr_mmap_areas=1, fd=f.fileno())
    assert ret == -1
    assert c.get_errno() == errno.EINVAL

    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_BAR2_REGION_IDX,
                           size=0x10000,
                           flags=(VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM),
                           mmap_areas=mmap_areas, fd=-1)
    assert ret == -1
    assert c.get_errno() == errno.EINVAL

    mmap_areas = [(0x2000, 0x1000), (0x4000, 0x2000)]

    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_BAR2_REGION_IDX, size=0x5000,
                           flags=(VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM),
                           mmap_areas=mmap_areas, fd=f.fileno())
    assert ret == -1
    assert c.get_errno() == errno.EINVAL


def test_setup_region_bad_index():
    ret = vfu_setup_region(ctx, index=-2, size=0x10000,
                           flags=(VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM))
    assert ret == -1
    assert c.get_errno() == errno.EINVAL

    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_NUM_REGIONS, size=0x10000,
                           flags=(VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM))
    assert ret == -1
    assert c.get_errno() == errno.EINVAL


def test_setup_region_bad_pci():
    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_CFG_REGION_IDX, size=0x1000,
                           flags=(VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM))
    assert ret == -1
    assert c.get_errno() == errno.EINVAL


def test_setup_region_bad_migr():
    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_MIGR_REGION_IDX, size=512,
                           flags=(VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM))
    assert ret == -1
    assert c.get_errno() == errno.EINVAL

    f = tempfile.TemporaryFile()
    f.truncate(0x2000)

    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_MIGR_REGION_IDX, size=0x2000,
                           flags=(VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM),
                           fd=f.fileno())
    assert ret == -1
    assert c.get_errno() == errno.EINVAL

    mmap_areas = [(0x0, 0x1000), (0x1000, 0x1000)]

    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_MIGR_REGION_IDX, size=0x2000,
                           flags=(VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM),
                           mmap_areas=mmap_areas, fd=f.fileno())
    assert ret == -1
    assert c.get_errno() == errno.EINVAL


def test_setup_region_cfg_always_cb_nocb():
    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_CFG_REGION_IDX,
                           size=PCI_CFG_SPACE_EXP_SIZE, cb=None,
                           flags=(VFU_REGION_FLAG_RW |
                                  VFU_REGION_FLAG_ALWAYS_CB))
    assert ret == -1
    assert c.get_errno() == errno.EINVAL


@vfu_region_access_cb_t
def pci_cfg_region_cb(ctx, buf, count, offset, is_write):
    if not is_write:
        for i in range(count):
            buf[i] = 0xcc

    return count


def test_setup_region_cfg_always_cb():
    global ctx

    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_CFG_REGION_IDX,
                           size=PCI_CFG_SPACE_EXP_SIZE, cb=pci_cfg_region_cb,
                           flags=(VFU_REGION_FLAG_RW |
                                  VFU_REGION_FLAG_ALWAYS_CB))
    assert ret == 0

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    sock = connect_client(ctx)

    payload = read_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX,
                          offset=0, count=2)
    assert payload == b'\xcc\xcc'

    disconnect_client(ctx, sock)


def test_region_offset_overflow():
    global ctx

    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_CFG_REGION_IDX,
                           size=PCI_CFG_SPACE_EXP_SIZE, cb=pci_cfg_region_cb,
                           flags=(VFU_REGION_FLAG_RW))
    assert ret == 0

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    sock = connect_client(ctx)

    read_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX,
                offset=UINT64_MAX, count=256, expect=errno.EINVAL)

    disconnect_client(ctx, sock)


def test_access_region_zero_count():
    global ctx

    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_BAR0_REGION_IDX,
                           size=0x1000, flags=VFU_REGION_FLAG_RW)
    assert ret == 0

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    sock = connect_client(ctx)

    payload = read_region(ctx, sock, VFU_PCI_DEV_BAR0_REGION_IDX, offset=0,
                          count=0)
    assert payload == b''

    write_region(ctx, sock, VFU_PCI_DEV_BAR0_REGION_IDX, offset=0, count=0,
                 data=payload)

    disconnect_client(ctx, sock)


def test_access_region_large_count():
    global ctx

    sock = connect_client(ctx)

    read_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=0,
                count=SERVER_MAX_DATA_XFER_SIZE + 8, expect=errno.EINVAL)

    disconnect_client(ctx, sock)


def test_region_offset_too_short():
    global ctx

    sock = connect_client(ctx)

    payload = struct.pack("Q", 0)

    msg(ctx, sock, VFIO_USER_REGION_WRITE, payload,
        expect=errno.EINVAL)

    disconnect_client(ctx, sock)


def test_setup_region_cleanup():
    vfu_destroy_ctx(ctx)

# ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: #
