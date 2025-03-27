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
import ctypes as c
import errno

ctx = None


def test_pci_ext_cap_conventional():
    ctx = vfu_create_ctx(flags=LIBVFIO_USER_FLAG_ATTACH_NB)
    assert ctx is not None

    ret = vfu_pci_init(ctx, pci_type=VFU_PCI_TYPE_CONVENTIONAL)
    assert ret == 0

    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_CFG_REGION_IDX,
                           size=PCI_CFG_SPACE_SIZE,
                           flags=VFU_REGION_FLAG_RW)
    assert ret == 0

    # struct dsncap
    cap = struct.pack("HHII", PCI_EXT_CAP_ID_DSN, 0, 0, 0)

    pos = vfu_pci_add_capability(ctx, pos=0, flags=VFU_CAP_FLAG_EXTENDED,
                                 data=cap)
    assert pos == -1
    assert c.get_errno() == errno.EINVAL

    vfu_destroy_ctx(ctx)


def test_pci_ext_cap_setup():
    global ctx

    ctx = vfu_create_ctx(flags=LIBVFIO_USER_FLAG_ATTACH_NB)
    assert ctx is not None

    ret = vfu_pci_init(ctx)
    assert ret == 0

    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_CFG_REGION_IDX,
                           size=PCI_CFG_SPACE_EXP_SIZE, cb=pci_region_cb,
                           flags=VFU_REGION_FLAG_RW)
    assert ret == 0


def test_pci_ext_cap_unknown_cap():
    cap = struct.pack("HHII", PCI_EXT_CAP_ID_DSN + 99, 0, 0, 0)

    pos = vfu_pci_add_capability(ctx, pos=0, flags=VFU_CAP_FLAG_EXTENDED,
                                 data=cap)
    assert pos == -1
    assert c.get_errno() == errno.ENOTSUP


def test_pci_ext_cap_bad_pos():
    cap = struct.pack("HHII", PCI_EXT_CAP_ID_DSN, 0, 0, 0)

    pos = vfu_pci_add_capability(ctx, pos=(PCI_CFG_SPACE_EXP_SIZE - 2),
                                 flags=VFU_CAP_FLAG_EXTENDED, data=cap)
    assert pos == -1
    assert c.get_errno() == errno.EINVAL

    # first cap must be at 256
    pos = vfu_pci_add_capability(ctx, pos=512,
                                 flags=VFU_CAP_FLAG_EXTENDED, data=cap)
    assert pos == -1
    assert c.get_errno() == errno.EINVAL


@vfu_region_access_cb_t
def pci_region_cb(ctx, buf, count, offset, is_write):
    if not is_write:
        return read_pci_cfg_space(ctx, buf, count, offset, extended=True)

    return write_pci_cfg_space(ctx, buf, count, offset, extended=True)


cap_offsets = (
    PCI_CFG_SPACE_SIZE,
    PCI_CFG_SPACE_SIZE + PCI_EXT_CAP_DSN_SIZEOF,
    PCI_CFG_SPACE_SIZE + PCI_EXT_CAP_DSN_SIZEOF +
        PCI_EXT_CAP_VNDR_HDR_SIZEOF + 8,
    512,
    600
)


def test_add_ext_caps():
    cap = struct.pack("HHII", PCI_EXT_CAP_ID_DSN, 0, 4, 8)

    pos = vfu_pci_add_capability(ctx, pos=0, flags=VFU_CAP_FLAG_EXTENDED,
                                 data=cap)
    assert pos == cap_offsets[0]

    # struct pcie_ext_cap_vsc_hdr
    data = b"abcde"
    cap = struct.pack("HHHH%ds" % len(data), PCI_EXT_CAP_ID_VNDR, 0, 0x1a,
                      (len(data) + 8) << 4, data)

    pos = vfu_pci_add_capability(ctx, pos=0, flags=(VFU_CAP_FLAG_EXTENDED |
                                 VFU_CAP_FLAG_READONLY), data=cap)
    assert pos == cap_offsets[1]

    data = b"Hello world."
    cap = struct.pack("HHHH%ds" % len(data), PCI_EXT_CAP_ID_VNDR, 0, 0x1b,
                      (len(data) + 8) << 4, data)

    pos = vfu_pci_add_capability(ctx, pos=0, flags=(VFU_CAP_FLAG_EXTENDED |
                                 VFU_CAP_FLAG_CALLBACK), data=cap)
    assert pos == cap_offsets[2]

    cap = struct.pack("HHHH%ds" % len(data), PCI_EXT_CAP_ID_VNDR, 0, 0x1c,
                      (len(data) + 8) << 4, data)

    pos = vfu_pci_add_capability(ctx, pos=cap_offsets[3],
                                 flags=(VFU_CAP_FLAG_EXTENDED |
                                 VFU_CAP_FLAG_CALLBACK), data=cap)
    assert pos == cap_offsets[3]

    cap = struct.pack("HHHH%ds" % len(data), PCI_EXT_CAP_ID_VNDR, 0, 0x1d,
                      (len(data) + 8) << 4, data)

    pos = vfu_pci_add_capability(ctx, pos=cap_offsets[4],
                                 flags=(VFU_CAP_FLAG_EXTENDED |
                                 VFU_CAP_FLAG_CALLBACK), data=cap)
    assert pos == cap_offsets[4]

    ret = vfu_realize_ctx(ctx)
    assert ret == 0


