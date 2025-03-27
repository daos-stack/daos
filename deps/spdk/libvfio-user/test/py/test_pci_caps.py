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
import ctypes as c
import errno

ctx = None


def setup_function(function):
    global ctx

    ctx = vfu_create_ctx(flags=LIBVFIO_USER_FLAG_ATTACH_NB)
    assert ctx is not None
    ret = vfu_setup_device_reset_cb(ctx)
    assert ret == 0
    vfu_setup_device_quiesce_cb(ctx)


def teardown_function(function):
    vfu_destroy_ctx(ctx)


def setup_pci_dev(config_space=True, realize=False):
    global ctx
    ret = vfu_pci_init(ctx, pci_type=VFU_PCI_TYPE_CONVENTIONAL)
    assert ret == 0
    if config_space:
        ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_CFG_REGION_IDX,
                               size=PCI_CFG_SPACE_SIZE,
                               flags=VFU_REGION_FLAG_RW)
        assert ret == 0
    if realize:
        ret = vfu_realize_ctx(ctx)
        assert ret == 0


def test_pci_cap_bad_flags():
    """Tests adding a PCI capability with bad VFU_CAP_FLAG_ flags."""
    setup_pci_dev()
    pos = vfu_pci_add_capability(ctx, pos=0, flags=999,
              data=struct.pack("ccHH", to_byte(PCI_CAP_ID_PM), b'\0', 0, 0))
    assert pos == -1
    assert c.get_errno() == errno.EINVAL


def test_pci_cap_no_cb():
    """
    Tests adding a PCI capability VFU_CAP_FLAG_CALLBACK without a callback.
    """
    setup_pci_dev(config_space=False)
    pos = vfu_pci_add_capability(ctx, pos=0, flags=VFU_CAP_FLAG_CALLBACK,
              data=struct.pack("ccHH", to_byte(PCI_CAP_ID_PM), b'\0', 0, 0))
    assert pos == -1
    assert c.get_errno() == errno.EINVAL


def test_pci_cap_unknown_cap():
    """Tests adding an unknown PCI capability."""
    setup_pci_dev()
    pos = vfu_pci_add_capability(ctx, pos=0, flags=0,
              data=struct.pack("ccHH", b'\x81', b'\0', 0, 0))
    assert pos == -1
    assert c.get_errno() == errno.ENOTSUP


def test_pci_cap_bad_pos():
    """Tests adding a PCI capability at an invalid position."""
    setup_pci_dev()
    pos = vfu_pci_add_capability(ctx, pos=PCI_CFG_SPACE_SIZE, flags=0,
              data=struct.pack("ccHH", to_byte(PCI_CAP_ID_PM), b'\0', 0, 0))
    assert pos == -1
    assert c.get_errno() == errno.EINVAL


def __pci_region_cb(ctx, buf, count, offset, is_write):
    if not is_write:
        return read_pci_cfg_space(ctx, buf, count, offset)

    return write_pci_cfg_space(ctx, buf, count, offset)


cap_offsets = (
    PCI_STD_HEADER_SIZEOF,
    PCI_STD_HEADER_SIZEOF + PCI_PM_SIZEOF,
    # NB: note 4-byte alignment of vsc2
    PCI_STD_HEADER_SIZEOF + PCI_PM_SIZEOF + 8,
    0x80,
    0x90,
    0xa0
)


@patch("libvfio_user.pci_region_cb", side_effect=__pci_region_cb)
def test_add_caps(mock_pci_region_cb):
    setup_pci_dev()
    pos = vfu_pci_add_capability(ctx, pos=0, flags=0,
              data=struct.pack("ccHH", to_byte(PCI_CAP_ID_PM), b'\0', 0, 0))
    assert pos == cap_offsets[0]

    data = b"abc"
    cap = struct.pack("ccc%ds" % len(data), to_byte(PCI_CAP_ID_VNDR), b'\0',
                      to_byte(3 + len(data)), data)
    pos = vfu_pci_add_capability(ctx, pos=0, flags=VFU_CAP_FLAG_READONLY,
                                 data=cap)

    assert pos == cap_offsets[1]

    data = b"Hello world."
    cap = struct.pack("ccc%ds" % len(data), to_byte(PCI_CAP_ID_VNDR), b'\0',
                      to_byte(3 + len(data)), data)

    pos = vfu_pci_add_capability(ctx, pos=0, flags=VFU_CAP_FLAG_CALLBACK,
                                 data=cap)
    assert pos == cap_offsets[2]

    pos = vfu_pci_add_capability(ctx, pos=cap_offsets[3], flags=0, data=cap)
    assert pos == cap_offsets[3]

    pos = vfu_pci_add_capability(ctx, pos=cap_offsets[4], flags=0, data=cap)
    assert pos == cap_offsets[4]

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    __test_find_caps()

    sock = connect_client(ctx)

    __test_pci_cap_write_hdr(sock)
    __test_pci_cap_readonly(sock)
    __test_pci_cap_callback(sock)
    __test_pci_cap_write_pmcs(sock)


