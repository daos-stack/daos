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

ctx = None
sock = None

argsz = len(vfio_irq_info())


def test_device_get_irq_info_setup():
    global ctx, sock

    ctx = vfu_create_ctx(flags=LIBVFIO_USER_FLAG_ATTACH_NB)
    assert ctx is not None

    ret = vfu_pci_init(ctx)
    assert ret == 0

    ret = vfu_setup_device_nr_irqs(ctx, VFU_DEV_REQ_IRQ, 1)
    assert ret == 0
    ret = vfu_setup_device_nr_irqs(ctx, VFU_DEV_ERR_IRQ, 1)
    assert ret == 0
    ret = vfu_setup_device_nr_irqs(ctx, VFU_DEV_MSIX_IRQ, 2048)
    assert ret == 0

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    sock = connect_client(ctx)


def test_device_get_irq_info_bad_in():
    payload = struct.pack("II", 0, 0)

    msg(ctx, sock, VFIO_USER_DEVICE_GET_IRQ_INFO, payload,
        expect=errno.EINVAL)

    # bad argsz
    payload = vfio_irq_info(argsz=8, flags=0, index=VFU_DEV_REQ_IRQ,
                            count=0)

    msg(ctx, sock, VFIO_USER_DEVICE_GET_IRQ_INFO, payload,
        expect=errno.EINVAL)

    # bad index
    payload = vfio_irq_info(argsz=argsz, flags=0, index=VFU_DEV_NUM_IRQS,
                            count=0)

    msg(ctx, sock, VFIO_USER_DEVICE_GET_IRQ_INFO, payload,
        expect=errno.EINVAL)


def test_device_get_irq_info():

    # valid with larger argsz

    payload = vfio_irq_info(argsz=argsz + 16, flags=0, index=VFU_DEV_REQ_IRQ,
                            count=0)

    msg(ctx, sock, VFIO_USER_DEVICE_GET_IRQ_INFO, payload)

    payload = vfio_irq_info(argsz=argsz, flags=0, index=VFU_DEV_REQ_IRQ,
                            count=0)

    result = msg(ctx, sock, VFIO_USER_DEVICE_GET_IRQ_INFO, payload)

    info, _ = vfio_irq_info.pop_from_buffer(result)

    assert info.argsz == argsz
    assert info.flags == VFIO_IRQ_INFO_EVENTFD
    assert info.index == VFU_DEV_REQ_IRQ
    assert info.count == 1

    payload = vfio_irq_info(argsz=argsz, flags=0, index=VFU_DEV_ERR_IRQ,
                            count=0)

    result = msg(ctx, sock, VFIO_USER_DEVICE_GET_IRQ_INFO, payload)

    info, _ = vfio_irq_info.pop_from_buffer(result)

    assert info.argsz == argsz
    assert info.flags == VFIO_IRQ_INFO_EVENTFD
    assert info.index == VFU_DEV_ERR_IRQ
    assert info.count == 1

    payload = vfio_irq_info(argsz=argsz, flags=0, index=VFU_DEV_MSIX_IRQ,
                            count=0)

    result = msg(ctx, sock, VFIO_USER_DEVICE_GET_IRQ_INFO, payload)

    info, _ = vfio_irq_info.pop_from_buffer(result)

    assert info.argsz == argsz
    assert info.flags == VFIO_IRQ_INFO_EVENTFD
    assert info.index == VFU_DEV_MSIX_IRQ
    assert info.count == 2048


def test_device_get_irq_info_cleanup():
    disconnect_client(ctx, sock)

    vfu_destroy_ctx(ctx)

# ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: #