def test_find_ext_caps():
    offset = vfu_pci_find_capability(ctx, True, PCI_EXT_CAP_ID_DSN)
    assert offset == cap_offsets[0]

    space = get_pci_ext_cfg_space(ctx)

    cap_id, cap_next = ext_cap_hdr(space, offset)
    assert cap_id == PCI_EXT_CAP_ID_DSN
    assert cap_next == cap_offsets[1]

    offset = vfu_pci_find_next_capability(ctx, True, offset,
                                          PCI_EXT_CAP_ID_DSN)
    assert offset == 0

    offset = vfu_pci_find_capability(ctx, True, PCI_EXT_CAP_ID_VNDR)
    assert offset == cap_offsets[1]
    cap_id, cap_next = ext_cap_hdr(space, offset)
    assert cap_id == PCI_EXT_CAP_ID_VNDR
    assert cap_next == cap_offsets[2]

    offset = vfu_pci_find_next_capability(ctx, True, offset,
                                          PCI_EXT_CAP_ID_DSN)
    assert offset == 0

    offset = vfu_pci_find_next_capability(ctx, True, 0, PCI_EXT_CAP_ID_VNDR)
    assert offset == cap_offsets[1]
    cap_id, cap_next = ext_cap_hdr(space, offset)
    assert cap_id == PCI_EXT_CAP_ID_VNDR
    assert cap_next == cap_offsets[2]

    offset = vfu_pci_find_next_capability(ctx, True, offset,
                                          PCI_EXT_CAP_ID_VNDR)
    assert offset == cap_offsets[2]
    cap_id, cap_next = ext_cap_hdr(space, offset)
    assert cap_id == PCI_EXT_CAP_ID_VNDR
    assert cap_next == cap_offsets[3]

    offset = vfu_pci_find_next_capability(ctx, True, offset,
                                          PCI_EXT_CAP_ID_VNDR)
    assert offset == cap_offsets[3]
    offset = vfu_pci_find_next_capability(ctx, True, offset,
                                          PCI_EXT_CAP_ID_VNDR)
    assert offset == cap_offsets[4]
    offset = vfu_pci_find_next_capability(ctx, True, offset,
                                          PCI_EXT_CAP_ID_VNDR)
    assert offset == 0

    # check for invalid offsets

    offset = vfu_pci_find_next_capability(ctx, True, 8192, PCI_EXT_CAP_ID_DSN)
    assert offset == 0
    assert c.get_errno() == errno.EINVAL
    offset = vfu_pci_find_next_capability(ctx, True, 4096, PCI_EXT_CAP_ID_DSN)
    assert offset == 0
    assert c.get_errno() == errno.EINVAL
    offset = vfu_pci_find_next_capability(ctx, True, 4095, PCI_EXT_CAP_ID_DSN)
    assert offset == 0
    assert c.get_errno() == errno.EINVAL

    offset = vfu_pci_find_next_capability(ctx, True, cap_offsets[1] + 1,
                                          PCI_EXT_CAP_ID_DSN)
    assert offset == 0
    assert c.get_errno() == errno.ENOENT


def test_pci_ext_cap_write_hdr():
    sock = connect_client(ctx)

    # struct pcie_ext_cap_hdr
    offset = cap_offsets[0]
    data = b'\x01'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data, expect=errno.EPERM)

    # struct pcie_ext_cap_vsc_hdr also
    offset = cap_offsets[1] + 4
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data, expect=errno.EPERM)

    disconnect_client(ctx, sock)


def test_pci_ext_cap_readonly():
    sock = connect_client(ctx)

    # start of vendor payload
    offset = cap_offsets[1] + 8
    data = b'\x01'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data, expect=errno.EPERM)

    offset = cap_offsets[1] + 8
    payload = read_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                          count=5)
    assert payload == b'abcde'

    disconnect_client(ctx, sock)


def test_pci_ext_cap_callback():
    sock = connect_client(ctx)

    # start of vendor payload
    offset = cap_offsets[2] + 8
    data = b"Hello world."

    payload = read_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                          count=len(data))
    assert payload == data

    data = b"Bye world."
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data)

    payload = read_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                          count=len(data))
    assert payload == data

    disconnect_client(ctx, sock)


def test_pci_ext_cap_write_dsn():
    sock = connect_client(ctx)

    data = struct.pack("II", 1, 2)
    offset = cap_offsets[0] + 4
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data, expect=errno.EPERM)

    payload = read_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                          count=len(data))

    # unchanged!
    assert payload == struct.pack("II", 4, 8)

    disconnect_client(ctx, sock)


def test_pci_ext_cap_write_vendor():
    sock = connect_client(ctx)

    data = struct.pack("II", 0x1, 0x2)
    # start of vendor payload
    offset = cap_offsets[2] + 8
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data)

    payload = read_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                          count=len(data))

    assert payload == data

    disconnect_client(ctx, sock)


def test_pci_ext_cap_cleanup():
    vfu_destroy_ctx(ctx)

# ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: #