def __test_find_caps():
    offset = vfu_pci_find_capability(ctx, False, PCI_CAP_ID_PM)
    assert offset == cap_offsets[0]

    space = get_pci_cfg_space(ctx)

    assert space[offset] == PCI_CAP_ID_PM
    assert space[offset + PCI_CAP_LIST_NEXT] == cap_offsets[1]

    offset = vfu_pci_find_next_capability(ctx, False, offset, PCI_CAP_ID_PM)
    assert offset == 0

    offset = vfu_pci_find_capability(ctx, False, PCI_CAP_ID_VNDR)
    assert offset == cap_offsets[1]
    assert space[offset] == PCI_CAP_ID_VNDR
    assert space[offset + PCI_CAP_LIST_NEXT] == cap_offsets[2]

    offset = vfu_pci_find_next_capability(ctx, False, offset, PCI_CAP_ID_PM)
    assert offset == 0

    offset = vfu_pci_find_next_capability(ctx, False, 0, PCI_CAP_ID_VNDR)
    assert offset == cap_offsets[1]
    assert space[offset] == PCI_CAP_ID_VNDR
    assert space[offset + PCI_CAP_LIST_NEXT] == cap_offsets[2]

    offset = vfu_pci_find_next_capability(ctx, False, offset, PCI_CAP_ID_VNDR)
    assert offset == cap_offsets[2]
    assert space[offset] == PCI_CAP_ID_VNDR
    assert space[offset + PCI_CAP_LIST_NEXT] == cap_offsets[3]

    offset = vfu_pci_find_next_capability(ctx, False, offset, PCI_CAP_ID_VNDR)
    assert offset == cap_offsets[3]
    offset = vfu_pci_find_next_capability(ctx, False, offset, PCI_CAP_ID_VNDR)
    assert offset == cap_offsets[4]
    offset = vfu_pci_find_next_capability(ctx, False, offset, PCI_CAP_ID_VNDR)
    assert offset == 0

    # check for invalid offsets

    offset = vfu_pci_find_next_capability(ctx, False, 8192, PCI_CAP_ID_PM)
    assert offset == 0
    assert c.get_errno() == errno.EINVAL

    offset = vfu_pci_find_next_capability(ctx, False, 256, PCI_CAP_ID_PM)
    assert offset == 0
    assert c.get_errno() == errno.EINVAL

    offset = vfu_pci_find_next_capability(ctx, False, 255, PCI_CAP_ID_PM)
    assert offset == 0
    assert c.get_errno() == errno.EINVAL

    offset = vfu_pci_find_next_capability(ctx, False,
                                          PCI_STD_HEADER_SIZEOF +
                                          PCI_PM_SIZEOF + 1,
                                          PCI_CAP_ID_VNDR)
    assert offset == 0
    assert c.get_errno() == errno.ENOENT


def __test_pci_cap_write_hdr(sock):
    # offset of struct cap_hdr
    offset = cap_offsets[0]
    data = b'\x01'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data, expect=errno.EPERM)


def __test_pci_cap_readonly(sock):
    # start of vendor payload
    offset = cap_offsets[1] + 2
    data = b'\x01'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data, expect=errno.EPERM)

    # offsetof(struct vsc, data)
    offset = cap_offsets[1] + 3
    payload = read_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                          count=3)
    assert payload == b'abc'


def __test_pci_cap_callback(sock):
    # offsetof(struct vsc, data)
    offset = cap_offsets[2] + 3
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


def __test_pci_cap_write_pmcs(sock):

    # struct pc

    offset = cap_offsets[0] + 3
    data = b'\x01\x02'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data, expect=errno.EINVAL)

    offset = cap_offsets[0] + 2
    data = b'\x01'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data, expect=errno.EINVAL)

    offset = cap_offsets[0] + 2
    data = b'\x01\x02'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data, expect=errno.ENOTSUP)

    # struct pmcs

    offset = cap_offsets[0] + 5
    data = b'\x01\x02'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data, expect=errno.EINVAL)

    offset = cap_offsets[0] + 4
    data = b'\x01'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data, expect=errno.EINVAL)

    offset = cap_offsets[0] + 4
    data = b'\x01\x02'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data)

    assert get_pci_cfg_space(ctx)[offset:offset+2] == data

    # pmcsr_se
    offset = cap_offsets[0] + 6
    data = b'\x01'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data, expect=errno.ENOTSUP)

    # data
    offset = cap_offsets[0] + 7
    data = b'\x01'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data, expect=errno.ENOTSUP)


