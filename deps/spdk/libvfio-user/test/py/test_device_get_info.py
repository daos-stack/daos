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
import errno


def test_device_get_info():
    global ctx

    ctx = vfu_create_ctx(flags=LIBVFIO_USER_FLAG_ATTACH_NB)
    assert ctx is not None

    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_BAR0_REGION_IDX, size=4096,
                           flags=VFU_REGION_FLAG_RW)
    assert ret == 0
    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_BAR1_REGION_IDX, size=4096,
                           flags=(VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM))
    assert ret == 0

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    # test short write

    sock = connect_client(ctx)

    payload = struct.pack("II", 0, 0)

    msg(ctx, sock, VFIO_USER_DEVICE_GET_INFO, payload,
        expect=errno.EINVAL)

    # bad argsz

    payload = vfio_user_device_info(argsz=8, flags=0,
                                    num_regions=0, num_irqs=0)

    msg(ctx, sock, VFIO_USER_DEVICE_GET_INFO, payload,
        expect=errno.EINVAL)

    # valid with larger argsz

    payload = vfio_user_device_info(argsz=32, flags=0,
                                    num_regions=0, num_irqs=0)

    result = msg(ctx, sock, VFIO_USER_DEVICE_GET_INFO, payload)

    (argsz, flags, num_regions, num_irqs) = struct.unpack("IIII", result)

    assert argsz == 16
    assert flags == VFIO_DEVICE_FLAGS_PCI | VFIO_DEVICE_FLAGS_RESET
    assert num_regions == VFU_PCI_DEV_NUM_REGIONS
    assert num_irqs == VFU_DEV_NUM_IRQS

    disconnect_client(ctx, sock)

    vfu_destroy_ctx(ctx)

# ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: #
