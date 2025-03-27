#
# Copyright (c) 2021 Nutanix Inc. All rights reserved.
#
# Author: Thanos Makatos <thanos.makatos@nutanix.com>
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

ctx = None
sock = None

argsz = len(vfio_region_info())


def test_device_get_region_info_setup():
    global ctx, sock

    ctx = vfu_create_ctx(flags=LIBVFIO_USER_FLAG_ATTACH_NB)
    assert ctx is not None

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    sock = connect_client(ctx)


def test_device_get_region_info_zero_sized_region():
    """Tests that a zero-sized region has no caps."""

    global sock

    for index in [VFU_PCI_DEV_BAR1_REGION_IDX, VFU_PCI_DEV_MIGR_REGION_IDX]:
        payload = vfio_region_info(argsz=argsz, flags=0,
                              index=index, cap_offset=0,
                              size=0, offset=0)

        hdr = vfio_user_header(VFIO_USER_DEVICE_GET_REGION_INFO,
                               size=len(payload))
        sock.send(hdr + payload)
        vfu_run_ctx(ctx)
        result = get_reply(sock)

        assert(len(result) == argsz)

        info, _ = vfio_region_info.pop_from_buffer(result)

        assert info.argsz == argsz
        assert info.flags == 0
        assert info.index == index
        assert info.cap_offset == 0
        assert info.size == 0
        assert info.offset == 0

    vfu_destroy_ctx(ctx)

# ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: #