def _setup_flrc(ctx):
    # flrc
    cap = struct.pack("ccHHcc52c", to_byte(PCI_CAP_ID_EXP), b'\0', 0, 0, b'\0',
                      b'\x10', *[b'\0' for _ in range(52)])
    # FIXME adding capability after we've realized the device only works
    # because of bug #618.
    pos = vfu_pci_add_capability(ctx, pos=0, flags=0, data=cap)
    assert pos == PCI_STD_HEADER_SIZEOF


@patch("libvfio_user.reset_cb", return_value=0)
@patch('libvfio_user.quiesce_cb')
def test_pci_cap_write_px(mock_quiesce, mock_reset):
    """
    Tests function level reset.
    """
    setup_pci_dev(realize=True)
    sock = connect_client(ctx)

    _setup_flrc(ctx)

    # iflr
    offset = PCI_STD_HEADER_SIZEOF + 8
    data = b'\x00\x80'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data)

    mock_quiesce.assert_called_once_with(ctx)
    mock_reset.assert_called_once_with(ctx, VFU_RESET_PCI_FLR)

    # bad access
    for _off in (-1, +1):
        for _len in (-1, +1):
            write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX,
                         offset=offset+_off, count=len(data)+_len, data=data,
                         expect=errno.EINVAL)


def to_bytes_le(n, length=1):
    return n.to_bytes(length, 'little')


def test_pci_cap_write_msix():
    setup_pci_dev(realize=True)
    sock = connect_client(ctx)

    flags = PCI_MSIX_FLAGS_MASKALL | PCI_MSIX_FLAGS_ENABLE
    pos = vfu_pci_add_capability(ctx, pos=0, flags=0,
                                 data=struct.pack("ccHII",
                                                  to_byte(PCI_CAP_ID_MSIX),
                                                  b'\0', 0, 0, 0))
    assert pos == cap_offsets[0]

    offset = vfu_pci_find_capability(ctx, False, PCI_CAP_ID_MSIX)

    # write exactly to Message Control: mask all vectors and enable MSI-X
    data = b'\xff\xff'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX,
                 offset=offset + PCI_MSIX_FLAGS,
                 count=len(data), data=data)
    data = b'\xff\xff' + to_bytes_le(flags, 2)
    payload = read_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                          count=len(data))
    expected = to_bytes_le(PCI_CAP_ID_MSIX) + b'\x00' + \
        to_bytes_le(PCI_MSIX_FLAGS_MASKALL | PCI_MSIX_FLAGS_ENABLE, 2)
    assert expected == payload

    # reset
    data = b'\x00\x00'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX,
                 offset=offset + PCI_MSIX_FLAGS,
                 count=len(data), data=data)
    expected = to_bytes_le(PCI_CAP_ID_MSIX) + b'\x00\x00'
    payload = read_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                          count=len(expected))
    assert expected == payload

    # write 2 bytes to Message Control + 1: mask all vectors and enable MSI-X
    # This looks bizarre, but some versions of QEMU do this.
    data = to_bytes_le(flags >> 8) + b'\xff'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX,
                 offset=offset + PCI_MSIX_FLAGS + 1,
                 count=len(data), data=data)
    # read back entire MSI-X
    expected = to_bytes_le(PCI_CAP_ID_MSIX) + b'\x00' + \
        to_bytes_le(flags, 2) + b'\x00\x00\x00\x00' + b'\x00\x00\x00\x00'
    payload = read_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                          count=PCI_CAP_MSIX_SIZEOF)
    assert expected == payload

    # reset with MSI-X enabled
    data = to_bytes_le(PCI_MSIX_FLAGS_ENABLE, 2)
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX,
                 offset=offset + PCI_MSIX_FLAGS,
                 count=len(data), data=data)

    # write 1 byte past Message Control, MSI-X should still be enabled
    data = b'\x00'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX,
                 offset=offset + PCI_MSIX_TABLE,
                 count=len(data), data=data)
    payload = read_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX,
                          offset=offset + PCI_MSIX_FLAGS, count=2)
    assert payload == to_bytes_le(PCI_MSIX_FLAGS_ENABLE, 2)


def test_pci_cap_write_pxdc2():

    setup_pci_dev(realize=True)
    sock = connect_client(ctx)

    _setup_flrc(ctx)

    offset = (vfu_pci_find_capability(ctx, False, PCI_CAP_ID_EXP) +
              PCI_EXP_DEVCTL2)
    data = b'\xde\xad'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data)
    payload = read_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                          count=len(data))
    assert payload == data


def test_pci_cap_write_pxlc2():

    setup_pci_dev(realize=True)
    _setup_flrc(ctx)

    sock = connect_client(ctx)
    offset = (vfu_pci_find_capability(ctx, False, PCI_CAP_ID_EXP) +
              PCI_EXP_LNKCTL2)
    data = b'\xbe\xef'
    write_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                 count=len(data), data=data)
    payload = read_region(ctx, sock, VFU_PCI_DEV_CFG_REGION_IDX, offset=offset,
                          count=len(data))
    assert payload == data


# ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: #
